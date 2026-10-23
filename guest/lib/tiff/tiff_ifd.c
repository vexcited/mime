#include "tiff_internal.h"

size_t mime_tiff_type_size(unsigned type)
{
    switch (type) {
    case MIME_TIFF_TYPE_BYTE:
    case MIME_TIFF_TYPE_ASCII:
    case MIME_TIFF_TYPE_SBYTE:
    case MIME_TIFF_TYPE_UNDEFINED:
        return 1;
    case MIME_TIFF_TYPE_SHORT:
    case MIME_TIFF_TYPE_SSHORT:
        return 2;
    case MIME_TIFF_TYPE_LONG:
    case MIME_TIFF_TYPE_SLONG:
    case MIME_TIFF_TYPE_FLOAT:
    case MIME_TIFF_TYPE_IFD:
        return 4;
    case MIME_TIFF_TYPE_RATIONAL:
    case MIME_TIFF_TYPE_SRATIONAL:
    case MIME_TIFF_TYPE_DOUBLE:
    case MIME_TIFF_TYPE_LONG8:
    case MIME_TIFF_TYPE_SLONG8:
    case MIME_TIFF_TYPE_IFD8:
        return 8;
    default:
        return 0;
    }
}

static unsigned header_len(const MimeTiff *t)
{
    return t->bigtiff ? 8 : 2;
}

static unsigned entry_len(const MimeTiff *t)
{
    return t->bigtiff ? 20 : 12;
}

int mime_tiff_open(MimeTiff *t, const void *data, size_t size)
{
    const uint8_t *d = data;
    if (!t || !d || size < 8)
        return MIME_TIFF_ERR_FORMAT;
    bool be;
    if (d[0] == 'I' && d[1] == 'I')
        be = false;
    else if (d[0] == 'M' && d[1] == 'M')
        be = true;
    else
        return MIME_TIFF_ERR_FORMAT;
    unsigned magic = tiff_rd16(d + 2, be);
    uint64_t first;
    bool big;
    if (magic == 42) {
        big = false;
        first = tiff_rd32(d + 4, be);
    } else if (magic == 43) {
        if (size < 16 || tiff_rd16(d + 4, be) != 8 || tiff_rd16(d + 6, be) != 0)
            return MIME_TIFF_ERR_FORMAT;
        big = true;
        first = tiff_rd64(d + 8, be);
    } else {
        return MIME_TIFF_ERR_FORMAT;
    }
    t->data = d;
    t->size = size;
    t->first_ifd = first;
    t->big_endian = be;
    t->bigtiff = big;
    return MIME_TIFF_OK;
}

int mime_tiff_open_exif(MimeTiff *t, const void *data, size_t size)
{
    const uint8_t *d = data;
    if (d && size >= 6 && d[0] == 'E' && d[1] == 'x' && d[2] == 'i' && d[3] == 'f' && d[4] == 0 &&
        (d[5] == 0 || d[5] == 0xff)) {
        d += 6;
        size -= 6;
    }
    return mime_tiff_open(t, d, size);
}

/* the entry count and next offset of an ifd, without the entry table checks of mime_tiff_ifd_at. */
static bool ifd_header(const MimeTiff *t, uint64_t off, uint32_t *count, uint64_t *next)
{
    unsigned hdr = header_len(t);
    if (off > t->size || t->size - off < hdr)
        return false;
    const uint8_t *p = t->data + off;
    uint64_t n = t->bigtiff ? tiff_rd64(p, t->big_endian) : tiff_rd16(p, t->big_endian);
    size_t avail = t->size - (size_t)off - hdr;
    size_t fit = avail / entry_len(t);
    uint64_t nx = 0;
    if (n <= fit) {
        uint64_t nxpos = off + hdr + n * entry_len(t);
        unsigned nlen = t->bigtiff ? 8 : 4;
        if (t->size - nxpos >= nlen)
            nx = t->bigtiff ? tiff_rd64(t->data + nxpos, t->big_endian) : tiff_rd32(t->data + nxpos, t->big_endian);
    } else {
        n = fit;
    }
    *count = n > 0xffffffffu ? 0xffffffffu : (uint32_t)n;
    *next = nx;
    return true;
}

static uint16_t entry_tag_at(const MimeTiffIfd *ifd, uint32_t i)
{
    const MimeTiff *t = &ifd->tiff;
    uint64_t pos = ifd->offset + header_len(t) + (uint64_t)i * entry_len(t);
    return tiff_rd16(t->data + pos, t->big_endian);
}

