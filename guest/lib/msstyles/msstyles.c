/* msstyles theme file parser. */
#include "msstyles.h"

struct MimeMsstylesRec {
    uint32_t off; /* record offset inside the variant blob. */
    int32_t prop, type, cls, part, state, res;
    uint32_t size;
};

#define REC_HEADER 32u
#define MAX_CLASSES 65535u
#define MAX_NAME 260u
#define RT_STRING_ID 6u

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static unsigned lower(unsigned c)
{
    return c - 'A' <= 'Z' - 'A' ? c + 32 : c;
}

static int32_t muldiv(int32_t a, int32_t b, int32_t c)
{
    if (c == 0)
        return -1;
    int64_t n = (int64_t)a * b;
    bool neg = (n < 0) != (c < 0);
    uint64_t un = n < 0 ? (uint64_t)-n : (uint64_t)n;
    uint64_t uc = c < 0 ? (uint64_t)-(int64_t)c : (uint64_t)c;
    uint64_t r = (un + uc / 2) / uc;
    return neg ? -(int32_t)r : (int32_t)r;
}

/* pe and resource directory. */

typedef struct Pe {
    const uint8_t *d;
    size_t n;
    uint32_t sec_off, sec_count;
    uint32_t rsrc;     /* file offset of the resource directory. */
    uint32_t rsrc_end; /* end of the raw section data holding it. */
} Pe;

static bool pe_rva(const Pe *pe, uint32_t rva, uint32_t size, uint32_t *off, uint32_t *limit)
{
    for (uint32_t i = 0; i < pe->sec_count; i++) {
        const uint8_t *s = pe->d + pe->sec_off + 40u * i;
        uint32_t va = rd32(s + 12), raw = rd32(s + 16), ptr = rd32(s + 20);
        if (rva < va || rva - va >= raw)
            continue;
        if ((uint64_t)ptr + raw > pe->n)
            return false;
        uint32_t delta = rva - va;
        if (size > raw - delta)
            return false;
        *off = ptr + delta;
        if (limit)
            *limit = ptr + raw;
        return true;
    }
    return false;
}

static bool pe_open(Pe *pe, const uint8_t *d, size_t n)
{
    pe->d = d;
    pe->n = n;
    if (!d || n < 0x40 || d[0] != 'M' || d[1] != 'Z')
        return false;
    uint32_t nt = rd32(d + 0x3c);
    if ((uint64_t)nt + 24 > n || rd32(d + nt) != 0x00004550)
        return false;
    uint32_t sections = rd16(d + nt + 6), opt_size = rd16(d + nt + 20);
    uint32_t opt = nt + 24;
    if ((uint64_t)opt + opt_size > n || opt_size < 2)
        return false;
    uint32_t magic = rd16(d + opt), dir_off, count_off;
    if (magic == 0x20b) {
        count_off = 108;
        dir_off = 112;
    } else if (magic == 0x10b) {
        count_off = 92;
        dir_off = 96;
    } else {
        return false;
    }
    if (opt_size < dir_off + 3 * 8 || rd32(d + opt + count_off) < 3)
        return false;
    uint32_t rva = rd32(d + opt + dir_off + 16), size = rd32(d + opt + dir_off + 20);
    pe->sec_off = opt + opt_size;
    pe->sec_count = sections;
    if ((uint64_t)pe->sec_off + 40ull * sections > n || rva == 0 || size < 16)
        return false;
    return pe_rva(pe, rva, 16, &pe->rsrc, &pe->rsrc_end);
}

/* compares a resource directory string with an ascii name, ignoring case. */
static bool res_name_eq(const Pe *pe, uint32_t off, const char *name)
{
    if ((uint64_t)off + 2 > pe->rsrc_end)
        return false;
    uint32_t len = rd16(pe->d + off);
    if ((uint64_t)off + 2 + 2ull * len > pe->rsrc_end)
        return false;
    for (uint32_t i = 0; i < len; i++) {
        unsigned c = rd16(pe->d + off + 2 + 2 * i);
        if (!name[i] || lower(c) != lower((unsigned char)name[i]))
            return false;
    }
    return name[len] == 0;
}

/* returns the matching entry's raw OffsetToData. a NULL name matches the id, id ~0 takes the first entry. */
static bool res_entry(const Pe *pe, uint32_t dir, const char *name, uint32_t id, uint32_t *out)
{
    uint32_t at = pe->rsrc + dir;
    if (dir > pe->rsrc_end - pe->rsrc || (uint64_t)at + 16 > pe->rsrc_end)
        return false;
    uint32_t named = rd16(pe->d + at + 12), ids = rd16(pe->d + at + 14);
    uint32_t total = named + ids;
    if ((uint64_t)at + 16 + 8ull * total > pe->rsrc_end)
        return false;
    for (uint32_t i = 0; i < total; i++) {
        const uint8_t *e = pe->d + at + 16 + 8 * i;
        uint32_t ename = rd32(e);
        bool hit;
        if (id == ~0u && !name)
            hit = true;
        else if (ename & 0x80000000u)
            hit = name && res_name_eq(pe, pe->rsrc + (ename & 0x7fffffffu), name);
        else
            hit = !name && ename == id;
        if (hit) {
            *out = rd32(e + 4);
            return true;
        }
    }
    return false;
}

static bool pe_resource(const Pe *pe, const char *type_str, uint32_t type_id, const char *name_str, uint32_t name_id,
                        const uint8_t **data, uint32_t *size)
{
    uint32_t e;
    if (!res_entry(pe, 0, type_str, type_id, &e) || !(e & 0x80000000u))
        return false;
    if (!res_entry(pe, e & 0x7fffffffu, name_str, name_id, &e) || !(e & 0x80000000u))
        return false;
    if (!res_entry(pe, e & 0x7fffffffu, nullptr, ~0u, &e) || (e & 0x80000000u))
        return false;
    uint32_t at = pe->rsrc + e;
    if (e > pe->rsrc_end - pe->rsrc || (uint64_t)at + 16 > pe->rsrc_end)
        return false;
    uint32_t rva = rd32(pe->d + at), sz = rd32(pe->d + at + 4), off;
    if (!pe_rva(pe, rva, sz, &off, nullptr))
        return false;
    *data = pe->d + off;
    *size = sz;
    return true;
}

static size_t pe_string(const Pe *pe, uint32_t id, uint16_t *buf, size_t cap)
{
    const uint8_t *d;
    uint32_t size;
    if (id > 0xffffu || !pe_resource(pe, nullptr, RT_STRING_ID, nullptr, (id >> 4) + 1, &d, &size))
        return 0;
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 16; i++) {
        if (pos + 2 > size)
            return 0;
        uint32_t len = rd16(d + pos);
        pos += 2;
        if (2ull * len > size - pos)
            return 0;
        if (i == (id & 15)) {
            if (!len || !cap)
                return len;
            size_t n = len < cap - 1 ? len : cap - 1;
            for (size_t j = 0; j < n; j++)
                buf[j] = rd16(d + pos + 2 * j);
            buf[n] = 0;
            return n;
        }
        pos += 2 * len;
    }
    return 0;
}

bool mime_msstyles_resource(const MimeMsstyles *ms, const char *type_str, uint32_t type_id, const char *name_str,
                            uint32_t name_id, const uint8_t **data, uint32_t *size)
{
    Pe pe;
    if (!ms || !pe_open(&pe, ms->file, ms->file_size))
        return false;
    return pe_resource(&pe, type_str, type_id, name_str, name_id, data, size);
}

size_t mime_msstyles_load_string(const MimeMsstyles *ms, uint32_t id, uint16_t *buf, size_t cap)
{
    Pe pe;
    size_t n = 0;
    if (cap)
        buf[0] = 0;
    if (!ms)
        return 0;
    if (ms->mui && pe_open(&pe, ms->mui, ms->mui_size))
        n = pe_string(&pe, id, buf, cap);
    if (!n && pe_open(&pe, ms->file, ms->file_size))
        n = pe_string(&pe, id, buf, cap);
    return n;
}

/* class names. */

static unsigned name_char(const MimeMsstyles *ms, int cls, uint32_t i)
{
    return rd16(ms->file + ms->class_name[cls] + 2 * i);
}

