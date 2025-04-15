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

	work_item wi = {fn, arg1, arg2, next_id_++, (int32_t)cycle, (int32_t)cycle};
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
		it->cycle = (int32_t)cycle;
		it->left = (int32_t)cycle;
		LOG_DBG("Reset work item %d, cycle %d", wq, cycle);
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

void ldp_wq::schedule()
{
	int32_t min_left = INT32_MAX;
	uint32_t sleepTime = 0;
	work_item *early_wi = nullptr;

	if (work_items_.empty()) {
		work_sem_.take();
		return;
	}

	/* * Find the earliest work item and sleep until it is ready.
	 * If there are no work items, just yield the CPU.
	 */
	{
		std::lock_guard lock(x_lock_);
		for (auto &wi : work_items_) {
			if (wi.left < min_left) {
				min_left = wi.left;
				early_wi = &wi;
			}
		}

		if (min_left > 0 && early_wi) {
			for (auto &wi : work_items_) {
				wi.left -= min_left;
			}
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

		/* wi might change its left/cycle in its callback, so
		 * we need to reset its left/cycle before calling it.
		 */
		early_wi->left = early_wi->cycle;
		curr = k_cycle_get_64();
		early_wi->fn(early_wi->arg1, early_wi->arg2);
		after = k_cycle_get_64();
		duration = after - curr;

		__ASSERT(after >= curr,
			 "work item %d callback time overflow. curr: %llu, after: %llu",
			 early_wi->id, curr, after);

		/* FIXME: this is a workaround for the case that
		 * the timer overflows and the callback takes longer than
		 * the timer period. In this case, we need to reset the
		 * timer to 0, otherwise the timer will never be triggered
		 * again. This is not a good solution, but it works for now.
		 */
		if (after < curr) {
			LOG_WRN("work item %d callback time overflow. curr: %llu, after: %llu",
				early_wi->id, curr, after);
			duration = 0;
		}

		if (duration > (uint64_t)early_wi->cycle) {
			LOG_WRN("work item %d callback time %llu is longer than cycle %d",
				early_wi->id, duration, early_wi->cycle);
		}

		for (auto &wi : work_items_) {
			if (early_wi->id == wi.id) {
				continue;
			}
			wi.left -= (uint32_t)duration + sleepTime;
		}
	}
}
