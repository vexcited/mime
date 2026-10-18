/* pbkdf2 of rfc 8018, hkdf of rfc 5869, sp 800-108 counter mode, CryptDeriveKey's expansion and the tls prfs. */
#include "internal.h"

void mime_pbkdf2(const MimeHashAlg *alg, const void *password, size_t password_size, const void *salt,
                 size_t salt_size, uint64_t iterations, uint8_t *out, size_t size)
{
    MimeHmac h;
    uint8_t u[MIME_HASH_MAX_DIGEST], t[MIME_HASH_MAX_DIGEST];
    mime_hmac_init(&h, alg, password, password_size);
    for (uint32_t block = 1; size; block++) {
        uint8_t index[4];
        store_be32(index, block);
        mime_hmac_update(&h, salt, salt_size);
        mime_hmac_update(&h, index, 4);
        mime_hmac_final(&h, u);
        memcpy(t, u, alg->digest_size);
        for (uint64_t i = 1; i < iterations; i++) {
            mime_hmac_update(&h, u, alg->digest_size);
            mime_hmac_final(&h, u);
            xor_block(t, t, u, alg->digest_size);
        }
        size_t n = size < alg->digest_size ? size : alg->digest_size;
        memcpy(out, t, n);
        out += n;
        size -= n;
    }
    mime_wipe(&h, sizeof(h));
    mime_wipe(u, sizeof(u));
    mime_wipe(t, sizeof(t));
}

void mime_hkdf_extract(const MimeHashAlg *alg, const void *salt, size_t salt_size, const void *ikm, size_t ikm_size,
                       uint8_t *prk)
{
    uint8_t zeros[MIME_HASH_MAX_DIGEST] = {};
    if (!salt || !salt_size) {
        salt = zeros;
        salt_size = alg->digest_size;
    }
    mime_hmac(alg, salt, salt_size, ikm, ikm_size, prk);
}

int mime_hkdf_expand(const MimeHashAlg *alg, const void *prk, size_t prk_size, const void *info, size_t info_size,
                     uint8_t *out, size_t size)
{
    if (size > 255 * (size_t)alg->digest_size)
        return MIME_CRYPTO_ERR_PARAM;
    MimeHmac h;
    uint8_t t[MIME_HASH_MAX_DIGEST];
    size_t t_size = 0;
    mime_hmac_init(&h, alg, prk, prk_size);
    for (uint8_t counter = 1; size; counter++) {
        mime_hmac_update(&h, t, t_size);
        mime_hmac_update(&h, info, info_size);
        mime_hmac_update(&h, &counter, 1);
        mime_hmac_final(&h, t);
        t_size = alg->digest_size;
        size_t n = size < t_size ? size : t_size;
        memcpy(out, t, n);
        out += n;
        size -= n;
    }
    mime_wipe(&h, sizeof(h));
    mime_wipe(t, sizeof(t));
    return MIME_CRYPTO_OK;
}

void mime_sp800108_hmac(const MimeHashAlg *alg, const void *key, size_t key_size, const void *label,
                        size_t label_size, const void *context, size_t context_size, uint8_t *out, size_t size)
{
    MimeHmac h;
    uint8_t t[MIME_HASH_MAX_DIGEST], length[4], zero = 0;
    store_be32(length, (uint32_t)(size * 8));
    mime_hmac_init(&h, alg, key, key_size);
    for (uint32_t i = 1; size; i++) {
        uint8_t counter[4];
        store_be32(counter, i);
        mime_hmac_update(&h, counter, 4);
        mime_hmac_update(&h, label, label_size);
        mime_hmac_update(&h, &zero, 1);
        mime_hmac_update(&h, context, context_size);
        mime_hmac_update(&h, length, 4);
        mime_hmac_final(&h, t);
        size_t n = size < alg->digest_size ? size : alg->digest_size;
        memcpy(out, t, n);
        out += n;
        size -= n;
    }
    mime_wipe(&h, sizeof(h));
    mime_wipe(t, sizeof(t));
}

void mime_capi_expand(const MimeHashAlg *alg, const uint8_t *hash, uint8_t *out, size_t size)
{
    if (size <= alg->digest_size) {
        memcpy(out, hash, size);
        return;
    }
    uint8_t buffer[64], derived[2 * MIME_HASH_MAX_DIGEST];
    for (int pass = 0; pass < 2; pass++) {
        memset(buffer, pass ? 0x5c : 0x36, sizeof(buffer));
        for (uint32_t i = 0; i < alg->digest_size && i < sizeof(buffer); i++)
            buffer[i] ^= hash[i];
        mime_hash(alg, buffer, sizeof(buffer), derived + pass * alg->digest_size);
    }
    memcpy(out, derived, size <= 2 * alg->digest_size ? size : 2 * alg->digest_size);
    mime_wipe(buffer, sizeof(buffer));
    mime_wipe(derived, sizeof(derived));
}

static void p_hash(const MimeHashAlg *alg, const void *secret, size_t secret_size, const void *label,
                   size_t label_size, const void *seed, size_t seed_size, uint8_t *out, size_t size, bool combine)
{
    MimeHmac h;
    uint8_t a[MIME_HASH_MAX_DIGEST], t[MIME_HASH_MAX_DIGEST];
    mime_hmac_init(&h, alg, secret, secret_size);
    mime_hmac_update(&h, label, label_size);
    mime_hmac_update(&h, seed, seed_size);
    mime_hmac_final(&h, a);
    while (size) {
        mime_hmac_update(&h, a, alg->digest_size);
        mime_hmac_update(&h, label, label_size);
        mime_hmac_update(&h, seed, seed_size);
        mime_hmac_final(&h, t);
        size_t n = size < alg->digest_size ? size : alg->digest_size;
        for (size_t i = 0; i < n; i++)
            out[i] = combine ? out[i] ^ t[i] : t[i];
        out += n;
        size -= n;
        mime_hmac_update(&h, a, alg->digest_size);
        mime_hmac_final(&h, a);
    }
    mime_wipe(&h, sizeof(h));
    mime_wipe(a, sizeof(a));
    mime_wipe(t, sizeof(t));
}

void mime_tls_prf(const MimeHashAlg *alg, const void *secret, size_t secret_size, const void *label,
                  size_t label_size, const void *seed, size_t seed_size, uint8_t *out, size_t size)
{
    if (alg) {
        p_hash(alg, secret, secret_size, label, label_size, seed, seed_size, out, size, false);
        return;
    }
    size_t half = (secret_size + 1) / 2;
    const uint8_t *s = secret;
    p_hash(&mime_md5, s, half, label, label_size, seed, seed_size, out, size, false);
    p_hash(&mime_sha1, s + secret_size - half, half, label, label_size, seed, seed_size, out, size, true);
}
