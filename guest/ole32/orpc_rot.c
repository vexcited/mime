/*
 * the running object table across processes. An object that a program registers in the table is marshaled, as a
 * standard object reference, into a table the whole machine shares, under the key of its moniker; a program that asks for
 * the object finds the reference there and unmarshals it into a proxy. The table is a named section and a mutex, as the
 * one of the classes in orpc_class.c; Windows' is a service's (rpcss).
 *
 * A moniker is told apart by its key: the class of the moniker and its display name in lower case. Two monikers of one key
 * are one in the table, which is what IsEqual means for the monikers here (item, file and composite).
 */
#include "orpc.h"

#define ROT_ENTRIES 256
#define ROT_KEY_CHARS 256
#define ROT_OBJREF_MAX 320

typedef struct ROT_ENTRY_SHARED {
    ULONG Used;
    DWORD Pid;
    ULONG64 Created;
    DWORD Cookie;
    DWORD Flags;
    FILETIME Time;
    ULONG ObjrefSize;
    WCHAR Key[ROT_KEY_CHARS];
    BYTE Objref[ROT_OBJREF_MAX];
} ROT_ENTRY_SHARED;

typedef struct ROT_TABLE {
    ULONG Magic;
    ULONG Serial;
    ROT_ENTRY_SHARED Entries[ROT_ENTRIES];
} ROT_TABLE;

#define ROT_TABLE_MAGIC 0x4d524f54

static ROT_TABLE *g_table;
static HANDLE g_mutex;
static SRWLOCK g_open_lock = SRWLOCK_INIT;

/* the registrations of this process, whose marshaled references hold the objects. */
typedef struct LOCAL_ENTRY {
    struct LOCAL_ENTRY *Next;
    DWORD Cookie;
    ULONG Size;
    BYTE Objref[ROT_OBJREF_MAX];
} LOCAL_ENTRY;

static LOCAL_ENTRY *g_local;
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
static BOOL alive(const ROT_ENTRY_SHARED *e)
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
        HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Global\\MimeRunningObjectTableLock");
        HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(ROT_TABLE),
                                            L"Global\\MimeRunningObjectTable");
        ROT_TABLE *table = section ? MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ROT_TABLE)) : nullptr;
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
    if (g_table->Magic != ROT_TABLE_MAGIC) {
        memset(g_table, 0, sizeof(*g_table));
        g_table->Magic = ROT_TABLE_MAGIC;
    }
}

static void table_unlock(void)
{
    ReleaseMutex(g_mutex);
}

static void release_objref(const BYTE *objref, ULONG size)
{
    IStream *stream;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
        ULONG written;
        LARGE_INTEGER zero = {};
        stream->lpVtbl->Write(stream, objref, size, &written);
        stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
        CoReleaseMarshalData(stream);
        stream->lpVtbl->Release(stream);
    }
}

