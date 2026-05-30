#include "phimantec/phim_lane.h"
#include "phimantec/phim_log.h"
#include "phimantec/phim_socket.h"
#include "phimantec/phim_string.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PHIM_LANE_POOL_CLIENTS 32u
#define PHIM_LANE_ACK_BUF 1024u

typedef struct phim_lane_client_s {
    int fd;
    char host[PHIM_MAX_HOST + 1u];
    uint16_t port;
    uint32_t timeout_ms;
    pthread_mutex_t lock;
} phim_lane_client_t;

static pthread_once_t g_lane_pool_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lane_select_lock = PTHREAD_MUTEX_INITIALIZER;
static phim_lane_client_t g_lane_clients[PHIM_LANE_POOL_CLIENTS];
static size_t g_lane_next;

static void phim_lane_pool_init_once(void)
{
    size_t i;
    for (i = 0u; i < PHIM_LANE_POOL_CLIENTS; i++) {
        g_lane_clients[i].fd = -1;
        g_lane_clients[i].port = 0u;
        g_lane_clients[i].timeout_ms = 0u;
        g_lane_clients[i].host[0] = '\0';
        (void)pthread_mutex_init(&g_lane_clients[i].lock, NULL);
    }
}

static size_t phim_lane_pool_size(const phim_config_t *cfg)
{
    size_t pool_size;
    if (cfg == NULL || cfg->lane_pool_size == 0u) {
        return 1u;
    }
    pool_size = cfg->lane_pool_size;
    if (pool_size > PHIM_LANE_POOL_CLIENTS) {
        pool_size = PHIM_LANE_POOL_CLIENTS;
    }
    return pool_size;
}

static phim_lane_client_t *phim_lane_select_client(const phim_config_t *cfg)
{
    size_t idx;
    size_t pool_size;
    (void)pthread_once(&g_lane_pool_once, phim_lane_pool_init_once);
    pool_size = phim_lane_pool_size(cfg);
    pthread_mutex_lock(&g_lane_select_lock);
    idx = g_lane_next % pool_size;
    g_lane_next++;
    pthread_mutex_unlock(&g_lane_select_lock);
    return &g_lane_clients[idx];
}

static void phim_lane_client_close_locked(phim_lane_client_t *client)
{
    if (client != NULL) {
        phim_close_fd(&client->fd);
    }
}

static bool phim_lane_client_matches_cfg(const phim_lane_client_t *client, const phim_config_t *cfg)
{
    if (client == NULL || cfg == NULL) {
        return false;
    }
    return client->port == cfg->lane_port &&
           client->timeout_ms == cfg->connect_timeout_ms &&
           strcmp(client->host, cfg->lane_host) == 0;
}

static int phim_lane_client_prepare_locked(phim_lane_client_t *client, const phim_config_t *cfg)
{
    if (client == NULL || cfg == NULL) {
        return PHIM_ERR;
    }
    if (!phim_lane_client_matches_cfg(client, cfg)) {
        phim_lane_client_close_locked(client);
        (void)phim_copy_cstr(client->host, sizeof(client->host), cfg->lane_host);
        client->port = cfg->lane_port;
        client->timeout_ms = cfg->connect_timeout_ms;
    }
    if (client->fd >= 0) {
        return PHIM_OK;
    }
    client->fd = phim_tcp_connect(client->host, client->port, client->timeout_ms);
    if (client->fd < 0) {
        return PHIM_ERR;
    }
    (void)phim_set_tcp_nodelay(client->fd);
    return PHIM_OK;
}

static int phim_lane_status_code(const unsigned char *response)
{
    if (response == NULL || memcmp(response, "HTTP/1.", 7u) != 0) {
        return 0;
    }
    return atoi((const char *)response + 9);
}

static bool phim_lane_ack_wants_close(const unsigned char *response)
{
    if (response == NULL) {
        return true;
    }
    return phim_strcasestr_ascii((const char *)response, "Connection: close") != NULL;
}

