/* records and cursors: reading, writing, deleting and moving over the records of a table, with the versions that give
   each session the records as of its transaction. */
#include "ese.h"

#define POS_BEFORE 0
#define POS_ON 1
#define POS_AFTER 2

/* ------------------------------------------------------------------ visibility */

VERSION *ese_visible(RECORD *rec, SESSION *sess)
{
    for (VERSION *v = rec->head; v; v = v->older) {
        if (!v->commit) {
            if (v->owner == sess)
                return v->deleted ? nullptr : v;
            continue;
        }
        if (!sess->level || v->commit <= sess->snapshot)
            return v->deleted ? nullptr : v;
    }
    return nullptr;
}

static JET_ERR check_write(RECORD *rec, SESSION *sess)
{
    if (rec->lock && rec->lock != sess)
        return JET_errWriteConflict;
    if (rec->head && !rec->head->commit && rec->head->owner != sess)
        return JET_errWriteConflict;
    for (VERSION *v = rec->head; v; v = v->older)
        if (v->commit) {
            if (sess->level && v->commit > sess->snapshot)
                return JET_errWriteConflict;
            break;
        }
    return ese_visible(rec, sess) ? JET_errSuccess : JET_errRecordDeleted;
}

/* ------------------------------------------------------------------ cursors */

JET_ERR ese_open_cursor(SESSION *sess, DBH *dbh, TABLE *table, JET_GRBIT grbit, CURSOR **out)
{
    CURSOR *cur = ese_alloc(sizeof(*cur));
    if (!cur)
        return JET_errOutOfMemory;
    cur->sess = sess;
    cur->dbh = dbh;
    cur->table = table;
    cur->index = table->primary;
    cur->pos = POS_BEFORE;
    cur->next = sess->cursors;
    sess->cursors = cur;
    table->refs++;
    if (dbh)
        dbh->refs++;
    cur->open_grbit = grbit;
    *out = cur;
    return JET_errSuccess;
}

void ese_close_cursor(CURSOR *cur)
{
    SESSION *sess = cur->sess;
    if (cur->copy) {
        ese_free_cells(cur->table, cur->copy);
        ese_free(cur->copy);
        cur->copy = nullptr;
        if (cur->copy_rec && cur->copy_rec->lock == sess && !sess->level) {
            cur->copy_rec->lock = nullptr;
        }
    }
    for (CURSOR **link = &sess->cursors; *link; link = &(*link)->next)
        if (*link == cur) {
            *link = cur->next;
            break;
        }
    TABLE *table = cur->table;
    if (table->creator == cur)
        table->creator = nullptr;
    table->refs--;
    if (cur->dbh)
        cur->dbh->refs--;
    ese_free(cur->ekey);
    ese_free(cur->mkey);
    ese_free(cur->rkey);
    ese_free(cur->bmcache);
    ese_free(cur);
    if (table->temp && !table->refs) {
        /* a temporary table is gone with its last cursor. */
        ese_free_table(table);
    } else if (table->deleted && !table->refs && !sess->level) {
        for (TABLE **link = &table->db->tables; *link; link = &(*link)->next)
            if (*link == table) {
                *link = table->next;
                ese_free_table(table);
                break;
            }
    }
}

JET_ERR ese_dup_cursor(CURSOR *cur, CURSOR **out)
{
    JET_ERR err = ese_open_cursor(cur->sess, cur->dbh, cur->table, 0, out);
    if (err)
        return err;
    CURSOR *dup = *out;
    dup->index = cur->index;
    dup->pos = cur->pos;
    dup->rec = nullptr;
    dup->ekey = ese_dup(cur->ekey, cur->ekeycb);
    dup->ekeycb = cur->ekeycb;
    dup->bmcache = ese_dup(cur->bmcache, cur->bmcache ? *(ULONG *)cur->bmcache + 4 : 0);
    return JET_errSuccess;
}

/* the record the cursor is on, which is found again by its key each time since records come and go. */
static RECORD *cursor_record(CURSOR *cur)
{
    if (cur->pos != POS_ON || !cur->bmcache)
        return nullptr;
    ULONG pkcb = *(ULONG *)cur->bmcache;
    BOOL found;
    INDEX *primary = cur->table->primary;
    LONG at = ese_entry_search(primary, cur->bmcache + 4, pkcb, cur->bmcache + 4, pkcb, &found);
    return found ? primary->entries[at].rec : nullptr;
}

VERSION *ese_current_version(CURSOR *cur)
{
    RECORD *rec = cursor_record(cur);
    return rec ? ese_visible(rec, cur->sess) : nullptr;
}

static void set_position(CURSOR *cur, ENTRY *e)
{
    ese_free(cur->bmcache);
    cur->bmcache = ese_alloc(e->rec->pkcb + 4);
    *(ULONG *)cur->bmcache = e->rec->pkcb;
    memcpy(cur->bmcache + 4, e->rec->pk, e->rec->pkcb);
    ese_free(cur->ekey);
    cur->ekey = ese_dup(e->key, e->cb);
    cur->ekeycb = e->cb;
    cur->pos = POS_ON;
}

/* whether an entry is one of the records the session sees, for the key it has. */
static BOOL entry_visible(CURSOR *cur, INDEX *idx, ENTRY *e)
{
    VERSION *v = ese_visible(e->rec, cur->sess);
    if (!v)
        return FALSE;
    if (idx->primary)
        return TRUE;
    BYTE **keys;
    ULONG *cbs, count;
    if (ese_index_keys(cur->table, idx, v, &keys, &cbs, &count))
        return FALSE;
    BOOL ok = FALSE;
    for (ULONG i = 0; i < count && !ok; i++)
        ok = cbs[i] == e->cb && !memcmp(keys[i], e->key, e->cb);
    ese_free_keys(keys, cbs, count);
    return ok;
}

static BOOL in_range(CURSOR *cur, ENTRY *e)
{
    if (!cur->has_range)
        return TRUE;
    ULONG n = e->cb < cur->rkeycb ? e->cb : cur->rkeycb;
    int c = n ? memcmp(e->key, cur->rkey, n) : 0;
    if (!c)
        c = e->cb < cur->rkeycb ? -1 : e->cb > cur->rkeycb ? 1 : 0;
    if (cur->range_upper)
        return c < 0 || (c == 0 && cur->range_inclusive);
    return c > 0 || c == 0;
}

/* the index of the entry the cursor is at, or where it would be. */
static LONG locate(CURSOR *cur, BOOL *found)
{
    if (cur->pos == POS_BEFORE) {
        *found = FALSE;
        return -1;
    }
    if (cur->pos == POS_AFTER) {
        *found = FALSE;
        return (LONG)cur->index->count;
    }
    ULONG pkcb = cur->bmcache ? *(ULONG *)cur->bmcache : 0;
    return ese_entry_search(cur->index, cur->ekey, cur->ekeycb, cur->bmcache + 4, pkcb, found);
}

