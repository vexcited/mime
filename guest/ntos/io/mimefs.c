/*
 * mimefs: file system driver for host backed volumes.
 * \Device\HarddiskVolume1 is the C: drive (the drive_c directory on the
 * host), \Device\HarddiskVolume2 exposes the host root as Z:. the system files
 * mime ships (kernel, drivers, dlls) are not on the host: they are served
 * read only from the rom the host maps into the guest, without hypercalls.
 */
#include "../ntos.h"

#define MIMEFS_CHUNK (128 * 1024)

typedef struct MIMEFS_VOLUME {
    ULONG HostVolume;
    ULONG Serial;
    WCHAR Label[32];
} MIMEFS_VOLUME;

typedef struct MIMEFS_FCB {
    const MimeRomEntry *Rom; /* set for system files served from the rom */
    MIMEFS_CACHE *Cache;     /* read only opens of host files */
    ULONG Volume;
    ULONG64 FileId;
    ULONG64 HostHandle;
    BOOLEAN Directory;
    BOOLEAN DeleteOnClose;
    BOOLEAN DotsDone;
    UCHAR DotIndex;
    BOOLEAN HostDone; /* directories list host entries, then rom children */
    ULONG DirIndex;
    ULONG RomIndex;
    BOOLEAN IsRoot;
    UNICODE_STRING Pattern;
    WCHAR PatternBuffer[260];
} MIMEFS_FCB;

static NTSTATUS host_status(ULONG64 hs, BOOLEAN path_missing)
{
    switch (hs) {
    case MIME_HC_STATUS_OK: return STATUS_SUCCESS;
    case MIME_HC_STATUS_NOT_FOUND: return path_missing ? STATUS_OBJECT_PATH_NOT_FOUND : STATUS_OBJECT_NAME_NOT_FOUND;
    case MIME_HC_STATUS_ACCESS_DENIED: return STATUS_ACCESS_DENIED;
    case MIME_HC_STATUS_EXISTS: return STATUS_OBJECT_NAME_COLLISION;
    case MIME_HC_STATUS_NOT_DIRECTORY: return STATUS_NOT_A_DIRECTORY;
    case MIME_HC_STATUS_IS_DIRECTORY: return STATUS_FILE_IS_A_DIRECTORY;
    case MIME_HC_STATUS_NO_MEMORY: return STATUS_INSUFFICIENT_RESOURCES;
    case MIME_HC_STATUS_NOT_EMPTY: return STATUS_DIRECTORY_NOT_EMPTY;
    case MIME_HC_STATUS_INVALID: return STATUS_INVALID_PARAMETER;
    default: return STATUS_UNSUCCESSFUL;
    }
}

static NTSTATUS complete(PIRP irp, NTSTATUS status, ULONG_PTR info)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = info;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static PDRIVER_OBJECT g_driver;

static WCHAR fold(WCHAR c)
{
    return c >= 'a' && c <= 'z' ? (WCHAR)(c - 32) : c;
}

static void rom_info(const MimeRomEntry *rom, MimeFsInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->size = rom->size;
    info->allocation_size = (rom->size + PAGE_SIZE - 1) & ~(ULONG64)(PAGE_SIZE - 1);
    info->creation_time = info->access_time = info->write_time = info->change_time = HalBootInfo->boot_filetime;
    /* an ntfs file reference: sequence number 1, mft records after the reserved ones. */
    info->file_id = (1ull << 48) | (0x1000 + (ULONG64)(rom - HalRomEntry(0)));
    info->attributes = FILE_ATTRIBUTE_ARCHIVE;
    info->links = 1;
}

/* the length of the rom entry name when it is a direct child of dir, else 0. */
static ULONG rom_child(PCUNICODE_STRING dir, const MimeRomEntry *rom, WCHAR *name, ULONG capacity)
{
    ULONG dlen = dir->Length / sizeof(WCHAR);
    while (dlen && dir->Buffer[dlen - 1] == '\\')
        dlen--;
    ULONG plen = 0;
    while (plen < sizeof(rom->path) && rom->path[plen])
        plen++;
    if (plen <= dlen + 1 || rom->path[dlen] != '\\')
        return 0;
    for (ULONG i = 0; i < dlen; i++)
        if (fold((WCHAR)(UCHAR)rom->path[i]) != fold(dir->Buffer[i]))
            return 0;
    ULONG n = 0;
    for (ULONG i = dlen + 1; i < plen; i++) {
        if (rom->path[i] == '\\' || n == capacity)
            return 0;
        name[n++] = (WCHAR)(UCHAR)rom->path[i];
    }
    return n;
}

/* host entries with the name of a rom file are stale copies, the rom wins. */
static BOOLEAN shadowed_by_rom(PCUNICODE_STRING dir, const WCHAR *name, ULONG chars)
{
    WCHAR path[128];
    ULONG dlen = dir->Length / sizeof(WCHAR);
    while (dlen && dir->Buffer[dlen - 1] == '\\')
        dlen--;
    if (dlen + 1 + chars > RTL_NUMBER_OF(path))
        return FALSE;
    memcpy(path, dir->Buffer, dlen * sizeof(WCHAR));
    path[dlen] = '\\';
    memcpy(path + dlen + 1, name, chars * sizeof(WCHAR));
    return HalRomFind(path, dlen + 1 + chars) != NULL;
}

