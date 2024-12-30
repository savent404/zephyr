/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _FMSH_YT8521_H_
#define _FMSH_YT8521_H_

/* *************************** Include Files ******************************** */
#include "fmsh_common.h"
#include "fmsh_gmac_lib.h"
#include "fmsh_gmac_hw.h"
#include "fmsh_mdio.h"
/* ************************ Constant Definitions **************************** */

/* ID */
#define YT8521_ID1_VAL 0x4f51
#define YT8521_ID2_VAL 0xe91a

/* status & function */
#define YT8521_PHY_TIME_OUT      10000
#define YT8521_PHY_LINK_TIME_OUT 1000

/* timing ctrl */

/* reg map - IEEE */
#define YT8521_CTRL        0    /* Control Register */
#define YT8521_STAT        1    /* Status Register */
#define YT8521_ID1         2    /* PHY ID */
#define YT8521_ID2         3    /* PHY ID */
#define YT8521_ANA         4    /* Auto-Neg Advertisement Register */
#define YT8521_LPA         5    /* Auto-Neg Link Partner Ability Register */
#define YT8521_ANE         6    /* Auto-Neg Expansion Register */
#define YT8521_NPTX        7    /* Auto-Neg Link Next Page */
#define YT8521_LPNPA       8    /* Auto-Neg Link Partner Next Page Ability */
#define YT8521_1000CT      9    /* 1000BASE-T Control Register */
#define YT8521_1000ST      0xA  /* 1000BASE-T Status Register */
#define YT8521_MMDC        0xD  /* MMD ctrl */
#define YT8521_MMDD        0xE  /* mmd reg/data */
#define YT8521_ES          0xF  /* extended status */
#define YT8521_RMLP        0x11 /* remote loopback */
#define YT8521_LMDCD       0x12 /* LinkMD Cable Diagnostic */
#define YT8521_DGPS        0x13 /* Digital PMA/PCS Status */
#define YT8521_RXERC       0x15 /* RXER Counter */
#define YT8521_ITCS        0x1B /* Interrupt Ctrl/Status */
#define YT8521_AMDIX       0x1C /* Auto MDI/MDI-X */
#define YT8521_PAGE_SELECT 0x1F /* PAGE SELECT */

#define PHY_RTL8211x_FORCE_MASTER       BIT(1)
#define PHY_RTL8211E_PINE64_GIGABIT_FIX BIT(2)

#define PHY_AUTONEGOTIATE_TIMEOUT 5000

/* RTL8211x 1000BASE-T Control Register */
#define MIIM_RTL8211x_CTRL1000T_MSCE   BIT(12);
#define MIIM_RTL8211x_CTRL1000T_MASTER BIT(11);

/* RTL8211x PHY Status Register */
#define MIIM_RTL8211x_PHY_STATUS      0x11
#define MIIM_RTL8211x_PHYSTAT_SPEED   0xc000
#define MIIM_RTL8211x_PHYSTAT_GBIT    0x8000
#define MIIM_RTL8211x_PHYSTAT_100     0x4000
#define MIIM_RTL8211x_PHYSTAT_DUPLEX  0x2000
#define MIIM_RTL8211x_PHYSTAT_SPDDONE 0x0800
#define MIIM_RTL8211x_PHYSTAT_LINK    0x0400

/* RTL8211x PHY Interrupt Enable Register */
#define MIIM_RTL8211x_PHY_INER     0x12
#define MIIM_RTL8211x_PHY_INTR_ENA 0x9f01
#define MIIM_RTL8211x_PHY_INTR_DIS 0x0000

/* RTL8211x PHY Interrupt Status Register */
#define MIIM_RTL8211x_PHY_INSR 0x13

/* RTL8211F PHY Status Register */
#define MIIM_RTL8211F_PHY_STATUS      0x1a
#define MIIM_RTL8211F_AUTONEG_ENABLE  0x1000
#define MIIM_RTL8211F_PHYSTAT_SPEED   0x0030
#define MIIM_RTL8211F_PHYSTAT_GBIT    0x0020
#define MIIM_RTL8211F_PHYSTAT_100     0x0010
#define MIIM_RTL8211F_PHYSTAT_DUPLEX  0x0008
#define MIIM_RTL8211F_PHYSTAT_SPDDONE 0x0800
#define MIIM_RTL8211F_PHYSTAT_LINK    0x0004

#define MIIM_RTL8211E_CONFREG       0x1c
#define MIIM_RTL8211E_CONFREG_TXD   0x0002
#define MIIM_RTL8211E_CONFREG_RXD   0x0004
#define MIIM_RTL8211E_CONFREG_MAGIC 0xb400 /* Undocumented */

#define MIIM_RTL8211E_EXT_PAGE_SELECT 0x1e

#define MIIM_RTL8211F_PAGE_SELECT 0x1f
#define MIIM_RTL8211F_TX_DELAY    0x100
#define MIIM_RTL8211F_LCR         0x10

/* *************** Macros (Inline Functions) Definitions ******************** */

/* ************************ Function Prototypes ***************************** */
u8 yt8521_init(FGmacPs_Instance_T *pGmac);
u8 yt8521_reset(FGmacPs_Instance_T *pGmac);
u8 yt8521_cfg(FGmacPs_Instance_T *pGmac);
u8 yt8521_reg_dump(FGmacPs_Instance_T *pGmac);

#endif /* _FMSH_YT8521_H_ */
