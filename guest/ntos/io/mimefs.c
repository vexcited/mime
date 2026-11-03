/*
 * mimefs: file system driver for host backed volumes.
 * \Device\HarddiskVolume1 is C:, the host's drive_c directory, and
 * \Device\HarddiskVolume2 exposes the host root as Z:. the kernel, drivers
 * and dlls mime ships are not on the host: they are served read only from
 * the rom the host maps into the guest, without hypercalls.
 */
#include "../ntos.h"

#define MIMEFS_CHUNK (128 * 1024)
#define STATUS_USER_MAPPED_FILE ((NTSTATUS)0xC0000243L)
#define STATUS_CANNOT_DELETE ((NTSTATUS)0xC0000121L)
#define STATUS_INVALID_VOLUME_LABEL ((NTSTATUS)0xC0000086L)

typedef struct MIMEFS_COUNTERS {
    ULONG64 Reads;
    ULONG64 ReadBytes;
    ULONG64 Writes;
    ULONG64 WriteBytes;
} MIMEFS_COUNTERS;

#define MIMEFS_MAX_CPUS 64

typedef struct MIMEFS_VOLUME {
    ULONG HostVolume;
    ULONG Serial;
    WCHAR Label[32];
    /* what the volume read and wrote, for each processor, as the statistics of the file system tell. */
    MIMEFS_COUNTERS Counters[MIMEFS_MAX_CPUS];
} MIMEFS_VOLUME;

typedef struct MIMEFS_FCB {
    const MimeRomEntry *Rom; /* set for system files served from the rom. */
    MIMEFS_CACHE *Cache;     /* read only opens of host files. */
    ULONG Volume;
    ULONG64 FileId;
    ULONG64 HostHandle;
    FS_WB Wb;            /* small writes buffered for the host, see io/fswrite.c. */
    BOOLEAN Buffered;    /* not opened for write through or unbuffered i/o, so it may buffer. */
    BOOLEAN NoAccessTime; /* reads leave the access time alone. */
    LONGLONG LastTouch;   /* when a read last set the access time. */
    BOOLEAN StatOnly;    /* answers one query by name: no host handle, OpenInfo is the answer. */
    MimeFsInfo OpenInfo; /* what the host reported at open. */
    BOOLEAN Directory;
    BOOLEAN Link;         /* opened as the symbolic link or junction itself, which has no data. */
    ACCESS_MASK Access;   /* what the handle was given. */
    BOOLEAN DeleteOnClose;
    BOOLEAN DotsDone;
    UCHAR DotIndex;
    BOOLEAN HostDone; /* directories list host entries, then rom children. */
    ULONG DirIndex;
    ULONG RomIndex;
    BOOLEAN IsRoot;
    MimeFsDirEntry *Batch; /* host listing entries fetched together, starting at BatchFirst. */
    ULONG BatchFirst;
    ULONG BatchCount;
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
    case MIME_HC_STATUS_CROSS_DEVICE: return STATUS_NOT_SAME_DEVICE;
    case MIME_HC_STATUS_TOO_MANY_LINKS: return STATUS_TOO_MANY_LINKS;
    default: return STATUS_UNSUCCESSFUL;
    }
}

NTSTATUS MimeFsHostStatus(ULONG64 hs, BOOLEAN path_missing)
{
    return host_status(hs, path_missing);
}

static NTSTATUS complete(PIRP irp, NTSTATUS status, ULONG_PTR info)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = info;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    if (NT_SUCCESS(status) && info && (stack->MajorFunction == IRP_MJ_READ || stack->MajorFunction == IRP_MJ_WRITE) &&
        stack->DeviceObject) {
        MIMEFS_VOLUME *vol = stack->DeviceObject->DeviceExtension;
        MIMEFS_COUNTERS *counters = &vol->Counters[KeGetCurrentPrcb()->Number % MIMEFS_MAX_CPUS];
        if (stack->MajorFunction == IRP_MJ_READ) {
            counters->Reads++;
            counters->ReadBytes += info;
        } else {
            counters->Writes++;
            counters->WriteBytes += info;
        }
    }
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static PDRIVER_OBJECT g_driver;

BOOLEAN MimeFsLockIdentity(PFILE_OBJECT file, PULONG volume, PULONG64 file_id)
{
    if (!IopIsMimeFs(file))
        return FALSE;
    MIMEFS_FCB *fcb = file->FsContext;
    if (!fcb || fcb->Directory || fcb->StatOnly)
        return FALSE;
    *volume = fcb->Rom ? ~0u : fcb->Volume;
    *file_id = fcb->Rom ? (ULONG64)(ULONG_PTR)fcb->Rom : fcb->FileId;
    return TRUE;
}

static WCHAR fold(WCHAR c)
{
    return c >= 'a' && c <= 'z' ? (WCHAR)(c - 32) : c;
}

#define KEY_FILE_SYSTEM L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\FileSystem"

/*
 * NtfsDisableLastAccessUpdate: with the value set, bit 0 decides whether reads set the access time. without it the
 * system manages the setting, and ntfs.sys of Windows 11 disables the updates on every volume of a client: the
 * default is 0x80000003, and the size threshold only matters on a server. writes set the access time either way.
 */
static BOOLEAN access_time_disabled(void)
{
    ULONG type, value, length = sizeof(value);
    if (NT_SUCCESS(CmQueryValueUnlessBusy(KEY_FILE_SYSTEM, L"NtfsDisableLastAccessUpdate", &type, &value, &length)) &&
        type == 4 /* REG_DWORD */ && length >= sizeof(value))
        return (value & 1) != 0;
    return TRUE;
}

static void rom_info(const MimeRomEntry *rom, MimeFsInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->size = rom->size;
    info->allocation_size = (rom->size + PAGE_SIZE - 1) & ~(ULONG64)(PAGE_SIZE - 1);
    info->creation_time = info->access_time = info->write_time = info->change_time = HalRomFiletime();
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

/* host entries named like a rom file are stale copies, the rom wins. */
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
    return HalRomFind(path, dlen + 1 + chars) != nullptr;
}

static NTSTATUS create_rom(PIRP irp, PFILE_OBJECT file, const MimeRomEntry *rom, ACCESS_MASK access, ULONG options,
                           ULONG disposition, PIO_SECURITY_CONTEXT security, BOOLEAN check)
{
    if (options & FILE_DIRECTORY_FILE)
        return complete(irp, STATUS_NOT_A_DIRECTORY, 0);
    if ((disposition != FILE_OPEN && disposition != FILE_OPEN_IF) || (file->Flags & FO_DELETE_ON_CLOSE) ||
        (access & (FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_WRITE | GENERIC_ALL | DELETE)))
        return complete(irp, STATUS_ACCESS_DENIED, 0);
    if (check) {
        ACCESS_MASK granted;
        NTSTATUS status = MimeFsCheckRomOpen(access, options, &granted);
        if (!NT_SUCCESS(status)) {
            SeTraceDenial("rom file", &file->FileName, access, status);
            return complete(irp, status, 0);
        }
        security->GrantedAccess = granted;
        security->Checked = TRUE;
        file->ReadAccess = (granted & (FILE_READ_DATA | FILE_EXECUTE)) != 0;
    }
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
        return nullptr;
    return ((MIMEFS_FCB *)file->FsContext)->Rom;
}

/*
 * the pages backing a file for sections: the rom, or a window on the host's page cache, so a view and reads or writes
 * of the file see each other at once. a read-only file's window is shared by its readers; one for a handle opened
 * for writing, or for a writable view, belongs to the section. a file with a window in use buffers no writes, see
 * io/fswrite.c, and what it buffered is flushed before the window is made. *ref is released with IoReleaseFileWindow.
 */
typedef struct MIMEFS_WINDOW_REF {
    MIMEFS_CACHE *Cache; /* a shared window, held by the cache entry. */
    ULONG64 Private;     /* guest address of the section's private window. */
    ULONG Volume;
    ULONG64 FileId;
    BOOLEAN Image; /* the window of an image section. */
} MIMEFS_WINDOW_REF;

static NTSTATUS query(MIMEFS_FCB *fcb, MimeFsInfo *info);

BOOLEAN IoGetFileWindowEx(PFILE_OBJECT file, BOOLEAN writable, ULONG64 minimum_size, PULONG64 pa, PULONG64 size, PVOID *ref)
{
    *ref = nullptr;
    if (!file->DeviceObject || file->DeviceObject->DriverObject != g_driver || !file->FsContext)
        return FALSE;
    MIMEFS_FCB *fcb = file->FsContext;
    if (fcb->Rom) {
        if (writable)
            return FALSE;
        *pa = HalRomPhysical(fcb->Rom);
        *size = fcb->Rom->size;
        return TRUE;
    }
    if (fcb->Directory || fcb->StatOnly || (writable && !file->WriteAccess))
        return FALSE;
    MIMEFS_WINDOW_REF *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), KI_TAG('M', 'f', 'W', 'r'));
    if (!r)
        return FALSE;
    memset(r, 0, sizeof(*r));
    r->Volume = fcb->Volume;
    r->FileId = fcb->FileId;
    /* counted first so concurrent writes go to the host, then what was buffered is flushed. */
    FsWbMapped(r->Volume, r->FileId, 1);
    FsWbSync(&fcb->Wb);
    BOOLEAN done = FALSE;
    if (!writable && fcb->Cache && FsCacheGetWindow(fcb->Cache, fcb->HostHandle, pa, size)) {
        r->Cache = fcb->Cache;
        done = TRUE;
    }
    if (!done) {
        MimeFsInfo *info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*info), KI_TAG('M', 'f', 'I', 'n'));
        if (info && NT_SUCCESS(query(fcb, info)) && (info->size || (writable && minimum_size))) {
            ULONG64 length = info->size;
            /* a section larger than the file extends the file. */
            if (writable && minimum_size > length) {
                FsCacheInvalidate(fcb->Volume, fcb->FileId);
                if (HalHypercallUnlocked(MIME_HC_FS_SET_SIZE, fcb->HostHandle, minimum_size, 0, 0, 0, 0, nullptr) == MIME_HC_STATUS_OK) {
                    length = minimum_size;
                    FsNotifyReport(fcb->Volume, &file->FileName, FILE_ACTION_MODIFIED,
                                   FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
                }
            }
            if (length >= minimum_size || !writable) {
                done = FsCacheMapPrivate(fcb->HostHandle, writable, pa);
                if (done) {
                    r->Private = *pa;
                    *size = length;
                }
            }
        }
        if (info)
            ExFreePool(info);
    }
    if (!done) {
        FsWbMapped(r->Volume, r->FileId, -1);
        ExFreePool(r);
        return FALSE;
    }
    *ref = r;
    return TRUE;
}

BOOLEAN IoGetFileWindow(PFILE_OBJECT file, PULONG64 pa, PULONG64 size, PVOID *ref)
{
    return IoGetFileWindowEx(file, FALSE, 0, pa, size, ref);
}

BOOLEAN IoGetImageFileWindow(PFILE_OBJECT file, PULONG64 pa, PULONG64 size, PVOID *ref)
{
    if (!IoGetFileWindowEx(file, FALSE, 0, pa, size, ref))
        return FALSE;
    MIMEFS_WINDOW_REF *r = *ref;
    if (r) {
        r->Image = TRUE;
        FsWbImageMapped(r->Volume, r->FileId, 1);
    }
    return TRUE;
}

void IoReleaseFileWindow(PVOID ref)
{
    MIMEFS_WINDOW_REF *r = ref;
    if (!r)
        return;
    if (r->Cache)
        FsCacheClose(r->Cache);
    if (r->Private)
        FsCacheUnmapPrivate(r->Private);
    if (r->Image)
        FsWbImageMapped(r->Volume, r->FileId, -1);
    FsWbMapped(r->Volume, r->FileId, -1);
    ExFreePool(r);
}

BOOLEAN IopIsMimeFs(PFILE_OBJECT file)
{
    return file->DeviceObject && file->DeviceObject->DriverObject == g_driver;
}

NTSTATUS MimeFsDirectoryVolume(PFILE_OBJECT file, PULONG volume, PULONG64 host_handle)
{
    if (!IopIsMimeFs(file) || !file->FsContext)
        return STATUS_INVALID_DEVICE_REQUEST;
    MIMEFS_FCB *fcb = file->FsContext;
    if (fcb->Rom || !fcb->Directory)
        return STATUS_INVALID_PARAMETER;
    *volume = fcb->Volume;
    if (host_handle)
        *host_handle = fcb->HostHandle;
    return STATUS_SUCCESS;
}

