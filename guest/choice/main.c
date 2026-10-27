/*
 * choice.exe: asks for one of a few keys and ends with the index of the key, counted from 1.
 *
 *   choice [/C choices] [/N] [/CS] [/T seconds /D default] [/M text]
 *
 * the exit code is the index of the choice, 0 after CTRL+C and 255 when the command line is wrong or the input ended.
 */
#include "tool.h"

#define MAX_TEXT 254

static const WCHAR kUsage[] =
    L"\nCHOICE [/C choices] [/N] [/CS] [/T timeout /D choice] [/M text]\n\n"
    L"Description:\n"
    L"    This tool allows users to select one item from a list \n"
    L"    of choices and returns the index of the selected choice.\n\n"
    L"Parameter List:\n"
    L"   /C    choices       Specifies the list of choices to be created.\n"
    L"                       Default list is \"YN\".\n\n"
    L"   /N                  Hides the list of choices in the prompt.\n"
    L"                       The message before the prompt is displayed\n"
    L"                       and the choices are still enabled.\n\n"
    L"   /CS                 Enables case-sensitive choices to be selected.\n"
    L"                       By default, the utility is case-insensitive.\n\n"
    L"   /T    timeout       The number of seconds to pause before a default \n"
    L"                       choice is made. Acceptable values are from 0 to \n"
    L"                       9999. If 0 is specified, there will be no pause \n"
    L"                       and the default choice is selected.\n\n"
    L"   /D    choice        Specifies the default choice after nnnn seconds.\n"
    L"                       Character must be in the set of choices specified\n"
    L"                       by /C option and must also specify nnnn with /T.\n\n"
    L"   /M    text          Specifies the message to be displayed before \n"
    L"                       the prompt. If not specified, the utility \n"
    L"                       displays only a prompt.\n\n"
    L"   /?                  Displays this help message.\n\n"
    L"   NOTE:\n"
    L"   The ERRORLEVEL environment variable is set to the index of the\n"
    L"   key that was selected from the set of choices. The first choice\n"
    L"   listed returns a value of 1, the second a value of 2, and so on.\n"
    L"   If the user presses a key that is not a valid choice, the tool \n"
    L"   sounds a warning beep. If tool detects an error condition,\n"
    L"   it returns an ERRORLEVEL value of 255. If the user presses \n"
    L"   CTRL+BREAK or CTRL+C, the tool returns an ERRORLEVEL value\n"
    L"   of 0. When you use ERRORLEVEL parameters in a batch program, list\n"
    L"   them in decreasing order.\n\n"
    L"Examples:\n"
    L"   CHOICE /?\n"
    L"   CHOICE /C YNC /M \"Press Y for Yes, N for No or C for Cancel.\"\n"
    L"   CHOICE /T 10 /C ync /CS /D y \n"
    L"   CHOICE /C ab /M \"Select a for option 1 and b for option 2.\"\n"
    L"   CHOICE /C ab /N /M \"Select a for option 1 and b for option 2.\"\n";

static int fail(const WCHAR *text)
{
    tu_eprint(L"%s", text);
    return 255;
}

/* the option name at the start of the word, with the value that follows a colon or sits in the next word. */
static BOOL option_of(const WCHAR *a, const WCHAR *name, const WCHAR **inline_value)
{
    if (a[0] != L'/' && a[0] != L'-')
        return FALSE;
    SIZE_T n = wcslen(name);
    if (!tu_eq_n(a + 1, name, n))
        return FALSE;
    if (!a[1 + n]) {
        *inline_value = nullptr;
        return TRUE;
    }
    if (a[1 + n] == L':') {
        *inline_value = a + 2 + n;
        return TRUE;
    }
    return FALSE;
}

static BOOL WINAPI ctrl_handler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        tu_print(L"\n");
        tu_exit(0);
    }
    return FALSE;
}

/* the next character typed, or 0 at the end of the input; wait is in milliseconds, -1 forever, and the result
   is -1 when the wait ran out. */
