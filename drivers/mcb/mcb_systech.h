/*
 * Copyright (c) 2025 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <zephyr/kernel.h>

#define MCB_MAX_PORT 0x80

#define MCB_REG_CTRL1          0x0000
#define MCB_REG_CTRL2          0x0004
#define MCB_REG_STATUS1        0x0008
#define MCB_REG_I_A_COUNT      0x000C
#define MCB_REG_I_B_COUNT      0x0010
#define MCB_REG_DEVICE_INF0    0x0014
#define MCB_REG_DEVICE_INF1    0x0018
#define MCB_REG_DEVICE_INF2    0x001C
#define MCB_REG_PORT_MASK0     0x0020
#define MCB_REG_PORT_MASK1     0x0024
#define MCB_REG_PORT_MASK2     0x0028
#define MCB_REG_PORT_MASK3     0x002C
#define MCB_REG_PORT_RDY_MASK0 0x0030
#define MCB_REG_PORT_RDY_MASK1 0x0034
#define MCB_REG_PORT_RDY_MASK2 0x0038
#define MCB_REG_PORT_RDY_MASK3 0x003C
#define MCB_REG_PORT_W_MASK0   0x0040
#define MCB_REG_PORT_W_MASK1   0x0044
#define MCB_REG_PORT_W_MASK2   0x0048
#define MCB_REG_PORT_W_MASK3   0x004C
#define MCB_PORT_MAP(n)        mcb_port_map_(n)
#define MCB_REG_PORT_RX_LEN(n) (0x50 + MCB_PORT_MAP(n) * 16)
#define MCB_REG_PORT_RX_MAX(n) (0x54 + MCB_PORT_MAP(n) * 16)
#define MCB_REG_PORT_TX_LEN(n) (0x58 + MCB_PORT_MAP(n) * 16)
#define MCB_REG_PORT_RX_SID(n) (0x5C + MCB_PORT_MAP(n) * 16)
#define MCB_REG_PORT_RX(n)     (MCB_PORT_MAP(n) * 0x800)
#define MCB_REG_PORT_TX(n)     (MCB_PORT_MAP(n) * 0x800)

static inline uint32_t mcb_port_map_(uint32_t raw)
{
	switch (raw) {
	case 0:
		return 0;
	case 0x10:
		return 1;
	case 0x40:
		return 2;
	}
	return 0xFF;
}

/* For MCB_CTRL1 */
#define b_MCB_CTRL1_TxEN       BIT(0)
#define b_MCB_CTRL1_RxEN       BIT(1)
#define r_MCB_CTRL1_R_Ack_pos  2
#define r_MCB_CTRL1_R_Ack_mask (BIT(2) - 1)
#define R_Ack_echo             0
#define R_Ack_reset            1
#define R_Ack_set              2
#define r_MCB_CTRL1_PORT_pos   8
#define r_MCB_CTRL1_PORT_mask  (BIT(8) - 1)
#define r_MCB_CTRL1_D_SID_pos  16
#define r_MCB_CTRL1_D_SID_mask (BIT(8) - 1)
#define r_MCB_CTRL1_S_SID_pos  24
#define r_MCB_CTRL1_S_SID_mask (BIT(8) - 1)

/* For MCB_CTRL2 */
#define r_MCB_CTRL2_DR_pos  0
#define r_MCB_CTRL2_DR_mask (BIT(8) - 1)
#define DR_Unknown          0x00
#define DR_MPU_B            0x01
#define DR_EXT_B            0x02
#define DR_IO               0x03
#define DR_ETH              0x04
#define DR_MPU_P            0x81
#define DR_EXT_P            0x82
#define r_MCB_CTRL2_DT_pos  8
#define r_MCB_CTRL2_DT_mask (BIT(8) - 1)
#define DT_Unknown          0x00
#define DT_MPU_P            0x01
#define DT_MPU_B            0x02
#define DT_IO_AI            0x10
#define DT_IO_AO            0x11
#define DT_IO_DI            0x12
#define DT_IO_DO            0x13
#define DT_ETH              0x20
#define DT_CAN              0x21
#define DT_EXT_P            0x30
#define DT_EXT_B            0x31
#define r_MCB_CTRL2_PT_pos  16
#define r_MCB_CTRL2_PT_mask (BIT(16) - 1)

/* For MCB_STATUS1 */
#define b_MCB_STATUS1_RE          BIT(0)
#define b_MCB_STATUS1_TE          BIT(1)
#define b_MCB_STATUS1_IE          BIT(2)
#define b_MCB_STATUS1_RF          BIT(3)
#define b_MCB_STATUS1_PE          BIT(4)
#define b_MCB_STATUS1_RDY         BIT(7)
#define r_MCB_STATUS1_RxPort_pos  8
#define r_MCB_STATUS1_RxPort_mask (BIT(8) - 1)

#define _REG_MASK(reg, bit_name) r_MCB_##reg##_##bit_name##_mask
#define _REG_POS(reg, bit_name)  r_MCB_##reg##_##bit_name##_pos
#define REG2VALUE(reg, bit_name, value)                                                            \
	(((value) >> _REG_POS(reg, bit_name)) & _REG_MASK(reg, bit_name))
#define VALUE2REG(reg, bit_name, value)                                                            \
	(((value) & _REG_MASK(reg, bit_name)) << _REG_POS(reg, bit_name))
