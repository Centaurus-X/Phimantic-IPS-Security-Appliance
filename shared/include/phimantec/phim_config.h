#ifndef PHIM_CONFIG_H
#define PHIM_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "phimantec/phim_types.h"

typedef struct phim_config_s {
    char listener_host[PHIM_MAX_HOST + 1u];
    uint16_t listener_port;

    char ca_cert_path[512];
    char ca_key_path[512];
    char cert_cache_dir[512];
    size_t cert_cache_entries;
    bool upstream_verify;
    size_t max_body_bytes;
    char http_mode[32];
    char downstream_alpn[32];
    char upstream_alpn[32];
    bool strip_alt_svc;
    bool strip_upgrade_headers;
    uint32_t preconnect_grace_ms;
    uint32_t first_request_timeout_ms;
    uint32_t idle_tunnel_timeout_ms;
    size_t sample_target_bytes;
    size_t inspection_queue_max;
    uint32_t inspection_queue_wait_ms;
    size_t inspection_max_inflight;
    size_t inspection_max_inflight_per_host;
    size_t inspection_max_inflight_per_client;

    char lane_host[PHIM_MAX_HOST + 1u];
    uint16_t lane_port;
    bool lane_enabled;
    size_t lane_pool_size;

    char flowbus_host[PHIM_MAX_HOST + 1u];
    uint16_t flowbus_port;
    char flowbus_token[PHIM_TOKEN_MAX];
    bool flowbus_enabled;
    size_t flowbus_pool_size;
    bool flowbus_keepalive;
    uint32_t flowbus_timeout_ms;
    uint32_t flowbus_connect_timeout_ms;
    uint32_t flowbus_read_timeout_ms;
    uint32_t verdict_timeout_ms;
    uint32_t verdict_timeout_under_load_ms;
    uint32_t no_alert_grace_ms;
    uint32_t connect_timeout_ms;
    phim_bool_policy_t fail_policy;

    char eve_path[512];
    char lane_log_path[512];
} phim_config_t;

void phim_config_set_defaults(phim_config_t *cfg);
int phim_config_load(phim_config_t *cfg, const char *path);
int phim_parse_hostport(const char *text, char *host, size_t host_len, uint16_t *port);

#endif
