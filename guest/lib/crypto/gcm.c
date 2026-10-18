/* gcm of sp 800-38d and ccm of sp 800-38c over aes. */
#include "internal.h"

/* x = x * h in gcm's bit reflected field, without data dependent branches. */
static void gf_mul_soft(uint8_t *x, const uint8_t *h)
{
    uint64_t vh = 0, vl = 0, zh = 0, zl = 0;
    for (int i = 0; i < 8; i++) {
        vh = vh << 8 | h[i];
        vl = vl << 8 | h[8 + i];
    }
    for (int i = 0; i < 128; i++) {
        uint64_t bit = (uint64_t)((x[i / 8] >> (7 - i % 8)) & 1);
        uint64_t mask = 0 - bit;
        zh ^= vh & mask;
        zl ^= vl & mask;
        uint64_t lsb = 0 - (vl & 1);
        vl = vl >> 1 | vh << 63;
        vh = (vh >> 1) ^ (0xE100000000000000ull & lsb);
    }
    for (int i = 0; i < 8; i++) {
        x[i] = (uint8_t)(zh >> (56 - 8 * i));
        x[8 + i] = (uint8_t)(zl >> (56 - 8 * i));
    }
}

#if (defined(__x86_64__) || defined(__i386__)) && !defined(MIME_CRYPTO_PORTABLE)
typedef long long v2di __attribute__((vector_size(16)));
typedef char v16qi __attribute__((vector_size(16)));

__attribute__((target("pclmul,sse2,ssse3"))) static v2di reverse(v2di v)
{
    v16qi b = (v16qi)v;
    return (v2di)__builtin_shufflevector(b, b, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
}

/* the carry-less product, shifted for the reflected bits and reduced modulo x^128 + x^7 + x^2 + x + 1. */
__attribute__((target("pclmul,sse2,ssse3"))) static void gf_mul_clmul(uint8_t *x, const uint8_t *h)
{
    v2di a, b;
    memcpy(&a, x, 16);
    memcpy(&b, h, 16);
    a = reverse(a);
    b = reverse(b);
    v2di lo = __builtin_ia32_pclmulqdq128(a, b, 0x00);
    v2di hi = __builtin_ia32_pclmulqdq128(a, b, 0x11);
    v2di mid = __builtin_ia32_pclmulqdq128(a, b, 0x10) ^ __builtin_ia32_pclmulqdq128(a, b, 0x01);
    lo ^= (v2di){0, mid[0]};
    hi ^= (v2di){mid[1], 0};
    /* shift the 256 bit product left by one. */
    v2di lo_carry = (v2di)((unsigned long long __attribute__((vector_size(16))))lo >> 63);
    v2di hi_carry = (v2di)((unsigned long long __attribute__((vector_size(16))))hi >> 63);
    lo <<= 1;
    hi <<= 1;
    lo |= (v2di){0, lo_carry[0]};
    hi |= (v2di){lo_carry[1], hi_carry[0]};
    /* first phase of the reduction. */
    v2di t = (lo << 63) ^ (lo << 62) ^ (lo << 57);
    v2di spill = (v2di){t[1], 0};
    lo ^= (v2di){0, t[0]};
    /* second phase. */
    typedef unsigned long long u2di __attribute__((vector_size(16)));
    u2di ul = (u2di)lo;
    v2di r = (v2di)((ul >> 1) ^ (ul >> 2) ^ (ul >> 7)) ^ spill;
    lo ^= r;
    hi ^= lo;
    hi = reverse(hi);
    memcpy(x, &hi, 16);
}

static void gf_mul(uint8_t *x, const uint8_t *h)
{
    if (mime_crypto_has_clmul())
        gf_mul_clmul(x, h);
    else
        gf_mul_soft(x, h);
}
#else
static void gf_mul(uint8_t *x, const uint8_t *h)
{
    gf_mul_soft(x, h);
}
#endif

static void ghash(uint8_t *s, const uint8_t *h, const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; i += 16) {
        size_t n = size - i < 16 ? size - i : 16;
        for (size_t j = 0; j < n; j++)
            s[j] ^= data[i + j];
        gf_mul(s, h);
    }
}

