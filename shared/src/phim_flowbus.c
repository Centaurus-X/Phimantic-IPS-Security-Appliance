#include "phimantec/phim_flowbus.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_mem.h"
#include "phimantec/phim_socket.h"
#include "phimantec/phim_string.h"
#include "phimantec/phim_time.h"

#include <arpa/inet.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PHIM_FLOWBUS_STACK_FRAME 768u

static void phim_put_u16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)((v >> 8u) & 0xffu);
    p[1] = (unsigned char)(v & 0xffu);
}

static void phim_put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)((v >> 24u) & 0xffu);
    p[1] = (unsigned char)((v >> 16u) & 0xffu);
    p[2] = (unsigned char)((v >> 8u) & 0xffu);
    p[3] = (unsigned char)(v & 0xffu);
}

static void phim_put_u64(unsigned char *p, uint64_t v)
{
    p[0] = (unsigned char)((v >> 56u) & 0xffu);
    p[1] = (unsigned char)((v >> 48u) & 0xffu);
    p[2] = (unsigned char)((v >> 40u) & 0xffu);
    p[3] = (unsigned char)((v >> 32u) & 0xffu);
    p[4] = (unsigned char)((v >> 24u) & 0xffu);
    p[5] = (unsigned char)((v >> 16u) & 0xffu);
    p[6] = (unsigned char)((v >> 8u) & 0xffu);
    p[7] = (unsigned char)(v & 0xffu);
}

static uint16_t phim_get_u16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8u) | (uint16_t)p[1]);
}

static uint32_t phim_get_u32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) | ((uint32_t)p[2] << 8u) | (uint32_t)p[3];
}

static uint64_t phim_get_u64(const unsigned char *p)
{
    return ((uint64_t)p[0] << 56u) | ((uint64_t)p[1] << 48u) | ((uint64_t)p[2] << 40u) | ((uint64_t)p[3] << 32u) |
           ((uint64_t)p[4] << 24u) | ((uint64_t)p[5] << 16u) | ((uint64_t)p[6] << 8u) | (uint64_t)p[7];
}

static int phim_hmac(const char *token, const unsigned char *data, size_t data_len, unsigned char out[PHIM_FLOWBUS_HMAC_LEN])
{
    unsigned int out_len = 0u;
    unsigned char *rc = HMAC(EVP_sha256(), token, (int)strlen(token), data, data_len, out, &out_len);
    if (rc == NULL || out_len != PHIM_FLOWBUS_HMAC_LEN) {
        return PHIM_ERR;
    }
    return PHIM_OK;
}

void phim_flowbus_frame_init(phim_flowbus_frame_t *frame)
{
    if (frame != NULL) {
        memset(frame, 0, sizeof(*frame));
    }
}

void phim_flowbus_frame_free(phim_flowbus_frame_t *frame)
{
    if (frame != NULL) {
        free(frame->payload);
        memset(frame, 0, sizeof(*frame));
    }
}

static int phim_flowbus_frame_total_len(uint32_t payload_len, size_t *out_total)
{
    if (out_total == NULL || payload_len > PHIM_FLOWBUS_MAX_PAYLOAD) {
        return PHIM_ERR;
    }
    *out_total = PHIM_FLOWBUS_HEADER_LEN + (size_t)payload_len + PHIM_FLOWBUS_HMAC_LEN;
    return PHIM_OK;
}

