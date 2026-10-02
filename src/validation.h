#ifndef DMI2C_VALIDATION_H
#define DMI2C_VALIDATION_H
#include "dmi2c_types.h"

/**
 * @brief Accept ordinary unshifted 7-bit target addresses.
 */
static inline bool i2c_address_valid(uint16_t address)
{
    return address >= 0x08 && address <= 0x77;
}

/**
 * @brief Check portable bus settings before claiming any hardware.
 */
static inline bool i2c_config_valid(const dmi2c_config_t *c)
{
    return c && c->instance >= 1 && i2c_address_valid(c->address) &&
           (c->baudrate == 100000 || c->baudrate == 400000) && c->timeout_ms >= 1 &&
           c->timeout_ms <= 60000;
}

/**
 * @brief Validate the full borrowed vector before starting its first message.
 */
static inline bool i2c_transfer_valid(const dmi2c_transfer_t *t)
{
    if (!t || !t->messages || !t->count || t->count > 32)
    {
        return false;
    }
    for (size_t i = 0; i < t->count; ++i)
    {
        const dmi2c_message_t *m = &t->messages[i];
        if (!i2c_address_valid(m->address) || (m->size && !m->data) || (!m->size && m->read))
        {
            return false;
        }
    }
    return true;
}
#endif
