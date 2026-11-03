#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmi2c_port.h"
#include "esp32s3_i2c.h"
#include <errno.h>
#include <string.h>

/*
 * dmi2c_port for the ESP32-S3: the master of the I2C0 / I2C1 controllers (instances 1 and 2).
 *
 * The controller runs a list of up to 8 commands (RSTART, WRITE n, READ n, STOP, END) from its FIFO of 32 bytes. A message is cut into
 * chunks that fit the FIFO; a chunk ends with END (the controller waits with SCL low for the next list: the next chunk of the message
 * or the repeated START of the next one) or, when it ends the last message, with STOP. The interrupt of the end of the list (END_DETECT
 * or TRANS_COMPLETE) takes the received bytes, programs the next chunk or reports the message to the core; an error flag (NACK,
 * arbitration lost, time out) ends the message with the error of the STM32 ports: -ENXIO, -EAGAIN, -EIO.
 *
 * The signals are routed to pads of the GPIO matrix (dmi2c_port_set_pins; there are no fixed pads), open-drain with the internal
 * pull-up on (the board's resistors are meant to do the work; the internal ones are weak, 45 kOhm).
 */

typedef struct
{
    dmi2c_completion_t completion;
    void *user;
    const dmi2c_message_t *message;
    size_t position;        /* payload bytes of the message done */
    size_t chunk;           /* payload bytes of the chunk in the controller */
    uint32_t baudrate;
    int scl_pad;
    int sda_pad;
    bool last;
    bool active;
    bool addr_sent;         /* the first chunk (RSTART + address) was programmed */
    bool stop_pending;
    bool arbitration_lost;
} i2c_t;

static i2c_t controllers[ESP32S3_I2C_COUNT];

static inline volatile uint32_t *reg(uintptr_t address)
{
    return (volatile uint32_t *)address;
}

static inline volatile uint32_t *creg(unsigned index, uint32_t offset)
{
    return reg(ESP32S3_I2C_BASE(index) + offset);
}

static inline uint32_t intenable_read(void)
{
    uint32_t v;
    __asm__ __volatile__("rsr.intenable %0" : "=a"(v));
    return v;
}

static inline void intenable_write(uint32_t v)
{
    __asm__ __volatile__("wsr.intenable %0; rsync" ::"a"(v) : "memory");
}

static bool instance_valid(uint8_t instance)
{
    return instance >= 1U && instance <= ESP32S3_I2C_COUNT;
}

/* GPIO0-21 and GPIO38-48; GPIO19/20 (USB) and GPIO26-37 (the flash and the PSRAM of the module) are not for the user */
static bool pad_valid(int pad)
{
    return (pad >= 0 && pad <= 18) || (pad == 21) || (pad >= 38 && pad <= 48);
}

/** A pad as an open-drain GPIO of the matrix with the input on: a bidirectional signal */
static void route_pad(int pad, uint32_t signal)
{
    volatile uint32_t *mux = reg(ESP32S3_IO_MUX_REG((uint32_t)pad));
    *mux = (*mux & ~ESP32S3_IO_MUX_MCU_SEL_M) | (ESP32S3_IO_MUX_FUNC_GPIO << ESP32S3_IO_MUX_MCU_SEL_S) | ESP32S3_IO_MUX_IE |
           ESP32S3_IO_MUX_PU;
    *reg(ESP32S3_GPIO_PIN_REG((uint32_t)pad)) |= ESP32S3_GPIO_PIN_PAD_DRIVER;
    *reg(ESP32S3_GPIO_FUNC_OUT_SEL_REG((uint32_t)pad)) = signal;            /* the output enable is the signal's own */
    *reg(ESP32S3_GPIO_FUNC_IN_SEL_REG(signal)) = ESP32S3_GPIO_IN_SEL_ROUTED | (uint32_t)pad;
}

static void unroute_pad(int pad)
{
    if (pad >= 0)
    {
        *reg(ESP32S3_GPIO_FUNC_OUT_SEL_REG((uint32_t)pad)) = 256UL;         /* the GPIO output register again */
        *reg(ESP32S3_GPIO_PIN_REG((uint32_t)pad)) &= ~ESP32S3_GPIO_PIN_PAD_DRIVER;
    }
}

