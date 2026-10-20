/*
 * the client of rpc: connections to servers, the bind that opens an interface on one, and the buffers of calls. A stub asks
 * for a buffer of the size of its arguments, fills it, and sends it; the reply replaces it.
 *
 * Connections are kept for the next call: a call takes an idle one that was made for the same server and security, or makes
 * one, and gives it back when the reply is read. The interface a connection carries is opened once, with a bind, and named
 * by a short number afterwards.
 */
#include "rpcrt4_private.h"

const RPC_SYNTAX_IDENTIFIER g_ndr20_syntax = {{0x8A885D04, 0x1CEB, 0x11C9, {0x9F, 0xE8, 0x08, 0x00, 0x2B, 0x10, 0x48, 0x60}},
                                              {2, 0}};
const RPC_SYNTAX_IDENTIFIER g_ndr64_syntax = {{0x71710533, 0xBEBA, 0x4937, {0x83, 0x19, 0xB5, 0xDB, 0xEF, 0x9C, 0xCC, 0x36}},
                                              {1, 0}};

#define MAX_CONTEXTS 64
#define MAX_IDLE_PER_KEY 8
#define IDLE_LIFETIME_MS 30000

typedef struct {
    RPC_SYNTAX_IDENTIFIER Abstract;
    RPC_SYNTAX_IDENTIFIER Transfer;
    USHORT Id;
} CONTEXT_ENTRY;

typedef struct RPC_CONN {
    struct RPC_CONN *Next;
    RPC_STREAM Stream;
    WCHAR Key[300];
    ULONG CallId;
    USHORT MaxXmit;
    USHORT MaxRecv;
    ULONG Tick;
    ULONG Contexts;
    CONTEXT_ENTRY Context[MAX_CONTEXTS];
    UCHAR AuthType;
    UCHAR AuthLevel;
    unsigned char *Receive;
} RPC_CONN;

/* the state of a call between its buffer and its reply. */
typedef struct CCALL {
    RPC_CONN *Conn;
    USHORT ContextId;
    BOOL Ndr64;
    /* the object the call is made to, which the call has chosen when it asked for its buffer. */
    UUID Object;
} CCALL;

static SRWLOCK g_pool_lock = SRWLOCK_INIT;
static RPC_CONN *g_idle;

/* ------------------------------------------------------------------ buffers */

/* a buffer of size bytes with room before it for the header of a pdu, aligned for the engine. */
unsigned char *RpcpBufferAlloc(SIZE_T size)
{
    unsigned char *raw = HeapAlloc(GetProcessHeap(), 0, size + PDU_ROOM + 32);
    if (!raw)
        return nullptr;
    unsigned char *aligned = (unsigned char *)(((ULONG_PTR)raw + 16) & ~(ULONG_PTR)15);
    ((unsigned char **)aligned)[-1] = raw;
    return aligned + PDU_ROOM;
}

void RpcpBufferFree(void *buffer)
{
    if (!buffer)
        return;
    unsigned char *aligned = (unsigned char *)buffer - PDU_ROOM;
    HeapFree(GetProcessHeap(), 0, ((unsigned char **)aligned)[-1]);
}

/* ------------------------------------------------------------------ the thread */

static DWORD g_thread_tls = TLS_OUT_OF_INDEXES;
static SRWLOCK g_thread_lock = SRWLOCK_INIT;
static RPC_THREAD *g_threads;

RPC_THREAD *RpcpThread(void)
{
    if (g_thread_tls == TLS_OUT_OF_INDEXES) {
        DWORD slot = TlsAlloc();
        if (InterlockedCompareExchange((volatile LONG *)&g_thread_tls, (LONG)slot, (LONG)TLS_OUT_OF_INDEXES) !=
            (LONG)TLS_OUT_OF_INDEXES)
            TlsFree(slot);
    }
    RPC_THREAD *t = TlsGetValue(g_thread_tls);
    if (t)
        return t;
    t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (!t)
        return nullptr;
    t->ThreadId = GetCurrentThreadId();
    t->CancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    TlsSetValue(g_thread_tls, t);
    AcquireSRWLockExclusive(&g_thread_lock);
    t->Next = g_threads;
    g_threads = t;
    ReleaseSRWLockExclusive(&g_thread_lock);
    return t;
}

