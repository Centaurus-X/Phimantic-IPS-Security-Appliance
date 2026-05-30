#include "phimantec/phim_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *phim_xcalloc(size_t n, size_t size)
{
    void *ptr = calloc(n, size);
    if (ptr == NULL) {
        perror("calloc");
        abort();
    }
    return ptr;
}

void *phim_xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL) {
        perror("malloc");
        abort();
    }
    return ptr;
}

char *phim_xstrdup(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = phim_xmalloc(len + 1u);
    memcpy(copy, s, len + 1u);
    return copy;
}

void phim_secure_zero(void *ptr, size_t len)
{
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len > 0u) {
        *p = 0u;
        p++;
        len--;
    }
}
