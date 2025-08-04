/**
 * @file tpm_shell.c
 * @author Liao, YuanKai (savent_gate@outlook.com)
 * @brief
 * @version 0.1
 * @date 2025-07-07
 * This project is CLOSE SOURCE CODE, all rights reserved.
 *
 * @copyright Copyright (c) 2025 SYSFly Co.
 *
 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h> /* Include for CONFIG_SYS_CLOCK_TICKS_PER_SEC */
#include <zephyr/arch/cpu.h>  /* Ensure ARCH_STACK_PTR_ALIGN is defined */
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <wolftpm/tpm2_wrap.h>
#include <wolftpm/tpm2.h>
#include <wolftpm/tpm2_types.h> /* Ensure TPM2_HANDLE is defined */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/random.h>
#include <gmssl/sm3.h>
#include <gmssl/hex.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/flash.h>     /* Add flash driver header file */
#include <zephyr/storage/flash_map.h> /* Add flash map header file */

#include <zephyr/device.h>
#include <zephyr/sys/printk.h>

#ifndef TPM2_ALG_SM3_256
#define TPM2_ALG_SM3_256 0x0012 /* Define SM3_256 algorithm identifier if not defined */
#endif

#define SM3_DIGEST_SIZE 32

/* Application memory definitions for trusted boot */
#define DEMO_ADDR 0x00010000 /* 0x00010000*/
#define DEMO_LEN  0x00010000 /* 0x00010000*/
/* Policy */
#define GOON      0x01ffffff
#define HANG      0x00ffffff

LOG_MODULE_REGISTER(sb_shell, CONFIG_LOG_DEFAULT_LEVEL);

/* SPI bus device */
int TPM2_Zephyr_IoCb(TPM2_CTX *ctx, const byte *txBuf, byte *rxBuf, word16 xferSz, void *userCtx);

/* private spi device */
#define SPI_BUS DEVICE_DT_GET(DT_NODELABEL(spi1))
static struct spi_config spi_cfg = {
	.frequency = 1000000,
	.operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8),
	.slave = 0,
	.cs = {},
};
struct {
	const struct device *dev;
	struct spi_config *cfg;
} _tpm_user_ctx = {
	.dev = SPI_BUS,
	.cfg = &spi_cfg,
};

/**
 * @brief TPM common initialization interface
 * @param shell shell instance for error output
 * @param dev tpm device pointer
 * @return 0 on success, negative value on error
 */
static int tpm_init_common(const struct shell *shell, WOLFTPM2_DEV *dev);

/**
 * @brief TPM common cleanup interface
 * @param dev tpm device pointer
 */
static void tpm_cleanup_common(WOLFTPM2_DEV *dev);

/**
 * @brief TPM common authorization handle setting interface
 * @param shell shell instance for error output
 * @param dev tpm device pointer
 * @param parent tpm parent handle pointer
 * @param auth_handle authorization handle value
 * @return 0 on success, negative value on error
 */
static int tpm_set_auth_handle_common(const struct shell *shell, WOLFTPM2_DEV *dev,
				      WOLFTPM2_HANDLE *parent, TPM_HANDLE auth_handle);

typedef struct TCMU_HA {
	BYTE sm3_256[32]; /* SM3_256 hash value */
	BYTE reserve[16]; /* Reserved bytes for future use */
} TCMU_HA;                /* Hash algorithm structure */

typedef struct TCM2B_DIGEST {
	UINT16 size;    /* Size of the digest */
	TCMU_HA digest; /* Digest value */
} TCM2B_DIGEST;         /* Store digest information */

struct TCM_SPIROM_SUB {
	uint32_t startAddr;  /* Start address of the SPI ROM */
	uint32_t len;        /* Length of the SPI ROM */
	TCM2B_DIGEST digest; /* Digest of the SPI ROM content */
	uint16_t hashAlg;    /* Hash algorithm used */
	uint8_t policy[4];   /* Policy for the SPI ROM */
}; /* Define SPI ROM sub-region information */

struct TCM_NV_INDEX_TPCM1_SUB {
	uint8_t valid[4];                   /* Validity flag */
	struct TCM_SPIROM_SUB spiRomsub[4]; /* Array of SPI ROM sub-regions */
};
struct TCM_NV_INDEX_TPCM1 {
	uint8_t valid[4];                        /* Validity flag */
	struct TCM_NV_INDEX_TPCM1_SUB spiRom[2]; /* Array of SPI ROM regions */
}; /* Define NV index structure 1 */

struct TCM_NV_INDEX_TPCM2 {
	uint32_t result;           /* Result of the operation */
	uint32_t spiromindex;      /* SPI ROM index */
	uint32_t spiromindexSub;   /* SPI ROM sub-index */
	TCM2B_DIGEST resultDigest; /* Result digest */
	uint8_t rsvd[2];           /* Reserved bytes */
}; /* Define NV index structure 2 */

/**
 * @brief Common function for define/undefine NV indexes
 *
 * @param sh shell instance
 * @param nvIndex NV index to operate on
 * @param nvName NV index name for display
 * @param is_define true for define, false for undefine
 * @return 0 on success, negative on error
 */
