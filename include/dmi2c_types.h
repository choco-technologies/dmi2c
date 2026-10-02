#ifndef DMI2C_TYPES_H
#define DMI2C_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dmdrvi_ioctl.h"

/* Addresses are unshifted, ordinary 7-bit targets (0x08..0x77). */
typedef struct
{
    uint8_t instance;       /* 1-based hardware controller */
    uint16_t address;       /* Default target for read/write */
    uint32_t baudrate;      /* 100000 or 400000 Hz */
    uint32_t timeout_ms;    /* Whole transaction deadline, 1..60000 */
} dmi2c_config_t;

typedef struct
{
    uint16_t address;
    bool read;
    uint8_t *data;          /* Borrowed; unchanged by write messages */
    size_t size;           /* Zero is allowed only for an address-only write */
} dmi2c_message_t;

typedef struct
{
    dmi2c_message_t *messages;
    size_t count;          /* 1..32; repeated START between every message */
} dmi2c_transfer_t;

typedef enum
{
    dmi2c_ioctl_cmd_transfer = DMDRVI_IOCTL_CUSTOM_BASE,
    dmi2c_ioctl_cmd_get_address,
    dmi2c_ioctl_cmd_set_address, /* uint16_t*, per-open handle */
    dmi2c_ioctl_cmd_get_config,  /* dmi2c_config_t*, including handle address */
    dmi2c_ioctl_cmd_probe,       /* uint16_t*: explicit target, no payload */
    dmi2c_ioctl_cmd_max
} dmi2c_ioctl_cmd_t;
#endif
