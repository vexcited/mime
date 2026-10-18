/* rc4 and the chacha20 block function of rfc 8439. */
#include "internal.h"

void mime_rc4_init(MimeRc4 *rc4, const void *key, size_t size)
{
    const uint8_t *k = key;
    for (int i = 0; i < 256; i++)
        rc4->s[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j += rc4->s[i] + k[i % size];
        uint8_t t = rc4->s[i];
        rc4->s[i] = rc4->s[j];
        rc4->s[j] = t;
    }
    rc4->i = rc4->j = 0;
}

void mime_rc4_crypt(MimeRc4 *rc4, const uint8_t *in, uint8_t *out, size_t size)
{
    uint8_t i = rc4->i, j = rc4->j;
    for (size_t n = 0; n < size; n++) {
        i++;
        j += rc4->s[i];
        uint8_t t = rc4->s[i];
        rc4->s[i] = rc4->s[j];
        rc4->s[j] = t;
        out[n] = in[n] ^ rc4->s[(uint8_t)(rc4->s[i] + rc4->s[j])];
    }
    rc4->i = i;
    rc4->j = j;
}

#define QUARTER(a, b, c, d)                                                                                       \
    a += b, d ^= a, d = ROL32(d, 16), c += d, b ^= c, b = ROL32(b, 12), a += b, d ^= a, d = ROL32(d, 8), c += d, \
    b ^= c, b = ROL32(b, 7)

void mime_chacha20_block(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12], uint8_t out[64])
{
    uint32_t in[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (int i = 0; i < 8; i++)
        in[4 + i] = load_le32(key + 4 * i);
    in[12] = counter;
    for (int i = 0; i < 3; i++)
        in[13 + i] = load_le32(nonce + 4 * i);
    uint32_t x[16];
    memcpy(x, in, sizeof(x));
    for (int i = 0; i < 10; i++) {
        QUARTER(x[0], x[4], x[8], x[12]);
        QUARTER(x[1], x[5], x[9], x[13]);
        QUARTER(x[2], x[6], x[10], x[14]);
        QUARTER(x[3], x[7], x[11], x[15]);
        QUARTER(x[0], x[5], x[10], x[15]);
        QUARTER(x[1], x[6], x[11], x[12]);
        QUARTER(x[2], x[7], x[8], x[13]);
        QUARTER(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++)
        store_le32(out + 4 * i, x[i] + in[i]);
    mime_wipe(x, sizeof(x));
    mime_wipe(in, sizeof(in));
}
