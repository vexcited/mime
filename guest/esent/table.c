/* tables, columns and indexes: the schema of a database and the lists of values a record is made of. */
#include "ese.h"

const CELL *ese_cell(const VERSION *ver, ULONG slot)
{
    static const CELL empty;
    return slot < ver->ncells ? &ver->cells[slot] : &empty;
}

CELL *ese_cell_w(VERSION *ver, ULONG slot)
{
    if (slot >= ver->ncells) {
        ULONG n = slot + 8;
        ver->cells = ese_realloc(ver->cells, n * sizeof(CELL));
        ver->ncells = n;
    }
    return &ver->cells[slot];
}

static void free_cell(CELL *cell)
{
    for (ULONG i = 0; i < cell->count; i++)
        ese_free(cell->vals[i].data);
    ese_free(cell->vals);
    cell->vals = nullptr;
    cell->count = 0;
}

void ese_free_cells(TABLE *table, VERSION *ver)
{
    for (ULONG i = 0; i < ver->ncells; i++)
        free_cell(&ver->cells[i]);
    ese_free(ver->cells);
    ver->cells = nullptr;
    ver->ncells = 0;
}

VERSION *ese_new_version(TABLE *table)
{
    VERSION *ver = ese_alloc(sizeof(*ver));
    ver->ncells = table->ncols;
    ver->cells = ese_alloc((ver->ncells ? ver->ncells : 1) * sizeof(CELL));
    return ver;
}

VERSION *ese_copy_version(TABLE *table, const VERSION *from)
{
    VERSION *ver = ese_new_version(table);
    if (from->ncells > ver->ncells) {
        ver->cells = ese_realloc(ver->cells, from->ncells * sizeof(CELL));
        ver->ncells = from->ncells;
    }
    for (ULONG i = 0; i < from->ncells; i++) {
        const CELL *src = &from->cells[i];
        CELL *dst = &ver->cells[i];
        dst->set = src->set;
        if (!src->count)
            continue;
        dst->vals = ese_alloc(src->count * sizeof(VAL));
        dst->count = src->count;
        for (ULONG v = 0; v < src->count; v++) {
            dst->vals[v].cb = src->vals[v].cb;
            dst->vals[v].data = ese_dup(src->vals[v].data, src->vals[v].cb);
        }
    }
    return ver;
}

BOOL ese_ansi(const COLUMN *col)
{
    return (col->coltyp == JET_coltypText || col->coltyp == JET_coltypLongText) && col->cp != 1200;
}

ULONG ese_slot(TABLE *table, JET_COLUMNID id)
{
    for (ULONG i = 0; i < table->ncols; i++)
        if (table->cols[i]->id == id)
            return i;
    return ~0u;
}

COLUMN *ese_column_by_id(TABLE *table, JET_COLUMNID id)
{
    for (ULONG i = 0; i < table->ncols; i++)
        if (table->cols[i]->id == id && !table->cols[i]->deleted)
            return table->cols[i];
    return nullptr;
}

COLUMN *ese_find_column(TABLE *table, LPCWSTR name)
{
    for (ULONG i = 0; i < table->ncols; i++)
        if (!table->cols[i]->deleted && !lstrcmpiW(table->cols[i]->name, name))
            return table->cols[i];
    return nullptr;
}

INDEX *ese_find_index(TABLE *table, LPCWSTR name)
{
    for (ULONG i = 0; i < table->nidx; i++)
        if (!table->idx[i]->deleted && !table->idx[i]->sequential && !lstrcmpiW(table->idx[i]->name, name))
            return table->idx[i];
    return nullptr;
}

TABLE *ese_find_table(DB *db, LPCWSTR name)
{
    for (TABLE *t = db->tables; t; t = t->next)
        if (!t->deleted && !lstrcmpiW(t->name, name))
            return t;
    return nullptr;
}

static BOOL valid_name(LPCWSTR name)
{
    if (!name)
        return FALSE;
    ULONG length = (ULONG)lstrlenW(name);
    if (!length || length > JET_cbNameMost || name[0] == L' ')
        return FALSE;
    for (ULONG i = 0; i < length; i++)
        if (name[i] < 0x20 || name[i] == L'!' || name[i] == L',' || name[i] == L'[' || name[i] == L']' ||
            name[i] == L'\\' || name[i] == L'/' || name[i] == L'.')
            return FALSE;
    return TRUE;
}

