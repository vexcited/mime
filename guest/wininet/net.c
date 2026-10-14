/*
 * the operations that need a server: opening urls, file transfers, ftp and gopher. mime's guest has no
 * internet transport yet, so each validates its handle and fails cleanly.
 */
#include "wininet_private.h"
#include <mime/stub.h>

/* checks a handle, then fails with the error a valid handle of the wrong type or state gets. */
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

DECLSPEC_EXPORT HINTERNET WINAPI InternetOpenUrlW(HINTERNET session, LPCWSTR url, LPCWSTR headers,
                                                 DWORD headers_length, DWORD flags, DWORD_PTR context)
{
    MIME_STUB("wininet");
    INET_OBJECT *object = InetReferenceType(session, INTERNET_HANDLE_TYPE_INTERNET);
    if (!object)
        return NULL;
    InetRelease(object);
    URL_COMPONENTSW components = {sizeof(components)};
    components.dwSchemeLength = 1;
    if (!url || !*url) {
        SetLastError(ERROR_INVALID_PARAMETER);
    } else if (!InternetCrackUrlW(url, 0, 0, &components)) {
        /* the crack error stands. */
    } else if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS &&
               components.nScheme != INTERNET_SCHEME_FTP) {
        SetLastError(ERROR_INTERNET_UNRECOGNIZED_SCHEME);
    } else {
        SetLastError(ERROR_INTERNET_CANNOT_CONNECT);
    }
    return NULL;
}

/* only InternetOpenUrl, FtpOpenFile and sent requests make readable handles, and none succeeds yet. */
DECLSPEC_EXPORT BOOL WINAPI InternetReadFile(HINTERNET file, LPVOID buffer, DWORD length, LPDWORD read)
{
    MIME_STUB("wininet");
    if (read)
        *read = 0;
    return FailHandle(file, INTERNET_HANDLE_TYPE_HTTP_REQUEST, ERROR_INTERNET_INCORRECT_HANDLE_STATE);
}

DECLSPEC_EXPORT BOOL WINAPI InternetWriteFile(HINTERNET file, LPCVOID buffer, DWORD length, LPDWORD written)
{
    MIME_STUB("wininet");
    if (written)
        *written = 0;
    return FailHandle(file, INTERNET_HANDLE_TYPE_HTTP_REQUEST, ERROR_INTERNET_INCORRECT_HANDLE_STATE);
}

DECLSPEC_EXPORT BOOL WINAPI InternetQueryDataAvailable(HINTERNET file, LPDWORD available, DWORD flags,
                                                      DWORD_PTR context)
{
    MIME_STUB("wininet");
    if (available)
        *available = 0;
    return FailHandle(file, INTERNET_HANDLE_TYPE_HTTP_REQUEST, ERROR_INTERNET_INCORRECT_HANDLE_STATE);
}

DECLSPEC_EXPORT DWORD WINAPI InternetSetFilePointer(HINTERNET file, LONG distance, PVOID distance_high, DWORD method,
                                                   DWORD_PTR context)
{
    MIME_STUB("wininet");
    FailHandle(file, INTERNET_HANDLE_TYPE_HTTP_REQUEST, ERROR_INTERNET_INCORRECT_HANDLE_STATE);
    return INVALID_SET_FILE_POINTER;
}

/* find handles come from FtpFindFirstFile and GopherFindFirstFile, which never succeed. */
DECLSPEC_EXPORT BOOL WINAPI InternetFindNextFileW(HINTERNET find, LPVOID data)
{
    MIME_STUB("wininet");
    return FailHandle(find, INTERNET_HANDLE_TYPE_FTP_FIND, ERROR_NO_MORE_FILES);
}

/* ftp connections fail in InternetConnect, so every handle given here is invalid or of another type. */
#define FAIL_FTP(handle) FailHandle(handle, INTERNET_HANDLE_TYPE_CONNECT_FTP, ERROR_INTERNET_CANNOT_CONNECT)

