#ifndef PHIM_TLS_H
#define PHIM_TLS_H

#include <stdbool.h>
#include <sys/types.h>
#include <openssl/ssl.h>
#include "phimantec/phim_cert.h"

int phim_tls_global_init(void);
SSL_CTX *phim_tls_create_downstream_ctx(const phim_ca_t *ca, const char *hostname);
SSL_CTX *phim_tls_create_upstream_ctx(bool verify_peer, const char *hostname);
SSL *phim_tls_accept_client(int fd, SSL_CTX *ctx);
SSL *phim_tls_connect_upstream(int fd, SSL_CTX *ctx, const char *sni);
int phim_ssl_write_all(SSL *ssl, const void *buf, size_t len);
ssize_t phim_ssl_read_some(SSL *ssl, void *buf, size_t len);
int phim_ssl_read_until(SSL *ssl, unsigned char *buf, size_t cap, const char *needle, size_t *out_len);
void phim_ssl_close(SSL **ssl);
void phim_ssl_ctx_free(SSL_CTX **ctx);

#endif
