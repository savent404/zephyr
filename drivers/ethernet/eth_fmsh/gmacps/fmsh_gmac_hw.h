/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _FMSH_GMAC_HW_H_
#define _FMSH_GMAC_HW_H_

#include "fmsh_common.h"
#include "fmsh_gmac_lib.h"
/* ************************ Constant Definitions **************************** */
/* DMA Reg0:DMA_BMR (DMA Offset: 0x000) Bus Mode Register */
#define GDMA_BMR_SWR   (0x1u)        /* software reset */
#define GDMA_BMR_DA    (0x1u << 1)   /*  */
#define GDMA_BMR_DSL   (0x1Fu << 2)  /*  */
#define GDMA_BMR_ATDS  (0x1u << 7)   /*  */
#define GDMA_BMR_PBL   (0x3Fu << 8)  /* Programmable Burst Length */
#define GDMA_BMR_PR_1  (0x0u << 14)  /* Priority Ration is 1:1 */
#define GDMA_BMR_PR_2  (0x1u << 14)  /* Priority Ration is 2:1 */
#define GDMA_BMR_PR_3  (0x2u << 14)  /* Priority Ration is 3:1 */
#define GDMA_BMR_PR_4  (0x3u << 14)  /* Priority Ration is 4:1 */
#define GDMA_BMR_FB    (0x1u << 16)  /* Fixed Burst */
#define GDMA_BMR_RPBL  (0x3Fu << 17) /*  */
#define GDMA_BMR_USP   (0x1u << 23)  /*  */
#define GDMA_BMR_PBLx8 (0x1u << 24)  /*  */
#define GDMA_BMR_AAL   (0x1u << 25)  /* Address*Aligned Burst */
#define GDMA_BMR_MB    (0x1u << 26)  /* Mixed Burst */
#define GDMA_BMR_TXPR  (0x1u << 27)  /* Transmit Priority */
#define GDMA_BMR_PRWG  (0x3u << 28)  /*  */

/* DMA Reg5:DMA_SR (DMA Offset: 0x014) Status Register */
#define GDMA_SR_TI    (0x1u)       /* Transmit Interrupt */
#define GDMA_SR_TPS   (0x1u << 1)  /* Transmit Process Stopped */
#define GDMA_SR_TU    (0x1u << 2)  /* Transmit Buffer Unavailable */
#define GDMA_SR_TJT   (0x1u << 3)  /* Transmit Jabber Timeout */
#define GDMA_SR_OVF   (0x1u << 4)  /* Receive Overflow */
#define GDMA_SR_UNF   (0x1u << 5)  /* Transmit Underflow */
#define GDMA_SR_RI    (0x1u << 6)  /* Receive Interrupt */
#define GDMA_SR_RU    (0x1u << 7)  /* Receive Buffer Unavailable */
#define GDMA_SR_RPS   (0x1u << 8)  /* Receive Process Stopped */
#define GDMA_SR_RWT   (0x1u << 9)  /* Receive Watchdog Timeout */
#define GDMA_SR_ETI   (0x1u << 10) /* Early Transmit Interrupt */
#define GDMA_SR_FBI   (0x1u << 13) /* Fatal Bus Error Interrupt */
#define GDMA_SR_ERI   (0x1u << 14) /* Early Receive Interrupt */
#define GDMA_SR_AIS   (0x1u << 15) /* Abnormal Interrupt Summary */
#define GDMA_SR_NIS   (0x1u << 16) /* Normal Interrupt Summary */
#define GDMA_SR_RS    (0x7u << 17) /* 3*bit Receive Process State */
#define GDMA_SR_TS    (0x7u << 20) /* 3*bit Transmit Process State */
#define GDMA_SR_EB    (0x7u << 23) /* 3*bit Error Bits */
#define GDMA_SR_GLI   (0x1u << 26) /* GMAC Line Interface Interrupt */
#define GDMA_SR_GMI   (0x1u << 27) /* GMAC MMC Interrupt */
#define GDMA_SR_GPI   (0x1u << 28) /* GMAC PMT Interrupt */
#define GDMA_SR_TTI   (0x1u << 29) /* Timestamp Trigger Interrupt */
#define GDMA_SR_GLPII (0x1u << 30) /* GMAC LPI Interrupt(for Channel 0) */