DECLSPEC_EXPORT HINTERNET WINAPI FtpFindFirstFileW(HINTERNET connect, LPCWSTR search, LPWIN32_FIND_DATAW data,
                                                  DWORD flags, DWORD_PTR context)
{
    MIME_STUB("wininet");
    FAIL_FTP(connect);
    return NULL;
}

DECLSPEC_EXPORT BOOL WINAPI FtpGetFileW(HINTERNET connect, LPCWSTR remote, LPCWSTR local, BOOL fail_if_exists,
                                       DWORD attributes, DWORD flags, DWORD_PTR context)
{
    MIME_STUB("wininet");
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpPutFileW(HINTERNET connect, LPCWSTR local, LPCWSTR remote, DWORD flags,
                                       DWORD_PTR context)
{
    MIME_STUB("wininet");
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpDeleteFileW(HINTERNET connect, LPCWSTR name)
{
    MIME_STUB("wininet");
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpRenameFileW(HINTERNET connect, LPCWSTR existing, LPCWSTR name)
{
    MIME_STUB("wininet");
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT HINTERNET WINAPI FtpOpenFileW(HINTERNET connect, LPCWSTR name, DWORD access, DWORD flags,
                                             DWORD_PTR context)
{
    MIME_STUB("wininet");
    FAIL_FTP(connect);
    return NULL;
}

DECLSPEC_EXPORT BOOL WINAPI FtpCreateDirectoryW(HINTERNET connect, LPCWSTR name)
{
    MIME_STUB("wininet");
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpRemoveDirectoryW(HINTERNET connect, LPCWSTR name)
{
    MIME_STUB("wininet");
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpSetCurrentDirectoryW(HINTERNET connect, LPCWSTR name)
{
    MIME_STUB("wininet");
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpGetCurrentDirectoryW(HINTERNET connect, LPWSTR name, LPDWORD length)
{
    MIME_STUB("wininet");
    return FAIL_FTP(connect);
}

DECLSPEC_EXPORT BOOL WINAPI FtpCommandW(HINTERNET connect, BOOL expect_response, DWORD flags, LPCWSTR command,
                                       DWORD_PTR context, HINTERNET *ftp_command)
{
    MIME_STUB("wininet");
    if (ftp_command)
        *ftp_command = NULL;
    return FAIL_FTP(connect);
}

/* windows dropped gopher in internet explorer 7, so every gopher call reports the protocol as missing. */
DECLSPEC_EXPORT BOOL WINAPI GopherCreateLocatorW(LPCWSTR host, INTERNET_PORT port, LPCWSTR display, LPCWSTR selector,
                                                DWORD type, LPWSTR locator, LPDWORD length)
{
    MIME_STUB("wininet");
    return InetFail(ERROR_INTERNET_PROTOCOL_NOT_FOUND);
}

DECLSPEC_EXPORT HINTERNET WINAPI GopherFindFirstFileW(HINTERNET connect, LPCWSTR locator, LPCWSTR search,
                                                     LPGOPHER_FIND_DATAW data, DWORD flags, DWORD_PTR context)
{
    MIME_STUB("wininet");
    SetLastError(ERROR_INTERNET_PROTOCOL_NOT_FOUND);
    return NULL;
}

DECLSPEC_EXPORT HINTERNET WINAPI GopherOpenFileW(HINTERNET connect, LPCWSTR locator, LPCWSTR view, DWORD flags,
                                                DWORD_PTR context)
{
    MIME_STUB("wininet");
    SetLastError(ERROR_INTERNET_PROTOCOL_NOT_FOUND);
    return NULL;
}

DECLSPEC_EXPORT BOOL WINAPI GopherGetAttributeW(HINTERNET connect, LPCWSTR locator, LPCWSTR name, LPBYTE buffer,
                                               DWORD length, LPDWORD returned, GOPHER_ATTRIBUTE_ENUMERATOR enumerator,
                                               DWORD_PTR context)
{
    MIME_STUB("wininet");
    return InetFail(ERROR_INTERNET_PROTOCOL_NOT_FOUND);
}
