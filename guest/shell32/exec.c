/*
 * launching documents and programs, and command line splitting.
 *
 * ShellExecute starts executables with CreateProcess and other files through their type's open command. there is
 * no explorer yet, so folders and urls without a registered handler fail with SE_ERR_NOASSOC, as windows does for a
 * type nobody handles.
 */
#include "shell32_private.h"

/* ShellExecute's success value, as on windows. */
#define SE_SUCCESS 42

static HINSTANCE se_error(DWORD error)
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
        return (HINSTANCE)SE_ERR_FNF;
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
        return (HINSTANCE)SE_ERR_PNF;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return (HINSTANCE)SE_ERR_OOM;
    case ERROR_BAD_FORMAT:
    case ERROR_BAD_EXE_FORMAT:
        return (HINSTANCE)ERROR_BAD_FORMAT;
    case ERROR_NO_ASSOCIATION:
        return (HINSTANCE)SE_ERR_NOASSOC;
    case ERROR_SHARING_VIOLATION:
        return (HINSTANCE)SE_ERR_SHARE;
    case ERROR_DLL_NOT_FOUND:
        return (HINSTANCE)SE_ERR_DLLNOTFOUND;
    default:
        return (HINSTANCE)SE_ERR_ACCESSDENIED;
    }
}

static BOOL has_extension(LPCWSTR path, LPCWSTR extension)
{
    LPCWSTR dot = wcsrchr(path, L'.');
    return dot && !wcschr(dot, L'\\') && !_wcsicmp(dot, extension);
}

/* a scheme of two letters or more, so "c:" stays a drive. */
static BOOL is_url(LPCWSTR file, WCHAR *scheme, SIZE_T chars)
{
    LPCWSTR colon = wcschr(file, L':');
    if (!colon || colon - file < 2 || (SIZE_T)(colon - file) >= chars)
        return FALSE;
    for (LPCWSTR p = file; p < colon; p++)
        if (!((*p | 0x20) >= L'a' && (*p | 0x20) <= L'z') && !(*p >= L'0' && *p <= L'9') && *p != L'+' &&
            *p != L'-' && *p != L'.')
            return FALSE;
    memcpy(scheme, file, (SIZE_T)(colon - file) * sizeof(WCHAR));
    scheme[colon - file] = 0;
    return TRUE;
}

static BOOL read_default(LPCWSTR subkey, LPWSTR out, DWORD chars)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, subkey, 0, KEY_READ, &key))
        return FALSE;
    WCHAR raw[1024];
    DWORD type, bytes = sizeof(raw) - sizeof(WCHAR);
    LSTATUS status = RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)raw, &bytes);
    RegCloseKey(key);
    if (status || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < 2 * sizeof(WCHAR))
        return FALSE;
    raw[bytes / sizeof(WCHAR)] = 0;
    if (type == REG_EXPAND_SZ) {
        DWORD n = ExpandEnvironmentStringsW(raw, out, chars);
        return n && n <= chars;
    }
    if (wcslen(raw) >= chars)
        return FALSE;
    wcscpy(out, raw);
    return TRUE;
}

/* type is a key such as "txtfile" or "http". */
static BOOL verb_command(LPCWSTR type, LPCWSTR verb, LPWSTR command, DWORD chars)
{
    WCHAR subkey[MAX_PATH];
    if (wcslen(type) + wcslen(verb) + 16 >= MAX_PATH)
        return FALSE;
    _snwprintf(subkey, MAX_PATH, L"%ls\\shell\\%ls\\command", type, verb);
    return read_default(subkey, command, chars);
}

