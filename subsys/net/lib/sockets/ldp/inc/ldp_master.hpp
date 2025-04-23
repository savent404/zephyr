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
#include "ldp_bc.hpp"
#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
#include <zephyr/kernel.h>
#endif

#include <shared_mutex>

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
template <typename T_mempool, typename T_cache, typename T_mutex, typename T_rwlock>
struct ldp_master: public ldp_basic {

	using bc_mode = bc::bc_mode;

	explicit ldp_master(mcb_if *mcb, work_queue_if *wq) : mcb_(mcb), work_queue_(wq)
	{
		mcb->reset(mcb_if::MCB_ROLE_MASTER);

		/* Create a work queue for sync handler */
		sync_wq_id_ = work_queue_->enqueue(
			[](void *arg, void *_1) {
				auto self = static_cast<ldp_master *>(arg);
				self->sync_handler();
			},
			this, nullptr, cycle_time_);
		wqs_.push_back(sync_wq_id_);

		bc_ = std::make_unique<bc::bc_std>(mcb_->get_bus_pps(), 0.1, 0.2);
	}

	virtual ~ldp_master()
	{
		std::unique_lock lock_wq(*work_queue_);
		std::unique_lock lock(conns_lock);

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

	virtual conn create(bool is_async, const ldp_config *config)
	{
		conn id;

		if (!config) {
			return -LDP_ERR_INVALID;
		}

		std::unique_lock lock_wq(*work_queue_);
		std::unique_lock lock(conns_lock);

		if (is_async) {
			id = create_async(
				reinterpret_cast<const ldp_master_async_config *>(config));
		} else {
			id = create_sync(reinterpret_cast<const ldp_master_sync_config *>(config));
		}
		return id;
	}

	virtual int destroy(conn c)
	{
		std::unique_lock lock_wq(*work_queue_);
		std::unique_lock lock(conns_lock);

		auto sync_it = std::find_if(sync_conns_.begin(), sync_conns_.end(),
					    [c](const sync_conn_ptr &ci) { return ci->id == c; });
		auto async_it = std::find_if(async_conns_.begin(), async_conns_.end(),
					     [c](const async_conn_ptr &ci) { return ci->id == c; });

		if (sync_it == sync_conns_.end() && async_it == async_conns_.end()) {
			return -LDP_ERR_CONN_NOT_FOUND;
		}

		if (sync_it != sync_conns_.end()) {
			auto bc = (*sync_it)->bc;
			if ((*sync_it)->tx_buf) {
				mempool_if::free((*sync_it)->tx_buf);
				(*sync_it)->tx_buf = nullptr;
			}
			if ((*sync_it)->rx_buf) {
				mempool_if::free((*sync_it)->rx_buf);
				(*sync_it)->rx_buf = nullptr;
			}
			bc_->rm_conn(bc);
			sync_conns_.erase(sync_it);
		} else {
			/* call cancel only if wq_id is in the wqs_ */
			auto wq_it = std::find(wqs_.begin(), wqs_.end(), (*async_it)->wq_id);
			auto bc = (*async_it)->bc;
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
			bc_->rm_conn(bc);
			async_conns_.erase(async_it);
		}
		return 0;
	}

	virtual int send(conn c, const uint8_t *buf, uint16_t len)
	{
		std::shared_lock lock(conns_lock);
		auto async_it = std::find_if(async_conns_.begin(), async_conns_.end(),
					     [c](const async_conn_ptr &ci) { return ci->id == c; });
		auto sync_it = std::find_if(sync_conns_.begin(), sync_conns_.end(),
					    [c](const sync_conn_ptr &ci) { return ci->id == c; });
		int ret;

		if (async_it == async_conns_.end() && sync_it == sync_conns_.end()) {
			return -LDP_ERR_CONN_NOT_FOUND;
		}

		if (async_it != async_conns_.end()) {
			ret = send_async(async_it->get(), buf, len);
		} else {
			ret = send_sync(sync_it->get(), buf, len);
		}
		return ret;
	}

	virtual int recv(conn c, uint8_t *buf, uint16_t len)
	{
		std::shared_lock lock(conns_lock);
		auto async_it = std::find_if(async_conns_.begin(), async_conns_.end(),
					     [c](const async_conn_ptr &ci) { return ci->id == c; });
		auto sync_it = std::find_if(sync_conns_.begin(), sync_conns_.end(),
					    [c](const sync_conn_ptr &ci) { return ci->id == c; });
		int read_len;

		if (async_it == async_conns_.end() && sync_it == sync_conns_.end()) {
			return -LDP_ERR_CONN_NOT_FOUND;
		}

		if (async_it != async_conns_.end()) {
			read_len = recv_async((*async_it).get(), buf, len);
		} else {
			read_len = recv_sync((*sync_it).get(), buf, len);
		}

		return read_len;
	}

	virtual uint32_t get_extra_error(conn c)
	{
		std::shared_lock lock(conns_lock);
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
		std::shared_lock lock(conns_lock);
		auto async_it = std::find_if(async_conns_.begin(), async_conns_.end(),
					     [c](const async_conn_ptr &ci) { return ci->id == c; });
		auto sync_it = std::find_if(sync_conns_.begin(), sync_conns_.end(),
					    [c](const sync_conn_ptr &ci) { return ci->id == c; });
		auto fn_clr = [](conn_info_base *ci, uint32_t bits) {
			/* additional error handling */
			if ((ci->last_err & bits) & (1 << LDP_ERR_PREV_ATIMEOUT)) {
				ci->timeout_cnt = ci->timeout_allowed;
				ci->last_err &= ~(1 << LDP_ERR_ATIMEOUT);
			}
			ci->last_err &= ~bits;
		};

		if (async_it == async_conns_.end() && sync_it == sync_conns_.end()) {
			return;
		}

		if (async_it != async_conns_.end()) {
			fn_clr(async_it->get(), err_bits);
		} else {
			fn_clr(sync_it->get(), err_bits);
		}
	}

      protected:
	struct async_buf {
		uint8_t *buf;
		uint16_t len;
	};

	struct conn_info_base {
		conn id;              /* connection id */
		int sid;              /* dest slot id */
		int port;             /* dest port */
		uint32_t cycle;       /* cycle time in microsecond */
		bool preempt;         /* preempt flag */
		int last_err;         /* last error */
		bool flg_one_shot;    /* one shot flag */
		bool flg_hold_on;     /* Hold on flag for a second if no user action triggered */
		bc::conn_ptr bc;      /* bandwidth control handle */
		uint32_t timeout_cnt; /* timeout counter */
		uint32_t timeout_allowed; /* timeout allowed in cycle count */

		uint32_t stat_rx_packet; /* run loop counter */
		T_mutex lock;
		virtual ~conn_info_base() = default;
	};

	struct async_conn_info: public conn_info_base {
		work_queue_if::id wq_id; /* work queue id */
		bool flg_wait_for_rx;    /* flag to activate timeout mechanism */
		bool flg_strong_order;   /* only accept response if rsp.xid == req.rxid+1 */
		uint8_t xid;             /* transaction id */
		int rxid; /* last received transaction id, -1 means no response received */

		std::list<async_buf> rx_bufs; /* received buffers */
		std::list<async_buf> tx_bufs; /* transmit buffers */
	};

	struct sync_conn_info: public conn_info_base {
		bool flg_new_data; /* new data received */
		uint8_t *tx_buf;   /* transmit buffer */
		uint8_t *rx_buf;   /* receive buffer */
		uint16_t tx_len;   /* transmit length */
		uint16_t rx_len;   /* receive length */
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

	conn create_async(const ldp_master_async_config *config)
	{
		auto cfg = static_cast<const ldp_master_async_config *>(config);
		auto ci = std::make_unique<async_conn_info>();
		auto bc = std::make_shared<bc::conn_item>(
			cfg->pps > 0 ? bc_mode::BC_MODE_ASYNC : bc_mode::BC_MODE_ASYNC_AUTO,
			cfg->pps, 0);

		if (!ci) {
			return -LDP_ERR_NOMEM;
		}

		if (!cfg->cycle_time || !cfg->timeout || cfg->cycle_time > cfg->timeout) {
			return -LDP_ERR_INVALID;
		}

		if (conn_exists(cfg->dst, cfg->port)) {
			return -LDP_ERR_CONN_EXIST;
		}

		if (!bc_->add_conn(bc)) {
			return -LDP_ERR_INVALID;
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
		ci->flg_hold_on = cfg->one_shot ? true : false;
		ci->stat_rx_packet = 0;
		ci->last_err = 0;
		ci->xid = LDP_INITIAL_XID;
		ci->rxid = -1;
		ci->bc = bc;
		ci->wq_id = work_queue_->enqueue(
			[](void *arg1, void *arg2) {
				auto self = static_cast<ldp_master *>(arg1);
				auto conn_info = static_cast<async_conn_info *>(arg2);
				self->async_handler(conn_info);
			},
			this, ci.get(), cfg->cycle_time);
		wqs_.push_back(ci->wq_id);
		async_conns_.push_back(std::move(ci));
		return next_id_++;
	}

	conn create_sync(const ldp_master_sync_config *config)
	{
		auto cfg = static_cast<const ldp_master_sync_config *>(config);
		auto ci = std::make_unique<sync_conn_info>();
		auto bc = std::make_shared<bc::conn_item>(bc_mode::BC_MODE_SYNC,
							  1'000'000 / cfg->cycle_time, 1);
		if (!ci || !bc) {
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

		if (!bc_->add_conn(bc)) {
			return -LDP_ERR_INVALID;
		}

		ci->id = next_id_;
		ci->sid = cfg->dst;
		ci->port = cfg->port;
		ci->cycle = cfg->cycle_time;
		ci->last_err = 0;
		ci->preempt = cfg->preempt;
		ci->flg_one_shot = cfg->one_shot;
		ci->flg_hold_on = cfg->one_shot ? true : false;
		ci->stat_rx_packet = 0;
		ci->bc = bc;
		ci->flg_new_data = false;
		ci->timeout_allowed = cfg->timeout / cfg->cycle_time;
		ci->timeout_cnt = ci->timeout_allowed;
		sync_conns_.push_back(std::move(ci));
		return next_id_++;
	}

	int send_async(async_conn_info *ci, const uint8_t *buf, uint16_t len)
	{
		std::unique_lock lock(ci->lock);
		async_buf abuf;

		if (ci->tx_bufs.size() >= LDP_MAX_TX_BUF) {
			return -LDP_ERR_AGAIN;
		}

		if (get_immediate_err(ci->last_err)) {
			return get_immediate_err(ci->last_err);
		}

		abuf.buf = reinterpret_cast<uint8_t *>(mempool_if::alloc(len));
		if (!abuf.buf) {
			return -LDP_ERR_NOMEM;
		}
		abuf.len = len;
		ldp_memcpy::memcpy(abuf.buf, buf, len);
		ci->tx_bufs.push_back(std::move(abuf));

		ci->flg_hold_on = false;
		return len;
	}

	int send_sync(sync_conn_info *ci, const uint8_t *buf, uint16_t len)
	{
		std::unique_lock lock(ci->lock);
		uint8_t *tx_buf = reinterpret_cast<uint8_t *>(mempool_if::alloc(len));
		uint8_t *tx_buf_prev = ci->tx_buf;
		if (!tx_buf) {
			return -LDP_ERR_NOMEM;
		}
		ldp_memcpy::memcpy(tx_buf, buf, len);
		ci->tx_buf = tx_buf;
		ci->tx_len = len;
		ci->flg_hold_on = false;

		if (tx_buf_prev) {
			mempool_if::free(tx_buf_prev);
		}
		return len;
	}

	int recv_async(async_conn_info *ci, uint8_t *buf, uint16_t len)
	{
		std::unique_lock lock(ci->lock);
		async_buf abuf;

		/* user action triggered, reset hold on if oneshot mode is on */
		ci->flg_hold_on = false;

		int err = get_immediate_err(ci->last_err);
		if (err) {
			return err;
		}

		if (ci->rx_bufs.empty()) {
			/* no data received */
			if (!ci->flg_wait_for_rx) {
				/* If timeout is not activated, and no data received, start to count
				 */
				ci->timeout_cnt = ci->timeout_allowed;
				ci->flg_wait_for_rx = true;
			}
			return -LDP_ERR_AGAIN;
		}

		/* data ready, reset timeout */
		ci->flg_wait_for_rx = false;

		abuf = ci->rx_bufs.front();
		if (abuf.len <= len) {
			ldp_memcpy::memcpy(buf, abuf.buf, abuf.len);
			ci->rx_bufs.pop_front();
			mempool_if::free(abuf.buf);
			return abuf.len;
		} else {
			return -LDP_ERR_RX_BUF_TOO_SMALL;
		}
	}

	int recv_sync(sync_conn_info *ci, uint8_t *buf, uint16_t len)
	{
		std::unique_lock lock(ci->lock);
		int err = get_immediate_err(ci->last_err);
		if (err) {
			return err;
		}

		ci->flg_hold_on = false;

		if (ci->rx_len == 0 || !ci->flg_new_data) {
			return -LDP_ERR_AGAIN;
		}

		if (ci->rx_len > len) {
			return -LDP_ERR_RX_BUF_TOO_SMALL;
		}

		ci->flg_new_data = false;

		ldp_memcpy::memcpy(buf, ci->rx_buf, ci->rx_len);
		return ci->rx_len;
	}

	void set_sync_cycle(uint32_t cycle)
	{
		cycle_time_ = cycle;

		work_queue_->reset(sync_wq_id_, cycle_time_);
	}
	/**
	 * @brief Configure MCB port for transmission
	 *
	 * @param port Port number
	 * @param len Transmission length
	 */
	void configure_port(int port, uint16_t len)
	{
		mcb_->config_port(port, true, true, mcb_if::MCB_MAX_FRAME_LEN);
		mcb_->set_tx_len(port, len);
	}

	/**
	 * @brief Manage timeout for connections
	 *
	 * @param ci Connection info pointer (base)
	 * @param data_received Whether data was received successfully
	 * @return true if timeout occurred
	 */
	bool manage_timeout(conn_info_base *ci, bool data_received)
	{
		if (data_received) {
			// If new data received, reset timeout
			ci->timeout_cnt = ci->timeout_allowed;
			ci->last_err &= ~(1 << LDP_ERR_ATIMEOUT);
			return false;
		} else {
			if (ci->timeout_cnt > 1) {
				ci->timeout_cnt--;
				return false;
			} else {
				ci->timeout_cnt = 0;
				ci->last_err |=
					(1 << LDP_ERR_ATIMEOUT) | (1 << LDP_ERR_PREV_ATIMEOUT);
				return true;
			}
		}
	}

	/**
	 * @brief Process bus status and handle errors
	 *
	 * @param ci Connection info pointer
	 * @param status MCB status
	 */
	void process_bus_status(conn_info_base *ci, uint32_t status)
	{
		bool data_timeout = status & mcb_if::MCB_ERR_T_ERR;

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

	/**
	 * @brief Check if connection should be processed
	 *
	 * @param ci Connection info pointer (base)
	 * @return true if connection should be processed
	 */
	bool should_process_connection(conn_info_base *ci)
	{
		// Skip if one shot mode and already received or holding on
		return !(ci->flg_one_shot && (ci->stat_rx_packet || ci->flg_hold_on));
	}

	/**
	 * @brief Process received data for synchronous connection
	 *
	 * @param ci Sync connection info
	 * @param rx_buf Received buffer
	 * @param rx_len Received length
	 * @return true if data was processed successfully
	 */
	bool process_received_data_sync(sync_conn_info *ci, uint8_t *rx_buf, uint16_t rx_len)
	{
		if (!rx_len) {
			return false;
		}

		// Ensure buffer capacity
		if (ci->rx_len < rx_len) {
			if (ci->rx_buf) {
				mempool_if::free(ci->rx_buf);
				ci->rx_buf = nullptr;
			}

			ci->rx_buf = reinterpret_cast<uint8_t *>(mempool_if::alloc(rx_len));
			if (!ci->rx_buf) {
				ci->rx_len = 0;
				ci->last_err |= 1 << LDP_ERR_NOMEM;
				return false;
			}
		}

		cache_if::rmb(); // Make sure buffer is updated
		ldp_memcpy::memcpy(ci->rx_buf, rx_buf, rx_len);
		ci->rx_len = rx_len;
		ci->stat_rx_packet++;
		ci->flg_new_data = true;
		return true;
	}

	/**
	 * @brief Wait for bus operation completion
	 *
	 * @param port Port number
	 * @param data_ready Reference to data_ready flag
	 * @param data_timeout Reference to data_timeout flag
	 * @param port_rejected Reference to port_rejected flag
	 * @return MCB status value
	 */
	uint32_t wait_for_bus_operation(int port, int sid, bool &data_ready, bool &data_timeout,
					bool &port_rejected)
	{
		uint32_t status;
#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
		uint32_t timeout = LDP_POLL_TIMEOUT;
#endif

		do {
			status = mcb_->get_status();
			data_ready = mcb_->has_rx(port);
			data_timeout = status & mcb_if::MCB_ERR_T_ERR;
			port_rejected = status & mcb_if::MCB_ERR_P_ERR;
#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
			if (--timeout == 0) {
				status = mcb_->get_status() | mcb_if::MCB_ERR_T_ERR;
				data_ready = false;
				data_timeout = true;
				port_rejected = false;
				printk("LDP_MASTER: poll timeout, sid=%d, port=%d\n", sid, port);
				k_panic();
			}
			k_usleep(LDP_POLL_INTERVAL);
#endif
		} while (!data_ready && !data_timeout && !port_rejected);

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
		/* FIXME: HW bug, single transmit will trigger timeout and ready at the same time */
		if (data_ready && data_timeout) {
			printk("LDP_MASTER: data_ready and data_timeout at the same time\n");
			data_timeout = 0;
		}
#endif

		return status;
	}

	void sync_handler()
	{
		std::shared_lock bus_lock(conns_lock);

		for (auto &ci : sync_conns_) {
			std::unique_lock conn_lock(ci->lock);

			if (!should_process_connection(ci.get())) {
				continue;
			}

			if (!bc_->try_grant(ci->bc, 1)) {
				/* If no resource available, we should wait for a while */
				continue;
			}

			/* Prepare frame */
			configure_port(ci->port, ci->tx_len);

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
			/* Force to output, HW can't send frame if len==0 */
			if (ci->tx_len == 0) {
				ci->tx_len = 4;
			}
#endif
			uint8_t *tx_buf = mcb_->get_tx_buf(ci->port);
			if (ci->tx_len) {
				ldp_memcpy::memcpy(tx_buf, ci->tx_buf, ci->tx_len);
			}

			cache_if::wmb(); /* Make sure buffer is updated */
			mcb_->tx(ci->port, ci->sid, ci->preempt);

			/* Wait for response */
			bool data_ready, data_timeout, port_rejected;
			uint32_t status = wait_for_bus_operation(ci->port, ci->sid, data_ready, data_timeout,
								 port_rejected);

			/* Process received data */
			uint8_t *rx_buf = mcb_->get_rx_buf(ci->port);
			uint16_t rx_len = mcb_->get_rx_len(ci->port);

			bool data_processed = false;
			if (data_ready && rx_len) {
				data_processed =
					process_received_data_sync(ci.get(), rx_buf, rx_len);
				mcb_->clr_rx(ci->port);
			}

			/* Manage timeout */
			manage_timeout(ci.get(), data_processed);

			/* Process bus status and errors */
			process_bus_status(ci.get(), status);
		}

		/* Refresh all available packets */
		bc_->schedule(cycle_time_);
	}

	/**
	 * @brief Process received data for asynchronous connection
	 *
	 * @param ci Async connection info
	 * @param rx_buf Received buffer
	 * @param rx_len Received length
	 * @param tx_hdr Transmit header
	 * @return Struct with processing results
	 */
	struct AsyncRxResult {
		bool is_invalid_hdr;
		bool is_new_rsp;
		bool is_first_rsp;
		bool is_ordered_rsp;
		bool is_unordered_rsp;
		bool is_ack_rsp;
		bool is_acceptable_rsp;
		bool is_rx_full;
		bool is_memalloc_fail;
		bool is_rx_duplicate;
		bool data_processed;
	};

	AsyncRxResult process_received_data_async(async_conn_info *ci, uint8_t *rx_buf,
						  uint16_t rx_len, volatile ldp_a_header *tx_hdr)
	{
		AsyncRxResult result = {};
		constexpr unsigned hdr_size = sizeof(ldp_a_header);
		volatile ldp_a_header *rx_hdr = reinterpret_cast<volatile ldp_a_header *>(rx_buf);
		uint8_t *rx_data = rx_buf + hdr_size;

		// Validate header
		result.is_invalid_hdr = rx_len < hdr_size || rx_hdr->magic != LDP_MAGIC;
		result.is_first_rsp = !result.is_invalid_hdr && ci->rxid == -1;
		result.is_ordered_rsp = !result.is_invalid_hdr && !result.is_first_rsp &&
					(rx_hdr->xid == ((ci->rxid + 1) & 0xFF));
		result.is_new_rsp = !result.is_invalid_hdr && (rx_hdr->xid != ci->rxid);
		result.is_unordered_rsp = result.is_new_rsp && !result.is_ordered_rsp;
		result.is_ack_rsp = !result.is_invalid_hdr && (rx_hdr->rxid == tx_hdr->xid);
		result.is_acceptable_rsp =
			result.is_new_rsp && (ci->flg_strong_order ? result.is_ordered_rsp : true);
		result.is_rx_full = ci->rx_bufs.size() >= LDP_MAX_RX_BUF;
		result.is_rx_duplicate = !result.is_invalid_hdr && (rx_hdr->xid == ci->rxid);

		// Process data if valid
		if (result.is_acceptable_rsp && !result.is_rx_full) {
			async_buf rx_abuf;
			rx_abuf.len = rx_len - hdr_size;

			if (rx_abuf.len) {
				rx_abuf.buf =
					reinterpret_cast<uint8_t *>(mempool_if::alloc(rx_abuf.len));
				if (!rx_abuf.buf) {
					result.is_memalloc_fail = true;
				} else {
					ldp_memcpy::memcpy(rx_abuf.buf, rx_data, rx_abuf.len);
					ci->rx_bufs.push_back(std::move(rx_abuf));
					ci->stat_rx_packet++;
					result.data_processed = true;
				}
			} else {
				result.data_processed = true;
			}
		}

		if (result.is_new_rsp && !result.is_rx_full) {
			// Record received transaction ID
			ci->rxid = rx_hdr->xid;
		}

		return result;
	}

	void async_handler(async_conn_info *ci)
	{
		std::shared_lock bus_lock(conns_lock);
		std::unique_lock conn_lock(ci->lock);
		bool reset_timeout_flag = false;
		bool comback_to_me = false;

		do {
			if (!bc_->try_grant(ci->bc, 1)) {
				/* If no token available, we should wait for a while */
				break;
			}

			if (!should_process_connection(ci)) {
				break;
			}

			async_buf abuf = {nullptr, 0};
			if (!ci->tx_bufs.empty()) {
				abuf = ci->tx_bufs.front();
			}

			/* Prepare to transmit */
			configure_port(ci->port, abuf.len + sizeof(ldp_a_header));

			uint8_t *tx_buf = mcb_->get_tx_buf(ci->port);
			uint8_t *tx_data = tx_buf + sizeof(ldp_a_header);
			volatile ldp_a_header *tx_hdr =
				reinterpret_cast<volatile ldp_a_header *>(tx_buf);

			/* Fill header */
			tx_hdr->xid = ci->xid;
			tx_hdr->rxid = ci->rxid;
			tx_hdr->magic = LDP_MAGIC;

			/* Copy data if any */
			if (abuf.len) {
				ldp_memcpy::memcpy(tx_data, abuf.buf, abuf.len);
			}

			cache_if::wmb(); /* Make sure buffer is updated */
			mcb_->tx(ci->port, ci->sid, ci->preempt);

			/* Wait for response */
			bool data_ready, data_timeout, p_error;
			uint32_t status =
				wait_for_bus_operation(ci->port, ci->sid, data_ready, data_timeout, p_error);

			AsyncRxResult rx_result = {};

			/* Process received data */
			if (data_ready && !p_error) {
				uint16_t rx_len = mcb_->get_rx_len(ci->port);
				uint8_t *rx_buf = mcb_->get_rx_buf(ci->port);

				cache_if::rmb(); /* Make sure buffer is updated */
				rx_result = process_received_data_async(ci, rx_buf, rx_len, tx_hdr);

				mcb_->clr_rx(ci->port);
			}

			/* Process response and update state */
			if (rx_result.is_new_rsp || rx_result.is_ack_rsp) {
				reset_timeout_flag = true;

				/* Grant more resource if progress made */
				if (bc_->try_grant(ci->bc, 1)) {
					comback_to_me = true;
				}
			}

			if (rx_result.is_ack_rsp) {
				/* Slave accepted the previous transmit data */
				if (ci->tx_bufs.size()) {
					ci->tx_bufs.pop_front();
				}
				if (abuf.buf) {
					mempool_if::free(abuf.buf);
				}
				ci->xid++;
			}

			/* Handle errors */
			process_bus_status(ci, status);

			/* Handle additional async-specific errors */
			ci->last_err = handle_error(rx_result.is_invalid_hdr, ci->last_err,
						    LDP_ERR_INVALID_ASYNC_PACK,
						    LDP_ERR_PREV_INVALID_ASYNC_PACK);
			ci->last_err = handle_error(rx_result.is_unordered_rsp, ci->last_err,
						    LDP_ERR_MAY_LOST, LDP_ERR_PREV_MAY_LOST);
			ci->last_err = handle_error(p_error, ci->last_err, LDP_ERR_P_ERROR,
						    LDP_ERR_PREV_P_ERROR);

			if (rx_result.is_memalloc_fail) {
				ci->last_err |= 1 << LDP_ERR_PREV_RX_DROP_NOMEM;
			}
			if (rx_result.is_rx_full) {
				ci->last_err |= 1 << LDP_ERR_PREV_RX_DROP_FIFO_FULL;
			}
			if (rx_result.is_rx_duplicate) {
				ci->last_err |= 1 << LDP_ERR_PREV_RX_DROP_DUPLICATE;
			}
			if (rx_result.is_invalid_hdr) {
				ci->last_err |= 1 << LDP_ERR_PREV_RX_DROP_INVALID;
			}
		} while (0);

		/* Handle timeout for async connections */
		if (reset_timeout_flag) {
			ci->flg_wait_for_rx = false;
			ci->last_err &= ~(1 << LDP_ERR_ATIMEOUT);
		} else if (ci->flg_wait_for_rx) {
			manage_timeout(ci, false);
		}

		work_queue_->reset(ci->wq_id, comback_to_me ? 0 : ci->cycle);
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
		} else if (!set && last_err & (1 << curr)) {
			last_err &= ~(1 << curr);
			last_err |= (1 << prev);
		}
		return last_err;
	}

      protected:
	sync_conn_list sync_conns_;
	async_conn_list async_conns_;
	T_rwlock conns_lock;

	int32_t next_id_ = 0;
	wq_list wqs_;
	work_queue_if::id sync_wq_id_;

	mcb_if *mcb_;
	work_queue_if *work_queue_;
	unsigned cycle_time_ = 10000;
	std::unique_ptr<bc::bc_std> bc_;

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
	static inline uint32_t LDP_POLL_TIMEOUT = 100; /* 10ms */
	static inline uint32_t LDP_POLL_INTERVAL = 100; /* 100us */
#endif
      public:
#if CONFIG_LDP_MAX_HARQ
	static inline constexpr unsigned LDP_MAX_HARQ = CONFIG_LDP_MAX_HARQ;
#else
	static inline constexpr unsigned LDP_MAX_HARQ = 10;
#endif
	static inline constexpr unsigned LDP_MAX_TX_BUF = LDP_MAX_HARQ;
	static inline constexpr unsigned LDP_MAX_RX_BUF = LDP_MAX_HARQ;

	using cache_if = ldp_cache<T_cache>;
	using mempool_if = ldp_mempool<T_mempool>;
};

} // namespace cif
} // namespace systech
