/*
 * the nist prime curves p-256, p-384 and p-521 of fips 186-4 and sec 1, with
 * ecdsa and ecdh. coordinates are projective, added with the complete
 * formulas of renes, costello and batina for a = -3, so doubling, adding a
 * point to itself and the point at infinity need no special case, and
 * scalars are multiplied with a fixed window read by mask.
 */
#include "internal.h"

#define EC_LIMBS 17

struct MimeEcCurve {
    const char *name;
    uint32_t bits;
    uint32_t size;
    uint8_t p[66], b[66], gx[66], gy[66], n[66];
};

#include "ec_curves.inc"

uint32_t mime_ec_bits(const MimeEcCurve *curve)
{
    return curve->bits;
}

uint32_t mime_ec_size(const MimeEcCurve *curve)
{
    return curve->size;
}

const char *mime_ec_name(const MimeEcCurve *curve)
{
    return curve->name;
}

void mime_ec_parameters(const MimeEcCurve *curve, uint8_t *p, uint8_t *a, uint8_t *b, uint8_t *gx, uint8_t *gy,
                        uint8_t *n)
{
    uint32_t size = curve->size;
    memcpy(p, curve->p, size);
    /* a = p - 3. */
    memcpy(a, curve->p, size);
    a[size - 1] -= 3;
    memcpy(b, curve->b, size);
    memcpy(gx, curve->gx, size);
    memcpy(gy, curve->gy, size);
    memcpy(n, curve->n, size);
}

/* ------------------------------------------------------------------ field arithmetic */

typedef struct Field {
    uint32_t k;
    uint32_t m0inv;
    uint32_t m[EC_LIMBS];
    uint32_t r2[EC_LIMBS];
    uint32_t one[EC_LIMBS];
} Field;

typedef uint32_t fe[EC_LIMBS];

static void fe_from_bytes(uint32_t *v, uint32_t k, const uint8_t *be, size_t size)
{
    memset(v, 0, k * sizeof(uint32_t));
    for (size_t i = 0; i < size; i++)
        v[i / 4] |= (uint32_t)be[size - 1 - i] << (8 * (i % 4));
}

static void fe_to_bytes(const uint32_t *v, uint8_t *be, size_t size)
{
    for (size_t i = 0; i < size; i++)
        be[size - 1 - i] = (uint8_t)(v[i / 4] >> (8 * (i % 4)));
}

/* r = a - b, the borrow returned. */
static uint32_t sub_limbs(uint32_t *r, const uint32_t *a, const uint32_t *b, uint32_t k)
{
    uint64_t borrow = 0;
    for (uint32_t i = 0; i < k; i++) {
        uint64_t v = (uint64_t)a[i] - b[i] - borrow;
        r[i] = (uint32_t)v;
        borrow = (v >> 32) & 1;
    }
    return (uint32_t)borrow;
}

static uint32_t add_limbs(uint32_t *r, const uint32_t *a, const uint32_t *b, uint32_t k)
{
    uint64_t carry = 0;
    for (uint32_t i = 0; i < k; i++) {
        carry += (uint64_t)a[i] + b[i];
        r[i] = (uint32_t)carry;
        carry >>= 32;
    }
    return (uint32_t)carry;
}

static void select_limbs(uint32_t *r, const uint32_t *a, const uint32_t *b, uint32_t mask, uint32_t k)
{
    for (uint32_t i = 0; i < k; i++)
        r[i] = (a[i] & mask) | (b[i] & ~mask);
}

/* whether a < b, as a mask. */
static uint32_t less(const uint32_t *a, const uint32_t *b, uint32_t k)
{
    uint32_t t[EC_LIMBS];
    return (uint32_t)0 - sub_limbs(t, a, b, k);
}

static void fadd(const Field *f, uint32_t *r, const uint32_t *a, const uint32_t *b)
{
    uint32_t s[EC_LIMBS], t[EC_LIMBS];
    uint32_t carry = add_limbs(s, a, b, f->k);
    uint32_t borrow = sub_limbs(t, s, f->m, f->k);
    /* keep s when the sum stayed below m: no carry and the subtraction borrowed. */
    select_limbs(r, s, t, (uint32_t)0 - (borrow & ~carry & 1), f->k);
}

