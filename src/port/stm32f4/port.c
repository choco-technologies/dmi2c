#define DMOD_ENABLE_REGISTRATION ON
#include "dmi2c_port.h"
#include "../stm32_common/stm32_common.h"
int dmod_init(const Dmod_Config_t *config)
{
    (void)config;
    return stm32_i2c_init(false);
}
int dmod_deinit(void)
{
    return stm32_i2c_deinit();
}