static void periph_clock(unsigned index, bool enable)
{
    Dmod_EnterCritical();
    if (enable)
        *reg(ESP32S3_PERIP_CLK_EN0_REG) |= ESP32S3_I2C_PERIP_BIT(index);
    else
        *reg(ESP32S3_PERIP_CLK_EN0_REG) &= ~ESP32S3_I2C_PERIP_BIT(index);
    Dmod_ExitCritical();
}

static void periph_reset(unsigned index)
{
    Dmod_EnterCritical();
    *reg(ESP32S3_PERIP_RST_EN0_REG) |= ESP32S3_I2C_PERIP_BIT(index);
    *reg(ESP32S3_PERIP_RST_EN0_REG) &= ~ESP32S3_I2C_PERIP_BIT(index);
    Dmod_ExitCritical();
}

static void cpu_line(unsigned index, bool enable)
{
    uint32_t line = ESP32S3_I2C_CPU_INTR(index);
    Dmod_EnterCritical();
    if (enable)
    {
        *reg(ESP32S3_INTMTX_I2C_MAP_REG(index)) = line;
        intenable_write(intenable_read() | (1UL << line));
    }
    else
    {
        intenable_write(intenable_read() & ~(1UL << line));
    }
    Dmod_ExitCritical();
}

static void fifo_reset(unsigned index)
{
    *creg(index, I2C_FIFO_CONF_REG) |= I2C_FIFO_RX_RST | I2C_FIFO_TX_RST;
    *creg(index, I2C_FIFO_CONF_REG) &= ~(I2C_FIFO_RX_RST | I2C_FIFO_TX_RST);
}

static uint32_t bit_length(uint32_t value)
{
    uint32_t n = 0;
    while (value != 0U)
    {
        n++;
        value >>= 1;
    }
    return n;
}

/* The clock and the timing of the bus from the XTAL (the same arithmetic as the HAL of ESP-IDF: bus frequency -> divider, periods) */
static void bus_timing(unsigned index, uint32_t bus_hz)
{
    uint32_t clkm_div = ESP32S3_I2C_SOURCE_CLOCK_HZ / (bus_hz * 1024U) + 1U;
    uint32_t sclk = ESP32S3_I2C_SOURCE_CLOCK_HZ / clkm_div;
    uint32_t half = sclk / bus_hz / 2U;
    uint32_t wait_high = bus_hz >= 80000U ? half / 2U - 2U : half / 4U;
    uint32_t high = half - wait_high;
    uint32_t hold = half / 4U;
    uint32_t sample = half / 2U;
    uint32_t tout = bit_length(5U * half) + 2U;

    *creg(index, I2C_CLK_CONF_REG) = (clkm_div - 1U) | I2C_CLK_SCLK_ACTIVE;          /* source 0: the XTAL, no fraction */
    *creg(index, I2C_SCL_LOW_PERIOD_REG) = half - 1U;
    *creg(index, I2C_SCL_HIGH_PERIOD_REG) = high | (wait_high << I2C_SCL_WAIT_HIGH_S);
    *creg(index, I2C_SDA_HOLD_REG) = hold - 1U;
    *creg(index, I2C_SDA_SAMPLE_REG) = sample - 1U;
    *creg(index, I2C_SCL_RSTART_SETUP_REG) = half - 1U;
    *creg(index, I2C_SCL_STOP_SETUP_REG) = half - 1U;
    *creg(index, I2C_SCL_START_HOLD_REG) = half - 1U;
    *creg(index, I2C_SCL_STOP_HOLD_REG) = half - 1U;
    *creg(index, I2C_TO_REG) = (tout & I2C_TO_VALUE_M) | I2C_TO_EN;
    *creg(index, I2C_FILTER_CFG_REG) = 7U | (7U << 4) | I2C_FILTER_SCL_EN | I2C_FILTER_SDA_EN;
}

