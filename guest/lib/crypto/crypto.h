/*
 * cryptographic primitives shared by bcrypt, crypt32 and advapi32, written
 * from their public specifications: fips 180-4 and rfc 1320/1321 for the
 * hashes, rfc 2104 for hmac, fips 197 and sp 800-38a/b/c/d for aes and its
 * modes, fips 46-3 for des, rfc 8018, rfc 5869 and sp 800-108 for the key
 * derivations, rfc 8017 for rsa, fips 186-4 and sec 1 for the elliptic
 * curves, rfc 7748 for x25519 and rfc 7919 for the finite field groups.
 *
 * freestanding: the hashes and ciphers need only memcpy, memset and memcmp.
 * the big number code, rsa, the curves and diffie-hellman also need the three
 * hooks below, which the module linking them defines.
 */
#ifndef MIME_LIB_CRYPTO_H
#define MIME_LIB_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ hooks and helpers */

void *mime_crypto_alloc(size_t size);
void mime_crypto_free(void *p);
bool mime_crypto_random(void *out, size_t size);

enum {
    MIME_CRYPTO_OK = 0,
    MIME_CRYPTO_ERR_PARAM = -1,     /* a size or value the algorithm refuses. */
    MIME_CRYPTO_ERR_SIGNATURE = -2, /* a signature or tag that does not verify. */
    MIME_CRYPTO_ERR_DATA = -3,      /* padding or encoding that does not decode. */
    MIME_CRYPTO_ERR_NOMEM = -4,
    MIME_CRYPTO_ERR_RANDOM = -5,
};

void mime_wipe(void *p, size_t size);
/* compares without a data dependent branch. */
bool mime_ct_equal(const void *a, const void *b, size_t size);

/* ------------------------------------------------------------------ hashes */

#define MIME_HASH_MAX_DIGEST 64
#define MIME_HASH_MAX_BLOCK 128

typedef struct MimeHashCtx32 {
    uint32_t h[8];
    uint64_t count;
    uint32_t used;
    uint8_t buf[64];
} MimeHashCtx32;

typedef struct MimeHashCtx64 {
    uint64_t h[8];
    uint64_t count;
    uint32_t used;
    uint8_t buf[128];
} MimeHashCtx64;

typedef union MimeHashCtx {
    MimeHashCtx32 c32;
    MimeHashCtx64 c64;
} MimeHashCtx;

typedef struct MimeHashAlg {
    uint32_t digest_size;
    uint32_t block_size;
    /* the bytes of chaining state, saved and resumed by hmac. */
    uint32_t state_size;
    /* the der DigestInfo prefix of pkcs #1 v1.5 signatures. */
    const uint8_t *digest_info;
    uint32_t digest_info_size;
    void (*init)(MimeHashCtx *ctx);
    void (*update)(MimeHashCtx *ctx, const void *data, size_t size);
    void (*final)(MimeHashCtx *ctx, uint8_t *out);
} MimeHashAlg;

extern const MimeHashAlg mime_md4, mime_md5, mime_sha1, mime_sha256, mime_sha384, mime_sha512;

void mime_hash(const MimeHashAlg *alg, const void *data, size_t size, uint8_t *out);
/* the chaining state of a context that absorbed whole blocks only, and the way back. */
void mime_hash_save(const MimeHashAlg *alg, const MimeHashCtx *ctx, uint8_t *state);
void mime_hash_resume(const MimeHashAlg *alg, MimeHashCtx *ctx, const uint8_t *state, uint64_t blocks);

/* ------------------------------------------------------------------ hmac */

/* ctx comes last: hashes of 64 byte blocks touch only its first variant, so their state can be smaller. */
typedef struct MimeHmac {
    const MimeHashAlg *alg;
    uint8_t inner[64];
    uint8_t outer[64];
    MimeHashCtx ctx;
} MimeHmac;

/* the bytes a context or an hmac of alg uses. */
size_t mime_hash_ctx_size(const MimeHashAlg *alg);
size_t mime_hmac_size(const MimeHashAlg *alg);

