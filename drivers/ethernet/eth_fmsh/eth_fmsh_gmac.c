/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/cache.h>
#include <zephyr/irq.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/logging/log.h>

#include "eth_fmsh_gmac_priv.h"
#include "eth_fmsh_gmac.h"

#define DT_DRV_COMPAT snps_fmsh_gmac

#define LOG_LEVEL CONFIG_ETHERNET_LOG_LEVEL

LOG_MODULE_REGISTER(eth_fmsh);

/* 驱动私有数据 */
FGmacPs_LinkStatus_T s_GMAC_LinkStatus;

/* DMA缓冲区池 */
NET_BUF_POOL_DEFINE(rx_dma_pool, 128, GMAC_RBUFFER_UNIT_SIZE, sizeof(uint32_t), NULL);

FGmacPs_Config_T s_GMAC_Config = {
	.DeviceId = FPAR_GMACPS_0_DEVICE_ID,
	.BaseAddress = FPAR_GMACPS_0_BASEADDR,
	.Speed = FPAR_GMACPS_0_SPEED,
	.InterFaceType = FPAR_GMACPS_0_INTERFACE,
};

FGmacPs_PhyConfig_T s_GMAC_PhyCfg = {
	.phy_device = PHY_YT8521,
	.speed = FPAR_GMACPS_0_SPEED,
	.auto_detect_ad_en = 1,
	.mdio_address = DT_INST_PROP(0, mdio_addr),
	.gmii2rgmii_mdio_addr1 = DT_INST_PROP(0, gmii2rgmii_addr1),
	.gmii2rgmii_mdio_addr2 = DT_INST_PROP(0, gmii2rgmii_addr2),
	.phy_mode = DT_INST_PROP(0, phy_mode),
	.phy_delay = DT_INST_PROP(0, phy_delay),
	.auto_nag_en = 1,
	.interface = FPAR_GMACPS_0_INTERFACE,
};

FGmacPs_Instance_T s_GMAC_Instance = {
	.index = 0,
	.base_address = (void *)DT_INST_REG_ADDR(0),
	.mac_address = DT_INST_PROP(0, local_mac_address),
	.csr_clk = 5,
	.wRxListSize = GMAC_RDES_NUM,
	.wTxListSize = GMAC_TDES_NUM,
	.RxDesBufSize = GMAC_RBUFFER_UNIT_SIZE,
	.TxDesBufSize = GMAC_TBUFFER_UNIT_SIZE,
	.pFrmBuffer = NULL,
	.FrmBufferSize = GMAC_PACKET_BUFFER_SIZE,
	.wTxHead = 0,
	.wTxTail = 0,
	.phy_cfg = &s_GMAC_PhyCfg,
	.gmac_link_status = &s_GMAC_LinkStatus,
	.gmac_cfg = &s_GMAC_Config,
};

FGmacPs_RxDescriptor_T __attribute__((section(".ocm_data")))
__aligned(CONFIG_DCACHE_LINE_SIZE) GMAC0_RxDs[GMAC_RDES_NUM];
FGmacPs_TxDescriptor_T __attribute__((section(".ocm_data")))
__aligned(CONFIG_DCACHE_LINE_SIZE) GMAC0_TxDs[GMAC_TDES_NUM];
uint8_t __aligned(CONFIG_DCACHE_LINE_SIZE) dw_tx[2 * GMAC_TDES_NUM * GMAC_RBUFFER_UNIT_SIZE];
uint8_t __aligned(CONFIG_DCACHE_LINE_SIZE) dw_rx[2 * GMAC_RDES_NUM * GMAC_RBUFFER_UNIT_SIZE];
uint8_t __aligned(CONFIG_DCACHE_LINE_SIZE) pack_buf[GMAC_PACKET_BUFFER_SIZE];

