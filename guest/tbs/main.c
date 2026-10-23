/*
 * tpm base services for a machine without a tpm. calls check their arguments like windows, then find no device, so
 * contexts cannot be created and every context handle is invalid.
 */
#define TBSAPI
#include <mime/win32/tbs.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

TBS_RESULT WINAPI Tbsi_GetDeviceInfo(UINT32 size, PVOID info)
{
    return TBS_E_TPM_NOT_FOUND;
}

BOOL WINAPI Tbsi_Is_Tpm_Present(void)
{
    return FALSE;
}

TBS_RESULT WINAPI Tbsi_Context_Create(PCTBS_CONTEXT_PARAMS params, PTBS_HCONTEXT context)
{
    if (!params)
        return TBS_E_BAD_PARAMETER;
    if (!context)
        return TBS_E_INVALID_OUTPUT_POINTER;
    return Tbsi_GetDeviceInfo(sizeof(TPM_DEVICE_INFO), nullptr);
}

TBS_RESULT WINAPI Tbsip_Context_Close(TBS_HCONTEXT context)
{
    return TBS_E_INVALID_CONTEXT;
}

TBS_RESULT WINAPI Tbsip_Cancel_Commands(TBS_HCONTEXT context)
{
    return TBS_E_INVALID_CONTEXT;
}

/* only locality zero, and the buffers are checked before the context. */
static TBS_RESULT submit(TBS_HCONTEXT context, TBS_COMMAND_LOCALITY locality, TBS_COMMAND_PRIORITY priority,
                         const BYTE *command, UINT32 size, PBYTE result, PUINT32 result_size)
{
    if (locality || priority > 0x80000000u)
        return TBS_E_BAD_PARAMETER;
    if (!result || !result_size)
        return TBS_E_INVALID_OUTPUT_POINTER;
    if (*result_size < 10)
        return TBS_E_INSUFFICIENT_BUFFER;
    if (!command || !size)
        return TBS_E_BAD_PARAMETER;
    return TBS_E_INVALID_CONTEXT;
}

TBS_RESULT WINAPI Tbsip_Submit_Command(TBS_HCONTEXT context, TBS_COMMAND_LOCALITY locality,
                                       TBS_COMMAND_PRIORITY priority, const BYTE *command, UINT32 size, PBYTE result,
                                       PUINT32 result_size)
{
    return submit(context, locality, priority, command, size, result, result_size);
}

TBS_RESULT WINAPI Tbsip_Submit_Command_NonBlocking(TBS_HCONTEXT context, TBS_COMMAND_LOCALITY locality,
                                                   TBS_COMMAND_PRIORITY priority, const BYTE *command, UINT32 size,
                                                   PBYTE result, PUINT32 result_size)
{
    return submit(context, locality, priority, command, size, result, result_size);
}

TBS_RESULT WINAPI Tbsi_Physical_Presence_Command(TBS_HCONTEXT context, const BYTE *input, UINT32 input_size,
                                                 PBYTE output, PUINT32 output_size)
{
    if (!input)
        return TBS_E_BAD_PARAMETER;
    if (!output || !output_size)
        return TBS_E_INVALID_OUTPUT_POINTER;
    if (input_size > 0x40000 || *output_size > 0x40000)
        return TBS_E_BUFFER_TOO_LARGE;
    return TBS_E_INVALID_CONTEXT;
}

/* windows keeps the measured boot log in the registry. a machine without a tpm has none. */
TBS_RESULT WINAPI Tbsi_Get_TCG_Log(TBS_HCONTEXT context, PBYTE log, PUINT32 size)
{
    if (!size)
        return TBS_E_INVALID_OUTPUT_POINTER;
    UINT32 capacity = *size;
    *size = 0;
    HKEY key;
    LONG error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\IntegrityServices", 0,
                               KEY_QUERY_VALUE, &key);
    /* windows always has the key, so a missing key means a missing log. */
    if (error)
        return error == ERROR_FILE_NOT_FOUND ? TBS_E_NO_EVENT_LOG : HRESULT_FROM_WIN32((ULONG)error);
    DWORD bytes = 0;
    error = RegQueryValueExW(key, L"WBCL", nullptr, nullptr, nullptr, &bytes);
    if (error) {
        RegCloseKey(key);
        return error == ERROR_FILE_NOT_FOUND ? TBS_E_NO_EVENT_LOG : HRESULT_FROM_WIN32((ULONG)error);
    }
    if (!log || capacity < bytes) {
        RegCloseKey(key);
        *size = bytes;
        return log ? TBS_E_INSUFFICIENT_BUFFER : TBS_SUCCESS;
    }
    error = RegQueryValueExW(key, L"WBCL", nullptr, nullptr, log, &bytes);
    RegCloseKey(key);
    if (error)
        return HRESULT_FROM_WIN32((ULONG)error);
    *size = bytes;
    return TBS_SUCCESS;
}

TBS_RESULT WINAPI Tbsi_Get_TCG_Log_Ex(UINT32 type, PBYTE log, PUINT32 size)
{
    if (!size)
        return TBS_E_BAD_PARAMETER;
    return TBS_E_TPM_NOT_FOUND;
}

TBS_RESULT WINAPI Tbsi_Revoke_Attestation(void)
{
    return TBS_E_TPM_NOT_FOUND;
}

/* the two keys windows keeps in the tpm. */
TBS_RESULT WINAPI Tbsi_Create_Windows_Key(UINT32 key)
{
    if (key != 0x81000001u && key != 0x81010001u)
        return TBS_E_BAD_PARAMETER;
    return TBS_E_TPM_NOT_FOUND;
}
