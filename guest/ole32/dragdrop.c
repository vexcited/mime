/*
 * drag and drop: per window drop targets and the modal DoDragDrop loop, which follows the mouse inside the window it
 * began in. a drag that leaves that window goes to the host, see hostdnd.c, which carries it to desktop programs and
 * other guest windows and reports how it ended.
 */
#include "ole32_private.h"

typedef struct _DROP_ENTRY {
    HWND Window;
    IDropTarget *Target;
} DROP_ENTRY;

static SRWLOCK g_drop_lock = SRWLOCK_INIT;
static DROP_ENTRY *g_drops;
static ULONG g_drop_count, g_drop_capacity;

static constexpr WCHAR g_target_prop[] = L"OleDropTargetInterface";

static LONG find_entry(HWND hwnd)
{
    for (ULONG i = 0; i < g_drop_count; i++)
        if (g_drops[i].Window == hwnd)
            return (LONG)i;
    return -1;
}

WINOLEAPI HRESULT WINAPI RegisterDragDrop(HWND hwnd, IDropTarget *target)
{
    if (!target)
        return E_INVALIDARG;
    if (!hwnd || !IsWindow(hwnd))
        return DRAGDROP_E_INVALIDHWND;
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t || !t->OleInits)
        return E_OUTOFMEMORY;
    DWORD process = 0;
    GetWindowThreadProcessId(hwnd, &process);
    if (process != GetCurrentProcessId())
        return DRAGDROP_E_INVALIDHWND;
    AcquireSRWLockExclusive(&g_drop_lock);
    if (find_entry(hwnd) >= 0) {
        ReleaseSRWLockExclusive(&g_drop_lock);
        return DRAGDROP_E_ALREADYREGISTERED;
    }
    if (g_drop_count == g_drop_capacity) {
        ULONG capacity = g_drop_capacity ? g_drop_capacity * 2 : 16;
        DROP_ENTRY *grown = g_drops ? HeapReAlloc(GetProcessHeap(), 0, g_drops, capacity * sizeof(*grown))
                                    : OleAlloc(capacity * sizeof(*grown));
        if (!grown) {
            ReleaseSRWLockExclusive(&g_drop_lock);
            return E_OUTOFMEMORY;
        }
        g_drops = grown;
        g_drop_capacity = capacity;
    }
    target->lpVtbl->AddRef(target);
    g_drops[g_drop_count++] = (DROP_ENTRY){hwnd, target};
    ReleaseSRWLockExclusive(&g_drop_lock);
    SetPropW(hwnd, g_target_prop, (HANDLE)target);
    /* drags from host programs reach the target through the thread's message loop. */
    OleHostDndRegister(hwnd);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI RevokeDragDrop(HWND hwnd)
{
    AcquireSRWLockExclusive(&g_drop_lock);
    LONG i = hwnd ? find_entry(hwnd) : -1;
    IDropTarget *target = nullptr;
    if (i >= 0) {
        target = g_drops[i].Target;
        g_drops[i] = g_drops[--g_drop_count];
    }
    ReleaseSRWLockExclusive(&g_drop_lock);
    if (!target)
        return hwnd && IsWindow(hwnd) ? DRAGDROP_E_NOTREGISTERED : DRAGDROP_E_INVALIDHWND;
    RemovePropW(hwnd, g_target_prop);
    OleHostDndRevoke(hwnd);
    target->lpVtbl->Release(target);
    return S_OK;
}

/* returns the referenced target of the window or of its nearest registered ancestor. */
IDropTarget *OleFindDropTarget(HWND hwnd)
{
    for (; hwnd; hwnd = GetAncestor(hwnd, GA_PARENT)) {
        AcquireSRWLockExclusive(&g_drop_lock);
        LONG i = find_entry(hwnd);
        IDropTarget *target = i >= 0 ? g_drops[i].Target : nullptr;
        if (target)
            target->lpVtbl->AddRef(target);
        ReleaseSRWLockExclusive(&g_drop_lock);
        if (target)
            return target;
    }
    return nullptr;
}


static BOOL key_down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static DWORD current_key_state(void)
{
    DWORD state = 0;
    if (key_down(VK_LBUTTON))
        state |= MK_LBUTTON;
    if (key_down(VK_RBUTTON))
        state |= MK_RBUTTON;
    if (key_down(VK_MBUTTON))
        state |= MK_MBUTTON;
    if (key_down(VK_SHIFT))
        state |= MK_SHIFT;
    if (key_down(VK_CONTROL))
        state |= MK_CONTROL;
    if (key_down(VK_MENU))
        state |= MK_ALT;
    return state;
}

/*
 * dispatches pending messages except mouse and keyboard input, which belongs
 * to the drag. returns FALSE on WM_QUIT, which is posted again.
 */
static BOOL pump_messages(BOOL *escape)
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            return FALSE;
        }
        if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) {
            if ((msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) && msg.wParam == VK_ESCAPE)
                *escape = TRUE;
            continue;
        }
        if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST)
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return TRUE;
}

