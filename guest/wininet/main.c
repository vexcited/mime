/*
 * wininet entry point, connection state, response info and the error dialog.
 */
#include "wininet_private.h"
#include <mime/stub.h>
#include <mime/win32/iphlpapi.h>
#include <mime/win32/shlwapi.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* the host network is reported as a LAN connection, unless the machine has none at all. */
DECLSPEC_EXPORT BOOL WINAPI InternetGetConnectedState(LPDWORD flags, DWORD reserved)
{
    static DWORD(NETIOAPI_API_ * hint_of)(NL_NETWORK_CONNECTIVITY_HINT *);
    if (!hint_of) {
        HMODULE iphlpapi = LoadLibraryW(L"iphlpapi.dll");
        hint_of = iphlpapi ? (void *)GetProcAddress(iphlpapi, "GetNetworkConnectivityHint") : nullptr;
    }
    NL_NETWORK_CONNECTIVITY_HINT hint = {};
    bool connected = !hint_of || hint_of(&hint) || hint.ConnectivityLevel != NetworkConnectivityLevelHintNone;
    if (flags)
        *flags = connected ? INTERNET_CONNECTION_LAN : 0;
    return connected;
}

/* there is nothing to attempt, the machine is connected as it is. only the reserved argument is checked. */
DECLSPEC_EXPORT DWORD WINAPI InternetAttemptConnect(DWORD reserved)
{
    return reserved ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
}

#define FLAG_ICC_FORCE_CONNECTION 1
#define ERROR_NOT_CONNECTED_ 2250

/*
 * without the flag the answer is whether any server was reached so far. with it a connection is made to the host of the
 * address, and closed again.
 */
DECLSPEC_EXPORT BOOL WINAPI InternetCheckConnectionW(LPCWSTR url, DWORD flags, DWORD reserved)
{
    if (reserved)
        return InetFail(ERROR_INVALID_PARAMETER);
    if (!(flags & FLAG_ICC_FORCE_CONNECTION)) {
        if (InetServerReached)
            return TRUE;
        return InetFail(ERROR_NOT_CONNECTED_);
    }
    if (!url)
        return InetFail(ERROR_INTERNET_INVALID_OPERATION);
    WCHAR host[INTERNET_MAX_HOST_NAME_LENGTH];
    URL_COMPONENTSW parts = {sizeof(parts)};
    parts.lpszHostName = host;
    parts.dwHostNameLength = ARRAYSIZE(host);
    if (!InternetCrackUrlW(url, 0, 0, &parts))
        return FALSE;
    INTERNET_PORT port = parts.nPort;
    if (!port)
        port = parts.nScheme == INTERNET_SCHEME_HTTPS ? 443 : parts.nScheme == INTERNET_SCHEME_FTP ? 21 : 80;
    INET_OBJECT probe = {};
    INET_TIMEOUT(&probe, INTERNET_OPTION_CONNECT_TIMEOUT) = 60000;
    INET_CONN *conn;
    DWORD error = InetConnOpen(&probe, host, port, &conn);
    if (error)
        return InetFail(error);
    InetConnClose(nullptr, conn);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI InternetCheckConnectionA(LPCSTR url, DWORD flags, DWORD reserved)
{
    if (!url)
        return InternetCheckConnectionW(nullptr, flags, reserved);
    PWSTR wide = InetWiden(url, -1);
    if (!wide)
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL ok = InternetCheckConnectionW(wide, flags, reserved);
    DWORD error = GetLastError();
    InetFree(wide);
    SetLastError(error);
    return ok;
}

/*
 * dialing is a no-op as the machine has no modem. reject flags beyond the four dialing flags and nonzero reserved
 * values. like Windows with its remote access library, return TRUE and clear last error.
 */
DECLSPEC_EXPORT BOOL WINAPI InternetAutodial(DWORD flags, HWND window)
{
    if (flags & ~0xFu) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI InternetAutodialHangup(DWORD reserved)
{
    if (reserved) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/*
 * combine URLs through shlwapi with WinINet rules. ICU_NO_ENCODE reverses the engine's encoding flag. report a short
 * buffer with ERROR_INSUFFICIENT_BUFFER and required size, allocation failure with ERROR_NOT_ENOUGH_MEMORY, and other
 * failures with ERROR_INVALID_PARAMETER.
 */
#define COMBINE_FLAGS(flags) (((flags) ^ ICU_NO_ENCODE) | 0x80000000u)

static BOOL combine_result(HRESULT hr)
{
    if (hr >= 0) {
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    SetLastError(hr == E_POINTER ? ERROR_INSUFFICIENT_BUFFER : hr == HRESULT_FROM_WIN32(ERROR_OUTOFMEMORY) ?
                 ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
    return FALSE;
}

DECLSPEC_EXPORT BOOL WINAPI InternetCombineUrlW(LPCWSTR base, LPCWSTR relative, LPWSTR buffer, LPDWORD length,
                                                DWORD flags)
{
    if (!base || !relative || !length)
        return combine_result(E_INVALIDARG);
    return combine_result(UrlCombineW(base, relative, buffer, length, COMBINE_FLAGS(flags)));
}

DECLSPEC_EXPORT BOOL WINAPI InternetCombineUrlA(LPCSTR base, LPCSTR relative, LPSTR buffer, LPDWORD length,
                                                DWORD flags)
{
    if (!base || !relative || !length)
        return combine_result(E_INVALIDARG);
    return combine_result(UrlCombineA(base, relative, buffer, length, COMBINE_FLAGS(flags)));
}

/* without FTP or Gopher responses, return empty response text and error 0. */
DECLSPEC_EXPORT BOOL WINAPI InternetGetLastResponseInfoW(LPDWORD error, LPWSTR buffer, LPDWORD length)
{
    if (!error || !length)
        return InetFail(ERROR_INVALID_PARAMETER);
    *error = 0;
    if (buffer && *length)
        buffer[0] = 0;
    *length = 0;
    return TRUE;
}

DECLSPEC_EXPORT DWORD WINAPI InternetErrorDlg(HWND window, HINTERNET request, DWORD error, DWORD flags, LPVOID *data)
{
    MIME_STUB("wininet");
    return ERROR_CANCELLED;
}
