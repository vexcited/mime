/*
 * rundll32.exe: calls a function of a library with the signature of a control panel or installer entry.
 *
 *   rundll32 [/sta] library,entry arguments
 *
 * the entry is a CALLBACK function that gets a stub window, the instance of rundll32, the arguments as wide or ansi text
 * and the show command.
 * the program always ends with exit code 0, whatever happened.
 */
#include <mime/win32/ole2.h>
#include <mime/win32/winuser.h>
#include <mime/ntapi.h>
#include <rtl.h>

#define SWITCH_STA 1
#define SWITCH_LOCALSERVER 2

#define MESSAGE_MEMORY L"There is not enough memory to run the file %s.\n\nPlease close other windows and try again."
#define MESSAGE_MISSING_ENTRY L"Error in %s\nMissing entry: %s"
#define MESSAGE_LOAD L"There was a problem starting %s\n\n%s"
#define MESSAGE_ILLEGAL L"Error in %s\nIllegal value: %s"
#define MESSAGE_TITLE L"RunDLL"

#define ERROR_BAD_EXE_FORMAT_ 193

#define TOKEN_QUERY_ 0x0008
#define TOKEN_ADJUST_DEFAULT_ 0x0080
#define TOKEN_ELEVATION_TYPE_CLASS 18
#define TOKEN_ELEVATION_CLASS 20
#define TOKEN_VIRTUALIZATION_ENABLED_CLASS 24
#define ELEVATION_TYPE_DEFAULT 1
#define ELEVATION_TYPE_FULL 2
#define RUN_LEVEL_CLASS 5
#define RUN_LEVEL_AS_INVOKER 1

typedef void(CALLBACK *ENTRY)(HWND window, HINSTANCE instance, PCWSTR arguments, int show);

static HINSTANCE g_instance;

/* ------------------------------------------------------------------ the command line */

typedef struct COMMAND {
    DWORD Switches;
    PWSTR Library;
    PWSTR Entry;
    PWSTR Arguments;
} COMMAND;

static BOOL starts_with(PCWSTR text, PCWSTR name)
{
    for (; *name; name++, text++)
        if ((*text | 0x20) != *name)
            return FALSE;
    return TRUE;
}

/* the line is edited in place, a library or entry is ended with a null. */
static BOOL parse(PWSTR line, COMMAND *command)
{
    command->Switches = 0;
    command->Library = command->Entry = command->Arguments = nullptr;
    if (!*line)
        return FALSE;
    PWSTR at = line;
    while (*at == L' ')
        at++;
    while (*at == L'/' || *at == L'-') {
        at++;
        while (*at && *at != L' ') {
            if ((*at | 0x20) == L's' && starts_with(at, L"sta")) {
                command->Switches |= SWITCH_STA;
                at += 3;
            } else if ((*at | 0x20) == L'l' && starts_with(at, L"localserver")) {
                command->Switches |= SWITCH_LOCALSERVER;
                at += 11;
            } else {
                at++;
            }
        }
        while (*at == L' ')
            at++;
    }
    if (!*at)
        return FALSE;

    PWSTR library = at;
    if (*at == L'"') {
        library = ++at;
        while (*at && *at != L'"')
            at++;
        if (!*at)
            return FALSE;
        *at++ = 0;
    } else {
        while (*at && *at != L' ' && *at != L',')
            at++;
        if (!*at) {
            if (!command->Switches)
                return FALSE;
        } else {
            *at++ = 0;
        }
    }
    while (*at == L' ' || *at == L',')
        at++;

    command->Library = library;
    if (command->Switches) {
        command->Arguments = at;
        return TRUE;
    }
    if (!*at)
        return FALSE;
    command->Entry = at;
    while (*at && *at != L' ')
        at++;
    if (*at) {
        *at++ = 0;
        while (*at && *at <= 0x20)
            at++;
    }
    command->Arguments = at;
    for (PWSTR p = command->Entry; *p; p++)
        if (*p == L'\\' || *p == L'/')
            return FALSE;
    return TRUE;
}

/* ------------------------------------------------------------------ messages */

static void show_error(PCWSTR format, PCWSTR first, PCWSTR second)
{
    WCHAR text[460];
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, format, first, second);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    MessageBoxW(nullptr, text, MESSAGE_TITLE, MB_ICONERROR);
}

