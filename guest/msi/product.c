#include "msi_private.h"
#include <mime/ntrtl.h>

static constexpr BYTE guid_positions[32] = {
    8, 7, 6, 5, 4, 3, 2, 1, 13, 12, 11, 10, 18, 17, 16, 15,
    21, 20, 23, 22, 26, 25, 28, 27, 30, 29, 32, 31, 34, 33, 36, 35
};

static WCHAR hex_character(WCHAR ch)
{
    if (ch >= L'a' && ch <= L'f')
        ch -= L'a' - L'A';
    return (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'F') ? ch : 0;
}

BOOL MimeMsiPackGuid(PCWSTR guid, WCHAR packed[33])
{
    if (!guid || wcslen(guid) != 38 || guid[0] != L'{' || guid[37] != L'}' ||
        guid[9] != L'-' || guid[14] != L'-' || guid[19] != L'-' || guid[24] != L'-')
        return FALSE;
    for (ULONG i = 0; i < 32; i++)
        if (!(packed[i] = hex_character(guid[guid_positions[i]])))
            return FALSE;
    packed[32] = 0;
    return TRUE;
}

BOOL MimeMsiUnpackGuid(PCWSTR packed, WCHAR guid[39])
{
    if (wcslen(packed) != 32)
        return FALSE;
    wcscpy(guid, L"{00000000-0000-0000-0000-000000000000}");
    for (ULONG i = 0; i < 32; i++)
        if (!(guid[guid_positions[i]] = hex_character(packed[i])))
            return FALSE;
    return TRUE;
}

static UINT open_catalogs(PCWSTR category, PCWSTR code, HKEY keys[3])
{
    static const PCWSTR prefixes[] = {
        L"Software\\Microsoft\\Installer\\",
        L"Software\\Classes\\Installer\\",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Installer\\"
    };
    for (ULONG i = 0; i < 3; i++) {
        WCHAR path[256];
        _snwprintf(path, RTL_NUMBER_OF(path), L"%s%s%s%s", prefixes[i], category, code ? L"\\" : L"", code ? code : L"");
        LSTATUS error = RegOpenKeyExW(i ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, path, 0, KEY_READ, &keys[i]);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            continue;
        if (error)
            return error;
    }
    return ERROR_SUCCESS;
}

static BOOL catalog_contains(HKEY key, PCWSTR name, BOOL values)
{
    if (!key)
        return FALSE;
    if (values)
        return RegQueryValueExW(key, name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    HKEY child;
    if (RegOpenKeyExW(key, name, 0, KEY_READ, &child))
        return FALSE;
    RegCloseKey(child);
    return TRUE;
}

static UINT enumerate_catalog(PCWSTR category, PCWSTR code, DWORD index, LPWSTR out)
{
    HKEY keys[3] = {};
    UINT error = open_catalogs(category, code, keys);
    if (error)
        goto done;
    error = ERROR_NO_MORE_ITEMS;
    for (ULONG i = 0; i < RTL_NUMBER_OF(keys); i++) {
        if (!keys[i])
            continue;
        for (DWORD entry = 0;; entry++) {
            WCHAR packed[33], guid[39];
            DWORD length = RTL_NUMBER_OF(packed);
            LSTATUS status = code ? RegEnumValueW(keys[i], entry, packed, &length, nullptr, nullptr, nullptr, nullptr) :
                                   RegEnumKeyExW(keys[i], entry, packed, &length, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS)
                break;
            if (status) {
                error = status == ERROR_MORE_DATA ? ERROR_BAD_CONFIGURATION : (UINT)status;
                goto done;
            }
            if (!length)
                continue;
            if (!MimeMsiUnpackGuid(packed, guid)) {
                error = ERROR_BAD_CONFIGURATION;
                goto done;
            }
            BOOL duplicate = FALSE;
            for (ULONG previous = 0; previous < i; previous++)
                if (catalog_contains(keys[previous], packed, code != nullptr))
                    duplicate = TRUE;
            if (duplicate)
                continue;
            if (index) {
                index--;
                continue;
            }
            memcpy(out, guid, sizeof(guid));
            error = ERROR_SUCCESS;
            goto done;
        }
    }
done:
    for (ULONG i = 0; i < RTL_NUMBER_OF(keys); i++)
        if (keys[i])
            RegCloseKey(keys[i]);
    return error;
}

UINT WINAPI MsiEnumRelatedProductsW(LPCWSTR upgrade, DWORD reserved, DWORD index, LPWSTR product)
{
    MIME_REMOTE(PROXY_EnumRelatedProductsW, UINT, MIME_ARG(upgrade), MIME_ARG(reserved), MIME_ARG(index), MIME_ARG(product));
    WCHAR packed[33];
    if (reserved || !product || !MimeMsiPackGuid(upgrade, packed))
        return ERROR_INVALID_PARAMETER;
    return enumerate_catalog(L"UpgradeCodes", packed, index, product);
}

/* the products installed for the user or for the machine, in the order the catalogs list them. */
UINT WINAPI MsiEnumProductsW(DWORD index, LPWSTR product)
{
    if (!product)
        return ERROR_INVALID_PARAMETER;
    return enumerate_catalog(L"Products", nullptr, index, product);
}

/* removing the patches of a product is not done: nothing is applied in a way that can be taken apart. */
UINT WINAPI MsiRemovePatchesW(LPCWSTR patches, LPCWSTR product, INSTALLTYPE type, LPCWSTR command)
{
    WCHAR packed[33];
    if (!patches || !*patches || !product || !MimeMsiPackGuid(product, packed) || type != INSTALLTYPE_SINGLE_INSTANCE)
        return ERROR_INVALID_PARAMETER;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

static UINT registry_text(HKEY key, PCWSTR name, PWSTR *out, DWORD expected)
{
    DWORD bytes = 0, type;
    UINT error = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (error)
        return error;
    if (type != expected || bytes % sizeof(WCHAR) || bytes > 1024 * 1024)
        return ERROR_BAD_CONFIGURATION;
    PWSTR text = MimeMsiAllocate((SIZE_T)bytes + 2 * sizeof(WCHAR));
    if (!text)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD capacity = bytes;
    error = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)text, &capacity);
    if (!error && (type != expected || capacity % sizeof(WCHAR)))
        error = ERROR_BAD_CONFIGURATION;
    if (error)
        MimeMsiFree(text);
    else
        *out = text;
    return error;
}

