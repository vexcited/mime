#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/rpc.h>
#include "rpcepmap/epmp.h"

#define EPT_S_NOT_REGISTERED 0x16c9a0d6
#define EPT_S_INVALID_ENTRY 0x16c9a0d5
#define EPT_S_CANT_PERFORM_OP 0x16c9a0d4

#define LOOKUP_MAGIC 0x4b4f4c45 /* "ELOK" */
#define OWNER_MAGIC 0x4e574f45  /* "EOWN" */

/* one registered endpoint: the entry a server sent, and the parts of its tower lookups compare. */
typedef struct EP_ENTRY {
    struct EP_ENTRY *Next;
    void *Owner;
    GUID Object;
    GUID Interface;
    USHORT Major, Minor;
    UCHAR Protocol[8]; /* the left sides of the floors from the third to the last but one. */
    ULONG ProtocolSize;
    char Annotation[64];
    ULONG TowerSize;
    BYTE *Tower;
} EP_ENTRY;

typedef struct EP_OWNER {
    DWORD Magic;
} EP_OWNER;

/* where a lookup that did not fit in one call goes on: the number of matches it already returned. */
typedef struct EP_LOOKUP {
    DWORD Magic;
    ULONG Done;
} EP_LOOKUP;

static SRWLOCK g_lock = SRWLOCK_INIT;
static EP_ENTRY *g_entries;
static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;

void *WINAPI MIDL_user_allocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void WINAPI MIDL_user_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

static const GUID g_nil;

static BOOL guid_equal(const GUID *a, const GUID *b)
{
    return !memcmp(a, b, sizeof(GUID));
}

/*
 * a tower is a floor count, then the floors, each a left side, the protocol and its data, and a right side, both
 * preceded by their sizes. the first floor names the interface: 0x0d, its uuid and major version on the left, the
 * minor version on the right. the second the transfer syntax the same way, then the protocols, then the endpoint.
 */
typedef struct TOWER_FLOOR {
    const BYTE *Lhs, *Rhs;
    USHORT LhsSize, RhsSize;
} TOWER_FLOOR;

static ULONG tower_floors(const BYTE *tower, ULONG size, TOWER_FLOOR *floors, ULONG max)
{
    if (size < 2)
        return 0;
    ULONG count = tower[0] | tower[1] << 8, at = 2;
    if (count > max)
        return 0;
    for (ULONG i = 0; i < count; i++) {
        if (at + 2 > size)
            return 0;
        floors[i].LhsSize = tower[at] | tower[at + 1] << 8;
        floors[i].Lhs = tower + at + 2;
        at += 2 + floors[i].LhsSize;
        if (at + 2 > size)
            return 0;
        floors[i].RhsSize = tower[at] | tower[at + 1] << 8;
        floors[i].Rhs = tower + at + 2;
        at += 2 + floors[i].RhsSize;
        if (at > size)
            return 0;
    }
    return count;
}

/* the interface and protocol of a tower; false when it is not one of an interface. */
static BOOL tower_parse(const BYTE *tower, ULONG size, GUID *iface, USHORT *major, USHORT *minor, UCHAR *protocol,
                        ULONG *protocol_size)
{
    TOWER_FLOOR floors[8];
    ULONG count = tower_floors(tower, size, floors, 8);
    if (count < 4 || floors[0].LhsSize != 19 || floors[0].Lhs[0] != 0x0d || floors[0].RhsSize != 2)
        return FALSE;
    memcpy(iface, floors[0].Lhs + 1, sizeof(GUID));
    *major = floors[0].Lhs[17] | floors[0].Lhs[18] << 8;
    *minor = floors[0].Rhs[0] | floors[0].Rhs[1] << 8;
    *protocol_size = 0;
    for (ULONG i = 2; i + 1 < count && *protocol_size < 8; i++)
        if (floors[i].LhsSize)
            protocol[(*protocol_size)++] = floors[i].Lhs[0];
    return TRUE;
}

