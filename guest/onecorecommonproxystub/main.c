/*
 * OneCoreCommonProxyStub.dll: the proxies and stubs windows names for IServiceProvider. The one interface is described
 * in the proxy file the build generates, see gen/onecorecommonproxystub/ps.h.
 */
#include <mime/win32/shldisp.h>
#include <mime/win32/rpcproxy.h>
#include <mime/win32/rpcndr.h>

#include "onecorecommonproxystub/ps.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **object)
{
    return onecoreps_DllGetClassObject(clsid, iid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return onecoreps_DllCanUnloadNow();
}

DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    return onecoreps_DllRegisterProxy(GetModuleHandleW(nullptr));
}

DECLSPEC_EXPORT HRESULT WINAPI DllUnregisterServer(void)
{
    return onecoreps_DllUnregisterProxy(GetModuleHandleW(nullptr));
}
