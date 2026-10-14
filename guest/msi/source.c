#include "msi_private.h"

static SRWLOCK source_lock;

static BOOL missing(UINT error)
{
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

static UINT source_product(PCWSTR product, PCWSTR sid, MSIINSTALLCONTEXT context,
                           DWORD options, HKEY *root, WCHAR path[160])
{
    UINT error = MimeMsiCatalogPath(product, path);
    if (error || (context != MSIINSTALLCONTEXT_MACHINE && context != MSIINSTALLCONTEXT_USERMANAGED &&
                  context != MSIINSTALLCONTEXT_USERUNMANAGED) ||
        (context == MSIINSTALLCONTEXT_MACHINE && sid) ||
        (sid && (!wcscmp(sid, L"S-1-5-18") || !wcscmp(sid, L"S-1-1-0"))))
        return ERROR_INVALID_PARAMETER;
    UINT unknown = options & MSICODE_PATCH ? ERROR_UNKNOWN_PATCH : ERROR_UNKNOWN_PRODUCT;
    if (sid) {
        WCHAR current[256];
        error = MimeMsiCurrentSid(current);
        if (error)
            return error;
        if (wcscmp(current, sid))
            return unknown;
    }
    if (context == MSIINSTALLCONTEXT_USERMANAGED || (options & MSICODE_PATCH))
        return unknown;
    *root = context == MSIINSTALLCONTEXT_MACHINE ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    HKEY key;
    error = RegOpenKeyExW(*root, path, 0, KEY_READ, &key);
    if (missing(error))
        return unknown;
    if (error)
        return error;
    DWORD state, type, bytes = sizeof(state);
    error = RegQueryValueExW(key, L"State", NULL, &type, (BYTE *)&state, &bytes);
    RegCloseKey(key);
    if (error || type != REG_DWORD || bytes != sizeof(state) || state != INSTALLSTATE_DEFAULT)
        return ERROR_BAD_CONFIGURATION;
    wcscat(path, L"\\SourceList");
    return ERROR_SUCCESS;
}

static UINT write_text(MIME_MSI_PACKAGE *journal, HKEY root, PCWSTR path, PCWSTR name, PCWSTR value)
{
    SIZE_T length = wcslen(value);
    if (length >= 1024 * 1024 / sizeof(WCHAR))
        return ERROR_INVALID_PARAMETER;
    return MimeMsiWriteRegistry(journal, root, path, name, REG_SZ, value, (DWORD)(length + 1) * sizeof(WCHAR));
}

static UINT read_text(HKEY root, PCWSTR path, PCWSTR name, PWSTR *out)
{
    HKEY key;
    UINT error = RegOpenKeyExW(root, path, 0, KEY_READ, &key);
    if (!error) {
        error = MimeMsiRegistryText(key, name, out);
        RegCloseKey(key);
    }
    return error;
}

static UINT copy_text(PCWSTR text, PWSTR out, DWORD *size)
{
    if (out)
        return MimeMsiCopyString(text, out, size);
    if (size)
        *size = (DWORD)wcslen(text);
    return ERROR_SUCCESS;
}

static PWSTR normalize_source(PCWSTR source, DWORD type)
{
    SIZE_T length = wcslen(source);
    if (!length || length > 32767)
        return NULL;
    PWSTR text = MimeMsiAllocate((length + 2) * sizeof(WCHAR));
    if (!text)
        return NULL;
    memcpy(text, source, length * sizeof(WCHAR));
    WCHAR separator = type == MSISOURCETYPE_URL ? L'/' : L'\\';
    if (type == MSISOURCETYPE_NETWORK)
        for (SIZE_T i = 0; i < length; i++)
            if (text[i] == L'/')
                text[i] = L'\\';
    if (text[length - 1] != separator)
        text[length++] = separator;
    text[length] = 0;
    return text;
}

static UINT add_source(MIME_MSI_PACKAGE *journal, HKEY root, PCWSTR path, DWORD type,
                       PCWSTR source, BOOL last)
{
    if (!source || !*source || wcslen(source) > 32767)
        return ERROR_INVALID_PARAMETER;
    PWSTR normalized = normalize_source(source, type);
    if (!normalized)
        return ERROR_NOT_ENOUGH_MEMORY;
    WCHAR list[176], name[16];
    _snwprintf(list, RTL_NUMBER_OF(list), L"%s\\%s", path, type == MSISOURCETYPE_URL ? L"URL" : L"Net");
    DWORD index = 1;
    UINT error;
    for (;;) {
        _snwprintf(name, RTL_NUMBER_OF(name), L"%u", index);
        PWSTR existing = NULL;
        error = read_text(root, list, name, &existing);
        if (missing(error)) {
            error = write_text(journal, root, list, name, normalized);
            break;
        }
        BOOL equal = !error && (type == MSISOURCETYPE_URL ? !wcscmp(existing, normalized) :
                                                         !_wcsicmp(existing, normalized));
        MimeMsiFree(existing);
        if (error || equal)
            break;
        if (++index == 65536) {
            error = ERROR_BAD_CONFIGURATION;
            break;
        }
    }
    if (!error && last) {
        SIZE_T capacity = wcslen(normalized) + 32;
        PWSTR encoded = MimeMsiAllocate(capacity * sizeof(WCHAR));
        if (!encoded)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            _snwprintf(encoded, capacity, L"%c;%u;%s", type == MSISOURCETYPE_URL ? L'u' : L'n', index, normalized);
            error = write_text(journal, root, path, L"LastUsedSource", encoded);
            MimeMsiFree(encoded);
        }
    }
    MimeMsiFree(normalized);
    return error;
}