void mime_hmac_init(MimeHmac *h, const MimeHashAlg *alg, const void *key, size_t size);
void mime_hmac_update(MimeHmac *h, const void *data, size_t size);
/* writes the tag and starts over with the same key. */
void mime_hmac_final(MimeHmac *h, uint8_t *out);
void mime_hmac_reset(MimeHmac *h);
void mime_hmac(const MimeHashAlg *alg, const void *key, size_t key_size, const void *data, size_t size,
               uint8_t *out);

/* ------------------------------------------------------------------ key derivation */

void mime_pbkdf2(const MimeHashAlg *alg, const void *password, size_t password_size, const void *salt,
                 size_t salt_size, uint64_t iterations, uint8_t *out, size_t size);
void mime_hkdf_extract(const MimeHashAlg *alg, const void *salt, size_t salt_size, const void *ikm, size_t ikm_size,
                       uint8_t *prk);
/* fails when more than 255 blocks are asked. */
int mime_hkdf_expand(const MimeHashAlg *alg, const void *prk, size_t prk_size, const void *info, size_t info_size,
                     uint8_t *out, size_t size);
/* sp 800-108 in counter mode: [i]32 || label || 00 || context || [bits]32 under hmac. */
void mime_sp800108_hmac(const MimeHashAlg *alg, const void *key, size_t key_size, const void *label,
                        size_t label_size, const void *context, size_t context_size, uint8_t *out, size_t size);
/* the expansion of CryptDeriveKey: a hash value stretched with the 0x36 and 0x5c pads, up to two digests. */
void mime_capi_expand(const MimeHashAlg *alg, const uint8_t *hash, uint8_t *out, size_t size);
/* the tls 1.2 p_hash, or with alg null the md5 and sha-1 halves of tls 1.0 and 1.1. */
void mime_tls_prf(const MimeHashAlg *alg, const void *secret, size_t secret_size, const void *label,
                  size_t label_size, const void *seed, size_t seed_size, uint8_t *out, size_t size);

/* ------------------------------------------------------------------ block ciphers */

/* the round keys in byte order, dk the decryption keys of the equivalent inverse cipher. */
typedef struct MimeAes {
    uint8_t ek[240];
    uint8_t dk[240];
    uint32_t rounds;
    uint32_t hardware;
} MimeAes;

/* 16, 24 or 32 bytes. */
int mime_aes_init(MimeAes *aes, const void *key, size_t size);
void mime_aes_encrypt(const MimeAes *aes, const uint8_t *in, uint8_t *out);
void mime_aes_decrypt(const MimeAes *aes, const uint8_t *in, uint8_t *out);

typedef struct MimeDes {
    uint64_t k[16];
} MimeDes;

typedef struct MimeDes3 {
    MimeDes k[3];
} MimeDes3;

typedef struct MimeDesx {
    MimeDes k;
    uint8_t pre[8];
    uint8_t post[8];
} MimeDesx;

void mime_des_init(MimeDes *des, const uint8_t key[8]);
void mime_des_encrypt(const MimeDes *des, const uint8_t *in, uint8_t *out);
void mime_des_decrypt(const MimeDes *des, const uint8_t *in, uint8_t *out);
/* 16 bytes for two key and 24 for three key triple des. */
int mime_des3_init(MimeDes3 *des, const void *key, size_t size);
void mime_des3_encrypt(const MimeDes3 *des, const uint8_t *in, uint8_t *out);
void mime_des3_decrypt(const MimeDes3 *des, const uint8_t *in, uint8_t *out);
/* the des key, then the input and output whitening keys. */
void mime_desx_init(MimeDesx *des, const uint8_t key[24]);
void mime_desx_encrypt(const MimeDesx *des, const uint8_t *in, uint8_t *out);
void mime_desx_decrypt(const MimeDesx *des, const uint8_t *in, uint8_t *out);

typedef void (*MimeBlockFn)(const void *key, const uint8_t *in, uint8_t *out);

typedef struct MimeCipher {
    uint32_t block_size;
    MimeBlockFn encrypt;
    MimeBlockFn decrypt;
} MimeCipher;

