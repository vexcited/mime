/*
 * LZX decompressor for WIM resources. each chunk is an independent stream and its own window. there is no
 * Intel E8 header: translation is always enabled with size 12000000. recent offsets start at 1.
 */
#ifndef MIME_LZX_H
#define MIME_LZX_H

#include "../mime_host.h"

#define MIME_LZX_MAX_CHUNK 32768u

typedef struct MimeLzx MimeLzx;

MimeLzx *mime_lzx_create(void);
void mime_lzx_destroy(MimeLzx *lzx);

/* decompresses one chunk; out_size must be the exact uncompressed size. */
bool mime_lzx_decompress(MimeLzx *lzx, const void *in, size_t in_size, void *out, size_t out_size);

#endif