static void increment32(uint8_t *counter)
{
    for (int i = 15; i >= 12; i--)
        if (++counter[i])
            break;
}

void mime_gcm_init(MimeGcm *g, const MimeAes *aes, const uint8_t *nonce, size_t nonce_size)
{
    memset(g, 0, sizeof(*g));
    mime_aes_encrypt(aes, g->h, g->h);
    if (nonce_size == 12) {
        memcpy(g->j0, nonce, 12);
        g->j0[15] = 1;
    } else {
        uint8_t lengths[16] = {};
        ghash(g->j0, g->h, nonce, nonce_size);
        store_be64(lengths + 8, (uint64_t)nonce_size * 8);
        ghash(g->j0, g->h, lengths, 16);
    }
    memcpy(g->counter, g->j0, 16);
    increment32(g->counter);
}

void mime_gcm_aad(MimeGcm *g, const uint8_t *aad, size_t size)
{
    ghash(g->s, g->h, aad, size);
    g->aad_size += size;
}

static void gcm_crypt(MimeGcm *g, const MimeAes *aes, const uint8_t *in, uint8_t *out, size_t size, bool decrypt)
{
    uint8_t stream[16], block[16];
    for (size_t i = 0; i < size; i += 16) {
        size_t n = size - i < 16 ? size - i : 16;
        memcpy(block, in + i, n);
        if (decrypt)
            ghash(g->s, g->h, block, n);
        mime_aes_encrypt(aes, g->counter, stream);
        increment32(g->counter);
        xor_block(out + i, block, stream, n);
        if (!decrypt)
            ghash(g->s, g->h, out + i, n);
    }
    g->data_size += size;
    mime_wipe(stream, sizeof(stream));
}

void mime_gcm_encrypt(MimeGcm *g, const MimeAes *aes, const uint8_t *in, uint8_t *out, size_t size)
{
    gcm_crypt(g, aes, in, out, size, false);
}

void mime_gcm_decrypt(MimeGcm *g, const MimeAes *aes, const uint8_t *in, uint8_t *out, size_t size)
{
    gcm_crypt(g, aes, in, out, size, true);
}

void mime_gcm_tag(MimeGcm *g, const MimeAes *aes, uint8_t *tag, size_t size)
{
    uint8_t lengths[16], s[16], e[16];
    store_be64(lengths, g->aad_size * 8);
    store_be64(lengths + 8, g->data_size * 8);
    memcpy(s, g->s, 16);
    ghash(s, g->h, lengths, 16);
    mime_aes_encrypt(aes, g->j0, e);
    xor_block(s, s, e, 16);
    memcpy(tag, s, size);
}

/* ------------------------------------------------------------------ ccm */

bool mime_ccm_valid(size_t nonce_size, size_t tag_size)
{
    return nonce_size >= 7 && nonce_size <= 13 && tag_size >= 4 && tag_size <= 16 && !(tag_size & 1);
}

static void ccm_counter(uint8_t *block, const uint8_t *nonce, size_t nonce_size, uint32_t index)
{
    size_t q = 15 - nonce_size;
    memset(block, 0, 16);
    block[0] = (uint8_t)(q - 1);
    memcpy(block + 1, nonce, nonce_size);
    for (size_t i = 0; i < q && i < 4; i++)
        block[15 - i] = (uint8_t)(index >> (8 * i));
}

