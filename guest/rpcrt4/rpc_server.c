/*
 * the rpc server: the protocol sequences and endpoints it listens on, registered interfaces and their managers, and the
 * threads that accept connections and serve calls.
 *
 * each endpoint is a pipe with an accepting thread, and each connection has a thread that serves its requests one after
 * the other, since calls on a connection are serial. the interface's dispatch routine serves a call with a message the
 * stub reads its buffer from, and what the stub leaves in the message is the reply.
 */
#include "rpcrt4_private.h"

#define MAX_CONTEXTS 64
#define MAX_ENDPOINTS 64

/* ------------------------------------------------------------------ state */

typedef struct SMGR {
    struct SMGR *Next;
    UUID Type;
    RPC_MGR_EPV *Epv;
} SMGR;

typedef struct SIF {
    struct SIF *Next;
    const RPC_SERVER_INTERFACE *Iface;
    ULONG Flags;
    ULONG MaxCalls;
    RPC_IF_CALLBACK_FN *Callback;
    SMGR *Managers;
    LONG Active;
    BOOL Unregistering;
} SIF;

typedef struct SENDPOINT {
    struct SENDPOINT *Next;
    WCHAR Protseq[24];
    WCHAR Endpoint[200];
    WCHAR PipeName[300];
    BOOL Dynamic;
    HANDLE Thread;
    HANDLE Stop;
    HANDLE FirstPipe;
    PSECURITY_DESCRIPTOR Sd;
    ULONG MaxCalls;
} SENDPOINT;

typedef struct {
    SIF *If;
    RPC_SYNTAX_IDENTIFIER Transfer;
    BOOL Used;
    BOOL Checked;
    /*
     * a com interface, bound by the iid of the com interface it carries. calls go to the single ole dispatch function
     * with a description naming that interface.
     */
    BOOL Ole;
    RPC_SERVER_INTERFACE OleIface;
} SCONTEXT;

typedef struct SCONN {
    struct SCONN *Next;
    RPC_STREAM Stream;
    SENDPOINT *Endpoint;
    HANDLE Thread;
    HANDLE Abort;
    SCONTEXT Context[MAX_CONTEXTS];
    UCHAR AuthType;
    UCHAR AuthLevel;
    ULONG ClientPid;
    USHORT MaxXmit;
    USHORT MaxRecv;
    /* reassembles a fragmented request. */
    unsigned char *Assembly;
    ULONG AssemblyHave;
    ULONG AssemblyCapacity;
} SCONN;

typedef struct SCALL {
    SCONN *Conn;
    BINDING *Handle;
    UUID Object;
    ULONG CallId;
    USHORT ContextId;
    USHORT Opnum;
    unsigned char *Request;
    unsigned char *Reply;
    BOOL Impersonating;
    SIF *If;
    RPC_THREAD *Thread;
    LONG Cancelled;
} SCALL;

static SRWLOCK g_lock = SRWLOCK_INIT;
static SIF *g_interfaces;
static SENDPOINT *g_endpoints;
static SCONN *g_connections;
static LONG g_listening;
static LONG g_active_calls;
static HANDLE g_idle_event;
static HANDLE g_stop_event;
static ULONG g_stack_size;
static RPC_MGMT_AUTHORIZATION_FN volatile g_mgmt_authorization;
static RPC_OBJECT_INQ_FN *g_object_inquiry;

typedef struct OBJTYPE {
    struct OBJTYPE *Next;
    UUID Object;
    UUID Type;
} OBJTYPE;
static OBJTYPE *g_object_types;

static BOOL same_uuid(const UUID *a, const UUID *b)
{
    return !memcmp(a, b, sizeof(*a));
}

static RPC_STATUS ensure_events(void)
{
    if (!g_idle_event) {
        HANDLE e = CreateEventW(nullptr, TRUE, TRUE, nullptr);
        if (InterlockedCompareExchangePointer(&g_idle_event, e, nullptr))
            CloseHandle(e);
        e = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (InterlockedCompareExchangePointer(&g_stop_event, e, nullptr))
            CloseHandle(e);
    }
    return g_idle_event && g_stop_event ? RPC_S_OK : RPC_S_OUT_OF_RESOURCES;
}

/* ------------------------------------------------------------------ objects and their types */

