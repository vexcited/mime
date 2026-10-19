/*
 * out of process class objects: a program that registers a class with CoRegisterClassObject for a local server serves it
 * to other processes, and CoGetClassObject with CLSCTX_LOCAL_SERVER finds or starts the program registered under
 * HKCR\CLSID\{clsid}\LocalServer32 and talks to it. there is no RPC runtime in Mime, so the two ends speak over a named
 * pipe of their own, \\.\pipe\MimeCom.{clsid}, and an object crosses only as one of the interfaces this file has a
 * proxy and a stub for: IUnknown, IClassFactory and INotificationActivationCallback, the interface an application's
 * toast activator is called through. any other interface is E_NOINTERFACE across the pipe. this is a decision, not a
 * reading of the Windows runtime: a program that marshals through the RPC runtime itself does not reach it.
 *
 * the messages are a header of four 32-bit numbers (the size of the rest, the operation, the handle of the object and
 * a spare) and little endian fields; strings are a count of characters and the characters. a connection is a session:
 * the objects it was given are numbered, 1 being the class factory, and released with the connection.
 */
#include "ole32_private.h"
#include <mime/win32/notificationactivationcallback.h>

#define LS_MAX_OBJECTS 64
#define LS_MAX_MESSAGE 0x10000
#define LS_START_TIMEOUT_MS 120000

#define LS_E_EXEC_FAILURE ((HRESULT)0x80080005)
#define RPC_E_DISCONNECTED ((HRESULT)0x80010108)
#define RPC_E_INVALID_DATAPACKET ((HRESULT)0x8001000F)

enum {
    OP_QUERY = 1,    /* handle, iid -> hr, new handle */
    OP_RELEASE = 2,  /* handle */
    OP_CREATE = 3,   /* the factory's handle, iid -> hr, new handle */
    OP_ACTIVATE = 4, /* handle, aumid, arguments, inputs -> hr */
    OP_LOCK = 5,     /* the factory's handle, lock -> hr */
};

typedef struct LS_HEADER {
    UINT32 Size, Op, Handle, Spare;
} LS_HEADER;

/* ------------------------------------------------------------------ the pipe */

static void pipe_name(REFCLSID clsid, WCHAR *out)
{
    wcscpy(out, L"\\\\.\\pipe\\MimeCom.");
    OleFormatGuid(clsid, out + wcslen(out));
}

static BOOL read_exact(HANDLE pipe, void *data, DWORD size)
{
    BYTE *p = data;
    while (size) {
        DWORD got;
        if (!ReadFile(pipe, p, size, &got, nullptr) || !got)
            return FALSE;
        p += got;
        size -= got;
    }
    return TRUE;
}

static BOOL write_exact(HANDLE pipe, const void *data, DWORD size)
{
    const BYTE *p = data;
    while (size) {
        DWORD put;
        if (!WriteFile(pipe, p, size, &put, nullptr) || !put)
            return FALSE;
        p += put;
        size -= put;
    }
    return TRUE;
}

/* a message under construction or being read. */
typedef struct LS_BUFFER {
    BYTE *Data;
    UINT32 Size, Capacity, Position;
    BOOL Failed;
} LS_BUFFER;

static void buffer_free(LS_BUFFER *b)
{
    OleFree(b->Data);
    memset(b, 0, sizeof(*b));
}

static void put(LS_BUFFER *b, const void *data, UINT32 size)
{
    if (b->Failed)
        return;
    if (b->Size + size > LS_MAX_MESSAGE) {
        b->Failed = TRUE;
        return;
    }
    if (b->Size + size > b->Capacity) {
        UINT32 capacity = b->Capacity ? b->Capacity * 2 : 256;
        while (capacity < b->Size + size)
            capacity *= 2;
        BYTE *grown = OleAlloc(capacity);
        if (!grown) {
            b->Failed = TRUE;
            return;
        }
        if (b->Size)
            memcpy(grown, b->Data, b->Size);
        OleFree(b->Data);
        b->Data = grown;
        b->Capacity = capacity;
    }
    memcpy(b->Data + b->Size, data, size);
    b->Size += size;
}

