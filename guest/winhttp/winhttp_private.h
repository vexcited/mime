/* shared WinHTTP handle objects and connection helpers. */
#pragma once
#ifndef MIME_BUILD_WINHTTP
#define MIME_BUILD_WINHTTP
#endif
#include <mime/win32/winsock2.h>
#include <mime/win32/ws2tcpip.h>
#include <mime/win32/winhttp.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>
#include <mime/win32/sspi.h>

#ifndef ERROR_INVALID_OPERATION
#define ERROR_INVALID_OPERATION 4317L
#endif
#ifndef ERROR_ARITHMETIC_OVERFLOW
#define ERROR_ARITHMETIC_OVERFLOW 534L
#endif

enum {
    WH_SESSION = WINHTTP_HANDLE_TYPE_SESSION,
    WH_CONNECT = WINHTTP_HANDLE_TYPE_CONNECT,
    WH_REQUEST = WINHTTP_HANDLE_TYPE_REQUEST,
    WH_RESOLVER = WINHTTP_HANDLE_TYPE_PROXY_RESOLVER,
    WH_WEBSOCKET = WINHTTP_HANDLE_TYPE_WEBSOCKET,
    WH_PROTOCOL = WINHTTP_HANDLE_TYPE_PROTOCOL,
};

/* WinHttpSetTimeouts and option timeouts; -1 means infinite. */
typedef struct {
    int Resolve;
    int Connect;
    int Send;
    int Receive;
    int ReceiveResponse;
} WH_TIMEOUTS;

typedef struct {
    char *Name;
    char *Value;
} WH_HEADER;

typedef struct {
    WH_HEADER *Items;
    DWORD Count;
    DWORD Capacity;
} WH_HEADERS;

/* TCP connection to a server or proxy, including TLS state. */
typedef struct WH_CONN {
    struct WH_CONN *Next;
    SOCKET Socket;
    WCHAR Host[INTERNET_MAX_HOST_NAME_LENGTH];
    INTERNET_PORT Port;
    BOOL Secure;
    BOOL ViaProxy;
    /* plaintext received and not consumed yet. */
    BYTE *Buffer;
    DWORD Start;
    DWORD End;
    DWORD Size;
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
    BOOL Eof;
    DWORD CipherStrength;
} WH_CONN;

enum { WH_BODY_NONE, WH_BODY_LENGTH, WH_BODY_CHUNKED, WH_BODY_CLOSE, WH_BODY_MEMORY };

enum { WH_OP_NONE, WH_OP_SEND, WH_OP_RECEIVE, WH_OP_QUERY, WH_OP_READ, WH_OP_WRITE };

typedef struct WH_OBJECT {
    LONG volatile Refs;
    DWORD Type;
    HINTERNET Handle;
    struct WH_OBJECT *Parent;
    CRITICAL_SECTION Lock;
    BOOL Closed;
    DWORD_PTR Context;
    WINHTTP_STATUS_CALLBACK Callback;
    DWORD NotifyFlags;
    WH_TIMEOUTS Timeouts;
    DWORD Flags;
    DWORD Decompression;
    DWORD RedirectPolicy;
    DWORD MaxRedirects;
    DWORD MaxResponseHeaderSize;
    DWORD MaxResponseDrainSize;
    DWORD DisableFeatures;
    DWORD SecurityFlags;
    DWORD SecureProtocols;
    DWORD ReadBufferSize;
    DWORD WriteBufferSize;
    PWSTR Agent;
    /* proxy of the session or the request, WINHTTP_ACCESS_TYPE_*. */
    DWORD AccessType;
    PWSTR Proxy;
    PWSTR ProxyBypass;
    /* session. */
    DWORD MaxConnsPerServer;
    DWORD MaxConnsPer10Server;
    WH_CONN *Idle;
    DWORD IdleCount;
    /* connection. */
    PWSTR Server;
    INTERNET_PORT Port;
    /* request. */
    PWSTR Verb;
    PWSTR Path;
    PWSTR Referrer;
    DWORD Major;
    DWORD Minor;
    BOOL Secure;
    PWSTR Host;
    INTERNET_PORT HostPort;
    WH_HEADERS Request;
    BOOL HeadersAdded;
    PWSTR User;
    PWSTR Password;
    DWORD AuthScheme;
    PWSTR ProxyUser;
    PWSTR ProxyPassword;
    DWORD ProxyAuthScheme;
    BOOL RejectUserPassword;
    /* the request body of WinHttpSendRequest, kept for redirects. */
    BYTE *Optional;
    DWORD OptionalLength;
    ULONGLONG TotalLength;
    ULONGLONG Written;
    BOOL ChunkedUpload;
    /* state: 0 opened, 1 sent, 2 response headers received. */
    DWORD State;
    WH_CONN *Conn;
    BOOL ConnReused;
    WH_HEADERS Response;
    DWORD StatusCode;
    char *StatusText;
    DWORD RespMajor;
    DWORD RespMinor;
    char *RawStatusLine;
    BOOL KeepAlive;
    DWORD BodyMode;
    ULONGLONG BodyLeft;
    BOOL ChunkEnd;
    BOOL BodyDone;
    BYTE *Memory;
    DWORD MemorySize;
    DWORD MemoryOffset;
    DWORD Redirects;
    BOOL UpgradeRequested;
    /* serialize asynchronous operations on the request worker. */
    HANDLE Worker;
    HANDLE Wake;
    DWORD Op;
    BOOL Busy;
    LPVOID OpBuffer;
    DWORD OpSize;
    /* proxy resolver. */
    BOOL ResolverUsed;
    BOOL ResolverBusy;
    BOOL ResolverDone;
    WINHTTP_PROXY_RESULT ResolverResult;
    /* websocket. */
    struct WH_OBJECT *Owner;
    BOOL WsClosedSent;
    BOOL WsClosedReceived;
    USHORT WsCloseStatus;
    BYTE WsCloseReason[WINHTTP_WEB_SOCKET_MAX_CLOSE_REASON_LENGTH];
    DWORD WsCloseReasonLength;
    DWORD WsFrameLeft;
    BYTE WsFrameType;
    BYTE WsMask[4];
    BOOL WsMasked;
    BOOL WsFinal;
    BOOL WsSendFragment;
} WH_OBJECT;

