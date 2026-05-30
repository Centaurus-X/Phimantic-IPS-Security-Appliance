#include "phimantec/phim_proxy_worker.h"

#include "phimantec/phim_blockpage.h"
#include "phimantec/phim_connect_parser.h"
#include "phimantec/phim_h1.h"
#include "phimantec/phim_lane.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_mem.h"
#include "phimantec/phim_proxy_out.h"
#include "phimantec/phim_socket.h"
#include "phimantec/phim_string.h"
#include "phimantec/phim_tls.h"
#include "phimantec/phim_time.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <openssl/rand.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define PHIM_IO_BUF 65536u
#define PHIM_BACKLOG 1024
#define PHIM_MAX_REQUESTS_PER_TUNNEL 1000u
#define PHIM_TUNNEL_IDLE_TIMEOUT_MS 30000u
#define PHIM_STREAM_INSPECT_OVERLAP_BYTES 4096u
#define PHIM_INSPECTION_COUNTER_SLOTS 512u
#define PHIM_INSPECTION_KEY_MAX (PHIM_MAX_HOST + 96u)


typedef struct phim_inspection_counter_s {
    bool used;
    char key[PHIM_INSPECTION_KEY_MAX];
    size_t in_flight;
    uint64_t last_used_ms;
} phim_inspection_counter_t;

typedef struct phim_inspection_ticket_s {
    bool acquired;
    bool under_load;
    char host_key[PHIM_INSPECTION_KEY_MAX];
    char client_key[PHIM_INSPECTION_KEY_MAX];
    uint64_t queue_wait_us;
    size_t global_inflight;
    size_t host_inflight;
    size_t client_inflight;
    size_t waiters;
} phim_inspection_ticket_t;

static pthread_mutex_t g_inspection_gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_inspection_gate_cond = PTHREAD_COND_INITIALIZER;
static phim_inspection_counter_t g_inspection_counters[PHIM_INSPECTION_COUNTER_SLOTS];
static size_t g_inspection_inflight;
static size_t g_inspection_waiting;

static void phim_make_abs_deadline(uint64_t deadline_ms, struct timespec *ts)
{
    if (ts == NULL) {
        return;
    }
    ts->tv_sec = (time_t)(deadline_ms / 1000u);
    ts->tv_nsec = (long)((deadline_ms % 1000u) * 1000000u);
}

static void phim_inspection_key(char *dst, size_t dst_len, const char *prefix, const char *value)
{
    const char *safe_value = value != NULL && value[0] != '\0' ? value : "unknown";
    if (dst == NULL || dst_len == 0u) {
        return;
    }
    (void)snprintf(dst, dst_len, "%s:%s", prefix != NULL ? prefix : "k", safe_value);
    dst[dst_len - 1u] = '\0';
}

static phim_inspection_counter_t *phim_inspection_counter_find_locked(const char *key, bool create)
{
    size_t i;
    phim_inspection_counter_t *empty = NULL;
    phim_inspection_counter_t *victim = NULL;
    uint64_t oldest_ms = UINT64_MAX;

    if (key == NULL || key[0] == '\0') {
        return NULL;
    }
    for (i = 0u; i < PHIM_INSPECTION_COUNTER_SLOTS; i++) {
        phim_inspection_counter_t *counter = &g_inspection_counters[i];
        if (!counter->used) {
            if (empty == NULL) {
                empty = counter;
            }
            continue;
        }
        if (strcmp(counter->key, key) == 0) {
            return counter;
        }
        if (counter->in_flight == 0u && counter->last_used_ms < oldest_ms) {
            oldest_ms = counter->last_used_ms;
            victim = counter;
        }
    }
    if (!create) {
        return NULL;
    }
    if (empty == NULL) {
        empty = victim;
    }
    if (empty == NULL) {
        return NULL;
    }
    memset(empty, 0, sizeof(*empty));
    empty->used = true;
    (void)phim_copy_cstr(empty->key, sizeof(empty->key), key);
    empty->last_used_ms = phim_now_ms();
    return empty;
}

static bool phim_inspection_gate_can_enter_locked(const phim_config_t *cfg,
                                                  const char *host_key,
                                                  const char *client_key,
                                                  phim_inspection_counter_t **host_counter,
                                                  phim_inspection_counter_t **client_counter)
{
    phim_inspection_counter_t *host;
    phim_inspection_counter_t *client;

    if (cfg == NULL || host_counter == NULL || client_counter == NULL) {
        return false;
    }
    if (cfg->inspection_max_inflight > 0u && g_inspection_inflight >= cfg->inspection_max_inflight) {
        return false;
    }
    host = phim_inspection_counter_find_locked(host_key, true);
    client = phim_inspection_counter_find_locked(client_key, true);
    if (host == NULL || client == NULL) {
        return false;
    }
    if (cfg->inspection_max_inflight_per_host > 0u &&
        host->in_flight >= cfg->inspection_max_inflight_per_host) {
        return false;
    }
    if (cfg->inspection_max_inflight_per_client > 0u &&
        client->in_flight >= cfg->inspection_max_inflight_per_client) {
        return false;
    }
    *host_counter = host;
    *client_counter = client;
    return true;
}

