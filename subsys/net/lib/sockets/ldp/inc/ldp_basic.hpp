/**
 * @file ldp_basic.hpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @brief Abstract class for CIF/LDP stack
 * @version 0.1
 * @date 2025-01-02
 *
 * @copyright Copyright SYSTech Co. 2025
 * @license This project is CLOSED SOURCE, All Rights Reserved
 *
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>

#include "ldp_utils.hpp"
#include "mcb.hpp"

namespace systech
{
namespace cif
{

struct ldp_config {
	unsigned port;
};

struct ldp_master_sync_config: ldp_config {
	unsigned dst;        /* destination slave id */
	unsigned cycle_time; /* cycle time in microseconds */
	unsigned timeout;    /* sync timeout in microseconds */
	bool preempt;        /* set R flag, means want to take over the bus */
	bool one_shot;       /* one shot mode, keep sync till receive response */
};

struct ldp_master_async_config: ldp_config {
	unsigned dst;        /* destination slave id */
	unsigned cycle_time; /* cycle time in microseconds */
	unsigned timeout;    /* async timeout in microseconds */
	bool preempt;        /* set R flag, means want to take over the bus */
	bool one_shot;       /* one shot mode, keep sync till receive response */
	bool strong_order;   /* Only accept ordered response (rsp.xid == req.rxid+1) */
	bool is_hw_slave;    /* indicate if the slave is implemented in HW */
	unsigned pps;        /* max packets per second */
};

struct ldp_slave_sync_config: ldp_config {
	unsigned max_recv_len; /* max receive length */
	bool allow_write;      /* allow master write to the port */
};

struct ldp_slave_async_config: ldp_config {
	unsigned max_recv_len; /* max receive length */
};

struct port_stat {
	enum stat_id {
		STAT_ID_BLOCKING_TX_BYTES, /* Indicate the bytes from master waiting for send */
		STAT_ID_BLOCKING_RX_BYTES, /* Indicate the bytes from slave wait for master */
		STAT_ID_BLOCKING_TX_COUNT, /* Indicate the msg packs from master waiting for send */
		STAT_ID_BLOCKING_RX_COUNT, /* Indicate the msg packs from slave wait for master */
		STAT_ID_HIST_XFER_COUNT,   /* Indicate the packs tx count */
		STAT_ID_HIST_RX_COUNT,     /* Indicate the packs rx count */
		STAT_ID_HIST_RX_COUNT_WITH_DATA, /* Indicate the packs rx count with valid data */
		STAT_ID_HIST_RX_COUNT_WITH_ACK, /* Indicate the packs rx count with ack */
		STAT_ID_MAX,
	};
	uint32_t valid_mask;
	uint32_t raw[STAT_ID_MAX]; /* statistic information */

	uint32_t &val(stat_id id)
	{
		static uint32_t dummy = 0;
		if (!is_valid(id)) {
			return dummy;
		}
		return raw[id];
	}

	bool is_valid(stat_id id) const
	{
		if (id >= STAT_ID_MAX) {
			return false;
		}
		return (valid_mask & (1 << id)) != 0;
	}

	void reset(uint32_t mask = 0xFFFF'FFFF)
	{
		valid_mask = mask;
		for (unsigned i = 0; i < STAT_ID_MAX; i++) {
			raw[i] = 0;
		}
	}
};

struct ldp_basic {
	using conn = int32_t;
	virtual ~ldp_basic()
	{
	}

	/**
	 * @brief Create sync/async connection
	 *
	 * @param is_async indicate if the connection is async
	 * @param config  connection configuration. For sync connection, it should
	 * be
	 * @c ldp_master_sync_config or @c ldp_slave_sync_config. For async
	 * connection, it should be @c ldp_master_async_config or @c
	 * ldp_slave_async_config
	 * @return conn connection id, or error code if failed
	 * @retval >= 0 connection id
	 * @return < 0 error code, see @c ldp_error
	 */
	virtual conn create(bool is_async, const ldp_config *config) = 0;

	/**
	 * @brief destroy connection
	 *
	 * @param c connection id
	 * @return int error code
	 * @retval 0 success
	 * @return < 0 error code, see @c ldp_error
	 */
	virtual int destroy(conn c) = 0;

	/**
	 * @brief send data to slave
	 *
	 * @param c connection id
	 * @param buf data buffer
	 * @param len data length
	 * @return int error code
	 * @retval 0 success
	 * @return < 0 error code, see @c ldp_error
	 */
	virtual int send(conn c, const uint8_t *buf, uint16_t len) = 0;

	/**
	 * @brief receive data from slave
	 *
	 * @param c connection id
	 * @param buf data buffer
	 * @param len data length
	 * @return int error code
	 * @retval >= 0 data length
	 * @return < 0 error code, see @c ldp_error
	 */
	virtual int recv(conn c, uint8_t *buf, uint16_t len) = 0;

	/**
	 * @brief get error code
	 *
	 * @param c connection id
	 * @return int error code
	 * @retval 0 success
	 * @return < 0 error code, see @c ldp_error
	 */
	virtual uint32_t get_extra_error(conn c) = 0;

	/**
	 * @brief clear error code
	 *
	 * @param c connection id
	 * @param err_bits error bits to clear. see @c ldp_error
	 */
	virtual void clr_extra_error(conn c, uint32_t err_bits) = 0;

