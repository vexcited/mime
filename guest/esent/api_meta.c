/* the jet api: indexes made from structures, the information calls, temporary tables and what is enumerated. */
#include "ese.h"
#include "api.h"

#define LOCK() EnterCriticalSection(&g_ese_lock)
#define UNLOCK() LeaveCriticalSection(&g_ese_lock)

DB g_temp_db;

/* ------------------------------------------------------------------ indexes made from structures */

/* the sizes of the structure that are taken, the earliest without the last key length and the later ones with it. */
static BOOL index_size_valid(ULONG version, ULONG cb)
{
    ULONG v1 = (ULONG)offsetof(JET_INDEXCREATE, cbKeyMost), v1b = (ULONG)sizeof(JET_INDEXCREATE);
    if (version <= 1)
        return cb == v1 || cb == v1b;
    return cb >= v1b;
}

JET_ERR api_one_index(SESSION *sess, TABLE *table, BOOL wide, ULONG version, BYTE *base, ULONG index)
{
    JET_INDEXCREATE *first = (JET_INDEXCREATE *)base;
    if (!first->cbStruct)
        return JET_errInvalidParameter;
    JET_INDEXCREATE *ic = (JET_INDEXCREATE *)(base + (SIZE_T)index * first->cbStruct);
    if (!index_size_valid(version, ic->cbStruct))
        return JET_errInvalidParameter;
    WCHAR name[JET_cbNameMost + 2];
    if (!ic->szIndexName || !ic->szKey)
        return JET_errInvalidParameter;
    if (wide)
        lstrcpynW(name, ic->szIndexName, JET_cbNameMost + 2);
    else
        api_wide(ic->szIndexName, name, JET_cbNameMost + 2);
    WCHAR key[1300];
    ULONG chars;
    if (wide) {
        chars = ic->cbKey / sizeof(WCHAR);
        if (chars > 1290)
            return JET_errInvalidParameter;
        memcpy(key, ic->szKey, chars * sizeof(WCHAR));
    } else {
        chars = 0;
        const char *k = ic->szKey;
        for (ULONG i = 0; i < ic->cbKey && chars < 1280;) {
            ULONG n = (ULONG)lstrlenA(k + i);
            MultiByteToWideChar(CP_ACP, 0, k + i, (int)n + 1, key + chars, 1290 - chars);
            chars += n + 1;
            i += n + 1;
        }
    }
    key[chars] = 0;
    ULONG lcid = 0, map_flags = 0;
    if (ic->grbit & JET_bitIndexUnicode) {
        if (!ic->pidxunicode) {
            ic->err = JET_errInvalidParameter;
            return ic->err;
        }
        if (version >= 4) {
            JET_UNICODEINDEX2 *u = ic->pidxunicode;
            lcid = LocaleNameToLCID(u->szLocaleName, 0);
            map_flags = u->dwMapFlags;
        } else {
            JET_UNICODEINDEX *u = ic->pidxunicode;
            lcid = u->lcid;
            map_flags = u->dwMapFlags;
        }
    } else {
        lcid = ic->lcid;
    }
    JET_CONDITIONALCOLUMN conds[12];
    WCHAR cond_names[12][JET_cbNameMost + 2];
    if (ic->cConditionalColumn > 12)
        return ic->err = JET_errInvalidParameter;
    for (ULONG i = 0; i < ic->cConditionalColumn; i++) {
        JET_CONDITIONALCOLUMN *src = &ic->rgconditionalcolumn[i];
        if (!ic->rgconditionalcolumn || src->cbStruct != sizeof(*src))
            return ic->err = JET_errInvalidParameter;
        if (wide)
            lstrcpynW(cond_names[i], src->szColumnName, JET_cbNameMost + 2);
        else
            api_wide(src->szColumnName, cond_names[i], JET_cbNameMost + 2);
        conds[i].cbStruct = sizeof(conds[i]);
        conds[i].szColumnName = cond_names[i];
        conds[i].grbit = src->grbit;
    }
    if (ic->grbit & JET_bitIndexTuples)
        return ic->err = JET_errFeatureNotAvailable;
    ULONG varsegmac = (ic->grbit & JET_bitIndexTupleLimits) ? 0 : ic->cbVarSegMac;
    ic->err = ese_create_index(sess, table, name, ic->grbit, key, (chars + 1) * sizeof(WCHAR), ic->ulDensity, lcid,
                               map_flags, varsegmac, (ic->grbit & JET_bitIndexTupleLimits) ? ic->ptuplelimits : nullptr,
                               conds, ic->cConditionalColumn, TRUE,
                               (ic->grbit & JET_bitIndexKeyMost) && ic->cbStruct >= sizeof(JET_INDEXCREATE) ? ic->cbKeyMost : 0);
    return ic->err;
}

