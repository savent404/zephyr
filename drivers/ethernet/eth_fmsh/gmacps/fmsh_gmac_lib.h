/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _FMSH_GMAC_LIB_H_
#define _FMSH_GMAC_LIB_H_

/*
 * ***************************************************************************
 *
 * Copyright (C) 2018 - 2028 FMSH, Inc.  All rights reserved.
 *
 * ****************************************************************************
 */
/* ************************************************************************** */
/*
 * @file  fmsh_gmac_lib.h
 *
 * @note		None.
 *
 * MODIFICATION HISTORY:
 *
 * <pre>
 * Ver   Who  Date     Changes
 * ----- ---- -------- ---------------------------------------------
 * 0.01   Hansen Yang  12/24/2018  First Release
 * </pre>
 * ****************************************************************************
 */
/* *************************** Include Files ******************************** */
#include "fmsh_common.h"
/* ************************ Constant Definitions **************************** */
#define GMAC_TIME_OUT_VALUE_1 1000 /* us */

#define GMAC_RDES_NUM             1024 /* number of Rx DES */
#define GMAC_TDES_NUM             128  /* number of Tx DES */
#define GMAC_PACKET_BUFFER_SIZE   3000 /* RxPacketBuffer size */
#define GMAC_TXPACKET_BUFFER_SIZE 1600 /* TxPacketBuffer size */
#define GMAC_RBUFFER_UNIT_SIZE    1600 /* size for each Rx buffer;must be a multiple of 4,8 or 16 */
#define GMAC_TBUFFER_UNIT_SIZE    1600

#define GMAC_POLL_MAX 512

/* return code */
#define GMAC_RETURN_CODE_OK              0
#define GMAC_RETURN_CODE_ERR             1
#define GMAC_RETURN_CODE_TIME_OUT        2
#define GMAC_RETURN_CODE_PARAM_ERR       3 /* Parameter error */
#define GMAC_RETURN_CODE_TX_BUSY         4
#define GMAC_RETURN_CODE_RX_NULL         5 /* No data received */
#define GMAC_RETURN_CODE_SIZE_TOO_SMALL  6
#define GMAC_RETURN_CODE_NOT_INITIALIZED 7
#define GMAC_RETURN_CODE_SIZE_TOO_BIG    8

/* gmac status */
#define GMAC_STATE_RUN   0
#define GMAC_STATE_STOP  1
#define GMAC_STATE_SUSBD 2

/* driver config */
#define GMAC_TX_COPY_MEM 0 /* 0: disable, 1: enable */

/* gmac base */
#define GMAC_DMA_OFFSET 0x1000

/* status & function */
#define GMAC_AXI_BURST_LEHGTH     GDMA_ABM_BLEN4
#define GMAC_BUS_MODE_FIXED_BURST 0
#define GMAC_BUS_MODE_AALB        0

#define GMAC_RCV_ALL           1
#define GMAC_RCV_ALL_MULTICAST 0
#define GMAC_DIS_BROADCAST     0
#define GMAC_SRC_ADDR_FILT     0

#define GMAC_CFG_PAD_CRC_STRIPPING      0
#define GMAC_CFG_CRC_STRIPPING_FOR_TYPE 0
#define GMAC_CFG_LOOPBACK               0
#define GMAC_CFG_2K_FRAME_EN            0
#define GMAC_CFG_JUMBO_EN               0

#define GMAC_VLAN_TAG         0xF05A
#define GMAC_PAUSE_TIME       0x100
#define GMAC_WATCHDOG_TIMEOUT 3000

/* status & function : OP mode */
#define GMAC_OP_MODE_RCV_STORE_FORWARD  1
#define GMAC_OP_MODE_HW_FLOW_CTRL       0
#define GMAC_OP_MODE_RX_FLOW_CTRL       0
#define GMAC_OP_MODE_TX_FLOW_CTRL       0
#define GMAC_OP_MODE_FW_ERR_FRAME       0
#define GMAC_OP_MODE_FW_UDSZ_GOOD_FRAME 0

