/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "motorcomm_yt8521.h"

#define LOG_LEVEL CONFIG_ETHERNET_LOG_LEVEL

LOG_MODULE_REGISTER(yt8521);

u32 yt8521_reg_write(FGmacPs_Instance_T *pGmac, u8 page, u8 regAddr, u32 regdata)
{
	u32 ret = 0;

	if (page == 0) {
		ret = fmsh_mdio_write(pGmac, regAddr, regdata);
	} else {
		ret = fmsh_mdio_write(pGmac, YT8521_PAGE_SELECT, page);

		ret = fmsh_mdio_write(pGmac, regAddr, regdata);
	}
	return ret;
}

u32 yt8521_reg_read(FGmacPs_Instance_T *pGmac, u8 page, u8 regAddr)
{
	u32 reg = 0;
	u32 ret = 0;

	if (page == 0) {
		reg = fmsh_mdio_read(pGmac, regAddr);
	} else {
		ret = fmsh_mdio_write(pGmac, YT8521_PAGE_SELECT, page);

		reg = fmsh_mdio_read(pGmac, regAddr);
	}
	return reg;
}

u8 yt8521_reg_dump(FGmacPs_Instance_T *pGmac)
{
	u8 i = 0;
	u16 reg;

	FMSH_DEBUG("yt8521 reg:\r\n");
	for (i = 0; i < 32; i++) {
		reg = yt8521_reg_read(pGmac, PAGE0, i);
		FMSH_DEBUG("reg%d : %x\r\n", i, reg);
	}
	return ETHERNET_PHY_OK;
}

u8 yt8521_detect(FGmacPs_Instance_T *pGmac)
{
	u32 reg1 = yt8521_reg_read(pGmac, PAGE0, YT8521_ID1);
	u32 reg2 = yt8521_reg_read(pGmac, PAGE0, YT8521_ID2);

	if ((reg1 == YT8521_ID1_VAL) && (reg2 == YT8521_ID2_VAL)) {
		FMSH_INFO("YT8521 or alike PHY detect 0x%x \r\n", pGmac->phy_cfg->mdio_address);
		return ETHERNET_PHY_OK;
	}
	FMSH_INFO("PHY detect fail \r\n");

	return ETHERNET_PHY_ERR;
}

u8 yt8521_init(FGmacPs_Instance_T *pGmac)
{
	FGmacPs_PhyConfig_T *pPhyConfig = pGmac->phy_cfg;
	u8 ret;

	if (pPhyConfig->auto_detect_ad_en == 1) {
		ret = yt8521_detect(pGmac);
		if (ret != ETHERNET_PHY_OK) {
			return ETHERNET_PHY_ERR;
		}
	}

	return ETHERNET_PHY_OK;
}

u8 yt8521_reset(FGmacPs_Instance_T *pGmac)
{
	u16 reg;
	u32 phy_timeout = 0;

	reg = yt8521_reg_read(pGmac, PAGE0, YT8521_CTRL);
	SET_BIT(reg, 0x8000);
	yt8521_reg_write(pGmac, PAGE0, YT8521_CTRL, reg);

	/* wait reset done */
	phy_timeout = 0;
	do {
		reg = yt8521_reg_read(pGmac, PAGE0, YT8521_CTRL);
		phy_timeout++;
		if (phy_timeout > YT8521_PHY_TIME_OUT) {
			FMSH_DEBUG("PHY reset timeout \r\n");
			return ETHERNET_PHY_TIMEOUT;
		}
		FMSH_DELAY_MS(1);
	} while ((reg & 0x8000) != 0);
	FMSH_DEBUG("PHY reset ok \r\n");

	/* wait link up */
	phy_timeout = 0;
	do {
		reg = yt8521_reg_read(pGmac, PAGE0, YT8521_STAT);
		phy_timeout++;
		if (phy_timeout > YT8521_PHY_LINK_TIME_OUT) {
			FMSH_DEBUG("PHY YT8521 link timeout \r\n");
			return ETHERNET_PHY_TIMEOUT;
		}
		FMSH_DELAY_MS(1);
	} while ((reg & 0x4) == 0);
	FMSH_DEBUG("PHY link up \r\n");
	return ETHERNET_PHY_OK;
}

#define REG_DEBUG_ADDR_OFFSET 0x1e
#define REG_DEBUG_DATA        0x1f

int ytphy_read_ext(FGmacPs_Instance_T *pGmac, u8 page, u16 regAddr)
{
	int ret;

	ret = yt8521_reg_write(pGmac, page, REG_DEBUG_ADDR_OFFSET, regAddr);
	if (ret < 0) {
		goto err_handle;
	}

	ret = yt8521_reg_read(pGmac, page, REG_DEBUG_DATA);
	if (ret < 0) {
		goto err_handle;
	}

err_handle:
	return ret;
}

