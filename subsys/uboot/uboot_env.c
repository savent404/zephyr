/**
 * @file env_uboot.c
 * @author Liao, YuanKai(savent_gate@outlook.com)
 * @brief
 * @version 0.1
 * @date 2025-03-21
 *
 * @copyright Copyright (c) 2025 SYSFly Co.
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/uboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/hash_map.h>

LOG_MODULE_REGISTER(uboot_env, CONFIG_UBOOT_LOG_LEVEL);

struct env_item {
	sys_dnode_t node;
	char *key;
	char *value;
};

struct env_instance {
	int fa_id;
	uint8_t redundant_cycle;
	const struct flash_area *fa;
	sys_dlist_t items;

#define ENV_FLG_VALID  BIT(1)
#define ENV_FLG_REDUND BIT(2)
	uint8_t flags;
};

#define ENV_NAME           bootenv
#define ENV_REDUNDANT_NAME bootenv_redund

#define ENV_SIZE FIXED_PARTITION_SIZE(ENV_NAME)

#if FIXED_PARTITION_EXISTS(ENV_REDUNDANT_NAME)
#define ENV_REDUNDANT 1
#define ENV_HDR_SIZE  5
static struct env_instance env_instance[2] = {
	{
		.fa_id = FIXED_PARTITION_ID(ENV_NAME),
		.flags = 0,
	},
	{
		.fa_id = FIXED_PARTITION_ID(ENV_REDUNDANT_NAME),
		.flags = 0,
	},
};

#if FIXED_PARTITION_SIZE(ENV_NAME) != FIXED_PARTITION_SIZE(ENV_REDUNDANT_NAME)
#error "ENV_NAME and ENV_REDUNDANT_NAME must have the same size"
#endif

#else
#define ENV_REDUNDANT 0
#define ENV_HDR_SIZE  4
static struct env_instance env_instance[1] = {
	{
		.fa_id = FIXED_PARTITION_ID(ENV_NAME),
		.flags = 0,
	},
};
#endif

static int _uboot_env_load(struct env_instance *ei)
{
	int rc;
	uint32_t crc_hdr;
	size_t fa_size;
	struct env_item *item;
	char *buf;

	buf = malloc(ENV_SIZE);
	if (!buf) {
		LOG_ERR("Failed to allocate memory for env");
		return -ENOMEM;
	}

	rc = flash_area_open(ei->fa_id, &ei->fa);
	if (rc) {
		LOG_ERR("Failed to open flash area %d", ei->fa_id);
		return rc;
	}

	fa_size = ei->fa->fa_size;

	if (fa_size < ENV_HDR_SIZE) {
		LOG_ERR("Env area size is too small");
		rc = -EINVAL;
		goto close_fa;
	}

	rc = flash_area_read(ei->fa, 0, buf, fa_size);
	if (rc) {
		LOG_ERR("Failed to read env header");
		return rc;
	}

	crc_hdr = *(uint32_t *)buf;
	if (crc_hdr != crc32_ieee(buf + ENV_HDR_SIZE, fa_size - ENV_HDR_SIZE)) {
		LOG_ERR("Env header CRC check failed");
		rc = -EINVAL;
		goto close_fa;
	}

	/* TODO: Fix flag */

	sys_dlist_init(&ei->items);

	/* Deal with items key=value\0... */
	for (size_t i = ENV_HDR_SIZE; i < fa_size - ENV_HDR_SIZE;) {

		/**
		 *  key=value\0
		 *  ^ ^     ^
		 *  | |     |
		 *  i k     v
		 * key_len = k - i + 2 (with '\0')
		 * value_len = v - k (with '\0')
		 */
		uint32_t key_pos = 0;
		uint32_t val_pos = 0;

		/* skip blank */
		if (buf[i] == ' ' || buf[i] == '\t') {
			i++;
			continue;
		}

		/* skip comment lines */
		if (buf[i] == '#') {
			while (buf[i] != '\0') {
				i++;
			}
			continue;
		}

		/* detect the end of items */
		if (buf[i] == '\0') {
			break;
		}

		/* detect the end of items */
		for (size_t j = 0; j < fa_size - ENV_HDR_SIZE - i; j++) {
			char k = buf[i + j];
			char v = buf[i + j + 1];

			/* Match the first '=' */
			if (!key_pos && k == '=') {
				key_pos = i + j - 1;

				if (v == '\0') {
					LOG_WRN("DELETE ERROR");
					rc = -EINVAL;
					goto destroy_dlist;
				}
			} else if (k == '\0') {
				val_pos = i + j - 1;
				break;
			}
		}

		if (!key_pos || !val_pos) {
			LOG_ERR("Invalid env item, key_pos=%d, val_pos=%d", key_pos, val_pos);
			rc = -EINVAL;
			goto destroy_dlist;
		}

		item = malloc(sizeof(struct env_item));
		if (!item) {
			LOG_ERR("Failed to allocate memory for env item");
			rc = -ENOMEM;
			goto destroy_dlist;
		}
		memset(item, 0, sizeof(struct env_item));

		int key_len = key_pos - i + 2;
		int val_len = val_pos - key_pos;
		int key_offset = i;
		int val_offset = key_pos + 2;

		item->key = malloc(key_len);
		if (!item->key) {
			LOG_ERR("Failed to allocate memory for env key");
			rc = -ENOMEM;
			goto destroy_dlist;
		}
		memcpy(item->key, &buf[key_offset], key_len - 1);
		item->key[key_len - 1] = '\0';

		item->value = malloc(val_len);
		if (!item->value) {
			LOG_ERR("Failed to allocate memory for env value");
			rc = -ENOMEM;
			goto destroy_dlist;
		}
		memcpy(item->value, &buf[val_offset], val_len - 1);
		item->value[val_len - 1] = '\0';

		sys_dlist_append(&ei->items, &item->node);

		LOG_DBG("Put item(%d:%d, %d:%d) %s<=>%s", key_offset, key_len, val_offset, val_len,
			item->key, item->value);

		i = val_pos + 2;
	}