BOOL ese_valid_name(LPCWSTR name)
{
    return valid_name(name);
}

static INDEX *new_index(TABLE *table)
{
    INDEX *idx = ese_alloc(sizeof(*idx));
    if (table->nidx == table->nicap) {
        table->nicap = table->nicap ? table->nicap * 2 : 8;
        table->idx = ese_realloc(table->idx, table->nicap * sizeof(INDEX *));
    }
    table->idx[table->nidx++] = idx;
    idx->objid = table->db ? table->db->next_objid++ : 0;
    return idx;
}

static void free_index(INDEX *idx)
{
    for (ULONG i = 0; i < idx->count; i++)
        ese_free(idx->entries[i].key);
    ese_free(idx->entries);
    ese_free(idx);
}

void ese_free_table(TABLE *table)
{
    for (ULONG i = 0; i < table->nidx; i++) {
        INDEX *idx = table->idx[i];
        if (idx == table->primary || idx->primary) {
            /* the records are freed with the primary index. */
        }
    }
    INDEX *primary = table->primary;
    if (primary)
        while (primary->count) {
            RECORD *rec = primary->entries[0].rec;
            for (ULONG i = 0; i < table->nidx; i++) {
                INDEX *idx = table->idx[i];
                if (idx != primary)
                    for (ULONG e = 0; e < idx->count;) {
                        if (idx->entries[e].rec == rec) {
                            ese_free(idx->entries[e].key);
                            memmove(&idx->entries[e], &idx->entries[e + 1], (idx->count - e - 1) * sizeof(ENTRY));
                            idx->count--;
                        } else {
                            e++;
                        }
                    }
            }
            while (rec->head) {
                VERSION *v = rec->head;
                rec->head = v->older;
                ese_free_cells(table, v);
                ese_free(v);
            }
            ese_free(rec->pk);
            ese_free(rec);
            ese_free(primary->entries[0].key);
            memmove(&primary->entries[0], &primary->entries[1], (primary->count - 1) * sizeof(ENTRY));
            primary->count--;
        }
    for (ULONG i = 0; i < table->nidx; i++)
        free_index(table->idx[i]);
    ese_free(table->idx);
    for (ULONG i = 0; i < table->ncols; i++) {
        ese_free(table->cols[i]->def);
        ese_free(table->cols[i]);
    }
    ese_free(table->cols);
    ese_free(table);
}

/* ------------------------------------------------------------------ tables */

JET_ERR ese_create_table(SESSION *sess, DBH *dbh, LPCWSTR name, ULONG pages, ULONG density, TABLE **out)
{
    DB *db = dbh->db;
    if (db->readonly)
        return JET_errPermissionDenied;
    if (!valid_name(name))
        return JET_errInvalidName;
    if (ese_find_table(db, name))
        return JET_errTableDuplicate;
    TABLE *table = ese_alloc(sizeof(*table));
    table->db = db;
    lstrcpyW(table->name, name);
    table->next_id = 1;
    table->last_var = 127;
    table->last_tagged = 255;
    table->pages = pages ? pages : 1;
    table->density = density ? density : 100;
    table->objid = db->next_objid++;
    table->primary = new_index(table);
    table->primary->primary = TRUE;
    table->primary->sequential = TRUE;
    table->primary->name[0] = 0;
    table->next = db->tables;
    db->tables = table;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    table->created = table->updated = ((ULONG64)now.dwHighDateTime << 32) | now.dwLowDateTime;
    if (sess->level) {
        UNDO *u = ese_log(sess, U_CREATE_TABLE);
        u->table = table;
    }
    db->dirty = TRUE;
    if (out)
        *out = table;
    return JET_errSuccess;
}

