#ifndef ESP32S3_I2C_H
#define ESP32S3_I2C_H

#include <stdint.h>

/*
 * ESP32-S3: two I2C controllers (I2C0 = instance 1, I2C1 = instance 2), master only here. The signals go to the pads through the
 * GPIO matrix (dmi2c_port_set_pins), there are no fixed pads. Registers: ESP32-S3 Technical Reference Manual, chapter I2C.
 */
#define ESP32S3_I2C_COUNT               2U
#define ESP32S3_I2C_BASE(i)             ((i) == 0U ? 0x60013000UL : 0x60027000UL)
#define ESP32S3_I2C_FIFO_LEN            32U
#define ESP32S3_I2C_SOURCE_CLOCK_HZ     40000000UL      /* XTAL */

/* Registers (offsets) */
#define I2C_SCL_LOW_PERIOD_REG          0x00U
#define I2C_CTR_REG                     0x04U
#define I2C_SR_REG                      0x08U
#define I2C_TO_REG                      0x0CU
#define I2C_FIFO_CONF_REG               0x18U
#define I2C_DATA_REG                    0x1CU
#define I2C_INT_RAW_REG                 0x20U
#define I2C_INT_CLR_REG                 0x24U
#define I2C_INT_ENA_REG                 0x28U
#define I2C_INT_ST_REG                  0x2CU
#define I2C_SDA_HOLD_REG                0x30U
#define I2C_SDA_SAMPLE_REG              0x34U
#define I2C_SCL_HIGH_PERIOD_REG         0x38U
#define I2C_SCL_START_HOLD_REG          0x40U
#define I2C_SCL_RSTART_SETUP_REG        0x44U
#define I2C_SCL_STOP_HOLD_REG           0x48U
#define I2C_SCL_STOP_SETUP_REG          0x4CU
#define I2C_FILTER_CFG_REG              0x50U
#define I2C_CLK_CONF_REG                0x54U
#define I2C_COMD_REG(n)                 (0x58U + 4U * (n))
#define I2C_COMMANDS                    8U

/* CTR */
#define I2C_CTR_SDA_FORCE_OUT           (1UL << 0)
#define I2C_CTR_SCL_FORCE_OUT           (1UL << 1)
#define I2C_CTR_MS_MODE                 (1UL << 4)
#define I2C_CTR_TRANS_START             (1UL << 5)
#define I2C_CTR_CLK_EN                  (1UL << 8)
#define I2C_CTR_FSM_RST                 (1UL << 10)
#define I2C_CTR_CONF_UPGATE             (1UL << 11)
/* SR */
#define I2C_SR_BUS_BUSY                 (1UL << 4)
/* FIFO_CONF */
#define I2C_FIFO_RX_RST                 (1UL << 12)
#define I2C_FIFO_TX_RST                 (1UL << 13)
/* TO */
#define I2C_TO_VALUE_M                  0x1FUL
#define I2C_TO_EN                       (1UL << 5)
/* SCL_HIGH_PERIOD: high in [8:0], wait-high in [15:9] */
#define I2C_SCL_WAIT_HIGH_S             9U
/* FILTER_CFG: SCL threshold [3:0], SDA threshold [7:4], enables */
#define I2C_FILTER_SCL_EN               (1UL << 8)
#define I2C_FILTER_SDA_EN               (1UL << 9)
/* CLK_CONF: divider [7:0], fractional A [13:8], B [19:14], source (0: XTAL), active */
#define I2C_CLK_SCLK_ACTIVE             (1UL << 21)

/* Interrupts */
#define I2C_INT_RXFIFO_OVF              (1UL << 2)
#define I2C_INT_END_DETECT              (1UL << 3)
#define I2C_INT_ARBITRATION_LOST        (1UL << 5)
#define I2C_INT_MST_TXFIFO_UDF          (1UL << 6)
#define I2C_INT_TRANS_COMPLETE          (1UL << 7)
#define I2C_INT_TIME_OUT                (1UL << 8)
#define I2C_INT_NACK                    (1UL << 10)
#define I2C_INT_SCL_ST_TO               (1UL << 13)
#define I2C_INT_SCL_MAIN_ST_TO          (1UL << 14)
#define I2C_INT_ALL                     0x3FFFFUL

