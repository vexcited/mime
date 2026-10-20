/*
 * the endpoint mapper: a table in memory shared by the processes of the machine in which servers publish the endpoints their
 * interfaces can be reached on, and clients that name a server without an endpoint look the endpoint up. Windows keeps the
 * table in a service; here it is a section any process opens, and an entry whose process has ended is dropped by whoever
 * finds it.
 */
#include "rpcrt4_private.h"

#define EP_COUNT 256
#define EP_MAGIC 0x31505045u

typedef struct {
    ULONG Used;
    ULONG Pid;
    ULONGLONG CreationTime;
    UUID IfUuid;
    USHORT Major;
    USHORT Minor;
    UUID Object;
    WCHAR Protseq[24];
    WCHAR Endpoint[100];
    WCHAR Annotation[48];
} EP_ENTRY;

typedef struct {
    ULONG Magic;
    ULONG Generation;
    EP_ENTRY Entries[EP_COUNT];
} EP_TABLE;

static EP_TABLE *g_table;
static HANDLE g_mutex;
static SRWLOCK g_init_lock = SRWLOCK_INIT;

/* creates or opens the shared table and its lock. */
static RPC_STATUS table_open(void)
{
    if (g_table)
        return RPC_S_OK;
    AcquireSRWLockExclusive(&g_init_lock);
    if (!g_table) {
        /* everyone may use both, each with all the rights of its kind. */
        PSECURITY_DESCRIPTOR sd = RpcpEveryoneDescriptor(0x1F0001), section_sd = RpcpEveryoneDescriptor(0xF001F);
        SECURITY_ATTRIBUTES sa = {sizeof(sa), sd, FALSE}, section_sa = {sizeof(section_sa), section_sd, FALSE};
        HANDLE mutex = CreateMutexW(&sa, FALSE, L"Global\\MimeRpcEndpointMapperLock");
        HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, &section_sa, PAGE_READWRITE, 0, sizeof(EP_TABLE),
                                            L"Global\\MimeRpcEndpointMapper");
        HeapFree(GetProcessHeap(), 0, sd);
        HeapFree(GetProcessHeap(), 0, section_sd);
        EP_TABLE *table = section ? MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(EP_TABLE)) : nullptr;
        if (mutex && table) {
            g_mutex = mutex;
            g_table = table;
        } else {
            if (mutex)
                CloseHandle(mutex);
        }
        if (section)
            CloseHandle(section);
    }
    ReleaseSRWLockExclusive(&g_init_lock);
    return g_table ? RPC_S_OK : RPC_S_OUT_OF_RESOURCES;
}

static void table_lock(void)
{
    WaitForSingleObject(g_mutex, INFINITE);
    if (g_table->Magic != EP_MAGIC) {
        memset(g_table, 0, sizeof(*g_table));
        g_table->Magic = EP_MAGIC;
    }
}

static void table_unlock(void)
{
    ReleaseMutex(g_mutex);
}

static ULONGLONG process_creation_time(DWORD pid)
{
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p)
        return 0;
    FILETIME create, exit, kernel, user;
    ULONGLONG t = 0;
    if (GetProcessTimes(p, &create, &exit, &kernel, &user) && exit.dwHighDateTime == 0 && exit.dwLowDateTime == 0)
        t = ((ULONGLONG)create.dwHighDateTime << 32) | create.dwLowDateTime;
    CloseHandle(p);
    return t;
}

/* does the process that made an entry still run. */
static BOOL entry_alive(const EP_ENTRY *e)
{
    return e->Pid == GetCurrentProcessId() || process_creation_time(e->Pid) == e->CreationTime;
}

static BOOL same_if(const EP_ENTRY *e, const RPC_SYNTAX_IDENTIFIER *id, ULONG vers_option)
{
    if (memcmp(&e->IfUuid, &id->SyntaxGUID, sizeof(UUID)))
        return FALSE;
    switch (vers_option) {
    case RPC_C_VERS_ALL:
        return TRUE;
    case RPC_C_VERS_MAJOR_ONLY:
        return e->Major == id->SyntaxVersion.MajorVersion;
    case RPC_C_VERS_EXACT:
        return e->Major == id->SyntaxVersion.MajorVersion && e->Minor == id->SyntaxVersion.MinorVersion;
    case RPC_C_VERS_UPTO:
        return e->Major < id->SyntaxVersion.MajorVersion ||
               (e->Major == id->SyntaxVersion.MajorVersion && e->Minor <= id->SyntaxVersion.MinorVersion);
    default:
        /* compatible: same major, and a server at least as new as the client. */
        return e->Major == id->SyntaxVersion.MajorVersion && e->Minor >= id->SyntaxVersion.MinorVersion;
    }
}