static int tpm_nv_define_undefine_common(const struct shell *sh, word32 nvIndex, const char *nvName,
					 bool is_define)
{
	int rc;
	WOLFTPM2_DEV dev;
	WOLFTPM2_HANDLE parent;

	shell_print(sh, "%s NV index %s (0x%08X)...", is_define ? "Defining" : "Undefining", nvName,
		    nvIndex);

	/* Initialize TPM */
	rc = tpm_init_common(sh, &dev);
	if (rc != 0) {
		return rc;
	}

	/* Set auth handle */
	rc = tpm_set_auth_handle_common(sh, &dev, &parent, TPM_RH_OWNER);
	if (rc != 0) {
		tpm_cleanup_common(&dev);
		return rc;
	}

	if (is_define) {
		/* Define NV index */
		NV_DefineSpace_In input;
		word32 nvAttributes;
		size_t dataSize;

		/* Set attributes and data size based on index */
		if (nvIndex == 0x013ffffe) {
			nvAttributes = TPMA_NV_OWNERWRITE | TPM_NT_ORDINARY |
				       TPMA_NV_WRITE_STCLEAR | TPMA_NV_OWNERREAD;
			dataSize = 524;
		} else if (nvIndex == 0x013fffff) {
			nvAttributes = TPMA_NV_PPWRITE | TPM_NT_ORDINARY | TPMA_NV_PPREAD |
				       TPMA_NV_OWNERREAD | TPMA_NV_AUTHREAD | TPMA_NV_NO_DA |
				       TPMA_NV_OWNERWRITE;
			dataSize = sizeof(struct TCM_NV_INDEX_TPCM2);
		} else {
			shell_error(sh, "Unknown NV index: 0x%08X", nvIndex);
			tpm_cleanup_common(&dev);
			return -EINVAL;
		}

		XMEMSET(&input, 0, sizeof(input));
		input.authHandle = parent.hndl;
		input.publicInfo.nvPublic.nvIndex = nvIndex;
		input.publicInfo.nvPublic.nameAlg = TPM_ALG_SM3_256;
		input.publicInfo.nvPublic.attributes = nvAttributes;
		input.publicInfo.nvPublic.dataSize = dataSize;

		rc = TPM2_NV_DefineSpace(&input);
		if (rc == TPM_RC_NV_DEFINED) {
			shell_print(sh, "NV index %s (0x%08X) already exists", nvName, nvIndex);
		} else if (rc != TPM_RC_SUCCESS) {
			shell_error(sh, "TPM2_NV_DefineSpace failed: 0x%x", rc);
			tpm_cleanup_common(&dev);
			return -EIO;
		}
	} else {
		/* Undefine NV index */
		NV_UndefineSpace_In input;

		XMEMSET(&input, 0, sizeof(input));
		input.authHandle = parent.hndl;
		input.nvIndex = nvIndex;

		rc = TPM2_NV_UndefineSpace(&input);
		if (rc != TPM_RC_SUCCESS) {
			shell_error(sh, "TPM2_NV_UndefineSpace failed: 0x%x", rc);
			tpm_cleanup_common(&dev);
			return -EIO;
		}
		shell_print(sh, "NV index %s (0x%08X) undefined successfully", nvName, nvIndex);
	}

	tpm_cleanup_common(&dev);
	return 0;
}

static int cmd_tpm_nvdefine_undefine(const struct shell *sh, size_t argc, char **argv,
				     bool is_define)
{
	word32 nvIndex;
	const char *cmd_name = is_define ? "nvdefine" : "nvundefine";

	/* Check parameter count */
	if (argc != 2) {
		shell_error(sh, "Usage:");
		shell_error(sh, "  tpm %s NV1", cmd_name);
		shell_error(sh, "  tpm %s NV2", cmd_name);
		return -EINVAL;
	}

	/* Parse and validate parameters */
	if (strcasecmp(argv[1], "NV1") == 0) {
		nvIndex = 0x013ffffe;
	} else if (strcasecmp(argv[1], "NV2") == 0) {
		nvIndex = 0x013fffff;
	} else {
		shell_error(sh, "Invalid index type: %s", argv[1]);
		shell_error(sh, "Must be 'NV1' or 'NV2'");
		return -EINVAL;
	}

	return tpm_nv_define_undefine_common(sh, nvIndex, argv[1], is_define);
}

static int cmd_tpm_nvdefine(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_tpm_nvdefine_undefine(sh, argc, argv, true);
}

static int cmd_tpm_nvundefine(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_tpm_nvdefine_undefine(sh, argc, argv, false);
}
/**
 * @brief Debug and print NV index TPCM1 structure
 *
 * This function reads NV index 1 data and prints its structure in a readable format.
 *
 * Usage: tpm debug_nv1
 */
