/* the jet api: information about databases, tables, columns and indexes, temporary tables and the backup calls. */
#include "ese.h"
#include "api.h"

#define LOCK() EnterCriticalSection(&g_ese_lock)
#define UNLOCK() LeaveCriticalSection(&g_ese_lock)

/* ------------------------------------------------------------------ temporary tables of results */

typedef struct LISTCOL {
    const WCHAR *name;
    JET_COLTYP type;
    ULONG cbmax;
} LISTCOL;

static JET_ERR make_temp_table(SESSION *sess, const LISTCOL *defs, ULONG n, BOOL wide, JET_COLUMNID *ids, CURSOR **out)
{
    TABLE *table = ese_alloc(sizeof(*table));
    table->db = &g_temp_db;
    table->temp = TRUE;
    table->next_id = 1;
    table->last_var = 127;
    table->last_tagged = 255;
    wcscpy(table->name, L"temp");
    INDEX *idx = ese_alloc(sizeof(*idx));
    idx->primary = idx->sequential = TRUE;
    table->primary = idx;
    table->idx = ese_alloc(8 * sizeof(INDEX *));
    table->nicap = 8;
    table->idx[table->nidx++] = idx;
    for (ULONG i = 0; i < n; i++) {
        JET_COLUMNDEF def = {sizeof(def), 0, defs[i].type, 0, 0, wide ? 1200 : 1252, 0, defs[i].cbmax, 0};
        JET_ERR err = ese_add_column(sess, table, defs[i].name, &def, nullptr, 0, &ids[i]);
        if (err) {
            ese_free_table(table);
            return err;
        }
    }
    return ese_open_cursor(sess, nullptr, table, JET_bitTableUpdatable, out);
}

typedef struct ROW {
    CURSOR *cur;
    JET_ERR err;
} ROW;

static void row_begin(ROW *r)
{
    r->err = ese_prepare(r->cur, JET_prepInsert);
}

static void row_long(ROW *r, JET_COLUMNID id, ULONG value)
{
    if (!r->err)
        r->err = ese_set_column(r->cur, id, &value, 4, 0, nullptr);
}

static void row_short(ROW *r, JET_COLUMNID id, USHORT value)
{
    if (!r->err)
        r->err = ese_set_column(r->cur, id, &value, 2, 0, nullptr);
}

static void row_double(ROW *r, JET_COLUMNID id, double value)
{
    if (!r->err)
        r->err = ese_set_column(r->cur, id, &value, 8, 0, nullptr);
}

static void row_text(ROW *r, JET_COLUMNID id, const WCHAR *text, BOOL wide)
{
    if (r->err)
        return;
    if (wide) {
        r->err = ese_set_column(r->cur, id, text, (ULONG)lstrlenW(text) * 2, JET_bitSetZeroLength, nullptr);
    } else {
        char buffer[512];
        int n = WideCharToMultiByte(CP_ACP, 0, text, -1, buffer, sizeof(buffer), nullptr, nullptr);
        r->err = ese_set_column(r->cur, id, buffer, n > 0 ? (ULONG)n - 1 : 0, JET_bitSetZeroLength, nullptr);
    }
}

static void row_binary(ROW *r, JET_COLUMNID id, const void *data, ULONG cb)
{
    if (!r->err && cb)
        r->err = ese_set_column(r->cur, id, data, cb, 0, nullptr);
}

static JET_ERR row_end(ROW *r)
{
    if (r->err) {
        ese_prepare(r->cur, JET_prepCancel);
        return r->err;
    }
    return ese_update(r->cur, nullptr, 0, nullptr, 0);
}

/* ------------------------------------------------------------------ columns */

static void fill_column_def(const COLUMN *col, JET_COLUMNDEF *def)
{
    memset(def, 0, sizeof(*def));
    def->cbStruct = sizeof(*def);
    def->columnid = col->id;
    def->coltyp = col->coltyp;
    def->wCountry = col->country;
    def->langid = col->langid;
    def->cp = col->cp;
    def->wCollate = col->collate;
    def->cbMax = col->cbMax;
    def->grbit = col->grbit;
}

