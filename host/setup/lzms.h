/*
 * LZMS decompressor for WIM resources in install.esd. each chunk has an independent range coder reading
 * 16-bit words from the front and adaptive Huffman codes reading from the back. an x86 filter restores
 * relative call targets.
 */
#ifndef MIME_LZMS_H
#define MIME_LZMS_H

#include "../mime_host.h"

typedef struct MimeLzms MimeLzms;

MimeLzms *mime_lzms_create(void);
void mime_lzms_destroy(MimeLzms *lzms);

/* decompresses one chunk; out_size must be the exact uncompressed size. */
bool mime_lzms_decompress(MimeLzms *lzms, const void *in, size_t in_size, void *out, size_t out_size);

#endif
