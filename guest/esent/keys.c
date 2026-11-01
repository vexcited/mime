/* normalized keys: how the values of a record become the bytes an index orders by, and the order of index entries. */
#include "ese.h"
#include <mime/win32/winnls.h>

BOOL ese_fixed_size(JET_COLTYP type, ULONG *size)
{
    ULONG n;
    switch (type) {
    case JET_coltypBit:
    case JET_coltypUnsignedByte:
        n = 1;
        break;
    case JET_coltypShort:
    case JET_coltypUnsignedShort:
        n = 2;
        break;
    case JET_coltypLong:
    case JET_coltypIEEESingle:
    case JET_coltypUnsignedLong:
        n = 4;
        break;
    case JET_coltypCurrency:
    case JET_coltypIEEEDouble:
    case JET_coltypDateTime:
    case JET_coltypLongLong:
        n = 8;
        break;
    case JET_coltypGUID:
        n = 16;
        break;
    default:
        return FALSE;
    }
    if (size)
        *size = n;
    return TRUE;
}

ULONG ese_key_most(ULONG pagesize)
{
    return JET_cbKeyMost;
}

static void put_be(BYTE *out, const BYTE *in, ULONG n, BOOL flip_sign)
{
    for (ULONG i = 0; i < n; i++)
        out[i] = in[n - 1 - i];
    if (flip_sign)
        out[0] ^= 0x80;
}

/* the sort key of text, bytes that order the way the locale orders the characters. */
static ULONG text_key(const COLUMN *col, const INDEX *idx, const void *data, ULONG cb, BYTE *out, ULONG room)
{
    WCHAR local[512];
    WCHAR *wide = local;
    int chars;
    if (col->cp == 1200 || col->cp == 0) {
        chars = (int)(cb / 2);
        if ((ULONG)chars > 510)
            wide = ese_alloc(chars * 2 + 2);
        memcpy(wide, data, chars * 2);
    } else {
        chars = MultiByteToWideChar(col->cp, 0, data, (int)cb, nullptr, 0);
        if ((ULONG)chars > 510)
            wide = ese_alloc(chars * 2 + 2);
        MultiByteToWideChar(col->cp, 0, data, (int)cb, wide, chars);
    }
    ULONG flags = idx && idx->map_flags ? idx->map_flags : (NORM_IGNORECASE | NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH);
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    ULONG lcid = idx && idx->lcid ? idx->lcid : 0x409;
    if (!LCIDToLocaleName(lcid, locale, LOCALE_NAME_MAX_LENGTH, 0))
        lstrcpyW(locale, L"en-US");
    BYTE sort[1200];
    int length = chars ? LCMapStringEx(locale, flags | LCMAP_SORTKEY, wide, chars, (LPWSTR)sort, sizeof(sort), nullptr,
                                       nullptr, 0)
                       : 0;
    if (wide != local)
        ese_free(wide);
    if (length <= 0) {
        length = 0;
    }
    if ((ULONG)length > room)
        length = (int)room;
    memcpy(out, sort, length);
    return (ULONG)length;
}

