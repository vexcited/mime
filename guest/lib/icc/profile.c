#include "icc_internal.h"

bool icc_valid(const uint8_t *data, size_t size)
{
    if (!data || size < ICC_HEADER_SIZE + 4)
        return false;
    uint32_t declared = icc_be32(data);
    if (declared < ICC_HEADER_SIZE + 4 || declared > size || icc_be32(data + 36) != ICC_SIG('a', 'c', 's', 'p'))
        return false;
    return icc_be32(data + 128) <= (declared - 132) / 12;
}

uint32_t icc_tag_count(const uint8_t *data) { return icc_be32(data + 128); }

int icc_tag_index(const uint8_t *data, uint32_t sig)
{
    uint32_t count = icc_tag_count(data);
    for (uint32_t i = 0; i < count; i++)
        if (icc_be32(data + 132 + i * 12) == sig)
            return (int)i;
    return -1;
}

const uint8_t *icc_tag(const uint8_t *data, size_t size, uint32_t sig, uint32_t *length)
{
    int i = icc_tag_index(data, sig);
    if (i < 0)
        return nullptr;
    uint32_t offset = icc_be32(data + 132 + i * 12 + 4), len = icc_be32(data + 132 + i * 12 + 8);
    if (offset >= size || len < 8 || len > size - offset)
        return nullptr;
    *length = len;
    return data + offset;
}

bool icc_tag_shared(const uint8_t *data, uint32_t index)
{
    uint32_t count = icc_tag_count(data), offset = icc_be32(data + 132 + index * 12 + 4);
    for (uint32_t i = 0; i < count; i++)
        if (i != index && icc_be32(data + 132 + i * 12 + 4) == offset)
            return true;
    return false;
}

uint32_t icc_space_channels(uint32_t space)
{
    switch (space) {
    case ICC_SIG('G', 'R', 'A', 'Y'):
        return 1;
    case ICC_SIG('2', 'C', 'L', 'R'):
        return 2;
    case ICC_SIG('X', 'Y', 'Z', ' '):
    case ICC_SIG('L', 'a', 'b', ' '):
    case ICC_SIG('L', 'u', 'v', ' '):
    case ICC_SIG('Y', 'C', 'b', 'r'):
    case ICC_SIG('Y', 'x', 'y', ' '):
    case ICC_SIG('R', 'G', 'B', ' '):
    case ICC_SIG('H', 'S', 'V', ' '):
    case ICC_SIG('H', 'L', 'S', ' '):
    case ICC_SIG('C', 'M', 'Y', ' '):
    case ICC_SIG('3', 'C', 'L', 'R'):
        return 3;
    case ICC_SIG('C', 'M', 'Y', 'K'):
    case ICC_SIG('4', 'C', 'L', 'R'):
        return 4;
    }
    /* nCLR for n from 5 to 15, written in hexadecimal. */
    uint32_t n = space >> 24, rest = space & 0xffffff;
    if (rest == ICC_SIG(0, 'C', 'L', 'R')) {
        if (n >= '5' && n <= '9')
            return n - '0';
        if (n >= 'A' && n <= 'F')
            return n - 'A' + 10;
    }
    return 0;
}

/* ------------------------------------------------------------------ editing */

typedef struct Entry {
    uint32_t sig, offset, length;
    /* the data the entry takes in the new profile: a block of the old one or zeros, and how long it is. */
    const uint8_t *from;
    uint32_t from_length, new_length;
    int block;
    bool own;
} Entry;

/*
 * writes a new profile from a list of entries. entries that point at the same old data keep sharing it unless one of
 * them was given its own block.
 */
