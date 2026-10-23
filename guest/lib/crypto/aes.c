/*
 * aes of fips 197. aes-ni runs in constant time; the byte oriented fallback
 * looks up the s-box, whose timing depends on the cache.
 */
#include "internal.h"

#include "aes_tables.inc"

#if (defined(__x86_64__) || defined(__i386__)) && !defined(MIME_CRYPTO_PORTABLE)
#define MIME_AES_NI 1
typedef long long v2di __attribute__((vector_size(16)));

static uint32_t cpuid_ecx(void)
{
    uint32_t a = 1, b, c = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    return c;
}

/* bit 25 is aes-ni, bit 1 pclmulqdq. */
static uint32_t cpu_features(void)
{
    static volatile uint32_t features;
    uint32_t f = features;
    if (!f) {
        f = 0x80000000u | (cpuid_ecx() & ((1u << 25) | (1u << 1)));
        features = f;
    }
    return f;
}

bool mime_crypto_has_clmul(void)
{
    return (cpu_features() >> 1) & 1;
}

__attribute__((target("aes,sse2"))) static void hw_inverse_keys(MimeAes *a)
{
    uint32_t n = a->rounds;
    memcpy(a->dk, a->ek + 16 * n, 16);
    for (uint32_t r = 1; r < n; r++) {
        v2di k;
        memcpy(&k, a->ek + 16 * (n - r), 16);
        k = __builtin_ia32_aesimc128(k);
        memcpy(a->dk + 16 * r, &k, 16);
    }
    memcpy(a->dk + 16 * n, a->ek, 16);
}

__attribute__((target("aes,sse2"))) static void hw_encrypt(const MimeAes *a, const uint8_t *in, uint8_t *out)
{
    v2di s, k;
    memcpy(&s, in, 16);
    memcpy(&k, a->ek, 16);
    s ^= k;
    for (uint32_t r = 1; r < a->rounds; r++) {
        memcpy(&k, a->ek + 16 * r, 16);
        s = __builtin_ia32_aesenc128(s, k);
    }
    memcpy(&k, a->ek + 16 * a->rounds, 16);
    s = __builtin_ia32_aesenclast128(s, k);
    memcpy(out, &s, 16);
}

__attribute__((target("aes,sse2"))) static void hw_decrypt(const MimeAes *a, const uint8_t *in, uint8_t *out)
{
    v2di s, k;
    memcpy(&s, in, 16);
    memcpy(&k, a->dk, 16);
    s ^= k;
    for (uint32_t r = 1; r < a->rounds; r++) {
        memcpy(&k, a->dk + 16 * r, 16);
        s = __builtin_ia32_aesdec128(s, k);
    }
    memcpy(&k, a->dk + 16 * a->rounds, 16);
    s = __builtin_ia32_aesdeclast128(s, k);
    memcpy(out, &s, 16);
}
#else
bool mime_crypto_has_clmul(void)
{
    return false;
}
#endif

static uint8_t xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (0x1b & (0 - (x >> 7))));
}

static uint8_t gmul(uint8_t x, uint8_t y)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
        r ^= x & (0 - (y & 1));
        x = xtime(x);
        y >>= 1;
    }
    return r;
}

int mime_aes_init(MimeAes *a, const void *key, size_t size)
{
    if (size != 16 && size != 24 && size != 32)
        return MIME_CRYPTO_ERR_PARAM;
    uint32_t nk = (uint32_t)size / 4;
    a->rounds = nk + 6;
    uint8_t *w = a->ek;
    memcpy(w, key, size);
    uint8_t rcon = 1;
    for (uint32_t i = nk; i < 4 * (a->rounds + 1); i++) {
        uint8_t t[4];
        memcpy(t, w + 4 * (i - 1), 4);
        if (i % nk == 0) {
            uint8_t first = t[0];
            t[0] = aes_sbox[t[1]] ^ rcon;
            t[1] = aes_sbox[t[2]];
            t[2] = aes_sbox[t[3]];
            t[3] = aes_sbox[first];
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            for (int j = 0; j < 4; j++)
                t[j] = aes_sbox[t[j]];
        }
        for (int j = 0; j < 4; j++)
            w[4 * i + j] = w[4 * (i - nk) + j] ^ t[j];
    }
    a->hardware = 0;
#ifdef MIME_AES_NI
    if (cpu_features() & (1u << 25)) {
        a->hardware = 1;
        hw_inverse_keys(a);
    }
#endif
    return MIME_CRYPTO_OK;
}

static void sub_shift(uint8_t *s, const uint8_t *box, int direction)
{
    uint8_t t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            t[4 * c + r] = box[s[4 * ((c + direction * r + 4) & 3) + r]];
    memcpy(s, t, 16);
}

void mime_aes_encrypt(const MimeAes *a, const uint8_t *in, uint8_t *out)
{
#ifdef MIME_AES_NI
    if (a->hardware) {
        hw_encrypt(a, in, out);
        return;
    }
#endif
    uint8_t s[16];
    xor_block(s, in, a->ek, 16);
    for (uint32_t round = 1; round <= a->rounds; round++) {
        sub_shift(s, aes_sbox, 1);
        if (round != a->rounds)
            for (int c = 0; c < 4; c++) {
                uint8_t *col = s + 4 * c, all = col[0] ^ col[1] ^ col[2] ^ col[3], first = col[0];
                col[0] ^= all ^ xtime(col[0] ^ col[1]);
                col[1] ^= all ^ xtime(col[1] ^ col[2]);
                col[2] ^= all ^ xtime(col[2] ^ col[3]);
                col[3] ^= all ^ xtime(col[3] ^ first);
            }
        xor_block(s, s, a->ek + 16 * round, 16);
    }
    memcpy(out, s, 16);
}

void mime_aes_decrypt(const MimeAes *a, const uint8_t *in, uint8_t *out)
{
#ifdef MIME_AES_NI
    if (a->hardware) {
        hw_decrypt(a, in, out);
        return;
    }
#endif
    uint8_t s[16];
    xor_block(s, in, a->ek + 16 * a->rounds, 16);
    for (uint32_t round = a->rounds; round >= 1; round--) {
        sub_shift(s, aes_inv_sbox, -1);
        xor_block(s, s, a->ek + 16 * (round - 1), 16);
        if (round != 1)
            for (int c = 0; c < 4; c++) {
                uint8_t *col = s + 4 * c, v[4];
                for (int r = 0; r < 4; r++)
                    v[r] = gmul(col[r], 14) ^ gmul(col[(r + 1) & 3], 11) ^ gmul(col[(r + 2) & 3], 13) ^
                           gmul(col[(r + 3) & 3], 9);
                memcpy(col, v, 4);
            }
    }
    memcpy(out, s, 16);
}

static void aes_encrypt_fn(const void *key, const uint8_t *in, uint8_t *out)
{
    mime_aes_encrypt(key, in, out);
}

static void aes_decrypt_fn(const void *key, const uint8_t *in, uint8_t *out)
{
    mime_aes_decrypt(key, in, out);
}

const MimeCipher mime_cipher_aes = {16, aes_encrypt_fn, aes_decrypt_fn};
