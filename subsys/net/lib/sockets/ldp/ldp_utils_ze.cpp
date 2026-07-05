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

	work_item wi = {fn, arg1, arg2, next_id_++, (int32_t)cycle, (int32_t)cycle, 0,
			PRIORITY_NORMAL};
	work_items_.push_back(wi);
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

void ldp_wq::schedule()
{
	int32_t min_left = INT32_MAX;
	uint32_t sleepTime = 0;
	id early_id = -1;
	bool wait_for_work = false;
	bool selected_due = false;
	priority selected_priority = PRIORITY_NORMAL;
	auto select_next_item = [&]() {
		int32_t next_high_priority_left = INT32_MAX;
		uint32_t next_high_priority_cycle = 0;

		min_left = INT32_MAX;
		early_id = -1;
		selected_due = false;
		selected_priority = PRIORITY_NORMAL;

		for (auto &wi : work_items_) {
			if (wi.prio == PRIORITY_HIGH && wi.left > 0 &&
			    wi.left < next_high_priority_left) {
				next_high_priority_left = wi.left;
				next_high_priority_cycle =
					static_cast<uint32_t>(std::max(wi.cycle, 1));
			}

			if (ldp_work_item_should_select(wi.left, wi.prio, min_left,
							selected_priority, selected_due)) {
				min_left = wi.left;
				early_id = wi.id;
				selected_due = wi.left <= 0;
				selected_priority = wi.prio;
			}
		}

		if (early_id >= 0 && min_left <= 0 &&
		    next_high_priority_left != INT32_MAX &&
		    ldp_work_item_should_defer_for_priority(selected_priority,
							    next_high_priority_left,
							    PRIORITY_HIGH,
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
			wait_for_work = true;
		}
	}

	if (wait_for_work) {
		work_sem_.take();
		std::lock_guard lock(x_lock_);
		last_update_us_ = now_us();
		return;
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
	}

	if (sleepTime) {
		k_usleep(sleepTime);
		return;
	}

	{
		std::lock_guard lock(x_lock_);
		uint64_t curr, after, duration;

		account_elapsed(now_us());

		select_next_item();

		if (early_id < 0 || min_left > 0) {
			if (min_left > 0 && min_left != INT32_MAX) {
				sleepTime = static_cast<uint32_t>(min_left);
			}
		} else {
			/* Re-find by ID: cancel() or destroy() may have erased the list node. */
			auto it = std::find_if(work_items_.begin(), work_items_.end(),
					       [early_id](const work_item &wi) {
						       return wi.id == early_id;
					       });
			if (it == work_items_.end()) {
				return;
			}

			auto fn = it->fn;
			auto arg1 = it->arg1;
			auto arg2 = it->arg2;
			auto revision = it->revision;
			auto carry_left = std::min(it->left, 0);

			/* Callback may reset or cancel this item, so don't keep a list pointer. */
			it->left = it->cycle;
			curr = now_us();
			fn(arg1, arg2);
			after = now_us();
			duration = after >= curr ? after - curr : 0;
			duration = std::min<uint64_t>(duration, INT32_MAX);
			last_update_us_ = after;

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

			auto current_it =
				std::find_if(work_items_.begin(), work_items_.end(),
					     [early_id](const work_item &wi) {
						     return wi.id == early_id;
					     });
			if (current_it != work_items_.end() && current_it->revision == revision) {
				if (duration > (uint64_t)current_it->cycle &&
				    current_it->cycle > 0) {
					LOG_WRN("work item %d callback time %llu is longer than cycle %d",
						current_it->id, duration, current_it->cycle);
				}

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
		k_usleep(sleepTime);
	}
}