/* compares class name chars from up to but excluding to with a utf-16 string of len chars. */
static bool name_range_eq(const MimeMsstyles *ms, int cls, uint32_t from, uint32_t to, const uint16_t *s, size_t len)
{
    if (to - from != len)
        return false;
    for (uint32_t i = 0; i < len; i++)
        if (lower(name_char(ms, cls, from + i)) != lower(s[i]))
            return false;
    return true;
}

static uint32_t app_len(const MimeMsstyles *ms, int cls)
{
    return ms->class_sep[cls] ? ms->class_sep[cls] - 2u : 0u;
}

/* app NULL or empty selects classes without app name. */
static bool class_eq(const MimeMsstyles *ms, int cls, const uint16_t *app, size_t app_n, const uint16_t *name,
                     size_t name_n)
{
    uint32_t sep = ms->class_sep[cls];
    if (!name_range_eq(ms, cls, sep, ms->class_len[cls], name, name_n))
        return false;
    if (!app_n)
        return sep == 0;
    return sep && name_range_eq(ms, cls, 0, app_len(ms, cls), app, app_n);
}

static bool full_name_eq(const MimeMsstyles *ms, int a, int b)
{
    if (ms->class_len[a] != ms->class_len[b])
        return false;
    for (uint32_t i = 0; i < ms->class_len[a]; i++)
        if (lower(name_char(ms, a, i)) != lower(name_char(ms, b, i)))
            return false;
    return true;
}

static int find_plain(const MimeMsstyles *ms, const char *name)
{
    uint16_t w[32];
    size_t n = 0;
    while (name[n] && n < 31) {
        w[n] = (unsigned char)name[n];
        n++;
    }
    for (uint32_t c = 0; c < ms->class_count; c++)
        if (class_eq(ms, (int)c, nullptr, 0, w, n))
            return (int)c;
    return -1;
}

static bool parse_cmap(MimeMsstyles *ms, const uint8_t *cmap, uint32_t size)
{
    uint32_t count = 0;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t pos = 0, n = 0;
        while (pos + 2 <= size && n < MAX_CLASSES) {
            uint32_t len = 0;
            while (pos + 2 * len + 2 <= size && rd16(cmap + pos + 2 * len))
                len++;
            if (pos + 2 * len + 2 > size || len > 0xffffu)
                break;
            if (pass) {
                ms->class_name[n] = (uint32_t)(cmap + pos - ms->file);
                ms->class_len[n] = (uint16_t)len;
                uint32_t sep = 0;
                for (uint32_t i = 0; i + 1 < len; i++)
                    if (rd16(cmap + pos + 2 * i) == ':' && rd16(cmap + pos + 2 * i + 2) == ':')
                        sep = i + 2;
                ms->class_sep[n] = (uint16_t)sep;
            }
            n++;
            uint32_t step = (2 * len + 9) & ~7u;
            if (step > size - pos)
                break;
            pos += step;
        }
        if (!pass) {
            count = n;
            if (!count)
                return false;
            ms->class_name = mime_alloc(ms->alloc, count * sizeof(uint32_t));
            ms->class_len = mime_alloc(ms->alloc, count * sizeof(uint16_t));
            ms->class_sep = mime_alloc(ms->alloc, count * sizeof(uint16_t));
            ms->base = mime_alloc(ms->alloc, count * sizeof(int32_t));
            ms->first = mime_alloc(ms->alloc, count * sizeof(uint32_t));
            ms->end = mime_alloc(ms->alloc, count * sizeof(uint32_t));
            if (!ms->class_name || !ms->class_len || !ms->class_sep || !ms->base || !ms->first || !ms->end)
                return false;
        }
    }
    ms->class_count = count;
    return true;
}

/* variant selection through VMAP: entries of name, size name, color name. */

static bool vmap_str(const uint8_t *d, uint32_t size, uint32_t *pos, uint32_t *str, uint32_t *len)
{
    if (*pos > size || size - *pos < 4)
        return false;
    uint32_t n = rd32(d + *pos);
    if (n == 0 || n > (size - *pos - 4) / 2)
        return false;
    *str = *pos + 4;
    *len = 0;
    while (*len < n && rd16(d + *str + 2 * *len))
        (*len)++;
    uint64_t next = (uint64_t)*pos + 4 + ((2ull * n + 7) & ~7ull);
    *pos = next > size ? size : (uint32_t)next;
    return true;
}

static bool vmap_eq(const uint8_t *d, uint32_t str, uint32_t len, const char *s)
{
    if (!s)
        return true;
    for (uint32_t i = 0; i < len; i++)
        if (!s[i] || lower(rd16(d + str + 2 * i)) != lower((unsigned char)s[i]))
            return false;
    return s[len] == 0;
}

static bool select_variant(MimeMsstyles *ms, const Pe *pe, const char *color, const char *size_name)
{
    const uint8_t *vm;
    uint32_t vm_size, pos = 0;
    if (!pe_resource(pe, "VMAP", 0, "VMAP", 0, &vm, &vm_size)) {
        /* no map: take the NORMAL variant. */
        return pe_resource(pe, "VARIANT", 0, "NORMAL", 0, &ms->variant, &ms->variant_size);
    }
    while (pos < vm_size) {
        uint32_t s[3], l[3];
        int i;
        for (i = 0; i < 3; i++)
            if (!vmap_str(vm, vm_size, &pos, &s[i], &l[i]))
                break;
        if (i < 3)
            return false;
        if (!vmap_eq(vm, s[1], l[1], size_name) || !vmap_eq(vm, s[2], l[2], color))
            continue;
        char name[64];
        if (l[0] >= sizeof(name))
            return false;
        for (uint32_t k = 0; k < l[0]; k++) {
            unsigned c = rd16(vm + s[0] + 2 * k);
            if (c == 0 || c > 0x7e)
                return false;
            name[k] = (char)c;
        }
        name[l[0]] = 0;
        return pe_resource(pe, "VARIANT", 0, name, 0, &ms->variant, &ms->variant_size);
    }
    return false;
}

/* records. */

static uint32_t rec_step(const uint8_t *p, uint32_t remain, bool *ok)
{
    uint32_t res = rd32(p + 20), size = rd32(p + 28);
    uint64_t data = res ? 0 : size;
    *ok = data <= remain - REC_HEADER;
    return (uint32_t)((REC_HEADER + data + 7) & ~7ull);
}

static int rec_cmp(const MimeMsstylesRec *a, const MimeMsstylesRec *b)
{
    if (a->cls != b->cls)
        return a->cls < b->cls ? -1 : 1;
    if (a->part != b->part)
        return a->part < b->part ? -1 : 1;
    if (a->state != b->state)
        return a->state < b->state ? -1 : 1;
    return a->off < b->off ? -1 : a->off > b->off;
}

static bool sort_recs(MimeMsstyles *ms)
{
    uint32_t n = ms->rec_count;
    bool sorted = true;
    for (uint32_t i = 1; i < n && sorted; i++)
        sorted = rec_cmp(&ms->recs[i - 1], &ms->recs[i]) <= 0;
    if (sorted)
        return true;
    MimeMsstylesRec *tmp = mime_alloc(ms->alloc, n * sizeof(*tmp));
    if (!tmp)
        return false;
    MimeMsstylesRec *src = ms->recs, *dst = tmp;
    for (uint32_t width = 1; width < n; width *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * width) {
            uint32_t mid = lo + width < n ? lo + width : n;
            uint32_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            uint32_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi)
                dst[k++] = rec_cmp(&src[j], &src[i]) < 0 ? src[j++] : src[i++];
            while (i < mid)
                dst[k++] = src[i++];
            while (j < hi)
                dst[k++] = src[j++];
        }
        MimeMsstylesRec *t = src;
        src = dst;
        dst = t;
        if (width > n / 2)
            break; /* the next doubling could overflow. */
    }
    if (src != ms->recs) {
        __builtin_memcpy(ms->recs, src, n * sizeof(*tmp));
    }
    mime_free(ms->alloc, tmp);
    return true;
}

