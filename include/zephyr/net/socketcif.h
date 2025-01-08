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

enum {
	CIF_OPT_MASTER_CONFIG = 1,
	CIF_OPT_PORT,
	CIF_OPT_ERROR,
	CIF_OPT_MAX,
};

struct cif_raw_master_config {
	uint32_t poll_time;  /* in microseconds */
	uint32_t cycle_time; /* in microseconds */
};

struct cif_raw_port_config {
	/* directory: IN */
	uint8_t port;
	uint8_t slot;

	/* directory: IN */
#define CIF_PORT_FLG_ENABLE      0x01
#define CIF_PORT_FLG_ONE_SHOT    0x02
#define CIF_PORT_FLG_PREEMPT     0x04
#define CIF_PORT_FLG_ALLOW_WRITE 0x08
	unsigned int flags;              /* see CIF_PORT_FLG_* */
	unsigned int async_interval;     /* query interval while port is idle. (in microseconds) */
	unsigned int async_timeout;      /* async timeout. (in microseconds) */
	unsigned int async_bandwidth;    /* async bandwidth. (in bps) */
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
#define CIF_ERR_R_ERROR       BIT(11) /* R_ERROR, redundancy error. One of the A/B frames lost */
#define CIF_ERR_I_ERROR       BIT(12) /* CRC error */
#define CIF_ERR_MAY_LOST      BIT(13) /* XID mismatch, means previous packet may be lost */
#define CIF_ERR_PREEMPT       BIT(14) /* Preempted by R=1 frame */
#define CIF_ERR_PREV_T_ERROR  BIT(15) /* T_ERROR happened in previous operation */
#define CIF_ERR_PREV_R_ERROR  BIT(16) /* R_ERROR happened in previous operation */
#define CIF_ERR_PREV_I_ERROR  BIT(17) /* I_ERROR happened in previous operation */
#define CIF_ERR_PREV_P_ERROR  BIT(18) /* P_ERROR happened in previous operation */
#define CIF_ERR_PREV_MAY_LOST BIT(19) /* Previous packet may be lost */
#define CIF_ERR_PREV_PREEMPT  BIT(20) /* R=1 frame receive in previous operation */
#define CIF_ERR_PREV_INVALID_ASYNC_PACK                                                            \
	BIT(21) /* Invalid async packet happened in previous operation */
#define CIF_ERR_MASK (BIT(22) - 1)
	uint32_t error_mask;
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

#define CIF_IS_SYNC_PORT(port)    ((port < 0x10) || (port >= 0x40 && port < 0x60))
#define CIF_IS_ASYNC_PORT(port)   ((port >= 0x10 && port < 0x40) || (port >= 0x60 && port < 0x80))
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