static int phim_inspection_gate_acquire(const phim_config_t *cfg,
                                        const char *flow_id,
                                        const char *direction,
                                        const char *phase,
                                        const char *host,
                                        const char *client_ip,
                                        phim_inspection_ticket_t *ticket,
                                        char *reason,
                                        size_t reason_len)
{
    char host_key[PHIM_INSPECTION_KEY_MAX];
    char client_key[PHIM_INSPECTION_KEY_MAX];
    uint64_t start_us;
    uint64_t deadline_ms;
    bool counted_waiter = false;

    if (ticket == NULL || cfg == NULL) {
        return PHIM_ERR;
    }
    memset(ticket, 0, sizeof(*ticket));
    if (reason != NULL && reason_len > 0u) {
        reason[0] = '\0';
    }
    phim_inspection_key(host_key, sizeof(host_key), "h", host);
    phim_inspection_key(client_key, sizeof(client_key), "c", client_ip);
    start_us = phim_now_us();
    deadline_ms = phim_now_ms() + cfg->inspection_queue_wait_ms;

    pthread_mutex_lock(&g_inspection_gate_lock);
    for (;;) {
        phim_inspection_counter_t *host_counter = NULL;
        phim_inspection_counter_t *client_counter = NULL;
        if (phim_inspection_gate_can_enter_locked(cfg, host_key, client_key, &host_counter, &client_counter)) {
            if (counted_waiter && g_inspection_waiting > 0u) {
                g_inspection_waiting--;
            }
            g_inspection_inflight++;
            host_counter->in_flight++;
            client_counter->in_flight++;
            host_counter->last_used_ms = phim_now_ms();
            client_counter->last_used_ms = host_counter->last_used_ms;
            ticket->acquired = true;
            ticket->queue_wait_us = phim_now_us() - start_us;
            ticket->global_inflight = g_inspection_inflight;
            ticket->host_inflight = host_counter->in_flight;
            ticket->client_inflight = client_counter->in_flight;
            ticket->waiters = g_inspection_waiting;
            ticket->under_load = ticket->queue_wait_us > 1000u || g_inspection_waiting > 0u ||
                                 (cfg->flowbus_pool_size > 0u && g_inspection_inflight > cfg->flowbus_pool_size);
            (void)phim_copy_cstr(ticket->host_key, sizeof(ticket->host_key), host_key);
            (void)phim_copy_cstr(ticket->client_key, sizeof(ticket->client_key), client_key);
            pthread_mutex_unlock(&g_inspection_gate_lock);
            if (ticket->queue_wait_us > 1000u || ticket->under_load) {
                PHIM_LOGD("flow=%s inspection gate acquired direction=%s phase=%s queue_ms=%llu inflight=%zu host_inflight=%zu client_inflight=%zu waiters=%zu under_load=%u",
                          flow_id, direction, phase,
                          (unsigned long long)(ticket->queue_wait_us / 1000u),
                          ticket->global_inflight, ticket->host_inflight,
                          ticket->client_inflight, ticket->waiters,
                          ticket->under_load ? 1u : 0u);
            }
            return PHIM_OK;
        }

        if (cfg->inspection_queue_max == 0u || cfg->inspection_queue_wait_ms == 0u) {
            if (reason != NULL && reason_len > 0u) {
                (void)phim_copy_cstr(reason, reason_len, "inspection-inflight-limit");
            }
            break;
        }
        if (!counted_waiter) {
            if (g_inspection_waiting >= cfg->inspection_queue_max) {
                if (reason != NULL && reason_len > 0u) {
                    (void)phim_copy_cstr(reason, reason_len, "inspection-queue-full");
                }
                break;
            }
            g_inspection_waiting++;
            counted_waiter = true;
        }
        if (phim_now_ms() >= deadline_ms) {
            if (reason != NULL && reason_len > 0u) {
                (void)phim_copy_cstr(reason, reason_len, "inspection-queue-timeout");
            }
            break;
        }
        {
            struct timespec ts;
            int rc;
            phim_make_abs_deadline(deadline_ms, &ts);
            rc = pthread_cond_timedwait(&g_inspection_gate_cond, &g_inspection_gate_lock, &ts);
            if (rc != 0 && phim_now_ms() >= deadline_ms) {
                if (reason != NULL && reason_len > 0u) {
                    (void)phim_copy_cstr(reason, reason_len, "inspection-queue-timeout");
                }
                break;
            }
        }
    }
    {
        size_t inflight_snapshot;
        size_t waiting_snapshot;
        if (counted_waiter && g_inspection_waiting > 0u) {
            g_inspection_waiting--;
        }
        inflight_snapshot = g_inspection_inflight;
        waiting_snapshot = g_inspection_waiting;
        pthread_mutex_unlock(&g_inspection_gate_lock);
        PHIM_LOGW("flow=%s inspection gate denied direction=%s phase=%s reason=%s inflight=%zu waiters=%zu host=%s client=%s",
                  flow_id, direction, phase,
                  reason != NULL && reason[0] != '\0' ? reason : "inspection-gate-unavailable",
                  inflight_snapshot, waiting_snapshot, host_key, client_key);
    }
    return PHIM_ERR;
}

static void phim_inspection_gate_release(phim_inspection_ticket_t *ticket)
{
    phim_inspection_counter_t *host_counter;
    phim_inspection_counter_t *client_counter;

    if (ticket == NULL || !ticket->acquired) {
        return;
    }
    pthread_mutex_lock(&g_inspection_gate_lock);
    if (g_inspection_inflight > 0u) {
        g_inspection_inflight--;
    }
    host_counter = phim_inspection_counter_find_locked(ticket->host_key, false);
    client_counter = phim_inspection_counter_find_locked(ticket->client_key, false);
    if (host_counter != NULL && host_counter->in_flight > 0u) {
        host_counter->in_flight--;
        host_counter->last_used_ms = phim_now_ms();
    }
    if (client_counter != NULL && client_counter->in_flight > 0u) {
        client_counter->in_flight--;
        client_counter->last_used_ms = phim_now_ms();
    }
    ticket->acquired = false;
    pthread_cond_broadcast(&g_inspection_gate_cond);
    pthread_mutex_unlock(&g_inspection_gate_lock);
}

static void phim_generate_flow_id(char out[PHIM_MAX_FLOW_ID])
{
    unsigned char r[16];
    if (RAND_bytes(r, sizeof(r)) != 1) {
        uint64_t now = phim_now_us();
        memcpy(r, &now, sizeof(now));
        memset(r + sizeof(now), 0, sizeof(r) - sizeof(now));
    }
    (void)snprintf(out, PHIM_MAX_FLOW_ID,
                   "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                   r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
                   r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
}

static void phim_client_addr(int fd, char *ip, size_t ip_len, uint16_t *port)
{
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    if (ip_len > 0u) {
        ip[0] = '\0';
    }
    if (port != NULL) {
        *port = 0u;
    }
    if (getpeername(fd, (struct sockaddr *)&ss, &slen) != 0) {
        (void)phim_copy_cstr(ip, ip_len, "0.0.0.0");
        return;
    }
    if (ss.ss_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)&ss;
        (void)inet_ntop(AF_INET, &a->sin_addr, ip, (socklen_t)ip_len);
        if (port != NULL) {
            *port = ntohs(a->sin_port);
        }
    } else if (ss.ss_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)&ss;
        (void)inet_ntop(AF_INET6, &a->sin6_addr, ip, (socklen_t)ip_len);
        if (port != NULL) {
            *port = ntohs(a->sin6_port);
        }
    } else {
        (void)phim_copy_cstr(ip, ip_len, "unknown");
    }
}

static void phim_send_plain_error(int fd, int code, const char *reason)
{
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
                     "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                     code, reason);
    if (n > 0) {
        (void)phim_write_all(fd, buf, (size_t)n);
    }
}

static int phim_read_h1_request(SSL *ssl, const phim_config_t *cfg,
                                unsigned char **out, size_t *out_len,
                                phim_h1_request_t *request)
{
    unsigned char *buf = phim_xcalloc(1u, cfg->max_body_bytes + PHIM_MAX_HEADER_BLOCK + 1u);
    size_t used = 0u;
    size_t body_have;
    size_t total_needed;
    int rc;

    rc = phim_ssl_read_until(ssl, buf, PHIM_MAX_HEADER_BLOCK, "\r\n\r\n", &used);
    if (rc != PHIM_OK) {
        free(buf);
        return PHIM_ERR;
    }
    rc = phim_h1_parse_request(buf, used, request);
    if (rc != PHIM_OK) {
        free(buf);
        return PHIM_ERR;
    }
    if (request->chunked) {
        phim_h1_chunked_decoder_t decoder;
        size_t body_len;
        phim_h1_chunked_decoder_init(&decoder);
        body_len = used > request->header_len ? used - request->header_len : 0u;
        if (body_len > 0u &&
            phim_h1_chunked_decoder_feed(&decoder, buf + request->header_len, body_len) != PHIM_OK) {
            free(buf);
            return PHIM_ERR;
        }
        while (!phim_h1_chunked_decoder_complete(&decoder)) {
            ssize_t n;
            if (body_len >= cfg->max_body_bytes) {
                PHIM_LOGW("chunked request body too large: %zu >= %zu", body_len, cfg->max_body_bytes);
                free(buf);
                return PHIM_ERR;
            }
            n = phim_ssl_read_some(ssl, buf + used, cfg->max_body_bytes - body_len);
            if (n <= 0) {
                free(buf);
                return PHIM_ERR;
            }
            if (phim_h1_chunked_decoder_feed(&decoder, buf + used, (size_t)n) != PHIM_OK) {
                free(buf);
                return PHIM_ERR;
            }
            used += (size_t)n;
            body_len += (size_t)n;
        }
        *out = buf;
        *out_len = used;
        return PHIM_OK;
    }
    if (!request->has_content_length || request->content_length == 0u) {
        *out = buf;
        *out_len = used;
        return PHIM_OK;
    }
    if (request->content_length > cfg->max_body_bytes) {
        PHIM_LOGW("request body too large: %zu > %zu", request->content_length, cfg->max_body_bytes);
        free(buf);
        return PHIM_ERR;
    }
    body_have = used > request->header_len ? used - request->header_len : 0u;
    total_needed = request->header_len + request->content_length;
    while (used < total_needed) {
        ssize_t n = phim_ssl_read_some(ssl, buf + used, total_needed - used);
        if (n <= 0) {
            free(buf);
            return PHIM_ERR;
        }
        used += (size_t)n;
    }
    (void)body_have;
    *out = buf;
    *out_len = used;
    return PHIM_OK;
}

