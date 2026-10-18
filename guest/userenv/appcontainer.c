/*
 * app containers: the sid of one is derived from its name, the sha-256 of the lower cased utf-16 name,
 * whose first 28 bytes become seven sub authorities after S-1-15-2. a profile is the name, a mapping of the
 * sid to it in the user's registry and a folder of the user's local data.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/wincrypt.h>

#define USERENV DECLSPEC_EXPORT

NTSYSAPI NTSTATUS NTAPI RtlConvertSidToUnicodeString(PUNICODE_STRING out, PVOID sid, BOOLEAN allocate);
#define APP_CONTAINER_NAME_MAX 64

typedef BOOL(WINAPI *HASH_CERTIFICATE)(HCRYPTPROV_LEGACY, ALG_ID, DWORD, const BYTE *, DWORD, BYTE *, DWORD *);

static BOOL sha256(const void *data, DWORD size, BYTE *hash)
{
    static HASH_CERTIFICATE hash_certificate;
    if (!hash_certificate) {
        HMODULE crypt32 = LoadLibraryW(L"crypt32.dll");
        hash_certificate = crypt32 ? (HASH_CERTIFICATE)GetProcAddress(crypt32, "CryptHashCertificate") : nullptr;
        if (!hash_certificate)
            return FALSE;
    }
    DWORD length = 32;
    return hash_certificate(0, CALG_SHA_256, 0, data, size, hash, &length) && length == 32;
}

USERENV HRESULT WINAPI DeriveAppContainerSidFromAppContainerName(PCWSTR name, PSID *sid)
{
    if (!sid)
        return E_INVALIDARG;
    *sid = nullptr;
    size_t length = name ? wcslen(name) : 0;
    if (!length || length > APP_CONTAINER_NAME_MAX)
        return E_INVALIDARG;
    WCHAR lower[APP_CONTAINER_NAME_MAX + 1];
    if (!LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, name, (int)length, lower, APP_CONTAINER_NAME_MAX))
        return HRESULT_FROM_WIN32(GetLastError());
    BYTE hash[32];
    if (!sha256(lower, (DWORD)(length * sizeof(WCHAR)), hash))
        return E_FAIL;
    /* revision 1, eight sub authorities, authority 15, then 2 and the hash. */
    BYTE *result = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, 8 + 4 * 8);
    if (!result)
        return E_OUTOFMEMORY;
    static const BYTE header[12] = {1, 8, 0, 0, 0, 0, 0, 15, 2, 0, 0, 0};
    memcpy(result, header, sizeof(header));
    memcpy(result + sizeof(header), hash, 28);
    *sid = result;
    return S_OK;
}

/* ------------------------------------------------------------------ profiles */

#define MAPPINGS_KEY L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\Mappings"
#define DISPLAY_NAME_MAX 512
#define DESCRIPTION_MAX 2048

/* a letter, a digit, a dot, a dash or an underscore. */
static BOOL valid_name_character(WCHAR c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
}

static BOOL valid_text(PCWSTR text, size_t maximum)
{
    size_t length = text ? wcslen(text) : 0;
    return length && length <= maximum;
}

/* S-1-15-2-... as text, the name of the mapping. */
static void sid_to_string(const BYTE *sid, WCHAR *out)
{
    int n = _snwprintf(out, 128, L"S-%u-%u", sid[0], sid[7]);
    for (int i = 0; i < sid[1]; i++)
        n += _snwprintf(out + n, 128 - n, L"-%lu", *(const DWORD *)(sid + 8 + 4 * i));
}

/* the profile folder of a name: Packages under the user's local data. */
static BOOL package_folder(PCWSTR moniker, WCHAR *path, DWORD chars)
{
    WCHAR base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return FALSE;
    return _snwprintf(path, chars, L"%ls\\Packages\\%ls", base, moniker) > 0;
}

