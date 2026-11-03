/*
 * the window of a console: its frame, what it draws from the cells the console keeps, the keyboard and the mouse, the
 * scroll bar, the selection and the clipboard.
 */
#include "conhost.h"

#define TIMER_BLINK 1
#define TIMER_SCROLL 2

#define IDM_COPY 0x0100
#define IDM_PASTE 0x0110
#define IDM_SELECT_ALL 0x0120
#define IDM_PROPERTIES 0x0130

#define WINDOW_STYLE (WS_OVERLAPPEDWINDOW | WS_VSCROLL)
#define KEYDOWN_FLAGS 0x0

#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(w) ((short)HIWORD(w))
#endif
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(l) ((int)(short)LOWORD(l))
#define GET_Y_LPARAM(l) ((int)(short)HIWORD(l))
#endif

static int g_wheel_remainder;
static HMENU g_edit_menu;
static ULONG g_last_click_time;
static POINT g_last_click;
static int g_click_count;
static WCHAR g_last_char[256]; /* the character of the last key down of each virtual key, which its release repeats */
static BOOL g_swallowed[256];  /* the keys the window used itself, whose release is not the program's either */

/* ------------------------------------------------------------------ geometry */

static int metric(int index)
{
    return GetSystemMetricsForDpi(index, (UINT)g.Dpi);
}

static BOOL has_horizontal_bar(void)
{
    return g.State.Size.X > g.State.Window.Right - g.State.Window.Left + 1;
}

/* the size of a window whose client area holds columns by rows cells. */
static void frame_size(int columns, int rows, int *width, int *height)
{
    RECT r = {0, 0, columns * g.CellWidth, rows * g.CellHeight};
    AdjustWindowRectExForDpi(&r, WINDOW_STYLE & ~WS_VSCROLL, FALSE, 0, (UINT)g.Dpi);
    *width = r.right - r.left + metric(SM_CXVSCROLL);
    *height = r.bottom - r.top + (has_horizontal_bar() ? metric(SM_CYHSCROLL) : 0);
}

