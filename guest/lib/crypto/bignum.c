/*
 * unsigned big integers on 32 bit limbs: the arithmetic rsa, diffie-hellman
 * and key generation need, with montgomery multiplication for modular
 * exponentiation. exponents marked secret run a fixed window whose table is
 * read whole for every lookup, and the montgomery reduction subtracts
 * without branching on the value.
 */
#include "internal.h"

static void trim(MimeBn *a)
{
    while (a->n && !a->d[a->n - 1])
        a->n--;
}

void mime_bn_zero(MimeBn *a)
{
    a->n = 0;
}

void mime_bn_set(MimeBn *a, uint32_t value)
{
    a->d[0] = value;
    a->n = value ? 1 : 0;
}

void mime_bn_copy(MimeBn *r, const MimeBn *a)
{
    if (r != a) {
        r->n = a->n;
        memcpy(r->d, a->d, a->n * sizeof(uint32_t));
    }
}

bool mime_bn_from_bytes(MimeBn *a, const uint8_t *be, size_t size)
{
    while (size && !*be) {
        be++;
        size--;
    }
    if (size > MIME_BN_LIMBS * 4)
        return false;
    a->n = (uint32_t)((size + 3) / 4);
    memset(a->d, 0, a->n * sizeof(uint32_t));
    for (size_t i = 0; i < size; i++)
        a->d[i / 4] |= (uint32_t)be[size - 1 - i] << (8 * (i % 4));
    return true;
}

bool mime_bn_to_bytes(const MimeBn *a, uint8_t *be, size_t size)
{
    if ((mime_bn_bits(a) + 7) / 8 > size)
        return false;
    for (size_t i = 0; i < size; i++) {
        size_t limb = i / 4;
        be[size - 1 - i] = limb < a->n ? (uint8_t)(a->d[limb] >> (8 * (i % 4))) : 0;
    }
    return true;
}

uint32_t mime_bn_bits(const MimeBn *a)
{
    if (!a->n)
        return 0;
    uint32_t top = a->d[a->n - 1], bits = 32 * (a->n - 1);
    while (top) {
        bits++;
        top >>= 1;
    }
    return bits;
}

bool mime_bn_is_zero(const MimeBn *a)
{
    return a->n == 0;
}

bool mime_bn_is_one(const MimeBn *a)
{
    return a->n == 1 && a->d[0] == 1;
}

static int cmp_limbs(const uint32_t *a, uint32_t an, const uint32_t *b, uint32_t bn)
{
    if (an != bn)
        return an < bn ? -1 : 1;
    for (uint32_t i = an; i; i--)
        if (a[i - 1] != b[i - 1])
            return a[i - 1] < b[i - 1] ? -1 : 1;
    return 0;
}

int mime_bn_cmp(const MimeBn *a, const MimeBn *b)
{
    return cmp_limbs(a->d, a->n, b->d, b->n);
}

int mime_bn_cmp_word(const MimeBn *a, uint32_t w)
{
    if (a->n > 1)
        return 1;
    uint32_t v = a->n ? a->d[0] : 0;
    return v == w ? 0 : v < w ? -1 : 1;
}

bool mime_bn_add(MimeBn *r, const MimeBn *a, const MimeBn *b)
{
    if (a->n < b->n) {
        const MimeBn *t = a;
        a = b;
        b = t;
    }
    uint64_t carry = 0;
    uint32_t n = a->n;
    for (uint32_t i = 0; i < n; i++) {
        carry += (uint64_t)a->d[i] + (i < b->n ? b->d[i] : 0);
        r->d[i] = (uint32_t)carry;
        carry >>= 32;
    }
    if (carry) {
        if (n == MIME_BN_LIMBS)
            return false;
        r->d[n++] = 1;
    }
    r->n = n;
    return true;
}