static int phim_flowbus_fill_frame(uint16_t type, uint64_t seq, const char *token,
                                   const unsigned char *payload, uint32_t payload_len,
                                   unsigned char *buf, size_t buf_len, size_t *out_len)
{
    unsigned char mac[PHIM_FLOWBUS_HMAC_LEN];
    size_t total;

    if (token == NULL || buf == NULL || out_len == NULL) {
        return PHIM_ERR;
    }
    if (payload_len > 0u && payload == NULL) {
        return PHIM_ERR;
    }
    if (phim_flowbus_frame_total_len(payload_len, &total) != PHIM_OK || buf_len < total) {
        return PHIM_ERR;
    }

    memcpy(buf, PHIM_FLOWBUS_MAGIC, 4u);
    buf[4] = PHIM_FLOWBUS_VERSION;
    buf[5] = 0u;
    phim_put_u16(buf + 6u, type);
    phim_put_u64(buf + 8u, seq);
    phim_put_u64(buf + 16u, phim_now_ms());
    if (RAND_bytes(buf + 24u, PHIM_FLOWBUS_NONCE_LEN) != 1) {
        return PHIM_ERR;
    }
    phim_put_u32(buf + 40u, payload_len);
    if (payload_len > 0u) {
        memcpy(buf + PHIM_FLOWBUS_HEADER_LEN, payload, payload_len);
    }
    if (phim_hmac(token, buf, PHIM_FLOWBUS_HEADER_LEN + (size_t)payload_len, mac) != PHIM_OK) {
        return PHIM_ERR;
    }
    memcpy(buf + PHIM_FLOWBUS_HEADER_LEN + (size_t)payload_len, mac, PHIM_FLOWBUS_HMAC_LEN);
    *out_len = total;
    return PHIM_OK;
}

int phim_flowbus_build_frame(uint16_t type, uint64_t seq, const char *token,
                             const unsigned char *payload, uint32_t payload_len,
                             unsigned char **out, size_t *out_len)
{
    unsigned char *buf;
    size_t total;
    if (out == NULL || out_len == NULL) {
        return PHIM_ERR;
    }
    *out = NULL;
    *out_len = 0u;
    if (phim_flowbus_frame_total_len(payload_len, &total) != PHIM_OK) {
        return PHIM_ERR;
    }
    buf = phim_xmalloc(total);
    if (phim_flowbus_fill_frame(type, seq, token, payload, payload_len, buf, total, out_len) != PHIM_OK) {
        free(buf);
        return PHIM_ERR;
    }
    *out = buf;
    return PHIM_OK;
}

int phim_flowbus_parse_frame(const unsigned char *buf, size_t len, const char *token,
                             phim_flowbus_frame_t *out)
{
    uint32_t payload_len;
    size_t expected;
    unsigned char mac[PHIM_FLOWBUS_HMAC_LEN];
    if (buf == NULL || token == NULL || out == NULL || len < PHIM_FLOWBUS_HEADER_LEN + PHIM_FLOWBUS_HMAC_LEN) {
        return PHIM_ERR;
    }
    if (memcmp(buf, PHIM_FLOWBUS_MAGIC, 4u) != 0 || buf[4] != PHIM_FLOWBUS_VERSION) {
        return PHIM_ERR;
    }
    payload_len = phim_get_u32(buf + 40u);
    if (payload_len > PHIM_FLOWBUS_MAX_PAYLOAD) {
        return PHIM_ERR;
    }
    expected = PHIM_FLOWBUS_HEADER_LEN + (size_t)payload_len + PHIM_FLOWBUS_HMAC_LEN;
    if (len != expected) {
        return PHIM_ERR;
    }
    if (phim_hmac(token, buf, PHIM_FLOWBUS_HEADER_LEN + (size_t)payload_len, mac) != PHIM_OK) {
        return PHIM_ERR;
    }
    if (CRYPTO_memcmp(mac, buf + PHIM_FLOWBUS_HEADER_LEN + (size_t)payload_len, PHIM_FLOWBUS_HMAC_LEN) != 0) {
        return PHIM_ERR;
    }
    phim_flowbus_frame_init(out);
    out->version = buf[4];
    out->flags = buf[5];
    out->type = phim_get_u16(buf + 6u);
    out->seq = phim_get_u64(buf + 8u);
    out->timestamp_ms = phim_get_u64(buf + 16u);
    memcpy(out->nonce, buf + 24u, PHIM_FLOWBUS_NONCE_LEN);
    out->payload_len = payload_len;
    if (payload_len > 0u) {
        out->payload = phim_xmalloc((size_t)payload_len + 1u);
        memcpy(out->payload, buf + PHIM_FLOWBUS_HEADER_LEN, payload_len);
        out->payload[payload_len] = 0u;
    }
    return PHIM_OK;
}

