#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmi2c.h"
#include "private.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "validation.h"
#include <errno.h>
#include <string.h>

/**
 * @brief Check the private driver context guard.
 */
static bool valid_context(dmdrvi_context_t context)
{
    return context != NULL && context->magic == DMI2C_CONTEXT_MAGIC;
}

/**
 * @brief Check that an open handle belongs to this live bus.
 */
static bool valid_handle(dmdrvi_context_t context, struct i2c_handle *handle)
{
    return valid_context(context) && handle != NULL && handle->magic == DMI2C_HANDLE_MAGIC &&
           handle->owner == context;
}

/**
 * @brief Create a bus from the active INI view and assign its device number.
 */
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, dmdrvi_context_t, _create,
                                (dmini_context_t config, dmdrvi_dev_num_t *dev_num))
{
    if (!config || !dev_num)
    {
        return NULL;
    }
    const char *section = dmini_section_name(config, 0);
    if (section != NULL && strlen(section) > DMDRVI_ALT_NAME_MAX_LEN)
    {
        return NULL;
    }
    dmdrvi_context_t context = Dmod_Malloc(sizeof(*context));
    if (!context)
    {
        return NULL;
    }
    memset(context, 0, sizeof(*context));
    context->magic = DMI2C_CONTEXT_MAGIC;
    if (!i2c_parse_config(config, &context->config) || i2c_bus_create(context))
    {
        Dmod_Free(context);
        return NULL;
    }
    memset(dev_num, 0, sizeof(*dev_num));
    dev_num->flags = DMDRVI_NUM_MINOR;
    dev_num->minor = context->config.instance;
    if (section != NULL && strcmp(section, "dmi2c"))
    {
        dev_num->flags |= DMDRVI_NUM_ALT_NAME;
        memcpy(dev_num->alt_name, section, strlen(section) + 1);
    }
    return context;
}

/**
 * @brief Release a bus after dmdevfs closes its handles.
 */
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, void, _free, (dmdrvi_context_t context))
{
    if (!valid_context(context))
    {
        return;
    }
    i2c_bus_destroy(context);
    context->magic = 0;
    Dmod_Free(context);
}

/**
 * @brief Allocate independent target-address state for this open file.
 */
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, void *, _open,
                                (dmdrvi_context_t context, int flags,
                                 const dmdrvi_dev_num_t *dev_num))
{
    (void)flags;
    (void)dev_num;
    if (!valid_context(context))
    {
        return NULL;
    }
    struct i2c_handle *opened = Dmod_Malloc(sizeof(*opened));
    if (!opened)
    {
        return NULL;
    }
    opened->magic = DMI2C_HANDLE_MAGIC;
    opened->owner = context;
    opened->address = context->config.address;
    return opened;
}

/**
 * @brief Release the open handle after validating its owning bus.
 */
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, void, _close, (dmdrvi_context_t context, void *handle))
{
    struct i2c_handle *opened = handle;
    if (!valid_handle(context, opened))
    {
        return;
    }
    opened->magic = 0;
    Dmod_Free(opened);
}

/**
 * @brief Adapt stream I/O into one message, preserving negative errno.
 */
static dmdrvi_ssize_t stream_io(dmdrvi_context_t context, void *handle, void *buffer, size_t size,
                                dmdrvi_offset_t offset, bool read)
{
    struct i2c_handle *opened = handle;
    if (!valid_handle(context, opened) || offset < 0 || (size && !buffer))
    {
        return -EINVAL;
    }
    if (size > (uint64_t)INT64_MAX)
    {
        return -EOVERFLOW;
    }
    if (!size)
    {
        return 0;
    }
    dmi2c_message_t message = {opened->address, read, buffer, size};
    dmi2c_transfer_t transfer = {&message, 1};
    int result = i2c_transfer(context, &transfer);
    return result ? result : (dmdrvi_ssize_t)size;
}

/**
 * @brief Read one message from the handle target through the common scheduler.
 */
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, dmdrvi_ssize_t, _read,
                                (dmdrvi_context_t context, void *handle, void *buffer, size_t size,
                                 dmdrvi_offset_t offset))
{
    return stream_io(context, handle, buffer, size, offset, true);
}

/**
 * @brief Write one message to the handle target through the common scheduler.
 */
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, dmdrvi_ssize_t, _write,
                                (dmdrvi_context_t context, void *handle, const void *buffer,
                                 size_t size, dmdrvi_offset_t offset))
{
    return stream_io(context, handle, (void *)buffer, size, offset, false);
}

/**
 * @brief Dispatch I2C controls and reject commands outside this driver ABI.
 */
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, int, _ioctl,
                                (dmdrvi_context_t context, void *handle, int command, void *arg))
{
    struct i2c_handle *opened = handle;
    if (!valid_handle(context, opened))
    {
        return -EINVAL;
    }
    if (command < dmi2c_ioctl_cmd_transfer || command >= dmi2c_ioctl_cmd_max)
    {
        return -ENOTTY;
    }
    if (!arg)
    {
        return -EINVAL;
    }
    switch (command)
    {
    case dmi2c_ioctl_cmd_transfer:
        return i2c_transfer(context, arg);
    case dmi2c_ioctl_cmd_set_address:
        if (!i2c_address_valid(*(uint16_t *)arg))
        {
            return -EINVAL;
        }
        opened->address = *(uint16_t *)arg;
        return 0;
    case dmi2c_ioctl_cmd_get_address:
        *(uint16_t *)arg = opened->address;
        return 0;
    case dmi2c_ioctl_cmd_get_config:
        *(dmi2c_config_t *)arg = context->config;
        ((dmi2c_config_t *)arg)->address = opened->address;
        return 0;
    case dmi2c_ioctl_cmd_probe:
    {
        dmi2c_message_t message = {*(uint16_t *)arg, false, NULL, 0};
        dmi2c_transfer_t transfer = {&message, 1};
        return i2c_transfer(context, &transfer);
    }
    default:
        return -ENOTTY;
    }
}

/**
 * @brief Validate the handle; synchronous transfers leave no buffered work.
 */
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, int, _flush, (dmdrvi_context_t context, void *handle))
{
    return valid_handle(context, handle) ? 0 : -EINVAL;
}

/**
 * @brief Describe the nonseekable character device.
 */
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, int, _stat,
                                (dmdrvi_context_t context, const char *path, dmdrvi_stat_t *stat))
{
    (void)path;
    if (!valid_context(context) || !stat)
    {
        return -EINVAL;
    }
    memset(stat, 0, sizeof(*stat));
    stat->mode = 0666;
    return 0;
}

/**
 * @brief Initialization function for the module.
 *
 * Bus resources are created lazily by dmdrvi_create for each configured device.
 * @param Config Loader configuration supplied when this module is enabled.
 * @return Zero on successful initialization.
 */
int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    return 0;
}

/**
 * @brief De-initialization function for the module.
 *
 * dmdevfs releases its contexts and IRQ callbacks before disabling the module.
 * @return Zero on successful de-initialization.
 */
int dmod_deinit(void)
{
    return 0;
}