static void size_frame(int columns, int rows)
{
    int w, h;
    frame_size(columns, rows, &w, &h);
    RECT now;
    GetWindowRect(g.Window, &now);
    if (now.right - now.left == w && now.bottom - now.top == h)
        return;
    BOOL was = g.Sizing;
    g.Sizing = TRUE;
    SetWindowPos(g.Window, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    g.Sizing = was;
}

static int window_columns(void)
{
    return g.State.Window.Right - g.State.Window.Left + 1;
}

static int window_rows(void)
{
    return g.State.Window.Bottom - g.State.Window.Top + 1;
}

/* ------------------------------------------------------------------ the picture of the cells */

static COLORREF default_background(void)
{
    return g.State.ColorTable[(g.State.Attributes >> 4) & 15];
}

static void back_resize(int columns, int rows)
{
    int w = columns * g.CellWidth, h = rows * g.CellHeight;
    if (g.BackDc && w == g.BackWidth && h == g.BackHeight)
        return;
    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
    if (!g.BackDc) {
        g.BackDc = CreateCompatibleDC(screen);
        SetBkMode(g.BackDc, OPAQUE);
    }
    ReleaseDC(nullptr, screen);
    if (!bitmap)
        return;
    HBITMAP old = (HBITMAP)SelectObject(g.BackDc, bitmap);
    if (g.BackBitmap)
        DeleteObject(g.BackBitmap);
    else
        g.BackOld = old;
    g.BackBitmap = bitmap;
    g.BackWidth = w;
    g.BackHeight = h;
    RECT all = {0, 0, w, h};
    HBRUSH brush = CreateSolidBrush(default_background());
    FillRect(g.BackDc, &all, brush);
    DeleteObject(brush);
}

static BOOL cell_selected(int column, int row)
{
    if (!g.Select.Active)
        return FALSE;
    int x = g.ViewLeft + column, y = g.ViewTop + row;
    POINT a = g.Select.Anchor, b = g.Select.End;
    if (g.Select.Block)
        return x >= min(a.x, b.x) && x <= max(a.x, b.x) && y >= min(a.y, b.y) && y <= max(a.y, b.y);
    if (a.y > b.y || (a.y == b.y && a.x > b.x)) {
        POINT t = a;
        a = b;
        b = t;
    }
    if (y < a.y || y > b.y)
        return FALSE;
    if (y == a.y && x < a.x)
        return FALSE;
    if (y == b.y && x > b.x)
        return FALSE;
    return TRUE;
}

static COLORREF faint_color(COLORREF fg, COLORREF bg)
{
    int r = (GetRValue(fg) * 2 + GetRValue(bg)) / 3, gr = (GetGValue(fg) * 2 + GetGValue(bg)) / 3,
        b = (GetBValue(fg) * 2 + GetBValue(bg)) / 3;
    return RGB(r, gr, b);
}

#define RUN_STYLE_MASK                                                                                              \
    (MIME_CONDRV_CELL_BOLD | MIME_CONDRV_CELL_ITALIC | MIME_CONDRV_CELL_STRIKE | MIME_CONDRV_CELL_CONCEAL |         \
     MIME_CONDRV_CELL_UNDERLINE_MASK | MIME_CONDRV_CELL_OVERLINE | MIME_CONDRV_CELL_UNDERSCORE | 0x1800)

static void cell_colors(const Cell *c, BOOL selected, COLORREF *fg, COLORREF *bg)
{
    COLORREF f = c->Foreground, b = c->Background;
    if (c->Style & MIME_CONDRV_CELL_REVERSE) {
        COLORREF t = f;
        f = b;
        b = t;
    }
    if (c->Style & MIME_CONDRV_CELL_FAINT)
        f = faint_color(f, b);
    if (selected) {
        COLORREF t = f;
        f = b;
        b = t;
    }
    *fg = f;
    *bg = b;
}

static void render_row(int row)
{
    if (!g.BackDc || row < 0 || row >= g.ViewRows)
        return;
    const Cell *cells = g.View + (size_t)row * g.ViewCols;
    int y = row * g.CellHeight;
    int c0 = 0;
    while (c0 < g.ViewCols) {
        COLORREF fg, bg;
        BOOL sel = cell_selected(c0, row);
        cell_colors(&cells[c0], sel, &fg, &bg);
        UINT style = cells[c0].Style & RUN_STYLE_MASK;
        int c1 = c0 + 1;
        while (c1 < g.ViewCols) {
            COLORREF f2, b2;
            BOOL s2 = cell_selected(c1, row);
            cell_colors(&cells[c1], s2, &f2, &b2);
            if (f2 != fg || b2 != bg || (UINT)(cells[c1].Style & RUN_STYLE_MASK) != style)
                break;
            c1++;
        }
        /* a wide character is not cut in two. */
        if (c1 < g.ViewCols && (cells[c1].Flags & MIME_CONDRV_CELL_TRAIL) && c1 > c0)
            c1++;
        FontDrawRun(g.BackDc, c0 * g.CellWidth, y, cells + c0, c1 - c0, fg, bg, style);
        c0 = c1;
    }
}

static void invalidate_rows(int row0, int row1)
{
    RECT r = {0, row0 * g.CellHeight, g.ViewCols * g.CellWidth, (row1 + 1) * g.CellHeight};
    InvalidateRect(g.Window, &r, FALSE);
}

void WindowRefill(BOOL all)
{
    (void)all;
    for (int r = 0; r < g.ViewRows; r++)
        render_row(r);
    InvalidateRect(g.Window, nullptr, FALSE);
}

/* the rectangle of the cursor in the client area, empty when it is not shown. */
static BOOL cursor_rect(RECT *r)
{
    const MIME_CONDRV_HOST_STATE *s = &g.State;
    if (!s->CursorVisible)
        return FALSE;
    int column = s->Cursor.X - s->Window.Left, row = s->Cursor.Y - s->Window.Top;
    if (column < 0 || row < 0 || column >= g.ViewCols || row >= g.ViewRows)
        return FALSE;
    int width = g.CellWidth;
    if (g.View[(size_t)row * g.ViewCols + column].Flags & MIME_CONDRV_CELL_LEAD)
        width *= 2;
    int height = g.CellHeight * (int)s->CursorSize / 100;
    if (height < 1)
        height = 1;
    r->left = column * g.CellWidth;
    r->right = r->left + width;
    r->bottom = (row + 1) * g.CellHeight;
    r->top = r->bottom - height;
    return TRUE;
}

static void invalidate_cursor(void)
{
    RECT r;
    if (cursor_rect(&r))
        InvalidateRect(g.Window, &r, FALSE);
}

static void paint(HDC dc, const RECT *clip)
{
    RECT cells = {0, 0, g.BackWidth, g.BackHeight}, part;
    if (g.BackDc && IntersectRect(&part, clip, &cells))
        BitBlt(dc, part.left, part.top, part.right - part.left, part.bottom - part.top, g.BackDc, part.left, part.top, SRCCOPY);
    HBRUSH brush = CreateSolidBrush(default_background());
    RECT strip;
    if (clip->right > g.BackWidth) {
        strip = *clip;
        strip.left = max(strip.left, g.BackWidth);
        FillRect(dc, &strip, brush);
    }
    if (clip->bottom > g.BackHeight) {
        strip = *clip;
        strip.top = max(strip.top, g.BackHeight);
        strip.right = min(strip.right, g.BackWidth);
        FillRect(dc, &strip, brush);
    }
    DeleteObject(brush);
    RECT c;
    if ((g.CursorOn || !g.Focused) && cursor_rect(&c))
        PatBlt(dc, c.left, c.top, c.right - c.left, c.bottom - c.top, DSTINVERT);
}

/* ------------------------------------------------------------------ keeping up with the console */

static void update_scroll_bars(void)
{
    const MIME_CONDRV_HOST_STATE *s = &g.State;
    SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL, 0, s->Size.Y - 1, (UINT)window_rows(),
                     s->Window.Top, 0};
    SetScrollInfo(g.Window, SB_VERT, &si, TRUE);
    BOOL horizontal = has_horizontal_bar();
    if (horizontal) {
        SCROLLINFO hi = {sizeof(hi), SIF_RANGE | SIF_PAGE | SIF_POS, 0, s->Size.X - 1, (UINT)window_columns(), s->Window.Left, 0};
        SetScrollInfo(g.Window, SB_HORZ, &hi, TRUE);
    }
    ShowScrollBar(g.Window, SB_HORZ, horizontal);
}

/* the title of the console, with "Select " in front while text is selected, as the window of Windows shows it. */
static void apply_title(void)
{
    if (!g.Select.Active) {
        SetWindowTextW(g.Window, g.State.Title);
        return;
    }
    WCHAR text[280];
    lstrcpyW(text, L"Select ");
    lstrcpynW(text + 7, g.State.Title, 260);
    SetWindowTextW(g.Window, text);
}

static void restart_blink(void)
{
    g.CursorOn = TRUE;
    UINT time = GetCaretBlinkTime();
    KillTimer(g.Window, TIMER_BLINK);
    if (g.Focused && time && time != INFINITE)
        SetTimer(g.Window, TIMER_BLINK, time, nullptr);
    invalidate_cursor();
}

