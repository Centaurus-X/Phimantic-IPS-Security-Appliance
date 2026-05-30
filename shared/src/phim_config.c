#include "phimantec/phim_config.h"
#include "phimantec/phim_string.h"
#include "phimantec/phim_log.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void phim_config_set_defaults(phim_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    (void)phim_copy_cstr(cfg->listener_host, sizeof(cfg->listener_host), "0.0.0.0");
    cfg->listener_port = PHIM_DEFAULT_LISTENER_PORT;
    (void)phim_copy_cstr(cfg->ca_cert_path, sizeof(cfg->ca_cert_path), "config/ca/phimantec-bump-ca.crt");
    (void)phim_copy_cstr(cfg->ca_key_path, sizeof(cfg->ca_key_path), "config/ca/phimantec-bump-ca.key");
    (void)phim_copy_cstr(cfg->cert_cache_dir, sizeof(cfg->cert_cache_dir), "/var/cache/phimantec/certs");
    cfg->cert_cache_entries = 1024u;
    cfg->upstream_verify = false;
    cfg->max_body_bytes = 1024u * 1024u;
    (void)phim_copy_cstr(cfg->http_mode, sizeof(cfg->http_mode), "h1_only");
    (void)phim_copy_cstr(cfg->downstream_alpn, sizeof(cfg->downstream_alpn), "http/1.1");
    (void)phim_copy_cstr(cfg->upstream_alpn, sizeof(cfg->upstream_alpn), "http/1.1");
    cfg->strip_alt_svc = true;
    cfg->strip_upgrade_headers = true;
    cfg->preconnect_grace_ms = 250u;
    cfg->first_request_timeout_ms = 1500u;
    cfg->idle_tunnel_timeout_ms = 30000u;
    cfg->sample_target_bytes = 524288u;
    cfg->inspection_queue_max = 1024u;
    cfg->inspection_queue_wait_ms = 3000u;
    cfg->inspection_max_inflight = 128u;
    cfg->inspection_max_inflight_per_host = 32u;
    cfg->inspection_max_inflight_per_client = 96u;
    (void)phim_copy_cstr(cfg->lane_host, sizeof(cfg->lane_host), "127.0.0.1");
    cfg->lane_port = PHIM_DEFAULT_LANE_PORT;
    cfg->lane_enabled = true;
    cfg->lane_pool_size = 16u;
    (void)phim_copy_cstr(cfg->flowbus_host, sizeof(cfg->flowbus_host), "127.0.0.1");
    cfg->flowbus_port = PHIM_DEFAULT_FLOWBUS_PORT;
    (void)phim_copy_cstr(cfg->flowbus_token, sizeof(cfg->flowbus_token), "change-me-in-production");
    cfg->flowbus_enabled = true;
    cfg->flowbus_pool_size = 16u;
    cfg->flowbus_keepalive = false;
    cfg->flowbus_timeout_ms = 3000u;
    cfg->flowbus_connect_timeout_ms = 500u;
    cfg->flowbus_read_timeout_ms = 3000u;
    cfg->verdict_timeout_ms = 3000u;
    cfg->verdict_timeout_under_load_ms = 8000u;
    cfg->no_alert_grace_ms = 100u;
    cfg->connect_timeout_ms = 1000u;
    cfg->fail_policy = PHIM_POLICY_FAIL_CLOSED;
    (void)phim_copy_cstr(cfg->eve_path, sizeof(cfg->eve_path), "/var/log/suricata/eve.json");
    (void)phim_copy_cstr(cfg->lane_log_path, sizeof(cfg->lane_log_path), "/var/log/phimantec/lane.jsonl");
}

static void phim_unquote(char *s)
{
    size_t len;
    phim_trim_ascii(s);
    len = strlen(s);
    if (len >= 2u && ((s[0] == '"' && s[len - 1u] == '"') || (s[0] == '\'' && s[len - 1u] == '\''))) {
        memmove(s, s + 1u, len - 2u);
        s[len - 2u] = '\0';
    }
}

