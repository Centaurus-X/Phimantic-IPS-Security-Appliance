#include "phimantec/phim_cert.h"

#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "check failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); return 1; } } while (0)

static EVP_PKEY *make_rsa_key(void)
{
    EVP_PKEY_CTX *ctx;
    EVP_PKEY *key;

    key = NULL;
    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (ctx == NULL) {
        return NULL;
    }
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }
    if (EVP_PKEY_keygen(ctx, &key) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }
    EVP_PKEY_CTX_free(ctx);
    return key;
}

static int add_ext(X509 *cert, X509 *issuer, int nid, const char *value)
{
    X509V3_CTX ctx;
    X509_EXTENSION *ext;

    X509V3_set_ctx(&ctx, issuer, cert, NULL, NULL, 0);
    ext = X509V3_EXT_conf_nid(NULL, &ctx, nid, (char *)value);
    if (ext == NULL) {
        return 0;
    }
    if (X509_add_ext(cert, ext, -1) != 1) {
        X509_EXTENSION_free(ext);
        return 0;
    }
    X509_EXTENSION_free(ext);
    return 1;
}

static X509 *make_ca_cert(EVP_PKEY *key)
{
    X509 *cert;
    X509_NAME *name;

    cert = X509_new();
    if (cert == NULL) {
        return NULL;
    }
    if (X509_set_version(cert, 2L) != 1) {
        X509_free(cert);
        return NULL;
    }
    if (ASN1_INTEGER_set(X509_get_serialNumber(cert), 1L) != 1) {
        X509_free(cert);
        return NULL;
    }
    if (X509_gmtime_adj(X509_get_notBefore(cert), -300L) == NULL) {
        X509_free(cert);
        return NULL;
    }
    if (X509_gmtime_adj(X509_get_notAfter(cert), 86400L) == NULL) {
        X509_free(cert);
        return NULL;
    }
    if (X509_set_pubkey(cert, key) != 1) {
        X509_free(cert);
        return NULL;
    }
    name = X509_get_subject_name(cert);
    if (name == NULL) {
        X509_free(cert);
        return NULL;
    }
    if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)"PHIMANTEC Unit Root CA", -1, -1, 0) != 1) {
        X509_free(cert);
        return NULL;
    }
    if (X509_set_issuer_name(cert, name) != 1) {
        X509_free(cert);
        return NULL;
    }
    if (!add_ext(cert, cert, NID_basic_constraints, "critical,CA:TRUE,pathlen:0")) {
        X509_free(cert);
        return NULL;
    }
    if (!add_ext(cert, cert, NID_key_usage, "critical,keyCertSign,cRLSign")) {
        X509_free(cert);
        return NULL;
    }
    if (!add_ext(cert, cert, NID_subject_key_identifier, "hash")) {
        X509_free(cert);
        return NULL;
    }
    if (!add_ext(cert, cert, NID_authority_key_identifier, "keyid:always,issuer")) {
        X509_free(cert);
        return NULL;
    }
    if (X509_sign(cert, key, EVP_sha256()) <= 0) {
        X509_free(cert);
        return NULL;
    }
    return cert;
}

static int write_pem_pair(const char *cert_path, const char *key_path, X509 *cert, EVP_PKEY *key)
{
    FILE *f;
    int ok;

    f = fopen(cert_path, "w");
    if (f == NULL) {
        return 0;
    }
    ok = PEM_write_X509(f, cert);
    fclose(f);
    if (ok != 1) {
        return 0;
    }

    f = fopen(key_path, "w");
    if (f == NULL) {
        return 0;
    }
    ok = PEM_write_PrivateKey(f, key, NULL, NULL, 0, NULL, NULL);
    fclose(f);
    if (ok != 1) {
        return 0;
    }
    return 1;
}

static int verify_leaf_chain(X509 *ca_cert, X509 *leaf)
{
    X509_STORE *store;
    X509_STORE_CTX *ctx;
    int ok;

    store = X509_STORE_new();
    if (store == NULL) {
        return 0;
    }
    if (X509_STORE_add_cert(store, ca_cert) != 1) {
        X509_STORE_free(store);
        return 0;
    }
    ctx = X509_STORE_CTX_new();
    if (ctx == NULL) {
        X509_STORE_free(store);
        return 0;
    }
    if (X509_STORE_CTX_init(ctx, store, leaf, NULL) != 1) {
        X509_STORE_CTX_free(ctx);
        X509_STORE_free(store);
        return 0;
    }
    ok = X509_verify_cert(ctx);
    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);
    return ok == 1;
}

static int test_dns_leaf(phim_ca_t *ca)
{
    X509 *leaf;
    EVP_PKEY *leaf_key;
    int ok;

    leaf = NULL;
    leaf_key = NULL;
    CHECK_TRUE(phim_generate_leaf_cert(ca, "example.test", &leaf, &leaf_key) == PHIM_OK);
    CHECK_TRUE(leaf != NULL);
    CHECK_TRUE(leaf_key != NULL);
    CHECK_TRUE(X509_check_private_key(leaf, leaf_key) == 1);
    CHECK_TRUE(X509_check_ca(leaf) <= 0);
    CHECK_TRUE(X509_check_host(leaf, "example.test", 0u, 0u, NULL) == 1);
    CHECK_TRUE(verify_leaf_chain(ca->cert, leaf));
    ok = 0;
    X509_free(leaf);
    EVP_PKEY_free(leaf_key);
    return ok;
}

static int test_ip_leaf(phim_ca_t *ca)
{
    X509 *leaf;
    EVP_PKEY *leaf_key;
    int ok;

    leaf = NULL;
    leaf_key = NULL;
    CHECK_TRUE(phim_generate_leaf_cert(ca, "127.0.0.1", &leaf, &leaf_key) == PHIM_OK);
    CHECK_TRUE(leaf != NULL);
    CHECK_TRUE(leaf_key != NULL);
    CHECK_TRUE(X509_check_private_key(leaf, leaf_key) == 1);
    CHECK_TRUE(X509_check_ca(leaf) <= 0);
    CHECK_TRUE(X509_check_ip_asc(leaf, "127.0.0.1", 0u) == 1);
    CHECK_TRUE(verify_leaf_chain(ca->cert, leaf));
    ok = 0;
    X509_free(leaf);
    EVP_PKEY_free(leaf_key);
    return ok;
}

int main(void)
{
    char cert_path[256];
    char key_path[256];
    EVP_PKEY *ca_key;
    X509 *ca_cert;
    phim_ca_t ca;
    int rc;

    (void)snprintf(cert_path, sizeof(cert_path), "/tmp/phim-cert-test-%ld.crt", (long)getpid());
    (void)snprintf(key_path, sizeof(key_path), "/tmp/phim-cert-test-%ld.key", (long)getpid());

    ca_key = make_rsa_key();
    CHECK_TRUE(ca_key != NULL);
    ca_cert = make_ca_cert(ca_key);
    CHECK_TRUE(ca_cert != NULL);
    CHECK_TRUE(write_pem_pair(cert_path, key_path, ca_cert, ca_key));

    phim_ca_init(&ca);
    CHECK_TRUE(phim_ca_load(&ca, cert_path, key_path) == PHIM_OK);
    CHECK_TRUE(test_dns_leaf(&ca) == 0);
    CHECK_TRUE(test_ip_leaf(&ca) == 0);

    phim_ca_free(&ca);
    X509_free(ca_cert);
    EVP_PKEY_free(ca_key);
    rc = 0;
    (void)unlink(cert_path);
    (void)unlink(key_path);
    return rc;
}
