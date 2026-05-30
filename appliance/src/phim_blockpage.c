#include "phimantec/phim_blockpage.h"
#include "phimantec/phim_string.h"

#include <stdio.h>
#include <string.h>

int phim_blockpage_build(const phim_verdict_t *verdict, const char *flow_id,
                         unsigned char *dst, size_t dst_cap, size_t *dst_len)
{
    char reason[512];
    char rule[512];
    char body[2048];
    int body_len;
    int n;
    if (verdict == NULL || flow_id == NULL || dst == NULL || dst_len == NULL) {
        return PHIM_ERR;
    }
    if (phim_json_escape((const unsigned char *)verdict->reason, strlen(verdict->reason), reason, sizeof(reason)) != 0) {
        (void)phim_copy_cstr(reason, sizeof(reason), "blocked");
    }
    if (phim_json_escape((const unsigned char *)verdict->rule_msg, strlen(verdict->rule_msg), rule, sizeof(rule)) != 0) {
        rule[0] = '\0';
    }
    body_len = snprintf(body, sizeof(body),
                        "<!doctype html><html><head><meta charset=\"utf-8\"><title>PHIMANTEC Block</title>"
                        "<style>body{font-family:sans-serif;margin:3rem;line-height:1.45}code{background:#eee;padding:.2rem}</style>"
                        "</head><body><h1>Zugriff blockiert</h1>"
                        "<p>Der entschluesselte Traffic wurde durch eine Suricata-EVE-Alert-Entscheidung blockiert.</p>"
                        "<p><b>Grund:</b> %s</p><p><b>Regel:</b> %u %s</p><p><b>Flow-ID:</b> <code>%s</code></p>"
                        "</body></html>",
                        reason, (unsigned int)verdict->sid, rule, flow_id);
    if (body_len < 0 || (size_t)body_len >= sizeof(body)) {
        return PHIM_ERR;
    }
    n = snprintf((char *)dst, dst_cap,
                 "HTTP/1.1 403 Forbidden\r\n"
                 "Content-Type: text/html; charset=utf-8\r\n"
                 "Cache-Control: no-store\r\n"
                 "Connection: close\r\n"
                 "Content-Length: %d\r\n\r\n%s",
                 body_len, body);
    if (n < 0 || (size_t)n >= dst_cap) {
        return PHIM_ERR;
    }
    *dst_len = (size_t)n;
    return PHIM_OK;
}
