#include "phimantec/phim_verdict.h"
#include "phimantec/phim_base64.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_mem.h"
#include "phimantec/phim_string.h"
#include "phimantec/phim_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int phim_verdict_engine_init(phim_verdict_engine_t *engine, const phim_config_t *cfg)
{
    size_t i;
    if (engine == NULL || cfg == NULL) {
        return PHIM_ERR;
    }
    memset(engine, 0, sizeof(*engine));
    engine->cfg = *cfg;
    if (pthread_mutex_init(&engine->flowbus_select_lock, NULL) != 0) {
        return PHIM_ERR;
    }
    engine->selector_initialized = true;
    if (cfg->flowbus_enabled) {
        engine->flowbus_count = cfg->flowbus_pool_size == 0u ? 1u : cfg->flowbus_pool_size;
        if (engine->flowbus_count > PHIM_VERDICT_FLOWBUS_CLIENTS) {
            PHIM_LOGW("flowbus.pool_size capped at %u", (unsigned int)PHIM_VERDICT_FLOWBUS_CLIENTS);
            engine->flowbus_count = PHIM_VERDICT_FLOWBUS_CLIENTS;
        }
        for (i = 0u; i < engine->flowbus_count; i++) {
            if (phim_flowbus_client_init(&engine->flowbus[i], cfg->flowbus_host, cfg->flowbus_port,
                                         cfg->flowbus_token, cfg->flowbus_timeout_ms,
                                         cfg->flowbus_connect_timeout_ms, cfg->flowbus_read_timeout_ms,
                                         cfg->flowbus_keepalive) != PHIM_OK) {
                phim_verdict_engine_close(engine);
                return PHIM_ERR;
            }
        }
    }
    return PHIM_OK;
}

void phim_verdict_engine_close(phim_verdict_engine_t *engine)
{
    size_t i;
    if (engine == NULL) {
        return;
    }
    for (i = 0u; i < engine->flowbus_count && i < PHIM_VERDICT_FLOWBUS_CLIENTS; i++) {
        phim_flowbus_client_close(&engine->flowbus[i]);
    }
    if (engine->selector_initialized) {
        pthread_mutex_destroy(&engine->flowbus_select_lock);
    }
    memset(engine, 0, sizeof(*engine));
}

static phim_flowbus_client_t *phim_verdict_select_flowbus_client(phim_verdict_engine_t *engine, size_t *out_idx)
{
    size_t idx;
    if (out_idx != NULL) {
        *out_idx = 0u;
    }
    if (engine == NULL || engine->flowbus_count == 0u) {
        return NULL;
    }
    pthread_mutex_lock(&engine->flowbus_select_lock);
    idx = engine->flowbus_next % engine->flowbus_count;
    engine->flowbus_next++;
    pthread_mutex_unlock(&engine->flowbus_select_lock);
    if (out_idx != NULL) {
        *out_idx = idx;
    }
    return &engine->flowbus[idx];
}

static int phim_build_verdict_request_json(const phim_request_meta_t *meta, char **out_json, uint32_t *out_len)
{
    char *json;
    size_t json_cap;
    int n;

    if (meta == NULL || out_json == NULL || out_len == NULL) {
        return PHIM_ERR;
    }

    /*
     * The decrypted body sample is already sent to the Suricata lane before this
     * FlowBus request is made. The inspectord only needs the Flow-ID to
     * correlate Suricata EVE output and return the final Suricata verdict.
     * Keeping FlowBus metadata-only removes duplicate body transport from the
     * hot path without adding a proxy-side security decision.
     */
    json_cap = 4096u;
    json = phim_xcalloc(1u, json_cap);
    n = snprintf(json, json_cap,
                 "{\"schema\":\"phimantec.verdict.request.v1\","
                 "\"flow_id\":\"%s\",\"direction\":\"%s\",\"phase\":\"%s\","
                 "\"client\":{\"ip\":\"%s\",\"port\":%u},"
                 "\"server\":{\"sni\":\"%s\",\"host\":\"%s\",\"port\":%u,\"alpn\":\"%s\"},"
                 "\"http\":{\"version\":\"h1\",\"method\":\"%s\",\"scheme\":\"https\",\"authority\":\"%s\",\"path\":\"%s\"},"
                 "\"body\":{\"sample_len\":%zu,\"sample_omitted\":true,\"complete\":true},"
                 "\"policy\":{\"timeout_ms\":500}}",
                 meta->flow_id, meta->direction, meta->phase,
                 meta->client_ip, (unsigned int)meta->client_port,
                 meta->server_host, meta->server_host, (unsigned int)meta->server_port,
                 meta->alpn[0] != '\0' ? meta->alpn : "http/1.1",
                 meta->method, meta->server_host, meta->path,
                 meta->sample_len);
    if (n < 0 || (size_t)n >= json_cap || (size_t)n > UINT32_MAX) {
        free(json);
        return PHIM_ERR;
    }
    *out_json = json;
    *out_len = (uint32_t)n;
    return PHIM_OK;
}


