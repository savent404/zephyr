/**
 * @file fwupdate.c
 * @brief Firmware update source file
 * @copyright SYSFly Co.
 */
#include "fwupdate.h"
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/uboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/storage/stream_flash.h>

#include <string.h>
#include <ctype.h>

LOG_MODULE_REGISTER(fwupdate, CONFIG_FWUPDATE_LOG_LEVEL);

#define FW_SLOT0_ID FIXED_PARTITION_ID(slot0_partition)
#define FW_SLOT1_ID FIXED_PARTITION_ID(slot1_partition)

#if !FIXED_PARTITION_EXISTS(slot0_partition)
#error "slot0_partition not defined"
#endif

#if !FIXED_PARTITION_EXISTS(slot1_partition)
#error "slot1_partition not defined"
#endif

enum partition_e {
	PART_A = 0,
	PART_B,
};

enum fw_metadata_key_e {
	FW_DIGEST = 0,
	FW_SIGNATURE,
	FW_TAG,
	FW_SIZE,
	FW_HASH_ALGO, /* e.g. "sha256" */
};

enum fw_global_key_e {
	FW_ACTIVE = 0,
	FW_CHECK,
	FW_BOOT_CNT,
	FW_MAX_BOOT,
	FW_SECURITY_MODE,        /* security mode, e.g. "cert", "hash", "none" */
	FW_SECURITY_POLICY,      /* security policy, e.g. "goon", "hang", "fallback" */
	FW_SECURITY_TRUST_CHAIN, /* trust chain in hex, splited by ' ' */
};

/* NOTE: this is the prefix for the metadata partition,
 * e.g. app_digest_a, app_digest_b
 */
const char *fw_metadata_keys[] = {
	[FW_DIGEST] = "app_digest", [FW_SIGNATURE] = "app_signature", [FW_TAG] = "app_tag",
	[FW_SIZE] = "app_size",     [FW_HASH_ALGO] = "app_hash_algo",
};

const char *fw_global_keys[] = {
	[FW_ACTIVE] = "active_slot",
	[FW_CHECK] = "check_boot",
	[FW_BOOT_CNT] = "bootcnt",
	[FW_MAX_BOOT] = "max_boot",
	[FW_SECURITY_MODE] = "security_mode",
	[FW_SECURITY_POLICY] = "security_policy",
	[FW_SECURITY_TRUST_CHAIN] = "trust_chain",
};

struct fw_metadata {
	/* firmware header, offset 44 */
	uint8_t fw_digest[32];     /* SHA256 digest */
	uint8_t fw_signature[512]; /* RSA4096 signature */
	uint32_t fw_size;
	char fw_tag[64];
	char fw_hash_algo[16]; /* e.g. "SHA256" */
};

struct fw_update_instance {
	const uint8_t fa_fw_id;         /* flash area id for firmware */
	const struct flash_area *fa_fw; /* flash area for firmware */
	struct fw_metadata metadata;    /* metadata */
	bool is_primary;                /* is primary slot */
	bool is_valid;                  /* is valid */
	bool is_streaming;              /* is streaming */

	struct stream_flash_ctx fw_ctx; /* firmware context */
	uint8_t stream_buf[256];        /* stream buffer */
};

static struct fw_update_instance fw_instance[2] = {
	{
		.fa_fw_id = FW_SLOT0_ID,
		.fa_fw = NULL,
		.metadata = {},
	},
	{
		.fa_fw_id = FW_SLOT1_ID,
		.fa_fw = NULL,
		.metadata = {},
	},
};

#if CONFIG_FWUPDATE_AUTO_COMMIT
static bool fw_autocommit_cancel; /* default to false */
#endif

static uboot_env_t env[1];

static inline bool prepare_key(char *buf, size_t len, enum fw_metadata_key_e key,
			       enum partition_e part)
{
	int rc = snprintf(buf, len, "%s_%c", fw_metadata_keys[key], part == PART_A ? 'a' : 'b');

	return rc > 0 && rc < len;
}

static inline int fw_query_metadata(char *buf, size_t len, enum fw_metadata_key_e key,
				    enum partition_e part)
{
	char key_buf[64];

	if (!prepare_key(key_buf, sizeof(key_buf), key, part)) {
		LOG_ERR("Failed to prepare key for metadata");
		return -EINVAL;
	}

	int rc = uboot_env_get(env[0], key_buf, buf, len);