static void put_u32(LS_BUFFER *b, UINT32 value)
{
    put(b, &value, 4);
}

static void put_string(LS_BUFFER *b, LPCWSTR text)
{
    UINT32 n = text ? (UINT32)wcslen(text) : 0;
    put_u32(b, n);
    put(b, text, n * sizeof(WCHAR));
}

static BOOL get(LS_BUFFER *b, void *out, UINT32 size)
{
    if (b->Position + size > b->Size)
        return FALSE;
    memcpy(out, b->Data + b->Position, size);
    b->Position += size;
    return TRUE;
}

static BOOL get_u32(LS_BUFFER *b, UINT32 *value)
{
    return get(b, value, 4);
}

/* a string of the message in a new allocation. */
static WCHAR *get_string(LS_BUFFER *b)
{
    UINT32 n;
    if (!get_u32(b, &n) || n > LS_MAX_MESSAGE / 2 || b->Position + n * sizeof(WCHAR) > b->Size)
        return nullptr;
    WCHAR *text = OleAlloc((n + 1) * sizeof(WCHAR));
    if (!text)
        return nullptr;
    memcpy(text, b->Data + b->Position, n * sizeof(WCHAR));
    text[n] = 0;
    b->Position += n * sizeof(WCHAR);
    return text;
}

static BOOL send_message(HANDLE pipe, UINT32 op, UINT32 handle, const LS_BUFFER *body)
{
    if (body && body->Failed)
        return FALSE;
    LS_HEADER h = {body ? body->Size : 0, op, handle, 0};
    BYTE *whole = OleAlloc(sizeof(h) + h.Size);
    if (!whole)
        return FALSE;
    memcpy(whole, &h, sizeof(h));
    if (h.Size)
        memcpy(whole + sizeof(h), body->Data, h.Size);
    BOOL ok = write_exact(pipe, whole, (DWORD)(sizeof(h) + h.Size));
    OleFree(whole);
    return ok;
}

