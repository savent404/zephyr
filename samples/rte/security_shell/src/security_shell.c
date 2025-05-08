/**
 * @file security_shell.c
 * @author Liao,YuanKai(savent_gate@outlook.com)
 * @brief
 * @version 0.1
 * @date 2025-05-06
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "security_shell.h"
#include "wolfssl_settings.h"
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>

/* wolfSSL includes */
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h> /* Required for ecc_key and ECC_SECP256R1 */
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/asn.h>

#include <wolfssl/wolfcrypt/error-crypt.h> /* For wc_ErrorString */
#include <wolfssl/wolfcrypt/asn_public.h>  /* For DerBuffer */

#include <wolfssl/wolfcrypt/hash.h>

LOG_MODULE_REGISTER(security_shell, CONFIG_LOG_DEFAULT_LEVEL);

static int initialize_rng(WC_RNG **rng)
{
	/* Initialize the random number generator */
	*rng = (WC_RNG *)k_malloc(sizeof(WC_RNG));
	/* Check if memory allocation was successful */
	if (*rng == NULL) {
		LOG_ERR("Failed to allocate memory for RNG");
		return -ENOMEM;
	}
	int ret = wc_InitRng(*rng);

	if (ret != 0) {
		LOG_ERR("RNG initialization failed with error: %d", ret);
		k_free(*rng);
		*rng = NULL;
		return -EIO;
	}
	return 0;
}

static void cleanup_rng(WC_RNG *rng)
{
	if (rng != NULL) {
		int ret = wc_FreeRng(rng);

		if (ret != 0) {
			LOG_ERR("Failed to free RNG: %d", ret);
		}
		k_free(rng);
	}
}

#define PEM_BUFFER_SIZE  8192
#define MAX_KEY_NUM      32
#define MY_MAX_KEY_SIZE  1024
#define MAX_SIGN_STR_LEN 512

size_t strnlen(const char *s, size_t maxlen);

typedef enum {
	KEY_TYPE_PRIV,
	KEY_TYPE_PUB,
	KEY_TYPE_CSR,
	KEY_TYPE_CERT,
	KEY_TYPE_SIGSTR, /* Key type for signing strings */
	KEY_TYPE_MAX,    /* Key size number */
} key_type_t;
typedef struct {
	bool is_used; /* Key is used or not */
	key_type_t type;
	size_t size; /* Key actual size */
} key_info_t;
/* Key buffer and information */
static uint8_t key_buf[MAX_KEY_NUM][MY_MAX_KEY_SIZE];
/* Key information */
static key_info_t key_info[MAX_KEY_NUM];

/* Free key buffer */
static int keybuf_store_free(int id)
{
	key_info_t *kinfo = &key_info[id];
	uint8_t *kbuf = key_buf[id];

	if (id < 0 || id >= MAX_KEY_NUM) {
		LOG_ERR("Invalid key id");
		return -EINVAL;
	}
	if (!kinfo->is_used) {
		LOG_ERR("Key is not used");
		return -ENOENT;
	}

	memset(kbuf, 0, MY_MAX_KEY_SIZE);
	kinfo->is_used = false;
	kinfo->size = 0;

	LOG_INF("Key buffer %d freed", id);
	return 0;
}

/* Load key/csr/... to key buffer */
static int load_keybuf(key_type_t type, const uint8_t *key, size_t size)
{
	int id = -1;

	if (type >= KEY_TYPE_MAX) {
		LOG_ERR("Invalid key type");
		return -EINVAL;
	}
	if (size > MY_MAX_KEY_SIZE) {
		LOG_ERR("Key size is too large: %d > %d", size, MY_MAX_KEY_SIZE);
		return -EINVAL;
	}
	/* Find an available key slot */
	for (int i = 0; i < MAX_KEY_NUM; i++) {
		if (!key_info[i].is_used) {
			id = i;
			break;
		}
	}
	if (id < 0) {
		LOG_ERR("No space for key");
		return -ENOSPC;
	}

	key_info_t *kinfo = &key_info[id];
	uint8_t *kbuf = key_buf[id];

	/* Copy key to buffer */
	kinfo->is_used = true;
	kinfo->type = type;
	kinfo->size = size;
	memcpy(kbuf, key, size);

	LOG_INF("Key loaded, id: %d, type: %d, size: %d", id, type, size);
	return id;
}

