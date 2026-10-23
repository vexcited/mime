#include "gif.h"
#include "gif_internal.h"

/* channel weights of the color distance, close to the luma of the eye. */
#define WR 5u
#define WG 9u
#define WB 2u

#define BIN_BITS 6
#define BIN_COUNT (1u << (3 * BIN_BITS))

static uint64_t div64(uint64_t a, uint32_t b)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) {
            r -= b;
            q |= (uint64_t)1 << i;
        }
    }
    return q;
}

/* fixed palettes. */

static const uint8_t cube_dims[][3] = {
    {2, 2, 2}, {3, 3, 3}, {4, 4, 4}, {5, 5, 5}, {6, 6, 6}, {6, 7, 6}, {6, 7, 6},
};

static unsigned cube_level(unsigned i, unsigned n)
{
    return (i * 255 + (n - 1) / 2) / (n - 1);
}

static bool fixed_cube(unsigned type, unsigned dims[3])
{
    if (type < MIME_GIF_PAL_HALFTONE8 || type > MIME_GIF_PAL_HALFTONE256)
        return false;
    const uint8_t *d = cube_dims[type - MIME_GIF_PAL_HALFTONE8];
    dims[0] = d[0];
    dims[1] = d[1];
    dims[2] = d[2];
    return true;
}

static unsigned fixed_gray_levels(unsigned type)
{
    switch (type) {
    case MIME_GIF_PAL_BLACK_WHITE:
        return 2;
    case MIME_GIF_PAL_GRAY4:
        return 4;
    case MIME_GIF_PAL_GRAY16:
        return 16;
    case MIME_GIF_PAL_GRAY256:
        return 256;
    default:
        return 0;
    }
}

unsigned mime_gif_fixed_palette(unsigned type, bool add_transparent, uint8_t palette[768])
{
    unsigned count = 0;
    unsigned dims[3];
    unsigned levels = fixed_gray_levels(type);
    if (levels) {
        for (unsigned i = 0; i < levels; i++) {
            uint8_t v = (uint8_t)cube_level(i, levels);
            palette[count * 3] = palette[count * 3 + 1] = palette[count * 3 + 2] = v;
            count++;
        }
    } else if (fixed_cube(type, dims)) {
        for (unsigned r = 0; r < dims[0]; r++)
            for (unsigned g = 0; g < dims[1]; g++)
                for (unsigned b = 0; b < dims[2]; b++) {
                    palette[count * 3] = (uint8_t)cube_level(r, dims[0]);
                    palette[count * 3 + 1] = (uint8_t)cube_level(g, dims[1]);
                    palette[count * 3 + 2] = (uint8_t)cube_level(b, dims[2]);
                    count++;
                }
        if (type == MIME_GIF_PAL_HALFTONE256) {
            /* the four extra entries are a guess, evenly spaced grays. */
            static const uint8_t extra[4] = {0x20, 0x60, 0xa0, 0xe0};
            for (unsigned i = 0; i < 4; i++) {
                palette[count * 3] = palette[count * 3 + 1] = palette[count * 3 + 2] = extra[i];
                count++;
            }
        }
    } else {
        return 0;
    }
    if (add_transparent) {
        if (count == 256)
            count--;
        palette[count * 3] = palette[count * 3 + 1] = palette[count * 3 + 2] = 0;
        count++;
    }
    return count;
}

/* nearest color lookup. */

enum { KIND_BIN, KIND_EXACT, KIND_CUBE, KIND_GRAY };

typedef struct Mapper {
    int kind;
    const uint8_t *pal;
    unsigned count;
    int transparent;
    uint16_t *bin_cache;  /* BIN_COUNT entries, 0xffff unset. */
    uint32_t *key_cache;  /* 4096 entries. */
    uint8_t *val_cache;   /* 4096 entries. */
    unsigned dims[3];     /* cube levels. */
    unsigned gray_levels;
} Mapper;

static unsigned nearest_scan(const Mapper *m, int r, int g, int b)
{
    uint32_t best = 0xffffffffu;
    unsigned bi = 0;
    for (unsigned i = 0; i < m->count; i++) {
        if ((int)i == m->transparent)
            continue;
        int dr = r - m->pal[i * 3], dg = g - m->pal[i * 3 + 1], db = b - m->pal[i * 3 + 2];
        uint32_t d = WR * (uint32_t)(dr * dr) + WG * (uint32_t)(dg * dg) + WB * (uint32_t)(db * db);
        if (d < best) {
            best = d;
            bi = i;
            if (!d)
                break;
        }
    }
    return bi;
}

static inline unsigned expand6(unsigned v)
{
    return (v << 2) | (v >> 4);
}

