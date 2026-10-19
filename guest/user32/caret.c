/*
 * the caret: one for each thread, a rectangle or bitmap inverted over its
 * window, blinking on the window's system timer. painting hides it while
 * BeginPaint and EndPaint run, as windows' does, so it never gets drawn
 * over and then inverted back into garbage.
 */
#include "user32_private.h"

#define CARET_TIMER 0xFFFF

typedef struct CARET {
    HWND hwnd;
    HBITMAP bitmap;
    int width, height;
    int x, y;
    int hidden;
    BOOL on;
} CARET;

static DWORD g_tls = TLS_OUT_OF_INDEXES;
static UINT g_blink = 530;

static CARET *thread_caret(BOOL create)
{
    if (g_tls == TLS_OUT_OF_INDEXES) {
        DWORD index = TlsAlloc();
        if (InterlockedCompareExchange((LONG *)&g_tls, (LONG)index, (LONG)TLS_OUT_OF_INDEXES) !=
            (LONG)TLS_OUT_OF_INDEXES)
            TlsFree(index);
    }
    CARET *c = TlsGetValue(g_tls);
    if (!c && create) {
        c = UserAlloc(sizeof(*c));
        TlsSetValue(g_tls, c);
    }
    return c;
}

static CARET *owned(void)
{
    CARET *c = thread_caret(FALSE);
    return c && c->hwnd ? c : nullptr;
}

/* a 50% gray, which a caret of bitmap 1 inverts with. */
static HBRUSH gray_brush(void)
{
    static HBRUSH brush;
    if (!brush) {
        static const WORD pattern[8] = {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};
        HBITMAP b = CreateBitmap(8, 8, 1, 1, pattern);
        brush = CreatePatternBrush(b);
        DeleteObject(b);
    }
    return brush;
}

static void toggle(CARET *c)
{
    HDC dc = GetDC(c->hwnd);
    if (!dc)
        return;
    if (!c->bitmap) {
        PatBlt(dc, c->x, c->y, c->width, c->height, DSTINVERT);
    } else if (c->bitmap == (HBITMAP)1) {
        HGDIOBJ old = SelectObject(dc, gray_brush());
        PatBlt(dc, c->x, c->y, c->width, c->height, PATINVERT);
        SelectObject(dc, old);
    } else {
        HDC memory = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(memory, c->bitmap);
        BitBlt(dc, c->x, c->y, c->width, c->height, memory, 0, 0, SRCINVERT);
        SelectObject(memory, old);
        DeleteDC(memory);
    }
    ReleaseDC(c->hwnd, dc);
    c->on = !c->on;
}

static void set_timer(CARET *c)
{
    MIME_SYS_TIMER st = {CARET_TIMER, g_blink == INFINITE ? 0 : g_blink};
    NtUserCallHwndParam(c->hwnd, (ULONG_PTR)&st, MIME_HWNDPARAM_SYSTIMER);
}

static void kill_timer(CARET *c)
{
    MIME_SYS_TIMER st = {CARET_TIMER, 0};
    NtUserCallHwndParam(c->hwnd, (ULONG_PTR)&st, MIME_HWNDPARAM_SYSTIMER);
}

/* shown again, the caret starts a blink period on. */
static void show_now(CARET *c)
{
    if (!c->on)
        toggle(c);
    set_timer(c);
}

WINUSERAPI BOOL WINAPI CreateCaret(HWND hwnd, HBITMAP bitmap, int width, int height)
{
    DWORD pid;
    if (!hwnd || GetWindowThreadProcessId(hwnd, &pid) != GetCurrentThreadId())
        return FALSE;
    DestroyCaret();
    CARET *c = thread_caret(TRUE);
    if (!c)
        return FALSE;
    if (bitmap && bitmap != (HBITMAP)1) {
        BITMAP bm;
        if (!GetObjectW(bitmap, sizeof(bm), &bm))
            return FALSE;
        width = bm.bmWidth;
        height = bm.bmHeight;
    } else {
        if (!width)
            width = GetSystemMetrics(SM_CXBORDER);
        if (!height)
            height = GetSystemMetrics(SM_CYBORDER);
    }
    *c = (CARET){hwnd, bitmap, width, height, 0, 0, 1, FALSE};
    return TRUE;
}