static void set_feedback(IDropSource *source, DWORD effect)
{
    if (source->lpVtbl->GiveFeedback(source, effect) == DRAGDROP_S_USEDEFAULTCURSORS)
        SetCursor(LoadCursorW(nullptr, (effect & ~DROPEFFECT_SCROLL) ? IDC_ARROW : IDC_NO));
}

static BOOL cursor_left(HWND source_top)
{
    RECT rect;
    POINT cursor;
    if (OleHostPointerOutside())
        return TRUE;
    if (!source_top || !GetWindowRect(source_top, &rect))
        return FALSE;
    GetCursorPos(&cursor);
    return !PtInRect(&rect, cursor);
}

static HRESULT run_drag(IDataObject *data, IDropSource *source, DWORD ok_effects, DWORD *effect)
{
    IDropTarget *target = nullptr;
    HWND target_window = nullptr;
    DWORD current = DROPEFFECT_NONE;
    HRESULT hr;
    POINT start;
    GetCursorPos(&start);
    HWND source_top = WindowFromPoint(start);
    if (source_top)
        source_top = GetAncestor(source_top, GA_ROOT);
    bool host = false;
    for (;;) {
        BOOL escape = key_down(VK_ESCAPE);
        if (!pump_messages(&escape))
            escape = TRUE;
        DWORD keys = current_key_state();
        POINT cursor;
        GetCursorPos(&cursor);
        POINTL pt = {cursor.x, cursor.y};

        if (host) {
            /* the host runs the drag now, its end arrives through the messages pumped above. */
            bool dropped;
            DWORD host_effect;
            if (OleHostDragEnded(&dropped, &host_effect)) {
                source->lpVtbl->QueryContinueDrag(source, FALSE, keys & ~MK_LBUTTON);
                *effect = dropped ? host_effect & ok_effects : DROPEFFECT_NONE;
                hr = dropped ? DRAGDROP_S_DROP : DRAGDROP_S_CANCEL;
                OleHostDragFinish();
                return hr;
            }
            hr = source->lpVtbl->QueryContinueDrag(source, escape, keys);
            if (hr == DRAGDROP_S_CANCEL || FAILED(hr)) {
                OleHostDragFinish();
                return hr == S_OK ? DRAGDROP_S_CANCEL : hr;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
            continue;
        }

        hr = source->lpVtbl->QueryContinueDrag(source, escape, keys);
        if (hr != S_OK)
            break;

        /* once out of its window, the drag continues in the host if the host can take it. */
        if (cursor_left(source_top) && OleHostDragBegin(data, ok_effects)) {
            if (target) {
                target->lpVtbl->DragLeave(target);
                target->lpVtbl->Release(target);
                target = nullptr;
            }
            host = true;
            set_feedback(source, DROPEFFECT_NONE);
            continue;
        }

        HWND window = WindowFromPoint(cursor);
        if (window != target_window) {
            IDropTarget *next = window ? OleFindDropTarget(window) : nullptr;
            if (next != target) {
                if (target) {
                    target->lpVtbl->DragLeave(target);
                    target->lpVtbl->Release(target);
                }
                target = next;
                current = ok_effects;
                if (target && FAILED(target->lpVtbl->DragEnter(target, data, keys, pt, &current)))
                    current = DROPEFFECT_NONE;
            } else if (next) {
                next->lpVtbl->Release(next);
            }
            target_window = window;
        } else if (target) {
            current = ok_effects;
            if (FAILED(target->lpVtbl->DragOver(target, keys, pt, &current)))
                current = DROPEFFECT_NONE;
        }
        if (!target)
            current = DROPEFFECT_NONE;
        current &= ok_effects | DROPEFFECT_SCROLL;
        set_feedback(source, current);
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }

    if (hr == DRAGDROP_S_DROP && target && (current & ~DROPEFFECT_SCROLL)) {
        POINT cursor;
        GetCursorPos(&cursor);
        POINTL pt = {cursor.x, cursor.y};
        DWORD dropped = ok_effects;
        if (FAILED(target->lpVtbl->Drop(target, data, current_key_state(), pt, &dropped)))
            dropped = DROPEFFECT_NONE;
        *effect = dropped & ok_effects;
    } else {
        if (target)
            target->lpVtbl->DragLeave(target);
        if (hr == DRAGDROP_S_DROP)
            hr = DRAGDROP_S_CANCEL;
    }
    if (target)
        target->lpVtbl->Release(target);
    return hr;
}

WINOLEAPI HRESULT WINAPI DoDragDrop(IDataObject *data, IDropSource *source, DWORD ok_effects, DWORD *effect)
{
    if (!data || !source || !effect)
        return E_INVALIDARG;
    *effect = DROPEFFECT_NONE;
    /*
     * Windows takes the capture for the drag. the control that started it learns
     * through WM_CAPTURECHANGED that its own mouse tracking is over.
     */
    HWND capture = OleDragWindow();
    if (capture)
        SetCapture(capture);
    HRESULT hr = run_drag(data, source, ok_effects, effect);
    if (capture && GetCapture() == capture)
        ReleaseCapture();
    return hr;
}
