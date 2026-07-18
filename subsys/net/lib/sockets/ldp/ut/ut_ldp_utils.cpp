/**
 * @file ut_ldp_utils.cpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @version 0.1
 * @date 2025-01-26
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * @copyright Copyright (c) 2025 SYSTech Co.
 */
#include <assert.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ldp_utils.hpp"

using namespace systech::cif;

TEST(memcpy, basic)
{
    uint8_t src[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint8_t dst[8] = {0};

    ldp_memcpy::memcpy(dst, src, 8);
    EXPECT_THAT(dst, testing::ElementsAreArray(src));
}

TEST(memcpy, partial_copy)
{
    uint8_t src[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                       0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
    uint8_t dst[12] = {0};

    ldp_memcpy::memcpy(dst, src, 10);

    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(dst[i], src[i]);
    }
    for (int i = 10; i < 12; i++) {
        EXPECT_EQ(dst[i], 0);
    }
}

TEST(work_queue_schedule, selects_due_item_over_sleeping_candidate)
{
    EXPECT_TRUE(ldp_work_item_should_select(-10, work_queue_if::PRIORITY_NORMAL, 1000,
                                            work_queue_if::PRIORITY_NORMAL, false));
}

TEST(work_queue_schedule, selects_high_priority_due_item_over_more_overdue_normal_item)
{
    EXPECT_TRUE(ldp_work_item_should_select(-100, work_queue_if::PRIORITY_HIGH, -5000,
                                            work_queue_if::PRIORITY_NORMAL, true));
}

TEST(work_queue_schedule, keeps_earliest_sleep_when_no_item_is_due)
{
    EXPECT_TRUE(ldp_work_item_should_select(500, work_queue_if::PRIORITY_NORMAL, 1000,
                                            work_queue_if::PRIORITY_NORMAL, false));
    EXPECT_FALSE(ldp_work_item_should_select(1500, work_queue_if::PRIORITY_NORMAL, 1000,
                                             work_queue_if::PRIORITY_NORMAL, false));
}

TEST(work_queue_schedule, keeps_most_overdue_item_when_priorities_match)
{
    EXPECT_TRUE(ldp_work_item_should_select(-5000, work_queue_if::PRIORITY_NORMAL, -100,
                                            work_queue_if::PRIORITY_NORMAL, true));
}

TEST(work_queue_schedule, defers_normal_due_item_when_high_priority_is_near_due)
{
    EXPECT_TRUE(ldp_work_item_should_defer_for_priority(work_queue_if::PRIORITY_NORMAL, 120,
                                                        work_queue_if::PRIORITY_HIGH, 5000));
    EXPECT_TRUE(ldp_work_item_should_defer_for_priority(work_queue_if::PRIORITY_NORMAL, 400,
                                                        work_queue_if::PRIORITY_HIGH, 5000));
    EXPECT_FALSE(ldp_work_item_should_defer_for_priority(work_queue_if::PRIORITY_NORMAL, 700,
                                                         work_queue_if::PRIORITY_HIGH, 5000));
    EXPECT_FALSE(ldp_work_item_should_defer_for_priority(work_queue_if::PRIORITY_HIGH, 120,
                                                         work_queue_if::PRIORITY_HIGH, 5000));
}

TEST(work_queue_schedule, keeps_high_at_or_below_normal_overdue_limit)
{
    auto below = ldp_work_item_priority_decision(true, true, -99999, false, 100000);
    auto boundary = ldp_work_item_priority_decision(true, true, -100000, false, 100000);

    EXPECT_EQ(below.choice, ldp_work_item_due_choice::DEFAULT);
    EXPECT_EQ(boundary.choice, ldp_work_item_due_choice::DEFAULT);
}

TEST(work_queue_schedule, forces_normal_strictly_past_overdue_limit)
{
    auto decision = ldp_work_item_priority_decision(true, true, -100001, false, 100000);

    EXPECT_EQ(decision.choice, ldp_work_item_due_choice::FORCE_NORMAL);
    EXPECT_FALSE(decision.clear_repayment);
}

TEST(work_queue_schedule, forces_async_after_40ms_deadline_plus_100ms_overdue)
{
    constexpr int32_t async_initial_delay_us = 40000;
    constexpr uint32_t max_overdue_us = 100000;
    auto at_boundary = ldp_work_item_priority_decision(
        true, true, async_initial_delay_us - 140000, false, max_overdue_us);
    auto past_boundary = ldp_work_item_priority_decision(
        true, true, async_initial_delay_us - 140001, false, max_overdue_us);

    EXPECT_EQ(at_boundary.choice, ldp_work_item_due_choice::DEFAULT);
    EXPECT_EQ(past_boundary.choice, ldp_work_item_due_choice::FORCE_NORMAL);
}

TEST(work_queue_schedule, repays_high_before_forcing_another_normal)
{
    auto decision = ldp_work_item_priority_decision(true, true, -200000, true, 100000);

    EXPECT_EQ(decision.choice, ldp_work_item_due_choice::REPAY_HIGH);
}

TEST(work_queue_schedule, pending_repayment_does_not_block_when_high_is_sleeping)
{
    auto decision = ldp_work_item_priority_decision(true, false, -200000, true, 100000);

    EXPECT_EQ(decision.choice, ldp_work_item_due_choice::DEFAULT);
    EXPECT_FALSE(decision.clear_repayment);
}

TEST(work_queue_schedule, clears_repayment_when_no_high_item_remains)
{
    auto decision = ldp_work_item_priority_decision(false, false, -200000, true, 100000);

    EXPECT_EQ(decision.choice, ldp_work_item_due_choice::DEFAULT);
    EXPECT_TRUE(decision.clear_repayment);
}

TEST(work_queue_schedule, handles_int32_min_overdue_without_overflow)
{
    auto decision = ldp_work_item_priority_decision(true, true, INT32_MIN, false,
                                                     2147483647u);

    EXPECT_EQ(decision.choice, ldp_work_item_due_choice::FORCE_NORMAL);
}

TEST(work_queue_dispatch, ignores_selection_that_was_not_dispatched)
{
    ldp_work_item_dispatch_state state{};
    const ldp_work_item_dispatch_attempt attempt{
        ldp_work_item_due_choice::FORCE_NORMAL, false, 2, 1, 100001, 50000};

    auto update = ldp_work_item_apply_dispatch(state, attempt, 1000000);

    EXPECT_FALSE(state.high_repayment_pending);
    EXPECT_EQ(update.event, ldp_work_item_forced_report_event::NONE);
}

TEST(work_queue_dispatch, repays_high_before_forcing_normal_again)
{
    ldp_work_item_dispatch_state state{};
    auto first = ldp_work_item_apply_dispatch(
        state,
        {ldp_work_item_due_choice::FORCE_NORMAL, true, 2, 1, 100001, 50000},
        1000000);

    EXPECT_TRUE(state.high_repayment_pending);
    EXPECT_EQ(first.event, ldp_work_item_forced_report_event::STARTED);

    auto repayment = ldp_work_item_priority_decision(true, true, -200000,
                                                      state.high_repayment_pending, 100000);
    EXPECT_EQ(repayment.choice, ldp_work_item_due_choice::REPAY_HIGH);
    ldp_work_item_apply_dispatch(
        state, {repayment.choice, true, 1, -1, 0, 60000}, 1000000);
    EXPECT_FALSE(state.high_repayment_pending);

    auto next = ldp_work_item_priority_decision(true, true, -200000,
                                                 state.high_repayment_pending, 100000);
    EXPECT_EQ(next.choice, ldp_work_item_due_choice::FORCE_NORMAL);
}

TEST(work_queue_dispatch, visits_all_overdue_normal_items_with_high_repayment)
{
    struct test_item {
        work_queue_if::id id;
        int32_t left;
        work_queue_if::priority priority;
    };
    constexpr work_queue_if::id high_id = 1;
    constexpr work_queue_if::id normal_a_id = 2;
    constexpr work_queue_if::id normal_b_id = 3;
    constexpr work_queue_if::id normal_c_id = 4;
    std::array<test_item, 4> items{{
        {high_id, -200000, work_queue_if::PRIORITY_HIGH},
        {normal_a_id, -130000, work_queue_if::PRIORITY_NORMAL},
        {normal_b_id, -120000, work_queue_if::PRIORITY_NORMAL},
        {normal_c_id, -110000, work_queue_if::PRIORITY_NORMAL},
    }};
    std::array<work_queue_if::id, 5> dispatched{};
    ldp_work_item_dispatch_state state{};

    for (size_t step = 0; step < dispatched.size(); ++step) {
        work_queue_if::id due_high_id = -1;
        work_queue_if::id due_normal_id = -1;
        int32_t due_high_left = INT32_MAX;
        int32_t due_normal_left = INT32_MAX;

        for (const auto &item : items) {
            if (item.left > 0) {
                continue;
            }
            if (item.priority == work_queue_if::PRIORITY_HIGH &&
                ldp_work_item_should_select(item.left, item.priority, due_high_left,
                                            work_queue_if::PRIORITY_HIGH,
                                            due_high_id >= 0)) {
                due_high_id = item.id;
                due_high_left = item.left;
            } else if (item.priority == work_queue_if::PRIORITY_NORMAL &&
                       ldp_work_item_should_select(item.left, item.priority,
                                                   due_normal_left,
                                                   work_queue_if::PRIORITY_NORMAL,
                                                   due_normal_id >= 0)) {
                due_normal_id = item.id;
                due_normal_left = item.left;
            }
        }

        auto decision = ldp_work_item_priority_decision(
            due_high_id >= 0, due_high_id >= 0, due_normal_left,
            state.high_repayment_pending, 100000);
        auto selected_id = decision.choice == ldp_work_item_due_choice::REPAY_HIGH
                                   ? due_high_id
                                   : due_normal_id;

        ASSERT_GE(selected_id, 0);
        dispatched[step] = selected_id;
        ldp_work_item_apply_dispatch(
            state,
            {decision.choice, true, selected_id, due_high_id,
             decision.choice == ldp_work_item_due_choice::FORCE_NORMAL
                     ? static_cast<uint64_t>(-static_cast<int64_t>(due_normal_left))
                     : 0u,
             step * 1000u},
            1000000);

        if (decision.choice == ldp_work_item_due_choice::FORCE_NORMAL) {
            auto selected = std::find_if(items.begin(), items.end(),
                                         [selected_id](const test_item &item) {
                                             return item.id == selected_id;
                                         });
            ASSERT_NE(selected, items.end());
            selected->left = 40000;
        }
    }

    EXPECT_THAT(dispatched,
                testing::ElementsAre(normal_a_id, high_id, normal_b_id, high_id,
                                     normal_c_id));
}

TEST(work_queue_dispatch, aggregates_forced_reports_inside_interval)
{
    ldp_work_item_dispatch_state state{};
    auto first = ldp_work_item_apply_dispatch(
        state,
        {ldp_work_item_due_choice::FORCE_NORMAL, true, 2, 1, 110000, 50000},
        1000000);
    ldp_work_item_apply_dispatch(
        state, {ldp_work_item_due_choice::REPAY_HIGH, true, 1, -1, 0, 60000},
        1000000);
    auto suppressed = ldp_work_item_apply_dispatch(
        state,
        {ldp_work_item_due_choice::FORCE_NORMAL, true, 2, 1, 130000, 500000},
        1000000);
    ldp_work_item_apply_dispatch(
        state, {ldp_work_item_due_choice::REPAY_HIGH, true, 1, -1, 0, 600000},
        1000000);
    auto summary = ldp_work_item_apply_dispatch(
        state,
        {ldp_work_item_due_choice::FORCE_NORMAL, true, 2, 1, 120000, 1050000},
        1000000);

    EXPECT_EQ(first.event, ldp_work_item_forced_report_event::STARTED);
    EXPECT_EQ(suppressed.event, ldp_work_item_forced_report_event::NONE);
    EXPECT_EQ(summary.event, ldp_work_item_forced_report_event::CONTINUING);
    EXPECT_EQ(summary.count, 2u);
    EXPECT_EQ(summary.total_count, 3u);
    EXPECT_EQ(summary.max_overdue_us, 130000u);
}

TEST(work_queue_overrun, reports_first_overrun_immediately)
{
    ldp_work_item_overrun_state state{};
    auto update = ldp_work_item_update_overrun(state, 10001, 10000, 50000, 1000000);

    EXPECT_EQ(update.event, ldp_work_item_overrun_event::STARTED);
    EXPECT_EQ(update.total_count, 1u);
    EXPECT_EQ(update.max_duration_us, 10001u);
}

TEST(work_queue_overrun, suppresses_continuing_report_inside_interval)
{
    ldp_work_item_overrun_state state{};
    ldp_work_item_update_overrun(state, 11000, 10000, 50000, 1000000);
    auto update = ldp_work_item_update_overrun(state, 12000, 10000, 1049999, 1000000);

    EXPECT_EQ(update.event, ldp_work_item_overrun_event::NONE);
    EXPECT_EQ(state.total_count, 2u);
}

TEST(work_queue_overrun, summarizes_continuing_episode_at_interval)
{
    ldp_work_item_overrun_state state{};
    ldp_work_item_update_overrun(state, 11000, 10000, 50000, 1000000);
    ldp_work_item_update_overrun(state, 12000, 10000, 500000, 1000000);
    auto update = ldp_work_item_update_overrun(state, 13000, 10000, 1050000, 1000000);

    EXPECT_EQ(update.event, ldp_work_item_overrun_event::CONTINUING);
    EXPECT_EQ(update.count, 2u);
    EXPECT_EQ(update.total_count, 3u);
    EXPECT_EQ(update.max_duration_us, 13000u);
}

TEST(work_queue_overrun, reports_recovery_after_one_stable_interval)
{
    ldp_work_item_overrun_state state{};
    ldp_work_item_update_overrun(state, 14000, 10000, 50000, 1000000);
    auto candidate = ldp_work_item_update_overrun(state, 10000, 10000, 60000, 1000000);
    auto early = ldp_work_item_update_overrun(state, 9000, 10000, 1059999, 1000000);
    auto recovery = ldp_work_item_update_overrun(state, 9000, 10000, 1060000, 1000000);
    auto stable = ldp_work_item_update_overrun(state, 9000, 10000, 1070000, 1000000);

    EXPECT_EQ(candidate.event, ldp_work_item_overrun_event::NONE);
    EXPECT_EQ(early.event, ldp_work_item_overrun_event::NONE);
    EXPECT_EQ(recovery.event, ldp_work_item_overrun_event::RECOVERED);
    EXPECT_EQ(recovery.total_count, 1u);
    EXPECT_EQ(recovery.max_duration_us, 14000u);
    EXPECT_EQ(stable.event, ldp_work_item_overrun_event::NONE);
    EXPECT_FALSE(state.active);
}

TEST(work_queue_overrun, overrun_cancels_pending_recovery)
{
    ldp_work_item_overrun_state state{};
    ldp_work_item_update_overrun(state, 14000, 10000, 50000, 1000000);
    ldp_work_item_update_overrun(state, 9000, 10000, 60000, 1000000);
    ldp_work_item_update_overrun(state, 12000, 10000, 500000, 1000000);
    auto premature = ldp_work_item_update_overrun(state, 9000, 10000, 1060000, 1000000);

    EXPECT_EQ(premature.event, ldp_work_item_overrun_event::NONE);
    EXPECT_TRUE(state.active);
}

TEST(work_queue_overrun, equal_duration_is_not_an_overrun)
{
    ldp_work_item_overrun_state state{};
    auto update = ldp_work_item_update_overrun(state, 10000, 10000, 50000, 1000000);

    EXPECT_EQ(update.event, ldp_work_item_overrun_event::NONE);
}