/* a thread that ends takes its record with it. */
void RpcpThreadDetach(void)
{
    if (g_thread_tls == TLS_OUT_OF_INDEXES)
        return;
    RPC_THREAD *t = TlsGetValue(g_thread_tls);
    if (!t)
        return;
    AcquireSRWLockExclusive(&g_thread_lock);
    RPC_THREAD **at = &g_threads;
    while (*at && *at != t)
        at = &(*at)->Next;
    if (*at)
        *at = t->Next;
    ReleaseSRWLockExclusive(&g_thread_lock);
    if (t->CancelEvent)
        CloseHandle(t->CancelEvent);
    HeapFree(GetProcessHeap(), 0, t->Scratch);
    HeapFree(GetProcessHeap(), 0, t);
    TlsSetValue(g_thread_tls, nullptr);
}

/* asks the call a thread is making to stop. */
RPC_STATUS RpcpThreadCancel(DWORD thread_id)
{
    RPC_STATUS status = RPC_S_INVALID_ARG;
    AcquireSRWLockShared(&g_thread_lock);
    for (RPC_THREAD *t = g_threads; t; t = t->Next)
        if (t->ThreadId == thread_id) {
            InterlockedExchange(&t->Cancelled, 1);
            SetEvent(t->CancelEvent);
            status = RPC_S_OK;
            break;
        }
    ReleaseSRWLockShared(&g_thread_lock);
    return status;
}

/* ------------------------------------------------------------------ connections */

void RpcpBindingKey(const BINDING *b, WCHAR *key, SIZE_T count)
{
    key[0] = 0;
    RpcpAppend(key, count, b->Protseq ? b->Protseq : L"");
    RpcpAppend(key, count, L"|");
    RpcpAppend(key, count, b->Address ? b->Address : L"");
    RpcpAppend(key, count, L"|");
    RpcpAppend(key, count, b->Endpoint ? b->Endpoint : L"");
    RpcpAppend(key, count, L"|");
    RpcpAppendNumber(key, count, b->HasAuth ? b->AuthnLevel : 0);
    RpcpAppend(key, count, L"|");
    RpcpAppendNumber(key, count, b->HasAuth ? b->AuthnService : 0);
    RpcpAppend(key, count, L"|");
    RpcpAppendNumber(key, count, b->ImpersonationType);
    RpcpAppend(key, count, L"|");
    RpcpAppend(key, count, b->Principal ? b->Principal : L"");
    for (WCHAR *p = key; *p; p++)
        if (*p >= L'A' && *p <= L'Z')
            *p += L'a' - L'A';
}

static void conn_destroy(RPC_CONN *c)
{
    RpcpStreamClose(&c->Stream);
    HeapFree(GetProcessHeap(), 0, c->Receive);
    HeapFree(GetProcessHeap(), 0, c);
}

/* closes the connections that were idle too long. */
static void pool_expire(ULONG now)
{
    RPC_CONN **at = &g_idle;
    while (*at) {
        RPC_CONN *c = *at;
        if (now - c->Tick > IDLE_LIFETIME_MS || c->Stream.Broken) {
            *at = c->Next;
            conn_destroy(c);
        } else {
            at = &c->Next;
        }
    }
}

static RPC_CONN *pool_take(const WCHAR *key)
{
    ULONG now = GetTickCount();
    RPC_CONN *found = nullptr;
    AcquireSRWLockExclusive(&g_pool_lock);
    pool_expire(now);
    for (RPC_CONN **at = &g_idle; *at; at = &(*at)->Next)
        if (!wcscmp((*at)->Key, key)) {
            found = *at;
            *at = found->Next;
            break;
        }
    ReleaseSRWLockExclusive(&g_pool_lock);
    return found;
}