/* the cells of the window, from the console. the rows that differ are drawn again. */
static void fetch_view(BOOL force)
{
    int columns = window_columns(), rows = window_rows();
    if (columns < 1 || rows < 1)
        return;
    BOOL resized = columns != g.ViewCols || rows != g.ViewRows;
    if (resized) {
        if (g.View)
            HeapFree(GetProcessHeap(), 0, g.View);
        g.View = (Cell *)HeapAlloc(GetProcessHeap(), 8 /* HEAP_ZERO_MEMORY */, (SIZE_T)columns * rows * sizeof(Cell));
        g.ViewCols = columns;
        g.ViewRows = rows;
        if (!g.View) {
            g.ViewCols = g.ViewRows = 0;
            return;
        }
        back_resize(columns, rows);
        force = TRUE;
    }
    Cell *fresh = (Cell *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)columns * rows * sizeof(Cell));
    if (!fresh)
        return;
    HostFetchRows(g.State.Window.Top, rows, g.State.Window.Left, columns, fresh);
    BOOL moved = g.ViewTop != g.State.Window.Top || g.ViewLeft != g.State.Window.Left;
    g.ViewTop = g.State.Window.Top;
    g.ViewLeft = g.State.Window.Left;
    int first = -1, last = -1;
    for (int r = 0; r < rows; r++) {
        Cell *old = g.View + (size_t)r * columns;
        Cell *now = fresh + (size_t)r * columns;
        if (force || moved || memcmp(old, now, (size_t)columns * sizeof(Cell))) {
            memcpy(old, now, (size_t)columns * sizeof(Cell));
            render_row(r);
            if (first < 0)
                first = r;
            last = r;
        }
    }
    HeapFree(GetProcessHeap(), 0, fresh);
    if (first >= 0)
        invalidate_rows(first, last);
}

/* shows the window the way the program that started it asked. */
static void show_first(void)
{
    const MIME_CONDRV_HOST_STATE *s = &g.State;
    int cmd = SW_SHOWNORMAL;
    if ((s->StartupFlags & 0x1) && s->ShowWindow != SW_HIDE)
        cmd = (int)s->ShowWindow;
    if (cmd == SW_SHOWDEFAULT)
        cmd = SW_SHOWNORMAL;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    int w, h;
    frame_size(window_columns(), window_rows(), &w, &h);
    if ((s->StartupFlags & 0x2) && s->SizeX && s->SizeY) {
        w = (int)s->SizeX;
        h = (int)s->SizeY;
    }
    int x = 0, y = 0;
    if (s->StartupFlags & 0x4) {
        x = s->StartX;
        y = s->StartY;
    } else {
        flags |= SWP_NOMOVE;
    }
    g.Sizing = TRUE;
    SetWindowPos(g.Window, nullptr, x, y, w, h, flags);
    g.Sizing = FALSE;
    ShowWindow(g.Window, cmd);
    RECT client;
    GetClientRect(g.Window, &client);
    int columns = client.right / g.CellWidth, rows = client.bottom / g.CellHeight;
    if ((s->StartupFlags & 0x2) && columns >= 1 && rows >= 1 && (columns != window_columns() || rows != window_rows())) {
        HostResize(columns, rows);
        MIME_CONDRV_HOST_STATE st;
        if (HostControl(IOCTL_CONDRV_HOST_QUERY, nullptr, 0, &st, sizeof(st)))
            g.State = st;
    }
}

void WindowUpdate(const MIME_CONDRV_HOST_STATE *old, ULONG changed, BOOL first)
{
    const MIME_CONDRV_HOST_STATE *s = &g.State;
    if (first) {
        apply_title();
        g.LastScrolls = s->Scrolls;
        fetch_view(TRUE);
        update_scroll_bars();
        show_first();
        fetch_view(FALSE);
        update_scroll_bars();
        restart_blink();
        return;
    }
    if (changed & MIME_CONDRV_CHANGED_TITLE)
        apply_title();
    if (changed & MIME_CONDRV_CHANGED_BELL)
        MessageBeep((UINT)-1);
    /* the selection follows its text when the buffer scrolls. */
    if (s->Scrolls != g.LastScrolls) {
        int delta = (int)(s->Scrolls - g.LastScrolls);
        g.LastScrolls = s->Scrolls;
        if (g.Select.Active) {
            g.Select.Anchor.y -= delta;
            g.Select.End.y -= delta;
            if (g.Select.Anchor.y < 0 && g.Select.End.y < 0)
                g.Select.Active = FALSE;
            else {
                g.Select.Anchor.y = max(0, g.Select.Anchor.y);
                g.Select.End.y = max(0, g.Select.End.y);
            }
        }
    }
    BOOL geometry = old->Size.X != s->Size.X || old->Size.Y != s->Size.Y || old->Window.Left != s->Window.Left ||
                    old->Window.Top != s->Window.Top || old->Window.Right != s->Window.Right ||
                    old->Window.Bottom != s->Window.Bottom || old->Alternate != s->Alternate;
    if (geometry) {
        if (!g.Maximized && !g.Minimized && !g.UserSizing)
            size_frame(window_columns(), window_rows());
        update_scroll_bars();
        if (!g.Maximized && !g.Minimized && !g.UserSizing)
            size_frame(window_columns(), window_rows());
    }
    BOOL palette = memcmp(old->ColorTable, s->ColorTable, sizeof(s->ColorTable)) || old->Attributes != s->Attributes;
    fetch_view(palette || (changed & MIME_CONDRV_CHANGED_PALETTE));
    if (old->Cursor.X != s->Cursor.X || old->Cursor.Y != s->Cursor.Y || old->CursorVisible != s->CursorVisible ||
        old->CursorSize != s->CursorSize) {
        RECT r;
        /* the cell the cursor was in is drawn again from the picture. */
        RECT was = {(old->Cursor.X - s->Window.Left) * g.CellWidth, (old->Cursor.Y - s->Window.Top) * g.CellHeight, 0, 0};
        was.right = was.left + 2 * g.CellWidth;
        was.bottom = was.top + g.CellHeight;
        InvalidateRect(g.Window, &was, FALSE);
        if (cursor_rect(&r))
            InvalidateRect(g.Window, &r, FALSE);
        restart_blink();
    }
    if (g.Select.Active)
        HostSetSelection();
}

