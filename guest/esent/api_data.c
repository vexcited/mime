/* the jet api: cursors, records, keys, transactions of records and the creation of tables with all their parts. */
#include "ese.h"
#include "api.h"

#define LOCK() EnterCriticalSection(&g_ese_lock)
#define UNLOCK() LeaveCriticalSection(&g_ese_lock)

#define CURSOR_API(var, ...)                                                                                          \
    JET_ERR err;                                                                                                       \
    LOCK();                                                                                                            \
    CURSOR *var = ese_cursor(sesid, tableid, &err);                                                                    \
    if (var && var->sess->inst->stopping) {                                                                            \
        err = JET_errClientRequestToStopJetService;                                                                    \
        var = nullptr;                                                                                                 \
    }                                                                                                                  \
    if (var) {                                                                                                         \
        __VA_ARGS__;                                                                                                          \
    }                                                                                                                  \
    UNLOCK();                                                                                                          \
    return err;

static JET_ERR open_table(JET_SESID handle, JET_DBID dbid, LPCWSTR name, JET_GRBIT grbit, JET_TABLEID *out)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    DBH *dbh = sess ? ese_dbh(sess, dbid) : nullptr;
    if (sess && !dbh)
        err = JET_errInvalidDatabaseId;
    if (!err && (!out || !name))
        err = JET_errInvalidParameter;
    if (!err) {
        TABLE *table = ese_find_table(dbh->db, name);
        if (!table)
            err = JET_errObjectNotFound;
        else if (table->creator && table->creator->sess != sess)
            err = JET_errTableLocked;
        else if ((grbit & JET_bitTableDenyRead) && table->refs)
            err = JET_errTableInUse;
        else {
            CURSOR *cur;
            err = ese_open_cursor(sess, dbh, table, grbit, &cur);
            if (!err) {
                cur->open_grbit = grbit;
                cur->readonly = (grbit & JET_bitTableReadOnly) != 0 || dbh->db->readonly;
                *out = (JET_TABLEID)cur;
            }
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetOpenTableW(JET_SESID s, JET_DBID db, LPCWSTR name, const void *p, ULONG cb, JET_GRBIT grbit,
                             JET_TABLEID *out)
{
    return open_table(s, db, name, grbit, out);
}

JET_ERR WINAPI JetOpenTableA(JET_SESID s, JET_DBID db, LPCSTR name, const void *p, ULONG cb, JET_GRBIT grbit,
                             JET_TABLEID *out)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return open_table(s, db, name ? w : nullptr, grbit, out);
}


JET_ERR WINAPI JetCloseTable(JET_SESID sesid, JET_TABLEID tableid)
{
    CURSOR_API(cur, ese_close_cursor(cur))
}

JET_ERR WINAPI JetDupCursor(JET_SESID sesid, JET_TABLEID tableid, JET_TABLEID *out, JET_GRBIT grbit)
{
    CURSOR_API(cur, {
        CURSOR *dup;
        if (!out)
            err = JET_errInvalidParameter;
        else {
            err = ese_dup_cursor(cur, &dup);
            if (!err)
                *out = (JET_TABLEID)dup;
        }
    })
}

/* ------------------------------------------------------------------ records */

JET_ERR WINAPI JetPrepareUpdate(JET_SESID sesid, JET_TABLEID tableid, ULONG prep)
{
    CURSOR_API(cur, {
        if (cur->readonly && prep != JET_prepReadOnlyCopy && prep != JET_prepCancel)
            err = JET_errPermissionDenied;
        else
            err = ese_prepare(cur, prep);
    })
}

JET_ERR WINAPI JetUpdate2(JET_SESID sesid, JET_TABLEID tableid, void *bookmark, ULONG cb, ULONG *actual,
                          JET_GRBIT grbit)
{
    CURSOR_API(cur, err = ese_update(cur, bookmark, cb, actual, grbit))
}

JET_ERR WINAPI JetUpdate(JET_SESID sesid, JET_TABLEID tableid, void *bookmark, ULONG cb, ULONG *actual)
{
    return JetUpdate2(sesid, tableid, bookmark, cb, actual, 0);
}

JET_ERR WINAPI JetDelete(JET_SESID sesid, JET_TABLEID tableid)
{
    CURSOR_API(cur, err = cur->readonly ? JET_errPermissionDenied : ese_delete(cur))
}

JET_ERR WINAPI JetSetColumn(JET_SESID sesid, JET_TABLEID tableid, JET_COLUMNID id, const void *data, ULONG cb,
                            JET_GRBIT grbit, JET_SETINFO *info)
{
    CURSOR_API(cur, {
        if (info && info->cbStruct != sizeof(*info))
            err = JET_errInvalidParameter;
        else
            err = ese_set_column(cur, id, data, cb, grbit, info);
    })
}

JET_ERR WINAPI JetSetColumns(JET_SESID sesid, JET_TABLEID tableid, JET_SETCOLUMN *columns, ULONG count)
{
    CURSOR_API(cur, {
        if (!columns || !count)
            err = JET_errInvalidParameter;
        else {
            err = JET_errSuccess;
            for (ULONG i = 0; i < count; i++) {
                JET_SETINFO info = {sizeof(info), columns[i].ibLongValue, columns[i].itagSequence};
                columns[i].err = ese_set_column(cur, columns[i].columnid, columns[i].pvData, columns[i].cbData,
                                                columns[i].grbit, &info);
                if (columns[i].err < 0) {
                    err = columns[i].err;
                    break;
                }
            }
        }
    })
}

JET_ERR WINAPI JetRetrieveColumn(JET_SESID sesid, JET_TABLEID tableid, JET_COLUMNID id, void *data, ULONG cb,
                                 ULONG *actual, JET_GRBIT grbit, JET_RETINFO *info)
{
    CURSOR_API(cur, {
        if (info && info->cbStruct != sizeof(*info))
            err = JET_errInvalidParameter;
        else if (cb && !data)
            err = JET_errInvalidParameter;
        else
            err = ese_retrieve_column(cur, id, data, cb, actual, grbit, info);
    })
}

JET_ERR WINAPI JetRetrieveColumns(JET_SESID sesid, JET_TABLEID tableid, JET_RETRIEVECOLUMN *columns, ULONG count)
{
    CURSOR_API(cur, {
        if (!columns || !count)
            err = JET_errInvalidParameter;
        else {
            err = JET_errSuccess;
            for (ULONG i = 0; i < count; i++) {
                JET_RETINFO info = {sizeof(info), columns[i].ibLongValue, columns[i].itagSequence, 0};
                ULONG actual = 0;
                columns[i].err = ese_retrieve_column(cur, columns[i].columnid, columns[i].pvData, columns[i].cbData,
                                                     &actual, columns[i].grbit, &info);
                columns[i].cbActual = actual;
                if (columns[i].err < 0) {
                    err = columns[i].err;
                    break;
                }
            }
        }
    })
}

JET_ERR WINAPI JetEscrowUpdate(JET_SESID sesid, JET_TABLEID tableid, JET_COLUMNID id, void *delta, ULONG deltacb,
                               void *old, ULONG oldcb, ULONG *oldactual, JET_GRBIT grbit)
{
    CURSOR_API(cur, err = ese_escrow(cur, id, delta, deltacb, old, oldcb, oldactual, grbit))
}

JET_ERR WINAPI JetEnumerateColumns(JET_SESID sesid, JET_TABLEID tableid, ULONG count, JET_ENUMCOLUMNID *ids,
                                   ULONG *out_count, JET_ENUMCOLUMN **out, void *(WINAPI *alloc)(void *, void *, ULONG),
                                   void *context, ULONG max_data, JET_GRBIT grbit)
{
    CURSOR_API(cur, {
        if (!out_count || !out || !alloc || (count && !ids))
            err = JET_errInvalidParameter;
        else
            err = api_enumerate(cur, count, ids, out_count, out, alloc, context, max_data, grbit);
    })
}

/* ------------------------------------------------------------------ movement and keys */

JET_ERR WINAPI JetMove(JET_SESID sesid, JET_TABLEID tableid, LONG rows, JET_GRBIT grbit)
{
    CURSOR_API(cur, {
        if (grbit & ~JET_bitMoveKeyNE)
            err = JET_errInvalidGrbit;
        else
            err = ese_move(cur, rows, grbit);
    })
}

JET_ERR WINAPI JetMakeKey(JET_SESID sesid, JET_TABLEID tableid, const void *data, ULONG cb, JET_GRBIT grbit)
{
    CURSOR_API(cur, err = ese_make_key(cur, data, cb, grbit))
}

JET_ERR WINAPI JetRetrieveKey(JET_SESID sesid, JET_TABLEID tableid, void *buffer, ULONG cb, ULONG *actual,
                              JET_GRBIT grbit)
{
    CURSOR_API(cur, {
        const BYTE *key;
        ULONG length;
        if (grbit & JET_bitRetrieveCopy) {
            if (!cur->mkey_flags) {
                err = JET_errKeyNotMade;
                key = nullptr;
                length = 0;
            } else {
                key = cur->mkey;
                length = cur->mkeycb;
            }
        } else if (cur->pos != 1) {
            err = JET_errNoCurrentRecord;
            key = nullptr;
            length = 0;
        } else if (grbit & JET_bitRetrieveFromPrimaryBookmark) {
            key = cur->bmcache + 4;
            length = *(ULONG *)cur->bmcache;
        } else {
            key = cur->ekey;
            length = cur->ekeycb;
        }
        if (!err) {
            if (actual)
                *actual = length;
            if (buffer && cb)
                memcpy(buffer, key, length < cb ? length : cb);
            if (length > cb)
                err = JET_wrnBufferTruncated;
        }
    })
}

JET_ERR WINAPI JetSeek(JET_SESID sesid, JET_TABLEID tableid, JET_GRBIT grbit)
{
    CURSOR_API(cur, err = ese_seek(cur, grbit))
}

JET_ERR WINAPI JetSetIndexRange(JET_SESID sesid, JET_TABLEID tableid, JET_GRBIT grbit)
{
    CURSOR_API(cur, err = ese_set_index_range(cur, grbit))
}

static JET_ERR set_current_index(JET_SESID sesid, JET_TABLEID tableid, LPCWSTR name, JET_GRBIT grbit, ULONG tag)
{
    CURSOR_API(cur, err = ese_set_current_index(cur, name, grbit, tag))
}

JET_ERR WINAPI JetSetCurrentIndexW(JET_SESID s, JET_TABLEID t, LPCWSTR name)
{
    return set_current_index(s, t, name, 0, 1);
}

JET_ERR WINAPI JetSetCurrentIndex2W(JET_SESID s, JET_TABLEID t, LPCWSTR name, JET_GRBIT grbit)
{
    return set_current_index(s, t, name, grbit, 1);
}

JET_ERR WINAPI JetSetCurrentIndex3W(JET_SESID s, JET_TABLEID t, LPCWSTR name, JET_GRBIT grbit, ULONG tag)
{
    return set_current_index(s, t, name, grbit, tag);
}

JET_ERR WINAPI JetSetCurrentIndex4W(JET_SESID s, JET_TABLEID t, LPCWSTR name, void *indexid, JET_GRBIT grbit, ULONG tag)
{
    return set_current_index(s, t, name, grbit, tag);
}

JET_ERR WINAPI JetSetCurrentIndexA(JET_SESID s, JET_TABLEID t, LPCSTR name)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return set_current_index(s, t, name ? w : nullptr, 0, 1);
}

