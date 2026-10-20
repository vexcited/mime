/*
 * oleacc.dll: active accessibility. the objects of windows are in proxy.c, their words in text.c; here are the
 * entry points that find objects, send them through messages and walk them.
 */
#define OLEACCAPI DECLSPEC_EXPORT
#include <mime/win32/oleacc.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/winuser.h>
#include <mime/win32/objidl.h>
#include "oleacc_private.h"

static const IID IID_IAccessible = {0x618736e0, 0x3c3d, 0x11cf, {0x81, 0x0c, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static void *slot(IUnknown *object, int index)
{
    return ((void **)*(void ***)object)[index];
}

/* ------------------------------------------------------------------ marshaling objects through messages */

/*
 * an object travels as windows' oleacc sends it: marshaled into a stream, the stream put in a page file backed
 * mapping behind a header, and an atom naming the mapping returned as the result of the message. the atom's
 * string tells the process and the handle of the mapping. a client takes the object out once.
 */
#define MARSHAL_MAGIC 0xAA1D5AF5u /* -1440944115 */
#define MARSHAL_LIMIT 0x100000
#define OUTSTANDING_MAX 40

WINOLEAPI HRESULT WINAPI CoMarshalInterface(IStream *stream, REFIID riid, IUnknown *unknown, DWORD context,
                                            void *destination, DWORD flags);
WINOLEAPI HRESULT WINAPI CoUnmarshalInterface(IStream *stream, REFIID riid, void **object);
WINOLEAPI HRESULT WINAPI CoReleaseMarshalData(IStream *stream);

typedef struct {
    ATOM atom;
    HANDLE mapping;
} OUTSTANDING;

static OUTSTANDING g_outstanding[OUTSTANDING_MAX];
static CRITICAL_SECTION g_outstanding_lock;
static LONG g_atom_sequence;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        InitializeCriticalSection(&g_outstanding_lock);
    return TRUE;
}

/* results nobody took are dropped in the order they were made, at most forty at a time. */
static void remember_outstanding(ATOM atom, HANDLE mapping)
{
    EnterCriticalSection(&g_outstanding_lock);
    OUTSTANDING oldest = g_outstanding[OUTSTANDING_MAX - 1];
    memmove(&g_outstanding[1], &g_outstanding[0], (OUTSTANDING_MAX - 1) * sizeof(OUTSTANDING));
    g_outstanding[0].atom = atom;
    g_outstanding[0].mapping = mapping;
    LeaveCriticalSection(&g_outstanding_lock);
    if (oldest.atom) {
        GlobalDeleteAtom(oldest.atom);
        CloseHandle(oldest.mapping);
    }
}

OLEACCAPI LRESULT WINAPI LresultFromObject(REFIID riid, WPARAM wparam, IUnknown *object)
{
    IStream *stream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr))
        return hr;
    /* a client of another process needs the object served over rpc; any other, which includes a caller that names none, is us. */
    DWORD me_now = GetCurrentProcessId(), asked = (DWORD)wparam;
    BOOL elsewhere = asked && asked != 0xFFFFFFFFu && asked != me_now;
    hr = CoMarshalInterface(stream, riid, object, elsewhere ? 0 : 3, nullptr, 0);
    if (FAILED(hr)) {
        stream->lpVtbl->Release(stream);
        return hr;
    }
    HGLOBAL global = nullptr;
    LARGE_INTEGER zero = {};
    ULARGE_INTEGER end = {};
    DWORD size = 0;
    hr = GetHGlobalFromStream(stream, &global);
    if (SUCCEEDED(hr) && global) {
        stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_END, &end);
        size = (DWORD)end.QuadPart;
        stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
    }
    const void *data = size && size <= MARSHAL_LIMIT ? GlobalLock(global) : nullptr;
    LRESULT result = E_FAIL;
    if (data) {
        DWORD me = GetCurrentProcessId(), target = wparam == 0xFFFFFFFFu || wparam == (WPARAM)-1 ? me : (DWORD)wparam;
        HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size + 16, nullptr);
        DWORD *view = mapping ? MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0) : nullptr;
        if (view) {
            view[0] = MARSHAL_MAGIC;
            view[1] = me;
            view[2] = target;
            view[3] = size;
            memcpy(view + 4, data, size);
            UnmapViewOfFile(view);
            WCHAR name[48];
            _snwprintf(name, RTL_NUMBER_OF(name), L"MSAA:%08X:%08X:%08X:%08X:", me, (DWORD)(ULONG_PTR)mapping,
                       (DWORD)InterlockedIncrement(&g_atom_sequence), me);
            ATOM atom = GlobalAddAtomW(name);
            if (atom) {
                remember_outstanding(atom, mapping);
                result = atom;
                mapping = nullptr;
            }
        }
        if (mapping)
            CloseHandle(mapping);
        GlobalUnlock(global);
    }
    if (result == E_FAIL) {
        CoReleaseMarshalData(stream);
    } else {
        /* the marshaled reference now lives in the mapping, which the client unmarshals. */
    }
    stream->lpVtbl->Release(stream);
    return result;
}

