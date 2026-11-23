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
    /* tls, with the credential of the server's slot it holds a reference on. */
    BOOL Tls;
    struct WH_CRED *Cred;
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
    /* WINHTTP_OPTION_CLIENT_CERT_CONTEXT: set at all, and the certificate, none for WINHTTP_NO_CLIENT_CERT_CONTEXT. */
    BOOL ClientCertSet;
    const void *ClientCert;
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
    /* the schannel credential of each server, webio's SSL credential slots. */
    struct WH_CRED_SLOT *CredSlots;
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
    /* option 207, the upgrade to a protocol of the program, which excludes the websocket one. */
    BOOL ProtocolRequested;
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
DWORD WhTlsHandshake(WH_OBJECT *session, WH_CONN *conn, LPCWSTR host, DWORD security_flags, DWORD protocols,
                     BOOL cert_set, const void *cert, int timeout, DWORD *failure);
/* the credentials a session keeps for its servers. */
void WhTlsSessionCleanup(WH_OBJECT *session);
const void *WhTlsCertDuplicate(const void *cert);
void WhTlsCertFree(const void *cert);
DWORD WhTlsSend(WH_CONN *conn, const void *data, DWORD size, int timeout);
DWORD WhTlsFill(WH_CONN *conn, int timeout);
void WhTlsClose(WH_CONN *conn);
/* the server's certificate and its chain, as crypt32 references the caller frees. */
DWORD WhTlsServerCert(WH_CONN *conn, const void **cert);
DWORD WhTlsServerChain(WH_CONN *conn, const void **chain);

/* proxy.c. */
BOOL WhReadDefaultProxy(WINHTTP_PROXY_INFO *info);
/* select the URL and scheme proxy; FALSE means direct connection. */
BOOL WhPickProxy(WH_OBJECT *request, LPCWSTR host, BOOL secure, PWSTR proxy_host, DWORD capacity,
                 INTERNET_PORT *proxy_port);

/* automatic proxy of a url, by the entries WinHttpFreeProxyResult frees. */
DWORD WhAutoProxy(LPCWSTR url, const WINHTTP_AUTOPROXY_OPTIONS *options, WINHTTP_PROXY_RESULT *result);
BOOL WhValidAutoProxyOptions(const WINHTTP_AUTOPROXY_OPTIONS *options);

/* internal.c. */
DWORD WhReferenceOfType(HINTERNET handle, DWORD type, WH_OBJECT **object);
void WhNotifyProxyConfigChange(void);

/* settings.c: the proxy settings wininet and winhttp exchange, whose strings are task memory. */
typedef struct {
    DWORD StructSize;
    DWORD Flags;
    DWORD CurrentSettingsVersion;
    PWSTR ConnectionName;
    PWSTR Proxy;
    PWSTR ProxyBypass;
    PWSTR AutoconfigUrl;
    PWSTR AutoconfigSecondaryUrl;
    DWORD AutoDiscoveryFlags;
    PWSTR LastKnownGoodAutoConfigUrl;
    DWORD AutoconfigReloadDelayMins;
    FILETIME LastKnownDetectTime;
    DWORD DetectedInterfaceIpCount;
    PDWORD DetectedInterfaceIp;
    DWORD NetworkKeyCount;
    PVOID NetworkKeys;
} WH_PROXY_SETTINGS;

#define WH_OPTION_UPGRADE_TO_PROTOCOL 207
#define WH_PROXY_SETTINGS_SIZE ((DWORD)sizeof(WH_PROXY_SETTINGS))
#define WH_NETWORK_KEY_SIZE 128

#define WH_SETTINGS_DIRECT 0x1
#define WH_SETTINGS_PROXY 0x2
#define WH_SETTINGS_CONFIG_URL 0x4
#define WH_SETTINGS_AUTO_DETECT 0x8

void *WhTaskAlloc(SIZE_T size);
void WhTaskFree(void *memory);
/* a copy in task memory, nothing for no string. */
DWORD WhTaskDupW(LPCWSTR text, PWSTR *copy);
void WhFreeProxySettings(WH_PROXY_SETTINGS *settings);
DWORD WhCopyProxySettings(const WH_PROXY_SETTINGS *from, WH_PROXY_SETTINGS *to);
DWORD WhReadProxySettings(LPCWSTR connection, BOOL fall_back, BOOL auto_discover, DWORD *version, BOOL *defaults,
                          WH_PROXY_SETTINGS *settings);

/* websocket.c. */
void WhWebSocketReleased(WH_OBJECT *socket);
/* read what an upgraded connection has, waiting for the first byte. */
DWORD WhReadSome(WH_OBJECT *object, void *data, DWORD length, DWORD *got);
