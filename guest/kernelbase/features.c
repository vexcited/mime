/*
 * The side of features on demand that programs meet: a file that is missing in the folders of a feature asks
 * features.exe for the feature, and the program that asked tries again. The file is compiled into kernelbase and into
 * the shim of the .NET Framework, both can ask.
 */
#include <mime/feature.h>
#include <mime/ntapi.h>
#include <mime/win32/winbase.h>

typedef struct TRIGGER {
    PCWSTR Folder;  /* Below the drive, with a closing backslash. */
    PCWSTR Feature;
    BOOL (*Present)(WCHAR drive);
    volatile LONG State;
} TRIGGER;

enum { TRIGGER_IDLE, TRIGGER_RUNNING, TRIGGER_ENABLED, TRIGGER_GAVE_UP, TRIGGER_PRESENT };

static BOOL webview2_present(WCHAR drive);
static BOOL netfx_present(WCHAR drive);

/* The folders programs look into for what a feature holds. */
static TRIGGER g_triggers[] = {
    {L"\\Program Files (x86)\\Microsoft\\EdgeWebView\\", MIME_FEATURE_WEBVIEW2, webview2_present, TRIGGER_IDLE},
    {L"\\Windows\\Microsoft.NET\\", MIME_FEATURE_NETFX4, netfx_present, TRIGGER_IDLE},
    {L"\\Windows\\assembly\\", MIME_FEATURE_NETFX4, netfx_present, TRIGGER_IDLE},
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

BOOL MimeFeatureFault(PCWSTR path)
{
    if (!path)
        return FALSE;
    path = skip_prefix(path);
    TRIGGER *trigger = nullptr;
    for (UINT i = 0; i < RTL_NUMBER_OF(g_triggers) && !trigger; i++)
        if (in_folder(path, g_triggers[i].Folder))
            trigger = &g_triggers[i];
    if (!trigger)
        return FALSE;
    LONG state = InterlockedCompareExchange(&trigger->State, TRIGGER_RUNNING, TRIGGER_IDLE);
    if (state == TRIGGER_IDLE) {
        /* What features.exe sets for the programs it runs, which must not ask for what they are installing. */
        WCHAR busy[4];
        DWORD last = GetLastError();
        if (GetEnvironmentVariableW(MIME_FEATURE_BUSY_VARIABLE, busy, RTL_NUMBER_OF(busy))) {
            InterlockedExchange(&trigger->State, TRIGGER_GAVE_UP);
            return FALSE;
        }
        /* A file that an installed feature does not have is missing, as on Windows: programs probe for optional ones. */
        if (trigger->Present(path[0])) {
            InterlockedExchange(&trigger->State, TRIGGER_PRESENT);
            return FALSE;
        }
        DWORD result = MimeFeatureRequest(trigger->Feature, 0);
        InterlockedExchange(&trigger->State, result == ERROR_SUCCESS ? TRIGGER_ENABLED : TRIGGER_GAVE_UP);
        SetLastError(last);
        return result == ERROR_SUCCESS;
    }
    /* Another thread is asking, its answer is ours. */
    while (state == TRIGGER_RUNNING) {
        Sleep(50);
        state = trigger->State;
    }
    return state == TRIGGER_ENABLED;
}
