/*
 * the transacted file functions of kernel32. Windows gives every one of them the same shape: a transaction handle of 0
 * or -1 is ERROR_INVALID_TRANSACTION, else the thread's current transaction is set to the handle, the plain function
 * runs and the current transaction is cleared. Mime has no kernel transaction manager and no ktmw32.dll, so no handle
 * of a transaction exists and no handle can pass: what is left of the shape is the argument checks and their errors.
 * A handle that is not 0 or -1 gets ERROR_INVALID_HANDLE, which is a guess at what the file system answers for an
 * object that is not a transaction.
 */
#include <mime/win32/winbase.h>

/* what FindFirstStreamW asks for. */
typedef enum _STREAM_INFO_LEVELS {
    FindStreamInfoStandard,
    FindStreamInfoMaxInfoLevel
} STREAM_INFO_LEVELS;

#define EXPORT_STD(type, name, args) DECLSPEC_EXPORT type WINAPI name args

#define ERROR_INVALID_TRANSACTION_ 6700

/* sets the error for a transaction handle that cannot be used, always. */
static void transaction_error(HANDLE transaction)
{
    SetLastError(transaction == nullptr || transaction == INVALID_HANDLE_VALUE ? ERROR_INVALID_TRANSACTION_
                                                                                : ERROR_INVALID_HANDLE);
}

/* the ansi name of a function that takes one, as a wide name that is freed afterwards; false with the error set. */
static BOOL wide_name(LPCSTR name, LPWSTR *out)
{
    *out = nullptr;
    if (!name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UINT page = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    int n = MultiByteToWideChar(page, 0, name, -1, nullptr, 0);
    if (n <= 0)
        return FALSE;
    WCHAR *wide = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(WCHAR));
    if (!wide) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    MultiByteToWideChar(page, 0, name, -1, wide, n);
    *out = wide;
    return TRUE;
}

static void free_name(LPWSTR wide)
{
    if (wide)
        HeapFree(GetProcessHeap(), 0, wide);
}

EXPORT_STD(BOOL, CopyFileTransactedW, (LPCWSTR existing, LPCWSTR target, LPPROGRESS_ROUTINE progress, LPVOID data,
                                       LPBOOL cancel, DWORD flags, HANDLE transaction))
{
    transaction_error(transaction);
    return FALSE;
}

EXPORT_STD(BOOL, CopyFileTransactedA, (LPCSTR existing, LPCSTR target, LPPROGRESS_ROUTINE progress, LPVOID data,
                                       LPBOOL cancel, DWORD flags, HANDLE transaction))
{
    LPWSTR a, b;
    if (!wide_name(existing, &a))
        return FALSE;
    if (!wide_name(target, &b)) {
        free_name(a);
        return FALSE;
    }
    BOOL ok = CopyFileTransactedW(a, b, progress, data, cancel, flags, transaction);
    free_name(a);
    free_name(b);
    return ok;
}

EXPORT_STD(BOOL, CreateDirectoryTransactedW, (LPCWSTR template_dir, LPCWSTR directory, LPSECURITY_ATTRIBUTES sa,
                                              HANDLE transaction))
{
    transaction_error(transaction);
    return FALSE;
}

EXPORT_STD(BOOL, CreateDirectoryTransactedA, (LPCSTR template_dir, LPCSTR directory, LPSECURITY_ATTRIBUTES sa,
                                              HANDLE transaction))
{
    /* the template may be absent, the new directory may not. */
    LPWSTR a = nullptr, b;
    if (template_dir && !wide_name(template_dir, &a))
        return FALSE;
    if (!wide_name(directory, &b)) {
        free_name(a);
        return FALSE;
    }
    BOOL ok = CreateDirectoryTransactedW(a, b, sa, transaction);
    free_name(a);
    free_name(b);
    return ok;
}

/* an extended parameter is not accepted, whatever the handle is. */
EXPORT_STD(HANDLE, CreateFileTransactedW, (LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                           DWORD disposition, DWORD flags, HANDLE template_file, HANDLE transaction,
                                           PUSHORT mini_version, PVOID extended))
{
    if (extended) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    transaction_error(transaction);
    return INVALID_HANDLE_VALUE;
}

