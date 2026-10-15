/*
 * xpress huffman decompressor for wim resources.
 * every wim chunk is one block: a table of 512 four bit code lengths
 * followed by a bitstream of 16 bit words with match length bytes inline.
 */
#ifndef MIME_XPRESS_H
#define MIME_XPRESS_H

#include "../mime_host.h"

#define MIME_XPRESS_MAX_CHUNK 65536u

/* decompresses one chunk; out_size must be the exact uncompressed size. */
bool mime_xpress_decompress(const void *in, size_t in_size, void *out, size_t out_size);

#endif