static NTSTATUS query(MIMEFS_FCB *fcb, MimeFsInfo *info)
{
    if (fcb->Rom) {
        rom_info(fcb->Rom, info);
        return STATUS_SUCCESS;
    }
    if (fcb->StatOnly) {
        *info = fcb->OpenInfo;
        return STATUS_SUCCESS;
    }
    /*
     * other handles flush their buffers for the file first; this one's is counted in. what it holds is taken before
     * the host answers, as the flusher may send it meanwhile and the host's size then is the one it had before.
     */
    FsWbSyncOthers(&fcb->Wb);
    ULONG64 pending = FsWbPendingEnd(&fcb->Wb);
    NTSTATUS status = host_status(HalHypercallUnlocked(MIME_HC_FS_QUERY, fcb->HostHandle, HalPhysicalOf(info), 0, 0, 0, 0, nullptr), FALSE);
    if (NT_SUCCESS(status) && pending > info->size) {
        info->size = pending;
        if (info->allocation_size < info->size)
            info->allocation_size = (info->size + 4095) & ~4095ull;
    }
    return status;
}

#define KEY_POLICY_NTFS L"\\REGISTRY\\MACHINE\\SOFTWARE\\Policies\\Microsoft\\Windows\\FileSystems\\NTFS"
#define KEY_APP_MODEL_UNLOCK L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock"

/* a class of symbolic links the system follows: the policy decides when it has a say, else the file system key. */
static BOOLEAN symlink_class_enabled(PCWSTR value, BOOLEAN fallback)
{
    static PCWSTR keys[] = {KEY_POLICY_NTFS, KEY_FILE_SYSTEM};
    for (ULONG i = 0; i < RTL_NUMBER_OF(keys); i++) {
        ULONG type, data, length = sizeof(data);
        if (NT_SUCCESS(CmQueryValueUnlessBusy(keys[i], value, &type, &data, &length)) && type == 4 /* REG_DWORD */ &&
            length >= sizeof(data))
            return data != 0;
    }
    return fallback;
}

/* with developer mode on a program may make symbolic links without the privilege to. */
static BOOLEAN developer_mode(void)
{
    ULONG type, data, length = sizeof(data);
    return NT_SUCCESS(CmQueryValueUnlessBusy(KEY_APP_MODEL_UNLOCK, L"AllowDevelopmentWithoutDevLicense", &type, &data,
                                             &length)) &&
           type == 4 && length >= sizeof(data) && data != 0;
}

static BOOLEAN names_start(const WCHAR *name, ULONG chars, PCWSTR prefix)
{
    ULONG n = (ULONG)wcslen(prefix);
    if (chars < n)
        return FALSE;
    for (ULONG i = 0; i < n; i++)
        if (fold(name[i]) != fold(prefix[i]))
            return FALSE;
    return TRUE;
}

#define LINK_NAME_LIMIT 1000

/*
 * a link was met on the way of an open. the file system answers with the whole name the open goes on with, in the file
 * object, and the object manager starts again at the root with it. an absolute target stands for itself and the rest of
 * the path follows it. a relative target is a path from the directory of the link, with . and .. taken out, on the device
 * of the volume, and a target that begins with a backslash is a path from the root of the volume.
 */
static NTSTATUS follow_link(MIMEFS_VOLUME *vol, PFILE_OBJECT file, const MimeFsOpen *req)
{
    ULONG tag = req->reparse_tag, sub_chars = req->reparse_substitute, prefix = req->reparse_prefix;
    const WCHAR *sub = req->path;
    ULONG name_chars = file->FileName.Length / sizeof(WCHAR);
    if (!sub_chars || prefix > name_chars || !prefix)
        return STATUS_OBJECT_PATH_NOT_FOUND;
    BOOLEAN relative = tag == IO_REPARSE_TAG_SYMLINK && (req->reparse_flags & SYMLINK_FLAG_RELATIVE);
    if (tag == IO_REPARSE_TAG_SYMLINK) {
        /* a link of a drive to a network path is another class of links than one to a drive. */
        BOOLEAN remote = !relative && (names_start(sub, sub_chars, L"\\??\\UNC\\") || names_start(sub, sub_chars, L"\\Device\\Mup"));
        if (remote ? !symlink_class_enabled(L"SymlinkLocalToRemoteEvaluation", TRUE)
                   : !symlink_class_enabled(L"SymlinkLocalToLocalEvaluation", TRUE))
            return STATUS_SYMLINK_CLASS_DISABLED;
    }
    const WCHAR *rest = file->FileName.Buffer + prefix;
    ULONG rest_chars = name_chars - prefix, n = 0;
    PWCHAR out = ExAllocatePool2(POOL_FLAG_NON_PAGED, (LINK_NAME_LIMIT + 2) * sizeof(WCHAR), KI_TAG('M', 'f', 'L', 'k'));
    if (!out)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (!relative) {
        if (sub_chars + rest_chars >= LINK_NAME_LIMIT)
            goto too_long;
        memcpy(out, sub, sub_chars * sizeof(WCHAR));
        n = sub_chars;
        /* the target of a link to a root ends with the backslash that the rest begins with. */
        if (n && out[n - 1] == '\\' && rest_chars && rest[0] == '\\')
            n--;
    } else {
        struct {
            const WCHAR *Text;
            ULONG Chars;
        } parts[128];
        ULONG count = 0;
        /* the directory of the link, unless the target is a path from the root. */
        if (sub[0] != '\\') {
            ULONG at = 0, end = prefix;
            while (end && file->FileName.Buffer[end - 1] == '\\')
                end--;
            while (end && file->FileName.Buffer[end - 1] != '\\')
                end--;
            while (at < end && count < RTL_NUMBER_OF(parts)) {
                while (at < end && file->FileName.Buffer[at] == '\\')
                    at++;
                ULONG start = at;
                while (at < end && file->FileName.Buffer[at] != '\\')
                    at++;
                if (at > start) {
                    parts[count].Text = file->FileName.Buffer + start;
                    parts[count++].Chars = at - start;
                }
            }
        }
        ULONG at = 0;
        while (at < sub_chars) {
            while (at < sub_chars && sub[at] == '\\')
                at++;
            ULONG start = at;
            while (at < sub_chars && sub[at] != '\\')
                at++;
            ULONG length = at - start;
            if (!length || (length == 1 && sub[start] == '.'))
                continue;
            if (length == 2 && sub[start] == '.' && sub[start + 1] == '.') {
                if (count)
                    count--;
                continue;
            }
            if (count == RTL_NUMBER_OF(parts))
                goto too_long;
            parts[count].Text = sub + start;
            parts[count++].Chars = length;
        }
        PCWSTR device = vol->HostVolume == MIME_FS_VOLUME_SYSTEM ? L"\\Device\\HarddiskVolume1" : L"\\Device\\HarddiskVolume2";
        n = (ULONG)wcslen(device);
        memcpy(out, device, n * sizeof(WCHAR));
        for (ULONG i = 0; i < count; i++) {
            if (n + 1 + parts[i].Chars >= LINK_NAME_LIMIT)
                goto too_long;
            out[n++] = '\\';
            memcpy(out + n, parts[i].Text, parts[i].Chars * sizeof(WCHAR));
            n += parts[i].Chars;
        }
        if (!count)
            out[n++] = '\\';
        if (n + rest_chars >= LINK_NAME_LIMIT)
            goto too_long;
        /* the root has its backslash already. */
        if (!count && rest_chars && rest[0] == '\\')
            n--;
    }
    memcpy(out + n, rest, rest_chars * sizeof(WCHAR));
    n += rest_chars;
    out[n] = 0;
    PWCHAR old = file->FileName.Buffer;
    FsWbNameLock();
    file->FileName.Buffer = out;
    file->FileName.Length = (USHORT)(n * sizeof(WCHAR));
    file->FileName.MaximumLength = (USHORT)((LINK_NAME_LIMIT + 2) * sizeof(WCHAR));
    FsWbNameUnlock();
    if (old)
        ExFreePool(old);
    return STATUS_REPARSE;
too_long:
    ExFreePool(out);
    return STATUS_NAME_TOO_LONG;
}

