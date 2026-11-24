#include <mime/harderror.h>
#include <mime/ntapi.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <rtl.h>
#include <stdarg.h>

#define STATUS_SERVICE_NOTIFICATION ((NTSTATUS)0x40000018L)
#define STATUS_VDM_HARD_ERROR ((NTSTATUS)0xC000021DL)
#define STATUS_UNHANDLED_EXCEPTION ((NTSTATUS)0xC0000144L)
#define STATUS_SYSTEM_IMAGE_BAD_SIGNATURE ((NTSTATUS)0xC00002D1L)
#define CODE_ACCESS_VIOLATION 0xC0000005u
#define CODE_IN_PAGE_ERROR 0xC0000006u
#define OPTION_OK_CANCEL 2
#define OPTION_CANCEL_TRY_CONTINUE 8
#define RESPONSE_NOT_HANDLED 1
#define MB_SERVICE_NOTIFICATION_FLAG 0x00200000u
#define MB_DEFAULT_DESKTOP_ONLY_FLAG 0x00020000u
#define MB_TIMEDOUT 32000
#define MESSAGE_TABLE 11
#define KEY_VALUE_PARTIAL 2
#define TEXT_CHARS 0x401

typedef struct MESSAGE_ENTRY {
    USHORT Length;
    USHORT Flags;
    WCHAR Text[1];
} MESSAGE_ENTRY;

typedef struct VALUE_PARTIAL {
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataLength;
    UCHAR Data[4];
} VALUE_PARTIAL;

NTSYSAPI NTSTATUS NTAPI RtlFindMessage(PVOID base, ULONG type, ULONG language, ULONG id, MESSAGE_ENTRY **entry);
WINUSERAPI BOOL WINAPI EnumThreadWindows(DWORD thread, WNDENUMPROC proc, LPARAM param);
WINUSERAPI int WINAPI MessageBoxTimeoutW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT style, WORD language,
                                         DWORD timeout);

/*
 * winsrvext's wOptions, wIcons, dwResponses and dwResponseDefault: the buttons by option, the icon by severity, the
 * response by button, and the response of a box nobody sees.
 */
static const UINT g_buttons[] = {MB_ABORTRETRYIGNORE, MB_OK, MB_OKCANCEL, MB_RETRYCANCEL, MB_YESNO,
                                 MB_YESNOCANCEL,      MB_OK, MB_OK,       MB_CANCELTRYCONTINUE};
static const UINT g_icons[] = {0, MB_ICONINFORMATION, MB_ICONWARNING, MB_ICONERROR};
static const ULONG g_responses[] = {1, 6, 3, 2, 7, 4, 8, 5, 1, 1, 9, 10};
static const ULONG g_unseen[] = {2, 6, 6, 3, 8, 8, 6, 6, 3};

/* the english strings of winsrvext's message table. */
static const WCHAR *const g_titles[] = {L"Success", L"System Information", L"System Warning", L"System Error"};
static const WCHAR g_unknown_error[] = L"Unknown Hard Error";
static const WCHAR g_unknown_exception[] = L"unknown software exception";
static const WCHAR g_terminate[] = L"Click on OK to terminate the program";
static const WCHAR g_debug[] = L"Click on CANCEL to debug the program";
static const WCHAR g_system_image[] = L"Windows";
static const WCHAR g_empty[] = L"";

static const MIME_HARD_ERROR *g_request;
/* the parameters as csrss formats them, and their kinds: 0 a number, 1 an ansi string, 2 a wide one. */
static ULONG_PTR g_args[MIME_HARD_ERROR_PARAMETERS];
static UCHAR g_kinds[MIME_HARD_ERROR_PARAMETERS];

static WCHAR g_text[TEXT_CHARS * 2 + 128], g_caption[TEXT_CHARS];
static const WCHAR *g_box_text, *g_box_caption;
static UINT g_style;
static DWORD g_timeout = INFINITE;
static BOOL g_no_image;

static void print(PWSTR out, SIZE_T chars, const WCHAR *format, ...)
{
    va_list list;
    va_start(list, format);
    _vsnwprintf(out, chars - 1, format, list);
    va_end(list);
    out[chars - 1] = 0;
}

static PVOID allocate(SIZE_T bytes)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
}

/* SubstituteDeviceName: the device of a volume is named by its drive. */
static BOOL drive_of_device(PWSTR text)
{
    WCHAR drive[3] = L"A:", target[512];
    for (WCHAR letter = L'A'; letter <= L'Z'; letter++) {
        drive[0] = letter;
        if (QueryDosDeviceW(drive, target, RTL_NUMBER_OF(target)) && !_wcsicmp(target, text)) {
            text[0] = letter;
            text[1] = L':';
            text[2] = 0;
            return TRUE;
        }
    }
    return FALSE;
}

