/*
 * ieframe.dll: the web browser control, CLSID_WebBrowser, that programs put in their windows to show a page.
 */
#include "ieframe_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_ieframe = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

typedef struct _DLLVERSIONINFO {
    DWORD cbSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformID;
} DLLVERSIONINFO;

DECLSPEC_EXPORT HRESULT WINAPI DllGetVersion(DLLVERSIONINFO *info)
{
    if (!info || info->cbSize < sizeof(*info))
        return E_INVALIDARG;
    info->dwMajorVersion = 11;
    info->dwMinorVersion = 0;
    info->dwBuildNumber = 26300;
    info->dwPlatformID = 2; /* DLLVER_PLATFORM_NT */
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllInstall(BOOL install, LPCWSTR command)
{
    return S_OK;
}

/* the registry has the classes already. */
DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DllUnregisterServer(void)
{
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!clsid || !riid)
        return E_INVALIDARG;
    static const CLSID internet_shortcut = {0xFBF23B40, 0xE3F0, 0x101B, {0x84, 0x88, 0x00, 0xAA, 0x00, 0x3E, 0x56, 0xF8}};
    if (IsEqualCLSID(clsid, &internet_shortcut)) {
        IClassFactory *shortcut = ShortcutFactory();
        return shortcut->lpVtbl->QueryInterface(shortcut, riid, object);
    }
    if (!IsEqualCLSID(clsid, &CLSID_WebBrowser) && !IsEqualCLSID(clsid, &CLSID_WebBrowser_V1))
        return CLASS_E_CLASSNOTAVAILABLE;
    IClassFactory *factory = BrowserFactory();
    return factory->lpVtbl->QueryInterface(factory, riid, object);
}
