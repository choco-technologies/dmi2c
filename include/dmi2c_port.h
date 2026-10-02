#ifndef DMI2C_PORT_H
#define DMI2C_PORT_H
#include "dmi2c_port_defs.h"
#include "dmi2c_types.h"
/* One owner per controller. The port serializes complete transactions.
 * Task context only. GPIO configuration/lifetime belongs to dmdevfs/dmgpio.
 * Returns 0 or negative errno; never retries a partially completed write. */
dmod_dmi2c_port_api(1.0, int, _init, (const dmi2c_config_t *config));
dmod_dmi2c_port_api(1.0, int, _deinit, (uint8_t instance));
dmod_dmi2c_port_api(1.0, int, _transfer, (uint8_t instance, const dmi2c_transfer_t *transfer));
#endif
