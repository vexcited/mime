/*
 * messages: sending, posting, retrieval, dispatch and the default window
 * procedure, which win32k implements.
 */
#include "user32_private.h"
#include <mime/msgtable.h>

WINUSERAPI LRESULT WINAPI SendMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return UserSendMessage(hwnd, msg, wparam, lparam, FALSE);
}

WINUSERAPI LRESULT WINAPI SendMessageA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return UserSendMessage(hwnd, msg, wparam, lparam, TRUE);
}

static LRESULT send_timeout(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT flags, UINT timeout,
                            ULONG_PTR *result, BOOL ansi)
{
    /* what windows refuses here it refuses silently, without an error. */
    if ((msg & 0xFFFE0000) || (flags & ~0x7Fu))
        return 0;
    if (result)
        *result = 0;
    MIME_SEND_TIMEOUT st = {flags, timeout, 0};
    LRESULT ok = UserMessageCall(hwnd, msg, wparam, lparam, (ULONG_PTR)&st, MIME_MSGCALL_SENDTIMEOUT, ansi);
    if (result)
        *result = st.Result;
    return ok;
}

WINUSERAPI LRESULT WINAPI SendMessageTimeoutW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT flags,
                                              UINT timeout, ULONG_PTR *result)
{
    return send_timeout(hwnd, msg, wparam, lparam, flags, timeout, result, FALSE);
}

WINUSERAPI LRESULT WINAPI SendMessageTimeoutA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT flags,
                                              UINT timeout, ULONG_PTR *result)
{
    return send_timeout(hwnd, msg, wparam, lparam, flags, timeout, result, TRUE);
}

WINUSERAPI BOOL WINAPI SendNotifyMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return (BOOL)UserMessageCall(hwnd, msg, wparam, lparam, 0, MIME_MSGCALL_SENDNOTIFY, FALSE);
}

WINUSERAPI BOOL WINAPI SendNotifyMessageA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return (BOOL)UserMessageCall(hwnd, msg, wparam, lparam, 0, MIME_MSGCALL_SENDNOTIFY, TRUE);
}

WINUSERAPI BOOL WINAPI SendMessageCallbackW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, SENDASYNCPROC proc,
                                            ULONG_PTR data)
{
    MIME_SEND_CALLBACK cb = {proc, data};
    return (BOOL)UserMessageCall(hwnd, msg, wparam, lparam, (ULONG_PTR)&cb, MIME_MSGCALL_SENDCALLBACK, FALSE);
}

WINUSERAPI BOOL WINAPI SendMessageCallbackA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, SENDASYNCPROC proc,
                                            ULONG_PTR data)
{
    MIME_SEND_CALLBACK cb = {proc, data};
    return (BOOL)UserMessageCall(hwnd, msg, wparam, lparam, (ULONG_PTR)&cb, MIME_MSGCALL_SENDCALLBACK, TRUE);
}

/* a directory listing posted to a list is filled in by the list when it gets there. */
static WPARAM post_wparam(UINT msg, WPARAM wparam)
{
    return msg == CB_DIR || msg == LB_DIR ? wparam | DDL_POSTMSGS : wparam;
}

static BOOL post(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    if (msg == WM_DROPFILES) {
        /* the memory of a drop is the sender's: the window's process makes a copy that is its own. */
        DWORD pid = 0;
        if (hwnd && GetWindowThreadProcessId(hwnd, &pid) && pid != GetCurrentProcessId()) {
            SIZE_T size = GlobalSize((HGLOBAL)wparam);
            wparam = (WPARAM)SendMessageW(hwnd, WM_COPYGLOBALDATA, size, (LPARAM)wparam);
            if (!wparam)
                return FALSE;
        }
    }
    if (ansi)
        wparam = UserCharWparam(msg, wparam, TRUE);
    return NtUserPostMessage(hwnd, msg, post_wparam(msg, wparam), lparam);
}

WINUSERAPI BOOL WINAPI PostMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return post(hwnd, msg, wparam, lparam, FALSE);
}

WINUSERAPI BOOL WINAPI PostMessageA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return post(hwnd, msg, wparam, lparam, TRUE);
}

