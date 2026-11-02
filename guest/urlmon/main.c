/* urlmon entry point, memory helpers, and session options with the user agent. */
#include "urlmon_private.h"

static SRWLOCK SessionLock = SRWLOCK_INIT;
static char *Agent;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

void *UrlAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void *UrlAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *UrlRealloc(void *memory, SIZE_T size)
{
    if (!memory)
        return UrlAlloc(size);
    return HeapReAlloc(GetProcessHeap(), 0, memory, size ? size : 1);
}

void UrlFree(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

PWSTR UrlDuplicateN(LPCWSTR text, DWORD length)
{
    if (!text)
        return nullptr;
    PWSTR copy = UrlAlloc(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, text, (SIZE_T)length * sizeof(WCHAR));
        copy[length] = 0;
    }
    return copy;
}

PWSTR UrlDuplicate(LPCWSTR text)
{
    return text ? UrlDuplicateN(text, (DWORD)lstrlenW(text)) : nullptr;
}

PWSTR UrlWiden(LPCSTR text)
{
    if (!text)
        return nullptr;
    int chars = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    PWSTR wide = UrlAlloc((SIZE_T)chars * sizeof(WCHAR));
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, text, -1, wide, chars);
    return wide;
}

/* each value name under the key becomes a "; name" token. */
static void AppendTokens(char *agent, SIZE_T capacity, HKEY root, const char *key_name)
{
    HKEY key;
    if (RegOpenKeyExA(root, key_name, 0, KEY_READ, &key))
        return;
    char name[128];
    for (DWORD i = 0;; i++) {
        DWORD length = sizeof(name);
        if (RegEnumValueA(key, i, name, &length, nullptr, nullptr, nullptr, nullptr))
            break;
        if (!length || strlen(agent) + length + 3 >= capacity)
            continue;
        lstrcatA(agent, "; ");
        lstrcatA(agent, name);
    }
    RegCloseKey(key);
}

/*
 * the internet explorer 7 compatible agent windows gives programs that host no browser, plus the tokens of internet
 * explorer's Post Platform keys.
 */
static char *BuildAgent(void)
{
    BOOL wow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &wow64);
    const char *platform = sizeof(void *) == 8 ? "Win64; x64" : wow64 ? "WOW64" : nullptr;
    char *agent = UrlAlloc(1024);
    if (!agent)
        return nullptr;
    lstrcpyA(agent, "Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 10.0");
    if (platform) {
        lstrcatA(agent, "; ");
        lstrcatA(agent, platform);
    }
    lstrcatA(agent, "; Trident/7.0");
    static const char key[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\5.0\\User Agent\\"
                              "Post Platform";
    AppendTokens(agent, 1000, HKEY_LOCAL_MACHINE, key);
    AppendTokens(agent, 1000, HKEY_CURRENT_USER, key);
    lstrcatA(agent, ")");
    return agent;
}

const char *UrlUserAgent(void)
{
    AcquireSRWLockExclusive(&SessionLock);
    if (!Agent)
        Agent = BuildAgent();
    const char *agent = Agent ? Agent : "Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 10.0)";
    ReleaseSRWLockExclusive(&SessionLock);
    return agent;
}

/* too small a buffer gives E_OUTOFMEMORY and the size needed, terminator included. */
DECLSPEC_EXPORT HRESULT WINAPI ObtainUserAgentString(DWORD option, LPSTR agent, DWORD *size)
{
    if (!agent || !size)
        return E_INVALIDARG;
    const char *text = UrlUserAgent();
    DWORD length = (DWORD)strlen(text);
    HRESULT hr = E_OUTOFMEMORY;
    if (*size > length) {
        lstrcpynA(agent, text, (int)*size);
        hr = S_OK;
    }
    *size = length + 1;
    return hr;
}