static bool rebuild(IccEdit *e, Entry *entries, uint32_t count)
{
    uint32_t at = 132 + count * 12;
    for (uint32_t i = 0; i < count; i++) {
        entries[i].block = -1;
        for (uint32_t j = 0; j < i; j++)
            if (!entries[i].own && !entries[j].own && entries[j].from && entries[j].from == entries[i].from &&
                entries[j].new_length == entries[i].new_length) {
                entries[i].block = (int)j;
                break;
            }
        if (entries[i].block >= 0)
            continue;
        at = (at + 3) & ~3u;
        entries[i].offset = at;
        at += entries[i].new_length;
    }
    uint8_t *p = mime_alloc(&e->alloc, at);
    if (!p)
        return false;
    memset(p, 0, at);
    memcpy(p, e->data, ICC_HEADER_SIZE);
    icc_put32(p, at);
    icc_put32(p + 128, count);
    for (uint32_t i = 0; i < count; i++) {
        Entry *t = &entries[i];
        if (t->block >= 0)
            t->offset = entries[t->block].offset;
        else if (t->from)
            memcpy(p + t->offset, t->from, t->from_length < t->new_length ? t->from_length : t->new_length);
        icc_put32(p + 132 + i * 12, t->sig);
        icc_put32(p + 136 + i * 12, t->offset);
        icc_put32(p + 140 + i * 12, t->new_length);
    }
    mime_free(&e->alloc, e->data);
    e->data = p;
    e->size = at;
    return true;
}

static Entry *entries_of(IccEdit *e, uint32_t extra, uint32_t *count)
{
    uint32_t n = icc_tag_count(e->data);
    Entry *list = mime_alloc(&e->alloc, (n + extra + 1) * sizeof(Entry));
    if (!list)
        return nullptr;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *t = e->data + 132 + i * 12;
        uint32_t offset = icc_be32(t + 4), length = icc_be32(t + 8);
        if (offset > e->size || length > e->size - offset)
            length = offset > e->size ? 0 : e->size - offset;
        list[i] = (Entry){icc_be32(t), offset, length, e->data + offset, length, length, -1, false};
    }
    *count = n;
    return list;
}

bool icc_edit_blank(IccEdit *e)
{
    uint8_t *p = mime_alloc(&e->alloc, 132);
    if (!p)
        return false;
    memset(p, 0, 132);
    icc_put32(p, 132);
    icc_put32(p + 8, 0x02100000);
    icc_put32(p + 36, ICC_SIG('a', 'c', 's', 'p'));
    mime_free(&e->alloc, e->data);
    e->data = p;
    e->size = 132;
    return true;
}

bool icc_edit_resize(IccEdit *e, uint32_t sig, uint32_t length)
{
    uint32_t n;
    Entry *list = entries_of(e, 1, &n);
    if (!list)
        return false;
    int i = icc_tag_index(e->data, sig);
    if (i < 0) {
        list[n++] = (Entry){sig, 0, 0, nullptr, 0, length, -1, true};
    } else {
        list[i].new_length = length;
        list[i].own = true;
    }
    bool ok = rebuild(e, list, n);
    mime_free(&e->alloc, list);
    return ok;
}

bool icc_edit_remove(IccEdit *e, uint32_t sig)
{
    int i = icc_tag_index(e->data, sig);
    if (i < 0)
        return false;
    uint32_t n;
    Entry *list = entries_of(e, 0, &n);
    if (!list)
        return false;
    memmove(list + i, list + i + 1, (n - i - 1) * sizeof(Entry));
    bool ok = rebuild(e, list, n - 1);
    mime_free(&e->alloc, list);
    return ok;
}

bool icc_edit_reference(IccEdit *e, uint32_t sig, uint32_t ref)
{
    int r = icc_tag_index(e->data, ref);
    if (r < 0)
        return false;
    uint32_t n;
    Entry *list = entries_of(e, 1, &n);
    if (!list)
        return false;
    int i = icc_tag_index(e->data, sig);
    Entry copy = list[r];
    copy.sig = sig;
    if (i < 0)
        list[n++] = copy;
    else
        list[i] = copy;
    bool ok = rebuild(e, list, n);
    mime_free(&e->alloc, list);
    return ok;
}

uint8_t *icc_edit_tag(IccEdit *e, uint32_t sig, uint32_t *length)
{
    int i = icc_tag_index(e->data, sig);
    if (i < 0)
        return nullptr;
    uint32_t len = icc_be32(e->data + 140 + i * 12);
    if (icc_tag_shared(e->data, (uint32_t)i) && !icc_edit_resize(e, sig, len))
        return nullptr;
    i = icc_tag_index(e->data, sig);
    uint32_t offset = icc_be32(e->data + 136 + i * 12);
    len = icc_be32(e->data + 140 + i * 12);
    if (offset > e->size || len > e->size - offset)
        return nullptr;
    *length = len;
    return e->data + offset;
}