int phim_flowbus_write_frame(int fd, uint16_t type, uint64_t seq, const char *token,
                             const unsigned char *payload, uint32_t payload_len)
{
    unsigned char stack_buf[PHIM_FLOWBUS_STACK_FRAME];
    unsigned char *buf = stack_buf;
    bool on_heap = false;
    size_t total = 0u;
    size_t len = 0u;
    int rc;

    if (phim_flowbus_frame_total_len(payload_len, &total) != PHIM_OK) {
        return PHIM_ERR;
    }
    if (total > sizeof(stack_buf)) {
        buf = phim_xmalloc(total);
        on_heap = true;
    }
    if (phim_flowbus_fill_frame(type, seq, token, payload, payload_len, buf, total, &len) != PHIM_OK) {
        if (on_heap) {
            free(buf);
        }
        return PHIM_ERR;
    }
    rc = phim_write_all(fd, buf, len);
    if (on_heap) {
        free(buf);
    }
    return rc == 0 ? PHIM_OK : PHIM_ERR;
}

int phim_flowbus_read_frame(int fd, const char *token, uint32_t timeout_ms,
                            phim_flowbus_frame_t *out)
{
    unsigned char header[PHIM_FLOWBUS_HEADER_LEN];
    uint32_t payload_len;
    size_t total;
    unsigned char *buf;
    int rc;
    if (phim_read_exact_timeout(fd, header, sizeof(header), timeout_ms) != 0) {
        return PHIM_ERR;
    }
    if (memcmp(header, PHIM_FLOWBUS_MAGIC, 4u) != 0) {
        return PHIM_ERR;
    }
    payload_len = phim_get_u32(header + 40u);
    if (payload_len > PHIM_FLOWBUS_MAX_PAYLOAD) {
        return PHIM_ERR;
    }
    total = PHIM_FLOWBUS_HEADER_LEN + (size_t)payload_len + PHIM_FLOWBUS_HMAC_LEN;
    buf = phim_xmalloc(total);
    memcpy(buf, header, sizeof(header));
    if (phim_read_exact_timeout(fd, buf + PHIM_FLOWBUS_HEADER_LEN, (size_t)payload_len + PHIM_FLOWBUS_HMAC_LEN, timeout_ms) != 0) {
        free(buf);
        return PHIM_ERR;
    }
    rc = phim_flowbus_parse_frame(buf, total, token, out);
    free(buf);
    return rc;
}

int phim_flowbus_client_init(phim_flowbus_client_t *client, const char *host,
                             uint16_t port, const char *token, uint32_t timeout_ms,
                             uint32_t connect_timeout_ms, uint32_t read_timeout_ms,
                             bool keepalive)
{
    if (client == NULL || host == NULL || token == NULL) {
        return PHIM_ERR;
    }
    memset(client, 0, sizeof(*client));
    client->fd = -1;
    client->seq = 1u;
    if (phim_copy_cstr(client->host, sizeof(client->host), host) != 0 ||
        phim_copy_cstr(client->token, sizeof(client->token), token) != 0) {
        return PHIM_ERR;
    }
    client->port = port;
    client->timeout_ms = timeout_ms;
    client->connect_timeout_ms = connect_timeout_ms > 0u ? connect_timeout_ms : timeout_ms;
    client->read_timeout_ms = read_timeout_ms > 0u ? read_timeout_ms : timeout_ms;
    client->keepalive = keepalive;
    if (pthread_mutex_init(&client->lock, NULL) != 0) {
        return PHIM_ERR;
    }
    client->initialized = true;
    return PHIM_OK;
}