JET_ERR WINAPI JetSetCurrentIndex2A(JET_SESID s, JET_TABLEID t, LPCSTR name, JET_GRBIT grbit)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return set_current_index(s, t, name ? w : nullptr, grbit, 1);
}

JET_ERR WINAPI JetSetCurrentIndex3A(JET_SESID s, JET_TABLEID t, LPCSTR name, JET_GRBIT grbit, ULONG tag)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return set_current_index(s, t, name ? w : nullptr, grbit, tag);
}

JET_ERR WINAPI JetSetCurrentIndex4A(JET_SESID s, JET_TABLEID t, LPCSTR name, void *indexid, JET_GRBIT grbit, ULONG tag)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return set_current_index(s, t, name ? w : nullptr, grbit, tag);
}

JET_ERR WINAPI JetSetCurrentIndex(JET_SESID s, JET_TABLEID t, LPCSTR name)
{
    return JetSetCurrentIndexA(s, t, name);
}

JET_ERR WINAPI JetSetCurrentIndex2(JET_SESID s, JET_TABLEID t, LPCSTR name, JET_GRBIT grbit)
{
    return JetSetCurrentIndex2A(s, t, name, grbit);
}

JET_ERR WINAPI JetSetCurrentIndex3(JET_SESID s, JET_TABLEID t, LPCSTR name, JET_GRBIT grbit, ULONG tag)
{
    return JetSetCurrentIndex3A(s, t, name, grbit, tag);
}

