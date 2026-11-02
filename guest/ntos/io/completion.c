/*
 * i/o completion ports: a queue of completion packets that threads
 * remove, and the association of file objects with a port.
 */
#include "../ntos.h"

POBJECT_TYPE IoCompletionObjectType;

typedef struct IO_COMPLETION_PACKET {
    LIST_ENTRY Links;
    PVOID KeyContext;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatus;
} IO_COMPLETION_PACKET;

/* the semaphore counts the queued packets; the lock guards the list. */
typedef struct IO_COMPLETION {
    KSEMAPHORE Packets;
    KSPIN_LOCK Lock;
    LIST_ENTRY Queue;
    LONG Count;
} IO_COMPLETION;

/* what NtSetInformationFile(FileCompletionInformation) attaches to a file object. */
typedef struct IO_COMPLETION_CONTEXT {
    IO_COMPLETION *Port;
    PVOID Key;
} IO_COMPLETION_CONTEXT;

typedef struct FILE_IO_COMPLETION_INFORMATION {
    PVOID KeyContext;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock;
} FILE_IO_COMPLETION_INFORMATION, *PFILE_IO_COMPLETION_INFORMATION;

#define IO_COMPLETION_QUERY_STATE 0x0001
#define IO_COMPLETION_MODIFY_STATE 0x0002
#define IO_COMPLETION_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3)

static void completion_delete(PVOID object)
{
    IO_COMPLETION *port = object;
    while (!IsListEmpty(&port->Queue))
        ExFreePool(CONTAINING_RECORD(RemoveHeadList(&port->Queue), IO_COMPLETION_PACKET, Links));
}

void IopInitializeCompletion(void)
{
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = IO_COMPLETION_ALL_ACCESS;
    init.DeleteProcedure = completion_delete;
    ObSetGenericMapping(&init, 0x20001, 0x20002, 0x120000, 0x1F0003);
    IoCompletionObjectType = ObCreateObjectType(L"IoCompletion", &init);
}

static NTSTATUS queue_packet(IO_COMPLETION *port, PVOID key, PVOID apc_context, NTSTATUS status, ULONG_PTR information)
{
    IO_COMPLETION_PACKET *p = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*p), KI_TAG('I', 'o', 'C', 'p'));
    if (!p)
        return STATUS_INSUFFICIENT_RESOURCES;
    p->KeyContext = key;
    p->ApcContext = apc_context;
    p->IoStatus.Status = status;
    p->IoStatus.Information = information;
    KIRQL irql;
    KeAcquireSpinLock(&port->Lock, &irql);
    InsertTailList(&port->Queue, &p->Links);
    port->Count++;
    KeReleaseSpinLock(&port->Lock, irql);
    KeReleaseSemaphore(&port->Packets, IO_NO_INCREMENT, 1, FALSE);
    if (KiTimelineOn) {
        char text[24];
        mime_format(text, sizeof(text), "port%llx", (ULONG64)(ULONG_PTR)port & 0xFFFFFFFFFFFull);
        KiTimelineNote(3, text, (ULONG64)(ULONG_PTR)apc_context);
    }
    return STATUS_SUCCESS;
}

NTSTATUS IopQueueCompletion(PVOID port, PVOID key, PVOID apc_context, NTSTATUS status, ULONG_PTR information)
{
    return queue_packet(port, key, apc_context, status, information);
}

/*
 * posts the packet of a completed user request when the file is bound to a port, unless it succeeded synchronously
 * and the file set FILE_SKIP_COMPLETION_PORT_ON_SUCCESS.
 */
void IopPostCompletion(PFILE_OBJECT file, PVOID apc_context, NTSTATUS status, ULONG_PTR information, BOOLEAN synchronous)
{
    IO_COMPLETION_CONTEXT *ctx = file->CompletionContext;
    if (synchronous && NT_SUCCESS(status) && (file->Flags & FO_SKIP_COMPLETION_PORT) &&
        !(file->Flags & FO_SYNCHRONOUS_IO))
        return;
    if (ctx && apc_context)
        queue_packet(ctx->Port, ctx->Key, apc_context, status, information);
}

/* the file's own event, which FILE_SKIP_SET_EVENT_ON_HANDLE leaves alone. */
void IopSignalFile(PFILE_OBJECT file)
{
    if (!(file->Flags & FO_SKIP_SET_EVENT))
        KeSetEvent(&file->Event, IO_NO_INCREMENT, FALSE);
}