/* windows answers the user agent with E_OUTOFMEMORY even when it fits. */
DECLSPEC_EXPORT HRESULT WINAPI UrlMkGetSessionOption(DWORD option, LPVOID buffer, DWORD length, DWORD *length_out,
                                                    DWORD reserved)
{
    if (!length_out)
        return E_INVALIDARG;
    if (option == URLMON_OPTION_USERAGENT) {
        const char *text = UrlUserAgent();
        DWORD chars = (DWORD)strlen(text);
        *length_out = chars + 1;
        if (chars < length) {
            if (!buffer)
                return E_INVALIDARG;
            lstrcpynA(buffer, text, (int)length);
        }
        return E_OUTOFMEMORY;
    }
    if (!buffer || length < sizeof(DWORD))
        return E_INVALIDARG;
    if (option == URLMON_OPTION_URL_ENCODING) {
        *(DWORD *)buffer = URL_ENCODING_ENABLE_UTF8;
    } else if (option == URLMON_OPTION_USE_BINDSTRINGCREDS || option == URLMON_OPTION_USE_BROWSERAPPSDOCUMENTS) {
        *(DWORD *)buffer = 1;
    } else {
        return E_INVALIDARG;
    }
    *length_out = sizeof(DWORD);
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI UrlMkSetSessionOption(DWORD option, LPVOID buffer, DWORD length, DWORD reserved)
{
    if (option == URLMON_OPTION_USERAGENT) {
        if (!buffer || !length)
            return E_INVALIDARG;
        char *copy = UrlAlloc((SIZE_T)length + 1);
        if (!copy)
            return E_OUTOFMEMORY;
        memcpy(copy, buffer, length);
        copy[length] = 0;
        /* the old agent is never freed, requests may still be reading it. */
        AcquireSRWLockExclusive(&SessionLock);
        Agent = copy;
        ReleaseSRWLockExclusive(&SessionLock);
        return S_OK;
    }
    if (option == URLMON_OPTION_USERAGENT_REFRESH) {
        char *fresh = BuildAgent();
        AcquireSRWLockExclusive(&SessionLock);
        if (fresh)
            Agent = fresh;
        ReleaseSRWLockExclusive(&SessionLock);
        return S_OK;
    }
    if (option == URLMON_OPTION_URL_ENCODING)
        return buffer && length >= sizeof(DWORD) ? S_OK : E_INVALIDARG;
    return E_INVALIDARG;
}

/* frees what a callback gave in its bind info and clears it, the size stays. */
DECLSPEC_EXPORT void WINAPI ReleaseBindInfo(BINDINFO *info)
{
    if (!info || !info->cbSize)
        return;
    ULONG size = info->cbSize;
    IUnknown *unknown = size >= sizeof(BINDINFO) ? info->pUnk : nullptr;
    CoTaskMemFree(info->szExtraInfo);
    ReleaseStgMedium(&info->stgmedData);
    CoTaskMemFree(info->szCustomVerb);
    if (unknown)
        unknown->lpVtbl->Release(unknown);
    memset(info, 0, size < sizeof(BINDINFO) ? size : sizeof(BINDINFO));
    info->cbSize = size;
}

static LPWSTR TaskDuplicate(LPCWSTR text)
{
    if (!text)
        return nullptr;
    SIZE_T bytes = ((SIZE_T)lstrlenW(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = CoTaskMemAlloc(bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

DECLSPEC_EXPORT HRESULT WINAPI CopyBindInfo(const BINDINFO *source, BINDINFO *dest)
{
    if (!source || !dest)
        return E_POINTER;
    if (source->cbSize < sizeof(BINDINFO) || dest->cbSize < sizeof(BINDINFO))
        return E_INVALIDARG;
    ULONG size = dest->cbSize;
    memcpy(dest, source, sizeof(BINDINFO));
    dest->cbSize = size;
    dest->szExtraInfo = TaskDuplicate(source->szExtraInfo);
    dest->szCustomVerb = TaskDuplicate(source->szCustomVerb);
    dest->pUnk = source->pUnk;
    if (dest->pUnk)
        dest->pUnk->lpVtbl->AddRef(dest->pUnk);
    if (source->stgmedData.tymed == TYMED_HGLOBAL && source->stgmedData.hGlobal) {
        SIZE_T bytes = GlobalSize(source->stgmedData.hGlobal);
        HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, bytes);
        void *to = copy ? GlobalLock(copy) : nullptr;
        void *from = GlobalLock(source->stgmedData.hGlobal);
        if (to && from)
            memcpy(to, from, bytes);
        if (to)
            GlobalUnlock(copy);
        GlobalUnlock(source->stgmedData.hGlobal);
        dest->stgmedData.hGlobal = copy;
        dest->stgmedData.pUnkForRelease = nullptr;
    } else if (source->stgmedData.tymed == TYMED_ISTREAM || source->stgmedData.tymed == TYMED_ISTORAGE) {
        if (source->stgmedData.pstm)
            source->stgmedData.pstm->lpVtbl->AddRef(source->stgmedData.pstm);
    }
    return S_OK;
}

/* the flags internet explorer keeps for an activex control, 0 when it has no entry. */
DECLSPEC_EXPORT HRESULT WINAPI CompatFlagsFromClsid(CLSID *clsid, LPDWORD compat_flags, LPDWORD misc_status_flags)
{
    if (!clsid || !compat_flags || !misc_status_flags)
        return E_INVALIDARG;
    *compat_flags = 0;
    *misc_status_flags = 0;
    WCHAR guid[40];
    if (!StringFromGUID2(clsid, guid, 40))
        return E_INVALIDARG;
    WCHAR path[128];
    wsprintfW(path, L"Software\\Microsoft\\Internet Explorer\\ActiveX Compatibility\\%s", guid);
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key))
        return S_OK;
    DWORD size = sizeof(DWORD), type;
    if (RegQueryValueExW(key, L"Compatibility Flags", nullptr, &type, (BYTE *)compat_flags, &size) || type != REG_DWORD)
        *compat_flags = 0;
    size = sizeof(DWORD);
    if (RegQueryValueExW(key, L"MiscStatus", nullptr, &type, (BYTE *)misc_status_flags, &size) || type != REG_DWORD)
        *misc_status_flags = 0;
    RegCloseKey(key);
    return S_OK;
}
