/*
 * reg.exe: the console tool for the registry.
 *
 *   reg query | add | delete | copy | save | restore | load | unload | compare | export | import | flags
 *
 * the exit code is 0 for success and 1 for failure; compare answers 2 when the keys differ.
 */
#include "reg.h"
#include <mime/win32/winnls.h>
#include <stdarg.h>

const WCHAR *const g_operation_names[OP_COUNT] = {L"QUERY",   L"ADD",    L"DELETE", L"COPY",   L"SAVE",   L"RESTORE",
                                                  L"LOAD",    L"UNLOAD", L"COMPARE", L"EXPORT", L"IMPORT", L"FLAGS"};

/* ------------------------------------------------------------------ memory and strings */

void *Allocate(SIZE_T bytes)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes ? bytes : 1);
}

void Release(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

PWSTR DuplicateString(PCWSTR text)
{
    SIZE_T size = (wcslen(text) + 1) * sizeof(WCHAR);
    PWSTR copy = Allocate(size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

void TrimSpaces(PWSTR text)
{
    SIZE_T length = wcslen(text), start = 0;
    while (length > 0 && (text[length - 1] == L' ' || text[length - 1] == L'\t'))
        length--;
    while (start < length && (text[start] == L' ' || text[start] == L'\t'))
        start++;
    memmove(text, text + start, (length - start) * sizeof(WCHAR));
    text[length - start] = 0;
}

BOOL IsSwitch(PCWSTR argument, PCWSTR name)
{
    return (argument[0] == L'/' || argument[0] == L'-') && _wcsicmp(argument + 1, name) == 0;
}

BOOL IsHelp(PCWSTR argument)
{
    return IsSwitch(argument, L"?") || IsSwitch(argument, L"h");
}

BOOL ParseView(PCWSTR argument, DWORD *view)
{
    DWORD wanted;
    if (IsSwitch(argument, L"reg:32"))
        wanted = KEY_WOW64_32KEY;
    else if (IsSwitch(argument, L"reg:64"))
        wanted = KEY_WOW64_64KEY;
    else
        return FALSE;
    if (*view)
        return FALSE;
    *view = wanted;
    return TRUE;
}

/* text where * is any run of characters and ? any one. */
BOOL WildcardMatch(PCWSTR text, PCWSTR pattern, BOOL case_sensitive)
{
    PCWSTR star_pattern = nullptr, star_text = nullptr;
    while (*text) {
        if (*pattern == L'*') {
            star_pattern = ++pattern;
            star_text = text;
        } else if (*pattern == L'?' || (case_sensitive ? *pattern == *text : towupper(*pattern) == towupper(*text))) {
            pattern++;
            text++;
        } else if (star_pattern) {
            pattern = star_pattern;
            text = ++star_text;
        } else {
            return FALSE;
        }
    }
    while (*pattern == L'*')
        pattern++;
    return *pattern == 0;
}

static const WCHAR *const g_type_names[12] = {
    L"REG_NONE", L"REG_SZ", L"REG_EXPAND_SZ", L"REG_BINARY", L"REG_DWORD", L"REG_DWORD_BIG_ENDIAN",
    L"REG_LINK", L"REG_MULTI_SZ", L"REG_RESOURCE_LIST", L"REG_FULL_RESOURCE_DESCRIPTOR",
    L"REG_RESOURCE_REQUIREMENTS_LIST", L"REG_QWORD"};

const WCHAR *TypeName(DWORD type)
{
    return type < 12 ? g_type_names[type] : L"REG_NONE";
}

/* the type a name or a number stands for, -1 when it is neither. */
int ParseTypeName(PCWSTR text)
{
    WCHAR name[40];
    if (wcslen(text) >= RTL_NUMBER_OF(name))
        return -1;
    wcscpy(name, text);
    TrimSpaces(name);
    for (int i = 0; i < 12; i++)
        if (_wcsicmp(name, g_type_names[i]) == 0)
            return i;
    if (_wcsicmp(name, L"REG_DWORD_LITTLE_ENDIAN") == 0)
        return REG_DWORD;
    return -1;
}

BOOL ParseNumber(PCWSTR text, int base, ULONGLONG *value, BOOL *overflow, PCWSTR *end, BOOL allow_minus)
{
    BOOL negative = FALSE, any = FALSE;
    ULONGLONG result = 0;
    *overflow = FALSE;
    while (*text == L' ' || *text == L'\t')
        text++;
    if (allow_minus && *text == L'-') {
        negative = TRUE;
        text++;
    }
    if (base == 16 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X'))
        text += 2;
    for (;; text++) {
        int digit;
        if (*text >= L'0' && *text <= L'9')
            digit = *text - L'0';
        else if (*text >= L'a' && *text <= L'z')
            digit = *text - L'a' + 10;
        else if (*text >= L'A' && *text <= L'Z')
            digit = *text - L'A' + 10;
        else
            break;
        if (digit >= base)
            break;
        if (result > (0xFFFFFFFFFFFFFFFFull - (ULONGLONG)digit) / (ULONGLONG)base)
            *overflow = TRUE;
        result = result * (ULONGLONG)base + (ULONGLONG)digit;
        any = TRUE;
    }
    *value = negative ? (ULONGLONG)0 - result : result;
    *end = text;
    return any;
}

PWSTR FindAny(PCWSTR text, PCWSTR characters)
{
    for (; *text; text++)
        for (PCWSTR c = characters; *c; c++)
            if (*text == *c)
                return (PWSTR)text;
    return nullptr;
}

/* ------------------------------------------------------------------ output */

static void write_stream(DWORD which, PCWSTR text, SIZE_T length)
{
    HANDLE handle = GetStdHandle(which);
    DWORD mode, written;
    if (handle == INVALID_HANDLE_VALUE || !handle)
        return;
    if (GetConsoleMode(handle, &mode)) {
        WriteConsoleW(handle, text, (DWORD)length, &written, nullptr);
        return;
    }
    /* a file or a pipe gets line ends of two characters, as the text mode of the C library makes them, in the code page
       of the console. */
    SIZE_T lines = 0;
    for (SIZE_T i = 0; i < length; i++)
        lines += text[i] == L'\n';
    PWSTR wide = HeapAlloc(GetProcessHeap(), 0, (length + lines + 1) * sizeof(WCHAR));
    if (!wide)
        return;
    SIZE_T n = 0;
    for (SIZE_T i = 0; i < length; i++) {
        if (text[i] == L'\n')
            wide[n++] = L'\r';
        wide[n++] = text[i];
    }
    UINT page = GetConsoleOutputCP();
    int bytes = WideCharToMultiByte(page, 0, wide, (int)n, nullptr, 0, nullptr, nullptr);
    char *narrow = bytes > 0 ? HeapAlloc(GetProcessHeap(), 0, (SIZE_T)bytes) : nullptr;
    if (narrow) {
        WideCharToMultiByte(page, 0, wide, (int)n, narrow, bytes, nullptr, nullptr);
        WriteFile(handle, narrow, (DWORD)bytes, &written, nullptr);
        HeapFree(GetProcessHeap(), 0, narrow);
    }
    HeapFree(GetProcessHeap(), 0, wide);
}

static void print_to(DWORD which, PCWSTR format, va_list arguments)
{
    WCHAR stack[1024];
    PWSTR text = stack;
    int n = _vsnwprintf(stack, RTL_NUMBER_OF(stack) - 1, format, arguments);
    if (n < 0 || n >= (int)RTL_NUMBER_OF(stack) - 1) {
        /* a longer text: keys and values can be long. */
        SIZE_T capacity = 1 << 16;
        PWSTR big = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(WCHAR));
        if (!big)
            return;
        n = _vsnwprintf(big, capacity - 1, format, arguments);
        if (n < 0)
            n = (int)capacity - 1;
        text = big;
        text[n] = 0;
        write_stream(which, text, (SIZE_T)n);
        HeapFree(GetProcessHeap(), 0, big);
        return;
    }
    text[n] = 0;
    write_stream(which, text, (SIZE_T)n);
}

void Out(PCWSTR format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    print_to(STD_OUTPUT_HANDLE, format, arguments);
    va_end(arguments);
}

void Err(PCWSTR format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    print_to(STD_ERROR_HANDLE, format, arguments);
    va_end(arguments);
}

void OutText(PCWSTR text)
{
    write_stream(STD_OUTPUT_HANDLE, text, wcslen(text));
}

/* ------------------------------------------------------------------ errors */

static void error_text(DWORD error, PWSTR text, DWORD capacity)
{
    if (error == ERROR_SUCCESS) {
        wcscpy(text, L"The operation completed successfully.");
        return;
    }
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        wcscpy(text, L"The system was unable to find the specified registry key or value.");
        return;
    }
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, text,
                             capacity, nullptr);
    while (n && (text[n - 1] == L'\n' || text[n - 1] == L'\r' || text[n - 1] == L' '))
        n--;
    text[n] = 0;
    if (!n)
        _snwprintf(text, capacity, L"Unknown error 0x%x.", error);
}

int ReportError(DWORD error, int operation)
{
    WCHAR text[512];
    error_text(error, text, RTL_NUMBER_OF(text));
    Err(L"ERROR: %s\n", text);
    return 1;
}

void ReportPlain(DWORD error, int stream)
{
    WCHAR text[512];
    if (error == ERROR_CANCELLED)
        wcscpy(text, L"The operation was canceled by the user.");
    else
        error_text(error, text, RTL_NUMBER_OF(text));
    if (stream == 1)
        Out(L"%s\n", text);
    else
        Err(L"%s\n", text);
}

void ReportSuccess(void)
{
    ReportPlain(ERROR_SUCCESS, 1);
}

int SyntaxError(int operation)
{
    Err(L"ERROR: Invalid syntax.\nType \"REG %s /?\" for usage.\n", g_operation_names[operation]);
    return 1;
}

int SyntaxErrorGeneric(void)
{
    Err(L"ERROR: Invalid syntax.\nType \"REG /?\" for usage.\n");
    return 1;
}

/* ------------------------------------------------------------------ questions */

static WCHAR g_line[256];
static SIZE_T g_line_length, g_line_position;
static BOOL g_stdin_ended;

/* the next character typed or piped; the console gives a line at a time. */
static BOOL read_character(WCHAR *c)
{
    if (g_line_position < g_line_length) {
        *c = g_line[g_line_position++];
        return TRUE;
    }
    if (g_stdin_ended)
        return FALSE;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode, read;
    g_line_length = g_line_position = 0;
    if (GetConsoleMode(in, &mode)) {
        if (!ReadConsoleW(in, g_line, RTL_NUMBER_OF(g_line), &read, nullptr) || read == 0) {
            g_stdin_ended = TRUE;
            return FALSE;
        }
        g_line_length = read;
    } else {
        char bytes[128];
        if (!ReadFile(in, bytes, 1, &read, nullptr) || read == 0) {
            g_stdin_ended = TRUE;
            return FALSE;
        }
        MultiByteToWideChar(GetConsoleCP(), 0, bytes, 1, g_line, 1);
        g_line_length = 1;
    }
    *c = g_line[g_line_position++];
    return TRUE;
}

int Prompt(PCWSTR format, PCWSTR argument, PCWSTR letters, BOOL force)
{
    if (force)
        return 1;
    for (;;) {
        Out(format, argument);
        WCHAR c;
        if (!read_character(&c))
            return 2; /* the end of the input is a refusal, the original asks again for ever. */
        for (int i = 0; letters[i]; i++)
            if (towupper(c) == towupper(letters[i]))
                return i + 1;
    }
}

/* ------------------------------------------------------------------ key names */

static BOOL root_by_name(PCWSTR name, SIZE_T length, int *root)
{
    static const struct {
        const WCHAR *Short;
        int Root;
    } shorts[] = {{L"HKCU", MIME_REG_ROOT_USER},     {L"HKCR", MIME_REG_ROOT_CLASSES},
                  {L"HKCC", MIME_REG_ROOT_CONFIG},   {L"HKLM", MIME_REG_ROOT_MACHINE},
                  {L"HKU", MIME_REG_ROOT_USERS}};
    for (int i = 0; i < 5; i++) {
        if (wcslen(shorts[i].Short) == length && _wcsnicmp(name, shorts[i].Short, length) == 0) {
            *root = shorts[i].Root;
            return TRUE;
        }
    }
    int found = MimeRegSysFindRoot(name, length);
    if (found < 0)
        return FALSE;
    *root = found;
    return TRUE;
}

/* every part of a subkey is not empty and shorter than 255 characters. */
static BOOL valid_subkey(PCWSTR subkey)
{
    SIZE_T length = wcslen(subkey);
    if (!length)
        return FALSE;
    SIZE_T part = 0;
    for (SIZE_T i = 0; i <= length; i++) {
        if (i == length || subkey[i] == L'\\') {
            if (part == 0 || part > 254)
                return FALSE;
            part = 0;
        } else {
            part++;
        }
    }
    return TRUE;
}

static int key_error(int operation, int message)
{
    switch (message) {
    case 105:
        Err(L"ERROR: A remote machine was specified, the root key must be HKLM or HKU.\nType \"REG %s /?\" for usage.\n",
            g_operation_names[operation]);
        break;
    case 106:
        Err(L"ERROR: The root key must be HKLM or HKU for LOAD/UNLOAD.\nType \"REG %s /?\" for usage.\n",
            g_operation_names[operation]);
        break;
    default:
        Err(L"ERROR: Invalid key name.\nType \"REG %s /?\" for usage.\n", g_operation_names[operation]);
        break;
    }
    return 1;
}

/* a key as the command line writes it: \\machine\ROOT\subkey, the machine and the subkey being optional. */
BOOL ParseKeyArgument(PCWSTR argument, int operation, TARGET *target)
{
    memset(target, 0, sizeof(*target));
    PWSTR text = DuplicateString(argument);
    if (!text)
        return FALSE;
    TrimSpaces(text);
    PWSTR rest = text;
    if (wcslen(text) > 2 && text[0] == L'\\' && text[1] == L'\\') {
        PWSTR slash = wcschr(text + 2, L'\\');
        if (slash) {
            *slash = 0;
            rest = slash + 1;
        } else {
            rest = nullptr;
        }
        target->Machine = DuplicateString(text);
        target->Remote = _wcsicmp(text, L"\\\\.") != 0;
        if (!rest) {
            key_error(operation, 104);
            Release(text);
            return FALSE;
        }
    }
    if (!*rest) {
        key_error(operation, 104);
        Release(text);
        return FALSE;
    }
    PWSTR slash = wcschr(rest, L'\\');
    PWSTR subkey = nullptr;
    if (slash) {
        *slash = 0;
        subkey = slash + 1;
    }
    PWSTR root_name = rest;
    if (*root_name == L'"')
        root_name++;
    int root;
    if (!root_by_name(root_name, wcslen(root_name), &root)) {
        key_error(operation, 104);
        Release(text);
        return FALSE;
    }
    BOOL machine_or_users = root == MIME_REG_ROOT_MACHINE || root == MIME_REG_ROOT_USERS;
    if (!machine_or_users) {
        if (target->Remote) {
            key_error(operation, 105);
            Release(text);
            return FALSE;
        }
        if (operation == OP_LOAD || operation == OP_UNLOAD) {
            key_error(operation, 106);
            Release(text);
            return FALSE;
        }
    }
    target->Root = root;
    target->RootKey = MimeRegSysRootKey(root);
    if (subkey) {
        SIZE_T length = wcslen(subkey);
        if (length > 1 && subkey[length - 1] == L'\\')
            subkey[length - 1] = 0;
        if (!valid_subkey(subkey)) {
            key_error(operation, 104);
            Release(text);
            return FALSE;
        }
        target->Subkey = DuplicateString(subkey);
    } else {
        target->Subkey = DuplicateString(L"");
    }
    const WCHAR *root_full = MimeRegSysRootName(root);
    SIZE_T size = wcslen(root_full) + 1 + wcslen(target->Subkey) + 1;
    target->Full = Allocate(size * sizeof(WCHAR));
    wcscpy(target->Full, root_full);
    if (*target->Subkey) {
        wcscat(target->Full, L"\\");
        wcscat(target->Full, target->Subkey);
    }
    Release(text);
    return TRUE;
}

void FreeTarget(TARGET *target)
{
    ClosePredefined(target);
    Release(target->Machine);
    Release(target->Subkey);
    Release(target->Full);
    memset(target, 0, sizeof(*target));
}

/* the root of a machine other than this one is a connection of its own. */
BOOL ConnectTarget(TARGET *target, DWORD *error)
{
    *error = ERROR_SUCCESS;
    if (!target->Remote)
        return TRUE;
    HKEY connected;
    LSTATUS status = RegConnectRegistryW(target->Machine, target->RootKey, &connected);
    if (status != ERROR_SUCCESS) {
        *error = (DWORD)status;
        return FALSE;
    }
    target->RootKey = connected;
    target->Connected = TRUE;
    return TRUE;
}

void ClosePredefined(TARGET *target)
{
    if (target->Connected && target->RootKey)
        RegCloseKey(target->RootKey);
    target->Connected = FALSE;
}

/* ------------------------------------------------------------------ the command line */

/* the words as the C library of Windows makes them: quotes, and backslashes before quotes. */
static PWSTR *split_command_line(PCWSTR line, int *count)
{
    SIZE_T length = wcslen(line);
    PWSTR copy = Allocate((length + 2) * sizeof(WCHAR));
    PWSTR *items = Allocate((length / 2 + 2) * sizeof(PWSTR));
    if (!copy || !items)
        return nullptr;
    PWSTR out = copy;
    int n = 0;
    const WCHAR *p = line;
    /* the program name has no escapes. */
    items[n++] = out;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"')
            *out++ = *p++;
        if (*p)
            p++;
    } else {
        while (*p && *p != L' ' && *p != L'\t')
            *out++ = *p++;
    }
    *out++ = 0;
    for (;;) {
        while (*p == L' ' || *p == L'\t')
            p++;
        if (!*p)
            break;
        items[n++] = out;
        BOOL quoted = FALSE;
        while (*p && (quoted || (*p != L' ' && *p != L'\t'))) {
            int backslashes = 0;
            while (*p == L'\\') {
                backslashes++;
                p++;
            }
            if (*p == L'"') {
                for (int i = 0; i < backslashes / 2; i++)
                    *out++ = L'\\';
                if (backslashes & 1) {
                    *out++ = L'"';
                } else {
                    quoted = !quoted;
                }
                p++;
            } else {
                for (int i = 0; i < backslashes; i++)
                    *out++ = L'\\';
                if (*p && (quoted || (*p != L' ' && *p != L'\t')))
                    *out++ = *p++;
            }
        }
        *out++ = 0;
    }
    *count = n;
    return items;
}