	/**
	 * @brief Set the sync cycle object
	 *
	 * @note only works for master side.
	 * @param cycle cycle time in microseconds
	 */
	virtual void set_sync_cycle(uint32_t cycle) = 0;

	/**
	 * @brief Gather statistic information
	 *
	 * @note only works for master side.
	 * @param c connection id
	 * @param stat pointer to a structure to store statistic information
	 * @return bool true if successful, false otherwise
	 */
	virtual bool get_statistic(conn c, port_stat* stat) = 0;

	/**
	 * @brief get error string
	 *
	 * @param err error code
	 * @return const char* error string
	 */
	inline static constexpr uint32_t LDP_MAGIC = 0xBEFF;

	/* initial xid */
	inline static constexpr uint8_t LDP_INITIAL_XID = 0x80;

	enum ldp_error {
		LDP_ERR_OK,
		LDP_ERR_ATIMEOUT,           /* User Period timeout */
		LDP_ERR_T_ERROR,            /* T_ERROR, no response found */
		LDP_ERR_P_ERROR,            /* P_ERROR, port rejected */
		LDP_ERR_INVALID_ASYNC_PACK, /* Magic number not match, or length too short*/
		LDP_ERR_INVALID,            /* parameter invalid */
		LDP_ERR_AGAIN,              /* Previous operation not completed, try it later */
		LDP_ERR_CONN_NOT_FOUND,     /* Connection id not found */
		LDP_ERR_NOMEM,              /* No more memory */
		LDP_ERR_RX_BUF_TOO_SMALL,   /* Rx buffer too small */
		LDP_ERR_CONN_EXIST,         /* Connection already exists */
		__LDP_ERR_IMMEDIATE_MAX,    /* Those errors should be handled immediately by
					       the caller */

		LDP_ERR_R_ERROR,       /* R_ERROR, redundancy error. One of the frames lost */
		LDP_ERR_I_ERROR,       /* recv invalid frame */
		LDP_ERR_MAY_LOST,      /* XID mismatch, means previous packet may be lost */
		LDP_ERR_PREEMPT,       /* Preempted by R=1 frame */
		LDP_ERR_PREV_T_ERROR,  /* T_ERROR happened in previous operation */
		LDP_ERR_PREV_R_ERROR,  /* R_ERROR happened in previous operation */
		LDP_ERR_PREV_I_ERROR,  /* I_ERROR happened in previous operation */
		LDP_ERR_PREV_P_ERROR,  /* P_ERROR happened in previous operation */
		LDP_ERR_PREV_MAY_LOST, /* Previous packet may be lost */
		LDP_ERR_PREV_PREEMPT,  /* R=1 frame receive in previous operation */
		LDP_ERR_PREV_INVALID_ASYNC_PACK, /* Invalid async packet happened in
						    previous operation */
		LDP_ERR_PREV_RX_DROP_NOMEM,      /* Drop packet due to no memory */
		LDP_ERR_PREV_RX_DROP_FIFO_FULL,  /* Drop packet due to rx fifo full */
		LDP_ERR_PREV_RX_DROP_DUPLICATE,  /* Drop packet due to duplicate */
		LDP_ERR_PREV_RX_DROP_INVALID,    /* Drop packet due to invalid */
		LDP_ERR_PREV_ATIMEOUT,           /* Previous operation timeout */
		LDP_ERR_MAX,
	};
	static_assert(LDP_ERR_MAX < 32, "Too many error codes");
	struct ldp_a_header {
		uint32_t xid: 8;  /* transfer xid */
		uint32_t rxid: 8; /* receiver xid */
		uint32_t magic: 16;
		explicit ldp_a_header(const ldp_a_header &) = default;
		explicit ldp_a_header(uint32_t val)
			: xid(val & 0xFF), rxid((val >> 8) & 0xFF), magic((val >> 16) & 0xFFFF)
		{
		}
		explicit ldp_a_header() : xid(LDP_INITIAL_XID), rxid(0), magic(LDP_MAGIC)
		{
		}
		uint32_t operator()() const
		{
			return (xid | (rxid << 8) | (magic << 16));
		}
	};
	static_assert(sizeof(ldp_a_header) == 4, "ldp_a_header size mismatch");

	inline static constexpr uint16_t LDP_USR_SYNC_MAX_LENGTH = 0x800;

	inline static constexpr uint16_t LDP_USR_ASYNC_MAX_LEN =
		(LDP_USR_SYNC_MAX_LENGTH - sizeof(ldp_a_header));

protected:
	/**
	 * @brief General error handler
	 * If set is true, set the error bit.
	 * If set is false, clear the error bit, and set the previous error bit.
	 *
	 * @param set true to set error bit, false to clear error bit
	 * @param last_err last error mask
	 * @param curr current error bit
	 * @param prev previous error bit
	 * @return int updated error mask
	 */
	uint32_t handle_error(bool set, uint32_t last_err, ldp_error curr, ldp_error prev)
	{
		if (set) {
			last_err |= (1 << curr);
		} else if (!set && last_err & (1 << curr)) {
			last_err &= ~(1 << curr);
			last_err |= (1 << prev);
		}
		return last_err;
	}


};

} // namespace cif
} // namespace systech