	if (rc) {
		LOG_ERR("Failed to get %s from uboot env, rc %d", key_buf, rc);
	}

	return rc;
}

static inline int fw_query_global(char *buf, size_t len, enum fw_global_key_e key)
{
	int rc = uboot_env_get(env[0], fw_global_keys[key], buf, len);

	if (rc) {
		LOG_ERR("Failed to get %s from uboot env, rc %d", fw_global_keys[key], rc);
	}

	return rc;
}

static inline int fw_set_global(enum fw_global_key_e key, const char *val)
{
	int rc = uboot_env_set(env[0], fw_global_keys[key], val);

	if (rc) {
		LOG_ERR("Failed to set %s to uboot env, rc %d", fw_global_keys[key], rc);
	}

	return rc;
}

static void fw_load_meta_from_env(uboot_env_t env, enum partition_e part, struct fw_metadata *meta)
{
	char val[sizeof(meta->fw_signature) * 2 + 1];

	ARG_UNUSED(env);

	memset(meta, 0, sizeof(struct fw_metadata));

	if (!fw_query_metadata(val, sizeof(val), FW_DIGEST, part)) {
		/* Each byte in digest is represented by 2 hex characters */
		size_t len = strnlen(val, sizeof(val));

		if (len == 2 * sizeof(meta->fw_digest)) { /* hex is 2 chars per byte */
			for (size_t i = 0; i < sizeof(meta->fw_digest); i++) {
				char hex[3] = {val[i * 2], val[i * 2 + 1], '\0'};

				meta->fw_digest[i] = strtoul(hex, NULL, 16);
			}
		} else {
			LOG_WRN("Invalid digest length: %zu (expected %zu)", len,
				2 * sizeof(meta->fw_digest));
		}
	}
	if (!fw_query_metadata(val, sizeof(val), FW_SIGNATURE, part)) {
		/* Each byte in signature is represented by 2 hex characters */
		size_t len = strnlen(val, sizeof(val));

		if (len == 2 * sizeof(meta->fw_signature)) { /* hex is 2 chars per byte */
			for (size_t i = 0; i < sizeof(meta->fw_signature); i++) {
				char hex[3] = {val[i * 2], val[i * 2 + 1], '\0'};

				meta->fw_signature[i] = strtoul(hex, NULL, 16);
			}
		} else {
			LOG_WRN("Invalid signature length: %zu (expected %zu)", len,
				2 * sizeof(meta->fw_signature));
		}
	}

	if (!fw_query_metadata(meta->fw_tag, sizeof(meta->fw_tag), FW_TAG, part)) {
		/* Tag is directly copied into meta->fw_tag */
	}

	if (!fw_query_metadata(val, sizeof(val), FW_SIZE, part)) {
		/* value is encoded hex value like 0x04000 */
		meta->fw_size = strtoul(val, NULL, 16);
	}

	if (!fw_query_metadata(meta->fw_hash_algo, sizeof(meta->fw_hash_algo), FW_HASH_ALGO,
			       part)) {
		/* Hash algorithm is directly copied into meta->fw_hash_algo */
		LOG_DBG("Loaded hash algorithm: %s for partition %s", meta->fw_hash_algo,
			part == PART_A ? "A" : "B");
	}
}

