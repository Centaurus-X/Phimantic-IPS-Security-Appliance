#ifndef PHIM_FLOWBUS_H
#define PHIM_FLOWBUS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "phimantec/phim_types.h"

#define PHIM_FLOWBUS_MAGIC "PHFB"
#define PHIM_FLOWBUS_VERSION 1u
#define PHIM_FLOWBUS_NONCE_LEN 16u
#define PHIM_FLOWBUS_HMAC_LEN 32u
#define PHIM_FLOWBUS_HEADER_LEN 44u
#define PHIM_FLOWBUS_MAX_PAYLOAD (1024u * 1024u)
#define PHIM_FLOWBUS_MAX_FRAME (PHIM_FLOWBUS_HEADER_LEN + PHIM_FLOWBUS_MAX_PAYLOAD + PHIM_FLOWBUS_HMAC_LEN)

typedef enum phim_flowbus_type_e {
    PHIM_FB_HELLO = 1,
    PHIM_FB_HEARTBEAT = 2,
    PHIM_FB_VERDICT_REQUEST = 10,
    PHIM_FB_VERDICT_RESPONSE = 11,
    PHIM_FB_ERROR = 99
} phim_flowbus_type_t;

typedef struct phim_flowbus_frame_s {
    uint8_t version;
    uint8_t flags;
    uint16_t type;
    uint64_t seq;
    uint64_t timestamp_ms;
    unsigned char nonce[PHIM_FLOWBUS_NONCE_LEN];
    uint32_t payload_len;
    unsigned char *payload;
} phim_flowbus_frame_t;

typedef struct phim_flowbus_client_s {
    int fd;
    uint64_t seq;
    char host[PHIM_MAX_HOST + 1u];
    uint16_t port;
    char token[PHIM_TOKEN_MAX];
    uint32_t timeout_ms;
    uint32_t connect_timeout_ms;
    uint32_t read_timeout_ms;
    bool keepalive;
    pthread_mutex_t lock;
    bool initialized;
} phim_flowbus_client_t;

void phim_flowbus_frame_init(phim_flowbus_frame_t *frame);
void phim_flowbus_frame_free(phim_flowbus_frame_t *frame);
int phim_flowbus_build_frame(uint16_t type, uint64_t seq, const char *token,
                             const unsigned char *payload, uint32_t payload_len,
                             unsigned char **out, size_t *out_len);
int phim_flowbus_parse_frame(const unsigned char *buf, size_t len, const char *token,
                             phim_flowbus_frame_t *out);
int phim_flowbus_write_frame(int fd, uint16_t type, uint64_t seq, const char *token,
                             const unsigned char *payload, uint32_t payload_len);
int phim_flowbus_read_frame(int fd, const char *token, uint32_t timeout_ms,
                            phim_flowbus_frame_t *out);

int phim_flowbus_client_init(phim_flowbus_client_t *client, const char *host,
                             uint16_t port, const char *token, uint32_t timeout_ms,
                             uint32_t connect_timeout_ms, uint32_t read_timeout_ms,
                             bool keepalive);
void phim_flowbus_client_close(phim_flowbus_client_t *client);
int phim_flowbus_client_request(phim_flowbus_client_t *client,
                                const unsigned char *payload, uint32_t payload_len,
                                unsigned char **response, uint32_t *response_len);

int phim_verdict_from_json(const unsigned char *json, size_t len, phim_verdict_t *verdict);
int phim_verdict_to_json(const phim_verdict_t *verdict, const char *flow_id,
                         char *dst, size_t dst_len);

#endif
