/*
 * SPDX-License-Identifier: Apache-2.0
 * COPYRIGHT (c) 2025 SYSTech Co.
 */
#include <gtest/gtest.h>
#include "ldp_bc.hpp"

using namespace systech::cif::bc;

TEST(bc, add_rm)
{
	ldp_bc bc(1000, 0.1, 0); /* alloc 100 pps for sync */

	auto conn1 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 30, 0);
	auto conn2 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 30, 0);
	auto conn3 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 30, 0);
	auto conn4 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 30, 0);

	EXPECT_EQ(bc.add_conn(conn1), true);
	EXPECT_EQ(bc.add_conn(conn2), true);
	EXPECT_EQ(bc.add_conn(conn3), true);
	EXPECT_EQ(bc.add_conn(conn4), false); /* no more pps for sync */

	bc.rm_conn(conn1);
	EXPECT_EQ(bc.add_conn(conn4), true); /* now we have pps for sync */
}

TEST(bc, sync)
{
	ldp_bc bc(1000, 0.1, 0); /* alloc 100 pps for sync */

	auto conn1 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 30, 0);
	auto conn2 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 30, 0);
	auto conn3 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 30, 0);
	auto conn4 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 30, 0);

	EXPECT_EQ(bc.add_conn(conn1), true);
	EXPECT_EQ(bc.add_conn(conn2), true);
	EXPECT_EQ(bc.add_conn(conn3), true);

	bc.schedule(1'000'000); /* 1s */
	ASSERT_NEAR(conn1->pps_granted, 30.0, 0.0001);
	ASSERT_NEAR(conn2->pps_granted, 30.0, 0.0001);
	ASSERT_NEAR(conn3->pps_granted, 30.0, 0.0001);

	bc.schedule(1'000'000); /* 1s */
	ASSERT_NEAR(conn1->pps_granted, 30.0, 0.0001);
	ASSERT_NEAR(conn2->pps_granted, 30.0, 0.0001);
	ASSERT_NEAR(conn3->pps_granted, 30.0, 0.0001);

	bc.try_grant(conn1, 10);
	bc.try_grant(conn2, 20);
	bc.schedule(1'000'000); /* 1s */
	ASSERT_NEAR(conn1->pps_granted, 30.0, 0.0001);
	ASSERT_NEAR(conn2->pps_granted, 30.0, 0.0001);
	ASSERT_NEAR(conn3->pps_granted, 30.0, 0.0001);
}

TEST(bc, async)
{
	ldp_bc bc(1000, 0.5, 0); /* alloc 500 pps for sync */

	auto conn1 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 100, 0);
	auto conn2 = std::make_shared<conn_item>(bc_mode::BC_MODE_ASYNC, 500, 0);
	auto conn3 = std::make_shared<conn_item>(bc_mode::BC_MODE_ASYNC, 500, 0);

	EXPECT_EQ(bc.add_conn(conn1), true);
	EXPECT_EQ(bc.add_conn(conn2), true);
	EXPECT_EQ(bc.add_conn(conn3), true);

	bc.schedule(1'000'000); /* 1s */
	ASSERT_NEAR(conn1->pps_granted, 100.0, 0.001);
	ASSERT_NEAR(conn2->pps_granted, 250.0, 0.001);
	ASSERT_NEAR(conn3->pps_granted, 250.0, 0.001);

	bc.schedule(1'000'000); /* 1s */
	ASSERT_NEAR(conn1->pps_granted, 100.0, 0.001);
	ASSERT_NEAR(conn2->pps_granted, 250.0, 0.001);
	ASSERT_NEAR(conn3->pps_granted, 250.0, 0.001);
}

TEST(bc, async_no_limit)
{
	ldp_bc bc(1000, 0.5, 0); /* alloc 500 pps for sync */

	auto conn1 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 100, 0);
	auto conn2 = std::make_shared<conn_item>(bc_mode::BC_MODE_ASYNC_NO_LIMIT, 0, 0);
	auto conn3 = std::make_shared<conn_item>(bc_mode::BC_MODE_ASYNC_NO_LIMIT, 0, 0);

	EXPECT_EQ(bc.add_conn(conn1), true);
	EXPECT_EQ(bc.add_conn(conn2), true);
	EXPECT_EQ(bc.add_conn(conn3), true);

	bc.schedule(1'000'000); /* 1s */
	ASSERT_NEAR(conn1->pps_granted, 100.0, 0.001);
	ASSERT_NEAR(conn2->pps_granted, 250.0, 0.001);
	ASSERT_NEAR(conn3->pps_granted, 250.0, 0.001);
}

