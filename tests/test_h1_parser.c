#include "phimantec/phim_h1.h"

#include <stdio.h>
#include <string.h>

#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "check failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); return 1; } } while (0)

static int contains_bytes(const unsigned char *buf, size_t len, const char *needle)
{
    size_t needle_len = strlen(needle);
    size_t i;
    if (len < needle_len) {
        return 0;
    }
    for (i = 0u; i + needle_len <= len; i++) {
        if (memcmp(buf + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    const unsigned char req[] = "GET /path?q=1 HTTP/1.1\r\nHost: example.test\r\nContent-Length: 5\r\n\r\nhello";
    phim_h1_request_t r;
    const unsigned char resp[] = "HTTP/1.1 200 OK\r\nAlt-Svc: h3=\":443\"\r\nContent-Length: 5\r\n\r\nhello";
    phim_h1_response_t parsed_resp;
    unsigned char out[512];
    size_t out_len = 0u;
    CHECK_TRUE(phim_h1_parse_request(req, strlen((const char *)req), &r) == PHIM_OK);
    CHECK_TRUE(strcmp(r.method, "GET") == 0);
    CHECK_TRUE(strcmp(r.target, "/path?q=1") == 0);
    CHECK_TRUE(strcmp(r.host, "example.test") == 0);
    CHECK_TRUE(r.content_length == 5u);
    CHECK_TRUE(phim_h1_parse_response(resp, strlen((const char *)resp), &parsed_resp) == PHIM_OK);
    CHECK_TRUE(phim_h1_strip_alt_svc(resp, parsed_resp.header_len, out, sizeof(out), &out_len) == PHIM_OK);
    CHECK_TRUE(contains_bytes(out, out_len, "Alt-Svc") == 0);
    CHECK_TRUE(contains_bytes(out, out_len, "Alt-Used") == 0);
    CHECK_TRUE(contains_bytes(out, out_len, "HTTP2-Settings") == 0);
    CHECK_TRUE(contains_bytes(out, out_len, "Upgrade:") == 0);
    CHECK_TRUE(contains_bytes(out, out_len, "Connection:") == 0);
    CHECK_TRUE(contains_bytes(out, out_len, "Content-Length: 5") == 1);
    return 0;
}