static BOOL parse_hex(const WCHAR *text, DWORD *value)
{
    DWORD v = 0;
    for (int i = 0; i < 8; i++) {
        WCHAR c = text[i];
        DWORD digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else
            return FALSE;
        v = v << 4 | digit;
    }
    *value = v;
    return TRUE;
}

OLEACCAPI HRESULT WINAPI ObjectFromLresult(LRESULT lresult, REFIID riid, WPARAM wparam, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if ((LONG)lresult < 0)
        return E_INVALIDARG;
    DWORD target = (DWORD)wparam;
    if (target == 0xFFFFFFFFu)
        target = GetCurrentProcessId();
    WCHAR name[48];
    DWORD process, handle, sequence, source;
    ATOM atom = (ATOM)lresult;
    if ((lresult >> 16) || !atom || GlobalGetAtomNameW(atom, name, RTL_NUMBER_OF(name)) != 41 ||
        wcsncmp(name, L"MSAA:", 5) || name[13] != ':' || name[22] != ':' || name[31] != ':' || name[40] != ':' ||
        !parse_hex(name + 5, &process) || !parse_hex(name + 14, &handle) || !parse_hex(name + 23, &sequence) ||
        !parse_hex(name + 32, &source))
        return E_FAIL;
    GlobalDeleteAtom(atom);
    HANDLE mapping;
    if (process == GetCurrentProcessId()) {
        mapping = (HANDLE)(LONG_PTR)(LONG)handle;
        /* the entry of the table is spent as the client takes the result. */
        EnterCriticalSection(&g_outstanding_lock);
        for (int i = 0; i < OUTSTANDING_MAX; i++)
            if (g_outstanding[i].atom == atom) {
                g_outstanding[i].atom = 0;
                g_outstanding[i].mapping = nullptr;
            }
        LeaveCriticalSection(&g_outstanding_lock);
    } else {
        HANDLE owner = OpenProcess(PROCESS_DUP_HANDLE, FALSE, process);
        if (!owner)
            return E_FAIL;
        BOOL ok = DuplicateHandle(owner, (HANDLE)(LONG_PTR)(LONG)handle, GetCurrentProcess(), &mapping, 0, FALSE,
                                  DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS);
        CloseHandle(owner);
        if (!ok || !mapping)
            return E_FAIL;
    }
    DWORD header[4];
    DWORD *view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 16);
    if (!view) {
        CloseHandle(mapping);
        return E_FAIL;
    }
    memcpy(header, view, sizeof(header));
    UnmapViewOfFile(view);
    HRESULT hr = E_FAIL;
    if (header[0] == MARSHAL_MAGIC && (!target || header[2] == GetCurrentProcessId()) && header[3] <= MARSHAL_LIMIT) {
        BYTE *all = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, header[3] + 16);
        HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, header[3]);
        void *copy = all && global ? GlobalLock(global) : nullptr;
        if (copy) {
            memcpy(copy, all + 16, header[3]);
            GlobalUnlock(global);
            IStream *stream = nullptr;
            hr = CreateStreamOnHGlobal(global, TRUE, &stream);
            if (SUCCEEDED(hr)) {
                hr = CoUnmarshalInterface(stream, riid, object);
                stream->lpVtbl->Release(stream);
                global = nullptr;
            }
        }
        if (global)
            GlobalFree(global);
        if (all)
            UnmapViewOfFile(all);
    }
    CloseHandle(mapping);
    return hr;
}

