/*
 * user32 initialization, the kernel callback table and the translation of
 * messages between ansi and unicode window procedures.
 */
#include "user32_private.h"

HINSTANCE User32Instance;
MIME_CLIENT_INFO User32Client;

/* ------------------------------------------------------------------ string conversion */

LPWSTR UserAtoWn(LPCSTR s, int count, int *out_count)
{
    if (count < 0)
        count = (int)strlen(s);
    int n = count ? MultiByteToWideChar(CP_ACP, 0, s, count, NULL, 0) : 0;
    LPWSTR w = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!w)
        return NULL;
    if (n)
        MultiByteToWideChar(CP_ACP, 0, s, count, w, n);
    w[n] = 0;
    if (out_count)
        *out_count = n;
    return w;
}

LPWSTR UserAtoW(LPCSTR s)
{
    if (!s || IS_INTRESOURCE(s))
        return (LPWSTR)s;
    return UserAtoWn(s, -1, NULL);
}

LPSTR UserWtoA(LPCWSTR s)
{
    if (!s || IS_INTRESOURCE(s))
        return (LPSTR)s;
    int n = WideCharToMultiByte(CP_ACP, 0, s, -1, NULL, 0, NULL, NULL);
    LPSTR a = UserAlloc((SIZE_T)(n ? n : 1));
    if (a)
        WideCharToMultiByte(CP_ACP, 0, s, -1, a, n, NULL, NULL);
    return a;
}

/* ------------------------------------------------------------------ message translation */

static BOOL is_char_message(UINT msg)
{
    return msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_DEADCHAR || msg == WM_SYSDEADCHAR;
}

/* calls an ansi procedure with a message expressed in unicode. */
static LRESULT call_ansi_proc(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_NCCREATE:
    case WM_CREATE: {
        CREATESTRUCTW *csw = (CREATESTRUCTW *)lparam;
        CREATESTRUCTA csa;
        memcpy(&csa, csw, sizeof(csa));
        csa.lpszName = UserWtoA(csw->lpszName);
        csa.lpszClass = UserWtoA(csw->lpszClass);
        LRESULT r = proc(hwnd, msg, wparam, (LPARAM)&csa);
        UserFreeConverted(csa.lpszName, csw->lpszName);
        UserFreeConverted(csa.lpszClass, csw->lpszClass);
        return r;
    }
    case WM_SETTEXT:
    case WM_SETTINGCHANGE: {
        LPSTR a = UserWtoA((LPCWSTR)lparam);
        LRESULT r = proc(hwnd, msg, wparam, (LPARAM)a);
        UserFreeConverted(a, (void *)lparam);
        return r;
    }
    case WM_GETTEXT: {
        if (!wparam || !lparam)
            return proc(hwnd, msg, wparam, lparam);
        SIZE_T cap = wparam * 2 + 1;
        LPSTR a = UserAlloc(cap);
        if (!a)
            return 0;
        LRESULT n = proc(hwnd, msg, (WPARAM)cap, (LPARAM)a);
        LPWSTR out = (LPWSTR)lparam;
        int w = MultiByteToWideChar(CP_ACP, 0, a, (int)n, out, (int)wparam - 1);
        out[w] = 0;
        UserFree(a);
        return w;
    }
    default:
        if (is_char_message(msg) && wparam > 0x7F) {
            WCHAR wc = (WCHAR)wparam;
            char buf[4];
            int n = WideCharToMultiByte(CP_ACP, 0, &wc, 1, buf, sizeof(buf), NULL, NULL);
            LRESULT r = 0;
            for (int i = 0; i < n; i++)
                r = proc(hwnd, msg, (UCHAR)buf[i], lparam);
            return r;
        }
        return proc(hwnd, msg, wparam, lparam);
    }
}

LRESULT UserCallProc(WNDPROC proc, BOOL proc_ansi, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (!proc)
        return 0;
    if (proc_ansi)
        return call_ansi_proc(proc, hwnd, msg, wparam, lparam);
    return proc(hwnd, msg, wparam, lparam);
}