static void fsub(const Field *f, uint32_t *r, const uint32_t *a, const uint32_t *b)
{
    uint32_t s[EC_LIMBS], t[EC_LIMBS];
    uint32_t borrow = sub_limbs(s, a, b, f->k);
    add_limbs(t, s, f->m, f->k);
    select_limbs(r, t, s, (uint32_t)0 - borrow, f->k);
}

static void fmul(const Field *f, uint32_t *r, const uint32_t *a, const uint32_t *b)
{
    uint32_t k = f->k, t[EC_LIMBS + 2] = {};
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
        uint32_t q = t[0] * f->m0inv;
        v = (uint64_t)q * f->m[0] + t[0];
        carry = v >> 32;
        for (uint32_t j = 1; j < k; j++) {
            v = (uint64_t)q * f->m[j] + t[j] + carry;
            t[j - 1] = (uint32_t)v;
            carry = v >> 32;
        }
        v = (uint64_t)t[k] + carry;
        t[k - 1] = (uint32_t)v;
        t[k] = t[k + 1] + (uint32_t)(v >> 32);
    }
    uint32_t s[EC_LIMBS];
    uint32_t borrow = sub_limbs(s, t, f->m, k);
    uint32_t keep = (uint32_t)0 - (uint32_t)(t[k] < borrow);
    select_limbs(r, t, s, keep, k);
}

static void field_init(Field *f, const uint8_t *modulus, uint32_t size)
{
    memset(f, 0, sizeof(*f));
    f->k = (size + 3) / 4;
    fe_from_bytes(f->m, f->k, modulus, size);
    uint32_t inv = 1;
    for (int i = 0; i < 5; i++)
        inv *= 2 - f->m[0] * inv;
    f->m0inv = (uint32_t)0 - inv;
    /* r mod m and r^2 mod m by doubling. */
    uint32_t v[EC_LIMBS] = {1};
    for (uint32_t i = 0; i < 64 * f->k; i++) {
        if (i == 32 * f->k)
            memcpy(f->one, v, sizeof(v));
        fadd(f, v, v, v);
    }
    memcpy(f->r2, v, sizeof(v));
}

static void to_mont(const Field *f, uint32_t *r, const uint32_t *a)
{
    fmul(f, r, a, f->r2);
}

static void from_mont(const Field *f, uint32_t *r, const uint32_t *a)
{
    uint32_t one[EC_LIMBS] = {1};
    fmul(f, r, a, one);
}

/* a to the power m - 2 in montgomery form; m is prime and public, so the square and multiply follows it. */
static void finv(const Field *f, uint32_t *r, const uint32_t *a)
{
    uint32_t e[EC_LIMBS], two[EC_LIMBS] = {2}, acc[EC_LIMBS];
    sub_limbs(e, f->m, two, f->k);
    memcpy(acc, f->one, sizeof(acc));
    for (uint32_t i = 32 * f->k; i; i--) {
        fmul(f, acc, acc, acc);
        if ((e[(i - 1) / 32] >> ((i - 1) % 32)) & 1)
            fmul(f, acc, acc, a);
    }
    memcpy(r, acc, sizeof(acc));
}

static bool fe_is_zero(const uint32_t *a, uint32_t k)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < k; i++)
        v |= a[i];
    return v == 0;
}

/* ------------------------------------------------------------------ points */

typedef struct Point {
    fe x, y, z;
} Point;

typedef struct Ctx {
    const MimeEcCurve *curve;
    Field p, n;
    fe b;
    Point g;
} Ctx;

static void ctx_init(Ctx *c, const MimeEcCurve *curve)
{
    c->curve = curve;
    field_init(&c->p, curve->p, curve->size);
    field_init(&c->n, curve->n, curve->size);
    fe t;
    fe_from_bytes(t, c->p.k, curve->b, curve->size);
    to_mont(&c->p, c->b, t);
    fe_from_bytes(t, c->p.k, curve->gx, curve->size);
    to_mont(&c->p, c->g.x, t);
    fe_from_bytes(t, c->p.k, curve->gy, curve->size);
    to_mont(&c->p, c->g.y, t);
    memcpy(c->g.z, c->p.one, sizeof(fe));
}

static void point_identity(const Ctx *c, Point *r)
{
    memset(r, 0, sizeof(*r));
    memcpy(r->y, c->p.one, sizeof(fe));
}

