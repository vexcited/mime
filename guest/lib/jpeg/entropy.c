/*
 * the encoder's entropy coding: the bit writer, huffman tables, and sequential and progressive scans.
 *
 * tables made for an image come from the counts of a first pass over the same blocks, merged the usual huffman way
 * and kept within the 16 bit code limit. an all ones code is not allowed, so a dummy symbol past the real ones takes
 * it and is dropped.
 */
#include "enc.h"

/* ------------------------------------------------------------------ output */

static bool ensure(Enc *e, size_t n)
{
    if (e->oom)
        return false;
    if (e->cap - e->pos >= n)
        return true;
    size_t cap = e->cap ? e->cap : 8192;
    while (cap - e->pos < n) {
        if (cap > SIZE_MAX / 4) {
            e->oom = true;
            return false;
        }
        cap *= 2;
    }
    uint8_t *grown = mime_alloc(e->alloc, cap);
    if (!grown) {
        e->oom = true;
        return false;
    }
    if (e->pos)
        __builtin_memcpy(grown, e->buf, e->pos);
    mime_free(e->alloc, e->buf);
    e->buf = grown;
    e->cap = cap;
    return true;
}

void enc_put_bytes(Enc *e, const void *data, size_t n)
{
    if (!ensure(e, n))
        return;
    __builtin_memcpy(e->buf + e->pos, data, n);
    e->pos += n;
}

void enc_put_u8(Enc *e, unsigned v)
{
    if (ensure(e, 1))
        e->buf[e->pos++] = (uint8_t)v;
}

void enc_put_u16(Enc *e, unsigned v)
{
    if (ensure(e, 2)) {
        e->buf[e->pos++] = (uint8_t)(v >> 8);
        e->buf[e->pos++] = (uint8_t)v;
    }
}

/* writes out the accumulator's complete bytes, stuffing a zero after every 0xff. */
static void drain(Enc *e)
{
    if (!ensure(e, 16)) {
        e->cnt = 0;
        return;
    }
    while (e->cnt >= 8) {
        uint8_t b = (uint8_t)(e->acc >> (e->cnt - 8));
        e->cnt -= 8;
        e->buf[e->pos++] = b;
        if (b == 0xFF)
            e->buf[e->pos++] = 0;
    }
}

static inline void put_bits(Enc *e, uint32_t v, int n)
{
    if (e->counting || !n)
        return;
    e->acc = (e->acc << n) | (v & ((1u << n) - 1));
    e->cnt += n;
    if (e->cnt >= 32)
        drain(e);
}

/* ends the entropy coded segment on a byte boundary, padding with one bits. */
static void flush_bits(Enc *e)
{
    if (e->counting)
        return;
    if (e->cnt & 7)
        put_bits(e, 0x7F, 8 - (e->cnt & 7));
    drain(e);
    e->cnt = 0;
}

static inline void emit(Enc *e, HuffSlot *s, int sym)
{
    if (e->counting)
        s->freq[sym]++;
    else
        put_bits(e, s->code[sym], s->size[sym]);
}

static inline int bit_length(unsigned v)
{
    return v ? 32 - __builtin_clz(v) : 0;
}

/* ------------------------------------------------------------------ huffman tables */

void huff_slot_clear(HuffSlot *s)
{
    __builtin_memset(s->freq, 0, sizeof s->freq);
}

/* canonical codes: shortest first, each the previous plus one, shifted when the length grows. */
static void assign_codes(HuffSlot *s)
{
    int code = 0, k = 0;
    __builtin_memset(s->code, 0, sizeof s->code);
    __builtin_memset(s->size, 0, sizeof s->size);
    for (int len = 1; len <= 16; len++) {
        for (int i = 0; i < s->spec.bits[len]; i++, k++, code++) {
            s->code[s->spec.vals[k]] = (uint16_t)code;
            s->size[s->spec.vals[k]] = (uint8_t)len;
        }
        code <<= 1;
    }
}

