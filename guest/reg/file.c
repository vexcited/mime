/*
 * reg export and reg import: registry script files.
 */
#include "reg.h"

static MimeRegWriter g_writer;

/* the file is written next to its place and moved over it, so a failure leaves the old file. */
int DoExport(const ARGS *args)
{
    int argc = args->Count;
    PWSTR *argv = args->Items;
    if (argc == 3 && IsHelp(argv[2])) {
        Usage(OP_EXPORT);
        return 0;
    }
    if (argc < 4 || argc > 6)
        return SyntaxError(OP_EXPORT);
    TARGET target;
    if (!ParseKeyArgument(argv[2], OP_EXPORT, &target))
        return 1;
    PWSTR file = DuplicateString(argv[3]);
    TrimSpaces(file);
    BOOL force = FALSE, bad = !*file;
    for (int i = 4; i < argc && !bad; i++) {
        if (IsSwitch(argv[i], L"y"))
            force = TRUE;
        else if (!ParseView(argv[i], &target.View))
            bad = TRUE;
    }
    int code = 1;
    HKEY key = nullptr;
    HANDLE handle = INVALID_HANDLE_VALUE;
    WCHAR temporary[MAX_PATH + 16];
    temporary[0] = 0;
    if (bad) {
        SyntaxError(OP_EXPORT);
        goto done;
    }
    if (target.Remote) {
        Err(L"ERROR: A remote machine was specified.\nType \"REG EXPORT /?\" for usage.\n");
        goto done;
    }
    DWORD error = (DWORD)RegOpenKeyExW(target.RootKey, target.Subkey, 0, KEY_READ | target.View, &key);
    if (error) {
        if (error == ERROR_INVALID_HANDLE)
            Err(L"ERROR: The key selected is invalid.\n");
        else
            ReportError(error, OP_EXPORT);
        goto done;
    }
    /* a file that is there is asked about. */
    handle = CreateFileW(file, GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
        int answer;
        do
            answer = Prompt(L"File %s already exists. Overwrite (Yes/No)?", file, L"YNA", force);
        while (answer > 2);
        if (answer != 1) {
            ReportPlain(ERROR_CANCELLED, 1);
            code = 0;
            goto done;
        }
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        Err(L"ERROR: Unable to write to the file. There may be a disk or file system error.\n");
        goto done;
    }
    WCHAR directory[MAX_PATH];
    DWORD n = GetFullPathNameW(file, MAX_PATH, directory, nullptr);
    PWSTR slash = n && n < MAX_PATH ? wcsrchr(directory, L'\\') : nullptr;
    if (slash)
        *slash = 0;
    if (!slash || GetTempFileNameW(directory, L"REG", 0, temporary) == 0) {
        temporary[0] = 0;
        Err(L"ERROR: Unable to write to the file. There may be a disk or file system error.\n");
        goto done;
    }
    handle = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        Err(L"ERROR: Unable to write to the file. There may be a disk or file system error.\n");
        goto done;
    }
    MimeRegSysInitWriter(&g_writer, handle);
    MimeRegWriterBegin(&g_writer, 0);
    BOOL ok = MimeRegSysExportKey(&g_writer, key, target.Full, target.View);
    if (!MimeRegWriterFlush(&g_writer))
        ok = FALSE;
    CloseHandle(handle);
    handle = INVALID_HANDLE_VALUE;
    if (!ok || !MoveFileExW(temporary, file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        Err(L"ERROR: Unable to write to the file. There may be a disk or file system error.\n");
        goto done;
    }
    temporary[0] = 0;
    ReportSuccess();
    code = 0;
done:
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    if (temporary[0])
        DeleteFileW(temporary);
    if (key)
        RegCloseKey(key);
    Release(file);
    FreeTarget(&target);
    return code;
}

int DoImport(const ARGS *args)
{
    int argc = args->Count;
    PWSTR *argv = args->Items;
    if (argc == 3 && IsHelp(argv[2])) {
        Usage(OP_IMPORT);
        return 0;
    }
    DWORD view = 0;
    if (argc < 3 || argc > 4 || (argc == 4 && !ParseView(argv[3], &view)))
        return SyntaxError(OP_IMPORT);
    int result = 0;
    enum { OPEN = 100 };
    WCHAR path[MAX_PATH];
    PWSTR part;
    DWORD length = SearchPathW(nullptr, argv[2], nullptr, MAX_PATH, path, &part);
    result = OPEN;
    if (length && length < MAX_PATH) {
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MimeRegSysOptions options = {view, 0x2001F, TRUE};
            result = (int)MimeRegSysImport(file, &options);
            CloseHandle(file);
        }
    }
    PCWSTR message = nullptr;
    switch (result) {
    case OPEN:
        message = L"Error opening the file. There may be a disk or file system error.";
        break;
    case MIME_REG_READ_FAILED:
        message = L"Error reading the file. There may be a disk error or the file may be corrupt.";
        break;
    case MIME_REG_KEY_FAILED:
        message = L"Error accessing the registry.";
        break;
    case MIME_REG_VALUE_FAILED:
        message = L"Not all data was successfully written to the registry.\n       Some keys are open by the system or "
                  L"other processes, or you have insufficient privileges to perform this operation.";
        break;
    case MIME_REG_BAD_HEADER:
    case MIME_REG_NOT_A_SCRIPT:
        message = L"The specified file is not a registry file. You can import only registry files.";
        break;
    case MIME_REG_WRONG_VERSION:
        message = L"The specified file is not intended for use with this version of Windows.";
        break;
    }
    if (message) {
        Err(L"ERROR: %s\n", message);
        return 1;
    }
    /* the original says it on the error stream too. */
    ReportPlain(ERROR_SUCCESS, 2);
    return 0;
}
