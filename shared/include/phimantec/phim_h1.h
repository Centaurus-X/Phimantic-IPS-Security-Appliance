#ifndef PHIM_H1_H
#define PHIM_H1_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "phimantec/phim_types.h"

int phim_h1_find_header_end(const unsigned char *buf, size_t len, size_t *header_len);
int phim_h1_parse_request(const unsigned char *buf, size_t len, phim_h1_request_t *out);
int phim_h1_parse_response(const unsigned char *buf, size_t len, phim_h1_response_t *out);
int phim_h1_strip_alt_svc(const unsigned char *header, size_t header_len,
                          unsigned char *dst, size_t dst_cap, size_t *dst_len);
int phim_h1_get_header_value(const unsigned char *header, size_t header_len,
                             const char *name, char *dst, size_t dst_len);

typedef enum phim_h1_chunked_decoder_state_e {
    PHIM_H1_CHUNKED_STATE_SIZE = 0,
    PHIM_H1_CHUNKED_STATE_DATA = 1,
    PHIM_H1_CHUNKED_STATE_DATA_CR = 2,
    PHIM_H1_CHUNKED_STATE_DATA_LF = 3,
    PHIM_H1_CHUNKED_STATE_TRAILER = 4,
    PHIM_H1_CHUNKED_STATE_COMPLETE = 5,
    PHIM_H1_CHUNKED_STATE_ERROR = 6
} phim_h1_chunked_decoder_state_t;

typedef struct phim_h1_chunked_decoder_s {
    phim_h1_chunked_decoder_state_t state;
    uint64_t remaining;
    uint64_t chunk_size;
    size_t trailer_line_len;
    bool saw_digit;
    bool in_extension;
    bool saw_cr;
} phim_h1_chunked_decoder_t;

void phim_h1_chunked_decoder_init(phim_h1_chunked_decoder_t *decoder);
int phim_h1_chunked_decoder_feed(phim_h1_chunked_decoder_t *decoder,
                                 const unsigned char *buf, size_t len);
bool phim_h1_chunked_decoder_complete(const phim_h1_chunked_decoder_t *decoder);

#endif
