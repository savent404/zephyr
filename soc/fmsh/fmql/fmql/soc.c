/*
 * Copyright (c) 2021 Weidmueller Interface GmbH & Co. KG
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/arch/cache.h>

#include <cmsis_core.h>
#include <zephyr/arch/arm/mmu/arm_mmu.h>
#include "soc.h"

/* System Level Control Registers (SLCR) */
#define SLCR_UNLOCK     0x0008
#define SLCR_UNLOCK_KEY 0xdf0d

#define M_DEVICE_UNALIGNED (MT_NORMAL | MPERM_R | MPERM_W | MATTR_SHARED)
#define M_DEVICE           (MT_DEVICE | MPERM_R | MPERM_W | MATTR_SHARED)
#define M_MEMORY           (MT_NORMAL | MPERM_R | MPERM_W | MPERM_X | MATTR_SHARED)
#define M_MEMORY_UNCACHED  (MT_STRONGLY_ORDERED | MPERM_R | MPERM_X | MATTR_SHARED)

static const struct arm_mmu_region mmu_regions[] = {

	/* Relocate vector address to 0x00000000, possible sram is ddr or ocm */
	MMU_REGION_ENTRY("vector", DT_REG_ADDR(DT_CHOSEN(zephyr_sram)), 0x00000000,
			 CONFIG_MMU_PAGE_SIZE, M_MEMORY_UNCACHED),

	/**
	 * @note Zephyr already has mapped the generic regions (e.g. .text, .data, .bss)
	 *
		MMU_REGION_FLAT_ENTRY("ocm_low",
					  DT_REG_ADDR(DT_NODELABEL(ocm_low)),
					  DT_REG_SIZE(DT_NODELABEL(ocm_low)),
					  M_MEMORY),

		MMU_REGION_FLAT_ENTRY("ocm_high",
					  DT_REG_ADDR(DT_NODELABEL(ocm_high)),
					  DT_REG_SIZE(DT_NODELABEL(ocm_high)),
					  M_MEMORY),
	*/

	MMU_REGION_FLAT_ENTRY("gic_dist", DT_REG_ADDR_BY_IDX(DT_NODELABEL(gic), 0),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(gic), 0), M_DEVICE),

	MMU_REGION_FLAT_ENTRY("gic_cpu", DT_REG_ADDR_BY_IDX(DT_NODELABEL(gic), 1),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(gic), 1), M_DEVICE),

#if DT_NODE_HAS_STATUS(DT_NODELABEL(arch_timer), okay)
	MMU_REGION_FLAT_ENTRY("arch_timer", DT_REG_ADDR(DT_NODELABEL(arch_timer)),
			      DT_REG_SIZE(DT_NODELABEL(arch_timer)), M_DEVICE),
#endif

};

const struct arm_mmu_config mmu_config = {
	.num_regions = ARRAY_SIZE(mmu_regions),
	.mmu_regions = mmu_regions,
};

/* Platform-specific early initialization */

void soc_reset_hook(void)
{
	/*
	 * When coming out of u-boot rather than downloading the Zephyr binary
	 * via JTAG, a few things modified by u-boot have to be re-set to a
	 * suitable default value for Zephyr to run, namely:
	 *
	 * - u-boot places the exception vectors somewhere in RAM and then
	 *   lets the VBAR register point to them. Zephyr uses the default
	 *   vector table location at address zero (and maybe at some later
	 *   time alternatively the HIVECS position). If VBAR isn't reset
	 *   to zero, the system crashes during the first context switch when
	 *   SVC is invoked.
	 * - u-boot sets the following bits in the SCTLR register:
	 *   - [I] ICache enable
	 *   - [C] DCache enable
	 *   - [Z] Branch prediction enable
	 *   - [A] Enforce strict alignment enable
	 *   [I] and [C] will be enabled during the MMU init -> disable them
	 *   until then. [Z] is probably not harmful. [A] will cause a crash
	 *   as early as z_mem_manage_init when an unaligned access is performed
	 *   -> clear [A].
	 */

	uint32_t vbar = 0;

	__set_VBAR(vbar);

	uint32_t sctlr = __get_SCTLR();

	sctlr &= ~SCTLR_I_Msk;
	sctlr &= ~SCTLR_C_Msk;
	sctlr &= ~SCTLR_A_Msk;
	sctlr &= ~SCTLR_M_Msk;
	__set_SCTLR(sctlr);

	/* Enable SMP */
	__set_ACTLR(__get_ACTLR() | ACTLR_SMP_Msk);

	/* invalidate dcache all, invalidate mmu tlb */
	arch_dcache_invd_all();

	/* Set TTBCR to disable LPAE */
	__set_TTBR0(0);

	/* Invalidate TLB */
	__set_TLBIALL(0);
	barrier_dsync_fence_full();
	barrier_isync_fence_full();

#if DT_NODE_HAS_STATUS(DT_NODELABEL(slcr), okay)
	mm_reg_t addr = DT_REG_ADDR(DT_NODELABEL(slcr));

	/* Unlock System Level Control Registers (SLCR) */
	sys_write32(SLCR_UNLOCK_KEY, addr + SLCR_UNLOCK);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(scu), okay)
	mm_reg_t scu_base = DT_REG_ADDR(DT_NODELABEL(scu));

	/* Enable the SCU */
	scu_enable(scu_base);
#endif
}

/* NOTE: If the vector table is not at address zero, relocate it */
#ifndef CONFIG_MMU
extern void *_vector_table[];
#define VECTOR_ADDRESS ((uintptr_t)_vector_table)
void relocate_vector_table(void)
{
	write_sctlr(read_sctlr() & ~HIVECS);
	write_vbar(VECTOR_ADDRESS & VBAR_MASK);
	barrier_isync_fence_full();
}
#endif
