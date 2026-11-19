/*
 * the state win32k keeps for user32 26300.9457 that the old user32 of mime kept itself: the caret of a thread, the
 * scroll bars of a window, accelerator tables, the keyboard layout a thread reads and the flags of the startup
 * information. the services take the arguments user32 gives, read in IDA from user32 and win32kfull.
 */
#include "win32k.h"

/* ------------------------------------------------------------------ the caret */

#define CARET_TIMER 0xFFFF
#define CARET_BLINK_DEFAULT 530

static UINT g_caret_blink = CARET_BLINK_DEFAULT;

static WND *caret_window(W32THREAD *t)
{
    return t && t->Caret.Hwnd ? WndLookup(t->Caret.Hwnd) : nullptr;
}

/* the caret is drawn by inverting its rectangle in the client area, which a second time takes it away. */
static void caret_invert(W32THREAD *t)
{
    WND *w = caret_window(t);
    if (!w || !WndIsVisible(w))
        return;
    HDC hdc = PaintGetDC(w, nullptr, DCX_CACHE | DCX_USESTYLE);
    if (!hdc)
        return;
    DC *dc = DcLookup(hdc);
    if (dc) {
        GdiPatBltDc(dc, t->Caret.X, t->Caret.Y, t->Caret.Width, t->Caret.Height, DSTINVERT, nullptr);
        PaintReleaseDC(dc);
    }
    WND *top = WndTopLevel(w);
    if (top && top->Thread == W32GetThread())
        PaintPresent(top);
}

static void caret_off(W32THREAD *t)
{
    if (t->Caret.On) {
        caret_invert(t);
        t->Caret.On = FALSE;
    }
}

static void caret_restart(W32THREAD *t)
{
    if (!t->Caret.Hwnd)
        return;
    if (t->Caret.Hide == 0 && !t->Caret.On) {
        caret_invert(t);
        t->Caret.On = TRUE;
    }
    if (g_caret_blink != (UINT)-1)
        TimerSet(t, t->Caret.Hwnd, CARET_TIMER, g_caret_blink, nullptr, TRUE);
}

/* the blink of the system timer of the caret, which win32k answers itself and does not deliver. */
BOOLEAN CaretTimer(W32THREAD *t, HWND hwnd, UINT_PTR id)
{
    if (id != CARET_TIMER || !t || hwnd != t->Caret.Hwnd)
        return FALSE;
    if (t->Caret.Hide == 0) {
        caret_invert(t);
        t->Caret.On = !t->Caret.On;
    }
    return TRUE;
}

void CaretDestroyThread(W32THREAD *t)
{
    if (t->Caret.Hwnd)
        TimerKill(t, t->Caret.Hwnd, CARET_TIMER, TRUE);
    memset(&t->Caret, 0, sizeof(t->Caret));
}

/* a new caret replaces the one of the thread and starts hidden. a bitmap of 1 is the gray caret. */
BOOL NTAPI NtUserCreateCaret(HWND hwnd, HBITMAP bitmap, int width, int height)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (t && w && w->Thread == t) {
        caret_off(t);
        if (t->Caret.Hwnd)
            TimerKill(t, t->Caret.Hwnd, CARET_TIMER, TRUE);
        t->Caret.Hwnd = hwnd;
        t->Caret.Bitmap = bitmap;
        t->Caret.Width = width ? width : MetricsGet(SM_CXBORDER, 96);
        t->Caret.Height = height ? height : MetricsGet(SM_CYBORDER, 96);
        t->Caret.X = t->Caret.Y = 0;
        t->Caret.Hide = 1;
        t->Caret.On = FALSE;
        ok = TRUE;
    } else {
        W32SetLastError(w ? ERROR_ACCESS_DENIED : ERROR_INVALID_WINDOW_HANDLE);
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserDestroyCaret(void)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOL ok = t && t->Caret.Hwnd;
    if (ok) {
        caret_off(t);
        CaretDestroyThread(t);
    }
    UserLeave();
    return ok;
}

