/*
 * rsa of rfc 8017: the raw operations, with the chinese remainder theorem and
 * a check of the result for private keys, the pkcs #1 v1.5 and pss signature
 * encodings, pkcs #1 v1.5 and oaep encryption, and key generation.
 */
#include "internal.h"

MimeRsa *mime_rsa_new(void)
{
    MimeRsa *rsa = mime_crypto_alloc(sizeof(MimeRsa));
    if (rsa) {
        rsa->bits = 0;
        rsa->private_key = false;
        mime_bn_zero(&rsa->n);
        mime_bn_zero(&rsa->e);
        mime_bn_zero(&rsa->d);
        mime_bn_zero(&rsa->p);
        mime_bn_zero(&rsa->q);
        mime_bn_zero(&rsa->dp);
        mime_bn_zero(&rsa->dq);
        mime_bn_zero(&rsa->qinv);
    }
    return rsa;
}

void mime_rsa_free(MimeRsa *rsa)
{
    if (rsa) {
        mime_wipe(rsa, sizeof(*rsa));
        mime_crypto_free(rsa);
    }
}

static size_t modulus_size(const MimeRsa *rsa)
{
    return (rsa->bits + 7) / 8;
}

/* d, dp, dq and qinv from p, q and e, whichever are missing. */
static int complete(MimeRsa *rsa)
{
    MimeBn *t = mime_crypto_alloc(4 * sizeof(MimeBn));
    if (!t)
        return MIME_CRYPTO_ERR_NOMEM;
    MimeBn *p1 = t, *q1 = t + 1, *g = t + 2, *l = t + 3;
    mime_bn_copy(p1, &rsa->p);
    mime_bn_sub_word(p1, 1);
    mime_bn_copy(q1, &rsa->q);
    mime_bn_sub_word(q1, 1);
    int status = MIME_CRYPTO_OK;
    if (mime_bn_is_zero(&rsa->d)) {
        /* d is the inverse of e modulo the least common multiple of p - 1 and q - 1. */
        status = mime_bn_gcd(g, p1, q1);
        if (!status)
            status = mime_bn_mul(l, p1, q1) ? mime_bn_divmod(l, nullptr, l, g) : MIME_CRYPTO_ERR_PARAM;
        if (!status)
            status = mime_bn_mod_inverse(&rsa->d, &rsa->e, l);
    }
    if (!status && mime_bn_is_zero(&rsa->dp))
        status = mime_bn_divmod(nullptr, &rsa->dp, &rsa->d, p1);
    if (!status && mime_bn_is_zero(&rsa->dq))
        status = mime_bn_divmod(nullptr, &rsa->dq, &rsa->d, q1);
    if (!status && mime_bn_is_zero(&rsa->qinv))
        status = mime_bn_mod_inverse(&rsa->qinv, &rsa->q, &rsa->p);
    mime_wipe(t, 4 * sizeof(MimeBn));
    mime_crypto_free(t);
    return status;
}

int mime_rsa_check(MimeRsa *rsa)
{
    rsa->bits = mime_bn_bits(&rsa->n);
    if (rsa->bits < 256 || !(rsa->n.d[0] & 1) || mime_bn_cmp_word(&rsa->e, 3) < 0 || mime_bn_cmp(&rsa->e, &rsa->n) >= 0)
        return MIME_CRYPTO_ERR_PARAM;
    if (!rsa->private_key)
        return MIME_CRYPTO_OK;
    if (mime_bn_is_zero(&rsa->p) || mime_bn_is_zero(&rsa->q))
        return mime_bn_is_zero(&rsa->d) ? MIME_CRYPTO_ERR_PARAM : MIME_CRYPTO_OK;
    MimeBn *n = mime_crypto_alloc(sizeof(MimeBn));
    if (!n)
        return MIME_CRYPTO_ERR_NOMEM;
    int status = mime_bn_mul(n, &rsa->p, &rsa->q) && !mime_bn_cmp(n, &rsa->n) ? MIME_CRYPTO_OK : MIME_CRYPTO_ERR_PARAM;
    mime_crypto_free(n);
    if (!status)
        status = complete(rsa);
    return status;
}

int mime_rsa_public(const MimeRsa *rsa, const uint8_t *in, uint8_t *out)
{
    size_t k = modulus_size(rsa);
    MimeBn *x = mime_crypto_alloc(sizeof(MimeBn));
    if (!x)
        return MIME_CRYPTO_ERR_NOMEM;
    int status = MIME_CRYPTO_ERR_PARAM;
    if (mime_bn_from_bytes(x, in, k) && mime_bn_cmp(x, &rsa->n) < 0) {
        status = mime_bn_mod_exp(x, x, &rsa->e, &rsa->n, false);
        if (!status)
            mime_bn_to_bytes(x, out, k);
    }
    mime_wipe(x, sizeof(*x));
    mime_crypto_free(x);
    return status;
}

