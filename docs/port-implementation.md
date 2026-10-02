# Port implementation

`dmi2c` and `dmi2c_port` are separate DMOD libraries. Core links dynamically to
three port operations: init(config), deinit(instance), transfer(instance,vector).
The family `port.c` files contain only registration and lifecycle calls selecting
classic or modern I2C. All implementation lives in `stm32_common/`:

- `stm32_common.c`: instance validation, ownership, RCC enable/reset, clock
  derivation, one module-wide OS mutex, transaction deadlines and local recovery.
  One mutex deliberately serializes all controller operations, including
  lifecycle. Separate buses can be configured concurrently but do not transfer
  in parallel in this version. OS services are dynamically linked via dmosi.
- `i2c_v1.c`: classic F4 SR1/SR2 state machine, CCR/TRISE, explicit ACK/POS
  sequencing for 1/2/3-byte receive tails, including repeated START after reads.
- `i2c_v2.c`: F7 CR2/NBYTES/RELOAD, TC/TCR, TXIS/RXNE, ICR clearing, and a timing
  search satisfying SCL high/low and SDA/SCL setup/hold constraints.

F4 and F7 do **not** have the same I2C register map. Shared orchestration selects
the proper engine without duplicating clock/error/lifecycle logic in each port.
Both engines poll; there are no enabled peripheral interrupts or DMA channels.
Critical sections cover only short RCC or ACK/STOP/data-access sequences;
waiting never occurs with interrupts disabled.

SYSCLK is obtained from dmclk_port; AHB and APB1 prescalers are decoded from
RCC_CFGR. F7 explicitly selects PCLK1 for the claimed controller, preserves
unrelated selectors, and restores its previous selector on deinit. F4 uses
PCLK1 directly. No fallback to an assumed reset clock is used. F7 timing
assumes analog filtering enabled (50..260 ns), DNF=0, specification-maximum
rise/fall times, and limits the fastest possible bus speed to the request.

A controller with RCC clock already enabled is not silently reset or stolen.
On NACK/bus error/timeout, a bounded STOP attempt is followed by a local reset
and timing restoration. Arbitration loss does not generate STOP or reset the
controller while another master may own the bus. Error flags are cleared at
the next transaction. No automatic retry is performed. A physical stuck bus
can continue to time out after local reset.

Use only peripheral instances actually present in the chosen MCU; the family
limits are not a part-number inventory. Board configs select known instances.
Dynamic system-clock changes require deinitializing and recreating the bus.

Reference manuals: [ST RM0090 (F4)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf),
[ST RM0385 (F74/F75)](https://www.st.com/resource/en/reference_manual/dm00124865-stm32f75xxx-and-stm32f74xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).
Board pin mappings and source manuals are listed under configs.
