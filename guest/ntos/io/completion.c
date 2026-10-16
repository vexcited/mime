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
    return STATUS_SUCCESS;
}

NTSTATUS IopQueueCompletion(PVOID port, PVOID key, PVOID apc_context, NTSTATUS status, ULONG_PTR information)
{
    return queue_packet(port, key, apc_context, status, information);
}

/*
 * called when a request of a user completes: posts its packet when the file
 * is bound to a port, unless it succeeded before going pending on a file
 * that asked to skip the port then (FILE_SKIP_COMPLETION_PORT_ON_SUCCESS).
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

/* binds a file object to a port, once, like windows. */
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

/* drops the port reference of a closing file object. */
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
        /* more only when they are already queued. */
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

/* runs routine(context, iosb, 0) in the requesting thread at its next alertable wait. */
void IopQueueCompletionApc(PETHREAD thread, PVOID routine, PVOID context, PIO_STATUS_BLOCK iosb)
{
    if (!routine)
        return;
    PKAPC apc = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KAPC), KI_TAG('I', 'o', 'A', 'p'));
    if (!apc)
        return;
    KeInitializeApc(apc, &thread->Tcb, OriginalApcEnvironment, apc_free, nullptr, (PKNORMAL_ROUTINE)routine, UserMode,
                    context);
    if (!KeInsertQueueApc(apc, iosb, nullptr, IO_NO_INCREMENT))
        ExFreePool(apc);
}
