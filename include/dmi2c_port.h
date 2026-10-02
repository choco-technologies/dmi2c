#ifndef DMI2C_PORT_H
#define DMI2C_PORT_H

#include "dmi2c_port_defs.h"
#include "dmi2c_types.h"

/**
 * @brief Report completion of one message; callable from interrupt context.
 */
typedef void (*dmi2c_completion_t)(void *user, int result);

/**
 * @brief Claim and configure hardware; callback remains valid until deinit.
 */
dmod_dmi2c_port_api(1.0, int, _init,
                    (uint8_t instance, uint32_t baudrate, dmi2c_completion_t completion,
                     void *user));

/**
 * @brief Disable interrupts, detach the callback and release the controller.
 */
dmod_dmi2c_port_api(1.0, void, _deinit, (uint8_t instance));

/** @brief Start one borrowed message; completion fires once after acceptance.
 * last requests STOP; otherwise leave hardware ready for repeated START.
 * A negative return means the message was not accepted and no callback follows.
 */
dmod_dmi2c_port_api(1.0, int, _start,
                    (uint8_t instance, const dmi2c_message_t *message, bool last));

/**
 * @brief Report physical BUSY or a pending STOP without waiting.
 */
dmod_dmi2c_port_api(1.0, bool, _is_busy, (uint8_t instance));

/** @brief Stop IRQ access to borrowed buffers and request STOP if still master.
 * This is synchronous and prevents any further callback for the cancelled message.
 */
dmod_dmi2c_port_api(1.0, void, _cancel, (uint8_t instance));

/**
 * @brief Restore peripheral configuration after cancellation; never wait.
 */
dmod_dmi2c_port_api(1.0, int, _recover, (uint8_t instance));

#endif // DMI2C_PORT_H
