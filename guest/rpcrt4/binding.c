/*
 * binding handles and string bindings. A string binding is "[object@]protseq:address[endpoint,options]", the
 * characters : , @ [ \ ] escaped by a backslash inside its parts. A binding handle made from one only keeps its parts and
 * an object uuid: Mime has no rpc transport, so nothing is ever bound to a server, but the handles can be copied, turned
 * back into strings and have their object and security settings queried, as callers expect.
 */
#include "rpcrt4_private.h"

#define MAX_PARTS_LENGTH 0x7FFF

#define BINDING_BUCKETS 64

static SRWLOCK g_lock = SRWLOCK_INIT;
static BINDING *g_bindings[BINDING_BUCKETS];

static ULONG bucket_of(const void *p)
{
    return (ULONG)(((ULONG_PTR)p >> 4) & (BINDING_BUCKETS - 1));
}

/* ------------------------------------------------------------------ strings */

PWSTR RpcpCopyString(PCWSTR s)
{
    SIZE_T n = s ? wcslen(s) : 0;
    PWSTR copy = HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
    if (copy) {
        if (n)
            memcpy(copy, s, n * sizeof(WCHAR));
        copy[n] = 0;
    }
    return copy;
}

static BOOL needs_escape(WCHAR c)
{
    return c == L',' || c == L':' || c == L'@' || c == L'[' || c == L'\\' || c == L']';
}

static SIZE_T escaped_length(PCWSTR s)
{
    SIZE_T n = 0;
    for (; s && *s; s++)
        n += needs_escape(*s) ? 2 : 1;
    return n;
}

static PWSTR put_escaped(PWSTR out, PCWSTR s)
{
    for (; s && *s; s++) {
        if (needs_escape(*s))
            *out++ = L'\\';
        *out++ = *s;
    }
    return out;
}

/* the text up to the first of the stop characters that is not escaped, with the escapes taken out. */
static PCWSTR scan_part(PCWSTR s, WCHAR stop1, WCHAR stop2, PWSTR *part, BOOL *ok)
{
    PCWSTR p = s;
    while (*p && *p != stop1 && *p != stop2) {
        if (*p == L'\\' && p[1])
            p++;
        else if (*p == L'\\')
            break;
        p++;
    }
    PWSTR copy = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)(p - s) + 1) * sizeof(WCHAR));
    *ok = copy != nullptr;
    if (copy) {
        PWSTR out = copy;
        for (PCWSTR q = s; q < p; q++) {
            if (*q == L'\\' && q + 1 < p)
                q++;
            *out++ = *q;
        }
        *out = 0;
    }
    *part = copy;
    return p;
}

void RpcpFreeString(PWSTR s)
{
    if (s)
        HeapFree(GetProcessHeap(), 0, s);
}

typedef struct {
    UUID Object;
    PWSTR Protseq, Address, Endpoint, Options;
} PARTS;

static void free_parts(PARTS *parts)
{
    RpcpFreeString(parts->Protseq);
    RpcpFreeString(parts->Address);
    RpcpFreeString(parts->Endpoint);
    RpcpFreeString(parts->Options);
    memset(parts, 0, sizeof(*parts));
}

/*
 * the string cut into its parts: an object uuid before the first unescaped @, the protocol sequence up to the first
 * unescaped colon, the network address up to a bracket, and in the brackets the endpoint and the options after a comma,
 * or "endpoint=...", or options alone when the text has an equals sign that is not an endpoint's.
 */
