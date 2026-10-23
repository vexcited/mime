/* dinput8.dll: DirectInput 8, on the engine both directinput dlls share. */
#include "../lib/dinput/dinput_private.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DiIs8 = TRUE;
        DiModule = module;
        RtlInitializeCriticalSection(&DiLock);
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

/* windows refuses the joystick configuration interface here and leaves the pointer alone. */
DECLSPEC_EXPORT HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID riid, void **out,
                                                  IUnknown *outer)
{
    if (IsEqualGUID(riid, &IID_IDirectInputJoyConfig8))
        return E_NOINTERFACE;
    return DiCreateHelper(instance, version, riid, out, outer, TRUE);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return DiLocks ? S_FALSE : S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    return DiGetClassObject(clsid, riid, out);
}

DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DllUnregisterServer(void)
{
    return S_OK;
}

DECLSPEC_EXPORT const DIDATAFORMAT *WINAPI GetdfDIJoystick(void)
{
    return &DiJoystickFormat;
}