static int phim_send_block_ssl(SSL *client_ssl, const phim_verdict_t *verdict, const char *flow_id)
{
    unsigned char page[4096];
    size_t page_len = 0u;
    if (phim_blockpage_build(verdict, flow_id, page, sizeof(page), &page_len) != PHIM_OK) {
        return PHIM_ERR;
    }
    return phim_ssl_write_all(client_ssl, page, page_len);
}

static void phim_fill_meta(phim_request_meta_t *meta, const char *flow_id, const char *direction,
                           const char *phase, const char *client_ip, uint16_t client_port,
                           const phim_connect_request_t *connect_req, const char *method,
                           const char *path, const unsigned char *sample, size_t sample_len)
{
    memset(meta, 0, sizeof(*meta));
    (void)phim_copy_cstr(meta->flow_id, sizeof(meta->flow_id), flow_id);
    (void)phim_copy_cstr(meta->direction, sizeof(meta->direction), direction);
    (void)phim_copy_cstr(meta->phase, sizeof(meta->phase), phase);
    (void)phim_copy_cstr(meta->client_ip, sizeof(meta->client_ip), client_ip);
    meta->client_port = client_port;
    (void)phim_copy_cstr(meta->server_host, sizeof(meta->server_host), connect_req->host);
    meta->server_port = connect_req->port;
    (void)phim_copy_cstr(meta->method, sizeof(meta->method), method != NULL && method[0] != '\0' ? method : "GET");
    (void)phim_copy_cstr(meta->path, sizeof(meta->path), path != NULL && path[0] != '\0' ? path : "/");
    (void)phim_copy_cstr(meta->alpn, sizeof(meta->alpn), "http/1.1");
    meta->sample = sample;
    meta->sample_len = sample_len;
}

static int phim_read_exact_ssl(SSL *ssl, unsigned char *dst, size_t len)
{
    size_t used = 0u;
    while (used < len) {
        ssize_t n = phim_ssl_read_some(ssl, dst + used, len - used);
        if (n <= 0) {
            return PHIM_ERR;
        }
        used += (size_t)n;
    }
    return PHIM_OK;
}

static int phim_set_socket_recv_timeout(int fd, uint32_t timeout_ms)
{
    struct timeval tv;
    if (fd < 0) {
        return PHIM_ERR;
    }
    tv.tv_sec = (time_t)(timeout_ms / 1000u);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        return PHIM_ERR;
    }
    return PHIM_OK;
}

static size_t phim_min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static bool phim_request_method_is_head(const phim_h1_request_t *request)
{
    return request != NULL && phim_strcasecmp_ascii(request->method, "HEAD") == 0;
}

static bool phim_request_can_replay(const phim_h1_request_t *request, size_t request_len)
{
    if (request == NULL) {
        return false;
    }
    if (request_len != request->header_len) {
        return false;
    }
    return phim_strcasecmp_ascii(request->method, "GET") == 0 ||
           phim_strcasecmp_ascii(request->method, "HEAD") == 0;
}

static bool phim_response_has_no_body(const phim_h1_request_t *request, const phim_h1_response_t *response)
{
    if (response == NULL) {
        return true;
    }
    if (phim_request_method_is_head(request)) {
        return true;
    }
    if (response->status_code >= 100 && response->status_code < 200) {
        return true;
    }
    if (response->status_code == 204 || response->status_code == 304) {
        return true;
    }
    if (response->has_content_length && response->content_length == 0u) {
        return true;
    }
    return false;
}

static void phim_derive_request_flow_id(const char *base_flow_id, unsigned int request_index,
                                        char out[PHIM_MAX_FLOW_ID])
{
    char suffix[16];
    int suffix_len;
    size_t base_len;
    size_t copy_len;

    if (request_index == 0u) {
        (void)phim_copy_cstr(out, PHIM_MAX_FLOW_ID, base_flow_id);
        return;
    }

    suffix_len = snprintf(suffix, sizeof(suffix), "-%04u", request_index);
    if (suffix_len <= 0 || (size_t)suffix_len >= sizeof(suffix)) {
        (void)phim_copy_cstr(out, PHIM_MAX_FLOW_ID, base_flow_id);
        return;
    }

    base_len = strlen(base_flow_id);
    copy_len = PHIM_MAX_FLOW_ID - (size_t)suffix_len - 1u;
    if (base_len < copy_len) {
        copy_len = base_len;
    }

    memcpy(out, base_flow_id, copy_len);
    memcpy(out + copy_len, suffix, (size_t)suffix_len + 1u);
}

static void phim_upstream_drop(int *fd, SSL_CTX **ctx, SSL **ssl)
{
    phim_ssl_close(ssl);
    phim_ssl_ctx_free(ctx);
    phim_close_fd(fd);
}

static int phim_write_bad_gateway_ssl(SSL *client_ssl)
{
    const char *bad_gateway = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: 11\r\n\r\nBad Gateway";
    return phim_ssl_write_all(client_ssl, bad_gateway, strlen(bad_gateway));
}

static int phim_write_inspection_unavailable_ssl_reason(SSL *client_ssl, const char *reason)
{
    const char *safe_reason = reason != NULL && reason[0] != '\0' ? reason : "suricata-inspection-unavailable";
    char body[256];
    char response[768];
    int body_len;
    int n;

    body_len = snprintf(body, sizeof(body), "Inspection unavailable\nReason: %s\n", safe_reason);
    if (body_len <= 0 || (size_t)body_len >= sizeof(body)) {
        return PHIM_ERR;
    }
    n = snprintf(response, sizeof(response),
                 "HTTP/1.1 503 Inspection Unavailable\r\n"
                 "Content-Type: text/plain\r\n"
                 "X-Phimantec-Inspection-Reason: %s\r\n"
                 "Connection: close\r\n"
                 "Content-Length: %u\r\n\r\n%s",
                 safe_reason, (unsigned int)body_len, body);
    if (n <= 0 || (size_t)n >= sizeof(response)) {
        return PHIM_ERR;
    }
    return phim_ssl_write_all(client_ssl, (const unsigned char *)response, (size_t)n);
}

static int phim_write_inspection_unavailable_ssl(SSL *client_ssl)
{
    return phim_write_inspection_unavailable_ssl_reason(client_ssl, "suricata-inspection-unavailable");
}