void phim_flowbus_client_close(phim_flowbus_client_t *client)
{
    if (client == NULL || !client->initialized) {
        return;
    }
    pthread_mutex_lock(&client->lock);
    phim_close_fd(&client->fd);
    pthread_mutex_unlock(&client->lock);
    pthread_mutex_destroy(&client->lock);
    client->initialized = false;
}

static int phim_flowbus_client_connect_locked(phim_flowbus_client_t *client)
{
    if (client->fd >= 0) {
        return PHIM_OK;
    }
    client->fd = phim_tcp_connect(client->host, client->port, client->connect_timeout_ms);
    if (client->fd < 0) {
        return PHIM_ERR;
    }
    (void)phim_set_tcp_nodelay(client->fd);
    return PHIM_OK;
}

static int phim_flowbus_client_request_once_locked(phim_flowbus_client_t *client,
                                                    const unsigned char *payload, uint32_t payload_len,
                                                    unsigned char **response, uint32_t *response_len)
{
    phim_flowbus_frame_t frame;
    uint64_t seq;
    int rc = PHIM_ERR;

    if (phim_flowbus_client_connect_locked(client) != PHIM_OK) {
        return PHIM_ERR;
    }

    seq = client->seq++;
    if (phim_flowbus_write_frame(client->fd, PHIM_FB_VERDICT_REQUEST, seq, client->token, payload, payload_len) != PHIM_OK) {
        phim_close_fd(&client->fd);
        return PHIM_ERR;
    }

    phim_flowbus_frame_init(&frame);
    if (phim_flowbus_read_frame(client->fd, client->token, client->read_timeout_ms, &frame) != PHIM_OK) {
        phim_close_fd(&client->fd);
        return PHIM_ERR;
    }

    if (frame.type == PHIM_FB_VERDICT_RESPONSE && frame.payload_len > 0u) {
        *response = phim_xmalloc((size_t)frame.payload_len + 1u);
        memcpy(*response, frame.payload, frame.payload_len);
        (*response)[frame.payload_len] = 0u;
        *response_len = frame.payload_len;
        rc = PHIM_OK;
    }
    phim_flowbus_frame_free(&frame);
    if (!client->keepalive) {
        phim_close_fd(&client->fd);
    }
    return rc;
}

int phim_flowbus_client_request(phim_flowbus_client_t *client,
                                const unsigned char *payload, uint32_t payload_len,
                                unsigned char **response, uint32_t *response_len)
{
    unsigned int attempt;
    unsigned int attempts;
    int rc = PHIM_ERR;

    if (client == NULL || response == NULL || response_len == NULL || !client->initialized) {
        return PHIM_ERR;
    }
    *response = NULL;
    *response_len = 0u;

    pthread_mutex_lock(&client->lock);
    attempts = client->keepalive ? 2u : 1u;
    for (attempt = 0u; attempt < attempts; attempt++) {
        rc = phim_flowbus_client_request_once_locked(client, payload, payload_len, response, response_len);
        if (rc == PHIM_OK) {
            break;
        }
        free(*response);
        *response = NULL;
        *response_len = 0u;
        phim_close_fd(&client->fd);
    }
    if (!client->keepalive) {
        phim_close_fd(&client->fd);
    }
    pthread_mutex_unlock(&client->lock);
    return rc;
}


static int phim_json_get_string(const unsigned char *json, size_t len, const char *key, char *dst, size_t dst_len)
{
    char needle[128];
    const char *start;
    const char *p;
    size_t key_len;
    if (dst == NULL || dst_len == 0u) {
        return PHIM_ERR;
    }
    dst[0] = '\0';
    key_len = strlen(key);
    if (key_len + 4u >= sizeof(needle)) {
        return PHIM_ERR;
    }
    (void)snprintf(needle, sizeof(needle), "\"%s\"", key);
    start = phim_strcasestr_ascii((const char *)json, needle);
    if (start == NULL || (size_t)(start - (const char *)json) >= len) {
        return PHIM_ERR;
    }
    p = strchr(start + strlen(needle), ':');
    if (p == NULL) {
        return PHIM_ERR;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '"') {
        return PHIM_ERR;
    }
    p++;
    size_t i = 0u;
    while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
        if (i + 1u >= dst_len) {
            return PHIM_ERR;
        }
        dst[i++] = *p++;
    }
    dst[i] = '\0';
    return PHIM_OK;
}

