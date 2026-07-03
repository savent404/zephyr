/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/cache.h>
#include <zephyr/irq.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "eth_fmsh_gmac_priv.h"
#include "eth_fmsh_gmac.h"
#include "gmacps/fmsh_gmac_hw.h"

#define DT_DRV_COMPAT snps_fmsh_gmac

#define LOG_LEVEL CONFIG_ETHERNET_LOG_LEVEL

LOG_MODULE_REGISTER(eth_fmsh);

struct fmsh_rx_dma_buf_meta {
	struct eth_fmsh_data *data;
	struct net_buf *owner;
	uint16_t first_idx;
	uint16_t desc_count;
};

static void eth_fmsh_rx_dma_buf_destroy(struct net_buf *buf);

#define FMSH_RX_DMA_BUF_META_OFFSET                                                                \
	ROUND_UP(CONFIG_NET_PKT_BUF_USER_DATA_SIZE, __alignof__(struct fmsh_rx_dma_buf_meta))

static struct fmsh_rx_dma_buf_meta *eth_fmsh_rx_dma_buf_meta(struct net_buf *buf)
{
	return (struct fmsh_rx_dma_buf_meta *)((uint8_t *)net_buf_user_data(buf) +
					       FMSH_RX_DMA_BUF_META_OFFSET);
}

/* DMA缓冲区池 */
NET_BUF_POOL_DEFINE(rx_dma_pool, 512, GMAC_RBUFFER_UNIT_SIZE,
		    FMSH_RX_DMA_BUF_META_OFFSET + sizeof(struct fmsh_rx_dma_buf_meta),
		    eth_fmsh_rx_dma_buf_destroy);
#define FMSH_ETH_TX_DESC_WAIT_TIMEOUT_MS 1000

static uint16_t eth_fmsh_rx_desc_next(const struct eth_fmsh_data *data, uint16_t idx)
{
	idx++;
	if (idx >= data->gmac_inst->wRxListSize) {
		idx = 0U;
	}

	return idx;
}

static bool eth_fmsh_rx_desc_is_held(struct eth_fmsh_data *data, uint16_t idx)
{
	k_spinlock_key_t key;
	bool held;

	key = k_spin_lock(&data->rx_desc_lock);
	held = data->rx_desc_held[idx] != 0U;
	if (held) {
		data->rx_waiting_on_held_desc = true;
	}
	k_spin_unlock(&data->rx_desc_lock, key);

	return held;
}

static bool eth_fmsh_rx_desc_is_held_cb(void *arg, u16 idx)
{
	return eth_fmsh_rx_desc_is_held(arg, idx);
}

static void eth_fmsh_rx_desc_hold(struct eth_fmsh_data *data, uint16_t first_idx,
				  uint16_t desc_count)
{
	k_spinlock_key_t key;
	uint16_t idx = first_idx;

	key = k_spin_lock(&data->rx_desc_lock);
	for (uint16_t i = 0U; i < desc_count; i++) {
		data->rx_desc_held[idx] = 1U;
		idx = eth_fmsh_rx_desc_next(data, idx);
	}
	k_spin_unlock(&data->rx_desc_lock, key);
}

static void eth_fmsh_rx_desc_release(struct eth_fmsh_data *data, uint16_t first_idx,
				     uint16_t desc_count)
{
	k_spinlock_key_t key;
	uint16_t idx = first_idx;
	bool wake_rx;
	bool released_held = false;
	bool dma_stalled;

	key = k_spin_lock(&data->rx_desc_lock);
	for (uint16_t i = 0U; i < desc_count; i++) {
		if (data->rx_desc_held[idx] != 0U) {
			released_held = true;
		}
		data->rx_desc_held[idx] = 0U;
		data->gmac_inst->pRxD[idx].RDES0.val = (u32)GMAC_RDES0_OWN;
		idx = eth_fmsh_rx_desc_next(data, idx);
	}
	wake_rx = data->rx_waiting_on_held_desc;
	data->rx_waiting_on_held_desc = false;
	dma_stalled = data->rx_dma_stalled;
	data->rx_dma_stalled = false;
	k_spin_unlock(&data->rx_desc_lock, key);

	/* DMA may suspend on a returned descriptor before software observes it.
	 * Kick after returning a real held descriptor, or a known DMA stall.
	 */
	if (dma_stalled || released_held) {
		gmac_DmaRxPollDemand(data->gmac_inst);
	}
	if (wake_rx) {
		k_sem_give(&data->rx_sem);
	}
}