static void fw_save_meta_to_env(uboot_env_t env, enum partition_e part, struct fw_metadata *meta)
{
	char key[64];
	char val[sizeof(meta->fw_signature) * 2 + 1];
	int rc;

	/* set digest */
	snprintf(key, sizeof(key), "%s_%c", fw_metadata_keys[FW_DIGEST],
		 part == PART_A ? 'a' : 'b');
	for (size_t i = 0; i < sizeof(meta->fw_digest); i++) {
		snprintf(&val[i * 2], 3, "%02x", meta->fw_digest[i]);

		if (i * 2 + 2 >= sizeof(val)) {
			LOG_ERR("Digest too long for uboot env, truncating");
			break;
		}
	}
	rc = uboot_env_set(env, key, val);
	if (rc) {
		LOG_ERR("Failed to set %s to uboot env, rc %d", key, rc);
	}

	/* set signature */
	snprintf(key, sizeof(key), "%s_%c", fw_metadata_keys[FW_SIGNATURE],
		 part == PART_A ? 'a' : 'b');
	for (size_t i = 0; i < sizeof(meta->fw_signature); i++) {
		snprintf(&val[i * 2], 3, "%02x", meta->fw_signature[i]);

		if (i * 2 + 2 >= sizeof(val)) {
			LOG_ERR("Signature too long for uboot env, truncating");
			break;
		}
	}
	rc = uboot_env_set(env, key, val);
	if (rc) {
		LOG_ERR("Failed to set %s to uboot env, rc %d", key, rc);
	}

	/* set tag */
	snprintf(key, sizeof(key), "%s_%c", fw_metadata_keys[FW_TAG], part == PART_A ? 'a' : 'b');
	rc = uboot_env_set(env, key, meta->fw_tag);
	if (rc) {
		LOG_ERR("Failed to set %s to uboot env, rc %d", key, rc);
	}

	/* set size */
	snprintf(key, sizeof(key), "%s_%c", fw_metadata_keys[FW_SIZE], part == PART_A ? 'a' : 'b');
	snprintf(val, sizeof(val), "0x%08x", meta->fw_size);
	rc = uboot_env_set(env, key, val);
	if (rc) {
		LOG_ERR("Failed to set %s to uboot env, rc %d", key, rc);
	}

	/* set hash algorithm */
	snprintf(key, sizeof(key), "%s_%c", fw_metadata_keys[FW_HASH_ALGO],
		 part == PART_A ? 'a' : 'b');
	rc = uboot_env_set(env, key, meta->fw_hash_algo);
	if (rc) {
		LOG_ERR("Failed to set %s to uboot env, rc %d", key, rc);
	}
}

int fw_init(void)
{
	static bool initialized;
	int rc;

	if (!initialized) {
		initialized = true;
	} else {
		return 0;
	}

	rc = uboot_env_load(&env[0]);
	if (rc) {
		LOG_ERR("Failed to load uboot env, rc %d", rc);
		return rc;
	}

	rc = 0;
	for (size_t i = 0; i < ARRAY_SIZE(fw_instance); i++) {
		/**
		 * We don't need to release the flash area, since it
		 * will be only initialized once.
		 */
		struct fw_update_instance *inst = &fw_instance[i];
		struct fw_metadata *ptr_m = &inst->metadata;

		LOG_INF("Initializing firmware partition %d", i);

		/* Load metadata from uboot env */
		LOG_DBG("Start to read metadata from uboot env..., partition %d", i);
		fw_load_meta_from_env(env, (enum partition_e)i, ptr_m);
		LOG_INF("Firmware partition %d(%s) size: %u", i, i == 0 ? "A" : "B",
			ptr_m->fw_size);
		LOG_HEXDUMP_DBG(ptr_m->fw_digest, sizeof(ptr_m->fw_digest), "digest");
		LOG_HEXDUMP_DBG(ptr_m->fw_signature, sizeof(ptr_m->fw_signature), "signature");
		size_t tag_len = strnlen(ptr_m->fw_tag, sizeof(ptr_m->fw_tag));

		if (tag_len != 0 && tag_len < sizeof(ptr_m->fw_tag)) {
			LOG_INF("Firmware tag: %s", ptr_m->fw_tag);
		} else {
			LOG_INF("Firmware tag: <empty>");
		}
		LOG_DBG("Metadata loaded from uboot env");

		/* Open the firmware partition */
		LOG_DBG("Opening firmware partition %d...", i);
		rc = flash_area_open(inst->fa_fw_id, &inst->fa_fw);
		if (rc) {
			LOG_ERR("Cannot open firmware partition %d", i);
			return rc;
		}
		LOG_DBG("Firmware partition %d: %08lx-%08lx", i, inst->fa_fw->fa_off,
			inst->fa_fw->fa_off + inst->fa_fw->fa_size);

		/* Set default valid state - should be determined by validation */
		inst->is_valid = true;
	}

	enum partition_e part = PART_A;
	char val[64];

	rc = fw_query_global(val, sizeof(val), FW_ACTIVE);
	if (!rc) {

		if (!strcmp(val, "A")) {
			part = PART_A;
		} else if (!strcmp(val, "B")) {
			part = PART_B;
		} else {
			LOG_WRN("Unknown active slot %s, assume slot A", val);
			part = PART_A;
		}
	}

	fw_instance[part].is_primary = true;

	if (!fw_instance[part].is_valid) {
		LOG_WRN("Primary slot %d is invalid", part);
	}

	return rc;
}