/* the next visible entry in a direction from an index, or -1. */
static LONG scan(CURSOR *cur, LONG from, int dir)
{
    INDEX *idx = cur->index;
    for (LONG i = from; i >= 0 && i < (LONG)idx->count; i += dir)
        if (entry_visible(cur, idx, &idx->entries[i]))
            return i;
    return -1;
}

JET_ERR ese_move(CURSOR *cur, LONG rows, JET_GRBIT grbit)
{
    INDEX *idx = cur->index;
    LONG at;
    int dir;
    LONG count;
    if (rows == (LONG)JET_MoveFirst) {
        at = scan(cur, 0, 1);
        dir = 1;
        count = 0;
        if (at >= 0 && !in_range(cur, &idx->entries[at]))
            at = -1;
        if (at < 0) {
            cur->pos = POS_AFTER;
            return JET_errNoCurrentRecord;
        }
        set_position(cur, &idx->entries[at]);
        return JET_errSuccess;
    }
    if (rows == JET_MoveLast) {
        at = scan(cur, (LONG)idx->count - 1, -1);
        if (at >= 0 && !in_range(cur, &idx->entries[at]))
            at = -1;
        if (at < 0) {
            cur->pos = POS_BEFORE;
            return JET_errNoCurrentRecord;
        }
        set_position(cur, &idx->entries[at]);
        return JET_errSuccess;
    }
    if (rows == 0)
        return cur->pos == POS_ON ? JET_errSuccess : JET_errNoCurrentRecord;
    dir = rows > 0 ? 1 : -1;
    count = rows > 0 ? rows : -rows;
    BOOL found;
    LONG here = locate(cur, &found);
    for (LONG step = 0; step < count; step++) {
        LONG from;
        if (cur->pos == POS_ON && found)
            from = here + dir;
        else if (cur->pos == POS_ON)
            from = dir > 0 ? here : here - 1;
        else
            from = dir > 0 ? 0 : (LONG)idx->count - 1;
        if (cur->pos == POS_BEFORE && dir < 0) {
            at = -1;
        } else if (cur->pos == POS_AFTER && dir > 0) {
            at = -1;
        } else {
            at = scan(cur, from, dir);
        }
        if (at >= 0 && !in_range(cur, &idx->entries[at]))
            at = -1;
        if (at < 0) {
            cur->pos = dir > 0 ? POS_AFTER : POS_BEFORE;
            if (cur->has_range) {
                cur->has_range = FALSE;
            }
            return JET_errNoCurrentRecord;
        }
        set_position(cur, &idx->entries[at]);
        here = at;
        found = TRUE;
    }
    return JET_errSuccess;
}

/* ------------------------------------------------------------------ keys */

JET_ERR ese_make_key(CURSOR *cur, const void *data, ULONG cb, JET_GRBIT grbit)
{
    INDEX *idx = cur->index;
    if (grbit & JET_bitNewKey) {
        ese_free(cur->mkey);
        cur->mkey = nullptr;
        cur->mkeycb = 0;
        cur->mkey_segs = 0;
        cur->mkey_flags = 1;
    } else if (!cur->mkey_flags) {
        return JET_errKeyNotMade;
    }
    if (cur->mkey_flags & 2)
        return JET_errKeyIsMade;
    if (grbit & JET_bitNormalizedKey) {
        if (cb > JET_cbKeyMost + 1)
            return JET_errInvalidParameter;
        ese_free(cur->mkey);
        cur->mkey = ese_dup(data, cb);
        cur->mkeycb = cb;
        cur->mkey_flags |= 2;
        return JET_errSuccess;
    }
    if (idx->sequential) {
        /* a sequential table is looked up by its bookmark only. */
        return JET_errInvalidParameter;
    }
    if (cur->mkey_segs >= idx->nsegs)
        return JET_errKeyIsMade;
    COLUMN *col = ese_column_by_id(cur->table, idx->segs[cur->mkey_segs].id);
    BOOL desc = idx->segs[cur->mkey_segs].desc;
    BOOL null = (!data || !cb) && !(grbit & JET_bitKeyDataZeroLength);
    ULONG size;
    if (!null && ese_fixed_size(col->coltyp, &size) && cb != size)
        return JET_errInvalidBufferSize;
    if ((grbit & JET_bitKeyDataZeroLength) && ese_fixed_size(col->coltyp, nullptr) && !cb)
        return JET_errInvalidBufferSize;
    ULONG keymost = idx->keymost ? idx->keymost : JET_cbKeyMost;
    if ((col->coltyp == JET_coltypText) && (cb & 1) && col->cp == 1200 && !null)
        cb &= ~1u;
    BYTE buf[2200];
    ULONG n = ese_key_from_value(col, idx, desc, data, cb, null, grbit, buf, 2000);
    if (grbit & (JET_bitSubStrLimit)) {
        /* only the weights of the characters themselves are kept, then the limit. */
        ULONG at = 1;
        while (at < n && buf[at] != (desc ? 0xfe : 0x01))
            at++;
        n = at;
    }
    BOOL pad = (grbit & (JET_bitStrLimit | JET_bitSubStrLimit | JET_bitFullColumnEndLimit | JET_bitPartialColumnEndLimit)) != 0;
    ULONG total = cur->mkeycb + n;
    if (total > keymost)
        total = keymost, n = total - cur->mkeycb;
    cur->mkey = ese_realloc(cur->mkey, keymost + 2);
    memcpy(cur->mkey + cur->mkeycb, buf, n);
    cur->mkeycb = total;
    if (pad) {
        memset(cur->mkey + cur->mkeycb, 0xff, keymost + 1 - cur->mkeycb);
        cur->mkeycb = keymost + 1;
        cur->mkey_flags |= 2;
    }
    cur->mkey_segs++;
    return JET_errSuccess;
}

static void clear_key(CURSOR *cur)
{
    ese_free(cur->mkey);
    cur->mkey = nullptr;
    cur->mkeycb = 0;
    cur->mkey_segs = 0;
    cur->mkey_flags = 0;
}