static int cmd_tpm_debug_nv1(const struct shell *sh, size_t argc, char **argv)
{
	int rc;
	WOLFTPM2_DEV dev;
	WOLFTPM2_HANDLE parent;
	word32 nvIndex1 = 0x013ffffe;
	NV_Read_In nvReadIn;
	NV_Read_Out nvReadOut;
	struct TCM_NV_INDEX_TPCM1 nvIndexTPCM1;

	if (argc != 1) {
		shell_error(sh, "Usage: tpm debug_nv1");
		return -EINVAL;
	}

	shell_print(sh, "Reading and debugging NV index 0x%08X...", nvIndex1);

	/* Initialize TPM */
	rc = tpm_init_common(sh, &dev);
	if (rc != 0) {
		return rc;
	}

	/* Set auth handle */
	rc = tpm_set_auth_handle_common(sh, &dev, &parent, TPM_RH_OWNER);
	if (rc != 0) {
		tpm_cleanup_common(&dev);
		return rc;
	}

	/* Read NV index data */
	XMEMSET(&nvReadIn, 0, sizeof(nvReadIn));
	nvReadIn.authHandle = parent.hndl;
	nvReadIn.nvIndex = nvIndex1;
	nvReadIn.size = sizeof(nvIndexTPCM1);
	nvReadIn.offset = 0;

	rc = TPM2_NV_Read(&nvReadIn, &nvReadOut);
	if (rc != TPM_RC_SUCCESS) {
		shell_error(sh, "TPM2_NV_Read failed for index 0x%08X: 0x%x (%s)", nvIndex1, rc,
			    wolfTPM2_GetRCString(rc));
		tpm_cleanup_common(&dev);
		return -EIO;
	}

	tpm_cleanup_common(&dev);

	/* Copy data to buffer and parse */
	if (nvReadOut.data.size == 0) {
		shell_print(sh, "No data to debug.");
		return 0;
	}

	if (nvReadOut.data.size < sizeof(nvIndexTPCM1)) {
		shell_warn(
			sh,
			"Warning: Read data size (%d) is smaller than expected structure size (%d)",
			nvReadOut.data.size, sizeof(nvIndexTPCM1));
	}

	XMEMCPY(&nvIndexTPCM1, nvReadOut.data.buffer,
		nvReadOut.data.size < sizeof(nvIndexTPCM1) ? nvReadOut.data.size
							   : sizeof(nvIndexTPCM1));

	/* Debug output */
	shell_print(sh, "Debug NV1 (TPCM1 structure):");
	shell_print(sh, "Valid flags: %02x %02x %02x %02x", nvIndexTPCM1.valid[0],
		    nvIndexTPCM1.valid[1], nvIndexTPCM1.valid[2], nvIndexTPCM1.valid[3]);

	for (int i = 0; i < 2; i++) {
		shell_print(sh, "spiRom[%d]:", i);
		shell_print(sh, "  Valid flags: %02x %02x %02x %02x",
			    nvIndexTPCM1.spiRom[i].valid[0], nvIndexTPCM1.spiRom[i].valid[1],
			    nvIndexTPCM1.spiRom[i].valid[2], nvIndexTPCM1.spiRom[i].valid[3]);

		for (int j = 0; j < 4; j++) {
			shell_print(sh, "  spiRomsub[%d]:", j);
			shell_print(sh, "    startAddr: 0x%08x",
				    nvIndexTPCM1.spiRom[i].spiRomsub[j].startAddr);
			shell_print(sh, "    len: 0x%08x", nvIndexTPCM1.spiRom[i].spiRomsub[j].len);
			shell_print(sh, "    hashAlg: 0x%04x",
				    nvIndexTPCM1.spiRom[i].spiRomsub[j].hashAlg);
			shell_print(sh, "    policy: %02x %02x %02x %02x",
				    nvIndexTPCM1.spiRom[i].spiRomsub[j].policy[0],
				    nvIndexTPCM1.spiRom[i].spiRomsub[j].policy[1],
				    nvIndexTPCM1.spiRom[i].spiRomsub[j].policy[2],
				    nvIndexTPCM1.spiRom[i].spiRomsub[j].policy[3]);

			shell_fprintf(sh, SHELL_NORMAL, "    digest (%d bytes): ",
				      nvIndexTPCM1.spiRom[i].spiRomsub[j].digest.size);
			for (int k = 0;
			     k < nvIndexTPCM1.spiRom[i].spiRomsub[j].digest.size && k < 48; k++) {
				shell_fprintf(sh, SHELL_NORMAL, "%02x ",
					      nvIndexTPCM1.spiRom[i]
						      .spiRomsub[j]
						      .digest.digest.sm3_256[k]);
			}
			shell_print(sh, "");
		}
	}

	return 0;
}

/**
 * @brief Debug and print NV index TPCM2 structure
 *
 * This function reads NV index 2 data and prints its structure in a readable format.
 *
 * Usage: tpm debug_nv2
 */
static int cmd_tpm_debug_nv2(const struct shell *sh, size_t argc, char **argv)
{
	int rc;
	WOLFTPM2_DEV dev;
	WOLFTPM2_HANDLE parent;
	word32 nvIndex2 = 0x013fffff;
	NV_Read_In nvReadIn;
	NV_Read_Out nvReadOut;
	struct TCM_NV_INDEX_TPCM2 nvIndexTPCM2;

	if (argc != 1) {
		shell_error(sh, "Usage: tpm debug_nv2");
		return -EINVAL;
	}

	shell_print(sh, "Reading and debugging NV index 0x%08X...", nvIndex2);

	/* Initialize TPM */
	rc = tpm_init_common(sh, &dev);
	if (rc != 0) {
		return rc;
	}

	/* Set auth handle */
	rc = tpm_set_auth_handle_common(sh, &dev, &parent, TPM_RH_OWNER);
	if (rc != 0) {
		tpm_cleanup_common(&dev);
		return rc;
	}

	/* Read NV index data */
	XMEMSET(&nvReadIn, 0, sizeof(nvReadIn));
	nvReadIn.authHandle = parent.hndl;
	nvReadIn.nvIndex = nvIndex2;
	nvReadIn.size = sizeof(nvIndexTPCM2);
	nvReadIn.offset = 0;

	rc = TPM2_NV_Read(&nvReadIn, &nvReadOut);
	if (rc != TPM_RC_SUCCESS) {
		shell_error(sh, "TPM2_NV_Read failed for index 0x%08X: 0x%x (%s)", nvIndex2, rc,
			    wolfTPM2_GetRCString(rc));
		tpm_cleanup_common(&dev);
		return -EIO;
	}

	tpm_cleanup_common(&dev);

	/* Copy data to buffer and parse */
	if (nvReadOut.data.size == 0) {
		shell_print(sh, "No data to debug.");
		return 0;
	}

	if (nvReadOut.data.size < sizeof(nvIndexTPCM2)) {
		shell_warn(
			sh,
			"Warning: Read data size (%d) is smaller than expected structure size (%d)",
			nvReadOut.data.size, sizeof(nvIndexTPCM2));
	}

	XMEMCPY(&nvIndexTPCM2, nvReadOut.data.buffer,
		nvReadOut.data.size < sizeof(nvIndexTPCM2) ? nvReadOut.data.size
							   : sizeof(nvIndexTPCM2));

	/* Debug output */
	shell_print(sh, "Debug NV2 (TPCM2 structure):");
	shell_print(sh, "result: 0x%08x", nvIndexTPCM2.result);
	shell_print(sh, "spiromindex: 0x%08x", nvIndexTPCM2.spiromindex);
	shell_print(sh, "spiromindexSub: 0x%08x", nvIndexTPCM2.spiromindexSub);

	shell_fprintf(sh, SHELL_NORMAL,
		      "resultDigest (%d bytes): ", nvIndexTPCM2.resultDigest.size);
	for (int i = 0; i < nvIndexTPCM2.resultDigest.size && i < 48; i++) {
		shell_fprintf(sh, SHELL_NORMAL, "%02x ",
			      nvIndexTPCM2.resultDigest.digest.sm3_256[i]);
	}
	shell_print(sh, "");

	shell_print(sh, "rsvd: %02x %02x", nvIndexTPCM2.rsvd[0], nvIndexTPCM2.rsvd[1]);

	return 0;
}