JET_ERR WINAPI JetSetCurrentIndex4(JET_SESID s, JET_TABLEID t, LPCSTR name, void *indexid, JET_GRBIT grbit, ULONG tag)
{
    return JetSetCurrentIndex4A(s, t, name, indexid, grbit, tag);
}

JET_ERR WINAPI JetGetCurrentIndexW(JET_SESID sesid, JET_TABLEID tableid, LPWSTR name, ULONG cb)
{
    CURSOR_API(cur, {
        if (!name || cb < sizeof(WCHAR))
            err = JET_errInvalidParameter;
        else {
            const WCHAR *n = cur->index->sequential ? L"" : cur->index->name;
            if ((ULONG)(lstrlenW(n) + 1) * sizeof(WCHAR) > cb)
                err = JET_errBufferTooSmall;
            else
                lstrcpyW(name, n);
        }
    })
}

JET_ERR WINAPI JetGetCurrentIndexA(JET_SESID sesid, JET_TABLEID tableid, LPSTR name, ULONG cb)
{
    CURSOR_API(cur, {
        if (!name || !cb)
            err = JET_errInvalidParameter;
        else {
            const WCHAR *n = cur->index->sequential ? L"" : cur->index->name;
            if ((ULONG)(lstrlenW(n) + 1) > cb)
                err = JET_errBufferTooSmall;
            else
                api_ansi_out(n, name, cb);
        }
    })
}

