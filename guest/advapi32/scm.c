/*
 * the service control manager api: the functions a program calls to open, create, query, start and control
 * services. on windows they are calls of the rpc interface svcctl to services.exe; here they are the messages of
 * include/mime/scm.h on a connection to the pipe \pipe\svcctl, which services.exe serves. see docs/services.md.
 *
 * an SC_HANDLE is a block of this process that names the handle services.exe keeps for the process; the blocks
 * are listed, so that a handle that is not one is told apart without being touched.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"
#include <mime/win32/winsvc.h>
#include <mime/scm.h>

ADVAPI BOOL WINAPI IsValidSecurityDescriptor(PSECURITY_DESCRIPTOR sd);
ADVAPI DWORD WINAPI GetSecurityDescriptorLength(PSECURITY_DESCRIPTOR sd);
ADVAPI BOOL WINAPI GetSecurityDescriptorControl(PSECURITY_DESCRIPTOR sd, PSECURITY_DESCRIPTOR_CONTROL control,
                                                LPDWORD revision);

#define SVC_ACTIONS_MAX_ 16
#define SVC_NAME_MAX_ 256
#define THREAD_SET_CONTEXT_ 0x0010

#define SC_MAGIC 0x53434F42
#define SCK_MANAGER 1
#define SCK_SERVICE 2
#define SCK_LOCK 3
#define SCK_STATUS 4 /* what RegisterServiceCtrlHandler gives a service */

#ifndef ERROR_INVALID_ADDRESS
#define ERROR_INVALID_ADDRESS 487L
#endif
#ifndef RPC_S_SERVER_UNAVAILABLE
#define RPC_S_SERVER_UNAVAILABLE 1722L
#endif

typedef struct SC_OBJECT {
    struct SC_OBJECT *Next;
    DWORD Magic;
    DWORD Kind;
    UINT64 Id;
    struct SC_OBJECT *Manager; /* of a lock */
} SC_OBJECT;

static SRWLOCK g_objects_lock = SRWLOCK_INIT;
static SC_OBJECT *g_objects;

static SC_OBJECT *object_new(DWORD kind, UINT64 id)
{
    SC_OBJECT *o = AdvAllocZero(sizeof(*o));
    if (!o)
        return nullptr;
    o->Magic = SC_MAGIC;
    o->Kind = kind;
    o->Id = id;
    AcquireSRWLockExclusive(&g_objects_lock);
    o->Next = g_objects;
    g_objects = o;
    ReleaseSRWLockExclusive(&g_objects_lock);
    return o;
}

/* whether the handle is one of this process', of the kind. */
static SC_OBJECT *object_find(SC_HANDLE h, DWORD kind)
{
    SC_OBJECT *found = nullptr;
    AcquireSRWLockShared(&g_objects_lock);
    for (SC_OBJECT *o = g_objects; o; o = o->Next) {
        if (o == (SC_OBJECT *)h) {
            found = o->Kind == kind ? o : nullptr;
            break;
        }
    }
    ReleaseSRWLockShared(&g_objects_lock);
    return found;
}

static SC_OBJECT *object_any(SC_HANDLE h)
{
    SC_OBJECT *found = nullptr;
    AcquireSRWLockShared(&g_objects_lock);
    for (SC_OBJECT *o = g_objects; o; o = o->Next) {
        if (o == (SC_OBJECT *)h) {
            found = o;
            break;
        }
    }
    ReleaseSRWLockShared(&g_objects_lock);
    return found;
}

static void object_free(SC_OBJECT *object)
{
    AcquireSRWLockExclusive(&g_objects_lock);
    for (SC_OBJECT **link = &g_objects; *link; link = &(*link)->Next) {
        if (*link == object) {
            *link = object->Next;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_objects_lock);
    object->Magic = 0;
    AdvFree(object);
}

/* ------------------------------------------------------------------ the connection */

#define POOL_SIZE 4
static SRWLOCK g_pool_lock = SRWLOCK_INIT;
static HANDLE g_pool[POOL_SIZE];
static volatile LONG g_ready;

static HANDLE connect_pipe(DWORD *error)
{
    for (int attempt = 0; attempt < 40; attempt++) {
        HANDLE h = CreateFileW(SCM_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            return h;
        DWORD e = GetLastError();
        if (e == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(SCM_PIPE_NAME, 1000);
            continue;
        }
        *error = e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? RPC_S_SERVER_UNAVAILABLE : e;
        return nullptr;
    }
    *error = RPC_S_SERVER_UNAVAILABLE;
    return nullptr;
}

/* a connection to the manager that is not in use. */
static HANDLE take_connection(DWORD *error)
{
    HANDLE h = nullptr;
    AcquireSRWLockExclusive(&g_pool_lock);
    for (int i = 0; i < POOL_SIZE; i++) {
        if (g_pool[i]) {
            h = g_pool[i];
            g_pool[i] = nullptr;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_pool_lock);
    return h ? h : connect_pipe(error);
}

static void give_connection(HANDLE h)
{
    AcquireSRWLockExclusive(&g_pool_lock);
    for (int i = 0; i < POOL_SIZE; i++) {
        if (!g_pool[i]) {
            g_pool[i] = h;
            h = nullptr;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_pool_lock);
    if (h)
        CloseHandle(h);
}

static DWORD pipe_write(HANDLE h, const void *data, DWORD size)
{
    const BYTE *p = data;
    while (size) {
        DWORD done = 0;
        if (!WriteFile(h, p, size, &done, nullptr))
            return GetLastError();
        p += done;
        size -= done;
    }
    return ERROR_SUCCESS;
}

static DWORD pipe_read(HANDLE h, void *data, DWORD size)
{
    BYTE *p = data;
    while (size) {
        DWORD done = 0;
        if (!ReadFile(h, p, size, &done, nullptr))
            return GetLastError();
        if (!done)
            return ERROR_BROKEN_PIPE;
        p += done;
        size -= done;
    }
    return ERROR_SUCCESS;
}

typedef struct SC_REPLY {
    BYTE *Data;
    SCM_READER R;
    DWORD Error; /* the manager's answer */
    BOOL Body;   /* the answer carries more than its header */
} SC_REPLY;

static void reply_free(SC_REPLY *r)
{
    AdvFree(r->Data);
    r->Data = nullptr;
}

/* one request and its answer. a connection that broke while idle is replaced once. */
static DWORD transact(SCM_WRITER *w, SC_REPLY *reply)
{
    UINT32 size;
    UCHAR *message = ScmWriterFinish(w, &size);
    memset(reply, 0, sizeof(*reply));
    if (!message) {
        AdvFree(w->Data);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    DWORD error = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 2; attempt++) {
        HANDLE pipe = take_connection(&error);
        if (!pipe)
            break;
        SCM_HEADER header;
        error = pipe_write(pipe, message, size);
        if (!error)
            error = pipe_read(pipe, &header, sizeof(header));
        if (!error && (header.Magic != SCM_MAGIC || header.Size < sizeof(header) || header.Size > SCM_MAX_MESSAGE))
            error = ERROR_INVALID_DATA;
        if (!error) {
            BYTE *data = AdvAlloc(header.Size);
            if (!data) {
                error = ERROR_NOT_ENOUGH_MEMORY;
            } else {
                memcpy(data, &header, sizeof(header));
                if (header.Size > sizeof(header))
                    error = pipe_read(pipe, data + sizeof(header), header.Size - (DWORD)sizeof(header));
                if (error) {
                    AdvFree(data);
                } else {
                    reply->Data = data;
                    reply->Error = header.Code;
                    reply->Body = header.Size > sizeof(header);
                    ScmReaderInit(&reply->R, data, header.Size);
                }
            }
        }
        if (!error) {
            give_connection(pipe);
            break;
        }
        CloseHandle(pipe);
        /* a connection that went away is tried again with a new one, other failures are final. */
        if (error != ERROR_BROKEN_PIPE && error != ERROR_PIPE_NOT_CONNECTED && error != ERROR_NO_DATA)
            break;
    }
    AdvFree(w->Data);
    if (error && error != ERROR_NOT_ENOUGH_MEMORY && error != RPC_S_SERVER_UNAVAILABLE)
        error = RPC_S_SERVER_UNAVAILABLE;
    return error;
}

/* the answer of a request as a win32 error. */
static DWORD call(SCM_WRITER *w, SC_REPLY *reply)
{
    DWORD error = transact(w, reply);
    return error ? error : reply->Error;
}

static DWORD call_simple(SCM_WRITER *w)
{
    SC_REPLY reply;
    DWORD error = call(w, &reply);
    reply_free(&reply);
    return error;
}

/* the manager is up when its event is set, the first time anyone asks; windows waits three minutes for it. */
static DWORD wait_ready(void)
{
    if (g_ready)
        return ERROR_SUCCESS;
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, SCM_START_EVENT);
    if (!event) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND)
            return error;
        /* nobody has made it yet: the first client does, so that there is one to wait for. */
        event = CreateEventW(nullptr, TRUE, FALSE, SCM_START_EVENT);
        if (!event) {
            error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS)
                return error;
            event = OpenEventW(SYNCHRONIZE, FALSE, SCM_START_EVENT);
            if (!event)
                return GetLastError();
        }
    }
    DWORD wait = WaitForSingleObject(event, 180000);
    CloseHandle(event);
    if (wait == WAIT_TIMEOUT)
        return ERROR_TIMEOUT;
    g_ready = 1;
    return ERROR_SUCCESS;
}

static BOOL fail(DWORD error)
{
    AdvSetError(error);
    return FALSE;
}

/* ------------------------------------------------------------------ the manager */

ADVAPI SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR machine, LPCWSTR database, DWORD access)
{
    DWORD error = wait_ready();
    if (error) {
        AdvSetError(error);
        return nullptr;
    }
    if (!AdvIsLocalSystem(machine)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return nullptr;
    }
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_OPEN_MANAGER);
    ScmPutString(&w, database);
    ScmPutU32(&w, access);
    SC_REPLY reply;
    error = call(&w, &reply);
    SC_OBJECT *object = nullptr;
    if (!error) {
        UINT64 id = ScmGetU64(&reply.R);
        object = object_new(SCK_MANAGER, id);
        if (!object) {
            SCM_WRITER close;
            ScmWriterInit(&close, SCM_OP_CLOSE_HANDLE);
            ScmPutU64(&close, id);
            call_simple(&close);
            error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    reply_free(&reply);
    if (error)
        AdvSetError(error);
    return object;
}

ADVAPI SC_HANDLE WINAPI OpenSCManagerA(LPCSTR machine, LPCSTR database, DWORD access)
{
    PWSTR m = AdvAnsiToWide(machine), d = AdvAnsiToWide(database);
    SC_HANDLE h = (!machine || m) && (!database || d) ? OpenSCManagerW(m, d, access) : nullptr;
    AdvFree(m);
    AdvFree(d);
    return h;
}

ADVAPI BOOL WINAPI CloseServiceHandle(SC_HANDLE handle)
{
    SC_OBJECT *o = object_any(handle);
    if (!o)
        return fail(ERROR_INVALID_HANDLE);
    if (o->Kind == SCK_LOCK) {
        /* a lock is let go with UnlockServiceDatabase, closing it gives it up too. */
        SCM_WRITER w;
        ScmWriterInit(&w, SCM_OP_UNLOCK_DATABASE);
        call_simple(&w);
        object_free(o);
        return TRUE;
    }
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_CLOSE_HANDLE);
    ScmPutU64(&w, o->Id);
    DWORD error = call_simple(&w);
    object_free(o);
    return error && error != ERROR_INVALID_HANDLE ? fail(error) : TRUE;
}

/* ------------------------------------------------------------------ services */