int mime_tiff_ifd_at(const MimeTiff *t, uint64_t offset, MimeTiffIfd *ifd)
{
    uint32_t count;
    uint64_t next;
    if (!t || !ifd || !ifd_header(t, offset, &count, &next))
        return MIME_TIFF_ERR_FORMAT;
    ifd->tiff = *t;
    ifd->offset = offset;
    ifd->next = next;
    ifd->count = count;
    bool sorted = true;
    uint16_t prev = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint16_t tag = entry_tag_at(ifd, i);
        if (tag < prev) {
            sorted = false;
            break;
        }
        prev = tag;
    }
    ifd->sorted = sorted;
    return MIME_TIFF_OK;
}

/* the ifd after the one at off, 0 when the chain ends or continues at an invalid offset. */
static uint64_t chain_step(const MimeTiff *t, uint64_t off)
{
    uint32_t count;
    uint64_t next;
    if (!ifd_header(t, off, &count, &next) || next == 0)
        return 0;
    uint32_t c2;
    uint64_t n2;
    if (!ifd_header(t, next, &c2, &n2))
        return 0;
    return next;
}

uint32_t mime_tiff_ifd_count(const MimeTiff *t)
{
    uint32_t c;
    uint64_t n;
    if (!t || t->first_ifd == 0 || !ifd_header(t, t->first_ifd, &c, &n))
        return 0;
    uint64_t x0 = t->first_ifd;
    uint64_t hare = chain_step(t, x0);
    if (!hare)
        return 1;
    /* brent's cycle detection. */
    uint64_t tort = x0;
    uint64_t power = 1, lam = 1, steps = 1;
    while (tort != hare) {
        if (power == lam) {
            tort = hare;
            power <<= 1;
            lam = 0;
        }
        uint64_t nx = chain_step(t, hare);
        if (!nx)
            return steps + 1 > 0xffffffffu ? 0xffffffffu : (uint32_t)(steps + 1);
        hare = nx;
        steps++;
        lam++;
    }
    uint64_t x = x0, y = x0;
    for (uint64_t i = 0; i < lam; i++)
        y = chain_step(t, y);
    uint64_t mu = 0;
    while (x != y) {
        x = chain_step(t, x);
        y = chain_step(t, y);
        mu++;
    }
    uint64_t total = mu + lam;
    return total > 0xffffffffu ? 0xffffffffu : (uint32_t)total;
}

int mime_tiff_ifd_index(const MimeTiff *t, uint32_t index, MimeTiffIfd *ifd)
{
    if (!t || !ifd)
        return MIME_TIFF_ERR_PARAM;
    uint32_t c;
    uint64_t n;
    if (t->first_ifd == 0 || !ifd_header(t, t->first_ifd, &c, &n))
        return MIME_TIFF_ERR_RANGE;
    uint64_t off = t->first_ifd;
    if (index <= 64) {
        uint64_t seen[65];
        seen[0] = off;
        for (uint32_t i = 1; i <= index; i++) {
            off = chain_step(t, off);
            if (!off)
                return MIME_TIFF_ERR_RANGE;
            for (uint32_t j = 0; j < i; j++)
                if (seen[j] == off)
                    return MIME_TIFF_ERR_RANGE;
            seen[i] = off;
        }
    } else {
        if (index >= mime_tiff_ifd_count(t))
            return MIME_TIFF_ERR_RANGE;
        for (uint32_t i = 0; i < index; i++)
            off = chain_step(t, off);
    }
    return mime_tiff_ifd_at(t, off, ifd);
}

int mime_tiff_ifd_next(const MimeTiffIfd *ifd, MimeTiffIfd *next)
{
    if (!ifd || !next)
        return MIME_TIFF_ERR_PARAM;
    if (ifd->next == 0)
        return MIME_TIFF_ERR_RANGE;
    MimeTiff t = ifd->tiff;
    return mime_tiff_ifd_at(&t, ifd->next, next) == MIME_TIFF_OK ? MIME_TIFF_OK : MIME_TIFF_ERR_RANGE;
}

