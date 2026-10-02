#ifndef DMI2C_PRIVATE_H
#define DMI2C_PRIVATE_H

#include "dmod.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmosi.h"
#include "dmi2c_types.h"

#define DMI2C_CONTEXT_MAGIC 0x49324344U
#define DMI2C_HANDLE_MAGIC 0x49324348U

/**
 * @brief Driver-owned bus state; no scheduler state resides in the port.
 */
struct dmdrvi_context
{
    uint32_t magic;
    dmi2c_config_t config;
    dmosi_mutex_t lock;
    dmosi_semaphore_t completion;
    volatile int result;
};

/**
 * @brief Per-open target selection, independent of other handles.
 */
struct i2c_handle
{
    uint32_t magic;
    dmdrvi_context_t owner;
    uint16_t address;
};

/**
 * @brief Read and validate dmdevfs's active INI section using dmini.
 */
bool i2c_parse_config(dmini_context_t ini, dmi2c_config_t *config);

/**
 * @brief Create OS resources and claim the port; unwind on failure.
 */
int i2c_bus_create(dmdrvi_context_t context);

/**
 * @brief Quiesce hardware before releasing OS resources.
 */
void i2c_bus_destroy(dmdrvi_context_t context);

/**
 * @brief Execute a validated vector under this bus's lock and deadline.
 */
int i2c_transfer(dmdrvi_context_t context, const dmi2c_transfer_t *transfer);

#endif // DMI2C_PRIVATE_H