/* ------------------------------------------------------------------ the keyboard */

static DWORD control_state(LPARAM lparam)
{
    DWORD s = 0;
    if (GetKeyState(VK_RMENU) & 0x8000)
        s |= RIGHT_ALT_PRESSED;
    if ((GetKeyState(VK_LMENU) & 0x8000) || ((GetKeyState(VK_MENU) & 0x8000) && !(s & RIGHT_ALT_PRESSED)))
        s |= LEFT_ALT_PRESSED;
    if (GetKeyState(VK_RCONTROL) & 0x8000)
        s |= RIGHT_CTRL_PRESSED;
    if ((GetKeyState(VK_LCONTROL) & 0x8000) || ((GetKeyState(VK_CONTROL) & 0x8000) && !(s & RIGHT_CTRL_PRESSED)))
        s |= LEFT_CTRL_PRESSED;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        s |= SHIFT_PRESSED;
    if (GetKeyState(VK_NUMLOCK) & 1)
        s |= 0x20;
    if (GetKeyState(VK_SCROLL) & 1)
        s |= 0x40;
    if (GetKeyState(VK_CAPITAL) & 1)
        s |= 0x80;
    if (lparam & (1 << 24))
        s |= ENHANCED_KEY;
    return s;
}

static void send_key(BOOL down, UINT vk, UINT scan, DWORD extra_state, LPARAM lparam, WCHAR ch)
{
    MIME_CONDRV_RECORD r = {};
    r.EventType = KEY_EVENT;
    r.Key.KeyDown = down;
    r.Key.RepeatCount = (USHORT)(down ? max(1, (int)(lparam & 0xFFFF)) : 1);
    r.Key.VirtualKey = (USHORT)vk;
    r.Key.ScanCode = (USHORT)scan;
    r.Key.Char = ch;
    r.Key.ControlKeyState = control_state(lparam) | extra_state;
    HostSendRecords(&r, 1);
}

static BOOL is_modifier(UINT vk)
{
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL ||
           vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU || vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL ||
           vk == VK_LWIN || vk == VK_RWIN;
}

/* a key brings the viewport back to the cursor. */
static void show_cursor_row(void)
{
    const MIME_CONDRV_HOST_STATE *s = &g.State;
    if (s->Alternate)
        return;
    if (s->Cursor.Y < s->Window.Top || s->Cursor.Y > s->Window.Bottom || s->Cursor.X < s->Window.Left || s->Cursor.X > s->Window.Right) {
        HostScroll(-1, -1);
        HostUpdate();
    }
}

