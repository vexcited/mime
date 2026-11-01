#include "msi_private.h"

static UINT summary_text(MSIHANDLE summary, UINT property, PWSTR *out)
{
    UINT type;
    DWORD size = 0;
    WCHAR empty[1];
    *out = nullptr;
    UINT error = MsiSummaryInfoGetPropertyW(summary, property, &type, nullptr, nullptr, empty, &size);
    if ((error && error != ERROR_MORE_DATA) || type != 30 || !size)
        return ERROR_PATCH_PACKAGE_INVALID;
    *out = MimeMsiAllocate(((SIZE_T)size + 1) * sizeof(WCHAR));
    if (!*out)
        return ERROR_NOT_ENOUGH_MEMORY;
    size++;
    error = MsiSummaryInfoGetPropertyW(summary, property, &type, nullptr, nullptr, *out, &size);
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

/* the patch is applied to the image as an administrative installation of it with the patch named. */
static UINT patch_image(PCWSTR patch, PCWSTR image, PCWSTR command)
{
    DWORD characters = GetFullPathNameW(patch, 0, nullptr, nullptr);
    PWSTR full = characters ? MimeMsiAllocate((SIZE_T)characters * sizeof(WCHAR)) : nullptr;
    if (!full || !GetFullPathNameW(patch, characters, full, nullptr)) {
        MimeMsiFree(full);
        return full ? ERROR_PATCH_PACKAGE_OPEN_FAILED : ERROR_NOT_ENOUGH_MEMORY;
    }
    SIZE_T capacity = wcslen(full) + (command ? wcslen(command) : 0) + 48;
    PWSTR line = MimeMsiAllocate(capacity * sizeof(WCHAR));
    UINT error = ERROR_NOT_ENOUGH_MEMORY;
    if (line) {
        _snwprintf(line, capacity, L"%s ACTION=ADMIN PATCH=\"%s\"", command ? command : L"", full);
        error = MsiInstallProductW(image, line);
        MimeMsiFree(line);
    }
    MimeMsiFree(full);
    return error;
}

/* each installed product the patch is for is reinstalled with it, those it is not for are passed over. */
static UINT patch_products(PCWSTR patches, PCWSTR single, PCWSTR targets, PCWSTR command)
{
    /* the engine reads the patches from wherever the caller is not. */
    SIZE_T capacity = wcslen(patches) + (command ? wcslen(command) : 0) + 32 + 260 * 8;
    PWSTR line = MimeMsiAllocate(capacity * sizeof(WCHAR));
    if (!line)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T at = (SIZE_T)_snwprintf(line, capacity, L"%s PATCH=\"", command ? command : L"");
    for (PCWSTR cursor = patches; *cursor;) {
        PCWSTR end = wcschr(cursor, L';');
        WCHAR one[MAX_PATH], full[MAX_PATH];
        SIZE_T length = end ? (SIZE_T)(end - cursor) : wcslen(cursor);
        if (length >= RTL_NUMBER_OF(one)) {
            MimeMsiFree(line);
            return ERROR_PATCH_PACKAGE_OPEN_FAILED;
        }
        memcpy(one, cursor, length * sizeof(WCHAR));
        one[length] = 0;
        if (length && GetFullPathNameW(one, RTL_NUMBER_OF(full), full, nullptr) && wcslen(full) + at + 8 < capacity) {
            at += (SIZE_T)_snwprintf(line + at, capacity - at, L"%s%s", line[at - 1] == L'"' ? L"" : L";", full);
        }
        cursor = end ? end + 1 : cursor + length;
    }
    line[at++] = L'"';
    line[at] = 0;
    BOOL matched = FALSE;
    UINT error = ERROR_SUCCESS;
    for (PCWSTR cursor = targets; !error && *cursor; cursor += *cursor == L';' ? 1 : 38) {
        if (*cursor == L';')
            continue;
        WCHAR code[39];
        memcpy(code, cursor, 38 * sizeof(WCHAR));
        code[38] = 0;
        if (single && _wcsicmp(single, code))
            continue;
        INSTALLSTATE state = MsiQueryProductStateW(code);
        if (state == INSTALLSTATE_DEFAULT) {
            matched = TRUE;
            error = MsiConfigureProductExW(code, 0, INSTALLSTATE_DEFAULT, line);
        } else if (state == INSTALLSTATE_ADVERTISED) {
            matched = TRUE;
            error = ERROR_CALL_NOT_IMPLEMENTED;
        }
        /* a product the patch does not fit is not the end of the others. */
        if (error == ERROR_PATCH_TARGET_NOT_FOUND)
            error = ERROR_SUCCESS;
    }
    MimeMsiFree(line);
    return error ? error : matched ? ERROR_SUCCESS : ERROR_PATCH_TARGET_NOT_FOUND;
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
    PWSTR revision = nullptr, targets = nullptr;
    error = summary_text(summary, 9, &revision);
    if (!error)
        error = summary_text(summary, 7, &targets);
    if (!error && (!guid_list(revision, FALSE) || !guid_list(targets, TRUE)))
        error = ERROR_PATCH_PACKAGE_INVALID;
    MimeMsiFree(revision);
    MsiCloseHandle(summary);
    if (!error && type == INSTALLTYPE_NETWORK_IMAGE)
        error = patch_image(path, package, command);
    else if (!error)
        error = patch_products(path, type == INSTALLTYPE_SINGLE_INSTANCE ? package : nullptr, targets, command);
    MimeMsiFree(targets);
    return error;
}

UINT WINAPI MsiApplyMultiplePatchesW(LPCWSTR patches, LPCWSTR product, LPCWSTR command)
{
    if (!patches || !*patches)
        return ERROR_INVALID_PARAMETER;
    WCHAR packed[33];
    if (product && *product && !MimeMsiPackGuid(product, packed))
        return ERROR_INVALID_PARAMETER;
    PWSTR copy = MimeMsiDuplicate(patches);
    if (!copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    /* every patch must be one of the installer, and the first says which products are to have them. */
    PWSTR targets = nullptr;
    UINT error = ERROR_SUCCESS;
    for (PWSTR cursor = copy; !error && cursor;) {
        PWSTR end = wcschr(cursor, L';');
        if (end)
            *end = 0;
        if (*cursor) {
            MIME_MSI_DATABASE *storage;
            error = MimeMsiOpenStorageFile(cursor, 0x86, &storage);
            MSIHANDLE summary = 0;
            if (error)
                error = error == ERROR_NOT_ENOUGH_MEMORY ? error :
                        error == ERROR_INSTALL_PACKAGE_INVALID ? ERROR_PATCH_PACKAGE_INVALID : ERROR_PATCH_PACKAGE_OPEN_FAILED;
            else {
                AcquireSRWLockExclusive(&MimeMsiLock);
                error = MimeMsiCreateSummary(storage, &summary);
                ReleaseSRWLockExclusive(&MimeMsiLock);
                storage->Object.Destroy(&storage->Object);
                error = error ? (error == ERROR_NOT_ENOUGH_MEMORY ? error : ERROR_PATCH_PACKAGE_INVALID) : ERROR_SUCCESS;
            }
            PWSTR list = nullptr;
            if (!error)
                error = summary_text(summary, 7, &list);
            if (!error && !guid_list(list, TRUE))
                error = ERROR_PATCH_PACKAGE_INVALID;
            if (summary)
                MsiCloseHandle(summary);
            if (!targets)
                targets = list;
            else
                MimeMsiFree(list);
        }
        cursor = end ? end + 1 : nullptr;
    }
    if (!error)
        error = patch_products(patches, product && *product ? product : nullptr, targets, command);
    MimeMsiFree(targets);
    MimeMsiFree(copy);
    return error;
}

/*
 * the patches of an installation's PATCH property. each is a patch class file whose summary names the products it
 * applies to in the template, and the transforms it holds in the last author, as storages written with a leading
 * colon. the transforms apply to the package database in that order, and the file stays with the database since the
 * transforms' Media cabinet is a stream of it.
 */
static BOOL listed(PCWSTR list, PCWSTR code)
{
    SIZE_T length = wcslen(code);
    while (*list) {
        PCWSTR end = wcschr(list, L';');
        SIZE_T part = end ? (SIZE_T)(end - list) : wcslen(list);
        if (part == length && !_wcsnicmp(list, code, length))
            return TRUE;
        if (!end)
            break;
        list = end + 1;
    }
    return FALSE;
}

static UINT apply_one(MIME_MSI_DATABASE *database, PCWSTR product, PCWSTR path)
{
    MIME_MSI_DATABASE *patch;
    UINT error = MimeMsiOpenStorageFile(path, 0x86, &patch);
    if (error)
        return error == ERROR_INSTALL_PACKAGE_INVALID ? ERROR_PATCH_PACKAGE_INVALID : ERROR_PATCH_PACKAGE_OPEN_FAILED;
    MIME_MSI_SUMMARY_VALUE targets = {}, transforms = {};
    error = MimeMsiSummaryProperty(patch, 7, &targets);
    if (!error)
        error = MimeMsiSummaryProperty(patch, 8, &transforms);
    if (error || targets.Type != 30 || transforms.Type != 30 || !targets.Text || !transforms.Text) {
        MimeMsiFree(targets.Text);
        MimeMsiFree(transforms.Text);
        patch->Object.Destroy(&patch->Object);
        return ERROR_PATCH_PACKAGE_INVALID;
    }
    /* held while the transforms are open, since they need the file. */
    patch->Object.References++;
    if (product && !listed(targets.Text, product))
        error = ERROR_PATCH_TARGET_NOT_FOUND;
    PWSTR cursor = transforms.Text;
    while (!error && *cursor) {
        PWSTR end = wcschr(cursor, L';');
        if (end)
            *end = 0;
        /* only transforms stored inside the patch are supported. */
        if (*cursor != L':' || !cursor[1]) {
            error = ERROR_CALL_NOT_IMPLEMENTED;
            break;
        }
        MIME_MSI_DATABASE *transform;
        error = MimeMsiOpenSubStorage(patch, cursor + 1, &transform);
        if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_PATCH_PACKAGE_INVALID;
        if (!error) {
            transform->Object.References++;
            error = MimeMsiApplyTransformStorage(database, transform, 0x3f);
            if (!error)
                database->PatchTransforms++;
            MimeMsiRelease(&transform->Object);
        }
        if (!end)
            break;
        cursor = end + 1;
    }
    MimeMsiFree(targets.Text);
    MimeMsiFree(transforms.Text);
    if (!error) {
        MIME_MSI_DATABASE **patches = MimeMsiAllocate(((SIZE_T)database->PatchCount + 1) * sizeof(*patches));
        if (!patches)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            memcpy(patches, database->Patches, (SIZE_T)database->PatchCount * sizeof(*patches));
            MimeMsiFree(database->Patches);
            database->Patches = patches;
            patches[database->PatchCount++] = patch;
            patch->Object.References++;
        }
    }
    MimeMsiRelease(&patch->Object);
    return error;
}

UINT MimeMsiApplyPatches(MSIHANDLE handle, PCWSTR product, PCWSTR list)
{
    PWSTR copy = MimeMsiDuplicate(list);
    if (!copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    UINT error = ERROR_SUCCESS;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    PWSTR directory = database ? MimeMsiDuplicate(database->Path) : nullptr;
    if (!database)
        error = ERROR_INVALID_HANDLE;
    else if (!directory)
        error = ERROR_NOT_ENOUGH_MEMORY;
    else {
        PWSTR separator = wcsrchr(directory, L'\\');
        if (separator)
            separator[1] = 0;
    }
    for (PWSTR cursor = copy; !error && *cursor;) {
        PWSTR end = wcschr(cursor, L';');
        if (end)
            *end = 0;
        if (*cursor) {
            SIZE_T length = wcslen(cursor);
            BOOL absolute = (length > 2 && cursor[1] == L':' && (cursor[2] == L'\\' || cursor[2] == L'/')) ||
                            (length > 1 && cursor[0] == L'\\' && cursor[1] == L'\\');
            PWSTR path = absolute ? MimeMsiDuplicate(cursor) : MimeMsiJoinPath(directory, cursor);
            if (!path)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else {
                error = apply_one(database, product, path);
                MimeMsiFree(path);
            }
        }
        if (!end)
            break;
        cursor = end + 1;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    MimeMsiFree(directory);
    MimeMsiFree(copy);
    return error;
}
