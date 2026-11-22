#include "rpcss.h"
#include <mime/win32/iphlpapi.h>

#include "rpcss/lclor.h"

/* the resolver's statuses, winerror's OR_ codes. */
#define OR_INVALID_OXID 1910
#define OR_INVALID_OID 1911
#define OR_INVALID_SET 1912

/* the bits of OXID_INFO.dwFlags rpcss checks: which apartment the oxid belongs to. */
#define OXIDF_STA 0x00800000
#define OXIDF_MTA 0x01000000
#define OXIDF_PRIMARY 0x02000000
#define OXIDF_NTA 0x04000000
#define OXIDF_WINRT 0x08000000
#define OXIDF_WINRT_EXTRA 0x10000000
#define OXIDF_KINDS 0x0F800000

typedef struct RS_PROCESS RS_PROCESS;

typedef struct RS_OXID {
    struct RS_OXID *Next;
    UINT64 Id;
    RS_PROCESS *Owner;
    OXID_INFO Info; /* a deep copy without psa: a resolution answers with the owner's bindings. */
    ULONG OidCount, OidCapacity;
    UINT64 *Oids;
} RS_OXID;

/* an oid a client process holds a reference to, with the oxid that exports it. */
typedef struct RS_REF {
    struct RS_REF *Next;
    UINT64 Oxid, Oid;
} RS_REF;

struct RS_PROCESS {
    DWORD Magic;
    LONG Refs;
    struct RS_PROCESS *Next;
    DWORD Pid;
    DWORD Flags;
    USHORT Architecture;
    GUID Identifier;
    UINT64 Signature;
    GUID AppId;
    WCHAR *Desktop, *Exe;
    DUALSTRINGARRAY *Bindings; /* the strings of the process's endpoints and its security bindings, as combase gave them. */
    RS_OXID *Primary;
    RS_REF *ClientRefs;
};

#define PROCESS_MAGIC 0x43505352 /* "RSPC" */

/* the period combase pings at, in seconds, as rpcss of 26300 answers Connect. */
#define BASE_TIMEOUT 360
#define MAX_RESERVED_IDS 10

/* the connect flags: network dcom on, server exceptions caught, call failures logged at level 2. */
#define CONNECT_NETWORK_DCOM 0x1
#define CONNECT_CATCH_EXCEPTIONS 0x10
#define CONNECT_FAILURE_LOGGING 0x40

#define TOWER_TCP 0x07

static SRWLOCK g_rs_lock = SRWLOCK_INIT;
static RS_PROCESS *g_processes;
static RS_OXID *g_oxids;
static UINT64 g_local_mid;
static LONG g_next_thread_id = 1;
static DUALSTRINGARRAY *g_my_bindings;
static UINT64 g_bindings_id;
static BOOL g_network_dcom = TRUE;
static DWORD g_imp_level = RPC_C_IMP_LEVEL_IDENTIFY, g_authn_level = RPC_C_AUTHN_LEVEL_CONNECT;

/* the security packages rpcss offers and accepts, with their authentication service numbers, as windows lists them. */
static const struct {
    USHORT Id;
    const WCHAR *Name;
} g_packages[] = {
    {9, L"Negotiate"}, {0x1e, L"NegoExtender"}, {0x10, L"Kerberos"}, {0xa, L"NTLM"},
    {0x16, L"TSSSP"},  {0x1f, L"pku2u"},        {0xe, L"Schannel"},
};

/*
 * the descriptors Connect hands a process, in rpcss's order: the launch and access permissions rpcss uses when the
 * registry has none, then the machine launch and access restrictions and the default access restrictions, which come
 * from HKLM\SOFTWARE\Microsoft\Ole. the first two are the bytes rpcss of 26300 answers.
 */
static const BYTE g_noreg_launch_permissions[] = {
    0x01, 0x00, 0x04, 0x80, 0x5c, 0x00, 0x00, 0x00, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x48, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x1f, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x12, 0x00,
    0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00, 0x20, 0x02, 0x00, 0x00,
    0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00, 0x20, 0x02, 0x00, 0x00,
};

static const BYTE g_noreg_access_permissions[] = {
    0x01, 0x00, 0x04, 0x80, 0x5c, 0x00, 0x00, 0x00, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x48, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x03, 0x00,
    0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00, 0x20, 0x02,
    0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00, 0x20, 0x02, 0x00, 0x00,
    0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00, 0x20, 0x02, 0x00, 0x00,
};

typedef struct RS_SD {
    const BYTE *Data;
    ULONG Size;
} RS_SD;

static RS_SD g_sds[5];

static BYTE *read_ole_value(const WCHAR *name, ULONG *size)
{
    *size = 0;
    DWORD type = 0, bytes = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Ole", name, RRF_RT_REG_BINARY, &type, nullptr,
                     &bytes) ||
        !bytes)
        return nullptr;
    BYTE *data = RsAlloc(bytes);
    if (data && RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Ole", name, RRF_RT_REG_BINARY, &type, data,
                             &bytes)) {
        RsFree(data);
        return nullptr;
    }
    *size = bytes;
    return data;
}