ADVAPI SC_HANDLE WINAPI OpenServiceW(SC_HANDLE manager, LPCWSTR name, DWORD access)
{
    SC_OBJECT *m = object_find(manager, SCK_MANAGER);
    if (!m) {
        AdvSetError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    if (!name) {
        AdvSetError(ERROR_INVALID_ADDRESS);
        return nullptr;
    }
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_OPEN_SERVICE);
    ScmPutU64(&w, m->Id);
    ScmPutString(&w, name);
    ScmPutU32(&w, access);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    SC_OBJECT *object = nullptr;
    if (!error) {
        UINT64 id = ScmGetU64(&reply.R);
        object = object_new(SCK_SERVICE, id);
        if (!object) {
            SCM_WRITER close;
            ScmWriterInit(&close, SCM_OP_CLOSE_HANDLE);
            ScmPutU64(&close, id);
            call_simple(&close);
            error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    reply_free(&reply);
    if (error)
        AdvSetError(error);
    return object;
}

ADVAPI SC_HANDLE WINAPI OpenServiceA(SC_HANDLE manager, LPCSTR name, DWORD access)
{
    PWSTR n = AdvAnsiToWide(name);
    SC_HANDLE h = name && !n ? nullptr : OpenServiceW(manager, n, access);
    AdvFree(n);
    return h;
}

ADVAPI SC_HANDLE WINAPI CreateServiceW(SC_HANDLE manager, LPCWSTR name, LPCWSTR display, DWORD access, DWORD type,
                                       DWORD start, DWORD error_control, LPCWSTR path, LPCWSTR group, LPDWORD tag,
                                       LPCWSTR dependencies, LPCWSTR account, LPCWSTR password)
{
    SC_OBJECT *m = object_find(manager, SCK_MANAGER);
    if (!m) {
        AdvSetError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    if (!name) {
        AdvSetError(ERROR_INVALID_ADDRESS);
        return nullptr;
    }
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_CREATE_SERVICE);
    ScmPutU64(&w, m->Id);
    ScmPutString(&w, name);
    ScmPutString(&w, display);
    ScmPutU32(&w, access);
    ScmPutU32(&w, type);
    ScmPutU32(&w, start);
    ScmPutU32(&w, error_control);
    ScmPutString(&w, path);
    ScmPutString(&w, group);
    ScmPutU32(&w, tag != nullptr);
    DWORD chars = 0;
    if (dependencies) {
        /* a list of names closed by an empty one; the characters it takes are counted here. */
        while (dependencies[chars])
            chars += (DWORD)wcslen(dependencies + chars) + 1;
        chars++;
    }
    ScmPutChars(&w, dependencies, chars);
    ScmPutString(&w, account);
    ScmPutString(&w, password);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    SC_OBJECT *object = nullptr;
    if (!error) {
        UINT64 id = ScmGetU64(&reply.R);
        DWORD new_tag = ScmGetU32(&reply.R);
        object = object_new(SCK_SERVICE, id);
        if (object) {
            if (tag)
                *tag = new_tag;
        } else {
            error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    reply_free(&reply);
    if (error)
        AdvSetError(error);
    return object;
}

ADVAPI SC_HANDLE WINAPI CreateServiceA(SC_HANDLE manager, LPCSTR name, LPCSTR display, DWORD access, DWORD type,
                                       DWORD start, DWORD error_control, LPCSTR path, LPCSTR group, LPDWORD tag,
                                       LPCSTR dependencies, LPCSTR account, LPCSTR password)
{
    PWSTR n = AdvAnsiToWide(name), d = AdvAnsiToWide(display), p = AdvAnsiToWide(path), g = AdvAnsiToWide(group),
          a = AdvAnsiToWide(account), pw = AdvAnsiToWide(password), deps = nullptr;
    BOOL ok = (!name || n) && (!display || d) && (!path || p) && (!group || g) && (!account || a) && (!password || pw);
    if (ok && dependencies) {
        /* the list is converted name by name, its terminator kept. */
        DWORD total = 0;
        while (dependencies[total])
            total += (DWORD)strlen(dependencies + total) + 1;
        total++;
        deps = AdvAllocZero((total + 1) * sizeof(WCHAR));
        if (!deps) {
            ok = FALSE;
        } else {
            int out = 0;
            for (DWORD at = 0; dependencies[at];) {
                int len = (int)strlen(dependencies + at) + 1;
                out += MultiByteToWideChar(CP_ACP, 0, dependencies + at, len, deps + out, (int)total - out);
                at += (DWORD)len;
            }
        }
    }
    SC_HANDLE h = ok ? CreateServiceW(manager, n, d, access, type, start, error_control, p, g, tag, deps, a, pw) : nullptr;
    AdvFree(n);
    AdvFree(d);
    AdvFree(p);
    AdvFree(g);
    AdvFree(a);
    AdvFree(pw);
    AdvFree(deps);
    return h;
}

ADVAPI BOOL WINAPI DeleteService(SC_HANDLE service)
{
    SC_OBJECT *s = object_find(service, SCK_SERVICE);
    if (!s)
        return fail(ERROR_INVALID_HANDLE);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_DELETE_SERVICE);
    ScmPutU64(&w, s->Id);
    DWORD error = call_simple(&w);
    return error ? fail(error) : TRUE;
}

static DWORD multi_chars(const WCHAR *m)
{
    DWORD chars = 0;
    if (!m)
        return 0;
    while (m[chars])
        chars += (DWORD)wcslen(m + chars) + 1;
    return chars + 1;
}

ADVAPI BOOL WINAPI ChangeServiceConfigW(SC_HANDLE service, DWORD type, DWORD start, DWORD error_control, LPCWSTR path,
                                        LPCWSTR group, LPDWORD tag, LPCWSTR dependencies, LPCWSTR account,
                                        LPCWSTR password, LPCWSTR display)
{
    SC_OBJECT *s = object_find(service, SCK_SERVICE);
    if (!s)
        return fail(ERROR_INVALID_HANDLE);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_CHANGE_CONFIG);
    ScmPutU64(&w, s->Id);
    ScmPutU32(&w, type);
    ScmPutU32(&w, start);
    ScmPutU32(&w, error_control);
    ScmPutString(&w, path);
    ScmPutString(&w, group);
    ScmPutU32(&w, tag != nullptr);
    ScmPutChars(&w, dependencies, multi_chars(dependencies));
    ScmPutString(&w, account);
    ScmPutString(&w, password);
    ScmPutString(&w, display);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (!error && tag)
        *tag = ScmGetU32(&reply.R);
    reply_free(&reply);
    return error ? fail(error) : TRUE;
}

ADVAPI BOOL WINAPI ChangeServiceConfigA(SC_HANDLE service, DWORD type, DWORD start, DWORD error_control, LPCSTR path,
                                        LPCSTR group, LPDWORD tag, LPCSTR dependencies, LPCSTR account,
                                        LPCSTR password, LPCSTR display)
{
    PWSTR p = AdvAnsiToWide(path), g = AdvAnsiToWide(group), a = AdvAnsiToWide(account), pw = AdvAnsiToWide(password),
          d = AdvAnsiToWide(display), deps = nullptr;
    BOOL ok = (!path || p) && (!group || g) && (!account || a) && (!password || pw) && (!display || d);
    if (ok && dependencies) {
        DWORD total = 0;
        while (dependencies[total])
            total += (DWORD)strlen(dependencies + total) + 1;
        total++;
        deps = AdvAllocZero((total + 1) * sizeof(WCHAR));
        if (!deps) {
            ok = FALSE;
        } else {
            int out = 0;
            for (DWORD at = 0; dependencies[at];) {
                int len = (int)strlen(dependencies + at) + 1;
                out += MultiByteToWideChar(CP_ACP, 0, dependencies + at, len, deps + out, (int)total - out);
                at += (DWORD)len;
            }
        }
    }
    BOOL result = ok && ChangeServiceConfigW(service, type, start, error_control, p, g, tag, deps, a, pw, d);
    AdvFree(p);
    AdvFree(g);
    AdvFree(a);
    AdvFree(pw);
    AdvFree(d);
    AdvFree(deps);
    return result;
}

/* ------------------------------------------------------------------ configuration */

ADVAPI BOOL WINAPI ChangeServiceConfig2W(SC_HANDLE service, DWORD level, LPVOID info)
{
    SC_OBJECT *s = object_find(service, SCK_SERVICE);
    if (!s)
        return fail(ERROR_INVALID_HANDLE);
    if (!info)
        return fail(ERROR_INVALID_PARAMETER);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_CHANGE_CONFIG2);
    ScmPutU64(&w, s->Id);
    ScmPutU32(&w, level);
    switch (level) {
    case SERVICE_CONFIG_DESCRIPTION:
        ScmPutString(&w, ((SERVICE_DESCRIPTIONW *)info)->lpDescription);
        break;
    case SERVICE_CONFIG_FAILURE_ACTIONS: {
        SERVICE_FAILURE_ACTIONSW *a = info;
        if (a->cActions && !a->lpsaActions) {
            AdvFree(w.Data);
            return fail(ERROR_INVALID_PARAMETER);
        }
        ScmPutU32(&w, a->dwResetPeriod);
        ScmPutU32(&w, 1);
        ScmPutString(&w, a->lpRebootMsg);
        ScmPutString(&w, a->lpCommand);
        ScmPutU32(&w, a->cActions);
        for (DWORD i = 0; i < a->cActions; i++) {
            ScmPutU32(&w, (UINT32)a->lpsaActions[i].Type);
            ScmPutU32(&w, a->lpsaActions[i].Delay);
        }
        break;
    }
    case SERVICE_CONFIG_DELAYED_AUTO_START_INFO:
        ScmPutU32(&w, ((SERVICE_DELAYED_AUTO_START_INFO *)info)->fDelayedAutostart != 0);
        break;
    case SERVICE_CONFIG_FAILURE_ACTIONS_FLAG:
        ScmPutU32(&w, ((SERVICE_FAILURE_ACTIONS_FLAG *)info)->fFailureActionsOnNonCrashFailures != 0);
        break;
    case SERVICE_CONFIG_SERVICE_SID_INFO:
        ScmPutU32(&w, ((SERVICE_SID_INFO *)info)->dwServiceSidType);
        break;
    case SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO: {
        const WCHAR *p = ((SERVICE_REQUIRED_PRIVILEGES_INFOW *)info)->pmszRequiredPrivileges;
        ScmPutChars(&w, p, multi_chars(p));
        break;
    }
    case SERVICE_CONFIG_PRESHUTDOWN_INFO:
        ScmPutU32(&w, ((SERVICE_PRESHUTDOWN_INFO *)info)->dwPreshutdownTimeout);
        break;
    case SERVICE_CONFIG_LAUNCH_PROTECTED:
        ScmPutU32(&w, ((SERVICE_LAUNCH_PROTECTED_INFO *)info)->dwLaunchProtected);
        break;
    default:
        break;
    }
    DWORD error = call_simple(&w);
    return error ? fail(error) : TRUE;
}

ADVAPI BOOL WINAPI ChangeServiceConfig2A(SC_HANDLE service, DWORD level, LPVOID info)
{
    if (!info)
        return fail(ERROR_INVALID_PARAMETER);
    BOOL result;
    switch (level) {
    case SERVICE_CONFIG_DESCRIPTION: {
        SERVICE_DESCRIPTIONA *a = info;
        PWSTR text = AdvAnsiToWide(a->lpDescription);
        SERVICE_DESCRIPTIONW w = {text};
        result = (!a->lpDescription || text) && ChangeServiceConfig2W(service, level, &w);
        AdvFree(text);
        return result;
    }
    case SERVICE_CONFIG_FAILURE_ACTIONS: {
        SERVICE_FAILURE_ACTIONSA *a = info;
        PWSTR reboot = AdvAnsiToWide(a->lpRebootMsg), command = AdvAnsiToWide(a->lpCommand);
        SERVICE_FAILURE_ACTIONSW w = {a->dwResetPeriod, reboot, command, a->cActions, a->lpsaActions};
        result = (!a->lpRebootMsg || reboot) && (!a->lpCommand || command) && ChangeServiceConfig2W(service, level, &w);
        AdvFree(reboot);
        AdvFree(command);
        return result;
    }
    case SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO: {
        SERVICE_REQUIRED_PRIVILEGES_INFOA *a = info;
        PWSTR list = nullptr;
        if (a->pmszRequiredPrivileges) {
            DWORD total = 0;
            while (a->pmszRequiredPrivileges[total])
                total += (DWORD)strlen(a->pmszRequiredPrivileges + total) + 1;
            total++;
            list = AdvAllocZero((total + 1) * sizeof(WCHAR));
            if (list) {
                int out = 0;
                for (DWORD at = 0; a->pmszRequiredPrivileges[at];) {
                    int len = (int)strlen(a->pmszRequiredPrivileges + at) + 1;
                    out += MultiByteToWideChar(CP_ACP, 0, a->pmszRequiredPrivileges + at, len, list + out, (int)total - out);
                    at += (DWORD)len;
                }
            }
        }
        SERVICE_REQUIRED_PRIVILEGES_INFOW w = {list};
        result = (!a->pmszRequiredPrivileges || list) && ChangeServiceConfig2W(service, level, &w);
        AdvFree(list);
        return result;
    }
    default:
        return ChangeServiceConfig2W(service, level, info);
    }
}

/* a string of the answer, copied to *at inside the caller's buffer, which has been checked to hold it. */
static WCHAR *place(BYTE **at, const WCHAR *chars, UINT32 count)
{
    WCHAR *p = (WCHAR *)*at;
    memcpy(p, chars, count * sizeof(WCHAR));
    p[count] = 0;
    *at += (count + 1) * sizeof(WCHAR);
    return p;
}

ADVAPI BOOL WINAPI QueryServiceConfigW(SC_HANDLE service, LPQUERY_SERVICE_CONFIGW config, DWORD size, LPDWORD needed)
{
    SC_OBJECT *s = object_find(service, SCK_SERVICE);
    if (!s)
        return fail(ERROR_INVALID_HANDLE);
    if (!needed)
        return fail(ERROR_INVALID_ADDRESS);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_QUERY_CONFIG);
    ScmPutU64(&w, s->Id);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (error) {
        reply_free(&reply);
        return fail(error);
    }
    SCM_READER *r = &reply.R;
    DWORD type = ScmGetU32(r), start = ScmGetU32(r), error_control = ScmGetU32(r);
    UINT32 path_n, group_n, deps_n, account_n, display_n;
    const WCHAR *path = ScmGetChars(r, &path_n);
    const WCHAR *group = ScmGetChars(r, &group_n);
    DWORD tag = ScmGetU32(r);
    const WCHAR *deps = ScmGetChars(r, &deps_n);
    const WCHAR *account = ScmGetChars(r, &account_n);
    const WCHAR *display = ScmGetChars(r, &display_n);
    /* the strings follow the structure, each closed; the list of dependencies ends with an empty name. */
    DWORD total = sizeof(QUERY_SERVICE_CONFIGW) + (path_n + 1) * 2 + (group_n + 1) * 2 + (deps ? deps_n * 2 + 2 : 2) +
                  (account ? (account_n + 1) * 2 : 0) + (display_n + 1) * 2;
    BOOL ok = TRUE;
    if (r->Failed) {
        error = ERROR_INVALID_DATA;
        ok = FALSE;
    } else if (!config || size < total) {
        *needed = total;
        error = ERROR_INSUFFICIENT_BUFFER;
        ok = FALSE;
    } else {
        BYTE *at = (BYTE *)(config + 1);
        config->dwServiceType = type;
        config->dwStartType = start;
        config->dwErrorControl = error_control;
        config->lpBinaryPathName = place(&at, path, path_n);
        config->lpLoadOrderGroup = place(&at, group, group_n);
        config->dwTagId = tag;
        if (deps && deps_n) {
            config->lpDependencies = (WCHAR *)at;
            memcpy(at, deps, deps_n * sizeof(WCHAR));
            ((WCHAR *)at)[deps_n] = 0;
            at += (deps_n + 1) * sizeof(WCHAR);
        } else {
            config->lpDependencies = (WCHAR *)at;
            ((WCHAR *)at)[0] = 0;
            at += sizeof(WCHAR);
        }
        config->lpServiceStartName = account ? place(&at, account, account_n) : nullptr;
        config->lpDisplayName = place(&at, display, display_n);
        *needed = total;
    }
    reply_free(&reply);
    return ok ? TRUE : fail(error);
}

