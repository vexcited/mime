#include "icc_internal.h"

enum { ST_CURVES, ST_MATRIX, ST_CLUT, ST_XYZ2LAB, ST_LAB2XYZ, ST_SCALE, ST_CLAMP };

typedef struct Stage {
    int kind;
    uint32_t in, out;
    IccCurve *curves;
    /* a 3 by 3 matrix, row major, then the offset. */
    double m[12];
    double scale[ICC_MAX_CHANNELS], offset[ICC_MAX_CHANNELS];
    uint32_t grid[ICC_MAX_CHANNELS];
    float *clut;
} Stage;

typedef struct Block {
    struct Block *next;
} Block;

struct IccXform {
    MimeAllocator alloc;
    Block *blocks;
    Stage *stages;
    uint32_t count, capacity;
    uint32_t inputs, outputs, in_space, out_space;
    bool identity;
    /* the tables of eval8. */
    int fast;
    float *in_table;   /* 256 per input channel. */
    double fm[12];
    float *out_table;  /* so many points per output channel. */
    float *lut;        /* a grid sampled over the transform, up to its last curves. */
    uint32_t lut_points;
    bool post;         /* the last curves are applied through out_table after the grid. */
};

enum { FAST_NONE, FAST_IDENTITY, FAST_SHAPER, FAST_LUT };
#define OUT_POINTS 4097

static void *xalloc(IccXform *x, size_t size)
{
    Block *b = mime_alloc(&x->alloc, sizeof(Block) + 8 + size);
    if (!b)
        return nullptr;
    b->next = x->blocks;
    x->blocks = b;
    void *p = (uint8_t *)b + sizeof(Block) + 8;
    memset(p, 0, size);
    return p;
}

static Stage *push(IccXform *x, int kind, uint32_t in, uint32_t out)
{
    if (x->count == x->capacity) {
        uint32_t cap = x->capacity ? x->capacity * 2 : 16;
        Stage *s = mime_alloc(&x->alloc, cap * sizeof(Stage));
        if (!s)
            return nullptr;
        if (x->count)
            memcpy(s, x->stages, x->count * sizeof(Stage));
        mime_free(&x->alloc, x->stages);
        x->stages = s;
        x->capacity = cap;
    }
    Stage *s = &x->stages[x->count++];
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    s->in = in;
    s->out = out;
    return s;
}

static bool push_matrix(IccXform *x, const double m[9], const double offset[3])
{
    Stage *s = push(x, ST_MATRIX, 3, 3);
    if (!s)
        return false;
    memcpy(s->m, m, 9 * sizeof(double));
    if (offset)
        memcpy(s->m + 9, offset, 3 * sizeof(double));
    return true;
}

static bool push_diag(IccXform *x, const double d[3], const double offset[3])
{
    double m[9] = {d[0], 0, 0, 0, d[1], 0, 0, 0, d[2]};
    return push_matrix(x, m, offset);
}

static bool push_scale(IccXform *x, uint32_t n, const double *scale, const double *offset)
{
    Stage *s = push(x, ST_SCALE, n, n);
    if (!s)
        return false;
    for (uint32_t i = 0; i < n; i++) {
        s->scale[i] = scale[i];
        s->offset[i] = offset ? offset[i] : 0;
    }
    return true;
}

/* ------------------------------------------------------------------ profiles */

typedef struct Profile {
    const uint8_t *data;
    uint32_t size;
    uint32_t version, device_class, space, pcs, channels;
    double media_white[3];
} Profile;

static const uint8_t *tag(const Profile *p, uint32_t sig, uint32_t *len)
{
    return icc_tag(p->data, p->size, sig, len);
}

static bool read_xyz(const Profile *p, uint32_t sig, double out[3])
{
    uint32_t len;
    const uint8_t *t = tag(p, sig, &len);
    if (!t || len < 20 || icc_be32(t) != ICC_SIG('X', 'Y', 'Z', ' '))
        return false;
    for (int i = 0; i < 3; i++)
        out[i] = icc_s15(t + 8 + i * 4);
    return true;
}

static bool profile_open(Profile *p, const uint8_t *data, uint32_t size)
{
    if (!icc_valid(data, size))
        return false;
    p->data = data;
    p->size = icc_be32(data);
    p->version = data[8];
    p->device_class = icc_be32(data + 12);
    p->space = icc_be32(data + 16);
    p->pcs = icc_be32(data + 20);
    p->channels = icc_space_channels(p->space);
    if (!p->channels)
        return false;
    if (p->device_class != ICC_SIG('l', 'i', 'n', 'k') && p->pcs != ICC_SIG('X', 'Y', 'Z', ' ') &&
        p->pcs != ICC_SIG('L', 'a', 'b', ' '))
        return false;
    /* version 2 displays put their white in wtpt unadapted while their colors are relative to d50 already. */
    memcpy(p->media_white, icc_d50, sizeof(p->media_white));
    if (!(p->version < 4 && p->device_class == ICC_SIG('m', 'n', 't', 'r')))
        read_xyz(p, ICC_SIG('w', 't', 'p', 't'), p->media_white);
    if (p->media_white[1] <= 0)
        memcpy(p->media_white, icc_d50, sizeof(p->media_white));
    return true;
}

/* ------------------------------------------------------------------ reading tags into stages */

