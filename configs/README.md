# Board configurations

These are dmdevfs INIs, with actual GPIO routing and the bus in one file.
GPIO sections use driver_order=10, the bus uses 11 (after SDRAM setup),
and GPIOs use AF4/open-drain. INI numbers are decimal: address=56 means 0x38. Copy the selected file
into `/configs/drivers/dmi2c/`; do not install all board files simultaneously.
The default address belongs to each open handle and can be changed with ioctl.
Do not configure the same peripheral or pins a second time in another driver.

| Board | Bus | SCL / SDA | Target / connector |
|---|---|---|---|
| NUCLEO-F401RE | I2C1 | PB8 / PB9 | CN5 D15 / D14 |
| NUCLEO-F411RE | I2C1 | PB8 / PB9 | CN5 D15 / D14 |
| NUCLEO-F446RE | I2C1 | PB8 / PB9 | CN5 D15 / D14 |
| NUCLEO-F767ZI | I2C1 | PB8 / PB9 | CN7 pins 2 / 4, D15 / D14 |
| STM32F746G-DISCO | I2C1 | PB8 / PB9 | CN5 D15 / D14 |
| STM32F746G-DISCO | I2C3 | PH7 / PH8 | LCD FT5336 (0x38), audio WM8994 (0x1a) |
| STM32F4-DISCOVERY | I2C1 | PB6 / PB9 | CS43L22 (0x4a), release PD4 reset separately |

Extension headers need an external target and pull-ups to 3.3 V, typically
2.2–4.7 kOhm as appropriate to bus capacitance. Internal GPIO pull-ups do not
replace I2C pull-ups. `0x50` is an example external EEPROM address, not a claim
that an EEPROM is fitted. D14/D15 are used directly; A4/A5 routing may require
solder-bridge changes. The onboard audio config only exposes the control bus.
F4 Discovery's codec remains in reset until its audio client releases PD4.

Sources checked for routing:

- [ST UM1724, Nucleo-64 MB1136](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf)
- [ST UM1974, Nucleo-144 MB1137](https://www.st.com/resource/en/user_manual/um1974-stm32-nucleo144-boards-mb1137-stmicroelectronics.pdf)
- [ST UM1907, STM32F746G-DISCO](https://www.st.com/resource/en/user_manual/um1907-discovery-kit-for-stm32f7-series-with-stm32f746ng-mcu-stmicroelectronics.pdf)
- [ST UM1472, STM32F4 Discovery](https://www.st.com/resource/en/user_manual/dm00039084-discovery-kit-with-stm32f407vg-mcu-stmicroelectronics.pdf)

`mcu/` contains controller defaults, without a board pin assignment. Electrical
validation on hardware is recorded separately in the test report; schematic
verification alone does not constitute a physical-board test.