int mime_rsa_private(const MimeRsa *rsa, const uint8_t *in, uint8_t *out)
{
    size_t k = modulus_size(rsa);
    MimeBn *t = mime_crypto_alloc(5 * sizeof(MimeBn));
    if (!t)
        return MIME_CRYPTO_ERR_NOMEM;
    MimeBn *c = t, *m1 = t + 1, *m2 = t + 2, *h = t + 3, *check = t + 4;
    int status = MIME_CRYPTO_ERR_PARAM;
    if (!mime_bn_from_bytes(c, in, k) || mime_bn_cmp(c, &rsa->n) >= 0)
        goto done;
    if (!mime_bn_is_zero(&rsa->p) && !mime_bn_is_zero(&rsa->dp)) {
        /* m = m2 + q * [qinv * [m1 - m2] mod p]. */
        status = mime_bn_mod_exp(m1, c, &rsa->dp, &rsa->p, true);
        if (!status)
            status = mime_bn_mod_exp(m2, c, &rsa->dq, &rsa->q, true);
        if (!status)
            status = mime_bn_divmod(nullptr, h, m2, &rsa->p);
        if (status)
            goto done;
        if (mime_bn_cmp(m1, h) >= 0) {
            mime_bn_sub(h, m1, h);
        } else {
            mime_bn_sub(h, &rsa->p, h);
            mime_bn_add(h, h, m1);
        }
        status = mime_bn_mod_mul(h, h, &rsa->qinv, &rsa->p);
        if (!status)
            status = mime_bn_mul(h, h, &rsa->q) && mime_bn_add(m1, h, m2) ? MIME_CRYPTO_OK : MIME_CRYPTO_ERR_PARAM;
    } else {
        status = mime_bn_mod_exp(m1, c, &rsa->d, &rsa->n, true);
    }
    if (status)
        goto done;
    /* a fault in the computation must not leak a factor: verify before releasing. */
    status = mime_bn_mod_exp(check, m1, &rsa->e, &rsa->n, false);
    if (!status && mime_bn_cmp(check, c))
        status = MIME_CRYPTO_ERR_PARAM;
    if (!status)
        mime_bn_to_bytes(m1, out, k);
done:
    mime_wipe(t, 5 * sizeof(MimeBn));
    mime_crypto_free(t);
    return status;
}

/* ------------------------------------------------------------------ signatures */

static int private_bytes(const MimeRsa *rsa, const uint8_t *em, uint8_t *sig)
{
    return rsa->private_key ? mime_rsa_private(rsa, em, sig) : MIME_CRYPTO_ERR_PARAM;
}

static int pkcs1_sig_encode(const MimeRsa *rsa, const uint8_t *prefix, size_t prefix_size, const uint8_t *hash,
                            size_t hash_size, uint8_t *em)
{
    size_t k = modulus_size(rsa);
    if (prefix_size + hash_size + 11 > k)
        return MIME_CRYPTO_ERR_PARAM;
    size_t pad = k - prefix_size - hash_size - 3;
    em[0] = 0;
    em[1] = 1;
    memset(em + 2, 0xFF, pad);
    em[2 + pad] = 0;
    if (prefix_size)
        memcpy(em + 3 + pad, prefix, prefix_size);
    memcpy(em + 3 + pad + prefix_size, hash, hash_size);
    return MIME_CRYPTO_OK;
}

int mime_rsa_pkcs1_sign_prefix(const MimeRsa *rsa, const uint8_t *prefix, size_t prefix_size, const uint8_t *hash,
                               size_t hash_size, uint8_t *sig)
{
    uint8_t *em = mime_crypto_alloc(modulus_size(rsa));
    if (!em)
        return MIME_CRYPTO_ERR_NOMEM;
    int status = pkcs1_sig_encode(rsa, prefix, prefix_size, hash, hash_size, em);
    if (!status)
        status = private_bytes(rsa, em, sig);
    mime_crypto_free(em);
    return status;
}

