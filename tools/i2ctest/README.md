# i2ctest

Run against a dmdevfs-configured bus:

```text
i2ctest /dev/dmi2cx/touch_i2c probe 0x38
i2ctest /dev/dmi2cx/touch_i2c read 0x38 0xa8 1
i2ctest /dev/dmi2cx/touch_i2c ft5336
```

Addresses are unshifted. `read` writes a one-byte register pointer then reads
with repeated START. There is no automatic bus scan or arbitrary payload write.
`ft5336` is specifically for STM32F746G-DISCO's FT5336: expected chip ID 0x51,
1/2/3/4-byte reads, 256-byte read through RELOAD, 100 repeated reads, independent
handle addresses, unknown ioctl and expected NACK from unpopulated address 0x77.
Use that regression only when no other device occupies 0x77. It only writes
register pointers, not register values. Exit status is zero only on success.
