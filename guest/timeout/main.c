/*
 * timeout.exe: waits for a number of seconds or until a key is pressed.
 *
 *   timeout [/T] seconds [/NOBREAK]
 *
 * -1 waits until a key is pressed. the input must be a console, otherwise the tool ends at once with code 1.
 * the exit code is 0 after the wait and 1 when something is wrong.
 */
#include "tool.h"

#define MAX_SECONDS 99999

#define VK_SHIFT_ 0x10
#define VK_CONTROL_ 0x11
#define VK_MENU_ 0x12
#define VK_CAPITAL_ 0x14
#define VK_NUMLOCK_ 0x90
#define VK_SCROLL_ 0x91

static const WCHAR kUsage[] =
    L"\nTIMEOUT [/T] timeout [/NOBREAK] \n"
    L"\nDescription:\n"
    L"    This utility accepts a timeout parameter to wait for the specified\n"
    L"    time period (in seconds) or until any key is pressed. It also \n"
    L"    accepts a parameter to ignore the key press. \n\n"
    L"Parameter List:\n"
    L"    /T        timeout       Specifies the number of seconds to wait.\n"
    L"                            Valid range is -1 to 99999 seconds.\n\n"
    L"    /NOBREAK                Ignore key presses and wait specified time.\n\n"
    L"    /?                      Displays this help message.\n\n"
    L"NOTE: A timeout value of -1 means to wait indefinitely for a key press.\n"
    L"\nExamples:\n"
    L"    TIMEOUT /?\n"
    L"    TIMEOUT /T 10\n"
    L"    TIMEOUT /T 300 /NOBREAK\n"
    L"    TIMEOUT /T -1\n";

static int bad_syntax(void)
{
    tu_eprint(L"ERROR: Invalid syntax.\nType \"TIMEOUT /?\" for usage.\n");
    return 1;
}

static BOOL is_switch(const WCHAR *a, const WCHAR *name)
{
    return (a[0] == L'/' || a[0] == L'-') && tu_eq(a + 1, name);
}

/* a key press is waiting in the console's input; other events are dropped. */
static BOOL key_pressed(HANDLE input)
{
    for (;;) {
        DWORD count = 0;
        if (!GetNumberOfConsoleInputEvents(input, &count) || !count)
            return FALSE;
        INPUT_RECORD record;
        DWORD read = 0;
        if (!ReadConsoleInputW(input, &record, 1, &read) || !read)
            return FALSE;
        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
            WORD key = record.Event.KeyEvent.wVirtualKeyCode;
            /* modifier keys alone are no key press. */
            if (key != VK_SHIFT_ && key != VK_CONTROL_ && key != VK_MENU_ && key != VK_CAPITAL_ &&
                key != VK_NUMLOCK_ && key != VK_SCROLL_)
                return TRUE;
        }
    }
}

int tool_main(void)
{
    LONG seconds = 0;
    BOOL have_seconds = FALSE, no_break = FALSE;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i];
        if (is_switch(a, L"?")) {
            if (tu_argc != 2)
                return bad_syntax();
            tu_puts(kUsage);
            return 0;
        }
        if (is_switch(a, L"NOBREAK")) {
            if (no_break)
                return bad_syntax();
            no_break = TRUE;
            continue;
        }
        const WCHAR *value;
        if (is_switch(a, L"T")) {
            if (have_seconds || i + 1 >= tu_argc)
                return bad_syntax();
            value = tu_argv[++i];
        } else if (a[0] != L'/' || (a[1] >= L'0' && a[1] <= L'9')) {
            /* a bare number; a negative one starts with a hyphen. */
            if (have_seconds)
                return bad_syntax();
            value = a;
        } else {
            return bad_syntax();
        }
        if (!tu_parse_int(value, &seconds)) {
            tu_eprint(L"ERROR: Invalid value for timeout (/T) specified. Valid range is -1 to 99999.\n");
            return 1;
        }
        if (seconds < -1 || seconds > MAX_SECONDS) {
            tu_eprint(L"ERROR: Invalid value for timeout (/T) specified. Valid range is -1 to 99999.\n");
            return 1;
        }
        have_seconds = TRUE;
    }
    if (!have_seconds)
        return bad_syntax();

    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (!input || input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode)) {
        tu_eprint(L"ERROR: Input redirection is not supported, exiting the process immediately.\n");
        return 1;
    }
    DWORD old_mode = mode;
    SetConsoleMode(input, (mode & ~(DWORD)ENABLE_LINE_INPUT & ~(DWORD)ENABLE_ECHO_INPUT) | ENABLE_PROCESSED_INPUT);
    /* what was typed before does not count. */
    FlushConsoleInputBuffer(input);

    int width = 1;
    for (LONG n = seconds; n >= 10; n /= 10)
        width++;
    if (seconds < 0) {
        tu_print(no_break ? L"\nPress CTRL+C to quit ..." : L"\nPress any key to continue ...");
        tu_flush();
        for (;;) {
            if (WaitForSingleObject(input, INFINITE) != WAIT_OBJECT_0)
                break;
            if (!no_break && key_pressed(input))
                break;
            if (no_break) {
                INPUT_RECORD record;
                DWORD read;
                ReadConsoleInputW(input, &record, 1, &read);
            }
        }
        SetConsoleMode(input, old_mode);
        tu_print(L"\n");
        return 0;
    }

    ULONGLONG start = GetTickCount64();
    LONG shown = -1;
    for (;;) {
        ULONGLONG elapsed = GetTickCount64() - start;
        LONG left = seconds - (LONG)(elapsed / 1000);
        if (left < 0)
            left = 0;
        if (left != shown) {
            tu_print(shown < 0 ? L"\nWaiting for %*lu" : L"\rWaiting for %*lu", width, (DWORD)left);
            tu_print(no_break ? L" seconds, press CTRL+C to quit ..." : L" seconds, press a key to continue ...");
            tu_flush();
            shown = left;
        }
        if (left == 0)
            break;
        DWORD wait = (DWORD)(1000 - elapsed % 1000);
        DWORD result = WaitForSingleObject(input, wait);
        if (result == WAIT_OBJECT_0) {
            if (no_break) {
                INPUT_RECORD record;
                DWORD read;
                ReadConsoleInputW(input, &record, 1, &read);
            } else if (key_pressed(input)) {
                break;
            }
        }
    }
    SetConsoleMode(input, old_mode);
    tu_print(L"\n");
    return 0;
}
