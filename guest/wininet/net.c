/*
 * URL opening, and the FTP, Gopher and web socket entry points of a machine without those protocols: they validate the
 * handle and fail as Windows does when no connection of that kind can exist.
 */
#include "wininet_private.h"

/* validate the handle, then report the wrong-type or wrong-state error. */
static BOOL FailHandle(HINTERNET handle, DWORD type, DWORD error)
{
    INET_OBJECT *object = InetReference(handle);
    if (!object)
        return FALSE;
    if (type && object->Type != type)
        error = ERROR_INTERNET_INCORRECT_HANDLE_TYPE;
    InetRelease(object);
    return InetFail(error);
}

/*
 * HTTP and HTTPS URLs create a hidden connection and GET request with supplied headers. return the request handle, or
 * deliver it through INTERNET_STATUS_REQUEST_COMPLETE for INTERNET_FLAG_ASYNC.
 */
DECLSPEC_EXPORT HINTERNET WINAPI InternetOpenUrlW(HINTERNET session, LPCWSTR url, LPCWSTR headers,
                                                 DWORD headers_length, DWORD flags, DWORD_PTR context)
{
    INET_OBJECT *parent = InetReferenceType(session, INTERNET_HANDLE_TYPE_INTERNET);
    if (!parent)
        return nullptr;
    URL_COMPONENTSW parts = {sizeof(parts)};
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = 1;
    parts.dwUrlPathLength = parts.dwExtraInfoLength = 1;
    DWORD error = ERROR_SUCCESS;
    if (!url || !*url)
        error = ERROR_INVALID_PARAMETER;
    else if (!InternetCrackUrlW(url, 0, 0, &parts))
        error = GetLastError();
    else if (parts.nScheme == INTERNET_SCHEME_FTP)
        error = ERROR_INTERNET_CANNOT_CONNECT;
    else if (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)
        error = ERROR_INTERNET_UNRECOGNIZED_SCHEME;
    else if (!parts.dwHostNameLength)
        error = ERROR_INTERNET_INVALID_URL;
    if (error) {
        InetRelease(parent);
        SetLastError(error);
        return nullptr;
    }
    PWSTR host = InetDuplicateN(parts.lpszHostName, parts.dwHostNameLength);
    PWSTR user = parts.dwUserNameLength ? InetDuplicateN(parts.lpszUserName, parts.dwUserNameLength) : nullptr;
    PWSTR password = parts.dwPasswordLength ? InetDuplicateN(parts.lpszPassword, parts.dwPasswordLength) : nullptr;
    /* send path and query; retain fragments on the client. */
    DWORD extra = parts.dwExtraInfoLength;
    for (DWORD i = 0; i < extra; i++)
        if (parts.lpszExtraInfo[i] == '#')
            extra = i;
    PWSTR object = InetAlloc(((SIZE_T)parts.dwUrlPathLength + extra + 2) * sizeof(WCHAR));
    if (object) {
        DWORD length = parts.dwUrlPathLength;
        memcpy(object, parts.lpszUrlPath, length * sizeof(WCHAR));
        if (!length)
            object[length++] = '/';
        memcpy(object + length, parts.lpszExtraInfo, extra * sizeof(WCHAR));
        object[length + extra] = 0;
    }
    if (flags & INTERNET_FLAG_EXISTING_CONNECT)
        flags |= INTERNET_FLAG_KEEP_CONNECTION;
    if (parts.nScheme == INTERNET_SCHEME_HTTPS)
        flags |= INTERNET_FLAG_SECURE;
    HINTERNET request = nullptr;
    if (!host || !object || (parts.dwUserNameLength && !user) || (parts.dwPasswordLength && !password)) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } else {
        HINTERNET connect = InetConnect(parent, host, parts.nPort, user, password, INTERNET_SERVICE_HTTP,
                                        flags & ~INTERNET_FLAG_EXISTING_CONNECT, 0);
        INET_OBJECT *connection = connect ? InetReference(connect) : nullptr;
        if (connection) {
            connection->Context = context;
            request = InetOpenRequest(connection, nullptr, object, nullptr, nullptr, nullptr, flags, context);
            /* only the request retains the hidden connection. */
            InetDetach(connection);
            InetRelease(connection);
        }
    }
    InetFree(host);
    InetFree(user);
    InetFree(password);
    InetFree(object);
    InetRelease(parent);
    if (!request)
        return nullptr;
    INET_OBJECT *object_request = InetReference(request);
    if (!object_request)
        return nullptr;
    error = InetPrepareSend(object_request, headers, headers_length, nullptr, 0, 0, FALSE);
    BOOL ok = FALSE;
    if (!error) {
        INET_OP op = {INET_OP_OPEN_URL, context};
        ok = InetStart(object_request, &op);
        error = ok ? ERROR_SUCCESS : GetLastError();
    }
    InetRelease(object_request);
    if (error == ERROR_IO_PENDING) {
        SetLastError(ERROR_IO_PENDING);
        return nullptr;
    }
    if (error) {
        InternetCloseHandle(request);
        SetLastError(error);
        return nullptr;
    }
    SetLastError(ERROR_SUCCESS);
    return request;
}

