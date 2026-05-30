#include "phimantec/phim_verdict.h"

#include <stdio.h>
#include <string.h>

#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "check failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); return 1; } } while (0)

int main(void)
{
    static const unsigned char eicar_sample[] =
        "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*";
    phim_config_t cfg;
    phim_verdict_engine_t engine;
    phim_request_meta_t meta;
    phim_verdict_t verdict;
    phim_config_set_defaults(&cfg);

    /* EICAR- oder Testmarker duerfen im Proxy selbst keine Blockentscheidung
     * mehr ausloesen. Nur ein Suricata-EVE-Alert ueber den FlowBus darf blocken.
     */
    cfg.flowbus_enabled = false;
    cfg.fail_policy = PHIM_POLICY_FAIL_CLOSED;

    CHECK_TRUE(phim_verdict_engine_init(&engine, &cfg) == PHIM_OK);
    memset(&meta, 0, sizeof(meta));
    (void)strcpy(meta.flow_id, "unit-suricata-only-flow");
    (void)strcpy(meta.direction, "response");
    (void)strcpy(meta.phase, "body");
    (void)strcpy(meta.server_host, "example.test");
    (void)strcpy(meta.method, "GET");
    (void)strcpy(meta.path, "/PHIMANTEC-SURICATA-BLOCK-TEST/eicar.com");
    meta.sample = eicar_sample;
    meta.sample_len = sizeof(eicar_sample) - 1u;

    CHECK_TRUE(phim_verdict_check(&engine, &meta, &verdict) == PHIM_OK);
    CHECK_TRUE(verdict.action == PHIM_VERDICT_ERROR);
    CHECK_TRUE(verdict.sid == 0u);
    CHECK_TRUE(strcmp(verdict.reason, "suricata-flowbus-disabled") == 0);
    phim_verdict_engine_close(&engine);
    return 0;
}
