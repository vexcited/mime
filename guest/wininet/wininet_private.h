/* shared WinINet handles, connections and helpers. */
#pragma once
/* not every processor build defines it, and the exports need it. */
#ifndef MIME_BUILD_WININET
#define MIME_BUILD_WININET
#endif
#include <mime/win32/winsock2.h>
#include <mime/win32/ws2tcpip.h>
#include <mime/win32/wininet.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>
#include <mime/win32/sspi.h>
#include <mime/win32/wincrypt.h>

/* option slots for INTERNET_OPTION_CONNECT_TIMEOUT through INTERNET_OPTION_DATA_RECEIVE_TIMEOUT. */
#define INET_TIMEOUT_FIRST INTERNET_OPTION_CONNECT_TIMEOUT
#define INET_TIMEOUT_LAST INTERNET_OPTION_DATA_RECEIVE_TIMEOUT
#define INET_TIMEOUT_COUNT (INET_TIMEOUT_LAST - INET_TIMEOUT_FIRST + 1)
#define INET_TIMEOUT(object, option) ((object)->Timeouts[(option) - INET_TIMEOUT_FIRST])

/* header lists use the ANSI code page. */
typedef struct {
    char *Name;
    char *Value;
} INET_HEADER;

typedef struct {
    INET_HEADER *Items;
    DWORD Count;
    DWORD Capacity;
} INET_HEADERS;

/* TCP connection to a server or proxy, including TLS state. */
typedef struct INET_CONN {
    struct INET_CONN *Next;
    SOCKET Socket;
    /* the pool key: the origin, and the proxy the connection goes through. */
    WCHAR Host[INTERNET_MAX_HOST_NAME_LENGTH];
    INTERNET_PORT Port;
    BOOL Secure;
    WCHAR Proxy[INTERNET_MAX_HOST_NAME_LENGTH];
    INTERNET_PORT ProxyPort;
    DWORD IdleSince;
    /* plaintext received and not consumed yet. */
    BYTE *Buffer;
    DWORD Start;
    DWORD End;
    DWORD Size;
    BOOL Eof;
    /* tls. */
    BOOL Tls;
    CredHandle Credentials;
    CtxtHandle Context;
    DWORD HeaderSize;
    DWORD TrailerSize;
    DWORD MaxMessage;
    BYTE *Raw;
    DWORD RawUsed;
    DWORD RawSize;
    PCCERT_CONTEXT Certificate;
    /* validation errors in _SECURITY_FLAG_CERT_* bits, and channel strength. */
    DWORD CertErrors;
    DWORD Protocol;
    DWORD CipherStrength;
} INET_CONN;

enum { INET_BODY_NONE, INET_BODY_LENGTH, INET_BODY_CHUNKED, INET_BODY_CLOSE, INET_BODY_MEMORY };

/* request states. */
enum { INET_OPENED, INET_SENDING, INET_RECEIVED };