static void initial_tpcm1(struct TCM_NV_INDEX_TPCM1 *tpcm1)
{
	XMEMSET(tpcm1, 0, sizeof(struct TCM_NV_INDEX_TPCM1));
	/* Set valid flags */
	tpcm1->valid[0] = 0x01; /* Valid flag for SPI ROM 0*/
	tpcm1->valid[1] = 0x00;
	tpcm1->valid[2] = 0x00;
	tpcm1->valid[3] = 0x00;

	/* Set SPI ROM 0 data */
	tpcm1->spiRom[0].valid[0] = 0x01; /* Valid flag for SPI ROM 0 sub-region 0 */
	tpcm1->spiRom[0].valid[1] = 0x00;
	tpcm1->spiRom[0].valid[2] = 0x00;
	tpcm1->spiRom[0].valid[3] = 0x00;

	/* Set SPI ROM 0 sub-region 0 data */
	tpcm1->spiRom[0].spiRomsub[0].startAddr = DEMO_ADDR;
	tpcm1->spiRom[0].spiRomsub[0].len = DEMO_LEN;
	tpcm1->spiRom[0].spiRomsub[0].hashAlg = TPM2_ALG_SM3_256;
	/* Set SPI ROM 0 sub-region 0 policy */
	tpcm1->spiRom[0].spiRomsub[0].policy[0] = (GOON >> 24) & 0xFF;
	tpcm1->spiRom[0].spiRomsub[0].policy[1] = (GOON >> 16) & 0xFF;
	tpcm1->spiRom[0].spiRomsub[0].policy[2] = (GOON >> 8) & 0xFF;
	tpcm1->spiRom[0].spiRomsub[0].policy[3] = (GOON) & 0xFF;

	/* Set SPI ROM 0 sub-region 0 digest */
	tpcm1->spiRom[0].spiRomsub[0].digest.size = SM3_DIGEST_SIZE;
	uint8_t sm3_digest[SM3_DIGEST_SIZE] = {0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0x34, 0x56,
					       0x78, 0x90, 0x12, 0x34, 0x56, 0x78, 0x90, 0x12,
					       0x34, 0x56, 0x78, 0x90, 0x12, 0x34, 0x56, 0x78,
					       0x90, 0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0x34};
	XMEMCPY(tpcm1->spiRom[0].spiRomsub[0].digest.digest.sm3_256, sm3_digest, SM3_DIGEST_SIZE);
	XMEMSET(tpcm1->spiRom[0].spiRomsub[0].digest.digest.reserve, 0xff, 16);

	for (int i = 1; i < 4; i++) {
		tpcm1->spiRom[0].spiRomsub[i].startAddr = 0x00000000;
		tpcm1->spiRom[0].spiRomsub[i].len = 0x00000000;
		tpcm1->spiRom[0].spiRomsub[i].hashAlg = TPM2_ALG_SM3_256;
		tpcm1->spiRom[0].spiRomsub[i].policy[0] = (GOON >> 24) & 0xFF;
		tpcm1->spiRom[0].spiRomsub[i].policy[1] = (GOON >> 16) & 0xFF;
		tpcm1->spiRom[0].spiRomsub[i].policy[2] = (GOON >> 8) & 0xFF;
		tpcm1->spiRom[0].spiRomsub[i].policy[3] = (GOON) & 0xFF;

		/* Set SPI ROM 0 sub-region 1 digest */
		tpcm1->spiRom[0].spiRomsub[i].digest.size = SM3_DIGEST_SIZE;
		XMEMSET(tpcm1->spiRom[0].spiRomsub[i].digest.digest.sm3_256, 0xff, SM3_DIGEST_SIZE);
		XMEMSET(tpcm1->spiRom[0].spiRomsub[i].digest.digest.reserve, 0xff, 16);
	}
	for (int j = 0; j < 4; j++) {
		tpcm1->spiRom[1].spiRomsub[j].startAddr = 0x00000000;
		tpcm1->spiRom[1].spiRomsub[j].len = 0x00000000;
		tpcm1->spiRom[1].spiRomsub[j].hashAlg = TPM2_ALG_SM3_256;
		tpcm1->spiRom[1].spiRomsub[j].policy[0] = (GOON >> 24) & 0xFF;
		tpcm1->spiRom[1].spiRomsub[j].policy[1] = (GOON >> 16) & 0xFF;
		tpcm1->spiRom[1].spiRomsub[j].policy[2] = (GOON >> 8) & 0xFF;
		tpcm1->spiRom[1].spiRomsub[j].policy[3] = (GOON) & 0xFF;

		/* Set SPI ROM 0 sub-region 1 digest */
		tpcm1->spiRom[1].spiRomsub[j].digest.size = SM3_DIGEST_SIZE;
		XMEMSET(tpcm1->spiRom[1].spiRomsub[j].digest.digest.sm3_256, 0xff, SM3_DIGEST_SIZE);
		XMEMSET(tpcm1->spiRom[1].spiRomsub[j].digest.digest.reserve, 0xff, 16);
	}
}

