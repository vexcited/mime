/*
 * advanced local procedure calls, the transport of ncalrpc: connection ports with a name, the pair of communication
 * ports a connection makes, and the messages between them with their attributes. what a message carries and how it is
 * told to the receiver was measured on windows 11 26300, the use rpcrt4 makes of it read from its 26300 build.
 *
 * a client sends on its communication port, and the message lands in the queue of the connection port, where the
 * server receives every client's messages with the context it gave the connection. what the server sends on its
 * communication port lands in the queue of the client's port. a message that is not a datagram waits for its reply
 * in a table of messages by id: a synchronous sender sleeps on it, another gets the reply in its port's queue.
 *
 * csr's port is an alpc port the kernel answers itself, see csr.c.
 */
#include "../ntos.h"
#include "../se/se.h"

#define TAG_ALPC KI_TAG('A', 'l', 'p', 'c')
#define PORT_CONNECT 0x0001
#define PORT_ALL_ACCESS 0x1F0001

#define STATUS_PORT_CONNECTION_REFUSED ((NTSTATUS)0xC0000041L)
#define STATUS_PORT_MESSAGE_TOO_LONG ((NTSTATUS)0xC000002FL)
#define STATUS_SERVER_SID_MISMATCH ((NTSTATUS)0xC00002A0L)
/* a reply to a message that waits for none, measured. */
#define STATUS_ALPC_REPLY_MISMATCH ((NTSTATUS)0xC0000702L)
#define STATUS_PORT_CLOSED ((NTSTATUS)0xC0000700L)

#define LPC_REQUEST 1
#define LPC_REPLY 2
#define LPC_DATAGRAM 3
#define LPC_PORT_CLOSED 5
#define LPC_CONNECTION_REQUEST 10
#define LPC_CONNECTION_REPLY 11
#define LPC_CANCELED 12
#define LPC_CONTINUATION_REQUIRED 0x2000

#define ALPC_MSGFLG_SYNC_REQUEST 0x20000
#define ALPC_MSGFLG_WAIT_ALERTABLE 0x200000

/* a connection port tells this flag of its own besides those it was made with. */
#define ALPC_PORFLG_CONNECTION 0x20000

#define ATTR_SECURITY 0x80000000u
#define ATTR_VIEW 0x40000000u
#define ATTR_CONTEXT 0x20000000u
#define ATTR_HANDLE 0x10000000u
#define ATTR_TOKEN 0x08000000u
#define ATTR_DIRECT 0x04000000u
#define ATTR_WORK_ON_BEHALF 0x02000000u

#define ALPC_VIEWFLG_UNMAP_EXISTING 0x10000

POBJECT_TYPE ExAlpcPortType;

typedef struct ALPC_PORT_HEADER {
    USHORT DataLength, TotalLength;
    USHORT Type, DataInfoOffset;
    ULONG64 Process, Thread;
    ULONG MessageId, Pad;
    ULONG64 CallbackId;
} ALPC_PORT_HEADER;
MIME_STATIC_ASSERT(sizeof(ALPC_PORT_HEADER) == 0x28, "PORT_MESSAGE");

typedef struct ALPC_QOS {
    ULONG Length;
    ULONG ImpersonationLevel;
    UCHAR ContextTrackingMode, EffectiveOnly;
    USHORT Pad;
} ALPC_QOS;

typedef struct ALPC_PORT_ATTRIBUTES {
    ULONG Flags;
    ALPC_QOS SecurityQos;
    SIZE_T MaxMessageLength, MemoryBandwidth, MaxPoolUsage, MaxSectionSize, MaxViewSize, MaxTotalSectionSize;
    ULONG DupObjectTypes, Reserved;
} ALPC_PORT_ATTRIBUTES;
MIME_STATIC_ASSERT(sizeof(ALPC_PORT_ATTRIBUTES) == 0x48, "ALPC_PORT_ATTRIBUTES");

typedef struct ALPC_SECTION {
    LIST_ENTRY Links; /* on the port that made it. */
    ULONG Id;
    PSECTION Section;
    SIZE_T Size;
} ALPC_SECTION;

/* a view of a port section in one process, made there or given to it with a message. */
typedef struct ALPC_VIEW {
    LIST_ENTRY Links;
    PEPROCESS Process;
    PSECTION Section;
    PVOID Base;
    SIZE_T Size;
} ALPC_VIEW;

typedef struct ALPC_CONTEXT {
    LIST_ENTRY Links;
    ULONG Id;
    PTOKEN Token;
    ULONG Level;
    BOOLEAN EffectiveOnly, Revoked;
} ALPC_CONTEXT;

typedef struct ALPC_PORT {
    ULONG Kind;
    ULONG State;
    ALPC_PORT_ATTRIBUTES Attributes;
    PEPROCESS Owner;
    struct ALPC_PORT *Connection; /* the connection port of a communication port, referenced. */
    struct ALPC_PORT *Peer;       /* the other end while connected, referenced. */
    PVOID Context;                /* what the receiver of the peer's messages is told: a client is told its handle. */
    ULONG Sequence;               /* the messages the peer delivered so far. */
    ULONG CloseCallbackId;        /* taken when the connection is asked for, for the notice of its end. */
    ULONG64 TokenIds[3];          /* those last told with a message of the peer, which are told again on a change. */
    LIST_ENTRY Queue;
    KEVENT Arrived;
    PVOID CompletionPort, CompletionKey;
    LIST_ENTRY Sections;
    LIST_ENTRY Contexts;
} ALPC_PORT;

enum { ALPC_KIND_CSR = 1, ALPC_KIND_CONNECTION, ALPC_KIND_SERVER, ALPC_KIND_CLIENT };
enum { PORT_OPEN, PORT_CONNECTING, PORT_CONNECTED, PORT_DISCONNECTED };
enum { MSG_QUEUED, MSG_RECEIVED, MSG_DONE };

typedef struct ALPC_MESSAGE {
    LIST_ENTRY Links;
    ULONG Id;
    ULONG CallbackId;
    ULONG State;
    USHORT Type;
    ULONG DataLength;
    PUCHAR Data;
    ULONG64 Process, Thread; /* who sent it. */
    ALPC_PORT *Queue;        /* the queue it sits in or came from. */
    ALPC_PORT *From;         /* the port it was sent on, referenced. */
    ALPC_PORT *To;           /* the communication port of the receiving side, referenced. */
    PVOID PortContext, MessageContext;
    ULONG Sequence;
    PEPROCESS Receiver;      /* the process that received it, for its reply. */
    /* a sender that sleeps for the reply. */
    BOOLEAN Waiting;
    KEVENT Done;
    NTSTATUS Status;
    struct ALPC_MESSAGE *Reply;
    PVOID SenderMessageContext;
    /* what was attached. */
    PTOKEN Token;
    ULONG Level;
    BOOLEAN EffectiveOnly, HasSecurity, TellToken;
    ULONG64 TokenIds[3];
    PSECTION View;
    SIZE_T ViewSize;
    PVOID ReceiverView; /* where the receiver had the view, for a reply that lets it go. */
    PVOID Object;       /* a handle's object. */
    ULONG ObjectType;
    ACCESS_MASK ObjectAccess;
    /* a connection request: the two ends of the connection to be. */
    ALPC_PORT *Client, *Server;
} ALPC_MESSAGE;

static ALPC_MESSAGE **g_messages;
static ULONG g_message_capacity;
static ULONG g_callback_id = 0x10000;
static ULONG g_next_id = 1;

/* ------------------------------------------------------------------ the caller's memory */

static NTSTATUS read_caller(void *dst, const void *src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (!src)
        return STATUS_ACCESS_VIOLATION;
    if (KeGetPreviousMode() == UserMode)
        return KiCopyFromUser(dst, src, length);
    memcpy(dst, src, length);
    return STATUS_SUCCESS;
}

static NTSTATUS write_caller(void *dst, const void *src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (!dst)
        return STATUS_ACCESS_VIOLATION;
    if (KeGetPreviousMode() == UserMode)
        return KiCopyToUser(dst, src, length);
    memcpy(dst, src, length);
    return STATUS_SUCCESS;
}

/*
 * wow64 hands the attributes of a 32-bit caller over as they are and marks the call with this flag. they keep the
 * layout of the 32-bit ntdll, with pointers and handles in four bytes.
 */
#define ALPC_MSGFLG_WOW64_CALL 0x80000000u

/* the size ntdll's AlpcGetHeaderSize gives the attributes before an attribute, from the bits above it. */
static ULONG attributes_size(ULONG bits, BOOLEAN wow)
{
    ULONG n = 8;
    if (bits & ATTR_SECURITY)
        n += wow ? 12 : 0x18;
    if (bits & ATTR_VIEW)
        n += wow ? 16 : 0x20;
    if (bits & ATTR_CONTEXT)
        n += wow ? 20 : 0x20;
    if (bits & ATTR_HANDLE)
        n += wow ? 16 : 0x18;
    if (bits & ATTR_TOKEN)
        n += 0x18;
    if (bits & ATTR_DIRECT)
        n += wow ? 4 : 8;
    if (bits & ATTR_WORK_ON_BEHALF)
        n += 8;
    return n;
}

static ULONG attribute_offset(ULONG allocated, ULONG bit, BOOLEAN wow)
{
    return attributes_size(allocated & ~(bit | (bit - 1)), wow);
}

/* the fields of an attribute: their size in 64 and in 32 bits, 0 for padding, and whether they widen with their sign. */
typedef struct ATTR_FIELD {
    UCHAR Size64, Size32;
    BOOLEAN Signed;
} ATTR_FIELD;

