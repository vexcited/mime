#include "winbrand_private.h"

#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005L)
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#define E_NOT_FOUND ((HRESULT)0x80070490L)
#define E_FILE_NOT_FOUND ((HRESULT)0x80070002L)
#define MUI_LANGUAGE_NAME 8
#define LOCALE_ENGLISH_US 0x409
#define KEY_WOW64_64KEY 0x100
#define MOST_CANDIDATES 8

NTSYSAPI NTSTATUS NTAPI RtlGetPersistedStateLocation(PCWSTR source, PCWSTR custom, PCWSTR fallback, ULONG type,
                                                     PWCHAR target, ULONG length, PULONG needed);
NTSYSAPI BOOL NTAPI ApiSetQueryApiSetPresence(PCUNICODE_STRING name, PBOOLEAN present);

static const WNF_STATE_NAME g_eula_installed = {{0xA3BC6075, 0x028F0222}};

static HRESULT status_result(NTSTATUS status)
{
    return NT_SUCCESS(status) ? S_OK : HRESULT_FROM_WIN32(RtlNtStatusToDosError(status));
}

static BOOL same(const WCHAR *a, const WCHAR *b)
{
    return CompareStringW(LOCALE_ENGLISH_US, NORM_IGNORECASE, a, -1, b, -1) == CSTR_EQUAL;
}

