/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "fmsh_gmac_hw.h"
#include "fmsh_ps_parameters.h"

/*
 *
 * Enable/Disable DMA Receiver
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   none
 *
 * @note    None.
 *
 * **************************************************************************
 */
void gmac_dma_enable_rcv(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_DmaPortMap_T *pDma;
	u32 reg;

	pDma = (FGmacPs_DmaPortMap_T *)((u32)pGmac->base_address + GMAC_DMA_OFFSET);
	reg = FMSH_IN32_32(pDma->GDMA_OMR);
	if (enable) {
		SET_BIT(reg, GDMA_OMR_SR);
	} else {
		RESET_BIT(reg, GDMA_OMR_SR);
	}
	FMSH_OUT32_32(reg, pDma->GDMA_OMR);
}

/*
 *
 * Enable/Disable DMA Transmitter
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   none
 *
 * @note    None.
 *
 * **************************************************************************
 */
void gmac_dma_enable_tsv(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_DmaPortMap_T *pDma;
	u32 reg;

	pDma = (FGmacPs_DmaPortMap_T *)((u32)pGmac->base_address + GMAC_DMA_OFFSET);

	reg = FMSH_IN32_32(pDma->GDMA_OMR);
	if (enable) {
		SET_BIT(reg, GDMA_OMR_ST);
	} else {
		RESET_BIT(reg, GDMA_OMR_ST);
	}
	FMSH_OUT32_32(reg, pDma->GDMA_OMR);
}

/*
 *
 * Enable/Disable GMAC Receiver
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK;
 *
 * @note
 *
 * **************************************************************************
 */
