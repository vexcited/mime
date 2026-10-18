/*
 * esent: instances, sessions and attachments of the extensible storage
 * engine, without the engine. a database file that does not exist fails to
 * attach as on windows; one that exists cannot be read, so attaching it
 * reports an invalid database, and everything after an attachment finds no
 * open database or table.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

typedef LONG JET_ERR;
typedef ULONG_PTR JET_INSTANCE, JET_SESID, JET_TABLEID;
typedef ULONG JET_DBID, JET_GRBIT;

#define JET_errSuccess 0
#define JET_errFeatureNotAvailable -1001
#define JET_errInvalidParameter -1003
#define JET_errInvalidDatabaseId -1010
#define JET_errOutOfMemory -1011
#define JET_errInvalidDatabase -1028
#define JET_errNotInitialized -1029
#define JET_errAlreadyInitialized -1030
#define JET_errInstanceNameInUse -1086
#define JET_errInvalidSesid -1104
#define JET_errInvalidInstance -1115
#define JET_errDatabaseNotFound -1203
#define JET_errInvalidTableId -1310
#define JET_errFileNotFound -1811

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

typedef struct INSTANCE {
    struct INSTANCE *next;
    WCHAR name[64];
    BOOL initialized, legacy;
} INSTANCE;

typedef struct SESSION {
    struct SESSION *next;
    INSTANCE *instance;
} SESSION;

static SRWLOCK g_lock = SRWLOCK_INIT;
static INSTANCE *g_instances;
static SESSION *g_sessions;

static INSTANCE *find_instance(JET_INSTANCE handle)
{
    for (INSTANCE *instance = g_instances; instance; instance = instance->next)
        if ((JET_INSTANCE)instance == handle)
            return instance;
    return nullptr;
}

static INSTANCE *legacy_instance(void)
{
    for (INSTANCE *instance = g_instances; instance; instance = instance->next)
        if (instance->legacy)
            return instance;
    return nullptr;
}

static SESSION *find_session(JET_SESID handle)
{
    for (SESSION *session = g_sessions; session; session = session->next)
        if ((JET_SESID)session == handle)
            return session;
    return nullptr;
}

static void to_wide(LPCSTR text, WCHAR *out, int size)
{
    out[0] = 0;
    if (text)
        MultiByteToWideChar(CP_ACP, 0, text, -1, out, size);
}

/* ------------------------------------------------------------------ instances */

static JET_ERR create_instance(JET_INSTANCE *out, LPCWSTR name, BOOL legacy)
{
    if (!out)
        return JET_errInvalidParameter;
    AcquireSRWLockExclusive(&g_lock);
    for (INSTANCE *instance = g_instances; instance && name; instance = instance->next)
        if (!_wcsicmp(instance->name, name)) {
            ReleaseSRWLockExclusive(&g_lock);
            return JET_errInstanceNameInUse;
        }
    INSTANCE *instance = LocalAlloc(LMEM_ZEROINIT, sizeof(*instance));
    if (!instance) {
        ReleaseSRWLockExclusive(&g_lock);
        return JET_errOutOfMemory;
    }
    if (name)
        lstrcpynW(instance->name, name, 64);
    instance->legacy = legacy;
    instance->next = g_instances;
    g_instances = instance;
    ReleaseSRWLockExclusive(&g_lock);
    *out = (JET_INSTANCE)instance;
    return JET_errSuccess;
}

JET_ERR WINAPI JetCreateInstance2W(JET_INSTANCE *out, LPCWSTR name, LPCWSTR display, JET_GRBIT grbit)
{
    return create_instance(out, name, FALSE);
}

JET_ERR WINAPI JetCreateInstanceW(JET_INSTANCE *out, LPCWSTR name)
{
    return create_instance(out, name, FALSE);
}

JET_ERR WINAPI JetCreateInstance2A(JET_INSTANCE *out, LPCSTR name, LPCSTR display, JET_GRBIT grbit)
{
    WCHAR wide[64];
    to_wide(name, wide, 64);
    return create_instance(out, name ? wide : nullptr, FALSE);
}

JET_ERR WINAPI JetCreateInstanceA(JET_INSTANCE *out, LPCSTR name)
{
    return JetCreateInstance2A(out, name, nullptr, 0);
}

/* no instance, or a null one, is the default instance of the single instance mode. */
static JET_ERR init(JET_INSTANCE *handle)
{
    AcquireSRWLockExclusive(&g_lock);
    INSTANCE *instance;
    if (!handle || !*handle) {
        instance = legacy_instance();
        if (!instance) {
            ReleaseSRWLockExclusive(&g_lock);
            JET_INSTANCE created;
            JET_ERR error = create_instance(&created, nullptr, TRUE);
            if (error)
                return error;
            AcquireSRWLockExclusive(&g_lock);
            instance = (INSTANCE *)created;
        }
        if (handle)
            *handle = (JET_INSTANCE)instance;
    } else {
        instance = find_instance(*handle);
        if (!instance) {
            ReleaseSRWLockExclusive(&g_lock);
            return JET_errInvalidInstance;
        }
    }
    JET_ERR error = instance->initialized ? JET_errAlreadyInitialized : JET_errSuccess;
    instance->initialized = TRUE;
    ReleaseSRWLockExclusive(&g_lock);
    return error;
}