/* the endpoint a server published for an interface; false if there is none. */
RPC_STATUS RpcpEpResolve(BINDING *b, const RPC_SYNTAX_IDENTIFIER *interface_id)
{
    RPC_STATUS status = table_open();
    if (status)
        return EPT_S_NOT_REGISTERED;
    status = EPT_S_NOT_REGISTERED;
    table_lock();
    for (ULONG i = 0; i < EP_COUNT; i++) {
        EP_ENTRY *e = &g_table->Entries[i];
        if (!e->Used)
            continue;
        if (!entry_alive(e)) {
            e->Used = 0;
            continue;
        }
        if (!same_if(e, interface_id, RPC_C_VERS_COMPATIBLE))
            continue;
        if (_wcsicmp(e->Protseq, b->Protseq))
            continue;
        if (!RpcpIsNil(&b->Object) && memcmp(&b->Object, &e->Object, sizeof(UUID)) && !RpcpIsNil(&e->Object))
            continue;
        PWSTR endpoint = RpcpCopyString(e->Endpoint);
        if (endpoint) {
            RpcpFreeString(b->Endpoint);
            b->Endpoint = endpoint;
            status = RPC_S_OK;
        }
        break;
    }
    table_unlock();
    return status;
}

/* removes the entries of a process. */
void RpcpEpPurge(DWORD pid)
{
    if (!g_table)
        return;
    table_lock();
    for (ULONG i = 0; i < EP_COUNT; i++)
        if (g_table->Entries[i].Used && g_table->Entries[i].Pid == pid)
            g_table->Entries[i].Used = 0;
    table_unlock();
}