static WCHAR *duplicate(const WCHAR *text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

static WCHAR *joined(const WCHAR *folder, const WCHAR *rest)
{
    SIZE_T length = wcslen(folder);
    WCHAR *path = HeapAlloc(GetProcessHeap(), 0, (length + wcslen(rest) + 2) * sizeof(WCHAR));
    if (!path)
        return nullptr;
    memcpy(path, folder, length * sizeof(WCHAR));
    if (length && path[length - 1] != L'\\' && path[length - 1] != L'/')
        path[length++] = L'\\';
    wcscpy(path + length, rest);
    return path;
}

static void release(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

static BOOL file_exists(const WCHAR *path)
{
    DWORD attributes = path ? GetFileAttributesW(path) : INVALID_FILE_ATTRIBUTES;
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* a function of an api set that this system may lack. a set that is there with no library behind it tells so. */
static void *extension(const WCHAR *set, const char *function, BOOL *present)
{
    UNICODE_STRING name;
    BOOLEAN there = FALSE;
    RtlInitUnicodeString(&name, set);
    *present = NT_SUCCESS(ApiSetQueryApiSetPresence(&name, &there)) && there;
    if (!*present)
        return nullptr;
    HMODULE module = LoadLibraryExW(set, nullptr, 0);
    return module ? (void *)GetProcAddress(module, function) : nullptr;
}

/* the place where the system keeps the acceptance when its state is apart: a key below the machine's, or none. */
static HRESULT state_key(WCHAR **key)
{
    ULONG bytes = 0;
    *key = nullptr;
    NTSTATUS status = RtlGetPersistedStateLocation(L"AcceptedEula", nullptr, nullptr, 0, nullptr, 0, &bytes);
    if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        return S_OK;
    if (status != STATUS_BUFFER_OVERFLOW)
        return status_result(status);
    WCHAR *buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + sizeof(WCHAR));
    if (!buffer)
        return E_OUTOFMEMORY;
    status = RtlGetPersistedStateLocation(L"AcceptedEula", nullptr, nullptr, 0, buffer, bytes, &bytes);
    if (!NT_SUCCESS(status)) {
        release(buffer);
        return status_result(status);
    }
    *key = buffer;
    return S_OK;
}

static HRESULT read_string(const WCHAR *key_path, const WCHAR *name, WCHAR **text)
{
    HKEY key;
    *text = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
    if (status)
        return HRESULT_FROM_WIN32(status);
    DWORD type = 0, bytes = 0;
    status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (!status && type != REG_SZ && type != REG_EXPAND_SZ)
        status = ERROR_INVALID_DATA;
    WCHAR *data = status ? nullptr : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + sizeof(WCHAR));
    if (!status && !data)
        status = ERROR_OUTOFMEMORY;
    if (!status)
        status = RegQueryValueExW(key, name, nullptr, nullptr, (BYTE *)data, &bytes);
    RegCloseKey(key);
    if (status) {
        release(data);
        return HRESULT_FROM_WIN32(status);
    }
    *text = data;
    return S_OK;
}

DECLSPEC_EXPORT void WINAPI EulaFreeBuffer(void *buffer)
{
    release(buffer);
}

DECLSPEC_EXPORT HRESULT WINAPI GetInstalledEULAPath(WCHAR **path)
{
    if (!path)
        return E_INVALIDARG;
    WCHAR *key;
    HRESULT hr = state_key(&key);
    if (FAILED(hr))
        return hr;
    if (key) {
        hr = read_string(key, L"EulaFilePath", path);
        release(key);
        return hr;
    }
    WCHAR folder[MAX_PATH];
    DWORD chars = ExpandEnvironmentStringsW(L"%windir%", folder, MAX_PATH);
    if (!chars)
        return HRESULT_FROM_WIN32(GetLastError());
    if (chars > MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    WCHAR *out = joined(folder, L"system32\\license.rtf");
    if (!out)
        return E_OUTOFMEMORY;
    *path = out;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI GetEULAInCurrentUILanguage(WCHAR **path)
{
    return GetInstalledEULAPath(path);
}

/*
 * the places of an edition's licence in one language, in the order they are tried: the text of a region among the
 * neutral ones, the channel's, then the default channel's. a file name of the edition under ProductOptions replaces
 * license.rtf.
 */
static int candidates(const WCHAR *language, const WCHAR *edition, const WCHAR *channel, const WCHAR *region,
                      WCHAR **list)
{
    WCHAR value[160], named[MAX_PATH] = L"license.rtf", path[4 * MAX_PATH];
    int count = 0;
    BOOL neutral = same(language, L"neutral");
    const WCHAR *format = neutral ? L"System32\\Licenses\\%s\\%s\\%s\\%s" : L"System32\\%s\\Licenses\\%s\\%s\\%s";
    WCHAR *custom = nullptr;
    _snwprintf(value, RTL_NUMBER_OF(value) - 1, L"%s-EulaLicenseFilename", edition);
    value[RTL_NUMBER_OF(value) - 1] = 0;
    if (SUCCEEDED(read_string(L"SYSTEM\\CurrentControlSet\\Control\\ProductOptions", value, &custom)) && custom) {
        _snwprintf(named, MAX_PATH - 1, L"%s", custom);
        named[MAX_PATH - 1] = 0;
    }
    release(custom);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    if (region) {
        const WCHAR *folder = channel && !same(channel, L"Retail") ? channel : L"_Default";
        _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"System32\\Licenses\\neutral\\%s\\%s\\%s-%s", folder, edition, region,
                   named);
        list[count++] = duplicate(path);
    }
    if (channel) {
        _snwprintf(path, RTL_NUMBER_OF(path) - 1, format, language, channel, edition, named);
        list[count++] = duplicate(path);
    }
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, format, language, L"_Default", edition, named);
    list[count++] = duplicate(path);
    if (same(edition, L"EnterpriseG") && same(language, L"zh-cn")) {
        _snwprintf(path, RTL_NUMBER_OF(path) - 1, format, language, L"_Default", edition, L"placeholder.rtf");
        list[count++] = duplicate(path);
    }
    return count;
}

/* a file that a language overlay puts in the place of this one, when the system has overlays. */
static HRESULT overlay(const WCHAR *language, const WCHAR *path, WCHAR **found)
{
    BOOL present;
    NTSTATUS(WINAPI * overlay_path)(const WCHAR *, const WCHAR *, ULONG *, WCHAR *) =
            extension(L"ext-ms-win-resources-languageoverlay-l1-1-1", "GetOverlayFilePath", &present);
    if (!present)
        return E_FILE_NOT_FOUND;
    if (!overlay_path)
        return E_NOTIMPL;
    ULONG bytes = 0;
    WCHAR *buffer = nullptr;
    NTSTATUS status = overlay_path(language, path, &bytes, nullptr);
    if (status == STATUS_BUFFER_TOO_SMALL) {
        buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + sizeof(WCHAR));
        if (!buffer)
            return E_OUTOFMEMORY;
        status = overlay_path(language, path, &bytes, buffer);
    }
    if (!NT_SUCCESS(status)) {
        release(buffer);
        return (HRESULT)(status | 0x10000000);
    }
    if (!file_exists(buffer)) {
        release(buffer);
        return E_FILE_NOT_FOUND;
    }
    *found = buffer;
    return S_OK;
}