static bool phim_parse_bool(const char *value, bool default_value)
{
    if (phim_strcasecmp_ascii(value, "true") == 0 || phim_strcasecmp_ascii(value, "yes") == 0 || strcmp(value, "1") == 0 || phim_strcasecmp_ascii(value, "on") == 0) {
        return true;
    }
    if (phim_strcasecmp_ascii(value, "false") == 0 || phim_strcasecmp_ascii(value, "no") == 0 || strcmp(value, "0") == 0 || phim_strcasecmp_ascii(value, "off") == 0) {
        return false;
    }
    return default_value;
}

int phim_parse_hostport(const char *text, char *host, size_t host_len, uint16_t *port)
{
    const char *colon;
    char port_text[16];
    unsigned long parsed;
    char *end = NULL;

    if (text == NULL || host == NULL || port == NULL || host_len == 0u) {
        return -1;
    }

    if (text[0] == '[') {
        const char *end_bracket = strchr(text, ']');
        if (end_bracket == NULL || end_bracket[1] != ':') {
            return -1;
        }
        if ((size_t)(end_bracket - text - 1) >= host_len) {
            return -1;
        }
        memcpy(host, text + 1, (size_t)(end_bracket - text - 1));
        host[end_bracket - text - 1] = '\0';
        colon = end_bracket + 1;
    } else {
        colon = strrchr(text, ':');
        if (colon == NULL || colon == text) {
            return -1;
        }
        if ((size_t)(colon - text) >= host_len) {
            return -1;
        }
        memcpy(host, text, (size_t)(colon - text));
        host[colon - text] = '\0';
    }

    if (strlen(colon + 1) >= sizeof(port_text)) {
        return -1;
    }
    (void)phim_copy_cstr(port_text, sizeof(port_text), colon + 1);
    errno = 0;
    parsed = strtoul(port_text, &end, 10);
    if (errno != 0 || end == port_text || *end != '\0' || parsed == 0ul || parsed > 65535ul) {
        return -1;
    }
    *port = (uint16_t)parsed;
    return 0;
}

