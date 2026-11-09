#include "../ntos.h"

#define STATUS_INVALID_LOCK_RANGE ((NTSTATUS)0xC00001A1L)
#define STATUS_FILE_CLOSED ((NTSTATUS)0xC0000128L)
#define STATUS_FILE_LOCK_CONFLICT ((NTSTATUS)0xC0000054L)
#define STATUS_LOCK_NOT_GRANTED ((NTSTATUS)0xC0000055L)
#define STATUS_RANGE_NOT_LOCKED ((NTSTATUS)0xC000007EL)

typedef struct FS_RANGE_LOCK {
    struct FS_RANGE_LOCK *Next;
    PFILE_OBJECT File;
    PEPROCESS Process;
    ULONG Volume;
    ULONG64 FileId, Start, Length;
    ULONG Key;
    BOOLEAN Exclusive;
    IO_USER_REQUEST *Request;
} FS_RANGE_LOCK;

static FAST_MUTEX g_lock;
static FS_RANGE_LOCK *g_active, *g_pending;

void FsLocksInitialize(void)
{
    ExInitializeFastMutex(&g_lock);
}

static BOOLEAN overlaps(const FS_RANGE_LOCK *a, const FS_RANGE_LOCK *b)
{
    if ((!a->Length && !b->Length) || (!a->Length && !a->Start) || (!b->Length && !b->Start))
        return FALSE;
    return a->Volume == b->Volume && a->FileId == b->FileId &&
           a->Start <= b->Start + b->Length - 1 && b->Start <= a->Start + a->Length - 1;
}

static BOOLEAN same_owner(const FS_RANGE_LOCK *a, const FS_RANGE_LOCK *b)
{
    return a->File == b->File && a->Process == b->Process && a->Key == b->Key;
}

static BOOLEAN conflict(const FS_RANGE_LOCK *wanted)
{
    for (FS_RANGE_LOCK *lock = g_active; lock; lock = lock->Next)
        if (overlaps(wanted, lock) && (wanted->Exclusive || (lock->Exclusive && !same_owner(wanted, lock))))
            return TRUE;
    return FALSE;
}

static BOOLEAN has_handle(PEPROCESS process, PFILE_OBJECT file)
{
    PHANDLE_TABLE table = &process->HandleTable;
    ULONG64 state = ObLockHandleTable(table);
    BOOLEAN found = FALSE;
    for (ULONG i = 0; i < table->Capacity; i++)
        if (table->Entries[i].Object == file) {
            found = TRUE;
            break;
        }
    ObUnlockHandleTable(table, state);
    return found;
}

/* queue completions until the lock protecting the ranges has been released. */
static void answered(PLIST_ENTRY ready, IO_USER_REQUEST *request, NTSTATUS status)
{
    request->Status = status;
    InsertTailList(ready, &request->Links);
}

static void finish(PLIST_ENTRY ready)
{
    while (!IsListEmpty(ready)) {
        IO_USER_REQUEST *request = CONTAINING_RECORD(RemoveHeadList(ready), IO_USER_REQUEST, Links);
        IopCompleteUserRequest(request, request->Status, 0);
    }
}

static void grant_waiters(PLIST_ENTRY ready)
{
    FS_RANGE_LOCK **link = &g_pending;
    while (*link) {
        FS_RANGE_LOCK *lock = *link;
        if (conflict(lock)) {
            link = &lock->Next;
            continue;
        }
        *link = lock->Next;
        answered(ready, lock->Request, STATUS_SUCCESS);
        lock->Request = nullptr;
        lock->Next = g_active;
        g_active = lock;
    }
}