/* the licence in a language or else in english, of the edition and then of the edition it is composed from. */
static HRESULT search_language(const WCHAR *folder, const WCHAR *language, const WCHAR *edition, const WCHAR *channel,
                               const WCHAR *region, WCHAR **found)
{
    const WCHAR *languages[2] = {language, L"en-us"};
    BOOL composes;
    HRESULT(WINAPI * composition)(const WCHAR *, WCHAR **) =
            extension(L"ext-ms-win-core-pkeyhelper-l1-1-0", "GetCompositionEditionForVirtualEdition", &composes);
    HRESULT hr = E_NOT_FOUND;
    *found = nullptr;
    for (int l = 0; l < 2 && !*found && (hr == E_NOT_FOUND || SUCCEEDED(hr)); l++) {
        WCHAR *list[MOST_CANDIDATES] = {0}, *composed = nullptr;
        int count = candidates(languages[l], edition, channel, region, list);
        hr = E_NOT_FOUND;
        if (composes) {
            /* the 32-bit system has the set and not its library: the search ends there, not implemented. */
            HRESULT asked = composition ? composition(edition, &composed) : E_NOTIMPL;
            if (FAILED(asked))
                hr = asked;
            else if (composed && !same(composed, edition))
                count += candidates(languages[l], composed, channel, region, list + count);
            if (composed)
                LocalFree(composed);
        }
        for (int i = 0; i < count && !*found && hr == E_NOT_FOUND; i++) {
            WCHAR *path = list[i] ? joined(folder, list[i]) : nullptr;
            if (!path) {
                hr = E_OUTOFMEMORY;
                break;
            }
            HRESULT over = overlay(language, path, found);
            if (SUCCEEDED(over)) {
                release(path);
            } else if (over != E_FILE_NOT_FOUND && over != (HRESULT)0xD0000034L) {
                hr = over;
                release(path);
            } else if (file_exists(path)) {
                *found = path;
            } else {
                release(path);
            }
        }
        for (int i = 0; i < count; i++)
            release(list[i]);
        if (*found)
            hr = S_OK;
    }
    return hr;
}

/* the language asked for, then each language the user prefers. */
static HRESULT search(const WCHAR *folder, const WCHAR *language, const WCHAR *edition, const WCHAR *channel,
                      const WCHAR *region, WCHAR **found)
{
    ULONG count = 0, chars = 0;
    *found = nullptr;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &chars))
        return HRESULT_FROM_WIN32(GetLastError());
    WCHAR *preferred = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (chars + 1) * sizeof(WCHAR));
    if (!preferred)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, preferred, &chars))
        hr = HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(hr)) {
        hr = language ? search_language(folder, language, edition, channel, region, found) : E_NOT_FOUND;
        const WCHAR *next = preferred;
        for (ULONG i = 0; i < count && hr == E_NOT_FOUND; i++, next += wcslen(next) + 1)
            if (!language || !same(language, next))
                hr = search_language(folder, next, edition, channel, region, found);
    }
    release(preferred);
    return hr;
}

