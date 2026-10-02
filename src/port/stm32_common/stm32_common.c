/* Registration is emitted only by the selected family's port.c. */
#include "stm32_common.h"
#include "../../validation.h"
#include "dmclk_port.h"
#include "dmosi.h"
#include <string.h>

static bool modern_ip;
static dmosi_mutex_t bus_lock;
/* Fixed hardware resources, not an unbounded collection. */
static struct
{
    bool active;
    dmi2c_config_t config;
    uint32_t saved_selector;
} controllers[4];

static uintptr_t controller_base(uint8_t instance)
{
    return 0x40005400UL + (instance - 1U) * 0x400UL;
}
static bool instance_valid(uint8_t instance)
{
    return instance >= 1 && instance <= (modern_ip ? 4 : 3);
}
static uint32_t peripheral_clock(void)
{
    uint32_t cfgr = i2c_read(RCC_CFGR);
    uint32_t ahb = (cfgr >> 4) & 15;
    uint32_t apb = (cfgr >> 10) & 7;
    uint32_t ahb_div = ahb < 8 ? 1 : (ahb < 12 ? BIT(ahb - 7) : BIT(ahb - 6));
    uint32_t apb_div = apb < 4 ? 1 : BIT(apb - 3);
    return (uint32_t)dmclk_port_get_current_frequency() / ahb_div / apb_div;
}
static void reset_controller(uint8_t instance)
{
    uint32_t bit = BIT(20 + instance);
    Dmod_EnterCritical();
    i2c_modify(RCC_APB1RSTR, 0, bit);
    i2c_modify(RCC_APB1RSTR, bit, 0);
    Dmod_ExitCritical();
}
static int configure(uint8_t instance)
{
    uint32_t clock = peripheral_clock();
    uintptr_t base = controller_base(instance);
    uint32_t baudrate = controllers[instance - 1].config.baudrate;
    return modern_ip ? i2c_v2_configure(base, clock, baudrate)
                     : i2c_v1_configure(base, clock, baudrate);
}
int stm32_i2c_init(bool modern)
{
    modern_ip = modern;
    memset(controllers, 0, sizeof(controllers));
    bus_lock = dmosi_mutex_create(false);
    return bus_lock ? 0 : -ENOMEM;
}
static void release_controller(uint8_t instance)
{
    i2c_write(controller_base(instance), 0);
    Dmod_EnterCritical();
    i2c_modify(RCC_APB1ENR, BIT(20 + instance), 0);
    if (modern_ip)
    {
        uint32_t shift = 16 + 2 * (instance - 1);
        i2c_modify(RCC_DCKCFGR2, 3UL << shift,
                   controllers[instance - 1].saved_selector << shift);
    }
    Dmod_ExitCritical();
    controllers[instance - 1].active = false;
}
int stm32_i2c_deinit(void)
{
    if (!bus_lock) return 0;
    for (uint8_t i = 1; i <= (modern_ip ? 4 : 3); ++i)
        if (controllers[i - 1].active) release_controller(i);
    dmosi_mutex_destroy(bus_lock);
    bus_lock = NULL;
    return 0;
}
static int acquire_controller(const dmi2c_config_t *config)
{
    uint8_t instance = config->instance;
    if (controllers[instance - 1].active) return -EBUSY;
    /* Do not reset hardware owned by firmware or another module. */
    if (i2c_read(RCC_APB1ENR) & BIT(20 + instance)) return -EBUSY;
    controllers[instance - 1].config = *config;
    Dmod_EnterCritical();
    if (modern_ip)
    {
        uint32_t shift = 16 + 2 * (instance - 1);
        controllers[instance - 1].saved_selector =
            (i2c_read(RCC_DCKCFGR2) >> shift) & 3;
        i2c_modify(RCC_DCKCFGR2, 3UL << shift, 0); /* PCLK1 */
    }
    i2c_modify(RCC_APB1ENR, 0, BIT(20 + instance));
    (void)i2c_read(RCC_APB1ENR);
    Dmod_ExitCritical();
    reset_controller(instance);
    int rc = configure(instance);
    if (rc) release_controller(instance);
    else controllers[instance - 1].active = true;
    return rc;
}
dmod_dmi2c_port_api_declaration(1.0, int, _init, (const dmi2c_config_t *config))
{
    if (!i2c_config_valid(config) || !instance_valid(config->instance)) return -EINVAL;
    if (!bus_lock || dmosi_mutex_lock(bus_lock)) return -EIO;
    int rc = acquire_controller(config);
    dmosi_mutex_unlock(bus_lock);
    return rc;
}
dmod_dmi2c_port_api_declaration(1.0, int, _deinit, (uint8_t instance))
{
    if (!instance_valid(instance)) return -EINVAL;
    if (!bus_lock || dmosi_mutex_lock(bus_lock)) return -EIO;
    if (controllers[instance - 1].active) release_controller(instance);
    dmosi_mutex_unlock(bus_lock);
    return 0;
}
dmod_dmi2c_port_api_declaration(1.0, int, _transfer,
    (uint8_t instance, const dmi2c_transfer_t *transfer))
{
    if (!instance_valid(instance) || !i2c_transfer_valid(transfer)) return -EINVAL;
    if (!bus_lock || dmosi_mutex_lock(bus_lock)) return -EIO;
    int rc = -ENODEV;
    if (controllers[instance - 1].active)
    {
        i2c_transaction_t t = {controller_base(instance), Dmod_GetUptime(),
                              controllers[instance - 1].config.timeout_ms};
        rc = modern_ip ? i2c_v2_transfer(&t, transfer) : i2c_v1_transfer(&t, transfer);
        /* Local reset after a failed transaction restores ACK/POS/RELOAD.
         * It cannot release SDA held low by a slave; that remains a timeout.
         * ARLO must never generate STOP or reset a bus owned by another master. */
        if (rc && rc != -EAGAIN)
        {
            reset_controller(instance);
            int recovered = configure(instance);
            if (recovered) release_controller(instance);
        }
    }
    dmosi_mutex_unlock(bus_lock);
    return rc;
}
