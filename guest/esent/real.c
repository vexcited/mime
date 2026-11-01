/*
 * the database file of Windows' storage engine, read: the header page, the b+trees of pages, the catalog that
 * names the tables, columns and indexes, the records with their fixed, variable and tagged columns and the long
 * values in their own tree. what is read is rebuilt in memory, so the indexes are made from the records and the
 * trees of the file that hold them are not read.
 */
#include "ese.h"

#define CATALOG_ROOT 4
#define PAGE_ROOT 0x1
#define PAGE_LEAF 0x2
#define PAGE_SPACE 0x20
#define NODE_VERSIONED 1
#define NODE_DELETED 2
#define NODE_COMPRESSED 4

#define CORRUPT JET_errDatabaseCorrupted

typedef struct IMAGE {
    const BYTE *file;
    ULONG size, ps;
} IMAGE;

typedef BOOL (*LEAF_FN)(void *context, const BYTE *key, ULONG key_cb, const BYTE *data, ULONG data_cb);

static const BYTE *page_at(const IMAGE *img, ULONG pgno)
{
    ULONG64 offset = ((ULONG64)pgno + 1) * img->ps;
    if (!pgno || offset + img->ps > img->size)
        return nullptr;
    return img->file + offset;
}

static USHORT u16(const BYTE *p)
{
    return (USHORT)(p[0] | p[1] << 8);
}

static ULONG u32(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (ULONG)p[3] << 24;
}

/* the bytes of a node of a page, none when the tag lies outside the page. */
static BOOL node_at(const IMAGE *img, const BYTE *page, ULONG count, ULONG index, ULONG *flags, const BYTE **bytes,
                    ULONG *cb)
{
    if (index >= count || (index + 1) * 4 + 40 > img->ps)
        return FALSE;
    const BYTE *tag = page + img->ps - 4 * (index + 1);
    ULONG size = u16(tag) & 0x1fff, start = u16(tag + 2) & 0x1fff;
    if (40 + start + size > img->ps - 4 * count)
        return FALSE;
    *flags = u16(tag + 2) >> 13;
    *bytes = page + 40 + start;
    *cb = size;
    return TRUE;
}

/* the leaves under a root in key order, the nodes marked deleted left out. */
static JET_ERR walk_tree(const IMAGE *img, ULONG pgno, LEAF_FN fn, void *context, ULONG depth)
{
    if (depth > 12)
        return CORRUPT;
    const BYTE *page = page_at(img, pgno);
    if (!page)
        return CORRUPT;
    ULONG flags = u32(page + 36), count = u16(page + 34);
    if (flags & PAGE_SPACE)
        return JET_errSuccess;
    if (count == 0 || count > img->ps / 4)
        return CORRUPT;
    const BYTE *external;
    ULONG node_flags, external_cb;
    if (!node_at(img, page, count, 0, &node_flags, &external, &external_cb))
        return CORRUPT;
    const BYTE *prefix = (flags & PAGE_ROOT) ? nullptr : external;
    ULONG prefix_cb = (flags & PAGE_ROOT) ? 0 : external_cb;
    for (ULONG i = 1; i < count; i++) {
        const BYTE *raw;
        ULONG cb;
        if (!node_at(img, page, count, i, &node_flags, &raw, &cb))
            return CORRUPT;
        if (node_flags & NODE_DELETED)
            continue;
        BYTE key[2200];
        ULONG key_cb, skip;
        if (node_flags & NODE_COMPRESSED) {
            if (cb < 4)
                return CORRUPT;
            ULONG common = u16(raw), suffix = u16(raw + 2);
            if (common > prefix_cb || 4 + suffix > cb || common + suffix > sizeof(key))
                return CORRUPT;
            memcpy(key, prefix, common);
            memcpy(key + common, raw + 4, suffix);
            key_cb = common + suffix;
            skip = 4 + suffix;
        } else {
            if (cb < 2)
                return CORRUPT;
            ULONG suffix = u16(raw);
            if (2 + suffix > cb || suffix > sizeof(key))
                return CORRUPT;
            memcpy(key, raw + 2, suffix);
            key_cb = suffix;
            skip = 2 + suffix;
        }
        if (flags & PAGE_LEAF) {
            if (!fn(context, key, key_cb, raw + skip, cb - skip))
                return CORRUPT;
        } else {
            if (cb - skip < 4)
                return CORRUPT;
            JET_ERR err = walk_tree(img, u32(raw + skip), fn, context, depth + 1);
            if (err)
                return err;
        }
    }
    return JET_errSuccess;
}

