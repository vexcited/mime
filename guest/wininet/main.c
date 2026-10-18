/*
 * wininet entry point, connection state, response info and the error dialog.
 */
#include "wininet_private.h"
#include <mime/stub.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* mime's guest reaches the network through the host, which it reports as a lan connection. */
DECLSPEC_EXPORT BOOL WINAPI InternetGetConnectedState(LPDWORD flags, DWORD reserved)
{
    if (flags)
        *flags = INTERNET_CONNECTION_LAN;
    return TRUE;
}

/* no ftp or gopher server ever answered, so there is no response text: an empty string and error 0. */
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
