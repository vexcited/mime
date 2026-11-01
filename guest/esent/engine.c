/* instances, sessions, transactions and the databases an instance has attached. */
#include "ese.h"

int _fltused = 0x9875;
CRITICAL_SECTION g_ese_lock;
INSTANCE *g_instances;
ULONG g_global_pagesize = ESE_PAGE_SIZE_DEFAULT;
BOOL g_global_pagesize_set;
BOOL g_any_initialized;

void *ese_alloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *ese_realloc(void *p, SIZE_T size)
{
    if (!p)
        return ese_alloc(size);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, size ? size : 1);
}

void ese_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

BYTE *ese_dup(const void *p, ULONG cb)
{
    BYTE *copy = ese_alloc(cb);
    if (copy && cb)
        memcpy(copy, p, cb);
    return copy;
}

INSTANCE *ese_instance(JET_INSTANCE handle)
{
    for (INSTANCE *inst = g_instances; inst; inst = inst->next)
        if ((JET_INSTANCE)inst == handle)
            return inst;
    return nullptr;
}

SESSION *ese_session(JET_SESID handle)
{
    for (INSTANCE *inst = g_instances; inst; inst = inst->next)
        for (SESSION *sess = inst->sessions; sess; sess = sess->next)
            if ((JET_SESID)sess == handle)
                return sess;
    return nullptr;
}

CURSOR *ese_cursor(JET_SESID sesid, JET_TABLEID tableid, JET_ERR *err)
{
    SESSION *sess = ese_session(sesid);
    if (!sess) {
        *err = JET_errInvalidSesid;
        return nullptr;
    }
    for (CURSOR *cur = sess->cursors; cur; cur = cur->next)
        if ((JET_TABLEID)cur == tableid) {
            *err = JET_errSuccess;
            return cur;
        }
    *err = JET_errInvalidTableId;
    return nullptr;
}

ULONG ese_page_size(INSTANCE *inst)
{
    return g_global_pagesize;
}

/* ------------------------------------------------------------------ sessions */

SESSION *ese_new_session(INSTANCE *inst)
{
    SESSION *sess = ese_alloc(sizeof(*sess));
    if (!sess)
        return nullptr;
    sess->inst = inst;
    sess->next = inst->sessions;
    inst->sessions = sess;
    return sess;
}

void ese_free_session(SESSION *sess)
{
    INSTANCE *inst = sess->inst;
    while (sess->level)
        ese_rollback(sess, TRUE);
    while (sess->cursors)
        ese_close_cursor(sess->cursors);
    while (sess->dbs)
        ese_close_db(sess, sess->dbs->id);
    for (SESSION **link = &inst->sessions; *link; link = &(*link)->next)
        if (*link == sess) {
            *link = sess->next;
            break;
        }
    ese_free(sess->undo);
    ese_free(sess);
}

/* ------------------------------------------------------------------ transactions */

UNDO *ese_log(SESSION *sess, ULONG kind)
{
    if (sess->nundo == sess->capundo) {
        sess->capundo = sess->capundo ? sess->capundo * 2 : 64;
        sess->undo = ese_realloc(sess->undo, sess->capundo * sizeof(UNDO));
    }
    UNDO *u = &sess->undo[sess->nundo++];
    memset(u, 0, sizeof(*u));
    u->kind = kind;
    u->level = sess->level;
    return u;
}

JET_ERR ese_begin(SESSION *sess, BOOL read_only)
{
    if (sess->level >= ESE_MAX_TRANSACTION_LEVEL)
        return JET_errTransTooDeep;
    if (sess->level == 0)
        sess->snapshot = sess->inst->clock;
    sess->level++;
    sess->marks[sess->level] = sess->nundo;
    sess->read_only_trx[sess->level] = read_only;
    return JET_errSuccess;
}

static void release_lock(RECORD *rec, SESSION *sess)
{
    if (rec->lock == sess) {
        rec->lock = nullptr;
        rec->lock_level = 0;
    }
}