static RPC_STATUS parse_binding(PCWSTR text, PARTS *parts)
{
    memset(parts, 0, sizeof(*parts));
    if (!text)
        return ERROR_INVALID_PARAMETER;
    if (wcslen(text) > MAX_PARTS_LENGTH)
        return ERROR_INVALID_PARAMETER;
    PCWSTR p = text;
    PCWSTR at = nullptr;
    for (PCWSTR q = text; *q; q++) {
        if (*q == L'\\' && q[1]) {
            q++;
        } else if (*q == L'@') {
            at = q;
            break;
        }
    }
    if (at) {
        PWSTR uuid;
        BOOL ok;
        scan_part(text, L'@', L'@', &uuid, &ok);
        if (!ok)
            return ERROR_OUTOFMEMORY;
        RPC_STATUS status = UuidFromStringW(uuid, &parts->Object);
        RpcpFreeString(uuid);
        if (status)
            return RPC_S_INVALID_STRING_UUID;
        p = at + 1;
    }
    BOOL ok;
    PCWSTR colon = scan_part(p, L':', L':', &parts->Protseq, &ok);
    if (!ok)
        return ERROR_OUTOFMEMORY;
    if (*colon != L':') {
        free_parts(parts);
        return RPC_S_INVALID_STRING_BINDING;
    }
    p = colon + 1;
    PCWSTR bracket = scan_part(p, L'[', L'[', &parts->Address, &ok);
    if (!ok) {
        free_parts(parts);
        return ERROR_OUTOFMEMORY;
    }
    if (*bracket != L'[') {
        parts->Endpoint = RpcpCopyString(L"");
        parts->Options = nullptr;
        if (!parts->Endpoint) {
            free_parts(parts);
            return ERROR_OUTOFMEMORY;
        }
        return RPC_S_OK;
    }
    p = bracket + 1;
    /* the closing bracket, then what is inside it. */
    PCWSTR close = p;
    while (*close && *close != L']') {
        if (*close == L'\\' && close[1])
            close++;
        close++;
    }
    if (*close != L']') {
        free_parts(parts);
        return RPC_S_INVALID_ENDPOINT_FORMAT;
    }
    SIZE_T inner = (SIZE_T)(close - p);
    PWSTR content = HeapAlloc(GetProcessHeap(), 0, (inner + 1) * sizeof(WCHAR));
    if (!content) {
        free_parts(parts);
        return ERROR_OUTOFMEMORY;
    }
    memcpy(content, p, inner * sizeof(WCHAR));
    content[inner] = 0;
    PCWSTR comma = nullptr, equals = nullptr;
    for (PCWSTR q = content; *q; q++) {
        if (*q == L'\\' && q[1]) {
            q++;
        } else if (*q == L',' && !comma) {
            comma = q;
        } else if (*q == L'=' && !equals && !comma) {
            equals = q;
        }
    }
    RPC_STATUS status = RPC_S_OK;
    if (equals) {
        /* only "endpoint=" may name a part; any other text of this form is options. */
        if (!_wcsnicmp(content, L"endpoint", 8) && (PCWSTR)content + 8 == equals) {
            BOOL e1, e2;
            PCWSTR end = scan_part(equals + 1, L',', L',', &parts->Endpoint, &e1);
            if (!e1)
                status = ERROR_OUTOFMEMORY;
            else if (*end == L',') {
                scan_part(end + 1, 0, 0, &parts->Options, &e2);
                if (!e2)
                    status = ERROR_OUTOFMEMORY;
            }
        } else {
            BOOL e1;
            parts->Endpoint = RpcpCopyString(L"");
            scan_part(content, 0, 0, &parts->Options, &e1);
            if (!parts->Endpoint || !e1)
                status = ERROR_OUTOFMEMORY;
        }
    } else {
        BOOL e1, e2 = TRUE;
        PCWSTR end = scan_part(content, L',', L',', &parts->Endpoint, &e1);
        if (e1 && *end == L',')
            scan_part(end + 1, 0, 0, &parts->Options, &e2);
        if (!e1 || !e2)
            status = ERROR_OUTOFMEMORY;
    }
    HeapFree(GetProcessHeap(), 0, content);
    if (status)
        free_parts(parts);
    return status;
}