int mime_rsa_pkcs1_verify_prefix(const MimeRsa *rsa, const uint8_t *prefix, size_t prefix_size,
                                 const uint8_t *hash, size_t hash_size, const uint8_t *sig, size_t sig_size)
{
    size_t k = modulus_size(rsa);
    if (sig_size != k)
        return MIME_CRYPTO_ERR_SIGNATURE;
    uint8_t *em = mime_crypto_alloc(2 * k);
    if (!em)
        return MIME_CRYPTO_ERR_NOMEM;
    int status = pkcs1_sig_encode(rsa, prefix, prefix_size, hash, hash_size, em);
    if (!status)
        status = mime_rsa_public(rsa, sig, em + k);
    if (status || memcmp(em, em + k, k))
        status = status == MIME_CRYPTO_ERR_NOMEM ? status : MIME_CRYPTO_ERR_SIGNATURE;
    mime_crypto_free(em);
    return status;
}

int mime_rsa_pkcs1_sign(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *hash, size_t hash_size,
                        uint8_t *sig)
{
    if (alg && hash_size != alg->digest_size)
        return MIME_CRYPTO_ERR_PARAM;
    return mime_rsa_pkcs1_sign_prefix(rsa, alg ? alg->digest_info : nullptr, alg ? alg->digest_info_size : 0, hash,
                                      hash_size, sig);
}

int mime_rsa_pkcs1_verify(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *hash, size_t hash_size,
                          const uint8_t *sig, size_t sig_size)
{
    if (alg && hash_size != alg->digest_size)
        return MIME_CRYPTO_ERR_SIGNATURE;
    return mime_rsa_pkcs1_verify_prefix(rsa, alg ? alg->digest_info : nullptr, alg ? alg->digest_info_size : 0, hash,
                                        hash_size, sig, sig_size);
}

static void mgf1(const MimeHashAlg *alg, const uint8_t *seed, size_t seed_size, uint8_t *mask, size_t size)
{
    uint8_t block[MIME_HASH_MAX_DIGEST];
    for (uint32_t counter = 0; size; counter++) {
        MimeHashCtx ctx;
        uint8_t c[4];
        store_be32(c, counter);
        alg->init(&ctx);
        alg->update(&ctx, seed, seed_size);
        alg->update(&ctx, c, 4);
        alg->final(&ctx, block);
        size_t n = size < alg->digest_size ? size : alg->digest_size;
        for (size_t i = 0; i < n; i++)
            mask[i] ^= block[i];
        mask += n;
        size -= n;
    }
}

static void pss_hash(const MimeHashAlg *alg, const uint8_t *hash, size_t hash_size, const uint8_t *salt,
                     size_t salt_size, uint8_t *out)
{
    static const uint8_t zeros[8] = {};
    MimeHashCtx ctx;
    alg->init(&ctx);
    alg->update(&ctx, zeros, 8);
    alg->update(&ctx, hash, hash_size);
    alg->update(&ctx, salt, salt_size);
    alg->final(&ctx, out);
}

int mime_rsa_pss_sign(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *hash, size_t hash_size,
                      size_t salt_size, uint8_t *sig)
{
    size_t k = modulus_size(rsa), em_bits = rsa->bits - 1, em_size = (em_bits + 7) / 8, h = alg->digest_size;
    if (hash_size != h || em_size < h + salt_size + 2)
        return MIME_CRYPTO_ERR_PARAM;
    uint8_t *em = mime_crypto_alloc(k + salt_size);
    if (!em)
        return MIME_CRYPTO_ERR_NOMEM;
    uint8_t *salt = em + k;
    int status = MIME_CRYPTO_OK;
    if (salt_size && !mime_crypto_random(salt, salt_size))
        status = MIME_CRYPTO_ERR_RANDOM;
    if (!status) {
        /* em is left padded to the modulus size when em_bits is a multiple of 8. */
        uint8_t *e = em + (k - em_size);
        memset(em, 0, k);
        size_t db_size = em_size - h - 1;
        pss_hash(alg, hash, hash_size, salt, salt_size, e + db_size);
        e[db_size - salt_size - 1] = 1;
        memcpy(e + db_size - salt_size, salt, salt_size);
        mgf1(alg, e + db_size, h, e, db_size);
        e[0] &= (uint8_t)(0xFF >> (8 * em_size - em_bits));
        e[em_size - 1] = 0xBC;
        status = private_bytes(rsa, em, sig);
    }
    mime_wipe(em, k + salt_size);
    mime_crypto_free(em);
    return status;
}

