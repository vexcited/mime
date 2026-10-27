/*
 * LZMS decompressor for the resources of a Windows image kept in solid form, as the ESD files of Windows are. Each
 * chunk has an independent range coder reading 16-bit words from the front and adaptive Huffman codes reading from
 * the back, and an x86 filter restores relative call targets. Freestanding: the caller gives the memory.
 */
#ifndef MIME_LIB_LZMS_H
#define MIME_LIB_LZMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MimeLzms MimeLzms;

/* How much memory a decompressor needs, and the way to make it ready. The memory is the caller's. */
size_t mime_lzms_size(void);
void mime_lzms_init(MimeLzms *lzms);

/* Decompresses one chunk. out_size must be the exact uncompressed size. */
bool mime_lzms_decompress(MimeLzms *lzms, const void *in, size_t in_size, void *out, size_t out_size);

#endif