/**
 * @brief Read data from specified NV index and display
 *
 * Usage: tpm nvread NV1
 *        tpm nvread NV2
 */
static int cmd_tpm_nvread(const struct shell *sh, size_t argc, char **argv)
{
	int rc;
	WOLFTPM2_DEV dev;
	WOLFTPM2_HANDLE nv;
	word32 nvIndex;
	TPMS_NV_PUBLIC nvPublic;
	uint8_t *auth = NULL;
	int authSz = 0;
	unsigned char buffer[524];
	size_t bytesRead;
	NV_Read_In input;
	NV_Read_Out output;
	size_t readSize;

	/* Check parameter count */
	if (argc != 2) {
		shell_error(sh, "Usage:");
		shell_error(sh, "  tpm nvread NV1");
		shell_error(sh, "  tpm nvread NV2");
		return -EINVAL;
	}

	/* Parse and validate parameters */
	if (strcasecmp(argv[1], "NV1") == 0) {
		nvIndex = 0x013ffffe;
		readSize = 524; /* TPCM1 structure size */
	} else if (strcasecmp(argv[1], "NV2") == 0) {
		nvIndex = 0x013fffff;
		readSize = sizeof(struct TCM_NV_INDEX_TPCM2); /* TPCM2 structure size */
	} else {
		shell_error(sh, "Invalid NV index type: %s", argv[1]);
		shell_error(sh, "Must be 'NV1' or 'NV2'");
		return -EINVAL;
	}

	shell_print(sh, "Reading from NV index %s (0x%08X)...", argv[1], nvIndex);

	/* Initialize TPM */
	rc = tpm_init_common(sh, &dev);
	if (rc != 0) {
		return rc;
	}

	/* Set NV handle */
	nv.hndl = nvIndex;
	if (auth != NULL && authSz > 0) {
		nv.auth.size = authSz;
		XMEMCPY(nv.auth.buffer, auth, authSz);
	}

	/* Read NV public information to verify index exists */
	rc = wolfTPM2_NVReadPublic(&dev, nv.hndl, &nvPublic);
	if (rc != TPM_RC_SUCCESS) {
		shell_error(sh, "Failed to read NV public info: 0x%x", rc);
		tpm_cleanup_common(&dev);
		return -EIO;
	}

	/* Read NV data */
	XMEMSET(&input, 0, sizeof(input));
	input.authHandle = TPM_RH_OWNER;
	input.nvIndex = nvIndex;
	input.offset = 0;
	input.size = readSize;

	rc = TPM2_NV_Read(&input, &output);
	if (rc != TPM_RC_SUCCESS) {
		shell_error(sh, "TPM2_NV_Read failed: 0x%x", rc);
		tpm_cleanup_common(&dev);
		return -EIO;
	}

	bytesRead = output.data.size;
	XMEMCPY(buffer, output.data.buffer, bytesRead);

	tpm_cleanup_common(&dev);

	/* Display read data */
	shell_print(sh, "%s (0x%08X) - Read %zu bytes:", argv[1], nvIndex, bytesRead);
	for (size_t i = 0; i < bytesRead; i++) {
		shell_fprintf(sh, SHELL_NORMAL, "%02x ", buffer[i]);
		if ((i + 1) % 16 == 0) {
			shell_print(sh, "");
		}
	}
	if (bytesRead % 16 != 0) {
		shell_print(sh, "");
	}

	shell_print(sh, "NV read operation completed successfully");

	return 0;
}

/**
 * @brief Perform TPM Clear operation
 *
 * This function implements TPM Clear shell command for secure boot
 */
static int cmd_tpm_clear(const struct shell *sh, size_t argc, char **argv)
{
	int rc;
	WOLFTPM2_DEV dev;
	Clear_In clearIn;

	shell_print(sh, "Performing TPM Clear...");

	/* Initialize TPM */
	rc = tpm_init_common(sh, &dev);
	if (rc != 0) {
		return rc;
	}

	/* Perform TPM Clear */
	XMEMSET(&clearIn, 0, sizeof(clearIn));
	clearIn.authHandle = TPM_RH_PLATFORM;

	rc = TPM2_Clear(&clearIn);
	if (rc != TPM_RC_SUCCESS) {
		shell_error(sh, "TPM2_Clear failed: 0x%x (%s)", rc, wolfTPM2_GetRCString(rc));
		tpm_cleanup_common(&dev);
		return -EIO;
	}

	shell_print(sh, "TPM Clear completed successfully");

	tpm_cleanup_common(&dev);
	return 0;
}