static void pool_give(RPC_CONN *c)
{
    if (c->Stream.Broken) {
        conn_destroy(c);
        return;
    }
    c->Tick = GetTickCount();
    AcquireSRWLockExclusive(&g_pool_lock);
    ULONG same = 0;
    for (RPC_CONN *i = g_idle; i; i = i->Next)
        if (!wcscmp(i->Key, c->Key))
            same++;
    if (same < MAX_IDLE_PER_KEY) {
        c->Next = g_idle;
        g_idle = c;
        c = nullptr;
    }
    ReleaseSRWLockExclusive(&g_pool_lock);
    if (c)
        conn_destroy(c);
}

/*
 * the last binding handle to a server is freed: the connections kept for it are closed, and a server that holds contexts for
 * the client rundowns them, as the end of the association does on windows.
 */
void RpcpPoolDrop(const WCHAR *key)
{
    AcquireSRWLockExclusive(&g_pool_lock);
    RPC_CONN **at = &g_idle;
    while (*at) {
        RPC_CONN *c = *at;
        if (!wcscmp(c->Key, key)) {
            *at = c->Next;
            conn_destroy(c);
        } else {
            at = &c->Next;
        }
    }
    ReleaseSRWLockExclusive(&g_pool_lock);
}

/* ------------------------------------------------------------------ bind */

static void pdu_syntax(PDU_SYNTAX *out, const RPC_SYNTAX_IDENTIFIER *s)
{
    out->Uuid = s->SyntaxGUID;
    out->Major = s->SyntaxVersion.MajorVersion;
    out->Minor = s->SyntaxVersion.MinorVersion;
}

/*
 * opens an interface on a connection: the first interface with a bind, the others by altering the context. The transfer
 * syntaxes offered are the interface's own. Returns the number the interface is called by.
 */
