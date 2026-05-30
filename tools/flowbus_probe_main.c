#include "phimantec/phim_config.h"
#include "phimantec/phim_flowbus.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_socket.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0)
{
    (void)fprintf(stderr, "Usage: %s [-c config] [-t timeout_ms] [-v]\n", argv0);
}

static int parse_timeout_ms(const char *text, uint32_t *out)
{
    unsigned long value;
    char *end = NULL;

    if (text == NULL || out == NULL) {
        return PHIM_ERR;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 100ul || value > 10000ul) {
        return PHIM_ERR;
    }
    *out = (uint32_t)value;
    return PHIM_OK;
}

static uint32_t probe_timeout_ms(const phim_config_t *cfg, uint32_t override_ms)
{
    uint32_t timeout_ms;

    timeout_ms = override_ms != 0u ? override_ms : (cfg != NULL ? cfg->flowbus_timeout_ms : 0u);
    if (timeout_ms < 250u) {
        timeout_ms = 250u;
    }
    if (timeout_ms > 10000u) {
        timeout_ms = 10000u;
    }
    return timeout_ms;
}

int main(int argc, char **argv)
{
    const char *config_path = "/etc/phimantec/phimantec-proxyd.conf";
    uint32_t timeout_override_ms = 0u;
    uint32_t timeout_ms;
    phim_config_t cfg;
    phim_flowbus_frame_t frame;
    int fd = -1;
    int i;
    int rc = 1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            if (parse_timeout_ms(argv[++i], &timeout_override_ms) != PHIM_OK) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "-v") == 0) {
            phim_log_set_level(PHIM_LOG_DEBUG);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (phim_config_load(&cfg, config_path) != PHIM_OK) {
        (void)fprintf(stderr, "flowbus-probe: config load failed: %s\n", config_path);
        return 3;
    }
    if (!cfg.flowbus_enabled) {
        (void)fprintf(stderr, "flowbus-probe: flowbus disabled in config: %s\n", config_path);
        return 4;
    }

    timeout_ms = probe_timeout_ms(&cfg, timeout_override_ms);
    fd = phim_tcp_connect(cfg.flowbus_host, cfg.flowbus_port, timeout_ms);
    if (fd < 0) {
        (void)fprintf(stderr, "flowbus-probe: connect failed: %s:%u\n", cfg.flowbus_host, (unsigned int)cfg.flowbus_port);
        return 5;
    }
    (void)phim_set_tcp_nodelay(fd);

    if (phim_flowbus_write_frame(fd, PHIM_FB_HEARTBEAT, 1u, cfg.flowbus_token, (const unsigned char *)"{}", 2u) != PHIM_OK) {
        (void)fprintf(stderr, "flowbus-probe: heartbeat write failed\n");
        goto done;
    }

    phim_flowbus_frame_init(&frame);
    if (phim_flowbus_read_frame(fd, cfg.flowbus_token, timeout_ms, &frame) != PHIM_OK) {
        (void)fprintf(stderr, "flowbus-probe: heartbeat read/HMAC failed\n");
        goto done;
    }
    if (frame.type != PHIM_FB_HEARTBEAT || frame.seq != 1u) {
        (void)fprintf(stderr, "flowbus-probe: unexpected response type=%u seq=%llu\n",
                      (unsigned int)frame.type, (unsigned long long)frame.seq);
        phim_flowbus_frame_free(&frame);
        goto done;
    }

    phim_flowbus_frame_free(&frame);
    rc = 0;

done:
    phim_close_fd(&fd);
    return rc;
}