/* DMA Reg6:DMA_OMR (DMA Offset: 0x018) Operation Mode Register */
#define GDMA_OMR_SR      (0x1u << 1)  /* Start or Stop Receive */
#define GDMA_OMR_OSF     (0x1u << 2)  /* Operation on Second Frame */
#define GDMA_OMR_RTC     (0x3u << 3)  /* Receive Threshold Control */
#define GDMA_OMR_RTC_64  (0x0u << 3)  /* Receive Threshold Control */
#define GDMA_OMR_RTC_32  (0x1u << 3)  /* Receive Threshold Control */
#define GDMA_OMR_RTC_96  (0x2u << 3)  /* Receive Threshold Control */
#define GDMA_OMR_RTC_128 (0x3u << 3)  /* Receive Threshold Control */
#define GDMA_OMR_DGF     (0x1u << 5)  /*  */
#define GDMA_OMR_FUF     (0x1u << 6)  /* forward undersized good frames */
#define GDMA_OMR_FEF     (0x1u << 7)  /* Forward Error Frame */
#define GDMA_OMR_EFC     (0x1u << 8)  /* Enable HW flow control */
#define GDMA_OMR_RFA_1K  (0x0u << 9)  /* Threshold for Act Flow Ctrl. Full - 1K */
#define GDMA_OMR_RFA_2K  (0x1u << 9)  /* Full - 2K */
#define GDMA_OMR_RFA_3K  (0x2u << 9)  /* Full - 3K */
#define GDMA_OMR_RFA_4K  (0x3u << 9)  /* Full - 4K */
#define GDMA_OMR_RFD_1K  (0x0u << 11) /* Threshold for deAct Flow Ctrl. Full - 1K */
#define GDMA_OMR_RFD_2K  (0x1u << 11) /* Full - 2K */
#define GDMA_OMR_RFD_3K  (0x2u << 11) /* Full - 3K */
#define GDMA_OMR_RFD_4K  (0x3u << 11) /* Full - 4K */
#define GDMA_OMR_ST      (0x1u << 13) /* Start or Stop Transmission Command */
#define GDMA_OMR_TTC     (0x7u << 14) /* Transmit Threshold Control */
#define GDMA_OMR_TTC_64  (0x0u << 14) /* Transmit Threshold Control */
#define GDMA_OMR_TTC_128 (0x1u << 14) /* Transmit Threshold Control */
#define GDMA_OMR_TTC_192 (0x2u << 14) /* Transmit Threshold Control */
#define GDMA_OMR_TTC_256 (0x3u << 14) /* Transmit Threshold Control */
#define GDMA_OMR_TTC_40  (0x4u << 14) /* Transmit Threshold Control */
#define GDMA_OMR_TTC_32  (0x5u << 14) /* Transmit Threshold Control */
#define GDMA_OMR_TTC_24  (0x6u << 14) /* Transmit Threshold Control */
#define GDMA_OMR_TTC_16  (0x7u << 14) /* Transmit Threshold Control */
#define GDMA_OMR_FTF     (0x1u << 20) /*  */
#define GDMA_OMR_TSF     (0x1u << 21) /* Transmit Store and Forward */
#define GDMA_OMR_RFD2    (0x1u << 22) /* RFD MSB */
#define GDMA_OMR_RFA2    (0x1u << 23) /* RFA MSB */
#define GDMA_OMR_DFF     (0x1u << 24) /* Disable Flush of Rx frame */
#define GDMA_OMR_RSF     (0x1u << 25) /* Receive Store and Forward */
#define GDMA_OMR_DT      (0x1u << 26) /*  */