/* the oldest snapshot any session still reads, which versions older than the one it sees are of no use to. */
static ULONG64 oldest_snapshot(INSTANCE *inst)
{
    ULONG64 oldest = inst->clock;
    for (SESSION *s = inst->sessions; s; s = s->next)
        if (s->level && s->snapshot < oldest)
            oldest = s->snapshot;
    return oldest;
}

static void free_version(TABLE *table, VERSION *ver)
{
    ese_free_cells(table, ver);
    ese_free(ver);
}

void ese_remove_entries_of(TABLE *table, INDEX *idx, RECORD *rec, const VERSION *ver)
{
    BYTE **keys;
    ULONG *cbs, count;
    if (idx->primary)
        return;
    if (ese_index_keys(table, idx, ver, &keys, &cbs, &count))
        return;
    for (ULONG k = 0; k < count; k++) {
        /* kept when another version of the record has the key as well. */
        BOOL needed = FALSE;
        for (VERSION *other = rec->head; other && !needed; other = other->older) {
            if (other == ver)
                continue;
            BYTE **okeys;
            ULONG *ocbs, ocount;
            if (ese_index_keys(table, idx, other, &okeys, &ocbs, &ocount))
                continue;
            for (ULONG j = 0; j < ocount && !needed; j++)
                needed = ocbs[j] == cbs[k] && !memcmp(okeys[j], keys[k], cbs[k]);
            ese_free_keys(okeys, ocbs, ocount);
        }
        if (needed)
            continue;
        BOOL found;
        LONG at = ese_entry_search(idx, keys[k], cbs[k], rec->pk, rec->pkcb, &found);
        if (found) {
            ese_free(idx->entries[at].key);
            memmove(&idx->entries[at], &idx->entries[at + 1], (idx->count - at - 1) * sizeof(ENTRY));
            idx->count--;
        }
    }
    ese_free_keys(keys, cbs, count);
}

void ese_drop_record(TABLE *table, RECORD *rec)
{
    INDEX *primary = table->primary;
    for (ULONG i = 0; i < table->nidx; i++) {
        INDEX *idx = table->idx[i];
        if (idx->primary || idx->deleted)
            continue;
        for (VERSION *v = rec->head; v; v = v->older)
            ese_remove_entries_of(table, idx, rec, v);
    }
    BOOL found;
    LONG at = ese_entry_search(primary, rec->pk, rec->pkcb, rec->pk, rec->pkcb, &found);
    if (found) {
        ese_free(primary->entries[at].key);
        memmove(&primary->entries[at], &primary->entries[at + 1], (primary->count - at - 1) * sizeof(ENTRY));
        primary->count--;
    }
    while (rec->head) {
        VERSION *v = rec->head;
        rec->head = v->older;
        free_version(table, v);
    }
    ese_free(rec->pk);
    ese_free(rec);
}

/* removes the versions nobody can read any more, and the record when only its deletion is left. */
void ese_gc_record(TABLE *table, RECORD *rec, SESSION *sess)
{
    ULONG64 oldest = oldest_snapshot(sess->inst);
    BOOL pending = FALSE;
    for (VERSION *v = rec->head; v; v = v->older)
        if (!v->commit)
            pending = TRUE;
    if (pending)
        return;
    /* the newest version committed no later than the oldest snapshot is the oldest one that is still read. */
    VERSION *keep = nullptr;
    for (VERSION *v = rec->head; v; v = v->older)
        if (v->commit <= oldest) {
            keep = v;
            break;
        }
    if (keep) {
        while (keep->older) {
            VERSION *old = keep->older;
            keep->older = old->older;
            for (ULONG i = 0; i < table->nidx; i++) {
                INDEX *idx = table->idx[i];
                if (!idx->primary && !idx->deleted)
                    ese_remove_entries_of(table, idx, rec, old);
            }
            free_version(table, old);
        }
    }
    if (rec->head && rec->head->deleted && !rec->head->older && !rec->lock)
        ese_drop_record(table, rec);
}

ULONG64 ese_next_stamp(INSTANCE *inst)
{
    return ++inst->clock;
}

