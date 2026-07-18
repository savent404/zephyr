/**
 * @file ldp_utils.hpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @brief CIF/LDP stack utilities
 * @version 0.1
 * @date 2025-01-02
 *
 * @copyright Copyright SYSTech Co. 2025
 * @license This project is CLOSED SOURCE, All Rights Reserved
 *
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace systech
{
namespace cif
{

struct work_queue_if {
	using id = int32_t;
	enum priority : uint8_t {
		PRIORITY_NORMAL = 0,
		PRIORITY_HIGH = 1,
	};

	virtual ~work_queue_if()
	{
	}

	/**
	 * @brief enqueue a function to be called after a delay
	 *
	 * @param fn function to be called
	 * @param arg1 function argument(first)
	 * @param arg2 function argument(second)
	 * @param delay delay in microseconds
	 * @return id unique id of the enqueued function
	 */
	virtual id enqueue(void (*fn)(void *, void *), void *arg1, void *arg2, uint32_t delay) = 0;

	/**
	 * @brief reset the schedule of a function
	 *
	 * @note this function is used to change/reset both the recurring cycle and
	 * the delay before the next run of an enqueued function
	 * @param id id of the enqueued function
	 * @param cycle recurring cycle in microseconds after the next run
	 * @param delay delay in microseconds before the next run
	 */
	virtual void reset(id id, uint32_t cycle, uint32_t delay) = 0;

	virtual void set_priority(id wq, priority prio)
	{
		(void)wq;
		(void)prio;
	}

	/**
	 * @brief cancel a enqueued function
	 *
	 * @param id id of the enqueued function
	 */
	virtual void cancel(id id) = 0;

	/**
	 * @brief check if a function is ready to be called
	 *
	 * @note this function is used by high-priority tasks to check if a important
	 * function is ready to be called but be preempted by a him self. In this
	 * case, the high-priority task should yield the CPU to the function.
	 * @param id id of the enqueued function
	 * @return true if the function is ready to be called
	 * @return false if the function is not ready to be called
	 */
	virtual bool is_ready(id id) = 0;

	/* virtual locking functions */
	virtual void lock() = 0;
	virtual void unlock() = 0;
};

static inline bool ldp_work_item_should_select(int32_t candidate_left,
					       work_queue_if::priority candidate_priority,
					       int32_t selected_left,
					       work_queue_if::priority selected_priority,
					       bool selected_due)
{
	bool candidate_due = candidate_left <= 0;

	if (candidate_due && selected_due && candidate_priority != selected_priority) {
		return candidate_priority > selected_priority;
	}

	if (candidate_left <= 0) {
		return !selected_due || candidate_left < selected_left;
	}

	if (selected_due) {
		return false;
	}

	return candidate_left < selected_left;
}

static inline uint32_t ldp_work_item_priority_guard_us(uint32_t high_priority_cycle_us)
{
	auto guard_us = high_priority_cycle_us / 10u;

	return guard_us ? guard_us : 1u;
}

static inline bool ldp_work_item_should_defer_for_priority(
	work_queue_if::priority selected_priority, int32_t next_high_priority_left,
	work_queue_if::priority high_priority, uint32_t high_priority_cycle_us)
{
	if (selected_priority >= high_priority || next_high_priority_left <= 0) {
		return false;
	}

	return static_cast<uint32_t>(next_high_priority_left) <=
	       ldp_work_item_priority_guard_us(high_priority_cycle_us);
}

enum class ldp_work_item_due_choice : uint8_t {
	DEFAULT,
	FORCE_NORMAL,
	REPAY_HIGH,
};

struct ldp_work_item_due_decision {
	ldp_work_item_due_choice choice;
	bool clear_repayment;
};

static inline ldp_work_item_due_decision ldp_work_item_priority_decision(
	bool has_high, bool high_due, int32_t normal_left, bool repayment_pending,
	uint32_t max_normal_overdue_us)
{
	if (!has_high) {
		return {ldp_work_item_due_choice::DEFAULT, repayment_pending};
	}

	if (repayment_pending && high_due) {
		return {ldp_work_item_due_choice::REPAY_HIGH, false};
	}

	auto normal_overdue_us = normal_left <= 0 ? -static_cast<int64_t>(normal_left) : 0;
	if (high_due && normal_overdue_us > static_cast<int64_t>(max_normal_overdue_us)) {
		return {ldp_work_item_due_choice::FORCE_NORMAL, false};
	}

	return {ldp_work_item_due_choice::DEFAULT, false};
}

enum class ldp_work_item_forced_report_event : uint8_t {
	NONE,
	STARTED,
	CONTINUING,
};

struct ldp_work_item_forced_report_state {
	bool active = false;
	uint32_t total_count = 0;
	uint32_t interval_count = 0;
	uint64_t interval_max_overdue_us = 0;
	uint64_t last_report_us = 0;
};

struct ldp_work_item_dispatch_state {
	bool high_repayment_pending = false;
	ldp_work_item_forced_report_state forced_report;
};

struct ldp_work_item_dispatch_attempt {
	ldp_work_item_due_choice choice;
	bool dispatched;
	work_queue_if::id item_id;
	work_queue_if::id bypassed_high_id;
	uint64_t overdue_us;
	uint64_t now_us;
};

struct ldp_work_item_forced_report_update {
	ldp_work_item_forced_report_event event = ldp_work_item_forced_report_event::NONE;
	uint32_t count = 0;
	uint32_t total_count = 0;
	uint64_t max_overdue_us = 0;
	work_queue_if::id item_id = -1;
	work_queue_if::id bypassed_high_id = -1;
};