static NTSTATUS create_rom(PIRP irp, PFILE_OBJECT file, const MimeRomEntry *rom, ACCESS_MASK access, ULONG options,
                           ULONG disposition)
{
    if (options & FILE_DIRECTORY_FILE)
        return complete(irp, STATUS_NOT_A_DIRECTORY, 0);
    if ((disposition != FILE_OPEN && disposition != FILE_OPEN_IF) || (file->Flags & FO_DELETE_ON_CLOSE) ||
        (access & (FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_WRITE | GENERIC_ALL | DELETE)))
        return complete(irp, STATUS_ACCESS_DENIED, 0);
    MIMEFS_FCB *fcb = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MIMEFS_FCB), KI_TAG('M', 'f', 'F', 'c'));
    if (!fcb)
        return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    fcb->Rom = rom;
    file->FsContext = fcb;
    return complete(irp, STATUS_SUCCESS, FILE_OPENED);
}

/* the rom file behind a file object of this driver, for zero copy image sections. */
const MimeRomEntry *IoGetRomFile(PFILE_OBJECT file)
{
    if (!file->DeviceObject || file->DeviceObject->DriverObject != g_driver || !file->FsContext)
        return NULL;
    return ((MIMEFS_FCB *)file->FsContext)->Rom;
}

/*
 * the read only pages holding a file's data, for sections: the rom, or the
 * file's host window. *ref is released with IoReleaseFileWindow.
 */
BOOLEAN IoGetFileWindow(PFILE_OBJECT file, PULONG64 pa, PULONG64 size, PVOID *ref)
{
    *ref = NULL;
    if (!file->DeviceObject || file->DeviceObject->DriverObject != g_driver || !file->FsContext)
        return FALSE;
    MIMEFS_FCB *fcb = file->FsContext;
    if (fcb->Rom) {
        *pa = HalRomPhysical(fcb->Rom);
        *size = fcb->Rom->size;
        return TRUE;
    }
    if (!fcb->Cache || !FsCacheGetWindow(fcb->Cache, fcb->HostHandle, pa, size))
        return FALSE;
    *ref = fcb->Cache;
    return TRUE;
}

void IoReleaseFileWindow(PVOID ref)
{
    if (ref)
        FsCacheClose(ref);
}

static NTSTATUS query(MIMEFS_FCB *fcb, MimeFsInfo *info)
{
    if (fcb->Rom) {
        rom_info(fcb->Rom, info);
        return STATUS_SUCCESS;
    }
    return host_status(HalHypercall(MIME_HC_FS_QUERY, fcb->HostHandle, HalPhysicalOf(info), 0, 0, 0, 0, NULL), FALSE);
}