/* The controller from a reset: master, the bus timing, the FIFOs empty, the interrupts off and clear */
static void hw_setup(unsigned index)
{
    i2c_t *s = &controllers[index];

    periph_clock(index, true);
    periph_reset(index);
    *creg(index, I2C_CTR_REG) = I2C_CTR_MS_MODE | I2C_CTR_CLK_EN | I2C_CTR_SDA_FORCE_OUT | I2C_CTR_SCL_FORCE_OUT;
    *creg(index, I2C_INT_ENA_REG) = 0;
    bus_timing(index, s->baudrate);
    fifo_reset(index);
    *creg(index, I2C_INT_CLR_REG) = I2C_INT_ALL;
    *creg(index, I2C_CTR_REG) |= I2C_CTR_CONF_UPGATE;
}

/* Programs the next chunk of the message into the controller and starts it */
static void program_chunk(unsigned index)
{
    i2c_t *s = &controllers[index];
    const dmi2c_message_t *m = s->message;
    size_t remaining = m->size - s->position;
    bool first = !s->addr_sent;
    uint32_t cmds[I2C_COMMANDS];
    unsigned n = 0;

    fifo_reset(index);
    if (first)
    {
        cmds[n++] = I2C_CMD(I2C_OP_RSTART, 0, 0, 0, 0);
    }
    if (!m->read)
    {
        size_t room = ESP32S3_I2C_FIFO_LEN - (first ? 1U : 0U);
        s->chunk = remaining < room ? remaining : room;
        if (first)
            *creg(index, I2C_DATA_REG) = (uint32_t)(m->address << 1);
        for (size_t i = 0; i < s->chunk; i++)
            *creg(index, I2C_DATA_REG) = m->data[s->position + i];
        cmds[n++] = I2C_CMD(I2C_OP_WRITE, s->chunk + (first ? 1U : 0U), 1, 0, 0);       /* the target has to ACK each byte */
    }
    else
    {
        if (first)
        {
            *creg(index, I2C_DATA_REG) = (uint32_t)((m->address << 1) | 1U);
            cmds[n++] = I2C_CMD(I2C_OP_WRITE, 1, 1, 0, 0);
        }
        s->chunk = remaining < ESP32S3_I2C_FIFO_LEN ? remaining : ESP32S3_I2C_FIFO_LEN;
        if (s->chunk == remaining)
        {
            /* the last byte of the message is answered with NACK */
            if (s->chunk > 1U)
                cmds[n++] = I2C_CMD(I2C_OP_READ, s->chunk - 1U, 0, 0, 0);
            cmds[n++] = I2C_CMD(I2C_OP_READ, 1, 0, 0, 1);
        }
        else
        {
            cmds[n++] = I2C_CMD(I2C_OP_READ, s->chunk, 0, 0, 0);
        }
    }
    s->addr_sent = true;
    cmds[n++] = (s->position + s->chunk == m->size && s->last) ? I2C_CMD(I2C_OP_STOP, 0, 0, 0, 0) : I2C_CMD(I2C_OP_END, 0, 0, 0, 0);
    for (unsigned i = 0; i < n; i++)
        *creg(index, I2C_COMD_REG(i)) = cmds[i];

    *creg(index, I2C_INT_CLR_REG) = I2C_INT_ALL;
    *creg(index, I2C_INT_ENA_REG) = I2C_INT_END_DETECT | I2C_INT_TRANS_COMPLETE | I2C_INT_NACK | I2C_INT_ARBITRATION_LOST |
                                    I2C_INT_TIME_OUT | I2C_INT_SCL_ST_TO | I2C_INT_SCL_MAIN_ST_TO | I2C_INT_MST_TXFIFO_UDF |
                                    I2C_INT_RXFIFO_OVF;
    *creg(index, I2C_CTR_REG) |= I2C_CTR_CONF_UPGATE;
    *creg(index, I2C_CTR_REG) |= I2C_CTR_TRANS_START;
}

static void finish(unsigned index, int result)
{
    i2c_t *s = &controllers[index];
    dmi2c_completion_t completion = s->completion;

    *creg(index, I2C_INT_ENA_REG) = 0;
    s->active = false;
    if (completion != NULL)
        completion(s->user, result);
}

