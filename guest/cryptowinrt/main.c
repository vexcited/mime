/*
 * CryptoWinRT.dll: the library genuine combase loads for Windows.Security.Credentials.KeyCredentialManager, as the
 * registry names it. the class is the one of guest/combase/keycred.c, which the build compiles here with the operations
 * it hands out.
 */
#include "../combase/combase_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING class_id, IActivationFactory **factory)
{
    if (!factory)
        return E_POINTER;
    *factory = nullptr;
    UINT32 length = 0;
    LPCWSTR name = WindowsGetStringRawBuffer(class_id, &length);
    if (!name || length != wcslen(CbKeyCredentialManagerClass) ||
        memcmp(name, CbKeyCredentialManagerClass, length * sizeof(WCHAR)))
        return CLASS_E_CLASSNOTAVAILABLE;
    return CbKeyCredentialManagerFactory(&IID_IActivationFactory, (void **)factory);
}

/* the factory is static, so the library stays. */
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
