#include "msi_private.h"

/* Serialize and flush a sibling file before replacing the destination. The open writable handle follows the
 * replacement, so another writer cannot open the new image between replacement and our next transaction. */
UINT MimeMsiCommitDatabase(MIME_MSI_DATABASE *database)
{
    if (!database->Writable)
        return ERROR_FUNCTION_FAILED;
    if (!database->OutputPending && database->SavedChanges == database->Changes)
        return ERROR_SUCCESS;
    BYTE *image = nullptr;
    SIZE_T size = 0;
    MIME_MSI_IMAGE_EDIT edit = {.Regenerate = TRUE, .WordCount = -1};
    UINT error = MimeMsiBuildImage(database, &edit, &image, &size);
    if (error)
        return error;
    PWSTR directory = MimeMsiDuplicate(database->OutputPath);
    WCHAR temporary[MAX_PATH] = {};
    HANDLE file = INVALID_HANDLE_VALUE;
    if (!directory)
        error = ERROR_NOT_ENOUGH_MEMORY;
    else {
        PWSTR last = wcsrchr(directory, L'\\');
        if (!last)
            error = ERROR_INVALID_PARAMETER;
        else {
            last[1] = 0;
            if (!GetTempFileNameW(directory, L"msi", 0, temporary))
                error = GetLastError();
        }
    }
    if (!error) {
        file = CreateFileW(temporary, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                           nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            error = GetLastError();
    }
    SIZE_T at = 0;
    while (!error && at < size) {
        DWORD written = 0, count = size - at > 0x100000 ? 0x100000 : (DWORD)(size - at);
        if (!WriteFile(file, image + at, count, &written, nullptr))
            error = GetLastError();
        else if (!written)
            error = ERROR_WRITE_FAULT;
        at += written;
    }
    if (!error && !FlushFileBuffers(file))
        error = GetLastError();
    if (!error) {
        DWORD attributes = GetFileAttributesW(database->OutputPath);
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY))
            error = ERROR_ACCESS_DENIED;
    }
    /* FileRenameInformation cannot replace a destination with open handles, even our transaction handle.
     * The replacement stays open and locked across the rename. */
    if (!error && database->WriteLock) {
        CloseHandle(database->WriteLock);
        database->WriteLock = nullptr;
    }
    if (!error && !MoveFileExW(temporary, database->OutputPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
        HANDLE lock = CreateFileW(database->OutputPath, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (lock != INVALID_HANDLE_VALUE)
            database->WriteLock = lock;
    }
    if (!error) {
        if (database->WriteLock)
            CloseHandle(database->WriteLock);
        database->WriteLock = file;
        file = INVALID_HANDLE_VALUE;
        database->SavedChanges = database->Changes;
        database->OutputPending = FALSE;
    }
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (error && temporary[0])
        DeleteFileW(temporary);
    MimeMsiFree(directory);
    MimeMsiFree(image);
    return error;
}
