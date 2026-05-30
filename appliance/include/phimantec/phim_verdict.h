#ifndef PHIM_VERDICT_H
#define PHIM_VERDICT_H

#include "phimantec/phim_config.h"
#include "phimantec/phim_flowbus.h"
#include "phimantec/phim_types.h"

#define PHIM_VERDICT_FLOWBUS_CLIENTS 32u

typedef struct phim_verdict_engine_s {
    phim_config_t cfg;
    phim_flowbus_client_t flowbus[PHIM_VERDICT_FLOWBUS_CLIENTS];
    size_t flowbus_count;
    size_t flowbus_next;
    pthread_mutex_t flowbus_select_lock;
    bool selector_initialized;
} phim_verdict_engine_t;

int phim_verdict_engine_init(phim_verdict_engine_t *engine, const phim_config_t *cfg);
void phim_verdict_engine_close(phim_verdict_engine_t *engine);
int phim_verdict_check(phim_verdict_engine_t *engine, const phim_request_meta_t *meta,
                       phim_verdict_t *verdict);
int phim_verdict_check_with_timeout(phim_verdict_engine_t *engine, const phim_request_meta_t *meta,
                                    phim_verdict_t *verdict, uint32_t timeout_ms);

#endif
