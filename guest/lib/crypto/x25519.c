/*
 * x25519 of rfc 7748: the montgomery ladder over curve25519, in the field of
 * 2^255 - 19 held as 16 limbs of 16 bits in signed 64-bit words, so sums and
 * differences need no carry before the next product. every step runs the
 * same instructions whatever the scalar.
 */
#include "internal.h"

typedef int64_t fe[16];

/* the limbs of p = 2^255 - 19. */
static const int64_t prime[16] = {0xFFED, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
                                  0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x7FFF};

/* 2^256 is 38 modulo p, so what leaves the top limb comes back into the first one times 38. */
static void carry(fe a)
{
    for (int i = 0; i < 16; i++) {
        int64_t c = a[i] >> 16;
        a[i] -= c * 65536;
        if (i < 15)
            a[i + 1] += c;
        else
            a[0] += 38 * c;
    }
}

static void add(fe r, const fe a, const fe b)
{
    for (int i = 0; i < 16; i++)
        r[i] = a[i] + b[i];
}

static void sub(fe r, const fe a, const fe b)
{
    for (int i = 0; i < 16; i++)
        r[i] = a[i] - b[i];
}

static void mul(fe r, const fe a, const fe b)
{
    int64_t t[31] = {};
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++)
        t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++)
        r[i] = t[i];
    carry(r);
    carry(r);
}

static void square(fe r, const fe a)
{
    mul(r, a, a);
}

/* swaps a and b when bit is 1, with a mask instead of a branch. */
static void cswap(fe a, fe b, int64_t bit)
{
    int64_t mask = -bit;
    for (int i = 0; i < 16; i++) {
        int64_t t = mask & (a[i] ^ b[i]);
        a[i] ^= t;
        b[i] ^= t;
    }
}

/* the inverse, a to the power p - 2. the exponent 2^255 - 21 has every bit set from 254 down but bits 4 and 2. */
static void invert(fe r, const fe a)
{
    fe c;
    memcpy(c, a, sizeof(fe));
    for (int i = 253; i >= 0; i--) {
        square(c, c);
        if (i != 4 && i != 2)
            mul(c, c, a);
    }
    memcpy(r, c, sizeof(fe));
}

static void unpack(fe r, const uint8_t in[32])
{
    for (int i = 0; i < 16; i++)
        r[i] = in[2 * i] | (int64_t)in[2 * i + 1] << 8;
    /* the top bit of a u-coordinate is ignored. */
    r[15] &= 0x7FFF;
}

/* the canonical value below p, little endian. */
static void pack(uint8_t out[32], const fe a)
{
    fe t, m;
    memcpy(t, a, sizeof(fe));
    /* fold everything above 2^255 back as 19 times it, until it is below 2^255. */
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 15; i++) {
            int64_t c = t[i] >> 16;
            t[i] -= c * 65536;
            t[i + 1] += c;
        }
        int64_t c = t[15] >> 15;
        t[15] -= c * 32768;
        t[0] += 19 * c;
    }
    /* then subtract p once unless that borrows. */
    int64_t borrow = 0;
    for (int i = 0; i < 16; i++) {
        m[i] = t[i] - prime[i] - borrow;
        borrow = (m[i] >> 16) & 1;
        m[i] &= 0xFFFF;
    }
    int64_t keep = -borrow;
    for (int i = 0; i < 16; i++) {
        int64_t v = (t[i] & keep) | (m[i] & ~keep);
        out[2 * i] = (uint8_t)v;
        out[2 * i + 1] = (uint8_t)(v >> 8);
    }
    mime_wipe(t, sizeof(t));
    mime_wipe(m, sizeof(m));
}

int mime_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    uint8_t k[32];
    memcpy(k, scalar, 32);
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;

    static const fe a24 = {0xDB41, 1};
    fe x1, x2 = {1}, z2 = {}, x3, z3 = {1}, a, aa, b, bb, e, c, d, da, cb;
    unpack(x1, point);
    memcpy(x3, x1, sizeof(fe));
    int64_t swap = 0;
    for (int t = 254; t >= 0; t--) {
        int64_t bit = (k[t / 8] >> (t % 8)) & 1;
        swap ^= bit;
        cswap(x2, x3, swap);
        cswap(z2, z3, swap);
        swap = bit;
        add(a, x2, z2);
        square(aa, a);
        sub(b, x2, z2);
        square(bb, b);
        sub(e, aa, bb);
        add(c, x3, z3);
        sub(d, x3, z3);
        mul(da, d, a);
        mul(cb, c, b);
        add(x3, da, cb);
        square(x3, x3);
        sub(z3, da, cb);
        square(z3, z3);
        mul(z3, z3, x1);
        mul(x2, aa, bb);
        mul(z2, a24, e);
        add(z2, z2, aa);
        mul(z2, z2, e);
    }
    cswap(x2, x3, swap);
    cswap(z2, z3, swap);
    invert(z2, z2);
    mul(x2, x2, z2);
    pack(out, x2);

    mime_wipe(k, sizeof(k));
    mime_wipe(x2, sizeof(x2));
    mime_wipe(z2, sizeof(z2));
    mime_wipe(x3, sizeof(x3));
    mime_wipe(z3, sizeof(z3));
    /* a peer point of small order gives zero, which key agreement must refuse. */
    uint8_t zero = 0;
    for (int i = 0; i < 32; i++)
        zero |= out[i];
    return zero ? MIME_CRYPTO_OK : MIME_CRYPTO_ERR_PARAM;
}

int mime_x25519_base(uint8_t out[32], const uint8_t scalar[32])
{
    static const uint8_t base[32] = {9};
    return mime_x25519(out, scalar, base);
}