EXPORT_STD(HANDLE, CreateFileTransactedA, (LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                           DWORD disposition, DWORD flags, HANDLE template_file, HANDLE transaction,
                                           PUSHORT mini_version, PVOID extended))
{
    LPWSTR wide;
    if (!wide_name(name, &wide))
        return INVALID_HANDLE_VALUE;
    HANDLE h = CreateFileTransactedW(wide, access, share, sa, disposition, flags, template_file, transaction, mini_version,
                                     extended);
    free_name(wide);
    return h;
}

EXPORT_STD(BOOL, CreateHardLinkTransactedW, (LPCWSTR link, LPCWSTR existing, LPSECURITY_ATTRIBUTES sa,
                                             HANDLE transaction))
{
    transaction_error(transaction);
    return FALSE;
}

EXPORT_STD(BOOL, CreateHardLinkTransactedA, (LPCSTR link, LPCSTR existing, LPSECURITY_ATTRIBUTES sa,
                                             HANDLE transaction))
{
    LPWSTR a, b;
    if (!wide_name(link, &a))
        return FALSE;
    if (!wide_name(existing, &b)) {
        free_name(a);
        return FALSE;
    }
    BOOL ok = CreateHardLinkTransactedW(a, b, sa, transaction);
    free_name(a);
    free_name(b);
    return ok;
}

EXPORT_STD(BOOLEAN, CreateSymbolicLinkTransactedW, (LPCWSTR link, LPCWSTR target, DWORD flags, HANDLE transaction))
{
    transaction_error(transaction);
    return FALSE;
}

EXPORT_STD(BOOLEAN, CreateSymbolicLinkTransactedA, (LPCSTR link, LPCSTR target, DWORD flags, HANDLE transaction))
{
    LPWSTR a, b;
    if (!wide_name(link, &a))
        return FALSE;
    if (!wide_name(target, &b)) {
        free_name(a);
        return FALSE;
    }
    BOOLEAN ok = CreateSymbolicLinkTransactedW(a, b, flags, transaction);
    free_name(a);
    free_name(b);
    return ok;
}

EXPORT_STD(BOOL, DeleteFileTransactedW, (LPCWSTR name, HANDLE transaction))
{
    transaction_error(transaction);
    return FALSE;
}

EXPORT_STD(BOOL, DeleteFileTransactedA, (LPCSTR name, HANDLE transaction))
{
    LPWSTR wide;
    if (!wide_name(name, &wide))
        return FALSE;
    BOOL ok = DeleteFileTransactedW(wide, transaction);
    free_name(wide);
    return ok;
}

EXPORT_STD(HANDLE, FindFirstFileNameTransactedW, (LPCWSTR name, DWORD flags, LPDWORD length, PWSTR link_name,
                                                  HANDLE transaction))
{
    transaction_error(transaction);
    return INVALID_HANDLE_VALUE;
}

EXPORT_STD(HANDLE, FindFirstFileTransactedW, (LPCWSTR name, FINDEX_INFO_LEVELS level, LPVOID data,
                                              FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags, HANDLE transaction))
{
    transaction_error(transaction);
    return INVALID_HANDLE_VALUE;
}

EXPORT_STD(HANDLE, FindFirstFileTransactedA, (LPCSTR name, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS op,
                                              LPVOID filter, DWORD flags, HANDLE transaction))
{
    LPWSTR wide;
    if (!wide_name(name, &wide))
        return INVALID_HANDLE_VALUE;
    HANDLE h = FindFirstFileTransactedW(wide, level, data, op, filter, flags, transaction);
    free_name(wide);
    return h;
}

EXPORT_STD(HANDLE, FindFirstStreamTransactedW, (LPCWSTR name, STREAM_INFO_LEVELS level, LPVOID data, DWORD flags,
                                                HANDLE transaction))
{
    transaction_error(transaction);
    return INVALID_HANDLE_VALUE;
}

EXPORT_STD(DWORD, GetCompressedFileSizeTransactedW, (LPCWSTR name, LPDWORD high, HANDLE transaction))
{
    transaction_error(transaction);
    return INVALID_FILE_SIZE;
}

