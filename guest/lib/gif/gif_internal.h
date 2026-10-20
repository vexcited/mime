#ifndef MIME_LIB_GIF_INTERNAL_H
#define MIME_LIB_GIF_INTERNAL_H

#include "gif.h"

struct MimeGif {
    MimeAllocator alloc;
    const uint8_t *data;
    size_t size;
    MimeGifInfo info;
    MimeGifFrame *frames;
    MimeGifExtension *exts;
    bool has_restore_previous; /* some frame disposes with 3. */
};

static inline uint32_t gif_load32(const uint8_t *p)
{
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
    return v;
}

static inline uint64_t gif_load64(const uint8_t *p)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t v;
    __builtin_memcpy(&v, p, 8);
    return v;
#else
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
#endif
}

/* a growing byte buffer, shared by the encoder files. */
bool gif_buffer_reserve(MimeGifBuffer *buf, const MimeAllocator *alloc, size_t extra);

#endif
