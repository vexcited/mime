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

/* the driver's input event follows its input buffer. */
static void sync_input_event(void)
{
    condrv(IOCTL_CONDRV_SYNC_EVENT, NULL, 0, NULL, 0, NULL);
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

/* ------------------------------------------------------------------ input records */

/*
 * the console keeps the input records, as conhost's input buffer does:
 * the keys typed, decoded from what the terminal sends, and the events
 * programs write. flags are CONSOLE_READ_NOREMOVE and CONSOLE_READ_NOWAIT.
 */
static BOOL read_input_records(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read, USHORT flags)
{
    *read = 0;
    if (!check_input(console))
        return FALSE;
    UCHAR out[sizeof(ULONG) + MAX_RECORDS * sizeof(INPUT_RECORD)];
    ULONG room = count < MAX_RECORDS ? count : MAX_RECORDS;
    ULONG in = flags & (MIME_CONDRV_READ_PEEK | MIME_CONDRV_READ_NOWAIT);
    ULONG_PTR done;
    NTSTATUS status =
        condrv(IOCTL_CONDRV_READ_INPUT, &in, sizeof(in), out, sizeof(ULONG) + room * sizeof(INPUT_RECORD), &done);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    ULONG n = done > sizeof(ULONG) ? (ULONG)(done - sizeof(ULONG)) / sizeof(INPUT_RECORD) : 0;
    memcpy(buffer, out + sizeof(ULONG), n * sizeof(INPUT_RECORD));
    *read = n;
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

BOOL WINAPI ReadConsoleInputExW(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read, USHORT flags)
{
    return read_input_records(console, buffer, count, read, flags);
}

BOOL WINAPI ReadConsoleInputExA(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read, USHORT flags)
{
    if (!read_input_records(console, buffer, count, read, flags))
        return FALSE;
    records_to_ansi(buffer, *read);
    return TRUE;
}

BOOL WINAPI ReadConsoleInputW(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    return ReadConsoleInputExW(console, buffer, count, read, 0);
}

BOOL WINAPI ReadConsoleInputA(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    return ReadConsoleInputExA(console, buffer, count, read, 0);
}

BOOL WINAPI PeekConsoleInputW(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    return ReadConsoleInputExW(console, buffer, count, read, CONSOLE_READ_NOREMOVE | CONSOLE_READ_NOWAIT);
}

BOOL WINAPI PeekConsoleInputA(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    return ReadConsoleInputExA(console, buffer, count, read, CONSOLE_READ_NOREMOVE | CONSOLE_READ_NOWAIT);
}

BOOL WINAPI GetNumberOfConsoleInputEvents(HANDLE console, LPDWORD count)
{
    if (!check_input(console))
        return FALSE;
    ULONG in = MIME_CONDRV_READ_PEEK | MIME_CONDRV_READ_NOWAIT, total = 0;
    NTSTATUS status = condrv(IOCTL_CONDRV_READ_INPUT, &in, sizeof(in), &total, sizeof(total), NULL);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *count = total;
    return TRUE;
}

/*
 * the console's write: appended, or ahead of the events waiting. the
 * events it took land in *written even when it fails, as on windows.
 */
static BOOL write_input(HANDLE console, const INPUT_RECORD *records, DWORD count, LPDWORD written, BOOL append)
{
    *written = 0;
    if (!check_input(console))
        return FALSE;
    if (count > (0xFFFFFFFFu - sizeof(MIME_CONDRV_WRITE_INPUT)) / sizeof(INPUT_RECORD)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG size = sizeof(MIME_CONDRV_WRITE_INPUT) + count * sizeof(INPUT_RECORD);
    MIME_CONDRV_WRITE_INPUT *in = KbAlloc(size);
    if (!in) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    in->Flags = append ? MIME_CONDRV_WRITE_APPEND : 0;
    in->Reserved = 0;
    memcpy(in + 1, records, count * sizeof(INPUT_RECORD));
    ULONG taken = 0;
    NTSTATUS status = condrv(IOCTL_CONDRV_WRITE_INPUT, in, size, &taken, sizeof(taken), NULL);
    KbFree(in);
    *written = taken;
    return NT_SUCCESS(status) || KbSetStatus(status);
}

static BOOL known_event(WORD type)
{
    return type == KEY_EVENT || type == MOUSE_EVENT || type == WINDOW_BUFFER_SIZE_EVENT || type == MENU_EVENT ||
           type == FOCUS_EVENT;
}

/*
 * ansi events carry a byte of the input code page. as the console does,
 * each key event becomes one per utf-16 unit of its character, a lead byte
 * takes the next event's byte as its trail, a lead byte left at the end
 * waits in the console for the next write, and a byte that converts to
 * nothing drops its event.
 */
static BOOL write_input_ansi(HANDLE console, const INPUT_RECORD *records, DWORD count, LPDWORD written, BOOL append)
{
    *written = 0;
    if (!check_input(console))
        return FALSE;
    for (DWORD i = 0; i < count; i++) {
        if (!known_event(records[i].EventType)) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }
    if (count > 0xFFFFFFFFu / 2 / sizeof(INPUT_RECORD) - 2) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PINPUT_RECORD wide = KbAlloc((count + 1) * 2 * sizeof(INPUT_RECORD));
    if (!wide) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    UINT cp = g_input_cp;
    INPUT_RECORD carry;
    ULONG_PTR had = 0;
    condrv(IOCTL_CONDRV_ANSI_CARRY, NULL, 0, &carry, sizeof(carry), &had);
    DWORD n = 0, i = 0, total = count + (had ? 1 : 0);
    BOOL keep = FALSE;
    while (i < total) {
        const INPUT_RECORD *r = had ? (i ? &records[i - 1] : &carry) : &records[i];
        i++;
        if (r->EventType != KEY_EVENT) {
            wide[n++] = *r;
            continue;
        }
        CHAR bytes[2] = {r->Event.KeyEvent.uChar.AsciiChar, 0};
        int length = 1;
        if (IsDBCSLeadByteEx(cp, (BYTE)bytes[0])) {
            if (i == total) {
                carry = *r;
                keep = TRUE;
                break;
            }
            const INPUT_RECORD *trail = had ? &records[i - 1] : &records[i];
            i++;
            bytes[1] = trail->Event.KeyEvent.uChar.AsciiChar;
            length = 2;
        }
        WCHAR units[2];
        int made = MultiByteToWideChar(cp, 0, bytes, length, units, 2);
        for (int u = 0; u < made; u++) {
            wide[n] = *r;
            wide[n++].Event.KeyEvent.uChar.UnicodeChar = units[u];
        }
    }
    if (keep)
        condrv(IOCTL_CONDRV_ANSI_CARRY, &carry, sizeof(carry), NULL, 0, NULL);
    BOOL ok = write_input(console, wide, n, written, append);
    KbFree(wide);
    return ok;
}

BOOL WINAPI WriteConsoleInputW(HANDLE console, const INPUT_RECORD *buffer, DWORD count, LPDWORD written)
{
    return write_input(console, buffer, count, written, TRUE);
}

BOOL WINAPI WriteConsoleInputA(HANDLE console, const INPUT_RECORD *buffer, DWORD count, LPDWORD written)
{
    return write_input_ansi(console, buffer, count, written, TRUE);
}

/* what ntvdm wrote: the events go ahead of the ones waiting. */
BOOL WINAPI WriteConsoleInputVDMW(HANDLE console, const INPUT_RECORD *buffer, DWORD count, LPDWORD written)
{
    return write_input(console, buffer, count, written, FALSE);
}

BOOL WINAPI WriteConsoleInputVDMA(HANDLE console, const INPUT_RECORD *buffer, DWORD count, LPDWORD written)
{
    return write_input_ansi(console, buffer, count, written, FALSE);
}

BOOL WINAPI FlushConsoleInputBuffer(HANDLE console)
{
    if (!check_input(console))
        return FALSE;
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
