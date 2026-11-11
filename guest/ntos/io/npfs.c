/*
 * the named pipe file system, \Device\NamedPipe: instances with a server
 * and a client end, byte and message streams, listening, peeking and
 * waiting for an instance.
 *
 * reads, listens and pipe waits can stay pending: a request records
 * where its caller wants the result, and whoever satisfies it later
 * copies into the caller's address space, writes its i/o status block,
 * signals its event and posts its completion packet. synchronous callers
 * sleep until their request completes. writes are buffered without a
 * quota and always complete at once, unlike windows, which blocks
 * writers of a full pipe.
 */
#include "../ntos.h"

#define STATUS_PIPE_NOT_AVAILABLE ((NTSTATUS)0xC00000ACL)
#define STATUS_INVALID_PIPE_STATE ((NTSTATUS)0xC00000ADL)
#define STATUS_PIPE_BUSY ((NTSTATUS)0xC00000AEL)
#define STATUS_PIPE_DISCONNECTED ((NTSTATUS)0xC00000B0L)
#define STATUS_PIPE_CLOSING ((NTSTATUS)0xC00000B1L)
#define STATUS_PIPE_CONNECTED ((NTSTATUS)0xC00000B2L)
#define STATUS_PIPE_LISTENING ((NTSTATUS)0xC00000B3L)
#define STATUS_INVALID_READ_MODE ((NTSTATUS)0xC00000B4L)
#define STATUS_PIPE_EMPTY ((NTSTATUS)0xC00000D9L)
#define STATUS_PIPE_BROKEN ((NTSTATUS)0xC000014BL)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5L)
#define STATUS_CANCELLED ((NTSTATUS)0xC0000120L)
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005L)
#define STATUS_INSTANCE_NOT_AVAILABLE ((NTSTATUS)0xC00000ABL)
#define STATUS_ILLEGAL_FUNCTION ((NTSTATUS)0xC00000AFL)
#define FILE_DEVICE_NAMED_PIPE 0x00000011

#define FSCTL_PIPE_DISCONNECT 0x00110004
#define FSCTL_PIPE_LISTEN 0x00110008
#define FSCTL_PIPE_PEEK 0x0011400C
#define FSCTL_PIPE_WAIT 0x00110018
#define FSCTL_PIPE_IMPERSONATE 0x0011001C
#define FSCTL_PIPE_TRANSCEIVE 0x0011C017
#define FSCTL_PIPE_GET_PIPE_ATTRIBUTE 0x00110028
#define FSCTL_PIPE_GET_CONNECTION_ATTRIBUTE 0x00110030

#define FILE_PIPE_BYTE_STREAM_TYPE 0
#define FILE_PIPE_MESSAGE_TYPE 1
#define FILE_PIPE_BYTE_STREAM_MODE 0
#define FILE_PIPE_MESSAGE_MODE 1
#define FILE_PIPE_QUEUE_OPERATION 0
#define FILE_PIPE_COMPLETE_OPERATION 1
#define FILE_PIPE_DISCONNECTED_STATE 1
#define FILE_PIPE_LISTENING_STATE 2
#define FILE_PIPE_CONNECTED_STATE 3
#define FILE_PIPE_CLOSING_STATE 4
#define FILE_PIPE_CLIENT_END 0
#define FILE_PIPE_SERVER_END 1

#define END_SERVER ((PVOID)1)
#define END_CLIENT ((PVOID)2)
#define END_ROOT ((PVOID)3)

typedef struct NP_DATA {
    LIST_ENTRY Links;
    ULONG Length;
    ULONG Offset; /* bytes of the message already read. */
    UCHAR Data[];
} NP_DATA;

/* what one end reads: the other end's data, and the reads waiting for it. */
typedef struct NP_QUEUE {
    LIST_ENTRY Data;
    ULONG Bytes;
    ULONG Messages;
    LIST_ENTRY Readers;
    BOOLEAN Consumed; /* something was read since the instance connected. */
} NP_QUEUE;

typedef struct NP_PIPE {
    LIST_ENTRY Links;
    UNICODE_STRING Name;
    ULONG Type;
    ULONG MaxInstances;
    ULONG Instances;
    ULONG InQuota;
    ULONG OutQuota;
    LONGLONG DefaultTimeout;
    LIST_ENTRY InstanceList;
    SD_BLOB *Sd; /* the pipe's descriptor, shared by its instances. */
} NP_PIPE;

typedef struct NP_INSTANCE {
    LIST_ENTRY Links;
    NP_PIPE *Pipe;
    ULONG State;
    BOOLEAN ServerOpen;
    BOOLEAN ClientOpen;
    PFILE_OBJECT Client;
    NP_QUEUE ToServer;
    NP_QUEUE ToClient;
    LIST_ENTRY Listeners;
    ULONG ReadMode[2];       /* by end: server, client. */
    ULONG CompletionMode[2];
    ULONG ProcessId[2];      /* the process that opened each end. */
    struct _TOKEN *ClientToken; /* the identity the client end acts with, referenced. */
    ULONG ClientLevel;          /* the impersonation level the client opened with. */
    BOOLEAN ClientEffectiveOnly;
    BOOLEAN ClientImpersonating; /* ClientToken is a thread's, not the process's. */
    ULONG Host; /* the host socket that serves the instance in place of a server end, 0 for none. */
} NP_INSTANCE;

typedef struct NP_REQUEST {
    LIST_ENTRY Links;
    PEPROCESS Process;
    PETHREAD Thread;
    PVOID ApcRoutine;
    PFILE_OBJECT File;
    PKEVENT Event;
    PVOID ApcContext;
    PIO_STATUS_BLOCK Iosb;
    PVOID Buffer;
    ULONG Length;
    BOOLEAN Waiter; /* a synchronous caller sleeps on Done and frees the request. */
    BOOLEAN Immediate; /* completed before its call returned. */
    BOOLEAN Completed; /* set under the lock once it left the lists. */
    KEVENT Done;
    NTSTATUS Status;
    ULONG_PTR Information;
} NP_REQUEST;

/* the arguments NtCreateNamedPipeFile passes to its thread's create request. */
typedef struct NP_CREATE {
    ULONG Type;
    ULONG ReadMode;
    ULONG CompletionMode;
    ULONG MaxInstances;
    ULONG InQuota;
    ULONG OutQuota;
    LONGLONG Timeout;
    ULONG Disposition;
} NP_CREATE;

static PDEVICE_OBJECT g_device;
static FAST_MUTEX g_lock; /* almost always uncontended, since the kernel lock is held. */
static KMUTEX g_create_lock;
static LIST_ENTRY g_pipes;
static LIST_ENTRY g_host_pipes; /* the pipes to discord on the host, one instance each, not found by name. */
/* host arguments live in the kernel image, which is physically contiguous. */
static UCHAR g_host_buffer[16384] __attribute__((aligned(64)));
static MimeNetPoll g_host_poll __attribute__((aligned(16)));
static NP_CREATE *volatile g_create_owner_params;
static PKTHREAD volatile g_create_owner;

static void lock(void)
{
    ExAcquireFastMutex(&g_lock);
}

static void unlock(void)
{
    ExReleaseFastMutex(&g_lock);
}

static void init_queue(NP_QUEUE *q)
{
    InitializeListHead(&q->Data);
    InitializeListHead(&q->Readers);
    q->Bytes = q->Messages = 0;
}

static void flush_queue(NP_QUEUE *q)
{
    while (!IsListEmpty(&q->Data))
        ExFreePool(CONTAINING_RECORD(RemoveHeadList(&q->Data), NP_DATA, Links));
    q->Bytes = q->Messages = 0;
}

static ULONG end_index(PFILE_OBJECT file)
{
    return file->FsContext2 == END_SERVER ? 0 : 1;
}

static NP_QUEUE *read_queue(NP_INSTANCE *in, PFILE_OBJECT file)
{
    return file->FsContext2 == END_SERVER ? &in->ToServer : &in->ToClient;
}

static NP_QUEUE *write_queue(NP_INSTANCE *in, PFILE_OBJECT file)
{
    return file->FsContext2 == END_SERVER ? &in->ToClient : &in->ToServer;
}

