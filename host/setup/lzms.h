/*
 * lzms decompressor for wim resources, the format of install.esd.
 * every chunk is an independent lzms stream: a range coder reads 16 bit
 * words from the front, adaptive huffman codes read them from the back,
 * and an x86 filter turns absolute call targets back into relative ones.
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
