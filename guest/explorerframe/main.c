/*
 * explorerframe.dll: the taskbar button list of a window, CLSID_TaskbarList.
 */
#include "explorerframe_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* the version of the dll, which the shell's own library reports for itself. */
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
    if (!IsEqualCLSID(clsid, &CLSID_TaskbarList))
        return CLASS_E_CLASSNOTAVAILABLE;
    return TaskbarListFactory()->lpVtbl->QueryInterface(TaskbarListFactory(), riid, object);
}
