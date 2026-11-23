#include "evr_private.h"

MIME_DEFINE_GUID(CLSID_EnhancedVideoRenderer_, 0xFA10746C, 0x9B63, 0x4B6C, 0xBC, 0x49, 0xFC, 0x30, 0x0E, 0xA5, 0xF2,
                 0x56);
MIME_DEFINE_GUID(CLSID_EVRGraphOptimizer_, 0x62079164, 0x233B, 0x41F8, 0xA8, 0x0F, 0xF0, 0x17, 0x05, 0xF5, 0x14, 0xA8);
MIME_DEFINE_GUID(CLSID_FilterMapper2_, 0xCDA42200, 0xBD88, 0x11D0, 0xBD, 0x4E, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86);
MIME_DEFINE_GUID(IID_IFilterMapper2_, 0xB79BB0B0, 0x33C1, 0x11D1, 0xAB, 0xE1, 0x00, 0xA0, 0xC9, 0x05, 0xF3, 0x75);
MIME_DEFINE_GUID(CLSID_FilterMapper_, 0xE436EBB2, 0x524F, 0x11CE, 0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70);
MIME_DEFINE_GUID(IID_IFilterMapper_, 0x56A868A3, 0x0AD4, 0x11CE, 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70);

/* evr's g_Templates. only the graph optimizer has a name; the five others are registered with an empty one. */
static const struct {
    const WCHAR *name;
    const GUID *clsid;
} TEMPLATES[] = {
    {L"", &CLSID_EnhancedVideoRenderer_},
    {L"", &CLSID_MFVideoMixer9},
    {L"", &CLSID_MFVideoPresenter9},
    {L"", &CLSID_MFVideoPresenter9FlipMode},
    {L"", &CLSID_EVRTearlessWindowPresenter9},
    {L"EVR Graph Optimizer", &CLSID_EVRGraphOptimizer_},
};

static void clsid_key(const GUID *clsid, WCHAR *key)
{
    static const WCHAR PREFIX[] = L"CLSID\\";
    WCHAR text[40];
    StringFromGUID2(clsid, text, 39);
    memcpy(key, PREFIX, sizeof(PREFIX));
    memcpy(key + RTL_NUMBER_OF(PREFIX) - 1, text, sizeof(text));
}

static HRESULT set_string(HKEY key, const WCHAR *name, const WCHAR *value)
{
    DWORD length = 0;
    while (value[length])
        length++;
    LSTATUS status = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, 2 * length + 2);
    return status ? HRESULT_FROM_WIN32(status) : S_OK;
}

/* AMovieSetupRegisterServer: the class name, the module as its in-process server, and the threading model Both. */
static HRESULT register_server(const GUID *clsid, const WCHAR *name, const WCHAR *path)
{
    WCHAR subkey[64];
    HKEY key = nullptr, server = nullptr;
    clsid_key(clsid, subkey);
    LSTATUS status = RegCreateKeyExW(HKEY_CLASSES_ROOT, subkey, 0, nullptr, 0, MAXIMUM_ALLOWED, nullptr, &key, nullptr);
    if (status)
        return HRESULT_FROM_WIN32(status);
    HRESULT hr = set_string(key, nullptr, name);
    if (SUCCEEDED(hr)) {
        status = RegCreateKeyExW(key, L"InprocServer32", 0, nullptr, 0, MAXIMUM_ALLOWED, nullptr, &server, nullptr);
        hr = status ? HRESULT_FROM_WIN32(status) : S_OK;
    }
    if (SUCCEEDED(hr))
        hr = set_string(server, nullptr, path);
    if (SUCCEEDED(hr))
        hr = set_string(server, L"ThreadingModel", L"Both");
    if (server)
        RegCloseKey(server);
    RegCloseKey(key);
    return hr;
}

/* evr's EliminateSubKey: a key and everything under it. */
static HRESULT eliminate(HKEY parent, const WCHAR *name)
{
    if (!name[0])
        return E_FAIL;
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(parent, name, 0, MAXIMUM_ALLOWED, &key);
    if (!status) {
        WCHAR child[MAX_PATH];
        for (;;) {
            DWORD length = MAX_PATH;
            FILETIME written;
            if (RegEnumKeyExW(key, 0, child, &length, nullptr, nullptr, nullptr, &written) ||
                eliminate(key, child) != S_OK)
                break;
        }
        RegCloseKey(key);
        status = RegDeleteKeyExW(parent, name, 0, 0);
    }
    return status > 0 ? HRESULT_FROM_WIN32(status) : status;
}

static HRESULT all_servers(const WCHAR *path, BOOL add)
{
    HRESULT hr = S_OK;
    for (ULONG i = 0; i < RTL_NUMBER_OF(TEMPLATES) && SUCCEEDED(hr); i++) {
        if (add) {
            hr = register_server(TEMPLATES[i].clsid, TEMPLATES[i].name, path);
        } else {
            WCHAR subkey[64];
            clsid_key(TEMPLATES[i].clsid, subkey);
            hr = eliminate(HKEY_CLASSES_ROOT, subkey);
        }
    }
    return hr;
}

/*
 * AMovieDllRegisterServer2: the servers first when registering and last when unregistering, then the EVR filter
 * through quartz's filter mapper. Mime has no quartz, so that step fails with the error of CoCreateInstance, and an
 * unregistration stops there before it removes any server, as on a Windows without the mapper.
 */
static HRESULT register_all(BOOL add)
{
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(EvrInstance, path, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());
    HRESULT hr = S_OK;
    if (add && FAILED(hr = all_servers(path, TRUE)))
        return hr;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IUnknown *mapper = nullptr;
    hr = CoCreateInstance(&CLSID_FilterMapper2_, nullptr, CLSCTX_INPROC_SERVER, &IID_IFilterMapper2_, (void **)&mapper);
    if (FAILED(hr))
        hr = CoCreateInstance(&CLSID_FilterMapper_, nullptr, CLSCTX_INPROC_SERVER, &IID_IFilterMapper_,
                              (void **)&mapper);
    /* the renderer filter is not served by Mime, so a mapper, once there is one, gets no filter to register. */
    if (mapper)
        mapper->lpVtbl->Release(mapper);
    CoFreeUnusedLibraries();
    CoUninitialize();
    if (SUCCEEDED(hr) && !add)
        hr = all_servers(path, FALSE);
    return hr;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return register_all(TRUE);
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return register_all(FALSE);
}