static RPC_STATUS ep_register(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings, UUID_VECTOR *objects,
                              PCWSTR annotation, BOOL replace)
{
    const RPC_CLIENT_INTERFACE *iface = interface_spec;
    if (!iface || !bindings)
        return RPC_S_INVALID_ARG;
    RPC_STATUS status = table_open();
    if (status)
        return status;
    static const UUID nil;
    ULONG object_count = objects && objects->Count ? objects->Count : 1;
    ULONGLONG created = process_creation_time(GetCurrentProcessId());
    table_lock();
    for (ULONG bi = 0; bi < bindings->Count && !status; bi++) {
        BINDING *b = RpcpBindingLookup(bindings->BindingH[bi]);
        if (!b) {
            status = RPC_S_INVALID_BINDING;
            break;
        }
        for (ULONG oi = 0; oi < object_count; oi++) {
            const UUID *object = objects && objects->Count ? objects->Uuid[oi] : &nil;
            EP_ENTRY *slot = nullptr;
            for (ULONG i = 0; i < EP_COUNT; i++) {
                EP_ENTRY *e = &g_table->Entries[i];
                if (!e->Used) {
                    if (!slot)
                        slot = e;
                    continue;
                }
                if (!entry_alive(e)) {
                    e->Used = 0;
                    if (!slot)
                        slot = e;
                    continue;
                }
                if (replace && same_if(e, &iface->InterfaceId, RPC_C_VERS_EXACT) &&
                    !memcmp(&e->Object, object, sizeof(UUID)) && !_wcsicmp(e->Protseq, b->Protseq) &&
                    !_wcsicmp(e->Endpoint, b->Endpoint)) {
                    slot = e;
                    break;
                }
            }
            if (!slot) {
                status = RPC_S_OUT_OF_RESOURCES;
                break;
            }
            memset(slot, 0, sizeof(*slot));
            slot->Pid = GetCurrentProcessId();
            slot->CreationTime = created;
            slot->IfUuid = iface->InterfaceId.SyntaxGUID;
            slot->Major = iface->InterfaceId.SyntaxVersion.MajorVersion;
            slot->Minor = iface->InterfaceId.SyntaxVersion.MinorVersion;
            slot->Object = *object;
            RpcpCopy(slot->Protseq, ARRAYSIZE(slot->Protseq), b->Protseq);
            RpcpCopy(slot->Endpoint, ARRAYSIZE(slot->Endpoint), b->Endpoint ? b->Endpoint : L"");
            if (annotation)
                RpcpCopy(slot->Annotation, ARRAYSIZE(slot->Annotation), annotation);
            slot->Used = 1;
        }
    }
    table_unlock();
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpRegisterW(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings,
                                             UUID_VECTOR *objects, RPC_WSTR annotation)
{
    return ep_register(interface_spec, bindings, objects, (PCWSTR)annotation, TRUE);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpRegisterNoReplaceW(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings,
                                                      UUID_VECTOR *objects, RPC_WSTR annotation)
{
    return ep_register(interface_spec, bindings, objects, (PCWSTR)annotation, FALSE);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpRegisterA(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings,
                                             UUID_VECTOR *objects, RPC_CSTR annotation)
{
    PWSTR wide = RpcpWiden(annotation);
    RPC_STATUS status = ep_register(interface_spec, bindings, objects, wide, TRUE);
    RpcpFreeString(wide);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpRegisterNoReplaceA(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings,
                                                      UUID_VECTOR *objects, RPC_CSTR annotation)
{
    PWSTR wide = RpcpWiden(annotation);
    RPC_STATUS status = ep_register(interface_spec, bindings, objects, wide, FALSE);
    RpcpFreeString(wide);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpUnregister(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings,
                                              UUID_VECTOR *objects)
{
    const RPC_CLIENT_INTERFACE *iface = interface_spec;
    if (!iface || !bindings)
        return RPC_S_INVALID_ARG;
    RPC_STATUS status = table_open();
    if (status)
        return status;
    static const UUID nil;
    ULONG object_count = objects && objects->Count ? objects->Count : 1;
    BOOL removed = FALSE;
    table_lock();
    for (ULONG bi = 0; bi < bindings->Count; bi++) {
        BINDING *b = RpcpBindingLookup(bindings->BindingH[bi]);
        if (!b)
            continue;
        for (ULONG oi = 0; oi < object_count; oi++) {
            const UUID *object = objects && objects->Count ? objects->Uuid[oi] : &nil;
            for (ULONG i = 0; i < EP_COUNT; i++) {
                EP_ENTRY *e = &g_table->Entries[i];
                if (e->Used && e->Pid == GetCurrentProcessId() && same_if(e, &iface->InterfaceId, RPC_C_VERS_EXACT) &&
                    !memcmp(&e->Object, object, sizeof(UUID)) && !_wcsicmp(e->Protseq, b->Protseq) &&
                    !_wcsicmp(e->Endpoint, b->Endpoint)) {
                    e->Used = 0;
                    removed = TRUE;
                }
            }
        }
    }
    table_unlock();
    return removed ? RPC_S_OK : EPT_S_NOT_REGISTERED;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpResolveBinding(RPC_BINDING_HANDLE binding, RPC_IF_HANDLE interface_spec)
{
    BINDING *b = RpcpBindingLookup(binding);
    const RPC_CLIENT_INTERFACE *iface = interface_spec;
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (!iface)
        return RPC_S_INVALID_ARG;
    if (b->Endpoint && *b->Endpoint)
        return RPC_S_OK;
    return RpcpEpResolve(b, &iface->InterfaceId);
}

/* ------------------------------------------------------------------ inquiry */

typedef struct {
    ULONG Magic;
    ULONG Type;
    RPC_IF_ID Id;
    ULONG VersOption;
    UUID Object;
    ULONG Next;
} EP_INQUIRY;

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEpEltInqBegin(RPC_BINDING_HANDLE ep_binding, ULONG inquiry_type, RPC_IF_ID *id,
                                                   ULONG version_option, UUID *object, RPC_EP_INQ_HANDLE *context)
{
    if (!context)
        return RPC_S_INVALID_ARG;
    if (inquiry_type > RPC_C_EP_MATCH_BY_BOTH)
        return RPC_S_INVALID_ARG;
    if (table_open())
        return RPC_S_OUT_OF_RESOURCES;
    EP_INQUIRY *q = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*q));
    if (!q)
        return RPC_S_OUT_OF_MEMORY;
    q->Magic = EP_MAGIC;
    q->Type = inquiry_type;
    if (id)
        q->Id = *id;
    q->VersOption = version_option ? version_option : RPC_C_VERS_ALL;
    if (object)
        q->Object = *object;
    *context = (RPC_EP_INQ_HANDLE)q;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEpEltInqDone(RPC_EP_INQ_HANDLE *context)
{
    if (!context || !*context)
        return RPC_S_INVALID_ARG;
    HeapFree(GetProcessHeap(), 0, *context);
    *context = nullptr;
    return RPC_S_OK;
}

static RPC_STATUS inq_next(RPC_EP_INQ_HANDLE context, RPC_IF_ID *id, RPC_BINDING_HANDLE *binding, UUID *object,
                           PWSTR *annotation)
{
    EP_INQUIRY *q = (EP_INQUIRY *)context;
    if (!q || q->Magic != EP_MAGIC)
        return RPC_S_INVALID_ARG;
    RPC_STATUS status = RPC_X_NO_MORE_ENTRIES;
    table_lock();
    for (; q->Next < EP_COUNT; q->Next++) {
        EP_ENTRY *e = &g_table->Entries[q->Next];
        if (!e->Used)
            continue;
        if (!entry_alive(e)) {
            e->Used = 0;
            continue;
        }
        RPC_SYNTAX_IDENTIFIER want = {q->Id.Uuid, {q->Id.VersMajor, q->Id.VersMinor}};
        if ((q->Type == RPC_C_EP_MATCH_BY_IF || q->Type == RPC_C_EP_MATCH_BY_BOTH) &&
            !same_if(e, &want, q->VersOption))
            continue;
        if ((q->Type == RPC_C_EP_MATCH_BY_OBJ || q->Type == RPC_C_EP_MATCH_BY_BOTH) &&
            memcmp(&e->Object, &q->Object, sizeof(UUID)))
            continue;
        if (id) {
            id->Uuid = e->IfUuid;
            id->VersMajor = e->Major;
            id->VersMinor = e->Minor;
        }
        if (object)
            *object = e->Object;
        if (binding) {
            BINDING *b = RpcpBindingNew(BINDING_CLIENT);
            if (!b) {
                status = RPC_S_OUT_OF_MEMORY;
                break;
            }
            b->Protseq = RpcpCopyString(e->Protseq);
            b->Address = RpcpCopyString(L"");
            b->Endpoint = RpcpCopyString(e->Endpoint);
            b->Options = RpcpCopyString(L"");
            b->Object = e->Object;
            *binding = b;
        }
        if (annotation)
            *annotation = RpcpCopyString(e->Annotation);
        q->Next++;
        status = RPC_S_OK;
        break;
    }
    table_unlock();
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEpEltInqNextW(RPC_EP_INQ_HANDLE context, RPC_IF_ID *id, RPC_BINDING_HANDLE *binding,
                                                   UUID *object, RPC_WSTR *annotation)
{
    return inq_next(context, id, binding, object, (PWSTR *)annotation);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEpEltInqNextA(RPC_EP_INQ_HANDLE context, RPC_IF_ID *id, RPC_BINDING_HANDLE *binding,
                                                   UUID *object, RPC_CSTR *annotation)
{
    PWSTR wide = nullptr;
    RPC_STATUS status = inq_next(context, id, binding, object, annotation ? &wide : nullptr);
    if (!status && annotation)
        *annotation = RpcpNarrow(wide);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEpUnregister(RPC_BINDING_HANDLE ep_binding, RPC_IF_ID *id,
                                                  RPC_BINDING_HANDLE binding, UUID *object)
{
    BINDING *b = RpcpBindingLookup(binding);
    if (!id || !b)
        return RPC_S_INVALID_ARG;
    if (table_open())
        return RPC_S_OUT_OF_RESOURCES;
    BOOL removed = FALSE;
    RPC_SYNTAX_IDENTIFIER want = {id->Uuid, {id->VersMajor, id->VersMinor}};
    table_lock();
    for (ULONG i = 0; i < EP_COUNT; i++) {
        EP_ENTRY *e = &g_table->Entries[i];
        if (e->Used && same_if(e, &want, RPC_C_VERS_EXACT) && !_wcsicmp(e->Protseq, b->Protseq) &&
            !_wcsicmp(e->Endpoint, b->Endpoint) && (!object || !memcmp(&e->Object, object, sizeof(UUID)))) {
            e->Used = 0;
            removed = TRUE;
        }
    }
    table_unlock();
    return removed ? RPC_S_OK : EPT_S_NOT_REGISTERED;
}