int mime_tiff_ifd_entry(const MimeTiffIfd *ifd, uint32_t index, MimeTiffEntry *e)
{
    if (!ifd || !e)
        return MIME_TIFF_ERR_PARAM;
    if (index >= ifd->count)
        return MIME_TIFF_ERR_RANGE;
    const MimeTiff *t = &ifd->tiff;
    bool be = t->big_endian;
    uint64_t pos = ifd->offset + header_len(t) + (uint64_t)index * entry_len(t);
    const uint8_t *p = t->data + pos;
    e->tag = tiff_rd16(p, be);
    e->type = tiff_rd16(p + 2, be);
    e->big_endian = be;
    e->count = t->bigtiff ? tiff_rd64(p + 4, be) : tiff_rd32(p + 4, be);
    const uint8_t *vf = p + (t->bigtiff ? 12 : 8);
    unsigned inline_cap = t->bigtiff ? 8 : 4;
    e->value_offset = pos + (t->bigtiff ? 12 : 8);
    e->data = NULL;
    e->size = 0;
    e->inline_value = false;
    size_t ts = mime_tiff_type_size(e->type);
    if (ts == 0)
        return MIME_TIFF_OK;
    unsigned shift = ts == 1 ? 0 : ts == 2 ? 1 : ts == 4 ? 2 : 3;
    if (shift && (e->count >> (64 - shift)) != 0)
        return MIME_TIFF_OK;
    uint64_t total = e->count << shift;
    if (total <= inline_cap) {
        e->inline_value = true;
        e->data = vf;
        e->size = (size_t)total;
        return MIME_TIFF_OK;
    }
    uint64_t off = t->bigtiff ? tiff_rd64(vf, be) : tiff_rd32(vf, be);
    e->value_offset = off;
    if (off > t->size || total > t->size - off)
        return MIME_TIFF_OK;
    e->data = t->data + off;
    e->size = (size_t)total;
    return MIME_TIFF_OK;
}

int mime_tiff_ifd_find(const MimeTiffIfd *ifd, uint16_t tag, MimeTiffEntry *e)
{
    if (!ifd || !e)
        return MIME_TIFF_ERR_PARAM;
    if (ifd->sorted) {
        uint32_t lo = 0, hi = ifd->count;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            uint16_t t = entry_tag_at(ifd, mid);
            if (t == tag)
                return mime_tiff_ifd_entry(ifd, mid, e);
            if (t < tag)
                lo = mid + 1;
            else
                hi = mid;
        }
        return MIME_TIFF_ERR_RANGE;
    }
    for (uint32_t i = 0; i < ifd->count; i++)
        if (entry_tag_at(ifd, i) == tag)
            return mime_tiff_ifd_entry(ifd, i, e);
    return MIME_TIFF_ERR_RANGE;
}

bool mime_tiff_ifd_uint(const MimeTiffIfd *ifd, uint16_t tag, uint64_t *value)
{
    MimeTiffEntry e;
    return mime_tiff_ifd_find(ifd, tag, &e) == MIME_TIFF_OK && mime_tiff_entry_uint(&e, 0, value);
}

int mime_tiff_ifd_sub(const MimeTiffIfd *ifd, uint16_t tag, uint64_t index, MimeTiffIfd *sub)
{
    MimeTiffEntry e;
    uint64_t off;
    if (mime_tiff_ifd_find(ifd, tag, &e) != MIME_TIFF_OK)
        return MIME_TIFF_ERR_RANGE;
    if (!mime_tiff_entry_uint(&e, index, &off))
        return MIME_TIFF_ERR_RANGE;
    MimeTiff t = ifd->tiff;
    return mime_tiff_ifd_at(&t, off, sub) == MIME_TIFF_OK ? MIME_TIFF_OK : MIME_TIFF_ERR_RANGE;
}

int mime_tiff_thumbnail(const MimeTiff *t, const uint8_t **data, size_t *size)
{
    MimeTiffIfd ifd;
    uint64_t off, len;
    if (mime_tiff_ifd_index(t, 1, &ifd) != MIME_TIFF_OK)
        return MIME_TIFF_ERR_RANGE;
    if (!mime_tiff_ifd_uint(&ifd, MIME_TIFF_TAG_JPEG_INTERCHANGE_FORMAT, &off) ||
        !mime_tiff_ifd_uint(&ifd, MIME_TIFF_TAG_JPEG_INTERCHANGE_LENGTH, &len))
        return MIME_TIFF_ERR_RANGE;
    if (off >= t->size || len == 0 || len > t->size - off)
        return MIME_TIFF_ERR_RANGE;
    *data = t->data + off;
    *size = (size_t)len;
    return MIME_TIFF_OK;
}