JET_ERR ese_seek(CURSOR *cur, JET_GRBIT grbit)
{
    INDEX *idx = cur->index;
    if (!cur->mkey_flags)
        return JET_errKeyNotMade;
    ULONG ops = grbit & (JET_bitSeekEQ | JET_bitSeekLT | JET_bitSeekLE | JET_bitSeekGE | JET_bitSeekGT);
    if (ops != JET_bitSeekEQ && ops != JET_bitSeekLT && ops != JET_bitSeekLE && ops != JET_bitSeekGE &&
        ops != JET_bitSeekGT)
        return JET_errInvalidParameter;
    BYTE *key = cur->mkey;
    ULONG cb = cur->mkeycb;
    BYTE inf[JET_cbKeyMost + 8];
    memset(inf, 0xff, sizeof(inf));
    LONG at = -1;
    if (ops == JET_bitSeekEQ || ops == JET_bitSeekGE) {
        LONG p = ese_entry_search(idx, key, cb, (const BYTE *)"", 0, nullptr);
        at = scan(cur, p, 1);
        if (at >= 0 && ops == JET_bitSeekEQ) {
            ENTRY *e = &idx->entries[at];
            if (e->cb < cb || memcmp(e->key, key, cb))
                at = -1;
        }
    } else if (ops == JET_bitSeekGT) {
        LONG p = ese_entry_search(idx, key, cb, inf, 40, nullptr);
        at = scan(cur, p, 1);
    } else if (ops == JET_bitSeekLE) {
        LONG p = ese_entry_search(idx, key, cb, inf, 40, nullptr);
        at = scan(cur, p - 1, -1);
    } else {
        LONG p = ese_entry_search(idx, key, cb, (const BYTE *)"", 0, nullptr);
        at = scan(cur, p - 1, -1);
    }
    if (at < 0) {
        clear_key(cur);
        cur->pos = ops == JET_bitSeekLT || ops == JET_bitSeekLE ? POS_BEFORE : POS_AFTER;
        return JET_errRecordNotFound;
    }
    ENTRY *e = &idx->entries[at];
    BOOL equal = e->cb == cb && !memcmp(e->key, key, cb);
    BOOL prefix = e->cb >= cb && !memcmp(e->key, key, cb);
    set_position(cur, e);
    if (grbit & JET_bitSetIndexRange) {
        ese_free(cur->rkey);
        cur->rkey = ese_dup(key, cb);
        cur->rkeycb = cb;
        cur->has_range = TRUE;
        cur->range_upper = TRUE;
        cur->range_inclusive = TRUE;
    }
    clear_key(cur);
    (void)prefix;
    return equal || ops == JET_bitSeekEQ || ops == JET_bitSeekGT || ops == JET_bitSeekLT ? JET_errSuccess
                                                                                        : JET_wrnSeekNotEqual;
}

JET_ERR ese_set_index_range(CURSOR *cur, JET_GRBIT grbit)
{
    if (grbit & JET_bitRangeRemove) {
        cur->has_range = FALSE;
        clear_key(cur);
        return JET_errSuccess;
    }
    if (!cur->mkey_flags)
        return JET_errKeyNotMade;
    if (cur->pos != POS_ON)
        return JET_errNoCurrentRecord;
    if (grbit & JET_bitRangeInstantDuration) {
        /* the current record is tested against the key, and the range is not kept. */
        ENTRY e = {cur->ekey, cur->ekeycb, nullptr};
        BYTE *saved = cur->rkey;
        ULONG savedcb = cur->rkeycb;
        BOOL had = cur->has_range, up = cur->range_upper, inc = cur->range_inclusive;
        cur->rkey = cur->mkey;
        cur->rkeycb = cur->mkeycb;
        cur->has_range = TRUE;
        cur->range_upper = (grbit & JET_bitRangeUpperLimit) != 0;
        cur->range_inclusive = (grbit & JET_bitRangeInclusive) != 0;
        BOOL ok = in_range(cur, &e);
        cur->rkey = saved;
        cur->rkeycb = savedcb;
        cur->has_range = had;
        cur->range_upper = up;
        cur->range_inclusive = inc;
        clear_key(cur);
        return ok ? JET_errSuccess : JET_errNoCurrentRecord;
    }
    ese_free(cur->rkey);
    cur->rkey = ese_dup(cur->mkey, cur->mkeycb);
    cur->rkeycb = cur->mkeycb;
    cur->has_range = TRUE;
    cur->range_upper = (grbit & JET_bitRangeUpperLimit) != 0;
    cur->range_inclusive = (grbit & JET_bitRangeInclusive) != 0;
    clear_key(cur);
    /* the current record may already be out of the range. */
    ENTRY here = {cur->ekey, cur->ekeycb, nullptr};
    if (!in_range(cur, &here)) {
        cur->has_range = FALSE;
        return JET_errNoCurrentRecord;
    }
    return JET_errSuccess;
}

JET_ERR ese_set_current_index(CURSOR *cur, LPCWSTR name, JET_GRBIT grbit, ULONG tag)
{
    INDEX *idx;
    if (!name || !*name) {
        idx = cur->table->primary;
    } else {
        idx = ese_find_index(cur->table, name);
        if (!idx && !lstrcmpiW(cur->table->primary->name, name) && !cur->table->primary->sequential)
            idx = cur->table->primary;
        if (!idx) {
            /* a name that is not an index leaves the cursor on the primary index with no current record. */
            if (!cur->copy) {
                cur->index = cur->table->primary;
                cur->pos = POS_BEFORE;
                cur->has_range = FALSE;
                clear_key(cur);
            }
            return JET_errIndexNotFound;
        }
    }
    if (cur->copy)
        return JET_errAlreadyPrepared;
    RECORD *rec = cursor_record(cur);
    VERSION *v = rec ? ese_visible(rec, cur->sess) : nullptr;
    BOOL was_on = cur->pos == POS_ON && v;
    cur->index = idx;
    cur->has_range = FALSE;
    clear_key(cur);
    if ((grbit & JET_bitNoMove)) {
        if (!was_on) {
            cur->pos = POS_BEFORE;
            return JET_errNoCurrentRecord;
        }
        BYTE **keys;
        ULONG *cbs, count;
        if (idx->primary) {
            ese_free(cur->ekey);
            cur->ekey = ese_dup(rec->pk, rec->pkcb);
            cur->ekeycb = rec->pkcb;
            return JET_errSuccess;
        }
        JET_ERR err = ese_index_keys(cur->table, idx, v, &keys, &cbs, &count);
        if (err || !count) {
            cur->pos = POS_BEFORE;
            ese_free_keys(keys, cbs, count);
            return JET_errNoCurrentRecord;
        }
        ese_free(cur->ekey);
        cur->ekey = ese_dup(keys[0], cbs[0]);
        cur->ekeycb = cbs[0];
        ese_free_keys(keys, cbs, count);
        return JET_errSuccess;
    }
    cur->pos = POS_BEFORE;
    return ese_move(cur, JET_MoveFirst, 0);
}

