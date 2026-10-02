# dmi2c

I2C master driver for the DMOD embedded module system, exposed through dmdrvi
2.0 and dmdevfs. STM32F4 and STM32F7 use their hardware I2C controllers.

- Unshifted 7-bit target addresses, Standard-mode (100 kHz) and Fast-mode
  (400 kHz), synchronous reads, writes, address-only probes and atomic vectors
  of messages with repeated START.
- Per-open target addresses, serialized transactions, whole-transaction
  deadlines, negative errno results, local controller recovery after errors.
- F4 ACK/POS receive sequences and F7 NBYTES/RELOAD for transfers over 255 bytes.
- Board INIs for NUCLEO-F401RE/F411RE/F446RE/F767ZI, STM32F4-DISCOVERY and
  STM32F746G-DISCO, with real GPIO assignments and pull-up requirements.

Payload transfer uses event/error interrupts; callers sleep until completion.
Slave mode, 10-bit addressing, DMA, SMBus/PEC and GPIO clock-pulse bus recovery are not
implemented. Reserved/general-call addresses are rejected. Calls require task
context and a running millisecond uptime clock; configuration assumes stable
system clocks for the lifetime of the bus. Baudrate is a maximum: conservative
filter/rise/fall timing may produce a slower clock.

## Build

```sh
cmake -S . -B build-f7 -DDMOD_CPU_FAMILY=stm32f7
cmake --build build-f7 --parallel 2
cmake -S . -B build-f4 -DDMOD_CPU_FAMILY=stm32f4
cmake --build build-f4 --parallel 2
```

Optionally pass `-DDMOD_DIR=/path/to/dmod`. CMake otherwise fetches the SDK from
its `develop` branch. Generated modules are under `build-f*/dmf/`; release
packages include headers, documentation and board configurations. The native Makefile retains the generated DMOD build structure; it does not
invoke CMake. Supply DMOD_DIR and the normal DMOD Make toolchain settings.
Dependencies are dynamically linked DMOD modules, with no bundled HAL.

## Use

Install `dmi2c` and the matching `dmi2c_port`, then select a file from
[configs/board](configs/README.md) for `/configs/drivers/dmi2c/`.
GPIO setup belongs to `dmgpio`/dmdevfs. For STM32F746G-DISCO's onboard touch bus:

```text
i2ctest /dev/dmi2cx/touch_i2c probe 0x38
i2ctest /dev/dmi2cx/touch_i2c read 0x38 0xa8 1
i2ctest /dev/dmi2cx/touch_i2c ft5336
```

See [API](docs/api-reference.md), [configuration](docs/configuration.md),
[ports](docs/port-implementation.md) and [hardware tool](tools/i2ctest/README.md).

## Tests

Integration tests run actual .dmf modules through dmod_loader, with real dmini,
dmdrvi ABI and OS services. A test-only port emulates hardware. Use
-DMI2C_HOST_TESTS=ON for a native build; see [testing](docs/testing.md) for
commands and coverage. i2ctest performs actual FT5336 transactions on F746G-DISCO.

## Project structure

```text
include/                 public core, types and minimal port API
src/dmi2c.c              handles and dmdrvi contract
src/config.c             active-section INI configuration
src/transfer.c           portable sequencing, locks, waits and deadlines
src/validation.h         shared argument validation
src/port/stm32_common/   RCC/NVIC and selected F4/F7 IRQ engine
src/port/stm32f4/        IRQ/lifecycle entries and toolchain configuration
src/port/stm32f7/        IRQ/lifecycle entries and toolchain configuration
configs/board/          ready-to-install board GPIO and I2C INIs
configs/mcu/            controller-only defaults
tests/virtual_port/     test-only loadable hardware model
tests/dmi2c_test.c       real-loader integration tests
tools/i2ctest/           explicit probe/register read/hardware regression
docs/                   API, configuration, ports and validation
```