TEST(bc, async_hyp)
{
	/* alloc 200 pps for sync, reserved 200 pps for no limited async */
	ldp_bc bc(1000, 0.2, 0.2);

	auto conn1 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 100, 0);
	auto conn2 = std::make_shared<conn_item>(bc_mode::BC_MODE_ASYNC_NO_LIMIT, 0, 0);
	auto conn3 = std::make_shared<conn_item>(bc_mode::BC_MODE_ASYNC, 500, 0);

	EXPECT_EQ(bc.add_conn(conn1), true);
	EXPECT_EQ(bc.add_conn(conn2), true);
	EXPECT_EQ(bc.add_conn(conn3), true);

	bc.schedule(1'000'000); /* 1s */
	ASSERT_NEAR(conn1->pps_granted, 100.0, 0.001);
	ASSERT_NEAR(conn2->pps_granted, 300.0, 0.001);
	ASSERT_NEAR(conn3->pps_granted, 500.0, 0.001);
}

TEST(bc, async_hyp_overrun)
{
	/* alloc 200 pps for sync, reserved 200 pps for no limited async */
	ldp_bc bc(1000, 0.2, 0.2);

	auto conn1 = std::make_shared<conn_item>(bc_mode::BC_MODE_SYNC, 100, 0);
	auto conn2 = std::make_shared<conn_item>(bc_mode::BC_MODE_ASYNC_NO_LIMIT, 0, 0);
	auto conn3 = std::make_shared<conn_item>(bc_mode::BC_MODE_ASYNC, 500, 0);
	auto conn4 = std::make_shared<conn_item>(bc_mode::BC_MODE_ASYNC, 500, 0);

	EXPECT_EQ(bc.add_conn(conn1), true);
	EXPECT_EQ(bc.add_conn(conn2), true);
	EXPECT_EQ(bc.add_conn(conn3), true);
	EXPECT_EQ(bc.add_conn(conn4), true);

	bc.schedule(1'000'000); /* 1s */
	EXPECT_NEAR(conn1->pps_granted, 100.0, 0.001);
	EXPECT_NEAR(conn2->pps_granted + conn3->pps_granted + conn4->pps_granted, 800.0, 0.001);
	EXPECT_NEAR(conn3->pps_granted, conn4->pps_granted, 0.001);
	EXPECT_GT(conn2->pps_granted, 1);

	while (bc.try_grant(conn2, 1))
		;

	bc.schedule(1'000'000); /* 1s */
	EXPECT_NEAR(conn1->pps_granted, 100.0, 0.001);
	EXPECT_NEAR(conn2->pps_granted + conn3->pps_granted + conn4->pps_granted, 800.0, 2);
	EXPECT_NEAR(conn3->pps_granted, conn4->pps_granted, 2);
	EXPECT_GT(conn2->pps_granted, 1);

	while (bc.try_grant(conn3, 1))
		;
	bc.schedule(1'000'000); /* 1s */
	EXPECT_NEAR(conn1->pps_granted, 100.0, 0.001);
	EXPECT_NEAR(conn2->pps_granted + conn3->pps_granted + conn4->pps_granted, 800.0, 2);
	EXPECT_NEAR(conn3->pps_granted, conn4->pps_granted, 2);
	EXPECT_GT(conn2->pps_granted, 1);

	while (bc.try_grant(conn3, 1))
		;
	while (bc.try_grant(conn4, 1))
		;
	bc.schedule(1'000'000); /* 1s */
	EXPECT_NEAR(conn1->pps_granted, 100.0, 0.001);
	EXPECT_NEAR(conn2->pps_granted + conn3->pps_granted + conn4->pps_granted, 800.0, 2);
	EXPECT_NEAR(conn3->pps_granted, conn4->pps_granted, 2);
	EXPECT_GT(conn2->pps_granted, 1);
}
