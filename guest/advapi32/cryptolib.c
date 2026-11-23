/* what guest/lib/crypto takes from the library it is linked into: memory and random bytes. */
#include "advapi32_private.h"
#include <crypto/crypto.h>

void *mime_crypto_alloc(size_t size)
{
    return AdvAlloc(size);
}

void mime_crypto_free(void *p)
{
    AdvFree(p);
}

bool mime_crypto_random(void *out, size_t size)
{
    return AdvGenRandom(out, size);
}