void huff_slot_standard(HuffSlot *s, const JpegHuffSpec *spec)
{
    s->spec = *spec;
    assign_codes(s);
}

/* sorts by weight, then real count, then larger symbol first, so the reserved symbol merges first. */
static void sort_by_weight(int *idx, int m, const uint32_t *f, const uint32_t *real)
{
    for (int i = 1; i < m; i++) {
        int x = idx[i], k = i;
        while (k > 0) {
            int y = idx[k - 1];
            bool after = f[y] > f[x] || (f[y] == f[x] && (real[y] > real[x] || (real[y] == real[x] && y < x)));
            if (!after)
                break;
            idx[k] = idx[k - 1];
            k--;
        }
        idx[k] = x;
    }
}

/*
 * code lengths of at most 16 bits for the symbols that have a count. when the plain merge goes deeper, counts below
 * a floor are raised to it, doubling the floor until the codes fit. rare symbols then share a depth, and the
 * lengths still follow the order of the counts.
 */
static void code_lengths(const uint32_t *counts, uint8_t *len)
{
    uint32_t f[257];
    uint32_t floor = 0;
    for (;;) {
        int idx[257], m = 0;
        for (int s = 0; s < 257; s++) {
            f[s] = counts[s] && counts[s] < floor ? floor : counts[s];
            if (f[s])
                idx[m++] = s;
        }
        sort_by_weight(idx, m, f, counts);
        uint64_t w[514];
        int parent[514], depth[514];
        for (int i = 0; i < m; i++)
            w[i] = f[idx[i]];
        int li = 0, ni = m, nn = m;
        for (int k = 0; k < m - 1; k++) {
            int pick[2];
            for (int p = 0; p < 2; p++) {
                if (li < m && (ni >= nn || w[li] <= w[ni]))
                    pick[p] = li++;
                else
                    pick[p] = ni++;
            }
            w[nn] = w[pick[0]] + w[pick[1]];
            parent[pick[0]] = parent[pick[1]] = nn;
            nn++;
        }
        int deepest = 0;
        __builtin_memset(len, 0, 257);
        if (m == 1) {
            len[idx[0]] = 1;
            return;
        }
        depth[nn - 1] = 0;
        for (int i = nn - 2; i >= 0; i--)
            depth[i] = depth[parent[i]] + 1;
        for (int i = 0; i < m; i++) {
            len[idx[i]] = (uint8_t)depth[i];
            if (depth[i] > deepest)
                deepest = depth[i];
        }
        if (deepest <= 16)
            return;
        floor = floor ? floor * 2 : 2;
    }
}

void huff_slot_optimal(HuffSlot *s)
{
    uint32_t counts[257];
    uint8_t len[257];
    int real = 0;
    __builtin_memcpy(counts, s->freq, sizeof counts);
    for (int i = 0; i < 256; i++)
        real += counts[i] != 0;
    if (!real)
        counts[0] = 1;
    counts[256] = 1;
    code_lengths(counts, len);

    /* the reserved symbol has to end up last among the longest codes, and is left out of the table. */
    int longest = 0;
    for (int i = 0; i < 257; i++)
        if (len[i] > longest)
            longest = len[i];
    if (len[256] != longest) {
        int swap = -1;
        for (int i = 255; i >= 0; i--)
            if (len[i] == longest) {
                swap = i;
                break;
            }
        len[swap] = len[256];
        len[256] = (uint8_t)longest;
    }
    __builtin_memset(&s->spec, 0, sizeof s->spec);
    int k = 0;
    for (int l = 1; l <= 16; l++) {
        for (int i = 0; i < 256; i++) {
            if (len[i] == l) {
                s->spec.vals[k++] = (uint8_t)i;
                s->spec.bits[l]++;
            }
        }
    }
    s->spec.count = k;
    assign_codes(s);
}

