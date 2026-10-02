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
enum
{
    V1_CR1 = 0,
    V1_CR2 = 4,
    V1_OAR1 = 8,
    V1_DR = 16,
    V1_SR1 = 20,
    V1_SR2 = 24,
    V1_CCR = 28,
    V1_TRISE = 32,
    V1_FLTR = 36
};

enum
{
    V2_CR1 = 0,
    V2_CR2 = 4,
    V2_TIMINGR = 16,
    V2_ISR = 24,
    V2_ICR = 28,
    V2_RXDR = 36,
    V2_TXDR = 40
};

/**
 * @brief Read one volatile peripheral register.
 */
static inline uint32_t i2c_read(uintptr_t address)
{
    return *(volatile uint32_t *)address;
}

/**
 * @brief Write one volatile peripheral register.
 */
static inline void i2c_write(uintptr_t address, uint32_t value)
{
    *(volatile uint32_t *)address = value;
}

/**
 * @brief Update selected bits; caller supplies critical-section protection.
 */
static inline void i2c_modify(uintptr_t address, uint32_t clear, uint32_t set)
{
    i2c_write(address, (i2c_read(address) & ~clear) | set);
}

/**
 * @brief Hardware progress only; synchronization and deadlines belong to core.
 */
typedef struct
{
    uintptr_t base;
    uint32_t baudrate;
    uint32_t saved_selector;
    dmi2c_completion_t completion;
    void *user;
    const dmi2c_message_t *message;
    size_t position;
    bool last;
    bool active;
    bool arbitration_lost;
} stm32_i2c_t;

/**
 * @brief Initialize family hardware descriptors.
 */
void stm32_i2c_init(void);
/**
 * @brief Release every claimed hardware instance.
 */
void stm32_i2c_deinit(void);
/**
 * @brief Dispatch the event/error IRQ for one physical controller.
 */
void stm32_i2c_irq(uint8_t instance);
/**
 * @brief Stop peripheral IRQs before returning a message completion.
 */
void stm32_i2c_complete(stm32_i2c_t *state, int result);
/**
 * @brief Program F4 clock/filter registers.
 */
int i2c_v1_configure(uintptr_t base, uint32_t clock, uint32_t baudrate);
/**
 * @brief Program F7 clock/filter registers.
 */
int i2c_v2_configure(uintptr_t base, uint32_t clock, uint32_t baudrate);
/**
 * @brief Arm F4 hardware for a single message.
 */
void i2c_v1_start(stm32_i2c_t *state);
/**
 * @brief Arm F7 hardware for a single message.
 */
void i2c_v2_start(stm32_i2c_t *state);
/**
 * @brief Advance one F4 peripheral event without waiting.
 */
void i2c_v1_irq(stm32_i2c_t *state);
/**
 * @brief Advance one F7 peripheral event without waiting.
 */
void i2c_v2_irq(stm32_i2c_t *state);

#endif // STM32_I2C_COMMON_H