JET_ERR WINAPI JetGetCurrentIndex(JET_SESID sesid, JET_TABLEID tableid, LPSTR name, ULONG cb)
{
    return JetGetCurrentIndexA(sesid, tableid, name, cb);
}

/* ------------------------------------------------------------------ bookmarks and positions */

JET_ERR WINAPI JetGetBookmark(JET_SESID sesid, JET_TABLEID tableid, void *buffer, ULONG cb, ULONG *actual)
{
    CURSOR_API(cur, err = ese_get_bookmark(cur, buffer, cb, actual))
}

JET_ERR WINAPI JetGotoBookmark(JET_SESID sesid, JET_TABLEID tableid, void *buffer, ULONG cb)
{
    CURSOR_API(cur, err = ese_goto_bookmark(cur, buffer, cb))
}

JET_ERR WINAPI JetGetSecondaryIndexBookmark(JET_SESID sesid, JET_TABLEID tableid, void *skey, ULONG skey_cb,
                                            ULONG *skey_actual, void *pkey, ULONG pkey_cb, ULONG *pkey_actual,
                                            JET_GRBIT grbit)
{
    CURSOR_API(cur, {
        if (cur->pos != 1)
            err = JET_errNoCurrentRecord;
        else {
            ULONG pk_length = *(ULONG *)cur->bmcache;
            if (skey_actual)
                *skey_actual = cur->ekeycb;
            if (pkey_actual)
                *pkey_actual = pk_length;
            if (skey && skey_cb >= cur->ekeycb)
                memcpy(skey, cur->ekey, cur->ekeycb);
            if (pkey && pkey_cb >= pk_length)
                memcpy(pkey, cur->bmcache + 4, pk_length);
            if ((skey && skey_cb < cur->ekeycb) || (pkey && pkey_cb < pk_length))
                err = JET_errBufferTooSmall;
        }
    })
}

