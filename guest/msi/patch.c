#include "msi_private.h"

static UINT summary_text(MSIHANDLE summary, UINT property, PWSTR *out)
{
    UINT type;
    DWORD size = 0;
    WCHAR empty[1];
    *out = NULL;
    UINT error = MsiSummaryInfoGetPropertyW(summary, property, &type, NULL, NULL, empty, &size);
    if ((error && error != ERROR_MORE_DATA) || type != 30 || !size)
        return ERROR_PATCH_PACKAGE_INVALID;
    *out = MimeMsiAllocate(((SIZE_T)size + 1) * sizeof(WCHAR));
    if (!*out)
        return ERROR_NOT_ENOUGH_MEMORY;
    size++;
    error = MsiSummaryInfoGetPropertyW(summary, property, &type, NULL, NULL, *out, &size);
    return error ? ERROR_PATCH_PACKAGE_INVALID : ERROR_SUCCESS;
}

static BOOL guid_list(PCWSTR text, BOOL separated)
{
    SIZE_T length = wcslen(text);
    if (!length)
        return FALSE;
    while (length) {
        if (length < 38)
            return FALSE;
        WCHAR guid[39], packed[33];
        memcpy(guid, text, 38 * sizeof(WCHAR));
        guid[38] = 0;
        if (!MimeMsiPackGuid(guid, packed))
            return FALSE;
        text += 38;
        length -= 38;
        if (separated && length) {
            if (*text++ != L';' || !--length)
                return FALSE;
        }
    }
    return TRUE;
}

UINT WINAPI MsiApplyPatchW(LPCWSTR path, LPCWSTR package, INSTALLTYPE type, LPCWSTR command)
{
    WCHAR packed[33];
    if (!path || !*path || type < INSTALLTYPE_DEFAULT || type > INSTALLTYPE_SINGLE_INSTANCE ||
        (type == INSTALLTYPE_DEFAULT && package) ||
        (type == INSTALLTYPE_NETWORK_IMAGE && (!package || !*package)) ||
        (type == INSTALLTYPE_SINGLE_INSTANCE && !MimeMsiPackGuid(package, packed)))
        return ERROR_INVALID_PARAMETER;
    MIME_MSI_DATABASE *storage;
    UINT error = MimeMsiOpenStorageFile(path, 0x86, &storage);
    if (error) {
        if (error == ERROR_NOT_ENOUGH_MEMORY)
            return error;
        return error == ERROR_INSTALL_PACKAGE_INVALID ? ERROR_PATCH_PACKAGE_INVALID : ERROR_PATCH_PACKAGE_OPEN_FAILED;
    }
    MSIHANDLE summary = 0;
    AcquireSRWLockExclusive(&MimeMsiLock);
    error = MimeMsiCreateSummary(storage, &summary);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    storage->Object.Destroy(&storage->Object);
    if (error)
        return error == ERROR_NOT_ENOUGH_MEMORY ? error : ERROR_PATCH_PACKAGE_INVALID;
    PWSTR revision = NULL, targets = NULL;
    error = summary_text(summary, 9, &revision);
    if (!error)
        error = summary_text(summary, 7, &targets);
    if (!error && (!guid_list(revision, FALSE) || !guid_list(targets, TRUE)))
        error = ERROR_PATCH_PACKAGE_INVALID;
    MimeMsiFree(revision);
    MimeMsiFree(targets);
    MsiCloseHandle(summary);
    if (!error)
        error = ERROR_PATCH_PACKAGE_UNSUPPORTED;
    return error;
}
