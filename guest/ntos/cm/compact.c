/*
 * registry journal compaction at boot, see journal_compact.h for what counts. the replay notes each record, and
 * when the journal is big the records that still matter go to a new file that replaces the journal by a rename,
 * so a crash leaves the old journal whole.
 */
#include "cm.h"

#define CM_COMPACT_ALLOC(size) ExAllocatePool2(POOL_FLAG_NON_PAGED, (size), KI_TAG('C', 'm', 'J', 'c'))
#define CM_COMPACT_FREE(p) ExFreePool(p)
#include "journal_compact.h"

#define CM_COMPACT_OUT (64u << 10)

NTSTATUS CmpJournalReadBytes(void *data, ULONG bytes, ULONG64 offset);

PVOID CmpCompactBegin(void)
{
    /* the nocompaction kernel option keeps the journal as is, to compare what a replay builds. */
    if (strstr(HalBootInfo->cmdline, "nocompaction"))
        return nullptr;
    return cm_compact_create();
}

void CmpCompactNote(PVOID context, ULONG64 offset, const CmJournalRecord *record, const UCHAR *payload)
{
    cm_compact_note(context, offset, record, payload);
}

void CmpCompactAbandon(PVOID context)
{
    cm_compact_free(context);
}

static NTSTATUS write_at(PFILE_OBJECT file, const void *data, ULONG size, ULONG64 offset)
{
    IO_STACK_LOCATION params = {};
    params.Parameters.Write.Length = size;
    params.Parameters.Write.ByteOffset.QuadPart = (LONGLONG)offset;
    IO_STATUS_BLOCK io = {};
    NTSTATUS status = IopSynchronousRequest(file, IRP_MJ_WRITE, 0, (PVOID)data, size, &params, &io);
    if (NT_SUCCESS(status) && io.Information != size)
        status = STATUS_DISK_FULL;
    return status;
}

/* renumbered and sealed again. */
static NTSTATUS write_kept(CmCompact *c, PFILE_OBJECT file, uint32_t largest, ULONG64 *length)
{
    PUCHAR out = ExAllocatePool2(POOL_FLAG_NON_PAGED, CM_COMPACT_OUT, KI_TAG('C', 'm', 'J', 'o'));
    PUCHAR record = ExAllocatePool2(POOL_FLAG_NON_PAGED, largest, KI_TAG('C', 'm', 'J', 'k'));
    NTSTATUS status = out && record ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
    ULONG used = 0;
    ULONG64 written = 0, sequence = 1;
    for (ULONG i = 0; NT_SUCCESS(status) && i < c->record_count; i++) {
        CmCompactRecord *r = &c->records[i];
        if (!r->keep)
            continue;
        status = CmpJournalReadBytes(record, r->size, r->offset);
        if (!NT_SUCCESS(status))
            break;
        cm_journal_put64(record + 8, sequence++);
        cm_journal_seal(record, r->size);
        if (used + r->size > CM_COMPACT_OUT) {
            status = write_at(file, out, used, written);
            written += used;
            used = 0;
            if (!NT_SUCCESS(status))
                break;
        }
        if (r->size > CM_COMPACT_OUT) {
            status = write_at(file, record, r->size, written);
            written += r->size;
        } else {
            memcpy(out + used, record, r->size);
            used += r->size;
        }
    }
    if (NT_SUCCESS(status) && used) {
        status = write_at(file, out, used, written);
        written += used;
    }
    if (out)
        ExFreePool(out);
    if (record)
        ExFreePool(record);
    *length = written;
    return status;
}

/*
 * after the replay noted the records: writes the new journal, puts it in place of the old one and hands it
 * over through the pointers. on any failure the old one stays as it was.
 */
void CmpCompactFinish(PVOID context, PFILE_OBJECT *journal, ULONG64 *offset, ULONG64 *sequence)
{
    CmCompact *c = context;
    ULONG64 kept_bytes, started = HalReadCounter();
    uint32_t kept, largest;
    if (!cm_compact_decide(c, &kept_bytes, &kept, &largest)) {
        cm_compact_free(c);
        return;
    }
    /* a journal that is mostly live records is not worth rewriting; the next boot looks again. */
    if (!kept || kept_bytes * 4 > *offset * 3) {
        cm_compact_free(c);
        return;
    }

    static const WCHAR scratch[] = L"\\??\\C:\\Windows\\System32\\config\\registry.journal.new";
    static const WCHAR journal_name[] = L"\\??\\C:\\Windows\\System32\\config\\registry.journal";
    UNICODE_STRING path;
    RtlInitUnicodeString(&path, scratch);
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    HANDLE handle;
    IO_STATUS_BLOCK io;
    NTSTATUS status = IopCreateFile(&handle, FILE_READ_DATA | FILE_WRITE_DATA | DELETE | SYNCHRONIZE, &attributes, &io,
                                    FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OVERWRITE_IF,
                                    FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_DELETE_ON_CLOSE |
                                        FILE_WRITE_THROUGH,
                                    KernelMode);
    PFILE_OBJECT file = nullptr;
    if (NT_SUCCESS(status)) {
        status = ObReferenceHandle(handle, IoFileObjectTypeObject, KernelMode, (PVOID *)&file, nullptr);
        ZwClose(handle);
    }
    ULONG64 length = 0;
    if (NT_SUCCESS(status))
        status = write_kept(c, file, largest, &length);
    if (NT_SUCCESS(status))
        status = IopSynchronousRequest(file, IRP_MJ_FLUSH_BUFFERS, 0, nullptr, 0, nullptr, nullptr);
    if (NT_SUCCESS(status)) {
        /* kept on close, as the journal. */
        FILE_DISPOSITION_INFORMATION keep = {FALSE};
        IO_STACK_LOCATION params = {};
        params.Parameters.SetFile.Length = sizeof(keep);
        params.Parameters.SetFile.FileInformationClass = FileDispositionInformation;
        status = IopSynchronousRequest(file, IRP_MJ_SET_INFORMATION, 0, &keep, sizeof(keep), &params, nullptr);
    }
    if (NT_SUCCESS(status))
        status = IoRenameFileObject(file, journal_name, TRUE);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("registry: journal not compacted: %08x", status);
        if (file)
            ObDereferenceObject(file);
        cm_compact_free(c);
        return;
    }
    KiLogPrintf("registry: journal compacted from %u records of %llu KiB to %u of %llu KiB in %llu ms", c->record_count,
                *offset >> 10, kept, length >> 10, HalCounterToHns(HalReadCounter() - started) / 10000);
    ObDereferenceObject(*journal);
    *journal = file;
    *offset = length;
    *sequence = (ULONG64)kept + 1;
    cm_compact_free(c);
}
