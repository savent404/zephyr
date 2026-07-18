/**
 * Copyright (c) 2025 SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <mutex>
#include <algorithm>
#include <time.h>
#include "ldp_utils_ze.hpp"

#if !(CONFIG_STD_CPP20)
#error "C++20 is required, please set CONFIG_STD_CPP20=y"
#endif

LOG_MODULE_REGISTER(ldp_utils);

using namespace systech::cif::zephyr;

ldp_wq::id ldp_wq::enqueue(void (*fn)(void *, void *), void *arg1, void *arg2, uint32_t cycle)
{
	std::lock_guard lock(x_lock_);
	bool is_empty = work_items_.empty();

	work_item wi = {fn, arg1, arg2, next_id_++, (int32_t)cycle, (int32_t)cycle,
			0,  PRIORITY_NORMAL, {}};
	work_items_.push_back(wi);
	notify_schedule_update();
	LOG_INF("Enqueue work item %d", wi.id);

	if (is_empty) {
		work_sem_.give();
	}
	return wi.id;
}

void ldp_wq::cancel(id wq)
{
	std::lock_guard lock(x_lock_);
	auto it = std::find_if(work_items_.begin(), work_items_.end(),
			       [wq](const work_item &wi) { return wi.id == wq; });
	if (it != work_items_.end()) {
		work_items_.erase(it);
		notify_schedule_update();
		LOG_INF("Cancel work item %d", wq);
	}
}

void ldp_wq::reset(id wq, uint32_t cycle, uint32_t delay)
{
	std::lock_guard lock(x_lock_);
	auto it = std::find_if(work_items_.begin(), work_items_.end(),
			       [wq](const work_item &wi) { return wi.id == wq; });
	if (it != work_items_.end()) {
		it->cycle = (int32_t)cycle;
		it->left = (int32_t)delay;
		it->revision++;
		notify_schedule_update();
		LOG_DBG("Reset work item %d, cycle %d, delay %d", wq, cycle, delay);
	}
}

void ldp_wq::set_priority(id wq, priority prio)
{
	std::lock_guard lock(x_lock_);
	auto it = std::find_if(work_items_.begin(), work_items_.end(),
			       [wq](const work_item &wi) { return wi.id == wq; });
	if (it != work_items_.end()) {
		it->prio = prio;
		notify_schedule_update();
	}
}

bool ldp_wq::is_ready(id wq)
{
	std::lock_guard lock(x_lock_);
	auto it = std::find_if(work_items_.begin(), work_items_.end(),
			       [wq](const work_item &wi) { return wi.id == wq; });
	if (it != work_items_.end()) {
		return it->left <= 0;
	}
	return false;
}

uint64_t ldp_wq::now_us() const
{
	return k_ticks_to_us_near64(k_uptime_ticks());
}

void ldp_wq::account_elapsed(uint64_t now_us)
{
	if (!last_update_us_) {
		last_update_us_ = now_us;
		return;
	}

	if (now_us <= last_update_us_) {
		return;
	}

	auto elapsed_us = std::min<uint64_t>(now_us - last_update_us_, INT32_MAX);
	last_update_us_ = now_us;

	for (auto &wi : work_items_) {
		auto next_left = static_cast<int64_t>(wi.left) - static_cast<int64_t>(elapsed_us);
		if (next_left < INT32_MIN) {
			wi.left = INT32_MIN;
		} else {
			wi.left = static_cast<int32_t>(next_left);
		}
	}
}

void ldp_wq::notify_schedule_update()
{
	work_sem_.give();
}

ldp_wq_diagnostics ldp_wq::schedule()
{
	ldp_wq_diagnostics diagnostics{};
	int32_t min_left = INT32_MAX;
	uint32_t sleepTime = 0;
	id early_id = -1;
	bool wait_for_work = false;
	bool selected_due = false;
	bool forced_normal = false;
	id bypassed_high_id = -1;
	ldp_work_item_due_choice due_choice = ldp_work_item_due_choice::DEFAULT;
	priority selected_priority = PRIORITY_NORMAL;
	auto select_next_item = [&]() {
		int32_t next_high_priority_left = INT32_MAX;
		uint32_t next_high_priority_cycle = 0;
		int32_t due_high_left = INT32_MAX;
		int32_t due_normal_left = INT32_MAX;
		id due_high_id = -1;
		id due_normal_id = -1;
		bool has_high = false;

		min_left = INT32_MAX;
		early_id = -1;
		selected_due = false;
		forced_normal = false;
		bypassed_high_id = -1;
		due_choice = ldp_work_item_due_choice::DEFAULT;
		selected_priority = PRIORITY_NORMAL;

		for (auto &wi : work_items_) {
			if (wi.prio == PRIORITY_HIGH) {
				has_high = true;
				if (wi.left <= 0 &&
				    (due_high_id < 0 || wi.left < due_high_left)) {
					due_high_left = wi.left;
					due_high_id = wi.id;
				} else if (wi.left > 0 && wi.left < next_high_priority_left) {
					next_high_priority_left = wi.left;
					next_high_priority_cycle =
						static_cast<uint32_t>(std::max(wi.cycle, 1));
				}
			} else if (wi.left <= 0 &&
				   (due_normal_id < 0 || wi.left < due_normal_left)) {
				due_normal_left = wi.left;
				due_normal_id = wi.id;
			}

			if (ldp_work_item_should_select(wi.left, wi.prio, min_left,
							selected_priority, selected_due)) {
				min_left = wi.left;
				early_id = wi.id;
				selected_due = wi.left <= 0;
				selected_priority = wi.prio;
			}
		}

		auto decision = ldp_work_item_priority_decision(
			has_high, due_high_id >= 0, due_normal_left,
			dispatch_state_.high_repayment_pending,
			CONFIG_CIF_WQ_NORMAL_MAX_OVERDUE_US);
		if (decision.clear_repayment) {
			dispatch_state_.high_repayment_pending = false;
		}

		due_choice = decision.choice;
		forced_normal = decision.choice == ldp_work_item_due_choice::FORCE_NORMAL;
		if (forced_normal) {
			early_id = due_normal_id;
			min_left = due_normal_left;
			selected_due = true;
			selected_priority = PRIORITY_NORMAL;
			bypassed_high_id = due_high_id;
		} else if (decision.choice == ldp_work_item_due_choice::REPAY_HIGH) {
			early_id = due_high_id;
			min_left = due_high_left;
			selected_due = true;
			selected_priority = PRIORITY_HIGH;
		}

		if (!forced_normal && early_id >= 0 && min_left <= 0 &&
		    next_high_priority_left != INT32_MAX &&
		    ldp_work_item_should_defer_for_priority(selected_priority,
							    next_high_priority_left, PRIORITY_HIGH,
							    next_high_priority_cycle)) {
			min_left = next_high_priority_left;
			early_id = -1;
			selected_due = false;
			selected_priority = PRIORITY_HIGH;
		}
	};

	{
		std::lock_guard lock(x_lock_);
		if (work_items_.empty()) {
			last_update_us_ = 0;
			dispatch_state_ = {};
			wait_for_work = true;
		}
	}

	if (wait_for_work) {
		work_sem_.take();
		std::lock_guard lock(x_lock_);
		last_update_us_ = now_us();
		return diagnostics;
	}

	/* Find the earliest work item after accounting actual elapsed time. */
	{
		std::lock_guard lock(x_lock_);
		account_elapsed(now_us());

		select_next_item();

		if (min_left > 0 && early_id >= 0) {
			sleepTime = static_cast<uint32_t>(min_left);
		} else if (min_left > 0 && early_id < 0 && min_left != INT32_MAX) {
			sleepTime = static_cast<uint32_t>(min_left);
		}
		if (sleepTime) {
			work_sem_.reset();
		}
	}

	if (sleepTime) {
		work_sem_.take(sleepTime);
		return diagnostics;
	}

	{
		std::lock_guard lock(x_lock_);
		uint64_t curr, after, duration;

		account_elapsed(now_us());

		select_next_item();

		if (early_id < 0 || min_left > 0) {
			if (min_left > 0 && min_left != INT32_MAX) {
				sleepTime = static_cast<uint32_t>(min_left);
				work_sem_.reset();
			}
		} else {
			/* Re-find by ID: cancel() or destroy() may have erased the list node. */
			auto it = std::find_if(
				work_items_.begin(), work_items_.end(),
				[early_id](const work_item &wi) { return wi.id == early_id; });
			if (it == work_items_.end()) {
				return diagnostics;
			}

			auto overdue_us = forced_normal
						  ? static_cast<uint64_t>(-static_cast<int64_t>(min_left))
						  : 0u;
			auto dispatch_update = ldp_work_item_apply_dispatch(
				dispatch_state_,
				{due_choice, true, early_id, bypassed_high_id, overdue_us, now_us()},
				static_cast<uint64_t>(CONFIG_CIF_WQ_DIAGNOSTIC_REPORT_INTERVAL_MS) *
					1000u);
			if (dispatch_update.event == ldp_work_item_forced_report_event::STARTED) {
				diagnostics.add({ldp_wq_diagnostic_type::FORCED_STARTED,
						 dispatch_update.item_id,
						 dispatch_update.bypassed_high_id,
						 dispatch_update.count,
						 dispatch_update.total_count,
						 0,
						 dispatch_update.max_overdue_us,
						 dispatch_update.max_overdue_us});
			} else if (dispatch_update.event ==
				   ldp_work_item_forced_report_event::CONTINUING) {
				diagnostics.add({ldp_wq_diagnostic_type::FORCED_CONTINUING,
						 dispatch_update.item_id,
						 dispatch_update.bypassed_high_id,
						 dispatch_update.count,
						 dispatch_update.total_count,
						 0,
						 dispatch_update.max_overdue_us,
						 dispatch_update.max_overdue_us});
			}

			auto fn = it->fn;
			auto arg1 = it->arg1;
			auto arg2 = it->arg2;
			auto revision = it->revision;
			auto carry_left = std::min(it->left, 0);
			auto dispatch_cycle = it->cycle > 0 ? static_cast<uint32_t>(it->cycle) : 0u;
			auto overrun = it->overrun;

			/* Callback may reset or cancel this item, so don't keep a list pointer. */
			it->left = it->cycle;
			curr = now_us();
			fn(arg1, arg2);
			after = now_us();
			duration = after >= curr ? after - curr : 0;
			duration = std::min<uint64_t>(duration, INT32_MAX);
			last_update_us_ = after;

			auto overrun_update = ldp_work_item_update_overrun(
				overrun, duration, dispatch_cycle, after,
				static_cast<uint64_t>(CONFIG_CIF_WQ_DIAGNOSTIC_REPORT_INTERVAL_MS) *
					1000u);
			switch (overrun_update.event) {
			case ldp_work_item_overrun_event::STARTED:
				diagnostics.add({ldp_wq_diagnostic_type::OVERRUN_STARTED, early_id, -1,
						 overrun_update.count, overrun_update.total_count,
						 dispatch_cycle, duration,
						 overrun_update.max_duration_us});
				break;
			case ldp_work_item_overrun_event::CONTINUING:
				diagnostics.add({ldp_wq_diagnostic_type::OVERRUN_CONTINUING, early_id,
						 -1, overrun_update.count,
						 overrun_update.total_count, dispatch_cycle, duration,
						 overrun_update.max_duration_us});
				break;
			case ldp_work_item_overrun_event::RECOVERED:
				diagnostics.add({ldp_wq_diagnostic_type::OVERRUN_RECOVERED, early_id,
						 -1, overrun_update.count,
						 overrun_update.total_count, dispatch_cycle, duration,
						 overrun_update.max_duration_us});
				break;
			case ldp_work_item_overrun_event::NONE:
				break;
			}

			if (duration > 0) {
				for (auto &wi : work_items_) {
					if (early_id == wi.id) {
						continue;
					}

					auto next_left = static_cast<int64_t>(wi.left) -
							 static_cast<int64_t>(duration);
					if (next_left < INT32_MIN) {
						wi.left = INT32_MIN;
					} else {
						wi.left = static_cast<int32_t>(next_left);
					}
				}
			}

			auto current_it = std::find_if(
				work_items_.begin(), work_items_.end(),
				[early_id](const work_item &wi) { return wi.id == early_id; });
			if (current_it != work_items_.end()) {
				current_it->overrun = overrun;
			}
			if (current_it != work_items_.end() && current_it->revision == revision) {
				auto next_left = static_cast<int64_t>(current_it->cycle) +
						 carry_left - static_cast<int64_t>(duration);
				if (next_left <= 0) {
					current_it->left = 0;
				} else if (next_left > INT32_MAX) {
					current_it->left = INT32_MAX;
				} else {
					current_it->left = static_cast<int32_t>(next_left);
				}
			}
		}
	}

	if (sleepTime) {
		work_sem_.take(sleepTime);
	}

	return diagnostics;
}

