/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _FMSH_MDIO_H_
#define _FMSH_MDIO_H_

#include "fmsh_common.h"
#include "fmsh_gmac_lib.h"
#include "fmsh_gmac_hw.h"
#include "motorcomm_yt8521.h"

/* ************************ Constant Definitions **************************** */
#define FMSH_ENET_PHY_DEBUG 1

/* ID */
#define MVL88E_ID 0x141

/* phy device */
#define PHY_GENERIC    0
#define PHY_88E1111    1
#define PHY_88E1116R   2
#define PHY_KSZ9031RNX 3
#define PHY_YT8521     4
#define PHY_YT8512     5
#define PHY_RTL8211    6

/* return code */
#define ETHERNET_PHY_OK        GMAC_RETURN_CODE_OK
#define ETHERNET_PHY_PARAM_ERR GMAC_RETURN_CODE_PARAM_ERR
#define ETHERNET_PHY_ERR       GMAC_RETURN_CODE_ERR
#define ETHERNET_PHY_TIMEOUT   GMAC_RETURN_CODE_TIME_OUT

/* page */
#define PAGE0 0
#define PAGE1 1
#define PAGE2 2
#define PAGE3 3
#define PAGE4 4
#define PAGE5 5
#define PAGE6 6

#define FMSH_ENET_PHY_TIMEOUT      1000 /* ms */
#define FMSH_ENET_PHY_LINK_TIMEOUT 1000 /* ms */

/* generic reg */
#define ENET_PHY_CTRL 0
#define ENET_PHY_STS  1
#define ENET_PHY_ID1  2
#define ENET_PHY_ID2  3

/*
 * *************************************************************************
 * Control Register - Register 0
 * ***************************************************************************
 */
#define ENET_PHY_CTRL_RST     (0x1u << 15) /* PHY software reset:1 for reset */
#define ENET_PHY_CTRL_LOOPBK  (0x1u << 14) /* loopback TXD to RXD */
#define ENET_PHY_CTRL_SPEED_L (0x1u << 13) /* speed select LSB [bit6,bit13] */

/* 10=1000; 01=100; 00=10 */
#define ENET_PHY_CTRL_AN_EN       (0x1u << 12) /* auto-negotiation enable */
#define ENET_PHY_CTRL_POWER_DOWN  (0x1u << 11) /* power down */
#define ENET_PHY_CTRL_RST_CP_AN   (0x1u << 9)  /* Restart Copper Auto-Negotiation */
#define ENET_PHY_CTRL_CP_DPLX_MOD (0x1u << 8)  /* Copper Duplex Mode. 1=full; 0=half */
#define ENET_PHY_CTRL_SPEED_H     (0x1u << 6)  /* speed select hsb */

/*
 * *************************************************************************
 * Status Register -  Register 1
 * ***************************************************************************
 */
#define ENET_PHY_STS_ANF      (0x1u << 5) /* AN finish:1 for finish */
#define ENET_PHY_STS_LNK_STAT (0x1u << 2) /* Copper link status, 1 up, 0 down */

/* ************************** Type Definitions ****************************** */

/* *************** Macros (Inline Functions) Definitions ******************** */

/* ************************ Function Prototypes ***************************** */
u8 fmsh_mdio_idle(FGmacPs_Instance_T *pGmac);

/* ************************ Variable Definitions **************************** */
u8 fmsh_mdio_idle(FGmacPs_Instance_T *pGmac);
u8 fmsh_mdio_device_detect(FGmacPs_Instance_T *pGmac);
u8 fmsh_mdio_init(FGmacPs_Instance_T *pGmac);
u8 fmsh_mdio_reset(FGmacPs_Instance_T *pGmac);
u32 fmsh_mdio_write(FGmacPs_Instance_T *pGmac, u8 regAddr, u32 regdata);
u32 fmsh_mdio_read(FGmacPs_Instance_T *pGmac, u8 regAddr);
void fmsh_mdio_reg_dump(FGmacPs_Instance_T *pGmac);
#endif