static bool parse_records(MimeMsstyles *ms)
{
    const uint8_t *v = ms->variant;
    uint32_t size = ms->variant_size, count = 0;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t pos = 0, n = 0;
        while (size - pos >= REC_HEADER) {
            bool ok;
            uint32_t step = rec_step(v + pos, size - pos, &ok);
            if (!ok)
                break;
            int32_t cls = (int32_t)rd32(v + pos + 8);
            if (cls >= 0 && (uint32_t)cls < ms->class_count) {
                if (pass) {
                    MimeMsstylesRec *r = &ms->recs[n];
                    r->off = pos;
                    r->prop = (int32_t)rd32(v + pos);
                    r->type = (int32_t)rd32(v + pos + 4);
                    r->cls = cls;
                    r->part = (int32_t)rd32(v + pos + 12);
                    r->state = (int32_t)rd32(v + pos + 16);
                    r->res = (int32_t)rd32(v + pos + 20);
                    r->size = rd32(v + pos + 28);
                }
                n++;
            }
            if (step >= size - pos)
                break;
            pos += step;
        }
        if (!pass) {
            count = n;
            ms->recs = mime_alloc(ms->alloc, (count ? count : 1) * sizeof(MimeMsstylesRec));
            if (!ms->recs)
                return false;
        }
    }
    ms->rec_count = count;
    if (!sort_recs(ms))
        return false;
    for (uint32_t c = 0; c < ms->class_count; c++)
        ms->first[c] = ms->end[c] = 0;
    for (uint32_t i = 0; i < count; i++) {
        int32_t c = ms->recs[i].cls;
        if (ms->first[c] == ms->end[c])
            ms->first[c] = i;
        ms->end[c] = i + 1;
    }
    return true;
}

/*
 * BCMAP indexes the loader's class list, built in the order classes first
 * appear in the variant, with globals and sysmetrics forced first.
 */
static bool parse_bcmap(MimeMsstyles *ms, const Pe *pe)
{
    for (uint32_t c = 0; c < ms->class_count; c++)
        ms->base[c] = -1;
    const uint8_t *bc;
    uint32_t bc_size;
    if (!pe_resource(pe, "BCMAP", 0, "BCMAP", 0, &bc, &bc_size) || bc_size < 4)
        return true;
    uint32_t entries = rd32(bc);
    if (entries > (bc_size - 4) / 4)
        entries = (bc_size - 4) / 4;
    int32_t *live = mime_alloc(ms->alloc, ms->class_count * sizeof(int32_t));
    int32_t *of = mime_alloc(ms->alloc, ms->class_count * sizeof(int32_t));
    if (!live || !of) {
        mime_free(ms->alloc, live);
        mime_free(ms->alloc, of);
        return false;
    }
    uint32_t nlive = 0;
    for (uint32_t c = 0; c < ms->class_count; c++)
        of[c] = -1;
    int32_t prev = -1;
    for (uint32_t i = 0; i < ms->rec_count + 2; i++) {
        int32_t c;
        if (i < 2)
            c = i == 0 ? ms->globals : ms->sysmetrics;
        else
            c = (int32_t)rd32(ms->variant + ms->recs[i - 2].off + 8);
        if (c < 0 || c == prev)
            continue;
        prev = c;
        if (of[c] >= 0)
            continue;
        for (uint32_t k = 0; k < nlive; k++)
            if (full_name_eq(ms, live[k], c))
                of[c] = (int32_t)k;
        if (of[c] < 0) {
            of[c] = (int32_t)nlive;
            live[nlive++] = c;
        }
    }
    for (uint32_t c = 0; c < ms->class_count; c++) {
        int32_t l = of[c];
        if (l < 0 || (uint32_t)l >= entries)
            continue;
        int32_t b = (int32_t)rd32(bc + 4 + 4 * (uint32_t)l);
        if (b >= 0 && (uint32_t)b < nlive && live[b] != (int32_t)c)
            ms->base[c] = live[b];
    }
    mime_free(ms->alloc, live);
    mime_free(ms->alloc, of);
    return true;
}

void mime_msstyles_close(MimeMsstyles *ms)
{
    if (!ms || !ms->alloc)
        return;
    mime_free(ms->alloc, ms->class_name);
    mime_free(ms->alloc, ms->class_len);
    mime_free(ms->alloc, ms->class_sep);
    mime_free(ms->alloc, ms->base);
    mime_free(ms->alloc, ms->first);
    mime_free(ms->alloc, ms->end);
    mime_free(ms->alloc, ms->recs);
    __builtin_memset(ms, 0, sizeof(*ms));
}

bool mime_msstyles_open_variant(MimeMsstyles *ms, const void *file, size_t size, const void *mui, size_t mui_size,
                                const char *color, const char *size_name, const MimeAllocator *alloc)
{
    if (!ms)
        return false;
    __builtin_memset(ms, 0, sizeof(*ms));
    if (!alloc || size > 0xffffffffu || mui_size > 0xffffffffu)
        return false;
    ms->file = file;
    ms->file_size = size;
    ms->mui = mui;
    ms->mui_size = mui ? mui_size : 0;
    ms->alloc = alloc;
    ms->globals = ms->sysmetrics = -1;
    Pe pe;
    const uint8_t *cmap;
    uint32_t cmap_size;
    if (!pe_open(&pe, file, size) || !pe_resource(&pe, "CMAP", 0, "CMAP", 0, &cmap, &cmap_size) ||
        !parse_cmap(ms, cmap, cmap_size) || !select_variant(ms, &pe, color, size_name)) {
        mime_msstyles_close(ms);
        return false;
    }
    if (!pe_resource(&pe, "RMAP", 0, "RMAP", 0, &ms->rmap, &ms->rmap_size))
        ms->rmap_size = 0;
    ms->globals = find_plain(ms, "globals");
    ms->sysmetrics = find_plain(ms, "sysmetrics");
    if (!parse_records(ms) || !parse_bcmap(ms, &pe)) {
        mime_msstyles_close(ms);
        return false;
    }
    return true;
}

bool mime_msstyles_open(MimeMsstyles *ms, const void *file, size_t size, const void *mui, size_t mui_size,
                        const MimeAllocator *alloc)
{
    return mime_msstyles_open_variant(ms, file, size, mui, mui_size, nullptr, nullptr, alloc);
}

uint32_t mime_msstyles_class_count(const MimeMsstyles *ms)
{
    return ms ? ms->class_count : 0;
}

size_t mime_msstyles_class_name(const MimeMsstyles *ms, int cls, char *buf, size_t cap)
{
    if (!ms || cls < 0 || (uint32_t)cls >= ms->class_count) {
        if (cap)
            buf[0] = 0;
        return 0;
    }
    uint32_t len = ms->class_len[cls];
    size_t n = 0;
    for (; n < len && n + 1 < cap; n++) {
        unsigned c = name_char(ms, cls, (uint32_t)n);
        buf[n] = c >= 0x20 && c < 0x7f ? (char)c : '?';
    }
    if (cap)
        buf[n] = 0;
    return len;
}

static size_t widen(const char *s, uint16_t *w, size_t cap)
{
    size_t n = 0;
    if (s)
        while (s[n] && n + 1 < cap) {
            w[n] = (unsigned char)s[n];
            n++;
        }
    w[n] = 0;
    return n;
}

int mime_msstyles_find_class(const MimeMsstyles *ms, const char *app, const char *cls)
{
    uint16_t wa[MAX_NAME], wc[MAX_NAME];
    if (!ms || !cls)
        return -1;
    size_t na = widen(app, wa, MAX_NAME), nc = widen(cls, wc, MAX_NAME);
    for (uint32_t c = 0; c < ms->class_count; c++)
        if (class_eq(ms, (int)c, wa, na, wc, nc))
            return (int)c;
    return -1;
}

/* tries the app specific entry first, then with fallback the plain class. */
static int match_app(const MimeMsstyles *ms, const uint16_t *app, size_t app_n, const uint16_t *cls, size_t cls_n,
                     bool fallback)
{
    if (app_n)
        for (uint32_t c = 0; c < ms->class_count; c++)
            if (class_eq(ms, (int)c, app, app_n, cls, cls_n))
                return (int)c;
    if (!app_n || fallback)
        for (uint32_t c = 0; c < ms->class_count; c++)
            if (class_eq(ms, (int)c, nullptr, 0, cls, cls_n))
                return (int)c;
    return -1;
}

