#include "stm32_common.h"
#include "dmclk_port.h"
#include "dmosi.h"
#include <string.h>

#if DMI2C_IP_VERSION == 2
#define modern_ip true
#define hardware_configure i2c_v2_configure
#define hardware_start i2c_v2_start
#define hardware_irq i2c_v2_irq
#define CONTROLLER_COUNT 4
#else
#define modern_ip false
#define hardware_configure i2c_v1_configure
#define hardware_start i2c_v1_start
#define hardware_irq i2c_v1_irq
#define CONTROLLER_COUNT 3
#endif
static stm32_i2c_t controllers[CONTROLLER_COUNT];
static const uint8_t event_irqs[] = {31, 33, 72, 95};

/**
 * @brief Validate physical instance before indexing hardware descriptors.
 */
static bool instance_valid(uint8_t instance)
{
    return instance >= 1 && instance <= (modern_ip ? 4 : 3);
}

/**
 * @brief Decode SYSCLK, HPRE and PPRE1 to obtain the current APB1 clock.
 */
static uint32_t peripheral_clock(void)
{
    uint32_t cfgr = i2c_read(RCC_CFGR);
    uint32_t ahb = (cfgr >> 4) & 15;
    uint32_t apb = (cfgr >> 10) & 7;
    uint32_t ahb_div = ahb < 8 ? 1 : (ahb < 12 ? BIT(ahb - 7) : BIT(ahb - 6));
    uint32_t apb_div = apb < 4 ? 1 : BIT(apb - 3);
    return (uint32_t)dmclk_port_get_current_frequency() / ahb_div / apb_div;
}

/**
 * @brief Configure both IRQ priorities so callbacks may use ISR-safe dmosi.
 */
static void interrupts(uint8_t instance, bool enable)
{
    for (uint32_t irq = event_irqs[instance - 1]; irq <= event_irqs[instance - 1] + 1U; ++irq)
    {
        i2c_write(0xe000e180UL + (irq / 32) * 4, BIT(irq % 32));
        i2c_write(0xe000e280UL + (irq / 32) * 4, BIT(irq % 32));
        if (enable)
        {
            *(volatile uint8_t *)(0xe000e400UL + irq) = dmosi_get_min_interrupt_priority();
            i2c_write(0xe000e100UL + (irq / 32) * 4, BIT(irq % 32));
        }
    }
}

/**
 * @brief Reset and configure only the locally claimed hardware controller.
 */
static int configure(uint8_t instance)
{
    stm32_i2c_t *state = &controllers[instance - 1];
    uint32_t bit = BIT(20 + instance);
    Dmod_EnterCritical();
    i2c_modify(RCC_APB1RSTR, 0, bit);
    i2c_modify(RCC_APB1RSTR, bit, 0);
    Dmod_ExitCritical();
    return hardware_configure(state->base, peripheral_clock(), state->baudrate);
}

/**
 * @brief Select the family IP layout; allocate no OS or transaction resources.
 */
void stm32_i2c_init(void)
{
    memset(controllers, 0, sizeof(controllers));
}

/**
 * @brief Disable event sources before callback invocation or cancellation.
 */
static void mask_events(stm32_i2c_t *state)
{
    if (modern_ip)
    {
        i2c_modify(state->base + V2_CR1, 0xfe, 0);
    }
    else
    {
        i2c_modify(state->base + V1_CR2, BIT(8) | BIT(9) | BIT(10), 0);
    }
}

/**
 * @brief Clear borrowed state before calling the core's completion callback.
 */
void stm32_i2c_complete(stm32_i2c_t *state, int result)
{
    mask_events(state);
    state->active = false;
    state->message = NULL;
    state->completion(state->user, result);
}

/**
 * @brief Forward an event/error IRQ only while a message is armed.
 */
void stm32_i2c_irq(uint8_t instance)
{
    stm32_i2c_t *state = &controllers[instance - 1];
    if (state->active)
    {
        hardware_irq(state);
    }
}

/**
 * @brief Claim RCC ownership, preserve the clock selector and enable IRQs.
 */
dmod_dmi2c_port_api_declaration(1.0, int, _init,
                                (uint8_t instance, uint32_t baudrate, dmi2c_completion_t completion,
                                 void *user))
{
    if (!instance_valid(instance) || completion == NULL)
    {
        return -EINVAL;
    }
    stm32_i2c_t *state = &controllers[instance - 1];
    Dmod_EnterCritical();
    if (state->completion != NULL || (i2c_read(RCC_APB1ENR) & BIT(20 + instance)))
    {
        Dmod_ExitCritical();
        return -EBUSY;
    }
    state->base = 0x40005400UL + (instance - 1U) * 0x400UL;
    state->baudrate = baudrate;
    state->completion = completion;
    state->user = user;
    if (modern_ip)
    {
        uint32_t shift = 16 + 2 * (instance - 1);
        state->saved_selector = (i2c_read(RCC_DCKCFGR2) >> shift) & 3;
        i2c_modify(RCC_DCKCFGR2, 3UL << shift, 0);
    }
    i2c_modify(RCC_APB1ENR, 0, BIT(20 + instance));
    (void)i2c_read(RCC_APB1ENR);
    Dmod_ExitCritical();
    int result = configure(instance);
    if (result != 0)
    {
        dmi2c_port_deinit(instance);
    }
    else
    {
        interrupts(instance, true);
    }
    return result;
}

