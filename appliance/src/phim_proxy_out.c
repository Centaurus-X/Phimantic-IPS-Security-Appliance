#include "phimantec/phim_proxy_out.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_socket.h"
#include "phimantec/phim_tls.h"

int phim_proxy_out_connect_tls(const phim_config_t *cfg, const char *host, uint16_t port,
                               int *out_fd, SSL_CTX **out_ctx, SSL **out_ssl)
{
    int fd;
    SSL_CTX *ctx;
    SSL *ssl;
    if (cfg == NULL || host == NULL || out_fd == NULL || out_ctx == NULL || out_ssl == NULL) {
        return PHIM_ERR;
    }
    *out_fd = -1;
    *out_ctx = NULL;
    *out_ssl = NULL;
    fd = phim_tcp_connect(host, port, cfg->connect_timeout_ms);
    if (fd < 0) {
        PHIM_LOGW("upstream TCP connect failed %s:%u", host, (unsigned int)port);
        return PHIM_ERR;
    }
    ctx = phim_tls_create_upstream_ctx(cfg->upstream_verify, host);
    if (ctx == NULL) {
        phim_close_fd(&fd);
        return PHIM_ERR;
    }
    ssl = phim_tls_connect_upstream(fd, ctx, host);
    if (ssl == NULL) {
        SSL_CTX_free(ctx);
        phim_close_fd(&fd);
        return PHIM_ERR;
    }
    *out_fd = fd;
    *out_ctx = ctx;
    *out_ssl = ssl;
    return PHIM_OK;
}
