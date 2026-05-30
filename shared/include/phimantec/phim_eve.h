#ifndef PHIM_EVE_H
#define PHIM_EVE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "phimantec/phim_types.h"

typedef enum phim_eve_flow_state_e {
    PHIM_EVE_FLOW_UNAVAILABLE = 0,
    PHIM_EVE_FLOW_NOT_SEEN = 1,
    PHIM_EVE_FLOW_SEEN_NO_ALERT = 2,
    PHIM_EVE_FLOW_ALERT = 3
} phim_eve_flow_state_t;

typedef struct phim_eve_flow_status_s {
    phim_eve_flow_state_t state;
    uint32_t sid;
    char msg[PHIM_MAX_RULE_MSG];
    uint64_t seen_ms;
    bool cursor_advanced;
} phim_eve_flow_status_t;

int phim_eve_get_flow_status(const char *path, const char *flow_id,
                             phim_eve_flow_status_t *status);
int phim_eve_find_alert_for_flow(const char *path, const char *flow_id,
                                 uint32_t *sid, char *msg, size_t msg_len);

#endif
