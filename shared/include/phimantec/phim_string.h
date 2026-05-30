#ifndef PHIM_STRING_H
#define PHIM_STRING_H

#include <stddef.h>
#include <stdbool.h>

int phim_ascii_tolower(int c);
int phim_strcasecmp_ascii(const char *a, const char *b);
int phim_strncasecmp_ascii(const char *a, const char *b, size_t n);
bool phim_startswith_ci(const char *s, const char *prefix);
const char *phim_strcasestr_ascii(const char *haystack, const char *needle);
void phim_trim_ascii(char *s);
int phim_copy_cstr(char *dst, size_t dst_len, const char *src);
int phim_json_escape(const unsigned char *src, size_t src_len, char *dst, size_t dst_len);
int phim_hex_encode(const unsigned char *src, size_t src_len, char *dst, size_t dst_len);

#endif