int ytphy_write_ext(FGmacPs_Instance_T *pGmac, u8 page, u16 regAddr, u32 regdata)
{
	int ret;

	ret = yt8521_reg_write(pGmac, page, REG_DEBUG_ADDR_OFFSET, regAddr);
	if (ret < 0) {
		goto err_handle;
	}

	ret = yt8521_reg_write(pGmac, page, REG_DEBUG_DATA, regdata);
	if (ret < 0) {
		goto err_handle;
	}

err_handle:
	return ret;
}

#define YT8521_EXTREG_SLEEP_CONTROL1 0x27
#define YT8521_EN_SLEEP_SW_BIT       (1 << 15)

u8 yt8521_cfg(FGmacPs_Instance_T *pGmac)
{
	FGmacPs_PhyConfig_T *pPhyConfig = pGmac->phy_cfg;
	u32 reg;

	reg = yt8521_reg_read(pGmac, PAGE0, YT8521_ANA);
	reg = 0x1de1;
	yt8521_reg_write(pGmac, PAGE0, YT8521_ANA, reg);

	/* auto MDI crossover */
	/* AN and speed cfg */
	if (pPhyConfig->auto_nag_en == 0) {
		/* AN disable */
		reg = yt8521_reg_read(pGmac, PAGE0, YT8521_CTRL);
		RESET_BIT(reg, 1 << 12);
		yt8521_reg_write(pGmac, PAGE0, YT8521_CTRL, reg);
		/* speed */
		reg = yt8521_reg_read(pGmac, PAGE0, YT8521_CTRL);
		RESET_BIT(reg, 1 << 13);
		RESET_BIT(reg, 1 << 6);
		if (pPhyConfig->speed == speed_10) {
			RESET_BIT(reg, 1 << 13);
			RESET_BIT(reg, 1 << 6);
			FMSH_DEBUG("set phy speed 10M \r\n");
		}
		if (pPhyConfig->speed == speed_100) {
			SET_BIT(reg, 1 << 13);
			RESET_BIT(reg, 1 << 6);
			FMSH_DEBUG("set phy speed 100M \r\n");
		}
		if (pPhyConfig->speed == speed_1000) {
			RESET_BIT(reg, 1 << 13);
			SET_BIT(reg, 1 << 6);
			FMSH_DEBUG("set phy speed 1000M \r\n");
		}
		/* loop back */
		FMSH_DEBUG("enable PHY loopback \r\n");
		SET_BIT(reg, 1 << 14);
		FMSH_DEBUG("Write PHY reg0 = 0x%x \r\n", reg);
		yt8521_reg_write(pGmac, PAGE0, YT8521_CTRL, reg);
	} else {
		reg = yt8521_reg_read(pGmac, PAGE0, YT8521_CTRL);
		/* to enable auto-negotiation */
		SET_BIT(reg, 1 << 12);

		yt8521_reg_write(pGmac, PAGE0, YT8521_CTRL, reg);
	}

	/* default use Fiber mode */
	u32 reg_val = 0x8161;

	if (strcmp(pPhyConfig->phy_mode, "utp") == 0) {
		reg_val = 0x8160;
	}
	fmsh_mdio_write(pGmac, 0x1e, 0xa001);
	fmsh_mdio_write(pGmac, 0x1f, reg_val);
	FMSH_INFO("%s<->RGMII\r\n", reg_val == 0x8161 ? "Fiber" : "UTP");

	if (pPhyConfig->phy_delay != 0x0) {
		fmsh_mdio_write(pGmac, 0x1e, 0xa003);
		fmsh_mdio_write(pGmac, 0x1f, pPhyConfig->phy_delay);
		FMSH_INFO("PHY delay:0x%x\r\n", pPhyConfig->phy_delay);
	}

	ytphy_write_ext(pGmac, PAGE0, 0xa00c, 0x6FF8);
	ytphy_write_ext(pGmac, PAGE0, 0xa00d, 0x6FF8);
	ytphy_write_ext(pGmac, PAGE0, 0xa00e, 0x6FF8);

	reg = ytphy_read_ext(pGmac, PAGE0, 0xa00c);
	FMSH_INFO("YT Read:0x%x\r\n", reg);
	reg = ytphy_read_ext(pGmac, PAGE0, 0xa00d);
	FMSH_INFO("YT Read:0x%x\r\n", reg);
	reg = ytphy_read_ext(pGmac, PAGE0, 0xa00e);
	FMSH_INFO("YT Read:0x%x\r\n", reg);

	return ETHERNET_PHY_OK;
}
