# dmi2c

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmi2c/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmi2c/actions/workflows/ci.yml)

dmi2c DMOD library module.

## Description

TODO: describe what this module does.

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Testing

Tests are built automatically alongside the module (see `tests/`). Once built,
run them with `ctest`:

```bash
cd build
ctest --output-on-failure
```

`ctest` installs the test module's dependencies with `dmf-get` and then runs
it through `dmod_loader`. To run it manually instead:

```bash
export DMOD_DMF_DIR=$(pwd)/build/dmf
dmf-get install -d ${DMOD_DMF_DIR}/test_dmi2c-local.dmd -y
dmod_loader build/dmf/test_dmi2c.dmf
```

## Usage

<TBD>

This library module provides functions that can be used by other modules:

```c
#include "dmi2c.h"
```

## API

| Function | Description |
|----------|-------------|
| `dmi2c_create()` | Create a new `dmi2c_t` instance. |
| `dmi2c_destroy()` | Destroy an instance created by `_create()`. |
| `dmi2c_is_valid()` | Check whether a handle is a valid instance. |

See [include/dmi2c.h](include/dmi2c.h) for the full
declarations and [docs/api-reference.md](docs/api-reference.md) for the
complete reference.

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Complete API documentation

View documentation using `dmf-man dmi2c`.

## Hardware Port

This module ships two DMOD modules: the architecture-independent
`dmi2c` and `dmi2c_port`, which contains the
architecture-specific implementation. The active architecture is selected via
`DMOD_CPU_FAMILY` (default: `stm32f7`):

```bash
cmake .. -DDMOD_CPU_FAMILY=stm32f7
```

See [docs/port-implementation.md](docs/port-implementation.md) for how to add
another architecture. Port-specific files:

```
├── include/dmi2c_port.h
├── src/port/
│   ├── CMakeLists.txt
│   └── stm32f7/
│       ├── config.cmake
│       └── port.c
└── dmi2c_port.dmr
```
## Project Structure

```
dmi2c/
├── docs/              # Documentation (markdown format)
├── include/           # Public headers
│   └── dmi2c.h
├── src/
│   └── dmi2c.c
├── tests/
│   ├── CMakeLists.txt
│   └── dmi2c_test.c
├── CMakeLists.txt
├── Makefile
├── dmi2c.dmr
└── manifest.dmm
```

## Author

Patryk Kubiak

## License

MIT
