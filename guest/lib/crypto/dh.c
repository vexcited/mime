/*
 * finite field diffie-hellman: the rfc 7919 groups for 2048 bits and more, a
 * fips 186 style group with a prime order subgroup below that, and the key
 * agreement itself.
 */
#include "internal.h"

#include "dh_groups.inc"

MimeDh *mime_dh_new(void)
{
    MimeDh *dh = mime_crypto_alloc(sizeof(MimeDh));
    if (dh) {
        dh->size = 0;
        dh->private_key = false;
        mime_bn_zero(&dh->p);
        mime_bn_zero(&dh->g);
        mime_bn_zero(&dh->q);
        mime_bn_zero(&dh->x);
        mime_bn_zero(&dh->y);
    }
    return dh;
}

void mime_dh_free(MimeDh *dh)
{
    if (dh) {
        mime_wipe(dh, sizeof(*dh));
        mime_crypto_free(dh);
    }
}

/* q prime of qbits, then p = 2kq + 1 prime of bits, then g as h to the power p - 1 over q for the first h giving more than 1. */
static int generate_group(MimeDh *dh, uint32_t bits)
{
    uint32_t qbits = bits <= 1024 ? 160 : 256;
    MimeBn *t = mime_crypto_alloc(3 * sizeof(MimeBn));
    if (!t)
        return MIME_CRYPTO_ERR_NOMEM;
    MimeBn *twoq = t, *rem = t + 1, *e = t + 2;
    int status;
    bool prime = false;
    do {
        status = mime_bn_random_bits(&dh->q, qbits);
        if (status)
            goto done;
        if (dh->q.n < (qbits + 31) / 32)
            continue;
        dh->q.d[(qbits - 1) / 32] |= 1u << ((qbits - 1) % 32);
        dh->q.d[0] |= 1;
        status = mime_bn_is_prime(&dh->q, 20, &prime);
    } while (!status && !prime);
    if (status)
        goto done;
    mime_bn_add(twoq, &dh->q, &dh->q);
    for (int tries = 0; tries < 100000; tries++) {
        status = mime_bn_random_bits(&dh->p, bits);
        if (status)
            goto done;
        if (dh->p.n < (bits + 31) / 32)
            continue;
        dh->p.d[(bits - 1) / 32] |= 1u << ((bits - 1) % 32);
        status = mime_bn_divmod(nullptr, rem, &dh->p, twoq);
        if (status)
            goto done;
        mime_bn_sub(&dh->p, &dh->p, rem);
        mime_bn_add_word(&dh->p, 1);
        if (mime_bn_bits(&dh->p) != bits)
            continue;
        status = mime_bn_is_prime(&dh->p, bits >= 1024 ? 5 : 8, &prime);
        if (status)
            goto done;
        if (prime)
            break;
    }
    if (!prime) {
        status = MIME_CRYPTO_ERR_RANDOM;
        goto done;
    }
    mime_bn_copy(e, &dh->p);
    mime_bn_sub_word(e, 1);
    status = mime_bn_divmod(e, nullptr, e, &dh->q);
    for (uint32_t h = 2; !status; h++) {
        MimeBn *base = rem;
        mime_bn_set(base, h);
        status = mime_bn_mod_exp(&dh->g, base, e, &dh->p, false);
        if (!status && mime_bn_cmp_word(&dh->g, 1) > 0)
            break;
    }
done:
    mime_wipe(t, 3 * sizeof(MimeBn));
    mime_crypto_free(t);
    return status;
}

int mime_dh_group(MimeDh *dh, uint32_t bits)
{
    const uint8_t *prime = bits == 2048 ? ffdhe2048 : bits == 3072 ? ffdhe3072 : bits == 4096 ? ffdhe4096 : nullptr;
    dh->size = bits / 8;
    mime_bn_zero(&dh->q);
    if (prime) {
        mime_bn_from_bytes(&dh->p, prime, bits / 8);
        mime_bn_set(&dh->g, 2);
        return MIME_CRYPTO_OK;
    }
    if (bits >= 2048 || bits < 512 || bits % 64)
        return MIME_CRYPTO_ERR_PARAM;
    return generate_group(dh, bits);
}

int mime_dh_derive_public(MimeDh *dh)
{
    return mime_bn_mod_exp(&dh->y, &dh->g, &dh->x, &dh->p, true);
}

int mime_dh_generate(MimeDh *dh)
{
    MimeBn *limit = mime_crypto_alloc(sizeof(MimeBn));
    if (!limit)
        return MIME_CRYPTO_ERR_NOMEM;
    /* in [1, q) for a group with a known order, else in [1, p - 1). */
    if (!mime_bn_is_zero(&dh->q)) {
        mime_bn_copy(limit, &dh->q);
    } else {
        mime_bn_copy(limit, &dh->p);
        mime_bn_sub_word(limit, 1);
    }
    int status = mime_bn_random_below(&dh->x, limit);
    mime_crypto_free(limit);
    if (status)
        return status;
    dh->private_key = true;
    return mime_dh_derive_public(dh);
}

int mime_dh_agree(const MimeDh *dh, const MimeBn *peer, uint8_t *secret)
{
    if (!dh->private_key)
        return MIME_CRYPTO_ERR_PARAM;
    MimeBn *t = mime_crypto_alloc(2 * sizeof(MimeBn));
    if (!t)
        return MIME_CRYPTO_ERR_NOMEM;
    MimeBn *limit = t, *z = t + 1;
    mime_bn_copy(limit, &dh->p);
    mime_bn_sub_word(limit, 1);
    int status = MIME_CRYPTO_ERR_PARAM;
    if (mime_bn_cmp_word(peer, 2) >= 0 && mime_bn_cmp(peer, limit) < 0) {
        status = mime_bn_mod_exp(z, peer, &dh->x, &dh->p, true);
        if (!status)
            mime_bn_to_bytes(z, secret, dh->size);
    }
    mime_wipe(t, 2 * sizeof(MimeBn));
    mime_crypto_free(t);
    return status;
}