static PWSTR format_binding(const PARTS *parts, BOOL with_uuid)
{
    SIZE_T length = 1;
    if (with_uuid)
        length += 37;
    length += escaped_length(parts->Protseq) + 1 + escaped_length(parts->Address);
    BOOL endpoint = parts->Endpoint && *parts->Endpoint, options = parts->Options && *parts->Options;
    if (endpoint || options)
        length += 2 + escaped_length(parts->Endpoint) + (options ? 1 + escaped_length(parts->Options) : 0);
    PWSTR text = HeapAlloc(GetProcessHeap(), 0, (length + 2) * sizeof(WCHAR));
    if (!text)
        return nullptr;
    PWSTR out = text;
    if (with_uuid) {
        PWSTR uuid;
        if (UuidToStringW(&parts->Object, &uuid)) {
            HeapFree(GetProcessHeap(), 0, text);
            return nullptr;
        }
        for (PCWSTR u = uuid; *u; u++)
            *out++ = *u;
        RpcpFreeString(uuid);
        *out++ = L'@';
    }
    out = put_escaped(out, parts->Protseq);
    *out++ = L':';
    out = put_escaped(out, parts->Address);
    if (endpoint || options) {
        *out++ = L'[';
        out = put_escaped(out, parts->Endpoint);
        if (options) {
            *out++ = L',';
            out = put_escaped(out, parts->Options);
        }
        *out++ = L']';
    }
    *out = 0;
    return text;
}

BOOL RpcpIsNil(const UUID *u)
{
    static const UUID nil;
    return !memcmp(u, &nil, sizeof(nil));
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringBindingComposeW(RPC_WSTR object, RPC_WSTR protseq, RPC_WSTR address,
                                                       RPC_WSTR endpoint, RPC_WSTR options, RPC_WSTR *binding)
{
    if (!binding)
        return RPC_S_OK;
    PARTS parts = {};
    if (object && *object && UuidFromStringW(object, &parts.Object))
        return RPC_S_INVALID_STRING_UUID;
    parts.Protseq = (PWSTR)protseq;
    parts.Address = (PWSTR)address;
    parts.Endpoint = (PWSTR)endpoint;
    parts.Options = (PWSTR)options;
    *binding = format_binding(&parts, !RpcpIsNil(&parts.Object));
    return *binding ? RPC_S_OK : ERROR_OUTOFMEMORY;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringBindingParseW(RPC_WSTR binding, RPC_WSTR *object, RPC_WSTR *protseq,
                                                     RPC_WSTR *address, RPC_WSTR *endpoint, RPC_WSTR *options)
{
    if (!binding)
        return ERROR_INVALID_PARAMETER;
    PARTS parts;
    RPC_STATUS status = parse_binding(binding, &parts);
    if (status)
        return status;
    PWSTR uuid = nullptr;
    if (object) {
        status = UuidToStringW(&parts.Object, &uuid);
        /* an object that is not given is the empty string, not the nil uuid. */
        if (!status && RpcpIsNil(&parts.Object)) {
            RpcpFreeString(uuid);
            uuid = RpcpCopyString(L"");
            if (!uuid)
                status = ERROR_OUTOFMEMORY;
        }
    }
    PWSTR proto = protseq ? RpcpCopyString(parts.Protseq) : nullptr;
    PWSTR addr = address ? RpcpCopyString(parts.Address) : nullptr;
    PWSTR ep = endpoint ? RpcpCopyString(parts.Endpoint) : nullptr;
    PWSTR opt = options ? RpcpCopyString(parts.Options) : nullptr;
    free_parts(&parts);
    if (status || (protseq && !proto) || (address && !addr) || (endpoint && !ep) || (options && !opt)) {
        RpcpFreeString(uuid);
        RpcpFreeString(proto);
        RpcpFreeString(addr);
        RpcpFreeString(ep);
        RpcpFreeString(opt);
        return status ? status : ERROR_OUTOFMEMORY;
    }
    if (object)
        *object = uuid;
    if (protseq)
        *protseq = proto;
    if (address)
        *address = addr;
    if (endpoint)
        *endpoint = ep;
    if (options)
        *options = opt;
    return RPC_S_OK;
}

/* ------------------------------------------------------------------ the narrow forms */

PWSTR RpcpWiden(const unsigned char *s)
{
    if (!s)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, (const char *)s, -1, nullptr, 0);
    PWSTR w = n > 0 ? HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(WCHAR)) : nullptr;
    if (w)
        MultiByteToWideChar(CP_ACP, 0, (const char *)s, -1, w, n);
    return w;
}