/* ------------------------------------------------------------------ the entry */

static int atoi_w(PCWSTR text)
{
    int value = 0, sign = 1;
    while (*text == L' ' || *text == L'\t')
        text++;
    if (*text == L'-') {
        sign = -1;
        text++;
    } else if (*text == L'+') {
        text++;
    }
    while (*text >= L'0' && *text <= L'9')
        value = value * 10 + (*text++ - L'0');
    return value * sign;
}

/* a number sign names an ordinal, which takes the wide arguments. a name is tried with W, then A, then as it is, and
   the last two take the arguments as ansi text. */
static FARPROC find_entry(HMODULE module, PCWSTR name, BOOL *ansi)
{
    *ansi = FALSE;
    if (name[0] == L'#' && name[1])
        return GetProcAddress(module, (PCSTR)(ULONG_PTR)(WORD)atoi_w(name + 1));
    char narrow[512];
    int length = WideCharToMultiByte(CP_ACP, 0x400, name, -1, narrow, sizeof(narrow) - 2, nullptr, nullptr);
    if (!length)
        return nullptr;
    length--;
    narrow[length + 1] = 0;
    narrow[length] = 'W';
    FARPROC entry = GetProcAddress(module, narrow);
    if (entry)
        return entry;
    *ansi = TRUE;
    narrow[length] = 'A';
    entry = GetProcAddress(module, narrow);
    if (!entry) {
        narrow[length] = 0;
        entry = GetProcAddress(module, narrow);
    }
    return entry;
}

/* ------------------------------------------------------------------ a library of another processor */

