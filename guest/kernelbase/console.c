/*
 * console: the standard handles of a console process are pseudo handles,
 * as before windows 8, backed by the console driver (\Device\ConDrv,
 * mime/condrv.h) that every process opens. a program `mime run` started
 * shares that terminal: output goes to its stdout or stderr, input comes
 * from it, and ctrl+c, ctrl+break and closing it are ctrl events that the
 * driver delivers by starting ctrl_routine on a new thread. a gui program
 * has no console until AttachConsole or AllocConsole.
 */
#include "kernelbase_private.h"
#include <mime/condrv.h>
#include <mime/win32/winuser.h>

#define DEFAULT_OUTPUT_MODE (ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT)
#define MAX_WRITE 0x10000
#define MAX_RECORDS 256

/* a value win32k never hands out: user handles start at index 0x20. */
#define CONSOLE_WINDOW ((HWND)(ULONG_PTR)0x00FF0010)

static UINT g_input_cp = CP_UTF8;
static UINT g_output_cp = CP_UTF8;
static WORD g_attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
static WCHAR g_title[256];
static RTL_CRITICAL_SECTION g_console_lock = RTL_CRITICAL_SECTION_INIT;

static HANDLE g_condrv;      /* \Device\ConDrv */
static HANDLE g_input_event; /* set by the driver while input waits */
static BOOL g_attached;
static ULONG g_tty; /* MIME_CONDRV_TTY_... of the attached console */

static PHANDLER_ROUTINE g_ctrl_handlers[32];
static ULONG g_ctrl_handler_count;
static BOOL g_ignore_ctrl_c;

/* utf-8 bytes of a character split between two reads. */
static UCHAR g_read_carry[4];
static ULONG g_read_carry_length;

/* key events decoded from input but not read yet. */
static INPUT_RECORD g_records[MAX_RECORDS];
static ULONG g_record_count;

/* ------------------------------------------------------------------ the driver */

static NTSTATUS condrv(ULONG code, const void *in, ULONG in_len, void *out, ULONG out_len, ULONG_PTR *done)
{
    if (done)
        *done = 0;
    if (!g_condrv)
        return STATUS_INVALID_HANDLE;
    IO_STATUS_BLOCK io = {0};
    NTSTATUS status = NtDeviceIoControlFile(g_condrv, NULL, NULL, NULL, &io, code, (PVOID)in, in_len, out, out_len);
    if (done && NT_SUCCESS(status))
        *done = io.Information;
    return status;
}

static BOOL query_state(MIME_CONDRV_STATE *st)
{
    memset(st, 0, sizeof(*st));
    return NT_SUCCESS(condrv(IOCTL_CONDRV_STATE, NULL, 0, st, sizeof(*st), NULL)) && (st->Flags & MIME_CONDRV_ATTACHED);
}

/*
 * a standard handle `mime run` was given a pipe or a file for is no console
 * to the program, as a redirected handle on windows: the console functions
 * fail on it and ReadFile and WriteFile pass bytes through.
 */
static BOOL is_console(HANDLE h)
{
    if (!g_attached)
        return FALSE;
    if (h == KB_CONSOLE_INPUT)
        return (g_tty & MIME_CONDRV_TTY_INPUT) != 0;
    if (h == KB_CONSOLE_OUTPUT)
        return (g_tty & MIME_CONDRV_TTY_OUTPUT) != 0;
    if (h == KB_CONSOLE_ERROR)
        return (g_tty & MIME_CONDRV_TTY_ERROR) != 0;
    return FALSE;
}

BOOL KbConsoleIsRedirected(HANDLE h)
{
    return KbIsConsoleHandle(h) && g_attached && !is_console(h);
}

/* ------------------------------------------------------------------ text output */

/* debug output through NtDisplayString, chunked for its limits; console output goes to the driver. */
void KbDisplayText(LPCWSTR text, SIZE_T chars)
{
    /* NtDisplayString converts to at most 512 utf-8 bytes per call. */
    while (chars) {
        SIZE_T n = chars > 160 ? 160 : chars;
        if (n < chars && text[n - 1] >= 0xD800 && text[n - 1] <= 0xDBFF)
            n--;
        UNICODE_STRING s = {(USHORT)(n * sizeof(WCHAR)), (USHORT)(n * sizeof(WCHAR)), (PWSTR)text};
        NtDisplayString(&s);
        text += n;
        chars -= n;
    }
}

static BOOL is_output(HANDLE h)
{
    return h == KB_CONSOLE_OUTPUT || h == KB_CONSOLE_ERROR;
}

/* utf-8 bytes to the terminal of the console. */
static BOOL write_utf8(HANDLE console, const UCHAR *bytes, DWORD length)
{
    UCHAR buffer[sizeof(ULONG) + 4096];
    ULONG stream = console == KB_CONSOLE_ERROR ? MIME_CONDRV_STDERR : MIME_CONDRV_STDOUT;
    do {
        DWORD n = length;
        const UCHAR *in = bytes;
        PUCHAR heap = NULL;
        if (n > MAX_WRITE)
            n = MAX_WRITE;
        if (n <= sizeof(buffer) - sizeof(ULONG)) {
            in = buffer;
        } else {
            heap = KbAlloc(sizeof(ULONG) + n);
            if (!heap)
                return KbSetStatus(STATUS_NO_MEMORY);
            in = heap;
        }
        memcpy((PUCHAR)in, &stream, sizeof(ULONG));
        memcpy((PUCHAR)in + sizeof(ULONG), bytes, n);
        NTSTATUS status = condrv(IOCTL_CONDRV_WRITE, in, sizeof(ULONG) + n, NULL, 0, NULL);
        KbFree(heap);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status == STATUS_INVALID_HANDLE ? STATUS_INVALID_HANDLE : status);
        bytes += n;
        length -= n;
    } while (length);
    return TRUE;
}