static UINT finish(MIME_MSI_PACKAGE *journal, UINT error)
{
    UINT cleanup = MimeMsiFinishTransaction(journal, !error);
    return cleanup ? cleanup : error;
}

UINT WINAPI MsiSourceListSetInfoW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context,
                                  DWORD options, LPCWSTR property, LPCWSTR value)
{
    if (!property || !value)
        return ERROR_INVALID_PARAMETER;
    BOOL last = !wcscmp(property, L"LastUsedSource");
    if (!last && wcscmp(property, L"PackageName") && wcscmp(property, L"MediaPackagePath") && wcscmp(property, L"DiskPrompt"))
        return ERROR_UNKNOWN_PROPERTY;
    DWORD type = options & ~MSICODE_PATCH;
    if (last ? type != MSISOURCETYPE_NETWORK && type != MSISOURCETYPE_URL : type != 0)
        return ERROR_INVALID_PARAMETER;
    HKEY root;
    WCHAR path[160];
    AcquireSRWLockExclusive(&source_lock);
    UINT error = source_product(product, sid, context, options, &root, path);
    MIME_MSI_PACKAGE journal = {.TransactionActive = TRUE};
    if (!error)
        error = last && *value ? add_source(&journal, root, path, type, value, TRUE) :
                                write_text(&journal, root, path, property, value);
    error = finish(&journal, error);
    ReleaseSRWLockExclusive(&source_lock);
    return error;
}

UINT WINAPI MsiSourceListGetInfoW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context,
                                  DWORD options, LPCWSTR property, LPWSTR out, LPDWORD size)
{
    if (!property || (out && !size) || (options & ~MSICODE_PATCH))
        return ERROR_INVALID_PARAMETER;
    BOOL last = !wcscmp(property, L"LastUsedSource"), type = !wcscmp(property, L"LastUsedType");
    if (!last && !type && wcscmp(property, L"PackageName") && wcscmp(property, L"MediaPackagePath") && wcscmp(property, L"DiskPrompt"))
        return ERROR_UNKNOWN_PROPERTY;
    HKEY root;
    WCHAR path[160];
    AcquireSRWLockShared(&source_lock);
    UINT error = source_product(product, sid, context, options, &root, path);
    PWSTR text = NULL;
    if (!error) {
        error = read_text(root, path, type ? L"LastUsedSource" : property, &text);
        if (missing(error))
            error = ERROR_SUCCESS;
    }
    PCWSTR value = text ? text : L"";
    WCHAR kind[2] = {0};
    if (!error && (last || type) && *value) {
        PCWSTR separator = wcslen(value) >= 3 && value[1] == L';' ? wcschr(value + 2, L';') : NULL;
        if (!separator || separator == value + 2 || !wcschr(L"num", value[0]))
            error = ERROR_BAD_CONFIGURATION;
        else {
            for (PCWSTR p = value + 2; p < separator; p++)
                if (*p < L'0' || *p > L'9')
                    error = ERROR_BAD_CONFIGURATION;
            kind[0] = value[0];
            value = type ? kind : separator + 1;
        }
    }
    if (!error)
        error = copy_text(value, out, size);
    MimeMsiFree(text);
    ReleaseSRWLockShared(&source_lock);
    return error;
}