JET_ERR ese_commit(SESSION *sess, JET_GRBIT grbit)
{
    if (!sess->level)
        return JET_errNotInTransaction;
    if (sess->level > 1) {
        for (ULONG i = sess->marks[sess->level]; i < sess->nundo; i++)
            sess->undo[i].level = sess->level - 1;
        for (ULONG i = sess->marks[sess->level]; i < sess->nundo; i++)
            if (sess->undo[i].kind == U_VERSION && sess->undo[i].ver)
                sess->undo[i].ver->level = sess->level - 1;
        sess->level--;
        return JET_errSuccess;
    }
    INSTANCE *inst = sess->inst;
    ULONG64 stamp = ese_next_stamp(inst);
    DB *touched[8] = {};
    ULONG ntouched = 0;
    for (ULONG i = 0; i < sess->nundo; i++) {
        UNDO *u = &sess->undo[i];
        if (u->table && u->table->db && ntouched < 8) {
            BOOL seen = FALSE;
            for (ULONG t = 0; t < ntouched; t++)
                seen |= touched[t] == u->table->db;
            if (!seen)
                touched[ntouched++] = u->table->db;
            u->table->db->dirty = TRUE;
        }
        switch (u->kind) {
        case U_VERSION:
            u->ver->commit = stamp;
            u->ver->owner = nullptr;
            u->ver->level = 0;
            break;
        case U_LOCK:
            release_lock(u->rec, sess);
            break;
        case U_DELETE_TABLE:
            u->table->deleted = TRUE;
            break;
        default:
            break;
        }
    }
    /* records that are only a deletion now, and versions that nobody reads any more, are cleaned up. */
    sess->level = 0;
    for (ULONG i = 0; i < sess->nundo; i++) {
        UNDO *u = &sess->undo[i];
        if (u->kind == U_VERSION || u->kind == U_ESCROW) {
            BOOL gone = FALSE;
            if (u->kind == U_ESCROW) {
                COLUMN *col = ese_column_by_id(u->table, u->col);
                if (col && (col->grbit & JET_bitColumnDeleteOnZero)) {
                    VERSION *v = u->rec->head;
                    ULONG slot = 0;
                    for (; slot < u->table->ncols; slot++)
                        if (u->table->cols[slot]->id == u->col)
                            break;
                    const CELL *cell = v ? ese_cell(v, slot) : nullptr;
                    if (v && !v->deleted && cell->count && cell->vals[0].cb == 4 && *(LONG *)cell->vals[0].data == 0) {
                        VERSION *dead = ese_new_version(u->table);
                        dead->deleted = TRUE;
                        dead->commit = stamp;
                        dead->older = u->rec->head;
                        u->rec->head = dead;
                    }
                }
                (void)gone;
            }
            ese_gc_record(u->table, u->rec, sess);
        }
    }
    for (ULONG i = 0; i < sess->nundo; i++)
        if (sess->undo[i].kind == U_DELETE_TABLE) {
            TABLE *t = sess->undo[i].table;
            DB *db = t->db;
            for (TABLE **link = &db->tables; *link; link = &(*link)->next)
                if (*link == t) {
                    *link = t->next;
                    break;
                }
            ese_free_table(t);
        }
    sess->nundo = 0;
    for (ULONG t = 0; t < ntouched; t++)
        if (!(grbit & JET_bitCommitLazyFlush))
            ese_save_db(touched[t], FALSE);
    return JET_errSuccess;
}