static const ATTR_FIELD g_security_fields[] = {{4, 4}, {4, 0}, {8, 4}, {8, 4, TRUE}, {0}};
static const ATTR_FIELD g_view_fields[] = {{4, 4}, {4, 0}, {8, 4, TRUE}, {8, 4}, {8, 4}, {0}};
static const ATTR_FIELD g_context_fields[] = {{8, 4}, {8, 4}, {4, 4}, {4, 4}, {4, 4}, {4, 0}, {0}};
static const ATTR_FIELD g_handle_fields[] = {{4, 4}, {4, 0}, {8, 4, TRUE}, {4, 4}, {4, 4}, {0}};

static NTSTATUS read_attribute(void *out, SIZE_T size, PVOID attributes, ULONG allocated, ULONG bit, BOOLEAN wow,
                               const ATTR_FIELD *fields)
{
    PUCHAR at = (PUCHAR)attributes + attribute_offset(allocated, bit, wow);
    if (!wow)
        return read_caller(out, at, size);
    UCHAR narrow[0x20];
    ULONG n = 0;
    for (const ATTR_FIELD *f = fields; f->Size64; f++)
        n += f->Size32;
    NTSTATUS status = read_caller(narrow, at, n);
    if (!NT_SUCCESS(status))
        return status;
    PUCHAR wide = out;
    memset(wide, 0, size);
    n = 0;
    for (const ATTR_FIELD *f = fields; f->Size64; wide += f->Size64, f++) {
        if (!f->Size32)
            continue;
        ULONG v;
        memcpy(&v, narrow + n, 4);
        n += 4;
        ULONG64 w = f->Signed ? (ULONG64)(LONG64)(LONG)v : v;
        memcpy(wide, &w, f->Size64);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS write_attribute(PVOID attributes, ULONG allocated, ULONG bit, BOOLEAN wow, const void *in, SIZE_T size,
                                const ATTR_FIELD *fields)
{
    PUCHAR at = (PUCHAR)attributes + attribute_offset(allocated, bit, wow);
    if (!wow)
        return write_caller(at, in, size);
    UCHAR narrow[0x20];
    ULONG n = 0;
    const UCHAR *wide = in;
    for (const ATTR_FIELD *f = fields; f->Size64; wide += f->Size64, f++) {
        if (f->Size32) {
            memcpy(narrow + n, wide, 4);
            n += 4;
        }
    }
    return write_caller(at, narrow, n);
}

/* ------------------------------------------------------------------ messages */

static ALPC_MESSAGE *message_new(void)
{
    ALPC_MESSAGE *m = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*m), TAG_ALPC);
    if (!m)
        return nullptr;
    memset(m, 0, sizeof(*m));
    KeInitializeEvent(&m->Done, NotificationEvent, FALSE);
    m->CallbackId = g_callback_id++;
    for (ULONG i = 0; i < g_message_capacity; i++) {
        ULONG slot = (g_next_id + i) % g_message_capacity;
        if (slot && !g_messages[slot]) {
            g_messages[slot] = m;
            m->Id = slot * 4;
            g_next_id = slot + 1;
            return m;
        }
    }
    ULONG capacity = g_message_capacity ? g_message_capacity * 2 : 256;
    ALPC_MESSAGE **table = ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity * sizeof(*table), TAG_ALPC);
    if (!table) {
        ExFreePool(m);
        return nullptr;
    }
    memset(table, 0, capacity * sizeof(*table));
    if (g_messages) {
        memcpy(table, g_messages, g_message_capacity * sizeof(*table));
        ExFreePool(g_messages);
    }
    ULONG slot = g_message_capacity ? g_message_capacity : 1;
    g_messages = table;
    g_message_capacity = capacity;
    g_messages[slot] = m;
    m->Id = slot * 4;
    g_next_id = slot + 1;
    return m;
}

static ALPC_MESSAGE *message_find(ULONG id)
{
    ULONG slot = id / 4;
    if ((id & 3) || !slot || slot >= g_message_capacity)
        return nullptr;
    return g_messages[slot];
}

static void message_free(ALPC_MESSAGE *m)
{
    if (!m)
        return;
    if (message_find(m->Id) == m)
        g_messages[m->Id / 4] = nullptr;
    if (m->Data)
        ExFreePool(m->Data);
    if (m->From)
        ObDereferenceObject(m->From);
    if (m->To)
        ObDereferenceObject(m->To);
    if (m->Token)
        ObDereferenceObject(m->Token);
    if (m->View)
        ObDereferenceObject(m->View);
    if (m->Object)
        ObDereferenceObject(m->Object);
    if (m->Client)
        ObDereferenceObject(m->Client);
    if (m->Server)
        ObDereferenceObject(m->Server);
    message_free(m->Reply);
    ExFreePool(m);
}

/* a reply or a notice for a port's queue, outside the table: it needs no answer. */
static ALPC_MESSAGE *notice_new(USHORT type, ULONG data_length)
{
    ALPC_MESSAGE *m = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*m), TAG_ALPC);
    if (!m)
        return nullptr;
    memset(m, 0, sizeof(*m));
    KeInitializeEvent(&m->Done, NotificationEvent, FALSE);
    m->Type = type;
    m->DataLength = data_length;
    if (data_length) {
        m->Data = ExAllocatePool2(POOL_FLAG_NON_PAGED, data_length, TAG_ALPC);
        if (!m->Data) {
            ExFreePool(m);
            return nullptr;
        }
        memset(m->Data, 0, data_length);
    }
    return m;
}

static void enqueue(ALPC_PORT *queue, ALPC_MESSAGE *m)
{
    m->Queue = queue;
    m->State = MSG_QUEUED;
    InsertTailList(&queue->Queue, &m->Links);
    KeSetEvent(&queue->Arrived, IO_NO_INCREMENT, FALSE);
    if (queue->CompletionPort)
        IopQueueCompletion(queue->CompletionPort, queue->CompletionKey, nullptr, STATUS_SUCCESS, 0);
}

/* wakes a sender sleeping on its message, or hands the reply to its port. */
static void complete(ALPC_MESSAGE *m, NTSTATUS status)
{
    m->Status = status;
    m->State = MSG_DONE;
    if (m->Waiting) {
        KeSetEvent(&m->Done, IO_NO_INCREMENT, FALSE);
        return;
    }
    ALPC_PORT *from = m->From;
    if (m->Reply && from && from->State == PORT_CONNECTED) {
        ALPC_MESSAGE *reply = m->Reply;
        m->Reply = nullptr;
        reply->Sequence = ++from->Sequence;
        reply->PortContext = from->Context;
        enqueue(from->Kind == ALPC_KIND_SERVER ? from->Connection : from, reply);
    }
    message_free(m);
}

/* ------------------------------------------------------------------ ports */

static void port_delete(PVOID object)
{
    ALPC_PORT *port = object;
    if (port->Kind == ALPC_KIND_CSR)
        return;
    while (!IsListEmpty(&port->Queue)) {
        ALPC_MESSAGE *m = CONTAINING_RECORD(RemoveHeadList(&port->Queue), ALPC_MESSAGE, Links);
        message_free(m);
    }
    while (!IsListEmpty(&port->Sections)) {
        ALPC_SECTION *s = CONTAINING_RECORD(RemoveHeadList(&port->Sections), ALPC_SECTION, Links);
        ObDereferenceObject(s->Section);
        ExFreePool(s);
    }
    while (!IsListEmpty(&port->Contexts)) {
        ALPC_CONTEXT *c = CONTAINING_RECORD(RemoveHeadList(&port->Contexts), ALPC_CONTEXT, Links);
        if (c->Token)
            ObDereferenceObject(c->Token);
        ExFreePool(c);
    }
    if (port->CompletionPort)
        ObDereferenceObject(port->CompletionPort);
    if (port->Connection)
        ObDereferenceObject(port->Connection);
    if (port->Owner)
        ObDereferenceObject(port->Owner);
}

/* the messages still waiting on a port that goes away: senders that sleep learn it. */
static void fail_queue(ALPC_PORT *port, NTSTATUS status)
{
    LIST_ENTRY *e = port->Queue.Flink;
    while (e != &port->Queue) {
        ALPC_MESSAGE *m = CONTAINING_RECORD(e, ALPC_MESSAGE, Links);
        e = e->Flink;
        if (m->Type == LPC_CONNECTION_REQUEST) {
            RemoveEntryList(&m->Links);
            complete(m, status);
        }
    }
}

/* a communication port that is closed or disconnected: the peer is told, and senders waiting on it wake. */
static void disconnect(ALPC_PORT *port)
{
    if (port->State != PORT_CONNECTED)
        return;
    port->State = PORT_DISCONNECTED;
    ALPC_PORT *peer = port->Peer;
    port->Peer = nullptr;
    /* requests from this end that wait for the other end get their answer now. */
    for (ULONG i = 1; i < g_message_capacity; i++) {
        ALPC_MESSAGE *m = g_messages[i];
        if (!m || m->State == MSG_DONE || m->Type == LPC_CONNECTION_REQUEST)
            continue;
        if (m->From == port || (peer && m->From == peer)) {
            if (m->State == MSG_QUEUED)
                RemoveEntryList(&m->Links);
            complete(m, STATUS_PORT_DISCONNECTED);
        }
    }
    if (!peer)
        return;
    if (peer->State == PORT_CONNECTED) {
        peer->State = PORT_DISCONNECTED;
        peer->Peer = nullptr;
        /* the end that stays is told with the time it happened, in the queue it receives from. */
        ALPC_PORT *queue = peer->Kind == ALPC_KIND_SERVER ? peer->Connection : peer;
        ALPC_MESSAGE *closed = queue ? message_new() : nullptr;
        if (closed)
            closed->Data = ExAllocatePool2(POOL_FLAG_NON_PAGED, 8, TAG_ALPC);
        if (closed && closed->Data) {
            LARGE_INTEGER now;
            KeQuerySystemTime(&now);
            memcpy(closed->Data, &now, 8);
            closed->DataLength = 8;
            closed->Type = LPC_PORT_CLOSED;
            closed->CallbackId = peer->CloseCallbackId;
            closed->PortContext = peer->Context;
            closed->Sequence = ++peer->Sequence;
            enqueue(queue, closed);
        } else {
            message_free(closed);
        }
    }
    ObDereferenceObject(peer);
}