static int phim_inspect_sample(SSL *client_ssl, const phim_config_t *cfg,
                               phim_verdict_engine_t *verdict_engine,
                               const char *flow_id, const char *client_ip,
                               uint16_t client_port,
                               const phim_connect_request_t *connect_req,
                               const phim_h1_request_t *request,
                               const char *direction, const char *phase,
                               const unsigned char *sample, size_t sample_len,
                               bool headers_already_sent,
                               bool *out_blocked)
{
    phim_request_meta_t meta;
    phim_verdict_t verdict;
    phim_inspection_ticket_t ticket;
    char gate_reason[PHIM_MAX_REASON];
    uint32_t verdict_timeout_ms;
    uint64_t inspect_start_us;

    if (out_blocked != NULL) {
        *out_blocked = false;
    }
    gate_reason[0] = '\0';
    phim_fill_meta(&meta, flow_id, direction, phase, client_ip, client_port, connect_req,
                   request->method, request->target, sample, sample_len);

    if (phim_inspection_gate_acquire(cfg, flow_id, direction, phase, meta.server_host,
                                     meta.client_ip, &ticket, gate_reason, sizeof(gate_reason)) != PHIM_OK) {
        if (!headers_already_sent) {
            (void)phim_write_inspection_unavailable_ssl_reason(client_ssl,
                                                               gate_reason[0] != '\0' ? gate_reason : "inspection-queue-unavailable");
        }
        if (out_blocked != NULL) {
            *out_blocked = true;
        }
        return PHIM_OK;
    }

    inspect_start_us = phim_now_us();
    verdict_timeout_ms = ticket.under_load ? cfg->verdict_timeout_under_load_ms : cfg->verdict_timeout_ms;
    if (phim_lane_send(cfg, &meta) != PHIM_OK) {
        PHIM_LOGW("flow=%s %s/%s lane send failed; failing closed queue_ms=%llu inflight=%zu host_inflight=%zu client_inflight=%zu",
                  flow_id, direction, phase, (unsigned long long)(ticket.queue_wait_us / 1000u),
                  ticket.global_inflight, ticket.host_inflight, ticket.client_inflight);
        phim_inspection_gate_release(&ticket);
        if (!headers_already_sent) {
            (void)phim_write_inspection_unavailable_ssl_reason(client_ssl, "lane-send-failed");
        }
        if (out_blocked != NULL) {
            *out_blocked = true;
        }
        return PHIM_OK;
    }
    if (phim_verdict_check_with_timeout(verdict_engine, &meta, &verdict, verdict_timeout_ms) != PHIM_OK) {
        PHIM_LOGW("flow=%s %s/%s inspection verdict failed queue_ms=%llu verdict_budget_ms=%u",
                  flow_id, direction, phase,
                  (unsigned long long)(ticket.queue_wait_us / 1000u),
                  (unsigned int)verdict_timeout_ms);
        phim_inspection_gate_release(&ticket);
        if (!headers_already_sent) {
            (void)phim_write_inspection_unavailable_ssl_reason(client_ssl, "flowbus-verdict-failed");
        }
        if (out_blocked != NULL) {
            *out_blocked = true;
        }
        return PHIM_OK;
    }
    phim_inspection_gate_release(&ticket);
    PHIM_LOGD("flow=%s %s/%s inspection done action=%d reason=%s queue_ms=%llu inspect_ms=%llu verdict_budget_ms=%u inflight=%zu host_inflight=%zu client_inflight=%zu",
              flow_id, direction, phase, (int)verdict.action, verdict.reason,
              (unsigned long long)(ticket.queue_wait_us / 1000u),
              (unsigned long long)((phim_now_us() - inspect_start_us) / 1000u),
              (unsigned int)verdict_timeout_ms, ticket.global_inflight,
              ticket.host_inflight, ticket.client_inflight);
    if (verdict.action != PHIM_VERDICT_ALLOW) {
        PHIM_LOGI("flow=%s %s/%s blocked reason=%s sid=%u msg=%s queue_ms=%llu verdict_ms=%llu", flow_id, direction, phase,
                  verdict.reason, (unsigned int)verdict.sid, verdict.rule_msg,
                  (unsigned long long)(ticket.queue_wait_us / 1000u),
                  (unsigned long long)(verdict.latency_us / 1000u));
        if (!headers_already_sent) {
            if (verdict.action == PHIM_VERDICT_BLOCK) {
                (void)phim_send_block_ssl(client_ssl, &verdict, flow_id);
            } else {
                (void)phim_write_inspection_unavailable_ssl_reason(client_ssl, verdict.reason);
            }
        } else {
            PHIM_LOGW("flow=%s %s/%s blocked after response headers were forwarded; closing stream without injecting a nested block page",
                      flow_id, direction, phase);
        }
        if (out_blocked != NULL) {
            *out_blocked = true;
        }
    }
    return PHIM_OK;
}

typedef struct phim_forward_result_s {
    bool blocked;
    bool upstream_reusable;
    bool client_reusable;
} phim_forward_result_t;

static int phim_write_filtered_response_header(SSL *client_ssl,
                                               const unsigned char *header,
                                               size_t header_len)
{
    unsigned char *filtered = phim_xmalloc(PHIM_IO_BUF + 1u);
    size_t filtered_len = 0u;
    int rc;

    rc = phim_h1_strip_alt_svc(header, header_len, filtered, PHIM_IO_BUF, &filtered_len);
    if (rc == PHIM_OK) {
        rc = phim_ssl_write_all(client_ssl, filtered, filtered_len);
    }
    free(filtered);
    return rc;
}

static int phim_inspect_response_part(SSL *client_ssl, const phim_config_t *cfg,
                                      phim_verdict_engine_t *verdict_engine,
                                      const char *flow_id, const char *client_ip,
                                      uint16_t client_port,
                                      const phim_connect_request_t *connect_req,
                                      const phim_h1_request_t *request,
                                      const char *phase,
                                      const unsigned char *sample, size_t sample_len,
                                      bool headers_already_sent,
                                      bool *out_blocked)
{
    if (sample_len == 0u) {
        return phim_inspect_sample(client_ssl, cfg, verdict_engine, flow_id, client_ip, client_port,
                                   connect_req, request, "response", "headers",
                                   NULL, 0u, headers_already_sent, out_blocked);
    }
    return phim_inspect_sample(client_ssl, cfg, verdict_engine, flow_id, client_ip, client_port,
                               connect_req, request, "response", phase,
                               sample, sample_len, headers_already_sent, out_blocked);
}

static size_t phim_effective_sample_target(const phim_config_t *cfg)
{
    size_t target;
    size_t max_sample;

    if (cfg == NULL) {
        return PHIM_MAX_SAMPLE;
    }
    max_sample = cfg->max_body_bytes;
    target = cfg->sample_target_bytes;
    if (max_sample < 4096u) {
        max_sample = 4096u;
    }
    if (target < 4096u) {
        target = 4096u;
    }
    if (max_sample > PHIM_STREAM_INSPECT_OVERLAP_BYTES + 4096u &&
        target > max_sample - PHIM_STREAM_INSPECT_OVERLAP_BYTES) {
        target = max_sample - PHIM_STREAM_INSPECT_OVERLAP_BYTES;
    } else if (target > max_sample) {
        target = max_sample;
    }
    return target;
}