static void rollback_entry(SESSION *sess, UNDO *u)
{
    switch (u->kind) {
    case U_VERSION: {
        RECORD *rec = u->rec;
        VERSION *ver = u->ver;
        for (VERSION **link = &rec->head; *link; link = &(*link)->older)
            if (*link == ver) {
                *link = ver->older;
                break;
            }
        for (ULONG i = 0; i < u->table->nidx; i++) {
            INDEX *idx = u->table->idx[i];
            if (!idx->primary && !idx->deleted)
                ese_remove_entries_of(u->table, idx, rec, ver);
        }
        free_version(u->table, ver);
        if (!rec->head)
            ese_drop_record(u->table, rec);
        break;
    }
    case U_LOCK:
        release_lock(u->rec, sess);
        break;
    case U_ESCROW: {
        ULONG slot = 0;
        for (; slot < u->table->ncols; slot++)
            if (u->table->cols[slot]->id == u->col)
                break;
        for (VERSION *v = u->rec->head; v; v = v->older)
            if (!v->deleted && slot < u->table->ncols && ese_cell(v, slot)->count && ese_cell(v, slot)->vals[0].cb == 4) {
                *(LONG *)ese_cell(v, slot)->vals[0].data -= u->delta;
                break;
            }
        break;
    }
    case U_CREATE_TABLE: {
        TABLE *t = u->table;
        for (TABLE **link = &t->db->tables; *link; link = &(*link)->next)
            if (*link == t) {
                *link = t->next;
                break;
            }
        t->deleted = TRUE;
        if (!t->refs)
            ese_free_table(t);
        break;
    }
    case U_DELETE_TABLE:
        u->table->deleted = FALSE;
        break;
    case U_ADD_COLUMN: {
        COLUMN *col = ese_column_by_id(u->table, u->col);
        if (col)
            col->deleted = TRUE;
        break;
    }
    case U_DELETE_COLUMN: {
        for (ULONG i = 0; i < u->table->ncols; i++)
            if (u->table->cols[i]->id == u->col)
                u->table->cols[i]->deleted = FALSE;
        break;
    }
    case U_CREATE_INDEX:
        for (ULONG i = 0; i < u->table->nidx; i++)
            if (!lstrcmpiW(u->table->idx[i]->name, u->name) && !u->table->idx[i]->deleted)
                u->table->idx[i]->deleted = TRUE;
        break;
    case U_DELETE_INDEX:
        for (ULONG i = 0; i < u->table->nidx; i++)
            if (!lstrcmpiW(u->table->idx[i]->name, u->name))
                u->table->idx[i]->deleted = FALSE;
        break;
    case U_RENAME_TABLE:
        lstrcpyW(u->table->name, u->name);
        break;
    case U_RENAME_COLUMN:
        for (ULONG i = 0; i < u->table->ncols; i++)
            if (u->table->cols[i]->id == u->col)
                lstrcpyW(u->table->cols[i]->name, u->name);
        break;
    default:
        break;
    }
}

JET_ERR ese_rollback(SESSION *sess, BOOL all)
{
    if (!sess->level)
        return JET_errNotInTransaction;
    do {
        for (ULONG i = sess->nundo; i > sess->marks[sess->level]; i--)
            rollback_entry(sess, &sess->undo[i - 1]);
        sess->nundo = sess->marks[sess->level];
        sess->level--;
    } while (all && sess->level);
    if (!sess->level)
        sess->nundo = 0;
    return JET_errSuccess;
}

/* ------------------------------------------------------------------ parameters */

void ese_init_params(PARAMS *params)
{
    memset(params, 0, sizeof(*params));
    for (ULONG id = 0; id < g_param_count; id++) {
        const PARAM_DEFAULT *d = &g_param_defaults[id];
        if (d->kind == 0)
            params->values[id] = (JET_API_PTR)d->value;
        else if (d->kind == 1)
            params->texts[id] = lstrcpyW(ese_alloc((lstrlenW(d->text) + 1) * sizeof(WCHAR)), d->text);
    }
}

const WCHAR *ese_text_param(const PARAMS *params, ULONG id)
{
    return params->texts[id] ? params->texts[id] : L"";
}

void ese_copy_params(PARAMS *to, const PARAMS *from)
{
    *to = *from;
    for (ULONG id = 0; id < ESE_PARAM_COUNT; id++)
        if (from->texts[id])
            to->texts[id] = lstrcpyW(ese_alloc((lstrlenW(from->texts[id]) + 1) * sizeof(WCHAR)), from->texts[id]);
}

void ese_free_params(PARAMS *params)
{
    for (ULONG id = 0; id < ESE_PARAM_COUNT; id++)
        ese_free(params->texts[id]);
}