/* Rx DES bit */
#define GMAC_RDES0_OWN  (0x1u << 31)
#define GMAC_RDES0_AFM  (0x1u << 30)   /* Destination Address Filter Fail */
#define GMAC_RDES0_FL   (0x3FFF << 16) /* Frame Length 0~2048 Bytes */
#define GMAC_RDES0_ES   (0x1u << 15)   /* Error Summary */
#define GMAC_RDES0_SAF  (0x1u << 13)   /* Source Address Filter Fail */
#define GMAC_RDES0_LE   (0x1u << 12)   /* Length Error */
#define GMAC_RDES0_OE   (0x1u << 11)   /* Overflow Error */
#define GMAC_RDES0_VLAN (0x1u << 10)   /* VLAN Tag */
#define GMAC_RDES0_FS   (0x1u << 9)
#define GMAC_RDES0_LS   (0x1u << 8)
#define GMAC_RDES0_RWT  (0x1u << 4) /* Receive Watchdog Timeout */
#define GMAC_RDES0_CE   (0x1u << 1) /* CRC Error */

#define GMAC_RDES1_RER  (0x1u << 25)  /* Receive End of Ring */
#define GMAC_RDES1_RCH  (0x1u << 24)  /* Using second chain */
#define GMAC_RDES1_RBS2 (0x7FF << 10) /* Receive Buffer 2 Size */
#define GMAC_RDES1_RBS1 (0x7FF)       /* Receive Buffer 1 Size */

/* Tx DES bit */
#define GMAC_TDES0_OWN (0x1u << 31)
#define GMAC_TDES0_ES  (0x1u << 15) /* Error Summary */
#define GMAC_TDES0_JT  (0x1u << 14) /* Jabber Timeout */
#define GMAC_TDES0_UF  (0x1u << 1)  /* Underflow Error */

#define GMAC_TDES1_IC   (0x1u << 31) /* Interrupt on Completion */
#define GMAC_TDES1_LS   (0x1u << 30) /* Last Segment */
#define GMAC_TDES1_FS   (0x1u << 29) /* First Segment */
#define GMAC_TDES1_CIC  (0x11 << 27)
#define GMAC_TDES1_DC   (0x1u << 26) /* disable CRC */
#define GMAC_TDES1_TER  (0x1u << 25) /* Transmit End of Ring */
#define GMAC_TDES1_TCH  (0x1u << 24)
#define GMAC_TDES1_DP   (0x1u << 23)  /* disable padding */
#define GMAC_TDES1_TBS2 (0x7FF << 11) /* Transmit Buffer 2 Size */
#define GMAC_TDES1_TBS1 (0x7FF)       /* Transmit Buffer 1 Size */

/* define : gmac link status (PHY interface) */
/* ref@ datasheet DMA reg 22 */
#define PHY_ITF_GMIIMII 0
#define PHY_ITF_RGMII   1
#define PHY_ITF_SGMII   2
#define PHY_ITF_TBI     3
#define PHY_ITF_RMII    4
#define PHY_ITF_RTBI    5
#define PHY_ITF_SMII    6
#define PHY_ITF_RevMII  7

#define PLLCTRL_FBDIV_MASK   0x7f0000
#define PLLCTRL_FBDIV_SHIFT  16
#define PLLCTRL_BPFORCE_MASK (1 << 3)
#define PLLCTRL_BPQUAL_MASK  (1 << 2)

#define PLLCTRL_PWRDWN_MASK  2
#define PLLCTRL_PWRDWN_SHIFT 1

#define PLLCTRL_RESET_MASK  1
#define PLLCTRL_RESET_SHIFT 0

#define CLK_CTRL_SRCSEL_SHIFT 0
#define CLK_CTRL_SRCSEL_MASK  (0x3 << CLK_CTRL_SRCSEL_SHIFT)

