# Core and hardware port

The two loadable modules have different responsibilities.

## dmi2c: portable transaction driver

- dmi2c.c implements the dmdrvi DIF, device contexts and per-open addresses.
- config.c reads dmdevfs's active view using dmini_get_int/get_string.
- transfer.c owns per-bus mutexes and completion semaphores, validates and
  sequences complete message vectors, applies the whole-vector deadline,
  sleeps while hardware is active and coordinates cancellation/recovery.
- validation.h contains private portable validation, with no exported API.

A new architecture implements the small asynchronous port contract. It reuses
all configuration, handles, sequencing, locking, timeout and recovery policy,
and the same loader integration suite. Unloading the core releases its driver
contexts, synchronization objects and buffers. The port retains no OS locks,
semaphores or heap-owned transaction data.

## dmi2c_port: hardware operations

The port claims/configures hardware, starts one message, reports completion,
reads BUSY, cancels IRQ access to borrowed buffers and restores registers.
Start does not wait. Last-message selection comes from the core; the port
implements the hardware-specific STOP/repeated-START sequence. Cancelling or
deinitializing must detach all buffer/callback access before returning.

Family port.c files register lifecycle and event/error IRQ entry points.
stm32_common contains RCC/NVIC handling plus separate F4 and F7 engines.
Only the selected family's engine is compiled into its module.

F4 uses SR1/SR2, ACK/POS and short critical sections for the 1/2/3-byte receive
tails. F7 uses TXIS/RXNE, NBYTES/RELOAD and TC/STOPF. ISR handlers advance the
current message without polling. Peripheral interrupt sources are masked before
notifying the core. NVIC priorities use dmosi_get_min_interrupt_priority so the
completion callback may safely post the OS semaphore from an ISR.

The core waits on physical BUSY/STOP by sleeping one millisecond between checks;
payload transfer does not spin. Timeout cancellation masks IRQ access first,
then allows up to 10 ms for STOP before local hardware recovery. Arbitration
loss never forces STOP/reset while another master owns the bus.

SYSCLK comes from dmclk_port; RCC prescalers determine PCLK1. F7 selects PCLK1
and restores the previous selector when released. A controller with its clock
already enabled is rejected rather than stolen. Timing assumes analog filtering,
DNF=0 and conservative rise/fall times. Baudrate is a maximum, not a measured
waveform frequency. Recreate the bus after system-clock changes.

Hardware reference: ST RM0090 (F4) and RM0385 (F74/F75). IRQ mapping was checked
against [ST's CMSIS stm32f746xx.h](https://raw.githubusercontent.com/STMicroelectronics/cmsis-device-f7/master/Include/stm32f746xx.h). The board routing sources are in configs/README.md.