static void read_settings(void)
{
    g_sds[0] = (RS_SD){g_noreg_launch_permissions, sizeof(g_noreg_launch_permissions)};
    g_sds[1] = (RS_SD){g_noreg_access_permissions, sizeof(g_noreg_access_permissions)};
    g_sds[2].Data = read_ole_value(L"MachineLaunchRestriction", &g_sds[2].Size);
    g_sds[3].Data = read_ole_value(L"MachineAccessRestriction", &g_sds[3].Size);
    g_sds[4] = g_sds[3];

    WCHAR text[4] = {0};
    DWORD bytes = sizeof(text);
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Ole", L"EnableDCOM", RRF_RT_REG_SZ, nullptr, text,
                      &bytes))
        g_network_dcom = text[0] == 'Y' || text[0] == 'y';
    DWORD value = 0;
    bytes = sizeof(value);
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Ole", L"LegacyImpersonationLevel", RRF_RT_REG_DWORD,
                      nullptr, &value, &bytes))
        g_imp_level = value;
    bytes = sizeof(value);
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Ole", L"LegacyAuthenticationLevel",
                      RRF_RT_REG_DWORD, nullptr, &value, &bytes))
        g_authn_level = value;
}

/* ------------------------------------------------------------------ dual string arrays */

static DUALSTRINGARRAY *RsDsaCopy(const DUALSTRINGARRAY *d)
{
    if (!d)
        return nullptr;
    SIZE_T size = FIELD_OFFSET(DUALSTRINGARRAY, aStringArray) + d->wNumEntries * sizeof(USHORT);
    DUALSTRINGARRAY *c = MIDL_user_allocate(size);
    if (c)
        memcpy(c, d, size);
    return c;
}

/* the string bindings end with an empty one where the security bindings start, and those with one at the end. */
static BOOL dsa_valid(const DUALSTRINGARRAY *d)
{
    if (!d || d->wNumEntries < 2 || !d->wSecurityOffset || d->wSecurityOffset >= d->wNumEntries)
        return FALSE;
    return !d->aStringArray[d->wSecurityOffset - 1] && !d->aStringArray[d->wNumEntries - 1];
}

/* the string bindings of one array with the security bindings of another. */
static DUALSTRINGARRAY *dsa_join(const DUALSTRINGARRAY *strings, const DUALSTRINGARRAY *security)
{
    USHORT a = strings->wSecurityOffset, b = security->wNumEntries - security->wSecurityOffset;
    DUALSTRINGARRAY *d = RsAlloc(FIELD_OFFSET(DUALSTRINGARRAY, aStringArray) + (a + b) * sizeof(USHORT));
    if (!d)
        return nullptr;
    d->wNumEntries = a + b;
    d->wSecurityOffset = a;
    memcpy(d->aStringArray, strings->aStringArray, a * sizeof(USHORT));
    memcpy(d->aStringArray + a, security->aStringArray + security->wSecurityOffset, b * sizeof(USHORT));
    return d;
}

typedef struct DSA_BUILDER {
    USHORT Words[1024];
    USHORT Count;
} DSA_BUILDER;

static void put_word(DSA_BUILDER *b, USHORT w)
{
    if (b->Count < ARRAYSIZE(b->Words))
        b->Words[b->Count++] = w;
}

static void put_string(DSA_BUILDER *b, const WCHAR *s)
{
    for (; *s; s++)
        put_word(b, *s);
    put_word(b, 0);
}

/*
 * what rpcss answers as its own bindings: the machine's name and each of its addresses on ncacn_ip_tcp, then every
 * security package it accepts, with no principal.
 */
static DUALSTRINGARRAY *make_my_bindings(void)
{
    static DSA_BUILDER b;
    b.Count = 0;
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = ARRAYSIZE(name);
    if (GetComputerNameW(name, &size)) {
        put_word(&b, TOWER_TCP);
        put_string(&b, name);
    }
    ULONG bytes = 16384;
    IP_ADAPTER_ADDRESSES *list = RsAlloc(bytes);
    if (list && !GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                                    GAA_FLAG_SKIP_DNS_SERVER, nullptr, list, &bytes)) {
        for (IP_ADAPTER_ADDRESSES *a = list; a; a = a->Next) {
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->OperStatus != IfOperStatusUp)
                continue;
            for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next) {
                const SOCKADDR_IN *in = (const SOCKADDR_IN *)u->Address.lpSockaddr;
                if (!in || in->sin_family != AF_INET)
                    continue;
                const BYTE *ip = (const BYTE *)&in->sin_addr;
                WCHAR text[16];
                _snwprintf(text, ARRAYSIZE(text), L"%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
                put_word(&b, TOWER_TCP);
                put_string(&b, text);
            }
        }
    }
    RsFree(list);
    put_word(&b, 0);
    USHORT security = b.Count;
    for (ULONG i = 0; i < ARRAYSIZE(g_packages); i++) {
        put_word(&b, g_packages[i].Id);
        put_word(&b, 0xffff);
        put_word(&b, 0);
    }
    put_word(&b, 0);
    DUALSTRINGARRAY *d = RsAlloc(FIELD_OFFSET(DUALSTRINGARRAY, aStringArray) + b.Count * sizeof(USHORT));
    if (d) {
        d->wNumEntries = b.Count;
        d->wSecurityOffset = security;
        memcpy(d->aStringArray, b.Words, b.Count * sizeof(USHORT));
    }
    return d;
}

/* ------------------------------------------------------------------ oxid info copies */

static RPCSS_HSTRING hstring_copy(RPCSS_HSTRING h)
{
    if (!h)
        return nullptr;
    struct RPCSS_HSTRING_DATA *c = MIDL_user_allocate(sizeof(ULONG) + h->Bytes);
    if (c) {
        c->Bytes = h->Bytes;
        memcpy(c->Data, h->Data, h->Bytes);
    }
    return c;
}

