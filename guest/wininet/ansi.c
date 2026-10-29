/* ANSI wrappers using the process code page. */
#include "wininet_private.h"

/* copy length characters to the heap; -1 copies the terminated string. */
static PWSTR Widen(LPCSTR text, int length)
{
    if (!text)
        return nullptr;
    int chars = MultiByteToWideChar(CP_ACP, 0, text, length, nullptr, 0);
    PWSTR wide = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)chars + 1) * sizeof(WCHAR));
    if (wide) {
        MultiByteToWideChar(CP_ACP, 0, text, length, wide, chars);
        wide[chars] = 0;
    }
    return wide;
}

/* ANSI header length; -1 means a terminated string. */
static int HeaderLength(LPCSTR headers, DWORD length)
{
    return length == (DWORD)-1 || !length ? -1 : (int)length;
}

DECLSPEC_EXPORT HINTERNET WINAPI InternetOpenA(LPCSTR agent, DWORD access_type, LPCSTR proxy, LPCSTR proxy_bypass,
                                              DWORD flags)
{
    PWSTR wagent = Widen(agent, -1), wproxy = Widen(proxy, -1), wbypass = Widen(proxy_bypass, -1);
    HINTERNET handle = InternetOpenW(wagent, access_type, wproxy, wbypass, flags);
    InetFree(wagent);
    InetFree(wproxy);
    InetFree(wbypass);
    return handle;
}

DECLSPEC_EXPORT HINTERNET WINAPI InternetConnectA(HINTERNET session, LPCSTR server, INTERNET_PORT port, LPCSTR user,
                                                 LPCSTR password, DWORD service, DWORD flags, DWORD_PTR context)
{
    PWSTR wserver = Widen(server, -1), wuser = Widen(user, -1), wpassword = Widen(password, -1);
    HINTERNET handle = InternetConnectW(session, wserver, port, wuser, wpassword, service, flags, context);
    InetFree(wserver);
    InetFree(wuser);
    InetFree(wpassword);
    return handle;
}

DECLSPEC_EXPORT HINTERNET WINAPI InternetOpenUrlA(HINTERNET session, LPCSTR url, LPCSTR headers,
                                                 DWORD headers_length, DWORD flags, DWORD_PTR context)
{
    PWSTR wurl = Widen(url, -1), wheaders = Widen(headers, HeaderLength(headers, headers_length));
    HINTERNET handle = InternetOpenUrlW(session, wurl, wheaders, wheaders ? (DWORD)-1 : 0, flags, context);
    InetFree(wurl);
    InetFree(wheaders);
    return handle;
}

DECLSPEC_EXPORT HINTERNET WINAPI HttpOpenRequestA(HINTERNET connect, LPCSTR verb, LPCSTR object, LPCSTR version,
                                                 LPCSTR referrer, LPCSTR *accept_types, DWORD flags,
                                                 DWORD_PTR context)
{
    PWSTR wverb = Widen(verb, -1), wobject = Widen(object, -1), wversion = Widen(version, -1);
    PWSTR wreferrer = Widen(referrer, -1);
    DWORD count = 0;
    if (accept_types)
        while (accept_types[count])
            count++;
    LPCWSTR *types = nullptr;
    if (accept_types) {
        types = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ((SIZE_T)count + 1) * sizeof(*types));
        for (DWORD i = 0; types && i < count; i++)
            types[i] = Widen(accept_types[i], -1);
    }
    HINTERNET handle = HttpOpenRequestW(connect, wverb, wobject, wversion, wreferrer, types, flags, context);
    for (DWORD i = 0; types && i < count; i++)
        InetFree((PWSTR)types[i]);
    InetFree(types);
    InetFree(wverb);
    InetFree(wobject);
    InetFree(wversion);
    InetFree(wreferrer);
    return handle;
}

DECLSPEC_EXPORT BOOL WINAPI HttpSendRequestA(HINTERNET request, LPCSTR headers, DWORD headers_length,
                                            LPVOID optional, DWORD optional_length)
{
    PWSTR wide = Widen(headers, HeaderLength(headers, headers_length));
    BOOL ok = HttpSendRequestW(request, wide, wide ? (DWORD)-1 : 0, optional, optional_length);
    InetFree(wide);
    return ok;
}

static BOOL IsStringOption(DWORD option)
{
    return option == INTERNET_OPTION_USER_AGENT || option == INTERNET_OPTION_URL ||
           option == INTERNET_OPTION_USERNAME || option == INTERNET_OPTION_PASSWORD ||
           option == INTERNET_OPTION_PROXY_USERNAME || option == INTERNET_OPTION_PROXY_PASSWORD ||
           option == INTERNET_OPTION_SECURITY_CERTIFICATE;
}

