/* dinput.dll: DirectInput 1 to 7, on the engine both directinput dlls share. */
#include "../lib/dinput/dinput_private.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DiIs8 = FALSE;
        DiModule = module;
        RtlInitializeCriticalSection(&DiLock);
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI DirectInputCreateA(HINSTANCE instance, DWORD version, IDirectInputA **out,
                                                  IUnknown *outer)
{
    return DiCreateHelper(instance, version, &IID_IDirectInputA, (void **)out, outer, FALSE);
}

DECLSPEC_EXPORT HRESULT WINAPI DirectInputCreateW(HINSTANCE instance, DWORD version, IDirectInputW **out,
                                                  IUnknown *outer)
{
    return DiCreateHelper(instance, version, &IID_IDirectInputW, (void **)out, outer, FALSE);
}

/* the six interfaces of directinput 1 to 7 alone; the pointer stays untouched otherwise. */
DECLSPEC_EXPORT HRESULT WINAPI DirectInputCreateEx(HINSTANCE instance, DWORD version, REFIID riid, void **out,
                                                   IUnknown *outer)
{
    static const IID *const ACCEPTED[] = {&IID_IDirectInputA,  &IID_IDirectInputW,  &IID_IDirectInput2A,
                                          &IID_IDirectInput2W, &IID_IDirectInput7A, &IID_IDirectInput7W};
    for (DWORD i = 0; i < RTL_NUMBER_OF(ACCEPTED); i++)
        if (IsEqualGUID(riid, ACCEPTED[i]))
            return DiCreateHelper(instance, version, riid, out, outer, FALSE);
    return E_NOINTERFACE;
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