void mime_bn_sub(MimeBn *r, const MimeBn *a, const MimeBn *b)
{
    uint64_t borrow = 0;
    for (uint32_t i = 0; i < a->n; i++) {
        uint64_t v = (uint64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - borrow;
        r->d[i] = (uint32_t)v;
        borrow = (v >> 32) & 1;
    }
    r->n = a->n;
    trim(r);
}

void mime_bn_add_word(MimeBn *r, uint32_t w)
{
    for (uint32_t i = 0; w; i++) {
        if (i == r->n)
            r->d[r->n++] = 0;
        uint64_t v = (uint64_t)r->d[i] + w;
        r->d[i] = (uint32_t)v;
        w = (uint32_t)(v >> 32);
    }
}

void mime_bn_sub_word(MimeBn *r, uint32_t w)
{
    for (uint32_t i = 0; w && i < r->n; i++) {
        uint32_t old = r->d[i];
        r->d[i] = old - w;
        w = old < w;
    }
    trim(r);
}

void mime_bn_shr(MimeBn *r, const MimeBn *a, uint32_t bits)
{
    uint32_t words = bits / 32, shift = bits % 32;
    if (words >= a->n) {
        r->n = 0;
        return;
    }
    uint32_t n = a->n - words;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t lo = a->d[i + words] >> shift;
        uint32_t hi = shift && i + words + 1 < a->n ? a->d[i + words + 1] << (32 - shift) : 0;
        r->d[i] = lo | hi;
    }
    r->n = n;
    trim(r);
}

static void mul_limbs(uint32_t *r, const uint32_t *a, uint32_t an, const uint32_t *b, uint32_t bn)
{
    memset(r, 0, (an + bn) * sizeof(uint32_t));
    for (uint32_t i = 0; i < an; i++) {
        uint64_t carry = 0;
        for (uint32_t j = 0; j < bn; j++) {
            carry += (uint64_t)a[i] * b[j] + r[i + j];
            r[i + j] = (uint32_t)carry;
            carry >>= 32;
        }
        r[i + bn] = (uint32_t)carry;
    }
}

bool mime_bn_mul(MimeBn *r, const MimeBn *a, const MimeBn *b)
{
    if (a->n + b->n > MIME_BN_LIMBS)
        return false;
    uint32_t *t = mime_crypto_alloc((a->n + b->n + 1) * sizeof(uint32_t));
    if (!t)
        return false;
    mul_limbs(t, a->d, a->n, b->d, b->n);
    r->n = a->n + b->n;
    memcpy(r->d, t, r->n * sizeof(uint32_t));
    mime_crypto_free(t);
    trim(r);
    return true;
}

uint32_t mime_bn_mod_word(const MimeBn *a, uint32_t w)
{
    uint64_t r = 0;
    for (uint32_t i = a->n; i; i--)
        r = ((r << 32) | a->d[i - 1]) % w;
    return (uint32_t)r;
}

/* knuth's algorithm d: the divisor is normalized so its top bit is set, and each quotient limb is estimated from
 * the top two limbs, corrected at most twice. */
