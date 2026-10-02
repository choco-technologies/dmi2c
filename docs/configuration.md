# Configuration

Each bus is one dmdevfs driver context. Multiple opens share that controller
and timing, while each open retains its own default target address. A second
configuration owning the same controller is rejected.

```ini
[touch_i2c]
driver_name=dmi2c
driver_order=3
friends_group=touch_i2c
instance=3
role=master
address=0x38
baudrate=100000
timeout_ms=100
```

| Key | Default | Accepted values |
|---|---|---|
| `instance` | required | 1..3 F4, 1..4 F7, only instances present on selected part |
| `role` | master | master |
| `address` | 0x50 | unshifted 0x08..0x77 |
| `baudrate` | 100000 | 100000, 400000 Hz |
| `timeout_ms` | 100 | 1..60000 ms per complete transfer |

Numbers accept decimal and `0x` hexadecimal, with full-string and overflow
validation. Named sections determine device names; `[dmi2c]` uses the numeric
instance. Enumeration respects dmini's active-section restriction used by
dmdevfs. GPIOs must be configured first and remain configured throughout use.
`friends_group` groups their lifetime; this driver does not manipulate GPIO
friend paths. Use [board INIs](../configs/README.md) for complete pin setup.

No target reset pin, audio initialization, address discovery or external
pull-up is supplied by the bus driver. GPIO recovery pulses for a slave
holding SDA low require a separate, deliberate board-level recovery policy.