/* convert string options back to ANSI, retaining the wide result's character count. */
DECLSPEC_EXPORT BOOL WINAPI InternetQueryOptionA(HINTERNET handle, DWORD option, LPVOID buffer, LPDWORD length)
{
    if (!IsStringOption(option) || !length)
        return InternetQueryOptionW(handle, option, buffer, length);
    DWORD wide_bytes = 0;
    if (InternetQueryOptionW(handle, option, nullptr, &wide_bytes) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;
    PWSTR wide = HeapAlloc(GetProcessHeap(), 0, wide_bytes);
    if (!wide)
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL ok = InternetQueryOptionW(handle, option, wide, &wide_bytes);
    if (ok) {
        int bytes = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (!buffer || (DWORD)bytes > *length) {
            *length = (DWORD)bytes;
            ok = InetFail(ERROR_INSUFFICIENT_BUFFER);
        } else {
            WideCharToMultiByte(CP_ACP, 0, wide, -1, buffer, bytes, nullptr, nullptr);
            *length = (DWORD)bytes - 1;
        }
    }
    HeapFree(GetProcessHeap(), 0, wide);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI InternetSetOptionA(HINTERNET handle, DWORD option, LPVOID buffer, DWORD length)
{
    if (option == INTERNET_OPTION_PROXY && buffer && length >= sizeof(INTERNET_PROXY_INFO)) {
        const INTERNET_PROXY_INFO *info = buffer;
        INTERNET_PROXY_INFO wide = {info->dwAccessType, Widen((LPCSTR)info->lpszProxy, -1),
                                    Widen((LPCSTR)info->lpszProxyBypass, -1)};
        BOOL ok = InternetSetOptionW(handle, option, &wide, sizeof(wide));
        InetFree((PWSTR)wide.lpszProxy);
        InetFree((PWSTR)wide.lpszProxyBypass);
        return ok;
    }
    if (!IsStringOption(option) || !buffer)
        return InternetSetOptionW(handle, option, buffer, length);
    PWSTR wide = Widen(buffer, length ? (int)length : -1);
    if (!wide)
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL ok = InternetSetOptionW(handle, option, wide, (DWORD)lstrlenW(wide));
    InetFree(wide);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI InternetGetLastResponseInfoA(LPDWORD error, LPSTR buffer, LPDWORD length)
{
    if (!length)
        return InetFail(ERROR_INVALID_PARAMETER);
    WCHAR wide[1024];
    DWORD chars = RTL_NUMBER_OF(wide);
    if (!InternetGetLastResponseInfoW(error, wide, &chars))
        return FALSE;
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide, (int)chars, nullptr, 0, nullptr, nullptr);
    if (!buffer || (DWORD)bytes + 1 > *length) {
        *length = (DWORD)bytes + 1;
        return InetFail(ERROR_INSUFFICIENT_BUFFER);
    }
    WideCharToMultiByte(CP_ACP, 0, wide, (int)chars, buffer, bytes, nullptr, nullptr);
    buffer[bytes] = 0;
    *length = (DWORD)bytes;
    return TRUE;
}

static PWSTR WidenCounted(LPCSTR text, DWORD length)
{
    return Widen(text, HeaderLength(text, length));
}

DECLSPEC_EXPORT BOOL WINAPI HttpSendRequestExA(HINTERNET request, LPINTERNET_BUFFERSA buffers_in,
                                              LPINTERNET_BUFFERSA buffers_out, DWORD flags, DWORD_PTR context)
{
    if (!buffers_in)
        return HttpSendRequestExW(request, nullptr, (LPINTERNET_BUFFERSW)buffers_out, flags, context);
    if (buffers_in->dwStructSize != sizeof(*buffers_in))
        return InetFail(ERROR_INVALID_PARAMETER);
    INTERNET_BUFFERSW wide = {sizeof(wide)};
    wide.lpcszHeader = buffers_in->lpcszHeader ? WidenCounted(buffers_in->lpcszHeader, buffers_in->dwHeadersLength)
                                               : nullptr;
    wide.dwHeadersLength = wide.lpcszHeader ? (DWORD)-1 : 0;
    wide.lpvBuffer = buffers_in->lpvBuffer;
    wide.dwBufferLength = buffers_in->dwBufferLength;
    wide.dwBufferTotal = buffers_in->dwBufferTotal;
    BOOL ok = HttpSendRequestExW(request, &wide, (LPINTERNET_BUFFERSW)buffers_out, flags, context);
    InetFree((PWSTR)wide.lpcszHeader);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI HttpEndRequestA(HINTERNET request, LPINTERNET_BUFFERSA buffers_out, DWORD flags,
                                           DWORD_PTR context)
{
    return HttpEndRequestW(request, (LPINTERNET_BUFFERSW)buffers_out, flags, context);
}

/* InternetConnect cannot create FTP connections, so only handle errors are possible. */
DECLSPEC_EXPORT HINTERNET WINAPI FtpFindFirstFileA(HINTERNET connect, LPCSTR search, LPWIN32_FIND_DATAA data, DWORD flags,
                                                  DWORD_PTR context)
{
    PWSTR wsearch = Widen(search, -1);
    WIN32_FIND_DATAW found;
    HINTERNET handle = FtpFindFirstFileW(connect, wsearch, data ? &found : nullptr, flags, context);
    DWORD error = GetLastError();
    HeapFree(GetProcessHeap(), 0, wsearch);
    SetLastError(error);
    return handle;
}

/* the FTP and Gopher calls fail on the handle or the protocol before they read any name, so the names are not widened. */
DECLSPEC_EXPORT BOOL WINAPI FtpGetFileA(HINTERNET connect, LPCSTR remote, LPCSTR local, BOOL fail_if_exists,
                                       DWORD attributes, DWORD flags, DWORD_PTR context)
{
    return FtpGetFileW(connect, nullptr, nullptr, fail_if_exists, attributes, flags, context);
}

DECLSPEC_EXPORT BOOL WINAPI FtpPutFileA(HINTERNET connect, LPCSTR local, LPCSTR remote, DWORD flags, DWORD_PTR context)
{
    return FtpPutFileW(connect, nullptr, nullptr, flags, context);
}

DECLSPEC_EXPORT BOOL WINAPI FtpDeleteFileA(HINTERNET connect, LPCSTR name)
{
    return FtpDeleteFileW(connect, nullptr);
}

DECLSPEC_EXPORT BOOL WINAPI FtpRenameFileA(HINTERNET connect, LPCSTR existing, LPCSTR name)
{
    return FtpRenameFileW(connect, nullptr, nullptr);
}

DECLSPEC_EXPORT HINTERNET WINAPI FtpOpenFileA(HINTERNET connect, LPCSTR name, DWORD access, DWORD flags,
                                             DWORD_PTR context)
{
    return FtpOpenFileW(connect, nullptr, access, flags, context);
}

DECLSPEC_EXPORT BOOL WINAPI FtpCreateDirectoryA(HINTERNET connect, LPCSTR name)
{
    return FtpCreateDirectoryW(connect, nullptr);
}

DECLSPEC_EXPORT BOOL WINAPI FtpRemoveDirectoryA(HINTERNET connect, LPCSTR name)
{
    return FtpRemoveDirectoryW(connect, nullptr);
}

DECLSPEC_EXPORT BOOL WINAPI FtpSetCurrentDirectoryA(HINTERNET connect, LPCSTR name)
{
    return FtpSetCurrentDirectoryW(connect, nullptr);
}

DECLSPEC_EXPORT BOOL WINAPI FtpGetCurrentDirectoryA(HINTERNET connect, LPSTR name, LPDWORD length)
{
    return FtpGetCurrentDirectoryW(connect, nullptr, length);
}

DECLSPEC_EXPORT BOOL WINAPI FtpCommandA(HINTERNET connect, BOOL expect_response, DWORD flags, LPCSTR command,
                                       DWORD_PTR context, HINTERNET *ftp_command)
{
    return FtpCommandW(connect, expect_response, flags, nullptr, context, ftp_command);
}

DECLSPEC_EXPORT BOOL WINAPI InternetFindNextFileA(HINTERNET find, LPVOID data)
{
    return InternetFindNextFileW(find, data);
}

DECLSPEC_EXPORT BOOL WINAPI GopherCreateLocatorA(LPCSTR host, INTERNET_PORT port, LPCSTR display, LPCSTR selector,
                                                DWORD type, LPSTR locator, LPDWORD length)
{
    return GopherCreateLocatorW(nullptr, port, nullptr, nullptr, type, nullptr, length);
}

DECLSPEC_EXPORT HINTERNET WINAPI GopherFindFirstFileA(HINTERNET connect, LPCSTR locator, LPCSTR search, LPVOID data,
                                                     DWORD flags, DWORD_PTR context)
{
    return GopherFindFirstFileW(connect, nullptr, nullptr, nullptr, flags, context);
}

DECLSPEC_EXPORT HINTERNET WINAPI GopherOpenFileA(HINTERNET connect, LPCSTR locator, LPCSTR view, DWORD flags,
                                                DWORD_PTR context)
{
    return GopherOpenFileW(connect, nullptr, nullptr, flags, context);
}

DECLSPEC_EXPORT BOOL WINAPI GopherGetAttributeA(HINTERNET connect, LPCSTR locator, LPCSTR name, LPBYTE buffer,
                                               DWORD length, LPDWORD returned, GOPHER_ATTRIBUTE_ENUMERATOR enumerator,
                                               DWORD_PTR context)
{
    return GopherGetAttributeW(connect, nullptr, nullptr, buffer, length, returned, enumerator, context);
}

/* a global or registry setting is applied to the process whatever the handle is. */
#define ISO_VALID_FLAGS_ 0x3

DECLSPEC_EXPORT BOOL WINAPI InternetSetOptionExW(HINTERNET handle, DWORD option, LPVOID buffer, DWORD length,
                                                DWORD flags)
{
    if (flags & ~ISO_VALID_FLAGS_)
        return InetFail(ERROR_INVALID_PARAMETER);
    return InternetSetOptionW(handle, option, buffer, length);
}

DECLSPEC_EXPORT BOOL WINAPI InternetSetOptionExA(HINTERNET handle, DWORD option, LPVOID buffer, DWORD length,
                                                DWORD flags)
{
    if (flags & ~ISO_VALID_FLAGS_)
        return InetFail(ERROR_INVALID_PARAMETER);
    return InternetSetOptionA(handle, option, buffer, length);
}