static RPC_STATUS conn_bind(RPC_CONN *c, const BINDING *b, const RPC_SYNTAX_IDENTIFIER *abstract,
                            const RPC_SYNTAX_IDENTIFIER *offered, ULONG offered_count, USHORT *context_id,
                            RPC_SYNTAX_IDENTIFIER *chosen)
{
    for (ULONG i = 0; i < c->Contexts; i++)
        if (!memcmp(&c->Context[i].Abstract, abstract, sizeof(*abstract))) {
            *context_id = c->Context[i].Id;
            *chosen = c->Context[i].Transfer;
            return RPC_S_OK;
        }
    if (c->Contexts == MAX_CONTEXTS)
        return RPC_S_OUT_OF_RESOURCES;

    unsigned char pdu[512];
    memset(pdu, 0, sizeof(pdu));
    PDU_HEADER *h = (PDU_HEADER *)pdu;
    h->Version = RPC_PDU_VERSION;
    h->Type = c->Contexts ? PDU_ALTER_CONTEXT : PDU_BIND;
    h->Flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
    h->Drep[0] = 0x10;
    h->CallId = ++c->CallId;
    unsigned char *p = pdu + sizeof(PDU_HEADER);
    *(USHORT *)p = c->MaxXmit;
    *(USHORT *)(p + 2) = c->MaxRecv;
    *(ULONG *)(p + 4) = 0;
    p += 8;
    p[0] = 1;
    p += 4;
    USHORT id = (USHORT)c->Contexts;
    *(USHORT *)p = id;
    p[2] = (unsigned char)offered_count;
    p += 4;
    pdu_syntax((PDU_SYNTAX *)p, abstract);
    p += sizeof(PDU_SYNTAX);
    for (ULONG i = 0; i < offered_count; i++) {
        pdu_syntax((PDU_SYNTAX *)p, &offered[i]);
        p += sizeof(PDU_SYNTAX);
    }
    USHORT auth_length = 0;
    if (b->HasAuth && b->AuthnLevel > RPC_C_AUTHN_LEVEL_NONE) {
        /* the level and the service travel in a trailer, with no token. */
        while ((p - pdu) & 3)
            *p++ = 0;
        PDU_SEC_TRAILER *t = (PDU_SEC_TRAILER *)p;
        t->AuthType = (UCHAR)(b->AuthnService == 0xFFFFFFFFu ? RPC_C_AUTHN_WINNT : b->AuthnService);
        t->AuthLevel = (UCHAR)b->AuthnLevel;
        t->ContextId = 0;
        p += sizeof(*t);
        memset(p, 0, 8);
        p += 8;
        auth_length = 8;
        c->AuthType = t->AuthType;
        c->AuthLevel = t->AuthLevel;
    }
    h->AuthLength = auth_length;
    h->FragLength = (USHORT)(p - pdu);
    RPC_STATUS status = RpcpStreamSend(&c->Stream, pdu, h->FragLength);
    if (status)
        return status;

    ULONG got;
    unsigned char *reply = c->Receive;
    status = RpcpStreamReceive(&c->Stream, reply, RPC_MAX_FRAGMENT + 64, &got);
    if (status)
        return status;
    h = (PDU_HEADER *)reply;
    if (got < sizeof(PDU_HEADER) || h->Version != RPC_PDU_VERSION || h->FragLength != got)
        return RPC_S_PROTOCOL_ERROR;
    if (h->Type == PDU_BIND_NAK)
        return RPC_S_PROTOCOL_ERROR;
    if (h->Type != PDU_BIND_ACK && h->Type != PDU_ALTER_CONTEXT_RESP)
        return RPC_S_PROTOCOL_ERROR;
    unsigned char *r = reply + sizeof(PDU_HEADER);
    if (!c->Contexts) {
        c->MaxXmit = min(c->MaxXmit, *(USHORT *)r);
        c->MaxRecv = min(c->MaxRecv, *(USHORT *)(r + 2));
    }
    r += 8;
    /* the secondary address, then padding to four bytes from the pdu's start. */
    USHORT sec_len = *(USHORT *)r;
    r += 2 + sec_len;
    while ((r - reply) & 3)
        r++;
    if (r + 4 + 24 > reply + got)
        return RPC_S_PROTOCOL_ERROR;
    r += 4;
    USHORT result = *(USHORT *)r;
    USHORT reason = *(USHORT *)(r + 2);
    if (result != 0)
        return reason == 1 ? RPC_S_UNKNOWN_IF : RPC_S_UNSUPPORTED_TRANS_SYN;
    PDU_SYNTAX *accepted = (PDU_SYNTAX *)(r + 4);
    RPC_SYNTAX_IDENTIFIER transfer = {accepted->Uuid, {accepted->Major, accepted->Minor}};
    c->Context[c->Contexts].Abstract = *abstract;
    c->Context[c->Contexts].Transfer = transfer;
    c->Context[c->Contexts].Id = id;
    c->Contexts++;
    *context_id = id;
    *chosen = transfer;
    return RPC_S_OK;
}

static RPC_STATUS conn_new(const BINDING *b, const WCHAR *key, RPC_CONN **out)
{
    RPC_CONN *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        return RPC_S_OUT_OF_MEMORY;
    c->Receive = HeapAlloc(GetProcessHeap(), 0, RPC_MAX_FRAGMENT + 64);
    if (!c->Receive) {
        HeapFree(GetProcessHeap(), 0, c);
        return RPC_S_OUT_OF_MEMORY;
    }
    RPC_STATUS status = RpcpStreamConnect(b, &c->Stream);
    if (status) {
        HeapFree(GetProcessHeap(), 0, c->Receive);
        HeapFree(GetProcessHeap(), 0, c);
        return status;
    }
    wcscpy(c->Key, key);
    c->MaxXmit = c->MaxRecv = RPC_MAX_FRAGMENT;
    *out = c;
    return RPC_S_OK;
}

/* ------------------------------------------------------------------ calls */