/* reads a curv or para curve, and how many bytes it takes, 0 when it is damaged. */
static uint32_t read_curve(IccXform *x, const uint8_t *t, uint32_t len, IccCurve *c)
{
    if (len < 12)
        return 0;
    uint32_t type = icc_be32(t);
    if (type == ICC_SIG('c', 'u', 'r', 'v')) {
        uint32_t n = icc_be32(t + 8);
        if (n > (len - 12) / 2)
            return 0;
        if (n == 0) {
            c->kind = CURVE_IDENTITY;
        } else if (n == 1) {
            c->kind = CURVE_PARAMETRIC;
            double g = icc_be16(t + 12) / 256.0;
            double p[7] = {g, 1, 0, 0, 0, 0, 0};
            memcpy(c->p, p, sizeof(p));
        } else {
            c->kind = CURVE_TABLE;
            c->n = n;
            c->table = xalloc(x, n * sizeof(float));
            if (!c->table)
                return 0;
            for (uint32_t i = 0; i < n; i++)
                c->table[i] = icc_be16(t + 12 + i * 2) / 65535.0f;
        }
        return 12 + n * 2;
    }
    if (type == ICC_SIG('p', 'a', 'r', 'a')) {
        static const uint8_t counts[5] = {1, 3, 4, 5, 7};
        uint32_t func = icc_be16(t + 8);
        if (func > 4 || len < 12 + counts[func] * 4u)
            return 0;
        double v[7] = {0};
        for (uint32_t i = 0; i < counts[func]; i++)
            v[i] = icc_s15(t + 12 + i * 4);
        double g = v[0], a = v[1], b = v[2], cc = v[3];
        c->kind = CURVE_PARAMETRIC;
        switch (func) {
        case 0: {
            double p[7] = {g, 1, 0, 0, 0, 0, 0};
            memcpy(c->p, p, sizeof(p));
            break;
        }
        case 1: {
            double p[7] = {g, a, b, 0, a != 0 ? -b / a : 0, 0, 0};
            memcpy(c->p, p, sizeof(p));
            break;
        }
        case 2: {
            double p[7] = {g, a, b, 0, a != 0 ? -b / a : 0, cc, cc};
            memcpy(c->p, p, sizeof(p));
            break;
        }
        case 3: {
            double p[7] = {g, a, b, cc, v[4], 0, 0};
            memcpy(c->p, p, sizeof(p));
            break;
        }
        case 4:
            memcpy(c->p, v, sizeof(v));
            break;
        }
        return 12 + counts[func] * 4;
    }
    return 0;
}

static IccCurve *read_curves(IccXform *x, const uint8_t *tagdata, uint32_t taglen, uint32_t offset, uint32_t n)
{
    IccCurve *c = xalloc(x, n * sizeof(IccCurve));
    if (!c)
        return nullptr;
    for (uint32_t i = 0; i < n; i++) {
        if (offset >= taglen)
            return nullptr;
        uint32_t used = read_curve(x, tagdata + offset, taglen - offset, &c[i]);
        if (!used)
            return nullptr;
        offset += (used + 3) & ~3u;
    }
    return c;
}

static bool push_curves(IccXform *x, IccCurve *c, uint32_t n, bool inverse)
{
    if (!c)
        return false;
    Stage *s = push(x, ST_CURVES, n, n);
    if (!s)
        return false;
    if (inverse) {
        IccCurve *copy = xalloc(x, n * sizeof(IccCurve));
        if (!copy)
            return false;
        for (uint32_t i = 0; i < n; i++) {
            copy[i] = c[i];
            copy[i].inverse = !c[i].inverse;
        }
        c = copy;
    }
    s->curves = c;
    return true;
}

/* tables of 16 bit or 8 bit entries as curves, for the luts of version 2. */
static IccCurve *table_curves(IccXform *x, const uint8_t *p, uint32_t n, uint32_t entries, bool wide)
{
    IccCurve *c = xalloc(x, n * sizeof(IccCurve));
    if (!c)
        return nullptr;
    for (uint32_t i = 0; i < n; i++) {
        c[i].kind = CURVE_TABLE;
        c[i].n = entries;
        c[i].table = xalloc(x, entries * sizeof(float));
        if (!c[i].table)
            return nullptr;
        for (uint32_t j = 0; j < entries; j++)
            c[i].table[j] = wide ? icc_be16(p + (i * entries + j) * 2) / 65535.0f : p[i * entries + j] / 255.0f;
    }
    return c;
}

static bool push_clut(IccXform *x, const uint8_t *p, size_t avail, uint32_t in, uint32_t out, const uint32_t *grid,
                      uint32_t bytes)
{
    size_t total = out;
    for (uint32_t i = 0; i < in; i++) {
        if (grid[i] < 2)
            return false;
        total *= grid[i];
        if (total > (1u << 24))
            return false;
    }
    if (total * bytes > avail)
        return false;
    Stage *s = push(x, ST_CLUT, in, out);
    if (!s)
        return false;
    memcpy(s->grid, grid, in * sizeof(uint32_t));
    float *f = xalloc(x, total * sizeof(float));
    if (!f)
        return false;
    s = &x->stages[x->count - 1];
    s->clut = f;
    for (size_t i = 0; i < total; i++)
        f[i] = bytes == 2 ? icc_be16(p + i * 2) / 65535.0f : p[i] / 255.0f;
    return true;
}

/* the pcs encodings, from the values a lut sees to the actual colors and back. */
enum { ENC_V4, ENC_V2 };

static bool push_pcs_decode(IccXform *x, uint32_t pcs, int encoding)
{
    if (pcs == ICC_SIG('X', 'Y', 'Z', ' ')) {
        const double k = 65535.0 / 32768.0, s[3] = {k, k, k};
        return push_scale(x, 3, s, nullptr);
    }
    double f = encoding == ENC_V2 ? 65535.0 / 65280.0 : 1.0;
    double s[3] = {100 * f, 255 * f, 255 * f}, o[3] = {0, -128, -128};
    if (!push_scale(x, 3, s, o))
        return false;
    return push(x, ST_LAB2XYZ, 3, 3) != nullptr;
}