ADVAPI BOOL WINAPI QueryServiceConfigA(SC_HANDLE service, LPQUERY_SERVICE_CONFIGA config, DWORD size, LPDWORD needed)
{
    if (!needed)
        return fail(ERROR_INVALID_ADDRESS);
    /* the wide answer is converted string by string into the buffer behind the structure. */
    DWORD wide_size = 0;
    QUERY_SERVICE_CONFIGW *wide = nullptr;
    if (!QueryServiceConfigW(service, nullptr, 0, &wide_size)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return FALSE;
    }
    wide = AdvAlloc(wide_size);
    if (!wide)
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    if (!QueryServiceConfigW(service, wide, wide_size, &wide_size)) {
        AdvFree(wide);
        return FALSE;
    }
    const WCHAR *strings[4] = {wide->lpBinaryPathName, wide->lpLoadOrderGroup, wide->lpServiceStartName,
                               wide->lpDisplayName};
    DWORD lengths[4];
    DWORD total = sizeof(QUERY_SERVICE_CONFIGA);
    for (int i = 0; i < 4; i++) {
        lengths[i] = strings[i] ? (DWORD)WideCharToMultiByte(CP_ACP, 0, strings[i], -1, nullptr, 0, nullptr, nullptr) : 0;
        total += lengths[i];
    }
    DWORD deps_chars = 0;
    if (wide->lpDependencies)
        while (wide->lpDependencies[deps_chars])
            deps_chars += (DWORD)wcslen(wide->lpDependencies + deps_chars) + 1;
    DWORD deps_bytes = deps_chars ? (DWORD)WideCharToMultiByte(CP_ACP, 0, wide->lpDependencies, (int)deps_chars + 1, nullptr, 0, nullptr, nullptr) : 1;
    total += deps_bytes;
    *needed = total;
    if (!config || size < total) {
        AdvFree(wide);
        return fail(ERROR_INSUFFICIENT_BUFFER);
    }
    BYTE *at = (BYTE *)(config + 1);
    config->dwServiceType = wide->dwServiceType;
    config->dwStartType = wide->dwStartType;
    config->dwErrorControl = wide->dwErrorControl;
    config->dwTagId = wide->dwTagId;
    LPSTR *targets[4] = {&config->lpBinaryPathName, &config->lpLoadOrderGroup, &config->lpServiceStartName,
                         &config->lpDisplayName};
    for (int i = 0; i < 4; i++) {
        if (!strings[i]) {
            *targets[i] = nullptr;
            continue;
        }
        *targets[i] = (LPSTR)at;
        WideCharToMultiByte(CP_ACP, 0, strings[i], -1, (LPSTR)at, (int)lengths[i], nullptr, nullptr);
        at += lengths[i];
    }
    config->lpDependencies = (LPSTR)at;
    if (deps_chars)
        WideCharToMultiByte(CP_ACP, 0, wide->lpDependencies, (int)deps_chars + 1, (LPSTR)at, (int)deps_bytes, nullptr, nullptr);
    else
        *at = 0;
    AdvFree(wide);
    return TRUE;
}

ADVAPI BOOL WINAPI QueryServiceConfig2W(SC_HANDLE service, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    SC_OBJECT *s = object_find(service, SCK_SERVICE);
    if (!s)
        return fail(ERROR_INVALID_HANDLE);
    if (!needed)
        return fail(ERROR_INVALID_ADDRESS);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_QUERY_CONFIG2);
    ScmPutU64(&w, s->Id);
    ScmPutU32(&w, level);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (error) {
        reply_free(&reply);
        return fail(error);
    }
    SCM_READER *r = &reply.R;
    DWORD total = 0;
    BOOL ok = TRUE;
    switch (level) {
    case SERVICE_CONFIG_DESCRIPTION: {
        UINT32 n;
        const WCHAR *text = ScmGetChars(r, &n);
        total = sizeof(SERVICE_DESCRIPTIONW) + (text ? (n + 1) * 2 : 0);
        if (!buffer || size < total) {
            error = ERROR_INSUFFICIENT_BUFFER;
            ok = FALSE;
        } else {
            SERVICE_DESCRIPTIONW *d = (SERVICE_DESCRIPTIONW *)buffer;
            BYTE *at = (BYTE *)(d + 1);
            d->lpDescription = text ? place(&at, text, n) : nullptr;
        }
        break;
    }
    case SERVICE_CONFIG_FAILURE_ACTIONS: {
        DWORD reset = ScmGetU32(r);
        UINT32 reboot_n, command_n;
        const WCHAR *reboot = ScmGetChars(r, &reboot_n);
        const WCHAR *command = ScmGetChars(r, &command_n);
        DWORD count = ScmGetU32(r);
        if (count > SVC_ACTIONS_MAX_) {
            error = ERROR_INVALID_DATA;
            ok = FALSE;
            break;
        }
        total = sizeof(SERVICE_FAILURE_ACTIONSW) + count * sizeof(SC_ACTION) + (reboot ? (reboot_n + 1) * 2 : 0) +
                (command ? (command_n + 1) * 2 : 0);
        if (!buffer || size < total) {
            error = ERROR_INSUFFICIENT_BUFFER;
            ok = FALSE;
            break;
        }
        SERVICE_FAILURE_ACTIONSW *a = (SERVICE_FAILURE_ACTIONSW *)buffer;
        SC_ACTION *list = (SC_ACTION *)(a + 1);
        BYTE *at = (BYTE *)(list + count);
        for (DWORD i = 0; i < count; i++) {
            list[i].Type = (SC_ACTION_TYPE)ScmGetU32(r);
            list[i].Delay = ScmGetU32(r);
        }
        a->dwResetPeriod = reset;
        a->cActions = count;
        a->lpsaActions = count ? list : nullptr;
        a->lpRebootMsg = reboot ? place(&at, reboot, reboot_n) : nullptr;
        a->lpCommand = command ? place(&at, command, command_n) : nullptr;
        break;
    }
    case SERVICE_CONFIG_DELAYED_AUTO_START_INFO:
    case SERVICE_CONFIG_FAILURE_ACTIONS_FLAG:
    case SERVICE_CONFIG_SERVICE_SID_INFO:
    case SERVICE_CONFIG_PRESHUTDOWN_INFO:
    case SERVICE_CONFIG_LAUNCH_PROTECTED:
        total = sizeof(DWORD);
        if (!buffer || size < total) {
            error = ERROR_INSUFFICIENT_BUFFER;
            ok = FALSE;
        } else {
            *(DWORD *)buffer = ScmGetU32(r);
        }
        break;
    case SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO: {
        UINT32 n;
        const WCHAR *list = ScmGetChars(r, &n);
        total = sizeof(SERVICE_REQUIRED_PRIVILEGES_INFOW) + (list ? n * 2 : 0);
        if (!buffer || size < total) {
            error = ERROR_INSUFFICIENT_BUFFER;
            ok = FALSE;
        } else {
            SERVICE_REQUIRED_PRIVILEGES_INFOW *p = (SERVICE_REQUIRED_PRIVILEGES_INFOW *)buffer;
            if (list) {
                p->pmszRequiredPrivileges = (WCHAR *)(p + 1);
                memcpy(p + 1, list, n * sizeof(WCHAR));
            } else {
                p->pmszRequiredPrivileges = nullptr;
            }
        }
        break;
    }
    case SERVICE_CONFIG_TRIGGER_INFO:
        total = 16 + 2 * sizeof(PVOID);
        if (!buffer || size < total) {
            error = ERROR_INSUFFICIENT_BUFFER;
            ok = FALSE;
        } else {
            memset(buffer, 0, total);
        }
        break;
    default:
        error = ERROR_INVALID_LEVEL;
        ok = FALSE;
        break;
    }
    if (r->Failed && ok) {
        error = ERROR_INVALID_DATA;
        ok = FALSE;
    }
    if (ok || error == ERROR_INSUFFICIENT_BUFFER)
        *needed = total;
    reply_free(&reply);
    return ok ? TRUE : fail(error);
}

