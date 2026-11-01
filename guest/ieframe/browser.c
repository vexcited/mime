/*
 * The web browser control of ieframe.dll: an embedded OLE control that a container such as an MFC dialog or a .NET form
 * puts in a window. The object answers IWebBrowser2 and the interfaces a container asks of a control, draws nothing
 * itself and lets host.c put the page in a child window. Events go to the sinks of DWebBrowserEvents2.
 */
#include "ieframe_private.h"

#define MAX_SINKS 8
#define WM_BROWSER_FINISH (WM_APP + 1)

IF_GUID(IID_IProvideClassInfo_, 0xB196B283, 0xBAB4, 0x101A, 0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07);
IF_GUID(IID_Null_, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

typedef struct CONNECTION_POINT {
    const IConnectionPointVtbl *Vtbl;
    BROWSER *Browser;
    const GUID *Iid;
    IDispatch *Sinks[MAX_SINKS];
    DWORD Cookies[MAX_SINKS];
} CONNECTION_POINT;

enum { FLAG_SILENT, FLAG_OFFLINE, FLAG_REGISTER_BROWSER, FLAG_DROP_TARGET, FLAG_THEATER, FLAG_ADDRESS_BAR, FLAG_RESIZABLE,
       FLAG_VISIBLE, FLAG_STATUS_BAR, FLAG_MENU_BAR, FLAG_FULL_SCREEN, FLAG_COUNT };

struct BROWSER {
    IWebBrowser2 Browser2;
    IOleObject OleObject;
    IOleInPlaceObject InPlace;
    IOleInPlaceActiveObject Active;
    IOleControl Control;
    IViewObject2 View;
    IPersistStreamInit Persist;
    IConnectionPointContainer Container;
    IProvideClassInfo2 ClassInfo;
    IOleCommandTarget Command;
    CONNECTION_POINT Points[2];
    LONG Refs;
    IOleClientSite *Site;
    IAdviseSink *Advise;
    HWND Window;
    HWND Content;
    HOST *Host;
    SIZEL Extent;
    LONG ReadyState;
    BOOL Busy;
    BOOL InPlaceActive;
    BOOL ExpectStart;
    BOOL HtmlNavigation;
    BOOL WaitingHost;
    LPWSTR Url;
    LPWSTR Title;
    LPWSTR Status;
    LPWSTR PendingUrl;
    VARIANT_BOOL Flags[FLAG_COUNT];
    DWORD NextCookie;
};

#define BROWSER_FROM(member, pointer) ((BROWSER *)((BYTE *)(pointer) - offsetof(BROWSER, member)))

HINSTANCE g_ieframe;

/* ------------------------------------------------------------------ small helpers */

static LPWSTR dup_string(LPCWSTR text)
{
    SIZE_T size = ((SIZE_T)lstrlenW(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = HeapAlloc(GetProcessHeap(), 0, size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

static void set_string(LPWSTR *slot, LPCWSTR text)
{
    LPWSTR copy = text ? dup_string(text) : nullptr;
    HeapFree(GetProcessHeap(), 0, *slot);
    *slot = copy;
}

static HRESULT out_bstr(LPCWSTR text, BSTR *value)
{
    if (!value)
        return E_POINTER;
    *value = SysAllocString(text ? text : L"");
    return *value ? S_OK : E_OUTOFMEMORY;
}

/* the text of a variant that holds a url, a string or a reference to one. */
static LPCWSTR variant_text(const VARIANT *v)
{
    while (v && V_VT(v) == (VT_VARIANT | VT_BYREF))
        v = V_VARIANTREF(v);
    if (!v)
        return nullptr;
    if (V_VT(v) == VT_BSTR)
        return V_BSTR(v);
    if (V_VT(v) == (VT_BSTR | VT_BYREF) && v->pbstrVal)
        return *v->pbstrVal;
    return nullptr;
}

/* ------------------------------------------------------------------ events */

static void fire(BROWSER *b, DISPID id, VARIANTARG *args, UINT count)
{
    CONNECTION_POINT *point = &b->Points[0];
    DISPPARAMS params = {args, nullptr, count, 0};
    IDispatch *sinks[MAX_SINKS];
    UINT n = 0;
    for (UINT i = 0; i < MAX_SINKS; i++)
        if (point->Sinks[i]) {
            sinks[n] = point->Sinks[i];
            sinks[n]->lpVtbl->AddRef(sinks[n]);
            n++;
        }
    for (UINT i = 0; i < n; i++) {
        sinks[i]->lpVtbl->Invoke(sinks[i], id, &IID_Null_, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, nullptr,
                                 nullptr, nullptr);
        sinks[i]->lpVtbl->Release(sinks[i]);
    }
}

static IDispatch *self_dispatch(BROWSER *b)
{
    return (IDispatch *)&b->Browser2;
}

static void fire_before_navigate(BROWSER *b, LPCWSTR url, BOOL *cancel)
{
    VARIANT url_v = {}, flags_v = {}, target_v = {}, post_v = {}, post_data_v = {}, headers_v = {};
    VARIANT_BOOL cancelled = VARIANT_FALSE;
    VARIANTARG args[7] = {};
    V_VT(&url_v) = VT_BSTR;
    V_BSTR(&url_v) = SysAllocString(url);
    V_VT(&flags_v) = VT_I4;
    V_VT(&target_v) = VT_BSTR;
    V_BSTR(&target_v) = SysAllocString(L"");
    /* the post data is a reference to a variant, which is empty when there is none. */
    V_VT(&post_data_v) = VT_EMPTY;
    V_VT(&post_v) = VT_VARIANT | VT_BYREF;
    V_VARIANTREF(&post_v) = &post_data_v;
    V_VT(&headers_v) = VT_BSTR;
    V_BSTR(&headers_v) = SysAllocString(L"");
    V_VT(&args[6]) = VT_DISPATCH;
    V_DISPATCH(&args[6]) = self_dispatch(b);
    V_VT(&args[5]) = VT_VARIANT | VT_BYREF;
    V_VARIANTREF(&args[5]) = &url_v;
    V_VT(&args[4]) = VT_VARIANT | VT_BYREF;
    V_VARIANTREF(&args[4]) = &flags_v;
    V_VT(&args[3]) = VT_VARIANT | VT_BYREF;
    V_VARIANTREF(&args[3]) = &target_v;
    V_VT(&args[2]) = VT_VARIANT | VT_BYREF;
    V_VARIANTREF(&args[2]) = &post_v;
    V_VT(&args[1]) = VT_VARIANT | VT_BYREF;
    V_VARIANTREF(&args[1]) = &headers_v;
    V_VT(&args[0]) = VT_BOOL | VT_BYREF;
    args[0].byref = &cancelled;
    fire(b, DISPID_BEFORENAVIGATE2, args, 7);
    *cancel = cancelled != VARIANT_FALSE;
    SysFreeString(V_BSTR(&url_v));
    SysFreeString(V_BSTR(&target_v));
    SysFreeString(V_BSTR(&headers_v));
}

static void fire_url_event(BROWSER *b, DISPID id, LPCWSTR url)
{
    VARIANT url_v = {};
    VARIANTARG args[2] = {};
    V_VT(&url_v) = VT_BSTR;
    V_BSTR(&url_v) = SysAllocString(url);
    V_VT(&args[1]) = VT_DISPATCH;
    V_DISPATCH(&args[1]) = self_dispatch(b);
    V_VT(&args[0]) = VT_VARIANT | VT_BYREF;
    V_VARIANTREF(&args[0]) = &url_v;
    fire(b, id, args, 2);
    SysFreeString(V_BSTR(&url_v));
}

static void fire_bstr_event(BROWSER *b, DISPID id, LPCWSTR text)
{
    VARIANTARG arg = {};
    V_VT(&arg) = VT_BSTR;
    V_BSTR(&arg) = SysAllocString(text);
    fire(b, id, &arg, 1);
    SysFreeString(V_BSTR(&arg));
}

static void finish_navigation(BROWSER *b, LPCWSTR url)
{
    b->Busy = FALSE;
    b->ReadyState = READYSTATE_COMPLETE;
    fire_url_event(b, DISPID_NAVIGATECOMPLETE2, url);
    b->ReadyState = READYSTATE_COMPLETE;
    fire(b, DISPID_DOWNLOADCOMPLETE, nullptr, 0);
    fire_url_event(b, DISPID_DOCUMENTCOMPLETE, url);
}

/* ------------------------------------------------------------------ what the page tells */

void BrowserNavigationStarting(BROWSER *b, LPCWSTR url, BOOL *cancel)
{
    if (b->ExpectStart) {
        b->ExpectStart = FALSE;
        return;
    }
    fire_before_navigate(b, url, cancel);
    if (!*cancel) {
        b->Busy = TRUE;
        b->ReadyState = READYSTATE_LOADING;
        b->HtmlNavigation = FALSE;
        fire(b, DISPID_DOWNLOADBEGIN, nullptr, 0);
    }
}

void BrowserNavigationCompleted(BROWSER *b, LPCWSTR url, BOOL success)
{
    if (b->HtmlNavigation && b->Url)
        url = b->Url;
    else
        set_string(&b->Url, url);
    finish_navigation(b, url);
}

void BrowserTitleChanged(BROWSER *b, LPCWSTR title)
{
    set_string(&b->Title, title);
    fire_bstr_event(b, DISPID_TITLECHANGE, title);
}

void BrowserHostReady(BROWSER *b)
{
    b->WaitingHost = FALSE;
}

/* ------------------------------------------------------------------ navigation */

/* the html of a res:// url: res://module/id or res://module/type/id, the type html when it is left out. */
static LPWSTR resource_html(LPCWSTR url)
{
    WCHAR module[MAX_PATH];
    LPCWSTR p = url + 6;
    LPCWSTR last = nullptr, before_last = nullptr;
    for (LPCWSTR c = p; *c; c++)
        if (*c == L'/') {
            before_last = last;
            last = c;
        }
    if (!last)
        return nullptr;
    LPCWSTR module_end = last;
    LPCWSTR type_text = nullptr;
    if (before_last && before_last > p) {
        module_end = before_last;
        type_text = before_last + 1;
    }
    SIZE_T length = (SIZE_T)(module_end - p);
    if (length == 0 || length >= MAX_PATH)
        return nullptr;
    memcpy(module, p, length * sizeof(WCHAR));
    module[length] = 0;

    WCHAR type_name[64] = L"", id_name[64] = L"";
    if (type_text) {
        SIZE_T n = (SIZE_T)(last - type_text);
        if (n >= 64)
            return nullptr;
        memcpy(type_name, type_text, n * sizeof(WCHAR));
        type_name[n] = 0;
    }
    lstrcpynW(id_name, last + 1, 64);
    for (WCHAR *c = id_name; *c; c++)
        if (*c == L'?' || *c == L'#') {
            *c = 0;
            break;
        }

    HMODULE library = LoadLibraryExW(module, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!library)
        return nullptr;
    /* a number is the id of the resource, with # before it a name is a number too. */
    LPCWSTR id = id_name;
    ULONG_PTR id_value = 0;
    BOOL numeric = id_name[0] != 0;
    for (LPCWSTR c = id_name[0] == L'#' ? id_name + 1 : id_name; *c; c++) {
        if (*c < L'0' || *c > L'9') {
            numeric = FALSE;
            break;
        }
        id_value = id_value * 10 + (ULONG_PTR)(*c - L'0');
    }
    if (numeric)
        id = MAKEINTRESOURCEW(id_value);
    LPCWSTR type = (LPCWSTR)RT_HTML;
    if (type_name[0]) {
        ULONG_PTR type_value = 0;
        BOOL type_numeric = TRUE;
        for (LPCWSTR c = type_name[0] == L'#' ? type_name + 1 : type_name; *c; c++) {
            if (*c < L'0' || *c > L'9') {
                type_numeric = FALSE;
                break;
            }
            type_value = type_value * 10 + (ULONG_PTR)(*c - L'0');
        }
        type = type_numeric ? MAKEINTRESOURCEW(type_value) : type_name;
    }
    HRSRC resource = FindResourceW(library, id, type);
    LPWSTR html = nullptr;
    if (resource) {
        DWORD size = SizeofResource(library, resource);
        HGLOBAL loaded = LoadResource(library, resource);
        const BYTE *data = loaded ? LockResource(loaded) : nullptr;
        if (data) {
            UINT page = CP_UTF8;
            DWORD flags = MB_ERR_INVALID_CHARS;
            if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
                data += 3;
                size -= 3;
            } else if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
                html = HeapAlloc(GetProcessHeap(), 0, size);
                if (html) {
                    memcpy(html, data + 2, size - 2);
                    html[(size - 2) / sizeof(WCHAR)] = 0;
                }
                size = 0;
            }
            if (size) {
                int chars = MultiByteToWideChar(page, flags, (LPCSTR)data, (int)size, nullptr, 0);
                if (!chars) {
                    page = CP_ACP;
                    flags = 0;
                    chars = MultiByteToWideChar(page, flags, (LPCSTR)data, (int)size, nullptr, 0);
                }
                html = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)chars + 1) * sizeof(WCHAR));
                if (html) {
                    MultiByteToWideChar(page, flags, (LPCSTR)data, (int)size, html, chars);
                    html[chars] = 0;
                }
            }
        }
    }
    FreeLibrary(library);
    return html;
}