/* ------------------------------------------------------------------ the catalog */

typedef struct CATROW {
    ULONG objid, type, id, pgno, space, flags, locale;
    WCHAR name[JET_cbNameMost + 1];
    BYTE *def;
    ULONG defcb;
    BYTE *keys;
    ULONG keycb;
    BOOL has_template;
} CATROW;

typedef struct CATALOG {
    CATROW *rows;
    ULONG count, cap;
    BOOL bad;
} CATALOG;

static BOOL catalog_leaf(void *context, const BYTE *key, ULONG key_cb, const BYTE *data, ULONG cb)
{
    CATALOG *cat = context;
    if (cb < 4)
        return FALSE;
    ULONG last_fixed = data[0], last_var = data[1], end_fixed = u16(data + 2);
    static const ULONG sizes[] = {4, 2, 4, 4, 4, 4, 4, 1, 2, 4, 2};
    if (end_fixed > cb || last_fixed > 11)
        return FALSE;
    ULONG bitmap = (last_fixed + 7) / 8, off = 4;
    ULONG values[11] = {};
    BOOL is_null[11] = {};
    for (ULONG f = 0; f < last_fixed; f++) {
        if (off + sizes[f] > end_fixed)
            return FALSE;
        ULONG v = 0;
        for (ULONG b = 0; b < sizes[f]; b++)
            v |= (ULONG)data[off + b] << (8 * b);
        values[f] = v;
        off += sizes[f];
    }
    if (end_fixed < bitmap + off)
        return FALSE;
    for (ULONG f = 0; f < last_fixed; f++)
        is_null[f] = (data[end_fixed - bitmap + f / 8] >> (f % 8)) & 1;
    if (cat->count == cat->cap) {
        cat->cap = cat->cap ? cat->cap * 2 : 256;
        cat->rows = ese_realloc(cat->rows, cat->cap * sizeof(CATROW));
    }
    CATROW *row = &cat->rows[cat->count++];
    memset(row, 0, sizeof(*row));
    row->objid = values[0];
    row->type = values[1] & 0xffff;
    row->id = values[2];
    row->pgno = values[3];
    row->space = values[4];
    row->flags = values[5];
    row->locale = values[6];
    ULONG nvar = last_var >= 128 ? last_var - 127 : 0;
    if (end_fixed + 2 * nvar > cb)
        return FALSE;
    ULONG start = end_fixed + 2 * nvar, previous = 0;
    for (ULONG i = 0; i < nvar; i++) {
        ULONG o = u16(data + end_fixed + 2 * i), end = o & 0x7fff;
        if (!(o & 0x8000) && start + end <= cb && end >= previous) {
            const BYTE *p = data + start + previous;
            ULONG n = end - previous;
            switch (128 + i) {
            case 128:
                for (ULONG c = 0; c < n && c < JET_cbNameMost; c++)
                    row->name[c] = p[c];
                break;
            case 130:
                row->has_template = n != 0;
                break;
            case 131:
                row->def = ese_dup(p, n);
                row->defcb = n;
                break;
            case 132:
                row->keys = ese_dup(p, n);
                row->keycb = n;
                break;
            }
        }
        previous = end;
    }
    (void)is_null;
    return TRUE;
}

/* ------------------------------------------------------------------ long values */

typedef struct LVPIECE {
    ULONG lid, offset;
    const BYTE *data;
    ULONG cb;
} LVPIECE;

typedef struct LVTREE {
    LVPIECE *pieces;
    ULONG count, cap;
} LVTREE;

static BOOL lv_leaf(void *context, const BYTE *key, ULONG key_cb, const BYTE *data, ULONG cb)
{
    LVTREE *tree = context;
    if (key_cb != 4 && key_cb != 8)
        return TRUE;
    if (tree->count == tree->cap) {
        tree->cap = tree->cap ? tree->cap * 2 : 64;
        tree->pieces = ese_realloc(tree->pieces, tree->cap * sizeof(LVPIECE));
    }
    LVPIECE *p = &tree->pieces[tree->count++];
    p->lid = key[0] << 24 | key[1] << 16 | key[2] << 8 | key[3];
    /* the header of a value sorts before its pieces, so a piece is kept one past its offset. */
    p->offset = key_cb == 8 ? (key[4] << 24 | key[5] << 16 | key[6] << 8 | key[7]) + 1 : 0;
    p->data = data;
    p->cb = cb;
    return TRUE;
}

