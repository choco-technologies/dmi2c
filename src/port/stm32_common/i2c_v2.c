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
static uint32_t divide_up(uint64_t value, uint64_t divisor)
{
    return (uint32_t)((value + divisor - 1) / divisor);
}
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
    if (!scldel || scldel > 16 || sdadel > 15 || upper < 0 ||
        sdadel * presc > (uint64_t)upper) return false;
    *out = ((scldel - 1) << 20) | (sdadel << 16);
    return true;
}
static int compute_timing(uint32_t clock, uint32_t baudrate, uint32_t *result)
{
    if (clock < 2000000 || clock > 54000000) return -ERANGE;
    uint64_t tick = 1000000000000ULL / clock;
    uint64_t target = 1000000000000ULL / baudrate;
    uint64_t best = UINT64_MAX;
    bool fast = baudrate == 400000;
    for (uint32_t p = 0; p < 16; ++p)
    {
        uint64_t unit = (p + 1) * tick;
        uint32_t delays;
        if (!timing_delays(tick, unit, fast, &delays)) continue;
        uint32_t low = divide_up(fast ? 1300000 : 4700000, unit);
        uint32_t high = divide_up(fast ? 600000 : 4000000, unit);
        for (uint32_t l = low; l <= 256; ++l)
        {
            uint64_t minimum = l * unit + 100000 + 4 * tick;
            uint32_t h = minimum < target ? divide_up(target - minimum, unit) : 1;
            if (h < high) h = high;
            uint64_t period = minimum + h * unit;
            if (h > 256 || period >= best) continue;
            best = period;
            *result = (p << 28) | delays | ((h - 1) << 8) | (l - 1);
        }
    }
    return best == UINT64_MAX ? -ERANGE : 0;
}
int i2c_v2_configure(uintptr_t base, uint32_t clock, uint32_t baudrate)
{
    uint32_t timing = 0;
    int rc = compute_timing(clock, baudrate, &timing);
    if (rc) return rc;
    i2c_write(base + V2_CR1, 0);
    i2c_write(base + V2_CR2, 0);
    i2c_write(base + V2_TIMINGR, timing);
    i2c_write(base + V2_ICR, CLEAR_FLAGS);
    i2c_write(base + V2_CR1, BIT(0)); /* PE; filters: analog on, digital off */
    return (i2c_read(base + V2_CR1) & BIT(0)) &&
        i2c_read(base + V2_TIMINGR) == timing ? 0 : -EIO;
}
static int wait_flag(i2c_transaction_t *t, uint32_t mask, bool set)
{
    for (;;)
    {
        uint32_t sr = i2c_read(t->base + V2_ISR);
        if (sr & ARLO) return -EAGAIN;
        if (sr & NACKF) return -ENXIO;
        if (sr & BERR) return -EIO;
        if (sr & OVR) return -EOVERFLOW;
        if (i2c_expired(t)) return -ETIMEDOUT;
        if (((sr & mask) != 0) == set) return 0;
    }
}
static int payload(i2c_transaction_t *t, const dmi2c_message_t *m,
                   size_t offset, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        int rc = wait_flag(t, m->read ? RXNE : TXIS, true);
        if (rc) return rc;
        if (m->read) m->data[offset + i] = i2c_read(t->base + V2_RXDR);
        else i2c_write(t->base + V2_TXDR, m->data[offset + i]);
    }
    return 0;
}
static int message(i2c_transaction_t *t, const dmi2c_message_t *m)
{
    size_t offset = 0;
    do
    {
        size_t chunk = m->size - offset;
        if (chunk > 255) chunk = 255;
        bool reload = m->size - offset > chunk;
        uint32_t cr2 = (m->address << 1) | (m->read ? BIT(10) : 0) |
            ((uint32_t)chunk << 16) | (reload ? RELOAD : 0) |
            (offset == 0 ? START : 0);
        i2c_write(t->base + V2_CR2, cr2);
        int rc = payload(t, m, offset, chunk);
        if (rc) return rc;
        rc = wait_flag(t, reload ? TCR : TC, true);
        if (rc) return rc;
        offset += chunk;
    } while (offset < m->size);
    return 0;
}
static void abort_transfer(i2c_transaction_t *t, int rc)
{
    if (rc != -EAGAIN && (i2c_read(t->base + V2_ISR) & BUSY))
    {
        i2c_modify(t->base + V2_CR2, 0, STOP);
        i2c_transaction_t cleanup = {t->base, Dmod_GetUptime(), 10};
        while (!(i2c_read(t->base + V2_ISR) & STOPF))
            if (i2c_expired(&cleanup)) break;
    }
    i2c_write(t->base + V2_ICR, CLEAR_FLAGS);
}
int i2c_v2_transfer(i2c_transaction_t *t, const dmi2c_transfer_t *transfer)
{
    i2c_write(t->base + V2_ICR, CLEAR_FLAGS);
    int rc = wait_flag(t, BUSY, false);
    if (rc) return rc;
    for (size_t i = 0; !rc && i < transfer->count; ++i)
        rc = message(t, &transfer->messages[i]);
    if (!rc)
    {
        i2c_modify(t->base + V2_CR2, 0, STOP);
        rc = wait_flag(t, STOPF, true);
    }
    if (rc) abort_transfer(t, rc);
    else i2c_write(t->base + V2_ICR, CLEAR_FLAGS);
    return rc;
}
