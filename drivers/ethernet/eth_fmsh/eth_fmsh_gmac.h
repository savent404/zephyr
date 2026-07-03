/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_DRIVERS_ETHERNET_ETH_FMSH_H_
#define ZEPHYR_DRIVERS_ETHERNET_ETH_FMSH_H_

#include <zephyr/net/net_stats.h>
#include "gmacps/fmsh_gmac_lib.h"

#define FMSH_ETH_RX_STACK_SIZE              4096
#define FMSH_ETH_PYH_STACK_SIZE             2048
#define FMSH_ETH_RX_THREAD_PRIORITY         CONFIG_ETH_FMQL_RX_THREAD_PRIORITY
#define FMSH_ETH_PHY_UPDATE_THREAD_PRIORITY CONFIG_ETH_FMQL_PHY_UPDATE_THREAD_PRIORITY

typedef void (*eth_config_irq_t)(void);

/* 驱动配置结构体 */
struct eth_fmsh_config {
	uint32_t instance_id;    /* GMAC实例号 */
	uint32_t base_addr;      /* 寄存器基地址 */
	uint32_t mdio_base_addr; /* mdio寄存器基地址 */
	uint32_t irq_num;        /* 中断号 */
	uint32_t irq_priority;   /* 中断优先级 */
	eth_config_irq_t irq_config_fn;
};

/* 网络配置结构体 */
struct net_config {
	uint8_t *mac_address; /* MAC地址 */
	uint8_t *ip_address;  /* IP地址 */
	uint8_t *netmask;     /* 子网掩码 */
	uint8_t *gateway;     /* 网关地址 */
};

/* 驱动运行时数据结构体 */
struct eth_fmsh_data {
	/* 网络接口 */
	struct net_if *iface;

	/* GMAC实例 */
	FGmacPs_Instance_T *gmac_inst;

	/* 网络配置 */
	struct net_config net_config;

	/* 同步原语 */
	struct k_sem tx_sem;
	struct k_sem rx_sem;
	struct k_mutex tx_mutex;
	struct k_spinlock rx_desc_lock;

	/* 接收线程 */
	struct k_thread rx_thread;

	K_KERNEL_STACK_MEMBER(rx_thread_stack, FMSH_ETH_RX_STACK_SIZE);

	/* phy update */
	struct k_thread phy_update_thread;

	K_KERNEL_STACK_MEMBER(phy_update_thread_stack, FMSH_ETH_PYH_STACK_SIZE);

	/* 统计信息 */
	struct net_stats_eth stats;

	/* 状态标志 */
	atomic_t rx_busy; /* NAPI处理状态标志 */
	int napi_budget;  /* 单次处理配额 */
	uint8_t rx_desc_held[GMAC_RDES_NUM];
	bool rx_waiting_on_held_desc;
	bool rx_dma_stalled; /* DMA因无空闲descriptor而停顿 */

	FGmacPs_RxDescriptor_T *rx_descs;
	FGmacPs_TxDescriptor_T *tx_descs;
	uint8_t *tx_buffer;
	uint8_t *rx_buffer;
	uint8_t *packet_buffer;
};

#endif /* ZEPHYR_DRIVERS_ETHERNET_ETH_FMSH_H_ */