RPCRTAPI RPC_STATUS RPC_ENTRY RpcObjectSetType(UUID *object, UUID *type)
{
    static const UUID nil;
    if (!object || RpcpIsNil(object))
        return RPC_S_INVALID_OBJECT;
    if (type && RpcpIsNil(type))
        type = nullptr;
    RPC_STATUS status = RPC_S_OK;
    AcquireSRWLockExclusive(&g_lock);
    OBJTYPE **at = &g_object_types;
    while (*at && !same_uuid(&(*at)->Object, object))
        at = &(*at)->Next;
    if (type) {
        if (*at) {
            (*at)->Type = *type;
        } else {
            OBJTYPE *t = HeapAlloc(GetProcessHeap(), 0, sizeof(*t));
            if (t) {
                t->Object = *object;
                t->Type = *type;
                t->Next = nullptr;
                *at = t;
            } else {
                status = RPC_S_OUT_OF_MEMORY;
            }
        }
    } else if (*at) {
        OBJTYPE *gone = *at;
        *at = gone->Next;
        HeapFree(GetProcessHeap(), 0, gone);
    }
    (void)nil;
    ReleaseSRWLockExclusive(&g_lock);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcObjectSetInqFn(RPC_OBJECT_INQ_FN *function)
{
    g_object_inquiry = function;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcObjectInqType(UUID *object, UUID *type)
{
    if (!type)
        return RPC_S_INVALID_ARG;
    static const UUID nil;
    if (!object || RpcpIsNil(object)) {
        *type = nil;
        return RPC_S_OK;
    }
    RPC_STATUS status = RPC_S_OBJECT_NOT_FOUND;
    AcquireSRWLockShared(&g_lock);
    for (OBJTYPE *t = g_object_types; t; t = t->Next)
        if (same_uuid(&t->Object, object)) {
            *type = t->Type;
            status = RPC_S_OK;
            break;
        }
    ReleaseSRWLockShared(&g_lock);
    if (status && g_object_inquiry) {
        RPC_STATUS inq = RPC_S_OK;
        g_object_inquiry(object, type, &inq);
        return inq;
    }
    if (status)
        *type = nil;
    return RPC_S_OK;
}

/* ------------------------------------------------------------------ interfaces */

static BOOL interface_matches(const RPC_SERVER_INTERFACE *iface, const RPC_SYNTAX_IDENTIFIER *id)
{
    return same_uuid(&iface->InterfaceId.SyntaxGUID, &id->SyntaxGUID) &&
           iface->InterfaceId.SyntaxVersion.MajorVersion == id->SyntaxVersion.MajorVersion &&
           iface->InterfaceId.SyntaxVersion.MinorVersion >= id->SyntaxVersion.MinorVersion;
}

static SIF *find_interface_exact(const RPC_SERVER_INTERFACE *iface)
{
    for (SIF *s = g_interfaces; s; s = s->Next)
        if (s->Iface == iface ||
            (same_uuid(&s->Iface->InterfaceId.SyntaxGUID, &iface->InterfaceId.SyntaxGUID) &&
             s->Iface->InterfaceId.SyntaxVersion.MajorVersion == iface->InterfaceId.SyntaxVersion.MajorVersion &&
             s->Iface->InterfaceId.SyntaxVersion.MinorVersion == iface->InterfaceId.SyntaxVersion.MinorVersion))
            return s;
    return nullptr;
}

static void listen_automatically(void);

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterIf3(RPC_IF_HANDLE interface_spec, UUID *type, RPC_MGR_EPV *epv,
                                                   unsigned int flags, unsigned int max_calls,
                                                   unsigned int max_rpc_size, RPC_IF_CALLBACK_FN *callback, void *sd)
{
    const RPC_SERVER_INTERFACE *iface = interface_spec;
    if (!iface || iface->Length < NT351_INTERFACE_SIZE)
        return RPC_S_INVALID_ARG;
    if (flags & RPC_IF_AUTOLISTEN) {
        /* the interface starts the server for its own sake. */
    }
    if (max_calls == 0)
        max_calls = RPC_C_LISTEN_MAX_CALLS_DEFAULT;
    static const UUID nil;
    const UUID *mgr_type = type && !RpcpIsNil(type) ? type : &nil;
    if (!epv)
        epv = iface->DefaultManagerEpv;
    RPC_STATUS status = RPC_S_OK;
    AcquireSRWLockExclusive(&g_lock);
    SIF *s = find_interface_exact(iface);
    if (s) {
        for (SMGR *m = s->Managers; m; m = m->Next)
            if (same_uuid(&m->Type, mgr_type)) {
                ReleaseSRWLockExclusive(&g_lock);
                return RPC_S_TYPE_ALREADY_REGISTERED;
            }
    } else {
        s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
        if (!s) {
            ReleaseSRWLockExclusive(&g_lock);
            return RPC_S_OUT_OF_MEMORY;
        }
        s->Iface = iface;
        s->Flags = flags;
        s->MaxCalls = max_calls;
        s->Callback = callback;
        s->Next = g_interfaces;
        g_interfaces = s;
    }
    SMGR *m = HeapAlloc(GetProcessHeap(), 0, sizeof(*m));
    if (!m) {
        ReleaseSRWLockExclusive(&g_lock);
        return RPC_S_OUT_OF_MEMORY;
    }
    m->Type = *mgr_type;
    m->Epv = epv;
    m->Next = s->Managers;
    s->Managers = m;
    ReleaseSRWLockExclusive(&g_lock);
    if (flags & RPC_IF_AUTOLISTEN)
        listen_automatically();
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterIf2(RPC_IF_HANDLE interface_spec, UUID *type, RPC_MGR_EPV *epv,
                                                   unsigned int flags, unsigned int max_calls,
                                                   unsigned int max_rpc_size, RPC_IF_CALLBACK_FN *callback)
{
    return RpcServerRegisterIf3(interface_spec, type, epv, flags, max_calls, max_rpc_size, callback, nullptr);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterIfEx(RPC_IF_HANDLE interface_spec, UUID *type, RPC_MGR_EPV *epv,
                                                    unsigned int flags, unsigned int max_calls,
                                                    RPC_IF_CALLBACK_FN *callback)
{
    return RpcServerRegisterIf3(interface_spec, type, epv, flags, max_calls, (unsigned int)-1, callback, nullptr);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterIf(RPC_IF_HANDLE interface_spec, UUID *type, RPC_MGR_EPV *epv)
{
    return RpcServerRegisterIf3(interface_spec, type, epv, 0, RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1,
                                nullptr, nullptr);
}

static void free_interface(SIF *s)
{
    SMGR *m = s->Managers;
    while (m) {
        SMGR *next = m->Next;
        HeapFree(GetProcessHeap(), 0, m);
        m = next;
    }
    HeapFree(GetProcessHeap(), 0, s);
}

static RPC_STATUS unregister(RPC_IF_HANDLE interface_spec, UUID *type, BOOL wait)
{
    static const UUID nil;
    const RPC_SERVER_INTERFACE *iface = interface_spec;
    BOOL all = !iface;
    BOOL found = FALSE;
    for (;;) {
        SIF *gone = nullptr;
        AcquireSRWLockExclusive(&g_lock);
        for (SIF **at = &g_interfaces; *at; at = &(*at)->Next) {
            SIF *s = *at;
            if (!all && !find_interface_exact(iface))
                break;
            if (!all && s != find_interface_exact(iface))
                continue;
            if (type && !RpcpIsNil(type)) {
                SMGR **mm = &s->Managers;
                while (*mm && !same_uuid(&(*mm)->Type, type))
                    mm = &(*mm)->Next;
                if (!*mm)
                    continue;
                SMGR *m = *mm;
                *mm = m->Next;
                HeapFree(GetProcessHeap(), 0, m);
                found = TRUE;
                if (s->Managers)
                    continue;
            }
            *at = s->Next;
            gone = s;
            found = TRUE;
            break;
        }
        ReleaseSRWLockExclusive(&g_lock);
        if (!gone)
            break;
        /* calls still running in it finish first. */
        gone->Unregistering = TRUE;
        if (wait)
            while (gone->Active > 0)
                Sleep(1);
        free_interface(gone);
        if (!all)
            break;
    }
    (void)nil;
    return found ? RPC_S_OK : RPC_S_UNKNOWN_IF;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUnregisterIf(RPC_IF_HANDLE interface_spec, UUID *type,
                                                    unsigned int wait_for_calls)
{
    return unregister(interface_spec, type, wait_for_calls);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUnregisterIfEx(RPC_IF_HANDLE interface_spec, UUID *type, int rundown_contexts)
{
    return unregister(interface_spec, type, TRUE);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqIf(RPC_IF_HANDLE interface_spec, UUID *type, RPC_MGR_EPV **epv)
{
    const RPC_SERVER_INTERFACE *iface = interface_spec;
    static const UUID nil;
    RPC_STATUS status = RPC_S_UNKNOWN_IF;
    AcquireSRWLockShared(&g_lock);
    SIF *s = iface ? find_interface_exact(iface) : nullptr;
    if (s) {
        status = RPC_S_UNKNOWN_MGR_TYPE;
        for (SMGR *m = s->Managers; m; m = m->Next)
            if (same_uuid(&m->Type, type ? type : &nil)) {
                if (epv)
                    *epv = m->Epv;
                status = RPC_S_OK;
                break;
            }
    }
    ReleaseSRWLockShared(&g_lock);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcIfInqId(RPC_IF_HANDLE interface_spec, RPC_IF_ID *id)
{
    const RPC_SERVER_INTERFACE *iface = interface_spec;
    if (!iface || !id)
        return RPC_S_INVALID_ARG;
    id->Uuid = iface->InterfaceId.SyntaxGUID;
    id->VersMajor = iface->InterfaceId.SyntaxVersion.MajorVersion;
    id->VersMinor = iface->InterfaceId.SyntaxVersion.MinorVersion;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqIfIds(RPC_BINDING_HANDLE binding, RPC_IF_ID_VECTOR **vector)
{
    if (!vector)
        return RPC_S_INVALID_ARG;
    AcquireSRWLockShared(&g_lock);
    ULONG count = 0;
    for (SIF *s = g_interfaces; s; s = s->Next)
        count++;
    RPC_IF_ID_VECTOR *v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                    sizeof(*v) + count * sizeof(RPC_IF_ID *) + count * sizeof(RPC_IF_ID));
    if (!v) {
        ReleaseSRWLockShared(&g_lock);
        return RPC_S_OUT_OF_MEMORY;
    }
    RPC_IF_ID *ids = (RPC_IF_ID *)((unsigned char *)v + sizeof(*v) + count * sizeof(RPC_IF_ID *));
    ULONG i = 0;
    for (SIF *s = g_interfaces; s; s = s->Next, i++) {
        ids[i].Uuid = s->Iface->InterfaceId.SyntaxGUID;
        ids[i].VersMajor = s->Iface->InterfaceId.SyntaxVersion.MajorVersion;
        ids[i].VersMinor = s->Iface->InterfaceId.SyntaxVersion.MinorVersion;
        v->IfId[i] = &ids[i];
    }
    v->Count = count;
    ReleaseSRWLockShared(&g_lock);
    *vector = v;
    return count ? RPC_S_OK : RPC_S_NO_INTERFACES;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcIfIdVectorFree(RPC_IF_ID_VECTOR **vector)
{
    if (!vector || !*vector)
        return RPC_S_INVALID_ARG;
    HeapFree(GetProcessHeap(), 0, *vector);
    *vector = nullptr;
    return RPC_S_OK;
}

/* ------------------------------------------------------------------ endpoints */

static RPC_STATUS protseq_status(PCWSTR protseq)
{
    if (!_wcsicmp(protseq, L"ncalrpc") || !_wcsicmp(protseq, L"ncacn_np"))
        return RPC_S_OK;
    if (!_wcsnicmp(protseq, L"ncacn_", 6) || !_wcsnicmp(protseq, L"ncadg_", 6))
        return RPC_S_PROTSEQ_NOT_SUPPORTED;
    return RPC_S_INVALID_RPC_PROTSEQ;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcNetworkIsProtseqValidW(RPC_WSTR protseq)
{
    return protseq ? protseq_status(protseq) : RPC_S_INVALID_RPC_PROTSEQ;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcNetworkIsProtseqValidA(RPC_CSTR protseq)
{
    PWSTR wide = RpcpWiden(protseq);
    if (protseq && !wide)
        return RPC_S_OUT_OF_MEMORY;
    RPC_STATUS status = RpcNetworkIsProtseqValidW(wide);
    RpcpFreeString(wide);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcNetworkInqProtseqsW(RPC_PROTSEQ_VECTORW **vector)
{
    static const WCHAR *const names[] = {L"ncalrpc", L"ncacn_np"};
    if (!vector)
        return RPC_S_INVALID_ARG;
    SIZE_T chars = 0;
    for (int i = 0; i < 2; i++)
        chars += wcslen(names[i]) + 1;
    RPC_PROTSEQ_VECTORW *v = HeapAlloc(GetProcessHeap(), 0, sizeof(*v) + sizeof(void *) + chars * sizeof(WCHAR));
    if (!v)
        return RPC_S_OUT_OF_MEMORY;
    v->Count = 2;
    WCHAR *text = (WCHAR *)((unsigned char *)v + sizeof(*v) + sizeof(void *));
    for (int i = 0; i < 2; i++) {
        v->Protseq[i] = text;
        wcscpy(text, names[i]);
        text += wcslen(names[i]) + 1;
    }
    *vector = v;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcNetworkInqProtseqsA(RPC_PROTSEQ_VECTORA **vector)
{
    static const char *const names[] = {"ncalrpc", "ncacn_np"};
    if (!vector)
        return RPC_S_INVALID_ARG;
    RPC_PROTSEQ_VECTORA *v = HeapAlloc(GetProcessHeap(), 0, sizeof(*v) + sizeof(void *) + 32);
    if (!v)
        return RPC_S_OUT_OF_MEMORY;
    v->Count = 2;
    char *text = (char *)v + sizeof(*v) + sizeof(void *);
    for (int i = 0; i < 2; i++) {
        v->Protseq[i] = (unsigned char *)text;
        lstrcpyA(text, names[i]);
        text += lstrlenA(names[i]) + 1;
    }
    *vector = v;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcProtseqVectorFreeW(RPC_PROTSEQ_VECTORW **vector)
{
    if (!vector || !*vector)
        return RPC_S_INVALID_ARG;
    HeapFree(GetProcessHeap(), 0, *vector);
    *vector = nullptr;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcProtseqVectorFreeA(RPC_PROTSEQ_VECTORA **vector)
{
    if (!vector || !*vector)
        return RPC_S_INVALID_ARG;
    HeapFree(GetProcessHeap(), 0, *vector);
    *vector = nullptr;
    return RPC_S_OK;
}

static HANDLE create_pipe_instance(SENDPOINT *e, BOOL first, DWORD *error)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), e->Sd, FALSE};
    HANDLE pipe = CreateNamedPipeW(e->PipeName,
                                   PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                   PIPE_UNLIMITED_INSTANCES, 0x10000, 0x10000, 0, &sa);
    if (pipe == INVALID_HANDLE_VALUE && error)
        *error = GetLastError();
    return pipe;
}

static DWORD WINAPI listener_main(void *parameter);

static void random_name(const WCHAR *prefix, WCHAR *out, SIZE_T count)
{
    UUID id;
    UuidCreate(&id);
    RPC_WSTR text;
    UuidToStringW(&id, &text);
    out[0] = 0;
    RpcpAppend(out, count, prefix);
    for (const WCHAR *c = (WCHAR *)text; *c; c++) {
        if (*c == L'-')
            continue;
        WCHAR one[2] = {*c, 0};
        RpcpAppend(out, count, one);
    }
    RpcStringFreeW(&text);
}

__declspec(dllimport) ULONG NTAPI RtlLengthSecurityDescriptor(PVOID sd);
__declspec(dllimport) LONG NTAPI RtlGetControlSecurityDescriptor(PVOID sd, PUSHORT control, PULONG revision);
__declspec(dllimport) LONG NTAPI RtlMakeSelfRelativeSD(PVOID absolute, PVOID relative, PULONG length);
#define SE_SELF_RELATIVE_ 0x8000

/* the endpoint keeps its own copy of the descriptor, the caller may free its own once the call returns. */
static void *copy_descriptor(void *sd)
{
    USHORT control;
    ULONG revision;
    ULONG length = RtlLengthSecurityDescriptor(sd);
    void *copy = HeapAlloc(GetProcessHeap(), 0, length);
    if (!copy)
        return nullptr;
    if (RtlGetControlSecurityDescriptor(sd, &control, &revision) < 0)
        goto failed;
    if (control & SE_SELF_RELATIVE_) {
        memcpy(copy, sd, length);
        return copy;
    }
    /* an absolute descriptor is made self relative, in a buffer that holds what it points to. */
    ULONG size = 0;
    RtlMakeSelfRelativeSD(sd, nullptr, &size);
    if (!size)
        goto failed;
    HeapFree(GetProcessHeap(), 0, copy);
    copy = HeapAlloc(GetProcessHeap(), 0, size);
    if (copy && RtlMakeSelfRelativeSD(sd, copy, &size) >= 0)
        return copy;
failed:
    HeapFree(GetProcessHeap(), 0, copy);
    return nullptr;
}

static RPC_STATUS endpoint_create(PCWSTR protseq, PCWSTR endpoint, ULONG max_calls, void *sd, SENDPOINT **out)
{
    RPC_STATUS status = protseq_status(protseq);
    if (status)
        return status;
    SENDPOINT *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e)
        return RPC_S_OUT_OF_MEMORY;
    RpcpCopy(e->Protseq, ARRAYSIZE(e->Protseq), protseq);
    if (endpoint && *endpoint) {
        if (wcslen(endpoint) >= ARRAYSIZE(e->Endpoint)) {
            HeapFree(GetProcessHeap(), 0, e);
            return RPC_S_INVALID_ENDPOINT_FORMAT;
        }
        wcscpy(e->Endpoint, endpoint);
    } else {
        e->Dynamic = TRUE;
        if (!_wcsicmp(protseq, L"ncalrpc"))
            random_name(L"LRPC-", e->Endpoint, ARRAYSIZE(e->Endpoint));
        else
            random_name(L"\\pipe\\rpc-", e->Endpoint, ARRAYSIZE(e->Endpoint));
    }
    status = RpcpPipeName(e->Protseq, e->Endpoint, e->PipeName, ARRAYSIZE(e->PipeName));
    if (status) {
        HeapFree(GetProcessHeap(), 0, e);
        return status;
    }
    e->MaxCalls = max_calls;
    e->Sd = sd ? copy_descriptor(sd) : RpcpEveryoneDescriptor(0x1F01FF);
    if (!e->Sd) {
        HeapFree(GetProcessHeap(), 0, e);
        return RPC_S_OUT_OF_MEMORY;
    }

    AcquireSRWLockExclusive(&g_lock);
    for (SENDPOINT *o = g_endpoints; o; o = o->Next)
        if (!_wcsicmp(o->PipeName, e->PipeName)) {
            ReleaseSRWLockExclusive(&g_lock);
            HeapFree(GetProcessHeap(), 0, e->Sd);
            HeapFree(GetProcessHeap(), 0, e);
            return RPC_S_DUPLICATE_ENDPOINT;
        }
    DWORD error = 0;
    e->FirstPipe = create_pipe_instance(e, TRUE, &error);
    if (e->FirstPipe == INVALID_HANDLE_VALUE) {
        ReleaseSRWLockExclusive(&g_lock);
        HeapFree(GetProcessHeap(), 0, e->Sd);
        HeapFree(GetProcessHeap(), 0, e);
        return error == ERROR_ACCESS_DENIED || error == ERROR_PIPE_BUSY ? RPC_S_DUPLICATE_ENDPOINT
                                                                         : RPC_S_CANT_CREATE_ENDPOINT;
    }
    e->Stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    e->Next = g_endpoints;
    g_endpoints = e;
    e->Thread = CreateThread(nullptr, 0, listener_main, e, 0, nullptr);
    ReleaseSRWLockExclusive(&g_lock);
    if (out)
        *out = e;
    return RPC_S_OK;
}

static RPC_STATUS use_protseq(PCWSTR protseq, ULONG max_calls, PCWSTR endpoint, void *sd)
{
    if (!protseq)
        return RPC_S_INVALID_RPC_PROTSEQ;
    if (ensure_events())
        return RPC_S_OUT_OF_RESOURCES;
    if (!_wcsicmp(protseq, L"ncacn_np") && (!endpoint || !*endpoint)) {
        /* the caller names a named pipe endpoint, a dynamic one is made like for the local sequence. */
    }
    return endpoint_create(protseq, endpoint, max_calls, sd, nullptr);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqEpW(RPC_WSTR protseq, unsigned int max_calls, RPC_WSTR endpoint, void *sd)
{
    if (!endpoint)
        return RPC_S_INVALID_ENDPOINT_FORMAT;
    return use_protseq(protseq, max_calls, endpoint, sd);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqEpExW(RPC_WSTR protseq, unsigned int max_calls, RPC_WSTR endpoint,
                                                       void *sd, PRPC_POLICY policy)
{
    return RpcServerUseProtseqEpW(protseq, max_calls, endpoint, sd);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqW(RPC_WSTR protseq, unsigned int max_calls, void *sd)
{
    return use_protseq(protseq, max_calls, nullptr, sd);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqExW(RPC_WSTR protseq, unsigned int max_calls, void *sd,
                                                     PRPC_POLICY policy)
{
    return RpcServerUseProtseqW(protseq, max_calls, sd);
}

#define NARROW_USE(name, ...)                                                                                       \
    PWSTR p = RpcpWiden(protseq), e = RpcpWiden(endpoint);                                                          \
    RPC_STATUS status = (p || !protseq) && (e || !endpoint) ? name(__VA_ARGS__) : RPC_S_OUT_OF_MEMORY;              \
    RpcpFreeString(p);                                                                                              \
    RpcpFreeString(e);                                                                                              \
    return status

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqEpA(RPC_CSTR protseq, unsigned int max_calls, RPC_CSTR endpoint, void *sd)
{
    NARROW_USE(RpcServerUseProtseqEpW, p, max_calls, e, sd);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqEpExA(RPC_CSTR protseq, unsigned int max_calls, RPC_CSTR endpoint,
                                                       void *sd, PRPC_POLICY policy)
{
    NARROW_USE(RpcServerUseProtseqEpW, p, max_calls, e, sd);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqA(RPC_CSTR protseq, unsigned int max_calls, void *sd)
{
    RPC_CSTR endpoint = nullptr;
    NARROW_USE(RpcServerUseProtseqW, p, max_calls, sd);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqExA(RPC_CSTR protseq, unsigned int max_calls, void *sd,
                                                     PRPC_POLICY policy)
{
    return RpcServerUseProtseqA(protseq, max_calls, sd);
}

static RPC_STATUS use_protseq_if(PCWSTR protseq, ULONG max_calls, RPC_IF_HANDLE interface_spec, void *sd)
{
    const RPC_SERVER_INTERFACE *iface = interface_spec;
    if (!iface)
        return RPC_S_INVALID_ARG;
    PWSTR endpoint = nullptr;
    for (unsigned i = 0; i < iface->RpcProtseqEndpointCount; i++) {
        PWSTR p = RpcpWiden(iface->RpcProtseqEndpoint[i].RpcProtocolSequence);
        BOOL match = p && !_wcsicmp(p, protseq);
        RpcpFreeString(p);
        if (match) {
            endpoint = RpcpWiden(iface->RpcProtseqEndpoint[i].Endpoint);
            break;
        }
    }
    RPC_STATUS status = use_protseq(protseq, max_calls, endpoint, sd);
    RpcpFreeString(endpoint);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqIfW(RPC_WSTR protseq, unsigned int max_calls,
                                                     RPC_IF_HANDLE interface_spec, void *sd)
{
    return protseq ? use_protseq_if(protseq, max_calls, interface_spec, sd) : RPC_S_INVALID_RPC_PROTSEQ;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqIfExW(RPC_WSTR protseq, unsigned int max_calls,
                                                       RPC_IF_HANDLE interface_spec, void *sd, PRPC_POLICY policy)
{
    return RpcServerUseProtseqIfW(protseq, max_calls, interface_spec, sd);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqIfA(RPC_CSTR protseq, unsigned int max_calls,
                                                     RPC_IF_HANDLE interface_spec, void *sd)
{
    PWSTR p = RpcpWiden(protseq);
    RPC_STATUS status = p || !protseq ? RpcServerUseProtseqIfW(p, max_calls, interface_spec, sd) : RPC_S_OUT_OF_MEMORY;
    RpcpFreeString(p);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqIfExA(RPC_CSTR protseq, unsigned int max_calls,
                                                       RPC_IF_HANDLE interface_spec, void *sd, PRPC_POLICY policy)
{
    return RpcServerUseProtseqIfA(protseq, max_calls, interface_spec, sd);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseAllProtseqs(unsigned int max_calls, void *sd)
{
    RPC_STATUS status = RpcServerUseProtseqW((RPC_WSTR)L"ncalrpc", max_calls, sd);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseAllProtseqsEx(unsigned int max_calls, void *sd, PRPC_POLICY policy)
{
    return RpcServerUseAllProtseqs(max_calls, sd);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseAllProtseqsIf(unsigned int max_calls, RPC_IF_HANDLE interface_spec, void *sd)
{
    const RPC_SERVER_INTERFACE *iface = interface_spec;
    if (!iface)
        return RPC_S_INVALID_ARG;
    RPC_STATUS status = RPC_S_NO_PROTSEQS;
    for (unsigned i = 0; i < iface->RpcProtseqEndpointCount; i++) {
        PWSTR p = RpcpWiden(iface->RpcProtseqEndpoint[i].RpcProtocolSequence);
        if (p && protseq_status(p) == RPC_S_OK)
            status = use_protseq_if(p, max_calls, interface_spec, sd);
        RpcpFreeString(p);
    }
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseAllProtseqsIfEx(unsigned int max_calls, RPC_IF_HANDLE interface_spec,
                                                          void *sd, PRPC_POLICY policy)
{
    return RpcServerUseAllProtseqsIf(max_calls, interface_spec, sd);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqBindings(RPC_BINDING_VECTOR **vector)
{
    if (!vector)
        return RPC_S_INVALID_ARG;
    AcquireSRWLockShared(&g_lock);
    ULONG count = 0;
    for (SENDPOINT *e = g_endpoints; e; e = e->Next)
        count++;
    if (!count) {
        ReleaseSRWLockShared(&g_lock);
        return RPC_S_NO_BINDINGS;
    }
    RPC_BINDING_VECTOR *v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      sizeof(*v) + (count > 1 ? (count - 1) : 0) * sizeof(RPC_BINDING_HANDLE));
    if (!v) {
        ReleaseSRWLockShared(&g_lock);
        return RPC_S_OUT_OF_MEMORY;
    }
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 4] = L"\\\\";
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computer + 2, &size);
    ULONG i = 0;
    for (SENDPOINT *e = g_endpoints; e; e = e->Next) {
        BINDING *b = RpcpBindingNew(BINDING_CLIENT);
        if (!b)
            break;
        b->Protseq = RpcpCopyString(e->Protseq);
        b->Address = RpcpCopyString(!_wcsicmp(e->Protseq, L"ncalrpc") ? L"" : computer);
        b->Endpoint = RpcpCopyString(e->Endpoint);
        b->Options = RpcpCopyString(L"");
        v->BindingH[i++] = b;
    }
    v->Count = i;
    ReleaseSRWLockShared(&g_lock);
    *vector = v;
    return RPC_S_OK;
}

/* ------------------------------------------------------------------ listener */

static DWORD WINAPI connection_main(void *parameter);
static void contexts_rundown(SCONN *conn);

static void connection_add(SCONN *c)
{
    AcquireSRWLockExclusive(&g_lock);
    c->Next = g_connections;
    g_connections = c;
    ReleaseSRWLockExclusive(&g_lock);
}

static void connection_remove(SCONN *c)
{
    AcquireSRWLockExclusive(&g_lock);
    for (SCONN **at = &g_connections; *at; at = &(*at)->Next)
        if (*at == c) {
            *at = c->Next;
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
}

static DWORD WINAPI listener_main(void *parameter)
{
    SENDPOINT *e = parameter;
    HANDLE pipe = e->FirstPipe;
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    for (;;) {
        ResetEvent(ov.hEvent);
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        DWORD error = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && error == ERROR_IO_PENDING) {
            HANDLE handles[2] = {ov.hEvent, e->Stop};
            DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &ov);
                break;
            }
            DWORD ignored;
            error = GetOverlappedResult(pipe, &ov, &ignored, FALSE) ? ERROR_SUCCESS : GetLastError();
        }
        if (error != ERROR_SUCCESS && error != ERROR_PIPE_CONNECTED) {
            if (WaitForSingleObject(e->Stop, 0) == WAIT_OBJECT_0)
                break;
            /* the client left before being accepted, so reuse the instance. */
            DisconnectNamedPipe(pipe);
            continue;
        }
        SCONN *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
        HANDLE next = create_pipe_instance(e, FALSE, nullptr);
        if (!c || next == INVALID_HANDLE_VALUE) {
            if (c)
                HeapFree(GetProcessHeap(), 0, c);
            DisconnectNamedPipe(pipe);
            if (next != INVALID_HANDLE_VALUE) {
                CloseHandle(pipe);
                pipe = next;
            } else {
                Sleep(100);
            }
            continue;
        }
        RpcpStreamAdopt(&c->Stream, pipe);
        c->Endpoint = e;
        c->MaxXmit = c->MaxRecv = RPC_MAX_FRAGMENT;
        connection_add(c);
        c->Thread = CreateThread(nullptr, g_stack_size, connection_main, c, 0, nullptr);
        if (!c->Thread) {
            connection_remove(c);
            RpcpStreamClose(&c->Stream);
            HeapFree(GetProcessHeap(), 0, c);
        }
        pipe = next;
    }
    CloseHandle(pipe);
    CloseHandle(ov.hEvent);
    return 0;
}

/* ------------------------------------------------------------------ connections */

static void send_fault(SCONN *c, ULONG call_id, USHORT context_id, ULONG status, BOOL did_not_execute)
{
    PDU_FAULT_PDU f = {};
    f.Response.Header.Version = RPC_PDU_VERSION;
    f.Response.Header.Type = PDU_FAULT;
    f.Response.Header.Flags = PFC_FIRST_FRAG | PFC_LAST_FRAG | (did_not_execute ? PFC_DID_NOT_EXECUTE : 0);
    f.Response.Header.Drep[0] = 0x10;
    f.Response.Header.FragLength = sizeof(f);
    f.Response.Header.CallId = call_id;
    f.Response.ContextId = context_id;
    f.Status = status;
    RpcpStreamSend(&c->Stream, &f, sizeof(f));
}


static SIF *interface_for(const PDU_SYNTAX *abstract)
{
    RPC_SYNTAX_IDENTIFIER id = {abstract->Uuid, {abstract->Major, abstract->Minor}};
    SIF *found = nullptr;
    AcquireSRWLockShared(&g_lock);
    for (SIF *s = g_interfaces; s; s = s->Next)
        if (!s->Unregistering && interface_matches(s->Iface, &id)) {
            found = s;
            break;
        }
    /* com interfaces are not registered by their own iid: the interface registered for com serves them all. */
    if (!found)
        for (SIF *s = g_interfaces; s; s = s->Next)
            if (!s->Unregistering && (s->Flags & RPC_IF_OLE)) {
                found = s;
                break;
            }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

/* the interface's own transfer syntax, and ndr64 when it carries tables for it. */
static BOOL interface_supports(const SIF *s, const PDU_SYNTAX *transfer, RPC_SYNTAX_IDENTIFIER *out)
{
    RPC_SYNTAX_IDENTIFIER want = {transfer->Uuid, {transfer->Major, transfer->Minor}};
    const RPC_SYNTAX_IDENTIFIER *own = &s->Iface->TransferSyntax;
    if (!own->SyntaxGUID.Data1 && !own->SyntaxVersion.MajorVersion)
        own = &g_ndr20_syntax;
    if (!memcmp(&want.SyntaxGUID, &own->SyntaxGUID, sizeof(GUID)) &&
        want.SyntaxVersion.MajorVersion == own->SyntaxVersion.MajorVersion) {
        *out = want;
        return TRUE;
    }
    return FALSE;
}

static void handle_bind(SCONN *c, const unsigned char *pdu, ULONG length)
{
    const PDU_HEADER *h = (const PDU_HEADER *)pdu;
    BOOL first = h->Type == PDU_BIND;
    const unsigned char *p = pdu + sizeof(PDU_HEADER);
    if (length < sizeof(PDU_HEADER) + 12)
        return;
    if (first) {
        c->MaxXmit = min(c->MaxXmit, *(const USHORT *)p);
        c->MaxRecv = min(c->MaxRecv, *(const USHORT *)(p + 2));
    }
    p += 8;
    unsigned count = p[0];
    p += 4;
    unsigned char reply[1024];
    memset(reply, 0, sizeof(reply));
    PDU_HEADER *rh = (PDU_HEADER *)reply;
    rh->Version = RPC_PDU_VERSION;
    rh->Type = first ? PDU_BIND_ACK : PDU_ALTER_CONTEXT_RESP;
    rh->Flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
    rh->Drep[0] = 0x10;
    rh->CallId = h->CallId;
    unsigned char *r = reply + sizeof(PDU_HEADER);
    *(USHORT *)r = c->MaxXmit;
    *(USHORT *)(r + 2) = c->MaxRecv;
    *(ULONG *)(r + 4) = 0x1000 + (ULONG)(ULONG_PTR)c->Endpoint;
    r += 8;
    *(USHORT *)r = 0;
    r += 2;
    while ((r - reply) & 3)
        r++;
    unsigned results = count < 8 ? count : 8;
    r[0] = (unsigned char)results;
    r += 4;
    for (unsigned i = 0; i < count; i++) {
        if ((SIZE_T)(p - pdu) + 4 + sizeof(PDU_SYNTAX) > length)
            break;
        USHORT id = *(const USHORT *)p;
        unsigned transfers = p[2];
        const PDU_SYNTAX *abstract = (const PDU_SYNTAX *)(p + 4);
        const PDU_SYNTAX *offered = abstract + 1;
        p += 4 + sizeof(PDU_SYNTAX) * (1 + transfers);
        if ((SIZE_T)(p - pdu) > length)
            break;
        USHORT result = 2, reason = 1;
        PDU_SYNTAX accepted = {};
        SIF *sif = interface_for(abstract);
        if (sif) {
            reason = 2;
            for (unsigned t = 0; t < transfers; t++) {
                RPC_SYNTAX_IDENTIFIER chosen;
                if (interface_supports(sif, &offered[t], &chosen)) {
                    result = 0;
                    reason = 0;
                    accepted.Uuid = chosen.SyntaxGUID;
                    accepted.Major = chosen.SyntaxVersion.MajorVersion;
                    accepted.Minor = chosen.SyntaxVersion.MinorVersion;
                    if (id < MAX_CONTEXTS) {
                        SCONTEXT *ctx = &c->Context[id];
                        ctx->If = sif;
                        ctx->Transfer = chosen;
                        ctx->Used = TRUE;
                        ctx->Checked = FALSE;
                        ctx->Ole = !interface_matches(sif->Iface, &(RPC_SYNTAX_IDENTIFIER){abstract->Uuid,
                                                      {abstract->Major, abstract->Minor}});
                        if (ctx->Ole) {
                            ctx->OleIface = *sif->Iface;
                            ctx->OleIface.InterfaceId.SyntaxGUID = abstract->Uuid;
                            ctx->OleIface.InterfaceId.SyntaxVersion = (RPC_VERSION){abstract->Major, abstract->Minor};
                        }
                    }
                    break;
                }
            }
        }
        if (id >= MAX_CONTEXTS && result == 0) {
            result = 2;
            reason = 0;
        }
        if (i < results) {
            *(USHORT *)r = result;
            *(USHORT *)(r + 2) = reason;
            memcpy(r + 4, &accepted, sizeof(accepted));
            r += 4 + sizeof(PDU_SYNTAX);
        }
    }
    /* the security the client asked for sticks to the connection. */
    if (h->AuthLength && length >= sizeof(PDU_SEC_TRAILER) + h->AuthLength) {
        const PDU_SEC_TRAILER *t = (const PDU_SEC_TRAILER *)(pdu + length - h->AuthLength - sizeof(PDU_SEC_TRAILER));
        c->AuthType = t->AuthType;
        c->AuthLevel = t->AuthLevel;
    }
    rh->FragLength = (USHORT)(r - reply);
    RpcpStreamSend(&c->Stream, reply, rh->FragLength);
}

/* ------------------------------------------------------------------ calls */

struct SCALL *RpcpCurrentCall(void)
{
    RPC_THREAD *t = RpcpThread();
    return t ? t->Call : nullptr;
}

BINDING *RpcpCurrentCallBinding(void)
{
    SCALL *call = RpcpCurrentCall();
    return call ? call->Handle : nullptr;
}

RPC_STATUS RpcpServerGetBuffer(RPC_MESSAGE *message, BINDING *b)
{
    SCALL *call = b->Call;
    if (!call)
        return RPC_S_NO_CALL_ACTIVE;
    unsigned char *buffer = RpcpBufferAlloc(message->BufferLength);
    if (!buffer)
        return RPC_S_OUT_OF_MEMORY;
    if (call->Reply)
        RpcpBufferFree(call->Reply);
    call->Reply = buffer;
    message->Buffer = buffer;
    return RPC_S_OK;
}

void RpcpServerFreeBuffer(RPC_MESSAGE *message)
{
    BINDING *b = RpcpBindingLookup(message->Handle);
    SCALL *call = b ? b->Call : nullptr;
    if (!call)
        return;
    /* the call owns the request and reply buffers and frees them when done. */
    if (message->Buffer == call->Reply)
        call->Reply = nullptr;
    if (message->Buffer == call->Request)
        call->Request = nullptr;
    RpcpBufferFree(message->Buffer);
    message->Buffer = nullptr;
}

static ULONG exception_status(ULONG code)
{
    switch (code) {
    case 0xC0000005:
    case 0xC00000FD:
        return RPC_S_ADDRESS_ERROR;
    case 0xC0000094:
        return RPC_S_ZERO_DIVIDE;
    case 0xC000008E:
        return RPC_S_FP_DIV_ZERO;
    case 0xC0000093:
        return RPC_S_FP_UNDERFLOW;
    case 0xC0000091:
        return RPC_S_FP_OVERFLOW;
    default:
        return code;
    }
}

/* the manager registered for the object's type. */
static RPC_MGR_EPV *manager_for(SIF *s, const UUID *object, RPC_STATUS *status)
{
    static const UUID nil;
    UUID type = nil;
    if (object && !RpcpIsNil(object)) {
        RPC_STATUS inq = RpcObjectInqType((UUID *)object, &type);
        if (inq) {
            *status = inq;
            return nullptr;
        }
    }
    RPC_MGR_EPV *epv = nullptr;
    BOOL found = FALSE;
    AcquireSRWLockShared(&g_lock);
    for (SMGR *m = s->Managers; m; m = m->Next)
        if (same_uuid(&m->Type, &type)) {
            epv = m->Epv;
            found = TRUE;
            break;
        }
    ReleaseSRWLockShared(&g_lock);
    *status = found ? RPC_S_OK : RPC_S_UNKNOWN_MGR_TYPE;
    return epv;
}

/* the interface's flags and security callback decide whether the connection may use it. */
static RPC_STATUS check_access(SCONN *c, SCONTEXT *ctx, SCALL *call)
{
    SIF *s = ctx->If;
    if ((s->Flags & RPC_IF_ALLOW_SECURE_ONLY) && c->AuthLevel < RPC_C_AUTHN_LEVEL_CONNECT)
        return RPC_S_ACCESS_DENIED;
    if (s->Callback) {
        if (!ctx->Checked || (s->Flags & RPC_IF_SEC_NO_CACHE)) {
            RPC_STATUS status = s->Callback((RPC_IF_HANDLE)s->Iface, call->Handle);
            if (status)
                return status;
            ctx->Checked = TRUE;
        }
    }
    return RPC_S_OK;
}

static void call_begin(void)
{
    if (InterlockedIncrement(&g_active_calls) == 1)
        ResetEvent(g_idle_event);
}

static void call_end(void)
{
    if (InterlockedDecrement(&g_active_calls) == 0)
        SetEvent(g_idle_event);
}

static void send_response(SCONN *c, SCALL *call, RPC_MESSAGE *m)
{
    unsigned char *data = m->Buffer;
    ULONG total = m->BufferLength;
    ULONG header = sizeof(PDU_RESPONSE_HEADER);
    ULONG per = (c->MaxRecv - header) & ~7u;
    if (!data) {
        total = 0;
    }
    if (total <= per && data && (data == call->Reply)) {
        unsigned char *pdu = data - header;
        PDU_RESPONSE_HEADER *h = (PDU_RESPONSE_HEADER *)pdu;
        h->Header.Version = RPC_PDU_VERSION;
        h->Header.Minor = 0;
        h->Header.Type = PDU_RESPONSE;
        h->Header.Flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
        h->Header.Drep[0] = 0x10;
        h->Header.Drep[1] = h->Header.Drep[2] = h->Header.Drep[3] = 0;
        h->Header.FragLength = (USHORT)(header + total);
        h->Header.AuthLength = 0;
        h->Header.CallId = call->CallId;
        h->AllocHint = total;
        h->ContextId = call->ContextId;
        h->CancelCount = 0;
        h->Reserved = 0;
        RpcpStreamSend(&c->Stream, pdu, header + total);
        return;
    }
    unsigned char *frag = HeapAlloc(GetProcessHeap(), 0, header + per);
    if (!frag)
        return;
    ULONG sent = 0;
    do {
        ULONG n = min(per, total - sent);
        PDU_RESPONSE_HEADER *h = (PDU_RESPONSE_HEADER *)frag;
        memset(h, 0, sizeof(*h));
        h->Header.Version = RPC_PDU_VERSION;
        h->Header.Type = PDU_RESPONSE;
        h->Header.Flags = (sent == 0 ? PFC_FIRST_FRAG : 0) | (sent + n == total ? PFC_LAST_FRAG : 0);
        h->Header.Drep[0] = 0x10;
        h->Header.FragLength = (USHORT)(header + n);
        h->Header.CallId = call->CallId;
        h->AllocHint = total - sent;
        h->ContextId = call->ContextId;
        if (n)
            memcpy(frag + header, data + sent, n);
        if (RpcpStreamSend(&c->Stream, frag, header + n))
            break;
        sent += n;
    } while (sent < total);
    HeapFree(GetProcessHeap(), 0, frag);
}

/* the request's stub data is complete. */
static void serve(SCONN *c, ULONG call_id, USHORT context_id, USHORT opnum, const UUID *object, unsigned char *stub,
                  ULONG length, ULONG drep)
{
    if (context_id >= MAX_CONTEXTS || !c->Context[context_id].Used) {
        send_fault(c, call_id, context_id, RPC_S_UNKNOWN_IF, TRUE);
        RpcpBufferFree(stub);
        return;
    }
    SCONTEXT *ctx = &c->Context[context_id];
    SIF *sif = ctx->If;
    if (!g_listening && !(sif->Flags & RPC_IF_AUTOLISTEN)) {
        send_fault(c, call_id, context_id, RPC_S_SERVER_UNAVAILABLE, TRUE);
        RpcpBufferFree(stub);
        return;
    }
    SCALL call = {};
    call.Conn = c;
    call.CallId = call_id;
    call.ContextId = context_id;
    call.Opnum = opnum;
    call.Request = stub;
    call.If = sif;
    if (object)
        call.Object = *object;
    call.Handle = RpcpBindingNew(BINDING_SERVER_CALL);
    if (!call.Handle) {
        send_fault(c, call_id, context_id, RPC_S_OUT_OF_MEMORY, TRUE);
        RpcpBufferFree(stub);
        return;
    }
    call.Handle->Call = &call;
    call.Handle->Object = call.Object;
    call.Handle->Protseq = RpcpCopyString(c->Endpoint->Protseq);
    call.Handle->Address = RpcpCopyString(L"");
    call.Handle->Endpoint = RpcpCopyString(c->Endpoint->Endpoint);
    call.Handle->Options = RpcpCopyString(L"");
    call.Handle->HasAuth = c->AuthLevel > RPC_C_AUTHN_LEVEL_NONE;
    call.Handle->AuthnLevel = c->AuthLevel;
    call.Handle->AuthnService = c->AuthType;

    call_begin();
    InterlockedIncrement(&sif->Active);
    RPC_STATUS status = check_access(c, ctx, &call);
    RPC_MGR_EPV *epv = nullptr;
    if (!status)
        epv = manager_for(sif, object, &status);
    const RPC_DISPATCH_TABLE *table = sif->Iface->DispatchTable;
    /* the single com dispatch function takes every method. */
    if (!status && (!table || (!ctx->Ole && opnum >= table->DispatchTableCount)))
        status = RPC_S_PROCNUM_OUT_OF_RANGE;
    if (status) {
        send_fault(c, call_id, context_id, status, TRUE);
    } else {
        RPC_MESSAGE m = {};
        m.Handle = call.Handle;
        m.DataRepresentation = drep;
        m.Buffer = stub;
        m.BufferLength = length;
        m.ProcNum = opnum;
        m.TransferSyntax = &ctx->Transfer;
        m.RpcInterfaceInformation = ctx->Ole ? (void *)&ctx->OleIface : (void *)sif->Iface;
        m.ManagerEpv = epv;
        m.RpcFlags = RPC_BUFFER_COMPLETE | RPCFLG_LOCAL_CALL;
        RPC_THREAD *thread = RpcpThread();
        SCALL *saved = thread ? thread->Call : nullptr;
        if (thread)
            thread->Call = &call;
        ULONG fault = 0;
        __try {
            table->DispatchTable[ctx->Ole ? 0 : opnum](&m);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            fault = exception_status(GetExceptionCode());
        }
        if (call.Impersonating) {
            RpcpAdvapi()->RevertToSelf();
            call.Impersonating = FALSE;
        }
        if (thread)
            thread->Call = saved;
        if (fault)
            send_fault(c, call_id, context_id, fault, FALSE);
        else
            send_response(c, &call, &m);
    }
    InterlockedDecrement(&sif->Active);
    call_end();
    RpcpBufferFree(call.Reply);
    RpcpBufferFree(call.Request);
    call.Handle->Call = nullptr;
    RpcpBindingRelease(call.Handle);
}

static void handle_request(SCONN *c, unsigned char *pdu, ULONG length)
{
    PDU_REQUEST_HEADER *r = (PDU_REQUEST_HEADER *)pdu;
    if (length < sizeof(*r))
        return;
    ULONG offset = sizeof(*r);
    UUID object;
    BOOL has_object = (r->Header.Flags & PFC_OBJECT_UUID) != 0;
    if (has_object) {
        if (length < offset + sizeof(UUID))
            return;
        object = *(UUID *)(pdu + offset);
        offset += sizeof(UUID);
    }
    ULONG auth = r->Header.AuthLength ? r->Header.AuthLength + sizeof(PDU_SEC_TRAILER) : 0;
    if (length < offset + auth)
        return;
    ULONG n = length - offset - auth;
    ULONG drep = r->Header.Drep[0] | (r->Header.Drep[1] << 8);
    BOOL first = (r->Header.Flags & PFC_FIRST_FRAG) != 0;
    BOOL last = (r->Header.Flags & PFC_LAST_FRAG) != 0;
    if (first && last) {
        unsigned char *stub = RpcpBufferAlloc(n ? n : 1);
        if (!stub) {
            send_fault(c, r->Header.CallId, r->ContextId, RPC_S_OUT_OF_MEMORY, TRUE);
            return;
        }
        memcpy(stub, pdu + offset, n);
        serve(c, r->Header.CallId, r->ContextId, r->Opnum, has_object ? &object : nullptr, stub, n, drep);
        return;
    }
    if (first) {
        RpcpBufferFree(c->Assembly);
        c->AssemblyCapacity = max(n * 2, min(r->AllocHint + n, 0x1000000u));
        c->Assembly = RpcpBufferAlloc(c->AssemblyCapacity ? c->AssemblyCapacity : 1);
        c->AssemblyHave = 0;
    }
    if (!c->Assembly)
        return;
    if (c->AssemblyHave + n > c->AssemblyCapacity) {
        ULONG capacity = max(c->AssemblyCapacity * 2, c->AssemblyHave + n);
        unsigned char *bigger = RpcpBufferAlloc(capacity);
        if (!bigger) {
            RpcpBufferFree(c->Assembly);
            c->Assembly = nullptr;
            return;
        }
        memcpy(bigger, c->Assembly, c->AssemblyHave);
        RpcpBufferFree(c->Assembly);
        c->Assembly = bigger;
        c->AssemblyCapacity = capacity;
    }
    memcpy(c->Assembly + c->AssemblyHave, pdu + offset, n);
    c->AssemblyHave += n;
    if (last) {
        unsigned char *stub = c->Assembly;
        ULONG have = c->AssemblyHave;
        c->Assembly = nullptr;
        serve(c, r->Header.CallId, r->ContextId, r->Opnum, has_object ? &object : nullptr, stub, have, drep);
    }
}

static DWORD WINAPI connection_main(void *parameter)
{
    SCONN *c = parameter;
    unsigned char *buffer = HeapAlloc(GetProcessHeap(), 0, RPC_MAX_FRAGMENT + 64);
    if (buffer) {
        c->Stream.Timeout = 0;
        for (;;) {
            ULONG got;
            if (RpcpStreamReceive(&c->Stream, buffer, RPC_MAX_FRAGMENT + 64, &got))
                break;
            PDU_HEADER *h = (PDU_HEADER *)buffer;
            if (got < sizeof(*h) || h->Version != RPC_PDU_VERSION || h->FragLength != got)
                break;
            switch (h->Type) {
            case PDU_BIND:
            case PDU_ALTER_CONTEXT:
                handle_bind(c, buffer, got);
                break;
            case PDU_REQUEST:
                handle_request(c, buffer, got);
                break;
            default:
                break;
            }
        }
        HeapFree(GetProcessHeap(), 0, buffer);
    }
    connection_remove(c);
    contexts_rundown(c);
    RpcpBufferFree(c->Assembly);
    RpcpStreamClose(&c->Stream);
    CloseHandle(c->Thread);
    HeapFree(GetProcessHeap(), 0, c);
    RpcpThreadDetach();
    return 0;
}

/* ------------------------------------------------------------------ server context handles */

#include <mime/win32/rpcndr.h>

/*
 * the client holds a twenty byte handle naming it, and its connection runs it down if the client goes away. the first
 * three words match NDR_SCONTEXT.
 */
typedef struct SCTX {
    struct SCTX *Next;
    SCONN *Conn;
    void *UserContext;
    GUID Id;
    BOOL Registered;
    NDR_RUNDOWN Rundown;
    ULONG Flags;
    void *Guard;
} SCTX;

#define SCTX_BUCKETS 64
static SRWLOCK g_ctx_lock = SRWLOCK_INIT;
static SCTX *g_ctx_table[SCTX_BUCKETS];

static ULONG ctx_bucket(const GUID *id)
{
    return (id->Data1 ^ id->Data2 ^ id->Data3 ^ *(const ULONG *)id->Data4) & (SCTX_BUCKETS - 1);
}

static SCTX *ctx_lookup(const GUID *id)
{
    for (SCTX *c = g_ctx_table[ctx_bucket(id)]; c; c = c->Next)
        if (same_uuid(&c->Id, id))
            return c;
    return nullptr;
}

/* a null handle in the buffer gives a new unregistered context. */
RPCRTAPI NDR_SCONTEXT RPC_ENTRY NDRSContextUnmarshall2(RPC_BINDING_HANDLE binding, void *buffer, ULONG drep, void *guard,
                                                       ULONG flags)
{
    static const GUID nil;
    const unsigned char *wire = buffer;
    BOOL null_handle = !wire;
    if (wire) {
        ULONG attrs = *(const ULONG *)wire;
        const GUID *id = (const GUID *)(wire + 4);
        null_handle = !attrs && same_uuid(id, &nil);
    }
    if (null_handle) {
        SCTX *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
        if (!c)
            RpcRaiseException(RPC_S_OUT_OF_MEMORY);
        c->Flags = flags;
        c->Guard = guard;
        return (NDR_SCONTEXT)c;
    }
    GUID id;
    memcpy(&id, wire + 4, sizeof(id));
    SCALL *call = RpcpCurrentCall();
    AcquireSRWLockShared(&g_ctx_lock);
    SCTX *c = ctx_lookup(&id);
    if (c && (!call || c->Conn != call->Conn))
        c = nullptr;
    ReleaseSRWLockShared(&g_ctx_lock);
    return (NDR_SCONTEXT)c;
}

static void ctx_unlink(SCTX *c)
{
    SCTX **at = &g_ctx_table[ctx_bucket(&c->Id)];
    while (*at && *at != c)
        at = &(*at)->Next;
    if (*at)
        *at = c->Next;
    c->Registered = FALSE;
}

/* written as closed if the user's part is gone, else registered and named. */
RPCRTAPI void RPC_ENTRY NDRSContextMarshall2(RPC_BINDING_HANDLE binding, NDR_SCONTEXT context, void *buffer,
                                             NDR_RUNDOWN rundown, void *guard, ULONG flags)
{
    SCTX *c = (SCTX *)context;
    unsigned char *wire = buffer;
    SCALL *call = RpcpCurrentCall();
    if (!c->UserContext) {
        memset(wire, 0, 20);
        AcquireSRWLockExclusive(&g_ctx_lock);
        if (c->Registered)
            ctx_unlink(c);
        ReleaseSRWLockExclusive(&g_ctx_lock);
        HeapFree(GetProcessHeap(), 0, c);
        return;
    }
    if (!c->Registered) {
        UuidCreate(&c->Id);
        c->Conn = call ? call->Conn : nullptr;
        c->Rundown = rundown;
        c->Flags = flags;
        c->Guard = guard;
        AcquireSRWLockExclusive(&g_ctx_lock);
        ULONG slot = ctx_bucket(&c->Id);
        c->Next = g_ctx_table[slot];
        g_ctx_table[slot] = c;
        c->Registered = TRUE;
        ReleaseSRWLockExclusive(&g_ctx_lock);
    }
    *(ULONG *)wire = 0;
    memcpy(wire + 4, &c->Id, sizeof(GUID));
}

RPCRTAPI NDR_SCONTEXT RPC_ENTRY NDRSContextUnmarshallEx(RPC_BINDING_HANDLE binding, void *buffer, ULONG drep)
{
    return NDRSContextUnmarshall2(binding, buffer, drep, nullptr, 0);
}

RPCRTAPI NDR_SCONTEXT RPC_ENTRY NDRSContextUnmarshall(void *buffer, ULONG drep)
{
    return NDRSContextUnmarshall2(nullptr, buffer, drep, nullptr, 0);
}

RPCRTAPI void RPC_ENTRY NDRSContextMarshallEx(RPC_BINDING_HANDLE binding, NDR_SCONTEXT context, void *buffer,
                                              NDR_RUNDOWN rundown)
{
    NDRSContextMarshall2(binding, context, buffer, rundown, nullptr, 0);
}

RPCRTAPI void RPC_ENTRY NDRSContextMarshall(NDR_SCONTEXT context, void *buffer, NDR_RUNDOWN rundown)
{
    NDRSContextMarshall2(nullptr, context, buffer, rundown, nullptr, 0);
}

/* runs down the context handles of an ended connection. */
static void contexts_rundown(SCONN *conn)
{
    for (;;) {
        SCTX *c = nullptr;
        AcquireSRWLockExclusive(&g_ctx_lock);
        for (ULONG i = 0; i < SCTX_BUCKETS && !c; i++)
            for (SCTX *t = g_ctx_table[i]; t; t = t->Next)
                if (t->Conn == conn) {
                    c = t;
                    break;
                }
        if (c)
            ctx_unlink(c);
        ReleaseSRWLockExclusive(&g_ctx_lock);
        if (!c)
            break;
        if (c->Rundown && c->UserContext) {
            __try {
                c->Rundown(c->UserContext);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        HeapFree(GetProcessHeap(), 0, c);
    }
}

/* ------------------------------------------------------------------ listening */

static void listen_automatically(void)
{
    InterlockedExchange(&g_listening, 1);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerListen(unsigned int min_threads, unsigned int max_calls, unsigned int dont_wait)
{
    if (ensure_events())
        return RPC_S_OUT_OF_RESOURCES;
    if (min_threads > max_calls && max_calls)
        return RPC_S_MAX_CALLS_TOO_SMALL;
    AcquireSRWLockShared(&g_lock);
    BOOL have = g_endpoints != nullptr;
    ReleaseSRWLockShared(&g_lock);
    if (!have)
        return RPC_S_NO_PROTSEQS_REGISTERED;
    if (InterlockedCompareExchange(&g_listening, 1, 0))
        return RPC_S_ALREADY_LISTENING;
    ResetEvent(g_stop_event);
    if (!dont_wait)
        return RpcMgmtWaitServerListen();
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtWaitServerListen(void)
{
    if (ensure_events())
        return RPC_S_OUT_OF_RESOURCES;
    if (!g_listening && WaitForSingleObject(g_stop_event, 0) != WAIT_OBJECT_0)
        return RPC_S_NOT_LISTENING;
    WaitForSingleObject(g_stop_event, INFINITE);
    WaitForSingleObject(g_idle_event, INFINITE);
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtStopServerListening(RPC_BINDING_HANDLE binding)
{
    if (binding)
        return RPC_S_BINDING_INCOMPLETE;
    if (ensure_events())
        return RPC_S_OUT_OF_RESOURCES;
    if (!InterlockedCompareExchange(&g_listening, 0, 1))
        return RPC_S_NOT_LISTENING;
    SetEvent(g_stop_event);
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtIsServerListening(RPC_BINDING_HANDLE binding)
{
    if (!binding)
        return g_listening ? RPC_S_OK : RPC_S_NOT_LISTENING;
    BINDING *b = RpcpBindingLookup(binding);
    if (!b)
        return RPC_S_INVALID_BINDING;
    /* a remote server is listening if it accepts a connection. */
    RPC_STREAM s;
    RPC_STATUS status = RpcpStreamConnect(b, &s);
    if (status)
        return status == RPC_S_SERVER_UNAVAILABLE ? RPC_S_NOT_LISTENING : status;
    RpcpStreamClose(&s);
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtSetServerStackSize(ULONG size)
{
    if (size < 4096)
        return RPC_S_INVALID_ARG;
    g_stack_size = size;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEnableIdleCleanup(void)
{
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtSetAuthorizationFn(RPC_MGMT_AUTHORIZATION_FN function)
{
    g_mgmt_authorization = function;
    return RPC_S_OK;
}

RPCRTAPI void RPC_ENTRY RpcServerYield(void)
{
    SwitchToThread();
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqStats(RPC_BINDING_HANDLE binding, RPC_STATS_VECTOR **stats)
{
    if (!stats)
        return RPC_S_INVALID_ARG;
    RPC_STATS_VECTOR *v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v) + 3 * sizeof(ULONG));
    if (!v)
        return RPC_S_OUT_OF_MEMORY;
    v->Count = 4;
    *stats = v;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtStatsVectorFree(RPC_STATS_VECTOR **stats)
{
    if (!stats || !*stats)
        return RPC_S_INVALID_ARG;
    HeapFree(GetProcessHeap(), 0, *stats);
    *stats = nullptr;
    return RPC_S_OK;
}

/* ------------------------------------------------------------------ call client */

/* the call a handle names, or the thread's current call when it names none. */
static SCALL *call_of(RPC_BINDING_HANDLE handle, RPC_STATUS *status)
{
    BINDING *b = handle ? RpcpBindingLookup(handle) : RpcpCurrentCallBinding();
    if (!b) {
        *status = handle ? RPC_S_INVALID_BINDING : RPC_S_NO_CALL_ACTIVE;
        return nullptr;
    }
    if (b->Kind != BINDING_SERVER_CALL || !b->Call) {
        *status = RPC_S_WRONG_KIND_OF_BINDING;
        return nullptr;
    }
    *status = RPC_S_OK;
    return b->Call;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcImpersonateClient(RPC_BINDING_HANDLE handle)
{
    RPC_STATUS status;
    SCALL *call = call_of(handle, &status);
    if (!call)
        return status;
    if (call->Impersonating)
        return RPC_S_OK;
    if (!RpcpAdvapi()->ImpersonateNamedPipeClient(call->Conn->Stream.Handle))
        return RPC_S_CANNOT_SUPPORT;
    call->Impersonating = TRUE;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcRevertToSelfEx(RPC_BINDING_HANDLE handle)
{
    RPC_STATUS status;
    SCALL *call = call_of(handle, &status);
    if (!call)
        return status;
    if (call->Impersonating) {
        RpcpAdvapi()->RevertToSelf();
        call->Impersonating = FALSE;
    }
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcRevertToSelf(void)
{
    return RpcRevertToSelfEx(nullptr);
}

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcBindingInqLocalClientPID(RPC_BINDING_HANDLE handle, ULONG *pid)
{
    RPC_STATUS status;
    SCALL *call = call_of(handle, &status);
    if (!call)
        return status;
    if (!pid)
        return RPC_S_INVALID_ARG;
    ULONG id = 0;
    if (!GetNamedPipeClientProcessId(call->Conn->Stream.Handle, &id))
        return RPC_S_CANNOT_SUPPORT;
    *pid = id;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcBindingIsClientLocal(RPC_BINDING_HANDLE handle, unsigned int *local)
{
    RPC_STATUS status;
    SCALL *call = call_of(handle, &status);
    if (!call)
        return status;
    if (local)
        *local = 1;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcBindingInqTransportType(RPC_BINDING_HANDLE handle, unsigned int *type)
{
    BINDING *b = handle ? RpcpBindingLookup(handle) : RpcpCurrentCallBinding();
    if (!b)
        return handle ? RPC_S_INVALID_BINDING : RPC_S_NO_CALL_ACTIVE;
    if (type)
        *type = b->Protseq && !_wcsicmp(b->Protseq, L"ncalrpc") ? TRANSPORT_TYPE_LPC : TRANSPORT_TYPE_CN;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcServerInqTransportType(unsigned int *type)
{
    return I_RpcBindingInqTransportType(nullptr, type);
}

RPCRTAPI RPC_BINDING_HANDLE RPC_ENTRY I_RpcGetCurrentCallHandle(void)
{
    return RpcpCurrentCallBinding();
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqBindingHandle(RPC_BINDING_HANDLE *binding)
{
    BINDING *b = RpcpCurrentCallBinding();
    if (!b)
        return RPC_S_NO_CALL_ACTIVE;
    *binding = b;
    return RPC_S_OK;
}

/* the calling account as "domain\user". */
static RPC_STATUS client_name(SCALL *call, WCHAR *name, ULONG count)
{
    const RPC_ADVAPI *adv = RpcpAdvapi();
    BOOL(WINAPI * lookup)(LPCWSTR, PVOID, LPWSTR, LPDWORD, LPWSTR, LPDWORD, PDWORD) = adv->LookupAccountSidW;
    RPC_STATUS result = RPC_S_CANNOT_SUPPORT;
    BOOL was = call->Impersonating;
    if (!was && !RpcpAdvapi()->ImpersonateNamedPipeClient(call->Conn->Stream.Handle))
        return result;
    HANDLE token;
    if (lookup && adv->OpenThreadToken(GetCurrentThread(), 8, TRUE, &token)) {
        unsigned char buffer[256];
        DWORD needed;
        if (adv->GetTokenInformation(token, 1, buffer, sizeof(buffer), &needed)) {
            WCHAR user[128], domain[128];
            DWORD ul = ARRAYSIZE(user), dl = ARRAYSIZE(domain), use;
            if (lookup(nullptr, *(PVOID *)buffer, user, &ul, domain, &dl, &use)) {
                name[0] = 0;
                RpcpAppend(name, count, domain);
                RpcpAppend(name, count, L"\\");
                RpcpAppend(name, count, user);
                result = RPC_S_OK;
            }
        }
        CloseHandle(token);
    }
    if (!was)
        RpcpAdvapi()->RevertToSelf();
    return result;
}

static RPC_STATUS inq_auth_client(RPC_BINDING_HANDLE client, RPC_AUTHZ_HANDLE *privs, RPC_WSTR *principal, ULONG *level,
                                  ULONG *service, ULONG *authz)
{
    RPC_STATUS status;
    SCALL *call = call_of(client, &status);
    if (!call)
        return status;
    if (call->Conn->AuthLevel <= RPC_C_AUTHN_LEVEL_NONE)
        return RPC_S_BINDING_HAS_NO_AUTH;
    if (privs) {
        WCHAR name[300];
        if (client_name(call, name, ARRAYSIZE(name)))
            *privs = nullptr;
        else {
            PWSTR copy = RpcpCopyString(name);
            if (!copy)
                return RPC_S_OUT_OF_MEMORY;
            *privs = copy;
        }
    }
    if (principal)
        *principal = nullptr;
    if (level)
        *level = call->Conn->AuthLevel;
    if (service)
        *service = call->Conn->AuthType;
    if (authz)
        *authz = RPC_C_AUTHZ_NAME;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthClientW(RPC_BINDING_HANDLE client, RPC_AUTHZ_HANDLE *privs,
                                                       RPC_WSTR *principal, ULONG *level, ULONG *service, ULONG *authz)
{
    return inq_auth_client(client, privs, principal, level, service, authz);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthClientExW(RPC_BINDING_HANDLE client, RPC_AUTHZ_HANDLE *privs,
                                                         RPC_WSTR *principal, ULONG *level, ULONG *service,
                                                         ULONG *authz, ULONG flags)
{
    return inq_auth_client(client, privs, principal, level, service, authz);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthClientA(RPC_BINDING_HANDLE client, RPC_AUTHZ_HANDLE *privs,
                                                       RPC_CSTR *principal, ULONG *level, ULONG *service, ULONG *authz)
{
    return inq_auth_client(client, privs, nullptr, level, service, authz);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthClientExA(RPC_BINDING_HANDLE client, RPC_AUTHZ_HANDLE *privs,
                                                         RPC_CSTR *principal, ULONG *level, ULONG *service,
                                                         ULONG *authz, ULONG flags)
{
    return inq_auth_client(client, privs, nullptr, level, service, authz);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqCallAttributesW(RPC_BINDING_HANDLE client, void *attributes)
{
    RPC_STATUS status;
    SCALL *call = call_of(client, &status);
    if (!call)
        return status;
    RPC_CALL_ATTRIBUTES_V2_W *a = attributes;
    if (!a || a->Version < 1 || a->Version > 3)
        return RPC_S_INVALID_ARG;
    a->AuthenticationLevel = call->Conn->AuthLevel ? call->Conn->AuthLevel : RPC_C_AUTHN_LEVEL_NONE;
    a->AuthenticationService = call->Conn->AuthType;
    a->NullSession = FALSE;
    if (a->Flags & RPC_QUERY_CLIENT_PRINCIPAL_NAME) {
        WCHAR name[300];
        if (client_name(call, name, ARRAYSIZE(name)) == RPC_S_OK) {
            ULONG need = (ULONG)(wcslen(name) + 1) * sizeof(WCHAR);
            if (a->ClientPrincipalNameBufferLength < need) {
                a->ClientPrincipalNameBufferLength = need;
                return RPC_S_BUFFER_TOO_SMALL;
            }
            memcpy(a->ClientPrincipalName, name, need);
            a->ClientPrincipalNameBufferLength = need;
        }
    }
    if (a->Version >= 2) {
        a->KernelModeCaller = FALSE;
        a->ProtocolSequence = 3;
        a->IsClientLocal = 1;
        ULONG pid = 0;
        GetNamedPipeClientProcessId(call->Conn->Stream.Handle, &pid);
        a->ClientPID = (HANDLE)(ULONG_PTR)pid;
        a->CallStatus = RPC_CALL_STATUS_IN_PROGRESS;
        a->OpNum = call->Opnum;
        a->InterfaceUuid = call->If->Iface->InterfaceId.SyntaxGUID;
    }
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqCallAttributesA(RPC_BINDING_HANDLE client, void *attributes)
{
    return RPC_S_CANNOT_SUPPORT;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingServerFromClient(RPC_BINDING_HANDLE client, RPC_BINDING_HANDLE *server)
{
    RPC_STATUS status;
    SCALL *call = call_of(client, &status);
    if (!call)
        return status;
    if (!server)
        return RPC_S_INVALID_ARG;
    BINDING *b = RpcpBindingCopy(call->Handle);
    if (!b)
        return RPC_S_OUT_OF_MEMORY;
    *server = b;
    return RPC_S_OK;
}

/* tells the serving thread whether its client cancelled the call. */
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerTestCancel(RPC_BINDING_HANDLE binding)
{
    RPC_STATUS status;
    SCALL *call = call_of(binding, &status);
    if (!call)
        return status;
    return call->Cancelled ? RPC_S_CALL_CANCELLED : RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcTestCancel(void)
{
    RPC_THREAD *t = RpcpThread();
    return t && t->Cancelled ? RPC_S_CALL_CANCELLED : RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcCancelThread(void *thread)
{
    DWORD id = GetThreadId(thread);
    return id ? RpcpThreadCancel(id) : RPC_S_INVALID_ARG;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcCancelThreadEx(void *thread, LONG timeout)
{
    return RpcCancelThread(thread);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterAuthInfoW(RPC_WSTR principal, ULONG service,
                                                         RPC_AUTH_KEY_RETRIEVAL_FN get_key, void *arg)
{
    switch (service) {
    case RPC_C_AUTHN_WINNT:
    case RPC_C_AUTHN_GSS_NEGOTIATE:
    case RPC_C_AUTHN_GSS_KERBEROS:
    case RPC_C_AUTHN_GSS_SCHANNEL:
    case RPC_C_AUTHN_DEFAULT:
        return RPC_S_OK;
    default:
        return RPC_S_UNKNOWN_AUTHN_SERVICE;
    }
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterAuthInfoA(RPC_CSTR principal, ULONG service,
                                                         RPC_AUTH_KEY_RETRIEVAL_FN get_key, void *arg)
{
    return RpcServerRegisterAuthInfoW(nullptr, service, get_key, arg);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqDefaultPrincNameW(ULONG service, RPC_WSTR *principal)
{
    if (!principal)
        return RPC_S_INVALID_ARG;
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 2];
    DWORD size = ARRAYSIZE(name);
    if (!GetComputerNameW(name, &size))
        return RPC_S_NO_PRINC_NAME;
    *principal = (RPC_WSTR)RpcpCopyString(name);
    return *principal ? RPC_S_OK : RPC_S_OUT_OF_MEMORY;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqDefaultPrincNameA(ULONG service, RPC_CSTR *principal)
{
    PWSTR wide = nullptr;
    RPC_STATUS status = RpcServerInqDefaultPrincNameW(service, (RPC_WSTR *)&wide);
    if (!status)
        *principal = RpcpNarrow(wide);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqServerPrincNameW(RPC_BINDING_HANDLE binding, ULONG service,
                                                         RPC_WSTR *principal)
{
    return RpcServerInqDefaultPrincNameW(service, principal);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqServerPrincNameA(RPC_BINDING_HANDLE binding, ULONG service,
                                                         RPC_CSTR *principal)
{
    return RpcServerInqDefaultPrincNameA(service, principal);
}