static bool push_pcs_encode(IccXform *x, uint32_t pcs, int encoding)
{
    if (pcs == ICC_SIG('X', 'Y', 'Z', ' ')) {
        const double k = 32768.0 / 65535.0, s[3] = {k, k, k};
        return push_scale(x, 3, s, nullptr);
    }
    if (!push(x, ST_XYZ2LAB, 3, 3))
        return false;
    double f = encoding == ENC_V2 ? 65280.0 / 65535.0 : 1.0;
    double s[3] = {f / 100, f / 255, f / 255}, o[3] = {0, 128 * f / 255, 128 * f / 255};
    return push_scale(x, 3, s, o);
}

static void read_matrix(const uint8_t *p, double m[9])
{
    for (int i = 0; i < 9; i++)
        m[i] = icc_s15(p + i * 4);
}

/*
 * a lut8, lut16, lutAtoB or lutBtoA tag as stages. in and out are the channel counts the profile expects, encoding
 * gets how the pcs side is encoded. xyz_in says the input is xyz, when the matrix of the old luts applies.
 */
static bool push_lut(IccXform *x, const uint8_t *t, uint32_t len, uint32_t in, uint32_t out, bool xyz_in, int *encoding)
{
    if (len < 32)
        return false;
    uint32_t type = icc_be32(t);
    if (type == ICC_SIG('m', 'f', 't', '1') || type == ICC_SIG('m', 'f', 't', '2')) {
        bool wide = type == ICC_SIG('m', 'f', 't', '2');
        uint32_t tin = t[8], tout = t[9], g = t[10];
        if (tin != in || tout != out || g < 2 || len < 52)
            return false;
        uint32_t in_entries = wide ? icc_be16(t + 48) : 256, out_entries = wide ? icc_be16(t + 50) : 256;
        if (in_entries < 2 || out_entries < 2)
            return false;
        uint32_t bytes = wide ? 2 : 1, at = wide ? 52 : 48;
        if (xyz_in && in == 3) {
            double m[9];
            read_matrix(t + 12, m);
            if (!push_matrix(x, m, nullptr))
                return false;
        }
        if ((size_t)at + (size_t)tin * in_entries * bytes > len)
            return false;
        if (!push_curves(x, table_curves(x, t + at, tin, in_entries, wide), tin, false))
            return false;
        at += tin * in_entries * bytes;
        uint32_t grid[ICC_MAX_CHANNELS];
        size_t cells = tout;
        for (uint32_t i = 0; i < tin; i++) {
            grid[i] = g;
            cells *= g;
        }
        if (!push_clut(x, t + at, len - at, tin, tout, grid, bytes))
            return false;
        at += (uint32_t)(cells * bytes);
        if ((size_t)at + (size_t)tout * out_entries * bytes > len)
            return false;
        if (!push_curves(x, table_curves(x, t + at, tout, out_entries, wide), tout, false))
            return false;
        *encoding = wide ? ENC_V2 : ENC_V4;
        return true;
    }
    bool a2b = type == ICC_SIG('m', 'A', 'B', ' ');
    if (!a2b && type != ICC_SIG('m', 'B', 'A', ' '))
        return false;
    uint32_t tin = t[8], tout = t[9];
    if (tin != in || tout != out)
        return false;
    uint32_t off_b = icc_be32(t + 12), off_m = icc_be32(t + 16), off_mm = icc_be32(t + 20), off_clut = icc_be32(t + 24),
             off_a = icc_be32(t + 28);
    *encoding = ENC_V4;
    if (!off_b)
        return false;
    /* lutAtoB: a, clut, m, matrix, b. lutBtoA: b, matrix, m, clut, a. */
    uint32_t clut_in = a2b ? tin : 3, clut_out = a2b ? 3 : tout;
    bool has_clut = off_clut != 0;
    if (!has_clut && tin != tout)
        return false;
    for (int step = 0; step < 5; step++) {
        int which = a2b ? step : 4 - step; /* 0 a, 1 clut, 2 m, 3 matrix, 4 b. */
        switch (which) {
        case 0:
            if (off_a && !push_curves(x, read_curves(x, t, len, off_a, a2b ? tin : tout), a2b ? tin : tout, false))
                return false;
            break;
        case 1:
            if (has_clut) {
                if (off_clut > len || len - off_clut < 20)
                    return false;
                uint32_t grid[ICC_MAX_CHANNELS];
                for (uint32_t i = 0; i < clut_in; i++)
                    grid[i] = t[off_clut + i];
                uint32_t bytes = t[off_clut + 16];
                if (bytes != 1 && bytes != 2)
                    return false;
                if (!push_clut(x, t + off_clut + 20, len - off_clut - 20, clut_in, clut_out, grid, bytes))
                    return false;
            }
            break;
        case 2:
            if (off_m && !push_curves(x, read_curves(x, t, len, off_m, 3), 3, false))
                return false;
            break;
        case 3:
            if (off_mm) {
                if (off_mm > len || len - off_mm < 48)
                    return false;
                double m[9], o[3];
                read_matrix(t + off_mm, m);
                for (int i = 0; i < 3; i++)
                    o[i] = icc_s15(t + off_mm + 36 + i * 4);
                if (!push_matrix(x, m, o))
                    return false;
            }
            break;
        case 4:
            if (!push_curves(x, read_curves(x, t, len, off_b, a2b ? tout : tin), a2b ? tout : tin, false))
                return false;
            break;
        }
    }
    return true;
}

static uint32_t lut_tag(const Profile *p, bool to_pcs, int intent)
{
    static const char a2b[4][5] = {"A2B0", "A2B1", "A2B2", "A2B1"}, b2a[4][5] = {"B2A0", "B2A1", "B2A2", "B2A1"};
    const char *s = to_pcs ? a2b[intent & 3] : b2a[intent & 3];
    uint32_t sig = ICC_SIG(s[0], s[1], s[2], s[3]), len;
    if (tag(p, sig, &len))
        return sig;
    sig = to_pcs ? ICC_SIG('A', '2', 'B', '0') : ICC_SIG('B', '2', 'A', '0');
    return tag(p, sig, &len) ? sig : 0;
}

