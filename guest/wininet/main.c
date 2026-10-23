/*
 * wininet entry point, connection state, response info and the error dialog.
 */
#include "wininet_private.h"
#include <mime/stub.h>
#include <mime/win32/shlwapi.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* the host network is reported as a LAN connection. */
DECLSPEC_EXPORT BOOL WINAPI InternetGetConnectedState(LPDWORD flags, DWORD reserved)
{
    if (flags)
        *flags = INTERNET_CONNECTION_LAN;
    return TRUE;
}

/*
 * dialing is a no-op because the machine is always online. reject flags beyond the four dialing flags and nonzero
 * reserved values. like Windows with its remote access library, return TRUE and clear last error.
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