static BOOL ctrl_down(void)
{
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

static BOOL shift_down(void)
{
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

static BOOL selection_not_empty(void)
{
    return g.Select.Active && (g.Select.Anchor.x != g.Select.End.x || g.Select.Anchor.y != g.Select.End.y || g.Select.Block);
}

/* returns TRUE when the window used the key itself. */
static BOOL key_shortcut(UINT vk, BOOL down)
{
    if (!down)
        return FALSE;
    if (vk == VK_ESCAPE && g.Select.Active) {
        WindowClearSelection();
        return TRUE;
    }
    if ((vk == VK_RETURN) && selection_not_empty()) {
        WindowCopy();
        WindowClearSelection();
        return TRUE;
    }
    if (ctrl_down() && vk == 'C' && selection_not_empty() && !shift_down()) {
        WindowCopy();
        WindowClearSelection();
        return TRUE;
    }
    if ((ctrl_down() && vk == 'V' && !shift_down()) || (shift_down() && vk == VK_INSERT && !ctrl_down())) {
        WindowPaste();
        return TRUE;
    }
    if (ctrl_down() && vk == 'A' && !shift_down()) {
        WindowSelectAll();
        return TRUE;
    }
    return FALSE;
}

/* a control character that arrives alone is not typed. */
static BOOL is_control_char_without_key(WCHAR ch)
{
    return ch < 0x20 && ch != L'\r' && ch != L'\t' && ch != L'\b';
}

static LRESULT key_message(UINT msg, WPARAM wparam, LPARAM lparam)
{
    BOOL down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    UINT vk = (UINT)wparam;
    BOOL sys = msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP;
    /* the system keeps Alt+F4 and Alt+Space. */
    if (sys && (vk == VK_F4 || vk == VK_SPACE || vk == VK_TAB || vk == VK_ESCAPE))
        return DefWindowProcW(g.Window, msg, wparam, lparam);
    WCHAR ch = 0;
    if (down) {
        /* the character of this key is the next message when it has one: a character that belongs to a later key stays. */
        MSG m;
        if (PeekMessageW(&m, g.Window, 0, 0, PM_NOREMOVE) && (m.message == WM_CHAR || m.message == WM_SYSCHAR) &&
            PeekMessageW(&m, g.Window, m.message, m.message, PM_REMOVE))
            ch = (WCHAR)m.wParam;
        /* with the control key a letter is its control character, as the layout makes it. */
        if (ctrl_down() && !(GetKeyState(VK_MENU) & 0x8000) && vk >= 'A' && vk <= 'Z')
            ch = (WCHAR)(vk - 'A' + 1);
        if (key_shortcut(vk, TRUE)) {
            g_swallowed[vk & 0xFF] = TRUE;
            return 0;
        }
        g_last_char[vk & 0xFF] = ch;
        if (!is_modifier(vk)) {
            if (g.Select.Active)
                WindowClearSelection();
            show_cursor_row();
        }
    } else {
        if (g_swallowed[vk & 0xFF]) {
            g_swallowed[vk & 0xFF] = FALSE;
            return 0;
        }
        ch = g_last_char[vk & 0xFF];
        g_last_char[vk & 0xFF] = 0;
    }
    g.CursorOn = TRUE;
    UINT out_vk = vk, scan = (UINT)((lparam >> 16) & 0xFF);
    DWORD extra = 0;
    if (vk == VK_PACKET) {
        /* a character typed as itself, as the input of the machine and input methods make them. */
        SHORT s = ch ? VkKeyScanW(ch) : (SHORT)-1;
        out_vk = s == -1 ? 0 : (UINT)(s & 0xFF);
        scan = out_vk ? MapVirtualKeyW(out_vk, MAPVK_VK_TO_VSC) : 0;
        if (s != -1 && (s & 0x100))
            extra |= SHIFT_PRESSED;
        if (s != -1 && (s & 0x200))
            extra |= LEFT_CTRL_PRESSED;
    }
    send_key(down, out_vk, scan, extra, lparam, ch);
    return 0;
}

/* ------------------------------------------------------------------ the mouse and the selection */

static BOOL app_wants_mouse(void)
{
    return (g.State.InputMode & ENABLE_MOUSE_INPUT) && !g.State.QuickEdit;
}

/* the cell under a point of the client area, in cells of the buffer. */
static POINT cell_at(int x, int y, BOOL clamp)
{
    int column = x / g.CellWidth, row = y / g.CellHeight;
    if (x < 0)
        column = -1;
    if (y < 0)
        row = -1;
    if (clamp) {
        column = max(0, min(column, g.ViewCols - 1));
        row = max(0, min(row, g.ViewRows - 1));
    }
    POINT p = {g.ViewLeft + column, g.ViewTop + row};
    return p;
}

static void send_mouse(UINT msg, WPARAM wparam, LPARAM lparam)
{
    POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    MIME_CONDRV_RECORD r = {};
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL)
        ScreenToClient(g.Window, &pt);
    POINT cell = cell_at(pt.x, pt.y, TRUE);
    r.EventType = MOUSE_EVENT;
    r.Mouse.X = (SHORT)cell.x;
    r.Mouse.Y = (SHORT)cell.y;
    UINT keys = LOWORD(wparam);
    ULONG buttons = 0;
    if (keys & MK_LBUTTON)
        buttons |= FROM_LEFT_1ST_BUTTON_PRESSED;
    if (keys & MK_RBUTTON)
        buttons |= RIGHTMOST_BUTTON_PRESSED;
    if (keys & MK_MBUTTON)
        buttons |= FROM_LEFT_2ND_BUTTON_PRESSED;
    ULONG flags = 0;
    if (msg == WM_MOUSEMOVE)
        flags |= 1;
    if (msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK || msg == WM_MBUTTONDBLCLK)
        flags |= 2;
    if (msg == WM_MOUSEWHEEL) {
        flags |= 4;
        buttons |= (ULONG)(GET_WHEEL_DELTA_WPARAM(wparam) << 16);
    }
    if (msg == WM_MOUSEHWHEEL) {
        flags |= 8;
        buttons |= (ULONG)(GET_WHEEL_DELTA_WPARAM(wparam) << 16);
    }
    r.Mouse.ButtonState = buttons;
    r.Mouse.ControlKeyState = control_state(0);
    r.Mouse.EventFlags = flags;
    HostSendRecords(&r, 1);
}

static void apply_title(void);

static void selection_changed(void)
{
    apply_title();
    g.Select.Dragging = g.Select.Dragging && g.Select.Active;
    for (int r = 0; r < g.ViewRows; r++)
        render_row(r);
    InvalidateRect(g.Window, nullptr, FALSE);
    HostSetSelection();
}

void WindowClearSelection(void)
{
    if (!g.Select.Active)
        return;
    g.Select.Active = FALSE;
    g.Select.Dragging = FALSE;
    if (GetCapture() == g.Window)
        ReleaseCapture();
    selection_changed();
}

static BOOL word_char(WCHAR c)
{
    static const WCHAR separators[] = L"!\"#$%&'()*+,-./:;<=>?@[\\]^`{|}~";
    if (c <= L' ')
        return FALSE;
    for (const WCHAR *p = separators; *p; p++)
        if (*p == c)
            return FALSE;
    return TRUE;
}

/* the cells of a word, or of the line when the click was beyond its text. */
static void select_word(POINT cell)
{
    int row = cell.y - g.ViewTop;
    if (row < 0 || row >= g.ViewRows)
        return;
    const Cell *cells = g.View + (size_t)row * g.ViewCols;
    int column = cell.x - g.ViewLeft;
    if (column < 0 || column >= g.ViewCols)
        return;
    BOOL word = word_char((WCHAR)cells[column].Char);
    int a = column, b = column;
    while (a > 0 && word_char((WCHAR)cells[a - 1].Char) == word)
        a--;
    while (b < g.ViewCols - 1 && word_char((WCHAR)cells[b + 1].Char) == word)
        b++;
    g.Select.Active = TRUE;
    g.Select.Block = FALSE;
    g.Select.Anchor.x = g.ViewLeft + a;
    g.Select.Anchor.y = cell.y;
    g.Select.End.x = g.ViewLeft + b;
    g.Select.End.y = cell.y;
    selection_changed();
}

static void select_line(POINT cell)
{
    g.Select.Active = TRUE;
    g.Select.Block = FALSE;
    g.Select.Anchor.x = 0;
    g.Select.Anchor.y = cell.y;
    g.Select.End.x = g.State.Size.X - 1;
    g.Select.End.y = cell.y;
    selection_changed();
}

void WindowSelectAll(void)
{
    const MIME_CONDRV_HOST_STATE *s = &g.State;
    g.Select.Active = TRUE;
    g.Select.Block = FALSE;
    g.Select.Anchor.x = 0;
    g.Select.Anchor.y = 0;
    g.Select.End.x = s->Size.X - 1;
    g.Select.End.y = max(s->Cursor.Y, s->Window.Bottom);
    if (g.Select.End.y >= s->Size.Y)
        g.Select.End.y = s->Size.Y - 1;
    selection_changed();
}

static void scroll_by(int rows)
{
    const MIME_CONDRV_HOST_STATE *s = &g.State;
    if (!rows || s->Alternate)
        return;
    int top = s->Window.Top + rows;
    top = max(0, min(top, s->Size.Y - window_rows()));
    if (top == s->Window.Top)
        return;
    HostScroll(s->Window.Left, top);
    HostUpdate();
}

static void mouse_message(UINT msg, WPARAM wparam, LPARAM lparam)
{
    POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    if (app_wants_mouse()) {
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
            SetCapture(g.Window);
        if ((msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP) && !(wparam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)))
            ReleaseCapture();
        send_mouse(msg, wparam, lparam);
        return;
    }
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        POINT cell = cell_at(pt.x, pt.y, TRUE);
        ULONG now = GetTickCount();
        if (msg == WM_LBUTTONDBLCLK) {
            g_click_count = 2;
            g_last_click_time = now;
            g_last_click = cell;
            select_word(cell);
            return;
        }
        if (g_click_count == 2 && now - g_last_click_time <= GetDoubleClickTime() && cell.x == g_last_click.x &&
            cell.y == g_last_click.y) {
            g_click_count = 3;
            select_line(cell);
            return;
        }
        g_click_count = 1;
        g_last_click_time = now;
        g_last_click = cell;
        SetFocus(g.Window);
        if (shift_down() && g.Select.Active) {
            g.Select.End = cell;
        } else {
            g.Select.Active = TRUE;
            g.Select.Block = (GetKeyState(VK_MENU) & 0x8000) != 0;
            g.Select.Anchor = cell;
            g.Select.End = cell;
        }
        g.Select.Dragging = TRUE;
        SetCapture(g.Window);
        selection_changed();
        break;
    }
    case WM_MOUSEMOVE:
        if (g.Select.Dragging) {
            POINT cell = cell_at(pt.x, pt.y, TRUE);
            RECT client;
            GetClientRect(g.Window, &client);
            if (pt.y < 0)
                scroll_by(-1), cell = cell_at(pt.x, 0, TRUE);
            else if (pt.y >= g.ViewRows * g.CellHeight)
                scroll_by(1), cell = cell_at(pt.x, g.ViewRows * g.CellHeight - 1, TRUE);
            if (cell.x != g.Select.End.x || cell.y != g.Select.End.y) {
                g.Select.End = cell;
                selection_changed();
            }
        }
        break;
    case WM_LBUTTONUP:
        if (g.Select.Dragging) {
            g.Select.Dragging = FALSE;
            ReleaseCapture();
            if (g.Select.Anchor.x == g.Select.End.x && g.Select.Anchor.y == g.Select.End.y && !g.Select.Block)
                g.Select.Active = FALSE;
            selection_changed();
        }
        break;
    case WM_RBUTTONDOWN:
        if (selection_not_empty()) {
            WindowCopy();
            WindowClearSelection();
        } else {
            WindowClearSelection();
            WindowPaste();
        }
        break;
    }
}