JET_ERR ese_get_bookmark(CURSOR *cur, void *buf, ULONG cb, ULONG *actual)
{
    RECORD *rec = cursor_record(cur);
    if (!rec || !ese_visible(rec, cur->sess))
        return cur->pos == POS_ON ? JET_errRecordDeleted : JET_errNoCurrentRecord;
    if (actual)
        *actual = rec->pkcb;
    if (buf && cb < rec->pkcb)
        return JET_errBufferTooSmall;
    if (buf)
        memcpy(buf, rec->pk, rec->pkcb);
    return JET_errSuccess;
}

JET_ERR ese_goto_bookmark(CURSOR *cur, const void *buf, ULONG cb)
{
    if (!buf || !cb || cb > JET_cbBookmarkMost)
        return JET_errInvalidBookmark;
    BOOL found;
    INDEX *primary = cur->table->primary;
    LONG at = ese_entry_search(primary, buf, cb, buf, cb, &found);
    if (!found)
        return JET_errRecordDeleted;
    RECORD *rec = primary->entries[at].rec;
    VERSION *v = ese_visible(rec, cur->sess);
    if (!v)
        return JET_errRecordDeleted;
    if (cur->index->primary) {
        set_position(cur, &primary->entries[at]);
        return JET_errSuccess;
    }
    BYTE **keys;
    ULONG *cbs, count;
    JET_ERR err = ese_index_keys(cur->table, cur->index, v, &keys, &cbs, &count);
    if (err || !count) {
        ese_free_keys(keys, cbs, count);
        return JET_errRecordNotFound;
    }
    BOOL f2;
    LONG at2 = ese_entry_search(cur->index, keys[0], cbs[0], rec->pk, rec->pkcb, &f2);
    if (f2)
        set_position(cur, &cur->index->entries[at2]);
    ese_free_keys(keys, cbs, count);
    return f2 ? JET_errSuccess : JET_errRecordNotFound;
}

ULONG ese_visible_count(CURSOR *cur, INDEX *idx, ULONG limit)
{
    ULONG n = 0;
    for (ULONG i = 0; i < idx->count && n < limit; i++)
        if (entry_visible(cur, idx, &idx->entries[i]))
            n++;
    return n;
}

JET_ERR ese_index_count(CURSOR *cur, ULONG *count, ULONG maximum)
{
    ULONG n = 0;
    INDEX *idx = cur->index;
    BOOL found;
    if (cur->pos != POS_ON)
        return JET_errNoCurrentRecord;
    LONG at = locate(cur, &found);
    for (LONG i = at < 0 ? 0 : at; i < (LONG)idx->count; i++) {
        if (!entry_visible(cur, idx, &idx->entries[i]) || !in_range(cur, &idx->entries[i]))
            continue;
        n++;
        if (maximum && n >= maximum)
            break;
    }
    *count = n;
    return JET_errSuccess;
}

ULONG ese_record_estimate(TABLE *table)
{
    return table->primary ? table->primary->count : 0;
}

/* ------------------------------------------------------------------ values */

static void set_cell_values(CELL *cell, VAL *vals, ULONG count)
{
    for (ULONG i = 0; i < cell->count; i++)
        ese_free(cell->vals[i].data);
    ese_free(cell->vals);
    cell->vals = vals;
    cell->count = count;
}

static void remove_value(CELL *cell, ULONG at)
{
    ese_free(cell->vals[at].data);
    memmove(&cell->vals[at], &cell->vals[at + 1], (cell->count - at - 1) * sizeof(VAL));
    cell->count--;
}

JET_ERR ese_set_column(CURSOR *cur, JET_COLUMNID id, const void *data, ULONG cb, JET_GRBIT grbit,
                       const JET_SETINFO *info)
{
    if (!cur->copy)
        return JET_errUpdateNotPrepared;
    if (cur->prep == JET_prepReadOnlyCopy)
        return JET_errPermissionDenied;
    TABLE *table = cur->table;
    COLUMN *col = ese_column_by_id(table, id);
    if (!col)
        return JET_errColumnNotFound;
    ULONG slot = ese_slot(table, id);
    if (col->grbit & (JET_bitColumnAutoincrement | JET_bitColumnVersion))
        return JET_errInvalidColumnType;
    if (cb && !data && !(grbit & JET_bitSetSizeLV))
        return JET_errInvalidParameter;
    if ((col->coltyp == JET_coltypLongBinary || col->coltyp == JET_coltypLongText) && !cur->sess->level && !table->temp)
        return JET_errNotInTransaction;
    cur->copy_dirty = TRUE;
    ULONG itag = info ? info->itagSequence : 1;
    ULONG iblv = info ? info->ibLongValue : 0;
    BOOL multi = (col->grbit & JET_bitColumnMultiValued) != 0;
    BOOL long_value = col->coltyp == JET_coltypLongBinary || col->coltyp == JET_coltypLongText;
    CELL *cell = ese_cell_w(cur->copy, slot);
    ULONG size;
    BOOL fixed = ese_fixed_size(col->coltyp, &size);
    JET_ERR warn = JET_errSuccess;
    if (grbit & JET_bitSetRevertToDefaultValue) {
        set_cell_values(cell, nullptr, 0);
        cell->set = FALSE;
        return JET_errSuccess;
    }
    if (!multi && itag > 1)
        return JET_errBadItagSequence;
    BOOL null = !cb && !(grbit & JET_bitSetZeroLength);
    if (long_value && (grbit & (JET_bitSetAppendLV | JET_bitSetOverwriteLV | JET_bitSetSizeLV)) == 0 && !cb && !data)
        null = !(grbit & JET_bitSetZeroLength);
    if (null && !(grbit & JET_bitSetSizeLV)) {
        if (!multi || !itag || itag > cell->count) {
            if (!multi) {
                set_cell_values(cell, nullptr, 0);
            } else if (itag && itag <= cell->count) {
                remove_value(cell, itag - 1);
            }
        } else {
            remove_value(cell, itag - 1);
        }
        cell->set = TRUE;
        return JET_errSuccess;
    }
    if (fixed && cb != size && !(grbit & JET_bitSetZeroLength))
        return JET_errInvalidBufferSize;
    if (fixed && (grbit & JET_bitSetZeroLength) && cb)
        ;
    if (!long_value && !fixed && cb > col->cbMax) {
        cb = col->cbMax;
        warn = JET_wrnColumnMaxTruncated;
    }
    if (!long_value && col->coltyp == JET_coltypText && col->cp == 1200 && (cb & 1))
        return JET_errInvalidBufferSize;
    ULONG idx = 0;
    BOOL add = FALSE;
    if (multi) {
        if (!itag || itag >= cell->count + 1) {
            add = TRUE;
            idx = cell->count;
        } else {
            idx = itag - 1;
        }
        if (grbit & JET_bitSetUniqueMultiValues)
            for (ULONG i = 0; i < cell->count; i++)
                if (cell->vals[i].cb == cb && !(cb && memcmp(cell->vals[i].data, data, cb)) && i != idx)
                    return JET_errMultiValuedDuplicate;
    } else {
        idx = 0;
        add = !cell->count;
    }
    BYTE *bytes;
    ULONG length;
    if (long_value && (grbit & JET_bitSetSizeLV)) {
        ULONG old = !add && cell->count ? cell->vals[idx].cb : 0;
        bytes = ese_alloc(cb);
        memcpy(bytes, !add && cell->count ? cell->vals[idx].data : (BYTE *)"", old < cb ? old : cb);
        length = cb;
    } else if (long_value && (grbit & JET_bitSetAppendLV) && !add) {
        VAL *old = &cell->vals[idx];
        bytes = ese_alloc(old->cb + cb);
        memcpy(bytes, old->data, old->cb);
        memcpy(bytes + old->cb, data, cb);
        length = old->cb + cb;
    } else if (long_value && (grbit & JET_bitSetOverwriteLV) && !add) {
        VAL *old = &cell->vals[idx];
        length = old->cb > iblv + cb ? old->cb : iblv + cb;
        bytes = ese_alloc(length);
        memcpy(bytes, old->data, old->cb);
        memcpy(bytes + iblv, data, cb);
    } else if (long_value && iblv && !add) {
        VAL *old = &cell->vals[idx];
        length = iblv + cb;
        bytes = ese_alloc(length);
        memcpy(bytes, old->data, old->cb < iblv ? old->cb : iblv);
        memcpy(bytes + iblv, data, cb);
    } else {
        bytes = ese_dup(data, cb);
        length = cb;
    }
    if (add) {
        cell->vals = ese_realloc(cell->vals, (cell->count + 1) * sizeof(VAL));
        cell->vals[cell->count].cb = length;
        cell->vals[cell->count].data = bytes;
        cell->count++;
    } else {
        ese_free(cell->vals[idx].data);
        cell->vals[idx].cb = length;
        cell->vals[idx].data = bytes;
    }
    cell->set = TRUE;
    if (length > 0x7fffffff)
        warn = JET_errInvalidParameter;
    return warn;
}