JET_ERR WINAPI JetGotoSecondaryIndexBookmark(JET_SESID sesid, JET_TABLEID tableid, void *skey, ULONG skey_cb, void *pkey,
                                             ULONG pkey_cb, JET_GRBIT grbit)
{
    CURSOR_API(cur, {
        if (!skey || !pkey)
            err = JET_errInvalidBookmark;
        else
            err = api_goto_secondary(cur, skey, skey_cb, pkey, pkey_cb, grbit);
    })
}

JET_ERR WINAPI JetGetRecordPosition(JET_SESID sesid, JET_TABLEID tableid, JET_RECPOS *pos, ULONG cb)
{
    CURSOR_API(cur, {
        if (!pos || cb < sizeof(*pos))
            err = JET_errInvalidParameter;
        else if (cur->pos != 1)
            err = JET_errNoCurrentRecord;
        else
            err = api_record_position(cur, pos);
    })
}

JET_ERR WINAPI JetGotoPosition(JET_SESID sesid, JET_TABLEID tableid, JET_RECPOS *pos)
{
    CURSOR_API(cur, {
        if (!pos || pos->cbStruct != sizeof(*pos))
            err = JET_errInvalidParameter;
        else
            err = api_goto_position(cur, pos);
    })
}

JET_ERR WINAPI JetIndexRecordCount(JET_SESID sesid, JET_TABLEID tableid, ULONG *count, ULONG maximum)
{
    CURSOR_API(cur, {
        if (!count)
            err = JET_errInvalidParameter;
        else
            err = ese_index_count(cur, count, maximum);
    })
}

JET_ERR WINAPI JetIndexRecordCount2(JET_SESID sesid, JET_TABLEID tableid, ULONG64 *count, ULONG64 maximum)
{
    ULONG small;
    JET_ERR err = JetIndexRecordCount(sesid, tableid, &small, maximum > 0xffffffff ? 0 : (ULONG)maximum);
    if (!err && count)
        *count = small;
    return err;
}

JET_ERR WINAPI JetSetTableSequential(JET_SESID sesid, JET_TABLEID tableid, JET_GRBIT grbit)
{
    CURSOR_API(cur, cur->sequential = TRUE)
}

JET_ERR WINAPI JetResetTableSequential(JET_SESID sesid, JET_TABLEID tableid, JET_GRBIT grbit)
{
    CURSOR_API(cur, cur->sequential = FALSE)
}

JET_ERR WINAPI JetGetLS(JET_SESID sesid, JET_TABLEID tableid, JET_LS *ls, JET_GRBIT grbit)
{
    CURSOR_API(cur, {
        if (!ls)
            err = JET_errInvalidParameter;
        else {
            *ls = cur->ls ? cur->ls : JET_LSNil;
            if (grbit & 1)
                cur->ls = 0;
            if (*ls == JET_LSNil)
                err = JET_errSuccess;
        }
    })
}

JET_ERR WINAPI JetSetLS(JET_SESID sesid, JET_TABLEID tableid, JET_LS ls, JET_GRBIT grbit)
{
    CURSOR_API(cur, cur->ls = ls == JET_LSNil ? 0 : ls)
}

JET_ERR WINAPI JetComputeStats(JET_SESID sesid, JET_TABLEID tableid)
{
    CURSOR_API(cur, {
        cur->table->stat_records = ese_visible_count(cur, cur->table->primary, 0xffffffff);
        cur->table->stat_pages = 1 + cur->table->stat_records / 20;
    })
}

JET_ERR WINAPI JetPrereadKeys(JET_SESID sesid, JET_TABLEID tableid, const void **keys, const ULONG *sizes, LONG count,
                              LONG *processed, JET_GRBIT grbit)
{
    CURSOR_API(cur, {
        if (!keys || !sizes || count <= 0)
            err = JET_errInvalidParameter;
        else if (processed)
            *processed = count;
    })
}

