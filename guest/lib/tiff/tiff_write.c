#include "tiff_internal.h"

#define MAX_DEPTH 8
#define MAX_ENTRIES 65535u
#define MAX_FILE 0xffffff00u

bool tiff_buf_reserve(TiffBuf *b, size_t extra)
{
    if (b->failed)
        return false;
    if (extra > MAX_FILE || b->len > MAX_FILE - extra) {
        b->failed = true;
        return false;
    }
    size_t need = b->len + extra;
    if (need <= b->cap)
        return true;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < need)
        cap *= 2;
    uint8_t *p = mime_alloc(b->alloc, cap);
    if (!p) {
        b->failed = true;
        return false;
    }
    if (b->len)
        memcpy(p, b->p, b->len);
    mime_free(b->alloc, b->p);
    b->p = p;
    b->cap = cap;
    return true;
}

size_t tiff_buf_zeros(TiffBuf *b, size_t n)
{
    if (!tiff_buf_reserve(b, n))
        return SIZE_MAX;
    size_t pos = b->len;
    memset(b->p + pos, 0, n);
    b->len += n;
    return pos;
}

bool tiff_buf_align(TiffBuf *b, size_t align)
{
    size_t rel = b->len - b->base;
    size_t pad = (align - (rel & (align - 1))) & (align - 1);
    return pad == 0 || tiff_buf_zeros(b, pad) != SIZE_MAX;
}

void tiff_copy_values(uint8_t *dst, const uint8_t *src, uint64_t count, unsigned type, bool swap)
{
    size_t ts = mime_tiff_type_size(type);
    if (!swap || ts == 1) {
        memcpy(dst, src, (size_t)(count * ts));
        return;
    }
    unsigned unit = (type == MIME_TIFF_TYPE_RATIONAL || type == MIME_TIFF_TYPE_SRATIONAL) ? 4 : (unsigned)ts;
    uint64_t units = count * (ts / unit);
    for (uint64_t i = 0; i < units; i++) {
        const uint8_t *s = src + i * unit;
        uint8_t *d = dst + i * unit;
        if (unit == 2) {
            d[0] = s[1];
            d[1] = s[0];
        } else if (unit == 4) {
            uint32_t v;
            memcpy(&v, s, 4);
            v = __builtin_bswap32(v);
            memcpy(d, &v, 4);
        } else {
            uint64_t v;
            memcpy(&v, s, 8);
            v = __builtin_bswap64(v);
            memcpy(d, &v, 8);
        }
    }
}

static void sift_down(uint32_t *a, uint32_t start, uint32_t end)
{
    uint32_t root = start;
    while (2 * root + 1 < end) {
        uint32_t child = 2 * root + 1;
        if (child + 1 < end && a[child] < a[child + 1])
            child++;
        if (a[root] >= a[child])
            return;
        uint32_t t = a[root];
        a[root] = a[child];
        a[child] = t;
        root = child;
    }
}

static void heap_sort(uint32_t *a, uint32_t n)
{
    for (uint32_t i = n / 2; i-- > 0;)
        sift_down(a, i, n);
    for (uint32_t end = n; end-- > 1;) {
        uint32_t t = a[0];
        a[0] = a[end];
        a[end] = t;
        sift_down(a, 0, end);
    }
}

typedef struct Pending {
    uint32_t slot; /* position of the entry value field relative to base. */
    const MimeTiffWIfd *sub;
} Pending;

static bool entry_valid(const MimeTiffWEntry *e)
{
    if (e->sub)
        return true;
    if (e->type >= MIME_TIFF_TYPE_LONG8 || mime_tiff_type_size(e->type) == 0)
        return false;
    return e->count == 0 || e->data != NULL;
}

