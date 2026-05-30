#include "phimantec/phim_string.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int phim_ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

int phim_strcasecmp_ascii(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;
    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)phim_ascii_tolower((unsigned char)*a);
        cb = (unsigned char)phim_ascii_tolower((unsigned char)*b);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int phim_strncasecmp_ascii(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca == '\0' || cb == '\0') {
            return (int)ca - (int)cb;
        }
        ca = (unsigned char)phim_ascii_tolower(ca);
        cb = (unsigned char)phim_ascii_tolower(cb);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
    }
    return 0;
}

bool phim_startswith_ci(const char *s, const char *prefix)
{
    return phim_strncasecmp_ascii(s, prefix, strlen(prefix)) == 0;
}

const char *phim_strcasestr_ascii(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0u) {
        return haystack;
    }
    while (*haystack != '\0') {
        if (phim_strncasecmp_ascii(haystack, needle, needle_len) == 0) {
            return haystack;
        }
        haystack++;
    }
    return NULL;
}

void phim_trim_ascii(char *s)
{
    size_t len;
    char *start = s;
    while (*start != '\0' && isspace((unsigned char)*start) != 0) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1u);
    }
    len = strlen(s);
    while (len > 0u && isspace((unsigned char)s[len - 1u]) != 0) {
        s[len - 1u] = '\0';
        len--;
    }
}

int phim_copy_cstr(char *dst, size_t dst_len, const char *src)
{
    size_t len;
    if (dst == NULL || dst_len == 0u || src == NULL) {
        return -1;
    }
    len = strlen(src);
    if (len >= dst_len) {
        return -1;
    }
    memcpy(dst, src, len + 1u);
    return 0;
}

int phim_json_escape(const unsigned char *src, size_t src_len, char *dst, size_t dst_len)
{
    size_t i;
    size_t j = 0u;
    for (i = 0u; i < src_len; i++) {
        unsigned char c = src[i];
        const char *esc = NULL;
        char tmp[7];
        if (c == '"') {
            esc = "\\\"";
        } else if (c == '\\') {
            esc = "\\\\";
        } else if (c == '\b') {
            esc = "\\b";
        } else if (c == '\f') {
            esc = "\\f";
        } else if (c == '\n') {
            esc = "\\n";
        } else if (c == '\r') {
            esc = "\\r";
        } else if (c == '\t') {
            esc = "\\t";
        }
        if (esc != NULL) {
            size_t elen = strlen(esc);
            if (j + elen >= dst_len) {
                return -1;
            }
            memcpy(dst + j, esc, elen);
            j += elen;
        } else if (c < 0x20u) {
            int n = snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned int)c);
            if (n != 6 || j + 6u >= dst_len) {
                return -1;
            }
            memcpy(dst + j, tmp, 6u);
            j += 6u;
        } else {
            if (j + 1u >= dst_len) {
                return -1;
            }
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
    return 0;
}

int phim_hex_encode(const unsigned char *src, size_t src_len, char *dst, size_t dst_len)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;
    if (dst_len < (src_len * 2u) + 1u) {
        return -1;
    }
    for (i = 0u; i < src_len; i++) {
        dst[i * 2u] = hex[(src[i] >> 4u) & 0x0fu];
        dst[(i * 2u) + 1u] = hex[src[i] & 0x0fu];
    }
    dst[src_len * 2u] = '\0';
    return 0;
}