/* ------------------------------------------------------------------ the clipboard */

BOOL WindowCopy(void)
{
    if (!g.Select.Active)
        return FALSE;
    POINT a = g.Select.Anchor, b = g.Select.End;
    if (!g.Select.Block && (a.y > b.y || (a.y == b.y && a.x > b.x))) {
        POINT t = a;
        a = b;
        b = t;
    }
    int top = min(a.y, b.y), bottom = max(a.y, b.y);
    int width = g.State.Size.X;
    int rows = bottom - top + 1;
    Cell *cells = (Cell *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)rows * width * sizeof(Cell));
    if (!cells)
        return FALSE;
    HostFetchRows(top, rows, 0, width, cells);
    SIZE_T capacity = ((SIZE_T)rows * (width + 2) + 1) * sizeof(WCHAR);
    WCHAR *text = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, capacity);
    if (!text) {
        HeapFree(GetProcessHeap(), 0, cells);
        return FALSE;
    }
    SIZE_T n = 0;
    for (int y = top; y <= bottom; y++) {
        int x0 = 0, x1 = width - 1;
        if (g.Select.Block) {
            x0 = min(a.x, b.x);
            x1 = max(a.x, b.x);
        } else {
            if (y == a.y)
                x0 = a.x;
            if (y == b.y)
                x1 = b.x;
        }
        const Cell *row = cells + (size_t)(y - top) * width;
        SIZE_T start = n;
        for (int x = max(0, x0); x <= x1 && x < width; x++) {
            if (row[x].Flags & MIME_CONDRV_CELL_TRAIL)
                continue;
            ULONG ch = row[x].Char;
            if (ch < 0x20)
                ch = ' ';
            if (ch >= 0x10000) {
                ch -= 0x10000;
                text[n++] = (WCHAR)(0xD800 + (ch >> 10));
                text[n++] = (WCHAR)(0xDC00 + (ch & 0x3FF));
            } else {
                text[n++] = (WCHAR)ch;
            }
        }
        while (n > start && text[n - 1] == L' ')
            n--;
        BOOL wrapped = (row[width - 1].Flags & 0x4) != 0;
        if (y < bottom && (g.Select.Block || !wrapped)) {
            text[n++] = L'\r';
            text[n++] = L'\n';
        }
    }
    text[n] = 0;
    HeapFree(GetProcessHeap(), 0, cells);
    BOOL ok = FALSE;
    if (OpenClipboard(g.Window)) {
        EmptyClipboard();
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (n + 1) * sizeof(WCHAR));
        if (mem) {
            WCHAR *dst = (WCHAR *)GlobalLock(mem);
            if (dst) {
                memcpy(dst, text, (n + 1) * sizeof(WCHAR));
                GlobalUnlock(mem);
                ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
            }
            if (!ok)
                GlobalFree(mem);
        }
        CloseClipboard();
    }
    HeapFree(GetProcessHeap(), 0, text);
    return ok;
}

