#ifndef PHIM_MEM_H
#define PHIM_MEM_H

#include <stddef.h>

void *phim_xcalloc(size_t n, size_t size);
void *phim_xmalloc(size_t size);
char *phim_xstrdup(const char *s);
void phim_secure_zero(void *ptr, size_t len);

#endif
