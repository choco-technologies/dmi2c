#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "../stm32_common/stm32_common.h"

/**
 * @brief Initialize this family's I2C port module.
 */
int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    stm32_i2c_init();
    return 0;
}

/**
 * @brief Release controllers before disabling the port module.
 */
int dmod_deinit(void)
{
    stm32_i2c_deinit();
    return 0;
}

/**
 * @brief I2C1 event interrupt entry.
 */
DMOD_IRQ_HANDLER(31)
{
    stm32_i2c_irq(1);
}

/**
 * @brief I2C1 error interrupt entry.
 */
DMOD_IRQ_HANDLER(32)
{
    stm32_i2c_irq(1);
}

/**
 * @brief I2C2 event interrupt entry.
 */
DMOD_IRQ_HANDLER(33)
{
    stm32_i2c_irq(2);
}

/**
 * @brief I2C2 error interrupt entry.
 */
DMOD_IRQ_HANDLER(34)
{
    stm32_i2c_irq(2);
}

/**
 * @brief I2C3 event interrupt entry.
 */
DMOD_IRQ_HANDLER(72)
{
    stm32_i2c_irq(3);
}

/**
 * @brief I2C3 error interrupt entry.
 */
DMOD_IRQ_HANDLER(73)
{
    stm32_i2c_irq(3);
}
