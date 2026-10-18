/*
 * messages: sending, posting, retrieval, dispatch and the default window
 * procedure, which win32k implements.
 */
#include "user32_private.h"

WINUSERAPI LRESULT WINAPI SendMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return UserSendMessage(hwnd, msg, wparam, lparam, FALSE);
}

WINUSERAPI LRESULT WINAPI SendMessageA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return UserSendMessage(hwnd, msg, wparam, lparam, TRUE);
}

WINUSERAPI LRESULT WINAPI SendMessageTimeoutW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT flags,
                                              UINT timeout, ULONG_PTR *result)
{
    MIME_SEND_TIMEOUT st = {flags, timeout, 0};
    LRESULT ok = NtUserMessageCall(hwnd, msg, wparam, lparam, (ULONG_PTR)&st, MIME_MSGCALL_SENDTIMEOUT, FALSE);
    if (result)
        *result = st.Result;
    return ok;
}

WINUSERAPI BOOL WINAPI SendNotifyMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    NtUserMessageCall(hwnd, msg, wparam, lparam, 0, MIME_MSGCALL_SENDNOTIFY, FALSE);
    return TRUE;
}

WINUSERAPI BOOL WINAPI PostMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return NtUserPostMessage(hwnd, msg, wparam, lparam);
}

WINUSERAPI BOOL WINAPI PostMessageA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return NtUserPostMessage(hwnd, msg, wparam, lparam);
}

WINUSERAPI BOOL WINAPI PostThreadMessageW(DWORD thread, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return NtUserPostThreadMessage(thread, msg, wparam, lparam);
}

WINUSERAPI BOOL WINAPI PostThreadMessageA(DWORD thread, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return NtUserPostThreadMessage(thread, msg, wparam, lparam);
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
    if ((msg->message == WM_CHAR || msg->message == WM_SYSCHAR) && msg->wParam > 0x7F) {
        WCHAR wc = (WCHAR)msg->wParam;
        char c = '?';
        WideCharToMultiByte(CP_ACP, 0, &wc, 1, &c, 1, nullptr, nullptr);
        msg->wParam = (UCHAR)c;
    }
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
    if ((m.message == WM_CHAR || m.message == WM_SYSCHAR) && m.wParam > 0x7F) {
        char c = (char)m.wParam;
        WCHAR wc;
        MultiByteToWideChar(CP_ACP, 0, &c, 1, &wc, 1);
        m.wParam = wc;
    }
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

WINUSERAPI BOOL WINAPI InSendMessage(void)
{
    return FALSE;
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
    switch (msg) {
    case WM_NCCREATE:
    case WM_CREATE: {
        CREATESTRUCTA *csa = (CREATESTRUCTA *)lparam;
        CREATESTRUCTW csw;
        memcpy(&csw, csa, sizeof(csw));
        csw.lpszName = UserAtoW(csa->lpszName);
        csw.lpszClass = UserAtoW(csa->lpszClass);
        LRESULT r = DefWindowProcW(hwnd, msg, wparam, (LPARAM)&csw);
        UserFreeConverted(csw.lpszName, csa->lpszName);
        UserFreeConverted(csw.lpszClass, csa->lpszClass);
        return r;
    }
    case WM_SETTEXT: {
        LPWSTR w = UserAtoW((LPCSTR)lparam);
        LRESULT r = DefWindowProcW(hwnd, msg, wparam, (LPARAM)w);
        UserFreeConverted(w, (void *)lparam);
        return r;
    }
    case WM_GETTEXT: {
        if (!wparam || !lparam)
            return 0;
        LPWSTR w = UserAlloc(wparam * sizeof(WCHAR));
        if (!w)
            return 0;
        LRESULT n = DefWindowProcW(hwnd, msg, wparam, (LPARAM)w);
        int a = WideCharToMultiByte(CP_ACP, 0, w, (int)n, (LPSTR)lparam, (int)wparam - 1, nullptr, nullptr);
        ((LPSTR)lparam)[a] = 0;
        UserFree(w);
        return a;
    }
    case WM_GETTEXTLENGTH: {
        int n = (int)DefWindowProcW(hwnd, msg, wparam, lparam);
        LPWSTR w = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
        if (!w)
            return n;
        DefWindowProcW(hwnd, WM_GETTEXT, (WPARAM)n + 1, (LPARAM)w);
        int a = WideCharToMultiByte(CP_ACP, 0, w, n, nullptr, 0, nullptr, nullptr);
        UserFree(w);
        return a;
    }
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

WINUSERAPI LRESULT WINAPI CallWindowProcW(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return proc ? proc(hwnd, msg, wparam, lparam) : 0;
}

WINUSERAPI LRESULT WINAPI CallWindowProcA(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return proc ? proc(hwnd, msg, wparam, lparam) : 0;
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