static void irq(unsigned index)
{
    i2c_t *s = &controllers[index];
    uint32_t st = *creg(index, I2C_INT_ST_REG);

    *creg(index, I2C_INT_CLR_REG) = st;
    if (!s->active || st == 0U)
    {
        *creg(index, I2C_INT_ENA_REG) = s->active ? *creg(index, I2C_INT_ENA_REG) : 0U;
        return;
    }

    int err = 0;
    if (st & I2C_INT_ARBITRATION_LOST)
    {
        s->arbitration_lost = true;
        err = -EAGAIN;
    }
    else if (st & I2C_INT_NACK)
    {
        err = -ENXIO;
    }
    else if (st & (I2C_INT_TIME_OUT | I2C_INT_SCL_ST_TO | I2C_INT_SCL_MAIN_ST_TO | I2C_INT_MST_TXFIFO_UDF | I2C_INT_RXFIFO_OVF))
    {
        err = -EIO;
    }
    if (err != 0)
    {
        finish(index, err);
        return;
    }

    if (st & (I2C_INT_END_DETECT | I2C_INT_TRANS_COMPLETE))
    {
        const dmi2c_message_t *m = s->message;
        if (m->read)
        {
            for (size_t i = 0; i < s->chunk; i++)
                m->data[s->position + i] = (uint8_t)*creg(index, I2C_DATA_REG);
        }
        s->position += s->chunk;
        if (s->position >= m->size)
            finish(index, 0);
        else
            program_chunk(index);
    }
}

dmod_dmi2c_port_api_declaration(1.0, int, _set_pins, (uint8_t instance, int scl_pin, int sda_pin))
{
    if (!instance_valid(instance) || (scl_pin >= 0 && !pad_valid(scl_pin)) || (sda_pin >= 0 && !pad_valid(sda_pin)))
        return -EINVAL;
    i2c_t *s = &controllers[instance - 1U];
    if (s->completion != NULL)
        return -EBUSY;
    if (scl_pin >= 0)
        s->scl_pad = scl_pin;
    if (sda_pin >= 0)
        s->sda_pad = sda_pin;
    return 0;
}

dmod_dmi2c_port_api_declaration(1.0, int, _init,
                                (uint8_t instance, uint32_t baudrate, dmi2c_completion_t completion, void *user))
{
    if (!instance_valid(instance) || completion == NULL || baudrate == 0U || baudrate > 1000000U)
        return -EINVAL;
    unsigned index = instance - 1U;
    i2c_t *s = &controllers[index];
    if (s->scl_pad < 0 || s->sda_pad < 0)
        return -EINVAL;                                 /* there are no fixed pads: scl_pin and sda_pin are required */

    Dmod_EnterCritical();
    if (s->completion != NULL || (*reg(ESP32S3_PERIP_CLK_EN0_REG) & ESP32S3_I2C_PERIP_BIT(index)))
    {
        Dmod_ExitCritical();
        return -EBUSY;                                  /* ours already, or somebody else's controller */
    }
    s->completion = completion;
    s->user = user;
    s->baudrate = baudrate;
    s->active = false;
    s->stop_pending = false;
    Dmod_ExitCritical();

    hw_setup(index);
    route_pad(s->scl_pad, ESP32S3_I2C_SIG_SCL(index));
    route_pad(s->sda_pad, ESP32S3_I2C_SIG_SDA(index));
    cpu_line(index, true);
    return 0;
}

dmod_dmi2c_port_api_declaration(1.0, void, _deinit, (uint8_t instance))
{
    if (!instance_valid(instance))
        return;
    unsigned index = instance - 1U;
    i2c_t *s = &controllers[index];
    if (s->completion == NULL)
        return;

    cpu_line(index, false);
    *creg(index, I2C_INT_ENA_REG) = 0;
    Dmod_EnterCritical();
    s->active = false;
    s->completion = NULL;
    s->user = NULL;
    s->message = NULL;
    Dmod_ExitCritical();
    unroute_pad(s->scl_pad);
    unroute_pad(s->sda_pad);
    *creg(index, I2C_CTR_REG) = 0;
    periph_reset(index);
    periph_clock(index, false);
    s->scl_pad = -1;
    s->sda_pad = -1;
}