static inline unsigned lookup(Mapper *m, int r, int g, int b)
{
    switch (m->kind) {
    case KIND_BIN: {
        unsigned bin = ((unsigned)r >> 2) << 12 | ((unsigned)g >> 2) << 6 | ((unsigned)b >> 2);
        unsigned v = m->bin_cache[bin];
        if (v == 0xffff) {
            v = nearest_scan(m, (int)expand6((unsigned)r >> 2), (int)expand6((unsigned)g >> 2),
                             (int)expand6((unsigned)b >> 2));
            m->bin_cache[bin] = (uint16_t)v;
        }
        return v;
    }
    case KIND_EXACT: {
        uint32_t key = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        unsigned h = (key * 0x9e3779b1u) >> 20;
        if (m->key_cache[h] != key) {
            m->key_cache[h] = key;
            m->val_cache[h] = (uint8_t)nearest_scan(m, r, g, b);
        }
        return m->val_cache[h];
    }
    case KIND_CUBE: {
        unsigned nr = m->dims[0] - 1, ng = m->dims[1] - 1, nb = m->dims[2] - 1;
        unsigned qr = ((unsigned)r * nr + 127) / 255, qg = ((unsigned)g * ng + 127) / 255,
                 qb = ((unsigned)b * nb + 127) / 255;
        return (qr * m->dims[1] + qg) * m->dims[2] + qb;
    }
    default: {
        unsigned y = (77u * (unsigned)r + 150u * (unsigned)g + 29u * (unsigned)b + 128) >> 8;
        return (y * (m->gray_levels - 1) + 127) / 255;
    }
    }
}

