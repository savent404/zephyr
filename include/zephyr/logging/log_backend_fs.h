/*
 * Copyright (c) 2026 SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_LOGGING_LOG_BACKEND_FS_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_BACKEND_FS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check whether file system logging backend is degraded.
 *
 * @retval true Backend has reached degradation threshold and is inactive.
 * @retval false Backend has not degraded.
 */
#if defined(CONFIG_LOG_BACKEND_FS)
bool log_backend_fs_is_degraded(void);
#else
static inline bool log_backend_fs_is_degraded(void)
{
	return false;
}
#endif

/**
 * @brief Get consecutive file system logging backend failure count.
 *
 * @retval 0 No recorded failure or backend disabled.
 * @retval >0 Number of consecutive write path failures.
 */
#if defined(CONFIG_LOG_BACKEND_FS)
uint32_t log_backend_fs_failure_count_get(void);
#else
static inline uint32_t log_backend_fs_failure_count_get(void)
{
	return 0;
}
#endif

/**
 * @brief Get last file system logging backend error.
 *
 * @retval 0 No recorded error or backend disabled.
 * @retval <0 Last negative errno recorded on write path failure.
 */
#if defined(CONFIG_LOG_BACKEND_FS)
int log_backend_fs_last_error_get(void);
#else
static inline int log_backend_fs_last_error_get(void)
{
	return 0;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_LOGGING_LOG_BACKEND_FS_H_ */
