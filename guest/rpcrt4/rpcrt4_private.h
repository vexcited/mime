/*
 * rpcrt4 internal interface shared by its source files.
 */
#ifndef MIME_RPCRT4_PRIVATE_H
#define MIME_RPCRT4_PRIVATE_H

#define RPCRTAPI DECLSPEC_EXPORT
#include <mime/win32/rpc.h>
#include <rtl.h>

#define GetExceptionCode() __exception_code()

/* ------------------------------------------------------------------ binding handles */

#define BINDING_MAGIC 0x89ABCDEFu

typedef enum {
    BINDING_CLIENT,
    BINDING_SERVER_CALL,
} BINDING_KIND;

struct SCALL;

/*
 * a binding handle: what a client names a server with, or what a server holds for the call it is serving. The strings are
 * the parts of the string binding it was made from.
 */
typedef struct BINDING {
    struct BINDING *Next;
    ULONG Magic;
    BINDING_KIND Kind;
    UUID Object;
    PWSTR Protseq, Address, Endpoint, Options;
    BOOL HasAuth;
    ULONG AuthnLevel, AuthnService, AuthzService;
    PWSTR Principal;
    ULONG ImpersonationType;
    ULONG_PTR OptionValue[31];
    ULONG ComTimeout;
    ULONG CallTimeout;
    /* the call a server binding stands for. */
    struct SCALL *Call;
} BINDING;

BINDING *RpcpBindingNew(BINDING_KIND kind);
BINDING *RpcpBindingLookup(RPC_BINDING_HANDLE handle);
void RpcpBindingRelease(BINDING *b);
void RpcpBindingFree(BINDING *b);
BINDING *RpcpBindingCopy(const BINDING *b);
PWSTR RpcpCopyString(PCWSTR s);
void RpcpFreeString(PWSTR s);
PWSTR RpcpWiden(const unsigned char *s);
RPC_CSTR RpcpNarrow(PWSTR w);
BOOL RpcpIsNil(const UUID *u);

/* ------------------------------------------------------------------ the protocol */

#define PDU_REQUEST 0
#define PDU_PING 1
#define PDU_RESPONSE 2
#define PDU_FAULT 3
#define PDU_WORKING 4
#define PDU_NOCALL 5
#define PDU_REJECT 6
#define PDU_BIND 11
#define PDU_BIND_ACK 12
#define PDU_BIND_NAK 13
#define PDU_ALTER_CONTEXT 14
#define PDU_ALTER_CONTEXT_RESP 15
#define PDU_AUTH3 16
#define PDU_SHUTDOWN 17
#define PDU_CO_CANCEL 18
#define PDU_ORPHANED 19

#define PFC_FIRST_FRAG 0x01
#define PFC_LAST_FRAG 0x02
#define PFC_PENDING_CANCEL 0x04
#define PFC_CONC_MPX 0x10
#define PFC_DID_NOT_EXECUTE 0x20
#define PFC_MAYBE 0x40
#define PFC_OBJECT_UUID 0x80

#pragma pack(push, 1)
typedef struct {
    UCHAR Version;
    UCHAR Minor;
    UCHAR Type;
    UCHAR Flags;
    UCHAR Drep[4];
    USHORT FragLength;
    USHORT AuthLength;
    ULONG CallId;
} PDU_HEADER;

typedef struct {
    PDU_HEADER Header;
    ULONG AllocHint;
    USHORT ContextId;
    USHORT Opnum;
} PDU_REQUEST_HEADER;

typedef struct {
    PDU_HEADER Header;
    ULONG AllocHint;
    USHORT ContextId;
    UCHAR CancelCount;
    UCHAR Reserved;
} PDU_RESPONSE_HEADER;

typedef struct {
    PDU_RESPONSE_HEADER Response;
    ULONG Status;
    ULONG Reserved2;
} PDU_FAULT_PDU;

typedef struct {
    GUID Uuid;
    USHORT Major;
    USHORT Minor;
} PDU_SYNTAX;

typedef struct {
    UCHAR AuthType;
    UCHAR AuthLevel;
    UCHAR PadLength;
    UCHAR Reserved;
    ULONG ContextId;
} PDU_SEC_TRAILER;
#pragma pack(pop)

/* the room before the stub data of a buffer, for the header of a request or response and the stub data's alignment. */
#define PDU_ROOM 64
#define RPC_MAX_FRAGMENT 0xFFF8u
#define RPC_PDU_VERSION 5

