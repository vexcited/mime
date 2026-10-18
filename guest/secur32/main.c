/*
 * secur32 entry point and the helpers shared by its source files.
 *
 * windows' secur32 forwards the security support provider interface to
 * sspicli.dll, which talks to lsa. mime has neither, so secur32 implements
 * the interface itself: the package table, credential handles, the user
 * name queries and the schannel tls client are real, while the other
 * packages fail to establish a context: there is no kerberos, ntlm or
 * digest implementation.
 */
#include "secur32_private.h"

DWORD SecTlsCallData = TLS_OUT_OF_INDEXES;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        SecTlsCallData = TlsAlloc();
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

/* the cases of sspicli's SspNtStatusToSecStatus that secur32 can meet. */
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