static CONTAINER_EXTENSIONS *extensions_copy(const CONTAINER_EXTENSIONS *e)
{
    if (!e)
        return nullptr;
    CONTAINER_EXTENSIONS *c = MIDL_user_allocate(sizeof(*c));
    if (!c)
        return nullptr;
    *c = *e;
    c->extensions = MIDL_user_allocate(e->size * sizeof(CONTAINER_EXTENSION *));
    if (!c->extensions) {
        c->size = 0;
        return c;
    }
    for (ULONG i = 0; i < e->size; i++) {
        const CONTAINER_EXTENSION *x = e->extensions[i];
        CONTAINER_EXTENSION *y = nullptr;
        if (x && (y = MIDL_user_allocate(FIELD_OFFSET(CONTAINER_EXTENSION, data) + x->size)))
            memcpy(y, x, FIELD_OFFSET(CONTAINER_EXTENSION, data) + x->size);
        c->extensions[i] = y;
    }
    return c;
}

static void extensions_free(CONTAINER_EXTENSIONS *e)
{
    if (!e)
        return;
    for (ULONG i = 0; e->extensions && i < e->size; i++)
        MIDL_user_free(e->extensions[i]);
    MIDL_user_free(e->extensions);
    MIDL_user_free(e);
}

/* a deep copy of everything but the bindings, which are the owner's. */
static void info_copy(OXID_INFO *to, const OXID_INFO *from)
{
    *to = *from;
    to->psa = nullptr;
    to->containerVersion.extensions = extensions_copy(from->containerVersion.extensions);
    to->packageFullName = hstring_copy(from->packageFullName);
    to->userSid = hstring_copy(from->userSid);
    to->appcontainerSid = hstring_copy(from->appcontainerSid);
}

static void info_clear(OXID_INFO *info)
{
    extensions_free(info->containerVersion.extensions);
    MIDL_user_free(info->packageFullName);
    MIDL_user_free(info->userSid);
    MIDL_user_free(info->appcontainerSid);
    MIDL_user_free(info->psa);
    memset(info, 0, sizeof(*info));
}

/* ------------------------------------------------------------------ the tables */

static RS_PROCESS *process_of(PHPROCESS handle)
{
    for (RS_PROCESS *p = g_processes; p; p = p->Next)
        if (p == handle && p->Magic == PROCESS_MAGIC)
            return p;
    return nullptr;
}

static DWORD caller_pid(handle_t binding)
{
    ULONG pid = 0;
    if (I_RpcBindingInqLocalClientPID(binding, &pid) != RPC_S_OK)
        return 0;
    return pid;
}

/* the process of a context handle, when the caller is that process. call with the lock held. */
static RS_PROCESS *check_caller(handle_t binding, PHPROCESS handle)
{
    RS_PROCESS *p = process_of(handle);
    if (!p || caller_pid(binding) != p->Pid)
        return nullptr;
    return p;
}

DWORD RsCallerOf(handle_t binding, void *handle)
{
    AcquireSRWLockShared(&g_rs_lock);
    RS_PROCESS *p = check_caller(binding, handle);
    DWORD pid = p ? p->Pid : 0;
    ReleaseSRWLockShared(&g_rs_lock);
    return pid;
}

ULONG RsNextThreadId(void)
{
    return (ULONG)InterlockedExchangeAdd(&g_next_thread_id, 1);
}

static RS_OXID *find_oxid(UINT64 id)
{
    for (RS_OXID *o = g_oxids; o; o = o->Next)
        if (o->Id == id)
            return o;
    return nullptr;
}

static BOOL oxid_has_oid(const RS_OXID *o, UINT64 oid)
{
    for (ULONG i = 0; i < o->OidCount; i++)
        if (o->Oids[i] == oid)
            return TRUE;
    return FALSE;
}

static BOOL oxid_add_oid(RS_OXID *o, UINT64 oid)
{
    if (o->OidCount == o->OidCapacity) {
        ULONG capacity = o->OidCapacity ? o->OidCapacity * 2 : 16;
        UINT64 *oids = RsAlloc(capacity * sizeof(UINT64));
        if (!oids)
            return FALSE;
        if (o->Oids)
            memcpy(oids, o->Oids, o->OidCount * sizeof(UINT64));
        RsFree(o->Oids);
        o->Oids = oids;
        o->OidCapacity = capacity;
    }
    o->Oids[o->OidCount++] = oid;
    return TRUE;
}

static BOOL oxid_remove_oid(RS_OXID *o, UINT64 oid)
{
    for (ULONG i = 0; i < o->OidCount; i++) {
        if (o->Oids[i] == oid) {
            o->Oids[i] = o->Oids[--o->OidCount];
            return TRUE;
        }
    }
    return FALSE;
}

/* the oxid that holds an oid among those of a process. */
static RS_OXID *find_owned_oid(const RS_PROCESS *p, UINT64 oid)
{
    for (RS_OXID *o = g_oxids; o; o = o->Next)
        if (o->Owner == p && oxid_has_oid(o, oid))
            return o;
    return nullptr;
}

static void oxid_free(RS_OXID *o)
{
    for (RS_OXID **q = &g_oxids; *q; q = &(*q)->Next) {
        if (*q == o) {
            *q = o->Next;
            break;
        }
    }
    if (o->Owner && o->Owner->Primary == o)
        o->Owner->Primary = nullptr;
    info_clear(&o->Info);
    RsFree(o->Oids);
    RsFree(o);
}