/* the strings of the message, read as csrss reads them out of the program: wide for the statuses that take them so. */
static void read_parameters(void)
{
    const MIME_HARD_ERROR *r = g_request;
    NTSTATUS status = r->Status;
    BOOL wide = status == STATUS_SERVICE_NOTIFICATION || status == STATUS_VDM_HARD_ERROR ||
                status == STATUS_SYSTEM_IMAGE_BAD_SIGNATURE;
    for (ULONG i = 0; i < MIME_HARD_ERROR_PARAMETERS; i++) {
        BOOL string = i >= r->Count || (r->StringMask >> i & 1);
        g_args[i] = string ? (ULONG_PTR)g_empty : (ULONG_PTR)r->Parameters[i];
        g_kinds[i] = string ? 2 : 0;
    }
    for (ULONG i = 0; i < r->Count; i++) {
        if (!(r->StringMask >> i & 1))
            continue;
        ULONG chars = r->StringLength[i] / sizeof(WCHAR);
        PWSTR text = allocate((chars + 3) * sizeof(WCHAR));
        if (!text)
            continue;
        memcpy(text, (const UCHAR *)r + r->Parameters[i], chars * sizeof(WCHAR));
        if (!wcsncmp(text, L"\\Device", 7))
            drive_of_device(text);
        else if (chars > 4 && !_wcsnicmp(text, L"\\??\\", 4))
            memmove(text, text + 4, (chars - 3) * sizeof(WCHAR));
        if (wide) {
            g_args[i] = (ULONG_PTR)text;
            g_kinds[i] = 2;
            continue;
        }
        UNICODE_STRING from;
        ANSI_STRING to;
        RtlInitUnicodeString(&from, text);
        if (NT_SUCCESS(RtlUnicodeStringToAnsiString(&to, &from, TRUE))) {
            g_args[i] = (ULONG_PTR)to.Buffer;
            g_kinds[i] = 1;
        }
    }
}

/*
 * the arguments a format takes and their kinds, as winsrvext!ValidateFormatStringW counts them: %s is wide unless h
 * makes it ansi, %S ansi unless l or w makes it wide, and conversions of floating point are not counted. -1 for more
 * than most.
 */
static int format_arguments(const WCHAR *format, int most, UCHAR *kinds)
{
    int n = 0;
    for (const WCHAR *p = format; *p; p++) {
        if (*p != L'%')
            continue;
        if (*++p == L'%')
            continue;
        while (*p == L'-' || *p == L'+' || *p == L' ' || *p == L'#' || *p == L'0')
            p++;
        while (*p == L'*' || (*p >= L'0' && *p <= L'9'))
            p++;
        if (*p == L'.')
            for (p++; *p == L'*' || (*p >= L'0' && *p <= L'9'); p++) {}
        BOOL ansi = FALSE, wide = FALSE;
        for (;; p++) {
            if (*p == L'h')
                ansi = TRUE;
            else if (*p == L'l' && p[1] == L'l')
                p++;
            else if (*p == L'l' || *p == L'w')
                wide = TRUE;
            else if (*p == L'I' && ((p[1] == L'6' && p[2] == L'4') || (p[1] == L'3' && p[2] == L'2')))
                p += 2;
            else if (*p != L'I')
                break;
        }
        UCHAR kind;
        switch (*p) {
        case L'c': case L'C': case L'd': case L'i': case L'o': case L'u': case L'x': case L'X': case L'p':
            kind = 0;
            break;
        case L's':
            kind = ansi ? 1 : 2;
            break;
        case L'S':
            kind = wide ? 2 : 1;
            break;
        case L'Z':
            kind = wide ? 4 : 3;
            break;
        case 0:
            return n;
        default:
            continue;
        }
        if (n >= most)
            return -1;
        kinds[n++] = kind;
    }
    return n;
}

/* FindWindowFromThread: the first window of the thread that no other owns, or else its first. */
static BOOL CALLBACK find_window(HWND window, LPARAM found)
{
    static BOOL(WINAPI * in_desktop_band)(HWND);
    if (!in_desktop_band)
        in_desktop_band = (BOOL(WINAPI *)(HWND))GetProcAddress(GetModuleHandleW(L"user32.dll"), "IsInDesktopWindowBand");
    WCHAR name[16];
    int n = GetClassNameW(window, name, 15);
    if (n && (!wcsncmp(name, L"IME", 3) || (GetClassLongW(window, GCL_STYLE) & CS_IME)))
        return TRUE;
    if (!in_desktop_band || !in_desktop_band(window))
        return TRUE;
    HWND owner = GetWindow(window, GW_OWNER);
    if (!*(HWND *)found || !owner)
        *(HWND *)found = window;
    return owner != nullptr;
}

