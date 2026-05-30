#include "phimantec/phim_h1.h"
#include "phimantec/phim_string.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int phim_h1_find_header_end(const unsigned char *buf, size_t len, size_t *header_len)
{
    size_t i;
    if (buf == NULL || header_len == NULL) {
        return PHIM_ERR;
    }
    for (i = 0u; i + 3u < len; i++) {
        if (buf[i] == '\r' && buf[i + 1u] == '\n' && buf[i + 2u] == '\r' && buf[i + 3u] == '\n') {
            *header_len = i + 4u;
            return PHIM_OK;
        }
    }
    if (len >= PHIM_MAX_HEADER_BLOCK) {
        return PHIM_ERR;
    }
    return PHIM_INCOMPLETE;
}

static int phim_token_copy(char *dst, size_t dst_len, const unsigned char *start, size_t len)
{
    if (len >= dst_len) {
        return PHIM_ERR;
    }
    memcpy(dst, start, len);
    dst[len] = '\0';
    return PHIM_OK;
}

int phim_h1_get_header_value(const unsigned char *header, size_t header_len,
                             const char *name, char *dst, size_t dst_len)
{
    size_t pos = 0u;
    size_t name_len;
    if (header == NULL || name == NULL || dst == NULL || dst_len == 0u) {
        return PHIM_ERR;
    }
    dst[0] = '\0';
    name_len = strlen(name);
    while (pos + 2u <= header_len) {
        size_t line_start = pos;
        size_t line_len;
        const unsigned char *colon;
        while (pos + 1u < header_len && !(header[pos] == '\r' && header[pos + 1u] == '\n')) {
            pos++;
        }
        if (pos + 1u >= header_len) {
            break;
        }
        line_len = pos - line_start;
        pos += 2u;
        if (line_len == 0u) {
            break;
        }
        if (memchr(header + line_start, ' ', line_len) != NULL && line_start == 0u) {
            continue;
        }
        colon = memchr(header + line_start, ':', line_len);
        if (colon == NULL) {
            continue;
        }
        if ((size_t)(colon - (header + line_start)) == name_len &&
            phim_strncasecmp_ascii((const char *)(header + line_start), name, name_len) == 0) {
            const unsigned char *vstart = colon + 1;
            const unsigned char *vend = header + line_start + line_len;
            while (vstart < vend && isspace((unsigned char)*vstart) != 0) {
                vstart++;
            }
            while (vend > vstart && isspace((unsigned char)*(vend - 1)) != 0) {
                vend--;
            }
            if ((size_t)(vend - vstart) >= dst_len) {
                return PHIM_ERR;
            }
            memcpy(dst, vstart, (size_t)(vend - vstart));
            dst[vend - vstart] = '\0';
            return PHIM_OK;
        }
    }
    return PHIM_ERR;
}

