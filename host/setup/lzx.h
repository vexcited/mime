/*
 * lzx decompressor for wim resources.
 * every wim chunk is an independent lzx stream: the window is the chunk,
 * there is no intel e8 header (translation is always on with a fixed
 * translation size of 12000000) and recent offsets start at 1.
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