#define PLLCLK_CTRL_DIV_MASK         (0x7f)
#define CLK_GEM_TX_CTRL_SRCSEL_SHIFT 6
#define CLK_GEM_TX_CTRL_SRCSEL_MASK  (0x7 << CLK_GEM_TX_CTRL_SRCSEL_SHIFT)
#define CLK_GEM_TX_CTRL_DIV_SHIFT    9
#define CLK_GEM_TX_CTRL_DIV_MASK     (0x3f << CLK_GEM_TX_CTRL_DIV_SHIFT)

#define RXENABLE (1 << 2)
#define TXENABLE (1 << 3)

/* ************************** Type Definitions ****************************** */
/* Tx DES struct */
typedef struct _GmacTxDescriptor {
	volatile union _GmacTDES0 {
		u32 val;

		struct _TDES0BM {
			u32 DB: 1, UF: 1, DE: 1, CC: 4, VF: 1, EC: 1, LateC: 1, NC: 1, LossC: 1,
				PCE: 1, FF: 1, JT: 1, ES: 1, IHE: 1, TTSS: 1, Rsv: 13, OWN: 1;
		} bm;
	} TDES0;

	volatile union _GmacTDES1 {
		u32 val;

		struct _TDES1BM {
			u32 TBS1: 11, TBS2: 11, TTSE: 1, DP: 1, TCH: 1, TER: 1, DC: 1, CIC: 2,
				FS: 1, LS: 1, IC: 1;
		} bm;
	} TDES1;
	volatile u32 BufferAdd1;
	volatile u32 BufferAdd2;
} FGmacPs_TxDescriptor_T;

/* Rx DES struct */
typedef struct _GmacRxDescriptor {
	volatile union _GmacRDES0 {
		u32 val;

		struct _RDES0BM {
			u32 macadd_match: 1, CE: 1, DE: 1, RE: 1, RWT: 1, FT: 1, LateC: 1,
				IPCCKErr: 1, LS: 1, FS: 1, VLAN: 1, OE: 1, LE: 1, SAF: 1, DesErr: 1,
				ES: 1, FL: 14, AFM: 1, OWN: 1;
		} bm;
	} RDES0;

	volatile union _GmacRDES1 {
		u32 val;

		struct _RDES1BM {
			u32 RBS1: 11, RBS2: 11, Rsv: 2, RCH: 1, RER: 1, Rsv2: 5, DIC: 1;

		} bm;
	} RDES1;
	volatile u32 BufferAdd1;
	volatile u32 BufferAdd2;
} FGmacPs_RxDescriptor_T;

/* interface type */
typedef enum _FGmacPs_PathSel {
	gmac_path_gmii = PHY_ITF_GMIIMII,
	gmac_path_rgmii = PHY_ITF_RGMII
} FGmacPs_ITF_Type;

/* speed */
typedef enum _FGmacPs_Speed {
	speed_10 = 0,
	speed_100 = 1,
	speed_1000 = 2
} FGmacPs_Speed;

/* config struct */
typedef struct _gmac_config {
	u16 DeviceId;    /* Unique ID  of device */
	u32 BaseAddress; /* Base address of device */
	FGmacPs_Speed Speed;
	FGmacPs_ITF_Type InterFaceType;

} FGmacPs_Config_T;

/* IRQ */
typedef enum _FGmacPs_DmaIrq_T {
	gdma_irq_none = 0x0,
	gdma_irq_tx = 0x1,
	gdma_irq_tx_stop = 0x2,
	gdma_irq_tx_unbuffer = 0x4,
	gdma_irq_tx_jabber_timeout = 0x8,
	gdma_irq_rx_overflow = 0x10,
	gdma_irq_tx_underflow = 0x20,
	gdma_irq_rx = 0x40,
	gdma_irq_rx_unbuffer = 0x80,
	gdma_irq_rx_stop = 0x100,
	gdma_irq_rx_wd_timeout = 0x200,
	gdma_irq_early_tx = 0x400,
	gdma_irq_fatal_bus = 0x2000,
	gdma_irq_early_rx = 0x4000,
	gdma_irq_aie = 0x8000,
	gdma_irq_nie = 0x10000,
	gdma_irq_gli = 0x4000000,
	gdma_irq_all = 0x1E7FF,
	gdma_irq_all_exp_smry = 0x67FF,
	gdma_irq_all_ni = 0x4045,
	gdma_irq_all_ai = 0x27BA
} FGmacPs_DmaIrq_T;