bool fw_is_valid(uint8_t slot)
{
	if (slot >= ARRAY_SIZE(fw_instance)) {
		return false;
	}

	const struct fw_update_instance *inst = &fw_instance[slot];

	/* Check validity */
	if (!inst->is_valid) {
		return false;
	}

	/* Check firmware size */
	if (inst->metadata.fw_size == 0) {
		return false;
	}

	/* If flash area is not initialized, return basic validity */
	if (!inst->fa_fw) {
		return inst->is_valid;
	}

	return true;
}

bool fw_is_primary(uint8_t slot)
{
	if (slot >= ARRAY_SIZE(fw_instance)) {
		return false;
	}
	return fw_instance[slot].is_primary;
}

int fw_get_info(uint8_t slot, struct fw_info *info)
{
	struct fw_update_instance *inst = &fw_instance[slot];

	if (slot >= ARRAY_SIZE(fw_instance) || !info) {
		return -EINVAL;
	}

	if (!inst->is_valid) {
		return -EINVAL;
	}

	info->fw_size = inst->metadata.fw_size;
	memcpy(info->fw_digest, inst->metadata.fw_digest, sizeof(info->fw_digest));
	memcpy(info->fw_signature, inst->metadata.fw_signature, sizeof(info->fw_signature));
	memcpy(info->fw_tag, inst->metadata.fw_tag, sizeof(info->fw_tag));
	return 0;
}

int fw_start(uint8_t slot, bool erase, struct stream_flash_ctx **stream)
{
	if (slot >= ARRAY_SIZE(fw_instance) || !stream) {
		return -EINVAL;
	}
	struct fw_update_instance *inst = &fw_instance[slot];
	int rc;

	if (inst->is_primary) {
		return -EACCES;
	}

	if (erase) {
		LOG_INF("Erasing firmware partition %d", slot);
		rc = flash_area_erase(inst->fa_fw, 0, inst->fa_fw->fa_size);
		if (rc) {
			LOG_ERR("Cannot erase firmware partition %d", slot);
			return rc;
		}
	}

	/* Initialize the stream flash context */
	rc = stream_flash_init(&inst->fw_ctx, inst->fa_fw->fa_dev, inst->stream_buf,
			       sizeof(inst->stream_buf), inst->fa_fw->fa_off, inst->fa_fw->fa_size,
			       NULL);
	if (rc) {
		LOG_WRN("Cannot initialize stream flash context for slot %d", slot);
		return rc;
	}

	/* Return the stream flash context */
	*stream = &inst->fw_ctx;
	inst->is_streaming = true;
	return 0;
}

