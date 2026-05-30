#include "phimantec/phim_cert.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_string.h"

#include <arpa/inet.h>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PHIM_LEAF_SERIAL_BYTES 16u
#define PHIM_LEAF_VALIDITY_SECONDS (90L * 24L * 60L * 60L)
#define PHIM_LEAF_NOT_BEFORE_SKEW_SECONDS (-300L)

void phim_ca_init(phim_ca_t *ca)
{
    if (ca != NULL) {
        ca->cert = NULL;
        ca->key = NULL;
    }
}

void phim_ca_free(phim_ca_t *ca)
{
    if (ca != NULL) {
        X509_free(ca->cert);
        EVP_PKEY_free(ca->key);
        ca->cert = NULL;
        ca->key = NULL;
    }
}

static int phim_ca_is_usable(const phim_ca_t *ca)
{
    int basic_constraints;
    uint32_t key_usage;

    if (ca == NULL || ca->cert == NULL || ca->key == NULL) {
        return PHIM_ERR;
    }
    basic_constraints = X509_check_ca(ca->cert);
    if (basic_constraints <= 0) {
        PHIM_LOGE("loaded certificate is not a CA certificate");
        return PHIM_ERR;
    }
    key_usage = X509_get_key_usage(ca->cert);
    if (key_usage != UINT32_MAX) {
        if ((key_usage & KU_KEY_CERT_SIGN) == 0U) {
            PHIM_LOGE("loaded CA certificate is missing keyCertSign usage");
            return PHIM_ERR;
        }
    }
    return PHIM_OK;
}

int phim_ca_load(phim_ca_t *ca, const char *cert_path, const char *key_path)
{
    FILE *f;
    if (ca == NULL || cert_path == NULL || key_path == NULL) {
        return PHIM_ERR;
    }
    phim_ca_init(ca);
    f = fopen(cert_path, "r");
    if (f == NULL) {
        PHIM_LOGE("cannot open CA certificate: %s", cert_path);
        return PHIM_ERR;
    }
    ca->cert = PEM_read_X509(f, NULL, NULL, NULL);
    fclose(f);
    if (ca->cert == NULL) {
        PHIM_LOGE("cannot read CA certificate: %s", cert_path);
        phim_ca_free(ca);
        return PHIM_ERR;
    }
    f = fopen(key_path, "r");
    if (f == NULL) {
        PHIM_LOGE("cannot open CA key: %s", key_path);
        phim_ca_free(ca);
        return PHIM_ERR;
    }
    ca->key = PEM_read_PrivateKey(f, NULL, NULL, NULL);
    fclose(f);
    if (ca->key == NULL) {
        PHIM_LOGE("cannot read CA key: %s", key_path);
        phim_ca_free(ca);
        return PHIM_ERR;
    }
    if (X509_check_private_key(ca->cert, ca->key) != 1) {
        PHIM_LOGE("CA private key does not match CA certificate");
        phim_ca_free(ca);
        return PHIM_ERR;
    }
    if (phim_ca_is_usable(ca) != PHIM_OK) {
        phim_ca_free(ca);
        return PHIM_ERR;
    }
    return PHIM_OK;
}

static int phim_generate_leaf_key(EVP_PKEY **out_key)
{
    EVP_PKEY_CTX *ctx;
    EVP_PKEY *key = NULL;
    int rc = PHIM_ERR;

    if (out_key == NULL) {
        return PHIM_ERR;
    }
    *out_key = NULL;

    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (ctx == NULL) {
        return PHIM_ERR;
    }
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        goto done;
    }
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0) {
        goto done;
    }
    if (EVP_PKEY_keygen(ctx, &key) <= 0) {
        goto done;
    }
    *out_key = key;
    key = NULL;
    rc = PHIM_OK;

done:
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(ctx);
    return rc;
}

static int phim_add_ext(X509 *cert, X509 *issuer, int nid, const char *value)
{
    X509V3_CTX ctx;
    X509_EXTENSION *ext;
    X509V3_set_ctx(&ctx, issuer, cert, NULL, NULL, 0);
    ext = X509V3_EXT_conf_nid(NULL, &ctx, nid, (char *)value);
    if (ext == NULL) {
        return PHIM_ERR;
    }
    if (X509_add_ext(cert, ext, -1) != 1) {
        X509_EXTENSION_free(ext);
        return PHIM_ERR;
    }
    X509_EXTENSION_free(ext);
    return PHIM_OK;
}