static void eth_fmsh_rx_dma_buf_destroy(struct net_buf *buf)
{
	struct fmsh_rx_dma_buf_meta *meta = eth_fmsh_rx_dma_buf_meta(buf);

	if (meta->owner == buf && meta->data != NULL && meta->desc_count != 0U &&
	    (buf->flags & NET_BUF_EXTERNAL_DATA) != 0U) {
		eth_fmsh_rx_desc_release(meta->data, meta->first_idx, meta->desc_count);
		meta->data = NULL;
		meta->owner = NULL;
		meta->desc_count = 0U;
	}

	net_buf_destroy(buf);
}

/* 回收已完成发送的描述符，并返还可用描述符计数 */
static void eth_fmsh_tx_reclaim(struct eth_fmsh_data *data)
{
	FGmacPs_TxDescriptor_T *tx_desc = data->gmac_inst->pTxD;
	u16 tx_tail = data->gmac_inst->wTxTail;
	const u16 tx_head = data->gmac_inst->wTxHead;
	const u16 tx_desc_num = data->gmac_inst->wTxListSize;
	u16 reclaimed = 0U;

	while (tx_tail != tx_head) {
		if (tx_desc[tx_tail].TDES0.val & GMAC_TDES0_OWN) {
			break;
		}

		tx_tail++;
		if (tx_tail >= tx_desc_num) {
			tx_tail = 0U;
		}
		reclaimed++;
	}

	data->gmac_inst->wTxTail = tx_tail;

	while (reclaimed > 0U) {
		k_sem_give(&data->tx_sem);
		reclaimed--;
	}
}