/**
 * @brief 解析带前缀的策略字符串 (policy=xxx格式)
 */
static uint32_t parse_policy_string(const char *policy_str)
{
	const char *prefix = "policy=";

	if (strncmp(policy_str, prefix, strlen(prefix)) != 0) {
		return 0; /* Invalid format */
	}

	const char *policy_value = policy_str + strlen(prefix);

	if (strcasecmp(policy_value, "goon") == 0) {
		return GOON; /* 0x01ffffff */
	} else if (strcasecmp(policy_value, "hang") == 0) {
		return HANG; /* 0x00ffffff */
	}
	return 0; /* Invalid policy */
}

/**
 * @brief Calculate SM3 hash of specified Flash region
 */
static int calculate_uboot_hash_sm3(uint32_t start_addr, uint32_t length, uint8_t *hash_output)
{
	const struct device *dev;
	uint8_t buf[4096];
	SM3_CTX sm3_ctx;
	int rc;

	if (!hash_output) {
		return -EINVAL;
	}

	/* Get Flash device */
	dev = device_get_binding("qspi@e0000000");
	if (!dev) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(qspi0_flash));
	}
	if (!device_is_ready(dev)) {
		LOG_ERR("Failed to get flash device binding");
		return -ENODEV;
	}

	/* Initialize SM3 context */
	sm3_init(&sm3_ctx);

	uint32_t cur_offset = start_addr;
	uint32_t remain = length;

	/* Execute Flash read and calculate SM3 hash */
	while (remain > 0) {
		size_t len = MIN(sizeof(buf), remain);

		/* Read Flash data */
		rc = flash_read(dev, cur_offset, buf, len);
		if (rc) {
			LOG_ERR("Failed to read flash at offset 0x%08x, rc %d", cur_offset, rc);
			return rc;
		}

		/* Update SM3 hash calculation */
		sm3_update(&sm3_ctx, buf, len);

		cur_offset += len;
		remain -= len;
	}

	/* Complete SM3 hash calculation */
	sm3_finish(&sm3_ctx, hash_output);

	return 0;
}

/**
 * @brief TPM Measure Boot - measure boot implementation
 *
 * This command implements TPM measure boot functionality, writing U-Boot information to TPCM1
 * configuration
 *
 * Usage: tpm updatemeasure <u-boot addr> <u-boot size> <policy=goon|hang> [hash_value]
 *   - hash_value (optional): 64-bit hex string, if provided use user hash, otherwise calculate
 * actual hash
 */