UINT WINAPI MsiSourceListEnumSourcesW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context,
                                      DWORD options, DWORD index, LPWSTR out, LPDWORD size)
{
    DWORD type = options & ~MSICODE_PATCH;
    if ((out && !size) || (type != MSISOURCETYPE_NETWORK && type != MSISOURCETYPE_URL))
        return ERROR_INVALID_PARAMETER;
    HKEY root;
    WCHAR path[160], list[176], name[16];
    AcquireSRWLockShared(&source_lock);
    UINT error = source_product(product, sid, context, options, &root, path);
    PWSTR text = NULL;
    if (!error && index == UINT32_MAX)
        error = ERROR_NO_MORE_ITEMS;
    if (!error) {
        _snwprintf(list, RTL_NUMBER_OF(list), L"%s\\%s", path, type == MSISOURCETYPE_URL ? L"URL" : L"Net");
        _snwprintf(name, RTL_NUMBER_OF(name), L"%u", index + 1);
        error = read_text(root, list, name, &text);
        if (missing(error))
            error = ERROR_NO_MORE_ITEMS;
    }
    if (!error)
        error = copy_text(text, out, size);
    MimeMsiFree(text);
    ReleaseSRWLockShared(&source_lock);
    return error;
}

UINT WINAPI MsiSourceListAddSourceW(LPCWSTR product, LPCWSTR user, DWORD reserved, LPCWSTR source)
{
    if (reserved || !source || !*source)
        return ERROR_INVALID_PARAMETER;
    MSIINSTALLCONTEXT context = MSIINSTALLCONTEXT_MACHINE;
    if (user && *user) {
        HMODULE module = LoadLibraryW(L"advapi32.dll");
        if (!module)
            return GetLastError();
        BOOL (WINAPI *get_user)(PWSTR, DWORD *) = (void *)GetProcAddress(module, "GetUserNameW");
        WCHAR name[256], computer[256], qualified[514];
        DWORD size = RTL_NUMBER_OF(name), computer_size = RTL_NUMBER_OF(computer);
        BOOL found = get_user && get_user(name, &size) && GetComputerNameW(computer, &computer_size);
        if (found) {
            _snwprintf(qualified, RTL_NUMBER_OF(qualified), L"%s\\%s", computer, name);
            found = !_wcsicmp(user, name) || !_wcsicmp(user, qualified);
        }
        FreeLibrary(module);
        if (!found)
            return ERROR_BAD_USERNAME;
        context = MSIINSTALLCONTEXT_USERUNMANAGED;
    }
    HKEY root;
    WCHAR path[160];
    AcquireSRWLockExclusive(&source_lock);
    UINT error = source_product(product, NULL, context, 0, &root, path);
    MIME_MSI_PACKAGE journal = {.TransactionActive = TRUE};
    if (!error)
        error = add_source(&journal, root, path, MSISOURCETYPE_NETWORK, source, FALSE);
    error = finish(&journal, error);
    ReleaseSRWLockExclusive(&source_lock);
    return error;
}

UINT MimeMsiRegisterSource(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR product_path)
{
    WCHAR path[160];
    _snwprintf(path, RTL_NUMBER_OF(path), L"%s\\SourceList", product_path);
    PCWSTR filename = wcsrchr(package->Database->Path, L'\\');
    AcquireSRWLockExclusive(&source_lock);
    UINT error = write_text(package, root, path, L"PackageName", filename ? filename + 1 : package->Database->Path);
    if (!error)
        error = add_source(package, root, path, MSISOURCETYPE_NETWORK, MimeMsiGetProperty(package, L"SourceDir"), TRUE);
    ReleaseSRWLockExclusive(&source_lock);
    return error;
}