static NTSTATUS NTAPI mimefs_create(PDEVICE_OBJECT device, PIRP irp)
{
    MIMEFS_VOLUME *vol = device->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PFILE_OBJECT file = stack->FileObject;
    ULONG options = stack->Parameters.Create.Options & 0x00FFFFFF;
    ULONG disposition = stack->Parameters.Create.Options >> 24;
    ACCESS_MASK access = stack->Parameters.Create.SecurityContext ? stack->Parameters.Create.SecurityContext->DesiredAccess : 0;

    MimeFsOpen *req = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MimeFsOpen), KI_TAG('M', 'f', 'O', 'p'));
    if (!req)
        return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    ULONG chars = file->FileName.Length / sizeof(WCHAR);
    if (chars > RTL_NUMBER_OF(req->path)) {
        ExFreePool(req);
        return complete(irp, STATUS_NAME_TOO_LONG, 0);
    }
    /* reject wildcards and the characters ntfs refuses. */
    for (ULONG i = 0; i < chars; i++) {
        WCHAR c = file->FileName.Buffer[i];
        if (c == '*' || c == '?' || c == '<' || c == '>' || c == '|' || c == '"' || (c == ':' && i != 1)) {
            ExFreePool(req);
            return complete(irp, STATUS_OBJECT_NAME_INVALID, 0);
        }
    }
    if (vol->HostVolume == MIME_FS_VOLUME_SYSTEM) {
        const MimeRomEntry *rom = HalRomFind(file->FileName.Buffer, chars);
        if (rom) {
            ExFreePool(req);
            return create_rom(irp, file, rom, access, options, disposition);
        }
    }
    memcpy(req->path, file->FileName.Buffer, chars * sizeof(WCHAR));
    req->path_chars = chars;
    req->volume = vol->HostVolume;
    req->flags = 0;
    if (access & (FILE_READ_DATA | GENERIC_READ | GENERIC_ALL | MAXIMUM_ALLOWED | FILE_EXECUTE | GENERIC_EXECUTE))
        req->flags |= MIME_FS_OPEN_READ;
    if (access & (FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_WRITE | GENERIC_ALL))
        req->flags |= MIME_FS_OPEN_WRITE;
    if (options & FILE_DIRECTORY_FILE)
        req->flags |= MIME_FS_OPEN_DIRECTORY;
    if (options & FILE_NON_DIRECTORY_FILE)
        req->flags |= MIME_FS_OPEN_NON_DIRECTORY;
    switch (disposition) {
    case FILE_SUPERSEDE: req->disposition = MIME_FS_DISP_SUPERSEDE; break;
    case FILE_CREATE: req->disposition = MIME_FS_DISP_CREATE; break;
    case FILE_OPEN_IF: req->disposition = MIME_FS_DISP_OPEN_IF; break;
    case FILE_OVERWRITE: req->disposition = MIME_FS_DISP_OVERWRITE; break;
    case FILE_OVERWRITE_IF: req->disposition = MIME_FS_DISP_OVERWRITE_IF; break;
    default: req->disposition = MIME_FS_DISP_OPEN; break;
    }
    ULONG64 hs = HalHypercall(MIME_HC_FS_OPEN, HalPhysicalOf(req), 0, 0, 0, 0, 0, NULL);
    if (hs != MIME_HC_STATUS_OK) {
        NTSTATUS status = host_status(hs, req->action_out == 1);
        ExFreePool(req);
        return complete(irp, status, 0);
    }
    ULONG64 host = req->handle_out;
    ULONG action = req->action_out;
    ULONG req_flags = req->flags;
    ExFreePool(req);

    MimeFsInfo *info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MimeFsInfo), KI_TAG('M', 'f', 'I', 'n'));
    MIMEFS_FCB *fcb = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MIMEFS_FCB), KI_TAG('M', 'f', 'F', 'c'));
    if (!info || !fcb) {
        ExFreePool(info);
        ExFreePool(fcb);
        HalHypercall(MIME_HC_FS_CLOSE, host, 0, 0, 0, 0, 0, NULL);
        return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    HalHypercall(MIME_HC_FS_QUERY, host, HalPhysicalOf(info), 0, 0, 0, 0, NULL);
    fcb->HostHandle = host;
    fcb->Volume = vol->HostVolume;
    fcb->FileId = info->file_id;
    fcb->Directory = (info->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!fcb->Directory) {
        if (req_flags & MIME_FS_OPEN_WRITE || action != FILE_OPENED)
            FsCacheInvalidate(fcb->Volume, fcb->FileId);
        else
            fcb->Cache = FsCacheOpen(fcb->Volume, info);
    }
    fcb->IsRoot = chars == 0 || (chars == 1 && file->FileName.Buffer[0] == '\\');
    fcb->DeleteOnClose = (file->Flags & FO_DELETE_ON_CLOSE) != 0;
    ExFreePool(info);
    if (fcb->Directory)
        file->Flags |= FO_DIRECTORY_FILE;
    file->FsContext = fcb;
    return complete(irp, STATUS_SUCCESS, action);
}