typedef struct INET_OBJECT {
    LONG volatile Refs;
    DWORD Type;
    HINTERNET Handle;
    struct INET_OBJECT *Parent;
    SRWLOCK Lock;
    BOOL Closed;
    /* hidden InternetOpenUrl connection without its own handle. */
    BOOL Hidden;
    DWORD Flags;
    DWORD_PTR Context;
    INTERNET_STATUS_CALLBACK Callback;
    /* InternetSetStatusCallbackW requires conversion of selected strings. */
    BOOL WideCallback;
    DWORD Timeouts[INET_TIMEOUT_COUNT];
    BOOL Decoding;
    DWORD ReadBufferSize;
    DWORD WriteBufferSize;
    /* connection or request credential options. */
    PWSTR User;
    PWSTR Password;
    PWSTR ProxyUser;
    PWSTR ProxyPassword;
    /* session. */
    PWSTR Agent;
    DWORD AccessType;
    PWSTR Proxy;
    PWSTR ProxyBypass;
    /* connection. */
    PWSTR Server;
    INTERNET_PORT Port;
    DWORD Service;
    /* HttpOpenRequest parameters; redirects update the target. */
    PWSTR Verb;
    PWSTR Object;
    PWSTR Version;
    PWSTR Referrer;
    INET_HEADERS Request;
    PWSTR Host;
    INTERNET_PORT HostPort;
    BOOL Secure;
    DWORD SecurityFlags;
    /* the body of HttpSendRequest, kept for redirects and authentication. */
    BYTE *Optional;
    DWORD OptionalLength;
    ULONGLONG TotalLength;
    ULONGLONG Written;
    BOOL SendEx;
    DWORD State;
    INET_CONN *Conn;
    BOOL ConnReused;
    INET_HEADERS Response;
    DWORD StatusCode;
    char *StatusLine;
    char *StatusText;
    char *VersionText;
    BOOL KeepAlive;
    DWORD BodyMode;
    ULONGLONG BodyLeft;
    BOOL BodyDone;
    BYTE *Memory;
    DWORD MemorySize;
    DWORD MemoryOffset;
    ULONGLONG Position;
    DWORD Redirects;
    BOOL AuthTried;
    PCCERT_CONTEXT Certificate;
    DWORD CertErrors;
    DWORD Protocol;
    DWORD CipherStrength;
    /* resent headers replace the original request headers. */
    BOOL Sent;
    /* rebuild the Cookie header from the jar for every request. */
    BOOL JarCookie;
    BOOL ProxyAuthTried;
    BOOL ViaProxy;
    /* serialize asynchronous operations on the request worker. */
    HANDLE Worker;
    HANDLE Wake;
    BOOL Busy;
    BOOL HasOp;
    /* the worker sends closing notification if the handle closes during an operation. */
    BOOL CloseDeferred;
    struct INET_OP {
        DWORD Kind;
        DWORD_PTR Context;
        void *Buffer;
        DWORD Size;
        DWORD Flags;
        LPDWORD Count;
        void *Buffers;
        /* the parameters of HttpSendRequest. */
        PWSTR Headers;
        DWORD HeadersLength;
        DWORD Total;
        DWORD_PTR Result;
    } Op;
} INET_OBJECT;

typedef struct INET_OP INET_OP;

enum { INET_OP_SEND, INET_OP_END, INET_OP_READ, INET_OP_READ_EX, INET_OP_QUERY, INET_OP_WRITE, INET_OP_OPEN_URL };

/* handle.c. */
PWSTR InetDuplicate(LPCWSTR text);
PWSTR InetDuplicateN(LPCWSTR text, DWORD length);
char *InetDuplicateA(const char *text, DWORD length);
void *InetAlloc(SIZE_T size);
void *InetAllocZero(SIZE_T size);
void *InetRealloc(void *memory, SIZE_T size);
void InetFree(void *memory);
/* convert through the ANSI code page; -1 means a terminated string. */
char *InetNarrow(LPCWSTR text, int length);
PWSTR InetWiden(const char *text, int length);
/* return a referenced handle object, or NULL with ERROR_INVALID_HANDLE. */
INET_OBJECT *InetReference(HINTERNET handle);
void InetAddRef(INET_OBJECT *object);
void InetRelease(INET_OBJECT *object);
/* return a referenced object of the required type, or NULL with an error. */
INET_OBJECT *InetReferenceType(HINTERNET handle, DWORD type);
BOOL InetFail(DWORD error);
INET_OBJECT *InetSession(INET_OBJECT *object);
/* Windows requires both a callback and nonzero context for notifications. */
void InetNotify(INET_OBJECT *object, DWORD status, LPVOID information, DWORD length);
/* convert selected W callback notifications from ANSI to wide. */
void InetNotifyString(INET_OBJECT *object, DWORD status, LPCWSTR text);
/* asynchronous handles need INTERNET_FLAG_ASYNC on the session, a callback and a context. */
BOOL InetIsAsync(INET_OBJECT *object, DWORD_PTR context);
HINTERNET InetOpenRequest(INET_OBJECT *connect, LPCWSTR verb, LPCWSTR object, LPCWSTR version, LPCWSTR referrer,
                          LPCWSTR *accept_types, DWORD flags, DWORD_PTR context);
HINTERNET InetConnect(INET_OBJECT *session, LPCWSTR server, INTERNET_PORT port, LPCWSTR user, LPCWSTR password,
                      DWORD service, DWORD flags, DWORD_PTR context);
/* remove the table reference; other references keep the object alive. */
void InetDetach(INET_OBJECT *object);
/* current request URL after redirects. */
PWSTR InetRequestUrl(INET_OBJECT *request);

/* http.c. */
void InetHeadersClear(INET_HEADERS *headers);
BOOL InetHeadersAppend(INET_HEADERS *headers, const char *name, DWORD name_length, const char *value,
                       DWORD value_length);