static void port_close(PEPROCESS process, PVOID object, ULONG handle_count)
{
    ALPC_PORT *port = object;
    if (port->Kind == ALPC_KIND_CSR || handle_count != 1)
        return;
    if (port->Kind == ALPC_KIND_CONNECTION) {
        port->State = PORT_DISCONNECTED;
        fail_queue(port, STATUS_PORT_CONNECTION_REFUSED);
        return;
    }
    disconnect(port);
}

void ExInitializeAlpc(void)
{
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = PORT_ALL_ACCESS;
    init.CloseProcedure = port_close;
    init.DeleteProcedure = port_delete;
    ObSetGenericMapping(&init, 0x20001, 0x10001, 0, PORT_ALL_ACCESS);
    ExAlpcPortType = ObCreateObjectType(L"ALPC Port", &init);
}

static NTSTATUS port_new(ULONG kind, const ALPC_PORT_ATTRIBUTES *attributes, POBJECT_ATTRIBUTES oa, ALPC_PORT **out)
{
    ALPC_PORT *port;
    NTSTATUS status = ObCreateObject(ExAlpcPortType, oa, KeGetPreviousMode(), sizeof(*port), (PVOID *)&port);
    if (!NT_SUCCESS(status))
        return status;
    memset(port, 0, sizeof(*port));
    port->Kind = kind;
    if (attributes)
        port->Attributes = *attributes;
    if (!port->Attributes.MaxMessageLength)
        port->Attributes.MaxMessageLength = 0x1000;
    port->Owner = PsCurrentProcess();
    ObReferenceObject(port->Owner);
    InitializeListHead(&port->Queue);
    InitializeListHead(&port->Sections);
    InitializeListHead(&port->Contexts);
    KeInitializeEvent(&port->Arrived, NotificationEvent, FALSE);
    *out = port;
    return STATUS_SUCCESS;
}

static NTSTATUS reference_port(HANDLE handle, ALPC_PORT **out)
{
    NTSTATUS status = ObReferenceHandle(handle, ExAlpcPortType, KeGetPreviousMode(), (PVOID *)out, nullptr);
    if (NT_SUCCESS(status) && (*out)->Kind == ALPC_KIND_CSR) {
        ObDereferenceObject(*out);
        return STATUS_NOT_SUPPORTED;
    }
    return status;
}

static NTSTATUS capture_port_attributes(PVOID user, ALPC_PORT_ATTRIBUTES *attributes)
{
    memset(attributes, 0, sizeof(*attributes));
    return user ? read_caller(attributes, user, sizeof(*attributes)) : STATUS_SUCCESS;
}

NTSTATUS NTAPI NtAlpcCreatePort(PHANDLE handle, POBJECT_ATTRIBUTES attributes, PVOID port_attributes)
{
    ALPC_PORT_ATTRIBUTES pa;
    NTSTATUS status = capture_port_attributes(port_attributes, &pa);
    if (!NT_SUCCESS(status))
        return status;
    OBJECT_ATTRIBUTES oa = {};
    UNICODE_STRING name = {};
    if (attributes) {
        status = ObCaptureObjectAttributes(attributes, &oa, &name);
        if (!NT_SUCCESS(status))
            return status;
    }
    ALPC_PORT *port;
    status = port_new(ALPC_KIND_CONNECTION, port_attributes ? &pa : nullptr, attributes ? &oa : nullptr, &port);
    if (NT_SUCCESS(status)) {
        port->State = PORT_OPEN;
        HANDLE h;
        status = attributes ? ObInsertObjectNamed(port, &oa, PORT_ALL_ACCESS, KeGetPreviousMode(), &h)
                            : ObInsertObject(port, PORT_ALL_ACCESS, 0, &h);
        if (NT_SUCCESS(status))
            status = ObStoreHandle(handle, h);
    }
    if (attributes)
        KiFreeCapturedString(&name);
    return status;
}

/* ------------------------------------------------------------------ tokens */

static PTOKEN sender_token(void)
{
    return SepReferenceEffectiveToken(PsCurrentThread());
}

static void token_ids(PTOKEN token, ULONG64 ids[3])
{
    ids[0] = token ? token->TokenId : 0;
    ids[1] = token ? token->AuthenticationId : 0;
    ids[2] = token ? token->ModifiedId : 0;
}

/* the sid of the user a process runs as. */
static ULONG process_sid(PEPROCESS process, UCHAR sid[SEP_SID_MAX])
{
    PTOKEN token = SepReferencePrimaryToken(process);
    if (!token)
        return 0;
    ULONG length = SepSidLength(token->Groups[0].Sid);
    memcpy(sid, token->Groups[0].Sid, length);
    ObDereferenceObject(token);
    return length;
}

/* ------------------------------------------------------------------ connecting */

static NTSTATUS connect(PHANDLE handle, PUNICODE_STRING name, POBJECT_ATTRIBUTES client_attributes, PVOID port_attributes,
                        ULONG flags, PVOID required_sid, PVOID message, PSIZE_T length, PLARGE_INTEGER timeout)
{
    ALPC_PORT_ATTRIBUTES pa;
    NTSTATUS status = capture_port_attributes(port_attributes, &pa);
    if (!NT_SUCCESS(status))
        return status;
    LARGE_INTEGER wait = {};
    if (timeout && !NT_SUCCESS(read_caller(&wait, timeout, sizeof(wait))))
        return STATUS_ACCESS_VIOLATION;
    ALPC_PORT_HEADER header = {};
    PUCHAR data = nullptr;
    if (message) {
        status = read_caller(&header, message, sizeof(header));
        if (!NT_SUCCESS(status))
            return status;
        if (header.TotalLength < sizeof(header) + header.DataLength || header.DataLength > 0x1000)
            return STATUS_INVALID_PARAMETER;
        if (header.DataLength) {
            data = ExAllocatePool2(POOL_FLAG_NON_PAGED, header.DataLength, TAG_ALPC);
            if (!data)
                return STATUS_INSUFFICIENT_RESOURCES;
            status = read_caller(data, (PUCHAR)message + sizeof(header), header.DataLength);
            if (!NT_SUCCESS(status)) {
                ExFreePool(data);
                return status;
            }
        }
    }
    UCHAR wanted_sid[SEP_SID_MAX];
    if (required_sid) {
        UCHAR head[2];
        status = read_caller(head, required_sid, 2);
        if (NT_SUCCESS(status) && head[1] > 15)
            status = STATUS_INVALID_SID;
        if (NT_SUCCESS(status))
            status = read_caller(wanted_sid, required_sid, 8 + 4 * (SIZE_T)head[1]);
        if (!NT_SUCCESS(status)) {
            if (data)
                ExFreePool(data);
            return status;
        }
    }
    ALPC_PORT *server_port;
    status = ObReferenceObjectByName(name, OBJ_CASE_INSENSITIVE, nullptr, 0, ExAlpcPortType, KernelMode, nullptr,
                                     (PVOID *)&server_port);
    if (NT_SUCCESS(status) && (server_port->Kind != ALPC_KIND_CONNECTION || server_port->State != PORT_OPEN)) {
        ObDereferenceObject(server_port);
        status = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("alpc: %s connects to %wZ: %08x", PsCurrentProcess()->ImageFileName, name, status);
        if (data)
            ExFreePool(data);
        return status;
    }
    ACCESS_MASK granted;
    status = ObCheckOpenAccess(server_port, PORT_CONNECT, KeGetPreviousMode(), &granted);
    if (NT_SUCCESS(status) && required_sid) {
        UCHAR sid[SEP_SID_MAX];
        if (!process_sid(server_port->Owner, sid) || !SepSidEqual(sid, wanted_sid))
            status = STATUS_SERVER_SID_MISMATCH;
    }
    ALPC_PORT *client = nullptr, *server = nullptr;
    if (NT_SUCCESS(status))
        status = port_new(ALPC_KIND_CLIENT, port_attributes ? &pa : nullptr, client_attributes, &client);
    if (NT_SUCCESS(status)) {
        status = port_new(ALPC_KIND_SERVER, nullptr, nullptr, &server);
        if (!NT_SUCCESS(status))
            ObDereferenceObject(client);
    }
    if (NT_SUCCESS(status))
        client->CloseCallbackId = server->CloseCallbackId = g_callback_id++;
    ALPC_MESSAGE *request = NT_SUCCESS(status) ? message_new() : nullptr;
    if (NT_SUCCESS(status) && !request) {
        ObDereferenceObject(client);
        ObDereferenceObject(server);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (!NT_SUCCESS(status)) {
        if (data)
            ExFreePool(data);
        ObDereferenceObject(server_port);
        return status;
    }
    client->State = server->State = PORT_CONNECTING;
    client->Connection = server_port;
    ObReferenceObject(server_port);
    server->Connection = server_port;
    ObReferenceObject(server_port);
    request->Type = LPC_CONNECTION_REQUEST | LPC_CONTINUATION_REQUIRED;
    request->DataLength = header.DataLength;
    request->Data = data;
    request->Process = (ULONG64)PsCurrentProcess()->UniqueProcessId;
    request->Thread = (ULONG64)PsCurrentThread()->Cid.UniqueThread;
    request->Client = client;
    request->Server = server;
    request->Waiting = TRUE;
    PTOKEN token = sender_token();
    token_ids(token, request->TokenIds);
    request->Token = token;
    request->Level = pa.SecurityQos.ImpersonationLevel;
    request->TellToken = TRUE;
    enqueue(server_port, request);
    ObDereferenceObject(server_port);

    status = KeWaitForSingleObject(&request->Done, UserRequest, UserMode, FALSE, timeout ? &wait : nullptr);
    if (status != STATUS_WAIT_0 && request->State != MSG_DONE) {
        /* a request the server has not seen goes, one it holds is refused when it answers. */
        request->Waiting = FALSE;
        if (request->State == MSG_QUEUED) {
            RemoveEntryList(&request->Links);
            message_free(request);
        }
        return status;
    }
    status = request->Status;
    ALPC_MESSAGE *reply = request->Reply;
    request->Reply = nullptr;
    if (NT_SUCCESS(status)) {
        HANDLE h;
        ObReferenceObject(client);
        status = client_attributes && client_attributes->ObjectName
                     ? ObInsertObjectNamed(client, client_attributes, PORT_ALL_ACCESS, KernelMode, &h)
                     : ObInsertObject(client, PORT_ALL_ACCESS, 0, &h);
        if (NT_SUCCESS(status)) {
            client->Context = h;
            status = ObStoreHandle(handle, h);
        }
        if (NT_SUCCESS(status) && message && reply) {
            ALPC_PORT_HEADER out = {};
            out.DataLength = (USHORT)reply->DataLength;
            out.TotalLength = (USHORT)(sizeof(out) + reply->DataLength);
            out.Type = LPC_CONNECTION_REPLY;
            out.Process = reply->Process;
            out.Thread = reply->Thread;
            out.MessageId = request->Id;
            out.CallbackId = request->CallbackId;
            SIZE_T room = out.TotalLength;
            if (NT_SUCCESS(write_caller(message, &out, sizeof(out))) &&
                NT_SUCCESS(write_caller((PUCHAR)message + sizeof(out), reply->Data, reply->DataLength)) && length)
                write_caller(length, &room, sizeof(room));
        }
        /*
         * a connection asked for without ALPC_MSGFLG_SYNC_REQUEST is answered on the port: the reply of the server
         * comes as a message there, which rpcrt4 waits for with its completion port before an asynchronous call.
         */
        if (NT_SUCCESS(status) && !(flags & ALPC_MSGFLG_SYNC_REQUEST)) {
            ALPC_MESSAGE *note = message_new();
            ULONG bytes = reply ? reply->DataLength : 0;
            if (note && bytes)
                note->Data = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, TAG_ALPC);
            if (note && (!bytes || note->Data)) {
                if (bytes)
                    memcpy(note->Data, reply->Data, bytes);
                note->DataLength = bytes;
                note->Type = LPC_CONNECTION_REPLY;
                note->Id = request->Id;
                note->CallbackId = request->CallbackId;
                note->Process = reply ? reply->Process : 0;
                note->Thread = reply ? reply->Thread : 0;
                note->Sequence = ++client->Sequence;
                enqueue(client, note);
            } else {
                message_free(note);
            }
        }
    }
    message_free(reply);
    message_free(request);
    return status;
}

