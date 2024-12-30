/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "fmsh_mdio.h"

#define LOG_LEVEL CONFIG_ETHERNET_LOG_LEVEL

LOG_MODULE_REGISTER(fmsh_mdio);

extern u32 g_GmacLinkStatus;

u8 fmsh_mdio_idle(FGmacPs_Instance_T *pGmac)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;

	u32 reg;
	u32 phy_timeout;

	phy_timeout = 0;
	do {
		reg = FMSH_IN32_32(pGmacPortMap->GMAC_GAR);
		phy_timeout++;
		if (phy_timeout > FMSH_ENET_PHY_TIMEOUT) {
			FMSH_DEBUG("GMII busy timeout \r\n");
			return ETHERNET_PHY_TIMEOUT;
		}
		FMSH_DELAY_MS(1);
	} while ((reg & GMAC_GAR_GB) != 0); /* wait for GMII not busy */
	return ETHERNET_PHY_OK;
}

u8 fmsh_mdio_device_detect(FGmacPs_Instance_T *pGmac)
{
	FGmacPs_PhyConfig_T *pPhyConfig = pGmac->phy_cfg;
	u32 reg;
	u8 index = 0;

	for (index = 0; index < 32; index++) {
		pPhyConfig->mdio_address = index;
		reg = fmsh_mdio_read(pGmac, ENET_PHY_ID1);
		if ((reg != 0xffff) && (reg != 0)) {
			return ETHERNET_PHY_OK;
		}
	}
	pPhyConfig->mdio_address = 32;
	return ETHERNET_PHY_ERR;
}

u8 fmsh_mdio_init(FGmacPs_Instance_T *pGmac)
{
	FGmacPs_PhyConfig_T *pPhyConfig = pGmac->phy_cfg;
	u32 reg = 0;
	u32 phy_timeout;

	if (pPhyConfig->auto_detect_ad_en == 1) {
		if (ETHERNET_PHY_ERR == fmsh_mdio_device_detect(pGmac)) {
			FMSH_DEBUG("PHY detect fail \r\n");
		} else {
			FMSH_DEBUG("PHY detected, address 0x%x \r\n", pPhyConfig->mdio_address);
		}
	} else {
		FMSH_DEBUG("PHY address 0x%x \r\n", pPhyConfig->mdio_address);
	}

	/* read ID */
	phy_timeout = 0;
	do {
		reg = fmsh_mdio_read(pGmac, ENET_PHY_ID1);
		/* reg=mic_phy_read(pGmac,2); */
		phy_timeout++;
		if (phy_timeout > FMSH_ENET_PHY_TIMEOUT) {
			FMSH_DEBUG("PHY ID read timeout \r\n");
			return ETHERNET_PHY_TIMEOUT;
		}
		FMSH_DELAY_MS(1);
	} while ((reg == 0) || (reg == 0xffff));
	FMSH_DEBUG("PHY ID get:%x \r\n", reg);
	return ETHERNET_PHY_OK;
}

u8 fmsh_mdio_reset(FGmacPs_Instance_T *pGmac)
{
	u16 reg;
	u32 phy_timeout = 0;

	reg = fmsh_mdio_read(pGmac, ENET_PHY_CTRL);
	SET_BIT(reg, ENET_PHY_CTRL_RST);
	fmsh_mdio_write(pGmac, ENET_PHY_CTRL, reg);
	/* wait reset done */
	phy_timeout = 0;
	do {
		reg = fmsh_mdio_read(pGmac, ENET_PHY_CTRL);
		phy_timeout++;
		if (phy_timeout > FMSH_ENET_PHY_TIMEOUT) {
			FMSH_DEBUG("PHY reset timeout \r\n");
			return ETHERNET_PHY_TIMEOUT;
		}
		FMSH_DELAY_MS(1);
	} while ((reg & ENET_PHY_CTRL_RST) != 0);
	FMSH_DEBUG("PHY reset ok \r\n");

	/* wait link up */
	phy_timeout = 0;
	do {
		reg = fmsh_mdio_read(pGmac, ENET_PHY_STS);
		phy_timeout++;
		if (phy_timeout > FMSH_ENET_PHY_TIMEOUT) {
			FMSH_DEBUG("PHY link timeout \r\n");
			return ETHERNET_PHY_TIMEOUT;
		}
		FMSH_DELAY_MS(1);
	} while ((reg & ENET_PHY_STS_LNK_STAT) == 0);

	g_GmacLinkStatus = 1; /* link up */

	FMSH_DEBUG("PHY link up \r\n");
	return ETHERNET_PHY_OK;
}

u32 fmsh_mdio_write(FGmacPs_Instance_T *pGmac, u8 regAddr, u32 regdata)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	FGmacPs_PhyConfig_T *pPhyConfig = pGmac->phy_cfg;
	u16 reg;

	if ((regdata & 0xFFFF00000) != 0) {
		return ETHERNET_PHY_PARAM_ERR;
	}

	/* wait idle */
	if (fmsh_mdio_idle(pGmac) != ETHERNET_PHY_OK) {
		return ETHERNET_PHY_ERR;
	}
	/* write data */
	reg = (pPhyConfig->mdio_address << 11) | (pGmac->csr_clk << 2);
	reg |= (regAddr << 6);
	reg |= (GMAC_GAR_GB | GMAC_GAR_GW);
	FMSH_OUT32_32(regdata, pGmacPortMap->GMAC_GDR);
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_GAR);
	/* wait idle */
	if (fmsh_mdio_idle(pGmac) != ETHERNET_PHY_OK) {
		return ETHERNET_PHY_ERR;
	}

	return ETHERNET_PHY_OK;
}

/*
 * @brief: Read phy reg data through MDIO
 * @para: FGmacPs_Instance_T *pGmac
 * @para: u8 regAddr : reg index of phy reg
 * @return: u32 data
 */
u32 fmsh_mdio_read(FGmacPs_Instance_T *pGmac, u8 regAddr)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	FGmacPs_PhyConfig_T *pPhyConfig = pGmac->phy_cfg;
	u32 reg = 0;
	u32 datareg;

	/* wait idle */
	if (fmsh_mdio_idle(pGmac) != ETHERNET_PHY_OK) {
		return ETHERNET_PHY_ERR;
	}
	/* read data */
	reg = (pPhyConfig->mdio_address << 11) | (pGmac->csr_clk << 2);
	reg |= (regAddr << 6);
	reg |= GMAC_GAR_GB;
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_GAR);
	/* wait idle */
	if (fmsh_mdio_idle(pGmac) != ETHERNET_PHY_OK) {
		return ETHERNET_PHY_ERR;
	}

	datareg = FMSH_IN32_32(pGmacPortMap->GMAC_GDR);
	return datareg;
}

void fmsh_mdio_reg_dump(FGmacPs_Instance_T *pGmac)
{
	u8 i = 0;

	u16 reg;
	/* fmsh_mdio_init(pGmac); */
	FMSH_DEBUG("phy reg:\r\n");
	for (i = 0; i < 32; i++) {
		reg = fmsh_mdio_read(pGmac, i);
		FMSH_DEBUG("reg%d : %x\r\n", i, reg);
	}
}