const char *InetHeadersFind(const INET_HEADERS *headers, const char *name, DWORD index);
void InetHeadersRemove(INET_HEADERS *headers, const char *name);
DWORD InetAddRequestHeaders(INET_OBJECT *request, const char *text, DWORD length, DWORD modifiers);
/* return the HttpQueryInfo header name, or NULL for non-header queries. */
const char *InetLevelName(DWORD level);

/* request.c. */
/* HttpSendRequest takes headers and body immediately and discards the previous response. */
DWORD InetPrepareSend(INET_OBJECT *request, LPCWSTR headers, DWORD headers_length, const void *optional,
                      DWORD optional_length, DWORD total_length, BOOL ex);
DWORD InetEndRequest(INET_OBJECT *request);
DWORD InetRead(INET_OBJECT *request, void *buffer, DWORD size, DWORD *read);
DWORD InetWrite(INET_OBJECT *request, const void *buffer, DWORD size, DWORD *written);
DWORD InetQueryAvailable(INET_OBJECT *request, BOOL wait, DWORD *available);
/* whether a read or query can complete without waiting for the network. */
BOOL InetDataReady(INET_OBJECT *request);
void InetRequestCleanup(INET_OBJECT *request);
/* abort pending waits; TRUE means the worker will send closing notification. */
BOOL InetRequestClosing(INET_OBJECT *request);
/* run on the asynchronous worker with ERROR_IO_PENDING, otherwise synchronously. */
BOOL InetStart(INET_OBJECT *request, INET_OP *op);
DWORD InetRunOp(INET_OBJECT *request, INET_OP *op);

/* conn.c. */
void InetStartup(void);
DWORD InetConnOpen(INET_OBJECT *request, LPCWSTR host, INTERNET_PORT port, INET_CONN **result);
DWORD InetConnSend(INET_CONN *conn, const void *data, DWORD size, DWORD timeout);
DWORD InetConnRecv(INET_CONN *conn, void *data, DWORD size, DWORD *got, DWORD timeout);
DWORD InetConnFill(INET_CONN *conn, DWORD timeout);
BOOL InetConnAppend(INET_CONN *conn, const void *data, DWORD size);
DWORD InetConnBuffered(INET_CONN *conn);
DWORD InetConnReadLine(INET_CONN *conn, char **line, DWORD limit, DWORD timeout, DWORD *total);
void InetConnClose(INET_OBJECT *notify, INET_CONN *conn);
INET_CONN *InetPoolTake(LPCWSTR host, INTERNET_PORT port, BOOL secure, LPCWSTR proxy, INTERNET_PORT proxy_port);
void InetPoolPut(INET_CONN *conn);

/* tls.c. */
DWORD InetTlsHandshake(INET_CONN *conn, LPCWSTR host, DWORD security_flags, DWORD timeout);
DWORD InetTlsSend(INET_CONN *conn, const void *data, DWORD size, DWORD timeout);
DWORD InetTlsFill(INET_CONN *conn, DWORD timeout);
void InetTlsClose(INET_CONN *conn);
/* INTERNET_CERTIFICATE_INFOW with LocalAlloc strings. */
BOOL InetCertificateInfo(PCCERT_CONTEXT certificate, DWORD strength, INTERNET_CERTIFICATE_INFOW *info);

/* proxy.c. */
/* read Internet Explorer proxy and bypass lists; FALSE means no proxy. */
BOOL InetReadIEProxy(PWSTR *proxy, PWSTR *bypass);
/* select the host proxy; FALSE means direct connection. */
BOOL InetPickProxy(INET_OBJECT *request, LPCWSTR host, BOOL secure, PWSTR proxy, DWORD capacity,
                   INTERNET_PORT *proxy_port);

/* cookie.c. */
/* Cookie header for the URL, or NULL if no cookies match. */
char *InetCookiesFor(LPCWSTR host, LPCWSTR path, BOOL secure);
void InetCookieStore(LPCWSTR host, LPCWSTR path, const char *set_cookie);

/* time.c. */
BOOL InetParseTime(const char *text, SYSTEMTIME *time);
PCCERT_CONTEXT InetCertificateDuplicate(PCCERT_CONTEXT certificate);
void InetCertificateFree(PCCERT_CONTEXT certificate);

#ifndef ERROR_ARITHMETIC_OVERFLOW
#define ERROR_ARITHMETIC_OVERFLOW 534L
#endif
#ifndef ERROR_INVALID_OPERATION
#define ERROR_INVALID_OPERATION 4317L
#endif