/* ------------------------------------------------------------ requests */

static NTSTATUS copy_to_process(PEPROCESS process, PVOID dst, const void *src, SIZE_T length)
{
    return IopCopyToProcess(process, dst, src, length);
}

static NP_REQUEST *new_request(PFILE_OBJECT file, HANDLE event, PVOID apc_routine, PVOID apc_context,
                               PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, NTSTATUS *status)
{
    NP_REQUEST *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), KI_TAG('N', 'p', 'R', 'q'));
    if (!r) {
        *status = STATUS_INSUFFICIENT_RESOURCES;
        return nullptr;
    }
    memset(r, 0, sizeof(*r));
    if (event) {
        *status = ObReferenceHandleFor(event, EVENT_MODIFY_STATE, ExEventType, KeGetPreviousMode(), &r->Event);
        if (!NT_SUCCESS(*status)) {
            ExFreePool(r);
            return nullptr;
        }
        KeClearEvent(r->Event);
    }
    r->Process = PsCurrentProcess();
    ObReferenceObject(r->Process);
    r->Thread = PsCurrentThread();
    ObReferenceObject(r->Thread);
    r->ApcRoutine = apc_routine;
    r->File = file;
    ObReferenceObject(file);
    r->ApcContext = apc_context;
    r->Iosb = iosb;
    r->Buffer = buffer;
    r->Length = length;
    r->Waiter = (file->Flags & FO_SYNCHRONOUS_IO) != 0;
    KeInitializeEvent(&r->Done, NotificationEvent, FALSE);
    *status = STATUS_SUCCESS;
    return r;
}

static void free_request(NP_REQUEST *r)
{
    if (r->Event)
        ObDereferenceObject(r->Event);
    ObDereferenceObject(r->File);
    ObDereferenceObject(r->Thread);
    ObDereferenceObject(r->Process);
    ExFreePool(r);
}

/*
 * a read or write lives on the call's stack until it has to wait: most complete before returning, with nothing
 * allocated or referenced but the caller's event. a request that goes pending is copied to the pool with references
 * that keep what it names alive.
 */
static BOOLEAN init_call(NP_REQUEST *r, PFILE_OBJECT file, HANDLE event, PVOID apc_routine, PVOID apc_context,
                         PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, NTSTATUS *status)
{
    memset(r, 0, offsetof(NP_REQUEST, Done));
    if (event) {
        *status = ObReferenceHandleFor(event, EVENT_MODIFY_STATE, ExEventType, KeGetPreviousMode(), &r->Event);
        if (!NT_SUCCESS(*status))
            return FALSE;
        KeClearEvent(r->Event);
    }
    r->Process = PsCurrentProcess();
    r->Thread = PsCurrentThread();
    r->ApcRoutine = apc_routine;
    r->File = file;
    r->ApcContext = apc_context;
    r->Iosb = iosb;
    r->Buffer = buffer;
    r->Length = length;
    r->Waiter = (file->Flags & FO_SYNCHRONOUS_IO) != 0;
    return TRUE;
}

static void finish_request(NP_REQUEST *r, NTSTATUS status, ULONG_PTR information);

/* ends a call on the stack: its status is also the system call's. */
static NTSTATUS call_done(NP_REQUEST *r, NTSTATUS status, ULONG_PTR information)
{
    r->Immediate = TRUE;
    finish_request(r, status, information);
    if (r->Event)
        ObDereferenceObject(r->Event);
    return status;
}

/* the pooled copy of a call that has to wait, which now owns the event. */
static NP_REQUEST *call_to_pool(NP_REQUEST *call)
{
    NP_REQUEST *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), KI_TAG('N', 'p', 'R', 'q'));
    if (!r)
        return nullptr;
    memcpy(r, call, offsetof(NP_REQUEST, Done));
    call->Event = nullptr;
    ObReferenceObject(r->Process);
    ObReferenceObject(r->Thread);
    ObReferenceObject(r->File);
    KeInitializeEvent(&r->Done, NotificationEvent, FALSE);
    return r;
}

/* reports to the caller: status block, event and completion port. like windows, an error that ended the request
   before it went pending skips them. */
static void finish_request(NP_REQUEST *r, NTSTATUS status, ULONG_PTR information)
{
    r->Status = status;
    r->Information = information;
    if (r->Iosb) {
        ULONG64 image[2];
        PVOID address;
        ULONG length = IopIosbImage(r->Iosb, status, information, &address, image);
        copy_to_process(r->Process, address, image, length);
    }
    if (r->Event)
        KeSetEvent(r->Event, IO_NO_INCREMENT, FALSE);
    IopSignalFile(r->File);
    if (!r->Immediate || !NT_ERROR(status)) {
        if (r->ApcRoutine)
            IopQueueCompletionApc(r->Thread, r->ApcRoutine, r->ApcContext, r->Iosb);
        else
            IopPostCompletion(r->File, r->ApcContext, status, information, r->Immediate);
    }
}

/* completes a pending request: a waiter frees it itself, otherwise it is freed here. */
static void complete_request(NP_REQUEST *r, NTSTATUS status, ULONG_PTR information)
{
    r->Completed = TRUE;
    finish_request(r, status, information);
    if (r->Waiter)
        KeSetEvent(&r->Done, IO_NO_INCREMENT, FALSE);
    else
        free_request(r);
}

/* ends an immediate request: its status is also the system call's. */
static NTSTATUS immediate(NP_REQUEST *r, NTSTATUS status, ULONG_PTR information)
{
    r->Immediate = TRUE;
    finish_request(r, status, information);
    free_request(r);
    return status;
}

/* called with the lock held and releases it: sleeps for a synchronous caller, or returns pending. the request
   may complete and be freed as soon as the lock is released. */
static NTSTATUS pending(NP_REQUEST *r)
{
    BOOLEAN waiter = r->Waiter;
    unlock();
    if (!waiter)
        return STATUS_PENDING;
    if (KeWaitForSingleObject(&r->Done, UserRequest, UserMode, FALSE, nullptr) != STATUS_WAIT_0) {
        /* the thread is ending, and a pipe wait must not hold that up: it leaves. */
        lock();
        if (!r->Completed) {
            RemoveEntryList(&r->Links);
            complete_request(r, STATUS_CANCELLED, 0);
        }
        unlock();
        KeWaitForSingleObject(&r->Done, Executive, KernelMode, FALSE, nullptr);
    }
    NTSTATUS status = r->Status;
    free_request(r);
    return status;
}

static void fail_all(LIST_ENTRY *list, NTSTATUS status)
{
    while (!IsListEmpty(list))
        complete_request(CONTAINING_RECORD(RemoveHeadList(list), NP_REQUEST, Links), status, 0);
}

/* for the thread dump: a queue with data and a waiting reader means a lost wake. */
void NpfsDumpState(void)
{
    ULONG instances = 0, waiting = 0;
    lock();
    for (PLIST_ENTRY l = g_pipes.Flink; l != &g_pipes; l = l->Flink) {
        NP_PIPE *p = CONTAINING_RECORD(l, NP_PIPE, Links);
        for (PLIST_ENTRY i = p->InstanceList.Flink; i != &p->InstanceList; i = i->Flink) {
            NP_INSTANCE *in = CONTAINING_RECORD(i, NP_INSTANCE, Links);
            NP_QUEUE *queues[2] = {&in->ToServer, &in->ToClient};
            instances++;
            for (ULONG k = 0; k < 2; k++) {
                /* each queue is read by the end it is named for: ToServer by the server. */
                BOOLEAN reader = !IsListEmpty(&queues[k]->Readers);
                waiting += reader;
                if (!IsListEmpty(&queues[k]->Data))
                    KiLogPrintf("npfs: %s end of %.*ws (process %u) has %u bytes in %u messages unread%s",
                                k ? "client" : "server", p->Name.Length / 2, p->Name.Buffer, in->ProcessId[k ? 1 : 0],
                                queues[k]->Bytes, queues[k]->Messages, reader ? ", and a read waiting for them" : ", no read is waiting");
            }
        }
    }
    unlock();
    KiLogPrintf("npfs: %u instances, %u queues with a reader waiting", instances, waiting);
}