/* main.c. */
void WhStartup(void);
void *WhAlloc(SIZE_T size);
void *WhAllocZero(SIZE_T size);
void *WhRealloc(void *memory, SIZE_T size);
void WhFree(void *memory);
PWSTR WhDupW(LPCWSTR text);
PWSTR WhDupWn(LPCWSTR text, DWORD length);
char *WhDupA(const char *text);
char *WhDupAn(const char *text, DWORD length);
PWSTR WhAnsiToWide(const char *text, DWORD length);
char *WhWideToAnsi(LPCWSTR text, DWORD length, BOOL *lossy);
BOOL WhFail(DWORD error);
WH_OBJECT *WhCreate(DWORD type, WH_OBJECT *parent);
/* return a referenced handle object, or NULL with ERROR_INVALID_HANDLE. */
WH_OBJECT *WhReference(HINTERNET handle);
void WhAddRef(WH_OBJECT *object);
void WhRelease(WH_OBJECT *object);
WH_OBJECT *WhSession(WH_OBJECT *object);
/* send only notifications enabled by the callback mask. */
void WhNotify(WH_OBJECT *object, DWORD status, LPVOID info, DWORD length);
BOOL WhIsAsync(WH_OBJECT *object);

/* url.c. */
BOOL WhCrack(LPCWSTR url, DWORD length, DWORD flags, URL_COMPONENTS *components);

/* time.c. */
BOOL WhParseHttpDate(const char *text, SYSTEMTIME *time);

/* headers.c. */
void WhHeadersClear(WH_HEADERS *headers);
BOOL WhHeadersAppend(WH_HEADERS *headers, const char *name, DWORD name_length, const char *value,
                     DWORD value_length);
const char *WhHeadersFind(const WH_HEADERS *headers, const char *name, DWORD index);
void WhHeadersRemove(WH_HEADERS *headers, const char *name);
DWORD WhAddHeaders(WH_OBJECT *request, const char *text, DWORD length, DWORD modifiers);
const char *WhKnownHeaderName(DWORD level);

/* request.c. */
DWORD WhSendRequest(WH_OBJECT *request);
DWORD WhWrite(WH_OBJECT *request, const void *buffer, DWORD size, DWORD *written);
DWORD WhReceiveResponse(WH_OBJECT *request);
DWORD WhQueryAvailable(WH_OBJECT *request, DWORD *available);
DWORD WhRead(WH_OBJECT *request, void *buffer, DWORD size, DWORD *read);
void WhConnClose(WH_OBJECT *notify, WH_CONN *conn);
void WhDropConnection(WH_OBJECT *request);
void WhSessionCleanup(WH_OBJECT *session);
DWORD WhConnSend(WH_CONN *conn, const void *data, DWORD size, int timeout);
BOOL WhConnAppend(WH_CONN *conn, const void *data, DWORD size);
DWORD WhConnRecv(WH_CONN *conn, void *data, DWORD size, DWORD *got, int timeout);

/* tls.c. */
DWORD WhTlsHandshake(WH_CONN *conn, LPCWSTR host, DWORD security_flags, int timeout, DWORD *failure);
DWORD WhTlsSend(WH_CONN *conn, const void *data, DWORD size, int timeout);
DWORD WhTlsFill(WH_CONN *conn, int timeout);
void WhTlsClose(WH_CONN *conn);

/* proxy.c. */
BOOL WhReadDefaultProxy(WINHTTP_PROXY_INFO *info);
/* select the URL and scheme proxy; FALSE means direct connection. */
BOOL WhPickProxy(WH_OBJECT *request, LPCWSTR host, BOOL secure, PWSTR proxy_host, DWORD capacity,
                 INTERNET_PORT *proxy_port);

/* websocket.c. */
void WhWebSocketReleased(WH_OBJECT *socket);
/* read what an upgraded connection has, waiting for the first byte. */
DWORD WhReadSome(WH_OBJECT *object, void *data, DWORD length, DWORD *got);