static const WCHAR *after_spaces(const WCHAR *text)
{
    while (*text && *text <= L' ')
        text++;
    return text;
}

/* GetHardErrorText for the statuses ntdll's message table words. */
static void message_text(void)
{
    const MIME_HARD_ERROR *r = g_request;
    ULONG status = (ULONG)r->Status, severity = status >> 30;
    PVOID ntdll = GetModuleHandleW(L"ntdll.dll");
    g_style = g_buttons[r->Option] | g_icons[severity];

    WCHAR image[MAX_PATH];
    ULONG image_chars = r->ImageNameLength / sizeof(WCHAR);
    if (image_chars >= RTL_NUMBER_OF(image))
        image_chars = RTL_NUMBER_OF(image) - 1;
    memcpy(image, (const UCHAR *)r + r->ImageName, image_chars * sizeof(WCHAR));
    image[image_chars] = 0;
    if (!image_chars) {
        wcscpy(image, g_system_image);
        g_no_image = TRUE;
    }

    /* "{title}" before the message names the box. */
    const WCHAR *format = g_unknown_error;
    WCHAR title[128] = {0};
    MESSAGE_ENTRY *entry;
    if (NT_SUCCESS(RtlFindMessage(ntdll, MESSAGE_TABLE, 0, status, &entry))) {
        const WCHAR *text = entry->Text;
        if (*text == L'{') {
            ULONG n = 0;
            for (text++; *text;) {
                WCHAR c = *text++;
                if (c == L'}')
                    break;
                n++;
            }
            text = after_spaces(text);
            if (n && n < RTL_NUMBER_OF(title))
                memcpy(title, entry->Text + 1, n * sizeof(WCHAR));
        }
        if (*text)
            format = text;
    }

    WCHAR window_title[TEXT_CHARS] = {0};
    HWND window = nullptr;
    EnumThreadWindows(r->ThreadId, find_window, (LPARAM)&window);
    int length = window ? GetWindowTextLengthW(window) : 0;
    if (length > 0 && length < TEXT_CHARS - 3) {
        GetWindowTextW(window, window_title, length + 1);
        wcscat(window_title, L": ");
    }
    print(g_caption, RTL_NUMBER_OF(g_caption), L"%s%s - %s", window_title, image, title[0] ? title : g_titles[severity]);

    static WCHAR line[TEXT_CHARS];
    if (status == (ULONG)STATUS_UNHANDLED_EXCEPTION) {
        ULONG code = (ULONG)g_args[0];
        MESSAGE_ENTRY *exception;
        if (!NT_SUCCESS(RtlFindMessage(ntdll, MESSAGE_TABLE, 0, code, &exception))) {
            print(g_text, TEXT_CHARS, format, g_unknown_exception, g_args[0], g_args[1]);
            return;
        }
        if (code == CODE_ACCESS_VIOLATION) {
            print(line, RTL_NUMBER_OF(line), exception->Text, g_args[1], g_args[3], g_args[2] ? L"written" : L"read");
        } else if (code == CODE_IN_PAGE_ERROR) {
            print(line, RTL_NUMBER_OF(line), exception->Text, g_args[1], g_args[3], g_args[2]);
        } else {
            const WCHAR *what = wcsncmp(exception->Text, L"{EXCEPTION}", 11) ? g_unknown_exception
                                                                            : after_spaces(exception->Text + 11);
            UCHAR kinds[3];
            int n = format_arguments(format, 3, kinds);
            if (n >= 0 && (n < 1 || kinds[0] == 2) && (n < 2 || kinds[1] == g_kinds[0]) &&
                (n < 3 || kinds[2] == g_kinds[1]))
                print(line, RTL_NUMBER_OF(line), format, what, g_args[0], g_args[1]);
        }
        BOOL debug = r->Option == OPTION_OK_CANCEL;
        print(g_text, RTL_NUMBER_OF(g_text), L"%s\n%s%s%s", line, g_terminate, debug ? L"\n" : g_empty,
              debug ? g_debug : g_empty);
        return;
    }
    if (status == (ULONG)STATUS_SYSTEM_IMAGE_BAD_SIGNATURE) {
        print(g_text, TEXT_CHARS, format, g_args[0]);
        return;
    }
    UCHAR kinds[MIME_HARD_ERROR_PARAMETERS];
    int n = format_arguments(format, MIME_HARD_ERROR_PARAMETERS, kinds);
    BOOL fits = n >= 0;
    for (int i = 0; fits && i < n; i++)
        fits = kinds[i] == g_kinds[i];
    if (fits)
        print(g_text, TEXT_CHARS, format, g_args[0], g_args[1], g_args[2], g_args[3]);
    else
        print(g_text, TEXT_CHARS, L"Exception Processing Message 0x%x - Unexpected parameters", status);
    /* the message is one line. */
    for (PWSTR p = g_text; *p; p++) {
        if (p[0] == L'\r' && p[1] == L'\n')
            memmove(p, p + 1, (wcslen(p + 1) + 1) * sizeof(WCHAR));
        if (*p == L'\r' || *p == L'\n')
            *p = L' ';
    }
}

