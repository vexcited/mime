/*
 * Windows.Media.Devices.dll: the library genuine combase loads for Windows.Media.Devices.MediaDevice, as the registry
 * names it. the class is the one of guest/combase/mediadevice.c, which the build compiles here with the audio
 * endpoints of guest/combase/audioendpoint.c.
 */
#include "../combase/combase_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

FARPROC CbOleProc(LPCSTR name)
{
    HMODULE ole32 = LoadLibraryW(L"ole32.dll");
    return ole32 ? GetProcAddress(ole32, name) : nullptr;
}

/* the watchers are those of Windows.Devices.Enumeration.dll, which listens to mmdevapi itself: none is here. */
void CbDeviceWatchersRefresh(void)
{
}

HRESULT WINAPI DllGetActivationFactory(HSTRING class_id, IActivationFactory **factory)
{
    if (!factory)
        return E_POINTER;
    *factory = nullptr;
    UINT32 length = 0;
    LPCWSTR name = WindowsGetStringRawBuffer(class_id, &length);
    if (!name || length != wcslen(CbMediaDeviceClass) || memcmp(name, CbMediaDeviceClass, length * sizeof(WCHAR)))
        return CLASS_E_CLASSNOTAVAILABLE;
    return CbMediaDeviceFactory(&IID_IActivationFactory, (void **)factory);
}

/* mmdevapi keeps the client of the library for the life of the process. */
HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}