int mime_bn_divmod(MimeBn *q, MimeBn *r, const MimeBn *a, const MimeBn *m)
{
    if (!m->n)
        return MIME_CRYPTO_ERR_PARAM;
    if (mime_bn_cmp(a, m) < 0) {
        if (r)
            mime_bn_copy(r, a);
        if (q)
            q->n = 0;
        return MIME_CRYPTO_OK;
    }
    if (m->n == 1) {
        uint64_t rem = 0;
        uint32_t d = m->d[0], n = a->n;
        uint32_t *quot = q ? q->d : nullptr;
        for (uint32_t i = n; i; i--) {
            uint64_t cur = (rem << 32) | a->d[i - 1];
            if (quot)
                quot[i - 1] = (uint32_t)(cur / d);
            rem = cur % d;
        }
        if (q) {
            q->n = n;
            trim(q);
        }
        if (r)
            mime_bn_set(r, (uint32_t)rem);
        return MIME_CRYPTO_OK;
    }
    uint32_t n = m->n, total = a->n, mlen = total - n;
    uint32_t *buffer = mime_crypto_alloc((total + 1 + n + mlen + 1) * sizeof(uint32_t));
    if (!buffer)
        return MIME_CRYPTO_ERR_NOMEM;
    uint32_t *u = buffer, *v = u + total + 1, *quot = v + n;
    int shift = 0;
    for (uint32_t top = m->d[n - 1]; !(top & 0x80000000u); top <<= 1)
        shift++;
    for (uint32_t i = n; i; i--)
        v[i - 1] = (m->d[i - 1] << shift) | (shift && i > 1 ? m->d[i - 2] >> (32 - shift) : 0);
    u[total] = shift ? a->d[total - 1] >> (32 - shift) : 0;
    for (uint32_t i = total; i; i--)
        u[i - 1] = (a->d[i - 1] << shift) | (shift && i > 1 ? a->d[i - 2] >> (32 - shift) : 0);
    for (uint32_t j = mlen + 1; j; j--) {
        uint32_t jj = j - 1;
        uint64_t num = ((uint64_t)u[jj + n] << 32) | u[jj + n - 1];
        uint64_t qhat = num / v[n - 1], rhat = num % v[n - 1];
        while (qhat >> 32 || qhat * v[n - 2] > ((rhat << 32) | u[jj + n - 2])) {
            qhat--;
            rhat += v[n - 1];
            if (rhat >> 32)
                break;
        }
        int64_t borrow = 0;
        uint64_t carry = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t p = qhat * v[i] + carry;
            carry = p >> 32;
            int64_t t = (int64_t)u[i + jj] - (int64_t)(uint32_t)p + borrow;
            u[i + jj] = (uint32_t)t;
            borrow = t >> 32;
        }
        int64_t t = (int64_t)u[jj + n] - (int64_t)carry + borrow;
        u[jj + n] = (uint32_t)t;
        quot[jj] = (uint32_t)qhat;
        if (t < 0) {
            quot[jj]--;
            uint64_t c = 0;
            for (uint32_t i = 0; i < n; i++) {
                c += (uint64_t)u[i + jj] + v[i];
                u[i + jj] = (uint32_t)c;
                c >>= 32;
            }
            u[jj + n] += (uint32_t)c;
        }
    }
    if (q) {
        q->n = mlen + 1;
        memcpy(q->d, quot, q->n * sizeof(uint32_t));
        trim(q);
    }
    if (r) {
        for (uint32_t i = 0; i < n; i++)
            r->d[i] = (u[i] >> shift) | (shift ? u[i + 1] << (32 - shift) : 0);
        r->n = n;
        trim(r);
    }
    mime_wipe(buffer, (total + 1 + n + mlen + 1) * sizeof(uint32_t));
    mime_crypto_free(buffer);
    return MIME_CRYPTO_OK;
}

/* ------------------------------------------------------------------ montgomery arithmetic */

typedef struct Mont {
    uint32_t k;
    uint32_t m0inv;
    const uint32_t *m;
    uint32_t *r2;
    uint32_t *t;
    uint32_t *one;
} Mont;

/* out = a * b / r mod m on k limbs, the final subtraction chosen by mask. */
static void mont_mul(const Mont *c, uint32_t *out, const uint32_t *a, const uint32_t *b)
{
    uint32_t k = c->k, *t = c->t;
    const uint32_t *m = c->m;
    memset(t, 0, (k + 2) * sizeof(uint32_t));
    for (uint32_t i = 0; i < k; i++) {
        uint64_t carry = 0;
        for (uint32_t j = 0; j < k; j++) {
            carry += (uint64_t)a[j] * b[i] + t[j];
            t[j] = (uint32_t)carry;
            carry >>= 32;
        }
        uint64_t v = (uint64_t)t[k] + carry;
        t[k] = (uint32_t)v;
        t[k + 1] = (uint32_t)(v >> 32);
        uint32_t q = t[0] * c->m0inv;
        v = (uint64_t)q * m[0] + t[0];
        carry = v >> 32;
        for (uint32_t j = 1; j < k; j++) {
            v = (uint64_t)q * m[j] + t[j] + carry;
            t[j - 1] = (uint32_t)v;
            carry = v >> 32;
        }
        v = (uint64_t)t[k] + carry;
        t[k - 1] = (uint32_t)v;
        t[k] = t[k + 1] + (uint32_t)(v >> 32);
    }
    /* t < 2m: subtract into out, then keep t if that borrowed past the top limb. */
    uint64_t borrow = 0;
    for (uint32_t i = 0; i < k; i++) {
        uint64_t v = (uint64_t)t[i] - m[i] - borrow;
        out[i] = (uint32_t)v;
        borrow = (v >> 32) & 1;
    }
    uint32_t keep = (uint32_t)0 - (uint32_t)((uint64_t)t[k] < borrow);
    for (uint32_t i = 0; i < k; i++)
        out[i] = (out[i] & ~keep) | (t[i] & keep);
}

static void mont_free(Mont *c)
{
    if (c->r2) {
        mime_wipe(c->r2, (3 * c->k + 2) * sizeof(uint32_t));
        mime_crypto_free(c->r2);
    }
    c->r2 = nullptr;
}