/* Get ECCkey from buffer */
static int load_buf_get_ecckey(int id, ecc_key **key)
{
	int ret;
	const uint8_t *data;
	size_t size;
	word32 idx = 0;
	ecc_key *temp_key;

	if (id < 0 || id >= MAX_KEY_NUM) {
		LOG_ERR("Invalid key id");
		return -EINVAL;
	}

	const key_info_t *kinfo = &key_info[id];
	const uint8_t *kbuf = key_buf[id];

	if (!kinfo->is_used) {
		LOG_ERR("Key is not used");
		return -ENOENT;
	}
	if (kinfo->type != KEY_TYPE_PRIV && kinfo->type != KEY_TYPE_PUB) {
		LOG_ERR("Key type is not ecc private or public key");
		return -EINVAL;
	}

	data = kbuf;
	size = kinfo->size;

	/* Allocate memory for the ECC key */
	temp_key = (ecc_key *)k_malloc(sizeof(ecc_key));
	if (temp_key == NULL) {
		LOG_ERR("Failed to allocate memory for ECC key");
		return -ENOMEM;
	}
	ret = wc_ecc_init(temp_key);
	if (ret != 0) {
		LOG_ERR("Failed to initialize ECC key: %d", ret);
		k_free(temp_key);
		return -EIO;
	}

	/* Decode the ECC key */
	if (kinfo->type == KEY_TYPE_PRIV) {
		ret = wc_EccPrivateKeyDecode(data, &idx, temp_key, size);
		if (ret != 0) {
			LOG_ERR("Failed to decode ECC private key: %d", ret);
			wc_ecc_free(temp_key);
			k_free(temp_key);
			return -EIO;
		}
	} else {
		ret = wc_EccPublicKeyDecode(data, &idx, temp_key, size);
		if (ret != 0) {
			LOG_ERR("Failed to decode ECC public key: %d", ret);
			wc_ecc_free(temp_key);
			k_free(temp_key);
			return -EIO;
		}
	}
	*key = temp_key;
	return 0;
}

/* Get key buffer */
static int load_buf_get(int id, const uint8_t **data, size_t *size)
{
	if (id < 0 || id >= MAX_KEY_NUM) {
		LOG_ERR("Invalid key id");
		return -EINVAL;
	}

	const key_info_t *kinfo = &key_info[id];
	const uint8_t *kbuf = key_buf[id];

	if (!kinfo->is_used) {
		LOG_ERR("Key is not used");
		return -EINVAL;
	}

	*data = kbuf;
	*size = kinfo->size;

	return 0;
}

