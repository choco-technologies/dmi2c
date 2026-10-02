#include "private.h"
#include "validation.h"
#include <string.h>

/**
 * @brief Read only the active section handed to the driver by dmdevfs.
 * @param ini Restricted configuration context.
 * @param config Destination for range-checked configuration.
 * @return true when all requested settings are supported.
 */
bool i2c_parse_config(dmini_context_t ini, dmi2c_config_t *config)
{
    int instance = dmini_get_int(ini, NULL, "instance", 0);
    int address = dmini_get_int(ini, NULL, "address", 80);
    int baudrate = dmini_get_int(ini, NULL, "baudrate", 100000);
    int timeout = dmini_get_int(ini, NULL, "timeout_ms", 100);
    const char *role = dmini_get_string(ini, NULL, "role", "master");

    if (instance < 1 || instance > 255 || address < 8 || address > 119 || baudrate < 0 ||
        timeout < 1 || timeout > 60000 || strcmp(role, "master"))
    {
        return false;
    }

    config->instance = (uint8_t)instance;
    config->address = (uint16_t)address;
    config->baudrate = (uint32_t)baudrate;
    config->timeout_ms = (uint32_t)timeout;
    return i2c_config_valid(config);
}
