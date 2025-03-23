/**
 * @file uboot.h
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @brief
 * @version 0.1
 * @date 2025-03-23
 *
 * @copyright Copyright (c) 2025 SYSFly Co.
 *
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief uboot public API for uboot control of image boot process
 *
 * @defgroup uboot_api uboot image control API
 * @ingroup third_party
 * @{
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef void *uboot_env_t;

/**
 * @brief Load uboot environment from flash device
 *
 * @param[out] env uboot environment instance
 * @return 0 on success, negative errno code on fail
 * @return -ENOMEM if failed to allocate memory
 * @return -EIO if failed to open flash area
 * @return -EINVAL if flash area is too small or
 *          env is invalid(CRC check failed, etc.)
 */
int uboot_env_load(uboot_env_t *env);

/**
 * @brief Save uboot environment to flash device
 *
 * @param env uboot environment instance
 * @return 0 on success, negative errno code on fail
 * @return -ENOMEM if failed to allocate memory
 * @return -EINVAL if flash area is too small
 */
int uboot_env_save(uboot_env_t env);

/**
 * @brief Set uboot environment variable
 *
 * @param env uboot environment instance
 * @param name variable name
 * @param value variable value
 * @return 0 on success, negative errno code on fail
 * @return -ENOENT  if variable not found
 * @return -ENOBUFS if failed to allocate memory
 */
int uboot_env_set(uboot_env_t env, const char *name, const char *value);

/**
 * @brief Get uboot environment variable
 *
 * @param env uboot environment instance
 * @param name variable name
 * @param value variable value
 * @param len value buffer size
 * @return 0 on success, negative errno code on fail
 * @return -ENOMEM if failed to allocate memory
 */
int uboot_env_get(uboot_env_t env, const char *name, char *value, size_t len);

#ifdef __cplusplus
}
#endif
/**
 * @}
 */