/* DMA Reg7:DMA_IER (DMA Offset: 0x018) Interrupt Enable Register */
#define GDMA_IER_TIE (0x1u)       /* Transmit Interrupt Enable */
#define GDMA_IER_TSE (0x1u << 1)  /* Transmit Stop Enable */
#define GDMA_IER_TUE (0x1u << 2)  /* Transmit Buffer Unavailable Enable */
#define GDMA_IER_TJE (0x1u << 3)  /* Transmit Jabber Timeout Enable */
#define GDMA_IER_OVE (0x1u << 4)  /* Overflow Interrupt Enable */
#define GDMA_IER_UNE (0x1u << 5)  /* Underflow Interrupt Enable */
#define GDMA_IER_RIE (0x1u << 6)  /* Receive Interrupt Enable */
#define GDMA_IER_RUE (0x1u << 7)  /* Receive Buffer Unavailable Enable */
#define GDMA_IER_RSE (0x1u << 8)  /* Receive Stop Enable */
#define GDMA_IER_RWE (0x1u << 9)  /* Receive Watchdog Timeout Enable */
#define GDMA_IER_ETE (0x1u << 10) /* Early Transmit Interrupt Enable */
#define GDMA_IER_FBE (0x1u << 13) /* Fatal Bus Error Enable */
#define GDMA_IER_ERE (0x1u << 14) /* Early Receive Interrupt Enable */
#define GDMA_IER_AIE (0x1u << 15) /* Abnormal Interrupt Summary Enable */
#define GDMA_IER_NIE (0x1u << 16) /* Normal Interrupt Summary Enable */

/* DMA Reg10:DMA_ABM (DMA Offset: 0x028) AXI Bus Mode Register */
#define GDMA_ABM_UNDEF   (0x1u)
#define GDMA_ABM_BLEN4   (0x1u << 1)
#define GDMA_ABM_BLEN8   (0x1u << 2)
#define GDMA_ABM_BLEN16  (0x1u << 3)
#define GDMA_ABM_BLEN32  (0x1u << 4)
#define GDMA_ABM_BLEN64  (0x1u << 5)
#define GDMA_ABM_BLEN128 (0x1u << 6)
#define GDMA_ABM_BLEN256 (0x1u << 7)

/* DMA Reg11:DMA_AASR (DMA Offset: 0x02C) AHB or AXI Status Register */
#define GDMA_AXWHSTS  (0x1u)
#define GDMA_AXIRDSTS (0x1u << 1)

/* DMA Reg22:GDMA_HWFT (DMA Offset: 0x058) HW Feature Register */
#define GDMA_HWFT_ACTPHYIF_GMIIMII (0x0u << 28)
#define GDMA_HWFT_ACTPHYIF_RGMII   (0x1u << 28)
#define GDMA_HWFT_ACTPHYIF_SGMII   (0x2u << 28)
#define GDMA_HWFT_ACTPHYIF_TBI     (0x3u << 28)
#define GDMA_HWFT_ACTPHYIF_RMII    (0x4u << 28)
#define GDMA_HWFT_ACTPHYIF_RTBI    (0x5u << 28)
#define GDMA_HWFT_ACTPHYIF_SMII    (0x6u << 28)
#define GDMA_HWFT_ACTPHYIF_RevMII  (0x7u << 28)
#define GDMA_HWFT_ACTPHYIF         (0x7u << 28) /* Active or selected PHY interface */
#define GDMA_HWFT_SAVLANINS        (0x1u << 27) /* Source Address or VLAN Insertion */
#define GDMA_HWFT_FLEXIPPSEN       (0x1u << 26) /* Flexible Pulse-Per-Second Output */
#define GDMA_HWFT_INTTSEN          (0x1u << 25) /* Timestamping with Internal System Time */
#define GDMA_HWFT_ENHDESSEL        (0x1u << 24) /* Alternate (Enhanced Descriptor) */
#define GDMA_HWFT_TXCHCNT          (0x3u << 22) /* Number of additional Tx Channels */
#define GDMA_HWFT_RXCHCNT          (0x3u << 20) /* Number of additional Rx Channels */
#define GDMA_HWFT_RXFIFOSIZE       (0x1u << 19) /* Rx FIFO > 2,048 Bytes */
#define GDMA_HWFT_RXTYP2COE        (0x1u << 18) /* IP Checksum Offload (Type 2) in Rx */
#define GDMA_HWFT_RXTYP1COE        (0x1u << 17) /* IP Checksum Offload (Type 1) in Rx */
#define GDMA_HWFT_TXCOESEL         (0x1u << 16) /* Checksum Offload in Tx */
#define GDMA_HWFT_AVSEL            (0x1u << 15) /* AV feature */
#define GDMA_HWFT_EEESEL           (0x1u << 14) /* Energy Efficient Ethernet */
#define GDMA_HWFT_TSVER2SEL        (0x1u << 13) /* IEEE 1588-2008 Advanced timestamp */
#define GDMA_HWFT_TSVER1SEL        (0x1u << 12) /* Only IEEE 1588-2002 timestamp */
#define GDMA_HWFT_MMCSEL           (0x1u << 11) /* RMON module */
#define GDMA_HWFT_MGKSEL           (0x1u << 10) /* PMT magic packet */
#define GDMA_HWFT_RWKSEL           (0x1u << 9)  /* PMT remote wake-up frame */
#define GDMA_HWFT_SMASEL           (0x1u << 8)  /* SMA (MDIO) Interface */
#define GDMA_HWFT_L3L4FLTREN       (0x1u << 7)  /* Layer 3 and Layer 4 feature */
/* PCS registers (TBI, SGMII, or RTBI PHY interface) */
#define GDMA_HWFT_PCSSEL           (0x1u << 6)
#define GDMA_HWFT_ADDMACADRSEL     (0x1u << 5) /* Multiple MAC Address registers */
#define GDMA_HWFT_HASHSEL          (0x1u << 4) /* HASH filter */
#define GDMA_HWFT_EXTHASHEN        (0x1u << 3) /* Expanded DA Hash filter */
#define GDMA_HWFT_HDSEL            (0x1u << 2) /* Half-duplex support */
#define GDMA_HWFT_GMIISEL          (0x1u << 1) /* 1000 Mbps support */
#define GDMA_HWFT_MIISEL           (0x1u)      /* 10 or 100 Mbps support */

