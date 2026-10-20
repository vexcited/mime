/*
 * console: the standard handles of a console program are files of the
 * console driver (\Device\ConDrv, mime/condrv.h): \CurrentIn for the input
 * and \CurrentOut for the screen buffer that is shown. kernelbase asks them
 * its questions with device controls and reads and writes them as files. a
 * program `mime run` started shares that terminal, and ctrl+c, ctrl+break
 * and closing it are ctrl events that the driver delivers by starting
 * ctrl_routine on a new thread. a gui program has no console until
 * AttachConsole or AllocConsole.
 */
#include "kernelbase_private.h"
#include <mime/condrv.h>
#include <mime/win32/winuser.h>

#define MAX_TRANSFER_CELLS 0x4000
#define MAX_WRITE_CHARS 0x8000

/* a value win32k never hands out: user handles start at index 0x20. */
#define CONSOLE_WINDOW ((HWND)(ULONG_PTR)0x00FF0010)

static RTL_CRITICAL_SECTION g_console_lock = RTL_CRITICAL_SECTION_INIT;
static HANDLE g_condrv; /* \Device\ConDrv, for what is about the process and its console */
static PHANDLER_ROUTINE g_ctrl_handlers[32];
static ULONG g_ctrl_handler_count;
static WCHAR g_exe_name[256]; /* the name the history and aliases of the process go by */

/* ------------------------------------------------------------------ the driver */

static NTSTATUS control(HANDLE handle, ULONG code, const void *in, ULONG in_len, void *out, ULONG out_len, ULONG_PTR *done)
{
    if (done)
        *done = 0;
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return STATUS_INVALID_HANDLE;
    IO_STATUS_BLOCK io = {};
    NTSTATUS status = NtDeviceIoControlFile(handle, nullptr, nullptr, nullptr, &io, code, (PVOID)in, in_len, out, out_len);
    if (done && NT_SUCCESS(status))
        *done = io.Information;
    return status;
}

/* what the driver, or another driver that was asked by mistake, answered, as the error of the call. */
static BOOL fail(NTSTATUS status)
{
    switch (status) {
    case STATUS_INVALID_HANDLE:
    case STATUS_INVALID_DEVICE_REQUEST:
    case STATUS_OBJECT_TYPE_MISMATCH:
    case STATUS_NOT_SUPPORTED:
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    default:
        return KbSetStatus(status);
    }
}

/* a request about the console of the process; fails when there is none. */
static NTSTATUS process_control(ULONG code, const void *in, ULONG in_len, void *out, ULONG out_len, ULONG_PTR *done)
{
    return control(g_condrv, code, in, in_len, out, out_len, done);
}

static BOOL query_state(MIME_CONDRV_STATE *st)
{
    memset(st, 0, sizeof(*st));
    return NT_SUCCESS(process_control(IOCTL_CONDRV_STATE, nullptr, 0, st, sizeof(*st), nullptr)) &&
           (st->Flags & MIME_CONDRV_ATTACHED);
}

static BOOL has_console(void)
{
    MIME_CONDRV_STATE st;
    return query_state(&st);
}

static UINT code_page(BOOL input)
{
    MIME_CONDRV_CODE_PAGES cp = {};
    if (!NT_SUCCESS(process_control(IOCTL_CONDRV_CODE_PAGES, &cp, sizeof(cp), &cp, sizeof(cp), nullptr)))
        return CP_UTF8;
    return input ? cp.Input : cp.Output;
}

/* ------------------------------------------------------------------ text output */

/* debug output through NtDisplayString, chunked for its limits. */
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

/* text for a screen buffer: utf-16, or bytes of the output code page. */
static BOOL write_text(HANDLE console, BOOL bytes, const void *data, DWORD length)
{
    DWORD unit = bytes ? 1 : sizeof(WCHAR);
    const UCHAR *p = data;
    do {
        DWORD n = length > MAX_WRITE_CHARS ? MAX_WRITE_CHARS : length;
        /* a pair of utf-16 units is not cut. */
        if (!bytes && n < length && n && ((const WCHAR *)p)[n - 1] >= 0xD800 && ((const WCHAR *)p)[n - 1] <= 0xDBFF)
            n--;
        ULONG size = sizeof(MIME_CONDRV_WRITE) + n * unit;
        MIME_CONDRV_WRITE *rq = KbAlloc(size);
        if (!rq) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        rq->Flags = bytes ? MIME_CONDRV_WRITE_BYTES : 0;
        rq->Length = n;
        memcpy(rq + 1, p, n * unit);
        NTSTATUS status = control(console, IOCTL_CONDRV_OUT_WRITE, rq, size, nullptr, 0, nullptr);
        KbFree(rq);
        if (!NT_SUCCESS(status))
            return fail(status);
        p += n * unit;
        length -= n;
    } while (length);
    return TRUE;
}

BOOL WINAPI WriteConsoleW(HANDLE console, const void *buffer, DWORD count, LPDWORD written, LPVOID reserved)
{
    if (written)
        *written = 0;
    if (!buffer && count) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    if (!write_text(console, FALSE, buffer, count))
        return FALSE;
    if (written)
        *written = count;
    return TRUE;
}

BOOL WINAPI WriteConsoleA(HANDLE console, const void *buffer, DWORD count, LPDWORD written, LPVOID reserved)
{
    if (written)
        *written = 0;
    if (!buffer && count) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    if (!write_text(console, TRUE, buffer, count))
        return FALSE;
    if (written)
        *written = count;
    return TRUE;
}

/* ------------------------------------------------------------------ text input */

static PCWSTR exe_name(ULONG *length)
{
    if (!g_exe_name[0]) {
        PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
        PCWSTR path = pp->ImagePathName.Buffer;
        SIZE_T n = pp->ImagePathName.Length / sizeof(WCHAR), start = n;
        while (start > 0 && path[start - 1] != '\\' && path[start - 1] != '/')
            start--;
        n -= start;
        if (n >= RTL_NUMBER_OF(g_exe_name))
            n = RTL_NUMBER_OF(g_exe_name) - 1;
        memcpy(g_exe_name, path + start, n * sizeof(WCHAR));
        g_exe_name[n] = 0;
    }
    *length = (ULONG)wcslen(g_exe_name);
    return g_exe_name;
}

/* the text a read gives back, as characters (wide) or bytes of the input code page. */
static BOOL read_text(HANDLE console, BOOL wide, PVOID buffer, DWORD count, LPDWORD read, PCONSOLE_READCONSOLE_CONTROL control_in)
{
    ULONG exe_length;
    PCWSTR exe = exe_name(&exe_length);
    ULONG initial = 0;
    if (control_in && control_in->nLength == sizeof(*control_in) && control_in->nInitialChars) {
        initial = control_in->nInitialChars;
        if (initial > count) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }
    ULONG unit = wide ? sizeof(WCHAR) : 1;
    ULONG in_size = sizeof(MIME_CONDRV_READ) + (initial + exe_length) * sizeof(WCHAR);
    ULONG out_size = sizeof(MIME_CONDRV_READ_RESULT) + count * unit;
    ULONG size = in_size > out_size ? in_size : out_size;
    PUCHAR block = KbAlloc(size);
    if (!block) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    MIME_CONDRV_READ *rq = (MIME_CONDRV_READ *)block;
    rq->Flags = wide ? MIME_CONDRV_READ_WIDE : 0;
    rq->Length = count;
    rq->InitialChars = initial;
    rq->Control = control_in && control_in->nLength == sizeof(*control_in);
    rq->WakeupMask = rq->Control ? control_in->dwCtrlWakeupMask : 0;
    rq->ExeNameLength = exe_length;
    PWCHAR text = (PWCHAR)(rq + 1);
    if (initial) {
        /* the characters of the buffer that start the line are wide, or bytes of the input code page. */
        if (wide) {
            memcpy(text, buffer, initial * sizeof(WCHAR));
        } else {
            MultiByteToWideChar(code_page(TRUE), 0, buffer, (int)initial, text, (int)initial);
        }
    }
    memcpy(text + initial, exe, exe_length * sizeof(WCHAR));
    ULONG_PTR done;
    NTSTATUS status = control(console, IOCTL_CONDRV_IN_READ, block, size, block, size, &done);
    BOOL ok = NT_SUCCESS(status);
    if (ok) {
        MIME_CONDRV_READ_RESULT *res = (MIME_CONDRV_READ_RESULT *)block;
        ULONG got = done > sizeof(*res) ? (ULONG)(done - sizeof(*res)) / unit : 0;
        memcpy(buffer, res + 1, got * unit);
        if (read)
            *read = got;
        if (control_in && control_in->nLength == sizeof(*control_in))
            control_in->dwControlKeyState = res->ControlKeyState;
    } else {
        fail(status);
    }
    KbFree(block);
    return ok;
}

