#include "phimantec/phim_connect_parser.h"
#include "phimantec/phim_h1.h"
#include "phimantec/phim_config.h"
#include "phimantec/phim_string.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int phim_parse_connect_request(const unsigned char *buf, size_t len, phim_connect_request_t *out)
{
    size_t header_len;
    size_t line_end = 0u;
    size_t p = 0u;
    size_t authority_start;
    size_t authority_end;
    char authority[PHIM_MAX_HOST + 16u];
    char value[64];

    if (buf == NULL || out == NULL) {
        return PHIM_ERR;
    }
    memset(out, 0, sizeof(*out));
    out->port = 443u;
    out->keep_alive = true;
    if (phim_h1_find_header_end(buf, len, &header_len) != PHIM_OK) {
        return PHIM_INCOMPLETE;
    }
    out->header_len = header_len;
    while (line_end + 1u < header_len && !(buf[line_end] == '\r' && buf[line_end + 1u] == '\n')) {
        line_end++;
    }
    if (line_end < 14u || phim_strncasecmp_ascii((const char *)buf, "CONNECT ", 8u) != 0) {
        return PHIM_ERR;
    }
    p = 8u;
    while (p < line_end && buf[p] == ' ') {
        p++;
    }
    authority_start = p;
    while (p < line_end && buf[p] != ' ') {
        p++;
    }
    authority_end = p;
    if (authority_end <= authority_start || authority_end - authority_start >= sizeof(authority)) {
        return PHIM_ERR;
    }
    memcpy(authority, buf + authority_start, authority_end - authority_start);
    authority[authority_end - authority_start] = '\0';
    while (p < line_end && buf[p] == ' ') {
        p++;
    }
    if (p + 8u > line_end || phim_strncasecmp_ascii((const char *)(buf + p), "HTTP/1.", 7u) != 0) {
        return PHIM_ERR;
    }
    if (phim_parse_hostport(authority, out->host, sizeof(out->host), &out->port) != 0) {
        return PHIM_ERR;
    }
    if (phim_h1_get_header_value(buf, header_len, "Proxy-Connection", value, sizeof(value)) == PHIM_OK ||
        phim_h1_get_header_value(buf, header_len, "Connection", value, sizeof(value)) == PHIM_OK) {
        if (phim_strcasestr_ascii(value, "close") != NULL) {
            out->keep_alive = false;
        }
    }
    return PHIM_OK;
}
