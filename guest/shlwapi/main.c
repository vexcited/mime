/* shlwapi entry point, version, class objects and shared helpers. */
#include "shlwapi_private.h"

HRESULT ShlAssocCreateInstance(REFIID riid, void **out);

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

FARPROC ShlProc(LPCWSTR dll, LPCSTR name)
{
    HMODULE module = GetModuleHandleW(dll);
    if (!module)
        module = LoadLibraryW(dll);
    return module ? GetProcAddress(module, name) : nullptr;
}

void *ShlAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

void ShlFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

void *ShlTaskAlloc(SIZE_T size)
{
    void *(WINAPI * alloc)(SIZE_T) = (void *(WINAPI *)(SIZE_T))ShlProc(L"ole32.dll", "CoTaskMemAlloc");
    return alloc ? alloc(size) : nullptr;
}

void ShlTaskFree(void *p)
{
    void(WINAPI * free_task)(void *) = (void(WINAPI *)(void *))ShlProc(L"ole32.dll", "CoTaskMemFree");
    if (p && free_task)
        free_task(p);
}

LPWSTR ShlWide(LPCSTR s)
{
    if (!s)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    LPWSTR w = ShlAlloc((SIZE_T)n * sizeof(WCHAR));
    if (w)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}

/* shlwapi reports 10.0.10586 whatever the build, like windows 10's. */
HRESULT WINAPI DllGetVersion(DLLVERSIONINFO *info)
{
    if (!info || (info->cbSize != sizeof(DLLVERSIONINFO) && info->cbSize != sizeof(DLLVERSIONINFO2)))
        return E_INVALIDARG;
    info->dwMajorVersion = 10;
    info->dwMinorVersion = 0;
    info->dwBuildNumber = 10586;
    info->dwPlatformID = DLLVER_PLATFORM_WINDOWS;
    if (info->cbSize == sizeof(DLLVERSIONINFO2)) {
        DLLVERSIONINFO2 *info2 = (DLLVERSIONINFO2 *)info;
        info2->dwFlags = 0;
        info2->ullVersion = (10ull << 48) | (10586ull << 16);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *out = This;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return ShlAssocCreateInstance(riid, out);
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl factory_vtbl = {factory_query, factory_add_ref, factory_release, factory_create,
                                               factory_lock};
static IClassFactory assoc_factory = {&factory_vtbl};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualCLSID(clsid, &CLSID_QueryAssociations))
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory_query(&assoc_factory, riid, out);
}

/* PLATFORM_INTEGRATED: the shell and the browser as one. */
UINT WINAPI WhichPlatform(void)
{
    return 2;
}

/* 6 since windows xp, a guess. */
UINT WINAPI GetUIVersion(void);
UINT WINAPI GetUIVersion(void)
{
    return 6;
}

BOOL WINAPI SHIsLowMemoryMachine(DWORD type)
{
    return FALSE;
}

BOOL WINAPI IsAppCompatModeEnabled(DWORD mode);
BOOL WINAPI IsAppCompatModeEnabled(DWORD mode)
{
    return FALSE;
}

DWORD WINAPI SHGetObjectCompatFlags(IUnknown *object, const CLSID *clsid);
DWORD WINAPI SHGetObjectCompatFlags(IUnknown *object, const CLSID *clsid)
{
    return 0;
}

BOOL WINAPI SHWindowsPolicy(REFGUID policy);
BOOL WINAPI SHWindowsPolicy(REFGUID policy)
{
    return FALSE;
}

HRESULT WINAPI SHWindowsPolicyGetValue(REFGUID policy, DWORD *value, DWORD size);
HRESULT WINAPI SHWindowsPolicyGetValue(REFGUID policy, DWORD *value, DWORD size)
{
    if (value && size >= sizeof(DWORD))
        *value = 0;
    return E_FAIL;
}

/* no dock, no battery worth reporting. */
DWORD WINAPI SHGetMachineInfo(UINT what);
DWORD WINAPI SHGetMachineInfo(UINT what)
{
    return 0;
}

