# Configuration

Each bus is one dmdevfs driver context. Multiple opens share that controller
and timing, while each open retains its own default target address. A second
configuration owning the same controller is rejected.

```ini
[touch_i2c]
driver_name=dmi2c
driver_order=11
friends_group=touch_i2c
instance=3
role=master
address=56
baudrate=100000
timeout_ms=100
```

| Key | Default | Accepted values |
|---|---|---|
| `instance` | required | Positive controller number; the port checks its hardware inventory |
| `role` | master | master |
| `address` | 80 | Decimal 8..119, ordinary unshifted 7-bit addresses |
| `baudrate` | 100000 | 100000, 400000 Hz |
| `timeout_ms` | 100 | 1..60000 ms per complete transfer |

Values are read through dmini_get_int; use decimal integers in INI files.
The driver validates supported ranges before narrowing fields. It does not
implement a separate integer parser or promise stricter parsing than dmini.

The dmdevfs active-section restriction selects the driver configuration.
Every lookup uses section=NULL. The first visible section name is used only
for the optional device name; the driver does not search other sections.
Direct DIF callers must supply an active-section view (or global keys).

Board INIs use driver_order=10 for GPIO and 11 for I2C, after FMC/SDRAM and
DMA setup. This lets driver allocations use the initialized general heap.
friends_group groups the GPIO and bus lifetimes. The driver does not manipulate
GPIO friend paths. Select only the INI for the actual board and controller.

No target reset pin, audio initialization, address discovery or external
pull-up is supplied by the bus driver. GPIO recovery pulses for a slave
holding SDA low require a separate, deliberate board-level recovery policy.
