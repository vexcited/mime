/*
 * console: there is no console host yet, so the standard handles are
 * pseudo handles whose output goes to the terminal that started the
 * program and whose input is always at end of file.
 */
#include "kernelbase_private.h"

#define DEFAULT_INPUT_MODE                                                                                        \
    (ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_INSERT_MODE | ENABLE_QUICK_EDIT_MODE | \
     ENABLE_EXTENDED_FLAGS | ENABLE_AUTO_POSITION)
#define DEFAULT_OUTPUT_MODE (ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT)

static UINT g_input_cp = CP_UTF8;
static UINT g_output_cp = CP_UTF8;
static DWORD g_input_mode = DEFAULT_INPUT_MODE;
static DWORD g_output_mode = DEFAULT_OUTPUT_MODE;
static WORD g_attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
static WCHAR g_title[256];
static RTL_CRITICAL_SECTION g_console_lock = RTL_CRITICAL_SECTION_INIT;

/* bytes of an incomplete utf-8 sequence carried between writes, per output handle. */
static UCHAR g_carry[2][4];
static ULONG g_carry_length[2];

static PHANDLER_ROUTINE g_ctrl_handlers[32];
static ULONG g_ctrl_handler_count;
static BOOL g_ignore_ctrl_c;

/* ------------------------------------------------------------------ text output */

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