ADVAPI BOOL WINAPI QueryServiceConfig2A(SC_HANDLE service, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    if (!needed)
        return fail(ERROR_INVALID_ADDRESS);
    if (level != SERVICE_CONFIG_DESCRIPTION && level != SERVICE_CONFIG_FAILURE_ACTIONS &&
        level != SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO)
        return QueryServiceConfig2W(service, level, buffer, size, needed);
    DWORD wide_size = 0;
    if (!QueryServiceConfig2W(service, level, nullptr, 0, &wide_size) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;
    BYTE *wide = AdvAlloc(wide_size);
    if (!wide)
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    if (!QueryServiceConfig2W(service, level, wide, wide_size, &wide_size)) {
        AdvFree(wide);
        return FALSE;
    }
    DWORD total, extra = 0;
    const WCHAR *a = nullptr, *b = nullptr, *c = nullptr;
    DWORD list_chars = 0;
    if (level == SERVICE_CONFIG_DESCRIPTION) {
        a = ((SERVICE_DESCRIPTIONW *)wide)->lpDescription;
        total = sizeof(SERVICE_DESCRIPTIONA);
    } else if (level == SERVICE_CONFIG_FAILURE_ACTIONS) {
        SERVICE_FAILURE_ACTIONSW *f = (SERVICE_FAILURE_ACTIONSW *)wide;
        a = f->lpRebootMsg;
        b = f->lpCommand;
        total = sizeof(SERVICE_FAILURE_ACTIONSA) + f->cActions * sizeof(SC_ACTION);
    } else {
        c = ((SERVICE_REQUIRED_PRIVILEGES_INFOW *)wide)->pmszRequiredPrivileges;
        total = sizeof(SERVICE_REQUIRED_PRIVILEGES_INFOA);
        if (c)
            while (c[list_chars])
                list_chars += (DWORD)wcslen(c + list_chars) + 1;
    }
    DWORD an = a ? (DWORD)WideCharToMultiByte(CP_ACP, 0, a, -1, nullptr, 0, nullptr, nullptr) : 0;
    DWORD bn = b ? (DWORD)WideCharToMultiByte(CP_ACP, 0, b, -1, nullptr, 0, nullptr, nullptr) : 0;
    DWORD cn = c ? (DWORD)WideCharToMultiByte(CP_ACP, 0, c, (int)list_chars + 1, nullptr, 0, nullptr, nullptr) : 0;
    (void)extra;
    total += an + bn + cn;
    *needed = total;
    if (!buffer || size < total) {
        AdvFree(wide);
        return fail(ERROR_INSUFFICIENT_BUFFER);
    }
    if (level == SERVICE_CONFIG_DESCRIPTION) {
        SERVICE_DESCRIPTIONA *d = (SERVICE_DESCRIPTIONA *)buffer;
        BYTE *at = (BYTE *)(d + 1);
        d->lpDescription = a ? (LPSTR)at : nullptr;
        if (a)
            WideCharToMultiByte(CP_ACP, 0, a, -1, (LPSTR)at, (int)an, nullptr, nullptr);
    } else if (level == SERVICE_CONFIG_FAILURE_ACTIONS) {
        SERVICE_FAILURE_ACTIONSW *f = (SERVICE_FAILURE_ACTIONSW *)wide;
        SERVICE_FAILURE_ACTIONSA *d = (SERVICE_FAILURE_ACTIONSA *)buffer;
        SC_ACTION *list = (SC_ACTION *)(d + 1);
        BYTE *at = (BYTE *)(list + f->cActions);
        for (DWORD i = 0; i < f->cActions; i++)
            list[i] = f->lpsaActions[i];
        d->dwResetPeriod = f->dwResetPeriod;
        d->cActions = f->cActions;
        d->lpsaActions = f->cActions ? list : nullptr;
        d->lpRebootMsg = a ? (LPSTR)at : nullptr;
        if (a) {
            WideCharToMultiByte(CP_ACP, 0, a, -1, (LPSTR)at, (int)an, nullptr, nullptr);
            at += an;
        }
        d->lpCommand = b ? (LPSTR)at : nullptr;
        if (b)
            WideCharToMultiByte(CP_ACP, 0, b, -1, (LPSTR)at, (int)bn, nullptr, nullptr);
    } else {
        SERVICE_REQUIRED_PRIVILEGES_INFOA *d = (SERVICE_REQUIRED_PRIVILEGES_INFOA *)buffer;
        BYTE *at = (BYTE *)(d + 1);
        d->pmszRequiredPrivileges = c ? (LPSTR)at : nullptr;
        if (c)
            WideCharToMultiByte(CP_ACP, 0, c, (int)list_chars + 1, (LPSTR)at, (int)cn, nullptr, nullptr);
    }
    AdvFree(wide);
    return TRUE;
}

/* ------------------------------------------------------------------ status */

static BOOL query_status(SC_HANDLE service, SERVICE_STATUS_PROCESS *out)
{
    SC_OBJECT *s = object_find(service, SCK_SERVICE);
    if (!s)
        return fail(ERROR_INVALID_HANDLE);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_QUERY_STATUS);
    ScmPutU64(&w, s->Id);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (!error) {
        DWORD *f = (DWORD *)out;
        for (int i = 0; i < 9; i++)
            f[i] = ScmGetU32(&reply.R);
        if (reply.R.Failed)
            error = ERROR_INVALID_DATA;
    }
    reply_free(&reply);
    return error ? fail(error) : TRUE;
}

ADVAPI BOOL WINAPI QueryServiceStatus(SC_HANDLE service, LPSERVICE_STATUS status)
{
    if (!status)
        return fail(object_find(service, SCK_SERVICE) ? ERROR_INVALID_ADDRESS : ERROR_INVALID_HANDLE);
    SERVICE_STATUS_PROCESS p;
    if (!query_status(service, &p))
        return FALSE;
    memcpy(status, &p, sizeof(*status));
    return TRUE;
}

ADVAPI BOOL WINAPI QueryServiceStatusEx(SC_HANDLE service, SC_STATUS_TYPE level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    if (level != SC_STATUS_PROCESS_INFO)
        return fail(ERROR_INVALID_LEVEL);
    if (!needed)
        return fail(ERROR_INVALID_ADDRESS);
    if (size < sizeof(SERVICE_STATUS_PROCESS)) {
        *needed = sizeof(SERVICE_STATUS_PROCESS);
        return fail(ERROR_INSUFFICIENT_BUFFER);
    }
    SERVICE_STATUS_PROCESS p;
    if (!query_status(service, &p))
        return FALSE;
    memcpy(buffer, &p, sizeof(p));
    return TRUE;
}

/* ------------------------------------------------------------------ enumerations */

typedef struct ENUM_ITEM {
    const WCHAR *Name, *Display;
    UINT32 NameChars, DisplayChars;
    DWORD Status[9];
} ENUM_ITEM;

/*
 * lists services the way both families of calls do: the structures first and the strings behind them, as many as
 * the buffer takes, ERROR_MORE_DATA and the bytes the rest needs when it takes less.
 */
static BOOL enum_fill(ENUM_ITEM *items, DWORD count, BOOL process, BYTE *buffer, DWORD size, LPDWORD needed,
                      LPDWORD returned, LPDWORD resume, DWORD start_index)
{
    DWORD item_size = process ? sizeof(ENUM_SERVICE_STATUS_PROCESSW) : sizeof(ENUM_SERVICE_STATUSW);
    DWORD used = 0, fit = 0, remaining = 0;
    for (DWORD i = 0; i < count; i++) {
        DWORD bytes = item_size + (items[i].NameChars + 1) * 2 + (items[i].DisplayChars + 1) * 2;
        remaining += bytes;
        if (buffer && used + bytes <= size && fit == i) {
            used += bytes;
            fit++;
        }
    }
    if (fit) {
        BYTE *strings = buffer + (SIZE_T)fit * item_size;
        for (DWORD i = 0; i < fit; i++) {
            BYTE *entry = buffer + (SIZE_T)i * item_size;
            WCHAR *name = place(&strings, items[i].Name, items[i].NameChars);
            WCHAR *display = place(&strings, items[i].Display, items[i].DisplayChars);
            if (process) {
                ENUM_SERVICE_STATUS_PROCESSW *e = (ENUM_SERVICE_STATUS_PROCESSW *)entry;
                e->lpServiceName = name;
                e->lpDisplayName = display;
                memcpy(&e->ServiceStatusProcess, items[i].Status, sizeof(SERVICE_STATUS_PROCESS));
            } else {
                ENUM_SERVICE_STATUSW *e = (ENUM_SERVICE_STATUSW *)entry;
                e->lpServiceName = name;
                e->lpDisplayName = display;
                memcpy(&e->ServiceStatus, items[i].Status, sizeof(SERVICE_STATUS));
            }
        }
    }
    *returned = fit;
    if (fit == count) {
        *needed = 0;
        if (resume)
            *resume = 0;
        return TRUE;
    }
    DWORD rest = 0;
    for (DWORD i = fit; i < count; i++)
        rest += item_size + (items[i].NameChars + 1) * 2 + (items[i].DisplayChars + 1) * 2;
    (void)remaining;
    *needed = rest;
    if (resume)
        *resume = start_index + fit;
    return fail(ERROR_MORE_DATA);
}

static BOOL enum_services(SC_HANDLE manager, BOOL process, DWORD type, DWORD state, BYTE *buffer, DWORD size, LPDWORD needed,
                          LPDWORD returned, LPDWORD resume, LPCWSTR group)
{
    SC_OBJECT *m = object_find(manager, SCK_MANAGER);
    if (!m)
        return fail(ERROR_INVALID_HANDLE);
    if (!needed || !returned)
        return fail(ERROR_INVALID_ADDRESS);
    DWORD first = resume ? *resume : 0;
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_ENUM_SERVICES);
    ScmPutU64(&w, m->Id);
    ScmPutU32(&w, type);
    ScmPutU32(&w, state);
    ScmPutU32(&w, first);
    ScmPutString(&w, group);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (error) {
        reply_free(&reply);
        *returned = 0;
        return fail(error);
    }
    SCM_READER *r = &reply.R;
    DWORD count = ScmGetU32(r);
    ENUM_ITEM *items = count && count < 100000 ? AdvAllocZero(count * sizeof(*items)) : nullptr;
    if (count && !items) {
        reply_free(&reply);
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    }
    for (DWORD i = 0; i < count && !r->Failed; i++) {
        items[i].Name = ScmGetChars(r, &items[i].NameChars);
        items[i].Display = ScmGetChars(r, &items[i].DisplayChars);
        for (int k = 0; k < 9; k++)
            items[i].Status[k] = ScmGetU32(r);
    }
    BOOL ok;
    if (r->Failed) {
        ok = fail(ERROR_INVALID_DATA);
    } else {
        ok = enum_fill(items, count, process, buffer, size, needed, returned, resume, first);
    }
    AdvFree(items);
    reply_free(&reply);
    return ok;
}

ADVAPI BOOL WINAPI EnumServicesStatusExW(SC_HANDLE manager, SC_ENUM_TYPE level, DWORD type, DWORD state, LPBYTE services,
                                         DWORD size, LPDWORD needed, LPDWORD returned, LPDWORD resume, LPCWSTR group)
{
    if (level != SC_ENUM_PROCESS_INFO)
        return fail(ERROR_INVALID_LEVEL);
    return enum_services(manager, TRUE, type, state, services, size, needed, returned, resume, group);
}

ADVAPI BOOL WINAPI EnumServicesStatusW(SC_HANDLE manager, DWORD type, DWORD state, LPENUM_SERVICE_STATUSW services,
                                       DWORD size, LPDWORD needed, LPDWORD returned, LPDWORD resume)
{
    return enum_services(manager, FALSE, type, state, (BYTE *)services, size, needed, returned, resume, nullptr);
}

/* the A forms list the same services and convert what they put in the buffer. */
static BOOL enum_ansi(SC_HANDLE manager, BOOL process, DWORD type, DWORD state, BYTE *buffer, DWORD size, LPDWORD needed,
                      LPDWORD returned, LPDWORD resume, LPCSTR group)
{
    PWSTR wide_group = AdvAnsiToWide(group);
    if (group && !wide_group)
        return FALSE;
    SC_OBJECT *m = object_find(manager, SCK_MANAGER);
    if (!m) {
        AdvFree(wide_group);
        return fail(ERROR_INVALID_HANDLE);
    }
    if (!needed || !returned) {
        AdvFree(wide_group);
        return fail(ERROR_INVALID_ADDRESS);
    }
    DWORD first = resume ? *resume : 0;
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_ENUM_SERVICES);
    ScmPutU64(&w, m->Id);
    ScmPutU32(&w, type);
    ScmPutU32(&w, state);
    ScmPutU32(&w, first);
    ScmPutString(&w, wide_group);
    AdvFree(wide_group);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (error) {
        reply_free(&reply);
        *returned = 0;
        return fail(error);
    }
    SCM_READER *r = &reply.R;
    DWORD count = ScmGetU32(r);
    ENUM_ITEM *items = count && count < 100000 ? AdvAllocZero(count * sizeof(*items)) : nullptr;
    if (count && !items) {
        reply_free(&reply);
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    }
    for (DWORD i = 0; i < count && !r->Failed; i++) {
        items[i].Name = ScmGetChars(r, &items[i].NameChars);
        items[i].Display = ScmGetChars(r, &items[i].DisplayChars);
        for (int k = 0; k < 9; k++)
            items[i].Status[k] = ScmGetU32(r);
    }
    BOOL ok = TRUE;
    if (r->Failed) {
        ok = fail(ERROR_INVALID_DATA);
    } else {
        DWORD item_size = process ? sizeof(ENUM_SERVICE_STATUS_PROCESSA) : sizeof(ENUM_SERVICE_STATUSA);
        DWORD used = 0, fit = 0, rest = 0;
        DWORD *name_bytes = AdvAllocZero((count + 1) * sizeof(DWORD) * 2);
        for (DWORD i = 0; i < count && name_bytes; i++) {
            name_bytes[2 * i] = (DWORD)WideCharToMultiByte(CP_ACP, 0, items[i].Name, (int)items[i].NameChars, nullptr, 0, nullptr, nullptr) + 1;
            name_bytes[2 * i + 1] = (DWORD)WideCharToMultiByte(CP_ACP, 0, items[i].Display, (int)items[i].DisplayChars, nullptr, 0, nullptr, nullptr) + 1;
            DWORD bytes = item_size + name_bytes[2 * i] + name_bytes[2 * i + 1];
            if (buffer && used + bytes <= size && fit == i) {
                used += bytes;
                fit++;
            } else {
                rest += bytes;
            }
        }
        if (!name_bytes) {
            ok = fail(ERROR_NOT_ENOUGH_MEMORY);
        } else {
            BYTE *strings = buffer + (SIZE_T)fit * item_size;
            for (DWORD i = 0; i < fit; i++) {
                BYTE *entry = buffer + (SIZE_T)i * item_size;
                LPSTR name = (LPSTR)strings;
                WideCharToMultiByte(CP_ACP, 0, items[i].Name, (int)items[i].NameChars, name, (int)name_bytes[2 * i], nullptr, nullptr);
                name[name_bytes[2 * i] - 1] = 0;
                strings += name_bytes[2 * i];
                LPSTR display = (LPSTR)strings;
                WideCharToMultiByte(CP_ACP, 0, items[i].Display, (int)items[i].DisplayChars, display, (int)name_bytes[2 * i + 1], nullptr, nullptr);
                display[name_bytes[2 * i + 1] - 1] = 0;
                strings += name_bytes[2 * i + 1];
                if (process) {
                    ENUM_SERVICE_STATUS_PROCESSA *e = (ENUM_SERVICE_STATUS_PROCESSA *)entry;
                    e->lpServiceName = name;
                    e->lpDisplayName = display;
                    memcpy(&e->ServiceStatusProcess, items[i].Status, sizeof(SERVICE_STATUS_PROCESS));
                } else {
                    ENUM_SERVICE_STATUSA *e = (ENUM_SERVICE_STATUSA *)entry;
                    e->lpServiceName = name;
                    e->lpDisplayName = display;
                    memcpy(&e->ServiceStatus, items[i].Status, sizeof(SERVICE_STATUS));
                }
            }
            *returned = fit;
            if (fit == count) {
                *needed = 0;
                if (resume)
                    *resume = 0;
            } else {
                *needed = rest;
                if (resume)
                    *resume = first + fit;
                ok = fail(ERROR_MORE_DATA);
            }
        }
        AdvFree(name_bytes);
    }
    AdvFree(items);
    reply_free(&reply);
    return ok;
}