static size_t cat(uint16_t *dst, size_t n, const char *s, const uint16_t *w, size_t wn)
{
    while (s && *s && n + 1 < MAX_NAME)
        dst[n++] = (unsigned char)*s++;
    for (size_t i = 0; w && i < wn && n + 1 < MAX_NAME; i++)
        dst[n++] = w[i];
    dst[n] = 0;
    return n;
}

static int match_dark(const MimeMsstyles *ms, const uint16_t *app, size_t app_n, const uint16_t *cls, size_t cls_n)
{
    uint16_t dark[MAX_NAME];
    size_t n = cat(dark, 0, "DarkMode", nullptr, 0);
    if (app_n)
        n = cat(dark, n, "_", app, app_n);
    int r = match_app(ms, dark, n, cls, cls_n, false);
    if (r >= 0)
        return r;
    n = cat(dark, 0, "DarkMode_DarkTheme", nullptr, 0);
    if (app_n)
        n = cat(dark, n, "_", app, app_n);
    r = match_app(ms, dark, n, cls, cls_n, false);
    if (r < 0 && app_n) {
        n = cat(dark, 0, "DarkMode_DarkTheme", nullptr, 0);
        r = match_app(ms, dark, n, cls, cls_n, false);
    }
    return r;
}

int mime_msstyles_match_class(const MimeMsstyles *ms, const uint16_t *class_list, const uint16_t *app, unsigned flags)
{
    if (!ms || !class_list)
        return -1;
    size_t app_n = 0;
    while (app && app[app_n] && app_n < MAX_NAME)
        app_n++;
    size_t pos = 0;
    for (;;) {
        size_t start = pos;
        while (class_list[pos] && class_list[pos] != ';')
            pos++;
        const uint16_t *cls = class_list + start, *eapp = app;
        size_t cls_n = pos - start, eapp_n = app_n;
        if (!app_n) {
            /* an "App::" prefix in the entry is used when the window has no app name. */
            for (size_t i = 0; i + 1 < cls_n; i++)
                if (cls[i] == ':' && cls[i + 1] == ':') {
                    eapp = cls;
                    eapp_n = i;
                    cls += i + 2;
                    cls_n -= i + 2;
                    break;
                }
        }
        int r = -1;
        if (flags & MIME_MSSTYLES_DARK)
            r = match_dark(ms, eapp, eapp_n, cls, cls_n);
        if (r < 0)
            r = match_app(ms, eapp, eapp_n, cls, cls_n, true);
        if (r >= 0)
            return r;
        if (!class_list[pos])
            return -1;
        pos++;
    }
}

int mime_msstyles_match_class_a(const MimeMsstyles *ms, const char *class_list, const char *app, unsigned flags)
{
    uint16_t wl[512], wa[MAX_NAME];
    if (!class_list)
        return -1;
    widen(class_list, wl, 512);
    widen(app, wa, MAX_NAME);
    return mime_msstyles_match_class(ms, wl, app ? wa : nullptr, flags);
}

int mime_msstyles_base_class(const MimeMsstyles *ms, int cls)
{
    if (!ms || cls < 0 || (uint32_t)cls >= ms->class_count)
        return -1;
    return ms->base[cls];
}

/* record access and lookup. */

static void fill_prop(const MimeMsstyles *ms, const MimeMsstylesRec *r, MimeMsstylesProp *out)
{
    if (!out)
        return;
    out->prop = r->prop;
    out->type = r->type;
    out->cls = r->cls;
    out->part = r->part;
    out->state = r->state;
    out->res_id = r->res;
    out->size = r->size;
    out->data = r->res ? nullptr : ms->variant + r->off + REC_HEADER;
}

uint32_t mime_msstyles_record_count(const MimeMsstyles *ms)
{
    return ms ? ms->rec_count : 0;
}

bool mime_msstyles_record(const MimeMsstyles *ms, uint32_t index, MimeMsstylesProp *out)
{
    if (!ms || !ms->variant)
        return false;
    /* file order: walk the blob again, records are few enough for dumps. */
    uint32_t pos = 0, n = 0, size = ms->variant_size;
    while (size - pos >= REC_HEADER) {
        bool ok;
        uint32_t step = rec_step(ms->variant + pos, size - pos, &ok);
        if (!ok)
            return false;
        int32_t cls = (int32_t)rd32(ms->variant + pos + 8);
        if (cls >= 0 && (uint32_t)cls < ms->class_count) {
            if (n == index) {
                MimeMsstylesRec r = {pos,
                                     (int32_t)rd32(ms->variant + pos),
                                     (int32_t)rd32(ms->variant + pos + 4),
                                     cls,
                                     (int32_t)rd32(ms->variant + pos + 12),
                                     (int32_t)rd32(ms->variant + pos + 16),
                                     (int32_t)rd32(ms->variant + pos + 20),
                                     rd32(ms->variant + pos + 28)};
                fill_prop(ms, &r, out);
                return true;
            }
            n++;
        }
        if (step >= size - pos)
            return false;
        pos += step;
    }
    return false;
}

bool mime_msstyles_class_record(const MimeMsstyles *ms, int cls, uint32_t index, MimeMsstylesProp *out)
{
    if (!ms || cls < 0 || (uint32_t)cls >= ms->class_count)
        return false;
    if (index >= ms->end[cls] - ms->first[cls])
        return false;
    fill_prop(ms, &ms->recs[ms->first[cls] + index], out);
    return true;
}