static int mont_init(Mont *c, const MimeBn *m)
{
    uint32_t k = m->n;
    memset(c, 0, sizeof(*c));
    if (!k || !(m->d[0] & 1))
        return MIME_CRYPTO_ERR_PARAM;
    c->k = k;
    c->m = m->d;
    c->r2 = mime_crypto_alloc((3 * k + 2) * sizeof(uint32_t));
    if (!c->r2)
        return MIME_CRYPTO_ERR_NOMEM;
    c->one = c->r2 + k;
    c->t = c->one + k;
    uint32_t inv = 1;
    for (int i = 0; i < 5; i++)
        inv *= 2 - m->d[0] * inv;
    c->m0inv = (uint32_t)0 - inv;
    memset(c->one, 0, k * sizeof(uint32_t));
    c->one[0] = 1;
    /* r^2 mod m: 1 doubled 64 k times, reduced each step. */
    uint32_t *r2 = c->r2;
    memset(r2, 0, k * sizeof(uint32_t));
    r2[0] = 1;
    for (uint32_t i = 0; i < 64 * k; i++) {
        uint32_t carry = 0;
        for (uint32_t j = 0; j < k; j++) {
            uint32_t next = r2[j] >> 31;
            r2[j] = (r2[j] << 1) | carry;
            carry = next;
        }
        if (carry || cmp_limbs(r2, k, m->d, k) >= 0) {
            uint64_t borrow = 0;
            for (uint32_t j = 0; j < k; j++) {
                uint64_t v = (uint64_t)r2[j] - m->d[j] - borrow;
                r2[j] = (uint32_t)v;
                borrow = (v >> 32) & 1;
            }
        }
    }
    return MIME_CRYPTO_OK;
}

static uint32_t exp_bit(const MimeBn *e, uint32_t bit)
{
    return bit / 32 < e->n ? (e->d[bit / 32] >> (bit % 32)) & 1 : 0;
}

/* base below m, both on k limbs; out gets base^e mod m. */
static int mont_exp(const Mont *c, uint32_t *out, const uint32_t *base, const MimeBn *e, bool secret)
{
    uint32_t k = c->k;
    uint32_t bits = mime_bn_bits(e);
    uint32_t *table = mime_crypto_alloc((16 + 2) * k * sizeof(uint32_t));
    if (!table)
        return MIME_CRYPTO_ERR_NOMEM;
    uint32_t *acc = table + 16 * k, *x = acc + k;
    mont_mul(c, table, c->one, c->r2);
    mont_mul(c, table + k, base, c->r2);
    if (!secret) {
        memcpy(acc, table, k * sizeof(uint32_t));
        for (uint32_t i = bits; i; i--) {
            mont_mul(c, acc, acc, acc);
            if (exp_bit(e, i - 1))
                mont_mul(c, acc, acc, table + k);
        }
    } else {
        for (int i = 2; i < 16; i++)
            mont_mul(c, table + i * k, table + (i - 1) * k, table + k);
        memcpy(acc, table, k * sizeof(uint32_t));
        uint32_t windows = (bits + 3) / 4;
        for (uint32_t w = windows; w; w--) {
            for (int s = 0; s < 4; s++)
                mont_mul(c, acc, acc, acc);
            uint32_t index = 0;
            for (int b = 3; b >= 0; b--)
                index = index << 1 | exp_bit(e, 4 * (w - 1) + b);
            memset(x, 0, k * sizeof(uint32_t));
            for (uint32_t i = 0; i < 16; i++) {
                uint32_t mask = ~ct_nonzero(i ^ index);
                for (uint32_t j = 0; j < k; j++)
                    x[j] |= table[i * k + j] & mask;
            }
            mont_mul(c, acc, acc, x);
        }
    }
    mont_mul(c, out, acc, c->one);
    mime_wipe(table, (16 + 2) * k * sizeof(uint32_t));
    mime_crypto_free(table);
    return MIME_CRYPTO_OK;
}