extern const RPC_SYNTAX_IDENTIFIER g_ndr20_syntax;
extern const RPC_SYNTAX_IDENTIFIER g_ndr64_syntax;

/* ------------------------------------------------------------------ the transport */

typedef enum {
    TRANSPORT_PIPE,
} TRANSPORT_KIND;

typedef struct RPC_STREAM {
    TRANSPORT_KIND Kind;
    HANDLE Handle;
    HANDLE Event;
    BOOL Broken;
    ULONG Timeout;
} RPC_STREAM;

/* appends text and numbers to a wide string of count characters. */
void RpcpAppend(WCHAR *dst, SIZE_T count, const WCHAR *text);
void RpcpAppendNumber(WCHAR *dst, SIZE_T count, ULONG number);

/* functions of advapi32, which rpcrt4 does not link against. */
typedef struct {
    BOOL(WINAPI *ImpersonateNamedPipeClient)(HANDLE pipe);
    BOOL(WINAPI *RevertToSelf)(void);
    BOOL(WINAPI *OpenThreadToken)(HANDLE thread, DWORD access, BOOL self, PHANDLE token);
    BOOL(WINAPI *GetTokenInformation)(HANDLE token, ULONG klass, PVOID info, DWORD length, PDWORD returned);
    BOOL(WINAPI *LookupAccountSidW)(LPCWSTR system, PVOID sid, LPWSTR name, LPDWORD name_size, LPWSTR domain,
                                    LPDWORD domain_size, PDWORD use);
} RPC_ADVAPI;
const RPC_ADVAPI *RpcpAdvapi(void);
void RpcpCopy(WCHAR *dst, SIZE_T count, const WCHAR *text);

RPC_STATUS RpcpStreamConnect(const BINDING *b, RPC_STREAM *stream);
RPC_STATUS RpcpStreamAdopt(RPC_STREAM *stream, HANDLE pipe);
PSECURITY_DESCRIPTOR RpcpEveryoneDescriptor(ULONG mask);
void RpcpStreamClose(RPC_STREAM *stream);
RPC_STATUS RpcpStreamSend(RPC_STREAM *stream, const void *data, ULONG length);
RPC_STATUS RpcpStreamReceive(RPC_STREAM *stream, void *buffer, ULONG capacity, ULONG *received);
/* the name of the pipe that an endpoint of a protocol sequence is. */
RPC_STATUS RpcpPipeName(PCWSTR protseq, PCWSTR endpoint, PWSTR name, SIZE_T count);

/* ------------------------------------------------------------------ the thread */

typedef struct RPC_THREAD {
    struct RPC_THREAD *Next;
    DWORD ThreadId;
    /* the call this thread is serving, or making. */
    struct SCALL *Call;
    void *StubMessage;
    /* an event the calls of the thread wait on besides their reply, for cancellation. */
    HANDLE CancelEvent;
    LONG Cancelled;
    unsigned char *Scratch;
    /* how the thread waits for a reply, when its owner has to do more than wait. */
    DWORD(WINAPI *WaitFn)(DWORD count, const HANDLE *handles, DWORD timeout, void *context);
    void *WaitContext;
} RPC_THREAD;

RPC_THREAD *RpcpThread(void);
/* the key under which the connections for a binding are pooled, and the pool's release of those of a key. */
void RpcpBindingKey(const BINDING *b, WCHAR *key, SIZE_T count);
void RpcpPoolDrop(const WCHAR *key);
void RpcpThreadDetach(void);
RPC_STATUS RpcpThreadCancel(DWORD thread_id);

/* buffers with room before them for the header of a pdu. */
unsigned char *RpcpBufferAlloc(SIZE_T size);
void RpcpBufferFree(void *buffer);

/* ------------------------------------------------------------------ the client */

RPC_STATUS RpcpClientBinding(RPC_MESSAGE *message, BINDING **binding);

/* ------------------------------------------------------------------ the server */

struct SCALL *RpcpCurrentCall(void);
BINDING *RpcpCurrentCallBinding(void);

/* ------------------------------------------------------------------ the endpoint mapper */

RPC_STATUS RpcpEpResolve(BINDING *b, const RPC_SYNTAX_IDENTIFIER *interface_id);
void RpcpEpPurge(DWORD pid);

#endif