/* reads characters, waiting for at least one; none at end of file. */
BOOL WINAPI ReadConsoleW(HANDLE console, LPVOID buffer, DWORD count, LPDWORD read, PCONSOLE_READCONSOLE_CONTROL control_in)
{
    if (read)
        *read = 0;
    if (!buffer && count) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    return read_text(console, TRUE, buffer, count, read, control_in);
}

/* bytes in the input code page. */
BOOL WINAPI ReadConsoleA(HANDLE console, LPVOID buffer, DWORD count, LPDWORD read, PCONSOLE_READCONSOLE_CONTROL control_in)
{
    if (read)
        *read = 0;
    if (!buffer && count) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    return read_text(console, FALSE, buffer, count, read, control_in);
}

/* ------------------------------------------------------------------ input records */

#define MAX_RECORDS 256

/*
 * the console keeps the input records, as conhost's input buffer does:
 * the keys typed, decoded from what the terminal sends, and the events
 * programs write. flags are CONSOLE_READ_NOREMOVE and CONSOLE_READ_NOWAIT.
 */
static BOOL read_input_records(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read, USHORT flags)
{
    *read = 0;
    DWORD total = 0;
    while (total < count) {
        UCHAR out[sizeof(ULONG) + MAX_RECORDS * sizeof(INPUT_RECORD)];
        ULONG room = count - total < MAX_RECORDS ? count - total : MAX_RECORDS;
        /* the first batch waits, the next ones take what is there. */
        ULONG in = (flags & (MIME_CONDRV_READ_PEEK | MIME_CONDRV_READ_NOWAIT)) | (total ? MIME_CONDRV_READ_NOWAIT : 0);
        ULONG_PTR done;
        NTSTATUS status = control(console, IOCTL_CONDRV_IN_RECORDS, &in, sizeof(in), out,
                                  sizeof(ULONG) + room * sizeof(INPUT_RECORD), &done);
        if (!NT_SUCCESS(status))
            return total ? TRUE : fail(status);
        ULONG n = done > sizeof(ULONG) ? (ULONG)(done - sizeof(ULONG)) / sizeof(INPUT_RECORD) : 0;
        memcpy(buffer + total, out + sizeof(ULONG), n * sizeof(INPUT_RECORD));
        total += n;
        /* a peek sees the same records again, so it is one batch. */
        if (!n || (flags & CONSOLE_READ_NOREMOVE) || n < room)
            break;
    }
    *read = total;
    return TRUE;
}