close_fa:
	flash_area_close(ei->fa);
	free(buf);
	return rc;

destroy_dlist:
	/* Safely remove and free all items */
	struct env_item *tmp;

	SYS_DLIST_FOR_EACH_CONTAINER_SAFE(&ei->items, item, tmp, node) {
		sys_dlist_remove(&item->node);
		if (item->key) {
			free(item->key);
		}
		if (item->value) {
			free(item->value);
		}
		free(item);
	}
	flash_area_close(ei->fa);
	free(buf);
	return rc;
}

static int _uboot_env_save(struct env_instance *ei)
{
	char *buf;
	uint32_t data_size = 0;
	size_t fa_size;
	struct env_item *item;
	sys_dnode_t *node;
	int rc;

	SYS_DLIST_FOR_EACH_NODE(&ei->items, node) {
		item = CONTAINER_OF(node, struct env_item, node);
		data_size += strlen(item->key) + strlen(item->value) + 2;
	}

	rc = flash_area_open(ei->fa_id, &ei->fa);
	if (rc) {
		LOG_ERR("Failed to open flash area %d", ei->fa_id);
		return rc;
	}
	fa_size = ei->fa->fa_size;

	if (data_size + ENV_HDR_SIZE > fa_size) {
		LOG_ERR("Env area size is too small");
		rc = -EINVAL;
		goto close_fa;
	}

	buf = malloc(fa_size);
	if (!buf) {
		LOG_ERR("Failed to allocate memory for env");
		rc = -ENOMEM;
		goto close_fa;
	}
	memset(buf, 0, fa_size);

	/* Write items */
	size_t off = ENV_HDR_SIZE;

	SYS_DLIST_FOR_EACH_CONTAINER(&ei->items, item, node) {
		size_t walk = 0;

		char *ptr = &buf[off];
		const char *src = item->key;

		while (*src) {
			if (*src != '\\' && *src != '\0') {
				*ptr = *src;
			} else {
				*ptr++ = '\\';
			}

			ptr += 1;
			src += 1;
			walk += 1;
		}
		*ptr++ = '=';
		walk += 1;

		src = item->value;
		while (*src) {
			if (*src != '\\' && *src != '\0') {
				*ptr = *src;
			} else {
				*ptr++ = '\\';
			}

			ptr += 1;
			src += 1;
			walk += 1;
		}

		*ptr++ = '\0';
		walk += 1;
		off += walk;
	}

	if (off != data_size + ENV_HDR_SIZE) {
		LOG_ERR("Invalid env data size");
		rc = -EINVAL;
		goto free_buf;
	}

	/* Write Header */
	*(uint32_t *)buf = crc32_ieee(buf + ENV_HDR_SIZE, fa_size - ENV_HDR_SIZE);

	rc = flash_area_erase(ei->fa, 0, fa_size);
	if (rc) {
		LOG_ERR("Failed to erase env area");
		goto free_buf;
	}

	rc = flash_area_write(ei->fa, 0, buf, fa_size);
	if (rc) {
		LOG_ERR("Failed to write env area");
		goto free_buf;
	}

free_buf:
	free(buf);

close_fa:
	flash_area_close(ei->fa);

	return rc;
}

struct env_item *find_item(struct env_instance *ei, const char *key)
{
	struct env_item *item;

	SYS_DLIST_FOR_EACH_CONTAINER(&ei->items, item, node) {
		if (!strcmp(item->key, key)) {
			return item;
		}
	}

	return NULL;
}

int uboot_env_load(uboot_env_t *env)
{
	int rc;

	for (size_t i = 0; i < ARRAY_SIZE(env_instance); i++) {
		rc = _uboot_env_load(&env_instance[i]);
		if (rc) {
			return rc;
		}
	}

	if (env) {
		*env = &env_instance[0];
	}

	return 0;
}

int uboot_env_save(uboot_env_t env)
{
	int rc;

	for (size_t i = 0; i < ARRAY_SIZE(env_instance); i++) {
		rc = _uboot_env_save(&env_instance[i]);
		if (rc) {
			return rc;
		}
	}
	return 0;
}

int uboot_env_get(uboot_env_t env, const char *name, char *value, size_t len)
{
	struct env_instance *ei = (struct env_instance *)env;
	struct env_item *item = find_item(ei, name);

	if (!item) {
		return -ENOENT;
	}

	if (strlen(item->value) >= len) {
		return -ENOBUFS;
	}

	strcpy(value, item->value);

	return 0;
}

int uboot_env_set(uboot_env_t env, const char *name, const char *value)
{
	struct env_instance *ei = (struct env_instance *)env;
	struct env_item *item = find_item(ei, name);

	if (item) {
		free(item->value);
		item->value = strdup(value);
		if (!item->value) {
			return -ENOMEM;
		}
	} else {
		item = malloc(sizeof(struct env_item));
		if (!item) {
			return -ENOMEM;
		}

		item->key = strdup(name);
		if (!item->key) {
			free(item);
			return -ENOMEM;
		}

		item->value = strdup(value);
		if (!item->value) {
			free(item->key);
			free(item);
			return -ENOMEM;
		}

		sys_dlist_append(&ei->items, &item->node);
	}

	return 0;
}