static BOOL receive_message(HANDLE pipe, LS_HEADER *h, LS_BUFFER *body)
{
    memset(body, 0, sizeof(*body));
    if (!read_exact(pipe, h, sizeof(*h)) || h->Size > LS_MAX_MESSAGE)
        return FALSE;
    if (h->Size) {
        body->Data = OleAlloc(h->Size);
        if (!body->Data || !read_exact(pipe, body->Data, h->Size)) {
            buffer_free(body);
            return FALSE;
        }
        body->Size = body->Capacity = h->Size;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the server */

typedef struct LS_SESSION {
    HANDLE Pipe;
    IUnknown *Objects[LS_MAX_OBJECTS + 1]; /* handle 1 is the factory */
} LS_SESSION;

typedef struct LS_PUBLISHED {
    struct LS_PUBLISHED *Next;
    CLSID Clsid;
    IUnknown *Factory;
    DWORD Cookie;
    volatile LONG Stop;
    HANDLE Thread;
    HANDLE Listening; /* the first instance of the pipe, kept so that the name is the class's */
} LS_PUBLISHED;

static SRWLOCK g_lock = SRWLOCK_INIT;
static LS_PUBLISHED *g_published;

static UINT32 add_object(LS_SESSION *s, IUnknown *object)
{
    for (UINT32 i = 2; i <= LS_MAX_OBJECTS; i++)
        if (!s->Objects[i]) {
            s->Objects[i] = object;
            return i;
        }
    return 0;
}

static IUnknown *object_of(LS_SESSION *s, UINT32 handle)
{
    return handle >= 1 && handle <= LS_MAX_OBJECTS ? s->Objects[handle] : nullptr;
}

static void serve_activate(LS_SESSION *s, UINT32 handle, LS_BUFFER *in, LS_BUFFER *out)
{
    IUnknown *object = object_of(s, handle);
    INotificationActivationCallback *callback = nullptr;
    HRESULT hr = object ? object->lpVtbl->QueryInterface(object, &IID_INotificationActivationCallback, (void **)&callback)
                        : E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        WCHAR *aumid = get_string(in), *arguments = get_string(in);
        UINT32 count = 0;
        BOOL ok = aumid && arguments && get_u32(in, &count) && count <= 64;
        NOTIFICATION_USER_INPUT_DATA data[64];
        WCHAR *strings[128] = {};
        for (UINT32 i = 0; ok && i < count; i++) {
            strings[2 * i] = get_string(in);
            strings[2 * i + 1] = get_string(in);
            ok = strings[2 * i] && strings[2 * i + 1];
            data[i].Key = strings[2 * i];
            data[i].Value = strings[2 * i + 1];
        }
        hr = ok ? callback->lpVtbl->Activate(callback, aumid, arguments, count ? data : nullptr, count) : E_INVALIDARG;
        for (UINT32 i = 0; i < 2 * count && i < 128; i++)
            OleFree(strings[i]);
        OleFree(aumid);
        OleFree(arguments);
        callback->lpVtbl->Release(callback);
    }
    put_u32(out, (UINT32)hr);
}

static void serve_session(LS_SESSION *s)
{
    for (;;) {
        LS_HEADER h;
        LS_BUFFER in, out = {};
        if (!receive_message(s->Pipe, &h, &in))
            return;
        GUID iid;
        switch (h.Op) {
        case OP_QUERY:
        case OP_CREATE: {
            IUnknown *source = object_of(s, h.Handle), *made = nullptr;
            HRESULT hr = source && get(&in, &iid, sizeof(iid)) ? S_OK : E_INVALIDARG;
            UINT32 handle = 0;
            if (SUCCEEDED(hr)) {
                if (h.Op == OP_CREATE) {
                    IClassFactory *factory;
                    hr = source->lpVtbl->QueryInterface(source, &IID_IClassFactory, (void **)&factory);
                    if (SUCCEEDED(hr)) {
                        hr = factory->lpVtbl->CreateInstance(factory, nullptr, &iid, (void **)&made);
                        factory->lpVtbl->Release(factory);
                    }
                } else {
                    hr = source->lpVtbl->QueryInterface(source, &iid, (void **)&made);
                }
            }
            if (SUCCEEDED(hr)) {
                handle = add_object(s, made);
                if (!handle) {
                    made->lpVtbl->Release(made);
                    hr = E_OUTOFMEMORY;
                }
            }
            put_u32(&out, (UINT32)hr);
            put_u32(&out, handle);
            break;
        }
        case OP_RELEASE:
            if (h.Handle > 1 && h.Handle <= LS_MAX_OBJECTS && s->Objects[h.Handle]) {
                s->Objects[h.Handle]->lpVtbl->Release(s->Objects[h.Handle]);
                s->Objects[h.Handle] = nullptr;
            }
            put_u32(&out, 0);
            break;
        case OP_ACTIVATE:
            serve_activate(s, h.Handle, &in, &out);
            break;
        case OP_LOCK: {
            IUnknown *source = object_of(s, h.Handle);
            UINT32 lock = 0;
            IClassFactory *factory;
            HRESULT hr = source && get_u32(&in, &lock) &&
                                 SUCCEEDED(source->lpVtbl->QueryInterface(source, &IID_IClassFactory, (void **)&factory))
                             ? S_OK
                             : E_INVALIDARG;
            if (SUCCEEDED(hr)) {
                hr = factory->lpVtbl->LockServer(factory, lock != 0);
                factory->lpVtbl->Release(factory);
            }
            put_u32(&out, (UINT32)hr);
            break;
        }
        default:
            put_u32(&out, (UINT32)E_NOTIMPL);
        }
        BOOL sent = send_message(s->Pipe, h.Op, h.Handle, &out);
        buffer_free(&in);
        buffer_free(&out);
        if (!sent)
            return;
    }
}

typedef struct LS_CONNECTION_ARGS {
    HANDLE Pipe;
    IUnknown *Factory;
} LS_CONNECTION_ARGS;

static DWORD WINAPI session_thread(void *param)
{
    LS_CONNECTION_ARGS *args = param;
    LS_SESSION *s = OleAllocZero(sizeof(*s));
    if (s) {
        s->Pipe = args->Pipe;
        s->Objects[1] = args->Factory;
        /* the session's own reference of the factory. */
        args->Factory->lpVtbl->AddRef(args->Factory);
        serve_session(s);
        for (UINT32 i = 1; i <= LS_MAX_OBJECTS; i++)
            if (s->Objects[i])
                s->Objects[i]->lpVtbl->Release(s->Objects[i]);
        OleFree(s);
    }
    FlushFileBuffers(args->Pipe);
    DisconnectNamedPipe(args->Pipe);
    CloseHandle(args->Pipe);
    args->Factory->lpVtbl->Release(args->Factory);
    OleFree(args);
    return 0;
}

static DWORD WINAPI accept_thread(void *param)
{
    LS_PUBLISHED *p = param;
    WCHAR name[80];
    pipe_name(&p->Clsid, name);
    for (;;) {
        HANDLE pipe = p->Listening;
        if (pipe == INVALID_HANDLE_VALUE) {
            pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                    PIPE_UNLIMITED_INSTANCES, LS_MAX_MESSAGE, LS_MAX_MESSAGE, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
                break;
        }
        p->Listening = INVALID_HANDLE_VALUE;
        BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (p->Stop || !connected) {
            CloseHandle(pipe);
            break;
        }
        LS_CONNECTION_ARGS *args = OleAlloc(sizeof(*args));
        HANDLE thread = nullptr;
        if (args) {
            args->Pipe = pipe;
            args->Factory = p->Factory;
            args->Factory->lpVtbl->AddRef(args->Factory);
            thread = CreateThread(nullptr, 0, session_thread, args, 0, nullptr);
            if (thread)
                CloseHandle(thread);
            else {
                args->Factory->lpVtbl->Release(args->Factory);
                OleFree(args);
            }
        }
        if (!thread)
            CloseHandle(pipe);
    }
    return 0;
}

/* offers the class to other processes, unless another process already does. */
void OleLocalServerPublish(REFCLSID clsid, IUnknown *factory, DWORD cookie)
{
    WCHAR name[80];
    pipe_name(clsid, name);
    HANDLE first = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
                                    LS_MAX_MESSAGE, LS_MAX_MESSAGE, 0, nullptr);
    if (first == INVALID_HANDLE_VALUE)
        return;
    LS_PUBLISHED *p = OleAllocZero(sizeof(*p));
    if (!p) {
        CloseHandle(first);
        return;
    }
    p->Clsid = *clsid;
    p->Factory = factory;
    factory->lpVtbl->AddRef(factory);
    p->Cookie = cookie;
    p->Listening = first;
    p->Thread = CreateThread(nullptr, 0, accept_thread, p, 0, nullptr);
    if (!p->Thread) {
        factory->lpVtbl->Release(factory);
        CloseHandle(first);
        OleFree(p);
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    p->Next = g_published;
    g_published = p;
    ReleaseSRWLockExclusive(&g_lock);
}

void OleLocalServerRevoke(DWORD cookie)
{
    LS_PUBLISHED *p = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    for (LS_PUBLISHED **link = &g_published; *link; link = &(*link)->Next)
        if ((*link)->Cookie == cookie) {
            p = *link;
            *link = p->Next;
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
    if (!p)
        return;
    p->Stop = TRUE;
    /* a connection wakes the thread that waits for one. */
    WCHAR name[80];
    pipe_name(&p->Clsid, name);
    HANDLE wake = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE)
        CloseHandle(wake);
    WaitForSingleObject(p->Thread, 2000);
    CloseHandle(p->Thread);
    p->Factory->lpVtbl->Release(p->Factory);
    OleFree(p);
}

/* ------------------------------------------------------------------ the client */

/* a connection to a server, shared by the proxies of the objects it gave. */
typedef struct LS_CONNECTION {
    LONG Refs;
    HANDLE Pipe;
    SRWLOCK Lock;
} LS_CONNECTION;

static void connection_release(LS_CONNECTION *c)
{
    if (InterlockedDecrement(&c->Refs))
        return;
    CloseHandle(c->Pipe);
    OleFree(c);
}

/* one call: the message goes out and the answer comes back, one at a time on a connection. */
static BOOL call(LS_CONNECTION *c, UINT32 op, UINT32 handle, const LS_BUFFER *body, LS_BUFFER *answer)
{
    LS_HEADER h;
    AcquireSRWLockExclusive(&c->Lock);
    BOOL ok = send_message(c->Pipe, op, handle, body) && receive_message(c->Pipe, &h, answer) && h.Op == op;
    ReleaseSRWLockExclusive(&c->Lock);
    return ok;
}

typedef struct LS_PROXY {
    const void *Vtbl;
    LONG Refs;
    LS_CONNECTION *Connection;
    UINT32 Handle;
    BOOL Factory;
} LS_PROXY;

static HRESULT proxy_for(LS_CONNECTION *c, UINT32 handle, BOOL factory, REFIID riid, void **object);

static HRESULT STDMETHODCALLTYPE proxy_query(void *This, REFIID riid, void **object)
{
    LS_PROXY *p = This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || (p->Factory && IsEqualIID(riid, &IID_IClassFactory)) ||
        (!p->Factory && IsEqualIID(riid, &IID_INotificationActivationCallback))) {
        *object = p;
        InterlockedIncrement(&p->Refs);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE proxy_add_ref(void *This)
{
    return (ULONG)InterlockedIncrement(&((LS_PROXY *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE proxy_release(void *This)
{
    LS_PROXY *p = This;
    LONG refs = InterlockedDecrement(&p->Refs);
    if (!refs) {
        if (p->Handle > 1) {
            LS_BUFFER answer;
            if (call(p->Connection, OP_RELEASE, p->Handle, nullptr, &answer))
                buffer_free(&answer);
        }
        connection_release(p->Connection);
        OleFree(p);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    LS_PROXY *p = (LS_PROXY *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_INotificationActivationCallback))
        return E_NOINTERFACE;
    LS_BUFFER body = {}, answer;
    put(&body, riid, sizeof(GUID));
    BOOL ok = call(p->Connection, OP_CREATE, p->Handle, &body, &answer);
    buffer_free(&body);
    if (!ok)
        return RPC_E_DISCONNECTED;
    UINT32 hr = E_FAIL, handle = 0;
    BOOL read = get_u32(&answer, &hr) && get_u32(&answer, &handle);
    buffer_free(&answer);
    if (!read)
        return RPC_E_INVALID_DATAPACKET;
    if (FAILED((HRESULT)hr))
        return (HRESULT)hr;
    return proxy_for(p->Connection, handle, FALSE, riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock)
{
    LS_PROXY *p = (LS_PROXY *)This;
    LS_BUFFER body = {}, answer;
    put_u32(&body, lock != 0);
    BOOL ok = call(p->Connection, OP_LOCK, p->Handle, &body, &answer);
    buffer_free(&body);
    if (!ok)
        return RPC_E_DISCONNECTED;
    UINT32 hr = E_FAIL;
    get_u32(&answer, &hr);
    buffer_free(&answer);
    return (HRESULT)hr;
}

static HRESULT STDMETHODCALLTYPE callback_activate(INotificationActivationCallback *This, LPCWSTR aumid, LPCWSTR arguments,
                                                   const NOTIFICATION_USER_INPUT_DATA *data, ULONG count)
{
    LS_PROXY *p = (LS_PROXY *)This;
    if (count > 64 || (count && !data))
        return E_INVALIDARG;
    LS_BUFFER body = {}, answer;
    put_string(&body, aumid);
    put_string(&body, arguments);
    put_u32(&body, count);
    for (ULONG i = 0; i < count; i++) {
        put_string(&body, data[i].Key);
        put_string(&body, data[i].Value);
    }
    BOOL ok = call(p->Connection, OP_ACTIVATE, p->Handle, &body, &answer);
    buffer_free(&body);
    if (!ok)
        return RPC_E_DISCONNECTED;
    UINT32 hr = E_FAIL;
    get_u32(&answer, &hr);
    buffer_free(&answer);
    return (HRESULT)hr;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    (void *)proxy_query, (void *)proxy_add_ref, (void *)proxy_release, factory_create, factory_lock};
static const INotificationActivationCallbackVtbl g_callback_vtbl = {
    (void *)proxy_query, (void *)proxy_add_ref, (void *)proxy_release, callback_activate};

/* a proxy of the remote object with the handle, in the interface asked. */
static HRESULT proxy_for(LS_CONNECTION *c, UINT32 handle, BOOL factory, REFIID riid, void **object)
{
    LS_PROXY *p = OleAllocZero(sizeof(*p));
    if (!p)
        return E_OUTOFMEMORY;
    p->Vtbl = factory ? (const void *)&g_factory_vtbl : (const void *)&g_callback_vtbl;
    p->Refs = 1;
    p->Connection = c;
    InterlockedIncrement(&c->Refs);
    p->Handle = handle;
    p->Factory = factory;
    HRESULT hr = proxy_query(p, riid, object);
    proxy_release(p);
    return hr;
}

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

static HANDLE open_server_pipe(const WCHAR *name)
{
    for (int attempt = 0; attempt < 20; attempt++) {
        HANDLE pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
            return pipe;
        if (GetLastError() != ERROR_PIPE_BUSY)
            break;
        WaitNamedPipeW(name, 200);
    }
    return INVALID_HANDLE_VALUE;
}

/*
 * the class object of a local server as a proxy: the server is started when it is not serving the class yet, and
 * waited for until it does, or ends.
 */
HRESULT OleLocalServerGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    WCHAR name[80];
    pipe_name(clsid, name);
    HANDLE pipe = open_server_pipe(name);
    if (pipe == INVALID_HANDLE_VALUE) {
        WCHAR command[MAX_PATH + 96];
        if (!local_server_command(clsid, command, RTL_NUMBER_OF(command)))
            return REGDB_E_CLASSNOTREG;
        STARTUPINFOW startup = {sizeof(startup)};
        PROCESS_INFORMATION process;
        if (!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
            return LS_E_EXEC_FAILURE;
        CloseHandle(process.hThread);
        ULONGLONG end = GetTickCount64() + LS_START_TIMEOUT_MS;
        while (pipe == INVALID_HANDLE_VALUE && GetTickCount64() < end) {
            Sleep(20);
            pipe = open_server_pipe(name);
            /* a server that ended before it served the class is not coming. */
            if (pipe == INVALID_HANDLE_VALUE && WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
                pipe = open_server_pipe(name);
                break;
            }
        }
        CloseHandle(process.hProcess);
        if (pipe == INVALID_HANDLE_VALUE)
            return LS_E_EXEC_FAILURE;
    }
    LS_CONNECTION *c = OleAllocZero(sizeof(*c));
    if (!c) {
        CloseHandle(pipe);
        return E_OUTOFMEMORY;
    }
    c->Refs = 1;
    c->Pipe = pipe;
    HRESULT hr = proxy_for(c, 1, TRUE, riid, object);
    connection_release(c);
    return hr;
}
