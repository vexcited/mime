/* hmac of rfc 2104 over any of the hashes, keeping the padded key states for reuse. */
#include "internal.h"

size_t mime_hmac_size(const MimeHashAlg *alg)
{
    return offsetof(MimeHmac, ctx) + mime_hash_ctx_size(alg);
}

void mime_hmac_init(MimeHmac *h, const MimeHashAlg *alg, const void *key, size_t size)
{
    uint8_t pad[MIME_HASH_MAX_BLOCK];
    memset(pad, 0, sizeof(pad));
    if (size > alg->block_size)
        mime_hash(alg, key, size, pad);
    else if (size)
        memcpy(pad, key, size);
    h->alg = alg;
    for (uint32_t i = 0; i < alg->block_size; i++)
        pad[i] ^= 0x36;
    alg->init(&h->ctx);
    alg->update(&h->ctx, pad, alg->block_size);
    mime_hash_save(alg, &h->ctx, h->inner);
    for (uint32_t i = 0; i < alg->block_size; i++)
        pad[i] ^= 0x36 ^ 0x5c;
    alg->init(&h->ctx);
    alg->update(&h->ctx, pad, alg->block_size);
    mime_hash_save(alg, &h->ctx, h->outer);
    mime_wipe(pad, sizeof(pad));
    mime_hmac_reset(h);
}

void mime_hmac_reset(MimeHmac *h)
{
    mime_hash_resume(h->alg, &h->ctx, h->inner, 1);
}

void mime_hmac_update(MimeHmac *h, const void *data, size_t size)
{
    h->alg->update(&h->ctx, data, size);
}

void mime_hmac_final(MimeHmac *h, uint8_t *out)
{
    uint8_t inner[MIME_HASH_MAX_DIGEST];
    h->alg->final(&h->ctx, inner);
    mime_hash_resume(h->alg, &h->ctx, h->outer, 1);
    h->alg->update(&h->ctx, inner, h->alg->digest_size);
    h->alg->final(&h->ctx, out);
    mime_wipe(inner, sizeof(inner));
    mime_hmac_reset(h);
}

void mime_hmac(const MimeHashAlg *alg, const void *key, size_t key_size, const void *data, size_t size,
               uint8_t *out)
{
    MimeHmac h;
    mime_hmac_init(&h, alg, key, key_size);
    mime_hmac_update(&h, data, size);
    mime_hmac_final(&h, out);
    mime_wipe(&h, sizeof(h));
}