static NTSTATUS capture_range(PFILE_OBJECT file, PLARGE_INTEGER offset, PLARGE_INTEGER length, ULONG key,
                               FS_RANGE_LOCK *range)
{
    LARGE_INTEGER start, size;
    NTSTATUS status = STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode) {
        status = KiCopyFromUser(&start, offset, sizeof(start));
        if (NT_SUCCESS(status))
            status = KiCopyFromUser(&size, length, sizeof(size));
    } else {
        start = *offset;
        size = *length;
    }
    if (!NT_SUCCESS(status))
        return status;
    if (size.QuadPart && (ULONG64)start.QuadPart + (ULONG64)size.QuadPart - 1 < (ULONG64)start.QuadPart)
        return STATUS_INVALID_LOCK_RANGE;
    if (!MimeFsLockIdentity(file, &range->Volume, &range->FileId))
        return STATUS_INVALID_DEVICE_REQUEST;
    range->File = file;
    range->Process = PsCurrentProcess();
    range->Start = start.QuadPart;
    range->Length = size.QuadPart;
    range->Key = key;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtLockFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID context, PIO_STATUS_BLOCK iosb,
                           PLARGE_INTEGER offset, PLARGE_INTEGER length, ULONG key, BOOLEAN immediate, BOOLEAN exclusive)
{
    PVOID routine = (PVOID)apc;
    if (KeGetPreviousMode() == UserMode)
        IopTakeIosb32(&routine, &iosb);
    PFILE_OBJECT file;
    ACCESS_MASK access;
    NTSTATUS status = ObReferenceHandle(handle, IoFileObjectTypeObject, KeGetPreviousMode(), (PVOID *)&file, &access);
    if (!NT_SUCCESS(status))
        return status;
    FS_RANGE_LOCK *lock = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*lock), KI_TAG('F', 's', 'L', 'k'));
    if (!lock) {
        ObDereferenceObject(file);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = access & (FILE_READ_DATA | FILE_WRITE_DATA) ? capture_range(file, offset, length, key, lock) : STATUS_ACCESS_DENIED;
    IO_USER_REQUEST *request = nullptr;
    if (NT_SUCCESS(status))
        request = IopNewUserRequest(file, event, routine, context, iosb, nullptr, 0, &status);
    if (!request) {
        ExFreePool(lock);
        ObDereferenceObject(file);
        return status;
    }
    if (!event)
        KeClearEvent(&file->Event);
    lock->Exclusive = exclusive;
    BOOLEAN waiter = request->Waiter, pending = FALSE;
    ExAcquireFastMutex(&g_lock);
    if (!has_handle(PsCurrentProcess(), file)) {
        status = STATUS_FILE_CLOSED;
    } else if (!conflict(lock)) {
        lock->Next = g_active;
        g_active = lock;
        lock = nullptr;
    } else if (immediate) {
        status = STATUS_LOCK_NOT_GRANTED;
    } else {
        ULONG64 image[2];
        PVOID address;
        ULONG bytes = IopIosbImage(iosb, STATUS_PENDING, 0, &address, image);
        status = IopCopyToRequester(request, address, image, bytes);
        if (NT_SUCCESS(status)) {
            lock->Request = request;
            FS_RANGE_LOCK **tail = &g_pending;
            while (*tail)
                tail = &(*tail)->Next;
            *tail = lock;
            lock = nullptr;
            pending = TRUE;
        }
    }
    ExReleaseFastMutex(&g_lock);
    if (lock)
        ExFreePool(lock);
    ObDereferenceObject(file);
    if (pending)
        return waiter ? IopPendUserRequest(request) : STATUS_PENDING;
    return IopImmediateUserRequest(request, status, 0);
}

