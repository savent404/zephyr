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