/* algorithm 4 of renes, costello and batina, "complete addition formulas for prime order elliptic curves". */
static void point_add(const Ctx *c, Point *r, const Point *a, const Point *q)
{
    const Field *f = &c->p;
    fe t0, t1, t2, t3, t4, x3, y3, z3;
    fmul(f, t0, a->x, q->x);
    fmul(f, t1, a->y, q->y);
    fmul(f, t2, a->z, q->z);
    fadd(f, t3, a->x, a->y);
    fadd(f, t4, q->x, q->y);
    fmul(f, t3, t3, t4);
    fadd(f, t4, t0, t1);
    fsub(f, t3, t3, t4);
    fadd(f, t4, a->y, a->z);
    fadd(f, x3, q->y, q->z);
    fmul(f, t4, t4, x3);
    fadd(f, x3, t1, t2);
    fsub(f, t4, t4, x3);
    fadd(f, x3, a->x, a->z);
    fadd(f, y3, q->x, q->z);
    fmul(f, x3, x3, y3);
    fadd(f, y3, t0, t2);
    fsub(f, y3, x3, y3);
    fmul(f, z3, c->b, t2);
    fsub(f, x3, y3, z3);
    fadd(f, z3, x3, x3);
    fadd(f, x3, x3, z3);
    fsub(f, z3, t1, x3);
    fadd(f, x3, t1, x3);
    fmul(f, y3, c->b, y3);
    fadd(f, t1, t2, t2);
    fadd(f, t2, t1, t2);
    fsub(f, y3, y3, t2);
    fsub(f, y3, y3, t0);
    fadd(f, t1, y3, y3);
    fadd(f, y3, t1, y3);
    fadd(f, t1, t0, t0);
    fadd(f, t0, t1, t0);
    fsub(f, t0, t0, t2);
    fmul(f, t1, t4, y3);
    fmul(f, t2, t0, y3);
    fmul(f, y3, x3, z3);
    fadd(f, y3, y3, t2);
    fmul(f, x3, t3, x3);
    fsub(f, x3, x3, t1);
    fmul(f, z3, t4, z3);
    fmul(f, t1, t3, t0);
    fadd(f, z3, z3, t1);
    memcpy(r->x, x3, sizeof(fe));
    memcpy(r->y, y3, sizeof(fe));
    memcpy(r->z, z3, sizeof(fe));
}

/* scalar is size bytes, big endian. */
static void point_mul(const Ctx *c, Point *r, const Point *p, const uint8_t *scalar)
{
    Point table[16], acc, pick;
    point_identity(c, &table[0]);
    table[1] = *p;
    for (int i = 2; i < 16; i++)
        point_add(c, &table[i], &table[i - 1], p);
    point_identity(c, &acc);
    for (uint32_t i = 0; i < 2 * c->curve->size; i++) {
        for (int d = 0; d < 4; d++)
            point_add(c, &acc, &acc, &acc);
        uint32_t nibble = (scalar[i / 2] >> (i & 1 ? 0 : 4)) & 15;
        memset(&pick, 0, sizeof(pick));
        for (uint32_t j = 0; j < 16; j++) {
            uint32_t mask = ~ct_nonzero(j ^ nibble);
            const uint32_t *src = (const uint32_t *)&table[j];
            uint32_t *dst = (uint32_t *)&pick;
            for (size_t w = 0; w < sizeof(Point) / 4; w++)
                dst[w] |= src[w] & mask;
        }
        point_add(c, &acc, &acc, &pick);
    }
    *r = acc;
    mime_wipe(table, sizeof(table));
    mime_wipe(&pick, sizeof(pick));
}

/* affine coordinates out of montgomery form; false for the point at infinity. */
static bool point_affine(const Ctx *c, const Point *a, uint32_t *x, uint32_t *y)
{
    const Field *f = &c->p;
    if (fe_is_zero(a->z, f->k))
        return false;
    fe zi, t;
    finv(f, zi, a->z);
    fmul(f, t, a->x, zi);
    from_mont(f, x, t);
    fmul(f, t, a->y, zi);
    from_mont(f, y, t);
    return true;
}

