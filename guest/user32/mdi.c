/*
 * multiple document interface: the MDICLIENT class that holds the document windows of a frame, the default procedures of
 * the frame and of the documents, and the windows that make one. A document is a child window of the client. The client
 * keeps the active one, the one that fills it when maximized, the list of them in the window menu and the arranging.
 */
#include "user32_private.h"

#define WM_MDIDESTROY 0x0221
#define WM_MDIACTIVATE 0x0222
#define WM_MDIRESTORE 0x0223
#define WM_MDINEXT 0x0224
#define WM_MDIMAXIMIZE 0x0225
#define WM_MDITILE 0x0226
#define WM_MDICASCADE 0x0227
#define WM_MDIICONARRANGE 0x0228
#define WM_MDISETMENU 0x0230
#define WM_MDIREFRESHMENU 0x0234
#define MDIS_ALLCHILDSTYLES 0x0001
#ifndef MDITILE_VERTICAL
#define MDITILE_VERTICAL 0x0000
#define MDITILE_HORIZONTAL 0x0001
#endif
#define MDI_LISTED 9
#define MDI_ITEM_MORE 9

typedef struct CLIENT_CREATE {
    HANDLE WindowMenu;
    UINT FirstChild;
} CLIENT_CREATE;

typedef struct MDI_CLIENT {
    HWND Active;
    UINT FirstChild, Created;
    HMENU WindowMenu;
} MDI_CLIENT;

static MDI_CLIENT *client_state(HWND client)
{
    return (MDI_CLIENT *)GetWindowLongPtrW(client, 0);
}

static BOOL is_client(HWND hwnd)
{
    WCHAR name[16];
    return hwnd && GetClassNameW(hwnd, name, RTL_NUMBER_OF(name)) && !_wcsicmp(name, L"MDIClient");
}

/* the documents are the children that are not the icon titles of the client. */
static int documents(HWND client, HWND *list, int capacity)
{
    int count = 0;
    for (HWND child = GetWindow(client, GW_CHILD); child && count < capacity; child = GetWindow(child, GW_HWNDNEXT))
        if (GetWindow(child, GW_OWNER) == nullptr)
            list[count++] = child;
    return count;
}

/* the documents in the order they were made, which their identifiers keep. */
static int documents_in_order(HWND client, HWND *list, int capacity)
{
    int count = documents(client, list, capacity);
    for (int i = 1; i < count; i++)
        for (int j = i; j > 0 && GetWindowLongW(list[j], GWL_ID) < GetWindowLongW(list[j - 1], GWL_ID); j--) {
            HWND swap = list[j];
            list[j] = list[j - 1];
            list[j - 1] = swap;
        }
    return count;
}

static HWND frame_of(HWND client)
{
    return GetParent(client);
}

/* ------------------------------------------------------------------ the window menu */

static void refresh_window_menu(HWND client)
{
    MDI_CLIENT *state = client_state(client);
    if (!state || !state->WindowMenu)
        return;
    HMENU menu = state->WindowMenu;
    for (UINT i = 0; i <= MDI_LISTED; i++)
        DeleteMenu(menu, state->FirstChild + i, MF_BYCOMMAND);
    /* the separator that precedes the list is the item before the first of them, which is left when it is one. */
    HWND list[64];
    int count = documents_in_order(client, list, RTL_NUMBER_OF(list));
    if (!count)
        return;
    int items = GetMenuItemCount(menu);
    if (items > 0 && GetMenuItemID(menu, items - 1) != 0)
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < count && i < MDI_LISTED; i++) {
        WCHAR title[128], text[160];
        GetWindowTextW(list[i], title, RTL_NUMBER_OF(title));
        _snwprintf(text, RTL_NUMBER_OF(text), L"&%d %s", i + 1, title);
        AppendMenuW(menu, MF_STRING | (list[i] == state->Active ? MF_CHECKED : 0), state->FirstChild + i, text);
    }
    if (count > MDI_LISTED)
        AppendMenuW(menu, MF_STRING, state->FirstChild + MDI_ITEM_MORE, L"&More Windows...");
    HWND frame = frame_of(client);
    if (frame)
        DrawMenuBar(frame);
}

/* ------------------------------------------------------------------ the activation */

static void maximized_fill(HWND client, HWND child)
{
    ShowWindow(child, SW_SHOWMAXIMIZED);
}