static int generate_ecc_key(const struct shell *sh, WC_RNG *rng)
{

	int ret = 0;
	int priv_id = -1;
	int pub_id = -1;

	byte derPriv[512];
	byte derPub[256];
	byte pemBuffer[PEM_BUFFER_SIZE];
	word32 derPrivLen;
	word32 derPubLen;
	word32 pemLen;

	/* Initialize wolfCrypt */
	wolfCrypt_Init();

	ecc_key *key = (ecc_key *)k_malloc(sizeof(ecc_key));

	if (key == NULL) {
		shell_print(sh, "Failed to allocate memory for ECC key");
		return -ENOMEM;
	}

	/* Initialize the ECC key */
	ret = wc_ecc_init(key);
	if (ret != 0) {
		shell_print(sh, "Failed to initialize ECC key: %d", ret);
		ret = -EIO;
		goto cleanup;
	}
	/* Generate the ECC key */
	ret = wc_ecc_make_key_ex(rng, 32, key, ECC_SECP256R1);
	if (ret != 0) {
		shell_print(sh, "Failed to generate ECC key: %d", ret);
		ret = -EIO;
		goto cleanup;
	}

	/* Convert the ECC private key to DER format */
	derPrivLen = wc_EccPrivateKeyToDer(key, derPriv, sizeof(derPriv));
	if (derPrivLen <= 0) {
		shell_print(sh, "Failed to convert ECC private key to DER: %d", derPrivLen);
		ret = -EIO;
		goto cleanup;
	}
	/* Convert the ECC private key to PEM format */
	pemLen =
		wc_DerToPem(derPriv, derPrivLen, pemBuffer, sizeof(pemBuffer), ECC_PRIVATEKEY_TYPE);
	if (pemLen <= 0) {
		shell_print(sh, "Failed to convert ECC private key to PEM: %d", pemLen);
		ret = -EIO;
		goto cleanup;
	}

	/* Load the private key to buffer */
	priv_id = load_keybuf(KEY_TYPE_PRIV, derPriv, derPrivLen);
	if (priv_id < 0) {
		shell_print(sh, "Failed to load private key to buffer: %d", priv_id);
		ret = priv_id;
		goto cleanup;
	}
	shell_print(sh, "Private key loaded to buffer, id: %d", priv_id);

	/* Convert the ECC public key to DER format */
	derPubLen = wc_EccPublicKeyToDer(key, derPub, sizeof(derPub), 1);
	if (derPubLen <= 0) {
		shell_print(sh, "Failed to convert ECC public key to DER: %d", derPubLen);
		ret = -EIO;
		goto cleanup;
	}
	/* Convert the ECC public key to PEM format */
	pemLen = wc_DerToPem(derPub, derPubLen, pemBuffer, sizeof(pemBuffer), ECC_PUBLICKEY_TYPE);
	if (pemLen <= 0) {
		shell_print(sh, "Failed to convert ECC public key to PEM: %d", pemLen);
		ret = -EIO;
		goto cleanup;
	}

	/* Load the public key to buffer */
	pub_id = load_keybuf(KEY_TYPE_PUB, derPub, derPubLen);
	if (pub_id < 0) {
		shell_print(sh, "Failed to load public key to buffer: %d", pub_id);
		ret = pub_id;
		goto cleanup;
	}
	shell_print(sh, "Public key loaded to buffer, id: %d", pub_id);
cleanup:
	if (ret != 0) {
		keybuf_store_free(priv_id);
		keybuf_store_free(pub_id);
	}
	if (key) {
		wc_ecc_free(key);
		k_free(key);
	}
	return ret;
}

/* Create keypair from private key */
static int create_keypair(ecc_key *priv_key, ecc_key **keypair_out)
{
	ecc_key *keypair;
	int ret;
	byte der_buf[1024];
	word32 der_len = sizeof(der_buf);
	word32 idx = 0;

	keypair = (ecc_key *)k_malloc(sizeof(ecc_key));
	if (keypair == NULL) {
		LOG_ERR("Failed to allocate memory for keypair");
		return -ENOMEM;
	}

	ret = wc_ecc_init(keypair);
	if (ret != 0) {
		LOG_ERR("Failed to initialize keypair: %d", ret);
		ret = -EINVAL;
		goto cleanup_alloc;
	}

	ret = wc_EccPrivateKeyToDer(priv_key, der_buf, der_len);
	if (ret <= 0) {
		LOG_ERR("Failed to export private key to DER: %d", ret);
		ret = -EIO;
		goto cleanup_ecc;
	}
	der_len = ret; /* Update der_len with actual size */

	/* Import the private key back into the new keypair */
	ret = wc_EccPrivateKeyDecode(der_buf, &idx, keypair, der_len);
	if (ret != 0) {
		LOG_ERR("Failed to import private key from DER: %d", ret);
		ret = -EINVAL;
		goto cleanup_ecc;
	}

	/* Explicitly make sure the public key part is computed */
	ret = wc_ecc_make_pub(keypair, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to calculate public key: %d", ret);
		ret = -EIO;
		goto cleanup_ecc;
	}

	/* Set the output parameter */
	*keypair_out = keypair;
	return 0;

cleanup_ecc:
	wc_ecc_free(keypair);
cleanup_alloc:
	k_free(keypair);
	return ret;
}

