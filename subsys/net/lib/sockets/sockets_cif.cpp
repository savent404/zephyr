/**
 * @file sockets_cif.cpp
 * @author Liao YuanKai (savent_gate@outlook.com)
 * @brief CIF sockets support
 * @version 0.1
 * @date 2025-01-05
 *
 * @copyright Copyright (c) 2025 SYSTech Co.
 * @license This project is CLOSED SOURCE, All Rights Reserved
 *
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_sock_cif, CONFIG_NET_SOCKETS_LOG_LEVEL);

#include <zephyr/sys/fdtable.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/socketcif.h>
#include <zephyr/net/socket.h>
#include "sockets_internal.h"
#include <memory>
#include <array>
#include <map>

#include "ldp/ldp_port_ze.hpp"

using namespace systech::cif;

struct cif_master_data {
	using ldp_wq = zephyr::ldp_wq;
	using ldp_mcb_impl = zephyr::ldp_mcb_impl;
	ldp_wq wq;
	ldp_mcb_impl mcb[CIF_BUS_MAX];
	bool occupied[CIF_BUS_MAX];

	/* wq background thread */
	k_tid_t wq_tid;
	k_thread wq_thread;
	k_thread_stack_t wq_stack[9216];
	k_mutex x_lock_;
};

struct cif_sock_data {
	using ldp_ptr_t = ldp_basic *;
	using mcb_ptr_t = mcb_if *;
	using conn_t = struct {
		ldp_basic::conn conn_id;
		cif_raw_port_config config;
	};
	struct conn_idx {
		explicit conn_idx(uint16_t _idx) : idx(_idx)
		{
		}
		explicit conn_idx(uint8_t _port, uint8_t _slot) : port(_port), slot(_slot)
		{
		}
		union {
			struct {
				uint8_t port;
				uint8_t slot;
			};
			uint16_t idx;
		};
		/* for sorting */
		bool operator<(const conn_idx &rhs) const
		{
			return idx < rhs.idx;
		}
		bool operator==(const conn_idx &rhs) const
		{
			return idx == rhs.idx;
		}
	};
	using conn_list_t = std::map<conn_idx, conn_t>;

	ldp_ptr_t ldp; /* ldp instance */
	mcb_ptr_t mcb;
	conn_list_t conns;                /* connection list */
	cif_raw_master_config master_cfg; /* only for master */
};

using conn_idx = cif_sock_data::conn_idx;
using conn_t = cif_sock_data::conn_t;

#define MCB_DEV_LOW  DEVICE_DT_GET(DT_ALIAS(mcbl))
#define MCB_DEV_HIGH DEVICE_DT_GET(DT_ALIAS(mcbh))

static cif_master_data cif_data = {
	.mcb = {zephyr::ldp_mcb_impl{MCB_DEV_LOW}, zephyr::ldp_mcb_impl{MCB_DEV_HIGH}},
	.occupied = {0},
};