JET_ERR ese_retrieve_column(CURSOR *cur, JET_COLUMNID id, void *data, ULONG cb, ULONG *actual, JET_GRBIT grbit,
                            const JET_RETINFO *info)
{
    TABLE *table = cur->table;
    COLUMN *col = ese_column_by_id(table, id);
    if (!col)
        return JET_errColumnNotFound;
    const VERSION *v;
    if (grbit & JET_bitRetrieveCopy) {
        if (!cur->copy)
            return JET_errUpdateNotPrepared;
        v = cur->copy;
    } else {
        RECORD *rec = cursor_record(cur);
        if (!rec) {
            if (cur->pos == POS_ON)
                return JET_errRecordDeleted;
            return JET_errNoCurrentRecord;
        }
        v = ese_visible(rec, cur->sess);
        if (!v)
            return JET_errRecordDeleted;
    }
    ULONG slot = ese_slot(table, id);
    const CELL *cell = ese_cell(v, slot);
    ULONG itag = info ? info->itagSequence : 0;
    ULONG iblv = info ? info->ibLongValue : 0;
    if (!itag)
        itag = 1;
    if (actual)
        *actual = 0;
    if (!cell->set && !cell->count && col->defcb) {
        ULONG n = col->defcb;
        if (actual)
            *actual = n;
        if (data && cb)
            memcpy(data, col->def, n < cb ? n : cb);
        return n > cb ? JET_wrnBufferTruncated : JET_errSuccess;
    }
    if (!cell->count || itag > cell->count) {
        if (grbit & JET_bitRetrieveTag) {
            if (actual)
                *actual = 0;
        }
        return JET_wrnColumnNull;
    }
    const VAL *val = &cell->vals[itag - 1];
    ULONG start = 0;
    BOOL long_value = col->coltyp == JET_coltypLongBinary || col->coltyp == JET_coltypLongText;
    if (long_value && iblv) {
        if (iblv > val->cb)
            iblv = val->cb;
        start = iblv;
    }
    ULONG n = val->cb - start;
    if (actual)
        *actual = n;
    if (data && cb)
        memcpy(data, val->data + start, n < cb ? n : cb);
    if (n > cb)
        return JET_wrnBufferTruncated;
    return JET_errSuccess;
}

/* ------------------------------------------------------------------ updates */

static void bump_version_columns(TABLE *table, VERSION *copy)
{
    for (ULONG i = 0; i < table->ncols; i++) {
        COLUMN *col = table->cols[i];
        if (col->deleted || !(col->grbit & JET_bitColumnVersion))
            continue;
        CELL *cell = ese_cell_w(copy, i);
        LONG value = 1;
        if (cell->count)
            value = *(LONG *)cell->vals[0].data + 1;
        BYTE *bytes = ese_dup(&value, 4);
        VAL *vals = ese_alloc(sizeof(VAL));
        vals[0].cb = 4;
        vals[0].data = bytes;
        set_cell_values(cell, vals, 1);
        cell->set = TRUE;
    }
}