static void phim_derive_response_segment_flow_id(const char *flow_id, unsigned int segment_index,
                                                 char out[PHIM_MAX_FLOW_ID])
{
    char suffix[16];
    int suffix_len;
    size_t flow_len;
    size_t copy_len;

    suffix_len = snprintf(suffix, sizeof(suffix), "-%05u", 90000u + segment_index);
    if (suffix_len <= 0 || (size_t)suffix_len >= sizeof(suffix)) {
        (void)phim_copy_cstr(out, PHIM_MAX_FLOW_ID, flow_id);
        return;
    }
    flow_len = strlen(flow_id);
    copy_len = PHIM_MAX_FLOW_ID - (size_t)suffix_len - 1u;
    if (flow_len < copy_len) {
        copy_len = flow_len;
    }
    memcpy(out, flow_id, copy_len);
    memcpy(out + copy_len, suffix, (size_t)suffix_len + 1u);
}

static void phim_stream_tail_update(unsigned char tail[PHIM_STREAM_INSPECT_OVERLAP_BYTES],
                                    size_t *tail_len,
                                    const unsigned char *chunk,
                                    size_t chunk_len)
{
    size_t keep;

    if (tail == NULL || tail_len == NULL || chunk == NULL || chunk_len == 0u) {
        return;
    }
    if (chunk_len >= PHIM_STREAM_INSPECT_OVERLAP_BYTES) {
        memcpy(tail, chunk + chunk_len - PHIM_STREAM_INSPECT_OVERLAP_BYTES,
               PHIM_STREAM_INSPECT_OVERLAP_BYTES);
        *tail_len = PHIM_STREAM_INSPECT_OVERLAP_BYTES;
        return;
    }
    keep = *tail_len;
    if (keep + chunk_len > PHIM_STREAM_INSPECT_OVERLAP_BYTES) {
        size_t drop = keep + chunk_len - PHIM_STREAM_INSPECT_OVERLAP_BYTES;
        memmove(tail, tail + drop, keep - drop);
        keep -= drop;
    }
    memcpy(tail + keep, chunk, chunk_len);
    *tail_len = keep + chunk_len;
}

static int phim_write_response_header_once(SSL *client_ssl,
                                           const unsigned char *header_buf,
                                           size_t header_len,
                                           bool *header_sent)
{
    if (header_sent == NULL) {
        return PHIM_ERR;
    }
    if (*header_sent) {
        return PHIM_OK;
    }
    if (phim_write_filtered_response_header(client_ssl, header_buf, header_len) != PHIM_OK) {
        return PHIM_ERR;
    }
    *header_sent = true;
    return PHIM_OK;
}

static int phim_inspect_response_stream_chunk(SSL *client_ssl, const phim_config_t *cfg,
                                              phim_verdict_engine_t *verdict_engine,
                                              const char *flow_id, const char *client_ip,
                                              uint16_t client_port,
                                              const phim_connect_request_t *connect_req,
                                              const phim_h1_request_t *request,
                                              const unsigned char *chunk,
                                              size_t chunk_len,
                                              unsigned char tail[PHIM_STREAM_INSPECT_OVERLAP_BYTES],
                                              size_t *tail_len,
                                              unsigned int *segment_index,
                                              bool headers_already_sent,
                                              bool *out_blocked)
{
    char segment_flow_id[PHIM_MAX_FLOW_ID];
    unsigned char *sample = NULL;
    const unsigned char *sample_ptr;
    size_t sample_len;
    bool blocked = false;
    int rc;

    if (out_blocked != NULL) {
        *out_blocked = false;
    }
    if (chunk == NULL || chunk_len == 0u || tail_len == NULL || segment_index == NULL) {
        return PHIM_ERR;
    }

    phim_derive_response_segment_flow_id(flow_id, *segment_index, segment_flow_id);
    sample_len = *tail_len + chunk_len;
    if (*tail_len > 0u) {
        sample = phim_xmalloc(sample_len + 1u);
        memcpy(sample, tail, *tail_len);
        memcpy(sample + *tail_len, chunk, chunk_len);
        sample_ptr = sample;
    } else {
        sample_ptr = chunk;
    }

    rc = phim_inspect_response_part(client_ssl, cfg, verdict_engine, segment_flow_id, client_ip, client_port,
                                    connect_req, request, "body", sample_ptr, sample_len,
                                    headers_already_sent, &blocked);
    free(sample);
    if (rc != PHIM_OK) {
        return rc;
    }
    if (blocked) {
        if (out_blocked != NULL) {
            *out_blocked = true;
        }
        return PHIM_OK;
    }
    phim_stream_tail_update(tail, tail_len, chunk, chunk_len);
    (*segment_index)++;
    return PHIM_OK;
}

static int phim_forward_response_fixed_length(SSL *client_ssl, SSL *upstream_ssl,
                                              const phim_config_t *cfg,
                                              phim_verdict_engine_t *verdict_engine,
                                              const char *flow_id, const char *client_ip,
                                              uint16_t client_port,
                                              const phim_connect_request_t *connect_req,
                                              const phim_h1_request_t *request,
                                              const unsigned char *header_buf,
                                              const phim_h1_response_t *response,
                                              const unsigned char *initial_body,
                                              size_t initial_body_len,
                                              phim_forward_result_t *result)
{
    unsigned char tail[PHIM_STREAM_INSPECT_OVERLAP_BYTES];
    size_t tail_len = 0u;
    size_t target = phim_effective_sample_target(cfg);
    unsigned char *chunk = phim_xmalloc(target + 1u);
    size_t remaining = response->content_length;
    size_t initial_off = 0u;
    size_t initial_limit = phim_min_size(initial_body_len, response->content_length);
    unsigned int segment_index = 0u;
    bool header_sent = false;

    memset(tail, 0, sizeof(tail));
    while (remaining > 0u) {
        size_t chunk_len = 0u;
        bool blocked = false;
        while (chunk_len < target && remaining > 0u) {
            size_t want = phim_min_size(target - chunk_len, remaining);
            if (initial_body != NULL && initial_off < initial_limit) {
                size_t available = initial_limit - initial_off;
                size_t take = phim_min_size(want, available);
                memcpy(chunk + chunk_len, initial_body + initial_off, take);
                chunk_len += take;
                initial_off += take;
                remaining -= take;
                continue;
            }
            if (phim_read_exact_ssl(upstream_ssl, chunk + chunk_len, want) != PHIM_OK) {
                free(chunk);
                return PHIM_ERR;
            }
            chunk_len += want;
            remaining -= want;
        }
        if (chunk_len == 0u) {
            break;
        }
        if (phim_inspect_response_stream_chunk(client_ssl, cfg, verdict_engine, flow_id, client_ip, client_port,
                                               connect_req, request, chunk, chunk_len, tail, &tail_len,
                                               &segment_index, header_sent, &blocked) != PHIM_OK) {
            free(chunk);
            return PHIM_ERR;
        }
        if (blocked) {
            result->blocked = true;
            free(chunk);
            return PHIM_OK;
        }
        if (phim_write_response_header_once(client_ssl, header_buf, response->header_len, &header_sent) != PHIM_OK) {
            free(chunk);
            return PHIM_ERR;
        }
        if (phim_ssl_write_all(client_ssl, chunk, chunk_len) != PHIM_OK) {
            free(chunk);
            return PHIM_ERR;
        }
    }

    free(chunk);
    result->upstream_reusable = !response->connection_close && !request->connection_close;
    result->client_reusable = result->upstream_reusable;
    return PHIM_OK;
}

