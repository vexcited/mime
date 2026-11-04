#include "icc_internal.h"

typedef struct Writer {
    uint8_t *p;
    uint32_t size, capacity;
    bool failed;
    const MimeAllocator *a;
} Writer;

static uint8_t *room(Writer *w, uint32_t n)
{
    if (w->failed)
        return nullptr;
    if (w->size + n > w->capacity) {
        uint32_t cap = w->capacity ? w->capacity : 1024;
        while (cap < w->size + n)
            cap *= 2;
        uint8_t *q = mime_alloc(w->a, cap);
        if (!q) {
            w->failed = true;
            return nullptr;
        }
        memset(q, 0, cap);
        if (w->size)
            memcpy(q, w->p, w->size);
        mime_free(w->a, w->p);
        w->p = q;
        w->capacity = cap;
    }
    uint8_t *at = w->p + w->size;
    w->size += n;
    return at;
}

static void put32(Writer *w, uint32_t v)
{
    uint8_t *p = room(w, 4);
    if (p)
        icc_put32(p, v);
}

static void put16(Writer *w, uint32_t v)
{
    uint8_t *p = room(w, 2);
    if (p)
        icc_put16(p, v);
}

static void put_s15(Writer *w, double v)
{
    uint8_t *p = room(w, 4);
    if (p)
        icc_put_s15(p, v);
}

static void align(Writer *w)
{
    while (w->size & 3)
        room(w, 1);
}

typedef struct TagRef {
    uint32_t sig, offset, length;
} TagRef;

static void begin_tag(Writer *w, TagRef *t, uint32_t sig)
{
    align(w);
    t->sig = sig;
    t->offset = w->size;
}

static void end_tag(Writer *w, TagRef *t) { t->length = w->size - t->offset; }

static void xyz_tag(Writer *w, TagRef *t, uint32_t sig, const double v[3])
{
    begin_tag(w, t, sig);
    put32(w, ICC_SIG('X', 'Y', 'Z', ' '));
    put32(w, 0);
    for (int i = 0; i < 3; i++)
        put_s15(w, v[i]);
    end_tag(w, t);
}

static void text_tag(Writer *w, TagRef *t, uint32_t sig, const char *s, int version, bool description)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    begin_tag(w, t, sig);
    if (version >= 4) {
        put32(w, ICC_SIG('m', 'l', 'u', 'c'));
        put32(w, 0);
        put32(w, 1);
        put32(w, 12);
        put16(w, 'e' << 8 | 'n');
        put16(w, 'U' << 8 | 'S');
        put32(w, n * 2);
        put32(w, 28);
        for (uint32_t i = 0; i < n; i++)
            put16(w, (uint8_t)s[i]);
    } else if (description) {
        put32(w, ICC_SIG('d', 'e', 's', 'c'));
        put32(w, 0);
        put32(w, n + 1);
        uint8_t *p = room(w, n + 1);
        if (p)
            memcpy(p, s, n);
        /* empty unicode and script code parts. */
        room(w, 4 + 4 + 2 + 1 + 67);
    } else {
        put32(w, ICC_SIG('t', 'e', 'x', 't'));
        put32(w, 0);
        uint8_t *p = room(w, n + 1);
        if (p)
            memcpy(p, s, n);
    }
    end_tag(w, t);
}

static double srgb_eotf(double v) { return v <= 0.04045 ? v / 12.92 : icc_pow((v + 0.055) / 1.055, 2.4); }

static double pq_eotf(double v)
{
    const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32,
                 c3 = 2392.0 / 4096 * 32;
    double e = icc_pow(v, 1 / m2), num = e - c1;
    if (num < 0)
        num = 0;
    return icc_pow(num / (c2 - c3 * e), 1 / m1);
}

static double hlg_inverse_oetf(double v)
{
    const double a = 0.17883277, b = 0.28466892, c = 0.55991073;
    return v <= 0.5 ? v * v / 3 : (icc_exp((v - c) / a) + b) / 12;
}

static void curve_tag(Writer *w, TagRef *t, uint32_t sig, int curve, double gamma, int version)
{
    begin_tag(w, t, sig);
    if (version >= 4 && (curve == ICC_CURVE_SRGB || curve == ICC_CURVE_LINEAR || curve == ICC_CURVE_GAMMA)) {
        put32(w, ICC_SIG('p', 'a', 'r', 'a'));
        put32(w, 0);
        if (curve == ICC_CURVE_SRGB) {
            put16(w, 3);
            put16(w, 0);
            put_s15(w, 2.4);
            put_s15(w, 1 / 1.055);
            put_s15(w, 0.055 / 1.055);
            put_s15(w, 1 / 12.92);
            put_s15(w, 0.04045);
        } else {
            put16(w, 0);
            put16(w, 0);
            put_s15(w, curve == ICC_CURVE_LINEAR ? 1.0 : gamma);
        }
        end_tag(w, t);
        return;
    }
    put32(w, ICC_SIG('c', 'u', 'r', 'v'));
    put32(w, 0);
    if (curve == ICC_CURVE_LINEAR) {
        put32(w, 0);
    } else if (curve == ICC_CURVE_GAMMA) {
        put32(w, 1);
        put16(w, (uint32_t)(gamma * 256 + 0.5));
    } else {
        uint32_t n = curve == ICC_CURVE_SRGB || curve == ICC_CURVE_SRGB_TABLE ? 1024 : 4096;
        put32(w, n);
        for (uint32_t i = 0; i < n; i++) {
            double v = (double)i / (n - 1);
            double y = curve == ICC_CURVE_PQ ? pq_eotf(v) : curve == ICC_CURVE_HLG ? hlg_inverse_oetf(v) : srgb_eotf(v);
            put16(w, (uint32_t)(y * 65535 + 0.5));
        }
    }
    end_tag(w, t);
}

