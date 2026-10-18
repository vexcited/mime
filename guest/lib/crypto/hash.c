/*
 * md4 of rfc 1320, md5 of rfc 1321, sha-1 and the sha-2 family of fips 180-4.
 */
#include "internal.h"

/* ------------------------------------------------------------------ md4 */

static void md4_block(uint32_t *h, const uint8_t *p)
{
    static const uint8_t order2[16] = {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15};
    static const uint8_t order3[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    static const uint8_t shift[3][4] = {{3, 7, 11, 19}, {3, 5, 9, 13}, {3, 9, 11, 15}};
    uint32_t x[16];
    for (int i = 0; i < 16; i++)
        x[i] = load_le32(p + 4 * i);
    uint32_t v[4] = {h[0], h[1], h[2], h[3]};
    for (int i = 0; i < 48; i++) {
        uint32_t a = v[(16 - i) & 3], b = v[(17 - i) & 3], c = v[(18 - i) & 3], d = v[(19 - i) & 3];
        uint32_t f, k;
        int s = shift[i / 16][i & 3];
        if (i < 16) {
            f = (b & c) | (~b & d);
            k = x[i];
        } else if (i < 32) {
            f = (b & c) | (b & d) | (c & d);
            k = x[order2[i - 16]] + 0x5A827999;
        } else {
            f = b ^ c ^ d;
            k = x[order3[i - 32]] + 0x6ED9EBA1;
        }
        a += f + k;
        v[(16 - i) & 3] = ROL32(a, s);
    }
    for (int i = 0; i < 4; i++)
        h[i] += v[i];
}

/* ------------------------------------------------------------------ md5 */

static const uint32_t md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static const uint8_t md5_r[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};

static void md5_block(uint32_t *h, const uint8_t *p)
{
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
        m[i] = load_le32(p + 4 * i);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) & 15;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) & 15;
        }
        uint32_t t = d;
        uint32_t x = a + f + md5_k[i] + m[g];
        int s = md5_r[(i / 16) * 4 + (i & 3)];
        d = c;
        c = b;
        b = b + ROL32(x, s);
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

/* ------------------------------------------------------------------ sha-1 */