void WindowPaste(void)
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(g.Window))
        return;
    HGLOBAL mem = GetClipboardData(CF_UNICODETEXT);
    const WCHAR *text = mem ? (const WCHAR *)GlobalLock(mem) : nullptr;
    if (text) {
        MIME_CONDRV_RECORD batch[256];
        ULONG n = 0;
        show_cursor_row();
        for (const WCHAR *p = text; *p; p++) {
            WCHAR ch = *p;
            if (ch == L'\n') {
                if (p > text && p[-1] == L'\r')
                    continue;
                ch = L'\r';
            }
            SHORT scan = VkKeyScanW(ch);
            UINT vk = scan == -1 ? 0 : (UINT)(scan & 0xFF);
            if (ch == L'\r')
                vk = VK_RETURN;
            else if (ch == L'\t')
                vk = VK_TAB;
            for (int down = 1; down >= 0; down--) {
                MIME_CONDRV_RECORD *r = &batch[n++];
                memset(r, 0, sizeof(*r));
                r->EventType = KEY_EVENT;
                r->Key.KeyDown = (ULONG)down;
                r->Key.RepeatCount = 1;
                r->Key.VirtualKey = (USHORT)vk;
                r->Key.ScanCode = (USHORT)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                r->Key.Char = ch;
                if (scan != -1 && (scan & 0x100))
                    r->Key.ControlKeyState = SHIFT_PRESSED;
            }
            if (n >= RTL_NUMBER_OF(batch) - 2) {
                HostSendRecords(batch, n);
                n = 0;
            }
        }
        if (n)
            HostSendRecords(batch, n);
        GlobalUnlock(mem);
    }
    CloseClipboard();
}

/* ------------------------------------------------------------------ the window procedure */