static bool is_matrix_shaper(const Profile *p)
{
    uint32_t len;
    return p->space == ICC_SIG('R', 'G', 'B', ' ') && tag(p, ICC_SIG('r', 'X', 'Y', 'Z'), &len) &&
           tag(p, ICC_SIG('g', 'X', 'Y', 'Z'), &len) && tag(p, ICC_SIG('b', 'X', 'Y', 'Z'), &len) &&
           tag(p, ICC_SIG('r', 'T', 'R', 'C'), &len) && tag(p, ICC_SIG('g', 'T', 'R', 'C'), &len) &&
           tag(p, ICC_SIG('b', 'T', 'R', 'C'), &len);
}

static IccCurve *trc_curves(IccXform *x, const Profile *p, uint32_t n)
{
    static const uint32_t sigs[3] = {ICC_SIG('r', 'T', 'R', 'C'), ICC_SIG('g', 'T', 'R', 'C'), ICC_SIG('b', 'T', 'R', 'C')};
    IccCurve *c = xalloc(x, n * sizeof(IccCurve));
    if (!c)
        return nullptr;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t len;
        const uint8_t *t = tag(p, n == 1 ? ICC_SIG('k', 'T', 'R', 'C') : sigs[i], &len);
        if (!t || !read_curve(x, t, len, &c[i]))
            return nullptr;
    }
    return c;
}