static int phim_set_key(phim_config_t *cfg, const char *section, const char *key, const char *value)
{
    char full_key[512];
    char host[PHIM_MAX_HOST + 1u];
    uint16_t port = 0u;

    if (section[0] != '\0') {
        (void)snprintf(full_key, sizeof(full_key), "%s.%s", section, key);
    } else {
        (void)snprintf(full_key, sizeof(full_key), "%s", key);
    }

    if (phim_strcasecmp_ascii(full_key, "listener.bind") == 0) {
        if (phim_parse_hostport(value, host, sizeof(host), &port) != 0) {
            return -1;
        }
        (void)phim_copy_cstr(cfg->listener_host, sizeof(cfg->listener_host), host);
        cfg->listener_port = port;
    } else if (phim_strcasecmp_ascii(full_key, "tls.ca_cert") == 0) {
        (void)phim_copy_cstr(cfg->ca_cert_path, sizeof(cfg->ca_cert_path), value);
    } else if (phim_strcasecmp_ascii(full_key, "tls.ca_key") == 0) {
        (void)phim_copy_cstr(cfg->ca_key_path, sizeof(cfg->ca_key_path), value);
    } else if (phim_strcasecmp_ascii(full_key, "tls.cert_cache_dir") == 0) {
        (void)phim_copy_cstr(cfg->cert_cache_dir, sizeof(cfg->cert_cache_dir), value);
    } else if (phim_strcasecmp_ascii(full_key, "tls.cert_cache_entries") == 0) {
        cfg->cert_cache_entries = (size_t)strtoull(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "tls.upstream_verify") == 0) {
        cfg->upstream_verify = phim_parse_bool(value, cfg->upstream_verify);
    } else if (phim_strcasecmp_ascii(full_key, "tls.max_body_bytes") == 0 ||
               phim_strcasecmp_ascii(full_key, "inspection.sample_max_bytes") == 0 ||
               phim_strcasecmp_ascii(full_key, "inspection.max_sample_bytes") == 0) {
        cfg->max_body_bytes = (size_t)strtoull(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "http.mode") == 0) {
        (void)phim_copy_cstr(cfg->http_mode, sizeof(cfg->http_mode), value);
    } else if (phim_strcasecmp_ascii(full_key, "http.downstream_alpn") == 0) {
        (void)phim_copy_cstr(cfg->downstream_alpn, sizeof(cfg->downstream_alpn), value);
    } else if (phim_strcasecmp_ascii(full_key, "http.upstream_alpn") == 0) {
        (void)phim_copy_cstr(cfg->upstream_alpn, sizeof(cfg->upstream_alpn), value);
    } else if (phim_strcasecmp_ascii(full_key, "http.strip_alt_svc") == 0) {
        cfg->strip_alt_svc = phim_parse_bool(value, cfg->strip_alt_svc);
    } else if (phim_strcasecmp_ascii(full_key, "http.strip_upgrade_headers") == 0) {
        cfg->strip_upgrade_headers = phim_parse_bool(value, cfg->strip_upgrade_headers);
    } else if (phim_strcasecmp_ascii(full_key, "http.preconnect_grace_ms") == 0) {
        cfg->preconnect_grace_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "http.first_request_timeout_ms") == 0) {
        cfg->first_request_timeout_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "http.idle_tunnel_timeout_ms") == 0) {
        cfg->idle_tunnel_timeout_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "inspection.sample_target_bytes") == 0 ||
               phim_strcasecmp_ascii(full_key, "inspection.stream_chunk_min_bytes") == 0) {
        cfg->sample_target_bytes = (size_t)strtoull(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "inspection.queue_max") == 0 ||
               phim_strcasecmp_ascii(full_key, "inspection.inspection_queue_max") == 0) {
        cfg->inspection_queue_max = (size_t)strtoull(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "inspection.queue_wait_ms") == 0 ||
               phim_strcasecmp_ascii(full_key, "inspection.inspection_queue_wait_ms") == 0) {
        cfg->inspection_queue_wait_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "inspection.max_inflight") == 0 ||
               phim_strcasecmp_ascii(full_key, "inspection.inspection_max_inflight") == 0) {
        cfg->inspection_max_inflight = (size_t)strtoull(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "inspection.max_inflight_per_host") == 0 ||
               phim_strcasecmp_ascii(full_key, "inspection.inspection_max_inflight_per_host") == 0) {
        cfg->inspection_max_inflight_per_host = (size_t)strtoull(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "inspection.max_inflight_per_client") == 0 ||
               phim_strcasecmp_ascii(full_key, "inspection.inspection_max_inflight_per_client") == 0) {
        cfg->inspection_max_inflight_per_client = (size_t)strtoull(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "lane.addr") == 0) {
        if (phim_parse_hostport(value, host, sizeof(host), &port) != 0) {
            return -1;
        }
        (void)phim_copy_cstr(cfg->lane_host, sizeof(cfg->lane_host), host);
        cfg->lane_port = port;
    } else if (phim_strcasecmp_ascii(full_key, "lane.enabled") == 0) {
        cfg->lane_enabled = phim_parse_bool(value, cfg->lane_enabled);
    } else if (phim_strcasecmp_ascii(full_key, "lane.pool_size") == 0) {
        cfg->lane_pool_size = (size_t)strtoull(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "flowbus.addr") == 0) {
        if (phim_parse_hostport(value, host, sizeof(host), &port) != 0) {
            return -1;
        }
        (void)phim_copy_cstr(cfg->flowbus_host, sizeof(cfg->flowbus_host), host);
        cfg->flowbus_port = port;
    } else if (phim_strcasecmp_ascii(full_key, "flowbus.token") == 0) {
        (void)phim_copy_cstr(cfg->flowbus_token, sizeof(cfg->flowbus_token), value);
    } else if (phim_strcasecmp_ascii(full_key, "flowbus.enabled") == 0) {
        cfg->flowbus_enabled = phim_parse_bool(value, cfg->flowbus_enabled);
    } else if (phim_strcasecmp_ascii(full_key, "flowbus.pool_size") == 0) {
        cfg->flowbus_pool_size = (size_t)strtoull(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "flowbus.keepalive") == 0) {
        cfg->flowbus_keepalive = phim_parse_bool(value, cfg->flowbus_keepalive);
    } else if (phim_strcasecmp_ascii(full_key, "flowbus.timeout_ms") == 0) {
        cfg->flowbus_timeout_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "flowbus.connect_timeout_ms") == 0) {
        cfg->flowbus_connect_timeout_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "flowbus.read_timeout_ms") == 0) {
        cfg->flowbus_read_timeout_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "verdict.timeout_ms") == 0) {
        cfg->verdict_timeout_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "verdict.timeout_under_load_ms") == 0) {
        cfg->verdict_timeout_under_load_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "verdict.no_alert_grace_ms") == 0) {
        cfg->no_alert_grace_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "network.connect_timeout_ms") == 0) {
        cfg->connect_timeout_ms = (uint32_t)strtoul(value, NULL, 10);
    } else if (phim_strcasecmp_ascii(full_key, "verdict.fail_policy") == 0) {
        if (phim_strcasecmp_ascii(value, "closed") == 0 || phim_strcasecmp_ascii(value, "fail-closed") == 0 || phim_strcasecmp_ascii(value, "security") == 0) {
            cfg->fail_policy = PHIM_POLICY_FAIL_CLOSED;
        } else {
            PHIM_LOGW("unsupported verdict.fail_policy=%s; forcing fail-closed", value);
            cfg->fail_policy = PHIM_POLICY_FAIL_CLOSED;
        }
    } else if (phim_strcasecmp_ascii(full_key, "suricata.eve_path") == 0) {
        (void)phim_copy_cstr(cfg->eve_path, sizeof(cfg->eve_path), value);
    } else if (phim_strcasecmp_ascii(full_key, "lane.log_path") == 0) {
        (void)phim_copy_cstr(cfg->lane_log_path, sizeof(cfg->lane_log_path), value);
    } else {
        PHIM_LOGW("unknown config key ignored: %s", full_key);
    }
    return 0;
}