/* the binding a message names, for a client call. */
RPC_STATUS RpcpClientBinding(RPC_MESSAGE *message, BINDING **binding)
{
    BINDING *b = RpcpBindingLookup(message->Handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    *binding = b;
    return RPC_S_OK;
}

RPC_STATUS RpcpServerGetBuffer(RPC_MESSAGE *message, BINDING *b);
void RpcpServerFreeBuffer(RPC_MESSAGE *message);

/*
 * the connection a call goes over and the number of its interface on it, made if the call is the first for them. The server's
 * endpoint is asked of the endpoint mapper if the binding names none.
 */
static RPC_STATUS call_connect(BINDING *b, const RPC_CLIENT_INTERFACE *iface, RPC_MESSAGE *message, CCALL *call)
{
    RPC_STATUS status;
    if (!b->Endpoint || !*b->Endpoint) {
        status = RpcpEpResolve(b, &iface->InterfaceId);
        if (status)
            return status;
    }
    WCHAR key[300];
    RpcpBindingKey(b, key, ARRAYSIZE(key));
    RPC_SYNTAX_IDENTIFIER offered[2];
    ULONG offered_count = 0;
    offered[offered_count++] = iface->TransferSyntax.SyntaxGUID.Data1 || iface->TransferSyntax.SyntaxVersion.MajorVersion
                                   ? iface->TransferSyntax
                                   : g_ndr20_syntax;
    for (int attempt = 0; attempt < 2; attempt++) {
        BOOL reused = FALSE;
        RPC_CONN *c = pool_take(key);
        if (c)
            reused = TRUE;
        else {
            status = conn_new(b, key, &c);
            if (status)
                return status;
        }
        RPC_SYNTAX_IDENTIFIER chosen;
        USHORT id;
        status = conn_bind(c, b, &iface->InterfaceId, offered, offered_count, &id, &chosen);
        if (status) {
            conn_destroy(c);
            /* a connection the server closed while it was idle: try again with a new one. */
            if (reused && (status == RPC_S_CALL_FAILED || status == RPC_S_COMM_FAILURE))
                continue;
            return status;
        }
        call->Conn = c;
        call->ContextId = id;
        call->Ndr64 = !memcmp(&chosen, &g_ndr64_syntax, sizeof(chosen));
        message->TransferSyntax = (PRPC_SYNTAX_IDENTIFIER)(call->Ndr64 ? &g_ndr64_syntax : &g_ndr20_syntax);
        return RPC_S_OK;
    }
    return RPC_S_SERVER_UNAVAILABLE;
}

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcGetBufferWithObject(RPC_MESSAGE *message, UUID *object)
{
    BINDING *b = RpcpBindingLookup(message->Handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    if (b->Kind == BINDING_SERVER_CALL)
        return RpcpServerGetBuffer(message, b);
    const RPC_CLIENT_INTERFACE *iface = message->RpcInterfaceInformation;
    if (!iface)
        return RPC_S_INVALID_ARG;
    CCALL *call = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*call));
    if (!call)
        return RPC_S_OUT_OF_MEMORY;
    RPC_STATUS status = call_connect(b, iface, message, call);
    if (status) {
        HeapFree(GetProcessHeap(), 0, call);
        return status;
    }
    unsigned char *buffer = RpcpBufferAlloc(message->BufferLength);
    if (!buffer) {
        pool_give(call->Conn);
        HeapFree(GetProcessHeap(), 0, call);
        return RPC_S_OUT_OF_MEMORY;
    }
    message->Buffer = buffer;
    message->ReservedForRuntime = call;
    call->Object = object ? *object : b->Object;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcGetBuffer(RPC_MESSAGE *message)
{
    BINDING *b = RpcpBindingLookup(message->Handle);
    if (!b)
        return RPC_S_INVALID_BINDING;
    return I_RpcGetBufferWithObject(message, &b->Object);
}

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcNegotiateTransferSyntax(RPC_MESSAGE *message)
{
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcFreeBuffer(RPC_MESSAGE *message)
{
    BINDING *b = RpcpBindingLookup(message->Handle);
    if (b && b->Kind == BINDING_SERVER_CALL) {
        RpcpServerFreeBuffer(message);
        return RPC_S_OK;
    }
    CCALL *call = message->ReservedForRuntime;
    RpcpBufferFree(message->Buffer);
    message->Buffer = nullptr;
    if (call) {
        /* a call that never sent: its connection goes back. */
        if (call->Conn)
            pool_give(call->Conn);
        HeapFree(GetProcessHeap(), 0, call);
        message->ReservedForRuntime = nullptr;
    }
    return RPC_S_OK;
}

/* sends the stub data of a request in as many fragments as the connection takes. */
static RPC_STATUS send_request(RPC_CONN *c, RPC_MESSAGE *m, CCALL *call, const UUID *object, ULONG call_id)
{
    ULONG header = sizeof(PDU_REQUEST_HEADER) + (object ? sizeof(UUID) : 0);
    ULONG per = (c->MaxXmit - header) & ~7u;
    ULONG total = m->BufferLength;
    unsigned char *data = m->Buffer;

    if (total <= per) {
        unsigned char *pdu = data - header;
        PDU_REQUEST_HEADER *h = (PDU_REQUEST_HEADER *)pdu;
        h->Header.Version = RPC_PDU_VERSION;
        h->Header.Minor = 0;
        h->Header.Type = PDU_REQUEST;
        h->Header.Flags = PFC_FIRST_FRAG | PFC_LAST_FRAG | (object ? PFC_OBJECT_UUID : 0);
        if (m->RpcFlags & RPC_NCA_FLAGS_MAYBE)
            h->Header.Flags |= PFC_MAYBE;
        h->Header.Drep[0] = 0x10;
        h->Header.Drep[1] = h->Header.Drep[2] = h->Header.Drep[3] = 0;
        h->Header.FragLength = (USHORT)(header + total);
        h->Header.AuthLength = 0;
        h->Header.CallId = call_id;
        h->AllocHint = total;
        h->ContextId = call->ContextId;
        h->Opnum = (USHORT)m->ProcNum;
        if (object)
            *(UUID *)(pdu + sizeof(PDU_REQUEST_HEADER)) = *object;
        return RpcpStreamSend(&c->Stream, pdu, header + total);
    }

    unsigned char *frag = HeapAlloc(GetProcessHeap(), 0, header + per);
    if (!frag)
        return RPC_S_OUT_OF_MEMORY;
    RPC_STATUS status = RPC_S_OK;
    ULONG sent = 0;
    while (sent < total) {
        ULONG n = min(per, total - sent);
        PDU_REQUEST_HEADER *h = (PDU_REQUEST_HEADER *)frag;
        h->Header.Version = RPC_PDU_VERSION;
        h->Header.Minor = 0;
        h->Header.Type = PDU_REQUEST;
        h->Header.Flags = (sent == 0 ? PFC_FIRST_FRAG : 0) | (sent + n == total ? PFC_LAST_FRAG : 0) |
                          (object ? PFC_OBJECT_UUID : 0);
        h->Header.Drep[0] = 0x10;
        h->Header.Drep[1] = h->Header.Drep[2] = h->Header.Drep[3] = 0;
        h->Header.FragLength = (USHORT)(header + n);
        h->Header.AuthLength = 0;
        h->Header.CallId = call_id;
        h->AllocHint = total - sent;
        h->ContextId = call->ContextId;
        h->Opnum = (USHORT)m->ProcNum;
        if (object)
            *(UUID *)(frag + sizeof(PDU_REQUEST_HEADER)) = *object;
        memcpy(frag + header, data + sent, n);
        status = RpcpStreamSend(&c->Stream, frag, header + n);
        if (status)
            break;
        sent += n;
    }
    HeapFree(GetProcessHeap(), 0, frag);
    return status;
}

/*
 * reads the answer: the stub data of a response, whole, or the status of a fault. Several fragments are put together.
 */
static RPC_STATUS read_reply(RPC_CONN *c, ULONG call_id, unsigned char **data, ULONG *length)
{
    unsigned char *whole = nullptr;
    ULONG have = 0, capacity = 0;
    for (;;) {
        ULONG got;
        RPC_STATUS status = RpcpStreamReceive(&c->Stream, c->Receive, RPC_MAX_FRAGMENT + 64, &got);
        if (status) {
            RpcpBufferFree(whole);
            return status;
        }
        PDU_HEADER *h = (PDU_HEADER *)c->Receive;
        if (got < sizeof(PDU_HEADER) || h->Version != RPC_PDU_VERSION || h->FragLength != got ||
            h->CallId != call_id) {
            RpcpBufferFree(whole);
            c->Stream.Broken = TRUE;
            return RPC_S_PROTOCOL_ERROR;
        }
        if (h->Type == PDU_FAULT) {
            RpcpBufferFree(whole);
            if (got < sizeof(PDU_FAULT_PDU))
                return RPC_S_PROTOCOL_ERROR;
            return (RPC_STATUS)((PDU_FAULT_PDU *)h)->Status;
        }
        if (h->Type != PDU_RESPONSE || got < sizeof(PDU_RESPONSE_HEADER)) {
            RpcpBufferFree(whole);
            c->Stream.Broken = TRUE;
            return RPC_S_PROTOCOL_ERROR;
        }
        PDU_RESPONSE_HEADER *r = (PDU_RESPONSE_HEADER *)h;
        ULONG n = got - sizeof(*r) - h->AuthLength;
        if (n > got) {
            RpcpBufferFree(whole);
            return RPC_S_PROTOCOL_ERROR;
        }
        if (!whole) {
            capacity = (h->Flags & PFC_LAST_FRAG) ? n : max(n, min(r->AllocHint, 0x1000000u));
            whole = RpcpBufferAlloc(capacity ? capacity : 1);
            if (!whole)
                return RPC_S_OUT_OF_MEMORY;
        } else if (have + n > capacity) {
            capacity = max(capacity * 2, have + n);
            unsigned char *bigger = RpcpBufferAlloc(capacity);
            if (!bigger) {
                RpcpBufferFree(whole);
                return RPC_S_OUT_OF_MEMORY;
            }
            memcpy(bigger, whole, have);
            RpcpBufferFree(whole);
            whole = bigger;
        }
        memcpy(whole + have, (unsigned char *)(r + 1), n);
        have += n;
        if (h->Flags & PFC_LAST_FRAG)
            break;
    }
    *data = whole;
    *length = have;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcSendReceive(RPC_MESSAGE *message)
{
    BINDING *b = RpcpBindingLookup(message->Handle);
    CCALL *call = message->ReservedForRuntime;
    if (!b || !call)
        return RPC_S_INVALID_BINDING;
    RPC_CONN *c = call->Conn;
    ULONG id = ++c->CallId;
    c->Stream.Timeout = b->CallTimeout ? b->CallTimeout : 0;

    RPC_STATUS status = send_request(c, message, call, RpcpIsNil(&call->Object) ? nullptr : &call->Object, id);
    unsigned char *reply = nullptr;
    ULONG length = 0;
    if (!status)
        status = read_reply(c, id, &reply, &length);
    RpcpBufferFree(message->Buffer);
    message->Buffer = nullptr;
    call->Conn = nullptr;
    pool_give(c);
    HeapFree(GetProcessHeap(), 0, call);
    message->ReservedForRuntime = nullptr;
    if (status)
        return status;
    message->Buffer = reply;
    message->BufferLength = length;
    /* the reply is a buffer without a call: the stub frees it with I_RpcFreeBuffer. */
    return RPC_S_OK;
}

/*
 * Mime's own: the function a thread waits for the replies of its calls with. A thread that has windows has to go on
 * serving them while it waits, and the function does that. It is called as WaitForMultipleObjects would be, without the
 * wait for all.
 */
RPCRTAPI void RPC_ENTRY I_RpcMimeSetWaitFunction(DWORD(WINAPI *function)(DWORD, const HANDLE *, DWORD, void *), void *context,
                                                 void *previous[2])
{
    RPC_THREAD *t = RpcpThread();
    if (!t)
        return;
    if (previous) {
        previous[0] = (void *)t->WaitFn;
        previous[1] = t->WaitContext;
    }
    t->WaitFn = function;
    t->WaitContext = context;
}
