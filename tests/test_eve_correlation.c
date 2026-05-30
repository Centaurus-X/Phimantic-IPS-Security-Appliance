#include "phimantec/phim_eve.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "check failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); return 1; } } while (0)

int main(void)
{
    char path[256];
    FILE *f;
    uint32_t sid = 0u;
    char msg[256];
    phim_eve_flow_status_t status;
    const char *flow_header = "11111111-2222-3333-4444-555555555555";
    const char *flow_eicar = "c215eabc-1555-75ba-a326-3ea5865c85bf";
    const char *flow_path = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
    const char *flow_suffix = "01234567-89ab-cdef-0123-456789abcdef-0001";
    const char *flow_multi_suffix = "01234567-89ab-cdef-0123-456789abcdef-0001-90000";
    const char *flow_http = "bbbbbbbb-1111-2222-3333-cccccccccccc";
    const char *flow_partial = "dddddddd-1111-2222-3333-eeeeeeeeeeee";

    (void)snprintf(path, sizeof(path), "/tmp/phim-eve-test-%ld.json", (long)getpid());
    f = fopen(path, "w");
    CHECK_TRUE(f != NULL);
    (void)fprintf(f,
        "{\"event_type\":\"alert\",\"payload_printable\":\"X-Phim-Flow-Id: %s\",\"alert\":{\"signature_id\":900001,\"signature\":\"PHIMANTEC TEST BLOCK\"}}\n",
        flow_header);
    (void)fclose(f);
    CHECK_TRUE(phim_eve_find_alert_for_flow(path, flow_header, &sid, msg, sizeof(msg)) == PHIM_OK);
    CHECK_TRUE(sid == 900001u);
    CHECK_TRUE(strcmp(msg, "PHIMANTEC TEST BLOCK") == 0);

    f = fopen(path, "a");
    CHECK_TRUE(f != NULL);
    (void)fprintf(f,
        "{\"event_type\":\"alert\",\"payload_printable\":\"X-Phim-Flow-Id: %sTP/1.1\",\"alert\":{\"signature_id\":900002,\"signature\":\"PHIMANTEC EICAR TEST FILE\"}}\n",
        flow_eicar);
    (void)fclose(f);
    CHECK_TRUE(phim_eve_find_alert_for_flow(path, flow_eicar, &sid, msg, sizeof(msg)) == PHIM_OK);
    CHECK_TRUE(sid == 900002u);
    CHECK_TRUE(strcmp(msg, "PHIMANTEC EICAR TEST FILE") == 0);

    f = fopen(path, "a");
    CHECK_TRUE(f != NULL);
    (void)fprintf(f,
        "{\"event_type\":\"alert\",\"http\":{\"url\":\"/phimantec/lane/response/%s\"},\"payload_printable\":\"EICAR-STANDARD-ANTIVIRUS-TEST-FILE\",\"alert\":{\"signature_id\":900002,\"signature\":\"PHIMANTEC EICAR TEST FILE\"}}\n",
        flow_path);
    (void)fclose(f);
    CHECK_TRUE(phim_eve_find_alert_for_flow(path, flow_path, &sid, msg, sizeof(msg)) == PHIM_OK);
    CHECK_TRUE(sid == 900002u);
    CHECK_TRUE(strcmp(msg, "PHIMANTEC EICAR TEST FILE") == 0);

    f = fopen(path, "a");
    CHECK_TRUE(f != NULL);
    (void)fprintf(f,
        "{\"event_type\":\"alert\",\"http\":{\"url\":\"\\/phimantec\\/lane\\/response\\/%sTP\\/1.1\"},\"alert\":{\"signature_id\":900003,\"signature\":\"PHIMANTEC SURICATA PATH BLOCK\"}}\n",
        flow_suffix);
    (void)fclose(f);
    CHECK_TRUE(phim_eve_find_alert_for_flow(path, flow_suffix, &sid, msg, sizeof(msg)) == PHIM_OK);
    CHECK_TRUE(sid == 900003u);
    CHECK_TRUE(strcmp(msg, "PHIMANTEC SURICATA PATH BLOCK") == 0);

    f = fopen(path, "a");
    CHECK_TRUE(f != NULL);
    (void)fprintf(f,
        "{\"event_type\":\"alert\",\"http\":{\"url\":\"\\/phimantec\\/lane\\/response\\/%sTP\\/1.1\"},\"alert\":{\"signature_id\":900005,\"signature\":\"PHIMANTEC SEGMENT BLOCK\"}}\n",
        flow_multi_suffix);
    (void)fclose(f);
    CHECK_TRUE(phim_eve_find_alert_for_flow(path, flow_multi_suffix, &sid, msg, sizeof(msg)) == PHIM_OK);
    CHECK_TRUE(sid == 900005u);
    CHECK_TRUE(strcmp(msg, "PHIMANTEC SEGMENT BLOCK") == 0);

    f = fopen(path, "a");
    CHECK_TRUE(f != NULL);
    (void)fprintf(f,
        "{\"event_type\":\"http\",\"http\":{\"url\":\"/phimantec/lane/response/%s\"}}\n",
        flow_http);
    (void)fclose(f);
    CHECK_TRUE(phim_eve_get_flow_status(path, flow_http, &status) == PHIM_OK);
    CHECK_TRUE(status.state == PHIM_EVE_FLOW_SEEN_NO_ALERT);

    f = fopen(path, "a");
    CHECK_TRUE(f != NULL);
    (void)fprintf(f,
        "{\"event_type\":\"alert\",\"payload_printable\":\"X-Phim-Flow-Id: %s\",\"alert\":{\"signature_id\":900004,",
        flow_partial);
    (void)fclose(f);
    CHECK_TRUE(phim_eve_find_alert_for_flow(path, flow_partial, &sid, msg, sizeof(msg)) != PHIM_OK);

    f = fopen(path, "a");
    CHECK_TRUE(f != NULL);
    (void)fprintf(f, "\"signature\":\"PHIMANTEC PARTIAL ALERT\"}}\n");
    (void)fclose(f);
    CHECK_TRUE(phim_eve_find_alert_for_flow(path, flow_partial, &sid, msg, sizeof(msg)) == PHIM_OK);
    CHECK_TRUE(sid == 900004u);
    CHECK_TRUE(strcmp(msg, "PHIMANTEC PARTIAL ALERT") == 0);

    f = fopen(path, "a");
    CHECK_TRUE(f != NULL);
    (void)fprintf(f,
        "{\"event_type\":\"http\",\"http\":{\"url\":\"/phimantec/lane/response/%s\"}}\n",
        flow_partial);
    (void)fclose(f);
    CHECK_TRUE(phim_eve_get_flow_status(path, flow_partial, &status) == PHIM_OK);
    CHECK_TRUE(status.state == PHIM_EVE_FLOW_ALERT);
    CHECK_TRUE(status.sid == 900004u);

    (void)unlink(path);
    return 0;
}