/* ansi records carry the character in the input code page. */
static void records_to_ansi(PINPUT_RECORD records, DWORD count)
{
    UINT cp = code_page(TRUE);
    for (DWORD i = 0; i < count; i++) {
        if (records[i].EventType != KEY_EVENT)
            continue;
        WCHAR w = records[i].Event.KeyEvent.uChar.UnicodeChar;
        CHAR a = 0;
        if (w && WideCharToMultiByte(cp == CP_UTF8 ? CP_ACP : cp, 0, &w, 1, &a, 1, nullptr, nullptr) != 1)
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
    ULONG in = MIME_CONDRV_READ_PEEK | MIME_CONDRV_READ_NOWAIT, total = 0;
    NTSTATUS status = control(console, IOCTL_CONDRV_IN_RECORDS, &in, sizeof(in), &total, sizeof(total), nullptr);
    if (!NT_SUCCESS(status))
        return fail(status);
    if (count)
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
    NTSTATUS status = control(console, IOCTL_CONDRV_IN_WRITE, in, size, &taken, sizeof(taken), nullptr);
    KbFree(in);
    *written = taken;
    return NT_SUCCESS(status) || fail(status);
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
    UINT cp = code_page(TRUE);
    INPUT_RECORD carry;
    ULONG_PTR had = 0;
    control(console, IOCTL_CONDRV_IN_ANSI_CARRY, nullptr, 0, &carry, sizeof(carry), &had);
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
        control(console, IOCTL_CONDRV_IN_ANSI_CARRY, &carry, sizeof(carry), nullptr, 0, nullptr);
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
    NTSTATUS status = control(console, IOCTL_CONDRV_IN_FLUSH, nullptr, 0, nullptr, 0, nullptr);
    return NT_SUCCESS(status) || fail(status);
}

/* ------------------------------------------------------------------ the modes */

BOOL WINAPI GetConsoleMode(HANDLE console, LPDWORD mode)
{
    MIME_CONDRV_MODE m = {};
    NTSTATUS status = control(console, IOCTL_CONDRV_IN_MODE, &m, sizeof(m), &m, sizeof(m), nullptr);
    if (!NT_SUCCESS(status))
        return fail(status);
    if (!mode) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    *mode = m.Mode;
    return TRUE;
}

BOOL WINAPI SetConsoleMode(HANDLE console, DWORD mode)
{
    MIME_CONDRV_MODE m = {1, mode};
    NTSTATUS status = control(console, IOCTL_CONDRV_IN_MODE, &m, sizeof(m), &m, sizeof(m), nullptr);
    return NT_SUCCESS(status) || fail(status);
}

/* ------------------------------------------------------------------ code pages */

UINT WINAPI GetConsoleCP(void)
{
    MIME_CONDRV_CODE_PAGES cp = {};
    if (!NT_SUCCESS(process_control(IOCTL_CONDRV_CODE_PAGES, &cp, sizeof(cp), &cp, sizeof(cp), nullptr))) {
        KbSetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return cp.Input;
}

UINT WINAPI GetConsoleOutputCP(void)
{
    MIME_CONDRV_CODE_PAGES cp = {};
    if (!NT_SUCCESS(process_control(IOCTL_CONDRV_CODE_PAGES, &cp, sizeof(cp), &cp, sizeof(cp), nullptr))) {
        KbSetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return cp.Output;
}

static BOOL set_code_page(ULONG which, UINT cp)
{
    if (!IsValidCodePage(cp)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MIME_CONDRV_CODE_PAGES rq = {which, cp, cp};
    NTSTATUS status = process_control(IOCTL_CONDRV_CODE_PAGES, &rq, sizeof(rq), &rq, sizeof(rq), nullptr);
    if (!NT_SUCCESS(status)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI SetConsoleCP(UINT cp)
{
    return set_code_page(1, cp);
}

BOOL WINAPI SetConsoleOutputCP(UINT cp)
{
    return set_code_page(2, cp);
}

/* ------------------------------------------------------------------ standard handles */

static HANDLE open_driver_file(PCWSTR name, ACCESS_MASK access, ULONG share, ULONG attributes, NTSTATUS *status_out)
{
    WCHAR path[64];
    UNICODE_STRING us;
    wcscpy(path, MIME_CONDRV_DEVICE L"\\");
    wcscat(path, name);
    RtlInitUnicodeString(&us, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, attributes, nullptr, nullptr);
    IO_STATUS_BLOCK io;
    HANDLE h = nullptr;
    NTSTATUS status = NtCreateFile(&h, access | SYNCHRONIZE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, &oa, &io, nullptr, 0,
                                   share, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    if (status_out)
        *status_out = status;
    return NT_SUCCESS(status) ? h : nullptr;
}

/*
 * a stream the terminal has is a file of the console; a stream `mime run`
 * was given a pipe or a file for is a handle to its end of the console's
 * stream device, which programs see as a pipe, as on windows.
 */
static HANDLE standard_handle(ULONG tty_flags, ULONG tty, PCWSTR console_name, PCWSTR stream, ACCESS_MASK access)
{
    if (tty_flags & tty) {
        HANDLE h = open_driver_file(console_name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, OBJ_INHERIT, nullptr);
        if (h)
            return h;
    }
    WCHAR path[64];
    UNICODE_STRING name;
    wcscpy(path, MIME_CONDRV_STREAM_DEVICE L"\\");
    wcscat(path, stream);
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_INHERIT, nullptr, nullptr);
    IO_STATUS_BLOCK io;
    HANDLE h;
    if (!NT_SUCCESS(NtCreateFile(&h, access | SYNCHRONIZE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, &oa, &io, nullptr, 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0)))
        return nullptr;
    return h;
}

static void set_console_handles(BOOL only_missing)
{
    MIME_CONDRV_STATE st;
    if (!query_state(&st))
        return;
    PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
    if (!only_missing || !pp->StandardInput)
        pp->StandardInput = standard_handle(st.Flags, MIME_CONDRV_TTY_INPUT, MIME_CONDRV_CURRENT_IN, L"Input", GENERIC_READ);
    if (!only_missing || !pp->StandardOutput)
        pp->StandardOutput = standard_handle(st.Flags, MIME_CONDRV_TTY_OUTPUT, MIME_CONDRV_CURRENT_OUT, L"Output", GENERIC_WRITE);
    if (!only_missing || !pp->StandardError)
        pp->StandardError = standard_handle(st.Flags, MIME_CONDRV_TTY_ERROR, MIME_CONDRV_CURRENT_OUT, L"Error", GENERIC_WRITE);
}

/*
 * the console driver starts this on a new thread for each ctrl event, as
 * csrss starts kernel32's CtrlRoutine. handlers run newest first; when none
 * handles the event, and always after a close, the process exits. a handler
 * that took a logoff or a shutdown ends the thread with the event as its
 * exit code, and the system ends the process once the handler is back, as
 * csrss does: that is how it knows the handler took it.
 */
static DWORD WINAPI ctrl_routine(LPVOID parameter)
{
    DWORD event = (DWORD)(ULONG_PTR)parameter;
    if (event == CTRL_C_EVENT && (KbParams()->ConsoleFlags & 1))
        return 0;
    PHANDLER_ROUTINE handlers[RTL_NUMBER_OF(g_ctrl_handlers)];
    RtlEnterCriticalSection(&g_console_lock);
    ULONG count = g_ctrl_handler_count;
    memcpy(handlers, g_ctrl_handlers, count * sizeof(handlers[0]));
    RtlLeaveCriticalSection(&g_console_lock);
    BOOL handled = FALSE;
    for (ULONG i = count; i-- > 0 && !handled;)
        handled = handlers[i](event);
    if (handled && (event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT))
        return event;
    if (!handled || event >= CTRL_CLOSE_EVENT)
        ExitProcess(STATUS_CONTROL_C_EXIT);
    return 0;
}

/* a handle of the console driver, whatever the handle of a pipe or a file is. */
static BOOL is_console_device(HANDLE h)
{
    if (!h || h == INVALID_HANDLE_VALUE || KbIsPseudoHandle(h))
        return FALSE;
    FILE_FS_DEVICE_INFORMATION info;
    IO_STATUS_BLOCK io;
    return NT_SUCCESS(NtQueryVolumeInformationFile(h, &io, &info, sizeof(info), FileFsDeviceInformation)) &&
           info.DeviceType == 0x50 /* FILE_DEVICE_CONSOLE */;
}

/* a program that is no console program has no use for the console handles it was given. */
static void close_console_handle(PHANDLE slot)
{
    if (is_console_device(*slot)) {
        NtClose(*slot);
        *slot = nullptr;
    }
}

void KbInitializeConsole(void)
{
    UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_CONDRV_DEVICE);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, 0, nullptr, nullptr);
    IO_STATUS_BLOCK io;
    if (!NT_SUCCESS(NtCreateFile(&g_condrv, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io, nullptr, 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0)))
        g_condrv = nullptr;
    MIME_CONDRV_REGISTER r = {(ULONG64)(ULONG_PTR)ctrl_routine, 0};
    process_control(IOCTL_CONDRV_REGISTER, &r, sizeof(r), nullptr, 0, nullptr);
    if (KbPeb()->ImageSubsystem != IMAGE_SUBSYSTEM_WINDOWS_CUI) {
        PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
        close_console_handle(&pp->StandardInput);
        close_console_handle(&pp->StandardOutput);
        close_console_handle(&pp->StandardError);
        return;
    }
    set_console_handles(TRUE);
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
    return SetStdHandleEx(which, handle, nullptr);
}

/*
 * CONIN$, CONOUT$ and CON are names of the console of the process: opens the file of the
 * driver, or returns nullptr when the name is none of them.
 */
HANDLE KbOpenConsoleDevice(LPCWSTR name, DWORD access, DWORD share, const SECURITY_ATTRIBUTES *sa, DWORD disposition)
{
    PCWSTR base = name;
    if (!wcsncmp(base, L"\\\\.\\", 4))
        base += 4;
    PCWSTR driver_name;
    if (!_wcsicmp(base, L"CONIN$"))
        driver_name = MIME_CONDRV_CURRENT_IN;
    else if (!_wcsicmp(base, L"CONOUT$"))
        driver_name = MIME_CONDRV_CURRENT_OUT;
    else if (!_wcsicmp(base, L"CON"))
        driver_name = (access & GENERIC_WRITE) ? MIME_CONDRV_CURRENT_OUT : MIME_CONDRV_CURRENT_IN;
    else
        return nullptr;
    (void)disposition;
    NTSTATUS status;
    HANDLE h = open_driver_file(driver_name, access ? access : GENERIC_READ | GENERIC_WRITE,
                                share ? share : FILE_SHARE_READ | FILE_SHARE_WRITE,
                                sa && sa->bInheritHandle ? OBJ_INHERIT : 0, &status);
    if (!h) {
        KbSetStatus(status == STATUS_OBJECT_NAME_NOT_FOUND ? STATUS_INVALID_HANDLE : status);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

/* ------------------------------------------------------------------ console management */

/*
 * windows makes a new console window; mime has no console host window, so a process that
 * descends from a run takes the terminal of that run while it is open, and any other gets a
 * console that nothing shows.
 */
BOOL WINAPI AllocConsole(void)
{
    MIME_CONDRV_ALLOC rq = {MIME_CONDRV_ALLOC_RUN_TERMINAL, 0};
    NTSTATUS status = process_control(IOCTL_CONDRV_ALLOC, &rq, sizeof(rq), nullptr, 0, nullptr);
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_ACCESS_DENIED ? ERROR_ACCESS_DENIED : ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    set_console_handles(FALSE);
    return TRUE;
}

BOOL WINAPI FreeConsole(void)
{
    NTSTATUS status = process_control(IOCTL_CONDRV_FREE, nullptr, 0, nullptr, 0, nullptr);
    return NT_SUCCESS(status) || fail(status);
}

BOOL WINAPI AttachConsole(DWORD process)
{
    ULONG pid = process;
    NTSTATUS status = process_control(IOCTL_CONDRV_ATTACH, &pid, sizeof(pid), nullptr, 0, nullptr);
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_ACCESS_DENIED    ? ERROR_ACCESS_DENIED
                   : status == STATUS_INVALID_HANDLE ? ERROR_INVALID_HANDLE
                                                     : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    set_console_handles(TRUE);
    return TRUE;
}

/* there is no console window, but programs such as qt's logging only ask whether one exists. */
HWND WINAPI GetConsoleWindow(void)
{
    return has_console() ? CONSOLE_WINDOW : nullptr;
}

/* a console window is never full screen; without a console the server call fails. */
DECLSPEC_EXPORT BOOL WINAPI GetConsoleDisplayMode(LPDWORD flags)
{
    if (!has_console()) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    *flags = 0;
    return TRUE;
}

DWORD WINAPI GetConsoleProcessList(LPDWORD list, DWORD count)
{
    if (!list || !count) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG ids[1 + 64];
    if (!NT_SUCCESS(process_control(IOCTL_CONDRV_PROCESSES, nullptr, 0, ids, sizeof(ids), nullptr))) {
        KbSetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    ULONG total = ids[0];
    /* a list too small for every process is left alone, and the count says how many there are. */
    if (total <= count)
        memcpy(list, ids + 1, (total < 64 ? total : 64) * sizeof(ULONG));
    return total;
}

/* ------------------------------------------------------------------ ctrl events */

BOOL WINAPI SetConsoleCtrlHandler(PHANDLER_ROUTINE handler, BOOL add)
{
    RtlEnterCriticalSection(&g_console_lock);
    BOOL ok = TRUE;
    if (!handler) {
        PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
        if (add)
            pp->ConsoleFlags |= 1;
        else
            pp->ConsoleFlags &= ~1u;
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
    NTSTATUS status = process_control(IOCTL_CONDRV_GENERATE, in, sizeof(in), nullptr, 0, nullptr);
    return NT_SUCCESS(status) || fail(status);
}

/* ------------------------------------------------------------------ title */

static BOOL set_title(LPCWSTR title)
{
    SIZE_T n = wcslen(title);
    if (n > 0xFFFF)
        n = 0xFFFF;
    ULONG size = (ULONG)(sizeof(MIME_CONDRV_TITLE) + n * sizeof(WCHAR));
    MIME_CONDRV_TITLE *rq = KbAlloc(size);
    if (!rq) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    rq->Set = 1;
    rq->Original = 0;
    rq->Length = (ULONG)n;
    memcpy(rq + 1, title, n * sizeof(WCHAR));
    NTSTATUS status = process_control(IOCTL_CONDRV_TITLE, rq, size, nullptr, 0, nullptr);
    KbFree(rq);
    return NT_SUCCESS(status) || fail(status);
}

BOOL WINAPI SetConsoleTitleW(LPCWSTR title)
{
    if (!title) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return set_title(title);
}

BOOL WINAPI SetConsoleTitleA(LPCSTR title)
{
    PWSTR w = KbAnsiToWideCp(code_page(TRUE) == CP_UTF8 ? CP_UTF8 : code_page(TRUE), title);
    if (!w) {
        if (!title)
            KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = set_title(w);
    KbFree(w);
    return ok;
}

static DWORD get_title(LPWSTR title, DWORD size, BOOL original)
{
    if (!title || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UCHAR block[sizeof(ULONG) + 520];
    MIME_CONDRV_TITLE rq = {0, original, 0};
    memcpy(block, &rq, sizeof(rq));
    ULONG_PTR done;
    NTSTATUS status = process_control(IOCTL_CONDRV_TITLE, &rq, sizeof(rq), block, sizeof(block), &done);
    if (!NT_SUCCESS(status)) {
        fail(status);
        return 0;
    }
    ULONG length = *(PULONG)block;
    ULONG have = (ULONG)((done - sizeof(ULONG)) / sizeof(WCHAR));
    DWORD copy = have < size - 1 ? have : size - 1;
    memcpy(title, block + sizeof(ULONG), copy * sizeof(WCHAR));
    title[copy] = 0;
    return length;
}

DWORD WINAPI GetConsoleTitleW(LPWSTR title, DWORD size)
{
    return get_title(title, size, FALSE);
}

DWORD WINAPI GetConsoleOriginalTitleW(LPWSTR title, DWORD size)
{
    return get_title(title, size, TRUE);
}

static DWORD title_ansi(LPSTR title, DWORD size, BOOL original)
{
    if (!title || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WCHAR w[261];
    DWORD n = get_title(w, RTL_NUMBER_OF(w), original);
    if (!n && GetLastError() != ERROR_SUCCESS)
        return 0;
    int got = WideCharToMultiByte(code_page(FALSE), 0, w, -1, title, (int)size, nullptr, nullptr);
    if (!got)
        title[size - 1] = 0;
    return n;
}

DWORD WINAPI GetConsoleTitleA(LPSTR title, DWORD size)
{
    return title_ansi(title, size, FALSE);
}

DWORD WINAPI GetConsoleOriginalTitleA(LPSTR title, DWORD size)
{
    return title_ansi(title, size, TRUE);
}

/* ------------------------------------------------------------------ screen buffers */

static BOOL screen_info(HANDLE console, MIME_CONDRV_INFO *info)
{
    MIME_CONDRV_INFO_REQUEST rq = {0};
    UCHAR block[sizeof(MIME_CONDRV_INFO)];
    memcpy(block, &rq, sizeof(rq));
    NTSTATUS status = control(console, IOCTL_CONDRV_OUT_INFO, &rq, sizeof(rq), block, sizeof(block), nullptr);
    if (!NT_SUCCESS(status))
        return fail(status);
    memcpy(info, block, sizeof(*info));
    return TRUE;
}

BOOL WINAPI GetConsoleScreenBufferInfo(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFO info)
{
    MIME_CONDRV_INFO i;
    if (!screen_info(console, &i))
        return FALSE;
    info->dwSize.X = i.Size.X;
    info->dwSize.Y = i.Size.Y;
    info->dwCursorPosition.X = i.Cursor.X;
    info->dwCursorPosition.Y = i.Cursor.Y;
    info->wAttributes = i.Attributes;
    info->srWindow.Left = i.Window.Left;
    info->srWindow.Top = i.Window.Top;
    info->srWindow.Right = i.Window.Right;
    info->srWindow.Bottom = i.Window.Bottom;
    info->dwMaximumWindowSize.X = i.MaxWindow.X;
    info->dwMaximumWindowSize.Y = i.MaxWindow.Y;
    return TRUE;
}

BOOL WINAPI GetConsoleScreenBufferInfoEx(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFOEX info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MIME_CONDRV_INFO i;
    if (!screen_info(console, &i))
        return FALSE;
    info->dwSize.X = i.Size.X;
    info->dwSize.Y = i.Size.Y;
    info->dwCursorPosition.X = i.Cursor.X;
    info->dwCursorPosition.Y = i.Cursor.Y;
    info->wAttributes = i.Attributes;
    info->srWindow.Left = i.Window.Left;
    info->srWindow.Top = i.Window.Top;
    info->srWindow.Right = i.Window.Right;
    info->srWindow.Bottom = i.Window.Bottom;
    info->dwMaximumWindowSize.X = i.MaxWindow.X;
    info->dwMaximumWindowSize.Y = i.MaxWindow.Y;
    info->wPopupAttributes = i.PopupAttributes;
    info->bFullscreenSupported = FALSE;
    memcpy(info->ColorTable, i.ColorTable, sizeof(info->ColorTable));
    return TRUE;
}

BOOL WINAPI SetConsoleScreenBufferInfoEx(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFOEX info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UCHAR block[sizeof(MIME_CONDRV_INFO_REQUEST) + sizeof(MIME_CONDRV_INFO)] = {};
    MIME_CONDRV_INFO_REQUEST *rq = (MIME_CONDRV_INFO_REQUEST *)block;
    MIME_CONDRV_INFO *i = (MIME_CONDRV_INFO *)(rq + 1);
    rq->Set = 1;
    i->Size.X = info->dwSize.X;
    i->Size.Y = info->dwSize.Y;
    i->Cursor.X = info->dwCursorPosition.X;
    i->Cursor.Y = info->dwCursorPosition.Y;
    i->Attributes = info->wAttributes;
    i->PopupAttributes = info->wPopupAttributes;
    /*
     * the size of the window the server is given is right minus left, where the call on windows reads the
     * rectangle it returns as inclusive: setting what was got makes the window a column and a row smaller.
     */
    i->Window.Left = info->srWindow.Left;
    i->Window.Top = info->srWindow.Top;
    i->Window.Right = (SHORT)(info->srWindow.Right - 1);
    i->Window.Bottom = (SHORT)(info->srWindow.Bottom - 1);
    i->MaxWindow.X = info->dwMaximumWindowSize.X;
    i->MaxWindow.Y = info->dwMaximumWindowSize.Y;
    memcpy(i->ColorTable, info->ColorTable, sizeof(i->ColorTable));
    NTSTATUS status = control(console, IOCTL_CONDRV_OUT_INFO, block, sizeof(block), nullptr, 0, nullptr);
    return NT_SUCCESS(status) || fail(status);
}

BOOL WINAPI SetConsoleScreenBufferSize(HANDLE console, COORD size)
{
    MIME_CONDRV_COORD c = {size.X, size.Y};
    NTSTATUS status = control(console, IOCTL_CONDRV_OUT_SIZE, &c, sizeof(c), nullptr, 0, nullptr);
    return NT_SUCCESS(status) || fail(status);
}

BOOL WINAPI SetConsoleTextAttribute(HANDLE console, WORD attributes)
{
    if (attributes & 0x2000) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NTSTATUS status = control(console, IOCTL_CONDRV_OUT_ATTRIBUTE, &attributes, sizeof(attributes), nullptr, 0, nullptr);
    return NT_SUCCESS(status) || fail(status);
}

static BOOL cursor_op(HANDLE console, MIME_CONDRV_CURSOR *c)
{
    NTSTATUS status = control(console, IOCTL_CONDRV_OUT_CURSOR, c, sizeof(*c), c, sizeof(*c), nullptr);
    return NT_SUCCESS(status) || fail(status);
}

BOOL WINAPI SetConsoleCursorPosition(HANDLE console, COORD position)
{
    MIME_CONDRV_CURSOR c = {1, {position.X, position.Y}, 0, 0};
    return cursor_op(console, &c);
}

BOOL WINAPI GetConsoleCursorInfo(HANDLE console, PCONSOLE_CURSOR_INFO info)
{
    MIME_CONDRV_CURSOR c = {};
    if (!cursor_op(console, &c))
        return FALSE;
    if (!info) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    info->dwSize = c.Size;
    info->bVisible = c.Visible;
    return TRUE;
}

BOOL WINAPI SetConsoleCursorInfo(HANDLE console, const CONSOLE_CURSOR_INFO *info)
{
    if (!info) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    MIME_CONDRV_CURSOR c = {1, {0, 0}, info->dwSize, info->bVisible != 0};
    if (!c.Size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return cursor_op(console, &c);
}

BOOL WINAPI SetConsoleWindowInfo(HANDLE console, BOOL absolute, const SMALL_RECT *window)
{
    if (!window) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    MIME_CONDRV_WINDOW w = {absolute != 0, {window->Left, window->Top, window->Right, window->Bottom}};
    NTSTATUS status = control(console, IOCTL_CONDRV_OUT_WINDOW, &w, sizeof(w), nullptr, 0, nullptr);
    return NT_SUCCESS(status) || fail(status);
}

COORD WINAPI GetLargestConsoleWindowSize(HANDLE console)
{
    MIME_CONDRV_INFO i;
    COORD c = {0, 0};
    if (screen_info(console, &i)) {
        c.X = i.MaxWindow.X;
        c.Y = i.MaxWindow.Y;
    }
    return c;
}

HANDLE WINAPI CreateConsoleScreenBuffer(DWORD access, DWORD share, const SECURITY_ATTRIBUTES *sa, DWORD flags, LPVOID data)
{
    if (flags != CONSOLE_TEXTMODE_BUFFER && flags != 0) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    NTSTATUS status;
    HANDLE h = open_driver_file(MIME_CONDRV_SCREEN_BUFFER, access ? access : GENERIC_READ | GENERIC_WRITE,
                                share, sa && sa->bInheritHandle ? OBJ_INHERIT : 0, &status);
    if (!h) {
        KbSetStatus(status == STATUS_OBJECT_NAME_NOT_FOUND ? STATUS_INVALID_HANDLE : status);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

BOOL WINAPI SetConsoleActiveScreenBuffer(HANDLE console)
{
    NTSTATUS status = control(console, IOCTL_CONDRV_OUT_ACTIVATE, nullptr, 0, nullptr, 0, nullptr);
    return NT_SUCCESS(status) || fail(status);
}

/* ------------------------------------------------------------------ rectangles of cells */

static BOOL rect_transfer(HANDLE console, BOOL write, PCHAR_INFO buffer, COORD size, COORD origin, PSMALL_RECT region,
                          BOOL ansi)
{
    if (!buffer || !region) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    /*
     * the size of the rectangle is cut to what the caller's array has room for from the origin on. a read of
     * nothing, or of nothing that is in the buffer, fails with the error of a server that does not know the
     * call; a write of nothing is a bad parameter.
     */
    int w = (SHORT)(region->Right - region->Left + 1), h = (SHORT)(region->Bottom - region->Top + 1);
    if (w > size.X - origin.X)
        w = size.X - origin.X;
    if (h > size.Y - origin.Y)
        h = size.Y - origin.Y;
    if (w <= 0 || h <= 0) {
        if (write) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        region->Right = (SHORT)(region->Left + w - 1);
        region->Bottom = (SHORT)(region->Top + h - 1);
        KbSetError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    if (origin.X < 0 || origin.Y < 0) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UINT cp = ansi ? code_page(FALSE) : 0;
    int rows_per_call = h;
    if ((DWORD)w * (DWORD)rows_per_call > MAX_TRANSFER_CELLS)
        rows_per_call = w > MAX_TRANSFER_CELLS ? 1 : MAX_TRANSFER_CELLS / w;
    SHORT left = region->Left, top = region->Top;
    SMALL_RECT total = {SHRT_MAX, SHRT_MAX, SHRT_MIN, SHRT_MIN};
    BOOL any = FALSE;
    for (int y0 = 0; y0 < h; y0 += rows_per_call) {
        int rows = h - y0 < rows_per_call ? h - y0 : rows_per_call;
        ULONG cells = (ULONG)w * (ULONG)rows;
        ULONG size_in = sizeof(MIME_CONDRV_RECT_IO) + (write ? cells * sizeof(CHAR_INFO) : 0);
        ULONG size_out = sizeof(MIME_CONDRV_RECT) + (write ? 0 : cells * sizeof(CHAR_INFO));
        ULONG block_size = size_in > size_out ? size_in : size_out;
        PUCHAR block = KbAlloc(block_size);
        if (!block) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        MIME_CONDRV_RECT_IO *rq = (MIME_CONDRV_RECT_IO *)block;
        rq->Rect.Left = left;
        rq->Rect.Top = (SHORT)(top + y0);
        rq->Rect.Right = (SHORT)(left + w - 1);
        rq->Rect.Bottom = (SHORT)(top + y0 + rows - 1);
        if (write) {
            CHAR_INFO *cellsp = (CHAR_INFO *)(rq + 1);
            for (int y = 0; y < rows; y++) {
                const CHAR_INFO *src = buffer + (size_t)(origin.Y + y0 + y) * (size_t)size.X + (size_t)origin.X;
                for (int x = 0; x < w; x++) {
                    CHAR_INFO c = src[x];
                    if (ansi) {
                        WCHAR wc = 0;
                        MultiByteToWideChar(cp, 0, &c.Char.AsciiChar, 1, &wc, 1);
                        c.Char.UnicodeChar = wc;
                    }
                    cellsp[(size_t)y * (size_t)w + (size_t)x] = c;
                }
            }
        }
        MIME_CONDRV_RECT_IO copy = *rq;
        ULONG_PTR done;
        NTSTATUS status = control(console, write ? IOCTL_CONDRV_OUT_WRITE_RECT : IOCTL_CONDRV_OUT_READ_RECT, block, size_in,
                                  block, size_out, &done);
        if (!NT_SUCCESS(status)) {
            KbFree(block);
            return fail(status);
        }
        MIME_CONDRV_RECT got;
        memcpy(&got, block, sizeof(got));
        if (!write && got.Right >= got.Left && got.Bottom >= got.Top) {
            const CHAR_INFO *cellsp = (const CHAR_INFO *)(block + sizeof(MIME_CONDRV_RECT));
            for (int y = got.Top; y <= got.Bottom; y++) {
                CHAR_INFO *dst = buffer + (size_t)(origin.Y + y0 + (y - copy.Rect.Top)) * (size_t)size.X + (size_t)origin.X;
                for (int x = got.Left; x <= got.Right; x++) {
                    CHAR_INFO c = cellsp[(size_t)(y - copy.Rect.Top) * (size_t)w + (size_t)(x - copy.Rect.Left)];
                    if (ansi) {
                        CHAR a = 0;
                        WCHAR wc = c.Char.UnicodeChar;
                        if (WideCharToMultiByte(cp, 0, &wc, 1, &a, 1, nullptr, nullptr) != 1)
                            a = '?';
                        c.Char.UnicodeChar = 0;
                        c.Char.AsciiChar = wc < 0x80 ? (CHAR)wc : a;
                    }
                    dst[x - copy.Rect.Left] = c;
                }
            }
        }
        if (got.Right >= got.Left && got.Bottom >= got.Top) {
            any = TRUE;
            if (got.Left < total.Left) total.Left = got.Left;
            if (got.Top < total.Top) total.Top = got.Top;
            if (got.Right > total.Right) total.Right = got.Right;
            if (got.Bottom > total.Bottom) total.Bottom = got.Bottom;
        }
        KbFree(block);
    }
    if (any) {
        *region = total;
        return TRUE;
    }
    /* the server read nothing of the buffer. */
    region->Right = (SHORT)(region->Left - 1);
    region->Bottom = (SHORT)(region->Top - 1);
    if (write)
        return TRUE;
    KbSetError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

BOOL WINAPI ReadConsoleOutputW(HANDLE console, PCHAR_INFO buffer, COORD size, COORD origin, PSMALL_RECT region)
{
    return rect_transfer(console, FALSE, buffer, size, origin, region, FALSE);
}

BOOL WINAPI ReadConsoleOutputA(HANDLE console, PCHAR_INFO buffer, COORD size, COORD origin, PSMALL_RECT region)
{
    return rect_transfer(console, FALSE, buffer, size, origin, region, TRUE);
}

BOOL WINAPI WriteConsoleOutputW(HANDLE console, const CHAR_INFO *buffer, COORD size, COORD origin, PSMALL_RECT region)
{
    return rect_transfer(console, TRUE, (PCHAR_INFO)buffer, size, origin, region, FALSE);
}

BOOL WINAPI WriteConsoleOutputA(HANDLE console, const CHAR_INFO *buffer, COORD size, COORD origin, PSMALL_RECT region)
{
    return rect_transfer(console, TRUE, (PCHAR_INFO)buffer, size, origin, region, TRUE);
}

/* runs of characters or attributes from a cell on. */
static BOOL read_run(HANDLE console, ULONG kind, COORD start, void *out, DWORD length, LPDWORD read)
{
    if (read)
        *read = 0;
    DWORD total = 0;
    while (total < length) {
        DWORD n = length - total > MAX_TRANSFER_CELLS ? MAX_TRANSFER_CELLS : length - total;
        PUCHAR block = KbAlloc(sizeof(MIME_CONDRV_RUN) > sizeof(ULONG) + n * 2 ? sizeof(MIME_CONDRV_RUN) : sizeof(ULONG) + n * 2);
        if (!block) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        MIME_CONDRV_RUN rq = {kind, {start.X, start.Y}, n};
        memcpy(block, &rq, sizeof(rq));
        ULONG_PTR done;
        NTSTATUS status = control(console, IOCTL_CONDRV_OUT_READ_RUN, &rq, sizeof(rq), block, sizeof(ULONG) + n * 2, &done);
        if (!NT_SUCCESS(status)) {
            KbFree(block);
            return fail(status);
        }
        ULONG got = *(PULONG)block;
        memcpy((PUCHAR)out + (size_t)total * 2, block + sizeof(ULONG), got * 2);
        KbFree(block);
        total += got;
        /* the cells go on in the rows below. */
        ULONG skip = got;
        LONG64 pos = (LONG64)start.Y * 0 + skip;
        (void)pos;
        if (got < n)
            break;
        if (total < length) {
            MIME_CONDRV_INFO info;
            if (!screen_info(console, &info))
                return FALSE;
            ULONG64 index = (ULONG64)start.Y * (ULONG64)info.Size.X + (ULONG64)start.X + got;
            start.X = (SHORT)(index % (ULONG64)info.Size.X);
            start.Y = (SHORT)(index / (ULONG64)info.Size.X);
        }
    }
    if (read)
        *read = total;
    return TRUE;
}

BOOL WINAPI ReadConsoleOutputAttribute(HANDLE console, LPWORD attributes, DWORD length, COORD start, LPDWORD read)
{
    if (!attributes) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    return read_run(console, MIME_CONDRV_RUN_ATTRIBUTES, start, attributes, length, read);
}

BOOL WINAPI ReadConsoleOutputCharacterW(HANDLE console, LPWSTR chars, DWORD length, COORD start, LPDWORD read)
{
    if (!chars) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    return read_run(console, MIME_CONDRV_RUN_CHARS, start, chars, length, read);
}

BOOL WINAPI ReadConsoleOutputCharacterA(HANDLE console, LPSTR chars, DWORD length, COORD start, LPDWORD read)
{
    if (read)
        *read = 0;
    if (!chars) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    PWSTR wide = KbAlloc((SIZE_T)length * sizeof(WCHAR) + sizeof(WCHAR));
    if (!wide) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    DWORD got = 0;
    BOOL ok = read_run(console, MIME_CONDRV_RUN_CHARS, start, wide, length, &got);
    if (ok) {
        int n = got ? WideCharToMultiByte(code_page(FALSE), 0, wide, (int)got, chars, (int)length, nullptr, nullptr) : 0;
        if (read)
            *read = n > 0 ? (DWORD)n : 0;
    }
    KbFree(wide);
    return ok;
}

static BOOL write_run(HANDLE console, ULONG kind, COORD start, const void *data, DWORD length, LPDWORD written)
{
    if (written)
        *written = 0;
    if (!length)
        return TRUE;
    DWORD total = 0;
    while (total < length) {
        DWORD n = length - total > MAX_TRANSFER_CELLS ? MAX_TRANSFER_CELLS : length - total;
        ULONG size = sizeof(MIME_CONDRV_RUN) + n * 2;
        PUCHAR block = KbAlloc(size > sizeof(ULONG) ? size : sizeof(ULONG));
        if (!block) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        MIME_CONDRV_RUN rq = {kind, {start.X, start.Y}, n};
        memcpy(block, &rq, sizeof(rq));
        memcpy(block + sizeof(rq), (const UCHAR *)data + (size_t)total * 2, n * 2);
        NTSTATUS status = control(console, IOCTL_CONDRV_OUT_WRITE_RUN, block, size, block, sizeof(ULONG), nullptr);
        ULONG got = NT_SUCCESS(status) ? *(PULONG)block : 0;
        KbFree(block);
        if (!NT_SUCCESS(status))
            return fail(status);
        total += got;
        if (got < n)
            break;
        if (total < length) {
            MIME_CONDRV_INFO info;
            if (!screen_info(console, &info))
                return FALSE;
            ULONG64 index = (ULONG64)start.Y * (ULONG64)info.Size.X + (ULONG64)start.X + got;
            start.X = (SHORT)(index % (ULONG64)info.Size.X);
            start.Y = (SHORT)(index / (ULONG64)info.Size.X);
        }
    }
    if (written)
        *written = total;
    return TRUE;
}

BOOL WINAPI WriteConsoleOutputCharacterW(HANDLE console, LPCWSTR text, DWORD length, COORD start, LPDWORD written)
{
    if (!text && length) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    return write_run(console, MIME_CONDRV_RUN_CHARS, start, text, length, written);
}

BOOL WINAPI WriteConsoleOutputCharacterA(HANDLE console, LPCSTR text, DWORD length, COORD start, LPDWORD written)
{
    if (written)
        *written = 0;
    if (!text && length) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    if (!length)
        return TRUE;
    PWSTR wide = KbAlloc((SIZE_T)length * sizeof(WCHAR));
    if (!wide) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    int n = MultiByteToWideChar(code_page(FALSE), 0, text, (int)length, wide, (int)length);
    DWORD done = 0;
    BOOL ok = write_run(console, MIME_CONDRV_RUN_CHARS, start, wide, n > 0 ? (DWORD)n : 0, &done);
    KbFree(wide);
    if (ok && written)
        *written = done;
    return ok;
}

BOOL WINAPI WriteConsoleOutputAttribute(HANDLE console, const WORD *attributes, DWORD length, COORD start, LPDWORD written)
{
    if (!attributes && length) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    return write_run(console, MIME_CONDRV_RUN_ATTRIBUTES, start, attributes, length, written);
}

static BOOL fill_run(HANDLE console, ULONG kind, USHORT value, DWORD length, COORD start, LPDWORD written)
{
    if (written)
        *written = 0;
    MIME_CONDRV_FILL rq = {kind, {start.X, start.Y}, length, value};
    UCHAR block[sizeof(MIME_CONDRV_FILL)];
    memcpy(block, &rq, sizeof(rq));
    NTSTATUS status = control(console, IOCTL_CONDRV_OUT_FILL, block, sizeof(block), block, sizeof(block), nullptr);
    if (!NT_SUCCESS(status))
        return fail(status);
    if (written)
        *written = *(PULONG)block;
    return TRUE;
}

BOOL WINAPI FillConsoleOutputCharacterW(HANDLE console, WCHAR c, DWORD length, COORD start, LPDWORD written)
{
    return fill_run(console, MIME_CONDRV_RUN_CHARS, c, length, start, written);
}

BOOL WINAPI FillConsoleOutputCharacterA(HANDLE console, CHAR c, DWORD length, COORD start, LPDWORD written)
{
    WCHAR w = (UCHAR)c;
    if (c & 0x80)
        MultiByteToWideChar(code_page(FALSE), 0, &c, 1, &w, 1);
    return fill_run(console, MIME_CONDRV_RUN_CHARS, w, length, start, written);
}

BOOL WINAPI FillConsoleOutputAttribute(HANDLE console, WORD attribute, DWORD length, COORD start, LPDWORD written)
{
    return fill_run(console, MIME_CONDRV_RUN_ATTRIBUTES, attribute, length, start, written);
}

static BOOL scroll(HANDLE console, const SMALL_RECT *source, const SMALL_RECT *clip, COORD destination, WCHAR c, WORD attributes)
{
    if (!source) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    MIME_CONDRV_SCROLL rq = {};
    rq.Source.Left = source->Left;
    rq.Source.Top = source->Top;
    rq.Source.Right = source->Right;
    rq.Source.Bottom = source->Bottom;
    if (clip) {
        rq.HasClip = 1;
        rq.Clip.Left = clip->Left;
        rq.Clip.Top = clip->Top;
        rq.Clip.Right = clip->Right;
        rq.Clip.Bottom = clip->Bottom;
    }
    rq.Destination.X = destination.X;
    rq.Destination.Y = destination.Y;
    rq.Fill.Char = c;
    rq.Fill.Attributes = attributes;
    NTSTATUS status = control(console, IOCTL_CONDRV_OUT_SCROLL, &rq, sizeof(rq), nullptr, 0, nullptr);
    return NT_SUCCESS(status) || fail(status);
}

BOOL WINAPI ScrollConsoleScreenBufferW(HANDLE console, const SMALL_RECT *source, const SMALL_RECT *clip, COORD destination,
                                       const CHAR_INFO *fill)
{
    if (!fill) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    return scroll(console, source, clip, destination, fill->Char.UnicodeChar, fill->Attributes);
}

BOOL WINAPI ScrollConsoleScreenBufferA(HANDLE console, const SMALL_RECT *source, const SMALL_RECT *clip, COORD destination,
                                       const CHAR_INFO *fill)
{
    if (!fill) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    WCHAR w = 0;
    CHAR a = fill->Char.AsciiChar;
    if (a)
        MultiByteToWideChar(code_page(FALSE), 0, &a, 1, &w, 1);
    return scroll(console, source, clip, destination, w, fill->Attributes);
}

/* ------------------------------------------------------------------ fonts and the like */

static BOOL font_info(HANDLE console, MIME_CONDRV_FONT *font)
{
    NTSTATUS status = control(console, IOCTL_CONDRV_OUT_FONT, nullptr, 0, font, sizeof(*font), nullptr);
    return NT_SUCCESS(status) || fail(status);
}

BOOL WINAPI GetCurrentConsoleFont(HANDLE console, BOOL maximum, PCONSOLE_FONT_INFO info)
{
    MIME_CONDRV_FONT f;
    if (!font_info(console, &f))
        return FALSE;
    info->nFont = 0;
    info->dwFontSize.X = f.Size.X;
    info->dwFontSize.Y = f.Size.Y;
    return TRUE;
}

BOOL WINAPI GetCurrentConsoleFontEx(HANDLE console, BOOL maximum, PCONSOLE_FONT_INFOEX info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MIME_CONDRV_FONT f;
    if (!font_info(console, &f))
        return FALSE;
    info->nFont = 0;
    info->dwFontSize.X = f.Size.X;
    info->dwFontSize.Y = f.Size.Y;
    info->FontFamily = f.Family;
    info->FontWeight = f.Weight;
    memcpy(info->FaceName, f.Face, (RTL_NUMBER_OF(info->FaceName) - 1) * sizeof(WCHAR));
    info->FaceName[RTL_NUMBER_OF(info->FaceName) - 1] = 0;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI SetCurrentConsoleFontEx(HANDLE console, BOOL maximum, PCONSOLE_FONT_INFOEX info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MIME_CONDRV_FONT f;
    /* the font of a terminal is the terminal's: the call is accepted by a handle that may be written. */
    return font_info(console, &f);
}

DECLSPEC_EXPORT COORD WINAPI GetConsoleFontSize(HANDLE console, DWORD font)
{
    COORD c = {0, 0};
    MIME_CONDRV_FONT f;
    if (font != 0) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return c;
    }
    if (font_info(console, &f)) {
        c.X = f.Size.X;
        c.Y = f.Size.Y;
    }
    return c;
}

DECLSPEC_EXPORT BOOL WINAPI GetConsoleSelectionInfo(PCONSOLE_SELECTION_INFO info)
{
    if (!info) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    memset(info, 0, sizeof(*info));
    return has_console() || (KbSetError(ERROR_INVALID_HANDLE), FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI GetNumberOfConsoleMouseButtons(LPDWORD buttons)
{
    if (!buttons) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    *buttons = 3;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI SetConsoleDisplayMode(HANDLE console, DWORD flags, PCOORD dimensions)
{
    MIME_CONDRV_INFO i;
    if (!screen_info(console, &i))
        return FALSE;
    if (dimensions) {
        dimensions->X = i.Size.X;
        dimensions->Y = i.Size.Y;
    }
    if (!(flags & (CONSOLE_FULLSCREEN_MODE | CONSOLE_WINDOWED_MODE))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ history and aliases */

static BOOL history_call(ULONG op, ULONG value, ULONG value2, PCWSTR exe, ULONG exe_length, void *out, ULONG out_size,
                         ULONG_PTR *done)
{
    ULONG in_size = sizeof(MIME_CONDRV_HISTORY) + exe_length * sizeof(WCHAR);
    ULONG size = in_size > out_size ? in_size : out_size;
    PUCHAR block = KbAlloc(size);
    if (!block) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    MIME_CONDRV_HISTORY *rq = (MIME_CONDRV_HISTORY *)block;
    rq->Op = op;
    rq->Value = value;
    rq->Value2 = value2;
    rq->ExeNameLength = exe_length;
    memcpy(rq + 1, exe, exe_length * sizeof(WCHAR));
    ULONG_PTR got = 0;
    NTSTATUS status = process_control(IOCTL_CONDRV_IN_HISTORY, block, in_size, block, size, &got);
    BOOL ok = NT_SUCCESS(status);
    if (ok && out)
        memcpy(out, block, got < out_size ? got : out_size);
    if (done)
        *done = got;
    KbFree(block);
    if (!ok) {
        KbSetError(status == STATUS_BUFFER_OVERFLOW ? ERROR_INSUFFICIENT_BUFFER : status == STATUS_INVALID_PARAMETER ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
    }
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI GetConsoleHistoryInfo(PCONSOLE_HISTORY_INFO info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MIME_CONDRV_HISTORY_INFO h;
    if (!history_call(MIME_CONDRV_HISTORY_GET_INFO, 0, 0, nullptr, 0, &h, sizeof(h), nullptr))
        return FALSE;
    info->HistoryBufferSize = h.BufferSize;
    info->NumberOfHistoryBuffers = h.NumberOfBuffers;
    info->dwFlags |= h.Flags;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI SetConsoleHistoryInfo(PCONSOLE_HISTORY_INFO info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* the flags travel in the length of the name, which this request has no use for. */
    ULONG in_size = sizeof(MIME_CONDRV_HISTORY);
    MIME_CONDRV_HISTORY rq = {MIME_CONDRV_HISTORY_SET_INFO, info->HistoryBufferSize, info->NumberOfHistoryBuffers, info->dwFlags};
    NTSTATUS status = process_control(IOCTL_CONDRV_IN_HISTORY, &rq, in_size, nullptr, 0, nullptr);
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_INVALID_PARAMETER ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

/* the exe name argument, wide, of a wide or ansi call. */
static PWSTR exe_arg(LPCSTR ansi, LPCWSTR wide, BOOL is_ansi)
{
    if (!is_ansi)
        return (PWSTR)wide;
    return KbAnsiToWideCp(code_page(TRUE), ansi);
}

static DWORD history_bytes(PCWSTR exe, BOOL is_ansi, void *buffer, DWORD size, BOOL length_only)
{
    ULONG exe_length = exe ? (ULONG)wcslen(exe) : 0;
    ULONG_PTR done = 0;
    if (length_only) {
        ULONG chars = 0;
        if (!history_call(MIME_CONDRV_HISTORY_LENGTH, 0, 0, exe, exe_length, &chars, sizeof(chars), &done))
            return 0;
        if (!is_ansi)
            return chars * sizeof(WCHAR);
        return chars;
    }
    ULONG room = size / (is_ansi ? 1 : sizeof(WCHAR));
    PWSTR wide = KbAlloc((SIZE_T)room * sizeof(WCHAR) + sizeof(WCHAR));
    if (!wide) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    if (!history_call(MIME_CONDRV_HISTORY_GET, 0, 0, exe, exe_length, wide, room * sizeof(WCHAR), &done)) {
        KbFree(wide);
        return 0;
    }
    ULONG chars = (ULONG)(done / sizeof(WCHAR));
    DWORD result;
    if (!is_ansi) {
        memcpy(buffer, wide, chars * sizeof(WCHAR));
        result = chars * sizeof(WCHAR);
    } else {
        int n = chars ? WideCharToMultiByte(code_page(TRUE), 0, wide, (int)chars, buffer, (int)size, nullptr, nullptr) : 0;
        result = n > 0 ? (DWORD)n : 0;
    }
    KbFree(wide);
    return result;
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleCommandHistoryW(LPWSTR commands, DWORD length, LPCWSTR exe)
{
    if (!commands && length) {
        KbSetError(ERROR_INVALID_ACCESS);
        return 0;
    }
    return history_bytes(exe, FALSE, commands, length, FALSE);
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleCommandHistoryA(LPSTR commands, DWORD length, LPCSTR exe)
{
    PWSTR w = exe_arg(exe, nullptr, TRUE);
    DWORD n = history_bytes(w, TRUE, commands, length, FALSE);
    KbFree(w);
    return n;
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleCommandHistoryLengthW(LPCWSTR exe)
{
    return history_bytes(exe, FALSE, nullptr, 0, TRUE);
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleCommandHistoryLengthA(LPCSTR exe)
{
    PWSTR w = exe_arg(exe, nullptr, TRUE);
    DWORD n = history_bytes(w, TRUE, nullptr, 0, TRUE);
    KbFree(w);
    return n;
}

DECLSPEC_EXPORT VOID WINAPI ExpungeConsoleCommandHistoryW(LPCWSTR exe)
{
    history_call(MIME_CONDRV_HISTORY_EXPUNGE, 0, 0, exe, exe ? (ULONG)wcslen(exe) : 0, nullptr, 0, nullptr);
}

DECLSPEC_EXPORT VOID WINAPI ExpungeConsoleCommandHistoryA(LPCSTR exe)
{
    PWSTR w = exe_arg(exe, nullptr, TRUE);
    ExpungeConsoleCommandHistoryW(w);
    KbFree(w);
}

DECLSPEC_EXPORT BOOL WINAPI SetConsoleNumberOfCommandsW(DWORD number, LPCWSTR exe)
{
    return history_call(MIME_CONDRV_HISTORY_SET_COUNT, number, 0, exe, exe ? (ULONG)wcslen(exe) : 0, nullptr, 0, nullptr);
}

DECLSPEC_EXPORT BOOL WINAPI SetConsoleNumberOfCommandsA(DWORD number, LPCSTR exe)
{
    PWSTR w = exe_arg(exe, nullptr, TRUE);
    BOOL ok = SetConsoleNumberOfCommandsW(number, w);
    KbFree(w);
    return ok;
}

static BOOL alias_call(ULONG op, PCWSTR source, PCWSTR target, PCWSTR exe, void *out, ULONG out_size, ULONG_PTR *done)
{
    ULONG sl = source ? (ULONG)wcslen(source) : 0, tl = target ? (ULONG)wcslen(target) : 0, el = exe ? (ULONG)wcslen(exe) : 0;
    ULONG in_size = sizeof(MIME_CONDRV_ALIAS) + (sl + tl + el) * sizeof(WCHAR);
    ULONG size = in_size > out_size ? in_size : out_size;
    PUCHAR block = KbAlloc(size);
    if (!block) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    MIME_CONDRV_ALIAS *rq = (MIME_CONDRV_ALIAS *)block;
    rq->Op = op;
    rq->SourceLength = sl;
    rq->TargetLength = tl;
    rq->ExeNameLength = el;
    PWCHAR text = (PWCHAR)(rq + 1);
    memcpy(text, source, sl * sizeof(WCHAR));
    memcpy(text + sl, target, tl * sizeof(WCHAR));
    memcpy(text + sl + tl, exe, el * sizeof(WCHAR));
    ULONG_PTR got = 0;
    NTSTATUS status = process_control(IOCTL_CONDRV_IN_ALIAS, block, in_size, block, size, &got);
    if (NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW) {
        if (out)
            memcpy(out, block, got < out_size ? got : out_size);
    }
    if (done)
        *done = got;
    KbFree(block);
    if (status == STATUS_BUFFER_OVERFLOW) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_OBJECT_NAME_NOT_FOUND ? ERROR_GEN_FAILURE
                   : status == STATUS_INVALID_PARAMETER   ? ERROR_INVALID_PARAMETER
                                                          : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI AddConsoleAliasW(LPWSTR source, LPWSTR target, LPWSTR exe)
{
    if (!source || !exe) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return alias_call(MIME_CONDRV_ALIAS_ADD, source, target, exe, nullptr, 0, nullptr);
}

DECLSPEC_EXPORT BOOL WINAPI AddConsoleAliasA(LPSTR source, LPSTR target, LPSTR exe)
{
    UINT cp = code_page(TRUE);
    PWSTR s = KbAnsiToWideCp(cp, source), t = target ? KbAnsiToWideCp(cp, target) : nullptr, e = KbAnsiToWideCp(cp, exe);
    BOOL ok = s && e ? AddConsoleAliasW(s, t, e) : (KbSetError(ERROR_INVALID_PARAMETER), FALSE);
    KbFree(s);
    KbFree(t);
    KbFree(e);
    return ok;
}

/* the text of a wide result in the bytes a wide or ansi call counts. */
static DWORD alias_text(ULONG op, PCWSTR source, PCWSTR exe, void *out, DWORD bytes, BOOL is_ansi)
{
    ULONG room = bytes / (is_ansi ? 1 : sizeof(WCHAR));
    PWSTR wide = KbAlloc((SIZE_T)room * sizeof(WCHAR) + sizeof(WCHAR));
    if (!wide) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    ULONG_PTR done = 0;
    BOOL ok = alias_call(op, source, nullptr, exe, wide, room * sizeof(WCHAR), &done);
    DWORD result = 0;
    if (ok) {
        ULONG chars = (ULONG)(done / sizeof(WCHAR));
        if (!is_ansi) {
            memcpy(out, wide, chars * sizeof(WCHAR));
            result = chars * sizeof(WCHAR);
        } else {
            int n = chars ? WideCharToMultiByte(code_page(TRUE), 0, wide, (int)chars, out, (int)bytes, nullptr, nullptr) : 0;
            result = n > 0 ? (DWORD)n : 0;
        }
    } else if (out && bytes) {
        if (is_ansi)
            *(PCHAR)out = 0;
        else
            *(PWCHAR)out = 0;
    }
    KbFree(wide);
    return result;
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleAliasW(LPWSTR source, LPWSTR buffer, DWORD length, LPWSTR exe)
{
    if (!source || !exe) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return alias_text(MIME_CONDRV_ALIAS_GET, source, exe, buffer, length, FALSE);
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleAliasA(LPSTR source, LPSTR buffer, DWORD length, LPSTR exe)
{
    UINT cp = code_page(TRUE);
    PWSTR s = KbAnsiToWideCp(cp, source), e = KbAnsiToWideCp(cp, exe);
    DWORD n = s && e ? alias_text(MIME_CONDRV_ALIAS_GET, s, e, buffer, length, TRUE) : (KbSetError(ERROR_INVALID_PARAMETER), 0);
    KbFree(s);
    KbFree(e);
    return n;
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleAliasesW(LPWSTR buffer, DWORD length, LPWSTR exe)
{
    return alias_text(MIME_CONDRV_ALIAS_LIST, nullptr, exe, buffer, length, FALSE);
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleAliasesA(LPSTR buffer, DWORD length, LPSTR exe)
{
    PWSTR e = KbAnsiToWideCp(code_page(TRUE), exe);
    DWORD n = alias_text(MIME_CONDRV_ALIAS_LIST, nullptr, e, buffer, length, TRUE);
    KbFree(e);
    return n;
}

static DWORD alias_length(ULONG op, PCWSTR exe, BOOL is_ansi)
{
    ULONG_PTR done = 0;
    for (ULONG room = 1024;; room *= 4) {
        PWSTR wide = KbAlloc((SIZE_T)room * sizeof(WCHAR));
        if (!wide) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        BOOL ok = alias_call(op, nullptr, nullptr, exe, wide, room * sizeof(WCHAR), &done);
        DWORD result = 0;
        if (ok) {
            ULONG chars = (ULONG)(done / sizeof(WCHAR));
            result = is_ansi ? (chars ? (DWORD)WideCharToMultiByte(code_page(TRUE), 0, wide, (int)chars, nullptr, 0, nullptr, nullptr) : 0)
                             : chars * sizeof(WCHAR);
        }
        KbFree(wide);
        if (ok || GetLastError() != ERROR_INSUFFICIENT_BUFFER || room > 0x1000000)
            return result;
    }
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleAliasesLengthW(LPWSTR exe)
{
    return alias_length(MIME_CONDRV_ALIAS_LIST, exe, FALSE);
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleAliasesLengthA(LPSTR exe)
{
    PWSTR e = KbAnsiToWideCp(code_page(TRUE), exe);
    DWORD n = alias_length(MIME_CONDRV_ALIAS_LIST, e, TRUE);
    KbFree(e);
    return n;
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleAliasExesW(LPWSTR buffer, DWORD length)
{
    return alias_text(MIME_CONDRV_ALIAS_EXES, nullptr, nullptr, buffer, length, FALSE);
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleAliasExesA(LPSTR buffer, DWORD length)
{
    return alias_text(MIME_CONDRV_ALIAS_EXES, nullptr, nullptr, buffer, length, TRUE);
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleAliasExesLengthW(void)
{
    return alias_length(MIME_CONDRV_ALIAS_EXES, nullptr, FALSE);
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleAliasExesLengthA(void)
{
    return alias_length(MIME_CONDRV_ALIAS_EXES, nullptr, TRUE);
}

DECLSPEC_EXPORT BOOL WINAPI SetConsoleInputExeNameW(LPCWSTR name)
{
    SIZE_T n = name ? wcslen(name) : 0;
    if (!n || n >= RTL_NUMBER_OF(g_exe_name)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlEnterCriticalSection(&g_console_lock);
    wcscpy(g_exe_name, name);
    RtlLeaveCriticalSection(&g_console_lock);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI SetConsoleInputExeNameA(LPCSTR name)
{
    PWSTR w = KbAnsiToWideCp(code_page(TRUE), name);
    if (!w) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = SetConsoleInputExeNameW(w);
    KbFree(w);
    return ok;
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleInputExeNameW(DWORD size, LPWSTR buffer)
{
    ULONG length;
    PCWSTR exe = exe_name(&length);
    if (length + 1 > size) {
        KbSetError(ERROR_BUFFER_OVERFLOW);
        return length + 1;
    }
    wcscpy(buffer, exe);
    return length + 1;
}

DECLSPEC_EXPORT DWORD WINAPI GetConsoleInputExeNameA(DWORD size, LPSTR buffer)
{
    WCHAR w[256];
    DWORD n = GetConsoleInputExeNameW(RTL_NUMBER_OF(w), w);
    int got = WideCharToMultiByte(code_page(TRUE), 0, w, -1, buffer, (int)size, nullptr, nullptr);
    if (!got) {
        KbSetError(ERROR_BUFFER_OVERFLOW);
        return n;
    }
    return n;
}