ADVAPI BOOL WINAPI EnumServicesStatusExA(SC_HANDLE manager, SC_ENUM_TYPE level, DWORD type, DWORD state, LPBYTE services,
                                         DWORD size, LPDWORD needed, LPDWORD returned, LPDWORD resume, LPCSTR group)
{
    if (level != SC_ENUM_PROCESS_INFO)
        return fail(ERROR_INVALID_LEVEL);
    return enum_ansi(manager, TRUE, type, state, services, size, needed, returned, resume, group);
}

ADVAPI BOOL WINAPI EnumServicesStatusA(SC_HANDLE manager, DWORD type, DWORD state, LPENUM_SERVICE_STATUSA services,
                                       DWORD size, LPDWORD needed, LPDWORD returned, LPDWORD resume)
{
    return enum_ansi(manager, FALSE, type, state, (BYTE *)services, size, needed, returned, resume, nullptr);
}

ADVAPI BOOL WINAPI EnumDependentServicesW(SC_HANDLE service, DWORD state, LPENUM_SERVICE_STATUSW services, DWORD size,
                                          LPDWORD needed, LPDWORD returned)
{
    SC_OBJECT *s = object_find(service, SCK_SERVICE);
    if (!s)
        return fail(ERROR_INVALID_HANDLE);
    if (!needed || !returned)
        return fail(ERROR_INVALID_ADDRESS);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_ENUM_DEPENDENTS);
    ScmPutU64(&w, s->Id);
    ScmPutU32(&w, state);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (error) {
        reply_free(&reply);
        *returned = 0;
        return fail(error);
    }
    SCM_READER *r = &reply.R;
    DWORD count = ScmGetU32(r);
    ENUM_ITEM *items = count && count < 100000 ? AdvAllocZero(count * sizeof(*items)) : nullptr;
    if (count && !items) {
        reply_free(&reply);
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    }
    for (DWORD i = 0; i < count && !r->Failed; i++) {
        items[i].Name = ScmGetChars(r, &items[i].NameChars);
        items[i].Display = ScmGetChars(r, &items[i].DisplayChars);
        for (int k = 0; k < 9; k++)
            items[i].Status[k] = ScmGetU32(r);
    }
    BOOL ok;
    if (r->Failed) {
        ok = fail(ERROR_INVALID_DATA);
    } else {
        /* all of them or none: the order is the order to stop them in, which a part of it would not keep. */
        DWORD total = 0;
        for (DWORD i = 0; i < count; i++)
            total += sizeof(ENUM_SERVICE_STATUSW) + (items[i].NameChars + 1) * 2 + (items[i].DisplayChars + 1) * 2;
        if (!services || size < total) {
            *needed = total;
            *returned = 0;
            ok = fail(ERROR_MORE_DATA);
        } else {
            ok = enum_fill(items, count, FALSE, (BYTE *)services, size, needed, returned, nullptr, 0);
        }
    }
    AdvFree(items);
    reply_free(&reply);
    return ok;
}

ADVAPI BOOL WINAPI EnumDependentServicesA(SC_HANDLE service, DWORD state, LPENUM_SERVICE_STATUSA services, DWORD size,
                                          LPDWORD needed, LPDWORD returned)
{
    /* the wide list converts in place through a buffer of its own. */
    DWORD wide_size = 0, count = 0;
    if (!needed || !returned)
        return fail(ERROR_INVALID_ADDRESS);
    if (EnumDependentServicesW(service, state, nullptr, 0, &wide_size, &count) || GetLastError() != ERROR_MORE_DATA) {
        if (GetLastError() == ERROR_INVALID_HANDLE || GetLastError() == ERROR_ACCESS_DENIED || GetLastError() == ERROR_INVALID_ADDRESS)
            return FALSE;
        /* none depend on it. */
        *needed = 0;
        *returned = 0;
        return TRUE;
    }
    ENUM_SERVICE_STATUSW *wide = AdvAlloc(wide_size);
    if (!wide)
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    if (!EnumDependentServicesW(service, state, wide, wide_size, &wide_size, &count)) {
        AdvFree(wide);
        return FALSE;
    }
    DWORD total = 0;
    for (DWORD i = 0; i < count; i++)
        total += sizeof(ENUM_SERVICE_STATUSA) +
                 (DWORD)WideCharToMultiByte(CP_ACP, 0, wide[i].lpServiceName, -1, nullptr, 0, nullptr, nullptr) +
                 (DWORD)WideCharToMultiByte(CP_ACP, 0, wide[i].lpDisplayName, -1, nullptr, 0, nullptr, nullptr);
    if (!services || size < total) {
        *needed = total;
        *returned = 0;
        AdvFree(wide);
        return fail(ERROR_MORE_DATA);
    }
    BYTE *strings = (BYTE *)services + (SIZE_T)count * sizeof(ENUM_SERVICE_STATUSA);
    for (DWORD i = 0; i < count; i++) {
        int n = WideCharToMultiByte(CP_ACP, 0, wide[i].lpServiceName, -1, (LPSTR)strings, 4096, nullptr, nullptr);
        services[i].lpServiceName = (LPSTR)strings;
        strings += n;
        n = WideCharToMultiByte(CP_ACP, 0, wide[i].lpDisplayName, -1, (LPSTR)strings, 4096, nullptr, nullptr);
        services[i].lpDisplayName = (LPSTR)strings;
        strings += n;
        services[i].ServiceStatus = wide[i].ServiceStatus;
    }
    *needed = 0;
    *returned = count;
    AdvFree(wide);
    return TRUE;
}

/* ------------------------------------------------------------------ starting and controlling */

ADVAPI BOOL WINAPI StartServiceW(SC_HANDLE service, DWORD count, LPCWSTR *arguments)
{
    SC_OBJECT *s = object_find(service, SCK_SERVICE);
    if (!s)
        return fail(ERROR_INVALID_HANDLE);
    if (count && !arguments)
        return fail(ERROR_INVALID_PARAMETER);
    for (DWORD i = 0; i < count; i++)
        if (!arguments[i])
            return fail(ERROR_INVALID_PARAMETER);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_START_SERVICE);
    ScmPutU64(&w, s->Id);
    ScmPutU32(&w, count);
    for (DWORD i = 0; i < count; i++)
        ScmPutString(&w, arguments[i]);
    DWORD error = call_simple(&w);
    return error ? fail(error) : TRUE;
}

ADVAPI BOOL WINAPI StartServiceA(SC_HANDLE service, DWORD count, LPCSTR *arguments)
{
    if (count && !arguments)
        return fail(object_find(service, SCK_SERVICE) ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
    PWSTR *wide = count ? AdvAllocZero(count * sizeof(PWSTR)) : nullptr;
    if (count && !wide)
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL ok = TRUE;
    for (DWORD i = 0; i < count && ok; i++) {
        if (!arguments[i]) {
            ok = fail(ERROR_INVALID_PARAMETER);
            break;
        }
        wide[i] = AdvAnsiToWide(arguments[i]);
        ok = wide[i] != nullptr;
    }
    BOOL result = ok && StartServiceW(service, count, (LPCWSTR *)wide);
    for (DWORD i = 0; i < count; i++)
        AdvFree(wide[i]);
    AdvFree(wide);
    return result;
}

static BOOL control(SC_HANDLE service, DWORD control_code, DWORD level, DWORD reason, SERVICE_STATUS_PROCESS *out)
{
    SC_OBJECT *s = object_find(service, SCK_SERVICE);
    if (!s)
        return fail(ERROR_INVALID_HANDLE);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_CONTROL_SERVICE);
    ScmPutU64(&w, s->Id);
    ScmPutU32(&w, control_code);
    ScmPutU32(&w, level);
    if (level)
        ScmPutU32(&w, reason);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (reply.Data && reply.Body && out) {
        DWORD *f = (DWORD *)out;
        for (int i = 0; i < 9; i++)
            f[i] = ScmGetU32(&reply.R);
    }
    reply_free(&reply);
    return error ? fail(error) : TRUE;
}

ADVAPI BOOL WINAPI ControlService(SC_HANDLE service, DWORD code, LPSERVICE_STATUS status)
{
    if (!status)
        return fail(object_find(service, SCK_SERVICE) ? ERROR_INVALID_ADDRESS : ERROR_INVALID_HANDLE);
    SERVICE_STATUS_PROCESS p;
    memset(&p, 0, sizeof(p));
    BOOL ok = control(service, code, 0, 0, &p);
    memcpy(status, &p, sizeof(*status));
    return ok;
}

typedef struct SERVICE_CONTROL_STATUS_REASON_PARAMSW {
    DWORD dwReason;
    LPWSTR pszComment;
    SERVICE_STATUS_PROCESS ServiceStatus;
} SERVICE_CONTROL_STATUS_REASON_PARAMSW;

typedef struct SERVICE_CONTROL_STATUS_REASON_PARAMSA {
    DWORD dwReason;
    LPSTR pszComment;
    SERVICE_STATUS_PROCESS ServiceStatus;
} SERVICE_CONTROL_STATUS_REASON_PARAMSA;

ADVAPI BOOL WINAPI ControlServiceExW(SC_HANDLE service, DWORD code, DWORD level, PVOID info)
{
    if (level != 1)
        return fail(ERROR_INVALID_LEVEL);
    if (!info)
        return fail(ERROR_INVALID_PARAMETER);
    SERVICE_CONTROL_STATUS_REASON_PARAMSW *p = info;
    return control(service, code, level, p->dwReason, &p->ServiceStatus);
}

ADVAPI BOOL WINAPI ControlServiceExA(SC_HANDLE service, DWORD code, DWORD level, PVOID info)
{
    if (level != 1)
        return fail(ERROR_INVALID_LEVEL);
    if (!info)
        return fail(ERROR_INVALID_PARAMETER);
    SERVICE_CONTROL_STATUS_REASON_PARAMSA *p = info;
    return control(service, code, level, p->dwReason, &p->ServiceStatus);
}

/* ------------------------------------------------------------------ security */

ADVAPI BOOL WINAPI QueryServiceObjectSecurity(SC_HANDLE handle, DWORD information, PSECURITY_DESCRIPTOR sd, DWORD size,
                                              LPDWORD needed)
{
    SC_OBJECT *o = object_any(handle);
    if (!o || (o->Kind != SCK_SERVICE && o->Kind != SCK_MANAGER))
        return fail(ERROR_INVALID_HANDLE);
    if (!needed)
        return fail(ERROR_INVALID_ADDRESS);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_QUERY_SECURITY);
    ScmPutU64(&w, o->Id);
    ScmPutU32(&w, information);
    ScmPutU32(&w, sd ? size : 0);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (!error) {
        DWORD n = ScmGetU32(&reply.R);
        UINT32 length;
        const void *bytes = ScmGetBytes(&reply.R, &length);
        if (reply.R.Failed || !bytes || length > size) {
            error = ERROR_INSUFFICIENT_BUFFER;
        } else {
            memcpy(sd, bytes, length);
            *needed = n;
        }
    } else if (error == ERROR_INSUFFICIENT_BUFFER && reply.Body) {
        *needed = ScmGetU32(&reply.R);
    }
    reply_free(&reply);
    return error ? fail(error) : TRUE;
}