NTSTATUS NTAPI NtAlpcConnectPort(PHANDLE handle, PUNICODE_STRING port_name, POBJECT_ATTRIBUTES attributes,
                                 PVOID port_attributes, ULONG flags, PVOID required_sid, PVOID message,
                                 PSIZE_T length, PVOID out_attributes, PVOID in_attributes, PLARGE_INTEGER timeout)
{
    UNICODE_STRING name;
    NTSTATUS status = KiCaptureUnicodeString(&name, port_name);
    if (!NT_SUCCESS(status))
        return status;
    OBJECT_ATTRIBUTES oa = {};
    UNICODE_STRING client_name = {};
    if (attributes) {
        status = ObCaptureObjectAttributes(attributes, &oa, &client_name);
        if (!NT_SUCCESS(status)) {
            KiFreeCapturedString(&name);
            return status;
        }
    }
    status = connect(handle, &name, attributes ? &oa : nullptr, port_attributes, flags, required_sid, message, length,
                     timeout);
    if (attributes)
        KiFreeCapturedString(&client_name);
    KiFreeCapturedString(&name);
    return status;
}

/*
 * the server's security requirements are a descriptor the server process must pass. rpcrt4 gives one that lets every
 * server through, and mime keeps no other: the descriptor is read but not checked against the server.
 */
NTSTATUS NTAPI NtAlpcConnectPortEx(PHANDLE handle, POBJECT_ATTRIBUTES connection_attributes,
                                   POBJECT_ATTRIBUTES client_attributes, PVOID port_attributes, ULONG flags,
                                   PVOID server_requirements, PVOID message, PSIZE_T length, PVOID out_attributes,
                                   PVOID in_attributes, PLARGE_INTEGER timeout)
{
    OBJECT_ATTRIBUTES coa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(connection_attributes, &coa, &name);
    if (!NT_SUCCESS(status))
        return status;
    if (!coa.ObjectName) {
        KiFreeCapturedString(&name);
        return STATUS_INVALID_PARAMETER;
    }
    OBJECT_ATTRIBUTES oa = {};
    UNICODE_STRING client_name = {};
    if (client_attributes) {
        status = ObCaptureObjectAttributes(client_attributes, &oa, &client_name);
        if (!NT_SUCCESS(status)) {
            KiFreeCapturedString(&name);
            return status;
        }
    }
    status = connect(handle, &name, client_attributes ? &oa : nullptr, port_attributes, flags, nullptr, message, length,
                     timeout);
    if (client_attributes)
        KiFreeCapturedString(&client_name);
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtAlpcAcceptConnectPort(PHANDLE handle, HANDLE connection_handle, ULONG flags,
                                       POBJECT_ATTRIBUTES attributes, PVOID port_attributes, PVOID context,
                                       PVOID request_message, PVOID message_attributes, BOOLEAN accept)
{
    ALPC_PORT_ATTRIBUTES pa;
    NTSTATUS status = capture_port_attributes(port_attributes, &pa);
    if (!NT_SUCCESS(status))
        return status;
    ALPC_PORT_HEADER header;
    status = read_caller(&header, request_message, sizeof(header));
    if (!NT_SUCCESS(status))
        return status;
    if (header.TotalLength < sizeof(header) + header.DataLength)
        return STATUS_INVALID_PARAMETER;
    ALPC_MESSAGE *reply = notice_new(LPC_CONNECTION_REPLY, header.DataLength);
    if (!reply)
        return STATUS_INSUFFICIENT_RESOURCES;
    status = read_caller(reply->Data, (PUCHAR)request_message + sizeof(header), header.DataLength);
    if (!NT_SUCCESS(status)) {
        message_free(reply);
        return status;
    }
    ALPC_PORT *port;
    status = reference_port(connection_handle, &port);
    if (!NT_SUCCESS(status)) {
        message_free(reply);
        return status;
    }
    ALPC_MESSAGE *request = message_find(header.MessageId);
    if (port->Kind != ALPC_KIND_CONNECTION || !request || request->Type != (LPC_CONNECTION_REQUEST | LPC_CONTINUATION_REQUIRED) ||
        request->State != MSG_RECEIVED || request->Queue != port) {
        ObDereferenceObject(port);
        message_free(reply);
        return STATUS_ALPC_REPLY_MISMATCH;
    }
    reply->Process = (ULONG64)PsCurrentProcess()->UniqueProcessId;
    reply->Thread = (ULONG64)PsCurrentThread()->Cid.UniqueThread;
    request->Reply = reply;
    ALPC_PORT *client = request->Client, *server = request->Server;
    if (!accept || !request->Waiting || !client || !server) {
        complete(request, STATUS_PORT_CONNECTION_REFUSED);
        ObDereferenceObject(port);
        return STATUS_SUCCESS;
    }
    server->Attributes = port_attributes ? pa : port->Attributes;
    if (!server->Attributes.MaxMessageLength)
        server->Attributes.MaxMessageLength = 0x1000;
    server->Context = context;
    HANDLE h = nullptr;
    if (handle) {
        ObReferenceObject(server);
        status = ObInsertObject(server, PORT_ALL_ACCESS, 0, &h);
        /* wow64 passes the place of the handle on: a 32-bit program keeps it in four bytes. */
        if (NT_SUCCESS(status) && (flags & ALPC_MSGFLG_WOW64_CALL)) {
            ULONG narrow = (ULONG)(ULONG_PTR)h;
            status = write_caller(handle, &narrow, sizeof(narrow));
            if (!NT_SUCCESS(status))
                NtClose(h);
        } else if (NT_SUCCESS(status)) {
            status = ObStoreHandle(handle, h);
        }
    }
    if (!NT_SUCCESS(status)) {
        complete(request, STATUS_PORT_CONNECTION_REFUSED);
        ObDereferenceObject(port);
        return status;
    }
    client->State = server->State = PORT_CONNECTED;
    client->Peer = server;
    ObReferenceObject(server);
    server->Peer = client;
    ObReferenceObject(client);
    /* the connection reply is the first message the client gets. */
    client->Sequence = 1;
    complete(request, STATUS_SUCCESS);
    ObDereferenceObject(port);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ sections, views and security contexts */

static ULONG g_object_ids = 0x100;

static ALPC_SECTION *find_section(ALPC_PORT *port, ULONG id)
{
    for (LIST_ENTRY *e = port->Sections.Flink; e != &port->Sections; e = e->Flink) {
        ALPC_SECTION *s = CONTAINING_RECORD(e, ALPC_SECTION, Links);
        if (s->Id == id)
            return s;
    }
    if (port->Peer)
        for (LIST_ENTRY *e = port->Peer->Sections.Flink; e != &port->Peer->Sections; e = e->Flink) {
            ALPC_SECTION *s = CONTAINING_RECORD(e, ALPC_SECTION, Links);
            if (s->Id == id)
                return s;
        }
    return nullptr;
}

static LIST_ENTRY g_views = {&g_views, &g_views};

static ALPC_VIEW *find_view(PEPROCESS process, PVOID base)
{
    for (LIST_ENTRY *e = g_views.Flink; e != &g_views; e = e->Flink) {
        ALPC_VIEW *v = CONTAINING_RECORD(e, ALPC_VIEW, Links);
        if (v->Process == process && v->Base == base)
            return v;
    }
    return nullptr;
}

static NTSTATUS map_view(PSECTION section, SIZE_T size, PVOID *base)
{
    ALPC_VIEW *v = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*v), TAG_ALPC);
    if (!v)
        return STATUS_INSUFFICIENT_RESOURCES;
    PVOID at = nullptr;
    SIZE_T length = size;
    NTSTATUS status = MiMapViewOfSection(section, PsCurrentProcess(), &at, 0, &length, PAGE_READWRITE, 0);
    if (!NT_SUCCESS(status)) {
        ExFreePool(v);
        return status;
    }
    v->Process = PsCurrentProcess();
    v->Section = section;
    ObReferenceObject(section);
    v->Base = at;
    v->Size = size;
    InsertTailList(&g_views, &v->Links);
    *base = at;
    return STATUS_SUCCESS;
}

