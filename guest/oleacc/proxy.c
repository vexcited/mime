/*
 * oleacc.dll: the accessible objects of windows and of their standard parts and controls, as active
 * accessibility's own proxies are for programs that do not implement IAccessible themselves. one proxy is a
 * window, or one of the objects of a window (client, title bar, scroll bars...), and its simple elements are
 * numbered children. see docs/oleacc.md.
 */
#define OLEACCAPI
#include <mime/win32/oleacc.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/winuser.h>
#include "oleacc_private.h"

#define E_DISCONNECTED ((HRESULT)0x80010108L)

static const IID IID_IAccessible_ = {0x618736e0, 0x3c3d, 0x11cf, {0x81, 0x0c, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const IID IID_IDispatch_ = {0x00020400, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID IID_IEnumVARIANT_ = {0x00020404, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID IID_IOleWindow_ = {0x00000114, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

/* ------------------------------------------------------------------ interfaces */

typedef struct IAccessibleVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *);
    ULONG(STDMETHODCALLTYPE *Release)(void *);
    HRESULT(STDMETHODCALLTYPE *GetTypeInfoCount)(void *, UINT *);
    HRESULT(STDMETHODCALLTYPE *GetTypeInfo)(void *, UINT, LCID, void **);
    HRESULT(STDMETHODCALLTYPE *GetIDsOfNames)(void *, REFIID, LPOLESTR *, UINT, LCID, DISPID *);
    HRESULT(STDMETHODCALLTYPE *Invoke)(void *, DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
    HRESULT(STDMETHODCALLTYPE *get_accParent)(void *, IDispatch **);
    HRESULT(STDMETHODCALLTYPE *get_accChildCount)(void *, LONG *);
    HRESULT(STDMETHODCALLTYPE *get_accChild)(void *, VARIANT, IDispatch **);
    HRESULT(STDMETHODCALLTYPE *get_accName)(void *, VARIANT, BSTR *);
    HRESULT(STDMETHODCALLTYPE *get_accValue)(void *, VARIANT, BSTR *);
    HRESULT(STDMETHODCALLTYPE *get_accDescription)(void *, VARIANT, BSTR *);
    HRESULT(STDMETHODCALLTYPE *get_accRole)(void *, VARIANT, VARIANT *);
    HRESULT(STDMETHODCALLTYPE *get_accState)(void *, VARIANT, VARIANT *);
    HRESULT(STDMETHODCALLTYPE *get_accHelp)(void *, VARIANT, BSTR *);
    HRESULT(STDMETHODCALLTYPE *get_accHelpTopic)(void *, BSTR *, VARIANT, LONG *);
    HRESULT(STDMETHODCALLTYPE *get_accKeyboardShortcut)(void *, VARIANT, BSTR *);
    HRESULT(STDMETHODCALLTYPE *get_accFocus)(void *, VARIANT *);
    HRESULT(STDMETHODCALLTYPE *get_accSelection)(void *, VARIANT *);
    HRESULT(STDMETHODCALLTYPE *get_accDefaultAction)(void *, VARIANT, BSTR *);
    HRESULT(STDMETHODCALLTYPE *accSelect)(void *, LONG, VARIANT);
    HRESULT(STDMETHODCALLTYPE *accLocation)(void *, LONG *, LONG *, LONG *, LONG *, VARIANT);
    HRESULT(STDMETHODCALLTYPE *accNavigate)(void *, LONG, VARIANT, VARIANT *);
    HRESULT(STDMETHODCALLTYPE *accHitTest)(void *, LONG, LONG, VARIANT *);
    HRESULT(STDMETHODCALLTYPE *accDoDefaultAction)(void *, VARIANT);
    HRESULT(STDMETHODCALLTYPE *put_accName)(void *, VARIANT, BSTR);
    HRESULT(STDMETHODCALLTYPE *put_accValue)(void *, VARIANT, BSTR);
} IAccessibleVtbl;

typedef struct IOleWindowVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *);
    ULONG(STDMETHODCALLTYPE *Release)(void *);
    HRESULT(STDMETHODCALLTYPE *GetWindow)(void *, HWND *);
    HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)(void *, BOOL);
} IOleWindowVtbl;

typedef struct OaEnumeratorVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *);
    ULONG(STDMETHODCALLTYPE *Release)(void *);
    HRESULT(STDMETHODCALLTYPE *Next)(void *, ULONG, VARIANT *, ULONG *);
    HRESULT(STDMETHODCALLTYPE *Skip)(void *, ULONG);
    HRESULT(STDMETHODCALLTYPE *Reset)(void *);
    HRESULT(STDMETHODCALLTYPE *Clone)(void *, void **);
} OaEnumeratorVtbl;

typedef struct PROXY {
    const IAccessibleVtbl *accessible;
    const IOleWindowVtbl *window;
    const OaEnumeratorVtbl *enumerator;
    LONG refs;
    HWND hwnd;
    LONG object;
    LONG next; /* the child the enumerator gives next, from 1 */
} PROXY;

static PROXY *from_window(void *p)
{
    return CONTAINING_RECORD(p, PROXY, window);
}

static PROXY *from_enumerator(void *p)
{
    return CONTAINING_RECORD(p, PROXY, enumerator);
}

/* ------------------------------------------------------------------ what a window is */

typedef enum {
    CLS_OTHER,
    CLS_BUTTON,
    CLS_EDIT,
    CLS_STATIC,
    CLS_LISTBOX,
    CLS_COMBOBOX,
    CLS_SCROLLBAR,
    CLS_LISTVIEW,
    CLS_TREEVIEW,
    CLS_PROGRESS,
    CLS_TRACKBAR,
    CLS_STATUSBAR,
    CLS_TOOLBAR,
    CLS_TAB,
    CLS_UPDOWN,
    CLS_TOOLTIP,
    CLS_DIALOG,
    CLS_MENU,
    CLS_ANIMATE,
    CLS_HOTKEY,
    CLS_IPADDRESS,
    CLS_LINK,
    CLS_RICHEDIT,
} CLASS;

static CLASS window_class(HWND hwnd)
{
    static const struct {
        const WCHAR *name;
        CLASS kind;
    } classes[] = {
        {L"Button", CLS_BUTTON},           {L"Edit", CLS_EDIT},
        {L"Static", CLS_STATIC},           {L"ListBox", CLS_LISTBOX},
        {L"ComboBox", CLS_COMBOBOX},       {L"ScrollBar", CLS_SCROLLBAR},
        {L"SysListView32", CLS_LISTVIEW},  {L"SysTreeView32", CLS_TREEVIEW},
        {L"msctls_progress32", CLS_PROGRESS}, {L"msctls_trackbar32", CLS_TRACKBAR},
        {L"msctls_statusbar32", CLS_STATUSBAR}, {L"ToolbarWindow32", CLS_TOOLBAR},
        {L"SysTabControl32", CLS_TAB},     {L"msctls_updown32", CLS_UPDOWN},
        {L"tooltips_class32", CLS_TOOLTIP}, {L"#32770", CLS_DIALOG},
        {L"#32768", CLS_MENU},             {L"SysAnimate32", CLS_ANIMATE},
        {L"msctls_hotkey32", CLS_HOTKEY},  {L"SysIPAddress32", CLS_IPADDRESS},
        {L"SysLink", CLS_LINK},            {L"RichEdit20W", CLS_RICHEDIT},
        {L"RICHEDIT50W", CLS_RICHEDIT},
    };
    WCHAR name[64];
    if (!GetClassNameW(hwnd, name, RTL_NUMBER_OF(name)))
        return CLS_OTHER;
    for (DWORD i = 0; i < RTL_NUMBER_OF(classes); i++)
        if (!_wcsicmp(name, classes[i].name))
            return classes[i].kind;
    return CLS_OTHER;
}

