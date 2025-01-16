/**
 * @file ldp_master.hpp
 * @author Liao YuanKai (savent_gate@outlook.com)
 * @brief Master implement for CIF/LDP stack
 * @version 0.1
 * @date 2025-01-02
 *
 * @copyright Copyright SYSTech Co. 2025
 * @license This project is CLOSED SOURCE, All Rights Reserved
 *
 */
#pragma once

#include "ldp_basic.hpp"
#include <zephyr/kernel.h>

namespace systech
{
namespace cif
{

/**
 * @brief LDP master abstract class
 *
 * @tparam T_mempool abstract memory pool
 * @tparam T_cache   abstract cache interface
 */
template <typename T_mempool, typename T_cache> struct ldp_master: public ldp_basic {
	explicit ldp_master(mcb_if *mcb, work_queue_if *wq)
		: next_id_(0), mcb_(mcb), work_queue_(wq)
	{
		mcb->reset(mcb_if::MCB_ROLE_MASTER);

		/* Create a work queue for sync handler */
		sync_wq_id_ = work_queue_->enqueue(
			[](void *arg, void *_1) {
				auto self = static_cast<ldp_master *>(arg);
				self->sync_handler();
			},
			this, nullptr, 10000);
		wqs_.push_back(sync_wq_id_);
	}

	virtual ~ldp_master()
	{
		for (auto &id : wqs_) {
			work_queue_->cancel(id);
		}

		for (auto &ci : sync_conns_) {
			if (ci->tx_buf) {
				mempool_if::free(ci->tx_buf);
				ci->tx_buf = nullptr;
			}
			if (ci->rx_buf) {
				mempool_if::free(ci->rx_buf);
				ci->rx_buf = nullptr;
			}
		}

		for (auto &ci : async_conns_) {
			for (auto &abuf : ci->tx_bufs) {
				mempool_if::free(abuf.buf);
				abuf.buf = nullptr;
			}
			for (auto &abuf : ci->rx_bufs) {
				mempool_if::free(abuf.buf);
				abuf.buf = nullptr;
			}
		}
	}

	int set_mcb_config(const ldp_mcb_config &cfg)
	{
		if (!cfg.poll_time) {
			return -LDP_ERR_INVALID;
		}
		mcb_->set_poll_time(cfg.poll_time);
		return 0;
	}

	virtual conn create(bool is_async, ldp_config *config)
	{

		if (!config) {
			return -LDP_ERR_INVALID;
		}

		if (is_async) {
			auto cfg = static_cast<ldp_master_async_config *>(config);
			auto ci = std::make_unique<async_conn_info>();

			if (!ci) {
				return -LDP_ERR_NOMEM;
			}
			if (!cfg->cycle_time || !cfg->timeout || cfg->cycle_time > cfg->timeout) {
				return -LDP_ERR_INVALID;
			}

			if (conn_exists(cfg->dst, cfg->port)) {
				return -LDP_ERR_CONN_EXIST;
			}

			ci->id = next_id_;
			ci->sid = cfg->dst;
			ci->port = cfg->port;
			ci->cycle = cfg->cycle_time;
			ci->preempt = cfg->preempt;
			ci->timeout_allowed = cfg->timeout / cfg->cycle_time;
			ci->timeout_cnt = ci->timeout_allowed;
			ci->flg_wait_for_rx = false;
			ci->flg_strong_order = cfg->strong_order;
			ci->flg_one_shot = cfg->one_shot;
			ci->stat_rx_packet = 0;
			ci->last_err = 0;
			ci->xid = LDP_INITIAL_XID;
			ci->rxid = -1;
			ci->wq_id = work_queue_->enqueue(
				[](void *arg1, void *arg2) {
					auto self = static_cast<ldp_master *>(arg1);
					auto conn_info = static_cast<async_conn_info *>(arg2);
					self->async_handler(conn_info);
				},
				this, ci.get(), cfg->cycle_time);
			wqs_.push_back(ci->wq_id);
			async_conns_.push_back(std::move(ci));
		} else {
			auto cfg = static_cast<ldp_master_sync_config *>(config);
			auto ci = std::make_unique<sync_conn_info>();

			if (!ci) {
				return -LDP_ERR_NOMEM;
			}
			if (!cfg->cycle_time) {
				return -LDP_ERR_INVALID;
			}

			if (conn_exists(cfg->dst, cfg->port)) {
				return -LDP_ERR_CONN_EXIST;
			}

			/* make sure all the connection's cycle time is the same. cause we use
			 * only one work queue for all sync connections */
			if (!sync_conns_.empty()) {
				if (cfg->cycle_time != sync_conns_.front()->cycle) {
					return -LDP_ERR_INVALID;
				}
			}

			ci->id = next_id_;
			ci->sid = cfg->dst;
			ci->port = cfg->port;
			ci->cycle = cfg->cycle_time;
			ci->last_err = 0;
			ci->preempt = cfg->preempt;
			ci->flg_one_shot = cfg->one_shot;
			ci->stat_rx_packet = 0;
			sync_conns_.push_back(std::move(ci));
		}
		return next_id_++;
	}

	virtual int destroy(conn c)
	{
		auto sync_it = std::find_if(sync_conns_.begin(), sync_conns_.end(),
					    [c](const sync_conn_ptr &ci) { return ci->id == c; });
		auto async_it = std::find_if(async_conns_.begin(), async_conns_.end(),
					     [c](const async_conn_ptr &ci) { return ci->id == c; });

		if (sync_it == sync_conns_.end() && async_it == async_conns_.end()) {
			return -LDP_ERR_CONN_NOT_FOUND;
		}

		if (sync_it != sync_conns_.end()) {
			if ((*sync_it)->tx_buf) {
				mempool_if::free((*sync_it)->tx_buf);
				(*sync_it)->tx_buf = nullptr;
			}
			if ((*sync_it)->rx_buf) {
				mempool_if::free((*sync_it)->rx_buf);
				(*sync_it)->rx_buf = nullptr;
			}
			sync_conns_.erase(sync_it);
		} else {
			/* call cancel only if wq_id is in the wqs_ */
			auto wq_it = std::find(wqs_.begin(), wqs_.end(), (*async_it)->wq_id);
			if (wq_it != wqs_.end()) {
				work_queue_->cancel((*async_it)->wq_id);
				wqs_.remove((*async_it)->wq_id);
			}
			for (auto &abuf : (*async_it)->tx_bufs) {
				mempool_if::free(abuf.buf);
				abuf.buf = nullptr;
			}
			for (auto &abuf : (*async_it)->rx_bufs) {
				mempool_if::free(abuf.buf);
				abuf.buf = nullptr;
			}
			async_conns_.erase(async_it);
		}
		return 0;
	}

	virtual int send(conn c, const uint8_t *buf, uint16_t len)
	{
		auto async_it = std::find_if(async_conns_.begin(), async_conns_.end(),
					     [c](const async_conn_ptr &ci) { return ci->id == c; });
		auto sync_it = std::find_if(sync_conns_.begin(), sync_conns_.end(),
					    [c](const sync_conn_ptr &ci) { return ci->id == c; });

		if (async_it == async_conns_.end() && sync_it == sync_conns_.end()) {
			return -LDP_ERR_CONN_NOT_FOUND;
		}

		if (async_it != async_conns_.end()) {
			async_buf abuf;

			abuf.buf = reinterpret_cast<uint8_t *>(mempool_if::alloc(len));
			if (!abuf.buf) {
				return -LDP_ERR_NOMEM;
			}
			abuf.len = len;
			memcpy(abuf.buf, buf, len);
			(*async_it)->tx_bufs.push_back(std::move(abuf));
		} else {
			uint8_t *tx_buf = reinterpret_cast<uint8_t *>(mempool_if::alloc(len));
			uint8_t *tx_buf_prev = (*sync_it)->tx_buf;
			if (!tx_buf) {
				return -LDP_ERR_NOMEM;
			}
			memcpy(tx_buf, buf, len);
			(*sync_it)->tx_buf = tx_buf;
			(*sync_it)->tx_len = len;

			if (tx_buf_prev) {
				mempool_if::free(tx_buf_prev);
			}
		}
		return len;
	}

	virtual int recv(conn c, uint8_t *buf, uint16_t len)
	{
		auto async_it = std::find_if(async_conns_.begin(), async_conns_.end(),
					     [c](const async_conn_ptr &ci) { return ci->id == c; });
		auto sync_it = std::find_if(sync_conns_.begin(), sync_conns_.end(),
					    [c](const sync_conn_ptr &ci) { return ci->id == c; });
		int read_len;

		if (async_it == async_conns_.end() && sync_it == sync_conns_.end()) {
			return -LDP_ERR_CONN_NOT_FOUND;
		}

		if (async_it != async_conns_.end()) {
			async_buf abuf;

			int err = get_immediate_err((*async_it)->last_err);
			if (err) {
				return err;
			}

			if ((*async_it)->rx_bufs.empty()) {
				/* no data received */
				if (!(*async_it)->flg_wait_for_rx) {
					/* If timeout is not activated, and no data received, start
					 * to count
					 */
					(*async_it)->timeout_cnt = (*async_it)->timeout_allowed;
					(*async_it)->flg_wait_for_rx = true;
				}
				return -LDP_ERR_AGAIN;
			}

			abuf = (*async_it)->rx_bufs.front();
			if (abuf.len <= len) {
				memcpy(buf, abuf.buf, abuf.len);
				(*async_it)->rx_bufs.pop_front();
				mempool_if::free(abuf.buf);
				read_len = abuf.len;
			} else {
				return -LDP_ERR_RX_BUF_TOO_SMALL;
			}

		} else {
			int err = get_immediate_err((*sync_it)->last_err);
			if (err) {
				return err;
			}

			if ((*sync_it)->rx_len == 0) {
				return -LDP_ERR_AGAIN;
			}

			if ((*sync_it)->rx_len > len) {
				return -LDP_ERR_RX_BUF_TOO_SMALL;
			}

			memcpy(buf, (*sync_it)->rx_buf, (*sync_it)->rx_len);
			read_len = (*sync_it)->rx_len;
		}

		return read_len;
	}

	virtual uint32_t get_extra_error(conn c)
	{
		auto async_it = std::find_if(async_conns_.begin(), async_conns_.end(),
					     [c](const async_conn_ptr &ci) { return ci->id == c; });
		auto sync_it = std::find_if(sync_conns_.begin(), sync_conns_.end(),
					    [c](const sync_conn_ptr &ci) { return ci->id == c; });
		if (async_it == async_conns_.end() && sync_it == sync_conns_.end()) {
			return (1 << LDP_ERR_CONN_NOT_FOUND);
		}
		if (async_it != async_conns_.end()) {
			return (*async_it)->last_err;
		} else {
			return (*sync_it)->last_err;
		}
	}

	virtual void clr_extra_error(conn c, uint32_t err_bits)
	{
		auto async_it = std::find_if(async_conns_.begin(), async_conns_.end(),
					     [c](const async_conn_ptr &ci) { return ci->id == c; });
		auto sync_it = std::find_if(sync_conns_.begin(), sync_conns_.end(),
					    [c](const sync_conn_ptr &ci) { return ci->id == c; });
		if (async_it == async_conns_.end() && sync_it == sync_conns_.end()) {
			return;
		}

		if (async_it != async_conns_.end()) {
			(*async_it)->last_err &= ~err_bits;
		} else {
			(*sync_it)->last_err &= ~err_bits;
		}
	}

      protected:
	struct async_buf {
		uint8_t *buf;
		uint16_t len;
	};

	struct async_conn_info {
		conn id;                 /* connection id */
		int sid;                 /* dest slot id */
		int port;                /* dest port */
		work_queue_if::id wq_id; /* work queue id */

		uint32_t cycle;           /* cycle time in microsecond */
		bool preempt;             /* preempt flag (R flag) */
		uint32_t timeout_cnt;     /* timeout counter */
		uint32_t timeout_allowed; /* timeout allowed in cycle count */
		uint32_t stat_rx_packet;  /* run loop counter */
		bool flg_one_shot;        /* one shot flag */
		bool flg_wait_for_rx;     /* This is the flag to activate timeout mechanism
					 Rising edge means reset timeout, activate timer
					 Falling edge means deactivate timer */
		bool flg_strong_order;    /* only accept response if rsp.xid == req.rxid+1 */
		uint8_t xid;              /* transaction id */
		int rxid;                 /* last received transaction id, -1 means no
					     response received */
		int last_err;             /* last error */

		std::list<async_buf> rx_bufs; /* received buffers */
		std::list<async_buf> tx_bufs; /* transmit buffers */
	};

	struct sync_conn_info {
		conn id;                 /* connection id */
		int sid;                 /* dest slot id */
		int port;                /* dest port */
		uint32_t cycle;          /* cycle time in microsecond */
		bool preempt;            /* preempt flag (R flag) */
		int last_err;            /* last error */
		uint32_t stat_rx_packet; /* run loop counter */
		bool flg_one_shot;       /* one shot flag */

		uint8_t *tx_buf; /* transmit buffer */
		uint8_t *rx_buf; /* receive buffer */
		uint16_t tx_len; /* transmit length */
		uint16_t rx_len; /* receive length */
	};

	using sync_conn_ptr = std::unique_ptr<sync_conn_info>;
	using async_conn_ptr = std::unique_ptr<async_conn_info>;
	using sync_conn_list = std::list<sync_conn_ptr>;
	using async_conn_list = std::list<async_conn_ptr>;
	using wq_list = std::list<work_queue_if::id>;

	/**
	 * @brief Get the immediate error
	 *
	 * @param err_mask error mask (last_err), the 0 bit is reserved.
	 * @return int error code, see @c ldp_error
	 * @retval 0 no error
	 * @return < 0 error code
	 */
	int get_immediate_err(uint32_t err_mask)
	{
		constexpr int error_mask = (1 << __LDP_ERR_IMMEDIATE_MAX) - 1;

		if (error_mask & err_mask) {
			return -__builtin_ctz(err_mask);
		}
		return 0;
	}

	/**
	 * @brief Helper function to handle connection exists error
	 *
	 * @param dst dest sid
	 * @param port dest port
	 * @return true connection exists
	 * @return false connection not exists
	 */
	bool conn_exists(uint8_t dst, uint8_t port)
	{
		for (auto &ci : sync_conns_) {
			if (ci->port == port && ci->sid == dst) {
				return true;
			}
		}
		for (auto &ci : async_conns_) {
			if (ci->port == port && ci->sid == dst) {
				return true;
			}
		}
		return false;
	}

	void sync_handler()
	{
		for (auto &ci : sync_conns_) {
			uint8_t *rx_buf, *tx_buf;
			uint16_t rx_len;
			uint32_t status;
			bool data_ready, data_timeout, port_rejected;

			if (ci->flg_one_shot && ci->stat_rx_packet) {
				continue;
			}

			/* prepare frame */
			mcb_->config_port(ci->port, true, true, mcb_if::MCB_MAX_FRAME_LEN);

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
			/* force to output, HW can't send frame if len==0 */
			/* FIXME: HW bug, if tx_len is 0, the function will not work */
			if (ci->tx_len == 0) {
				ci->tx_len = 4;
			}
#endif
			mcb_->set_tx_len(ci->port, ci->tx_len);
			tx_buf = mcb_->get_tx_buf(ci->port);
			if (ci->tx_len) {
				memcpy(tx_buf, ci->tx_buf, ci->tx_len);
			}
			cache_if::wmb(); /* make sure buffer is updated */
			mcb_->tx(ci->port, ci->sid, ci->preempt);

			/* wait for response */
			do {
				status = mcb_->get_status();
				data_ready = mcb_->has_rx(ci->port);
				data_timeout = status & mcb_if::MCB_ERR_T_ERR;
				port_rejected = status & mcb_if::MCB_ERR_P_ERR;
			} while (!data_ready && !data_timeout && !port_rejected);

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
			/* FIXME: HW bug, single transmit will trigger timeout and ready at the same
			 * time
			 */
			if (data_ready) {
				data_timeout = 0;
			}
#endif

			/* handle received frame */
			if (data_ready) {
				rx_buf = mcb_->get_rx_buf(ci->port);
				rx_len = mcb_->get_rx_len(ci->port);

				/* if rx buffer is not enough, reallocate */
				if (ci->rx_len < rx_len) {
					if (ci->rx_buf) {
						mempool_if::free(ci->rx_buf);
						ci->rx_buf = nullptr;
					}
					ci->rx_buf = reinterpret_cast<uint8_t *>(
						mempool_if::alloc(rx_len));
				}
				cache_if::rmb(); /* make sure buffer is updated */
				memcpy(ci->rx_buf, rx_buf, rx_len);
				ci->rx_len = rx_len;
				ci->stat_rx_packet++;
				mcb_->clr_rx(ci->port);
			}

			/* General error handling */
			ci->last_err = handle_error(data_timeout, ci->last_err, LDP_ERR_T_ERROR,
						    LDP_ERR_PREV_T_ERROR);
			ci->last_err = handle_error(status & mcb_if::MCB_ERR_R_ERR, ci->last_err,
						    LDP_ERR_R_ERROR, LDP_ERR_PREV_R_ERROR);
			ci->last_err = handle_error(status & mcb_if::MCB_ERR_I_ERR, ci->last_err,
						    LDP_ERR_I_ERROR, LDP_ERR_PREV_I_ERROR);
			ci->last_err = handle_error(status & mcb_if::MCB_ERR_P_ERR, ci->last_err,
						    LDP_ERR_P_ERROR, LDP_ERR_PREV_P_ERROR);
			ci->last_err = handle_error(status & mcb_if::MCB_ERR_PREEMPT, ci->last_err,
						    LDP_ERR_PREEMPT, LDP_ERR_PREV_PREEMPT);
			mcb_->clr_status(status);
		}
	}

	void async_handler(async_conn_info *ci)
	{
		int should_continue = 1;    /* Assume no progress, 1 slot is enough */
		const int max_continue = 2; /* If we have some progress(tx acked, new rx
		data), we should give appropriate time to wait for slave response */
		bool reset_timeout_flag = false;

		while (should_continue--) {
			uint8_t *tx_buf;
			uint8_t *tx_data;
			uint32_t status;
#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
			/* HW bug, if payload is empty, the function will not work */
			async_buf abuf = {(uint8_t *)"empt", 4};
#else
			async_buf abuf = {nullptr, 0};
#endif
			volatile ldp_a_header *tx_hdr;
			bool data_ready, data_timeout, p_error;
			bool is_invalid_rsp = false; /* header is craped */
			bool is_new_rsp = false;     /* rsp.xid != req.rxid */
			bool is_ordered_rsp = false; /* rsp.xid = req.rxid+1*/
			bool is_ack_rsp = false;     /* rsp.rxid = req.xid */
			bool is_first_rsp = false;   /* first rsp */
			bool is_unordered_rsp =
				false; /* new rsp but not ordered(including first rsp)*/
			bool is_accept_rsp = false; /* accept rsp */

			if (ci->flg_one_shot && ci->stat_rx_packet) {
				/* FIXME: This is not a good way to handle one shot connection
				 * because we must deal it till the connection is destroyed */
				/* this is for syncing rxid to slave. To notice slave that
				 * master received the response */
				break;
			}

			/* prefetch tx buffer */
			if (!ci->tx_bufs.empty()) {
				abuf = ci->tx_bufs.front();
			}

			/* prepare to tx, assume port is reused by other connection. so we need to
			 * reconfig at every tx */
			mcb_->config_port(ci->port, true, true, mcb_if::MCB_MAX_FRAME_LEN);
#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
			/* FIXME: This is a hardware bug, if update tx_len from 13 to 8, the
			 * function will not work. So we can only update tx_len for once.
			 */
			if (mcb_->get_tx_len(ci->port) == 0) {
				mcb_->set_tx_len(ci->port, abuf.len + sizeof(ldp_a_header));
			}
#else
			mcb_->set_tx_len(ci->port, abuf.len + sizeof(ldp_a_header));
#endif
			tx_buf = mcb_->get_tx_buf(ci->port);
			tx_data = tx_buf + sizeof(ldp_a_header);
			tx_hdr = reinterpret_cast<volatile ldp_a_header *>(tx_buf);
			tx_hdr->xid = ci->xid;
			tx_hdr->rxid = ci->rxid;
			tx_hdr->magic = LDP_MAGIC;
			if (abuf.len) {
				memcpy(tx_data, abuf.buf, abuf.len);
			}
			cache_if::wmb(); /* make sure buffer is updated */
			mcb_->tx(ci->port, ci->sid, ci->preempt);
			/* wait for response */
			do {
				status = mcb_->get_status();
				data_ready = mcb_->has_rx(ci->port);
				data_timeout = status & mcb_if::MCB_ERR_T_ERR;
			} while (!data_ready && !data_timeout);

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
			/* FIXME: HW bug, single transmit will trigger timeout and ready at the same
			 * time
			 */
			if (data_ready) {
				data_timeout = 0;
			}
#endif

			p_error = status & mcb_if::MCB_ERR_P_ERR;

			/* handle received frame */
			if (data_ready && !p_error) {
				uint8_t *rx_buf = mcb_->get_rx_buf(ci->port);
				uint16_t rx_len = mcb_->get_rx_len(ci->port);
				constexpr unsigned hdr_size = sizeof(ldp_a_header);
				volatile ldp_a_header *rx_hdr =
					reinterpret_cast<volatile ldp_a_header *>(rx_buf);
				uint8_t *rx_data = rx_buf + hdr_size;

				cache_if::rmb(); /* make sure buffer is updated */
				is_invalid_rsp = rx_len < hdr_size || rx_hdr->magic != LDP_MAGIC;
				is_first_rsp = !is_invalid_rsp && ci->rxid == -1;
				is_ordered_rsp = !is_invalid_rsp && !is_first_rsp &&
						 (rx_hdr->xid == ((ci->rxid + 1) & 0xFF));
				is_new_rsp = (!is_invalid_rsp && (rx_hdr->xid != ci->rxid));
				is_unordered_rsp = is_new_rsp && !is_ordered_rsp;
				is_ack_rsp = !is_invalid_rsp && (rx_hdr->rxid == tx_hdr->xid);
				is_accept_rsp = is_new_rsp &&
						(ci->flg_strong_order ? is_ordered_rsp : true);

				if (is_accept_rsp) {
					/* If strong order is set, only accept ordered response */
					async_buf rx_abuf;

					rx_abuf.len = rx_len - hdr_size;
					if (rx_abuf.len) {
						rx_abuf.buf = reinterpret_cast<uint8_t *>(
							mempool_if::alloc(rx_abuf.len));
						memcpy(rx_abuf.buf, rx_data, rx_abuf.len);
						ci->rx_bufs.push_back(std::move(rx_abuf));
					}
				}

				if (is_new_rsp) {
					/* new data received (even if is wrong data) */
					reset_timeout_flag = true;
					should_continue = max_continue;

					/* ready for ack */
					ci->rxid = rx_hdr->xid;
				}

				if (is_ack_rsp) {
					/* Slave accepted the previous transmit data.
					 * Drop the tx buffer then.
					 */
					ci->tx_bufs.pop_front();
					if (abuf.buf) {
						mempool_if::free(abuf.buf);
					}
					ci->xid++;
				}
				mcb_->clr_rx(ci->port);
			}

			/* General error handling */
			ci->last_err = handle_error(data_timeout, ci->last_err, LDP_ERR_T_ERROR,
						    LDP_ERR_PREV_T_ERROR);
			ci->last_err = handle_error(status & mcb_if::MCB_ERR_R_ERR, ci->last_err,
						    LDP_ERR_R_ERROR, LDP_ERR_PREV_R_ERROR);
			ci->last_err = handle_error(status & mcb_if::MCB_ERR_I_ERR, ci->last_err,
						    LDP_ERR_I_ERROR, LDP_ERR_PREV_I_ERROR);
			ci->last_err = handle_error(p_error, ci->last_err, LDP_ERR_P_ERROR,
						    LDP_ERR_PREV_P_ERROR);
			ci->last_err = handle_error(status & mcb_if::MCB_ERR_PREEMPT, ci->last_err,
						    LDP_ERR_PREEMPT, LDP_ERR_PREV_PREEMPT);
			ci->last_err = handle_error(is_invalid_rsp, ci->last_err,
						    LDP_ERR_INVALID_ASYNC_PACK,
						    LDP_ERR_PREV_INVALID_ASYNC_PACK);
			ci->last_err = handle_error(is_unordered_rsp, ci->last_err,
						    LDP_ERR_MAY_LOST, LDP_ERR_PREV_MAY_LOST);
			if (is_accept_rsp) {
				ci->stat_rx_packet++;
			}
			mcb_->clr_status(status);
		}

		/* Count if user wants some data from slave */
		if (reset_timeout_flag) {
			ci->flg_wait_for_rx = false;
		} else if (ci->flg_wait_for_rx) {
			if (--ci->timeout_cnt == 0) {
				ci->last_err |= (1 << LDP_ERR_ATIMEOUT);
			}
		}
		work_queue_->reset(ci->wq_id, ci->cycle);
	}

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
		} else if (last_err & (1 << curr)) {
			last_err &= ~(1 << curr);
			last_err |= (1 << prev);
		}
		return last_err;
	}

      protected:
	sync_conn_list sync_conns_;
	async_conn_list async_conns_;
	int32_t next_id_;
	wq_list wqs_;
	work_queue_if::id sync_wq_id_;

	mcb_if *mcb_;
	work_queue_if *work_queue_;

	using cache_if = ldp_cache<T_cache>;
	using mempool_if = ldp_mempool<T_mempool>;
};

} // namespace cif
} // namespace systech