static NTSTATUS unmap_view(PVOID base)
{
    ALPC_VIEW *v = find_view(PsCurrentProcess(), base);
    if (!v)
        return STATUS_INVALID_PARAMETER;
    RemoveEntryList(&v->Links);
    MiUnmapViewOfSection(PsCurrentProcess(), v->Base);
    ObDereferenceObject(v->Section);
    ExFreePool(v);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtAlpcCreatePortSection(HANDLE port_handle, ULONG flags, HANDLE section_handle, SIZE_T size,
                                       PHANDLE alpc_section, PSIZE_T actual_size)
{
    if (!size && !section_handle)
        return STATUS_INVALID_PARAMETER;
    ALPC_PORT *port;
    NTSTATUS status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    PSECTION section = nullptr;
    if (section_handle)
        status = ObReferenceHandleFor(section_handle, SECTION_MAP_READ | SECTION_MAP_WRITE, MmSectionType,
                                      KeGetPreviousMode(), &section);
    else
        status = MiCreateDataSection(nullptr, (size + PAGE_SIZE - 1) & ~(SIZE_T)(PAGE_SIZE - 1), PAGE_READWRITE,
                                     SEC_COMMIT, &section);
    ALPC_SECTION *s = NT_SUCCESS(status) ? ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*s), TAG_ALPC) : nullptr;
    if (NT_SUCCESS(status) && !s) {
        ObDereferenceObject(section);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (NT_SUCCESS(status)) {
        s->Section = section;
        s->Size = (size + PAGE_SIZE - 1) & ~(SIZE_T)(PAGE_SIZE - 1);
        s->Id = g_object_ids++ * 4;
        InsertTailList(&port->Sections, &s->Links);
        HANDLE id = (HANDLE)(ULONG_PTR)s->Id;
        status = write_caller(alpc_section, &id, sizeof(id));
        if (NT_SUCCESS(status) && actual_size)
            status = write_caller(actual_size, &s->Size, sizeof(s->Size));
    }
    ObDereferenceObject(port);
    return status;
}

NTSTATUS NTAPI NtAlpcDeletePortSection(HANDLE port_handle, ULONG flags, HANDLE alpc_section)
{
    ALPC_PORT *port;
    NTSTATUS status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    ALPC_SECTION *s = find_section(port, (ULONG)(ULONG_PTR)alpc_section);
    if (s) {
        RemoveEntryList(&s->Links);
        ObDereferenceObject(s->Section);
        ExFreePool(s);
    }
    ObDereferenceObject(port);
    return s ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}

typedef struct ALPC_VIEW_ATTRIBUTE {
    ULONG Flags, Pad;
    ULONG64 SectionHandle;
    PVOID ViewBase;
    SIZE_T ViewSize;
} ALPC_VIEW_ATTRIBUTE;

NTSTATUS NTAPI NtAlpcCreateSectionView(HANDLE port_handle, ULONG flags, PVOID view_attributes)
{
    ALPC_VIEW_ATTRIBUTE va;
    NTSTATUS status = read_caller(&va, view_attributes, sizeof(va));
    if (!NT_SUCCESS(status))
        return status;
    ALPC_PORT *port;
    status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    ALPC_SECTION *s = find_section(port, (ULONG)va.SectionHandle);
    if (!s)
        status = STATUS_INVALID_HANDLE;
    else if (va.ViewSize > s->Size)
        status = STATUS_INVALID_VIEW_SIZE;
    if (NT_SUCCESS(status)) {
        SIZE_T size = va.ViewSize ? va.ViewSize : s->Size;
        status = map_view(s->Section, size, &va.ViewBase);
        va.ViewSize = size;
    }
    if (NT_SUCCESS(status))
        status = write_caller(view_attributes, &va, sizeof(va));
    ObDereferenceObject(port);
    return status;
}

NTSTATUS NTAPI NtAlpcDeleteSectionView(HANDLE port_handle, ULONG flags, PVOID base)
{
    ALPC_PORT *port;
    NTSTATUS status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    status = unmap_view(base);
    ObDereferenceObject(port);
    return status;
}

typedef struct ALPC_SECURITY_ATTRIBUTE {
    ULONG Flags, Pad;
    ALPC_QOS *QoS;
    ULONG64 ContextHandle;
} ALPC_SECURITY_ATTRIBUTE;

static ALPC_CONTEXT *find_context(ALPC_PORT *port, ULONG id)
{
    for (LIST_ENTRY *e = port->Contexts.Flink; e != &port->Contexts; e = e->Flink) {
        ALPC_CONTEXT *c = CONTAINING_RECORD(e, ALPC_CONTEXT, Links);
        if (c->Id == id)
            return c;
    }
    return nullptr;
}

NTSTATUS NTAPI NtAlpcCreateSecurityContext(HANDLE port_handle, ULONG flags, PVOID security_attribute)
{
    ALPC_SECURITY_ATTRIBUTE sa;
    NTSTATUS status = read_caller(&sa, security_attribute, sizeof(sa));
    if (!NT_SUCCESS(status))
        return status;
    ALPC_QOS qos = {};
    if (!sa.QoS || !NT_SUCCESS(read_caller(&qos, sa.QoS, sizeof(qos))))
        return STATUS_INVALID_PARAMETER;
    ALPC_PORT *port;
    status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    ALPC_CONTEXT *c = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), TAG_ALPC);
    if (!c) {
        ObDereferenceObject(port);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memset(c, 0, sizeof(*c));
    c->Token = sender_token();
    c->Level = qos.ImpersonationLevel;
    c->EffectiveOnly = qos.EffectiveOnly;
    c->Id = g_object_ids++ * 4;
    InsertTailList(&port->Contexts, &c->Links);
    sa.ContextHandle = c->Id;
    status = write_caller(security_attribute, &sa, sizeof(sa));
    ObDereferenceObject(port);
    return status;
}

static NTSTATUS drop_context(HANDLE port_handle, HANDLE context, BOOLEAN revoke)
{
    ALPC_PORT *port;
    NTSTATUS status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    ALPC_CONTEXT *c = find_context(port, (ULONG)(ULONG_PTR)context);
    if (c && revoke) {
        c->Revoked = TRUE;
    } else if (c) {
        RemoveEntryList(&c->Links);
        if (c->Token)
            ObDereferenceObject(c->Token);
        ExFreePool(c);
    }
    ObDereferenceObject(port);
    return c ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}

NTSTATUS NTAPI NtAlpcDeleteSecurityContext(HANDLE port_handle, ULONG flags, HANDLE context)
{
    return drop_context(port_handle, context, FALSE);
}

NTSTATUS NTAPI NtAlpcRevokeSecurityContext(HANDLE port_handle, ULONG flags, HANDLE context)
{
    return drop_context(port_handle, context, TRUE);
}

NTSTATUS NTAPI NtAlpcCreateResourceReserve(HANDLE port_handle, ULONG flags, SIZE_T size, PHANDLE reserve)
{
    ALPC_PORT *port;
    NTSTATUS status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    HANDLE id = (HANDLE)(ULONG_PTR)(g_object_ids++ * 4);
    /* wow64 passes the place of the reserve on: a 32-bit program keeps it in four bytes. */
    status = write_caller(reserve, &id, PspIsWow64Process(PsCurrentProcess()) ? sizeof(ULONG) : sizeof(id));
    ObDereferenceObject(port);
    return status;
}

NTSTATUS NTAPI NtAlpcDeleteResourceReserve(HANDLE port_handle, ULONG flags, HANDLE reserve)
{
    ALPC_PORT *port;
    NTSTATUS status = reference_port(port_handle, &port);
    if (NT_SUCCESS(status))
        ObDereferenceObject(port);
    return status;
}

/* ------------------------------------------------------------------ sending and receiving */

typedef struct ALPC_CONTEXT_ATTRIBUTE {
    PVOID PortContext, MessageContext;
    ULONG Sequence, MessageId, CallbackId, Pad;
} ALPC_CONTEXT_ATTRIBUTE;

typedef struct ALPC_HANDLE_ATTRIBUTE {
    ULONG Flags, Pad;
    ULONG64 Handle;
    ULONG ObjectType;
    ACCESS_MASK DesiredAccess;
} ALPC_HANDLE_ATTRIBUTE;

#define ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS 0x10000

