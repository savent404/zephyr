/**
 * @file ldp_utils_ze.hpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @brief Utilities for LDP Zephyr interface
 * @version 0.1
 * @date 2025-01-05
 *
 * @copyright Copyright (c) 2025 SYSTech Co.
 *
 */
#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <list>

#include "ldp/inc/ldp_utils.hpp"

namespace systech::cif::zephyr
{
struct ldp_mem_slab {
	static inline void *alloc(size_t size)
	{
		return k_malloc(size);
	}
	static inline void free(void *ptr)
	{
		k_free(ptr);
	}
};

struct ldp_cache_if {
	static inline void rmb()
	{
		barrier_dsync_fence_full();
	}
	static inline void wmb()
	{
		barrier_dsync_fence_full();
	}
};

struct ze_sem {
	k_sem sem_;
	ze_sem()
	{
		k_sem_init(&sem_, 0, 1);
	}
	void give()
	{
		k_sem_give(&sem_);
	}
	void take()
	{
		k_sem_take(&sem_, K_FOREVER);
	}
};

struct ze_mutex {
	k_mutex x_lock_;
	ze_mutex()
	{
		k_mutex_init(&x_lock_);
	}
	void lock()
	{
		k_mutex_lock(&x_lock_, K_FOREVER);
	}
	void unlock()
	{
		k_mutex_unlock(&x_lock_);
	}
};

struct ldp_wq: public work_queue_if {
	using work_item = struct {
		void (*fn)(void *, void *);
		void *arg1;
		void *arg2;
		int id;
		uint32_t cycle;
		uint32_t left;
	};
	using work_list = std::list<work_item>;

	virtual id enqueue(void (*fn)(void *, void *), void *arg1, void *arg2,
			   uint32_t cycle) override;
	virtual void reset(id wq, uint32_t cycle) override;
	virtual void cancel(id wq) override;
	virtual bool is_ready(id wq) override;
	void schedule();

	work_list work_items_;
	int next_id_;
	ze_mutex x_lock_;
	ze_sem work_sem_;
	uint32_t prev_fn_cost_ = 0;
	static inline const uint32_t enclosed_cycle = 1000; /* 1ms */
};

} // namespace systech::cif::zephyr
