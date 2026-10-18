/*
 * the internet security manager: urls mapped to security zones through
 * internet explorer's zone map, and the zones' url action policies.
 */
#include <mime/win32/objbase.h>
#include <mime/win32/winbase.h>

#define URLZONE_INVALID ((DWORD)-1)
#define URLZONE_LOCAL_MACHINE 0
#define URLZONE_INTRANET 1
#define URLZONE_TRUSTED 2
#define URLZONE_INTERNET 3
#define URLZONE_UNTRUSTED 4

#define URLPOLICY_ALLOW 0x00
#define URLPOLICY_DISALLOW 0x03
#define URLPOLICY_MASK_PERMISSIONS 0x0F

#define INET_E_DEFAULT_ACTION ((HRESULT)0x800C0011L)
#define MAX_SIZE_SECURITY_ID 512

#define INTERNET_SETTINGS L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"

typedef struct IInternetSecurityManager IInternetSecurityManager;
typedef struct IInternetSecurityMgrSite IInternetSecurityMgrSite;

typedef struct IInternetSecurityManagerVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IInternetSecurityManager *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IInternetSecurityManager *);
    ULONG(STDMETHODCALLTYPE *Release)(IInternetSecurityManager *);
    HRESULT(STDMETHODCALLTYPE *SetSecuritySite)(IInternetSecurityManager *, IInternetSecurityMgrSite *);
    HRESULT(STDMETHODCALLTYPE *GetSecuritySite)(IInternetSecurityManager *, IInternetSecurityMgrSite **);
    HRESULT(STDMETHODCALLTYPE *MapUrlToZone)(IInternetSecurityManager *, LPCWSTR, DWORD *, DWORD);
    HRESULT(STDMETHODCALLTYPE *GetSecurityId)(IInternetSecurityManager *, LPCWSTR, BYTE *, DWORD *, DWORD_PTR);
    HRESULT(STDMETHODCALLTYPE *ProcessUrlAction)(IInternetSecurityManager *, LPCWSTR, DWORD, BYTE *, DWORD, BYTE *,
                                                 DWORD, DWORD, DWORD);
    HRESULT(STDMETHODCALLTYPE *QueryCustomPolicy)(IInternetSecurityManager *, LPCWSTR, REFGUID, BYTE **, DWORD *,
                                                  BYTE *, DWORD, DWORD);
    HRESULT(STDMETHODCALLTYPE *SetZoneMapping)(IInternetSecurityManager *, DWORD, LPCWSTR, DWORD);
    HRESULT(STDMETHODCALLTYPE *GetZoneMappings)(IInternetSecurityManager *, DWORD, IUnknown **, DWORD);
} IInternetSecurityManagerVtbl;

struct IInternetSecurityManager {
    const IInternetSecurityManagerVtbl *lpVtbl;
};

typedef struct {
    IInternetSecurityManager Interface;
    LONG volatile Refs;
    IInternetSecurityMgrSite *Site;
} SECURITY_MANAGER;

static const IID IID_IInternetSecurityManager = {
    0x79eac9ee, 0xbaf9, 0x11ce, {0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9, 0x0b}};

static SECURITY_MANAGER *manager_of(IInternetSecurityManager *iface)
{
    return (SECURITY_MANAGER *)iface;
}

static HRESULT STDMETHODCALLTYPE sm_QueryInterface(IInternetSecurityManager *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IInternetSecurityManager)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE sm_AddRef(IInternetSecurityManager *iface)
{
    return (ULONG)InterlockedIncrement(&manager_of(iface)->Refs);
}