static int lv_compare(ULONG lid, ULONG offset, const LVPIECE *p)
{
    if (lid != p->lid)
        return lid < p->lid ? -1 : 1;
    if (offset != p->offset)
        return offset < p->offset ? -1 : 1;
    return 0;
}

static const LVPIECE *lv_find(const LVTREE *tree, ULONG lid, ULONG offset)
{
    LONG lo = 0, hi = (LONG)tree->count - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        int c = lv_compare(lid, offset, &tree->pieces[mid]);
        if (!c)
            return &tree->pieces[mid];
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return nullptr;
}

/* the whole value behind a reference, put together from its pieces. */
static BOOL lv_fetch(const LVTREE *tree, ULONG lid, BYTE **out, ULONG *cb)
{
    const LVPIECE *header = lv_find(tree, lid, 0);
    if (!header || header->cb < 8)
        return FALSE;
    ULONG total = u32(header->data + 4);
    if (total > 0x40000000)
        return FALSE;
    BYTE *value = ese_alloc(total ? total : 1);
    ULONG at = 0;
    while (at < total) {
        const LVPIECE *piece = lv_find(tree, lid, at + 1);
        if (!piece || at + piece->cb > total) {
            ese_free(value);
            return FALSE;
        }
        memcpy(value + at, piece->data, piece->cb);
        at += piece->cb;
        if (!piece->cb) {
            ese_free(value);
            return FALSE;
        }
    }
    *out = value;
    *cb = total;
    return TRUE;
}

/* ------------------------------------------------------------------ records */

static void add_value(CELL *cell, const BYTE *data, ULONG cb)
{
    cell->vals = ese_realloc(cell->vals, (cell->count + 1) * sizeof(VAL));
    cell->vals[cell->count].cb = cb;
    cell->vals[cell->count].data = ese_dup(data, cb);
    cell->count++;
}

static void add_value_owned(CELL *cell, BYTE *data, ULONG cb)
{
    cell->vals = ese_realloc(cell->vals, (cell->count + 1) * sizeof(VAL));
    cell->vals[cell->count].cb = cb;
    cell->vals[cell->count].data = data;
    cell->count++;
}

/* one tagged column of a record, the values it holds added to its cell. */
static JET_ERR decode_tagged(const LVTREE *lvs, CELL *cell, const BYTE *data, ULONG cb, BOOL extended)
{
    if (!extended) {
        add_value(cell, data, cb);
        return JET_errSuccess;
    }
    if (!cb)
        return CORRUPT;
    ULONG flags = data[0];
    const BYTE *payload = data + 1;
    ULONG size = cb - 1;
    if (flags & 0x02)
        return JET_errFeatureNotAvailable;
    if (flags & 0x08) {
        if (flags & 0x10) {
            if (!size)
                return CORRUPT;
            ULONG each = payload[0];
            if (!each)
                return CORRUPT;
            for (ULONG at = 1; at + each <= size; at += each)
                add_value(cell, payload + at, each);
            return JET_errSuccess;
        }
        if (size < 2)
            return CORRUPT;
        ULONG first = u16(payload) & 0x7fff, n = first / 2;
        if (first > size || (first & 1))
            return CORRUPT;
        for (ULONG i = 0; i < n; i++) {
            ULONG raw = u16(payload + 2 * i), start = raw & 0x7fff;
            ULONG end = i + 1 < n ? u16(payload + 2 * (i + 1)) & 0x7fff : size;
            if (start > end || end > size)
                return CORRUPT;
            if (raw & 0x8000) {
                BYTE *value;
                ULONG lv_cb;
                if (end - start != 4 || !lv_fetch(lvs, u32(payload + start), &value, &lv_cb))
                    return CORRUPT;
                add_value_owned(cell, value, lv_cb);
            } else {
                add_value(cell, payload + start, end - start);
            }
        }
        return JET_errSuccess;
    }
    if (flags & 0x04) {
        BYTE *value;
        ULONG lv_cb;
        if (size != 4 || !lv_fetch(lvs, u32(payload), &value, &lv_cb))
            return CORRUPT;
        add_value_owned(cell, value, lv_cb);
        return JET_errSuccess;
    }
    add_value(cell, payload, size);
    return JET_errSuccess;
}