/* expands %1, %L, %* and %% of an association command. */
static LPWSTR build_command(LPCWSTR template, LPCWSTR file, LPCWSTR parameters)
{
    SIZE_T size = wcslen(template) + 1, file_length = wcslen(file), parameter_length = wcslen(parameters);
    size += 4 * (file_length + parameter_length) + 4;
    LPWSTR command = ShAlloc(size * sizeof(WCHAR));
    if (!command)
        return nullptr;
    LPWSTR d = command;
    BOOL used_file = FALSE;
    for (LPCWSTR p = template; *p; p++) {
        if (*p != L'%' || !p[1]) {
            *d++ = *p;
            continue;
        }
        WCHAR c = *++p;
        if (c == L'1' || c == L'l' || c == L'L') {
            memcpy(d, file, file_length * sizeof(WCHAR));
            d += file_length;
            used_file = TRUE;
        } else if (c == L'*') {
            memcpy(d, parameters, parameter_length * sizeof(WCHAR));
            d += parameter_length;
        } else if (c == L'%') {
            *d++ = L'%';
        }
    }
    if (!used_file) {
        *d++ = L' ';
        *d++ = L'"';
        memcpy(d, file, file_length * sizeof(WCHAR));
        d += file_length;
        *d++ = L'"';
    }
    *d = 0;
    return command;
}

/*
 * elevated processes come from the services.exe broker through advapi32. it never prompts: the machine has no
 * consent interface and its policy is to approve.
 */
typedef DWORD(WINAPI *LAUNCH_ELEVATED)(LPCWSTR application, LPCWSTR command, LPCWSTR directory, DWORD flags,
                                       const STARTUPINFOW *startup, PROCESS_INFORMATION *info, DWORD *kind);

static BOOL run_elevated(LPCWSTR application, LPWSTR command, LPCWSTR directory, STARTUPINFOW *startup,
                         PROCESS_INFORMATION *process)
{
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    LAUNCH_ELEVATED launch = advapi32 ? (LAUNCH_ELEVATED)GetProcAddress(advapi32, "LaunchElevated") : nullptr;
    if (!launch) {
        SetLastError(ERROR_ELEVATION_REQUIRED);
        return FALSE;
    }
    DWORD error = launch(application, command, directory, 0, startup, process, nullptr);
    if (error)
        SetLastError(error);
    return !error;
}

/*
 * runs with this process's token, or elevated for runas. like on windows, a program whose manifest needs more than
 * the token has fails CreateProcess with ERROR_ELEVATION_REQUIRED and is then started through the broker.
 */
static BOOL run(LPCWSTR application, LPWSTR command, LPCWSTR directory, int show, SHELLEXECUTEINFOW *info, BOOL elevate)
{
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = (WORD)show;
    BOOL started = !elevate && CreateProcessW(application, command, nullptr, nullptr, FALSE, 0, nullptr, directory,
                                              &startup, &process);
    if (!started && (elevate || GetLastError() == ERROR_ELEVATION_REQUIRED))
        started = run_elevated(application, command, directory, &startup, &process);
    if (!started)
        return FALSE;
    CloseHandle(process.hThread);
    if (info->fMask & SEE_MASK_NOCLOSEPROCESS)
        info->hProcess = process.hProcess;
    else
        CloseHandle(process.hProcess);
    return TRUE;
}

static BOOL fail(SHELLEXECUTEINFOW *info, DWORD error)
{
    info->hInstApp = se_error(error);
    SetLastError(error);
    return FALSE;
}

static BOOL run_association(SHELLEXECUTEINFOW *info, LPCWSTR type, LPCWSTR verb, LPCWSTR target,
                            LPCWSTR parameters)
{
    WCHAR template[1024];
    if (!verb_command(type, verb, template, RTL_NUMBER_OF(template)))
        return fail(info, ERROR_NO_ASSOCIATION);
    LPWSTR command = build_command(template, target, parameters);
    if (!command)
        return fail(info, ERROR_OUTOFMEMORY);
    BOOL ok = run(nullptr, command, info->lpDirectory, info->nShow, info, !_wcsicmp(verb, L"runas"));
    DWORD error = GetLastError();
    ShFree(command);
    if (!ok)
        return fail(info, error);
    info->hInstApp = (HINSTANCE)SE_SUCCESS;
    return TRUE;
}

