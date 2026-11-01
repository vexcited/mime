/*
 * proxies: the elements of windows that do not provide their own, and the entry points that find the element for a
 * window, a point or the focus. a window is described by its class, style and messages, and the parts of it that have no
 * window by the accessible objects of oleacc.
 */
#include "uia.h"

extern UiaProviderCallback *g_provider_callback;

enum { PK_DESKTOP = 1, PK_WINDOW, PK_ACC };

typedef struct PROXY {
    NODE base;
    int kind;
    HWND hwnd;
    IAccessible *acc;
    LONG object;
    LONG child;
} PROXY;

#define PX(n) ((PROXY *)(n))

#define TBS_VERT 0x0002
#define PBS_VERTICAL 0x0004

static const NODE_OPS g_proxy_ops;
static const PATTERN_OPS g_native_pattern_ops;

typedef struct NATPAT {
    PATTERN base;
    NODE *node;
} NATPAT;

static BOOL is_proxy(NODE *n)
{
    return n->ops == &g_proxy_ops;
}

static HRESULT proxy_make(int kind, HWND hwnd, IAccessible *acc, LONG object, LONG child, NODE **out)
{
    PROXY *p = NodeAlloc(sizeof(*p), &g_proxy_ops);
    if (!p)
        return E_OUTOFMEMORY;
    p->kind = kind;
    p->hwnd = hwnd;
    p->acc = acc;
    if (acc)
        acc->lpVtbl->AddRef(acc);
    p->object = object;
    p->child = child;
    *out = &p->base;
    return S_OK;
}

static void proxy_destroy(NODE *n)
{
    if (PX(n)->acc)
        PX(n)->acc->lpVtbl->Release(PX(n)->acc);
}

HRESULT ProxyDesktopNode(NODE **out)
{
    return proxy_make(PK_DESKTOP, GetDesktopWindow(), nullptr, 0, 0, out);
}

HRESULT ProxyNodeFromWindow(HWND hwnd, NODE **out)
{
    *out = nullptr;
    if (hwnd == GetDesktopWindow())
        return ProxyDesktopNode(out);
    return proxy_make(PK_WINDOW, hwnd, nullptr, 0, 0, out);
}

/* ------------------------------------------------------------------ windows and their accessible objects */

static BOOL is_top_level(HWND hwnd)
{
    return !(GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD);
}

static IAccessible *window_accessible(HWND hwnd, LONG object)
{
    IAccessible *acc = nullptr;
    if (FAILED(AccessibleObjectFromWindow(hwnd, object, &IID_IAccessible, (void **)&acc)))
        return nullptr;
    return acc;
}

/* the accessible object that speaks for a node: the client of a control, the window of a top level window. */
static IAccessible *speaking_accessible(PROXY *p, VARIANT *child)
{
    VariantInit(child);
    child->vt = VT_I4;
    child->lVal = 0;
    if (p->kind == PK_ACC) {
        child->lVal = p->child;
        p->acc->lpVtbl->AddRef(p->acc);
        return p->acc;
    }
    if (p->kind != PK_WINDOW)
        return nullptr;
    return window_accessible(p->hwnd, is_top_level(p->hwnd) ? OBJID_WINDOW : OBJID_CLIENT);
}

static BSTR accessible_text(IAccessible *acc, VARIANT child, int which)
{
    BSTR text = nullptr;
    switch (which) {
    case 0: acc->lpVtbl->get_accName(acc, child, &text); break;
    case 1: acc->lpVtbl->get_accValue(acc, child, &text); break;
    case 2: acc->lpVtbl->get_accDescription(acc, child, &text); break;
    case 3: acc->lpVtbl->get_accHelp(acc, child, &text); break;
    case 4: acc->lpVtbl->get_accKeyboardShortcut(acc, child, &text); break;
    case 5: acc->lpVtbl->get_accDefaultAction(acc, child, &text); break;
    }
    return text;
}

static DWORD accessible_state(IAccessible *acc, VARIANT child)
{
    VARIANT v;
    VariantInit(&v);
    DWORD state = 0;
    if (SUCCEEDED(acc->lpVtbl->get_accState(acc, child, &v)) && v.vt == VT_I4)
        state = (DWORD)v.lVal;
    VariantClear(&v);
    return state;
}

static DWORD accessible_role(IAccessible *acc, VARIANT child)
{
    VARIANT v;
    VariantInit(&v);
    DWORD role = 0;
    if (SUCCEEDED(acc->lpVtbl->get_accRole(acc, child, &v)) && v.vt == VT_I4)
        role = (DWORD)v.lVal;
    VariantClear(&v);
    return role;
}

#define STATE_UNAVAILABLE 0x1
#define STATE_SELECTED 0x2
#define STATE_FOCUSED 0x4
#define STATE_PRESSED 0x8
#define STATE_CHECKED 0x10
#define STATE_MIXED 0x20
#define STATE_READONLY 0x40
#define STATE_EXPANDED 0x200
#define STATE_COLLAPSED 0x400
#define STATE_INVISIBLE 0x8000
#define STATE_OFFSCREEN 0x10000
#define STATE_FOCUSABLE 0x100000
#define STATE_SELECTABLE 0x200000
#define STATE_PROTECTED 0x20000000

static int control_type_of_role(DWORD role)
{
    switch (role) {
    case 0x1: return UIA_TitleBarControlTypeId;
    case 0x2: return UIA_MenuBarControlTypeId;
    case 0x3: return UIA_ScrollBarControlTypeId;
    case 0x4: return UIA_ThumbControlTypeId;
    case 0x9: return UIA_WindowControlTypeId;
    case 0xa: return UIA_PaneControlTypeId;
    case 0xb: return UIA_MenuControlTypeId;
    case 0xc: return UIA_MenuItemControlTypeId;
    case 0xd: return UIA_ToolTipControlTypeId;
    case 0xe: return UIA_PaneControlTypeId;
    case 0xf: return UIA_DocumentControlTypeId;
    case 0x10: return UIA_PaneControlTypeId;
    case 0x14: return UIA_GroupControlTypeId;
    case 0x15: return UIA_SeparatorControlTypeId;
    case 0x16: return UIA_ToolBarControlTypeId;
    case 0x17: return UIA_StatusBarControlTypeId;
    case 0x18: return UIA_TableControlTypeId;
    case 0x19: return UIA_HeaderItemControlTypeId;
    case 0x1a: return UIA_HeaderItemControlTypeId;
    case 0x1b: return UIA_HeaderItemControlTypeId;
    case 0x1c: return UIA_DataItemControlTypeId;
    case 0x1d: return UIA_DataItemControlTypeId;
    case 0x1e: return UIA_HyperlinkControlTypeId;
    case 0x21: return UIA_ListControlTypeId;
    case 0x22: return UIA_ListItemControlTypeId;
    case 0x23: return UIA_TreeControlTypeId;
    case 0x24: return UIA_TreeItemControlTypeId;
    case 0x25: return UIA_TabItemControlTypeId;
    case 0x28: return UIA_ImageControlTypeId;
    case 0x29: return UIA_TextControlTypeId;
    case 0x2a: return UIA_EditControlTypeId;
    case 0x2b: return UIA_ButtonControlTypeId;
    case 0x2c: return UIA_CheckBoxControlTypeId;
    case 0x2d: return UIA_RadioButtonControlTypeId;
    case 0x2e: return UIA_ComboBoxControlTypeId;
    case 0x30: return UIA_ProgressBarControlTypeId;
    case 0x32: return UIA_EditControlTypeId;
    case 0x33: return UIA_SliderControlTypeId;
    case 0x34: return UIA_SpinnerControlTypeId;
    case 0x38: return UIA_SplitButtonControlTypeId;
    case 0x39: return UIA_SplitButtonControlTypeId;
    case 0x3a: return UIA_SplitButtonControlTypeId;
    case 0x3e: return UIA_SplitButtonControlTypeId;
    case 0x3c: return UIA_TabControlTypeId;
    case 0x3d: return UIA_CustomControlTypeId;
    }
    return UIA_CustomControlTypeId;
}