/* sends a message given in ansi or unicode; win32k only carries unicode. */
LRESULT UserSendMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    if (!ansi)
        return NtUserMessageCall(hwnd, msg, wparam, lparam, 0, MIME_MSGCALL_SEND, FALSE);
    switch (msg) {
    case WM_SETTEXT: {
        LPWSTR w = UserAtoW((LPCSTR)lparam);
        LRESULT r = NtUserMessageCall(hwnd, msg, wparam, (LPARAM)w, 0, MIME_MSGCALL_SEND, FALSE);
        UserFreeConverted(w, (void *)lparam);
        return r;
    }
    case WM_GETTEXT: {
        if (!wparam || !lparam)
            return 0;
        LPWSTR w = UserAlloc(wparam * sizeof(WCHAR));
        if (!w)
            return 0;
        LRESULT n = NtUserMessageCall(hwnd, msg, wparam, (LPARAM)w, 0, MIME_MSGCALL_SEND, FALSE);
        int a = WideCharToMultiByte(CP_ACP, 0, w, (int)n, (LPSTR)lparam, (int)wparam - 1, NULL, NULL);
        ((LPSTR)lparam)[a] = 0;
        UserFree(w);
        return a;
    }
    default:
        if (is_char_message(msg) && wparam > 0x7F) {
            char c = (char)wparam;
            WCHAR wc;
            MultiByteToWideChar(CP_ACP, 0, &c, 1, &wc, 1);
            wparam = wc;
        }
        return NtUserMessageCall(hwnd, msg, wparam, lparam, 0, MIME_MSGCALL_SEND, FALSE);
    }
}

/* ------------------------------------------------------------------ kernel callbacks */

static NTSTATUS CALLBACK callback_window_proc(MIME_WINDOWPROC_ARGS *args, ULONG length)
{
    LPARAM lparam = args->Lparam;
    switch (args->LparamKind) {
    case MIME_LPARAM_IN:
    case MIME_LPARAM_INOUT:
        lparam = (LPARAM)args->Data;
        if (args->Msg == WM_NCCALCSIZE && args->Wparam && args->DataSize >= sizeof(NCCALCSIZE_PARAMS) + sizeof(WINDOWPOS)) {
            NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)args->Data;
            p->lppos = (WINDOWPOS *)(p + 1);
        }
        break;
    case MIME_LPARAM_CREATESTRUCT: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)args->Data;
        if (cs->lpszName)
            cs->lpszName = (LPCWSTR)(args->Data + (ULONG_PTR)cs->lpszName);
        if (cs->lpszClass)
            cs->lpszClass = (LPCWSTR)(args->Data + (ULONG_PTR)cs->lpszClass);
        lparam = (LPARAM)cs;
        break;
    }
    }
    LRESULT result = UserCallProc(args->Proc, args->Ansi, args->Hwnd, args->Msg, args->Wparam, lparam);

    if (args->LparamKind == MIME_LPARAM_INOUT && args->DataSize) {
        /* the result header goes right before the data, which stays in place. */
        MIME_CALLBACK_RESULT *res = (MIME_CALLBACK_RESULT *)((PUCHAR)args->Data - sizeof(MIME_CALLBACK_RESULT));
        ULONG size = args->DataSize;
        res->Result = result;
        res->DataSize = size;
        res->Pad = 0;
        return NtCallbackReturn(res, sizeof(*res) + size, STATUS_SUCCESS);
    }
    MIME_CALLBACK_RESULT res = {result, 0, 0};
    return NtCallbackReturn(&res, sizeof(res), STATUS_SUCCESS);
}

static NTSTATUS CALLBACK callback_timer_proc(MIME_TIMERPROC_ARGS *args, ULONG length)
{
    args->Proc(args->Hwnd, args->Msg, args->Id, args->Time);
    MIME_CALLBACK_RESULT res = {0, 0, 0};
    return NtCallbackReturn(&res, sizeof(res), STATUS_SUCCESS);
}

