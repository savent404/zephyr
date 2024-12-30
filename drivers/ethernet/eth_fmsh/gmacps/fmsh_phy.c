/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "fmsh_phy.h"
#include "fmsh_mdio.h"

/* 第一片GMII_to_RGMII IP核的MDIO地址:0x1,第二片：0x9 */
#define XILINX_GMII2RGMII_MDIO_ADDR1 0x1
#define XILINX_GMII2RGMII_MDIO_ADDR2 0x9
#define XILINX_GMII2RGMII_CTRL_REG   0x10
#define XILINX_GMII2RGMII_SPEED_1000 0x40
#define XILINX_GMII2RGMII_SPEED_100  0x2000
#define XILINX_GMII2RGMII_SPEED_10   0x0

#define LOG_LEVEL CONFIG_ETHERNET_LOG_LEVEL

LOG_MODULE_REGISTER(fmsh_phy);

u32 g_link_speed_neg_en = 1;     /* auto neg enable flag */
u32 g_GmacLinkStatus;            /* 0-down 1-up */
static u32 g_GmacLinkStatus_Old; /* 0-down 1-up */

#if (GMAC_DEBUG == 1)
static const char *Speed_Desr[3] = {"10M", "100M", "1000M"};
static const char *Duplex_Desr[2] = {"half duplex", "full duplex"};
#endif

/*
 * PC端强制切换速率和模式，可能有中间态，需要延时处理一下，
 * 1.main主循环多次检测，本demo处理方式
 * 2.中断处理函数里置标志，这个主要关注link变化中断是否可以多次进入，
 * 或者启动一个1秒定时器，1秒后做延时处理？
 */
int FGmacPs_GmacLink_Updata(FGmacPs_Instance_T *pGmac)
{
	u32 LinkSpeed = 0;
	u32 LinkSpeed_Old;
	u32 ClkRate;
	u32 Reg_Ctrl;
	u32 DuplMode = 0;
	u32 DuplMode_Old;
	u32 BaseAddress = (u32)pGmac->base_address;

	FGmacPs_LinkStatus_T *pGmacLinkSts = 0;

	pGmacLinkSts = pGmac->gmac_link_status;
	LinkSpeed_Old = pGmacLinkSts->link_speed;
	DuplMode_Old = pGmacLinkSts->link_mod;

	BaseAddress = (u32)(pGmac->base_address);

	FGmac_Ps_GetLinkStatus(pGmac);
	pGmacLinkSts = pGmac->gmac_link_status;

	if ((LinkSpeed_Old == pGmacLinkSts->link_speed) &&
	    (DuplMode_Old == pGmacLinkSts->link_mod) &&
	    (g_GmacLinkStatus_Old == pGmacLinkSts->link_status)) {
		return 0;
	}

	g_GmacLinkStatus = pGmacLinkSts->link_status;
	g_GmacLinkStatus_Old = pGmacLinkSts->link_status;

	FMSH_DEBUG("get gamc LinkSpeed 0x%x,DuplMode 0x%x\r\n", pGmacLinkSts->link_speed,
		   pGmacLinkSts->link_mod);

	LinkSpeed = pGmacLinkSts->link_speed;
	/* it anded with GMAC_MII_STS_LNKMOD, it will be 0 or 1 */
	DuplMode = pGmacLinkSts->link_mod;

	g_GmacLinkStatus = pGmacLinkSts->link_status;

	Reg_Ctrl = FMSH_ReadReg(BaseAddress, 0);

	if (DuplMode == 1) {
		/* full */
		Reg_Ctrl |= GMAC_MCR_DM;
	} else {
		/* half */
		Reg_Ctrl &= ~GMAC_MCR_DM;
	}
	switch (LinkSpeed) {
	case speed_1000:
		ClkRate = 125 * 1000 * 1000;
		Reg_Ctrl &= ~GMAC_MCR_PS;
		FMSH_WriteReg(BaseAddress, 0, Reg_Ctrl);
		break;
	case speed_100:
		ClkRate = 25 * 1000 * 1000;
		Reg_Ctrl |= GMAC_MCR_FES | GMAC_MCR_PS;
		FMSH_WriteReg(BaseAddress, 0, Reg_Ctrl);
		break;
	case speed_10:
		ClkRate = 25 * 100 * 1000;
		Reg_Ctrl &= ~GMAC_MCR_FES;
		Reg_Ctrl |= GMAC_MCR_PS;
		FMSH_WriteReg(BaseAddress, 0, Reg_Ctrl);
		break;
	default:
		ClkRate = 25 * 1000 * 1000;
		break;
	}
	/* FMSH_DEBUG("get ClkRate %d,Reg_Ctrl 0x%x\r\n",ClkRate,Reg_Ctrl); */
	FMSH_DEBUG("LinkSpeed=%d, DuplMode=%d\r\n", LinkSpeed, DuplMode);
	FMSH_DEBUG("speed:%s %s\r\n", Speed_Desr[LinkSpeed], Duplex_Desr[DuplMode]);
	FGmac_Ps_Set_Gem_Rate(pGmac, ClkRate);
	return 0;
}

