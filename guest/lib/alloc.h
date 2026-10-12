/*
 * allocator interface for freestanding libraries used both by the kernel
 * and by user mode, such as the font rasterizer and the image decoders.
 */
#ifndef MIME_LIB_ALLOC_H
#define MIME_LIB_ALLOC_H

#include <stddef.h>

typedef struct MimeAllocator {
    void *(*alloc)(void *ctx, size_t size); /* returns NULL on failure, memory need not be zeroed */
    void (*free)(void *ctx, void *p);
    void *ctx;
} MimeAllocator;

static inline void *mime_alloc(const MimeAllocator *a, size_t size)
{
    return a->alloc(a->ctx, size);
}

static inline void mime_free(const MimeAllocator *a, void *p)
{
    if (p)
        a->free(a->ctx, p);
}

#endif
