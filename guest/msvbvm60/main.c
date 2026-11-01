/*
 * the thread state, the runtime errors and ThunRTMain, which starts a compiled program.
 */
#include "vb.h"

int _fltused = 0x9875;

static DWORD g_tls = TLS_OUT_OF_INDEXES;

void VbOutputDebug(const char *text) { OutputDebugStringA(text); }

LCID VbLocale(void) { return GetUserDefaultLCID(); }

VbThread *VbThreadState(void)
{
    VbThread *t = TlsGetValue(g_tls);
    if (!t) {
        t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
        TlsSetValue(g_tls, t);
    }
    return t;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_tls = TlsAlloc();
    } else if (reason == DLL_THREAD_DETACH && g_tls != TLS_OUT_OF_INDEXES) {
        VbThread *t = TlsGetValue(g_tls);
        if (t)
            HeapFree(GetProcessHeap(), 0, t);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ errors */

LONG VbErrorOfHresult(HRESULT hr)
{
    switch ((ULONG)hr) {
    case 0x8002000A:
        return VBERR_OVERFLOW;
    case 0x80020005:
    case 0x80020009 + 0x80000000:
        return VBERR_TYPE_MISMATCH;
    case 0x80020012:
        return VBERR_DIVISION_BY_ZERO;
    case 0x8002000B:
        return VBERR_SUBSCRIPT;
    case 0x8007000E:
    case 0x8007000F + 0x80000000:
        return VBERR_OUT_OF_MEMORY;
    case 0x80004005:
        return VBERR_INVALID_CALL;
    case 0x80070057:
        return VBERR_INVALID_CALL;
    case 0x80020006:
        return VBERR_NO_SUCH_MEMBER;
    }
    if ((hr & 0xFFFF0000) == 0x800A0000)
        return hr & 0xFFFF;
    if ((hr & 0xFFFF0000) == 0x80070000)
        return VBERR_INVALID_CALL;
    return VBERR_APPLICATION_DEFINED;
}

__attribute__((noreturn)) void VbRaise(LONG number)
{
    VbThread *t = VbThreadState();
    t->err_number = number;
    ULONG_PTR args[1] = {(ULONG_PTR)number};
    RaiseException(VB_EXCEPTION, 0, 1, args);
    ExitProcess(1);
}

void VbCheck(HRESULT hr)
{
    if (hr < 0)
        VbRaise(VbErrorOfHresult(hr));
}

VB_STD __attribute__((noreturn)) void vb___vbaErrorOverflow(void) { VbRaise(VBERR_OVERFLOW); }

VB_C __attribute__((noreturn)) void vb___vbaError(unsigned int number)
{
    if (number && number <= 0xFFFF)
        VbRaise((LONG)number);
    VbRaise(VBERR_INVALID_CALL);
}

VB_STD __attribute__((noreturn)) void vb___vbaEnd(void) { VbRaise(VBERR_END); }

VB_STD __attribute__((noreturn)) void vb___vbaGenerateBoundsError(void) { VbRaise(VBERR_SUBSCRIPT); }

/* on x87 error the status word says which error it was: a divide by zero, an invalid operation or an overflow. */
VB_STD __attribute__((noreturn)) void vb___vbaFPException(void)
{
    unsigned short status;
    __asm__ volatile("fnstsw %0" : "=a"(status));
    if (status & 0x40)
        VbRaise(16);
    VbRaise(status & 4 ? 11 : 6);
}

VB_C int vb___vbaSetSystemError(void)
{
    VbThread *t = VbThreadState();
    t->last_dll_error = (LONG)GetLastError();
    return 0;
}

/* an unhandled runtime error ends the program the way the runtime does, with its dialog. */
static LONG WINAPI unhandled(EXCEPTION_POINTERS *info)
{
    if ((ULONG)info->ExceptionRecord->ExceptionCode == VB_EXCEPTION) {
        LONG number = (LONG)info->ExceptionRecord->ExceptionInformation[0];
        if (number == VBERR_END) {
            VbOutputDebug("msvbvm60: End\n");
            ExitProcess(0);
        }
        char text[96];
        wsprintfA(text, "Run-time error '%d':", (int)number);
        VbOutputDebug(text);
        MessageBoxA(nullptr, text, "Microsoft Visual Basic", MB_OK | MB_ICONSTOP);
        ExitProcess(1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* the exe's own frames register this handler. an error that nothing in the program handles goes on to the filter. */
VB_C int vb___vbaExceptHandler(void *record, void *frame) { return 1; }

/* ------------------------------------------------------------------ startup */

/* the address of the program's data. the exe sits at its own base, so addresses in it are used as they are. */
static BOOL valid_header(const VbHeader *h)
{
    return !IsBadReadPtr(h, sizeof(*h)) && h->signature[0] == 'V' && h->signature[1] == 'B' && h->signature[2] == '5' && h->signature[3] == '!';
}

static LRESULT CALLBACK form_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void register_classes(HINSTANCE module)
{
    static const WCHAR *names[] = {L"ThunderRT6Main", L"ThunderRT6FormDC", L"ThunderRT6Form", L"ThunderRT6MDIForm"};
    for (int i = 0; i < 4; i++) {
        WNDCLASSW wc = {};
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = form_proc;
        wc.hInstance = module;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = names[i];
        RegisterClassW(&wc);
    }
}

typedef void (*SubMainFn)(void);

VB_C __attribute__((noreturn)) void vb_ThunRTMain(const VbHeader *header)
{
    if (!valid_header(header)) {
        MessageBoxA(nullptr, "The program is not a Visual Basic 6 executable.", "Microsoft Visual Basic", MB_OK | MB_ICONSTOP);
        ExitProcess(1);
    }
    SetUnhandledExceptionFilter(unhandled);
    VbThreadState();
    HINSTANCE exe = GetModuleHandleW(nullptr);
    CoInitialize(nullptr);
    register_classes(exe);
    {
        char text[160];
        wsprintfA(text, "msvbvm60: starting, sub main %08X, %u forms, startup flags %08X\n", (unsigned)header->sub_main, (unsigned)header->form_count, (unsigned)header->thread_flags);
        VbOutputDebug(text);
    }
    if (header->sub_main) {
        /* a project that starts with Sub Main runs it, then the loop stays while forms are open. */
        SubMainFn main_proc = (SubMainFn)(ULONG_PTR)header->sub_main;
        main_proc();
        VbOutputDebug("msvbvm60: sub main returned\n");
    }
    MSG msg;
    while (IsWindow(GetActiveWindow()) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    ExitProcess(0);
}