WINUSERAPI BOOL WINAPI PostThreadMessageW(DWORD thread, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return NtUserPostThreadMessage(thread, msg, wparam, lparam);
}

WINUSERAPI BOOL WINAPI PostThreadMessageA(DWORD thread, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return NtUserPostThreadMessage(thread, msg, UserCharWparam(msg, wparam, TRUE), lparam);
}

/*
 * BroadcastSystemMessage: the recipients and flags are checked here, then every top level window gets the message
 * the way the flags say. it answers the number of recipients that took it, -1 if the message could not be sent,
 * and 0 if a query was denied.
 */
static long broadcast_system(DWORD flags, LPDWORD recipients, UINT msg, WPARAM wparam, LPARAM lparam, PBSMINFO info,
                             BOOL ansi)
{
    if ((msg & 0xFFFE0000) || (flags & ~0x7FFu) || ((flags & (BSF_RETURNHDESK | BSF_LUID)) && !info) ||
        (info && info->cbSize != sizeof(*info)) || (msg >= 0x400 && msg <= 0xBFFF)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (flags & BSF_FORCEIFHUNG)
        flags |= BSF_NOHANG;
    if (flags & BSF_QUERY)
        flags &= ~(BSF_POSTMESSAGE | BSF_SENDNOTIFYMESSAGE);
    if ((flags & (BSF_POSTMESSAGE | BSF_SENDNOTIFYMESSAGE)) && msg < 0x400 && MimeMsgIsSyncOnly(msg, wparam, TRUE)) {
        SetLastError(ERROR_MESSAGE_SYNC_ONLY);
        return 0;
    }
    DWORD who = recipients ? *recipients : 0;
    if (!(who & 0xF))
        who |= 0xF;
    if (who & 0xFFFFFFE0u) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!(who & BSM_APPLICATIONS))
        return -1;
    MIME_BROADCAST b = {who, flags, nullptr, nullptr};
    CONV c;
    BOOL converted = ansi && !(flags & BSF_POSTMESSAGE);
    if (converted)
        UserConvBegin(&c, nullptr, msg, wparam, lparam);
    LRESULT r = NtUserMessageCall(nullptr, msg, converted ? c.wparam : wparam, converted ? c.lparam : lparam,
                                  (ULONG_PTR)&b, MIME_MSGCALL_BROADCAST, FALSE);
    if (converted)
        UserConvEnd(&c, 0);
    if (recipients)
        *recipients = b.Recipients;
    if (!r && (flags & BSF_QUERY) && info) {
        info->hwnd = b.Hwnd;
        info->hdesk = b.Desktop;
    }
    return (long)r;
}

WINUSERAPI long WINAPI BroadcastSystemMessageW(DWORD flags, LPDWORD recipients, UINT msg, WPARAM wparam,
                                               LPARAM lparam)
{
    return broadcast_system(flags, recipients, msg, wparam, lparam, nullptr, FALSE);
}

WINUSERAPI long WINAPI BroadcastSystemMessageA(DWORD flags, LPDWORD recipients, UINT msg, WPARAM wparam,
                                               LPARAM lparam)
{
    return broadcast_system(flags, recipients, msg, wparam, lparam, nullptr, TRUE);
}

WINUSERAPI long WINAPI BroadcastSystemMessageExW(DWORD flags, LPDWORD recipients, UINT msg, WPARAM wparam,
                                                 LPARAM lparam, PBSMINFO info)
{
    return broadcast_system(flags, recipients, msg, wparam, lparam, info, FALSE);
}

WINUSERAPI long WINAPI BroadcastSystemMessageExA(DWORD flags, LPDWORD recipients, UINT msg, WPARAM wparam,
                                                 LPARAM lparam, PBSMINFO info)
{
    return broadcast_system(flags, recipients, msg, wparam, lparam, info, TRUE);
}

WINUSERAPI void WINAPI PostQuitMessage(int code)
{
    NtUserCallOneParam((ULONG_PTR)(LONG_PTR)code, MIME_ONEPARAM_POSTQUITMESSAGE);
}

WINUSERAPI BOOL WINAPI GetMessageW(LPMSG msg, HWND hwnd, UINT min, UINT max)
{
    return NtUserGetMessage(msg, hwnd, min, max);
}