JET_ERR WINAPI JetPrereadIndexRange(JET_SESID sesid, JET_TABLEID tableid, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5,
                                    ULONG_PTR a6, ULONG_PTR a7)
{
    CURSOR_API(cur, (void)cur)
}

JET_ERR WINAPI JetPrereadIndexRanges(JET_SESID sesid, JET_TABLEID tableid, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5,
                                     ULONG_PTR a6, ULONG_PTR a7, ULONG_PTR a8)
{
    CURSOR_API(cur, (void)cur)
}

JET_ERR WINAPI JetRegisterCallback(JET_SESID sesid, JET_TABLEID tableid, JET_CBTYP type, void *callback, void *context,
                                   JET_HANDLE *handle)
{
    CURSOR_API(cur, {
        if (!callback || !handle)
            err = JET_errInvalidParameter;
        else
            *handle = (JET_HANDLE)callback;
    })
}

JET_ERR WINAPI JetUnregisterCallback(JET_SESID sesid, JET_TABLEID tableid, JET_CBTYP type, JET_HANDLE handle)
{
    CURSOR_API(cur, (void)cur)
}

/* ------------------------------------------------------------------ tables made at once */

typedef struct CCOL {
    void *name;
    ULONG coltyp, cbmax, grbit;
    void *def;
    ULONG defcb, cp;
    JET_COLUMNID *id;
    JET_ERR *err;
} CCOL;

/* the parts of a table to create, from any of the versions of the structures. */
typedef struct TCREATE {
    BOOL wide;
    void *name, *template_name;
    ULONG pages, density, ncols, nidx, grbit;
    BYTE *cols;
    BYTE *idxs;
    ULONG idx_version;
    JET_TABLEID *tableid;
    ULONG *created;
} TCREATE;

static void name_of(BOOL wide, const void *name, WCHAR *out)
{
    if (wide)
        lstrcpynW(out, name, JET_cbNameMost + 2);
    else
        api_wide(name, out, JET_cbNameMost + 2);
}

JET_ERR api_create_table_full(SESSION *sess, DBH *dbh, BOOL wide, ULONG version, void *spec, JET_TABLEID *tableid_out)
{
    return JET_errSuccess;
}

static JET_ERR create_columns_and_indexes(SESSION *sess, TABLE *table, BOOL wide, ULONG version, ULONG ncols,
                                          JET_COLUMNCREATE *cols, ULONG nidx, void *idxs, ULONG *created)
{
    JET_ERR first_error = JET_errSuccess;
    for (ULONG i = 0; i < ncols; i++) {
        JET_COLUMNCREATE *cc = (JET_COLUMNCREATE *)((BYTE *)cols + i * cols->cbStruct);
        if (cc->cbStruct != sizeof(JET_COLUMNCREATE)) {
            return JET_errInvalidParameter;
        }
        WCHAR name[JET_cbNameMost + 2];
        name_of(wide, cc->szColumnName, name);
        JET_COLUMNDEF def = {sizeof(def), 0, cc->coltyp, 0, 0, (USHORT)cc->cp, 0, cc->cbMax, cc->grbit};
        cc->err = ese_add_column(sess, table, name, &def, cc->pvDefault, cc->cbDefault, &cc->columnid);
        if (cc->err < 0) {
            if (!first_error)
                first_error = cc->err;
            return first_error;
        }
        if (created)
            (*created)++;
    }
    for (ULONG i = 0; i < nidx; i++) {
        JET_ERR err = api_one_index(sess, table, wide, version, (BYTE *)idxs, i);
        if (err < 0)
            return err;
        if (created)
            (*created)++;
    }
    return first_error;
}

static JET_ERR create_table_from(JET_SESID handle, JET_DBID dbid, BOOL wide, ULONG version, void *spec)
{
    SESSION *sess;
    DBH *dbh;
    JET_ERR err;
    LOCK();
    sess = api_session(handle, &err);
    dbh = sess ? ese_dbh(sess, dbid) : nullptr;
    if (sess && !dbh)
        err = JET_errInvalidDatabaseId;
    if (!err)
        err = api_create_table_spec(sess, dbh, wide, version, spec);
    UNLOCK();
    return err;
}

