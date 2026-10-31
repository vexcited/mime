/*
 * the http server driver's own types, shared by http.c, httpurl.c and httpnet.c. all of it runs under one lock.
 */
#ifndef MIME_HTTP_P_H
#define MIME_HTTP_P_H

#include "../ntos.h"

#include <mime/afd.h>
#include <mime/http.h>

#define FILE_DEVICE_NETWORK 0x12

#define HTTP_MAX_URL_CHARS 4096
#define HTTP_MAX_HEADERS 128
#define HTTP_MAX_HEADER_BYTES 16384
#define HTTP_MAX_UNKNOWN 120
#define HTTP_BODY_LIMIT 262144

#define STATUS_CONNECTION_INVALID ((NTSTATUS)0xC000023AL)
#define STATUS_DATATYPE_MISALIGNMENT_ERROR ((NTSTATUS)0xC0000345L)
#define STATUS_ADDRESS_ALREADY_EXISTS ((NTSTATUS)0xC000020AL)

typedef struct HTTP_OPEN HTTP_OPEN;
typedef struct HTTP_SESSION HTTP_SESSION;
typedef struct HTTP_GROUP HTTP_GROUP;
typedef struct HTTP_QUEUE HTTP_QUEUE;
typedef struct HTTP_URLREG HTTP_URLREG;
typedef struct HTTP_ENDPOINT HTTP_ENDPOINT;
typedef struct HTTP_CONN HTTP_CONN;
typedef struct HTTP_REQ HTTP_REQ;

enum { HTTP_OPEN_CHANNEL = 1, HTTP_OPEN_QUEUE = 2 };

/* what a file of the device is: the control channel or a handle of a request queue. */
struct HTTP_OPEN {
    ULONG Kind;
    BOOLEAN Closed;
    BOOLEAN Wow;
    USHORT VersionMajor, VersionMinor;
    ULONG QueueType; /* 1 for an ordinary handle, 2 for a controller. */
    ULONG ProcessId;
    HTTP_QUEUE *Queue;
    LIST_ENTRY Links;    /* in g_opens */
    LIST_ENTRY Sessions; /* server sessions a channel created */
};

typedef struct HTTP_TIMEOUTS {
    ULONG Flags;
    USHORT EntityBody, DrainEntityBody, RequestQueue, IdleConnection, HeaderWait;
    ULONG MinSendRate;
} HTTP_TIMEOUTS;

struct HTTP_SESSION {
    LIST_ENTRY Links;       /* in g_sessions */
    LIST_ENTRY OwnerLinks;  /* in its channel's list */
    LIST_ENTRY Groups;
    ULONG64 Id;
    HTTP_OPEN *Owner;
    USHORT VersionMajor, VersionMinor;
    HTTP_TIMEOUTS Timeouts;
    ULONG State; /* HTTP_ENABLED_STATE */
    BOOLEAN Auto;
    UCHAR Auth[72];
    ULONG AuthLength;
};

struct HTTP_GROUP {
    LIST_ENTRY Links; /* in its session */
    LIST_ENTRY Urls;
    ULONG64 Id;
    HTTP_SESSION *Session;
    HTTP_QUEUE *Queue;
    HTTP_TIMEOUTS Timeouts;
    ULONG State;
    UCHAR Auth[72];
    ULONG AuthLength;
    UCHAR Qos[24];
    ULONG QosLength;
};

struct HTTP_QUEUE {
    LIST_ENTRY Links; /* in g_queues */
    PWSTR Name;       /* NULL for an unnamed queue */
    USHORT NameLength;
    ULONG Handles;
    USHORT VersionMajor, VersionMinor;
    ULONG MaxLength;
    ULONG State; /* HTTP_ENABLED_STATE */
    ULONG Verbosity;
    BOOLEAN Shutdown;
    BOOLEAN Closed;
    HTTP_SESSION *AutoSession;
    HTTP_GROUP *AutoGroup;
    ULONG Bound; /* url groups bound to it */
    LIST_ENTRY Pending; /* requests that arrived and wait for HttpReceiveHttpRequest */
    ULONG Enqueued;
};

