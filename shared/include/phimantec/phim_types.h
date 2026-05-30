#ifndef PHIM_TYPES_H
#define PHIM_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PHIM_MAX_HOST 255u
#define PHIM_MAX_PATH 2048u
#define PHIM_MAX_METHOD 32u
#define PHIM_MAX_HEADER_BLOCK 65536u
#define PHIM_MAX_FLOW_ID 64u
#define PHIM_MAX_REASON 256u
#define PHIM_MAX_RULE_MSG 256u
#define PHIM_MAX_SAMPLE 65536u
#define PHIM_TOKEN_MAX 256u
#define PHIM_DEFAULT_LISTENER_PORT 3128
#define PHIM_DEFAULT_LANE_PORT 18080
#define PHIM_DEFAULT_FLOWBUS_PORT 18090

#define PHIM_OK 0
#define PHIM_ERR -1
#define PHIM_INCOMPLETE 1

typedef enum phim_bool_policy_e {
    PHIM_POLICY_FAIL_OPEN = 0,
    PHIM_POLICY_FAIL_CLOSED = 1
} phim_bool_policy_t;

typedef enum phim_verdict_action_e {
    PHIM_VERDICT_ALLOW = 0,
    PHIM_VERDICT_BLOCK = 1,
    PHIM_VERDICT_ERROR = 2,
    PHIM_VERDICT_PENDING = 3
} phim_verdict_action_t;

typedef struct phim_endpoint_s {
    char host[PHIM_MAX_HOST + 1u];
    uint16_t port;
} phim_endpoint_t;

typedef struct phim_connect_request_s {
    char host[PHIM_MAX_HOST + 1u];
    uint16_t port;
    size_t header_len;
    bool keep_alive;
} phim_connect_request_t;

typedef struct phim_h1_request_s {
    char method[PHIM_MAX_METHOD];
    char target[PHIM_MAX_PATH];
    char host[PHIM_MAX_HOST + 1u];
    size_t header_len;
    size_t content_length;
    bool has_content_length;
    bool chunked;
    bool connection_close;
} phim_h1_request_t;

typedef struct phim_h1_response_s {
    int status_code;
    size_t header_len;
    size_t content_length;
    bool has_content_length;
    bool chunked;
    bool connection_close;
} phim_h1_response_t;

typedef struct phim_verdict_s {
    phim_verdict_action_t action;
    char reason[PHIM_MAX_REASON];
    uint32_t sid;
    char rule_msg[PHIM_MAX_RULE_MSG];
    uint64_t latency_us;
} phim_verdict_t;

typedef struct phim_request_meta_s {
    char flow_id[PHIM_MAX_FLOW_ID];
    char direction[16];
    char phase[32];
    char client_ip[64];
    uint16_t client_port;
    char server_host[PHIM_MAX_HOST + 1u];
    uint16_t server_port;
    char method[PHIM_MAX_METHOD];
    char path[PHIM_MAX_PATH];
    char alpn[32];
    const unsigned char *sample;
    size_t sample_len;
} phim_request_meta_t;

#endif