SHSTDAPI BOOL WINAPI ShellExecuteExW(SHELLEXECUTEINFOW *info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    info->hInstApp = nullptr;
    info->hProcess = nullptr;
    WCHAR target[MAX_PATH], scheme[32];
    if ((info->fMask & SEE_MASK_IDLIST) && info->lpIDList && (!info->lpFile || !info->lpFile[0])) {
        if (!ShPathFromIdList(info->lpIDList, target, MAX_PATH))
            return fail(info, ERROR_FILE_NOT_FOUND);
    } else if (!info->lpFile || !info->lpFile[0]) {
        return fail(info, ERROR_FILE_NOT_FOUND);
    } else if (wcslen(info->lpFile) >= MAX_PATH) {
        return fail(info, ERROR_INVALID_NAME);
    } else {
        wcscpy(target, info->lpFile);
    }
    SIZE_T target_length = wcslen(target);
    if (target_length >= 2 && target[0] == L'"' && target[target_length - 1] == L'"') {
        memmove(target, target + 1, (target_length - 2) * sizeof(WCHAR));
        target[target_length - 2] = 0;
    }
    LPCWSTR verb = info->lpVerb && info->lpVerb[0] ? info->lpVerb : L"open";
    LPCWSTR parameters = info->lpParameters ? info->lpParameters : L"";

    if (is_url(target, scheme, RTL_NUMBER_OF(scheme)))
        return run_association(info, scheme, verb, target, parameters);

    /* a file: relative to the given directory, else searched like a program name. */
    WCHAR path[MAX_PATH];
    DWORD attributes = INVALID_FILE_ATTRIBUTES;
    BOOL relative = target[0] != L'\\' && target[1] != L':';
    if (info->lpDirectory && info->lpDirectory[0] && relative &&
        wcslen(info->lpDirectory) + wcslen(target) + 2 < MAX_PATH) {
        _snwprintf(path, MAX_PATH, L"%ls\\%ls", info->lpDirectory, target);
        path[MAX_PATH - 1] = 0;
        attributes = GetFileAttributesW(path);
    }
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD n = GetFullPathNameW(target, MAX_PATH, path, nullptr);
        attributes = n && n < MAX_PATH ? GetFileAttributesW(path) : INVALID_FILE_ATTRIBUTES;
    }
    if (attributes == INVALID_FILE_ATTRIBUTES && !wcschr(target, L'\\')) {
        DWORD n = SearchPathW(nullptr, target, L".exe", MAX_PATH, path, nullptr);
        if (n && n < MAX_PATH)
            attributes = GetFileAttributesW(path);
    }
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return fail(info, error == ERROR_PATH_NOT_FOUND ? error : ERROR_FILE_NOT_FOUND);
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        return run_association(info, L"Folder", verb, path, parameters);

    if (has_extension(path, L".exe") || has_extension(path, L".com")) {
        if (_wcsicmp(verb, L"open") && _wcsicmp(verb, L"runas"))
            return fail(info, ERROR_NO_ASSOCIATION);
        SIZE_T size = wcslen(path) + wcslen(parameters) + 4;
        LPWSTR command = ShAlloc(size * sizeof(WCHAR));
        if (!command)
            return fail(info, ERROR_OUTOFMEMORY);
        _snwprintf(command, size, parameters[0] ? L"\"%ls\" %ls" : L"\"%ls\"", path, parameters);
        command[size - 1] = 0;
        BOOL ok = run(path, command, info->lpDirectory, info->nShow, info, !_wcsicmp(verb, L"runas"));
        DWORD error = GetLastError();
        ShFree(command);
        if (!ok)
            return fail(info, error);
        info->hInstApp = (HINSTANCE)SE_SUCCESS;
        return TRUE;
    }

    /* the extension's type, then its verb's command. */
    WCHAR type[MAX_PATH];
    LPCWSTR dot = wcsrchr(path, L'.');
    if (!dot || wcschr(dot, L'\\') || !read_default(dot, type, RTL_NUMBER_OF(type)))
        return fail(info, ERROR_NO_ASSOCIATION);
    return run_association(info, type, verb, path, parameters);
}

