/*
 * oleacc.dll: active accessibility. mime has no accessibility proxies yet, so
 * every entry point fails cleanly.
 */
#define OLEACCAPI DECLSPEC_EXPORT
#include <mime/win32/oleacc.h>
#include <mime/stub.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

OLEACCAPI LRESULT WINAPI LresultFromObject(REFIID riid, WPARAM wparam, IUnknown *object)
{
    MIME_STUB("oleacc");
    return (LRESULT)E_NOTIMPL;
}

OLEACCAPI HRESULT WINAPI AccessibleObjectFromWindow(HWND hwnd, DWORD object_id, REFIID riid, void **object)
{
    MIME_STUB("oleacc");
    if (!object)
        return E_INVALIDARG;
    *object = NULL;
    return E_NOTIMPL;
}

OLEACCAPI HRESULT WINAPI CreateStdAccessibleObject(HWND hwnd, LONG object_id, REFIID riid, void **object)
{
    MIME_STUB("oleacc");
    if (!object)
        return E_INVALIDARG;
    *object = NULL;
    return E_NOTIMPL;
}