ADVAPI BOOL WINAPI SetServiceObjectSecurity(SC_HANDLE handle, DWORD information, PSECURITY_DESCRIPTOR sd)
{
    SC_OBJECT *o = object_any(handle);
    if (!o || (o->Kind != SCK_SERVICE && o->Kind != SCK_MANAGER))
        return fail(ERROR_INVALID_HANDLE);
    if (!sd)
        return fail(ERROR_INVALID_ADDRESS);
    if (!IsValidSecurityDescriptor(sd))
        return FALSE;
    /* the manager takes the descriptor self relative. */
    DWORD length = GetSecurityDescriptorLength(sd);
    PSECURITY_DESCRIPTOR relative = AdvAlloc(length + 64);
    if (!relative)
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    DWORD relative_length = length + 64;
    SECURITY_DESCRIPTOR_CONTROL control_bits = 0;
    DWORD revision;
    GetSecurityDescriptorControl(sd, &control_bits, &revision);
    if (control_bits & 0x8000) {
        memcpy(relative, sd, length);
        relative_length = length;
    } else if (!MakeSelfRelativeSD(sd, relative, &relative_length)) {
        AdvFree(relative);
        return FALSE;
    }
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_SET_SECURITY);
    ScmPutU64(&w, o->Id);
    ScmPutU32(&w, information);
    ScmPutBytes(&w, relative, relative_length);
    AdvFree(relative);
    DWORD error = call_simple(&w);
    return error ? fail(error) : TRUE;
}

/* ------------------------------------------------------------------ the lock of the database */

ADVAPI SC_LOCK WINAPI LockServiceDatabase(SC_HANDLE manager)
{
    SC_OBJECT *m = object_find(manager, SCK_MANAGER);
    if (!m) {
        AdvSetError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_LOCK_DATABASE);
    ScmPutU64(&w, m->Id);
    DWORD error = call_simple(&w);
    if (error) {
        AdvSetError(error);
        return nullptr;
    }
    SC_OBJECT *lock = object_new(SCK_LOCK, 0);
    if (!lock) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    lock->Manager = m;
    return lock;
}

ADVAPI BOOL WINAPI UnlockServiceDatabase(SC_LOCK lock)
{
    SC_OBJECT *l = object_find(lock, SCK_LOCK);
    if (!l)
        return fail(ERROR_INVALID_SERVICE_LOCK);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_UNLOCK_DATABASE);
    DWORD error = call_simple(&w);
    object_free(l);
    return error ? fail(error) : TRUE;
}

ADVAPI BOOL WINAPI QueryServiceLockStatusW(SC_HANDLE manager, LPQUERY_SERVICE_LOCK_STATUSW status, DWORD size, LPDWORD needed)
{
    SC_OBJECT *m = object_find(manager, SCK_MANAGER);
    if (!m)
        return fail(ERROR_INVALID_HANDLE);
    if (!needed)
        return fail(ERROR_INVALID_ADDRESS);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_QUERY_LOCK_STATUS);
    ScmPutU64(&w, m->Id);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (!error) {
        DWORD locked = ScmGetU32(&reply.R);
        UINT32 n;
        const WCHAR *owner = ScmGetChars(&reply.R, &n);
        DWORD duration = ScmGetU32(&reply.R);
        DWORD total = sizeof(QUERY_SERVICE_LOCK_STATUSW) + (n + 1) * 2;
        *needed = total;
        if (!status || size < total) {
            error = ERROR_INSUFFICIENT_BUFFER;
        } else {
            BYTE *at = (BYTE *)(status + 1);
            status->fIsLocked = locked;
            status->lpLockOwner = place(&at, owner ? owner : L"", owner ? n : 0);
            status->dwLockDuration = duration;
        }
    }
    reply_free(&reply);
    return error ? fail(error) : TRUE;
}

ADVAPI BOOL WINAPI QueryServiceLockStatusA(SC_HANDLE manager, LPQUERY_SERVICE_LOCK_STATUSA status, DWORD size, LPDWORD needed)
{
    if (!needed)
        return fail(ERROR_INVALID_ADDRESS);
    BYTE wide[sizeof(QUERY_SERVICE_LOCK_STATUSW) + 1024];
    DWORD wide_needed;
    if (!QueryServiceLockStatusW(manager, (LPQUERY_SERVICE_LOCK_STATUSW)wide, sizeof(wide), &wide_needed))
        return FALSE;
    QUERY_SERVICE_LOCK_STATUSW *w = (QUERY_SERVICE_LOCK_STATUSW *)wide;
    DWORD owner = (DWORD)WideCharToMultiByte(CP_ACP, 0, w->lpLockOwner, -1, nullptr, 0, nullptr, nullptr);
    DWORD total = sizeof(QUERY_SERVICE_LOCK_STATUSA) + owner;
    *needed = total;
    if (!status || size < total)
        return fail(ERROR_INSUFFICIENT_BUFFER);
    status->fIsLocked = w->fIsLocked;
    status->lpLockOwner = (LPSTR)(status + 1);
    WideCharToMultiByte(CP_ACP, 0, w->lpLockOwner, -1, status->lpLockOwner, (int)owner, nullptr, nullptr);
    status->dwLockDuration = w->dwLockDuration;
    return TRUE;
}

/* ------------------------------------------------------------------ names */

static BOOL names(SC_HANDLE manager, DWORD op, LPCWSTR in, LPWSTR out, LPDWORD size)
{
    SC_OBJECT *m = object_find(manager, SCK_MANAGER);
    if (!m)
        return fail(ERROR_INVALID_HANDLE);
    if (!size || !in)
        return fail(ERROR_INVALID_ADDRESS);
    SCM_WRITER w;
    ScmWriterInit(&w, op);
    ScmPutU64(&w, m->Id);
    ScmPutString(&w, in);
    SC_REPLY reply;
    DWORD error = call(&w, &reply);
    if (!error) {
        UINT32 n;
        const WCHAR *name = ScmGetChars(&reply.R, &n);
        if (reply.R.Failed || !name) {
            error = ERROR_INVALID_DATA;
        } else if (!out || *size < n + 1) {
            /* the size counts the characters, and without the terminator when the buffer is too small. */
            *size = n;
            error = ERROR_INSUFFICIENT_BUFFER;
        } else {
            memcpy(out, name, n * sizeof(WCHAR));
            out[n] = 0;
            *size = n;
        }
    }
    reply_free(&reply);
    return error ? fail(error) : TRUE;
}

ADVAPI BOOL WINAPI GetServiceDisplayNameW(SC_HANDLE manager, LPCWSTR name, LPWSTR display, LPDWORD size)
{
    return names(manager, SCM_OP_GET_DISPLAY_NAME, name, display, size);
}

ADVAPI BOOL WINAPI GetServiceKeyNameW(SC_HANDLE manager, LPCWSTR display, LPWSTR name, LPDWORD size)
{
    return names(manager, SCM_OP_GET_KEY_NAME, display, name, size);
}

static BOOL names_ansi(SC_HANDLE manager, DWORD op, LPCSTR in, LPSTR out, LPDWORD size)
{
    if (!size || !in)
        return fail(ERROR_INVALID_ADDRESS);
    PWSTR wide_in = AdvAnsiToWide(in);
    if (!wide_in)
        return FALSE;
    WCHAR wide[SVC_NAME_MAX_ + 2];
    DWORD n = RTL_NUMBER_OF(wide);
    BOOL ok = names(manager, op, wide_in, wide, &n);
    AdvFree(wide_in);
    if (!ok && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;
    if (!ok) {
        *size = n;
        return FALSE;
    }
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide, (int)n, nullptr, 0, nullptr, nullptr);
    if (!out || (int)*size < bytes + 1) {
        *size = (DWORD)bytes;
        return fail(ERROR_INSUFFICIENT_BUFFER);
    }
    WideCharToMultiByte(CP_ACP, 0, wide, (int)n, out, bytes, nullptr, nullptr);
    out[bytes] = 0;
    *size = (DWORD)bytes;
    return TRUE;
}

ADVAPI BOOL WINAPI GetServiceDisplayNameA(SC_HANDLE manager, LPCSTR name, LPSTR display, LPDWORD size)
{
    return names_ansi(manager, SCM_OP_GET_DISPLAY_NAME, name, display, size);
}

ADVAPI BOOL WINAPI GetServiceKeyNameA(SC_HANDLE manager, LPCSTR display, LPSTR name, LPDWORD size)
{
    return names_ansi(manager, SCM_OP_GET_KEY_NAME, display, name, size);
}

ADVAPI BOOL WINAPI NotifyBootConfigStatus(BOOL acceptable)
{
    (void)acceptable;
    DWORD error = wait_ready();
    if (error)
        return fail(error);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_BOOT_CONFIG_STATUS);
    error = call_simple(&w);
    return error ? fail(error) : TRUE;
}

/* ------------------------------------------------------------------ notifications */

typedef struct NOTIFY_REG {
    struct NOTIFY_REG *Next;
    UINT64 Block;
    PSERVICE_NOTIFYW Notify;
    DWORD Version;
    HANDLE Thread;
} NOTIFY_REG;

static SRWLOCK g_notify_lock = SRWLOCK_INIT;
static NOTIFY_REG *g_notifies;
static HANDLE g_notify_pipe;
static UINT64 g_notify_block = 1;

static void WINAPI notify_apc(ULONG_PTR parameter)
{
    PSERVICE_NOTIFYW n = (PSERVICE_NOTIFYW)parameter;
    if (n->pfnNotifyCallback)
        n->pfnNotifyCallback(n);
}

/* reads the completions the manager writes, and has each delivered in the thread that asked. */
static DWORD WINAPI notify_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        SCM_HEADER header;
        if (pipe_read(g_notify_pipe, &header, sizeof(header)))
            break;
        if (header.Magic != SCM_MAGIC || header.Size < sizeof(header) || header.Size > SCM_MAX_MESSAGE)
            break;
        BYTE *data = AdvAlloc(header.Size);
        if (!data)
            break;
        memcpy(data, &header, sizeof(header));
        if (header.Size > sizeof(header) && pipe_read(g_notify_pipe, data + sizeof(header), header.Size - (DWORD)sizeof(header))) {
            AdvFree(data);
            break;
        }
        SCM_READER r;
        ScmReaderInit(&r, data, header.Size);
        UINT64 block = ScmGetU64(&r);
        DWORD status = ScmGetU32(&r), triggered = ScmGetU32(&r);
        DWORD process[9];
        for (int i = 0; i < 9; i++)
            process[i] = ScmGetU32(&r);
        UINT32 names_chars;
        const WCHAR *names = ScmGetChars(&r, &names_chars);
        NOTIFY_REG *reg = nullptr;
        AcquireSRWLockExclusive(&g_notify_lock);
        for (NOTIFY_REG **link = &g_notifies; *link; link = &(*link)->Next) {
            if ((*link)->Block == block) {
                reg = *link;
                *link = reg->Next;
                break;
            }
        }
        ReleaseSRWLockExclusive(&g_notify_lock);
        if (reg && !r.Failed) {
            PSERVICE_NOTIFYW n = reg->Notify;
            n->dwNotificationStatus = status;
            memcpy(&n->ServiceStatus, process, sizeof(process));
            if (reg->Version >= 2) {
                n->dwNotificationTriggered = triggered;
                n->pszServiceNames = nullptr;
                if (names && names_chars) {
                    WCHAR *copy = LocalAlloc(0, names_chars * sizeof(WCHAR));
                    if (copy) {
                        memcpy(copy, names, names_chars * sizeof(WCHAR));
                        n->pszServiceNames = copy;
                    }
                }
            }
            QueueUserAPC(notify_apc, reg->Thread, (ULONG_PTR)n);
        }
        if (reg) {
            CloseHandle(reg->Thread);
            AdvFree(reg);
        }
        AdvFree(data);
    }
    return 0;
}

