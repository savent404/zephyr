/**
 * @brief SMP initialization for FMSH SoC
 *
 * Copyright (c) 2024 SYSFly Co.
 * @param core
 * @param entrypoint
 */

#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/kernel/smp.h>
#include <zephyr/init.h>
#include "soc.h"

/* System Level Control Registers (SLCR) */
#define SLCR_BASE            0xE0026000
#define SLCR_UNLOCK_KEY      0xDF0D767B
#define REG_SLCR_LOCK        0x0004
#define REG_SLCR_UNLOCK      0x0008
#define REG_SLCR_CPU_STATE   0x0400
#define REG_SLCR_CORE0_ENTRY 0x0438

#if CONFIG_SMP

static void soc_cpu_start(uint8_t core, uint32_t entrypoint)
{
	uint32_t reg_cpu_entry;
	uint32_t cpu_state;

	reg_cpu_entry = SLCR_BASE + REG_SLCR_CORE0_ENTRY + core * 8;
	if (!entrypoint || (entrypoint & 0x3)) {
		return;
	}

	/* Unlock SLCR */
	sys_write32(SLCR_UNLOCK_KEY, SLCR_BASE + REG_SLCR_UNLOCK);

	/* Setup cpu entry */
	sys_write32(entrypoint, reg_cpu_entry);

	/* Unlock cpu state */
	cpu_state = sys_read32(SLCR_BASE + REG_SLCR_CPU_STATE);
	cpu_state &= ~BIT(core);
	sys_write32(cpu_state, SLCR_BASE + REG_SLCR_CPU_STATE);

	/* Invalidate cache */
	sys_cache_data_flush_and_invd_all();
	sys_cache_instr_flush_and_invd_all();

	/* wmb */
	barrier_dsync_fence_full();

	/* isb */
	barrier_isync_fence_full();

	__asm__ volatile("sev" : : : "memory");
}

__attribute__((naked)) static void soc_secondary_cpu_entry(void)
{
	__asm volatile("bl z_arm_reset");
}

extern void z_arm_reset(void);
extern void z_smp_init(void);

int soc_smp_init(void)
{
	for (int i = 1; i < CONFIG_MP_MAX_NUM_CPUS; i++) {
		soc_cpu_start(i, (uint32_t)soc_secondary_cpu_entry);
		k_busy_wait(1);
	}
	z_smp_init();
	return 0;
}

SYS_INIT(soc_smp_init, SMP, 99);

#endif /* CONFIG_SMP */