/**
 * @brief Quiesce IRQs and restore clock ownership before detaching user state.
 */
dmod_dmi2c_port_api_declaration(1.0, void, _deinit, (uint8_t instance))
{
    if (!instance_valid(instance))
    {
        return;
    }
    stm32_i2c_t *state = &controllers[instance - 1];
    if (state->completion == NULL)
    {
        return;
    }
    Dmod_EnterCritical();
    interrupts(instance, false);
    i2c_write(state->base, 0);
    i2c_modify(RCC_APB1ENR, BIT(20 + instance), 0);
    if (modern_ip)
    {
        uint32_t shift = 16 + 2 * (instance - 1);
        i2c_modify(RCC_DCKCFGR2, 3UL << shift, state->saved_selector << shift);
    }
    memset(state, 0, sizeof(*state));
    Dmod_ExitCritical();
}

/**
 * @brief Release any remaining hardware instances during module shutdown.
 */
void stm32_i2c_deinit(void)
{
    for (uint8_t instance = 1; instance <= (modern_ip ? 4 : 3); ++instance)
    {
        dmi2c_port_deinit(instance);
    }
}

/**
 * @brief Borrow one message buffer and arm the peripheral event sources.
 */
dmod_dmi2c_port_api_declaration(1.0, int, _start,
                                (uint8_t instance, const dmi2c_message_t *message, bool last))
{
    if (!instance_valid(instance) || message == NULL)
    {
        return -EINVAL;
    }
    stm32_i2c_t *state = &controllers[instance - 1];
    if (state->completion == NULL)
    {
        return -ENODEV;
    }
    if (state->active)
    {
        return -EBUSY;
    }
    Dmod_EnterCritical();
    state->message = message;
    state->position = 0;
    state->last = last;
    state->arbitration_lost = false;
    state->active = true;
    hardware_start(state);
    Dmod_ExitCritical();
    return 0;
}

/**
 * @brief Read BUSY/STOP without owning a timeout or blocking the caller.
 */
dmod_dmi2c_port_api_declaration(1.0, bool, _is_busy, (uint8_t instance))
{
    if (!instance_valid(instance))
    {
        return false;
    }
    stm32_i2c_t *state = &controllers[instance - 1];
    if (state->completion == NULL)
    {
        return false;
    }
    return modern_ip ? (i2c_read(state->base + V2_ISR) & BIT(15)) != 0
                     : ((i2c_read(state->base + V1_SR2) & BIT(1)) ||
                        (i2c_read(state->base + V1_CR1) & BIT(9)));
}

/**
 * @brief Detach buffers synchronously and request STOP only if still master.
 */
dmod_dmi2c_port_api_declaration(1.0, void, _cancel, (uint8_t instance))
{
    if (!instance_valid(instance))
    {
        return;
    }
    stm32_i2c_t *state = &controllers[instance - 1];
    if (state->completion == NULL)
    {
        return;
    }
    Dmod_EnterCritical();
    mask_events(state);
    bool lost = state->arbitration_lost || (modern_ip ? (i2c_read(state->base + V2_ISR) & BIT(9))
                                                      : (i2c_read(state->base + V1_SR1) & BIT(9)));
    if (!lost)
    {
        if (modern_ip && (i2c_read(state->base + V2_ISR) & BIT(15)))
        {
            i2c_modify(state->base + V2_CR2, 0, BIT(14));
        }
        else if (!modern_ip && (i2c_read(state->base + V1_SR2) & BIT(0)))
        {
            i2c_modify(state->base + V1_CR1, 0, BIT(9));
        }
    }
    state->arbitration_lost = lost;
    state->active = false;
    state->message = NULL;
    Dmod_ExitCritical();
}

/**
 * @brief Reset hardware after timeout/error; retain its callback registration.
 */
dmod_dmi2c_port_api_declaration(1.0, int, _recover, (uint8_t instance))
{
    if (!instance_valid(instance))
    {
        return -EINVAL;
    }
    if (controllers[instance - 1].completion == NULL)
    {
        return -ENODEV;
    }
    if (controllers[instance - 1].arbitration_lost)
    {
        return 0;
    }
    return configure(instance);
}