static inline int clamp255(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

/*
 * maps the whole image. with dither the error of each pixel is spread to its
 * neighbours, Floyd Steinberg, scanning left to right on even rows and right to
 * left on odd ones. pixels that are transparent get no error and give none.
 */
static int map_image(Mapper *m, const uint8_t *bgra, unsigned w, unsigned h, size_t stride, bool dither,
                     uint8_t *indices, size_t istride, const MimeAllocator *alloc)
{
    const int tr = m->transparent;
    const bool gray = m->kind == KIND_GRAY;

    if (!dither) {
        for (unsigned y = 0; y < h; y++) {
            const uint8_t *s = bgra + y * stride;
            uint8_t *d = indices + y * istride;
            for (unsigned x = 0; x < w; x++, s += 4) {
                if (s[3] < 128 && tr >= 0)
                    d[x] = (uint8_t)tr;
                else
                    d[x] = (uint8_t)lookup(m, s[2], s[1], s[0]);
            }
        }
        return MIME_GIF_OK;
    }

    size_t n = (size_t)(w + 2) * 3;
    int16_t *errs = mime_alloc(alloc, n * 2 * sizeof(int16_t));
    if (!errs)
        return MIME_GIF_ERR_NOMEM;
    __builtin_memset(errs, 0, n * 2 * sizeof(int16_t));
    int16_t *cur = errs, *nxt = errs + n;

    for (unsigned y = 0; y < h; y++) {
        const uint8_t *s = bgra + y * stride;
        uint8_t *d = indices + y * istride;
        bool rtl = (y & 1) != 0;
        __builtin_memset(nxt, 0, n * sizeof(int16_t));
        for (unsigned i = 0; i < w; i++) {
            unsigned x = rtl ? w - 1 - i : i;
            const uint8_t *px = s + (size_t)x * 4;
            int16_t *ce = cur + (size_t)(x + 1) * 3;
            if (px[3] < 128 && tr >= 0) {
                d[x] = (uint8_t)tr;
                continue;
            }
            int r = px[2], g = px[1], b = px[0];
            if (gray)
                r = g = b = (int)((77u * (unsigned)r + 150u * (unsigned)g + 29u * (unsigned)b + 128) >> 8);
            r = clamp255(r + ce[0]);
            g = clamp255(g + ce[1]);
            b = clamp255(b + ce[2]);
            unsigned idx = lookup(m, r, g, b);
            d[x] = (uint8_t)idx;
            int e[3] = {r - m->pal[idx * 3], g - m->pal[idx * 3 + 1], b - m->pal[idx * 3 + 2]};
            /* neighbour in scan direction, then below behind, below and below ahead. */
            int ahead = rtl ? -1 : 1;
            int16_t *fw = ce + ahead * 3;
            int16_t *bb = nxt + (size_t)(x + 1) * 3;
            for (int c = 0; c < 3; c++) {
                int a = (e[c] * 7) >> 4, bl = (e[c] * 3) >> 4, bm = (e[c] * 5) >> 4;
                int br = e[c] - a - bl - bm;
                fw[c] = (int16_t)(fw[c] + a);
                bb[c - ahead * 3] = (int16_t)(bb[c - ahead * 3] + bl);
                bb[c] = (int16_t)(bb[c] + bm);
                bb[c + ahead * 3] = (int16_t)(bb[c + ahead * 3] + br);
            }
        }
        int16_t *t = cur;
        cur = nxt;
        nxt = t;
    }
    mime_free(alloc, errs);
    return MIME_GIF_OK;
}

/* public mapping functions. */

int mime_gif_map_palette(const uint8_t *bgra, unsigned width, unsigned height, size_t stride, const uint8_t *palette,
                         unsigned count, unsigned flags, int transparent_index, uint8_t *indices,
                         size_t index_stride, const MimeAllocator *alloc)
{
    if (!bgra || !palette || !indices || !alloc || !width || !height || stride < (size_t)width * 4 ||
        index_stride < width || count < 1 || count > 256 || transparent_index >= (int)count)
        return MIME_GIF_ERR_PARAM;
    Mapper m;
    __builtin_memset(&m, 0, sizeof(m));
    m.kind = KIND_EXACT;
    m.pal = palette;
    m.count = count;
    m.transparent = transparent_index;
    uint32_t *keys = mime_alloc(alloc, 4096 * 5);
    if (!keys)
        return MIME_GIF_ERR_NOMEM;
    __builtin_memset(keys, 0xff, 4096 * sizeof(uint32_t));
    m.key_cache = keys;
    m.val_cache = (uint8_t *)(keys + 4096);
    int rc = map_image(&m, bgra, width, height, stride, (flags & MIME_GIF_QUANT_DITHER) != 0, indices, index_stride,
                       alloc);
    mime_free(alloc, keys);
    return rc;
}

int mime_gif_map_fixed(const uint8_t *bgra, unsigned width, unsigned height, size_t stride, unsigned type,
                       unsigned flags, int transparent_index, uint8_t *indices, size_t index_stride,
                       const MimeAllocator *alloc)
{
    if (!bgra || !indices || !alloc || !width || !height || stride < (size_t)width * 4 || index_stride < width ||
        transparent_index > 255)
        return MIME_GIF_ERR_PARAM;
    uint8_t *pal = mime_alloc(alloc, 768);
    if (!pal)
        return MIME_GIF_ERR_NOMEM;
    unsigned count = mime_gif_fixed_palette(type, false, pal);
    if (!count) {
        mime_free(alloc, pal);
        return MIME_GIF_ERR_PARAM;
    }
    Mapper m;
    __builtin_memset(&m, 0, sizeof(m));
    m.pal = pal;
    m.count = count;
    m.transparent = transparent_index;
    unsigned levels = fixed_gray_levels(type);
    if (levels) {
        m.kind = KIND_GRAY;
        m.gray_levels = levels;
    } else {
        m.kind = KIND_CUBE;
        fixed_cube(type, m.dims);
    }
    int rc = map_image(&m, bgra, width, height, stride, (flags & MIME_GIF_QUANT_DITHER) != 0, indices, index_stride,
                       alloc);
    mime_free(alloc, pal);
    return rc;
}

/* median cut. */

typedef struct Entry {
    uint32_t bin;
    uint32_t weight;
} Entry;

typedef struct Box {
    unsigned lo, hi; /* range in the entry array. */
    uint32_t total;
    uint64_t score;
    unsigned axis;
} Box;

static inline unsigned chan(uint32_t bin, unsigned axis)
{
    return (bin >> (12 - 6 * axis)) & 63; /* axis 0 is red. */
}

static void box_stats(const Entry *e, Box *b)
{
    uint64_t sum[3] = {0, 0, 0}, sum2[3] = {0, 0, 0};
    uint32_t total = 0;
    for (unsigned i = b->lo; i < b->hi; i++) {
        uint32_t w = e[i].weight;
        total += w;
        for (unsigned c = 0; c < 3; c++) {
            uint64_t v = chan(e[i].bin, c);
            sum[c] += w * v;
            sum2[c] += w * v * v;
        }
    }
    b->total = total;
    static const uint32_t wts[3] = {WR, WG, WB};
    uint64_t score = 0, best = 0;
    b->axis = 0;
    for (unsigned c = 0; c < 3; c++) {
        uint64_t num = ((uint64_t)total * sum2[c] - sum[c] * sum[c]) * wts[c];
        if (num > best) {
            best = num;
            b->axis = c;
        }
        score += total ? div64(num, total) : 0;
    }
    b->score = b->hi - b->lo >= 2 ? score : 0;
}

/* splits the box at the weighted median of its widest axis. */
static void box_split(Entry *e, Box *b, Box *out)
{
    uint32_t bucket[64];
    __builtin_memset(bucket, 0, sizeof(bucket));
    for (unsigned i = b->lo; i < b->hi; i++)
        bucket[chan(e[i].bin, b->axis)] += e[i].weight;
    uint32_t cum = 0;
    unsigned m = 0, vmax = 0, vprev = 0;
    bool found = false;
    for (unsigned v = 0; v < 64; v++) {
        if (!bucket[v])
            continue;
        if (!found) {
            cum += bucket[v];
            if ((uint64_t)cum * 2 >= b->total) {
                m = v;
                found = true;
            } else {
                vprev = v;
            }
        }
        vmax = v;
    }
    if (m == vmax) {
        /* everything is at or below the median, split just below the top value. */
        m = vprev;
        if (!bucket[m]) {
            for (unsigned v = 0; v < vmax; v++)
                if (bucket[v])
                    m = v;
        }
    }
    unsigned i = b->lo, j = b->hi;
    while (i < j) {
        if (chan(e[i].bin, b->axis) <= m) {
            i++;
        } else {
            j--;
            Entry t = e[i];
            e[i] = e[j];
            e[j] = t;
        }
    }
    *out = *b;
    out->lo = i;
    b->hi = i;
}

int mime_gif_quantize(const uint8_t *bgra, unsigned width, unsigned height, size_t stride, unsigned max_colors,
                      unsigned flags, uint8_t palette[768], unsigned *palette_size, int *transparent_index,
                      uint8_t *indices, size_t index_stride, const MimeAllocator *alloc)
{
    if (!bgra || !palette || !palette_size || !transparent_index || !indices || !alloc || !width || !height ||
        stride < (size_t)width * 4 || index_stride < width || max_colors < 2 || max_colors > 256)
        return MIME_GIF_ERR_PARAM;
    if ((uint64_t)width * height > MIME_GIF_MAX_PIXELS)
        return MIME_GIF_ERR_UNSUPPORTED;

    /* first pass: transparency and the exact color set, given up at max_colors + 1 colors. */
    uint32_t *keys = mime_alloc(alloc, 1024 * 5);
    if (!keys)
        return MIME_GIF_ERR_NOMEM;
    uint8_t *vals = (uint8_t *)(keys + 1024);
    __builtin_memset(keys, 0, 1024 * sizeof(uint32_t));
    uint8_t *colors = palette;
    unsigned ncolors = 0;
    bool has_t = false, overflow = false;
    for (unsigned y = 0; y < height && !overflow; y++) {
        const uint8_t *s = bgra + y * stride;
        for (unsigned x = 0; x < width; x++, s += 4) {
            if (s[3] < 128) {
                has_t = true;
                continue;
            }
            uint32_t key = 0x80000000u | (uint32_t)s[2] << 16 | (uint32_t)s[1] << 8 | s[0];
            unsigned h = (key * 0x9e3779b1u) >> 22;
            while (keys[h] && keys[h] != key)
                h = (h + 1) & 1023;
            if (keys[h])
                continue;
            if (ncolors == 256) {
                overflow = true;
                break;
            }
            keys[h] = key;
            vals[h] = (uint8_t)ncolors;
            colors[ncolors * 3] = s[2];
            colors[ncolors * 3 + 1] = s[1];
            colors[ncolors * 3 + 2] = s[0];
            ncolors++;
        }
    }
    unsigned tcount = has_t ? 1 : 0;
    unsigned used;
    if (!overflow && ncolors + tcount <= max_colors) {
        /* exact. the table is complete, as the whole image was scanned. */
        unsigned tidx = (flags & MIME_GIF_QUANT_FULL_PALETTE) ? max_colors - 1 : ncolors;
        for (unsigned y = 0; y < height; y++) {
            const uint8_t *s = bgra + y * stride;
            uint8_t *d = indices + y * index_stride;
            for (unsigned x = 0; x < width; x++, s += 4) {
                if (s[3] < 128) {
                    d[x] = (uint8_t)tidx;
                    continue;
                }
                uint32_t key = 0x80000000u | (uint32_t)s[2] << 16 | (uint32_t)s[1] << 8 | s[0];
                unsigned h = (key * 0x9e3779b1u) >> 22;
                while (keys[h] != key)
                    h = (h + 1) & 1023;
                d[x] = vals[h];
            }
        }
        mime_free(alloc, keys);
        used = ncolors;
    } else {
        mime_free(alloc, keys);
        unsigned k = max_colors - tcount;

        uint32_t *hist = mime_alloc(alloc, (size_t)BIN_COUNT * sizeof(uint32_t));
        if (!hist)
            return MIME_GIF_ERR_NOMEM;
        __builtin_memset(hist, 0, (size_t)BIN_COUNT * sizeof(uint32_t));
        for (unsigned y = 0; y < height; y++) {
            const uint8_t *s = bgra + y * stride;
            for (unsigned x = 0; x < width; x++, s += 4) {
                if (s[3] >= 128)
                    hist[(unsigned)(s[2] >> 2) << 12 | (unsigned)(s[1] >> 2) << 6 | (unsigned)(s[0] >> 2)]++;
            }
        }
        unsigned wshift = 0;
        while (((uint64_t)width * height >> wshift) > (1u << 23))
            wshift++;
        unsigned nentries = 0;
        for (unsigned i = 0; i < BIN_COUNT; i++)
            if (hist[i])
                nentries++;
        Entry *ents = mime_alloc(alloc, ((size_t)nentries + 1) * sizeof(Entry) + 256 * sizeof(Box));
        if (!ents) {
            mime_free(alloc, hist);
            return MIME_GIF_ERR_NOMEM;
        }
        Box *boxes = (Box *)(ents + nentries + 1);
        unsigned ne = 0;
        for (unsigned i = 0; i < BIN_COUNT; i++) {
            if (hist[i]) {
                uint32_t wgt = hist[i] >> wshift;
                ents[ne].bin = i;
                ents[ne].weight = wgt ? wgt : 1;
                ne++;
            }
        }
        mime_free(alloc, hist);

        unsigned nboxes = 0;
        if (ne) {
            boxes[0].lo = 0;
            boxes[0].hi = ne;
            box_stats(ents, &boxes[0]);
            nboxes = 1;
        }
        while (nboxes < k) {
            unsigned pick = 0;
            for (unsigned i = 1; i < nboxes; i++)
                if (boxes[i].score > boxes[pick].score)
                    pick = i;
            if (!nboxes || !boxes[pick].score)
                break;
            box_split(ents, &boxes[pick], &boxes[nboxes]);
            box_stats(ents, &boxes[pick]);
            box_stats(ents, &boxes[nboxes]);
            nboxes++;
        }
        for (unsigned i = 0; i < nboxes; i++) {
            uint64_t sum[3] = {0, 0, 0};
            for (unsigned j = boxes[i].lo; j < boxes[i].hi; j++)
                for (unsigned c = 0; c < 3; c++)
                    sum[c] += (uint64_t)ents[j].weight * expand6(chan(ents[j].bin, c));
            uint32_t total = boxes[i].total;
            for (unsigned c = 0; c < 3; c++)
                palette[i * 3 + c] = (uint8_t)((uint32_t)(sum[c] + total / 2) / total);
        }
        mime_free(alloc, ents);
        used = nboxes;

        uint16_t *cache = mime_alloc(alloc, (size_t)BIN_COUNT * sizeof(uint16_t));
        if (!cache)
            return MIME_GIF_ERR_NOMEM;
        __builtin_memset(cache, 0xff, (size_t)BIN_COUNT * sizeof(uint16_t));
        Mapper m;
        __builtin_memset(&m, 0, sizeof(m));
        m.kind = KIND_BIN;
        m.pal = palette;
        m.count = used;
        m.transparent = has_t ? ((flags & MIME_GIF_QUANT_FULL_PALETTE) ? (int)max_colors - 1 : (int)used) : -1;
        m.bin_cache = cache;
        int rc = map_image(&m, bgra, width, height, stride, (flags & MIME_GIF_QUANT_DITHER) != 0, indices,
                           index_stride, alloc);
        mime_free(alloc, cache);
        if (rc < 0)
            return rc;
    }

    unsigned total_entries = used + tcount;
    *transparent_index = -1;
    if (has_t) {
        unsigned tidx = (flags & MIME_GIF_QUANT_FULL_PALETTE) ? max_colors - 1 : used;
        __builtin_memset(palette + used * 3, 0, (size_t)(tidx + 1 - used) * 3);
        *transparent_index = (int)tidx;
        total_entries = tidx + 1;
    }
    if ((flags & MIME_GIF_QUANT_FULL_PALETTE) && total_entries < max_colors) {
        __builtin_memset(palette + total_entries * 3, 0, (size_t)(max_colors - total_entries) * 3);
        total_entries = max_colors;
    }
    if (!total_entries) {
        palette[0] = palette[1] = palette[2] = 0;
        total_entries = 1;
    }
    *palette_size = total_entries;
    return MIME_GIF_OK;
}
