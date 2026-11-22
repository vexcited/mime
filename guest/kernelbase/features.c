/*
 * The side of features on demand that Mime's own libraries meet. The kernel asks for a feature when a program opens a
 * file of it that is not there, see guest/ntos/ex/feature.c. A library that learned a name is missing by looking at it
 * says here that it meant to use it: the file is opened to be read, the kernel asks once, and the open tells whether
 * the feature arrived. The file is compiled into kernelbase and shell32, each can say so.
 */
#include <mime/feature.h>
#include <mime/ntapi.h>
#include <mime/win32/winbase.h>

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

/* The native open and not the file functions of kernelbase, which would end here again. */
static BOOL open_to_read(PCWSTR path)
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
    IO_STATUS_BLOCK iosb;
    HANDLE file;
    DWORD last = GetLastError();
    NTSTATUS status = NtOpenFile(&file, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (NT_SUCCESS(status))
        NtClose(file);
    SetLastError(last);
    return NT_SUCCESS(status);
}

static PCWSTR skip_prefix(PCWSTR path)
{
    if (path[0] == L'\\' && (path[1] == L'?' || path[1] == L'\\') && path[2] == L'?' && path[3] == L'\\')
        return path + 4;
    return path;
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

static BOOL shell_program(WCHAR *path)
{
    UINT n = GetWindowsDirectoryW(path, MAX_PATH);
    PCWSTR tail = L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    UINT length = 0;
    while (tail[length])
        length++;
    if (!n || n + length >= MAX_PATH)
        return FALSE;
    for (UINT i = 0; i <= length; i++)
        path[n + i] = tail[i];
    return TRUE;
}

BOOL MimeFeatureFault(PCWSTR path)
{
    if (!path)
        return FALSE;
    path = skip_prefix(path);
    WCHAR shell[MAX_PATH];
    if (is_shell_name(path)) {
        if (!shell_program(shell))
            return FALSE;
        path = shell;
    }
    return path[0] && path[1] == L':' && open_to_read(path);
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
 * a look of any other program is not. The shell is asked for by its program.
 */
BOOL MimeFeatureFaultFromRuntime(PCWSTR path, PVOID caller)
{
    if (!path)
        return FALSE;
    path = skip_prefix(path);
    if (!in_shell_assembly(path))
        return FALSE;
    HMODULE module;
    WCHAR name[MAX_PATH], shell[MAX_PATH];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, caller, &module))
        return FALSE;
    DWORD n = GetModuleFileNameW(module, name, MAX_PATH);
    if (!n || n >= MAX_PATH || !in_folder(name, L"\\Windows\\Microsoft.NET\\Framework"))
        return FALSE;
    return shell_program(shell) && open_to_read(shell);
}