typedef void (*FMSH_callback)(void *pDev, int32_t eCode);

/* gmac link status struct */
typedef struct _ethernet_link_status {
	u8 smidrxs;        /* Delay SMII RX Data Sampling with respect to the SMII SYNC Signal */
	u8 fals_car_dect;  /* False Carrier Detected */
	u8 jabber_timeout; /* RT */
	u8 link_status;    /* RT */
	u8 link_speed;     /* RT */
	u32 link_mod;      /* RT */
} FGmacPs_LinkStatus_T;

/* gmac config */
/*
 * typedef struct _gmac_config {
 * FGmacPs_Speed speed;
 * FGmacPs_ITF_Type interface;
 * u8 mac_address[6];
 * u8 csr_clk;
 * }FGmacPs_Config_T;
 */
/*
 * Selection  |  CSR Clock      |  MDC Clock
 * 0000       |  60每100 MHz     |  CSR clock/42
 * 0001       |  100每150 MHz    |  CSR clock/62
 * 0010       |  20每35 MHz      |  CSR clock/16
 * 0011       |  35每60 MHz      |  CSR clock/26
 * 0100       |  150每250 MHz    |  CSR clock/102
 * 0101       |  250每300 MHz    |  CSR clock/124
 * 0110, 0111 |  Reserved
 */
typedef struct _gmac_instance_s FGmacPs_Instance_T;

/* phy */
typedef struct _phy_config {
	u8 phy_device;
	u8 auto_detect_ad_en;
	u8 mdio_address;
	u8 link_up;
	u8 auto_nag_en;

	FGmacPs_Speed speed;
	u8 is_duplex;

	FGmacPs_ITF_Type interface;

	u8 (*phy_op_init)(FGmacPs_Instance_T *pGmac);
	u8 (*phy_op_cfg)(FGmacPs_Instance_T *pGmac);
	u8 (*phy_op_reset)(FGmacPs_Instance_T *pGmac);
	u8 (*phy_op_get_status)(FGmacPs_Instance_T *pGmac);
	u8 (*phy_op_reg_dump)(FGmacPs_Instance_T *pGmac);

} FGmacPs_PhyConfig_T;

/* gmac instance struct */
typedef struct _gmac_instance_s {
	u8 index;
	void *base_address;

	/* tx */
	u8 *pTxBuffer;                                                 /* point to TxBuffer */
	FGmacPs_TxDescriptor_T *pTxD; /* point to the list of TxDES */ /* FGmacPs_TxDescriptor_T */
	u32 TxDesBufSize;                                              /* size for each Tx buffer */

	u16 wTxListSize; /* total number of Tx DES */
	u16 wTxHead;     /* the 1st TxDES of frame to send */
	u16 wTxTail;     /* the last TxDES of frame we send previous, use this DES to get status */
	/* rx */
	u8 *pRxBuffer;

	FGmacPs_RxDescriptor_T *pRxD; /* FGmacPs_RxDescriptor_T */
	u32 RxDesBufSize;             /* size for each Rx buffer */

	u16 wRxListSize; /* total number of Rx DES */
	u16 wRxI;
	u8 *pFrmBuffer;
	u32 FrmBufferSize;
	u32 RxFrameSize;

	/* call back function */
	FMSH_callback listener;
	FMSH_callback txCallback;
	FMSH_callback rxCallback;
	/* link status */
	FGmacPs_LinkStatus_T *gmac_link_status; /* FGmacPs_LinkStatus_T */
	/* mac cfg */
	FGmacPs_Config_T *gmac_cfg; /* FGmacPs_Config_T */
	/* phy cfg */
	FGmacPs_PhyConfig_T *phy_cfg; /* FGmacPs_PhyConfig_T */
	/* status descriptor */
	FGmacPs_TxDescriptor_T *tx_last_des; /* FGmacPs_TxDescriptor_T */
	u32 rx_last_des0;

	u8 mac_address[6];
	u8 csr_clk;

} FGmacPs_Instance_T;