JET_ERR ese_prepare(CURSOR *cur, ULONG prep)
{
    TABLE *table = cur->table;
    SESSION *sess = cur->sess;
    if (prep == JET_prepCancel) {
        if (!cur->copy)
            return JET_errUpdateNotPrepared;
        ese_free_cells(table, cur->copy);
        ese_free(cur->copy);
        cur->copy = nullptr;
        if (cur->copy_rec) {
            if (cur->copy_rec->lock == sess && !sess->level)
                cur->copy_rec->lock = nullptr;
            cur->copy_rec = nullptr;
        }
        cur->prep = 0;
        return JET_errSuccess;
    }
    if (prep != JET_prepInsert && prep != JET_prepReplace && prep != JET_prepReplaceNoLock &&
        prep != JET_prepInsertCopy && prep != JET_prepInsertCopyDeleteOriginal && prep != JET_prepReadOnlyCopy)
        return JET_errInvalidParameter;
    if (cur->copy)
        return JET_errAlreadyPrepared;
    if (table->db && table->db->readonly && prep != JET_prepReadOnlyCopy)
        return JET_errPermissionDenied;
    if (prep == JET_prepInsert) {
        VERSION *copy = ese_new_version(table);
        for (ULONG i = 0; i < table->ncols; i++) {
            COLUMN *col = table->cols[i];
            if (col->deleted)
                continue;
            CELL *cell = ese_cell_w(copy, i);
            if (col->grbit & JET_bitColumnAutoincrement) {
                col->autoinc++;
                ULONG size = col->coltyp == JET_coltypCurrency ? 8 : 4;
                LONGLONG value = col->autoinc;
                cell->vals = ese_alloc(sizeof(VAL));
                cell->vals[0].cb = size;
                cell->vals[0].data = ese_dup(&value, size);
                cell->count = 1;
                cell->set = TRUE;
            } else if ((col->grbit & JET_bitColumnVersion)) {
                LONG value = 0;
                cell->vals = ese_alloc(sizeof(VAL));
                cell->vals[0].cb = 4;
                cell->vals[0].data = ese_dup(&value, 4);
                cell->count = 1;
                cell->set = TRUE;
            } else if (col->defcb && !(col->grbit & JET_bitColumnTagged)) {
                cell->vals = ese_alloc(sizeof(VAL));
                cell->vals[0].cb = col->defcb;
                cell->vals[0].data = ese_dup(col->def, col->defcb);
                cell->count = 1;
                cell->set = TRUE;
            } else if ((col->grbit & JET_bitColumnEscrowUpdate)) {
                LONG zero = 0;
                cell->vals = ese_alloc(sizeof(VAL));
                cell->vals[0].cb = 4;
                cell->vals[0].data = ese_dup(&zero, 4);
                cell->count = 1;
                cell->set = TRUE;
            }
        }
        cur->copy = copy;
        cur->copy_dirty = TRUE;
        cur->prep = prep;
        cur->copy_new = TRUE;
        cur->copy_rec = nullptr;
        return JET_errSuccess;
    }
    RECORD *rec = cursor_record(cur);
    if (!rec)
        return cur->pos == POS_ON ? JET_errRecordDeleted : JET_errNoCurrentRecord;
    VERSION *v = ese_visible(rec, sess);
    if (!v)
        return JET_errRecordDeleted;
    if (prep == JET_prepReplace) {
        JET_ERR err = check_write(rec, sess);
        if (err)
            return err;
        /* only a transaction keeps the record for the update, outside one the conflict shows at the update. */
        if (sess->level) {
            rec->lock = sess;
            rec->lock_level = sess->level;
            UNDO *u = ese_log(sess, U_LOCK);
            u->table = table;
            u->rec = rec;
        }
    }
    cur->copy = ese_copy_version(table, v);
    cur->copy_dirty = FALSE;
    cur->prep = prep;
    cur->copy_new = prep == JET_prepInsertCopy || prep == JET_prepInsertCopyDeleteOriginal;
    cur->copy_rec = prep == JET_prepReplace || prep == JET_prepReplaceNoLock || prep == JET_prepInsertCopyDeleteOriginal
                        ? rec
                        : nullptr;
    if (cur->copy_new) {
        /* a copy gets new values of the columns the table makes by itself. */
        for (ULONG i = 0; i < table->ncols; i++) {
            COLUMN *col = table->cols[i];
            if (col->deleted)
                continue;
            if (col->grbit & JET_bitColumnAutoincrement) {
                CELL *cell = ese_cell_w(cur->copy, i);
                col->autoinc++;
                ULONG size = col->coltyp == JET_coltypCurrency ? 8 : 4;
                LONGLONG value = col->autoinc;
                set_cell_values(cell, ese_alloc(sizeof(VAL)), 1);
                cell->vals[0].cb = size;
                cell->vals[0].data = ese_dup(&value, size);
            }
        }
        if (prep == JET_prepInsertCopy)
            cur->copy_rec = nullptr;
    }
    return JET_errSuccess;
}

/* the primary key of a version, or a new sequential number for a table without one. */
static JET_ERR primary_key(TABLE *table, const VERSION *ver, BYTE **key, ULONG *cb)
{
    INDEX *primary = table->primary;
    if (primary->sequential) {
        BYTE seq[4];
        ULONG id = (ULONG)table->next_id++;
        seq[0] = (BYTE)(id >> 24);
        seq[1] = (BYTE)(id >> 16);
        seq[2] = (BYTE)(id >> 8);
        seq[3] = (BYTE)id;
        *key = ese_dup(seq, 4);
        *cb = 4;
        return JET_errSuccess;
    }
    BYTE **keys;
    ULONG *cbs, count;
    JET_ERR err = ese_index_keys(table, primary, ver, &keys, &cbs, &count);
    if (err)
        return err;
    if (!count)
        return JET_errNullKeyDisallowed;
    *key = ese_dup(keys[0], cbs[0]);
    *cb = cbs[0];
    ese_free_keys(keys, cbs, count);
    return JET_errSuccess;
}

static void insert_entry(INDEX *idx, LONG at, BYTE *key, ULONG cb, RECORD *rec)
{
    if (idx->count == idx->cap) {
        idx->cap = idx->cap ? idx->cap * 2 : 16;
        idx->entries = ese_realloc(idx->entries, idx->cap * sizeof(ENTRY));
    }
    memmove(&idx->entries[at + 1], &idx->entries[at], (idx->count - at) * sizeof(ENTRY));
    idx->entries[at].key = key;
    idx->entries[at].cb = cb;
    idx->entries[at].rec = rec;
    idx->count++;
}

/* whether a secondary index would hold the same key twice, which a unique index refuses. */
static JET_ERR check_unique(TABLE *table, INDEX *idx, RECORD *rec, SESSION *sess, const VERSION *ver)
{
    if (!(idx->grbit & JET_bitIndexUnique))
        return JET_errSuccess;
    BYTE **keys;
    ULONG *cbs, count;
    JET_ERR err = ese_index_keys(table, idx, ver, &keys, &cbs, &count);
    if (err)
        return err;
    for (ULONG k = 0; k < count && !err; k++) {
        LONG at = ese_entry_search(idx, keys[k], cbs[k], (const BYTE *)"", 0, nullptr);
        for (; (ULONG)at < idx->count; at++) {
            ENTRY *e = &idx->entries[at];
            if (e->cb != cbs[k] || memcmp(e->key, keys[k], cbs[k]))
                break;
            if (e->rec == rec)
                continue;
            VERSION *head = e->rec->head;
            if (!head)
                continue;
            if (!head->commit && head->owner != sess) {
                err = head->deleted ? JET_errWriteConflict : JET_errKeyDuplicate;
                break;
            }
            if (!head->deleted) {
                /* the head might have another key by now. */
                BYTE **hk;
                ULONG *hc, hn;
                if (!ese_index_keys(table, idx, head, &hk, &hc, &hn)) {
                    for (ULONG j = 0; j < hn; j++)
                        if (hc[j] == cbs[k] && !memcmp(hk[j], keys[k], cbs[k]))
                            err = JET_errKeyDuplicate;
                    ese_free_keys(hk, hc, hn);
                }
                if (err)
                    break;
            }
        }
    }
    ese_free_keys(keys, cbs, count);
    return err;
}