static void msg_to_ansi(LPMSG msg)
{
    msg->wParam = UserCharWparam(msg->message, msg->wParam, FALSE);
}

WINUSERAPI BOOL WINAPI GetMessageA(LPMSG msg, HWND hwnd, UINT min, UINT max)
{
    BOOL r = NtUserGetMessage(msg, hwnd, min, max);
    if (r > 0)
        msg_to_ansi(msg);
    return r;
}

WINUSERAPI BOOL WINAPI PeekMessageW(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT flags)
{
    return NtUserPeekMessage(msg, hwnd, min, max, flags);
}

WINUSERAPI BOOL WINAPI PeekMessageA(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT flags)
{
    BOOL r = NtUserPeekMessage(msg, hwnd, min, max, flags);
    if (r)
        msg_to_ansi(msg);
    return r;
}

WINUSERAPI BOOL WINAPI TranslateMessage(const MSG *msg)
{
    return NtUserTranslateMessage(msg, 0);
}

static LRESULT dispatch(const MSG *msg)
{
    /* timer procedures run directly in the receiving thread. */
    if ((msg->message == WM_TIMER || msg->message == WM_SYSTIMER) && msg->lParam) {
        ((TIMERPROC)msg->lParam)(msg->hwnd, msg->message, msg->wParam, GetTickCount());
        return 0;
    }
    if (!msg->hwnd)
        return 0;
    return NtUserDispatchMessage(msg);
}

WINUSERAPI LRESULT WINAPI DispatchMessageW(const MSG *msg)
{
    return dispatch(msg);
}

WINUSERAPI LRESULT WINAPI DispatchMessageA(const MSG *msg)
{
    MSG m = *msg;
    m.wParam = UserCharWparam(m.message, m.wParam, TRUE);
    return dispatch(&m);
}

WINUSERAPI BOOL WINAPI WaitMessage(void)
{
    return NtUserWaitMessage();
}

WINUSERAPI DWORD WINAPI GetQueueStatus(UINT flags)
{
    return (DWORD)NtUserCallOneParam(flags, MIME_ONEPARAM_GETQUEUESTATUS);
}

WINUSERAPI BOOL WINAPI GetInputState(void)
{
    return HIWORD(GetQueueStatus(QS_INPUT)) != 0;
}

WINUSERAPI LONG WINAPI GetMessageTime(void)
{
    return (LONG)NtUserCallNoParam(MIME_NOPARAM_GETMESSAGETIME);
}

WINUSERAPI DWORD WINAPI GetMessagePos(void)
{
    return (DWORD)NtUserGetMessagePos();
}

WINUSERAPI LPARAM WINAPI GetMessageExtraInfo(void)
{
    return (LPARAM)NtUserCallNoParam(MIME_NOPARAM_GETMESSAGEEXTRAINFO);
}

WINUSERAPI DWORD WINAPI MsgWaitForMultipleObjectsEx(DWORD count, const HANDLE *handles, DWORD timeout, DWORD mask,
                                                    DWORD flags)
{
    if (!(flags & MWMO_WAITALL))
        return NtUserMsgWaitForMultipleObjectsEx(count, handles, timeout, mask, flags);
    /* all the handles and the queue: the handles in short slices so new messages are noticed. */
    DWORD start = GetTickCount();
    for (;;) {
        if (HIWORD(GetQueueStatus(mask)))
            return WAIT_OBJECT_0 + count;
        DWORD slice = 10;
        if (timeout != INFINITE) {
            DWORD elapsed = GetTickCount() - start;
            if (elapsed >= timeout)
                return WAIT_TIMEOUT;
            if (timeout - elapsed < slice)
                slice = timeout - elapsed;
        }
        if (count) {
            DWORD r = WaitForMultipleObjectsEx(count, handles, (flags & MWMO_WAITALL) != 0, slice,
                                               (flags & MWMO_ALERTABLE) != 0);
            if (r != WAIT_TIMEOUT)
                return r;
        } else {
            Sleep(slice);
        }
    }
}

WINUSERAPI DWORD WINAPI MsgWaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL wait_all, DWORD timeout,
                                                  DWORD mask)
{
    return MsgWaitForMultipleObjectsEx(count, handles, timeout, mask, wait_all ? MWMO_WAITALL : 0);
}