static ULONG fixed_width(const TABLE *table, ULONG fid)
{
    for (ULONG i = 0; i < table->ncols; i++)
        if (table->cols[i]->id == fid) {
            ULONG size = 0;
            /* a binary or text column can be fixed too, and then takes all of its maximum size. */
            if (ese_fixed_size(table->cols[i]->coltyp, &size))
                return size;
            return table->cols[i]->coltyp == JET_coltypBinary || table->cols[i]->coltyp == JET_coltypText
                       ? table->cols[i]->cbMax : 0;
        }
    return 0;
}

static JET_ERR decode_record(const TABLE *table, const LVTREE *lvs, const BYTE *rec, ULONG cb, VERSION *ver)
{
    if (cb < 4)
        return CORRUPT;
    ULONG last_fixed = rec[0], last_var = rec[1], end_fixed = u16(rec + 2);
    if (end_fixed > cb || end_fixed < 4 + (last_fixed + 7) / 8)
        return CORRUPT;
    ULONG bitmap = (last_fixed + 7) / 8, off = 4;
    for (ULONG fid = 1; fid <= last_fixed; fid++) {
        ULONG width = fixed_width(table, fid);
        if (!width || off + width > end_fixed - bitmap)
            return CORRUPT;
        ULONG slot = ese_slot((TABLE *)table, fid);
        BOOL null = (rec[end_fixed - bitmap + (fid - 1) / 8] >> ((fid - 1) % 8)) & 1;
        CELL *cell = ese_cell_w(ver, slot);
        cell->set = TRUE;
        if (!null)
            add_value(cell, rec + off, width);
        off += width;
    }
    ULONG nvar = last_var >= 128 ? last_var - 127 : 0;
    if (end_fixed + 2 * nvar > cb)
        return CORRUPT;
    ULONG start = end_fixed + 2 * nvar, previous = 0;
    for (ULONG i = 0; i < nvar; i++) {
        ULONG raw = u16(rec + end_fixed + 2 * i), end = raw & 0x7fff;
        if (end < previous || start + end > cb)
            return CORRUPT;
        ULONG slot = ese_slot((TABLE *)table, 128 + i);
        if (slot != ~0u) {
            CELL *cell = ese_cell_w(ver, slot);
            cell->set = TRUE;
            if (!(raw & 0x8000))
                add_value(cell, rec + start + previous, end - previous);
        }
        previous = end;
    }
    ULONG tagged_at = start + previous;
    if (tagged_at >= cb)
        return JET_errSuccess;
    const BYTE *t = rec + tagged_at;
    ULONG tcb = cb - tagged_at;
    if (tcb < 4)
        return CORRUPT;
    ULONG entries = (u16(t + 2) & 0x1fff) / 4;
    if (entries * 4 > tcb)
        return CORRUPT;
    for (ULONG i = 0; i < entries; i++) {
        ULONG fid = u16(t + 4 * i), ib = u16(t + 4 * i + 2);
        ULONG s = ib & 0x1fff;
        ULONG e = i + 1 < entries ? u16(t + 4 * (i + 1) + 2) & 0x1fff : tcb;
        if (s > e || e > tcb)
            return CORRUPT;
        ULONG slot = ese_slot((TABLE *)table, fid);
        if (slot == ~0u)
            continue;
        CELL *cell = ese_cell_w(ver, slot);
        cell->set = TRUE;
        if (ib & 0x8000)
            continue;
        JET_ERR err = decode_tagged(lvs, cell, t + s, e - s, (ib & 0x4000) != 0);
        if (err)
            return err;
    }
    return JET_errSuccess;
}

/* ------------------------------------------------------------------ rebuilding a database */

typedef struct LOADING {
    TABLE *table;
    const LVTREE *lvs;
    JET_ERR err;
} LOADING;