FARPROC WINAPI DelayLoadFailureHook(LPCSTR dll, LPCSTR function);
FARPROC WINAPI DelayLoadFailureHook(LPCSTR dll, LPCSTR function)
{
    return nullptr;
}

/* language resources live next to the dll here, so this is plain LoadLibraryEx. */
HINSTANCE WINAPI MLLoadLibraryW(LPCWSTR name, HMODULE parent, DWORD flags)
{
    return LoadLibraryExW(name, nullptr, 0);
}

HINSTANCE WINAPI MLLoadLibraryA(LPCSTR name, HMODULE parent, DWORD flags)
{
    return LoadLibraryExA(name, nullptr, 0);
}

/* loads the class's in process server and never frees it. */
HMODULE WINAPI SHPinDllOfCLSID(const CLSID *clsid);
HMODULE WINAPI SHPinDllOfCLSID(const CLSID *clsid)
{
    if (!clsid)
        return nullptr;
    WCHAR key[80], path[MAX_PATH];
    wsprintfW(key, L"CLSID\\{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\\InprocServer32",
              (unsigned long)clsid->Data1, clsid->Data2, clsid->Data3, clsid->Data4[0], clsid->Data4[1],
              clsid->Data4[2], clsid->Data4[3], clsid->Data4[4], clsid->Data4[5], clsid->Data4[6], clsid->Data4[7]);
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_CLASSES_ROOT, key, nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, path, &size))
        return nullptr;
    return LoadLibraryW(path);
}

/* process wide, indexes wrap around. */
static LONG volatile counters[64];

LONG WINAPI SHGlobalCounterGetValue(int counter);
LONG WINAPI SHGlobalCounterGetValue(int counter)
{
    return counters[(unsigned)counter % RTL_NUMBER_OF(counters)];
}

LONG WINAPI SHGlobalCounterIncrement(int counter);
LONG WINAPI SHGlobalCounterIncrement(int counter)
{
    return InterlockedIncrement(&counters[(unsigned)counter % RTL_NUMBER_OF(counters)]);
}

LONG WINAPI SHGlobalCounterDecrement(int counter);
LONG WINAPI SHGlobalCounterDecrement(int counter)
{
    return InterlockedDecrement(&counters[(unsigned)counter % RTL_NUMBER_OF(counters)]);
}

#ifdef _WIN64
/* x64 only exports. */
PVOID WINAPI SHInterlockedCompareExchange(PVOID volatile *destination, PVOID exchange, PVOID comparand);
PVOID WINAPI SHInterlockedCompareExchange(PVOID volatile *destination, PVOID exchange, PVOID comparand)
{
    return InterlockedCompareExchangePointer(destination, exchange, comparand);
}

LPWSTR WINAPI ualstrcpynW(LPWSTR dest, LPCWSTR src, int count);
LPWSTR WINAPI ualstrcpynW(LPWSTR dest, LPCWSTR src, int count)
{
    return lstrcpynW(dest, src, count);
}

int WINAPI ualstrcmpiW(LPCWSTR a, LPCWSTR b);
int WINAPI ualstrcmpiW(LPCWSTR a, LPCWSTR b)
{
    return lstrcmpiW(a, b);
}

int WINAPI ualstrcmpW(LPCWSTR a, LPCWSTR b);
int WINAPI ualstrcmpW(LPCWSTR a, LPCWSTR b)
{
    return lstrcmpW(a, b);
}

int WINAPI ualstrlenW(LPCWSTR s);
int WINAPI ualstrlenW(LPCWSTR s)
{
    return lstrlenW(s);
}

LPWSTR WINAPI ualstrcpyW(LPWSTR dest, LPCWSTR src);
LPWSTR WINAPI ualstrcpyW(LPWSTR dest, LPCWSTR src)
{
    return lstrcpyW(dest, src);
}
#endif
