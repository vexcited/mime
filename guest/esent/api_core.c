/* the jet api: instances, sessions, system parameters, databases and the definition of tables. */
#include "ese.h"
#include "api.h"

#define LOCK() EnterCriticalSection(&g_ese_lock)
#define UNLOCK() LeaveCriticalSection(&g_ese_lock)

static int g_run_mode;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&g_ese_lock);
        ese_init_params(&g_defaults);
    }
    return TRUE;
}

PARAMS g_defaults;

void api_wide(LPCSTR text, WCHAR *out, int size)
{
    out[0] = 0;
    if (text && !MultiByteToWideChar(CP_ACP, 0, text, -1, out, size))
        out[size - 1] = 0;
}

JET_ERR api_ansi_out(const WCHAR *text, char *out, ULONG cb)
{
    if (!out || !cb)
        return JET_errInvalidParameter;
    int n = WideCharToMultiByte(CP_ACP, 0, text, -1, out, (int)cb, nullptr, nullptr);
    if (!n) {
        out[cb - 1] = 0;
        return JET_wrnBufferTruncated;
    }
    return JET_errSuccess;
}

SESSION *api_session(JET_SESID sesid, JET_ERR *err)
{
    SESSION *sess = ese_session(sesid);
    if (!sess) {
        *err = JET_errInvalidSesid;
        return nullptr;
    }
    if (sess->inst->stopping) {
        *err = JET_errClientRequestToStopJetService;
        return nullptr;
    }
    *err = JET_errSuccess;
    return sess;
}

/* ------------------------------------------------------------------ instances */

static JET_INSTANCE g_failed_init[16];
static ULONG g_failed_next;
static void free_instance(INSTANCE *inst);