static DWORD start_notifications(void)
{
    if (g_notify_pipe)
        return ERROR_SUCCESS;
    DWORD error = ERROR_SUCCESS;
    HANDLE pipe = connect_pipe(&error);
    if (!pipe)
        return error;
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_NOTIFY_ATTACH);
    UINT32 size;
    UCHAR *message = ScmWriterFinish(&w, &size);
    SCM_HEADER header = {};
    error = message ? pipe_write(pipe, message, size) : ERROR_NOT_ENOUGH_MEMORY;
    AdvFree(w.Data);
    if (!error)
        error = pipe_read(pipe, &header, sizeof(header));
    if (!error && (header.Magic != SCM_MAGIC || header.Code))
        error = header.Code ? header.Code : ERROR_INVALID_DATA;
    if (error) {
        CloseHandle(pipe);
        return error;
    }
    AcquireSRWLockExclusive(&g_notify_lock);
    if (g_notify_pipe) {
        ReleaseSRWLockExclusive(&g_notify_lock);
        CloseHandle(pipe);
        return ERROR_SUCCESS;
    }
    g_notify_pipe = pipe;
    ReleaseSRWLockExclusive(&g_notify_lock);
    HANDLE thread = CreateThread(nullptr, 0, notify_thread, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    return thread ? ERROR_SUCCESS : GetLastError();
}

static DWORD notify_change(SC_HANDLE handle, DWORD mask, PSERVICE_NOTIFYW notify)
{
    SC_OBJECT *o = object_any(handle);
    if (!handle || !o || (o->Kind != SCK_SERVICE && o->Kind != SCK_MANAGER))
        return ERROR_INVALID_PARAMETER;
    /* a mask of states and one of creations and deletions cannot be mixed, and the second needs version 2. */
    if ((mask & 0x3FF) == 0 || ((mask & 0x27F) != 0 && (mask & 0x180) != 0) || !notify || notify->dwVersion - 1 > 1 ||
        (notify->dwVersion < 2 && (mask & 0x180) != 0) || !notify->pfnNotifyCallback)
        return ERROR_INVALID_PARAMETER;
    DWORD error = start_notifications();
    if (error)
        return error;
    notify->dwNotificationStatus = 0;
    memset(&notify->ServiceStatus, 0, sizeof(notify->ServiceStatus));
    if (notify->dwVersion >= 2) {
        notify->dwNotificationTriggered = 0;
        notify->pszServiceNames = nullptr;
    }
    NOTIFY_REG *reg = AdvAllocZero(sizeof(*reg));
    if (!reg)
        return ERROR_NOT_ENOUGH_MEMORY;
    reg->Notify = notify;
    reg->Version = notify->dwVersion;
    reg->Thread = OpenThread(THREAD_SET_CONTEXT_, FALSE, GetCurrentThreadId());
    if (!reg->Thread) {
        error = GetLastError();
        AdvFree(reg);
        return error;
    }
    AcquireSRWLockExclusive(&g_notify_lock);
    reg->Block = g_notify_block++;
    reg->Next = g_notifies;
    g_notifies = reg;
    ReleaseSRWLockExclusive(&g_notify_lock);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_NOTIFY_STATUS_CHANGE);
    ScmPutU64(&w, o->Id);
    ScmPutU64(&w, reg->Block);
    ScmPutU32(&w, mask);
    ScmPutU32(&w, notify->dwVersion);
    error = call_simple(&w);
    if (error) {
        AcquireSRWLockExclusive(&g_notify_lock);
        BOOL still = FALSE;
        for (NOTIFY_REG **link = &g_notifies; *link; link = &(*link)->Next) {
            if (*link == reg) {
                *link = reg->Next;
                still = TRUE;
                break;
            }
        }
        ReleaseSRWLockExclusive(&g_notify_lock);
        if (still) {
            CloseHandle(reg->Thread);
            AdvFree(reg);
        }
    }
    return error;
}

ADVAPI DWORD WINAPI NotifyServiceStatusChangeW(SC_HANDLE service, DWORD mask, PSERVICE_NOTIFYW notify)
{
    return notify_change(service, mask, notify);
}

ADVAPI DWORD WINAPI NotifyServiceStatusChangeA(SC_HANDLE service, DWORD mask, PSERVICE_NOTIFYA notify)
{
    /* the names of a version 2 notice are wide in this layout, as in windows' own. */
    return notify_change(service, mask, (PSERVICE_NOTIFYW)notify);
}

ADVAPI DWORD WINAPI WaitServiceState(SC_HANDLE service, DWORD notify_mask, DWORD timeout, HANDLE cancel_event)
{
    (void)cancel_event;
    /* polls the state, which is what a wait for a notification amounts to. */
    ULONGLONG deadline = GetTickCount64() + timeout;
    for (;;) {
        SERVICE_STATUS_PROCESS p;
        if (!query_status(service, &p))
            return GetLastError();
        DWORD bit = p.dwCurrentState >= 1 && p.dwCurrentState <= 7 ? 1u << (p.dwCurrentState - 1) : 0;
        if (notify_mask & bit)
            return ERROR_SUCCESS;
        if (timeout != INFINITE && GetTickCount64() >= deadline)
            return WAIT_TIMEOUT;
        Sleep(10);
    }
}

/* ------------------------------------------------------------------ the service side */

typedef struct DISPATCH_ENTRY {
    WCHAR *Name;  /* the table's name */
    WCHAR *Alias; /* the name the manager knows the service by, when it differs */
    LPSERVICE_MAIN_FUNCTIONW MainW;
    LPSERVICE_MAIN_FUNCTIONA MainA;
    LPHANDLER_FUNCTION Handler;
    LPHANDLER_FUNCTION_EX HandlerEx;
    PVOID Context;
    SC_OBJECT *Status; /* the handle the service reports with, set when it is started */
    BOOL Started;
} DISPATCH_ENTRY;

static DISPATCH_ENTRY *g_entries;
static DWORD g_entry_count;
static BOOL g_ansi_table;
static volatile LONG g_dispatcher_started;
static SRWLOCK g_entries_lock = SRWLOCK_INIT;

static void free_entries(void)
{
    AcquireSRWLockExclusive(&g_entries_lock);
    for (DWORD i = 0; i < g_entry_count; i++) {
        AdvFree(g_entries[i].Name);
        AdvFree(g_entries[i].Alias);
    }
    AdvFree(g_entries);
    g_entries = nullptr;
    g_entry_count = 0;
    ReleaseSRWLockExclusive(&g_entries_lock);
}

/* the entry of a service: by the name of the table or the name the manager gave it. */
static DISPATCH_ENTRY *find_entry(const WCHAR *name)
{
    for (DWORD i = 0; i < g_entry_count; i++)
        if (!_wcsicmp(g_entries[i].Name, name) || (g_entries[i].Alias && !_wcsicmp(g_entries[i].Alias, name)))
            return &g_entries[i];
    return nullptr;
}

typedef struct START_PARAMS {
    DISPATCH_ENTRY *Entry;
    DWORD Count;
    WCHAR **Wide;
    CHAR **Narrow;
} START_PARAMS;

static DWORD WINAPI service_main_thread(LPVOID context)
{
    START_PARAMS *p = context;
    if (p->Entry->MainA && g_ansi_table)
        p->Entry->MainA(p->Count, p->Narrow);
    else if (p->Entry->MainW)
        p->Entry->MainW(p->Count, p->Wide);
    for (DWORD i = 0; i < p->Count; i++) {
        AdvFree(p->Wide ? p->Wide[i] : nullptr);
        AdvFree(p->Narrow ? p->Narrow[i] : nullptr);
    }
    AdvFree(p->Wide);
    AdvFree(p->Narrow);
    AdvFree(p);
    return 0;
}

/* the channel: the process says it is a service and the pipe becomes the line the manager's controls come on. */
static HANDLE open_channel(DWORD *error)
{
    HANDLE pipe = connect_pipe(error);
    if (!pipe)
        return nullptr;
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_OPEN_CHANNEL);
    ScmPutU32(&w, GetCurrentProcessId());
    UINT32 size;
    UCHAR *message = ScmWriterFinish(&w, &size);
    SCM_HEADER header = {};
    *error = message ? pipe_write(pipe, message, size) : ERROR_NOT_ENOUGH_MEMORY;
    AdvFree(w.Data);
    if (!*error)
        *error = pipe_read(pipe, &header, sizeof(header));
    if (!*error && header.Magic != SCM_MAGIC)
        *error = ERROR_INVALID_DATA;
    if (!*error && header.Code)
        *error = header.Code;
    if (*error) {
        CloseHandle(pipe);
        return nullptr;
    }
    return pipe;
}

static DWORD answer(HANDLE pipe, DWORD code)
{
    SCM_HEADER h = {SCM_MAGIC, sizeof(h), code, 0};
    return pipe_write(pipe, &h, sizeof(h));
}

static DWORD handler_result(DISPATCH_ENTRY *e, DWORD control, DWORD event_type, void *event_data)
{
    LPHANDLER_FUNCTION_EX ex = e->HandlerEx;
    LPHANDLER_FUNCTION plain = e->Handler;
    if (ex)
        return ex(control, event_type, event_data, e->Context);
    if (!plain)
        return ERROR_SERVICE_CANNOT_ACCEPT_CTRL;
    /* a handler of the old kind knows the controls up to the network ones and the controls of its own. */
    if ((control - 1) > 9 && (control - 128) > 0x7F)
        return ERROR_CALL_NOT_IMPLEMENTED;
    plain(control);
    return ERROR_SUCCESS;
}

static void dispatch_loop(HANDLE pipe)
{
    for (;;) {
        SCM_HEADER header;
        if (pipe_read(pipe, &header, sizeof(header)))
            return;
        if (header.Magic != SCM_MAGIC || header.Size < sizeof(header) || header.Size > SCM_MAX_MESSAGE)
            return;
        BYTE *data = AdvAlloc(header.Size);
        if (!data)
            return;
        memcpy(data, &header, sizeof(header));
        if (header.Size > sizeof(header) && pipe_read(pipe, data + sizeof(header), header.Size - (DWORD)sizeof(header))) {
            AdvFree(data);
            return;
        }
        SCM_READER r;
        ScmReaderInit(&r, data, header.Size);
        UINT32 name_n;
        const WCHAR *name_chars = ScmGetChars(&r, &name_n);
        DWORD event_type = ScmGetU32(&r);
        UINT32 event_size;
        const void *event_data = ScmGetBytes(&r, &event_size);
        UINT64 status_id = ScmGetU64(&r);
        DWORD count = ScmGetU32(&r);
        DWORD control = header.Code;
        WCHAR *name = name_chars ? AdvAlloc((name_n + 1) * sizeof(WCHAR)) : nullptr;
        if (name) {
            memcpy(name, name_chars, name_n * sizeof(WCHAR));
            name[name_n] = 0;
        }
        DWORD result = ERROR_SUCCESS;
        BOOL quit = FALSE;
        if (r.Failed || !name) {
            result = ERROR_INVALID_DATA;
        } else if (control == SCM_CTRL_QUIT && !*name) {
            quit = TRUE;
        } else if (control == SCM_CTRL_START_OWN || control == SCM_CTRL_START_SHARE) {
            AcquireSRWLockExclusive(&g_entries_lock);
            DISPATCH_ENTRY *e = control == SCM_CTRL_START_OWN ? (g_entry_count ? &g_entries[0] : nullptr) : find_entry(name);
            if (!e) {
                result = ERROR_SERVICE_NOT_IN_EXE;
            } else if (e->Started) {
                result = ERROR_SERVICE_ALREADY_RUNNING;
            } else {
                /* an own process service is called by the name the manager gives it, whatever the table says. */
                if (control == SCM_CTRL_START_OWN && _wcsicmp(e->Name, name)) {
                    AdvFree(e->Alias);
                    e->Alias = AdvAlloc((wcslen(name) + 1) * sizeof(WCHAR));
                    if (e->Alias)
                        wcscpy(e->Alias, name);
                }
                START_PARAMS *p = AdvAllocZero(sizeof(*p));
                SC_OBJECT *status = object_new(SCK_STATUS, status_id);
                if (!p || !status) {
                    AdvFree(p);
                    if (status)
                        object_free(status);
                    result = ERROR_NOT_ENOUGH_MEMORY;
                } else {
                    p->Entry = e;
                    p->Count = count + 1;
                    p->Wide = AdvAllocZero(p->Count * sizeof(WCHAR *));
                    p->Narrow = g_ansi_table ? AdvAllocZero(p->Count * sizeof(CHAR *)) : nullptr;
                    BOOL good = p->Wide != nullptr && (!g_ansi_table || p->Narrow);
                    if (good) {
                        p->Wide[0] = AdvAlloc((wcslen(name) + 1) * sizeof(WCHAR));
                        if (p->Wide[0])
                            wcscpy(p->Wide[0], name);
                        for (DWORD i = 0; i < count && good; i++) {
                            UINT32 n;
                            const WCHAR *arg = ScmGetChars(&r, &n);
                            p->Wide[i + 1] = AdvAlloc((n + 1) * sizeof(WCHAR));
                            if (p->Wide[i + 1] && arg) {
                                memcpy(p->Wide[i + 1], arg, n * sizeof(WCHAR));
                                p->Wide[i + 1][n] = 0;
                            } else {
                                good = FALSE;
                            }
                        }
                        good = good && p->Wide[0] != nullptr;
                        if (good && g_ansi_table)
                            for (DWORD i = 0; i < p->Count; i++)
                                p->Narrow[i] = AdvWideToAnsi(p->Wide[i]);
                    }
                    if (!good) {
                        for (DWORD i = 0; p->Wide && i < p->Count; i++)
                            AdvFree(p->Wide[i]);
                        AdvFree(p->Wide);
                        AdvFree(p->Narrow);
                        AdvFree(p);
                        object_free(status);
                        result = ERROR_NOT_ENOUGH_MEMORY;
                    } else {
                        e->Status = status;
                        e->Started = TRUE;
                        e->Handler = nullptr;
                        e->HandlerEx = nullptr;
                        HANDLE thread = CreateThread(nullptr, 0, service_main_thread, p, 0, nullptr);
                        if (thread) {
                            CloseHandle(thread);
                        } else {
                            e->Started = FALSE;
                            e->Status = nullptr;
                            object_free(status);
                            for (DWORD i = 0; i < p->Count; i++) {
                                AdvFree(p->Wide[i]);
                                if (p->Narrow)
                                    AdvFree(p->Narrow[i]);
                            }
                            AdvFree(p->Wide);
                            AdvFree(p->Narrow);
                            AdvFree(p);
                            result = ERROR_SERVICE_NO_THREAD;
                        }
                    }
                }
            }
            ReleaseSRWLockExclusive(&g_entries_lock);
        } else {
            AcquireSRWLockShared(&g_entries_lock);
            DISPATCH_ENTRY *e = find_entry(name);
            LPHANDLER_FUNCTION plain = e ? e->Handler : nullptr;
            LPHANDLER_FUNCTION_EX ex = e ? e->HandlerEx : nullptr;
            PVOID context = e ? e->Context : nullptr;
            ReleaseSRWLockShared(&g_entries_lock);
            if (!e || (!plain && !ex)) {
                result = ERROR_SERVICE_CANNOT_ACCEPT_CTRL;
            } else {
                DISPATCH_ENTRY copy = *e;
                copy.Handler = plain;
                copy.HandlerEx = ex;
                copy.Context = context;
                void *payload = event_size ? (void *)event_data : nullptr;
                result = handler_result(&copy, control, event_type, payload);
                /* a service that was told to stop, or to shut down, takes no more controls of the kind. */
                if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN ||
                    control == SERVICE_CONTROL_PRESHUTDOWN) {
                    AcquireSRWLockExclusive(&g_entries_lock);
                    e->Handler = nullptr;
                    e->HandlerEx = nullptr;
                    ReleaseSRWLockExclusive(&g_entries_lock);
                }
            }
        }
        AdvFree(name);
        AdvFree(data);
        if (answer(pipe, result))
            return;
        if (quit)
            return;
    }
}