/* 数据发送接口 */
#define USE_GMAC_LIB_SEND 0
#if (USE_GMAC_LIB_SEND == 1)
static int eth_fmsh_send(const struct device *dev, struct net_pkt *pkt)
{
	struct eth_fmsh_data *ctx = dev->data;
	int ret;

	/* 获取发送锁 */
	k_mutex_lock(&ctx->tx_mutex, K_FOREVER);
	k_sem_reset(&ctx->tx_sem);

	u32 size = net_pkt_get_len(pkt);

	ret = net_pkt_read(pkt, pack_buf, size);

	if (ret < 0) {
		FMSH_ERROR("net_pkt_read error");
		return ret;
	}
	FGmac_Ps_Send(ctx->gmac_inst, (u8 *)pack_buf, size, 0, 0);
	FMSH_HEXDUMP("sendbuf", pack_buf, size);

	/* 等待发送完成信号量 */
	k_sem_take(&ctx->tx_sem, K_FOREVER);

	k_mutex_unlock(&ctx->tx_mutex);

	FMSH_DEBUG("TX end");
	return 0;
}
#elif (USE_GMAC_LIB_SEND == 0)
static int eth_fmsh_send(const struct device *dev, struct net_pkt *pkt)
{
	struct eth_fmsh_data *ctx = dev->data;
	int ret;

	/* 获取发送锁 */
	k_mutex_lock(&ctx->tx_mutex, K_FOREVER);
	k_sem_reset(&ctx->tx_sem);

	/* 获取当前发送描述符 */
	FGmacPs_TxDescriptor_T *pTxDES_tmp; /* tmp pDES */
	u16 TxDES_Idx;                      /* tmp Idx of TxDES */
	u32 TDes_num = ctx->gmac_inst->wTxListSize;
	u32 TxDesBufSize = ctx->gmac_inst->TxDesBufSize;

	/* initial */
	TxDES_Idx = ctx->gmac_inst->wTxHead;
	pTxDES_tmp = &ctx->gmac_inst->pTxD[0];
	u32 size = net_pkt_get_len(pkt);

	/* 清空描述符状态 */
	pTxDES_tmp[TxDES_Idx].TDES0.val = 0;
	pTxDES_tmp[TxDES_Idx].TDES1.val = 0;

	uint32_t *buf_addr = (uint32_t *)(pTxDES_tmp[TxDES_Idx].BufferAdd1);

	/* 拷贝数据到DMA缓冲区 */
	ret = net_pkt_read(pkt, buf_addr, size);
	if (ret < 0) {
		FMSH_ERROR("net_pkt_read error");
		return ret;
	}

	/* 配置描述符 */
	if (TxDES_Idx >= (TDes_num - 1)) {
		pTxDES_tmp[TxDES_Idx].TDES1.val |= GMAC_TDES1_TER;
	}

	/* 设置缓冲区大小 */
	if (size <= TxDesBufSize) {
		pTxDES_tmp[TxDES_Idx].TDES1.val |= size;
	} else {
		pTxDES_tmp[TxDES_Idx].TDES1.val |= TxDesBufSize;
		pTxDES_tmp[TxDES_Idx].TDES1.val |= (size - TxDesBufSize) << 11;
	}

#if FMSH_CACHE_ENABLE
	u32 range_start, range_end;

	range_start = pTxDES_tmp[TxDES_Idx].BufferAdd1 & 0xffffffc0;
	range_end = ((pTxDES_tmp[TxDES_Idx].BufferAdd1 + TxDesBufSize * 2) & 0xffffffc0) + (1 << 6);
	FMSH_DCACHE_FLUSH((void *)range_start, range_end - range_start);
	/* flush_dcache_range(range_start, range_end); */
	FMSH_DEBUG("flush_dcache_range: %x, %x\n", range_start, range_end);
#endif
	/* 设置发送控制位 */
	pTxDES_tmp[TxDES_Idx].TDES1.val |= GMAC_TDES1_LS | GMAC_TDES1_IC;
	pTxDES_tmp[TxDES_Idx].TDES1.val |= GMAC_TDES1_FS;
	pTxDES_tmp[TxDES_Idx].TDES0.val |= GMAC_TDES0_OWN;

	/* 更新发送索引 */
	TxDES_Idx++;
	if (TxDES_Idx >= TDes_num) {
		TxDES_Idx = 0;
	}

	ctx->gmac_inst->wTxHead = TxDES_Idx;

	/* 触发发送 */
	gmac_DmaTxPollDemand(ctx->gmac_inst);

	/* 等待发送完成信号量 */
	k_sem_take(&ctx->tx_sem, K_FOREVER);

	/* 更新统计 */
	ctx->stats.bytes.sent += size;
	ctx->stats.pkts.tx++;

	k_mutex_unlock(&ctx->tx_mutex);

	return 0;
}
#endif

