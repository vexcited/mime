/* the file of a database: a header page like the one of Windows' engine, then the contents of the database. */
#include "ese.h"

typedef struct BUF {
    BYTE *data;
    ULONG size, cap;
    BOOL bad;
} BUF;

static void put(BUF *b, const void *p, ULONG n)
{
    if (b->size + n > b->cap) {
        b->cap = (b->size + n) * 2 + 4096;
        b->data = ese_realloc(b->data, b->cap);
    }
    memcpy(b->data + b->size, p, n);
    b->size += n;
}

static void put32(BUF *b, ULONG v)
{
    put(b, &v, 4);
}

static void put64(BUF *b, ULONG64 v)
{
    put(b, &v, 8);
}

static void put_str(BUF *b, const WCHAR *s)
{
    ULONG n = (ULONG)lstrlenW(s);
    put32(b, n);
    put(b, s, n * 2);
}

typedef struct RD {
    const BYTE *data;
    ULONG size, at;
    BOOL bad;
} RD;

static ULONG get32(RD *r)
{
    ULONG v = 0;
    if (r->at + 4 > r->size) {
        r->bad = TRUE;
        return 0;
    }
    memcpy(&v, r->data + r->at, 4);
    r->at += 4;
    return v;
}

static ULONG64 get64(RD *r)
{
    ULONG64 v = 0;
    if (r->at + 8 > r->size) {
        r->bad = TRUE;
        return 0;
    }
    memcpy(&v, r->data + r->at, 8);
    r->at += 8;
    return v;
}

static const BYTE *get_bytes(RD *r, ULONG n)
{
    if (r->bad || r->at + n > r->size || r->at + n < r->at) {
        r->bad = TRUE;
        return nullptr;
    }
    const BYTE *p = r->data + r->at;
    r->at += n;
    return p;
}

static void get_str(RD *r, WCHAR *out, ULONG max)
{
    ULONG n = get32(r);
    const BYTE *p = get_bytes(r, n * 2);
    if (!p || n >= max) {
        r->bad = TRUE;
        out[0] = 0;
        return;
    }
    memcpy(out, p, n * 2);
    out[n] = 0;
}

static VERSION *saved_version(RECORD *rec)
{
    for (VERSION *v = rec->head; v; v = v->older)
        if (v->commit)
            return v->deleted ? nullptr : v;
    return nullptr;
}

static void serialize(DB *db, BUF *b)
{
    put(b, "MIMEESE1", 8);
    ULONG ntables = 0;
    for (TABLE *t = db->tables; t; t = t->next)
        if (!t->deleted)
            ntables++;
    put32(b, ntables);
    put64(b, db->next_objid);
    for (TABLE *t = db->tables; t; t = t->next) {
        if (t->deleted)
            continue;
        put_str(b, t->name);
        put64(b, t->objid);
        put32(b, t->pages);
        put32(b, t->density);
        put64(b, t->next_id);
        put32(b, t->last_fixed);
        put32(b, t->last_var);
        put32(b, t->last_tagged);
        put64(b, t->created);
        put64(b, t->updated);
        put32(b, t->is_template);
        ULONG ncols = 0;
        for (ULONG i = 0; i < t->ncols; i++)
            ncols++;
        put32(b, ncols);
        for (ULONG i = 0; i < t->ncols; i++) {
            COLUMN *c = t->cols[i];
            put32(b, c->id);
            put_str(b, c->name);
            put32(b, c->coltyp);
            put32(b, c->cbMax);
            put32(b, c->grbit);
            put32(b, c->cp | (ULONG)c->langid << 16);
            put32(b, c->country | (ULONG)c->collate << 16);
            put32(b, c->defcb);
            put(b, c->def, c->defcb);
            put32(b, c->deleted);
            put32(b, (ULONG)c->autoinc);
        }
        ULONG nidx = 0;
        for (ULONG i = 0; i < t->nidx; i++)
            if (!t->idx[i]->deleted && !t->idx[i]->sequential)
                nidx++;
        put32(b, nidx);
        for (ULONG i = 0; i < t->nidx; i++) {
            INDEX *idx = t->idx[i];
            if (idx->deleted || idx->sequential)
                continue;
            put_str(b, idx->name);
            put32(b, idx->grbit);
            put32(b, idx->nsegs);
            for (ULONG s = 0; s < idx->nsegs; s++) {
                put32(b, idx->segs[s].id);
                put32(b, idx->segs[s].desc);
            }
            put32(b, idx->lcid);
            put32(b, idx->map_flags);
            put32(b, idx->keymost);
            put32(b, idx->varsegmac);
            put32(b, idx->density);
            put32(b, idx->ncond);
            for (ULONG s = 0; s < idx->ncond; s++) {
                put32(b, idx->cond[s].id);
                put32(b, idx->cond[s].must_be_null);
            }
            put(b, &idx->tuples, sizeof(idx->tuples));
            put64(b, idx->objid);
        }
        ULONG nrec = 0;
        INDEX *primary = t->primary;
        for (ULONG i = 0; i < primary->count; i++)
            if (saved_version(primary->entries[i].rec))
                nrec++;
        put32(b, nrec);
        for (ULONG i = 0; i < primary->count; i++) {
            RECORD *rec = primary->entries[i].rec;
            VERSION *v = saved_version(rec);
            if (!v)
                continue;
            put32(b, rec->pkcb);
            put(b, rec->pk, rec->pkcb);
            for (ULONG s = 0; s < t->ncols; s++) {
                const CELL *cell = ese_cell(v, s);
                put32(b, cell->count | (cell->set ? 0x80000000 : 0));
                for (ULONG k = 0; k < cell->count; k++) {
                    put32(b, cell->vals[k].cb);
                    put(b, cell->vals[k].data, cell->vals[k].cb);
                }
            }
        }
    }
}

