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
#include <zephyr/posix/pthread.h>
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
		barrier_dmem_fence_full();
	}
	static inline void wmb()
	{
		barrier_dsync_fence_full();
		barrier_dmem_fence_full();
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

struct ze_rwlock {
	pthread_rwlock_t x_lock_;

	ze_rwlock()
	{
		pthread_rwlock_init(&x_lock_, nullptr);
	}

	void lock_shared()
	{
		pthread_rwlock_rdlock(&x_lock_);
	}
	void unlock_shared()
	{
		pthread_rwlock_unlock(&x_lock_);
	}
	void lock()
	{
		pthread_rwlock_wrlock(&x_lock_);
	}
	void unlock()
	{
		pthread_rwlock_unlock(&x_lock_);
	}
};

struct ldp_wq: public work_queue_if {
	using work_item = struct {
		void (*fn)(void *, void *);
		void *arg1;
		void *arg2;
		int id;
		int32_t cycle;
		int32_t left;
		uint64_t expect_start_us;
		uint64_t last_expect_start_us;
		uint64_t real_start_us;
		int32_t last_jitter_us;
		uint64_t sum_abs_jitter_us;
		uint32_t max_abs_jitter_us;
		uint32_t jitter_samples;
		uint32_t reset_gen;
	};
	using work_list = std::list<work_item>;

	virtual id enqueue(void (*fn)(void *, void *), void *arg1, void *arg2,
			   uint32_t cycle) override;
	virtual void reset(id wq, uint32_t cycle) override;
	virtual void reset_guarded(id wq, uint32_t cycle, id guard_wq,
				   uint32_t guard_threshold) override;
	virtual void cancel(id wq) override;
	virtual bool is_ready(id wq) override;
	virtual bool get_jitter(id wq, jitter_stats *stat) override;
	virtual bool reset_jitter(id wq) override;

	virtual void lock() override
	{
		x_lock_.lock();
	}

	virtual void unlock() override
	{
		x_lock_.unlock();
	}

	bool empty() const
	{
		return work_items_.empty();
	}
	void schedule();

	work_list work_items_;
	int next_id_;
	ze_mutex x_lock_;
	ze_sem work_sem_;
};

} // namespace systech::cif::zephyr
