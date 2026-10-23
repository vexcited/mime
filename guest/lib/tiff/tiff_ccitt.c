#include "tiff_internal.h"

/*
 * ccitt t.4 and t.6 run length codes as bit strings. setup expands them into lookup tables for the decoder and
 * code, length pairs for the encoder.
 */
static const char *const white_term[64] = {
    "00110101", "000111",   "0111",     "1000",     "1011",     "1100",     "1110",     "1111",
    "10011",    "10100",    "00111",    "01000",    "001000",   "000011",   "110100",   "110101",
    "101010",   "101011",   "0100111",  "0001100",  "0001000",  "0010111",  "0000011",  "0000100",
    "0101000",  "0101011",  "0010011",  "0100100",  "0011000",  "00000010", "00000011", "00011010",
    "00011011", "00010010", "00010011", "00010100", "00010101", "00010110", "00010111", "00101000",
    "00101001", "00101010", "00101011", "00101100", "00101101", "00000100", "00000101", "00001010",
    "00001011", "01010010", "01010011", "01010100", "01010101", "00100100", "00100101", "01011000",
    "01011001", "01011010", "01011011", "01001010", "01001011", "00110010", "00110011", "00110100",
};

static const char *const white_make[27] = {
    "11011",     "10010",     "010111",    "0110111",   "00110110",  "00110111",  "01100100",
    "01100101",  "01101000",  "01100111",  "011001100", "011001101", "011010010", "011010011",
    "011010100", "011010101", "011010110", "011010111", "011011000", "011011001", "011011010",
    "011011011", "010011000", "010011001", "010011010", "011000",    "010011011",
};

static const char *const black_term[64] = {
    "0000110111",   "010",          "11",           "10",           "011",          "0011",
    "0010",         "00011",        "000101",       "000100",       "0000100",      "0000101",
    "0000111",      "00000100",     "00000111",     "000011000",    "0000010111",   "0000011000",
    "0000001000",   "00001100111",  "00001101000",  "00001101100",  "00000110111",  "00000101000",
    "00000010111",  "00000011000",  "000011001010", "000011001011", "000011001100", "000011001101",
    "000001101000", "000001101001", "000001101010", "000001101011", "000011010010", "000011010011",
    "000011010100", "000011010101", "000011010110", "000011010111", "000001101100", "000001101101",
    "000011011010", "000011011011", "000001010100", "000001010101", "000001010110", "000001010111",
    "000001100100", "000001100101", "000001010010", "000001010011", "000000100100", "000000110111",
    "000000111000", "000000100111", "000000101000", "000001011000", "000001011001", "000000101011",
    "000000101100", "000001011010", "000001100110", "000001100111",
};

static const char *const black_make[27] = {
    "0000001111",    "000011001000",  "000011001001",  "000001011011",  "000000110011",  "000000110100",
    "000000110101",  "0000001101100", "0000001101101", "0000001001010", "0000001001011", "0000001001100",
    "0000001001101", "0000001110010", "0000001110011", "0000001110100", "0000001110101", "0000001110110",
    "0000001110111", "0000001010010", "0000001010011", "0000001010100", "0000001010101", "0000001011010",
    "0000001011011", "0000001100100", "0000001100101",
};

/* makeup codes of 1792 to 2560, shared by both colors. */
static const char *const ext_make[13] = {
    "00000001000",  "00000001100",  "00000001101",  "000000010010", "000000010011",
    "000000010100", "000000010101", "000000010110", "000000010111", "000000011100",
    "000000011101", "000000011110", "000000011111",
};

enum { MODE_P = 1, MODE_H, MODE_V0, MODE_VR1, MODE_VR2, MODE_VR3, MODE_VL1, MODE_VL2, MODE_VL3, MODE_EXT };

typedef struct ModeCode {
    const char *bits;
    unsigned mode;
} ModeCode;

static const ModeCode mode_codes[] = {
    {"0001", MODE_P},      {"001", MODE_H},        {"1", MODE_V0},         {"011", MODE_VR1},
    {"000011", MODE_VR2},  {"0000011", MODE_VR3},  {"010", MODE_VL1},      {"000010", MODE_VL2},
    {"0000010", MODE_VL3}, {"0000001", MODE_EXT},
};

#define MAX_WIDTH (1u << 20)