static WORD machine_of(PCWSTR path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    WORD machine = 0;
    BYTE dos[0x40];
    DWORD read;
    if (ReadFile(file, dos, sizeof(dos), &read, nullptr) && read == sizeof(dos) && dos[0] == 'M' && dos[1] == 'Z') {
        LONG offset = *(LONG *)(dos + 0x3C);
        BYTE header[8];
        if (SetFilePointer(file, offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
            ReadFile(file, header, sizeof(header), &read, nullptr) && read == sizeof(header) && header[0] == 'P' &&
            header[1] == 'E')
            machine = *(WORD *)(header + 4);
    }
    CloseHandle(file);
    return machine;
}

/*
 * a library of the other processor is run by the rundll32 of that processor with the same command line. the 32 bit
 * program of a 64 bit machine reaches the 64 bit one through the unredirected System32.
 */
static BOOL run_other_processor(PCWSTR library)
{
    WORD machine = machine_of(library);
    if (!machine)
        return FALSE;
    USHORT process = 0, native = 0;
    if (!IsWow64Process2(GetCurrentProcess(), &process, &native))
        return FALSE;
    WCHAR path[MAX_PATH + 16];
    UINT length;
    if (machine == native) {
        if (!process)
            return FALSE;
        length = GetWindowsDirectoryW(path, MAX_PATH);
        if (!length || length + 28 > RTL_NUMBER_OF(path))
            return FALSE;
        wcscpy(path + length, L"\\System32");
        length += 9;
    } else {
        length = GetSystemWow64Directory2W(path, MAX_PATH, machine);
        if (!length || length >= MAX_PATH)
            return FALSE;
    }
    wcscpy(path + length, L"\\rundll32.exe");

    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION info;
    Wow64EnableWow64FsRedirection(FALSE);
    BOOL created = CreateProcessW(path, GetCommandLineW(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                                  nullptr, &startup, &info);
    Wow64EnableWow64FsRedirection(TRUE);
    if (!created)
        return FALSE;
    DWORD wait = WaitForSingleObject(info.hProcess, INFINITE);
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    return wait == WAIT_OBJECT_0;
}

/* ------------------------------------------------------------------ the activation context of the library */

static BOOL is_relative(PCWSTR path)
{
    if (path[0] == L'\\' || path[0] == L'/')
        return FALSE;
    return !(path[0] && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'));
}

/* the manifest next to the library, else its resources 123, 124 and 2. the context is active when it is returned. */
static HANDLE activate_manifest(PCWSTR library, ULONG_PTR *cookie)
{
    WCHAR path[MAX_PATH + 16];
    *cookie = 0;
    if (is_relative(library)) {
        PWSTR part;
        if (!SearchPathW(nullptr, library, nullptr, MAX_PATH, path, &part))
            return nullptr;
    } else {
        if (wcslen(library) >= MAX_PATH)
            return nullptr;
        wcscpy(path, library);
    }
    SIZE_T length = wcslen(path);
    ACTCTXW context = {sizeof(context)};
    context.lpSource = path;
    HANDLE handle = INVALID_HANDLE_VALUE;
    wcscpy(path + length, L".manifest");
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        handle = CreateActCtxW(&context);
    path[length] = 0;
    if (handle == INVALID_HANDLE_VALUE) {
        context.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
        static const WORD ids[] = {123, 124, 2};
        for (int i = 0; i < 3 && handle == INVALID_HANDLE_VALUE; i++) {
            context.lpResourceName = MAKEINTRESOURCEW(ids[i]);
            handle = CreateActCtxW(&context);
        }
    }
    if (handle != INVALID_HANDLE_VALUE)
        ActivateActCtx(handle, cookie);
    return handle;
}

/*
 * a process that is not elevated runs a library with file and registry virtualization on, unless its manifest says
 * which level it wants: asInvoker is fine and any other level is refused, as a library that needs more cannot get it.
 */
static BOOL enter_run_level(HANDLE context)
{
    HANDLE token;
    if (NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY_, &token) < 0)
        return FALSE;
    ULONG type = ELEVATION_TYPE_DEFAULT, elevation = 0, returned;
    NTSTATUS status = NtQueryInformationToken(token, TOKEN_ELEVATION_TYPE_CLASS, &type, sizeof(type), &returned);
    BOOL elevated = FALSE;
    if (status >= 0) {
        if (type == ELEVATION_TYPE_FULL) {
            elevated = TRUE;
        } else if (type == ELEVATION_TYPE_DEFAULT) {
            status = NtQueryInformationToken(token, TOKEN_ELEVATION_CLASS, &elevation, sizeof(elevation), &returned);
            elevated = elevation != 0;
        }
    }
    NtClose(token);
    if (status < 0)
        return FALSE;
    if (elevated)
        return TRUE;
    ULONG level[3] = {};
    if (context && context != INVALID_HANDLE_VALUE &&
        QueryActCtxW(QUERY_ACTCTX_FLAG_NO_ADDREF, context, nullptr, RUN_LEVEL_CLASS, level,
                     sizeof(level), nullptr) &&
        level[1]) {
        return level[1] == RUN_LEVEL_AS_INVOKER;
    }
    if (NtOpenProcessToken(NtCurrentProcess(), TOKEN_ADJUST_DEFAULT_, &token) >= 0) {
        ULONG on = 1;
        NtSetInformationToken(token, TOKEN_VIRTUALIZATION_ENABLED_CLASS, &on, sizeof(on));
        NtClose(token);
    }
    return TRUE;
}

/* the operating system version a library asks for in its header. */
static void apply_version(HMODULE module)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)module + dos->e_lfanew);
    if (!nt->FileHeader.SizeOfOptionalHeader || !nt->OptionalHeader.Win32VersionValue)
        return;
    ULONG value = nt->OptionalHeader.Win32VersionValue;
    PEB *peb = NtCurrentPeb();
    peb->OSMajorVersion = value & 0xFF;
    peb->OSMinorVersion = (value >> 8) & 0xFF;
    peb->OSBuildNumber = (USHORT)(value >> 16);
    peb->OSPlatformId = (value ^ 0xBFFFFFFF) >> 30;
}

/* ------------------------------------------------------------------ the window the entry gets */

static LRESULT CALLBACK stub_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, message, wparam, lparam);
}

static HWND create_stub_window(void)
{
    WNDCLASSW class = {};
    class.lpfnWndProc = stub_proc;
    class.cbWndExtra = sizeof(PVOID);
    class.hInstance = g_instance;
    class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    class.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
    class.lpszClassName = L"RunDLL";
    RegisterClassW(&class);
    return CreateWindowExW(WS_EX_TOOLWINDOW, L"RunDLL", L"", 0, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr,
                           g_instance, nullptr);
}

/* ------------------------------------------------------------------ the modes */