/* Commands: byte count [7:0], ack check enable [8], ack value expected [9], ack value to send [10], opcode [13:11] */
#define I2C_OP_WRITE                    1UL
#define I2C_OP_STOP                     2UL
#define I2C_OP_READ                     3UL
#define I2C_OP_END                      4UL
#define I2C_OP_RSTART                   6UL
#define I2C_CMD(op, bytes, ack_en, ack_exp, ack_val) \
    ((uint32_t)(bytes) | ((uint32_t)(ack_en) << 8) | ((uint32_t)(ack_exp) << 9) | ((uint32_t)(ack_val) << 10) | ((op) << 11))

/* System: clocks and resets of the controllers, PERIP_CLK_EN0 / PERIP_RST_EN0 */
#define ESP32S3_SYSTEM_BASE             0x600C0000UL
#define ESP32S3_PERIP_CLK_EN0_REG       (ESP32S3_SYSTEM_BASE + 0x18UL)
#define ESP32S3_PERIP_RST_EN0_REG       (ESP32S3_SYSTEM_BASE + 0x20UL)
#define ESP32S3_I2C_PERIP_BIT(i)        ((i) == 0U ? (1UL << 7) : (1UL << 18))

/* GPIO matrix and IO MUX */
#define ESP32S3_GPIO_BASE               0x60004000UL
#define ESP32S3_GPIO_PIN_REG(pad)       (ESP32S3_GPIO_BASE + 0x74UL + 4UL * (pad))
#define ESP32S3_GPIO_PIN_PAD_DRIVER     (1UL << 2)      /* open-drain */
#define ESP32S3_GPIO_FUNC_IN_SEL_REG(s) (ESP32S3_GPIO_BASE + 0x154UL + 4UL * (s))
#define ESP32S3_GPIO_FUNC_OUT_SEL_REG(p) (ESP32S3_GPIO_BASE + 0x554UL + 4UL * (p))
#define ESP32S3_GPIO_IN_SEL_ROUTED      (1UL << 7)
#define ESP32S3_IO_MUX_REG(pad)         (0x60009004UL + 4UL * (pad))
#define ESP32S3_IO_MUX_PU               (1UL << 8)
#define ESP32S3_IO_MUX_IE               (1UL << 9)
#define ESP32S3_IO_MUX_MCU_SEL_S        12U
#define ESP32S3_IO_MUX_MCU_SEL_M        (7UL << ESP32S3_IO_MUX_MCU_SEL_S)
#define ESP32S3_IO_MUX_FUNC_GPIO        1UL
/* Signals of the GPIO matrix (gpio_sig_map.h): the same index for the input and the output */
#define ESP32S3_I2C_SIG_SCL(i)          ((i) == 0U ? 89UL : 91UL)
#define ESP32S3_I2C_SIG_SDA(i)          ((i) == 0U ? 90UL : 92UL)

/*
 * The interrupt of a controller is a source of the interrupt matrix (I2C_EXT0 = 42, I2C_EXT1 = 43, level) that goes to a level 1 line
 * of the CPU (the lines may call the RTOS from their handlers). 4 and 5 are used by nothing else of the firmware (the GPIO takes 2,
 * the UARTs 3, 9 and 12, the USB Serial/JTAG 13). The handlers are the modules' DMOD_IRQ_HANDLER(line).
 */
#define ESP32S3_I2C_CPU_INTR(i)         ((i) == 0U ? 4U : 5U)
#define ESP32S3_INTMTX_I2C_MAP_REG(i)   (0x600C2000UL + 4UL * ((i) == 0U ? 42UL : 43UL))

#endif /* ESP32S3_I2C_H */