static uint32_t phim_json_get_u32(const unsigned char *json, const char *key, uint32_t fallback)
{
    char needle[128];
    const char *start;
    const char *p;
    (void)snprintf(needle, sizeof(needle), "\"%s\"", key);
    start = phim_strcasestr_ascii((const char *)json, needle);
    if (start == NULL) {
        return fallback;
    }
    p = strchr(start + strlen(needle), ':');
    if (p == NULL) {
        return fallback;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return (uint32_t)strtoul(p, NULL, 10);
}

int phim_verdict_from_json(const unsigned char *json, size_t len, phim_verdict_t *verdict)
{
    char action[32];
    if (json == NULL || verdict == NULL) {
        return PHIM_ERR;
    }
    memset(verdict, 0, sizeof(*verdict));
    verdict->action = PHIM_VERDICT_ERROR;
    if (phim_json_get_string(json, len, "action", action, sizeof(action)) != PHIM_OK) {
        return PHIM_ERR;
    }
    if (phim_strcasecmp_ascii(action, "allow") == 0) {
        verdict->action = PHIM_VERDICT_ALLOW;
    } else if (phim_strcasecmp_ascii(action, "block") == 0) {
        verdict->action = PHIM_VERDICT_BLOCK;
    } else if (phim_strcasecmp_ascii(action, "pending") == 0) {
        verdict->action = PHIM_VERDICT_PENDING;
    } else if (phim_strcasecmp_ascii(action, "error") == 0) {
        verdict->action = PHIM_VERDICT_ERROR;
    } else {
        verdict->action = PHIM_VERDICT_ERROR;
    }
    if (phim_json_get_string(json, len, "reason", verdict->reason, sizeof(verdict->reason)) != PHIM_OK) {
        (void)phim_copy_cstr(verdict->reason, sizeof(verdict->reason), "no-reason");
    }
    verdict->sid = phim_json_get_u32(json, "sid", 0u);
    if (phim_json_get_string(json, len, "msg", verdict->rule_msg, sizeof(verdict->rule_msg)) != PHIM_OK) {
        verdict->rule_msg[0] = '\0';
    }
    verdict->latency_us = (uint64_t)phim_json_get_u32(json, "latency_us", 0u);
    return PHIM_OK;
}

int phim_verdict_to_json(const phim_verdict_t *verdict, const char *flow_id,
                         char *dst, size_t dst_len)
{
    const char *action;
    int n;
    if (verdict == NULL || flow_id == NULL || dst == NULL) {
        return PHIM_ERR;
    }
    if (verdict->action == PHIM_VERDICT_BLOCK) {
        action = "block";
    } else if (verdict->action == PHIM_VERDICT_ALLOW) {
        action = "allow";
    } else if (verdict->action == PHIM_VERDICT_PENDING) {
        action = "pending";
    } else {
        action = "error";
    }
    n = snprintf(dst, dst_len,
                 "{\"schema\":\"phimantec.verdict.response.v1\",\"flow_id\":\"%s\",\"action\":\"%s\",\"reason\":\"%s\",\"source\":\"phimantec-inspectord\",\"latency_us\":%llu,\"rule\":{\"sid\":%u,\"msg\":\"%s\"}}",
                 flow_id, action, verdict->reason,
                 (unsigned long long)verdict->latency_us,
                 (unsigned int)verdict->sid, verdict->rule_msg);
    if (n < 0 || (size_t)n >= dst_len) {
        return PHIM_ERR;
    }
    return PHIM_OK;
}
