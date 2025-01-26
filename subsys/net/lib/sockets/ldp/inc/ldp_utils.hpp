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
	 * @brief reset the delay of a function
	 *
	 * @note this function is used to change/reset the delay of a function
	 * @param id id of the enqueued function
	 * @param delay new delay in microseconds
	 */
	virtual void reset(id id, uint32_t delay) = 0;

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
};

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
