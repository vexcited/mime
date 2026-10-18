/*
 * wininet internals: the HINTERNET objects, the connections under them and the helpers the files share.
 */
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

/* headers as wininet keeps them, in the ansi code page. */
typedef struct {
    char *Name;
    char *Value;
} INET_HEADER;

typedef struct {
    INET_HEADER *Items;
    DWORD Count;
    DWORD Capacity;
} INET_HEADERS;

/* a tcp connection to a server or a proxy, with its tls state. */
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
    /* the _SECURITY_FLAG_CERT_* errors validation found, and the strength of the channel. */
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
    /* the connection InternetOpenUrl makes, which has no handle of its own. */
    BOOL Hidden;
    DWORD Flags;
    DWORD_PTR Context;
    INTERNET_STATUS_CALLBACK Callback;
    /* set through InternetSetStatusCallbackW, whose callbacks get some strings wide. */
    BOOL WideCallback;
    DWORD Timeouts[INET_TIMEOUT_COUNT];
    BOOL Decoding;
    DWORD ReadBufferSize;
    DWORD WriteBufferSize;
    /* the user and password options, of a connection or a request. */
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
    /* request: what HttpOpenRequest asked for, the target follows redirects. */
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
    /* the request went out once, so headers given again replace those of the first time. */
    BOOL Sent;
    /* the Cookie header is the jar's, to be recomputed for each request. */
    BOOL JarCookie;
    BOOL ProxyAuthTried;
    BOOL ViaProxy;
    /* asynchronous operations, one at a time on the worker thread of the request. */
    HANDLE Worker;
    HANDLE Wake;
    BOOL Busy;
    BOOL HasOp;
    /* the handle closed while an operation ran: its worker sends the closing notification. */
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

/* handle.c */
PWSTR InetDuplicate(LPCWSTR text);
PWSTR InetDuplicateN(LPCWSTR text, DWORD length);
char *InetDuplicateA(const char *text, DWORD length);
void *InetAlloc(SIZE_T size);
void *InetAllocZero(SIZE_T size);
void *InetRealloc(void *memory, SIZE_T size);
void InetFree(void *memory);
/* converts through the ansi code page; length -1 converts a terminated string. */
char *InetNarrow(LPCWSTR text, int length);
PWSTR InetWiden(const char *text, int length);
/* the referenced object of a handle, or NULL with ERROR_INVALID_HANDLE. */
INET_OBJECT *InetReference(HINTERNET handle);
void InetAddRef(INET_OBJECT *object);
void InetRelease(INET_OBJECT *object);
/* the referenced object of a handle of the given type, else NULL with the error set. */
INET_OBJECT *InetReferenceType(HINTERNET handle, DWORD type);
BOOL InetFail(DWORD error);
INET_OBJECT *InetSession(INET_OBJECT *object);
/* windows only notifies handles that have both a callback and a nonzero context. */
void InetNotify(INET_OBJECT *object, DWORD status, LPVOID information, DWORD length);
/* a string notification: ansi, or wide for the ones the W callbacks get converted. */
void InetNotifyString(INET_OBJECT *object, DWORD status, LPCWSTR text);
/* asynchronous handles need INTERNET_FLAG_ASYNC on the session, a callback and a context. */
BOOL InetIsAsync(INET_OBJECT *object, DWORD_PTR context);
HINTERNET InetOpenRequest(INET_OBJECT *connect, LPCWSTR verb, LPCWSTR object, LPCWSTR version, LPCWSTR referrer,
                          LPCWSTR *accept_types, DWORD flags, DWORD_PTR context);
HINTERNET InetConnect(INET_OBJECT *session, LPCWSTR server, INTERNET_PORT port, LPCWSTR user, LPCWSTR password,
                      DWORD service, DWORD flags, DWORD_PTR context);
