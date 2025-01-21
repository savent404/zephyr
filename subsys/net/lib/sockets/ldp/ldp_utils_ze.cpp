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

	work_item wi = {fn, arg1, arg2, next_id_++, cycle, cycle};
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

void ldp_wq::reset(id wq, uint32_t cycle)
{
	std::lock_guard lock(x_lock_);
	auto it = std::find_if(work_items_.begin(), work_items_.end(),
			       [wq](const work_item &wi) { return wi.id == wq; });
	if (it != work_items_.end()) {
		it->cycle = cycle;
		it->left = cycle;
		LOG_DBG("Reset work item %d, cycle %d", wq, cycle);
	}
}

bool ldp_wq::is_ready(id wq)
{
	std::lock_guard lock(x_lock_);
	auto it = std::find_if(work_items_.begin(), work_items_.end(),
			       [wq](const work_item &wi) { return wi.id == wq; });
	if (it != work_items_.end()) {
		return it->left <= enclosed_cycle;
	}
	return false;
}

void ldp_wq::schedule()
{
	uint32_t free_time = UINT32_MAX;
	uint32_t curr_cycle, next_cycle, time_diff;

	if (work_items_.empty()) {
		work_sem_.take();
	}

	{
		std::lock_guard lock(x_lock_);
		/* Find the next work item based on the left time */
		for (auto &wi : work_items_) {
			if (wi.left < free_time) {
				free_time = wi.left;
			}
		}
	}

	/* Schedule the work item
	 * NOTE: Since the work item is scheduled by the software timer, the
	 *       free time is not accurate.
	 *       But according to the LDP protocol, All the request is scheduled by
	 *       ourself, so the bus didn't have any request at this point.
	 */
	free_time = ((free_time + enclosed_cycle - 1) / enclosed_cycle) * enclosed_cycle;

	if (free_time < prev_fn_cost_) {
		/* FIXME: bus cycle takes too long,
		 * but we still need give CPU some time...
		 */
		prev_fn_cost_ = 0;
	}
	k_usleep(free_time - prev_fn_cost_);

	{
		curr_cycle = k_cycle_get_32();
		std::lock_guard lock(x_lock_);
		/* Execute the work item */
		for (auto &wi : work_items_) {
			if (wi.left <= free_time) {
				wi.fn(wi.arg1, wi.arg2);
				wi.left = wi.cycle;
			} else {
				wi.left -= free_time;
			}
		}

		next_cycle = k_cycle_get_32();
	}

	/* Check if the work queue overrun */
	time_diff = next_cycle > curr_cycle ? next_cycle - curr_cycle
					    : UINT32_MAX - curr_cycle + next_cycle;
	prev_fn_cost_ = k_cyc_to_us_ceil32(time_diff);
	if (prev_fn_cost_ > enclosed_cycle) {
		LOG_WRN_ONCE("Work queue overrun %d", prev_fn_cost_);
	}
}
