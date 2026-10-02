#include "private.h"
#include "validation.h"
#include "dmi2c_port.h"
#include <errno.h>

/**
 * @brief Wake the task waiting for an IRQ-driven message completion.
 */
static void message_complete(void *user, int result)
{
    dmdrvi_context_t context = user;
    if (context == NULL || context->magic != DMI2C_CONTEXT_MAGIC)
    {
        return;
    }
    context->result = result;
    dmosi_semaphore_post(context->completion, 1);
}

/**
 * @brief Allocate per-bus synchronization and attach the hardware callback.
 */
int i2c_bus_create(dmdrvi_context_t context)
{
    if (context == NULL || context->magic != DMI2C_CONTEXT_MAGIC)
    {
        return -EINVAL;
    }
    context->lock = dmosi_mutex_create(false);
    context->completion = dmosi_semaphore_create(0, 1);
    if (context->lock == NULL || context->completion == NULL)
    {
        if (context->completion != NULL)
        {
            dmosi_semaphore_destroy(context->completion);
        }
        if (context->lock != NULL)
        {
            dmosi_mutex_destroy(context->lock);
        }
        return -ENOMEM;
    }

    int result = dmi2c_port_init(context->config.instance, context->config.baudrate,
                                 message_complete, context);
    if (result != 0)
    {
        dmosi_semaphore_destroy(context->completion);
        dmosi_mutex_destroy(context->lock);
    }
    return result;
}

/**
 * @brief Detach all IRQ access before destroying the callback's context.
 */
void i2c_bus_destroy(dmdrvi_context_t context)
{
    if (context == NULL || context->magic != DMI2C_CONTEXT_MAGIC)
    {
        return;
    }
    dmosi_mutex_lock(context->lock);
    dmi2c_port_deinit(context->config.instance);
    dmosi_mutex_unlock(context->lock);
    dmosi_semaphore_destroy(context->completion);
    dmosi_mutex_destroy(context->lock);
}

/**
 * @brief Return milliseconds remaining in the whole-vector deadline.
 */
static uint32_t time_left(dmdrvi_context_t context, Dmod_Timestamp_t started)
{
    Dmod_Timestamp_t elapsed = Dmod_GetUptime() - started;
    return elapsed < context->config.timeout_ms ? context->config.timeout_ms - elapsed : 0;
}

/**
 * @brief Yield while waiting for another master or STOP to release the bus.
 */
static int wait_idle(uint8_t instance, Dmod_Timestamp_t started, uint32_t timeout)
{
    while (dmi2c_port_is_busy(instance))
    {
        if (Dmod_GetUptime() - started >= timeout)
        {
            return -ETIMEDOUT;
        }
        dmosi_thread_sleep(1);
    }
    return 0;
}

/**
 * @brief Start one message and sleep until completion or the vector deadline.
 */
static int run_message(dmdrvi_context_t context, const dmi2c_message_t *message, bool last,
                       Dmod_Timestamp_t started)
{
    uint32_t remaining = time_left(context, started);
    if (remaining == 0)
    {
        return -ETIMEDOUT;
    }
    /* Drain a completion which raced a preceding timeout before reusing the bus. */
    (void)dmosi_semaphore_wait(context->completion, 1, 0);
    context->result = -EINPROGRESS;
    int result = dmi2c_port_start(context->config.instance, message, last);
    if (result != 0)
    {
        return result;
    }
    remaining = time_left(context, started);
    result = dmosi_semaphore_wait(context->completion, 1, (int32_t)remaining);
    return result == 0 ? context->result : -ETIMEDOUT;
}

/**
 * @brief Cancel a failed transfer without issuing STOP after arbitration loss.
 */
static void recover_bus(dmdrvi_context_t context, int result)
{
    dmi2c_port_cancel(context->config.instance);
    if (result != -EAGAIN)
    {
        (void)wait_idle(context->config.instance, Dmod_GetUptime(), 10);
        (void)dmi2c_port_recover(context->config.instance);
    }
}

/**
 * @brief Serialize the complete vector, including error recovery, per bus.
 */
int i2c_transfer(dmdrvi_context_t context, const dmi2c_transfer_t *transfer)
{
    if (context == NULL || context->magic != DMI2C_CONTEXT_MAGIC || !i2c_transfer_valid(transfer))
    {
        return -EINVAL;
    }
    if (dmosi_mutex_lock(context->lock) != 0)
    {
        return -EIO;
    }
    Dmod_Timestamp_t started = Dmod_GetUptime();
    int result = wait_idle(context->config.instance, started, context->config.timeout_ms);
    bool started_message = false;
    for (size_t i = 0; result == 0 && i < transfer->count; ++i)
    {
        started_message = true;
        result = run_message(context, &transfer->messages[i], i + 1 == transfer->count, started);
    }
    if (result == 0)
    {
        result = wait_idle(context->config.instance, started, context->config.timeout_ms);
    }
    if (result != 0 && started_message)
    {
        recover_bus(context, result);
    }
    dmosi_mutex_unlock(context->lock);
    return result;
}