extern const MimeCipher mime_cipher_aes, mime_cipher_des, mime_cipher_des3, mime_cipher_desx;

/* ------------------------------------------------------------------ stream ciphers */

typedef struct MimeRc4 {
    uint8_t s[256];
    uint8_t i, j;
} MimeRc4;

void mime_rc4_init(MimeRc4 *rc4, const void *key, size_t size);
void mime_rc4_crypt(MimeRc4 *rc4, const uint8_t *in, uint8_t *out, size_t size);

void mime_chacha20_block(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12], uint8_t out[64]);

/* ------------------------------------------------------------------ modes */

/* the sizes are whole blocks; the iv is updated for the next call. */
void mime_ecb_encrypt(const MimeCipher *c, const void *key, const uint8_t *in, uint8_t *out, size_t size);
void mime_ecb_decrypt(const MimeCipher *c, const void *key, const uint8_t *in, uint8_t *out, size_t size);
void mime_cbc_encrypt(const MimeCipher *c, const void *key, uint8_t *iv, const uint8_t *in, uint8_t *out,
                      size_t size);
void mime_cbc_decrypt(const MimeCipher *c, const void *key, uint8_t *iv, const uint8_t *in, uint8_t *out,
                      size_t size);
/* segment is 1 for cfb8 or the block size; the size is whole segments. */
void mime_cfb_encrypt(const MimeCipher *c, const void *key, uint8_t *iv, size_t segment, const uint8_t *in,
                      uint8_t *out, size_t size);
void mime_cfb_decrypt(const MimeCipher *c, const void *key, uint8_t *iv, size_t segment, const uint8_t *in,
                      uint8_t *out, size_t size);

typedef struct MimeCmac {
    MimeAes aes;
    uint8_t k1[16], k2[16];
    uint8_t x[16];
    uint8_t buf[16];
    uint32_t used;
} MimeCmac;

int mime_cmac_init(MimeCmac *m, const void *key, size_t size);
void mime_cmac_update(MimeCmac *m, const void *data, size_t size);
/* writes the 16 byte tag and starts over with the same key. */
void mime_cmac_final(MimeCmac *m, uint8_t *tag);

/* aes key wrap of rfc 3394 with its default iv: out gets size + 8 bytes, or size - 8 when unwrapping. */
int mime_aes_wrap(const MimeAes *kek, const uint8_t *in, size_t size, uint8_t *out);
int mime_aes_unwrap(const MimeAes *kek, const uint8_t *in, size_t size, uint8_t *out);

/* gcm over aes, streaming: every update but the last is whole blocks. */
typedef struct MimeGcm {
    uint8_t h[16];
    uint8_t j0[16];
    uint8_t counter[16];
    uint8_t s[16];
    uint64_t aad_size;
    uint64_t data_size;
} MimeGcm;

void mime_gcm_init(MimeGcm *g, const MimeAes *aes, const uint8_t *nonce, size_t nonce_size);
void mime_gcm_aad(MimeGcm *g, const uint8_t *aad, size_t size);
void mime_gcm_encrypt(MimeGcm *g, const MimeAes *aes, const uint8_t *in, uint8_t *out, size_t size);
void mime_gcm_decrypt(MimeGcm *g, const MimeAes *aes, const uint8_t *in, uint8_t *out, size_t size);
void mime_gcm_tag(MimeGcm *g, const MimeAes *aes, uint8_t *tag, size_t size);

/* ccm per sp 800-38c: nonce of 7 to 13 bytes, even tags of 4 to 16. */
int mime_ccm_encrypt(const MimeAes *aes, const uint8_t *nonce, size_t nonce_size, const uint8_t *aad,
                     size_t aad_size, const uint8_t *in, uint8_t *out, size_t size, uint8_t *tag, size_t tag_size);
int mime_ccm_decrypt(const MimeAes *aes, const uint8_t *nonce, size_t nonce_size, const uint8_t *aad,
                     size_t aad_size, const uint8_t *in, uint8_t *out, size_t size, const uint8_t *tag,
                     size_t tag_size);