static void eth_fmsh_phy_update(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	struct eth_fmsh_data *ctx = dev->data;

	while (1) {
		k_sleep(K_MSEC(1000));
		FGmacPs_GmacLink_Updata(ctx->gmac_inst);
		FGmacPS_Gmii2rgmii_Update_Speed(ctx->gmac_inst,
						ctx->gmac_inst->phy_cfg->gmii2rgmii_mdio_addr1);
		FGmacPS_Gmii2rgmii_Update_Speed(ctx->gmac_inst,
						ctx->gmac_inst->phy_cfg->gmii2rgmii_mdio_addr2);
	}
}

/* 数据接收线程 */
static void eth_fmsh_rx_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	struct eth_fmsh_data *data = dev->data;
	int budget;

	uint32_t rx_len;
	uint8_t *rcv_packet;
	struct net_pkt *pkt;

	while (1) {
		/* 等待中断触发 */
		k_sem_take(&data->rx_sem, K_FOREVER);

		/* 进入轮询模式 */
		budget = data->napi_budget;
		while (budget > 0) {
			rcv_packet = (uint8_t *)FGmac_Ps_RcvPollEFrame(data->gmac_inst, &rx_len);
			if (rcv_packet == NULL) {
				break;
			}

			/* 分配不带buffer的net_pkt */
			pkt = net_pkt_rx_alloc(K_NO_WAIT);
			if (pkt == NULL) {
				data->stats.error_details.rx_buf_alloc_failed++;
				continue;
			}
			struct net_buf *buf = net_buf_alloc_len(&rx_dma_pool, rx_len, K_NO_WAIT);

			if (buf == NULL) {
				net_pkt_unref(pkt);
				continue;
			}

			/* 设置buffer数据 */
			buf->data = rcv_packet;
			buf->len = rx_len;
			net_pkt_append_buffer(pkt, buf);

			if (net_recv_data(data->iface, pkt) < 0) {
				data->stats.error_details.rx_frame_errors++;
				net_pkt_unref(pkt);
			}

			budget--;
		}
		/* 更新统计 */
		data->stats.bytes.received += rx_len;
		data->stats.pkts.rx++;
		/* 处理完成，退出轮询模式 */
		atomic_clear_bit(&data->rx_busy, 0);
		FGmac_Ps_SetupIntr(data->gmac_inst, gdma_irq_tx | gdma_irq_rx | gdma_irq_nie);
	}
}

