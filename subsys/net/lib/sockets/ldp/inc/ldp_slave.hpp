/**
 * @file ldp_slave.hpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @brief Slave implement for CIF/LDP stack
 * @version 0.1
 * @date 2025-01-02
 *
 * @copyright Copyright SYSTech Co. 2025
 * @license This project is CLOSED SOURCE, All Rights Reserved
 *
 */
#pragma once

#include "ldp_basic.hpp"

namespace systech
{
namespace cif
{

template <typename T_cache> struct ldp_slave: public ldp_basic {
	explicit ldp_slave(mcb_if *mcb) : mcb_(mcb), next_id_(0)
	{
		mcb->reset(mcb_if::MCB_ROLE_SLAVE);
	}

	virtual ~ldp_slave()
	{
		for (auto &ci : conns_) {
			mcb_->config_port(ci->port, false, false, 0);
		}
	}

	virtual int create(bool is_async, ldp_config *config)
	{
		auto ci = std::make_unique<conn_info>();

		if (!config) {
			return -LDP_ERR_INVALID;
		}

		if (!ci) {
			/* cov: no memory error injection is too boring */
			return -LDP_ERR_NOMEM;
		}

		if (std::find_if(conns_.begin(), conns_.end(), [config](const conn_info_ptr &cip) {
			    return cip->port == config->port;
		    }) != conns_.end()) {
			return -LDP_ERR_CONN_EXIST;
		}

		ci->id = next_id_;
		ci->is_async = is_async;
		ci->port = config->port;

		if (!is_async) {
			/**
			 * @brief open port for sync connection
			 * set max receive length and allow write, set tx buffer to 0
			 */
			auto cfg = static_cast<ldp_slave_sync_config *>(config);

			if (cfg->max_recv_len > mcb_if::MCB_MAX_FRAME_LEN) {
				return -LDP_ERR_INVALID;
			}

			mcb_->set_tx_len(ci->port, 0);
			mcb_->config_port(ci->port, true, cfg->allow_write, cfg->max_recv_len);
		} else {
			/**
			 * @brief open port for async connection
			 * set max receive length, initialize the tx buffer with default
			 * ldp_a_header set rx buffer to 0
			 */
			auto cfg = static_cast<ldp_slave_async_config *>(config);
			uint8_t *tx_buf, *rx_buf;

			if (cfg->max_recv_len > mcb_if::MCB_MAX_FRAME_LEN) {
				return -LDP_ERR_INVALID;
			}

			tx_buf = mcb_->get_tx_buf(ci->port);
			rx_buf = mcb_->get_rx_buf(ci->port);

			/* Initialize tx buffer with default ldp_a_header, and rx header to 0 */
			memset(rx_buf, 0, sizeof(ldp_a_header));
			*(reinterpret_cast<ldp_a_header *>(tx_buf)) = ldp_a_header{};
			mcb_->set_tx_len(ci->port, sizeof(ldp_a_header));
			cache_if::wmb(); /* make sure buffer is updated */
			mcb_->config_port(ci->port, true, true,
					  cfg->max_recv_len + sizeof(ldp_a_header));
		}

		conns_.push_back(std::move(ci));
		return next_id_++;
	}

	virtual int destroy(conn c)
	{
		auto it = std::find_if(conns_.begin(), conns_.end(),
				       [c](const conn_info_ptr &ci) { return ci->id == c; });

		if (it == conns_.end()) {
			return -LDP_ERR_CONN_NOT_FOUND;
		}

		mcb_->config_port((*it)->port, false, false, 0);
		conns_.erase(it);
		return 0;
	}

	virtual int send(conn c, const uint8_t *buf, uint16_t len)
	{
		int port;
		auto it = std::find_if(conns_.begin(), conns_.end(),
				       [c](const conn_info_ptr &ci) { return ci->id == c; });

		if (it == conns_.end()) {
			return -LDP_ERR_CONN_NOT_FOUND;
		}

		if (!buf || !len) {
			return -LDP_ERR_INVALID;
		}

		port = (*it)->port;

		if (!(*it)->is_async) {
			/**
			 * @brief Sync connection send data
			 * copy data into tx buffer and set tx length simply
			 */
			uint8_t *tx_buf = mcb_->get_tx_buf(port);
			unsigned offset = 0;

			memcpy(tx_buf + offset, buf, len);
			cache_if::wmb(); /* make sure buffer is updated */
			mcb_->set_tx_len(port, len);
		} else {
			/**
			 * @brief Async connection send data
			 * copy data and modify the header (see @c ldp_a_header)
			 */
			uint8_t *tx_buf = mcb_->get_tx_buf(port);
			uint8_t *rx_buf = mcb_->get_rx_buf(port);
			uint16_t rx_len = mcb_->get_rx_len(port);
			uint16_t tx_len = mcb_->get_tx_len(port);
			constexpr unsigned offset = sizeof(ldp_a_header);
			auto *tx_hdr = reinterpret_cast<volatile ldp_a_header *>(tx_buf);
			auto *rx_hdr = reinterpret_cast<volatile ldp_a_header *>(rx_buf);
			bool first_tx = tx_len == sizeof(ldp_a_header);
			bool rx_valid = rx_hdr->magic == LDP_MAGIC && rx_len >= offset;
			bool data_acked = rx_valid && rx_hdr->rxid == tx_hdr->xid;

			// wait for previous packet to be acknowledged
			if (!first_tx && !data_acked) {
				return -LDP_ERR_AGAIN;
			}

			memcpy(tx_buf + offset, buf, len);
			tx_hdr->xid++;
			cache_if::wmb(); /* make sure buffer is updated */
			mcb_->set_tx_len(port, len + offset);
		}
		return len;
	}

	virtual int recv(conn c, uint8_t *buf, uint16_t len)
	{
		int rx_len;

		auto it = std::find_if(conns_.begin(), conns_.end(),
				       [c](const conn_info_ptr &ci) { return ci->id == c; });

		if (it == conns_.end()) {
			return -LDP_ERR_CONN_NOT_FOUND;
		}

		if (!(*it)->is_async) {
			uint8_t port = (*it)->port;
			uint8_t *rx_buf;

			if (!mcb_->has_rx(port)) {
				return 0;
			}
			rx_buf = mcb_->get_rx_buf(port);
			rx_len = mcb_->get_rx_len(port);
			if (rx_len <= len) {
				memcpy(buf, rx_buf, rx_len);
				mcb_->clr_rx(port);
			} else {
				return -LDP_ERR_RX_BUF_TOO_SMALL;
			}
		} else {
			constexpr size_t offset = sizeof(ldp_a_header);
			volatile ldp_a_header *rx_hdr, *tx_hdr;
			uint8_t *rx_buf;
			uint8_t port = (*it)->port;
			bool new_data;

			if (!mcb_->has_rx(port)) {
				return 0;
			}

			rx_len = mcb_->get_rx_len(port);
			rx_buf = mcb_->get_rx_buf(port);
			rx_hdr = reinterpret_cast<volatile ldp_a_header *>(rx_buf);
			tx_hdr = reinterpret_cast<volatile ldp_a_header *>(mcb_->get_tx_buf(port));

			cache_if::rmb(); /* make sure buffer is updated */
			if (rx_hdr->magic != LDP_MAGIC || rx_len < offset) {
				return -LDP_ERR_INVALID_ASYNC_PACK;
			}
			rx_buf = rx_buf + offset;
			rx_len = rx_len - offset;
			if (rx_len > len) {
				return -LDP_ERR_RX_BUF_TOO_SMALL;
			}

			new_data = tx_hdr->rxid != rx_hdr->xid;
			if (!new_data) {
				rx_len = -LDP_ERR_AGAIN;
			} else {
				memcpy(buf, rx_buf, rx_len);
				tx_hdr->rxid = rx_hdr->xid;
			}
			mcb_->clr_rx(port);
		}
		return rx_len;
	}

	virtual uint32_t get_extra_error(conn c)
	{
		return 0;
	}

	virtual void clr_extra_error(conn c, uint32_t err_bits)
	{
	}

      protected:
	struct conn_info {
		conn id;
		bool is_async;
		int port;
	};
	using conn_info_ptr = std::unique_ptr<conn_info>;
	using conn_list = std::list<conn_info_ptr>;

      protected:
	mcb_if *mcb_;
	conn_list conns_;
	int32_t next_id_;

	using cache_if = ldp_cache<T_cache>;
};

} // namespace cif
} // namespace systech
