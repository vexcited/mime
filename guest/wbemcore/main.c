/*
 * wbemcore.dll: management instrumentation. the classes of the engine are created here, for the locator, the objects,
 * the path parser and the mof compiler, and the winmgmt service that svchost hosts runs from the same file.
 */
#include "wbemcore.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        wr_init();
    }
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return wm_class_factory(clsid, riid, out);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_wbem_locks > 0 ? S_FALSE : S_OK;
}