static int phim_set_random_serial(X509 *cert)
{
    unsigned char serial_raw[PHIM_LEAF_SERIAL_BYTES];
    BIGNUM *bn = NULL;
    ASN1_INTEGER *asn1 = NULL;
    int rc = PHIM_ERR;

    if (cert == NULL) {
        return PHIM_ERR;
    }
    if (RAND_bytes(serial_raw, sizeof(serial_raw)) != 1) {
        return PHIM_ERR;
    }
    serial_raw[0] &= 0x7fu;
    serial_raw[0] |= 0x01u;

    bn = BN_bin2bn(serial_raw, (int)sizeof(serial_raw), NULL);
    if (bn == NULL) {
        goto done;
    }
    asn1 = BN_to_ASN1_INTEGER(bn, NULL);
    if (asn1 == NULL) {
        goto done;
    }
    if (X509_set_serialNumber(cert, asn1) != 1) {
        goto done;
    }
    rc = PHIM_OK;

done:
    ASN1_INTEGER_free(asn1);
    BN_free(bn);
    return rc;
}

static int phim_is_ip_literal(const char *hostname)
{
    unsigned char buf[sizeof(struct in6_addr)];
    return inet_pton(AF_INET, hostname, buf) == 1 || inet_pton(AF_INET6, hostname, buf) == 1;
}

int phim_generate_leaf_cert(const phim_ca_t *ca, const char *hostname,
                            X509 **out_cert, EVP_PKEY **out_key)
{
    X509 *cert = NULL;
    EVP_PKEY *key = NULL;
    X509_NAME *name;
    char san[PHIM_MAX_HOST + 16u];
    int rc = PHIM_ERR;

    if (ca == NULL || ca->cert == NULL || ca->key == NULL || hostname == NULL || out_cert == NULL || out_key == NULL) {
        return PHIM_ERR;
    }

    if (phim_generate_leaf_key(&key) != PHIM_OK) {
        return PHIM_ERR;
    }

    cert = X509_new();
    if (cert == NULL) {
        goto done;
    }
    if (X509_set_version(cert, 2L) != 1) {
        goto done;
    }
    if (phim_set_random_serial(cert) != PHIM_OK) {
        goto done;
    }
    if (X509_gmtime_adj(X509_get_notBefore(cert), PHIM_LEAF_NOT_BEFORE_SKEW_SECONDS) == NULL) {
        goto done;
    }
    if (X509_gmtime_adj(X509_get_notAfter(cert), PHIM_LEAF_VALIDITY_SECONDS) == NULL) {
        goto done;
    }
    if (X509_set_pubkey(cert, key) != 1) {
        goto done;
    }
    if (X509_set_issuer_name(cert, X509_get_subject_name(ca->cert)) != 1) {
        goto done;
    }
    name = X509_get_subject_name(cert);
    if (name == NULL) {
        goto done;
    }
    if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)hostname, -1, -1, 0) != 1) {
        goto done;
    }
    if (phim_add_ext(cert, ca->cert, NID_basic_constraints, "critical,CA:FALSE") != PHIM_OK) {
        goto done;
    }
    if (phim_add_ext(cert, ca->cert, NID_key_usage, "critical,digitalSignature") != PHIM_OK) {
        goto done;
    }
    if (phim_add_ext(cert, ca->cert, NID_ext_key_usage, "serverAuth") != PHIM_OK) {
        goto done;
    }
    if (phim_add_ext(cert, ca->cert, NID_subject_key_identifier, "hash") != PHIM_OK) {
        goto done;
    }
    if (phim_add_ext(cert, ca->cert, NID_authority_key_identifier, "keyid,issuer") != PHIM_OK) {
        goto done;
    }
    (void)snprintf(san, sizeof(san), "%s:%s", phim_is_ip_literal(hostname) != 0 ? "IP" : "DNS", hostname);
    if (phim_add_ext(cert, ca->cert, NID_subject_alt_name, san) != PHIM_OK) {
        goto done;
    }
    if (X509_sign(cert, ca->key, EVP_sha256()) <= 0) {
        goto done;
    }
    *out_cert = cert;
    *out_key = key;
    cert = NULL;
    key = NULL;
    rc = PHIM_OK;

done:
    X509_free(cert);
    EVP_PKEY_free(key);
    return rc;
}