/* what a sender attaches to a message, from the attributes it says are valid. */
static NTSTATUS attach(ALPC_MESSAGE *m, ALPC_PORT *port, PVOID user, BOOLEAN wow)
{
    PTOKEN token = sender_token();
    token_ids(token, m->TokenIds);
    m->Token = token;
    m->Level = port->Attributes.SecurityQos.ImpersonationLevel;
    m->EffectiveOnly = port->Attributes.SecurityQos.EffectiveOnly;
    if (!user)
        return STATUS_SUCCESS;
    ULONG head[2];
    NTSTATUS status = read_caller(head, user, sizeof(head));
    if (!NT_SUCCESS(status))
        return status;
    ULONG allocated = head[0], valid = head[1] & head[0];
    if (valid & ATTR_SECURITY) {
        ALPC_SECURITY_ATTRIBUTE sa;
        status = read_attribute(&sa, sizeof(sa), user, allocated, ATTR_SECURITY, wow, g_security_fields);
        if (!NT_SUCCESS(status))
            return status;
        /* rpcrt4 gives -2 for the identity the port was connected with, which is taken as the message goes. */
        BOOLEAN own = sa.ContextHandle == (ULONG64)-2;
        ALPC_CONTEXT *c = sa.ContextHandle && !own ? find_context(port, (ULONG)sa.ContextHandle) : nullptr;
        if (sa.ContextHandle && !own && (!c || c->Revoked)) {
            KiLogPrintf("alpc: security context %llx flags %x is not one of the port", sa.ContextHandle, sa.Flags);
            return STATUS_INVALID_HANDLE;
        }
        if (c) {
            if (m->Token)
                ObDereferenceObject(m->Token);
            m->Token = c->Token;
            if (m->Token)
                ObReferenceObject(m->Token);
            m->Level = c->Level;
            m->EffectiveOnly = c->EffectiveOnly;
        } else if (sa.QoS) {
            ALPC_QOS qos;
            status = read_caller(&qos, sa.QoS, sizeof(qos));
            if (!NT_SUCCESS(status))
                return status;
            m->Level = qos.ImpersonationLevel;
            m->EffectiveOnly = qos.EffectiveOnly;
        }
        m->HasSecurity = TRUE;
    }
    if (valid & ATTR_VIEW) {
        ALPC_VIEW_ATTRIBUTE va;
        status = read_attribute(&va, sizeof(va), user, allocated, ATTR_VIEW, wow, g_view_fields);
        if (!NT_SUCCESS(status))
            return status;
        if (va.ViewBase) {
            ALPC_VIEW *v = find_view(PsCurrentProcess(), va.ViewBase);
            if (!v)
                return STATUS_INVALID_VIEW_SIZE;
            m->View = v->Section;
            ObReferenceObject(m->View);
            m->ViewSize = va.ViewSize && va.ViewSize <= v->Size ? va.ViewSize : v->Size;
        }
    }
    if (valid & ATTR_CONTEXT) {
        ALPC_CONTEXT_ATTRIBUTE ca;
        status = read_attribute(&ca, sizeof(ca), user, allocated, ATTR_CONTEXT, wow, g_context_fields);
        if (!NT_SUCCESS(status))
            return status;
        m->SenderMessageContext = ca.MessageContext;
    }
    if (valid & ATTR_HANDLE) {
        ALPC_HANDLE_ATTRIBUTE ha;
        status = read_attribute(&ha, sizeof(ha), user, allocated, ATTR_HANDLE, wow, g_handle_fields);
        if (!NT_SUCCESS(status))
            return status;
        if (ha.Handle) {
            ACCESS_MASK granted = 0;
            status = ObReferenceHandle((HANDLE)ha.Handle, nullptr, KeGetPreviousMode(), &m->Object, &granted);
            if (!NT_SUCCESS(status)) {
                KiLogPrintf("alpc: handle attribute %llx flags %x type %x: %08x", ha.Handle, ha.Flags, ha.ObjectType,
                            status);
                return status;
            }
            m->ObjectAccess = (ha.Flags & ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS) ? granted : ha.DesiredAccess;
            m->ObjectType = ha.ObjectType;
        }
    }
    return STATUS_SUCCESS;
}

/* writes a message and the attributes the receiver allocated for it into the receiver's buffers. */
static NTSTATUS deliver(ALPC_MESSAGE *m, ULONG id, ULONG callback, PVOID buffer, PVOID attributes, BOOLEAN wow)
{
    ALPC_PORT_HEADER out = {};
    out.DataLength = (USHORT)m->DataLength;
    out.TotalLength = (USHORT)(sizeof(out) + m->DataLength);
    out.Type = m->Type;
    out.Process = m->Process;
    out.Thread = m->Thread;
    out.MessageId = id;
    out.CallbackId = callback;
    NTSTATUS status = write_caller(buffer, &out, sizeof(out));
    if (NT_SUCCESS(status))
        status = write_caller((PUCHAR)buffer + sizeof(out), m->Data, m->DataLength);
    if (!NT_SUCCESS(status) || !attributes)
        return status;
    ULONG allocated;
    status = read_caller(&allocated, attributes, sizeof(allocated));
    if (!NT_SUCCESS(status))
        return status;
    ULONG valid = 0;
    if ((allocated & ATTR_SECURITY) && m->HasSecurity) {
        ALPC_SECURITY_ATTRIBUTE sa = {};
        status = write_attribute(attributes, allocated, ATTR_SECURITY, wow, &sa, sizeof(sa), g_security_fields);
        valid |= ATTR_SECURITY;
    }
    if ((allocated & ATTR_VIEW) && m->View && NT_SUCCESS(status)) {
        ALPC_VIEW_ATTRIBUTE va = {};
        status = map_view(m->View, m->ViewSize, &va.ViewBase);
        if (NT_SUCCESS(status)) {
            va.ViewSize = m->ViewSize;
            m->ReceiverView = va.ViewBase;
            status = write_attribute(attributes, allocated, ATTR_VIEW, wow, &va, sizeof(va), g_view_fields);
            valid |= ATTR_VIEW;
        }
    }
    if ((allocated & ATTR_CONTEXT) && NT_SUCCESS(status) && m->Type != (LPC_CONNECTION_REQUEST | LPC_CONTINUATION_REQUIRED)) {
        ALPC_CONTEXT_ATTRIBUTE ca = {m->PortContext, m->MessageContext, m->Sequence, id, callback, 0};
        status = write_attribute(attributes, allocated, ATTR_CONTEXT, wow, &ca, sizeof(ca), g_context_fields);
        valid |= ATTR_CONTEXT;
    }
    if ((allocated & ATTR_HANDLE) && m->Object && NT_SUCCESS(status)) {
        ALPC_HANDLE_ATTRIBUTE ha = {};
        HANDLE h;
        status = ObCreateHandle(PsCurrentProcess(), m->Object, m->ObjectAccess, 0, &h);
        if (NT_SUCCESS(status)) {
            ha.Handle = (ULONG64)h;
            ha.ObjectType = m->ObjectType;
            ha.DesiredAccess = m->ObjectAccess;
            status = write_attribute(attributes, allocated, ATTR_HANDLE, wow, &ha, sizeof(ha), g_handle_fields);
            valid |= ATTR_HANDLE;
        }
    }
    if ((allocated & ATTR_TOKEN) && m->Token && m->TellToken && NT_SUCCESS(status)) {
        status = write_caller((PUCHAR)attributes + attribute_offset(allocated, ATTR_TOKEN, wow), m->TokenIds,
                              sizeof(m->TokenIds));
        valid |= ATTR_TOKEN;
    }
    if (NT_SUCCESS(status))
        status = write_caller((PULONG)attributes + 1, &valid, sizeof(valid));
    return status;
}

/* a thread that ends must not stay in a wait for a server, so the wait is a user mode one whatever the flags say. */
static NTSTATUS wait_flags(ULONG flags, PLARGE_INTEGER timeout, PKEVENT event)
{
    return KeWaitForSingleObject(event, UserRequest, UserMode, (flags & ALPC_MSGFLG_WAIT_ALERTABLE) != 0, timeout);
}

static NTSTATUS receive(ALPC_PORT *port, ULONG flags, PVOID buffer, PSIZE_T length, PVOID attributes,
                        PLARGE_INTEGER timeout)
{
    ALPC_PORT *queue = port->Kind == ALPC_KIND_SERVER ? port->Connection : port;
    if (!queue)
        return STATUS_INVALID_PARAMETER;
    SIZE_T room = 0;
    if (length && !NT_SUCCESS(read_caller(&room, length, sizeof(room))))
        return STATUS_ACCESS_VIOLATION;
    if (!length)
        room = (SIZE_T)port->Attributes.MaxMessageLength + sizeof(ALPC_PORT_HEADER);
    for (;;) {
        if (!IsListEmpty(&queue->Queue))
            break;
        if (port->Kind == ALPC_KIND_CLIENT && port->State == PORT_DISCONNECTED)
            return STATUS_PORT_DISCONNECTED;
        KeResetEvent(&queue->Arrived);
        NTSTATUS status = wait_flags(flags, timeout, &queue->Arrived);
        if (status != STATUS_WAIT_0)
            return status;
    }
    ALPC_MESSAGE *m = CONTAINING_RECORD(queue->Queue.Flink, ALPC_MESSAGE, Links);
    if (room < sizeof(ALPC_PORT_HEADER) + m->DataLength) {
        room = sizeof(ALPC_PORT_HEADER) + m->DataLength;
        if (length)
            write_caller(length, &room, sizeof(room));
        return STATUS_BUFFER_TOO_SMALL;
    }
    RemoveEntryList(&m->Links);
    if (IsListEmpty(&queue->Queue))
        KeResetEvent(&queue->Arrived);
    m->State = MSG_RECEIVED;
    m->Receiver = PsCurrentProcess();
    ULONG id = m->Id, callback = m->CallbackId;
    BOOLEAN tracked = message_find(id) == m;
    NTSTATUS status = deliver(m, id, callback, buffer, attributes, (flags & ALPC_MSGFLG_WOW64_CALL) != 0);
    /* a datagram, a reply or a notice is done once received. */
    if (!tracked || !(m->Type & LPC_CONTINUATION_REQUIRED))
        message_free(m);
    return status;
}