static NTSTATUS NTAPI mimefs_create(PDEVICE_OBJECT device, PIRP irp)
{
    MIMEFS_VOLUME *vol = device->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PFILE_OBJECT file = stack->FileObject;
    ULONG options = stack->Parameters.Create.Options & 0x00FFFFFF;
    ULONG disposition = stack->Parameters.Create.Options >> 24;
    PIO_SECURITY_CONTEXT security = stack->Parameters.Create.SecurityContext;
    ACCESS_MASK access = security ? security->DesiredAccess : 0;
    /* user mode programs are held to file descriptors, the kernel and drivers are not. */
    BOOLEAN check = (stack->Flags & SL_FORCE_ACCESS_CHECK) && security && SeAccessCheckRequired();

    MimeFsOpen *req = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MimeFsOpen), KI_TAG('M', 'f', 'O', 'p'));
    if (!req)
        return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    ULONG chars = file->FileName.Length / sizeof(WCHAR);
    /* an open by id names the file by its 8 or 16 byte id, with or without the backslash that follows the device. */
    BOOLEAN by_id = (options & FILE_OPEN_BY_FILE_ID) != 0;
    const WCHAR *name = file->FileName.Buffer;
    if (by_id) {
        if (chars && name[0] == '\\' && (chars == 5 || chars == 9)) {
            name++;
            chars--;
        }
        if (disposition != FILE_OPEN || (chars != 4 && chars != 8)) {
            ExFreePool(req);
            return complete(irp, STATUS_INVALID_PARAMETER, 0);
        }
    }
    /* the volume itself, opened as \\.\C:, has no name under the device: it answers the ioctls of the drive. */
    if (!chars) {
        ExFreePool(req);
        if (disposition != FILE_OPEN && disposition != FILE_OPEN_IF)
            return complete(irp, STATUS_OBJECT_NAME_COLLISION, 0);
        file->FsContext = nullptr;
        return complete(irp, STATUS_SUCCESS, FILE_OPENED);
    }
    if (chars > RTL_NUMBER_OF(req->path)) {
        ExFreePool(req);
        return complete(irp, STATUS_NAME_TOO_LONG, 0);
    }
    /* reject wildcards and the characters ntfs refuses. */
    for (ULONG i = 0; i < chars && !by_id; i++) {
        WCHAR c = file->FileName.Buffer[i];
        if (c == '*' || c == '?' || c == '<' || c == '>' || c == '|' || c == '"' || (c == ':' && i != 1)) {
            ExFreePool(req);
            return complete(irp, STATUS_OBJECT_NAME_INVALID, 0);
        }
    }
    if (vol->HostVolume == MIME_FS_VOLUME_SYSTEM && !by_id) {
        const MimeRomEntry *rom = HalRomFind(file->FileName.Buffer, chars);
        if (rom) {
            ExFreePool(req);
            return create_rom(irp, file, rom, access, options, disposition, security, check);
        }
    }
    memcpy(req->path, name, chars * sizeof(WCHAR));
    req->path_chars = chars;
    req->volume = vol->HostVolume;
    req->flags = by_id ? MIME_FS_OPEN_BY_ID : 0;
    if (access & (FILE_READ_DATA | GENERIC_READ | GENERIC_ALL | MAXIMUM_ALLOWED | FILE_EXECUTE | GENERIC_EXECUTE))
        req->flags |= MIME_FS_OPEN_READ;
    if (access & (FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_WRITE | GENERIC_ALL))
        req->flags |= MIME_FS_OPEN_WRITE;
    if (options & FILE_DIRECTORY_FILE)
        req->flags |= MIME_FS_OPEN_DIRECTORY;
    if (options & FILE_NON_DIRECTORY_FILE)
        req->flags |= MIME_FS_OPEN_NON_DIRECTORY;
    if (options & FILE_OPEN_REPARSE_POINT)
        req->flags |= MIME_FS_OPEN_REPARSE_POINT;
    if (check)
        req->flags |= MIME_FS_OPEN_WANT_SD;
    if ((req->flags & MIME_FS_OPEN_READ) && access_time_disabled())
        req->flags |= MIME_FS_OPEN_NO_ACCESS_TIME;
    /* a query by name only needs the attributes: the host answers without making a handle. */
    BOOLEAN stat_only = (stack->Flags & SL_OPEN_STAT_ONLY) != 0 && disposition == FILE_OPEN && !by_id &&
                        !(req->flags & MIME_FS_OPEN_WRITE) && !(file->Flags & FO_DELETE_ON_CLOSE);
    if (stat_only)
        req->flags |= MIME_FS_OPEN_STAT_ONLY;
    ULONG host_disposition;
    switch (disposition) {
    case FILE_SUPERSEDE: host_disposition = MIME_FS_DISP_SUPERSEDE; break;
    case FILE_CREATE: host_disposition = MIME_FS_DISP_CREATE; break;
    case FILE_OPEN_IF: host_disposition = MIME_FS_DISP_OPEN_IF; break;
    case FILE_OVERWRITE: host_disposition = MIME_FS_DISP_OVERWRITE; break;
    case FILE_OVERWRITE_IF: host_disposition = MIME_FS_DISP_OVERWRITE_IF; break;
    default: host_disposition = MIME_FS_DISP_OPEN; break;
    }
    req->disposition = host_disposition;
    /*
     * a name asked about lately is answered from io/fsattr.c. otherwise the host is asked, and the answer is kept if
     * the directory is watched for host changes.
     */
    BOOLEAN keep = FALSE;
    LONG keep_token = 0;
    ULONG64 hs;
    ACCESS_MASK granted = access;
    BOOLEAN checked = FALSE;
    if (stat_only && !check && disposition == FILE_OPEN && !(options & (FILE_DIRECTORY_FILE | FILE_NON_DIRECTORY_FILE))) {
        BOOLEAN missing;
        MimeFsInfo known;
        if (FsAttrLookup(vol->HostVolume, &file->FileName, &missing, &known)) {
            if (missing) {
                ExFreePool(req);
                return complete(irp, host_status(MIME_HC_STATUS_NOT_FOUND, FALSE), 0);
            }
            req->info_out = known;
            req->handle_out = 0;
            req->action_out = FILE_OPENED;
            hs = MIME_HC_STATUS_OK;
            goto opened;
        }
        keep = FsAttrBegin(vol->HostVolume, &file->FileName, &keep_token);
    }
    /* buffered writes for this name are flushed before the host looks at its size, time, or truncates it. */
    if (!by_id)
        FsWbSyncPath(vol->HostVolume, &file->FileName, FALSE);
    /*
     * a file with a section in use is not truncated by an open, as it is not by a size change: a view would read
     * what is gone, and when the pages are the host's that is a fault the host cannot take. the file is first looked
     * up by name, without a handle, to identify it, and its cached pages are dropped before the host truncates it,
     * not after, or a read in between would take pages gone from the window. an open that empties an existing file
     * cannot be undone, so it is checked first: the host opens the name as is with a handle, which also returns the
     * descriptor, and the real open follows once the descriptor allows it.
     */
    if (disposition == FILE_SUPERSEDE || disposition == FILE_OVERWRITE || disposition == FILE_OVERWRITE_IF) {
        ULONG saved_flags = req->flags, saved_disposition = req->disposition;
        if (check)
            req->flags &= ~(ULONG)MIME_FS_OPEN_STAT_ONLY;
        else
            req->flags = MIME_FS_OPEN_STAT_ONLY;
        req->disposition = MIME_FS_DISP_OPEN;
        ULONG64 probe = HalHypercallUnlocked(MIME_HC_FS_OPEN, HalPhysicalOf(req), 0, 0, 0, 0, 0, nullptr);
        if (probe == MIME_HC_STATUS_REPARSE) {
            /* a link on the way: the open goes on with the name it gives, and the host has replaced the path. */
            ULONG tag = req->reparse_tag;
            NTSTATUS status = follow_link(vol, file, req);
            ExFreePool(req);
            return complete(irp, status, status == STATUS_REPARSE ? tag : 0);
        }
        ULONG64 id = req->info_out.file_id;
        if (probe == MIME_HC_STATUS_OK && check) {
            MIMEFS_STORED known = {req->sd_status, req->sd_length, req->sd_distance, req->sd_data};
            NTSTATUS allowed = MimeFsCheckOpen(vol->HostVolume, req->handle_out, &file->FileName,
                                               (req->info_out.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0, access, options,
                                               disposition, FALSE, &known, &granted);
            if (req->handle_out)
                HalHypercallUnlocked(MIME_HC_FS_CLOSE, req->handle_out, 0, 0, 0, 0, 0, nullptr);
            if (!NT_SUCCESS(allowed)) {
                SeTraceDenial("file", &file->FileName, access, allowed);
                ExFreePool(req);
                return complete(irp, allowed, 0);
            }
            checked = TRUE;
        } else if (probe == MIME_HC_STATUS_OK && req->handle_out) {
            HalHypercallUnlocked(MIME_HC_FS_CLOSE, req->handle_out, 0, 0, 0, 0, 0, nullptr);
        }
        req->flags = saved_flags;
        req->disposition = saved_disposition;
        req->handle_out = 0;
        if (probe == MIME_HC_STATUS_OK) {
            if (FsWbIsMapped(vol->HostVolume, id)) {
                ExFreePool(req);
                return complete(irp, STATUS_USER_MAPPED_FILE, 0);
            }
            FsCacheInvalidate(vol->HostVolume, id);
        }
    }
reopen:
    hs = HalHypercallUnlocked(MIME_HC_FS_OPEN, HalPhysicalOf(req), 0, 0, 0, 0, 0, nullptr);
opened:
    if (keep) {
        BOOLEAN found = hs == MIME_HC_STATUS_OK && !req->handle_out;
        BOOLEAN missing = hs == MIME_HC_STATUS_NOT_FOUND && req->action_out == 0;
        FsAttrKeep(vol->HostVolume, &file->FileName, found ? ATTR_KEEP_FILE : missing ? ATTR_KEEP_MISSING : ATTR_KEEP_NOTHING,
                   &req->info_out, keep_token);
    }
    if (hs == MIME_HC_STATUS_REPARSE) {
        ULONG tag = req->reparse_tag;
        NTSTATUS status = follow_link(vol, file, req);
        ExFreePool(req);
        return complete(irp, status, status == STATUS_REPARSE ? tag : 0);
    }
    if (hs != MIME_HC_STATUS_OK) {
        NTSTATUS status = host_status(hs, req->action_out == 1);
        if (SeTraceAccess)
            KiLogPrintf("the host refused the open of '%wZ': status %llu, parent missing %u", &file->FileName, hs, req->action_out);
        ExFreePool(req);
        return complete(irp, status, 0);
    }
    ULONG64 host = req->handle_out;
    ULONG action = req->action_out;
    ULONG req_flags = req->flags;
    MimeFsInfo opened = req->info_out;
    BOOLEAN directory = (opened.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (by_id) {
        /* the file object takes the name the host found the id at. */
        ULONG found = req->path_chars;
        PWCHAR text = ExAllocatePool2(POOL_FLAG_NON_PAGED, (found + 1) * sizeof(WCHAR), KI_TAG('I', 'o', 'F', 'n'));
        if (!text) {
            if (host)
                HalHypercallUnlocked(MIME_HC_FS_CLOSE, host, 0, 0, 0, 0, 0, nullptr);
            ExFreePool(req);
            return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        }
        memcpy(text, req->path, found * sizeof(WCHAR));
        text[found] = 0;
        ExFreePool(file->FileName.Buffer);
        file->FileName.Buffer = text;
        file->FileName.Length = (USHORT)(found * sizeof(WCHAR));
        file->FileName.MaximumLength = (USHORT)((found + 1) * sizeof(WCHAR));
        chars = found;
    }

    if (check && (!checked || action == FILE_CREATED)) {
        MIMEFS_STORED known = {req->sd_status, req->sd_length, req->sd_distance, req->sd_data};
        NTSTATUS allowed = MimeFsCheckOpen(vol->HostVolume, host, &file->FileName, directory, access, options, disposition,
                                           action == FILE_CREATED, &known, &granted);
        if (allowed == STATUS_MORE_PROCESSING_REQUIRED && !host) {
            /* a query by name that needs the parent directory: reopen with a handle so the host can be asked more. */
            req->flags &= ~(ULONG)MIME_FS_OPEN_STAT_ONLY;
            goto reopen;
        }
        if (!NT_SUCCESS(allowed)) {
            SeTraceDenial("file", &file->FileName, access, allowed);
            /* a file created by a refused open is not left behind. */
            if (host && action == FILE_CREATED)
                HalHypercallUnlocked(MIME_HC_FS_DELETE, host, 1, 0, 0, 0, 0, nullptr);
            if (host)
                HalHypercallUnlocked(MIME_HC_FS_CLOSE, host, 0, 0, 0, 0, 0, nullptr);
            ExFreePool(req);
            return complete(irp, allowed, 0);
        }
        checked = TRUE;
    }
    ExFreePool(req);

    MIMEFS_FCB *fcb = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MIMEFS_FCB), KI_TAG('M', 'f', 'F', 'c'));
    if (!fcb) {
        if (host)
            HalHypercallUnlocked(MIME_HC_FS_CLOSE, host, 0, 0, 0, 0, 0, nullptr);
        return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    if ((file->Flags & FO_DELETE_ON_CLOSE) && action == FILE_OPENED && !directory &&
        FsWbIsImageMapped(vol->HostVolume, opened.file_id)) {
        ExFreePool(fcb);
        if (host)
            HalHypercallUnlocked(MIME_HC_FS_CLOSE, host, 0, 0, 0, 0, 0, nullptr);
        return complete(irp, STATUS_CANNOT_DELETE, 0);
    }
    fcb->HostHandle = host;
    fcb->StatOnly = (req_flags & MIME_FS_OPEN_STAT_ONLY) != 0;
    fcb->OpenInfo = opened;
    fcb->Volume = vol->HostVolume;
    fcb->FileId = opened.file_id;
    fcb->Directory = directory;
    fcb->Link = (opened.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && host != 0;
    fcb->Access = granted;
    fcb->NoAccessTime = (req_flags & MIME_FS_OPEN_NO_ACCESS_TIME) != 0;
    fcb->Buffered = (req_flags & MIME_FS_OPEN_WRITE) && !(options & (FILE_WRITE_THROUGH | FILE_NO_INTERMEDIATE_BUFFERING));
    if (!fcb->Directory && !fcb->StatOnly && !fcb->Link)
        FsWbOpen(&fcb->Wb, vol->HostVolume, opened.file_id, host, &file->FileName);
    if (!fcb->Directory && opened.links > 1)
        FsWbNoteLinked(vol->HostVolume, opened.file_id);
    if (!fcb->Directory && !fcb->StatOnly && !fcb->Link) {
        if (req_flags & MIME_FS_OPEN_WRITE || action != FILE_OPENED)
            FsCacheInvalidate(fcb->Volume, fcb->FileId);
        else
            fcb->Cache = FsCacheOpen(fcb->Volume, &opened);
    }
    fcb->IsRoot = chars == 0 || (chars == 1 && file->FileName.Buffer[0] == '\\');
    fcb->DeleteOnClose = (file->Flags & FO_DELETE_ON_CLOSE) != 0;
    if (action == FILE_CREATED) {
        NTSTATUS status = MimeFsAssignNewFile(vol->HostVolume, host, &file->FileName, fcb->Directory,
                                              security ? security->SecurityDescriptor : nullptr, KeGetPreviousMode());
        if (!NT_SUCCESS(status)) {
            if (SeTraceAccess)
                KiLogPrintf("the descriptor of '%wZ' cannot be kept: %08x", &file->FileName, status);
            /* a file the creator may not create that way is not left behind. */
            HalHypercallUnlocked(MIME_HC_FS_DELETE, host, 1, 0, 0, 0, 0, nullptr);
            HalHypercallUnlocked(MIME_HC_FS_CLOSE, host, 0, 0, 0, 0, 0, nullptr);
            FsCacheInvalidate(fcb->Volume, fcb->FileId);
            if (fcb->Cache)
                FsCacheClose(fcb->Cache);
            ExFreePool(fcb);
            return complete(irp, status, 0);
        }
    }
    if (fcb->Directory)
        file->Flags |= FO_DIRECTORY_FILE;
    file->FsContext = fcb;
    if (checked) {
        /* the handle gets what the descriptor granted, and the file object agrees. */
        security->GrantedAccess = granted;
        security->Checked = TRUE;
        file->ReadAccess = (granted & (FILE_READ_DATA | FILE_EXECUTE)) != 0;
        file->WriteAccess = (granted & (FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
        file->DeleteAccess = (granted & DELETE) != 0;
    }
    if (action == FILE_CREATED)
        FsNotifyReport(vol->HostVolume, &file->FileName, FILE_ACTION_ADDED,
                       fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME);
    else if (action == FILE_OVERWRITTEN || action == FILE_SUPERSEDED)
        FsNotifyReport(vol->HostVolume, &file->FileName, FILE_ACTION_MODIFIED,
                       FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
    return complete(irp, STATUS_SUCCESS, action);
}

static NTSTATUS NTAPI mimefs_cleanup(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    MIMEFS_FCB *fcb = file->FsContext;
    if (fcb && !fcb->Rom && fcb->Directory)
        FsNotifyCleanup(file);
    /* the last handle is closing: flush its buffer before anything can look at the file again. */
    if (fcb && !fcb->Rom && !fcb->Directory)
        FsWbClose(&fcb->Wb);
    return complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI mimefs_close(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    MIMEFS_FCB *fcb = file->FsContext;
    if (fcb && fcb->Rom) {
        ExFreePool(fcb);
        file->FsContext = nullptr;
    } else if (fcb) {
        FsWbClose(&fcb->Wb);
        if (fcb->Cache)
            FsCacheClose(fcb->Cache);
        if (fcb->DeleteOnClose)
            FsCacheInvalidate(fcb->Volume, fcb->FileId);
        if (fcb->DeleteOnClose) {
            ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_DELETE, fcb->HostHandle, 1, 0, 0, 0, 0, nullptr);
            if (hs == MIME_HC_STATUS_OK)
                FsNotifyReport(fcb->Volume, &file->FileName, FILE_ACTION_REMOVED,
                               fcb->Directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME);
        }
        if (fcb->HostHandle)
            HalHypercallUnlocked(MIME_HC_FS_CLOSE, fcb->HostHandle, 0, 0, 0, 0, 0, nullptr);
        if (fcb->Batch)
            ExFreePool(fcb->Batch);
        ExFreePool(fcb);
        file->FsContext = nullptr;
    }
    return complete(irp, STATUS_SUCCESS, 0);
}

/* a read sets the access time of the file, at most once a second for a handle. */
static void touch_access(MIMEFS_FCB *fcb)
{
    if (fcb->NoAccessTime || !fcb->HostHandle)
        return;
    LARGE_INTEGER now;
    KeQuerySystemTime(&now);
    if (fcb->LastTouch && now.QuadPart - fcb->LastTouch < 10000000)
        return;
    fcb->LastTouch = now.QuadPart;
    MimeFsBasic *touch = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*touch), KI_TAG('M', 'f', 'B', 's'));
    if (!touch)
        return;
    memset(touch, 0, sizeof(*touch));
    touch->mask = MIME_FS_BASIC_TOUCH;
    HalHypercallUnlocked(MIME_HC_FS_SET_BASIC, fcb->HostHandle, HalPhysicalOf(touch), 0, 0, 0, 0, nullptr);
    ExFreePool(touch);
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
    /* the link has no data. */
    if (fcb->Link)
        return complete(irp, write ? STATUS_ACCESS_DENIED : STATUS_END_OF_FILE, 0);
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
    if (write) {
        FsCacheInvalidate(fcb->Volume, fcb->FileId);
        NTSTATUS kept;
        if (fcb->Buffered && FsWbWrite(&fcb->Wb, offset, user, length, &kept)) {
            if (NT_SUCCESS(kept))
                FsNotifyReport(fcb->Volume, &stack->FileObject->FileName, FILE_ACTION_MODIFIED,
                               FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
            return complete(irp, kept, NT_SUCCESS(kept) ? length : 0);
        }
    }
    if (!write)
        touch_access(fcb);
    /* host reads and writes stay ordered with what handles buffered for the file. */
    NTSTATUS synced = FsWbSync(&fcb->Wb);
    if (!NT_SUCCESS(synced) && write)
        return complete(irp, synced, 0);
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
        ULONG64 hs = HalHypercallUnlocked(write ? MIME_HC_FS_WRITE : MIME_HC_FS_READ, fcb->HostHandle, offset + done, bounce_pa,
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
    if (write && done)
        FsNotifyReport(fcb->Volume, &stack->FileObject->FileName, FILE_ACTION_MODIFIED,
                       FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
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

/*
 * every name of the file on its volume: the count first, then an entry for each name with the id of its directory, the
 * way ntfs gives them. what does not fit is counted in BytesNeeded, so the caller can ask again with room for all.
 */
static NTSTATUS query_links(PFILE_OBJECT file, MIMEFS_FCB *fcb, PUCHAR out, ULONG length, PULONG written)
{
    if (length < 32)
        return STATUS_INFO_LENGTH_MISMATCH;
    PFILE_LINKS_INFORMATION header = (PFILE_LINKS_INFORMATION)out;
    header->BytesNeeded = 8;
    header->EntriesReturned = 0;
    PUCHAR raw = nullptr;
    ULONG64 need = 0;
    /* a file the rom serves has one name, and no directory of its own to give an id for. */
    UCHAR rom_name[sizeof(MimeFsLinkName) + 520 * sizeof(WCHAR)];
    if (fcb->Rom) {
        ULONG chars = file->FileName.Length / sizeof(WCHAR), start = chars;
        while (start && file->FileName.Buffer[start - 1] != '\\')
            start--;
        chars -= start;
        if (chars > 255)
            return STATUS_OBJECT_NAME_INVALID;
        MimeFsLinkName *record = (MimeFsLinkName *)rom_name;
        record->size = (ULONG)((sizeof(*record) + chars * sizeof(WCHAR) + 7) & ~7ull);
        record->name_chars = chars;
        record->parent_id = 0;
        memcpy(record->name, file->FileName.Buffer + start, chars * sizeof(WCHAR));
        raw = rom_name;
        need = record->size;
    } else {
        ULONG64 capacity = 2048;
        for (int attempt = 0; attempt < 3; attempt++) {
            raw = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)capacity, KI_TAG('M', 'f', 'L', 's'));
            if (!raw)
                return STATUS_INSUFFICIENT_RESOURCES;
            ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_LINKS, fcb->HostHandle, HalPhysicalOf(raw), capacity, 0, 0, 0, &need);
            if (hs != MIME_HC_STATUS_OK) {
                ExFreePool(raw);
                return host_status(hs, FALSE);
            }
            if (need <= capacity)
                break;
            ExFreePool(raw);
            raw = nullptr;
            capacity = need;
        }
        if (!raw)
            return STATUS_UNSUCCESSFUL;
    }
    ULONG room = length - 8;
    PUCHAR cursor = out + 8;
    PFILE_LINK_ENTRY_INFORMATION last = nullptr;
    BOOLEAN overflow = FALSE;
    for (ULONG64 at = 0; at < need;) {
        const MimeFsLinkName *record = (const MimeFsLinkName *)(raw + at);
        ULONG size = (ULONG)((offsetof(FILE_LINK_ENTRY_INFORMATION, FileName) + record->name_chars * sizeof(WCHAR) + 7) & ~7ull);
        header->BytesNeeded += size;
        if (!overflow && room >= size) {
            PFILE_LINK_ENTRY_INFORMATION entry = (PFILE_LINK_ENTRY_INFORMATION)cursor;
            memset(entry, 0, size);
            entry->NextEntryOffset = size;
            entry->ParentFileId = (LONGLONG)record->parent_id;
            entry->FileNameLength = record->name_chars;
            memcpy(entry->FileName, record->name, record->name_chars * sizeof(WCHAR));
            last = entry;
            cursor += size;
            room -= size;
            header->EntriesReturned++;
        } else {
            overflow = TRUE;
        }
        at += record->size;
    }
    if (last)
        last->NextEntryOffset = 0;
    if (!fcb->Rom)
        ExFreePool(raw);
    *written = (ULONG)(cursor - out);
    return overflow ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
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
        ((PFILE_ATTRIBUTE_TAG_INFORMATION)out)->ReparseTag =
            (info->attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? info->reparse_tag : 0;
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
    case FileStandardLinkInformation: {
        if (length < sizeof(FILE_STANDARD_LINK_INFORMATION)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        PFILE_STANDARD_LINK_INFORMATION l = (PFILE_STANDARD_LINK_INFORMATION)out;
        l->NumberOfAccessibleLinks = l->TotalNumberOfLinks = info->links ? info->links : 1;
        l->DeletePending = fcb->DeleteOnClose;
        l->Directory = fcb->Directory;
        written = sizeof(FILE_STANDARD_LINK_INFORMATION);
        break;
    }
    case FileIdInformation: {
        if (length < sizeof(FILE_ID_INFORMATION)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        PFILE_ID_INFORMATION id = (PFILE_ID_INFORMATION)out;
        MIMEFS_VOLUME *vol = device->DeviceExtension;
        memset(id, 0, sizeof(*id));
        id->VolumeSerialNumber = vol->Serial;
        memcpy(id->FileId, &info->file_id, sizeof(info->file_id));
        written = sizeof(FILE_ID_INFORMATION);
        break;
    }
    case FileStatInformation:
    case FileStatLxInformation: {
        BOOLEAN lx = stack->Parameters.QueryFile.FileInformationClass == FileStatLxInformation;
        ULONG need = lx ? sizeof(FILE_STAT_LX_INFORMATION) : sizeof(FILE_STAT_INFORMATION);
        if (length < need) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        memset(out, 0, need);
        PFILE_STAT_INFORMATION st = (PFILE_STAT_INFORMATION)out;
        st->FileId.QuadPart = (LONGLONG)info->file_id;
        st->CreationTime.QuadPart = (LONGLONG)info->creation_time;
        st->LastAccessTime.QuadPart = (LONGLONG)info->access_time;
        st->LastWriteTime.QuadPart = (LONGLONG)info->write_time;
        st->ChangeTime.QuadPart = (LONGLONG)info->change_time;
        st->AllocationSize.QuadPart = (LONGLONG)info->allocation_size;
        st->EndOfFile.QuadPart = (LONGLONG)info->size;
        st->FileAttributes = info->attributes ? info->attributes : FILE_ATTRIBUTE_NORMAL;
        st->NumberOfLinks = info->links ? info->links : 1;
        st->ReparseTag = (info->attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? info->reparse_tag : 0;
        st->EffectiveAccess = fcb->Rom ? 0x1200A9 /* read and execute. */
                                       : MimeFsMaximumAccess(fcb->Volume, fcb->HostHandle, &file->FileName, fcb->Directory);
        written = need;
        break;
    }
    case FileHardLinkInformation:
        status = query_links(file, fcb, out, length, &written);
        break;
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
        s->NumberOfLinks = info->links ? info->links : 1;
        s->DeletePending = fcb->DeleteOnClose;
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

/*
 * moves the file into the directory the i/o manager opened for it. the leaf is the part of the new name after its
 * last backslash, and the file object takes its new name in the directory.
 */
static NTSTATUS rename_in_directory(PDEVICE_OBJECT device, PFILE_OBJECT file, MIMEFS_FCB *fcb, PIO_STACK_LOCATION stack,
                                    const UCHAR *in, ULONG length)
{
    PFILE_OBJECT dir = stack->Parameters.SetFile.FileObject;
    MIMEFS_FCB *dfcb = dir ? dir->FsContext : nullptr;
    /* no directory is a new name in the file's own. */
    if (dir && (!dfcb || dir->DeviceObject != device))
        return STATUS_NOT_SAME_DEVICE;
    if (dir && (dfcb->Rom || !dfcb->Directory))
        return STATUS_NOT_A_DIRECTORY;
    const WCHAR *name = (const WCHAR *)(in + 20);
    ULONG chars = *(PULONG)(in + 16) / sizeof(WCHAR), start = chars;
    while (start && name[start - 1] != '\\')
        start--;
    ULONG leaf = chars - start;
    if (!leaf || leaf > 255)
        return STATUS_OBJECT_NAME_INVALID;
    for (ULONG i = 0; i < leaf; i++) {
        WCHAR c = name[start + i];
        if (c < ' ' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|' || c == '"' || c == ':')
            return STATUS_OBJECT_NAME_INVALID;
    }
    PWCHAR text = ExAllocatePool2(POOL_FLAG_NON_PAGED, leaf * sizeof(WCHAR), KI_TAG('M', 'f', 'R', 'n'));
    if (!text)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(text, name + start, leaf * sizeof(WCHAR));
    /* buffered writes for the file, or for anything below a directory, are flushed before the name changes. */
    if (fcb->Directory)
        FsWbSyncPath(fcb->Volume, &file->FileName, TRUE);
    else
        FsWbSync(&fcb->Wb);
    ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_RENAME, fcb->HostHandle, dfcb ? dfcb->HostHandle : 0, HalPhysicalOf(text), leaf,
                                      stack->Parameters.SetFile.ReplaceIfExists ? MIME_FS_RENAME_REPLACE : 0, 0, nullptr);
    NTSTATUS status = host_status(hs, FALSE);
    if (NT_SUCCESS(status)) {
        /* the volume relative name: the directory's, a backslash and the leaf. */
        PCUNICODE_STRING where = dir ? &dir->FileName : &file->FileName;
        ULONG base = where->Length / sizeof(WCHAR);
        while (!dir && base && where->Buffer[base - 1] != '\\')
            base--;
        while (base && where->Buffer[base - 1] == '\\')
            base--;
        PWCHAR full = ExAllocatePool2(POOL_FLAG_NON_PAGED, (base + 1 + leaf + 1) * sizeof(WCHAR), KI_TAG('I', 'o', 'F', 'n'));
        if (full) {
            memcpy(full, where->Buffer, base * sizeof(WCHAR));
            full[base] = '\\';
            memcpy(full + base + 1, text, leaf * sizeof(WCHAR));
            full[base + 1 + leaf] = 0;
            PWCHAR old = file->FileName.Buffer;
            FsWbNameLock();
            file->FileName.Buffer = full;
            file->FileName.Length = (USHORT)((base + 1 + leaf) * sizeof(WCHAR));
            file->FileName.MaximumLength = (USHORT)(file->FileName.Length + sizeof(WCHAR));
            FsWbNameUnlock();
            if (old)
                ExFreePool(old);
        }
    }
    ExFreePool(text);
    return status;
}

/* the volume relative name of a leaf in a directory, or in the directory of the file when none is given. */
static PWCHAR name_in_directory(PFILE_OBJECT file, PFILE_OBJECT dir, const WCHAR *leaf, ULONG leaf_chars, PULONG chars)
{
    PCUNICODE_STRING base = dir ? &dir->FileName : &file->FileName;
    ULONG n = base->Length / sizeof(WCHAR);
    if (!dir) {
        while (n && base->Buffer[n - 1] != '\\')
            n--;
    }
    while (n && base->Buffer[n - 1] == '\\')
        n--;
    PWCHAR full = ExAllocatePool2(POOL_FLAG_NON_PAGED, (n + 1 + leaf_chars + 1) * sizeof(WCHAR), KI_TAG('I', 'o', 'F', 'n'));
    if (!full)
        return nullptr;
    memcpy(full, base->Buffer, n * sizeof(WCHAR));
    full[n] = '\\';
    memcpy(full + n + 1, leaf, leaf_chars * sizeof(WCHAR));
    full[n + 1 + leaf_chars] = 0;
    *chars = n + 1 + leaf_chars;
    return full;
}

static BOOLEAN same_directory(PFILE_OBJECT file, PFILE_OBJECT dir)
{
    if (!dir)
        return TRUE;
    ULONG a = file->FileName.Length / sizeof(WCHAR), b = dir->FileName.Length / sizeof(WCHAR);
    while (a && file->FileName.Buffer[a - 1] != '\\')
        a--;
    while (a > 1 && file->FileName.Buffer[a - 1] == '\\')
        a--;
    while (b > 1 && dir->FileName.Buffer[b - 1] == '\\')
        b--;
    if (a != b)
        return FALSE;
    for (ULONG i = 0; i < a; i++) {
        if (fold(file->FileName.Buffer[i]) != fold(dir->FileName.Buffer[i]))
            return FALSE;
    }
    return TRUE;
}

/*
 * adds a name to the file, in the directory the i/o manager opened or in the file's own. a name that is already there
 * is an error unless it is to be replaced, and a file that is itself called that keeps its name either way. a file has
 * 1024 names at most, and it has one security descriptor, which every name shows.
 */
static NTSTATUS link_in_directory(PDEVICE_OBJECT device, PFILE_OBJECT file, MIMEFS_FCB *fcb, PIO_STACK_LOCATION stack,
                                  const UCHAR *in, ULONG length)
{
    BOOLEAN ex = stack->Parameters.SetFile.FileInformationClass == FileLinkInformationEx;
    ULONG flags = ex ? *(const ULONG *)in : (in[0] ? FILE_LINK_REPLACE_IF_EXISTS : 0);
    /* the bits that mean something to a file system that keeps storage reserves; no other is accepted. */
    if (flags & ~0x1FBu)
        return STATUS_NOT_SUPPORTED;
    if (fcb->Directory)
        return STATUS_FILE_IS_A_DIRECTORY;
    if (fcb->StatOnly)
        return STATUS_ACCESS_DENIED;
    /* the file's own descriptor has to allow writing its attributes, whatever the handle was opened for. */
    if ((stack->Flags & SL_LINK_CHECK_SOURCE) && !fcb->Rom) {
        NTSTATUS allowed = MimeFsCheckRight(fcb->Volume, fcb->HostHandle, &file->FileName, FALSE, FILE_WRITE_ATTRIBUTES);
        if (!NT_SUCCESS(allowed))
            return allowed;
    }
    PFILE_OBJECT dir = stack->Parameters.SetFile.FileObject;
    MIMEFS_FCB *dfcb = dir ? dir->FsContext : nullptr;
    if (dir && (!dfcb || dir->DeviceObject != device))
        return STATUS_NOT_SAME_DEVICE;
    if (dir && (dfcb->Rom || !dfcb->Directory))
        return STATUS_NOT_A_DIRECTORY;
    const WCHAR *name = (const WCHAR *)(in + 20);
    ULONG chars = *(const ULONG *)(in + 16) / sizeof(WCHAR), start = chars;
    while (start && name[start - 1] != '\\')
        start--;
    ULONG leaf = chars - start;
    BOOLEAN dots = TRUE;
    if (!leaf || leaf > 255)
        return STATUS_OBJECT_NAME_INVALID;
    for (ULONG i = 0; i < leaf; i++) {
        WCHAR c = name[start + i];
        if (c < ' ' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|' || c == '"' || c == ':' || c == '/')
            return STATUS_OBJECT_NAME_INVALID;
        if (c != '.')
            dots = FALSE;
    }
    if (dots && leaf <= 2)
        return STATUS_OBJECT_NAME_INVALID;
    if (fcb->DeleteOnClose)
        return STATUS_DELETE_PENDING;

    /* the file gives every name its descriptor: one that only follows the directory is written to it first. */
    if (!same_directory(file, dir)) {
        NTSTATUS pinned = MimeFsPinSecurity(fcb->Volume, fcb->HostHandle, &file->FileName);
        if (!NT_SUCCESS(pinned))
            return pinned;
    }
    PWCHAR text = ExAllocatePool2(POOL_FLAG_NON_PAGED, leaf * sizeof(WCHAR), KI_TAG('M', 'f', 'L', 'k'));
    if (!text)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(text, name + start, leaf * sizeof(WCHAR));
    FsWbSync(&fcb->Wb);
    ULONG64 host_flags = ((flags & FILE_LINK_REPLACE_IF_EXISTS) ? MIME_FS_LINK_REPLACE : 0) |
                         ((flags & FILE_LINK_IGNORE_READONLY_ATTRIBUTE) ? MIME_FS_LINK_IGNORE_READONLY : 0);
    ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_LINK, fcb->HostHandle, dfcb ? dfcb->HostHandle : 0, HalPhysicalOf(text),
                                      leaf, host_flags, 0, nullptr);
    NTSTATUS status = host_status(hs, FALSE);
    if (NT_SUCCESS(status)) {
        ULONG added_chars;
        PWCHAR added = name_in_directory(file, dir, text, leaf, &added_chars);
        MimeFsSecurityChanged();
        FsWbNoteLinked(fcb->Volume, fcb->FileId);
        /* the other names of the file, and the one that was linked, have a count the programs have asked about. */
        FsAttrChanged(fcb->Volume, file->FileName.Buffer, file->FileName.Length / sizeof(WCHAR), FALSE);
        if (added) {
            UNICODE_STRING path = {(USHORT)(added_chars * sizeof(WCHAR)), (USHORT)((added_chars + 1) * sizeof(WCHAR)), added};
            FsNotifyReport(fcb->Volume, &path, FILE_ACTION_ADDED, FILE_NOTIFY_CHANGE_FILE_NAME);
            ExFreePool(added);
        }
    }
    ExFreePool(text);
    return status;
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
        /* like windows, a file with a section in use is not truncated: a view would read what is gone, and when
           the pages are the host's that is a fault the host cannot take. */
        if (FsWbIsMapped(fcb->Volume, fcb->FileId)) {
            MimeFsInfo *current = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*current), KI_TAG('M', 'f', 'I', 'n'));
            BOOLEAN shorter = current && NT_SUCCESS(query(fcb, current)) && size < current->size;
            if (current)
                ExFreePool(current);
            if (shorter)
                return complete(irp, STATUS_USER_MAPPED_FILE, 0);
        }
        FsCacheInvalidate(fcb->Volume, fcb->FileId);
        FsWbSync(&fcb->Wb);
        ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_SET_SIZE, fcb->HostHandle, size, 0, 0, 0, 0, nullptr);
        if (hs == MIME_HC_STATUS_OK)
            FsNotifyReport(fcb->Volume, &stack->FileObject->FileName, FILE_ACTION_MODIFIED,
                           FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        return complete(irp, host_status(hs, FALSE), 0);
    }
    case FileDispositionInformation:
    case FileDispositionInformationEx: {
        BOOLEAN ex = stack->Parameters.SetFile.FileInformationClass == FileDispositionInformationEx;
        if (length < (ex ? sizeof(ULONG) : 1))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        BOOLEAN remove = ex ? (*(PULONG)in & 1) != 0 : ((PFILE_DISPOSITION_INFORMATION)in)->DeleteFile;
        /* a directory with entries cannot be marked, the caller learns it now and not at the close. */
        if (remove && fcb->Directory && fcb->HostHandle &&
            HalHypercallUnlocked(MIME_HC_FS_DELETE, fcb->HostHandle, 1, 0, 0, 0, 0, nullptr) == MIME_HC_STATUS_NOT_EMPTY)
            return complete(irp, STATUS_DIRECTORY_NOT_EMPTY, 0);
        /* a file whose image is mapped, a loaded dll or a running program, stays. */
        if (remove && !fcb->Directory && FsWbIsImageMapped(fcb->Volume, fcb->FileId))
            return complete(irp, STATUS_CANNOT_DELETE, 0);
        if (!remove && fcb->Directory && fcb->HostHandle)
            HalHypercallUnlocked(MIME_HC_FS_DELETE, fcb->HostHandle, 0, 0, 0, 0, 0, nullptr);
        fcb->DeleteOnClose = remove;
        return complete(irp, STATUS_SUCCESS, 0);
    }
    case FileLinkInformation:
    case FileLinkInformationEx:
        if (length < 24)
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        return complete(irp, link_in_directory(device, stack->FileObject, fcb, stack, in, length), 0);
    case FileRenameInformation:
    case FileRenameInformationEx: {
        PFILE_OBJECT file = stack->FileObject;
        UNICODE_STRING from = {};
        if (file->FileName.Length && (from.Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, file->FileName.Length, KI_TAG('M', 'f', 'N', 'm')))) {
            memcpy(from.Buffer, file->FileName.Buffer, file->FileName.Length);
            from.Length = from.MaximumLength = file->FileName.Length;
        }
        MimeFsSecurityChanged();
        NTSTATUS status = rename_in_directory(device, file, fcb, stack, in, length);
        MimeFsSecurityChanged();
        if (NT_SUCCESS(status) && from.Buffer)
            FsNotifyRename(fcb->Volume, &from, &file->FileName, fcb->Directory);
        ExFreePool(from.Buffer);
        return complete(irp, status, 0);
    }
    case FileBasicInformation: {
        if (length < sizeof(FILE_BASIC_INFORMATION))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        const FILE_BASIC_INFORMATION *basic = (const void *)in;
        /* a time below 0 is only -1, which keeps the time from updating, or -2, which lets it update again. */
        const LONGLONG given[4] = {basic->CreationTime.QuadPart, basic->LastAccessTime.QuadPart,
                                   basic->LastWriteTime.QuadPart, basic->ChangeTime.QuadPart};
        for (ULONG i = 0; i < 4; i++)
            if (given[i] < -2)
                return complete(irp, STATUS_INVALID_PARAMETER, 0);
        MimeFsBasic *change = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*change), KI_TAG('M', 'f', 'B', 's'));
        if (!change)
            return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        memset(change, 0, sizeof(*change));
        /* a time of 0 is left as it is. one that is set stops updating for this handle, like -1. */
        if (given[0] > 0) {
            change->creation_time = (ULONG64)given[0];
            change->mask |= MIME_FS_BASIC_CREATION;
        }
        static const struct {
            ULONG Index, Mask, Time;
        } times[3] = {{1, MIME_FS_BASIC_ACCESS, MIME_FS_TIME_ACCESS},
                      {2, MIME_FS_BASIC_WRITE, MIME_FS_TIME_WRITE},
                      {3, MIME_FS_BASIC_CHANGE, MIME_FS_TIME_CHANGE}};
        ULONG64 *values[3] = {&change->access_time, &change->write_time, &change->change_time};
        for (ULONG i = 0; i < 3; i++) {
            LONGLONG value = given[times[i].Index];
            if (value > 0) {
                *values[i] = (ULONG64)value;
                change->mask |= times[i].Mask;
                change->freeze |= times[i].Time;
            } else if (value == -1) {
                change->freeze |= times[i].Time;
            } else if (value == -2) {
                change->thaw |= times[i].Time;
            }
        }
        if (basic->FileAttributes) {
            change->attributes = basic->FileAttributes;
            change->mask |= MIME_FS_BASIC_READONLY;
        }
        NTSTATUS status = STATUS_SUCCESS;
        if ((change->mask || change->freeze || change->thaw) && !fcb->StatOnly) {
            /* buffered writes of this file go first, or the host would stamp the file with them afterwards. */
            FsWbSyncOthers(&fcb->Wb);
            FsWbSync(&fcb->Wb);
            status = host_status(HalHypercallUnlocked(MIME_HC_FS_SET_BASIC, fcb->HostHandle, HalPhysicalOf(change), 0, 0, 0, 0,
                                                       nullptr), FALSE);
            if (NT_SUCCESS(status) && change->mask) {
                ULONG filter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES;
                if (change->mask & MIME_FS_BASIC_ACCESS)
                    filter |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
                if (change->mask & MIME_FS_BASIC_CREATION)
                    filter |= FILE_NOTIFY_CHANGE_CREATION;
                FsNotifyReport(fcb->Volume, &stack->FileObject->FileName, FILE_ACTION_MODIFIED, filter);
            }
        }
        ExFreePool(change);
        return complete(irp, status, 0);
    }
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
    /* the size of the extended attributes of a link is its tag, which FindFirstFile gives as dwReserved0. */
    ULONG tag = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) ? info->reparse_tag : 0;
    switch (klass) {
    case FileDirectoryInformation:
        memcpy(d->FileName, name, name_bytes);
        break;
    case FileFullDirectoryInformation:
        ((PFILE_FULL_DIR_INFORMATION)out)->EaSize = tag;
        memcpy(((PFILE_FULL_DIR_INFORMATION)out)->FileName, name, name_bytes);
        break;
    case FileIdFullDirectoryInformation:
        ((PFILE_FULL_DIR_INFORMATION)out)->EaSize = tag;
        *(PLARGE_INTEGER)(out + offsetof(FILE_FULL_DIR_INFORMATION, FileName)) = (LARGE_INTEGER){.QuadPart = (LONGLONG)info->file_id};
        memcpy(out + offsetof(FILE_FULL_DIR_INFORMATION, FileName) + 8, name, name_bytes);
        break;
    case FileBothDirectoryInformation:
        ((PFILE_BOTH_DIR_INFORMATION)out)->EaSize = tag;
        memcpy(((PFILE_BOTH_DIR_INFORMATION)out)->FileName, name, name_bytes);
        break;
    case FileIdBothDirectoryInformation:
        ((PFILE_ID_BOTH_DIR_INFORMATION)out)->EaSize = tag;
        ((PFILE_ID_BOTH_DIR_INFORMATION)out)->FileId.QuadPart = (LONGLONG)info->file_id;
        memcpy(((PFILE_ID_BOTH_DIR_INFORMATION)out)->FileName, name, name_bytes);
        break;
    default:
        break;
    }
}

/* the host lists a directory several entries per call, instead of one call per entry. */
#define DIR_BATCH 16

static BOOLEAN host_dir_entry(MIMEFS_FCB *fcb, ULONG index, MimeFsDirEntry *out)
{
    if (!fcb->Batch)
        fcb->Batch = ExAllocatePool2(POOL_FLAG_NON_PAGED, DIR_BATCH * sizeof(MimeFsDirEntry), KI_TAG('M', 'f', 'D', 'b'));
    if (!fcb->Batch) {
        ULONG64 more = 0;
        ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_READDIR, fcb->HostHandle, index, HalPhysicalOf(out), 0, 0, 0, &more);
        return hs == MIME_HC_STATUS_OK && more;
    }
    if (index < fcb->BatchFirst || index - fcb->BatchFirst >= fcb->BatchCount) {
        ULONG64 got = 0;
        ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_READDIR, fcb->HostHandle, index, HalPhysicalOf(fcb->Batch), DIR_BATCH, 0, 0,
                                          &got);
        fcb->BatchCount = 0;
        if (hs != MIME_HC_STATUS_OK || !got || got > DIR_BATCH)
            return FALSE;
        fcb->BatchFirst = index;
        fcb->BatchCount = (ULONG)got;
    }
    *out = fcb->Batch[index - fcb->BatchFirst];
    return TRUE;
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

    BOOLEAN first_query = fcb->Pattern.Buffer == nullptr;
    if ((stack->Flags & SL_RESTART_SCAN) || first_query) {
        fcb->DirIndex = 0;
        fcb->BatchCount = 0; /* the host lists the directory again for index 0. */
        fcb->RomIndex = 0;
        fcb->HostDone = FALSE;
        fcb->DotIndex = 0;
        fcb->DotsDone = fcb->IsRoot;
        /* the listing's sizes come from the host: flush buffered writes for files of this directory first. */
        FsWbSyncPath(fcb->Volume, &stack->FileObject->FileName, TRUE);
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
            HalHypercallUnlocked(MIME_HC_FS_QUERY, fcb->HostHandle, HalPhysicalOf(self), 0, 0, 0, 0, nullptr);
            name = dot;
            name_chars = fcb->DotIndex + 1;
            index = fcb->DirIndex;
        } else if (!fcb->HostDone) {
            if (!host_dir_entry(fcb, fcb->DirIndex, de)) {
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
        ((PULONG)out)[0] = 0x00000003 | 0x00000004 | 0x00000002; /* case preserved, unicode names. */
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
        MimeFsVolumeInfo *volume = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*volume), KI_TAG('F', 's', 'V', 'l'));
        if (!volume)
            return complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        NTSTATUS status = host_status(HalHypercall(MIME_HC_FS_VOLUME_QUERY, vol->HostVolume,
                                                   HalPhysicalOf(volume), 0, 0, 0, 0, nullptr), FALSE);
        if (NT_SUCCESS(status) && (!volume->bytes_per_unit || volume->bytes_per_unit % 512 ||
            volume->total_units > INT64_MAX || volume->available_units > INT64_MAX || volume->free_units > INT64_MAX))
            status = STATUS_UNSUCCESSFUL;
        if (!NT_SUCCESS(status)) {
            ExFreePool(volume);
            return complete(irp, status, 0);
        }
        PLARGE_INTEGER v = (PLARGE_INTEGER)out;
        v[0].QuadPart = (LONGLONG)volume->total_units;
        if (need == 32) {
            v[1].QuadPart = (LONGLONG)volume->available_units;
            v[2].QuadPart = (LONGLONG)volume->free_units;
            ((PULONG)(out + 24))[0] = volume->bytes_per_unit / 512;
            ((PULONG)(out + 24))[1] = 512;
        } else {
            v[1].QuadPart = (LONGLONG)volume->free_units;
            ((PULONG)(out + 16))[0] = volume->bytes_per_unit / 512;
            ((PULONG)(out + 16))[1] = 512;
        }
        ExFreePool(volume);
        return complete(irp, STATUS_SUCCESS, need);
    }
    default:
        return complete(irp, STATUS_INVALID_INFO_CLASS, 0);
    }
}

