#ifndef MIME_LIB_CRYPTO_INTERNAL_H
#define MIME_LIB_CRYPTO_INTERNAL_H

#include "crypto.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static inline uint32_t load_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline uint32_t load_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t load_be64(const uint8_t *p)
{
    return (uint64_t)load_be32(p) << 32 | load_be32(p + 4);
}

static inline void store_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline void store_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void store_be64(uint8_t *p, uint64_t v)
{
    store_be32(p, (uint32_t)(v >> 32));
    store_be32(p + 4, (uint32_t)v);
}

static inline void xor_block(uint8_t *out, const uint8_t *a, const uint8_t *b, size_t size)
{
    for (size_t i = 0; i < size; i++)
        out[i] = a[i] ^ b[i];
}

/* whether the processor multiplies carry-less, for ghash. */
bool mime_crypto_has_clmul(void);

/* all ones when the value is not zero. */
static inline uint32_t ct_nonzero(uint32_t v)
{
    return (uint32_t)(0 - ((v | (0 - v)) >> 31));
}

#endif