int phim_config_load(phim_config_t *cfg, const char *path)
{
    FILE *f;
    char line[1024];
    char section[64];
    unsigned int line_no = 0u;

    if (cfg == NULL || path == NULL) {
        return -1;
    }
    phim_config_set_defaults(cfg);
    f = fopen(path, "r");
    if (f == NULL) {
        PHIM_LOGW("config %s not found, using defaults", path);
        return 0;
    }
    section[0] = '\0';
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq;
        char *comment;
        char key[256];
        char value[768];
        line_no++;
        comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }
        phim_trim_ascii(line);
        if (line[0] == '\0') {
            continue;
        }
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end == NULL) {
                PHIM_LOGE("bad section in %s:%u", path, line_no);
                fclose(f);
                return -1;
            }
            *end = '\0';
            (void)phim_copy_cstr(section, sizeof(section), line + 1);
            phim_trim_ascii(section);
            continue;
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            PHIM_LOGE("bad key/value in %s:%u", path, line_no);
            fclose(f);
            return -1;
        }
        *eq = '\0';
        (void)phim_copy_cstr(key, sizeof(key), line);
        (void)phim_copy_cstr(value, sizeof(value), eq + 1);
        phim_trim_ascii(key);
        phim_unquote(value);
        if (phim_set_key(cfg, section, key, value) != 0) {
            PHIM_LOGE("invalid config value in %s:%u key %s", path, line_no, key);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    if (cfg->max_body_bytes < 4096u) {
        cfg->max_body_bytes = 4096u;
    }
    if (cfg->sample_target_bytes < 4096u) {
        cfg->sample_target_bytes = 4096u;
    }
    if (cfg->sample_target_bytes > cfg->max_body_bytes) {
        cfg->sample_target_bytes = cfg->max_body_bytes;
    }
    if (cfg->inspection_queue_wait_ms > 30000u) {
        cfg->inspection_queue_wait_ms = 30000u;
    }
    if (cfg->inspection_max_inflight == 0u) {
        cfg->inspection_max_inflight = 1u;
    }
    if (cfg->inspection_max_inflight > 4096u) {
        cfg->inspection_max_inflight = 4096u;
    }
    if (cfg->inspection_max_inflight_per_host > cfg->inspection_max_inflight) {
        cfg->inspection_max_inflight_per_host = cfg->inspection_max_inflight;
    }
    if (cfg->inspection_max_inflight_per_client > cfg->inspection_max_inflight) {
        cfg->inspection_max_inflight_per_client = cfg->inspection_max_inflight;
    }
    if (cfg->inspection_queue_max > 65536u) {
        cfg->inspection_queue_max = 65536u;
    }
    if (cfg->lane_pool_size == 0u) {
        cfg->lane_pool_size = 1u;
    }
    if (cfg->lane_pool_size > 32u) {
        PHIM_LOGW("lane.pool_size capped at 32");
        cfg->lane_pool_size = 32u;
    }
    if (cfg->flowbus_pool_size == 0u) {
        cfg->flowbus_pool_size = 1u;
    }
    if (cfg->flowbus_pool_size > 32u) {
        PHIM_LOGW("flowbus.pool_size capped at 32");
        cfg->flowbus_pool_size = 32u;
    }
    if (cfg->flowbus_timeout_ms < 250u) {
        cfg->flowbus_timeout_ms = 250u;
    }
    if (cfg->flowbus_timeout_ms > 10000u) {
        cfg->flowbus_timeout_ms = 10000u;
    }
    if (cfg->flowbus_connect_timeout_ms == 0u) {
        cfg->flowbus_connect_timeout_ms = cfg->flowbus_timeout_ms;
    }
    if (cfg->flowbus_connect_timeout_ms < 100u) {
        cfg->flowbus_connect_timeout_ms = 100u;
    }
    if (cfg->flowbus_connect_timeout_ms > cfg->flowbus_timeout_ms) {
        cfg->flowbus_connect_timeout_ms = cfg->flowbus_timeout_ms;
    }
    if (cfg->flowbus_read_timeout_ms == 0u) {
        cfg->flowbus_read_timeout_ms = cfg->flowbus_timeout_ms;
    }
    if (cfg->flowbus_read_timeout_ms < 250u) {
        cfg->flowbus_read_timeout_ms = 250u;
    }
    if (cfg->flowbus_read_timeout_ms > cfg->flowbus_timeout_ms) {
        cfg->flowbus_read_timeout_ms = cfg->flowbus_timeout_ms;
    }
    if (cfg->verdict_timeout_ms < 250u) {
        cfg->verdict_timeout_ms = 250u;
    }
    if (cfg->verdict_timeout_under_load_ms < cfg->verdict_timeout_ms) {
        cfg->verdict_timeout_under_load_ms = cfg->verdict_timeout_ms;
    }
    if (cfg->verdict_timeout_under_load_ms > 30000u) {
        cfg->verdict_timeout_under_load_ms = 30000u;
    }
    if (cfg->no_alert_grace_ms < 25u) {
        cfg->no_alert_grace_ms = 25u;
    }
    if (cfg->no_alert_grace_ms > 1000u) {
        cfg->no_alert_grace_ms = 1000u;
    }
    if (cfg->no_alert_grace_ms >= cfg->verdict_timeout_ms) {
        cfg->no_alert_grace_ms = cfg->verdict_timeout_ms / 2u;
        if (cfg->no_alert_grace_ms < 25u) {
            cfg->no_alert_grace_ms = 25u;
        }
    }
    if (cfg->first_request_timeout_ms < cfg->preconnect_grace_ms) {
        cfg->first_request_timeout_ms = cfg->preconnect_grace_ms;
    }
    if (cfg->idle_tunnel_timeout_ms < cfg->first_request_timeout_ms) {
        cfg->idle_tunnel_timeout_ms = cfg->first_request_timeout_ms;
    }
    return 0;
}
