/*
 * OneCoreUAPCommonProxyStub.dll: the proxies and stubs windows names for IShellBrowser and IShellView. The proxy file the
 * build generates, gen/onecoreuapcommonproxystub/ps.h, describes the methods that cross to another process. The others
 * are not remote: the proxy of one answers E_NOTIMPL at once and the stub of one is never called with arguments, so a
 * caller that tries to pass a menu, a message or an item list to another process is told so.
 */
#include <mime/win32/shldisp.h>
#include <mime/win32/rpcproxy.h>
#include <mime/win32/rpcndr.h>

/* the local forms the generated proxy file names. */
#define NOT_REMOTE(iface, name, ...)                                                                                   \
    HRESULT STDMETHODCALLTYPE iface##_##name##_Proxy(iface *This, ##__VA_ARGS__);                                      \
    HRESULT STDMETHODCALLTYPE iface##_##name##_Stub(iface *This);

NOT_REMOTE(IShellBrowser, InsertMenusSB, HMENU shared, LPOLEMENUGROUPWIDTHS widths)
NOT_REMOTE(IShellBrowser, SetMenuSB, HMENU shared, HOLEMENU descriptor, HWND object)
NOT_REMOTE(IShellBrowser, RemoveMenusSB, HMENU shared)
NOT_REMOTE(IShellBrowser, TranslateAcceleratorSB, MSG *message, WORD id)
NOT_REMOTE(IShellBrowser, BrowseObject, PCUIDLIST_RELATIVE pidl, UINT flags)
NOT_REMOTE(IShellBrowser, SendControlMsg, UINT id, UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result)
NOT_REMOTE(IShellBrowser, SetToolbarItems, void *buttons, UINT count, UINT flags)
NOT_REMOTE(IShellView, TranslateAccelerator, MSG *message)
NOT_REMOTE(IShellView, CreateViewWindow, IShellView *previous, const void *settings, IShellBrowser *owner, RECT *bounds,
           HWND *window)
NOT_REMOTE(IShellView, GetCurrentInfo, void *settings)
NOT_REMOTE(IShellView, AddPropertySheetPages, DWORD reserved, void *add_page, LPARAM lparam)
NOT_REMOTE(IShellView, SelectItem, PCUITEMID_CHILD pidl, UINT flags)

#include "onecoreuapcommonproxystub/ps.h"

#define ANSWER(iface, name, ...)                                                                                       \
    HRESULT STDMETHODCALLTYPE iface##_##name##_Proxy(iface *This, ##__VA_ARGS__)                                       \
    {                                                                                                                  \
        return E_NOTIMPL;                                                                                              \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE iface##_##name##_Stub(iface *This)                                                       \
    {                                                                                                                  \
        return E_NOTIMPL;                                                                                              \
    }

ANSWER(IShellBrowser, InsertMenusSB, HMENU shared, LPOLEMENUGROUPWIDTHS widths)
ANSWER(IShellBrowser, SetMenuSB, HMENU shared, HOLEMENU descriptor, HWND object)
ANSWER(IShellBrowser, RemoveMenusSB, HMENU shared)
ANSWER(IShellBrowser, TranslateAcceleratorSB, MSG *message, WORD id)
ANSWER(IShellBrowser, BrowseObject, PCUIDLIST_RELATIVE pidl, UINT flags)
ANSWER(IShellBrowser, SendControlMsg, UINT id, UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result)
ANSWER(IShellBrowser, SetToolbarItems, void *buttons, UINT count, UINT flags)
ANSWER(IShellView, TranslateAccelerator, MSG *message)
ANSWER(IShellView, CreateViewWindow, IShellView *previous, const void *settings, IShellBrowser *owner, RECT *bounds,
       HWND *window)
ANSWER(IShellView, GetCurrentInfo, void *settings)
ANSWER(IShellView, AddPropertySheetPages, DWORD reserved, void *add_page, LPARAM lparam)
ANSWER(IShellView, SelectItem, PCUITEMID_CHILD pidl, UINT flags)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **object)
{
    return onecoreuapps_DllGetClassObject(clsid, iid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return onecoreuapps_DllCanUnloadNow();
}

DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    return onecoreuapps_DllRegisterProxy(GetModuleHandleW(nullptr));
}

DECLSPEC_EXPORT HRESULT WINAPI DllUnregisterServer(void)
{
    return onecoreuapps_DllUnregisterProxy(GetModuleHandleW(nullptr));
}
