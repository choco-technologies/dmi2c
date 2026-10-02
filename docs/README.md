# dmi2c — I2C master devices

The dmi2c driver exposes STM32F4/F7 I2C controllers as dmdevfs character
devices. It supports 7-bit target addresses, configured 100/400 kHz, read,
write, explicit address probes and message vectors with repeated START.
Transfers use interrupts. The calling task sleeps until completion or timeout.

## Configure a bus

Install dmi2c and the matching dmi2c_port module. Copy one configuration for
your board into /configs/drivers/dmi2c/. The board configurations include
GPIO alternate functions and open-drain outputs. External connectors need
real pull-up resistors; an INI cannot supply them.

For STM32F746G-DISCO, select configs/board/stm32f746g-disco/i2c3.ini.
It uses PH7/PH8 for the onboard FT5336 touch controller at address 56 (0x38).
The GPIO sections run at driver_order=10 and I2C at driver_order=11, after
SDRAM initialization. Do not configure these pins or this controller twice.

The I2C section is:

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

INI numbers use the decimal format supported by dmini_get_int. Each opened
file inherits the configured address, and may change its own address by ioctl.
The resulting device is /dev/dmi2cx/touch_i2c.

## Try the onboard touch controller

```text
i2ctest /dev/dmi2cx/touch_i2c probe 0x38
i2ctest /dev/dmi2cx/touch_i2c read 0x38 0xa8 1
```

The second command writes the register pointer and reads with repeated START.
FT5336 returns chip ID 51 (hex). Command-line addresses accept hexadecimal;
INI configuration uses decimal. A probe of an absent target returns -ENXIO.

## Use from another module

Include dmi2c_types.h and use Dmod_FileOpen, Dmod_FileRead, Dmod_FileWrite,
Dmod_Ioctl and Dmod_FileClose. A read/write is one complete bus message.
For register reads use dmi2c_ioctl_cmd_transfer with a pointer-write message
followed by a read message. The whole vector has one lock and one timeout.
There are no public validation functions; validation is internal to the driver.

## Read further in this viewer

```text
dmf-man dmi2c api-reference
dmf-man dmi2c configuration
dmf-man dmi2c port-implementation
dmf-man dmi2c testing
```

Slave mode, 10-bit addressing, DMA and GPIO clock-pulse recovery are not
implemented. A target holding SDA low can keep timing out after local recovery.