/* MAC Reg0:GMAC_MCR (Gmac Offset: 0x000) MAC Configuration Register */
#define GMAC_MCR_PRELEN   (0x3u)      /* Preamble Length for Transmit frames */
#define GMAC_MCR_PRELEN_7 (0x0u)      /* Preamble Length 7 */
#define GMAC_MCR_PRELEN_5 (0x1u)      /* Preamble Length 5 */
#define GMAC_MCR_PRELEN_3 (0x2u)      /* Preamble Length 3 */
#define GMAC_MCR_RE       (0x1u << 2) /* Receiver Enable */
#define GMAC_MCR_TE       (0x1u << 3) /* Transmiter Enable */

#define GMAC_MCR_ACS (0x1u << 7) /* Automatic pad or CRC stripping */
#define GMAC_MCR_LUD (0x1u << 8) /*  */

#define GMAC_MCR_IPC (0x1u << 10) /*  */
#define GMAC_MCR_DM  (0x1u << 11) /* Duplex Mode */
#define GMAC_MCR_LM  (0x1u << 12) /* Loopback Mode */

#define GMAC_MCR_FES (0x1u << 14) /* speed:0 for 10;1 for 100 */
#define GMAC_MCR_PS  (0x1u << 15) /* Port Select:0 for 1000;1 for 10/100 */

#define GMAC_MCR_IFG    (0x7u << 17) /* Inter*Frame Gap */
#define GMAC_MCR_IFG_96 (0x0u << 17) /* Inter*Frame Gap:96 bit times */
#define GMAC_MCR_IFG_88 (0x1u << 17) /* Inter*Frame Gap:88 bit times */
#define GMAC_MCR_IFG_80 (0x2u << 17) /* Inter*Frame Gap:80 bit times */
#define GMAC_MCR_IFG_72 (0x3u << 17) /* Inter*Frame Gap:72 bit times */
#define GMAC_MCR_IFG_64 (0x4u << 17) /* Inter*Frame Gap:64 bit times */
#define GMAC_MCR_IFG_56 (0x5u << 17) /* Inter*Frame Gap:56 bit times */
#define GMAC_MCR_IFG_48 (0x6u << 17) /* Inter*Frame Gap:48 bit times */
#define GMAC_MCR_IFG_40 (0x7u << 17) /* Inter*Frame Gap:40 bit times */

#define GMAC_MCR_JE (0x1u << 20) /* Jumbo Frame En */

#define GMAC_MCR_JD  (0x1u << 22) /* Jabber Disable */
#define GMAC_MCR_WD  (0x1u << 23) /* Watchdog disable */
#define GMAC_MCR_TC  (0x1u << 24) /*  */
#define GMAC_MCR_CST (0x1u << 25) /* CRC Stripping for type frames */

#define GMAC_MCR_TWOKPE (0x1u << 27) /*  */

