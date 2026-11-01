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
    t->hresult = number ? (LONG)(0x800A0000u | (ULONG)number) : 0;
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

static const VbHeader *g_header;

void VbProjectTitle(WCHAR *out, int count)
{
    const char *name = nullptr;
    if (g_header && g_header->project_title)
        name = (const char *)g_header + g_header->project_title;
    if (!name || !name[0])
        name = "Project1";
    MultiByteToWideChar(CP_ACP, 0, name, -1, out, count);
}

static const struct {
    LONG number;
    const WCHAR *text;
} g_error_texts[] = {
    {3, L"Return without GoSub"}, {5, L"Invalid procedure call or argument"}, {6, L"Overflow"}, {7, L"Out of memory"},
    {9, L"Subscript out of range"}, {10, L"This array is fixed or temporarily locked"}, {11, L"Division by zero"},
    {13, L"Type mismatch"}, {14, L"Out of string space"}, {16, L"Expression too complex"},
    {17, L"Can't perform requested operation"}, {18, L"User interrupt occurred"}, {20, L"Resume without error"},
    {28, L"Out of stack space"}, {35, L"Sub or Function not defined"}, {47, L"Too many DLL application clients"},
    {48, L"Error in loading DLL"}, {49, L"Bad DLL calling convention"}, {51, L"Internal error"},
    {52, L"Bad file name or number"}, {53, L"File not found"}, {54, L"Bad file mode"}, {55, L"File already open"},
    {57, L"Device I/O error"}, {58, L"File already exists"}, {59, L"Bad record length"}, {61, L"Disk full"},
    {62, L"Input past end of file"}, {63, L"Bad record number"}, {67, L"Too many files"}, {68, L"Device unavailable"},
    {70, L"Permission denied"}, {71, L"Disk not ready"}, {74, L"Can't rename with different drive"},
    {75, L"Path/File access error"}, {76, L"Path not found"},
    {91, L"Object variable or With block variable not set"}, {92, L"For loop not initialized"},
    {93, L"Invalid pattern string"}, {94, L"Invalid use of Null"},
    {321, L"Invalid file format"}, {322, L"Can't create necessary temporary file"}, {380, L"Invalid property value"},
    {381, L"Invalid property array index"}, {424, L"Object required"},
    {429, L"ActiveX component can't create object"}, {430, L"Class doesn't support Automation"},
    {432, L"File name or class name not found during Automation operation"},
    {438, L"Object doesn't support this property or method"}, {440, L"Automation error"},
    {445, L"Object doesn't support this action"}, {446, L"Object doesn't support named arguments"},
    {447, L"Object doesn't support current locale setting"}, {448, L"Named argument not found"},
    {449, L"Argument not optional"}, {450, L"Wrong number of arguments or invalid property assignment"},
    {451, L"Property let procedure not defined and property get procedure did not return an object"},
    {452, L"Invalid ordinal"}, {453, L"Specified DLL function not found"}, {454, L"Code resource not found"},
    {455, L"Code resource lock error"}, {457, L"This key is already associated with an element of this collection"},
    {458, L"Variable uses an Automation type not supported in Visual Basic"},
    {459, L"Object or class does not support the set of events"}, {460, L"Invalid clipboard format"},
    {461, L"Method or data member not found"}, {462, L"The remote server machine does not exist or is unavailable"},
    {463, L"Class not registered on local machine"}, {481, L"Invalid picture"}, {482, L"Printer error"},
};

const WCHAR *VbErrorText(LONG number)
{
    for (size_t i = 0; i < sizeof(g_error_texts) / sizeof(g_error_texts[0]); i++)
        if (g_error_texts[i].number == number)
            return g_error_texts[i].text;
    return number >= 1 && number <= 65535 ? L"Application-defined or object-defined error" : L"";
}

/* an error nothing handles ends the program with its dialog, and End ends it without. */
void VbTerminateWithError(LONG number)
{
    if (number == VBERR_END) {
        VbOutputDebug("msvbvm60: End\n");
        ExitProcess(0);
    }
    WCHAR title[64], text[256];
    VbProjectTitle(title, 64);
    wsprintfW(text, L"Run-time error '%d':\n\n%s", (int)number, VbErrorText(number));
    char debug[96];
    wsprintfA(debug, "msvbvm60: Run-time error '%d'\n", (int)number);
    VbOutputDebug(debug);
    MessageBoxW(nullptr, text, title, MB_OK | MB_ICONSTOP);
    ExitProcess(1);
}

static LONG WINAPI unhandled(EXCEPTION_POINTERS *info)
{
    if ((ULONG)info->ExceptionRecord->ExceptionCode == VB_EXCEPTION)
        VbTerminateWithError((LONG)info->ExceptionRecord->ExceptionInformation[0]);
    return EXCEPTION_CONTINUE_SEARCH;
}

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
    g_header = header;
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