void enc_write_dht(Enc *e, int cls, int id, const HuffSlot *s)
{
    enc_put_u8(e, 0xFF);
    enc_put_u8(e, M_DHT);
    enc_put_u16(e, 2 + 1 + 16 + (unsigned)s->spec.count);
    enc_put_u8(e, (unsigned)(cls << 4 | id));
    enc_put_bytes(e, s->spec.bits + 1, 16);
    enc_put_bytes(e, s->spec.vals, (size_t)s->spec.count);
}

/* ------------------------------------------------------------------ scan pieces */

static unsigned comp_id(const Enc *e, int i)
{
    return e->mode == MODE_RGB ? (unsigned)"RGB"[i] : (unsigned)(i + 1);
}

static void write_sos(Enc *e, const int *ci, int ns, int ss, int se, int ah, int al, bool ac_table)
{
    enc_put_u8(e, 0xFF);
    enc_put_u8(e, M_SOS);
    enc_put_u16(e, 6 + 2 * (unsigned)ns);
    enc_put_u8(e, (unsigned)ns);
    for (int i = 0; i < ns; i++) {
        enc_put_u8(e, comp_id(e, ci[i]));
        unsigned t = (unsigned)e->comp[ci[i]].cls;
        enc_put_u8(e, t << 4 | (ac_table ? t : 0));
    }
    enc_put_u8(e, (unsigned)ss);
    enc_put_u8(e, (unsigned)se);
    enc_put_u8(e, (unsigned)(ah << 4 | al));
}

/* after each restart interval: the marker, and the predictions start over. */
static void restart_marker(Enc *e)
{
    if (e->counting)
        return;
    flush_bits(e);
    enc_put_u8(e, 0xFF);
    enc_put_u8(e, M_RST0 + e->rst_next);
    e->rst_next = (e->rst_next + 1) & 7;
}

static inline int16_t *block_of(const Enc *e, const EncComp *c, uint32_t mrow, uint32_t bx, uint32_t by)
{
    /* a streaming encode holds the blocks of one MCU row only. */
    uint32_t row = e->buffered ? by : by - mrow * (uint32_t)c->v;
    return c->blocks + ((size_t)row * c->bw + bx) * 64;
}

static void code_dc(Enc *e, HuffSlot *dc, int *pred, int value)
{
    int diff = value - *pred;
    *pred = value;
    unsigned a = (unsigned)(diff < 0 ? -diff : diff);
    int n = bit_length(a);
    emit(e, dc, n);
    if (n)
        put_bits(e, (uint32_t)(diff < 0 ? diff - 1 : diff), n);
}

/* bit k is set when zigzag coefficient k is nonzero. */
static inline uint64_t nonzero_mask(const int16_t *zz)
{
#ifdef MIME_JPEG_SSE2
    const __m128i zero = _mm_setzero_si128();
    uint64_t zeros = 0;
    for (int i = 0; i < 4; i++) {
        __m128i a = _mm_cmpeq_epi16(_mm_loadu_si128((const __m128i *)(zz + 16 * i)), zero);
        __m128i b = _mm_cmpeq_epi16(_mm_loadu_si128((const __m128i *)(zz + 16 * i + 8)), zero);
        zeros |= (uint64_t)(unsigned)_mm_movemask_epi8(_mm_packs_epi16(a, b)) << (16 * i);
    }
    return ~zeros;
#else
    uint64_t m = 0;
    for (int k = 0; k < 64; k++)
        m |= (uint64_t)(zz[k] != 0) << k;
    return m;
#endif
}

/* counts a block's symbols for the tables, without writing it. */
static void count_block_sequential(HuffSlot *dc, HuffSlot *ac, int *pred, const int16_t *blk)
{
    int diff = blk[0] - *pred;
    *pred = blk[0];
    dc->freq[bit_length((unsigned)(diff < 0 ? -diff : diff))]++;
    uint64_t m = nonzero_mask(blk) & ~1ull;
    int prev = 0;
    while (m) {
        int k = __builtin_ctzll(m);
        int run = k - prev - 1;
        int v = blk[k];
        for (; run > 15; run -= 16)
            ac->freq[0xF0]++;
        ac->freq[run << 4 | bit_length((unsigned)(v < 0 ? -v : v))]++;
        prev = k;
        m &= m - 1;
    }
    if (prev < 63)
        ac->freq[0x00]++;
}

