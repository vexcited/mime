/*
 * app container registry: a sid registered under a name, used by userenv profiles and browser sandboxes. the
 * mappings are the ones userenv writes.
 */
#include "kernelbase_private.h"

#define MAPPINGS_KEY L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\Mappings"
#define MONIKER_MAX 64
#define TEXT_MAX 128
#define TOKEN_QUERY 0x0008
#define TOKEN_CLASS_SESSION_ID 12
#define TOKEN_CLASS_APP_CONTAINER_SID 31

NTSYSAPI NTSTATUS NTAPI RtlConvertSidToUnicodeString(PUNICODE_STRING out, PVOID sid, BOOLEAN allocate);

/* S-1-15-2 and at least one more sub authority. */
static BOOL is_package_sid(PSID sid)
{
    const BYTE *b = sid;
    static const BYTE authority[6] = {0, 0, 0, 0, 0, 15};
    return b && b[0] == 1 && b[1] >= 2 && !memcmp(b + 2, authority, 6) && *(const DWORD *)(b + 8) == 2;
}

static BOOL sid_text(PSID sid, WCHAR *text)
{
    UNICODE_STRING us = {0, TEXT_MAX * sizeof(WCHAR) - sizeof(WCHAR), text};
    if (!NT_SUCCESS(RtlConvertSidToUnicodeString(&us, sid, FALSE)))
        return FALSE;
    text[us.Length / sizeof(WCHAR)] = 0;
    return TRUE;
}

static BOOL mapping_path(PSID sid, WCHAR *path, DWORD chars)
{
    WCHAR text[TEXT_MAX];
    return is_package_sid(sid) && sid_text(sid, text) && _snwprintf(path, chars, MAPPINGS_KEY L"\\%ls", text) > 0;
}

static BOOL name_fits(PCWSTR text, size_t maximum)
{
    size_t length = text ? wcslen(text) : 0;
    return length && length <= maximum;
}

DECLSPEC_EXPORT VOID WINAPI AppContainerFreeMemory(PVOID memory)
{
    if (memory)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, memory);
}