static int cmd_ss_genkey(const struct shell *sh, size_t argc, char **argv)
{
	static const char *const algorithms[] = {
		"ecc256",
		/* Add more algorithms as needed */
	};
	const char *chosen_algorithm = NULL;
	int chosen_algorithm_index = -1;

	if (argc == 1) {
		chosen_algorithm = algorithms[0]; /* Default to the first algorithm */
	} else if (argc == 2) {
		chosen_algorithm = argv[1];
	} else {
		shell_print(sh, "Invalid arguments number");
		return -EINVAL;
	}

	/* determine the chosen algorithm */
	for (int i = 0; i < ARRAY_SIZE(algorithms); i++) {
		if (strcmp(chosen_algorithm, algorithms[i]) == 0) {
			chosen_algorithm_index = i;
			break;
		}
	}

	if (chosen_algorithm_index == -1) {
		shell_print(sh, "Invalid algorithm: %s", chosen_algorithm);
		return -EINVAL;
	}

	LOG_INF("Generating key with algorithm: %s[%d]", chosen_algorithm, chosen_algorithm_index);

	WC_RNG *rng = NULL;

	int ret = initialize_rng(&rng);

	if (ret != 0) {
		shell_print(sh, "Failed to initialize RNG: %d", ret);
		return ret;
	}

	if (chosen_algorithm_index == 0) {
		ret = generate_ecc_key(sh, rng);
	} else {
		shell_print(sh, "Unsupported algorithm index: %d", chosen_algorithm_index);
		return -EINVAL;
	}

	cleanup_rng(rng);
	return ret;
}

static int cmd_ss_gencsr(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_print(sh, "Invalid arguments number");
		return -EINVAL;
	}

	const char *subject = argv[1];
	int priv_key_id = atoi(argv[2]);

	if (priv_key_id < 0 || priv_key_id >= MAX_KEY_NUM) {
		shell_print(sh, "Invalid private key id: %d", priv_key_id);
		return -EINVAL;
	}
	const key_info_t *kinfo = &key_info[priv_key_id];

	if (!kinfo->is_used) {
		shell_print(sh, "Key id %d is not used", priv_key_id);
		return -EINVAL;
	}
	if (kinfo->type != KEY_TYPE_PRIV) {
		shell_print(sh, "Key id %d is not a private key", priv_key_id);
		return -EINVAL;
	}

	int ret;
	int csr_id = -1;
	ecc_key *priv_key = NULL;
	ecc_key *keypair = NULL;
	WC_RNG *rng = NULL;

	ret = initialize_rng(&rng);
	if (ret != 0) {
		shell_print(sh, "Failed to initialize RNG: %d", ret);
		goto cleanup;
	}

	/* Load the private key for signing */
	ret = load_buf_get_ecckey(priv_key_id, &priv_key);
	if (ret != 0) {
		shell_print(sh, "Failed to get private key from buffer: %d", ret);
		goto cleanup;
	}
	shell_print(sh, "Private key loaded from buffer, id: %d", priv_key_id);

	/* Create a copy of private key to use as keypair for CSR generation */
	ret = create_keypair(priv_key, &keypair);
	if (ret != 0) {
		shell_print(sh, "Failed to create keypair from private key: %d", ret);
		goto cleanup;
	}

	/* CSR generation buffers */
	Cert csr;
	byte csrBuffer[PEM_BUFFER_SIZE];
	byte pemBuffer[PEM_BUFFER_SIZE];
	word32 csrLen;
	word32 pemLen;

	/* Set CSR signature type */
	csr.sigType = CTC_SHA256wECDSA;

	/* Initialize the CSR structure */
	ret = wc_InitCert(&csr);
	if (ret != 0) {
		shell_print(sh, "Failed to initialize CSR: %d", ret);
		ret = -EIO;
		goto cleanup;
	}

	/* Set the subject name for the CSR */
	snprintf(csr.subject.commonName, sizeof(csr.subject.commonName), "%s", subject);

	/* Generate the CSR in DER format */
	csrLen = wc_MakeCertReq(&csr, csrBuffer, sizeof(csrBuffer), NULL, keypair);
	if (csrLen <= 0) {
		shell_print(sh, "Failed to generate CSR in DER format: %d", csrLen);
		ret = -EIO;
		goto cleanup;
	}

	/* Use SHA-256 to sign the CSR */
	ret = wc_SignCert(csrLen, CTC_SHA256wECDSA, csrBuffer, sizeof(csrBuffer), NULL, priv_key,
			  rng);
	if (ret <= 0) {
		shell_print(sh, "Failed to sign CSR: %d", ret);
		char errorBuf[80];

		wc_ErrorString(ret, errorBuf);
		shell_print(sh, "Detailed error: %s", errorBuf);
		ret = -EIO;
		goto cleanup;
	}
	csrLen = ret; /* Update csrLen to the return value of wc_SignCert */

	/* Convert the CSR to PEM format */
	pemLen = wc_DerToPem(csrBuffer, csrLen, pemBuffer, sizeof(pemBuffer), CERTREQ_TYPE);
	if (pemLen <= 0) {
		shell_print(sh, "Failed to convert CSR to PEM: %d", pemLen);
		ret = -EIO;
		goto cleanup;
	}

	/* Load the PEM CSR to buffer */
	csr_id = load_keybuf(KEY_TYPE_CSR, csrBuffer, csrLen);
	if (csr_id < 0) {
		shell_print(sh, "Failed to load CSR to buffer: %d", csr_id);
		ret = -EIO;
		goto cleanup;
	}
	shell_print(sh, "CSR loaded to buffer, id: %d", csr_id);