bool mime_tiff_entry_uint(const MimeTiffEntry *e, uint64_t i, uint64_t *value)
{
    if (!e || !e->data || i >= e->count)
        return false;
    bool be = e->big_endian;
    switch (e->type) {
    case MIME_TIFF_TYPE_BYTE:
    case MIME_TIFF_TYPE_UNDEFINED:
        *value = e->data[i];
        return true;
    case MIME_TIFF_TYPE_SHORT:
        *value = tiff_rd16(e->data + i * 2, be);
        return true;
    case MIME_TIFF_TYPE_LONG:
    case MIME_TIFF_TYPE_IFD:
        *value = tiff_rd32(e->data + i * 4, be);
        return true;
    case MIME_TIFF_TYPE_LONG8:
    case MIME_TIFF_TYPE_IFD8:
        *value = tiff_rd64(e->data + i * 8, be);
        return true;
    case MIME_TIFF_TYPE_SBYTE:
    case MIME_TIFF_TYPE_SSHORT:
    case MIME_TIFF_TYPE_SLONG:
    case MIME_TIFF_TYPE_SLONG8: {
        int64_t v;
        if (!mime_tiff_entry_int(e, i, &v) || v < 0)
            return false;
        *value = (uint64_t)v;
        return true;
    }
    default:
        return false;
    }
}

bool mime_tiff_entry_int(const MimeTiffEntry *e, uint64_t i, int64_t *value)
{
    if (!e || !e->data || i >= e->count)
        return false;
    bool be = e->big_endian;
    switch (e->type) {
    case MIME_TIFF_TYPE_SBYTE:
        *value = (int8_t)e->data[i];
        return true;
    case MIME_TIFF_TYPE_SSHORT:
        *value = (int16_t)tiff_rd16(e->data + i * 2, be);
        return true;
    case MIME_TIFF_TYPE_SLONG:
        *value = (int32_t)tiff_rd32(e->data + i * 4, be);
        return true;
    case MIME_TIFF_TYPE_SLONG8:
        *value = (int64_t)tiff_rd64(e->data + i * 8, be);
        return true;
    default: {
        uint64_t u;
        if (!mime_tiff_entry_uint(e, i, &u) || u > INT64_MAX)
            return false;
        *value = (int64_t)u;
        return true;
    }
    }
}

bool mime_tiff_entry_rational(const MimeTiffEntry *e, uint64_t i, int64_t *num, int64_t *den)
{
    if (!e || !e->data || i >= e->count)
        return false;
    const uint8_t *p = e->data + i * 8;
    if (e->type == MIME_TIFF_TYPE_RATIONAL) {
        *num = tiff_rd32(p, e->big_endian);
        *den = tiff_rd32(p + 4, e->big_endian);
        return true;
    }
    if (e->type == MIME_TIFF_TYPE_SRATIONAL) {
        *num = (int32_t)tiff_rd32(p, e->big_endian);
        *den = (int32_t)tiff_rd32(p + 4, e->big_endian);
        return true;
    }
    return false;
}

bool mime_tiff_entry_bits(const MimeTiffEntry *e, uint64_t i, uint64_t *bits)
{
    if (!e || !e->data || i >= e->count)
        return false;
    if (e->type == MIME_TIFF_TYPE_FLOAT) {
        *bits = tiff_rd32(e->data + i * 4, e->big_endian);
        return true;
    }
    if (e->type == MIME_TIFF_TYPE_DOUBLE) {
        *bits = tiff_rd64(e->data + i * 8, e->big_endian);
        return true;
    }
    return false;
}

bool mime_tiff_entry_string(const MimeTiffEntry *e, const char **text, size_t *length)
{
    if (!e || !e->data || e->type != MIME_TIFF_TYPE_ASCII)
        return false;
    size_t n = 0;
    while (n < e->size && e->data[n])
        n++;
    *text = (const char *)e->data;
    *length = n;
    return true;
}

size_t mime_tiff_entry_copy_string(const MimeTiffEntry *e, char *dst, size_t cap)
{
    const char *s;
    size_t n;
    if (!mime_tiff_entry_string(e, &s, &n))
        n = 0, s = "";
    if (cap) {
        size_t c = n < cap - 1 ? n : cap - 1;
        memcpy(dst, s, c);
        dst[c] = 0;
    }
    return n;
}
