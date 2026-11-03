/*
 * The managed library of the Windows Management Infrastructure. Windows has it as a part of the system, the Management
 * Framework does not carry it, and PowerShell loads it as it starts. It is read by range out of a package of NuGet
 * and put in the cache of assemblies, where the runtime finds it by its name.
 */
#include "featurep.h"

#define GAC_FOLDER L"Windows\\Microsoft.NET\\Assembly\\GAC_MSIL\\Microsoft.Management.Infrastructure"
#define GAC_VERSION L"\\v4.0_1.0.0.0__31bf3856ad364e35"

static void folder_path(RUN *run, WCHAR *path, SIZE_T chars, BOOL version)
{
    _snwprintf(path, chars, L"%ls" GAC_FOLDER L"%ls", run->Root, version ? GAC_VERSION : L"");
    path[chars - 1] = 0;
}

BOOL ManagementPresent(RUN *run)
{
    WCHAR path[MAX_PATH];
    folder_path(run, path, RTL_NUMBER_OF(path), TRUE);
    SIZE_T n = wcslen(path);
    _snwprintf(path + n, RTL_NUMBER_OF(path) - n, L"\\Microsoft.Management.Infrastructure.dll");
    path[RTL_NUMBER_OF(path) - 1] = 0;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

/* The folders below the drive, one level at a time. */
static void make_folders(PCWSTR path)
{
    WCHAR partial[MAX_PATH];
    for (SIZE_T i = 3; path[i] && i < RTL_NUMBER_OF(partial) - 1; i++) {
        if (path[i] != L'\\')
            continue;
        memcpy(partial, path, i * sizeof(WCHAR));
        partial[i] = 0;
        CreateDirectoryW(partial, nullptr);
    }
    CreateDirectoryW(path, nullptr);
}

typedef struct INSTALL {
    WCHAR Folder[MAX_PATH];
    DWORD Error;
    DWORD Installed;
} INSTALL;

static BOOL entry(void *context, const char *name, const BYTE *bytes, SIZE_T length)
{
    INSTALL *in = context;
    WCHAR path[MAX_PATH];
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%hs", in->Folder, name);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    if (file == INVALID_HANDLE_VALUE || !WriteFile(file, bytes, (DWORD)length, &written, nullptr) || written != length) {
        in->Error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        return FALSE;
    }
    CloseHandle(file);
    in->Installed++;
    return TRUE;
}

uint32_t ManagementInstall(RUN *run, const FEATURE_DEF *feature, PCWSTR package)
{
    BYTE *data = nullptr;
    SIZE_T size = 0;
    DWORD error = ReadPackage(package, &data, &size);
    if (error)
        return error;
    INSTALL in = {};
    folder_path(run, in.Folder, RTL_NUMBER_OF(in.Folder), TRUE);
    make_folders(in.Folder);
    if (!PackWalk(data, size, entry, &in))
        error = in.Error ? in.Error : ERROR_INVALID_DATA;
    else if (in.Installed != feature->FontCount)
        error = ERROR_FILE_NOT_FOUND;
    HeapFree(GetProcessHeap(), 0, data);
    return error;
}

uint32_t ManagementRemove(RUN *run)
{
    WCHAR path[MAX_PATH];
    folder_path(run, path, RTL_NUMBER_OF(path), FALSE);
    DeleteTree(path);
    return ManagementPresent(run) ? ERROR_SHARING_VIOLATION : ERROR_SUCCESS;
}