static ULONG STDMETHODCALLTYPE sm_Release(IInternetSecurityManager *iface)
{
    SECURITY_MANAGER *manager = manager_of(iface);
    LONG refs = InterlockedDecrement(&manager->Refs);
    if (!refs) {
        if (manager->Site)
            ((IUnknown *)manager->Site)->lpVtbl->Release((IUnknown *)manager->Site);
        HeapFree(GetProcessHeap(), 0, manager);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE sm_SetSecuritySite(IInternetSecurityManager *iface, IInternetSecurityMgrSite *site)
{
    SECURITY_MANAGER *manager = manager_of(iface);
    if (site)
        ((IUnknown *)site)->lpVtbl->AddRef((IUnknown *)site);
    if (manager->Site)
        ((IUnknown *)manager->Site)->lpVtbl->Release((IUnknown *)manager->Site);
    manager->Site = site;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sm_GetSecuritySite(IInternetSecurityManager *iface, IInternetSecurityMgrSite **site)
{
    if (!site)
        return E_INVALIDARG;
    *site = manager_of(iface)->Site;
    if (*site)
        ((IUnknown *)*site)->lpVtbl->AddRef((IUnknown *)*site);
    return S_OK;
}

typedef struct {
    WCHAR Scheme[32];
    WCHAR Host[256];
    BOOL File;
    BOOL Unc;
} URL_PARTS;

static BOOL is_alpha(WCHAR c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

/* the scheme and host of a url; a dos or unc path counts as a file url. */
static BOOL split_url(LPCWSTR url, URL_PARTS *parts)
{
    memset(parts, 0, sizeof(*parts));
    if (is_alpha(url[0]) && url[1] == L':' && (url[2] == L'\\' || url[2] == L'/' || !url[2])) {
        wcscpy(parts->Scheme, L"file");
        parts->File = TRUE;
        return TRUE;
    }
    if (url[0] == L'\\' && url[1] == L'\\') {
        wcscpy(parts->Scheme, L"file");
        parts->File = parts->Unc = TRUE;
        url += 2;
    } else {
        size_t n = 0;
        while (url[n] && url[n] != L':' && n < RTL_NUMBER_OF(parts->Scheme) - 1)
            n++;
        if (url[n] != L':' || n < 2)
            return FALSE;
        for (size_t i = 0; i < n; i++)
            parts->Scheme[i] = (WCHAR)(url[i] | 0x20);
        url += n + 1;
        parts->File = !wcscmp(parts->Scheme, L"file");
        if (url[0] != L'/' || url[1] != L'/')
            return TRUE;
        url += 2;
        if (parts->File) {
            if (url[0] == L'/' || (is_alpha(url[0]) && (url[1] == L':' || url[1] == L'|')))
                return TRUE;
            if (!_wcsnicmp(url, L"localhost/", 10))
                return TRUE;
            parts->Unc = TRUE;
        }
    }
    LPCWSTR end = url;
    while (*end && *end != L'/' && *end != L'\\' && *end != L'?' && *end != L'#')
        end++;
    LPCWSTR at = end;
    while (at > url && at[-1] != L'@')
        at--;
    LPCWSTR host = at;
    LPCWSTR colon = host;
    while (colon < end && *colon != L':')
        colon++;
    size_t length = (size_t)(colon - host);
    if (length >= RTL_NUMBER_OF(parts->Host))
        length = RTL_NUMBER_OF(parts->Host) - 1;
    for (size_t i = 0; i < length; i++)
        parts->Host[i] = host[i] >= L'A' && host[i] <= L'Z' ? (WCHAR)(host[i] | 0x20) : host[i];
    return TRUE;
}

static BOOL read_dword(HKEY root, LPCWSTR key, LPCWSTR value, DWORD *out)
{
    DWORD size = sizeof(*out);
    return !RegGetValueW(root, key, value, RRF_RT_REG_DWORD, nullptr, out, &size);
}

/* a zone map entry of a key: the scheme's own value, then the wildcard. */
static BOOL mapped_zone(LPCWSTR key, LPCWSTR scheme, DWORD *zone)
{
    static const HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (int i = 0; i < 2; i++)
        if (read_dword(roots[i], key, scheme, zone) || read_dword(roots[i], key, L"*", zone))
            return TRUE;
    return FALSE;
}

/*
 * the zone map keys a host by its last two labels, with the labels before
 * them as a subkey: www.example.com is Domains\example.com\www.
 */
static BOOL domain_zone(const URL_PARTS *parts, DWORD *zone)
{
    LPCWSTR host = parts->Host, dot = nullptr, last = nullptr;
    for (LPCWSTR p = host; *p; p++) {
        if (*p == L'.') {
            dot = last;
            last = p;
        }
    }
    LPCWSTR domain = dot ? dot + 1 : host;
    WCHAR key[512];
    if (domain != host) {
        _snwprintf(key, RTL_NUMBER_OF(key), INTERNET_SETTINGS L"\\ZoneMap\\Domains\\%ls\\%.*ls", domain,
                   (int)(domain - host - 1), host);
        if (mapped_zone(key, parts->Scheme, zone))
            return TRUE;
    }
    _snwprintf(key, RTL_NUMBER_OF(key), INTERNET_SETTINGS L"\\ZoneMap\\Domains\\%ls", domain);
    return mapped_zone(key, parts->Scheme, zone);
}

static HRESULT url_zone(LPCWSTR url, DWORD *zone)
{
    URL_PARTS parts;
    if (!split_url(url, &parts))
        return E_INVALIDARG;
    if (parts.File) {
        *zone = parts.Unc ? URLZONE_INTRANET : URLZONE_LOCAL_MACHINE;
        return S_OK;
    }
    if (parts.Host[0] && domain_zone(&parts, zone))
        return S_OK;
    /* a plain host name without dots is on the intranet, as windows' IntranetName default. */
    *zone = parts.Host[0] && !wcschr(parts.Host, L'.') ? URLZONE_INTRANET : URLZONE_INTERNET;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sm_MapUrlToZone(IInternetSecurityManager *iface, LPCWSTR url, DWORD *zone,
                                                 DWORD flags)
{
    if (!zone)
        return E_INVALIDARG;
    *zone = URLZONE_INVALID;
    if (!url)
        return E_INVALIDARG;
    return url_zone(url, zone);
}

/* the ansi "scheme:host" of the url followed by its zone. */
static HRESULT STDMETHODCALLTYPE sm_GetSecurityId(IInternetSecurityManager *iface, LPCWSTR url, BYTE *id,
                                                  DWORD *size, DWORD_PTR reserved)
{
    if (!url || !id || !size)
        return E_INVALIDARG;
    URL_PARTS parts;
    DWORD zone;
    HRESULT hr = url_zone(url, &zone);
    if (FAILED(hr) || !split_url(url, &parts))
        return FAILED(hr) ? hr : E_INVALIDARG;
    WCHAR text[300];
    _snwprintf(text, RTL_NUMBER_OF(text), L"%ls:%ls", parts.Scheme, parts.Host);
    int length = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr) - 1;
    DWORD needed = (DWORD)length + sizeof(DWORD);
    if (*size < needed) {
        *size = needed;
        return E_NOT_SUFFICIENT_BUFFER;
    }
    WideCharToMultiByte(CP_ACP, 0, text, -1, (char *)id, length, nullptr, nullptr);
    memcpy(id + length, &zone, sizeof(zone));
    *size = needed;
    return S_OK;
}

/* a zone's own policy for an action from the registry, else the default of its template. */
static DWORD action_policy(DWORD zone, DWORD action)
{
    WCHAR key[160], value[16];
    _snwprintf(key, RTL_NUMBER_OF(key), INTERNET_SETTINGS L"\\Zones\\%lu", zone);
    _snwprintf(value, RTL_NUMBER_OF(value), L"%04lX", action);
    DWORD policy;
    if (read_dword(HKEY_CURRENT_USER, key, value, &policy) || read_dword(HKEY_LOCAL_MACHINE, key, value, &policy))
        return policy;
    return zone == URLZONE_UNTRUSTED ? URLPOLICY_DISALLOW : URLPOLICY_ALLOW;
}

static HRESULT STDMETHODCALLTYPE sm_ProcessUrlAction(IInternetSecurityManager *iface, LPCWSTR url, DWORD action,
                                                     BYTE *policy, DWORD policy_size, BYTE *context,
                                                     DWORD context_size, DWORD flags, DWORD reserved)
{
    if (!url)
        return E_INVALIDARG;
    DWORD zone;
    HRESULT hr = url_zone(url, &zone);
    if (FAILED(hr))
        return hr;
    DWORD value = action_policy(zone, action);
    if (policy && policy_size >= sizeof(DWORD))
        memcpy(policy, &value, sizeof(value));
    return (value & URLPOLICY_MASK_PERMISSIONS) == URLPOLICY_ALLOW ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE sm_QueryCustomPolicy(IInternetSecurityManager *iface, LPCWSTR url, REFGUID key,
                                                      BYTE **policy, DWORD *policy_size, BYTE *context,
                                                      DWORD context_size, DWORD reserved)
{
    return INET_E_DEFAULT_ACTION;
}

static HRESULT STDMETHODCALLTYPE sm_SetZoneMapping(IInternetSecurityManager *iface, DWORD zone, LPCWSTR pattern,
                                                   DWORD flags)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sm_GetZoneMappings(IInternetSecurityManager *iface, DWORD zone,
                                                    IUnknown **enumerator, DWORD flags)
{
    if (enumerator)
        *enumerator = nullptr;
    return E_NOTIMPL;
}

static const IInternetSecurityManagerVtbl security_manager_vtbl = {
    sm_QueryInterface,  sm_AddRef,           sm_Release,          sm_SetSecuritySite,
    sm_GetSecuritySite, sm_MapUrlToZone,     sm_GetSecurityId,    sm_ProcessUrlAction,
    sm_QueryCustomPolicy, sm_SetZoneMapping, sm_GetZoneMappings,
};

DECLSPEC_EXPORT HRESULT WINAPI CoInternetCreateSecurityManager(IUnknown *provider, IInternetSecurityManager **out,
                                                               DWORD reserved)
{
    if (!out)
        return E_INVALIDARG;
    SECURITY_MANAGER *manager = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*manager));
    if (!manager) {
        *out = nullptr;
        return E_OUTOFMEMORY;
    }
    manager->Interface.lpVtbl = &security_manager_vtbl;
    manager->Refs = 1;
    *out = &manager->Interface;
    return S_OK;
}

/* the zone of a url for IUri::GetZone, URLZONE_INVALID when it maps to none. */
DWORD UrlZoneOf(LPCWSTR url)
{
    DWORD zone = URLZONE_INVALID;
    if (FAILED(url_zone(url, &zone)))
        zone = URLZONE_INVALID;
    return zone;
}