BOOL KbConsoleWrite(HANDLE console, LPCWSTR text, DWORD chars)
{
    if (!is_output(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    char bytes[1024];
    BOOL ok = TRUE;
    RtlEnterCriticalSection(&g_console_lock);
    while (ok && chars) {
        DWORD n = chars > 256 ? 256 : chars;
        if (n < chars && text[n - 1] >= 0xD800 && text[n - 1] <= 0xDBFF)
            n--;
        int b = WideCharToMultiByte(CP_UTF8, 0, text, (int)n, bytes, sizeof(bytes), NULL, NULL);
        ok = b <= 0 || write_utf8(console, (const UCHAR *)bytes, (DWORD)b);
        text += n;
        chars -= n;
    }
    RtlLeaveCriticalSection(&g_console_lock);
    return ok;
}

/* bytes in the output code page; utf-8 goes through as is, the terminal joins split characters. */
BOOL KbConsoleWriteBytes(HANDLE console, const void *data, DWORD length)
{
    if (!is_output(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (g_output_cp == CP_UTF8) {
        RtlEnterCriticalSection(&g_console_lock);
        BOOL ok = write_utf8(console, data, length);
        RtlLeaveCriticalSection(&g_console_lock);
        return ok;
    }
    const UCHAR *bytes = data;
    WCHAR wide[512];
    while (length) {
        DWORD n = length > 256 ? 256 : length;
        int w = MultiByteToWideChar(g_output_cp, 0, (LPCCH)bytes, (int)n, wide, RTL_NUMBER_OF(wide));
        if (w > 0 && !KbConsoleWrite(console, wide, (DWORD)w))
            return FALSE;
        bytes += n;
        length -= n;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ input */

/* waits for input and returns what is there, 0 bytes at end of file. */
static BOOL read_bytes(PUCHAR buffer, DWORD length, LPDWORD read)
{
    ULONG_PTR done;
    NTSTATUS status = condrv(IOCTL_CONDRV_READ, NULL, 0, buffer, length, &done);
    *read = (DWORD)done;
    return NT_SUCCESS(status) || KbSetStatus(status);
}

/* the driver's input event follows its queue; decoded events waiting here keep it set. */
static void sync_input_event(void)
{
    if (g_record_count && g_input_event)
        NtSetEvent(g_input_event, NULL);
    else
        condrv(IOCTL_CONDRV_SYNC_EVENT, NULL, 0, NULL, 0, NULL);
}

static void key_record(INPUT_RECORD *r, BOOL down, WORD vk, WORD scan, WCHAR ch, DWORD state)
{
    memset(r, 0, sizeof(*r));
    r->EventType = KEY_EVENT;
    r->Event.KeyEvent.bKeyDown = down;
    r->Event.KeyEvent.wRepeatCount = 1;
    r->Event.KeyEvent.wVirtualKeyCode = vk;
    r->Event.KeyEvent.wVirtualScanCode = scan;
    r->Event.KeyEvent.uChar.UnicodeChar = ch;
    r->Event.KeyEvent.dwControlKeyState = state;
}

/* a key press and release, as the console reports a typed key. */
static ULONG key_pair(INPUT_RECORD *out, ULONG room, WORD vk, WORD scan, WCHAR ch, DWORD state)
{
    if (room < 2)
        return 0;
    key_record(&out[0], TRUE, vk, scan, ch, state);
    key_record(&out[1], FALSE, vk, scan, ch, state);
    return 2;
}

/* the us layout key of a printable ascii character: virtual key, scan code, shift. */
static void ascii_key(UCHAR c, WORD *vk, WORD *scan, BOOL *shift)
{
    static const char row1[] = "1234567890", shifted1[] = "!@#$%^&*()";
    static const struct {
        char plain, shifted;
        WORD vk, scan;
    } oem[] = {
        {';', ':', 0xBA, 0x27}, {'=', '+', 0xBB, 0x0D}, {',', '<', 0xBC, 0x33}, {'-', '_', 0xBD, 0x0C},
        {'.', '>', 0xBE, 0x34}, {'/', '?', 0xBF, 0x35}, {'`', '~', 0xC0, 0x29}, {'[', '{', 0xDB, 0x1A},
        {'\\', '|', 0xDC, 0x2B}, {']', '}', 0xDD, 0x1B}, {'\'', '"', 0xDE, 0x28},
    };
    static const UCHAR letter_scan[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                          0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    *vk = 0;
    *scan = 0;
    *shift = FALSE;
    if (c >= 'a' && c <= 'z') {
        *vk = (WORD)(c - 'a' + 'A');
        *scan = letter_scan[c - 'a'];
    } else if (c >= 'A' && c <= 'Z') {
        *vk = c;
        *scan = letter_scan[c - 'A'];
        *shift = TRUE;
    } else if (c == ' ') {
        *vk = VK_SPACE;
        *scan = 0x39;
    } else {
        for (int i = 0; i < 10; i++) {
            if (c == row1[i] || c == shifted1[i]) {
                *vk = (WORD)row1[i];
                *scan = (WORD)(i + 2);
                *shift = c == shifted1[i];
                return;
            }
        }
        for (ULONG i = 0; i < RTL_NUMBER_OF(oem); i++) {
            if (c == (UCHAR)oem[i].plain || c == (UCHAR)oem[i].shifted) {
                *vk = oem[i].vk;
                *scan = oem[i].scan;
                *shift = c == (UCHAR)oem[i].shifted;
                return;
            }
        }
    }
}

/* the escape sequences terminals send for the keys without a character. */
static const struct {
    const char *seq;
    WORD vk, scan;
} g_escape_keys[] = {
    {"[A", VK_UP, 0x48},       {"[B", VK_DOWN, 0x50},   {"[C", VK_RIGHT, 0x4D},  {"[D", VK_LEFT, 0x4B},
    {"[H", VK_HOME, 0x47},     {"[F", VK_END, 0x4F},    {"OA", VK_UP, 0x48},     {"OB", VK_DOWN, 0x50},
    {"OC", VK_RIGHT, 0x4D},    {"OD", VK_LEFT, 0x4B},   {"OH", VK_HOME, 0x47},   {"OF", VK_END, 0x4F},
    {"OP", VK_F1, 0x3B},       {"OQ", VK_F2, 0x3C},     {"OR", VK_F3, 0x3D},     {"OS", VK_F4, 0x3E},
    {"[1~", VK_HOME, 0x47},    {"[2~", VK_INSERT, 0x52}, {"[3~", VK_DELETE, 0x53}, {"[4~", VK_END, 0x4F},
    {"[5~", VK_PRIOR, 0x49},   {"[6~", VK_NEXT, 0x51},  {"[7~", VK_HOME, 0x47},  {"[8~", VK_END, 0x4F},
    {"[15~", VK_F5, 0x3F},     {"[17~", VK_F6, 0x40},   {"[18~", VK_F7, 0x41},   {"[19~", VK_F8, 0x42},
    {"[20~", VK_F9, 0x43},     {"[21~", VK_F10, 0x44},  {"[23~", VK_F11, 0x57},  {"[24~", VK_F12, 0x58},
    {"[Z", VK_TAB, 0x0F},
};

/* decodes the escape sequence at p into a key. */
static BOOL escape_key(const UCHAR *p, ULONG length, ULONG *used, WORD *vk, WORD *scan, DWORD *state)
{
    /* modified keys carry ;<modifier> before the final byte: ESC [ 1 ; 5 C is ctrl+right. */
    UCHAR seq[16];
    ULONG n = 0, modifier = 0;
    ULONG i = 1;
    while (i < length && n < sizeof(seq) - 1) {
        UCHAR c = p[i++];
        if (c == ';') {
            while (i < length && p[i] >= '0' && p[i] <= '9')
                modifier = modifier * 10 + (p[i++] - '0');
            continue;
        }
        seq[n++] = c;
        if (n > 1 && (c == '~' || (c >= 'A' && c <= 'Z')))
            break;
    }
    seq[n] = 0;
    /* ESC [ 1 ; m X keeps the 1 only for the modifier. */
    if (modifier && n == 3 && seq[0] == '[' && seq[1] == '1' && seq[2] != '~') {
        seq[1] = seq[2];
        seq[2] = 0;
    }
    for (ULONG k = 0; k < RTL_NUMBER_OF(g_escape_keys); k++) {
        if (strcmp((const char *)seq, g_escape_keys[k].seq))
            continue;
        *vk = g_escape_keys[k].vk;
        *scan = g_escape_keys[k].scan;
        *state = g_escape_keys[k].vk == VK_TAB ? SHIFT_PRESSED : ENHANCED_KEY;
        if (modifier > 1) {
            ULONG m = modifier - 1;
            if (m & 1)
                *state |= SHIFT_PRESSED;
            if (m & 2)
                *state |= LEFT_ALT_PRESSED;
            if (m & 4)
                *state |= LEFT_CTRL_PRESSED;
        }
        *used = i;
        return TRUE;
    }
    return FALSE;
}

/* turns terminal input into key events; returns the bytes used, leaving an incomplete character. */
static ULONG decode_keys(const UCHAR *p, ULONG length, INPUT_RECORD *out, ULONG room, ULONG *count)
{
    ULONG i = 0;
    *count = 0;
    while (i < length) {
        UCHAR c = p[i];
        ULONG used = 1, made = 0;
        WORD vk, scan;
        DWORD state = 0;
        if (c == 0x1B) {
            if (i + 1 < length && (p[i + 1] == '[' || p[i + 1] == 'O') &&
                escape_key(p + i, length - i, &used, &vk, &scan, &state)) {
                made = key_pair(out + *count, room - *count, vk, scan, 0, state);
            } else if (i + 1 < length && p[i + 1] >= 0x20 && p[i + 1] < 0x7F) {
                /* option+key on a terminal is escape then the key: alt+key. */
                BOOL shift;
                ascii_key(p[i + 1], &vk, &scan, &shift);
                made = key_pair(out + *count, room - *count, vk, scan, p[i + 1],
                                LEFT_ALT_PRESSED | (shift ? SHIFT_PRESSED : 0));
                used = 2;
            } else {
                made = key_pair(out + *count, room - *count, VK_ESCAPE, 0x01, 0x1B, 0);
            }
        } else if (c == '\r' || c == '\n') {
            made = key_pair(out + *count, room - *count, VK_RETURN, 0x1C, '\r', 0);
            if (c == '\r' && i + 1 < length && p[i + 1] == '\n')
                used = 2;
        } else if (c == 0x7F || c == 0x08) {
            made = key_pair(out + *count, room - *count, VK_BACK, 0x0E, 0x08, 0);
        } else if (c == '\t') {
            made = key_pair(out + *count, room - *count, VK_TAB, 0x0F, '\t', 0);
        } else if (c == 0) {
            made = key_pair(out + *count, room - *count, VK_SPACE, 0x39, 0, LEFT_CTRL_PRESSED);
        } else if (c < 0x20) {
            /* ctrl+letter. */
            BOOL shift;
            ascii_key((UCHAR)(c + 'a' - 1), &vk, &scan, &shift);
            made = key_pair(out + *count, room - *count, vk, scan, c, LEFT_CTRL_PRESSED);
        } else if (c < 0x80) {
            BOOL shift;
            ascii_key(c, &vk, &scan, &shift);
            made = key_pair(out + *count, room - *count, vk, scan, c, shift ? SHIFT_PRESSED : 0);
        } else {
            /* a utf-8 character: one event per utf-16 unit. */
            ULONG need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            if (i + need > length)
                break;
            WCHAR w[2];
            int units = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)p + i, (int)need, w, 2);
            if (units <= 0) {
                units = 1;
                w[0] = 0xFFFD;
            }
            if (room - *count < (ULONG)units * 2)
                break;
            for (int u = 0; u < units; u++)
                made += key_pair(out + *count + made, room - *count - made, VK_PACKET, 0, w[u], 0);
            used = need;
        }
        if (!made)
            break;
        *count += made;
        i += used;
    }
    return i;
}

static BOOL check_input(HANDLE console)
{
    if (!KbIsConsoleInput(console) || !is_console(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ ctrl events */

/*
 * the console driver starts this on a new thread for each ctrl event, as
 * csrss starts kernel32's CtrlRoutine. handlers run newest first; when none
 * handles the event, and always after a close, the process exits.
 */
static DWORD WINAPI ctrl_routine(LPVOID parameter)
{
    DWORD event = (DWORD)(ULONG_PTR)parameter;
    if (event == CTRL_C_EVENT && g_ignore_ctrl_c)
        return 0;
    PHANDLER_ROUTINE handlers[RTL_NUMBER_OF(g_ctrl_handlers)];
    RtlEnterCriticalSection(&g_console_lock);
    ULONG count = g_ctrl_handler_count;
    memcpy(handlers, g_ctrl_handlers, count * sizeof(handlers[0]));
    RtlLeaveCriticalSection(&g_console_lock);
    BOOL handled = FALSE;
    for (ULONG i = count; i-- > 0 && !handled;)
        handled = handlers[i](event);
    if (!handled || event >= CTRL_CLOSE_EVENT)
        ExitProcess(STATUS_CONTROL_C_EXIT);
    return 0;
}

/* ------------------------------------------------------------------ standard handles */

/*
 * a stream the terminal has is a console pseudo handle; a stream `mime run`
 * was given a pipe or a file for is a handle to its end of the console's
 * stream device, which programs see as a pipe, as on windows.
 */
static HANDLE standard_handle(HANDLE pseudo, ULONG tty, PCWSTR stream, ACCESS_MASK access)
{
    if (g_tty & tty)
        return pseudo;
    WCHAR path[64];
    UNICODE_STRING name;
    wcscpy(path, MIME_CONDRV_STREAM_DEVICE L"\\");
    wcscat(path, stream);
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_INHERIT, NULL, NULL);
    IO_STATUS_BLOCK io;
    HANDLE h;
    if (!NT_SUCCESS(NtCreateFile(&h, access | SYNCHRONIZE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, &oa, &io,
                                 NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT,
                                 NULL, 0)))
        return pseudo;
    return h;
}

static void set_console_handles(BOOL only_missing)
{
    PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
    if (!only_missing || !pp->StandardInput)
        pp->StandardInput = standard_handle(KB_CONSOLE_INPUT, MIME_CONDRV_TTY_INPUT, L"Input", GENERIC_READ);
    if (!only_missing || !pp->StandardOutput)
        pp->StandardOutput = standard_handle(KB_CONSOLE_OUTPUT, MIME_CONDRV_TTY_OUTPUT, L"Output", GENERIC_WRITE);
    if (!only_missing || !pp->StandardError)
        pp->StandardError = standard_handle(KB_CONSOLE_ERROR, MIME_CONDRV_TTY_ERROR, L"Error", GENERIC_WRITE);
}

void KbInitializeConsole(void)
{
    UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_CONDRV_DEVICE);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, 0, NULL, NULL);
    IO_STATUS_BLOCK io;
    if (!NT_SUCCESS(NtCreateFile(&g_condrv, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io, NULL, 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0)))
        g_condrv = NULL;
    if (!NT_SUCCESS(NtCreateEvent(&g_input_event, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE)))
        g_input_event = NULL;
    MIME_CONDRV_REGISTER r = {(ULONG64)(ULONG_PTR)ctrl_routine, (ULONG64)(ULONG_PTR)g_input_event};
    condrv(IOCTL_CONDRV_REGISTER, &r, sizeof(r), NULL, 0, NULL);
    MIME_CONDRV_STATE st;
    g_attached = query_state(&st);
    g_tty = st.Flags;
    if (g_attached)
        sync_input_event();

    PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
    /* CREATE_NEW_PROCESS_GROUP and SetConsoleCtrlHandler(NULL, TRUE) pass this on. */
    g_ignore_ctrl_c = (pp->ConsoleFlags & 1) != 0;
    if (g_attached)
        set_console_handles(TRUE);
    if (pp->WindowTitle.Length) {
        SIZE_T n = pp->WindowTitle.Length / sizeof(WCHAR);
        if (n >= RTL_NUMBER_OF(g_title))
            n = RTL_NUMBER_OF(g_title) - 1;
        memcpy(g_title, pp->WindowTitle.Buffer, n * sizeof(WCHAR));
        g_title[n] = 0;
    }
}

/* what a wait on a console pseudo handle waits for: the input event, or nothing for always signaled. */
HANDLE KbConsoleWaitHandle(HANDLE h)
{
    if (h != KB_CONSOLE_INPUT || !g_attached || !g_input_event)
        return NULL;
    sync_input_event();
    return g_input_event;
}

HANDLE WINAPI GetStdHandle(DWORD which)
{
    PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
    switch (which) {
    case STD_INPUT_HANDLE:
        return pp->StandardInput;
    case STD_OUTPUT_HANDLE:
        return pp->StandardOutput;
    case STD_ERROR_HANDLE:
        return pp->StandardError;
    default:
        KbSetError(ERROR_INVALID_HANDLE);
        return INVALID_HANDLE_VALUE;
    }
}

BOOL WINAPI SetStdHandleEx(DWORD which, HANDLE handle, PHANDLE previous)
{
    PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
    PHANDLE slot;
    switch (which) {
    case STD_INPUT_HANDLE:
        slot = &pp->StandardInput;
        break;
    case STD_OUTPUT_HANDLE:
        slot = &pp->StandardOutput;
        break;
    case STD_ERROR_HANDLE:
        slot = &pp->StandardError;
        break;
    default:
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (previous)
        *previous = *slot;
    *slot = handle;
    return TRUE;
}

BOOL WINAPI SetStdHandle(DWORD which, HANDLE handle)
{
    return SetStdHandleEx(which, handle, NULL);
}

/* ------------------------------------------------------------------ console management */

static BOOL attached_now(void)
{
    MIME_CONDRV_STATE st;
    g_attached = query_state(&st);
    g_tty = st.Flags;
    if (g_attached)
        sync_input_event();
    return g_attached;
}

/*
 * windows makes a new console window; mime has no console host, so the
 * console is the terminal of the run that started the program while it is
 * still open.
 */
BOOL WINAPI AllocConsole(void)
{
    if (g_attached) {
        KbSetError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    ULONG pid = ATTACH_PARENT_PROCESS;
    NTSTATUS status = condrv(IOCTL_CONDRV_ATTACH, &pid, sizeof(pid), NULL, 0, NULL);
    if (!NT_SUCCESS(status)) {
        KbSetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    attached_now();
    set_console_handles(FALSE);
    return TRUE;
}

BOOL WINAPI FreeConsole(void)
{
    condrv(IOCTL_CONDRV_FREE, NULL, 0, NULL, 0, NULL);
    g_attached = FALSE;
    g_record_count = 0;
    return TRUE;
}

BOOL WINAPI AttachConsole(DWORD process)
{
    ULONG pid = process;
    NTSTATUS status = condrv(IOCTL_CONDRV_ATTACH, &pid, sizeof(pid), NULL, 0, NULL);
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_ACCESS_DENIED    ? ERROR_ACCESS_DENIED
                   : status == STATUS_INVALID_HANDLE ? ERROR_INVALID_HANDLE
                                                     : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    attached_now();
    set_console_handles(TRUE);
    return TRUE;
}

/* there is no console window, but programs such as qt's logging only ask whether one exists. */
HWND WINAPI GetConsoleWindow(void)
{
    return g_attached ? CONSOLE_WINDOW : NULL;
}

UINT WINAPI GetConsoleCP(void)
{
    return g_input_cp;
}

UINT WINAPI GetConsoleOutputCP(void)
{
    return g_output_cp;
}

BOOL WINAPI SetConsoleCP(UINT cp)
{
    if (!IsValidCodePage(cp)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    g_input_cp = cp;
    return TRUE;
}

BOOL WINAPI SetConsoleOutputCP(UINT cp)
{
    if (!IsValidCodePage(cp)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlEnterCriticalSection(&g_console_lock);
    g_output_cp = cp;
    RtlLeaveCriticalSection(&g_console_lock);
    return TRUE;
}

/* the modes belong to the console, shared by its processes; the terminal follows the input mode. */
BOOL WINAPI GetConsoleMode(HANDLE console, LPDWORD mode)
{
    MIME_CONDRV_STATE st;
    if (!is_console(console) || !query_state(&st)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!mode) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    *mode = KbIsConsoleInput(console) ? st.InputMode : st.OutputMode;
    return TRUE;
}

BOOL WINAPI SetConsoleMode(HANDLE console, DWORD mode)
{
    MIME_CONDRV_STATE st;
    if (!is_console(console) || !query_state(&st)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    ULONG modes[2] = {st.InputMode, st.OutputMode};
    if (KbIsConsoleInput(console)) {
        if (mode & ~(DWORD)0x3FF) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        modes[0] = mode;
    } else {
        if (mode & ~(DWORD)0x1F) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        modes[1] = mode;
    }
    return NT_SUCCESS(condrv(IOCTL_CONDRV_SET_MODE, modes, sizeof(modes), NULL, 0, NULL)) ||
           KbSetStatus(STATUS_INVALID_HANDLE);
}

BOOL WINAPI WriteConsoleW(HANDLE console, const void *buffer, DWORD count, LPDWORD written, LPVOID reserved)
{
    if (written)
        *written = 0;
    if (!is_console(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!buffer && count) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    if (!KbConsoleWrite(console, buffer, count))
        return FALSE;
    if (written)
        *written = count;
    return TRUE;
}

BOOL WINAPI WriteConsoleA(HANDLE console, const void *buffer, DWORD count, LPDWORD written, LPVOID reserved)
{
    if (written)
        *written = 0;
    if (!is_console(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!buffer && count) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    if (!KbConsoleWriteBytes(console, buffer, count))
        return FALSE;
    if (written)
        *written = count;
    return TRUE;
}

static WCHAR g_read_pending; /* the low surrogate of a pair that did not fit */

/* reads characters, waiting for at least one; none at end of file. */
BOOL WINAPI ReadConsoleW(HANDLE console, LPVOID buffer, DWORD count, LPDWORD read,
                         PCONSOLE_READCONSOLE_CONTROL control)
{
    if (read)
        *read = 0;
    if (!check_input(console))
        return FALSE;
    if (!buffer && count) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    LPWSTR out = buffer;
    DWORD done = 0;
    if (count && g_read_pending) {
        out[done++] = g_read_pending;
        g_read_pending = 0;
    }
    while (!done && count) {
        UCHAR bytes[4096 + 4];
        memcpy(bytes, g_read_carry, g_read_carry_length);
        DWORD want = count < 4096 ? count : 4096;
        DWORD got;
        if (!read_bytes(bytes + g_read_carry_length, want, &got))
            return FALSE;
        if (!got)
            break;
        ULONG length = g_read_carry_length + got;
        /* keep a character the read split for the next one. */
        ULONG end = length, back = 0;
        while (back < 3 && back < length && (bytes[length - 1 - back] & 0xC0) == 0x80)
            back++;
        if (back < length) {
            UCHAR lead = bytes[length - 1 - back];
            ULONG need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (need > back + 1)
                end = length - 1 - back;
        }
        g_read_carry_length = length - end;
        memcpy(g_read_carry, bytes + end, g_read_carry_length);
        WCHAR wide[4096 + 4];
        int w = end ? MultiByteToWideChar(CP_UTF8, 0, (LPCCH)bytes, (int)end, wide, RTL_NUMBER_OF(wide)) : 0;
        for (int i = 0; i < w; i++) {
            if (done < count)
                out[done++] = wide[i];
            else
                g_read_pending = wide[i];
        }
    }
    if (read)
        *read = done;
    return TRUE;
}

/* bytes in the input code page: the terminal's utf-8 as is. */
BOOL WINAPI ReadConsoleA(HANDLE console, LPVOID buffer, DWORD count, LPDWORD read,
                         PCONSOLE_READCONSOLE_CONTROL control)
{
    if (read)
        *read = 0;
    if (!check_input(console))
        return FALSE;
    if (g_input_cp == CP_UTF8) {
        DWORD got = 0;
        if (!read_bytes(buffer, count, &got))
            return FALSE;
        if (read)
            *read = got;
        return TRUE;
    }
    WCHAR wide[1024];
    DWORD chars = 0;
    if (!ReadConsoleW(console, wide, count < RTL_NUMBER_OF(wide) ? count : RTL_NUMBER_OF(wide), &chars, control))
        return FALSE;
    int n = chars ? WideCharToMultiByte(g_input_cp, 0, wide, (int)chars, buffer, (int)count, NULL, NULL) : 0;
    if (read)
        *read = n > 0 ? (DWORD)n : 0;
    return TRUE;
}

/* ReadFile of the console: characters in the input code page, or the bytes of a redirected input. */
BOOL KbConsoleRead(HANDLE console, LPVOID buffer, DWORD length, LPDWORD read)
{
    *read = 0;
    if (console == KB_CONSOLE_INPUT && KbConsoleIsRedirected(console))
        return read_bytes(buffer, length, read);
    return ReadConsoleA(console, buffer, length, read, NULL);
}

/* bytes read from the driver that do not make a whole key yet. */
static UCHAR g_key_carry[16];
static ULONG g_key_carry_length;

/* decodes input into g_records, waiting for a key when asked to. */
static BOOL read_records(void)
{
    while (!g_record_count) {
        UCHAR bytes[MAX_RECORDS / 2 + sizeof(g_key_carry)];
        memcpy(bytes, g_key_carry, g_key_carry_length);
        DWORD got;
        if (!read_bytes(bytes + g_key_carry_length, MAX_RECORDS / 2, &got))
            return FALSE;
        if (!got)
            return TRUE; /* end of file: no more events */
        ULONG length = g_key_carry_length + got, count;
        ULONG used = decode_keys(bytes, length, g_records, MAX_RECORDS, &count);
        g_key_carry_length = length - used < sizeof(g_key_carry) ? length - used : 0;
        memcpy(g_key_carry, bytes + used, g_key_carry_length);
        g_record_count = count;
    }
    return TRUE;
}

/* the events waiting, decoded from queued input without taking it. */
static ULONG peek_records(INPUT_RECORD *out, ULONG room)
{
    if (g_record_count) {
        ULONG n = g_record_count < room ? g_record_count : room;
        memcpy(out, g_records, n * sizeof(*out));
        return g_record_count;
    }
    UCHAR bytes[MAX_RECORDS / 2 + sizeof(g_key_carry)];
    memcpy(bytes, g_key_carry, g_key_carry_length);
    ULONG_PTR got = 0;
    condrv(IOCTL_CONDRV_PEEK, NULL, 0, bytes + g_key_carry_length, MAX_RECORDS / 2, &got);
    INPUT_RECORD all[MAX_RECORDS];
    ULONG count;
    decode_keys(bytes, g_key_carry_length + (ULONG)got, all, MAX_RECORDS, &count);
    memcpy(out, all, (count < room ? count : room) * sizeof(*out));
    return count;
}

static void take_records(PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    DWORD n = g_record_count < count ? g_record_count : count;
    memcpy(buffer, g_records, n * sizeof(*buffer));
    memmove(g_records, g_records + n, (g_record_count - n) * sizeof(*buffer));
    g_record_count -= n;
    if (read)
        *read = n;
    sync_input_event();
}

/* key events made from what the terminal sends: characters, and escape sequences for the other keys. */
BOOL WINAPI ReadConsoleInputW(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    if (read)
        *read = 0;
    if (!check_input(console))
        return FALSE;
    if (!count)
        return TRUE;
    if (!read_records())
        return FALSE;
    take_records(buffer, count, read);
    return TRUE;
}

/* ansi records carry the character in the input code page. */
static void records_to_ansi(PINPUT_RECORD records, DWORD count)
{
    for (DWORD i = 0; i < count; i++) {
        if (records[i].EventType != KEY_EVENT)
            continue;
        WCHAR w = records[i].Event.KeyEvent.uChar.UnicodeChar;
        CHAR a = 0;
        if (w && WideCharToMultiByte(g_input_cp == CP_UTF8 ? CP_ACP : g_input_cp, 0, &w, 1, &a, 1, NULL, NULL) != 1)
            a = '?';
        records[i].Event.KeyEvent.uChar.UnicodeChar = 0;
        records[i].Event.KeyEvent.uChar.AsciiChar = w < 0x80 ? (CHAR)w : a;
    }
}

BOOL WINAPI ReadConsoleInputA(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    DWORD n = 0;
    if (!ReadConsoleInputW(console, buffer, count, &n))
        return FALSE;
    records_to_ansi(buffer, n);
    if (read)
        *read = n;
    return TRUE;
}

/* what a terminal sends for a key without a character, 0 bytes for a key it has no sequence for. */
static ULONG encode_escape(const KEY_EVENT_RECORD *k, UCHAR *out)
{
    DWORD state = k->dwControlKeyState;
    BOOL shift = (state & SHIFT_PRESSED) != 0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_escape_keys); i++) {
        const char *seq = g_escape_keys[i].seq;
        if (g_escape_keys[i].vk != k->wVirtualKeyCode || (k->wVirtualKeyCode == VK_TAB && !shift))
            continue;
        ULONG modifier = 1 + (shift && k->wVirtualKeyCode != VK_TAB ? 1 : 0) +
                         (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED) ? 2 : 0) +
                         (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED) ? 4 : 0);
        ULONG n = 0, length = strlen(seq);
        out[n++] = 0x1B;
        if (modifier == 1) {
            memcpy(out + n, seq, length);
            return n + length;
        }
        /* ESC [ 1 ; m X for a letter, ESC [ n ; m ~ for a number. */
        out[n++] = '[';
        if (seq[length - 1] == '~') {
            memcpy(out + n, seq + 1, length - 2);
            n += length - 2;
        } else {
            out[n++] = '1';
        }
        out[n++] = ';';
        out[n++] = (UCHAR)('0' + modifier);
        out[n++] = (UCHAR)seq[length - 1];
        return n;
    }
    return 0;
}

/*
 * written records join the console's input as what a terminal would send
 * for them, so a read waiting in any attached process returns, as libuv
 * expects when it writes an enter to end its ReadConsoleW. only key
 * presses with a character or an escape sequence make it there.
 */
BOOL WINAPI WriteConsoleInputW(HANDLE console, const INPUT_RECORD *buffer, DWORD count, LPDWORD written)
{
    if (written)
        *written = 0;
    if (!check_input(console))
        return FALSE;
    UCHAR bytes[1024];
    ULONG length = 0;
    WCHAR high = 0;
    for (DWORD i = 0; i < count; i++) {
        const KEY_EVENT_RECORD *k = &buffer[i].Event.KeyEvent;
        if (buffer[i].EventType != KEY_EVENT || !k->bKeyDown)
            continue;
        UCHAR one[16];
        ULONG n = 0;
        WCHAR c = k->uChar.UnicodeChar;
        DWORD state = k->dwControlKeyState;
        if (c >= 0xD800 && c < 0xDC00) {
            high = c;
            continue;
        }
        if (c) {
            WCHAR pair[2] = {high ? high : c, c};
            BOOL alt = (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) &&
                       !(state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED));
            if (alt)
                one[n++] = 0x1B;
            int w = WideCharToMultiByte(CP_UTF8, 0, pair, high ? 2 : 1, (LPSTR)one + n, sizeof(one) - n, NULL, NULL);
            n += w > 0 ? (ULONG)w : 0;
        } else {
            n = encode_escape(k, one);
        }
        high = 0;
        for (WORD r = k->wRepeatCount ? k->wRepeatCount : 1; r && n; r--) {
            if (length + n > sizeof(bytes)) {
                NTSTATUS status = condrv(IOCTL_CONDRV_WRITE_INPUT, bytes, length, NULL, 0, NULL);
                if (!NT_SUCCESS(status))
                    return KbSetStatus(status);
                length = 0;
            }
            memcpy(bytes + length, one, n);
            length += n;
        }
    }
    if (length) {
        NTSTATUS status = condrv(IOCTL_CONDRV_WRITE_INPUT, bytes, length, NULL, 0, NULL);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
    }
    if (written)
        *written = count;
    return TRUE;
}

BOOL WINAPI WriteConsoleInputA(HANDLE console, const INPUT_RECORD *buffer, DWORD count, LPDWORD written)
{
    INPUT_RECORD wide[MAX_RECORDS];
    if (count > MAX_RECORDS)
        count = MAX_RECORDS;
    memcpy(wide, buffer, count * sizeof(*buffer));
    for (DWORD i = 0; i < count; i++) {
        if (wide[i].EventType != KEY_EVENT)
            continue;
        CHAR a = buffer[i].Event.KeyEvent.uChar.AsciiChar;
        WCHAR w = 0;
        if (a && MultiByteToWideChar(g_input_cp == CP_UTF8 ? CP_ACP : g_input_cp, 0, &a, 1, &w, 1) != 1)
            w = L'?';
        wide[i].Event.KeyEvent.uChar.UnicodeChar = w;
    }
    return WriteConsoleInputW(console, wide, count, written);
}

BOOL WINAPI PeekConsoleInputW(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    if (read)
        *read = 0;
    if (!check_input(console))
        return FALSE;
    INPUT_RECORD all[MAX_RECORDS];
    ULONG n = peek_records(all, MAX_RECORDS);
    if (n > count)
        n = count;
    if (n > MAX_RECORDS)
        n = MAX_RECORDS;
    memcpy(buffer, all, n * sizeof(*buffer));
    if (read)
        *read = n;
    return TRUE;
}

BOOL WINAPI PeekConsoleInputA(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    DWORD n = 0;
    if (!PeekConsoleInputW(console, buffer, count, &n))
        return FALSE;
    records_to_ansi(buffer, n);
    if (read)
        *read = n;
    return TRUE;
}

BOOL WINAPI GetNumberOfConsoleInputEvents(HANDLE console, LPDWORD count)
{
    if (!check_input(console))
        return FALSE;
    INPUT_RECORD all[MAX_RECORDS];
    ULONG n = peek_records(all, MAX_RECORDS);
    if (count)
        *count = n;
    return TRUE;
}

BOOL WINAPI FlushConsoleInputBuffer(HANDLE console)
{
    if (!check_input(console))
        return FALSE;
    g_record_count = 0;
    g_key_carry_length = 0;
    g_read_carry_length = 0;
    condrv(IOCTL_CONDRV_FLUSH_INPUT, NULL, 0, NULL, 0, NULL);
    return TRUE;
}

BOOL WINAPI SetConsoleTitleW(LPCWSTR title)
{
    if (!title) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlEnterCriticalSection(&g_console_lock);
    SIZE_T n = wcslen(title);
    if (n >= RTL_NUMBER_OF(g_title))
        n = RTL_NUMBER_OF(g_title) - 1;
    memcpy(g_title, title, n * sizeof(WCHAR));
    g_title[n] = 0;
    RtlLeaveCriticalSection(&g_console_lock);
    return TRUE;
}

BOOL WINAPI SetConsoleTitleA(LPCSTR title)
{
    PWSTR w = KbAnsiToWideCp(CP_ACP, title);
    if (!w) {
        if (!title)
            KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = SetConsoleTitleW(w);
    KbFree(w);
    return ok;
}

DWORD WINAPI GetConsoleTitleW(LPWSTR title, DWORD size)
{
    if (!title || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    RtlEnterCriticalSection(&g_console_lock);
    DWORD n = (DWORD)wcslen(g_title);
    DWORD copy = n < size ? n : size - 1;
    memcpy(title, g_title, copy * sizeof(WCHAR));
    title[copy] = 0;
    RtlLeaveCriticalSection(&g_console_lock);
    return n;
}

DWORD WINAPI GetConsoleTitleA(LPSTR title, DWORD size)
{
    if (!title || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WCHAR w[256];
    DWORD n = GetConsoleTitleW(w, RTL_NUMBER_OF(w));
    int got = WideCharToMultiByte(CP_ACP, 0, w, -1, title, (int)size, NULL, NULL);
    if (!got)
        title[size - 1] = 0;
    return n;
}

static BOOL check_output(HANDLE console)
{
    if (!is_output(console) || !is_console(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

/* the buffer is the terminal's window, as a conpty console has no scrollback of its own. */
BOOL WINAPI GetConsoleScreenBufferInfo(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFO info)
{
    if (!check_output(console))
        return FALSE;
    MIME_CONDRV_STATE st;
    if (!query_state(&st)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    SHORT columns = (SHORT)st.Columns, rows = (SHORT)st.Rows;
    info->dwSize.X = columns;
    info->dwSize.Y = rows;
    info->dwCursorPosition.X = 0;
    info->dwCursorPosition.Y = 0;
    info->wAttributes = g_attributes;
    info->srWindow.Left = 0;
    info->srWindow.Top = 0;
    info->srWindow.Right = (SHORT)(columns - 1);
    info->srWindow.Bottom = (SHORT)(rows - 1);
    info->dwMaximumWindowSize.X = columns;
    info->dwMaximumWindowSize.Y = rows;
    return TRUE;
}

BOOL WINAPI GetConsoleScreenBufferInfoEx(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFOEX info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    CONSOLE_SCREEN_BUFFER_INFO basic;
    if (!GetConsoleScreenBufferInfo(console, &basic))
        return FALSE;
    info->dwSize = basic.dwSize;
    info->dwCursorPosition = basic.dwCursorPosition;
    info->wAttributes = basic.wAttributes;
    info->srWindow = basic.srWindow;
    info->dwMaximumWindowSize = basic.dwMaximumWindowSize;
    info->wPopupAttributes = 0xF5;
    info->bFullscreenSupported = FALSE;
    static const COLORREF table[16] = {0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080,
                                       0x008080, 0xC0C0C0, 0x808080, 0xFF0000, 0x00FF00, 0xFFFF00,
                                       0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    memcpy(info->ColorTable, table, sizeof(table));
    return TRUE;
}

BOOL WINAPI SetConsoleScreenBufferSize(HANDLE console, COORD size)
{
    return check_output(console);
}

/* maps windows console colors onto ansi escape sequences for the terminal. */
BOOL WINAPI SetConsoleTextAttribute(HANDLE console, WORD attributes)
{
    if (!check_output(console))
        return FALSE;
    RtlEnterCriticalSection(&g_console_lock);
    g_attributes = attributes;
    WCHAR seq[32];
    if ((attributes & 0xFF) == (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)) {
        wcscpy(seq, L"\x1b[0m");
    } else {
        int fg = ((attributes & FOREGROUND_RED) ? 1 : 0) | ((attributes & FOREGROUND_GREEN) ? 2 : 0) |
                 ((attributes & FOREGROUND_BLUE) ? 4 : 0);
        int bg = ((attributes & BACKGROUND_RED) ? 1 : 0) | ((attributes & BACKGROUND_GREEN) ? 2 : 0) |
                 ((attributes & BACKGROUND_BLUE) ? 4 : 0);
        KbFormat(seq, RTL_NUMBER_OF(seq), L"\x1b[0;%d;%dm", (attributes & FOREGROUND_INTENSITY ? 90 : 30) + fg,
                   (attributes & BACKGROUND_INTENSITY ? 100 : 40) + bg);
    }
    RtlLeaveCriticalSection(&g_console_lock);
    return KbConsoleWrite(console, seq, (DWORD)wcslen(seq));
}

BOOL WINAPI SetConsoleCursorPosition(HANDLE console, COORD position)
{
    return check_output(console);
}

BOOL WINAPI GetConsoleCursorInfo(HANDLE console, PCONSOLE_CURSOR_INFO info)
{
    if (!check_output(console))
        return FALSE;
    info->dwSize = 25;
    info->bVisible = TRUE;
    return TRUE;
}

BOOL WINAPI SetConsoleCursorInfo(HANDLE console, const CONSOLE_CURSOR_INFO *info)
{
    return check_output(console);
}

BOOL WINAPI SetConsoleWindowInfo(HANDLE console, BOOL absolute, const SMALL_RECT *window)
{
    return check_output(console);
}

COORD WINAPI GetLargestConsoleWindowSize(HANDLE console)
{
    MIME_CONDRV_STATE st;
    COORD c = {80, 25};
    if (query_state(&st)) {
        c.X = (SHORT)st.Columns;
        c.Y = (SHORT)st.Rows;
    }
    return c;
}

BOOL WINAPI FillConsoleOutputCharacterW(HANDLE console, WCHAR c, DWORD length, COORD start, LPDWORD written)
{
    if (!check_output(console))
        return FALSE;
    if (written)
        *written = length;
    return TRUE;
}

BOOL WINAPI FillConsoleOutputCharacterA(HANDLE console, CHAR c, DWORD length, COORD start, LPDWORD written)
{
    return FillConsoleOutputCharacterW(console, (UCHAR)c, length, start, written);
}

BOOL WINAPI FillConsoleOutputAttribute(HANDLE console, WORD attribute, DWORD length, COORD start, LPDWORD written)
{
    return FillConsoleOutputCharacterW(console, 0, length, start, written);
}

BOOL WINAPI WriteConsoleOutputCharacterW(HANDLE console, LPCWSTR text, DWORD length, COORD start, LPDWORD written)
{
    return FillConsoleOutputCharacterW(console, 0, length, start, written);
}

BOOL WINAPI SetConsoleCtrlHandler(PHANDLER_ROUTINE handler, BOOL add)
{
    RtlEnterCriticalSection(&g_console_lock);
    BOOL ok = TRUE;
    if (!handler) {
        g_ignore_ctrl_c = add;
    } else if (add) {
        if (g_ctrl_handler_count < RTL_NUMBER_OF(g_ctrl_handlers)) {
            g_ctrl_handlers[g_ctrl_handler_count++] = handler;
        } else {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            ok = FALSE;
        }
    } else {
        ok = FALSE;
        for (ULONG i = g_ctrl_handler_count; i-- > 0;) {
            if (g_ctrl_handlers[i] == handler) {
                memmove(&g_ctrl_handlers[i], &g_ctrl_handlers[i + 1],
                        (g_ctrl_handler_count - i - 1) * sizeof(g_ctrl_handlers[0]));
                g_ctrl_handler_count--;
                ok = TRUE;
                break;
            }
        }
        if (!ok)
            KbSetError(ERROR_INVALID_PARAMETER);
    }
    RtlLeaveCriticalSection(&g_console_lock);
    return ok;
}

/* the event reaches every process of the console, or of one process group, on threads of their own. */
BOOL WINAPI GenerateConsoleCtrlEvent(DWORD event, DWORD group)
{
    if (event > CTRL_BREAK_EVENT) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG in[2] = {event, group};
    NTSTATUS status = condrv(IOCTL_CONDRV_GENERATE, in, sizeof(in), NULL, 0, NULL);
    return NT_SUCCESS(status) || KbSetStatus(status);
}

BOOL WINAPI GetCurrentConsoleFont(HANDLE console, BOOL maximum, PCONSOLE_FONT_INFO info)
{
    if (!check_output(console))
        return FALSE;
    info->nFont = 0;
    info->dwFontSize.X = 8;
    info->dwFontSize.Y = 16;
    return TRUE;
}

BOOL WINAPI GetCurrentConsoleFontEx(HANDLE console, BOOL maximum, PCONSOLE_FONT_INFOEX info)
{
    if (!check_output(console))
        return FALSE;
    if (!info || info->cbSize != sizeof(*info)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    info->nFont = 0;
    info->dwFontSize.X = 8;
    info->dwFontSize.Y = 16;
    info->FontFamily = 0x36;
    info->FontWeight = 400;
    wcscpy(info->FaceName, L"Consolas");
    return TRUE;
}

DWORD WINAPI GetConsoleProcessList(LPDWORD list, DWORD count)
{
    if (!list || !count) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG ids[1 + 64];
    if (!NT_SUCCESS(condrv(IOCTL_CONDRV_PROCESSES, NULL, 0, ids, sizeof(ids), NULL))) {
        KbSetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    ULONG total = ids[0];
    /* a list too small for every process is left alone, and the count says how many there are. */
    if (total <= count)
        memcpy(list, ids + 1, (total < 64 ? total : 64) * sizeof(ULONG));
    return total;
}

HANDLE WINAPI CreateConsoleScreenBuffer(DWORD access, DWORD share, const SECURITY_ATTRIBUTES *sa, DWORD flags,
                                        LPVOID data)
{
    return KB_CONSOLE_OUTPUT;
}

BOOL WINAPI SetConsoleActiveScreenBuffer(HANDLE console)
{
    return check_output(console);
}
