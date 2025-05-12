/**
 * @file ldp_bc.hpp
 * @author Liao Yuan Kai(savent_gate@outlook.com)
 * @brief bandwidth control for CIF/LDP master stack
 * @version 0.1
 * @date 2025-02-13
 *
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * @copyright Copyright (c) 2025 SYSTech Co.
 *
 */
#pragma once

#include <list>
#include <memory>

namespace systech::cif::bc
{

enum class bc_mode {
	BC_MODE_SYNC,
	BC_MODE_ASYNC_AUTO,
	BC_MODE_ASYNC,
};

struct conn_item {
	bc_mode mode;
	unsigned pps_required;
	float pps_granted;

	explicit conn_item(bc_mode m, unsigned pps, unsigned granted);
};
using conn_ptr = std::shared_ptr<conn_item>;

/**
 * @brief Bandwidth control for CIF/LDP master stack
 *
 * @details
 * * The bandwidth is divided into two parts: reserved for sync and reserved for async.
 * * The reserved bandwidth for sync is fixed, while the reserved bandwidth for async is
 *   shared by all async connections.
 * * The reserved bandwidth for async is divided into two parts: limited and auto.
 *   * The limited async conn will get shares based on the bias value.
 *   * The auto async conns will share the rest of the reserved async bandwidth equally.
 * * There is a situation that all the async bandwidth will scale down: when the reserved async
 *   bandwidth and the async bandwidth asked by user is larger than the total async bandwidth.
 *   In this case, all the async bandwidth will scale down by the ratio of overuse, to make sure
 * 	 that the total async bandwidth + reserved sync bandwidth is less than the total bus
 * bandwidth.
 * @example
 * 	BUS_PPS = 1000, reserved_for_sync = 200, reserved_for_async = 200
 *  conn1: SYNC required 100 pps
 *  conn2: ASYNC required 500 pps
 *  conn3: auto ASYNC required
 *  conn4: auto ASYNC required
 *
 *  In this case, the bus is not overused, conn will get:
 *  - conn1: 100 pps
 *  - conn2: 500 pps
 *  - conn3: 150 pps
 *  - conn4: 150 pps
 *
 * @example
 *  BUS_PPS = 1000, reserved_for_sync = 200, reserved_for_async = 200
 *  conn1: SYNC required 100 pps
 *  conn2: ASYNC required 500 pps
 *  conn3: ASYNC required 500 pps
 *  conn4: auto ASYNC required
 *  conn5: auto ASYNC required
 *  conn6: auto ASYNC required
 *
 *  In this case, the bus is overused (overrun ratio=1200/800), conn will get:
 *  - conn1: 100 pps
 *  - conn2: 333 pps (500* 0.666)
 *  - conn3: 333 pps (500* 0.666)
 *  - conn4:  44 pps (200* (1/3) * 0.666)
 *  - conn5:  44 pps (200* (1/3) * 0.666)
 *  - conn6:  44 pps (200* (1/3) * 0.666)
 */
struct ldp_bc {

      public:
	explicit ldp_bc(unsigned bus_pps, float reserved_for_sync, float reserved_for_async);
	virtual ~ldp_bc() = default;

	/**
	 * @brief Add a connection to the bandwidth control
	 */
	bool add_conn(conn_ptr conn);

	/**
	 * @brief Remove a connection from the bandwidth control
	 */
	void rm_conn(conn_ptr conn);

	/**
	 * @brief Schedules and updates the granted PPS (packets per second) for each connection.
	 *
	 * This function computes the PPS allocation for connections based on a given time delta.
	 * The delta is converted from microseconds to seconds, and then the method calculates
	 * separate PPS values for synchronous and asynchronous modes:
	 *
	 * - For synchronous connections (BC_MODE_SYNC), it determines the allocation proportionally
	 * by the reserved synchronous PPS.
	 * - For asynchronous connections with limits (BC_MODE_ASYNC), it calculates the allocation
	 * using the limited asynchronous PPS adjusted by the async_overrun factor to prevent
	 * overload.
	 * - For asynchronous connections without limits (BC_MODE_ASYNC_AUTO), it computes the
	 * allocation either using the surplus asynchronous PPS (when not overloaded) or by
	 * adjusting the minimal reserved asynchronous PPS using the async_overrun factor. The
	 * allocation is then equally distributed among all no-limit asynchronous connections.
	 *
	 * The computed allocation (t) is then applied to each connection's granted PPS:
	 * - If the current granted PPS is below 1, it is incremented by t, to make sure resource
	 * can be used after serval rounds.
	 * - Otherwise, the granted PPS is reset to t.
	 *
	 * @param delta_microsec The elapsed time interval in microseconds used for scheduling.
	 */
	void schedule(unsigned delta_microsec);

	/**
	 * @brief Try to grant bandwidth to a connection
	 * @param conn The connection to grant bandwidth
	 * @param p The packets want to grant
	 *
	 * @retval true The bandwidth is granted
	 * @retval false The bandwidth is not granted
	 */
	bool try_grant(conn_ptr conn, int p);

      private:
	std::list<conn_ptr> conn_list;
	unsigned bus_pps_;
	unsigned pps_reserved_for_sync_;  /* reserved pps for sync */
	unsigned pps_reserved_for_async_; /* reserved pps for async (auto) */
	unsigned pps_sync_used_ = 0;
	unsigned pps_async_used_ = 0;
	unsigned count_async_auto_ = 0;
};

struct ldp_bc_dummy: public ldp_bc {
	ldp_bc_dummy(unsigned, float, float) : ldp_bc(0, 0, 0)
	{
	}
	bool add_conn(conn_ptr conn)
	{
		return true;
	}
	void rm_conn(conn_ptr conn)
	{
	}
	void schedule(unsigned delta_microsec)
	{
	}
	bool try_grant(conn_ptr conn, int p)
	{
		return true;
	}
};

using bc_std = ldp_bc;

} // namespace systech::cif::bc