enum { HTTP_URL_HOST, HTTP_URL_STRONG, HTTP_URL_WEAK, HTTP_URL_IP, HTTP_URL_SUBDOMAIN };

struct HTTP_URLREG {
    LIST_ENTRY GroupLinks;
    LIST_ENTRY Links; /* in g_urls */
    HTTP_GROUP *Group;
    ULONG64 Context;
    ULONG Kind;
    USHORT Port;
    UCHAR IpFamily; /* HTTP_URL_IP: 2 or 23 */
    UCHAR Ip[16];
    PWSTR Host;      /* lower case, no port, no brackets */
    USHORT HostLength;
    PWSTR Text;      /* the registered prefix as http.sys keeps it */
    USHORT TextLength;
    ULONG SegmentCount;
    USHORT Segments[64]; /* start of each path segment in Text, then the length in the next array */
    USHORT SegmentLengths[64];
};

struct HTTP_ENDPOINT {
    LIST_ENTRY Links;
    USHORT Port;
    ULONG Sock4, Sock6; /* host sockets, 0 when there is none */
    ULONG Urls;
    BOOLEAN Dirty;
};

typedef struct HTTP_OUT {
    LIST_ENTRY Links;
    PUCHAR Data;
    ULONG Length;
    ULONG Sent;
    PFILE_OBJECT File; /* a file the response is read from, a block at a time */
    ULONG64 FileOffset, FileRemaining;
    struct HTTP_WAIT *Wait; /* completes with the last byte */
    BOOLEAN Last; /* the end of a response */
    BOOLEAN Close; /* close the connection after it */
} HTTP_OUT;

enum { HTTP_REQ_NEW, HTTP_REQ_QUEUED, HTTP_REQ_DELIVERED, HTTP_REQ_DONE };

struct HTTP_REQ {
    LIST_ENTRY Links; /* in the queue's pending list */
    ULONG64 Id;
    HTTP_CONN *Conn;
    HTTP_QUEUE *Queue;
    HTTP_URLREG *Url;
    ULONG64 Context;
    ULONG State;
    USHORT VerMajor, VerMinor;
    ULONG Verb;
    PCHAR Method;
    USHORT MethodLength;
    PCHAR RawUrl;
    USHORT RawUrlLength;
    PWSTR FullUrl, Host, AbsPath, Query;
    USHORT FullUrlLength, HostLength, AbsPathLength, QueryLength; /* bytes */
    struct {
        PCHAR Value;
        USHORT Length;
    } Known[HTTP_REQUEST_HEADER_COUNT];
    ULONG UnknownCount;
    struct {
        PCHAR Name, Value;
        USHORT NameLength, ValueLength;
    } Unknown[HTTP_MAX_UNKNOWN];
    PUCHAR Blob; /* owns the strings above */
    ULONG BlobSize;
    ULONG64 HeaderBytes;
    /* the body. */
    BOOLEAN Chunked;
    BOOLEAN HasBody;
    BOOLEAN BodyDone;     /* the framing ended */
    BOOLEAN Expect100;
    BOOLEAN Sent100;
    ULONG64 ContentLength;
    ULONG64 BodyTotal;    /* bytes of body received */
    ULONG ChunkState;
    ULONG64 ChunkLeft;
    ULONG LineLength;
    PUCHAR Body;
    ULONG BodyLength, BodyCapacity;
    BOOLEAN Opaque;       /* an upgrade: both ways are raw bytes now */
    /* the response. */
    BOOLEAN RespStarted, RespDone, RespChunked, RespClose, HeadRequest;
    BOOLEAN KeepAlive;    /* the connection serves another request after this one */
    BOOLEAN Cancelled;
};

struct HTTP_CONN {
    LIST_ENTRY Links; /* in g_conns */
    ULONG64 Id;
    ULONG Sock;
    HTTP_ENDPOINT *Endpoint;
    MimeNetAddr Remote, Local;
    USHORT LocalPort;
    BOOLEAN Dirty;
    BOOLEAN PeerClosed; /* the client sent its end */
    BOOLEAN Broken;     /* the connection failed */
    BOOLEAN Closing;    /* closed after the output drains */
    BOOLEAN Closed;     /* the host socket is gone */
    BOOLEAN Raw;        /* an opaque connection */
    PUCHAR In;
    ULONG InLength, InCapacity;
    LIST_ENTRY Out;
    HTTP_REQ *Req;
    ULONG64 LastActive;
    ULONG64 RequestStart;
    ULONG Requests;
};

