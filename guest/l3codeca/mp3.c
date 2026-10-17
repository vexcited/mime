/*
 * the layer iii decoding of iso/iec 11172-3 and of the low rates of iso/iec
 * 13818-3, with mpeg 2.5, behind the input ring l3codeca keeps: frames are
 * found bit by bit, locked by a second header, decoded in floating point
 * and cut to 16 or 8 bit pcm by truncation.
 */
#include "mp3.h"

#define MASK 0xFFFE0C00u

typedef struct Frame {
    BYTE lsf, sri, channels, mode, mode_ext, crc, granules;
    DWORD length;
} Frame;

typedef struct Granule {
    WORD part23, big_values, global_gain, compress;
    BYTE block_type, mixed, table[3], subgain[3], region0, region1, preflag, scale, quad;
} Granule;

typedef struct Side {
    WORD begin;
    BYTE scfsi[2];
    Granule g[2][2];
} Side;

typedef struct Scales {
    BYTE l[22], s[13][3];
    BYTE illegal_l[22], illegal_s[13];
} Scales;

typedef struct Bits {
    const BYTE *p;
    DWORD pos;
} Bits;

static const WORD BITRATES[2][15] = {
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
};
static const DWORD RATES[9] = {44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000};
static const BYTE PRETAB[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 2, 0};
static const float POW2Q[4] = {1.0f, 1.189207115f, 1.414213562f, 1.681792831f};

/* the number of scalefactors of each partition of 13818-3, by table, then long, short and mixed blocks. */
static const BYTE LSF_COUNTS[6][3][4] = {
    {{6, 5, 5, 5}, {9, 9, 9, 9}, {6, 9, 9, 9}},
    {{6, 5, 7, 3}, {9, 9, 12, 6}, {6, 9, 12, 6}},
    {{11, 10, 0, 0}, {18, 18, 0, 0}, {15, 18, 0, 0}},
    {{7, 7, 7, 0}, {12, 12, 12, 0}, {6, 15, 12, 0}},
    {{6, 6, 6, 3}, {12, 9, 9, 6}, {6, 12, 9, 6}},
    {{8, 8, 5, 0}, {15, 12, 9, 0}, {6, 18, 9, 0}},
};

static WORD g_tree[2 * 2048];
static WORD g_roots[34];
static float g_pow43[8207];
static float g_window[512];

/* ------------------------------------------------------------------ tables */

static void tree_add(WORD *count, WORD root, DWORD code, int length, WORD value)
{
    WORD node = root;
    for (int i = length - 1; i >= 0; i--) {
        WORD *slot = &g_tree[2 * node + ((code >> i) & 1)];
        if (!i) {
            *slot = 0x8000 | value;
            break;
        }
        if (!*slot)
            *slot = (*count)++;
        node = *slot;
    }
}

/* the huffman trees, the 4/3 powers and the float window, built once per process. */
void Mp3Setup(void)
{
    WORD count = 0;
    for (int t = 0; t < 34; t++) {
        const uint32_t *codes;
        int entries, size = 0;
        if (t < 32) {
            if (!MP3_HUFF[t].codes || MP3_HUFF[t].codes == MP3_HUFF[t - 1].codes)
                continue;
            codes = MP3_HUFF[t].codes;
            size = MP3_HUFF[t].size;
            entries = size * size;
        } else {
            codes = MP3_QUAD[t - 32];
            entries = 16;
        }
        WORD root = count++;
        g_roots[t] = root;
        for (int i = 0; i < entries; i++) {
            WORD value = size ? (WORD)((i / size) << 4 | (i % size)) : (WORD)i;
            tree_add(&count, root, codes[i] & 0xFFFFFF, codes[i] >> 24, value);
        }
    }
    for (int t = 1; t < 32; t++)
        if (MP3_HUFF[t].codes && MP3_HUFF[t].codes == MP3_HUFF[t - 1].codes)
            g_roots[t] = g_roots[t - 1];
    double y = 1.0;
    for (int i = 1; i < 8207; i++) {
        for (int n = 0; n < 4; n++)
            y -= (y * y * y - i) / (3 * y * y);
        g_pow43[i] = (float)(i * y);
    }
    for (int i = 0; i < 512; i++)
        g_window[i] = MP3_WINDOW[i] / 65536.0f;
}