static JET_ERR add_entries(TABLE *table, RECORD *rec, const VERSION *ver)
{
    for (ULONG i = 0; i < table->nidx; i++) {
        INDEX *idx = table->idx[i];
        if (idx->primary || idx->deleted)
            continue;
        BYTE **keys;
        ULONG *cbs, count;
        JET_ERR err = ese_index_keys(table, idx, ver, &keys, &cbs, &count);
        if (err)
            return err;
        for (ULONG k = 0; k < count; k++) {
            BOOL found;
            LONG at = ese_entry_search(idx, keys[k], cbs[k], rec->pk, rec->pkcb, &found);
            if (found) {
                ese_free(keys[k]);
                continue;
            }
            insert_entry(idx, at, keys[k], cbs[k], rec);
        }
        ese_free(keys);
        ese_free(cbs);
    }
    return JET_errSuccess;
}

static JET_ERR check_columns(TABLE *table, const VERSION *copy)
{
    for (ULONG i = 0; i < table->ncols; i++) {
        COLUMN *col = table->cols[i];
        if (col->deleted)
            continue;
        const CELL *cell = ese_cell(copy, i);
        if ((col->grbit & JET_bitColumnNotNULL) && !cell->count && !(col->grbit & JET_bitColumnAutoincrement))
            return JET_errNullInvalid;
    }
    return JET_errSuccess;
}

JET_ERR ese_update(CURSOR *cur, void *bookmark, ULONG cb, ULONG *actual, JET_GRBIT grbit)
{
    TABLE *table = cur->table;
    SESSION *sess = cur->sess;
    if (!cur->copy)
        return JET_errUpdateNotPrepared;
    if (cur->prep == JET_prepReadOnlyCopy)
        return JET_errPermissionDenied;
    JET_ERR err = check_columns(table, cur->copy);
    if (err)
        return err;
    /* an update that changed nothing leaves the record as it is. */
    if (!cur->copy_dirty && (cur->prep == JET_prepReplace || cur->prep == JET_prepReplaceNoLock)) {
        RECORD *same = cur->copy_rec;
        ese_free_cells(table, cur->copy);
        ese_free(cur->copy);
        cur->copy = nullptr;
        if (same && same->lock == sess && !sess->level)
            same->lock = nullptr;
        cur->copy_rec = nullptr;
        cur->prep = 0;
        if (actual)
            *actual = 0;
        return JET_errSuccess;
    }
    BOOL implicit = !sess->level;
    if (implicit)
        ese_begin(sess, FALSE);
    BOOL replace = cur->prep == JET_prepReplace || cur->prep == JET_prepReplaceNoLock;
    RECORD *rec = nullptr;
    VERSION *ver = nullptr;
    BYTE *pk = nullptr;
    ULONG pkcb = 0;
    if (replace) {
        rec = cur->copy_rec;
        BOOL found;
        INDEX *primary = table->primary;
        if (rec) {
            ese_entry_search(primary, rec->pk, rec->pkcb, rec->pk, rec->pkcb, &found);
            if (!found)
                err = JET_errRecordDeleted;
        }
        if (!err)
            err = check_write(rec, sess);
        if (!err && !primary->sequential) {
            err = primary_key(table, cur->copy, &pk, &pkcb);
            if (!err && (pkcb != rec->pkcb || memcmp(pk, rec->pk, pkcb))) {
                /* the key of the record changed, which makes it a new record in place of the old. */
                ese_free(pk);
                pk = nullptr;
                replace = FALSE;
                VERSION *dead = ese_new_version(table);
                dead->deleted = TRUE;
                dead->owner = sess;
                dead->level = sess->level;
                dead->older = rec->head;
                rec->head = dead;
                UNDO *u = ese_log(sess, U_VERSION);
                u->table = table;
                u->rec = rec;
                u->ver = dead;
                cur->copy_new = TRUE;
                rec = nullptr;
            } else {
                ese_free(pk);
                pk = nullptr;
            }
        }
        if (err) {
            if (implicit)
                ese_rollback(sess, TRUE);
            return err;
        }
    }
    if (replace) {
        /* the escrow columns keep what the other sessions made of them, not the copy's old values. */
        VERSION *latest = nullptr;
        for (VERSION *v = rec->head; v; v = v->older)
            if (v->commit || v->owner == sess) {
                latest = v;
                break;
            }
        for (ULONG i = 0; latest && i < table->ncols; i++) {
            COLUMN *col = table->cols[i];
            if (col->deleted || !(col->grbit & JET_bitColumnEscrowUpdate))
                continue;
            const CELL *src = ese_cell(latest, i);
            CELL *dst = ese_cell_w(cur->copy, i);
            if (src->count) {
                set_cell_values(dst, ese_alloc(sizeof(VAL)), 1);
                dst->vals[0].cb = src->vals[0].cb;
                dst->vals[0].data = ese_dup(src->vals[0].data, src->vals[0].cb);
            }
        }
        bump_version_columns(table, cur->copy);
        VERSION *own = rec->head && !rec->head->commit && rec->head->owner == sess && rec->head->level == sess->level
                           ? rec->head
                           : nullptr;
        for (ULONG i = 0; i < table->nidx; i++) {
            INDEX *idx = table->idx[i];
            if (idx->primary || idx->deleted)
                continue;
            err = check_unique(table, idx, rec, sess, cur->copy);
            if (err)
                break;
            BYTE **keys;
            ULONG *cbs, count;
            JET_ERR kerr = ese_index_keys(table, idx, cur->copy, &keys, &cbs, &count);
            if (kerr) {
                err = kerr;
                break;
            }
            ese_free_keys(keys, cbs, count);
        }
        if (err) {
            if (implicit)
                ese_rollback(sess, TRUE);
            return err;
        }
        if (own) {
            ese_free_cells(table, own);
            own->cells = cur->copy->cells;
            own->ncells = cur->copy->ncells;
            cur->copy->cells = nullptr;
            cur->copy->ncells = 0;
            ver = own;
        } else {
            ver = cur->copy;
            cur->copy = nullptr;
            ver->owner = sess;
            ver->level = sess->level;
            ver->older = rec->head;
            rec->head = ver;
            UNDO *u = ese_log(sess, U_VERSION);
            u->table = table;
            u->rec = rec;
            u->ver = ver;
        }
        err = add_entries(table, rec, ver);
        if (!err) {
            pk = rec->pk;
            pkcb = rec->pkcb;
        }
    } else {
        BOOL dropped = FALSE;
        err = primary_key(table, cur->copy, &pk, &pkcb);
        if (!err) {
            BOOL found;
            INDEX *primary = table->primary;
            LONG at = ese_entry_search(primary, pk, pkcb, pk, pkcb, &found);
            if (found) {
                RECORD *existing = primary->entries[at].rec;
                VERSION *head = existing->head;
                if (head && !head->commit && head->owner != sess)
                    err = head->deleted ? JET_errWriteConflict : JET_errKeyDuplicate;
                else if (head && !head->deleted && table->temp)
                    dropped = TRUE;
                else if (head && !head->deleted)
                    err = JET_errKeyDuplicate;
                else
                    rec = existing;
            }
            if (!err && dropped) {
                /* a sort drops the second record of a key. */
                ese_free(pk);
                pk = nullptr;
                pkcb = 0;
            } else if (!err) {
                BOOL isnew = !rec;
                if (isnew) {
                    rec = ese_alloc(sizeof(*rec));
                    rec->pk = pk;
                    rec->pkcb = pkcb;
                    pk = nullptr;
                } else {
                    ese_free(pk);
                    pk = nullptr;
                }
                for (ULONG i = 0; i < table->nidx && !err; i++) {
                    INDEX *idx = table->idx[i];
                    if (!idx->primary && !idx->deleted)
                        err = check_unique(table, idx, rec, sess, cur->copy);
                }
                if (err) {
                    if (isnew) {
                        ese_free(rec->pk);
                        ese_free(rec);
                        rec = nullptr;
                    }
                } else {
                    ver = cur->copy;
                    cur->copy = nullptr;
                    ver->owner = sess;
                    ver->level = sess->level;
                    ver->older = rec->head;
                    rec->head = ver;
                    if (isnew)
                        insert_entry(primary, at, ese_dup(rec->pk, rec->pkcb), rec->pkcb, rec);
                    UNDO *u = ese_log(sess, U_VERSION);
                    u->table = table;
                    u->rec = rec;
                    u->ver = ver;
                    err = add_entries(table, rec, ver);
                    pk = rec->pk;
                    pkcb = rec->pkcb;
                }
            } else {
                ese_free(pk);
            }
        }
    }
    if (err) {
        if (implicit)
            ese_rollback(sess, TRUE);
        return err;
    }
    if (actual)
        *actual = pkcb;
    if (bookmark && cb >= pkcb)
        memcpy(bookmark, pk, pkcb);
    JET_ERR result = bookmark && cb < pkcb ? JET_errBufferTooSmall : JET_errSuccess;
    if (cur->copy) {
        ese_free_cells(table, cur->copy);
        ese_free(cur->copy);
        cur->copy = nullptr;
    }
    if (cur->prep == JET_prepInsertCopyDeleteOriginal && cur->copy_rec) {
        VERSION *dead = ese_new_version(table);
        dead->deleted = TRUE;
        dead->owner = sess;
        dead->level = sess->level;
        dead->older = cur->copy_rec->head;
        cur->copy_rec->head = dead;
        UNDO *u = ese_log(sess, U_VERSION);
        u->table = table;
        u->rec = cur->copy_rec;
        u->ver = dead;
    }
    if (cur->copy_rec && cur->copy_rec->lock == sess && !sess->level)
        cur->copy_rec->lock = nullptr;
    cur->copy_rec = nullptr;
    cur->prep = 0;
    if (implicit)
        ese_commit(sess, grbit);
    return result;
}