cleanup:
	if (priv_key) {
		wc_ecc_free(priv_key);
		k_free(priv_key);
	}
	if (keypair) {
		wc_ecc_free(keypair);
		k_free(keypair);
	}
	cleanup_rng(rng);
	return ret;
}

static char pem_buffer[PEM_BUFFER_SIZE];           /* PEM buffer for storing PEM data */
static size_t pem_buffer_len;                      /* Current length of the PEM buffer */
static key_type_t current_key_type = KEY_TYPE_MAX; /* Current key type */

/* Initialize the PEM buffer */
static void init_pem_buffer(void)
{
	memset(pem_buffer, 0, sizeof(pem_buffer));
	pem_buffer_len = 0;
	current_key_type = KEY_TYPE_MAX;
}

/* Handle BEGIN line  */
static int handle_begin_line(const struct shell *sh, const char *pem_str, size_t len,
			     key_type_t key_type)
{
	/* Initialize the buffer and set the current key type */
	init_pem_buffer();
	current_key_type = key_type;
	/* Add the BEGIN line to the buffer */
	memcpy(pem_buffer, pem_str, len);
	pem_buffer_len = len;

	if (len > 0 && pem_buffer[pem_buffer_len - 1] != '\n') {
		pem_buffer[pem_buffer_len++] = '\n';
	}
	pem_buffer[pem_buffer_len] = '\0';

	shell_print(sh, "PEM buffer not complete, continue to input");
	return 0;
}

/* Add a line to the PEM buffer */
static int add_pem_line(const struct shell *sh, const char *pem_str, size_t len)
{
	/* Check if the buffer has been initialized */
	if (pem_buffer_len == 0 || current_key_type == KEY_TYPE_MAX) {
		shell_print(sh, "No PEM input started, please start with BEGIN line");
		return -EINVAL;
	}

	/* Check if the buffer has enough space */
	if (pem_buffer_len + len + 2 >= PEM_BUFFER_SIZE) {
		shell_print(sh, "PEM buffer overflow (current: %d, adding: %d, max: %d)",
			    pem_buffer_len, len + 2, PEM_BUFFER_SIZE);
		return -ENOSPC;
	}

	/* Add the current line to the buffer */
	memcpy(pem_buffer + pem_buffer_len, pem_str, len);
	pem_buffer_len += len;

	/* Ensure the buffer is properly null-terminated */
	pem_buffer[pem_buffer_len] = '\0';

	return 0;
}