EXPORT_STD(DWORD, GetCompressedFileSizeTransactedA, (LPCSTR name, LPDWORD high, HANDLE transaction))
{
    LPWSTR wide;
    if (!wide_name(name, &wide))
        return INVALID_FILE_SIZE;
    DWORD size = GetCompressedFileSizeTransactedW(wide, high, transaction);
    free_name(wide);
    return size;
}

EXPORT_STD(BOOL, GetFileAttributesTransactedW, (LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info,
                                                HANDLE transaction))
{
    transaction_error(transaction);
    return FALSE;
}

EXPORT_STD(BOOL, GetFileAttributesTransactedA, (LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info,
                                                HANDLE transaction))
{
    LPWSTR wide;
    if (!wide_name(name, &wide))
        return FALSE;
    BOOL ok = GetFileAttributesTransactedW(wide, level, info, transaction);
    free_name(wide);
    return ok;
}

EXPORT_STD(DWORD, GetFullPathNameTransactedW, (LPCWSTR name, DWORD length, LPWSTR buffer, LPWSTR *file_part,
                                               HANDLE transaction))
{
    transaction_error(transaction);
    return 0;
}

EXPORT_STD(DWORD, GetFullPathNameTransactedA, (LPCSTR name, DWORD length, LPSTR buffer, LPSTR *file_part,
                                               HANDLE transaction))
{
    LPWSTR wide;
    if (!wide_name(name, &wide))
        return 0;
    DWORD n = GetFullPathNameTransactedW(wide, length, nullptr, nullptr, transaction);
    free_name(wide);
    return n;
}

EXPORT_STD(DWORD, GetLongPathNameTransactedW, (LPCWSTR short_path, LPWSTR long_path, DWORD length, HANDLE transaction))
{
    transaction_error(transaction);
    return 0;
}

EXPORT_STD(DWORD, GetLongPathNameTransactedA, (LPCSTR short_path, LPSTR long_path, DWORD length, HANDLE transaction))
{
    LPWSTR wide;
    if (!wide_name(short_path, &wide))
        return 0;
    DWORD n = GetLongPathNameTransactedW(wide, nullptr, 0, transaction);
    free_name(wide);
    return n;
}

EXPORT_STD(BOOL, MoveFileTransactedW, (LPCWSTR existing, LPCWSTR target, LPPROGRESS_ROUTINE progress, LPVOID data,
                                       DWORD flags, HANDLE transaction))
{
    transaction_error(transaction);
    return FALSE;
}

EXPORT_STD(BOOL, MoveFileTransactedA, (LPCSTR existing, LPCSTR target, LPPROGRESS_ROUTINE progress, LPVOID data,
                                       DWORD flags, HANDLE transaction))
{
    LPWSTR a, b = nullptr;
    if (!wide_name(existing, &a))
        return FALSE;
    /* the new name may be absent for a delayed delete. */
    if (target && !wide_name(target, &b)) {
        free_name(a);
        return FALSE;
    }
    BOOL ok = MoveFileTransactedW(a, b, progress, data, flags, transaction);
    free_name(a);
    free_name(b);
    return ok;
}

EXPORT_STD(BOOL, RemoveDirectoryTransactedW, (LPCWSTR path, HANDLE transaction))
{
    transaction_error(transaction);
    return FALSE;
}

EXPORT_STD(BOOL, RemoveDirectoryTransactedA, (LPCSTR path, HANDLE transaction))
{
    LPWSTR wide;
    if (!wide_name(path, &wide))
        return FALSE;
    BOOL ok = RemoveDirectoryTransactedW(wide, transaction);
    free_name(wide);
    return ok;
}

EXPORT_STD(BOOL, SetFileAttributesTransactedW, (LPCWSTR name, DWORD attributes, HANDLE transaction))
{
    transaction_error(transaction);
    return FALSE;
}

EXPORT_STD(BOOL, SetFileAttributesTransactedA, (LPCSTR name, DWORD attributes, HANDLE transaction))
{
    LPWSTR wide;
    if (!wide_name(name, &wide))
        return FALSE;
    BOOL ok = SetFileAttributesTransactedW(wide, attributes, transaction);
    free_name(wide);
    return ok;
}