static BOOL start_dispatcher(const WCHAR *const *names, void *const *mains, DWORD count, BOOL ansi)
{
    if (InterlockedExchange(&g_dispatcher_started, 1) == 1)
        return fail(ERROR_SERVICE_ALREADY_RUNNING);
    if (!count)
        return fail(ERROR_INVALID_PARAMETER);
    g_entries = AdvAllocZero(count * sizeof(DISPATCH_ENTRY));
    if (!g_entries)
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    g_entry_count = count;
    g_ansi_table = ansi;
    for (DWORD i = 0; i < count; i++) {
        g_entries[i].Name = AdvAlloc((wcslen(names[i]) + 1) * sizeof(WCHAR));
        if (!g_entries[i].Name) {
            free_entries();
            return fail(ERROR_NOT_ENOUGH_MEMORY);
        }
        wcscpy(g_entries[i].Name, names[i]);
        if (ansi)
            g_entries[i].MainA = (LPSERVICE_MAIN_FUNCTIONA)mains[i];
        else
            g_entries[i].MainW = (LPSERVICE_MAIN_FUNCTIONW)mains[i];
    }
    /* a service is a client of the manager first. */
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        DWORD error = GetLastError();
        free_entries();
        return fail(error);
    }
    CloseServiceHandle(manager);
    DWORD error;
    HANDLE pipe = open_channel(&error);
    if (!pipe) {
        free_entries();
        return fail(error);
    }
    /* the services are taken down before the system goes: below the programs' level, after services.exe's. */
    SetProcessShutdownParameters(0x100, 0);
    dispatch_loop(pipe);
    CloseHandle(pipe);
    free_entries();
    return TRUE;
}

ADVAPI BOOL WINAPI StartServiceCtrlDispatcherW(const SERVICE_TABLE_ENTRYW *table)
{
    if (g_dispatcher_started)
        return fail(ERROR_SERVICE_ALREADY_RUNNING);
    DWORD count = 0;
    if (table)
        while (table[count].lpServiceName)
            count++;
    if (!count) {
        InterlockedExchange(&g_dispatcher_started, 1);
        return fail(ERROR_INVALID_PARAMETER);
    }
    /* a group of services in svchost.exe has more than sixty. */
    const WCHAR **names = AdvAlloc(count * sizeof(*names));
    void **mains = AdvAlloc(count * sizeof(*mains));
    if (!names || !mains) {
        AdvFree(names);
        AdvFree(mains);
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    }
    for (DWORD i = 0; i < count; i++) {
        names[i] = table[i].lpServiceName;
        mains[i] = (void *)table[i].lpServiceProc;
    }
    BOOL result = start_dispatcher(names, mains, count, FALSE);
    AdvFree(names);
    AdvFree(mains);
    return result;
}

ADVAPI BOOL WINAPI StartServiceCtrlDispatcherA(const SERVICE_TABLE_ENTRYA *table)
{
    if (g_dispatcher_started)
        return fail(ERROR_SERVICE_ALREADY_RUNNING);
    DWORD count = 0;
    if (table)
        while (table[count].lpServiceName)
            count++;
    if (!count) {
        InterlockedExchange(&g_dispatcher_started, 1);
        return fail(ERROR_INVALID_PARAMETER);
    }
    WCHAR **names = AdvAllocZero(count * sizeof(*names));
    void **mains = AdvAlloc(count * sizeof(*mains));
    if (!names || !mains) {
        AdvFree(names);
        AdvFree(mains);
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    }
    BOOL ok = TRUE;
    for (DWORD i = 0; i < count; i++) {
        names[i] = AdvAnsiToWide(table[i].lpServiceName);
        mains[i] = (void *)table[i].lpServiceProc;
        ok = ok && names[i] != nullptr;
    }
    BOOL result = ok && start_dispatcher((const WCHAR *const *)names, mains, count, TRUE);
    for (DWORD i = 0; i < count; i++)
        AdvFree(names[i]);
    AdvFree(names);
    AdvFree(mains);
    return result;
}

static SERVICE_STATUS_HANDLE register_handler(const WCHAR *name, LPHANDLER_FUNCTION plain, LPHANDLER_FUNCTION_EX ex,
                                              PVOID context)
{
    if (!name) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    AcquireSRWLockExclusive(&g_entries_lock);
    DISPATCH_ENTRY *e = g_entries ? find_entry(name) : nullptr;
    SERVICE_STATUS_HANDLE handle = nullptr;
    DWORD error = ERROR_SUCCESS;
    if (!e)
        error = ERROR_SERVICE_NOT_IN_EXE;
    else if (!plain && !ex)
        error = ERROR_INVALID_PARAMETER;
    else {
        e->Handler = plain;
        e->HandlerEx = ex;
        e->Context = context;
        handle = e->Status;
    }
    ReleaseSRWLockExclusive(&g_entries_lock);
    if (error)
        AdvSetError(error);
    return handle;
}

ADVAPI SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerW(LPCWSTR name, LPHANDLER_FUNCTION handler)
{
    return register_handler(name, handler, nullptr, nullptr);
}

ADVAPI SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerExW(LPCWSTR name, LPHANDLER_FUNCTION_EX handler, LPVOID context)
{
    return register_handler(name, nullptr, handler, context);
}

ADVAPI SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerA(LPCSTR name, LPHANDLER_FUNCTION handler)
{
    PWSTR n = AdvAnsiToWide(name);
    SERVICE_STATUS_HANDLE h = name && !n ? nullptr : RegisterServiceCtrlHandlerW(n, handler);
    AdvFree(n);
    return h;
}

ADVAPI SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerExA(LPCSTR name, LPHANDLER_FUNCTION_EX handler, LPVOID context)
{
    PWSTR n = AdvAnsiToWide(name);
    SERVICE_STATUS_HANDLE h = name && !n ? nullptr : RegisterServiceCtrlHandlerExW(n, handler, context);
    AdvFree(n);
    return h;
}

ADVAPI BOOL WINAPI SetServiceStatus(SERVICE_STATUS_HANDLE handle, LPSERVICE_STATUS status)
{
    if (!handle)
        return fail(ERROR_INVALID_HANDLE);
    SC_OBJECT *o = object_find(handle, SCK_STATUS);
    if (!o)
        return fail(ERROR_INVALID_HANDLE);
    if (!status)
        return fail(ERROR_INVALID_ADDRESS);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_SET_STATUS);
    ScmPutU64(&w, o->Id);
    const DWORD *f = (const DWORD *)status;
    for (int i = 0; i < 7; i++)
        ScmPutU32(&w, f[i]);
    DWORD error = call_simple(&w);
    if (error)
        return fail(error);
    if (status->dwCurrentState == SERVICE_STOPPED) {
        /* it has stopped: it takes no controls, and the handle is no more. */
        AcquireSRWLockExclusive(&g_entries_lock);
        for (DWORD i = 0; i < g_entry_count; i++) {
            if (g_entries[i].Status == o) {
                g_entries[i].Status = nullptr;
                g_entries[i].Handler = nullptr;
                g_entries[i].HandlerEx = nullptr;
                g_entries[i].Started = FALSE;
            }
        }
        ReleaseSRWLockExclusive(&g_entries_lock);
        object_free(o);
    }
    return TRUE;
}

ADVAPI BOOL WINAPI SetServiceBits(SERVICE_STATUS_HANDLE handle, DWORD bits, BOOL set, BOOL update)
{
    SC_OBJECT *o = handle ? object_find(handle, SCK_STATUS) : nullptr;
    if (!o)
        return fail(ERROR_INVALID_HANDLE);
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_SET_BITS);
    ScmPutU64(&w, o->Id);
    ScmPutU32(&w, bits);
    ScmPutU32(&w, set != 0);
    ScmPutU32(&w, update != 0);
    DWORD error = call_simple(&w);
    return error ? fail(error) : TRUE;
}

/* ------------------------------------------------------------------ elevation */

/*
 * a process with the rights of an administrator: services.exe, which stands for the appinfo service of windows, makes
 * it with the linked token of the caller and the caller as its parent, and gives back handles in this process. the
 * startup information is what ShellExecute has to give: the window, the desktop and the title. an error of 0 is a
 * process, ERROR_CANCELLED a request the policy of the machine (or a token with no administrator behind it) refused.
 */
ADVAPI DWORD WINAPI MimeLaunchElevated(LPCWSTR application, LPCWSTR command_line, LPCWSTR directory, DWORD creation_flags,
                                       const STARTUPINFOW *startup, PROCESS_INFORMATION *info, DWORD *elevation)
{
    if (!command_line || !info)
        return ERROR_INVALID_PARAMETER;
    memset(info, 0, sizeof(*info));
    DWORD error = wait_ready();
    if (error)
        return error;
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_LAUNCH_ELEVATED);
    ScmPutString(&w, application);
    ScmPutString(&w, command_line);
    ScmPutString(&w, directory);
    ScmPutString(&w, startup ? startup->lpDesktop : nullptr);
    ScmPutString(&w, startup ? startup->lpTitle : nullptr);
    ScmPutU32(&w, creation_flags);
    ScmPutU32(&w, startup ? startup->dwFlags : 0);
    ScmPutU32(&w, startup ? startup->wShowWindow : 1);
    SC_REPLY reply;
    error = call(&w, &reply);
    if (!error) {
        info->dwProcessId = ScmGetU32(&reply.R);
        info->dwThreadId = ScmGetU32(&reply.R);
        DWORD kind = ScmGetU32(&reply.R);
        info->hProcess = (HANDLE)(ULONG_PTR)ScmGetU64(&reply.R);
        info->hThread = (HANDLE)(ULONG_PTR)ScmGetU64(&reply.R);
        if (reply.R.Failed)
            error = ERROR_INVALID_DATA;
        else if (elevation)
            *elevation = kind;
    }
    reply_free(&reply);
    AdvSetError(error);
    return error;
}