static int handle_complete_pem(const struct shell *sh, int key_type_enum)
{
	int ret;
	DerBuffer *der = NULL;

	/* PEM ends with a newline */
	if (pem_buffer_len > 0 && pem_buffer[pem_buffer_len - 1] != '\n') {
		pem_buffer[pem_buffer_len++] = '\n';
		pem_buffer[pem_buffer_len] = '\0';
	}

	/* Process the complete PEM buffer */
	ret = wc_PemToDer((const byte *)pem_buffer, (long)pem_buffer_len, key_type_enum, &der, NULL,
			  NULL, NULL);

	if (ret != 0 || der == NULL) {
		shell_print(sh, "Error: PEM to DER conversion failed: %d", ret);
		if (ret != 0) {
			char errstr[80];

			wc_ErrorString(ret, errstr);
			shell_print(sh, "WolfSSL error: %s", errstr);
		}
		return -EIO;
	}

	/* Load the key into the buffer */
	int key_id = load_keybuf(current_key_type, der->buffer, der->length);

	/* Free the DER buffer */
	wc_FreeDer(&der);

	if (key_id < 0) {
		shell_print(sh, "Failed to load key to buffer: %d", key_id);
		return -EIO;
	}

	/* Convert key_type to corresponding string */
	const char *type_str = NULL;

	switch (current_key_type) {
	case KEY_TYPE_PRIV:
		type_str = "priv";
		break;
	case KEY_TYPE_PUB:
		type_str = "pub";
		break;
	case KEY_TYPE_CSR:
		type_str = "csr";
		break;
	case KEY_TYPE_CERT:
		type_str = "cert";
		break;
	default:
		type_str = "unknown";
		break;
	}
	shell_print(sh, "Key loaded to buffer, type: %s, id: %d", type_str, key_id);
	return key_id;
}

/* Input PEM data */
static int input_pem(const struct shell *sh, key_type_t key_type, int key_type_enum,
		     const char *pem_str)
{
	size_t len = 0;

	while (len < (PEM_BUFFER_SIZE - 1) && pem_str[len] != '\0') {
		len++;
	}

	/* Check if the input string is too long */
	if (len >= PEM_BUFFER_SIZE) {
		shell_print(sh, "PEM string is too long");
		return -EINVAL;
	}

	bool is_begin_line = (strstr(pem_str, "-----BEGIN") != NULL);
	bool is_end_line = (strstr(pem_str, "-----END") != NULL);

	/* Check if the input is empty or contains only whitespace */
	bool is_empty = true;

	for (size_t i = 0; i < len; i++) {
		if (!isspace((unsigned char)pem_str[i])) {
			is_empty = false;
			break;
		}
	}

	if (is_begin_line) {
		return handle_begin_line(sh, pem_str, len, key_type);
	}

	if (is_empty) {
		/* If already receiving PEM data, add a newline */
		if (pem_buffer_len > 0 && pem_buffer_len < PEM_BUFFER_SIZE - 1) {
			pem_buffer[pem_buffer_len++] = '\n';
			pem_buffer[pem_buffer_len] = '\0';
		} else {
			shell_print(sh, "Empty line ignored");
		}
		return 0;
	}

	int ret = add_pem_line(sh, pem_str, len);

	if (ret < 0) {
		return ret;
	}

	if (is_end_line) {
		return handle_complete_pem(sh, key_type_enum);
	}
	shell_print(sh, "PEM buffer not complete, continue to input");
	return 0;
}