/* the rgb to xyz matrix of primaries and a white, columns being the colorants, white at y 1. */
static bool primaries_matrix(const double pr[3][2], const double white[2], double m[9], double w[3])
{
    double p[9];
    for (int i = 0; i < 3; i++) {
        double x = pr[i][0], y = pr[i][1];
        if (y <= 0)
            return false;
        p[i] = x / y;
        p[3 + i] = 1;
        p[6 + i] = (1 - x - y) / y;
    }
    if (white[1] <= 0)
        return false;
    w[0] = white[0] / white[1];
    w[1] = 1;
    w[2] = (1 - white[0] - white[1]) / white[1];
    double inv[9], s[3];
    if (!icc_mat_invert(p, inv))
        return false;
    icc_mat_apply(inv, w, s);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            m[r * 3 + c] = p[r * 3 + c] * s[c];
    return true;
}

uint8_t *icc_build_rgb(const MimeAllocator *a, const IccRgbSpec *spec, uint32_t *size)
{
    double m[9], white[3] = {icc_d50[0], icc_d50[1], icc_d50[2]}, chad[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (spec->xyz) {
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++)
                m[r * 3 + c] = spec->xyz[c][r];
    } else {
        double raw[9];
        if (!primaries_matrix(spec->primaries, spec->white, raw, white))
            return nullptr;
        icc_bradford(white, icc_d50, chad);
        icc_mat_mul(chad, raw, m);
    }
    const double *media = spec->media_white ? spec->media_white : spec->version >= 4 || spec->xyz ? icc_d50 : white;
    if (spec->version >= 4)
        media = icc_d50;
    Writer w = {.a = a};
    room(&w, 128);
    TagRef tags[12];
    uint32_t n = 0;
    uint32_t table = 132;
    /* the table is written last: leave room for at most 12 entries. */
    room(&w, 4 + 12 * 12);
    text_tag(&w, &tags[n++], ICC_SIG('d', 'e', 's', 'c'), spec->description ? spec->description : "RGB", spec->version, true);
    text_tag(&w, &tags[n++], ICC_SIG('c', 'p', 'r', 't'), "No copyright, use freely", spec->version, false);
    xyz_tag(&w, &tags[n++], ICC_SIG('w', 't', 'p', 't'), media);
    if (spec->version >= 4) {
        TagRef *t = &tags[n++];
        begin_tag(&w, t, ICC_SIG('c', 'h', 'a', 'd'));
        put32(&w, ICC_SIG('s', 'f', '3', '2'));
        put32(&w, 0);
        for (int i = 0; i < 9; i++)
            put_s15(&w, chad[i]);
        end_tag(&w, t);
    }
    static const uint32_t colorant_sigs[3] = {ICC_SIG('r', 'X', 'Y', 'Z'), ICC_SIG('g', 'X', 'Y', 'Z'), ICC_SIG('b', 'X', 'Y', 'Z')};
    for (int c = 0; c < 3; c++) {
        double v[3] = {m[c], m[3 + c], m[6 + c]};
        xyz_tag(&w, &tags[n++], colorant_sigs[c], v);
    }
    curve_tag(&w, &tags[n], ICC_SIG('r', 'T', 'R', 'C'), spec->curve, spec->gamma, spec->version);
    tags[n + 1] = tags[n];
    tags[n + 1].sig = ICC_SIG('g', 'T', 'R', 'C');
    tags[n + 2] = tags[n];
    tags[n + 2].sig = ICC_SIG('b', 'T', 'R', 'C');
    n += 3;
    align(&w);
    if (w.failed) {
        mime_free(a, w.p);
        return nullptr;
    }
    /* the data was placed after a table of 12 entries: move it down to follow the real table. */
    uint32_t data_at = table + 4 + 12 * 12, real = table + 4 + 12 * n, shift = data_at - real;
    memmove(w.p + real, w.p + data_at, w.size - data_at);
    w.size -= shift;
    uint8_t *p = w.p;
    icc_put32(p + 128, n);
    for (uint32_t i = 0; i < n; i++) {
        icc_put32(p + 132 + i * 12, tags[i].sig);
        icc_put32(p + 136 + i * 12, tags[i].offset - shift);
        icc_put32(p + 140 + i * 12, tags[i].length);
    }
    icc_put32(p, w.size);
    icc_put32(p + 8, spec->version >= 4 ? 0x04300000 : 0x02100000);
    icc_put32(p + 12, spec->device_class ? spec->device_class : ICC_SIG('m', 'n', 't', 'r'));
    icc_put32(p + 16, ICC_SIG('R', 'G', 'B', ' '));
    icc_put32(p + 20, ICC_SIG('X', 'Y', 'Z', ' '));
    icc_put16(p + 24, 2024);
    icc_put16(p + 26, 1);
    icc_put16(p + 28, 1);
    icc_put32(p + 36, ICC_SIG('a', 'c', 's', 'p'));
    icc_put32(p + 40, ICC_SIG('M', 'S', 'F', 'T'));
    for (int i = 0; i < 3; i++)
        icc_put_s15(p + 68 + i * 4, icc_d50[i]);
    *size = w.size;
    return p;
}

uint8_t *icc_build_srgb(const MimeAllocator *a, uint32_t *size)
{
    static const double d65[3] = {0.9505, 1.0, 1.0891};
    IccRgbSpec spec = {.primaries = {{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}},
                       .white = {0.3127, 0.3290},
                       .media_white = d65,
                       .curve = ICC_CURVE_SRGB_TABLE,
                       .version = 2,
                       .description = "sRGB IEC61966-2.1"};
    return icc_build_rgb(a, &spec, size);
}