static NTSTATUS NTAPI mimefs_cleanup(PDEVICE_OBJECT device, PIRP irp)
{
    return complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI mimefs_close(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    MIMEFS_FCB *fcb = file->FsContext;
    if (fcb && fcb->Rom) {
        ExFreePool(fcb);
        file->FsContext = NULL;
    } else if (fcb) {
        if (fcb->Cache)
            FsCacheClose(fcb->Cache);
        if (fcb->DeleteOnClose)
            FsCacheInvalidate(fcb->Volume, fcb->FileId);
        if (fcb->DeleteOnClose)
            HalHypercall(MIME_HC_FS_DELETE, fcb->HostHandle, 1, 0, 0, 0, 0, NULL);
        HalHypercall(MIME_HC_FS_CLOSE, fcb->HostHandle, 0, 0, 0, 0, 0, NULL);
        ExFreePool(fcb);
        file->FsContext = NULL;
    }
    return complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI mimefs_read_write(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    MIMEFS_FCB *fcb = stack->FileObject->FsContext;
    BOOLEAN write = stack->MajorFunction == IRP_MJ_WRITE;
    if (!fcb)
        return complete(irp, STATUS_INVALID_HANDLE, 0);
    if (fcb->Directory)
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    ULONG length = stack->Parameters.Read.Length;
    ULONG64 offset = (ULONG64)stack->Parameters.Read.ByteOffset.QuadPart;
    PUCHAR user = irp->UserBuffer;
    if (!length)
        return complete(irp, STATUS_SUCCESS, 0);
    if (fcb->Rom) {
        if (write)
            return complete(irp, STATUS_ACCESS_DENIED, 0);
        if (offset >= fcb->Rom->size)
            return complete(irp, STATUS_END_OF_FILE, 0);
        ULONG n = fcb->Rom->size - offset < length ? (ULONG)(fcb->Rom->size - offset) : length;
        memcpy(user, (PUCHAR)HalPhysmap(HalRomPhysical(fcb->Rom)) + offset, n);
        return complete(irp, STATUS_SUCCESS, n);
    }
    if (write)
        FsCacheInvalidate(fcb->Volume, fcb->FileId);
    ULONG cached;
    if (!write && fcb->Cache && FsCacheRead(fcb->Cache, fcb->HostHandle, offset, user, length, &cached))
        return complete(irp, cached ? STATUS_SUCCESS : STATUS_END_OF_FILE, cached);
    PUCHAR bounce = ExAllocatePool2(POOL_FLAG_NON_PAGED, length < MIMEFS_CHUNK ? length : MIMEFS_CHUNK, KI_TAG('M', 'f', 'B', 'n'));
    if (!bounce)
        return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    ULONG64 bounce_pa = HalPhysicalOf(bounce);
    ULONG done = 0;
    NTSTATUS status = STATUS_SUCCESS;
    while (done < length) {
        ULONG n = length - done < MIMEFS_CHUNK ? length - done : MIMEFS_CHUNK;
        if (write)
            memcpy(bounce, user + done, n);
        ULONG64 got = 0;
        ULONG64 hs = HalHypercall(write ? MIME_HC_FS_WRITE : MIME_HC_FS_READ, fcb->HostHandle, offset + done, bounce_pa,
                                  n, 0, 0, &got);
        if (hs != MIME_HC_STATUS_OK) {
            status = host_status(hs, FALSE);
            break;
        }
        if (!write && got)
            memcpy(user + done, bounce, got);
        done += (ULONG)got;
        if (got < n)
            break;
    }
    ExFreePool(bounce);
    if (NT_SUCCESS(status) && !write && done == 0)
        status = STATUS_END_OF_FILE;
    return complete(irp, status, done);
}

static void fill_basic(const MimeFsInfo *i, PFILE_BASIC_INFORMATION b)
{
    b->CreationTime.QuadPart = (LONGLONG)i->creation_time;
    b->LastAccessTime.QuadPart = (LONGLONG)i->access_time;
    b->LastWriteTime.QuadPart = (LONGLONG)i->write_time;
    b->ChangeTime.QuadPart = (LONGLONG)i->change_time;
    b->FileAttributes = i->attributes ? i->attributes : FILE_ATTRIBUTE_NORMAL;
}

static NTSTATUS NTAPI mimefs_query_info(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PFILE_OBJECT file = stack->FileObject;
    MIMEFS_FCB *fcb = file->FsContext;
    if (!fcb)
        return complete(irp, STATUS_INVALID_HANDLE, 0);
    MimeFsInfo *info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MimeFsInfo), KI_TAG('M', 'f', 'I', 'n'));
    if (!info)
        return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    NTSTATUS qs = query(fcb, info);
    if (!NT_SUCCESS(qs)) {
        ExFreePool(info);
        return complete(irp, qs, 0);
    }
    ULONG length = stack->Parameters.QueryFile.Length;
    PUCHAR out = irp->UserBuffer;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG written = 0;
    switch (stack->Parameters.QueryFile.FileInformationClass) {
    case FileBasicInformation:
        if (length < sizeof(FILE_BASIC_INFORMATION)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        fill_basic(info, (PFILE_BASIC_INFORMATION)out);
        written = sizeof(FILE_BASIC_INFORMATION);
        break;
    case FileStandardInformation: {
        if (length < sizeof(FILE_STANDARD_INFORMATION)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        PFILE_STANDARD_INFORMATION s = (PFILE_STANDARD_INFORMATION)out;
        s->AllocationSize.QuadPart = (LONGLONG)info->allocation_size;
        s->EndOfFile.QuadPart = (LONGLONG)info->size;
        s->NumberOfLinks = info->links ? info->links : 1;
        s->DeletePending = fcb->DeleteOnClose;
        s->Directory = fcb->Directory;
        written = sizeof(FILE_STANDARD_INFORMATION);
        break;
    }
    case FileNetworkOpenInformation: {
        if (length < sizeof(FILE_NETWORK_OPEN_INFORMATION)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        PFILE_NETWORK_OPEN_INFORMATION n = (PFILE_NETWORK_OPEN_INFORMATION)out;
        n->CreationTime.QuadPart = (LONGLONG)info->creation_time;
        n->LastAccessTime.QuadPart = (LONGLONG)info->access_time;
        n->LastWriteTime.QuadPart = (LONGLONG)info->write_time;
        n->ChangeTime.QuadPart = (LONGLONG)info->change_time;
        n->AllocationSize.QuadPart = (LONGLONG)info->allocation_size;
        n->EndOfFile.QuadPart = (LONGLONG)info->size;
        n->FileAttributes = info->attributes ? info->attributes : FILE_ATTRIBUTE_NORMAL;
        written = sizeof(FILE_NETWORK_OPEN_INFORMATION);
        break;
    }
    case FileInternalInformation:
        if (length < sizeof(FILE_INTERNAL_INFORMATION)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        ((PFILE_INTERNAL_INFORMATION)out)->IndexNumber.QuadPart = (LONGLONG)info->file_id;
        written = sizeof(FILE_INTERNAL_INFORMATION);
        break;
    case FileAttributeTagInformation:
        if (length < sizeof(FILE_ATTRIBUTE_TAG_INFORMATION)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        ((PFILE_ATTRIBUTE_TAG_INFORMATION)out)->FileAttributes = info->attributes;
        ((PFILE_ATTRIBUTE_TAG_INFORMATION)out)->ReparseTag = 0;
        written = sizeof(FILE_ATTRIBUTE_TAG_INFORMATION);
        break;
    case FileEaInformation:
        if (length < sizeof(ULONG)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        *(PULONG)out = 0;
        written = sizeof(ULONG);
        break;
    case FileNameInformation: {
        if (length < offsetof(FILE_NAME_INFORMATION, FileName)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        PFILE_NAME_INFORMATION n = (PFILE_NAME_INFORMATION)out;
        ULONG name_len = file->FileName.Length;
        if (!name_len) {
            n->FileNameLength = 2;
            if (length >= offsetof(FILE_NAME_INFORMATION, FileName) + 2) {
                n->FileName[0] = '\\';
                written = offsetof(FILE_NAME_INFORMATION, FileName) + 2;
            } else {
                status = STATUS_BUFFER_OVERFLOW;
                written = offsetof(FILE_NAME_INFORMATION, FileName);
            }
            break;
        }
        n->FileNameLength = name_len;
        ULONG room = length - (ULONG)offsetof(FILE_NAME_INFORMATION, FileName);
        ULONG copy = name_len < room ? name_len : room;
        memcpy(n->FileName, file->FileName.Buffer, copy);
        written = (ULONG)offsetof(FILE_NAME_INFORMATION, FileName) + copy;
        if (copy < name_len)
            status = STATUS_BUFFER_OVERFLOW;
        break;
    }
    case FileAllInformation: {
        ULONG need = sizeof(FILE_BASIC_INFORMATION) + sizeof(FILE_STANDARD_INFORMATION) + sizeof(FILE_INTERNAL_INFORMATION) +
                     sizeof(ULONG) * 3 + sizeof(LARGE_INTEGER) + sizeof(ULONG) * 2 + sizeof(ULONG);
        if (length < need) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        memset(out, 0, need);
        fill_basic(info, (PFILE_BASIC_INFORMATION)out);
        PFILE_STANDARD_INFORMATION s = (PFILE_STANDARD_INFORMATION)(out + 40);
        s->AllocationSize.QuadPart = (LONGLONG)info->allocation_size;
        s->EndOfFile.QuadPart = (LONGLONG)info->size;
        s->NumberOfLinks = 1;
        s->Directory = fcb->Directory;
        ((PFILE_INTERNAL_INFORMATION)(out + 64))->IndexNumber.QuadPart = (LONGLONG)info->file_id;
        written = need;
        break;
    }
    default:
        status = STATUS_INVALID_INFO_CLASS;
        break;
    }
    ExFreePool(info);
    return complete(irp, status, written);
}

static NTSTATUS NTAPI mimefs_set_info(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    MIMEFS_FCB *fcb = stack->FileObject->FsContext;
    if (!fcb)
        return complete(irp, STATUS_INVALID_HANDLE, 0);
    PUCHAR in = irp->UserBuffer;
    ULONG length = stack->Parameters.SetFile.Length;
    if (fcb->Rom)
        return complete(irp, stack->Parameters.SetFile.FileInformationClass == FileBasicInformation ? STATUS_SUCCESS
                                                                                                  : STATUS_ACCESS_DENIED,
                        0);
    switch (stack->Parameters.SetFile.FileInformationClass) {
    case FileEndOfFileInformation:
    case FileAllocationInformation: {
        if (length < sizeof(LARGE_INTEGER))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        ULONG64 size = (ULONG64)((PFILE_END_OF_FILE_INFORMATION)in)->EndOfFile.QuadPart;
        if (stack->Parameters.SetFile.FileInformationClass == FileAllocationInformation)
            return complete(irp, STATUS_SUCCESS, 0);
        FsCacheInvalidate(fcb->Volume, fcb->FileId);
        ULONG64 hs = HalHypercall(MIME_HC_FS_SET_SIZE, fcb->HostHandle, size, 0, 0, 0, 0, NULL);
        return complete(irp, host_status(hs, FALSE), 0);
    }
    case FileDispositionInformation:
        if (length < 1)
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        fcb->DeleteOnClose = ((PFILE_DISPOSITION_INFORMATION)in)->DeleteFile;
        return complete(irp, STATUS_SUCCESS, 0);
    case FileDispositionInformationEx:
        if (length < sizeof(ULONG))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        fcb->DeleteOnClose = (*(PULONG)in & 1) != 0;
        return complete(irp, STATUS_SUCCESS, 0);
    case FileBasicInformation:
        return complete(irp, STATUS_SUCCESS, 0);
    default:
        return complete(irp, STATUS_INVALID_INFO_CLASS, 0);
    }
}

/* FsRtlIsNameInExpression semantics including the dos wildcards < > ". */
static BOOLEAN name_matches(const WCHAR *pat, ULONG plen, const WCHAR *name, ULONG nlen)
{
    ULONG p = 0, n = 0, star_p = (ULONG)-1, star_n = 0;
    while (n < nlen) {
        if (p < plen) {
            WCHAR c = pat[p];
            if (c == '*' || c == '<') {
                star_p = p++;
                star_n = n;
                continue;
            }
            if (c == '?' || c == '>' || (c == '"' && name[n] == '.') || towupper(c) == towupper(name[n])) {
                p++;
                n++;
                continue;
            }
        }
        if (star_p != (ULONG)-1) {
            p = star_p + 1;
            n = ++star_n;
            continue;
        }
        return FALSE;
    }
    while (p < plen && (pat[p] == '*' || pat[p] == '<' || pat[p] == '>' || pat[p] == '"' ||
                        (pat[p] == '.' && p + 1 < plen && (pat[p + 1] == '*' || pat[p + 1] == '<'))))
        p++;
    return p == plen;
}

static ULONG entry_size(FILE_INFORMATION_CLASS klass, ULONG name_bytes)
{
    ULONG base;
    switch (klass) {
    case FileDirectoryInformation: base = offsetof(FILE_DIRECTORY_INFORMATION, FileName); break;
    case FileFullDirectoryInformation: base = offsetof(FILE_FULL_DIR_INFORMATION, FileName); break;
    case FileIdFullDirectoryInformation: base = offsetof(FILE_FULL_DIR_INFORMATION, FileName) + 8; break;
    case FileBothDirectoryInformation: base = offsetof(FILE_BOTH_DIR_INFORMATION, FileName); break;
    case FileIdBothDirectoryInformation: base = offsetof(FILE_ID_BOTH_DIR_INFORMATION, FileName); break;
    case FileNamesInformation: base = offsetof(FILE_NAMES_INFORMATION, FileName); break;
    default: return 0;
    }
    return base + name_bytes;
}

static void write_entry(PUCHAR out, FILE_INFORMATION_CLASS klass, const MimeFsInfo *info, const WCHAR *name,
                        ULONG name_bytes, ULONG index)
{
    memset(out, 0, entry_size(klass, 0));
    ULONG attrs = info->attributes ? info->attributes : FILE_ATTRIBUTE_NORMAL;
    if (klass == FileNamesInformation) {
        PFILE_NAMES_INFORMATION n = (PFILE_NAMES_INFORMATION)out;
        n->FileIndex = index;
        n->FileNameLength = name_bytes;
        memcpy(n->FileName, name, name_bytes);
        return;
    }
    PFILE_DIRECTORY_INFORMATION d = (PFILE_DIRECTORY_INFORMATION)out;
    d->FileIndex = index;
    d->CreationTime.QuadPart = (LONGLONG)info->creation_time;
    d->LastAccessTime.QuadPart = (LONGLONG)info->access_time;
    d->LastWriteTime.QuadPart = (LONGLONG)info->write_time;
    d->ChangeTime.QuadPart = (LONGLONG)info->change_time;
    d->EndOfFile.QuadPart = (LONGLONG)info->size;
    d->AllocationSize.QuadPart = (LONGLONG)info->allocation_size;
    d->FileAttributes = attrs;
    d->FileNameLength = name_bytes;
    switch (klass) {
    case FileDirectoryInformation:
        memcpy(d->FileName, name, name_bytes);
        break;
    case FileFullDirectoryInformation:
        memcpy(((PFILE_FULL_DIR_INFORMATION)out)->FileName, name, name_bytes);
        break;
    case FileIdFullDirectoryInformation:
        *(PLARGE_INTEGER)(out + offsetof(FILE_FULL_DIR_INFORMATION, FileName)) = (LARGE_INTEGER){.QuadPart = (LONGLONG)info->file_id};
        memcpy(out + offsetof(FILE_FULL_DIR_INFORMATION, FileName) + 8, name, name_bytes);
        break;
    case FileBothDirectoryInformation:
        memcpy(((PFILE_BOTH_DIR_INFORMATION)out)->FileName, name, name_bytes);
        break;
    case FileIdBothDirectoryInformation:
        ((PFILE_ID_BOTH_DIR_INFORMATION)out)->FileId.QuadPart = (LONGLONG)info->file_id;
        memcpy(((PFILE_ID_BOTH_DIR_INFORMATION)out)->FileName, name, name_bytes);
        break;
    default:
        break;
    }
}

static NTSTATUS NTAPI mimefs_directory(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    MIMEFS_FCB *fcb = stack->FileObject->FsContext;
    if (stack->MinorFunction != IRP_MN_QUERY_DIRECTORY)
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (!fcb || !fcb->Directory)
        return complete(irp, STATUS_INVALID_PARAMETER, 0);
    FILE_INFORMATION_CLASS klass = stack->Parameters.QueryDirectory.FileInformationClass;
    if (!entry_size(klass, 0))
        return complete(irp, STATUS_INVALID_INFO_CLASS, 0);

    BOOLEAN first_query = fcb->Pattern.Buffer == NULL;
    if ((stack->Flags & SL_RESTART_SCAN) || first_query) {
        fcb->DirIndex = 0;
        fcb->RomIndex = 0;
        fcb->HostDone = FALSE;
        fcb->DotIndex = 0;
        fcb->DotsDone = fcb->IsRoot;
    }
    PUNICODE_STRING pattern = stack->Parameters.QueryDirectory.FileName;
    if (pattern && pattern->Length) {
        ULONG n = pattern->Length / sizeof(WCHAR);
        if (n > RTL_NUMBER_OF(fcb->PatternBuffer))
            n = RTL_NUMBER_OF(fcb->PatternBuffer);
        memcpy(fcb->PatternBuffer, pattern->Buffer, n * sizeof(WCHAR));
        fcb->Pattern.Buffer = fcb->PatternBuffer;
        fcb->Pattern.Length = (USHORT)(n * sizeof(WCHAR));
    } else if (first_query) {
        fcb->PatternBuffer[0] = '*';
        fcb->Pattern.Buffer = fcb->PatternBuffer;
        fcb->Pattern.Length = sizeof(WCHAR);
    }
    ULONG plen = fcb->Pattern.Length / sizeof(WCHAR);

    MimeFsDirEntry *de = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MimeFsDirEntry), KI_TAG('M', 'f', 'D', 'e'));
    if (!de)
        return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    PCUNICODE_STRING dir = &stack->FileObject->FileName;
    PUCHAR out = irp->UserBuffer;
    ULONG length = stack->Parameters.QueryDirectory.Length;
    ULONG used = 0, last = (ULONG)-1, count = 0;
    BOOLEAN single = (stack->Flags & SL_RETURN_SINGLE_ENTRY) != 0;
    NTSTATUS status = STATUS_SUCCESS;

    for (;;) {
        const WCHAR *name;
        ULONG name_chars;
        static const WCHAR dot[] = L"..";
        ULONG index;
        if (!fcb->DotsDone) {
            MimeFsInfo *self = &de->info;
            HalHypercall(MIME_HC_FS_QUERY, fcb->HostHandle, HalPhysicalOf(self), 0, 0, 0, 0, NULL);
            name = dot;
            name_chars = fcb->DotIndex + 1;
            index = fcb->DirIndex;
        } else if (!fcb->HostDone) {
            ULONG64 more = 0;
            ULONG64 hs = HalHypercall(MIME_HC_FS_READDIR, fcb->HostHandle, fcb->DirIndex, HalPhysicalOf(de), 0, 0, 0, &more);
            if (hs != MIME_HC_STATUS_OK || !more) {
                fcb->HostDone = TRUE;
                continue;
            }
            name = de->name;
            name_chars = de->name_chars;
            index = fcb->DirIndex;
            if (shadowed_by_rom(dir, name, name_chars)) {
                fcb->DirIndex++;
                continue;
            }
        } else {
            const MimeRomEntry *rom = HalRomEntry(fcb->RomIndex);
            if (!rom)
                break;
            name_chars = rom_child(dir, rom, de->name, RTL_NUMBER_OF(de->name));
            if (!name_chars) {
                fcb->RomIndex++;
                continue;
            }
            rom_info(rom, &de->info);
            name = de->name;
            index = 0x80000000u | fcb->RomIndex;
        }
        if (!name_matches(fcb->Pattern.Buffer, plen, name, name_chars)) {
            if (!fcb->DotsDone) {
                if (++fcb->DotIndex == 2)
                    fcb->DotsDone = TRUE;
            } else if (!fcb->HostDone) {
                fcb->DirIndex++;
            } else {
                fcb->RomIndex++;
            }
            continue;
        }
        ULONG size = entry_size(klass, name_chars * sizeof(WCHAR));
        ULONG aligned = (used + 7) & ~7u;
        if (aligned + size > length) {
            if (count == 0)
                status = STATUS_BUFFER_OVERFLOW;
            break;
        }
        if (last != (ULONG)-1)
            *(PULONG)(out + last) = aligned - last;
        write_entry(out + aligned, klass, &de->info, name, name_chars * sizeof(WCHAR), index);
        last = aligned;
        used = aligned + size;
        count++;
        if (!fcb->DotsDone) {
            if (++fcb->DotIndex == 2)
                fcb->DotsDone = TRUE;
        } else if (!fcb->HostDone) {
            fcb->DirIndex++;
        } else {
            fcb->RomIndex++;
        }
        if (single)
            break;
    }
    ExFreePool(de);
    if (count == 0 && status == STATUS_SUCCESS)
        status = first_query ? STATUS_NO_SUCH_FILE : STATUS_NO_MORE_FILES;
    return complete(irp, status, used);
}

static NTSTATUS NTAPI mimefs_query_volume(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    MIMEFS_VOLUME *vol = device->DeviceExtension;
    PUCHAR out = irp->UserBuffer;
    ULONG length = stack->Parameters.QueryVolume.Length;
    switch (stack->Parameters.QueryVolume.FsInformationClass) {
    case FileFsDeviceInformation:
        if (length < sizeof(FILE_FS_DEVICE_INFORMATION))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        ((PFILE_FS_DEVICE_INFORMATION)out)->DeviceType = FILE_DEVICE_DISK;
        ((PFILE_FS_DEVICE_INFORMATION)out)->Characteristics = 0;
        return complete(irp, STATUS_SUCCESS, sizeof(FILE_FS_DEVICE_INFORMATION));
    case FileFsAttributeInformation: {
        /* FILE_FS_ATTRIBUTE_INFORMATION: flags, max component, name length, name. */
        static const WCHAR fs[] = L"NTFS";
        ULONG need = 12 + sizeof(fs) - sizeof(WCHAR);
        if (length < 12)
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        ((PULONG)out)[0] = 0x00000003 | 0x00000004 | 0x00000002; /* case preserved, unicode names */
        ((PULONG)out)[1] = 255;
        ((PULONG)out)[2] = sizeof(fs) - sizeof(WCHAR);
        ULONG copy = length - 12 < sizeof(fs) - sizeof(WCHAR) ? length - 12 : (ULONG)(sizeof(fs) - sizeof(WCHAR));
        memcpy(out + 12, fs, copy);
        return complete(irp, copy == sizeof(fs) - sizeof(WCHAR) ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW, 12 + copy);
        (void)need;
    }
    case FileFsVolumeInformation: {
        /* creation time, serial, label length, supports objects, label. */
        ULONG label_bytes = (ULONG)(wcslen(vol->Label) * sizeof(WCHAR));
        if (length < 18)
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        memset(out, 0, 18);
        *(PULONG)(out + 8) = vol->Serial;
        *(PULONG)(out + 12) = label_bytes;
        ULONG copy = length - 18 < label_bytes ? length - 18 : label_bytes;
        memcpy(out + 18, vol->Label, copy);
        return complete(irp, copy == label_bytes ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW, 18 + copy);
    }
    case FileFsSizeInformation:
    case FileFsFullSizeInformation: {
        ULONG need = stack->Parameters.QueryVolume.FsInformationClass == FileFsSizeInformation ? 24 : 32;
        if (length < need)
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        PLARGE_INTEGER v = (PLARGE_INTEGER)out;
        v[0].QuadPart = 64ll * 1024 * 1024 * 1024 / 4096; /* total units */
        v[1].QuadPart = 32ll * 1024 * 1024 * 1024 / 4096; /* available units */
        if (need == 32) {
            v[2].QuadPart = v[1].QuadPart;
            ((PULONG)(out + 24))[0] = 8;
            ((PULONG)(out + 24))[1] = 512;
        } else {
            ((PULONG)(out + 16))[0] = 8;
            ((PULONG)(out + 16))[1] = 512;
        }
        return complete(irp, STATUS_SUCCESS, need);
    }
    default:
        return complete(irp, STATUS_INVALID_INFO_CLASS, 0);
    }
}

static NTSTATUS NTAPI mimefs_flush(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    MIMEFS_FCB *fcb = file->FsContext;
    if (!fcb)
        return complete(irp, STATUS_INVALID_HANDLE, 0);
    if (fcb->Rom)
        return complete(irp, STATUS_SUCCESS, 0);
    ULONG64 hs = HalHypercall(MIME_HC_FS_FLUSH, fcb->HostHandle, 0, 0, 0, 0, 0, NULL);
    return complete(irp, host_status(hs, FALSE), 0);
}

static NTSTATUS create_volume(PDRIVER_OBJECT driver, PCWSTR name, ULONG host_volume, PCSTR label, ULONG serial)
{
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, name);
    PDEVICE_OBJECT device;
    NTSTATUS status = IoCreateDevice(driver, sizeof(MIMEFS_VOLUME), &n, FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &device);
    if (!NT_SUCCESS(status))
        return status;
    MIMEFS_VOLUME *vol = device->DeviceExtension;
    vol->HostVolume = host_volume;
    vol->Serial = serial;
    for (ULONG i = 0; label[i] && i + 1 < RTL_NUMBER_OF(vol->Label); i++)
        vol->Label[i] = (UCHAR)label[i];
    device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS MimeFsDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    g_driver = driver;
    FsCacheInitialize();
    driver->MajorFunction[IRP_MJ_CREATE] = mimefs_create;
    driver->MajorFunction[IRP_MJ_CLEANUP] = mimefs_cleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = mimefs_close;
    driver->MajorFunction[IRP_MJ_READ] = mimefs_read_write;
    driver->MajorFunction[IRP_MJ_WRITE] = mimefs_read_write;
    driver->MajorFunction[IRP_MJ_QUERY_INFORMATION] = mimefs_query_info;
    driver->MajorFunction[IRP_MJ_SET_INFORMATION] = mimefs_set_info;
    driver->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] = mimefs_directory;
    driver->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = mimefs_query_volume;
    driver->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = mimefs_flush;
    /* C: has no label, like a fresh install; Z: shows the name of the host volume. */
    ULONG serial = HalBootInfo->volume_serial;
    NTSTATUS status = create_volume(driver, L"\\Device\\HarddiskVolume1", MIME_FS_VOLUME_SYSTEM, "", serial);
    if (NT_SUCCESS(status))
        status = create_volume(driver, L"\\Device\\HarddiskVolume2", MIME_FS_VOLUME_HOST, HalBootInfo->host_volume_label,
                               serial * 2654435761u | 1);
    return status;
}