/* one segment of a key for a value, or for a null when null is set. */
ULONG ese_key_from_value(const COLUMN *col, const INDEX *idx, BOOL desc, const void *data, ULONG cb, BOOL null,
                         JET_GRBIT grbit, BYTE *out, ULONG room)
{
    BYTE tmp[1300];
    ULONG n = 0;
    const BYTE *p = data;
    if (null) {
        tmp[n++] = 0;
    } else {
        tmp[n++] = 0x7f;
        switch (col->coltyp) {
        case JET_coltypBit:
        case JET_coltypUnsignedByte:
            tmp[n++] = cb ? p[0] : 0;
            break;
        case JET_coltypShort:
            put_be(tmp + n, p, 2, TRUE);
            n += 2;
            break;
        case JET_coltypUnsignedShort:
            put_be(tmp + n, p, 2, FALSE);
            n += 2;
            break;
        case JET_coltypLong:
            put_be(tmp + n, p, 4, TRUE);
            n += 4;
            break;
        case JET_coltypUnsignedLong:
            put_be(tmp + n, p, 4, FALSE);
            n += 4;
            break;
        case JET_coltypCurrency:
        case JET_coltypLongLong:
            put_be(tmp + n, p, 8, TRUE);
            n += 8;
            break;
        case JET_coltypIEEESingle:
        case JET_coltypIEEEDouble:
        case JET_coltypDateTime: {
            ULONG size = col->coltyp == JET_coltypIEEESingle ? 4 : 8;
            put_be(tmp + n, p, size, FALSE);
            if (tmp[n] & 0x80) {
                for (ULONG i = 0; i < size; i++)
                    tmp[n + i] = (BYTE)~tmp[n + i];
            } else {
                tmp[n] |= 0x80;
            }
            n += size;
            break;
        }
        case JET_coltypGUID: {
            static const BYTE order[16] = {10, 11, 12, 13, 14, 15, 8, 9, 6, 7, 4, 5, 3, 2, 1, 0};
            for (ULONG i = 0; i < 16; i++)
                tmp[n++] = p[order[i]];
            break;
        }
        case JET_coltypBinary:
        case JET_coltypLongBinary: {
            /* groups of eight bytes, each followed by 9 when more follow and by the count of bytes in the last. */
            ULONG left = cb;
            while (TRUE) {
                ULONG take = left > 8 ? 8 : left;
                memcpy(tmp + n, p + (cb - left), take);
                memset(tmp + n + take, 0, 8 - take);
                n += 8;
                if (left > 8) {
                    tmp[n++] = 9;
                    left -= 8;
                } else {
                    tmp[n++] = (BYTE)(take ? take : 8);
                    break;
                }
                if (n > 1100)
                    break;
            }
            break;
        }
        case JET_coltypText:
        case JET_coltypLongText:
            n += text_key(col, idx, p, cb, tmp + n, sizeof(tmp) - n - 1);
            break;
        default:
            break;
        }
    }
    if (desc)
        for (ULONG i = 0; i < n; i++)
            tmp[i] = (BYTE)~tmp[i];
    if (n > room)
        n = room;
    memcpy(out, tmp, n);
    return n;
}

void ese_free_keys(BYTE **keys, ULONG *cbs, ULONG count)
{
    for (ULONG i = 0; i < count; i++)
        ese_free(keys[i]);
    ese_free(keys);
    ese_free(cbs);
}

static BOOL all_null(const CELL *cell)
{
    return !cell || cell->count == 0;
}

/*
 * the keys a version has in an index, none when the record is left out of it.
 * a multi valued column gives one key for each of its values.
 */