bool mime_ccm_valid(size_t nonce_size, size_t tag_size);

/* ------------------------------------------------------------------ big numbers */

#define MIME_BN_LIMBS 520

/* an unsigned integer, little endian 32 bit limbs, n of them significant. */
typedef struct MimeBn {
    uint32_t n;
    uint32_t d[MIME_BN_LIMBS];
} MimeBn;

void mime_bn_zero(MimeBn *a);
void mime_bn_set(MimeBn *a, uint32_t value);
void mime_bn_copy(MimeBn *r, const MimeBn *a);
bool mime_bn_from_bytes(MimeBn *a, const uint8_t *be, size_t size);
/* big endian, left padded to size; false when it does not fit. */
bool mime_bn_to_bytes(const MimeBn *a, uint8_t *be, size_t size);
uint32_t mime_bn_bits(const MimeBn *a);
bool mime_bn_is_zero(const MimeBn *a);
bool mime_bn_is_one(const MimeBn *a);
int mime_bn_cmp(const MimeBn *a, const MimeBn *b);
int mime_bn_cmp_word(const MimeBn *a, uint32_t w);
bool mime_bn_add(MimeBn *r, const MimeBn *a, const MimeBn *b);
/* a >= b. */
void mime_bn_sub(MimeBn *r, const MimeBn *a, const MimeBn *b);
void mime_bn_add_word(MimeBn *r, uint32_t w);
void mime_bn_sub_word(MimeBn *r, uint32_t w);
void mime_bn_shr(MimeBn *r, const MimeBn *a, uint32_t bits);
bool mime_bn_mul(MimeBn *r, const MimeBn *a, const MimeBn *b);
/* quotient and remainder, either may be null; m is not zero. */
int mime_bn_divmod(MimeBn *q, MimeBn *r, const MimeBn *a, const MimeBn *m);
uint32_t mime_bn_mod_word(const MimeBn *a, uint32_t w);
/* base^exp mod m, m odd; secret exponents run a fixed window without data dependent branches or loads. */
int mime_bn_mod_exp(MimeBn *r, const MimeBn *base, const MimeBn *exp, const MimeBn *m, bool secret);
int mime_bn_mod_mul(MimeBn *r, const MimeBn *a, const MimeBn *b, const MimeBn *m);
int mime_bn_mod_inverse(MimeBn *r, const MimeBn *a, const MimeBn *m);
int mime_bn_gcd(MimeBn *r, const MimeBn *a, const MimeBn *b);
/* uniformly in [1, max). */
int mime_bn_random_below(MimeBn *r, const MimeBn *max);
int mime_bn_random_bits(MimeBn *r, uint32_t bits);
/* trial division then miller-rabin rounds with random bases; MIME_CRYPTO_OK when probably prime. */
int mime_bn_is_prime(const MimeBn *a, int rounds, bool *prime);

/* ------------------------------------------------------------------ rsa */

typedef struct MimeRsa {
    uint32_t bits;
    bool private_key;
    MimeBn n, e, d, p, q, dp, dq, qinv;
} MimeRsa;

MimeRsa *mime_rsa_new(void);
void mime_rsa_free(MimeRsa *rsa);
/* checks n and e and, with private parts, completes d, dp, dq and qinv from p and q. */
int mime_rsa_check(MimeRsa *rsa);
int mime_rsa_generate(MimeRsa *rsa, uint32_t bits);
/* raw operations on modulus sized big endian values. */
int mime_rsa_public(const MimeRsa *rsa, const uint8_t *in, uint8_t *out);
int mime_rsa_private(const MimeRsa *rsa, const uint8_t *in, uint8_t *out);
/* without alg the hash is signed bare, as when bcrypt is given no algorithm. */
int mime_rsa_pkcs1_sign(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *hash, size_t hash_size,
                        uint8_t *sig);
int mime_rsa_pkcs1_verify(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *hash, size_t hash_size,
                          const uint8_t *sig, size_t sig_size);