/* ------------------------------------------------------------------ what a window is */

static int window_control_type(HWND hwnd, DWORD *role_out)
{
    WCHAR cls[64];
    GetClassNameW(hwnd, cls, RTL_NUMBER_OF(cls));
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (is_top_level(hwnd))
        return UIA_WindowControlTypeId;
    if (!_wcsicmp(cls, L"Button")) {
        switch (style & BS_TYPEMASK) {
        case BS_CHECKBOX:
        case BS_AUTOCHECKBOX:
        case BS_3STATE:
        case BS_AUTO3STATE: return UIA_CheckBoxControlTypeId;
        case BS_RADIOBUTTON:
        case BS_AUTORADIOBUTTON: return UIA_RadioButtonControlTypeId;
        case BS_GROUPBOX: return UIA_GroupControlTypeId;
        }
        return UIA_ButtonControlTypeId;
    }
    if (!_wcsicmp(cls, L"Edit"))
        return UIA_EditControlTypeId;
    if (!_wcsicmp(cls, L"Static")) {
        int kind = style & SS_TYPEMASK;
        return kind == SS_ICON || kind == SS_BITMAP || kind == SS_ENHMETAFILE ? UIA_ImageControlTypeId
                                                                              : UIA_TextControlTypeId;
    }
    if (!_wcsicmp(cls, L"ListBox") || !_wcsicmp(cls, L"ComboLBox") || !_wcsicmp(cls, L"SysListView32"))
        return UIA_ListControlTypeId;
    if (!_wcsicmp(cls, L"ComboBox") || !_wcsicmp(cls, L"ComboBoxEx32"))
        return UIA_ComboBoxControlTypeId;
    if (!_wcsicmp(cls, L"ScrollBar"))
        return UIA_ScrollBarControlTypeId;
    if (!_wcsicmp(cls, L"msctls_trackbar32"))
        return UIA_SliderControlTypeId;
    if (!_wcsicmp(cls, L"msctls_progress32"))
        return UIA_ProgressBarControlTypeId;
    if (!_wcsicmp(cls, L"msctls_updown32"))
        return UIA_SpinnerControlTypeId;
    if (!_wcsicmp(cls, L"SysTabControl32"))
        return UIA_TabControlTypeId;
    if (!_wcsicmp(cls, L"SysTreeView32"))
        return UIA_TreeControlTypeId;
    if (!_wcsicmp(cls, L"ToolbarWindow32"))
        return UIA_ToolBarControlTypeId;
    if (!_wcsicmp(cls, L"msctls_statusbar32"))
        return UIA_StatusBarControlTypeId;
    if (!_wcsicmp(cls, L"tooltips_class32"))
        return UIA_ToolTipControlTypeId;
    if (!_wcsicmp(cls, L"SysLink"))
        return UIA_HyperlinkControlTypeId;
    if (!_wcsicmp(cls, L"SysHeader32"))
        return UIA_HeaderControlTypeId;
    if (!_wcsicmp(cls, L"msctls_hotkey32") || !_wcsicmp(cls, L"SysIPAddress32"))
        return UIA_EditControlTypeId;
    if (!_wcsnicmp(cls, L"RichEdit", 8) || !_wcsnicmp(cls, L"RICHEDIT", 8))
        return UIA_DocumentControlTypeId;
    if (!_wcsicmp(cls, L"SysMonthCal32"))
        return UIA_CalendarControlTypeId;
    return UIA_PaneControlTypeId;
}

static void put_string(VARIANT *out, const WCHAR *text)
{
    out->bstrVal = SysAllocString(text ? text : L"");
    if (out->bstrVal)
        out->vt = VT_BSTR;
}

static void put_int(VARIANT *out, int v)
{
    out->vt = VT_I4;
    out->lVal = v;
}

static void put_bool(VARIANT *out, BOOL v)
{
    out->vt = VT_BOOL;
    out->boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
}

static void put_rect(VARIANT *out, const RECT *r)
{
    double d[4] = {r->left, r->top, r->right - r->left, r->bottom - r->top};
    if (SUCCEEDED(UiaDoubleArrayToSafeArray(d, 4, &out->parray)))
        out->vt = VT_ARRAY | VT_R8;
}

static BOOL window_enabled(HWND hwnd)
{
    for (HWND h = hwnd; h && h != GetDesktopWindow(); h = GetAncestor(h, GA_PARENT))
        if (!IsWindowEnabled(h))
            return FALSE;
    return TRUE;
}

static BOOL window_offscreen(HWND hwnd)
{
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;
    RECT r, screen = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)}, both;
    GetWindowRect(hwnd, &r);
    return !IntersectRect(&both, &r, &screen);
}

static BOOL window_focused(HWND hwnd)
{
    GUITHREADINFO info = {sizeof(info)};
    DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
    return GetGUIThreadInfo(thread, &info) && info.hwndFocus == hwnd;
}