struct TiffCcitt {
    uint16_t white[1 << 12]; /* length << 12 | run, indexed by the next 12 bits. */
    uint16_t black[1 << 13];
    uint8_t mode[128]; /* length << 4 | mode, indexed by the next 7 bits. */
    uint32_t enc_term[2][64];
    uint32_t enc_make[2][27];
    uint32_t enc_ext[13];
    uint32_t width;
    uint32_t *ref; /* changing elements of the reference line, followed by width sentinels. */
    uint32_t *cur;
    uint32_t ref_n;
    uint32_t cur_n;
};

#define WORK_SLOTS(w) ((size_t)(w) + 16)

size_t tiff_ccitt_state_size(uint32_t width)
{
    return sizeof(TiffCcitt) + 2 * WORK_SLOTS(width) * sizeof(uint32_t);
}

static unsigned parse_bits(const char *s, unsigned *len)
{
    unsigned v = 0, n = 0;
    for (; *s; s++, n++)
        v = v << 1 | (unsigned)(*s - '0');
    *len = n;
    return v;
}

static void table_fill(uint16_t *tab, unsigned width_bits, const char *s, unsigned run)
{
    unsigned len;
    unsigned code = parse_bits(s, &len);
    unsigned first = code << (width_bits - len);
    unsigned count = 1u << (width_bits - len);
    for (unsigned i = 0; i < count; i++)
        tab[first + i] = (uint16_t)(len << 12 | run);
}