static int phim_forward_response_chunked(SSL *client_ssl, SSL *upstream_ssl,
                                         const phim_config_t *cfg,
                                         phim_verdict_engine_t *verdict_engine,
                                         const char *flow_id, const char *client_ip,
                                         uint16_t client_port,
                                         const phim_connect_request_t *connect_req,
                                         const phim_h1_request_t *request,
                                         const unsigned char *header_buf,
                                         const phim_h1_response_t *response,
                                         const unsigned char *initial_body,
                                         size_t initial_body_len,
                                         phim_forward_result_t *result)
{
    phim_h1_chunked_decoder_t decoder;
    unsigned char tail[PHIM_STREAM_INSPECT_OVERLAP_BYTES];
    size_t tail_len = 0u;
    size_t target = phim_effective_sample_target(cfg);
    unsigned char *chunk = phim_xmalloc(target + 1u);
    size_t chunk_len = 0u;
    size_t initial_off = 0u;
    unsigned int segment_index = 0u;
    bool header_sent = false;

    (void)response;
    memset(tail, 0, sizeof(tail));
    phim_h1_chunked_decoder_init(&decoder);

    for (;;) {
        bool blocked = false;
        while (chunk_len < target && !phim_h1_chunked_decoder_complete(&decoder)) {
            size_t room = target - chunk_len;
            if (initial_body != NULL && initial_off < initial_body_len) {
                size_t take = phim_min_size(room, initial_body_len - initial_off);
                memcpy(chunk + chunk_len, initial_body + initial_off, take);
                if (phim_h1_chunked_decoder_feed(&decoder, chunk + chunk_len, take) != PHIM_OK) {
                    free(chunk);
                    return PHIM_ERR;
                }
                chunk_len += take;
                initial_off += take;
            } else {
                ssize_t n = phim_ssl_read_some(upstream_ssl, chunk + chunk_len, room);
                if (n <= 0) {
                    free(chunk);
                    return PHIM_ERR;
                }
                if (phim_h1_chunked_decoder_feed(&decoder, chunk + chunk_len, (size_t)n) != PHIM_OK) {
                    free(chunk);
                    return PHIM_ERR;
                }
                chunk_len += (size_t)n;
            }
        }
        if (chunk_len > 0u) {
            if (phim_inspect_response_stream_chunk(client_ssl, cfg, verdict_engine, flow_id, client_ip, client_port,
                                                   connect_req, request, chunk, chunk_len, tail, &tail_len,
                                                   &segment_index, header_sent, &blocked) != PHIM_OK) {
                free(chunk);
                return PHIM_ERR;
            }
            if (blocked) {
                result->blocked = true;
                free(chunk);
                return PHIM_OK;
            }
            if (phim_write_response_header_once(client_ssl, header_buf, response->header_len, &header_sent) != PHIM_OK) {
                free(chunk);
                return PHIM_ERR;
            }
            if (phim_ssl_write_all(client_ssl, chunk, chunk_len) != PHIM_OK) {
                free(chunk);
                return PHIM_ERR;
            }
            chunk_len = 0u;
        }
        if (phim_h1_chunked_decoder_complete(&decoder)) {
            break;
        }
    }

    free(chunk);
    result->upstream_reusable = !response->connection_close && !request->connection_close;
    result->client_reusable = result->upstream_reusable;
    return PHIM_OK;
}

static int phim_forward_response_until_close(SSL *client_ssl, SSL *upstream_ssl,
                                             const phim_config_t *cfg,
                                             phim_verdict_engine_t *verdict_engine,
                                             const char *flow_id, const char *client_ip,
                                             uint16_t client_port,
                                             const phim_connect_request_t *connect_req,
                                             const phim_h1_request_t *request,
                                             const unsigned char *header_buf,
                                             const phim_h1_response_t *response,
                                             const unsigned char *initial_body,
                                             size_t initial_body_len,
                                             phim_forward_result_t *result)
{
    unsigned char tail[PHIM_STREAM_INSPECT_OVERLAP_BYTES];
    size_t tail_len = 0u;
    size_t target = phim_effective_sample_target(cfg);
    unsigned char *chunk = phim_xmalloc(target + 1u);
    size_t chunk_len = 0u;
    size_t initial_off = 0u;
    unsigned int segment_index = 0u;
    bool header_sent = false;

    (void)response;
    memset(tail, 0, sizeof(tail));

    for (;;) {
        ssize_t n;
        bool blocked = false;
        while (chunk_len < target && initial_body != NULL && initial_off < initial_body_len) {
            size_t take = phim_min_size(target - chunk_len, initial_body_len - initial_off);
            memcpy(chunk + chunk_len, initial_body + initial_off, take);
            chunk_len += take;
            initial_off += take;
        }
        if (chunk_len < target) {
            n = phim_ssl_read_some(upstream_ssl, chunk + chunk_len, target - chunk_len);
            if (n > 0) {
                chunk_len += (size_t)n;
            } else if (n < 0) {
                free(chunk);
                return PHIM_ERR;
            }
        } else {
            n = 1;
        }

        if (chunk_len > 0u && (chunk_len >= target || n == 0)) {
            if (phim_inspect_response_stream_chunk(client_ssl, cfg, verdict_engine, flow_id, client_ip, client_port,
                                                   connect_req, request, chunk, chunk_len, tail, &tail_len,
                                                   &segment_index, header_sent, &blocked) != PHIM_OK) {
                free(chunk);
                return PHIM_ERR;
            }
            if (blocked) {
                result->blocked = true;
                free(chunk);
                return PHIM_OK;
            }
            if (phim_write_response_header_once(client_ssl, header_buf, response->header_len, &header_sent) != PHIM_OK) {
                free(chunk);
                return PHIM_ERR;
            }
            if (phim_ssl_write_all(client_ssl, chunk, chunk_len) != PHIM_OK) {
                free(chunk);
                return PHIM_ERR;
            }
            chunk_len = 0u;
        }
        if (n == 0) {
            break;
        }
    }

    if (!header_sent) {
        bool blocked = false;
        char segment_flow_id[PHIM_MAX_FLOW_ID];
        phim_derive_response_segment_flow_id(flow_id, segment_index, segment_flow_id);
        if (phim_inspect_response_part(client_ssl, cfg, verdict_engine, segment_flow_id, client_ip, client_port,
                                       connect_req, request, "headers", NULL, 0u, false, &blocked) != PHIM_OK) {
            free(chunk);
            return PHIM_ERR;
        }
        if (blocked) {
            result->blocked = true;
            free(chunk);
            return PHIM_OK;
        }
        if (phim_write_response_header_once(client_ssl, header_buf, response->header_len, &header_sent) != PHIM_OK) {
            free(chunk);
            return PHIM_ERR;
        }
    }

    free(chunk);
    result->upstream_reusable = false;
    result->client_reusable = false;
    return PHIM_OK;
}