static int cmd_tpm_updatemeasure(const struct shell *sh, size_t argc, char **argv)
{
	int rc;
	WOLFTPM2_DEV dev;
	NV_Write_In input;
	word32 nvIndex = 0x013ffffe; /* TPCM1 Index */
	TPMS_NV_PUBLIC nvPublic;
	struct TCM_NV_INDEX_TPCM1 tpcm1;
	uint8_t calculated_hash[SM3_DIGEST_SIZE];
	uint8_t final_hash[SM3_DIGEST_SIZE];
	unsigned char buffer[524];
	size_t bytesRead = 0;
	bool use_user_hash = false;

	/* Parsed parameters */
	uint32_t flash_addr;
	uint32_t flash_size;
	uint32_t policy;

	/* Check parameter count - support 4 or 5 parameters */
	if (argc != 4 && argc != 5) {
		shell_error(sh, "Command Usage Examples:");
		shell_error(sh, "  Auto-calculate: tpm updatemeasure <u-boot addr> <u-boot size>"
				" <policy=goon|hang>");
		shell_error(sh, "  Custom hash: tpm updatemeasure <u-boot addr> <u-boot size>"
				" <policy=goon|hang> [hash_value]");
		shell_error(sh, "  u-boot addr: The starting address of U-Boot "
				" (in hexadecimal format, e.g., 0x00100000)");
		shell_error(sh, "  u-boot size: The size of U-Boot "
				" (in hexadecimal format, e.g., 0x00080000)");
		shell_error(sh, "  policy: Security policy (policy=goon or policy=hang)");
		shell_error(sh, "  hash_value (optional): User-provided hash value "
				" (64-character hexadecimal string)");
		shell_error(sh, "Examples:");
		shell_error(sh, "  tpm updatemeasure 0x00100000 0x00080000 policy=goon");
		shell_error(sh,
			    "  tpm updatemeasure 0x00100000 0x00080000 policy=hang "
			    " 1234567890123456789012345678901234567890123456789012345678901234");
		return -EINVAL;
	}

	/* Parse U-Boot address */
	flash_addr = strtoul(argv[1], NULL, 0);

	/* Parse U-Boot size */
	flash_size = strtoul(argv[2], NULL, 0);
	if (flash_size == 0) {
		shell_error(sh, "Invalid Flash size: %s", argv[2]);
		shell_error(sh, "Size must be in hex format (e.g., 0x00080000)");
		return -EINVAL;
	}

	/* Parse security policy */
	policy = parse_policy_string(argv[3]);
	if (policy == 0) {
		shell_error(sh, "Invalid policy format: %s", argv[3]);
		shell_error(sh, "Expected format: policy=goon or policy=hang");
		shell_error(sh, "Valid policies:");
		shell_error(sh, "  policy=goon (0x%08X) - Continue on failure", GOON);
		shell_error(sh, "  policy=hang (0x%08X) - Halt on failure", HANG);
		return -EINVAL;
	}

	/* If hash value parameter is provided, parse user hash value */
	if (argc == 5) {
		const char *hexStr = argv[4];
		size_t hexLen = strlen(hexStr);

		if (hexLen != SM3_DIGEST_SIZE * 2) {
			shell_error(sh, "Hash must be exactly %d bytes (%d hex characters)",
				    SM3_DIGEST_SIZE, SM3_DIGEST_SIZE * 2);
			shell_error(sh, "Provided hash length: %zu characters", hexLen);
			return -EINVAL;
		}

		/* Parse user-provided hash value */
		for (size_t i = 0; i < SM3_DIGEST_SIZE; ++i) {
			if (sscanf(&hexStr[i * 2], "%2hhx", &final_hash[i]) != 1) {
				shell_error(sh, "Failed to parse hex at position %zu", i * 2);
				return -EINVAL;
			}
		}

		use_user_hash = true;
		shell_print(sh, "Using user-provided hash value");
	}

	/* Display parsed configuration */
	shell_print(sh, "Parsed configuration:");
	shell_print(sh, "  Flash Address: 0x%08X", flash_addr);
	shell_print(sh, "  Flash Size: 0x%08X (%u bytes)", flash_size, flash_size);
	shell_print(sh, "  Flash Range: 0x%08X - 0x%08X", flash_addr, flash_addr + flash_size - 1);

	/* Display policy information */
	if (policy == GOON) {
		shell_print(sh,
			    "Security Policy: GOON (0x%08X) - Continue execution on verification "
			    "failure",
			    policy);
	} else if (policy == HANG) {
		shell_print(sh,
			    "Security Policy: HANG (0x%08X) - Halt system on verification failure",
			    policy);
	}

	/* Display hash source */
	if (use_user_hash) {
		shell_print(sh, "  Hash Source: User-provided hash value");
		shell_print(sh, "  Provided Hash: ");
		for (int i = 0; i < SM3_DIGEST_SIZE; i++) {
			shell_fprintf(sh, SHELL_NORMAL, "%02x", final_hash[i]);
		}
		shell_print(sh, "");
	} else {
		shell_print(sh, "  Hash Source: Will calculate from flash content");
	}

	/* Check address range reasonableness */
	if (flash_size > 16 * 1024 * 1024) { /* 16MB limit */
		shell_warn(sh, "Warning: U-Boot size is very large (%u bytes)", flash_size);
	}
	if (flash_addr + flash_size < flash_addr) { /* Overflow check */
		shell_error(sh, "Address range overflow detected");
		return -EINVAL;
	}

	/* Calculate or use hash value */
	if (!use_user_hash) {
		/* Calculate SM3 hash of U-Boot region */
		rc = calculate_uboot_hash_sm3(flash_addr, flash_size, calculated_hash);
		if (rc != 0) {
			shell_error(sh, "Failed to calculate U-Boot hash: %d", rc);
			shell_error(sh, "Please check flash device and address range");
			return rc;
		}

		/* Use calculated hash value */
		XMEMCPY(final_hash, calculated_hash, SM3_DIGEST_SIZE);

		shell_print(sh, "U-Boot SM3 Hash calculated successfully:");
		for (int i = 0; i < SM3_DIGEST_SIZE; i++) {
			shell_fprintf(sh, SHELL_NORMAL, "%02x", calculated_hash[i]);
		}
		shell_print(sh, "");
	} else {
		shell_print(sh, "Using user-provided hash value (skipping flash calculation)");
	}

	initial_tpcm1(&tpcm1);

	/* Update TPCM1 configuration with Boot measure boot parameters */
	tpcm1.spiRom[0].spiRomsub[0].startAddr = flash_addr;
	tpcm1.spiRom[0].spiRomsub[0].len = flash_size;
	tpcm1.spiRom[0].spiRomsub[0].hashAlg = TPM2_ALG_SM3_256;

	/* Set security policy */
	tpcm1.spiRom[0].spiRomsub[0].policy[0] = (policy >> 24) & 0xFF;
	tpcm1.spiRom[0].spiRomsub[0].policy[1] = (policy >> 16) & 0xFF;
	tpcm1.spiRom[0].spiRomsub[0].policy[2] = (policy >> 8) & 0xFF;
	tpcm1.spiRom[0].spiRomsub[0].policy[3] = (policy) & 0xFF;

	/* Set final hash value to TPCM1 structure */
	tpcm1.spiRom[0].spiRomsub[0].digest.size = SM3_DIGEST_SIZE;
	XMEMCPY(tpcm1.spiRom[0].spiRomsub[0].digest.digest.sm3_256, final_hash, SM3_DIGEST_SIZE);
	/* Initialize TPM */
	rc = tpm_init_common(sh, &dev);
	if (rc != 0) {
		return rc;
	}

	/* Verify NV index exists */
	rc = wolfTPM2_NVReadPublic(&dev, nvIndex, &nvPublic);
	if (rc != TPM_RC_SUCCESS) {
		shell_error(sh, "Failed to read NV public info: 0x%x (%s)", rc,
			    wolfTPM2_GetRCString(rc));
		shell_error(sh, "Please run 'tpm nvdefine NV1' first to create NV indexes");
		tpm_cleanup_common(&dev);
		return -EIO;
	}

	/* Prepare to write TPCM1 data */
	bytesRead = sizeof(tpcm1);
	XMEMCPY(buffer, &tpcm1, bytesRead);

	XMEMSET(&input, 0, sizeof(input));
	input.authHandle = TPM_RH_OWNER;
	input.nvIndex = nvIndex;
	input.data.size = bytesRead;
	if (bytesRead > sizeof(input.data.buffer)) {
		bytesRead = sizeof(input.data.buffer);
	}
	XMEMCPY(input.data.buffer, buffer, bytesRead);
	input.offset = 0;

	/* Execute write operation */
	rc = TPM2_NV_Write(&input);
	if (rc != TPM_RC_SUCCESS) {
		shell_error(sh, "TPM2_NV_Write failed: 0x%x (%s)", rc, wolfTPM2_GetRCString(rc));
		tpm_cleanup_common(&dev);
		return -EIO;
	}

	tpm_cleanup_common(&dev);

	/* Display operation result */
	shell_print(sh, "=== TPM Measure Boot Configuration Completed ===");
	shell_print(sh, "Flash measure boot configuration updated successfully");
	shell_print(sh, "Monitored region: 0x%08X - 0x%08X (%u bytes)", flash_addr,
		    flash_addr + flash_size - 1, flash_size);

	return 0;
}

