/*
 * user mode requests a mime driver answers itself and may leave pending.
 * like windows' i/o manager does for an irp, the caller's status block,
 * event, apc or completion port are filled when the answer comes; the
 * caller of a synchronous file sleeps until then.
 */
#include "../ntos.h"

IO_USER_REQUEST *IopNewUserRequest(PFILE_OBJECT file, HANDLE event, PVOID apc_routine, PVOID apc_context,
                                   PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, NTSTATUS *status)
{
    IO_USER_REQUEST *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), KI_TAG('I', 'o', 'U', 'r'));
    if (!r) {
        *status = STATUS_INSUFFICIENT_RESOURCES;
        return nullptr;
    }
    memset(r, 0, sizeof(*r));
    if (event) {
        *status = ObReferenceHandle(event, ExEventType, KeGetPreviousMode(), (PVOID *)&r->Event, nullptr);
        if (!NT_SUCCESS(*status)) {
            ExFreePool(r);
            return nullptr;
        }
        KeClearEvent(r->Event);
    } else if (!(file->Flags & FO_SKIP_SET_EVENT)) {
        /* a request that names no event is waited for on the file, which signals again when this one is done. */
        KeClearEvent(&file->Event);
    }
    r->Process = PsCurrentProcess();
    ObReferenceObject(r->Process);
    r->Thread = PsCurrentThread();
    ObReferenceObject(r->Thread);
    r->File = file;
    ObReferenceObject(file);
    r->ApcRoutine = apc_routine;
    r->ApcContext = apc_context;
    r->Iosb = iosb;
    r->Buffer = buffer;
    r->Length = length;
    r->Waiter = (file->Flags & FO_SYNCHRONOUS_IO) != 0;
    KeInitializeEvent(&r->Done, NotificationEvent, FALSE);
    InitializeListHead(&r->Links);
    *status = STATUS_SUCCESS;
    return r;
}

static void free_request(IO_USER_REQUEST *r)
{
    if (r->Event)
        ObDereferenceObject(r->Event);
    ObDereferenceObject(r->File);
    ObDereferenceObject(r->Thread);
    ObDereferenceObject(r->Process);
    ExFreePool(r);
}

NTSTATUS IopCopyToProcess(PEPROCESS process, PVOID dst, const void *src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (process == PsCurrentProcess())
        return MimeCopyToUser(dst, src, length);
    SIZE_T done = MmCopyToProcessFast(process, dst, src, length);
    if (done == length)
        return STATUS_SUCCESS;
    PKPROCESS state;
    KeStackAttachProcess(&process->Pcb, &state);
    NTSTATUS status = MimeCopyToUser((PUCHAR)dst + done, (const UCHAR *)src + done, length - done);
    KeUnstackDetachProcess(&state);
    return status;
}

/* works from whatever thread the answer comes on. */
NTSTATUS IopCopyToRequester(IO_USER_REQUEST *r, PVOID dst, const void *src, SIZE_T length)
{
    return IopCopyToProcess(r->Process, dst, src, length);
}

/* the status block, the event, then the apc or the completion port, which an error before pending skips. */
static void finish(IO_USER_REQUEST *r, NTSTATUS status, ULONG_PTR information)
{
    r->Status = status;
    r->Information = information;
    if (r->Iosb) {
        ULONG64 image[2];
        PVOID address;
        ULONG length = IopIosbImage(r->Iosb, status, information, &address, image);
        IopCopyToRequester(r, address, image, length);
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

void IopCompleteUserRequest(IO_USER_REQUEST *r, NTSTATUS status, ULONG_PTR information)
{
    finish(r, status, information);
    if (r->Waiter)
        KeSetEvent(&r->Done, IO_NO_INCREMENT, FALSE);
    else
        free_request(r);
}

NTSTATUS IopImmediateUserRequest(IO_USER_REQUEST *r, NTSTATUS status, ULONG_PTR information)
{
    r->Immediate = TRUE;
    finish(r, status, information);
    free_request(r);
    return status;
}

/* called with the driver's lock released: a synchronous caller sleeps until the answer, others go on. */
NTSTATUS IopPendUserRequest(IO_USER_REQUEST *r)
{
    if (!r->Waiter)
        return STATUS_PENDING;
    if (KeWaitForSingleObject(&r->Done, UserRequest, UserMode, FALSE, nullptr) != STATUS_WAIT_0) {
        /* the thread is ending: its requests on the file are cancelled, which completes this one. */
        if (IopIsPad(r->File))
            PadCancel(r->File, r->Thread, nullptr);
        else if (IopIsMimeFs(r->File))
            FsLocksCancel(r->File, r->Thread, r->Iosb);
        KeWaitForSingleObject(&r->Done, Executive, KernelMode, FALSE, nullptr);
    }
    NTSTATUS status = r->Status;
    free_request(r);
    return status;
}