static int phim_forward_response(SSL *client_ssl, SSL *upstream_ssl, const phim_config_t *cfg,
                                 phim_verdict_engine_t *verdict_engine,
                                 const char *flow_id, const char *client_ip, uint16_t client_port,
                                 const phim_connect_request_t *connect_req,
                                 const phim_h1_request_t *request,
                                 phim_forward_result_t *result)
{
    unsigned char *buf = phim_xcalloc(1u, PHIM_IO_BUF + 1u);
    size_t used = 0u;
    phim_h1_response_t response;
    size_t initial_body_len;
    int rc;

    if (result == NULL) {
        free(buf);
        return PHIM_ERR;
    }
    memset(result, 0, sizeof(*result));

    rc = phim_ssl_read_until(upstream_ssl, buf, PHIM_IO_BUF, "\r\n\r\n", &used);
    if (rc != PHIM_OK) {
        free(buf);
        return PHIM_ERR;
    }
    if (phim_h1_parse_response(buf, used, &response) != PHIM_OK) {
        free(buf);
        return PHIM_ERR;
    }

    initial_body_len = used > response.header_len ? used - response.header_len : 0u;

    if (phim_response_has_no_body(request, &response)) {
        bool blocked = false;
        char segment_flow_id[PHIM_MAX_FLOW_ID];
        phim_derive_response_segment_flow_id(flow_id, 0u, segment_flow_id);
        if (phim_inspect_response_part(client_ssl, cfg, verdict_engine, segment_flow_id, client_ip, client_port,
                                       connect_req, request, "headers", NULL, 0u, false, &blocked) != PHIM_OK) {
            free(buf);
            return PHIM_ERR;
        }
        if (blocked) {
            result->blocked = true;
            free(buf);
            return PHIM_OK;
        }
        if (phim_write_filtered_response_header(client_ssl, buf, response.header_len) != PHIM_OK) {
            free(buf);
            return PHIM_ERR;
        }
        result->upstream_reusable = !response.connection_close && !request->connection_close;
        result->client_reusable = result->upstream_reusable;
        free(buf);
        return PHIM_OK;
    }

    if (response.has_content_length) {
        rc = phim_forward_response_fixed_length(client_ssl, upstream_ssl, cfg, verdict_engine,
                                                flow_id, client_ip, client_port, connect_req, request,
                                                buf, &response,
                                                initial_body_len > 0u ? buf + response.header_len : NULL,
                                                initial_body_len, result);
    } else if (response.chunked) {
        rc = phim_forward_response_chunked(client_ssl, upstream_ssl, cfg, verdict_engine,
                                           flow_id, client_ip, client_port, connect_req, request,
                                           buf, &response,
                                           initial_body_len > 0u ? buf + response.header_len : NULL,
                                           initial_body_len, result);
    } else {
        rc = phim_forward_response_until_close(client_ssl, upstream_ssl, cfg, verdict_engine,
                                               flow_id, client_ip, client_port, connect_req, request,
                                               buf, &response,
                                               initial_body_len > 0u ? buf + response.header_len : NULL,
                                               initial_body_len, result);
    }

    free(buf);
    return rc;
}

typedef struct phim_worker_arg_s {
    int fd;
    phim_proxy_runtime_t *rt;
} phim_worker_arg_t;

static int phim_connect_upstream_for_tunnel(const phim_config_t *cfg,
                                            const phim_connect_request_t *connect_req,
                                            int *up_fd, SSL_CTX **up_ctx, SSL **up_ssl)
{
    if (phim_proxy_out_connect_tls(cfg, connect_req->host, connect_req->port, up_fd, up_ctx, up_ssl) != PHIM_OK) {
        return PHIM_ERR;
    }
    (void)phim_set_socket_recv_timeout(*up_fd, cfg->idle_tunnel_timeout_ms);
    return PHIM_OK;
}

static int phim_send_request_to_upstream(const phim_config_t *cfg,
                                         const phim_connect_request_t *connect_req,
                                         const phim_h1_request_t *request,
                                         const unsigned char *request_buf,
                                         size_t request_len,
                                         int *up_fd, SSL_CTX **up_ctx, SSL **up_ssl)
{
    if (*up_ssl == NULL) {
        if (phim_connect_upstream_for_tunnel(cfg, connect_req, up_fd, up_ctx, up_ssl) != PHIM_OK) {
            return PHIM_ERR;
        }
    }

    if (phim_ssl_write_all(*up_ssl, request_buf, request_len) == PHIM_OK) {
        return PHIM_OK;
    }

    PHIM_LOGW("upstream write failed for %s:%u; dropping reusable upstream", connect_req->host,
              (unsigned int)connect_req->port);
    phim_upstream_drop(up_fd, up_ctx, up_ssl);

    if (!phim_request_can_replay(request, request_len)) {
        return PHIM_ERR;
    }

    if (phim_connect_upstream_for_tunnel(cfg, connect_req, up_fd, up_ctx, up_ssl) != PHIM_OK) {
        return PHIM_ERR;
    }
    return phim_ssl_write_all(*up_ssl, request_buf, request_len);
}

static int phim_proxy_process_one_h1_request(SSL *client_ssl,
                                             phim_proxy_runtime_t *rt,
                                             const phim_connect_request_t *connect_req,
                                             const char *base_flow_id,
                                             unsigned int request_index,
                                             const char *client_ip,
                                             uint16_t client_port,
                                             int *up_fd, SSL_CTX **up_ctx, SSL **up_ssl,
                                             bool *out_continue)
{
    unsigned char *request_buf = NULL;
    size_t request_len = 0u;
    phim_h1_request_t h1req;
    phim_request_meta_t meta;
    phim_verdict_t verdict;
    phim_forward_result_t forward_result;
    char request_flow_id[PHIM_MAX_FLOW_ID];

    if (out_continue == NULL) {
        return PHIM_ERR;
    }
    *out_continue = false;
    phim_derive_request_flow_id(base_flow_id, request_index, request_flow_id);
    if (request_index == 0u) {
        (void)phim_set_socket_recv_timeout(SSL_get_fd(client_ssl), rt->cfg.first_request_timeout_ms);
    } else {
        (void)phim_set_socket_recv_timeout(SSL_get_fd(client_ssl), rt->cfg.idle_tunnel_timeout_ms);
    }

    if (phim_read_h1_request(client_ssl, &rt->cfg, &request_buf, &request_len, &h1req) != PHIM_OK) {
        if (request_index == 0u) {
            PHIM_LOGD("flow=%s no H1 request after TLS bump within %u ms; treating as idle/preconnect tunnel",
                      base_flow_id, (unsigned int)rt->cfg.first_request_timeout_ms);
        } else {
            PHIM_LOGD("flow=%s tunnel idle/closed after %u requests", base_flow_id, request_index);
        }
        return PHIM_OK;
    }
    (void)phim_set_socket_recv_timeout(SSL_get_fd(client_ssl), rt->cfg.idle_tunnel_timeout_ms);

    phim_fill_meta(&meta, request_flow_id, "request", "headers", client_ip, client_port, connect_req,
                   h1req.method, h1req.target,
                   request_len > h1req.header_len ? request_buf + h1req.header_len : NULL,
                   request_len > h1req.header_len ? request_len - h1req.header_len : 0u);
    if (phim_lane_send(&rt->cfg, &meta) != PHIM_OK) {
        PHIM_LOGW("flow=%s request lane send failed; failing closed", request_flow_id);
        (void)phim_write_inspection_unavailable_ssl(client_ssl);
        free(request_buf);
        return PHIM_OK;
    }
    if (phim_verdict_check(&rt->verdict_engine, &meta, &verdict) != PHIM_OK) {
        (void)phim_write_inspection_unavailable_ssl(client_ssl);
        free(request_buf);
        return PHIM_OK;
    }
    if (verdict.action != PHIM_VERDICT_ALLOW) {
        PHIM_LOGI("flow=%s request blocked reason=%s sid=%u msg=%s", request_flow_id,
                  verdict.reason, (unsigned int)verdict.sid, verdict.rule_msg);
        if (verdict.action == PHIM_VERDICT_BLOCK) {
            (void)phim_send_block_ssl(client_ssl, &verdict, request_flow_id);
        } else {
            (void)phim_write_inspection_unavailable_ssl(client_ssl);
        }
        free(request_buf);
        return PHIM_OK;
    }

    if (phim_send_request_to_upstream(&rt->cfg, connect_req, &h1req, request_buf, request_len,
                                      up_fd, up_ctx, up_ssl) != PHIM_OK) {
        (void)phim_write_bad_gateway_ssl(client_ssl);
        free(request_buf);
        return PHIM_ERR;
    }

    if (phim_forward_response(client_ssl, *up_ssl, &rt->cfg, &rt->verdict_engine,
                              request_flow_id, client_ip, client_port, connect_req, &h1req,
                              &forward_result) != PHIM_OK) {
        free(request_buf);
        return PHIM_ERR;
    }

    if (forward_result.blocked) {
        free(request_buf);
        return PHIM_OK;
    }
    if (!forward_result.upstream_reusable) {
        phim_upstream_drop(up_fd, up_ctx, up_ssl);
    }

    *out_continue = !h1req.connection_close && forward_result.client_reusable;
    free(request_buf);
    return PHIM_OK;
}