/* 中断处理函数 */
static void eth_fmsh_isr(const struct device *dev)
{
	struct eth_fmsh_data *data = dev->data;
	FGmacPs_DmaPortMap_T *pDma;
	uint32_t reg_val_32b;
	FGmacPs_DmaIrq_T clearIrqMask = gdma_irq_none;
	FMSH_callback userCallback;
	u32 callbackArg;

	userCallback = NULL;
	callbackArg = 0;

	/* 获取DMA寄存器基地址 */
	pDma = (FGmacPs_DmaPortMap_T *)((uint32_t)data->gmac_inst->base_address + GMAC_DMA_OFFSET);

	/* 读取中断状态 */
	reg_val_32b = FMSH_IN32_32(pDma->GDMA_SR);

	/* 异常中断处理 */
	if (reg_val_32b & gdma_irq_aie) {
		userCallback = data->gmac_inst->listener;
		if (reg_val_32b & gdma_irq_rx_wd_timeout) {
			FMSH_ERROR("Receive watchdog timeout");
			data->stats.error_details.rx_missed_errors++;
			clearIrqMask = gdma_irq_rx_wd_timeout;
			callbackArg = gdma_irq_rx_wd_timeout;
		} else if (reg_val_32b & gdma_irq_tx_jabber_timeout) {
			FMSH_ERROR("Transmit jabber timeout");
			data->stats.error_details.tx_aborted_errors++;
			clearIrqMask = gdma_irq_tx_jabber_timeout;
			callbackArg = gdma_irq_tx_jabber_timeout;
		} else if (reg_val_32b & gdma_irq_rx_overflow) {
			FMSH_ERROR("Receive FIFO overflow");
			data->stats.error_details.rx_over_errors++;
			clearIrqMask = gdma_irq_rx_overflow;
			callbackArg = gdma_irq_rx_overflow;
		} else if (reg_val_32b & gdma_irq_rx_unbuffer) {
			FMSH_ERROR("Receive buffer unavailable");
			data->stats.error_details.rx_missed_errors++;
			clearIrqMask = gdma_irq_rx_unbuffer;
			callbackArg = gdma_irq_rx_unbuffer;
		} else if (reg_val_32b & gdma_irq_rx_stop) {
			FMSH_ERROR("Receive process stopped");
			data->stats.error_details.rx_dma_failed++;
			clearIrqMask = gdma_irq_rx_stop;
			callbackArg = gdma_irq_rx_stop;
		} else if (reg_val_32b & gdma_irq_tx_stop) {
			FMSH_ERROR("Transmit process stopped");
			data->stats.error_details.tx_aborted_errors++;
			clearIrqMask = gdma_irq_tx_stop;
			callbackArg = gdma_irq_tx_stop;
		} else if (reg_val_32b & gdma_irq_tx_underflow) {
			FMSH_ERROR("Transmit underflow");
			data->stats.error_details.tx_fifo_errors++;
			clearIrqMask = gdma_irq_tx_underflow;
			callbackArg = gdma_irq_tx_underflow;
		} else if (reg_val_32b & gdma_irq_fatal_bus) {
			FMSH_ERROR("Fatal bus error");
			data->stats.error_details.tx_dma_failed++;
			data->stats.error_details.rx_dma_failed++;
			clearIrqMask = gdma_irq_fatal_bus;
			callbackArg = gdma_irq_fatal_bus;
		}
	}
	/* 正常中断处理 */
	else if (reg_val_32b & gdma_irq_nie) {

		if (reg_val_32b & gdma_irq_rx) {
			/* 关闭接收中断 */
			FGmac_Ps_SetupIntr(data->gmac_inst, gdma_irq_tx | gdma_irq_nie);
			/* 如果NAPI未在运行，则调度NAPI处理 */
			if (!atomic_test_and_set_bit(&data->rx_busy, 0)) {
				k_sem_give(&data->rx_sem);
			}
			data->stats.pkts.rx++;
			clearIrqMask = gdma_irq_rx;
			callbackArg = gdma_irq_rx;
			userCallback = data->gmac_inst->rxCallback;
		} else if (reg_val_32b & gdma_irq_early_rx) {
			FMSH_DEBUG("Early receive interrupt\n");
			clearIrqMask = gdma_irq_early_rx;
			callbackArg = gdma_irq_early_rx;
			userCallback = data->gmac_inst->listener;
		} else if (reg_val_32b & gdma_irq_tx) {
			FMSH_DEBUG("Transmit interrupt");
			data->stats.pkts.tx++;
			k_sem_give(&data->tx_sem);
			clearIrqMask = gdma_irq_tx;
			callbackArg = gdma_irq_tx;
			userCallback = data->gmac_inst->txCallback;
		} else if (reg_val_32b & gdma_irq_tx_unbuffer) {
			FMSH_DEBUG("Transmit buffer unavailable");
			data->stats.error_details.tx_fifo_errors++;
			clearIrqMask = gdma_irq_tx_unbuffer;
			callbackArg = gdma_irq_tx_unbuffer;
			userCallback = data->gmac_inst->listener;
		}
	}
	/* GMAC链路中断处理 */
	else if (reg_val_32b & gdma_irq_gli) {
		FMSH_DEBUG("GMAC line interface interrupt");
		printk("GMAC line interface interrupt\n");
		/* 更新链路状态 */
		FGmac_Ps_GetLinkStatus(data->gmac_inst);
		clearIrqMask = gdma_irq_none;
		callbackArg = gdma_irq_gli;
		userCallback = data->gmac_inst->listener;
	} else {
		FMSH_ERROR("!!! Unknown Interrupt !!!");
		return;
	}

	/* 调用回调函数 */
	if (userCallback != NULL) {
		userCallback(data->gmac_inst, callbackArg);
	}

	/* 清除中断标志 */
	if (clearIrqMask != 0) {
		FGmac_Ps_ClearIrq(data->gmac_inst, clearIrqMask);
		reg_val_32b = FMSH_IN32_32(pDma->GDMA_SR);

		if ((reg_val_32b & gdma_irq_all_ai) == 0) {
			FMSH_OUT32_32(gdma_irq_aie, pDma->GDMA_SR);
		}
		if ((reg_val_32b & gdma_irq_all_ni) == 0) {
			FMSH_OUT32_32(gdma_irq_nie, pDma->GDMA_SR);
		}
		FMSH_DEBUG("Clear interrupt");
	}

	FMSH_DEBUG("ISR end");
}