/* like windows, a file object can be bound to a port only once. */
NTSTATUS IopSetCompletion(PFILE_OBJECT file, HANDLE port_handle, PVOID key)
{
    IO_COMPLETION *port;
    NTSTATUS status = ObReferenceHandle(port_handle, IoCompletionObjectType, KeGetPreviousMode(), (PVOID *)&port, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    IO_COMPLETION_CONTEXT *ctx = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*ctx), KI_TAG('I', 'o', 'C', 'c'));
    if (!ctx) {
        ObDereferenceObject(port);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ctx->Port = port;
    ctx->Key = key;
    if (__atomic_compare_exchange_n(&file->CompletionContext, &(PVOID){nullptr}, ctx, FALSE, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE))
        return STATUS_SUCCESS;
    ExFreePool(ctx);
    ObDereferenceObject(port);
    return STATUS_INVALID_PARAMETER;
}

void IopReleaseCompletion(PFILE_OBJECT file)
{
    IO_COMPLETION_CONTEXT *ctx = file->CompletionContext;
    if (!ctx)
        return;
    file->CompletionContext = nullptr;
    ObDereferenceObject(ctx->Port);
    ExFreePool(ctx);
}

NTSTATUS NTAPI NtCreateIoCompletion(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, ULONG concurrency)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    IO_COMPLETION *port;
    status = ObCreateObject(IoCompletionObjectType, attributes ? &oa : nullptr, KeGetPreviousMode(), sizeof(*port),
                            (PVOID *)&port);
    if (NT_SUCCESS(status)) {
        memset(port, 0, sizeof(*port));
        KeInitializeSemaphore(&port->Packets, 0, 0x7FFFFFFF);
        KeInitializeSpinLock(&port->Lock);
        InitializeListHead(&port->Queue);
        HANDLE h;
        status = ObInsertObjectNamed(port, attributes ? &oa : nullptr, access ? access : IO_COMPLETION_ALL_ACCESS,
                                     KeGetPreviousMode(), &h);
        if (NT_SUCCESS(status))
            status = ObStoreHandle(handle, h);
    }
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtOpenIoCompletion(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    HANDLE h;
    status = ObOpenObjectByName(&oa, IoCompletionObjectType, KeGetPreviousMode(), access, nullptr, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtSetIoCompletion(HANDLE handle, PVOID key, PVOID apc_context, NTSTATUS io_status, ULONG_PTR information)
{
    IO_COMPLETION *port;
    NTSTATUS status = ObReferenceHandle(handle, IoCompletionObjectType, KeGetPreviousMode(), (PVOID *)&port, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    status = queue_packet(port, key, apc_context, io_status, information);
    ObDereferenceObject(port);
    return status;
}

NTSTATUS NTAPI NtSetIoCompletionEx(HANDLE handle, HANDLE reserve, PVOID key, PVOID apc_context, NTSTATUS io_status,
                                   ULONG_PTR information)
{
    return NtSetIoCompletion(handle, key, apc_context, io_status, information);
}

/* waits in the caller's mode for up to count packets, into a kernel buffer. */
static NTSTATUS remove_packets(HANDLE handle, FILE_IO_COMPLETION_INFORMATION *out, ULONG count, PULONG removed,
                               PLARGE_INTEGER timeout, BOOLEAN alertable)
{
    IO_COMPLETION *port;
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    *removed = 0;
    NTSTATUS status = ObReferenceHandle(handle, IoCompletionObjectType, mode, (PVOID *)&port, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    ULONG n = 0;
    status = KeWaitForSingleObject(&port->Packets, UserRequest, mode, alertable, timeout);
    while (status == STATUS_SUCCESS) {
        KIRQL irql;
        KeAcquireSpinLock(&port->Lock, &irql);
        IO_COMPLETION_PACKET *p = nullptr;
        if (!IsListEmpty(&port->Queue)) {
            p = CONTAINING_RECORD(RemoveHeadList(&port->Queue), IO_COMPLETION_PACKET, Links);
            port->Count--;
        }
        KeReleaseSpinLock(&port->Lock, irql);
        if (!p)
            break;
        out[n].KeyContext = p->KeyContext;
        out[n].ApcContext = p->ApcContext;
        out[n].IoStatusBlock = p->IoStatus;
        ExFreePool(p);
        if (++n == count)
            break;
        /* after the first packet, take only the ones already queued. */
        LARGE_INTEGER zero = {};
        if (KeWaitForSingleObject(&port->Packets, UserRequest, mode, FALSE, &zero) != STATUS_SUCCESS)
            break;
    }
    ObDereferenceObject(port);
    *removed = n;
    return n ? STATUS_SUCCESS : status;
}

static NTSTATUS capture_timeout(PLARGE_INTEGER user, PLARGE_INTEGER storage, PLARGE_INTEGER *out)
{
    *out = nullptr;
    if (!user)
        return STATUS_SUCCESS;
    NTSTATUS status = KiCopyFromUser(storage, user, sizeof(*storage));
    if (NT_SUCCESS(status))
        *out = storage;
    return status;
}

NTSTATUS NTAPI NtRemoveIoCompletion(HANDLE handle, PVOID *key, PVOID *apc_context, PIO_STATUS_BLOCK iosb,
                                    PLARGE_INTEGER timeout)
{
    LARGE_INTEGER t, *pt;
    NTSTATUS status = capture_timeout(timeout, &t, &pt);
    if (!NT_SUCCESS(status))
        return status;
    FILE_IO_COMPLETION_INFORMATION info;
    ULONG n;
    status = remove_packets(handle, &info, 1, &n, pt, FALSE);
    if (!n)
        return status;
    if (!NT_SUCCESS(KiCopyToUser(key, &info.KeyContext, sizeof(PVOID))) ||
        !NT_SUCCESS(KiCopyToUser(apc_context, &info.ApcContext, sizeof(PVOID))) ||
        !NT_SUCCESS(KiCopyToUser(iosb, &info.IoStatusBlock, sizeof(IO_STATUS_BLOCK))))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtRemoveIoCompletionEx(HANDLE handle, PVOID info, ULONG count,
                                      PULONG removed, PLARGE_INTEGER timeout, BOOLEAN alertable)
{
    if (!count)
        return STATUS_INVALID_PARAMETER;
    if (count > 256)
        count = 256;
    LARGE_INTEGER t, *pt;
    NTSTATUS status = capture_timeout(timeout, &t, &pt);
    if (!NT_SUCCESS(status))
        return status;
    FILE_IO_COMPLETION_INFORMATION *buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, count * sizeof(*buf), KI_TAG('I', 'o', 'C', 'r'));
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG n = 0;
    status = remove_packets(handle, buf, count, &n, pt, alertable);
    if (n && !NT_SUCCESS(KiCopyToUser(info, buf, n * sizeof(*buf))))
        status = STATUS_ACCESS_VIOLATION;
    ExFreePool(buf);
    if (removed && !NT_SUCCESS(KiCopyToUser(removed, &n, sizeof(n))))
        status = STATUS_ACCESS_VIOLATION;
    return status;
}

typedef struct IO_COMPLETION_BASIC_INFORMATION {
    LONG Depth;
} IO_COMPLETION_BASIC_INFORMATION;

NTSTATUS NTAPI NtQueryIoCompletion(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    if (klass != 0)
        return STATUS_INVALID_INFO_CLASS;
    if (length < sizeof(IO_COMPLETION_BASIC_INFORMATION))
        return STATUS_INFO_LENGTH_MISMATCH;
    IO_COMPLETION *port;
    NTSTATUS status = ObReferenceHandle(handle, IoCompletionObjectType, KeGetPreviousMode(), (PVOID *)&port, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    IO_COMPLETION_BASIC_INFORMATION basic = {port->Count};
    ObDereferenceObject(port);
    status = KiCopyToUser(info, &basic, sizeof(basic));
    ULONG n = sizeof(basic);
    if (NT_SUCCESS(status) && returned)
        status = KiCopyToUser(returned, &n, sizeof(n));
    return status;
}

/* ------------------------------------------------------------ completion apcs */

static void NTAPI apc_free(PKAPC apc, PKNORMAL_ROUTINE *normal, PVOID *context, PVOID *arg1, PVOID *arg2)
{
    ExFreePool(apc);
}

/* a low bit on the apc routine marks a 32-bit status block: the mark moves to the iosb. */
void IopTakeIosb32(PVOID *apc_routine, PIO_STATUS_BLOCK *iosb)
{
    if (!((ULONG_PTR)*apc_routine & 1))
        return;
    *apc_routine = (PVOID)((ULONG_PTR)*apc_routine & ~(ULONG_PTR)1);
    if (*iosb)
        *iosb = (PIO_STATUS_BLOCK)((ULONG_PTR)*iosb | 1);
}

/* the status block bytes and their address, in the 32-bit layout when the iosb is marked. */
ULONG IopIosbImage(PIO_STATUS_BLOCK iosb, NTSTATUS status, ULONG_PTR information, PVOID *address, ULONG64 image[2])
{
    *address = (PVOID)((ULONG_PTR)iosb & ~(ULONG_PTR)1);
    if ((ULONG_PTR)iosb & 1) {
        image[0] = (ULONG)status | (ULONG64)(ULONG)information << 32;
        return 8;
    }
    IO_STATUS_BLOCK io = {.Status = status, .Information = information};
    memcpy(image, &io, sizeof(io));
    return sizeof(io);
}

void IopQueueCompletionApc(PETHREAD thread, PVOID routine, PVOID context, PIO_STATUS_BLOCK iosb)
{
    if (!routine)
        return;
    iosb = (PIO_STATUS_BLOCK)((ULONG_PTR)iosb & ~(ULONG_PTR)1);
    PKAPC apc = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KAPC), KI_TAG('I', 'o', 'A', 'p'));
    if (!apc)
        return;
    KeInitializeApc(apc, &thread->Tcb, OriginalApcEnvironment, apc_free, nullptr, (PKNORMAL_ROUTINE)routine, UserMode,
                    context);
    if (!KeInsertQueueApc(apc, iosb, nullptr, IO_NO_INCREMENT))
        ExFreePool(apc);
}

/* ------------------------------------------------------------ wait completion packets */

/*
 * a packet ties the wait on an object to a port: once the object is signalled, a completion is queued to the port.
 * a system thread does the wait, and a packet has at most one wait pending.
 */
typedef struct WAIT_PACKET WAIT_PACKET;

typedef struct PACKET_WAIT {
    WAIT_PACKET *Packet;
    IO_COMPLETION *Port;
    PVOID Target;
    PDISPATCHER_HEADER TargetWait;
    PVOID Key;
    PVOID ApcContext;
    NTSTATUS IoStatus;
    ULONG_PTR Information;
    KEVENT Cancel;
} PACKET_WAIT;

struct WAIT_PACKET {
    KSPIN_LOCK Lock;
    PACKET_WAIT *Active;
};

static POBJECT_TYPE WaitPacketObjectType;

/* ends the pending wait of a packet, and tells whether there was one. */
static BOOLEAN cancel_wait(WAIT_PACKET *packet)
{
    KIRQL irql;
    KeAcquireSpinLock(&packet->Lock, &irql);
    PACKET_WAIT *wait = packet->Active;
    packet->Active = nullptr;
    if (wait)
        KeSetEvent(&wait->Cancel, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&packet->Lock, irql);
    return wait != nullptr;
}

static void packet_close(PEPROCESS process, PVOID object, ULONG handle_count)
{
    if (handle_count <= 1)
        cancel_wait(object);
}

static void NTAPI packet_thread(PVOID context)
{
    PACKET_WAIT *wait = context;
    PVOID objects[2] = {wait->TargetWait, &wait->Cancel};
    KWAIT_BLOCK blocks[2];
    NTSTATUS status = KiWaitObjects(2, objects, WaitAny, Executive, KernelMode, FALSE, nullptr, blocks);
    WAIT_PACKET *packet = wait->Packet;
    BOOLEAN post = FALSE;
    if (status == STATUS_WAIT_0) {
        KIRQL irql;
        KeAcquireSpinLock(&packet->Lock, &irql);
        if (packet->Active == wait) {
            packet->Active = nullptr;
            post = TRUE;
        }
        KeReleaseSpinLock(&packet->Lock, irql);
    }
    if (post)
        queue_packet(wait->Port, wait->Key, wait->ApcContext, wait->IoStatus, wait->Information);
    ObDereferenceObject(wait->Target);
    ObDereferenceObject(wait->Port);
    ObDereferenceObject(packet);
    ExFreePool(wait);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS NTAPI NtCreateWaitCompletionPacket(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    WAIT_PACKET *packet;
    status = ObCreateObject(WaitPacketObjectType, attributes ? &oa : nullptr, KeGetPreviousMode(), sizeof(*packet),
                            (PVOID *)&packet);
    if (NT_SUCCESS(status)) {
        memset(packet, 0, sizeof(*packet));
        KeInitializeSpinLock(&packet->Lock);
        HANDLE h;
        status = ObInsertObjectNamed(packet, attributes ? &oa : nullptr, access ? access : IO_COMPLETION_ALL_ACCESS,
                                     KeGetPreviousMode(), &h);
        if (NT_SUCCESS(status))
            status = ObStoreHandle(handle, h);
    }
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtAssociateWaitCompletionPacket(HANDLE packet_handle, HANDLE port_handle, HANDLE target_handle, PVOID key,
                                               PVOID apc_context, NTSTATUS io_status, ULONG_PTR information,
                                               PBOOLEAN already_signaled)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    WAIT_PACKET *packet;
    IO_COMPLETION *port;
    PVOID target;
    NTSTATUS status = ObReferenceHandle(packet_handle, WaitPacketObjectType, mode, (PVOID *)&packet, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    status = ObReferenceHandle(port_handle, IoCompletionObjectType, mode, (PVOID *)&port, nullptr);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(packet);
        return status;
    }
    status = ObReferenceHandle(target_handle, nullptr, mode, &target, nullptr);
    PDISPATCHER_HEADER target_wait = NT_SUCCESS(status) ? ObGetWaitObject(target) : nullptr;
    if (NT_SUCCESS(status) && !target_wait) {
        ObDereferenceObject(target);
        status = STATUS_OBJECT_TYPE_MISMATCH;
    }
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(port);
        ObDereferenceObject(packet);
        return status;
    }
    if (already_signaled) {
        BOOLEAN no = FALSE;
        status = KiCopyToUser(already_signaled, &no, sizeof(no));
        if (!NT_SUCCESS(status))
            goto fail;
    }
    /* a wait already pending is no place for a second one. */
    if (packet->Active) {
        status = STATUS_INVALID_PARAMETER;
        goto fail;
    }
    /* an object that is signalled now completes at once. */
    LARGE_INTEGER zero = {};
    PVOID now[1] = {target_wait};
    KWAIT_BLOCK block;
    if (KiWaitObjects(1, now, WaitAny, Executive, KernelMode, FALSE, &zero, &block) == STATUS_WAIT_0) {
        if (already_signaled) {
            BOOLEAN yes = TRUE;
            KiCopyToUser(already_signaled, &yes, sizeof(yes));
        }
        status = queue_packet(port, key, apc_context, io_status, information);
        goto fail;
    }
    PACKET_WAIT *wait = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*wait), KI_TAG('W', 'p', 'W', 't'));
    if (!wait) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }
    wait->Packet = packet;
    wait->Port = port;
    wait->Target = target;
    wait->TargetWait = target_wait;
    wait->Key = key;
    wait->ApcContext = apc_context;
    wait->IoStatus = io_status;
    wait->Information = information;
    KeInitializeEvent(&wait->Cancel, NotificationEvent, FALSE);
    KIRQL irql;
    KeAcquireSpinLock(&packet->Lock, &irql);
    packet->Active = wait;
    KeReleaseSpinLock(&packet->Lock, irql);
    status = PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, packet_thread, wait);
    if (NT_SUCCESS(status))
        return STATUS_SUCCESS;
    KeAcquireSpinLock(&packet->Lock, &irql);
    packet->Active = nullptr;
    KeReleaseSpinLock(&packet->Lock, irql);
    ExFreePool(wait);
fail:
    ObDereferenceObject(target);
    ObDereferenceObject(port);
    ObDereferenceObject(packet);
    return status;
}

/* a cancelled wait reports STATUS_CANCELLED, a packet that was queued already STATUS_SUCCESS. */
NTSTATUS NTAPI NtCancelWaitCompletionPacket(HANDLE packet_handle, BOOLEAN remove_signaled)
{
    WAIT_PACKET *packet;
    NTSTATUS status = ObReferenceHandle(packet_handle, WaitPacketObjectType, KeGetPreviousMode(), (PVOID *)&packet, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    status = cancel_wait(packet) ? STATUS_CANCELLED : STATUS_SUCCESS;
    ObDereferenceObject(packet);
    return status;
}

void IopInitializeWaitPackets(void)
{
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = IO_COMPLETION_ALL_ACCESS;
    init.CloseProcedure = packet_close;
    ObSetGenericMapping(&init, 0x20001, 0x20002, 0x120000, 0x1F0003);
    WaitPacketObjectType = ObCreateObjectType(L"WaitCompletionPacket", &init);
}
