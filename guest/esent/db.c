/* databases: attaching, opening and the file a database is kept in. */
#include "ese.h"

JET_ERR ese_flush_if_needed(SESSION *sess, DB *db)
{
    return JET_errSuccess;
}

DBH *ese_dbh(SESSION *sess, JET_DBID dbid)
{
    for (DBH *h = sess->dbs; h; h = h->next)
        if (h->id == dbid)
            return h;
    return nullptr;
}

static BOOL same_path(const WCHAR *a, const WCHAR *b)
{
    WCHAR fa[MAX_PATH], fb[MAX_PATH];
    if (!GetFullPathNameW(a, MAX_PATH, fa, nullptr) || !GetFullPathNameW(b, MAX_PATH, fb, nullptr))
        return !lstrcmpiW(a, b);
    return !lstrcmpiW(fa, fb);
}

static DB *find_db(INSTANCE *inst, LPCWSTR path)
{
    for (DB *db = inst->dbs; db; db = db->next)
        if (same_path(db->path, path))
            return db;
    return nullptr;
}

void ese_free_db(DB *db)
{
    while (db->tables) {
        TABLE *t = db->tables;
        db->tables = t->next;
        ese_free_table(t);
    }
    ese_free(db);
}

static DBH *add_handle(SESSION *sess, DB *db)
{
    DBH *h = ese_alloc(sizeof(*h));
    h->db = db;
    h->sess = sess;
    h->id = db->ifmp;
    h->next = sess->dbs;
    sess->dbs = h;
    db->refs++;
    return h;
}

static JET_ERR path_error(LPCWSTR path)
{
    WCHAR dir[MAX_PATH];
    lstrcpynW(dir, path, MAX_PATH);
    WCHAR *slash = wcsrchr(dir, L'\\');
    if (!slash)
        slash = wcsrchr(dir, L'/');
    if (slash) {
        if (slash == dir || slash[-1] == L':')
            slash++;
        *slash = 0;
        DWORD attributes = GetFileAttributesW(dir);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return JET_errInvalidPath;
    }
    return JET_errSuccess;
}

JET_ERR ese_create_db(SESSION *sess, LPCWSTR path, ULONG pagesize, JET_GRBIT grbit, JET_DBID *dbid)
{
    INSTANCE *inst = sess->inst;
    if (!path || !*path)
        return JET_errDatabaseInvalidPath;
    if (lstrlenW(path) >= MAX_PATH)
        return JET_errInvalidPath;
    if (find_db(inst, path))
        return JET_errDatabaseDuplicate;
    JET_ERR err = path_error(path);
    if (err)
        return err;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (!(grbit & JET_bitDbOverwriteExisting))
            return JET_errDatabaseDuplicate;
        if (!DeleteFileW(path))
            return JET_errFileAccessDenied;
    }
    DB *db = ese_alloc(sizeof(*db));
    db->inst = inst;
    lstrcpyW(db->path, path);
    db->pagesize = g_global_pagesize;
    db->next_objid = 16;
    db->ifmp = ++inst->next_ifmp;
    db->attached = TRUE;
    db->created_here = TRUE;
    db->version = 0x620;
    db->update = 0x14;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    db->sign.ulRandom = (ULONG)(((ULONG64)now.dwHighDateTime << 32 | now.dwLowDateTime) / 7);
    db->next = inst->dbs;
    inst->dbs = db;
    err = ese_save_db(db, FALSE);
    if (err) {
        inst->dbs = db->next;
        ese_free_db(db);
        DeleteFileW(path);
        return err;
    }
    DBH *h = add_handle(sess, db);
    *dbid = h->id;
    return JET_errSuccess;
}

JET_ERR ese_attach_db(SESSION *sess, LPCWSTR path, JET_GRBIT grbit, ULONG max_pages)
{
    INSTANCE *inst = sess->inst;
    if (!path || !*path)
        return JET_errDatabaseInvalidPath;
    if (lstrlenW(path) >= MAX_PATH)
        return JET_errInvalidPath;
    if (find_db(inst, path))
        return JET_wrnDatabaseAttached;
    JET_ERR err = path_error(path);
    if (err)
        return err;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return JET_errFileNotFound;
    DB *db = ese_alloc(sizeof(*db));
    db->inst = inst;
    lstrcpyW(db->path, path);
    db->readonly = (grbit & JET_bitDbReadOnly) != 0;
    db->next_objid = 16;
    db->ifmp = ++inst->next_ifmp;
    db->attached = TRUE;
    db->max_pages = max_pages;
    err = ese_load_db(db, path);
    if (err) {
        ese_free_db(db);
        return err;
    }
    db->next = inst->dbs;
    inst->dbs = db;
    return JET_errSuccess;
}

JET_ERR ese_open_db(SESSION *sess, LPCWSTR path, JET_DBID *dbid, JET_GRBIT grbit)
{
    INSTANCE *inst = sess->inst;
    if (!path || !*path)
        return JET_errInvalidParameter;
    DB *db = find_db(inst, path);
    if (!db)
        return JET_errDatabaseNotFound;
    for (DBH *h = sess->dbs; h; h = h->next)
        if (h->db == db) {
            *dbid = h->id;
            h->refs++;
            return JET_errSuccess;
        }
    if (db->exclusive || ((grbit & JET_bitDbExclusive) && db->refs))
        return JET_errDatabaseLocked;
    if (grbit & JET_bitDbExclusive)
        db->exclusive = TRUE;
    DBH *h = add_handle(sess, db);
    *dbid = h->id;
    return JET_errSuccess;
}

JET_ERR ese_close_db(SESSION *sess, JET_DBID dbid)
{
    DBH *h = ese_dbh(sess, dbid);
    if (!h)
        return JET_errInvalidDatabaseId;
    for (CURSOR *cur = sess->cursors; cur;) {
        CURSOR *next = cur->next;
        if (cur->dbh == h)
            ese_close_cursor(cur);
        cur = next;
    }
    for (DBH **link = &sess->dbs; *link; link = &(*link)->next)
        if (*link == h) {
            *link = h->next;
            break;
        }
    h->db->refs--;
    h->db->exclusive = FALSE;
    ese_free(h);
    return JET_errSuccess;
}

JET_ERR ese_detach_db(SESSION *sess, LPCWSTR path, JET_GRBIT grbit)
{
    INSTANCE *inst = sess->inst;
    DB *db = find_db(inst, path);
    if (!db)
        return JET_errDatabaseNotFound;
    for (SESSION *s = inst->sessions; s; s = s->next)
        for (DBH *h = s->dbs; h; h = h->next)
            if (h->db == db)
                return JET_errDatabaseInUse;
    ese_save_db(db, TRUE);
    for (DB **link = &inst->dbs; *link; link = &(*link)->next)
        if (*link == db) {
            *link = db->next;
            break;
        }
    ese_free_db(db);
    return JET_errSuccess;
}