/* ------------------------------------------------------------ data */

/* moves data from q into a reader's buffer; returns the read status and the bytes moved. */
static NTSTATUS take_data(NP_QUEUE *q, ULONG mode, PEPROCESS process, PUCHAR buffer, ULONG length, ULONG *moved)
{
    ULONG done = 0;
    NTSTATUS status = STATUS_SUCCESS;
    if (!IsListEmpty(&q->Data))
        q->Consumed = TRUE;
    while (!IsListEmpty(&q->Data) && done < length) {
        NP_DATA *d = CONTAINING_RECORD(q->Data.Flink, NP_DATA, Links);
        ULONG n = d->Length - d->Offset;
        if (n > length - done)
            n = length - done;
        if (!NT_SUCCESS(copy_to_process(process, buffer + done, d->Data + d->Offset, n)))
            return STATUS_ACCESS_VIOLATION;
        done += n;
        d->Offset += n;
        q->Bytes -= n;
        if (d->Offset == d->Length) {
            RemoveEntryList(&d->Links);
            ExFreePool(d);
            q->Messages--;
            if (mode == FILE_PIPE_MESSAGE_MODE)
                break;
        } else if (mode == FILE_PIPE_MESSAGE_MODE) {
            /* the rest of the message stays for the next read. */
            status = STATUS_BUFFER_OVERFLOW;
            break;
        }
    }
    /* an empty message completes a message mode read with no bytes. */
    if (mode == FILE_PIPE_MESSAGE_MODE && !done && !IsListEmpty(&q->Data)) {
        NP_DATA *d = CONTAINING_RECORD(q->Data.Flink, NP_DATA, Links);
        if (d->Length == 0) {
            RemoveEntryList(&d->Links);
            ExFreePool(d);
            q->Messages--;
        }
    }
    *moved = done;
    return status;
}

static void serve_readers(NP_INSTANCE *in, NP_QUEUE *q, ULONG mode)
{
    while (!IsListEmpty(&q->Readers) && !IsListEmpty(&q->Data)) {
        NP_REQUEST *r = CONTAINING_RECORD(RemoveHeadList(&q->Readers), NP_REQUEST, Links);
        ULONG moved;
        NTSTATUS status = take_data(q, mode, r->Process, r->Buffer, r->Length, &moved);
        complete_request(r, status, moved);
    }
}

static ULONG reader_mode(NP_INSTANCE *in, NP_QUEUE *q)
{
    return in->ReadMode[q == &in->ToServer ? 0 : 1];
}

/* ------------------------------------------------------------ pipes and instances */

static NP_PIPE *find_pipe(PCUNICODE_STRING name)
{
    for (PLIST_ENTRY l = g_pipes.Flink; l != &g_pipes; l = l->Flink) {
        NP_PIPE *p = CONTAINING_RECORD(l, NP_PIPE, Links);
        if (RtlEqualUnicodeString(&p->Name, name, TRUE))
            return p;
    }
    return nullptr;
}

static NP_INSTANCE *listening_instance(NP_PIPE *p)
{
    for (PLIST_ENTRY l = p->InstanceList.Flink; l != &p->InstanceList; l = l->Flink) {
        NP_INSTANCE *in = CONTAINING_RECORD(l, NP_INSTANCE, Links);
        if (in->State == FILE_PIPE_LISTENING_STATE && in->ServerOpen)
            return in;
    }
    return nullptr;
}

static void release_instance(NP_INSTANCE *in)
{
    if (in->ServerOpen || in->ClientOpen)
        return;
    flush_queue(&in->ToServer);
    flush_queue(&in->ToClient);
    if (in->ClientToken)
        ObDereferenceObject(in->ClientToken);
    NP_PIPE *p = in->Pipe;
    RemoveEntryList(&in->Links);
    ExFreePool(in);
    if (--p->Instances == 0) {
        RemoveEntryList(&p->Links);
        SeSdDereference(p->Sd);
        ExFreePool(p->Name.Buffer);
        ExFreePool(p);
    }
}

/* without its leading backslash. */
static void pipe_name(PFILE_OBJECT file, PUNICODE_STRING name)
{
    *name = file->FileName;
    while (name->Length && name->Buffer[0] == '\\') {
        name->Buffer++;
        name->Length -= sizeof(WCHAR);
        name->MaximumLength -= sizeof(WCHAR);
    }
}

#ifndef FILE_CREATE_PIPE_INSTANCE
#define FILE_CREATE_PIPE_INSTANCE 0x0004
#endif

/*
 * a program opening a pipe is held to its descriptor: a client to the rights it asks for, which its handle gets, and
 * a server creating another instance to the right to create one.
 */
static NTSTATUS check_pipe(PIO_STACK_LOCATION stack, NP_PIPE *p, ACCESS_MASK asked, BOOLEAN client)
{
    PIO_SECURITY_CONTEXT context = stack->Parameters.Create.SecurityContext;
    if (!(stack->Flags & SL_FORCE_ACCESS_CHECK) || !p->Sd || !context)
        return STATUS_SUCCESS;
    ACCESS_MASK granted;
    NTSTATUS status = SeCheckAccess(p->Sd, IoFileObjectTypeObject->Init.GenericMapping, asked, 0, 0, &granted);
    if (NT_SUCCESS(status) && client) {
        context->GrantedAccess = granted;
        context->Checked = TRUE;
    }
    return status;
}

