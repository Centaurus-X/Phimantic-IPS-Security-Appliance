#ifndef PHIM_BLOCKPAGE_H
#define PHIM_BLOCKPAGE_H

#include <stddef.h>
#include "phimantec/phim_types.h"

int phim_blockpage_build(const phim_verdict_t *verdict, const char *flow_id,
                         unsigned char *dst, size_t dst_cap, size_t *dst_len);

#endif