static BOOL is_resource_url(LPCWSTR url)
{
    static const WCHAR prefix[] = L"res://";
    for (int i = 0; i < 6; i++)
        if ((url[i] | 0x20) != (prefix[i] | 0x20) || !url[i])
            return FALSE;
    return TRUE;
}

static HRESULT start_navigation(BROWSER *b, LPCWSTR url)
{
    BOOL cancel = FALSE;
    fire_before_navigate(b, url, &cancel);
    if (cancel)
        return S_OK;
    b->Busy = TRUE;
    b->ReadyState = READYSTATE_LOADING;
    set_string(&b->Url, url);
    fire(b, DISPID_DOWNLOADBEGIN, nullptr, 0);
    BOOL accepted = FALSE;
    if (b->Host) {
        b->ExpectStart = TRUE;
        if (is_resource_url(url)) {
            LPWSTR html = resource_html(url);
            if (html) {
                b->HtmlNavigation = TRUE;
                accepted = HostNavigateHtml(b->Host, html);
                HeapFree(GetProcessHeap(), 0, html);
            }
        } else {
            b->HtmlNavigation = FALSE;
            accepted = HostNavigate(b->Host, url);
        }
        if (!accepted)
            b->ExpectStart = FALSE;
    }
    if (!accepted && b->Window)
        PostMessageW(b->Window, WM_BROWSER_FINISH, 0, 0);
    return S_OK;
}

