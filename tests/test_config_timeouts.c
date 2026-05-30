#include "phimantec/phim_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "check failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); return 1; } } while (0)

static int write_text_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return -1;
    }
    if (fputs(text, f) == EOF) {
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        return -1;
    }
    return 0;
}

int main(void)
{
    char path[] = "/tmp/phimantec-config-timeouts-XXXXXX";
    int fd;
    phim_config_t cfg;
    const char *text =
        "[flowbus]\n"
        "addr = \"127.0.0.1:18090\"\n"
        "pool_size = 1\n"
        "timeout_ms = 1111\n"
        "connect_timeout_ms = 444\n"
        "read_timeout_ms = 999\n"
        "keepalive = false\n"
        "token = \"unit-token\"\n"
        "[verdict]\n"
        "timeout_ms = 2222\n"
        "timeout_under_load_ms = 3333\n"
        "no_alert_grace_ms = 123\n"
        "fail_policy = \"open\"\n"
        "[inspection]\n"
        "queue_max = 77\n"
        "queue_wait_ms = 888\n"
        "max_inflight = 55\n"
        "max_inflight_per_host = 11\n"
        "max_inflight_per_client = 22\n";

    fd = mkstemp(path);
    CHECK_TRUE(fd >= 0);
    close(fd);
    CHECK_TRUE(write_text_file(path, text) == 0);
    CHECK_TRUE(phim_config_load(&cfg, path) == 0);
    unlink(path);

    CHECK_TRUE(cfg.flowbus_timeout_ms == 1111u);
    CHECK_TRUE(cfg.flowbus_connect_timeout_ms == 444u);
    CHECK_TRUE(cfg.flowbus_read_timeout_ms == 999u);
    CHECK_TRUE(cfg.verdict_timeout_ms == 2222u);
    CHECK_TRUE(cfg.verdict_timeout_under_load_ms == 3333u);
    CHECK_TRUE(cfg.no_alert_grace_ms == 123u);
    CHECK_TRUE(cfg.flowbus_keepalive == false);
    CHECK_TRUE(cfg.flowbus_pool_size == 1u);
    CHECK_TRUE(cfg.inspection_queue_max == 77u);
    CHECK_TRUE(cfg.inspection_queue_wait_ms == 888u);
    CHECK_TRUE(cfg.inspection_max_inflight == 55u);
    CHECK_TRUE(cfg.inspection_max_inflight_per_host == 11u);
    CHECK_TRUE(cfg.inspection_max_inflight_per_client == 22u);
    CHECK_TRUE(strcmp(cfg.flowbus_token, "unit-token") == 0);
    CHECK_TRUE(cfg.fail_policy == PHIM_POLICY_FAIL_CLOSED);
    return 0;
}