static JET_ERR create_indexes(JET_SESID handle, JET_TABLEID table, BOOL wide, ULONG version, void *specs, ULONG count)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = api_ddl_cursor(handle, table, &err);
    if (cur) {
        if (!specs || !count)
            err = JET_errInvalidParameter;
        else {
            BOOL implicit;
            api_ddl_begin(cur->sess, &implicit);
            for (ULONG i = 0; i < count; i++) {
                JET_ERR one = api_one_index(cur->sess, cur->table, wide, version, specs, i);
                if (one < 0) {
                    err = one;
                    break;
                }
            }
            api_ddl_end(cur->sess, implicit, err);
        }
    }
    UNLOCK();
    return err;
}

#define CI(name, wide, version)                                                                                        \
    JET_ERR WINAPI name(JET_SESID s, JET_TABLEID t, void *specs, ULONG count)                                          \
    {                                                                                                                  \
        return create_indexes(s, t, wide, version, specs, count);                                                      \
    }

CI(JetCreateIndex2W, TRUE, 1)
CI(JetCreateIndex2A, FALSE, 1)
CI(JetCreateIndex2, FALSE, 1)
CI(JetCreateIndex3W, TRUE, 2)
CI(JetCreateIndex3A, FALSE, 2)
CI(JetCreateIndex4W, TRUE, 4)
CI(JetCreateIndex4A, FALSE, 4)

/* ------------------------------------------------------------------ positions */

JET_ERR api_record_position(CURSOR *cur, JET_RECPOS *pos)
{
    ULONG before = 0, total = 0;
    INDEX *idx = cur->index;
    BOOL found;
    ULONG pkcb = cur->bmcache ? *(ULONG *)cur->bmcache : 0;
    LONG here = ese_entry_search(idx, cur->ekey, cur->ekeycb, cur->bmcache + 4, pkcb, &found);
    for (LONG i = 0; i < (LONG)idx->count; i++) {
        VERSION *v = ese_visible(idx->entries[i].rec, cur->sess);
        if (!v)
            continue;
        if (i < here)
            before++;
        total++;
    }
    pos->cbStruct = sizeof(*pos);
    pos->centriesLT = before;
    pos->centriesInRange = total;
    pos->centriesTotal = total;
    return JET_errSuccess;
}

JET_ERR api_goto_position(CURSOR *cur, JET_RECPOS *pos)
{
    ULONG total = ese_visible_count(cur, cur->index, 0xffffffff);
    if (!total)
        return JET_errRecordNotFound;
    ULONG target = pos->centriesTotal ? (ULONG)((ULONG64)pos->centriesLT * total / pos->centriesTotal) : 0;
    if (target >= total)
        target = total - 1;
    JET_ERR err = ese_move(cur, JET_MoveFirst, 0);
    if (!err && target)
        err = ese_move(cur, (LONG)target, 0);
    return err;
}

JET_ERR api_goto_secondary(CURSOR *cur, void *skey, ULONG skey_cb, void *pkey, ULONG pkey_cb, JET_GRBIT grbit)
{
    BOOL found;
    LONG at = ese_entry_search(cur->index, skey, skey_cb, pkey, pkey_cb, &found);
    if (!found)
        return JET_errRecordDeleted;
    ENTRY *e = &cur->index->entries[at];
    if (!ese_visible(e->rec, cur->sess))
        return JET_errRecordDeleted;
    ese_free(cur->bmcache);
    cur->bmcache = ese_alloc(e->rec->pkcb + 4);
    *(ULONG *)cur->bmcache = e->rec->pkcb;
    memcpy(cur->bmcache + 4, e->rec->pk, e->rec->pkcb);
    ese_free(cur->ekey);
    cur->ekey = ese_dup(e->key, e->cb);
    cur->ekeycb = e->cb;
    cur->pos = 1;
    return JET_errSuccess;
}

/* ------------------------------------------------------------------ enumerating the columns of a record */

