/** Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __FMQL_PINCTRL_H__
#define __FMQL_PINCTRL_H__

/**
 * @brief pinmux configuration
 *
 * - ???'??'?'1: QSPI
 * - ???'??'1:0: USB
 * - ???'01'0'0: NOR Flash
 * - ???'10'0'0: NAND Flash
 * - ???'11'0'0: SDIO Pwr
 * - 000'00'0'0: GPIO
 * - 001'00'0'0: CAN
 * - 010'00'0'0: I2C
 * - 011'00'0'0: PJTAG
 * - 100'00'0'0: SDIO
 * - 101'00'0'0: SPI
 * - 110'00'0'0: TTC
 * - 111'00'0'0: UART
 */
#define PINMUX_GPIO       0x00
#define PINMUX_QSPI       0x01
#define PINMUX_USB        0x02
#define PINMUX_NOR_FLASH  0x04
#define PINMUX_NAND_FLASH 0x08
#define PINMUX_SDIO_PWR   0x0C
#define PINMUX_CAN        0x10
#define PINMUX_I2C        0x20
#define PINMUX_PJTAG      0x30
#define PINMUX_SDIO       0x40
#define PINMUX_SPI        0x50
#define PINMUX_TTC        0x60
#define PINMUX_UART       0x70
#define PINMUX_MDIO0      0x80
#define PINMUX_MDIO1      0xa0

#define MIO  0
#define EMIO 1

/* MUX: |31----24|23----16|15----8|7----0|
 *      |   MIO  |        |  PIN  |  MUX |
 */
#define PINMUX_SHIFTER          0
#define PINMUX_MASK             0x7F
#define PIN_SHIFTER             8
#define PIN_MASK                0xFF
#define MIO_SHIFTER             24
#define MIO_MASK                1
#define FMQL_SHIFT(val, prefix) (((val) & prefix##_MASK) << prefix##_SHIFTER)
#define FMQL_PINMUX(mio, pin, mode)                                                                \
	(FMQL_SHIFT(mio, MIO) | FMQL_SHIFT(pin, PIN) | FMQL_SHIFT(mode, PINMUX))

#define IO_TYPE_DISABLE  0
#define IO_TYPE_LVCMOS18 1
#define IO_TYPE_LVCMOS25 2
#define IO_TYPE_LVCMOS33 3
#define IO_TYPE_HSTL_I   4
#define IO_TYPE_HSTL_II  5

#define FMQL_HW_HZ_EN_SHIFT    0
#define FMQL_HW_SEL_SHIFT      1
#define FMQL_HW_SPEED_SHIFT    8
#define FMQL_HW_TYPE_SHIFT     9
#define FMQL_HW_PULLUP_SHIFT   12
#define FMQL_HW_RECV_DIS_SHIFT 13
#define FMQL_HW_STRENGTH_SHIFT 14
#define FMQL_HW_HYST_EN_SHIFT  16
#define FMQL_HW_PULLDOWN_SHIFT 17
#define FMQL_HW_KEEPER_SHIFT   18

#endif
