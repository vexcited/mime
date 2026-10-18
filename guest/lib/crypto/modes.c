/* the block cipher modes of sp 800-38a and the cmac of sp 800-38b. */
#include "internal.h"

void mime_ecb_encrypt(const MimeCipher *c, const void *key, const uint8_t *in, uint8_t *out, size_t size)
{
    for (size_t i = 0; i < size; i += c->block_size)
        c->encrypt(key, in + i, out + i);
}

void mime_ecb_decrypt(const MimeCipher *c, const void *key, const uint8_t *in, uint8_t *out, size_t size)
{
    for (size_t i = 0; i < size; i += c->block_size)
        c->decrypt(key, in + i, out + i);
}

void mime_cbc_encrypt(const MimeCipher *c, const void *key, uint8_t *iv, const uint8_t *in, uint8_t *out,
                      size_t size)
{
    size_t b = c->block_size;
    for (size_t i = 0; i < size; i += b) {
        xor_block(iv, iv, in + i, b);
        c->encrypt(key, iv, iv);
        memcpy(out + i, iv, b);
    }
}

void mime_cbc_decrypt(const MimeCipher *c, const void *key, uint8_t *iv, const uint8_t *in, uint8_t *out,
                      size_t size)
{
    size_t b = c->block_size;
    uint8_t block[16], next[16];
    for (size_t i = 0; i < size; i += b) {
        memcpy(next, in + i, b);
        c->decrypt(key, next, block);
        xor_block(out + i, block, iv, b);
        memcpy(iv, next, b);
    }
}

static void cfb(const MimeCipher *c, const void *key, uint8_t *iv, size_t segment, const uint8_t *in, uint8_t *out,
                size_t size, bool decrypt)
{
    size_t b = c->block_size;
    uint8_t stream[16], text[16];
    for (size_t i = 0; i < size; i += segment) {
        c->encrypt(key, iv, stream);
        memcpy(text, in + i, segment);
        xor_block(out + i, text, stream, segment);
        const uint8_t *feedback = decrypt ? text : out + i;
        memmove(iv, iv + segment, b - segment);
        memcpy(iv + b - segment, feedback, segment);
    }
}

void mime_cfb_encrypt(const MimeCipher *c, const void *key, uint8_t *iv, size_t segment, const uint8_t *in,
                      uint8_t *out, size_t size)
{
    cfb(c, key, iv, segment, in, out, size, false);
}

void mime_cfb_decrypt(const MimeCipher *c, const void *key, uint8_t *iv, size_t segment, const uint8_t *in,
                      uint8_t *out, size_t size)
{
    cfb(c, key, iv, segment, in, out, size, true);
}

/* ------------------------------------------------------------------ cmac */

static void double_block(uint8_t *out, const uint8_t *in)
{
    uint8_t carry = in[0] >> 7;
    for (int i = 0; i < 15; i++)
        out[i] = (uint8_t)(in[i] << 1 | in[i + 1] >> 7);
    out[15] = (uint8_t)(in[15] << 1) ^ (0x87 & (0 - carry));
}

int mime_cmac_init(MimeCmac *m, const void *key, size_t size)
{
    int status = mime_aes_init(&m->aes, key, size);
    if (status)
        return status;
    uint8_t l[16] = {};
    mime_aes_encrypt(&m->aes, l, l);
    double_block(m->k1, l);
    double_block(m->k2, m->k1);
    mime_wipe(l, sizeof(l));
    memset(m->x, 0, 16);
    m->used = 0;
    return MIME_CRYPTO_OK;
}

void mime_cmac_update(MimeCmac *m, const void *data, size_t size)
{
    const uint8_t *p = data;
    while (size) {
        /* a full buffer is only processed once more data shows it was not the last block. */
        if (m->used == 16) {
            xor_block(m->x, m->x, m->buf, 16);
            mime_aes_encrypt(&m->aes, m->x, m->x);
            m->used = 0;
        }
        size_t n = 16 - m->used < size ? 16 - m->used : size;
        memcpy(m->buf + m->used, p, n);
        m->used += (uint32_t)n;
        p += n;
        size -= n;
    }
}

void mime_cmac_final(MimeCmac *m, uint8_t *tag)
{
    if (m->used == 16) {
        xor_block(m->buf, m->buf, m->k1, 16);
    } else {
        m->buf[m->used] = 0x80;
        memset(m->buf + m->used + 1, 0, 15 - m->used);
        xor_block(m->buf, m->buf, m->k2, 16);
    }
    xor_block(m->x, m->x, m->buf, 16);
    mime_aes_encrypt(&m->aes, m->x, tag);
    memset(m->x, 0, 16);
    m->used = 0;
}

/* ------------------------------------------------------------------ aes key wrap of rfc 3394 */

int mime_aes_wrap(const MimeAes *kek, const uint8_t *in, size_t size, uint8_t *out)
{
    if (size < 16 || size % 8)
        return MIME_CRYPTO_ERR_PARAM;
    size_t n = size / 8;
    uint8_t a[8], b[16];
    memset(a, 0xA6, 8);
    memmove(out + 8, in, size);
    for (uint64_t j = 0; j < 6; j++)
        for (size_t i = 1; i <= n; i++) {
            memcpy(b, a, 8);
            memcpy(b + 8, out + 8 * i, 8);
            mime_aes_encrypt(kek, b, b);
            uint64_t t = n * j + i;
            for (int k = 0; k < 8; k++)
                a[k] = b[k] ^ (uint8_t)(t >> (56 - 8 * k));
            memcpy(out + 8 * i, b + 8, 8);
        }
    memcpy(out, a, 8);
    mime_wipe(b, sizeof(b));
    return MIME_CRYPTO_OK;
}

int mime_aes_unwrap(const MimeAes *kek, const uint8_t *in, size_t size, uint8_t *out)
{
    if (size < 24 || size % 8)
        return MIME_CRYPTO_ERR_PARAM;
    size_t n = size / 8 - 1;
    uint8_t a[8], b[16], iv[8];
    memcpy(a, in, 8);
    memmove(out, in + 8, size - 8);
    for (uint64_t j = 6; j--;)
        for (size_t i = n; i >= 1; i--) {
            uint64_t t = n * j + i;
            for (int k = 0; k < 8; k++)
                b[k] = a[k] ^ (uint8_t)(t >> (56 - 8 * k));
            memcpy(b + 8, out + 8 * (i - 1), 8);
            mime_aes_decrypt(kek, b, b);
            memcpy(a, b, 8);
            memcpy(out + 8 * (i - 1), b + 8, 8);
        }
    memset(iv, 0xA6, 8);
    mime_wipe(b, sizeof(b));
    if (!mime_ct_equal(a, iv, 8)) {
        mime_wipe(out, size - 8);
        return MIME_CRYPTO_ERR_SIGNATURE;
    }
    return MIME_CRYPTO_OK;
}