static UINT catalog_string(PCWSTR value, LPWSTR out, LPDWORD size)
{
    if (out)
        return MimeMsiCopyString(value, out, size);
    if (size)
        *size = (DWORD)wcslen(value);
    return ERROR_SUCCESS;
}

UINT MimeMsiCurrentSid(WCHAR sid[256])
{
    HANDLE token;
    NTSTATUS status = NtOpenProcessToken(NtCurrentProcess(), 8, &token);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    union { ULONG_PTR Alignment; BYTE Bytes[256]; } buffer;
    ULONG returned;
    status = NtQueryInformationToken(token, 1, &buffer, sizeof(buffer), &returned);
    NtClose(token);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    const BYTE *value;
    memcpy(&value, buffer.Bytes, sizeof(value));
    if (value < buffer.Bytes || value > buffer.Bytes + sizeof(buffer) - 8 ||
        value[1] > 15 || value + 8 + 4 * value[1] > buffer.Bytes + sizeof(buffer))
        return ERROR_BAD_CONFIGURATION;
    ULONGLONG authority = 0;
    for (ULONG i = 2; i < 8; i++)
        authority = authority * 256 + value[i];
    _snwprintf(sid, 256, L"S-%u-%llu", value[0], authority);
    for (ULONG i = 0; i < value[1]; i++) {
        ULONG part;
        memcpy(&part, value + 8 + i * 4, sizeof(part));
        SIZE_T used = wcslen(sid);
        _snwprintf(sid + used, 256 - used, L"-%u", part);
    }
    return ERROR_SUCCESS;
}

UINT WINAPI MsiGetPatchInfoW(LPCWSTR patch, LPCWSTR attribute, LPWSTR out, LPDWORD size)
{
    WCHAR packed[33], sid[256];
    if (!MimeMsiPackGuid(patch, packed) || !attribute || (out && !size))
        return ERROR_INVALID_PARAMETER;
    if (wcscmp(attribute, L"LocalPackage"))
        return ERROR_UNKNOWN_PROPERTY;
    UINT error = MimeMsiCurrentSid(sid);
    if (error)
        return error;
    PCWSTR contexts[] = {sid, L"S-1-5-18"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(contexts); i++) {
        WCHAR path[512];
        _snwprintf(path, RTL_NUMBER_OF(path),
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData\\%s\\Patches\\%s",
                   contexts[i], packed);
        HKEY key;
        error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            continue;
        if (error)
            return error;
        PWSTR text = nullptr;
        error = registry_text(key, attribute, &text, REG_SZ);
        RegCloseKey(key);
        if (!error)
            error = catalog_string(text, out, size);
        else if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_BAD_CONFIGURATION;
        MimeMsiFree(text);
        return error;
    }
    return ERROR_UNKNOWN_PRODUCT;
}

UINT WINAPI MsiEnumPatchesW(LPCWSTR product, DWORD index, LPWSTR patch, LPWSTR transforms, LPDWORD size)
{
    WCHAR packed[33], category[80];
    if (!MimeMsiPackGuid(product, packed) || !patch || !transforms || !size)
        return ERROR_INVALID_PARAMETER;
    _snwprintf(category, RTL_NUMBER_OF(category), L"Products\\%s\\Patches", packed);
    HKEY keys[3] = {};
    UINT error = open_catalogs(category, nullptr, keys);
    PWSTR lists[3] = {};
    if (error)
        goto done;
    for (ULONG i = 0; i < RTL_NUMBER_OF(keys); i++) {
        if (!keys[i])
            continue;
        error = registry_text(keys[i], L"Patches", &lists[i], REG_MULTI_SZ);
        if (error == ERROR_FILE_NOT_FOUND)
            continue;
        if (error)
            goto done;
        for (PCWSTR entry = lists[i]; *entry; entry += wcslen(entry) + 1) {
            WCHAR guid[39];
            if (!MimeMsiUnpackGuid(entry, guid)) {
                error = ERROR_BAD_CONFIGURATION;
                goto done;
            }
            BOOL duplicate = FALSE;
            for (ULONG previous = 0; previous < i; previous++)
                for (PCWSTR old = lists[previous]; old && *old; old += wcslen(old) + 1)
                    if (!_wcsicmp(old, entry))
                        duplicate = TRUE;
            if (duplicate)
                continue;
            if (index) {
                index--;
                continue;
            }
            PWSTR text = nullptr;
            error = registry_text(keys[i], entry, &text, REG_SZ);
            if (!error) {
                DWORD capacity = *size;
                error = MimeMsiCopyString(text, transforms, size);
                if (!error)
                    *size = capacity;
                memcpy(patch, guid, sizeof(guid));
            } else if (error == ERROR_FILE_NOT_FOUND)
                error = ERROR_BAD_CONFIGURATION;
            MimeMsiFree(text);
            goto done;
        }
    }
    error = ERROR_NO_MORE_ITEMS;
done:
    for (ULONG i = 0; i < RTL_NUMBER_OF(keys); i++) {
        if (keys[i])
            RegCloseKey(keys[i]);
        MimeMsiFree(lists[i]);
    }
    return error;
}