#define TC(name, wide, version)                                                                                        \
    JET_ERR WINAPI name(JET_SESID s, JET_DBID db, void *spec) { return create_table_from(s, db, wide, version, spec); }

TC(JetCreateTableColumnIndexW, TRUE, 1)
TC(JetCreateTableColumnIndexA, FALSE, 1)
TC(JetCreateTableColumnIndex, FALSE, 1)
TC(JetCreateTableColumnIndex2W, TRUE, 2)
TC(JetCreateTableColumnIndex2A, FALSE, 2)
TC(JetCreateTableColumnIndex2, FALSE, 2)
TC(JetCreateTableColumnIndex3W, TRUE, 3)
TC(JetCreateTableColumnIndex3A, FALSE, 3)
TC(JetCreateTableColumnIndex4W, TRUE, 4)
TC(JetCreateTableColumnIndex4A, FALSE, 4)
TC(JetCreateTableColumnIndex5W, TRUE, 5)
TC(JetCreateTableColumnIndex5A, FALSE, 5)

JET_ERR api_create_table_spec(SESSION *sess, DBH *dbh, BOOL wide, ULONG version, void *spec)
{
    /* every version begins with the same fields and the callback and space hints of the later ones follow. */
    JET_TABLECREATE *tc = spec;
    if (!tc)
        return JET_errInvalidParameter;
    ULONG expected = tc->cbStruct;
    if (version == 1 && expected != sizeof(JET_TABLECREATE))
        return JET_errInvalidParameter;
    WCHAR name[JET_cbNameMost + 2], template_name[JET_cbNameMost + 2];
    if (!tc->szTableName)
        return JET_errInvalidParameter;
    name_of(wide, tc->szTableName, name);
    template_name[0] = 0;
    if (tc->szTemplateTableName)
        name_of(wide, tc->szTemplateTableName, template_name);
    if (tc->ulDensity && (tc->ulDensity < 20 || tc->ulDensity > 100))
        return JET_errDensityInvalid;
    BOOL implicit;
    api_ddl_begin(sess, &implicit);
    TABLE *table;
    JET_ERR err = ese_create_table(sess, dbh, name, tc->ulPages, tc->ulDensity, &table);
    ULONG created = 0;
    if (!err) {
        if (template_name[0]) {
            TABLE *source = ese_find_table(dbh->db, template_name);
            if (!source || !source->is_template)
                err = source ? JET_errInvalidParameter : JET_errObjectNotFound;
            else {
                lstrcpyW(table->template_name, template_name);
                for (ULONG i = 0; i < source->ncols && !err; i++) {
                    COLUMN *c = source->cols[i];
                    if (c->deleted)
                        continue;
                    JET_COLUMNDEF def = {sizeof(def), 0, c->coltyp, c->country, c->langid, c->cp, c->collate, c->cbMax,
                                         c->grbit};
                    err = ese_add_column(sess, table, c->name, &def, c->def, c->defcb, nullptr);
                }
            }
        }
        if (!err && (tc->grbit & JET_bitTableCreateTemplateTable))
            table->is_template = TRUE;
    }
    if (!err)
        err = create_columns_and_indexes(sess, table, wide, version, tc->cColumns, tc->rgcolumncreate, tc->cIndexes,
                                         tc->rgindexcreate, &created);
    if (!err) {
        CURSOR *cur;
        err = ese_open_cursor(sess, dbh, table, JET_bitTablePermitDDL | JET_bitTableUpdatable, &cur);
        if (!err) {
            cur->ddl = TRUE;
            table->creator = cur;
            if (tc->grbit & JET_bitTableCreateTemplateTable) {
                ese_close_cursor(cur);
                tc->tableid = 0;
            } else {
                tc->tableid = (JET_TABLEID)cur;
            }
        }
    }
    tc->cCreated = created + (err < 0 ? 0 : 1);
    api_ddl_end(sess, implicit, err < 0 ? err : JET_errSuccess);
    return err;
}
