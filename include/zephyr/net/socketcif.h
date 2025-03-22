/**
 * @brief SocketCIF definitions.
 *
 * Definitions for SocketCIF support.
 */

/*
 * Copyright (c) 2025 SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_SOCKETCIF_H_
#define ZEPHYR_INCLUDE_NET_SOCKETCIF_H_

#include <zephyr/types.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_if.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SocketCIF library
 * @defgroup socket_can SocketCIF library
 * @since 3.6
 * @version 0.1.0
 * @ingroup networking
 * @{
 */

/** Protocols of the protocol family PF_CIF */
#define CIF_RAW_MASTER 1
#define CIF_RAW_SLAVE  2

/** @cond INTERNAL_HIDDEN */

/* SocketCIF options */
#define SOL_CIF_BASE 200
#define SOL_CIF_RAW  (SOL_CIF_BASE)

/* DR/DT for MCB */
#define CIF_DR_Unknown 0x00
#define CIF_DR_MPU_B   0x01
#define CIF_DR_EXT_B   0x02
#define CIF_DR_IO      0x03
#define CIF_DR_ETH     0x04
#define CIF_DR_MPU_P   0x81
#define CIF_DR_EXT_P   0x82

#define CIF_DT_Unknown 0x00
#define CIF_DT_MPU_P   0x01
#define CIF_DT_MPU_B   0x02
#define CIF_DT_IO_AI   0x10
#define CIF_DT_IO_AO   0x11
#define CIF_DT_IO_DI   0x12
#define CIF_DT_IO_DO   0x13
#define CIF_DT_ETH     0x20
#define CIF_DT_CAN     0x21
#define CIF_DT_EXT_P   0x30
#define CIF_DT_EXT_B   0x31

#define CIF_IS_DR_MASTER(dr) ((dr) & 0x80)

enum {
	CIF_OPT_MASTER_CONFIG = 1,
	CIF_OPT_SLAVE_CONFIG = 2,
	CIF_OPT_PORT,
	CIF_OPT_ERROR,
	CIF_OPT_INFO,
	CIF_OPT_MAX,
};

struct cif_raw_master_config {
	uint32_t poll_time;    /* in microseconds */
	uint32_t cycle_time;   /* in microseconds */
	uint32_t sync_timeout; /* in microseconds */
	uint8_t dr;
	uint8_t dt;
};

struct cif_raw_slave_config {
	unsigned int want_preempt;
	uint8_t dr;
	uint8_t dt;
};

struct cif_raw_port_config {
	/* directory: IN */
	uint8_t port;
	uint8_t slot;

	/* directory: IN */
#define CIF_PORT_FLG_ENABLE       0x01
#define CIF_PORT_FLG_ONE_SHOT     0x02
#define CIF_PORT_FLG_PREEMPT      0x04
#define CIF_PORT_FLG_ALLOW_WRITE  0x08
#define CIF_PORT_FLG_STRONG_ORDER 0x10   /* Drop rsp if rsp.xid!=req.rxid or the first one */
	unsigned int flags;              /* see CIF_PORT_FLG_* */
	unsigned int async_interval;     /* query interval while port is idle. (in microseconds) */
	unsigned int async_timeout;      /* async timeout. (in microseconds) */
	unsigned int async_bandwidth;    /* async bandwidth. (in pps) */
	unsigned int slave_max_recv_len; /* max receive length for slave. */
};

struct cif_error_filter {
	/* directory: IN */
	uint8_t slot;
	uint8_t port;
#define CIF_FILTER_SLOT 0x01
#define CIF_FILTER_PORT 0x02
	unsigned int flags;

	/* directory: IN/OUT */
#define CIF_ERR_R_ERROR       BIT(12) /* R_ERROR, redundancy error. One of the frames lost */
#define CIF_ERR_I_ERROR       BIT(13) /* recv invalid frame */
#define CIF_ERR_MAY_LOST      BIT(14) /* XID mismatch, means previous packet may be lost */
#define CIF_ERR_PREEMPT       BIT(15) /* Preempted by R=1 frame */
#define CIF_ERR_PREV_T_ERROR  BIT(16) /* T_ERROR happened in previous operation */
#define CIF_ERR_PREV_R_ERROR  BIT(17) /* R_ERROR happened in previous operation */
#define CIF_ERR_PREV_I_ERROR  BIT(18) /* I_ERROR happened in previous operation */
#define CIF_ERR_PREV_P_ERROR  BIT(19) /* P_ERROR happened in previous operation */
#define CIF_ERR_PREV_MAY_LOST BIT(20) /* Previous packet may be lost */
#define CIF_ERR_PREV_PREEMPT  BIT(21) /* R=1 frame receive in previous operation */
#define CIF_ERR_PREV_INVALID_ASYNC_PACK                                                            \
	BIT(22) /* Invalid async packet happened in previous operation */
#define CIF_ERR_PREV_RX_DROP_NOMEM     BIT(23) /* Drop packet due to no memory */
#define CIF_ERR_PREV_RX_DROP_FIFO_FULL BIT(24) /* Drop packet due to rx buffer full */
#define CIF_ERR_PREV_RX_DROP_DUPLICATE BIT(25) /* Drop packet due to duplicate */
#define CIF_ERR_PREV_RX_DROP_INVALID   BIT(26) /* Drop packet due to invalid */
#define CIF_ERR_MASK                   ((BIT_MASK(15) << 12))
	uint32_t error_mask;
};

struct cif_info {
	uint8_t slot;
	uint32_t hw_version;
};

/** @endcond */

/* SocketCIF MTU size */
/** CIF frame MTU */
#define CIF_MTU (0x800)

/* SocketCIF address bus */
enum {
	CIF_BUS_DEFAULT = 0,
	CIF_BUS_SLOW = 0,
	CIF_BUS_FAST,
	CIF_BUS_MAX,
};

/* This is for CIF socket address */
#define CIF_PORT_MOD(n)           ((n) & 0x1F) /* 0x00 ~ 0x1F */
#define CIF_IS_SYNC_PORT(port)    (CIF_PORT_MOD(port) < 0x08)
#define CIF_IS_ASYNC_PORT(port)   (CIF_PORT_MOD(port) >= 0x08)
#define CIF_IS_UNKNOWN_PORT(port) (!(CIF_IS_SYNC_PORT(port) || CIF_IS_ASYNC_PORT(port)))

/**
 * struct sockaddr_can - The sockaddr structure for CIF sockets
 *
 */
struct sockaddr_cif {
	sa_family_t cif_family; /**< Address family */
	uint8_t bus;            /* see CIF_BUS_*. Only works for bind() */
	uint8_t slot;           /* slot number, not used in bind() */
	uint8_t port;           /* port number , not used in bind() */
};

/** @} */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_SOCKETCIF_H_ */
