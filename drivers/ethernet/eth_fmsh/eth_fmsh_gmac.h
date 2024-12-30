/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_DRIVERS_ETHERNET_ETH_FMSH_H_
#define ZEPHYR_DRIVERS_ETHERNET_ETH_FMSH_H_

#include <zephyr/net/net_stats.h>
#include "gmacps/fmsh_gmac_lib.h"

#define FMSH_ETH_RX_STACK_SIZE      4096
#define FMSH_ETH_PYH_STACK_SIZE     2048
#define FMSH_ETH_RX_THREAD_PRIORITY 5

/* DMA描述符结构 */
struct gmac_dma_desc {
	uint32_t status;
	uint32_t ctrl;
	uint32_t buf_addr;
	uint32_t next;
};

/* GMAC配置结构 */
struct eth_fmsh_gmac_config {
	uint32_t base_addr;
	uint32_t irq_num;
	const struct device *phy_dev;
};

/* 驱动配置结构体 */
struct eth_fmsh_config {
	uint32_t base_addr;    /* 寄存器基地址 */
	uint32_t irq_num;      /* 中断号 */
	uint32_t irq_priority; /* 中断优先级 */
	uint32_t mac_speed;    /* MAC速度 */
	uint32_t phy_addr;     /* PHY地址 */
	uint32_t feature0;     /* 硬件特性寄存器0 */
	uint32_t feature1;     /* 硬件特性寄存器1 */
	uint32_t feature2;     /* 硬件特性寄存器2 */
	uint32_t feature3;     /* 硬件特性寄存器3 */
};

/* 驱动运行时数据结构体 */
struct eth_fmsh_data {
	/* 网络接口 */
	struct net_if *iface;

	/* GMAC实例 */
	FGmacPs_Instance_T *gmac_inst;

	/* 同步原语 */
	struct k_sem tx_sem;
	struct k_sem rx_sem;
	struct k_mutex tx_mutex;
	atomic_t tx_pending;

	/* 接收线程 */
	struct k_thread rx_thread;
	k_tid_t rx_thread_id;

	K_KERNEL_STACK_MEMBER(rx_thread_stack, FMSH_ETH_RX_STACK_SIZE);

	/* phy update */
	struct k_thread phy_update_thread;
	k_tid_t phy_update_thread_id;

	K_KERNEL_STACK_MEMBER(phy_update_thread_stack, FMSH_ETH_PYH_STACK_SIZE);

	/* 统计信息 */
	struct net_stats_eth stats;

	/* 状态标志 */

	atomic_t rx_busy; /* NAPI处理状态标志 */
	int napi_budget;  /* 单次处理配额 */
};

#endif /* ZEPHYR_DRIVERS_ETHERNET_ETH_FMSH_H_ */