/* ------------------------------------------------------------------ objects of windows */

static HRESULT std_object(HWND hwnd, LONG object_id, REFIID riid, void **object)
{
    return OaCreateProxy(hwnd, object_id, riid, object);
}

OLEACCAPI HRESULT WINAPI CreateStdAccessibleObject(HWND hwnd, LONG object_id, REFIID riid, void **object)
{
    return std_object(hwnd, object_id, riid, object);
}

OLEACCAPI HRESULT WINAPI CreateStdAccessibleProxyW(HWND hwnd, LPCWSTR class_name, LONG object_id, REFIID riid,
                                                   void **object)
{
    /* the proxy follows the class of the window, not the name given. */
    if (!object)
        return E_INVALIDARG;
    return std_object(hwnd, object_id, riid, object);
}

OLEACCAPI HRESULT WINAPI CreateStdAccessibleProxyA(HWND hwnd, LPCSTR class_name, LONG object_id, REFIID riid,
                                                   void **object)
{
    return CreateStdAccessibleProxyW(hwnd, nullptr, object_id, riid, object);
}

/*
 * the object a window gives for an id: its own, from WM_GETOBJECT, whose result may be an error that is the
 * answer, nothing, when the window has none, or an object marshaled for this call. a window without one gets
 * the standard proxy.
 */
OLEACCAPI HRESULT WINAPI AccessibleObjectFromWindow(HWND hwnd, DWORD object_id, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    WPARAM from = 0;
    ULONG_PTR result = 0;
    if (IsWindow(hwnd)) {
        /* a window of the calling thread is answered in the thread: the object goes back as it is. */
        from = GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId() ? (WPARAM)0xFFFFFFFFu
                                                                                : (WPARAM)GetCurrentProcessId();
        SendMessageTimeoutW(hwnd, WM_GETOBJECT, from, (LPARAM)object_id, SMTO_ABORTIFHUNG, 10000, &result);
    }
    HRESULT hr = (HRESULT)(LONG)result;
    if (hr < 0)
        return hr;
    if (result) {
        hr = ObjectFromLresult((LRESULT)result, riid, from, object);
        if (SUCCEEDED(hr) && *object)
            return hr;
        /* a window of this thread that answered wrongly has no other object to offer. */
        if (from == 0xFFFFFFFFu)
            return FAILED(hr) ? hr : E_FAIL;
    }
    return std_object(hwnd, (LONG)object_id, riid, object);
}

OLEACCAPI HRESULT WINAPI AccessibleObjectFromEvent(HWND hwnd, DWORD object_id, DWORD child_id, IAccessible **accessible,
                                                   VARIANT *child)
{
    if (!accessible || !child || (!IsWindow(hwnd) && hwnd))
        return E_INVALIDARG;
    *accessible = nullptr;
    VariantInit(child);
    IUnknown *object = nullptr;
    HRESULT hr = AccessibleObjectFromWindow(hwnd, object_id, &IID_IAccessible, (void **)&object);
    if (FAILED(hr))
        return hr;
    if (!object)
        return E_FAIL;
    VARIANT id;
    VariantInit(&id);
    id.vt = VT_I4;
    id.lVal = (LONG)child_id;
    IDispatch *found = nullptr;
    typedef HRESULT(STDMETHODCALLTYPE * GET_CHILD)(IUnknown *, VARIANT, IDispatch **);
    if (FAILED(((GET_CHILD)slot(object, 9))(object, id, &found)) || !found) {
        *accessible = (IAccessible *)object;
        return VariantCopy(child, &id);
    }
    object->lpVtbl->Release(object);
    object = nullptr;
    hr = ((IUnknown *)found)->lpVtbl->QueryInterface((IUnknown *)found, &IID_IAccessible, (void **)&object);
    ((IUnknown *)found)->lpVtbl->Release((IUnknown *)found);
    if (FAILED(hr))
        return hr;
    if (!object)
        return E_FAIL;
    id.lVal = CHILDID_SELF;
    *accessible = (IAccessible *)object;
    return VariantCopy(child, &id);
}