int tiff_write_ifd(TiffBuf *b, const MimeTiffWIfd *ifd, uint32_t depth, uint32_t *off)
{
    if (depth > MAX_DEPTH)
        return MIME_TIFF_ERR_PARAM;
    uint32_t n = ifd->entry_count;
    bool thumb = ifd->thumbnail && ifd->thumbnail_size;
    if (n > MAX_ENTRIES - 2 || (n && !ifd->entries))
        return MIME_TIFF_ERR_PARAM;
    for (uint32_t i = 0; i < n; i++)
        if (!entry_valid(&ifd->entries[i]))
            return MIME_TIFF_ERR_PARAM;
    uint32_t total = n + (thumb ? 2 : 0);
    if (thumb && ifd->thumbnail_size > MAX_FILE)
        return MIME_TIFF_ERR_PARAM;

    uint32_t stack_keys[64];
    uint32_t *keys = stack_keys;
    if (total > 64) {
        keys = mime_alloc(b->alloc, (size_t)total * 4);
        if (!keys)
            return MIME_TIFF_ERR_NOMEM;
    }
    for (uint32_t i = 0; i < n; i++)
        keys[i] = (uint32_t)ifd->entries[i].tag << 16 | i;
    if (thumb) {
        keys[n] = (uint32_t)MIME_TIFF_TAG_JPEG_INTERCHANGE_FORMAT << 16 | n;
        keys[n + 1] = (uint32_t)MIME_TIFF_TAG_JPEG_INTERCHANGE_LENGTH << 16 | (n + 1);
    }
    heap_sort(keys, total);
    /* drop repeated tags, the first one in the caller's order wins. */
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < total; i++)
        if (cnt == 0 || (keys[cnt - 1] >> 16) != (keys[i] >> 16))
            keys[cnt++] = keys[i];

    int rc = MIME_TIFF_OK;
    Pending pend_stack[8];
    Pending *pend = pend_stack;
    uint32_t pend_cap = 8, pend_n = 0;
    uint32_t thumb_slot_off = 0, thumb_slot_len = 0;
    bool pend_heap = false;

    if (!tiff_buf_align(b, 4)) {
        rc = MIME_TIFF_ERR_NOMEM;
        goto out;
    }
    uint32_t table = (uint32_t)(b->len - b->base);
    size_t tpos = tiff_buf_zeros(b, 2 + (size_t)cnt * 12 + 4);
    if (tpos == SIZE_MAX) {
        rc = MIME_TIFF_ERR_NOMEM;
        goto out;
    }
    tiff_wr16(b->p + tpos, (uint16_t)cnt, b->be);
    *off = table;

    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t idx = keys[i] & 0xffff;
        size_t epos = tpos + 2 + (size_t)i * 12;
        uint32_t slot = (uint32_t)(epos + 8 - b->base);
        if (idx >= n) {
            tiff_wr16(b->p + epos, (uint16_t)(keys[i] >> 16), b->be);
            tiff_wr16(b->p + epos + 2, MIME_TIFF_TYPE_LONG, b->be);
            tiff_wr32(b->p + epos + 4, 1, b->be);
            if (idx == n)
                thumb_slot_off = slot;
            else
                thumb_slot_len = slot;
            continue;
        }
        const MimeTiffWEntry *e = &ifd->entries[idx];
        tiff_wr16(b->p + epos, e->tag, b->be);
        if (e->sub) {
            tiff_wr16(b->p + epos + 2, MIME_TIFF_TYPE_LONG, b->be);
            tiff_wr32(b->p + epos + 4, 1, b->be);
            if (pend_n == pend_cap) {
                uint32_t ncap = pend_cap * 2;
                Pending *np = mime_alloc(b->alloc, (size_t)ncap * sizeof(Pending));
                if (!np) {
                    rc = MIME_TIFF_ERR_NOMEM;
                    goto out;
                }
                memcpy(np, pend, (size_t)pend_n * sizeof(Pending));
                if (pend_heap)
                    mime_free(b->alloc, pend);
                pend = np;
                pend_cap = ncap;
                pend_heap = true;
            }
            pend[pend_n].slot = slot;
            pend[pend_n].sub = e->sub;
            pend_n++;
            continue;
        }
        size_t ts = mime_tiff_type_size(e->type);
        uint64_t size = (uint64_t)e->count * ts;
        tiff_wr16(b->p + epos + 2, e->type, b->be);
        tiff_wr32(b->p + epos + 4, e->count, b->be);
        bool swap = b->be != tiff_is_bigendian_flag(e->flags);
        if (size <= 4) {
            if (size)
                tiff_copy_values(b->p + epos + 8, e->data, e->count, e->type, swap);
            continue;
        }
        if (!tiff_buf_align(b, 4)) {
            rc = MIME_TIFF_ERR_NOMEM;
            goto out;
        }
        if (size > MAX_FILE) {
            rc = MIME_TIFF_ERR_PARAM;
            goto out;
        }
        uint32_t voff = (uint32_t)(b->len - b->base);
        size_t vpos = tiff_buf_zeros(b, (size_t)size);
        if (vpos == SIZE_MAX) {
            rc = MIME_TIFF_ERR_NOMEM;
            goto out;
        }
        tiff_copy_values(b->p + vpos, e->data, e->count, e->type, swap);
        tiff_wr32(b->p + epos + 8, voff, b->be);
    }

    for (uint32_t i = 0; i < pend_n; i++) {
        uint32_t soff;
        rc = tiff_write_ifd(b, pend[i].sub, depth + 1, &soff);
        if (rc != MIME_TIFF_OK)
            goto out;
        tiff_wr32(b->p + b->base + pend[i].slot, soff, b->be);
    }

    if (thumb) {
        if (!tiff_buf_align(b, 2)) {
            rc = MIME_TIFF_ERR_NOMEM;
            goto out;
        }
        uint32_t toff = (uint32_t)(b->len - b->base);
        if (!tiff_buf_reserve(b, ifd->thumbnail_size)) {
            rc = MIME_TIFF_ERR_NOMEM;
            goto out;
        }
        memcpy(b->p + b->len, ifd->thumbnail, ifd->thumbnail_size);
        b->len += ifd->thumbnail_size;
        if (thumb_slot_off)
            tiff_wr32(b->p + b->base + thumb_slot_off, toff, b->be);
        if (thumb_slot_len)
            tiff_wr32(b->p + b->base + thumb_slot_len, (uint32_t)ifd->thumbnail_size, b->be);
    }

    if (ifd->next) {
        uint32_t noff;
        rc = tiff_write_ifd(b, ifd->next, depth, &noff);
        if (rc != MIME_TIFF_OK)
            goto out;
        tiff_wr32(b->p + tpos + 2 + (size_t)cnt * 12, noff, b->be);
    }
