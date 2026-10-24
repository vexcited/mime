/*
 * a window's taskbar state: progress, overlay icon, thumbnail toolbar, tooltip, clip and registered tabs. there is
 * no taskbar, so the settings are only kept. progress is also stored as window properties, "TaskbarProgress"
 * for the state and "TaskbarValue" for the value from 0 to 65534, for a host that shows progress.
 */
#include "explorerframe_private.h"
#include <mime/shellclient.h>

#define MAX_WINDOWS 128
#define MAX_BUTTONS 7
#define PROGRESS_MAX 65534
#define TAB_PROPERTY ((LPCWSTR)(ULONG_PTR)0xA91B)

typedef struct _WINDOW_STATE {
    HWND Window;
    HICON Overlay;
    WCHAR OverlayText[260];
    WCHAR Tooltip[260];
    BOOL HasClip;
    RECT Clip;
    UINT ButtonCount;
    THUMBBUTTON Buttons[MAX_BUTTONS];
} WINDOW_STATE;

static SRWLOCK g_lock = SRWLOCK_INIT;
static WINDOW_STATE g_states[MAX_WINDOWS];

typedef struct _TASKBAR_LIST {
    ITaskbarList4 Interface;
    LONG Refs;
} TASKBAR_LIST;

/* also drops the state of windows that are gone. */
static WINDOW_STATE *state_of(HWND window, BOOL create)
{
    WINDOW_STATE *free_slot = nullptr;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        WINDOW_STATE *s = &g_states[i];
        if (s->Window && !IsWindow(s->Window)) {
            if (s->Overlay)
                DestroyIcon(s->Overlay);
            memset(s, 0, sizeof(*s));
        }
        if (s->Window == window)
            return s;
        if (!s->Window && !free_slot)
            free_slot = s;
    }
    if (create && free_slot) {
        free_slot->Window = window;
        return free_slot;
    }
    return nullptr;
}

static void copy_text(WCHAR *to, SIZE_T chars, LPCWSTR from)
{
    SIZE_T n = 0;
    if (from)
        while (n + 1 < chars && from[n]) {
            to[n] = from[n];
            n++;
        }
    to[n] = 0;
}