static int ccm_mac(const MimeAes *aes, const uint8_t *nonce, size_t nonce_size, const uint8_t *aad, size_t aad_size,
                   const uint8_t *data, size_t size, size_t tag_size, uint8_t *mac)
{
    size_t q = 15 - nonce_size;
    if (q < 8 && (uint64_t)size >> (8 * q))
        return MIME_CRYPTO_ERR_PARAM;
    uint8_t b[16];
    b[0] = (uint8_t)((aad_size ? 0x40 : 0) | ((tag_size - 2) / 2) << 3 | (q - 1));
    memcpy(b + 1, nonce, nonce_size);
    for (size_t i = 0; i < q; i++)
        b[15 - i] = i < sizeof(size_t) ? (uint8_t)((uint64_t)size >> (8 * i)) : 0;
    mime_aes_encrypt(aes, b, mac);
    if (aad_size) {
        uint8_t head[10];
        size_t head_size;
        if (aad_size < 0xFF00) {
            head[0] = (uint8_t)(aad_size >> 8);
            head[1] = (uint8_t)aad_size;
            head_size = 2;
        } else if ((uint64_t)aad_size <= 0xFFFFFFFF) {
            head[0] = 0xFF;
            head[1] = 0xFE;
            store_be32(head + 2, (uint32_t)aad_size);
            head_size = 6;
        } else {
            head[0] = 0xFF;
            head[1] = 0xFF;
            store_be64(head + 2, (uint64_t)aad_size);
            head_size = 10;
        }
        /* the encoded length and the data run into one padded stream. */
        size_t used = 0;
        memset(b, 0, 16);
        for (size_t i = 0; i < head_size + aad_size; i++) {
            b[used++] = i < head_size ? head[i] : aad[i - head_size];
            if (used == 16 || i + 1 == head_size + aad_size) {
                xor_block(mac, mac, b, 16);
                mime_aes_encrypt(aes, mac, mac);
                memset(b, 0, 16);
                used = 0;
            }
        }
    }
    for (size_t i = 0; i < size; i += 16) {
        size_t n = size - i < 16 ? size - i : 16;
        memset(b, 0, 16);
        memcpy(b, data + i, n);
        xor_block(mac, mac, b, 16);
        mime_aes_encrypt(aes, mac, mac);
    }
    return MIME_CRYPTO_OK;
}

static void ccm_ctr(const MimeAes *aes, const uint8_t *nonce, size_t nonce_size, const uint8_t *in, uint8_t *out,
                    size_t size)
{
    uint8_t counter[16], stream[16];
    for (size_t i = 0; i < size; i += 16) {
        size_t n = size - i < 16 ? size - i : 16;
        ccm_counter(counter, nonce, nonce_size, (uint32_t)(i / 16 + 1));
        mime_aes_encrypt(aes, counter, stream);
        xor_block(out + i, in + i, stream, n);
    }
    mime_wipe(stream, sizeof(stream));
}

int mime_ccm_encrypt(const MimeAes *aes, const uint8_t *nonce, size_t nonce_size, const uint8_t *aad,
                     size_t aad_size, const uint8_t *in, uint8_t *out, size_t size, uint8_t *tag, size_t tag_size)
{
    uint8_t mac[16], s0[16];
    if (!mime_ccm_valid(nonce_size, tag_size))
        return MIME_CRYPTO_ERR_PARAM;
    int status = ccm_mac(aes, nonce, nonce_size, aad, aad_size, in, size, tag_size, mac);
    if (status)
        return status;
    ccm_ctr(aes, nonce, nonce_size, in, out, size);
    ccm_counter(s0, nonce, nonce_size, 0);
    mime_aes_encrypt(aes, s0, s0);
    xor_block(tag, mac, s0, tag_size);
    return MIME_CRYPTO_OK;
}

int mime_ccm_decrypt(const MimeAes *aes, const uint8_t *nonce, size_t nonce_size, const uint8_t *aad,
                     size_t aad_size, const uint8_t *in, uint8_t *out, size_t size, const uint8_t *tag,
                     size_t tag_size)
{
    uint8_t mac[16], s0[16];
    if (!mime_ccm_valid(nonce_size, tag_size))
        return MIME_CRYPTO_ERR_PARAM;
    ccm_ctr(aes, nonce, nonce_size, in, out, size);
    int status = ccm_mac(aes, nonce, nonce_size, aad, aad_size, out, size, tag_size, mac);
    if (status)
        return status;
    ccm_counter(s0, nonce, nonce_size, 0);
    mime_aes_encrypt(aes, s0, s0);
    xor_block(mac, mac, s0, tag_size);
    if (!mime_ct_equal(mac, tag, tag_size)) {
        mime_wipe(out, size);
        return MIME_CRYPTO_ERR_SIGNATURE;
    }
    return MIME_CRYPTO_OK;
}