static HRESULT create_directories(PCWSTR path)
{
    WCHAR partial[MAX_PATH];
    size_t length = wcslen(path);
    if (length >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    wcscpy(partial, path);
    /* from the first level below the drive on. */
    for (WCHAR *p = partial + 3; *p; p++) {
        if (*p != '\\')
            continue;
        *p = 0;
        if (!CreateDirectoryW(partial, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return HRESULT_FROM_WIN32(GetLastError());
        *p = '\\';
    }
    if (!CreateDirectoryW(partial, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

/*
 * makes the profile: the mapping of the sid to the name and its display name, and the folders. a profile that
 * is there is ERROR_ALREADY_EXISTS, and the caller derives the sid of it.
 */
USERENV HRESULT WINAPI CreateAppContainerProfile(PCWSTR name, PCWSTR display_name, PCWSTR description,
                                                 PVOID capabilities, DWORD capability_count, PSID *sid)
{
    if (!sid)
        return E_INVALIDARG;
    *sid = nullptr;
    if (!valid_text(name, APP_CONTAINER_NAME_MAX) || !valid_text(display_name, DISPLAY_NAME_MAX) ||
        !valid_text(description, DESCRIPTION_MAX) || (capability_count && !capabilities))
        return E_INVALIDARG;
    for (PCWSTR c = name; *c; c++)
        if (!valid_name_character(*c))
            return E_INVALIDARG;
    PSID derived;
    HRESULT result = DeriveAppContainerSidFromAppContainerName(name, &derived);
    if (FAILED(result))
        return result;
    WCHAR text[128], key_path[MAX_PATH];
    sid_to_string(derived, text);
    _snwprintf(key_path, RTL_NUMBER_OF(key_path), MAPPINGS_KEY L"\\%ls", text);
    HKEY key;
    DWORD disposition;
    LONG error = RegCreateKeyExW(HKEY_CURRENT_USER, key_path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition);
    if (error) {
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, derived);
        return HRESULT_FROM_WIN32(error);
    }
    if (disposition == REG_OPENED_EXISTING_KEY) {
        RegCloseKey(key);
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, derived);
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    RegSetValueExW(key, L"Moniker", 0, REG_SZ, (const BYTE *)name, (DWORD)((wcslen(name) + 1) * sizeof(WCHAR)));
    RegSetValueExW(key, L"DisplayName", 0, REG_SZ, (const BYTE *)display_name, (DWORD)((wcslen(display_name) + 1) * sizeof(WCHAR)));
    RegSetValueExW(key, L"Description", 0, REG_SZ, (const BYTE *)description, (DWORD)((wcslen(description) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    WCHAR folder[MAX_PATH];
    if (package_folder(name, folder, RTL_NUMBER_OF(folder))) {
        result = create_directories(folder);
        if (SUCCEEDED(result)) {
            WCHAR sub[MAX_PATH];
            _snwprintf(sub, RTL_NUMBER_OF(sub), L"%ls\\AC", folder);
            create_directories(sub);
            _snwprintf(sub, RTL_NUMBER_OF(sub), L"%ls\\AppData", folder);
            create_directories(sub);
        }
        if (FAILED(result)) {
            RegDeleteKeyW(HKEY_CURRENT_USER, key_path);
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, derived);
            return result;
        }
    }
    *sid = derived;
    return S_OK;
}

/* the sid of a text, S-1-15-2 and its sub authorities; the profile mappings only hold app containers. */
static BOOL string_to_sid(PCWSTR text, BYTE *sid)
{
    if (text[0] != 'S' || text[1] != '-')
        return FALSE;
    ULONG values[10];
    int count = 0;
    for (PCWSTR p = text + 2; *p;) {
        if (*p < '0' || *p > '9' || count == 10)
            return FALSE;
        ULONG64 value = 0;
        while (*p >= '0' && *p <= '9' && value <= 0xFFFFFFFF)
            value = value * 10 + (*p++ - '0');
        if (value > 0xFFFFFFFF || (*p && *p != '-'))
            return FALSE;
        if (*p == '-')
            p++;
        values[count++] = (ULONG)value;
    }
    if (count < 3 || values[0] != 1 || values[1] != 15)
        return FALSE;
    memset(sid, 0, 8);
    sid[0] = 1;
    sid[1] = (BYTE)(count - 2);
    sid[7] = 15;
    for (int i = 2; i < count; i++)
        *(DWORD *)(sid + 8 + 4 * (i - 2)) = values[i];
    return TRUE;
}

/* the profile the sid names: deleted with its mapping and its folders. */
USERENV HRESULT WINAPI DeleteAppContainerProfile(PCWSTR name)
{
    if (!valid_text(name, APP_CONTAINER_NAME_MAX))
        return E_INVALIDARG;
    PSID derived;
    HRESULT result = DeriveAppContainerSidFromAppContainerName(name, &derived);
    if (FAILED(result))
        return result;
    WCHAR text[128], key_path[MAX_PATH];
    sid_to_string(derived, text);
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, derived);
    _snwprintf(key_path, RTL_NUMBER_OF(key_path), MAPPINGS_KEY L"\\%ls", text);
    LONG error = RegDeleteKeyW(HKEY_CURRENT_USER, key_path);
    if (error && error != ERROR_FILE_NOT_FOUND)
        return HRESULT_FROM_WIN32(error);
    /* the folders: the sub folders it made, then the folder. what a program put there stays. */
    WCHAR folder[MAX_PATH], sub[MAX_PATH];
    if (package_folder(name, folder, RTL_NUMBER_OF(folder))) {
        _snwprintf(sub, RTL_NUMBER_OF(sub), L"%ls\\AC", folder);
        RemoveDirectoryW(sub);
        _snwprintf(sub, RTL_NUMBER_OF(sub), L"%ls\\AppData", folder);
        RemoveDirectoryW(sub);
        RemoveDirectoryW(folder);
    }
    return S_OK;
}

/* the folder of the profile of the sid in text, from the co task allocator. */
USERENV HRESULT WINAPI GetAppContainerFolderPath(PCWSTR sid_text, PWSTR *path)
{
    if (!path)
        return E_INVALIDARG;
    *path = nullptr;
    BYTE sid[8 + 4 * 15];
    if (!sid_text || !string_to_sid(sid_text, sid) || sid[1] < 2 || *(DWORD *)(sid + 8) != 2)
        return E_INVALIDARG;
    WCHAR key_path[MAX_PATH], moniker[MAX_PATH];
    _snwprintf(key_path, RTL_NUMBER_OF(key_path), MAPPINGS_KEY L"\\%ls", sid_text);
    HKEY key;
    LONG error = RegOpenKeyExW(HKEY_CURRENT_USER, key_path, 0, KEY_QUERY_VALUE, &key);
    if (error)
        return HRESULT_FROM_WIN32(error == ERROR_FILE_NOT_FOUND ? ERROR_NOT_FOUND : error);
    DWORD type, size = sizeof(moniker) - sizeof(WCHAR);
    error = RegQueryValueExW(key, L"Moniker", nullptr, &type, (LPBYTE)moniker, &size);
    RegCloseKey(key);
    if (error)
        return HRESULT_FROM_WIN32(error);
    moniker[size / sizeof(WCHAR)] = 0;
    WCHAR folder[MAX_PATH];
    if (!package_folder(moniker, folder, RTL_NUMBER_OF(folder)))
        return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    HMODULE ole32 = LoadLibraryW(L"ole32.dll");
    PVOID(WINAPI * allocate)(SIZE_T) = ole32 ? (PVOID(WINAPI *)(SIZE_T))GetProcAddress(ole32, "CoTaskMemAlloc") : nullptr;
    if (!allocate)
        return E_OUTOFMEMORY;
    SIZE_T bytes = (wcslen(folder) + 1) * sizeof(WCHAR);
    PWSTR copy = allocate(bytes);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, folder, bytes);
    *path = copy;
    return S_OK;
}

/* the storage key of the app container the calling thread runs in, under the user's registry. */
USERENV HRESULT WINAPI GetAppContainerRegistryLocation(REGSAM access, HKEY *key)
{
    if (!key)
        return E_INVALIDARG;
    HANDLE token = nullptr;
    if (!NT_SUCCESS(NtOpenThreadToken(NtCurrentThread(), 8 /* TOKEN_QUERY */, TRUE, &token)) &&
        !NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), 8, &token)))
        return E_FAIL;
    ULONG is_container = 0, returned;
    BYTE user[8 + 4 * 15 + 16], container[8 + 4 * 15 + 16];
    HRESULT result = 0x8000FFFF; /* E_UNEXPECTED, for a caller that is not in an app container */
    if (NT_SUCCESS(NtQueryInformationToken(token, 29 /* TokenIsAppContainer */, &is_container, sizeof(is_container), &returned)) &&
        is_container &&
        NT_SUCCESS(NtQueryInformationToken(token, 1 /* TokenUser */, user, sizeof(user), &returned)) &&
        NT_SUCCESS(NtQueryInformationToken(token, 31 /* TokenAppContainerSid */, container, sizeof(container), &returned))) {
        UNICODE_STRING user_text, container_text;
        if (NT_SUCCESS(RtlConvertSidToUnicodeString(&user_text, *(PSID *)user, TRUE))) {
            if (NT_SUCCESS(RtlConvertSidToUnicodeString(&container_text, *(PSID *)container, TRUE))) {
                WCHAR path[512];
                _snwprintf(path, RTL_NUMBER_OF(path),
                           L"%ls\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\Storage\\%ls",
                           user_text.Buffer, container_text.Buffer);
                path[RTL_NUMBER_OF(path) - 1] = 0;
                LONG error = RegOpenKeyExW(HKEY_USERS, path, 0, access, key);
                result = error ? HRESULT_FROM_WIN32(error) : S_OK;
                RtlFreeUnicodeString(&container_text);
            } else {
                result = E_OUTOFMEMORY;
            }
            RtlFreeUnicodeString(&user_text);
        } else {
            result = E_OUTOFMEMORY;
        }
    }
    NtClose(token);
    return result;
}
