#ifndef STM32_I2C_COMMON_H
#define STM32_I2C_COMMON_H
#include "dmi2c_port.h"
#include "dmod.h"
#include <errno.h>

#define RCC_BASE 0x40023800UL
#define RCC_CFGR (RCC_BASE + 0x08)
#define RCC_APB1RSTR (RCC_BASE + 0x20)
#define RCC_APB1ENR (RCC_BASE + 0x40)
#define RCC_DCKCFGR2 (RCC_BASE + 0x90)
#define BIT(n) (1UL << (n))
/* Register offsets are deliberately separate: these are different IPs. */
enum { V1_CR1=0, V1_CR2=4, V1_OAR1=8, V1_DR=16, V1_SR1=20,
       V1_SR2=24, V1_CCR=28, V1_TRISE=32, V1_FLTR=36 };
enum { V2_CR1=0, V2_CR2=4, V2_TIMINGR=16, V2_ISR=24, V2_ICR=28,
       V2_RXDR=36, V2_TXDR=40 };
#ifdef I2C_TEST
uint32_t i2c_read(uintptr_t address);
void i2c_write(uintptr_t address, uint32_t value);
#else
static inline uint32_t i2c_read(uintptr_t address)
{
    return *(volatile uint32_t *)address;
}
static inline void i2c_write(uintptr_t address, uint32_t value)
{
    *(volatile uint32_t *)address = value;
}
#endif
static inline void i2c_modify(uintptr_t address, uint32_t clear, uint32_t set)
{
    i2c_write(address, (i2c_read(address) & ~clear) | set);
}
typedef struct
{
    uintptr_t base;
    Dmod_Timestamp_t start;
    uint32_t timeout_ms;
} i2c_transaction_t;
static inline bool i2c_expired(const i2c_transaction_t *t)
{
    return Dmod_GetUptime() - t->start >= t->timeout_ms;
}
int stm32_i2c_init(bool modern);
int stm32_i2c_deinit(void);
int i2c_v1_configure(uintptr_t base, uint32_t clock, uint32_t baudrate);
int i2c_v2_configure(uintptr_t base, uint32_t clock, uint32_t baudrate);
int i2c_v1_transfer(i2c_transaction_t *t, const dmi2c_transfer_t *transfer);
int i2c_v2_transfer(i2c_transaction_t *t, const dmi2c_transfer_t *transfer);
#endif