/* first record of the class at or after part, state. */
static uint32_t lower_bound(const MimeMsstyles *ms, int cls, int part, int state)
{
    uint32_t lo = ms->first[cls], hi = ms->end[cls];
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const MimeMsstylesRec *r = &ms->recs[mid];
        if (r->part < part || (r->part == part && r->state < state))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static bool has_part(const MimeMsstyles *ms, int cls, int part)
{
    uint32_t i = lower_bound(ms, cls, part, (int32_t)0x80000000);
    return i < ms->end[cls] && ms->recs[i].part == part;
}

static bool has_state(const MimeMsstyles *ms, int cls, int part, int state)
{
    uint32_t i = lower_bound(ms, cls, part, state);
    return i < ms->end[cls] && ms->recs[i].part == part && ms->recs[i].state == state;
}

static const MimeMsstylesRec *block_find(const MimeMsstyles *ms, int cls, int part, int state, int prop)
{
    for (uint32_t i = lower_bound(ms, cls, part, state); i < ms->end[cls]; i++) {
        const MimeMsstylesRec *r = &ms->recs[i];
        if (r->part != part || r->state != state)
            break;
        if (r->prop == prop)
            return r;
    }
    return nullptr;
}

/* a part section: the state block, then the part level block. */
static const MimeMsstylesRec *part_find(const MimeMsstyles *ms, int cls, int part, int state, int prop)
{
    int s = state > 0 && has_state(ms, cls, part, state) ? state : 0;
    const MimeMsstylesRec *r = block_find(ms, cls, part, s, prop);
    if (!r && s)
        r = block_find(ms, cls, part, 0, prop);
    return r;
}

static const MimeMsstylesRec *lookup(const MimeMsstyles *ms, int cls, int part, int state, int prop)
{
    if (!ms || cls < 0 || (uint32_t)cls >= ms->class_count)
        return nullptr;
    int c = cls;
    for (uint32_t guard = 0; c >= 0 && guard <= ms->class_count; guard++) {
        int p = part > 0 && has_part(ms, c, part) ? part : 0;
        const MimeMsstylesRec *r = part_find(ms, c, p, state, prop);
        if (!r && p)
            r = part_find(ms, c, 0, state, prop);
        if (r)
            return r;
        if (c == ms->globals)
            break;
        c = ms->base[c] >= 0 ? ms->base[c] : ms->globals;
    }
    return nullptr;
}

bool mime_msstyles_find_prop(const MimeMsstyles *ms, int cls, int part, int state, int prop, MimeMsstylesProp *out)
{
    const MimeMsstylesRec *r = lookup(ms, cls, part, state, prop);
    if (r)
        fill_prop(ms, r, out);
    return r != nullptr;
}

bool mime_msstyles_find_prop_exact(const MimeMsstyles *ms, int cls, int part, int state, int prop,
                                   MimeMsstylesProp *out)
{
    if (!ms || cls < 0 || (uint32_t)cls >= ms->class_count)
        return false;
    const MimeMsstylesRec *r = block_find(ms, cls, part, state, prop);
    if (r)
        fill_prop(ms, r, out);
    return r != nullptr;
}

/* value decoding. */

static bool is_space(unsigned c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* parses up to cap integers separated by commas or spaces. */
static uint32_t parse_ints(const uint16_t *s, int32_t *out, uint32_t cap)
{
    uint32_t n = 0;
    size_t i = 0;
    while (s[i] && n < cap) {
        while (is_space(s[i]) || s[i] == ',')
            i++;
        if (!s[i])
            break;
        bool neg = false;
        if (s[i] == '-' || s[i] == '+')
            neg = s[i++] == '-';
        uint32_t base = 10, v = 0, digits = 0;
        if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
            base = 16;
            i += 2;
        }
        for (;; i++) {
            unsigned c = s[i], d;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (base == 16 && lower(c) >= 'a' && lower(c) <= 'f')
                d = lower(c) - 'a' + 10;
            else
                break;
            v = v * base + d;
            digits++;
        }
        if (!digits)
            break;
        out[n++] = neg ? -(int32_t)v : (int32_t)v;
        while (s[i] && !is_space(s[i]) && s[i] != ',')
            i++;
    }
    return n;
}

static bool str_ieq(const uint16_t *s, size_t n, const char *t)
{
    size_t i = 0;
    for (; i < n; i++)
        if (!t[i] || lower(s[i]) != lower((unsigned char)t[i]))
            return false;
    return t[i] == 0;
}

static bool res_string(const MimeMsstyles *ms, const MimeMsstylesProp *p, uint16_t *buf, size_t cap)
{
    return p->res_id > 0 && mime_msstyles_load_string(ms, (uint32_t)p->res_id, buf, cap) > 0;
}

bool mime_msstyles_prop_ints(const MimeMsstyles *ms, const MimeMsstylesProp *p, int32_t *out, uint32_t cap,
                             uint32_t *count)
{
    uint32_t n = 0;
    if (!ms || !p)
        return false;
    if (p->data) {
        uint32_t avail = p->size / 4, first = 0;
        if (p->type == MIME_TMT_INTLIST) {
            if (!avail)
                return false;
            uint32_t declared = rd32(p->data);
            first = 1;
            avail = declared < avail - 1 ? declared : avail - 1;
        }
        for (; n < avail && n < cap; n++)
            out[n] = (int32_t)rd32(p->data + 4 * (first + n));
    } else {
        uint16_t s[256];
        if (!res_string(ms, p, s, 256))
            return false;
        if (p->type == MIME_TMT_BOOL) {
            size_t len = 0;
            while (s[len])
                len++;
            while (len && is_space(s[len - 1]))
                len--;
            if (!cap)
                return false;
            if (str_ieq(s, len, "true") || str_ieq(s, len, "1"))
                out[0] = 1;
            else if (str_ieq(s, len, "false") || str_ieq(s, len, "0"))
                out[0] = 0;
            else
                return false;
            n = 1;
        } else {
            n = parse_ints(s, out, cap);
        }
    }
    if (count)
        *count = n;
    return n > 0;
}

bool mime_msstyles_prop_int(const MimeMsstyles *ms, const MimeMsstylesProp *p, int32_t *out)
{
    int32_t v[3];
    uint32_t n;
    if (!p || !out)
        return false;
    if (p->type == MIME_TMT_COLOR && !p->data) {
        if (!mime_msstyles_prop_ints(ms, p, v, 3, &n) || n < 3)
            return false;
        *out = (int32_t)((uint32_t)(v[0] & 0xff) | ((uint32_t)(v[1] & 0xff) << 8) | ((uint32_t)(v[2] & 0xff) << 16));
        return true;
    }
    if (!mime_msstyles_prop_ints(ms, p, v, 1, &n))
        return false;
    *out = v[0];
    return true;
}

bool mime_msstyles_prop_string(const MimeMsstyles *ms, const MimeMsstylesProp *p, uint16_t *buf, size_t cap)
{
    if (!ms || !p || !cap)
        return false;
    buf[0] = 0;
    if (!p->data)
        return res_string(ms, p, buf, cap);
    size_t n = 0, chars = p->size / 2;
    while (n < chars && n + 1 < cap && rd16(p->data + 2 * n)) {
        buf[n] = rd16(p->data + 2 * n);
        n++;
    }
    buf[n] = 0;
    return true;
}

/* "Face, points[, bold][, italic][, underline][, strikeout][, quality:...]" as in the mui string tables. */
static bool parse_font(const uint16_t *s, MimeMsstylesFont *f)
{
    __builtin_memset(f, 0, sizeof(*f));
    f->weight = 400;
    f->charset = 1;
    size_t i = 0;
    while (is_space(s[i]))
        i++;
    size_t start = i;
    while (s[i] && s[i] != ',')
        i++;
    size_t end = i;
    while (end > start && is_space(s[end - 1]))
        end--;
    if (end == start || s[i] != ',')
        return false;
    for (size_t k = 0; k < end - start && k < 31; k++)
        f->face[k] = s[start + k];
    i++;
    int32_t pt;
    while (is_space(s[i]))
        i++;
    size_t num = i;
    while (s[i] && s[i] != ',')
        i++;
    uint16_t tmp[16];
    size_t tl = i - num < 15 ? i - num : 15;
    for (size_t k = 0; k < tl; k++)
        tmp[k] = s[num + k];
    tmp[tl] = 0;
    if (parse_ints(tmp, &pt, 1) != 1)
        return false;
    f->height = -muldiv(pt, 96, 72);
    while (s[i]) {
        while (s[i] == ',' || is_space(s[i]))
            i++;
        size_t t = i;
        while (s[i] && s[i] != ',' && !is_space(s[i]))
            i++;
        size_t n = i - t;
        if (!n)
            break;
        const uint16_t *w = s + t;
        if (str_ieq(w, n, "bold"))
            f->weight = 700;
        else if (str_ieq(w, n, "italic"))
            f->italic = 1;
        else if (str_ieq(w, n, "underline"))
            f->underline = 1;
        else if (str_ieq(w, n, "strikeout"))
            f->strikeout = 1;
        else if (str_ieq(w, n, "quality:cleartype"))
            f->quality = 5;
        else if (str_ieq(w, n, "quality:cleartype-natural"))
            f->quality = 6;
        else if (str_ieq(w, n, "quality:antialiased"))
            f->quality = 4;
        else if (str_ieq(w, n, "quality:nonantialiased"))
            f->quality = 3;
    }
    return true;
}

bool mime_msstyles_prop_font(const MimeMsstyles *ms, const MimeMsstylesProp *p, MimeMsstylesFont *out)
{
    if (!ms || !p || !out)
        return false;
    if (p->data) {
        if (p->size < sizeof(*out))
            return false;
        const uint8_t *d = p->data;
        out->height = (int32_t)rd32(d);
        out->width = (int32_t)rd32(d + 4);
        out->escapement = (int32_t)rd32(d + 8);
        out->orientation = (int32_t)rd32(d + 12);
        out->weight = (int32_t)rd32(d + 16);
        out->italic = d[20];
        out->underline = d[21];
        out->strikeout = d[22];
        out->charset = d[23];
        out->out_precision = d[24];
        out->clip_precision = d[25];
        out->quality = d[26];
        out->pitch_and_family = d[27];
        for (int i = 0; i < 32; i++)
            out->face[i] = rd16(d + 28 + 2 * i);
        out->face[31] = 0;
        return true;
    }
    uint16_t s[256];
    return res_string(ms, p, s, 256) && parse_font(s, out);
}

/* typed getters. */

bool mime_msstyles_get_color(const MimeMsstyles *ms, int cls, int part, int state, int prop, uint32_t *out)
{
    MimeMsstylesProp p;
    int32_t v;
    if (!mime_msstyles_find_prop(ms, cls, part, state, prop, &p) || !mime_msstyles_prop_int(ms, &p, &v))
        return false;
    *out = (uint32_t)v & 0x00ffffffu;
    return true;
}

bool mime_msstyles_get_int(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *out)
{
    MimeMsstylesProp p;
    return mime_msstyles_find_prop(ms, cls, part, state, prop, &p) && mime_msstyles_prop_int(ms, &p, out);
}

bool mime_msstyles_get_enum(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *out)
{
    return mime_msstyles_get_int(ms, cls, part, state, prop, out);
}

bool mime_msstyles_get_bool(const MimeMsstyles *ms, int cls, int part, int state, int prop, bool *out)
{
    int32_t v;
    if (!mime_msstyles_get_int(ms, cls, part, state, prop, &v))
        return false;
    *out = v != 0;
    return true;
}

static bool get_n(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *v, uint32_t n)
{
    MimeMsstylesProp p;
    uint32_t got;
    return mime_msstyles_find_prop(ms, cls, part, state, prop, &p) && mime_msstyles_prop_ints(ms, &p, v, n, &got) &&
           got == n;
}

bool mime_msstyles_get_margins(const MimeMsstyles *ms, int cls, int part, int state, int prop,
                               MimeMsstylesMargins *out)
{
    int32_t v[4];
    if (!get_n(ms, cls, part, state, prop, v, 4))
        return false;
    out->left = v[0];
    out->right = v[1];
    out->top = v[2];
    out->bottom = v[3];
    return true;
}

bool mime_msstyles_get_rect(const MimeMsstyles *ms, int cls, int part, int state, int prop, MimeMsstylesRect *out)
{
    int32_t v[4];
    if (!get_n(ms, cls, part, state, prop, v, 4))
        return false;
    out->left = v[0];
    out->top = v[1];
    out->right = v[2];
    out->bottom = v[3];
    return true;
}

bool mime_msstyles_get_position(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *x,
                                int32_t *y)
{
    int32_t v[2];
    if (!get_n(ms, cls, part, state, prop, v, 2))
        return false;
    *x = v[0];
    *y = v[1];
    return true;
}

bool mime_msstyles_get_intlist(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *values,
                               uint32_t cap, uint32_t *count)
{
    MimeMsstylesProp p;
    return mime_msstyles_find_prop(ms, cls, part, state, prop, &p) &&
           mime_msstyles_prop_ints(ms, &p, values, cap, count);
}

bool mime_msstyles_get_string(const MimeMsstyles *ms, int cls, int part, int state, int prop, uint16_t *buf,
                              size_t cap)
{
    MimeMsstylesProp p;
    return mime_msstyles_find_prop(ms, cls, part, state, prop, &p) && mime_msstyles_prop_string(ms, &p, buf, cap);
}

static void scale_font(MimeMsstylesFont *f, unsigned dpi)
{
    if (dpi && dpi != 96)
        f->height = muldiv(f->height, (int32_t)dpi, 96);
}

bool mime_msstyles_get_font(const MimeMsstyles *ms, int cls, int part, int state, int prop, unsigned dpi,
                            MimeMsstylesFont *out)
{
    MimeMsstylesProp p;
    if (!mime_msstyles_find_prop(ms, cls, part, state, prop, &p) || !mime_msstyles_prop_font(ms, &p, out))
        return false;
    scale_font(out, dpi);
    return true;
}

static bool prop_resource(const MimeMsstyles *ms, const MimeMsstylesProp *p, const char *type, const uint8_t **data,
                          uint32_t *size)
{
    if (p->res_id <= 0 || p->res_id > 0xffff)
        return false;
    return mime_msstyles_resource(ms, type, 0, nullptr, (uint32_t)p->res_id, data, size);
}

bool mime_msstyles_get_filename(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *res_id,
                                const uint8_t **data, uint32_t *size)
{
    MimeMsstylesProp p;
    if (!mime_msstyles_find_prop(ms, cls, part, state, prop, &p) || !prop_resource(ms, &p, "IMAGE", data, size))
        return false;
    if (res_id)
        *res_id = p.res_id;
    return true;
}

bool mime_msstyles_get_stream(const MimeMsstyles *ms, int cls, int part, int state, int prop, const uint8_t **data,
                              uint32_t *size)
{
    MimeMsstylesProp p;
    return mime_msstyles_find_prop(ms, cls, part, state, prop, &p) && prop_resource(ms, &p, "STREAM", data, size);
}

/* sysmetrics and documentation. */

bool mime_msstyles_sys_color(const MimeMsstyles *ms, int color_index, uint32_t *out)
{
    MimeMsstylesProp p;
    int32_t v;
    if (!ms || color_index < 0 || color_index > MIME_TMT_LASTCOLOR - MIME_TMT_FIRSTCOLOR)
        return false;
    if (!mime_msstyles_find_prop_exact(ms, ms->sysmetrics, 0, 0, MIME_TMT_FIRSTCOLOR + color_index, &p) ||
        !mime_msstyles_prop_int(ms, &p, &v))
        return false;
    *out = (uint32_t)v & 0x00ffffffu;
    return true;
}

bool mime_msstyles_sys_font(const MimeMsstyles *ms, int prop, unsigned dpi, MimeMsstylesFont *out)
{
    MimeMsstylesProp p;
    if (!ms || !mime_msstyles_find_prop_exact(ms, ms->sysmetrics, 0, 0, prop, &p) ||
        !mime_msstyles_prop_font(ms, &p, out))
        return false;
    scale_font(out, dpi);
    return true;
}

bool mime_msstyles_sys_int(const MimeMsstyles *ms, int prop, int32_t *out)
{
    MimeMsstylesProp p;
    return ms && mime_msstyles_find_prop_exact(ms, ms->sysmetrics, 0, 0, prop, &p) &&
           mime_msstyles_prop_int(ms, &p, out);
}

bool mime_msstyles_sys_string(const MimeMsstyles *ms, int prop, uint16_t *buf, size_t cap)
{
    MimeMsstylesProp p;
    return ms && mime_msstyles_find_prop_exact(ms, ms->sysmetrics, 0, 0, prop, &p) &&
           mime_msstyles_prop_string(ms, &p, buf, cap);
}

bool mime_msstyles_doc_string(const MimeMsstyles *ms, int prop, uint16_t *buf, size_t cap)
{
    if (!ms || !ms->rmap)
        return false;
    uint32_t pos = 0, size = ms->rmap_size;
    while (size - pos >= REC_HEADER) {
        const uint8_t *r = ms->rmap + pos;
        bool ok;
        uint32_t step = rec_step(r, size - pos, &ok);
        if (!ok)
            return false;
        if ((int32_t)rd32(r) == prop && rd32(r + 8) == 0) {
            MimeMsstylesProp p = {prop,
                                  (int32_t)rd32(r + 4),
                                  0,
                                  0,
                                  0,
                                  (int32_t)rd32(r + 20),
                                  rd32(r + 28),
                                  rd32(r + 20) ? nullptr : r + REC_HEADER};
            return mime_msstyles_prop_string(ms, &p, buf, cap);
        }
        if (step >= size - pos)
            break;
        pos += step;
    }
    return false;
}

/* images. */

static constexpr int32_t imagefile_ids[7] = {3002, 3003, 3004, 3005, 3006, 3009, 3010};
static constexpr int32_t mindpi_ids[7] = {2420, 2421, 2422, 2423, 2424, 2433, 2434};
static constexpr int32_t minsize_ids[7] = {3404, 3405, 3406, 3407, 3408, 3410, 3411};
static constexpr int32_t composed_ids[7] = {5144, 5145, 5146, 5147, 5148, 5152, 5153};

#define TMT_COMPOSEDIMAGEFILE 5143
#define TMT_COMPOSEDGLYPHIMAGEFILE 5149

static int32_t get_or(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t def)
{
    int32_t v;
    return mime_msstyles_get_int(ms, cls, part, state, prop, &v) ? v : def;
}

static bool image_header(const uint8_t *d, uint32_t n, uint32_t *format, int32_t *w, int32_t *h)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (n >= 24 && !__builtin_memcmp(d, sig, 8) && !__builtin_memcmp(d + 12, "IHDR", 4)) {
        uint32_t ww = be32(d + 16), hh = be32(d + 20);
        if (!ww || !hh || ww > 0x7fffffffu || hh > 0x7fffffffu)
            return false;
        *format = MIME_MSSTYLES_FORMAT_PNG;
        *w = (int32_t)ww;
        *h = (int32_t)hh;
        return true;
    }
    if (n >= 30 && d[0] == 'B' && d[1] == 'M') {
        int32_t ww = (int32_t)rd32(d + 18), hh = (int32_t)rd32(d + 22);
        uint32_t bpp = rd16(d + 28);
        if (ww <= 0 || hh == 0 || hh == (int32_t)0x80000000 || (bpp != 24 && bpp != 32))
            return false;
        *format = MIME_MSSTYLES_FORMAT_BMP;
        *w = ww;
        *h = hh < 0 ? -hh : hh;
        return true;
    }
    return false;
}

