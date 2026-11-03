/*
 * The side of features on demand that programs meet: a program that uses a file that is missing in the folders of a
 * feature asks features.exe for the feature, and tries again. Using is starting a program, loading a library and opening
 * a file to read it. A look at the name asks for nothing. The file is compiled into kernelbase, shell32 and gdi32, each
 * can ask.
 */
#include <mime/feature.h>
#include <mime/ntapi.h>
#include <mime/win32/winbase.h>

typedef struct FEATURE {
    PCWSTR Name;
    BOOL (*Present)(WCHAR drive);
} FEATURE;

typedef struct TRIGGER {
    PCWSTR Folder; /* Below the drive, with a closing backslash. */
    const FEATURE *Feature;
} TRIGGER;

static BOOL webview2_present(WCHAR drive);
static BOOL netfx_present(WCHAR drive);
static BOOL powershell_present(WCHAR drive);

static const FEATURE g_powershell = {MIME_FEATURE_POWERSHELL, powershell_present};
static const FEATURE g_webview2 = {MIME_FEATURE_WEBVIEW2, webview2_present};
static const FEATURE g_netfx = {MIME_FEATURE_NETFX4, netfx_present};

/* The folders programs look into for what a feature holds. The assemblies of the shell come before the Framework's cache. */
static const TRIGGER g_triggers[] = {
    {L"\\Windows\\System32\\WindowsPowerShell\\", &g_powershell},
    {L"\\Windows\\SysWOW64\\WindowsPowerShell\\", &g_powershell},
    {L"\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Management.Automation\\", &g_powershell},
    {L"\\Program Files (x86)\\Microsoft\\EdgeWebView\\", &g_webview2},
    {L"\\Windows\\Microsoft.NET\\", &g_netfx},
    {L"\\Windows\\assembly\\", &g_netfx},
};

static WCHAR fold(WCHAR c)
{
    return c >= L'A' && c <= L'Z' ? (WCHAR)(c + 32) : c;
}

/* Whether the path, after a drive, starts with the folder. */
static BOOL in_folder(PCWSTR path, PCWSTR folder)
{
    if (!path[0] || path[1] != L':')
        return FALSE;
    for (path += 2; *folder; path++, folder++)
        if (fold(*path) != fold(*folder))
            return FALSE;
    return TRUE;
}

/*
 * The look at what is on the drive uses the native calls and not the file functions of kernelbase, which would end in
 * MimeFeatureFault again.
 */
static BOOL native_open(PCWSTR path, BOOL directory, HANDLE *handle)
{
    WCHAR name[MAX_PATH + 8];
    UINT length = 0;
    for (PCWSTR p = L"\\??\\"; *p; p++)
        name[length++] = *p;
    for (; *path && length < RTL_NUMBER_OF(name) - 1; path++)
        name[length++] = *path;
    name[length] = 0;
    UNICODE_STRING nt = {(USHORT)(length * sizeof(WCHAR)), (USHORT)(sizeof(name)), name};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    if (!directory) {
        FILE_BASIC_INFORMATION info;
        return NT_SUCCESS(NtQueryAttributesFile(&oa, &info));
    }
    IO_STATUS_BLOCK iosb;
    return NT_SUCCESS(NtOpenFile(handle, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT));
}

/* The runtime is a folder named after its version that holds msedgewebview2.exe. */
static BOOL webview2_present(WCHAR drive)
{
    WCHAR application[MAX_PATH];
    UINT base = 0;
    application[base++] = drive;
    application[base++] = L':';
    for (PCWSTR p = L"\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application"; *p; p++)
        application[base++] = *p;
    application[base] = 0;
    HANDLE dir;
    if (!native_open(application, TRUE, &dir))
        return FALSE;
    BOOL found = FALSE;
    UCHAR buffer[1024];
    BOOLEAN restart = TRUE;
    while (!found) {
        IO_STATUS_BLOCK iosb;
        if (!NT_SUCCESS(NtQueryDirectoryFile(dir, nullptr, nullptr, nullptr, &iosb, buffer, sizeof(buffer), FileNamesInformation, FALSE, nullptr, restart)))
            break;
        restart = FALSE;
        for (FILE_NAMES_INFORMATION *e = (FILE_NAMES_INFORMATION *)buffer;;
             e = (FILE_NAMES_INFORMATION *)((PUCHAR)e + e->NextEntryOffset)) {
            ULONG n = e->FileNameLength / sizeof(WCHAR);
            if (n && e->FileName[0] != L'.' && base + 1 + n + 20 < RTL_NUMBER_OF(application)) {
                UINT length = base;
                application[length++] = L'\\';
                for (ULONG i = 0; i < n; i++)
                    application[length++] = e->FileName[i];
                for (PCWSTR p = L"\\msedgewebview2.exe"; *p; p++)
                    application[length++] = *p;
                application[length] = 0;
                if (native_open(application, FALSE, nullptr)) {
                    found = TRUE;
                    break;
                }
            }
            if (!e->NextEntryOffset)
                break;
        }
    }
    NtClose(dir);
    return found;
}

