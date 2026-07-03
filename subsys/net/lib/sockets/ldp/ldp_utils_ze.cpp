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

static uint64_t wq_now_us()
{
	return k_cyc_to_us_near64(k_cycle_get_64());
}

static uint64_t abs_i64(int64_t val)
{
	return val < 0 ? (uint64_t)-val : (uint64_t)val;
}

static void update_jitter(ldp_wq::work_item &wi, uint64_t real_start_us)
{
	int64_t jitter = (int64_t)real_start_us - (int64_t)wi.expect_start_us;
	uint64_t abs_jitter = abs_i64(jitter);

	wi.last_expect_start_us = wi.expect_start_us;
	wi.real_start_us = real_start_us;
	wi.last_jitter_us = (int32_t)jitter;
	wi.sum_abs_jitter_us += abs_jitter;
	if (abs_jitter > wi.max_abs_jitter_us) {
		wi.max_abs_jitter_us = (uint32_t)abs_jitter;
	}
	wi.jitter_samples++;
}

static void reset_jitter_stats(ldp_wq::work_item &wi, uint64_t now_us)
{
	wi.real_start_us = 0;
	wi.last_expect_start_us = 0;
	wi.last_jitter_us = 0;
	wi.sum_abs_jitter_us = 0;
	wi.max_abs_jitter_us = 0;
	wi.jitter_samples = 0;
	wi.expect_start_us = now_us + (wi.left > 0 ? (uint32_t)wi.left : 0);
}

static void reset_work_item(ldp_wq::work_item &wi, uint32_t cycle, uint64_t now_us)
{
	wi.cycle = (int32_t)cycle;
	wi.left = (int32_t)cycle;
	wi.expect_start_us = now_us + cycle;
	wi.reset_gen++;
}

ldp_wq::id ldp_wq::enqueue(void (*fn)(void *, void *), void *arg1, void *arg2, uint32_t cycle)
{
	std::lock_guard lock(x_lock_);
	bool is_empty = work_items_.empty();

	uint64_t now = wq_now_us();
	work_item wi = {fn, arg1, arg2, next_id_++, (int32_t)cycle, (int32_t)cycle,
			now + cycle, 0, 0, 0, 0, 0, 0, 0};
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
		uint64_t now = wq_now_us();

		reset_work_item(*it, cycle, now);
		LOG_DBG("Reset work item %d, cycle %d", wq, cycle);
	}
}

void ldp_wq::reset_guarded(id wq, uint32_t cycle, id guard_wq, uint32_t guard_threshold)
{
	std::lock_guard lock(x_lock_);
	auto it = std::find_if(work_items_.begin(), work_items_.end(),
			       [wq](const work_item &wi) { return wi.id == wq; });
	if (it == work_items_.end()) {
		return;
	}

	auto guard_it = std::find_if(work_items_.begin(), work_items_.end(),
				     [guard_wq](const work_item &wi) {
					     return wi.id == guard_wq;
				     });
	if (guard_it != work_items_.end() && guard_it->left > (int32_t)cycle &&
	    guard_it->left < (int32_t)guard_threshold) {
		cycle = (uint32_t)guard_it->left;
	}

	reset_work_item(*it, cycle, wq_now_us());
	LOG_DBG("Reset work item %d, cycle %d, guard %d", wq, cycle, guard_wq);
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

bool ldp_wq::get_jitter(id wq, jitter_stats *stat)
{
	std::lock_guard lock(x_lock_);
	auto it = std::find_if(work_items_.begin(), work_items_.end(),
			       [wq](const work_item &wi) { return wi.id == wq; });
	if (it == work_items_.end() || !stat) {
		return false;
	}

	uint32_t cycle = it->cycle > 0 ? (uint32_t)it->cycle : 0;
	uint32_t avg_abs = it->jitter_samples ?
		(uint32_t)(it->sum_abs_jitter_us / it->jitter_samples) : 0;

	stat->cycle_us = cycle;
	stat->samples = it->jitter_samples;
	stat->last_jitter_us = it->last_jitter_us;
	stat->avg_abs_jitter_us = avg_abs;
	stat->max_abs_jitter_us = it->max_abs_jitter_us;
	stat->avg_err_0p1ms = avg_abs / 100U;
	stat->max_err_0p1ms = it->max_abs_jitter_us / 100U;
	stat->expect_timestamp_us = it->last_expect_start_us;
	stat->real_timestamp_us = it->real_start_us;
	return true;
}

bool ldp_wq::reset_jitter(id wq)
{
	std::lock_guard lock(x_lock_);
	auto it = std::find_if(work_items_.begin(), work_items_.end(),
			       [wq](const work_item &wi) { return wi.id == wq; });
	if (it == work_items_.end()) {
		return false;
	}

	reset_jitter_stats(*it, wq_now_us());
	return true;
}

void ldp_wq::schedule()
{
	int32_t min_left = INT32_MAX;
	uint32_t sleepTime = 0;
	id early_id = -1;

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
				early_id = wi.id;
			}
		}

		if (min_left > 0 && early_id >= 0) {
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

		/*
		 * Re-find the work item by ID: cancel() or destroy() may have
		 * erased the list node between releasing the first lock above and
		 * re-acquiring it here, leaving any previously captured pointer
		 * dangling.  Looking up by ID is safe and avoids the
		 * use-after-free.
		 */
		auto it = std::find_if(work_items_.begin(), work_items_.end(),
				       [early_id](const work_item &wi) { return wi.id == early_id; });
		if (it == work_items_.end()) {
			return;
		}
		work_item *early_wi = &*it;

		/* wi might change its left/cycle in its callback, so
		 * we need to reset its left/cycle before calling it.
		 */
		early_wi->left = early_wi->cycle;
		uint32_t reset_gen = early_wi->reset_gen;
		curr = k_cycle_get_64();
		uint64_t real_start_us = k_cyc_to_us_near64(curr);
		update_jitter(*early_wi, real_start_us);
		early_wi->fn(early_wi->arg1, early_wi->arg2);
		after = k_cycle_get_64();
		duration = after - curr;
		duration = k_cyc_to_us_near64(duration);

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

		if (duration > (uint64_t)early_wi->cycle && early_wi->cycle > 0) {
			LOG_WRN("work item %d callback time %llu is longer than cycle %d",
				early_wi->id, duration, early_wi->cycle);
		}

		if (reset_gen == early_wi->reset_gen) {
			early_wi->expect_start_us = real_start_us +
				(early_wi->cycle > 0 ? (uint32_t)early_wi->cycle : 0);
		}

		for (auto &wi : work_items_) {
			if (early_wi->id == wi.id) {
				continue;
			}
			wi.left -= (uint32_t)duration + sleepTime;
		}
	}
}