static BOOL record_leaf(void *context, const BYTE *key, ULONG key_cb, const BYTE *data, ULONG cb)
{
    LOADING *loading = context;
    TABLE *table = loading->table;
    VERSION *v = ese_new_version(table);
    v->commit = 1;
    loading->err = decode_record(table, loading->lvs, data, cb, v);
    if (loading->err) {
        ese_free_cells(table, v);
        ese_free(v);
        return FALSE;
    }
    RECORD *rec = ese_alloc(sizeof(*rec));
    rec->head = v;
    if (table->primary->sequential) {
        BYTE seq[4];
        ULONG id = (ULONG)table->next_id++;
        seq[0] = (BYTE)(id >> 24);
        seq[1] = (BYTE)(id >> 16);
        seq[2] = (BYTE)(id >> 8);
        seq[3] = (BYTE)id;
        rec->pk = ese_dup(seq, 4);
        rec->pkcb = 4;
    } else {
        BYTE **keys;
        ULONG *cbs, count;
        if (ese_index_keys(table, table->primary, v, &keys, &cbs, &count) || !count) {
            ese_free_cells(table, v);
            ese_free(v);
            ese_free(rec);
            loading->err = CORRUPT;
            return FALSE;
        }
        rec->pk = ese_dup(keys[0], cbs[0]);
        rec->pkcb = cbs[0];
        ese_free_keys(keys, cbs, count);
    }
    BOOL found;
    INDEX *primary = table->primary;
    LONG at = ese_entry_search(primary, rec->pk, rec->pkcb, rec->pk, rec->pkcb, &found);
    if (found) {
        ese_free_cells(table, v);
        ese_free(v);
        ese_free(rec->pk);
        ese_free(rec);
        loading->err = CORRUPT;
        return FALSE;
    }
    if (primary->count == primary->cap) {
        primary->cap = primary->cap ? primary->cap * 2 : 64;
        primary->entries = ese_realloc(primary->entries, primary->cap * sizeof(ENTRY));
    }
    memmove(&primary->entries[at + 1], &primary->entries[at], (primary->count - at) * sizeof(ENTRY));
    primary->entries[at].key = ese_dup(rec->pk, rec->pkcb);
    primary->entries[at].cb = rec->pkcb;
    primary->entries[at].rec = rec;
    primary->count++;
    ese_index_record(table, rec, v);
    return TRUE;
}

static JET_COLTYP column_grbit(const CATROW *row)
{
    JET_GRBIT grbit = 0;
    if (row->flags & 0x1)
        grbit |= JET_bitColumnNotNULL;
    if (row->flags & 0x2)
        grbit |= JET_bitColumnVersion;
    if (row->flags & 0x4)
        grbit |= JET_bitColumnAutoincrement;
    if (row->flags & 0x8)
        grbit |= JET_bitColumnMultiValued;
    if (row->flags & 0x20)
        grbit |= JET_bitColumnEscrowUpdate;
    if (row->flags & 0x40)
        grbit |= JET_bitColumnFinalize;
    if (row->flags & 0x80)
        grbit |= JET_bitColumnUserDefinedDefault;
    if (row->id < 128)
        grbit |= JET_bitColumnFixed;
    else if (row->id >= 256)
        grbit |= JET_bitColumnTagged;
    return grbit;
}

static JET_GRBIT index_grbit(ULONG flags)
{
    JET_GRBIT grbit = 0;
    if (flags & 0x1)
        grbit |= JET_bitIndexUnique;
    if (flags & 0x20)
        grbit |= JET_bitIndexPrimary;
    if (flags & 0x10)
        grbit |= JET_bitIndexDisallowNull;
    else if (!(flags & 0x2) && !(flags & 0x4) && !(flags & 0x8))
        grbit |= JET_bitIndexIgnoreAnyNull;
    else if (!(flags & 0x2) && (flags & 0x8) && (flags & 0x4))
        grbit |= JET_bitIndexIgnoreNull;
    else if (!(flags & 0x2) && (flags & 0x8) && !(flags & 0x4))
        grbit |= JET_bitIndexIgnoreFirstNull;
    if (flags & 0x800)
        grbit |= JET_bitIndexSortNullsHigh;
    return grbit;
}

