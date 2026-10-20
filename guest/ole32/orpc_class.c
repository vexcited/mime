/*
 * the classes that programs serve to other processes. A program that registers a class object for a local server
 * marshals it, as a standard object reference to its IClassFactory, into a table the whole machine shares; a program
 * that asks for the class finds the reference there, or starts the program registered under HKCR\CLSID\{clsid}\LocalServer32
 * and waits for the reference to appear, and unmarshals it into a proxy.
 *
 * This is what windows' service control manager and rpcss do for classes; here the table is a named section and a mutex.
 */
#include "orpc.h"

#define CLASS_TABLE_ENTRIES 128
#define CLASS_OBJREF_MAX 320
#define CLASS_START_TIMEOUT_MS 120000
#define LS_E_EXEC_FAILURE ((HRESULT)0x80080005)

typedef struct CLASS_ENTRY_SHARED {
    ULONG Used;
    CLSID Clsid;
    DWORD Pid;
    ULONG64 Created;
    DWORD Cookie;
    DWORD Flags;
    ULONG ObjrefSize;
    BYTE Objref[CLASS_OBJREF_MAX];
} CLASS_ENTRY_SHARED;

typedef struct CLASS_TABLE {
    ULONG Magic;
    ULONG Serial;
    CLASS_ENTRY_SHARED Entries[CLASS_TABLE_ENTRIES];
} CLASS_TABLE;

#define CLASS_TABLE_MAGIC 0x4d434c54

static CLASS_TABLE *g_table;
static HANDLE g_mutex;
static SRWLOCK g_open_lock = SRWLOCK_INIT;

/* the registrations of this process, whose marshaled references hold the factories. */
typedef struct LOCAL_CLASS {
    struct LOCAL_CLASS *Next;
    DWORD Cookie;
    ULONG Size;
    BYTE Objref[CLASS_OBJREF_MAX];
} LOCAL_CLASS;

static LOCAL_CLASS *g_local;
static SRWLOCK g_local_lock = SRWLOCK_INIT;

static ULONG64 creation_time(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    FILETIME created, exited, kernel, user;
    ULONG64 value = 0;
    if (process) {
        if (GetProcessTimes(process, &created, &exited, &kernel, &user))
            value = ((ULONG64)created.dwHighDateTime << 32) | created.dwLowDateTime;
        CloseHandle(process);
    }
    return value;
}

/* a registration of a process that is gone is not one. */
static BOOL alive(const CLASS_ENTRY_SHARED *e)
{
    return e->Pid == GetCurrentProcessId() || (e->Created && creation_time(e->Pid) == e->Created);
}

static HRESULT table_open(void)
{
    if (g_table)
        return S_OK;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&g_open_lock);
    if (!g_table) {
        HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Global\\MimeComClassTableLock");
        HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(CLASS_TABLE),
                                            L"Global\\MimeComClassTable");
        CLASS_TABLE *table = section ? MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CLASS_TABLE)) : nullptr;
        if (!mutex || !table) {
            if (section)
                CloseHandle(section);
            if (mutex)
                CloseHandle(mutex);
            hr = E_OUTOFMEMORY;
        } else {
            g_mutex = mutex;
            g_table = table;
        }
    }
    ReleaseSRWLockExclusive(&g_open_lock);
    return hr;
}

static void table_lock(void)
{
    WaitForSingleObject(g_mutex, INFINITE);
    if (g_table->Magic != CLASS_TABLE_MAGIC) {
        memset(g_table, 0, sizeof(*g_table));
        g_table->Magic = CLASS_TABLE_MAGIC;
    }
}

static void table_unlock(void)
{
    ReleaseMutex(g_mutex);
}

/* ------------------------------------------------------------------ serving a class */

HRESULT OrpcClassPublish(REFCLSID clsid, IUnknown *factory, DWORD flags, DWORD cookie)
{
    HRESULT hr = table_open();
    if (FAILED(hr))
        return hr;
    IStream *stream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr))
        return hr;
    hr = CoMarshalInterface(stream, &IID_IClassFactory, factory, MSHCTX_LOCAL, nullptr, MSHLFLAGS_TABLESTRONG);
    if (FAILED(hr)) {
        stream->lpVtbl->Release(stream);
        return hr;
    }
    LARGE_INTEGER zero = {};
    ULARGE_INTEGER end;
    stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &end);
    stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
    ULONG size = (ULONG)end.QuadPart;
    LOCAL_CLASS *local = size <= CLASS_OBJREF_MAX ? OleAllocZero(sizeof(*local)) : nullptr;
    if (!local) {
        CoReleaseMarshalData(stream);
        stream->lpVtbl->Release(stream);
        return size > CLASS_OBJREF_MAX ? E_FAIL : E_OUTOFMEMORY;
    }
    ULONG done = 0;
    stream->lpVtbl->Read(stream, local->Objref, size, &done);
    stream->lpVtbl->Release(stream);
    local->Size = size;
    local->Cookie = cookie;

    table_lock();
    CLASS_ENTRY_SHARED *slot = nullptr;
    for (ULONG i = 0; i < CLASS_TABLE_ENTRIES; i++) {
        CLASS_ENTRY_SHARED *e = &g_table->Entries[i];
        if (e->Used && !alive(e))
            e->Used = 0;
        if (!e->Used && !slot)
            slot = e;
    }
    if (slot) {
        slot->Used = 1;
        slot->Clsid = *clsid;
        slot->Pid = GetCurrentProcessId();
        slot->Created = creation_time(slot->Pid);
        slot->Cookie = cookie;
        slot->Flags = flags;
        slot->ObjrefSize = size;
        memcpy(slot->Objref, local->Objref, size);
        g_table->Serial++;
    }
    table_unlock();
    if (!slot) {
        IStream *again;
        if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &again))) {
            ULONG written;
            again->lpVtbl->Write(again, local->Objref, size, &written);
            again->lpVtbl->Seek(again, zero, STREAM_SEEK_SET, nullptr);
            CoReleaseMarshalData(again);
            again->lpVtbl->Release(again);
        }
        OleFree(local);
        return E_OUTOFMEMORY;
    }
    AcquireSRWLockExclusive(&g_local_lock);
    local->Next = g_local;
    g_local = local;
    ReleaseSRWLockExclusive(&g_local_lock);
    return S_OK;
}