/* the file itself for programs, else its type's open command. */
SHSTDAPI HINSTANCE WINAPI FindExecutableW(LPCWSTR file, LPCWSTR directory, LPWSTR result)
{
    if (!result)
        return (HINSTANCE)SE_ERR_FNF;
    result[0] = 0;
    if (!file || !file[0] || wcslen(file) >= MAX_PATH)
        return (HINSTANCE)SE_ERR_FNF;
    WCHAR path[MAX_PATH];
    DWORD attributes = INVALID_FILE_ATTRIBUTES;
    BOOL relative = file[0] != L'\\' && file[1] != L':';
    if (directory && directory[0] && relative && wcslen(directory) + wcslen(file) + 2 < MAX_PATH) {
        _snwprintf(path, MAX_PATH, L"%ls\\%ls", directory, file);
        path[MAX_PATH - 1] = 0;
        attributes = GetFileAttributesW(path);
    }
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD n = SearchPathW(nullptr, file, nullptr, MAX_PATH, path, nullptr);
        attributes = n && n < MAX_PATH ? GetFileAttributesW(path) : INVALID_FILE_ATTRIBUTES;
    }
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return (HINSTANCE)SE_ERR_FNF;
    if (has_extension(path, L".exe") || has_extension(path, L".com") || has_extension(path, L".bat") ||
        has_extension(path, L".cmd")) {
        wcscpy(result, path);
        return (HINSTANCE)33;
    }
    WCHAR type[MAX_PATH], command[1024];
    LPCWSTR dot = wcsrchr(path, L'.');
    if (!dot || wcschr(dot, L'\\') || !read_default(dot, type, RTL_NUMBER_OF(type)) ||
        !verb_command(type, L"open", command, RTL_NUMBER_OF(command)))
        return (HINSTANCE)SE_ERR_NOASSOC;
    /* the program is the quoted first word, or everything before the first space. */
    LPCWSTR start = command, end;
    if (*start == L'"') {
        start++;
        end = wcschr(start, L'"');
        if (!end)
            end = start + wcslen(start);
    } else {
        end = wcschr(start, L' ');
        if (!end)
            end = start + wcslen(start);
    }
    if (end == start || end - start >= MAX_PATH)
        return (HINSTANCE)SE_ERR_NOASSOC;
    memcpy(result, start, (SIZE_T)(end - start) * sizeof(WCHAR));
    result[end - start] = 0;
    return (HINSTANCE)33;
}

SHSTDAPI HINSTANCE WINAPI FindExecutableA(LPCSTR file, LPCSTR directory, LPSTR result)
{
    if (!result)
        return (HINSTANCE)SE_ERR_FNF;
    result[0] = 0;
    LPWSTR f = ShWiden(file), d = ShWiden(directory);
    WCHAR wide[MAX_PATH];
    HINSTANCE r = (HINSTANCE)SE_ERR_OOM;
    if ((!file || f) && (!directory || d)) {
        r = FindExecutableW(f, d, wide);
        if ((INT_PTR)r > 32)
            WideCharToMultiByte(CP_ACP, 0, wide, -1, result, MAX_PATH, nullptr, nullptr);
    }
    ShFree(f);
    ShFree(d);
    return r;
}

SHSTDAPI HINSTANCE WINAPI ShellExecuteW(HWND hwnd, LPCWSTR verb, LPCWSTR file, LPCWSTR parameters,
                                        LPCWSTR directory, INT show)
{
    SHELLEXECUTEINFOW info = {sizeof(info)};
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.hwnd = hwnd;
    info.lpVerb = verb;
    info.lpFile = file;
    info.lpParameters = parameters;
    info.lpDirectory = directory;
    info.nShow = show;
    ShellExecuteExW(&info);
    return info.hInstApp;
}

SHSTDAPI HINSTANCE WINAPI ShellExecuteA(HWND hwnd, LPCSTR verb, LPCSTR file, LPCSTR parameters, LPCSTR directory,
                                        INT show)
{
    LPWSTR w_verb = ShWiden(verb), w_file = ShWiden(file), w_parameters = ShWiden(parameters),
           w_directory = ShWiden(directory);
    HINSTANCE result = ShellExecuteW(hwnd, w_verb, w_file, w_parameters, w_directory, show);
    ShFree(w_verb);
    ShFree(w_file);
    ShFree(w_parameters);
    ShFree(w_directory);
    return result;
}

