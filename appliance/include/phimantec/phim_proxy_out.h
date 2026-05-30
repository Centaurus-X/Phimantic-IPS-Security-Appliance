#ifndef PHIM_PROXY_OUT_H
#define PHIM_PROXY_OUT_H

#include <openssl/ssl.h>
#include "phimantec/phim_config.h"

int phim_proxy_out_connect_tls(const phim_config_t *cfg, const char *host, uint16_t port,
                               int *out_fd, SSL_CTX **out_ctx, SSL **out_ssl);

#endif
