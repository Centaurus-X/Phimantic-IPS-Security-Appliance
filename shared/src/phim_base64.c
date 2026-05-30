#include "phimantec/phim_base64.h"

static const char g_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t phim_base64_encoded_len(size_t input_len)
{
    return ((input_len + 2u) / 3u) * 4u;
}

int phim_base64_encode(const unsigned char *src, size_t src_len, char *dst, size_t dst_len)
{
    size_t i = 0u;
    size_t j = 0u;
    size_t out_len = phim_base64_encoded_len(src_len);
    if (dst_len < out_len + 1u) {
        return -1;
    }
    while (i < src_len) {
        unsigned int octet_a = i < src_len ? src[i++] : 0u;
        unsigned int octet_b = i < src_len ? src[i++] : 0u;
        unsigned int octet_c = i < src_len ? src[i++] : 0u;
        unsigned int triple = (octet_a << 16u) | (octet_b << 8u) | octet_c;
        dst[j++] = g_b64[(triple >> 18u) & 0x3fu];
        dst[j++] = g_b64[(triple >> 12u) & 0x3fu];
        dst[j++] = g_b64[(triple >> 6u) & 0x3fu];
        dst[j++] = g_b64[triple & 0x3fu];
    }
    if (src_len % 3u == 1u) {
        dst[out_len - 1u] = '=';
        dst[out_len - 2u] = '=';
    } else if (src_len % 3u == 2u) {
        dst[out_len - 1u] = '=';
    }
    dst[out_len] = '\0';
    return 0;
}
