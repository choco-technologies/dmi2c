# Testing dmi2c

## Loader integration

The native suite runs a test module through dmod_loader. It dynamically loads
the production dmi2c module and resolves its dmdrvi DIF. dmini and the loader's
OS services are real dependencies. There are no replacement SAL/dmini/dmosi
headers or production C files linked directly into a test executable.

A separate test-only dmi2c_port module models the hardware boundary. It provides
a register device at 0x38, NACK, arbitration loss, bus error and a stalled target.
A delayed completion uses a real OS thread. This tests the common driver's
contract; it does not simulate STM32 registers or prove electrical timing.

```sh
cmake -S . -B build-loader -DDMI2C_HOST_TESTS=ON -DDMOD_CPU_FAMILY=x86_64
cmake --build build-loader --parallel 2
export DMOD_DMF_DIR="$PWD/build-loader/dmf"
dmf-get install -d tests/runtime.dmd -y
ctest --test-dir build-loader --output-on-failure
```

Select native DMOD_TOOLS_NAME when cross-development defaults differ from the
host, e.g. arch/aarch64/cortex-a53 on an AArch64 host. Never run an ARM .dmf with
a native loader. Host simulation is enabled explicitly and never selected by
hardware-family release discovery.

The suite covers active-section isolation, decimal configuration/ranges, real
DIF discovery, independent open handles, unknown/null ioctls, vectors, timeout
cancellation, error propagation, duplicate ownership and reuse after failure.
Both immediate and deferred completion are checked.

## Hardware regression

Build the normal stm32f4 or stm32f7 configuration. Install the matching dmi2c,
dmi2c_port and i2ctest modules and the board INI. On STM32F746G-DISCO:

```text
i2ctest /dev/dmi2cx/touch_i2c ft5336
```

This checks FT5336 ID 0x51, repeated START, read lengths 1–4, a 256-byte transfer,
100 repeated reads, independent handles, unknown ioctl and NACK from 0x77.
It writes register pointers only. Ensure no target occupies address 0x77.
Repeat with baudrate=100000 and 400000, recreating the controller after a change.

The revised interrupt engine passed the full FT5336 regression at configured
100 kHz and 400 kHz on STM32F746G-DISCO on 2026-10-02, with zero failures.
The native loader suite passed 8/8 steps. Both F4 and F7 ARM builds passed.
These are functional results; no scope or logic-analyzer measurement was made.
F4 has not been physically tested.

When injecting modules into dmod-boot, remove stale compressed versions of those
same modules and regenerate modules.dmp. Preserve the existing physical flash
and generated build artifacts before testing, and restore them afterward.
