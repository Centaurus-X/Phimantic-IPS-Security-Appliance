#ifndef PHIM_PROXY_WORKER_H
#define PHIM_PROXY_WORKER_H

#include "phimantec/phim_cert.h"
#include "phimantec/phim_config.h"
#include "phimantec/phim_verdict.h"

typedef struct phim_proxy_runtime_s {
    phim_config_t cfg;
    phim_ca_t ca;
    phim_verdict_engine_t verdict_engine;
} phim_proxy_runtime_t;

int phim_proxy_run(const phim_config_t *cfg);

#endif
