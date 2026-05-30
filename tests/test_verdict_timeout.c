#include "phimantec/phim_verdict.h"

#include <stdio.h>
#include <string.h>

#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "check failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); return 1; } } while (0)

int main(void)
{
    phim_config_t cfg;
    phim_verdict_engine_t engine;
    phim_request_meta_t meta;
    phim_verdict_t verdict;
    phim_config_set_defaults(&cfg);

    /* PHIMANTEC v0.3.14: Suricata bleibt der einzige fachliche Entscheider.
     * Der Proxy darf aber ohne finalen Suricata-Allow nicht forwarden. FlowBus-
     * oder Suricata-Nichterreichbarkeit ist deshalb ein Inspection-Error und
     * kein stilles Allow mehr.
     */
    cfg.flowbus_enabled = true;
    cfg.flowbus_port = 9u;
    cfg.verdict_timeout_ms = 25u;
    cfg.connect_timeout_ms = 25u;
    cfg.fail_policy = PHIM_POLICY_FAIL_CLOSED;

    CHECK_TRUE(phim_verdict_engine_init(&engine, &cfg) == PHIM_OK);
    memset(&meta, 0, sizeof(meta));
    (void)strcpy(meta.flow_id, "unit-flow");
    (void)strcpy(meta.direction, "request");
    (void)strcpy(meta.phase, "headers");
    (void)strcpy(meta.server_host, "example.test");
    (void)strcpy(meta.method, "GET");
    (void)strcpy(meta.path, "/");

    CHECK_TRUE(phim_verdict_check(&engine, &meta, &verdict) == PHIM_OK);
    CHECK_TRUE(verdict.action == PHIM_VERDICT_ERROR);
    CHECK_TRUE(strcmp(verdict.reason, "suricata-inspection-unavailable") == 0);
    phim_verdict_engine_close(&engine);
    return 0;
}