int mime_rsa_pss_verify(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *hash, size_t hash_size,
                        size_t salt_size, const uint8_t *sig, size_t sig_size)
{
    size_t k = modulus_size(rsa), em_bits = rsa->bits - 1, em_size = (em_bits + 7) / 8, h = alg->digest_size;
    if (sig_size != k || hash_size != h || em_size < h + salt_size + 2)
        return MIME_CRYPTO_ERR_SIGNATURE;
    uint8_t *em = mime_crypto_alloc(k);
    if (!em)
        return MIME_CRYPTO_ERR_NOMEM;
    int status = mime_rsa_public(rsa, sig, em);
    if (status) {
        status = MIME_CRYPTO_ERR_SIGNATURE;
        goto done;
    }
    uint8_t *e = em + (k - em_size);
    status = MIME_CRYPTO_ERR_SIGNATURE;
    if ((k > em_size && em[0]) || e[em_size - 1] != 0xBC || (e[0] & ~(0xFF >> (8 * em_size - em_bits))))
        goto done;
    size_t db_size = em_size - h - 1;
    mgf1(alg, e + db_size, h, e, db_size);
    e[0] &= (uint8_t)(0xFF >> (8 * em_size - em_bits));
    for (size_t i = 0; i < db_size - salt_size - 1; i++)
        if (e[i])
            goto done;
    if (e[db_size - salt_size - 1] != 1)
        goto done;
    uint8_t check[MIME_HASH_MAX_DIGEST];
    pss_hash(alg, hash, hash_size, e + db_size - salt_size, salt_size, check);
    if (!memcmp(check, e + db_size, h))
        status = MIME_CRYPTO_OK;
done:
    mime_crypto_free(em);
    return status;
}

/* ------------------------------------------------------------------ encryption */

int mime_rsa_pkcs1_encrypt(const MimeRsa *rsa, const uint8_t *in, size_t size, uint8_t *out)
{
    size_t k = modulus_size(rsa);
    if (size + 11 > k)
        return MIME_CRYPTO_ERR_PARAM;
    uint8_t *em = mime_crypto_alloc(k);
    if (!em)
        return MIME_CRYPTO_ERR_NOMEM;
    size_t pad = k - size - 3;
    em[0] = 0;
    em[1] = 2;
    int status = MIME_CRYPTO_OK;
    for (size_t i = 0; i < pad && !status; i++) {
        do {
            if (!mime_crypto_random(em + 2 + i, 1))
                status = MIME_CRYPTO_ERR_RANDOM;
        } while (!status && !em[2 + i]);
    }
    em[2 + pad] = 0;
    memcpy(em + 3 + pad, in, size);
    if (!status)
        status = mime_rsa_public(rsa, em, out);
    mime_wipe(em, k);
    mime_crypto_free(em);
    return status;
}

int mime_rsa_pkcs1_decrypt(const MimeRsa *rsa, const uint8_t *in, uint8_t *out, size_t *size)
{
    size_t k = modulus_size(rsa);
    if (k < 11)
        return MIME_CRYPTO_ERR_PARAM;
    uint8_t *em = mime_crypto_alloc(k);
    if (!em)
        return MIME_CRYPTO_ERR_NOMEM;
    int status = private_bytes(rsa, in, em);
    if (!status) {
        /* find the separator without branching on the padding bytes. */
        uint32_t bad = ct_nonzero(em[0]) | ct_nonzero(em[1] ^ 2), found = 0, index = 0;
        for (size_t i = 2; i < k; i++) {
            uint32_t zero = ~ct_nonzero(em[i]) & ~found;
            index |= (uint32_t)i & zero;
            found |= zero;
        }
        bad |= ~found;
        bad |= (uint32_t)0 - (uint32_t)(index < 10);
        if (bad) {
            status = MIME_CRYPTO_ERR_DATA;
        } else {
            *size = k - index - 1;
            memmove(out, em + index + 1, *size);
        }
    }
    mime_wipe(em, k);
    mime_crypto_free(em);
    return status;
}

int mime_rsa_oaep_encrypt(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *label, size_t label_size,
                          const uint8_t *in, size_t size, uint8_t *out)
{
    size_t k = modulus_size(rsa), h = alg->digest_size;
    if (k < 2 * h + 2 || size > k - 2 * h - 2)
        return MIME_CRYPTO_ERR_PARAM;
    uint8_t *em = mime_crypto_alloc(k);
    if (!em)
        return MIME_CRYPTO_ERR_NOMEM;
    uint8_t *seed = em + 1, *db = em + 1 + h;
    size_t db_size = k - h - 1;
    memset(em, 0, k);
    mime_hash(alg, label, label_size, db);
    db[db_size - size - 1] = 1;
    memcpy(db + db_size - size, in, size);
    int status = mime_crypto_random(seed, h) ? MIME_CRYPTO_OK : MIME_CRYPTO_ERR_RANDOM;
    if (!status) {
        mgf1(alg, seed, h, db, db_size);
        mgf1(alg, db, db_size, seed, h);
        status = mime_rsa_public(rsa, em, out);
    }
    mime_wipe(em, k);
    mime_crypto_free(em);
    return status;
}