/* the same with a der DigestInfo prefix of the caller's choice, empty for a bare hash. */
int mime_rsa_pkcs1_sign_prefix(const MimeRsa *rsa, const uint8_t *prefix, size_t prefix_size, const uint8_t *hash,
                               size_t hash_size, uint8_t *sig);
int mime_rsa_pkcs1_verify_prefix(const MimeRsa *rsa, const uint8_t *prefix, size_t prefix_size,
                                 const uint8_t *hash, size_t hash_size, const uint8_t *sig, size_t sig_size);
int mime_rsa_pss_sign(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *hash, size_t hash_size,
                      size_t salt_size, uint8_t *sig);
int mime_rsa_pss_verify(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *hash, size_t hash_size,
                        size_t salt_size, const uint8_t *sig, size_t sig_size);
int mime_rsa_pkcs1_encrypt(const MimeRsa *rsa, const uint8_t *in, size_t size, uint8_t *out);
/* the message goes to out, which holds the modulus size. */
int mime_rsa_pkcs1_decrypt(const MimeRsa *rsa, const uint8_t *in, uint8_t *out, size_t *size);
int mime_rsa_oaep_encrypt(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *label, size_t label_size,
                          const uint8_t *in, size_t size, uint8_t *out);
int mime_rsa_oaep_decrypt(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *label, size_t label_size,
                          const uint8_t *in, uint8_t *out, size_t *size);

/* ------------------------------------------------------------------ elliptic curves */

typedef struct MimeEcCurve MimeEcCurve;

extern const MimeEcCurve mime_p256, mime_p384, mime_p521;

uint32_t mime_ec_bits(const MimeEcCurve *curve);
uint32_t mime_ec_size(const MimeEcCurve *curve);
const char *mime_ec_name(const MimeEcCurve *curve);
/* the prime, a, b, the generator, the order and the cofactor, each size bytes but the cofactor. */
void mime_ec_parameters(const MimeEcCurve *curve, uint8_t *p, uint8_t *a, uint8_t *b, uint8_t *gx, uint8_t *gy,
                        uint8_t *n);

typedef struct MimeEcKey {
    const MimeEcCurve *curve;
    bool private_key;
    uint8_t x[66], y[66], d[66];
} MimeEcKey;

int mime_ec_generate(MimeEcKey *key, const MimeEcCurve *curve);
/* fills the public point from d. */
int mime_ec_derive_public(MimeEcKey *key);
/* the point lies on the curve, in range, and is not infinity. */
int mime_ec_check_public(const MimeEcKey *key);
int mime_ecdsa_sign(const MimeEcKey *key, const uint8_t *hash, size_t hash_size, uint8_t *sig);
int mime_ecdsa_verify(const MimeEcKey *key, const uint8_t *hash, size_t hash_size, const uint8_t *sig,
                      size_t sig_size);
/* the x coordinate of d times the peer's point, big endian. */
int mime_ecdh(const MimeEcKey *key, const MimeEcKey *peer, uint8_t *secret);

/* ------------------------------------------------------------------ curve25519 */

/* x25519 of rfc 7748, every value 32 bytes little endian; MIME_CRYPTO_ERR_PARAM when the result is zero. */
int mime_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
/* the public value of a private scalar: the scalar times the base point 9. */
int mime_x25519_base(uint8_t out[32], const uint8_t scalar[32]);

/* ------------------------------------------------------------------ finite field diffie-hellman */

typedef struct MimeDh {
    uint32_t size;
    bool private_key;
    MimeBn p, g, q, x, y;
} MimeDh;

MimeDh *mime_dh_new(void);
void mime_dh_free(MimeDh *dh);
/* the rfc 7919 group of 2048, 3072 or 4096 bits, or a generated one below 2048. */
int mime_dh_group(MimeDh *dh, uint32_t bits);
/* a private value for the group of dh and its public value. */
int mime_dh_generate(MimeDh *dh);
int mime_dh_derive_public(MimeDh *dh);
/* the peer's value must lie in [2, p - 2]; the secret is size bytes, big endian. */
int mime_dh_agree(const MimeDh *dh, const MimeBn *peer, uint8_t *secret);

#endif