JET_ERR ese_delete_table(SESSION *sess, DBH *dbh, LPCWSTR name)
{
    DB *db = dbh->db;
    if (db->readonly)
        return JET_errPermissionDenied;
    if (!name || !valid_name(name))
        return JET_errInvalidName;
    TABLE *table = ese_find_table(db, name);
    if (!table)
        return JET_errObjectNotFound;
    if (table->refs)
        return JET_errTableInUse;
    table->deleted = TRUE;
    if (sess->level) {
        UNDO *u = ese_log(sess, U_DELETE_TABLE);
        u->table = table;
        /* the table stays in the list until the transaction ends, hidden from every lookup. */
    } else {
        for (TABLE **link = &db->tables; *link; link = &(*link)->next)
            if (*link == table) {
                *link = table->next;
                break;
            }
        ese_free_table(table);
    }
    db->dirty = TRUE;
    return JET_errSuccess;
}

JET_ERR ese_rename_table(SESSION *sess, DBH *dbh, LPCWSTR from, LPCWSTR to)
{
    DB *db = dbh->db;
    if (db->readonly)
        return JET_errPermissionDenied;
    if (!valid_name(from) || !valid_name(to))
        return JET_errInvalidName;
    TABLE *table = ese_find_table(db, from);
    if (!table)
        return JET_errObjectNotFound;
    if (ese_find_table(db, to) && lstrcmpiW(from, to))
        return JET_errTableDuplicate;
    if (table->refs)
        return JET_errTableInUse;
    if (sess->level) {
        UNDO *u = ese_log(sess, U_RENAME_TABLE);
        u->table = table;
        lstrcpyW(u->name, table->name);
    }
    lstrcpyW(table->name, to);
    db->dirty = TRUE;
    return JET_errSuccess;
}

/* ------------------------------------------------------------------ columns */

static BOOL type_valid(JET_COLTYP type)
{
    return type >= JET_coltypBit && type <= JET_coltypUnsignedShort && type != JET_coltypSLV;
}