/* 数据发送接口 */
static int eth_fmsh_send(const struct device *dev, struct net_pkt *pkt)
{
	struct eth_fmsh_data *ctx = dev->data;
	int ret;

	/* 获取当前发送描述符 */
	FGmacPs_TxDescriptor_T *pTxDES_tmp; /* tmp pDES */
	u16 TxDES_Idx;                      /* tmp Idx of TxDES */
	u32 TDes_num = ctx->gmac_inst->wTxListSize;
	u32 TxDesBufSize = ctx->gmac_inst->TxDesBufSize;

	pTxDES_tmp = &ctx->gmac_inst->pTxD[0];
	u32 size = net_pkt_get_len(pkt);

	/* 预占用一个可用Tx描述符，避免永久阻塞 */
	ret = k_sem_take(&ctx->tx_sem, K_MSEC(FMSH_ETH_TX_DESC_WAIT_TIMEOUT_MS));
	if (ret == -EAGAIN) {
		unsigned int key = irq_lock();

		/* 中断丢失时主动回收一次已完成描述符 */
		eth_fmsh_tx_reclaim(ctx);
		irq_unlock(key);

		ret = k_sem_take(&ctx->tx_sem, K_NO_WAIT);
		if (ret == -EAGAIN) {
			FMSH_ERROR("Timeout waiting for free Tx descriptor");
			return -ETIMEDOUT;
		}
	}
	if (ret != 0) {
		return ret;
	}

	/* 获取发送锁 */
	k_mutex_lock(&ctx->tx_mutex, K_FOREVER);

	/* initial */
	TxDES_Idx = ctx->gmac_inst->wTxHead;

	/* 信号量与描述符状态失步时避免覆写DMA仍在使用的描述符 */
	if (pTxDES_tmp[TxDES_Idx].TDES0.val & GMAC_TDES0_OWN) {
		FMSH_ERROR("Tx descriptor %u still owned by DMA", TxDES_Idx);
		k_sem_give(&ctx->tx_sem);
		k_mutex_unlock(&ctx->tx_mutex);
		return -EIO;
	}

	/* 清空描述符状态 */
	pTxDES_tmp[TxDES_Idx].TDES0.val = 0;
	pTxDES_tmp[TxDES_Idx].TDES1.val = 0;

	uint32_t *buf_addr = (uint32_t *)(pTxDES_tmp[TxDES_Idx].BufferAdd1);

	/* 拷贝数据到DMA缓冲区 */
	ret = net_pkt_read(pkt, buf_addr, size);
	if (ret < 0) {
		FMSH_ERROR("net_pkt_read error");
		k_sem_give(&ctx->tx_sem);
		k_mutex_unlock(&ctx->tx_mutex);
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

	/* 更新统计 */
	ctx->stats.bytes.sent += size;
	ctx->stats.pkts.tx++;

	k_mutex_unlock(&ctx->tx_mutex);

	return 0;
}

static void eth_fmsh_gmac_link_state_update(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	struct eth_fmsh_data *ctx = dev->data;
	const struct eth_fmsh_config *config = dev->config;
	const char *phy_mode = ctx->gmac_inst->phy_cfg->phy_mode;
	bool has_phy = (phy_mode != NULL) && (strcmp(phy_mode, "none") != 0);

	while (1) {
		k_sleep(K_MSEC(1000));

		if (has_phy) {
			FGmacPs_GmacLink_Updata(ctx->gmac_inst);
			FGmacPs_GetRxErrCount(ctx->gmac_inst);
		}

		if (config->instance_id == 0) {
			FGmacPS_Gmii2rgmii_Update_Speed(
				ctx->gmac_inst, ctx->gmac_inst->phy_cfg->gmii2rgmii_mdio_addr1);
		} else {
			FGmacPS_Gmii2rgmii_Update_Speed(
				ctx->gmac_inst, ctx->gmac_inst->phy_cfg->gmii2rgmii_mdio_addr2);
		}
	}
}

/* 数据接收线程 */
static void eth_fmsh_rx_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	struct eth_fmsh_data *data = dev->data;
	int budget;
	int ret;
	FGmacPs_RxFrame_T frame;
	struct net_pkt *pkt;
	struct net_buf *buf;
	struct fmsh_rx_dma_buf_meta *meta;

	while (1) {
		/* 等待中断触发 */
		k_sem_take(&data->rx_sem, K_FOREVER);

		/* 进入轮询模式 */
		budget = data->napi_budget;
		while (budget > 0) {

			ret = FGmac_Ps_RcvPollEFrameZeroCopy(data->gmac_inst, &frame,
							     eth_fmsh_rx_desc_is_held_cb, data);
			if (ret == GMAC_RETURN_CODE_RX_NULL) {
				break;
			}
			if (ret != GMAC_RETURN_CODE_OK) {
				data->stats.error_details.rx_frame_errors++;
				budget--;
				continue;
			}

			eth_fmsh_rx_desc_hold(data, frame.first_idx, frame.desc_count);

			pkt = net_pkt_rx_alloc_on_iface(data->iface, K_NO_WAIT);
			if (pkt == NULL) {
				data->stats.error_details.rx_buf_alloc_failed++;
				eth_fmsh_rx_desc_release(data, frame.first_idx, frame.desc_count);
				continue;
			}

			buf = net_buf_alloc_with_data(&rx_dma_pool, frame.data, frame.len,
						      K_NO_WAIT);
			if (buf == NULL) {
				data->stats.error_details.rx_buf_alloc_failed++;
				net_pkt_unref(pkt);
				eth_fmsh_rx_desc_release(data, frame.first_idx, frame.desc_count);
				continue;
			}

			meta = eth_fmsh_rx_dma_buf_meta(buf);
			meta->data = data;
			meta->owner = buf;
			meta->first_idx = frame.first_idx;
			meta->desc_count = frame.desc_count;

			net_pkt_append_buffer(pkt, buf);

			if (net_recv_data(data->iface, pkt) < 0) {
				data->stats.error_details.rx_frame_errors++;
				net_pkt_unref(pkt);
			} else {
				data->stats.bytes.received += frame.len;
				data->stats.pkts.rx++;
			}

			budget--;
		}
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

	/* Tx相关中断优先回收已完成描述符，更新可用描述符计数 */
	if (reg_val_32b & (gdma_irq_tx | gdma_irq_tx_unbuffer | gdma_irq_tx_stop |
			   gdma_irq_tx_underflow | gdma_irq_tx_jabber_timeout)) {
		eth_fmsh_tx_reclaim(data);
	}

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
			k_spinlock_key_t key;

			FMSH_ERROR("Receive buffer unavailable");
			data->stats.error_details.rx_missed_errors++;
			key = k_spin_lock(&data->rx_desc_lock);
			data->rx_dma_stalled = true;
			k_spin_unlock(&data->rx_desc_lock, key);
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
	struct eth_fmsh_data *data = dev->data;
	FGmacPs_Instance_T *pGmac = data->gmac_inst;
	const uint8_t *mac_addr = config->mac_address.addr;
	int ret;

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
		memcpy(pGmac->mac_address, mac_addr, 6);
		net_if_set_link_addr(data->iface, pGmac->mac_address, 6, NET_LINK_ETHERNET);
		ret = FGmac_Ps_SetupMacAddr(pGmac, 0, pGmac->mac_address, 1, 0, 0);
		if (ret != 0) {
			FMSH_ERROR("Failed to set MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
				   mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4],
				   mac_addr[5]);
			return -EIO;
		}
		FMSH_DEBUG("MAC address set to: %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0],
			   mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
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
	const struct eth_fmsh_data *ctx = dev->data;

	FMSH_DEBUG("Get config");
	switch (type) {
	case ETHERNET_CONFIG_TYPE_PRIORITY_QUEUES_NUM:
		/* 获取优先级队列数量 */
		break;
	case ETHERNET_CONFIG_TYPE_MAC_ADDRESS:
		/* 获取MAC地址 */
		memcpy(config->mac_address.addr, ctx->net_config.mac_address, 6);
		break;
	case ETHERNET_CONFIG_TYPE_LINK:
		/* 获取链接配置 */
		config->l.link_10bt = false;
		config->l.link_100bt = false;
		config->l.link_1000bt = false;

		if (!ctx || !ctx->gmac_inst || !ctx->gmac_inst->gmac_link_status) {
			break;
		}

		uint8_t link_status = ctx->gmac_inst->gmac_link_status->link_status;
		uint8_t link_speed = ctx->gmac_inst->gmac_link_status->link_speed;

		FMSH_DEBUG("link status: %d, speed: %d", link_status, link_speed);

		if (!link_status) {
			break;
		}

		switch (link_speed) {
		case 0:
			config->l.link_10bt = true;
			break;
		case 1:
			config->l.link_100bt = true;
			break;
		case 2:
			config->l.link_1000bt = true;
			break;
		default:
			break;
		}
		break;
	default:
		FMSH_DEBUG("unsupported config type: %d", type);
		return -ENOTSUP;
	}
	return 0;
}

/* 接口初始化函数 */
static void eth_fmsh_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct eth_fmsh_data *data = dev->data;
	const struct eth_fmsh_config *config = dev->config;
	struct net_config *net_config = &data->net_config;

	data->iface = iface;

	ethernet_init(data->iface);

	/* 添加链路状态设置 */
	net_eth_carrier_on(data->iface);

	/* 设置MAC地址 */
	net_config->mac_address = data->gmac_inst->mac_address;
	net_if_set_link_addr(data->iface, net_config->mac_address, 6, NET_LINK_ETHERNET);
	LOG_INF("GMAC%d: MAC address: %02x:%02x:%02x:%02x:%02x:%02x", config->instance_id,
		net_config->mac_address[0], net_config->mac_address[1], net_config->mac_address[2],
		net_config->mac_address[3], net_config->mac_address[4], net_config->mac_address[5]);

	/*  配置IP地址、子网掩码和网关  */
	struct in_addr addr, netmask, gw;
	struct net_if_addr *ifaddr;

	/*  设置IP地址  */
	net_addr_pton(AF_INET, net_config->ip_address, &addr);
	ifaddr = net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);

	/*  设置子网掩码  */
	net_addr_pton(AF_INET, net_config->netmask, &netmask);
	net_if_ipv4_set_netmask_by_addr(iface, (const struct in_addr *)ifaddr, &netmask);

	/*  设置网关  */
	net_addr_pton(AF_INET, net_config->gateway, &gw);
	net_if_ipv4_set_gw(iface, &gw);

	data->napi_budget = 256;
	atomic_clear(&data->rx_busy);

	BUILD_ASSERT(FMSH_ETH_RX_THREAD_PRIORITY < CONFIG_NUM_COOP_PRIORITIES,
		     "ETH RX thread cooperative priority exceeds CONFIG_NUM_COOP_PRIORITIES");

	/* 创建接收线程 */
	char thread_name[32];

	snprintf(thread_name, sizeof(thread_name), "eth_fmsh_rx%d", config->instance_id);
	k_thread_create(&data->rx_thread, data->rx_thread_stack, FMSH_ETH_RX_STACK_SIZE,
			eth_fmsh_rx_thread, (void *)dev, NULL, NULL,
			K_PRIO_COOP(FMSH_ETH_RX_THREAD_PRIORITY), 0, K_SECONDS(2));
	k_thread_name_set(&data->rx_thread, thread_name);

	snprintf(thread_name, sizeof(thread_name), "eth_fmsh_phy_update%d", config->instance_id);
	k_thread_create(
		&data->phy_update_thread, data->phy_update_thread_stack, FMSH_ETH_PYH_STACK_SIZE,
		eth_fmsh_gmac_link_state_update, (void *)dev, NULL, NULL,
		CLAMP(FMSH_ETH_PHY_UPDATE_THREAD_PRIORITY, 0, K_LOWEST_APPLICATION_THREAD_PRIO), 0,
		K_SECONDS(1));
	k_thread_name_set(&data->phy_update_thread, thread_name);

	FMSH_DEBUG("Interface init done.");
}