static void sha1_block(uint32_t *h, const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = load_be32(p + 4 * i);
    for (int i = 16; i < 80; i++) {
        uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = ROL32(x, 1);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
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
        uint32_t t = ROL32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROL32(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

/* ------------------------------------------------------------------ sha-256 */

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void sha256_block(uint32_t *h, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = load_be32(p + 4 * i);
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t v[8];
    memcpy(v, h, sizeof(v));
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = ROR32(v[4], 6) ^ ROR32(v[4], 11) ^ ROR32(v[4], 25);
        uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t t1 = v[7] + s1 + ch + sha256_k[i] + w[i];
        uint32_t s0 = ROR32(v[0], 2) ^ ROR32(v[0], 13) ^ ROR32(v[0], 22);
        uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + s0 + maj;
    }
    for (int i = 0; i < 8; i++)
        h[i] += v[i];
}

/* ------------------------------------------------------------------ sha-512 and sha-384 */

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538,
    0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe,
    0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
    0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
    0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab,
    0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
    0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed,
    0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
    0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
    0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8, 0x1e376c085141ab53,
    0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373,
    0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c,
    0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6,
    0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
    0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817,
};

static void sha512_block(uint64_t *h, const uint8_t *p)
{
    uint64_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = load_be64(p + 8 * i);
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t v[8];
    memcpy(v, h, sizeof(v));
    for (int i = 0; i < 80; i++) {
        uint64_t s1 = ROR64(v[4], 14) ^ ROR64(v[4], 18) ^ ROR64(v[4], 41);
        uint64_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint64_t t1 = v[7] + s1 + ch + sha512_k[i] + w[i];
        uint64_t s0 = ROR64(v[0], 28) ^ ROR64(v[0], 34) ^ ROR64(v[0], 39);
        uint64_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + s0 + maj;
    }
    for (int i = 0; i < 8; i++)
        h[i] += v[i];
}

/* ------------------------------------------------------------------ merkle damgard framing */

typedef void (*block32)(uint32_t *h, const uint8_t *p);
typedef void (*block64)(uint64_t *h, const uint8_t *p);

static void update32(MimeHashCtx32 *c, const void *data, size_t size, block32 block)
{
    const uint8_t *p = data;
    if (!size)
        return;
    c->count += size;
    if (c->used) {
        size_t n = 64 - c->used < size ? 64 - c->used : size;
        memcpy(c->buf + c->used, p, n);
        c->used += (uint32_t)n;
        p += n;
        size -= n;
        if (c->used < 64)
            return;
        block(c->h, c->buf);
        c->used = 0;
    }
    for (; size >= 64; p += 64, size -= 64)
        block(c->h, p);
    memcpy(c->buf, p, size);
    c->used = (uint32_t)size;
}

static void final32(MimeHashCtx32 *c, block32 block, bool little_endian)
{
    uint64_t bits = c->count * 8;
    c->buf[c->used++] = 0x80;
    if (c->used > 56) {
        memset(c->buf + c->used, 0, 64 - c->used);
        block(c->h, c->buf);
        c->used = 0;
    }
    memset(c->buf + c->used, 0, 56 - c->used);
    for (int i = 0; i < 8; i++)
        c->buf[little_endian ? 56 + i : 63 - i] = (uint8_t)(bits >> (8 * i));
    block(c->h, c->buf);
}

static void update64(MimeHashCtx64 *c, const void *data, size_t size, block64 block)
{
    const uint8_t *p = data;
    if (!size)
        return;
    c->count += size;
    if (c->used) {
        size_t n = 128 - c->used < size ? 128 - c->used : size;
        memcpy(c->buf + c->used, p, n);
        c->used += (uint32_t)n;
        p += n;
        size -= n;
        if (c->used < 128)
            return;
        block(c->h, c->buf);
        c->used = 0;
    }
    for (; size >= 128; p += 128, size -= 128)
        block(c->h, p);
    memcpy(c->buf, p, size);
    c->used = (uint32_t)size;
}

static void final64(MimeHashCtx64 *c, block64 block)
{
    uint64_t bits = c->count * 8;
    c->buf[c->used++] = 0x80;
    if (c->used > 112) {
        memset(c->buf + c->used, 0, 128 - c->used);
        block(c->h, c->buf);
        c->used = 0;
    }
    /* up to the low half of the length: the buffer still holds earlier data there. */
    memset(c->buf + c->used, 0, 120 - c->used);
    /* the top 64 bits of the 128 bit length stay zero, but for the three bits shifted out of count. */
    c->buf[119] = (uint8_t)(c->count >> 61);
    for (int i = 0; i < 8; i++)
        c->buf[127 - i] = (uint8_t)(bits >> (8 * i));
    block(c->h, c->buf);
}

static void init32(MimeHashCtx *ctx, const uint32_t *h, int words)
{
    memset(&ctx->c32, 0, sizeof(ctx->c32));
    memcpy(ctx->c32.h, h, words * sizeof(uint32_t));
}

static void out32(const uint32_t *h, uint8_t *out, int bytes, bool little_endian)
{
    for (int i = 0; i < bytes; i++)
        out[i] = (uint8_t)(h[i / 4] >> (little_endian ? 8 * (i % 4) : 24 - 8 * (i % 4)));
}

static const uint32_t md_iv[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};

static void md4_init(MimeHashCtx *ctx) { init32(ctx, md_iv, 4); }
static void md4_update(MimeHashCtx *ctx, const void *d, size_t n) { update32(&ctx->c32, d, n, md4_block); }
static void md4_final(MimeHashCtx *ctx, uint8_t *out)
{
    final32(&ctx->c32, md4_block, true);
    out32(ctx->c32.h, out, 16, true);
}

static void md5_init(MimeHashCtx *ctx) { init32(ctx, md_iv, 4); }
static void md5_update(MimeHashCtx *ctx, const void *d, size_t n) { update32(&ctx->c32, d, n, md5_block); }
static void md5_final(MimeHashCtx *ctx, uint8_t *out)
{
    final32(&ctx->c32, md5_block, true);
    out32(ctx->c32.h, out, 16, true);
}

static void sha1_init(MimeHashCtx *ctx) { init32(ctx, md_iv, 5); }
static void sha1_update(MimeHashCtx *ctx, const void *d, size_t n) { update32(&ctx->c32, d, n, sha1_block); }
static void sha1_final(MimeHashCtx *ctx, uint8_t *out)
{
    final32(&ctx->c32, sha1_block, false);
    out32(ctx->c32.h, out, 20, false);
}

static void sha256_init(MimeHashCtx *ctx)
{
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    init32(ctx, iv, 8);
}
static void sha256_update(MimeHashCtx *ctx, const void *d, size_t n) { update32(&ctx->c32, d, n, sha256_block); }
static void sha256_final(MimeHashCtx *ctx, uint8_t *out)
{
    final32(&ctx->c32, sha256_block, false);
    out32(ctx->c32.h, out, 32, false);
}

static void init64(MimeHashCtx *ctx, const uint64_t *h)
{
    memset(&ctx->c64, 0, sizeof(ctx->c64));
    memcpy(ctx->c64.h, h, 8 * sizeof(uint64_t));
}

static void sha384_init(MimeHashCtx *ctx)
{
    static const uint64_t iv[8] = {0xcbbb9d5dc1059ed8, 0x629a292a367cd507, 0x9159015a3070dd17, 0x152fecd8f70e5939,
                                   0x67332667ffc00b31, 0x8eb44a8768581511, 0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4};
    init64(ctx, iv);
}

static void sha512_init(MimeHashCtx *ctx)
{
    static const uint64_t iv[8] = {0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
                                   0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179};
    init64(ctx, iv);
}

static void sha512_update(MimeHashCtx *ctx, const void *d, size_t n) { update64(&ctx->c64, d, n, sha512_block); }

static void out64(const uint64_t *h, uint8_t *out, int bytes)
{
    for (int i = 0; i < bytes; i++)
        out[i] = (uint8_t)(h[i / 8] >> (56 - 8 * (i % 8)));
}

static void sha384_final(MimeHashCtx *ctx, uint8_t *out)
{
    final64(&ctx->c64, sha512_block);
    out64(ctx->c64.h, out, 48);
}

static void sha512_final(MimeHashCtx *ctx, uint8_t *out)
{
    final64(&ctx->c64, sha512_block);
    out64(ctx->c64.h, out, 64);
}

/* ------------------------------------------------------------------ descriptors */

/* the DigestInfo prefixes of rfc 8017, section 9.2. */
static const uint8_t md4_info[] = {0x30, 0x20, 0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                   0x86, 0xf7, 0x0d, 0x02, 0x04, 0x05, 0x00, 0x04, 0x10};
static const uint8_t md5_info[] = {0x30, 0x20, 0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                   0x86, 0xf7, 0x0d, 0x02, 0x05, 0x05, 0x00, 0x04, 0x10};
static const uint8_t sha1_info[] = {0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e,
                                    0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14};
static const uint8_t sha256_info[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                      0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
static const uint8_t sha384_info[] = {0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                      0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30};
static const uint8_t sha512_info[] = {0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                      0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};

const MimeHashAlg mime_md4 = {16, 64, 16, md4_info, sizeof(md4_info), md4_init, md4_update, md4_final};
const MimeHashAlg mime_md5 = {16, 64, 16, md5_info, sizeof(md5_info), md5_init, md5_update, md5_final};
const MimeHashAlg mime_sha1 = {20, 64, 20, sha1_info, sizeof(sha1_info), sha1_init, sha1_update, sha1_final};
const MimeHashAlg mime_sha256 = {32,          64,          32,          sha256_info, sizeof(sha256_info),
                                 sha256_init, sha256_update, sha256_final};
const MimeHashAlg mime_sha384 = {48,          128,           64,          sha384_info, sizeof(sha384_info),
                                 sha384_init, sha512_update, sha384_final};
const MimeHashAlg mime_sha512 = {64,          128,           64,          sha512_info, sizeof(sha512_info),
                                 sha512_init, sha512_update, sha512_final};

void mime_hash(const MimeHashAlg *alg, const void *data, size_t size, uint8_t *out)
{
    MimeHashCtx ctx;
    alg->init(&ctx);
    alg->update(&ctx, data, size);
    alg->final(&ctx, out);
    mime_wipe(&ctx, sizeof(ctx));
}

void mime_hash_save(const MimeHashAlg *alg, const MimeHashCtx *ctx, uint8_t *state)
{
    memcpy(state, alg->block_size == 128 ? (const void *)ctx->c64.h : (const void *)ctx->c32.h, alg->state_size);
}

void mime_hash_resume(const MimeHashAlg *alg, MimeHashCtx *ctx, const uint8_t *state, uint64_t blocks)
{
    alg->init(ctx);
    if (alg->block_size == 128) {
        memcpy(ctx->c64.h, state, alg->state_size);
        ctx->c64.count = blocks * 128;
    } else {
        memcpy(ctx->c32.h, state, alg->state_size);
        ctx->c32.count = blocks * 64;
    }
}

size_t mime_hash_ctx_size(const MimeHashAlg *alg)
{
    return alg->block_size == 128 ? sizeof(MimeHashCtx64) : sizeof(MimeHashCtx32);
}

void mime_wipe(void *p, size_t size)
{
    volatile uint8_t *v = p;
    while (size--)
        *v++ = 0;
}

bool mime_ct_equal(const void *a, const void *b, size_t size)
{
    const uint8_t *x = a, *y = b;
    uint8_t diff = 0;
    for (size_t i = 0; i < size; i++)
        diff |= x[i] ^ y[i];
    return diff == 0;
}