JET_ERR ese_index_record(TABLE *t, RECORD *rec, VERSION *v)
{
    for (ULONG i = 0; i < t->nidx; i++) {
        INDEX *idx = t->idx[i];
        if (idx->primary)
            continue;
        BYTE **keys;
        ULONG *cbs, count;
        JET_ERR err = ese_index_keys(t, idx, v, &keys, &cbs, &count);
        if (err)
            continue;
        for (ULONG k = 0; k < count; k++) {
            BOOL found;
            LONG at = ese_entry_search(idx, keys[k], cbs[k], rec->pk, rec->pkcb, &found);
            if (found) {
                ese_free(keys[k]);
                continue;
            }
            if (idx->count == idx->cap) {
                idx->cap = idx->cap ? idx->cap * 2 : 16;
                idx->entries = ese_realloc(idx->entries, idx->cap * sizeof(ENTRY));
            }
            memmove(&idx->entries[at + 1], &idx->entries[at], (idx->count - at) * sizeof(ENTRY));
            idx->entries[at].key = keys[k];
            idx->entries[at].cb = cbs[k];
            idx->entries[at].rec = rec;
            idx->count++;
        }
        ese_free(keys);
        ese_free(cbs);
    }
    return JET_errSuccess;
}

static JET_ERR deserialize(DB *db, const BYTE *data, ULONG size)
{
    RD r = {data, size, 8, FALSE};
    if (size < 8 || memcmp(data, "MIMEESE1", 8))
        return JET_errDatabaseCorrupted;
    ULONG ntables = get32(&r);
    db->next_objid = get64(&r);
    for (ULONG ti = 0; ti < ntables && !r.bad; ti++) {
        TABLE *t = ese_alloc(sizeof(*t));
        t->db = db;
        get_str(&r, t->name, JET_cbNameMost + 1);
        t->objid = get64(&r);
        t->pages = get32(&r);
        t->density = get32(&r);
        t->next_id = get64(&r);
        t->last_fixed = get32(&r);
        t->last_var = get32(&r);
        t->last_tagged = get32(&r);
        t->created = get64(&r);
        t->updated = get64(&r);
        t->is_template = get32(&r);
        ULONG ncols = get32(&r);
        if (r.bad || ncols > 0x10000) {
            ese_free(t);
            return JET_errDatabaseCorrupted;
        }
        t->cols = ese_alloc((ncols + 1) * sizeof(COLUMN *));
        t->ncap = ncols + 1;
        for (ULONG i = 0; i < ncols && !r.bad; i++) {
            COLUMN *c = ese_alloc(sizeof(*c));
            c->id = get32(&r);
            get_str(&r, c->name, JET_cbNameMost + 1);
            c->coltyp = get32(&r);
            c->cbMax = get32(&r);
            c->grbit = get32(&r);
            ULONG x = get32(&r);
            c->cp = (USHORT)x;
            c->langid = (USHORT)(x >> 16);
            x = get32(&r);
            c->country = (USHORT)x;
            c->collate = (USHORT)(x >> 16);
            c->defcb = get32(&r);
            const BYTE *def = get_bytes(&r, c->defcb);
            c->def = def ? ese_dup(def, c->defcb) : nullptr;
            c->deleted = get32(&r);
            c->autoinc = (LONG)get32(&r);
            t->cols[t->ncols++] = c;
        }
        t->primary = ese_alloc(sizeof(INDEX));
        t->primary->primary = TRUE;
        t->primary->sequential = TRUE;
        t->idx = ese_alloc(16 * sizeof(INDEX *));
        t->nicap = 16;
        t->idx[t->nidx++] = t->primary;
        ULONG nidx = get32(&r);
        for (ULONG i = 0; i < nidx && !r.bad; i++) {
            INDEX *idx = ese_alloc(sizeof(*idx));
            get_str(&r, idx->name, JET_cbNameMost + 1);
            idx->grbit = get32(&r);
            idx->nsegs = get32(&r);
            if (idx->nsegs > 16) {
                r.bad = TRUE;
                ese_free(idx);
                break;
            }
            for (ULONG s = 0; s < idx->nsegs; s++) {
                idx->segs[s].id = get32(&r);
                idx->segs[s].desc = get32(&r);
            }
            idx->lcid = get32(&r);
            idx->map_flags = get32(&r);
            idx->keymost = get32(&r);
            idx->varsegmac = get32(&r);
            idx->density = get32(&r);
            idx->ncond = get32(&r);
            if (idx->ncond > 12) {
                r.bad = TRUE;
                ese_free(idx);
                break;
            }
            for (ULONG s = 0; s < idx->ncond; s++) {
                idx->cond[s].id = get32(&r);
                idx->cond[s].must_be_null = get32(&r);
            }
            const BYTE *tl = get_bytes(&r, sizeof(idx->tuples));
            if (tl)
                memcpy(&idx->tuples, tl, sizeof(idx->tuples));
            idx->objid = get64(&r);
            idx->primary = (idx->grbit & JET_bitIndexPrimary) != 0;
            if (t->nidx == t->nicap) {
                t->nicap *= 2;
                t->idx = ese_realloc(t->idx, t->nicap * sizeof(INDEX *));
            }
            if (idx->primary) {
                ese_free(t->primary);
                t->idx[0] = idx;
                t->primary = idx;
            } else {
                t->idx[t->nidx++] = idx;
            }
        }
        ULONG nrec = get32(&r);
        for (ULONG i = 0; i < nrec && !r.bad; i++) {
            RECORD *rec = ese_alloc(sizeof(*rec));
            rec->pkcb = get32(&r);
            const BYTE *pk = get_bytes(&r, rec->pkcb);
            if (!pk) {
                ese_free(rec);
                break;
            }
            rec->pk = ese_dup(pk, rec->pkcb);
            VERSION *v = ese_new_version(t);
            v->commit = 1;
            for (ULONG s = 0; s < t->ncols && !r.bad; s++) {
                ULONG x = get32(&r);
                CELL *cell = &v->cells[s];
                cell->set = (x & 0x80000000) != 0;
                ULONG count = x & 0x7fffffff;
                if (count > 100000) {
                    r.bad = TRUE;
                    break;
                }
                cell->vals = ese_alloc((count ? count : 1) * sizeof(VAL));
                for (ULONG k = 0; k < count && !r.bad; k++) {
                    ULONG cb = get32(&r);
                    const BYTE *p = get_bytes(&r, cb);
                    if (!p)
                        break;
                    cell->vals[k].cb = cb;
                    cell->vals[k].data = ese_dup(p, cb);
                    cell->count++;
                }
            }
            rec->head = v;
            BOOL found;
            LONG at = ese_entry_search(t->primary, rec->pk, rec->pkcb, rec->pk, rec->pkcb, &found);
            if (found || r.bad) {
                ese_free_cells(t, v);
                ese_free(v);
                ese_free(rec->pk);
                ese_free(rec);
                if (found)
                    r.bad = TRUE;
                continue;
            }
            INDEX *primary = t->primary;
            if (primary->count == primary->cap) {
                primary->cap = primary->cap ? primary->cap * 2 : 16;
                primary->entries = ese_realloc(primary->entries, primary->cap * sizeof(ENTRY));
            }
            memmove(&primary->entries[at + 1], &primary->entries[at], (primary->count - at) * sizeof(ENTRY));
            primary->entries[at].key = ese_dup(rec->pk, rec->pkcb);
            primary->entries[at].cb = rec->pkcb;
            primary->entries[at].rec = rec;
            primary->count++;
            ese_index_record(t, rec, v);
        }
        t->next = db->tables;
        db->tables = t;
    }
    return r.bad ? JET_errDatabaseCorrupted : JET_errSuccess;
}