/* GetHardErrorMode: ErrorMode of the Windows key, 1 for no box of a system error, 2 for none at all. */
static ULONG hard_error_mode(void)
{
    UNICODE_STRING path = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Windows");
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"ErrorMode");
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE key;
    ULONG mode = 0, returned;
    if (!NT_SUCCESS(NtOpenKey(&key, KEY_READ, &attributes)))
        return 0;
    VALUE_PARTIAL value;
    if (NT_SUCCESS(NtQueryValueKey(key, &name, KEY_VALUE_PARTIAL, &value, sizeof(value), &returned)))
        mode = *(ULONG *)value.Data;
    NtClose(key);
    return mode;
}

/* csrss shows one box at a time, the others wait their turn. */
static DWORD WINAPI show(void *unused)
{
    HANDLE turn = CreateMutexW(nullptr, FALSE, L"MimeHardErrorBox");
    if (turn)
        WaitForSingleObject(turn, INFINITE);
    UINT style = (g_style & ~(MB_SERVICE_NOTIFICATION_FLAG | MB_SETFOREGROUND | MB_SYSTEMMODAL | MB_DEFAULT_DESKTOP_ONLY_FLAG)) |
                 MB_TOPMOST;
    int button = MessageBoxTimeoutW(nullptr, g_box_text, g_box_caption, style, 0, g_timeout);
    if (button == MB_TIMEDOUT)
        button = IDOK;
    if (turn)
        ReleaseMutex(turn);
    ExitProcess(g_responses[(unsigned)button < RTL_NUMBER_OF(g_responses) ? button : 0]);
}

static BOOL request_is_whole(const MIME_HARD_ERROR *r, ULONG size)
{
    if (size < sizeof(*r) || r->Size != size || r->Count > MIME_HARD_ERROR_PARAMETERS ||
        r->Option > OPTION_CANCEL_TRY_CONTINUE || r->ImageName > size || r->ImageNameLength > size - r->ImageName)
        return FALSE;
    for (ULONG i = 0; i < r->Count; i++)
        if ((r->StringMask >> i & 1) && (r->Parameters[i] > size || r->StringLength[i] > size - r->Parameters[i]))
            return FALSE;
    return TRUE;
}

/*
 * the first thread ends once the text is made, which tells the kernel to let a caller of OptionOkNoWait go on. the box
 * is shown on a thread of its own and its button, made a response, is the exit code.
 */
void entry(void)
{
    PRTL_USER_PROCESS_PARAMETERS parameters = NtCurrentPeb()->ProcessParameters;
    const MIME_HARD_ERROR *r = (const MIME_HARD_ERROR *)parameters->RuntimeData.Buffer;
    if (!r || !request_is_whole(r, parameters->RuntimeData.Length))
        ExitProcess(RESPONSE_NOT_HANDLED);
    g_request = r;
    read_parameters();
    if (r->Status == STATUS_SERVICE_NOTIFICATION) {
        g_box_text = *(const WCHAR *)g_args[0] ? (const WCHAR *)g_args[0] : nullptr;
        g_box_caption = *(const WCHAR *)g_args[1] ? (const WCHAR *)g_args[1] : nullptr;
        g_style = (UINT)g_args[2] & ~MB_SERVICE_NOTIFICATION_FLAG;
        if (r->Count == 4)
            g_timeout = (DWORD)g_args[3];
    } else if (r->Status == STATUS_VDM_HARD_ERROR) {
        /* the box of ntvdm, which no 64-bit windows has: its own buttons are not kept. */
        g_box_caption = *(const WCHAR *)g_args[2] ? (const WCHAR *)g_args[2] : nullptr;
        g_box_text = *(const WCHAR *)g_args[3] ? (const WCHAR *)g_args[3] : nullptr;
        g_style = MB_OK;
    } else {
        message_text();
        g_box_text = g_text;
        g_box_caption = g_caption;
    }
    ULONG mode = r->Status == STATUS_SERVICE_NOTIFICATION ? 0 : hard_error_mode();
    BOOL unseen = r->Status == STATUS_VDM_HARD_ERROR ? mode && mode != 1 : mode && (mode != 1 || g_no_image);
    if (unseen)
        ExitProcess(r->Status == STATUS_VDM_HARD_ERROR ? 6 : g_unseen[r->Option]);
    if (!CreateThread(nullptr, 0, show, nullptr, 0, nullptr))
        ExitProcess(RESPONSE_NOT_HANDLED);
    ExitThread(0);
}
