#ifndef DMI2C_H
#define DMI2C_H
#include "dmi2c_defs.h"
#include "dmi2c_types.h"
dmod_dmi2c_api(1.0, bool, _validate_config, (const dmi2c_config_t *config));
dmod_dmi2c_api(1.0, bool, _validate_transfer, (const dmi2c_transfer_t *transfer));
#endif