/* hiding counts: the caret shows again once it was shown as many times as it was hidden. */
BOOL NTAPI NtUserHideCaret(HWND hwnd)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOL ok = t && t->Caret.Hwnd && (!hwnd || hwnd == t->Caret.Hwnd);
    if (ok) {
        caret_off(t);
        t->Caret.Hide++;
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserShowCaret(HWND hwnd)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOL ok = t && t->Caret.Hwnd && (!hwnd || hwnd == t->Caret.Hwnd) && t->Caret.Hide > 0;
    if (ok && --t->Caret.Hide == 0)
        caret_restart(t);
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserSetCaretPos(int x, int y)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOL ok = t && t->Caret.Hwnd;
    if (ok && (t->Caret.X != x || t->Caret.Y != y)) {
        caret_off(t);
        t->Caret.X = x;
        t->Caret.Y = y;
        caret_restart(t);
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserGetCaretPos(POINT *user_point)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    POINT p = {t ? t->Caret.X : 0, t ? t->Caret.Y : 0};
    BOOL ok = t && t->Caret.Hwnd;
    UserLeave();
    if (!ok) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return NT_SUCCESS(MimeCopyToUser(user_point, &p, sizeof(p)));
}

UINT NTAPI NtUserGetCaretBlinkTime(void)
{
    return g_caret_blink;
}

BOOL NTAPI NtUserSetCaretBlinkTime(UINT ms)
{
    g_caret_blink = ms;
    return TRUE;
}

/* ------------------------------------------------------------------ scroll bars */

static SCROLL_DATA *scroll_bar(WND *w, int bar)
{
    if (bar != SB_HORZ && bar != SB_VERT)
        return nullptr;
    if (!w->Scroll) {
        w->Scroll = W32Alloc(sizeof(*w->Scroll));
        if (!w->Scroll)
            return nullptr;
        w->Scroll->Bar[0].Max = w->Scroll->Bar[1].Max = 100;
    }
    return &w->Scroll->Bar[bar == SB_VERT];
}

/* the position the scroll bar can reach: from its minimum to its maximum less one page. */
static void scroll_clamp(SCROLL_DATA *d)
{
    LONG range = d->Max - d->Min;
    if (d->Page > (UINT)range + 1)
        d->Page = (UINT)range + 1;
    LONG top = d->Max - (d->Page ? (LONG)d->Page - 1 : 0);
    if (d->Pos > top)
        d->Pos = top;
    if (d->Pos < d->Min)
        d->Pos = d->Min;
}

static BOOLEAN scroll_needed(const SCROLL_DATA *d)
{
    return d->Max > d->Min && (!d->Page || (LONG)d->Page <= d->Max - d->Min);
}

/*
 * SetScrollInfo of a scroll bar of the frame: the range, the page and the position SIF_* name, clamped as windows
 * does, and the bar shown or hidden as the range needs, or kept and disabled with SIF_DISABLENOSCROLL. the answer is
 * the position. a scroll bar control is told with SBM_SETSCROLLINFO.
 */
int NTAPI NtUserSetScrollInfo(HWND hwnd, int bar, const SCROLLINFO *user_info, BOOL redraw)
{
    SCROLLINFO si;
    if (!NT_SUCCESS(MimeCopyFromUser(&si, user_info, sizeof(si))) ||
        (si.cbSize != sizeof(si) && si.cbSize != sizeof(si) - sizeof(si.nTrackPos)) || (si.fMask & ~0x1F)) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UserEnter();
    WND *w = WndLookup(hwnd);
    int result = 0;
    if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    } else if (bar == SB_CTL) {
        result = (int)MsgSendKernel(w, SBM_SETSCROLLINFO, redraw, (LPARAM)&si, MIME_LPARAM_INOUT, sizeof(si));
    } else {
        SCROLL_DATA *d = scroll_bar(w, bar);
        if (!d) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
        } else {
            if (si.fMask & SIF_RANGE) {
                d->Min = si.nMin;
                d->Max = si.nMax < si.nMin ? si.nMin : si.nMax;
            }
            if (si.fMask & SIF_PAGE)
                d->Page = si.nPage;
            if (si.fMask & SIF_POS)
                d->Pos = si.nPos;
            scroll_clamp(d);
            result = d->Pos;
            DWORD style = bar == SB_VERT ? WS_VSCROLL : WS_HSCROLL;
            BOOLEAN shown = (w->Style & style) != 0;
            BOOLEAN needed = scroll_needed(d) || (si.fMask & SIF_DISABLENOSCROLL);
            if ((si.fMask & (SIF_RANGE | SIF_PAGE)) && shown != needed) {
                w->Style = needed ? w->Style | style : w->Style & ~style;
                WndSetPos(w, nullptr, 0, 0, 0, 0,
                          SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            } else if (redraw && shown) {
                w->Flags |= WNDF_FRAME_DIRTY;
                WndInvalidateRegion(w, nullptr, RDW_FRAME | RDW_INVALIDATE);
            }
        }
    }
    UserLeave();
    return result;
}