int mime_bn_mod_exp(MimeBn *r, const MimeBn *base, const MimeBn *exp, const MimeBn *m, bool secret)
{
    Mont c;
    int status = mont_init(&c, m);
    if (status)
        return status;
    MimeBn *b = mime_crypto_alloc(sizeof(MimeBn));
    if (!b) {
        mont_free(&c);
        return MIME_CRYPTO_ERR_NOMEM;
    }
    status = mime_bn_divmod(nullptr, b, base, m);
    if (!status) {
        memset(b->d + b->n, 0, (c.k - b->n) * sizeof(uint32_t));
        status = mont_exp(&c, r->d, b->d, exp, secret);
        if (!status) {
            r->n = c.k;
            trim(r);
        }
    }
    mime_wipe(b, sizeof(*b));
    mime_crypto_free(b);
    mont_free(&c);
    return status;
}

int mime_bn_mod_mul(MimeBn *r, const MimeBn *a, const MimeBn *b, const MimeBn *m)
{
    MimeBn *t = mime_crypto_alloc(sizeof(MimeBn));
    if (!t)
        return MIME_CRYPTO_ERR_NOMEM;
    int status = mime_bn_mul(t, a, b) ? mime_bn_divmod(nullptr, r, t, m) : MIME_CRYPTO_ERR_PARAM;
    mime_wipe(t, sizeof(*t));
    mime_crypto_free(t);
    return status;
}

int mime_bn_gcd(MimeBn *r, const MimeBn *a, const MimeBn *b)
{
    MimeBn *x = mime_crypto_alloc(3 * sizeof(MimeBn));
    if (!x)
        return MIME_CRYPTO_ERR_NOMEM;
    MimeBn *y = x + 1, *t = x + 2;
    mime_bn_copy(x, a);
    mime_bn_copy(y, b);
    int status = MIME_CRYPTO_OK;
    while (y->n && !status) {
        status = mime_bn_divmod(nullptr, t, x, y);
        mime_bn_copy(x, y);
        mime_bn_copy(y, t);
    }
    mime_bn_copy(r, x);
    mime_wipe(x, 3 * sizeof(MimeBn));
    mime_crypto_free(x);
    return status;
}

/* extended euclid, keeping the coefficient of a reduced modulo m. */
int mime_bn_mod_inverse(MimeBn *r, const MimeBn *a, const MimeBn *m)
{
    MimeBn *u = mime_crypto_alloc(7 * sizeof(MimeBn));
    if (!u)
        return MIME_CRYPTO_ERR_NOMEM;
    MimeBn *v = u + 1, *x1 = u + 2, *x2 = u + 3, *q = u + 4, *rem = u + 5, *t = u + 6;
    int status = mime_bn_divmod(nullptr, u, a, m);
    mime_bn_copy(v, m);
    mime_bn_set(x1, 1);
    mime_bn_zero(x2);
    while (!status && u->n && !mime_bn_is_one(u)) {
        status = mime_bn_divmod(q, rem, v, u);
        if (status)
            break;
        /* x = x2 - q x1 mod m */
        if (!mime_bn_mul(t, q, x1)) {
            status = MIME_CRYPTO_ERR_PARAM;
            break;
        }
        status = mime_bn_divmod(nullptr, t, t, m);
        if (status)
            break;
        if (mime_bn_cmp(x2, t) >= 0) {
            mime_bn_sub(t, x2, t);
        } else {
            mime_bn_sub(t, m, t);
            mime_bn_add(t, t, x2);
        }
        mime_bn_copy(x2, x1);
        mime_bn_copy(x1, t);
        mime_bn_copy(v, u);
        mime_bn_copy(u, rem);
    }
    if (!status && !mime_bn_is_one(u))
        status = MIME_CRYPTO_ERR_PARAM;
    if (!status)
        mime_bn_copy(r, x1);
    mime_wipe(u, 7 * sizeof(MimeBn));
    mime_crypto_free(u);
    return status;
}

int mime_bn_random_bits(MimeBn *r, uint32_t bits)
{
    uint32_t n = (bits + 31) / 32;
    if (n > MIME_BN_LIMBS)
        return MIME_CRYPTO_ERR_PARAM;
    if (!mime_crypto_random(r->d, n * sizeof(uint32_t)))
        return MIME_CRYPTO_ERR_RANDOM;
    if (bits % 32)
        r->d[n - 1] &= (1u << (bits % 32)) - 1;
    r->n = n;
    trim(r);
    return MIME_CRYPTO_OK;
}