/* a new label lasts as long as the machine runs: the host keeps no label of a volume to change. */
static NTSTATUS NTAPI mimefs_set_volume(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    MIMEFS_VOLUME *vol = device->DeviceExtension;
    PUCHAR in = irp->UserBuffer;
    ULONG length = stack->Parameters.QueryVolume.Length;
    if (stack->Parameters.QueryVolume.FsInformationClass != FileFsLabelInformation)
        return complete(irp, STATUS_INVALID_INFO_CLASS, 0);
    if (length < sizeof(ULONG))
        return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
    ULONG bytes = ((PFILE_FS_LABEL_INFORMATION)in)->VolumeLabelLength;
    if (bytes > length - FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel))
        return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
    ULONG chars = bytes / sizeof(WCHAR);
    if (chars >= RTL_NUMBER_OF(vol->Label))
        return complete(irp, STATUS_INVALID_VOLUME_LABEL, 0);
    memcpy(vol->Label, ((PFILE_FS_LABEL_INFORMATION)in)->VolumeLabel, chars * sizeof(WCHAR));
    vol->Label[chars] = 0;
    return complete(irp, STATUS_SUCCESS, 0);
}

/* the whole descriptor as a reference in the information, see IopGetFileSecurity. */
static NTSTATUS NTAPI mimefs_query_security(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    MIMEFS_FCB *fcb = file->FsContext;
    MIMEFS_VOLUME *vol = device->DeviceExtension;
    if (!fcb)
        return complete(irp, STATUS_INVALID_HANDLE, 0);
    SD_BLOB *sd;
    NTSTATUS status = fcb->Rom ? MimeFsRomSecurity(&sd)
                               : MimeFsEffectiveSecurity(vol->HostVolume, fcb->HostHandle, &file->FileName, 0,
                                                         fcb->Directory, &sd);
    return complete(irp, status, NT_SUCCESS(status) ? (ULONG_PTR)sd : 0);
}