out:
    if (pend_heap)
        mime_free(b->alloc, pend);
    if (keys != stack_keys)
        mime_free(b->alloc, keys);
    return rc;
}

int mime_tiff_write(const MimeTiffWIfd *root, unsigned flags, const MimeAllocator *alloc, void **out,
                    size_t *out_len)
{
    if (!root || !alloc || !out || !out_len)
        return MIME_TIFF_ERR_PARAM;
    *out = NULL;
    TiffBuf b = {.alloc = alloc, .be = (flags & MIME_TIFF_WRITE_BIG_ENDIAN) != 0};
    if (flags & MIME_TIFF_WRITE_EXIF_PREFIX) {
        if (tiff_buf_zeros(&b, 6) == SIZE_MAX)
            return MIME_TIFF_ERR_NOMEM;
        memcpy(b.p, "Exif", 4);
        b.base = 6;
    }
    size_t h = tiff_buf_zeros(&b, 8);
    if (h == SIZE_MAX) {
        mime_free(alloc, b.p);
        return MIME_TIFF_ERR_NOMEM;
    }
    b.p[h] = b.p[h + 1] = b.be ? 'M' : 'I';
    tiff_wr16(b.p + h + 2, 42, b.be);
    uint32_t first;
    int rc = tiff_write_ifd(&b, root, 0, &first);
    if (rc != MIME_TIFF_OK) {
        mime_free(alloc, b.p);
        return rc;
    }
    tiff_wr32(b.p + h + 4, first, b.be);
    *out = b.p;
    *out_len = b.len;
    return MIME_TIFF_OK;
}

/* ---------------------------------------------------------------- import. */

#define IMPORT_MAX_TOTAL_ENTRIES 200000u
#define IMPORT_MAX_IFDS 64u

typedef struct ImportCtx {
    bool fill;
    MimeTiffWIfd *ifds;
    MimeTiffWEntry *entries;
    uint32_t ifd_used;
    uint64_t entry_used;
    bool failed;
} ImportCtx;

static bool is_pointer_tag(uint16_t tag)
{
    return tag == MIME_TIFF_TAG_EXIF_IFD || tag == MIME_TIFF_TAG_GPS_IFD || tag == MIME_TIFF_TAG_INTEROP_IFD;
}