static int next_char(HANDLE input, BOOL console, LONG wait)
{
    ULONGLONG start = GetTickCount64();
    for (;;) {
        DWORD remaining = INFINITE;
        if (wait >= 0) {
            ULONGLONG elapsed = GetTickCount64() - start;
            if (elapsed >= (ULONGLONG)wait)
                return -1;
            remaining = (DWORD)((ULONGLONG)wait - elapsed);
        }
        if (console) {
            if (WaitForSingleObject(input, remaining) != WAIT_OBJECT_0)
                return -1;
            INPUT_RECORD record;
            DWORD read = 0;
            if (!ReadConsoleInputW(input, &record, 1, &read) || !read)
                return 0;
            if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown && record.Event.KeyEvent.uChar.UnicodeChar)
                return record.Event.KeyEvent.uChar.UnicodeChar;
            continue;
        }
        if (wait >= 0 && GetFileType(input) == FILE_TYPE_PIPE) {
            DWORD available = 0;
            if (PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr) && !available) {
                Sleep(remaining < 20 ? remaining : 20);
                continue;
            }
        }
        BYTE byte;
        DWORD read = 0;
        if (!ReadFile(input, &byte, 1, &read, nullptr) || !read)
            return 0;
        WCHAR wide;
        if (MultiByteToWideChar(GetOEMCP(), 0, (LPCSTR)&byte, 1, &wide, 1) != 1)
            continue;
        return wide;
    }
}