/*
 * writes a block. bits stay in a local register and bytes go straight to the buffer, which has room for a block's
 * worst case: 64 codes of up to 27 bits, every byte stuffed.
 */
static void write_block_sequential(Enc *e, HuffSlot *dc, HuffSlot *ac, int *pred, const int16_t *blk)
{
    if (!ensure(e, 1024))
        return;
    uint8_t *out = e->buf + e->pos;
    uint64_t acc = e->acc;
    int cnt = e->cnt;
#define PUT(bits, n)                                      \
    do {                                                  \
        acc = (acc << (n)) | (uint64_t)(bits);            \
        cnt += (n);                                       \
        if (cnt >= 32) {                                  \
            while (cnt >= 8) {                            \
                uint8_t byte = (uint8_t)(acc >> (cnt - 8)); \
                cnt -= 8;                                 \
                *out++ = byte;                            \
                if (byte == 0xFF)                         \
                    *out++ = 0;                           \
            }                                             \
        }                                                 \
    } while (0)
    int diff = blk[0] - *pred;
    *pred = blk[0];
    unsigned a = (unsigned)(diff < 0 ? -diff : diff);
    int n = bit_length(a);
    PUT(((uint64_t)dc->code[n] << n) | ((uint32_t)(diff < 0 ? diff - 1 : diff) & ((1u << n) - 1)), dc->size[n] + n);
    uint64_t m = nonzero_mask(blk) & ~1ull;
    int prev = 0;
    while (m) {
        int k = __builtin_ctzll(m);
        int run = k - prev - 1;
        int v = blk[k];
        while (run > 15) {
            PUT(ac->code[0xF0], ac->size[0xF0]);
            run -= 16;
        }
        n = bit_length((unsigned)(v < 0 ? -v : v));
        int sym = run << 4 | n;
        PUT(((uint64_t)ac->code[sym] << n) | ((uint32_t)(v < 0 ? v - 1 : v) & ((1u << n) - 1)), ac->size[sym] + n);
        prev = k;
        m &= m - 1;
    }
    if (prev < 63)
        PUT(ac->code[0x00], ac->size[0x00]);
#undef PUT
    e->pos = (size_t)(out - e->buf);
    e->acc = acc;
    e->cnt = cnt;
}

/* ------------------------------------------------------------------ sequential */

static int slots_needed(const Enc *e)
{
    int n = 1;
    for (int i = 0; i < e->ncomp; i++)
        if (e->comp[i].cls + 1 > n)
            n = e->comp[i].cls + 1;
    return n;
}

/* one scan over the blocks of components ci: all of them in MCU order, or a single one. */
static void sequential_pass(Enc *e, const int *ci, int ns)
{
    uint32_t count = 0;
    e->rst_next = 0;
    for (int i = 0; i < e->ncomp; i++)
        e->comp[i].pred = 0;
    if (ns == 1 && e->ncomp > 1) {
        EncComp *c = &e->comp[ci[0]];
        for (uint32_t by = 0; by < c->cbh; by++) {
            for (uint32_t bx = 0; bx < c->cbw; bx++, count++) {
                if (e->restart && count && count % e->restart == 0) {
                    restart_marker(e);
                    c->pred = 0;
                }
                const int16_t *blk = block_of(e, c, 0, bx, by);
                if (e->counting)
                    count_block_sequential(&e->dc[c->cls], &e->ac[c->cls], &c->pred, blk);
                else
                    write_block_sequential(e, &e->dc[c->cls], &e->ac[c->cls], &c->pred, blk);
            }
        }
        return;
    }
    for (uint32_t mrow = 0; mrow < e->mcuy; mrow++) {
        if (!e->buffered)
            enc_quantize_row(e, mrow);
        for (uint32_t mx = 0; mx < e->mcux; mx++, count++) {
            if (e->restart && count && count % e->restart == 0) {
                restart_marker(e);
                for (int i = 0; i < e->ncomp; i++)
                    e->comp[i].pred = 0;
            }
            for (int i = 0; i < ns; i++) {
                EncComp *c = &e->comp[ci[i]];
                for (int v = 0; v < c->v; v++) {
                    for (int h = 0; h < c->h; h++) {
                        const int16_t *blk = block_of(e, c, mrow, mx * (uint32_t)c->h + (uint32_t)h,
                                                      mrow * (uint32_t)c->v + (uint32_t)v);
                        if (e->counting)
                            count_block_sequential(&e->dc[c->cls], &e->ac[c->cls], &c->pred, blk);
                        else
                            write_block_sequential(e, &e->dc[c->cls], &e->ac[c->cls], &c->pred, blk);
                    }
                }
            }
        }
    }
}