#ifdef __cplusplus
extern "C" {
#endif

static int cif_sock_close(struct net_context *ctx);
static int cif_sock_bind(struct net_context *ctx, const struct sockaddr_cif *addr);
static ssize_t cif_sock_sendto(struct net_context *ctx, const void *buf, size_t len, int flags,
			       const struct sockaddr_cif *addr);
static ssize_t cif_sock_recvfrom(struct net_context *ctx, void *buf, size_t max_len, int flags,
				 const struct sockaddr_cif *addr);
static int cif_sock_getsockopt(struct net_context *ctx, int level, int optname, void *optval,
			       socklen_t *optlen);
static int cif_sock_setsockopt(struct net_context *ctx, int level, int optname, const void *optval,
			       socklen_t optlen);
static int cif_ctrl_port(struct net_context *ctr, const struct cif_raw_port_config *cfg);
static int cif_ldp_error_to_errno(int ldp_err);

static int cif_sock_close_vmeth(void *obj)
{
	int ret;

	k_mutex_lock(&cif_data.x_lock_, K_FOREVER);
	ret = cif_sock_close(reinterpret_cast<struct net_context *>(obj));
	k_mutex_unlock(&cif_data.x_lock_);
	if (ret < 0) {
		NET_DBG("Cannot detach net_context %p (%d)", obj, ret);

		errno = -ret;
		ret = -1;
	}
	return ret;
}

static int cif_sock_bind_vmeth(void *obj, const struct sockaddr *addr, socklen_t addrlen)
{
	int ret;

	if (addr && addrlen == sizeof(struct sockaddr_cif)) {
		k_mutex_lock(&cif_data.x_lock_, K_FOREVER);
		ret = cif_sock_bind(reinterpret_cast<struct net_context *>(obj),
				    reinterpret_cast<const struct sockaddr_cif *>(addr));
		k_mutex_unlock(&cif_data.x_lock_);
		if (ret < 0) {
			NET_DBG("Cannot bind net_context %p (%d)", obj, ret);
			errno = -ret;
			return -1;
		}
		return 0;
	}
	errno = EINVAL;
	return -1;
}

static int cif_sock_sendto_vmeth(void *obj, const void *buf, size_t len, int flags,
				 const struct sockaddr *addr, socklen_t addrlen)
{
	int ret;

	if (buf && len && addr && addrlen == sizeof(struct sockaddr_cif)) {
		k_mutex_lock(&cif_data.x_lock_, K_FOREVER);
		ret = cif_sock_sendto(reinterpret_cast<struct net_context *>(obj), buf, len, flags,
				      reinterpret_cast<const struct sockaddr_cif *>(addr));
		k_mutex_unlock(&cif_data.x_lock_);
		if (ret < 0) {
			NET_DBG("Cannot sendto net_context %p (%d)", obj, ret);
			errno = -ret;
			return -1;
		}
		return ret;
	}
	errno = EINVAL;
	return -1;
}

static int cif_sock_recvfrom_vmeth(void *obj, void *buf, size_t max_len, int flags,
				   struct sockaddr *src_addr, socklen_t *addrlen)
{
	int ret;

	if (buf && max_len && src_addr && *addrlen == sizeof(struct sockaddr_cif)) {
		k_mutex_lock(&cif_data.x_lock_, K_FOREVER);
		ret = cif_sock_recvfrom(reinterpret_cast<struct net_context *>(obj), buf, max_len,
					flags,
					reinterpret_cast<const struct sockaddr_cif *>(src_addr));
		k_mutex_unlock(&cif_data.x_lock_);
		if (ret < 0) {
			NET_DBG("Cannot recvfrom net_context %p (%d)", obj, ret);
			errno = -ret;
			return -1;
		}
		return ret;
	}

	errno = EINVAL;
	return -1;
}

static int cif_sock_getsockopt_vmeth(void *obj, int level, int optname, void *optval,
				     socklen_t *optlen)
{
	int ret;

	if (level == SOL_CIF_RAW && optname >= 0 && optname < CIF_OPT_MAX) {
		k_mutex_lock(&cif_data.x_lock_, K_FOREVER);
		ret = cif_sock_getsockopt(reinterpret_cast<struct net_context *>(obj), level,
					  optname, optval, optlen);
		k_mutex_unlock(&cif_data.x_lock_);
		if (ret < 0) {
			NET_DBG("Cannot getsockopt net_context %p (%d)", obj, ret);
			errno = -ret;
			return -1;
		}
		return 0;
	}
	errno = ENOTSUP;
	return -1;
}

static int cif_sock_setsockopt_vmeth(void *obj, int level, int optname, const void *optval,
				     socklen_t optlen)
{
	int ret;

	if (level == SOL_CIF_RAW && optname >= 0 && optname < CIF_OPT_MAX) {
		k_mutex_lock(&cif_data.x_lock_, K_FOREVER);
		ret = cif_sock_setsockopt(reinterpret_cast<struct net_context *>(obj), level,
					  optname, optval, optlen);
		k_mutex_unlock(&cif_data.x_lock_);
		if (ret < 0) {
			NET_DBG("Cannot setsockopt net_context %p (%d)", obj, ret);
			errno = -ret;
			return -1;
		}
		return 0;
	}
	errno = ENOTSUP;
	return -1;
}

const struct socket_op_vtable cif_sock_fd_op_vtable = {
	.fd_vtable =
		{
			.read = nullptr,
			.write = nullptr,
			.close = cif_sock_close_vmeth,
			.ioctl = nullptr,
		},
	.bind = cif_sock_bind_vmeth,
	.connect = nullptr,
	.listen = nullptr,
	.accept = nullptr,
	.sendto = cif_sock_sendto_vmeth,
	.recvfrom = cif_sock_recvfrom_vmeth,
	.getsockopt = cif_sock_getsockopt_vmeth,
	.setsockopt = cif_sock_setsockopt_vmeth,
};

static int cif_sock_close(struct net_context *ctx)
{
	auto usr_data = reinterpret_cast<cif_sock_data *>(ctx->user_data);

	/* Try to destroy the connection if it exists */
	if (usr_data->ldp) {
		/* destroy all connections by destructing the ldp instance */
		delete usr_data->ldp;
		usr_data->ldp = nullptr;
		usr_data->conns.clear();

		/* release the bus */
		for (int idx = 0; idx < CIF_BUS_MAX; idx++) {
			if (usr_data->mcb == &cif_data.mcb[idx]) {
				cif_data.occupied[idx] = false;
				break;
			}
		}
	}
	delete usr_data;
	net_context_unref(ctx);
	return 0;
}

static int cif_sock_bind(struct net_context *ctx, const struct sockaddr_cif *addr)
{
	auto usr_data = reinterpret_cast<cif_sock_data *>(ctx->user_data);
	mcb_if *mcb;
	work_queue_if *wq;

	if (usr_data->ldp) {
		NET_DBG("Already bound");
		return -EISCONN;
	}

	if (addr->bus >= CIF_BUS_MAX) {
		return -EINVAL;
	}

	mcb = &cif_data.mcb[addr->bus];
	wq = &cif_data.wq;

	if (cif_data.occupied[addr->bus]) {
		return -EADDRNOTAVAIL;
	}

	switch (net_context_get_proto(ctx)) {
	case CIF_RAW_MASTER:
		usr_data->ldp = new zephyr::ldp_master_impl(mcb, wq);
		break;
	case CIF_RAW_SLAVE:
		usr_data->ldp = new zephyr::ldp_slave_impl(mcb);
		break;
	default:
		return -ENOTSUP;
	}

	if (!usr_data->ldp) {
		return -ENOMEM;
	}

	cif_data.occupied[addr->bus] = true;

	return 0;
}

static ssize_t cif_sock_sendto(struct net_context *ctx, const void *buf, size_t len, int flags,
			       const struct sockaddr_cif *addr)
{
	auto usr_data = reinterpret_cast<cif_sock_data *>(ctx->user_data);
	conn_idx id{addr->port, addr->slot};

	if (!usr_data->ldp) {
		return -ENOTCONN;
	}

	auto conn_it = usr_data->conns.find(id);
	if (conn_it == usr_data->conns.end()) {
		return -ENOENT;
	}

	auto conn = conn_it->second.conn_id;
	int ret = (*usr_data->ldp).send(conn, reinterpret_cast<const uint8_t *>(buf), len);

	if (ret < 0) {
		cif_ldp_error_to_errno(ret);
	}
	return ret;
}

static ssize_t cif_sock_recvfrom(struct net_context *ctx, void *buf, size_t max_len, int flags,
				 const struct sockaddr_cif *addr)
{
	auto usr_data = reinterpret_cast<cif_sock_data *>(ctx->user_data);
	conn_idx id{addr->port, addr->slot};

	if (!usr_data->ldp) {
		return -ENOTCONN;
	}

	auto conn_it = usr_data->conns.find(id);
	if (conn_it == usr_data->conns.end()) {
		return -ENOENT;
	}

	auto conn = conn_it->second.conn_id;
	int ret = (*usr_data->ldp).recv(conn, reinterpret_cast<uint8_t *>(buf), max_len);

	if (ret < 0) {
		ret = cif_ldp_error_to_errno(ret);
	}

	return ret;
}

static int cif_sock_getsockopt(struct net_context *ctx, int level, int optname, void *optval,
			       socklen_t *optlen)
{
	auto usr_data = reinterpret_cast<cif_sock_data *>(ctx->user_data);

	if (!usr_data->ldp) {
		return -ENOTCONN;
	}

	switch (optname) {
	case CIF_OPT_MASTER_CONFIG: {
		if (*optlen != sizeof(cif_raw_master_config) ||
		    net_context_get_proto(ctx) != CIF_RAW_MASTER) {
			return -EINVAL;
		}
		memcpy(optval, &usr_data->master_cfg, sizeof(cif_raw_master_config));
		break;
	}
	case CIF_OPT_PORT: {
		if (*optlen != sizeof(cif_raw_port_config)) {
			return -EINVAL;
		}
		auto conn_it = usr_data->conns.find(
			conn_idx{reinterpret_cast<cif_raw_port_config *>(optval)->port,
				 reinterpret_cast<cif_raw_port_config *>(optval)->slot});
		if (conn_it == usr_data->conns.end()) {
			return -ENOENT;
		}
		memcpy(optval, &conn_it->second.config, sizeof(cif_raw_port_config));
		break;
	}
	case CIF_OPT_ERROR: {
		auto filter = reinterpret_cast<cif_error_filter *>(optval);
		uint32_t errors = 0;
		uint32_t matched_cnt = 0;
		if (*optlen != sizeof(cif_error_filter)) {
			return -EINVAL;
		}
		for (auto &conn : usr_data->conns) {
			bool is_slot_matched = (filter->flags & CIF_FILTER_SLOT)
						       ? conn.first.slot == filter->slot
						       : true;
			bool is_port_matched = (filter->flags & CIF_FILTER_PORT)
						       ? conn.first.port == filter->port
						       : true;
			bool is_port_enabled = conn.second.config.flags & CIF_PORT_FLG_ENABLE &&
					       conn.second.conn_id >= 0;

			if (is_slot_matched && is_port_matched && is_port_enabled) {
				errors |= (*usr_data->ldp).get_extra_error(conn.second.conn_id);
				matched_cnt++;
			}
		}
		filter->error_mask = errors;
		return matched_cnt ? 0 : -ENOENT;
	}
	default: {
		return -ENOTSUP;
	}
	}
	return 0;
}

static int cif_sock_setsockopt(struct net_context *ctx, int level, int optname, const void *optval,
			       socklen_t optlen)
{
	auto usr_data = reinterpret_cast<cif_sock_data *>(ctx->user_data);
	int ret = 0;

	if (!usr_data->ldp) {
		return -ENOTCONN;
	}

	switch (optname) {
	case CIF_OPT_MASTER_CONFIG: {
		auto opt = reinterpret_cast<const cif_raw_master_config *>(optval);
		if (optlen != sizeof(cif_raw_master_config) ||
		    net_context_get_proto(ctx) != CIF_RAW_MASTER) {
			return -EINVAL;
		}
		ldp_mcb_config cfg{opt->poll_time};
		ret = reinterpret_cast<zephyr::ldp_master_impl *>(usr_data->ldp)
			      ->set_mcb_config(cfg);
		if (ret >= 0) {
			memcpy(&usr_data->master_cfg, optval, sizeof(cif_raw_master_config));
		} else {
			ret = cif_ldp_error_to_errno(ret);
		}
	} break;
	case CIF_OPT_PORT: {
		if (optlen != sizeof(cif_raw_port_config)) {
			return -EINVAL;
		}
		ret = cif_ctrl_port(ctx, reinterpret_cast<const cif_raw_port_config *>(optval));
	} break;
	case CIF_OPT_ERROR: {
		auto filter = reinterpret_cast<const cif_error_filter *>(optval);
		uint32_t errors = filter->error_mask;
		uint32_t matched_cnt = 0;

		if (optlen != sizeof(cif_error_filter)) {
			return -EINVAL;
		}

		for (auto &conn : usr_data->conns) {
			bool is_slot_matched = (filter->flags & CIF_FILTER_SLOT)
						       ? conn.first.slot == filter->slot
						       : true;
			bool is_port_matched = (filter->flags & CIF_FILTER_PORT)
						       ? conn.first.port == filter->port
						       : true;
			bool is_port_enabled = conn.second.config.flags & CIF_PORT_FLG_ENABLE &&
					       conn.second.conn_id >= 0;

			if (is_slot_matched && is_port_matched && is_port_enabled) {
				(*usr_data->ldp).clr_extra_error(conn.second.conn_id, errors);
				matched_cnt++;
			}
		}
		return matched_cnt ? 0 : -ENOENT;
	}
	default: {
		return -ENOTSUP;
	}
	}
	return ret;
}

static int cif_ctrl_port(struct net_context *ctx, const struct cif_raw_port_config *cfg)
{
	auto usr_data = reinterpret_cast<cif_sock_data *>(ctx->user_data);
	conn_idx id{cfg->port, cfg->slot};
	conn_t conn{-1};
	bool prev_enable, next_enable;
	auto conn_it = usr_data->conns.find(id);
	int ret = 0;

	/* Find previous connection, other wise using default connection info */
	if (conn_it != usr_data->conns.end()) {
		conn = conn_it->second;
	}

	next_enable = cfg->flags & CIF_PORT_FLG_ENABLE;
	prev_enable = conn.config.flags & CIF_PORT_FLG_ENABLE && conn.conn_id >= 0;

	if (prev_enable && next_enable) {
		/* Connection already enabled, disable it first. Not support config in run mode */
		ret = -EBUSY;
	} else if (!prev_enable && !next_enable) {
		/* Connection already disabled */
	} else if (prev_enable && !next_enable) {
		/* Disable connection */
		if (conn.conn_id >= 0) {
			ret = (*usr_data->ldp).destroy(conn.conn_id);

			if (ret < 0) {
				ret = cif_ldp_error_to_errno(ret);
				NET_WARN("Failed to disable connection %d", conn.conn_id);
			}
			NET_INFO("Connection %d disabled", conn.conn_id);
			conn.conn_id = -1;
		}
	} else {
		/* Enable connection */
		bool is_async = CIF_IS_ASYNC_PORT(cfg->port);
		bool is_master = net_context_get_proto(ctx) == CIF_RAW_MASTER;
		bool is_preempt = cfg->flags & CIF_PORT_FLG_PREEMPT;
		bool is_one_shot = cfg->flags & CIF_PORT_FLG_ONE_SHOT;
		bool is_allow_write = cfg->flags & CIF_PORT_FLG_ALLOW_WRITE;
		bool is_strong_order = cfg->flags & CIF_PORT_FLG_STRONG_ORDER;

		if (CIF_IS_UNKNOWN_PORT(cfg->port)) {
			return -EINVAL;
		}

		if (is_master && !is_async) {
			/* master sync port */
			ldp_master_sync_config config;
			config.port = cfg->port;
			config.dst = cfg->slot;
			config.cycle_time = usr_data->master_cfg.cycle_time;
			config.preempt = is_preempt;
			config.one_shot = is_one_shot;
			conn.conn_id = (*usr_data->ldp).create(false, &config);
		} else if (is_master && is_async) {
			/* master async port */
			ldp_master_async_config config;
			config.port = cfg->port;
			config.dst = cfg->slot;
			config.cycle_time = cfg->async_interval;
			config.timeout = cfg->async_timeout;
			config.preempt = is_preempt;
			config.one_shot = is_one_shot;
			config.strong_order = is_strong_order;
			conn.conn_id = (*usr_data->ldp).create(true, &config);
		} else if (!is_master && !is_async) {
			/* slave sync port */
			ldp_slave_sync_config config;
			config.port = cfg->port;
			config.max_recv_len = cfg->slave_max_recv_len;
			config.allow_write = is_allow_write;
			conn.conn_id = (*usr_data->ldp).create(false, &config);
		} else {
			/* slave async port */
			ldp_slave_async_config config;
			config.port = cfg->port;
			config.max_recv_len = cfg->slave_max_recv_len;
			/* FIXME(savent): add allow write option? */
			conn.conn_id = (*usr_data->ldp).create(true, &config);
		}

		if (conn.conn_id >= 0) {
			conn.config = *cfg;
			NET_INFO("Connection %d created. "
				 "[%d:%d].\tai:%04x,at:%04x,ab:%04x,smrl:%04x",
				 conn.conn_id, cfg->port, cfg->slot, cfg->async_interval,
				 cfg->async_timeout, cfg->async_bandwidth, cfg->slave_max_recv_len);
		} else {
			NET_INFO("Failed to create connection [%d:%d]", cfg->port, cfg->slot);
			return cif_ldp_error_to_errno(conn.conn_id);
		}
	}
	/* insert or update connection info */
	/* FIXME: using usr_data->conns[id] = conn; will hang in std::rb_tree::insert_and_rebalance
	 */
	if (conn_it != usr_data->conns.end()) {
		conn_it->second = conn;
	} else {
		usr_data->conns.insert(cif_sock_data::conn_list_t::value_type(id, conn));
	}
	usr_data->conns[id] = conn;
	return 0;
}

static inline int cif_ldp_error_to_errno(int ldp_err)
{
	using ldp_error = ldp_basic::ldp_error;
	switch (-ldp_err) {
	case ldp_error::LDP_ERR_OK:
		return 0;
	case ldp_error::LDP_ERR_T_ERROR:
		return -EIO;
	case ldp_error::LDP_ERR_P_ERROR:
		return -ECONNREFUSED;
	case ldp_error::LDP_ERR_ATIMEOUT:
		return -ETIMEDOUT;
	case ldp_error::LDP_ERR_INVALID:
		return -EINVAL;
	case ldp_error::LDP_ERR_INVALID_ASYNC_PACK:
		return -ENOMSG;
	case ldp_error::LDP_ERR_AGAIN:
		return -EAGAIN;
	case ldp_error::LDP_ERR_CONN_NOT_FOUND:
		return -ENOENT;
	case ldp_error::LDP_ERR_NOMEM:
		return -ENOMEM;
	case ldp_error::LDP_ERR_RX_BUF_TOO_SMALL:
		return -ENOBUFS;
	case ldp_error::LDP_ERR_CONN_EXIST:
		return -EEXIST;
	default:
		printk("Unknown error code %d\n", ldp_err);
		return -EOVERFLOW;
	}
	return 0;
}

int zcif_socket(int family, net_sock_type type, int proto)
{
	struct net_context *ctx;
	cif_sock_data *usr_data;
	int fd, ret;

	fd = zvfs_reserve_fd();
	if (fd < 0) {
		return -1;
	}

	usr_data = new cif_sock_data;
	if (!usr_data) {
		zvfs_free_fd(fd);
		errno = ENOMEM;
		return -1;
	}

	ret = net_context_get(family, type, proto, &ctx);
	if (ret < 0) {
		zvfs_free_fd(fd);
		delete usr_data;
		errno = -ret;
		return -1;
	}

	usr_data->ldp = nullptr;
	ctx->user_data = usr_data;

	zvfs_finalize_typed_fd(fd, ctx, (const struct fd_op_vtable *)&cif_sock_fd_op_vtable,
			       ZVFS_MODE_IFSOCK);
	return fd;
}

static bool cif_is_supported(int family, int type, int proto)
{
	bool is_sup_family = family == PF_CIF;
	bool is_sup_type = type == SOCK_RAW;
	bool is_sup_proto = proto == CIF_RAW_MASTER || proto == CIF_RAW_SLAVE;

	return is_sup_family && is_sup_type && is_sup_proto;
}

NET_SOCKET_REGISTER(af_cif, NET_SOCKET_DEFAULT_PRIO, PF_CIF, cif_is_supported,
		    reinterpret_cast<int (*)(int, int, int)>(zcif_socket));

static void wq_background_entry(void *arg1, void *arg2, void *arg3)
{
	auto wq = reinterpret_cast<cif_master_data::ldp_wq *>(arg1);

	while (true) {
		if (wq->empty()) {
			k_sleep(K_MSEC(10));
		} else {
			k_mutex_lock(&cif_data.x_lock_, K_FOREVER);
			wq->schedule();
			k_mutex_unlock(&cif_data.x_lock_);
			NET_DBG("WQ scheduled");
		}
	}
}

static int ldp_init(void)
{
	k_mutex_init(&cif_data.x_lock_);
	cif_data.wq_tid = k_thread_create(
		&cif_data.wq_thread, cif_data.wq_stack, K_THREAD_STACK_SIZEOF(cif_data.wq_stack),
		wq_background_entry, &cif_data.wq, nullptr, nullptr,
		K_PRIO_PREEMPT(CONFIG_MAIN_THREAD_PRIORITY + 1), 0, K_NO_WAIT);
	k_thread_name_set(&cif_data.wq_thread, "cif_wq");
	return 0;
}

SYS_INIT_NAMED(net_sock_ldp_init, ldp_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#ifdef __cplusplus
}
#endif