NTSTATUS NTAPI NtUnlockFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PLARGE_INTEGER offset, PLARGE_INTEGER length, ULONG key)
{
    PFILE_OBJECT file;
    NTSTATUS status = ObReferenceHandle(handle, IoFileObjectTypeObject, KeGetPreviousMode(), (PVOID *)&file, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    FS_RANGE_LOCK range = {};
    status = capture_range(file, offset, length, key, &range);
    LIST_ENTRY ready;
    InitializeListHead(&ready);
    if (NT_SUCCESS(status)) {
        ExAcquireFastMutex(&g_lock);
        FS_RANGE_LOCK **match = nullptr;
        for (FS_RANGE_LOCK **link = &g_active; *link; link = &(*link)->Next) {
            FS_RANGE_LOCK *lock = *link;
            if (same_owner(lock, &range) && lock->Start == range.Start && lock->Length == range.Length) {
                match = link;
                if (lock->Exclusive)
                    break;
            }
        }
        status = STATUS_RANGE_NOT_LOCKED;
        if (match) {
            FS_RANGE_LOCK *lock = *match;
            *match = lock->Next;
            ExFreePool(lock);
            status = STATUS_SUCCESS;
            grant_waiters(&ready);
        }
        ExReleaseFastMutex(&g_lock);
    }
    finish(&ready);
    ULONG64 image[2];
    PVOID address;
    ULONG bytes = IopIosbImage(iosb, status, 0, &address, image);
    NTSTATUS copied = KeGetPreviousMode() == UserMode ? KiCopyToUser(address, image, bytes) :
                                                       (memcpy(address, image, bytes), STATUS_SUCCESS);
    ObDereferenceObject(file);
    return NT_SUCCESS(copied) ? status : copied;
}

NTSTATUS FsLocksCancel(PFILE_OBJECT file, PETHREAD thread, PVOID iosb)
{
    LIST_ENTRY ready;
    InitializeListHead(&ready);
    ExAcquireFastMutex(&g_lock);
    for (FS_RANGE_LOCK **link = &g_pending; *link;) {
        FS_RANGE_LOCK *lock = *link;
        if (lock->File != file || lock->Process != PsCurrentProcess() ||
            (thread && lock->Request->Thread != thread) || (iosb && ((ULONG_PTR)lock->Request->Iosb & ~(ULONG_PTR)1) != ((ULONG_PTR)iosb & ~(ULONG_PTR)1))) {
            link = &lock->Next;
            continue;
        }
        *link = lock->Next;
        answered(&ready, lock->Request, STATUS_CANCELLED);
        ExFreePool(lock);
    }
    ExReleaseFastMutex(&g_lock);
    BOOLEAN found = !IsListEmpty(&ready);
    finish(&ready);
    return found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

void FsLocksCleanup(PFILE_OBJECT file, PEPROCESS process)
{
    if (!IopIsMimeFs(file))
        return;
    LIST_ENTRY ready;
    InitializeListHead(&ready);
    ExAcquireFastMutex(&g_lock);
    if (has_handle(process, file)) {
        ExReleaseFastMutex(&g_lock);
        return;
    }
    for (FS_RANGE_LOCK **link = &g_active; *link;) {
        FS_RANGE_LOCK *lock = *link;
        if (lock->File != file || lock->Process != process) {
            link = &lock->Next;
            continue;
        }
        *link = lock->Next;
        ExFreePool(lock);
    }
    for (FS_RANGE_LOCK **link = &g_pending; *link;) {
        FS_RANGE_LOCK *lock = *link;
        if (lock->File != file || lock->Process != process) {
            link = &lock->Next;
            continue;
        }
        *link = lock->Next;
        answered(&ready, lock->Request, STATUS_CANCELLED);
        ExFreePool(lock);
    }
    grant_waiters(&ready);
    ExReleaseFastMutex(&g_lock);
    finish(&ready);
}

/* whether any byte range of the file is locked, so a write that cannot name its offset yet need not look for one. */
BOOLEAN FsLocksHeld(PFILE_OBJECT file)
{
    FS_RANGE_LOCK probe = {};
    if (!MimeFsLockIdentity(file, &probe.Volume, &probe.FileId))
        return FALSE;
    BOOLEAN held = FALSE;
    ExAcquireFastMutex(&g_lock);
    for (FS_RANGE_LOCK *lock = g_active; lock && !held; lock = lock->Next)
        held = lock->Volume == probe.Volume && lock->FileId == probe.FileId;
    ExReleaseFastMutex(&g_lock);
    return held;
}

NTSTATUS FsLocksCheck(PFILE_OBJECT file, BOOLEAN write, ULONG64 start, ULONG length, ULONG key)
{
    FS_RANGE_LOCK range = {.File = file, .Process = PsCurrentProcess(), .Start = start, .Length = length, .Key = key};
    if (!length || !MimeFsLockIdentity(file, &range.Volume, &range.FileId))
        return STATUS_SUCCESS;
    NTSTATUS status = STATUS_SUCCESS;
    ExAcquireFastMutex(&g_lock);
    for (FS_RANGE_LOCK *lock = g_active; lock; lock = lock->Next)
        if (overlaps(lock, &range) && (write ? !lock->Exclusive || !same_owner(lock, &range) :
                                               lock->Exclusive && !same_owner(lock, &range))) {
            status = STATUS_FILE_LOCK_CONFLICT;
            break;
        }
    ExReleaseFastMutex(&g_lock);
    return status;
}
