/*
 * des of fips 46-3, triple des in its ede form and desx. the s-boxes are
 * looked up by secret bits; des survives for compatibility only.
 */
#include "internal.h"

static const uint8_t ip[64] = {58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
                               62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
                               57, 49, 41, 33, 25, 17, 9,  1, 59, 51, 43, 35, 27, 19, 11, 3,
                               61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7};

static const uint8_t fp[64] = {40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
                               38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
                               36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
                               34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9,  49, 17, 57, 25};

static const uint8_t expansion[48] = {32, 1,  2,  3,  4,  5,  4,  5,  6,  7,  8,  9,  8,  9,  10, 11,
                                      12, 13, 12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21,
                                      22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};

static const uint8_t pbox[32] = {16, 7, 20, 21, 29, 12, 28, 17, 1,  15, 23, 26, 5,  18, 31, 10,
                                 2,  8, 24, 14, 32, 27, 3,  9,  19, 13, 30, 6,  22, 11, 4,  25};

static const uint8_t pc1[56] = {57, 49, 41, 33, 25, 17, 9,  1,  58, 50, 42, 34, 26, 18,
                                10, 2,  59, 51, 43, 35, 27, 19, 11, 3,  60, 52, 44, 36,
                                63, 55, 47, 39, 31, 23, 15, 7,  62, 54, 46, 38, 30, 22,
                                14, 6,  61, 53, 45, 37, 29, 21, 13, 5,  28, 20, 12, 4};

static const uint8_t pc2[48] = {14, 17, 11, 24, 1,  5,  3,  28, 15, 6,  21, 10, 23, 19, 12, 4,
                                26, 8,  16, 7,  27, 20, 13, 2,  41, 52, 31, 37, 47, 55, 30, 40,
                                51, 45, 33, 48, 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};

static const uint8_t shifts[16] = {1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1};

static const uint8_t sbox[8][64] = {
    {14, 4,  13, 1, 2,  15, 11, 8,  3,  10, 6,  12, 5,  9,  0, 7,  0, 15, 7,  4,  14, 2,
     13, 1,  10, 6, 12, 11, 9,  5,  3,  8,  4,  1,  14, 8,  13, 6, 2,  11, 15, 12, 9,  7,
     3,  10, 5,  0, 15, 12, 8,  2,  4,  9,  1,  7,  5,  11, 3,  14, 10, 0,  6,  13},
    {15, 1,  8,  14, 6,  11, 3,  4,  9,  7, 2,  13, 12, 0, 5,  10, 3,  13, 4,  7,  15, 2,
     8,  14, 12, 0,  1,  10, 6,  9,  11, 5, 0,  14, 7,  11, 10, 4,  13, 1,  5,  8,  12, 6,
     9,  3,  2,  15, 13, 8,  10, 1,  3,  15, 4,  2,  11, 6,  7,  12, 0,  5,  14, 9},
    {10, 0,  9,  14, 6,  3,  15, 5,  1,  13, 12, 7,  11, 4,  2,  8,  13, 7,  0,  9,  3,  4,
     6,  10, 2,  8,  5,  14, 12, 11, 15, 1,  13, 6,  4,  9,  8,  15, 3,  0,  11, 1,  2,  12,
     5,  10, 14, 7,  1,  10, 13, 0,  6,  9,  8,  7,  4,  15, 14, 3,  11, 5,  2,  12},
    {7,  13, 14, 3,  0,  6,  9,  10, 1,  2,  8,  5,  11, 12, 4,  15, 13, 8,  11, 5,  6,  15,
     0,  3,  4,  7,  2,  12, 1,  10, 14, 9,  10, 6,  9,  0,  12, 11, 7,  13, 15, 1,  3,  14,
     5,  2,  8,  4,  3,  15, 0,  6,  10, 1,  13, 8,  9,  4,  5,  11, 12, 7,  2,  14},
    {2,  12, 4,  1,  7,  10, 11, 6,  8,  5,  3,  15, 13, 0, 14, 9,  14, 11, 2,  12, 4,  7,
     13, 1,  5,  0,  15, 10, 3,  9,  8,  6,  4,  2,  1,  11, 10, 13, 7,  8,  15, 9,  12, 5,
     6,  3,  0,  14, 11, 8,  12, 7,  1,  14, 2,  13, 6,  15, 0,  9,  10, 4,  5,  3},
    {12, 1,  10, 15, 9,  2,  6,  8,  0,  13, 3,  4,  14, 7,  5,  11, 10, 15, 4,  2,  7,  12,
     9,  5,  6,  1,  13, 14, 0,  11, 3,  8,  9,  14, 15, 5,  2,  8,  12, 3,  7,  0,  4,  10,
     1,  13, 11, 6,  4,  3,  2,  12, 9,  5,  15, 10, 11, 14, 1,  7,  6,  0,  8,  13},
    {4,  11, 2,  14, 15, 0,  8,  13, 3,  12, 9,  7,  5,  10, 6,  1,  13, 0,  11, 7,  4,  9,
     1,  10, 14, 3,  5,  12, 2,  15, 8,  6,  1,  4,  11, 13, 12, 3,  7,  14, 10, 15, 6,  8,
     0,  5,  9,  2,  6,  11, 13, 8,  1,  4,  10, 7,  9,  5,  0,  15, 14, 2,  3,  12},
    {13, 2,  8,  4,  6,  15, 11, 1,  10, 9,  3,  14, 5,  0,  12, 7,  1,  15, 13, 8,  10, 3,
     7,  4,  12, 5,  6,  11, 0,  14, 9,  2,  7,  11, 4,  1,  9,  12, 14, 2,  0,  6,  10, 13,
     15, 3,  5,  8,  2,  1,  14, 7,  4,  10, 8,  13, 15, 12, 9,  0,  3,  5,  6,  11},
};