int FGmacPS_Gmii2rgmii_Update_Speed1(FGmacPs_Instance_T *pGmac)
{
	static u32 LinkSpeed;
	u8 ext_phy_address = 0;

	FGmacPs_PhyConfig_T *pPhyConfig = pGmac->phy_cfg;
	FGmacPs_LinkStatus_T *pGmacLinkSts = 0;

	FGmac_Ps_GetLinkStatus(pGmac);
	pGmacLinkSts = pGmac->gmac_link_status;

	if (LinkSpeed == pGmacLinkSts->link_speed) {
		return 0;
	}

	LinkSpeed = pGmacLinkSts->link_speed;

	ext_phy_address = pPhyConfig->mdio_address;
	pPhyConfig->mdio_address = XILINX_GMII2RGMII_MDIO_ADDR1;
	switch (LinkSpeed) {
	case speed_1000:
		fmsh_mdio_write(pGmac, XILINX_GMII2RGMII_CTRL_REG, XILINX_GMII2RGMII_SPEED_1000);
		break;
	case speed_100:
		fmsh_mdio_write(pGmac, XILINX_GMII2RGMII_CTRL_REG, XILINX_GMII2RGMII_SPEED_100);
		break;
	case speed_10:
		fmsh_mdio_write(pGmac, XILINX_GMII2RGMII_CTRL_REG, XILINX_GMII2RGMII_SPEED_10);
		break;
	default:
		fmsh_mdio_write(pGmac, XILINX_GMII2RGMII_CTRL_REG, XILINX_GMII2RGMII_SPEED_1000);
		break;
	}
	pPhyConfig->mdio_address = ext_phy_address;
	return 0;
}

int FGmacPS_Gmii2rgmii_Update_Speed2(FGmacPs_Instance_T *pGmac)
{
	static u32 LinkSpeed;
	u8 ext_phy_address = 0;

	FGmacPs_PhyConfig_T *pPhyConfig = pGmac->phy_cfg;
	FGmacPs_LinkStatus_T *pGmacLinkSts = 0;

	FGmac_Ps_GetLinkStatus(pGmac);
	pGmacLinkSts = pGmac->gmac_link_status;

	if (LinkSpeed == pGmacLinkSts->link_speed) {
		return 0;
	}

	LinkSpeed = pGmacLinkSts->link_speed;

	ext_phy_address = pPhyConfig->mdio_address;
	pPhyConfig->mdio_address = XILINX_GMII2RGMII_MDIO_ADDR2;
	switch (LinkSpeed) {
	case speed_1000:
		fmsh_mdio_write(pGmac, XILINX_GMII2RGMII_CTRL_REG, XILINX_GMII2RGMII_SPEED_1000);
		break;
	case speed_100:
		fmsh_mdio_write(pGmac, XILINX_GMII2RGMII_CTRL_REG, XILINX_GMII2RGMII_SPEED_100);
		break;
	case speed_10:
		fmsh_mdio_write(pGmac, XILINX_GMII2RGMII_CTRL_REG, XILINX_GMII2RGMII_SPEED_10);
		break;
	default:
		fmsh_mdio_write(pGmac, XILINX_GMII2RGMII_CTRL_REG, XILINX_GMII2RGMII_SPEED_1000);
		break;
	}
	pPhyConfig->mdio_address = ext_phy_address;
	return 0;
}