WINUSERAPI UINT WINAPI RegisterWindowMessageW(LPCWSTR name)
{
    UNICODE_STRING s;
    RtlInitUnicodeString(&s, name);
    return NtUserRegisterWindowMessage(&s);
}

WINUSERAPI UINT WINAPI RegisterWindowMessageA(LPCSTR name)
{
    LPWSTR w = UserAtoW(name);
    UINT r = RegisterWindowMessageW(w);
    UserFreeConverted(w, name);
    return r;
}

/* ------------------------------------------------------------------ window procedures */

WINUSERAPI LRESULT WINAPI DefWindowProcW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    LRESULT result;
    if (UserDefMoveSize(hwnd, msg, wparam, lparam, &result) || UserDefScrollBars(hwnd, msg, wparam, lparam, &result))
        return result;
    if (msg == WM_SYSTIMER && UserCaretTimer(hwnd, wparam))
        return 0;
    /* common controls ask their parent which character set their notifications use. */
    if (msg == WM_NOTIFYFORMAT)
        return lparam == NF_QUERY ? (IsWindowUnicode(hwnd) ? NFR_UNICODE : NFR_ANSI) : 0;
    return NtUserMessageCall(hwnd, msg, wparam, lparam, 0, MIME_MSGCALL_DEFWINDOWPROC, FALSE);
}

/*
 * multiple document interface: there is no MDIClient class, so a frame
 * only keeps its client window sized and focused, and children get the
 * plain window procedure.
 */
WINUSERAPI LRESULT WINAPI DefFrameProcW(HWND hwnd, HWND client, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (client && IsWindow(client)) {
        switch (msg) {
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED)
                MoveWindow(client, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
            return 0;
        case WM_SETFOCUS:
            SetFocus(client);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

WINUSERAPI LRESULT WINAPI DefMDIChildProcW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* without MDI children there is no window menu key to translate. */
WINUSERAPI BOOL WINAPI TranslateMDISysAccel(HWND client, LPMSG msg)
{
    (void)client;
    (void)msg;
    return FALSE;
}

WINUSERAPI LRESULT WINAPI DefWindowProcA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    CONV c;
    if (!UserConvBegin(&c, hwnd, msg, wparam, lparam))
        return c.refused ? c.refusal : 0;
    return UserConvEnd(&c, DefWindowProcW(hwnd, msg, c.wparam, c.lparam));
}

WINUSERAPI LRESULT WINAPI CallWindowProcW(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return proc ? proc(hwnd, msg, wparam, lparam) : 0;
}

/* the procedures user32 has for its classes take unicode, even when an ansi program holds them. */
static BOOL is_system_proc(WNDPROC proc)
{
    if (proc == DefWindowProcW || proc == DefDlgProcW)
        return TRUE;
    for (int i = 0; i < MIME_FNID_COUNT; i++)
        if (User32Client.Procs[i] == proc)
            return TRUE;
    return FALSE;
}

WINUSERAPI LRESULT WINAPI CallWindowProcA(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (!proc)
        return 0;
    if (!is_system_proc(proc))
        return proc(hwnd, msg, wparam, lparam);
    CONV c;
    if (!UserConvBegin(&c, hwnd, msg, wparam, lparam))
        return c.refused ? c.refusal : 0;
    return UserConvEnd(&c, proc(hwnd, msg, c.wparam, c.lparam));
}

/* ------------------------------------------------------------------ timers */

WINUSERAPI UINT_PTR WINAPI SetTimer(HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc)
{
    return NtUserSetTimer(hwnd, id, elapse, proc, 0);
}

WINUSERAPI BOOL WINAPI KillTimer(HWND hwnd, UINT_PTR id)
{
    return NtUserKillTimer(hwnd, id);
}

WINUSERAPI UINT_PTR WINAPI SetCoalescableTimer(HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc, ULONG tolerance)
{
    return NtUserSetTimer(hwnd, id, elapse, proc, tolerance);
}

WINUSERAPI BOOL WINAPI MessageBeep(UINT type)
{
    return NtUserMessageBeep(type);
}