/* ------------------------------------------------------------------ the header page */

static ULONG header_checksum(const BYTE *page, ULONG size)
{
    ULONG sum = 0x89abcdef;
    for (ULONG i = 4; i < size; i += 4)
        sum ^= *(const ULONG *)(page + i);
    return sum;
}

static void fill_header(DB *db, BYTE *page, BOOL clean)
{
    memset(page, 0, db->pagesize);
    *(ULONG *)(page + 4) = 0x89abcdef;
    *(ULONG *)(page + 8) = 0x620;
    *(ULONG *)(page + 12) = 0;
    *(ULONG64 *)(page + 0x10) = db->dbtime;
    memcpy(page + 0x18, &db->sign, 28);
    *(ULONG *)(page + 0x34) = clean ? JET_dbstateCleanShutdown : JET_dbstateDirtyShutdown;
    *(ULONG *)(page + 0xd4) = 10;
    *(ULONG *)(page + 0xd8) = 0;
    *(ULONG *)(page + 0xe0) = 26300;
    *(ULONG *)(page + 0xe8) = 20;
    *(ULONG *)(page + 0xec) = db->pagesize;
    *(ULONG *)page = header_checksum(page, db->pagesize);
}

JET_ERR ese_save_db(DB *db, BOOL final)
{
    if (db->readonly || !db->path[0])
        return JET_errSuccess;
    BUF b = {};
    serialize(db, &b);
    ULONG ps = db->pagesize;
    ULONG total = 2 * ps + ((8 + b.size + ps - 1) / ps) * ps;
    BYTE *file = ese_alloc(total);
    fill_header(db, file, TRUE);
    memcpy(file + ps, file, ps);
    *(ULONG64 *)(file + 2 * ps) = b.size;
    memcpy(file + 2 * ps + 8, b.data, b.size);
    ese_free(b.data);
    WCHAR temp[MAX_PATH + 8];
    lstrcpyW(temp, db->path);
    lstrcatW(temp, L".tmp");
    HANDLE h = CreateFileW(temp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        ese_free(file);
        return JET_errFileAccessDenied;
    }
    DWORD written;
    BOOL ok = WriteFile(h, file, total, &written, nullptr) && written == total;
    CloseHandle(h);
    ese_free(file);
    if (!ok || !MoveFileExW(temp, db->path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp);
        return JET_errDiskIO;
    }
    db->dirty = FALSE;
    return JET_errSuccess;
}

