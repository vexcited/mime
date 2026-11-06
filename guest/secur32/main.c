/*
 * secur32 entry point and shared helpers.
 *
 * windows' secur32 forwards the sspi interface to sspicli.dll, which talks to lsa. mime has neither, so secur32
 * implements the interface itself: the package table, credential handles, user name queries and the schannel tls
 * client are real. there is no kerberos, ntlm or digest, so the other packages never establish a context.
 */
#include "secur32_private.h"

DWORD SecTlsCallData = TLS_OUT_OF_INDEXES;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        SecTlsCallData = TlsAlloc();
    else if (reason == DLL_PROCESS_DETACH && !reserved && SecTlsCallData != TLS_OUT_OF_INDEXES)
        TlsFree(SecTlsCallData);
    return TRUE;
}

PWSTR SecAnsiToWide(PCSTR text)
{
    if (!text)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    PWSTR out = n > 0 ? SecHeapAlloc(n * sizeof(WCHAR)) : nullptr;
    if (out)
        MultiByteToWideChar(CP_ACP, 0, text, -1, out, n);
    return out;
}

PSTR SecWideToAnsi(PCWSTR text)
{
    if (!text)
        return nullptr;
    int n = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    PSTR out = n > 0 ? SecHeapAlloc(n) : nullptr;
    if (out)
        WideCharToMultiByte(CP_ACP, 0, text, -1, out, n, nullptr, nullptr);
    return out;
}

/* only the status mappings secur32 can hit. */
SECURITY_STATUS SecNtStatusToSecStatus(NTSTATUS status)
{
    if (status == STATUS_SUCCESS || (status & 0x1FFF0000) == 0x00090000)
        return status;
    switch (status) {
    case STATUS_INVALID_PARAMETER:
    case STATUS_NAME_TOO_LONG:
        return SEC_E_INVALID_TOKEN;
    case STATUS_NO_MEMORY:
        return SEC_E_INSUFFICIENT_MEMORY;
    case STATUS_BUFFER_TOO_SMALL:
        return SEC_E_BUFFER_TOO_SMALL;
    case STATUS_INVALID_HANDLE:
        return SEC_E_INVALID_HANDLE;
    case STATUS_NOT_SUPPORTED:
        return SEC_E_UNSUPPORTED_FUNCTION;
    default:
        return SEC_E_INTERNAL_ERROR;
    }
}
