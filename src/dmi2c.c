#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmi2c.h"
#include "dmi2c_port.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "validation.h"
#include <errno.h>
#include <string.h>

#define CONTEXT_MAGIC 0x49324344U
#define HANDLE_MAGIC 0x49324348U
struct dmdrvi_context
{
    uint32_t magic;
    dmi2c_config_t config;
};
struct i2c_handle
{
    uint32_t magic;
    dmdrvi_context_t owner;
    uint16_t address;
};
static bool valid_context(dmdrvi_context_t c)
{
    return c && c->magic == CONTEXT_MAGIC;
}
static bool valid_handle(dmdrvi_context_t c, struct i2c_handle *h)
{
    return valid_context(c) && h && h->magic == HANDLE_MAGIC && h->owner == c;
}
dmod_dmi2c_api_declaration(1.0, bool, _validate_config, (const dmi2c_config_t *config))
{
    return i2c_config_valid(config);
}
dmod_dmi2c_api_declaration(1.0, bool, _validate_transfer, (const dmi2c_transfer_t *transfer))
{
    return i2c_transfer_valid(transfer);
}

/* Enumeration also respects dmdevfs's active-section restriction. */
static const char *config_section(dmini_context_t ini)
{
    for (int i = 0; i < dmini_section_count(ini); ++i)
    {
        const char *s = dmini_section_name(ini, i);
        if (s && dmini_has_key(ini, s, "instance"))
            return s;
    }
    return NULL;
}
static bool number(dmini_context_t ini, const char *section,
                   const char *key, uint32_t fallback, uint32_t *out)
{
    const char *s = dmini_get_string(ini, section, key, NULL);
    if (!s) { *out = fallback; return true; }
    uint32_t value = 0, base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    if (!*s) return false;
    for (; *s; ++s)
    {
        unsigned digit = *s >= '0' && *s <= '9' ? (unsigned)(*s - '0') :
            (*s >= 'a' && *s <= 'f' ? (unsigned)(*s - 'a' + 10) :
            (*s >= 'A' && *s <= 'F' ? (unsigned)(*s - 'A' + 10) : 16));
        if (digit >= base || value > (UINT32_MAX - digit) / base) return false;
        value = value * base + digit;
    }
    *out = value;
    return true;
}
static bool parse_config(dmini_context_t ini, const char *s, dmi2c_config_t *c)
{
    uint32_t instance, address;
    if (!number(ini, s, "instance", 0, &instance) || instance > 4 ||
        !number(ini, s, "address", 0x50, &address) || address > 0x77 ||
        !number(ini, s, "baudrate", 100000, &c->baudrate) ||
        !number(ini, s, "timeout_ms", 100, &c->timeout_ms))
        return false;
    c->instance = instance;
    c->address = address;
    /* Reject features that are not implemented instead of ignoring them. */
    const char *role = dmini_get_string(ini, s, "role", "master");
    return strcmp(role, "master") == 0 && i2c_config_valid(c);
}
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, dmdrvi_context_t, _create,
    (dmini_context_t config, dmdrvi_dev_num_t *dev_num))
{
    if (!config || !dev_num) return NULL;
    const char *s = config_section(config);
    if (!s || strlen(s) > DMDRVI_ALT_NAME_MAX_LEN) return NULL;
    dmdrvi_context_t c = Dmod_Malloc(sizeof(*c));
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    if (!parse_config(config, s, &c->config) || dmi2c_port_init(&c->config))
    {
        Dmod_Free(c);
        return NULL;
    }
    c->magic = CONTEXT_MAGIC;
    memset(dev_num, 0, sizeof(*dev_num));
    dev_num->flags = DMDRVI_NUM_MINOR;
    dev_num->minor = c->config.instance;
    if (strcmp(s, "dmi2c"))
    {
        dev_num->flags |= DMDRVI_NUM_ALT_NAME;
        memcpy(dev_num->alt_name, s, strlen(s) + 1);
    }
    return c;
}
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, void, _free, (dmdrvi_context_t context))
{
    if (!valid_context(context)) return;
    dmi2c_port_deinit(context->config.instance);
    context->magic = 0;
    Dmod_Free(context);
}
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, void*, _open,
    (dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t *dev_num))
{
    (void)flags; (void)dev_num;
    if (!valid_context(context)) return NULL;
    struct i2c_handle *h = Dmod_Malloc(sizeof(*h));
    if (!h) return NULL;
    h->magic = HANDLE_MAGIC;
    h->owner = context;
    h->address = context->config.address;
    return h;
}
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, void, _close,
    (dmdrvi_context_t context, void *handle))
{
    struct i2c_handle *h = handle;
    if (!valid_handle(context, h)) return;
    h->magic = 0;
    Dmod_Free(h);
}
static dmdrvi_ssize_t stream_io(dmdrvi_context_t c, void *handle,
    void *buffer, size_t size, dmdrvi_offset_t offset, bool read)
{
    struct i2c_handle *h = handle;
    if (!valid_handle(c, h) || offset < 0 || (size && !buffer)) return -EINVAL;
    if (size > (uint64_t)INT64_MAX) return -EOVERFLOW;
    if (!size) return 0;
    dmi2c_message_t m = {h->address, read, buffer, size};
    dmi2c_transfer_t t = {&m, 1};
    int rc = dmi2c_port_transfer(c->config.instance, &t);
    return rc ? rc : (dmdrvi_ssize_t)size;
}
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, dmdrvi_ssize_t, _read,
    (dmdrvi_context_t context, void *handle, void *buffer, size_t size, dmdrvi_offset_t offset))
{
    return stream_io(context, handle, buffer, size, offset, true);
}
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, dmdrvi_ssize_t, _write,
    (dmdrvi_context_t context, void *handle, const void *buffer, size_t size, dmdrvi_offset_t offset))
{
    return stream_io(context, handle, (void *)buffer, size, offset, false);
}
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, int, _ioctl,
    (dmdrvi_context_t context, void *handle, int command, void *arg))
{
    struct i2c_handle *h = handle;
    if (!valid_handle(context, h)) return -EINVAL;
    if (command < dmi2c_ioctl_cmd_transfer || command >= dmi2c_ioctl_cmd_max)
        return -ENOTTY;
    if (!arg) return -EINVAL;
    switch (command)
    {
    case dmi2c_ioctl_cmd_transfer:
        return dmi2c_port_transfer(context->config.instance, arg);
    case dmi2c_ioctl_cmd_set_address:
        if (!i2c_address_valid(*(uint16_t *)arg)) return -EINVAL;
        h->address = *(uint16_t *)arg;
        return 0;
    case dmi2c_ioctl_cmd_get_address:
        *(uint16_t *)arg = h->address;
        return 0;
    case dmi2c_ioctl_cmd_get_config:
        *(dmi2c_config_t *)arg = context->config;
        ((dmi2c_config_t *)arg)->address = h->address;
        return 0;
    case dmi2c_ioctl_cmd_probe:
    {
        dmi2c_message_t m = {*(uint16_t *)arg, false, NULL, 0};
        dmi2c_transfer_t t = {&m, 1};
        return dmi2c_port_transfer(context->config.instance, &t);
    }
    default: return -ENOTTY;
    }
}
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, int, _flush,
    (dmdrvi_context_t context, void *handle))
{
    return valid_handle(context, handle) ? 0 : -EINVAL;
}
dmod_dmdrvi_dif_api_declaration(2.0, dmi2c, int, _stat,
    (dmdrvi_context_t context, const char *path, dmdrvi_stat_t *stat))
{
    (void)path;
    if (!valid_context(context) || !stat) return -EINVAL;
    memset(stat, 0, sizeof(*stat));
    stat->mode = 0666;
    return 0;
}
int dmod_init(const Dmod_Config_t *config) { (void)config; return 0; }
int dmod_deinit(void) { return 0; }
