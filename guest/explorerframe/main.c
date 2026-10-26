/*
 * explorerframe.dll: CLSID_TaskbarList, a window's taskbar button state, and the class factory of the list of the
 * shell's windows.
 */
#include "explorerframe_private.h"

HINSTANCE g_explorerframe;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_explorerframe = instance;
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
    info->dwMajorVersion = 6;
    info->dwMinorVersion = 0;
    info->dwBuildNumber = 26300;
    info->dwPlatformID = 2; /* DLLVER_PLATFORM_NT */
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!clsid || !riid)
        return E_INVALIDARG;
    static const CLSID shell_windows_factory = {0xD92BD3B9, 0x99A0, 0x4334, {0xA4, 0x97, 0x11, 0xBC, 0xB0, 0x93, 0xE9, 0xD2}};
    IClassFactory *factory;
    if (IsEqualCLSID(clsid, &CLSID_TaskbarList))
        factory = TaskbarListFactory();
    else if (IsEqualCLSID(clsid, &shell_windows_factory))
        factory = ShellWindowsFactory();
    else
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory->lpVtbl->QueryInterface(factory, riid, object);
}
