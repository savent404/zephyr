/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __GMAC_PHY___
#define __GMAC_PHY___

#include "fmsh_gmac_hw.h"
#include "fmsh_gmac_lib.h"
#include "fmsh_ps_parameters.h"
#include "fmsh_common.h"

extern u32 g_link_speed_neg_en;
extern u32 g_GmacLinkStatus;

int FGmacPs_GmacLink_Updata(FGmacPs_Instance_T *pGmac);
int FGmacPS_Gmii2rgmii_Update_Speed1(FGmacPs_Instance_T *pGmac);
int FGmacPS_Gmii2rgmii_Update_Speed2(FGmacPs_Instance_T *pGmac);

#endif