JET_ERR ese_delete(CURSOR *cur)
{
    TABLE *table = cur->table;
    SESSION *sess = cur->sess;
    if (cur->copy)
        return JET_errAlreadyPrepared;
    if (table->db && table->db->readonly)
        return JET_errPermissionDenied;
    RECORD *rec = cursor_record(cur);
    if (!rec)
        return cur->pos == POS_ON ? JET_errRecordDeleted : JET_errNoCurrentRecord;
    JET_ERR err = check_write(rec, sess);
    if (err)
        return err;
    BOOL implicit = !sess->level;
    if (implicit)
        ese_begin(sess, FALSE);
    VERSION *dead = ese_new_version(table);
    dead->deleted = TRUE;
    dead->owner = sess;
    dead->level = sess->level;
    dead->older = rec->head;
    rec->head = dead;
    UNDO *u = ese_log(sess, U_VERSION);
    u->table = table;
    u->rec = rec;
    u->ver = dead;
    if (implicit)
        ese_commit(sess, 0);
    return JET_errSuccess;
}

JET_ERR ese_escrow(CURSOR *cur, JET_COLUMNID id, void *delta, ULONG deltacb, void *old, ULONG oldcb, ULONG *oldactual,
                   JET_GRBIT grbit)
{
    TABLE *table = cur->table;
    SESSION *sess = cur->sess;
    COLUMN *col = ese_column_by_id(table, id);
    if (!col)
        return JET_errColumnNotFound;
    if (!(col->grbit & JET_bitColumnEscrowUpdate))
        return JET_errInvalidColumnType;
    if (deltacb != 4 || !delta)
        return JET_errInvalidBufferSize;
    if (cur->copy)
        return JET_errAlreadyPrepared;
    if (!sess->level)
        return JET_errNotInTransaction;
    RECORD *rec = cursor_record(cur);
    if (!rec)
        return cur->pos == POS_ON ? JET_errRecordDeleted : JET_errNoCurrentRecord;
    if (!ese_visible(rec, sess))
        return JET_errRecordDeleted;
    VERSION *latest = nullptr;
    for (VERSION *v = rec->head; v; v = v->older)
        if (v->commit || v->owner == sess) {
            latest = v;
            break;
        }
    if (!latest || latest->deleted)
        return JET_errRecordDeleted;
    ULONG slot = ese_slot(table, id);
    CELL *cell = ese_cell_w(latest, slot);
    LONG before = 0;
    if (cell->count)
        before = *(LONG *)cell->vals[0].data;
    else {
        set_cell_values(cell, ese_alloc(sizeof(VAL)), 1);
        cell->vals[0].cb = 4;
        cell->vals[0].data = ese_alloc(4);
    }
    LONG d = *(LONG *)delta;
    *(LONG *)cell->vals[0].data = before + d;
    cell->set = TRUE;
    if (old && oldcb >= 4)
        memcpy(old, &before, 4);
    if (oldactual)
        *oldactual = 4;
    BOOL implicit = !sess->level;
    if (implicit)
        ese_begin(sess, FALSE);
    UNDO *u = ese_log(sess, U_ESCROW);
    u->table = table;
    u->rec = rec;
    u->col = id;
    u->delta = (grbit & JET_bitEscrowNoRollback) ? 0 : d;
    if (implicit)
        ese_commit(sess, 0);
    return JET_errSuccess;
}