int mime_bn_random_below(MimeBn *r, const MimeBn *max)
{
    uint32_t bits = mime_bn_bits(max);
    if (mime_bn_cmp_word(max, 2) < 0)
        return MIME_CRYPTO_ERR_PARAM;
    for (int tries = 0; tries < 1000; tries++) {
        int status = mime_bn_random_bits(r, bits);
        if (status)
            return status;
        if (r->n && mime_bn_cmp(r, max) < 0)
            return MIME_CRYPTO_OK;
    }
    return MIME_CRYPTO_ERR_RANDOM;
}

static const uint16_t small_primes[] = {
    3,   5,   7,   11,  13,  17,  19,  23,  29,  31,  37,  41,  43,  47,  53,  59,  61,  67,  71,  73,  79,  83,
    89,  97,  101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197,
    199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331,
    337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461,
    463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599, 601, 607,
    613, 617, 619, 631, 641, 643, 647, 653, 659, 661, 673, 677, 683, 691, 701, 709, 719, 727, 733, 739, 743, 751,
    757, 761, 769, 773, 787, 797, 809, 811, 821, 823, 827, 829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907,
    911, 919, 929, 937, 941, 947, 953, 967, 971, 977, 983, 991, 997};

int mime_bn_is_prime(const MimeBn *a, int rounds, bool *prime)
{
    *prime = false;
    if (mime_bn_cmp_word(a, 2) < 0)
        return MIME_CRYPTO_OK;
    if (!(a->d[0] & 1)) {
        *prime = mime_bn_cmp_word(a, 2) == 0;
        return MIME_CRYPTO_OK;
    }
    for (size_t i = 0; i < sizeof(small_primes) / sizeof(small_primes[0]); i++)
        if (mime_bn_mod_word(a, small_primes[i]) == 0) {
            *prime = mime_bn_cmp_word(a, small_primes[i]) == 0;
            return MIME_CRYPTO_OK;
        }
    if (mime_bn_cmp_word(a, 1000000) < 0) {
        *prime = true;
        return MIME_CRYPTO_OK;
    }
    Mont c;
    int status = mont_init(&c, a);
    if (status)
        return status;
    MimeBn *w = mime_crypto_alloc(4 * sizeof(MimeBn));
    if (!w) {
        mont_free(&c);
        return MIME_CRYPTO_ERR_NOMEM;
    }
    MimeBn *minus1 = w, *d = w + 1, *base = w + 2, *x = w + 3;
    mime_bn_copy(minus1, a);
    mime_bn_sub_word(minus1, 1);
    uint32_t s = 0;
    while (!exp_bit(minus1, s))
        s++;
    mime_bn_shr(d, minus1, s);
    uint32_t k = c.k;
    uint32_t *xm = mime_crypto_alloc(2 * k * sizeof(uint32_t));
    uint32_t *minus1m = xm + k;
    if (!xm)
        status = MIME_CRYPTO_ERR_NOMEM;
    bool composite = false;
    if (!status) {
        memset(minus1m, 0, k * sizeof(uint32_t));
        memcpy(minus1m, minus1->d, minus1->n * sizeof(uint32_t));
        mont_mul(&c, minus1m, minus1m, c.r2);
    }
    for (int round = 0; !status && !composite && round < rounds; round++) {
        status = mime_bn_random_below(base, minus1);
        if (status)
            break;
        if (mime_bn_cmp_word(base, 2) < 0)
            mime_bn_set(base, 2);
        memset(x->d, 0, k * sizeof(uint32_t));
        memcpy(x->d, base->d, base->n * sizeof(uint32_t));
        status = mont_exp(&c, x->d, x->d, d, false);
        if (status)
            break;
        x->n = k;
        trim(x);
        if (mime_bn_is_one(x) || mime_bn_cmp(x, minus1) == 0)
            continue;
        /* square in montgomery form and compare with -1 there. */
        memset(xm, 0, k * sizeof(uint32_t));
        memcpy(xm, x->d, x->n * sizeof(uint32_t));
        mont_mul(&c, xm, xm, c.r2);
        bool found = false;
        for (uint32_t i = 1; i < s && !found; i++) {
            mont_mul(&c, xm, xm, xm);
            found = !memcmp(xm, minus1m, k * sizeof(uint32_t));
        }
        composite = !found;
    }
    if (!status)
        *prime = !composite;
    if (xm) {
        mime_wipe(xm, 2 * k * sizeof(uint32_t));
        mime_crypto_free(xm);
    }
    mime_wipe(w, 4 * sizeof(MimeBn));
    mime_crypto_free(w);
    mont_free(&c);
    return status;
}