JET_ERR ese_add_column(SESSION *sess, TABLE *table, LPCWSTR name, const JET_COLUMNDEF *def, const void *dv, ULONG dvcb,
                       JET_COLUMNID *id)
{
    if (table->db->readonly)
        return JET_errPermissionDenied;
    if (!valid_name(name))
        return JET_errInvalidName;
    if (!def || def->cbStruct != sizeof(JET_COLUMNDEF))
        return JET_errInvalidParameter;
    if (ese_find_column(table, name))
        return JET_errColumnDuplicate;
    JET_COLTYP type = def->coltyp;
    if (!type_valid(type))
        return JET_errInvalidColumnType;
    JET_GRBIT grbit = def->grbit;
    ULONG size;
    BOOL fixed_type = ese_fixed_size(type, &size);
    BOOL long_value = type == JET_coltypLongBinary || type == JET_coltypLongText;
    BOOL tagged = (grbit & JET_bitColumnTagged) || long_value;
    if ((grbit & JET_bitColumnTagged) && (grbit & JET_bitColumnFixed))
        return JET_errInvalidParameter;
    if ((grbit & JET_bitColumnMultiValued) && !tagged)
        return JET_errMultiValuedColumnMustBeTagged;
    if ((grbit & JET_bitColumnEscrowUpdate) && tagged)
        return JET_errCannotBeTagged;
    if ((grbit & JET_bitColumnEscrowUpdate) && (type != JET_coltypLong || (grbit & JET_bitColumnMultiValued)))
        return JET_errInvalidGrbit;
    if ((grbit & JET_bitColumnAutoincrement) && ((type != JET_coltypLong && type != JET_coltypCurrency) || tagged ||
                                                  (grbit & JET_bitColumnMultiValued)))
        return JET_errInvalidGrbit;
    if ((grbit & JET_bitColumnVersion) && (type != JET_coltypLong || tagged))
        return JET_errInvalidGrbit;
    if ((grbit & JET_bitColumnAutoincrement) && (grbit & JET_bitColumnVersion))
        return JET_errInvalidGrbit;
    if ((grbit & JET_bitColumnNotNULL) && tagged && (grbit & JET_bitColumnMultiValued))
        return JET_errInvalidGrbit;
    ULONG cbmax = def->cbMax;
    if (fixed_type) {
        cbmax = size;
    } else if (!long_value) {
        if (!cbmax)
            cbmax = 255;
        if (cbmax > JET_cbColumnMost)
            return JET_errInvalidParameter;
    } else {
        if (!cbmax)
            cbmax = 0;
    }
    USHORT cp = def->cp;
    if (type == JET_coltypText || type == JET_coltypLongText) {
        if (!cp)
            cp = 1200;
        if (cp != 1200 && cp != 1252)
            return JET_errInvalidCodePage;
    } else {
        cp = 0;
    }
    JET_COLUMNID colid;
    if (tagged) {
        if (table->last_tagged - 255 >= JET_ccolTaggedMost)
            return JET_errTooManyColumns;
        colid = ++table->last_tagged;
    } else if (fixed_type) {
        if (table->last_fixed >= 127)
            return JET_errTooManyColumns;
        colid = ++table->last_fixed;
    } else {
        if (table->last_var >= 255)
            return JET_errTooManyColumns;
        colid = ++table->last_var;
    }
    ULONG default_cb = dvcb;
    if (dv && dvcb) {
        if (fixed_type && dvcb != size)
            return JET_errInvalidBufferSize;
        if (!fixed_type && !long_value && dvcb > cbmax)
            return JET_errColumnTooBig;
    }
    if (table->ncols == table->ncap) {
        table->ncap = table->ncap ? table->ncap * 2 : 16;
        table->cols = ese_realloc(table->cols, table->ncap * sizeof(COLUMN *));
    }
    COLUMN *col = ese_alloc(sizeof(*col));
    col->id = colid;
    lstrcpyW(col->name, name);
    col->coltyp = type;
    col->cbMax = cbmax;
    col->grbit = grbit & ~(JET_bitColumnFixed);
    if (tagged)
        col->grbit |= JET_bitColumnTagged;
    else if (fixed_type || type == JET_coltypBinary || type == JET_coltypText)
        col->grbit |= (fixed_type ? JET_bitColumnFixed : 0);
    col->cp = cp;
    col->langid = def->langid ? def->langid : (type == JET_coltypText || type == JET_coltypLongText ? 0x409 : 0);
    col->country = def->wCountry;
    col->collate = def->wCollate;
    if (dv && default_cb) {
        col->def = ese_dup(dv, default_cb);
        col->defcb = default_cb;
    } else if ((grbit & JET_bitColumnEscrowUpdate) && !dv) {
        col->def = ese_alloc(4);
        col->defcb = 4;
    }
    if (grbit & JET_bitColumnAutoincrement)
        col->autoinc = 0;
    table->cols[table->ncols++] = col;
    if (sess->level) {
        UNDO *u = ese_log(sess, U_ADD_COLUMN);
        u->table = table;
        u->col = colid;
    }
    table->db->dirty = TRUE;
    if (id)
        *id = colid;
    return JET_errSuccess;
}

JET_ERR ese_delete_column(SESSION *sess, TABLE *table, LPCWSTR name, JET_GRBIT grbit)
{
    if (table->db->readonly)
        return JET_errPermissionDenied;
    COLUMN *col = ese_find_column(table, name);
    if (!col)
        return JET_errColumnNotFound;
    for (ULONG i = 0; i < table->nidx; i++) {
        INDEX *idx = table->idx[i];
        if (idx->deleted)
            continue;
        for (ULONG s = 0; s < idx->nsegs; s++)
            if (idx->segs[s].id == col->id)
                return JET_errColumnInUse;
        for (ULONG s = 0; s < idx->ncond; s++)
            if (idx->cond[s].id == col->id)
                return JET_errColumnInUse;
    }
    col->deleted = TRUE;
    if (sess->level) {
        UNDO *u = ese_log(sess, U_DELETE_COLUMN);
        u->table = table;
        u->col = col->id;
    }
    table->db->dirty = TRUE;
    return JET_errSuccess;
}