static void *phim_proxy_worker_thread(void *arg)
{
    phim_worker_arg_t *wa = (phim_worker_arg_t *)arg;
    int client_fd = wa->fd;
    phim_proxy_runtime_t *rt = wa->rt;
    unsigned char connect_buf[PHIM_MAX_HEADER_BLOCK];
    ssize_t connect_len;
    phim_connect_request_t connect_req;
    SSL_CTX *down_ctx = NULL;
    SSL *client_ssl = NULL;
    SSL_CTX *up_ctx = NULL;
    SSL *up_ssl = NULL;
    int up_fd = -1;
    char flow_id[PHIM_MAX_FLOW_ID];
    char client_ip[64];
    uint16_t client_port = 0u;
    unsigned int request_index = 0u;
    bool keep_going = true;

    free(wa);
    phim_generate_flow_id(flow_id);
    phim_client_addr(client_fd, client_ip, sizeof(client_ip), &client_port);

    connect_len = phim_read_until(client_fd, connect_buf, sizeof(connect_buf), "\r\n\r\n", rt->cfg.connect_timeout_ms);
    if (connect_len <= 0) {
        phim_close_fd(&client_fd);
        return NULL;
    }
    if (phim_parse_connect_request(connect_buf, (size_t)connect_len, &connect_req) != PHIM_OK) {
        phim_send_plain_error(client_fd, 405, "CONNECT Required");
        phim_close_fd(&client_fd);
        return NULL;
    }
    PHIM_LOGI("flow=%s CONNECT %s:%u from %s:%u", flow_id, connect_req.host,
              (unsigned int)connect_req.port, client_ip, (unsigned int)client_port);

    {
        const char *ok_msg = "HTTP/1.1 200 Connection Established\r\nProxy-Agent: phimantec-proxyd/0.2\r\n\r\n";
        if (phim_write_all(client_fd, ok_msg, strlen(ok_msg)) != 0) {
            phim_close_fd(&client_fd);
            return NULL;
        }
    }

    down_ctx = phim_tls_create_downstream_ctx(&rt->ca, connect_req.host);
    if (down_ctx == NULL) {
        phim_close_fd(&client_fd);
        return NULL;
    }
    client_ssl = phim_tls_accept_client(client_fd, down_ctx);
    if (client_ssl == NULL) {
        phim_ssl_ctx_free(&down_ctx);
        phim_close_fd(&client_fd);
        return NULL;
    }
    (void)phim_set_socket_recv_timeout(client_fd, rt->cfg.first_request_timeout_ms);

    while (keep_going && request_index < PHIM_MAX_REQUESTS_PER_TUNNEL) {
        keep_going = false;
        if (phim_proxy_process_one_h1_request(client_ssl, rt, &connect_req, flow_id,
                                             request_index, client_ip, client_port,
                                             &up_fd, &up_ctx, &up_ssl, &keep_going) != PHIM_OK) {
            break;
        }
        if (keep_going) {
            request_index++;
        }
    }

    if (request_index >= PHIM_MAX_REQUESTS_PER_TUNNEL) {
        PHIM_LOGW("flow=%s tunnel reached max_requests_per_tunnel=%u", flow_id,
                  (unsigned int)PHIM_MAX_REQUESTS_PER_TUNNEL);
    }

    phim_upstream_drop(&up_fd, &up_ctx, &up_ssl);
    phim_ssl_close(&client_ssl);
    phim_ssl_ctx_free(&down_ctx);
    phim_close_fd(&client_fd);
    return NULL;
}

int phim_proxy_run(const phim_config_t *cfg)
{
    int listen_fd;
    phim_proxy_runtime_t rt;
    memset(&rt, 0, sizeof(rt));
    rt.cfg = *cfg;
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return PHIM_ERR;
    }
    if (phim_tls_global_init() != PHIM_OK) {
        return PHIM_ERR;
    }
    if (phim_ca_load(&rt.ca, cfg->ca_cert_path, cfg->ca_key_path) != PHIM_OK) {
        return PHIM_ERR;
    }
    if (phim_verdict_engine_init(&rt.verdict_engine, cfg) != PHIM_OK) {
        phim_ca_free(&rt.ca);
        return PHIM_ERR;
    }
    listen_fd = phim_tcp_listen(cfg->listener_host, cfg->listener_port, PHIM_BACKLOG);
    if (listen_fd < 0) {
        PHIM_LOGE("listen failed on %s:%u", cfg->listener_host, (unsigned int)cfg->listener_port);
        phim_verdict_engine_close(&rt.verdict_engine);
        phim_ca_free(&rt.ca);
        return PHIM_ERR;
    }
    PHIM_LOGI("phimantec-proxyd listening on %s:%u", cfg->listener_host, (unsigned int)cfg->listener_port);
    for (;;) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int fd = accept(listen_fd, (struct sockaddr *)&ss, &slen);
        pthread_t tid;
        phim_worker_arg_t *wa;
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            PHIM_LOGE("accept failed: %s", strerror(errno));
            break;
        }
        wa = phim_xcalloc(1u, sizeof(*wa));
        wa->fd = fd;
        wa->rt = &rt;
        if (pthread_create(&tid, NULL, phim_proxy_worker_thread, wa) != 0) {
            PHIM_LOGE("pthread_create failed");
            phim_close_fd(&fd);
            free(wa);
            continue;
        }
        (void)pthread_detach(tid);
    }
    phim_close_fd(&listen_fd);
    phim_verdict_engine_close(&rt.verdict_engine);
    phim_ca_free(&rt.ca);
    return PHIM_ERR;
}