static BOOL netfx_present(WCHAR drive)
{
    WCHAR path[MAX_PATH];
    UINT length = 0;
    path[length++] = drive;
    path[length++] = L':';
    for (PCWSTR p = L"\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\clr.dll"; *p; p++)
        path[length++] = *p;
    path[length] = 0;
    return native_open(path, FALSE, nullptr);
}

/* The shell is the file and the assembly of its engine in the cache. */
static BOOL powershell_present(WCHAR drive)
{
    WCHAR path[MAX_PATH];
    UINT length = 0;
    path[length++] = drive;
    path[length++] = L':';
    for (PCWSTR p = L"\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"; *p; p++)
        path[length++] = *p;
    path[length] = 0;
    if (!native_open(path, FALSE, nullptr))
        return FALSE;
    length = 2;
    for (PCWSTR p = L"\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Management.Automation"; *p; p++)
        path[length++] = *p;
    path[length] = 0;
    HANDLE directory;
    if (!native_open(path, TRUE, &directory))
        return FALSE;
    NtClose(directory);
    return TRUE;
}

static PCWSTR skip_prefix(PCWSTR path)
{
    if (path[0] == L'\\' && (path[1] == L'?' || path[1] == L'\\') && path[2] == L'?' && path[3] == L'\\')
        return path + 4;
    return path;
}

static DWORD run_program(PCWSTR arguments, PCWSTR caller)
{
    WCHAR line[MAX_PATH + 600];
    WCHAR windows[MAX_PATH];
    UINT n = GetWindowsDirectoryW(windows, MAX_PATH);
    if (!n || n >= MAX_PATH - 40)
        return ERROR_PATH_NOT_FOUND;
    UINT length = 0;
    line[length++] = L'"';
    for (UINT i = 0; i < n; i++)
        line[length++] = windows[i];
    for (PCWSTR p = MIME_FEATURE_PROGRAM; *p; p++)
        line[length++] = *p;
    line[length++] = L'"';
    line[length++] = L' ';
    for (; *arguments && length < RTL_NUMBER_OF(line) - 2; arguments++)
        line[length++] = *arguments;
    WCHAR pid[16];
    UINT digits = 0;
    for (DWORD id = GetCurrentProcessId(); id || !digits; id /= 10)
        pid[digits++] = (WCHAR)(L'0' + id % 10);
    for (PCWSTR flag = L" /caller-pid:"; *flag && length < RTL_NUMBER_OF(line) - 2; flag++)
        line[length++] = *flag;
    while (digits && length < RTL_NUMBER_OF(line) - 2)
        line[length++] = pid[--digits];
    if (caller && *caller) {
        PCWSTR flag = L" /caller-name:";
        for (; *flag; flag++)
            line[length++] = *flag;
        for (; *caller && length < RTL_NUMBER_OF(line) - 2; caller++)
            line[length++] = *caller == L' ' ? L'_' : *caller;
    }
    line[length] = 0;
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup, &process))
        return GetLastError();
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = ERROR_GEN_FAILURE;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return code;
}

/* The name of the program that asks, for the window. */
static void caller_name(PWSTR name, UINT chars)
{
    WCHAR path[MAX_PATH];
    name[0] = 0;
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return;
    UINT start = n;
    while (start && path[start - 1] != L'\\')
        start--;
    UINT length = 0;
    for (; path[start] && length + 1 < chars; start++)
        name[length++] = path[start];
    name[length] = 0;
}

DWORD MimeFeatureRequest(PCWSTR name, DWORD flags)
{
    WCHAR arguments[128] = L"/enable-feature:";
    UINT length = 16;
    for (; *name && length < RTL_NUMBER_OF(arguments) - 1; name++)
        arguments[length++] = *name;
    arguments[length] = 0;
    WCHAR caller[64];
    caller_name(caller, RTL_NUMBER_OF(caller));
    return run_program(arguments, caller);
}

DWORD MimeFeatureRequestFont(PCWSTR family)
{
    WCHAR arguments[160] = L"/enable-font:\"";
    UINT length = 14;
    for (; *family && length < RTL_NUMBER_OF(arguments) - 2; family++)
        if (*family != L'"')
            arguments[length++] = *family;
    arguments[length++] = L'"';
    arguments[length] = 0;
    WCHAR caller[64];
    caller_name(caller, RTL_NUMBER_OF(caller));
    return run_program(arguments, caller);
}

/* A program started by its bare name, which the search of the path did not find. */
static BOOL is_shell_name(PCWSTR name)
{
    for (PCWSTR p = name; *p; p++)
        if (*p == L'\\' || *p == L'/' || *p == L':')
            return FALSE;
    static const WCHAR plain[] = L"powershell", with_extension[] = L"powershell.exe";
    UINT n = 0;
    while (name[n])
        n++;
    PCWSTR want = n == RTL_NUMBER_OF(plain) - 1 ? plain : n == RTL_NUMBER_OF(with_extension) - 1 ? with_extension : nullptr;
    if (!want)
        return FALSE;
    for (UINT i = 0; i < n; i++)
        if (fold(name[i]) != want[i])
            return FALSE;
    return TRUE;
}

