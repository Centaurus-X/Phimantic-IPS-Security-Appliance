#include "phimantec/phim_config.h"
#include "phimantec/phim_h1.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_mem.h"
#include "phimantec/phim_socket.h"
#include "phimantec/phim_string.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PHIM_LANE_MAX_BODY (1024u * 1024u)

typedef struct lane_arg_s {
    int fd;
    phim_config_t cfg;
} lane_arg_t;

static void usage(const char *argv0)
{
    (void)fprintf(stderr, "Usage: %s [-c config/phimantec-suricata-node.conf] [-v]\n", argv0);
}

static void lane_log_event(const phim_config_t *cfg, const unsigned char *header, size_t header_len,
                           const unsigned char *body, size_t body_len)
{
    char flow_id[128];
    char direction[32];
    char path[2048];
    char escaped[4096];
    FILE *f;
    if (phim_h1_get_header_value(header, header_len, "X-Phim-Flow-Id", flow_id, sizeof(flow_id)) != PHIM_OK) {
        (void)phim_copy_cstr(flow_id, sizeof(flow_id), "unknown");
    }
    if (phim_h1_get_header_value(header, header_len, "X-Phim-Direction", direction, sizeof(direction)) != PHIM_OK) {
        (void)phim_copy_cstr(direction, sizeof(direction), "unknown");
    }
    if (phim_h1_get_header_value(header, header_len, "X-Phim-Path", path, sizeof(path)) != PHIM_OK) {
        (void)phim_copy_cstr(path, sizeof(path), "/");
    }
    if (phim_json_escape(body, body_len > 256u ? 256u : body_len, escaped, sizeof(escaped)) != 0) {
        escaped[0] = '\0';
    }
    f = fopen(cfg->lane_log_path, "a");
    if (f != NULL) {
        (void)fprintf(f, "{\"flow_id\":\"%s\",\"direction\":\"%s\",\"path\":\"%s\",\"sample\":\"%s\"}\n",
                      flow_id, direction, path, escaped);
        fclose(f);
    }
    PHIM_LOGI("lane flow=%s direction=%s path=%s body=%zu", flow_id, direction, path, body_len);
}

static int lane_header_wants_close(const unsigned char *header)
{
    if (header == NULL) {
        return 1;
    }
    return phim_strcasestr_ascii((const char *)header, "Connection: close") != NULL ? 1 : 0;
}

static void *lane_client_thread(void *arg)
{
    lane_arg_t *la = (lane_arg_t *)arg;
    int fd = la->fd;
    phim_config_t cfg = la->cfg;
    unsigned char *buf = phim_xcalloc(1u, PHIM_MAX_HEADER_BLOCK + PHIM_LANE_MAX_BODY + 1u);
    free(la);

    for (;;) {
        ssize_t n;
        size_t header_len = 0u;
        char content_length_text[64];
        size_t content_length = 0u;
        size_t used;
        int wants_close;
        const char *resp_keepalive = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n";
        const char *resp_close = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

        memset(buf, 0, PHIM_MAX_HEADER_BLOCK + PHIM_LANE_MAX_BODY + 1u);
        n = phim_read_until(fd, buf, PHIM_MAX_HEADER_BLOCK, "\r\n\r\n", 10000u);
        if (n <= 0) {
            break;
        }
        used = (size_t)n;
        if (phim_h1_find_header_end(buf, used, &header_len) != PHIM_OK) {
            break;
        }
        wants_close = lane_header_wants_close(buf);
        if (phim_h1_get_header_value(buf, header_len, "Content-Length", content_length_text, sizeof(content_length_text)) == PHIM_OK) {
            content_length = (size_t)strtoull(content_length_text, NULL, 10);
        }
        if (content_length > PHIM_LANE_MAX_BODY) {
            break;
        }
        while (used < header_len + content_length) {
            ssize_t rn = phim_read_some_timeout(fd, buf + used, header_len + content_length - used, 1000u);
            if (rn <= 0) {
                goto done;
            }
            used += (size_t)rn;
        }
        lane_log_event(&cfg, buf, header_len, buf + header_len, content_length);
        if (phim_write_all(fd, wants_close != 0 ? resp_close : resp_keepalive,
                           strlen(wants_close != 0 ? resp_close : resp_keepalive)) != 0) {
            break;
        }
        if (wants_close != 0) {
            break;
        }
    }

done:
    free(buf);
    phim_close_fd(&fd);
    return NULL;
}

int main(int argc, char **argv)
{
    const char *config_path = "config/phimantec-suricata-node.conf";
    phim_config_t cfg;
    int fd;
    int i;
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            phim_log_set_level(PHIM_LOG_DEBUG);
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (phim_config_load(&cfg, config_path) != PHIM_OK) {
        return 1;
    }
    fd = phim_tcp_listen(cfg.lane_host, cfg.lane_port, 1024);
    if (fd < 0) {
        PHIM_LOGE("lane-sink listen failed on %s:%u", cfg.lane_host, (unsigned int)cfg.lane_port);
        return 1;
    }
    PHIM_LOGI("phimantec-lane-sink listening on %s:%u", cfg.lane_host, (unsigned int)cfg.lane_port);
    for (;;) {
        int cfd = accept(fd, NULL, NULL);
        pthread_t tid;
        lane_arg_t *la;
        if (cfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        la = phim_xcalloc(1u, sizeof(*la));
        la->fd = cfd;
        la->cfg = cfg;
        if (pthread_create(&tid, NULL, lane_client_thread, la) == 0) {
            (void)pthread_detach(tid);
        } else {
            phim_close_fd(&cfd);
            free(la);
        }
    }
    phim_close_fd(&fd);
    return 1;
}