static NTSTATUS NTAPI mimefs_set_security(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    MIMEFS_FCB *fcb = stack->FileObject->FsContext;
    if (!fcb)
        return complete(irp, STATUS_INVALID_HANDLE, 0);
    /* the files the machine ships are read only. */
    if (fcb->Rom)
        return complete(irp, STATUS_ACCESS_DENIED, 0);
    return complete(irp, MimeFsStoreSecurity(fcb->HostHandle, stack->Parameters.Others.Argument1), 0);
}

/*
 * the storage ioctls a fingerprint of the machine reads: the device number, the descriptor of the nvme drive
 * the reference machine ships, its geometry and the extents of the volume. a volume has no bus of its own, so the
 * answers name the drive the way stornvme reports one.
 */
#define IOCTL_STORAGE_QUERY_PROPERTY 0x002D1400u
#define IOCTL_STORAGE_GET_DEVICE_NUMBER 0x002D1080u
#define IOCTL_STORAGE_CHECK_VERIFY 0x002D4800u
#define IOCTL_DISK_GET_DRIVE_GEOMETRY_EX 0x000700A0u
#define IOCTL_DISK_GET_LENGTH_INFO 0x0007405Cu
#define IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS 0x00560000u

typedef struct MIMEFS_DEVICE_NUMBER {
    ULONG DeviceType;
    ULONG DeviceNumber;
    ULONG PartitionNumber;
} MIMEFS_DEVICE_NUMBER;

