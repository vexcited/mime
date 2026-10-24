/* nonvolatile changes are a checksummed write-ahead log beside the imported hives. */
#include "cm.h"

static PFILE_OBJECT CmpJournalFile;
static ULONG64 CmpJournalOffset;
static ULONG64 CmpJournalSequence = 1;
static NTSTATUS CmpJournalStatus;
static BOOLEAN CmpJournalReady;

/*
 * a record reaches the host file as soon as it is appended, which survives the program ending. making it
 * durable on the host disk is a flush costing milliseconds, so a worker does one for all the records appended
 * since the last flush, while appends go on.
 */
static ULONG64 CmpJournalDurable;
static KMUTEX CmpJournalSyncLock;
static KEVENT CmpJournalWake;
static volatile LONG CmpJournalPending;
static BOOLEAN CmpJournalWorker;

/* unknown operations met during replay, each logged once. */
static ULONG CmpJournalSkipped[8];
static ULONG CmpJournalSkippedCount;

static void note_unknown(ULONG operation, ULONG64 offset)
{
    for (ULONG i = 0; i < CmpJournalSkippedCount; i++)
        if (CmpJournalSkipped[i] == operation)
            return;
    if (CmpJournalSkippedCount < RTL_NUMBER_OF(CmpJournalSkipped))
        CmpJournalSkipped[CmpJournalSkippedCount++] = operation;
    KiLogPrintf("registry: journal skips records of unknown operation %u, first at offset %llu", operation, offset);
}

static NTSTATUS flush(void)
{
    return IopSynchronousRequest(CmpJournalFile, IRP_MJ_FLUSH_BUFFERS, 0, nullptr, 0, nullptr, nullptr);
}

static NTSTATUS truncate_tail(void)
{
    /* extending a short file would leave a hole of zeros with records after it. */
    ULONG64 length;
    NTSTATUS status = IoQueryFileSize(CmpJournalFile, &length);
    if (!NT_SUCCESS(status))
        return status;
    if (length < CmpJournalOffset)
        return STATUS_REGISTRY_CORRUPT;
    /* the flush below makes the whole file durable up to the new end. */
    FILE_END_OF_FILE_INFORMATION end;
    end.EndOfFile.QuadPart = (LONGLONG)CmpJournalOffset;
    IO_STACK_LOCATION params = {};
    params.Parameters.SetFile.Length = sizeof(end);
    params.Parameters.SetFile.FileInformationClass = FileEndOfFileInformation;
    status = IopSynchronousRequest(CmpJournalFile, IRP_MJ_SET_INFORMATION, 0, &end, sizeof(end), &params, nullptr);
    if (NT_SUCCESS(status))
        status = flush();
    if (NT_SUCCESS(status))
        CmpJournalDurable = CmpJournalOffset;
    return status;
}

/*
 * the log holds tens of thousands of records and each read is a host call, so replay reads a block at a time
 * and takes the records out of that.
 */
#define CM_REPLAY_BLOCK (64 * 1024)
static PUCHAR CmpReplayBlock;
static ULONG64 CmpReplayBlockOffset;
static ULONG CmpReplayBlockLength;
static ULONG64 CmpReplayFileLength;

NTSTATUS CmpJournalReadBytes(void *data, ULONG bytes, ULONG64 offset)
{
    ULONG got = 0;
    if (CmpReplayBlock && bytes <= CM_REPLAY_BLOCK && offset + bytes <= CmpReplayFileLength) {
        if (offset < CmpReplayBlockOffset || offset + bytes > CmpReplayBlockOffset + CmpReplayBlockLength) {
            ULONG64 room = CmpReplayFileLength - offset;
            ULONG want = room < CM_REPLAY_BLOCK ? (ULONG)room : CM_REPLAY_BLOCK;
            NTSTATUS status = IoReadFileObject(CmpJournalFile, CmpReplayBlock, want, offset, &got);
            if (!NT_SUCCESS(status))
                return status;
            if (got != want)
                return STATUS_REGISTRY_CORRUPT;
            CmpReplayBlockOffset = offset;
            CmpReplayBlockLength = got;
        }
        if (offset + bytes > CmpReplayBlockOffset + CmpReplayBlockLength)
            return STATUS_REGISTRY_CORRUPT;
        memcpy(data, CmpReplayBlock + (offset - CmpReplayBlockOffset), bytes);
        return STATUS_SUCCESS;
    }
    NTSTATUS status = IoReadFileObject(CmpJournalFile, data, bytes, offset, &got);
    if (!NT_SUCCESS(status))
        return status;
    return got == bytes ? STATUS_SUCCESS : STATUS_REGISTRY_CORRUPT;
}

/* the longest zero tail taken for a torn flush; one flush covers far less. */
#define TORN_TAIL_MAX (1u << 20)

static BOOLEAN tail_is_zero(ULONG64 offset, ULONG64 length)
{
    if (length - offset > TORN_TAIL_MAX)
        return FALSE;
    PUCHAR chunk = ExAllocatePool2(POOL_FLAG_NON_PAGED, 4096, KI_TAG('C', 'm', 'J', 'z'));
    if (!chunk)
        return FALSE;
    BOOLEAN zero = TRUE;
    while (zero && offset < length) {
        ULONG bytes = length - offset < 4096 ? (ULONG)(length - offset) : 4096;
        if (!NT_SUCCESS(CmpJournalReadBytes(chunk, bytes, offset)))
            zero = FALSE;
        for (ULONG i = 0; zero && i < bytes; i++)
            zero = chunk[i] == 0;
        offset += bytes;
    }
    ExFreePool(chunk);
    return zero;
}

/*
 * a crash between a write and its flush can leave the end of the file unwritten or half written, in any order
 * the host filesystem likes. a partial last record, or zeros to the end, is such a tail and is dropped. damage
 * followed by valid data is real corruption.
 */
static NTSTATUS drop_torn_tail(const char *why)
{
    KiLogPrintf("registry: journal drops a torn %s at offset %llu", why, CmpJournalOffset);
    return truncate_tail();
}

/* a journal this long is considered for compaction, see compact.c. */
#define CM_COMPACT_MIN_BYTES (2u << 20)

static NTSTATUS replay(PVOID *compact)
{
    static UCHAR small[4096];
    ULONG64 length;
    NTSTATUS status = IoQueryFileSize(CmpJournalFile, &length);
    if (!NT_SUCCESS(status))
        return status;
    if (length > CM_JOURNAL_MAX_FILE)
        return STATUS_REGISTRY_CORRUPT;
    CmpReplayFileLength = length;
    *compact = length >= CM_COMPACT_MIN_BYTES ? CmpCompactBegin() : nullptr;
    while (CmpJournalOffset < length) {
        if (length - CmpJournalOffset < CM_JOURNAL_HEADER)
            return truncate_tail();
        UCHAR header[CM_JOURNAL_HEADER];
        status = CmpJournalReadBytes(header, sizeof(header), CmpJournalOffset);
        if (!NT_SUCCESS(status))
            return status;
        CmJournalRecord record;
        if (!cm_journal_decode(header, sizeof(header), &record))
            return tail_is_zero(CmpJournalOffset, length) ? drop_torn_tail("run of zeros") : STATUS_REGISTRY_CORRUPT;
        ULONG size = cm_journal_size(&record);
        if (size > length - CmpJournalOffset)
            return truncate_tail();
        /* most records are small and replay reuses one buffer for them. */
        PUCHAR bytes = size <= sizeof(small) ? small : ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('C', 'm', 'J', 'r'));
        if (!bytes)
            return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(bytes, header, sizeof(header));
        status = CmpJournalReadBytes(bytes + sizeof(header), size - sizeof(header), CmpJournalOffset + sizeof(header));
        BOOLEAN torn = FALSE;
        if (NT_SUCCESS(status)) {
            if (!cm_journal_validate(bytes, size, CmpJournalSequence)) {
                torn = size == length - CmpJournalOffset;
                status = STATUS_REGISTRY_CORRUPT;
            } else if (!cm_journal_known(record.operation))
                note_unknown(record.operation, CmpJournalOffset);
            else
                status = CmpRestoreJournal(&record, bytes + sizeof(header));
            if (NT_SUCCESS(status) && *compact)
                CmpCompactNote(*compact, CmpJournalOffset, &record, bytes + sizeof(header));
        }
        if (bytes != small)
            ExFreePool(bytes);
        if (torn)
            return drop_torn_tail("record");
        if (!NT_SUCCESS(status))
            return status;
        CmpJournalOffset += size;
        CmpJournalSequence++;
    }
    return STATUS_SUCCESS;
}

void CmpJournalInitialize(void)
{
    CmpJournalSkippedCount = 0;
    UNICODE_STRING path = RTL_CONSTANT_STRING(L"\\??\\C:\\Windows\\System32\\config\\registry.journal");
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    HANDLE handle;
    IO_STATUS_BLOCK io;
    CmpJournalStatus = IopCreateFile(&handle, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, &attributes,
                                     &io, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN_IF,
                                     FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_WRITE_THROUGH,
                                     KernelMode);
    if (NT_SUCCESS(CmpJournalStatus)) {
        CmpJournalStatus = ObReferenceHandle(handle, IoFileObjectTypeObject, KernelMode,
                                              (PVOID *)&CmpJournalFile, nullptr);
        ZwClose(handle);
    }
    if (NT_SUCCESS(CmpJournalStatus)) {
        CmpReplayBlock = ExAllocatePool2(POOL_FLAG_NON_PAGED, CM_REPLAY_BLOCK, KI_TAG('C', 'm', 'J', 'b'));
        CmpReplayBlockLength = 0;
        PVOID compact = nullptr;
        CmpJournalStatus = replay(&compact);
        if (compact && NT_SUCCESS(CmpJournalStatus))
            CmpCompactFinish(compact, &CmpJournalFile, &CmpJournalOffset, &CmpJournalSequence);
        else if (compact)
            CmpCompactAbandon(compact);
        if (CmpReplayBlock)
            ExFreePool(CmpReplayBlock);
        CmpReplayBlock = nullptr;
    }
    if (NT_SUCCESS(CmpJournalStatus))
        CmpJournalDurable = CmpJournalOffset;
    if (!NT_SUCCESS(CmpJournalStatus))
        KiLogPrintf("registry: journal unavailable at offset %llu: %08x", CmpJournalOffset, CmpJournalStatus);
}

void CmpJournalEnable(void)
{
    CmpJournalReady = TRUE;
}

/* one flush covers every record written before it began. */
static NTSTATUS make_durable(void)
{
    KeWaitForSingleObject(&CmpJournalSyncLock, Executive, KernelMode, FALSE, nullptr);
    ULONG64 target = CmpJournalOffset;
    NTSTATUS status = NT_SUCCESS(CmpJournalStatus) ? STATUS_SUCCESS : CmpJournalStatus;
    if (NT_SUCCESS(status) && CmpJournalDurable < target) {
        status = flush();
        if (NT_SUCCESS(status)) {
            CmpJournalDurable = target;
        } else {
            /* records callers were told were saved may not be, so nothing more is appended after them. */
            CmpJournalStatus = status;
            KiLogPrintf("registry: journal flush failed at offset %llu: %08x", target, status);
        }
    }
    KeReleaseMutex(&CmpJournalSyncLock, FALSE);
    return status;
}

static void NTAPI journal_worker(PVOID)
{
    for (;;) {
        KeWaitForSingleObject(&CmpJournalWake, Executive, KernelMode, FALSE, nullptr);
        __atomic_store_n(&CmpJournalPending, 0, __ATOMIC_SEQ_CST);
        make_durable();
    }
}

void CmpJournalStart(void)
{
    KeInitializeMutex(&CmpJournalSyncLock, 0);
    KeInitializeEvent(&CmpJournalWake, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(CmpJournalStatus))
        return;
    NTSTATUS status = PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, journal_worker, nullptr);
    if (NT_SUCCESS(status))
        CmpJournalWorker = TRUE;
    else
        KiLogPrintf("registry: journal has no flush worker: %08x", status);
}

void CmpJournalShutdown(void)
{
    if (CmpJournalFile && CmpJournalReady)
        make_durable();
}

NTSTATUS CmpJournalAppend(CM_NODE *node, ULONG operation, ULONG type, const WCHAR *name, ULONG chars,
                         const void *data, ULONG length, LARGE_INTEGER last_write)
{
    if (!CmpJournalReady || (node->Flags & CM_NODE_VOLATILE))
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(CmpJournalStatus))
        return CmpJournalStatus;
    ULONG path_chars = CmpFullPath(node, nullptr, 0);
    if (path_chars > CM_JOURNAL_MAX_PATH / sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;
    if (chars > CM_JOURNAL_MAX_NAME / sizeof(WCHAR) || length > CM_JOURNAL_MAX_DATA)
        return STATUS_INVALID_PARAMETER;
    CmJournalRecord record = {CmpJournalSequence, operation, type, path_chars * sizeof(WCHAR),
                              chars * sizeof(WCHAR), length, (ULONG64)last_write.QuadPart};
    ULONG size = cm_journal_size(&record);
    if (size > CM_JOURNAL_MAX_FILE - CmpJournalOffset)
        return STATUS_DISK_FULL;
    PUCHAR bytes = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('C', 'm', 'J', 'w'));
    if (!bytes)
        return STATUS_INSUFFICIENT_RESOURCES;
    cm_journal_header(bytes, &record);
    CmpFullPath(node, (PWCHAR)(bytes + CM_JOURNAL_HEADER), path_chars);
    if (chars)
        memcpy(bytes + CM_JOURNAL_HEADER + record.path_bytes, name, record.name_bytes);
    if (length)
        memcpy(bytes + CM_JOURNAL_HEADER + record.path_bytes + record.name_bytes, data, length);
    cm_journal_seal(bytes, size);
    if (!cm_journal_validate(bytes, size, CmpJournalSequence)) {
        ExFreePool(bytes);
        return STATUS_OBJECT_NAME_INVALID;
    }
    IO_STACK_LOCATION params = {};
    params.Parameters.Write.Length = size;
    params.Parameters.Write.ByteOffset.QuadPart = (LONGLONG)CmpJournalOffset;
    IO_STATUS_BLOCK io = {};
    NTSTATUS status = IopSynchronousRequest(CmpJournalFile, IRP_MJ_WRITE, 0, bytes, size, &params, &io);
    ExFreePool(bytes);
    if (NT_SUCCESS(status) && io.Information != size)
        status = STATUS_DISK_FULL;
    /* without the worker each record is flushed before it counts as saved. */
    if (NT_SUCCESS(status) && !CmpJournalWorker)
        status = flush();
    if (!NT_SUCCESS(status)) {
        NTSTATUS rollback = truncate_tail();
        if (!NT_SUCCESS(rollback))
            CmpJournalStatus = rollback;
        KiLogPrintf("registry: journal write failed at offset %llu: %08x", CmpJournalOffset, status);
        return status;
    }
    CmpJournalOffset += size;
    CmpJournalSequence++;
    if (!CmpJournalWorker)
        CmpJournalDurable = CmpJournalOffset;
    else if (!__atomic_exchange_n(&CmpJournalPending, 1, __ATOMIC_SEQ_CST))
        KeSetEvent(&CmpJournalWake, 0, FALSE);
    return STATUS_SUCCESS;
}

NTSTATUS CmpJournalFlush(BOOLEAN volatile_key)
{
    if (volatile_key)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(CmpJournalStatus))
        return CmpJournalStatus;
    if (!CmpJournalFile)
        return STATUS_UNSUCCESSFUL;
    return make_durable();
}
