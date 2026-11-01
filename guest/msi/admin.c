/* the InstallAdminPackage action: the package database goes to the administrative image beside its files. */
#include "msi_private.h"

/* whether a path is a full one, on a drive or a share. */
static BOOL full_path(PCWSTR path)
{
    return (path[0] && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) || (path[0] == L'\\' && path[1] == L'\\');
}

/* the properties the image keeps, as NAME="value" words in the form of a command line. */
static UINT admin_properties(MIME_MSI_PACKAGE *package, PWSTR *out)
{
    PCWSTR saved = MimeMsiGetProperty(package, L"AdminProperties");
    SIZE_T capacity = 64;
    for (PCWSTR p = saved; *p;) {
        PCWSTR end = wcschr(p, L';');
        SIZE_T length = end ? (SIZE_T)(end - p) : wcslen(p);
        WCHAR name[128];
        if (length && length < RTL_NUMBER_OF(name)) {
            memcpy(name, p, length * sizeof(WCHAR));
            name[length] = 0;
            capacity += length + 2 * wcslen(MimeMsiGetProperty(package, name)) + 8;
        }
        if (!end)
            break;
        p = end + 1;
    }
    PWSTR text = MimeMsiAllocate((capacity + 32) * sizeof(WCHAR));
    if (!text)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T at = 0;
    UINT error = MimeMsiSetProperty(package, L"IsAdminPackage", L"1");
    PWSTR list = MimeMsiAllocate((wcslen(saved) + 32) * sizeof(WCHAR));
    if (!list || error) {
        MimeMsiFree(list);
        MimeMsiFree(text);
        return error ? error : ERROR_NOT_ENOUGH_MEMORY;
    }
    wcscpy(list, saved);
    if (*list)
        wcscat(list, L";");
    wcscat(list, L"IsAdminPackage");
    for (PCWSTR p = list; *p;) {
        PCWSTR end = wcschr(p, L';');
        SIZE_T length = end ? (SIZE_T)(end - p) : wcslen(p);
        WCHAR name[128];
        if (length && length < RTL_NUMBER_OF(name)) {
            memcpy(name, p, length * sizeof(WCHAR));
            name[length] = 0;
            at += (SIZE_T)_snwprintf(text + at, capacity + 32 - at, L"%s=\"", name);
            for (PCWSTR value = MimeMsiGetProperty(package, name); *value; value++) {
                if (*value == L'"')
                    text[at++] = L'"';
                text[at++] = *value;
            }
            text[at++] = L'"';
            text[at++] = L' ';
            text[at] = 0;
        }
        if (!end)
            break;
        p = end + 1;
    }
    MimeMsiFree(list);
    *out = text;
    return ERROR_SUCCESS;
}

UINT MimeMsiInstallAdminPackage(MIME_MSI_PACKAGE *package)
{
    if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
        return ERROR_FUNCTION_NOT_CALLED;
    PCWSTR original = MimeMsiGetProperty(package, L"OriginalDatabase");
    if (!full_path(original)) {
        DbgPrint("msi: the OriginalDatabase %ws is not a full path\n", original);
        return ERROR_INSTALL_FAILURE;
    }
    BOOL patching = *MimeMsiGetProperty(package, L"PATCH") != 0;
    PWSTR target = nullptr, saved = nullptr;
    UINT error = ERROR_SUCCESS;
    static const WCHAR signature[] = L"\x0005" L"DigitalSignature";
    WCHAR (*removals)[32] = nullptr;
    ULONG removal_count = 0;
    BYTE *image = nullptr;
    SIZE_T size = 0;
    if (patching) {
        /* a patch changes the image in place. */
        if (!package->AdminImage)
            return ERROR_INSTALL_PACKAGE_INVALID;
        target = MimeMsiDuplicate(original);
        if (!target)
            return ERROR_NOT_ENOUGH_MEMORY;
    } else {
        PCWSTR name = wcsrchr(original, L'\\');
        PCWSTR directory = MimeMsiGetProperty(package, L"TARGETDIR");
        if (!*directory)
            return ERROR_INSTALL_FAILURE;
        target = MimeMsiJoinPath(directory, name ? name + 1 : original);
        if (!target)
            return ERROR_NOT_ENOUGH_MEMORY;
        if (!_wcsicmp(target, original)) {
            MimeMsiFree(target);
            return ERROR_INSTALL_FAILURE;
        }
        error = admin_properties(package, &saved);
    }
    MIME_MSI_IMAGE_EDIT edit = {.WordCount = -1, .Regenerate = patching};
    MIME_MSI_TABLE *media = nullptr;
    if (!error && !patching) {
        /* the embedded cabinets are unpacked into the image, so its file does not carry them. */
        error = MimeMsiLoadTable(package->Database, L"Media", &media);
        if (error == ERROR_BAD_QUERY_SYNTAX) {
            media = nullptr;
            error = ERROR_SUCCESS;
        }
        ULONG column = media ? MimeMsiColumn(media, L"Cabinet", TRUE) : UINT32_MAX;
        removals = MimeMsiAllocate(((SIZE_T)(media ? media->Rows : 0) + 1) * sizeof(*removals));
        if (!error && !removals)
            error = ERROR_NOT_ENOUGH_MEMORY;
        for (ULONG row = 0; !error && column != UINT32_MAX && row < media->Rows; row++) {
            PCWSTR cabinet = MimeMsiText(package->Database, media, row, column);
            if (cabinet && cabinet[0] == L'#' && cabinet[1] && MimeMsiEncodeName(cabinet + 1, FALSE, removals[removal_count]))
                removal_count++;
        }
        edit.WordCount = 4 | (*MimeMsiGetProperty(package, L"SHORTFILENAMES") ? 1 : 0);
        edit.LastAuthor = MimeMsiGetProperty(package, L"LogonUser");
        edit.SaveTime = TRUE;
        edit.AdminProperties = saved;
    } else if (!error) {
        PCWSTR code = MimeMsiGetProperty(package, L"PATCHNEWPACKAGECODE");
        PCWSTR subject = MimeMsiGetProperty(package, L"PATCHNEWSUMMARYSUBJECT");
        PCWSTR comments = MimeMsiGetProperty(package, L"PATCHNEWSUMMARYCOMMENTS");
        edit.Revision = *code ? code : nullptr;
        edit.Subject = *subject ? subject : nullptr;
        edit.Comments = *comments ? comments : nullptr;
        removals = MimeMsiAllocate(sizeof(*removals));
        if (!removals)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error) {
        /* the signature no longer matches the changed file. */
        memset(removals[removal_count], 0, sizeof(removals[0]));
        for (ULONG i = 0; i < 31 && signature[i]; i++)
            removals[removal_count][i] = signature[i];
        removal_count++;
        edit.Remove = (const WCHAR (*)[32])removals;
        edit.RemoveCount = removal_count;
        error = MimeMsiBuildImage(package->Database, &edit, &image, &size);
    }
    if (!error)
        error = MimeMsiWriteInstalledFile(package, image, size, target);
    MimeMsiFree(image);
    MimeMsiFree(removals);
    MimeMsiFree(saved);
    MimeMsiFree(target);
    return error;
}