static bool colorants(const Profile *p, double m[9])
{
    double r[3], g[3], b[3];
    if (!read_xyz(p, ICC_SIG('r', 'X', 'Y', 'Z'), r) || !read_xyz(p, ICC_SIG('g', 'X', 'Y', 'Z'), g) ||
        !read_xyz(p, ICC_SIG('b', 'X', 'Y', 'Z'), b))
        return false;
    double v[9] = {r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
    memcpy(m, v, sizeof(v));
    return true;
}

/* device to actual xyz, relative to d50. */
static bool leg_to_pcs(IccXform *x, const Profile *p, int intent)
{
    uint32_t sig = lut_tag(p, true, intent), len;
    if (sig) {
        const uint8_t *t = tag(p, sig, &len);
        int encoding;
        if (!push_lut(x, t, len, p->channels, 3, p->space == ICC_SIG('X', 'Y', 'Z', ' '), &encoding))
            return false;
        return push_pcs_decode(x, p->pcs, encoding);
    }
    if (is_matrix_shaper(p)) {
        double m[9];
        if (!colorants(p, m) || !push_curves(x, trc_curves(x, p, 3), 3, false))
            return false;
        return push_matrix(x, m, nullptr);
    }
    if (p->space == ICC_SIG('G', 'R', 'A', 'Y') && tag(p, ICC_SIG('k', 'T', 'R', 'C'), &len)) {
        if (!push_curves(x, trc_curves(x, p, 1), 1, false))
            return false;
        Stage *s = push(x, ST_SCALE, 1, 3);
        if (!s)
            return false;
        for (int i = 0; i < 3; i++)
            s->scale[i] = icc_d50[i];
        return true;
    }
    return false;
}

/* actual xyz relative to d50 to the device. */
static bool leg_from_pcs(IccXform *x, const Profile *p, int intent)
{
    uint32_t sig = lut_tag(p, false, intent), len;
    if (sig) {
        const uint8_t *t = tag(p, sig, &len);
        /* the encoding of the pcs side depends on the tag type, which must be known before the stages. */
        int encoding = t && len >= 4 && icc_be32(t) == ICC_SIG('m', 'f', 't', '2') ? ENC_V2 : ENC_V4;
        if (!push_pcs_encode(x, p->pcs, encoding))
            return false;
        return push_lut(x, t, len, 3, p->channels, p->pcs == ICC_SIG('X', 'Y', 'Z', ' '), &encoding);
    }
    if (is_matrix_shaper(p)) {
        double m[9], inv[9];
        if (!colorants(p, m) || !icc_mat_invert(m, inv) || !push_matrix(x, inv, nullptr))
            return false;
        return push_curves(x, trc_curves(x, p, 3), 3, true);
    }
    if (p->space == ICC_SIG('G', 'R', 'A', 'Y') && tag(p, ICC_SIG('k', 'T', 'R', 'C'), &len)) {
        Stage *s = push(x, ST_SCALE, 3, 1);
        if (!s)
            return false;
        s->scale[0] = 1;
        return push_curves(x, trc_curves(x, p, 1), 1, true);
    }
    return false;
}

/* an abstract profile or a device link: its A2B0 from encoded values to encoded values. */
static bool leg_lut0(IccXform *x, const Profile *p, uint32_t in, uint32_t out)
{
    uint32_t len;
    const uint8_t *t = tag(p, ICC_SIG('A', '2', 'B', '0'), &len);
    int encoding;
    return t && push_lut(x, t, len, in, out, false, &encoding);
}

/* ------------------------------------------------------------------ linking */

/*
 * what goes between the pcs side of one profile and the next: the media whites of absolute colorimetric. like the
 * icm engine, no black point compensation: perceptual tables bring their own black.
 */
static bool link_pcs(IccXform *x, const Profile *from, int from_intent, const Profile *to, int to_intent)
{
    if (from_intent == ICC_INTENT_ABSOLUTE) {
        double d[3];
        for (int i = 0; i < 3; i++)
            d[i] = from->media_white[i] / icc_d50[i];
        if (!push_diag(x, d, nullptr))
            return false;
    }
    if (to_intent == ICC_INTENT_ABSOLUTE) {
        double d[3];
        for (int i = 0; i < 3; i++)
            d[i] = icc_d50[i] / to->media_white[i];
        if (!push_diag(x, d, nullptr))
            return false;
    }
    return true;
}

/* ------------------------------------------------------------------ evaluation */

static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* tetrahedral interpolation in the cell at c of a 3 dimensional table. */
static void tetra(const float *c, const uint32_t *stride, const double *f, uint32_t m, double *out)
{
    uint32_t s0 = stride[0], s1 = stride[1], s2 = stride[2];
    double rx = f[0], ry = f[1], rz = f[2];
    for (uint32_t o = 0; o < m; o++, c++) {
        double c000 = c[0], c111 = c[s0 + s1 + s2], v;
        if (rx >= ry) {
            if (ry >= rz)
                v = c000 + rx * (c[s0] - c000) + ry * (c[s0 + s1] - c[s0]) + rz * (c111 - c[s0 + s1]);
            else if (rx >= rz)
                v = c000 + rx * (c[s0] - c000) + rz * (c[s0 + s2] - c[s0]) + ry * (c111 - c[s0 + s2]);
            else
                v = c000 + rz * (c[s2] - c000) + rx * (c[s0 + s2] - c[s2]) + ry * (c111 - c[s0 + s2]);
        } else {
            if (rz >= ry)
                v = c000 + rz * (c[s2] - c000) + ry * (c[s1 + s2] - c[s2]) + rx * (c111 - c[s1 + s2]);
            else if (rz >= rx)
                v = c000 + ry * (c[s1] - c000) + rz * (c[s1 + s2] - c[s1]) + rx * (c111 - c[s1 + s2]);
            else
                v = c000 + ry * (c[s1] - c000) + rx * (c[s0 + s1] - c[s1]) + rz * (c111 - c[s0 + s1]);
        }
        out[o] = v;
    }
}

/* more than 3 inputs: linear along the first, the rest recursively, as lcms and the icm engines do. */
static void interp(const float *c, uint32_t n, const uint32_t *base, const uint32_t *stride, const double *f, uint32_t m,
                   double *out)
{
    if (n == 3) {
        tetra(c + base[0] * stride[0] + base[1] * stride[1] + base[2] * stride[2], stride, f, m, out);
        return;
    }
    if (n < 3) {
        for (uint32_t o = 0; o < m; o++)
            out[o] = 0;
        for (uint32_t corner = 0; corner < (1u << n); corner++) {
            double w = 1;
            size_t at = 0;
            for (uint32_t i = 0; i < n; i++) {
                bool up = corner >> i & 1;
                w *= up ? f[i] : 1 - f[i];
                at += (size_t)(base[i] + up) * stride[i];
            }
            for (uint32_t o = 0; o < m; o++)
                out[o] += w * c[at + o];
        }
        return;
    }
    double lo[ICC_MAX_CHANNELS], hi[ICC_MAX_CHANNELS];
    const float *c0 = c + base[0] * stride[0];
    interp(c0, n - 1, base + 1, stride + 1, f + 1, m, lo);
    if (f[0] > 0) {
        interp(c0 + stride[0], n - 1, base + 1, stride + 1, f + 1, m, hi);
        for (uint32_t o = 0; o < m; o++)
            out[o] = lo[o] + (hi[o] - lo[o]) * f[0];
    } else {
        memcpy(out, lo, m * sizeof(double));
    }
}

static void clut_eval(const Stage *s, const double *in, double *out)
{
    uint32_t n = s->in, m = s->out;
    uint32_t base[ICC_MAX_CHANNELS], stride[ICC_MAX_CHANNELS];
    double f[ICC_MAX_CHANNELS];
    uint32_t st = m;
    for (int i = (int)n - 1; i >= 0; i--) {
        stride[i] = st;
        st *= s->grid[i];
    }
    for (uint32_t i = 0; i < n; i++) {
        double v = clamp01(in[i]) * (s->grid[i] - 1);
        uint32_t b = (uint32_t)v;
        if (b >= s->grid[i] - 1)
            b = s->grid[i] - 2;
        base[i] = b;
        f[i] = v - b;
    }
    interp(s->clut, n, base, stride, f, m, out);
}

static void run_stages(const IccXform *x, uint32_t stages, const double *in, double *out)
{
    double a[ICC_MAX_CHANNELS], b[ICC_MAX_CHANNELS];
    memcpy(a, in, x->inputs * sizeof(double));
    for (uint32_t i = 0; i < stages; i++) {
        const Stage *s = &x->stages[i];
        switch (s->kind) {
        case ST_CURVES:
            for (uint32_t c = 0; c < s->in; c++)
                b[c] = icc_curve_eval(&s->curves[c], a[c]);
            break;
        case ST_MATRIX:
            for (int r = 0; r < 3; r++)
                b[r] = s->m[r * 3] * a[0] + s->m[r * 3 + 1] * a[1] + s->m[r * 3 + 2] * a[2] + s->m[9 + r];
            break;
        case ST_SCALE:
            if (s->in == s->out) {
                for (uint32_t c = 0; c < s->in; c++)
                    b[c] = a[c] * s->scale[c] + s->offset[c];
            } else if (s->in == 1) {
                for (uint32_t c = 0; c < s->out; c++)
                    b[c] = a[0] * s->scale[c] + s->offset[c];
            } else {
                /* xyz to gray: y. */
                b[0] = a[1] * s->scale[0] + s->offset[0];
            }
            break;
        case ST_CLUT:
            clut_eval(s, a, b);
            break;
        case ST_XYZ2LAB:
            icc_xyz_to_lab(a, b);
            break;
        case ST_LAB2XYZ:
            icc_lab_to_xyz(a, b);
            break;
        case ST_CLAMP:
            for (uint32_t c = 0; c < s->in; c++)
                b[c] = clamp01(a[c]);
            break;
        }
        memcpy(a, b, s->out * sizeof(double));
    }
    memcpy(out, a, x->outputs * sizeof(double));
}

static void run(const IccXform *x, const double *in, double *out) { run_stages(x, x->count, in, out); }

void icc_xform_eval(const IccXform *x, const float *in, float *out, size_t count, bool unclamped)
{
    double a[ICC_MAX_CHANNELS], b[ICC_MAX_CHANNELS];
    for (size_t i = 0; i < count; i++) {
        for (uint32_t c = 0; c < x->inputs; c++)
            a[c] = in[i * x->inputs + c];
        run(x, a, b);
        for (uint32_t c = 0; c < x->outputs; c++)
            out[i * x->outputs + c] = (float)(unclamped ? b[c] : clamp01(b[c]));
    }
}

void icc_xform_gamut(const IccXform *x, const float *in, uint8_t *out, size_t count)
{
    double a[ICC_MAX_CHANNELS], b[ICC_MAX_CHANNELS];
    for (size_t i = 0; i < count; i++) {
        for (uint32_t c = 0; c < x->inputs; c++)
            a[c] = in[i * x->inputs + c];
        run(x, a, b);
        double worst = 0;
        for (uint32_t c = 0; c < x->outputs; c++) {
            double d = b[c] < 0 ? -b[c] : b[c] > 1 ? b[c] - 1 : 0;
            if (d > worst)
                worst = d;
        }
        /* a small miss is rounding, beyond it the distance grows to 255. */
        out[i] = worst < 1.0 / 512 ? 0 : worst > 0.5 ? 255 : (uint8_t)(worst * 510);
    }
}

/* ------------------------------------------------------------------ optimizing */

static bool curve_same(const IccCurve *a, const IccCurve *b)
{
    if (a->kind != b->kind)
        return false;
    if (a->kind == CURVE_IDENTITY)
        return true;
    if (a->kind == CURVE_PARAMETRIC)
        return !memcmp(a->p, b->p, sizeof(a->p));
    return a->n == b->n && !memcmp(a->table, b->table, a->n * sizeof(float));
}

static bool matrix_identity(const Stage *s)
{
    static const double id[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    for (int i = 0; i < 12; i++) {
        double d = s->m[i] - id[i];
        if (d > 1e-5 || d < -1e-5)
            return false;
    }
    return true;
}

static bool stage_identity(const Stage *s)
{
    if (s->kind == ST_MATRIX)
        return matrix_identity(s);
    if (s->kind == ST_SCALE && s->in == s->out) {
        for (uint32_t i = 0; i < s->in; i++)
            if (s->scale[i] != 1 || s->offset[i] != 0)
                return false;
        return true;
    }
    if (s->kind == ST_CURVES) {
        for (uint32_t i = 0; i < s->in; i++)
            if (s->curves[i].kind != CURVE_IDENTITY)
                return false;
        return true;
    }
    return false;
}

static void remove_stage(IccXform *x, uint32_t i)
{
    memmove(&x->stages[i], &x->stages[i + 1], (x->count - i - 1) * sizeof(Stage));
    x->count--;
}

static void optimize(IccXform *x)
{
    /* a 3 channel scale is a matrix. */
    for (uint32_t i = 0; i < x->count; i++) {
        Stage *s = &x->stages[i];
        if (s->kind == ST_SCALE && s->in == 3 && s->out == 3) {
            double m[12] = {s->scale[0], 0, 0, 0, s->scale[1], 0, 0, 0, s->scale[2], s->offset[0], s->offset[1], s->offset[2]};
            s->kind = ST_MATRIX;
            memcpy(s->m, m, sizeof(m));
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t i = 0; i < x->count; i++) {
            Stage *s = &x->stages[i];
            if (stage_identity(s)) {
                remove_stage(x, i);
                changed = true;
                break;
            }
            if (i + 1 >= x->count)
                continue;
            Stage *t = &x->stages[i + 1];
            if (s->kind == ST_MATRIX && t->kind == ST_MATRIX) {
                double m[9], o[3];
                icc_mat_mul(t->m, s->m, m);
                icc_mat_apply(t->m, s->m + 9, o);
                for (int k = 0; k < 3; k++)
                    s->m[9 + k] = o[k] + t->m[9 + k];
                memcpy(s->m, m, sizeof(m));
                remove_stage(x, i + 1);
                changed = true;
                break;
            }
            if ((s->kind == ST_XYZ2LAB && t->kind == ST_LAB2XYZ) || (s->kind == ST_LAB2XYZ && t->kind == ST_XYZ2LAB)) {
                remove_stage(x, i + 1);
                remove_stage(x, i);
                changed = true;
                break;
            }
            if (s->kind == ST_CURVES && t->kind == ST_CURVES && s->in == t->in) {
                bool cancel = true;
                for (uint32_t c = 0; c < s->in && cancel; c++)
                    cancel = s->curves[c].inverse != t->curves[c].inverse && curve_same(&s->curves[c], &t->curves[c]);
                if (cancel) {
                    remove_stage(x, i + 1);
                    remove_stage(x, i);
                    changed = true;
                    break;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ creation */

void icc_xform_destroy(IccXform *x)
{
    if (!x)
        return;
    MimeAllocator a = x->alloc;
    for (Block *b = x->blocks, *next; b; b = next) {
        next = b->next;
        mime_free(&a, b);
    }
    mime_free(&a, x->stages);
    mime_free(&a, x);
}

static bool is_pcs(uint32_t space) { return space == ICC_SIG('X', 'Y', 'Z', ' ') || space == ICC_SIG('L', 'a', 'b', ' '); }

IccXform *icc_xform_create(const MimeAllocator *a, const IccSource *profiles, uint32_t count, const uint32_t *intents,
                           uint32_t intent_count, uint32_t flags, int *error)
{
    int dummy;
    if (!error)
        error = &dummy;
    *error = ICC_ERR_ARGUMENT;
    if (!profiles || !count || count > 64)
        return nullptr;
    IccXform *x = mime_alloc(a, sizeof(IccXform));
    if (!x) {
        *error = ICC_ERR_MEMORY;
        return nullptr;
    }
    memset(x, 0, sizeof(*x));
    x->alloc = *a;
    Profile *p = xalloc(x, count * sizeof(Profile));
    if (!p) {
        *error = ICC_ERR_MEMORY;
        icc_xform_destroy(x);
        return nullptr;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *copy = profiles[i].data && profiles[i].size >= 132 ? xalloc(x, profiles[i].size) : nullptr;
        if (copy)
            memcpy(copy, profiles[i].data, profiles[i].size);
        if (!copy || !profile_open(&p[i], copy, profiles[i].size)) {
            *error = copy || !profiles[i].data ? ICC_ERR_PROFILE : ICC_ERR_MEMORY;
            icc_xform_destroy(x);
            return nullptr;
        }
    }
    int fail = ICC_ERR_PROFILE;
    /* where the colors are between legs: on a device of so many channels, or in actual xyz. */
    bool in_pcs = false;
    uint32_t device_space = 0;
    const Profile *last = nullptr;
    int last_intent = 0;
    for (uint32_t i = 0; i < count; i++) {
        const Profile *q = &p[i];
        int intent = (int)(intents && intent_count ? intents[i < intent_count ? i : intent_count - 1] : 0) & 3;
        bool ok;
        if (i == 0) {
            x->inputs = q->channels;
            x->in_space = q->space;
            if (q->device_class == ICC_SIG('l', 'i', 'n', 'k')) {
                ok = leg_lut0(x, q, q->channels, icc_space_channels(q->pcs));
                device_space = q->pcs;
            } else if (q->device_class == ICC_SIG('a', 'b', 's', 't')) {
                int enc = ENC_V4;
                ok = push_pcs_decode(x, q->space, enc) && push_pcs_encode(x, q->space, enc) &&
                     leg_lut0(x, q, 3, 3) && push_pcs_decode(x, q->pcs, enc);
                in_pcs = true;
            } else {
                ok = leg_to_pcs(x, q, intent);
                in_pcs = true;
            }
        } else if (q->device_class == ICC_SIG('l', 'i', 'n', 'k')) {
            if (in_pcs || device_space != q->space) {
                fail = ICC_ERR_COLORSPACE;
                ok = false;
            } else {
                ok = leg_lut0(x, q, q->channels, icc_space_channels(q->pcs));
                device_space = q->pcs;
            }
        } else if (!in_pcs) {
            /* from a device link to a device profile: the device is that of the profile. */
            if (device_space != q->space) {
                fail = ICC_ERR_COLORSPACE;
                ok = false;
            } else if (i == count - 1) {
                /* a device link followed by its output profile changes nothing more. */
                ok = true;
                device_space = q->space;
            } else {
                ok = leg_to_pcs(x, q, intent);
                in_pcs = true;
            }
        } else if (q->device_class == ICC_SIG('a', 'b', 's', 't')) {
            ok = link_pcs(x, last, last_intent, q, ICC_INTENT_RELATIVE) && push_pcs_encode(x, q->space, ENC_V4) &&
                 leg_lut0(x, q, 3, 3) && push_pcs_decode(x, q->pcs, ENC_V4);
            intent = ICC_INTENT_RELATIVE;
        } else {
            ok = link_pcs(x, last, last_intent, q, intent) && leg_from_pcs(x, q, intent);
            device_space = q->space;
            in_pcs = false;
            if (ok && i < count - 1) {
                /* a profile in the middle: back to the pcs. a proof target leaves with the intent of the last. */
                if ((flags & ICC_XFORM_PROOF) && i == count - 2)
                    intent = (int)(intents && intent_count ? intents[intent_count - 1] : 0) & 3;
                ok = leg_to_pcs(x, q, intent);
                in_pcs = true;
            }
        }
        if (!ok) {
            *error = fail;
            icc_xform_destroy(x);
            return nullptr;
        }
        last = q;
        last_intent = intent;
    }
    if (in_pcs) {
        /* the transform ends in the pcs of the last profile, encoded as version 4 luts see it. */
        uint32_t pcs = last->pcs;
        if (!push_pcs_encode(x, pcs, ENC_V4)) {
            *error = ICC_ERR_MEMORY;
            icc_xform_destroy(x);
            return nullptr;
        }
        x->out_space = pcs;
        x->outputs = 3;
    } else {
        x->out_space = device_space;
        x->outputs = icc_space_channels(device_space);
    }
    if (x->in_space && is_pcs(x->in_space) && p[0].device_class == ICC_SIG('a', 'b', 's', 't'))
        x->inputs = 3;
    optimize(x);
    x->identity = x->count == 0 && x->inputs == x->outputs;
    *error = ICC_OK;
    return x;
}

uint32_t icc_xform_inputs(const IccXform *x) { return x->inputs; }
uint32_t icc_xform_outputs(const IccXform *x) { return x->outputs; }
uint32_t icc_xform_input_space(const IccXform *x) { return x->in_space; }
uint32_t icc_xform_output_space(const IccXform *x) { return x->out_space; }
bool icc_xform_identity(const IccXform *x) { return x->identity; }

/* ------------------------------------------------------------------ 8 bit tables */

static double sqrt_approx(double v)
{
    if (v <= 0)
        return 0;
    /* a guess from halving the exponent, then newton steps. */
    union {
        double d;
        uint64_t u;
    } b = {v};
    b.u = (b.u >> 1) + 0x1ff8000000000000ull;
    double r = b.d;
    for (int i = 0; i < 4; i++)
        r = 0.5 * (r + v / r);
    return r;
}

static bool fill_out_table(IccXform *x, const Stage *curves)
{
    x->out_table = xalloc(x, curves->in * OUT_POINTS * sizeof(float));
    if (!x->out_table)
        return false;
    /* sampled over the square root of the input, where curves that start steep are smooth. */
    for (uint32_t c = 0; c < curves->in; c++)
        for (int i = 0; i < OUT_POINTS; i++) {
            double u = i / (double)(OUT_POINTS - 1);
            x->out_table[c * OUT_POINTS + i] = (float)clamp01(icc_curve_eval(&curves->curves[c], u * u));
        }
    return true;
}

static double post_curve(const IccXform *x, uint32_t c, double v)
{
    v = sqrt_approx(clamp01(v)) * (OUT_POINTS - 1);
    uint32_t k = (uint32_t)v;
    if (k >= OUT_POINTS - 1)
        k = OUT_POINTS - 2;
    double f = v - k;
    const float *t = x->out_table + c * OUT_POINTS + k;
    return t[0] + (t[1] - t[0]) * f;
}

bool icc_xform_prepare8(IccXform *x)
{
    if (x->fast != FAST_NONE)
        return true;
    if (x->identity) {
        x->fast = FAST_IDENTITY;
        return true;
    }
    Stage *s = x->stages;
    if (x->count == 3 && x->inputs == 3 && x->outputs == 3 && s[0].kind == ST_CURVES && s[1].kind == ST_MATRIX &&
        s[2].kind == ST_CURVES) {
        x->in_table = xalloc(x, 3 * 256 * sizeof(float));
        if (!x->in_table || !fill_out_table(x, &s[2]))
            return false;
        for (int c = 0; c < 3; c++)
            for (int i = 0; i < 256; i++)
                x->in_table[c * 256 + i] = (float)icc_curve_eval(&s[0].curves[c], i / 255.0);
        memcpy(x->fm, s[1].m, sizeof(x->fm));
        x->fast = FAST_SHAPER;
        return true;
    }
    uint32_t n = x->inputs;
    uint32_t points = n == 1 ? 256 : n == 2 ? 65 : n == 3 ? 33 : n == 4 ? 17 : 0;
    if (!points)
        return true;
    size_t cells = 1;
    for (uint32_t i = 0; i < n; i++)
        cells *= points;
    x->lut = xalloc(x, cells * x->outputs * sizeof(float));
    if (!x->lut)
        return false;
    /* the last curves stay out of the grid: the grid then holds values that interpolate well, as linear light. */
    uint32_t stages = x->count;
    Stage *tail = stages ? &x->stages[stages - 1] : nullptr;
    if (tail && tail->kind == ST_CURVES && tail->in == x->outputs) {
        if (!fill_out_table(x, tail))
            return false;
        x->post = true;
        stages--;
    }
    double in[ICC_MAX_CHANNELS], out[ICC_MAX_CHANNELS];
    for (size_t cell = 0; cell < cells; cell++) {
        size_t rest = cell;
        for (int i = (int)n - 1; i >= 0; i--) {
            in[i] = (double)(rest % points) / (points - 1);
            rest /= points;
        }
        run_stages(x, stages, in, out);
        /* unclamped, so cells across the gamut edge interpolate to the edge rather than inside. */
        for (uint32_t o = 0; o < x->outputs; o++)
            x->lut[cell * x->outputs + o] = (float)(out[o] < -4 ? -4 : out[o] > 4 ? 4 : out[o]);
    }
    x->lut_points = points;
    x->fast = FAST_LUT;
    return true;
}

static uint8_t to8(double v)
{
    v = v * 255 + 0.5;
    return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t)v;
}

void icc_xform_eval8(const IccXform *x, const uint8_t *in, size_t in_step, uint8_t *out, size_t out_step, size_t count)
{
    uint32_t ni = x->inputs, no = x->outputs;
    if (x->fast == FAST_IDENTITY) {
        for (size_t i = 0; i < count; i++, in += in_step, out += out_step)
            for (uint32_t c = 0; c < no; c++)
                out[c] = c < ni ? in[c] : 0;
        return;
    }
    if (x->fast == FAST_SHAPER) {
        const double *m = x->fm;
        for (size_t i = 0; i < count; i++, in += in_step, out += out_step) {
            double r = x->in_table[in[0]], g = x->in_table[256 + in[1]], b = x->in_table[512 + in[2]];
            for (int c = 0; c < 3; c++) {
                double v = m[c * 3] * r + m[c * 3 + 1] * g + m[c * 3 + 2] * b + m[9 + c];
                out[c] = to8(post_curve(x, c, v));
            }
        }
        return;
    }
    if (x->fast == FAST_LUT) {
        Stage s = {.kind = ST_CLUT, .in = ni, .out = no, .clut = x->lut};
        for (uint32_t i = 0; i < ni; i++)
            s.grid[i] = x->lut_points;
        double a[ICC_MAX_CHANNELS], b[ICC_MAX_CHANNELS];
        for (size_t i = 0; i < count; i++, in += in_step, out += out_step) {
            for (uint32_t c = 0; c < ni; c++)
                a[c] = in[c] / 255.0;
            if (ni == 1)
                for (uint32_t c = 0; c < no; c++)
                    b[c] = x->lut[in[0] * no + c];
            else
                clut_eval(&s, a, b);
            for (uint32_t c = 0; c < no; c++)
                out[c] = to8(x->post ? post_curve(x, c, b[c]) : b[c]);
        }
        return;
    }
    double a[ICC_MAX_CHANNELS], b[ICC_MAX_CHANNELS];
    for (size_t i = 0; i < count; i++, in += in_step, out += out_step) {
        for (uint32_t c = 0; c < ni; c++)
            a[c] = in[c] / 255.0;
        run(x, a, b);
        for (uint32_t c = 0; c < no; c++)
            out[c] = to8(clamp01(b[c]));
    }
}
