/* helpers shared by the vector library sources. */
#ifndef MIME_LIB_VECTOR_INTERNAL_H
#define MIME_LIB_VECTOR_INTERNAL_H

#include "vector.h"

/* grows an array by doubling until at least need elements fit. */
static inline int vec_grow(const MimeAllocator *a, void **buf, uint32_t *cap, uint32_t need, size_t esz, uint32_t limit)
{
    if (need <= *cap)
        return MIME_VEC_OK;
    if (need > limit)
        return MIME_VEC_TOOBIG;
    uint32_t n = *cap < 32 ? 32 : *cap;
    while (n < need) {
        n = n > limit / 2 ? limit : n * 2;
    }
    if (n > limit)
        n = limit;
    void *p = mime_alloc(a, (size_t)n * esz);
    if (!p)
        return MIME_VEC_NOMEM;
    if (*buf) {
        __builtin_memcpy(p, *buf, (size_t)*cap * esz);
        mime_free(a, *buf);
    }
    *buf = p;
    *cap = n;
    return MIME_VEC_OK;
}

static inline float vec_absf(float v)
{
    return v < 0 ? -v : v;
}

static inline float vec_minf(float a, float b)
{
    return b < a ? b : a;
}

static inline float vec_maxf(float a, float b)
{
    return b > a ? b : a;
}

#endif
