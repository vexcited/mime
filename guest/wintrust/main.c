#include "trust_private.h"

static SRWLOCK state_lock;
static struct { USHORT generation; BOOL active; } states[256];

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

static LONG state_open(WINTRUST_DATA *data)
{
    AcquireSRWLockExclusive(&state_lock);
    for (DWORD i = 0; i < RTL_NUMBER_OF(states); i++) {
        if (states[i].active || states[i].generation == 0xffff)
            continue;
        states[i].active = TRUE;
        states[i].generation++;
        data->hWVTStateData = (HANDLE)(ULONG_PTR)(0x57000000u | (DWORD)states[i].generation << 8 | i);
        ReleaseSRWLockExclusive(&state_lock);
        return 0;
    }
    ReleaseSRWLockExclusive(&state_lock);
    return E_OUTOFMEMORY;
}

static LONG state_close(WINTRUST_DATA *data)
{
    if (!data->hWVTStateData)
        return 0;
    ULONG_PTR token = (ULONG_PTR)data->hWVTStateData;
    DWORD i = token & 255, generation = (token >> 8) & 0xffff;
    AcquireSRWLockExclusive(&state_lock);
    BOOL valid = (token >> 24) == 0x57 && states[i].active && states[i].generation == generation;
    if (valid) {
        states[i].active = FALSE;
        data->hWVTStateData = NULL;
    }
    ReleaseSRWLockExclusive(&state_lock);
    return valid ? 0 : E_INVALIDARG;
}

static LONG verify_file(const WINTRUST_DATA *data)
{
    WINTRUST_FILE_INFO *info = data->pFile;
    if (!info || info->cbStruct != sizeof(*info) || info->pgKnownSubject ||
        (!info->hFile && (!info->pcwszFilePath || !*info->pcwszFilePath)))
        return E_INVALIDARG;
    HANDLE file = info->hFile;
    BOOL owned = !file;
    if (owned)
        file = CreateFileW(info->pcwszFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return owned ? HRESULT_FROM_WIN32(GetLastError()) : E_INVALIDARG;
    LARGE_INTEGER size, position, zero = {0};
    BYTE *bytes = NULL;
    LONG result = 0;
    BOOL restore = FALSE;
    if (!GetFileSizeEx(file, &size))
        result = HRESULT_FROM_WIN32(GetLastError());
    else if (size.QuadPart < 0 || size.QuadPart > 256 * 1024 * 1024)
        result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    else if (!(bytes = LocalAlloc(LMEM_FIXED, size.QuadPart ? (SIZE_T)size.QuadPart : 1)))
        result = E_OUTOFMEMORY;
    else if (!SetFilePointerEx(file, zero, &position, FILE_CURRENT))
        result = HRESULT_FROM_WIN32(GetLastError());
    else {
        restore = !owned;
        DWORD read;
        if (!SetFilePointerEx(file, zero, NULL, FILE_BEGIN) || !ReadFile(file, bytes, (DWORD)size.QuadPart, &read, NULL))
            result = HRESULT_FROM_WIN32(GetLastError());
        else if (read != size.QuadPart)
            result = HRESULT_FROM_WIN32(ERROR_READ_FAULT);
    }
    if (restore && !SetFilePointerEx(file, position, NULL, FILE_BEGIN) && !result)
        result = HRESULT_FROM_WIN32(GetLastError());
    if (owned)
        CloseHandle(file);
    if (!result)
        result = WtVerifyPe(bytes, (DWORD)size.QuadPart, data);
    LocalFree(bytes);
    return result;
}

LONG WINAPI WinVerifyTrust(HWND window, GUID *action, PVOID input)
{
    static const GUID generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA *data = input;
    if (!action || !data || (data->cbStruct != offsetof(WINTRUST_DATA, pSignatureSettings) &&
        data->cbStruct != sizeof(*data)))
        return E_INVALIDARG;
    if (memcmp(action, &generic, sizeof(generic)))
        return TRUST_E_ACTION_UNKNOWN;
    if (data->dwStateAction == WTD_STATEACTION_CLOSE)
        return state_close(data);
    if (data->dwStateAction > WTD_STATEACTION_VERIFY || data->pPolicyCallbackData || data->pSIPClientData ||
        data->dwUIChoice < WTD_UI_ALL || data->dwUIChoice > WTD_UI_NOGOOD || data->fdwRevocationChecks > 1 ||
        (data->cbStruct == sizeof(*data) && data->pSignatureSettings))
        return E_INVALIDARG;
    if (data->dwUnionChoice != WTD_CHOICE_FILE)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    if (data->dwStateAction == WTD_STATEACTION_VERIFY) {
        if (data->hWVTStateData)
            return E_INVALIDARG;
        LONG error = state_open(data);
        if (error)
            return error;
    }
    return verify_file(data);
}

HRESULT WINAPI WinVerifyTrustEx(HWND window, GUID *action, WINTRUST_DATA *data)
{
    return WinVerifyTrust(window, action, data);
}