/* ------------------------------------------------------------------ CommandLineToArgvW */

/*
 * splits a command line the way windows does. the program name ends at the
 * closing quote or the first blank, with no escapes. in the arguments, 2n
 * backslashes before a quote give n backslashes and the quote toggles the
 * quoted state, 2n+1 give n backslashes and a literal quote, and other
 * backslashes are literal. in a run of quotes every third one counts as a
 * literal quote, so "" inside quotes gives a quote and leaves the quotes.
 * with argv NULL it only counts the arguments and characters.
 */
static int split(LPCWSTR line, LPWSTR *argv, LPWSTR text, SIZE_T *chars)
{
    int argc = 0;
    SIZE_T used = 0;
#define EMIT(c)                                                                                                   \
    do {                                                                                                          \
        if (text)                                                                                                 \
            text[used] = (c);                                                                                     \
        used++;                                                                                                   \
    } while (0)
    LPCWSTR p = line;
    if (argv)
        argv[argc] = text + used;
    argc++;
    if (*p == L'"') {
        for (p++; *p && *p != L'"'; p++)
            EMIT(*p);
        if (*p)
            p++;
    } else {
        for (; *p && *p != L' ' && *p != L'\t'; p++)
            EMIT(*p);
    }
    EMIT(0);
    while (*p == L' ' || *p == L'\t')
        p++;
    int quotes = 0;
    BOOL in_argument = FALSE;
    while (*p) {
        if ((*p == L' ' || *p == L'\t') && !quotes) {
            if (in_argument)
                EMIT(0);
            in_argument = FALSE;
            p++;
            continue;
        }
        if (!in_argument) {
            if (argv)
                argv[argc] = text + used;
            argc++;
            in_argument = TRUE;
        }
        if (*p == L'\\') {
            SIZE_T run = 0;
            while (p[run] == L'\\')
                run++;
            if (p[run] == L'"') {
                for (SIZE_T i = 0; i < run / 2; i++)
                    EMIT(L'\\');
                p += run;
                if (run & 1) {
                    EMIT(L'"');
                    p++;
                }
            } else {
                for (SIZE_T i = 0; i < run; i++)
                    EMIT(L'\\');
                p += run;
            }
        } else if (*p == L'"') {
            for (; *p == L'"'; p++) {
                if (++quotes == 3) {
                    EMIT(L'"');
                    quotes = 0;
                }
            }
            if (quotes == 2)
                quotes = 0;
        } else {
            EMIT(*p);
            p++;
        }
    }
    if (in_argument)
        EMIT(0);
#undef EMIT
    *chars = used;
    return argc;
}

SHSTDAPI LPWSTR *WINAPI CommandLineToArgvW(LPCWSTR command_line, int *count)
{
    if (!count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    WCHAR module[MAX_PATH];
    if (!command_line || !command_line[0]) {
        DWORD n = GetModuleFileNameW(nullptr, module, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return nullptr;
        /* the module path as a quoted program name, so its blanks do not split it. */
        WCHAR quoted[MAX_PATH + 2];
        _snwprintf(quoted, RTL_NUMBER_OF(quoted), L"\"%ls\"", module);
        quoted[RTL_NUMBER_OF(quoted) - 1] = 0;
        return CommandLineToArgvW(quoted, count);
    }
    SIZE_T chars;
    int argc = split(command_line, nullptr, nullptr, &chars);
    SIZE_T pointers = (SIZE_T)(argc + 1) * sizeof(LPWSTR);
    LPWSTR *argv = LocalAlloc(LMEM_FIXED, pointers + chars * sizeof(WCHAR));
    if (!argv) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    split(command_line, argv, (LPWSTR)((BYTE *)argv + pointers), &chars);
    argv[argc] = nullptr;
    *count = argc;
    return argv;
}