static RS_OXID *oxid_new(RS_PROCESS *owner, const OXID_INFO *info)
{
    RS_OXID *o = RsAllocZero(sizeof(*o));
    if (!o)
        return nullptr;
    do
        o->Id = RsNewId();
    while (find_oxid(o->Id));
    o->Owner = owner;
    info_copy(&o->Info, info);
    o->Next = g_oxids;
    g_oxids = o;
    return o;
}

static void process_free(RS_PROCESS *p)
{
    RsProcessGone(p->Pid);
    RsRotProcessGone(p->Pid);
    for (RS_PROCESS **q = &g_processes; *q; q = &(*q)->Next) {
        if (*q == p) {
            *q = p->Next;
            break;
        }
    }
    for (RS_OXID *o = g_oxids, *next; o; o = next) {
        next = o->Next;
        if (o->Owner == p)
            oxid_free(o);
    }
    for (RS_REF *r = p->ClientRefs, *next; r; r = next) {
        next = r->Next;
        RsFree(r);
    }
    p->Magic = 0;
    RsFree(p->Desktop);
    RsFree(p->Exe);
    RsFree(p->Bindings);
    RsFree(p);
}

void WINAPI PHPROCESS_rundown(void *handle)
{
    AcquireSRWLockExclusive(&g_rs_lock);
    RS_PROCESS *p = process_of(handle);
    if (p)
        process_free(p);
    ReleaseSRWLockExclusive(&g_rs_lock);
}

/* ------------------------------------------------------------------ hstrings on the wire */

/* an hstring travels as combase's HSTRING_UserMarshal64 writes it: at a boundary of eight, a tag, the byte count, the
 * characters without a terminator. */
#define HSTRING_TAG 0x50746557

ULONG WINAPI RPCSS_HSTRING_UserSize(ULONG *flags, ULONG start, RPCSS_HSTRING *h)
{
    start = (start + 7) & ~7u;
    return start + 8 + (*h ? (*h)->Bytes : 0);
}

unsigned char *WINAPI RPCSS_HSTRING_UserMarshal(ULONG *flags, unsigned char *b, RPCSS_HSTRING *h)
{
    while ((ULONG_PTR)b & 7)
        *b++ = 0;
    ULONG n = *h ? (*h)->Bytes : 0;
    ULONG tag = HSTRING_TAG;
    memcpy(b, &tag, 4);
    memcpy(b + 4, &n, 4);
    if (n)
        memcpy(b + 8, (*h)->Data, n);
    return b + 8 + n;
}

unsigned char *WINAPI RPCSS_HSTRING_UserUnmarshal(ULONG *flags, unsigned char *b, RPCSS_HSTRING *h)
{
    b = (unsigned char *)(((ULONG_PTR)b + 7) & ~(ULONG_PTR)7);
    ULONG n;
    memcpy(&n, b + 4, 4);
    *h = nullptr;
    if (n && (*h = MIDL_user_allocate(sizeof(ULONG) + n))) {
        (*h)->Bytes = n;
        memcpy((*h)->Data, b + 8, n);
    }
    return b + 8 + n;
}

void WINAPI RPCSS_HSTRING_UserFree(ULONG *flags, RPCSS_HSTRING *h)
{
    MIDL_user_free(*h);
    *h = nullptr;
}

/* ------------------------------------------------------------------ ILocalObjectExporter */

static void reserve_ids(unsigned long count, unsigned long long *ids, unsigned long *done)
{
    *done = count < MAX_RESERVED_IDS ? count : MAX_RESERVED_IDS;
    for (ULONG i = 0; i < *done; i++)
        ids[i] = RsNewId();
}

