#ifndef PHIM_CONNECT_PARSER_H
#define PHIM_CONNECT_PARSER_H

#include <stddef.h>
#include "phimantec/phim_types.h"

int phim_parse_connect_request(const unsigned char *buf, size_t len, phim_connect_request_t *out);

#endif