static int phim_lane_read_ack_locked(phim_lane_client_t *client)
{
    unsigned char response[PHIM_LANE_ACK_BUF];
    ssize_t n;
    int status_code;

    memset(response, 0, sizeof(response));
    n = phim_read_until(client->fd, response, sizeof(response) - 1u, "\r\n\r\n", client->timeout_ms);
    if (n <= 0) {
        phim_lane_client_close_locked(client);
        return PHIM_ERR;
    }
    response[(size_t)n] = '\0';
    status_code = phim_lane_status_code(response);
    if (phim_lane_ack_wants_close(response)) {
        phim_lane_client_close_locked(client);
    }
    if (status_code < 200 || status_code >= 300) {
        return PHIM_ERR;
    }
    return PHIM_OK;
}

static int phim_lane_send_locked(phim_lane_client_t *client, const phim_config_t *cfg,
                                 const char *header, size_t header_len,
                                 const unsigned char *body, size_t body_len)
{
    if (phim_lane_client_prepare_locked(client, cfg) != PHIM_OK) {
        return PHIM_ERR;
    }
    if (phim_write_all(client->fd, header, header_len) != 0) {
        phim_lane_client_close_locked(client);
        return PHIM_ERR;
    }
    if (body_len > 0u && body != NULL) {
        if (phim_write_all(client->fd, body, body_len) != 0) {
            phim_lane_client_close_locked(client);
            return PHIM_ERR;
        }
    }
    return phim_lane_read_ack_locked(client);
}

int phim_lane_send(const phim_config_t *cfg, const phim_request_meta_t *meta)
{
    phim_lane_client_t *client;
    char header[4096];
    char safe_path[PHIM_MAX_PATH * 2u];
    int n;
    int rc;

    if (cfg == NULL || meta == NULL || !cfg->lane_enabled) {
        return PHIM_OK;
    }
    if (phim_json_escape((const unsigned char *)meta->path, strlen(meta->path), safe_path, sizeof(safe_path)) != 0) {
        (void)phim_copy_cstr(safe_path, sizeof(safe_path), "/");
    }
    n = snprintf(header, sizeof(header),
                 "POST /phimantec/lane/%s/%s HTTP/1.1\r\n"
                 "Host: phimantec-lane.local\r\n"
                 "X-Phim-Flow-Id: %s\r\n"
                 "X-Phim-Direction: %s\r\n"
                 "X-Phim-Phase: %s\r\n"
                 "X-Phim-Client-IP: %s\r\n"
                 "X-Phim-Client-Port: %u\r\n"
                 "X-Phim-Server-SNI: %s\r\n"
                 "X-Phim-Server-Port: %u\r\n"
                 "X-Phim-ALPN: %s\r\n"
                 "X-Phim-HTTP-Version: h1\r\n"
                 "X-Phim-Method: %s\r\n"
                 "X-Phim-Path: %s\r\n"
                 "Content-Type: application/octet-stream\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: keep-alive\r\n\r\n",
                 meta->direction, meta->flow_id, meta->flow_id, meta->direction, meta->phase,
                 meta->client_ip, (unsigned int)meta->client_port,
                 meta->server_host, (unsigned int)meta->server_port,
                 meta->alpn[0] != '\0' ? meta->alpn : "http/1.1",
                 meta->method, safe_path, meta->sample_len);
    if (n < 0 || (size_t)n >= sizeof(header)) {
        return PHIM_ERR;
    }

    client = phim_lane_select_client(cfg);
    pthread_mutex_lock(&client->lock);
    rc = phim_lane_send_locked(client, cfg, header, (size_t)n, meta->sample, meta->sample_len);
    if (rc != PHIM_OK) {
        phim_lane_client_close_locked(client);
        rc = phim_lane_send_locked(client, cfg, header, (size_t)n, meta->sample, meta->sample_len);
    }
    pthread_mutex_unlock(&client->lock);

    if (rc != PHIM_OK) {
        PHIM_LOGW("lane send failed %s:%u", cfg->lane_host, (unsigned int)cfg->lane_port);
    }
    return rc;
}