static int cmd_ss_load(const struct shell *sh, size_t argc, char **argv)
{
	/* Check if the PEM buffer is ready for loading */
	if (pem_buffer_len > 0 && current_key_type != KEY_TYPE_MAX && argc == 2) {
		int key_type_enum;

		switch (current_key_type) {
		case KEY_TYPE_PRIV:
			key_type_enum = ECC_PRIVATEKEY_TYPE;
			break;
		case KEY_TYPE_PUB:
			key_type_enum = ECC_PUBLICKEY_TYPE;
			break;
		case KEY_TYPE_CSR:
			key_type_enum = CERTREQ_TYPE;
			break;
		case KEY_TYPE_CERT:
			key_type_enum = CERT_TYPE;
			break;
		default:
			shell_print(sh, "Invalid key type");
			return -EINVAL;
		}
		return input_pem(sh, current_key_type, key_type_enum, argv[1]);
	}

	if (argc < 3) {
		shell_print(sh, "Invalid arguments number");
		return -EINVAL;
	}

	const char *key_type_str = argv[1];
	const char *pem_str = argv[2];
	key_type_t key_type;
	int key_type_enum = -1;

	/* Determine the key type */
	if (strcmp(key_type_str, "priv") == 0) {
		key_type = KEY_TYPE_PRIV;
		key_type_enum = ECC_PRIVATEKEY_TYPE;
	} else if (strcmp(key_type_str, "pub") == 0) {
		key_type = KEY_TYPE_PUB;
		key_type_enum = ECC_PUBLICKEY_TYPE;
	} else if (strcmp(key_type_str, "csr") == 0) {
		key_type = KEY_TYPE_CSR;
		key_type_enum = CERTREQ_TYPE;
	} else if (strcmp(key_type_str, "cert") == 0) {
		key_type = KEY_TYPE_CERT;
		key_type_enum = CERT_TYPE;
	} else {
		shell_print(sh, "Invalid key type: %s", key_type_str);
		shell_print(sh, "Valid key types: priv, pub, csr, cert");
		return -EINVAL;
	}
	return input_pem(sh, key_type, key_type_enum, pem_str);
}

static int cmd_ss_dump(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Invalid arguments number");
		return -EINVAL;
	}

	int key_id = atoi(argv[1]);
	int key_type_enum = -1;
	const uint8_t *data;
	size_t size;
	int ret;

	ret = load_buf_get(key_id, &data, &size);
	if (ret != 0) {
		shell_print(sh, "Failed to get key from buffer: %d", ret);
		return -EIO;
	}

	switch (key_info[key_id].type) {
	case KEY_TYPE_PRIV:
		key_type_enum = ECC_PRIVATEKEY_TYPE;
		break;
	case KEY_TYPE_PUB:
		key_type_enum = ECC_PUBLICKEY_TYPE;
		break;
	case KEY_TYPE_CSR:
		key_type_enum = CERTREQ_TYPE;
		break;
	case KEY_TYPE_CERT:
		key_type_enum = CERT_TYPE;
		break;
	case KEY_TYPE_SIGSTR:
		shell_print(sh, "key_id: %d, type: signature, length: %zu", key_id, size);
		shell_print(sh, "Signature (hex):");
		for (size_t i = 0; i < size; i++) {
			shell_fprintf(sh, SHELL_NORMAL, "%02x", data[i]);
			if ((i + 1) % 16 == 0 && i < size - 1) {
				shell_fprintf(sh, SHELL_NORMAL, "\n");
			}
		}
		shell_fprintf(sh, SHELL_NORMAL, "\n");
		return 0;
	default:
		shell_print(sh, "Invalid key type");
		return -EINVAL;
	}

	byte pemBuffer[PEM_BUFFER_SIZE];
	word32 pemLen;

	/* Convert DER to PEM */
	pemLen = wc_DerToPem(data, size, pemBuffer, sizeof(pemBuffer), key_type_enum);
	if (pemLen <= 0) {
		shell_print(sh, "Failed to convert DER to PEM: %d", pemLen);
		return -EIO;
	}

	/* Ensure the buffer is properly null-terminated */
	pemBuffer[pemLen] = '\0';

	/* Convert key_type to string */
	const char *type_str = NULL;

	switch (key_info[key_id].type) {
	case KEY_TYPE_PRIV:
		type_str = "priv";
		break;
	case KEY_TYPE_PUB:
		type_str = "pub";
		break;
	case KEY_TYPE_CSR:
		type_str = "csr";
		break;
	case KEY_TYPE_CERT:
		type_str = "cert";
		break;
	default:
		type_str = "unknown";
		break;
	}
	shell_print(sh, "key_id: %d, type: %s in PEM format:", key_id, type_str);
	shell_print(sh, "%s", pemBuffer);
	return 0;
}