typedef struct Cand {
    const MimeMsstylesRec *file;
    int32_t min_dpi;
    bool has_dpi;
} Cand;

bool mime_msstyles_get_image(const MimeMsstyles *ms, int cls, int part, int state, unsigned dpi, int dest_w,
                             int dest_h, unsigned flags, MimeMsstylesImage *out)
{
    if (!ms || !out || cls < 0 || (uint32_t)cls >= ms->class_count)
        return false;
    __builtin_memset(out, 0, sizeof(*out));
    if (!dpi)
        dpi = 96;
    bool glyph = (flags & MIME_MSSTYLES_GLYPH) != 0;
    int32_t count = get_or(ms, cls, part, state, MIME_TMT_IMAGECOUNT, 1);
    out->image_count = count < 1 ? 1 : count;
    out->layout = get_or(ms, cls, part, state, MIME_TMT_IMAGELAYOUT, MIME_IL_HORIZONTAL);
    out->glyph_type = get_or(ms, cls, part, state, MIME_TMT_GLYPHTYPE, MIME_GT_NONE);
    out->image_select_type = get_or(ms, cls, part, state, MIME_TMT_IMAGESELECTTYPE, MIME_IST_NONE);

    /* multi images belong to the glyph when GLYPHTYPE is IMAGEGLYPH, else to the background. */
    Cand kept[7];
    int nk = 0;
    bool multi_here = out->image_select_type != MIME_IST_NONE &&
                      (glyph ? out->glyph_type == MIME_GT_IMAGEGLYPH : out->glyph_type != MIME_GT_IMAGEGLYPH);
    if (multi_here) {
        const MimeMsstylesRec *files[7];
        int32_t dpis[7];
        bool has[7];
        int best = -1;
        int32_t best_d = 0x7fffffff;
        for (int o = 0; o < 7; o++) {
            files[o] = lookup(ms, cls, part, state, imagefile_ids[o]);
            if (!files[o])
                files[o] = lookup(ms, cls, part, state, composed_ids[o]);
            MimeMsstylesProp p;
            const MimeMsstylesRec *r = lookup(ms, cls, part, state, mindpi_ids[o]);
            has[o] = false;
            dpis[o] = 0;
            if (r) {
                fill_prop(ms, r, &p);
                has[o] = mime_msstyles_prop_int(ms, &p, &dpis[o]);
            }
            /* the loader keeps the image whose MINDPI is closest below the dpi. */
            int32_t d = (int32_t)dpi - (has[o] ? dpis[o] : 0);
            if (d >= 0 && d < best_d) {
                best = o;
                best_d = d;
            }
        }
        for (int o = 0; o < 7; o++) {
            if (!files[o] || (has[o] && o != best))
                continue;
            kept[nk].file = files[o];
            kept[nk].has_dpi = has[o];
            kept[nk].min_dpi = has[o] ? (dpis[o] < 1 ? 1 : dpis[o]) : 96;
            nk++;
        }
    }

    const MimeMsstylesRec *file = nullptr;
    int sel = -1;
    if (nk > 0) {
        if (out->image_select_type == MIME_IST_DPI) {
            for (int k = nk - 1; k >= 0; k--)
                if (kept[k].min_dpi <= (int32_t)dpi && (sel < 0 || kept[sel].min_dpi < kept[k].min_dpi))
                    sel = k;
        } else if (dest_w > 0 && dest_h > 0) {
            for (int k = nk - 1; k >= 0 && sel < 0; k--) {
                int32_t v[2];
                const MimeMsstylesRec *r = lookup(ms, cls, part, state, minsize_ids[k]);
                MimeMsstylesProp p;
                uint32_t got = 0;
                if (r) {
                    fill_prop(ms, r, &p);
                    mime_msstyles_prop_ints(ms, &p, v, 2, &got);
                }
                if (got < 2) {
                    /* default MINSIZE is the frame size of that image. */
                    const uint8_t *d;
                    uint32_t n, fmt;
                    int32_t w, h;
                    fill_prop(ms, kept[k].file, &p);
                    if (!prop_resource(ms, &p, "IMAGE", &d, &n) || !image_header(d, n, &fmt, &w, &h))
                        continue;
                    bool comp = p.prop != imagefile_ids[0] && p.prop >= TMT_COMPOSEDIMAGEFILE;
                    bool horz = out->layout == MIME_IL_HORIZONTAL && !comp;
                    v[0] = horz ? w / out->image_count : w;
                    v[1] = horz ? h : h / (out->image_count * (comp ? 2 : 1));
                }
                if (v[0] <= dest_w && v[1] <= dest_h)
                    sel = k;
            }
        }
        if (sel < 0)
            sel = 0;
        file = kept[sel].file;
        out->min_dpi = kept[sel].min_dpi;
    } else {
        file = lookup(ms, cls, part, state, glyph ? MIME_TMT_GLYPHIMAGEFILE : MIME_TMT_IMAGEFILE);
        if (!file)
            file = lookup(ms, cls, part, state, glyph ? TMT_COMPOSEDGLYPHIMAGEFILE : TMT_COMPOSEDIMAGEFILE);
        if (glyph && out->glyph_type != MIME_GT_IMAGEGLYPH)
            file = nullptr;
        out->min_dpi = 96;
    }

    MimeMsstylesProp p;
    int32_t sx = 0, sy = 0, sw, sh;
    if (file) {
        fill_prop(ms, file, &p);
        uint32_t n;
        if (!prop_resource(ms, &p, "IMAGE", &out->data, &n))
            return false;
        out->size = n;
        out->res_id = p.res_id;
        out->prop = p.prop;
        if (!image_header(out->data, (uint32_t)out->size, &out->format, &out->width, &out->height))
            return false;
        sw = out->width;
        sh = out->height;
        out->composed = p.prop >= TMT_COMPOSEDIMAGEFILE && p.prop <= composed_ids[6];
    } else {
        /* dwm parts: a rectangle of the class DISKSTREAM atlas. */
        MimeMsstylesRect rc;
        const uint8_t *d;
        uint32_t n;
        if (glyph || !mime_msstyles_get_rect(ms, cls, part, state, MIME_TMT_ATLASRECT, &rc) ||
            !mime_msstyles_find_prop(ms, cls, part, state, MIME_TMT_DISKSTREAM, &p) ||
            !prop_resource(ms, &p, "STREAM", &d, &n))
            return false;
        out->data = d;
        out->size = n;
        out->res_id = p.res_id;
        out->prop = MIME_TMT_ATLASIMAGE;
        out->atlas = true;
        if (!image_header(d, n, &out->format, &out->width, &out->height))
            return false;
        if (rc.left < 0 || rc.top < 0 || rc.right <= rc.left || rc.bottom <= rc.top || rc.right > out->width ||
            rc.bottom > out->height)
            return false;
        sx = rc.left;
        sy = rc.top;
        sw = rc.right - rc.left;
        sh = rc.bottom - rc.top;
    }

    /* dwm cuts atlas strips vertically and drops a 1 pixel gutter around each frame. */
    int32_t gutter = 0;
    if (out->atlas) {
        if (!lookup(ms, cls, part, state, MIME_TMT_IMAGELAYOUT))
            out->layout = MIME_IL_VERTICAL;
        gutter = 1;
    }
    if (out->composed) {
        /* base and overlay sub images alternate vertically, two per frame. */
        out->layout = MIME_IL_VERTICAL;
        out->frame_width = sw;
        out->frame_height = sh / (2 * out->image_count);
    } else if (out->layout == MIME_IL_HORIZONTAL) {
        out->frame_width = sw / out->image_count;
        out->frame_height = sh;
    } else {
        out->frame_width = sw;
        out->frame_height = sh / out->image_count;
    }
    if (out->frame_width <= 0 || out->frame_height <= 0)
        return false;
    out->frame = state >= 1 && state <= out->image_count ? state - 1 : 0;
    if (out->composed)
        sy += 2 * out->frame * out->frame_height;
    else if (out->layout == MIME_IL_HORIZONTAL)
        sx += out->frame * out->frame_width;
    else
        sy += out->frame * out->frame_height;
    out->src.left = sx + gutter;
    out->src.top = sy + gutter;
    out->src.right = sx + out->frame_width - gutter;
    out->src.bottom = sy + out->frame_height - gutter;
    if (out->src.right <= out->src.left || out->src.bottom <= out->src.top)
        return false;
    out->overlay_src = out->src;
    if (out->composed) {
        out->overlay_src.top += out->frame_height;
        out->overlay_src.bottom += out->frame_height;
    }
    out->frame_width -= 2 * gutter;
    out->frame_height -= 2 * gutter;

    out->bg_type = get_or(ms, cls, part, state, MIME_TMT_BGTYPE, MIME_BT_BORDERFILL);
    out->sizing_type = get_or(ms, cls, part, state, MIME_TMT_SIZINGTYPE, MIME_ST_STRETCH);
    out->halign = get_or(ms, cls, part, state, MIME_TMT_HALIGN, MIME_HA_CENTER);
    out->valign = get_or(ms, cls, part, state, MIME_TMT_VALIGN, MIME_VA_CENTER);
    out->truesize_scaling_type = get_or(ms, cls, part, state, MIME_TMT_TRUESIZESCALINGTYPE, MIME_TSST_NONE);
    out->truesize_stretch_mark = get_or(ms, cls, part, state, MIME_TMT_TRUESIZESTRETCHMARK, 0);
    out->border_only = get_or(ms, cls, part, state, MIME_TMT_BORDERONLY, 0) != 0;
    out->transparent = get_or(ms, cls, part, state, MIME_TMT_TRANSPARENT, 0) != 0;
    out->mirror_image = get_or(ms, cls, part, state, MIME_TMT_MIRRORIMAGE, 1) != 0;
    out->uniform_sizing = get_or(ms, cls, part, state, MIME_TMT_UNIFORMSIZING, 0) != 0;
    out->integral_sizing = get_or(ms, cls, part, state, MIME_TMT_INTEGRALSIZING, 0) != 0;
    out->source_grow = get_or(ms, cls, part, state, MIME_TMT_SOURCEGROW, 0) != 0;
    out->source_shrink = get_or(ms, cls, part, state, MIME_TMT_SOURCESHRINK, 0) != 0;
    out->glyph_only = get_or(ms, cls, part, state, MIME_TMT_GLYPHONLY, 0) != 0;
    out->glyph_transparent = get_or(ms, cls, part, state, MIME_TMT_GLYPHTRANSPARENT, 0) != 0;
    out->bg_fill = get_or(ms, cls, part, state, MIME_TMT_BGFILL, 0) != 0;
    if (!mime_msstyles_get_color(ms, cls, part, state, MIME_TMT_FILLCOLOR, &out->fill_color))
        out->fill_color = 0xffffff;
    out->has_transparent_color =
        mime_msstyles_get_color(ms, cls, part, state, MIME_TMT_TRANSPARENTCOLOR, &out->transparent_color);
    if (!mime_msstyles_get_margins(ms, cls, part, state, MIME_TMT_SIZINGMARGINS, &out->sizing_margins))
        __builtin_memset(&out->sizing_margins, 0, sizeof(out->sizing_margins));
    if (!mime_msstyles_get_margins(ms, cls, part, state, MIME_TMT_CONTENTMARGINS, &out->content_margins))
        out->content_margins = out->sizing_margins;

    int32_t ms_v[2];
    int minsize_prop = sel >= 0 ? minsize_ids[sel] : MIME_TMT_MINSIZE;
    if (get_n(ms, cls, part, state, minsize_prop, ms_v, 2)) {
        out->min_width = ms_v[0];
        out->min_height = ms_v[1];
    } else {
        out->min_width = out->frame_width;
        out->min_height = out->frame_height;
    }

    /* uxtheme scales the sizing margins by an integer factor only from 385 dpi up. */
    out->margin_scale = 1;
    const MimeMsstylesMargins *m = &out->sizing_margins;
    bool scaled = dpi != 96;
    bool small = (out->source_shrink || scaled) && dest_w > 0 && dest_h > 0 &&
                 (dest_w < out->min_width || dest_h < out->min_height);
    if ((m->left || m->right || m->top || m->bottom) && (out->source_grow || scaled) && !small && dpi >= 385 &&
        (int32_t)dpi >= 2 * out->min_dpi)
        out->margin_scale = (int32_t)dpi / out->min_dpi;

    /* TRUESIZE images: the draw size follows the scaling type, the dpi and the destination. */
    out->draw_width = out->frame_width;
    out->draw_height = out->frame_height;
    if (out->sizing_type == MIME_ST_TRUESIZE && out->truesize_scaling_type != MIME_TSST_NONE) {
        int32_t w = out->frame_width, h = out->frame_height, nw = 0, nh = 0;
        bool dest = dest_w > 0 && dest_h > 0;
        if (dest && (scaled || w > dest_w || h > dest_h)) {
            out->draw_width = dest_w;
            out->draw_height = dest_h;
        } else {
            if (out->truesize_scaling_type == MIME_TSST_DPI) {
                nw = muldiv(w, (int32_t)dpi, out->min_dpi);
                nh = muldiv(h, (int32_t)dpi, out->min_dpi);
            } else if (dest && out->min_width > 0 && out->min_height > 0) {
                nw = muldiv(w, dest_w, out->min_width);
                nh = muldiv(h, dest_h, out->min_height);
            }
            if (nw) {
                if (dest) {
                    nw = nw < dest_w ? nw : dest_w;
                    nh = nh < dest_h ? nh : dest_h;
                }
                if (100 * (nw - w) / w >= out->truesize_stretch_mark &&
                    100 * (nh - h) / h >= out->truesize_stretch_mark) {
                    out->draw_width = nw;
                    out->draw_height = nh;
                }
            }
        }
        if (out->uniform_sizing) {
            int64_t a = (int64_t)out->draw_width * h, b = (int64_t)out->draw_height * w;
            if (a > b)
                out->draw_width = (int32_t)(b / h);
            else if (a < b)
                out->draw_height = (int32_t)(a / w);
        }
    }
    return true;
}