static JET_ERR create_instance(JET_INSTANCE *out, LPCWSTR name, LPCWSTR display, BOOL legacy)
{
    if (!out)
        return JET_errInvalidParameter;
    LOCK();
    JET_ERR err = JET_errSuccess;
    ULONG count = 0;
    for (INSTANCE *inst = g_instances; inst; inst = inst->next) {
        count++;
        if (name && *name && !lstrcmpiW(inst->name, name))
            err = JET_errInstanceNameInUse;
    }
    if (!err && g_run_mode == 1 && !legacy)
        err = JET_errRunningInOneInstanceMode;
    if (!err && g_run_mode == 2 && legacy)
        err = JET_errRunningInMultiInstanceMode;
    if (!err && count >= g_defaults.values[JET_paramMaxInstances])
        err = JET_errTooManyInstances;
    if (!err) {
        INSTANCE *inst = ese_alloc(sizeof(*inst));
        if (!inst) {
            err = JET_errOutOfMemory;
        } else {
            if (name)
                lstrcpynW(inst->name, name, 64);
            if (display)
                lstrcpynW(inst->display, display, 64);
            inst->legacy = legacy;
            ese_copy_params(&inst->params, &g_defaults);
            inst->next = g_instances;
            g_instances = inst;
            if (!g_run_mode)
                g_run_mode = legacy ? 1 : 2;
            *out = (JET_INSTANCE)inst;
            for (ULONG i = 0; i < 16; i++)
                if (g_failed_init[i] == (JET_INSTANCE)inst)
                    g_failed_init[i] = 0;
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetCreateInstance2W(JET_INSTANCE *out, LPCWSTR name, LPCWSTR display, JET_GRBIT grbit)
{
    if (!out)
        return JET_errInvalidParameter;
    *out = JET_instanceNil;
    return create_instance(out, name, display, FALSE);
}

JET_ERR WINAPI JetCreateInstanceW(JET_INSTANCE *out, LPCWSTR name)
{
    return JetCreateInstance2W(out, name, nullptr, 0);
}

JET_ERR WINAPI JetCreateInstance2A(JET_INSTANCE *out, LPCSTR name, LPCSTR display, JET_GRBIT grbit)
{
    WCHAR n[64], d[64];
    api_wide(name, n, 64);
    api_wide(display, d, 64);
    return JetCreateInstance2W(out, name ? n : nullptr, display ? d : nullptr, grbit);
}

JET_ERR WINAPI JetCreateInstanceA(JET_INSTANCE *out, LPCSTR name)
{
    return JetCreateInstance2A(out, name, nullptr, 0);
}

static INSTANCE *legacy_instance(void)
{
    for (INSTANCE *inst = g_instances; inst; inst = inst->next)
        if (inst->legacy)
            return inst;
    return nullptr;
}

JET_ERR api_init(JET_INSTANCE *handle, JET_GRBIT grbit)
{
    LOCK();
    INSTANCE *inst;
    JET_ERR err = JET_errSuccess;
    if (!handle || !*handle || *handle == JET_instanceNil) {
        inst = legacy_instance();
        if (!inst) {
            UNLOCK();
            JET_INSTANCE created;
            err = create_instance(&created, nullptr, nullptr, TRUE);
            if (err)
                return err;
            LOCK();
            inst = (INSTANCE *)created;
        }
        if (handle)
            *handle = (JET_INSTANCE)inst;
    } else {
        inst = ese_instance(*handle);
        if (!inst)
            err = JET_errInvalidInstance;
    }
    if (!err && inst->initialized)
        err = JET_errAlreadyInitialized;
    if (!err) {
        WCHAR system[MAX_PATH];
        lstrcpyW(system, ese_text_param(&inst->params, JET_paramSystemPath));
        DWORD attributes = GetFileAttributesW(system);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (inst->params.values[JET_paramCreatePathIfNotExist])
                CreateDirectoryW(system, nullptr);
            else
                err = JET_errInvalidPath;
        }
    }
    if (err && inst && !inst->initialized && err != JET_errInvalidInstance && err != JET_errAlreadyInitialized) {
        /* an instance that does not start is gone, and its handle is refused by JetTerm as a parameter. */
        g_failed_init[g_failed_next++ % 16] = (JET_INSTANCE)inst;
        free_instance(inst);
    }
    if (!err) {
        inst->initialized = TRUE;
        inst->recovery = TRUE;
        if (!g_any_initialized)
            g_global_pagesize = (ULONG)g_defaults.values[JET_paramDatabasePageSize];
        g_any_initialized = TRUE;
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetInit(JET_INSTANCE *handle)
{
    return api_init(handle, 0);
}

JET_ERR WINAPI JetInit2(JET_INSTANCE *handle, JET_GRBIT grbit)
{
    return api_init(handle, grbit);
}

JET_ERR WINAPI JetInit3W(JET_INSTANCE *handle, void *recovery, JET_GRBIT grbit)
{
    return api_init(handle, grbit);
}

JET_ERR WINAPI JetInit3A(JET_INSTANCE *handle, void *recovery, JET_GRBIT grbit)
{
    return api_init(handle, grbit);
}

JET_ERR WINAPI JetInit4W(JET_INSTANCE *handle, void *recovery, JET_GRBIT grbit)
{
    return api_init(handle, grbit);
}

JET_ERR WINAPI JetInit4A(JET_INSTANCE *handle, void *recovery, JET_GRBIT grbit)
{
    return api_init(handle, grbit);
}

static void free_instance(INSTANCE *inst)
{
    while (inst->sessions)
        ese_free_session(inst->sessions);
    while (inst->dbs) {
        DB *db = inst->dbs;
        ese_save_db(db, TRUE);
        inst->dbs = db->next;
        ese_free_db(db);
    }
    for (INSTANCE **link = &g_instances; *link; link = &(*link)->next)
        if (*link == inst) {
            *link = inst->next;
            break;
        }
    if (!g_instances) {
        g_run_mode = 0;
        g_any_initialized = FALSE;
    } else {
        BOOL any = FALSE;
        for (INSTANCE *i = g_instances; i; i = i->next)
            any |= i->initialized;
        g_any_initialized = any;
    }
    ese_free_params(&inst->params);
    ese_free(inst);
}

static JET_ERR term(JET_INSTANCE handle, JET_GRBIT grbit)
{
    LOCK();
    INSTANCE *inst = handle && handle != JET_instanceNil ? ese_instance(handle) : legacy_instance();
    JET_ERR err = JET_errSuccess;
    if (!inst) {
        err = handle && handle != JET_instanceNil ? JET_errInvalidInstance : JET_errNotInitialized;
        for (ULONG i = 0; i < 16; i++)
            if (handle && g_failed_init[i] == handle)
                err = JET_errInvalidParameter;
    } else if (!inst->initialized && inst->legacy)
        err = JET_errNotInitialized;
    if (!err)
        free_instance(inst);
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetTerm(JET_INSTANCE handle)
{
    return term(handle, 0);
}

JET_ERR WINAPI JetTerm2(JET_INSTANCE handle, JET_GRBIT grbit)
{
    return term(handle, grbit);
}

JET_ERR WINAPI JetStopServiceInstance(JET_INSTANCE handle)
{
    LOCK();
    INSTANCE *inst = ese_instance(handle);
    if (inst)
        inst->stopping = TRUE;
    UNLOCK();
    return inst ? JET_errSuccess : JET_errInvalidInstance;
}

JET_ERR WINAPI JetStopServiceInstance2(JET_INSTANCE handle, JET_GRBIT grbit)
{
    if (grbit & ~(0x1 | 0x2 | 0x4 | 0x8 | 0x10))
        return JET_errInvalidGrbit;
    return JetStopServiceInstance(handle);
}

JET_ERR WINAPI JetStopService(void)
{
    LOCK();
    for (INSTANCE *inst = g_instances; inst; inst = inst->next)
        inst->stopping = TRUE;
    UNLOCK();
    return JET_errSuccess;
}

JET_ERR WINAPI JetStopBackup(void)
{
    return JET_errNoBackup;
}

JET_ERR WINAPI JetStopBackupInstance(JET_INSTANCE handle)
{
    LOCK();
    INSTANCE *inst = ese_instance(handle);
    JET_ERR err = inst ? JET_errNoBackup : JET_errInvalidInstance;
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetEnableMultiInstanceW(JET_SETSYSPARAM *params, ULONG count, ULONG *set)
{
    if (set)
        *set = 0;
    LOCK();
    if (g_run_mode == 1) {
        UNLOCK();
        return JET_errRunningInOneInstanceMode;
    }
    g_run_mode = 2;
    UNLOCK();
    for (ULONG i = 0; params && i < count; i++) {
        params[i].err = api_set_param(nullptr, 0, params[i].paramid, params[i].lParam, params[i].sz, TRUE);
        if (set && !params[i].err)
            (*set)++;
    }
    return JET_errSuccess;
}

JET_ERR WINAPI JetEnableMultiInstanceA(JET_SETSYSPARAM *params, ULONG count, ULONG *set)
{
    if (set)
        *set = 0;
    LOCK();
    if (g_run_mode == 1) {
        UNLOCK();
        return JET_errRunningInOneInstanceMode;
    }
    g_run_mode = 2;
    UNLOCK();
    for (ULONG i = 0; params && i < count; i++) {
        params[i].err = api_set_param(nullptr, 0, params[i].paramid, params[i].lParam, params[i].sz, FALSE);
        if (set && !params[i].err)
            (*set)++;
    }
    return JET_errSuccess;
}

/* ------------------------------------------------------------------ system parameters */

/* the parameters that change while an instance runs. */
static BOOL dynamic_param(ULONG id)
{
    switch (id) {
    case JET_paramCacheSizeMax:
    case JET_paramCacheSizeMin:
    case JET_paramCheckpointDepthMax:
    case JET_paramStartFlushThreshold:
    case JET_paramStopFlushThreshold:
    case JET_paramLRUKCorrInterval:
    case JET_paramEventLoggingLevel:
    case JET_paramNoInformationEvent:
    case JET_paramDbExtensionSize:
    case JET_paramMaxTransactionSize:
    case JET_paramHungIOThreshold:
    case JET_paramHungIOActions:
    case JET_paramDeleteOldLogs:
    case JET_paramAccessDeniedRetryPeriod:
    case JET_paramOutstandingIOMax:
    case JET_paramCommitDefault:
    case JET_paramIOPriority:
    case JET_paramCachePriority:
        return TRUE;
    default:
        return FALSE;
    }
}

JET_ERR api_set_param(JET_INSTANCE *handle, JET_SESID sesid, ULONG id, JET_API_PTR value, const void *text, BOOL wide)
{
    if (id >= g_param_count || g_param_defaults[id].kind == 2)
        return JET_errInvalidParameter;
    LOCK();
    INSTANCE *inst = nullptr;
    PARAMS *params = &g_defaults;
    JET_ERR err = JET_errSuccess;
    if (handle && *handle && *handle != JET_instanceNil) {
        inst = ese_instance(*handle);
        if (!inst)
            err = JET_errInvalidInstance;
        else
            params = &inst->params;
    } else if (handle && g_run_mode == 1) {
        inst = legacy_instance();
        if (inst)
            params = &inst->params;
    }
    if (!err && inst && inst->initialized && !dynamic_param(id))
        err = JET_errAlreadyInitialized;
    if (!err && id == JET_paramDatabasePageSize) {
        if (g_run_mode)
            err = JET_errAlreadyInitialized;
        else if (value != 2048 && value != 4096 && value != 8192 && value != 16384 && value != 32768)
            err = JET_errInvalidParameter;
        else
            g_defaults.values[id] = value;
    } else if (!err && g_param_defaults[id].kind == 1) {
        WCHAR buffer[MAX_PATH];
        buffer[0] = 0;
        if (text) {
            if (wide)
                lstrcpynW(buffer, text, MAX_PATH);
            else
                api_wide(text, buffer, MAX_PATH);
        }
        if (id == JET_paramRecovery) {
            if (!lstrcmpiW(buffer, L"on"))
                params->values[id] = 1;
            else if (!lstrcmpiW(buffer, L"off"))
                params->values[id] = 0;
            else
                err = JET_errInvalidParameter;
        } else if (id == JET_paramBaseName && lstrlenW(buffer) != 3) {
            err = JET_errInvalidParameter;
        } else if (lstrlenW(buffer) > 246) {
            err = JET_errInvalidParameter;
        } else {
            BOOL path = id == JET_paramSystemPath || id == JET_paramLogFilePath;
            /* a directory is kept as a full path ending in a backslash, and no text is the current directory. */
            WCHAR full[MAX_PATH + 2];
            if (path) {
                DWORD length = GetFullPathNameW(buffer[0] ? buffer : L".", MAX_PATH, full, nullptr);
                if (!length || length >= MAX_PATH)
                    lstrcpyW(full, buffer);
                else if (full[length - 1] != L'\\')
                    full[length] = L'\\', full[length + 1] = 0;
                lstrcpyW(buffer, full);
            }
            ULONG n = (ULONG)lstrlenW(buffer);
            WCHAR *copy = ese_alloc((n + 2) * sizeof(WCHAR));
            lstrcpyW(copy, buffer);
            ese_free(params->texts[id]);
            params->texts[id] = copy;
        }
    } else if (!err) {
        params->values[id] = value;
    }
    if (!err)
        params->set[id] = TRUE;
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetSetSystemParameterW(JET_INSTANCE *handle, JET_SESID sesid, ULONG id, JET_API_PTR value, LPCWSTR text)
{
    return api_set_param(handle, sesid, id, value, text, TRUE);
}

JET_ERR WINAPI JetSetSystemParameterA(JET_INSTANCE *handle, JET_SESID sesid, ULONG id, JET_API_PTR value, LPCSTR text)
{
    return api_set_param(handle, sesid, id, value, text, FALSE);
}

static JET_ERR get_param(JET_INSTANCE handle, JET_SESID sesid, ULONG id, JET_API_PTR *value, void *text, ULONG cb,
                         BOOL wide)
{
    if (id >= g_param_count || g_param_defaults[id].kind == 2)
        return JET_errInvalidParameter;
    LOCK();
    PARAMS *params = &g_defaults;
    JET_ERR err = JET_errSuccess;
    if (handle && handle != JET_instanceNil) {
        INSTANCE *inst = ese_instance(handle);
        if (!inst)
            err = JET_errInvalidInstance;
        else
            params = &inst->params;
    } else if (g_run_mode == 1) {
        INSTANCE *inst = legacy_instance();
        if (inst)
            params = &inst->params;
    }
    if (!err) {
        if (g_param_defaults[id].kind == 1) {
            const WCHAR *s = id == JET_paramRecovery ? (params->values[id] ? L"on" : L"off") : ese_text_param(params, id);
            if (text && cb) {
                if (wide) {
                    if (cb < sizeof(WCHAR))
                        err = JET_errInvalidParameter;
                    else
                        lstrcpynW(text, s, (int)(cb / sizeof(WCHAR)));
                } else {
                    err = api_ansi_out(s, text, cb);
                }
            }
        } else if (value) {
            *value = id == JET_paramDatabasePageSize && !g_run_mode ? g_defaults.values[id] : params->values[id];
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetSystemParameterW(JET_INSTANCE handle, JET_SESID sesid, ULONG id, JET_API_PTR *value, LPWSTR text,
                                      ULONG cb)
{
    return get_param(handle, sesid, id, value, text, cb, TRUE);
}

JET_ERR WINAPI JetGetSystemParameterA(JET_INSTANCE handle, JET_SESID sesid, ULONG id, JET_API_PTR *value, LPSTR text,
                                      ULONG cb)
{
    return get_param(handle, sesid, id, value, text, cb, FALSE);
}

/* ------------------------------------------------------------------ sessions */

static JET_ERR begin_session(JET_INSTANCE handle, JET_SESID *out)
{
    LOCK();
    INSTANCE *inst = handle && handle != JET_instanceNil ? ese_instance(handle) : legacy_instance();
    JET_ERR err = JET_errSuccess;
    if (!inst)
        err = handle && handle != JET_instanceNil ? JET_errInvalidInstance : JET_errNotInitialized;
    else if (!inst->initialized)
        err = JET_errNotInitialized;
    else if (inst->stopping)
        err = JET_errClientRequestToStopJetService;
    else if (!out)
        err = JET_errInvalidParameter;
    else {
        ULONG count = 0;
        for (SESSION *s = inst->sessions; s; s = s->next)
            count++;
        if (count >= inst->params.values[JET_paramMaxSessions] + 65)
            err = JET_errOutOfSessions;
        else {
            SESSION *sess = ese_new_session(inst);
            if (!sess)
                err = JET_errOutOfMemory;
            else
                *out = (JET_SESID)sess;
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetBeginSessionW(JET_INSTANCE handle, JET_SESID *out, LPCWSTR user, LPCWSTR password)
{
    return begin_session(handle, out);
}

JET_ERR WINAPI JetBeginSessionA(JET_INSTANCE handle, JET_SESID *out, LPCSTR user, LPCSTR password)
{
    return begin_session(handle, out);
}

JET_ERR WINAPI JetEndSession(JET_SESID handle, JET_GRBIT grbit)
{
    LOCK();
    SESSION *sess = ese_session(handle);
    JET_ERR err = JET_errSuccess;
    if (!sess)
        err = JET_errInvalidSesid;
    else
        ese_free_session(sess);
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetDupSession(JET_SESID handle, JET_SESID *out)
{
    LOCK();
    SESSION *sess = ese_session(handle);
    JET_ERR err = JET_errSuccess;
    if (!sess)
        err = JET_errInvalidSesid;
    else if (!out)
        err = JET_errInvalidParameter;
    else {
        SESSION *dup = ese_new_session(sess->inst);
        if (!dup)
            err = JET_errOutOfMemory;
        else {
            for (DBH *h = sess->dbs; h; h = h->next) {
                DBH *copy = ese_alloc(sizeof(*copy));
                *copy = *h;
                copy->sess = dup;
                copy->refs = 0;
                copy->next = dup->dbs;
                dup->dbs = copy;
                h->db->refs++;
            }
            *out = (JET_SESID)dup;
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetDupSession2(JET_SESID handle, JET_SESID *out, ULONG_PTR flags)
{
    return JetDupSession(handle, out);
}

JET_ERR WINAPI JetSetSessionContext(JET_SESID handle, JET_API_PTR context)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess) {
        if (sess->context)
            err = JET_errSessionContextAlreadySet;
        else
            sess->context = context;
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetResetSessionContext(JET_SESID handle)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess) {
        if (!sess->context)
            err = JET_errSessionContextNotSetByThisThread;
        else
            sess->context = 0;
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetIdle(JET_SESID handle, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    api_session(handle, &err);
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetVersion(JET_SESID handle, ULONG *version)
{
    JET_ERR err;
    LOCK();
    api_session(handle, &err);
    UNLOCK();
    if (!err && !version)
        return JET_errInvalidParameter;
    if (!err)
        *version = 0x0a00624f;
    return err;
}

JET_ERR WINAPI JetFreeBuffer(char *buffer)
{
    ese_free(buffer);
    return JET_errSuccess;
}

JET_ERR WINAPI JetResetCounter(JET_SESID handle, LONG counter)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess && (counter < 0 || counter >= 8))
        err = JET_errInvalidParameter;
    else if (sess)
        sess->counters[counter] = 0;
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetCounter(JET_SESID handle, LONG counter, LONG *value)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess && (counter < 0 || counter >= 8 || !value))
        err = JET_errInvalidParameter;
    else if (sess)
        *value = sess->counters[counter];
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetThreadStats(JET_THREADSTATS *stats, ULONG cb)
{
    if (!stats || cb < sizeof(*stats))
        return JET_errInvalidParameter;
    memset(stats, 0, sizeof(*stats));
    stats->cbStruct = sizeof(*stats);
    return JET_errSuccess;
}

JET_ERR WINAPI JetBeginTransaction(JET_SESID handle)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess)
        err = ese_begin(sess, FALSE);
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetBeginTransaction2(JET_SESID handle, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess)
        err = ese_begin(sess, (grbit & JET_bitTransactionReadOnly) != 0);
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetBeginTransaction3(JET_SESID handle, ULONG64 trxid, JET_GRBIT grbit)
{
    return JetBeginTransaction2(handle, grbit);
}

JET_ERR WINAPI JetCommitTransaction(JET_SESID handle, JET_GRBIT grbit)
{
    JET_ERR err;
    if (grbit & 0x8000)
        return JET_errInvalidGrbit;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess)
        err = ese_commit(sess, grbit);
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetCommitTransaction2(JET_SESID handle, JET_GRBIT grbit, ULONG wait, JET_COMMIT_ID *id)
{
    return JetCommitTransaction(handle, grbit);
}

JET_ERR WINAPI JetPrepareToCommitTransaction(JET_SESID handle, const void *data, ULONG cb, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess && !sess->level)
        err = JET_errNotInTransaction;
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetRollback(JET_SESID handle, JET_GRBIT grbit)
{
    JET_ERR err;
    if (grbit & ~JET_bitRollbackAll)
        return JET_errInvalidGrbit;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess)
        err = ese_rollback(sess, (grbit & JET_bitRollbackAll) != 0);
    UNLOCK();
    return err;
}

/* ------------------------------------------------------------------ databases */

static JET_ERR create_database(JET_SESID handle, LPCWSTR path, ULONG max_pages, JET_DBID *dbid, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess) {
        if (!dbid)
            err = JET_errInvalidParameter;
        else if (sess->level)
            err = JET_errInTransaction;
        else
            err = ese_create_db(sess, path, 0, grbit, dbid);
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetCreateDatabaseW(JET_SESID s, LPCWSTR path, LPCWSTR connect, JET_DBID *dbid, JET_GRBIT grbit)
{
    return create_database(s, path, 0, dbid, grbit);
}

JET_ERR WINAPI JetCreateDatabaseA(JET_SESID s, LPCSTR path, LPCSTR connect, JET_DBID *dbid, JET_GRBIT grbit)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return create_database(s, path ? w : nullptr, 0, dbid, grbit);
}

JET_ERR WINAPI JetCreateDatabase2W(JET_SESID s, LPCWSTR path, ULONG max_pages, JET_DBID *dbid, JET_GRBIT grbit)
{
    return create_database(s, path, max_pages, dbid, grbit);
}

JET_ERR WINAPI JetCreateDatabase2A(JET_SESID s, LPCSTR path, ULONG max_pages, JET_DBID *dbid, JET_GRBIT grbit)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return create_database(s, path ? w : nullptr, max_pages, dbid, grbit);
}

JET_ERR WINAPI JetCreateDatabase3W(JET_SESID s, LPCWSTR path, JET_SETDBPARAM *params, ULONG count, JET_DBID *dbid,
                                   JET_GRBIT grbit)
{
    return create_database(s, path, 0, dbid, grbit);
}

JET_ERR WINAPI JetCreateDatabase3A(JET_SESID s, LPCSTR path, JET_SETDBPARAM *params, ULONG count, JET_DBID *dbid,
                                   JET_GRBIT grbit)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return create_database(s, path ? w : nullptr, 0, dbid, grbit);
}

static JET_ERR attach_database(JET_SESID handle, LPCWSTR path, ULONG max_pages, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess) {
        if (sess->level)
            err = JET_errInTransaction;
        else
            err = ese_attach_db(sess, path, grbit, max_pages);
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetAttachDatabaseW(JET_SESID s, LPCWSTR path, JET_GRBIT grbit)
{
    return attach_database(s, path, 0, grbit);
}

JET_ERR WINAPI JetAttachDatabaseA(JET_SESID s, LPCSTR path, JET_GRBIT grbit)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return attach_database(s, path ? w : nullptr, 0, grbit);
}

JET_ERR WINAPI JetAttachDatabase2W(JET_SESID s, LPCWSTR path, ULONG max_pages, JET_GRBIT grbit)
{
    return attach_database(s, path, max_pages, grbit);
}

JET_ERR WINAPI JetAttachDatabase2A(JET_SESID s, LPCSTR path, ULONG max_pages, JET_GRBIT grbit)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return attach_database(s, path ? w : nullptr, max_pages, grbit);
}

JET_ERR WINAPI JetAttachDatabase3W(JET_SESID s, LPCWSTR path, JET_SETDBPARAM *params, ULONG count, JET_GRBIT grbit)
{
    return attach_database(s, path, 0, grbit);
}

JET_ERR WINAPI JetAttachDatabase3A(JET_SESID s, LPCSTR path, JET_SETDBPARAM *params, ULONG count, JET_GRBIT grbit)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return attach_database(s, path ? w : nullptr, 0, grbit);
}

JET_ERR WINAPI JetOpenDatabaseW(JET_SESID handle, LPCWSTR path, LPCWSTR connect, JET_DBID *dbid, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess) {
        if (!dbid)
            err = JET_errInvalidParameter;
        else
            err = ese_open_db(sess, path, dbid, grbit);
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetOpenDatabaseA(JET_SESID handle, LPCSTR path, LPCSTR connect, JET_DBID *dbid, JET_GRBIT grbit)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return JetOpenDatabaseW(handle, path ? w : nullptr, nullptr, dbid, grbit);
}

JET_ERR WINAPI JetCloseDatabase(JET_SESID handle, JET_DBID dbid, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess)
        err = ese_close_db(sess, dbid);
    UNLOCK();
    return err;
}

static JET_ERR detach_database(JET_SESID handle, LPCWSTR path, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    SESSION *sess = api_session(handle, &err);
    if (sess) {
        if (sess->level)
            err = JET_errInTransaction;
        else if (!path) {
            /* every database of the instance. */
            while (sess->inst->dbs && !err) {
                err = ese_detach_db(sess, sess->inst->dbs->path, grbit);
            }
        } else {
            err = ese_detach_db(sess, path, grbit);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetDetachDatabaseW(JET_SESID s, LPCWSTR path)
{
    return detach_database(s, path, 0);
}

JET_ERR WINAPI JetDetachDatabaseA(JET_SESID s, LPCSTR path)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return detach_database(s, path ? w : nullptr, 0);
}

JET_ERR WINAPI JetDetachDatabase2W(JET_SESID s, LPCWSTR path, JET_GRBIT grbit)
{
    return detach_database(s, path, grbit);
}

JET_ERR WINAPI JetDetachDatabase2A(JET_SESID s, LPCSTR path, JET_GRBIT grbit)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return detach_database(s, path ? w : nullptr, grbit);
}

/* ------------------------------------------------------------------ tables */

static JET_ERR with_database(JET_SESID handle, JET_DBID dbid, SESSION **sess, DBH **dbh)
{
    JET_ERR err;
    *sess = api_session(handle, &err);
    if (!*sess)
        return err;
    *dbh = ese_dbh(*sess, dbid);
    return *dbh ? JET_errSuccess : JET_errInvalidDatabaseId;
}

static JET_ERR create_table(JET_SESID handle, JET_DBID dbid, LPCWSTR name, ULONG pages, ULONG density,
                            JET_TABLEID *out)
{
    SESSION *sess;
    DBH *dbh;
    LOCK();
    JET_ERR err = with_database(handle, dbid, &sess, &dbh);
    if (!err && (!out || !name)) {
        err = JET_errInvalidParameter;
    }
    if (!err && density && (density < 20 || density > 100))
        err = JET_errDensityInvalid;
    if (!err) {
        TABLE *table;
        BOOL implicit = !sess->level;
        if (implicit)
            ese_begin(sess, FALSE);
        err = ese_create_table(sess, dbh, name, pages, density, &table);
        if (!err) {
            CURSOR *cur;
            err = ese_open_cursor(sess, dbh, table, JET_bitTablePermitDDL | JET_bitTableUpdatable, &cur);
            if (!err) {
                cur->ddl = TRUE;
                table->creator = cur;
                *out = (JET_TABLEID)cur;
            }
        }
        if (implicit) {
            if (err)
                ese_rollback(sess, TRUE);
            else
                ese_commit(sess, 0);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetCreateTableW(JET_SESID s, JET_DBID db, LPCWSTR name, ULONG pages, ULONG density, JET_TABLEID *out)
{
    return create_table(s, db, name, pages, density, out);
}

JET_ERR WINAPI JetCreateTableA(JET_SESID s, JET_DBID db, LPCSTR name, ULONG pages, ULONG density, JET_TABLEID *out)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return create_table(s, db, name ? w : nullptr, pages, density, out);
}

static JET_ERR delete_table(JET_SESID handle, JET_DBID dbid, LPCWSTR name)
{
    SESSION *sess;
    DBH *dbh;
    LOCK();
    JET_ERR err = with_database(handle, dbid, &sess, &dbh);
    if (!err) {
        BOOL implicit = !sess->level;
        if (implicit)
            ese_begin(sess, FALSE);
        err = ese_delete_table(sess, dbh, name);
        if (implicit) {
            if (err)
                ese_rollback(sess, TRUE);
            else
                ese_commit(sess, 0);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetDeleteTableW(JET_SESID s, JET_DBID db, LPCWSTR name)
{
    return delete_table(s, db, name);
}

JET_ERR WINAPI JetDeleteTableA(JET_SESID s, JET_DBID db, LPCSTR name)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return delete_table(s, db, name ? w : nullptr);
}

JET_ERR WINAPI JetDeleteTable2W(JET_SESID s, JET_DBID db, LPCWSTR name, JET_GRBIT grbit)
{
    return delete_table(s, db, name);
}

JET_ERR WINAPI JetDeleteTable2A(JET_SESID s, JET_DBID db, LPCSTR name, JET_GRBIT grbit)
{
    return JetDeleteTableA(s, db, name);
}

JET_ERR WINAPI JetDeleteTable2(JET_SESID s, JET_DBID db, LPCSTR name, JET_GRBIT grbit)
{
    return JetDeleteTableA(s, db, name);
}

static JET_ERR rename_table(JET_SESID handle, JET_DBID dbid, LPCWSTR from, LPCWSTR to)
{
    SESSION *sess;
    DBH *dbh;
    LOCK();
    JET_ERR err = with_database(handle, dbid, &sess, &dbh);
    if (!err) {
        BOOL implicit = !sess->level;
        if (implicit)
            ese_begin(sess, FALSE);
        err = ese_rename_table(sess, dbh, from, to);
        if (implicit) {
            if (err)
                ese_rollback(sess, TRUE);
            else
                ese_commit(sess, 0);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetRenameTableW(JET_SESID s, JET_DBID db, LPCWSTR from, LPCWSTR to)
{
    return rename_table(s, db, from, to);
}

JET_ERR WINAPI JetRenameTableA(JET_SESID s, JET_DBID db, LPCSTR from, LPCSTR to)
{
    WCHAR a[JET_cbNameMost + 2], b[JET_cbNameMost + 2];
    api_wide(from, a, JET_cbNameMost + 2);
    api_wide(to, b, JET_cbNameMost + 2);
    return rename_table(s, db, from ? a : nullptr, to ? b : nullptr);
}

JET_ERR WINAPI JetRenameTable(JET_SESID s, JET_DBID db, LPCSTR from, LPCSTR to)
{
    return JetRenameTableA(s, db, from, to);
}

/* a transaction around a definition change, which a program outside a transaction gets by itself. */
JET_ERR api_ddl_begin(SESSION *sess, BOOL *implicit)
{
    *implicit = !sess->level;
    if (*implicit)
        return ese_begin(sess, FALSE);
    return JET_errSuccess;
}

JET_ERR api_ddl_end(SESSION *sess, BOOL implicit, JET_ERR err)
{
    if (implicit) {
        if (err < 0)
            ese_rollback(sess, TRUE);
        else
            ese_commit(sess, 0);
    }
    return err;
}

CURSOR *api_ddl_cursor(JET_SESID handle, JET_TABLEID table, JET_ERR *err)
{
    CURSOR *cur = ese_cursor(handle, table, err);
    if (!cur)
        return nullptr;
    if (cur->sess->inst->stopping) {
        *err = JET_errClientRequestToStopJetService;
        return nullptr;
    }
    return cur;
}

static JET_ERR add_column(JET_SESID handle, JET_TABLEID table, LPCWSTR name, const JET_COLUMNDEF *def, const void *dv,
                          ULONG dvcb, JET_COLUMNID *id)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = api_ddl_cursor(handle, table, &err);
    if (cur) {
        if (cur->table->temp)
            err = JET_errInvalidOperation;
        else if (dvcb && !dv)
            err = JET_errInvalidParameter;
        else {
            BOOL implicit;
            api_ddl_begin(cur->sess, &implicit);
            err = ese_add_column(cur->sess, cur->table, name, def, dv, dvcb, id);
            api_ddl_end(cur->sess, implicit, err);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetAddColumnW(JET_SESID s, JET_TABLEID t, LPCWSTR name, const JET_COLUMNDEF *def, const void *dv,
                             ULONG dvcb, JET_COLUMNID *id)
{
    return add_column(s, t, name, def, dv, dvcb, id);
}

JET_ERR WINAPI JetAddColumnA(JET_SESID s, JET_TABLEID t, LPCSTR name, const JET_COLUMNDEF *def, const void *dv,
                             ULONG dvcb, JET_COLUMNID *id)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return add_column(s, t, name ? w : nullptr, def, dv, dvcb, id);
}

JET_ERR WINAPI JetAddColumn(JET_SESID s, JET_TABLEID t, LPCSTR name, const JET_COLUMNDEF *def, const void *dv,
                            ULONG dvcb, JET_COLUMNID *id)
{
    return JetAddColumnA(s, t, name, def, dv, dvcb, id);
}

static JET_ERR delete_column(JET_SESID handle, JET_TABLEID table, LPCWSTR name, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = api_ddl_cursor(handle, table, &err);
    if (cur) {
        {
            BOOL implicit;
            api_ddl_begin(cur->sess, &implicit);
            err = ese_delete_column(cur->sess, cur->table, name, grbit);
            api_ddl_end(cur->sess, implicit, err);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetDeleteColumnW(JET_SESID s, JET_TABLEID t, LPCWSTR name)
{
    return delete_column(s, t, name, 0);
}

JET_ERR WINAPI JetDeleteColumnA(JET_SESID s, JET_TABLEID t, LPCSTR name)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return delete_column(s, t, name ? w : nullptr, 0);
}

JET_ERR WINAPI JetDeleteColumn(JET_SESID s, JET_TABLEID t, LPCSTR name)
{
    return JetDeleteColumnA(s, t, name);
}

JET_ERR WINAPI JetDeleteColumn2W(JET_SESID s, JET_TABLEID t, LPCWSTR name, JET_GRBIT grbit)
{
    return delete_column(s, t, name, grbit);
}

JET_ERR WINAPI JetDeleteColumn2A(JET_SESID s, JET_TABLEID t, LPCSTR name, JET_GRBIT grbit)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return delete_column(s, t, name ? w : nullptr, grbit);
}

JET_ERR WINAPI JetDeleteColumn2(JET_SESID s, JET_TABLEID t, LPCSTR name, JET_GRBIT grbit)
{
    return JetDeleteColumn2A(s, t, name, grbit);
}

static JET_ERR rename_column(JET_SESID handle, JET_TABLEID table, LPCWSTR from, LPCWSTR to, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = api_ddl_cursor(handle, table, &err);
    if (cur) {
        {
            BOOL implicit;
            api_ddl_begin(cur->sess, &implicit);
            err = ese_rename_column(cur->sess, cur->table, from, to);
            api_ddl_end(cur->sess, implicit, err);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetRenameColumnW(JET_SESID s, JET_TABLEID t, LPCWSTR from, LPCWSTR to, JET_GRBIT grbit)
{
    return rename_column(s, t, from, to, grbit);
}

JET_ERR WINAPI JetRenameColumnA(JET_SESID s, JET_TABLEID t, LPCSTR from, LPCSTR to, JET_GRBIT grbit)
{
    WCHAR a[JET_cbNameMost + 2], b[JET_cbNameMost + 2];
    api_wide(from, a, JET_cbNameMost + 2);
    api_wide(to, b, JET_cbNameMost + 2);
    return rename_column(s, t, from ? a : nullptr, to ? b : nullptr, grbit);
}

JET_ERR WINAPI JetRenameColumn(JET_SESID s, JET_TABLEID t, LPCSTR from, LPCSTR to, JET_GRBIT grbit)
{
    return JetRenameColumnA(s, t, from, to, grbit);
}

static JET_ERR set_default(JET_SESID handle, JET_DBID dbid, LPCWSTR table_name, LPCWSTR column, const void *data,
                           ULONG cb, JET_GRBIT grbit)
{
    SESSION *sess;
    DBH *dbh;
    LOCK();
    JET_ERR err = with_database(handle, dbid, &sess, &dbh);
    if (!err) {
        TABLE *table = table_name ? ese_find_table(dbh->db, table_name) : nullptr;
        COLUMN *col = table && column ? ese_find_column(table, column) : nullptr;
        if (!table)
            err = JET_errObjectNotFound;
        else if (!col)
            err = JET_errColumnNotFound;
        else if (table->refs && FALSE)
            err = JET_errTableInUse;
        else {
            ULONG size;
            if (cb && !data)
                err = JET_errInvalidParameter;
            else if (cb && ese_fixed_size(col->coltyp, &size) && cb != size)
                err = JET_errInvalidBufferSize;
            else if (cb > 0x7fff)
                err = JET_errDefaultValueTooBig;
            else {
                ese_free(col->def);
                col->def = cb ? ese_dup(data, cb) : nullptr;
                col->defcb = cb;
                dbh->db->dirty = TRUE;
            }
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetSetColumnDefaultValueW(JET_SESID s, JET_DBID db, LPCWSTR t, LPCWSTR c, const void *d, ULONG cb,
                                         JET_GRBIT grbit)
{
    return set_default(s, db, t, c, d, cb, grbit);
}

JET_ERR WINAPI JetSetColumnDefaultValueA(JET_SESID s, JET_DBID db, LPCSTR t, LPCSTR c, const void *d, ULONG cb,
                                         JET_GRBIT grbit)
{
    WCHAR a[JET_cbNameMost + 2], b[JET_cbNameMost + 2];
    api_wide(t, a, JET_cbNameMost + 2);
    api_wide(c, b, JET_cbNameMost + 2);
    return set_default(s, db, t ? a : nullptr, c ? b : nullptr, d, cb, grbit);
}

JET_ERR WINAPI JetSetColumnDefaultValue(JET_SESID s, JET_DBID db, LPCSTR t, LPCSTR c, const void *d, ULONG cb,
                                        JET_GRBIT grbit)
{
    return JetSetColumnDefaultValueA(s, db, t, c, d, cb, grbit);
}

/* ------------------------------------------------------------------ indexes */

JET_ERR api_create_index(JET_SESID handle, JET_TABLEID table, LPCWSTR name, JET_GRBIT grbit, const WCHAR *key,
                         ULONG cbkey, ULONG density, ULONG lcid, ULONG map_flags, ULONG varsegmac,
                         const JET_TUPLELIMITS *tuples, const JET_CONDITIONALCOLUMN *cond, ULONG ncond, BOOL cond_wide,
                         ULONG keymost)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = api_ddl_cursor(handle, table, &err);
    if (cur) {
        {
            BOOL implicit;
            api_ddl_begin(cur->sess, &implicit);
            err = ese_create_index(cur->sess, cur->table, name, grbit, key, cbkey, density, lcid, map_flags, varsegmac,
                                   tuples, cond, ncond, cond_wide, keymost);
            api_ddl_end(cur->sess, implicit, err);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetCreateIndexW(JET_SESID s, JET_TABLEID t, LPCWSTR name, JET_GRBIT grbit, const WCHAR *key,
                               ULONG cbkey, ULONG density)
{
    return api_create_index(s, t, name, grbit, key, cbkey, density, 0, 0, 0, nullptr, nullptr, 0, TRUE, 0);
}

JET_ERR WINAPI JetCreateIndexA(JET_SESID s, JET_TABLEID t, LPCSTR name, JET_GRBIT grbit, const char *key, ULONG cbkey,
                               ULONG density)
{
    WCHAR w[JET_cbNameMost + 2], k[1200];
    api_wide(name, w, JET_cbNameMost + 2);
    ULONG chars = 0;
    for (ULONG i = 0; key && i < cbkey && i < 1190;) {
        ULONG n = (ULONG)lstrlenA(key + i);
        MultiByteToWideChar(CP_ACP, 0, key + i, (int)n + 1, k + chars, 1190 - chars);
        chars += n + 1;
        i += n + 1;
    }
    k[chars] = 0;
    return api_create_index(s, t, name ? w : nullptr, grbit, key ? k : nullptr, (chars + 1) * sizeof(WCHAR), density, 0,
                            0, 0, nullptr, nullptr, 0, TRUE, 0);
}

JET_ERR WINAPI JetCreateIndex(JET_SESID s, JET_TABLEID t, LPCSTR name, JET_GRBIT grbit, const char *key, ULONG cbkey,
                              ULONG density)
{
    return JetCreateIndexA(s, t, name, grbit, key, cbkey, density);
}

JET_ERR WINAPI JetDeleteIndexW(JET_SESID handle, JET_TABLEID table, LPCWSTR name)
{
    JET_ERR err;
    LOCK();
    CURSOR *cur = api_ddl_cursor(handle, table, &err);
    if (cur) {
        {
            BOOL implicit;
            api_ddl_begin(cur->sess, &implicit);
            err = ese_delete_index(cur->sess, cur->table, name);
            api_ddl_end(cur->sess, implicit, err);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetDeleteIndexA(JET_SESID s, JET_TABLEID t, LPCSTR name)
{
    WCHAR w[JET_cbNameMost + 2];
    api_wide(name, w, JET_cbNameMost + 2);
    return JetDeleteIndexW(s, t, name ? w : nullptr);
}

JET_ERR WINAPI JetDeleteIndex(JET_SESID s, JET_TABLEID t, LPCSTR name)
{
    return JetDeleteIndexA(s, t, name);
}
