#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmi2c_port.h"
#include "dmosi.h"
#include <errno.h>
#include <string.h>

/**
 * @brief A test peripheral behind the real dynamically resolved port ABI.
 */
static struct
{
    dmi2c_completion_t completion;
    void *user;
    uint8_t reg;
    bool busy;
    bool cancelled;
    dmosi_thread_t worker;
} buses[2];

/**
 * @brief Deliver a hardware-like completion from a real OS thread.
 */
static void delayed_completion(void *user)
{
    unsigned index = (uintptr_t)user;
    dmosi_thread_sleep(5);
    buses[index].busy = false;
    buses[index].completion(buses[index].user, 0);
}

/**
 * @brief Join completion delivery before detaching its callback context.
 */
static void join_worker(unsigned index)
{
    if (buses[index].worker != NULL)
    {
        dmosi_thread_join(buses[index].worker);
        dmosi_thread_destroy(buses[index].worker);
        buses[index].worker = NULL;
    }
}

/**
 * @brief Claim a virtual controller; exercise duplicate ownership rejection.
 */
dmod_dmi2c_port_api_declaration(1.0, int, _init,
                                (uint8_t instance, uint32_t baudrate, dmi2c_completion_t completion,
                                 void *user))
{
    (void)baudrate;
    if (instance < 1 || instance > 2 || completion == NULL)
    {
        return -EINVAL;
    }
    if (buses[instance - 1].completion != NULL)
    {
        return -EBUSY;
    }
    buses[instance - 1].completion = completion;
    buses[instance - 1].user = user;
    return 0;
}

/**
 * @brief Release the virtual controller after cancellation.
 */
dmod_dmi2c_port_api_declaration(1.0, void, _deinit, (uint8_t instance))
{
    if (instance >= 1 && instance <= 2)
    {
        join_worker(instance - 1);
        memset(&buses[instance - 1], 0, sizeof(buses[0]));
    }
}

/** @brief Emulate register addressing, NACK, arbitration loss and a stalled target.
 * 0x38 exposes register 0xa8 = 0x51. 0x70 stalls until cancel, 0x71 loses
 * arbitration, 0x72 returns a bus error, other targets NACK. Completing inline
 * intentionally tests completion arriving before the core starts waiting.
 */
dmod_dmi2c_port_api_declaration(1.0, int, _start,
                                (uint8_t instance, const dmi2c_message_t *message, bool last))
{
    if (instance < 1 || instance > 2 || buses[instance - 1].completion == NULL)
    {
        return -ENODEV;
    }
    unsigned index = instance - 1;
    join_worker(index);
    buses[index].busy = true;
    buses[index].cancelled = false;
    int result = 0;
    if (message->address == 0x69)
    {
        buses[index].worker = dmosi_thread_create(delayed_completion, (void *)(uintptr_t)index, 0,
                                                  32768, "i2c_event", NULL);
        return buses[index].worker != NULL ? 0 : -ENOMEM;
    }
    if (message->address == 0x70)
    {
        return 0;
    }
    if (message->address == 0x71)
    {
        result = -EAGAIN;
    }
    else if (message->address == 0x72)
    {
        result = -EIO;
    }
    else if (message->address != 0x38)
    {
        result = -ENXIO;
    }
    else if (message->read)
    {
        for (size_t i = 0; i < message->size; ++i)
        {
            message->data[i] = buses[index].reg++ == 0xa8 ? 0x51 : 0;
        }
    }
    else if (message->size != 0)
    {
        buses[index].reg = message->data[0];
    }
    buses[index].busy = !last && result == 0;
    buses[index].completion(buses[index].user, result);
    return 0;
}

/**
 * @brief Expose virtual physical-bus ownership to the real core scheduler.
 */
dmod_dmi2c_port_api_declaration(1.0, bool, _is_busy, (uint8_t instance))
{
    return instance >= 1 && instance <= 2 && buses[instance - 1].busy;
}

/**
 * @brief Drop the stalled operation and release its borrowed buffer.
 */
dmod_dmi2c_port_api_declaration(1.0, void, _cancel, (uint8_t instance))
{
    if (instance >= 1 && instance <= 2)
    {
        join_worker(instance - 1);
        buses[instance - 1].busy = false;
        buses[instance - 1].cancelled = true;
    }
}

/**
 * @brief Accept recovery only after the core has explicitly cancelled.
 */
dmod_dmi2c_port_api_declaration(1.0, int, _recover, (uint8_t instance))
{
    if (instance < 1 || instance > 2)
    {
        return -EINVAL;
    }
    return buses[instance - 1].cancelled ? 0 : -EIO;
}

/**
 * @brief Initialize the test-only virtual port module.
 */
int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    memset(buses, 0, sizeof(buses));
    return 0;
}

/**
 * @brief Detach virtual devices when the loader disables this test module.
 */
int dmod_deinit(void)
{
    join_worker(0);
    join_worker(1);
    memset(buses, 0, sizeof(buses));
    return 0;
}
