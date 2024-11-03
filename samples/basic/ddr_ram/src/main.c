/*
 * Copyright (c) 2024 SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @brief DDR RAM usage example
 */

#include <zephyr/sys/printk.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

int main(void)
{
	uint8_t *ddr_ram;
	uintptr_t phy_addr = DT_REG_ADDR(DT_NODELABEL(ddr));
	/* Test size can't be too large, due to the limited OCM size */
	const size_t size = 0x200000;

	Z_LOG(LOG_LEVEL_INF, "ddr mmap start, phy_addr: 0x%lx, size: %uKB\n", phy_addr,
	      size / 1024);
	k_mem_map_phys_bare(&ddr_ram, phy_addr, size,
			    K_MEM_CACHE_WT | K_MEM_PERM_RW | K_MEM_DIRECT_MAP);
	Z_LOG(LOG_LEVEL_INF, "ddr mmaped at 0x%lx\n", (uintptr_t)ddr_ram);

	/* Read and write to the DDR RAM */
	Z_LOG(LOG_LEVEL_INF, "Writing to DDR RAM...\n");
	for (int i = 0; i < size; i++) {
		ddr_ram[i] = (uint8_t)i;
	}

	Z_LOG(LOG_LEVEL_INF, "Reading from DDR RAM...\n");
	for (int i = 0; i < size; i++) {
		if (ddr_ram[i] != (uint8_t)i) {
			Z_LOG(LOG_LEVEL_ERR, "DDR RAM data mismatch at %d, expected %d, got %d\n",
			      i, i, ddr_ram[i]);
		}
	}
	Z_LOG(LOG_LEVEL_INF, "DDR RAM data verified\n");
	return 0;
}