static BOOL tools_disabled(void)
{
    HKEY key;
    BOOL disabled = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD type, size = 4, value = 0;
        if (RegQueryValueExW(key, L"DisableRegistryTools", nullptr, &type, (BYTE *)&value, &size) == ERROR_SUCCESS &&
            type == REG_DWORD && size == 4)
            disabled = value != 0;
        RegCloseKey(key);
    }
    return disabled;
}

static int run(const ARGS *args)
{
    if (args->Count < 2)
        return SyntaxErrorGeneric();
    int operation = -1;
    for (int i = 0; i < OP_COUNT; i++) {
        if (_wcsicmp(args->Items[1], g_operation_names[i]) == 0) {
            operation = i;
            break;
        }
    }
    if (operation < 0) {
        if (IsHelp(args->Items[1])) {
            Usage(-1);
            return 0;
        }
        Err(L"ERROR: Invalid Argument/Option - '%s'.\nType \"REG /?\" for usage.\n", args->Items[1]);
        return 1;
    }
    if (args->Count >= 3 && tools_disabled() && !IsHelp(args->Items[2])) {
        Err(L"ERROR: Registry editing has been disabled by your administrator.\n");
        return 1;
    }
    switch (operation) {
    case OP_QUERY:
        return DoQuery(args);
    case OP_ADD:
        return DoAdd(args);
    case OP_DELETE:
        return DoDelete(args);
    case OP_COPY:
        return DoCopy(args);
    case OP_SAVE:
        return DoSave(args);
    case OP_RESTORE:
        return DoRestore(args);
    case OP_LOAD:
        return DoLoad(args);
    case OP_UNLOAD:
        return DoUnload(args);
    case OP_COMPARE:
        return DoCompare(args);
    case OP_EXPORT:
        return DoExport(args);
    case OP_IMPORT:
        return DoImport(args);
    default:
        return DoFlags(args);
    }
}

void entry(void)
{
    ARGS args;
    args.Items = split_command_line(GetCommandLineW(), &args.Count);
    if (!args.Items)
        ExitProcess(1);
    ExitProcess((UINT)run(&args));
}