BOOL KbConsoleWrite(HANDLE console, LPCWSTR text, DWORD chars)
{
    if (!is_output(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    RtlEnterCriticalSection(&g_console_lock);
    KbDisplayText(text, chars);
    RtlLeaveCriticalSection(&g_console_lock);
    return TRUE;
}

/* length of the utf-8 sequence a lead byte starts, 0 for a continuation or invalid byte. */
static ULONG utf8_sequence_length(UCHAR b)
{
    if (b < 0x80)
        return 1;
    if (b >= 0xC2 && b <= 0xDF)
        return 2;
    if (b >= 0xE0 && b <= 0xEF)
        return 3;
    if (b >= 0xF0 && b <= 0xF4)
        return 4;
    return 0;
}

static void write_converted(UINT cp, const UCHAR *bytes, DWORD length)
{
    WCHAR wide[512];
    while (length) {
        DWORD n = length > 256 ? 256 : length;
        if (cp == CP_UTF8 && n < length) {
            DWORD k = n;
            while (k > n - 3 && (bytes[k] & 0xC0) == 0x80)
                k--;
            n = k;
        }
        int w = MultiByteToWideChar(cp, 0, (LPCCH)bytes, (int)n, wide, RTL_NUMBER_OF(wide));
        if (w > 0)
            KbDisplayText(wide, (SIZE_T)w);
        bytes += n;
        length -= n;
    }
}

BOOL KbConsoleWriteBytes(HANDLE console, const void *data, DWORD length)
{
    if (!is_output(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    const UCHAR *bytes = data;
    RtlEnterCriticalSection(&g_console_lock);
    if (g_output_cp != CP_UTF8) {
        write_converted(g_output_cp, bytes, length);
        RtlLeaveCriticalSection(&g_console_lock);
        return TRUE;
    }
    ULONG slot = console == KB_CONSOLE_ERROR;
    /* complete a sequence left over from the previous write. */
    while (g_carry_length[slot] && length) {
        ULONG need = utf8_sequence_length(g_carry[slot][0]);
        if ((bytes[0] & 0xC0) != 0x80) {
            write_converted(CP_UTF8, g_carry[slot], g_carry_length[slot]);
            g_carry_length[slot] = 0;
            break;
        }
        g_carry[slot][g_carry_length[slot]++] = *bytes++;
        length--;
        if (g_carry_length[slot] == need) {
            write_converted(CP_UTF8, g_carry[slot], need);
            g_carry_length[slot] = 0;
        }
    }
    /* keep an incomplete trailing sequence for the next write. */
    DWORD tail = 0;
    for (DWORD back = 1; back <= 3 && back <= length; back++) {
        UCHAR b = bytes[length - back];
        if ((b & 0xC0) == 0x80)
            continue;
        ULONG need = utf8_sequence_length(b);
        if (need > back)
            tail = back;
        break;
    }
    write_converted(CP_UTF8, bytes, length - tail);
    if (tail) {
        memcpy(g_carry[slot], bytes + length - tail, tail);
        g_carry_length[slot] = tail;
    }
    RtlLeaveCriticalSection(&g_console_lock);
    return TRUE;
}

/* ------------------------------------------------------------------ standard handles */

void KbInitializeConsole(void)
{
    PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
    if (!pp->StandardInput)
        pp->StandardInput = KB_CONSOLE_INPUT;
    if (!pp->StandardOutput)
        pp->StandardOutput = KB_CONSOLE_OUTPUT;
    if (!pp->StandardError)
        pp->StandardError = KB_CONSOLE_ERROR;
    if (pp->WindowTitle.Length) {
        SIZE_T n = pp->WindowTitle.Length / sizeof(WCHAR);
        if (n >= RTL_NUMBER_OF(g_title))
            n = RTL_NUMBER_OF(g_title) - 1;
        memcpy(g_title, pp->WindowTitle.Buffer, n * sizeof(WCHAR));
        g_title[n] = 0;
    }
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

BOOL WINAPI AllocConsole(void)
{
    KbInitializeConsole();
    return TRUE;
}

BOOL WINAPI FreeConsole(void)
{
    return TRUE;
}

BOOL WINAPI AttachConsole(DWORD process)
{
    KbInitializeConsole();
    return TRUE;
}

HWND WINAPI GetConsoleWindow(void)
{
    return NULL;
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
    g_carry_length[0] = g_carry_length[1] = 0;
    RtlLeaveCriticalSection(&g_console_lock);
    return TRUE;
}

BOOL WINAPI GetConsoleMode(HANDLE console, LPDWORD mode)
{
    if (!KbIsConsoleHandle(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!mode) {
        KbSetError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    *mode = KbIsConsoleInput(console) ? g_input_mode : g_output_mode;
    return TRUE;
}

BOOL WINAPI SetConsoleMode(HANDLE console, DWORD mode)
{
    if (!KbIsConsoleHandle(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (KbIsConsoleInput(console)) {
        if (mode & ~(DWORD)0x3FF) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        g_input_mode = mode;
    } else {
        if (mode & ~(DWORD)0x1F) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        g_output_mode = mode;
    }
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

static BOOL check_input(HANDLE console)
{
    if (!KbIsConsoleInput(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI ReadConsoleW(HANDLE console, LPVOID buffer, DWORD count, LPDWORD read,
                         PCONSOLE_READCONSOLE_CONTROL control)
{
    if (!check_input(console))
        return FALSE;
    if (read)
        *read = 0;
    return TRUE;
}

BOOL WINAPI ReadConsoleA(HANDLE console, LPVOID buffer, DWORD count, LPDWORD read,
                         PCONSOLE_READCONSOLE_CONTROL control)
{
    return ReadConsoleW(console, buffer, count, read, control);
}

/* there are no input events: reading reports a broken channel, so callers treat it as end of file. */
BOOL WINAPI ReadConsoleInputW(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    if (read)
        *read = 0;
    if (!check_input(console))
        return FALSE;
    KbSetError(ERROR_BROKEN_PIPE);
    return FALSE;
}

BOOL WINAPI ReadConsoleInputA(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    return ReadConsoleInputW(console, buffer, count, read);
}

BOOL WINAPI PeekConsoleInputW(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    if (!check_input(console))
        return FALSE;
    if (read)
        *read = 0;
    return TRUE;
}

BOOL WINAPI PeekConsoleInputA(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read)
{
    return PeekConsoleInputW(console, buffer, count, read);
}

BOOL WINAPI GetNumberOfConsoleInputEvents(HANDLE console, LPDWORD count)
{
    if (!check_input(console))
        return FALSE;
    if (count)
        *count = 0;
    return TRUE;
}

BOOL WINAPI FlushConsoleInputBuffer(HANDLE console)
{
    return check_input(console);
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
    if (!is_output(console)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI GetConsoleScreenBufferInfo(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFO info)
{
    if (!check_output(console))
        return FALSE;
    info->dwSize.X = 80;
    info->dwSize.Y = 25;
    info->dwCursorPosition.X = 0;
    info->dwCursorPosition.Y = 0;
    info->wAttributes = g_attributes;
    info->srWindow.Left = 0;
    info->srWindow.Top = 0;
    info->srWindow.Right = 79;
    info->srWindow.Bottom = 24;
    info->dwMaximumWindowSize.X = 80;
    info->dwMaximumWindowSize.Y = 25;
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
    KbDisplayText(seq, wcslen(seq));
    RtlLeaveCriticalSection(&g_console_lock);
    return TRUE;
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
    COORD c = {80, 25};
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

BOOL WINAPI GenerateConsoleCtrlEvent(DWORD event, DWORD group)
{
    if (event > CTRL_BREAK_EVENT) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (event == CTRL_C_EVENT && g_ignore_ctrl_c)
        return TRUE;
    for (ULONG i = g_ctrl_handler_count; i-- > 0;)
        if (g_ctrl_handlers[i](event))
            return TRUE;
    ExitProcess(STATUS_CONTROL_C_EXIT);
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
    list[0] = GetCurrentProcessId();
    return 1;
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
