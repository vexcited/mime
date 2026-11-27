#include "winbrand_private.h"

#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
static const WNF_STATE_NAME g_eula_installed = {{0xA3BC6075, 0x028F0222}};

NTSYSAPI NTSTATUS NTAPI RtlGetPersistedStateLocation(PCWSTR source, PCWSTR custom, PCWSTR fallback, ULONG type,
                                                     PWCHAR target, ULONG length, PULONG needed);

DECLSPEC_EXPORT void WINAPI EulaFreeBuffer(void *buffer)
{
    if (buffer)
        HeapFree(GetProcessHeap(), 0, buffer);
}

/* where state is kept apart from the system, the licence's place is a key this does not read. */
static HRESULT state_is_here(void)
{
    ULONG needed = 0;
    NTSTATUS status = RtlGetPersistedStateLocation(L"AcceptedEula", nullptr, nullptr, 0, nullptr, 0, &needed);
    if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        return S_OK;
    return NT_SUCCESS(status) || status == (NTSTATUS)0x80000005L ? HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)
                                                               : HRESULT_FROM_WIN32(RtlNtStatusToDosError(status));
}

DECLSPEC_EXPORT HRESULT WINAPI GetInstalledEULAPath(WCHAR **path)
{
    if (!path)
        return E_INVALIDARG;
    HRESULT hr = state_is_here();
    if (FAILED(hr))
        return hr;
    WCHAR folder[MAX_PATH + 1];
    DWORD chars = ExpandEnvironmentStringsW(L"%windir%", folder, MAX_PATH + 1);
    if (!chars || chars > MAX_PATH + 1)
        return chars ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : HRESULT_FROM_WIN32(GetLastError());
    static const WCHAR file[] = L"system32\\license.rtf";
    SIZE_T length = wcslen(folder);
    WCHAR *out = HeapAlloc(GetProcessHeap(), 0, (length + 1 + RTL_NUMBER_OF(file)) * sizeof(WCHAR));
    if (!out)
        return E_OUTOFMEMORY;
    memcpy(out, folder, length * sizeof(WCHAR));
    if (length && out[length - 1] != L'\\')
        out[length++] = L'\\';
    memcpy(out + length, file, sizeof(file));
    *path = out;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI GetEULAInCurrentUILanguage(WCHAR **path)
{
    return GetInstalledEULAPath(path);
}

/*
 * the licences of an image wait under System32\Licenses by channel, edition and language, and this windows has none:
 * the search always ends with nothing found.
 */
DECLSPEC_EXPORT HRESULT WINAPI GetEULAFileEx(const WCHAR *edition, const WCHAR *channel, const WCHAR *language,
                                             const WCHAR *name, BOOL installed, WCHAR **path)
{
    if (!name || !path)
        return E_INVALIDARG;
    if (!installed)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    return GetInstalledEULAPath(path);
}

DECLSPEC_EXPORT HRESULT WINAPI GetEULAFile(const WCHAR *edition, const WCHAR *channel, const WCHAR *language,
                                           const WCHAR *name, WCHAR **path)
{
    return GetEULAFileEx(edition, channel, language, name, TRUE, path);
}

/* with no licence to copy, what is left is the time of the acceptance and the word to who waits for it. */
DECLSPEC_EXPORT HRESULT WINAPI InstallEULA(const WCHAR *edition, const WCHAR *channel, const WCHAR *language,
                                           const WCHAR *name)
{
    if (!name)
        return E_INVALIDARG;
    HRESULT hr = state_is_here();
    if (FAILED(hr))
        return hr;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    HKEY key;
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                     nullptr);
    if (!status) {
        status = RegSetValueExW(key, L"EulaInstalled", 0, REG_QWORD, (const BYTE *)&now, sizeof(now));
        RegCloseKey(key);
    }
    if (status)
        return HRESULT_FROM_WIN32(status);
    ULONG installed = 1;
    NTSTATUS published = RtlPublishWnfStateData(g_eula_installed, nullptr, &installed, sizeof(installed), nullptr);
    return NT_SUCCESS(published) ? S_OK : HRESULT_FROM_WIN32(RtlNtStatusToDosError(published));
}