dmod_dmi2c_port_api_declaration(1.0, int, _start, (uint8_t instance, const dmi2c_message_t *message, bool last))
{
    if (!instance_valid(instance) || message == NULL)
        return -EINVAL;
    unsigned index = instance - 1U;
    i2c_t *s = &controllers[index];
    if (s->completion == NULL)
        return -ENODEV;
    if (message->address < 8U || message->address > 119U ||
        (message->size == 0U && message->read) || (message->size != 0U && message->data == NULL))
        return -EINVAL;

    Dmod_EnterCritical();
    if (s->active)
    {
        Dmod_ExitCritical();
        return -EBUSY;
    }
    s->addr_sent = false;                               /* every message begins with its own START (repeated, inside a vector) */
    s->message = message;
    s->position = 0;
    s->chunk = 0;
    s->last = last;
    s->active = true;
    s->arbitration_lost = false;
    Dmod_ExitCritical();

    program_chunk(index);
    return 0;
}

dmod_dmi2c_port_api_declaration(1.0, bool, _is_busy, (uint8_t instance))
{
    if (!instance_valid(instance))
        return false;
    unsigned index = instance - 1U;
    i2c_t *s = &controllers[index];
    if (s->completion == NULL)
        return false;
    if (s->stop_pending && (*creg(index, I2C_INT_RAW_REG) & I2C_INT_TRANS_COMPLETE))
    {
        *creg(index, I2C_INT_CLR_REG) = I2C_INT_TRANS_COMPLETE;
        s->stop_pending = false;
    }
    return s->active || s->stop_pending || (*creg(index, I2C_SR_REG) & I2C_SR_BUS_BUSY) != 0U;
}

dmod_dmi2c_port_api_declaration(1.0, void, _cancel, (uint8_t instance))
{
    if (!instance_valid(instance))
        return;
    unsigned index = instance - 1U;
    i2c_t *s = &controllers[index];
    if (s->completion == NULL)
        return;

    *creg(index, I2C_INT_ENA_REG) = 0;                  /* no access to the borrowed buffer from here on */
    Dmod_EnterCritical();
    s->active = false;
    Dmod_ExitCritical();
    *creg(index, I2C_INT_CLR_REG) = I2C_INT_ALL;

    /* Release the bus with a STOP unless another master took it (arbitration lost) */
    if (!s->arbitration_lost && (*creg(index, I2C_SR_REG) & I2C_SR_BUS_BUSY))
    {
        *creg(index, I2C_CTR_REG) |= I2C_CTR_FSM_RST;
        fifo_reset(index);
        *creg(index, I2C_COMD_REG(0)) = I2C_CMD(I2C_OP_STOP, 0, 0, 0, 0);
        *creg(index, I2C_CTR_REG) |= I2C_CTR_CONF_UPGATE;
        *creg(index, I2C_CTR_REG) |= I2C_CTR_TRANS_START;
        s->stop_pending = true;
    }
}

dmod_dmi2c_port_api_declaration(1.0, int, _recover, (uint8_t instance))
{
    if (!instance_valid(instance))
        return -EINVAL;
    unsigned index = instance - 1U;
    i2c_t *s = &controllers[index];
    if (s->completion == NULL)
        return -ENODEV;

    s->stop_pending = false;
    hw_setup(index);
    return 0;
}

DMOD_IRQ_HANDLER(4)
{
    irq(0);
}

DMOD_IRQ_HANDLER(5)
{
    irq(1);
}

int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    for (unsigned i = 0; i < ESP32S3_I2C_COUNT; i++)
    {
        memset(&controllers[i], 0, sizeof(controllers[i]));
        controllers[i].scl_pad = -1;
        controllers[i].sda_pad = -1;
    }
    return 0;
}

int dmod_deinit(void)
{
    for (unsigned i = 1; i <= ESP32S3_I2C_COUNT; i++)
        dmi2c_port_deinit((uint8_t)i);
    return 0;
}
