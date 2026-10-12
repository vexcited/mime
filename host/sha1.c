/* sha-1 as specified by fips 180-4, used to verify wim blobs. */
#include "mime_host.h"

#include <string.h>

static inline uint32_t rol32(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static void sha1_block(uint32_t state[5], const uint8_t block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 |
               block[i * 4 + 3];
    for (int i = 16; i < 80; i++)
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t t = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void mime_sha1_init(MimeSha1 *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->length = 0;
    ctx->buffered = 0;
}

void mime_sha1_update(MimeSha1 *ctx, const void *data, size_t size)
{
    const uint8_t *p = data;
    ctx->length += size;
    if (ctx->buffered) {
        size_t n = MIME_MIN(size, 64 - ctx->buffered);
        memcpy(ctx->buffer + ctx->buffered, p, n);
        ctx->buffered += (uint32_t)n;
        p += n;
        size -= n;
        if (ctx->buffered < 64)
            return;
        sha1_block(ctx->state, ctx->buffer);
        ctx->buffered = 0;
    }
    while (size >= 64) {
        sha1_block(ctx->state, p);
        p += 64;
        size -= 64;
    }
    memcpy(ctx->buffer, p, size);
    ctx->buffered = (uint32_t)size;
}

void mime_sha1_final(MimeSha1 *ctx, uint8_t digest[20])
{
    uint64_t bits = ctx->length * 8;
    uint8_t pad = 0x80;
    mime_sha1_update(ctx, &pad, 1);
    uint8_t zero = 0;
    while (ctx->buffered != 56)
        mime_sha1_update(ctx, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++)
        len[i] = (uint8_t)(bits >> (56 - i * 8));
    mime_sha1_update(ctx, len, 8);
    for (int i = 0; i < 5; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}