static HRESULT window_prop(PROXY *p, PROPERTYID id, VARIANT *out)
{
    HWND hwnd = p->hwnd;
    if (!IsWindow(hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    VARIANT child;
    IAccessible *acc = nullptr;
    WCHAR text[256];
    switch (id) {
    case UIA_NativeWindowHandlePropertyId:
        put_int(out, (int)(LONG_PTR)hwnd);
        break;
    case UIA_ProcessIdPropertyId: {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        put_int(out, (int)pid);
        break;
    }
    case UIA_ClassNamePropertyId:
        GetClassNameW(hwnd, text, RTL_NUMBER_OF(text));
        put_string(out, text);
        break;
    case UIA_FrameworkIdPropertyId:
        put_string(out, L"Win32");
        break;
    case UIA_ProviderDescriptionPropertyId:
        put_string(out, L"Microsoft: MSAA Proxy (unmanaged:uiautomationcore.dll)");
        break;
    case UIA_ControlTypePropertyId:
        put_int(out, window_control_type(hwnd, nullptr));
        break;
    case UIA_NamePropertyId:
        acc = speaking_accessible(p, &child);
        if (acc) {
            BSTR name = accessible_text(acc, child, 0);
            if (name) {
                out->vt = VT_BSTR;
                out->bstrVal = name;
            }
            acc->lpVtbl->Release(acc);
        }
        if (out->vt == VT_EMPTY && is_top_level(hwnd)) {
            GetWindowTextW(hwnd, text, RTL_NUMBER_OF(text));
            put_string(out, text);
        }
        break;
    case UIA_IsEnabledPropertyId:
        put_bool(out, window_enabled(hwnd));
        break;
    case UIA_IsOffscreenPropertyId:
        put_bool(out, window_offscreen(hwnd));
        break;
    case UIA_HasKeyboardFocusPropertyId:
        put_bool(out, window_focused(hwnd));
        break;
    case UIA_IsKeyboardFocusablePropertyId: {
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        put_bool(out, (style & WS_TABSTOP) || is_top_level(hwnd));
        break;
    }
    case UIA_BoundingRectanglePropertyId: {
        RECT r;
        GetWindowRect(hwnd, &r);
        put_rect(out, &r);
        break;
    }
    case UIA_AutomationIdPropertyId:
        if (!is_top_level(hwnd)) {
            int ctrl = GetDlgCtrlID(hwnd);
            if (ctrl) {
                _snwprintf(text, RTL_NUMBER_OF(text), L"%d", ctrl);
                put_string(out, text);
            }
        }
        break;
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        put_bool(out, TRUE);
        break;
    case UIA_IsPasswordPropertyId: {
        WCHAR cls[32];
        GetClassNameW(hwnd, cls, RTL_NUMBER_OF(cls));
        put_bool(out, !_wcsicmp(cls, L"Edit") && (GetWindowLongW(hwnd, GWL_STYLE) & ES_PASSWORD));
        break;
    }
    case UIA_HelpTextPropertyId:
    case UIA_AcceleratorKeyPropertyId:
    case UIA_AccessKeyPropertyId: {
        acc = speaking_accessible(p, &child);
        if (acc) {
            BSTR t = accessible_text(acc, child, id == UIA_HelpTextPropertyId ? 3 : 4);
            if (t) {
                out->vt = VT_BSTR;
                out->bstrVal = t;
            }
            acc->lpVtbl->Release(acc);
        }
        break;
    }
    case UIA_OrientationPropertyId: {
        WCHAR cls[32];
        GetClassNameW(hwnd, cls, RTL_NUMBER_OF(cls));
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        if (!_wcsicmp(cls, L"ScrollBar"))
            put_int(out, style & SBS_VERT ? OrientationType_Vertical : OrientationType_Horizontal);
        else if (!_wcsicmp(cls, L"msctls_trackbar32"))
            put_int(out, style & TBS_VERT ? OrientationType_Vertical : OrientationType_Horizontal);
        else if (!_wcsicmp(cls, L"msctls_progress32"))
            put_int(out, style & PBS_VERTICAL ? OrientationType_Vertical : OrientationType_Horizontal);
        break;
    }
    }
    return S_OK;
}

static HRESULT acc_prop(PROXY *p, PROPERTYID id, VARIANT *out)
{
    VARIANT child;
    VariantInit(&child);
    child.vt = VT_I4;
    child.lVal = p->child;
    IAccessible *acc = p->acc;
    DWORD state = accessible_state(acc, child);
    switch (id) {
    case UIA_NativeWindowHandlePropertyId:
        put_int(out, 0);
        break;
    case UIA_ProcessIdPropertyId: {
        DWORD pid = 0;
        if (p->hwnd)
            GetWindowThreadProcessId(p->hwnd, &pid);
        put_int(out, (int)pid);
        break;
    }
    case UIA_FrameworkIdPropertyId:
        put_string(out, L"Win32");
        break;
    case UIA_ProviderDescriptionPropertyId:
        put_string(out, L"Microsoft: MSAA Proxy (unmanaged:uiautomationcore.dll)");
        break;
    case UIA_ControlTypePropertyId:
        put_int(out, control_type_of_role(accessible_role(acc, child)));
        break;
    case UIA_NamePropertyId: {
        BSTR t = accessible_text(acc, child, 0);
        if (t) {
            out->vt = VT_BSTR;
            out->bstrVal = t;
        }
        break;
    }
    case UIA_HelpTextPropertyId:
    case UIA_AcceleratorKeyPropertyId:
    case UIA_AccessKeyPropertyId: {
        BSTR t = accessible_text(acc, child, id == UIA_HelpTextPropertyId ? 3 : 4);
        if (t) {
            out->vt = VT_BSTR;
            out->bstrVal = t;
        }
        break;
    }
    case UIA_IsEnabledPropertyId:
        put_bool(out, !(state & STATE_UNAVAILABLE));
        break;
    case UIA_IsOffscreenPropertyId:
        put_bool(out, (state & (STATE_OFFSCREEN | STATE_INVISIBLE)) != 0);
        break;
    case UIA_HasKeyboardFocusPropertyId:
        put_bool(out, (state & STATE_FOCUSED) != 0);
        break;
    case UIA_IsKeyboardFocusablePropertyId:
        put_bool(out, (state & STATE_FOCUSABLE) != 0);
        break;
    case UIA_IsPasswordPropertyId:
        put_bool(out, (state & STATE_PROTECTED) != 0);
        break;
    case UIA_BoundingRectanglePropertyId: {
        LONG x = 0, y = 0, w = 0, h = 0;
        if (SUCCEEDED(acc->lpVtbl->accLocation(acc, &x, &y, &w, &h, child))) {
            RECT r = {x, y, x + w, y + h};
            put_rect(out, &r);
        }
        break;
    }
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        put_bool(out, TRUE);
        break;
    }
    return S_OK;
}

static HRESULT desktop_prop(PROPERTYID id, VARIANT *out)
{
    switch (id) {
    case UIA_NamePropertyId:
        put_string(out, L"Desktop");
        break;
    case UIA_ControlTypePropertyId:
        put_int(out, UIA_PaneControlTypeId);
        break;
    case UIA_ClassNamePropertyId:
        put_string(out, L"#32769");
        break;
    case UIA_FrameworkIdPropertyId:
        put_string(out, L"Win32");
        break;
    case UIA_NativeWindowHandlePropertyId:
        put_int(out, (int)(LONG_PTR)GetDesktopWindow());
        break;
    case UIA_IsEnabledPropertyId:
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        put_bool(out, TRUE);
        break;
    case UIA_IsOffscreenPropertyId:
    case UIA_HasKeyboardFocusPropertyId:
    case UIA_IsKeyboardFocusablePropertyId:
        put_bool(out, FALSE);
        break;
    case UIA_BoundingRectanglePropertyId: {
        RECT r = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        put_rect(out, &r);
        break;
    }
    case UIA_ProcessIdPropertyId:
        put_int(out, 0);
        break;
    case UIA_ProviderDescriptionPropertyId:
        put_string(out, L"Microsoft: MSAA Proxy (unmanaged:uiautomationcore.dll)");
        break;
    }
    return S_OK;
}

static HRESULT proxy_get_prop(NODE *n, PROPERTYID id, VARIANT *out)
{
    VariantInit(out);
    PROXY *p = PX(n);
    if (p->kind == PK_DESKTOP)
        return desktop_prop(id, out);
    if (p->kind == PK_WINDOW)
        return window_prop(p, id, out);
    return acc_prop(p, id, out);
}

/* ------------------------------------------------------------------ patterns of proxies */

static HRESULT native_pattern(NODE *n, PATTERNID id, PATTERN **out)
{
    NATPAT *np = PatternAlloc(sizeof(*np), &g_native_pattern_ops, id);
    if (!np)
        return E_OUTOFMEMORY;
    NodeAddRef(n);
    np->node = n;
    *out = &np->base;
    return S_OK;
}

static BOOL has_class(HWND hwnd, const WCHAR *cls)
{
    WCHAR name[64];
    GetClassNameW(hwnd, name, RTL_NUMBER_OF(name));
    return !_wcsicmp(name, cls);
}

static HRESULT proxy_get_pattern(NODE *n, PATTERNID id, PATTERN **out)
{
    *out = nullptr;
    PROXY *p = PX(n);
    if (p->kind == PK_DESKTOP)
        return S_OK;
    BOOL yes = FALSE;
    if (id == UIA_LegacyIAccessiblePatternId) {
        yes = TRUE;
    } else if (p->kind == PK_WINDOW) {
        HWND hwnd = p->hwnd;
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        int type = window_control_type(hwnd, nullptr);
        switch (id) {
        case UIA_WindowPatternId:
        case UIA_TransformPatternId:
            yes = is_top_level(hwnd);
            break;
        case UIA_InvokePatternId:
            yes = type == UIA_ButtonControlTypeId && has_class(hwnd, L"Button");
            break;
        case UIA_TogglePatternId:
            yes = type == UIA_CheckBoxControlTypeId;
            break;
        case UIA_SelectionItemPatternId:
            yes = type == UIA_RadioButtonControlTypeId;
            break;
        case UIA_ValuePatternId:
            yes = type == UIA_EditControlTypeId || (type == UIA_ComboBoxControlTypeId);
            break;
        case UIA_RangeValuePatternId:
            yes = type == UIA_ScrollBarControlTypeId || type == UIA_SliderControlTypeId ||
                  type == UIA_ProgressBarControlTypeId;
            break;
        case UIA_ExpandCollapsePatternId:
            yes = type == UIA_ComboBoxControlTypeId;
            break;
        }
        (void)style;
    } else {
        VARIANT child;
        VariantInit(&child);
        child.vt = VT_I4;
        child.lVal = p->child;
        DWORD state = accessible_state(p->acc, child);
        DWORD role = accessible_role(p->acc, child);
        BSTR action = accessible_text(p->acc, child, 5);
        switch (id) {
        case UIA_InvokePatternId:
            yes = action && *action;
            break;
        case UIA_SelectionItemPatternId:
            yes = (state & STATE_SELECTABLE) != 0;
            break;
        case UIA_TogglePatternId:
            yes = role == 0x2c;
            break;
        case UIA_ExpandCollapsePatternId:
            yes = (state & (STATE_EXPANDED | STATE_COLLAPSED)) != 0;
            break;
        case UIA_ValuePatternId: {
            BSTR value = accessible_text(p->acc, child, 1);
            yes = value && *value && role != 0x2c;
            SysFreeString(value);
            break;
        }
        }
        SysFreeString(action);
    }
    return yes ? native_pattern(n, id, out) : S_OK;
}

static void native_destroy(PATTERN *p)
{
    NodeRelease(((NATPAT *)p)->node);
}

static NODE *native_owner(PATTERN *p)
{
    return ((NATPAT *)p)->node;
}

static void ret_i(VARIANT *r, int v)
{
    r->vt = VT_I4;
    r->lVal = v;
}

static void ret_b(VARIANT *r, BOOL v)
{
    r->vt = VT_BOOL;
    r->boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
}

static void ret_d(VARIANT *r, double v)
{
    r->vt = VT_R8;
    r->dblVal = v;
}

static int argi(VARIANT *a, ULONG n, ULONG i)
{
    return i < n && a[i].vt == VT_I4 ? a[i].lVal : 0;
}

static double argd(VARIANT *a, ULONG n, ULONG i)
{
    return i < n && a[i].vt == VT_R8 ? a[i].dblVal : 0;
}

static HRESULT hwnd_range(PROXY *p, double *lo, double *hi, double *pos, double *small, double *large, BOOL *readonly)
{
    HWND hwnd = p->hwnd;
    *small = *large = 1;
    *readonly = FALSE;
    if (has_class(hwnd, L"ScrollBar")) {
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        if (!GetScrollInfo(hwnd, SB_CTL, &si))
            return UIA_E_ELEMENTNOTAVAILABLE;
        *lo = si.nMin;
        *hi = si.nMax;
        *pos = si.nPos;
        *large = si.nPage;
        return S_OK;
    }
    if (has_class(hwnd, L"msctls_trackbar32")) {
        *lo = (int)SendMessageW(hwnd, 0x401, 0, 0);
        *hi = (int)SendMessageW(hwnd, 0x402, 0, 0);
        *pos = (int)SendMessageW(hwnd, 0x400, 0, 0);
        *small = (int)SendMessageW(hwnd, 0x418, 0, 0);
        *large = (int)SendMessageW(hwnd, 0x416, 0, 0);
        return S_OK;
    }
    if (has_class(hwnd, L"msctls_progress32")) {
        *lo = (int)SendMessageW(hwnd, 0x407, TRUE, 0);
        *hi = (int)SendMessageW(hwnd, 0x407, FALSE, 0);
        *pos = (int)SendMessageW(hwnd, 0x408, 0, 0);
        *readonly = TRUE;
        return S_OK;
    }
    return UIA_E_NOTSUPPORTED;
}

static HRESULT window_pattern_call(PROXY *p, PATTERNID id, int method, VARIANT *a, ULONG n, VARIANT *ret)
{
    HWND hwnd = p->hwnd;
    if (!IsWindow(hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    switch (method) {
    case PM_Window_CanMaximize:
        ret_b(ret, (style & WS_MAXIMIZEBOX) != 0);
        return S_OK;
    case PM_Window_CanMinimize:
        ret_b(ret, (style & WS_MINIMIZEBOX) != 0);
        return S_OK;
    case PM_Window_IsModal: {
        HWND owner = GetWindow(hwnd, GW_OWNER);
        ret_b(ret, owner && !IsWindowEnabled(owner));
        return S_OK;
    }
    case PM_Window_IsTopmost:
        ret_b(ret, (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
        return S_OK;
    case PM_Window_VisualState:
        ret_i(ret, IsIconic(hwnd) ? WindowVisualState_Minimized : IsZoomed(hwnd) ? WindowVisualState_Maximized : WindowVisualState_Normal);
        return S_OK;
    case PM_Window_InteractionState: {
        DWORD_PTR result;
        if (!window_enabled(hwnd))
            ret_i(ret, WindowInteractionState_BlockedByModalWindow);
        else if (!SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 100, &result))
            ret_i(ret, WindowInteractionState_NotResponding);
        else
            ret_i(ret, WindowInteractionState_ReadyForUserInteraction);
        return S_OK;
    }
    case PM_Window_SetVisualState: {
        int state = argi(a, n, 0);
        if ((unsigned)state > 2)
            return E_INVALIDARG;
        if (!window_enabled(hwnd))
            return UIA_E_ELEMENTNOTENABLED;
        if (state == WindowVisualState_Maximized && !(style & WS_MAXIMIZEBOX))
            return UIA_E_INVALIDOPERATION;
        if (state == WindowVisualState_Minimized && !(style & WS_MINIMIZEBOX))
            return UIA_E_INVALIDOPERATION;
        ShowWindow(hwnd, state == WindowVisualState_Maximized ? SW_SHOWMAXIMIZED
                         : state == WindowVisualState_Minimized ? SW_SHOWMINIMIZED
                                                                : SW_RESTORE);
        return S_OK;
    }
    case PM_Window_Close:
        if (!PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0))
            return UIA_E_ELEMENTNOTAVAILABLE;
        return S_OK;
    case PM_Window_WaitForInputIdle: {
        DWORD_PTR result;
        int ms = argi(a, n, 0);
        ret_b(ret, SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, ms < 0 ? 0 : ms, &result) != 0);
        return S_OK;
    }
    case PM_Transform_CanMove:
        ret_b(ret, !IsZoomed(hwnd) && !IsIconic(hwnd));
        return S_OK;
    case PM_Transform_CanResize:
        ret_b(ret, (style & WS_THICKFRAME) != 0);
        return S_OK;
    case PM_Transform_CanRotate:
        ret_b(ret, FALSE);
        return S_OK;
    case PM_Transform_Move: {
        if (IsZoomed(hwnd) || IsIconic(hwnd))
            return UIA_E_INVALIDOPERATION;
        if (!SetWindowPos(hwnd, nullptr, (int)argd(a, n, 0), (int)argd(a, n, 1), 0, 0,
                          SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
            return E_FAIL;
        return S_OK;
    }
    case PM_Transform_Resize: {
        if (!(style & WS_THICKFRAME))
            return UIA_E_INVALIDOPERATION;
        if (!SetWindowPos(hwnd, nullptr, 0, 0, (int)argd(a, n, 0), (int)argd(a, n, 1),
                          SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
            return E_FAIL;
        return S_OK;
    }
    case PM_Transform_Rotate:
        return UIA_E_INVALIDOPERATION;
    }
    return E_NOTIMPL;
}

static const WCHAR *i_arg_string_(VARIANT *a, ULONG n);

static HRESULT control_pattern_call(PROXY *p, int method, VARIANT *a, ULONG n, VARIANT *ret)
{
    HWND hwnd = p->hwnd;
    if (!IsWindow(hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    WCHAR *text;
    switch (method) {
    case PM_Invoke_Invoke:
    case PM_Toggle_Toggle:
    case PM_SelectionItem_Select: {
        if (!window_enabled(hwnd))
            return UIA_E_ELEMENTNOTENABLED;
        if (method == PM_SelectionItem_Select && SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED)
            return S_OK;
        /* a click is posted so the control's thread is free to handle it. */
        PostMessageW(hwnd, BM_CLICK, 0, 0);
        return S_OK;
    }
    case PM_Toggle_State: {
        LRESULT check = SendMessageW(hwnd, BM_GETCHECK, 0, 0);
        ret_i(ret, check == BST_CHECKED ? ToggleState_On : check == BST_INDETERMINATE ? ToggleState_Indeterminate : ToggleState_Off);
        return S_OK;
    }
    case PM_SelectionItem_IsSelected:
        ret_b(ret, SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
        return S_OK;
    case PM_SelectionItem_AddToSelection:
    case PM_SelectionItem_RemoveFromSelection:
        return UIA_E_INVALIDOPERATION;
    case PM_SelectionItem_SelectionContainer:
        ret->vt = VT_UNKNOWN;
        ret->punkVal = nullptr;
        return S_OK;
    case PM_Value_Value: {
        int len = GetWindowTextLengthW(hwnd);
        text = UiaAlloc(((SIZE_T)len + 1) * sizeof(WCHAR));
        if (!text)
            return E_OUTOFMEMORY;
        GetWindowTextW(hwnd, text, len + 1);
        ret->bstrVal = SysAllocString(text);
        ret->vt = VT_BSTR;
        UiaFree(text);
        return S_OK;
    }
    case PM_Value_IsReadOnly:
        ret_b(ret, has_class(hwnd, L"Edit") ? (GetWindowLongW(hwnd, GWL_STYLE) & ES_READONLY) != 0
                                            : !has_class(hwnd, L"ComboBox"));
        return S_OK;
    case PM_Value_SetValue:
        if (!window_enabled(hwnd))
            return UIA_E_ELEMENTNOTENABLED;
        if (has_class(hwnd, L"Edit") && (GetWindowLongW(hwnd, GWL_STYLE) & ES_READONLY))
            return UIA_E_NOTSUPPORTED;
        if (!SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)(i_arg_string_(a, n)))) {
            if (!has_class(hwnd, L"Edit"))
                return E_FAIL;
        }
        return S_OK;
    case PM_ExpandCollapse_State:
        ret_i(ret, SendMessageW(hwnd, 0x157, 0, 0) ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed);
        return S_OK;
    case PM_ExpandCollapse_Expand:
    case PM_ExpandCollapse_Collapse:
        if (!window_enabled(hwnd))
            return UIA_E_ELEMENTNOTENABLED;
        SendMessageW(hwnd, 0x14F, method == PM_ExpandCollapse_Expand, 0);
        return S_OK;
    case PM_RangeValue_Value:
    case PM_RangeValue_Minimum:
    case PM_RangeValue_Maximum:
    case PM_RangeValue_SmallChange:
    case PM_RangeValue_LargeChange:
    case PM_RangeValue_IsReadOnly:
    case PM_RangeValue_SetValue: {
        double lo, hi, pos, small, large;
        BOOL readonly;
        HRESULT hr = hwnd_range(p, &lo, &hi, &pos, &small, &large, &readonly);
        if (FAILED(hr))
            return hr;
        switch (method) {
        case PM_RangeValue_Value: ret_d(ret, pos); break;
        case PM_RangeValue_Minimum: ret_d(ret, lo); break;
        case PM_RangeValue_Maximum: ret_d(ret, hi); break;
        case PM_RangeValue_SmallChange: ret_d(ret, small); break;
        case PM_RangeValue_LargeChange: ret_d(ret, large); break;
        case PM_RangeValue_IsReadOnly: ret_b(ret, readonly); break;
        default: {
            double want = argd(a, n, 0);
            if (readonly)
                return UIA_E_NOTSUPPORTED;
            if (want < lo || want > hi)
                return E_INVALIDARG;
            if (!window_enabled(hwnd))
                return UIA_E_ELEMENTNOTENABLED;
            if (has_class(hwnd, L"ScrollBar")) {
                SCROLLINFO si = {sizeof(si), SIF_POS, 0, 0, 0, (int)want};
                SetScrollInfo(hwnd, SB_CTL, &si, TRUE);
            } else {
                SendMessageW(hwnd, 0x405, TRUE, (LPARAM)(int)want);
            }
            break;
        }
        }
        return S_OK;
    }
    }
    return E_NOTIMPL;
}

/* the words of a call's first argument. */
static const WCHAR *i_arg_string_(VARIANT *a, ULONG n)
{
    return n && a[0].vt == VT_BSTR && a[0].bstrVal ? a[0].bstrVal : L"";
}

static HRESULT legacy_call(PROXY *p, int method, VARIANT *a, ULONG n, VARIANT *ret)
{
    VARIANT child;
    IAccessible *acc = speaking_accessible(p, &child);
    if (!acc)
        return UIA_E_NOTSUPPORTED;
    HRESULT hr = S_OK;
    BSTR text = nullptr;
    switch (method) {
    case PM_Legacy_ChildId:
        ret_i(ret, child.lVal);
        break;
    case PM_Legacy_Name: hr = acc->lpVtbl->get_accName(acc, child, &text); break;
    case PM_Legacy_Value: hr = acc->lpVtbl->get_accValue(acc, child, &text); break;
    case PM_Legacy_Description: hr = acc->lpVtbl->get_accDescription(acc, child, &text); break;
    case PM_Legacy_Help: hr = acc->lpVtbl->get_accHelp(acc, child, &text); break;
    case PM_Legacy_KeyboardShortcut: hr = acc->lpVtbl->get_accKeyboardShortcut(acc, child, &text); break;
    case PM_Legacy_DefaultAction: hr = acc->lpVtbl->get_accDefaultAction(acc, child, &text); break;
    case PM_Legacy_Role:
        ret_i(ret, (int)accessible_role(acc, child));
        break;
    case PM_Legacy_State:
        ret_i(ret, (int)accessible_state(acc, child));
        break;
    case PM_Legacy_DoDefaultAction:
        hr = acc->lpVtbl->accDoDefaultAction(acc, child);
        break;
    case PM_Legacy_Select:
        hr = acc->lpVtbl->accSelect(acc, argi(a, n, 0), child);
        break;
    case PM_Legacy_SetValue: {
        BSTR value = SysAllocString(i_arg_string_(a, n));
        hr = acc->lpVtbl->put_accValue(acc, child, value);
        SysFreeString(value);
        break;
    }
    case PM_Legacy_GetIAccessible:
        acc->lpVtbl->AddRef(acc);
        ret->vt = VT_UNKNOWN;
        ret->punkVal = (IUnknown *)acc;
        break;
    case PM_Legacy_GetSelection:
        ret->vt = VT_ARRAY | VT_UNKNOWN;
        ret->parray = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
        break;
    default:
        hr = E_NOTIMPL;
    }
    if (text) {
        ret->vt = VT_BSTR;
        ret->bstrVal = text;
    } else if (SUCCEEDED(hr) && ret && ret->vt == VT_EMPTY &&
               (method == PM_Legacy_Name || method == PM_Legacy_Value || method == PM_Legacy_Description ||
                method == PM_Legacy_Help || method == PM_Legacy_KeyboardShortcut || method == PM_Legacy_DefaultAction)) {
        ret->vt = VT_BSTR;
        ret->bstrVal = SysAllocString(L"");
    }
    /* msaa says S_FALSE for a property an object does not have. */
    if (hr == S_FALSE)
        hr = S_OK;
    acc->lpVtbl->Release(acc);
    return hr;
}

static HRESULT acc_pattern_call(PROXY *p, int method, VARIANT *a, ULONG n, VARIANT *ret)
{
    VARIANT child;
    VariantInit(&child);
    child.vt = VT_I4;
    child.lVal = p->child;
    IAccessible *acc = p->acc;
    switch (method) {
    case PM_Invoke_Invoke:
    case PM_Toggle_Toggle:
        return acc->lpVtbl->accDoDefaultAction(acc, child);
    case PM_Toggle_State: {
        DWORD s = accessible_state(acc, child);
        ret_i(ret, s & STATE_CHECKED ? ToggleState_On : s & STATE_MIXED ? ToggleState_Indeterminate : ToggleState_Off);
        return S_OK;
    }
    case PM_SelectionItem_Select:
        return acc->lpVtbl->accSelect(acc, SELFLAG_TAKESELECTION | SELFLAG_TAKEFOCUS, child);
    case PM_SelectionItem_AddToSelection:
        return acc->lpVtbl->accSelect(acc, SELFLAG_ADDSELECTION, child);
    case PM_SelectionItem_RemoveFromSelection:
        return acc->lpVtbl->accSelect(acc, SELFLAG_REMOVESELECTION, child);
    case PM_SelectionItem_IsSelected:
        ret_b(ret, (accessible_state(acc, child) & STATE_SELECTED) != 0);
        return S_OK;
    case PM_SelectionItem_SelectionContainer:
        ret->vt = VT_UNKNOWN;
        ret->punkVal = nullptr;
        return S_OK;
    case PM_Value_Value: {
        BSTR value = accessible_text(acc, child, 1);
        ret->vt = VT_BSTR;
        ret->bstrVal = value ? value : SysAllocString(L"");
        return S_OK;
    }
    case PM_Value_IsReadOnly:
        ret_b(ret, (accessible_state(acc, child) & STATE_READONLY) != 0);
        return S_OK;
    case PM_ExpandCollapse_State: {
        DWORD s = accessible_state(acc, child);
        ret_i(ret, s & STATE_EXPANDED ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed);
        return S_OK;
    }
    case PM_ExpandCollapse_Expand:
    case PM_ExpandCollapse_Collapse:
        return acc->lpVtbl->accDoDefaultAction(acc, child);
    }
    return E_NOTIMPL;
}

static HRESULT native_call(PATTERN *pattern, int method, VARIANT *a, ULONG n, VARIANT *ret)
{
    NATPAT *np = (NATPAT *)pattern;
    PROXY *p = PX(np->node);
    VARIANT scratch;
    VariantInit(&scratch);
    if (!ret)
        ret = &scratch;
    if (pattern->id == UIA_LegacyIAccessiblePatternId)
        return legacy_call(p, method, a, n, ret);
    if (p->kind == PK_ACC)
        return acc_pattern_call(p, method, a, n, ret);
    if (pattern->id == UIA_WindowPatternId || pattern->id == UIA_TransformPatternId)
        return window_pattern_call(p, pattern->id, method, a, n, ret);
    return control_pattern_call(p, method, a, n, ret);
}

static const PATTERN_OPS g_native_pattern_ops = {native_destroy, native_call, native_owner};

/* ------------------------------------------------------------------ the tree of proxies */

static HRESULT acc_node_of(IDispatch *dispatch, HWND hwnd, LONG object, NODE **out)
{
    *out = nullptr;
    IAccessible *acc = nullptr;
    if (!dispatch || FAILED(dispatch->lpVtbl->QueryInterface(dispatch, &IID_IAccessible, (void **)&acc)))
        return E_NOINTERFACE;
    HWND owner = nullptr;
    if (FAILED(WindowFromAccessibleObject(acc, &owner)) || !owner)
        owner = hwnd;
    HRESULT hr = proxy_make(PK_ACC, owner, acc, object, 0, out);
    acc->lpVtbl->Release(acc);
    return hr;
}

HRESULT ProxyNodeFromAccessible(IAccessible *acc, LONG child, NODE **out)
{
    HWND owner = nullptr;
    WindowFromAccessibleObject(acc, &owner);
    return proxy_make(PK_ACC, owner, acc, 0, child, out);
}

/* the children of a node, nodes of this process. */
static HRESULT children_of(PROXY *p, NODE ***list, int *count);

static HRESULT push_node(NODE ***list, int *count, int *capacity, NODE *node)
{
    if (*count == *capacity) {
        int grown = *capacity ? *capacity * 2 : 8;
        NODE **bigger = UiaAlloc((SIZE_T)grown * sizeof(*bigger));
        if (!bigger) {
            NodeRelease(node);
            return E_OUTOFMEMORY;
        }
        if (*count)
            memcpy(bigger, *list, (SIZE_T)*count * sizeof(*bigger));
        UiaFree(*list);
        *list = bigger;
        *capacity = grown;
    }
    (*list)[(*count)++] = node;
    return S_OK;
}

static BOOL should_show(HWND hwnd)
{
    return IsWindowVisible(hwnd);
}

static HRESULT window_children(HWND hwnd, NODE ***list, int *count, int *capacity)
{
    HRESULT hr = S_OK;
    if (is_top_level(hwnd)) {
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        NODE *item = nullptr;
        if ((style & WS_CAPTION) == WS_CAPTION) {
            IAccessible *bar = window_accessible(hwnd, OBJID_TITLEBAR);
            if (bar) {
                hr = proxy_make(PK_ACC, hwnd, bar, OBJID_TITLEBAR, 0, &item);
                bar->lpVtbl->Release(bar);
                if (SUCCEEDED(hr))
                    hr = push_node(list, count, capacity, item);
            }
        }
        if (SUCCEEDED(hr) && GetMenu(hwnd)) {
            IAccessible *menu = window_accessible(hwnd, OBJID_MENU);
            if (menu) {
                hr = proxy_make(PK_ACC, hwnd, menu, OBJID_MENU, 0, &item);
                menu->lpVtbl->Release(menu);
                if (SUCCEEDED(hr))
                    hr = push_node(list, count, capacity, item);
            }
        }
    }
    /* parts of the client that have no window, such as the items of a list. */
    IAccessible *client = window_accessible(hwnd, OBJID_CLIENT);
    LONG items = 0;
    if (client && SUCCEEDED(client->lpVtbl->get_accChildCount(client, &items)) && items > 0 && items < 4096) {
        VARIANT *kids = UiaAlloc((SIZE_T)items * sizeof(VARIANT));
        LONG got = 0;
        if (kids && SUCCEEDED(AccessibleChildren(client, 0, items, kids, &got))) {
            for (LONG i = 0; i < got && SUCCEEDED(hr); i++) {
                NODE *item = nullptr;
                if (kids[i].vt == VT_I4) {
                    hr = proxy_make(PK_ACC, hwnd, client, OBJID_CLIENT, kids[i].lVal, &item);
                    if (SUCCEEDED(hr))
                        hr = push_node(list, count, capacity, item);
                }
            }
            for (LONG i = 0; i < got; i++)
                VariantClear(&kids[i]);
        }
        UiaFree(kids);
    }
    if (client)
        client->lpVtbl->Release(client);
    for (HWND child = GetWindow(hwnd, GW_CHILD); SUCCEEDED(hr) && child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!should_show(child))
            continue;
        NODE *node = nullptr;
        hr = UiaNodeForWindow(child, &node);
        if (SUCCEEDED(hr))
            hr = push_node(list, count, capacity, node);
    }
    return hr;
}

static HRESULT children_of(PROXY *p, NODE ***list, int *count)
{
    *list = nullptr;
    *count = 0;
    int capacity = 0;
    HRESULT hr = S_OK;
    if (p->kind == PK_DESKTOP) {
        for (HWND top = GetWindow(GetDesktopWindow(), GW_CHILD); SUCCEEDED(hr) && top; top = GetWindow(top, GW_HWNDNEXT)) {
            if (!should_show(top))
                continue;
            NODE *node = nullptr;
            hr = UiaNodeForWindow(top, &node);
            if (SUCCEEDED(hr))
                hr = push_node(list, count, &capacity, node);
        }
    } else if (p->kind == PK_WINDOW) {
        hr = window_children(p->hwnd, list, count, &capacity);
    } else if (p->child == 0) {
        LONG items = 0;
        VARIANT self;
        VariantInit(&self);
        self.vt = VT_I4;
        if (SUCCEEDED(p->acc->lpVtbl->get_accChildCount(p->acc, &items)) && items > 0 && items < 4096) {
            VARIANT *kids = UiaAlloc((SIZE_T)items * sizeof(VARIANT));
            LONG got = 0;
            if (kids && SUCCEEDED(AccessibleChildren(p->acc, 0, items, kids, &got))) {
                for (LONG i = 0; i < got && SUCCEEDED(hr); i++) {
                    NODE *item = nullptr;
                    if (kids[i].vt == VT_I4)
                        hr = proxy_make(PK_ACC, p->hwnd, p->acc, p->object, kids[i].lVal, &item);
                    else if (kids[i].vt == VT_DISPATCH)
                        hr = acc_node_of(kids[i].pdispVal, p->hwnd, p->object, &item);
                    if (SUCCEEDED(hr) && item)
                        hr = push_node(list, count, &capacity, item);
                }
                for (LONG i = 0; i < got; i++)
                    VariantClear(&kids[i]);
            }
            UiaFree(kids);
        }
    }
    if (FAILED(hr)) {
        NodeListFree(*list, *count);
        *list = nullptr;
        *count = 0;
    }
    return hr;
}

static HRESULT parent_of(PROXY *p, NODE **out)
{
    *out = nullptr;
    if (p->kind == PK_DESKTOP)
        return S_OK;
    if (p->kind == PK_WINDOW) {
        HWND parent = GetAncestor(p->hwnd, GA_PARENT);
        if (!parent || parent == GetDesktopWindow())
            return ProxyDesktopNode(out);
        return UiaNodeForWindow(parent, out);
    }
    /* a simple element belongs to the object that numbers it. */
    if (p->child != 0)
        return proxy_make(PK_ACC, p->hwnd, p->acc, p->object, 0, out);
    if (p->object == OBJID_TITLEBAR || p->object == OBJID_MENU)
        return UiaNodeForWindow(p->hwnd, out);
    IDispatch *parent = nullptr;
    if (SUCCEEDED(p->acc->lpVtbl->get_accParent(p->acc, &parent)) && parent) {
        HRESULT hr = acc_node_of(parent, p->hwnd, p->object, out);
        parent->lpVtbl->Release(parent);
        return hr;
    }
    return p->hwnd ? UiaNodeForWindow(p->hwnd, out) : S_OK;
}

static BOOL proxy_same(NODE *a, NODE *b)
{
    if (!is_proxy(a) || !is_proxy(b))
        return FALSE;
    PROXY *x = PX(a), *y = PX(b);
    if (x->kind != y->kind || x->hwnd != y->hwnd)
        return FALSE;
    return x->kind != PK_ACC || (x->object == y->object && x->child == y->child);
}

static HRESULT proxy_navigate(NODE *n, NavigateDirection dir, NODE **out)
{
    *out = nullptr;
    PROXY *p = PX(n);
    if (p->kind == PK_WINDOW && !IsWindow(p->hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (dir == NavigateDirection_Parent)
        return parent_of(p, out);
    NODE **list;
    int count;
    HRESULT hr;
    if (dir == NavigateDirection_FirstChild || dir == NavigateDirection_LastChild) {
        hr = children_of(p, &list, &count);
        if (SUCCEEDED(hr) && count) {
            int pick = dir == NavigateDirection_FirstChild ? 0 : count - 1;
            *out = list[pick];
            list[pick] = nullptr;
        }
        NodeListFree(list, count);
        return hr;
    }
    if (p->kind == PK_DESKTOP)
        return S_OK;
    NODE *parent = nullptr;
    hr = parent_of(p, &parent);
    if (FAILED(hr) || !parent)
        return hr;
    if (!is_proxy(parent)) {
        NodeRelease(parent);
        return S_OK;
    }
    hr = children_of(PX(parent), &list, &count);
    NodeRelease(parent);
    if (FAILED(hr))
        return hr;
    for (int i = 0; i < count; i++) {
        BOOL same = proxy_same(list[i], n);
        if (!same && is_proxy(list[i]) && PX(list[i])->kind == PK_WINDOW && p->kind == PK_WINDOW)
            same = PX(list[i])->hwnd == p->hwnd;
        if (!same && !is_proxy(list[i]) && p->kind == PK_WINDOW)
            same = NodeOwnerWindow(list[i]) == p->hwnd;
        if (!same)
            continue;
        int at = dir == NavigateDirection_NextSibling ? i + 1 : i - 1;
        if (at >= 0 && at < count) {
            *out = list[at];
            list[at] = nullptr;
        }
        break;
    }
    NodeListFree(list, count);
    return S_OK;
}

static HRESULT proxy_runtime_id(NODE *n, int **ids, int *count)
{
    PROXY *p = PX(n);
    int len = p->kind == PK_ACC ? 4 : 2;
    int *made = UiaAlloc((SIZE_T)len * sizeof(int));
    if (!made)
        return E_OUTOFMEMORY;
    made[0] = 42;
    made[1] = (int)(LONG_PTR)p->hwnd;
    if (p->kind == PK_ACC) {
        made[2] = p->object;
        made[3] = p->child;
    }
    *ids = made;
    *count = len;
    return S_OK;
}

static HRESULT proxy_options(NODE *n, int *options)
{
    *options = ProviderOptions_ClientSideProvider;
    return S_OK;
}

static HRESULT proxy_host_hwnd(NODE *n, HWND *hwnd)
{
    *hwnd = nullptr;
    return S_OK;
}

static HRESULT proxy_set_focus(NODE *n)
{
    PROXY *p = PX(n);
    if (p->kind == PK_WINDOW) {
        if (!IsWindowEnabled(p->hwnd))
            return UIA_E_ELEMENTNOTENABLED;
        DWORD pid;
        DWORD thread = GetWindowThreadProcessId(p->hwnd, &pid);
        if (thread == GetCurrentThreadId()) {
            SetFocus(p->hwnd);
            return S_OK;
        }
        SetForegroundWindow(GetAncestor(p->hwnd, GA_ROOT));
        SendMessageW(p->hwnd, WM_SETFOCUS, 0, 0);
        return S_OK;
    }
    if (p->kind == PK_ACC) {
        VARIANT child;
        VariantInit(&child);
        child.vt = VT_I4;
        child.lVal = p->child;
        return p->acc->lpVtbl->accSelect(p->acc, SELFLAG_TAKEFOCUS, child);
    }
    return UIA_E_NOTSUPPORTED;
}

static HRESULT proxy_root_from_point(NODE *n, double x, double y, NODE **out)
{
    *out = nullptr;
    return UIA_E_NOTSUPPORTED;
}

static HRESULT proxy_root_focus(NODE *n, NODE **out)
{
    *out = nullptr;
    return UIA_E_NOTSUPPORTED;
}

static HRESULT proxy_fragment_root(NODE *n, NODE **out)
{
    PROXY *p = PX(n);
    *out = nullptr;
    if (p->kind == PK_DESKTOP)
        return S_OK;
    HWND root = p->hwnd ? GetAncestor(p->hwnd, GA_ROOT) : nullptr;
    return root ? UiaNodeForWindow(root, out) : S_OK;
}

static HRESULT proxy_embedded_roots(NODE *n, NODE ***list, int *count)
{
    *list = nullptr;
    *count = 0;
    return S_OK;
}

static HRESULT proxy_process_id(NODE *n, DWORD *pid)
{
    *pid = 0;
    if (PX(n)->hwnd)
        GetWindowThreadProcessId(PX(n)->hwnd, pid);
    return S_OK;
}

static HRESULT proxy_advise(NODE *n, BOOL added, int event, SAFEARRAY *props)
{
    return S_OK;
}

static IRawElementProviderSimple *proxy_provider(NODE *n)
{
    return nullptr;
}

static BOOL proxy_is_fragment(NODE *n)
{
    return TRUE;
}

static const NODE_OPS g_proxy_ops = {
    proxy_destroy,     proxy_get_prop,   proxy_get_pattern,  proxy_navigate,       proxy_runtime_id,
    proxy_options,     proxy_host_hwnd,  proxy_set_focus,    proxy_root_from_point, proxy_root_focus,
    proxy_fragment_root, proxy_embedded_roots, proxy_process_id, proxy_advise,     proxy_provider,
    proxy_is_fragment,
};

/* ------------------------------------------------------------------ finding the element of a window */

#define GETOBJECT_TIMEOUT 10000

HRESULT UiaNodeForWindow(HWND hwnd, NODE **out)
{
    *out = nullptr;
    if (!IsWindow(hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (hwnd == GetDesktopWindow())
        return ProxyDesktopNode(out);
    /* an application that gave proxies for its windows has them used. */
    if (g_provider_callback) {
        SAFEARRAY *providers = g_provider_callback(hwnd, ProviderType_Proxy);
        if (providers) {
            IUnknown **items;
            LONG lower, upper;
            SafeArrayGetLBound(providers, 1, &lower);
            SafeArrayGetUBound(providers, 1, &upper);
            if (upper >= lower && SUCCEEDED(SafeArrayAccessData(providers, (void **)&items))) {
                if (items[0])
                    NodeFromUnknown(items[0], out);
                SafeArrayUnaccessData(providers);
            }
            SafeArrayDestroy(providers);
            if (*out)
                return S_OK;
        }
    }
    UiaEnsureCom();
    DWORD pid = GetCurrentProcessId();
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(hwnd, WM_GETOBJECT, pid, UiaRootObjectId, SMTO_ABORTIFHUNG, GETOBJECT_TIMEOUT, &result) &&
        result && (LONG_PTR)result > 0 && result < 0x10000) {
        IDispatch *dispatch = nullptr;
        if (SUCCEEDED(ObjectFromLresult(result, &IID_IDispatch, pid, (void **)&dispatch)) && dispatch) {
            HRESULT hr = RemoteNodeFromDispatch(dispatch, out);
            dispatch->lpVtbl->Release(dispatch);
            if (SUCCEEDED(hr) && *out)
                return S_OK;
        }
    }
    return ProxyNodeFromWindow(hwnd, out);
}

HRESULT UiaClientNodeFromWindow(HWND hwnd, NODE **out)
{
    return UiaNodeForWindow(hwnd, out);
}

HRESULT UiaClientRootNode(NODE **out)
{
    return ProxyDesktopNode(out);
}

/* the deepest element at a point: the window there, then what that window says is under it. */
HRESULT UiaClientNodeFromPoint(POINT pt, NODE **out)
{
    *out = nullptr;
    HWND hwnd = WindowFromPoint(pt);
    if (!hwnd)
        return UIA_E_ELEMENTNOTAVAILABLE;
    /* the topmost window of the point, then its deepest child. */
    HWND deepest = hwnd;
    for (;;) {
        POINT local = pt;
        ScreenToClient(deepest, &local);
        HWND child = ChildWindowFromPointEx(deepest, local, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!child || child == deepest)
            break;
        deepest = child;
    }
    NODE *node = nullptr;
    HRESULT hr = UiaNodeForWindow(deepest, &node);
    if (FAILED(hr))
        return hr;
    for (int guard = 0; guard < 64; guard++) {
        NODE *inner = nullptr;
        if (!node->ops->is_fragment(node) || is_proxy(node))
            break;
        if (FAILED(node->ops->root_from_point(node, pt.x, pt.y, &inner)) || !inner || NodeSame(inner, node)) {
            NodeRelease(inner);
            break;
        }
        NodeRelease(node);
        node = inner;
    }
    if (is_proxy(node) && PX(node)->kind == PK_WINDOW) {
        IAccessible *client = window_accessible(PX(node)->hwnd, OBJID_CLIENT);
        if (client) {
            VARIANT hit;
            VariantInit(&hit);
            if (SUCCEEDED(client->lpVtbl->accHitTest(client, pt.x, pt.y, &hit)) && hit.vt == VT_I4 && hit.lVal > 0) {
                NODE *item = nullptr;
                if (SUCCEEDED(proxy_make(PK_ACC, PX(node)->hwnd, client, OBJID_CLIENT, hit.lVal, &item))) {
                    NodeRelease(node);
                    node = item;
                }
            }
            VariantClear(&hit);
            client->lpVtbl->Release(client);
        }
    }
    *out = node;
    return S_OK;
}

HRESULT UiaClientNodeFromFocus(NODE **out)
{
    *out = nullptr;
    GUITHREADINFO info = {sizeof(info)};
    HWND hwnd = nullptr;
    if (GetGUIThreadInfo(0, &info))
        hwnd = info.hwndFocus ? info.hwndFocus : info.hwndActive;
    if (!hwnd)
        return UIA_E_ELEMENTNOTAVAILABLE;
    NODE *node = nullptr;
    HRESULT hr = UiaNodeForWindow(hwnd, &node);
    if (FAILED(hr))
        return hr;
    for (int guard = 0; guard < 64; guard++) {
        NODE *inner = nullptr;
        if (is_proxy(node) || !node->ops->is_fragment(node))
            break;
        if (FAILED(node->ops->root_focus(node, &inner)) || !inner || NodeSame(inner, node)) {
            NodeRelease(inner);
            break;
        }
        NodeRelease(node);
        node = inner;
    }
    *out = node;
    return S_OK;
}

BOOL ProxyHasServerSideProvider(HWND hwnd)
{
    return IsWindow(hwnd) && UiaProviderTableHas(hwnd);
}