void ldp_wq::report_diagnostics(const ldp_wq_diagnostics &diagnostics)
{
	for (size_t i = 0; i < diagnostics.count; ++i) {
		const auto &diagnostic = diagnostics.entries[i];

		switch (diagnostic.type) {
		case ldp_wq_diagnostic_type::FORCED_STARTED:
			LOG_WRN("work item %d forced after %llu us overdue (limit %d us); "
				"deferring high-priority item %d once",
				diagnostic.item_id, diagnostic.value_us,
				CONFIG_CIF_WQ_NORMAL_MAX_OVERDUE_US, diagnostic.bypassed_high_id);
			break;
		case ldp_wq_diagnostic_type::FORCED_CONTINUING:
			LOG_WRN("normal work starvation continues: %u forced dispatches since "
				"last report, %u total, max overdue=%llu us",
				diagnostic.count, diagnostic.total_count, diagnostic.max_value_us);
			break;
		case ldp_wq_diagnostic_type::OVERRUN_STARTED:
			LOG_WRN("work item %d overrun: callback=%llu us cycle=%u us "
				"excess=%llu us; cycle or handler workload is unreasonable",
				diagnostic.item_id, diagnostic.value_us, diagnostic.cycle_us,
				diagnostic.value_us - diagnostic.cycle_us);
			break;
		case ldp_wq_diagnostic_type::OVERRUN_CONTINUING:
			LOG_WRN("work item %d overrun continues: %u since last report, "
				"%u total, max callback=%llu us cycle=%u us",
				diagnostic.item_id, diagnostic.count, diagnostic.total_count,
				diagnostic.max_value_us, diagnostic.cycle_us);
			break;
		case ldp_wq_diagnostic_type::OVERRUN_RECOVERED:
			LOG_INF("work item %d overrun recovered: callback=%llu us cycle=%u us, "
				"%u overruns, max callback=%llu us",
				diagnostic.item_id, diagnostic.value_us, diagnostic.cycle_us,
				diagnostic.total_count, diagnostic.max_value_us);
			break;
		}
	}
}