int phim_h1_parse_request(const unsigned char *buf, size_t len, phim_h1_request_t *out)
{
    size_t header_len;
    size_t line_end = 0u;
    size_t method_end;
    size_t target_start;
    size_t target_end;
    char value[128];

    if (buf == NULL || out == NULL) {
        return PHIM_ERR;
    }
    memset(out, 0, sizeof(*out));
    if (phim_h1_find_header_end(buf, len, &header_len) != PHIM_OK) {
        return PHIM_INCOMPLETE;
    }
    out->header_len = header_len;
    while (line_end + 1u < header_len && !(buf[line_end] == '\r' && buf[line_end + 1u] == '\n')) {
        line_end++;
    }
    method_end = 0u;
    while (method_end < line_end && buf[method_end] != ' ') {
        method_end++;
    }
    if (method_end == 0u || method_end >= line_end) {
        return PHIM_ERR;
    }
    target_start = method_end + 1u;
    while (target_start < line_end && buf[target_start] == ' ') {
        target_start++;
    }
    target_end = target_start;
    while (target_end < line_end && buf[target_end] != ' ') {
        target_end++;
    }
    if (target_end == target_start) {
        return PHIM_ERR;
    }
    if (phim_token_copy(out->method, sizeof(out->method), buf, method_end) != PHIM_OK) {
        return PHIM_ERR;
    }
    if (phim_token_copy(out->target, sizeof(out->target), buf + target_start, target_end - target_start) != PHIM_OK) {
        return PHIM_ERR;
    }
    if (phim_h1_get_header_value(buf, header_len, "Host", out->host, sizeof(out->host)) != PHIM_OK) {
        out->host[0] = '\0';
    }
    if (phim_h1_get_header_value(buf, header_len, "Content-Length", value, sizeof(value)) == PHIM_OK) {
        out->has_content_length = true;
        out->content_length = (size_t)strtoull(value, NULL, 10);
    }
    if (phim_h1_get_header_value(buf, header_len, "Transfer-Encoding", value, sizeof(value)) == PHIM_OK) {
        if (phim_strcasestr_ascii(value, "chunked") != NULL) {
            out->chunked = true;
        }
    }
    if (phim_h1_get_header_value(buf, header_len, "Connection", value, sizeof(value)) == PHIM_OK) {
        if (phim_strcasestr_ascii(value, "close") != NULL) {
            out->connection_close = true;
        }
    }
    return PHIM_OK;
}

int phim_h1_parse_response(const unsigned char *buf, size_t len, phim_h1_response_t *out)
{
    size_t header_len;
    char status_text[4];
    char value[128];

    if (buf == NULL || out == NULL) {
        return PHIM_ERR;
    }
    memset(out, 0, sizeof(*out));
    if (phim_h1_find_header_end(buf, len, &header_len) != PHIM_OK) {
        return PHIM_INCOMPLETE;
    }
    out->header_len = header_len;
    if (len < 12u || phim_strncasecmp_ascii((const char *)buf, "HTTP/", 5u) != 0) {
        return PHIM_ERR;
    }
    memcpy(status_text, buf + 9u, 3u);
    status_text[3] = '\0';
    out->status_code = atoi(status_text);
    if (phim_h1_get_header_value(buf, header_len, "Content-Length", value, sizeof(value)) == PHIM_OK) {
        out->has_content_length = true;
        out->content_length = (size_t)strtoull(value, NULL, 10);
    }
    if (phim_h1_get_header_value(buf, header_len, "Transfer-Encoding", value, sizeof(value)) == PHIM_OK) {
        if (phim_strcasestr_ascii(value, "chunked") != NULL) {
            out->chunked = true;
        }
    }
    if (phim_h1_get_header_value(buf, header_len, "Connection", value, sizeof(value)) == PHIM_OK) {
        if (phim_strcasestr_ascii(value, "close") != NULL) {
            out->connection_close = true;
        }
    }
    return PHIM_OK;
}

static bool phim_h1_header_name_eq(const unsigned char *line, size_t name_len, const char *name)
{
    return strlen(name) == name_len && phim_strncasecmp_ascii((const char *)line, name, name_len) == 0;
}

static bool phim_h1_should_strip_transport_hint(const unsigned char *line_start, size_t line_len)
{
    const unsigned char *colon;
    size_t name_len;
    const unsigned char *value_start;
    size_t value_len;
    char value[256];

    colon = memchr(line_start, ':', line_len);
    if (colon == NULL) {
        return false;
    }
    name_len = (size_t)(colon - line_start);

    if (phim_h1_header_name_eq(line_start, name_len, "Alt-Svc") ||
        phim_h1_header_name_eq(line_start, name_len, "Alt-Used") ||
        phim_h1_header_name_eq(line_start, name_len, "HTTP2-Settings") ||
        phim_h1_header_name_eq(line_start, name_len, "Upgrade")) {
        return true;
    }

    if (!phim_h1_header_name_eq(line_start, name_len, "Connection")) {
        return false;
    }

    value_start = colon + 1;
    while (value_start < line_start + line_len && isspace((unsigned char)*value_start) != 0) {
        value_start++;
    }
    value_len = (size_t)((line_start + line_len) - value_start);
    if (value_len >= sizeof(value)) {
        value_len = sizeof(value) - 1u;
    }
    memcpy(value, value_start, value_len);
    value[value_len] = '\0';
    return phim_strcasestr_ascii(value, "upgrade") != NULL ||
           phim_strcasestr_ascii(value, "http2-settings") != NULL;
}