DECLSPEC_EXPORT HRESULT WINAPI GetEULAFileEx(const WCHAR *language, const WCHAR *region, const WCHAR *channel,
                                             const WCHAR *edition, BOOL installed, WCHAR **path)
{
    if (!edition || !path)
        return E_INVALIDARG;
    WCHAR folder[MAX_PATH] = {0}, *found = nullptr;
    GetSystemWindowsDirectoryW(folder, MAX_PATH);
    HRESULT hr = search(folder, language, edition, channel, region, &found);
    if (found) {
        *path = found;
        return S_OK;
    }
    if (FAILED(hr) && (!installed || hr != E_NOT_FOUND))
        return hr;
    return GetInstalledEULAPath(path);
}

DECLSPEC_EXPORT HRESULT WINAPI GetEULAFile(const WCHAR *language, const WCHAR *region, const WCHAR *channel,
                                           const WCHAR *edition, WCHAR **path)
{
    return GetEULAFileEx(language, region, channel, edition, TRUE, path);
}

static HRESULT copy_to(const WCHAR *folder, const WCHAR *system, const WCHAR *from)
{
    WCHAR *directory = joined(folder, system);
    WCHAR *to = directory ? joined(directory, L"license.rtf") : nullptr;
    HRESULT hr = to ? S_OK : E_OUTOFMEMORY;
    DWORD attributes = directory ? GetFileAttributesW(directory) : INVALID_FILE_ATTRIBUTES;
    BOOL there = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    if (SUCCEEDED(hr) && (there || !_wcsicmp(system, L"system32")) && !CopyFileExW(from, to, nullptr, nullptr, nullptr, 0))
        hr = HRESULT_FROM_WIN32(GetLastError());
    release(directory);
    release(to);
    return hr;
}

static HRESULT write_value(const WCHAR *key_path, REGSAM view, const WCHAR *name, DWORD type, const void *data,
                           DWORD bytes)
{
    HKEY key;
    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_path, 0, KEY_SET_VALUE | view, &key);
    if (status)
        return HRESULT_FROM_WIN32(status);
    status = RegSetValueExW(key, name, 0, type, data, bytes);
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(status);
}

/*
 * the licence of the edition becomes license.rtf of each system folder, or its path a value of the key where state is
 * kept apart. then the time is written and the state that waits for the acceptance is published.
 */
DECLSPEC_EXPORT HRESULT WINAPI InstallEULA(const WCHAR *language, const WCHAR *region, const WCHAR *channel,
                                           const WCHAR *edition)
{
    if (!edition)
        return E_INVALIDARG;
    WCHAR *key, *found = nullptr;
    HRESULT hr = state_key(&key);
    if (FAILED(hr))
        return hr;
    if (key) {
        hr = GetEULAFile(language, region, channel, edition, &found);
        if (SUCCEEDED(hr))
            hr = write_value(key, KEY_WOW64_64KEY, L"EulaFilePath", REG_SZ, found,
                             (DWORD)((wcslen(found) + 1) * sizeof(WCHAR)));
    } else {
        WCHAR folder[MAX_PATH] = {0};
        GetSystemWindowsDirectoryW(folder, MAX_PATH);
        hr = search(folder, language, edition, channel, region, &found);
        if (SUCCEEDED(hr))
            hr = copy_to(folder, L"system32", found);
        if (SUCCEEDED(hr))
            hr = copy_to(folder, L"syswow64", found);
        if (SUCCEEDED(hr))
            hr = copy_to(folder, L"sysarm32", found);
        if (hr == E_NOT_FOUND)
            hr = S_OK;
    }
    release(found);
    if (SUCCEEDED(hr)) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        hr = write_value(key ? key : L"SYSTEM\\Setup", 0, L"EulaInstalled", REG_QWORD, &now, sizeof(now));
    }
    release(key);
    if (FAILED(hr))
        return hr;
    ULONG installed = 1;
    return status_result(RtlPublishWnfStateData(g_eula_installed, nullptr, &installed, sizeof(installed), nullptr));
}