/* bit 1 of a table is the most significant of the in_bits wide input. */
static uint64_t permute(uint64_t in, int in_bits, const uint8_t *table, int n)
{
    uint64_t out = 0;
    for (int i = 0; i < n; i++)
        out = out << 1 | ((in >> (in_bits - table[i])) & 1);
    return out;
}

void mime_des_init(MimeDes *des, const uint8_t key[8])
{
    uint64_t k = 0;
    for (int i = 0; i < 8; i++)
        k = k << 8 | key[i];
    uint64_t cd = permute(k, 64, pc1, 56);
    uint32_t c = (uint32_t)(cd >> 28), d = (uint32_t)(cd & 0xFFFFFFF);
    for (int i = 0; i < 16; i++) {
        for (int s = 0; s < shifts[i]; s++) {
            c = ((c << 1) | (c >> 27)) & 0xFFFFFFF;
            d = ((d << 1) | (d >> 27)) & 0xFFFFFFF;
        }
        des->k[i] = permute((uint64_t)c << 28 | d, 56, pc2, 48);
    }
}

static uint32_t feistel(uint32_t r, uint64_t k)
{
    uint64_t x = permute(r, 32, expansion, 48) ^ k;
    uint32_t out = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t b = (uint32_t)(x >> (42 - 6 * i)) & 0x3F;
        out = out << 4 | sbox[i][(b & 0x20) | ((b & 1) << 4) | ((b >> 1) & 0xF)];
    }
    return (uint32_t)permute(out, 32, pbox, 32);
}

static void des_block(const MimeDes *des, const uint8_t *in, uint8_t *out, bool decrypt)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = v << 8 | in[i];
    v = permute(v, 64, ip, 64);
    uint32_t l = (uint32_t)(v >> 32), r = (uint32_t)v;
    for (int i = 0; i < 16; i++) {
        uint32_t t = r;
        r = l ^ feistel(r, des->k[decrypt ? 15 - i : i]);
        l = t;
    }
    v = permute((uint64_t)r << 32 | l, 64, fp, 64);
    for (int i = 7; i >= 0; i--, v >>= 8)
        out[i] = (uint8_t)v;
}

void mime_des_encrypt(const MimeDes *des, const uint8_t *in, uint8_t *out)
{
    des_block(des, in, out, false);
}

void mime_des_decrypt(const MimeDes *des, const uint8_t *in, uint8_t *out)
{
    des_block(des, in, out, true);
}

int mime_des3_init(MimeDes3 *des, const void *key, size_t size)
{
    const uint8_t *k = key;
    if (size != 16 && size != 24)
        return MIME_CRYPTO_ERR_PARAM;
    mime_des_init(&des->k[0], k);
    mime_des_init(&des->k[1], k + 8);
    mime_des_init(&des->k[2], size == 24 ? k + 16 : k);
    return MIME_CRYPTO_OK;
}

void mime_des3_encrypt(const MimeDes3 *des, const uint8_t *in, uint8_t *out)
{
    uint8_t t[8];
    des_block(&des->k[0], in, t, false);
    des_block(&des->k[1], t, t, true);
    des_block(&des->k[2], t, out, false);
}

void mime_des3_decrypt(const MimeDes3 *des, const uint8_t *in, uint8_t *out)
{
    uint8_t t[8];
    des_block(&des->k[2], in, t, true);
    des_block(&des->k[1], t, t, false);
    des_block(&des->k[0], t, out, true);
}

void mime_desx_init(MimeDesx *des, const uint8_t key[24])
{
    mime_des_init(&des->k, key);
    memcpy(des->pre, key + 8, 8);
    memcpy(des->post, key + 16, 8);
}

void mime_desx_encrypt(const MimeDesx *des, const uint8_t *in, uint8_t *out)
{
    uint8_t t[8];
    xor_block(t, in, des->pre, 8);
    des_block(&des->k, t, t, false);
    xor_block(out, t, des->post, 8);
}

void mime_desx_decrypt(const MimeDesx *des, const uint8_t *in, uint8_t *out)
{
    uint8_t t[8];
    xor_block(t, in, des->post, 8);
    des_block(&des->k, t, t, true);
    xor_block(out, t, des->pre, 8);
}

static void des_encrypt_fn(const void *key, const uint8_t *in, uint8_t *out) { mime_des_encrypt(key, in, out); }
static void des_decrypt_fn(const void *key, const uint8_t *in, uint8_t *out) { mime_des_decrypt(key, in, out); }
static void des3_encrypt_fn(const void *key, const uint8_t *in, uint8_t *out) { mime_des3_encrypt(key, in, out); }
static void des3_decrypt_fn(const void *key, const uint8_t *in, uint8_t *out) { mime_des3_decrypt(key, in, out); }
static void desx_encrypt_fn(const void *key, const uint8_t *in, uint8_t *out) { mime_desx_encrypt(key, in, out); }
static void desx_decrypt_fn(const void *key, const uint8_t *in, uint8_t *out) { mime_desx_decrypt(key, in, out); }

const MimeCipher mime_cipher_des = {8, des_encrypt_fn, des_decrypt_fn};
const MimeCipher mime_cipher_des3 = {8, des3_encrypt_fn, des3_decrypt_fn};
const MimeCipher mime_cipher_desx = {8, desx_encrypt_fn, desx_decrypt_fn};