HRESULT OrpcRotPublish(const WCHAR *key, IUnknown *object, DWORD flags, DWORD cookie, const FILETIME *time)
{
    if (wcslen(key) >= ROT_KEY_CHARS)
        return E_INVALIDARG;
    HRESULT hr = table_open();
    if (FAILED(hr))
        return hr;
    IStream *stream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr))
        return hr;
    hr = CoMarshalInterface(stream, &IID_IUnknown, object, MSHCTX_LOCAL, nullptr,
                            flags & ROTFLAGS_REGISTRATIONKEEPSALIVE ? MSHLFLAGS_TABLESTRONG : MSHLFLAGS_TABLEWEAK);
    if (FAILED(hr)) {
        stream->lpVtbl->Release(stream);
        return hr;
    }
    LARGE_INTEGER zero = {};
    ULARGE_INTEGER end;
    stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &end);
    stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
    ULONG size = (ULONG)end.QuadPart;
    LOCAL_ENTRY *local = size <= ROT_OBJREF_MAX ? OleAllocZero(sizeof(*local)) : nullptr;
    if (!local) {
        CoReleaseMarshalData(stream);
        stream->lpVtbl->Release(stream);
        return size > ROT_OBJREF_MAX ? E_FAIL : E_OUTOFMEMORY;
    }
    ULONG done = 0;
    stream->lpVtbl->Read(stream, local->Objref, size, &done);
    stream->lpVtbl->Release(stream);
    local->Size = size;
    local->Cookie = cookie;

    table_lock();
    ROT_ENTRY_SHARED *slot = nullptr;
    for (ULONG i = 0; i < ROT_ENTRIES; i++) {
        ROT_ENTRY_SHARED *e = &g_table->Entries[i];
        if (e->Used && !alive(e))
            e->Used = 0;
        if (!e->Used && !slot)
            slot = e;
    }
    if (slot) {
        slot->Used = 1;
        slot->Pid = GetCurrentProcessId();
        slot->Created = creation_time(slot->Pid);
        slot->Cookie = cookie;
        slot->Flags = flags;
        slot->Time = *time;
        slot->ObjrefSize = size;
        wcscpy(slot->Key, key);
        memcpy(slot->Objref, local->Objref, size);
        g_table->Serial++;
    }
    table_unlock();
    if (!slot) {
        release_objref(local->Objref, size);
        OleFree(local);
        return E_OUTOFMEMORY;
    }
    AcquireSRWLockExclusive(&g_local_lock);
    local->Next = g_local;
    g_local = local;
    ReleaseSRWLockExclusive(&g_local_lock);
    return S_OK;
}

void OrpcRotRevoke(DWORD cookie)
{
    if (!g_table)
        return;
    table_lock();
    for (ULONG i = 0; i < ROT_ENTRIES; i++) {
        ROT_ENTRY_SHARED *e = &g_table->Entries[i];
        if (e->Used && e->Pid == GetCurrentProcessId() && e->Cookie == cookie)
            e->Used = 0;
    }
    g_table->Serial++;
    table_unlock();
    LOCAL_ENTRY *local = nullptr;
    AcquireSRWLockExclusive(&g_local_lock);
    for (LOCAL_ENTRY **link = &g_local; *link; link = &(*link)->Next)
        if ((*link)->Cookie == cookie) {
            local = *link;
            *link = local->Next;
            break;
        }
    ReleaseSRWLockExclusive(&g_local_lock);
    if (!local)
        return;
    /* the reference the table held lets go of the object. */
    release_objref(local->Objref, local->Size);
    OleFree(local);
}

void OrpcRotNoteChange(DWORD cookie, const FILETIME *time)
{
    if (!g_table)
        return;
    table_lock();
    for (ULONG i = 0; i < ROT_ENTRIES; i++) {
        ROT_ENTRY_SHARED *e = &g_table->Entries[i];
        if (e->Used && e->Pid == GetCurrentProcessId() && e->Cookie == cookie)
            e->Time = *time;
    }
    table_unlock();
}

/* the entry of another process under the key: its reference is copied out and its time when asked for. */
BOOL OrpcRotLookup(const WCHAR *key, BYTE *objref, ULONG *size, FILETIME *time)
{
    if (FAILED(table_open()))
        return FALSE;
    BOOL found = FALSE;
    table_lock();
    for (ULONG i = 0; i < ROT_ENTRIES && !found; i++) {
        ROT_ENTRY_SHARED *e = &g_table->Entries[i];
        if (!e->Used || wcscmp(e->Key, key))
            continue;
        if (!alive(e)) {
            e->Used = 0;
            continue;
        }
        if (objref) {
            memcpy(objref, e->Objref, e->ObjrefSize);
            *size = e->ObjrefSize;
        }
        if (time)
            *time = e->Time;
        found = TRUE;
    }
    table_unlock();
    return found;
}

HRESULT OrpcRotUnmarshal(const BYTE *objref, ULONG size, REFIID iid, void **object)
{
    IStream *stream;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
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
