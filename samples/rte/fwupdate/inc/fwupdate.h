/**
 * @file fwupdate.h
 * @brief Firmware update header file
 * @copyright SYSFly Co.
 */
#pragma once

#include <stdint.h>
#include <zephyr/storage/stream_flash.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fw_info {
	uint32_t fw_size;         /* Firmware size */
	uint32_t fw_digest[8];    /* Firmware digest, null if not available */
	uint32_t fw_signature[8]; /* Firmware signature, null if not available */
	char fw_tag[64];          /* Firmware tag */
};

/**
 * @brief Initialize the firmware update module
 * @note: This function must be called before any other function in this module
 * @return 0 on success, negative errno code on failure
 * @return -ENOENT if the partition is not available
 * @return -ENODEV if the flash device is not available
 * @return other negative errno codes on flash operation failure
 */
int fw_init(void);

/**
 * @brief Get the firmware information
 * @param slot Firmware slot
 * @param info Firmware information
 * @return 0 on success
 * @return -EINVAL if slot is invalid or partition is not initialized or too small
 * @return -ENODATA if the metadata or checksum is invalid
 */
int fw_get_info(uint8_t slot, struct fw_info *info);

/**
 * @brief Start a firmware update
 * @param slot Firmware slot
 * @param erase Erase the firmware partition before starting the update
 * @param[out] stream Stream to write the firmware to
 * @note  Use stream_flash_buffered_write() and stream_flash_erase_page() to write the firmware
 *        And user needs to make sure all the data is written to the flash before calling
 * fw_finish()
 * @return 0 on success
 * @return -EINVAL if slot is invalid or stream is NULL
 * @return -EACCES if the slot is primary or the stream is already initialized
 * @return other negative errno codes on flash operation failure
 */
int fw_start(uint8_t slot, bool erase, struct stream_flash_ctx **stream);

/**
 * @brief Finish a firmware update
 * @param slot Firmware slot
 * @param tag Firmware tag
 * @param tag_len Firmware tag length
 * @param hash_algo Hash algorithm name (e.g., "SHA256"), can be NULL for default
 * @param hash Hash value, can be NULL if not available
 * @param signature Signature value (hex), can be NULL if not available
 * @param trust_chain Trust chain (DER in hex, splited by ' '), can be NULL if not available
 * @note This function will set security_mode based on the hash_algo and signature
 *       - certificate: signature and trust_chain are used
 *       - hash: hash_algo and hash are used
 *       - none: no security.
 * @return 0 on success
 * @return -EINVAL if slot is invalid or tag is invalid
 * @return other negative errno codes on flash operation failure
 */
int fw_finish(uint8_t slot, const char *tag, size_t tag_len, const char *hash_algo,
	      const char *hash, const char *signature, const char *trust_chain);

/**
 * @brief Check if the firmware slot is valid
 * @param slot Firmware slot
 * @return true if the slot is valid
 * @return false if the slot is invalid
 */
bool fw_is_valid(uint8_t slot);

/**
 * @brief Check if the firmware slot is primary
 * @param slot Firmware slot
 * @return true if the slot is primary
 * @return false if the slot is not primary
 */
bool fw_is_primary(uint8_t slot);

/**
 * @brief Commit the firmware update
 *
 * @return true commit success
 * @return false commit failed
 */
bool fw_commit(void);

#ifdef __cplusplus
}
#endif
