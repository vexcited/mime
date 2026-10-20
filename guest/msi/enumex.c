/*
 * the calls of Windows Installer 3.0 and later that name a context: enumerating the products and the patches of the
 * machine or of a user, the version of a file, whether a product was installed with elevation. Mime keeps one catalog
 * of products per user and one for the machine (see catalog.c), which is what these read.
 */
#include "msi_private.h"
#include <mime/win32/winver.h>

static constexpr WCHAR g_catalog[] = L"Software\\Mime\\Installer\\Products";
static constexpr WCHAR g_everyone[] = L"S-1-1-0";

/* whether the sid asked for takes in the user of this process: none, everyone, or the user. */
static BOOL sid_takes_user(PCWSTR sid, PCWSTR user)
{
    return !sid || !wcscmp(sid, g_everyone) || !_wcsicmp(sid, user);
}

static UINT copy_sid(PCWSTR text, LPWSTR out, LPDWORD size)
{
    if (!out && !size)
        return ERROR_SUCCESS;
    if (!size)
        return ERROR_INVALID_PARAMETER;
    DWORD needed = (DWORD)wcslen(text);
    if (!out || *size <= needed) {
        *size = needed;
        return out ? ERROR_MORE_DATA : ERROR_SUCCESS;
    }
    memcpy(out, text, ((SIZE_T)needed + 1) * sizeof(WCHAR));
    *size = needed;
    return ERROR_SUCCESS;
}

/*
 * the products of the machine and then the ones of the user, each key of a catalog one product. The index counts the
 * products that pass the filters.
 */
UINT WINAPI MsiEnumProductsExW(LPCWSTR product, LPCWSTR sid, DWORD contexts, DWORD index, WCHAR installed[39],
                               MSIINSTALLCONTEXT *context, LPWSTR user_sid, LPDWORD sid_size)
{
    WCHAR wanted[33], user[256];
    if ((product && !MimeMsiPackGuid(product, wanted)) || !contexts || (contexts & ~(DWORD)MSIINSTALLCONTEXT_ALL) ||
        (sid && !*sid))
        return ERROR_INVALID_PARAMETER;
    UINT error = MimeMsiCurrentSid(user);
    if (error)
        return error;
    static const struct {
        HKEY Root;
        MSIINSTALLCONTEXT Context;
    } roots[] = {{HKEY_LOCAL_MACHINE, MSIINSTALLCONTEXT_MACHINE}, {HKEY_CURRENT_USER, MSIINSTALLCONTEXT_USERUNMANAGED}};
    for (ULONG i = 0; i < RTL_NUMBER_OF(roots); i++) {
        if (!(contexts & roots[i].Context))
            continue;
        if (roots[i].Context != MSIINSTALLCONTEXT_MACHINE && !sid_takes_user(sid, user))
            continue;
        HKEY catalog;
        error = RegOpenKeyExW(roots[i].Root, g_catalog, 0, KEY_READ, &catalog);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            continue;
        if (error)
            return error;
        for (DWORD entry = 0;; entry++) {
            WCHAR packed[33], code[39];
            DWORD length = RTL_NUMBER_OF(packed);
            LSTATUS status = RegEnumKeyExW(catalog, entry, packed, &length, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS)
                break;
            if (status || !MimeMsiUnpackGuid(packed, code)) {
                RegCloseKey(catalog);
                return ERROR_BAD_CONFIGURATION;
            }
            if (product && _wcsicmp(packed, wanted))
                continue;
            if (index) {
                index--;
                continue;
            }
            RegCloseKey(catalog);
            if (installed)
                memcpy(installed, code, sizeof(code));
            if (context)
                *context = roots[i].Context;
            return copy_sid(roots[i].Context == MSIINSTALLCONTEXT_MACHINE ? L"" : user, user_sid, sid_size);
        }
        RegCloseKey(catalog);
    }
    return ERROR_NO_MORE_ITEMS;
}

UINT WINAPI MsiEnumProductsW(DWORD index, LPWSTR product)
{
    if (!product)
        return ERROR_INVALID_PARAMETER;
    WCHAR code[39];
    UINT error = MsiEnumProductsExW(nullptr, g_everyone, MSIINSTALLCONTEXT_ALL, index, code, nullptr, nullptr, nullptr);
    if (!error)
        memcpy(product, code, sizeof(code));
    return error;
}

/* the patches of the products that pass the filters, in the order of the products. */
UINT WINAPI MsiEnumPatchesExW(LPCWSTR product, LPCWSTR sid, DWORD contexts, DWORD filter, DWORD index, WCHAR patch[39],
                              WCHAR target[39], MSIINSTALLCONTEXT *context, LPWSTR user_sid, LPDWORD sid_size)
{
    if (!filter || (filter & ~(DWORD)MSIPATCHSTATE_ALL))
        return ERROR_INVALID_PARAMETER;
    if (!(filter & MSIPATCHSTATE_APPLIED))
        return ERROR_NO_MORE_ITEMS;
    for (DWORD at = 0;; at++) {
        WCHAR owner[39], user[256];
        MSIINSTALLCONTEXT owner_context;
        DWORD user_size = RTL_NUMBER_OF(user);
        UINT error = MsiEnumProductsExW(product, sid, contexts, at, owner, &owner_context, user, &user_size);
        if (error)
            return error;
        for (DWORD which = 0;; which++) {
            WCHAR code[39], transforms[256];
            DWORD size = RTL_NUMBER_OF(transforms);
            error = MsiEnumPatchesW(owner, which, code, transforms, &size);
            if (error == ERROR_NO_MORE_ITEMS || error == ERROR_UNKNOWN_PRODUCT)
                break;
            if (error)
                return error;
            if (index) {
                index--;
                continue;
            }
            if (patch)
                memcpy(patch, code, sizeof(code));
            if (target)
                memcpy(target, owner, sizeof(owner));
            if (context)
                *context = owner_context;
            return copy_sid(user, user_sid, sid_size);
        }
    }
}