static JET_ERR column_info(SESSION *sess, DBH *dbh, TABLE *table, LPCWSTR name, void *out, ULONG cb, ULONG level,
                           BOOL wide)
{
    ULONG base_level = level & 0x1fffffff;
    BOOL minimal = (level & JET_ColInfoGrbitMinimalInfo) != 0;
    switch (base_level) {
    case JET_ColInfo:
    case JET_ColInfoByColid: {
        COLUMN *col;
        if (base_level == JET_ColInfoByColid) {
            if (!name)
                return JET_errInvalidParameter;
            col = ese_column_by_id(table, *(JET_COLUMNID *)name);
        } else {
            if (!name)
                return JET_errInvalidParameter;
            col = ese_find_column(table, name);
        }
        if (!col)
            return JET_errColumnNotFound;
        if (cb < sizeof(JET_COLUMNDEF))
            return JET_errInvalidParameter;
        fill_column_def(col, out);
        return JET_errSuccess;
    }
    case JET_ColInfoBase:
    case JET_ColInfoBaseByColid: {
        COLUMN *col;
        if (!name)
            return JET_errInvalidParameter;
        col = base_level == JET_ColInfoBaseByColid ? ese_column_by_id(table, *(JET_COLUMNID *)name)
                                                    : ese_find_column(table, name);
        if (!col)
            return JET_errColumnNotFound;
        if (wide) {
            JET_COLUMNBASE_W *b = out;
            if (cb < sizeof(*b))
                return JET_errInvalidParameter;
            memset(b, 0, sizeof(*b));
            b->cbStruct = sizeof(*b);
            b->columnid = col->id;
            b->coltyp = col->coltyp;
            b->wCountry = col->country;
            b->langid = col->langid;
            b->cp = col->cp;
            b->cbMax = col->cbMax;
            b->grbit = col->grbit;
            lstrcpyW(b->szBaseTableName, table->name);
            lstrcpyW(b->szBaseColumnName, col->name);
        } else {
            JET_COLUMNBASE_A *b = out;
            if (cb < sizeof(*b))
                return JET_errInvalidParameter;
            memset(b, 0, sizeof(*b));
            b->cbStruct = sizeof(*b);
            b->columnid = col->id;
            b->coltyp = col->coltyp;
            b->wCountry = col->country;
            b->langid = col->langid;
            b->cp = col->cp;
            b->cbMax = col->cbMax;
            b->grbit = col->grbit;
            api_ansi_out(table->name, b->szBaseTableName, 256);
            api_ansi_out(col->name, b->szBaseColumnName, 256);
        }
        return JET_errSuccess;
    }
    case JET_ColInfoList:
    case JET_ColInfoListCompact:
    case JET_ColInfoListSortColumnid: {
        if (cb < sizeof(JET_COLUMNLIST))
            return JET_errInvalidParameter;
        static const LISTCOL defs[] = {{L"Presentation order", JET_coltypLong, 0}, {L"Name", JET_coltypText, 255},
                                       {L"Id", JET_coltypLong, 0}, {L"Coltyp", JET_coltypLong, 0},
                                       {L"Country", JET_coltypShort, 0}, {L"Langid", JET_coltypShort, 0},
                                       {L"Cp", JET_coltypShort, 0}, {L"Collate", JET_coltypShort, 0},
                                       {L"Max", JET_coltypLong, 0}, {L"Grbit", JET_coltypLong, 0},
                                       {L"Default", JET_coltypLongBinary, 0}, {L"BaseTableName", JET_coltypText, 255},
                                       {L"BaseColumnName", JET_coltypText, 255}, {L"DefinitionName", JET_coltypText, 255}};
        JET_COLUMNID ids[14];
        CURSOR *cur;
        JET_ERR err = make_temp_table(sess, defs, 14, wide, ids, &cur);
        if (err)
            return err;
        ROW row = {cur, 0};
        ULONG order = 0;
        for (ULONG pass = 0; pass < table->ncols && !err; pass++) {
            COLUMN *c = table->cols[pass];
            if (c->deleted)
                continue;
            if (name && (level & 0xe0000000) == 0 && base_level == JET_ColInfoList && FALSE)
                continue;
            row_begin(&row);
            row_long(&row, ids[0], ++order);
            row_text(&row, ids[1], c->name, wide);
            row_long(&row, ids[2], c->id);
            row_long(&row, ids[3], c->coltyp);
            row_short(&row, ids[4], c->country);
            row_short(&row, ids[5], c->langid);
            row_short(&row, ids[6], c->cp);
            row_short(&row, ids[7], c->collate);
            row_long(&row, ids[8], c->cbMax);
            row_long(&row, ids[9], c->grbit);
            if (!minimal)
                row_binary(&row, ids[10], c->def, c->defcb);
            row_text(&row, ids[11], table->name, wide);
            row_text(&row, ids[12], c->name, wide);
            row_text(&row, ids[13], c->name, wide);
            err = row_end(&row);
        }
        if (err) {
            ese_close_cursor(cur);
            return err;
        }
        JET_COLUMNLIST *list = out;
        memset(list, 0, sizeof(*list));
        list->cbStruct = sizeof(*list);
        list->tableid = (JET_TABLEID)cur;
        list->cRecord = order;
        list->columnidPresentationOrder = ids[0];
        list->columnidcolumnname = ids[1];
        list->columnidcolumnid = ids[2];
        list->columnidcoltyp = ids[3];
        list->columnidCountry = ids[4];
        list->columnidLangid = ids[5];
        list->columnidCp = ids[6];
        list->columnidCollate = ids[7];
        list->columnidcbMax = ids[8];
        list->columnidgrbit = ids[9];
        list->columnidDefault = ids[10];
        list->columnidBaseTableName = ids[11];
        list->columnidBaseColumnName = ids[12];
        list->columnidDefinitionName = ids[13];
        ese_move(cur, JET_MoveFirst, 0);
        return JET_errSuccess;
    }
    default:
        return JET_errInvalidParameter;
    }
}

static JET_ERR table_by_name(SESSION *sess, JET_DBID dbid, LPCWSTR name, DBH **dbh, TABLE **table)
{
    *dbh = ese_dbh(sess, dbid);
    if (!*dbh)
        return JET_errInvalidDatabaseId;
    if (!name)
        return JET_errInvalidParameter;
    *table = ese_find_table((*dbh)->db, name);
    return *table ? JET_errSuccess : JET_errObjectNotFound;
}

