/*
 * one-shot zlib encoder, see rfc 1950 and rfc 1951: a hash chain match finder and dynamic huffman blocks.
 * freestanding: needs only memcpy, memset and the allocator interface.
 */
#ifndef MIME_LIB_PNG_DEFLATE_H
#define MIME_LIB_PNG_DEFLATE_H

#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

enum {
    MIME_DEFLATE_OK = 0,
    MIME_DEFLATE_ERR_NOMEM = -1,
};

/*
 * compresses src into a zlib stream in a buffer obtained from alloc, which the caller releases with
 * mime_free. level 0 stores, 1 to 9 trade time for size (the match chain is 4 times the level long).
 */
int mime_deflate_zlib(const void *src, size_t src_len, int level, const MimeAllocator *alloc, void **out,
                      size_t *out_len);

#endif