static void build_system_menu(HWND window)
{
    HMENU system = GetSystemMenu(window, FALSE);
    if (!system)
        return;
    g_edit_menu = CreatePopupMenu();
    AppendMenuW(g_edit_menu, MF_STRING, IDM_COPY, L"&Copy");
    AppendMenuW(g_edit_menu, MF_STRING, IDM_PASTE, L"&Paste");
    AppendMenuW(g_edit_menu, MF_STRING, IDM_SELECT_ALL, L"Select &All");
    AppendMenuW(system, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(system, MF_POPUP, (UINT_PTR)g_edit_menu, L"&Edit");
    AppendMenuW(system, MF_STRING, IDM_PROPERTIES, L"&Properties");
}

static void user_resized(void)
{
    if (g.Sizing || g.Minimized || !g.State.Window.Right)
        return;
    RECT client;
    GetClientRect(g.Window, &client);
    int columns = client.right / g.CellWidth, rows = client.bottom / g.CellHeight;
    if (columns < 1 || rows < 1)
        return;
    if (columns == window_columns() && rows == window_rows())
        return;
    HostResize(columns, rows);
    HostUpdate();
}

static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CREATE:
        build_system_menu(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        paint(dc, &ps.rcPaint);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_SIZE:
        g.Minimized = wparam == SIZE_MINIMIZED;
        g.Maximized = wparam == SIZE_MAXIMIZED;
        if (wparam != SIZE_MINIMIZED)
            user_resized();
        return 0;
    case WM_ENTERSIZEMOVE:
        g.UserSizing = TRUE;
        return 0;
    case WM_EXITSIZEMOVE:
        g.UserSizing = FALSE;
        user_resized();
        if (!g.Maximized)
            size_frame(window_columns(), window_rows());
        return 0;
    case WM_SIZING: {
        RECT *r = (RECT *)lparam;
        int w0, h0;
        frame_size(0, 0, &w0, &h0);
        int columns = max(12, (r->right - r->left - w0 + g.CellWidth / 2) / g.CellWidth);
        int rows = max(4, (r->bottom - r->top - h0 + g.CellHeight / 2) / g.CellHeight);
        int w = w0 + columns * g.CellWidth, h = h0 + rows * g.CellHeight;
        if (wparam == WMSZ_LEFT || wparam == WMSZ_TOPLEFT || wparam == WMSZ_BOTTOMLEFT)
            r->left = r->right - w;
        else
            r->right = r->left + w;
        if (wparam == WMSZ_TOP || wparam == WMSZ_TOPLEFT || wparam == WMSZ_TOPRIGHT)
            r->top = r->bottom - h;
        else
            r->bottom = r->top + h;
        return TRUE;
    }
    case WM_DPICHANGED: {
        g.Dpi = HIWORD(wparam);
        FontOpen(g.Dpi);
        g.CellWidth = g_font_cell_w;
        g.CellHeight = g_font_cell_h;
        HostFont();
        g.ViewCols = 0;
        g.ViewRows = 0;
        fetch_view(TRUE);
        const RECT *r = (const RECT *)lparam;
        BOOL was = g.Sizing;
        g.Sizing = TRUE;
        SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g.Sizing = was;
        size_frame(window_columns(), window_rows());
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_VSCROLL: {
        const MIME_CONDRV_HOST_STATE *s = &g.State;
        int top = s->Window.Top, page = window_rows();
        SCROLLINFO si = {sizeof(si), SIF_ALL, 0, 0, 0, 0, 0};
        GetScrollInfo(window, SB_VERT, &si);
        switch (LOWORD(wparam)) {
        case SB_LINEUP:
            top--;
            break;
        case SB_LINEDOWN:
            top++;
            break;
        case SB_PAGEUP:
            top -= page;
            break;
        case SB_PAGEDOWN:
            top += page;
            break;
        case SB_TOP:
            top = 0;
            break;
        case SB_BOTTOM:
            top = s->Size.Y;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            top = si.nTrackPos;
            break;
        default:
            return 0;
        }
        top = max(0, min(top, s->Size.Y - page));
        if (top != s->Window.Top) {
            HostScroll(s->Window.Left, top);
            HostUpdate();
        }
        return 0;
    }
    case WM_HSCROLL: {
        const MIME_CONDRV_HOST_STATE *s = &g.State;
        int left = s->Window.Left, page = window_columns();
        SCROLLINFO si = {sizeof(si), SIF_ALL, 0, 0, 0, 0, 0};
        GetScrollInfo(window, SB_HORZ, &si);
        switch (LOWORD(wparam)) {
        case SB_LINELEFT:
            left--;
            break;
        case SB_LINERIGHT:
            left++;
            break;
        case SB_PAGELEFT:
            left -= page;
            break;
        case SB_PAGERIGHT:
            left += page;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            left = si.nTrackPos;
            break;
        default:
            return 0;
        }
        left = max(0, min(left, s->Size.X - page));
        if (left != s->Window.Left) {
            HostScroll(left, s->Window.Top);
            HostUpdate();
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (app_wants_mouse()) {
            send_mouse(msg, wparam, lparam);
            return 0;
        } else {
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            g_wheel_remainder += GET_WHEEL_DELTA_WPARAM(wparam);
            int notches = g_wheel_remainder / WHEEL_DELTA;
            g_wheel_remainder -= notches * WHEEL_DELTA;
            if (notches)
                scroll_by(-notches * (int)(lines ? lines : 3));
        }
        return 0;
    case WM_MOUSEHWHEEL:
        if (app_wants_mouse())
            send_mouse(msg, wparam, lparam);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
        mouse_message(msg, wparam, lparam);
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        return key_message(msg, wparam, lparam);
    case WM_CHAR:
    case WM_SYSCHAR:
        /* the characters of keys were taken with them, so this one was typed as itself: by input that has no key, or by
         * a program that posted it. */
        if (!is_control_char_without_key((WCHAR)wparam)) {
            WCHAR ch = (WCHAR)wparam;
            SHORT scan = VkKeyScanW(ch);
            UINT vk = scan == -1 ? 0 : (UINT)(scan & 0xFF);
            DWORD extra = (scan != -1 && (scan & 0x100)) ? SHIFT_PRESSED : 0;
            if (g.Select.Active)
                WindowClearSelection();
            show_cursor_row();
            send_key(TRUE, vk, vk ? MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) : 0, extra, 1, ch);
            send_key(FALSE, vk, vk ? MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) : 0, extra, 1, ch);
        }
        return 0;
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        g.Focused = msg == WM_SETFOCUS;
        restart_blink();
        MIME_CONDRV_RECORD r = {};
        r.EventType = 0x10; /* FOCUS_EVENT */
        r.Value = g.Focused;
        HostSendRecords(&r, 1);
        return 0;
    }
    case WM_TIMER:
        if (wparam == TIMER_BLINK) {
            g.CursorOn = !g.CursorOn;
            invalidate_cursor();
        }
        return 0;
    case WM_INITMENUPOPUP:
        if ((HMENU)wparam == g_edit_menu) {
            EnableMenuItem(g_edit_menu, IDM_COPY, MF_BYCOMMAND | (selection_not_empty() ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(g_edit_menu, IDM_PASTE, MF_BYCOMMAND | (IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_ENABLED : MF_GRAYED));
        }
        return 0;
    case WM_SYSCOMMAND:
        switch (wparam & 0xFFF0) {
        case IDM_COPY:
            WindowCopy();
            WindowClearSelection();
            return 0;
        case IDM_PASTE:
            WindowPaste();
            return 0;
        case IDM_SELECT_ALL:
            WindowSelectAll();
            return 0;
        case IDM_PROPERTIES:
            PropertiesShow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        /* the programs are told, and the window goes when the last of them has. */
        HostClose();
        return 0;
    case WM_DESTROY:
        KillTimer(window, TIMER_BLINK);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, msg, wparam, lparam);
}

/* ------------------------------------------------------------------ the loop */

BOOL WindowCreate(void)
{
    HDC screen = GetDC(nullptr);
    g.Dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    if (!g.Dpi)
        g.Dpi = 96;
    if (!FontOpen(g.Dpi))
        return FALSE;
    g.CellWidth = g_font_cell_w;
    g.CellHeight = g_font_cell_h;
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = CONSOLE_CLASS;
    if (!RegisterClassExW(&wc))
        return FALSE;
    int w, h;
    frame_size(120, 30, &w, &h);
    g.Window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, CONSOLE_CLASS, L"", WINDOW_STYLE, CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr,
                               wc.hInstance, nullptr);
    return g.Window != nullptr;
}

void WindowRun(void)
{
    ULONG last = 0;
    for (;;) {
        HANDLE handles[1] = {g.Event};
        ULONG now = GetTickCount();
        ULONG wait = INFINITE;
        DWORD count = 1;
        /* a program that writes all the time is shown sixty times a second at most. */
        if (last && now - last < 16) {
            count = 0;
            wait = 16 - (now - last);
        }
        DWORD r = MsgWaitForMultipleObjectsEx(count, handles, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_OBJECT_0 && count) {
            HostUpdate();
            last = GetTickCount();
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                return;
            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)
                TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g.Closing)
            return;
    }
}