/* #define GMAC_MCR_SARC	   (0x7u << 28) */
/* MAC Reg1:GMAC_MFF (Gmac Offset: 0x004) MAC Frame Filter */
#define GMAC_MFF_PR     (0x1u)       /* Promiscuous Mode */
#define GMAC_MFF_HUC    (0x1u << 1)  /*  */
#define GMAC_MFF_HMC    (0x1u << 2)  /*  */
#define GMAC_MFF_DAIF   (0x1u << 3)  /* DA Inverse Filter */
#define GMAC_MFF_PM     (0x1u << 4)  /* Pass All Multicast */
#define GMAC_MFF_DBF    (0x1u << 5)  /* disable broadcast frame */
#define GMAC_MFF_PCF    (0x3u << 6)  /* Pass Control Frames */
#define GMAC_MFF_PCF_00 (0x00u << 6) /* filters all control frames */

/*                                                                             \
 * forwards all control frames except Pause frames to application even if they \
 * fail the Address filter.                                                    \
 */
#define GMAC_MFF_PCF_01 (0x1u << 6)

/*                                                                             \
 * forwards all control frames to application even if they fail the Address    \
 * Filter                                                                      \
 */
#define GMAC_MFF_PCF_10 (0x2u << 6)

/* MAC forwards control frames that pass the Address Filter. */
#define GMAC_MFF_PCF_11 (0x3u << 6)
#define GMAC_MFF_SAIF   (0x1u << 8)  /* SA Inverse Filter */
#define GMAC_MFF_SAF    (0x1u << 9)  /* Source Address Filter Enable */
#define GMAC_MFF_HPF    (0x1u << 10) /*  */
#define GMAC_MFF_VTFE   (0x1u << 16) /* VLAN Tag Filter Enable */
#define GMAC_MFF_IPFE   (0x1u << 20) /*  */
#define GMAC_MFF_DNTU   (0x1u << 21) /*  */
#define GMAC_MFF_RA     (0x1u << 31) /* Receive all */

/* MAC Reg4:GMAC_GAR (Gmac Offset: 0x010) GMII Address Register */
#define GMAC_GAR_GB (0x1u)        /* GMII Busy */
#define GMAC_GAR_GW (0x1u << 1)   /* GMII write */
#define GMAC_GAR_CR (0xFu << 2)   /* CSR clock range */
#define GMAC_GAR_GR (0x1Fu << 6)  /* GMII register */
#define GMAC_GAR_PA (0x1Fu << 11) /* physical layer address */

/* MAC Reg5:GMAC_GDR (Gmac Offset: 0x014) GMII Data Register */
#define GMAC_GDR_GD (0xFF) /* GMII Data */

/* MAC Reg6: Flow Control Register */
#define GMAC_FCR_PT      (0xFFFFu << 16) /* Pause time */
#define GMAC_FCR_DZQP    (0x1u << 7)     /* Disable Zero-Quanta Pause */
#define GMAC_FCR_PLT_4   (0x00u << 4)    /* Pause low threshold = PT *4 slot times */
#define GMAC_FCR_PLT_28  (0x01u << 4)    /* Pause low threshold = PT *28 slot times */
#define GMAC_FCR_PLT_144 (0x02u << 4)    /* Pause low threshold = PT *144 slot times */
#define GMAC_FCR_PLT_256 (0x03u << 4)    /* Pause low threshold = PT *256 slot times */
#define GMAC_FCR_UP      (0x1u << 3)     /* Unicast Pause Frame Detect */
#define GMAC_FCR_RFE     (0x1u << 2)     /* Receive flow control enable */
#define GMAC_FCR_TFE     (0x1u << 1)     /* Transmit flow control enable */
#define GMAC_FCR_FCB     (0x1u)          /* Flow control busy */

/* MAC Reg7: VLAN Tag Register */
#define GMAC_VTR_ESVL (0x1u << 18) /* Enable S-VLAN */
#define GMAC_VTR_VTIM (0x1u << 17) /* VLAN Tag Inverse Match Enable */
#define GMAC_VTR_ETV  (0x1u << 16) /* Enable,only 12*bit VLAN ID for comparison */
#define GMAC_VTR_VL   (0xFFFF)     /* VLAN Tag Identifier for Receive Frames */
#define GMAC_VTR_VID  (0xFFF)      /* VLAN Identifier */