/* an existing registration fails with ERROR_ALREADY_EXISTS. */
DECLSPEC_EXPORT HRESULT WINAPI AppContainerRegisterSid(PSID sid, PCWSTR moniker, PCWSTR display_name)
{
    WCHAR path[MAX_PATH];
    if (!mapping_path(sid, path, RTL_NUMBER_OF(path)) || !name_fits(moniker, MONIKER_MAX) || (display_name && !*display_name))
        return E_INVALIDARG;
    HKEY key;
    DWORD disposition;
    LONG error = RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition);
    if (error)
        return HRESULT_FROM_WIN32(error);
    if (disposition == REG_OPENED_EXISTING_KEY) {
        RegCloseKey(key);
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    RegSetValueExW(key, L"Moniker", 0, REG_SZ, (const BYTE *)moniker, (DWORD)((wcslen(moniker) + 1) * sizeof(WCHAR)));
    if (display_name)
        RegSetValueExW(key, L"DisplayName", 0, REG_SZ, (const BYTE *)display_name,
                       (DWORD)((wcslen(display_name) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI AppContainerUnregisterSid(PSID sid)
{
    WCHAR path[MAX_PATH];
    if (!mapping_path(sid, path, RTL_NUMBER_OF(path)))
        return E_INVALIDARG;
    LONG error = RegDeleteKeyW(HKEY_CURRENT_USER, path);
    return error ? HRESULT_FROM_WIN32(error == ERROR_FILE_NOT_FOUND ? ERROR_NOT_FOUND : error) : S_OK;
}

/* the moniker comes from the process heap, free it with AppContainerFreeMemory. */
DECLSPEC_EXPORT HRESULT WINAPI AppContainerLookupMoniker(PSID sid, PWSTR *moniker)
{
    if (!moniker)
        return E_INVALIDARG;
    *moniker = nullptr;
    WCHAR path[MAX_PATH], name[MONIKER_MAX + 1];
    if (!mapping_path(sid, path, RTL_NUMBER_OF(path)))
        return E_INVALIDARG;
    HKEY key;
    LONG error = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &key);
    if (error)
        return HRESULT_FROM_WIN32(error == ERROR_FILE_NOT_FOUND ? ERROR_NOT_FOUND : error);
    DWORD type, size = MONIKER_MAX * sizeof(WCHAR);
    error = RegQueryValueExW(key, L"Moniker", nullptr, &type, (LPBYTE)name, &size);
    RegCloseKey(key);
    if (error)
        return HRESULT_FROM_WIN32(error == ERROR_FILE_NOT_FOUND ? ERROR_NOT_FOUND : error);
    name[size / sizeof(WCHAR)] = 0;
    size = (DWORD)((wcslen(name) + 1) * sizeof(WCHAR));
    PWSTR copy = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, size);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, name, size);
    *moniker = copy;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI AppContainerDeriveSidFromMoniker(PCWSTR moniker, PSID *sid)
{
    HMODULE userenv = LoadLibraryW(L"userenv.dll");
    HRESULT(WINAPI * derive)(PCWSTR, PSID *) =
        userenv ? (HRESULT(WINAPI *)(PCWSTR, PSID *))GetProcAddress(userenv, "DeriveAppContainerSidFromAppContainerName") : nullptr;
    if (!derive)
        return E_FAIL;
    return derive(moniker, sid);
}

/*
 * \Sessions\<n>\AppContainerNamedObjects\<sid>, where an app container creates its named objects. the sid defaults
 * to the token's, the session is the token's.
 */
DECLSPEC_EXPORT BOOL WINAPI GetAppContainerNamedObjectPath(HANDLE token, PSID sid, ULONG length, PWSTR path, PULONG required)
{
    if (!required || (length && !path)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BYTE buffer[sizeof(PVOID) + 8 + 4 * 15];
    ULONG session = NtCurrentPeb()->SessionId;
    HANDLE opened = nullptr;
    NTSTATUS status = STATUS_SUCCESS;
    if (!token) {
        status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &opened);
        token = opened;
    }
    if (NT_SUCCESS(status)) {
        ULONG ignored;
        NtQueryInformationToken(token, TOKEN_CLASS_SESSION_ID, &session, sizeof(session), &ignored);
        if (!sid && NT_SUCCESS(status = NtQueryInformationToken(token, TOKEN_CLASS_APP_CONTAINER_SID, buffer, sizeof(buffer), &ignored)))
            sid = *(PSID *)buffer;
    }
    if (opened)
        NtClose(opened);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    WCHAR text[TEXT_MAX], name[MAX_PATH];
    if (!sid || !is_package_sid(sid) || !sid_text(sid, text)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG chars = (ULONG)_snwprintf(name, RTL_NUMBER_OF(name), L"\\Sessions\\%lu\\AppContainerNamedObjects\\%ls", session, text) + 1;
    *required = chars;
    if (length < chars) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(path, name, chars * sizeof(WCHAR));
    return TRUE;
}

NTSYSAPI NTSTATUS NTAPI RtlDeriveCapabilitySidsFromName(PCUNICODE_STRING name, PSID group, PSID capability);

/* the group and capability sids of a name, each in a one element array; arrays and sids are local memory. */
DECLSPEC_EXPORT BOOL WINAPI DeriveCapabilitySidsFromName(PCWSTR name, PSID **groups, DWORD *group_count, PSID **capabilities,
                                                         DWORD *capability_count)
{
    if (!name || !groups || !group_count || !capabilities || !capability_count) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UNICODE_STRING text;
    RtlInitUnicodeString(&text, name);
    PSID capability = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, 48);
    if (!capability)
        return FALSE;
    PSID group = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, 44);
    PSID *group_array = nullptr, *capability_array = nullptr;
    if (group) {
        NTSTATUS status = RtlDeriveCapabilitySidsFromName(&text, group, capability);
        if (!NT_SUCCESS(status)) {
            KbSetStatus(status);
        } else if ((group_array = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(PSID))) &&
                   (capability_array = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(PSID)))) {
            *group_array = group;
            *capability_array = capability;
            *group_count = 1;
            *capability_count = 1;
            *groups = group_array;
            *capabilities = capability_array;
            return TRUE;
        }
    }
    LocalFree(capability);
    if (group)
        LocalFree(group);
    if (group_array)
        LocalFree(group_array);
    return FALSE;
}