int phim_h1_strip_alt_svc(const unsigned char *header, size_t header_len,
                          unsigned char *dst, size_t dst_cap, size_t *dst_len)
{
    size_t pos = 0u;
    size_t out = 0u;
    if (header == NULL || dst == NULL || dst_len == NULL) {
        return PHIM_ERR;
    }
    while (pos + 2u <= header_len) {
        size_t line_start = pos;
        size_t line_len;
        bool skip = false;
        while (pos + 1u < header_len && !(header[pos] == '\r' && header[pos + 1u] == '\n')) {
            pos++;
        }
        if (pos + 1u >= header_len) {
            return PHIM_ERR;
        }
        line_len = pos - line_start;
        pos += 2u;
        if (line_len > 0u) {
            skip = phim_h1_should_strip_transport_hint(header + line_start, line_len);
        }
        if (!skip) {
            if (out + line_len + 2u > dst_cap) {
                return PHIM_ERR;
            }
            memcpy(dst + out, header + line_start, line_len);
            out += line_len;
            dst[out++] = '\r';
            dst[out++] = '\n';
        }
        if (line_len == 0u) {
            break;
        }
    }
    *dst_len = out;
    return PHIM_OK;
}


static int phim_h1_hex_value(unsigned char c)
{
    if (c >= (unsigned char)'0' && c <= (unsigned char)'9') {
        return (int)(c - (unsigned char)'0');
    }
    if (c >= (unsigned char)'a' && c <= (unsigned char)'f') {
        return (int)(c - (unsigned char)'a') + 10;
    }
    if (c >= (unsigned char)'A' && c <= (unsigned char)'F') {
        return (int)(c - (unsigned char)'A') + 10;
    }
    return -1;
}

static void phim_h1_chunked_decoder_error(phim_h1_chunked_decoder_t *decoder)
{
    if (decoder != NULL) {
        decoder->state = PHIM_H1_CHUNKED_STATE_ERROR;
    }
}