void FGmacPs_GmacListener(FGmacPs_Instance_T *pGmac, int32_t ecode)
{
	FGmacPs_MacPortMap_T *pGmacPortMap = pGmac->base_address;
	u32 reg;

	switch (ecode) {
	case gdma_irq_tx_stop:
		FMSH_ERROR("> Irq:Tx process stopped");
		break;
	case gdma_irq_tx_unbuffer:
		FMSH_DEBUG("> Irq:Tx Buffer Unavailable");
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
		FMSH_DEBUG("> Irq:Early Rx interrupt");
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

/* 检查网络接口 PHY 模式的回调函数 */
static void is_mac2mac(struct net_if *iface, void *user_data)
{
	const struct device *dev = net_if_get_device(iface);
	const struct eth_fmsh_data *data = dev->data;
	int *m2m = (int *)user_data;

	if (data && data->gmac_inst && data->gmac_inst->phy_cfg &&
	    data->gmac_inst->phy_cfg->phy_mode) {

		if (strcmp(data->gmac_inst->phy_cfg->phy_mode, "none") == 0) {
			*m2m = 1;
			return;
		}
	}
}

/* 检查是否存在 phy_mode 为 none 的接口 */
static int eth_fmsh_m2m_exists(void)
{
	int m2m = 0;

	net_if_foreach(is_mac2mac, &m2m);

	return m2m;
}

/* 设备初始化函数 */
static int eth_fmsh_init(const struct device *dev)
{
	struct eth_fmsh_data *data = dev->data;
	const struct eth_fmsh_config *cfg = dev->config;
	int ret;
	u16 tx_desc_num = data->gmac_inst->wTxListSize;

	/* 初始化信号量 */
	if (tx_desc_num < 2U) {
		FMSH_ERROR("Invalid Tx descriptor count: %u", tx_desc_num);
		return -EINVAL;
	}
	k_sem_init(&data->tx_sem, tx_desc_num - 1U, tx_desc_num - 1U);
	k_sem_init(&data->rx_sem, 0, 1);
	k_mutex_init(&data->tx_mutex);
	memset(data->rx_desc_held, 0, sizeof(data->rx_desc_held));
	data->rx_waiting_on_held_desc = false;
	data->rx_dma_stalled = false;

	data->gmac_inst->pFrmBuffer = data->packet_buffer;

	/* 检查并处理 phy_mode 为 none 的情况 */
	int m2m = eth_fmsh_m2m_exists();

	if (m2m) {
		LOG_INF("Detected a GMAC interface without PHY");
	}
	FGmac_Ps_phy_Init(data->gmac_inst->phy_cfg);
	ret = FGmac_Ps_DeviceReset(data->gmac_inst);
	if (ret != 0) {
		FMSH_ERROR("Device reset failed");
		return -EIO;
	}

	ret = FGmac_Ps_DmaInit(data->gmac_inst, data->rx_descs, data->rx_buffer, data->tx_descs,
			       data->tx_buffer);

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
	ret = FGmac_Ps_MacInit(data->gmac_inst, m2m);
	if (ret != 0) {
		FMSH_ERROR("Mac init failed");
		return -EIO;
	}

	/* 配置中断 */
	cfg->irq_config_fn();

	FMSH_DEBUG("eth Init done");

	return 0;
}

#if defined(CONFIG_NET_STATISTICS_ETHERNET)
static struct net_stats_eth *eth_fmsh_get_stats(const struct device *dev)
{
	struct eth_fmsh_data *data = dev->data;
	FGmacPs_Instance_T *pGmac = data->gmac_inst;

	data->stats.error_details.rx_crc_errors = pGmac->phy_cfg->rx_err_count;

	return &data->stats;
}
#endif

/* 更新后的网络接口操作集 */
static const struct ethernet_api eth_fmsh_api = {
	.iface_api.init = eth_fmsh_iface_init,
#if defined(CONFIG_NET_STATISTICS_ETHERNET)
	.get_stats = eth_fmsh_get_stats,
#endif
	.send = eth_fmsh_send,
	.get_capabilities = eth_fmsh_get_capabilities,
	.set_config = eth_fmsh_set_config,
	.get_config = eth_fmsh_get_config,
};

/* 设备定义 */
#define ETH_FMSH_INIT(n)                                                                           \
                                                                                                   \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, ext_phy_init_seq),                                    \
		    (static const uint32_t phy_init_stream_##n[] =                                 \
			     DT_INST_PROP(n, ext_phy_init_seq);),                                  \
		    ())                                                                            \
	static const uint32_t *const phy_init_stream_ptr_##n = COND_CODE_1(                        \
		DT_INST_NODE_HAS_PROP(n, ext_phy_init_seq), (phy_init_stream_##n), (NULL));        \
	static const uint32_t phy_init_stream_len_##n =                                            \
		COND_CODE_1(DT_INST_NODE_HAS_PROP(n, ext_phy_init_seq),                            \
			    (ARRAY_SIZE(phy_init_stream_##n)), (0));                               \
	FGmacPs_RxDescriptor_T __attribute__((section(".ocm_data")))                               \
	__aligned(CONFIG_DCACHE_LINE_SIZE) GMAC0_RxDs_##n[GMAC_RDES_NUM];                          \
	FGmacPs_TxDescriptor_T __attribute__((section(".ocm_data")))                               \
	__aligned(CONFIG_DCACHE_LINE_SIZE) GMAC0_TxDs_##n[GMAC_TDES_NUM];                          \
	uint8_t __aligned(CONFIG_DCACHE_LINE_SIZE)                                                 \
	dw_tx_##n[2 * GMAC_TDES_NUM * GMAC_RBUFFER_UNIT_SIZE];                                     \
	uint8_t __aligned(CONFIG_DCACHE_LINE_SIZE)                                                 \
	dw_rx_##n[2 * GMAC_RDES_NUM * GMAC_RBUFFER_UNIT_SIZE];                                     \
	uint8_t __aligned(CONFIG_DCACHE_LINE_SIZE) pack_buf_##n[GMAC_PACKET_BUFFER_SIZE];          \
                                                                                                   \
	static FGmacPs_LinkStatus_T s_GMAC_LinkStatus_##n;                                         \
                                                                                                   \
	static FGmacPs_Config_T s_GMAC_Config_##n = {                                              \
		.DeviceId = n,                                                                     \
		.BaseAddress = DT_INST_REG_ADDR(n),                                                \
		.Speed = DT_INST_PROP_OR(n, fixed_speed, 2),                                       \
		.InterFaceType = FPAR_GMACPS_0_INTERFACE,                                          \
	};                                                                                         \
                                                                                                   \
	static FGmacPs_PhyConfig_T s_GMAC_PhyCfg_##n = {                                           \
		.phy_device = PHY_YT8521,                                                          \
		.speed = DT_INST_PROP_OR(n, fixed_speed, 2),                                       \
		.auto_detect_ad_en = DT_INST_PROP_OR(n, auto_negotiation, 1),                      \
		.mdio_address = DT_INST_PROP(n, mdio_addr),                                        \
		.gmii2rgmii_mdio_addr1 = DT_INST_PROP(n, gmii2rgmii_addr1),                        \
		.gmii2rgmii_mdio_addr2 = DT_INST_PROP(n, gmii2rgmii_addr2),                        \
		.phy_mode = DT_INST_PROP(n, phy_mode),                                             \
		.phy_delay = DT_INST_PROP(n, phy_delay),                                           \
		.auto_nag_en = DT_INST_PROP_OR(n, auto_negotiation, 1),                            \
		.interface = FPAR_GMACPS_0_INTERFACE,                                              \
		.ext_phy_init_seq = phy_init_stream_ptr_##n,                                       \
		.ext_phy_init_seq_len = phy_init_stream_len_##n,                                   \
		.partner_phy_config =                                                              \
			{                                                                          \
				.mdio_address = DT_INST_PROP_OR(n, partner_mdio_addr,              \
								FMSH_GMAC_MDIO_INVALID_ADDR),      \
				.phy_delay = DT_INST_PROP_OR(n, partner_phy_delay, 0),             \
				.phy_mode = DT_INST_PROP_OR(n, partner_phy_mode, NULL),            \
			},                                                                         \
	};                                                                                         \
                                                                                                   \
	static FGmacPs_Instance_T s_GMAC_Instance_##n = {                                          \
		.index = n,                                                                        \
		.base_address = (void *)DT_INST_REG_ADDR(n),                                       \
		.mdio_base_address = (void *)DT_INST_REG_ADDR(n),                                  \
		.mac_address = DT_INST_PROP(n, local_mac_address),                                 \
		.csr_clk = GMAC_CSR_CLK,                                                           \
		.wRxListSize = GMAC_RDES_NUM,                                                      \
		.wTxListSize = GMAC_TDES_NUM,                                                      \
		.RxDesBufSize = GMAC_RBUFFER_UNIT_SIZE,                                            \
		.TxDesBufSize = GMAC_TBUFFER_UNIT_SIZE,                                            \
		.pFrmBuffer = NULL,                                                                \
		.FrmBufferSize = GMAC_PACKET_BUFFER_SIZE,                                          \
		.wTxHead = 0,                                                                      \
		.wTxTail = 0,                                                                      \
		.phy_cfg = &s_GMAC_PhyCfg_##n,                                                     \
		.gmac_link_status = &s_GMAC_LinkStatus_##n,                                        \
		.gmac_cfg = &s_GMAC_Config_##n,                                                    \
	};                                                                                         \
                                                                                                   \
	static struct eth_fmsh_data eth_fmsh_runtime_##n = {                                       \
		.gmac_inst = &s_GMAC_Instance_##n,                                                 \
		.rx_descs = GMAC0_RxDs_##n,                                                        \
		.tx_descs = GMAC0_TxDs_##n,                                                        \
		.tx_buffer = dw_tx_##n,                                                            \
		.rx_buffer = dw_rx_##n,                                                            \
		.packet_buffer = pack_buf_##n,                                                     \
		.net_config =                                                                      \
			{                                                                          \
				.ip_address = DT_INST_PROP(n, local_ip_address),                   \
				.netmask = DT_INST_PROP(n, local_netmask),                         \
				.gateway = DT_INST_PROP(n, local_gateway),                         \
			},                                                                         \
	};                                                                                         \
                                                                                                   \
	static void eth_fmsh_irq_init_##n(void)                                                    \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), eth_fmsh_isr,               \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}                                                                                          \
                                                                                                   \
	static const struct eth_fmsh_config eth_fmsh_config_##n = {                                \
		.base_addr = DT_INST_REG_ADDR(n),                                                  \
		.irq_num = DT_INST_IRQN(n),                                                        \
		.irq_priority = DT_INST_IRQ(n, priority),                                          \
		.instance_id = n,                                                                  \
		.irq_config_fn = eth_fmsh_irq_init_##n,                                            \
	};                                                                                         \
                                                                                                   \
	ETH_NET_DEVICE_DT_INST_DEFINE(n, eth_fmsh_init, NULL, &eth_fmsh_runtime_##n,               \
				      &eth_fmsh_config_##n, CONFIG_ETH_INIT_PRIORITY,              \
				      &eth_fmsh_api, NET_ETH_MTU);

DT_INST_FOREACH_STATUS_OKAY(ETH_FMSH_INIT)