/* MAC Reg9: debug reg */
#define GMAC_DBR_RPESTS      (0x1u) /* MAC GMII or MII Receive Protocol Engine Status */
/* MAC Receive Frame FIFO Controller Status, small FIFO Read controller */
#define GMAC_DBR_RFCFCSTS_WR (0x1u << 1)
/* MAC Receive Frame FIFO Controller Status, small FIFO Write controller */
#define GMAC_DBR_RFCFCSTS_RD (0x1u << 2)
#define GMAC_DBR_RWCSTS      (0x1u << 4)  /* MTL Rx FIFO Write Controller Active Status */
#define GMAC_DBR_RRCSTS      (0x3u << 5)  /* MTL RxFIFO Read Controller State */
#define GMAC_DBR_RXFSTS      (0x3u << 8)  /* MTL RxFIFO Fill-Level Status */
#define GMAC_DBR_TPESTS      (0x1u << 16) /* MAC GMII or MII Transmit Protocol Engine Status */
#define GMAC_DBR_TFCSTS      (0x3u << 17) /* MAC Transmit Frame Controller Status */
#define GMAC_DBR_TXPAUSED    (0x1u << 19) /* MAC Transmitter in Pause */
#define GMAC_DBR_TRCSTS      (0x3u << 20) /* MTL Tx FIFO Read Controller Status */
#define GMAC_DBR_TWCSTS      (0x1u << 22) /* MTL Tx FIFO Write Controller Status */
#define GMAC_DBR_TXFSTS      (0x1u << 24) /* MTL Tx FIFO Not Empty Status */
#define GMAC_DBR_TXSTSFSTS   (0x1u << 25) /* MTL TxStatus FIFO Full Status */

/* MAC Reg14: Interrupt Status Register */
#define GMAC_ISR_RGSMIIIS (0x1u) /* RGMII or SMII Interrupt Status */

/* MAC Reg54: SGMII/RGMII/SMII Status Register */
/* Delay SMII RX Data Sampling with respect to the SMII SYNC Signal */
#define GMAC_MII_STS_SMIDRXS       (0x1u << 16)
#define GMAC_MII_STS_FALSCARDET    (0x1u << 5) /* False Carrier Detected */
#define GMAC_MII_STS_JABTO         (0x1u << 4) /* Jabber Timeout */
#define GMAC_MII_STS_LNKSTS        (0x1u << 3) /* Link status */
#define GMAC_MII_STS_LNKSPEED      (0x3u << 1) /* Link speed */
#define GMAC_MII_STS_LNKSPEED_2M5  (0x0u << 1) /* Link speed */
#define GMAC_MII_STS_LNKSPEED_25M  (0x1u << 1) /* Link speed */
#define GMAC_MII_STS_LNKSPEED_125M (0x2u << 1) /* Link speed */
#define GMAC_MII_STS_LNKMOD        (0x1u)      /* Link mod */

/* MAC Reg544 */
#define GMAC_MAH_AE (0x1u << 31) /* Address enable */
#define GMAC_MAH_SA (0x1u << 30) /* Source addr: 1*compare with SA; 0*compare with DA */

/* MAC Reg55:GMAC_WDT */
#define GMAC_WDT_PWE (0x1u << 16) /* Programmable Watchdog Enable */
#define GMAC_WDT_WTO (0x3FFF)     /* Watchdog Timeout */