static JET_ERR get_column_info(JET_SESID handle, JET_DBID dbid, LPCWSTR table_name, const void *column, void *out,
                               ULONG cb, ULONG level, BOOL wide)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess) {
        DBH *dbh;
        TABLE *table;
        err = table_by_name(sess, dbid, table_name, &dbh, &table);
        if (!err) {
            if (!out)
                err = JET_errInvalidParameter;
            else {
                WCHAR name[JET_cbNameMost + 2];
                ULONG base = level & 0x1fffffff;
                const void *arg = column;
                if (column && base != JET_ColInfoByColid && base != JET_ColInfoBaseByColid) {
                    if (wide)
                        lstrcpynW(name, column, JET_cbNameMost + 2);
                    else
                        api_wide(column, name, JET_cbNameMost + 2);
                    arg = name;
                }
                err = column_info(sess, dbh, table, arg, out, cb, level, wide);
            }
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetColumnInfoW(JET_SESID s, JET_DBID db, LPCWSTR t, LPCWSTR c, void *out, ULONG cb, ULONG level)
{
    return get_column_info(s, db, t, c, out, cb, level, TRUE);
}

JET_ERR WINAPI JetGetColumnInfoA(JET_SESID s, JET_DBID db, LPCSTR t, LPCSTR c, void *out, ULONG cb, ULONG level)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(t, w, JET_cbNameMost + 2);
    return get_column_info(s, db, t ? w : nullptr, c, out, cb, level, FALSE);
}

JET_ERR WINAPI JetGetColumnInfo(JET_SESID s, JET_DBID db, LPCSTR t, LPCSTR c, void *out, ULONG cb, ULONG level)
{
    return JetGetColumnInfoA(s, db, t, c, out, cb, level);
}

static JET_ERR get_table_column_info(JET_SESID sesid, JET_TABLEID tableid, const void *column, void *out, ULONG cb,
                                     ULONG level, BOOL wide)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = ese_cursor(sesid, tableid, &err);
    if (cur) {
        if (!out)
            err = JET_errInvalidParameter;
        else {
            WCHAR name[JET_cbNameMost + 2];
            ULONG base = level & 0x1fffffff;
            const void *arg = column;
            if (column && base != JET_ColInfoByColid && base != JET_ColInfoBaseByColid) {
                if (wide)
                    lstrcpynW(name, column, JET_cbNameMost + 2);
                else
                    api_wide(column, name, JET_cbNameMost + 2);
                arg = name;
            }
            err = column_info(cur->sess, cur->dbh, cur->table, arg, out, cb, level, wide);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetTableColumnInfoW(JET_SESID s, JET_TABLEID t, LPCWSTR c, void *out, ULONG cb, ULONG level)
{
    return get_table_column_info(s, t, c, out, cb, level, TRUE);
}

JET_ERR WINAPI JetGetTableColumnInfoA(JET_SESID s, JET_TABLEID t, LPCSTR c, void *out, ULONG cb, ULONG level)
{
    return get_table_column_info(s, t, c, out, cb, level, FALSE);
}


/* ------------------------------------------------------------------ indexes */

static ULONG index_entry_count(CURSOR *cur, INDEX *idx)
{
    return ese_visible_count(cur, idx, 0xffffffff);
}

static JET_ERR index_info(SESSION *sess, DBH *dbh, TABLE *table, LPCWSTR name, void *out, ULONG cb, ULONG level,
                          BOOL wide)
{
    INDEX *named = nullptr;
    if (name && *name) {
        named = ese_find_index(table, name);
        if (!named && !table->primary->sequential && !lstrcmpiW(table->primary->name, name))
            named = table->primary;
    }
    switch (level) {
    case JET_IdxInfo:
    case JET_IdxInfoList: {
        if (level == JET_IdxInfo && !named)
            return name && *name ? JET_errIndexNotFound : JET_errInvalidParameter;
        if (cb < sizeof(JET_INDEXLIST))
            return JET_errInvalidParameter;
        static const LISTCOL defs[] = {{L"Name", JET_coltypText, 255}, {L"Grbit", JET_coltypLong, 0},
                                       {L"Key", JET_coltypLong, 0}, {L"Entries", JET_coltypLong, 0},
                                       {L"Pages", JET_coltypLong, 0}, {L"Columns", JET_coltypLong, 0},
                                       {L"Column", JET_coltypLong, 0}, {L"Id", JET_coltypLong, 0},
                                       {L"Coltyp", JET_coltypLong, 0}, {L"Country", JET_coltypShort, 0},
                                       {L"Langid", JET_coltypShort, 0}, {L"Cp", JET_coltypShort, 0},
                                       {L"Collate", JET_coltypShort, 0}, {L"ColumnGrbit", JET_coltypLong, 0},
                                       {L"ColumnName", JET_coltypText, 255}, {L"LCMapFlags", JET_coltypLong, 0}};
        JET_COLUMNID ids[16];
        CURSOR *cur;
        JET_ERR err = make_temp_table(sess, defs, 16, wide, ids, &cur);
        if (err)
            return err;
        CURSOR *counter;
        ese_open_cursor(sess, dbh, table, 0, &counter);
        ROW row = {cur, 0};
        ULONG rows = 0;
        for (ULONG i = 0; i < table->nidx && !err; i++) {
            INDEX *idx = table->idx[i];
            if (idx->deleted || idx->sequential || (named && idx != named))
                continue;
            ULONG entries = index_entry_count(counter, idx);
            for (ULONG s = 0; s < idx->nsegs && !err; s++) {
                COLUMN *c = ese_column_by_id(table, idx->segs[s].id);
                if (!c)
                    continue;
                row_begin(&row);
                row_text(&row, ids[0], idx->name, wide);
                row_long(&row, ids[1], idx->grbit);
                row_long(&row, ids[2], entries);
                row_long(&row, ids[3], entries);
                row_long(&row, ids[4], 1);
                row_long(&row, ids[5], idx->nsegs);
                row_long(&row, ids[6], s);
                row_long(&row, ids[7], c->id);
                row_long(&row, ids[8], c->coltyp);
                row_short(&row, ids[9], c->country);
                row_short(&row, ids[10], c->langid);
                row_short(&row, ids[11], c->cp);
                row_short(&row, ids[12], c->collate);
                row_long(&row, ids[13], idx->segs[s].desc ? 0x1 : 0);
                row_text(&row, ids[14], c->name, wide);
                row_long(&row, ids[15], idx->map_flags);
                err = row_end(&row);
                rows++;
            }
        }
        ese_close_cursor(counter);
        if (err) {
            ese_close_cursor(cur);
            return err;
        }
        JET_INDEXLIST *list = out;
        memset(list, 0, sizeof(*list));
        list->cbStruct = sizeof(*list);
        list->tableid = (JET_TABLEID)cur;
        list->cRecord = rows;
        list->columnidindexname = ids[0];
        list->columnidgrbitIndex = ids[1];
        list->columnidcKey = ids[2];
        list->columnidcEntry = ids[3];
        list->columnidcPage = ids[4];
        list->columnidcColumn = ids[5];
        list->columnidiColumn = ids[6];
        list->columnidcolumnid = ids[7];
        list->columnidcoltyp = ids[8];
        list->columnidCountry = ids[9];
        list->columnidLangid = ids[10];
        list->columnidCp = ids[11];
        list->columnidCollate = ids[12];
        list->columnidgrbitColumn = ids[13];
        list->columnidcolumnname = ids[14];
        list->columnidLCMapFlags = ids[15];
        ese_move(cur, JET_MoveFirst, 0);
        return JET_errSuccess;
    }
    case JET_IdxInfoSpaceAlloc:
    case JET_IdxInfoCount:
    case JET_IdxInfoVarSegMac:
    case JET_IdxInfoKeyMost: {
        if (cb < sizeof(ULONG))
            return JET_errInvalidParameter;
        ULONG value = level == JET_IdxInfoSpaceAlloc ? 1 : level == JET_IdxInfoVarSegMac ? (named ? named->varsegmac : 0)
                      : JET_cbKeyMost;
        if (level == JET_IdxInfoCount) {
            value = 0;
            for (ULONG i = 0; i < table->nidx; i++)
                if (!table->idx[i]->deleted && !table->idx[i]->sequential)
                    value++;
        }
        *(ULONG *)out = value;
        return JET_errSuccess;
    }
    case JET_IdxInfoLangid: {
        if (!named)
            return JET_errIndexNotFound;
        if (cb < sizeof(USHORT))
            return JET_errInvalidParameter;
        *(USHORT *)out = (USHORT)named->lcid;
        return JET_errSuccess;
    }
    case JET_IdxInfoIndexId: {
        if (!named)
            return JET_errIndexNotFound;
        if (cb < 2 * sizeof(ULONG))
            return JET_errInvalidParameter;
        memset(out, 0, cb);
        *(ULONG *)out = (ULONG)cb;
        return JET_errSuccess;
    }
    default:
        return JET_errInvalidParameter;
    }
}

static JET_ERR get_index_info(JET_SESID handle, JET_DBID dbid, LPCWSTR table_name, const void *index, void *out,
                              ULONG cb, ULONG level, BOOL wide)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess) {
        DBH *dbh;
        TABLE *table;
        err = table_by_name(sess, dbid, table_name, &dbh, &table);
        if (!err) {
            if (!out)
                err = JET_errInvalidParameter;
            else {
                WCHAR name[JET_cbNameMost + 2];
                name[0] = 0;
                if (index) {
                    if (wide)
                        lstrcpynW(name, index, JET_cbNameMost + 2);
                    else
                        api_wide(index, name, JET_cbNameMost + 2);
                }
                err = index_info(sess, dbh, table, index ? name : nullptr, out, cb, level, wide);
            }
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetIndexInfoW(JET_SESID s, JET_DBID db, LPCWSTR t, LPCWSTR i, void *out, ULONG cb, ULONG level)
{
    return get_index_info(s, db, t, i, out, cb, level, TRUE);
}

JET_ERR WINAPI JetGetIndexInfoA(JET_SESID s, JET_DBID db, LPCSTR t, LPCSTR i, void *out, ULONG cb, ULONG level)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(t, w, JET_cbNameMost + 2);
    return get_index_info(s, db, t ? w : nullptr, i, out, cb, level, FALSE);
}

JET_ERR WINAPI JetGetIndexInfo(JET_SESID s, JET_DBID db, LPCSTR t, LPCSTR i, void *out, ULONG cb, ULONG level)
{
    return JetGetIndexInfoA(s, db, t, i, out, cb, level);
}

static JET_ERR get_table_index_info(JET_SESID sesid, JET_TABLEID tableid, const void *index, void *out, ULONG cb,
                                    ULONG level, BOOL wide)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = ese_cursor(sesid, tableid, &err);
    if (cur) {
        if (!out)
            err = JET_errInvalidParameter;
        else {
            WCHAR name[JET_cbNameMost + 2];
            name[0] = 0;
            if (index) {
                if (wide)
                    lstrcpynW(name, index, JET_cbNameMost + 2);
                else
                    api_wide(index, name, JET_cbNameMost + 2);
            }
            err = index_info(cur->sess, cur->dbh, cur->table, name, out, cb, level, wide);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetTableIndexInfoW(JET_SESID s, JET_TABLEID t, LPCWSTR i, void *out, ULONG cb, ULONG level)
{
    return get_table_index_info(s, t, i, out, cb, level, TRUE);
}

JET_ERR WINAPI JetGetTableIndexInfoA(JET_SESID s, JET_TABLEID t, LPCSTR i, void *out, ULONG cb, ULONG level)
{
    return get_table_index_info(s, t, i, out, cb, level, FALSE);
}

JET_ERR WINAPI JetGetTableIndexInfo(JET_SESID s, JET_TABLEID t, LPCSTR i, void *out, ULONG cb, ULONG level)
{
    return JetGetTableIndexInfoA(s, t, i, out, cb, level);
}

/* ------------------------------------------------------------------ tables and objects */

static double ole_date(ULONG64 filetime)
{
    return (double)(LONGLONG)(filetime - 94353120000000000LL) / 864000000000.0;
}

static JET_ERR fill_object_info(SESSION *sess, DBH *dbh, TABLE *table, JET_OBJECTINFO *info)
{
    CURSOR *counter;
    ese_open_cursor(sess, dbh, table, 0, &counter);
    ULONG records = ese_visible_count(counter, table->primary, 0xffffffff);
    ese_close_cursor(counter);
    memset(info, 0, sizeof(*info));
    info->cbStruct = sizeof(*info);
    info->objtyp = JET_objtypTable;
    info->dtCreate = ole_date(table->created);
    info->dtUpdate = ole_date(table->updated);
    info->grbit = 0;
    info->flags = table->is_template ? 0x4 : 0;
    (void)records;
    info->cRecord = table->stat_records;
    info->cPage = table->stat_pages;
    return JET_errSuccess;
}

static JET_ERR table_info(CURSOR *cur, void *out, ULONG cb, ULONG level, BOOL wide)
{
    TABLE *table = cur->table;
    switch (level) {
    case JET_TblInfo:
        if (cb < sizeof(JET_OBJECTINFO))
            return JET_errInvalidParameter;
        return fill_object_info(cur->sess, cur->dbh, table, out);
    case JET_TblInfoName: {
        if (wide) {
            if (cb < (ULONG)(lstrlenW(table->name) + 1) * sizeof(WCHAR))
                return JET_errBufferTooSmall;
            lstrcpyW(out, table->name);
        } else {
            if (cb < (ULONG)lstrlenW(table->name) + 1)
                return JET_errBufferTooSmall;
            api_ansi_out(table->name, out, cb);
        }
        return JET_errSuccess;
    }
    case JET_TblInfoDbid:
        if (cb < sizeof(ULONG))
            return JET_errInvalidParameter;
        *(ULONG *)out = cur->dbh ? cur->dbh->id : JET_dbidNil;
        return JET_errSuccess;
    case JET_TblInfoRvu:
        if (cb < sizeof(ULONG))
            return JET_errInvalidParameter;
        *(ULONG *)out = 0;
        return JET_errSuccess;
    case JET_TblInfoTemplateTableName: {
        const WCHAR *n = table->template_name;
        if (wide) {
            if (cb < (ULONG)(lstrlenW(n) + 1) * sizeof(WCHAR))
                return JET_errBufferTooSmall;
            lstrcpyW(out, n);
        } else {
            if (cb < (ULONG)lstrlenW(n) + 1)
                return JET_errBufferTooSmall;
            api_ansi_out(n, out, cb);
        }
        return JET_errSuccess;
    }
    case JET_TblInfoSpaceOwned:
    case JET_TblInfoSpaceAvailable: {
        if (cb < sizeof(ULONG))
            return JET_errInvalidParameter;
        ULONG records = ese_visible_count(cur, table->primary, 0xffffffff);
        *(ULONG *)out = level == JET_TblInfoSpaceOwned ? 2 + records / 20 : 0;
        return JET_errSuccess;
    }
    default:
        return JET_errFeatureNotAvailable;
    }
}

static JET_ERR get_table_info(JET_SESID sesid, JET_TABLEID tableid, void *out, ULONG cb, ULONG level, BOOL wide)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = ese_cursor(sesid, tableid, &err);
    if (cur) {
        if (!out)
            err = JET_errInvalidParameter;
        else
            err = table_info(cur, out, cb, level, wide);
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetTableInfoW(JET_SESID s, JET_TABLEID t, void *out, ULONG cb, ULONG level)
{
    return get_table_info(s, t, out, cb, level, TRUE);
}

JET_ERR WINAPI JetGetTableInfoA(JET_SESID s, JET_TABLEID t, void *out, ULONG cb, ULONG level)
{
    return get_table_info(s, t, out, cb, level, FALSE);
}

JET_ERR WINAPI JetGetTableInfo(JET_SESID s, JET_TABLEID t, void *out, ULONG cb, ULONG level)
{
    return get_table_info(s, t, out, cb, level, FALSE);
}

JET_ERR WINAPI JetSetTableInfoW(JET_SESID sesid, JET_TABLEID tableid, void *data, ULONG cb, ULONG level)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = ese_cursor(sesid, tableid, &err);
    if (cur)
        err = JET_errInvalidParameter;
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetSetTableInfoA(JET_SESID s, JET_TABLEID t, void *data, ULONG cb, ULONG level)
{
    return JetSetTableInfoW(s, t, data, cb, level);
}

JET_ERR WINAPI JetSetTableInfo(JET_SESID s, JET_TABLEID t, void *data, ULONG cb, ULONG level)
{
    return JetSetTableInfoW(s, t, data, cb, level);
}

static JET_ERR get_object_info(JET_SESID handle, JET_DBID dbid, ULONG objtyp, const void *container, const void *object,
                               void *out, ULONG cb, ULONG level, BOOL wide)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    DBH *dbh = sess ? ese_dbh(sess, dbid) : nullptr;
    if (sess && !dbh)
        err = JET_errInvalidDatabaseId;
    if (!err && !out)
        err = JET_errInvalidParameter;
    if (!err) {
        if (level == JET_ObjInfo || level == JET_ObjInfoNoStats) {
            WCHAR name[JET_cbNameMost + 2];
            if (!object)
                err = JET_errInvalidParameter;
            else {
                if (wide)
                    lstrcpynW(name, object, JET_cbNameMost + 2);
                else
                    api_wide(object, name, JET_cbNameMost + 2);
                TABLE *table = ese_find_table(dbh->db, name);
                if (!table)
                    err = JET_errObjectNotFound;
                else if (cb < sizeof(JET_OBJECTINFO))
                    err = JET_errInvalidParameter;
                else
                    err = fill_object_info(sess, dbh, table, out);
            }
        } else if (level == JET_ObjInfoList || level == JET_ObjInfoListNoStats || level == JET_ObjInfoSystemObjects) {
            if (cb < sizeof(JET_OBJECTLIST)) {
                err = JET_errInvalidParameter;
            } else {
                static const LISTCOL defs[] = {{L"ContainerName", JET_coltypText, 255}, {L"ObjectName", JET_coltypText, 255},
                                               {L"ObjectType", JET_coltypLong, 0}, {L"DtCreate", JET_coltypIEEEDouble, 0},
                                               {L"DtUpdate", JET_coltypIEEEDouble, 0}, {L"Grbit", JET_coltypLong, 0},
                                               {L"Flags", JET_coltypLong, 0}, {L"Records", JET_coltypLong, 0},
                                               {L"Pages", JET_coltypLong, 0}};
                JET_COLUMNID ids[9];
                CURSOR *cur;
                err = make_temp_table(sess, defs, 9, wide, ids, &cur);
                ULONG rows = 0;
                if (!err) {
                    ROW row = {cur, 0};
                    for (TABLE *t = dbh->db->tables; t && !err; t = t->next) {
                        if (t->deleted)
                            continue;
                        JET_OBJECTINFO info;
                        fill_object_info(sess, dbh, t, &info);
                        row_begin(&row);
                        row_text(&row, ids[0], L"Tables", wide);
                        row_text(&row, ids[1], t->name, wide);
                        row_long(&row, ids[2], JET_objtypTable);
                        row_double(&row, ids[3], info.dtCreate);
                        row_double(&row, ids[4], info.dtUpdate);
                        row_long(&row, ids[5], info.grbit);
                        row_long(&row, ids[6], info.flags);
                        row_long(&row, ids[7], info.cRecord);
                        row_long(&row, ids[8], info.cPage);
                        err = row_end(&row);
                        rows++;
                    }
                    if (err) {
                        ese_close_cursor(cur);
                    } else {
                        JET_OBJECTLIST *list = out;
                        memset(list, 0, sizeof(*list));
                        list->cbStruct = sizeof(*list);
                        list->tableid = (JET_TABLEID)cur;
                        list->cRecord = rows;
                        list->columnidcontainername = ids[0];
                        list->columnidobjectname = ids[1];
                        list->columnidobjtyp = ids[2];
                        list->columniddtCreate = ids[3];
                        list->columniddtUpdate = ids[4];
                        list->columnidgrbit = ids[5];
                        list->columnidflags = ids[6];
                        list->columnidcRecord = ids[7];
                        list->columnidcPage = ids[8];
                        ese_move(cur, JET_MoveFirst, 0);
                    }
                }
            }
        } else {
            err = JET_errInvalidParameter;
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetObjectInfoW(JET_SESID s, JET_DBID db, ULONG type, LPCWSTR container, LPCWSTR object, void *out,
                                 ULONG cb, ULONG level)
{
    return get_object_info(s, db, type, container, object, out, cb, level, TRUE);
}

JET_ERR WINAPI JetGetObjectInfoA(JET_SESID s, JET_DBID db, ULONG type, LPCSTR container, LPCSTR object, void *out,
                                 ULONG cb, ULONG level)
{
    return get_object_info(s, db, type, container, object, out, cb, level, FALSE);
}

JET_ERR WINAPI JetGetObjectInfo(JET_SESID s, JET_DBID db, ULONG type, LPCSTR container, LPCSTR object, void *out,
                                ULONG cb, ULONG level)
{
    return JetGetObjectInfoA(s, db, type, container, object, out, cb, level);
}

/* ------------------------------------------------------------------ record sizes and cursor information */

JET_ERR WINAPI JetGetRecordSize(JET_SESID sesid, JET_TABLEID tableid, JET_RECSIZE *size, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = ese_cursor(sesid, tableid, &err);
    if (cur) {
        const VERSION *v;
        if (!size)
            err = JET_errInvalidParameter;
        else if (grbit & JET_bitRetrieveCopy) {
            v = cur->copy;
            if (!v)
                err = JET_errUpdateNotPrepared;
        } else {
            v = ese_current_version(cur);
            if (!v)
                err = cur->pos == 1 ? JET_errRecordDeleted : JET_errNoCurrentRecord;
        }
        if (!err) {
            memset(size, 0, sizeof(*size));
            for (ULONG i = 0; i < cur->table->ncols; i++) {
                COLUMN *c = cur->table->cols[i];
                const CELL *cell = ese_cell(v, i);
                if (c->deleted || !cell->count)
                    continue;
                BOOL lv = c->coltyp == JET_coltypLongBinary || c->coltyp == JET_coltypLongText;
                if (c->grbit & JET_bitColumnTagged)
                    size->cTaggedColumns++;
                else
                    size->cNonTaggedColumns++;
                if (cell->count > 1)
                    size->cMultiValues += cell->count - 1;
                for (ULONG k = 0; k < cell->count; k++) {
                    if (lv) {
                        size->cLongValues++;
                        size->cbLongValueData += cell->vals[k].cb;
                        size->cbLongValueOverhead += 8;
                    } else {
                        size->cbData += cell->vals[k].cb;
                    }
                }
                size->cbOverhead += 2;
            }
            size->cbOverhead += 4;
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetRecordSize2(JET_SESID sesid, JET_TABLEID tableid, JET_RECSIZE *size, JET_GRBIT grbit)
{
    return JetGetRecordSize(sesid, tableid, size, grbit);
}

JET_ERR WINAPI JetGetRecordSize3(JET_SESID sesid, JET_TABLEID tableid, JET_RECSIZE *size, JET_GRBIT grbit)
{
    return JetGetRecordSize(sesid, tableid, size, grbit);
}

JET_ERR WINAPI JetGetCursorInfo(JET_SESID sesid, JET_TABLEID tableid, void *info, ULONG cb, ULONG level)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = ese_cursor(sesid, tableid, &err);
    if (cur && (level != 0 || (info && cb)))
        err = JET_errInvalidParameter;
    UNLOCK();
    return err;
}

/* ------------------------------------------------------------------ temporary tables */

static JET_ERR open_temp(JET_SESID handle, const JET_COLUMNDEF *defs, ULONG count, ULONG lcid, JET_GRBIT grbit,
                         JET_TABLEID *out, JET_COLUMNID *ids)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess) {
        if (!defs || !count || !out || !ids)
            err = JET_errInvalidParameter;
        else if ((grbit & JET_bitTTForwardOnly) && (grbit & (JET_bitTTScrollable | JET_bitTTUpdatable)))
            err = JET_errCannotMaterializeForwardOnlySort;
        else {
            TABLE *table = ese_alloc(sizeof(*table));
            table->db = &g_temp_db;
            table->temp = TRUE;
            table->next_id = 1;
            table->last_var = 127;
            table->last_tagged = 255;
            lstrcpyW(table->name, L"temp");
            INDEX *idx = ese_alloc(sizeof(*idx));
            idx->primary = idx->sequential = TRUE;
            table->primary = idx;
            table->idx = ese_alloc(8 * sizeof(INDEX *));
            table->nicap = 8;
            table->idx[table->nidx++] = idx;
            WCHAR key[1300];
            ULONG chars = 0;
            for (ULONG i = 0; i < count && !err; i++) {
                JET_COLUMNDEF def = defs[i];
                if (defs[i].cbStruct != sizeof(JET_COLUMNDEF)) {
                    err = JET_errInvalidParameter;
                    break;
                }
                WCHAR name[24] = L"column";
                ULONG digits = 0, v = i;
                do {
                    digits++;
                } while (v /= 10);
                name[6 + digits] = 0;
                for (v = i; digits; v /= 10)
                    name[6 + --digits] = (WCHAR)(L'0' + v % 10);
                def.grbit &= ~(JET_bitColumnTTKey | JET_bitColumnTTDescending);
                err = ese_add_column(sess, table, name, &def, nullptr, 0, &ids[i]);
                if (!err && (defs[i].grbit & JET_bitColumnTTKey)) {
                    key[chars++] = (defs[i].grbit & JET_bitColumnTTDescending) ? L'-' : L'+';
                    lstrcpyW(key + chars, name);
                    chars += (ULONG)lstrlenW(name) + 1;
                }
            }
            if (!err && chars) {
                key[chars] = 0;
                err = ese_create_index(sess, table, L"primary", JET_bitIndexPrimary | ((grbit & JET_bitTTUnique) ? JET_bitIndexUnique : 0),
                                       key, (chars + 1) * sizeof(WCHAR), 0, lcid, 0, 0, nullptr, nullptr, 0, TRUE, 0);
            }
            CURSOR *cur;
            if (!err)
                err = ese_open_cursor(sess, nullptr, table, JET_bitTableUpdatable, &cur);
            if (err)
                ese_free_table(table);
            else
                *out = (JET_TABLEID)cur;
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetOpenTempTable(JET_SESID s, const JET_COLUMNDEF *defs, ULONG count, JET_GRBIT grbit, JET_TABLEID *out,
                                JET_COLUMNID *ids)
{
    return open_temp(s, defs, count, 0, grbit, out, ids);
}

JET_ERR WINAPI JetOpenTempTable2(JET_SESID s, const JET_COLUMNDEF *defs, ULONG count, ULONG lcid, JET_GRBIT grbit,
                                 JET_TABLEID *out, JET_COLUMNID *ids)
{
    return open_temp(s, defs, count, lcid, grbit, out, ids);
}

JET_ERR WINAPI JetOpenTempTable3(JET_SESID s, const JET_COLUMNDEF *defs, ULONG count, JET_UNICODEINDEX *unicode,
                                 JET_GRBIT grbit, JET_TABLEID *out, JET_COLUMNID *ids)
{
    return open_temp(s, defs, count, unicode ? unicode->lcid : 0, grbit, out, ids);
}

JET_ERR WINAPI JetOpenTemporaryTable(JET_SESID s, JET_OPENTEMPORARYTABLE *spec)
{
    if (!spec || spec->cbStruct != sizeof(*spec))
        return JET_errInvalidParameter;
    JET_ERR err = open_temp(s, spec->prgcolumndef, spec->ccolumn, spec->pidxunicode ? ((JET_UNICODEINDEX *)spec->pidxunicode)->lcid : 0,
                            spec->grbit, &spec->tableid, spec->prgcolumnid);
    return err;
}

JET_ERR WINAPI JetOpenTemporaryTable2(JET_SESID s, JET_OPENTEMPORARYTABLE *spec)
{
    return JetOpenTemporaryTable(s, spec);
}

/* ------------------------------------------------------------------ databases */

static JET_ERR db_info_common(DB *db, const WCHAR *path, void *out, ULONG cb, ULONG level, BOOL wide)
{
    switch (level) {
    case JET_DbInfoFilename: {
        if (wide) {
            if (cb < (ULONG)(lstrlenW(path) + 1) * sizeof(WCHAR))
                return JET_errBufferTooSmall;
            lstrcpyW(out, path);
        } else {
            if (cb < (ULONG)lstrlenW(path) + 1)
                return JET_errBufferTooSmall;
            api_ansi_out(path, out, cb);
        }
        return JET_errSuccess;
    }
    case JET_DbInfoConnect:
        if (cb < (wide ? 2u : 1u))
            return JET_errBufferTooSmall;
        if (wide)
            *(WCHAR *)out = 0;
        else
            *(char *)out = 0;
        return JET_errSuccess;
    case JET_DbInfoCountry:
    case JET_DbInfoLCID:
    case JET_DbInfoCp:
    case JET_DbInfoCollate:
    case JET_DbInfoOptions:
    case JET_DbInfoTransactions:
    case JET_DbInfoVersion:
    case JET_DbInfoIsam:
        if (cb < sizeof(ULONG))
            return JET_errInvalidParameter;
        *(ULONG *)out = level == JET_DbInfoCountry ? 1 : level == JET_DbInfoLCID ? 0x409 : level == JET_DbInfoCp ? 1252
                        : level == JET_DbInfoTransactions ? ESE_MAX_TRANSACTION_LEVEL : level == JET_DbInfoVersion ? 0x620
                        : level == JET_DbInfoIsam ? 1 : 0;
        return JET_errSuccess;
    case JET_DbInfoPageSize:
        if (cb < sizeof(ULONG))
            return JET_errInvalidParameter;
        *(ULONG *)out = db->pagesize;
        return JET_errSuccess;
    case JET_DbInfoFileType:
        if (cb < sizeof(ULONG))
            return JET_errInvalidParameter;
        *(ULONG *)out = JET_filetypeDatabase;
        return JET_errSuccess;
    case JET_DbInfoFilesize:
    case JET_DbInfoFilesizeOnDisk:
    case JET_DbInfoSpaceOwned:
    case JET_DbInfoSpaceAvailable: {
        WIN32_FILE_ATTRIBUTE_DATA data;
        ULONG64 size = 0;
        if (GetFileAttributesExW(path, GetFileExInfoStandard, &data))
            size = ((ULONG64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        if (level == JET_DbInfoFilesizeOnDisk) {
            if (cb < 8)
                return JET_errInvalidParameter;
            *(ULONG64 *)out = size;
        } else {
            if (cb < sizeof(ULONG))
                return JET_errInvalidParameter;
            ULONG pages = (ULONG)(size / db->pagesize);
            *(ULONG *)out = level == JET_DbInfoSpaceAvailable ? 0 : pages > 2 ? pages - 2 : 0;
        }
        return JET_errSuccess;
    }
    case JET_DbInfoUpgrade:
        if (cb < sizeof(JET_DBINFOUPGRADE))
            return JET_errInvalidParameter;
        memset(out, 0, sizeof(JET_DBINFOUPGRADE));
        ((JET_DBINFOUPGRADE *)out)->cbStruct = sizeof(JET_DBINFOUPGRADE);
        return JET_errSuccess;
    case JET_DbInfoMisc: {
        if (cb < sizeof(JET_DBINFOMISC))
            return JET_errInvalidParameter;
        JET_DBINFOMISC *m = out;
        memset(m, 0, sizeof(*m));
        m->ulVersion = db->version;
        m->ulUpdate = db->update;
        m->signDb = db->sign;
        m->dbstate = JET_dbstateCleanShutdown;
        m->dwMajorVersion = 10;
        m->dwBuildNumber = 26300;
        m->cbPageSize = db->pagesize;
        return JET_errSuccess;
    }
    case JET_DbInfoDBInUse:
        if (cb < sizeof(ULONG))
            return JET_errInvalidParameter;
        *(ULONG *)out = db->refs != 0;
        return JET_errSuccess;
    default:
        return JET_errInvalidParameter;
    }
}

static JET_ERR get_database_info(JET_SESID handle, JET_DBID dbid, void *out, ULONG cb, ULONG level, BOOL wide)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    DBH *dbh = sess ? ese_dbh(sess, dbid) : nullptr;
    if (sess && !dbh)
        err = JET_errInvalidDatabaseId;
    if (!err && !out)
        err = JET_errInvalidParameter;
    if (!err)
        err = db_info_common(dbh->db, dbh->db->path, out, cb, level, wide);
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetDatabaseInfoW(JET_SESID s, JET_DBID db, void *out, ULONG cb, ULONG level)
{
    return get_database_info(s, db, out, cb, level, TRUE);
}

JET_ERR WINAPI JetGetDatabaseInfoA(JET_SESID s, JET_DBID db, void *out, ULONG cb, ULONG level)
{
    return get_database_info(s, db, out, cb, level, FALSE);
}

JET_ERR WINAPI JetGetDatabaseInfo(JET_SESID s, JET_DBID db, void *out, ULONG cb, ULONG level)
{
    return get_database_info(s, db, out, cb, level, FALSE);
}

static JET_ERR get_database_file_info(LPCWSTR path, void *out, ULONG cb, ULONG level)
{
    if (!path || !out)
        return JET_errInvalidParameter;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return JET_errFileNotFound;
    DB db = {};
    db.pagesize = g_global_pagesize;
    JET_ERR err = ese_read_header(&db, path);
    if (err)
        return err;
    return db_info_common(&db, path, out, cb, level, TRUE);
}

JET_ERR WINAPI JetGetDatabaseFileInfoW(LPCWSTR path, void *out, ULONG cb, ULONG level)
{
    return get_database_file_info(path, out, cb, level);
}

JET_ERR WINAPI JetGetDatabaseFileInfoA(LPCSTR path, void *out, ULONG cb, ULONG level)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return get_database_file_info(path ? w : nullptr, out, cb, level);
}

JET_ERR WINAPI JetGetDatabaseFileInfo(LPCSTR path, void *out, ULONG cb, ULONG level)
{
    return JetGetDatabaseFileInfoA(path, out, cb, level);
}

JET_ERR WINAPI JetSetDatabaseSizeW(JET_SESID handle, LPCWSTR path, ULONG pages, ULONG *actual)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess && (!path || !*path))
        err = JET_errInvalidParameter;
    if (!err && actual)
        *actual = pages;
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetSetDatabaseSizeA(JET_SESID handle, LPCSTR path, ULONG pages, ULONG *actual)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return JetSetDatabaseSizeW(handle, path ? w : nullptr, pages, actual);
}

JET_ERR WINAPI JetSetDatabaseSize(JET_SESID handle, LPCSTR path, ULONG pages, ULONG *actual)
{
    return JetSetDatabaseSizeA(handle, path, pages, actual);
}

JET_ERR WINAPI JetGrowDatabase(JET_SESID handle, JET_DBID dbid, ULONG pages, ULONG *actual)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess && !ese_dbh(sess, dbid))
        err = JET_errInvalidDatabaseId;
    if (!err && actual)
        *actual = pages;
    UNLOCK();
    return err;
}