/* loads a public point, checking range and the curve equation y^2 = x^3 - 3x + b. */
static bool point_load(const Ctx *c, Point *r, const uint8_t *x, const uint8_t *y)
{
    const Field *f = &c->p;
    fe ax, ay, t, u, w;
    fe_from_bytes(ax, f->k, x, c->curve->size);
    fe_from_bytes(ay, f->k, y, c->curve->size);
    if (!less(ax, f->m, f->k) || !less(ay, f->m, f->k))
        return false;
    to_mont(f, r->x, ax);
    to_mont(f, r->y, ay);
    memcpy(r->z, f->one, sizeof(fe));
    fmul(f, t, r->y, r->y);
    fmul(f, u, r->x, r->x);
    fmul(f, u, u, r->x);
    fadd(f, w, r->x, r->x);
    fadd(f, w, w, r->x);
    fsub(f, u, u, w);
    fadd(f, u, u, c->b);
    return !memcmp(t, u, f->k * sizeof(uint32_t));
}

/* ------------------------------------------------------------------ keys */

static int random_scalar(const Ctx *c, uint8_t *out)
{
    uint32_t size = c->curve->size, top_bits = c->curve->bits % 8;
    fe v;
    for (int tries = 0; tries < 1000; tries++) {
        if (!mime_crypto_random(out, size))
            return MIME_CRYPTO_ERR_RANDOM;
        if (top_bits)
            out[0] &= (uint8_t)((1 << top_bits) - 1);
        fe_from_bytes(v, c->n.k, out, size);
        if (!fe_is_zero(v, c->n.k) && less(v, c->n.m, c->n.k))
            return MIME_CRYPTO_OK;
    }
    return MIME_CRYPTO_ERR_RANDOM;
}

int mime_ec_derive_public(MimeEcKey *key)
{
    Ctx c;
    ctx_init(&c, key->curve);
    fe d;
    fe_from_bytes(d, c.n.k, key->d, c.curve->size);
    if (fe_is_zero(d, c.n.k) || !less(d, c.n.m, c.n.k))
        return MIME_CRYPTO_ERR_PARAM;
    Point q;
    fe x, y;
    point_mul(&c, &q, &c.g, key->d);
    if (!point_affine(&c, &q, x, y))
        return MIME_CRYPTO_ERR_PARAM;
    fe_to_bytes(x, key->x, c.curve->size);
    fe_to_bytes(y, key->y, c.curve->size);
    mime_wipe(d, sizeof(d));
    return MIME_CRYPTO_OK;
}

int mime_ec_generate(MimeEcKey *key, const MimeEcCurve *curve)
{
    Ctx c;
    ctx_init(&c, curve);
    key->curve = curve;
    int status = random_scalar(&c, key->d);
    if (status)
        return status;
    key->private_key = true;
    return mime_ec_derive_public(key);
}

int mime_ec_check_public(const MimeEcKey *key)
{
    Ctx c;
    Point q;
    ctx_init(&c, key->curve);
    return point_load(&c, &q, key->x, key->y) ? MIME_CRYPTO_OK : MIME_CRYPTO_ERR_PARAM;
}

/* the hash truncated to the order's bit length, then reduced below the order. */
static void hash_to_scalar(const Ctx *c, const uint8_t *hash, size_t hash_size, uint32_t *e)
{
    uint32_t size = c->curve->size, bits = c->curve->bits;
    uint8_t buffer[66] = {};
    size_t take = hash_size < size ? hash_size : size;
    memcpy(buffer + size - take, hash, take);
    fe_from_bytes(e, c->n.k, buffer, size);
    if (hash_size * 8 > bits) {
        /* keep the top bits: shift right by the excess of the taken bytes. */
        uint32_t excess = (uint32_t)(take * 8 > bits ? take * 8 - bits : 0);
        for (uint32_t i = 0; i < c->n.k; i++)
            e[i] = (e[i] >> excess) | (excess && i + 1 < c->n.k ? e[i + 1] << (32 - excess) : 0);
    }
    uint32_t t[EC_LIMBS];
    uint32_t borrow = sub_limbs(t, e, c->n.m, c->n.k);
    select_limbs(e, e, t, (uint32_t)0 - borrow, c->n.k);
}