/* FtpFindFirstFile and GopherFindFirstFile never succeed, so no find handles exist. */
DECLSPEC_EXPORT BOOL WINAPI InternetFindNextFileW(HINTERNET find, LPVOID data)
{
    return FailHandle(find, INTERNET_HANDLE_TYPE_FTP_FIND, ERROR_NO_MORE_FILES);
}

/* FTP connections cannot be created; supplied handles are invalid or have another type. */
#define FAIL_FTP(handle) FailHandle(handle, INTERNET_HANDLE_TYPE_CONNECT_FTP, ERROR_INTERNET_CANNOT_CONNECT)

DECLSPEC_EXPORT HINTERNET WINAPI FtpFindFirstFileW(HINTERNET connect, LPCWSTR search, LPWIN32_FIND_DATAW data,
                                                  DWORD flags, DWORD_PTR context)
{
    FAIL_FTP(connect);
    return nullptr;
}

DECLSPEC_EXPORT BOOL WINAPI FtpGetFileW(HINTERNET connect, LPCWSTR remote, LPCWSTR local, BOOL fail_if_exists,
                                       DWORD attributes, DWORD flags, DWORD_PTR context)
{
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpPutFileW(HINTERNET connect, LPCWSTR local, LPCWSTR remote, DWORD flags,
                                       DWORD_PTR context)
{
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpDeleteFileW(HINTERNET connect, LPCWSTR name)
{
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpRenameFileW(HINTERNET connect, LPCWSTR existing, LPCWSTR name)
{
    return FAIL_FTP(connect);
}

/* no ftp file handle exists, so the size of one is asked of a handle that is not one. */
DECLSPEC_EXPORT DWORD WINAPI FtpGetFileSize(HINTERNET file, LPDWORD high)
{
    if (high)
        *high = 0;
    FailHandle(file, INTERNET_HANDLE_TYPE_FTP_FILE, ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
    return INVALID_FILE_SIZE;
}

DECLSPEC_EXPORT HINTERNET WINAPI FtpOpenFileW(HINTERNET connect, LPCWSTR name, DWORD access, DWORD flags,
                                             DWORD_PTR context)
{
    FAIL_FTP(connect);
    return nullptr;
}

DECLSPEC_EXPORT BOOL WINAPI FtpCreateDirectoryW(HINTERNET connect, LPCWSTR name)
{
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpRemoveDirectoryW(HINTERNET connect, LPCWSTR name)
{
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpSetCurrentDirectoryW(HINTERNET connect, LPCWSTR name)
{
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpGetCurrentDirectoryW(HINTERNET connect, LPWSTR name, LPDWORD length)
{
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpCommandW(HINTERNET connect, BOOL expect_response, DWORD flags, LPCWSTR command,
                                       DWORD_PTR context, HINTERNET *ftp_command)
{
    if (ftp_command)
        *ftp_command = nullptr;
    return FAIL_FTP(connect);
}

/* Gopher calls report a missing protocol, as Windows has since Internet Explorer 7. */
DECLSPEC_EXPORT BOOL WINAPI GopherCreateLocatorW(LPCWSTR host, INTERNET_PORT port, LPCWSTR display, LPCWSTR selector,
                                                DWORD type, LPWSTR locator, LPDWORD length)
{
    return InetFail(ERROR_INTERNET_PROTOCOL_NOT_FOUND);
}

DECLSPEC_EXPORT HINTERNET WINAPI GopherFindFirstFileW(HINTERNET connect, LPCWSTR locator, LPCWSTR search,
                                                     LPGOPHER_FIND_DATAW data, DWORD flags, DWORD_PTR context)
{
    SetLastError(ERROR_INTERNET_PROTOCOL_NOT_FOUND);
    return nullptr;
}

DECLSPEC_EXPORT HINTERNET WINAPI GopherOpenFileW(HINTERNET connect, LPCWSTR locator, LPCWSTR view, DWORD flags,
                                                DWORD_PTR context)
{
    SetLastError(ERROR_INTERNET_PROTOCOL_NOT_FOUND);
    return nullptr;
}

DECLSPEC_EXPORT BOOL WINAPI GopherGetAttributeW(HINTERNET connect, LPCWSTR locator, LPCWSTR name, LPBYTE buffer,
                                               DWORD length, LPDWORD returned, GOPHER_ATTRIBUTE_ENUMERATOR enumerator,
                                               DWORD_PTR context)
{
    return InetFail(ERROR_INTERNET_PROTOCOL_NOT_FOUND);
}

/* no URL cache entries exist. */
DECLSPEC_EXPORT BOOL WINAPI GetUrlCacheEntryInfoExW(LPCWSTR url, LPVOID info, LPDWORD size, LPWSTR redirect,
                                                   LPDWORD redirect_size, LPVOID reserved, DWORD flags)
{
    if (!url || !*url || redirect || redirect_size || reserved)
        return InetFail(ERROR_INVALID_PARAMETER);
    return InetFail(ERROR_FILE_NOT_FOUND);
}

DECLSPEC_EXPORT BOOL WINAPI GetUrlCacheEntryInfoW(LPCWSTR url, LPVOID info, LPDWORD size)
{
    return GetUrlCacheEntryInfoExW(url, info, size, nullptr, nullptr, nullptr, 0);
}

/*
 * web sockets are not made: the upgrade is not negotiated for a request, so there is no socket to send on, receive from
 * or close, and a request has no upgrade to complete.
 */
#define INTERNET_HANDLE_TYPE_WEB_SOCKET_ 0x6f

DECLSPEC_EXPORT HINTERNET WINAPI HttpWebSocketCompleteUpgrade(HINTERNET request, DWORD_PTR context)
{
    FailHandle(request, INTERNET_HANDLE_TYPE_HTTP_REQUEST, ERROR_INTERNET_INCORRECT_HANDLE_STATE);
    return nullptr;
}

DECLSPEC_EXPORT BOOL WINAPI HttpWebSocketSend(HINTERNET socket, DWORD type, PVOID buffer, DWORD length)
{
    if (!buffer != !length)
        return InetFail(ERROR_INVALID_PARAMETER);
    return FailHandle(socket, INTERNET_HANDLE_TYPE_WEB_SOCKET_, ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
}

DECLSPEC_EXPORT BOOL WINAPI HttpWebSocketReceive(HINTERNET socket, PVOID buffer, DWORD length, DWORD *read, DWORD *type)
{
    if (!buffer || !length || !read || !type)
        return InetFail(ERROR_INVALID_PARAMETER);
    return FailHandle(socket, INTERNET_HANDLE_TYPE_WEB_SOCKET_, ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
}

DECLSPEC_EXPORT BOOL WINAPI HttpWebSocketClose(HINTERNET socket, USHORT status, PVOID reason, DWORD length)
{
    return FailHandle(socket, INTERNET_HANDLE_TYPE_WEB_SOCKET_, ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
}

DECLSPEC_EXPORT BOOL WINAPI HttpWebSocketShutdown(HINTERNET socket, USHORT status, PVOID reason, DWORD length)
{
    return FailHandle(socket, INTERNET_HANDLE_TYPE_WEB_SOCKET_, ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
}

DECLSPEC_EXPORT BOOL WINAPI HttpWebSocketQueryCloseStatus(HINTERNET socket, USHORT *status, PVOID reason, DWORD length,
                                                         DWORD *consumed)
{
    return FailHandle(socket, INTERNET_HANDLE_TYPE_WEB_SOCKET_, ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
}