static void activate(HWND client, HWND child)
{
    MDI_CLIENT *state = client_state(client);
    if (!state || child == state->Active)
        return;
    HWND old = state->Active;
    state->Active = child;
    if (child)
        SetWindowPos(child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (old && IsWindow(old))
        SendMessageW(old, WM_MDIACTIVATE, (WPARAM)old, (LPARAM)child);
    if (child) {
        SendMessageW(child, WM_MDIACTIVATE, (WPARAM)old, (LPARAM)child);
        if (!(GetWindowLongW(child, GWL_STYLE) & WS_VISIBLE))
            ShowWindow(child, SW_SHOWNORMAL);
    }
    refresh_window_menu(client);
}

static void activate_next(HWND client, HWND from, BOOL previous)
{
    HWND list[64];
    int count = documents(client, list, RTL_NUMBER_OF(list));
    if (count < 2)
        return;
    int at = 0;
    for (int i = 0; i < count; i++)
        if (list[i] == from)
            at = i;
    /* the next is the one below in the order of the z axis, the previous is the one at the bottom. */
    HWND target = previous ? list[count - 1] : list[(at + 1) % count];
    activate(client, target);
}

/* ------------------------------------------------------------------ states of a document */

static void maximize(HWND client, HWND child)
{
    if (!(GetWindowLongW(child, GWL_STYLE) & WS_MAXIMIZE))
        ShowWindow(child, SW_SHOWMAXIMIZED);
    activate(client, child);
}

static void restore(HWND client, HWND child)
{
    if (GetWindowLongW(child, GWL_STYLE) & (WS_MAXIMIZE | WS_MINIMIZE))
        ShowWindow(child, SW_RESTORE);
    activate(client, child);
}

/* ------------------------------------------------------------------ the creation of a document */

static HWND create_document(HWND client, DWORD ex_style, LPCWSTR class_name, LPCWSTR title, DWORD style, int x, int y,
                            int cx, int cy, HANDLE owner, LPARAM param, MDICREATESTRUCTW *details)
{
    MDI_CLIENT *state = client_state(client);
    if (!state)
        return nullptr;
    DWORD client_style = (DWORD)GetWindowLongW(client, GWL_STYLE);
    if (!(client_style & MDIS_ALLCHILDSTYLES))
        style = (style & (WS_MINIMIZE | WS_MAXIMIZE | WS_HSCROLL | WS_VSCROLL)) | WS_SYSMENU | WS_CAPTION | WS_THICKFRAME |
                WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE;
    style |= WS_CHILD | WS_CLIPSIBLINGS;
    /* a document with no place is put in a cascade, one step further for each of the ones there are. */
    RECT area;
    GetClientRect(client, &area);
    int step = (GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME)) * (int)(state->Created % 8);
    if (x == CW_USEDEFAULT)
        x = step;
    if (y == CW_USEDEFAULT)
        y = step;
    if (cx == CW_USEDEFAULT)
        cx = (area.right - area.left) * 3 / 4;
    if (cy == CW_USEDEFAULT)
        cy = (area.bottom - area.top) * 3 / 4;
    UINT id = state->FirstChild + MDI_LISTED + state->Created;
    state->Created++;
    MDICREATESTRUCTW created = details ? *details : (MDICREATESTRUCTW){class_name, title, owner, x, y, cx, cy, style, param};
    created.x = x;
    created.y = y;
    created.cx = cx;
    created.cy = cy;
    HWND child = UserCreateWindowRaw(ex_style | WS_EX_MDICHILD, class_name, title, style, x, y, cx, cy, client,
                                     (HMENU)(UINT_PTR)id, (HINSTANCE)owner, &created);
    if (!child)
        return nullptr;
    if (style & WS_MAXIMIZE)
        maximize(client, child);
    activate(client, child);
    return child;
}

BOOL UserMdiCreate(HWND *result, DWORD ex_style, LPCWSTR class_name, LPCWSTR name, DWORD style, int x, int y, int width,
                   int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
    if (!parent || !is_client(parent))
        return FALSE;
    *result = create_document(parent, ex_style, class_name, name, style, x, y, width, height, instance, (LPARAM)param,
                              nullptr);
    return TRUE;
}

/* ------------------------------------------------------------------ the arrangement */