/* *************** Macros (Inline Functions) Definitions ******************** */
#define SET_BIT(a, b)   (a) |= (b)
#define RESET_BIT(a, b) (a) &= ~(b)

/* ************************ Function Prototypes ***************************** */
/* des */
u8 FGmac_Ps_InitRxDes(FGmacPs_Instance_T *pGmac);
void FGmac_Ps_ResetCurRxDES(FGmacPs_Instance_T *pGmac, FGmacPs_RxDescriptor_T *pRxD);
u8 FGmac_Ps_InitTxDes(FGmacPs_Instance_T *pGmac);

u8 FGmac_Ps_DmaInit(FGmacPs_Instance_T *pGmac, FGmacPs_RxDescriptor_T *g_GMAC_RxDs,
		    u8 *g_GMAC_RxBuffer, FGmacPs_TxDescriptor_T *g_GMAC_TxDs, u8 *g_GMAC_TxBuffer);

u8 FGmac_Ps_MacInit(FGmacPs_Instance_T *pGmac);

void *FGmac_Ps_RcvPollEFrame(FGmacPs_Instance_T *pGmac, u32 *pRcvSize);
u8 FGmac_Ps_Send(FGmacPs_Instance_T *pGmac, u8 *pBuffer, u32 size, u8 DisCRC, u8 DisPAD);
u8 FGmac_Ps_PreSendCopy(FGmacPs_Instance_T *pGmac, u8 *pFrame, u32 size, u8 DisCRC, u8 DisPAD);

void FGmac_Ps_SetListener(FGmacPs_Instance_T *pGmac, FMSH_callback userFunction);
void FGmac_Ps_SetRxCallback(FGmacPs_Instance_T *pGmac, FMSH_callback userFunction);
void FGmac_Ps_SetTxCallback(FGmacPs_Instance_T *pGmac, FMSH_callback userFunction);

u8 FGmac_Ps_SetTxState(FGmacPs_Instance_T *pGmac, u8 state);
u8 FGmac_Ps_SetRxState(FGmacPs_Instance_T *pGmac, u8 state);
u8 FGmac_Ps_GetLinkStatus(FGmacPs_Instance_T *pGmac);
u8 FGmac_Ps_GetHwFeature(FGmacPs_Instance_T *pGmac);
void FGmac_Ps_ClearIrq(FGmacPs_Instance_T *pGmac, FGmacPs_DmaIrq_T interrupts);

u8 FGmac_Ps_StructInit(FGmacPs_Instance_T *pGmac, FGmacPs_LinkStatus_T *gmac_link_status,
		       FGmacPs_Config_T *gmac_cfg, FGmacPs_PhyConfig_T *phy_cfg, u32 RDes_num,
		       u32 TDes_num, u32 RxDesBufSize, u32 TxDesBufSize, u8 *PacketBuffer,
		       u32 FrmBufferSize);
u8 FGmac_Ps_DeviceReset(FGmacPs_Instance_T *pGmac);

u32 FGmac_Ps_Set_Gem_Rate(FGmacPs_Instance_T *pGmac, u32 GemClkRate);
u8 FGmac_Ps_phy_Init(FGmacPs_PhyConfig_T *phy_cfg);

/* ************************ Variable Definitions **************************** */
#endif