/* a reply to a message this process received: the sender wakes, or gets it in its queue. */
static NTSTATUS reply_to(ALPC_PORT *port, const ALPC_PORT_HEADER *header, PUCHAR data, PVOID attributes, BOOLEAN wow)
{
    ALPC_MESSAGE *m = message_find(header->MessageId);
    if (!m || m->State != MSG_RECEIVED || m->Receiver != PsCurrentProcess() ||
        (m->Type & 0xFF) == LPC_CONNECTION_REQUEST)
        return STATUS_ALPC_REPLY_MISMATCH;
    if (!(m->Type & LPC_CONTINUATION_REQUIRED)) {
        message_free(m);
        return STATUS_SUCCESS;
    }
    ALPC_MESSAGE *reply = notice_new(LPC_REPLY, 0);
    if (!reply)
        return STATUS_INSUFFICIENT_RESOURCES;
    reply->DataLength = header->DataLength;
    reply->Data = data;
    reply->Process = (ULONG64)PsCurrentProcess()->UniqueProcessId;
    reply->Thread = (ULONG64)PsCurrentThread()->Cid.UniqueThread;
    reply->MessageContext = m->SenderMessageContext;
    if (attributes) {
        ULONG head[2];
        NTSTATUS status = read_caller(head, attributes, sizeof(head));
        if (NT_SUCCESS(status) && (head[1] & head[0] & ATTR_VIEW)) {
            ALPC_VIEW_ATTRIBUTE va;
            status = read_attribute(&va, sizeof(va), attributes, head[0], ATTR_VIEW, wow, g_view_fields);
            /* the view the request brought is let go of here, and one of this side may go back with the reply. */
            if (NT_SUCCESS(status) && (va.Flags & ALPC_VIEWFLG_UNMAP_EXISTING) && m->ReceiverView) {
                unmap_view(m->ReceiverView);
                m->ReceiverView = nullptr;
            }
            if (NT_SUCCESS(status) && va.ViewBase) {
                ALPC_VIEW *v = find_view(PsCurrentProcess(), va.ViewBase);
                if (v) {
                    reply->View = v->Section;
                    ObReferenceObject(reply->View);
                    reply->ViewSize = va.ViewSize && va.ViewSize <= v->Size ? va.ViewSize : v->Size;
                }
            }
        }
    }
    reply->Id = m->Id;
    reply->CallbackId = m->CallbackId;
    m->Reply = reply;
    complete(m, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ExCsrSendWaitReceivePort(PVOID object, ULONG flags, PVOID send, PVOID send_attributes, PVOID receive,
                                        PSIZE_T length, PVOID receive_attributes, PLARGE_INTEGER timeout);

NTSTATUS NTAPI NtAlpcSendWaitReceivePort(HANDLE handle, ULONG flags, PVOID send, PVOID send_attributes,
                                         PVOID receive_buffer, PSIZE_T length, PVOID receive_attributes,
                                         PLARGE_INTEGER timeout)
{
    ALPC_PORT *port;
    NTSTATUS status = ObReferenceHandle(handle, ExAlpcPortType, KeGetPreviousMode(), (PVOID *)&port, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    if (port->Kind == ALPC_KIND_CSR)
        return ExCsrSendWaitReceivePort(port, flags, send, send_attributes, receive_buffer, length,
                                        receive_attributes, timeout);
    LARGE_INTEGER wait = {};
    if (timeout && !NT_SUCCESS(read_caller(&wait, timeout, sizeof(wait)))) {
        ObDereferenceObject(port);
        return STATUS_ACCESS_VIOLATION;
    }
    PLARGE_INTEGER until = timeout ? &wait : nullptr;
    if (!send) {
        status = receive_buffer ? receive(port, flags, receive_buffer, length, receive_attributes, until)
                                : STATUS_INVALID_PARAMETER;
        ObDereferenceObject(port);
        return status;
    }
    ALPC_PORT_HEADER header;
    status = read_caller(&header, send, sizeof(header));
    if (NT_SUCCESS(status) && header.TotalLength < sizeof(header) + header.DataLength)
        status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(status) && header.DataLength > port->Attributes.MaxMessageLength)
        status = STATUS_PORT_MESSAGE_TOO_LONG;
    PUCHAR data = nullptr;
    if (NT_SUCCESS(status) && header.DataLength) {
        data = ExAllocatePool2(POOL_FLAG_NON_PAGED, header.DataLength, TAG_ALPC);
        status = data ? read_caller(data, (PUCHAR)send + sizeof(header), header.DataLength)
                      : STATUS_INSUFFICIENT_RESOURCES;
    }
    if (!NT_SUCCESS(status)) {
        if (data)
            ExFreePool(data);
        ObDereferenceObject(port);
        return status;
    }
    if (header.MessageId) {
        status = reply_to(port, &header, data, send_attributes, (flags & ALPC_MSGFLG_WOW64_CALL) != 0);
        if (!NT_SUCCESS(status) && data)
            ExFreePool(data);
        if (NT_SUCCESS(status) && receive_buffer)
            status = receive(port, flags, receive_buffer, length, receive_attributes, until);
        ObDereferenceObject(port);
        return status;
    }
    ALPC_PORT *peer = port->Peer;
    if ((port->Kind != ALPC_KIND_CLIENT && port->Kind != ALPC_KIND_SERVER) || port->State != PORT_CONNECTED ||
        !peer) {
        if (data)
            ExFreePool(data);
        status = port->Kind == ALPC_KIND_CONNECTION ? STATUS_INVALID_PARAMETER : STATUS_PORT_DISCONNECTED;
        ObDereferenceObject(port);
        return status;
    }
    ALPC_MESSAGE *m = message_new();
    if (!m) {
        if (data)
            ExFreePool(data);
        ObDereferenceObject(port);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    m->Data = data;
    m->DataLength = header.DataLength;
    /* every message waits for an answer but one sent as a datagram. */
    m->Type = (header.Type & 0xFF) == LPC_DATAGRAM ? LPC_DATAGRAM : LPC_REQUEST | LPC_CONTINUATION_REQUIRED;
    m->Process = (ULONG64)PsCurrentProcess()->UniqueProcessId;
    m->Thread = (ULONG64)PsCurrentThread()->Cid.UniqueThread;
    m->From = port;
    ObReferenceObject(port);
    m->To = peer;
    ObReferenceObject(peer);
    m->PortContext = peer->Context;
    m->Sequence = ++peer->Sequence;
    status = attach(m, port, send_attributes, (flags & ALPC_MSGFLG_WOW64_CALL) != 0);
    /* measured: the token is told with the first message of a connection and after it changes. */
    m->TellToken = memcmp(peer->TokenIds, m->TokenIds, sizeof(m->TokenIds)) != 0;
    memcpy(peer->TokenIds, m->TokenIds, sizeof(m->TokenIds));
    if (!NT_SUCCESS(status)) {
        message_free(m);
        ObDereferenceObject(port);
        return status;
    }
    BOOLEAN sync = (flags & ALPC_MSGFLG_SYNC_REQUEST) && m->Type != LPC_DATAGRAM;
    m->Waiting = sync;
    ALPC_PORT *queue = peer->Kind == ALPC_KIND_SERVER ? peer->Connection : peer;
    enqueue(queue, m);
    if (!sync) {
        status = receive_buffer ? receive(port, flags, receive_buffer, length, receive_attributes, until)
                                : STATUS_SUCCESS;
        ObDereferenceObject(port);
        return status;
    }
    status = wait_flags(flags, until, &m->Done);
    if (status != STATUS_WAIT_0 && m->State != MSG_DONE) {
        /* the sender leaves: a reply that comes later finds no one, and a message not received yet goes. */
        m->Waiting = FALSE;
        if (m->State == MSG_QUEUED) {
            RemoveEntryList(&m->Links);
            message_free(m);
        }
        ObDereferenceObject(port);
        return status;
    }
    status = m->Status;
    ALPC_MESSAGE *reply = m->Reply;
    if (NT_SUCCESS(status) && reply) {
        reply->PortContext = port->Context;
        reply->Sequence = ++port->Sequence;
        if (receive_buffer) {
            SIZE_T room = 0;
            if (length)
                read_caller(&room, length, sizeof(room));
            SIZE_T total = sizeof(ALPC_PORT_HEADER) + reply->DataLength;
            if (length && room < total) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                status = deliver(reply, m->Id, m->CallbackId, receive_buffer, receive_attributes,
                                 (flags & ALPC_MSGFLG_WOW64_CALL) != 0);
                if (NT_SUCCESS(status) && length)
                    write_caller(length, &total, sizeof(total));
            }
        }
    }
    message_free(m);
    ObDereferenceObject(port);
    return status;
}

NTSTATUS NTAPI NtAlpcCancelMessage(HANDLE port_handle, ULONG flags, PVOID context_attribute)
{
    ALPC_CONTEXT_ATTRIBUTE ca;
    NTSTATUS status = read_caller(&ca, context_attribute, sizeof(ca));
    if (!NT_SUCCESS(status))
        return status;
    ALPC_PORT *port;
    status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    ALPC_MESSAGE *m = message_find(ca.MessageId);
    if (!m || m->From != port || m->State == MSG_DONE) {
        status = STATUS_INVALID_PARAMETER;
    } else if (m->State == MSG_QUEUED) {
        RemoveEntryList(&m->Links);
        complete(m, STATUS_CANCELLED);
    }
    ObDereferenceObject(port);
    return status;
}

NTSTATUS NTAPI NtAlpcDisconnectPort(HANDLE port_handle, ULONG flags)
{
    ALPC_PORT *port;
    NTSTATUS status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    if (port->Kind == ALPC_KIND_CONNECTION) {
        port->State = PORT_DISCONNECTED;
        fail_queue(port, STATUS_PORT_CONNECTION_REFUSED);
    } else {
        disconnect(port);
    }
    ObDereferenceObject(port);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ information */

static NTSTATUS find_received(HANDLE port_handle, PVOID message, ALPC_PORT **port, ALPC_MESSAGE **out)
{
    ALPC_PORT_HEADER header;
    NTSTATUS status = read_caller(&header, message, sizeof(header));
    if (!NT_SUCCESS(status))
        return status;
    status = reference_port(port_handle, port);
    if (!NT_SUCCESS(status))
        return status;
    ALPC_MESSAGE *m = message_find(header.MessageId);
    if (!m || m->Receiver != PsCurrentProcess() || m->State != MSG_RECEIVED) {
        ObDereferenceObject(*port);
        return STATUS_ALPC_REPLY_MISMATCH;
    }
    *out = m;
    return STATUS_SUCCESS;
}

static NTSTATUS impersonate(HANDLE port_handle, PVOID message, BOOLEAN container)
{
    ALPC_PORT *port;
    ALPC_MESSAGE *m;
    NTSTATUS status = find_received(port_handle, message, &port, &m);
    if (!NT_SUCCESS(status))
        return status;
    PTOKEN token = m->Token;
    if (container && m->Process) {
        PEPROCESS sender;
        token = nullptr;
        if (NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)m->Process, &sender))) {
            token = SepReferencePrimaryToken(sender);
            ObDereferenceObject(sender);
        }
    } else if (token) {
        ObReferenceObject(token);
    }
    /* measured: a client that allows anonymous impersonation only is refused outright. */
    if (!token)
        status = STATUS_CANNOT_IMPERSONATE;
    else if (!container && m->Level == SecurityAnonymous)
        status = STATUS_ACCESS_DENIED;
    if (NT_SUCCESS(status)) {
        ULONG level = container ? SecurityImpersonation : m->Level;
        PTOKEN copy = SepDuplicateForPipe(token, level, m->EffectiveOnly);
        status = copy ? SepImpersonateClient(PsCurrentThread(), copy, FALSE, m->EffectiveOnly, level)
                      : STATUS_INSUFFICIENT_RESOURCES;
        if (copy)
            ObDereferenceObject(copy);
    }
    if (token)
        ObDereferenceObject(token);
    ObDereferenceObject(port);
    return status;
}