/* SBGetParms: what GetScrollInfo asks of a bar of the frame, by the SIF_* of the structure. */
BOOL NTAPI NtUserSBGetParms(HWND hwnd, int bar, PVOID data, SCROLLINFO *user_info)
{
    SCROLLINFO si;
    if (!NT_SUCCESS(MimeCopyFromUser(&si, user_info, sizeof(si))))
        return FALSE;
    (void)data;
    UserEnter();
    WND *w = WndLookup(hwnd);
    SCROLL_DATA *d = w && w->Scroll && (bar == SB_HORZ || bar == SB_VERT) ? &w->Scroll->Bar[bar == SB_VERT] : nullptr;
    BOOL ok = d != nullptr;
    if (d) {
        if (si.fMask & SIF_RANGE) {
            si.nMin = d->Min;
            si.nMax = d->Max;
        }
        if (si.fMask & SIF_PAGE)
            si.nPage = d->Page;
        if (si.fMask & SIF_POS)
            si.nPos = d->Pos;
        if (si.fMask & SIF_TRACKPOS)
            si.nTrackPos = d->Pos;
    } else {
        W32SetLastError(1447); /* ERROR_NO_SCROLLBARS */
    }
    UserLeave();
    return ok && NT_SUCCESS(MimeCopyToUser(user_info, &si, si.cbSize <= sizeof(si) ? si.cbSize : sizeof(si)));
}