/* ************************** Type Definitions ****************************** */
/* DMA Reg Map */
typedef struct _Dma {
	volatile u32 GDMA_BMR; /* Reg0:DMA_BMR (DMA Offset: 0x000) Bus Mode Register */
	volatile u32 GDMA_TPD; /* Reg1:DMA_TPD (DMA Offset: 0x004) Transmit Poll Demand Register */
	volatile u32 GDMA_RPD; /* Reg2:DMA_RPD (DMA Offset: 0x008) Receive Poll Demand Register */
	/* Reg3:DMA_RDLA (DMA Offset: 0x00C) Receive Descroptor List Address */
	volatile u32 GDMA_RDLA;
	/* Reg4:DMA_TDLA (DMA Offset: 0x010) Transmit Descroptor List Address */
	volatile u32 GDMA_TDLA;
	volatile u32 GDMA_SR;  /* Reg5:DMA_SR (DMA Offset: 0x014) Status Register */
	volatile u32 GDMA_OMR; /* Reg6:DMA_OMR (DMA Offset: 0x018) Operation Mode Register */
	volatile u32 GDMA_IER; /* Reg7:DMA_IER (DMA Offset: 0x01C) Interrupt Enable Register */
	volatile u32 GDMA_MF_BOC;
	volatile u32 GDMA_RIWT;
	volatile u32 GDMA_ABM; /* Reg10:DMA_ABM (DMA Offset: 0x028) AXI Bus Mode Register */
	volatile u32 GDMA_ASR; /* Reg11:DMA_ASR (DMA Offset: 0x02C) AHB or AXI Status Register */
	volatile u32 GDMA_RSVD[6]; /* Reg12-17:reserved */
	/*
	 * Reg18:GDMA_CTXDES (DMA Offset: 0x048) Current Host Transmit
	 * Descriptor Register
	 */
	volatile u32 GDMA_CTXDES;
	/*
	 * Reg19:GDMA_CRXDES (DMA Offset: 0x04C) Current Host Receive
	 * Descriptor Register
	 */
	volatile u32 GDMA_CRXDES;
	/*
	 * Reg20:GDMA_CTXBUFADD (DMA Offset: 0x050) Current Host
	 * Transmit Buffer Address Register
	 */
	volatile u32 GDMA_CTXBUFADD;
	/*
	 * Reg21:GDMA_CRXBUFADD (DMA Offset: 0x054) Current Host
	 * Receive Buffer Address Register
	 */
	volatile u32 GDMA_CRXBUFADD;
	volatile u32 GDMA_HWFT; /* Reg22:GDMA_HWFT (DMA Offset: 0x058) HW Feature Register */
} FGmacPs_DmaPortMap_T;

/* MAC Reg Map */
typedef struct _GMacAddress {
	volatile u32 GMAC_MAH; /* Mac Address High */
	volatile u32 GMAC_MAL; /* Mac Address Low */
} FGmacPs_MacAddress_T;

typedef struct _Gmac {
	volatile u32 GMAC_MCR; /* Reg0:GMAC_MCR (Gmac Offset: 0x000) MAC Configuration Register */
	volatile u32 GMAC_MFF; /* Reg1:GMAC_MFF (Gmac Offset: 0x004) MAC Frame Filter */
	volatile u32 GMAC_HTH; /* Reg2:GMAC_HTH (Gmac Offset: 0x008) Hash Table High Register */
	volatile u32 GMAC_HTL; /* Reg3:GMAC_HTL (Gmac Offset: 0x00C) Hash Table Low Register */
	volatile u32 GMAC_GAR; /* Reg4:GMAC_GAR (Gmac Offset: 0x010) GMII Address Register */
	volatile u32 GMAC_GDR; /* Reg5:GMAC_GDR (Gmac Offset: 0x014) GMII Data Register */
	volatile u32 GMAC_FCR; /* Reg6:GMAC_FCR (Gmac Offset: 0x018) Flow Control Register */
	volatile u32 GMAC_VTR; /* Reg7:GMAC_VTR (Gmac Offset: 0x01C) VLAN Tag Register */
	volatile u32 GMAC_VR;  /* Reg8:GMAC_VR  (Gmac Offset: 0x020) Version Register */
	volatile u32 GMAC_DBR; /* Reg9:GMAC_DBR (Gmac Offset: 0x024) Debug Register */
	/*
	 * Reg10:GMAC_RWFF (Gmac Offset: 0x028) Remote Wake*Up Frame Filter
	 * Register
	 */
	volatile u32 GMAC_RWFF;
	/* Reg11:GMAC_PCS (Gmac Offset: 0x02C) PMT Control and Status Register */
	volatile u32 GMAC_PCS;
	/* Reg12:GMAC_LCS (Gmac Offset: 0x030) LPI Control and Status Register */
	volatile u32 GMAC_LCS;
	volatile u32 GMAC_LTC; /* Reg13:GMAC_LTC (Gmac Offset: 0x034) LPI Timer Control Register */
	volatile u32 GMAC_ISR; /* Reg14:GMAC_ISR (Gmac Offset: 0x038) Interrupt Status Register */
	volatile u32 GMAC_IMR; /* Reg15:GMAC_IMR (Gmac Offset: 0x03C) Interrupt Mask Register */
	FGmacPs_MacAddress_T GMAC_MAR[16]; /* (Gmac Offset: 0x040) MAC Address 0..15 */
	volatile u32 GMAC_AN_CTRL;         /* Reg48: (Gmac Offset: 0xC0)) AN Control Register */
	volatile u32 GMAC_AN_STS;          /* Reg49: (Gmac Offset: 0xC4)) AN Stauts Register */
	volatile u32 GMAC_AN_ADV;          /*  */
	volatile u32 GMAC_AN_LPA;          /*  */
	volatile u32 GMAC_AN_EXP;          /*  */
	volatile u32 GMAC_TBI_STS;         /*  */
	volatile u32 GMAC_MII_STS; /* Reg54: (Gmac Offset: 0xD8) SGMII/RGMII/SMII Status Register */
	volatile u32 GMAC_WDT;     /* Reg55: (Gmac Offset: 0xDC) Watchdog Timeout Register */
	volatile u32 GMAC_GPIO;    /*  */
} FGmacPs_MacPortMap_T;
/* *************** Macros (Inline Functions) Definitions ******************** */
#define SET_BIT(a, b)   (a) |= (b)
#define RESET_BIT(a, b) (a) &= ~(b)