int fw_finish(uint8_t slot, const char *tag, size_t tag_len, const char *hash_algo,
	      const char *hash, const char *signature, const char *trust_chain)
{
	int rc;
	bool has_hash = false;
	bool has_signature = false;
	bool has_hash_algo = false;
	bool has_trust_chain = false;

	if (slot >= ARRAY_SIZE(fw_instance)) {
		return -EINVAL;
	}
	if (!tag || tag_len == 0 || tag_len >= sizeof(fw_instance[slot].metadata.fw_tag)) {
		LOG_WRN("finish slot %d: tag is invalid, ptr %p, len %u", slot, tag, tag_len);
		return -EINVAL;
	}
	struct fw_update_instance *inst = &fw_instance[slot];

	/* Check if we have a valid context */
	if (!inst->is_streaming) {
		return -EINVAL;
	}

	size_t bytes_written = stream_flash_bytes_written(&inst->fw_ctx);

	LOG_INF("Firmware partition %d: %u bytes written", slot, bytes_written);

	/* Prepare metadata */
	struct fw_metadata *meta = &inst->metadata;

	LOG_DBG("Start to prepare metadata...");
	meta->fw_size = (uint32_t)bytes_written;

	/* Update digest */
	if (hash && strnlen(hash, 2 * sizeof(meta->fw_digest)) >= 2 * sizeof(meta->fw_digest)) {
		for (size_t i = 0; i < sizeof(meta->fw_digest); i++) {
			char hex[3] = {hash[i * 2], hash[i * 2 + 1], '\0'};

			meta->fw_digest[i] = strtoul(hex, NULL, 16);
		}
		has_hash = true;
	} else {
		LOG_WRN("No hash provided (or invalid), using zero digest");
		if (hash) {
			LOG_WRN("Hash length: %zu (expected %zu)",
				strnlen(hash, 2 * sizeof(meta->fw_digest) + 1),
				2 * sizeof(meta->fw_digest));
		}
		memset(meta->fw_digest, 0, sizeof(meta->fw_digest));
	}

	/* Update signature */
	if (signature &&
	    strnlen(signature, 2 * sizeof(meta->fw_signature)) >= 2 * sizeof(meta->fw_signature)) {
		for (size_t i = 0; i < sizeof(meta->fw_signature); i++) {
			char hex[3] = {signature[i * 2], signature[i * 2 + 1], '\0'};

			meta->fw_signature[i] = strtoul(hex, NULL, 16);
		}
		has_signature = true;
	} else {
		LOG_WRN("No signature provided (or invalid), using zero signature");

		if (signature) {
			LOG_WRN("Signature length: %zu (expected %zu)",
				strnlen(signature, 2 * sizeof(meta->fw_signature) + 1),
				2 * sizeof(meta->fw_signature));
		}
		memset(meta->fw_signature, 0, sizeof(meta->fw_signature));
	}

	/* Set hash algorithm from parameter */
	if (hash_algo && strnlen(hash_algo, sizeof(meta->fw_hash_algo)) > 0) {
		snprintf(meta->fw_hash_algo, sizeof(meta->fw_hash_algo), "%s", hash_algo);
		has_hash_algo = true;
	}

	memcpy(meta->fw_tag, tag, tag_len);

	LOG_DBG("Firmware size: %u, tag: %s, hash_algo: %s", meta->fw_size, meta->fw_tag,
		meta->fw_hash_algo);
	LOG_HEXDUMP_DBG(meta->fw_digest, sizeof(meta->fw_digest), "digest");
	LOG_HEXDUMP_DBG(meta->fw_signature, sizeof(meta->fw_signature), "signature");
	inst->is_valid = true;
	inst->is_streaming = false;

	fw_save_meta_to_env(env[0], (enum partition_e)slot, meta);
	fw_set_global(FW_ACTIVE, slot == PART_A ? "A" : "B");
	fw_set_global(FW_CHECK, "1");
	fw_set_global(FW_BOOT_CNT, "0");

	if (trust_chain && strnlen(trust_chain, 256) > 0 &&
	    strnlen(trust_chain, 32 * 1024) < 32 * 1024) {
		LOG_DBG("Setting trust chain... len: %zu", strnlen(trust_chain, 32 * 1024));
		fw_set_global(FW_SECURITY_TRUST_CHAIN, trust_chain);
		has_trust_chain = true;
	}

	/*
	 * NOTE: change security mode and policy by
	 *  fw_set_global(FW_SECURITY_MODE, "cert");
	 *  fw_set_global(FW_SECURITY_POLICY, "goon");
	 *  or you can change those in u-boot env directly
	 */

	rc = uboot_env_save(env[0]);
	if (rc) {
		LOG_ERR("Failed to save uboot env, rc %d", rc);
		return rc;
	}
	LOG_INF("Firmware partition %d: fwupdate completed successfully", slot);

#if CONFIG_FWUPDATE_AUTO_COMMIT
	fw_autocommit_cancel = true;
#endif

	return 0;
}

bool fw_commit(void)
{
	int rc;
	char val[8];

	fw_init();

	rc = fw_query_global(val, sizeof(val), FW_CHECK);
	if (rc) {
		LOG_ERR("Failed to query check_boot, rc %d", rc);
		return false;
	}

	if (strcmp(val, "0")) {
		LOG_INF("Committing firmware update");
		rc = fw_set_global(FW_CHECK, "0");
		if (rc) {
			LOG_ERR("Failed to set check_boot to 0, rc %d", rc);
			return false;
		}
		rc = uboot_env_save(env[0]);
		if (rc) {
			LOG_ERR("Failed to save uboot env, rc %d", rc);
			return false;
		}
		LOG_INF("Firmware update committed");
	}

	return true;
}

#if CONFIG_FWUPDATE_AUTO_COMMIT

static void fw_autocommit_task(void)
{
	k_msleep(1000 * 60);

	/* If fw_autocommit_cancel is set, fw_commit will not be called automatically */
	if (!fw_autocommit_cancel) {
		fw_commit();
	} else {
		LOG_INF("fw_autocommit cancelled");
	}
}

K_THREAD_DEFINE(fw_autocommit, 4096, fw_autocommit_task, NULL, NULL, NULL, 10, 0, 0);

int fw_autocommit_start(void)
{
	k_tid_t tid = fw_autocommit;

	k_thread_name_set(tid, "fw_autocommit");
	k_thread_start(tid);
	return 0;
}

SYS_INIT(fw_autocommit_start, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif
