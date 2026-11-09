/*
 * write-back of small writes on the host backed volumes. appending a log record in a few tiny writes would cost a
 * host trip each, so a handle buffers contiguous writes and sends them together: when the buffer is full, when the
 * handle is flushed, closed or reads, when anything else looks at the file, such as another handle reading it, its
 * size, a directory listing or an open by name, when a write does not continue the buffer, at shutdown, and otherwise
 * ten milliseconds after the write. handles opened for write through or unbuffered i/o never buffer.
 *
 * the guest sees what a file system with its own cache would show: every way to see a file's data or size first
 * flushes the handles buffering for it. host programs see the file at most ten milliseconds late, and a crash of the
 * machine process loses at most that much.
 */
#include "../ntos.h"

#define WB_SIZE (64 * 1024)
#define WB_MAX_WRITE (16 * 1024)
#define WB_MAX_BUFFERS 48
#define WB_TICK_MS 10
#define WB_IDLE_TICKS 3 /* a buffer clean for this many ticks is freed. */
#define WB_BUCKETS 64

static FAST_MUTEX g_lock;
static LIST_ENTRY g_kept; /* buffers held by handles. */
static ULONG g_buffers;
/* per file bucket, buffers with unflushed data: read without the lock to see whether a file has any. */
static volatile LONG g_pending[WB_BUCKETS];
static volatile LONG g_dirty;
/*
 * files with a section or a window in use, which buffer no writes and cannot be truncated. there are few at a time, so
 * a table searched under the lock; the total is also kept apart so writes to unmapped files can skip the search.
 */
#define WB_MAPPED_FILES 256
static struct {
    ULONG Volume;
    ULONG64 FileId;
    LONG Count;
    LONG ImageCount; /* of the count, the windows of image sections. */
} g_mapped_files[WB_MAPPED_FILES];
static volatile LONG g_mapped_total;
static LONG g_mapped_overflow; /* uses the table had no room for: meanwhile every file counts as mapped. */
/*
 * files with more than one name. a buffer is found by the name its handle was opened by, so a look at the file by another
 * name has to flush the buffers of these whatever they were opened by.
 */
#define WB_LINKED_FILES 64
static struct {
    ULONG Volume;
    ULONG64 FileId;
} g_linked_files[WB_LINKED_FILES];
static ULONG g_linked_next;
static KEVENT g_wake;
static volatile LONG g_worker;
static BOOLEAN g_ready;

static ULONG bucket(ULONG volume, ULONG64 file_id)
{
    return (ULONG)(((file_id * 0x9E3779B97F4A7C15ull) >> 58) ^ volume) % WB_BUCKETS;
}

static void mark_dirty(FS_WB *wb)
{
    __atomic_add_fetch(&g_pending[bucket(wb->Volume, wb->FileId)], 1, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&g_dirty, 1, __ATOMIC_SEQ_CST);
}

static void mark_clean(FS_WB *wb)
{
    __atomic_sub_fetch(&g_pending[bucket(wb->Volume, wb->FileId)], 1, __ATOMIC_SEQ_CST);
    __atomic_sub_fetch(&g_dirty, 1, __ATOMIC_SEQ_CST);
}

/* lock held. the buffer is emptied whatever happens. */
static NTSTATUS flush_locked(FS_WB *wb)
{
    if (!wb->Length)
        return STATUS_SUCCESS;
    ULONG64 got = 0;
    ULONG sent = wb->Length;
    ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_WRITE, wb->Host, wb->Start, HalPhysicalOf(wb->Buffer), sent, 0, 0, &got);
    NTSTATUS status = MimeFsHostStatus(hs, FALSE);
    if (NT_SUCCESS(status) && got < sent) {
        KiLogPrintf("fs: the host wrote %llu of %u bytes at %llu of file %llu, the buffer now holds %u", got, (unsigned)sent,
                    wb->Start, wb->FileId, (unsigned)wb->Length);
        status = STATUS_DISK_FULL;
    }
    wb->Length = 0;
    mark_clean(wb);
    if (!NT_SUCCESS(status)) {
        /* the buffered data is lost, so a later write would leave a hole in the file: fail them all. */
        if (NT_SUCCESS(wb->Failed))
            wb->Failed = status;
        KiLogPrintf("fs: %u bytes at %llu of file %llu were not written: %08x", (unsigned)sent, wb->Start, wb->FileId, status);
    }
    return status;
}

static void release_buffer(FS_WB *wb)
{
    RemoveEntryList(&wb->Links);
    ExFreePool(wb->Buffer);
    wb->Buffer = nullptr;
    wb->Kept = FALSE;
    g_buffers--;
}

/* the same file, or one below the directory. names are volume relative and start with a backslash. */
static BOOLEAN name_matches(PCUNICODE_STRING name, PCUNICODE_STRING path, BOOLEAN directory)
{
    if (!name || !name->Length)
        return FALSE;
    if (!directory)
        return RtlEqualUnicodeString(name, path, TRUE);
    ULONG dir_chars = path->Length / sizeof(WCHAR);
    while (dir_chars && path->Buffer[dir_chars - 1] == '\\')
        dir_chars--;
    if (!dir_chars)
        return TRUE;
    if (name->Length / sizeof(WCHAR) <= dir_chars || name->Buffer[dir_chars] != '\\')
        return FALSE;
    UNICODE_STRING head = {(USHORT)(dir_chars * sizeof(WCHAR)), (USHORT)(dir_chars * sizeof(WCHAR)), name->Buffer};
    UNICODE_STRING dir = {head.Length, head.Length, path->Buffer};
    return RtlEqualUnicodeString(&head, &dir, TRUE);
}

/* flushes everything dirty and frees buffers idle for a while. TRUE when none are left. */
static BOOLEAN tick(void)
{
    ExAcquireFastMutex(&g_lock);
    for (PLIST_ENTRY e = g_kept.Flink; e != &g_kept;) {
        FS_WB *wb = CONTAINING_RECORD(e, FS_WB, Links);
        e = e->Flink;
        if (wb->Length) {
            flush_locked(wb);
            wb->Idle = 0;
        } else if (++wb->Idle >= WB_IDLE_TICKS) {
            release_buffer(wb);
        }
    }
    BOOLEAN empty = IsListEmpty(&g_kept);
    ExReleaseFastMutex(&g_lock);
    return empty;
}

static void NTAPI worker(PVOID context)
{
    for (;;) {
        KeWaitForSingleObject(&g_wake, Executive, KernelMode, FALSE, nullptr);
        do {
            LARGE_INTEGER interval = {.QuadPart = -(LONGLONG)WB_TICK_MS * 10000};
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
        } while (!tick());
    }
}

void FsWbInitialize(void)
{
    ExInitializeFastMutex(&g_lock);
    InitializeListHead(&g_kept);
    KeInitializeEvent(&g_wake, SynchronizationEvent, FALSE);
    g_ready = TRUE;
}

void FsWbOpen(FS_WB *wb, ULONG volume, ULONG64 file_id, ULONG64 host, PCUNICODE_STRING name)
{
    memset(wb, 0, sizeof(*wb));
    wb->Volume = volume;
    wb->FileId = file_id;
    wb->Host = host;
    wb->Name = name;
}

void FsWbNoteLinked(ULONG volume, ULONG64 file_id)
{
    ExAcquireFastMutex(&g_lock);
    BOOLEAN known = FALSE;
    for (ULONG i = 0; i < WB_LINKED_FILES && !known; i++)
        known = g_linked_files[i].FileId == file_id && g_linked_files[i].Volume == volume;
    if (!known) {
        g_linked_files[g_linked_next % WB_LINKED_FILES].Volume = volume;
        g_linked_files[g_linked_next++ % WB_LINKED_FILES].FileId = file_id;
    }
    ExReleaseFastMutex(&g_lock);
}

/* lock held. */
static BOOLEAN linked_locked(ULONG volume, ULONG64 file_id)
{
    for (ULONG i = 0; i < WB_LINKED_FILES; i++) {
        if (g_linked_files[i].FileId == file_id && g_linked_files[i].Volume == volume)
            return TRUE;
    }
    return FALSE;
}

void FsWbMapped(ULONG volume, ULONG64 file_id, LONG delta)
{
    ExAcquireFastMutex(&g_lock);
    LONG slot = -1, free_slot = -1;
    for (LONG i = 0; i < WB_MAPPED_FILES; i++) {
        if (g_mapped_files[i].Count && g_mapped_files[i].Volume == volume && g_mapped_files[i].FileId == file_id)
            slot = i;
        else if (!g_mapped_files[i].Count && free_slot < 0)
            free_slot = i;
    }
    if (slot < 0 && delta > 0 && free_slot >= 0) {
        g_mapped_files[free_slot].Volume = volume;
        g_mapped_files[free_slot].FileId = file_id;
        slot = free_slot;
    }
    if (slot >= 0)
        g_mapped_files[slot].Count += delta;
    else
        g_mapped_overflow += delta;
    __atomic_add_fetch(&g_mapped_total, delta, __ATOMIC_SEQ_CST);
    ExReleaseFastMutex(&g_lock);
}

void FsWbImageMapped(ULONG volume, ULONG64 file_id, LONG delta)
{
    ExAcquireFastMutex(&g_lock);
    for (LONG i = 0; i < WB_MAPPED_FILES; i++)
        if (g_mapped_files[i].Count && g_mapped_files[i].Volume == volume && g_mapped_files[i].FileId == file_id) {
            g_mapped_files[i].ImageCount += delta;
            break;
        }
    ExReleaseFastMutex(&g_lock);
}

BOOLEAN FsWbIsImageMapped(ULONG volume, ULONG64 file_id)
{
    if (!__atomic_load_n(&g_mapped_total, __ATOMIC_SEQ_CST))
        return FALSE;
    BOOLEAN mapped = FALSE;
    ExAcquireFastMutex(&g_lock);
    for (LONG i = 0; i < WB_MAPPED_FILES && !mapped; i++)
        mapped = g_mapped_files[i].Count && g_mapped_files[i].ImageCount > 0 && g_mapped_files[i].Volume == volume &&
                 g_mapped_files[i].FileId == file_id;
    ExReleaseFastMutex(&g_lock);
    return mapped;
}

BOOLEAN FsWbAnyMapped(void)
{
    return __atomic_load_n(&g_mapped_total, __ATOMIC_SEQ_CST) != 0;
}

BOOLEAN FsWbIsMapped(ULONG volume, ULONG64 file_id)
{
    if (!__atomic_load_n(&g_mapped_total, __ATOMIC_SEQ_CST))
        return FALSE;
    BOOLEAN mapped = FALSE;
    ExAcquireFastMutex(&g_lock);
    for (LONG i = 0; i < WB_MAPPED_FILES && !mapped; i++)
        mapped = g_mapped_files[i].Count && g_mapped_files[i].Volume == volume && g_mapped_files[i].FileId == file_id;
    ExReleaseFastMutex(&g_lock);
    return mapped;
}

/* lock held. a file the table lost track of counts as mapped, the safe side for a write. */
static BOOLEAN mapped_locked(ULONG volume, ULONG64 file_id)
{
    if (g_mapped_overflow > 0)
        return TRUE;
    for (LONG i = 0; i < WB_MAPPED_FILES; i++)
        if (g_mapped_files[i].Count && g_mapped_files[i].Volume == volume && g_mapped_files[i].FileId == file_id)
            return TRUE;
    return FALSE;
}

BOOLEAN FsWbWrite(FS_WB *wb, ULONG64 offset, const void *data, ULONG length, NTSTATUS *status)
{
    *status = STATUS_SUCCESS;
    if (!g_ready || !length || length > WB_MAX_WRITE || !wb->Host)
        return FALSE;
    /* a view or a window reads the host's pages and must see the write when it returns. what this handle
       buffered before the first one was made was flushed then. */
    if (__atomic_load_n(&g_mapped_total, __ATOMIC_SEQ_CST) > 0 && FsWbIsMapped(wb->Volume, wb->FileId))
        return FALSE;
    BOOLEAN start_worker = FALSE;
    ExAcquireFastMutex(&g_lock);
    /* a window made during this write is counted before it flushes the buffers, which is why this check comes here. */
    if (__atomic_load_n(&g_mapped_total, __ATOMIC_SEQ_CST) > 0 && mapped_locked(wb->Volume, wb->FileId)) {
        ExReleaseFastMutex(&g_lock);
        return FALSE;
    }
    if (!NT_SUCCESS(wb->Failed)) {
        /* a flush failed after its write had returned: every later write reports it and none lands. */
        *status = wb->Failed;
        ExReleaseFastMutex(&g_lock);
        return TRUE;
    }
    /* data other handles buffered for the file is older than this write and must not land after it. */
    if (__atomic_load_n(&g_pending[bucket(wb->Volume, wb->FileId)], __ATOMIC_SEQ_CST) > (wb->Length ? 1 : 0)) {
        for (PLIST_ENTRY e = g_kept.Flink; e != &g_kept; e = e->Flink) {
            FS_WB *other = CONTAINING_RECORD(e, FS_WB, Links);
            if (other != wb && other->Length && other->Volume == wb->Volume && other->FileId == wb->FileId)
                flush_locked(other);
        }
    }
    wb->Idle = 0;
    if (wb->Length) {
        ULONG64 end = wb->Start + wb->Length;
        if (offset >= wb->Start && offset <= end && offset + length - wb->Start <= WB_SIZE) {
            /* inside the buffer or right after it. */
            memcpy(wb->Buffer + (offset - wb->Start), data, length);
            if (offset + length > end)
                wb->Length = (ULONG)(offset + length - wb->Start);
            ExReleaseFastMutex(&g_lock);
            return TRUE;
        }
        *status = flush_locked(wb);
        if (!NT_SUCCESS(*status)) {
            ExReleaseFastMutex(&g_lock);
            return TRUE;
        }
    }
    if (!wb->Buffer) {
        if (g_buffers >= WB_MAX_BUFFERS) {
            ExReleaseFastMutex(&g_lock);
            return FALSE;
        }
        wb->Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, WB_SIZE, KI_TAG('M', 'f', 'W', 'b'));
        if (!wb->Buffer) {
            ExReleaseFastMutex(&g_lock);
            return FALSE;
        }
        wb->Kept = TRUE;
        BOOLEAN first = IsListEmpty(&g_kept);
        InsertTailList(&g_kept, &wb->Links);
        g_buffers++;
        if (first) {
            KeSetEvent(&g_wake, IO_NO_INCREMENT, FALSE);
            start_worker = !__atomic_exchange_n(&g_worker, 1, __ATOMIC_SEQ_CST);
        }
    }
    wb->Start = offset;
    memcpy(wb->Buffer, data, length);
    wb->Length = length;
    mark_dirty(wb);
    ExReleaseFastMutex(&g_lock);
    if (start_worker && !NT_SUCCESS(PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, worker, nullptr)))
        __atomic_store_n(&g_worker, 0, __ATOMIC_SEQ_CST);
    return TRUE;
}

ULONG64 FsWbPendingEnd(FS_WB *wb)
{
    if (!wb->Kept)
        return 0;
    ExAcquireFastMutex(&g_lock);
    ULONG64 end = wb->Length ? wb->Start + wb->Length : 0;
    ExReleaseFastMutex(&g_lock);
    return end;
}

void FsWbSyncOthers(FS_WB *wb)
{
    if (!wb->Host || __atomic_load_n(&g_pending[bucket(wb->Volume, wb->FileId)], __ATOMIC_SEQ_CST) <= (wb->Length ? 1 : 0))
        return;
    ExAcquireFastMutex(&g_lock);
    for (PLIST_ENTRY e = g_kept.Flink; e != &g_kept; e = e->Flink) {
        FS_WB *other = CONTAINING_RECORD(e, FS_WB, Links);
        if (other != wb && other->Length && other->Volume == wb->Volume && other->FileId == wb->FileId)
            flush_locked(other);
    }
    ExReleaseFastMutex(&g_lock);
}

NTSTATUS FsWbSync(FS_WB *wb)
{
    if (!wb->Host || (NT_SUCCESS(wb->Failed) && !__atomic_load_n(&g_pending[bucket(wb->Volume, wb->FileId)], __ATOMIC_SEQ_CST)))
        return STATUS_SUCCESS;
    NTSTATUS status = STATUS_SUCCESS;
    ExAcquireFastMutex(&g_lock);
    for (PLIST_ENTRY e = g_kept.Flink; e != &g_kept; e = e->Flink) {
        FS_WB *other = CONTAINING_RECORD(e, FS_WB, Links);
        if (other->Length && other->Volume == wb->Volume && other->FileId == wb->FileId) {
            NTSTATUS st = flush_locked(other);
            if (other == wb && !NT_SUCCESS(st))
                status = st;
        }
    }
    if (NT_SUCCESS(status) && !NT_SUCCESS(wb->Failed)) {
        status = wb->Failed;
    }
    ExReleaseFastMutex(&g_lock);
    return status;
}

void FsWbSyncPath(ULONG volume, PCUNICODE_STRING path, BOOLEAN directory)
{
    if (!__atomic_load_n(&g_dirty, __ATOMIC_SEQ_CST))
        return;
    ExAcquireFastMutex(&g_lock);
    for (PLIST_ENTRY e = g_kept.Flink; e != &g_kept; e = e->Flink) {
        FS_WB *wb = CONTAINING_RECORD(e, FS_WB, Links);
        if (wb->Length && wb->Volume == volume &&
            (name_matches(wb->Name, path, directory) || (!directory && linked_locked(volume, wb->FileId))))
            flush_locked(wb);
    }
    ExReleaseFastMutex(&g_lock);
}

void FsWbNameLock(void)
{
    ExAcquireFastMutex(&g_lock);
}

void FsWbNameUnlock(void)
{
    ExReleaseFastMutex(&g_lock);
}

void FsWbClose(FS_WB *wb)
{
    if (!wb->Kept)
        return;
    ExAcquireFastMutex(&g_lock);
    flush_locked(wb);
    if (wb->Kept)
        release_buffer(wb);
    ExReleaseFastMutex(&g_lock);
}

static void flush_all(ULONG volume, BOOLEAN every)
{
    if (!g_ready)
        return;
    ExAcquireFastMutex(&g_lock);
    for (PLIST_ENTRY e = g_kept.Flink; e != &g_kept; e = e->Flink) {
        FS_WB *wb = CONTAINING_RECORD(e, FS_WB, Links);
        if (every || wb->Volume == volume)
            flush_locked(wb);
    }
    ExReleaseFastMutex(&g_lock);
}

void FsWbFlushAll(void)
{
    flush_all(0, TRUE);
}

void FsWbFlushVolume(ULONG volume)
{
    flush_all(volume, FALSE);
}