JET_ERR WINAPI JetInit(JET_INSTANCE *handle)
{
    return init(handle);
}

JET_ERR WINAPI JetInit2(JET_INSTANCE *handle, JET_GRBIT grbit)
{
    return init(handle);
}

JET_ERR WINAPI JetInit3W(JET_INSTANCE *handle, void *recovery, JET_GRBIT grbit)
{
    return init(handle);
}

JET_ERR WINAPI JetInit3A(JET_INSTANCE *handle, void *recovery, JET_GRBIT grbit)
{
    return init(handle);
}

JET_ERR WINAPI JetInit4W(JET_INSTANCE *handle, void *recovery, JET_GRBIT grbit)
{
    return init(handle);
}

JET_ERR WINAPI JetInit4A(JET_INSTANCE *handle, void *recovery, JET_GRBIT grbit)
{
    return init(handle);
}

static JET_ERR term(JET_INSTANCE handle)
{
    AcquireSRWLockExclusive(&g_lock);
    INSTANCE *instance = handle && handle != (JET_INSTANCE)-1 ? find_instance(handle) : legacy_instance();
    if (!instance) {
        ReleaseSRWLockExclusive(&g_lock);
        return handle && handle != (JET_INSTANCE)-1 ? JET_errInvalidInstance : JET_errNotInitialized;
    }
    for (SESSION **link = &g_sessions; *link;) {
        SESSION *session = *link;
        if (session->instance == instance) {
            *link = session->next;
            LocalFree(session);
        } else {
            link = &session->next;
        }
    }
    for (INSTANCE **link = &g_instances; *link; link = &(*link)->next)
        if (*link == instance) {
            *link = instance->next;
            break;
        }
    LocalFree(instance);
    ReleaseSRWLockExclusive(&g_lock);
    return JET_errSuccess;
}

JET_ERR WINAPI JetTerm(JET_INSTANCE handle)
{
    return term(handle);
}

JET_ERR WINAPI JetTerm2(JET_INSTANCE handle, JET_GRBIT grbit)
{
    return term(handle);
}

/* the parameters are taken and have no engine to configure. */
JET_ERR WINAPI JetSetSystemParameterW(JET_INSTANCE *handle, JET_SESID session, ULONG id, ULONG_PTR value,
                                      LPCWSTR text)
{
    if (handle && *handle && *handle != (JET_INSTANCE)-1) {
        AcquireSRWLockShared(&g_lock);
        INSTANCE *instance = find_instance(*handle);
        ReleaseSRWLockShared(&g_lock);
        if (!instance)
            return JET_errInvalidInstance;
    }
    return JET_errSuccess;
}

JET_ERR WINAPI JetSetSystemParameterA(JET_INSTANCE *handle, JET_SESID session, ULONG id, ULONG_PTR value,
                                      LPCSTR text)
{
    return JetSetSystemParameterW(handle, session, id, value, nullptr);
}

/* ------------------------------------------------------------------ sessions */

