# API reference

Validation is private to the driver; there are no public validator exports.
Device access uses the dmdrvi 2.0 DIF through `Dmod_FileOpen`, `Dmod_FileRead`,
`Dmod_FileWrite`, `Dmod_Ioctl` and `Dmod_FileClose`. Driver/handle contexts are
opaque, heap allocated and magic guarded. dmdevfs owns their lifetime.

## Messages

```c
#include "dmod.h"
#include "dmi2c_types.h"

void *bus = Dmod_FileOpen("/dev/dmi2cx/touch_i2c", "r+");
if (bus != NULL) {
    uint8_t reg = 0xa8, id = 0;
    dmi2c_message_t messages[] = {
        { .address = 0x38, .read = false, .data = &reg, .size = 1 },
        { .address = 0x38, .read = true,  .data = &id,  .size = 1 }
    };
    dmi2c_transfer_t transfer = { messages, 2 };
    int result = Dmod_Ioctl(bus, dmi2c_ioctl_cmd_transfer, &transfer);
    /* result == 0: id contains the register value. */
    (void)result;
    Dmod_FileClose(bus);
}
```

A vector contains 1..32 messages. Every message sends its own unshifted
7-bit target address (0x08..0x77) and direction; messages may target different
devices. START precedes the first message, repeated START separates messages,
and STOP finishes the last one. Each write buffer is borrowed/read-only; the
non-const pointer also permits read messages. Buffers must remain valid until
the synchronous call returns. Zero-length writes are address-only probes;
zero-length reads are rejected. There is no no-START or no-STOP flag.

The core validates the complete vector before touching hardware. On success,
all bytes have completed. Errors return a negative errno, not a partial byte
count. Some bytes may already have reached a target or an RX buffer when an
error occurs. Do not blindly retry non-idempotent writes.

## IOCTLs

Commands start at `DMDRVI_IOCTL_CUSTOM_BASE`.

| Command | Argument | Result |
|---|---|---|
| `dmi2c_ioctl_cmd_transfer` | `dmi2c_transfer_t *` | Atomic message vector |
| `dmi2c_ioctl_cmd_get_address` | `uint16_t *` | This open handle's default address |
| `dmi2c_ioctl_cmd_set_address` | `uint16_t *` | Changes only this open handle |
| `dmi2c_ioctl_cmd_get_config` | `dmi2c_config_t *` | Bus configuration and handle address |
| `dmi2c_ioctl_cmd_probe` | `uint16_t *` | Explicit address-only write probe |

Unimplemented standard/class ioctls return `-ENOTTY`, including null-argument
probes. Known commands with null arguments return `-EINVAL`. Baudrate and timeout
are immutable after creation: close/unmount before selecting a new configuration.

`read`/`write` perform a single I2C transaction to the handle address. Zero-size
I/O returns zero without bus activity. Negative offsets are invalid; other
offsets are ignored because the bus is not a byte-addressed storage device.
Use a message vector for register addressing. `flush` has no pending buffered
work. The device reports size zero and mode 0666.

| Error | Meaning |
|---|---|
| `-EINVAL` | Invalid instance, address, message, argument or offset |
| `-ENXIO` | Target NACK, in address or data phase |
| `-EAGAIN` | Arbitration lost; no STOP is issued by this master |
| `-ETIMEDOUT` | BUSY/clock stretching/transfer exceeded deadline |
| `-EIO` | Bus error or unavailable OS lock |
| `-EOVERFLOW` | Peripheral overrun or size cannot fit dmdrvi return type |
| `-EBUSY` | Controller already owned or clock already enabled externally |
| `-ERANGE` | Peripheral clock cannot satisfy timing constraints |
| `-ENODEV` | Transfer on an uninitialized controller |
| `-ENOMEM` | Allocation failed |

The dmdrvi `create` and `open` ABI returns NULL on failure. Configuration ranges
are checked before narrowing integer fields using values read by dmini.
The transfer deadline starts after acquiring the per-bus lock, covers the whole
vector, and may be followed by up to 10 ms of STOP cleanup after an error. The task sleeps on an OS semaphore while IRQs transfer payload. Lock
queueing time is additional; callers must not use these functions from ISR.