static LONG client_role(HWND hwnd, CLASS kind)
{
    LONG style = (LONG)GetWindowLongW(hwnd, GWL_STYLE);
    switch (kind) {
    case CLS_BUTTON:
        switch (style & 0xF) {
        case BS_CHECKBOX:
        case BS_AUTOCHECKBOX:
        case BS_3STATE:
        case BS_AUTO3STATE:
            return ROLE_SYSTEM_CHECKBUTTON;
        case BS_RADIOBUTTON:
        case BS_AUTORADIOBUTTON:
            return ROLE_SYSTEM_RADIOBUTTON;
        case BS_GROUPBOX:
            return ROLE_SYSTEM_GROUPING;
        case 0xC:
        case 0xD:
            return ROLE_SYSTEM_SPLITBUTTON;
        }
        return ROLE_SYSTEM_PUSHBUTTON;
    case CLS_EDIT:
    case CLS_RICHEDIT:
        return ROLE_SYSTEM_TEXT;
    case CLS_STATIC:
        switch (style & 0x1F) {
        case SS_ICON:
        case SS_BITMAP:
        case SS_ENHMETAFILE:
            return ROLE_SYSTEM_GRAPHIC;
        }
        return ROLE_SYSTEM_STATICTEXT;
    case CLS_LISTBOX:
    case CLS_LISTVIEW:
        return ROLE_SYSTEM_LIST;
    case CLS_COMBOBOX:
        return ROLE_SYSTEM_COMBOBOX;
    case CLS_SCROLLBAR:
        return ROLE_SYSTEM_SCROLLBAR;
    case CLS_TREEVIEW:
        return ROLE_SYSTEM_OUTLINE;
    case CLS_PROGRESS:
        return ROLE_SYSTEM_PROGRESSBAR;
    case CLS_TRACKBAR:
        return ROLE_SYSTEM_SLIDER;
    case CLS_STATUSBAR:
        return ROLE_SYSTEM_STATUSBAR;
    case CLS_TOOLBAR:
        return ROLE_SYSTEM_TOOLBAR;
    case CLS_TAB:
        return ROLE_SYSTEM_PAGETABLIST;
    case CLS_UPDOWN:
        return ROLE_SYSTEM_SPINBUTTON;
    case CLS_TOOLTIP:
        return ROLE_SYSTEM_TOOLTIP;
    case CLS_DIALOG:
        return ROLE_SYSTEM_DIALOG;
    case CLS_MENU:
        return ROLE_SYSTEM_MENUPOPUP;
    case CLS_ANIMATE:
        return ROLE_SYSTEM_ANIMATION;
    case CLS_HOTKEY:
        return ROLE_SYSTEM_HOTKEYFIELD;
    case CLS_IPADDRESS:
        return ROLE_SYSTEM_IPADDRESS;
    case CLS_LINK:
        return ROLE_SYSTEM_LINK;
    default:
        return ROLE_SYSTEM_CLIENT;
    }
}

/* ------------------------------------------------------------------ parts of a window */

/* the objects a window is made of, in the order of its children: title bar, menu bar, client, scroll bars, grip. */
static LONG window_parts(HWND hwnd, LONG parts[6])
{
    LONG style = (LONG)GetWindowLongW(hwnd, GWL_STYLE);
    LONG n = 0;
    if ((style & WS_CAPTION) == WS_CAPTION)
        parts[n++] = OBJID_TITLEBAR;
    if (!(style & WS_CHILD) && GetMenu(hwnd))
        parts[n++] = OBJID_MENU;
    parts[n++] = OBJID_CLIENT;
    if (style & WS_VSCROLL)
        parts[n++] = OBJID_VSCROLL;
    if (style & WS_HSCROLL)
        parts[n++] = OBJID_HSCROLL;
    if ((style & WS_VSCROLL) && (style & WS_HSCROLL))
        parts[n++] = OBJID_SIZEGRIP;
    return n;
}

/* the simple elements of a title bar: the buttons that the style gives the window. */
static LONG titlebar_buttons(HWND hwnd, LONG ids[5])
{
    LONG style = (LONG)GetWindowLongW(hwnd, GWL_STYLE), ex = (LONG)GetWindowLongW(hwnd, GWL_EXSTYLE);
    LONG n = 0;
    if (style & WS_SYSMENU) {
        if (style & WS_MINIMIZEBOX)
            ids[n++] = 2;
        if (style & WS_MAXIMIZEBOX)
            ids[n++] = 3;
        if (ex & WS_EX_CONTEXTHELP)
            ids[n++] = 4;
        ids[n++] = 5;
    }
    return n;
}

/* ------------------------------------------------------------------ text */