static void entry_free(EP_ENTRY *e)
{
    MIDL_user_free(e->Tower);
    MIDL_user_free(e);
}

static BOOL same_endpoint(const EP_ENTRY *e, const GUID *object, const BYTE *tower, ULONG size)
{
    return guid_equal(&e->Object, object) && e->TowerSize == size && !memcmp(e->Tower, tower, size);
}

/* drops the entries of an owner that match the interface, object and protocols of x, as registering with replace does. */
static void remove_replaced(const EP_ENTRY *x)
{
    for (EP_ENTRY **p = &g_entries; *p;) {
        EP_ENTRY *e = *p;
        if (e->Owner == x->Owner && guid_equal(&e->Object, &x->Object) && guid_equal(&e->Interface, &x->Interface) &&
            e->Major == x->Major && e->ProtocolSize == x->ProtocolSize &&
            !memcmp(e->Protocol, x->Protocol, x->ProtocolSize)) {
            *p = e->Next;
            entry_free(e);
        } else {
            p = &e->Next;
        }
    }
}

static void remove_owner(void *owner)
{
    AcquireSRWLockExclusive(&g_lock);
    for (EP_ENTRY **p = &g_entries; *p;) {
        EP_ENTRY *e = *p;
        if (e->Owner == owner) {
            *p = e->Next;
            entry_free(e);
        } else {
            p = &e->Next;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

static twr_t *tower_copy(const BYTE *data, ULONG size)
{
    twr_t *t = MIDL_user_allocate(FIELD_OFFSET(twr_t, tower_octet_string) + size);
    if (!t)
        return nullptr;
    t->tower_length = size;
    memcpy(t->tower_octet_string, data, size);
    return t;
}

/* ------------------------------------------------------------------ localepmp, the servers' side */

error_status_t WINAPI localepmp_epm_open_s(handle_t h, epm_registration_handle_t *handle)
{
    EP_OWNER *o = MIDL_user_allocate(sizeof(*o));
    if (!o)
        return RPC_S_OUT_OF_MEMORY;
    o->Magic = OWNER_MAGIC;
    *handle = o;
    return RPC_S_OK;
}

static EP_OWNER *owner_of(void *handle)
{
    EP_OWNER *o = handle;
    return o && o->Magic == OWNER_MAGIC ? o : nullptr;
}

void WINAPI epm_registration_handle_t_rundown(void *handle)
{
    EP_OWNER *o = owner_of(handle);
    if (!o)
        return;
    remove_owner(o);
    o->Magic = 0;
    MIDL_user_free(o);
}

void WINAPI localepmp_epm_register_s(handle_t h, epm_registration_handle_t *handle, unsigned long count,
                                     ept_entry_t *entries, unsigned long replace, unsigned long flags, long long context,
                                     unsigned long options, error_status_t *status)
{
    *status = RPC_S_OK;
    if (!*handle) {
        *status = localepmp_epm_open_s(h, handle);
        if (*status)
            return;
    }
    EP_OWNER *o = owner_of(*handle);
    if (!o) {
        *status = EPT_S_CANT_PERFORM_OP;
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    for (ULONG i = 0; i < count; i++) {
        const twr_t *t = entries[i].tower;
        EP_ENTRY *e = MIDL_user_allocate(sizeof(*e));
        if (!e) {
            *status = RPC_S_OUT_OF_MEMORY;
            break;
        }
        memset(e, 0, sizeof(*e));
        if (!t || !tower_parse(t->tower_octet_string, t->tower_length, &e->Interface, &e->Major, &e->Minor,
                               e->Protocol, &e->ProtocolSize) ||
            !(e->Tower = MIDL_user_allocate(t->tower_length))) {
            MIDL_user_free(e);
            *status = EPT_S_INVALID_ENTRY;
            break;
        }
        e->Owner = o;
        e->Object = entries[i].object;
        e->TowerSize = t->tower_length;
        memcpy(e->Tower, t->tower_octet_string, t->tower_length);
        memcpy(e->Annotation, entries[i].annotation, sizeof(e->Annotation));
        e->Annotation[63] = 0;
        if (replace)
            remove_replaced(e);
        e->Next = g_entries;
        g_entries = e;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void WINAPI localepmp_epm_unregister_s(handle_t h, epm_registration_handle_t *handle, unsigned long count,
                                       ept_entry_t *entries, unsigned long flags, error_status_t *status)
{
    *status = EPT_S_NOT_REGISTERED;
    AcquireSRWLockExclusive(&g_lock);
    for (ULONG i = 0; i < count; i++) {
        const twr_t *t = entries[i].tower;
        if (!t)
            continue;
        for (EP_ENTRY **p = &g_entries; *p;) {
            EP_ENTRY *e = *p;
            if (same_endpoint(e, &entries[i].object, t->tower_octet_string, t->tower_length)) {
                *p = e->Next;
                entry_free(e);
                *status = RPC_S_OK;
            } else {
                p = &e->Next;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void WINAPI localepmp_epm_query_s(handle_t h)
{
}

void WINAPI localepmp_epm_reraise_triggers_s(handle_t h, unsigned long flags)
{
}

void WINAPI localepmp_epm_reserved_s(handle_t h)
{
}

/* ------------------------------------------------------------------ epm, the clients' side */

static EP_LOOKUP *lookup_of(void *handle)
{
    EP_LOOKUP *l = handle;
    return l && l->Magic == LOOKUP_MAGIC ? l : nullptr;
}

static void lookup_close(ept_lookup_handle_t *handle)
{
    EP_LOOKUP *l = lookup_of(*handle);
    if (l) {
        l->Magic = 0;
        MIDL_user_free(l);
    }
    *handle = nullptr;
}

void WINAPI ept_lookup_handle_t_rundown(void *handle)
{
    lookup_close(&handle);
}

/* keeps the place of a lookup that has more to give, or ends it. */
static void lookup_continue(ept_lookup_handle_t *handle, ULONG done, BOOL more)
{
    EP_LOOKUP *l = lookup_of(*handle);
    if (!more) {
        lookup_close(handle);
        return;
    }
    if (!l && !(l = MIDL_user_allocate(sizeof(*l))))
        return;
    l->Magic = LOOKUP_MAGIC;
    l->Done = done;
    *handle = l;
}

/*
 * the entries of an interface with a compatible version and the same protocols. an object other than nil looks for the
 * entries of that object first and falls back to those registered for any object.
 */
static BOOL map_matches(const EP_ENTRY *e, const GUID *object, BOOL exact_object, const GUID *iface, USHORT major,
                        USHORT minor, const UCHAR *protocol, ULONG protocol_size)
{
    if (!guid_equal(&e->Interface, iface) || e->Major != major || e->Minor < minor)
        return FALSE;
    if (e->ProtocolSize != protocol_size || memcmp(e->Protocol, protocol, protocol_size))
        return FALSE;
    return guid_equal(&e->Object, exact_object ? object : &g_nil);
}

static void map(GUID *object, twr_t *map_tower, ept_lookup_handle_t *entry_handle, unsigned long max_towers,
                unsigned long *num_towers, twr_t **towers, error_status_t *status)
{
    *num_towers = 0;
    GUID iface;
    USHORT major, minor;
    UCHAR protocol[8];
    ULONG protocol_size;
    if (!map_tower || !tower_parse(map_tower->tower_octet_string, map_tower->tower_length, &iface, &major, &minor,
                                   protocol, &protocol_size)) {
        *status = EPT_S_NOT_REGISTERED;
        lookup_close(entry_handle);
        return;
    }
    EP_LOOKUP *l = lookup_of(*entry_handle);
    ULONG skip = l ? l->Done : 0, seen = 0;
    BOOL more = FALSE;
    const GUID *obj = object ? object : &g_nil;
    BOOL by_object = !guid_equal(obj, &g_nil);
    AcquireSRWLockShared(&g_lock);
    if (by_object) {
        BOOL any = FALSE;
        for (EP_ENTRY *e = g_entries; e && !any; e = e->Next)
            any = map_matches(e, obj, TRUE, &iface, major, minor, protocol, protocol_size);
        by_object = any;
    }
    for (EP_ENTRY *e = g_entries; e; e = e->Next) {
        if (!map_matches(e, obj, by_object, &iface, major, minor, protocol, protocol_size))
            continue;
        if (seen++ < skip)
            continue;
        if (*num_towers == max_towers) {
            more = TRUE;
            break;
        }
        if (!(towers[*num_towers] = tower_copy(e->Tower, e->TowerSize)))
            break;
        (*num_towers)++;
    }
    ReleaseSRWLockShared(&g_lock);
    *status = *num_towers ? RPC_S_OK : EPT_S_NOT_REGISTERED;
    lookup_continue(entry_handle, skip + *num_towers, more);
}

void WINAPI epm_ept_map_s(handle_t h, GUID *object, twr_t *map_tower, ept_lookup_handle_t *entry_handle,
                          unsigned long max_towers, unsigned long *num_towers, twr_t **towers, error_status_t *status)
{
    map(object, map_tower, entry_handle, max_towers, num_towers, towers, status);
}

/*
 * the sid is the account the client wants the server to run as. windows maps only the endpoints a process of that account
 * registered; every endpoint here is local and mime does not keep the account of the registering process yet.
 */
void WINAPI epm_ept_map_auth_s(handle_t h, GUID *object, twr_t *map_tower, EPM_SID *sid,
                               ept_lookup_handle_t *entry_handle, unsigned long max_towers, unsigned long *num_towers,
                               twr_t **towers, error_status_t *status)
{
    map(object, map_tower, entry_handle, max_towers, num_towers, towers, status);
}

void WINAPI epm_ept_map_auth_async_s(handle_t h, GUID *object, twr_t *map_tower, EPM_SID *sid,
                                     ept_lookup_handle_t *entry_handle, unsigned long max_towers,
                                     unsigned long *num_towers, twr_t **towers, error_status_t *status)
{
    map(object, map_tower, entry_handle, max_towers, num_towers, towers, status);
}

#define RPC_C_EP_ALL_ELTS 0
#define RPC_C_EP_MATCH_BY_IF 1
#define RPC_C_EP_MATCH_BY_OBJ 2
#define RPC_C_EP_MATCH_BY_BOTH 3
#define RPC_C_VERS_ALL 1
#define RPC_C_VERS_COMPATIBLE 2
#define RPC_C_VERS_EXACT 3
#define RPC_C_VERS_MAJOR_ONLY 4
#define RPC_C_VERS_UPTO 5

static BOOL lookup_matches(const EP_ENTRY *e, ULONG inquiry, const GUID *object, const RPC_IF_ID_WIRE *id, ULONG vers)
{
    if ((inquiry == RPC_C_EP_MATCH_BY_OBJ || inquiry == RPC_C_EP_MATCH_BY_BOTH) &&
        !guid_equal(&e->Object, object ? object : &g_nil))
        return FALSE;
    if (inquiry != RPC_C_EP_MATCH_BY_IF && inquiry != RPC_C_EP_MATCH_BY_BOTH)
        return TRUE;
    if (!id || !guid_equal(&e->Interface, &id->Uuid))
        return FALSE;
    switch (vers) {
    case RPC_C_VERS_ALL:
        return TRUE;
    case RPC_C_VERS_COMPATIBLE:
        return e->Major == id->VersMajor && e->Minor >= id->VersMinor;
    case RPC_C_VERS_EXACT:
        return e->Major == id->VersMajor && e->Minor == id->VersMinor;
    case RPC_C_VERS_MAJOR_ONLY:
        return e->Major == id->VersMajor;
    case RPC_C_VERS_UPTO:
        return e->Major < id->VersMajor || (e->Major == id->VersMajor && e->Minor <= id->VersMinor);
    }
    return FALSE;
}

void WINAPI epm_ept_lookup_s(handle_t h, unsigned long inquiry_type, GUID *object, RPC_IF_ID_WIRE *interface_id,
                             unsigned long vers_option, ept_lookup_handle_t *entry_handle, unsigned long max_ents,
                             unsigned long *num_ents, ept_entry_t *entries, error_status_t *status)
{
    *num_ents = 0;
    EP_LOOKUP *l = lookup_of(*entry_handle);
    ULONG skip = l ? l->Done : 0, seen = 0;
    BOOL more = FALSE;
    AcquireSRWLockShared(&g_lock);
    for (EP_ENTRY *e = g_entries; e; e = e->Next) {
        if (!lookup_matches(e, inquiry_type, object, interface_id, vers_option))
            continue;
        if (seen++ < skip)
            continue;
        if (*num_ents == max_ents) {
            more = TRUE;
            break;
        }
        ept_entry_t *out = &entries[*num_ents];
        if (!(out->tower = tower_copy(e->Tower, e->TowerSize)))
            break;
        out->object = e->Object;
        memcpy(out->annotation, e->Annotation, sizeof(out->annotation));
        (*num_ents)++;
    }
    ReleaseSRWLockShared(&g_lock);
    *status = *num_ents ? RPC_S_OK : EPT_S_NOT_REGISTERED;
    lookup_continue(entry_handle, skip + *num_ents, more);
}

void WINAPI epm_ept_lookup_handle_free_s(handle_t h, ept_lookup_handle_t *entry_handle, error_status_t *status)
{
    lookup_close(entry_handle);
    *status = RPC_S_OK;
}

void WINAPI epm_ept_insert_s(handle_t h)
{
}

void WINAPI epm_ept_delete_s(handle_t h)
{
}

void WINAPI epm_ept_inq_object_s(handle_t h)
{
}

void WINAPI epm_ept_mgmt_delete_s(handle_t h)
{
}

/* ------------------------------------------------------------------ the service */

static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface, void *context)
{
    return RPC_S_OK;
}

/* D:(A;;0x1;;;WD)(A;;0x1;;;AC)(A;;0x1;;;S-1-15-2-2): every local caller connects, app containers included. */
static const BYTE g_endpoint_sd[] = {
    0x01, 0x00, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x00, 0x00,
    /* everyone */
    0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00,
    /* all application packages */
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    /* all restricted application packages */
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
};

static DWORD start_rpc(void)
{
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
                                               (RPC_WSTR)L"epmapper", (void *)g_endpoint_sd);
    if (status == RPC_S_DUPLICATE_ENDPOINT)
        status = RPC_S_OK;
    if (!status)
        status = RpcServerRegisterIf3(epm_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                      RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, allow_local,
                                      (void *)g_endpoint_sd);
    if (!status)
        status = RpcServerRegisterIf3(localepmp_s_ifspec, nullptr, nullptr,
                                      RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY, RPC_C_LISTEN_MAX_CALLS_DEFAULT,
                                      (unsigned int)-1, allow_local, (void *)g_endpoint_sd);
    return status;
}

static DWORD WINAPI control_handler(DWORD control, DWORD type, LPVOID data, LPVOID context)
{
    if (control != SERVICE_CONTROL_INTERROGATE)
        return ERROR_CALL_NOT_IMPLEMENTED;
    SetServiceStatus(g_handle, &g_status);
    return NO_ERROR;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* the endpoint mapper cannot be stopped, as on windows: it accepts no control. */
void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    if (!argc || !(g_handle = RegisterServiceCtrlHandlerExW(argv[0], control_handler, nullptr)))
        return;
    g_status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    DWORD error = start_rpc();
    if (error) {
        g_status.dwCurrentState = SERVICE_STOPPED;
        g_status.dwWin32ExitCode = error;
    } else {
        g_status.dwCurrentState = SERVICE_RUNNING;
    }
    SetServiceStatus(g_handle, &g_status);
}