static JET_ERR begin_session(JET_INSTANCE handle, JET_SESID *out)
{
    AcquireSRWLockExclusive(&g_lock);
    INSTANCE *instance = handle && handle != (JET_INSTANCE)-1 ? find_instance(handle) : legacy_instance();
    JET_ERR error = JET_errSuccess;
    if (!instance)
        error = handle && handle != (JET_INSTANCE)-1 ? JET_errInvalidInstance : JET_errNotInitialized;
    else if (!instance->initialized)
        error = JET_errNotInitialized;
    else if (!out)
        error = JET_errInvalidParameter;
    SESSION *session = error ? nullptr : LocalAlloc(LMEM_ZEROINIT, sizeof(*session));
    if (!error && !session)
        error = JET_errOutOfMemory;
    if (!error) {
        session->instance = instance;
        session->next = g_sessions;
        g_sessions = session;
        *out = (JET_SESID)session;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return error;
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
    AcquireSRWLockExclusive(&g_lock);
    for (SESSION **link = &g_sessions; *link; link = &(*link)->next)
        if ((JET_SESID)*link == handle) {
            SESSION *session = *link;
            *link = session->next;
            LocalFree(session);
            ReleaseSRWLockExclusive(&g_lock);
            return JET_errSuccess;
        }
    ReleaseSRWLockExclusive(&g_lock);
    return JET_errInvalidSesid;
}

static BOOL valid_session(JET_SESID handle)
{
    AcquireSRWLockShared(&g_lock);
    BOOL valid = find_session(handle) != nullptr;
    ReleaseSRWLockShared(&g_lock);
    return valid;
}

/* ------------------------------------------------------------------ databases and tables */

static JET_ERR attach(JET_SESID session, LPCWSTR path)
{
    if (!valid_session(session))
        return JET_errInvalidSesid;
    if (!path || !*path)
        return JET_errInvalidParameter;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return JET_errFileNotFound;
    return JET_errInvalidDatabase;
}

static JET_ERR attach_ansi(JET_SESID session, LPCSTR path)
{
    WCHAR wide[MAX_PATH];
    to_wide(path, wide, MAX_PATH);
    return attach(session, path ? wide : nullptr);
}

JET_ERR WINAPI JetAttachDatabaseW(JET_SESID session, LPCWSTR path, JET_GRBIT grbit)
{
    return attach(session, path);
}

JET_ERR WINAPI JetAttachDatabaseA(JET_SESID session, LPCSTR path, JET_GRBIT grbit)
{
    return attach_ansi(session, path);
}

JET_ERR WINAPI JetAttachDatabase2W(JET_SESID session, LPCWSTR path, ULONG max_pages, JET_GRBIT grbit)
{
    return attach(session, path);
}

JET_ERR WINAPI JetAttachDatabase2A(JET_SESID session, LPCSTR path, ULONG max_pages, JET_GRBIT grbit)
{
    return attach_ansi(session, path);
}

JET_ERR WINAPI JetAttachDatabase3W(JET_SESID session, LPCWSTR path, void *params, ULONG count, JET_GRBIT grbit)
{
    return attach(session, path);
}

JET_ERR WINAPI JetAttachDatabase3A(JET_SESID session, LPCSTR path, void *params, ULONG count, JET_GRBIT grbit)
{
    return attach_ansi(session, path);
}

/* nothing is ever attached. */
static JET_ERR detach(JET_SESID session)
{
    return valid_session(session) ? JET_errDatabaseNotFound : JET_errInvalidSesid;
}

JET_ERR WINAPI JetDetachDatabaseW(JET_SESID session, LPCWSTR path)
{
    return detach(session);
}

JET_ERR WINAPI JetDetachDatabaseA(JET_SESID session, LPCSTR path)
{
    return detach(session);
}

JET_ERR WINAPI JetDetachDatabase2W(JET_SESID session, LPCWSTR path, JET_GRBIT grbit)
{
    return detach(session);
}

JET_ERR WINAPI JetDetachDatabase2A(JET_SESID session, LPCSTR path, JET_GRBIT grbit)
{
    return detach(session);
}

static JET_ERR open_database(JET_SESID session, JET_DBID *dbid)
{
    if (!valid_session(session))
        return JET_errInvalidSesid;
    if (dbid)
        *dbid = ~0u;
    return JET_errDatabaseNotFound;
}

JET_ERR WINAPI JetOpenDatabaseW(JET_SESID session, LPCWSTR path, LPCWSTR connect, JET_DBID *dbid, JET_GRBIT grbit)
{
    return open_database(session, dbid);
}

JET_ERR WINAPI JetOpenDatabaseA(JET_SESID session, LPCSTR path, LPCSTR connect, JET_DBID *dbid, JET_GRBIT grbit)
{
    return open_database(session, dbid);
}

JET_ERR WINAPI JetCloseDatabase(JET_SESID session, JET_DBID dbid, JET_GRBIT grbit)
{
    return valid_session(session) ? JET_errInvalidDatabaseId : JET_errInvalidSesid;
}

static JET_ERR open_table(JET_SESID session, JET_TABLEID *table)
{
    if (!valid_session(session))
        return JET_errInvalidSesid;
    if (table)
        *table = (JET_TABLEID)-1;
    return JET_errInvalidDatabaseId;
}

JET_ERR WINAPI JetOpenTableW(JET_SESID session, JET_DBID dbid, LPCWSTR name, const void *parameters, ULONG size,
                             JET_GRBIT grbit, JET_TABLEID *table)
{
    return open_table(session, table);
}

JET_ERR WINAPI JetOpenTableA(JET_SESID session, JET_DBID dbid, LPCSTR name, const void *parameters, ULONG size,
                             JET_GRBIT grbit, JET_TABLEID *table)
{
    return open_table(session, table);
}

/* no table is ever open. */
static JET_ERR on_table(JET_SESID session)
{
    return valid_session(session) ? JET_errInvalidTableId : JET_errInvalidSesid;
}

JET_ERR WINAPI JetCloseTable(JET_SESID session, JET_TABLEID table)
{
    return on_table(session);
}

JET_ERR WINAPI JetMove(JET_SESID session, JET_TABLEID table, LONG rows, JET_GRBIT grbit)
{
    return on_table(session);
}

JET_ERR WINAPI JetRetrieveColumn(JET_SESID session, JET_TABLEID table, ULONG column, void *data, ULONG size,
                                 ULONG *actual, JET_GRBIT grbit, void *info)
{
    return on_table(session);
}

JET_ERR WINAPI JetRetrieveColumns(JET_SESID session, JET_TABLEID table, void *columns, ULONG count)
{
    return on_table(session);
}

JET_ERR WINAPI JetGetTableColumnInfoW(JET_SESID session, JET_TABLEID table, LPCWSTR column, void *result,
                                      ULONG size, ULONG level)
{
    return on_table(session);
}

JET_ERR WINAPI JetGetTableColumnInfoA(JET_SESID session, JET_TABLEID table, LPCSTR column, void *result, ULONG size,
                                      ULONG level)
{
    return on_table(session);
}
