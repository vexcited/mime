/*
 * oleacc.dll: active accessibility. mime has no accessibility proxies yet, so
 * every entry point fails cleanly.
 */
#define OLEACCAPI DECLSPEC_EXPORT
#include <mime/win32/oleacc.h>
#include <mime/win32/oleauto.h>
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
    *object = nullptr;
    return E_NOTIMPL;
}

OLEACCAPI HRESULT WINAPI CreateStdAccessibleObject(HWND hwnd, LONG object_id, REFIID riid, void **object)
{
    MIME_STUB("oleacc");
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ walking objects */

/* the slots of IAccessible after IDispatch's seven. */
typedef HRESULT(STDMETHODCALLTYPE *GET_PARENT)(IUnknown *, IDispatch **);
typedef HRESULT(STDMETHODCALLTYPE *GET_CHILD_COUNT)(IUnknown *, LONG *);
typedef HRESULT(STDMETHODCALLTYPE *GET_CHILD)(IUnknown *, VARIANT, IDispatch **);

static const IID IID_IAccessible = {0x618736e0, 0x3c3d, 0x11cf, {0x81, 0x0c, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const IID IID_IEnumVARIANT_ = {0x00020404, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID IID_IOleWindow_ = {0x00000114, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

static void *slot(IUnknown *object, int index)
{
    return ((void **)*(void ***)object)[index];
}

/* a child named by number becomes its object when the container has one for it. */
static void resolve_child(IUnknown *container, VARIANT *v)
{
    if (v->vt != VT_I4)
        return;
    IDispatch *child = nullptr;
    if (((GET_CHILD)slot(container, 9))(container, *v, &child) == S_OK && child) {
        v->vt = VT_DISPATCH;
        v->pdispVal = child;
    }
}

OLEACCAPI HRESULT WINAPI AccessibleChildren(IUnknown *container, LONG start, LONG count, VARIANT *children,
                                           LONG *obtained)
{
    if (!container || !children || !obtained || start < 0 || count < 0)
        return E_INVALIDARG;
    *obtained = 0;
    IUnknown *accessible;
    HRESULT hr = container->lpVtbl->QueryInterface(container, &IID_IAccessible, (void **)&accessible);
    if (FAILED(hr))
        return hr;
    IUnknown *enumerator = nullptr;
    if (SUCCEEDED(accessible->lpVtbl->QueryInterface(accessible, &IID_IEnumVARIANT_, (void **)&enumerator))) {
        /* Next, Skip and Reset follow IUnknown's three. */
        typedef HRESULT(STDMETHODCALLTYPE * NEXT)(IUnknown *, ULONG, VARIANT *, ULONG *);
        typedef HRESULT(STDMETHODCALLTYPE * SKIP)(IUnknown *, ULONG);
        typedef HRESULT(STDMETHODCALLTYPE * RESET)(IUnknown *);
        ULONG got = 0;
        ((RESET)slot(enumerator, 5))(enumerator);
        hr = start ? ((SKIP)slot(enumerator, 4))(enumerator, (ULONG)start) : S_OK;
        if (SUCCEEDED(hr))
            hr = ((NEXT)slot(enumerator, 3))(enumerator, (ULONG)count, children, &got);
        enumerator->lpVtbl->Release(enumerator);
        if (SUCCEEDED(hr)) {
            for (ULONG i = 0; i < got; i++)
                resolve_child(accessible, &children[i]);
            *obtained = (LONG)got;
            hr = got == (ULONG)count ? S_OK : S_FALSE;
        }
        accessible->lpVtbl->Release(accessible);
        return hr;
    }
    LONG total = 0;
    hr = ((GET_CHILD_COUNT)slot(accessible, 8))(accessible, &total);
    if (SUCCEEDED(hr)) {
        LONG n = 0;
        for (LONG id = start + 1; id <= total && n < count; id++, n++) {
            memset(&children[n], 0, sizeof(children[n]));
            children[n].vt = VT_I4;
            children[n].lVal = id;
            resolve_child(accessible, &children[n]);
        }
        *obtained = n;
        hr = n == count ? S_OK : S_FALSE;
    }
    accessible->lpVtbl->Release(accessible);
    return hr;
}

/* the window an object belongs to: its own if it is an IOleWindow, else its nearest ancestor's. */
OLEACCAPI HRESULT WINAPI WindowFromAccessibleObject(IUnknown *object, HWND *hwnd)
{
    if (!hwnd)
        return E_INVALIDARG;
    *hwnd = nullptr;
    if (!object)
        return E_INVALIDARG;
    IUnknown *current = (IUnknown *)object;
    current->lpVtbl->AddRef(current);
    for (int depth = 0; current && depth < 256; depth++) {
        IUnknown *window;
        if (SUCCEEDED(current->lpVtbl->QueryInterface(current, &IID_IOleWindow_, (void **)&window))) {
            typedef HRESULT(STDMETHODCALLTYPE * GET_WINDOW)(IUnknown *, HWND *);
            HRESULT hr = ((GET_WINDOW)slot(window, 3))(window, hwnd);
            window->lpVtbl->Release(window);
            current->lpVtbl->Release(current);
            return hr;
        }
        IUnknown *accessible = nullptr;
        IDispatch *parent = nullptr;
        if (SUCCEEDED(current->lpVtbl->QueryInterface(current, &IID_IAccessible, (void **)&accessible))) {
            if (((GET_PARENT)slot(accessible, 7))(accessible, &parent) != S_OK)
                parent = nullptr;
            accessible->lpVtbl->Release(accessible);
        }
        current->lpVtbl->Release(current);
        current = (IUnknown *)parent;
    }
    if (current)
        current->lpVtbl->Release(current);
    return E_FAIL;
}