bool tiff_ccitt_init(TiffCcitt *st, uint32_t width, bool decode)
{
    if (width == 0 || width > MAX_WIDTH)
        return false;
    st->width = width;
    st->ref = (uint32_t *)(st + 1);
    st->cur = st->ref + WORK_SLOTS(width);
    if (decode) {
        memset(st->white, 0, sizeof st->white);
        memset(st->black, 0, sizeof st->black);
        memset(st->mode, 0, sizeof st->mode);
        for (unsigned i = 0; i < 64; i++) {
            table_fill(st->white, 12, white_term[i], i);
            table_fill(st->black, 13, black_term[i], i);
        }
        for (unsigned i = 0; i < 27; i++) {
            table_fill(st->white, 12, white_make[i], (i + 1) * 64);
            table_fill(st->black, 13, black_make[i], (i + 1) * 64);
        }
        for (unsigned i = 0; i < 13; i++) {
            table_fill(st->white, 12, ext_make[i], 1792 + i * 64);
            table_fill(st->black, 13, ext_make[i], 1792 + i * 64);
        }
        for (unsigned i = 0; i < sizeof mode_codes / sizeof mode_codes[0]; i++) {
            unsigned len;
            unsigned code = parse_bits(mode_codes[i].bits, &len);
            unsigned first = code << (7 - len);
            for (unsigned k = 0; k < 1u << (7 - len); k++)
                st->mode[first + k] = (uint8_t)(len << 4 | mode_codes[i].mode);
        }
    } else {
        for (unsigned i = 0; i < 64; i++) {
            unsigned l;
            unsigned c = parse_bits(white_term[i], &l);
            st->enc_term[0][i] = l << 16 | c;
            c = parse_bits(black_term[i], &l);
            st->enc_term[1][i] = l << 16 | c;
        }
        for (unsigned i = 0; i < 27; i++) {
            unsigned l;
            unsigned c = parse_bits(white_make[i], &l);
            st->enc_make[0][i] = l << 16 | c;
            c = parse_bits(black_make[i], &l);
            st->enc_make[1][i] = l << 16 | c;
        }
        for (unsigned i = 0; i < 13; i++) {
            unsigned l;
            unsigned c = parse_bits(ext_make[i], &l);
            st->enc_ext[i] = l << 16 | c;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- decoding. */

typedef struct Br {
    const uint8_t *p, *end;
    uint64_t acc;
    unsigned cnt;
    unsigned pad; /* bits of cnt that are zeros appended after the end of the data. */
} Br;

static inline void br_fill(Br *b)
{
    if (b->cnt >= 32)
        return;
    if (b->end - b->p >= 4) {
        uint32_t w;
        memcpy(&w, b->p, 4);
        b->acc = (b->acc << 32) | __builtin_bswap32(w);
        b->p += 4;
        b->cnt += 32;
        return;
    }
    while (b->cnt < 32) {
        uint64_t v = 0;
        if (b->p < b->end)
            v = *b->p++;
        else
            b->pad += 8;
        b->acc = (b->acc << 8) | v;
        b->cnt += 8;
    }
}

static inline unsigned br_peek(const Br *b, unsigned n)
{
    return (unsigned)(b->acc >> (b->cnt - n)) & ((1u << n) - 1);
}

static inline void br_skip(Br *b, unsigned n)
{
    b->cnt -= n;
}

/* true when every real bit has been consumed. */
static inline bool br_empty(const Br *b)
{
    return b->cnt <= b->pad;
}

enum { ST_OK = 0, ST_ERR = -1, ST_END = -2 };

static int decode_run(const TiffCcitt *st, Br *b, unsigned color)
{
    unsigned total = 0;
    for (;;) {
        br_fill(b);
        unsigned e = color ? st->black[br_peek(b, 13)] : st->white[br_peek(b, 12)];
        if (!e)
            return ST_ERR;
        br_skip(b, e >> 12);
        unsigned r = e & 0xfff;
        total += r;
        if (r < 64)
            return (int)total;
        if (total > MAX_WIDTH)
            return ST_ERR;
        if (br_empty(b))
            return ST_ERR;
    }
}

static int row_1d(TiffCcitt *st, Br *b)
{
    uint32_t width = st->width;
    uint32_t *cur = st->cur;
    uint32_t n = 0, a0 = 0;
    unsigned color = 0;
    while (a0 < width) {
        int r = decode_run(st, b, color);
        if (r < 0)
            goto fail;
        if ((uint32_t)r > width - a0)
            goto fail;
        a0 += (uint32_t)r;
        if (n >= width + 8)
            goto fail;
        cur[n++] = a0;
        color ^= 1;
    }
    cur[n] = cur[n + 1] = cur[n + 2] = width;
    st->cur_n = n;
    return ST_OK;
fail:
    cur[n] = cur[n + 1] = cur[n + 2] = width;
    st->cur_n = n;
    return ST_ERR;
}

static int row_2d(TiffCcitt *st, Br *b)
{
    int32_t width = (int32_t)st->width;
    const uint32_t *ref = st->ref;
    uint32_t *cur = st->cur;
    uint32_t n = 0, j = 0;
    int32_t a0 = -1;
    unsigned color = 0;
    int rc = ST_OK;
    while (a0 < width) {
        br_fill(b);
        if (br_empty(b)) {
            rc = ST_END;
            break;
        }
        unsigned e = st->mode[br_peek(b, 7)];
        if (!e) {
            rc = br_peek(b, 12) == 1 ? ST_END : ST_ERR;
            break;
        }
        br_skip(b, e >> 4);
        unsigned mode = e & 15;
        if (mode == MODE_EXT) {
            rc = ST_ERR;
            break;
        }
        while (j > 0 && (int32_t)ref[j - 1] > a0)
            j--;
        while ((int32_t)ref[j] <= a0 && (int32_t)ref[j] < width)
            j++;
        if ((j & 1) != color)
            j++;
        int32_t b1 = (int32_t)ref[j];
        if (mode == MODE_P) {
            a0 = (int32_t)ref[j + 1];
            continue;
        }
        int32_t start = a0 < 0 ? 0 : a0;
        if (n >= (uint32_t)width + 8) {
            rc = ST_ERR;
            break;
        }
        if (mode == MODE_H) {
            int r1 = decode_run(st, b, color);
            int r2 = r1 < 0 ? r1 : decode_run(st, b, color ^ 1);
            if (r1 < 0 || r2 < 0 || r1 > width - start || r2 > width - start - r1) {
                rc = ST_ERR;
                break;
            }
            cur[n++] = (uint32_t)(start + r1);
            cur[n++] = (uint32_t)(start + r1 + r2);
            a0 = start + r1 + r2;
            continue;
        }
        int32_t a1 = b1;
        switch (mode) {
        case MODE_VR1: a1 += 1; break;
        case MODE_VR2: a1 += 2; break;
        case MODE_VR3: a1 += 3; break;
        case MODE_VL1: a1 -= 1; break;
        case MODE_VL2: a1 -= 2; break;
        case MODE_VL3: a1 -= 3; break;
        default: break;
        }
        if (a1 < start || a1 > width) {
            rc = ST_ERR;
            break;
        }
        cur[n++] = (uint32_t)a1;
        a0 = a1;
        color ^= 1;
    }
    cur[n] = cur[n + 1] = cur[n + 2] = (uint32_t)width;
    st->cur_n = n;
    return rc;
}

static void set_bits(uint8_t *row, uint32_t a, uint32_t b)
{
    if (a >= b)
        return;
    uint32_t ab = a >> 3, bb = b >> 3;
    unsigned am = 0xffu >> (a & 7);
    unsigned bm = (0xff00u >> (b & 7)) & 0xffu;
    if (ab == bb) {
        row[ab] |= (uint8_t)(am & bm);
        return;
    }
    row[ab] |= (uint8_t)am;
    if (bb > ab + 1)
        memset(row + ab + 1, 0xff, bb - ab - 1);
    if (b & 7)
        row[bb] |= (uint8_t)bm;
}

/* paints the black runs of the changes in cur into a zeroed row, then makes the row the reference. */
static void commit_row(TiffCcitt *st, uint8_t *row)
{
    uint32_t n = st->cur_n;
    uint32_t width = st->width;
    for (uint32_t k = 0; k < n; k += 2) {
        uint32_t a = st->cur[k];
        uint32_t e = k + 1 < n ? st->cur[k + 1] : width;
        if (e > width)
            e = width;
        set_bits(row, a, e);
    }
    uint32_t *t = st->ref;
    st->ref = st->cur;
    st->cur = t;
    st->ref_n = st->cur_n;
}

enum { EOL_NONE, EOL_FOUND, EOL_END };

/* skips fill bits and an end of line code if one is next. tagged two dimensional data has a tag bit after the eol. */
static int skip_eol(Br *b, bool tagged, bool *is_1d)
{
    br_fill(b);
    if (br_empty(b))
        return EOL_END;
    if (br_peek(b, 11) != 0)
        return EOL_NONE;
    for (;;) {
        br_fill(b);
        if (br_empty(b))
            return EOL_END;
        if (br_peek(b, 1)) {
            br_skip(b, 1);
            break;
        }
        br_skip(b, 1);
    }
    *is_1d = true;
    if (tagged) {
        br_fill(b);
        *is_1d = br_peek(b, 1) != 0;
        br_skip(b, 1);
    }
    br_fill(b);
    if (br_empty(b) || br_peek(b, 11) == 0)
        return EOL_END;
    return EOL_FOUND;
}

/* moves to the next end of line code without consuming it. */
static bool resync(Br *b)
{
    for (;;) {
        br_fill(b);
        if (br_empty(b))
            return false;
        if (br_peek(b, 12) == 1)
            return true;
        br_skip(b, 1);
    }
}

uint32_t tiff_ccitt_decode(TiffCcitt *st, unsigned compression, unsigned t4_options, const uint8_t *src, size_t n,
                           uint8_t *dst, size_t row_bytes, uint32_t rows, bool *error)
{
    Br b = {.p = src, .end = src + n};
    uint32_t width = st->width;
    *error = false;
    memset(dst, 0, row_bytes * rows);
    st->ref[0] = st->ref[1] = st->ref[2] = width;
    st->ref_n = 0;
    bool two_dim = (t4_options & 1) != 0;
    uint32_t done = 0;
    for (; done < rows; done++) {
        uint8_t *row = dst + (size_t)done * row_bytes;
        bool one_d = compression != MIME_TIFF_COMPRESSION_CCITT_G4;
        if (compression == MIME_TIFF_COMPRESSION_CCITT_G3) {
            bool tag = true;
            int k = skip_eol(&b, two_dim, &tag);
            if (k == EOL_END)
                break;
            if (k == EOL_FOUND)
                one_d = tag;
        } else if (compression == MIME_TIFF_COMPRESSION_CCITT_RLE) {
            bool tag;
            br_fill(&b);
            if (br_empty(&b))
                break;
            if (br_peek(&b, 11) == 0 && skip_eol(&b, false, &tag) == EOL_END)
                break;
        } else {
            br_fill(&b);
            if (br_empty(&b))
                break;
        }
        int rc = one_d ? row_1d(st, &b) : row_2d(st, &b);
        if (rc == ST_END && compression == MIME_TIFF_COMPRESSION_CCITT_G4)
            break;
        if (rc != ST_OK) {
            *error = true;
            if (compression != MIME_TIFF_COMPRESSION_CCITT_G3) {
                commit_row(st, row);
                done++;
                break;
            }
            commit_row(st, row);
            if (!resync(&b)) {
                done++;
                break;
            }
            continue;
        }
        commit_row(st, row);
        if (compression == MIME_TIFF_COMPRESSION_CCITT_RLE)
        {
            br_fill(&b);
            br_skip(&b, b.cnt & 7);
        }
    }
    if (done < rows)
        *error = true;
    return done;
}

/* ---------------------------------------------------------------- encoding. */

typedef struct BitW {
    uint8_t *o;
    uint64_t acc;
    unsigned cnt;
} BitW;

static inline void put_bits(BitW *w, unsigned code, unsigned len)
{
    w->acc = (w->acc << len) | code;
    w->cnt += len;
    while (w->cnt >= 8) {
        *w->o++ = (uint8_t)(w->acc >> (w->cnt - 8));
        w->cnt -= 8;
    }
}

static void put_run(const TiffCcitt *st, BitW *w, unsigned color, uint32_t run)
{
    while (run >= 2560) {
        put_bits(w, st->enc_ext[12] & 0xffff, st->enc_ext[12] >> 16);
        run -= 2560;
    }
    if (run >= 1792) {
        uint32_t e = st->enc_ext[(run - 1792) / 64];
        put_bits(w, e & 0xffff, e >> 16);
        run &= 63;
    } else if (run >= 64) {
        uint32_t e = st->enc_make[color][run / 64 - 1];
        put_bits(w, e & 0xffff, e >> 16);
        run &= 63;
    }
    uint32_t e = st->enc_term[color][run];
    put_bits(w, e & 0xffff, e >> 16);
}

/* the first position at or after pos holding a pixel equal to want, or width. inv flips the sense of the data. */
static uint32_t next_pixel(const uint8_t *row, uint32_t pos, uint32_t width, unsigned want, unsigned inv)
{
    if (pos >= width)
        return width;
    unsigned flip = (inv ? 0xffu : 0u) ^ (want ? 0u : 0xffu);
    unsigned skip = flip;
    size_t i = pos >> 3;
    size_t nbytes = ((size_t)width + 7) >> 3;
    unsigned x = (row[i] ^ flip) & (0xffu >> (pos & 7));
    if (!x) {
        i++;
        uint64_t pat = skip * 0x0101010101010101ull;
        while (i + 8 <= nbytes) {
            uint64_t v;
            memcpy(&v, row + i, 8);
            if (v != pat)
                break;
            i += 8;
        }
        while (i < nbytes && row[i] == skip)
            i++;
        if (i >= nbytes)
            return width;
        x = row[i] ^ flip;
    }
    uint32_t r = (uint32_t)(i << 3) + (uint32_t)__builtin_clz(x << 24);
    return r < width ? r : width;
}

static uint32_t find_changes(const uint8_t *row, uint32_t width, unsigned inv, uint32_t *cur)
{
    uint32_t n = 0, pos = 0;
    unsigned want = 1;
    for (;;) {
        pos = next_pixel(row, pos, width, want, inv);
        if (pos >= width)
            break;
        cur[n++] = pos;
        want ^= 1;
    }
    cur[n] = cur[n + 1] = cur[n + 2] = width;
    return n;
}

static void row_1d_out(const TiffCcitt *st, BitW *w, const uint32_t *cur, uint32_t n, uint32_t width)
{
    uint32_t pos = 0;
    unsigned color = 0;
    for (uint32_t k = 0;; k++) {
        uint32_t end = k < n ? cur[k] : width;
        put_run(st, w, color, end - pos);
        pos = end;
        color ^= 1;
        if (pos >= width)
            break;
    }
}

static void row_2d_out(const TiffCcitt *st, BitW *w, const uint32_t *ref, const uint32_t *cur, uint32_t width)
{
    int32_t wd = (int32_t)width;
    int32_t a0 = -1;
    unsigned color = 0;
    uint32_t ci = 0, j = 0;
    while (a0 < wd) {
        int32_t a1 = (int32_t)cur[ci];
        while (j > 0 && (int32_t)ref[j - 1] > a0)
            j--;
        while ((int32_t)ref[j] <= a0 && (int32_t)ref[j] < wd)
            j++;
        if ((j & 1) != color)
            j++;
        int32_t b1 = (int32_t)ref[j], b2 = (int32_t)ref[j + 1];
        if (b2 < a1) {
            put_bits(w, 1, 4);
            a0 = b2;
            continue;
        }
        int32_t d = a1 - b1;
        if (d >= -3 && d <= 3) {
            switch (d) {
            case 0: put_bits(w, 1, 1); break;
            case 1: put_bits(w, 3, 3); break;
            case 2: put_bits(w, 3, 6); break;
            case 3: put_bits(w, 3, 7); break;
            case -1: put_bits(w, 2, 3); break;
            case -2: put_bits(w, 2, 6); break;
            default: put_bits(w, 2, 7); break;
            }
            a0 = a1;
            ci++;
            color ^= 1;
        } else {
            int32_t a2 = (int32_t)cur[ci + 1];
            int32_t start = a0 < 0 ? 0 : a0;
            put_bits(w, 1, 3);
            put_run(st, w, color, (uint32_t)(a1 - start));
            put_run(st, w, color ^ 1, (uint32_t)(a2 - a1));
            a0 = a2;
            ci += 2;
        }
    }
}

static bool sink_reserve(TiffByteSink *s, size_t extra)
{
    if (s->failed)
        return false;
    if (extra > 0x7fffffffu || *s->len > 0x7fffffffu - extra) {
        s->failed = true;
        return false;
    }
    size_t need = *s->len + extra;
    if (need <= *s->cap)
        return true;
    size_t cap = *s->cap ? *s->cap : 4096;
    while (cap < need)
        cap *= 2;
    uint8_t *p = mime_alloc(s->alloc, cap);
    if (!p) {
        s->failed = true;
        return false;
    }
    if (*s->len)
        memcpy(p, *s->buf, *s->len);
    mime_free(s->alloc, *s->buf);
    *s->buf = p;
    *s->cap = cap;
    return true;
}

void tiff_ccitt_encode(TiffCcitt *st, unsigned compression, bool two_dim, bool fill, bool invert, const uint8_t *src,
                       size_t row_bytes, uint32_t rows, TiffByteSink *out)
{
    uint32_t width = st->width;
    BitW w = {0};
    unsigned inv = invert ? 1 : 0;
    st->ref[0] = st->ref[1] = st->ref[2] = width;
    bool g4 = compression == MIME_TIFF_COMPRESSION_CCITT_G4;
    bool g3 = compression == MIME_TIFF_COMPRESSION_CCITT_G3;
    uint32_t until_1d = 0;
    for (uint32_t r = 0; r < rows; r++) {
        if (!sink_reserve(out, (size_t)width + 64))
            return;
        w.o = *out->buf + *out->len;
        bool one_d = !g4;
        if (g3) {
            if (fill)
                put_bits(&w, 0, (8 - ((w.cnt + 12) & 7)) & 7);
            put_bits(&w, 1, 12);
            if (two_dim) {
                one_d = until_1d == 0;
                until_1d = one_d ? 3 : until_1d - 1;
                put_bits(&w, one_d ? 1 : 0, 1);
            }
        }
        uint32_t n = find_changes(src + (size_t)r * row_bytes, width, inv, st->cur);
        if (one_d)
            row_1d_out(st, &w, st->cur, n, width);
        else
            row_2d_out(st, &w, st->ref, st->cur, width);
        if (compression == MIME_TIFF_COMPRESSION_CCITT_RLE && w.cnt)
            put_bits(&w, 0, 8 - w.cnt);
        *out->len = (size_t)(w.o - *out->buf);
        uint32_t *t = st->ref;
        st->ref = st->cur;
        st->cur = t;
    }
    if (!sink_reserve(out, 16))
        return;
    w.o = *out->buf + *out->len;
    if (g4) {
        put_bits(&w, 1, 12);
        put_bits(&w, 1, 12);
    }
    if (w.cnt)
        put_bits(&w, 0, 8 - w.cnt);
    *out->len = (size_t)(w.o - *out->buf);
}
