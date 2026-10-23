/*
 * XPRESS Huffman decompressor for WIM resources. each chunk is one block: 512 four-bit code lengths, then
 * 16-bit bitstream words with inline match length bytes.
 */
#ifndef MIME_XPRESS_H
#define MIME_XPRESS_H

#include "../mime_host.h"

#define MIME_XPRESS_MAX_CHUNK 65536u

/* decompresses one chunk; out_size must be the exact uncompressed size. */
bool mime_xpress_decompress(const void *in, size_t in_size, void *out, size_t out_size);

#endif