static int tpm_init_common(const struct shell *sh, WOLFTPM2_DEV *dev)
{
	int rc;

	rc = wolfTPM2_Init(dev, TPM2_Zephyr_IoCb, &_tpm_user_ctx);
	if (rc != TPM_RC_SUCCESS) {
		shell_error(sh, "wolfTPM2_Init failed: 0x%x (%s)", rc, wolfTPM2_GetRCString(rc));
		return -EIO;
	}

	return 0;
}

static void tpm_cleanup_common(WOLFTPM2_DEV *dev)
{
	wolfTPM2_Cleanup(dev);
}

static int tpm_set_auth_handle_common(const struct shell *sh, WOLFTPM2_DEV *dev,
				      WOLFTPM2_HANDLE *parent, TPM_HANDLE auth_handle)
{
	int rc;

	parent->hndl = auth_handle;

	/* Set session auth */
	if (dev->ctx.session && !parent->policyAuth) {
		rc = wolfTPM2_SetAuthHandle(dev, 0, parent);
		if (rc != TPM_RC_SUCCESS) {
			shell_error(sh, "wolfTPM2_SetAuthHandle failed: 0x%x (%s)", rc,
				    wolfTPM2_GetRCString(rc));
			return -EIO;
		}
	}

	return 0;
}

int TPM2_Zephyr_IoCb(TPM2_CTX *ctx, const byte *txBuf, byte *rxBuf, word16 xferSz, void *userCtx)
{
	struct spi_buf tx_bufs[] = {
		{
			.buf = (void *)txBuf,
			.len = xferSz,
		},
	};
	struct spi_buf rx_bufs[] = {
		{
			.buf = rxBuf,
			.len = xferSz,
		},
	};
	struct spi_buf_set _tx = {
		.buffers = tx_bufs,
		.count = (size_t)ARRAY_SIZE(tx_bufs),
	};
	struct spi_buf_set _rx = {
		.buffers = rx_bufs,
		.count = (size_t)ARRAY_SIZE(rx_bufs),
	};

	struct {
		const struct device *dev;
		struct spi_config *cfg;
	} *user_ctx = userCtx;

	if (!user_ctx || !user_ctx->dev || !user_ctx->cfg) {
		LOG_ERR("Invalid user context or SPI configuration");
		return TPM_RC_FAILURE;
	}

	int ret = spi_transceive(user_ctx->dev, user_ctx->cfg, &_tx, &_rx);

	if (ret) {
		LOG_ERR("SPI transaction failed: %d", ret);
		LOG_ERR("Check SPI device configuration and connections.");
		return TPM_RC_FAILURE;
	}

	/* Add a small delay to ensure TPM has time to process */
	k_busy_wait(100); /* 10ms delay */

	return TPM_RC_SUCCESS;
}

/* Define the subcommands for the 'tpm' command */
/* Subcommand set for 'tpm' */
SHELL_STATIC_SUBCMD_SET_CREATE(
	tpm_cmds, SHELL_CMD_ARG(clear, NULL, "TPM Clear\nUsage:tpm clear", cmd_tpm_clear, 1, 0),
	SHELL_CMD_ARG(nvdefine, NULL, "Define dual NV indexes for secure boot\nUsage: tpm nvdefine",
		      cmd_tpm_nvdefine, 2, 0),
	SHELL_CMD_ARG(nvundefine, NULL,
		      "Undefine dual NV indexes for secure boot\nUsage: tpm nvundefine",
		      cmd_tpm_nvundefine, 2, 0),
	SHELL_CMD_ARG(nvread, NULL, "Read data from NV indexes\nUsage: tpm nvread", cmd_tpm_nvread,
		      2, 0),
	SHELL_CMD_ARG(dump_NV1, NULL,
		      "Debug and print NV index TPCM1 structure\nUsage: tpm debug_nv1",
		      cmd_tpm_debug_nv1, 1, 0),
	SHELL_CMD_ARG(dump_NV2, NULL,
		      "Debug and print NV index TPCM2 structure\nUsage: tpm debug_nv2",
		      cmd_tpm_debug_nv2, 1, 0),
	SHELL_CMD_ARG(updatemeasure, NULL,
		      "TPM Measure Boot Configuration\nUsage: tpm measureboot <flash addr> "
		      "<flash size> <policy=goon|hang>",
		      cmd_tpm_updatemeasure, 4, 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(tpm, &tpm_cmds, "TPM commands:\n", NULL);
