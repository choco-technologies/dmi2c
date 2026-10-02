/* STM32F7 I2C v2, RM0385: TIMINGR, NBYTES/RELOAD, TC/TCR. */
#include "stm32_common.h"
#define TXIS BIT(1)
#define RXNE BIT(2)
#define NACKF BIT(4)
#define STOPF BIT(5)
#define TC BIT(6)
#define TCR BIT(7)
#define BERR BIT(8)
#define ARLO BIT(9)
#define OVR BIT(10)
#define BUSY BIT(15)
#define START BIT(13)
#define STOP BIT(14)
#define RELOAD BIT(24)
#define CLEAR_FLAGS (NACKF | STOPF | BERR | ARLO | OVR)

/* Picosecond arithmetic avoids overclocking caused by rounding tI2CCLK up.
 * Assume specification-maximum rise/fall times and analog filter 50..260 ns.
 * DNF=0. Choose a conservative period even for zero rise/fall delay.
 * Constraints follow RM0385's timing equations (tSCLDEL/tSDADEL/SCLH/SCLL). */
/**
 * @brief Round a positive ratio up for conservative hardware timing.
 */
static uint32_t divide_up(uint64_t value, uint64_t divisor)
{
    return (uint32_t)((value + divisor - 1) / divisor);
}

/**
 * @brief Check SDA hold and SCL setup limits for one prescaler.
 */
static bool timing_delays(uint64_t tick, uint64_t presc, bool fast, uint32_t *out)
{
    int64_t rise = fast ? 300000 : 1000000;
    int64_t fall = 300000;
    int64_t setup = fast ? 100000 : 250000;
    int64_t hold_max = fast ? 900000 : 3450000;
    int64_t lower = fall - 50000 - 3 * (int64_t)tick;
    int64_t upper = hold_max - rise - 260000 - 4 * (int64_t)tick;
    uint32_t scldel = divide_up(rise + setup, presc);
    uint32_t sdadel = lower > 0 ? divide_up(lower, presc) : 0;
    if (!scldel || scldel > 16 || sdadel > 15 || upper < 0 || sdadel * presc > (uint64_t)upper)
    {
        return false;
    }
    *out = ((scldel - 1) << 20) | (sdadel << 16);
    return true;
}

/**
 * @brief Select TIMINGR for analog-filtered standard or fast mode.
 */
static int compute_timing(uint32_t clock, uint32_t baudrate, uint32_t *result)
{
    if (clock < 2000000 || clock > 54000000)
    {
        return -ERANGE;
    }
    uint64_t tick = 1000000000000ULL / clock;
    uint64_t target = 1000000000000ULL / baudrate;
    uint64_t best = UINT64_MAX;
    bool fast = baudrate == 400000;
    for (uint32_t p = 0; p < 16; ++p)
    {
        uint64_t unit = (p + 1) * tick;
        uint32_t delays;
        if (!timing_delays(tick, unit, fast, &delays))
        {
            continue;
        }
        uint32_t low = divide_up(fast ? 1300000 : 4700000, unit);
        uint32_t high = divide_up(fast ? 600000 : 4000000, unit);
        for (uint32_t l = low; l <= 256; ++l)
        {
            uint64_t minimum = l * unit + 100000 + 4 * tick;
            uint32_t h = minimum < target ? divide_up(target - minimum, unit) : 1;
            if (h < high)
            {
                h = high;
            }
            uint64_t period = minimum + h * unit;
            if (h > 256 || period >= best)
            {
                continue;
            }
            best = period;
            *result = (p << 28) | delays | ((h - 1) << 8) | (l - 1);
        }
    }
    return best == UINT64_MAX ? -ERANGE : 0;
}

/**
 * @brief Program the modern I2C peripheral while PE is disabled.
 */
int i2c_v2_configure(uintptr_t base, uint32_t clock, uint32_t baudrate)
{
    uint32_t timing = 0;
    int rc = compute_timing(clock, baudrate, &timing);
    if (rc)
    {
        return rc;
    }
    i2c_write(base + V2_CR1, 0);
    i2c_write(base + V2_CR2, 0);
    i2c_write(base + V2_TIMINGR, timing);
    i2c_write(base + V2_ICR, CLEAR_FLAGS);
    i2c_write(base + V2_CR1, BIT(0)); /* PE; filters: analog on, digital off */
    /* Only PE is read back: TIMINGR is plain read/write storage whose value
     * was just written, and some models of the peripheral (Renode's
     * STM32F7_I2C keeps its fields as tags) read it back as zero. */
    return (i2c_read(base + V2_CR1) & BIT(0)) ? 0 : -EIO;
}

/**
 * @brief Program one NBYTES chunk; only the first chunk sends START.
 */
static void chunk(stm32_i2c_t *state, bool first)
{
    size_t remaining = state->message->size - state->position;
    uint32_t size = remaining > 255 ? 255 : (uint32_t)remaining;
    uint32_t control =
        (state->message->address << 1) | (state->message->read ? BIT(10) : 0) | (size << 16);
    if (remaining > size)
    {
        control |= RELOAD;
    }
    else if (state->last)
    {
        control |= BIT(25); /* AUTOEND: complete on STOPF */
    }
    if (first)
    {
        control |= START;
    }
    i2c_write(state->base + V2_CR2, control);
}

/**
 * @brief Arm error, payload, TC/TCR and STOP interrupts for one message.
 */
void i2c_v2_start(stm32_i2c_t *state)
{
    i2c_write(state->base + V2_ICR, CLEAR_FLAGS);
    chunk(state, true);
    i2c_modify(state->base + V2_CR1, 0,
               BIT(4) | BIT(5) | BIT(6) | BIT(7) | (state->message->read ? BIT(2) : BIT(1)));
}

/**
 * @brief Advance NBYTES/RELOAD and complete only at TC or final STOP.
 */
void i2c_v2_irq(stm32_i2c_t *state)
{
    uint32_t status = i2c_read(state->base + V2_ISR);
    int result = status & ARLO    ? -EAGAIN
                 : status & NACKF ? -ENXIO
                 : status & BERR  ? -EIO
                 : status & OVR   ? -EOVERFLOW
                                  : 0;
    if (result != 0)
    {
        state->arbitration_lost = result == -EAGAIN;
        i2c_write(state->base + V2_ICR, CLEAR_FLAGS);
        stm32_i2c_complete(state, result);
        return;
    }
    if (state->position < state->message->size)
    {
        if (state->message->read && (status & RXNE))
        {
            state->message->data[state->position++] = i2c_read(state->base + V2_RXDR);
        }
        else if (!state->message->read && (status & TXIS))
        {
            i2c_write(state->base + V2_TXDR, state->message->data[state->position++]);
        }
    }
    if (status & TCR)
    {
        chunk(state, false);
    }
    else if ((status & STOPF) || ((status & TC) && !state->last))
    {
        i2c_write(state->base + V2_ICR, STOPF);
        stm32_i2c_complete(state, state->position == state->message->size ? 0 : -EIO);
    }
}