int tool_main(void)
{
    const WCHAR *choices = L"YN", *message = nullptr, *default_text = nullptr, *timeout_text = nullptr;
    BOOL hide = FALSE, case_sensitive = FALSE;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i], *value;
        BOOL has_next = i + 1 < tu_argc;
        if (option_of(a, L"?", &value)) {
            if (tu_argc != 2)
                return fail(L"Invalid syntax.\nType \"CHOICE /?\" for usage.\n");
            tu_puts(kUsage);
            return 0;
        } else if (option_of(a, L"CS", &value) && !value) {
            case_sensitive = TRUE;
        } else if (option_of(a, L"C", &value)) {
            if (!value) {
                if (!has_next)
                    return fail(L"Invalid syntax.\nType \"CHOICE /?\" for usage.\n");
                value = tu_argv[++i];
            }
            if (!*value)
                return fail(L"List of choices cannot be empty.\n");
            choices = value;
        } else if (option_of(a, L"N", &value) && !value) {
            hide = TRUE;
        } else if (option_of(a, L"M", &value)) {
            if (!value) {
                if (!has_next)
                    return fail(L"Invalid syntax.\nType \"CHOICE /?\" for usage.\n");
                value = tu_argv[++i];
            }
            if (wcslen(value) > MAX_TEXT)
                return fail(L"Message cannot be more than 254 characters.\n");
            message = value;
        } else if (option_of(a, L"T", &value)) {
            if (!value) {
                if (!has_next)
                    return fail(L"Invalid syntax.\nType \"CHOICE /?\" for usage.\n");
                value = tu_argv[++i];
            }
            if (!*value)
                return fail(L"The timeout factor cannot be empty.\n");
            timeout_text = value;
        } else if (option_of(a, L"D", &value)) {
            if (!value) {
                if (!has_next)
                    return fail(L"Invalid syntax.\nType \"CHOICE /?\" for usage.\n");
                value = tu_argv[++i];
            }
            if (!*value)
                return fail(L"The default choice cannot be empty.\n");
            default_text = value;
        } else {
            return fail(L"Invalid syntax.\nType \"CHOICE /?\" for usage.\n");
        }
    }
    DWORD timeout = 0;
    if (default_text && !timeout_text)
        return fail(L"Invalid syntax. /D can only be specified when /T is specified.\nType \"CHOICE /?\" for usage.\n");
    if (timeout_text && !default_text)
        return fail(L"Invalid syntax. /T can be specified only when /D is specified.\nType \"CHOICE /?\" for usage.\n");
    if (timeout_text) {
        if (!tu_parse_u32(timeout_text, &timeout))
            return fail(L"The timeout factor is invalid.\n");
        if (timeout > 9999)
            return fail(L"Invalid syntax. Valid range for /T is 0 - 9999.\nType \"CHOICE /?\" for usage.\n");
    }
    SIZE_T count = wcslen(choices);
    for (SIZE_T i = 0; i < count; i++) {
        WCHAR c = choices[i];
        BOOL valid = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
                     (c >= 128 && c <= 254);
        if (!valid)
            return fail(L"Invalid choice. The valid choice characters are: a-z, A-Z, 0-9 and ASCII values of 128 to 254.\n");
        for (SIZE_T j = 0; j < i; j++)
            if (case_sensitive ? choices[j] == c : tu_upper(choices[j]) == tu_upper(c))
                return fail(L"Duplicate choices are not allowed.\n");
    }
    int default_index = -1;
    if (default_text) {
        if (wcslen(default_text) != 1)
            return fail(L"Invalid syntax. /D only accepts single character.\nType \"CHOICE /?\" for usage.\n");
        for (SIZE_T i = 0; i < count; i++)
            if (case_sensitive ? choices[i] == default_text[0] : tu_upper(choices[i]) == tu_upper(default_text[0]))
                default_index = (int)i;
        if (default_index < 0)
            return fail(L"Invalid syntax. Default choice is not found in the specified choices.\nType \"CHOICE /?\" for usage.\n");
    }

    TU_STR prompt = {};
    if (message)
        tu_str_puts(&prompt, message);
    if (!hide) {
        if (message)
            tu_str_putc(&prompt, L' ');
        tu_str_putc(&prompt, L'[');
        for (SIZE_T i = 0; i < count; i++) {
            if (i)
                tu_str_putc(&prompt, L',');
            tu_str_putc(&prompt, case_sensitive ? choices[i] : tu_upper(choices[i]));
        }
        tu_str_puts(&prompt, L"]?");
    }
    tu_write(TU_STDOUT, prompt.data ? prompt.data : L"", prompt.length);
    tu_str_free(&prompt);
    tu_flush();

    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    BOOL console = input && input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &mode);
    if (console) {
        SetConsoleMode(input, (mode & ~(DWORD)ENABLE_LINE_INPUT & ~(DWORD)ENABLE_ECHO_INPUT) | ENABLE_PROCESSED_INPUT);
        FlushConsoleInputBuffer(input);
    }
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    LONG wait = timeout_text ? (LONG)(timeout * 1000) : -1;
    ULONGLONG start = GetTickCount64();
    for (;;) {
        LONG left = wait;
        if (wait >= 0) {
            ULONGLONG elapsed = GetTickCount64() - start;
            left = elapsed >= (ULONGLONG)wait ? 0 : (LONG)((ULONGLONG)wait - elapsed);
        }
        int c = next_char(input, console, left);
        if (c < 0) {
            tu_print(L"%c\n", case_sensitive ? choices[default_index] : tu_upper(choices[default_index]));
            if (console)
                SetConsoleMode(input, mode);
            return default_index + 1;
        }
        if (c == 0) {
            tu_print(L"\n");
            if (console)
                SetConsoleMode(input, mode);
            return 255;
        }
        for (SIZE_T i = 0; i < count; i++) {
            if (case_sensitive ? choices[i] == (WCHAR)c : tu_upper(choices[i]) == tu_upper((WCHAR)c)) {
                tu_print(L"%c\n", case_sensitive ? choices[i] : tu_upper(choices[i]));
                if (console)
                    SetConsoleMode(input, mode);
                return (int)i + 1;
            }
        }
        if (console) {
            tu_print(L"\a");
            tu_flush();
        }
    }
}
