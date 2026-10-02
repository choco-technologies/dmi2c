/* STM32F4 classic I2C, RM0090: SR1/SR2, CCR/TRISE, ACK/POS tail. */
#include "stm32_common.h"
#define PE BIT(0)
#define START BIT(8)
#define STOP BIT(9)
#define ACK BIT(10)
#define POS BIT(11)
#define SB BIT(0)
#define ADDR BIT(1)
#define BTF BIT(2)
#define RXNE BIT(6)
#define TXE BIT(7)
#define BERR BIT(8)
#define ARLO BIT(9)
#define AF BIT(10)
#define OVR BIT(11)

/**
 * @brief Configure classic I2C with analog filter and bounded SCL rate.
 */
int i2c_v1_configure(uintptr_t base, uint32_t clock, uint32_t baudrate)
{
    uint32_t mhz = clock / 1000000;
    if (clock < 2000000 || clock > 50000000 || (baudrate == 400000 && clock < 4000000))
    {
        return -ERANGE;
    }
    uint32_t divisor = (baudrate == 100000 ? 2 : 3) * baudrate;
    uint32_t ccr = (clock + divisor - 1) / divisor;
    if (ccr < (baudrate == 100000 ? 4U : 1U) || ccr > 4095)
    {
        return -ERANGE;
    }
    i2c_write(base + V1_CR1, 0);
    i2c_write(base + V1_CR2, mhz);
    i2c_write(base + V1_OAR1, BIT(14));
    i2c_write(base + V1_CCR, ccr | (baudrate == 400000 ? BIT(15) : 0));
    i2c_write(base + V1_TRISE, baudrate == 100000 ? mhz + 1 : mhz * 3 / 10 + 1);
    i2c_write(base + V1_FLTR, 0); /* Analog filter on, digital filter off */
    i2c_write(base + V1_CR1, PE | ACK);
    return (i2c_read(base + V1_CR1) & PE) ? 0 : -EIO;
}

/**
 * @brief Clear ADDR by the required SR1/SR2 read sequence.
 */
static void clear_address(stm32_i2c_t *state)
{
    (void)i2c_read(state->base + V1_SR1);
    (void)i2c_read(state->base + V1_SR2);
}

/**
 * @brief Terminate a read or write and notify the transaction scheduler.
 */
static void finish(stm32_i2c_t *state)
{
    i2c_modify(state->base + V1_CR1, 0, state->last ? STOP : START);
    stm32_i2c_complete(state, 0);
}

/**
 * @brief Prepare ACK/POS and buffer IRQs before releasing address stretching.
 */
static void address_ready(stm32_i2c_t *state)
{
    const dmi2c_message_t *message = state->message;
    if (!message->read)
    {
        clear_address(state);
        if (message->size == 0)
        {
            finish(state);
        }
        return;
    }
    Dmod_EnterCritical();
    if (message->size == 1)
    {
        i2c_modify(state->base + V1_CR1, ACK, 0);
        clear_address(state);
        i2c_modify(state->base + V1_CR1, 0, state->last ? STOP : START);
    }
    else if (message->size == 2)
    {
        i2c_modify(state->base + V1_CR1, 0, POS);
        clear_address(state);
        i2c_modify(state->base + V1_CR1, ACK, 0);
    }
    else
    {
        clear_address(state);
    }
    Dmod_ExitCritical();
}

/**
 * @brief Consume RXNE/BTF with the F4 one-, two- and three-byte receive tails.
 */
static void receive(stm32_i2c_t *state, uint32_t status)
{
    size_t remaining = state->message->size - state->position;
    uint8_t *data = state->message->data + state->position;
    if (remaining == 2 && (status & BTF))
    {
        Dmod_EnterCritical();
        i2c_modify(state->base + V1_CR1, 0, state->last ? STOP : START);
        data[0] = i2c_read(state->base + V1_DR);
        data[1] = i2c_read(state->base + V1_DR);
        Dmod_ExitCritical();
        state->position += 2;
        stm32_i2c_complete(state, 0);
    }
    else if (remaining == 3 && (status & BTF))
    {
        Dmod_EnterCritical();
        i2c_modify(state->base + V1_CR1, ACK, 0);
        *data = i2c_read(state->base + V1_DR);
        Dmod_ExitCritical();
        ++state->position;
    }
    else if ((remaining > 3 || remaining == 1) && (status & RXNE))
    {
        *data = i2c_read(state->base + V1_DR);
        ++state->position;
        if (remaining == 4)
        {
            i2c_modify(state->base + V1_CR2, BIT(10), 0);
        }
        if (remaining == 1)
        {
            stm32_i2c_complete(state, 0);
        }
    }
}

/**
 * @brief Feed TXE and finish only after the last byte reaches BTF.
 */
static void transmit(stm32_i2c_t *state, uint32_t status)
{
    if (state->position < state->message->size && (status & TXE))
    {
        i2c_write(state->base + V1_DR, state->message->data[state->position++]);
        if (state->position == state->message->size)
        {
            i2c_modify(state->base + V1_CR2, BIT(10), 0);
        }
    }
    else if (state->position == state->message->size && (status & BTF))
    {
        finish(state);
    }
}

/**
 * @brief Start or resume after repeated START; no task spins on status flags.
 */
void i2c_v1_start(stm32_i2c_t *state)
{
    i2c_write(state->base + V1_SR1, 0);
    i2c_modify(state->base + V1_CR1, POS, ACK);
    uint32_t interrupts = BIT(8) | BIT(9);
    if (!state->message->read || state->message->size == 1 || state->message->size > 3)
    {
        interrupts |= BIT(10);
    }
    if (!(i2c_read(state->base + V1_CR1) & START) && !(i2c_read(state->base + V1_SR1) & SB))
    {
        i2c_modify(state->base + V1_CR1, 0, START);
    }
    i2c_modify(state->base + V1_CR2, BIT(8) | BIT(9) | BIT(10), interrupts);
}

/**
 * @brief Handle one event/error snapshot; errors take priority over payload.
 */
void i2c_v1_irq(stm32_i2c_t *state)
{
    uint32_t status = i2c_read(state->base + V1_SR1);
    int result = status & ARLO   ? -EAGAIN
                 : status & AF   ? -ENXIO
                 : status & BERR ? -EIO
                 : status & OVR  ? -EOVERFLOW
                                 : 0;
    if (result != 0)
    {
        state->arbitration_lost = result == -EAGAIN;
        i2c_write(state->base + V1_SR1, 0);
        stm32_i2c_complete(state, result);
    }
    else if (status & SB)
    {
        i2c_write(state->base + V1_DR, (state->message->address << 1) | state->message->read);
    }
    else if (status & ADDR)
    {
        address_ready(state);
    }
    else if (state->message->read)
    {
        receive(state, status);
    }
    else
    {
        transmit(state, status);
    }
}
