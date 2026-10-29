/* drag lists: a list box whose items the user drags, and the insertion arrow that shows where one would drop. */
#include "comctl32_private.h"

#define DL_BEGINDRAG 1157
#define DL_DRAGGING 1158
#define DL_DROPPED 1159
#define DL_CANCELDRAG 1160
#define DL_STOPCURSOR 1
#define DL_COPYCURSOR 2
#define DL_MOVECURSOR 3

#define DRAG_TIMER 0xEE

typedef struct DRAGLISTINFO_ {
    UINT Notification;
    HWND Window;
    POINT Cursor;
} DRAGLISTINFO_;

/* the window that has the mouse while dragging, and whether the drag was accepted and has left its item. */
typedef struct DRAG_LIST {
    HWND Capture;
    UINT State;
} DRAG_LIST;

enum { DRAG_STARTED = 1, DRAG_ON_FIRST = 2 };

static UINT g_drag_message;

static LRESULT notify(HWND list, UINT code, POINT where)
{
    DRAGLISTINFO_ info = {code, list, where};
    ClientToScreen(list, &info.Cursor);
    return SendMessageW(GetParent(list), g_drag_message, (WPARAM)GetDlgCtrlID(list), (LPARAM)&info);
}

/* the cursor of the answer, the system's where comctl32 has none of its own. */
static void answer_cursor(LRESULT answer)
{
    LPCWSTR name = IDC_ARROW;
    switch (answer) {
    case DL_STOPCURSOR:
        name = IDC_NO;
        break;
    case DL_COPYCURSOR:
    case DL_MOVECURSOR:
        name = IDC_SIZEALL;
        break;
    default:
        return;
    }
    SetCursor(LoadCursorW(nullptr, name));
}

static BOOL on_item(HWND list, POINT where)
{
    RECT item = {};
    int top = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (SendMessageW(list, LB_GETITEMRECT, (WPARAM)top, (LPARAM)&item) == LB_ERR)
        return FALSE;
    InflateRect(&item, 0, 4);
    return PtInRect(&item, where);
}

static POINT message_position(HWND list)
{
    DWORD at = GetMessagePos();
    POINT where = {(short)LOWORD(at), (short)HIWORD(at)};
    ScreenToClient(list, &where);
    return where;
}

static LRESULT CALLBACK drag_proc(HWND list, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    DRAG_LIST *drag = (DRAG_LIST *)data;
    POINT where = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
    switch (message) {
    case WM_LBUTTONDOWN: {
        if (drag->Capture == list)
            SendMessageW(list, WM_RBUTTONDOWN, 0, 0);
        SetFocus(list);
        POINT screen = where;
        ClientToScreen(list, &screen);
        int item = LBItemFromPt(list, screen, FALSE);
        drag->State = 0;
        if (item >= 0) {
            SendMessageW(list, LB_SETCURSEL, (WPARAM)item, 0);
            if (GetWindowLongW(list, GWL_STYLE) & LBS_NOTIFY)
                SendMessageW(GetParent(list), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(list), LBN_SELCHANGE), (LPARAM)list);
            if (notify(list, DL_BEGINDRAG, where)) {
                SetTimer(list, DRAG_TIMER, 50, nullptr);
                drag->State = DRAG_STARTED | DRAG_ON_FIRST;
            }
        }
        drag->Capture = list;
        SetCapture(list);
        return 0;
    }
    case WM_TIMER:
        if (wparam != DRAG_TIMER)
            break;
        where = message_position(list);
        /* the timer moves the drag when the mouse is still, as the mouse moves it. */
        [[fallthrough]];
    case WM_MOUSEMOVE: {
        if (drag->Capture != list)
            break;
        if (!(drag->State & DRAG_STARTED))
            return 0;
        if (drag->State & DRAG_ON_FIRST) {
            if (on_item(list, where))
                return 0;
            drag->State &= ~(UINT)DRAG_ON_FIRST;
        }
        answer_cursor(notify(list, DL_DRAGGING, where));
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
        if (drag->Capture != list)
            break;
        drag->Capture = nullptr;
        KillTimer(list, DRAG_TIMER);
        ReleaseCapture();
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        notify(list, message == WM_LBUTTONUP ? DL_DROPPED : DL_CANCELDRAG, where);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE)
            SendMessageW(list, WM_RBUTTONDOWN, 0, 0);
        [[fallthrough]];
    case WM_KEYUP:
    case WM_CHAR:
        if (drag->Capture == list)
            return 0;
        break;
    case WM_GETDLGCODE:
        if (drag->Capture == list)
            return DefSubclassProc(list, message, wparam, lparam) | DLGC_WANTALLKEYS;
        break;
    case WM_NCDESTROY:
        if (drag->Capture == list)
            SendMessageW(list, WM_RBUTTONDOWN, 0, 0);
        RemoveWindowSubclass(list, drag_proc, 0);
        HeapFree(GetProcessHeap(), 0, drag);
        break;
    }
    return DefSubclassProc(list, message, wparam, lparam);
}