static void tile_or_cascade(HWND client, BOOL tile, UINT how)
{
    HWND list[64];
    int count = documents(client, list, RTL_NUMBER_OF(list)), shown = 0;
    HWND visible[64];
    for (int i = 0; i < count; i++) {
        DWORD style = (DWORD)GetWindowLongW(list[i], GWL_STYLE);
        if ((style & WS_VISIBLE) && !(style & WS_MINIMIZE)) {
            if (style & WS_MAXIMIZE)
                restore(client, list[i]);
            visible[shown++] = list[i];
        }
    }
    if (!shown)
        return;
    RECT area;
    GetClientRect(client, &area);
    int width = area.right - area.left, height = area.bottom - area.top;
    if (tile) {
        int columns = 1, rows;
        while (columns * columns < shown)
            columns++;
        rows = (shown + columns - 1) / columns;
        /* a horizontal tile has the long side across. */
        if (how & MDITILE_HORIZONTAL) {
            int swap = columns;
            columns = rows;
            rows = swap;
        }
        int cx = width / columns, cy = height / rows;
        for (int i = 0; i < shown; i++)
            SetWindowPos(visible[i], nullptr, (i % columns) * cx, (i / columns) * cy, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        int step = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME);
        for (int i = 0; i < shown; i++) {
            int k = shown - 1 - i;
            SetWindowPos(visible[i], nullptr, step * k, step * k, width * 3 / 4, height * 3 / 4, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

/* ------------------------------------------------------------------ the client window */

LRESULT CALLBACK MdiClientProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    MDI_CLIENT *state = client_state(hwnd);
    switch (msg) {
    case WM_NCCREATE:
        return TRUE;
    case WM_CREATE: {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
        if (!state)
            return -1;
        const CLIENT_CREATE *details = create->lpCreateParams;
        if (details) {
            state->WindowMenu = (HMENU)details->WindowMenu;
            state->FirstChild = details->FirstChild;
        }
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)state);
        return 0;
    }
    case WM_NCDESTROY:
        if (state) {
            HeapFree(GetProcessHeap(), 0, state);
            SetWindowLongPtrW(hwnd, 0, 0);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND: {
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect((HDC)wparam, &rect, GetSysColorBrush(COLOR_APPWORKSPACE));
        return 1;
    }
    case WM_SIZE:
        if (state && state->Active && (GetWindowLongW(state->Active, GWL_STYLE) & WS_MAXIMIZE))
            maximized_fill(hwnd, state->Active);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SETFOCUS:
        if (state && state->Active && IsWindow(state->Active))
            SetFocus(state->Active);
        return 0;
    case WM_MDICREATE:
        if (!lparam)
            return 0;
        {
            MDICREATESTRUCTW *details = (MDICREATESTRUCTW *)lparam;
            return (LRESULT)create_document(hwnd, 0, details->szClass, details->szTitle, details->style, details->x, details->y,
                                            details->cx, details->cy, details->hOwner, details->lParam, details);
        }
    case WM_MDIDESTROY: {
        HWND child = (HWND)wparam;
        if (!state || !IsWindow(child) || GetParent(child) != hwnd)
            return 0;
        if (state->Active == child)
            activate_next(hwnd, child, FALSE);
        if (state->Active == child)
            state->Active = nullptr;
        DestroyWindow(child);
        refresh_window_menu(hwnd);
        return 0;
    }
    case WM_MDIACTIVATE:
        if (state && IsWindow((HWND)wparam) && GetParent((HWND)wparam) == hwnd)
            activate(hwnd, (HWND)wparam);
        return 0;
    case WM_MDIRESTORE:
        if (state && IsWindow((HWND)wparam))
            restore(hwnd, (HWND)wparam);
        return 0;
    case WM_MDINEXT:
        if (state)
            activate_next(hwnd, wparam ? (HWND)wparam : state->Active, lparam != 0);
        return 0;
    case WM_MDIMAXIMIZE:
        if (state && IsWindow((HWND)wparam))
            maximize(hwnd, (HWND)wparam);
        return 0;
    case WM_MDITILE:
        tile_or_cascade(hwnd, TRUE, (UINT)wparam);
        return 1;
    case WM_MDICASCADE:
        tile_or_cascade(hwnd, FALSE, (UINT)wparam);
        return 1;
    case WM_MDIICONARRANGE:
        ArrangeIconicWindows(hwnd);
        return 0;
    case WM_MDIGETACTIVE:
        if (lparam)
            *(BOOL *)lparam = state && state->Active && (GetWindowLongW(state->Active, GWL_STYLE) & WS_MAXIMIZE) ? TRUE : FALSE;
        return (LRESULT)(state ? state->Active : nullptr);
    case WM_MDISETMENU: {
        HMENU previous = nullptr;
        HWND frame = frame_of(hwnd);
        if (!state)
            return 0;
        if (wparam && frame) {
            previous = GetMenu(frame);
            SetMenu(frame, (HMENU)wparam);
        }
        if (lparam)
            state->WindowMenu = (HMENU)lparam;
        refresh_window_menu(hwnd);
        if (frame)
            DrawMenuBar(frame);
        return (LRESULT)previous;
    }
    case WM_MDIREFRESHMENU:
        refresh_window_menu(hwnd);
        return (LRESULT)(state ? GetMenu(frame_of(hwnd)) : nullptr);
    case WM_PARENTNOTIFY:
        if (state && LOWORD(wparam) == WM_LBUTTONDOWN) {
            POINT point = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
            HWND hit = ChildWindowFromPoint(hwnd, point);
            if (hit && hit != hwnd)
                activate(hwnd, hit);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void UserRegisterMdiClient(void)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_GLOBALCLASS;
    wc.lpfnWndProc = MdiClientProc;
    wc.cbWndExtra = sizeof(void *);
    wc.hInstance = User32Instance;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"MDIClient";
    RegisterClassExW(&wc);
}

/* ------------------------------------------------------------------ the default procedures */

static LRESULT frame_proc(HWND hwnd, HWND client, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    if (client && IsWindow(client) && is_client(client)) {
        MDI_CLIENT *state = client_state(client);
        switch (msg) {
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED)
                MoveWindow(client, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
            return 0;
        case WM_SETFOCUS:
            SetFocus(client);
            return 0;
        case WM_COMMAND:
            if (state && LOWORD(wparam) >= state->FirstChild && LOWORD(wparam) < state->FirstChild + MDI_LISTED) {
                HWND list[64];
                int count = documents_in_order(client, list, RTL_NUMBER_OF(list));
                int index = LOWORD(wparam) - (int)state->FirstChild;
                if (index < count)
                    SendMessageW(client, WM_MDIACTIVATE, (WPARAM)list[index], 0);
                return 0;
            }
            break;
        case WM_MDICREATE:
        case WM_MDIDESTROY:
        case WM_MDIACTIVATE:
        case WM_MDIRESTORE:
        case WM_MDINEXT:
        case WM_MDIMAXIMIZE:
        case WM_MDITILE:
        case WM_MDICASCADE:
        case WM_MDIICONARRANGE:
        case WM_MDIGETACTIVE:
        case WM_MDISETMENU:
        case WM_MDIREFRESHMENU:
            return ansi ? SendMessageA(client, msg, wparam, lparam) : SendMessageW(client, msg, wparam, lparam);
        case WM_NCACTIVATE:
            break;
        }
    }
    return ansi ? DefWindowProcA(hwnd, msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT child_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    HWND client = GetParent(hwnd);
    BOOL in_client = is_client(client);
    switch (msg) {
    case WM_CHILDACTIVATE:
        if (in_client)
            SendMessageW(client, WM_MDIACTIVATE, (WPARAM)hwnd, 0);
        return 0;
    case WM_MDIACTIVATE:
        /* both the window that is left and the one that is entered are told, and each shows which it is. */
        DefWindowProcW(hwnd, WM_NCACTIVATE, hwnd == (HWND)lparam, 0);
        if (hwnd == (HWND)lparam)
            SetFocus(hwnd);
        return 0;
    case WM_NCACTIVATE:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SETFOCUS:
        if (in_client) {
            MDI_CLIENT *state = client_state(client);
            if (state && state->Active != hwnd)
                SendMessageW(client, WM_MDIACTIVATE, (WPARAM)hwnd, 0);
        }
        break;
    case WM_SYSCOMMAND:
        if (in_client) {
            switch (wparam & 0xFFF0) {
            case SC_NEXTWINDOW:
                SendMessageW(client, WM_MDINEXT, (WPARAM)hwnd, 0);
                return 0;
            case SC_PREVWINDOW:
                SendMessageW(client, WM_MDINEXT, (WPARAM)hwnd, 1);
                return 0;
            case SC_MAXIMIZE:
                SendMessageW(client, WM_MDIMAXIMIZE, (WPARAM)hwnd, 0);
                return 0;
            case SC_RESTORE:
                SendMessageW(client, WM_MDIRESTORE, (WPARAM)hwnd, 0);
                return 0;
            }
        }
        break;
    case WM_GETMINMAXINFO:
        /* a document that is maximized has the client of the frame inside its borders. */
        if (in_client && lparam) {
            MINMAXINFO *info = (MINMAXINFO *)lparam;
            RECT area, outer;
            GetClientRect(client, &area);
            outer = area;
            AdjustWindowRectEx(&outer, (DWORD)GetWindowLongW(hwnd, GWL_STYLE) & ~(DWORD)WS_MAXIMIZE,
                               FALSE, (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE));
            info->ptMaxPosition.x = outer.left;
            info->ptMaxPosition.y = outer.top;
            info->ptMaxSize.x = outer.right - outer.left;
            info->ptMaxSize.y = outer.bottom - outer.top;
            info->ptMaxTrackSize = info->ptMaxSize;
            return 0;
        }
        break;
    case WM_MENUCHAR: {
        HWND frame = in_client ? frame_of(client) : nullptr;
        if (frame)
            return SendMessageW(frame, msg, wparam, lparam);
        break;
    }
    case WM_SETTEXT: {
        LRESULT result = ansi ? DefWindowProcA(hwnd, msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
        if (in_client)
            refresh_window_menu(client);
        return result;
    }
    case WM_DESTROY:
        if (in_client) {
            MDI_CLIENT *state = client_state(client);
            if (state && state->Active == hwnd)
                state->Active = nullptr;
            refresh_window_menu(client);
        }
        break;
    }
    return ansi ? DefWindowProcA(hwnd, msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
}

WINUSERAPI LRESULT WINAPI DefFrameProcW(HWND hwnd, HWND client, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return frame_proc(hwnd, client, msg, wparam, lparam, FALSE);
}

WINUSERAPI LRESULT WINAPI DefFrameProcA(HWND hwnd, HWND client, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return frame_proc(hwnd, client, msg, wparam, lparam, TRUE);
}

WINUSERAPI LRESULT WINAPI DefMDIChildProcW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return child_proc(hwnd, msg, wparam, lparam, FALSE);
}

WINUSERAPI LRESULT WINAPI DefMDIChildProcA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return child_proc(hwnd, msg, wparam, lparam, TRUE);
}

WINUSERAPI HWND WINAPI CreateMDIWindowW(LPCWSTR class_name, LPCWSTR title, DWORD style, int x, int y, int width,
                                        int height, HWND parent, HINSTANCE instance, LPARAM param)
{
    if (!is_client(parent)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    MDICREATESTRUCTW details = {class_name, title, instance, x, y, width, height, style, param};
    return (HWND)SendMessageW(parent, WM_MDICREATE, 0, (LPARAM)&details);
}

WINUSERAPI HWND WINAPI CreateMDIWindowA(LPCSTR class_name, LPCSTR title, DWORD style, int x, int y, int width,
                                        int height, HWND parent, HINSTANCE instance, LPARAM param)
{
    if (!is_client(parent)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    MDICREATESTRUCTA details = {class_name, title, instance, x, y, width, height, style, param};
    return (HWND)SendMessageA(parent, WM_MDICREATE, 0, (LPARAM)&details);
}

/* control-F4 closes the active document, and control-F6 and control-tab go to the next one. */
WINUSERAPI BOOL WINAPI TranslateMDISysAccel(HWND client, LPMSG msg)
{
    if (!msg || msg->message != WM_KEYDOWN || !is_client(client) || !(GetKeyState(VK_CONTROL) & 0x8000))
        return FALSE;
    MDI_CLIENT *state = client_state(client);
    if (!state || !state->Active)
        return FALSE;
    if (msg->wParam == VK_F4) {
        SendMessageW(state->Active, WM_SYSCOMMAND, SC_CLOSE, 0);
        return TRUE;
    }
    if (msg->wParam == VK_F6 || msg->wParam == VK_TAB) {
        SendMessageW(client, WM_MDINEXT, (WPARAM)state->Active, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        return TRUE;
    }
    return FALSE;
}