/* the object under a point, and the child of it that is there. */
OLEACCAPI HRESULT WINAPI AccessibleObjectFromPoint(POINT point, IAccessible **accessible, VARIANT *child)
{
    if (!accessible || !child)
        return E_INVALIDARG;
    *accessible = nullptr;
    VariantInit(child);
    HWND hwnd = WindowFromPoint(point);
    if (!hwnd)
        return S_OK;
    IUnknown *object = nullptr;
    HRESULT hr = AccessibleObjectFromWindow(hwnd, OBJID_WINDOW, &IID_IAccessible, (void **)&object);
    if (FAILED(hr) || !object)
        return FAILED(hr) ? hr : E_FAIL;
    typedef HRESULT(STDMETHODCALLTYPE * HIT_TEST)(IUnknown *, LONG, LONG, VARIANT *);
    VARIANT hit;
    VariantInit(&hit);
    hr = ((HIT_TEST)slot(object, 24))(object, point.x, point.y, &hit);
    if (hr == S_OK && hit.vt == VT_DISPATCH && hit.pdispVal) {
        IUnknown *deeper = nullptr;
        HRESULT found = ((IUnknown *)hit.pdispVal)->lpVtbl->QueryInterface((IUnknown *)hit.pdispVal, &IID_IAccessible,
                                                                           (void **)&deeper);
        ((IUnknown *)hit.pdispVal)->lpVtbl->Release((IUnknown *)hit.pdispVal);
        if (SUCCEEDED(found) && deeper) {
            object->lpVtbl->Release(object);
            object = deeper;
        }
        hit.vt = VT_I4;
        hit.lVal = CHILDID_SELF;
    } else if (hr != S_OK || hit.vt != VT_I4) {
        hit.vt = VT_I4;
        hit.lVal = CHILDID_SELF;
    }
    *accessible = (IAccessible *)object;
    *child = hit;
    return S_OK;
}

OLEACCAPI void WINAPI GetOleaccVersionInfo(DWORD *version, DWORD *build)
{
    if (version && build) {
        *version = 0x00070002;
        *build = 0x271A0000;
    }
}

OLEACCAPI HANDLE WINAPI GetProcessHandleFromHwnd(HWND hwnd)
{
    DWORD process;
    if (!hwnd || !GetWindowThreadProcessId(hwnd, &process))
        return nullptr;
    return OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, process);
}

/* ------------------------------------------------------------------ walking objects */

/* the slots of IAccessible after IDispatch's seven. */
typedef HRESULT(STDMETHODCALLTYPE *GET_PARENT)(IUnknown *, IDispatch **);
typedef HRESULT(STDMETHODCALLTYPE *GET_CHILD_COUNT)(IUnknown *, LONG *);
typedef HRESULT(STDMETHODCALLTYPE *GET_CHILD)(IUnknown *, VARIANT, IDispatch **);

static const IID IID_IEnumVARIANT_ = {0x00020404, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID IID_IOleWindow_ = {0x00000114, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

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

OLEACCAPI HRESULT WINAPI AccessibleChildren(IAccessible *container_in, LONG start, LONG count, VARIANT *children,
                                           LONG *obtained)
{
    IUnknown *container = (IUnknown *)container_in;
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
OLEACCAPI HRESULT WINAPI WindowFromAccessibleObject(IAccessible *object_in, HWND *hwnd)
{
    IUnknown *object = (IUnknown *)object_in;
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