/* takes an object out of the handle table, leaving it to the references it has. */
void InetDetach(INET_OBJECT *object);
/* the url of a request as it stands, redirects included. */
PWSTR InetRequestUrl(INET_OBJECT *request);

/* http.c */
void InetHeadersClear(INET_HEADERS *headers);
BOOL InetHeadersAppend(INET_HEADERS *headers, const char *name, DWORD name_length, const char *value,
                       DWORD value_length);
const char *InetHeadersFind(const INET_HEADERS *headers, const char *name, DWORD index);
void InetHeadersRemove(INET_HEADERS *headers, const char *name);
DWORD InetAddRequestHeaders(INET_OBJECT *request, const char *text, DWORD length, DWORD modifiers);
/* the header name of a HttpQueryInfo level, NULL for the levels that are not headers. */
const char *InetLevelName(DWORD level);

/* request.c */
/* what HttpSendRequest does at once: the headers and the body to send are taken, the last response dropped. */
DWORD InetPrepareSend(INET_OBJECT *request, LPCWSTR headers, DWORD headers_length, const void *optional,
                      DWORD optional_length, DWORD total_length, BOOL ex);
DWORD InetEndRequest(INET_OBJECT *request);
DWORD InetRead(INET_OBJECT *request, void *buffer, DWORD size, DWORD *read);
DWORD InetWrite(INET_OBJECT *request, const void *buffer, DWORD size, DWORD *written);
DWORD InetQueryAvailable(INET_OBJECT *request, BOOL wait, DWORD *available);
/* whether a read or query can complete without waiting for the network. */
BOOL InetDataReady(INET_OBJECT *request);
void InetRequestCleanup(INET_OBJECT *request);
/* aborts what a closing request waits for, TRUE when its worker sends the closing notification. */
BOOL InetRequestClosing(INET_OBJECT *request);
/* runs an operation, on the worker thread of an asynchronous request with ERROR_IO_PENDING. */
BOOL InetStart(INET_OBJECT *request, INET_OP *op);
DWORD InetRunOp(INET_OBJECT *request, INET_OP *op);

/* conn.c */
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

/* tls.c */
DWORD InetTlsHandshake(INET_CONN *conn, LPCWSTR host, DWORD security_flags, DWORD timeout);
DWORD InetTlsSend(INET_CONN *conn, const void *data, DWORD size, DWORD timeout);
DWORD InetTlsFill(INET_CONN *conn, DWORD timeout);
void InetTlsClose(INET_CONN *conn);
/* the certificate of a connection as INTERNET_CERTIFICATE_INFOW, its strings from LocalAlloc. */
BOOL InetCertificateInfo(PCCERT_CONTEXT certificate, DWORD strength, INTERNET_CERTIFICATE_INFOW *info);

/* proxy.c */
/* the proxy list and bypass list of internet explorer's settings, FALSE when they name no proxy. */
BOOL InetReadIEProxy(PWSTR *proxy, PWSTR *bypass);
/* the proxy of a request to host, FALSE to connect directly. */
BOOL InetPickProxy(INET_OBJECT *request, LPCWSTR host, BOOL secure, PWSTR proxy, DWORD capacity,
                   INTERNET_PORT *proxy_port);

/* cookie.c */
/* the Cookie header value for a url, NULL when no cookie applies. */
char *InetCookiesFor(LPCWSTR host, LPCWSTR path, BOOL secure);
void InetCookieStore(LPCWSTR host, LPCWSTR path, const char *set_cookie);

/* time.c */
BOOL InetParseTime(const char *text, SYSTEMTIME *time);
PCCERT_CONTEXT InetCertificateDuplicate(PCCERT_CONTEXT certificate);
void InetCertificateFree(PCCERT_CONTEXT certificate);

#ifndef ERROR_ARITHMETIC_OVERFLOW
#define ERROR_ARITHMETIC_OVERFLOW 534L
#endif
#ifndef ERROR_INVALID_OPERATION
#define ERROR_INVALID_OPERATION 4317L
#endif
