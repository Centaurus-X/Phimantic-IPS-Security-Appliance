#include "phimantec/phim_flowbus.h"
#include "phimantec/phim_socket.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "check failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); return 1; } } while (0)

int main(void)
{
    int sv[2];
    const char *token = "unit-test-token";
    const unsigned char payload[] = "{\"flow_id\":\"flow-stack\",\"action\":\"allow\"}";
    phim_flowbus_frame_t frame;

    CHECK_TRUE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    phim_flowbus_frame_init(&frame);
    CHECK_TRUE(phim_flowbus_write_frame(sv[0], PHIM_FB_VERDICT_REQUEST, 42u, token,
                                        payload, (uint32_t)strlen((const char *)payload)) == PHIM_OK);
    CHECK_TRUE(phim_flowbus_read_frame(sv[1], token, 1000u, &frame) == PHIM_OK);
    CHECK_TRUE(frame.type == PHIM_FB_VERDICT_REQUEST);
    CHECK_TRUE(frame.seq == 42u);
    CHECK_TRUE(frame.payload_len == strlen((const char *)payload));
    CHECK_TRUE(memcmp(frame.payload, payload, frame.payload_len) == 0);
    phim_flowbus_frame_free(&frame);
    phim_close_fd(&sv[0]);
    phim_close_fd(&sv[1]);
    return 0;
}