static uint32_t phim_verdict_total_timeout_ms(const phim_config_t *cfg, uint32_t override_timeout_ms)
{
    uint32_t timeout_ms;

    timeout_ms = override_timeout_ms != 0u ? override_timeout_ms : (cfg != NULL ? cfg->verdict_timeout_ms : 0u);
    if (timeout_ms < 250u) {
        timeout_ms = 250u;
    }
    if (timeout_ms > 30000u) {
        timeout_ms = 30000u;
    }
    return timeout_ms;
}

static void phim_set_suricata_unavailable_error(phim_verdict_t *verdict, const char *reason)
{
    memset(verdict, 0, sizeof(*verdict));
    verdict->action = PHIM_VERDICT_ERROR;
    (void)phim_copy_cstr(verdict->reason, sizeof(verdict->reason), reason);
    verdict->sid = 0u;
    verdict->rule_msg[0] = '\0';
}

static bool phim_verdict_is_final(const phim_verdict_t *verdict)
{
    return verdict != NULL &&
           (verdict->action == PHIM_VERDICT_ALLOW || verdict->action == PHIM_VERDICT_BLOCK);
}

int phim_verdict_check_with_timeout(phim_verdict_engine_t *engine, const phim_request_meta_t *meta,
                                    phim_verdict_t *verdict, uint32_t timeout_ms)
{
    char *json = NULL;
    uint32_t json_len = 0u;
    unsigned char *response = NULL;
    uint32_t response_len = 0u;
    uint64_t start;
    uint64_t deadline_ms;
    uint32_t sleep_ms = 50u;
    unsigned int pending_count = 0u;
    unsigned int unavailable_count = 0u;

    if (engine == NULL || meta == NULL || verdict == NULL) {
        return PHIM_ERR;
    }
    start = phim_now_us();
    deadline_ms = phim_now_ms() + phim_verdict_total_timeout_ms(&engine->cfg, timeout_ms);
    if (!engine->cfg.flowbus_enabled) {
        phim_set_suricata_unavailable_error(verdict, "suricata-flowbus-disabled");
        verdict->latency_us = phim_now_us() - start;
        return PHIM_OK;
    }
    if (phim_build_verdict_request_json(meta, &json, &json_len) != PHIM_OK) {
        return PHIM_ERR;
    }
    for (;;) {
        size_t slot_idx = 0u;
        phim_flowbus_client_t *client = phim_verdict_select_flowbus_client(engine, &slot_idx);
        int rc;

        if (client == NULL) {
            free(json);
            return PHIM_ERR;
        }
        rc = phim_flowbus_client_request(client, (const unsigned char *)json, json_len, &response, &response_len);

        if (rc == PHIM_OK && response != NULL) {
            if (phim_verdict_from_json(response, response_len, verdict) != PHIM_OK) {
                free(response);
                free(json);
                return PHIM_ERR;
            }
            free(response);
            response = NULL;
            response_len = 0u;
            if (phim_verdict_is_final(verdict)) {
                PHIM_LOGD("FlowBus verdict final for flow=%s slot=%u reason=%s pending=%u unavailable=%u",
                          meta->flow_id, (unsigned int)slot_idx, verdict->reason,
                          pending_count, unavailable_count);
                break;
            }
            pending_count++;
            PHIM_LOGD("FlowBus verdict pending for flow=%s slot=%u reason=%s pending=%u",
                      meta->flow_id, (unsigned int)slot_idx, verdict->reason, pending_count);
        } else {
            unavailable_count++;
            PHIM_LOGW("FlowBus verdict unavailable for flow=%s slot=%u unavailable=%u",
                      meta->flow_id, (unsigned int)slot_idx, unavailable_count);
        }

        if (phim_now_ms() >= deadline_ms) {
            phim_set_suricata_unavailable_error(verdict, "suricata-inspection-unavailable");
            break;
        }
        phim_sleep_ms(sleep_ms);
        if (sleep_ms < 200u) {
            sleep_ms += 25u;
        }
    }
    free(json);
    verdict->latency_us = phim_now_us() - start;
    return PHIM_OK;
}

int phim_verdict_check(phim_verdict_engine_t *engine, const phim_request_meta_t *meta,
                       phim_verdict_t *verdict)
{
    return phim_verdict_check_with_timeout(engine, meta, verdict, 0u);
}
