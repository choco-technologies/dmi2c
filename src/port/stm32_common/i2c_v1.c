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

int i2c_v1_configure(uintptr_t base, uint32_t clock, uint32_t baudrate)
{
    uint32_t mhz = clock / 1000000;
    if (clock < 2000000 || clock > 50000000 ||
        (baudrate == 400000 && clock < 4000000)) return -ERANGE;
    uint32_t divisor = (baudrate == 100000 ? 2 : 3) * baudrate;
    uint32_t ccr = (clock + divisor - 1) / divisor;
    if (ccr < (baudrate == 100000 ? 4U : 1U) || ccr > 4095) return -ERANGE;
    i2c_write(base + V1_CR1, 0);
    i2c_write(base + V1_CR2, mhz);
    i2c_write(base + V1_OAR1, BIT(14));
    i2c_write(base + V1_CCR, ccr | (baudrate == 400000 ? BIT(15) : 0));
    i2c_write(base + V1_TRISE, baudrate == 100000 ? mhz + 1 : mhz * 3 / 10 + 1);
    i2c_write(base + V1_FLTR, 0); /* Analog filter on, digital filter off */
    i2c_write(base + V1_CR1, PE | ACK);
    return (i2c_read(base + V1_CR1) & PE) ? 0 : -EIO;
}
static int error_status(i2c_transaction_t *t)
{
    uint32_t sr = i2c_read(t->base + V1_SR1);
    if (sr & ARLO) return -EAGAIN;
    if (sr & AF) return -ENXIO;
    if (sr & BERR) return -EIO;
    if (sr & OVR) return -EOVERFLOW;
    return 0;
}
static int wait_flag(i2c_transaction_t *t, uint32_t reg, uint32_t mask, bool set)
{
    for (;;)
    {
        int rc = error_status(t);
        if (rc) return rc;
        if (i2c_expired(t)) return -ETIMEDOUT;
        if (((i2c_read(t->base + reg) & mask) != 0) == set) return 0;
    }
}
static void clear_address(i2c_transaction_t *t)
{
    (void)i2c_read(t->base + V1_SR1);
    (void)i2c_read(t->base + V1_SR2);
}
static void finish_message(i2c_transaction_t *t, bool last)
{
    i2c_modify(t->base + V1_CR1, 0, last ? STOP : START);
}
static int send_address(i2c_transaction_t *t, const dmi2c_message_t *m)
{
    i2c_modify(t->base + V1_CR1, POS, ACK);
    if (!(i2c_read(t->base + V1_CR1) & START) &&
        !(i2c_read(t->base + V1_SR1) & SB))
        i2c_modify(t->base + V1_CR1, 0, START);
    int rc = wait_flag(t, V1_SR1, SB, true);
    if (rc) return rc;
    i2c_write(t->base + V1_DR, (m->address << 1) | m->read);
    return wait_flag(t, V1_SR1, ADDR, true);
}
static int transmit(i2c_transaction_t *t, const dmi2c_message_t *m, bool last)
{
    clear_address(t);
    for (size_t i = 0; i < m->size; ++i)
    {
        int rc = wait_flag(t, V1_SR1, TXE, true);
        if (rc) return rc;
        i2c_write(t->base + V1_DR, m->data[i]);
    }
    int rc = m->size ? wait_flag(t, V1_SR1, BTF, true) : 0;
    if (!rc && last) finish_message(t, true);
    return rc;
}
static int receive_one(i2c_transaction_t *t, uint8_t *data, bool last)
{
    i2c_modify(t->base + V1_CR1, ACK, 0);
    Dmod_EnterCritical();
    clear_address(t);
    finish_message(t, last);
    Dmod_ExitCritical();
    int rc = wait_flag(t, V1_SR1, RXNE, true);
    if (!rc) *data = i2c_read(t->base + V1_DR);
    return rc;
}
static int receive_last_two(i2c_transaction_t *t, uint8_t *data, bool last)
{
    int rc = wait_flag(t, V1_SR1, BTF, true);
    if (rc) return rc;
    Dmod_EnterCritical();
    finish_message(t, last);
    data[0] = i2c_read(t->base + V1_DR);
    data[1] = i2c_read(t->base + V1_DR);
    Dmod_ExitCritical();
    return 0;
}
static int receive_two(i2c_transaction_t *t, uint8_t *data, bool last)
{
    i2c_modify(t->base + V1_CR1, 0, POS);
    Dmod_EnterCritical();
    clear_address(t);
    i2c_modify(t->base + V1_CR1, ACK, 0);
    Dmod_ExitCritical();
    return receive_last_two(t, data, last);
}
static int receive_many(i2c_transaction_t *t, uint8_t *data, size_t size, bool last)
{
    clear_address(t);
    while (size > 3)
    {
        int rc = wait_flag(t, V1_SR1, RXNE, true);
        if (rc) return rc;
        *data++ = i2c_read(t->base + V1_DR);
        --size;
    }
    int rc = wait_flag(t, V1_SR1, BTF, true);
    if (rc) return rc;
    Dmod_EnterCritical();
    i2c_modify(t->base + V1_CR1, ACK, 0);
    *data++ = i2c_read(t->base + V1_DR);
    Dmod_ExitCritical();
    return receive_last_two(t, data, last);
}
static int message(i2c_transaction_t *t, const dmi2c_message_t *m, bool last)
{
    int rc = send_address(t, m);
    if (rc) return rc;
    if (!m->read) return transmit(t, m, last);
    if (m->size == 1) return receive_one(t, m->data, last);
    if (m->size == 2) return receive_two(t, m->data, last);
    return receive_many(t, m->data, m->size, last);
}
int i2c_v1_transfer(i2c_transaction_t *t, const dmi2c_transfer_t *transfer)
{
    i2c_write(t->base + V1_SR1, 0); /* Clear sticky errors from preceding ARLO */
    int rc = wait_flag(t, V1_SR2, BIT(1), false);
    if (rc) return rc; /* No ownership: do not generate STOP */
    for (size_t i = 0; !rc && i < transfer->count; ++i)
        rc = message(t, &transfer->messages[i], i + 1 == transfer->count);
    if (rc)
    {
        if (rc != -EAGAIN && (i2c_read(t->base + V1_SR2) & BIT(0)))
            finish_message(t, true);
        i2c_write(t->base + V1_SR1, 0);
        i2c_transaction_t cleanup = {t->base, Dmod_GetUptime(), 10};
        while (rc != -EAGAIN && (i2c_read(t->base + V1_CR1) & STOP))
            if (i2c_expired(&cleanup)) break;
    }
    else
    {
        rc = wait_flag(t, V1_CR1, STOP, false);
        i2c_modify(t->base + V1_CR1, POS, ACK);
    }
    return rc;
}