/* the list box tells its parent, with a registered message, where the drag is. */
WINCOMMCTRLAPI BOOL WINAPI MakeDragList(HWND list)
{
    if (!g_drag_message)
        g_drag_message = RegisterWindowMessageW(L"commctrl_DragListMsg");
    DWORD_PTR existing;
    if (GetWindowSubclass(list, drag_proc, 0, &existing))
        return TRUE;
    DRAG_LIST *drag = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*drag));
    if (!drag)
        return FALSE;
    if (!SetWindowSubclass(list, drag_proc, 0, (DWORD_PTR)drag)) {
        HeapFree(GetProcessHeap(), 0, drag);
        return FALSE;
    }
    return TRUE;
}

/*
 * the item under a point of the screen, or -1. with auto scroll the list scrolls by an item every so often while the
 * point is above or below it.
 */
WINCOMMCTRLAPI int WINAPI LBItemFromPt(HWND list, POINT point, BOOL scroll)
{
    static DWORD last;
    RECT client = {};
    ScreenToClient(list, &point);
    GetClientRect(list, &client);
    int top = (int)SendMessageW(list, LB_GETTOPINDEX, 0, 0);
    if (PtInRect(&client, point)) {
        RECT item = {};
        for (int i = top; SendMessageW(list, LB_GETITEMRECT, (WPARAM)i, (LPARAM)&item) != LB_ERR; i++)
            if (PtInRect(&item, point))
                return i;
        return -1;
    }
    if (scroll && (UINT)point.x < (UINT)client.right) {
        int step = 1;
        int distance = point.y;
        if (point.y > 0) {
            distance = client.bottom - point.y;
        } else {
            step = -1;
        }
        int interval = 1000 / (6 - distance / 25);
        DWORD now = GetTickCount();
        if ((WORD)(now - last) > (WORD)interval) {
            if ((WORD)(now - last) <= 2 * (UINT)(WORD)interval)
                now = (WORD)interval + last;
            last = now;
            SendMessageW(list, LB_SETTOPINDEX, (WPARAM)(top + step), 0);
        }
    }
    return -1;
}

/* the arrow that shows the drop position is drawn in the parent, at the left of the item, and erased by invalidation. */
WINCOMMCTRLAPI void WINAPI DrawInsert(HWND parent, HWND list, int item)
{
    static int shown = -1;
    static POINT at;
    int size = MulDiv(16, (int)GetDpiForSystem(), 96);
    if (shown >= 0) {
        if (item == shown)
            return;
        RECT old = {at.x, at.y, at.x + size, at.y + size};
        InvalidateRect(parent, &old, TRUE);
        UpdateWindow(parent);
        shown = -1;
    }
    if (item < 0)
        return;
    RECT rect = {};
    GetWindowRect(list, &rect);
    ScreenToClient(list, (LPPOINT)&rect);
    at.x = rect.left - size;
    SendMessageW(list, LB_GETITEMRECT, (WPARAM)item, (LPARAM)&rect);
    shown = item;
    at.y = rect.top - size / 2;
    ClientToScreen(list, &at);
    ScreenToClient(parent, &at);
    HDC dc = GetDC(parent);
    if (dc) {
        POINT arrow[7] = {{at.x, at.y + size / 3},
                          {at.x + size / 2, at.y + size / 3},
                          {at.x + size / 2, at.y},
                          {at.x + size, at.y + size / 2},
                          {at.x + size / 2, at.y + size},
                          {at.x + size / 2, at.y + 2 * size / 3},
                          {at.x, at.y + 2 * size / 3}};
        HGDIOBJ brush = SelectObject(dc, GetStockObject(BLACK_BRUSH));
        Polygon(dc, arrow, 7);
        SelectObject(dc, brush);
        ReleaseDC(parent, dc);
    }
}