error_status_t WINAPI ILocalObjectExporter_Connect_s(
    handle_t hServer, unsigned short *pwszWinstaDesktop, unsigned short *pwszExePath, PROXY_DECODE_INFO *pProxyDecodeInfo,
    unsigned long dwProcessFlags, unsigned short processArchitecture, PHPROCESS *pProcess,
    unsigned long *pdwTimeoutInSeconds, DUALSTRINGARRAY **ppdsaOrBindings, unsigned long long *pLocalMid,
    unsigned long cIdsToReserve, unsigned long long *aIdsReserved, unsigned long *pcIdsReserved,
    unsigned long *pfConnectFlags, unsigned long *pdwConnect3, unsigned long *pdwConnect4,
    unsigned short **pwszLegacySecurity, unsigned long *pAuthnLevel, unsigned long *pImpLevel,
    unsigned long *pcServerSvc, unsigned short **aServerSvc, unsigned long *pcClientSvc, SECPKG **aClientSvc,
    unsigned long *pcChannelHook, GUID **aChannelHook, unsigned long *pProcessID, unsigned long *pScmProcessID,
    unsigned long long *pSignature, GUID *pguidRPCSSProcessIdentifier, unsigned long *pdwSDSizes,
    unsigned long *pdwSDBlob, byte **pSDBlob)
{
    DWORD pid = caller_pid(hServer);
    if (!pid)
        return ERROR_ACCESS_DENIED;
    if (!pwszWinstaDesktop || (dwProcessFlags & ~7u))
        return ERROR_INVALID_PARAMETER;
    *pfConnectFlags = (g_network_dcom ? CONNECT_NETWORK_DCOM : 0) | CONNECT_CATCH_EXCEPTIONS |
                      CONNECT_FAILURE_LOGGING;
    *pdwConnect3 = 0;
    *pdwConnect4 = 0;
    *pAuthnLevel = g_authn_level;
    *pImpLevel = g_imp_level;
    *pwszLegacySecurity = nullptr;
    *pcChannelHook = 0;
    *aChannelHook = nullptr;

    ULONG count = ARRAYSIZE(g_packages);
    USHORT *server = MIDL_user_allocate(count * sizeof(USHORT));
    SECPKG *client = MIDL_user_allocate(count * sizeof(SECPKG));
    ULONG sd_total = 0;
    for (int i = 0; i < 5; i++)
        sd_total += g_sds[i].Size;
    BYTE *sd = MIDL_user_allocate(sd_total);
    RS_PROCESS *p = RsAllocZero(sizeof(*p));
    if (!server || !client || !sd || !p) {
        MIDL_user_free(server);
        MIDL_user_free(client);
        MIDL_user_free(sd);
        RsFree(p);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    for (ULONG i = 0; i < count; i++) {
        server[i] = g_packages[i].Id;
        client[i].wId = g_packages[i].Id;
        client[i].pName = (unsigned short *)RsDupString(g_packages[i].Name);
    }
    *pcServerSvc = *pcClientSvc = count;
    *aServerSvc = server;
    *aClientSvc = client;
    for (int i = 0, at = 0; i < 5; i++) {
        pdwSDSizes[i] = g_sds[i].Size;
        if (g_sds[i].Size)
            memcpy(sd + at, g_sds[i].Data, g_sds[i].Size);
        at += g_sds[i].Size;
    }
    *pdwSDBlob = sd_total;
    *pSDBlob = sd;

    p->Magic = PROCESS_MAGIC;
    p->Pid = pid;
    p->Flags = dwProcessFlags;
    p->Architecture = processArchitecture;
    p->Desktop = RsDupString((const WCHAR *)pwszWinstaDesktop);
    p->Exe = RsDupString((const WCHAR *)pwszExePath);
    p->Signature = RsNewId();
    SystemFunction036(&p->Identifier, sizeof(p->Identifier));

    AcquireSRWLockExclusive(&g_rs_lock);
    p->Next = g_processes;
    g_processes = p;
    *ppdsaOrBindings = RsDsaCopy(g_my_bindings);
    ReleaseSRWLockExclusive(&g_rs_lock);

    *pProcess = p;
    *pdwTimeoutInSeconds = BASE_TIMEOUT;
    *pLocalMid = g_local_mid;
    reserve_ids(cIdsToReserve, aIdsReserved, pcIdsReserved);
    *pProcessID = RsNextThreadId();
    *pScmProcessID = GetCurrentProcessId();
    *pSignature = p->Signature;
    *pguidRPCSSProcessIdentifier = p->Identifier;
    return ERROR_SUCCESS;
}

error_status_t WINAPI ILocalObjectExporter_SetAppID_s(handle_t hRpc, PHPROCESS phProcess, GUID *guidAppID)
{
    AcquireSRWLockExclusive(&g_rs_lock);
    RS_PROCESS *p = check_caller(hRpc, phProcess);
    if (p)
        p->AppId = *guidAppID;
    ReleaseSRWLockExclusive(&g_rs_lock);
    return p ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}

error_status_t WINAPI ILocalObjectExporter_GetDefaultSecurityPermissions_s(handle_t hRpc, unsigned long *pdwFlags,
                                                                            unsigned long *pcbSD, byte **ppSD)
{
    ULONG total = 0;
    for (int i = 0; i < 4; i++)
        total += g_sds[i].Size;
    BYTE *sd = MIDL_user_allocate(total);
    if (!sd)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (int i = 0, at = 0; i < 4; i++) {
        pdwFlags[i] = g_sds[i].Size;
        if (g_sds[i].Size)
            memcpy(sd + at, g_sds[i].Data, g_sds[i].Size);
        at += g_sds[i].Size;
    }
    *pcbSD = total;
    *ppSD = sd;
    return ERROR_SUCCESS;
}

error_status_t WINAPI ILocalObjectExporter_AllocateReservedIds_s(handle_t hRpc, PHPROCESS phProcess,
                                                                  unsigned long cIdsToAlloc,
                                                                  unsigned long long *aIdsAllocated,
                                                                  unsigned long *pcIdsAllocated)
{
    *pcIdsAllocated = 0;
    AcquireSRWLockShared(&g_rs_lock);
    RS_PROCESS *p = check_caller(hRpc, phProcess);
    ReleaseSRWLockShared(&g_rs_lock);
    if (!p)
        return ERROR_ACCESS_DENIED;
    reserve_ids(cIdsToAlloc, aIdsAllocated, pcIdsAllocated);
    return *pcIdsAllocated ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

static BOOL client_ref_add(RS_PROCESS *p, UINT64 oxid, UINT64 oid)
{
    RS_REF *r = RsAlloc(sizeof(*r));
    if (!r)
        return FALSE;
    r->Oxid = oxid;
    r->Oid = oid;
    r->Next = p->ClientRefs;
    p->ClientRefs = r;
    return TRUE;
}

static void client_ref_remove(RS_PROCESS *p, UINT64 oxid, UINT64 oid)
{
    for (RS_REF **q = &p->ClientRefs; *q; q = &(*q)->Next) {
        if ((*q)->Oxid == oxid && (*q)->Oid == oid) {
            RS_REF *r = *q;
            *q = r->Next;
            RsFree(r);
            return;
        }
    }
}

/*
 * a client process says which oids of other apartments it holds and which it let go, and a server which of its own it
 * freed. rpcss of windows pings the remote ones and asks a server to run down what nobody holds; every oid here is
 * local, and the references are only kept for the rundown of a client process.
 */
error_status_t WINAPI ILocalObjectExporter_BulkUpdateOIDs_s(
    handle_t hRpc, PHPROCESS phProcess, unsigned long cOidsToBeAdded, OXID_OID_PAIR *aOidsToBeAdded,
    unsigned long *aStatusOfAdds, unsigned long cOidsToBeRemoved, OXID_OID_PAIR *aOidsToBeRemoved,
    unsigned long cServerOidsToFree, unsigned long long *aServerOids, unsigned long cServerOidsToUnPin,
    unsigned long long *aServerOidsToUnPin, unsigned long cClientOxidsToFree, OXID_REF *aClientOxids,
    unsigned long cOidsUnmarshaled, OID_UNMARSHAL *aOidsUnmarshaled)
{
    AcquireSRWLockExclusive(&g_rs_lock);
    RS_PROCESS *p = check_caller(hRpc, phProcess);
    if (!p) {
        ReleaseSRWLockExclusive(&g_rs_lock);
        return ERROR_ACCESS_DENIED;
    }
    for (ULONG i = 0; i < cOidsToBeAdded; i++) {
        const OXID_OID_PAIR *a = &aOidsToBeAdded[i];
        if (a->mid != g_local_mid)
            aStatusOfAdds[i] = OR_INVALID_OXID;
        else if (!find_oxid(a->oxid))
            aStatusOfAdds[i] = OR_INVALID_OXID;
        else
            aStatusOfAdds[i] = client_ref_add(p, a->oxid, a->oid) ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    }
    for (ULONG i = 0; i < cOidsToBeRemoved; i++)
        client_ref_remove(p, aOidsToBeRemoved[i].oxid, aOidsToBeRemoved[i].oid);
    for (ULONG i = 0; i < cServerOidsToFree; i++) {
        RS_OXID *o = find_owned_oid(p, aServerOids[i]);
        if (o)
            oxid_remove_oid(o, aServerOids[i]);
    }
    ReleaseSRWLockExclusive(&g_rs_lock);
    return ERROR_SUCCESS;
}

error_status_t WINAPI ILocalObjectExporter_ClientResolveOXID_s(
    handle_t hRpc, PHPROCESS phProcess, unsigned long long poxidServer,
    DUALSTRINGARRAY *pssaServerObjectResolverBindings, OXID_INFO *poxidInfo, unsigned long long *pLocalMidOfRemote,
    unsigned long *pulMarshaledTargetInfoLength, byte **pucMarshaledTargetInfo, unsigned short *pAuthnSvc)
{
    memset(poxidInfo, 0, sizeof(*poxidInfo));
    *pulMarshaledTargetInfoLength = 0;
    *pucMarshaledTargetInfo = nullptr;
    *pAuthnSvc = 0;
    *pLocalMidOfRemote = 0;
    if (pssaServerObjectResolverBindings && !dsa_valid(pssaServerObjectResolverBindings))
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockShared(&g_rs_lock);
    RS_PROCESS *p = check_caller(hRpc, phProcess);
    RS_OXID *o = p ? find_oxid(poxidServer) : nullptr;
    error_status_t status = !p ? ERROR_ACCESS_DENIED : !o ? OR_INVALID_OXID : ERROR_SUCCESS;
    if (o) {
        info_copy(poxidInfo, &o->Info);
        poxidInfo->psa = RsDsaCopy(o->Owner->Bindings);
        *pLocalMidOfRemote = g_local_mid;
        if (!poxidInfo->psa)
            status = ERROR_NOT_ENOUGH_MEMORY;
    }
    ReleaseSRWLockShared(&g_rs_lock);
    return status;
}

/*
 * the string binding of local rpc a process of an oxid listens on, for the calls rpcss makes into its apartments. a
 * process gives its local bindings as whole string bindings.
 */
BOOL RsOxidBinding(UINT64 oxid, WCHAR *out, SIZE_T chars)
{
    BOOL found = FALSE;
    AcquireSRWLockShared(&g_rs_lock);
    RS_OXID *o = find_oxid(oxid);
    const DUALSTRINGARRAY *d = o && o->Owner ? o->Owner->Bindings : nullptr;
    for (ULONG at = 0; d && !found && at < d->wSecurityOffset && d->aStringArray[at];) {
        const WCHAR *text = (const WCHAR *)&d->aStringArray[at];
        ULONG length = 0;
        while (at + length < d->wSecurityOffset && text[length])
            length++;
        if (length > 8 && !_wcsnicmp(text, L"ncalrpc:", 8) && length < chars) {
            memcpy(out, text, length * sizeof(WCHAR));
            out[length] = 0;
            found = TRUE;
        }
        at += length + 1;
    }
    ReleaseSRWLockShared(&g_rs_lock);
    return found;
}

/* the checks rpcss makes of what a server says of an apartment, read from _ServerAllocateOXIDAndOIDs of 26300. */
static BOOL info_valid(const OXID_INFO *info)
{
    ULONG kind = info->dwFlags & OXIDF_KINDS;
    if (info->dwFlags & 0xE07FFFFF)
        return FALSE;
    switch (kind) {
    case OXIDF_MTA:
        if (info->dwTid != 0)
            return FALSE;
        break;
    case OXIDF_PRIMARY:
        if (info->dwTid != 0xFFFFFFFF)
            return FALSE;
        break;
    case OXIDF_STA:
    case OXIDF_NTA:
    case OXIDF_WINRT:
        if (info->dwTid - 1 > 0xFFFFFFFD)
            return FALSE;
        break;
    default:
        return FALSE;
    }
    if ((info->dwFlags & OXIDF_WINRT_EXTRA) && kind != OXIDF_WINRT)
        return FALSE;
    if ((info->processHostId == 0) != (info->clientDependencyBehavior == 0) || info->clientDependencyBehavior > 2)
        return FALSE;
    const UINT64 *ipid = (const UINT64 *)&info->primaryIpidRemUnknown;
    return ipid[0] || ipid[1];
}

static void allocate_oids(RS_OXID *o, unsigned long count, unsigned long long *oids, unsigned long *done)
{
    *done = 0;
    for (ULONG i = 0; i < count; i++) {
        UINT64 oid = RsNewId();
        if (!oxid_add_oid(o, oid))
            break;
        oids[(*done)++] = oid;
    }
}

error_status_t WINAPI ILocalObjectExporter_ServerAllocateOXIDAndOIDs_s(
    handle_t hRpc, PHPROCESS phProcess, unsigned long long *poxidServer, unsigned long long *pPrimaryOxid,
    unsigned long cOids, unsigned long long *aOid, unsigned long *pcOidsAllocated, OXID_INFO *pOxidInfo,
    DUALSTRINGARRAY *pdsaStringBindings, DUALSTRINGARRAY *pdsaSecurityBindings, unsigned long long *pdwOrBindingsID,
    DUALSTRINGARRAY **ppdsaOrBindings)
{
    *pdwOrBindingsID = 0;
    *ppdsaOrBindings = nullptr;
    *pcOidsAllocated = 0;
    if (!info_valid(pOxidInfo))
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&g_rs_lock);
    RS_PROCESS *p = check_caller(hRpc, phProcess);
    error_status_t status = ERROR_SUCCESS;
    if (!p)
        status = ERROR_ACCESS_DENIED;
    else if (!dsa_valid(pdsaStringBindings) || !dsa_valid(pdsaSecurityBindings))
        status = ERROR_INVALID_PARAMETER;
    if (!status) {
        DUALSTRINGARRAY *joined = dsa_join(pdsaStringBindings, pdsaSecurityBindings);
        if (joined) {
            RsFree(p->Bindings);
            p->Bindings = joined;
        } else {
            status = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    /* the first registration of a process also makes its primary oxid, of no thread, with the primary ipid. */
    if (!status && !p->Primary) {
        OXID_INFO primary = *pOxidInfo;
        primary.dwTid = 0xFFFFFFFF;
        primary.dwFlags = (pOxidInfo->dwFlags & 0xE0100000) | OXIDF_PRIMARY;
        primary.ipidRemUnknown = pOxidInfo->primaryIpidRemUnknown;
        if (!(p->Primary = oxid_new(p, &primary)))
            status = ERROR_NOT_ENOUGH_MEMORY;
    }
    RS_OXID *o = nullptr;
    if (!status) {
        if (pOxidInfo->dwFlags & OXIDF_PRIMARY)
            o = p->Primary;
        else if (!(o = oxid_new(p, pOxidInfo)))
            status = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (o) {
        o->Info.primaryOxid = p->Primary->Id;
        *poxidServer = o->Id;
        *pPrimaryOxid = p->Primary->Id;
        allocate_oids(o, cOids, aOid, pcOidsAllocated);
    }
    ReleaseSRWLockExclusive(&g_rs_lock);
    return status;
}

error_status_t WINAPI ILocalObjectExporter_ServerAllocateOIDs_s(handle_t hRpc, PHPROCESS phProcess,
                                                                 unsigned long long *poxidServer,
                                                                 unsigned long cOidsReturn,
                                                                 unsigned long long *aOidsReturn, unsigned long cOids,
                                                                 unsigned long long *aOids,
                                                                 unsigned long *pcOidsAllocated)
{
    *pcOidsAllocated = 0;
    AcquireSRWLockExclusive(&g_rs_lock);
    RS_PROCESS *p = check_caller(hRpc, phProcess);
    RS_OXID *o = p ? find_oxid(*poxidServer) : nullptr;
    error_status_t status = !p ? ERROR_ACCESS_DENIED : !o || o->Owner != p ? OR_INVALID_OXID : ERROR_SUCCESS;
    if (!status) {
        for (ULONG i = 0; i < cOidsReturn; i++)
            oxid_remove_oid(o, aOidsReturn[i]);
        allocate_oids(o, cOids, aOids, pcOidsAllocated);
    }
    ReleaseSRWLockExclusive(&g_rs_lock);
    return status;
}

error_status_t WINAPI ILocalObjectExporter_ServerFreeOXIDAndOIDs_s(handle_t hRpc, PHPROCESS phProcess,
                                                                    unsigned long long oxidServer, unsigned long cOids,
                                                                    unsigned long long *aOids)
{
    AcquireSRWLockExclusive(&g_rs_lock);
    RS_PROCESS *p = check_caller(hRpc, phProcess);
    RS_OXID *o = p ? find_oxid(oxidServer) : nullptr;
    error_status_t status = !p ? ERROR_ACCESS_DENIED : !o || o->Owner != p ? OR_INVALID_OXID : ERROR_SUCCESS;
    if (!status) {
        if (o == p->Primary) {
            for (ULONG i = 0; i < cOids; i++)
                oxid_remove_oid(o, aOids[i]);
        } else {
            oxid_free(o);
        }
    }
    ReleaseSRWLockExclusive(&g_rs_lock);
    return status;
}

error_status_t WINAPI ILocalObjectExporter_SetServerOIDFlags_s(handle_t hRpc, PHPROCESS phProcess,
                                                                unsigned long long oid, unsigned long dwFlags)
{
    AcquireSRWLockShared(&g_rs_lock);
    RS_PROCESS *p = check_caller(hRpc, phProcess);
    error_status_t status = !p ? ERROR_ACCESS_DENIED : !find_owned_oid(p, oid) ? OR_INVALID_OID : ERROR_SUCCESS;
    ReleaseSRWLockShared(&g_rs_lock);
    return status;
}

error_status_t WINAPI ILocalObjectExporter_Disconnect_s(handle_t hRpc, PHPROCESS *pphProcess, unsigned long dwFlags)
{
    AcquireSRWLockExclusive(&g_rs_lock);
    RS_PROCESS *p = check_caller(hRpc, *pphProcess);
    if (p)
        process_free(p);
    ReleaseSRWLockExclusive(&g_rs_lock);
    if (!p)
        return ERROR_ACCESS_DENIED;
    *pphProcess = nullptr;
    return ERROR_SUCCESS;
}

error_status_t WINAPI ILocalObjectExporter_GetUpdatedResolverBindings_s(handle_t hRpc, PHPROCESS phProcess,
                                                                         DUALSTRINGARRAY **ppdsaOrBindings,
                                                                         unsigned long long *pdwBindingsID)
{
    AcquireSRWLockShared(&g_rs_lock);
    RS_PROCESS *p = check_caller(hRpc, phProcess);
    *ppdsaOrBindings = p ? RsDsaCopy(g_my_bindings) : nullptr;
    *pdwBindingsID = p ? g_bindings_id : 0;
    ReleaseSRWLockShared(&g_rs_lock);
    if (!p)
        return ERROR_ACCESS_DENIED;
    return *ppdsaOrBindings ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

/* ------------------------------------------------------------------ IMachineActivatorControl */

/*
 * a surrogate, dllhost.exe, tells the activator how far it got. DcomLaunch watches them to know when one can take a
 * class; mime starts no surrogate yet, so the calls are only checked.
 */
static long surrogate_notice(handle_t binding, PHPROCESS handle, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(binding, handle) ? S_OK : E_ACCESSDENIED;
}

long WINAPI IMachineActivatorControl_NotifySurrogateStarted_s(handle_t hRpc, PHPROCESS phProcess,
                                                             unsigned long cPackages, RPCSS_HSTRING *packages,
                                                             PROCESS_ACTIVATOR_TOKEN *pToken, error_status_t *prpcstat)
{
    return surrogate_notice(hRpc, phProcess, prpcstat);
}

long WINAPI IMachineActivatorControl_NotifySurrogateInitializing_s(handle_t hRpc, PHPROCESS phProcess,
                                                                  error_status_t *prpcstat)
{
    return surrogate_notice(hRpc, phProcess, prpcstat);
}

long WINAPI IMachineActivatorControl_NotifySurrogateReady_s(handle_t hRpc, PHPROCESS phProcess, unsigned long cPackages,
                                                           RPCSS_HSTRING *packages, error_status_t *prpcstat)
{
    return surrogate_notice(hRpc, phProcess, prpcstat);
}

long WINAPI IMachineActivatorControl_NotifySurrogateStopped_s(handle_t hRpc, PHPROCESS phProcess,
                                                             error_status_t *prpcstat)
{
    return surrogate_notice(hRpc, phProcess, prpcstat);
}

long WINAPI IMachineActivatorControl_NotifySurrogatePaused_s(handle_t hRpc, PHPROCESS phProcess,
                                                            error_status_t *prpcstat)
{
    return surrogate_notice(hRpc, phProcess, prpcstat);
}

long WINAPI IMachineActivatorControl_NotifySurrogateResumed_s(handle_t hRpc, PHPROCESS phProcess,
                                                             error_status_t *prpcstat)
{
    return surrogate_notice(hRpc, phProcess, prpcstat);
}

long WINAPI IMachineActivatorControl_NotifySurrogateUserInitializing_s(handle_t hRpc, PHPROCESS phProcess,
                                                                      unsigned long cPackages, RPCSS_HSTRING *packages,
                                                                      error_status_t *prpcstat)
{
    return surrogate_notice(hRpc, phProcess, prpcstat);
}

/* ------------------------------------------------------------------ the server */

static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface, void *context)
{
    return RPC_S_OK;
}

/* the endpoint is the endpoint mapper's: every local caller connects, app containers included. */
static const BYTE g_endpoint_sd[] = {
    0x01, 0x00, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
};

RPC_STATUS RsRegisterInterface(RPC_IF_HANDLE spec)
{
    return RpcServerRegisterIf3(spec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, allow_local, (void *)g_endpoint_sd);
}

DWORD RsStartResolver(void)
{
    RsLog("the resolver starts in process %lu", GetCurrentProcessId());
    read_settings();
    g_local_mid = RsNewId();
    g_bindings_id = 1;
    if (!(g_my_bindings = make_my_bindings()))
        return ERROR_NOT_ENOUGH_MEMORY;
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
                                               (RPC_WSTR)L"epmapper", (void *)g_endpoint_sd);
    if (status == RPC_S_DUPLICATE_ENDPOINT)
        status = RPC_S_OK;
    if (!status)
        status = RsRegisterInterface(ILocalObjectExporter_s_ifspec);
    if (!status)
        status = RsRegisterInterface(IMachineActivatorControl_s_ifspec);
    if (!status)
        status = RsStartScm();
    if (!status)
        status = RsStartRot();
    return status;
}