/* The name of an object that holds what a process learned about a feature: asked, and agreed. */
static void state_name(PWSTR name, UINT chars, PCWSTR feature, PCWSTR kind)
{
    WCHAR pid[16];
    UINT digits = 0;
    for (DWORD id = GetCurrentProcessId(); id || !digits; id /= 10)
        pid[digits++] = (WCHAR)(L'0' + id % 10);
    UINT length = 0;
    for (PCWSTR p = L"Local\\MimeFeature."; *p; p++)
        name[length++] = *p;
    while (digits)
        name[length++] = pid[--digits];
    name[length++] = L'.';
    for (; *feature && length + 8 < chars; feature++)
        name[length++] = *feature;
    name[length++] = L'.';
    for (; *kind && length + 1 < chars; kind++)
        name[length++] = *kind;
    name[length] = 0;
}

/*
 * A feature is requested once in a process, whatever the answer, and whichever of its libraries asks: the answer is kept
 * in two events that live as long as the process. Returns TRUE when the feature arrived.
 */
static BOOL ask_once(const FEATURE *feature, WCHAR drive)
{
    WCHAR asked_name[96], agreed_name[96];
    state_name(asked_name, RTL_NUMBER_OF(asked_name), feature->Name, L"Asked");
    state_name(agreed_name, RTL_NUMBER_OF(agreed_name), feature->Name, L"Agreed");
    DWORD last = GetLastError();
    HANDLE asked = CreateEventW(nullptr, TRUE, FALSE, asked_name);
    if (!asked) {
        SetLastError(last);
        return FALSE;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        /* Another thread or library is asking, its answer is ours. */
        WaitForSingleObject(asked, INFINITE);
        CloseHandle(asked);
        HANDLE agreed = OpenEventW(SYNCHRONIZE, FALSE, agreed_name);
        SetLastError(last);
        if (!agreed)
            return FALSE;
        CloseHandle(agreed);
        return TRUE;
    }
    /*
     * What features.exe sets for the programs it runs, which must not ask for what they are installing. A feature that is
     * on the drive does not have the file, as on Windows: programs use optional ones and fail when they are not there.
     */
    BOOL arrived = FALSE;
    WCHAR busy[4];
    if (!GetEnvironmentVariableW(MIME_FEATURE_BUSY_VARIABLE, busy, RTL_NUMBER_OF(busy)) && !feature->Present(drive))
        arrived = MimeFeatureRequest(feature->Name, 0) == ERROR_SUCCESS;
    if (arrived)
        CreateEventW(nullptr, TRUE, TRUE, agreed_name);
    SetEvent(asked);
    SetLastError(last);
    return arrived;
}

BOOL MimeFeatureFault(PCWSTR path)
{
    if (!path)
        return FALSE;
    path = skip_prefix(path);
    WCHAR shell[MAX_PATH];
    if (is_shell_name(path)) {
        UINT n = GetWindowsDirectoryW(shell, MAX_PATH);
        PCWSTR tail = L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        UINT length = 0;
        while (tail[length])
            length++;
        if (!n || n + length >= MAX_PATH)
            return FALSE;
        for (UINT i = 0; i <= length; i++)
            shell[n + i] = tail[i];
        path = shell;
    }
    const TRIGGER *trigger = nullptr;
    for (UINT i = 0; i < RTL_NUMBER_OF(g_triggers) && !trigger; i++)
        if (in_folder(path, g_triggers[i].Folder))
            trigger = &g_triggers[i];
    if (!trigger)
        return FALSE;
    return ask_once(trigger->Feature, path[0]);
}

/* Whether the path is the folder of the engine's assembly in the cache, or something below it. */
static BOOL in_shell_assembly(PCWSTR path)
{
    PCWSTR folder = L"\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Management.Automation";
    if (!path[0] || path[1] != L':')
        return FALSE;
    PCWSTR p = path + 2;
    for (; *folder; p++, folder++)
        if (fold(*p) != fold(*folder))
            return FALSE;
    return !*p || *p == L'\\';
}

/*
 * The runtime of the Framework looks for an assembly of the cache by the names of its files and folders, and never opens
 * one that is not there, so loading an assembly of the shell is seen as a look. The look of the runtime itself is the use,
 * a look of any other program is not.
 */
BOOL MimeFeatureFaultFromRuntime(PCWSTR path, PVOID caller)
{
    if (!path)
        return FALSE;
    path = skip_prefix(path);
    if (!in_shell_assembly(path))
        return FALSE;
    HMODULE module;
    WCHAR name[MAX_PATH];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, caller, &module))
        return FALSE;
    DWORD n = GetModuleFileNameW(module, name, MAX_PATH);
    if (!n || n >= MAX_PATH || !in_folder(name, L"\\Windows\\Microsoft.NET\\Framework"))
        return FALSE;
    return ask_once(&g_powershell, path[0]);
}
