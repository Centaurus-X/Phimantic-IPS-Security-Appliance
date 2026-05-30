#ifndef PHIM_CERT_H
#define PHIM_CERT_H

#include <openssl/x509.h>
#include <openssl/evp.h>
#include "phimantec/phim_config.h"

typedef struct phim_ca_s {
    X509 *cert;
    EVP_PKEY *key;
} phim_ca_t;

void phim_ca_init(phim_ca_t *ca);
void phim_ca_free(phim_ca_t *ca);
int phim_ca_load(phim_ca_t *ca, const char *cert_path, const char *key_path);
int phim_generate_leaf_cert(const phim_ca_t *ca, const char *hostname,
                            X509 **out_cert, EVP_PKEY **out_key);

#endif
