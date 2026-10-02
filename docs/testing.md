# Validation and reproduction

## Host regressions

```sh
cmake -S tests/host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host --parallel 2
ctest --test-dir build-host --output-on-failure
```

The tests compile the production driver and STM32 register engines. Both suites
pass: `register_protocols` and `driver_contract`. Coverage includes:

- F4 ACK/POS tails and F7 RELOAD at lengths 1, 2, 3, 4, 255, 256, 510 and 511;
- repeated START, including read followed by write, and address-only probes;
- NACK, arbitration loss, bus error, overrun, busy bus and total deadlines;
- reusing a controller after errors, clock ownership and selector restoration;
- independent register-address expectations for all supported instances;
- timing constraints at representative APB clocks and both supported baudrates;
- named INI sections, numeric overflow, invalid configurations, allocation
  failures, initialization rollback and independent addresses for open handles;
- dmdrvi read/write/ioctl/stat behavior and propagation of port errors.

AddressSanitizer and UndefinedBehaviorSanitizer can be enabled with:

```sh
cmake -S tests/host -B build-host-sanitize \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-host-sanitize --parallel 2
ctest --test-dir build-host-sanitize --output-on-failure
```

LeakSanitizer requires an environment that permits its process inspection.
Mocked registers do not establish electrical timing or replace an F4 board test.

## Cross compilation

Both `stm32f4` and `stm32f7` builds produce the core, port, API test and
`i2ctest` modules. See the root README for commands. CI runs the host suites
and discovers both family directories for ARM builds.

## Connected-board regression

Use STM32F746G-DISCO with its original LCD/touch assembly and the board's
`i2c3.ini`. GPIO PH7/PH8 are AF4 open-drain; FT5336 is at unshifted address
0x38. No external peripheral is needed. Run:

```text
test_dmi2c
i2ctest /dev/dmi2cx/touch_i2c ft5336
```

The hardware regression verifies ID 0x51 at register 0xa8, repeated-START
reads of 1–4 bytes, a 256-byte read crossing NBYTES/RELOAD, 100 repeated reads,
independent handle addresses, an unknown ioctl and an expected NACK from 0x77.
Only register pointers are written. Ensure no target occupies 0x77 before
running this board-specific test. Run separately with `baudrate=100000` and
`baudrate=400000` in the INI; the controller must be released and recreated
(or reboot the firmware) after changing configuration.

The connected F746 board passed this regression at configured 100 kHz and 400 kHz on
2026-10-02. The final driver binary passed the 400 kHz run, including the
separate API suite (2/2). This is a functional transfer test, without oscilloscope or logic
analyzer measurement. F4 and the other board configurations have build/model
and documented pin-routing coverage, but have not been physically tested.

When injecting local modules into dmod-boot, replace the matching `.dmf`, remove
stale `.dmfc` for those modules and rebuild `modules.dmp`; otherwise compressed
released modules may shadow the local implementation. Preserve the original
flash before testing and restore it afterward.