static int phim_h1_chunked_decoder_feed_byte(phim_h1_chunked_decoder_t *decoder, unsigned char c)
{
    int hex;

    if (decoder == NULL) {
        return PHIM_ERR;
    }

    switch (decoder->state) {
    case PHIM_H1_CHUNKED_STATE_SIZE:
        if (decoder->saw_cr) {
            if (c != (unsigned char)'\n' || !decoder->saw_digit) {
                phim_h1_chunked_decoder_error(decoder);
                return PHIM_ERR;
            }
            decoder->saw_cr = false;
            decoder->saw_digit = false;
            decoder->in_extension = false;
            if (decoder->chunk_size == 0u) {
                decoder->state = PHIM_H1_CHUNKED_STATE_TRAILER;
                decoder->trailer_line_len = 0u;
            } else {
                decoder->remaining = decoder->chunk_size;
                decoder->chunk_size = 0u;
                decoder->state = PHIM_H1_CHUNKED_STATE_DATA;
            }
            return PHIM_OK;
        }
        if (c == (unsigned char)'\r') {
            decoder->saw_cr = true;
            return PHIM_OK;
        }
        if (decoder->in_extension) {
            return PHIM_OK;
        }
        hex = phim_h1_hex_value(c);
        if (hex >= 0) {
            if (decoder->chunk_size > (UINT64_MAX >> 4)) {
                phim_h1_chunked_decoder_error(decoder);
                return PHIM_ERR;
            }
            decoder->chunk_size = (decoder->chunk_size << 4) | (uint64_t)hex;
            decoder->saw_digit = true;
            return PHIM_OK;
        }
        if (c == (unsigned char)';' || c == (unsigned char)' ' || c == (unsigned char)'\t') {
            if (!decoder->saw_digit) {
                phim_h1_chunked_decoder_error(decoder);
                return PHIM_ERR;
            }
            decoder->in_extension = true;
            return PHIM_OK;
        }
        phim_h1_chunked_decoder_error(decoder);
        return PHIM_ERR;

    case PHIM_H1_CHUNKED_STATE_DATA:
        if (decoder->remaining > 0u) {
            decoder->remaining--;
            if (decoder->remaining == 0u) {
                decoder->state = PHIM_H1_CHUNKED_STATE_DATA_CR;
            }
            return PHIM_OK;
        }
        decoder->state = PHIM_H1_CHUNKED_STATE_DATA_CR;
        /* fall through */

    case PHIM_H1_CHUNKED_STATE_DATA_CR:
        if (c != (unsigned char)'\r') {
            phim_h1_chunked_decoder_error(decoder);
            return PHIM_ERR;
        }
        decoder->state = PHIM_H1_CHUNKED_STATE_DATA_LF;
        return PHIM_OK;

    case PHIM_H1_CHUNKED_STATE_DATA_LF:
        if (c != (unsigned char)'\n') {
            phim_h1_chunked_decoder_error(decoder);
            return PHIM_ERR;
        }
        decoder->state = PHIM_H1_CHUNKED_STATE_SIZE;
        decoder->chunk_size = 0u;
        decoder->remaining = 0u;
        decoder->saw_digit = false;
        decoder->in_extension = false;
        decoder->saw_cr = false;
        return PHIM_OK;

    case PHIM_H1_CHUNKED_STATE_TRAILER:
        if (decoder->saw_cr) {
            if (c != (unsigned char)'\n') {
                phim_h1_chunked_decoder_error(decoder);
                return PHIM_ERR;
            }
            if (decoder->trailer_line_len == 0u) {
                decoder->state = PHIM_H1_CHUNKED_STATE_COMPLETE;
                decoder->saw_cr = false;
                return PHIM_OK;
            }
            decoder->trailer_line_len = 0u;
            decoder->saw_cr = false;
            return PHIM_OK;
        }
        if (c == (unsigned char)'\r') {
            decoder->saw_cr = true;
            return PHIM_OK;
        }
        if (decoder->trailer_line_len >= PHIM_MAX_HEADER_BLOCK) {
            phim_h1_chunked_decoder_error(decoder);
            return PHIM_ERR;
        }
        decoder->trailer_line_len++;
        return PHIM_OK;

    case PHIM_H1_CHUNKED_STATE_COMPLETE:
        return PHIM_OK;

    case PHIM_H1_CHUNKED_STATE_ERROR:
    default:
        return PHIM_ERR;
    }
}

void phim_h1_chunked_decoder_init(phim_h1_chunked_decoder_t *decoder)
{
    if (decoder == NULL) {
        return;
    }
    memset(decoder, 0, sizeof(*decoder));
    decoder->state = PHIM_H1_CHUNKED_STATE_SIZE;
}

int phim_h1_chunked_decoder_feed(phim_h1_chunked_decoder_t *decoder,
                                 const unsigned char *buf, size_t len)
{
    size_t i;
    if (decoder == NULL || (buf == NULL && len > 0u)) {
        return PHIM_ERR;
    }
    for (i = 0u; i < len; i++) {
        if (phim_h1_chunked_decoder_feed_byte(decoder, buf[i]) != PHIM_OK) {
            return PHIM_ERR;
        }
    }
    return PHIM_OK;
}

bool phim_h1_chunked_decoder_complete(const phim_h1_chunked_decoder_t *decoder)
{
    return decoder != NULL && decoder->state == PHIM_H1_CHUNKED_STATE_COMPLETE;
}