JET_ERR ese_index_keys(TABLE *table, INDEX *idx, const VERSION *ver, BYTE ***keys_out, ULONG **cbs_out, ULONG *count_out)
{
    *keys_out = nullptr;
    *cbs_out = nullptr;
    *count_out = 0;
    if (!idx->nsegs)
        return JET_errSuccess;
    /* conditional columns decide whether the record is in the index at all. */
    for (ULONG i = 0; i < idx->ncond; i++) {
        ULONG slot = 0;
        BOOL found = FALSE;
        for (ULONG c = 0; c < table->ncols; c++)
            if (table->cols[c]->id == idx->cond[i].id) {
                slot = c;
                found = TRUE;
                break;
            }
        BOOL null = !found || all_null(ese_cell(ver, slot));
        if (idx->cond[i].must_be_null != null)
            return JET_errSuccess;
    }
    ULONG slots[16], nvals[16];
    BOOL any_null = FALSE, all_nulls = TRUE;
    ULONG total = 1;
    for (ULONG s = 0; s < idx->nsegs; s++) {
        slots[s] = 0;
        for (ULONG c = 0; c < table->ncols; c++)
            if (table->cols[c]->id == idx->segs[s].id) {
                slots[s] = c;
                break;
            }
        const CELL *cell = ese_cell(ver, slots[s]);
        nvals[s] = cell->count ? cell->count : 1;
        if (!cell->count)
            any_null = TRUE;
        else
            all_nulls = FALSE;
        if (total * nvals[s] < 4096)
            total *= nvals[s];
    }
    if (idx->grbit & JET_bitIndexIgnoreAnyNull) {
        if (any_null)
            return JET_errSuccess;
    }
    if ((idx->grbit & JET_bitIndexIgnoreNull) && all_nulls)
        return JET_errSuccess;
    if ((idx->grbit & JET_bitIndexIgnoreFirstNull) && all_null(ese_cell(ver, slots[0])))
        return JET_errSuccess;
    if ((idx->grbit & JET_bitIndexDisallowNull) && any_null)
        return JET_errNullKeyDisallowed;
    BYTE **keys = ese_alloc(total * sizeof(BYTE *));
    ULONG *cbs = ese_alloc(total * sizeof(ULONG));
    ULONG produced = 0;
    ULONG pick[16] = {};
    ULONG keymost = idx->keymost ? idx->keymost : JET_cbKeyMost;
    while (produced < total) {
        BYTE buf[2100];
        ULONG n = 0;
        for (ULONG s = 0; s < idx->nsegs && n < keymost; s++) {
            COLUMN *col = table->cols[slots[s]];
            const CELL *cell = ese_cell(ver, slots[s]);
            if (!cell->count) {
                n += ese_key_from_value(col, idx, idx->segs[s].desc, nullptr, 0, TRUE, 0, buf + n, keymost - n);
            } else {
                VAL *val = &cell->vals[pick[s]];
                n += ese_key_from_value(col, idx, idx->segs[s].desc, val->data, val->cb, FALSE, 0, buf + n,
                                        keymost - n);
            }
        }
        keys[produced] = ese_dup(buf, n);
        cbs[produced] = n;
        produced++;
        /* the next combination of values. */
        LONG s = (LONG)idx->nsegs - 1;
        for (; s >= 0; s--) {
            if (++pick[s] < nvals[s])
                break;
            pick[s] = 0;
        }
        if (s < 0)
            break;
    }
    /* one entry for each different key. */
    for (ULONG i = 0; i < produced; i++)
        for (ULONG j = 0; j < i; j++)
            if (keys[j] && cbs[j] == cbs[i] && !memcmp(keys[j], keys[i], cbs[i])) {
                ese_free(keys[i]);
                keys[i] = nullptr;
                break;
            }
    ULONG kept = 0;
    for (ULONG i = 0; i < produced; i++)
        if (keys[i]) {
            keys[kept] = keys[i];
            cbs[kept] = cbs[i];
            kept++;
        }
    *keys_out = keys;
    *cbs_out = cbs;
    *count_out = kept;
    return JET_errSuccess;
}

static int bytes_compare(const BYTE *a, ULONG acb, const BYTE *b, ULONG bcb)
{
    ULONG n = acb < bcb ? acb : bcb;
    int c = n ? memcmp(a, b, n) : 0;
    if (c)
        return c < 0 ? -1 : 1;
    return acb < bcb ? -1 : acb > bcb ? 1 : 0;
}

int ese_compare_entry(const BYTE *a, ULONG acb, const BYTE *pa, ULONG pacb, const BYTE *b, ULONG bcb, const BYTE *pb,
                      ULONG pbcb)
{
    int c = bytes_compare(a, acb, b, bcb);
    if (c)
        return c;
    return bytes_compare(pa, pacb, pb, pbcb);
}

/* the position of an entry in an index, or where it would go. */
LONG ese_entry_search(INDEX *idx, const BYTE *key, ULONG cb, const BYTE *pk, ULONG pkcb, BOOL *found)
{
    LONG lo = 0, hi = (LONG)idx->count - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        ENTRY *e = &idx->entries[mid];
        int c = ese_compare_entry(e->key, e->cb, e->rec->pk, e->rec->pkcb, key, cb, pk, pkcb);
        if (c == 0) {
            if (found)
                *found = TRUE;
            return mid;
        }
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    if (found)
        *found = FALSE;
    return lo;
}
