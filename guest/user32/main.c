/* user32 initialization, the kernel callback table and string conversion. */
#include "user32_private.h"
#include <mime/mitigation.h>

HINSTANCE User32Instance;
MIME_CLIENT_INFO User32Client;

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

/* strings in a block store an offset in place of the pointer, at 0x10000 or above so atoms stay atoms. */
static LPCWSTR block_string(PUCHAR data, LPCWSTR field)
{
    return (ULONG_PTR)field >= 0x10000 ? (LPCWSTR)(data + ((ULONG_PTR)field - 0x10000)) : field;
}

static NTSTATUS CALLBACK callback_window_proc(MIME_WINDOWPROC_ARGS *args, ULONG length)
{
    WPARAM wparam = args->Wparam;
    LPARAM lparam = args->Lparam;
    ULONG kind = MIME_LPARAM_KIND(args->LparamKind);
    BOOL external = (args->LparamKind & MIME_LPARAM_EXTERNAL) != 0;
    PUCHAR data = external ? (PUCHAR)args->Lparam : args->Data;
    switch (kind) {
    case MIME_LPARAM_IN:
    case MIME_LPARAM_INOUT:
        lparam = (LPARAM)data;
        if (args->Msg == WM_NCCALCSIZE && args->Wparam && args->DataSize >= sizeof(NCCALCSIZE_PARAMS) + sizeof(WINDOWPOS)) {
            NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)data;
            p->lppos = (WINDOWPOS *)(p + 1);
        } else if (args->Msg == WM_COPYDATA && args->DataSize >= sizeof(COPYDATASTRUCT)) {
            COPYDATASTRUCT *cd = (COPYDATASTRUCT *)data;
            if (cd->lpData)
                cd->lpData = data + (ULONG_PTR)cd->lpData;
        } else if (args->Msg == WM_MDICREATE && args->DataSize >= sizeof(MDICREATESTRUCTW)) {
            MDICREATESTRUCTW *mdi = (MDICREATESTRUCTW *)data;
            mdi->szClass = block_string(data, mdi->szClass);
            mdi->szTitle = block_string(data, mdi->szTitle);
        }
        break;
    case MIME_LPARAM_CREATESTRUCT: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)data;
        if (cs->lpszName)
            cs->lpszName = (LPCWSTR)(data + (ULONG_PTR)cs->lpszName);
        if (cs->lpszClass)
            cs->lpszClass = (LPCWSTR)(data + (ULONG_PTR)cs->lpszClass);
        lparam = (LPARAM)cs;
        break;
    }
    case MIME_LPARAM_PTR2:
        wparam = (WPARAM)data;
        lparam = (LPARAM)(data + sizeof(DWORD));
        break;
    case MIME_LPARAM_WPTR:
        wparam = (WPARAM)data;
        break;
    }
    LRESULT result = UserCallProc(args->Proc, args->Ansi, args->Hwnd, args->Msg, wparam, lparam);
    /* scroll bar state lives in properties, released with the window. */
    if (args->Msg == WM_NCDESTROY)
        UserFreeScrollData(args->Hwnd);

    BOOL writes = kind == MIME_LPARAM_INOUT || kind == MIME_LPARAM_PTR2 || kind == MIME_LPARAM_WPTR;
    if (writes && args->DataSize && !external) {
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

static NTSTATUS CALLBACK callback_send_async(MIME_SENDASYNC_ARGS *args, ULONG length)
{
    args->Proc(args->Hwnd, args->Msg, args->Data, args->Result);
    MIME_CALLBACK_RESULT res = {0, 0, 0};
    return NtCallbackReturn(&res, sizeof(res), STATUS_SUCCESS);
}

/* HCBT_CREATEWND data holds offsets in place of its pointers, see MIME_HOOKPROC_ARGS. */
static LRESULT call_cbt_create(MIME_HOOKPROC_ARGS *args)
{
    CBT_CREATEWNDW *cbt = (CBT_CREATEWNDW *)args->Data;
    CREATESTRUCTW *cs = (CREATESTRUCTW *)(cbt + 1);
    LPCWSTR name = cs->lpszName, klass = cs->lpszClass;
    if (name)
        cs->lpszName = (LPCWSTR)((PUCHAR)cs + (ULONG_PTR)name);
    if (klass)
        cs->lpszClass = (LPCWSTR)((PUCHAR)cs + (ULONG_PTR)klass);
    LRESULT result;
    if (args->Ansi) {
        CREATESTRUCTA csa;
        memcpy(&csa, cs, sizeof(csa));
        csa.lpszName = UserWtoA(cs->lpszName);
        csa.lpszClass = UserWtoA(cs->lpszClass);
        CBT_CREATEWNDA cbta = {&csa, cbt->hwndInsertAfter};
        result = args->Proc(args->Code, args->Wparam, (LPARAM)&cbta);
        UserFreeConverted(csa.lpszName, cs->lpszName);
        UserFreeConverted(csa.lpszClass, cs->lpszClass);
        cs->x = csa.x, cs->y = csa.y, cs->cx = csa.cx, cs->cy = csa.cy;
        cbt->hwndInsertAfter = cbta.hwndInsertAfter;
    } else {
        cbt->lpcs = cs;
        result = args->Proc(args->Code, args->Wparam, (LPARAM)cbt);
        cbt->lpcs = nullptr;
    }
    cs->lpszName = name;
    cs->lpszClass = klass;
    return result;
}

/* the message of a window procedure hook, and after it the data its lparam points at, see MIME_HOOKPROC_ARGS. */
static LRESULT call_wnd_hook(MIME_HOOKPROC_ARGS *args)
{
    BOOLEAN after = args->Id == WH_CALLWNDPROCRET;
    ULONG head = after ? sizeof(CWPRETSTRUCT) : sizeof(CWPSTRUCT);
    if (args->DataSize > head) {
        LPARAM *lparam = after ? &((CWPRETSTRUCT *)args->Data)->lParam : &((CWPSTRUCT *)args->Data)->lParam;
        *lparam = (LPARAM)(args->Data + head);
    }
    return args->Proc(args->Code, args->Wparam, (LPARAM)args->Data);
}

static NTSTATUS CALLBACK callback_hook_proc(MIME_HOOKPROC_ARGS *args, ULONG length)
{
    ULONG size = args->DataSize;
    LRESULT result;
    if ((args->Id == WH_CALLWNDPROC && size >= sizeof(CWPSTRUCT)) ||
        (args->Id == WH_CALLWNDPROCRET && size >= sizeof(CWPRETSTRUCT)))
        result = call_wnd_hook(args);
    else if (args->Id == WH_CBT && args->Code == HCBT_CREATEWND && size >= sizeof(CBT_CREATEWNDW) + sizeof(CREATESTRUCTW))
        result = call_cbt_create(args);
    else
        result = args->Proc(args->Code, args->Wparam, size ? (LPARAM)args->Data : args->Lparam);
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
    (PVOID)callback_send_async,  (PVOID)callback_unused,     (PVOID)callback_unused, (PVOID)callback_unused,
};

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
    UserRegisterMdiClient();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        User32Instance = instance;
        /* a process without the win32k system calls loads user32 and connects to nothing. */
        ULONG policy[2] = {MIME_POLICY_SYSTEM_CALL_DISABLE, 0};
        NtQueryInformationProcess(NtCurrentProcess(), ProcessMitigationPolicy, policy, sizeof(policy), nullptr);
        if (!(policy[1] & MIME_SYSTEM_CALL_DISABLE_WIN32K)) {
            initialize_process();
            UserInitializeDpiAwareness();
        }
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
