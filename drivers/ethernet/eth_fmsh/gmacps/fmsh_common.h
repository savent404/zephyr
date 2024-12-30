/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _FMSH_COMMON_H_
#define _FMSH_COMMON_H_

#include <stdint.h>
#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>

#define GMAC_DEBUG 0

#ifdef CONFIG_CACHE
#define FMSH_CACHE_ENABLE 1
#endif

#if (GMAC_DEBUG)
#define FMSH_INFO(fmt, ...)           LOG_INF(fmt, ##__VA_ARGS__)
#define FMSH_DEBUG(fmt, ...)          LOG_DBG(fmt, ##__VA_ARGS__)
#define FMSH_ERROR(fmt, ...)          LOG_ERR(fmt, ##__VA_ARGS__)
#define FMSH_HEXDUMP(desc, addr, len) LOG_HEXDUMP_DBG(addr, len, desc)
#else
#define FMSH_INFO(fmt, ...)
#define FMSH_DEBUG(fmt, ...)
#define FMSH_ERROR(fmt, ...)
#define FMSH_HEXDUMP(desc, addr, len)
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* Register access macros */
#define FMSH_ReadReg(baseAddr, offSet)        sys_read32((baseAddr + offSet))
#define FMSH_WriteReg(baseAddr, offSet, data) sys_write32((data), (baseAddr + offSet))

/* Cache operations */
#define FMSH_DCACHE_FLUSH(addr, size)      sys_cache_data_flush_range((void *)(addr), (size))
#define FMSH_DCACHE_INVALIDATE(addr, size) sys_cache_data_invd_range((void *)(addr), (size))

/* 8-bit access */
#define FMSH_IN8_8(p)     sys_read8((uintptr_t)&(p))
#define FMSH_OUT8_8(v, p) sys_write8((u8)(v), (uintptr_t)&(p))

/* 16-bit access */
#define FMSH_IN16_16(p)     sys_read16((uintptr_t)&(p))
#define FMSH_OUT16_16(v, p) sys_write16((u16)(v), (uintptr_t)&(p))

/* 32-bit access */
#define FMSH_IN32_32(p)     sys_read32((uintptr_t)&(p))
#define FMSH_OUT32_32(v, p) sys_write32((u32)(v), (uintptr_t)&(p))

#define FMSH_DELAY_US(us) k_busy_wait((us))
#define FMSH_DELAY_MS(ms) k_sleep((K_MSEC(ms)))

#endif /* #ifndef _FMSH_COMMON_H_ */