/* ************************ Function Prototypes ***************************** */
void gmac_dma_enable_rcv(FGmacPs_Instance_T *pGmac, u8 enable);
void gmac_dma_enable_tsv(FGmacPs_Instance_T *pGmac, u8 enable);
u8 gmac_enable_rcv(FGmacPs_Instance_T *pGmac, u8 enable);
u8 gmac_enable_tsv(FGmacPs_Instance_T *pGmac, u8 enable);
void gmac_DmaTxPollDemand(FGmacPs_Instance_T *pGmac);

void gmac0_bus_rst(FGmacPs_ITF_Type path_sel);
void gmac1_bus_rst(FGmacPs_ITF_Type path_sel);

u8 FGmac_Ps_SetupTxMode(FGmacPs_Instance_T *pGmac, u32 mode);
u8 FGmac_Ps_SetupRxMode(FGmacPs_Instance_T *pGmac, u32 mode);
u8 FGmac_Ps_EnFwErrFrame(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_EnRxFwUnderSizeGoodFrame(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_EnTxOsf(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_SetupIntr(FGmacPs_Instance_T *pGmac, FGmacPs_DmaIrq_T mask);

u8 FGmac_Ps_TxPreamLeng(FGmacPs_Instance_T *pGmac, u32 value);
u8 FGmac_Ps_InterFrameGap(FGmacPs_Instance_T *pGmac, u32 gap);
u8 FGmac_Ps_Enable2k(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_TypeCrcStrip(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_SetupRxWatchDog(FGmacPs_Instance_T *pGmac, u8 wd_en, u8 prog_en, u16 timeout);
u8 FGmac_Ps_EnTxJabber(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_EnableJumbo(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_SetupSpeed(FGmacPs_Instance_T *pGmac, u8 speed);
u8 FGmac_Ps_LoopbackMode(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_PadCrcStrip(FGmacPs_Instance_T *pGmac, u8 enable);

u8 FGmac_Ps_RxFilt_EnRcvAll(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_RxFilt_EnFwAllMultiCast(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_RxFilt_EnFwBroadCast(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_RxFilt_EnDaInvF(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_RxFilt_EnSaF(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_RxFilt_EnSaInvF(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_RxFilt_CtrlFrame(FGmacPs_Instance_T *pGmac, u8 value);
u8 FGmac_Ps_RxFilt_EnPmsMode(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_FlCtrl_EnHwFlc(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_FlCtrl_EnRx(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_FlCtrl_EnTx(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_FlCtrl_EnUniPauseFraDetect(FGmacPs_Instance_T *pGmac, u8 enable);
u8 FGmac_Ps_FlCtrl_EnZeroQuatPause(FGmacPs_Instance_T *pGmac, u8 enable);

u8 FGmac_Ps_FlwCtrlSetup(FGmacPs_Instance_T *pGmac, u8 tx_flc_en, u8 rx_flc_en, u8 upfd_en,
			 u8 pause_low_th, u8 zq_pause_en, u16 pause_time, u8 fcbba);
u8 FGmac_Ps_SendPauseFrame(FGmacPs_Instance_T *pGmac);

u8 FGmac_Ps_SetupMacAddr(FGmacPs_Instance_T *pGmac, u8 Index, u8 *pMacAddr, u8 En, u8 SA, u8 mask);

/* ************************ Variable Definitions **************************** */

#endif