int mime_rsa_oaep_decrypt(const MimeRsa *rsa, const MimeHashAlg *alg, const uint8_t *label, size_t label_size,
                          const uint8_t *in, uint8_t *out, size_t *size)
{
    size_t k = modulus_size(rsa), h = alg->digest_size;
    if (k < 2 * h + 2)
        return MIME_CRYPTO_ERR_PARAM;
    uint8_t *em = mime_crypto_alloc(k);
    if (!em)
        return MIME_CRYPTO_ERR_NOMEM;
    int status = private_bytes(rsa, in, em);
    if (!status) {
        uint8_t *seed = em + 1, *db = em + 1 + h, lhash[MIME_HASH_MAX_DIGEST];
        size_t db_size = k - h - 1;
        mgf1(alg, db, db_size, seed, h);
        mgf1(alg, seed, h, db, db_size);
        mime_hash(alg, label, label_size, lhash);
        uint32_t bad = ct_nonzero(em[0]);
        for (size_t i = 0; i < h; i++)
            bad |= ct_nonzero(db[i] ^ lhash[i]);
        uint32_t found = 0, index = 0;
        for (size_t i = h; i < db_size; i++) {
            uint32_t one = ~ct_nonzero(db[i] ^ 1) & ~found;
            uint32_t other = ct_nonzero(db[i]) & ~found & ~one;
            index |= (uint32_t)i & one;
            found |= one;
            bad |= other;
        }
        bad |= ~found;
        if (bad) {
            status = MIME_CRYPTO_ERR_DATA;
        } else {
            *size = db_size - index - 1;
            memmove(out, db + index + 1, *size);
        }
    }
    mime_wipe(em, k);
    mime_crypto_free(em);
    return status;
}

/* ------------------------------------------------------------------ key generation */

/* a prime of exactly bits bits, its top two bits set, with p - 1 coprime to e. */
static int generate_prime(MimeBn *p, uint32_t bits, const MimeBn *e, MimeBn *scratch)
{
    int rounds = bits >= 1024 ? 5 : bits >= 512 ? 8 : 16;
    for (int tries = 0; tries < 100000; tries++) {
        int status = mime_bn_random_bits(p, bits);
        if (status)
            return status;
        if (p->n < (bits + 31) / 32)
            continue;
        uint32_t top = bits - 1;
        p->d[top / 32] |= 1u << (top % 32);
        p->d[(top - 1) / 32] |= 1u << ((top - 1) % 32);
        p->d[0] |= 1;
        bool prime;
        status = mime_bn_is_prime(p, rounds, &prime);
        if (status)
            return status;
        if (!prime)
            continue;
        mime_bn_copy(scratch, p);
        mime_bn_sub_word(scratch, 1);
        if (mime_bn_gcd(scratch, scratch, e))
            return MIME_CRYPTO_ERR_NOMEM;
        if (mime_bn_is_one(scratch))
            return MIME_CRYPTO_OK;
    }
    return MIME_CRYPTO_ERR_RANDOM;
}

int mime_rsa_generate(MimeRsa *rsa, uint32_t bits)
{
    if (bits < 256 || bits > MIME_BN_LIMBS * 32 - 256)
        return MIME_CRYPTO_ERR_PARAM;
    MimeBn *scratch = mime_crypto_alloc(sizeof(MimeBn));
    if (!scratch)
        return MIME_CRYPTO_ERR_NOMEM;
    int status;
    mime_bn_set(&rsa->e, 65537);
    do {
        status = generate_prime(&rsa->p, bits - bits / 2, &rsa->e, scratch);
        if (!status)
            status = generate_prime(&rsa->q, bits / 2, &rsa->e, scratch);
        if (!status && !mime_bn_mul(&rsa->n, &rsa->p, &rsa->q))
            status = MIME_CRYPTO_ERR_PARAM;
    } while (!status && (mime_bn_bits(&rsa->n) != bits || !mime_bn_cmp(&rsa->p, &rsa->q)));
    mime_crypto_free(scratch);
    if (status)
        return status;
    rsa->bits = bits;
    rsa->private_key = true;
    mime_bn_zero(&rsa->d);
    mime_bn_zero(&rsa->dp);
    mime_bn_zero(&rsa->dq);
    mime_bn_zero(&rsa->qinv);
    return complete(rsa);
}