static float pow2q(int e)
{
    int i = e >> 2;
    if (i < -126)
        return 0.0f;
    union {
        uint32_t u;
        float f;
    } x = {.u = (uint32_t)(i + 127) << 23};
    return x.f * POW2Q[e & 3];
}

/* ------------------------------------------------------------------ bits */

static inline DWORD be32(const BYTE *p)
{
    return (DWORD)p[0] << 24 | (DWORD)p[1] << 16 | (DWORD)p[2] << 8 | p[3];
}

static inline DWORD get(Bits *b, int n)
{
    if (!n)
        return 0;
    DWORD v = be32(b->p + (b->pos >> 3)) << (b->pos & 7) >> (32 - n);
    b->pos += n;
    return v;
}

static inline DWORD get1(Bits *b)
{
    DWORD v = (b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1;
    b->pos++;
    return v;
}

static inline WORD huff(Bits *b, int table)
{
    WORD node = g_roots[table];
    for (;;) {
        WORD next = g_tree[2 * node + get1(b)];
        if (next & 0x8000)
            return next & 0x7FFF;
        if (!next)
            return 0;
        node = next;
    }
}

/* ------------------------------------------------------------------ headers */

static BOOL parse_header(DWORD h, Frame *f)
{
    if ((h & 0xFFE00000) != 0xFFE00000)
        return FALSE;
    DWORD version = (h >> 19) & 3, layer = (h >> 17) & 3, rate = (h >> 12) & 15, sr = (h >> 10) & 3;
    if (version == 1 || layer != 1 || !rate || rate == 15 || sr == 3)
        return FALSE;
    f->lsf = version != 3;
    f->sri = (BYTE)((version == 3 ? 0 : version == 2 ? 3 : 6) + sr);
    f->mode = (h >> 6) & 3;
    f->mode_ext = (h >> 4) & 3;
    f->channels = f->mode == 3 ? 1 : 2;
    f->crc = !((h >> 16) & 1);
    f->granules = f->lsf ? 1 : 2;
    f->length = (f->lsf ? 72000 : 144000) * BITRATES[f->lsf][rate] / RATES[f->sri] + ((h >> 9) & 1);
    return TRUE;
}

/* the bits of the stream at a bit position, the ring padded past its end. */
static DWORD peek32(const Mp3Decoder *d, DWORD position)
{
    const BYTE *p = d->ring + (position >> 3);
    DWORD s = position & 7, v = be32(p);
    return s ? v << s | p[4] >> (8 - s) : v;
}

/* ------------------------------------------------------------------ side information */

/* a window switched granule of the reserved block type 0 makes the frame unusable. */
static BOOL read_side(Bits *b, const Frame *f, Side *s)
{
    int nch = f->channels;
    s->begin = (WORD)get(b, f->lsf ? 8 : 9);
    get(b, f->lsf ? nch : (nch == 1 ? 5 : 3));
    if (!f->lsf)
        for (int ch = 0; ch < nch; ch++)
            s->scfsi[ch] = (BYTE)get(b, 4);
    BOOL ok = TRUE;
    for (int gr = 0; gr < f->granules; gr++)
        for (int ch = 0; ch < nch; ch++) {
            Granule *g = &s->g[gr][ch];
            g->part23 = (WORD)get(b, 12);
            g->big_values = (WORD)get(b, 9);
            g->global_gain = (WORD)get(b, 8);
            g->compress = (WORD)get(b, f->lsf ? 9 : 4);
            if (get1(b)) {
                g->block_type = (BYTE)get(b, 2);
                g->mixed = (BYTE)get1(b);
                g->table[0] = (BYTE)get(b, 5);
                g->table[1] = (BYTE)get(b, 5);
                g->table[2] = 0;
                for (int w = 0; w < 3; w++)
                    g->subgain[w] = (BYTE)get(b, 3);
                g->region0 = g->block_type == 2 ? 8 : 7;
                g->region1 = 36;
                if (!g->block_type)
                    ok = FALSE;
            } else {
                g->block_type = g->mixed = 0;
                for (int i = 0; i < 3; i++)
                    g->table[i] = (BYTE)get(b, 5);
                g->subgain[0] = g->subgain[1] = g->subgain[2] = 0;
                g->region0 = (BYTE)get(b, 4);
                g->region1 = (BYTE)get(b, 3);
            }
            g->preflag = f->lsf ? 0 : (BYTE)get1(b);
            g->scale = (BYTE)get1(b);
            g->quad = (BYTE)get1(b);
            if (g->big_values > 288)
                g->big_values = g->global_gain = g->table[0] = g->table[1] = g->table[2] = 0;
        }
    return ok;
}

/* ------------------------------------------------------------------ scalefactors */

static void read_scales_mpeg1(Bits *b, const Granule *g, int gr, BYTE scfsi, Scales *sc)
{
    static const BYTE SLEN[2][16] = {
        {0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4},
        {0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3},
    };
    int s1 = SLEN[0][g->compress], s2 = SLEN[1][g->compress];
    memset(sc->illegal_l, 7, sizeof(sc->illegal_l));
    memset(sc->illegal_s, 7, sizeof(sc->illegal_s));
    if (g->block_type == 2) {
        int sfb = 0;
        if (g->mixed) {
            for (; sfb < 8; sfb++)
                sc->l[sfb] = (BYTE)get(b, s1);
            sfb = 3;
        }
        for (; sfb < 12; sfb++)
            for (int w = 0; w < 3; w++)
                sc->s[sfb][w] = (BYTE)get(b, sfb < 6 ? s1 : s2);
        sc->s[12][0] = sc->s[12][1] = sc->s[12][2] = 0;
        return;
    }
    static const BYTE GROUPS[5] = {0, 6, 11, 16, 21};
    for (int i = 0; i < 4; i++) {
        if (gr && (scfsi & (8 >> i)))
            continue;
        for (int sfb = GROUPS[i]; sfb < GROUPS[i + 1]; sfb++)
            sc->l[sfb] = (BYTE)get(b, i < 2 ? s1 : s2);
    }
    sc->l[21] = 0;
}

static void read_scales_lsf(Bits *b, const Frame *f, Granule *g, int ch, Scales *sc)
{
    BYTE slen[4];
    int table, c = g->compress;
    BOOL intensity_right = (f->mode_ext & 1) && f->mode == 1 && ch == 1;
    if (!intensity_right) {
        if (c < 400) {
            slen[0] = (BYTE)((c >> 4) / 5), slen[1] = (BYTE)((c >> 4) % 5), slen[2] = (BYTE)((c & 15) >> 2),
            slen[3] = (BYTE)(c & 3), table = 0;
        } else if (c < 500) {
            c -= 400;
            slen[0] = (BYTE)((c >> 2) / 5), slen[1] = (BYTE)((c >> 2) % 5), slen[2] = (BYTE)(c & 3), slen[3] = 0,
            table = 1;
        } else {
            c -= 500;
            slen[0] = (BYTE)(c / 3), slen[1] = (BYTE)(c % 3), slen[2] = slen[3] = 0, table = 2;
            g->preflag = 1;
        }
    } else {
        c >>= 1;
        if (c < 180) {
            slen[0] = (BYTE)(c / 36), slen[1] = (BYTE)((c % 36) / 6), slen[2] = (BYTE)(c % 36 % 6), slen[3] = 0,
            table = 3;
        } else if (c < 244) {
            c -= 180;
            slen[0] = (BYTE)((c & 63) >> 4), slen[1] = (BYTE)((c & 15) >> 2), slen[2] = (BYTE)(c & 3), slen[3] = 0,
            table = 4;
        } else {
            c -= 244;
            slen[0] = (BYTE)(c / 3), slen[1] = (BYTE)(c % 3), slen[2] = slen[3] = 0, table = 5;
        }
    }
    const BYTE *counts = LSF_COUNTS[table][g->block_type != 2 ? 0 : g->mixed ? 2 : 1];
    /* the scalefactors run over long bands, then short bands window by window. */
    int long_bands = g->block_type != 2 ? 21 : g->mixed ? 6 : 0, slot = 0;
    for (int part = 0; part < 4; part++)
        for (int i = 0; i < counts[part]; i++, slot++) {
            BYTE v = (BYTE)get(b, slen[part]), illegal = (BYTE)((1 << slen[part]) - 1);
            if (slot < long_bands) {
                sc->l[slot] = v;
                sc->illegal_l[slot] = illegal;
                continue;
            }
            int index = slot - long_bands + (g->mixed ? 9 : 0);
            sc->s[index / 3][index % 3] = v;
            sc->illegal_s[index / 3] = illegal;
        }
    sc->l[21] = 0;
    sc->illegal_l[21] = sc->illegal_l[20];
    sc->s[12][0] = sc->s[12][1] = sc->s[12][2] = 0;
    sc->illegal_s[12] = sc->illegal_s[11];
}

/* ------------------------------------------------------------------ spectrum */

/* the huffman values of a granule, up to its part2_3_length; the end of what may be nonzero is returned. */
static int read_values(Bits *b, DWORD end, const Granule *g, const Mp3Bands *bands, int *is)
{
    int big = g->big_values * 2, r1, r2;
    if (g->block_type) {
        r1 = g->block_type == 2 ? 3 * bands->s[3] : bands->l[8];
        r2 = 576;
    } else {
        int a = g->region0 + 1, c = g->region0 + g->region1 + 2;
        r1 = bands->l[a > 22 ? 22 : a];
        r2 = bands->l[c > 22 ? 22 : c];
    }
    r1 = r1 > big ? big : r1;
    r2 = r2 > big ? big : r2;
    int i = 0;
    for (int region = 0; region < 3; region++) {
        int stop = region == 0 ? r1 : region == 1 ? r2 : big, t = g->table[region];
        int linbits = MP3_HUFF[t].linbits;
        if (!MP3_HUFF[t].codes) {
            for (; i < stop; i++)
                is[i] = 0;
            continue;
        }
        for (; i < stop; i += 2) {
            WORD v = huff(b, t);
            int x = v >> 4, y = v & 15;
            if (x == 15 && linbits)
                x += get(b, linbits);
            if (x && get1(b))
                x = -x;
            if (y == 15 && linbits)
                y += get(b, linbits);
            if (y && get1(b))
                y = -y;
            is[i] = x;
            is[i + 1] = y;
        }
    }
    while (i + 4 <= 576 && b->pos < end) {
        WORD v = huff(b, 32 + g->quad);
        int q[4] = {(v >> 3) & 1, (v >> 2) & 1, (v >> 1) & 1, v & 1};
        for (int k = 0; k < 4; k++)
            if (q[k] && get1(b))
                q[k] = -1;
        if (b->pos > end)
            break;
        for (int k = 0; k < 4; k++)
            is[i + k] = q[k];
        i += 4;
    }
    int nonzero = i;
    for (; i < 576; i++)
        is[i] = 0;
    while (nonzero > 0 && !is[nonzero - 1])
        nonzero--;
    return nonzero;
}

static void requantize(const Granule *g, const Scales *sc, const Mp3Bands *bands, const int *is, int nonzero,
                       float *xr)
{
    int base = g->global_gain - 210, mul = g->scale ? 4 : 2, i = 0;
    int long_end = g->block_type != 2 ? 22 : g->mixed ? 0 : -1;
    if (g->block_type == 2 && g->mixed)
        while (bands->l[long_end + 1] <= 36 && long_end < 21)
            long_end++;
    for (int sfb = 0; sfb < (long_end < 0 ? 0 : long_end) && i < nonzero; sfb++) {
        float gain = pow2q(base - mul * (sc->l[sfb] + (g->preflag ? PRETAB[sfb] : 0)));
        for (int e = bands->l[sfb + 1]; i < e; i++) {
            int v = is[i];
            xr[i] = v < 0 ? -g_pow43[-v] * gain : g_pow43[v] * gain;
        }
    }
    if (g->block_type == 2) {
        int sfb = 0;
        while (3 * bands->s[sfb] < i)
            sfb++;
        for (; sfb < 13 && i < nonzero; sfb++) {
            int width = bands->s[sfb + 1] - bands->s[sfb];
            for (int w = 0; w < 3; w++) {
                float gain = pow2q(base - 8 * g->subgain[w] - mul * sc->s[sfb][w]);
                for (int k = 0; k < width; k++, i++) {
                    int v = is[i];
                    xr[i] = v < 0 ? -g_pow43[-v] * gain : g_pow43[v] * gain;
                }
            }
        }
    }
    for (; i < 576; i++)
        xr[i] = 0.0f;
}

/* ------------------------------------------------------------------ stereo */

static void ms_lines(float *l, float *r, int from, int to)
{
    for (int i = from; i < to; i++) {
        float m = l[i], s = r[i];
        l[i] = (m + s) * 0.70710678f;
        r[i] = (m - s) * 0.70710678f;
    }
}

static void intensity_lines(const Frame *f, float *l, float *r, int from, int to, int position, int compress)
{
    float kl, kr;
    if (!f->lsf) {
        kl = MP3_INTENSITY[2 * position];
        kr = MP3_INTENSITY[2 * position + 1];
    } else {
        float i0 = compress & 1 ? 0.70710678f : 0.84089642f, k = 1.0f;
        for (int n = 0; n < (position + 1) / 2; n++)
            k *= i0;
        kl = position & 1 ? k : 1.0f;
        kr = position & 1 || !position ? 1.0f : k;
    }
    for (int i = from; i < to; i++) {
        float v = l[i];
        l[i] = v * kl;
        r[i] = v * kr;
    }
}

/* joint stereo of a granule: intensity above the right channel's last nonzero band, mid and side below. */
static void stereo(const Frame *f, const Granule *g, const Scales *sc, const Mp3Bands *bands, float *l, float *r,
                   int right_nonzero)
{
    BOOL ms = f->mode_ext & 2;
    if (!(f->mode_ext & 1)) {
        if (ms)
            ms_lines(l, r, 0, 576);
        return;
    }
    if (g->block_type != 2) {
        int first = 0;
        while (first < 22 && bands->l[first] < right_nonzero)
            first++;
        if (ms)
            ms_lines(l, r, 0, bands->l[first]);
        for (int sfb = first; sfb < 22; sfb++) {
            int position = sc->l[sfb < 21 ? sfb : 20], from = bands->l[sfb], to = bands->l[sfb + 1];
            if (position >= sc->illegal_l[sfb]) {
                if (ms)
                    ms_lines(l, r, from, to);
            } else {
                intensity_lines(f, l, r, from, to, position, g->compress);
            }
        }
        return;
    }
    /* short blocks find the bound of each window; the long part of mixed blocks is never intensity coded. */
    int start = 0;
    if (g->mixed)
        while (3 * bands->s[start] < 36)
            start++;
    if (ms)
        ms_lines(l, r, 0, 3 * bands->s[start]);
    for (int w = 0; w < 3; w++) {
        int last = -1;
        for (int sfb = start; sfb < 13; sfb++) {
            int width = bands->s[sfb + 1] - bands->s[sfb], from = 3 * bands->s[sfb] + w * width;
            for (int k = 0; k < width; k++)
                if (r[from + k] != 0.0f)
                    last = sfb;
        }
        for (int sfb = start; sfb < 13; sfb++) {
            int width = bands->s[sfb + 1] - bands->s[sfb], from = 3 * bands->s[sfb] + w * width;
            int position = sc->s[sfb < 12 ? sfb : 11][w];
            if (sfb <= last || position >= sc->illegal_s[sfb]) {
                if (ms)
                    ms_lines(l, r, from, from + width);
            } else {
                intensity_lines(f, l, r, from, from + width, position, g->compress);
            }
        }
    }
}

/* ------------------------------------------------------------------ filterbank */

/* alias reduction, the imdct and overlap of each subband, giving 18 samples of each of the subbands kept. */
static void hybrid(Mp3Channel *c, const Granule *g, const Mp3Bands *bands, float *xr, int limit, float out[18][32])
{
    BOOL is_short = g->block_type == 2;
    int long_bands = is_short ? (g->mixed ? 2 : 0) : 32;
    for (int sb = 1; sb < (long_bands < limit ? long_bands : limit); sb++)
        for (int i = 0; i < 8; i++) {
            float *u = &xr[18 * sb - 1 - i], *d = &xr[18 * sb + i], a = *u, b = *d;
            *u = a * MP3_ALIAS[i] - b * MP3_ALIAS[8 + i];
            *d = b * MP3_ALIAS[i] + a * MP3_ALIAS[8 + i];
        }
    float reordered[576];
    if (is_short) {
        int sfb = 0, i = 0;
        if (g->mixed) {
            while (3 * bands->s[sfb] < 36)
                sfb++;
            i = 3 * bands->s[sfb];
        }
        for (; sfb < 13; sfb++) {
            int width = bands->s[sfb + 1] - bands->s[sfb];
            for (int w = 0; w < 3; w++)
                for (int k = 0; k < width; k++, i++) {
                    int f = bands->s[sfb] + k;
                    reordered[18 * (f / 6) + 6 * w + f % 6] = xr[i];
                }
        }
    }
    for (int sb = 0; sb < 32; sb++) {
        float *overlap = &c->overlap[18 * sb], y[36];
        if (sb >= limit) {
            for (int t = 0; t < 18; t++)
                out[t][sb] = 0.0f;
            memset(overlap, 0, 18 * sizeof(float));
            continue;
        }
        if (sb >= long_bands) {
            memset(y, 0, sizeof(y));
            const float *in = &reordered[18 * sb];
            for (int w = 0; w < 3; w++)
                for (int i = 0; i < 12; i++) {
                    float sum = 0.0f;
                    for (int k = 0; k < 6; k++)
                        sum += in[6 * w + k] * MP3_IMDCT12[12 * k + i];
                    y[6 + 6 * w + i] += sum * MP3_WINDOWS[2 * 36 + i];
                }
        } else {
            const float *in = &xr[18 * sb], *win = &MP3_WINDOWS[36 * (is_short ? 0 : g->block_type)];
            BOOL zero = TRUE;
            for (int k = 0; k < 18 && zero; k++)
                zero = in[k] == 0.0f;
            if (zero) {
                memset(y, 0, sizeof(y));
            } else {
                for (int i = 0; i < 36; i++)
                    y[i] = 0.0f;
                for (int k = 0; k < 18; k++) {
                    float v = in[k];
                    if (v == 0.0f)
                        continue;
                    const float *cs = &MP3_IMDCT36[36 * k];
                    for (int i = 0; i < 36; i++)
                        y[i] += v * cs[i];
                }
                for (int i = 0; i < 36; i++)
                    y[i] *= win[i];
            }
        }
        for (int t = 0; t < 18; t++) {
            float v = y[t] + overlap[t];
            out[t][sb] = (sb & t & 1) ? -v : v;
            overlap[t] = y[18 + t];
        }
    }
}

/* the polyphase synthesis of one slot of 32 subbands into 32 samples. */
static void synthesize(float *v, DWORD offset, const float *s, int limit, float *pcm)
{
    float c[33];
    for (int a = 0; a < 32; a++) {
        const float *row = &MP3_SYNTH[32 * a];
        float sum = 0.0f;
        for (int k = 0; k < limit; k++)
            sum += s[k] * row[k];
        c[a] = sum;
    }
    c[32] = 0.0f;
    for (int i = 0; i < 64; i++) {
        int m = 16 + i;
        float x = m <= 32 ? c[m] : m <= 64 ? -c[64 - m] : -c[m - 64];
        v[(offset + i) & 1023] = x;
    }
    for (int j = 0; j < 32; j++) {
        float sum = 0.0f;
        for (int i = 0; i < 8; i++)
            sum += v[(offset + 128 * i + j) & 1023] * g_window[64 * i + j] +
                   v[(offset + 128 * i + 96 + j) & 1023] * g_window[64 * i + 32 + j];
        pcm[j] = sum;
    }
}

static BYTE *put(Mp3Decoder *d, BYTE *out, float x)
{
    x *= 32768.0f;
    x = x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x;
    SHORT s = (SHORT)(int)x;
    if (d->eight) {
        *out++ = (BYTE)(((WORD)s >> 8) + 0x80);
    } else {
        memcpy(out, &s, 2);
        out += 2;
    }
    return out;
}

/* ------------------------------------------------------------------ frames */

static void restart(Mp3Decoder *d)
{
    d->reserved = 0;
    d->v_offset = 0;
    memset(d->ch, 0, sizeof(d->ch));
}

void Mp3Init(Mp3Decoder *d, int shift, BOOL eight, BOOL downmix)
{
    memset(d, 0, sizeof(*d));
    d->shift = (BYTE)shift;
    d->eight = (BYTE)eight;
    d->downmix = (BYTE)downmix;
    d->noise = 0x12345678;
}

void Mp3Reset(Mp3Decoder *d)
{
    d->start = d->end = d->bit = d->lock = d->checks = 0;
    d->noise = 0x12345678;
    restart(d);
}

DWORD Mp3Space(const Mp3Decoder *d)
{
    return MP3_RING - (d->end - d->start);
}

DWORD Mp3Feed(Mp3Decoder *d, const BYTE *data, DWORD size)
{
    if (d->start) {
        memmove(d->ring, d->ring + d->start, d->end - d->start);
        d->end -= d->start;
        d->start = 0;
    }
    DWORD n = MP3_RING - d->end < size ? MP3_RING - d->end : size;
    memcpy(d->ring + d->end, data, n);
    d->end += n;
    return n;
}

/* the frame's granules decoded to the subband samples of each output channel. */
static void decode_frame(Mp3Decoder *d, const Frame *f, const BYTE *frame, BYTE *out)
{
    Bits b = {frame, 32 + (f->crc ? 16 : 0)};
    Side side = {};
    int side_bytes = f->lsf ? (f->channels == 1 ? 9 : 17) : (f->channels == 1 ? 17 : 32);
    int header_bytes = 4 + (f->crc ? 2 : 0), main_bytes = (int)f->length - header_bytes - side_bytes;
    BOOL usable = main_bytes >= 0 && read_side(&b, f, &side);
    if (main_bytes < 0)
        main_bytes = 0;
    const BYTE *main_data = frame + header_bytes + side_bytes;
    BOOL conceal = !usable || side.begin > d->reserved;
    BYTE *data = d->reservoir;
    DWORD data_start = 0;
    if (!usable) {
        d->reserved = 0;
    } else {
        DWORD keep = d->reserved < 511 ? d->reserved : 511;
        memmove(d->reservoir, d->reservoir + d->reserved - keep, keep);
        d->reserved = keep;
        data_start = d->reserved - (conceal ? 0 : side.begin);
        memcpy(d->reservoir + d->reserved, main_data, main_bytes);
        d->reserved += main_bytes;
        memset(d->reservoir + d->reserved, 0, 8);
    }
    const Mp3Bands *bands = &MP3_BANDS[f->sri];
    int nch = f->channels, outch = d->downmix && nch == 2 ? 1 : nch, limit = 32 >> d->shift, step = 1 << d->shift;
    DWORD position = data_start * 8;
    Scales scales[2] = {};
    for (int gr = 0; gr < f->granules; gr++) {
        float xr[2][576];
        int nonzero[2] = {};
        for (int ch = 0; ch < nch; ch++) {
            Granule *g = &side.g[gr][ch];
            if (conceal) {
                for (int i = 0; i < 576; i++) {
                    d->noise = d->noise * 1664525 + 1013904223;
                    float v = d->ch[ch].last[gr][i] * 0.5f;
                    xr[ch][i] = d->noise & 0x80000000 ? -v : v;
                }
                memset(g, 0, sizeof(*g));
                nonzero[ch] = 576;
                continue;
            }
            Bits mb = {data, position};
            DWORD end = position + g->part23;
            if (f->lsf)
                read_scales_lsf(&mb, f, g, ch, &scales[ch]);
            else
                read_scales_mpeg1(&mb, g, gr, g->block_type == 2 ? 0 : side.scfsi[ch], &scales[ch]);
            int is[576];
            nonzero[ch] = read_values(&mb, end, g, bands, is);
            requantize(g, &scales[ch], bands, is, nonzero[ch], xr[ch]);
            position = end;
        }
        if (!conceal && f->mode == 1 && nch == 2)
            stereo(f, &side.g[gr][1], &scales[1], bands, xr[0], xr[1], nonzero[1]);
        float sub[2][18][32];
        for (int ch = 0; ch < nch; ch++) {
            memcpy(d->ch[ch].last[gr], xr[ch], sizeof(xr[ch]));
            if (f->granules == 1)
                memcpy(d->ch[ch].last[1], xr[ch], sizeof(xr[ch]));
            hybrid(&d->ch[ch], &side.g[gr][ch], bands, xr[ch], limit, sub[ch]);
        }
        if (outch != nch)
            for (int t = 0; t < 18; t++)
                for (int sb = 0; sb < limit; sb++)
                    sub[0][t][sb] = (sub[0][t][sb] + sub[1][t][sb]) * 0.5f;
        float pcm[2][18][32];
        DWORD offset = d->v_offset;
        for (int ch = 0; ch < outch; ch++) {
            offset = d->v_offset;
            for (int t = 0; t < 18; t++) {
                offset = (offset - 64) & 1023;
                synthesize(d->ch[ch].v, offset, sub[ch][t], limit, pcm[ch][t]);
            }
        }
        d->v_offset = offset;
        for (int t = 0; t < 18; t++)
            for (int j = 0; j < 32; j += step)
                for (int ch = 0; ch < outch; ch++)
                    out = put(d, out, pcm[ch][t][j]);
    }
}

/*
 * the next frame of the ring into out. before a lock the stream is
 * searched bit by bit for a header whose successor agrees with it, so a
 * stream of one frame never decodes.
 */
int Mp3Decode(Mp3Decoder *d, BYTE *out, DWORD room, DWORD *written)
{
    *written = 0;
    DWORD position = d->start * 8 + d->bit, end = d->end * 8;
    Frame f;
    if (!d->lock) {
        for (;; position++) {
            if (position + 32 > end) {
                d->start = position >> 3;
                d->bit = position & 7;
                return MP3_SEARCHING;
            }
            DWORD h = peek32(d, position);
            if ((h >> 21) != 0x7FF || !parse_header(h, &f))
                continue;
            if (position + (f.length + 4) * 8 > end) {
                d->start = position >> 3;
                d->bit = position & 7;
                return MP3_NEED_DATA;
            }
            if ((peek32(d, position + f.length * 8) & MASK) == (h & MASK)) {
                d->lock = h & MASK;
                d->checks = 10;
                break;
            }
        }
        d->start = position >> 3;
        d->bit = position & 7;
    }
    if (position + 32 > end)
        return MP3_NEED_DATA;
    DWORD h = peek32(d, position);
    if (!parse_header(h, &f) || (h & MASK) != d->lock) {
        d->lock = 0;
        restart(d);
        return MP3_SYNC_LOST;
    }
    if (position + f.length * 8 > end)
        return MP3_NEED_DATA;
    if (d->checks) {
        if (position + (f.length + 4) * 8 <= end && (peek32(d, position + f.length * 8) & MASK) != d->lock) {
            d->lock = 0;
            restart(d);
            return MP3_SYNC_LOST;
        }
        d->checks--;
    }
    int outch = d->downmix && f.channels == 2 ? 1 : f.channels;
    DWORD size = ((DWORD)576 * f.granules * outch * 2) >> (d->shift + d->eight);
    if (size > room)
        return MP3_FATAL;
    BYTE frame[1448];
    for (DWORD i = 0; i < f.length; i++)
        frame[i] = d->bit ? (BYTE)(peek32(d, position + 8 * i) >> 24) : d->ring[d->start + i];
    memset(frame + f.length, 0, 8);
    position += f.length * 8;
    d->start = position >> 3;
    d->bit = position & 7;
    decode_frame(d, &f, frame, out);
    *written = size;
    return MP3_OK;
}
