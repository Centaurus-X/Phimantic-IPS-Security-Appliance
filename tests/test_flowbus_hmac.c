#include "phimantec/phim_flowbus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "check failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); return 1; } } while (0)

int main(void)
{
    const char *token = "unit-test-token";
    const unsigned char payload[] = "{\"action\":\"allow\"}";
    unsigned char *frame = NULL;
    size_t frame_len = 0u;
    phim_flowbus_frame_t parsed;
    phim_verdict_t verdict;
    phim_flowbus_frame_init(&parsed);
    CHECK_TRUE(phim_flowbus_build_frame(PHIM_FB_VERDICT_RESPONSE, 7u, token, payload, (uint32_t)strlen((const char *)payload), &frame, &frame_len) == PHIM_OK);
    CHECK_TRUE(phim_flowbus_parse_frame(frame, frame_len, token, &parsed) == PHIM_OK);
    CHECK_TRUE(parsed.type == PHIM_FB_VERDICT_RESPONSE);
    CHECK_TRUE(parsed.seq == 7u);
    CHECK_TRUE(parsed.payload_len == strlen((const char *)payload));
    phim_flowbus_frame_free(&parsed);
    frame[20] ^= 0x01u;
    CHECK_TRUE(phim_flowbus_parse_frame(frame, frame_len, token, &parsed) == PHIM_ERR);
    free(frame);

    CHECK_TRUE(phim_verdict_from_json((const unsigned char *)"{\"action\":\"pending\",\"reason\":\"suricata-pending\"}",
                                      strlen("{\"action\":\"pending\",\"reason\":\"suricata-pending\"}"),
                                      &verdict) == PHIM_OK);
    CHECK_TRUE(verdict.action == PHIM_VERDICT_PENDING);
    CHECK_TRUE(phim_verdict_from_json((const unsigned char *)"{\"action\":\"error\",\"reason\":\"suricata-unavailable\"}",
                                      strlen("{\"action\":\"error\",\"reason\":\"suricata-unavailable\"}"),
                                      &verdict) == PHIM_OK);
    CHECK_TRUE(verdict.action == PHIM_VERDICT_ERROR);
    return 0;
}