typedef struct MIMEFS_DEVICE_DESCRIPTOR {
    ULONG Version;
    ULONG Size;
    UCHAR DeviceType;
    UCHAR DeviceTypeModifier;
    BOOLEAN RemovableMedia;
    BOOLEAN CommandQueueing;
    ULONG VendorIdOffset;
    ULONG ProductIdOffset;
    ULONG ProductRevisionOffset;
    ULONG SerialNumberOffset;
    ULONG BusType;
    ULONG RawPropertiesLength;
} MIMEFS_DEVICE_DESCRIPTOR;

typedef struct MIMEFS_DISK_GEOMETRY {
    LONGLONG Cylinders;
    ULONG MediaType;
    ULONG TracksPerCylinder;
    ULONG SectorsPerTrack;
    ULONG BytesPerSector;
} MIMEFS_DISK_GEOMETRY;

static NTSTATUS volume_bytes(MIMEFS_VOLUME *vol, ULONGLONG *bytes)
{
    MimeFsVolumeInfo *info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*info), KI_TAG('F', 's', 'V', 'l'));
    if (!info)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = host_status(HalHypercall(MIME_HC_FS_VOLUME_QUERY, vol->HostVolume, HalPhysicalOf(info), 0, 0, 0,
                                               0, nullptr), FALSE);
    if (NT_SUCCESS(status))
        *bytes = info->total_units * info->bytes_per_unit;
    ExFreePool(info);
    return status;
}

/* a serial of the drive, from the uuid of the machine so it keeps. */
static void drive_serial(PUCHAR out)
{
    static const char hex[] = "0123456789abcdef";
    for (ULONG i = 0; i < 12; i++)
        out[i] = hex[HalBootInfo->system_uuid[i % 16] & 0xF];
    out[12] = 0;
}

static NTSTATUS storage_query(PIRP irp, PIO_STACK_LOCATION stack, MIMEFS_VOLUME *vol, ULONG length)
{
    /* only the standard query of the device property is answered. */
    if (stack->Parameters.DeviceIoControl.InputBufferLength < 8)
        return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
    const ULONG *query = irp->UserBuffer;
    if (query[0] != 0 || query[1] != 0)
        return complete(irp, STATUS_INVALID_PARAMETER, 0);

    static const char vendor[] = "NVMe    ";
#if defined(__aarch64__)
    static const char product[] = "KBG40ZNS1T02 NVMe KIOXIA 1024GB";
#else
    static const char product[] = "SAMSUNG MZVL21T0HCLR-00BL2";
#endif
    static const char revision[] = "AGXA0101";
    char serial[16];
    drive_serial((PUCHAR)serial);
    ULONG need = sizeof(MIMEFS_DEVICE_DESCRIPTOR) + sizeof(vendor) + sizeof(product) + sizeof(revision) + sizeof(serial);
    if (length < need)
        return complete(irp, STATUS_BUFFER_TOO_SMALL, need);

    MIMEFS_DEVICE_DESCRIPTOR *desc = (MIMEFS_DEVICE_DESCRIPTOR *)irp->UserBuffer;
    memset(desc, 0, need);
    desc->Version = sizeof(*desc);
    desc->Size = need;
    desc->DeviceType = 0x07; /* disk */
    desc->RemovableMedia = FALSE;
    desc->CommandQueueing = TRUE;
    desc->BusType = 0x11; /* Nvme */
    ULONG at = sizeof(*desc);
    desc->VendorIdOffset = at;
    memcpy((PUCHAR)desc + at, vendor, sizeof(vendor));
    at += sizeof(vendor);
    desc->ProductIdOffset = at;
    memcpy((PUCHAR)desc + at, product, sizeof(product));
    at += sizeof(product);
    desc->ProductRevisionOffset = at;
    memcpy((PUCHAR)desc + at, revision, sizeof(revision));
    at += sizeof(revision);
    desc->SerialNumberOffset = at;
    memcpy((PUCHAR)desc + at, serial, sizeof(serial));
    at += sizeof(serial);
    (void)vol;
    return complete(irp, STATUS_SUCCESS, need);
}