RPC_CSTR RpcpNarrow(PWSTR w)
{
    if (!w)
        return nullptr;
    int n = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
    char *a = n > 0 ? HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n) : nullptr;
    if (a)
        WideCharToMultiByte(CP_ACP, 0, w, -1, a, n, nullptr, nullptr);
    RpcpFreeString(w);
    return (RPC_CSTR)a;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringBindingComposeA(RPC_CSTR object, RPC_CSTR protseq, RPC_CSTR address,
                                                       RPC_CSTR endpoint, RPC_CSTR options, RPC_CSTR *binding)
{
    PWSTR w[5] = {RpcpWiden(object), RpcpWiden(protseq), RpcpWiden(address), RpcpWiden(endpoint), RpcpWiden(options)};
    PWSTR result = nullptr;
    RPC_STATUS status = RpcStringBindingComposeW(w[0], w[1], w[2], w[3], w[4], binding ? &result : nullptr);
    for (int i = 0; i < 5; i++)
        RpcpFreeString(w[i]);
    if (!status && binding) {
        *binding = RpcpNarrow(result);
        if (!*binding)
            status = ERROR_OUTOFMEMORY;
    }
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringBindingParseA(RPC_CSTR binding, RPC_CSTR *object, RPC_CSTR *protseq,
                                                     RPC_CSTR *address, RPC_CSTR *endpoint, RPC_CSTR *options)
{
    PWSTR wide = RpcpWiden(binding);
    if (binding && !wide)
        return ERROR_OUTOFMEMORY;
    PWSTR o = nullptr, p = nullptr, a = nullptr, e = nullptr, n = nullptr;
    RPC_STATUS status = RpcStringBindingParseW(wide, object ? &o : nullptr, protseq ? &p : nullptr,
                                               address ? &a : nullptr, endpoint ? &e : nullptr, options ? &n : nullptr);
    RpcpFreeString(wide);
    if (status)
        return status;
    if (object)
        *object = RpcpNarrow(o);
    if (protseq)
        *protseq = RpcpNarrow(p);
    if (address)
        *address = RpcpNarrow(a);
    if (endpoint)
        *endpoint = RpcpNarrow(e);
    if (options)
        *options = RpcpNarrow(n);
    return RPC_S_OK;
}

/* ------------------------------------------------------------------ handles */

BINDING *RpcpBindingNew(BINDING_KIND kind)
{
    BINDING *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return nullptr;
    b->Magic = BINDING_MAGIC;
    b->Kind = kind;
    ULONG slot = bucket_of(b);
    AcquireSRWLockExclusive(&g_lock);
    b->Next = g_bindings[slot];
    g_bindings[slot] = b;
    ReleaseSRWLockExclusive(&g_lock);
    return b;
}

/* the handle if it is one the runtime made; a program's pointer is never dereferenced before it is known. */
BINDING *RpcpBindingLookup(RPC_BINDING_HANDLE handle)
{
    if (!handle)
        return nullptr;
    BINDING *found = nullptr;
    AcquireSRWLockShared(&g_lock);
    for (BINDING *b = g_bindings[bucket_of(handle)]; b; b = b->Next)
        if (b == handle) {
            found = b;
            break;
        }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

static void release_strings(BINDING *b)
{
    RpcpFreeString(b->Protseq);
    RpcpFreeString(b->Address);
    RpcpFreeString(b->Endpoint);
    RpcpFreeString(b->Options);
    RpcpFreeString(b->Principal);
}

/* removes the handle from the table and frees it; false if it was not there. */
static BOOL binding_unlink(BINDING *target)
{
    ULONG slot = bucket_of(target);
    AcquireSRWLockExclusive(&g_lock);
    BINDING **at = &g_bindings[slot];
    while (*at && *at != target)
        at = &(*at)->Next;
    BINDING *b = *at;
    if (b)
        *at = b->Next;
    ReleaseSRWLockExclusive(&g_lock);
    return b != nullptr;
}

void RpcpBindingRelease(BINDING *b)
{
    if (!binding_unlink(b))
        return;
    release_strings(b);
    b->Magic = 0;
    HeapFree(GetProcessHeap(), 0, b);
}

/* a client handle with the same parts, object and settings. */
BINDING *RpcpBindingCopy(const BINDING *from)
{
    BINDING *b = RpcpBindingNew(BINDING_CLIENT);
    if (!b)
        return nullptr;
    b->Object = from->Object;
    b->Protseq = RpcpCopyString(from->Protseq);
    b->Address = RpcpCopyString(from->Address);
    b->Endpoint = RpcpCopyString(from->Endpoint);
    b->Options = RpcpCopyString(from->Options);
    b->Principal = from->Principal ? RpcpCopyString(from->Principal) : nullptr;
    if (!b->Protseq || !b->Address || !b->Endpoint || !b->Options || (from->Principal && !b->Principal)) {
        RpcpBindingRelease(b);
        return nullptr;
    }
    b->HasAuth = from->HasAuth;
    b->AuthnLevel = from->AuthnLevel;
    b->AuthnService = from->AuthnService;
    b->AuthzService = from->AuthzService;
    b->ImpersonationType = from->ImpersonationType;
    memcpy(b->OptionValue, from->OptionValue, sizeof(b->OptionValue));
    b->ComTimeout = from->ComTimeout;
    b->CallTimeout = from->CallTimeout;
    return b;
}

/* the protocol sequences that make a handle: the local one and the connection oriented ones. */
static BOOL protseq_known(PCWSTR protseq)
{
    return !_wcsicmp(protseq, L"ncalrpc") || !_wcsnicmp(protseq, L"ncacn_", 6);
}

static BOOL protseq_supported(PCWSTR protseq)
{
    static const WCHAR *const names[] = {L"ncalrpc",       L"ncacn_np",   L"ncacn_ip_tcp", L"ncacn_http",
                                         L"ncacn_nb_tcp",  L"ncacn_hvsocket"};
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
        if (!_wcsicmp(protseq, names[i]))
            return TRUE;
    return FALSE;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingFromStringBindingW(RPC_WSTR string, RPC_BINDING_HANDLE *handle)
{
    if (!handle || !string)
        return ERROR_INVALID_PARAMETER;
    *handle = nullptr;
    PARTS parts;
    RPC_STATUS status = parse_binding(string, &parts);
    if (status)
        return status;
    if (!parts.Protseq || !protseq_known(parts.Protseq)) {
        free_parts(&parts);
        return RPC_S_INVALID_RPC_PROTSEQ;
    }
    if (!protseq_supported(parts.Protseq)) {
        free_parts(&parts);
        return RPC_S_PROTSEQ_NOT_SUPPORTED;
    }
    BINDING *b = RpcpBindingNew(BINDING_CLIENT);
    if (!b) {
        free_parts(&parts);
        return ERROR_OUTOFMEMORY;
    }
    b->Object = parts.Object;
    b->Protseq = parts.Protseq;
    b->Address = parts.Address;
    b->Endpoint = parts.Endpoint;
    b->Options = parts.Options;
    *handle = b;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingFromStringBindingA(RPC_CSTR string, RPC_BINDING_HANDLE *handle)
{
    PWSTR wide = RpcpWiden(string);
    if (string && !wide)
        return ERROR_OUTOFMEMORY;
    RPC_STATUS status = RpcBindingFromStringBindingW(wide, handle);
    RpcpFreeString(wide);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingFree(RPC_BINDING_HANDLE *handle)
{
    if (!handle)
        return ERROR_INVALID_PARAMETER;
    BINDING *b = RpcpBindingLookup(*handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (b->Kind != BINDING_CLIENT)
        return RPC_S_WRONG_KIND_OF_BINDING;
    /* when no other handle goes to the server, the connections kept for it are of no use. */
    WCHAR key[400];
    RpcpBindingKey(b, key, ARRAYSIZE(key));
    RpcpBindingRelease(b);
    *handle = nullptr;
    BOOL in_use = FALSE;
    WCHAR other[400];
    AcquireSRWLockShared(&g_lock);
    for (ULONG i = 0; i < BINDING_BUCKETS && !in_use; i++)
        for (BINDING *c = g_bindings[i]; c; c = c->Next)
            if (c->Kind == BINDING_CLIENT) {
                RpcpBindingKey(c, other, ARRAYSIZE(other));
                if (!wcscmp(key, other)) {
                    in_use = TRUE;
                    break;
                }
            }
    ReleaseSRWLockShared(&g_lock);
    if (!in_use)
        RpcpPoolDrop(key);
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingCopy(RPC_BINDING_HANDLE source, RPC_BINDING_HANDLE *destination)
{
    if (!destination)
        return ERROR_INVALID_PARAMETER;
    BINDING *from = RpcpBindingLookup(source);
    if (!from)
        return RPC_S_INVALID_BINDING;
    BINDING *b = RpcpBindingCopy(from);
    if (!b)
        return ERROR_OUTOFMEMORY;
    *destination = b;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcBindingCopy(RPC_BINDING_HANDLE source, RPC_BINDING_HANDLE *destination)
{
    return RpcBindingCopy(source, destination);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingToStringBindingW(RPC_BINDING_HANDLE handle, RPC_WSTR *string)
{
    BINDING *b = RpcpBindingLookup(handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (!string)
        return ERROR_INVALID_PARAMETER;
    PARTS parts = {b->Object, b->Protseq, b->Address, b->Endpoint, b->Options};
    *string = format_binding(&parts, !RpcpIsNil(&b->Object));
    return *string ? RPC_S_OK : ERROR_OUTOFMEMORY;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingToStringBindingA(RPC_BINDING_HANDLE handle, RPC_CSTR *string)
{
    PWSTR wide = nullptr;
    RPC_STATUS status = RpcBindingToStringBindingW(handle, string ? &wide : nullptr);
    if (status || !string)
        return status;
    *string = RpcpNarrow(wide);
    return *string ? RPC_S_OK : ERROR_OUTOFMEMORY;
}

/* a handle without an object has the nil uuid; a null handle asks for the call being served. */
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqObject(RPC_BINDING_HANDLE handle, UUID *object)
{
    BINDING *b = handle ? RpcpBindingLookup(handle) : RpcpCurrentCallBinding();
    if (!handle && !b)
        return RPC_S_NO_CALL_ACTIVE;
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (!object)
        return ERROR_INVALID_PARAMETER;
    *object = b->Object;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetObject(RPC_BINDING_HANDLE handle, UUID *object)
{
    BINDING *b = RpcpBindingLookup(handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    static const UUID nil;
    b->Object = object ? *object : nil;
    return RPC_S_OK;
}

/* a handle used again for another server finds its endpoint again when asked. */
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingReset(RPC_BINDING_HANDLE handle)
{
    BINDING *b = RpcpBindingLookup(handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (b->Kind != BINDING_CLIENT)
        return RPC_S_WRONG_KIND_OF_BINDING;
    if (b->Endpoint && *b->Endpoint) {
        PWSTR empty = RpcpCopyString(L"");
        if (!empty)
            return ERROR_OUTOFMEMORY;
        RpcpFreeString(b->Endpoint);
        b->Endpoint = empty;
    }
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingVectorFree(RPC_BINDING_VECTOR **vector)
{
    if (!vector || !*vector)
        return RPC_S_NO_BINDINGS;
    RPC_BINDING_VECTOR *v = *vector;
    for (ULONG i = 0; i < v->Count; i++)
        if (v->BindingH[i])
            RpcBindingFree(&v->BindingH[i]);
    HeapFree(GetProcessHeap(), 0, v);
    *vector = nullptr;
    return RPC_S_OK;
}

/*
 * the options are kept on the handle by number. The documented ones are below 14; larger numbers and zero are an
 * invalid argument. The call timeout and the no linger option take effect.
 */
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetOption(RPC_BINDING_HANDLE handle, ULONG option, ULONG_PTR value)
{
    BINDING *b = RpcpBindingLookup(handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (!option || option > 30)
        return ERROR_INVALID_PARAMETER;
    b->OptionValue[option] = value;
    if (option == RPC_C_OPT_CALL_TIMEOUT)
        b->CallTimeout = (ULONG)value;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqOption(RPC_BINDING_HANDLE handle, ULONG option, ULONG_PTR *value)
{
    BINDING *b = RpcpBindingLookup(handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (!option || option > 30 || !value)
        return ERROR_INVALID_PARAMETER;
    *value = b->OptionValue[option];
    return RPC_S_OK;
}

/* the time a client waits for a server to answer when it connects; the default is 5, 10 is for ever. */
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtSetComTimeout(RPC_BINDING_HANDLE handle, unsigned int timeout)
{
    BINDING *b = RpcpBindingLookup(handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (timeout > RPC_C_BINDING_INFINITE_TIMEOUT)
        return RPC_S_INVALID_TIMEOUT;
    b->ComTimeout = timeout + 1;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqComTimeout(RPC_BINDING_HANDLE handle, unsigned int *timeout)
{
    BINDING *b = RpcpBindingLookup(handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (!timeout)
        return ERROR_INVALID_PARAMETER;
    *timeout = b->ComTimeout ? b->ComTimeout - 1 : RPC_C_BINDING_DEFAULT_TIMEOUT;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtSetCancelTimeout(LONG timeout)
{
    if (timeout < RPC_C_CANCEL_INFINITE_TIMEOUT)
        return RPC_S_INVALID_TIMEOUT;
    return RPC_S_OK;
}

/* ------------------------------------------------------------------ security */

static RPC_STATUS set_auth(RPC_BINDING_HANDLE handle, PCWSTR principal, ULONG level, ULONG service, void *identity,
                           ULONG authz, const RPC_SECURITY_QOS *qos)
{
    BINDING *b = RpcpBindingLookup(handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (level > 6)
        return RPC_S_UNKNOWN_AUTHN_LEVEL;
    if (level == 1 || !level || service == 0) {
        b->HasAuth = FALSE;
        return level > 1 && service == 0 ? RPC_S_UNKNOWN_AUTHN_SERVICE : RPC_S_OK;
    }
    /* none 0, negotiate 9, ntlm 10, schannel 14, kerberos 16, default 0xFFFFFFFF. */
    if (service != 9 && service != 10 && service != 14 && service != 16 && service != 0xFFFFFFFFu)
        return RPC_S_UNKNOWN_AUTHN_SERVICE;
    PWSTR copy = principal ? RpcpCopyString(principal) : nullptr;
    if (principal && !copy)
        return ERROR_OUTOFMEMORY;
    RpcpFreeString(b->Principal);
    b->Principal = copy;
    b->HasAuth = TRUE;
    b->AuthnLevel = level;
    b->AuthnService = service;
    b->AuthzService = authz;
    b->ImpersonationType = qos ? qos->ImpersonationType : RPC_C_IMP_LEVEL_IMPERSONATE;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoW(RPC_BINDING_HANDLE handle, RPC_WSTR principal, ULONG level,
                                                     ULONG service, void *identity, ULONG authz)
{
    return set_auth(handle, principal, level, service, identity, authz, nullptr);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoA(RPC_BINDING_HANDLE handle, RPC_CSTR principal, ULONG level,
                                                     ULONG service, void *identity, ULONG authz)
{
    PWSTR wide = RpcpWiden(principal);
    if (principal && !wide)
        return ERROR_OUTOFMEMORY;
    RPC_STATUS status = set_auth(handle, wide, level, service, identity, authz, nullptr);
    RpcpFreeString(wide);
    return status;
}

/* the extended form carries a structure that starts with its version and size. */
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoExW(RPC_BINDING_HANDLE handle, RPC_WSTR principal, ULONG level,
                                                       ULONG service, void *identity, ULONG authz,
                                                       RPC_SECURITY_QOS *qos)
{
    if (qos && (qos->Version < 1 || qos->Version > 3))
        return ERROR_INVALID_PARAMETER;
    return set_auth(handle, principal, level, service, identity, authz, qos);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoExA(RPC_BINDING_HANDLE handle, RPC_CSTR principal, ULONG level,
                                                       ULONG service, void *identity, ULONG authz,
                                                       RPC_SECURITY_QOS *qos)
{
    PWSTR wide = RpcpWiden(principal);
    if (principal && !wide)
        return ERROR_OUTOFMEMORY;
    RPC_STATUS status = RpcBindingSetAuthInfoExW(handle, wide, level, service, identity, authz, qos);
    RpcpFreeString(wide);
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthInfoExW(RPC_BINDING_HANDLE handle, RPC_WSTR *principal, ULONG *level,
                                                       ULONG *service, void **identity, ULONG *authz, ULONG qos_version,
                                                       RPC_SECURITY_QOS *qos)
{
    BINDING *b = RpcpBindingLookup(handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (!b->HasAuth)
        return RPC_S_BINDING_HAS_NO_AUTH;
    if (principal) {
        *principal = b->Principal ? RpcpCopyString(b->Principal) : nullptr;
        if (b->Principal && !*principal)
            return ERROR_OUTOFMEMORY;
    }
    if (level)
        *level = b->AuthnLevel;
    if (service)
        *service = b->AuthnService;
    if (identity)
        *identity = nullptr;
    if (authz)
        *authz = b->AuthzService;
    if (qos && qos_version) {
        memset(qos, 0, sizeof(*qos));
        qos->Version = qos_version;
        qos->ImpersonationType = b->ImpersonationType;
    }
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthInfoExA(RPC_BINDING_HANDLE handle, RPC_CSTR *principal, ULONG *level,
                                                       ULONG *service, void **identity, ULONG *authz, ULONG qos_version,
                                                       RPC_SECURITY_QOS *qos)
{
    PWSTR wide = nullptr;
    RPC_STATUS status = RpcBindingInqAuthInfoExW(handle, principal ? &wide : nullptr, level, service, identity, authz,
                                                 qos_version, qos);
    if (!status && principal) {
        *principal = RpcpNarrow(wide);
        if (wide && !*principal)
            status = ERROR_OUTOFMEMORY;
    }
    return status;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthInfoW(RPC_BINDING_HANDLE handle, RPC_WSTR *principal, ULONG *level,
                                                     ULONG *service, void **identity, ULONG *authz)
{
    return RpcBindingInqAuthInfoExW(handle, principal, level, service, identity, authz, 0, nullptr);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthInfoA(RPC_BINDING_HANDLE handle, RPC_CSTR *principal, ULONG *level,
                                                     ULONG *service, void **identity, ULONG *authz)
{
    return RpcBindingInqAuthInfoExA(handle, principal, level, service, identity, authz, 0, nullptr);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqDefaultProtectLevel(ULONG service, ULONG *level)
{
    if (!level)
        return ERROR_INVALID_PARAMETER;
    switch (service) {
    case RPC_C_AUTHN_WINNT:
    case RPC_C_AUTHN_GSS_NEGOTIATE:
    case RPC_C_AUTHN_GSS_KERBEROS:
    case RPC_C_AUTHN_GSS_SCHANNEL:
    case RPC_C_AUTHN_DEFAULT:
        *level = RPC_C_AUTHN_LEVEL_PKT_PRIVACY;
        return RPC_S_OK;
    case RPC_C_AUTHN_NONE:
        *level = RPC_C_AUTHN_LEVEL_NONE;
        return RPC_S_OK;
    default:
        return RPC_S_UNKNOWN_AUTHN_SERVICE;
    }
}