typedef struct HTTP_WAIT {
    LIST_ENTRY Links;
    IO_USER_REQUEST *R;
    ULONG Kind;
    HTTP_QUEUE *Queue;
    HTTP_REQ *Req;
    HTTP_CONN *Conn;
    ULONG Flags;
    ULONG64 Id;
    PVOID Buffer;
    ULONG Length;
    BOOLEAN Wow;
    ULONG64 Total; /* bytes a send reports */
} HTTP_WAIT;

enum { HTTP_WAIT_REQUEST = 1, HTTP_WAIT_BODY, HTTP_WAIT_SEND, HTTP_WAIT_DISCONNECT, HTTP_WAIT_DEMAND };

/* the locking and the lists, http.c. */
extern PDEVICE_OBJECT HttpDeviceChannel, HttpDeviceQueue;
void HttpLock(void);
void HttpUnlock(void);
extern LIST_ENTRY HttpUrls, HttpQueues, HttpSessions, HttpConns, HttpEndpoints, HttpWaits;
ULONG64 HttpNewId(ULONG kind);
HTTP_WAIT *HttpNewWait(IO_USER_REQUEST *r, ULONG kind);
void HttpFinishWait(HTTP_WAIT *w, NTSTATUS status, ULONG_PTR information);
BOOLEAN HttpIsWow(void);

/* httpurl.c */
NTSTATUS HttpParseRegistration(PCWSTR text, ULONG chars, HTTP_URLREG **out);
void HttpFreeUrl(HTTP_URLREG *url);
HTTP_URLREG *HttpMatchUrl(const HTTP_REQ *req, const MimeNetAddr *local, USHORT port);
BOOLEAN HttpSameUrl(const HTTP_URLREG *a, const HTTP_URLREG *b);
NTSTATUS HttpCookUrl(HTTP_REQ *req, const MimeNetAddr *local, USHORT port, PCSTR host, ULONG host_length);
ULONG HttpUtf8ToWide(PCSTR in, ULONG length, PWSTR out, ULONG room, BOOLEAN decode_percent);

/* httpnet.c */
void HttpNetInitialize(void);
BOOLEAN HttpHostEvent(ULONG sock);
NTSTATUS HttpEndpointAdd(USHORT port);
void HttpEndpointRelease(USHORT port);
void HttpConnWake(HTTP_CONN *c);
void HttpConnClose(HTTP_CONN *c);
void HttpReqFree(HTTP_REQ *req);
void HttpDispatchPending(HTTP_QUEUE *queue);
void HttpQueueDetach(HTTP_QUEUE *queue, NTSTATUS status);
void HttpQueueUnbind(HTTP_QUEUE *queue);
NTSTATUS HttpReceiveRequest(IO_USER_REQUEST *r, HTTP_OPEN *o, ULONG64 id, ULONG flags, PVOID buffer, ULONG length,
                            ULONG_PTR *information);
NTSTATUS HttpReceiveBody(IO_USER_REQUEST *r, HTTP_OPEN *o, ULONG64 id, ULONG flags, PVOID buffer, ULONG length,
                         ULONG_PTR *information);
NTSTATUS HttpSendResponse(IO_USER_REQUEST *r, HTTP_OPEN *o, const UCHAR *input, ULONG in_len, BOOLEAN with_response,
                          ULONG_PTR *information);
NTSTATUS HttpWaitDisconnect(IO_USER_REQUEST *r, HTTP_OPEN *o, ULONG64 id);
NTSTATUS HttpCancelRequest(HTTP_OPEN *o, ULONG64 id);
void HttpFailQueueWaits(HTTP_QUEUE *queue, NTSTATUS status);
void HttpServiceAll(void);
void HttpAbortConnection(HTTP_CONN *c);
void HttpUrlRemoved(HTTP_URLREG *url);
HTTP_REQ *HttpFindRequest(ULONG64 id);
HTTP_CONN *HttpFindConn(ULONG64 id);

#endif
