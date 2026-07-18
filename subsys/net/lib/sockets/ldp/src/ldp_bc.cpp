/**
 * @file ldp_bc.cpp
 * @author Liao Yuan Kai(savent_gate@outlook.com)
 * @brief bandwidth control for CIF/LDP master stack
 * @version 0.1
 * @date 2025-02-13
 *
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * @copyright Copyright (c) 2025 SYSTech Co.
 *
 */
#include "ldp_bc.hpp"

#include <list>
#include <memory>

namespace systech::cif::bc
{

conn_item::conn_item(bc_mode m, unsigned pps, unsigned granted)
	: mode(m), pps_required(pps), pps_granted(granted)
{
}

ldp_bc::ldp_bc(unsigned bus_pps, float reserved_for_sync, float reserved_for_async)
	: bus_pps_(bus_pps), pps_reserved_for_sync_((unsigned)(bus_pps * reserved_for_sync)),
	  pps_reserved_for_async_((unsigned)(bus_pps * reserved_for_async))
{
}

bool ldp_bc::add_conn(conn_ptr conn)
{
	if (!conn) {
		return false;
	}

	switch (conn->mode) {
	case bc_mode::BC_MODE_SYNC:
		if (conn->pps_required + pps_sync_used_ > pps_reserved_for_sync_) {
			return false;
		}
		pps_sync_used_ += conn->pps_required;
		break;
	case bc_mode::BC_MODE_ASYNC_AUTO:
		count_async_auto_++;
		[[fallthrough]];
	case bc_mode::BC_MODE_ASYNC:
		pps_async_used_ += conn->pps_required;
		break;
	}
	conn_list.push_back(conn);
	return true;
}

void ldp_bc::rm_conn(conn_ptr conn)
{
	if (!conn) {
		return;
	}

	switch (conn->mode) {
	case bc_mode::BC_MODE_SYNC:
		pps_sync_used_ -= conn->pps_required;
		break;
	case bc_mode::BC_MODE_ASYNC_AUTO:
		count_async_auto_--;
		[[fallthrough]];
	case bc_mode::BC_MODE_ASYNC:
		pps_async_used_ -= conn->pps_required;
		break;
	}
	conn_list.remove_if([conn](const conn_ptr &c) { return c.get() == conn.get(); });
}

void ldp_bc::schedule(unsigned delta_microsec)
{
	float delta = static_cast<float>(delta_microsec) / 1'000'000;
	float pps_for_async = bus_pps_ - pps_reserved_for_sync_;
	float minimal_pps_for_async_no_limit = pps_reserved_for_async_;
	float pps_for_async_limited = pps_async_used_;
	float async_overrun =
		(pps_for_async_limited + minimal_pps_for_async_no_limit) / pps_for_async;
	float t = 0;

	for (const auto &conn : conn_list) {
		switch (conn->mode) {
		case bc_mode::BC_MODE_SYNC:
			t = delta * float(conn->pps_required);
			break;
		case bc_mode::BC_MODE_ASYNC:
			t = delta * float(conn->pps_required) / (async_overrun > 1 ? async_overrun : 1);
			break;
		case bc_mode::BC_MODE_ASYNC_AUTO:
			if (async_overrun < 1) {
				t = (pps_for_async - pps_for_async_limited) * delta;
			} else {
				t = minimal_pps_for_async_no_limit * delta / async_overrun;
			}
			t /= count_async_auto_ > 0 ? count_async_auto_ : 1;
			break;
		}

		/* Avoid value overflow, only allow tiny increment */
		if (conn->pps_granted < 1) {
			conn->pps_granted += t;
		} else if (t > 1) {
			conn->pps_granted = t;
		}
	}
}
bool ldp_bc::can_grant(conn_ptr conn, int p) const
{
	return conn->pps_granted >= p;
}

bool ldp_bc::try_grant(conn_ptr conn, int p)
{
	if (can_grant(conn, p)) {
		conn->pps_granted -= p;
		return true;
	}
	return false;
}
} // namespace systech::cif::bc