UINT WINAPI MsiGetPatchInfoExW(LPCWSTR patch, LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context, LPCWSTR property,
                               LPWSTR value, LPDWORD size)
{
    WCHAR packed[33];
    if (!patch || !product || !property || !MimeMsiPackGuid(patch, packed) || !MimeMsiPackGuid(product, packed) ||
        !context || (context & ~(DWORD)MSIINSTALLCONTEXT_ALL) || (value && !size))
        return ERROR_INVALID_PARAMETER;
    WCHAR owner[39];
    MSIINSTALLCONTEXT found;
    UINT error = MsiEnumProductsExW(product, sid, context, 0, owner, &found, nullptr, nullptr);
    if (error == ERROR_NO_MORE_ITEMS)
        return ERROR_UNKNOWN_PRODUCT;
    if (error)
        return error;
    return MsiGetPatchInfoW(patch, property, value, size);
}

/* the patches are applied in the order given: nothing in the catalog says more. */
UINT WINAPI MsiDeterminePatchSequenceW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context, DWORD count,
                                       MSIPATCHSEQUENCEINFOW *patches)
{
    if (!product || !count || !patches)
        return ERROR_INVALID_PARAMETER;
    WCHAR owner[39];
    MSIINSTALLCONTEXT found;
    UINT error = MsiEnumProductsExW(product, sid, context, 0, owner, &found, nullptr, nullptr);
    if (error == ERROR_NO_MORE_ITEMS)
        return ERROR_UNKNOWN_PRODUCT;
    if (error)
        return error;
    for (DWORD i = 0; i < count; i++) {
        patches[i].dwOrder = i;
        patches[i].uStatus = ERROR_SUCCESS;
    }
    return ERROR_SUCCESS;
}

/* patches are not read for their descriptions: no file here is a patch package the installer applies. */
UINT WINAPI MsiExtractPatchXMLDataW(LPCWSTR patch, DWORD reserved, LPWSTR xml, LPDWORD size)
{
    if (!patch || !*patch || (xml && !size))
        return ERROR_INVALID_PARAMETER;
    if (GetFileAttributesW(patch) == INVALID_FILE_ATTRIBUTES)
        return ERROR_FILE_NOT_FOUND;
    return ERROR_PATCH_PACKAGE_INVALID;
}

/* the version of a file as four numbers, and its language: the strings are empty when the file has none. */
UINT WINAPI MsiGetFileVersionW(LPCWSTR path, LPWSTR version, LPDWORD version_size, LPWSTR language,
                               LPDWORD language_size)
{
    if (!path || (version && !version_size) || (language && !language_size))
        return ERROR_INVALID_PARAMETER;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_PATH_NOT_FOUND ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND;
    WCHAR text[32] = L"", lang[16] = L"";
    DWORD handle, bytes = GetFileVersionInfoSizeW(path, &handle);
    BYTE *block = bytes ? MimeMsiAllocate(bytes) : nullptr;
    if (block && GetFileVersionInfoW(path, 0, bytes, block)) {
        VS_FIXEDFILEINFO *fixed;
        UINT length;
        if (VerQueryValueW(block, L"\\", (void **)&fixed, &length) && length >= sizeof(*fixed))
            _snwprintf(text, RTL_NUMBER_OF(text), L"%u.%u.%u.%u", fixed->dwFileVersionMS >> 16,
                       fixed->dwFileVersionMS & 0xFFFF, fixed->dwFileVersionLS >> 16, fixed->dwFileVersionLS & 0xFFFF);
        WORD *translation;
        if (VerQueryValueW(block, L"\\VarFileInfo\\Translation", (void **)&translation, &length) && length >= 4)
            _snwprintf(lang, RTL_NUMBER_OF(lang), L"%u", translation[0]);
    }
    MimeMsiFree(block);
    if (!text[0] && !lang[0] && !bytes)
        return ERROR_FILE_INVALID;
    UINT error = ERROR_SUCCESS;
    if (version) {
        UINT copied = MimeMsiCopyString(text, version, version_size);
        if (copied)
            error = copied;
    } else if (version_size) {
        *version_size = (DWORD)wcslen(text);
    }
    if (language) {
        UINT copied = MimeMsiCopyString(lang, language, language_size);
        if (copied)
            error = copied;
    } else if (language_size) {
        *language_size = (DWORD)wcslen(lang);
    }
    return error;
}

/* a product installed for the machine was put there by someone with the rights to do it. */
UINT WINAPI MsiIsProductElevatedW(LPCWSTR product, BOOL *elevated)
{
    WCHAR packed[33];
    if (!product || !elevated || !MimeMsiPackGuid(product, packed))
        return ERROR_INVALID_PARAMETER;
    WCHAR owner[39];
    MSIINSTALLCONTEXT context;
    UINT error = MsiEnumProductsExW(product, g_everyone, MSIINSTALLCONTEXT_ALL, 0, owner, &context, nullptr, nullptr);
    if (error == ERROR_NO_MORE_ITEMS)
        return ERROR_UNKNOWN_PRODUCT;
    if (error)
        return error;
    *elevated = context == MSIINSTALLCONTEXT_MACHINE;
    return ERROR_SUCCESS;
}
