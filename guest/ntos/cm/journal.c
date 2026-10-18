/* nonvolatile changes are a checksummed write-ahead log beside the imported hives. */
#include "cm.h"

static PFILE_OBJECT CmpJournalFile;
static ULONG64 CmpJournalOffset;
static ULONG64 CmpJournalSequence = 1;
static NTSTATUS CmpJournalStatus;
static BOOLEAN CmpJournalReady;

static NTSTATUS flush(void)
{
    return IopSynchronousRequest(CmpJournalFile, IRP_MJ_FLUSH_BUFFERS, 0, nullptr, 0, nullptr, nullptr);
}

static NTSTATUS truncate_tail(void)
{
    FILE_END_OF_FILE_INFORMATION end;
    end.EndOfFile.QuadPart = (LONGLONG)CmpJournalOffset;
    IO_STACK_LOCATION params = {};
    params.Parameters.SetFile.Length = sizeof(end);
    params.Parameters.SetFile.FileInformationClass = FileEndOfFileInformation;
    NTSTATUS status = IopSynchronousRequest(CmpJournalFile, IRP_MJ_SET_INFORMATION, 0, &end, sizeof(end),
                                            &params, nullptr);
    return NT_SUCCESS(status) ? flush() : status;
}

/*
 * the log is read in pieces of a record, tens of thousands of them, each of which is a call to the host:
 * replaying reads it a block at a time instead and takes the records out of that.
 */
#define CM_REPLAY_BLOCK (16 * 1024)
static PUCHAR CmpReplayBlock;
static ULONG64 CmpReplayBlockOffset;
static ULONG CmpReplayBlockLength;
static ULONG64 CmpReplayFileLength;

static NTSTATUS read_bytes(void *data, ULONG bytes, ULONG64 offset)
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

static NTSTATUS replay(void)
{
    ULONG64 length;
    NTSTATUS status = IoQueryFileSize(CmpJournalFile, &length);
    if (!NT_SUCCESS(status))
        return status;
    if (length > CM_JOURNAL_MAX_FILE)
        return STATUS_REGISTRY_CORRUPT;
    CmpReplayFileLength = length;
    while (CmpJournalOffset < length) {
        if (length - CmpJournalOffset < CM_JOURNAL_HEADER)
            return truncate_tail();
        UCHAR header[CM_JOURNAL_HEADER];
        status = read_bytes(header, sizeof(header), CmpJournalOffset);
        if (!NT_SUCCESS(status))
            return status;
        CmJournalRecord record;
        if (!cm_journal_decode(header, sizeof(header), &record))
            return STATUS_REGISTRY_CORRUPT;
        ULONG size = cm_journal_size(&record);
        if (size > length - CmpJournalOffset)
            return truncate_tail();
        PUCHAR bytes = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('C', 'm', 'J', 'r'));
        if (!bytes)
            return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(bytes, header, sizeof(header));
        status = read_bytes(bytes + sizeof(header), size - sizeof(header), CmpJournalOffset + sizeof(header));
        if (NT_SUCCESS(status)) {
            if (!cm_journal_validate(bytes, size, CmpJournalSequence))
                status = STATUS_REGISTRY_CORRUPT;
            else
                status = CmpRestoreJournal(&record, bytes + sizeof(header));
        }
        ExFreePool(bytes);
        if (!NT_SUCCESS(status))
            return status;
        CmpJournalOffset += size;
        CmpJournalSequence++;
    }
    return STATUS_SUCCESS;
}

void CmpJournalInitialize(void)
{
    UNICODE_STRING path = RTL_CONSTANT_STRING(L"\\??\\C:\\Windows\\System32\\config\\mime-registry.log");
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    HANDLE handle;
    IO_STATUS_BLOCK io;
    CmpJournalStatus = IopCreateFile(&handle, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, &attributes,
                                     &io, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN_IF,
                                     FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, KernelMode);
    if (NT_SUCCESS(CmpJournalStatus)) {
        CmpJournalStatus = ObReferenceHandle(handle, IoFileObjectTypeObject, KernelMode,
                                              (PVOID *)&CmpJournalFile, nullptr);
        ZwClose(handle);
    }
    if (NT_SUCCESS(CmpJournalStatus)) {
        CmpReplayBlock = ExAllocatePool2(POOL_FLAG_NON_PAGED, CM_REPLAY_BLOCK, KI_TAG('C', 'm', 'J', 'b'));
        CmpReplayBlockLength = 0;
        CmpJournalStatus = replay();
        if (CmpReplayBlock)
            ExFreePool(CmpReplayBlock);
        CmpReplayBlock = nullptr;
    }
    if (!NT_SUCCESS(CmpJournalStatus))
        KiLogPrintf("registry: journal unavailable at offset %llu: %08x", CmpJournalOffset, CmpJournalStatus);
}

void CmpJournalEnable(void)
{
    CmpJournalReady = TRUE;
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
    if (NT_SUCCESS(status))
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
    return STATUS_SUCCESS;
}

NTSTATUS CmpJournalFlush(CM_NODE *node)
{
    if (node->Flags & CM_NODE_VOLATILE)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(CmpJournalStatus))
        return CmpJournalStatus;
    return CmpJournalFile ? flush() : STATUS_UNSUCCESSFUL;
}