u8 gmac_enable_rcv(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	if (enable) {
		SET_BIT(reg, GMAC_MCR_RE);
	} else {
		RESET_BIT(reg, GMAC_MCR_RE);
	}
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Enable/Disable GMAC Transmitter
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK;
 *
 * @note
 *
 * **************************************************************************
 */
u8 gmac_enable_tsv(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	if (enable) {
		SET_BIT(reg, GMAC_MCR_TE);
	} else {
		RESET_BIT(reg, GMAC_MCR_TE);
	}
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Dma Tx Poll Demand
 *
 * @param  FGmacPs_Instance_T * pGmac
 *
 * @return   void
 *
 * @note    None.
 *
 * **************************************************************************
 */
void gmac_DmaTxPollDemand(FGmacPs_Instance_T *pGmac)
{
	FGmacPs_DmaPortMap_T *pDma;
	u32 reg = 1;

	pDma = (FGmacPs_DmaPortMap_T *)((u32)pGmac->base_address + GMAC_DMA_OFFSET);
	FMSH_OUT32_32(reg, pDma->GDMA_TPD);
}

/*
 *
 * SLCR reset GMAC0, set path
 *
 * @param  FGmacPs_PathSel path_sel, GMII or RGMII
 *
 * @return   void
 *
 * @note    None.
 *
 * **************************************************************************
 */
void gmac0_bus_rst(FGmacPs_ITF_Type path_sel)
{
	FMSH_WriteReg(FPS_SLCR_BASEADDR, 0x008, 0xDF0D767BU); /* unlock */
	/* selct phy interface */
	u32 reg;

	reg = FMSH_ReadReg(FPS_SLCR_BASEADDR, 0x414);
	RESET_BIT(reg, 7);
	reg |= path_sel; /* gmac 0 */
	FMSH_WriteReg(FPS_SLCR_BASEADDR, 0x414, reg);
	/* reset */
	FMSH_WriteReg(FPS_SLCR_BASEADDR, 0x270, 0x0fU);
	FMSH_WriteReg(FPS_SLCR_BASEADDR, 0x270, 0x00U);
	FMSH_WriteReg(FPS_SLCR_BASEADDR, 0x004, 0xDF0D767BU); /* lock */
}

/*
 *
 * SLCR reset GMAC1, set path
 *
 * @param  FGmacPs_PathSel path_sel, GMII or RGMII
 *
 * @return   void
 *
 * @note    None.
 *
 * **************************************************************************
 */
void gmac1_bus_rst(FGmacPs_ITF_Type path_sel)
{
	FMSH_WriteReg(FPS_SLCR_BASEADDR, 0x008, 0xDF0D767BU); /* unlock */
	/* selct phy interface */
	u32 reg;

	reg = FMSH_ReadReg(FPS_SLCR_BASEADDR, 0x414);
	RESET_BIT(reg, 7 << 4);
	reg |= (path_sel << 4); /* gmac 1 */
	FMSH_WriteReg(FPS_SLCR_BASEADDR, 0x414, reg);
	/* reset */
	FMSH_WriteReg(FPS_SLCR_BASEADDR, 0x270, 0xF << 5);
	FMSH_WriteReg(FPS_SLCR_BASEADDR, 0x270, 0x00U);
	FMSH_WriteReg(FPS_SLCR_BASEADDR, 0x004, 0xDF0D767BU); /* lock */
}

/* ******************* */
/* DMA operation mode */
/* ******************* */
/*
 *
 * setup Tx mode: TSF or TTH
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u32 mode
 *
 * @return   GMAC_RETURN_CODE_OK or GMAC_RETURN_CODE_PARAM_ERR
 *
 * @note    None.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_SetupTxMode(FGmacPs_Instance_T *pGmac, u32 mode)
{
	FGmacPs_DmaPortMap_T *pDma;
	u32 reg_val_32b;

	pDma = (FGmacPs_DmaPortMap_T *)((u32)pGmac->base_address + GMAC_DMA_OFFSET);
	reg_val_32b = FMSH_IN32_32(pDma->GDMA_OMR);

	RESET_BIT(reg_val_32b, GDMA_OMR_TSF | GDMA_OMR_TTC);
	switch (mode) {
	case GDMA_OMR_TSF:
		SET_BIT(reg_val_32b, GDMA_OMR_TSF);
		break;
	case GDMA_OMR_TTC_16:
		SET_BIT(reg_val_32b, GDMA_OMR_TTC_16);
		break;
	case GDMA_OMR_TTC_24:
		SET_BIT(reg_val_32b, GDMA_OMR_TTC_24);
		break;
	case GDMA_OMR_TTC_32:
		SET_BIT(reg_val_32b, GDMA_OMR_TTC_32);
		break;
	case GDMA_OMR_TTC_40:
		SET_BIT(reg_val_32b, GDMA_OMR_TTC_40);
		break;
	case GDMA_OMR_TTC_64:
		SET_BIT(reg_val_32b, GDMA_OMR_TTC_64);
		break;
	case GDMA_OMR_TTC_128:
		SET_BIT(reg_val_32b, GDMA_OMR_TTC_128);
		break;
	case GDMA_OMR_TTC_192:
		SET_BIT(reg_val_32b, GDMA_OMR_TTC_192);
		break;
	case GDMA_OMR_TTC_256:
		SET_BIT(reg_val_32b, GDMA_OMR_TTC_256);
		break;
	default:
		return GMAC_RETURN_CODE_PARAM_ERR;
	}
	FMSH_OUT32_32(reg_val_32b, pDma->GDMA_OMR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * setup Rx mode: RSF or RTH
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u32 mode
 *
 * @return   GMAC_RETURN_CODE_OK or GMAC_RETURN_CODE_PARAM_ERR
 *
 * @note    None.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_SetupRxMode(FGmacPs_Instance_T *pGmac, u32 mode)
{
	FGmacPs_DmaPortMap_T *pDma;
	u32 reg_val_32b;

	pDma = (FGmacPs_DmaPortMap_T *)((u32)pGmac->base_address + GMAC_DMA_OFFSET);
	reg_val_32b = FMSH_IN32_32(pDma->GDMA_OMR);

	RESET_BIT(reg_val_32b, GDMA_OMR_RSF | GDMA_OMR_RTC);
	switch (mode) {
	case GDMA_OMR_RSF:
		SET_BIT(reg_val_32b, GDMA_OMR_RSF);
		break;
	case GDMA_OMR_RTC_64:
		SET_BIT(reg_val_32b, GDMA_OMR_RTC_64);
		break;
	case GDMA_OMR_RTC_32:
		SET_BIT(reg_val_32b, GDMA_OMR_RTC_32);
		break;
	case GDMA_OMR_RTC_96:
		SET_BIT(reg_val_32b, GDMA_OMR_RTC_96);
		break;
	case GDMA_OMR_RTC_128:
		SET_BIT(reg_val_32b, GDMA_OMR_RTC_128);
		break;
	default:
		return GMAC_RETURN_CODE_PARAM_ERR;
	}
	FMSH_OUT32_32(reg_val_32b, pDma->GDMA_OMR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Enable/Disable DMA forward error frame
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note    None.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_EnFwErrFrame(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_DmaPortMap_T *pDma;
	u32 reg;

	pDma = (FGmacPs_DmaPortMap_T *)((u32)(pGmac->base_address) + GMAC_DMA_OFFSET);
	reg = FMSH_IN32_32(pDma->GDMA_OMR);

	if (enable) {
		SET_BIT(reg, GDMA_OMR_FEF);
	} else {
		RESET_BIT(reg, GDMA_OMR_FEF);
	}
	FMSH_OUT32_32(reg, pDma->GDMA_OMR);
	return GMAC_RETURN_CODE_OK;
}
/*
 *
 * Enable/Disable DMA forward error frame
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note    None.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_EnRxFwUnderSizeGoodFrame(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_DmaPortMap_T *pDma;
	u32 reg_val_32b;

	pDma = (FGmacPs_DmaPortMap_T *)((u32)pGmac->base_address + GMAC_DMA_OFFSET);
	reg_val_32b = FMSH_IN32_32(pDma->GDMA_OMR);
	if (enable) {
		SET_BIT(reg_val_32b, GDMA_OMR_FUF);
	} else {
		RESET_BIT(reg_val_32b, GDMA_OMR_FUF);
	}
	FMSH_OUT32_32(reg_val_32b, pDma->GDMA_OMR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Enable/Disable Tx OSF mode
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note    None.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_EnTxOsf(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_DmaPortMap_T *pDma;
	u32 reg_val_32b;

	pDma = (FGmacPs_DmaPortMap_T *)((u32)pGmac->base_address + GMAC_DMA_OFFSET);
	reg_val_32b = FMSH_IN32_32(pDma->GDMA_OMR);
	if (enable) {
		SET_BIT(reg_val_32b, GDMA_OMR_OSF);
	} else {
		RESET_BIT(reg_val_32b, GDMA_OMR_OSF);
	}
	FMSH_OUT32_32(reg_val_32b, pDma->GDMA_OMR);
	return GMAC_RETURN_CODE_OK;
}
/*
 *
 * setup interrupt
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  FGmacPs_DmaIrq_T mask
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note    None.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_SetupIntr(FGmacPs_Instance_T *pGmac, FGmacPs_DmaIrq_T mask)
{
	FGmacPs_DmaPortMap_T *pDmaPortMap;

	pDmaPortMap = (FGmacPs_DmaPortMap_T *)((u32)pGmac->base_address + GMAC_DMA_OFFSET);
	FMSH_OUT32_32(mask, pDmaPortMap->GDMA_IER);
	return GMAC_RETURN_CODE_OK;
}

/* *********** */
/* MAC config */
/* *********** */
/*
 *
 * config Preamble Length for Transmit frames
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u32 value (0 1 2)
 *
 * @return   GMAC_RETURN_CODE_OK or GMAC_RETURN_CODE_PARAM_ERR
 *
 * @note    None.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_TxPreamLeng(FGmacPs_Instance_T *pGmac, u32 value)
{
	if (value > 2) {
		return GMAC_RETURN_CODE_PARAM_ERR;
	}
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	RESET_BIT(reg, GMAC_MCR_PRELEN);
	SET_BIT(reg, value);
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * config inter frame gap
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u32 gap value 0-7
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note    None.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_InterFrameGap(FGmacPs_Instance_T *pGmac, u32 gap)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	RESET_BIT(reg, GMAC_MCR_IFG);
	SET_BIT(reg, gap);
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Enable/Disable 2K Frame
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note    None.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_Enable2k(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	if (enable) {
		SET_BIT(reg, GMAC_MCR_TWOKPE);
	} else {
		RESET_BIT(reg, GMAC_MCR_TWOKPE);
	}
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Enable/Disable CRC Stripping for type frame
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK;
 *
 * @note    None.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_TypeCrcStrip(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	if (enable) {
		SET_BIT(reg, GMAC_MCR_CST);
	} else {
		RESET_BIT(reg, GMAC_MCR_CST);
	}
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * setup RxWDG
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 wd_en : wdt enable or not
 * @param  u8 prog_en : Programmable Watchdog Enable
 * @param  u16 timeout : timeout value
 *
 * @return   GMAC_RETURN_CODE_OK or GMAC_RETURN_CODE_PARAM_ERR
 *
 * @note    When Bit 16 (PWE) is set, the value in this field should be more than 1,522
 * (0x05F2). Otherwise, the IEEE Std 802.3-specified valid tagged frames are
 * declared as error frames and are dropped..
 *
 * **************************************************************************
 */
u8 FGmac_Ps_SetupRxWatchDog(FGmacPs_Instance_T *pGmac, u8 wd_en, u8 prog_en, u16 timeout)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg = 0;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	if (wd_en) {
		RESET_BIT(reg, GMAC_MCR_WD);
	} else {
		SET_BIT(reg, GMAC_MCR_WD);
	}
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);

	reg = 0;
	if ((timeout <= 0x5F) || (timeout > 0x3FFF)) {
		return GMAC_RETURN_CODE_PARAM_ERR;
	}
	if (prog_en) {
		SET_BIT(reg, GMAC_WDT_PWE | timeout);
	}
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_WDT);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Enable/Disable Tx Jabber
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_EnTxJabber(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	if (!enable) {
		SET_BIT(reg_val_32b, GMAC_MCR_JD);
	} else {
		RESET_BIT(reg_val_32b, GMAC_MCR_JD);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Enable/Disable Jumbo Frame
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_EnableJumbo(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	if (enable) {
		SET_BIT(reg, GMAC_MCR_JE);
	} else {
		RESET_BIT(reg, GMAC_MCR_JE);
	}
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Select ethernet link speed
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 speed
 *
 * @return   GMAC_RETURN_CODE_OK;
 *
 * @note
 * speed[PS,FES]:
 * 0x - 1000Mbps operations
 * 10 - 10Mbps operations
 * 11 - 100Mbps operations
 * speed:2'b10 for 1000;2'b01 for 100;2'b00 for 10
 *
 * **************************************************************************
 */
u8 FGmac_Ps_SetupSpeed(FGmacPs_Instance_T *pGmac, u8 speed)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	RESET_BIT(reg, GMAC_MCR_PS | GMAC_MCR_FES);
	/* [PS,FES]= speed + 2'b10 = speed + 2 */
	reg |= ((speed + 2) & 0x3) << 14;
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Enable/Disable GMAC Loopback mode
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK;
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_LoopbackMode(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	if (enable) {
		SET_BIT(reg, GMAC_MCR_LM);
	} else {
		RESET_BIT(reg, GMAC_MCR_LM);
	}
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Enable/Disable GMAC CRC & Pad Stripping
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK;
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_PadCrcStrip(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	reg = FMSH_IN32_32(pGmacPortMap->GMAC_MCR);
	if (enable) {
		SET_BIT(reg, GMAC_MCR_ACS);
	} else {
		RESET_BIT(reg, GMAC_MCR_ACS);
	}
	FMSH_OUT32_32(reg, pGmacPortMap->GMAC_MCR);
	return GMAC_RETURN_CODE_OK;
}

/* ******* */
/* filter */
/* ******* */

/*
 *
 * Rx Filter config : Enable/Disable receive all
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note      The result of the SA or DA filtering is updated (pass or fail) in
 * the corresponding bits in the Receive Status Word.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_RxFilt_EnRcvAll(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_MFF);
	if (enable) {
		SET_BIT(reg_val_32b, GMAC_MFF_RA);
	} else {
		RESET_BIT(reg_val_32b, GMAC_MFF_RA);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_MFF);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Rx Filter config : Enable/Disable Pass All Multicast
 *
 * @param  FGmacPs_Instance_T * pGmac
 * @param  u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_RxFilt_EnFwAllMultiCast(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_MFF);
	if (enable) {
		SET_BIT(reg_val_32b, GMAC_MFF_PM);
	} else {
		RESET_BIT(reg_val_32b, GMAC_MFF_PM);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_MFF);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Rx Filter config : Enable/Disable Pass Broadcast Frames
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_RxFilt_EnFwBroadCast(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_MFF);
	if (enable) {
		RESET_BIT(reg_val_32b, GMAC_MFF_DBF);
	} else {
		SET_BIT(reg_val_32b, GMAC_MFF_DBF);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_MFF);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Rx Filter config : Enable/Disable DA Inverse Filtering
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_RxFilt_EnDaInvF(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_MFF);
	if (enable) {
		SET_BIT(reg_val_32b, GMAC_MFF_DAIF);
	} else {
		RESET_BIT(reg_val_32b, GMAC_MFF_DAIF);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_MFF);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Rx Filter config : Enable/Disable SA Filtering
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_RxFilt_EnSaF(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_MFF);
	if (enable) {
		SET_BIT(reg_val_32b, GMAC_MFF_SAF);
	} else {
		RESET_BIT(reg_val_32b, GMAC_MFF_SAF);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_MFF);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Rx Filter config : Enable/Disable SA Inverse Filtering
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_RxFilt_EnSaInvF(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_MFF);
	if (enable) {
		SET_BIT(reg_val_32b, GMAC_MFF_SAIF);
	} else {
		RESET_BIT(reg_val_32b, GMAC_MFF_SAIF);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_MFF);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Rx Filter config : config ctrl frame filter
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 value : 0 1 2 3
 *
 * @return   GMAC_RETURN_CODE_OK or GMAC_RETURN_CODE_PARAM_ERR
 *
 * @note
 * 00: MAC filters all control frames from reaching the application.
 * 01: MAC forwards all control frames except Pause frames to application even if
 * they fail the Address filter.
 * 10: MAC forwards all control frames to application even if they fail the Address
 * Filter.
 * 11: MAC forwards control frames that pass the Address Filter
 *
 * **************************************************************************
 */
u8 FGmac_Ps_RxFilt_CtrlFrame(FGmacPs_Instance_T *pGmac, u8 value)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_MFF);
	RESET_BIT(reg_val_32b, GMAC_MFF_PCF_11); /* clear */
	switch (value) {
	case 0:
		SET_BIT(reg_val_32b, GMAC_MFF_PCF_00);
		break;
	case 1:
		SET_BIT(reg_val_32b, GMAC_MFF_PCF_01);
		break;
	case 2:
		SET_BIT(reg_val_32b, GMAC_MFF_PCF_10);
		break;
	case 3:
		SET_BIT(reg_val_32b, GMAC_MFF_PCF_11);
		break;
	default:
		return GMAC_RETURN_CODE_PARAM_ERR;
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_MFF);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Rx Filter config : Enable/Disable Promiscuous Mode
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 * The SA or DA Filter Fails status
 * bits of the Receive Status Word are always cleared when PR is set.
 *
 * **************************************************************************
 */
u8 FGmac_Ps_RxFilt_EnPmsMode(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_MFF);
	if (enable) {
		SET_BIT(reg_val_32b, GMAC_MFF_PR);
	} else {
		RESET_BIT(reg_val_32b, GMAC_MFF_PR);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_MFF);
	return GMAC_RETURN_CODE_OK;
}

/* ************* */
/* flow control */
/* ************* */

/*
 *
 * Flow Control : Enable/Disable Hardware FC
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note     not support now, RxFIFO < 4KB
 *
 * **************************************************************************
 */
u8 FGmac_Ps_FlCtrl_EnHwFlc(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_DmaPortMap_T *pDma;
	u32 reg;

	pDma = (FGmacPs_DmaPortMap_T *)((u32)(pGmac->base_address) + GMAC_DMA_OFFSET);
	reg = FMSH_IN32_32(pDma->GDMA_OMR);
	if (enable) {
		SET_BIT(reg, GDMA_OMR_EFC);
	} else {
		RESET_BIT(reg, GDMA_OMR_EFC);
	}
	FMSH_OUT32_32(reg, pDma->GDMA_OMR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Flow Control : Enable/Disable Rx FC
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_FlCtrl_EnRx(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	do {
		reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_FCR);
	} while ((reg_val_32b & GMAC_FCR_FCB) != 0);
	if (enable) {
		SET_BIT(reg_val_32b, GMAC_FCR_RFE);
	} else {
		RESET_BIT(reg_val_32b, GMAC_FCR_RFE);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_FCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Flow Control : Enable/Disable Tx FC
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_FlCtrl_EnTx(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	do {
		reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_FCR);
	} while ((reg_val_32b & GMAC_FCR_FCB) != 0);
	if (enable) {
		SET_BIT(reg_val_32b, GMAC_FCR_TFE);
	} else {
		RESET_BIT(reg_val_32b, GMAC_FCR_TFE);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_FCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Flow Control : Enable/Disable Unicast Pause Frame Detect
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_FlCtrl_EnUniPauseFraDetect(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	do {
		reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_FCR);
	} while ((reg_val_32b & GMAC_FCR_FCB) != 0);
	if (enable) {
		SET_BIT(reg_val_32b, GMAC_FCR_UP);
	} else {
		RESET_BIT(reg_val_32b, GMAC_FCR_UP);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_FCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Flow Control : Enable/Disable Zero-Quanta Pause
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 enable
 *
 * @return   GMAC_RETURN_CODE_OK
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_FlCtrl_EnZeroQuatPause(FGmacPs_Instance_T *pGmac, u8 enable)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	do {
		reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_FCR);
	} while ((reg_val_32b & GMAC_FCR_FCB) != 0);
	if (enable) {
		RESET_BIT(reg_val_32b, GMAC_FCR_DZQP);
	} else {
		SET_BIT(reg_val_32b, GMAC_FCR_DZQP);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_FCR);
	return GMAC_RETURN_CODE_OK;
}

/*
 *
 * Flow Control : setup
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 tx_flc_en
 * @param    u8 rx_flc_en
 * @param    u8 upfd_en
 * @param    u8 pause_low_th
 * @param    u8 zq_pause_en
 * @param    u16 pause_time
 * @param    u8 fcbba
 *
 * @return   GMAC_RETURN_CODE_OK or GMAC_RETURN_CODE_PARAM_ERR
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_FlwCtrlSetup(FGmacPs_Instance_T *pGmac, u8 tx_flc_en, u8 rx_flc_en, u8 upfd_en,
			 u8 pause_low_th, u8 zq_pause_en, u16 pause_time, u8 fcbba)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg_val_32b;

	do {
		reg_val_32b = FMSH_IN32_32(pGmacPortMap->GMAC_FCR);
	} while ((reg_val_32b & GMAC_FCR_FCB) != 0);

	if (tx_flc_en == 1) {
		SET_BIT(reg_val_32b, GMAC_FCR_TFE);
	} else {
		RESET_BIT(reg_val_32b, GMAC_FCR_TFE);
	}
	if (rx_flc_en == 1) {
		SET_BIT(reg_val_32b, GMAC_FCR_RFE);
	} else {
		RESET_BIT(reg_val_32b, GMAC_FCR_RFE);
	}
	if (upfd_en == 1) {
		SET_BIT(reg_val_32b, GMAC_FCR_UP);
	} else {
		RESET_BIT(reg_val_32b, GMAC_FCR_UP);
	}
	RESET_BIT(reg_val_32b, GMAC_FCR_PLT_256);
	switch (pause_low_th) {
	case 0:
		SET_BIT(reg_val_32b, GMAC_FCR_PLT_4);
		break;
	case 1:
		SET_BIT(reg_val_32b, GMAC_FCR_PLT_28);
		break;
	case 2:
		SET_BIT(reg_val_32b, GMAC_FCR_PLT_144);
		break;
	case 3:
		SET_BIT(reg_val_32b, GMAC_FCR_PLT_256);
		break;
	default:
		return GMAC_RETURN_CODE_PARAM_ERR;
	}
	if (zq_pause_en == 1) {
		RESET_BIT(reg_val_32b, GMAC_FCR_DZQP);
	} else {
		SET_BIT(reg_val_32b, GMAC_FCR_DZQP);
	}
	RESET_BIT(reg_val_32b, GMAC_FCR_PT);
	reg_val_32b |= pause_time << 16;
	if (fcbba == 1) {
		SET_BIT(reg_val_32b, GMAC_FCR_FCB);
	} else {
		RESET_BIT(reg_val_32b, GMAC_FCR_FCB);
	}
	FMSH_OUT32_32(reg_val_32b, pGmacPortMap->GMAC_FCR);
	return GMAC_RETURN_CODE_OK;
}

/* ******** */
/* address */
/* ******** */

/*
 *
 * setup address reg
 *
 * @param    FGmacPs_Instance_T * pGmac
 * @param    u8 Index
 * @param    u8 *pMacAddr
 * @param    u8 En
 * @param    u8 SA
 * @param    u8 mask
 *
 * @return   GMAC_RETURN_CODE_OK;
 *
 * @note
 *
 * **************************************************************************
 */
u8 FGmac_Ps_SetupMacAddr(FGmacPs_Instance_T *pGmac, u8 Index, u8 *pMacAddr, u8 En, u8 SA, u8 mask)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	/* clear */
	pGmacPortMap->GMAC_MAR[Index].GMAC_MAL = 0;
	pGmacPortMap->GMAC_MAR[Index].GMAC_MAH = 0;
	/* set */
	pGmacPortMap->GMAC_MAR[Index].GMAC_MAL |=
		(pMacAddr[3] << 24 | pMacAddr[2] << 16 | pMacAddr[1] << 8 | pMacAddr[0]);
	pGmacPortMap->GMAC_MAR[Index].GMAC_MAH |=
		(pMacAddr[5] << 8 | pMacAddr[4] | En << 31 | SA << 30 | mask << 24);
	return GMAC_RETURN_CODE_OK;
}