static NTSTATUS create_server(PFILE_OBJECT file, const NP_CREATE *c, SD_BLOB *creator, PIO_STACK_LOCATION stack)
{
    UNICODE_STRING name;
    pipe_name(file, &name);
    if (!name.Length)
        return STATUS_OBJECT_NAME_INVALID;
    NP_PIPE *p = find_pipe(&name);
    if (p) {
        if (c->Disposition == FILE_CREATE)
            return STATUS_ACCESS_DENIED;
        if (p->Instances >= p->MaxInstances)
            return STATUS_INSTANCE_NOT_AVAILABLE;
        if (c->Type != p->Type)
            return STATUS_ACCESS_DENIED;
        NTSTATUS allowed = check_pipe(stack, p, FILE_CREATE_PIPE_INSTANCE, FALSE);
        if (!NT_SUCCESS(allowed))
            return allowed;
    } else {
        if (c->Disposition == FILE_OPEN)
            return STATUS_OBJECT_NAME_NOT_FOUND;
        p = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*p), KI_TAG('N', 'p', 'P', 'p'));
        if (!p)
            return STATUS_INSUFFICIENT_RESOURCES;
        memset(p, 0, sizeof(*p));
        p->Name.Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, name.Length + sizeof(WCHAR), KI_TAG('N', 'p', 'N', 'm'));
        if (!p->Name.Buffer) {
            ExFreePool(p);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        /* the creator's descriptor, else the token's own. */
        SD_BLOB *sd;
        SD_MAPPING mapping = IoFileObjectTypeObject->Init.GenericMapping;
        NTSTATUS status = creator ? SeSdAssign(nullptr, creator, mapping, FALSE, KeGetPreviousMode(), &sd)
                                  : SeSdDefault(mapping, FALSE, &sd);
        if (!NT_SUCCESS(status) && status != STATUS_NO_TOKEN) {
            ExFreePool(p->Name.Buffer);
            ExFreePool(p);
            return status;
        }
        p->Sd = NT_SUCCESS(status) ? sd : nullptr;
        memcpy(p->Name.Buffer, name.Buffer, name.Length);
        p->Name.Length = name.Length;
        p->Name.MaximumLength = name.Length + sizeof(WCHAR);
        p->Type = c->Type;
        p->MaxInstances = c->MaxInstances ? c->MaxInstances : 1;
        p->InQuota = c->InQuota;
        p->OutQuota = c->OutQuota;
        p->DefaultTimeout = c->Timeout;
        InitializeListHead(&p->InstanceList);
        InsertTailList(&g_pipes, &p->Links);
    }
    NP_INSTANCE *in = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*in), KI_TAG('N', 'p', 'I', 'n'));
    if (!in) {
        if (!p->Instances) {
            RemoveEntryList(&p->Links);
            SeSdDereference(p->Sd);
            ExFreePool(p->Name.Buffer);
            ExFreePool(p);
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memset(in, 0, sizeof(*in));
    in->Pipe = p;
    in->State = FILE_PIPE_LISTENING_STATE;
    in->ServerOpen = TRUE;
    init_queue(&in->ToServer);
    init_queue(&in->ToClient);
    InitializeListHead(&in->Listeners);
    in->ReadMode[0] = c->ReadMode;
    in->ReadMode[1] = FILE_PIPE_BYTE_STREAM_MODE;
    in->CompletionMode[0] = c->CompletionMode;
    in->ProcessId[0] = (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId;
    InsertTailList(&p->InstanceList, &in->Links);
    p->Instances++;
    file->FsContext = in;
    file->FsContext2 = END_SERVER;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------ discord on the host */

#define WSAEWOULDBLOCK 10035

/*
 * a client of \\.\pipe\discord-ipc-N that no program of the machine serves reaches the host's discord-ipc-N socket,
 * so games talk to the Discord the user runs. the host stands for the server end: what the client writes is sent
 * to it and what it answers is queued for the client.
 */
static BOOLEAN discord_index(PCUNICODE_STRING name, ULONG *index)
{
    static const char prefix[] = "discord-ipc-";
    ULONG n = sizeof(prefix) - 1;
    if (name->Length != (n + 1) * sizeof(WCHAR))
        return FALSE;
    for (ULONG i = 0; i < n; i++) {
        WCHAR c = name->Buffer[i];
        if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != (WCHAR)prefix[i])
            return FALSE;
    }
    WCHAR digit = name->Buffer[n];
    if (digit < '0' || digit > '9')
        return FALSE;
    *index = digit - '0';
    return TRUE;
}

/* returns 0 or the winsock error. */
static LONG host_call(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 *value)
{
    ULONG64 v = 0;
    ULONG64 hs = HalHypercallUnlocked(nr, a1, a2, a3, 0, 0, 0, &v);
    if (value)
        *value = v;
    if (hs == MIME_HC_STATUS_OK)
        return 0;
    return hs == MIME_HC_STATUS_NET_ERROR ? (LONG)v : 10022;
}

/* the lock is held. the client may still read what came, then sees the pipe broken. */
static void host_close(NP_INSTANCE *in)
{
    host_call(MIME_HC_NET_CLOSE, in->Host, 0, 0, nullptr);
    in->Host = 0;
    in->ServerOpen = FALSE;
    flush_queue(&in->ToServer);
    if (in->ClientOpen) {
        in->State = FILE_PIPE_CLOSING_STATE;
        if (IsListEmpty(&in->ToClient.Data))
            fail_all(&in->ToClient.Readers, STATUS_PIPE_BROKEN);
    }
}

/* the lock is held: sends what the client wrote, queues what the host answered, then watches for more. */
static void host_pump(NP_INSTANCE *in)
{
    BOOLEAN broken = FALSE, blocked = FALSE;
    NP_QUEUE *out = &in->ToServer, *back = &in->ToClient;
    while (!IsListEmpty(&out->Data)) {
        NP_DATA *d = CONTAINING_RECORD(out->Data.Flink, NP_DATA, Links);
        ULONG n = d->Length - d->Offset;
        if (n > sizeof(g_host_buffer))
            n = sizeof(g_host_buffer);
        memcpy(g_host_buffer, d->Data + d->Offset, n);
        ULONG64 sent = 0;
        LONG e = n ? host_call(MIME_HC_NET_SEND, in->Host, HalPhysicalOf(g_host_buffer), n, &sent) : 0;
        if (e == WSAEWOULDBLOCK) {
            blocked = TRUE;
            break;
        }
        if (e) {
            broken = TRUE;
            break;
        }
        d->Offset += (ULONG)sent;
        out->Bytes -= (ULONG)sent;
        if (d->Offset == d->Length) {
            RemoveEntryList(&d->Links);
            ExFreePool(d);
            out->Messages--;
        }
    }
    while (!broken) {
        ULONG64 got = 0;
        LONG e = host_call(MIME_HC_NET_RECV, in->Host, HalPhysicalOf(g_host_buffer), sizeof(g_host_buffer), &got);
        if (e == WSAEWOULDBLOCK)
            break;
        NP_DATA *d = !e && got ? ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NP_DATA) + got, KI_TAG('N', 'p', 'D', 't'))
                               : nullptr;
        if (!d) {
            broken = TRUE;
            break;
        }
        memcpy(d->Data, g_host_buffer, got);
        d->Length = (ULONG)got;
        d->Offset = 0;
        InsertTailList(&back->Data, &d->Links);
        back->Bytes += (ULONG)got;
        back->Messages++;
    }
    serve_readers(in, back, in->ReadMode[1]);
    if (broken) {
        host_close(in);
        return;
    }
    g_host_poll.socket = in->Host;
    g_host_poll.events = MIME_NET_READ | (blocked ? MIME_NET_WRITE : 0);
    g_host_poll.revents = 0;
    g_host_poll.error = 0;
    host_call(MIME_HC_NET_POLL, HalPhysicalOf(&g_host_poll), 1, 1, nullptr);
}