static inline ldp_work_item_forced_report_update ldp_work_item_apply_dispatch(
	ldp_work_item_dispatch_state &state, const ldp_work_item_dispatch_attempt &attempt,
	uint64_t report_interval_us)
{
	if (!attempt.dispatched) {
		return {};
	}

	if (attempt.choice == ldp_work_item_due_choice::REPAY_HIGH) {
		state.high_repayment_pending = false;
		return {};
	}

	if (attempt.choice != ldp_work_item_due_choice::FORCE_NORMAL) {
		return {};
	}

	state.high_repayment_pending = true;
	auto &report = state.forced_report;
	if (!report.active) {
		report.active = true;
		report.total_count = 1;
		report.last_report_us = attempt.now_us;
		return {ldp_work_item_forced_report_event::STARTED, 1, 1,
			attempt.overdue_us, attempt.item_id, attempt.bypassed_high_id};
	}

	if (report.total_count != UINT32_MAX) {
		++report.total_count;
	}
	if (report.interval_count != UINT32_MAX) {
		++report.interval_count;
	}
	report.interval_max_overdue_us =
		std::max(report.interval_max_overdue_us, attempt.overdue_us);

	if (attempt.now_us >= report.last_report_us &&
	    attempt.now_us - report.last_report_us >= report_interval_us) {
		ldp_work_item_forced_report_update update{
			ldp_work_item_forced_report_event::CONTINUING,
			report.interval_count,
			report.total_count,
			report.interval_max_overdue_us,
			attempt.item_id,
			attempt.bypassed_high_id,
		};
		report.interval_count = 0;
		report.interval_max_overdue_us = 0;
		report.last_report_us = attempt.now_us;
		return update;
	}

	return {};
}

enum class ldp_work_item_overrun_event : uint8_t {
	NONE,
	STARTED,
	CONTINUING,
	RECOVERED,
};

struct ldp_work_item_overrun_state {
	bool active = false;
	bool recovery_pending = false;
	uint32_t total_count = 0;
	uint32_t interval_count = 0;
	uint64_t episode_max_duration_us = 0;
	uint64_t interval_max_duration_us = 0;
	uint64_t last_report_us = 0;
	uint64_t recovery_start_us = 0;
};

struct ldp_work_item_overrun_update {
	ldp_work_item_overrun_event event = ldp_work_item_overrun_event::NONE;
	uint32_t count = 0;
	uint32_t total_count = 0;
	uint64_t max_duration_us = 0;
};

static inline ldp_work_item_overrun_update ldp_work_item_update_overrun(
	ldp_work_item_overrun_state &state, uint64_t duration_us, uint32_t cycle_us,
	uint64_t now_us, uint64_t report_interval_us)
{
	if (cycle_us == 0) {
		return {};
	}

	if (duration_us <= cycle_us) {
		if (!state.active) {
			return {};
		}
		if (!state.recovery_pending) {
			state.recovery_pending = true;
			state.recovery_start_us = now_us;
			return {};
		}
		if (now_us < state.recovery_start_us ||
		    now_us - state.recovery_start_us < report_interval_us) {
			return {};
		}

		ldp_work_item_overrun_update update{ldp_work_item_overrun_event::RECOVERED,
						     state.total_count, state.total_count,
						     state.episode_max_duration_us};
		state = {};
		return update;
	}
	state.recovery_pending = false;

	if (!state.active) {
		state.active = true;
		state.total_count = 1;
		state.episode_max_duration_us = duration_us;
		state.last_report_us = now_us;
		return {ldp_work_item_overrun_event::STARTED, 1, 1, duration_us};
	}

	if (state.total_count != UINT32_MAX) {
		++state.total_count;
	}
	if (state.interval_count != UINT32_MAX) {
		++state.interval_count;
	}
	state.episode_max_duration_us = std::max(state.episode_max_duration_us, duration_us);
	state.interval_max_duration_us = std::max(state.interval_max_duration_us, duration_us);

	if (now_us >= state.last_report_us &&
	    now_us - state.last_report_us >= report_interval_us) {
		ldp_work_item_overrun_update update{ldp_work_item_overrun_event::CONTINUING,
						     state.interval_count, state.total_count,
						     state.interval_max_duration_us};
		state.interval_count = 0;
		state.interval_max_duration_us = 0;
		state.last_report_us = now_us;
		return update;
	}

	return {};
}

/**
 * @brief Cache interface
 *
 * @tparam T implementation of the cache
 */
template <typename T> struct ldp_cache {
	static inline void rmb()
	{
		T::rmb();
	}
	static inline void wmb()
	{
		T::wmb();
	}
};

template <typename T> struct ldp_mempool {
	static inline void *alloc(size_t size)
	{
		return T::alloc(size);
	}
	static inline void free(void *ptr)
	{
		T::free(ptr);
	}
};

struct ldp_memcpy {
	static inline void memcpy(void *dst, const void *src, size_t len)
	{
		/* Make sure the copy is 4-byte aligned, if len is not 4-byte aligned,
		 * load the last 4-byte data and override its 1~3 bytes then write back */
		auto d = static_cast<volatile uint32_t *>(dst);
		auto s = static_cast<const volatile uint32_t *>(src);
		auto l = len / 4;
		auto r = len % 4;

		for (size_t i = 0; i < l; i++) {
			d[i] = s[i];
		}

		if (r) {
			volatile uint32_t w = d[l];
			volatile uint32_t v = s[l];

			/* override partial data */
			for (size_t i = 0; i < r; i++) {
				w &= ~(0xFF << (i * 8));
				w |= (v & (0xFF << (i * 8)));
			}

			/* write back (4-byte aligned) */
			d[l] = w;
		}
	}
};

} // namespace cif
} // namespace systech
