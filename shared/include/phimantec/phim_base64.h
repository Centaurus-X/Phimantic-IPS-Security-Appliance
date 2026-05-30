#ifndef PHIM_BASE64_H
#define PHIM_BASE64_H

#include <stddef.h>

size_t phim_base64_encoded_len(size_t input_len);
int phim_base64_encode(const unsigned char *src, size_t src_len, char *dst, size_t dst_len);

#endif