static JET_ERR build_table(DB *db, const IMAGE *img, const CATALOG *cat, const CATROW *trow)
{
    TABLE *t = ese_alloc(sizeof(*t));
    t->db = db;
    lstrcpyW(t->name, trow->name);
    t->objid = trow->objid;
    t->next_id = 1;
    t->pages = 1;
    t->density = 100;
    t->last_fixed = 0;
    t->last_var = 127;
    t->last_tagged = 255;
    t->primary = ese_alloc(sizeof(INDEX));
    t->primary->primary = TRUE;
    t->primary->sequential = TRUE;
    t->idx = ese_alloc(16 * sizeof(INDEX *));
    t->nicap = 16;
    t->idx[t->nidx++] = t->primary;
    ULONG lv_root = 0;
    for (ULONG i = 0; i < cat->count; i++) {
        const CATROW *row = &cat->rows[i];
        if (row->objid != trow->objid)
            continue;
        if (row->type == 4)
            lv_root = row->pgno;
        if (row->type != 2)
            continue;
        if (t->ncols == t->ncap) {
            t->ncap = t->ncap ? t->ncap * 2 : 16;
            t->cols = ese_realloc(t->cols, t->ncap * sizeof(COLUMN *));
        }
        COLUMN *c = ese_alloc(sizeof(*c));
        c->id = row->id;
        lstrcpyW(c->name, row->name);
        c->coltyp = row->pgno;
        c->cbMax = row->space;
        c->grbit = column_grbit(row);
        c->cp = (c->coltyp == JET_coltypText || c->coltyp == JET_coltypLongText) ? (USHORT)(row->locale ? row->locale : 1200)
                                                                                  : 0;
        c->langid = c->cp ? 0x409 : 0;
        c->deleted = (row->flags & 0x200) != 0;
        if (row->defcb) {
            c->def = ese_dup(row->def, row->defcb);
            c->defcb = row->defcb;
        }
        if (c->id < 128 && c->id > t->last_fixed)
            t->last_fixed = c->id;
        else if (c->id >= 128 && c->id < 256 && c->id > t->last_var)
            t->last_var = c->id;
        else if (c->id >= 256 && c->id > t->last_tagged)
            t->last_tagged = c->id;
        /* the columns are kept in the order of their ids. */
        ULONG at = t->ncols;
        while (at > 0 && t->cols[at - 1]->id > c->id) {
            t->cols[at] = t->cols[at - 1];
            at--;
        }
        t->cols[at] = c;
        t->ncols++;
    }
    for (ULONG i = 0; i < cat->count; i++) {
        const CATROW *row = &cat->rows[i];
        if (row->objid != trow->objid || row->type != 3)
            continue;
        INDEX *idx = ese_alloc(sizeof(*idx));
        lstrcpyW(idx->name, row->name);
        idx->grbit = index_grbit(row->flags);
        idx->primary = (row->flags & 0x20) != 0;
        idx->lcid = row->locale ? row->locale : 0x409;
        idx->keymost = JET_cbKeyMost;
        idx->density = row->space;
        idx->objid = row->id;
        for (ULONG k = 0; k + 4 <= row->keycb && idx->nsegs < 16; k += 4) {
            idx->segs[idx->nsegs].desc = (u16(row->keys + k) & 0x40) != 0;
            idx->segs[idx->nsegs].id = u16(row->keys + k + 2);
            idx->nsegs++;
        }
        if (idx->primary) {
            ese_free(t->primary);
            t->idx[0] = idx;
            t->primary = idx;
        } else {
            if (t->nidx == t->nicap) {
                t->nicap *= 2;
                t->idx = ese_realloc(t->idx, t->nicap * sizeof(INDEX *));
            }
            t->idx[t->nidx++] = idx;
        }
    }
    LVTREE lvs = {};
    JET_ERR err = JET_errSuccess;
    if (lv_root)
        err = walk_tree(img, lv_root, lv_leaf, &lvs, 0);
    if (!err) {
        LOADING loading = {t, &lvs, JET_errSuccess};
        err = walk_tree(img, trow->pgno, record_leaf, &loading, 0);
        if (!err)
            err = loading.err;
    }
    ese_free(lvs.pieces);
    if (err) {
        ese_free_table(t);
        return err;
    }
    t->next = db->tables;
    db->tables = t;
    return JET_errSuccess;
}

JET_ERR ese_load_real(DB *db, const BYTE *file, ULONG size)
{
    IMAGE img = {file, size, db->pagesize};
    CATALOG cat = {};
    JET_ERR err = walk_tree(&img, CATALOG_ROOT, catalog_leaf, &cat, 0);
    for (ULONG i = 0; i < cat.count && !err; i++) {
        const CATROW *row = &cat.rows[i];
        if (row->type != 1 || !row->name[0])
            continue;
        if (row->name[0] == 'M' && row->name[1] == 'S' && row->name[2] == 'y' && row->name[3] == 's')
            continue;
        err = build_table(db, &img, &cat, row);
    }
    for (ULONG i = 0; i < cat.count; i++) {
        ese_free(cat.rows[i].def);
        ese_free(cat.rows[i].keys);
    }
    ese_free(cat.rows);
    if (!err) {
        db->real_format = TRUE;
        /* the file is not written back in Windows' format yet, so it is not written at all. */
        db->readonly = TRUE;
    }
    return err;
}