static int sequential_scan(Enc *e, const int *ci, int ns)
{
    bool used[2] = {false, false};
    for (int i = 0; i < ns; i++)
        used[e->comp[ci[i]].cls] = true;
    if (e->optimize) {
        for (int k = 0; k < 2; k++) {
            if (used[k]) {
                huff_slot_clear(&e->dc[k]);
                huff_slot_clear(&e->ac[k]);
            }
        }
        e->counting = true;
        sequential_pass(e, ci, ns);
        e->counting = false;
    }
    for (int k = 0; k < 2; k++) {
        if (!used[k])
            continue;
        if (e->optimize) {
            huff_slot_optimal(&e->dc[k]);
            huff_slot_optimal(&e->ac[k]);
        } else {
            huff_slot_standard(&e->dc[k], &jpeg_std_dc[k]);
            huff_slot_standard(&e->ac[k], &jpeg_std_ac[k]);
        }
        enc_write_dht(e, 0, k, &e->dc[k]);
        enc_write_dht(e, 1, k, &e->ac[k]);
    }
    write_sos(e, ci, ns, 0, 63, 0, 0, true);
    e->acc = 0;
    e->cnt = 0;
    sequential_pass(e, ci, ns);
    flush_bits(e);
    return e->oom ? MIME_JPEG_ERR_NOMEM : 0;
}

int enc_entropy_sequential(Enc *e)
{
    int all[4] = {0, 1, 2, 3};
    int rc;
    if (e->restart) {
        enc_put_u8(e, 0xFF);
        enc_put_u8(e, M_DRI);
        enc_put_u16(e, 4);
        enc_put_u16(e, e->restart);
    }
    if (!e->separate)
        return sequential_scan(e, all, e->ncomp);
    for (int i = 0; i < e->ncomp; i++)
        if ((rc = sequential_scan(e, &all[i], 1)))
            return rc;
    return 0;
}

/* ------------------------------------------------------------------ progressive */

typedef struct Progress {
    uint32_t eobrun;
    uint8_t pending[4096]; /* correction bits that follow the run of empty blocks. */
    unsigned npending;
} Progress;

static void flush_eobrun(Enc *e, Progress *pg)
{
    if (pg->eobrun) {
        int n = bit_length(pg->eobrun) - 1;
        emit(e, &e->ac[0], n << 4);
        if (n)
            put_bits(e, pg->eobrun & ((1u << n) - 1), n);
        pg->eobrun = 0;
    }
    for (unsigned i = 0; i < pg->npending; i++)
        put_bits(e, pg->pending[i], 1);
    pg->npending = 0;
}

static void ac_first_block(Enc *e, Progress *pg, const int16_t *blk, int ss, int se, int al)
{
    int run = 0;
    for (int k = ss; k <= se; k++) {
        int v = blk[k];
        unsigned t = (unsigned)(v < 0 ? -v : v) >> al;
        if (!t) {
            run++;
            continue;
        }
        flush_eobrun(e, pg);
        while (run > 15) {
            emit(e, &e->ac[0], 0xF0);
            run -= 16;
        }
        int n = bit_length(t);
        emit(e, &e->ac[0], run << 4 | n);
        put_bits(e, v < 0 ? ~t : t, n);
        run = 0;
    }
    if (run) {
        pg->eobrun++;
        if (pg->eobrun == 0x7FFF)
            flush_eobrun(e, pg);
    }
}