/* 获取硬件能力 */
static enum ethernet_hw_caps eth_fmsh_get_capabilities(const struct device *dev)
{
	FMSH_DEBUG("Capabilities: 1000Base-T, 100Base-T, 10Base-T, Auto-Negotiation");
	return ETHERNET_LINK_10BASE_T | ETHERNET_LINK_100BASE_T | ETHERNET_LINK_1000BASE_T |
	       ETHERNET_AUTO_NEGOTIATION_SET;
}

/* 设置硬件配置 */
static int eth_fmsh_set_config(const struct device *dev, enum ethernet_config_type type,
			       const struct ethernet_config *config)
{
	switch (type) {
	case ETHERNET_CONFIG_TYPE_AUTO_NEG:
		/* 配置自动协商 */
		break;
	case ETHERNET_CONFIG_TYPE_LINK:
		/* 配置链路速度 */
		break;
	case ETHERNET_CONFIG_TYPE_DUPLEX:
		/* 配置双工模式 */
		break;
	case ETHERNET_CONFIG_TYPE_MAC_ADDRESS:
		/* 配置MAC地址 */
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

/* 获取硬件配置 */
static int eth_fmsh_get_config(const struct device *dev, enum ethernet_config_type type,
			       struct ethernet_config *config)
{
	FMSH_DEBUG("Get config");
	switch (type) {
	case ETHERNET_CONFIG_TYPE_PRIORITY_QUEUES_NUM:
		/* 获取优先级队列数量 */
		break;
	case ETHERNET_CONFIG_TYPE_MAC_ADDRESS:
		/* 获取MAC地址 */
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

/* 接口初始化函数 */
static void eth_fmsh_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct eth_fmsh_data *data = dev->data;

	data->iface = iface;

	ethernet_init(data->iface);

	/* 添加链路状态设置 */
	net_eth_carrier_on(data->iface);

	/* 设置MAC地址 */
	net_if_set_link_addr(data->iface, data->gmac_inst->mac_address, 6, NET_LINK_ETHERNET);
	LOG_INF("MAC address: %02x:%02x:%02x:%02x:%02x:%02x", data->gmac_inst->mac_address[0],
		data->gmac_inst->mac_address[1], data->gmac_inst->mac_address[2],
		data->gmac_inst->mac_address[3], data->gmac_inst->mac_address[4],
		data->gmac_inst->mac_address[5]);

	data->napi_budget = 256;
	atomic_clear(&data->rx_busy);

	/* 创建接收线程 */
	k_thread_create(&data->rx_thread, data->rx_thread_stack, FMSH_ETH_RX_STACK_SIZE,
			eth_fmsh_rx_thread, (void *)dev, NULL, NULL,
			K_PRIO_COOP(FMSH_ETH_RX_THREAD_PRIORITY), 0, K_SECONDS(2));
	k_thread_name_set(&data->rx_thread, "eth_fmsh_rx");

	k_thread_create(&data->phy_update_thread, data->phy_update_thread_stack,
			FMSH_ETH_PYH_STACK_SIZE, eth_fmsh_phy_update, (void *)dev, NULL, NULL,
			K_IDLE_PRIO, 0, K_SECONDS(1));
	k_thread_name_set(&data->phy_update_thread, "eth_fmsh_phy_update");

	FMSH_DEBUG("Interface init done.");
}

void FGmacPs_GmacListener(FGmacPs_Instance_T *pGmac, int32_t ecode)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	FGmacPs_DmaPortMap_T *pDmaPortMap;
	u32 reg;

	pDmaPortMap = (FGmacPs_DmaPortMap_T *)((u32)pGmac->base_address + GMAC_DMA_OFFSET);

	switch (ecode) {
	case gdma_irq_tx_stop:
		FMSH_ERROR("> Irq:Tx process stopped");
		break;
	case gdma_irq_tx_unbuffer:
		FMSH_ERROR("> Irq:Tx Buffer Unavailable");
		break;
	case gdma_irq_tx_jabber_timeout:
		FMSH_ERROR("> Irq:Tx jabber timeout");
		break;
	case gdma_irq_rx_overflow:
		FMSH_ERROR("> Irq:Rx FIFO overflow");
		break;
	case gdma_irq_tx_underflow:
		FMSH_ERROR("> Irq:Tx underflow");
		break;
	case gdma_irq_rx_unbuffer:
		FMSH_ERROR("> Irq:Rx buffer unavailable");
		reg = FMSH_IN32_32(pDmaPortMap->GDMA_CRXDES);
		FGmac_Ps_ResetCurRxDES(pGmac, (FGmacPs_RxDescriptor_T *)reg);
		break;
	case gdma_irq_rx_stop:
		FMSH_ERROR("> Irq:Rx process stopped");
		break;
	case gdma_irq_rx_wd_timeout:
		FMSH_ERROR("> Irq:Rx watchdog timeout");
		break;
	case gdma_irq_early_tx:
		FMSH_ERROR("> Irq:Early Tx interrupt");
		break;
	case gdma_irq_fatal_bus:
		FMSH_ERROR("> Irq:Fatal bus error");
		break;
	case gdma_irq_early_rx:
		FMSH_ERROR("> Irq:Early Rx interrupt");
		break;
	case gdma_irq_gli: /* not finished */
		FMSH_ERROR("> Irq:GMAC Line Interface Interrupt");
		reg = FMSH_IN32_32(pGmacPortMap->GMAC_ISR);
		if ((reg & GMAC_ISR_RGSMIIIS) != 0) {
			FMSH_ERROR("RGMII or SMII Interrupt, Link status change");
			/* GLI will be cleared when read these bits */
			FGmac_Ps_GetLinkStatus(pGmac);
		}
		break;
	default:
		FMSH_ERROR("> unexpected argument: 0x%x \r\n", ecode);
		break;
	}
}

void FGmacPs_GmacRxCallback(FGmacPs_Instance_T *pGmac, int32_t ecode)
{
	FMSH_DEBUG("Rx callback");
}

void FGmacPs_GmacTxCallback(FGmacPs_Instance_T *pGmac, int32_t ecode)
{
	FMSH_DEBUG("Tx callback");
}

/* 设备初始化函数 */
static int eth_fmsh_init(const struct device *dev)
{
	struct eth_fmsh_data *data = dev->data;
	int ret;

	/* 初始化信号量 */
	k_sem_init(&data->tx_sem, 0, 1);
	k_sem_init(&data->rx_sem, 0, 1);

	data->gmac_inst = &s_GMAC_Instance;
	data->gmac_inst->pFrmBuffer = pack_buf;

	FGmac_Ps_phy_Init(data->gmac_inst->phy_cfg);
	ret = FGmac_Ps_DeviceReset(data->gmac_inst);
	if (ret != 0) {
		FMSH_ERROR("Device reset failed");
		return -EIO;
	}

	ret = FGmac_Ps_DmaInit(data->gmac_inst, GMAC0_RxDs, dw_rx, GMAC0_TxDs, dw_tx);

	if (ret != 0) {
		FMSH_ERROR("DMA init failed");
		return -EIO;
	}
	/* 配置中断 */
	FGmac_Ps_SetupIntr(data->gmac_inst, gdma_irq_none);
	FGmac_Ps_SetupIntr(data->gmac_inst, gdma_irq_tx | gdma_irq_rx | gdma_irq_nie);
	FGmac_Ps_SetListener(data->gmac_inst, (FMSH_callback)FGmacPs_GmacListener);
	FGmac_Ps_SetTxCallback(data->gmac_inst, (FMSH_callback)FGmacPs_GmacTxCallback);
	FGmac_Ps_SetRxCallback(data->gmac_inst, (FMSH_callback)FGmacPs_GmacRxCallback);

	/* 初始化MAC */
	ret = FGmac_Ps_EnTxOsf(data->gmac_inst, 1);
	if (ret != 0) {
		FMSH_ERROR("TxOsf init failed");
		return -EIO;
	}
	ret = FGmac_Ps_MacInit(data->gmac_inst);
	if (ret != 0) {
		FMSH_ERROR("Mac init failed");
		return -EIO;
	}

	FGmacPs_GmacLink_Updata(data->gmac_inst);
	FGmacPS_Gmii2rgmii_Update_Speed(data->gmac_inst,
					data->gmac_inst->phy_cfg->gmii2rgmii_mdio_addr1);
	FGmacPS_Gmii2rgmii_Update_Speed(data->gmac_inst,
					data->gmac_inst->phy_cfg->gmii2rgmii_mdio_addr2);

	/* 配置中断 */
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), eth_fmsh_isr, DEVICE_DT_INST_GET(0),
		    0);
	irq_enable(DT_INST_IRQN(0));

	FMSH_DEBUG("eth Init done");

	return 0;
}