static HRESULT navigate(BROWSER *b, LPCWSTR url)
{
    if (!url || !url[0])
        return E_INVALIDARG;
    if (!b->Window) {
        /* a control that is not active has no window to show the page in, it goes when the control is shown. */
        set_string(&b->PendingUrl, url);
        return S_OK;
    }
    return start_navigation(b, url);
}

/* ------------------------------------------------------------------ the windows */

static LRESULT CALLBACK shell_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    BROWSER *b = (BROWSER *)GetWindowLongPtrW(window, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        break;
    }
    case WM_SIZE:
        if (b && b->Content) {
            int width = LOWORD(lparam), height = HIWORD(lparam);
            HWND view = GetParent(b->Content);
            MoveWindow(view, 0, 0, width, height, TRUE);
            MoveWindow(b->Content, 0, 0, width, height, TRUE);
            if (b->Host)
                HostResize(b->Host, width, height);
        }
        return 0;
    case WM_SETFOCUS:
        if (b && b->Host)
            HostSetFocus(b->Host);
        return 0;
    case WM_BROWSER_FINISH:
        if (b && b->Busy)
            finish_navigation(b, b->Url ? b->Url : L"about:blank");
        return 0;
    case WM_NCDESTROY:
        if (b)
            b->Window = nullptr;
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static LRESULT CALLBACK child_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, message, wparam, lparam);
}

static BOOL register_classes(void)
{
    static LONG done;
    if (done)
        return TRUE;
    WNDCLASSW shell = {CS_DBLCLKS, shell_proc, 0, 0, g_ieframe, nullptr, LoadCursorW(nullptr, IDC_ARROW),
                       (HBRUSH)(COLOR_WINDOW + 1), nullptr, L"Shell Embedding"};
    WNDCLASSW view = shell, server = shell;
    view.lpfnWndProc = child_proc;
    view.lpszClassName = L"Shell DocObject View";
    server.lpfnWndProc = child_proc;
    server.lpszClassName = L"Internet Explorer_Server";
    RegisterClassW(&shell);
    RegisterClassW(&view);
    RegisterClassW(&server);
    InterlockedExchange(&done, 1);
    return TRUE;
}

/* ------------------------------------------------------------------ in place activation */

static void create_windows(BROWSER *b, HWND parent, const RECT *position)
{
    register_classes();
    int width = position->right - position->left, height = position->bottom - position->top;
    if (width < 0)
        width = 0;
    if (height < 0)
        height = 0;
    b->Window = CreateWindowExW(0, L"Shell Embedding", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                position->left, position->top, width, height, parent, nullptr, g_ieframe, b);
    if (!b->Window)
        return;
    HWND view = CreateWindowExW(0, L"Shell DocObject View", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, width,
                                height, b->Window, nullptr, g_ieframe, nullptr);
    b->Content = CreateWindowExW(0, L"Internet Explorer_Server", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0,
                                 width, height, view, nullptr, g_ieframe, nullptr);
    if (b->Content) {
        b->Host = HostCreate(b, b->Content);
        b->WaitingHost = b->Host != nullptr;
    }
}

static HRESULT activate(BROWSER *b, LONG verb, HWND parent, LPCRECT position)
{
    if (b->InPlaceActive) {
        if (b->Window && verb != -3)
            ShowWindow(b->Window, SW_SHOW);
        return S_OK;
    }
    if (!b->Site)
        return E_UNEXPECTED;
    IOleInPlaceSite *site;
    HRESULT hr = b->Site->lpVtbl->QueryInterface(b->Site, &IID_IOleInPlaceSite, (void **)&site);
    if (FAILED(hr))
        return hr;
    if (site->lpVtbl->CanInPlaceActivate(site) != S_OK) {
        site->lpVtbl->Release(site);
        return E_FAIL;
    }
    site->lpVtbl->OnInPlaceActivate(site);

    RECT place = {0, 0, 0, 0}, clip = {0, 0, 0, 0};
    IOleInPlaceFrame *frame = nullptr;
    void *document = nullptr;
    OLEINPLACEFRAMEINFO info = {sizeof(info)};
    if (SUCCEEDED(site->lpVtbl->GetWindowContext(site, &frame, &document, &place, &clip, &info))) {
        if (frame)
            frame->lpVtbl->Release(frame);
        if (document)
            ((IUnknown *)document)->lpVtbl->Release((IUnknown *)document);
    }
    if (position)
        place = *position;
    if (!parent)
        site->lpVtbl->GetWindow(site, &parent);
    if (!parent) {
        site->lpVtbl->OnInPlaceDeactivate(site);
        site->lpVtbl->Release(site);
        return E_FAIL;
    }
    create_windows(b, parent, &place);
    b->InPlaceActive = TRUE;
    if (verb != -5)
        site->lpVtbl->OnUIActivate(site);
    site->lpVtbl->Release(site);
    if (b->PendingUrl) {
        LPWSTR url = b->PendingUrl;
        b->PendingUrl = nullptr;
        start_navigation(b, url);
        HeapFree(GetProcessHeap(), 0, url);
    }
    return S_OK;
}

static void deactivate(BROWSER *b)
{
    if (!b->InPlaceActive)
        return;
    if (b->Host) {
        HostDestroy(b->Host);
        b->Host = nullptr;
    }
    if (b->Window)
        DestroyWindow(b->Window);
    b->Window = nullptr;
    b->Content = nullptr;
    b->InPlaceActive = FALSE;
    if (b->Site) {
        IOleInPlaceSite *site;
        if (SUCCEEDED(b->Site->lpVtbl->QueryInterface(b->Site, &IID_IOleInPlaceSite, (void **)&site))) {
            site->lpVtbl->OnInPlaceDeactivate(site);
            site->lpVtbl->Release(site);
        }
    }
}

/* ------------------------------------------------------------------ IUnknown, shared by every face */

static HRESULT browser_query(BROWSER *b, REFIID iid, void **object)
{
    void *face = nullptr;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDispatch) || IsEqualIID(iid, &IID_IWebBrowser) ||
        IsEqualIID(iid, &IID_IWebBrowserApp) || IsEqualIID(iid, &IID_IWebBrowser2))
        face = &b->Browser2;
    else if (IsEqualIID(iid, &IID_IOleObject))
        face = &b->OleObject;
    else if (IsEqualIID(iid, &IID_IOleWindow) || IsEqualIID(iid, &IID_IOleInPlaceObject))
        face = &b->InPlace;
    else if (IsEqualIID(iid, &IID_IOleInPlaceActiveObject))
        face = &b->Active;
    else if (IsEqualIID(iid, &IID_IOleControl))
        face = &b->Control;
    else if (IsEqualIID(iid, &IID_IViewObject) || IsEqualIID(iid, &IID_IViewObject2))
        face = &b->View;
    else if (IsEqualIID(iid, &IID_IPersist) || IsEqualIID(iid, &IID_IPersistStream) ||
             IsEqualIID(iid, &IID_IPersistStreamInit))
        face = &b->Persist;
    else if (IsEqualIID(iid, &IID_IConnectionPointContainer))
        face = &b->Container;
    else if (IsEqualIID(iid, &IID_IProvideClassInfo_) || IsEqualIID(iid, &IID_IProvideClassInfo2))
        face = &b->ClassInfo;
    else if (IsEqualIID(iid, &IID_IOleCommandTarget))
        face = &b->Command;
    if (!face) {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&b->Refs);
    *object = face;
    return S_OK;
}