static NTSTATUS NTAPI mimefs_device_control(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    MIMEFS_VOLUME *vol = device->DeviceExtension;
    ULONG length = stack->Parameters.DeviceIoControl.OutputBufferLength;
    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_STORAGE_QUERY_PROPERTY:
        return storage_query(irp, stack, vol, length);
    case IOCTL_STORAGE_GET_DEVICE_NUMBER: {
        if (length < sizeof(MIMEFS_DEVICE_NUMBER))
            return complete(irp, STATUS_BUFFER_TOO_SMALL, sizeof(MIMEFS_DEVICE_NUMBER));
        MIMEFS_DEVICE_NUMBER *number = (MIMEFS_DEVICE_NUMBER *)irp->UserBuffer;
        number->DeviceType = FILE_DEVICE_DISK;
        number->DeviceNumber = vol->HostVolume;
        number->PartitionNumber = vol->HostVolume + 1;
        return complete(irp, STATUS_SUCCESS, sizeof(*number));
    }
    case IOCTL_STORAGE_CHECK_VERIFY:
        return complete(irp, STATUS_SUCCESS, 0);
    case IOCTL_DISK_GET_LENGTH_INFO: {
        ULONGLONG bytes;
        NTSTATUS status = volume_bytes(vol, &bytes);
        if (!NT_SUCCESS(status))
            return complete(irp, status, 0);
        if (length < sizeof(bytes))
            return complete(irp, STATUS_BUFFER_TOO_SMALL, sizeof(bytes));
        *(ULONGLONG *)irp->UserBuffer = bytes;
        return complete(irp, STATUS_SUCCESS, sizeof(bytes));
    }
    case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX: {
        ULONGLONG bytes;
        NTSTATUS status = volume_bytes(vol, &bytes);
        if (!NT_SUCCESS(status))
            return complete(irp, status, 0);
        ULONG need = sizeof(MIMEFS_DISK_GEOMETRY) + sizeof(LONGLONG);
        if (length < need)
            return complete(irp, STATUS_BUFFER_TOO_SMALL, need);
        MIMEFS_DISK_GEOMETRY *geometry = (MIMEFS_DISK_GEOMETRY *)irp->UserBuffer;
        memset(geometry, 0, need);
        geometry->MediaType = 12; /* fixed */
        geometry->TracksPerCylinder = 255;
        geometry->SectorsPerTrack = 63;
        geometry->BytesPerSector = 512;
        ULONGLONG sectors = bytes / 512;
        geometry->Cylinders = (LONGLONG)(sectors / (255ull * 63ull));
        *(ULONGLONG *)((PUCHAR)geometry + sizeof(*geometry)) = bytes;
        return complete(irp, STATUS_SUCCESS, need);
    }
    case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS: {
        /* numberOfDiskExtents, then the extents: disk number, offset, length. */
        ULONG need = sizeof(ULONG) + 3 * sizeof(LONGLONG);
        if (length < need)
            return complete(irp, STATUS_BUFFER_TOO_SMALL, need);
        ULONG *count = (ULONG *)irp->UserBuffer;
        LONGLONG *extent = (LONGLONG *)(count + 1);
        *count = 1;
        extent[0] = (LONGLONG)vol->HostVolume;
        extent[1] = 0;
        NTSTATUS status = volume_bytes(vol, (ULONGLONG *)&extent[2]);
        if (!NT_SUCCESS(status))
            return complete(irp, status, 0);
        return complete(irp, STATUS_SUCCESS, need);
    }
    default:
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

/*
 * the statistics of the file system, as ntfs gives them: one entry for each processor, each the common counters then
 * the counters of the file system, and padded to a multiple of 64 bytes. a buffer that cannot hold the first entry's
 * header is too small, and one that cannot hold every entry gets what fits with STATUS_BUFFER_OVERFLOW. the counters
 * of the user's files are the ones the volume keeps, and the ones of the metadata and of the file system itself stay
 * zero, as the volume has no such structures of its own.
 */
#define FSCTL_FILESYSTEM_GET_STATISTICS 0x00090060u
#define FSCTL_FILESYSTEM_GET_STATISTICS_EX 0x00090278u
#define FILESYSTEM_STATISTICS_TYPE_NTFS 1

typedef struct MIMEFS_STATISTICS {
    USHORT FileSystemType;
    USHORT Version;
    ULONG SizeOfCompleteStructure;
    ULONG UserFileReads;
    ULONG UserFileReadBytes;
    ULONG UserDiskReads;
    ULONG UserFileWrites;
    ULONG UserFileWriteBytes;
    ULONG UserDiskWrites;
    ULONG MetaDataReads;
    ULONG MetaDataReadBytes;
    ULONG MetaDataDiskReads;
    ULONG MetaDataWrites;
    ULONG MetaDataWriteBytes;
    ULONG MetaDataDiskWrites;
} MIMEFS_STATISTICS;

typedef struct MIMEFS_STATISTICS_EX {
    USHORT FileSystemType;
    USHORT Version;
    ULONG SizeOfCompleteStructure;
    ULONG64 UserFileReads;
    ULONG64 UserFileReadBytes;
    ULONG64 UserDiskReads;
    ULONG64 UserFileWrites;
    ULONG64 UserFileWriteBytes;
    ULONG64 UserDiskWrites;
    ULONG64 MetaDataReads;
    ULONG64 MetaDataReadBytes;
    ULONG64 MetaDataDiskReads;
    ULONG64 MetaDataWrites;
    ULONG64 MetaDataWriteBytes;
    ULONG64 MetaDataDiskWrites;
} MIMEFS_STATISTICS_EX;

/* the sizes of the counters of ntfs that follow: NTFS_STATISTICS, and NTFS_STATISTICS_EX with its wide counters. */
#define MIMEFS_NTFS_STATISTICS_SIZE 212
#define MIMEFS_NTFS_STATISTICS_EX_SIZE 408

static NTSTATUS fs_statistics(PIRP irp, MIMEFS_VOLUME *vol, ULONG length, BOOLEAN wide)
{
    ULONG header = wide ? sizeof(MIMEFS_STATISTICS_EX) : sizeof(MIMEFS_STATISTICS);
    ULONG stride = (header + (wide ? MIMEFS_NTFS_STATISTICS_EX_SIZE : MIMEFS_NTFS_STATISTICS_SIZE) + 63) & ~63u;
    ULONG count = KeNumberProcessors ? KeNumberProcessors : 1;
    if (count > MIMEFS_MAX_CPUS)
        count = MIMEFS_MAX_CPUS;
    ULONG total = stride * count;
    if (length < header)
        return complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
    PUCHAR out = irp->UserBuffer;
    ULONG written = length < total ? length : total;
    memset(out, 0, written);
    for (ULONG cpu = 0; cpu < count; cpu++) {
        ULONG at = cpu * stride;
        if (at >= written)
            break;
        /* an entry is built whole and copied as far as the buffer goes. */
        UCHAR entry[sizeof(MIMEFS_STATISTICS_EX)] = {};
        const MIMEFS_COUNTERS *c = &vol->Counters[cpu];
        if (wide) {
            MIMEFS_STATISTICS_EX *e = (MIMEFS_STATISTICS_EX *)entry;
            e->FileSystemType = FILESYSTEM_STATISTICS_TYPE_NTFS;
            e->Version = 1;
            e->SizeOfCompleteStructure = stride;
            e->UserFileReads = e->UserDiskReads = c->Reads;
            e->UserFileReadBytes = c->ReadBytes;
            e->UserFileWrites = e->UserDiskWrites = c->Writes;
            e->UserFileWriteBytes = c->WriteBytes;
        } else {
            MIMEFS_STATISTICS *e = (MIMEFS_STATISTICS *)entry;
            e->FileSystemType = FILESYSTEM_STATISTICS_TYPE_NTFS;
            e->Version = 1;
            e->SizeOfCompleteStructure = stride;
            e->UserFileReads = e->UserDiskReads = (ULONG)c->Reads;
            e->UserFileReadBytes = (ULONG)c->ReadBytes;
            e->UserFileWrites = e->UserDiskWrites = (ULONG)c->Writes;
            e->UserFileWriteBytes = (ULONG)c->WriteBytes;
        }
        ULONG n = written - at < header ? written - at : header;
        memcpy(out + at, entry, n);
    }
    return complete(irp, written < total ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS, written);
}

/*
 * what a buffer of reparse data may hold, in the order that Windows tests it: the size, the tag, the lengths and the
 * way a symbolic link or a junction names its target.
 */
static NTSTATUS validate_reparse(const REPARSE_DATA_BUFFER *b, ULONG length)
{
    if (length < REPARSE_DATA_BUFFER_HEADER_SIZE || length > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
        return STATUS_IO_REPARSE_DATA_INVALID;
    ULONG tag = b->ReparseTag;
    if ((tag & 0x0FFF0000u) || tag <= 2 || (tag & 0xC0000000u) == 0x40000000u || tag == IO_REPARSE_TAG_GLOBAL_REPARSE ||
        (tag & 0x30000000u) == 0x30000000u)
        return STATUS_IO_REPARSE_TAG_INVALID;
    ULONG data = b->ReparseDataLength;
    BOOLEAN plain = data + REPARSE_DATA_BUFFER_HEADER_SIZE == length;
    if (!plain && data + REPARSE_GUID_DATA_BUFFER_HEADER_SIZE != length)
        return STATUS_IO_REPARSE_DATA_INVALID;
    if (!plain) {
        /* a buffer with a GUID: the tag of somebody else, and the GUID is not empty. */
        const UCHAR *guid = (const UCHAR *)b + 8;
        BOOLEAN empty = TRUE;
        for (ULONG i = 0; i < 16; i++)
            empty &= guid[i] == 0;
        if (tag == IO_REPARSE_TAG_MOUNT_POINT || tag == IO_REPARSE_TAG_SYMLINK || ((tag & 0x80000000u) == 0 && empty))
            return STATUS_IO_REPARSE_DATA_INVALID;
        return STATUS_SUCCESS;
    }
    if (!(tag & 0x80000000u))
        return STATUS_IO_REPARSE_DATA_INVALID;
    if (tag == IO_REPARSE_TAG_MOUNT_POINT) {
        if (data < 8)
            return STATUS_IO_REPARSE_DATA_INVALID;
        ULONG sub = b->MountPointReparseBuffer.SubstituteNameLength, print = b->MountPointReparseBuffer.PrintNameLength;
        if (b->MountPointReparseBuffer.SubstituteNameOffset != 0 || b->MountPointReparseBuffer.PrintNameOffset != sub + 2)
            return STATUS_IO_REPARSE_DATA_INVALID;
        return data == sub + print + 12 ? STATUS_SUCCESS : STATUS_IO_REPARSE_DATA_INVALID;
    }
    if (tag == IO_REPARSE_TAG_SYMLINK) {
        if (data < 12)
            return STATUS_IO_REPARSE_DATA_INVALID;
        ULONG so = b->SymbolicLinkReparseBuffer.SubstituteNameOffset, sl = b->SymbolicLinkReparseBuffer.SubstituteNameLength;
        ULONG po = b->SymbolicLinkReparseBuffer.PrintNameOffset, pl = b->SymbolicLinkReparseBuffer.PrintNameLength;
        if (so + sl > 0xFFFF || po + pl > 0xFFFF)
            return STATUS_INTEGER_OVERFLOW;
        if (length < po + pl + 20 || length < so + sl + 20 || !sl || !pl || (sl & 1) || (pl & 1) || (so & 1) || (po & 1))
            return STATUS_IO_REPARSE_DATA_INVALID;
    }
    return STATUS_SUCCESS;
}

static void reparse_changed(PFILE_OBJECT file, MIMEFS_FCB *fcb)
{
    MimeFsInfo *now = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*now), KI_TAG('M', 'f', 'I', 'n'));
    FsCacheInvalidate(fcb->Volume, fcb->FileId);
    if (now) {
        if (NT_SUCCESS(query(fcb, now))) {
            fcb->FileId = now->file_id;
            fcb->OpenInfo = *now;
        }
        ExFreePool(now);
    }
    FsAttrChanged(fcb->Volume, file->FileName.Buffer, file->FileName.Length / sizeof(WCHAR), TRUE);
    FsNotifyReport(fcb->Volume, &file->FileName, FILE_ACTION_MODIFIED, FILE_NOTIFY_CHANGE_ATTRIBUTES);
}