NTSTATUS NTAPI NtAlpcImpersonateClientOfPort(HANDLE port_handle, PVOID message, PVOID flags)
{
    return impersonate(port_handle, message, FALSE);
}

NTSTATUS NTAPI NtAlpcImpersonateClientContainerOfPort(HANDLE port_handle, PVOID message, ULONG flags)
{
    return impersonate(port_handle, message, TRUE);
}

static NTSTATUS open_sender(PHANDLE handle, HANDLE port_handle, PVOID message, ACCESS_MASK access, BOOLEAN thread)
{
    ALPC_PORT *port;
    ALPC_MESSAGE *m;
    NTSTATUS status = find_received(port_handle, message, &port, &m);
    if (!NT_SUCCESS(status))
        return status;
    PVOID object = nullptr;
    status = thread ? PsLookupThreadByThreadId((HANDLE)m->Thread, (PETHREAD *)&object)
                    : PsLookupProcessByProcessId((HANDLE)m->Process, (PEPROCESS *)&object);
    ObDereferenceObject(port);
    if (!NT_SUCCESS(status))
        return STATUS_INVALID_CID;
    HANDLE h;
    status = ObOpenObjectByPointer(object, 0, nullptr, access, thread ? PsThreadTypeObject : PsProcessTypeObject, KernelMode, &h);
    ObDereferenceObject(object);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    return status;
}

NTSTATUS NTAPI NtAlpcOpenSenderProcess(PHANDLE handle, HANDLE port_handle, PVOID message, ULONG flags,
                                       ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return open_sender(handle, port_handle, message, access, FALSE);
}

NTSTATUS NTAPI NtAlpcOpenSenderThread(PHANDLE handle, HANDLE port_handle, PVOID message, ULONG flags,
                                      ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return open_sender(handle, port_handle, message, access, TRUE);
}

NTSTATUS NTAPI NtAlpcQueryInformationMessage(HANDLE port_handle, PVOID message, ULONG klass, PVOID info, ULONG length,
                                             PULONG returned)
{
    ALPC_PORT *port;
    ALPC_MESSAGE *m;
    NTSTATUS status = find_received(port_handle, message, &port, &m);
    if (!NT_SUCCESS(status))
        return status;
    switch (klass) {
    case 0: { /* the sid of the sender. */
        ULONG need = m->Token ? SepSidLength(m->Token->Groups[0].Sid) : 0;
        if (!need)
            status = STATUS_NOT_FOUND;
        else if (length < need)
            status = STATUS_BUFFER_TOO_SMALL;
        else
            status = write_caller(info, m->Token->Groups[0].Sid, need);
        if (returned && need)
            write_caller(returned, &need, sizeof(need));
        break;
    }
    case 1: { /* the modified id of the sender's token. */
        ULONG need = 8;
        status = length < need ? STATUS_BUFFER_TOO_SMALL : write_caller(info, &m->TokenIds[2], need);
        if (returned)
            write_caller(returned, &need, sizeof(need));
        break;
    }
    default:
        KiLogPrintf("alpc: message information class %u is not known", klass);
        status = STATUS_INVALID_INFO_CLASS;
        break;
    }
    ObDereferenceObject(port);
    return status;
}

NTSTATUS NTAPI NtAlpcQueryInformation(HANDLE port_handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    ALPC_PORT *port;
    NTSTATUS status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    ULONG need = 0;
    switch (klass) {
    case 0: { /* AlpcBasicInformation. */
        struct {
            ULONG Flags, SequenceNo;
            PVOID PortContext;
        } basic = {(ULONG)port->Attributes.Flags | (port->Kind == ALPC_KIND_CONNECTION ? ALPC_PORFLG_CONNECTION : 0),
                   port->Sequence, port->Context};
        need = sizeof(basic);
        status = length < need ? STATUS_INFO_LENGTH_MISMATCH : write_caller(info, &basic, need);
        break;
    }
    case 3: { /* AlpcConnectedSIDInformation: who serves a client's port. */
        UCHAR sid[SEP_SID_MAX];
        ALPC_PORT *peer = port->Kind == ALPC_KIND_CLIENT ? port->Peer : nullptr;
        need = peer ? process_sid(peer->Owner, sid) : 0;
        if (!need)
            status = STATUS_INVALID_PARAMETER;
        else
            status = length < need ? STATUS_BUFFER_TOO_SMALL : write_caller(info, sid, need);
        break;
    }
    case 12: { /* AlpcServerSessionInformation. */
        ALPC_PORT *peer = port->Peer;
        PEPROCESS owner = peer ? peer->Owner : port->Owner;
        struct {
            ULONG SessionId, ProcessId;
        } session = {SepTokenSessionId(owner), (ULONG)(ULONG_PTR)owner->UniqueProcessId};
        need = sizeof(session);
        status = length < need ? STATUS_INFO_LENGTH_MISMATCH : write_caller(info, &session, need);
        break;
    }
    default:
        KiLogPrintf("alpc: port information class %u is not known", klass);
        status = STATUS_INVALID_INFO_CLASS;
        break;
    }
    if (returned && need && NT_SUCCESS(status))
        write_caller(returned, &need, sizeof(need));
    ObDereferenceObject(port);
    return status;
}

NTSTATUS NTAPI NtAlpcSetInformation(HANDLE port_handle, ULONG klass, PVOID info, ULONG length)
{
    ALPC_PORT *port;
    NTSTATUS status = reference_port(port_handle, &port);
    if (!NT_SUCCESS(status))
        return status;
    switch (klass) {
    case 1: { /* AlpcPortInformation: new attributes. */
        ALPC_PORT_ATTRIBUTES pa;
        status = length < sizeof(pa) ? STATUS_INFO_LENGTH_MISMATCH : read_caller(&pa, info, sizeof(pa));
        if (NT_SUCCESS(status))
            port->Attributes.Flags = pa.Flags;
        break;
    }
    case 2: { /* AlpcAssociateCompletionPortInformation: the key and the port of every message's packet. */
        struct {
            PVOID Key;
            HANDLE Port;
        } completion;
        status = length < sizeof(completion) ? STATUS_INFO_LENGTH_MISMATCH
                                              : read_caller(&completion, info, sizeof(completion));
        PVOID object = nullptr;
        if (NT_SUCCESS(status))
            status = ObReferenceHandle(completion.Port, IoCompletionObjectType, KeGetPreviousMode(), &object, nullptr);
        if (NT_SUCCESS(status) && port->CompletionPort) {
            ObDereferenceObject(object);
            status = STATUS_INVALID_PARAMETER;
        } else if (NT_SUCCESS(status)) {
            port->CompletionPort = object;
            port->CompletionKey = completion.Key;
            /* messages that came before tell of themselves now. */
            for (LIST_ENTRY *e = port->Queue.Flink; e != &port->Queue; e = e->Flink)
                IopQueueCompletion(object, completion.Key, nullptr, STATUS_SUCCESS, 0);
        }
        break;
    }
    case 8: /* AlpcAdjustCompletionListConcurrencyCountInformation. */
        status = STATUS_SUCCESS;
        break;
    default:
        /* completion lists are not kept: rpcrt4 then goes on receiving one message at a time. */
        KiLogPrintf("alpc: setting port information class %u is not done", klass);
        status = STATUS_NOT_SUPPORTED;
        break;
    }
    ObDereferenceObject(port);
    return status;
}