JET_ERR api_enumerate(CURSOR *cur, ULONG count, JET_ENUMCOLUMNID *ids, ULONG *out_count, JET_ENUMCOLUMN **out,
                      void *(WINAPI *alloc)(void *, void *, ULONG), void *context, ULONG max_data, JET_GRBIT grbit)
{
    TABLE *table = cur->table;
    const VERSION *v;
    if (grbit & JET_bitEnumerateCopy) {
        if (!cur->copy)
            return JET_errUpdateNotPrepared;
        v = cur->copy;
    } else {
        v = ese_current_version(cur);
        if (!v)
            return cur->pos == 1 ? JET_errRecordDeleted : JET_errNoCurrentRecord;
    }
    ULONG n = count;
    JET_COLUMNID *all = nullptr;
    if (!count) {
        all = ese_alloc((table->ncols + 1) * sizeof(JET_COLUMNID));
        for (ULONG i = 0; i < table->ncols; i++) {
            COLUMN *c = table->cols[i];
            if (c->deleted)
                continue;
            if ((grbit & JET_bitEnumerateTaggedOnly) && !(c->grbit & JET_bitColumnTagged))
                continue;
            if (!ese_cell(v, i)->count && !(c->defcb && !ese_cell(v, i)->set))
                continue;
            all[n++] = c->id;
        }
    }
    JET_ENUMCOLUMN *result = alloc(context, nullptr, (n ? n : 1) * sizeof(JET_ENUMCOLUMN));
    if (!result) {
        ese_free(all);
        return JET_errOutOfMemory;
    }
    memset(result, 0, (n ? n : 1) * sizeof(JET_ENUMCOLUMN));
    JET_ERR first = JET_errSuccess;
    for (ULONG i = 0; i < n; i++) {
        JET_COLUMNID id = count ? ids[i].columnid : all[i];
        JET_ENUMCOLUMN *ec = &result[i];
        ec->columnid = id;
        COLUMN *col = ese_column_by_id(table, id);
        if (!col) {
            ec->err = JET_errColumnNotFound;
            continue;
        }
        const CELL *cell = ese_cell(v, ese_slot(table, id));
        const COLUMN *c = col;
        ULONG want = count ? ids[i].ctagSequence : 0;
        BOOL use_default = !cell->count && !cell->set && c->defcb && !(grbit & JET_bitEnumerateIgnoreDefault);
        if (!cell->count && !use_default) {
            ec->err = JET_wrnColumnNull;
            continue;
        }
        if (grbit & JET_bitEnumeratePresenceOnly) {
            ec->err = JET_wrnColumnPresent;
            continue;
        }
        BOOL compress = (grbit & JET_bitEnumerateCompressOutput) != 0;
        BOOL multi = (c->grbit & JET_bitColumnMultiValued) != 0;
        ULONG nvals = want ? want : use_default ? 1 : cell->count;
        if (compress && !want && !multi) {
            ULONG size = use_default ? c->defcb : cell->vals[0].cb;
            const void *source = use_default ? c->def : cell->vals[0].data;
            ec->err = use_default ? JET_wrnColumnDefault : JET_wrnColumnSingleValue;
            ec->cbData = size;
            if (max_data && size > max_data) {
                ec->err = JET_wrnColumnTruncated;
                size = max_data;
            }
            ec->pvData = alloc(context, nullptr, size ? size : 1);
            memcpy(ec->pvData, source, size);
            continue;
        }
        ec->cEnumColumnValue = nvals;
        ec->rgEnumColumnValue = alloc(context, nullptr, nvals * sizeof(JET_ENUMCOLUMNVALUE));
        memset(ec->rgEnumColumnValue, 0, nvals * sizeof(JET_ENUMCOLUMNVALUE));
        for (ULONG t = 0; t < nvals; t++) {
            ULONG tag = want ? ids[i].rgtagSequence[t] : t + 1;
            JET_ENUMCOLUMNVALUE *ev = &ec->rgEnumColumnValue[t];
            ev->itagSequence = tag;
            if (use_default) {
                ev->err = JET_wrnColumnDefault;
                ev->cbData = c->defcb;
                ev->pvData = alloc(context, nullptr, c->defcb ? c->defcb : 1);
                memcpy(ev->pvData, c->def, c->defcb);
                continue;
            }
            if (!tag || tag > cell->count) {
                ev->err = JET_wrnColumnNull;
                continue;
            }
            ULONG size = cell->vals[tag - 1].cb;
            ev->cbData = size;
            if (max_data && size > max_data) {
                ev->err = JET_wrnColumnTruncated;
                size = max_data;
            }
            ev->pvData = alloc(context, nullptr, size ? size : 1);
            memcpy(ev->pvData, cell->vals[tag - 1].data, size);
        }
    }
    *out = result;
    *out_count = n;
    ese_free(all);
    return first;
}