static void ac_refine_block(Enc *e, Progress *pg, const int16_t *blk, int ss, int se, int al)
{
    unsigned mag[64];
    int last_new = -1;
    for (int k = ss; k <= se; k++) {
        int v = blk[k];
        mag[k] = (unsigned)(v < 0 ? -v : v) >> al;
        if (mag[k] == 1)
            last_new = k;
    }
    uint8_t bits[64];
    unsigned nb = 0;
    int run = 0;
    for (int k = ss; k <= se; k++) {
        if (mag[k] == 0) {
            run++;
            if (run == 16 && k < last_new) {
                flush_eobrun(e, pg);
                emit(e, &e->ac[0], 0xF0);
                for (unsigned i = 0; i < nb; i++)
                    put_bits(e, bits[i], 1);
                nb = 0;
                run = 0;
            }
        } else if (mag[k] > 1) {
            bits[nb++] = (uint8_t)(mag[k] & 1);
        } else {
            flush_eobrun(e, pg);
            emit(e, &e->ac[0], run << 4 | 1);
            put_bits(e, blk[k] < 0 ? 0 : 1, 1);
            for (unsigned i = 0; i < nb; i++)
                put_bits(e, bits[i], 1);
            nb = 0;
            run = 0;
        }
    }
    if (run || nb) {
        if (pg->npending + nb > sizeof pg->pending)
            flush_eobrun(e, pg);
        pg->eobrun++;
        for (unsigned i = 0; i < nb; i++)
            pg->pending[pg->npending++] = bits[i];
        if (pg->eobrun == 0x7FFF || pg->npending > sizeof pg->pending - 64)
            flush_eobrun(e, pg);
    }
}

typedef struct ScanSpec {
    int ns;
    int ci[4];
    int ss, se, ah, al;
} ScanSpec;

static void progressive_pass(Enc *e, const ScanSpec *sc)
{
    Progress pg;
    pg.eobrun = 0;
    pg.npending = 0;
    e->rst_next = 0;
    for (int i = 0; i < e->ncomp; i++)
        e->comp[i].pred = 0;
    if (sc->ss == 0) {
        /* the interleaved DC scans. */
        uint32_t mcu = 0;
        for (uint32_t my = 0; my < e->mcuy; my++) {
            for (uint32_t mx = 0; mx < e->mcux; mx++, mcu++) {
                if (e->restart && mcu && mcu % e->restart == 0) {
                    restart_marker(e);
                    for (int i = 0; i < e->ncomp; i++)
                        e->comp[i].pred = 0;
                }
                for (int k = 0; k < sc->ns; k++) {
                    EncComp *c = &e->comp[sc->ci[k]];
                    for (int v = 0; v < c->v; v++)
                        for (int h = 0; h < c->h; h++) {
                            const int16_t *blk = block_of(e, c, 0, mx * (uint32_t)c->h + (uint32_t)h,
                                                          my * (uint32_t)c->v + (uint32_t)v);
                            if (sc->ah == 0)
                                code_dc(e, &e->dc[c->cls], &c->pred, blk[0] >> sc->al);
                            else
                                put_bits(e, (uint32_t)((blk[0] >> sc->al) & 1), 1);
                        }
                }
            }
        }
        return;
    }
    EncComp *c = &e->comp[sc->ci[0]];
    uint32_t n = 0;
    for (uint32_t by = 0; by < c->cbh; by++) {
        for (uint32_t bx = 0; bx < c->cbw; bx++, n++) {
            if (e->restart && n && n % e->restart == 0) {
                flush_eobrun(e, &pg);
                restart_marker(e);
            }
            const int16_t *blk = block_of(e, c, 0, bx, by);
            if (sc->ah == 0)
                ac_first_block(e, &pg, blk, sc->ss, sc->se, sc->al);
            else
                ac_refine_block(e, &pg, blk, sc->ss, sc->se, sc->al);
        }
    }
    flush_eobrun(e, &pg);
}