static HRESULT bstr_out(const WCHAR *text, BSTR *out)
{
    *out = nullptr;
    if (!text || !*text)
        return S_FALSE;
    *out = SysAllocString(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* the text of a window, as a control's name: the ampersand that marks a mnemonic gone, a doubled one kept as one. */
static void strip_mnemonic(WCHAR *text)
{
    WCHAR *to = text;
    for (WCHAR *from = text; *from; from++) {
        if (*from == '&') {
            if (from[1] == '&')
                *to++ = *++from;
            continue;
        }
        *to++ = *from;
    }
    *to = 0;
}

/* the mnemonic of a text, 0 if there is none. */
static WCHAR mnemonic(const WCHAR *text)
{
    for (; *text; text++)
        if (*text == '&') {
            if (text[1] == '&')
                text++;
            else
                return text[1];
        }
    return 0;
}

static BOOL window_text(HWND hwnd, WCHAR *text, int chars)
{
    text[0] = 0;
    int n = GetWindowTextW(hwnd, text, chars);
    return n > 0;
}

/* the name of a control that has no text of its own: the static control before it, without its colon. */
static BOOL label_text(HWND hwnd, WCHAR *text, int chars)
{
    HWND label = GetWindow(hwnd, GW_HWNDPREV);
    text[0] = 0;
    if (!label || window_class(label) != CLS_STATIC || !window_text(label, text, chars))
        return FALSE;
    strip_mnemonic(text);
    int n = (int)wcslen(text);
    while (n && (text[n - 1] == ':' || text[n - 1] == ' '))
        text[--n] = 0;
    return n > 0;
}

/* ------------------------------------------------------------------ state */

static LONG window_state(HWND hwnd)
{
    LONG style = (LONG)GetWindowLongW(hwnd, GWL_STYLE), state = 0;
    if (!IsWindowVisible(hwnd))
        state |= STATE_SYSTEM_INVISIBLE;
    if (!IsWindowEnabled(hwnd))
        state |= STATE_SYSTEM_UNAVAILABLE;
    if (style & WS_THICKFRAME)
        state |= STATE_SYSTEM_SIZEABLE;
    if ((style & WS_CAPTION) == WS_CAPTION)
        state |= STATE_SYSTEM_MOVEABLE;
    if (GetForegroundWindow() == hwnd || GetFocus() == hwnd)
        state |= STATE_SYSTEM_FOCUSED;
    state |= STATE_SYSTEM_FOCUSABLE;
    if (IsIconic(hwnd))
        state |= STATE_SYSTEM_INVISIBLE;
    return state;
}

static LONG client_state(HWND hwnd, CLASS kind)
{
    LONG style = (LONG)GetWindowLongW(hwnd, GWL_STYLE), state = 0;
    if (!IsWindowVisible(hwnd))
        state |= STATE_SYSTEM_INVISIBLE;
    if (!IsWindowEnabled(hwnd))
        state |= STATE_SYSTEM_UNAVAILABLE;
    if (GetFocus() == hwnd)
        state |= STATE_SYSTEM_FOCUSED;
    if (kind != CLS_OTHER && kind != CLS_STATIC && kind != CLS_DIALOG && kind != CLS_TOOLTIP && kind != CLS_STATUSBAR &&
        ((style & WS_TABSTOP) || kind == CLS_EDIT || kind == CLS_LISTBOX || kind == CLS_COMBOBOX))
        state |= STATE_SYSTEM_FOCUSABLE;
    switch (kind) {
    case CLS_BUTTON: {
        LONG type = style & 0xF;
        LRESULT check = SendMessageW(hwnd, BM_GETCHECK, 0, 0);
        if (type != BS_GROUPBOX && (type != BS_PUSHBUTTON && type != BS_DEFPUSHBUTTON))
            state |= check == BST_CHECKED ? STATE_SYSTEM_CHECKED : check == BST_INDETERMINATE ? STATE_SYSTEM_MIXED : 0;
        if (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED)
            state |= STATE_SYSTEM_PRESSED;
        if (type == BS_DEFPUSHBUTTON)
            state |= STATE_SYSTEM_DEFAULT;
        break;
    }
    case CLS_EDIT:
    case CLS_RICHEDIT:
        if (style & ES_READONLY)
            state |= STATE_SYSTEM_READONLY;
        if (style & ES_PASSWORD)
            state |= STATE_SYSTEM_PROTECTED;
        break;
    case CLS_LISTBOX:
        if (style & LBS_MULTIPLESEL)
            state |= STATE_SYSTEM_MULTISELECTABLE;
        if (style & LBS_EXTENDEDSEL)
            state |= STATE_SYSTEM_EXTSELECTABLE;
        break;
    case CLS_COMBOBOX:
        state |= SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0) ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED;
        state |= STATE_SYSTEM_HASPOPUP;
        break;
    default:
        break;
    }
    return state;
}

/* ------------------------------------------------------------------ geometry */

static void set_rect(const RECT *r, LONG *left, LONG *top, LONG *width, LONG *height)
{
    *left = r->left;
    *top = r->top;
    *width = r->right - r->left;
    *height = r->bottom - r->top;
}

static BOOL client_screen_rect(HWND hwnd, RECT *r)
{
    POINT origin = {0, 0};
    if (!GetClientRect(hwnd, r) || !ClientToScreen(hwnd, &origin))
        return FALSE;
    OffsetRect(r, origin.x, origin.y);
    return TRUE;
}

/* the title bar, the scroll bars and the size grip lie where the window's frame leaves room for them. */
static BOOL part_rect(HWND hwnd, LONG object, RECT *r)
{
    RECT window, client;
    if (!GetWindowRect(hwnd, &window) || !client_screen_rect(hwnd, &client))
        return FALSE;
    LONG style = (LONG)GetWindowLongW(hwnd, GWL_STYLE);
    switch (object) {
    case OBJID_TITLEBAR: {
        int frame = (style & WS_THICKFRAME) ? GetSystemMetrics(SM_CXSIZEFRAME) : GetSystemMetrics(SM_CXFIXEDFRAME);
        *r = window;
        r->left += frame;
        r->right -= frame;
        r->top += frame;
        r->bottom = r->top + GetSystemMetrics(SM_CYCAPTION);
        return TRUE;
    }
    case OBJID_MENU:
        *r = client;
        r->bottom = r->top;
        r->top -= GetSystemMetrics(SM_CYMENU);
        return TRUE;
    case OBJID_VSCROLL:
        *r = client;
        r->left = r->right;
        r->right += GetSystemMetrics(SM_CXVSCROLL);
        return TRUE;
    case OBJID_HSCROLL:
        *r = client;
        r->top = r->bottom;
        r->bottom += GetSystemMetrics(SM_CYHSCROLL);
        return TRUE;
    case OBJID_SIZEGRIP:
        r->left = client.right;
        r->top = client.bottom;
        r->right = r->left + GetSystemMetrics(SM_CXVSCROLL);
        r->bottom = r->top + GetSystemMetrics(SM_CYHSCROLL);
        return TRUE;
    case OBJID_SYSMENU:
        *r = window;
        r->left += GetSystemMetrics(SM_CXSIZEFRAME);
        r->top += GetSystemMetrics(SM_CYSIZEFRAME);
        r->right = r->left + GetSystemMetrics(SM_CXSMICON);
        r->bottom = r->top + GetSystemMetrics(SM_CYCAPTION);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ children */

static HRESULT window_object(HWND hwnd, IDispatch **out)
{
    return AccessibleObjectFromWindow(hwnd, OBJID_WINDOW, &IID_IDispatch_, (void **)out);
}

/* the windows directly below a window, the topmost first. */
static LONG child_windows(HWND hwnd, HWND *list, LONG capacity)
{
    LONG n = 0;
    for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (list && n < capacity)
            list[n] = child;
        n++;
    }
    return n;
}

static HWND nth_child(HWND hwnd, LONG id)
{
    LONG n = 0;
    for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        if (++n == id)
            return child;
    return nullptr;
}

/* what the numbered children of a proxy are. */
typedef enum { KIDS_NONE, KIDS_PARTS, KIDS_WINDOWS, KIDS_BUTTONS, KIDS_SCROLL, KIDS_ITEMS, KIDS_MENU } KIDS;

static KIDS kids_of(const PROXY *p, CLASS *kind)
{
    *kind = CLS_OTHER;
    switch (p->object) {
    case OBJID_WINDOW:
        return KIDS_PARTS;
    case OBJID_CLIENT:
        *kind = window_class(p->hwnd);
        if (*kind == CLS_LISTBOX)
            return KIDS_ITEMS;
        if (*kind == CLS_COMBOBOX || *kind == CLS_BUTTON || *kind == CLS_EDIT || *kind == CLS_STATIC)
            return KIDS_NONE;
        return KIDS_WINDOWS;
    case OBJID_TITLEBAR:
        return KIDS_BUTTONS;
    case OBJID_VSCROLL:
    case OBJID_HSCROLL:
        return KIDS_SCROLL;
    case OBJID_MENU:
        return KIDS_MENU;
    }
    return KIDS_NONE;
}

static LONG child_count(const PROXY *p)
{
    CLASS kind;
    LONG ids[6];
    switch (kids_of(p, &kind)) {
    case KIDS_PARTS:
        return window_parts(p->hwnd, ids);
    case KIDS_WINDOWS:
        return child_windows(p->hwnd, nullptr, 0);
    case KIDS_BUTTONS:
        return titlebar_buttons(p->hwnd, ids);
    case KIDS_SCROLL:
        return 5;
    case KIDS_ITEMS: {
        LRESULT n = SendMessageW(p->hwnd, LB_GETCOUNT, 0, 0);
        return n < 0 ? 0 : (LONG)n;
    }
    case KIDS_MENU: {
        HMENU menu = GetMenu(p->hwnd);
        int n = menu ? GetMenuItemCount(menu) : 0;
        return n < 0 ? 0 : n;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ the checks every method makes */

static HRESULT child_id(VARIANT v, LONG *id)
{
    if (v.vt != VT_I4)
        return E_INVALIDARG;
    *id = v.lVal;
    return S_OK;
}

static HRESULT live(const PROXY *p)
{
    return p->hwnd == nullptr ? S_OK : IsWindow(p->hwnd) ? S_OK : E_DISCONNECTED;
}

/* a child that is a window of its own: the call goes to it, as the child itself. */
static BOOL child_proxy(const PROXY *p, LONG id, IUnknown **target)
{
    CLASS kind;
    *target = nullptr;
    if (id <= 0 || kids_of(p, &kind) != KIDS_WINDOWS)
        return FALSE;
    HWND child = nth_child(p->hwnd, id);
    if (!child)
        return FALSE;
    IDispatch *dispatch;
    if (FAILED(window_object(child, &dispatch)) || !dispatch)
        return FALSE;
    *target = (IUnknown *)dispatch;
    return TRUE;
}

/* a part of a window that a child number names. */
static BOOL part_of(const PROXY *p, LONG id, IUnknown **target)
{
    LONG parts[6];
    CLASS kind;
    *target = nullptr;
    if (id <= 0 || kids_of(p, &kind) != KIDS_PARTS || id > window_parts(p->hwnd, parts))
        return FALSE;
    void *object;
    if (FAILED(CreateStdAccessibleObject(p->hwnd, parts[id - 1], &IID_IUnknown, &object)))
        return FALSE;
    *target = object;
    return TRUE;
}

typedef HRESULT(STDMETHODCALLTYPE *STRING_GETTER)(void *, VARIANT, BSTR *);

/* children that are objects of their own answer for themselves. */
static BOOL delegate(const PROXY *p, LONG id, IAccessible **target)
{
    IUnknown *unknown;
    if (!part_of(p, id, &unknown) && !child_proxy(p, id, &unknown))
        return FALSE;
    HRESULT hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IAccessible_, (void **)target);
    unknown->lpVtbl->Release(unknown);
    return SUCCEEDED(hr);
}

static const IAccessibleVtbl *vtbl_of(IAccessible *a)
{
    return *(const IAccessibleVtbl **)a;
}

static VARIANT self_variant(void)
{
    VARIANT v;
    memset(&v, 0, sizeof(v));
    v.vt = VT_I4;
    v.lVal = CHILDID_SELF;
    return v;
}

#define DELEGATE_STRING(p, id, field, out)                                                                            \
    do {                                                                                                              \
        IAccessible *delegate_target;                                                                                 \
        if (delegate(p, id, &delegate_target)) {                                                                      \
            HRESULT delegate_hr = vtbl_of(delegate_target)->field(delegate_target, self_variant(), out);              \
            ((IUnknown *)delegate_target)->lpVtbl->Release((IUnknown *)delegate_target);                              \
            return delegate_hr;                                                                                       \
        }                                                                                                             \
    } while (0)

/* ------------------------------------------------------------------ IAccessible */

static HRESULT STDMETHODCALLTYPE acc_query_interface(void *self, REFIID riid, void **out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch_) || IsEqualIID(riid, &IID_IAccessible_))
        *out = &p->accessible;
    else if (IsEqualIID(riid, &IID_IOleWindow_))
        *out = &p->window;
    else if (IsEqualIID(riid, &IID_IEnumVARIANT_))
        *out = &p->enumerator;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&p->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE acc_add_ref(void *self)
{
    return (ULONG)InterlockedIncrement(&((PROXY *)self)->refs);
}

static ULONG STDMETHODCALLTYPE acc_release(void *self)
{
    PROXY *p = self;
    LONG refs = InterlockedDecrement(&p->refs);
    if (!refs)
        LocalFree(p);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE acc_get_type_info_count(void *self, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE acc_get_type_info(void *self, UINT index, LCID lcid, void **info)
{
    if (info)
        *info = nullptr;
    return E_NOTIMPL;
}

static const struct {
    DISPID id;
    const WCHAR *name;
} dispids[] = {
    {-5000, L"accParent"},         {-5001, L"accChildCount"},       {-5002, L"accChild"},
    {-5003, L"accName"},           {-5004, L"accValue"},            {-5005, L"accDescription"},
    {-5006, L"accRole"},           {-5007, L"accState"},            {-5008, L"accHelp"},
    {-5009, L"accHelpTopic"},      {-5010, L"accKeyboardShortcut"}, {-5011, L"accFocus"},
    {-5012, L"accSelection"},      {-5013, L"accDefaultAction"},    {-5014, L"accSelect"},
    {-5015, L"accLocation"},       {-5016, L"accNavigate"},         {-5017, L"accHitTest"},
    {-5018, L"accDoDefaultAction"},
};

static HRESULT STDMETHODCALLTYPE acc_get_ids_of_names(void *self, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                                      DISPID *ids)
{
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count; i++) {
        ids[i] = DISPID_UNKNOWN;
        if (i)
            continue;
        for (DWORD j = 0; j < RTL_NUMBER_OF(dispids); j++)
            if (!_wcsicmp(names[0], dispids[j].name))
                ids[0] = dispids[j].id;
    }
    for (UINT i = 0; i < count; i++)
        if (ids[i] == DISPID_UNKNOWN)
            hr = DISP_E_UNKNOWNNAME;
    return hr;
}

/* arguments of a call by automation come last first. */
static VARIANT *argument(DISPPARAMS *params, UINT index)
{
    return index < params->cArgs ? &params->rgvarg[params->cArgs - 1 - index] : nullptr;
}

static VARIANT child_argument(DISPPARAMS *params, UINT index)
{
    VARIANT *v = argument(params, index);
    if (v && v->vt == VT_I4)
        return *v;
    return self_variant();
}

static HRESULT STDMETHODCALLTYPE acc_invoke(void *self, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                            DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception, UINT *error)
{
    const IAccessibleVtbl *v = (*(PROXY *)self).accessible;
    VARIANT scratch;
    if (!result)
        result = &scratch;
    VariantInit(result);
    if (!params)
        return DISP_E_BADPARAMCOUNT;
    HRESULT hr;
    switch (id) {
    case -5000:
        result->vt = VT_DISPATCH;
        return v->get_accParent(self, &result->pdispVal);
    case -5001:
        result->vt = VT_I4;
        return v->get_accChildCount(self, &result->lVal);
    case -5002:
        result->vt = VT_DISPATCH;
        return v->get_accChild(self, child_argument(params, 0), &result->pdispVal);
    case -5003:
    case -5004:
    case -5005:
    case -5008:
    case -5010:
    case -5013: {
        if (id == -5003 || id == -5004) {
            if (flags & DISPATCH_PROPERTYPUT) {
                VARIANT *text = argument(params, 0);
                if (!text || text->vt != VT_BSTR)
                    return DISP_E_TYPEMISMATCH;
                return id == -5003 ? v->put_accName(self, child_argument(params, 1), text->bstrVal)
                                   : v->put_accValue(self, child_argument(params, 1), text->bstrVal);
            }
        }
        HRESULT (STDMETHODCALLTYPE *getter)(void *, VARIANT, BSTR *) =
            id == -5003 ? v->get_accName : id == -5004 ? v->get_accValue : id == -5005 ? v->get_accDescription
            : id == -5008 ? v->get_accHelp : id == -5010 ? v->get_accKeyboardShortcut : v->get_accDefaultAction;
        result->vt = VT_BSTR;
        hr = getter(self, child_argument(params, 0), &result->bstrVal);
        if (!result->bstrVal)
            result->vt = VT_EMPTY;
        return hr;
    }
    case -5006:
        return v->get_accRole(self, child_argument(params, 0), result);
    case -5007:
        return v->get_accState(self, child_argument(params, 0), result);
    case -5009:
        return DISP_E_MEMBERNOTFOUND;
    case -5011:
        return v->get_accFocus(self, result);
    case -5012:
        return v->get_accSelection(self, result);
    case -5014: {
        VARIANT *flag = argument(params, 0);
        if (!flag || flag->vt != VT_I4)
            return DISP_E_TYPEMISMATCH;
        return v->accSelect(self, flag->lVal, child_argument(params, 1));
    }
    case -5015: {
        VARIANT *out[4] = {argument(params, 0), argument(params, 1), argument(params, 2), argument(params, 3)};
        LONG values[4] = {};
        for (int i = 0; i < 4; i++)
            if (!out[i] || out[i]->vt != (VT_BYREF | VT_I4) || !out[i]->byref)
                return DISP_E_TYPEMISMATCH;
        hr = v->accLocation(self, &values[0], &values[1], &values[2], &values[3], child_argument(params, 4));
        for (int i = 0; i < 4; i++)
            *(LONG *)out[i]->byref = values[i];
        return hr;
    }
    case -5016: {
        VARIANT *dir = argument(params, 0);
        if (!dir || dir->vt != VT_I4)
            return DISP_E_TYPEMISMATCH;
        return v->accNavigate(self, dir->lVal, child_argument(params, 1), result);
    }
    case -5017: {
        VARIANT *x = argument(params, 0), *y = argument(params, 1);
        if (!x || !y || x->vt != VT_I4 || y->vt != VT_I4)
            return DISP_E_TYPEMISMATCH;
        return v->accHitTest(self, x->lVal, y->lVal, result);
    }
    case -5018:
        return v->accDoDefaultAction(self, child_argument(params, 0));
    }
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT STDMETHODCALLTYPE acc_get_parent(void *self, IDispatch **out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    HRESULT hr = live(p);
    if (FAILED(hr))
        return hr;
    HWND target;
    LONG object;
    switch (p->object) {
    case OBJID_WINDOW: {
        /* a child window's parent is the client of the window above it, a top level window's is the desktop. */
        HWND parent = GetAncestor(p->hwnd, GA_PARENT);
        if (!parent)
            return S_FALSE;
        target = parent;
        object = (LONG)GetWindowLongW(p->hwnd, GWL_STYLE) & WS_CHILD ? OBJID_CLIENT : OBJID_WINDOW;
        break;
    }
    default:
        target = p->hwnd;
        object = OBJID_WINDOW;
        break;
    }
    if (target == GetDesktopWindow() && object == OBJID_WINDOW)
        return AccessibleObjectFromWindow(target, OBJID_WINDOW, &IID_IDispatch_, (void **)out);
    return AccessibleObjectFromWindow(target, object, &IID_IDispatch_, (void **)out);
}

static HRESULT STDMETHODCALLTYPE acc_get_child_count(void *self, LONG *count)
{
    PROXY *p = self;
    if (!count)
        return E_POINTER;
    *count = 0;
    HRESULT hr = live(p);
    if (FAILED(hr))
        return hr;
    *count = child_count(p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE acc_get_child(void *self, VARIANT var, IDispatch **out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    if (id == CHILDID_SELF) {
        acc_add_ref(p);
        *out = (IDispatch *)&p->accessible;
        return S_OK;
    }
    if (id < 0 || id > child_count(p))
        return E_INVALIDARG;
    IUnknown *unknown;
    if (part_of(p, id, &unknown) || child_proxy(p, id, &unknown)) {
        hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IDispatch_, (void **)out);
        unknown->lpVtbl->Release(unknown);
        return hr;
    }
    /* a simple element has no object of its own. */
    return S_FALSE;
}

/* the name of one of the objects, its title bar buttons and scroll bar pieces named by numbers. */
static const WCHAR *element_name(const PROXY *p, LONG id)
{
    LONG ids[5];
    switch (p->object) {
    case OBJID_TITLEBAR:
        if (id >= 1 && id <= titlebar_buttons(p->hwnd, ids)) {
            LONG button = ids[id - 1];
            if (button == 3 && IsZoomed(p->hwnd))
                return OaString(146);
            return OaString(button == 2 ? 142 : button == 3 ? 143 : button == 4 ? 144 : 145);
        }
        return nullptr;
    case OBJID_VSCROLL:
        return id >= 1 && id <= 5 ? OaString(180 + id) : nullptr;
    case OBJID_HSCROLL:
        return id >= 1 && id <= 5 ? OaString(186 + id) : nullptr;
    }
    return nullptr;
}

static HRESULT STDMETHODCALLTYPE acc_get_name(void *self, VARIANT var, BSTR *out)
{
    PROXY *p = self;
    WCHAR text[512];
    if (!out)
        return E_POINTER;
    *out = nullptr;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    DELEGATE_STRING(p, id, get_accName, out);
    CLASS kind;
    KIDS kids = kids_of(p, &kind);
    if (id) {
        if (id < 0 || id > child_count(p))
            return E_INVALIDARG;
        if (kids == KIDS_ITEMS) {
            LRESULT n = SendMessageW(p->hwnd, LB_GETTEXTLEN, (WPARAM)(id - 1), 0);
            if (n < 0 || n >= (LRESULT)RTL_NUMBER_OF(text))
                return S_FALSE;
            SendMessageW(p->hwnd, LB_GETTEXT, (WPARAM)(id - 1), (LPARAM)text);
            return bstr_out(text, out);
        }
        if (kids == KIDS_MENU) {
            HMENU menu = GetMenu(p->hwnd);
            if (!menu || !GetMenuStringW(menu, (UINT)(id - 1), text, RTL_NUMBER_OF(text), MF_BYPOSITION))
                return S_FALSE;
            strip_mnemonic(text);
            return bstr_out(text, out);
        }
        return bstr_out(element_name(p, id), out);
    }
    switch (p->object) {
    case OBJID_WINDOW:
    case OBJID_TITLEBAR:
        window_text(p->hwnd, text, RTL_NUMBER_OF(text));
        return bstr_out(text, out);
    case OBJID_SYSMENU:
        return bstr_out(OaString(251), out);
    case OBJID_MENU:
        return bstr_out(OaString(250), out);
    case OBJID_VSCROLL:
        return bstr_out(OaString(180), out);
    case OBJID_HSCROLL:
        return bstr_out(OaString(186), out);
    case OBJID_SIZEGRIP:
        return bstr_out(OaString(192), out);
    case OBJID_CARET:
        return bstr_out(OaRoleName(ROLE_SYSTEM_CARET), out);
    case OBJID_CURSOR:
        return bstr_out(OaString(111), out);
    case OBJID_CLIENT:
        kind = window_class(p->hwnd);
        if (kind == CLS_BUTTON || kind == CLS_STATIC || kind == CLS_LINK) {
            window_text(p->hwnd, text, RTL_NUMBER_OF(text));
            strip_mnemonic(text);
            return bstr_out(text, out);
        }
        if (kind == CLS_EDIT || kind == CLS_LISTBOX || kind == CLS_COMBOBOX || kind == CLS_RICHEDIT ||
            kind == CLS_TRACKBAR || kind == CLS_PROGRESS) {
            label_text(p->hwnd, text, RTL_NUMBER_OF(text));
            return bstr_out(text, out);
        }
        window_text(p->hwnd, text, RTL_NUMBER_OF(text));
        return bstr_out(text, out);
    }
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE acc_get_value(void *self, VARIANT var, BSTR *out)
{
    PROXY *p = self;
    WCHAR text[1024];
    if (!out)
        return E_POINTER;
    *out = nullptr;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    DELEGATE_STRING(p, id, get_accValue, out);
    if (id)
        return id < 0 || id > child_count(p) ? E_INVALIDARG : S_FALSE;
    if (p->object == OBJID_VSCROLL || p->object == OBJID_HSCROLL) {
        SCROLLINFO info = {sizeof(info), SIF_ALL};
        if (!GetScrollInfo(p->hwnd, p->object == OBJID_VSCROLL ? SB_VERT : SB_HORZ, &info))
            return S_FALSE;
        int range = info.nMax - info.nMin - (info.nPage > 1 ? (int)info.nPage - 1 : 0);
        int percent = range > 0 ? (info.nPos - info.nMin) * 100 / range : 0;
        _snwprintf(text, RTL_NUMBER_OF(text), L"%d%%", percent);
        return bstr_out(text, out);
    }
    if (p->object != OBJID_CLIENT)
        return S_FALSE;
    CLASS kind = window_class(p->hwnd);
    LONG style = (LONG)GetWindowLongW(p->hwnd, GWL_STYLE);
    switch (kind) {
    case CLS_EDIT:
    case CLS_RICHEDIT:
        if (style & ES_PASSWORD)
            return S_FALSE;
        window_text(p->hwnd, text, RTL_NUMBER_OF(text));
        return bstr_out(text, out);
    case CLS_COMBOBOX:
        window_text(p->hwnd, text, RTL_NUMBER_OF(text));
        return bstr_out(text, out);
    case CLS_PROGRESS: {
        LRESULT pos = SendMessageW(p->hwnd, 0x0408 /* PBM_GETPOS */, 0, 0);
        _snwprintf(text, RTL_NUMBER_OF(text), L"%d%%", (int)pos);
        return bstr_out(text, out);
    }
    case CLS_TRACKBAR: {
        LRESULT pos = SendMessageW(p->hwnd, 0x0400 /* TBM_GETPOS */, 0, 0);
        _snwprintf(text, RTL_NUMBER_OF(text), L"%d", (int)pos);
        return bstr_out(text, out);
    }
    default:
        return S_FALSE;
    }
}

static HRESULT STDMETHODCALLTYPE acc_get_description(void *self, VARIANT var, BSTR *out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    DELEGATE_STRING(p, id, get_accDescription, out);
    LONG ids[5];
    if (id) {
        if (id < 0 || id > child_count(p))
            return E_INVALIDARG;
        if (p->object == OBJID_TITLEBAR && id <= titlebar_buttons(p->hwnd, ids)) {
            LONG button = ids[id - 1];
            return bstr_out(OaString(button == 2 ? 152 : button == 3 ? 153 : button == 4 ? 154 : 155), out);
        }
        if (p->object == OBJID_VSCROLL)
            return bstr_out(OaString(200 + id), out);
        if (p->object == OBJID_HSCROLL)
            return bstr_out(OaString(206 + id), out);
        return S_FALSE;
    }
    switch (p->object) {
    case OBJID_TITLEBAR:
        return bstr_out(OaString(150), out);
    case OBJID_SYSMENU:
        return bstr_out(OaString(253), out);
    case OBJID_MENU:
        return bstr_out(OaString(252), out);
    case OBJID_VSCROLL:
        return bstr_out(OaString(200), out);
    case OBJID_HSCROLL:
        return bstr_out(OaString(206), out);
    case OBJID_SIZEGRIP:
        return bstr_out(OaString(212), out);
    }
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE acc_get_role(void *self, VARIANT var, VARIANT *out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    VariantInit(out);
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    IAccessible *target;
    if (delegate(p, id, &target)) {
        hr = vtbl_of(target)->get_accRole(target, self_variant(), out);
        ((IUnknown *)target)->lpVtbl->Release((IUnknown *)target);
        return hr;
    }
    CLASS kind;
    KIDS kids = kids_of(p, &kind);
    LONG role = 0;
    if (id) {
        if (id < 0 || id > child_count(p))
            return E_INVALIDARG;
        role = kids == KIDS_ITEMS ? ROLE_SYSTEM_LISTITEM : kids == KIDS_MENU ? ROLE_SYSTEM_MENUITEM
               : kids == KIDS_SCROLL ? (id == 3 ? ROLE_SYSTEM_INDICATOR : ROLE_SYSTEM_PUSHBUTTON)
                                     : ROLE_SYSTEM_PUSHBUTTON;
    } else {
        switch (p->object) {
        case OBJID_WINDOW:
            kind = window_class(p->hwnd);
            role = kind == CLS_DIALOG ? ROLE_SYSTEM_DIALOG : kind == CLS_TOOLTIP ? ROLE_SYSTEM_TOOLTIP
                   : kind == CLS_MENU ? ROLE_SYSTEM_MENUPOPUP : ROLE_SYSTEM_WINDOW;
            if (kind != CLS_DIALOG && kind != CLS_TOOLTIP && kind != CLS_MENU && (GetWindowLongW(p->hwnd, GWL_STYLE) & WS_CHILD))
                role = client_role(p->hwnd, kind);
            break;
        case OBJID_SYSMENU:
            role = ROLE_SYSTEM_MENUITEM;
            break;
        case OBJID_TITLEBAR:
            role = ROLE_SYSTEM_TITLEBAR;
            break;
        case OBJID_MENU:
            role = ROLE_SYSTEM_MENUBAR;
            break;
        case OBJID_CLIENT:
            role = client_role(p->hwnd, window_class(p->hwnd));
            break;
        case OBJID_VSCROLL:
        case OBJID_HSCROLL:
            role = ROLE_SYSTEM_SCROLLBAR;
            break;
        case OBJID_SIZEGRIP:
            role = ROLE_SYSTEM_GRIP;
            break;
        case OBJID_CARET:
            role = ROLE_SYSTEM_CARET;
            break;
        case OBJID_CURSOR:
            role = ROLE_SYSTEM_CURSOR;
            break;
        }
    }
    out->vt = VT_I4;
    out->lVal = role;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE acc_get_state(void *self, VARIANT var, VARIANT *out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    VariantInit(out);
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    IAccessible *target;
    if (delegate(p, id, &target)) {
        hr = vtbl_of(target)->get_accState(target, self_variant(), out);
        ((IUnknown *)target)->lpVtbl->Release((IUnknown *)target);
        return hr;
    }
    CLASS kind;
    KIDS kids = kids_of(p, &kind);
    LONG state = 0;
    if (id) {
        if (id < 0 || id > child_count(p))
            return E_INVALIDARG;
        state = window_state(p->hwnd) & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE);
        if (kids == KIDS_ITEMS) {
            state |= STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
            if (SendMessageW(p->hwnd, LB_GETSEL, (WPARAM)(id - 1), 0) > 0)
                state |= STATE_SYSTEM_SELECTED;
        }
    } else {
        switch (p->object) {
        case OBJID_WINDOW:
            state = window_state(p->hwnd);
            break;
        case OBJID_CLIENT:
            state = client_state(p->hwnd, window_class(p->hwnd));
            break;
        case OBJID_VSCROLL:
        case OBJID_HSCROLL:
        case OBJID_TITLEBAR:
        case OBJID_MENU:
        case OBJID_SYSMENU:
        case OBJID_SIZEGRIP:
            state = window_state(p->hwnd) & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE);
            if (p->object == OBJID_TITLEBAR)
                state |= STATE_SYSTEM_MOVEABLE;
            if (p->object == OBJID_SYSMENU)
                state |= STATE_SYSTEM_HASPOPUP;
            break;
        case OBJID_CARET:
            state = STATE_SYSTEM_NORMAL;
            break;
        case OBJID_CURSOR:
            state = STATE_SYSTEM_NORMAL;
            break;
        }
    }
    out->vt = VT_I4;
    out->lVal = state;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE acc_get_help(void *self, VARIANT var, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(self);
    if (FAILED(hr))
        return hr;
    DELEGATE_STRING((PROXY *)self, id, get_accHelp, out);
    return id < 0 || id > child_count(self) ? E_INVALIDARG : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE acc_get_help_topic(void *self, BSTR *file, VARIANT var, LONG *topic)
{
    if (!file || !topic)
        return E_POINTER;
    *file = nullptr;
    *topic = 0;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT STDMETHODCALLTYPE acc_get_keyboard_shortcut(void *self, VARIANT var, BSTR *out)
{
    PROXY *p = self;
    WCHAR text[512], shortcut[32];
    if (!out)
        return E_POINTER;
    *out = nullptr;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    DELEGATE_STRING(p, id, get_accKeyboardShortcut, out);
    if (id)
        return id < 0 || id > child_count(p) ? E_INVALIDARG : S_FALSE;
    if (p->object == OBJID_SYSMENU) {
        _snwprintf(shortcut, RTL_NUMBER_OF(shortcut), OaString(255), OaString(256));
        return bstr_out(shortcut, out);
    }
    if (p->object != OBJID_CLIENT)
        return S_FALSE;
    CLASS kind = window_class(p->hwnd);
    WCHAR key = 0;
    if (kind == CLS_BUTTON || kind == CLS_STATIC) {
        window_text(p->hwnd, text, RTL_NUMBER_OF(text));
        key = mnemonic(text);
    } else if (kind == CLS_EDIT || kind == CLS_LISTBOX || kind == CLS_COMBOBOX) {
        HWND label = GetWindow(p->hwnd, GW_HWNDPREV);
        if (label && window_class(label) == CLS_STATIC && window_text(label, text, RTL_NUMBER_OF(text)))
            key = mnemonic(text);
    }
    if (!key)
        return S_FALSE;
    WCHAR upper[2] = {(WCHAR)((key >= 'a' && key <= 'z') ? key - 32 : key), 0};
    _snwprintf(shortcut, RTL_NUMBER_OF(shortcut), OaString(255), upper);
    return bstr_out(shortcut, out);
}

/* the focused object under a proxy: itself, a child window, or no one. */
static HRESULT STDMETHODCALLTYPE acc_get_focus(void *self, VARIANT *out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    VariantInit(out);
    HRESULT hr = live(p);
    if (FAILED(hr))
        return hr;
    HWND focus = GetFocus();
    if (!focus)
        return S_OK;
    if (p->object == OBJID_WINDOW || p->object == OBJID_CLIENT) {
        if (focus == p->hwnd) {
            out->vt = VT_I4;
            out->lVal = CHILDID_SELF;
            return S_OK;
        }
        if (IsChild(p->hwnd, focus)) {
            out->vt = VT_DISPATCH;
            return window_object(focus, &out->pdispVal);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE acc_get_selection(void *self, VARIANT *out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    VariantInit(out);
    HRESULT hr = live(p);
    if (FAILED(hr))
        return hr;
    CLASS kind;
    if (kids_of(p, &kind) == KIDS_ITEMS) {
        LRESULT current = SendMessageW(p->hwnd, LB_GETCURSEL, 0, 0);
        if (current >= 0) {
            out->vt = VT_I4;
            out->lVal = (LONG)current + 1;
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE acc_get_default_action(void *self, VARIANT var, BSTR *out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    DELEGATE_STRING(p, id, get_accDefaultAction, out);
    if (id) {
        if (id < 0 || id > child_count(p))
            return E_INVALIDARG;
        if (p->object == OBJID_TITLEBAR || p->object == OBJID_VSCROLL || p->object == OBJID_HSCROLL)
            return bstr_out(OaString(308), out);
        return S_FALSE;
    }
    if (p->object == OBJID_SYSMENU)
        return bstr_out(OaString(285), out);
    if (p->object != OBJID_CLIENT)
        return S_FALSE;
    CLASS kind = window_class(p->hwnd);
    LONG role = client_role(p->hwnd, kind);
    switch (role) {
    case ROLE_SYSTEM_PUSHBUTTON:
    case ROLE_SYSTEM_SPLITBUTTON:
        return bstr_out(OaString(308), out);
    case ROLE_SYSTEM_CHECKBUTTON:
        return bstr_out(OaString(SendMessageW(p->hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED ? 310 : 309), out);
    case ROLE_SYSTEM_RADIOBUTTON:
        return bstr_out(OaString(309), out);
    case ROLE_SYSTEM_LINK:
        return bstr_out(OaString(307), out);
    case ROLE_SYSTEM_COMBOBOX:
        return bstr_out(OaString(SendMessageW(p->hwnd, CB_GETDROPPEDSTATE, 0, 0) ? 286 : 285), out);
    }
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE acc_select(void *self, LONG flags, VARIANT var)
{
    PROXY *p = self;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    if (flags & ~0x1F || !flags || ((flags & SELFLAG_TAKESELECTION) && (flags & (SELFLAG_ADDSELECTION | SELFLAG_REMOVESELECTION))))
        return E_INVALIDARG;
    IAccessible *target;
    if (delegate(p, id, &target)) {
        hr = vtbl_of(target)->accSelect(target, flags, self_variant());
        ((IUnknown *)target)->lpVtbl->Release((IUnknown *)target);
        return hr;
    }
    CLASS kind;
    if (id) {
        if (id < 0 || id > child_count(p))
            return E_INVALIDARG;
        if (kids_of(p, &kind) == KIDS_ITEMS && (flags & SELFLAG_TAKESELECTION)) {
            SendMessageW(p->hwnd, LB_SETCURSEL, (WPARAM)(id - 1), 0);
            return S_OK;
        }
        return S_FALSE;
    }
    if (flags & SELFLAG_TAKEFOCUS) {
        SetFocus(p->hwnd);
        return S_OK;
    }
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE acc_location(void *self, LONG *left, LONG *top, LONG *width, LONG *height, VARIANT var)
{
    PROXY *p = self;
    if (!left || !top || !width || !height)
        return E_POINTER;
    *left = *top = *width = *height = 0;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    IAccessible *target;
    if (delegate(p, id, &target)) {
        hr = vtbl_of(target)->accLocation(target, left, top, width, height, self_variant());
        ((IUnknown *)target)->lpVtbl->Release((IUnknown *)target);
        return hr;
    }
    RECT r;
    CLASS kind;
    if (id) {
        if (id < 0 || id > child_count(p))
            return E_INVALIDARG;
        KIDS kids = kids_of(p, &kind);
        if (kids == KIDS_ITEMS) {
            RECT item, client;
            if (SendMessageW(p->hwnd, LB_GETITEMRECT, (WPARAM)(id - 1), (LPARAM)&item) < 0 || !client_screen_rect(p->hwnd, &client))
                return S_FALSE;
            OffsetRect(&item, client.left, client.top);
            set_rect(&item, left, top, width, height);
            return S_OK;
        }
        if (kids == KIDS_BUTTONS) {
            RECT bar;
            LONG ids[5];
            LONG count = titlebar_buttons(p->hwnd, ids);
            if (!part_rect(p->hwnd, OBJID_TITLEBAR, &bar))
                return S_FALSE;
            int size = GetSystemMetrics(SM_CXSIZE);
            r = bar;
            r.right = bar.right - (count - id) * size;
            r.left = r.right - size;
            set_rect(&r, left, top, width, height);
            return S_OK;
        }
        if (kids == KIDS_SCROLL) {
            if (!part_rect(p->hwnd, p->object, &r))
                return S_FALSE;
            set_rect(&r, left, top, width, height);
            return S_OK;
        }
        return S_FALSE;
    }
    switch (p->object) {
    case OBJID_WINDOW:
        if (!GetWindowRect(p->hwnd, &r))
            return S_FALSE;
        break;
    case OBJID_CLIENT:
        if (!client_screen_rect(p->hwnd, &r))
            return S_FALSE;
        break;
    case OBJID_CARET: {
        GUITHREADINFO info = {sizeof(info)};
        if (!GetGUIThreadInfo(0, &info) || !info.hwndCaret)
            return S_FALSE;
        POINT corner = {info.rcCaret.left, info.rcCaret.top};
        ClientToScreen(info.hwndCaret, &corner);
        r.left = corner.x;
        r.top = corner.y;
        r.right = corner.x + (info.rcCaret.right - info.rcCaret.left);
        r.bottom = corner.y + (info.rcCaret.bottom - info.rcCaret.top);
        break;
    }
    case OBJID_CURSOR: {
        POINT at;
        if (!GetCursorPos(&at))
            return S_FALSE;
        r.left = at.x;
        r.top = at.y;
        r.right = at.x + GetSystemMetrics(SM_CXCURSOR);
        r.bottom = at.y + GetSystemMetrics(SM_CYCURSOR);
        break;
    }
    default:
        if (!part_rect(p->hwnd, p->object, &r))
            return S_FALSE;
        break;
    }
    set_rect(&r, left, top, width, height);
    return S_OK;
}

static HRESULT dispatch_variant(HWND hwnd, VARIANT *out)
{
    out->vt = VT_DISPATCH;
    HRESULT hr = window_object(hwnd, &out->pdispVal);
    if (FAILED(hr) || !out->pdispVal) {
        VariantInit(out);
        return FAILED(hr) ? hr : S_FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE acc_navigate(void *self, LONG direction, VARIANT var, VARIANT *out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    VariantInit(out);
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    if (direction < NAVDIR_UP || direction > NAVDIR_LASTCHILD)
        return E_INVALIDARG;
    CLASS kind;
    KIDS kids = kids_of(p, &kind);
    LONG count = child_count(p);
    if (id < 0 || id > count)
        return E_INVALIDARG;
    if (direction == NAVDIR_FIRSTCHILD || direction == NAVDIR_LASTCHILD) {
        if (id || !count)
            return S_FALSE;
        LONG which = direction == NAVDIR_FIRSTCHILD ? 1 : count;
        IUnknown *unknown;
        if (part_of(p, which, &unknown) || child_proxy(p, which, &unknown)) {
            out->vt = VT_DISPATCH;
            hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IDispatch_, (void **)&out->pdispVal);
            unknown->lpVtbl->Release(unknown);
            return hr;
        }
        out->vt = VT_I4;
        out->lVal = which;
        return S_OK;
    }
    if (direction == NAVDIR_NEXT || direction == NAVDIR_PREVIOUS) {
        if (id) {
            /* among the numbered children of a proxy. */
            LONG to = id + (direction == NAVDIR_NEXT ? 1 : -1);
            if (to < 1 || to > count)
                return S_FALSE;
            IUnknown *unknown;
            if (part_of(p, to, &unknown) || child_proxy(p, to, &unknown)) {
                out->vt = VT_DISPATCH;
                hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IDispatch_, (void **)&out->pdispVal);
                unknown->lpVtbl->Release(unknown);
                return hr;
            }
            out->vt = VT_I4;
            out->lVal = to;
            return S_OK;
        }
        /* a window among its siblings, its parts among the parts. */
        if (p->object == OBJID_WINDOW) {
            HWND sibling = GetWindow(p->hwnd, direction == NAVDIR_NEXT ? GW_HWNDNEXT : GW_HWNDPREV);
            return sibling ? dispatch_variant(sibling, out) : S_FALSE;
        }
        (void)kids;
        return S_FALSE;
    }
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE acc_hit_test(void *self, LONG x, LONG y, VARIANT *out)
{
    PROXY *p = self;
    if (!out)
        return E_POINTER;
    VariantInit(out);
    HRESULT hr = live(p);
    if (FAILED(hr))
        return hr;
    RECT r;
    POINT at = {x, y};
    BOOL inside = p->object == OBJID_CLIENT ? client_screen_rect(p->hwnd, &r) : GetWindowRect(p->hwnd, &r);
    if (!inside || !PtInRect(&r, at))
        return S_FALSE;
    CLASS kind;
    KIDS kids = kids_of(p, &kind);
    if (kids == KIDS_PARTS) {
        LONG parts[6];
        LONG n = window_parts(p->hwnd, parts);
        for (LONG i = 0; i < n; i++) {
            RECT part;
            BOOL have = parts[i] == OBJID_CLIENT ? client_screen_rect(p->hwnd, &part) : part_rect(p->hwnd, parts[i], &part);
            if (have && PtInRect(&part, at)) {
                void *object;
                if (SUCCEEDED(CreateStdAccessibleObject(p->hwnd, parts[i], &IID_IDispatch_, &object))) {
                    out->vt = VT_DISPATCH;
                    out->pdispVal = object;
                    return S_OK;
                }
            }
        }
        out->vt = VT_I4;
        out->lVal = CHILDID_SELF;
        return S_OK;
    }
    if (kids == KIDS_WINDOWS) {
        POINT client = at;
        ScreenToClient(p->hwnd, &client);
        HWND child = ChildWindowFromPointEx(p->hwnd, client, CWP_SKIPINVISIBLE);
        if (child && child != p->hwnd)
            return dispatch_variant(child, out);
    }
    if (kids == KIDS_ITEMS) {
        POINT client = at;
        ScreenToClient(p->hwnd, &client);
        LRESULT item = SendMessageW(p->hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(client.x, client.y));
        if (HIWORD(item) == 0 && (LONG)LOWORD(item) < child_count(p)) {
            out->vt = VT_I4;
            out->lVal = (LONG)LOWORD(item) + 1;
            return S_OK;
        }
    }
    out->vt = VT_I4;
    out->lVal = CHILDID_SELF;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE acc_do_default_action(void *self, VARIANT var)
{
    PROXY *p = self;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    IAccessible *target;
    if (delegate(p, id, &target)) {
        hr = vtbl_of(target)->accDoDefaultAction(target, self_variant());
        ((IUnknown *)target)->lpVtbl->Release((IUnknown *)target);
        return hr;
    }
    if (id) {
        if (id < 0 || id > child_count(p))
            return E_INVALIDARG;
        if (p->object == OBJID_TITLEBAR) {
            LONG ids[5];
            if (id <= titlebar_buttons(p->hwnd, ids)) {
                static const WORD commands[] = {0, 0, SC_MINIMIZE, SC_MAXIMIZE, SC_CONTEXTHELP, SC_CLOSE};
                WORD command = ids[id - 1] == 3 && IsZoomed(p->hwnd) ? SC_RESTORE : commands[ids[id - 1]];
                PostMessageW(p->hwnd, WM_SYSCOMMAND, command, 0);
                return S_OK;
            }
        }
        return S_FALSE;
    }
    if (p->object == OBJID_CLIENT) {
        CLASS kind = window_class(p->hwnd);
        LONG role = client_role(p->hwnd, kind);
        if (role == ROLE_SYSTEM_PUSHBUTTON || role == ROLE_SYSTEM_CHECKBUTTON || role == ROLE_SYSTEM_RADIOBUTTON ||
            role == ROLE_SYSTEM_SPLITBUTTON) {
            PostMessageW(p->hwnd, BM_CLICK, 0, 0);
            return S_OK;
        }
    }
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE acc_put_name(void *self, VARIANT var, BSTR name)
{
    LONG id;
    HRESULT hr = child_id(var, &id);
    return FAILED(hr) ? hr : DISP_E_MEMBERNOTFOUND;
}

static HRESULT STDMETHODCALLTYPE acc_put_value(void *self, VARIANT var, BSTR value)
{
    PROXY *p = self;
    LONG id;
    HRESULT hr = child_id(var, &id);
    if (FAILED(hr))
        return hr;
    hr = live(p);
    if (FAILED(hr))
        return hr;
    if (id == 0 && p->object == OBJID_CLIENT) {
        CLASS kind = window_class(p->hwnd);
        if (kind == CLS_EDIT || kind == CLS_COMBOBOX || kind == CLS_RICHEDIT) {
            if ((GetWindowLongW(p->hwnd, GWL_STYLE) & ES_READONLY) && kind == CLS_EDIT)
                return E_ACCESSDENIED;
            SendMessageW(p->hwnd, WM_SETTEXT, 0, (LPARAM)(value ? value : L""));
            return S_OK;
        }
    }
    return DISP_E_MEMBERNOTFOUND;
}

static const IAccessibleVtbl accessible_vtbl = {
    acc_query_interface, acc_add_ref, acc_release, acc_get_type_info_count, acc_get_type_info, acc_get_ids_of_names,
    acc_invoke, acc_get_parent, acc_get_child_count, acc_get_child, acc_get_name, acc_get_value, acc_get_description,
    acc_get_role, acc_get_state, acc_get_help, acc_get_help_topic, acc_get_keyboard_shortcut, acc_get_focus,
    acc_get_selection, acc_get_default_action, acc_select, acc_location, acc_navigate, acc_hit_test,
    acc_do_default_action, acc_put_name, acc_put_value,
};

/* ------------------------------------------------------------------ IOleWindow */

static HRESULT STDMETHODCALLTYPE win_query_interface(void *self, REFIID riid, void **out)
{
    return acc_query_interface(from_window(self), riid, out);
}

static ULONG STDMETHODCALLTYPE win_add_ref(void *self)
{
    return acc_add_ref(from_window(self));
}

static ULONG STDMETHODCALLTYPE win_release(void *self)
{
    return acc_release(from_window(self));
}

static HRESULT STDMETHODCALLTYPE win_get_window(void *self, HWND *hwnd)
{
    PROXY *p = from_window(self);
    if (!hwnd)
        return E_POINTER;
    *hwnd = p->hwnd;
    return IsWindow(p->hwnd) || !p->hwnd ? S_OK : E_DISCONNECTED;
}

static HRESULT STDMETHODCALLTYPE win_context_help(void *self, BOOL enter)
{
    return E_NOTIMPL;
}

static const IOleWindowVtbl window_vtbl = {win_query_interface, win_add_ref, win_release, win_get_window,
                                           win_context_help};

/* ------------------------------------------------------------------ IEnumVARIANT */

static HRESULT STDMETHODCALLTYPE enum_query_interface(void *self, REFIID riid, void **out)
{
    return acc_query_interface(from_enumerator(self), riid, out);
}

static ULONG STDMETHODCALLTYPE enum_add_ref(void *self)
{
    return acc_add_ref(from_enumerator(self));
}

static ULONG STDMETHODCALLTYPE enum_release(void *self)
{
    return acc_release(from_enumerator(self));
}

/* the children as numbers: a client of the caller's resolves them into objects when it wants them. */
static HRESULT STDMETHODCALLTYPE enum_next(void *self, ULONG count, VARIANT *items, ULONG *fetched)
{
    PROXY *p = from_enumerator(self);
    if (!items || (!fetched && count != 1))
        return E_POINTER;
    LONG total = child_count(p);
    ULONG got = 0;
    for (; got < count && p->next <= total; got++, p->next++) {
        VariantInit(&items[got]);
        items[got].vt = VT_I4;
        items[got].lVal = p->next;
    }
    if (fetched)
        *fetched = got;
    return got == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(void *self, ULONG count)
{
    PROXY *p = from_enumerator(self);
    LONG total = child_count(p);
    p->next += (LONG)count;
    if (p->next > total + 1) {
        p->next = total + 1;
        return S_FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_reset(void *self)
{
    from_enumerator(self)->next = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(void *self, void **out)
{
    PROXY *p = from_enumerator(self);
    if (!out)
        return E_POINTER;
    PROXY *copy = nullptr;
    HRESULT hr = OaCreateProxy(p->hwnd, p->object, &IID_IEnumVARIANT_, (void **)&copy);
    if (SUCCEEDED(hr)) {
        copy = from_enumerator(copy);
        copy->next = p->next;
        *out = &copy->enumerator;
    }
    return hr;
}

static const OaEnumeratorVtbl enumerator_vtbl = {enum_query_interface, enum_add_ref, enum_release, enum_next,
                                                 enum_skip,            enum_reset,   enum_clone};

/* ------------------------------------------------------------------ making one */

static BOOL object_known(LONG object)
{
    switch (object) {
    case OBJID_WINDOW:
    case OBJID_SYSMENU:
    case OBJID_TITLEBAR:
    case OBJID_MENU:
    case OBJID_CLIENT:
    case OBJID_VSCROLL:
    case OBJID_HSCROLL:
    case OBJID_SIZEGRIP:
    case OBJID_CARET:
    case OBJID_CURSOR:
        return TRUE;
    }
    return FALSE;
}

HRESULT OaCreateProxy(HWND hwnd, LONG object, REFIID riid, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    /* the cursor needs no window, the others do. */
    if (!hwnd && object != OBJID_CURSOR)
        return E_FAIL;
    if (!object_known(object))
        return E_INVALIDARG;
    if (hwnd && !IsWindow(hwnd))
        return E_FAIL;
    PROXY *p = LocalAlloc(LPTR, sizeof(*p));
    if (!p)
        return E_OUTOFMEMORY;
    p->accessible = &accessible_vtbl;
    p->window = &window_vtbl;
    p->enumerator = &enumerator_vtbl;
    p->refs = 1;
    p->hwnd = hwnd;
    p->object = object;
    p->next = 1;
    HRESULT hr = acc_query_interface(p, riid, out);
    acc_release(p);
    return hr;
}