static HRESULT STDMETHODCALLTYPE tl_query_interface(ITaskbarList3 *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_ITaskbarList) && !IsEqualIID(riid, &IID_ITaskbarList2) &&
        !IsEqualIID(riid, &IID_ITaskbarList3) && !IsEqualIID(riid, &IID_ITaskbarList4))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((TASKBAR_LIST *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE tl_add_ref(ITaskbarList3 *This)
{
    return (ULONG)InterlockedIncrement(&((TASKBAR_LIST *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE tl_release(ITaskbarList3 *This)
{
    LONG refs = InterlockedDecrement(&((TASKBAR_LIST *)This)->Refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE tl_hr_init(ITaskbarList3 *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tl_add_tab(ITaskbarList3 *This, HWND window)
{
    RemovePropW(window, L"ITaskList_Deleted");
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tl_delete_tab(ITaskbarList3 *This, HWND window)
{
    SetPropW(window, L"ITaskList_Deleted", (HANDLE)1);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tl_nothing(ITaskbarList3 *This, HWND window)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tl_mark_fullscreen(ITaskbarList3 *This, HWND window, BOOL fullscreen)
{
    return S_OK;
}

/*
 * tells the desktop launcher the bar's fill in hundredths of a percent. no progress hides the bar, an indeterminate
 * one shows it empty.
 */
static void send_progress(HWND window)
{
    ULONG_PTR state = (ULONG_PTR)GetPropW(window, L"TaskbarProgress");
    ULONG_PTR value = (ULONG_PTR)GetPropW(window, L"TaskbarValue");
    MimeShellWindow w;
    MimeShellClear(&w, sizeof(w));
    w.op = MIME_SHELL_LAUNCHER;
    w.window_id = (uint32_t)(ULONG_PTR)window;
    w.changed = MIME_LAUNCHER_PROGRESS;
    w.progress_visible = (state & ~(ULONG_PTR)TBPF_INDETERMINATE) != 0 || (state & TBPF_INDETERMINATE) != 0;
    w.indeterminate = (state & TBPF_INDETERMINATE) != 0;
    w.progress = w.indeterminate ? 0 : (uint32_t)(value * 10000 / PROGRESS_MAX);
    MimeShellWindowCommand(&w);
}

static HRESULT STDMETHODCALLTYPE tl_progress_value(ITaskbarList3 *This, HWND window, ULONGLONG completed,
                                                   ULONGLONG total)
{
    ULONGLONG value;
    /* 0 when none is done, the top when all is, else a share of 65532 plus one, which can overflow. */
    if (!completed) {
        value = 0;
    } else if (completed >= total) {
        value = PROGRESS_MAX;
    } else {
        value = (65532 * completed) / total + 1;
        if (value > 0xFFFFFFFFull)
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }
    SetPropW(window, L"TaskbarValue", (HANDLE)(ULONG_PTR)value);
    send_progress(window);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tl_progress_state(ITaskbarList3 *This, HWND window, TBPFLAG flags)
{
    SetPropW(window, L"TaskbarProgress", (HANDLE)(ULONG_PTR)flags);
    send_progress(window);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tl_register_tab(ITaskbarList3 *This, HWND tab, HWND mdi)
{
    if (!tab || !mdi || GetPropW(tab, TAB_PROPERTY) || !SetPropW(tab, TAB_PROPERTY, mdi))
        return E_FAIL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tl_unregister_tab(ITaskbarList3 *This, HWND tab)
{
    if (tab) {
        RemovePropW(tab, TAB_PROPERTY);
        RemovePropW(tab, L"ITaskList_TabProperties");
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tl_tab_order(ITaskbarList3 *This, HWND tab, HWND before)
{
    return tab && GetPropW(tab, TAB_PROPERTY) ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE tl_tab_active(ITaskbarList3 *This, HWND tab, HWND mdi, DWORD reserved)
{
    if (reserved || !mdi || (tab && GetPropW(tab, TAB_PROPERTY) != mdi))
        return E_FAIL;
    return S_OK;
}

static HRESULT thumb_buttons(HWND window, UINT count, LPTHUMBBUTTON buttons, BOOL update)
{
    if (!count || count > MAX_BUTTONS || !buttons)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&g_lock);
    WINDOW_STATE *s = state_of(window, TRUE);
    HRESULT hr = E_OUTOFMEMORY;
    if (s) {
        hr = S_OK;
        if (!update) {
            s->ButtonCount = count;
            memcpy(s->Buttons, buttons, count * sizeof(THUMBBUTTON));
        } else {
            /* an update changes already added buttons, matched by id. */
            for (UINT i = 0; i < count; i++) {
                BOOL found = FALSE;
                for (UINT k = 0; k < s->ButtonCount && !found; k++)
                    if (s->Buttons[k].iId == buttons[i].iId) {
                        s->Buttons[k] = buttons[i];
                        found = TRUE;
                    }
                if (!found)
                    hr = E_INVALIDARG;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE tl_buttons_add(ITaskbarList3 *This, HWND window, UINT count, LPTHUMBBUTTON buttons)
{
    return thumb_buttons(window, count, buttons, FALSE);
}

static HRESULT STDMETHODCALLTYPE tl_buttons_update(ITaskbarList3 *This, HWND window, UINT count, LPTHUMBBUTTON buttons)
{
    return thumb_buttons(window, count, buttons, TRUE);
}

static HRESULT STDMETHODCALLTYPE tl_button_images(ITaskbarList3 *This, HWND window, HANDLE images)
{
    return S_OK;
}

/*
 * desktop launchers cannot show an overlay icon, only a count badge. as a guess, the first number in the description
 * is the count, and an overlay without one counts as one.
 */
static void send_badge(HWND window, BOOL present, LPCWSTR description)
{
    ULONG count = 0;
    if (present) {
        count = 1;
        for (LPCWSTR p = description; p && *p; p++)
            if (*p >= L'0' && *p <= L'9') {
                count = 0;
                while (*p >= L'0' && *p <= L'9' && count < 100000)
                    count = count * 10 + (ULONG)(*p++ - L'0');
                break;
            }
    }
    MimeShellWindow w;
    MimeShellClear(&w, sizeof(w));
    w.op = MIME_SHELL_LAUNCHER;
    w.window_id = (uint32_t)(ULONG_PTR)window;
    w.changed = MIME_LAUNCHER_BADGE;
    w.count = count;
    MimeShellWindowCommand(&w);
}

static HRESULT STDMETHODCALLTYPE tl_overlay(ITaskbarList3 *This, HWND window, HICON icon, LPCWSTR description)
{
    AcquireSRWLockExclusive(&g_lock);
    WINDOW_STATE *s = state_of(window, TRUE);
    if (s) {
        HICON copy = icon ? CopyIcon(icon) : nullptr;
        if (s->Overlay)
            DestroyIcon(s->Overlay);
        s->Overlay = copy;
        copy_text(s->OverlayText, 260, icon ? description : nullptr);
    }
    ReleaseSRWLockExclusive(&g_lock);
    send_badge(window, icon != nullptr, icon ? description : nullptr);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tl_tooltip(ITaskbarList3 *This, HWND window, LPCWSTR tip)
{
    AcquireSRWLockExclusive(&g_lock);
    WINDOW_STATE *s = state_of(window, TRUE);
    if (s)
        copy_text(s->Tooltip, 260, tip);
    ReleaseSRWLockExclusive(&g_lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tl_clip(ITaskbarList3 *This, HWND window, RECT *clip)
{
    AcquireSRWLockExclusive(&g_lock);
    WINDOW_STATE *s = state_of(window, TRUE);
    if (s) {
        s->HasClip = clip != nullptr;
        if (clip)
            s->Clip = *clip;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return S_OK;
}

/* mutually exclusive flag pairs are invalid, and only a registered tab takes flags. */
static HRESULT STDMETHODCALLTYPE tl_tab_properties(ITaskbarList4 *This, HWND tab, STPFLAG flags)
{
    if ((flags & 0xC) == 0xC || (flags & 3) == 3)
        return E_INVALIDARG;
    if (!tab || !GetPropW(tab, TAB_PROPERTY) || !SetPropW(tab, L"ITaskList_TabProperties", (HANDLE)(ULONG_PTR)flags))
        return E_FAIL;
    return S_OK;
}

static const ITaskbarList4Vtbl g_vtbl = {
    {
        tl_query_interface, tl_add_ref, tl_release, tl_hr_init, tl_add_tab, tl_delete_tab, tl_nothing, tl_nothing,
        tl_mark_fullscreen, tl_progress_value, tl_progress_state, tl_register_tab, tl_unregister_tab, tl_tab_order,
        tl_tab_active, tl_buttons_add, tl_buttons_update, tl_button_images, tl_overlay, tl_tooltip, tl_clip,
    },
    tl_tab_properties,
};

/* ------------------------------------------------------------------ class */

static HRESULT STDMETHODCALLTYPE factory_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    TASKBAR_LIST *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*list));
    if (!list)
        return E_OUTOFMEMORY;
    list->Interface.lpVtbl = &g_vtbl;
    list->Refs = 1;
    HRESULT hr = tl_query_interface((ITaskbarList3 *)&list->Interface, riid, object);
    tl_release((ITaskbarList3 *)&list->Interface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};
static IClassFactory g_factory = {&g_factory_vtbl};

IClassFactory *TaskbarListFactory(void)
{
    return &g_factory;
}
