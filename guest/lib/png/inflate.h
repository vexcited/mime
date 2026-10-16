/*
 * one-shot deflate and zlib decoder, see rfc 1950 and rfc 1951.
 * freestanding: needs only memcpy, memmove, memset and the allocator interface.
 */
#ifndef MIME_LIB_PNG_INFLATE_H
#define MIME_LIB_PNG_INFLATE_H

#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

enum {
    MIME_INFLATE_OK = 0,
    MIME_INFLATE_ERR_DATA = -1,      /* malformed deflate or zlib stream. */
    MIME_INFLATE_ERR_TRUNCATED = -2, /* input ended before the end of the stream. */
    MIME_INFLATE_ERR_OUTPUT = -3,    /* output buffer too small, or max_size reached. */
    MIME_INFLATE_ERR_CHECKSUM = -4,  /* adler32 mismatch. */
    MIME_INFLATE_ERR_NOMEM = -5,     /* the allocator failed. */
    MIME_INFLATE_ERR_PARAM = -6,     /* invalid arguments. */
};

#define MIME_INFLATE_ZLIB 0x1u        /* input has a zlib header and an adler32 trailer. */
#define MIME_INFLATE_NO_CHECKSUM 0x2u /* with MIME_INFLATE_ZLIB, do not verify the adler32. */

/*
 * decoder state, about 6 KiB. the fields are private; the type is public so a
 * caller can place it somewhere other than the stack with mime_inflate_with.
 */
typedef struct MimeInflate {
    const uint8_t *in;
    const uint8_t *in_end;
    uint8_t *out_start;
    uint8_t *out;
    uint8_t *out_end;
    uint64_t bitbuf;
    unsigned bitcnt;
    unsigned overrun;
    int mode;
    bool last;
    bool fixed_loaded;
    int pend_lit;
    uint32_t pend_len;
    uint32_t pend_dist;
    uint32_t stored_left;
    uint32_t lit_table[1 << 10];
    uint32_t dist_table[1 << 8];
    uint16_t lit_count[16];
    uint16_t dist_count[16];
    uint16_t lit_sym[288];
    uint16_t dist_sym[32];
} MimeInflate;

/*
 * decodes src into dst. *out_len, if not NULL, receives the number of bytes
 * written, also on failure. uses a MimeInflate on the stack.
 */
int mime_inflate(const void *src, size_t src_len, void *dst, size_t dst_cap, size_t *out_len, unsigned flags);

/* same as mime_inflate with caller provided state storage. */
int mime_inflate_with(MimeInflate *state, const void *src, size_t src_len, void *dst, size_t dst_cap,
                      size_t *out_len, unsigned flags);

/*
 * decodes into a buffer obtained from alloc that grows as needed, starting at
 * size_hint bytes or a guess when 0, never larger than max_size, 0 meaning no
 * limit. on success *out receives the buffer, to release with mime_free, and
 * *out_len its used length. on failure *out is NULL.
 */
int mime_inflate_alloc(const void *src, size_t src_len, const MimeAllocator *alloc, size_t size_hint,
                       size_t max_size, void **out, size_t *out_len, unsigned flags);

/* running adler32, start with 1. */
uint32_t mime_inflate_adler32(uint32_t adler, const void *data, size_t len);

#endif