static NTSTATUS CALLBACK callback_hook_proc(MIME_HOOKPROC_ARGS *args, ULONG length)
{
    ULONG size = args->DataSize;
    LRESULT result = args->Proc(args->Code, args->Wparam, size ? (LPARAM)args->Data : args->Lparam);
    if (size) {
        /* the result header goes right before the data, like for window procedures. */
        MIME_CALLBACK_RESULT *res = (MIME_CALLBACK_RESULT *)((PUCHAR)args->Data - sizeof(MIME_CALLBACK_RESULT));
        res->Result = result;
        res->DataSize = size;
        res->Pad = 0;
        return NtCallbackReturn(res, sizeof(*res) + size, STATUS_SUCCESS);
    }
    MIME_CALLBACK_RESULT res = {result, 0, 0};
    return NtCallbackReturn(&res, sizeof(res), STATUS_SUCCESS);
}

static NTSTATUS CALLBACK callback_unused(PVOID args, ULONG length)
{
    MIME_CALLBACK_RESULT res = {0, 0, 0};
    return NtCallbackReturn(&res, sizeof(res), STATUS_NOT_IMPLEMENTED);
}

static PVOID g_callbacks[MIME_CB_COUNT] = {
    (PVOID)callback_window_proc, (PVOID)callback_timer_proc, (PVOID)callback_unused, (PVOID)callback_hook_proc,
    (PVOID)callback_unused,      (PVOID)callback_unused,     (PVOID)callback_unused, (PVOID)callback_unused,
};

/* ------------------------------------------------------------------ initialization */

static void register_system_class(PCWSTR name, WNDPROC proc, ULONG fnid, UINT style)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = style;
    wc.lpfnWndProc = proc;
    wc.hInstance = User32Instance;
    wc.lpszClassName = name;
    UNICODE_STRING s;
    RtlInitUnicodeString(&s, name);
    NtUserRegisterClassExWOW(&wc, &s, NULL, fnid, MIME_CLASS_SYSTEM);
}

static void initialize_process(void)
{
    NtCurrentPeb()->KernelCallbackTable = g_callbacks;

    User32Client.Size = sizeof(User32Client);
    User32Client.User32 = User32Instance;
    User32Client.Procs[MIME_FNID_BUTTON] = ButtonProc;
    User32Client.Procs[MIME_FNID_STATIC] = StaticProc;
    User32Client.Procs[MIME_FNID_EDIT] = EditProc;
    User32Client.Procs[MIME_FNID_LISTBOX] = ListBoxProc;
    User32Client.Procs[MIME_FNID_COMBOBOX] = ComboBoxProc;
    User32Client.Procs[MIME_FNID_SCROLLBAR] = ScrollBarProc;
    User32Client.Procs[MIME_FNID_DIALOG] = DialogProc;
    User32Client.Procs[MIME_FNID_MESSAGE] = MessageWindowProc;
    User32Client.Procs[MIME_FNID_DESKTOP] = DesktopProc;
    User32Client.Procs[MIME_FNID_MENU] = MenuProc;
    User32Client.DefWindowProcW = DefWindowProcW;
    NtUserInitializeClientPfnArrays(&User32Client);

    register_system_class(L"Button", ButtonProc, MIME_FNID_BUTTON, 0);
    register_system_class(L"Static", StaticProc, MIME_FNID_STATIC, 0);
    register_system_class(L"Edit", EditProc, MIME_FNID_EDIT, 0);
    register_system_class(L"ListBox", ListBoxProc, MIME_FNID_LISTBOX, 0);
    register_system_class(L"ComboBox", ComboBoxProc, MIME_FNID_COMBOBOX, 0);
    register_system_class(L"ScrollBar", ScrollBarProc, MIME_FNID_SCROLLBAR, 0);
    register_system_class(L"#32770", DialogProc, MIME_FNID_DIALOG, 0);
    register_system_class(L"#32769", DesktopProc, MIME_FNID_DESKTOP, 0);
    register_system_class(L"#32768", MenuProc, MIME_FNID_MENU, CS_DROPSHADOW | CS_SAVEBITS);
    register_system_class(L"Message", MessageWindowProc, MIME_FNID_MESSAGE, 0);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        User32Instance = instance;
        initialize_process();
    }
    return TRUE;
}

/* the desktop and message only windows draw nothing themselves. */
LRESULT CALLBACK DesktopProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
