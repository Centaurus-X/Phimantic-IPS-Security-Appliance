#include "phimantec/phim_h1.h"
#include "phimantec/phim_types.h"

#include <stdio.h>
#include <string.h>

static int feed_text(phim_h1_chunked_decoder_t *decoder, const char *text)
{
    return phim_h1_chunked_decoder_feed(decoder, (const unsigned char *)text, strlen(text));
}

static int test_simple_chunked(void)
{
    phim_h1_chunked_decoder_t decoder;
    phim_h1_chunked_decoder_init(&decoder);
    if (feed_text(&decoder, "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n") != PHIM_OK) {
        return 1;
    }
    if (!phim_h1_chunked_decoder_complete(&decoder)) {
        return 2;
    }
    return 0;
}

static int test_split_chunked(void)
{
    phim_h1_chunked_decoder_t decoder;
    phim_h1_chunked_decoder_init(&decoder);
    if (feed_text(&decoder, "a;ext=1\r\n012") != PHIM_OK) {
        return 10;
    }
    if (phim_h1_chunked_decoder_complete(&decoder)) {
        return 11;
    }
    if (feed_text(&decoder, "3456789\r\n0\r\nX-Test: yes\r\n\r\n") != PHIM_OK) {
        return 12;
    }
    if (!phim_h1_chunked_decoder_complete(&decoder)) {
        return 13;
    }
    return 0;
}

static int test_reject_bad_chunked(void)
{
    phim_h1_chunked_decoder_t decoder;
    phim_h1_chunked_decoder_init(&decoder);
    if (feed_text(&decoder, "z\r\nboom\r\n") == PHIM_OK) {
        return 20;
    }
    if (decoder.state != PHIM_H1_CHUNKED_STATE_ERROR) {
        return 21;
    }
    return 0;
}

int main(void)
{
    int rc;
    rc = test_simple_chunked();
    if (rc != 0) {
        fprintf(stderr, "test_simple_chunked failed rc=%d\n", rc);
        return rc;
    }
    rc = test_split_chunked();
    if (rc != 0) {
        fprintf(stderr, "test_split_chunked failed rc=%d\n", rc);
        return rc;
    }
    rc = test_reject_bad_chunked();
    if (rc != 0) {
        fprintf(stderr, "test_reject_bad_chunked failed rc=%d\n", rc);
        return rc;
    }
    return 0;
}