static NTSTATUS reparse_host_status(ULONG64 hs)
{
    switch (hs) {
    case MIME_HC_STATUS_NOT_DIRECTORY: return STATUS_NOT_A_DIRECTORY;
    case MIME_HC_STATUS_NOT_EMPTY: return STATUS_DIRECTORY_NOT_EMPTY;
    case MIME_HC_STATUS_INVALID: return STATUS_IO_REPARSE_DATA_INVALID;
    case MIME_HC_STATUS_NOT_REPARSE: return STATUS_NOT_A_REPARSE_POINT;
    case MIME_HC_STATUS_TAG_MISMATCH: return STATUS_IO_REPARSE_TAG_MISMATCH;
    case MIME_HC_STATUS_UNSUPPORTED: return STATUS_NOT_SUPPORTED;
    default: return host_status(hs, FALSE);
    }
}

/* FSCTL_SET_REPARSE_POINT: the empty file or directory becomes a symbolic link or a junction. */
static NTSTATUS reparse_set(PIRP irp, PFILE_OBJECT file, MIMEFS_FCB *fcb, const REPARSE_DATA_BUFFER *b, ULONG length)
{
    if (KeGetPreviousMode() == UserMode && !(fcb->Access & (FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES)))
        return STATUS_ACCESS_DENIED;
    NTSTATUS status = validate_reparse(b, length);
    if (!NT_SUCCESS(status))
        return status;
    ULONG tag = b->ReparseTag;
    if (tag != IO_REPARSE_TAG_SYMLINK && tag != IO_REPARSE_TAG_MOUNT_POINT)
        return STATUS_NOT_SUPPORTED;
    if (tag == IO_REPARSE_TAG_SYMLINK && KeGetPreviousMode() == UserMode && !SeThreadHasPrivilege(35) && !developer_mode())
        return STATUS_PRIVILEGE_NOT_HELD;
    if (!fcb->HostHandle)
        return STATUS_INVALID_DEVICE_REQUEST;
    MimeFsReparse *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), KI_TAG('M', 'f', 'R', 'p'));
    if (!r)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(r, 0, sizeof(*r));
    r->tag = tag;
    r->directory = fcb->Directory;
    const UCHAR *base = (const UCHAR *)b;
    ULONG so, sl, po, pl, path;
    if (tag == IO_REPARSE_TAG_MOUNT_POINT) {
        so = b->MountPointReparseBuffer.SubstituteNameOffset;
        sl = b->MountPointReparseBuffer.SubstituteNameLength;
        po = b->MountPointReparseBuffer.PrintNameOffset;
        pl = b->MountPointReparseBuffer.PrintNameLength;
        path = 16;
    } else {
        so = b->SymbolicLinkReparseBuffer.SubstituteNameOffset;
        sl = b->SymbolicLinkReparseBuffer.SubstituteNameLength;
        po = b->SymbolicLinkReparseBuffer.PrintNameOffset;
        pl = b->SymbolicLinkReparseBuffer.PrintNameLength;
        r->flags = b->SymbolicLinkReparseBuffer.Flags & SYMLINK_FLAG_RELATIVE;
        path = 20;
    }
    if (path + so + sl > length || path + po + pl > length || (sl + pl) / sizeof(WCHAR) > MIME_REPARSE_MAX_NAME) {
        ExFreePool(r);
        return STATUS_IO_REPARSE_DATA_INVALID;
    }
    memcpy(r->names, base + path + so, sl);
    memcpy(r->names + sl / sizeof(WCHAR), base + path + po, pl);
    r->substitute_chars = sl / sizeof(WCHAR);
    r->print_chars = pl / sizeof(WCHAR);
    if (fcb->Directory)
        FsWbSyncPath(fcb->Volume, &file->FileName, TRUE);
    else
        FsWbSync(&fcb->Wb);
    ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_SET_REPARSE, fcb->HostHandle, HalPhysicalOf(r), 0, 0, 0, 0, nullptr);
    ExFreePool(r);
    status = reparse_host_status(hs);
    if (NT_SUCCESS(status)) {
        fcb->Link = TRUE;
        reparse_changed(file, fcb);
    }
    return status;
}

/* FSCTL_GET_REPARSE_POINT: the data of the link, built the way the one that made it would have. */
static NTSTATUS reparse_get(PIRP irp, MIMEFS_FCB *fcb, ULONG out_length, PULONG written)
{
    if (!fcb->Link || !fcb->HostHandle)
        return STATUS_NOT_A_REPARSE_POINT;
    MimeFsReparse *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), KI_TAG('M', 'f', 'R', 'p'));
    if (!r)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_GET_REPARSE, fcb->HostHandle, HalPhysicalOf(r), 0, 0, 0, 0, nullptr);
    NTSTATUS status = reparse_host_status(hs);
    if (!NT_SUCCESS(status)) {
        ExFreePool(r);
        return status;
    }
    ULONG sl = r->substitute_chars * sizeof(WCHAR), pl = r->print_chars * sizeof(WCHAR);
    BOOLEAN junction = r->tag == IO_REPARSE_TAG_MOUNT_POINT;
    /* a symbolic link keeps the print name first, as CreateSymbolicLink writes it, a junction the substitute name. */
    ULONG header = junction ? 16 : 20, total = header + sl + pl + (junction ? 4 : 0);
    PUCHAR whole = ExAllocatePool2(POOL_FLAG_NON_PAGED, total, KI_TAG('M', 'f', 'R', 'q'));
    if (!whole) {
        ExFreePool(r);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memset(whole, 0, total);
    REPARSE_DATA_BUFFER *b = (REPARSE_DATA_BUFFER *)whole;
    const WCHAR *sub = r->names, *print = r->names + r->substitute_chars;
    b->ReparseTag = r->tag;
    b->ReparseDataLength = (USHORT)(total - REPARSE_DATA_BUFFER_HEADER_SIZE);
    if (junction) {
        b->MountPointReparseBuffer.SubstituteNameOffset = 0;
        b->MountPointReparseBuffer.SubstituteNameLength = (USHORT)sl;
        b->MountPointReparseBuffer.PrintNameOffset = (USHORT)(sl + 2);
        b->MountPointReparseBuffer.PrintNameLength = (USHORT)pl;
        memcpy(whole + header, sub, sl);
        memcpy(whole + header + sl + 2, print, pl);
    } else {
        b->SymbolicLinkReparseBuffer.SubstituteNameOffset = (USHORT)pl;
        b->SymbolicLinkReparseBuffer.SubstituteNameLength = (USHORT)sl;
        b->SymbolicLinkReparseBuffer.PrintNameOffset = 0;
        b->SymbolicLinkReparseBuffer.PrintNameLength = (USHORT)pl;
        b->SymbolicLinkReparseBuffer.Flags = r->flags;
        memcpy(whole + header, print, pl);
        memcpy(whole + header + pl, sub, sl);
    }
    ExFreePool(r);
    if (out_length < REPARSE_DATA_BUFFER_HEADER_SIZE) {
        ExFreePool(whole);
        return STATUS_BUFFER_TOO_SMALL;
    }
    ULONG copy = out_length < total ? out_length : total;
    memcpy(irp->UserBuffer, whole, copy);
    ExFreePool(whole);
    *written = copy;
    return copy < total ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
}

/* FSCTL_DELETE_REPARSE_POINT: the empty file or directory that the link was made from is left. */
static NTSTATUS reparse_delete(PFILE_OBJECT file, MIMEFS_FCB *fcb, const REPARSE_DATA_BUFFER *b, ULONG length)
{
    if (KeGetPreviousMode() == UserMode && !(fcb->Access & (FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES)))
        return STATUS_ACCESS_DENIED;
    if (length < REPARSE_DATA_BUFFER_HEADER_SIZE)
        return STATUS_IO_REPARSE_DATA_INVALID;
    ULONG tag = b->ReparseTag;
    if (!(tag & 0x80000000u) && length < REPARSE_GUID_DATA_BUFFER_HEADER_SIZE)
        return STATUS_IO_REPARSE_DATA_INVALID;
    if (b->ReparseDataLength != 0)
        return STATUS_IO_REPARSE_DATA_INVALID;
    if (!fcb->Link || !fcb->HostHandle)
        return STATUS_NOT_A_REPARSE_POINT;
    ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_DELETE_REPARSE, fcb->HostHandle, tag, 0, 0, 0, 0, nullptr);
    NTSTATUS status = reparse_host_status(hs);
    if (NT_SUCCESS(status)) {
        fcb->Link = FALSE;
        reparse_changed(file, fcb);
    }
    return status;
}

static NTSTATUS NTAPI mimefs_fs_control(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    MIMEFS_VOLUME *vol = device->DeviceExtension;
    ULONG length = stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    if (code == FSCTL_SET_REPARSE_POINT || code == FSCTL_GET_REPARSE_POINT || code == FSCTL_DELETE_REPARSE_POINT) {
        PFILE_OBJECT file = stack->FileObject;
        MIMEFS_FCB *fcb = file->FsContext;
        if (!fcb)
            return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
        if (fcb->Rom || fcb->StatOnly)
            return complete(irp, code == FSCTL_GET_REPARSE_POINT ? STATUS_NOT_A_REPARSE_POINT : STATUS_ACCESS_DENIED, 0);
        ULONG in_length = stack->Parameters.DeviceIoControl.InputBufferLength, written = 0;
        NTSTATUS status;
        if (code == FSCTL_SET_REPARSE_POINT)
            status = reparse_set(irp, file, fcb, irp->AssociatedIrp.SystemBuffer, in_length);
        else if (code == FSCTL_DELETE_REPARSE_POINT)
            status = reparse_delete(file, fcb, irp->AssociatedIrp.SystemBuffer, in_length);
        else
            status = reparse_get(irp, fcb, length, &written);
        return complete(irp, status, written);
    }
    switch (code) {
    case FSCTL_FILESYSTEM_GET_STATISTICS:
        return fs_statistics(irp, vol, length, FALSE);
    case FSCTL_FILESYSTEM_GET_STATISTICS_EX:
        return fs_statistics(irp, vol, length, TRUE);
    default:
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
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
    NTSTATUS kept = FsWbSync(&fcb->Wb);
    if (!NT_SUCCESS(kept))
        return complete(irp, kept, 0);
    /* the host writes the file out, which takes milliseconds, so the kernel lock is not held meanwhile. */
    ULONG64 hs = HalHypercallUnlocked(MIME_HC_FS_FLUSH, fcb->HostHandle, 0, 0, 0, 0, 0, nullptr);
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
    FsAttrInitialize();
    FsWbInitialize();
    FsNotifyInitialize();
    FsLocksInitialize();
    driver->MajorFunction[IRP_MJ_CREATE] = mimefs_create;
    driver->MajorFunction[IRP_MJ_CLEANUP] = mimefs_cleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = mimefs_close;
    driver->MajorFunction[IRP_MJ_READ] = mimefs_read_write;
    driver->MajorFunction[IRP_MJ_WRITE] = mimefs_read_write;
    driver->MajorFunction[IRP_MJ_QUERY_INFORMATION] = mimefs_query_info;
    driver->MajorFunction[IRP_MJ_SET_INFORMATION] = mimefs_set_info;
    driver->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] = mimefs_directory;
    driver->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = mimefs_query_volume;
    driver->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION] = mimefs_set_volume;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = mimefs_device_control;
    driver->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = mimefs_fs_control;
    driver->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = mimefs_flush;
    driver->MajorFunction[IRP_MJ_QUERY_SECURITY] = mimefs_query_security;
    driver->MajorFunction[IRP_MJ_SET_SECURITY] = mimefs_set_security;
    /* C: has no label, like a fresh install; Z: shows the name of the host volume. */
    ULONG serial = HalBootInfo->volume_serial;
    NTSTATUS status = create_volume(driver, L"\\Device\\HarddiskVolume1", MIME_FS_VOLUME_SYSTEM, "", serial);
    if (NT_SUCCESS(status))
        status = create_volume(driver, L"\\Device\\HarddiskVolume2", MIME_FS_VOLUME_HOST, HalBootInfo->host_volume_label,
                               serial * 2654435761u | 1);
    return status;
}