JET_ERR ese_rename_column(SESSION *sess, TABLE *table, LPCWSTR from, LPCWSTR to)
{
    if (table->db->readonly)
        return JET_errPermissionDenied;
    if (!valid_name(from) || !valid_name(to))
        return JET_errInvalidName;
    COLUMN *col = ese_find_column(table, from);
    if (!col)
        return JET_errColumnNotFound;
    if (ese_find_column(table, to) && lstrcmpiW(from, to))
        return JET_errKeyDuplicate;
    if (sess->level) {
        UNDO *u = ese_log(sess, U_RENAME_COLUMN);
        u->table = table;
        u->col = col->id;
        lstrcpyW(u->name, col->name);
    }
    lstrcpyW(col->name, to);
    table->db->dirty = TRUE;
    return JET_errSuccess;
}

/* ------------------------------------------------------------------ indexes */

JET_ERR ese_create_index(SESSION *sess, TABLE *table, LPCWSTR name, JET_GRBIT grbit, const WCHAR *key, ULONG cbkey,
                         ULONG density, ULONG lcid, ULONG map_flags, ULONG varsegmac, const JET_TUPLELIMITS *tuples,
                         const JET_CONDITIONALCOLUMN *cond, ULONG ncond, BOOL cond_wide, ULONG keymost)
{
    if (table->db->readonly)
        return JET_errPermissionDenied;
    if (!valid_name(name))
        return JET_errInvalidName;
    if (!key || !cbkey)
        return JET_errInvalidParameter;
    if (ese_find_index(table, name))
        return JET_errIndexDuplicate;
    if (table->nidx - 1 >= 255)
        return JET_errTooManyIndexes;
    INDEX *idx = ese_alloc(sizeof(*idx));
    lstrcpyW(idx->name, name);
    idx->grbit = grbit;
    idx->lcid = lcid ? lcid : 0x409;
    idx->map_flags = map_flags;
    idx->keymost = keymost ? keymost : JET_cbKeyMost;
    idx->varsegmac = varsegmac;
    idx->density = density ? density : 100;
    idx->primary = (grbit & JET_bitIndexPrimary) != 0;
    if (tuples && (grbit & JET_bitIndexTuples))
        idx->tuples = *tuples;
    /* the key is a list of columns, each ended by a zero, and the list by another. */
    ULONG chars = cbkey / sizeof(WCHAR);
    ULONG at = 0;
    JET_ERR err = JET_errSuccess;
    while (at < chars && key[at]) {
        if (idx->nsegs >= 16) {
            err = JET_errTooManyColumns;
            break;
        }
        BOOL desc = FALSE;
        if (key[at] == L'+' || key[at] == L'-') {
            desc = key[at] == L'-';
            at++;
        }
        WCHAR column_name[JET_cbNameMost + 2];
        ULONG n = 0;
        while (at < chars && key[at] && n < JET_cbNameMost + 1)
            column_name[n++] = key[at++];
        column_name[n] = 0;
        if (at >= chars || key[at]) {
            err = JET_errInvalidParameter;
            break;
        }
        at++;
        COLUMN *col = ese_find_column(table, column_name);
        if (!col) {
            err = JET_errColumnNotFound;
            break;
        }
        idx->segs[idx->nsegs].id = col->id;
        idx->segs[idx->nsegs].desc = desc;
        idx->nsegs++;
    }
    if (!err && !idx->nsegs)
        err = JET_errInvalidParameter;
    if (!err && ncond > 12)
        err = JET_errInvalidParameter;
    for (ULONG i = 0; !err && i < ncond; i++) {
        WCHAR cname[JET_cbNameMost + 2];
        if (cond_wide) {
            const JET_CONDITIONALCOLUMN *c = &cond[i];
            lstrcpynW(cname, c->szColumnName, JET_cbNameMost + 1);
            COLUMN *col = ese_find_column(table, cname);
            if (!col) {
                err = JET_errColumnNotFound;
                break;
            }
            idx->cond[idx->ncond].id = col->id;
            idx->cond[idx->ncond].must_be_null = (c->grbit & JET_bitIndexColumnMustBeNull) != 0;
            idx->ncond++;
        }
    }
    if (!err && (grbit & JET_bitIndexPrimary)) {
        if (table->primary && !table->primary->sequential)
            err = JET_errIndexHasPrimary;
        else if (table->primary && table->primary->count)
            err = JET_errTableNotEmpty;
    }
    if (err) {
        ese_free(idx);
        return err;
    }
    if (table->nidx == table->nicap) {
        table->nicap = table->nicap ? table->nicap * 2 : 8;
        table->idx = ese_realloc(table->idx, table->nicap * sizeof(INDEX *));
    }
    idx->objid = table->db->next_objid++;
    if (idx->primary) {
        /* the table is empty, so the sequential index gives way to the primary one. */
        INDEX *old = table->primary;
        for (ULONG i = 0; i < table->nidx; i++)
            if (table->idx[i] == old) {
                table->idx[i] = idx;
                break;
            }
        free_index(old);
        table->primary = idx;
        idx->sequential = FALSE;
        if (sess->level) {
            UNDO *u = ese_log(sess, U_CREATE_INDEX);
            u->table = table;
            lstrcpyW(u->name, name);
        }
        table->db->dirty = TRUE;
        return JET_errSuccess;
    }
    table->idx[table->nidx++] = idx;
    /* the records already there are indexed. */
    INDEX *primary = table->primary;
    for (ULONG r = 0; r < primary->count && !err; r++) {
        RECORD *rec = primary->entries[r].rec;
        for (VERSION *v = rec->head; v && !err; v = v->older) {
            BYTE **keys;
            ULONG *cbs, count;
            err = ese_index_keys(table, idx, v, &keys, &cbs, &count);
            if (err)
                break;
            for (ULONG k = 0; k < count && !err; k++) {
                BOOL found;
                LONG pos = ese_entry_search(idx, keys[k], cbs[k], rec->pk, rec->pkcb, &found);
                if (found)
                    continue;
                if ((grbit & JET_bitIndexUnique) && !v->deleted && v == rec->head) {
                    /* another entry with the same key is a duplicate. */
                    BOOL dup = (pos > 0 && idx->entries[pos - 1].cb == cbs[k] &&
                                !memcmp(idx->entries[pos - 1].key, keys[k], cbs[k])) ||
                               ((ULONG)pos < idx->count && idx->entries[pos].cb == cbs[k] &&
                                !memcmp(idx->entries[pos].key, keys[k], cbs[k]));
                    if (dup) {
                        err = JET_errKeyDuplicate;
                        break;
                    }
                }
                if (idx->count == idx->cap) {
                    idx->cap = idx->cap ? idx->cap * 2 : 16;
                    idx->entries = ese_realloc(idx->entries, idx->cap * sizeof(ENTRY));
                }
                memmove(&idx->entries[pos + 1], &idx->entries[pos], (idx->count - pos) * sizeof(ENTRY));
                idx->entries[pos].key = keys[k];
                idx->entries[pos].cb = cbs[k];
                idx->entries[pos].rec = rec;
                idx->count++;
                keys[k] = nullptr;
            }
            for (ULONG k = 0; k < count; k++)
                ese_free(keys[k]);
            ese_free(keys);
            ese_free(cbs);
        }
    }
    if (err) {
        table->nidx--;
        free_index(idx);
        return err;
    }
    if (sess->level) {
        UNDO *u = ese_log(sess, U_CREATE_INDEX);
        u->table = table;
        lstrcpyW(u->name, name);
    }
    table->db->dirty = TRUE;
    return JET_errSuccess;
}

JET_ERR ese_delete_index(SESSION *sess, TABLE *table, LPCWSTR name)
{
    if (table->db->readonly)
        return JET_errPermissionDenied;
    if (!valid_name(name))
        return JET_errInvalidName;
    INDEX *idx = ese_find_index(table, name);
    if (!idx)
        return JET_errIndexNotFound;
    if (idx->primary)
        return JET_errIndexMustStay;
    for (CURSOR *cur = sess->cursors; cur; cur = cur->next)
        if (cur->index == idx)
            return JET_errIndexInUse;
    idx->deleted = TRUE;
    if (sess->level) {
        UNDO *u = ese_log(sess, U_DELETE_INDEX);
        u->table = table;
        lstrcpyW(u->name, name);
    }
    table->db->dirty = TRUE;
    return JET_errSuccess;
}
