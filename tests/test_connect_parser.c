#include "phimantec/phim_connect_parser.h"

#include <stdio.h>
#include <string.h>

#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "check failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); return 1; } } while (0)

int main(void)
{
    const unsigned char req[] = "CONNECT example.test:443 HTTP/1.1\r\nHost: example.test:443\r\nProxy-Connection: keep-alive\r\n\r\n";
    phim_connect_request_t out;
    CHECK_TRUE(phim_parse_connect_request(req, strlen((const char *)req), &out) == PHIM_OK);
    CHECK_TRUE(strcmp(out.host, "example.test") == 0);
    CHECK_TRUE(out.port == 443u);
    CHECK_TRUE(out.keep_alive);
    return 0;
}
