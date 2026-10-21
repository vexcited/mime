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

/* the flags of SendMessageTimeout that are not refused: the defined ones, and a few more that are passed on. */
#define SMTO_ACCEPTED_FLAGS 0x7F
#define MSG_BEYOND_ALL_MESSAGES 0x20000

/*
 * SendMessageTimeout. a message number beyond 17 bits, or a flag outside the accepted ones, fails with nothing said and
 * the result left as it was. otherwise the result is cleared and the window manager answers whether the message came
 * back in time.
 */
static LRESULT send_timeout(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT flags, UINT timeout,
                            ULONG_PTR *result, BOOL ansi)
{
    if ((msg & ~(UINT)(MSG_BEYOND_ALL_MESSAGES - 1)) || (flags & ~(UINT)SMTO_ACCEPTED_FLAGS))
        return 0;
    if (result)
        *result = 0;
    MIME_SEND_TIMEOUT call = {flags, timeout, 0};
    LRESULT arrived = UserMessageCall(hwnd, msg, wparam, lparam, (ULONG_PTR)&call, MIME_MSGCALL_SENDTIMEOUT, ansi);
    if (arrived && result)
        *result = call.Result;
    return arrived;
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

#define MSG_CB_DIR 0x0145
#define MSG_LB_DIR 0x018D
#define MSG_DROPFILES 0x0233
/* DDL_POSTMSGS: the list fills itself from the messages the control receives, so that a posted directory listing works. */
#define DIR_POST_MESSAGES 0x2000

/*
 * the handle of the files dropped is the sender's global memory, which a window of another process cannot read. the
 * window's process is asked to make a copy of the bytes, and the post carries the handle of the copy.
 */
static BOOL drop_handle_for_other_process(HWND hwnd, WPARAM *wparam)
{
    DWORD owner = 0;
    if (!hwnd || !GetWindowThreadProcessId(hwnd, &owner) || owner == GetCurrentProcessId())
        return TRUE;
    HGLOBAL sent = (HGLOBAL)*wparam;
    SIZE_T size = GlobalSize(sent);
    LRESULT copy = SendMessageW(hwnd, WM_COPYGLOBALDATA, size, (LPARAM)sent);
    if (!copy)
        return FALSE;
    *wparam = (WPARAM)copy;
    return TRUE;
}

static BOOL post(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    if (ansi)
        wparam = UserCharWparam(msg, wparam, TRUE);
    if (msg == MSG_CB_DIR || msg == MSG_LB_DIR)
        wparam |= DIR_POST_MESSAGES;
    else if (msg == MSG_DROPFILES && !drop_handle_for_other_process(hwnd, &wparam))
        return FALSE;
    return NtUserPostMessage(hwnd, msg, wparam, lparam);
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

/* what BroadcastSystemMessage may be asked for. */
#define BSF_DEFINED_FLAGS 0x7FF
#define BSM_COMPONENTS 0x0F
#define BSM_DEFINED_RECIPIENTS 0x1F
#define BSM_BAD_RECIPIENTS (~(ULONG)BSM_DEFINED_RECIPIENTS)
#define MSG_PROGRAM_FIRST 0x0400
#define MSG_PROGRAM_LAST 0xBFFF
#define BROADCAST_NOBODY ((long)-1)

static long refuse_broadcast(DWORD error)
{
    SetLastError(error);
    return 0;
}

/*
 * BroadcastSystemMessage and its Ex form. the parameters are judged here, in this order: the message number, the flags,
 * the structure the flags ask for, the size of that structure and the message being one a program defines for itself. a
 * query never posts, and a message that carries data of the sender cannot be posted or sent without waiting. the
 * answer is 0 for a refusal or a denied query, -1 when no recipient the system serves was named, and else positive.
 */
static long broadcast_system(DWORD flags, LPDWORD recipients, UINT msg, WPARAM wparam, LPARAM lparam, PBSMINFO info,
                             BOOL ansi)
{
    if (msg & ~(UINT)(MSG_BEYOND_ALL_MESSAGES - 1))
        return refuse_broadcast(ERROR_INVALID_PARAMETER);
    if (flags & ~(DWORD)BSF_DEFINED_FLAGS)
        return refuse_broadcast(ERROR_INVALID_PARAMETER);
    if ((flags & (BSF_RETURNHDESK | BSF_LUID)) && !info)
        return refuse_broadcast(ERROR_INVALID_PARAMETER);
    if (info && info->cbSize != sizeof(BSMINFO))
        return refuse_broadcast(ERROR_INVALID_PARAMETER);
    if (msg >= MSG_PROGRAM_FIRST && msg <= MSG_PROGRAM_LAST)
        return refuse_broadcast(ERROR_INVALID_PARAMETER);

    if (flags & BSF_FORCEIFHUNG)
        flags |= BSF_NOHANG;
    if (flags & BSF_QUERY)
        flags &= ~(DWORD)(BSF_POSTMESSAGE | BSF_SENDNOTIFYMESSAGE);
    if ((flags & (BSF_POSTMESSAGE | BSF_SENDNOTIFYMESSAGE)) && msg < MSG_PROGRAM_FIRST &&
        MimeMsgIsSyncOnly(msg, wparam, FALSE))
        return refuse_broadcast(ERROR_MESSAGE_SYNC_ONLY);

    ULONG named = recipients ? *recipients : 0;
    if (!(named & BSM_COMPONENTS))
        named |= BSM_COMPONENTS;
    if (named & BSM_BAD_RECIPIENTS)
        return refuse_broadcast(ERROR_INVALID_PARAMETER);
    if (!(named & BSM_APPLICATIONS))
        return BROADCAST_NOBODY;

    MIME_BROADCAST call = {named, flags, nullptr, nullptr};
    long result = (long)UserMessageCall(nullptr, msg, wparam, lparam, (ULONG_PTR)&call, MIME_MSGCALL_BROADCAST, ansi);
    if (recipients)
        *recipients = call.Recipients;
    if (!result && (flags & BSF_QUERY) && info) {
        info->hwnd = call.Hwnd;
        info->hdesk = call.Desktop;
    }
    return result;
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
    /* a drag from or to the host's programs, run by the code of the process that takes it. */
    if (msg->message == WM_MIME_HOSTDND) {
        UserHostDnd(msg);
        return 0;
    }
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
    /* a copy of memory of another process, which the message brought, as a global handle of this one. */
    if (msg == WM_COPYGLOBALDATA) {
        if (!wparam || !lparam)
            return 0;
        HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, wparam);
        void *data = copy ? GlobalLock(copy) : nullptr;
        if (!data) {
            GlobalFree(copy);
            return 0;
        }
        memcpy(data, (const void *)lparam, wparam);
        GlobalUnlock(copy);
        return (LRESULT)copy;
    }
    if (UserDefMoveSize(hwnd, msg, wparam, lparam, &result) || UserDefScrollBars(hwnd, msg, wparam, lparam, &result) ||
        UserDefSystemMenu(hwnd, msg, wparam, lparam, &result))
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
