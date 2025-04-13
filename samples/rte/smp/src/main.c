/*
 * Copyright (c) 2025 SYSFly Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <zephyr/arch/arm/asm_inline.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#if CONFIG_FPU_SHARING
#error "arch_switch() on ARMv7-A is not support FPU share"
#endif

int main(void)
{
	printk("Sample thread started\n");
	k_sleep(K_FOREVER);

	return 0;
}

static void time_waste(void)
{
	for (int i = 0; i < 0xFFFFFFF; i++) {
		__asm volatile("nop");
	}
}

K_SEM_DEFINE(s1, 0, 1);
K_SEM_DEFINE(s2, 0, 1);

static void thread1(void)
{
	while (1) {
		LOG_INF("Thread 1");
		k_sem_give(&s1);
		k_sleep(K_MSEC(1000));
	}
}
static void thread2(void)
{
	while (1) {
		k_sem_take(&s1, K_FOREVER);
		LOG_INF("Thread 2");
		k_sem_give(&s2);
		k_sleep(K_MSEC(1000));
	}
}
static void thread3(void)
{
	while (1) {
		k_sem_take(&s2, K_FOREVER);
		LOG_INF("Thread 3");
		k_sleep(K_MSEC(1000));
	}
}

static void thread_spin(void *arg)
{
	const char *name = arg;

	while (1) {
		LOG_INF("Thread spin %s", name);
		time_waste();
	}
}

K_THREAD_DEFINE(t1, 2048, thread1, NULL, NULL, NULL, 7, 0, 200);
K_THREAD_DEFINE(t2, 2048, thread2, NULL, NULL, NULL, 7, 0, 200);
K_THREAD_DEFINE(t3, 2048, thread3, NULL, NULL, NULL, 7, 0, 200);

#if CONFIG_SMP && CONFIG_MP_MAX_NUM_CPUS > 1
K_THREAD_DEFINE(spin1, 2048, thread_spin, "1", NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
		200);
#endif

#if CONFIG_SMP && CONFIG_MP_MAX_NUM_CPUS > 2
K_THREAD_DEFINE(spin2, 2048, thread_spin, "2", NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
		200);
#endif

#if CONFIG_SMP && CONFIG_MP_MAX_NUM_CPUS > 3
K_THREAD_DEFINE(spin3, 2048, thread_spin, "3", NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
		200);
#endif

/* This is a workaround for the bit-flip issue of the DDR being
 * uninitialized in the early boot stage. This function is called before
 * the kernel is initialized, so it should not use any kernel APIs.
 */
void z_early_memset(void *dst, int c, size_t n)
{
	uint32_t aligned_size = n / sizeof(uint32_t);
	uint32_t *dst32 = (uint32_t *)dst;
	uint8_t *dst8 = (uint8_t *)dst;
	uint32_t i;

	for (i = 0; i < aligned_size; i++) {
		dst32[i] = c;
	}

	for (i = aligned_size * sizeof(uint32_t); i < n; i++) {
		dst8[i] = c;
	}
}