static int run_scan(Enc *e, const ScanSpec *sc)
{
    bool dc_tables = sc->ss == 0 && sc->ah == 0;
    bool ac_tables = sc->ss > 0;
    int classes = slots_needed(e);
    if (dc_tables || ac_tables) {
        for (int k = 0; k < classes; k++)
            huff_slot_clear(dc_tables ? &e->dc[k] : &e->ac[k]);
        e->counting = true;
        progressive_pass(e, sc);
        e->counting = false;
        if (dc_tables) {
            for (int k = 0; k < classes; k++) {
                huff_slot_optimal(&e->dc[k]);
                enc_write_dht(e, 0, k, &e->dc[k]);
            }
        } else {
            huff_slot_optimal(&e->ac[0]);
            enc_write_dht(e, 1, 0, &e->ac[0]);
        }
    }
    if (sc->ss > 0) {
        /* an AC scan names its one component's table as AC table 0. */
        int ci[1] = {sc->ci[0]};
        enc_put_u8(e, 0xFF);
        enc_put_u8(e, M_SOS);
        enc_put_u16(e, 8);
        enc_put_u8(e, 1);
        enc_put_u8(e, comp_id(e, ci[0]));
        enc_put_u8(e, 0);
        enc_put_u8(e, (unsigned)sc->ss);
        enc_put_u8(e, (unsigned)sc->se);
        enc_put_u8(e, (unsigned)(sc->ah << 4 | sc->al));
    } else {
        write_sos(e, sc->ci, sc->ns, 0, 0, sc->ah, sc->al, false);
    }
    e->acc = 0;
    e->cnt = 0;
    progressive_pass(e, sc);
    flush_bits(e);
    return e->oom ? MIME_JPEG_ERR_NOMEM : 0;
}

/*
 * scan order: the DC of every component with one bit dropped, then the low and high AC coefficients of each
 * component with one bit dropped, then each dropped bit.
 */
int enc_entropy_progressive(Enc *e)
{
    ScanSpec sc;
    int rc;
    if (e->restart) {
        enc_put_u8(e, 0xFF);
        enc_put_u8(e, M_DRI);
        enc_put_u16(e, 4);
        enc_put_u16(e, e->restart);
    }
    __builtin_memset(&sc, 0, sizeof sc);
    sc.ns = e->ncomp;
    for (int i = 0; i < e->ncomp; i++)
        sc.ci[i] = i;
    sc.ss = 0, sc.se = 0, sc.ah = 0, sc.al = 1;
    if ((rc = run_scan(e, &sc)))
        return rc;
    static const int bands[2][2] = {{1, 9}, {10, 63}};
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < e->ncomp; i++) {
            __builtin_memset(&sc, 0, sizeof sc);
            sc.ns = 1;
            sc.ci[0] = i;
            sc.ss = bands[b][0], sc.se = bands[b][1], sc.ah = 0, sc.al = 1;
            if ((rc = run_scan(e, &sc)))
                return rc;
        }
    }
    __builtin_memset(&sc, 0, sizeof sc);
    sc.ns = e->ncomp;
    for (int i = 0; i < e->ncomp; i++)
        sc.ci[i] = i;
    sc.ss = 0, sc.se = 0, sc.ah = 1, sc.al = 0;
    if ((rc = run_scan(e, &sc)))
        return rc;
    for (int i = 0; i < e->ncomp; i++) {
        __builtin_memset(&sc, 0, sizeof sc);
        sc.ns = 1;
        sc.ci[0] = i;
        sc.ss = 1, sc.se = 63, sc.ah = 1, sc.al = 0;
        if ((rc = run_scan(e, &sc)))
            return rc;
    }
    return 0;
}
