/*
 * wshom.ocx: the object model of the Windows Script Host, WScript.Shell and WScript.Network.
 */
#include "wshom_private.h"

int _fltused = 0x9875;

HINSTANCE g_module;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    return E_FAIL;
}

DECLSPEC_EXPORT HRESULT WINAPI DllUnregisterServer(void)
{
    return E_FAIL;
}