int mime_ecdsa_sign(const MimeEcKey *key, const uint8_t *hash, size_t hash_size, uint8_t *sig)
{
    if (!key->private_key)
        return MIME_CRYPTO_ERR_PARAM;
    Ctx c;
    ctx_init(&c, key->curve);
    const Field *n = &c.n;
    uint32_t size = c.curve->size;
    fe e, d, r, s, k, x, y, t;
    hash_to_scalar(&c, hash, hash_size, e);
    fe_from_bytes(d, n->k, key->d, size);
    for (int tries = 0; tries < 100; tries++) {
        uint8_t kbytes[66];
        int status = random_scalar(&c, kbytes);
        if (status)
            return status;
        Point rp;
        point_mul(&c, &rp, &c.g, kbytes);
        fe_from_bytes(k, n->k, kbytes, size);
        mime_wipe(kbytes, sizeof(kbytes));
        if (!point_affine(&c, &rp, x, y))
            continue;
        /* r = x mod n, x < p < 2n. */
        uint32_t borrow = sub_limbs(t, x, n->m, n->k);
        select_limbs(r, x, t, (uint32_t)0 - borrow, n->k);
        if (fe_is_zero(r, n->k))
            continue;
        /* s = k^-1 * [e + r d] mod n, in montgomery form until the end. */
        fe km, rm, dm, em, kinv;
        to_mont(n, km, k);
        to_mont(n, rm, r);
        to_mont(n, dm, d);
        to_mont(n, em, e);
        finv(n, kinv, km);
        fmul(n, t, rm, dm);
        fadd(n, t, t, em);
        fmul(n, t, t, kinv);
        from_mont(n, s, t);
        mime_wipe(km, sizeof(km));
        mime_wipe(kinv, sizeof(kinv));
        mime_wipe(dm, sizeof(dm));
        if (fe_is_zero(s, n->k))
            continue;
        fe_to_bytes(r, sig, size);
        fe_to_bytes(s, sig + size, size);
        mime_wipe(d, sizeof(d));
        mime_wipe(k, sizeof(k));
        return MIME_CRYPTO_OK;
    }
    return MIME_CRYPTO_ERR_RANDOM;
}

int mime_ecdsa_verify(const MimeEcKey *key, const uint8_t *hash, size_t hash_size, const uint8_t *sig,
                      size_t sig_size)
{
    Ctx c;
    ctx_init(&c, key->curve);
    const Field *n = &c.n;
    uint32_t size = c.curve->size;
    if (sig_size != 2 * size)
        return MIME_CRYPTO_ERR_SIGNATURE;
    Point q;
    if (!point_load(&c, &q, key->x, key->y))
        return MIME_CRYPTO_ERR_SIGNATURE;
    fe e, r, s, w, u1, u2, x, y, t;
    fe_from_bytes(r, n->k, sig, size);
    fe_from_bytes(s, n->k, sig + size, size);
    if (fe_is_zero(r, n->k) || fe_is_zero(s, n->k) || !less(r, n->m, n->k) || !less(s, n->m, n->k))
        return MIME_CRYPTO_ERR_SIGNATURE;
    hash_to_scalar(&c, hash, hash_size, e);
    fe sm, em, rm;
    to_mont(n, sm, s);
    to_mont(n, em, e);
    to_mont(n, rm, r);
    finv(n, w, sm);
    fmul(n, t, em, w);
    from_mont(n, u1, t);
    fmul(n, t, rm, w);
    from_mont(n, u2, t);
    uint8_t b1[66], b2[66];
    fe_to_bytes(u1, b1, size);
    fe_to_bytes(u2, b2, size);
    Point p1, p2;
    point_mul(&c, &p1, &c.g, b1);
    point_mul(&c, &p2, &q, b2);
    point_add(&c, &p1, &p1, &p2);
    if (!point_affine(&c, &p1, x, y))
        return MIME_CRYPTO_ERR_SIGNATURE;
    uint32_t borrow = sub_limbs(t, x, n->m, n->k);
    select_limbs(x, x, t, (uint32_t)0 - borrow, n->k);
    return memcmp(x, r, n->k * sizeof(uint32_t)) ? MIME_CRYPTO_ERR_SIGNATURE : MIME_CRYPTO_OK;
}

int mime_ecdh(const MimeEcKey *key, const MimeEcKey *peer, uint8_t *secret)
{
    if (!key->private_key || key->curve != peer->curve)
        return MIME_CRYPTO_ERR_PARAM;
    Ctx c;
    ctx_init(&c, key->curve);
    Point q, s;
    if (!point_load(&c, &q, peer->x, peer->y))
        return MIME_CRYPTO_ERR_PARAM;
    point_mul(&c, &s, &q, key->d);
    fe x, y;
    if (!point_affine(&c, &s, x, y))
        return MIME_CRYPTO_ERR_PARAM;
    fe_to_bytes(x, secret, c.curve->size);
    mime_wipe(x, sizeof(x));
    mime_wipe(&s, sizeof(s));
    return MIME_CRYPTO_OK;
}