/* the switch: a class of the library's own is created in a single threaded apartment and told to run. */
static void run_sta(const COMMAND *command)
{
    static const GUID iid = {0xfce4bde0, 0x4b68, 0x4b80, {0x8e, 0x9c, 0x74, 0x26, 0x31, 0x5a, 0x73, 0x88}};
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return;
    CLSID clsid;
    if (SUCCEEDED(CLSIDFromString(command->Library, &clsid))) {
        void **object = nullptr;
        if (SUCCEEDED(CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER, &iid, (void **)&object)) && object) {
            void **vtable = (void **)*object;
            ((HRESULT(STDMETHODCALLTYPE *)(void *, PCWSTR))vtable[3])(object, command->Arguments);
            ((ULONG(STDMETHODCALLTYPE *)(void *))vtable[2])(object);
        }
    }
    CoUninitialize();
}

static void run_entry(const COMMAND *command, int show)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    ULONG_PTR cookie;
    HANDLE context = activate_manifest(command->Library, &cookie);
    if (!enter_run_level(context)) {
        show_error(MESSAGE_ILLEGAL, command->Library, L"requestedRunLevel");
    } else {
        HMODULE module = LoadLibraryExW(command->Library, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            DWORD error = GetLastError();
            WCHAR reason[260];
            DWORD n;
            if (error == ERROR_BAD_EXE_FORMAT_) {
                if (run_other_processor(command->Library))
                    goto done;
                DWORD_PTR arguments[1] = {(DWORD_PTR)command->Library};
                n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY, nullptr, error, 0, reason,
                                   RTL_NUMBER_OF(reason), (va_list *)arguments);
            } else {
                n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reason,
                                   RTL_NUMBER_OF(reason), nullptr);
            }
            if (n)
                show_error(MESSAGE_LOAD, command->Library, reason);
        } else {
            BOOL ansi;
            ENTRY entry = (ENTRY)(void *)find_entry(module, command->Entry, &ansi);
            if (!entry) {
                show_error(MESSAGE_MISSING_ENTRY, command->Library, command->Entry);
            } else {
                PCWSTR arguments = command->Arguments;
                char *narrow = nullptr;
                BOOL ready = TRUE;
                if (ansi && *arguments) {
                    int size = WideCharToMultiByte(CP_ACP, 0x400, arguments, -1, nullptr, 0, nullptr, nullptr);
                    narrow = LocalAlloc(LMEM_FIXED, size);
                    if (narrow) {
                        WideCharToMultiByte(CP_ACP, 0x400, arguments, -1, narrow, size, nullptr, nullptr);
                        arguments = (PCWSTR)narrow;
                    } else {
                        show_error(MESSAGE_MEMORY, command->Arguments, nullptr);
                        ready = FALSE;
                    }
                }
                if (ready) {
                    apply_version(module);
                    HWND stub = create_stub_window();
                    entry(stub, g_instance, arguments, show);
                    if (stub)
                        DestroyWindow(stub);
                }
                if (narrow)
                    LocalFree(narrow);
            }
            FreeLibrary(module);
        }
    }
done:
    if (context != INVALID_HANDLE_VALUE && context) {
        if (cookie)
            DeactivateActCtx(0, cookie);
        ReleaseActCtx(context);
    }
}

void entry(void)
{
    g_instance = GetModuleHandleW(nullptr);
    PCWSTR line = GetCommandLineW();
    SIZE_T length = wcslen(line);
    PWSTR copy = LocalAlloc(LMEM_FIXED, (length + 1) * sizeof(WCHAR));
    if (copy) {
        wcscpy(copy, line);
        /* the program name goes first, and may be quoted. */
        PWSTR at = copy;
        BOOL quoted = FALSE;
        while (*at && (quoted || (*at != L' ' && *at != L'\t'))) {
            if (*at == L'"')
                quoted = !quoted;
            at++;
        }
        while (*at == L' ' || *at == L'\t')
            at++;
        STARTUPINFOW startup = {sizeof(startup)};
        GetStartupInfoW(&startup);
        int show = (startup.dwFlags & STARTF_USESHOWWINDOW) ? startup.wShowWindow : SW_SHOWDEFAULT;
        COMMAND command;
        if (parse(at, &command)) {
            if (command.Switches & SWITCH_STA)
                run_sta(&command);
            else if (!(command.Switches & SWITCH_LOCALSERVER))
                run_entry(&command, show);
        }
        LocalFree(copy);
    }
    ExitProcess(0);
}