BOOL NTAPI NtUserShowScrollBar(HWND hwnd, int bar, BOOL show)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = w != nullptr;
    if (w) {
        DWORD style = bar == SB_VERT ? WS_VSCROLL : bar == SB_HORZ ? WS_HSCROLL : bar == SB_BOTH ? WS_VSCROLL | WS_HSCROLL : 0;
        DWORD wanted = show ? w->Style | style : w->Style & ~style;
        if (bar == SB_CTL) {
            WndShow(w, show ? SW_SHOW : SW_HIDE);
        } else if (wanted != w->Style) {
            w->Style = wanted;
            WndSetPos(w, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    } else {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserEnableScrollBar(HWND hwnd, UINT bar, UINT arrows)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (w && bar == SB_CTL) {
        ok = (BOOL)MsgSend(w, SBM_ENABLE_ARROWS, arrows, 0);
    } else if (w && (bar == SB_HORZ || bar == SB_VERT || bar == SB_BOTH) && scroll_bar(w, SB_VERT)) {
        ULONG old = w->Scroll->Disable;
        if (bar != SB_VERT)
            w->Scroll->Disable = (w->Scroll->Disable & ~0x3u) | (arrows & 0x3);
        if (bar != SB_HORZ)
            w->Scroll->Disable = (w->Scroll->Disable & ~0xCu) | ((arrows & 0x3) << 2);
        ok = old != w->Scroll->Disable;
        if (ok) {
            w->Flags |= WNDF_FRAME_DIRTY;
            WndInvalidateRegion(w, nullptr, RDW_FRAME | RDW_INVALIDATE);
        }
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserGetScrollBarInfo(HWND hwnd, LONG object, SCROLLBARINFO *user_info)
{
    SCROLLBARINFO info;
    if (!NT_SUCCESS(MimeCopyFromUser(&info.cbSize, &user_info->cbSize, sizeof(info.cbSize))) ||
        info.cbSize != sizeof(info)) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (w && object == OBJID_CLIENT) {
        UserLeave();
        return (BOOL)NtUserMessageCall(hwnd, SBM_GETSCROLLBARINFO, 0, (LPARAM)user_info, 0, FNID_SENDMESSAGE, FALSE);
    }
    if (w && (object == OBJID_VSCROLL || object == OBJID_HSCROLL)) {
        BOOLEAN vert = object == OBJID_VSCROLL;
        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        RECT r = w->WindowRect, client = w->ClientRect;
        LONG thick = MetricsGet(vert ? SM_CXVSCROLL : SM_CYHSCROLL, WndGetDpi(w));
        if (vert)
            info.rcScrollBar = (RECT){client.right, client.top, client.right + thick, client.bottom};
        else
            info.rcScrollBar = (RECT){client.left, client.bottom, client.right, client.bottom + thick};
        info.dxyLineButton = thick;
        if (!(w->Style & (vert ? WS_VSCROLL : WS_HSCROLL)))
            info.rgstate[0] = STATE_SYSTEM_INVISIBLE;
        (void)r;
        ok = TRUE;
    }
    UserLeave();
    if (!ok) {
        W32SetLastError(w ? ERROR_INVALID_PARAMETER : ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    return NT_SUCCESS(MimeCopyToUser(user_info, &info, sizeof(info)));
}

/* ------------------------------------------------------------------ accelerator tables */

/* the ACCEL of windows: the flags, a byte of padding, the key and the command. */
typedef struct ACCEL_ENTRY {
    BYTE Virt;
    BYTE Pad;
    WORD Key;
    WORD Cmd;
} ACCEL_ENTRY;

typedef struct ACCEL_TABLE {
    USER_OBJECT Header;
    W32PROCESS *Process;
    ULONG Count;
    ACCEL_ENTRY Entries[];
} ACCEL_TABLE;

#define ACCEL_MAX 0x8000

HACCEL NTAPI NtUserCreateAcceleratorTable(const ACCEL_ENTRY *user_entries, int count)
{
    if (count <= 0 || count > ACCEL_MAX) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    ACCEL_TABLE *a = W32Alloc(sizeof(ACCEL_TABLE) + (SIZE_T)count * sizeof(ACCEL_ENTRY));
    if (!a) {
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    if (!NT_SUCCESS(MimeCopyFromUser(a->Entries, user_entries, (SIZE_T)count * sizeof(ACCEL_ENTRY)))) {
        W32Free(a);
        W32SetLastError(ERROR_NOACCESS);
        return nullptr;
    }
    a->Count = (ULONG)count;
    UserEnter();
    a->Process = W32GetProcess();
    HACCEL h = (HACCEL)UserCreateHandle(&a->Header, USER_TYPE_ACCELTABLE);
    UserLeave();
    if (!h)
        W32Free(a);
    return h;
}

BOOL NTAPI NtUserDestroyAcceleratorTable(HACCEL h)
{
    UserEnter();
    ACCEL_TABLE *a = (ACCEL_TABLE *)UserLookup(h, USER_TYPE_ACCELTABLE);
    if (a) {
        a->Header.Destroyed = TRUE;
        UserDereference(&a->Header);
    }
    UserLeave();
    return a != nullptr;
}

/* the entries, or their count when there is no room for them. */
int NTAPI NtUserCopyAcceleratorTable(HACCEL h, ACCEL_ENTRY *user_entries, int count)
{
    UserEnter();
    ACCEL_TABLE *a = (ACCEL_TABLE *)UserLookup(h, USER_TYPE_ACCELTABLE);
    int n = 0;
    if (!a) {
        W32SetLastError(ERROR_INVALID_ACCEL_HANDLE);
    } else if (!user_entries) {
        n = (int)a->Count;
    } else {
        n = count < (int)a->Count ? count : (int)a->Count;
        if (n > 0 && !NT_SUCCESS(MimeCopyToUser(user_entries, a->Entries, (SIZE_T)n * sizeof(ACCEL_ENTRY))))
            n = 0;
    }
    UserLeave();
    return n;
}

/*
 * TranslateAccelerator: a key message that matches an entry becomes WM_COMMAND, or WM_SYSCOMMAND for FVIRTKEY
 * entries of the system menu, sent to the window with 1 in the high word of wparam. a character entry matches
 * WM_CHAR, a virtual key one WM_KEYDOWN and WM_SYSKEYDOWN with the shift states it asks.
 */
int NTAPI NtUserTranslateAccelerator(HWND hwnd, HACCEL h, const MSG *user_msg)
{
    MSG m;
    if (!user_msg || !NT_SUCCESS(MimeCopyFromUser(&m, user_msg, sizeof(m))))
        return 0;
    if (m.message != WM_KEYDOWN && m.message != WM_SYSKEYDOWN && m.message != WM_CHAR && m.message != WM_SYSCHAR)
        return 0;
    UserEnter();
    ACCEL_TABLE *a = (ACCEL_TABLE *)UserLookup(h, USER_TYPE_ACCELTABLE);
    WND *w = WndLookup(hwnd);
    int done = 0;
    if (a && w) {
        BOOLEAN virt = m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN;
        W32THREAD *keys = W32InputOwner(W32GetThread());
        BOOLEAN shift = keys && (keys->KeyState[VK_SHIFT] & 0x80), ctrl = keys && (keys->KeyState[VK_CONTROL] & 0x80);
        BOOLEAN alt = (m.lParam & 0x20000000) != 0;
        for (ULONG i = 0; i < a->Count && !done; i++) {
            const ACCEL_ENTRY *e = &a->Entries[i];
            if (((e->Virt & FVIRTKEY) != 0) != virt || e->Key != (WORD)m.wParam)
                continue;
            if (virt && (((e->Virt & FSHIFT) != 0) != shift || ((e->Virt & FCONTROL) != 0) != ctrl))
                continue;
            if (((e->Virt & FALT) != 0) != alt)
                continue;
            UserReference(&w->Header);
            MsgSend(w, WM_COMMAND, MAKEWPARAM(e->Cmd, 1), 0);
            UserDereference(&w->Header);
            done = 1;
        }
    }
    UserLeave();
    return done;
}

/* ------------------------------------------------------------------ keyboard layouts */

/* the layout of a thread of the reference machine, english of the united states: its client information had it. */
#define LAYOUT_EN_US ((HKL)(ULONG_PTR)0x04090409)

HKL NTAPI NtUserGetKeyboardLayout(DWORD thread)
{
    (void)thread;
    return LAYOUT_EN_US;
}

UINT NTAPI NtUserGetKeyboardLayoutList(int count, HKL *user_list)
{
    if (count <= 0 || !user_list)
        return 1;
    HKL one = LAYOUT_EN_US;
    return NT_SUCCESS(MimeCopyToUser(user_list, &one, sizeof(one))) ? 1 : 0;
}

/* ------------------------------------------------------------------ the startup information */

/*
 * MessageBoxWorker sets STARTF_USESHOWWINDOW off and so on: the bits of set are added to the flags of the startup
 * information the process was given, those of clear taken away, and the old flags come back.
 */
DWORD NTAPI NtUserModifyUserStartupInfoFlags(DWORD set, DWORD clear)
{
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    DWORD old = p ? p->StartupFlags : 0;
    if (p)
        p->StartupFlags = (old | set) & ~clear;
    UserLeave();
    return old;
}

/* ------------------------------------------------------------------ FillWindow */

static HBRUSH control_color(WND *parent, WND *w, HDC hdc, UINT msg);

/*
 * xxxFillWindow: the client area of a window filled with a brush aligned to its parent, as DefDlgProc erases a
 * dialog. a brush of CTLCOLOR_MAX or less is a kind of control whose color the parent is asked for, as
 * DefDlgProc gives CTLCOLOR_DLG.
 */
BOOL NTAPI NtUserFillWindow(HWND parent_h, HWND hwnd, HDC hdc, HBRUSH brush)
{
    UserEnter();
    WND *w = WndLookup(hwnd), *parent = WndLookup(parent_h);
    if (w && (ULONG_PTR)brush <= 8)
        brush = control_color(parent ? parent : w, w, hdc, WM_CTLCOLORMSGBOX + (UINT)(ULONG_PTR)brush);
    DC *dc = DcLookup(hdc);
    BRUSH *b = brush ? (BRUSH *)GdiLookup(brush, GDI_TYPE_BRUSH) : nullptr;
    BOOL ok = w && dc && b;
    if (ok) {
        RECT r = w->ClientRect;
        GdiPatBltDc(dc, 0, 0, r.right - r.left, r.bottom - r.top, PATCOPY, b);
    }
    UserLeave();
    return ok;
}

/* ------------------------------------------------------------------ colors of controls */

/*
 * xxxGetControlColor: the parent is asked with WM_CTLCOLOR*, and when its answer is no brush the default processing
 * of the parent answers instead.
 */
static HBRUSH control_color(WND *parent, WND *w, HDC hdc, UINT msg)
{
    UserReference(&parent->Header);
    HBRUSH b = (HBRUSH)MsgSend(parent, msg, (WPARAM)hdc, (LPARAM)WndHandle(w));
    if (!parent->Header.Destroyed && (!b || !GdiLookup(b, GDI_TYPE_BRUSH)))
        b = (HBRUSH)DefWindowProcKernel(parent, msg, (WPARAM)hdc, (LPARAM)WndHandle(w));
    UserDereference(&parent->Header);
    return b;
}

HBRUSH NTAPI NtUserGetControlColor(HWND parent_h, HWND hwnd, HDC hdc, UINT msg)
{
    UserEnter();
    WND *parent = WndLookup(parent_h), *w = WndLookup(hwnd);
    HBRUSH b = parent && w && msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC ? control_color(parent, w, hdc, msg)
                                                                                 : nullptr;
    UserLeave();
    return b;
}

/* xxxGetControlBrush: the parent of a child, the owner of another window, or the window itself is asked. */
HBRUSH NTAPI NtUserGetControlBrush(HWND hwnd, HDC hdc, UINT msg)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    HBRUSH b = nullptr;
    if (w && msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC) {
        WND *to = (w->Style & WS_CHILD) ? w->Parent : w->Owner;
        if (!to || to == WndDesktop())
            to = w;
        if (to->Thread && w->Thread && to->Thread->Process != W32GetProcess())
            b = (HBRUSH)DefWindowProcKernel(w, msg, (WPARAM)hdc, (LPARAM)hwnd);
        else
            b = control_color(to, w, hdc, msg);
    }
    UserLeave();
    return b;
}

/* ------------------------------------------------------------------ dialogs and message boxes */

/* the system menu of a dialog is made when it is first asked for, as for any window with a system menu. */
BOOL NTAPI NtUserSetDialogSystemMenu(HWND hwnd)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    UserLeave();
    return w != nullptr;
}

/* a message box, WNDS_MSGBOX: its default processing beeps and closes as message boxes do. */
BOOL NTAPI NtUserSetMsgBox(HWND hwnd)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (w)
        w->Flags |= WNDF_MSGBOX;
    UserLeave();
    return w != nullptr;
}

/* the events of accessibility: no hook of WinEvent listens in mime. */
void NTAPI NtUserNotifyWinEvent(DWORD event, HWND hwnd, LONG object, LONG child)
{
    (void)event, (void)hwnd, (void)object, (void)child;
}

/* ------------------------------------------------------------------ the mouse */

/* the time two clicks make a double click in, which the input of win32k uses too. */
UINT NTAPI NtUserGetDoubleClickTime(void)
{
    return 500;
}