void OrpcClassRevoke(DWORD cookie)
{
    if (!g_table)
        return;
    table_lock();
    for (ULONG i = 0; i < CLASS_TABLE_ENTRIES; i++) {
        CLASS_ENTRY_SHARED *e = &g_table->Entries[i];
        if (e->Used && e->Pid == GetCurrentProcessId() && e->Cookie == cookie)
            e->Used = 0;
    }
    table_unlock();
    LOCAL_CLASS *local = nullptr;
    AcquireSRWLockExclusive(&g_local_lock);
    for (LOCAL_CLASS **link = &g_local; *link; link = &(*link)->Next)
        if ((*link)->Cookie == cookie) {
            local = *link;
            *link = local->Next;
            break;
        }
    ReleaseSRWLockExclusive(&g_local_lock);
    if (!local)
        return;
    /* the reference the table held lets go of the factory. */
    IStream *stream;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
        ULONG written;
        LARGE_INTEGER zero = {};
        stream->lpVtbl->Write(stream, local->Objref, local->Size, &written);
        stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
        CoReleaseMarshalData(stream);
        stream->lpVtbl->Release(stream);
    }
    OleFree(local);
}

/* ------------------------------------------------------------------ asking for a class */

/* the command line of the local server of a class, with -Embedding after it. */
static BOOL local_server_command(REFCLSID clsid, WCHAR *out, DWORD chars)
{
    HKEY key;
    if (OleOpenClassKey(clsid, L"LocalServer32", KEY_READ, &key))
        return FALSE;
    WCHAR raw[MAX_PATH + 64];
    DWORD type, size = sizeof(raw) - sizeof(WCHAR);
    LSTATUS status = RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)raw, &size);
    RegCloseKey(key);
    if (status || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(WCHAR))
        return FALSE;
    raw[size / sizeof(WCHAR)] = 0;
    WCHAR expanded[MAX_PATH + 64];
    DWORD n = type == REG_EXPAND_SZ ? ExpandEnvironmentStringsW(raw, expanded, RTL_NUMBER_OF(expanded)) : 0;
    const WCHAR *command = n && n <= RTL_NUMBER_OF(expanded) ? expanded : raw;
    if (wcslen(command) + 16 > chars)
        return FALSE;
    wcscpy(out, command);
    if (!wcsstr(out, L"-Embedding") && !wcsstr(out, L"/Embedding"))
        wcscat(out, L" -Embedding");
    return TRUE;
}

/* the reference of a served class, copied out of the table; false if the class is not served. */
static BOOL find_class(REFCLSID clsid, BYTE *objref, ULONG *size)
{
    BOOL found = FALSE;
    table_lock();
    for (ULONG i = 0; i < CLASS_TABLE_ENTRIES && !found; i++) {
        CLASS_ENTRY_SHARED *e = &g_table->Entries[i];
        if (!e->Used || !IsEqualCLSID(&e->Clsid, clsid))
            continue;
        if (!alive(e)) {
            e->Used = 0;
            continue;
        }
        memcpy(objref, e->Objref, e->ObjrefSize);
        *size = e->ObjrefSize;
        if (e->Flags & REGCLS_SINGLEUSE)
            e->Used = 0;
        found = TRUE;
    }
    table_unlock();
    return found;
}

HRESULT OrpcClassGet(REFCLSID clsid, REFIID iid, void **object)
{
    HRESULT hr = table_open();
    if (FAILED(hr))
        return hr;
    BYTE objref[CLASS_OBJREF_MAX];
    ULONG size = 0;
    HANDLE process = nullptr;
    if (!find_class(clsid, objref, &size)) {
        WCHAR command[MAX_PATH + 96];
        if (!local_server_command(clsid, command, RTL_NUMBER_OF(command)))
            return REGDB_E_CLASSNOTREG;
        STARTUPINFOW startup = {sizeof(startup)};
        PROCESS_INFORMATION info;
        if (!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info))
            return LS_E_EXEC_FAILURE;
        CloseHandle(info.hThread);
        process = info.hProcess;
        ULONGLONG end = GetTickCount64() + CLASS_START_TIMEOUT_MS;
        BOOL got = FALSE;
        while (!got && GetTickCount64() < end) {
            Sleep(20);
            got = find_class(clsid, objref, &size);
            /* a server that ended before it served the class is not coming. */
            if (!got && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
                got = find_class(clsid, objref, &size);
                break;
            }
        }
        CloseHandle(process);
        if (!got)
            return LS_E_EXEC_FAILURE;
    }
    IStream *stream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr))
        return hr;
    ULONG written;
    LARGE_INTEGER zero = {};
    stream->lpVtbl->Write(stream, objref, size, &written);
    stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
    hr = CoUnmarshalInterface(stream, iid, object);
    stream->lpVtbl->Release(stream);
    return hr;
}