static NTSTATUS open_host(PFILE_OBJECT file, PCUNICODE_STRING name, ULONG index)
{
    ULONG64 sock = 0;
    if (host_call(MIME_HC_NET_IPC, index, 0, 0, &sock) || !sock)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    NP_PIPE *p = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*p), KI_TAG('N', 'p', 'P', 'p'));
    NP_INSTANCE *in = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*in), KI_TAG('N', 'p', 'I', 'n'));
    PWSTR text = ExAllocatePool2(POOL_FLAG_NON_PAGED, name->Length + sizeof(WCHAR), KI_TAG('N', 'p', 'N', 'm'));
    if (!p || !in || !text) {
        if (p)
            ExFreePool(p);
        if (in)
            ExFreePool(in);
        if (text)
            ExFreePool(text);
        host_call(MIME_HC_NET_CLOSE, sock, 0, 0, nullptr);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memset(p, 0, sizeof(*p));
    memcpy(text, name->Buffer, name->Length);
    p->Name = (UNICODE_STRING){name->Length, (USHORT)(name->Length + sizeof(WCHAR)), text};
    p->Type = FILE_PIPE_BYTE_STREAM_TYPE;
    p->MaxInstances = 1;
    p->Instances = 1;
    p->InQuota = p->OutQuota = sizeof(g_host_buffer);
    p->DefaultTimeout = -500000;
    InitializeListHead(&p->InstanceList);
    InsertTailList(&g_host_pipes, &p->Links);
    memset(in, 0, sizeof(*in));
    in->Pipe = p;
    in->State = FILE_PIPE_CONNECTED_STATE;
    in->ServerOpen = TRUE;
    in->ClientOpen = TRUE;
    in->Client = file;
    init_queue(&in->ToServer);
    init_queue(&in->ToClient);
    InitializeListHead(&in->Listeners);
    in->ProcessId[1] = (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId;
    in->Host = (ULONG)sock;
    InsertTailList(&p->InstanceList, &in->Links);
    file->FsContext = in;
    file->FsContext2 = END_CLIENT;
    host_pump(in);
    return STATUS_SUCCESS;
}

BOOLEAN NpfsHostEvent(ULONG sock)
{
    BOOLEAN mine = FALSE;
    lock();
    for (PLIST_ENTRY l = g_host_pipes.Flink; l != &g_host_pipes && !mine; l = l->Flink) {
        NP_PIPE *p = CONTAINING_RECORD(l, NP_PIPE, Links);
        NP_INSTANCE *in = CONTAINING_RECORD(p->InstanceList.Flink, NP_INSTANCE, Links);
        if (in->Host == sock) {
            mine = TRUE;
            host_pump(in);
        }
    }
    unlock();
    return mine;
}

static NTSTATUS open_client(PFILE_OBJECT file, PIO_STACK_LOCATION stack)
{
    UNICODE_STRING name;
    pipe_name(file, &name);
    if (!name.Length) {
        file->FsContext2 = END_ROOT;
        return STATUS_SUCCESS;
    }
    NP_PIPE *p = find_pipe(&name);
    ULONG index;
    if (!p && discord_index(&name, &index))
        return open_host(file, &name, index);
    if (!p)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    PIO_SECURITY_CONTEXT context = stack->Parameters.Create.SecurityContext;
    /* a client does not create instances, so the right to is not held against it when it asks to write. */
    ACCESS_MASK asked = context ? ObMapGenericAccess(IoFileObjectTypeObject, context->DesiredAccess) : 0;
    NTSTATUS allowed = check_pipe(stack, p, asked & ~(ACCESS_MASK)FILE_CREATE_PIPE_INSTANCE, TRUE);
    if (!NT_SUCCESS(allowed))
        return allowed;
    /* the handle still has the append right it asked for, the write end of CreatePipe is 0x120196 on windows. */
    if (context && context->Checked)
        context->GrantedAccess |= asked & FILE_CREATE_PIPE_INSTANCE;
    NP_INSTANCE *in = listening_instance(p);
    if (!in)
        return STATUS_PIPE_NOT_AVAILABLE;
    in->State = FILE_PIPE_CONNECTED_STATE;
    in->ClientOpen = TRUE;
    in->Client = file;
    in->ReadMode[1] = FILE_PIPE_BYTE_STREAM_MODE;
    in->CompletionMode[1] = FILE_PIPE_QUEUE_OPERATION;
    in->ProcessId[1] = (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId;
    /* a server impersonates the identity the client opened with, at the level it allowed. */
    PETHREAD client = PsCurrentThread();
    in->ClientToken = SepReferenceEffectiveToken(client);
    in->ClientImpersonating = client->ImpersonationToken != nullptr;
    in->ClientLevel = client->CreateQos ? client->CreateQos - 1u : 2u; /* impersonation unless the client said otherwise. */
    in->ClientEffectiveOnly = client->CreateQos && client->CreateQosEffective;
    file->FsContext = in;
    file->FsContext2 = END_CLIENT;
    while (!IsListEmpty(&in->Listeners))
        complete_request(CONTAINING_RECORD(RemoveHeadList(&in->Listeners), NP_REQUEST, Links), STATUS_SUCCESS, 0);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------ dispatch */

static NTSTATUS complete_irp(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS NTAPI np_create(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PFILE_OBJECT file = stack->FileObject;
    lock();
    NTSTATUS status;
    if (g_create_owner == KiCurrentThread() && g_create_owner_params)
        status = create_server(file, g_create_owner_params,
                               stack->Parameters.Create.SecurityContext ? stack->Parameters.Create.SecurityContext->SecurityDescriptor : nullptr,
                               stack);
    else
        status = open_client(file, stack);
    unlock();
    return complete_irp(irp, status, NT_SUCCESS(status) ? FILE_OPENED : 0);
}

/* the descriptor belongs to the pipe, not the end, and the root has none. */
static NTSTATUS NTAPI np_query_security(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    NP_INSTANCE *in = file->FsContext;
    if (!in || file->FsContext2 == END_ROOT)
        return complete_irp(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    lock();
    SD_BLOB *sd = in->Pipe->Sd ? SeSdReference(in->Pipe->Sd) : nullptr;
    unlock();
    return complete_irp(irp, STATUS_SUCCESS, (ULONG_PTR)sd);
}

static NTSTATUS NTAPI np_set_security(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    NP_INSTANCE *in = stack->FileObject->FsContext;
    if (!in || stack->FileObject->FsContext2 == END_ROOT)
        return complete_irp(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    lock();
    SD_BLOB *old = in->Pipe->Sd;
    in->Pipe->Sd = SeSdReference(stack->Parameters.Others.Argument1);
    unlock();
    SeSdDereference(old);
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI np_cleanup(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    NP_INSTANCE *in = file->FsContext;
    lock();
    if (in && file->FsContext2 == END_SERVER && in->ServerOpen) {
        in->ServerOpen = FALSE;
        fail_all(&in->ToServer.Readers, STATUS_PIPE_BROKEN);
        fail_all(&in->Listeners, STATUS_PIPE_BROKEN);
        /* the client may still read what was written, then sees the pipe broken. */
        if (in->ClientOpen) {
            in->State = FILE_PIPE_CLOSING_STATE;
            if (IsListEmpty(&in->ToClient.Data))
                fail_all(&in->ToClient.Readers, STATUS_PIPE_BROKEN);
        }
        release_instance(in);
    } else if (in && file->FsContext2 == END_CLIENT && in->ClientOpen && in->Client == file) {
        in->ClientOpen = FALSE;
        in->Client = nullptr;
        fail_all(&in->ToClient.Readers, STATUS_PIPE_BROKEN);
        if (in->Host)
            host_close(in);
        if (in->ServerOpen) {
            in->State = FILE_PIPE_CLOSING_STATE;
            if (IsListEmpty(&in->ToServer.Data))
                fail_all(&in->ToServer.Readers, STATUS_PIPE_BROKEN);
        }
        release_instance(in);
    }
    file->FsContext = nullptr;
    unlock();
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI np_close(PDEVICE_OBJECT device, PIRP irp)
{
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

typedef struct FILE_PIPE_INFORMATION {
    ULONG ReadMode;
    ULONG CompletionMode;
} FILE_PIPE_INFORMATION;

typedef struct FILE_PIPE_LOCAL_INFORMATION {
    ULONG NamedPipeType;
    ULONG NamedPipeConfiguration;
    ULONG MaximumInstances;
    ULONG CurrentInstances;
    ULONG InboundQuota;
    ULONG ReadDataAvailable;
    ULONG OutboundQuota;
    ULONG WriteQuotaAvailable;
    ULONG NamedPipeState;
    ULONG NamedPipeEnd;
} FILE_PIPE_LOCAL_INFORMATION;

static NTSTATUS NTAPI np_query(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PFILE_OBJECT file = stack->FileObject;
    PVOID buf = irp->AssociatedIrp.SystemBuffer;
    ULONG length = stack->Parameters.QueryFile.Length;
    NP_INSTANCE *in = file->FsContext;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    ULONG_PTR info = 0;
    lock();
    switch (stack->Parameters.QueryFile.FileInformationClass) {
    case FilePipeInformation:
        if (!in) {
            status = STATUS_PIPE_DISCONNECTED;
        } else if (length < sizeof(FILE_PIPE_INFORMATION)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            FILE_PIPE_INFORMATION *p = buf;
            p->ReadMode = in->ReadMode[end_index(file)];
            p->CompletionMode = in->CompletionMode[end_index(file)];
            status = STATUS_SUCCESS;
            info = sizeof(*p);
        }
        break;
    case FilePipeLocalInformation:
        if (!in) {
            status = STATUS_PIPE_DISCONNECTED;
        } else if (length < sizeof(FILE_PIPE_LOCAL_INFORMATION)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            FILE_PIPE_LOCAL_INFORMATION *p = buf;
            memset(p, 0, sizeof(*p));
            p->NamedPipeType = in->Pipe->Type;
            p->NamedPipeConfiguration = 2; /* full duplex. */
            p->MaximumInstances = in->Pipe->MaxInstances;
            p->CurrentInstances = in->Pipe->Instances;
            p->InboundQuota = in->Pipe->InQuota;
            p->OutboundQuota = in->Pipe->OutQuota;
            p->ReadDataAvailable = read_queue(in, file)->Bytes;
            p->WriteQuotaAvailable = in->Pipe->OutQuota;
            p->NamedPipeState = in->State;
            p->NamedPipeEnd = file->FsContext2 == END_SERVER ? FILE_PIPE_SERVER_END : FILE_PIPE_CLIENT_END;
            status = STATUS_SUCCESS;
            info = sizeof(*p);
        }
        break;
    case FileNameInformation: {
        /* the name within the pipe volume: a backslash and the pipe name, or the backslash alone for a root handle. */
        if (length < offsetof(FILE_NAME_INFORMATION, FileName)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        FILE_NAME_INFORMATION *n = buf;
        ULONG name_len = sizeof(WCHAR) + (in && file->FsContext2 != END_ROOT ? in->Pipe->Name.Length : 0);
        ULONG room = length - (ULONG)offsetof(FILE_NAME_INFORMATION, FileName);
        ULONG copy = name_len < room ? name_len : room;
        n->FileNameLength = name_len;
        if (copy >= sizeof(WCHAR))
            n->FileName[0] = '\\';
        if (copy > sizeof(WCHAR))
            memcpy(&n->FileName[1], in->Pipe->Name.Buffer, copy - sizeof(WCHAR));
        info = offsetof(FILE_NAME_INFORMATION, FileName) + copy;
        status = copy < name_len ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
        break;
    }
    case FileStandardInformation:
        if (length < sizeof(FILE_STANDARD_INFORMATION)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            FILE_STANDARD_INFORMATION *s = buf;
            memset(s, 0, sizeof(*s));
            s->NumberOfLinks = 1;
            if (in) {
                s->EndOfFile.QuadPart = read_queue(in, file)->Bytes;
                s->AllocationSize.QuadPart = in->Pipe->InQuota + in->Pipe->OutQuota;
            }
            status = STATUS_SUCCESS;
            info = sizeof(*s);
        }
        break;
    default:
        break;
    }
    unlock();
    return complete_irp(irp, status, info);
}

static NTSTATUS NTAPI np_set(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PFILE_OBJECT file = stack->FileObject;
    NP_INSTANCE *in = file->FsContext;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    lock();
    if (stack->Parameters.SetFile.FileInformationClass == FilePipeInformation) {
        FILE_PIPE_INFORMATION *p = irp->AssociatedIrp.SystemBuffer;
        if (!in) {
            status = STATUS_PIPE_DISCONNECTED;
        } else if (stack->Parameters.SetFile.Length < sizeof(*p)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else if (p->ReadMode == FILE_PIPE_MESSAGE_MODE && in->Pipe->Type != FILE_PIPE_MESSAGE_TYPE) {
            status = STATUS_INVALID_PARAMETER;
        } else {
            in->ReadMode[end_index(file)] = p->ReadMode;
            in->CompletionMode[end_index(file)] = p->CompletionMode;
            status = STATUS_SUCCESS;
        }
    }
    unlock();
    return complete_irp(irp, status, 0);
}

static NTSTATUS NTAPI NpfsDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = np_create;
    driver->MajorFunction[IRP_MJ_CREATE_NAMED_PIPE] = np_create;
    driver->MajorFunction[IRP_MJ_CLEANUP] = np_cleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = np_close;
    driver->MajorFunction[IRP_MJ_QUERY_INFORMATION] = np_query;
    driver->MajorFunction[IRP_MJ_SET_INFORMATION] = np_set;
    driver->MajorFunction[IRP_MJ_QUERY_SECURITY] = np_query_security;
    driver->MajorFunction[IRP_MJ_SET_SECURITY] = np_set_security;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\NamedPipe");
    return IoCreateDevice(driver, 0, &name, FILE_DEVICE_NAMED_PIPE, 0, FALSE, &g_device);
}

void IopInitializeNamedPipes(void)
{
    ExInitializeFastMutex(&g_lock);
    KeInitializeMutex(&g_create_lock, 0);
    InitializeListHead(&g_pipes);
    InitializeListHead(&g_host_pipes);
    NTSTATUS status = IoCreateDriver(L"\\FileSystem\\Npfs", NpfsDriverEntry, nullptr);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("npfs failed to start: %08x", status);
        return;
    }
    ObCreateSymbolicLink(L"\\GLOBAL??\\PIPE", L"\\Device\\NamedPipe");
}

BOOLEAN IopIsNamedPipe(PFILE_OBJECT file)
{
    return g_device && file->DeviceObject == g_device;
}

/* ------------------------------------------------------------ system calls */

NTSTATUS NTAPI NtCreateNamedPipeFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                     PIO_STATUS_BLOCK iosb, ULONG share, ULONG disposition, ULONG options,
                                     ULONG type, ULONG read_mode, ULONG completion_mode, ULONG max_instances,
                                     ULONG in_quota, ULONG out_quota, PLARGE_INTEGER timeout)
{
    if (disposition != FILE_CREATE && disposition != FILE_OPEN && disposition != FILE_OPEN_IF)
        return STATUS_INVALID_PARAMETER;
    if (type > FILE_PIPE_MESSAGE_TYPE || read_mode > FILE_PIPE_MESSAGE_MODE ||
        (read_mode == FILE_PIPE_MESSAGE_MODE && type == FILE_PIPE_BYTE_STREAM_TYPE))
        return STATUS_INVALID_PARAMETER;
    NP_CREATE c = {type, read_mode, completion_mode, max_instances == 0xFFFFFFFF ? 0xFFFFFFFF : max_instances, in_quota,
                   out_quota, -500000, disposition};
    if (timeout) {
        LARGE_INTEGER t;
        NTSTATUS status = KiCopyFromUser(&t, timeout, sizeof(t));
        if (!NT_SUCCESS(status))
            return status;
        c.Timeout = t.QuadPart;
    }
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    /* one server create at a time: the create request finds its parameters through the owner thread. */
    KeWaitForSingleObject(&g_create_lock, Executive, KernelMode, FALSE, nullptr);
    lock();
    g_create_owner = KiCurrentThread();
    g_create_owner_params = &c;
    unlock();
    IO_STATUS_BLOCK io = {};
    HANDLE h = nullptr;
    status = IopCreateFile(&h, access, &oa, &io, 0, share, disposition, options, KeGetPreviousMode());
    lock();
    g_create_owner = nullptr;
    g_create_owner_params = nullptr;
    unlock();
    KeReleaseMutex(&g_create_lock, FALSE);
    KiFreeCapturedString(&name);
    io.Status = status;
    if (NT_SUCCESS(status)) {
        io.Information = FILE_CREATED;
        status = ObStoreHandle(handle, h);
    }
    if (iosb)
        KiCopyToUser(iosb, &io, sizeof(io));
    return status;
}

/* NtReadFile and NtWriteFile of a pipe end. */
NTSTATUS NpfsReadWrite(PFILE_OBJECT file, BOOLEAN write, HANDLE event, PVOID apc_routine, PVOID apc_context,
                       PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length)
{
    NTSTATUS status;
    NP_REQUEST call;
    if (!init_call(&call, file, event, apc_routine, apc_context, iosb, buffer, length, &status))
        return status;
    PUCHAR copy = nullptr;
    if (write && length) {
        /* the data is copied now, in the writer's address space. */
        copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NP_DATA) + length, KI_TAG('N', 'p', 'D', 't'));
        if (!copy)
            return call_done(&call, STATUS_INSUFFICIENT_RESOURCES, 0);
        status = KiCopyFromUser(copy + sizeof(NP_DATA), buffer, length);
        if (!NT_SUCCESS(status)) {
            ExFreePool(copy);
            return call_done(&call, status, 0);
        }
    }
    lock();
    NP_INSTANCE *in = file->FsContext;
    if (!in || file->FsContext2 == END_ROOT) {
        unlock();
        if (copy)
            ExFreePool(copy);
        return call_done(&call, in ? STATUS_INVALID_PARAMETER : STATUS_PIPE_DISCONNECTED, 0);
    }
    BOOLEAN server = file->FsContext2 == END_SERVER;
    if (write) {
        NTSTATUS st = STATUS_SUCCESS;
        if (in->State == FILE_PIPE_LISTENING_STATE)
            st = STATUS_PIPE_LISTENING;
        else if (in->State == FILE_PIPE_DISCONNECTED_STATE)
            st = STATUS_PIPE_DISCONNECTED;
        else if (in->State == FILE_PIPE_CLOSING_STATE || (server ? !in->ClientOpen : !in->ServerOpen))
            st = STATUS_PIPE_CLOSING;
        if (!NT_SUCCESS(st)) {
            unlock();
            if (copy)
                ExFreePool(copy);
            return call_done(&call, st, 0);
        }
        /* a client that impersonates, or stopped doing so, sends its current identity with the next data. */
        if (!server && (PsCurrentThread()->ImpersonationToken || in->ClientImpersonating)) {
            struct _TOKEN *now = SepReferenceEffectiveToken(PsCurrentThread());
            if (in->ClientToken)
                ObDereferenceObject(in->ClientToken);
            in->ClientToken = now;
            in->ClientImpersonating = PsCurrentThread()->ImpersonationToken != nullptr;
        }
        NP_QUEUE *q = write_queue(in, file);
        /* byte streams without readers skip empty writes; messages keep them. */
        if (length || in->Pipe->Type == FILE_PIPE_MESSAGE_TYPE) {
            NP_DATA *d = copy ? (NP_DATA *)copy
                              : ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NP_DATA), KI_TAG('N', 'p', 'D', 't'));
            if (!d) {
                unlock();
                return call_done(&call, STATUS_INSUFFICIENT_RESOURCES, 0);
            }
            d->Length = length;
            d->Offset = 0;
            InsertTailList(&q->Data, &d->Links);
            q->Bytes += length;
            q->Messages++;
            serve_readers(in, q, reader_mode(in, q));
            if (in->Host)
                host_pump(in);
        }
        unlock();
        return call_done(&call, STATUS_SUCCESS, length);
    }

    NP_QUEUE *q = read_queue(in, file);
    ULONG mode = in->ReadMode[end_index(file)];
    if (!IsListEmpty(&q->Data)) {
        ULONG moved;
        status = take_data(q, mode, call.Process, buffer, length, &moved);
        unlock();
        return call_done(&call, status, moved);
    }
    NTSTATUS st = STATUS_SUCCESS;
    if (in->State == FILE_PIPE_LISTENING_STATE)
        st = STATUS_PIPE_LISTENING;
    else if (in->State == FILE_PIPE_DISCONNECTED_STATE)
        st = STATUS_PIPE_DISCONNECTED;
    else if (in->State == FILE_PIPE_CLOSING_STATE || (server ? !in->ClientOpen : !in->ServerOpen))
        st = STATUS_PIPE_BROKEN;
    else if (in->CompletionMode[end_index(file)] == FILE_PIPE_COMPLETE_OPERATION)
        st = STATUS_PIPE_EMPTY;
    if (!NT_SUCCESS(st)) {
        unlock();
        return call_done(&call, st, 0);
    }
    NP_REQUEST *r = call_to_pool(&call);
    if (!r) {
        unlock();
        return call_done(&call, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    InsertTailList(&q->Readers, &r->Links);
    return pending(r);
}

typedef struct FILE_PIPE_PEEK_BUFFER {
    ULONG NamedPipeState;
    ULONG ReadDataAvailable;
    ULONG NumberOfMessages;
    ULONG MessageLength;
} FILE_PIPE_PEEK_BUFFER;

typedef struct FILE_PIPE_WAIT_FOR_BUFFER {
    LARGE_INTEGER Timeout;
    ULONG NameLength;
    BOOLEAN TimeoutSpecified;
    WCHAR Name[1];
} FILE_PIPE_WAIT_FOR_BUFFER;

static NTSTATUS peek(NP_REQUEST *r, NP_INSTANCE *in, PFILE_OBJECT file, PVOID out, ULONG out_len)
{
    if (out_len < sizeof(FILE_PIPE_PEEK_BUFFER))
        return immediate(r, STATUS_BUFFER_TOO_SMALL, 0);
    NP_QUEUE *q = read_queue(in, file);
    BOOLEAN server = file->FsContext2 == END_SERVER;
    if (IsListEmpty(&q->Data) &&
        (in->State == FILE_PIPE_CLOSING_STATE || (in->State == FILE_PIPE_CONNECTED_STATE && (server ? !in->ClientOpen : !in->ServerOpen))))
        return immediate(r, STATUS_PIPE_BROKEN, 0);
    if (in->State == FILE_PIPE_LISTENING_STATE)
        return immediate(r, STATUS_PIPE_LISTENING, 0);
    if (in->State == FILE_PIPE_DISCONNECTED_STATE)
        return immediate(r, STATUS_PIPE_DISCONNECTED, 0);
    FILE_PIPE_PEEK_BUFFER head;
    head.NamedPipeState = in->State;
    head.ReadDataAvailable = q->Bytes;
    head.NumberOfMessages = in->Pipe->Type == FILE_PIPE_MESSAGE_TYPE ? q->Messages : 0xFFFFFFFF;
    head.MessageLength = 0;
    if (!IsListEmpty(&q->Data) && in->Pipe->Type == FILE_PIPE_MESSAGE_TYPE) {
        NP_DATA *d = CONTAINING_RECORD(q->Data.Flink, NP_DATA, Links);
        head.MessageLength = d->Length - d->Offset;
    }
    if (!NT_SUCCESS(copy_to_process(r->Process, out, &head, sizeof(head))))
        return immediate(r, STATUS_ACCESS_VIOLATION, 0);
    ULONG room = out_len - sizeof(head), done = 0;
    PUCHAR dst = (PUCHAR)out + sizeof(head);
    for (PLIST_ENTRY l = q->Data.Flink; l != &q->Data && done < room; l = l->Flink) {
        NP_DATA *d = CONTAINING_RECORD(l, NP_DATA, Links);
        ULONG n = d->Length - d->Offset;
        if (n > room - done)
            n = room - done;
        if (!NT_SUCCESS(copy_to_process(r->Process, dst + done, d->Data + d->Offset, n)))
            return immediate(r, STATUS_ACCESS_VIOLATION, 0);
        done += n;
        if (in->Pipe->Type == FILE_PIPE_MESSAGE_TYPE)
            break;
    }
    NTSTATUS status = STATUS_SUCCESS;
    if (in->Pipe->Type == FILE_PIPE_MESSAGE_TYPE && done < head.MessageLength)
        status = STATUS_BUFFER_OVERFLOW;
    return immediate(r, status, sizeof(head) + done);
}

/*
 * writes a message and reads the answer as one request. the end must read
 * messages and have nothing left to read, a guess at windows' checks. the
 * lock is held, and released when the request goes pending.
 */
static NTSTATUS transceive(NP_REQUEST *r, NP_INSTANCE *in, PFILE_OBJECT file, PVOID input, ULONG in_len,
                           BOOLEAN *released)
{
    BOOLEAN server = file->FsContext2 == END_SERVER;
    if (in->ReadMode[end_index(file)] != FILE_PIPE_MESSAGE_MODE)
        return immediate(r, STATUS_INVALID_PIPE_STATE, 0);
    if (in->State != FILE_PIPE_CONNECTED_STATE || (server ? !in->ClientOpen : !in->ServerOpen))
        return immediate(r, STATUS_PIPE_DISCONNECTED, 0);
    NP_QUEUE *rq = read_queue(in, file);
    if (!IsListEmpty(&rq->Data))
        return immediate(r, STATUS_PIPE_BUSY, 0);
    NP_DATA *d = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NP_DATA) + in_len, KI_TAG('N', 'p', 'D', 't'));
    if (!d)
        return immediate(r, STATUS_INSUFFICIENT_RESOURCES, 0);
    NTSTATUS status = KiCopyFromUser(d->Data, input, in_len);
    if (!NT_SUCCESS(status)) {
        ExFreePool(d);
        return immediate(r, status, 0);
    }
    d->Length = in_len;
    d->Offset = 0;
    NP_QUEUE *wq = write_queue(in, file);
    InsertTailList(&wq->Data, &d->Links);
    wq->Bytes += in_len;
    wq->Messages++;
    serve_readers(in, wq, reader_mode(in, wq));
    if (!IsListEmpty(&rq->Data)) {
        ULONG moved;
        status = take_data(rq, FILE_PIPE_MESSAGE_MODE, r->Process, r->Buffer, r->Length, &moved);
        return immediate(r, status, moved);
    }
    InsertTailList(&rq->Readers, &r->Links);
    *released = TRUE;
    return pending(r);
}

/* polls for a listening instance of a pipe, for at most the timeout. */
static NTSTATUS wait_pipe(const FILE_PIPE_WAIT_FOR_BUFFER *w, ULONG in_len)
{
    if (in_len < offsetof(FILE_PIPE_WAIT_FOR_BUFFER, Name) || w->NameLength > in_len - offsetof(FILE_PIPE_WAIT_FOR_BUFFER, Name))
        return STATUS_INVALID_PARAMETER;
    UNICODE_STRING name = {(USHORT)w->NameLength, (USHORT)w->NameLength, (PWSTR)w->Name};
    LONGLONG remaining = 0;
    BOOLEAN have_timeout = FALSE;
    for (;;) {
        lock();
        NP_PIPE *p = find_pipe(&name);
        BOOLEAN ready = p && listening_instance(p);
        if (p && !have_timeout) {
            remaining = w->TimeoutSpecified ? w->Timeout.QuadPart : p->DefaultTimeout;
            have_timeout = TRUE;
        }
        unlock();
        /* a pipe to discord is there whenever the host serves it. */
        ULONG index;
        if (!p && discord_index(&name, &index))
            return host_call(MIME_HC_NET_IPC, index, 1, 0, nullptr) ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_SUCCESS;
        if (!p)
            return STATUS_OBJECT_NAME_NOT_FOUND;
        if (ready)
            return STATUS_SUCCESS;
        /* relative timeouts are negative counts of 100 ns; zero or positive values give up now. */
        if (remaining >= 0)
            return STATUS_IO_TIMEOUT;
        LARGE_INTEGER step;
        step.QuadPart = -100000; /* 10 ms. */
        NTSTATUS st = KeDelayExecutionThread(UserMode, TRUE, &step);
        if (st != STATUS_SUCCESS)
            return st;
        remaining += 100000;
        if (remaining >= 0)
            return STATUS_IO_TIMEOUT;
    }
}

NTSTATUS NpfsFsControl(PFILE_OBJECT file, HANDLE event, PVOID apc_routine, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                       ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len)
{
    NTSTATUS status;
    NP_REQUEST *r = new_request(file, event, apc_routine, apc_context, iosb, output, out_len, &status);
    if (!r)
        return status;
    if (code == FSCTL_PIPE_WAIT) {
        if (file->FsContext2 != END_ROOT || in_len > 1024)
            return immediate(r, STATUS_INVALID_PARAMETER, 0);
        UCHAR buf[1024];
        status = KiCopyFromUser(buf, input, in_len);
        if (NT_SUCCESS(status))
            status = wait_pipe((FILE_PIPE_WAIT_FOR_BUFFER *)buf, in_len);
        return immediate(r, status, 0);
    }
    lock();
    NP_INSTANCE *in = file->FsContext;
    if (!in || file->FsContext2 == END_ROOT) {
        unlock();
        return immediate(r, in ? STATUS_INVALID_PARAMETER : STATUS_PIPE_DISCONNECTED, 0);
    }
    switch (code) {
    case FSCTL_PIPE_LISTEN:
        if (file->FsContext2 != END_SERVER)
            status = STATUS_ILLEGAL_FUNCTION;
        else if (in->State == FILE_PIPE_CONNECTED_STATE)
            status = STATUS_PIPE_CONNECTED;
        else if (in->State == FILE_PIPE_CLOSING_STATE)
            status = STATUS_PIPE_CLOSING;
        else if (in->CompletionMode[0] == FILE_PIPE_COMPLETE_OPERATION)
            status = (in->State = FILE_PIPE_LISTENING_STATE, STATUS_PIPE_LISTENING);
        else {
            in->State = FILE_PIPE_LISTENING_STATE;
            InsertTailList(&in->Listeners, &r->Links);
            return pending(r);
        }
        unlock();
        return immediate(r, status, 0);
    case FSCTL_PIPE_DISCONNECT:
        if (file->FsContext2 != END_SERVER) {
            status = STATUS_ILLEGAL_FUNCTION;
        } else {
            fail_all(&in->Listeners, STATUS_PIPE_DISCONNECTED);
            fail_all(&in->ToClient.Readers, STATUS_PIPE_DISCONNECTED);
            fail_all(&in->ToServer.Readers, STATUS_PIPE_DISCONNECTED);
            flush_queue(&in->ToClient);
            flush_queue(&in->ToServer);
            in->ToServer.Consumed = FALSE;
            if (in->ClientToken) {
                ObDereferenceObject(in->ClientToken);
                in->ClientToken = nullptr;
            }
            if (in->Client) {
                /* the client end stays open but no longer belongs to the instance. */
                in->Client->FsContext = nullptr;
                in->Client = nullptr;
                in->ClientOpen = FALSE;
            }
            in->State = FILE_PIPE_DISCONNECTED_STATE;
            status = STATUS_SUCCESS;
        }
        unlock();
        return immediate(r, status, 0);
    case FSCTL_PIPE_IMPERSONATE: {
        /* the server thread takes the identity of the client whose data it read. */
        struct _TOKEN *client = in->ClientToken;
        if (file->FsContext2 != END_SERVER) {
            status = STATUS_ILLEGAL_FUNCTION;
        } else if (!in->ToServer.Consumed || !client) {
            status = STATUS_CANNOT_IMPERSONATE;
        } else {
            ULONG level = in->ClientLevel;
            struct _TOKEN *copy = SepDuplicateForPipe(client, level, in->ClientEffectiveOnly);
            status = copy ? SepImpersonateClient(PsCurrentThread(), copy, FALSE, in->ClientEffectiveOnly, level)
                          : STATUS_INSUFFICIENT_RESOURCES;
            if (copy)
                ObDereferenceObject(copy);
        }
        unlock();
        return immediate(r, status, 0);
    }
    case FSCTL_PIPE_PEEK:
        status = peek(r, in, file, output, out_len);
        unlock();
        return status;
    case FSCTL_PIPE_TRANSCEIVE:
        {
            BOOLEAN released = FALSE;
            status = transceive(r, in, file, input, in_len, &released);
            if (!released)
                unlock();
            return status;
        }
    case FSCTL_PIPE_GET_PIPE_ATTRIBUTE:
    case FSCTL_PIPE_GET_CONNECTION_ATTRIBUTE: {
        /* the processes and sessions of the two ends, queried by name. */
        char name[32] = {};
        ULONG value = 0;
        status = in_len && in_len < sizeof(name) ? KiCopyFromUser(name, input, in_len) : STATUS_INVALID_PARAMETER;
        if (NT_SUCCESS(status)) {
            if (!strcmp(name, "ClientProcessId"))
                value = in->ProcessId[1];
            else if (!strcmp(name, "ServerProcessId"))
                value = in->ProcessId[0];
            else if (!strcmp(name, "ClientSessionId") || !strcmp(name, "ServerSessionId"))
                value = 1;
            else
                status = STATUS_INVALID_PARAMETER;
        }
        if (NT_SUCCESS(status))
            status = out_len < sizeof(value) ? STATUS_BUFFER_TOO_SMALL : KiCopyToUser(output, &value, sizeof(value));
        unlock();
        return immediate(r, status, NT_SUCCESS(status) ? sizeof(value) : 0);
    }
    default:
        unlock();
        return immediate(r, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

/* cancels a caller's pending requests on a file: all of them, or the one with apc_context. */
NTSTATUS NpfsCancel(PFILE_OBJECT file, PEPROCESS process, BOOLEAN one, PVOID apc_context)
{
    NTSTATUS found = STATUS_NOT_FOUND;
    lock();
    NP_INSTANCE *in = file->FsContext;
    if (in) {
        LIST_ENTRY *lists[3] = {&in->ToServer.Readers, &in->ToClient.Readers, &in->Listeners};
        for (int i = 0; i < 3; i++) {
            PLIST_ENTRY l = lists[i]->Flink;
            while (l != lists[i]) {
                NP_REQUEST *r = CONTAINING_RECORD(l, NP_REQUEST, Links);
                l = l->Flink;
                if (r->File != file || r->Process != process || (one && r->ApcContext != apc_context))
                    continue;
                RemoveEntryList(&r->Links);
                complete_request(r, STATUS_CANCELLED, 0);
                found = STATUS_SUCCESS;
            }
        }
    }
    unlock();
    return found;
}