#define TMT_IMAGEPROPERTIES 5100
#define TMT_IMAGEINSET 5107

bool mime_msstyles_get_color_image(const MimeMsstyles *ms, int cls, int part, int state, MimeMsstylesColorImage *out)
{
    MimeMsstylesProp p;
    if (!ms || !out || !mime_msstyles_find_prop(ms, cls, part, state, TMT_IMAGEPROPERTIES, &p) || !p.data)
        return false;
    __builtin_memset(out, 0, sizeof(*out));
    int32_t count = get_or(ms, cls, part, state, MIME_TMT_IMAGECOUNT, 1);
    out->image_count = count < 1 ? 1 : count;
    uint32_t pairs = p.size / 8;
    if (!pairs)
        return false;
    uint32_t frame = state >= 1 && state <= out->image_count ? (uint32_t)state - 1 : 0;
    if (frame >= pairs)
        frame = 0;
    out->border = rd32(p.data + 8 * frame);
    out->fill = rd32(p.data + 8 * frame + 4);
    mime_msstyles_get_margins(ms, cls, part, state, MIME_TMT_SIZINGMARGINS, &out->sizing);
    mime_msstyles_get_margins(ms, cls, part, state, TMT_IMAGEINSET, &out->inset);
    out->width = out->sizing.left + out->sizing.right + out->inset.left + out->inset.right + 1;
    out->height = out->sizing.top + out->sizing.bottom + out->inset.top + out->inset.bottom + 1;
    return true;
}