static int cmd_ss_sign(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_print(sh, "Invalid arguments number");
		return -EINVAL;
	}

	const char *sign_str = argv[1];
	int priv_id = atoi(argv[2]);

	if (priv_id < 0 || priv_id >= MAX_KEY_NUM) {
		shell_print(sh, "Invalid private key id: %d", priv_id);
		return -EINVAL;
	}
	const key_info_t *kinfo = &key_info[priv_id];

	if (!kinfo->is_used) {
		shell_print(sh, "Key id %d is not used", priv_id);
		return -EINVAL;
	}
	if (kinfo->type != KEY_TYPE_PRIV) {
		shell_print(sh, "Key id %d is not a private key", priv_id);
		return -EINVAL;
	}

	int ret;
	WC_RNG *rng = NULL;
	ecc_key *privkey = NULL;
	byte signature[512];
	word32 signature_len = sizeof(signature);
	int signature_id = -1;

	/* Initialize RNG */
	ret = initialize_rng(&rng);
	if (ret != 0) {
		shell_print(sh, "Failed to initialize RNG: %d", ret);
		return ret;
	}

	/* Load the private key */
	ret = load_buf_get_ecckey(priv_id, &privkey);
	if (ret != 0) {
		shell_print(sh, "Failed to get private key from buffer: %d", ret);
		ret = -EIO;
		goto cleanup;
	}
	shell_print(sh, "Private key loaded from buffer, id: %d", priv_id);

	/* Compute the SHA-256 hash of the original string first */
	byte hash[32];
	wc_Sha256 sha;

	/* Initialize SHA-256 */
	ret = wc_InitSha256(&sha);
	if (ret != 0) {
		shell_print(sh, "Failed to initialize SHA-256: %d", ret);
		ret = -EIO;
		goto cleanup;
	}

	size_t sign_str_len = strnlen(sign_str, MAX_SIGN_STR_LEN);
	/* Update SHA-256 context with the original string */
	ret = wc_Sha256Update(&sha, (const byte *)sign_str, sign_str_len);
	if (ret != 0) {
		shell_print(sh, "Failed to update SHA-256: %d", ret);
		ret = -EIO;
		goto cleanup;
	}

	/* Compute the SHA-256 hash */
	ret = wc_Sha256Final(&sha, hash);
	if (ret != 0) {
		shell_print(sh, "Failed to finalize SHA-256: %d", ret);
		ret = -EIO;
		goto cleanup;
	}

	/* Sign the hash */
	ret = wc_ecc_sign_hash(hash, sizeof(hash), signature, &signature_len, rng, privkey);
	if (ret != 0) {
		shell_print(sh, "Failed to sign hash: %d", ret);
		char errorBuf[80];

		wc_ErrorString(ret, errorBuf);
		shell_print(sh, "Detailed error: %s", errorBuf);
		ret = -EIO;
		goto cleanup;
	}

	/* Load the signature to buffer */
	signature_id = load_keybuf(KEY_TYPE_SIGSTR, signature, signature_len);
	if (signature_id < 0) {
		shell_print(sh, "Failed to load signature to buffer: %d", signature_id);
		ret = -EIO;
		goto cleanup;
	}
	shell_print(sh, "Signature loaded to buffer, id: %d", signature_id);

cleanup:
	if (privkey) {
		wc_ecc_free(privkey);
		k_free(privkey);
	}
	cleanup_rng(rng);
	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_ss_cmds,
			       SHELL_CMD_ARG(genkey, NULL,
					     "Generate key\n"
					     "Usage: genkey [algorithm: rsa4096|ecc256...etc]",
					     cmd_ss_genkey, 1, 1),
			       SHELL_CMD_ARG(gencsr, NULL,
					     "Generate CSR\n"
					     "Usage: gencsr [subject] [private_key_id]\n",
					     cmd_ss_gencsr, 3, 0),
			       SHELL_CMD_ARG(load, NULL,
					     "Load key in PEM format\n"
					     "Usage: load [key_type] [pem_string]",
					     cmd_ss_load, 3, 0),
			       SHELL_CMD_ARG(dump, NULL,
					     "Dump key in PEM format\n"
					     "Usage: dump [key_id]",
					     cmd_ss_dump, 2, 0),
			       SHELL_CMD_ARG(sign, NULL,
					     "Sign a string with privkey\n"
					     "Usage: sign [string] [privkey_id]",
					     cmd_ss_sign, 3, 0),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(security, &sub_ss_cmds, "Security shell commands", NULL);