JET_ERR ese_load_db(DB *db, LPCWSTR path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? JET_errFileNotFound : JET_errFileAccessDenied;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 4096 || size.QuadPart > 0x7fffffff) {
        CloseHandle(h);
        return JET_errDatabaseCorrupted;
    }
    BYTE *file = ese_alloc((SIZE_T)size.QuadPart);
    DWORD got;
    BOOL ok = ReadFile(h, file, (DWORD)size.QuadPart, &got, nullptr) && got == size.QuadPart;
    CloseHandle(h);
    if (!ok) {
        ese_free(file);
        return JET_errDiskIO;
    }
    JET_ERR err = JET_errDatabaseCorrupted;
    ULONG ps = *(ULONG *)(file + 0xec);
    if (*(ULONG *)(file + 4) == 0x89abcdef && (ps == 4096 || ps == 8192 || ps == 16384 || ps == 32768) &&
        size.QuadPart >= 2 * ps && *(ULONG *)file == header_checksum(file, ps)) {
        db->pagesize = ps;
        db->version = *(ULONG *)(file + 8);
        memcpy(&db->sign, file + 0x18, 28);
        db->dbtime = *(ULONG64 *)(file + 0x10);
        if (size.QuadPart >= 2 * ps + 16 && !memcmp(file + 2 * ps + 8, "MIMEESE1", 8)) {
            ULONG64 length = *(ULONG64 *)(file + 2 * ps);
            if (length + 8 + 2 * ps <= (ULONG64)size.QuadPart)
                err = deserialize(db, file + 2 * ps + 8, (ULONG)length);
        } else {
            err = ese_load_real(db, file, (ULONG)size.QuadPart);
        }
    } else {
        err = JET_errDatabaseCorrupted;
    }
    ese_free(file);
    return err;
}

/* the header page alone, which says what the file is without attaching it. */
JET_ERR ese_read_header(DB *db, LPCWSTR path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return JET_errFileAccessDenied;
    BYTE page[512];
    DWORD got;
    BOOL ok = ReadFile(h, page, sizeof(page), &got, nullptr) && got == sizeof(page);
    LARGE_INTEGER size;
    GetFileSizeEx(h, &size);
    CloseHandle(h);
    if (!ok || *(ULONG *)(page + 4) != 0x89abcdef)
        return JET_errDatabaseCorrupted;
    ULONG ps = *(ULONG *)(page + 0xec);
    if (ps != 4096 && ps != 8192 && ps != 16384 && ps != 32768)
        return JET_errDatabaseCorrupted;
    db->pagesize = ps;
    db->version = *(ULONG *)(page + 8);
    memcpy(&db->sign, page + 0x18, 28);
    return JET_errSuccess;
}
