#ifndef DMI2C_H
#define DMI2C_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmi2c_defs.h"

/**
 * Public API for the dmi2c module.
 *
 * Functions are declared with the dmod_dmi2c_api(...) macro - dmod's
 * standard pattern for functions callable from other modules (or from this
 * module's own tests/), resolved dynamically by the loader rather than
 * through normal static linkage. See dm_sw_ring/include/dm_sw_ring.h for a
 * fully worked real-world example of the same shape.
 *
 * Definitions in src/dmi2c.c use the matching
 * dmod_dmi2c_api_declaration(...) macro - a plain C function
 * definition here will NOT satisfy these declarations at link time.
 *
 * This is an example interface using the usual "opaque handle" pattern -
 * replace the handle, functions, and struct definition in
 * src/dmi2c.c with your module's real API.
 */

/* Opaque handle - the real struct is defined in src/dmi2c.c */
typedef struct dmi2c* dmi2c_t;

/**
 * Create a new dmi2c instance.
 *
 * @return A valid handle on success, or NULL on allocation failure.
 */
dmod_dmi2c_api(1.0, dmi2c_t, _create, ( void ));

/**
 * Destroy an instance created by dmi2c_create(). Safe to call with
 * NULL.
 */
dmod_dmi2c_api(1.0, void, _destroy, ( dmi2c_t handle ));

/**
 * Example accessor - replace with your module's real API.
 *
 * @return true if handle is a valid, non-NULL instance.
 */
dmod_dmi2c_api(1.0, bool, _is_valid, ( dmi2c_t handle ));

#endif // DMI2C_H
