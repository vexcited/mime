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
    int n = count ? MultiByteToWideChar(CP_ACP, 0, s, count, nullptr, 0) : 0;
    LPWSTR w = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!w)
        return nullptr;
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
    return UserAtoWn(s, -1, nullptr);
}

LPSTR UserWtoA(LPCWSTR s)
{
    if (!s || IS_INTRESOURCE(s))
        return (LPSTR)s;
    int n = WideCharToMultiByte(CP_ACP, 0, s, -1, nullptr, 0, nullptr, nullptr);
    LPSTR a = UserAlloc((SIZE_T)(n ? n : 1));
    if (a)
        WideCharToMultiByte(CP_ACP, 0, s, -1, a, n, nullptr, nullptr);
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
            int n = WideCharToMultiByte(CP_ACP, 0, &wc, 1, buf, sizeof(buf), nullptr, nullptr);
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

static LRESULT send_w(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return NtUserMessageCall(hwnd, msg, wparam, lparam, 0, MIME_MSGCALL_SEND, FALSE);
}

enum { CONTROL_NONE, CONTROL_LIST, CONTROL_COMBO, CONTROL_EDIT };

/* the system control a window is, whose unicode procedure an ansi sender's strings must reach converted. */
static int control_kind(HWND hwnd, BOOL *strings)
{
    WCHAR name[16];
    if (!GetClassNameW(hwnd, name, 16))
        return CONTROL_NONE;
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    if (!lstrcmpiW(name, L"ListBox") || !lstrcmpiW(name, L"ComboLBox")) {
        *strings = !(style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) || (style & LBS_HASSTRINGS);
        return CONTROL_LIST;
    }
    if (!lstrcmpiW(name, L"ComboBox")) {
        *strings = (style & CBS_HASSTRINGS) != 0;
        return CONTROL_COMBO;
    }
    if (!lstrcmpiW(name, L"Edit")) {
        *strings = TRUE;
        return CONTROL_EDIT;
    }
    return CONTROL_NONE;
}

/* an item's text for an ansi caller, in the ansi code page, its length in bytes. */
static LRESULT ansi_item_text(HWND hwnd, UINT len_msg, UINT text_msg, WPARAM index, LPSTR out)
{
    LRESULT n = send_w(hwnd, len_msg, index, 0);
    if (n < 0)
        return n;
    LPWSTR w = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!w)
        return -1;
    n = send_w(hwnd, text_msg, index, (LPARAM)w);
    int bytes = n >= 0 ? WideCharToMultiByte(CP_ACP, 0, w, (int)n, out, out ? (int)n * 2 + 1 : 0, nullptr, nullptr) : 0;
    if (out && n >= 0)
        out[bytes] = 0;
    UserFree(w);
    return n < 0 ? n : bytes;
}

/* the string messages of the list, combo box and edit controls for ansi senders. */
static BOOL control_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    BOOL in_string = FALSE, always = FALSE;
    switch (msg) {
    case LB_ADDSTRING:
    case LB_INSERTSTRING:
    case LB_FINDSTRING:
    case LB_FINDSTRINGEXACT:
    case LB_SELECTSTRING:
    case CB_ADDSTRING:
    case CB_INSERTSTRING:
    case CB_FINDSTRING:
    case CB_FINDSTRINGEXACT:
    case CB_SELECTSTRING:
    case EM_REPLACESEL:
        in_string = TRUE;
        break;
    case LB_DIR:
    case LB_ADDFILE:
    case CB_DIR:
        in_string = always = TRUE;
        break;
    case LB_GETTEXT:
    case LB_GETTEXTLEN:
    case CB_GETLBTEXT:
    case CB_GETLBTEXTLEN:
    case LB_INITSTORAGE:
    case CB_INITSTORAGE:
    case EM_GETLINE:
    case EM_SETPASSWORDCHAR:
    case EM_GETPASSWORDCHAR:
        break;
    default:
        return FALSE;
    }
    BOOL strings = FALSE;
    int kind = control_kind(hwnd, &strings);
    if (kind == CONTROL_NONE)
        return FALSE;
    if (in_string) {
        if (!strings && !always)
            return FALSE;
        LPWSTR w = UserAtoW((LPCSTR)lparam);
        if (lparam && !w) {
            *result = LB_ERR;
            return TRUE;
        }
        *result = send_w(hwnd, msg, wparam, (LPARAM)w);
        UserFreeConverted(w, (void *)lparam);
        return TRUE;
    }
    switch (msg) {
    case LB_GETTEXT:
    case CB_GETLBTEXT:
        if (!strings)
            return FALSE;
        *result = ansi_item_text(hwnd, msg == LB_GETTEXT ? LB_GETTEXTLEN : CB_GETLBTEXTLEN, msg, wparam, (LPSTR)lparam);
        return TRUE;
    case LB_GETTEXTLEN:
    case CB_GETLBTEXTLEN:
        if (!strings)
            return FALSE;
        *result = ansi_item_text(hwnd, msg, msg == LB_GETTEXTLEN ? LB_GETTEXT : CB_GETLBTEXT, wparam, nullptr);
        return TRUE;
    case LB_INITSTORAGE:
    case CB_INITSTORAGE:
        *result = send_w(hwnd, msg, wparam, lparam * 2);
        return TRUE;
    case EM_GETLINE: {
        if (!lparam)
            return FALSE;
        WORD max = *(WORD *)lparam;
        LPWSTR w = UserAlloc(((SIZE_T)max + 1) * sizeof(WCHAR));
        if (!w) {
            *result = 0;
            return TRUE;
        }
        *(WORD *)w = max;
        LRESULT n = send_w(hwnd, msg, wparam, (LPARAM)w);
        *result = n > 0 ? WideCharToMultiByte(CP_ACP, 0, w, (int)n, (LPSTR)lparam, max, nullptr, nullptr) : 0;
        UserFree(w);
        return TRUE;
    }
    case EM_SETPASSWORDCHAR: {
        char c = (char)wparam;
        WCHAR wc = 0;
        if (c)
            MultiByteToWideChar(CP_ACP, 0, &c, 1, &wc, 1);
        *result = send_w(hwnd, msg, wc, lparam);
        return TRUE;
    }
    case EM_GETPASSWORDCHAR: {
        WCHAR wc = (WCHAR)send_w(hwnd, msg, wparam, lparam);
        char c = 0;
        if (wc)
            WideCharToMultiByte(CP_ACP, 0, &wc, 1, &c, 1, nullptr, nullptr);
        *result = (UCHAR)c;
        return TRUE;
    }
    }
    return FALSE;
}

/* sends a message given in ansi or unicode; win32k only carries unicode. */
LRESULT UserSendMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    if (!ansi)
        return NtUserMessageCall(hwnd, msg, wparam, lparam, 0, MIME_MSGCALL_SEND, FALSE);
    LRESULT result;
    if (control_message(hwnd, msg, wparam, lparam, &result))
        return result;
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
        int a = WideCharToMultiByte(CP_ACP, 0, w, (int)n, (LPSTR)lparam, (int)wparam - 1, nullptr, nullptr);
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
    /* scroll bar state lives in properties, released with the window. */
    if (args->Msg == WM_NCDESTROY)
        UserFreeScrollData(args->Hwnd);

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
    NtUserRegisterClassExWOW(&wc, &s, nullptr, fnid, MIME_CLASS_SYSTEM);
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
    User32Client.Procs[MIME_FNID_COMBOLBOX] = ListBoxProc;
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
    register_system_class(L"ComboLBox", ListBoxProc, MIME_FNID_COMBOLBOX, CS_SAVEBITS);
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