WINUSERAPI BOOL WINAPI DestroyCaret(void)
{
    CARET *c = owned();
    if (!c)
        return FALSE;
    if (c->on)
        toggle(c);
    kill_timer(c);
    c->hwnd = nullptr;
    return TRUE;
}

WINUSERAPI BOOL WINAPI ShowCaret(HWND hwnd)
{
    CARET *c = owned();
    if (!c || (hwnd && hwnd != c->hwnd) || !c->hidden)
        return FALSE;
    if (!--c->hidden)
        show_now(c);
    return TRUE;
}

WINUSERAPI BOOL WINAPI HideCaret(HWND hwnd)
{
    CARET *c = owned();
    if (!c || (hwnd && hwnd != c->hwnd))
        return FALSE;
    if (c->on)
        toggle(c);
    if (!c->hidden++)
        kill_timer(c);
    return TRUE;
}

/* an input method puts its popups at the caret, until the program says elsewhere. */
static void follow(const CARET *c)
{
    LONG area[5] = {c->x, c->y, c->x + (c->width > 0 ? c->width : 1), c->y + c->height, c->x};
    NtUserMimeIme(MIME_IME_AREA, (ULONG_PTR)c->hwnd, 0, (PUCHAR)area, sizeof(area));
}

WINUSERAPI BOOL WINAPI SetCaretPos(int x, int y)
{
    CARET *c = owned();
    if (!c)
        return FALSE;
    if (x == c->x && y == c->y)
        return TRUE;
    if (c->on)
        toggle(c);
    c->x = x;
    c->y = y;
    if (!c->hidden)
        show_now(c);
    follow(c);
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetCaretPos(LPPOINT point)
{
    CARET *c = owned();
    if (!c || !point)
        return FALSE;
    point->x = c->x;
    point->y = c->y;
    return TRUE;
}

WINUSERAPI UINT WINAPI GetCaretBlinkTime(void) { return g_blink; }

WINUSERAPI BOOL WINAPI SetCaretBlinkTime(UINT milliseconds)
{
    g_blink = milliseconds;
    CARET *c = owned();
    if (c && !c->hidden)
        set_timer(c);
    return TRUE;
}

/* the blink, from the system timer DefWindowProc gets. */
BOOL UserCaretTimer(HWND hwnd, WPARAM id)
{
    if (id != CARET_TIMER)
        return FALSE;
    CARET *c = owned();
    if (c && c->hwnd == hwnd && !c->hidden)
        toggle(c);
    return TRUE;
}

/* painting a window hides its caret until it is done. */
void UserCaretPaint(HWND hwnd, BOOL begin)
{
    CARET *c = owned();
    if (c && c->hwnd == hwnd) {
        if (begin)
            HideCaret(hwnd);
        else
            ShowCaret(hwnd);
    }
}

/* a window going away takes its thread's caret with it, its children's too. */
void UserCaretDestroyed(HWND hwnd)
{
    CARET *c = owned();
    if (c && (c->hwnd == hwnd || IsChild(hwnd, c->hwnd))) {
        kill_timer(c);
        c->on = FALSE;
        c->hwnd = nullptr;
    }
}

/* the window and rectangle of the thread's caret, for GetGUIThreadInfo. */
HWND UserCaretInfo(RECT *rect)
{
    CARET *c = owned();
    if (!c)
        return nullptr;
    SetRect(rect, c->x, c->y, c->x + c->width, c->y + c->height);
    return c->hwnd;
}

/* what a thread's windows are doing; only the calling thread's caret is known here. */
WINUSERAPI BOOL WINAPI GetGUIThreadInfo(DWORD thread, PGUITHREADINFO info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset((BYTE *)info + sizeof(info->cbSize), 0, sizeof(*info) - sizeof(info->cbSize));
    if (thread && thread != GetCurrentThreadId()) {
        HWND foreground = GetForegroundWindow();
        if (foreground && GetWindowThreadProcessId(foreground, nullptr) == thread)
            info->hwndActive = foreground;
        return TRUE;
    }
    info->hwndActive = GetActiveWindow();
    info->hwndFocus = GetFocus();
    info->hwndCapture = GetCapture();
    info->hwndCaret = UserCaretInfo(&info->rcCaret);
    CARET *c = owned();
    if (c && !c->hidden)
        info->flags |= GUI_CARETBLINKING;
    return TRUE;
}