static ULONG browser_add_ref(BROWSER *b)
{
    return (ULONG)InterlockedIncrement(&b->Refs);
}

static void browser_free(BROWSER *b)
{
    deactivate(b);
    if (b->Site)
        b->Site->lpVtbl->Release(b->Site);
    if (b->Advise)
        b->Advise->lpVtbl->Release(b->Advise);
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < MAX_SINKS; i++)
            if (b->Points[p].Sinks[i])
                b->Points[p].Sinks[i]->lpVtbl->Release(b->Points[p].Sinks[i]);
    HeapFree(GetProcessHeap(), 0, b->Url);
    HeapFree(GetProcessHeap(), 0, b->Title);
    HeapFree(GetProcessHeap(), 0, b->Status);
    HeapFree(GetProcessHeap(), 0, b->PendingUrl);
    HeapFree(GetProcessHeap(), 0, b);
}

static ULONG browser_release(BROWSER *b)
{
    LONG refs = InterlockedDecrement(&b->Refs);
    if (refs == 0) {
        b->Refs = 1;
        browser_free(b);
    }
    return (ULONG)refs;
}

#define FACE_UNKNOWN(Face, Type, Member)                                                                               \
    static HRESULT STDMETHODCALLTYPE Face##_query(Type *self, REFIID iid, void **object)                               \
    {                                                                                                                  \
        return browser_query(BROWSER_FROM(Member, self), iid, object);                                                 \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE Face##_add_ref(Type *self)                                                          \
    {                                                                                                                  \
        return browser_add_ref(BROWSER_FROM(Member, self));                                                            \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE Face##_release(Type *self)                                                          \
    {                                                                                                                  \
        return browser_release(BROWSER_FROM(Member, self));                                                            \
    }

FACE_UNKNOWN(wb, IWebBrowser2, Browser2)
FACE_UNKNOWN(oo, IOleObject, OleObject)
FACE_UNKNOWN(ip, IOleInPlaceObject, InPlace)
FACE_UNKNOWN(ia, IOleInPlaceActiveObject, Active)
FACE_UNKNOWN(oc, IOleControl, Control)
FACE_UNKNOWN(vo, IViewObject2, View)
FACE_UNKNOWN(ps, IPersistStreamInit, Persist)
FACE_UNKNOWN(cc, IConnectionPointContainer, Container)
FACE_UNKNOWN(ci, IProvideClassInfo2, ClassInfo)
FACE_UNKNOWN(ct, IOleCommandTarget, Command)

/* ------------------------------------------------------------------ IOleObject */

static HRESULT STDMETHODCALLTYPE oo_set_client_site(IOleObject *self, IOleClientSite *site)
{
    BROWSER *b = BROWSER_FROM(OleObject, self);
    if (site)
        site->lpVtbl->AddRef(site);
    if (b->Site)
        b->Site->lpVtbl->Release(b->Site);
    b->Site = site;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_get_client_site(IOleObject *self, IOleClientSite **site)
{
    BROWSER *b = BROWSER_FROM(OleObject, self);
    *site = b->Site;
    if (b->Site)
        b->Site->lpVtbl->AddRef(b->Site);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_set_host_names(IOleObject *self, LPCOLESTR app, LPCOLESTR object)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_close(IOleObject *self, DWORD save_option)
{
    deactivate(BROWSER_FROM(OleObject, self));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_set_moniker(IOleObject *self, DWORD which, IMoniker *moniker)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE oo_get_moniker(IOleObject *self, DWORD assign, DWORD which, IMoniker **moniker)
{
    *moniker = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE oo_init_from_data(IOleObject *self, IDataObject *data, BOOL creation, DWORD reserved)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE oo_get_clipboard_data(IOleObject *self, DWORD reserved, IDataObject **data)
{
    *data = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE oo_do_verb(IOleObject *self, LONG verb, MSG *message, IOleClientSite *site, LONG index,
                                            HWND parent, LPCRECT position)
{
    BROWSER *b = BROWSER_FROM(OleObject, self);
    switch (verb) {
    case 0:  /* OLEIVERB_PRIMARY */
    case -1: /* OLEIVERB_SHOW */
    case -4: /* OLEIVERB_UIACTIVATE */
    case -5: /* OLEIVERB_INPLACEACTIVATE */
        if (site && !b->Site)
            oo_set_client_site(self, site);
        return activate(b, verb, parent, position);
    case -3: /* OLEIVERB_HIDE */
        if (b->Window)
            ShowWindow(b->Window, SW_HIDE);
        return S_OK;
    default:
        return OLEOBJ_E_NOVERBS;
    }
}

static HRESULT STDMETHODCALLTYPE oo_enum_verbs(IOleObject *self, IEnumOLEVERB **verbs)
{
    *verbs = nullptr;
    return OLEOBJ_E_NOVERBS;
}

static HRESULT STDMETHODCALLTYPE oo_update(IOleObject *self)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_is_up_to_date(IOleObject *self)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_get_user_class_id(IOleObject *self, CLSID *clsid)
{
    *clsid = CLSID_WebBrowser;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_get_user_type(IOleObject *self, DWORD form, LPOLESTR *type)
{
    static const WCHAR name[] = L"Microsoft Web Browser";
    *type = CoTaskMemAlloc(sizeof(name));
    if (!*type)
        return E_OUTOFMEMORY;
    memcpy(*type, name, sizeof(name));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_set_extent(IOleObject *self, DWORD aspect, SIZEL *size)
{
    if (aspect != DVASPECT_CONTENT)
        return DV_E_DVASPECT;
    BROWSER_FROM(OleObject, self)->Extent = *size;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_get_extent(IOleObject *self, DWORD aspect, SIZEL *size)
{
    if (aspect != DVASPECT_CONTENT)
        return DV_E_DVASPECT;
    *size = BROWSER_FROM(OleObject, self)->Extent;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_advise(IOleObject *self, IAdviseSink *sink, DWORD *connection)
{
    BROWSER *b = BROWSER_FROM(OleObject, self);
    if (b->Advise)
        return OLE_E_ADVISENOTSUPPORTED;
    sink->lpVtbl->AddRef(sink);
    b->Advise = sink;
    *connection = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_unadvise(IOleObject *self, DWORD connection)
{
    BROWSER *b = BROWSER_FROM(OleObject, self);
    if (connection != 1 || !b->Advise)
        return OLE_E_NOCONNECTION;
    b->Advise->lpVtbl->Release(b->Advise);
    b->Advise = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_enum_advise(IOleObject *self, IEnumSTATDATA **enumerator)
{
    *enumerator = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE oo_get_misc_status(IOleObject *self, DWORD aspect, DWORD *status)
{
    /* recompose on resize, cannot link inside, activate when visible, set the client site first. */
    *status = 0x20111;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oo_set_color_scheme(IOleObject *self, LOGPALETTE *palette)
{
    return E_NOTIMPL;
}

static const IOleObjectVtbl g_ole_object_vtbl = {
    oo_query,        oo_add_ref,       oo_release,           oo_set_client_site, oo_get_client_site, oo_set_host_names,
    oo_close,        oo_set_moniker,   oo_get_moniker,       oo_init_from_data,  oo_get_clipboard_data, oo_do_verb,
    oo_enum_verbs,   oo_update,        oo_is_up_to_date,     oo_get_user_class_id, oo_get_user_type, oo_set_extent,
    oo_get_extent,   oo_advise,        oo_unadvise,          oo_enum_advise,     oo_get_misc_status, oo_set_color_scheme};

/* ------------------------------------------------------------------ IOleInPlaceObject and the active object */

static HRESULT STDMETHODCALLTYPE ip_get_window(IOleInPlaceObject *self, HWND *window)
{
    BROWSER *b = BROWSER_FROM(InPlace, self);
    *window = b->Window;
    return b->Window ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE ip_context_help(IOleInPlaceObject *self, BOOL enter)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ip_in_place_deactivate(IOleInPlaceObject *self)
{
    deactivate(BROWSER_FROM(InPlace, self));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ip_ui_deactivate(IOleInPlaceObject *self)
{
    BROWSER *b = BROWSER_FROM(InPlace, self);
    if (b->Site) {
        IOleInPlaceSite *site;
        if (SUCCEEDED(b->Site->lpVtbl->QueryInterface(b->Site, &IID_IOleInPlaceSite, (void **)&site))) {
            site->lpVtbl->OnUIDeactivate(site, FALSE);
            site->lpVtbl->Release(site);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ip_set_object_rects(IOleInPlaceObject *self, LPCRECT position, LPCRECT clip)
{
    BROWSER *b = BROWSER_FROM(InPlace, self);
    if (b->Window && position)
        MoveWindow(b->Window, position->left, position->top, position->right - position->left,
                   position->bottom - position->top, TRUE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ip_reactivate_and_undo(IOleInPlaceObject *self)
{
    return E_NOTIMPL;
}

static const IOleInPlaceObjectVtbl g_in_place_vtbl = {ip_query,           ip_add_ref,        ip_release,
                                                      ip_get_window,      ip_context_help,   ip_in_place_deactivate,
                                                      ip_ui_deactivate,   ip_set_object_rects, ip_reactivate_and_undo};

static HRESULT STDMETHODCALLTYPE ia_get_window(IOleInPlaceActiveObject *self, HWND *window)
{
    BROWSER *b = BROWSER_FROM(Active, self);
    *window = b->Window;
    return b->Window ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE ia_context_help(IOleInPlaceActiveObject *self, BOOL enter)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ia_translate_accelerator(IOleInPlaceActiveObject *self, MSG *message)
{
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE ia_frame_activate(IOleInPlaceActiveObject *self, BOOL activate)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ia_resize_border(IOleInPlaceActiveObject *self, LPCRECT border, void *window,
                                                  BOOL frame)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ia_enable_modeless(IOleInPlaceActiveObject *self, BOOL enable)
{
    return S_OK;
}

static const IOleInPlaceActiveObjectVtbl g_active_vtbl = {ia_query,        ia_add_ref,      ia_release,
                                                          ia_get_window,   ia_context_help, ia_translate_accelerator,
                                                          ia_frame_activate, ia_frame_activate, ia_resize_border,
                                                          ia_enable_modeless};

/* ------------------------------------------------------------------ IOleControl, IViewObject2, IPersistStreamInit */

static HRESULT STDMETHODCALLTYPE oc_get_control_info(IOleControl *self, CONTROLINFO *info)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE oc_on_mnemonic(IOleControl *self, MSG *message)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oc_on_ambient(IOleControl *self, DISPID id)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE oc_freeze_events(IOleControl *self, BOOL freeze)
{
    return S_OK;
}

static const IOleControlVtbl g_control_vtbl = {oc_query,      oc_add_ref,   oc_release,
                                               oc_get_control_info, oc_on_mnemonic, oc_on_ambient, oc_freeze_events};

static HRESULT STDMETHODCALLTYPE vo_draw(IViewObject2 *self, DWORD aspect, LONG index, void *aspect_info,
                                         DVTARGETDEVICE *device, HDC target, HDC draw, const RECTL *bounds,
                                         const RECTL *window_bounds, BOOL(STDMETHODCALLTYPE *keep_going)(ULONG_PTR),
                                         ULONG_PTR argument)
{
    /* the page is a window of its own, a control that is not active shows an empty page. */
    if (draw && bounds) {
        RECT area = {bounds->left, bounds->top, bounds->right, bounds->bottom};
        FillRect(draw, &area, (HBRUSH)GetStockObject(WHITE_BRUSH));
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vo_get_color_set(IViewObject2 *self, DWORD aspect, LONG index, void *aspect_info,
                                                  DVTARGETDEVICE *device, HDC target, LOGPALETTE **colors)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vo_freeze(IViewObject2 *self, DWORD aspect, LONG index, void *aspect_info,
                                           DWORD *freeze)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vo_unfreeze(IViewObject2 *self, DWORD freeze)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vo_set_advise(IViewObject2 *self, DWORD aspects, DWORD flags, IAdviseSink *sink)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vo_get_advise(IViewObject2 *self, DWORD *aspects, DWORD *flags, IAdviseSink **sink)
{
    if (aspects)
        *aspects = 0;
    if (flags)
        *flags = 0;
    if (sink)
        *sink = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vo_get_extent(IViewObject2 *self, DWORD aspect, LONG index, DVTARGETDEVICE *device,
                                               SIZEL *size)
{
    *size = BROWSER_FROM(View, self)->Extent;
    return S_OK;
}

static const IViewObject2Vtbl g_view_vtbl = {vo_query,    vo_add_ref,     vo_release,   vo_draw,       vo_get_color_set,
                                             vo_freeze,   vo_unfreeze,    vo_set_advise, vo_get_advise, vo_get_extent};

static HRESULT STDMETHODCALLTYPE ps_get_class_id(IPersistStreamInit *self, CLSID *clsid)
{
    *clsid = CLSID_WebBrowser;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ps_is_dirty(IPersistStreamInit *self)
{
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE ps_load(IPersistStreamInit *self, IStream *stream)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ps_save(IPersistStreamInit *self, IStream *stream, BOOL clear_dirty)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ps_get_size_max(IPersistStreamInit *self, ULARGE_INTEGER *size)
{
    size->QuadPart = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ps_init_new(IPersistStreamInit *self)
{
    return S_OK;
}

static const IPersistStreamInitVtbl g_persist_vtbl = {ps_query,   ps_add_ref,     ps_release,      ps_get_class_id,
                                                      ps_is_dirty, ps_load,        ps_save,         ps_get_size_max,
                                                      ps_init_new};

/* ------------------------------------------------------------------ connection points */

static HRESULT STDMETHODCALLTYPE cp_query(IConnectionPoint *self, REFIID iid, void **object)
{
    CONNECTION_POINT *point = (CONNECTION_POINT *)self;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IConnectionPoint)) {
        browser_add_ref(point->Browser);
        *object = self;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE cp_add_ref(IConnectionPoint *self)
{
    return browser_add_ref(((CONNECTION_POINT *)self)->Browser);
}

static ULONG STDMETHODCALLTYPE cp_release(IConnectionPoint *self)
{
    return browser_release(((CONNECTION_POINT *)self)->Browser);
}

static HRESULT STDMETHODCALLTYPE cp_get_interface(IConnectionPoint *self, IID *iid)
{
    *iid = *((CONNECTION_POINT *)self)->Iid;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_get_container(IConnectionPoint *self, IConnectionPointContainer **container)
{
    BROWSER *b = ((CONNECTION_POINT *)self)->Browser;
    *container = &b->Container;
    browser_add_ref(b);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_advise(IConnectionPoint *self, IUnknown *sink, DWORD *cookie)
{
    CONNECTION_POINT *point = (CONNECTION_POINT *)self;
    if (!sink || !cookie)
        return E_POINTER;
    IDispatch *dispatch;
    if (FAILED(sink->lpVtbl->QueryInterface(sink, &IID_IDispatch, (void **)&dispatch)))
        return CONNECT_E_CANNOTCONNECT;
    for (int i = 0; i < MAX_SINKS; i++)
        if (!point->Sinks[i]) {
            point->Sinks[i] = dispatch;
            point->Cookies[i] = ++point->Browser->NextCookie;
            *cookie = point->Cookies[i];
            return S_OK;
        }
    dispatch->lpVtbl->Release(dispatch);
    return CONNECT_E_ADVISELIMIT;
}

static HRESULT STDMETHODCALLTYPE cp_unadvise(IConnectionPoint *self, DWORD cookie)
{
    CONNECTION_POINT *point = (CONNECTION_POINT *)self;
    for (int i = 0; i < MAX_SINKS; i++)
        if (point->Sinks[i] && point->Cookies[i] == cookie) {
            IDispatch *sink = point->Sinks[i];
            point->Sinks[i] = nullptr;
            sink->lpVtbl->Release(sink);
            return S_OK;
        }
    return CONNECT_E_NOCONNECTION;
}

static HRESULT STDMETHODCALLTYPE cp_enum_connections(IConnectionPoint *self, IEnumConnections **enumerator)
{
    *enumerator = nullptr;
    return E_NOTIMPL;
}

static const IConnectionPointVtbl g_point_vtbl = {cp_query,         cp_add_ref,      cp_release,         cp_get_interface,
                                                  cp_get_container, cp_advise,       cp_unadvise,        cp_enum_connections};

static HRESULT STDMETHODCALLTYPE cc_enum_points(IConnectionPointContainer *self, IEnumConnectionPoints **enumerator)
{
    *enumerator = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE cc_find_point(IConnectionPointContainer *self, REFIID iid, IConnectionPoint **point)
{
    BROWSER *b = BROWSER_FROM(Container, self);
    for (int i = 0; i < 2; i++)
        if (IsEqualIID(iid, b->Points[i].Iid)) {
            *point = (IConnectionPoint *)&b->Points[i];
            browser_add_ref(b);
            return S_OK;
        }
    *point = nullptr;
    return CONNECT_E_NOCONNECTION;
}

static const IConnectionPointContainerVtbl g_container_vtbl = {cc_query, cc_add_ref, cc_release, cc_enum_points,
                                                               cc_find_point};

static HRESULT STDMETHODCALLTYPE ci_get_class_info(IProvideClassInfo2 *self, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ci_get_guid(IProvideClassInfo2 *self, DWORD kind, GUID *guid)
{
    if (kind == 1) { /* GUIDKIND_DEFAULT_SOURCE_DISP_IID */
        *guid = DIID_DWebBrowserEvents2;
        return S_OK;
    }
    return E_INVALIDARG;
}

static const IProvideClassInfo2Vtbl g_class_info_vtbl = {ci_query, ci_add_ref, ci_release, ci_get_class_info,
                                                         ci_get_guid};

/* ------------------------------------------------------------------ IOleCommandTarget */

static HRESULT STDMETHODCALLTYPE ct_query_status(IOleCommandTarget *self, const GUID *group, ULONG count,
                                                 OLECMD *commands, OLECMDTEXT *text)
{
    for (ULONG i = 0; i < count; i++)
        commands[i].cmdf = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ct_exec(IOleCommandTarget *self, const GUID *group, DWORD id, DWORD options,
                                         VARIANT *in, VARIANT *out)
{
    return (HRESULT)0x80040100L; /* OLECMDERR_E_NOTSUPPORTED */
}

static const IOleCommandTargetVtbl g_command_vtbl = {ct_query, ct_add_ref, ct_release, ct_query_status, ct_exec};

/* ------------------------------------------------------------------ IWebBrowser2 */

static const struct {
    LPCWSTR Name;
    DISPID Id;
} g_names[] = {{L"GoBack", 100},         {L"GoForward", 101},   {L"GoHome", 102},        {L"GoSearch", 103},
               {L"Navigate", 104},       {L"Refresh", -550},    {L"Stop", 106},          {L"Application", 200},
               {L"Parent", 201},         {L"Container", 202},   {L"Document", 203},      {L"LocationName", 210},
               {L"LocationURL", 211},    {L"Busy", 212},        {L"HWND", -515},         {L"Navigate2", 500},
               {L"ReadyState", -525},    {L"Silent", 551}};

static HRESULT STDMETHODCALLTYPE wb_get_type_info_count(IWebBrowser2 *self, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_get_type_info(IWebBrowser2 *self, UINT index, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE wb_get_ids_of_names(IWebBrowser2 *self, REFIID iid, LPOLESTR *names, UINT count,
                                                     LCID lcid, DISPID *ids)
{
    HRESULT result = S_OK;
    for (UINT i = 0; i < count; i++) {
        ids[i] = DISPID_UNKNOWN;
        if (i == 0)
            for (UINT n = 0; n < ARRAYSIZE(g_names); n++)
                if (lstrcmpiW(names[0], g_names[n].Name) == 0)
                    ids[0] = g_names[n].Id;
        if (ids[i] == DISPID_UNKNOWN)
            result = DISP_E_UNKNOWNNAME;
    }
    return result;
}

static VARIANT *arg_at(DISPPARAMS *params, UINT index)
{
    if (index >= params->cArgs)
        return nullptr;
    return &params->rgvarg[params->cArgs - 1 - index];
}

static HRESULT STDMETHODCALLTYPE wb_invoke(IWebBrowser2 *self, DISPID id, REFIID iid, LCID lcid, WORD flags,
                                           DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    BROWSER *b = BROWSER_FROM(Browser2, self);
    HRESULT hr = S_OK;
    if (result)
        V_VT(result) = VT_EMPTY;
    switch (id) {
    case 100:
        return self->lpVtbl->GoBack(self);
    case 101:
        return self->lpVtbl->GoForward(self);
    case 102:
        return self->lpVtbl->GoHome(self);
    case 106:
        return self->lpVtbl->Stop(self);
    case -550:
        return self->lpVtbl->Refresh(self);
    case 104:
    case 500: {
        VARIANT *url = arg_at(params, 0);
        LPCWSTR text = variant_text(url);
        return text ? navigate(b, text) : DISP_E_PARAMNOTOPTIONAL;
    }
    case -525:
        if (result) {
            V_VT(result) = VT_I4;
            V_I4(result) = b->ReadyState;
        }
        return S_OK;
    case 211:
        if (result) {
            V_VT(result) = VT_BSTR;
            hr = out_bstr(b->Url, &V_BSTR(result));
        }
        return hr;
    case 210:
        if (result) {
            V_VT(result) = VT_BSTR;
            hr = out_bstr(b->Title ? b->Title : b->Url, &V_BSTR(result));
        }
        return hr;
    case 212:
        if (result) {
            V_VT(result) = VT_BOOL;
            V_BOOL(result) = b->Busy ? VARIANT_TRUE : VARIANT_FALSE;
        }
        return S_OK;
    case -515:
        if (result) {
            V_VT(result) = VT_I4;
            V_I4(result) = (LONG)(LONG_PTR)b->Window;
        }
        return S_OK;
    case 200:
        if (result) {
            V_VT(result) = VT_DISPATCH;
            V_DISPATCH(result) = self_dispatch(b);
            browser_add_ref(b);
        }
        return S_OK;
    case 203:
        return S_OK;
    default:
        return DISP_E_MEMBERNOTFOUND;
    }
}

static HRESULT STDMETHODCALLTYPE wb_go_back(IWebBrowser2 *self)
{
    BROWSER *b = BROWSER_FROM(Browser2, self);
    if (!b->Host)
        return E_FAIL;
    HostCommand(b->Host, HOST_GO_BACK);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_go_forward(IWebBrowser2 *self)
{
    BROWSER *b = BROWSER_FROM(Browser2, self);
    if (!b->Host)
        return E_FAIL;
    HostCommand(b->Host, HOST_GO_FORWARD);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_go_home(IWebBrowser2 *self)
{
    return navigate(BROWSER_FROM(Browser2, self), L"about:blank");
}

static HRESULT STDMETHODCALLTYPE wb_go_search(IWebBrowser2 *self)
{
    return navigate(BROWSER_FROM(Browser2, self), L"about:blank");
}

static HRESULT STDMETHODCALLTYPE wb_navigate(IWebBrowser2 *self, BSTR url, VARIANT *flags, VARIANT *target,
                                             VARIANT *post, VARIANT *headers)
{
    return navigate(BROWSER_FROM(Browser2, self), url);
}

static HRESULT STDMETHODCALLTYPE wb_navigate2(IWebBrowser2 *self, VARIANT *url, VARIANT *flags, VARIANT *target,
                                              VARIANT *post, VARIANT *headers)
{
    LPCWSTR text = variant_text(url);
    if (!text)
        return E_INVALIDARG;
    return navigate(BROWSER_FROM(Browser2, self), text);
}

static HRESULT STDMETHODCALLTYPE wb_refresh(IWebBrowser2 *self)
{
    BROWSER *b = BROWSER_FROM(Browser2, self);
    if (!b->Host)
        return S_OK;
    HostCommand(b->Host, HOST_RELOAD);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_refresh2(IWebBrowser2 *self, VARIANT *level)
{
    return wb_refresh(self);
}

static HRESULT STDMETHODCALLTYPE wb_stop(IWebBrowser2 *self)
{
    BROWSER *b = BROWSER_FROM(Browser2, self);
    if (b->Host)
        HostCommand(b->Host, HOST_STOP);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_get_application(IWebBrowser2 *self, IDispatch **value)
{
    BROWSER *b = BROWSER_FROM(Browser2, self);
    *value = self_dispatch(b);
    browser_add_ref(b);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_get_parent(IWebBrowser2 *self, IDispatch **value)
{
    return wb_get_application(self, value);
}

static HRESULT STDMETHODCALLTYPE wb_get_container(IWebBrowser2 *self, IDispatch **value)
{
    *value = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_get_document(IWebBrowser2 *self, IDispatch **value)
{
    /* the document object is the engine's; a program that asks for it gets none. */
    *value = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_get_top_level(IWebBrowser2 *self, VARIANT_BOOL *value)
{
    *value = VARIANT_TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_get_type(IWebBrowser2 *self, BSTR *value)
{
    return out_bstr(L"HTML Document", value);
}

static LONG window_edge(BROWSER *b, int which)
{
    RECT r = {0, 0, 0, 0};
    if (b->Window) {
        GetWindowRect(b->Window, &r);
        MapWindowPoints(nullptr, GetParent(b->Window), (POINT *)&r, 2);
    }
    switch (which) {
    case 0:
        return r.left;
    case 1:
        return r.top;
    case 2:
        return r.right - r.left;
    default:
        return r.bottom - r.top;
    }
}

static void move_edge(BROWSER *b, int which, LONG value)
{
    if (!b->Window)
        return;
    LONG v[4] = {window_edge(b, 0), window_edge(b, 1), window_edge(b, 2), window_edge(b, 3)};
    v[which] = value;
    MoveWindow(b->Window, v[0], v[1], v[2], v[3], TRUE);
}

#define EDGE_ACCESSORS(Name, Which)                                                                                    \
    static HRESULT STDMETHODCALLTYPE wb_get_##Name(IWebBrowser2 *self, LONG *value)                                    \
    {                                                                                                                  \
        *value = window_edge(BROWSER_FROM(Browser2, self), Which);                                                     \
        return S_OK;                                                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE wb_put_##Name(IWebBrowser2 *self, LONG value)                                     \
    {                                                                                                                  \
        move_edge(BROWSER_FROM(Browser2, self), Which, value);                                                         \
        return S_OK;                                                                                                   \
    }
EDGE_ACCESSORS(left, 0)
EDGE_ACCESSORS(top, 1)
EDGE_ACCESSORS(width, 2)
EDGE_ACCESSORS(height, 3)

static HRESULT STDMETHODCALLTYPE wb_get_location_name(IWebBrowser2 *self, BSTR *value)
{
    BROWSER *b = BROWSER_FROM(Browser2, self);
    return out_bstr(b->Title ? b->Title : b->Url, value);
}

static HRESULT STDMETHODCALLTYPE wb_get_location_url(IWebBrowser2 *self, BSTR *value)
{
    return out_bstr(BROWSER_FROM(Browser2, self)->Url, value);
}

static HRESULT STDMETHODCALLTYPE wb_get_busy(IWebBrowser2 *self, VARIANT_BOOL *value)
{
    *value = BROWSER_FROM(Browser2, self)->Busy ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_quit(IWebBrowser2 *self)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_client_to_window(IWebBrowser2 *self, int *cx, int *cy)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_put_property(IWebBrowser2 *self, BSTR name, VARIANT value)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_get_property(IWebBrowser2 *self, BSTR name, VARIANT *value)
{
    V_VT(value) = VT_EMPTY;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_get_name(IWebBrowser2 *self, BSTR *value)
{
    return out_bstr(L"Microsoft Web Browser", value);
}

static HRESULT STDMETHODCALLTYPE wb_get_hwnd(IWebBrowser2 *self, LONG_PTR *value)
{
    *value = (LONG_PTR)BROWSER_FROM(Browser2, self)->Window;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_get_full_name(IWebBrowser2 *self, BSTR *value)
{
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(g_ieframe, path, MAX_PATH);
    return out_bstr(path, value);
}

static HRESULT STDMETHODCALLTYPE wb_get_path(IWebBrowser2 *self, BSTR *value)
{
    WCHAR path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    return out_bstr(path, value);
}

#define FLAG_ACCESSORS(Name, Flag)                                                                                     \
    static HRESULT STDMETHODCALLTYPE wb_get_##Name(IWebBrowser2 *self, VARIANT_BOOL *value)                            \
    {                                                                                                                  \
        *value = BROWSER_FROM(Browser2, self)->Flags[Flag];                                                            \
        return S_OK;                                                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE wb_put_##Name(IWebBrowser2 *self, VARIANT_BOOL value)                             \
    {                                                                                                                  \
        BROWSER_FROM(Browser2, self)->Flags[Flag] = value ? VARIANT_TRUE : VARIANT_FALSE;                              \
        return S_OK;                                                                                                   \
    }
FLAG_ACCESSORS(visible, FLAG_VISIBLE)
FLAG_ACCESSORS(status_bar, FLAG_STATUS_BAR)
FLAG_ACCESSORS(menu_bar, FLAG_MENU_BAR)
FLAG_ACCESSORS(full_screen, FLAG_FULL_SCREEN)
FLAG_ACCESSORS(offline, FLAG_OFFLINE)
FLAG_ACCESSORS(silent, FLAG_SILENT)
FLAG_ACCESSORS(register_browser, FLAG_REGISTER_BROWSER)
FLAG_ACCESSORS(drop_target, FLAG_DROP_TARGET)
FLAG_ACCESSORS(theater, FLAG_THEATER)
FLAG_ACCESSORS(address_bar, FLAG_ADDRESS_BAR)
FLAG_ACCESSORS(resizable, FLAG_RESIZABLE)

static HRESULT STDMETHODCALLTYPE wb_get_status_text(IWebBrowser2 *self, BSTR *value)
{
    return out_bstr(BROWSER_FROM(Browser2, self)->Status, value);
}

static HRESULT STDMETHODCALLTYPE wb_put_status_text(IWebBrowser2 *self, BSTR value)
{
    set_string(&BROWSER_FROM(Browser2, self)->Status, value);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_get_tool_bar(IWebBrowser2 *self, int *value)
{
    *value = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_put_tool_bar(IWebBrowser2 *self, int value)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_query_status_wb(IWebBrowser2 *self, DWORD cmd, DWORD *status)
{
    *status = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wb_exec_wb(IWebBrowser2 *self, DWORD cmd, DWORD options, VARIANT *in, VARIANT *out)
{
    return (HRESULT)0x80040100L;
}

static HRESULT STDMETHODCALLTYPE wb_show_browser_bar(IWebBrowser2 *self, VARIANT *clsid, VARIANT *show, VARIANT *size)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE wb_get_ready_state(IWebBrowser2 *self, LONG *value)
{
    *value = BROWSER_FROM(Browser2, self)->ReadyState;
    return S_OK;
}

static const IWebBrowser2Vtbl g_browser_vtbl = {
    wb_query, wb_add_ref, wb_release, wb_get_type_info_count, wb_get_type_info, wb_get_ids_of_names, wb_invoke,
    wb_go_back, wb_go_forward, wb_go_home, wb_go_search, wb_navigate, wb_refresh, wb_refresh2, wb_stop,
    wb_get_application, wb_get_parent, wb_get_container, wb_get_document, wb_get_top_level, wb_get_type,
    wb_get_left, wb_put_left, wb_get_top, wb_put_top, wb_get_width, wb_put_width, wb_get_height, wb_put_height,
    wb_get_location_name, wb_get_location_url, wb_get_busy,
    wb_quit, wb_client_to_window, wb_put_property, wb_get_property, wb_get_name, wb_get_hwnd, wb_get_full_name,
    wb_get_path, wb_get_visible, wb_put_visible, wb_get_status_bar, wb_put_status_bar, wb_get_status_text,
    wb_put_status_text, wb_get_tool_bar, wb_put_tool_bar, wb_get_menu_bar, wb_put_menu_bar, wb_get_full_screen,
    wb_put_full_screen,
    wb_navigate2, wb_query_status_wb, wb_exec_wb, wb_show_browser_bar, wb_get_ready_state, wb_get_offline,
    wb_put_offline, wb_get_silent, wb_put_silent, wb_get_register_browser, wb_put_register_browser,
    wb_get_drop_target, wb_put_drop_target, wb_get_theater, wb_put_theater, wb_get_address_bar, wb_put_address_bar,
    wb_get_resizable, wb_put_resizable};

/* ------------------------------------------------------------------ the class */

static BROWSER *browser_new(void)
{
    BROWSER *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return nullptr;
    b->Browser2.lpVtbl = &g_browser_vtbl;
    b->OleObject.lpVtbl = &g_ole_object_vtbl;
    b->InPlace.lpVtbl = &g_in_place_vtbl;
    b->Active.lpVtbl = &g_active_vtbl;
    b->Control.lpVtbl = &g_control_vtbl;
    b->View.lpVtbl = &g_view_vtbl;
    b->Persist.lpVtbl = &g_persist_vtbl;
    b->Container.lpVtbl = &g_container_vtbl;
    b->ClassInfo.lpVtbl = &g_class_info_vtbl;
    b->Command.lpVtbl = &g_command_vtbl;
    b->Points[0] = (CONNECTION_POINT){&g_point_vtbl, b, &DIID_DWebBrowserEvents2, {}, {}};
    b->Points[1] = (CONNECTION_POINT){&g_point_vtbl, b, &DIID_DWebBrowserEvents, {}, {}};
    b->Refs = 1;
    b->Extent.cx = 7620;
    b->Extent.cy = 5080;
    b->ReadyState = READYSTATE_UNINITIALIZED;
    for (int i = 0; i < FLAG_COUNT; i++)
        b->Flags[i] = VARIANT_FALSE;
    b->Flags[FLAG_DROP_TARGET] = VARIANT_TRUE;
    b->Flags[FLAG_ADDRESS_BAR] = VARIANT_TRUE;
    b->Flags[FLAG_RESIZABLE] = VARIANT_TRUE;
    b->Flags[FLAG_VISIBLE] = VARIANT_TRUE;
    return b;
}

typedef struct FACTORY {
    const IClassFactoryVtbl *Vtbl;
} FACTORY;

static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *self, REFIID iid, void **object)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IClassFactory)) {
        *object = self;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *self)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *self)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *self, IUnknown *outer, REFIID iid, void **object)
{
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    BROWSER *b = browser_new();
    if (!b)
        return E_OUTOFMEMORY;
    HRESULT hr = browser_query(b, iid, object);
    browser_release(b);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *self, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {factory_query, factory_add_ref, factory_release, factory_create,
                                                 factory_lock};
static FACTORY g_factory = {&g_factory_vtbl};

IClassFactory *BrowserFactory(void)
{
    return (IClassFactory *)&g_factory;
}