static bool import_ifd(ImportCtx *c, const MimeTiffIfd *src, uint32_t depth, bool chain, uint32_t chain_pos,
                       MimeTiffWIfd **out)
{
    if (c->failed)
        return false;
    if (c->ifd_used >= IMPORT_MAX_IFDS || c->entry_used + src->count > IMPORT_MAX_TOTAL_ENTRIES) {
        c->failed = true;
        return false;
    }
    uint32_t ifd_idx = c->ifd_used++;
    uint64_t first = c->entry_used;
    c->entry_used += src->count;
    MimeTiffWIfd *dst = c->fill ? &c->ifds[ifd_idx] : NULL;
    MimeTiffWEntry scratch;
    uint32_t used = 0;
    const uint8_t *thumb = NULL;
    size_t thumb_size = 0;
    MimeTiffEntry e;

    uint64_t toff = 0, tlen = 0;
    bool have_off = false, have_len = false;
    for (uint32_t i = 0; i < src->count; i++) {
        if (mime_tiff_ifd_entry(src, i, &e) != MIME_TIFF_OK)
            continue;
        if (e.tag == MIME_TIFF_TAG_JPEG_INTERCHANGE_FORMAT)
            have_off = mime_tiff_entry_uint(&e, 0, &toff);
        else if (e.tag == MIME_TIFF_TAG_JPEG_INTERCHANGE_LENGTH)
            have_len = mime_tiff_entry_uint(&e, 0, &tlen);
    }
    if (have_off && have_len && toff < src->tiff.size && tlen && tlen <= src->tiff.size - toff) {
        thumb = src->tiff.data + toff;
        thumb_size = (size_t)tlen;
    }

    for (uint32_t i = 0; i < src->count; i++) {
        if (mime_tiff_ifd_entry(src, i, &e) != MIME_TIFF_OK || !e.data)
            continue;
        if (e.tag == MIME_TIFF_TAG_SUB_IFDS || e.type >= MIME_TIFF_TYPE_LONG8 || e.count > 0xffffffffu)
            continue;
        if (thumb && (e.tag == MIME_TIFF_TAG_JPEG_INTERCHANGE_FORMAT || e.tag == MIME_TIFF_TAG_JPEG_INTERCHANGE_LENGTH))
            continue;
        MimeTiffWEntry *w = c->fill ? &c->entries[first + used] : &scratch;
        memset(w, 0, sizeof *w);
        w->tag = e.tag;
        if (is_pointer_tag(e.tag)) {
            MimeTiffIfd sub;
            if (depth >= 4 || mime_tiff_ifd_sub(src, e.tag, 0, &sub) != MIME_TIFF_OK)
                continue;
            if (!import_ifd(c, &sub, depth + 1, false, 0, (MimeTiffWIfd **)&w->sub))
                continue;
            w->type = MIME_TIFF_TYPE_LONG;
            w->count = 1;
        } else {
            w->type = e.type;
            w->count = (uint32_t)e.count;
            w->data = e.data;
            w->flags = e.big_endian ? MIME_TIFF_W_BIG_ENDIAN : 0;
        }
        used++;
    }
    if (dst) {
        dst->entries = c->entries + first;
        dst->entry_count = used;
        dst->next = NULL;
        dst->thumbnail = thumb;
        dst->thumbnail_size = thumb_size;
    }
    MimeTiffIfd next;
    if (chain && chain_pos < 7 && mime_tiff_ifd_next(src, &next) == MIME_TIFF_OK) {
        MimeTiffWIfd *n = NULL;
        if (import_ifd(c, &next, depth, true, chain_pos + 1, &n) && dst)
            dst->next = n;
    }
    if (out)
        *out = dst;
    return !c->failed;
}

int mime_tiff_import(const MimeTiffIfd *src, unsigned flags, const MimeAllocator *alloc, const MimeTiffWIfd **out)
{
    if (!src || !alloc || !out)
        return MIME_TIFF_ERR_PARAM;
    *out = NULL;
    bool chain = (flags & MIME_TIFF_IMPORT_CHAIN) != 0;
    ImportCtx c = {0};
    import_ifd(&c, src, 0, chain, 0, NULL);
    if (c.failed)
        return MIME_TIFF_ERR_NOMEM;
    size_t ifd_bytes = (size_t)c.ifd_used * sizeof(MimeTiffWIfd);
    size_t total = ifd_bytes + (size_t)c.entry_used * sizeof(MimeTiffWEntry);
    uint8_t *mem = mime_alloc(alloc, total ? total : 1);
    if (!mem)
        return MIME_TIFF_ERR_NOMEM;
    memset(mem, 0, total);
    ImportCtx f = {0};
    f.fill = true;
    f.ifds = (MimeTiffWIfd *)mem;
    f.entries = (MimeTiffWEntry *)(mem + ifd_bytes);
    import_ifd(&f, src, 0, chain, 0, NULL);
    if (f.failed || f.ifd_used != c.ifd_used) {
        mime_free(alloc, mem);
        return MIME_TIFF_ERR_NOMEM;
    }
    *out = f.ifds;
    return MIME_TIFF_OK;
}