/* 更新后的网络接口操作集 */
static const struct ethernet_api eth_fmsh_api = {
	.iface_api.init = eth_fmsh_iface_init,
	.send = eth_fmsh_send,
	.get_capabilities = eth_fmsh_get_capabilities,
	.set_config = eth_fmsh_set_config,
	.get_config = eth_fmsh_get_config,
};

/* 设备定义 */
#define ETH_FMSH_INIT(n)                                                                           \
	static const struct eth_fmsh_config eth_fmsh_config_##n = {                                \
		.base_addr = DT_INST_REG_ADDR(n),                                                  \
		.irq_num = DT_INST_IRQN(n),                                                        \
		.irq_priority = DT_INST_IRQ(n, priority),                                          \
	};                                                                                         \
                                                                                                   \
	static struct eth_fmsh_data eth_fmsh_runtime_##n;                                          \
                                                                                                   \
	ETH_NET_DEVICE_DT_INST_DEFINE(0, eth_fmsh_init, NULL, &eth_fmsh_runtime_##n,               \
				      &eth_fmsh_config_##n, CONFIG_ETH_INIT_PRIORITY,              \
				      &eth_fmsh_api, NET_ETH_MTU);

DT_INST_FOREACH_STATUS_OKAY(ETH_FMSH_INIT)
