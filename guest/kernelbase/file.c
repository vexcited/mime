/* files: creation, synchronous i/o, information, directories, copying, moving and enumeration. */
#include "kernelbase_private.h"

#define FILE_NO_INTERMEDIATE_BUFFERING 0x00000008
#define STATUS_NOT_SAME_DEVICE ((NTSTATUS)0xC00000D4L)
#define FILE_RANDOM_ACCESS 0x00000800
#define FILE_ATTRIBUTE_VALID_FLAGS 0x0007FFB7
#define FILE_DEVICE_CD_ROM 0x00000002
#define FILE_DEVICE_CD_ROM_FILE_SYSTEM 0x00000003
#define FILE_DEVICE_DISK 0x00000007
#define FILE_DEVICE_DISK_FILE_SYSTEM 0x00000008
#define FILE_DEVICE_NAMED_PIPE 0x00000011
#define FILE_DEVICE_NETWORK_FILE_SYSTEM 0x00000014
#define FILE_DEVICE_VIRTUAL_DISK 0x00000024
#define FILE_DEVICE_DFS 0x00000006
#define FILE_DEVICE_CONTROLLER 0x00000004
#define FILE_DEVICE_MASS_STORAGE 0x0000002D

typedef struct KB_FILE_RENAME_INFORMATION {
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
} KB_FILE_RENAME_INFORMATION;

typedef struct KB_FILE_FS_VOLUME_INFORMATION {
    LARGE_INTEGER VolumeCreationTime;
    ULONG VolumeSerialNumber;
    ULONG VolumeLabelLength;
    BOOLEAN SupportsObjects;
    WCHAR VolumeLabel[1];
} KB_FILE_FS_VOLUME_INFORMATION;

typedef struct KB_FILE_FS_SIZE_INFORMATION {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER AvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
} KB_FILE_FS_SIZE_INFORMATION;

/* ------------------------------------------------------------------ helpers */

static LONGLONG filetime_value(const FILETIME *ft)
{
    return (LONGLONG)(((ULONGLONG)ft->dwHighDateTime << 32) | ft->dwLowDateTime);
}

static void set_filetime(FILETIME *ft, LONGLONG v)
{
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)((ULONGLONG)v >> 32);
}

/*
 * whether the file is one a feature on demand holds and the feature arrived, so that the program looks again. Only a
 * program that reads the file asks for it, a look at the name does not.
 */
static BOOL feature_arrived(const UNICODE_STRING *nt, NTSTATUS status)
{
    if (status != STATUS_OBJECT_NAME_NOT_FOUND && status != STATUS_OBJECT_PATH_NOT_FOUND)
        return FALSE;
    return MimeFeatureFault(nt->Buffer);
}

/* whether the open reads the contents of a file or runs it, and does not open a directory. */
static BOOL reads_file(ACCESS_MASK access, ULONG options)
{
    return (options & FILE_NON_DIRECTORY_FILE) &&
           (access & (GENERIC_READ | GENERIC_EXECUTE | GENERIC_ALL | FILE_READ_DATA | FILE_EXECUTE));
}

static NTSTATUS open_path(LPCWSTR path, ACCESS_MASK access, ULONG share, ULONG options, PHANDLE handle)
{
    UNICODE_STRING nt;
    if (!RtlDosPathNameToNtPathName_U(path, &nt, nullptr, nullptr))
        return STATUS_OBJECT_PATH_NOT_FOUND;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtOpenFile(handle, access | SYNCHRONIZE, &oa, &iosb, share, options | FILE_SYNCHRONOUS_IO_NONALERT);
    RtlFreeUnicodeString(&nt);
    return status;
}

/* a missing file reports its parent directory when that is what is missing. */
static BOOL set_path_status(NTSTATUS status)
{
    return KbSetStatus(status);
}

/* ------------------------------------------------------------------ creation */

static HANDLE open_device_name(LPCWSTR name, DWORD access)
{
    PCWSTR base = name;
    if (!wcsncmp(base, L"\\\\.\\", 4))
        base += 4;
    if (!_wcsicmp(base, L"NUL"))
        return KB_NULL_DEVICE;
    return nullptr;
}

HANDLE WINAPI CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                          DWORD flags, HANDLE template_file)
{
    if (!name || !*name) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE device = open_device_name(name, access);
    if (device) {
        KbSetError(ERROR_SUCCESS);
        return device;
    }
    device = KbOpenConsoleDevice(name, access, share, sa, disposition);
    if (device == INVALID_HANDLE_VALUE)
        return device;
    if (device) {
        KbSetError(ERROR_SUCCESS);
        return device;
    }
    ULONG nt_disposition;
    switch (disposition) {
    case CREATE_NEW:
        nt_disposition = FILE_CREATE;
        break;
    case CREATE_ALWAYS:
        nt_disposition = FILE_OVERWRITE_IF;
        break;
    case OPEN_EXISTING:
        nt_disposition = FILE_OPEN;
        break;
    case OPEN_ALWAYS:
        nt_disposition = FILE_OPEN_IF;
        break;
    case TRUNCATE_EXISTING:
        nt_disposition = FILE_OVERWRITE;
        break;
    default:
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    ULONG options = 0;
    if (!(flags & FILE_FLAG_OVERLAPPED))
        options |= FILE_SYNCHRONOUS_IO_NONALERT;
    if (flags & FILE_FLAG_WRITE_THROUGH)
        options |= FILE_WRITE_THROUGH;
    if (flags & FILE_FLAG_NO_BUFFERING)
        options |= FILE_NO_INTERMEDIATE_BUFFERING;
    if (flags & FILE_FLAG_RANDOM_ACCESS)
        options |= FILE_RANDOM_ACCESS;
    if (flags & FILE_FLAG_SEQUENTIAL_SCAN)
        options |= FILE_SEQUENTIAL_ONLY;
    if (flags & FILE_FLAG_DELETE_ON_CLOSE) {
        options |= FILE_DELETE_ON_CLOSE;
        access |= DELETE;
    }
    if (flags & FILE_FLAG_BACKUP_SEMANTICS)
        options |= FILE_OPEN_FOR_BACKUP_INTENT;
    else
        options |= FILE_NON_DIRECTORY_FILE;
    if (flags & FILE_FLAG_OPEN_REPARSE_POINT)
        options |= FILE_OPEN_REPARSE_POINT;

    UNICODE_STRING nt;
    if (!KbDosPathToNt(name, &nt))
        return INVALID_HANDLE_VALUE;
    OBJECT_ATTRIBUTES oa;
    ULONG attributes = OBJ_CASE_INSENSITIVE;
    if (sa && sa->bInheritHandle)
        attributes |= OBJ_INHERIT;
    InitializeObjectAttributes(&oa, &nt, attributes, nullptr, sa ? sa->lpSecurityDescriptor : nullptr);
    /* how far a named pipe server may impersonate this client, when the program asks. */
    struct {
        ULONG Length, ImpersonationLevel;
        UCHAR ContextTrackingMode, EffectiveOnly;
    } qos = {12, (flags >> 16) & 3, (flags & 0x00040000) != 0, (flags & 0x00080000) != 0};
    if (flags & 0x00100000) /* SECURITY_SQOS_PRESENT */
        oa.SecurityQualityOfService = &qos;
    IO_STATUS_BLOCK iosb = {};
    HANDLE handle;
    NTSTATUS status = NtCreateFile(&handle, access | SYNCHRONIZE | FILE_READ_ATTRIBUTES, &oa, &iosb, nullptr,
                                   flags & FILE_ATTRIBUTE_VALID_FLAGS & ~(DWORD)FILE_ATTRIBUTE_DIRECTORY, share,
                                   nt_disposition, options, nullptr, 0);
    /* a program that reads what is not there may be waiting for a feature on demand. */
    if (!NT_SUCCESS(status) && nt_disposition == FILE_OPEN && reads_file(access, options) &&
        feature_arrived(&nt, status))
        status = NtCreateFile(&handle, access | SYNCHRONIZE | FILE_READ_ATTRIBUTES, &oa, &iosb, nullptr,
                              flags & FILE_ATTRIBUTE_VALID_FLAGS & ~(DWORD)FILE_ATTRIBUTE_DIRECTORY, share,
                              nt_disposition, options, nullptr, 0);
    RtlFreeUnicodeString(&nt);
    if (!NT_SUCCESS(status)) {
        if (status == STATUS_OBJECT_NAME_COLLISION)
            KbSetError(ERROR_FILE_EXISTS);
        else if (status == STATUS_FILE_IS_A_DIRECTORY && disposition != OPEN_EXISTING)
            KbSetError(ERROR_ACCESS_DENIED);
        else
            set_path_status(status);
        return INVALID_HANDLE_VALUE;
    }
    if ((disposition == CREATE_ALWAYS && iosb.Information == FILE_OVERWRITTEN) ||
        (disposition == OPEN_ALWAYS && iosb.Information == FILE_OPENED))
        KbSetError(ERROR_ALREADY_EXISTS);
    else
        KbSetError(ERROR_SUCCESS);
    return handle;
}

HANDLE WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                          DWORD flags, HANDLE template_file)
{
    if (!name) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    PWSTR w = KbAnsiToWide(name);
    if (!w)
        return INVALID_HANDLE_VALUE;
    HANDLE h = CreateFileW(w, access, share, sa, disposition, flags, template_file);
    KbFree(w);
    return h;
}

HANDLE WINAPI CreateFile2(LPCWSTR name, DWORD access, DWORD share, DWORD disposition,
                          LPCREATEFILE2_EXTENDED_PARAMETERS params)
{
    DWORD flags = 0;
    LPSECURITY_ATTRIBUTES sa = nullptr;
    HANDLE template_file = nullptr;
    if (params) {
        if (params->dwSize < sizeof(*params)) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return INVALID_HANDLE_VALUE;
        }
        flags = params->dwFileAttributes | params->dwFileFlags | params->dwSecurityQosFlags;
        sa = params->lpSecurityAttributes;
        template_file = params->hTemplateFile;
    }
    return CreateFileW(name, access, share, sa, disposition, flags, template_file);
}

/* ------------------------------------------------------------------ 16-bit file functions */

HFILE WINAPI _lopen(LPCSTR path, int mode)
{
    static const DWORD access_map[] = {GENERIC_READ, GENERIC_WRITE, GENERIC_READ | GENERIC_WRITE};
    DWORD access = access_map[(mode & 3) < 3 ? (mode & 3) : 0];
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    switch (mode & 0x70) {
    case OF_SHARE_EXCLUSIVE:
        share = 0;
        break;
    case OF_SHARE_DENY_WRITE:
        share = FILE_SHARE_READ;
        break;
    case OF_SHARE_DENY_READ:
        share = FILE_SHARE_WRITE;
        break;
    }
    HANDLE h = CreateFileA(path, access, share, nullptr, OPEN_EXISTING, 0, nullptr);
    return h == INVALID_HANDLE_VALUE ? HFILE_ERROR : (HFILE)(LONG_PTR)h;
}

HFILE WINAPI _lcreat(LPCSTR path, int attributes)
{
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           CREATE_ALWAYS, (DWORD)attributes & 7, nullptr);
    return h == INVALID_HANDLE_VALUE ? HFILE_ERROR : (HFILE)(LONG_PTR)h;
}

HFILE WINAPI _lclose(HFILE file)
{
    return CloseHandle((HANDLE)(LONG_PTR)file) ? 0 : HFILE_ERROR;
}

UINT WINAPI _lread(HFILE file, LPVOID buffer, UINT count)
{
    DWORD read;
    return ReadFile((HANDLE)(LONG_PTR)file, buffer, count, &read, nullptr) ? read : (UINT)HFILE_ERROR;
}

UINT WINAPI _lwrite(HFILE file, LPCCH buffer, UINT count)
{
    DWORD written;
    if (!count)
        return SetEndOfFile((HANDLE)(LONG_PTR)file) ? 0 : (UINT)HFILE_ERROR;
    return WriteFile((HANDLE)(LONG_PTR)file, buffer, count, &written, nullptr) ? written : (UINT)HFILE_ERROR;
}

/*
 * whether the device of a handle is powered. every device of the machine is, so a handle that is a device or a
 * file answers on and any other handle is invalid.
 */
BOOL WINAPI GetDevicePowerState(HANDLE device, BOOL *on)
{
    if (!on || GetFileType(device) == FILE_TYPE_UNKNOWN) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    *on = TRUE;
    return TRUE;
}

/* the same calls with a count that is signed: a count below zero is an error. */
LONG WINAPI _hread(HFILE file, LPVOID buffer, LONG count)
{
    return count < 0 ? HFILE_ERROR : (LONG)_lread(file, buffer, (UINT)count);
}

LONG WINAPI _hwrite(HFILE file, LPCCH buffer, LONG count)
{
    return count < 0 ? HFILE_ERROR : (LONG)_lwrite(file, buffer, (UINT)count);
}

LONG WINAPI _llseek(HFILE file, LONG offset, int origin)
{
    return (LONG)SetFilePointer((HANDLE)(LONG_PTR)file, offset, nullptr, (DWORD)origin);
}

#define FILE_REMOVABLE_MEDIA 0x00000001

static DWORD style_share(UINT style)
{
    switch (style & 0x70) {
    case OF_SHARE_EXCLUSIVE:
        return 0;
    case OF_SHARE_DENY_WRITE:
        return FILE_SHARE_READ;
    case OF_SHARE_DENY_READ:
        return FILE_SHARE_WRITE;
    }
    return FILE_SHARE_READ | FILE_SHARE_WRITE;
}

/*
 * the 16-bit way to open a file. the name is looked up along the search path unless the style reopens the record's
 * file; the record keeps the full path, the last write time for OF_VERIFY and the error. parse, exist and delete styles
 * return 1 on success, the others the handle. OF_PROMPT never asks for the disk, as that hard error is not shown here.
 */
HFILE WINAPI OpenFile(LPCSTR name, LPOFSTRUCT record, UINT style)
{
    OFSTRUCT saved = *record;
    HFILE result = HFILE_ERROR;
    BOOL not_found = FALSE;
    LPSTR part = nullptr;
    SetLastError(0);
    if (style & OF_PARSE) {
        if (GetFullPathNameA(name, sizeof(record->szPathName) - 1, record->szPathName, &part) > sizeof(record->szPathName) - 1) {
            SetLastError(ERROR_INVALID_DATA);
            result = HFILE_ERROR;
        } else {
            record->cBytes = sizeof(OFSTRUCT);
            record->fFixedDisk = 1;
            result = 0;
        }
        goto done;
    }
    DWORD access = (style & OF_WRITE) ? GENERIC_WRITE : GENERIC_READ;
    if (style & OF_READWRITE)
        access |= GENERIC_READ | GENERIC_WRITE;
    DWORD share = style_share(style), disposition = OPEN_EXISTING;
    if (style & OF_CREATE) {
        access = GENERIC_READ | GENERIC_WRITE;
        disposition = CREATE_ALWAYS;
    }
    if (!(style & OF_REOPEN)) {
        DWORD n = SearchPathA(nullptr, name, nullptr, sizeof(record->szPathName) - 1, record->szPathName, &part);
        if (n > sizeof(record->szPathName) - 1) {
            SetLastError(ERROR_INVALID_DATA);
            goto done;
        }
        if (!n) {
            /* not on the path: use the current directory, the file may be about to be created. */
            not_found = TRUE;
            n = GetFullPathNameA(name, sizeof(record->szPathName) - 1, record->szPathName, &part);
            if (!n || n > sizeof(record->szPathName) - 1) {
                SetLastError(ERROR_INVALID_DATA);
                goto done;
            }
        }
    }
    if ((style & OF_EXIST) && !(style & OF_CREATE)) {
        if (not_found) {
            SetLastError(ERROR_FILE_NOT_FOUND);
            goto done;
        }
        DWORD attributes = GetFileAttributesA(record->szPathName);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            SetLastError(ERROR_FILE_NOT_FOUND);
            goto done;
        }
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
            SetLastError(ERROR_ACCESS_DENIED);
            goto done;
        }
        record->cBytes = sizeof(OFSTRUCT);
        result = 1;
        goto done;
    }
    if (style & OF_DELETE) {
        if (DeleteFileA(record->szPathName)) {
            record->cBytes = sizeof(OFSTRUCT);
            result = 1;
        }
        goto done;
    }
    HANDLE file = CreateFileA(record->szPathName, access, share, nullptr, disposition, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        goto done;
    if (style & OF_EXIST) {
        CloseHandle(file);
        record->cBytes = sizeof(OFSTRUCT);
        result = 1;
        goto done;
    }
    IO_STATUS_BLOCK iosb;
    FILE_FS_DEVICE_INFORMATION device;
    NTSTATUS status = NtQueryVolumeInformationFile(file, &iosb, &device, sizeof(device), FileFsDeviceInformation);
    if (!NT_SUCCESS(status)) {
        CloseHandle(file);
        KbSetStatus(status);
        goto done;
    }
    record->fFixedDisk = (device.DeviceType == FILE_DEVICE_DISK || device.DeviceType == FILE_DEVICE_DISK_FILE_SYSTEM) &&
                         !(device.Characteristics & FILE_REMOVABLE_MEDIA);
    FILETIME written;
    if (!GetFileTime(file, nullptr, nullptr, &written) ||
        !FileTimeToDosDateTime(&written, &record->Reserved1, &record->Reserved2))
        record->Reserved1 = record->Reserved2 = 0;
    record->cBytes = sizeof(OFSTRUCT);
    if ((style & OF_VERIFY) && (saved.Reserved1 != record->Reserved1 || saved.Reserved2 != record->Reserved2 ||
                                strcmp(saved.szPathName, record->szPathName) != 0)) {
        *record = saved;
        CloseHandle(file);
        goto done;
    }
    result = (HFILE)(LONG_PTR)file;
done:
    record->nErrCode = (WORD)GetLastError();
    return result;
}

/* ------------------------------------------------------------------ reading and writing */

static HANDLE overlapped_event(LPOVERLAPPED ov)
{
    /* the low bit of hEvent only suppresses completion port notification. */
    return ov ? (HANDLE)((ULONG_PTR)ov->hEvent & ~(ULONG_PTR)1) : nullptr;
}

/* the low bit of hEvent suppresses the completion packet. */
static PVOID overlapped_port_context(LPOVERLAPPED ov)
{
    return ((ULONG_PTR)ov->hEvent & 1) ? nullptr : ov;
}

BOOL WINAPI ReadFile(HANDLE file, LPVOID buffer, DWORD length, LPDWORD read, LPOVERLAPPED overlapped)
{
    if (read)
        *read = 0;
    if (KbIsPseudoHandle(file))
        return TRUE;
    IO_STATUS_BLOCK local;
    NTSTATUS status;
    if (overlapped) {
        LARGE_INTEGER offset;
        offset.LowPart = overlapped->Offset;
        offset.HighPart = (LONG)overlapped->OffsetHigh;
        PIO_STATUS_BLOCK iosb = (PIO_STATUS_BLOCK)overlapped;
        iosb->Status = STATUS_PENDING;
        iosb->Information = 0;
        status = NtReadFile(file, overlapped_event(overlapped), nullptr, overlapped_port_context(overlapped), iosb, buffer,
                            length, &offset, nullptr);
        if (status == STATUS_PENDING) {
            KbSetError(ERROR_IO_PENDING);
            return FALSE;
        }
        if (read)
            *read = (DWORD)iosb->Information;
        if (!NT_SUCCESS(status)) {
            iosb->Status = status;
            return KbSetStatus(status);
        }
        return TRUE;
    }
    status = NtReadFile(file, nullptr, nullptr, nullptr, &local, buffer, length, nullptr, nullptr);
    if (status == STATUS_PENDING) {
        NtWaitForSingleObject(file, FALSE, nullptr);
        status = local.Status;
    }
    if (status == STATUS_END_OF_FILE)
        return TRUE;
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    if (read)
        *read = (DWORD)local.Information;
    return TRUE;
}

BOOL WINAPI WriteFile(HANDLE file, LPCVOID buffer, DWORD length, LPDWORD written, LPOVERLAPPED overlapped)
{
    if (written)
        *written = 0;
    if (KbIsPseudoHandle(file)) {
        if (written)
            *written = length;
        return TRUE;
    }
    IO_STATUS_BLOCK local;
    NTSTATUS status;
    if (overlapped) {
        LARGE_INTEGER offset;
        offset.LowPart = overlapped->Offset;
        offset.HighPart = (LONG)overlapped->OffsetHigh;
        PIO_STATUS_BLOCK iosb = (PIO_STATUS_BLOCK)overlapped;
        iosb->Status = STATUS_PENDING;
        iosb->Information = 0;
        status = NtWriteFile(file, overlapped_event(overlapped), nullptr, overlapped_port_context(overlapped), iosb,
                             (PVOID)buffer, length, &offset, nullptr);
        if (status == STATUS_PENDING) {
            KbSetError(ERROR_IO_PENDING);
            return FALSE;
        }
        if (written)
            *written = (DWORD)iosb->Information;
        if (!NT_SUCCESS(status)) {
            iosb->Status = status;
            return KbSetStatus(status);
        }
        return TRUE;
    }
    status = NtWriteFile(file, nullptr, nullptr, nullptr, &local, (PVOID)buffer, length, nullptr, nullptr);
    if (status == STATUS_PENDING) {
        NtWaitForSingleObject(file, FALSE, nullptr);
        status = local.Status;
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    if (written)
        *written = (DWORD)local.Information;
    return TRUE;
}

/* the pages of a scatter or gather request, which always finishes through the overlapped structure. */
static BOOL scatter_gather(BOOL write, HANDLE file, PFILE_SEGMENT_ELEMENT segments, DWORD bytes, LPDWORD reserved, LPOVERLAPPED overlapped)
{
    if (!overlapped || reserved) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LARGE_INTEGER offset;
    offset.LowPart = overlapped->Offset;
    offset.HighPart = (LONG)overlapped->OffsetHigh;
    PIO_STATUS_BLOCK iosb = (PIO_STATUS_BLOCK)overlapped;
    iosb->Status = STATUS_PENDING;
    iosb->Information = 0;
    NTSTATUS status = write ? NtWriteFileGather(file, overlapped_event(overlapped), nullptr, overlapped_port_context(overlapped),
                                                iosb, segments, bytes, &offset, nullptr) :
                              NtReadFileScatter(file, overlapped_event(overlapped), nullptr, overlapped_port_context(overlapped),
                                                iosb, segments, bytes, &offset, nullptr);
    if (status == STATUS_PENDING) {
        KbSetError(ERROR_IO_PENDING);
        return FALSE;
    }
    if (!NT_SUCCESS(status)) {
        iosb->Status = status;
        return KbSetStatus(status);
    }
    return TRUE;
}

WINBASEAPI BOOL WINAPI ReadFileScatter(HANDLE file, PFILE_SEGMENT_ELEMENT segments, DWORD bytes, LPDWORD reserved, LPOVERLAPPED overlapped)
{
    return scatter_gather(FALSE, file, segments, bytes, reserved, overlapped);
}

WINBASEAPI BOOL WINAPI WriteFileGather(HANDLE file, PFILE_SEGMENT_ELEMENT segments, DWORD bytes, LPDWORD reserved, LPOVERLAPPED overlapped)
{
    return scatter_gather(TRUE, file, segments, bytes, reserved, overlapped);
}

/* the kernel queues this as a user apc on completion, so the routine runs at an alertable wait. */
static void NTAPI completion_apc(PVOID routine, PIO_STATUS_BLOCK iosb, ULONG reserved)
{
    KbNoteApc();
    NTSTATUS status = iosb->Status;
    DWORD error = NT_SUCCESS(status) ? ERROR_SUCCESS : RtlNtStatusToDosError(status);
    ((LPOVERLAPPED_COMPLETION_ROUTINE)routine)(error, (DWORD)iosb->Information, (LPOVERLAPPED)iosb);
}

static BOOL io_ex(BOOL write, HANDLE file, PVOID buffer, DWORD length, LPOVERLAPPED overlapped,
                  LPOVERLAPPED_COMPLETION_ROUTINE completion)
{
    if (!overlapped || !completion) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LARGE_INTEGER offset;
    offset.LowPart = overlapped->Offset;
    offset.HighPart = (LONG)overlapped->OffsetHigh;
    PIO_STATUS_BLOCK iosb = (PIO_STATUS_BLOCK)overlapped;
    iosb->Status = STATUS_PENDING;
    iosb->Information = 0;
    NTSTATUS status = write ? NtWriteFile(file, nullptr, (PIO_APC_ROUTINE)completion_apc, (PVOID)completion, iosb, buffer,
                                          length, &offset, nullptr)
                            : NtReadFile(file, nullptr, (PIO_APC_ROUTINE)completion_apc, (PVOID)completion, iosb, buffer,
                                         length, &offset, nullptr);
    if (NT_ERROR(status))
        return KbSetStatus(status);
    /* pending or done: the routine runs later either way. */
    KbSetError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI ReadFileEx(HANDLE file, LPVOID buffer, DWORD length, LPOVERLAPPED overlapped,
                       LPOVERLAPPED_COMPLETION_ROUTINE completion)
{
    return io_ex(FALSE, file, buffer, length, overlapped, completion);
}

BOOL WINAPI WriteFileEx(HANDLE file, LPCVOID buffer, DWORD length, LPOVERLAPPED overlapped,
                        LPOVERLAPPED_COMPLETION_ROUTINE completion)
{
    return io_ex(TRUE, file, (PVOID)buffer, length, overlapped, completion);
}

BOOL WINAPI GetOverlappedResultEx(HANDLE file, LPOVERLAPPED overlapped, LPDWORD transferred, DWORD timeout,
                                  BOOL alertable)
{
    if ((NTSTATUS)overlapped->Internal == STATUS_PENDING) {
        if (!timeout) {
            KbSetError(ERROR_IO_INCOMPLETE);
            return FALSE;
        }
        HANDLE wait = overlapped_event(overlapped);
        DWORD r = WaitForSingleObjectEx(wait ? wait : file, timeout, alertable);
        if (r == WAIT_FAILED)
            return FALSE;
        if (r == WAIT_TIMEOUT) {
            KbSetError(WAIT_TIMEOUT);
            return FALSE;
        }
        if (r == WAIT_IO_COMPLETION) {
            KbSetError(WAIT_IO_COMPLETION);
            return FALSE;
        }
    }
    if (transferred)
        *transferred = (DWORD)overlapped->InternalHigh;
    NTSTATUS status = (NTSTATUS)overlapped->Internal;
    if (status == STATUS_END_OF_FILE) {
        KbSetError(ERROR_HANDLE_EOF);
        return FALSE;
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI GetOverlappedResult(HANDLE file, LPOVERLAPPED overlapped, LPDWORD transferred, BOOL wait)
{
    return GetOverlappedResultEx(file, overlapped, transferred, wait ? INFINITE : 0, FALSE);
}

BOOL WINAPI CancelIo(HANDLE file)
{
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtCancelIoFile(file, &iosb);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI CancelIoEx(HANDLE file, LPOVERLAPPED overlapped)
{
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtCancelIoFileEx(file, (PIO_STATUS_BLOCK)overlapped, &iosb);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI CancelSynchronousIo(HANDLE thread)
{
    KbSetError(ERROR_NOT_FOUND);
    return FALSE;
}

/* ------------------------------------------------------------------ size and position */

static BOOL query_standard(HANDLE file, FILE_STANDARD_INFORMATION *info)
{
    if (KbIsPseudoHandle(file)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtQueryInformationFile(file, &iosb, info, sizeof(*info), FileStandardInformation);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI GetFileSizeEx(HANDLE file, PLARGE_INTEGER size)
{
    FILE_STANDARD_INFORMATION info;
    if (!query_standard(file, &info))
        return FALSE;
    *size = info.EndOfFile;
    return TRUE;
}

DWORD WINAPI GetFileSize(HANDLE file, LPDWORD high)
{
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size))
        return INVALID_FILE_SIZE;
    if (high)
        *high = (DWORD)size.HighPart;
    if (size.LowPart == INVALID_FILE_SIZE)
        KbSetError(NO_ERROR);
    return size.LowPart;
}

BOOL WINAPI SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER position, DWORD method)
{
    if (KbIsPseudoHandle(file)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    IO_STATUS_BLOCK iosb;
    NTSTATUS status;
    LONGLONG base = 0;
    switch (method) {
    case FILE_BEGIN:
        break;
    case FILE_CURRENT: {
        FILE_POSITION_INFORMATION pos;
        status = NtQueryInformationFile(file, &iosb, &pos, sizeof(pos), FilePositionInformation);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
        base = pos.CurrentByteOffset.QuadPart;
        break;
    }
    case FILE_END: {
        FILE_STANDARD_INFORMATION std;
        status = NtQueryInformationFile(file, &iosb, &std, sizeof(std), FileStandardInformation);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
        base = std.EndOfFile.QuadPart;
        break;
    }
    default:
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONGLONG target = base + distance.QuadPart;
    if (target < 0) {
        KbSetError(ERROR_NEGATIVE_SEEK);
        return FALSE;
    }
    FILE_POSITION_INFORMATION pos;
    pos.CurrentByteOffset.QuadPart = target;
    status = NtSetInformationFile(file, &iosb, &pos, sizeof(pos), FilePositionInformation);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    if (position)
        position->QuadPart = target;
    return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE file, LONG distance, PLONG high, DWORD method)
{
    LARGE_INTEGER d, result;
    if (high) {
        d.LowPart = (ULONG)distance;
        d.HighPart = *high;
    } else {
        d.QuadPart = distance;
    }
    if (!high && method == FILE_BEGIN && distance < 0) {
        KbSetError(ERROR_NEGATIVE_SEEK);
        return INVALID_SET_FILE_POINTER;
    }
    if (!SetFilePointerEx(file, d, &result, method))
        return INVALID_SET_FILE_POINTER;
    if (high)
        *high = result.HighPart;
    else if (result.HighPart) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_SET_FILE_POINTER;
    }
    if (result.LowPart == INVALID_SET_FILE_POINTER)
        KbSetError(NO_ERROR);
    return result.LowPart;
}

BOOL WINAPI SetEndOfFile(HANDLE file)
{
    if (KbIsPseudoHandle(file)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    IO_STATUS_BLOCK iosb;
    FILE_POSITION_INFORMATION pos;
    NTSTATUS status = NtQueryInformationFile(file, &iosb, &pos, sizeof(pos), FilePositionInformation);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    FILE_END_OF_FILE_INFORMATION eof;
    eof.EndOfFile = pos.CurrentByteOffset;
    status = NtSetInformationFile(file, &iosb, &eof, sizeof(eof), FileEndOfFileInformation);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    NtSetInformationFile(file, &iosb, &eof, sizeof(eof), FileAllocationInformation);
    return TRUE;
}

BOOL WINAPI SetFileValidData(HANDLE file, LONGLONG length)
{
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size))
        return FALSE;
    if (length > size.QuadPart) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI FlushFileBuffers(HANDLE file)
{
    if (KbIsPseudoHandle(file))
        return TRUE;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtFlushBuffersFile(file, &iosb);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

DWORD WINAPI GetFileType(HANDLE file)
{
    if (KbIsPseudoHandle(file))
        return FILE_TYPE_CHAR;
    if (file == INVALID_HANDLE_VALUE || !file) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FILE_TYPE_UNKNOWN;
    }
    FILE_FS_DEVICE_INFORMATION info;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtQueryVolumeInformationFile(file, &iosb, &info, sizeof(info), FileFsDeviceInformation);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status == STATUS_OBJECT_TYPE_MISMATCH ? STATUS_INVALID_HANDLE : status);
        return FILE_TYPE_UNKNOWN;
    }
    KbSetError(NO_ERROR);
    switch (info.DeviceType) {
    case FILE_DEVICE_DISK:
    case FILE_DEVICE_DISK_FILE_SYSTEM:
    case FILE_DEVICE_CD_ROM:
    case FILE_DEVICE_CD_ROM_FILE_SYSTEM:
    case FILE_DEVICE_CONTROLLER:
    case FILE_DEVICE_DFS:
    case FILE_DEVICE_NETWORK_FILE_SYSTEM:
    case FILE_DEVICE_VIRTUAL_DISK:
    case FILE_DEVICE_MASS_STORAGE:
        return FILE_TYPE_DISK;
    case FILE_DEVICE_NAMED_PIPE:
        return FILE_TYPE_PIPE;
    default:
        return FILE_TYPE_CHAR;
    }
}

BOOL WINAPI LockFile(HANDLE file, DWORD offset_low, DWORD offset_high, DWORD length_low, DWORD length_high)
{
    LARGE_INTEGER offset = {.LowPart = offset_low, .HighPart = (LONG)offset_high};
    LARGE_INTEGER length = {.LowPart = length_low, .HighPart = (LONG)length_high};
    IO_STATUS_BLOCK io;
    NTSTATUS status = NtLockFile(file, nullptr, nullptr, nullptr, &io, &offset, &length, 0, TRUE, TRUE);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

BOOL WINAPI LockFileEx(HANDLE file, DWORD flags, DWORD reserved, DWORD length_low, DWORD length_high,
                       LPOVERLAPPED overlapped)
{
    if (reserved || !overlapped || (flags & ~(LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LARGE_INTEGER offset = {.LowPart = overlapped->Offset, .HighPart = (LONG)overlapped->OffsetHigh};
    LARGE_INTEGER length = {.LowPart = length_low, .HighPart = (LONG)length_high};
    overlapped->Internal = STATUS_PENDING;
    overlapped->InternalHigh = 0;
    NTSTATUS status = NtLockFile(file, overlapped_event(overlapped), nullptr, overlapped_port_context(overlapped),
                                 (PIO_STATUS_BLOCK)overlapped, &offset, &length, 0,
                                 (flags & LOCKFILE_FAIL_IMMEDIATELY) != 0, (flags & LOCKFILE_EXCLUSIVE_LOCK) != 0);
    if (status == STATUS_PENDING) {
        KbSetError(ERROR_IO_PENDING);
        return FALSE;
    }
    if (!NT_SUCCESS(status))
        overlapped->Internal = status;
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

BOOL WINAPI UnlockFile(HANDLE file, DWORD offset_low, DWORD offset_high, DWORD length_low, DWORD length_high)
{
    LARGE_INTEGER offset = {.LowPart = offset_low, .HighPart = (LONG)offset_high};
    LARGE_INTEGER length = {.LowPart = length_low, .HighPart = (LONG)length_high};
    IO_STATUS_BLOCK io;
    NTSTATUS status = NtUnlockFile(file, &io, &offset, &length, 0);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

BOOL WINAPI UnlockFileEx(HANDLE file, DWORD reserved, DWORD length_low, DWORD length_high, LPOVERLAPPED overlapped)
{
    if (reserved || !overlapped) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return UnlockFile(file, overlapped->Offset, overlapped->OffsetHigh, length_low, length_high);
}

/* ------------------------------------------------------------------ attributes and information */

DWORD WINAPI GetFileAttributesW(LPCWSTR name)
{
    UNICODE_STRING nt;
    if (!KbDosPathToNt(name, &nt))
        return INVALID_FILE_ATTRIBUTES;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    FILE_NETWORK_OPEN_INFORMATION info;
    NTSTATUS status = NtQueryFullAttributesFile(&oa, &info);
    if (!NT_SUCCESS(status) && (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND) &&
        MimeFeatureFaultFromRuntime(nt.Buffer, __builtin_return_address(0)))
        status = NtQueryFullAttributesFile(&oa, &info);
    RtlFreeUnicodeString(&nt);
    if (!NT_SUCCESS(status)) {
        set_path_status(status);
        return INVALID_FILE_ATTRIBUTES;
    }
    return info.FileAttributes;
}

DWORD WINAPI GetFileAttributesA(LPCSTR name)
{
    PWSTR w = KbAnsiToWide(name);
    if (!w) {
        if (!name)
            KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_FILE_ATTRIBUTES;
    }
    DWORD r = GetFileAttributesW(w);
    KbFree(w);
    return r;
}

BOOL WINAPI GetFileAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
    if (level != GetFileExInfoStandard || !info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UNICODE_STRING nt;
    if (!KbDosPathToNt(name, &nt))
        return FALSE;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    FILE_NETWORK_OPEN_INFORMATION net;
    NTSTATUS status = NtQueryFullAttributesFile(&oa, &net);
    if (!NT_SUCCESS(status) && (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND) &&
        MimeFeatureFaultFromRuntime(nt.Buffer, __builtin_return_address(0)))
        status = NtQueryFullAttributesFile(&oa, &net);
    RtlFreeUnicodeString(&nt);
    if (!NT_SUCCESS(status))
        return set_path_status(status);
    WIN32_FILE_ATTRIBUTE_DATA *data = info;
    data->dwFileAttributes = net.FileAttributes;
    set_filetime(&data->ftCreationTime, net.CreationTime.QuadPart);
    set_filetime(&data->ftLastAccessTime, net.LastAccessTime.QuadPart);
    set_filetime(&data->ftLastWriteTime, net.LastWriteTime.QuadPart);
    data->nFileSizeHigh = (DWORD)net.EndOfFile.HighPart;
    data->nFileSizeLow = net.EndOfFile.LowPart;
    return TRUE;
}

BOOL WINAPI GetFileAttributesExA(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
    PWSTR w = KbAnsiToWide(name);
    if (!w) {
        if (!name)
            KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL r = GetFileAttributesExW(w, level, info);
    KbFree(w);
    return r;
}

BOOL WINAPI SetFileAttributesW(LPCWSTR name, DWORD attributes)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE h;
    NTSTATUS status = open_path(name, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN_FOR_BACKUP_INTENT, &h);
    if (!NT_SUCCESS(status))
        return set_path_status(status);
    FILE_BASIC_INFORMATION info;
    memset(&info, 0, sizeof(info));
    info.FileAttributes = (attributes & FILE_ATTRIBUTE_VALID_FLAGS & ~(DWORD)FILE_ATTRIBUTE_DIRECTORY);
    if (!info.FileAttributes)
        info.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    IO_STATUS_BLOCK iosb;
    status = NtSetInformationFile(h, &iosb, &info, sizeof(info), FileBasicInformation);
    NtClose(h);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI SetFileAttributesA(LPCSTR name, DWORD attributes)
{
    PWSTR w = KbAnsiToWide(name);
    if (!w) {
        if (!name)
            KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL r = SetFileAttributesW(w, attributes);
    KbFree(w);
    return r;
}

static ULONG volume_serial(HANDLE file)
{
    UCHAR buf[sizeof(KB_FILE_FS_VOLUME_INFORMATION) + 64 * sizeof(WCHAR)];
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtQueryVolumeInformationFile(file, &iosb, buf, sizeof(buf), FileFsVolumeInformation);
    if (!NT_SUCCESS(status) && status != STATUS_BUFFER_OVERFLOW)
        return 0;
    return ((KB_FILE_FS_VOLUME_INFORMATION *)buf)->VolumeSerialNumber;
}

BOOL WINAPI GetFileInformationByHandle(HANDLE file, LPBY_HANDLE_FILE_INFORMATION info)
{
    if (KbIsPseudoHandle(file)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    IO_STATUS_BLOCK iosb;
    FILE_BASIC_INFORMATION basic;
    FILE_STANDARD_INFORMATION std;
    FILE_INTERNAL_INFORMATION internal = {};
    NTSTATUS status = NtQueryInformationFile(file, &iosb, &basic, sizeof(basic), FileBasicInformation);
    if (NT_SUCCESS(status))
        status = NtQueryInformationFile(file, &iosb, &std, sizeof(std), FileStandardInformation);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    NtQueryInformationFile(file, &iosb, &internal, sizeof(internal), FileInternalInformation);
    info->dwFileAttributes = basic.FileAttributes;
    set_filetime(&info->ftCreationTime, basic.CreationTime.QuadPart);
    set_filetime(&info->ftLastAccessTime, basic.LastAccessTime.QuadPart);
    set_filetime(&info->ftLastWriteTime, basic.LastWriteTime.QuadPart);
    info->dwVolumeSerialNumber = volume_serial(file);
    info->nFileSizeHigh = (DWORD)std.EndOfFile.HighPart;
    info->nFileSizeLow = std.EndOfFile.LowPart;
    info->nNumberOfLinks = std.NumberOfLinks;
    info->nFileIndexHigh = (DWORD)internal.IndexNumber.HighPart;
    info->nFileIndexLow = internal.IndexNumber.LowPart;
    return TRUE;
}

static BOOL query_directory_by_handle(HANDLE file, FILE_INFORMATION_CLASS klass, LPVOID info, DWORD size,
                                      BOOLEAN restart)
{
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtQueryDirectoryFile(file, nullptr, nullptr, nullptr, &iosb, info, size, klass, FALSE, nullptr, restart);
    if (status == STATUS_NO_MORE_FILES || status == STATUS_NO_SUCH_FILE) {
        KbSetError(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI GetFileInformationByHandleEx(HANDLE file, FILE_INFO_BY_HANDLE_CLASS klass, LPVOID info, DWORD size)
{
    if (KbIsPseudoHandle(file)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    IO_STATUS_BLOCK iosb;
    NTSTATUS status;
    switch (klass) {
    case FileBasicInfo:
        status = NtQueryInformationFile(file, &iosb, info, size, FileBasicInformation);
        break;
    case FileStandardInfo:
        status = NtQueryInformationFile(file, &iosb, info, size, FileStandardInformation);
        break;
    case FileNameInfo:
        status = NtQueryInformationFile(file, &iosb, info, size, FileNameInformation);
        break;
    case FileAttributeTagInfo:
        status = NtQueryInformationFile(file, &iosb, info, size, FileAttributeTagInformation);
        break;
    case FileIdBothDirectoryInfo:
    case FileIdBothDirectoryRestartInfo:
        return query_directory_by_handle(file, FileIdBothDirectoryInformation, info, size,
                                         klass == FileIdBothDirectoryRestartInfo);
    case FileFullDirectoryInfo:
    case FileFullDirectoryRestartInfo:
        return query_directory_by_handle(file, FileFullDirectoryInformation, info, size,
                                         klass == FileFullDirectoryRestartInfo);
    case FileIdInfo: {
        if (size < sizeof(FILE_ID_INFO)) {
            KbSetError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        FILE_INTERNAL_INFORMATION internal;
        status = NtQueryInformationFile(file, &iosb, &internal, sizeof(internal), FileInternalInformation);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
        FILE_ID_INFO *id = info;
        memset(id, 0, sizeof(*id));
        id->VolumeSerialNumber = volume_serial(file);
        memcpy(id->FileId.Identifier, &internal.IndexNumber, sizeof(internal.IndexNumber));
        return TRUE;
    }
    case FileAlignmentInfo:
        if (size < sizeof(FILE_ALIGNMENT_INFO)) {
            KbSetError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        ((FILE_ALIGNMENT_INFO *)info)->AlignmentRequirement = 0;
        return TRUE;
    case FileStorageInfo: {
        if (size < sizeof(FILE_STORAGE_INFO)) {
            KbSetError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        FILE_STORAGE_INFO *s = info;
        memset(s, 0, sizeof(*s));
        s->LogicalBytesPerSector = 512;
        s->PhysicalBytesPerSectorForAtomicity = 4096;
        s->PhysicalBytesPerSectorForPerformance = 4096;
        s->FileSystemEffectivePhysicalBytesPerSectorForAtomicity = 4096;
        return TRUE;
    }
    default:
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (status == STATUS_INFO_LENGTH_MISMATCH) {
        KbSetError(ERROR_BAD_LENGTH);
        return FALSE;
    }
    if (status == STATUS_BUFFER_OVERFLOW) {
        KbSetError(ERROR_MORE_DATA);
        return FALSE;
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

/*
 * opens a file by its id, which the volume handle names the volume of. a file id is 8 bytes, an object id or an extended
 * file id 16. the flags become the options of the open as CreateFileW's do, and the handle can be asked for no more
 * than the file system gives.
 */
HANDLE WINAPI OpenFileById(HANDLE volume, LPFILE_ID_DESCRIPTOR id, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                           DWORD flags)
{
    if (!id || id->dwSize < sizeof(FILE_ID_DESCRIPTOR) || id->Type > ExtendedFileIdType) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    UNICODE_STRING name = {id->Type == FileIdType ? 8 : 16, id->Type == FileIdType ? 8 : 16, (PWSTR)&id->FileId};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, volume, nullptr);
    ULONG options = FILE_OPEN_BY_FILE_ID;
    if (!(flags & FILE_FLAG_OVERLAPPED))
        options |= FILE_SYNCHRONOUS_IO_NONALERT;
    if (flags & FILE_FLAG_WRITE_THROUGH)
        options |= FILE_WRITE_THROUGH;
    if (flags & FILE_FLAG_SEQUENTIAL_SCAN)
        options |= FILE_SEQUENTIAL_ONLY;
    if (flags & FILE_FLAG_RANDOM_ACCESS)
        options |= FILE_RANDOM_ACCESS;
    if (flags & FILE_FLAG_NO_BUFFERING)
        options |= FILE_NO_INTERMEDIATE_BUFFERING;
    if (flags & FILE_FLAG_BACKUP_SEMANTICS)
        options |= FILE_OPEN_FOR_BACKUP_INTENT;
    if (flags & FILE_FLAG_DELETE_ON_CLOSE) {
        options |= FILE_DELETE_ON_CLOSE;
        access |= DELETE;
    }
    if (flags & FILE_FLAG_OPEN_REPARSE_POINT)
        options |= FILE_OPEN_REPARSE_POINT;
    if (flags & FILE_FLAG_OPEN_NO_RECALL)
        options |= 0x00400000;
    HANDLE file;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtCreateFile(&file, access | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, nullptr, 0, share,
                                   FILE_OPEN, options, nullptr, 0);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return INVALID_HANDLE_VALUE;
    }
    return file;
}

BOOL WINAPI SetFileInformationByHandle(HANDLE file, FILE_INFO_BY_HANDLE_CLASS klass, LPVOID info, DWORD size)
{
    if (KbIsPseudoHandle(file)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    FILE_INFORMATION_CLASS nt_class;
    switch (klass) {
    case FileBasicInfo:
        nt_class = FileBasicInformation;
        break;
    case FileRenameInfo:
        nt_class = FileRenameInformation;
        break;
    case FileDispositionInfo:
        nt_class = FileDispositionInformation;
        break;
    case FileAllocationInfo:
        nt_class = FileAllocationInformation;
        break;
    case FileEndOfFileInfo:
        nt_class = FileEndOfFileInformation;
        break;
    case FileDispositionInfoEx:
        nt_class = FileDispositionInformationEx;
        break;
    default:
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    IO_STATUS_BLOCK iosb;
    NTSTATUS status;
    KB_FILE_RENAME_INFORMATION *rename = info;
    if (klass == FileRenameInfo && info && size >= offsetof(KB_FILE_RENAME_INFORMATION, FileName) &&
        !rename->RootDirectory) {
        /* the name is a path of the program that the system call takes as a path of the system, and it ends at its
           terminator when the buffer has one, whatever the length field says. */
        ULONG room = (size - (ULONG)offsetof(KB_FILE_RENAME_INFORMATION, FileName)) / sizeof(WCHAR), chars = 0;
        while (chars < room && rename->FileName[chars])
            chars++;
        if (chars == room)
            chars = rename->FileNameLength / sizeof(WCHAR) < room ? rename->FileNameLength / sizeof(WCHAR) : room;
        ULONG length = chars * sizeof(WCHAR);
        WCHAR *dos = KbAlloc(length + sizeof(WCHAR));
        if (!dos) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        memcpy(dos, rename->FileName, length);
        dos[length / sizeof(WCHAR)] = 0;
        UNICODE_STRING nt;
        BOOL converted = RtlDosPathNameToNtPathName_U(dos, &nt, nullptr, nullptr);
        KbFree(dos);
        if (!converted) {
            KbSetError(ERROR_PATH_NOT_FOUND);
            return FALSE;
        }
        ULONG nt_size = (ULONG)(offsetof(KB_FILE_RENAME_INFORMATION, FileName) + nt.Length);
        KB_FILE_RENAME_INFORMATION *converted_info = KbAlloc(nt_size);
        if (!converted_info) {
            RtlFreeUnicodeString(&nt);
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        converted_info->ReplaceIfExists = rename->ReplaceIfExists;
        converted_info->RootDirectory = nullptr;
        converted_info->FileNameLength = nt.Length;
        memcpy(converted_info->FileName, nt.Buffer, nt.Length);
        status = NtSetInformationFile(file, &iosb, converted_info, nt_size, nt_class);
        KbFree(converted_info);
        RtlFreeUnicodeString(&nt);
    } else {
        status = NtSetInformationFile(file, &iosb, info, size, nt_class);
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write)
{
    if (KbIsPseudoHandle(file)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    IO_STATUS_BLOCK iosb;
    FILE_BASIC_INFORMATION basic;
    NTSTATUS status = NtQueryInformationFile(file, &iosb, &basic, sizeof(basic), FileBasicInformation);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    if (creation)
        set_filetime(creation, basic.CreationTime.QuadPart);
    if (access)
        set_filetime(access, basic.LastAccessTime.QuadPart);
    if (write)
        set_filetime(write, basic.LastWriteTime.QuadPart);
    return TRUE;
}

BOOL WINAPI SetFileTime(HANDLE file, const FILETIME *creation, const FILETIME *access, const FILETIME *write)
{
    if (KbIsPseudoHandle(file)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    FILE_BASIC_INFORMATION basic;
    memset(&basic, 0, sizeof(basic));
    if (creation)
        basic.CreationTime.QuadPart = filetime_value(creation);
    if (access)
        basic.LastAccessTime.QuadPart = filetime_value(access);
    if (write)
        basic.LastWriteTime.QuadPart = filetime_value(write);
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtSetInformationFile(file, &iosb, &basic, sizeof(basic), FileBasicInformation);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

/* ------------------------------------------------------------------ final path names */

/*
 * finds the drive letter of the volume a handle lives on by comparing serial numbers. every letter is tried, since
 * drive lists leave out Z: and a file opened through it must keep its letter.
 */
static WCHAR drive_of_handle(HANDLE file)
{
    ULONG serial = volume_serial(file);
    for (int i = 0; i < 26; i++) {
        WCHAR root[4] = {(WCHAR)('A' + i), ':', '\\', 0};
        HANDLE h;
        if (!NT_SUCCESS(open_path(root, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT, &h)))
            continue;
        ULONG s = volume_serial(h);
        NtClose(h);
        if (s == serial)
            return (WCHAR)('A' + i);
    }
    return 'C';
}

DWORD WINAPI GetFinalPathNameByHandleW(HANDLE file, LPWSTR path, DWORD size, DWORD flags)
{
    if (KbIsPseudoHandle(file)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (flags & ~(DWORD)(FILE_NAME_OPENED | VOLUME_NAME_GUID | VOLUME_NAME_NT | VOLUME_NAME_NONE)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UCHAR buf[sizeof(FILE_NAME_INFORMATION) + MAX_PATH * 4 * sizeof(WCHAR)];
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtQueryInformationFile(file, &iosb, buf, sizeof(buf) - sizeof(WCHAR), FileNameInformation);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return 0;
    }
    FILE_NAME_INFORMATION *fni = (FILE_NAME_INFORMATION *)buf;
    SIZE_T name_chars = fni->FileNameLength / sizeof(WCHAR);
    WCHAR result[MAX_PATH * 4 + 16];
    SIZE_T n = 0;
    DWORD volume = flags & (VOLUME_NAME_GUID | VOLUME_NAME_NT | VOLUME_NAME_NONE);
    if (volume == VOLUME_NAME_DOS) {
        wcscpy(result, L"\\\\?\\C:");
        result[4] = drive_of_handle(file);
        n = 6;
    } else if (volume == VOLUME_NAME_NT) {
        WCHAR drive = drive_of_handle(file);
        n = (SIZE_T)KbFormat(result, 64, L"%s", drive == 'C' ? L"\\Device\\HarddiskVolume1" : L"\\Device\\HarddiskVolume2");
    } else if (volume == VOLUME_NAME_GUID) {
        KbSetError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (name_chars + n >= RTL_NUMBER_OF(result)) {
        KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return 0;
    }
    if (!name_chars || fni->FileName[0] != '\\')
        result[n++] = '\\';
    memcpy(result + n, fni->FileName, name_chars * sizeof(WCHAR));
    n += name_chars;
    result[n] = 0;
    if (n >= size)
        return (DWORD)n + 1;
    memcpy(path, result, (n + 1) * sizeof(WCHAR));
    return (DWORD)n;
}

DWORD WINAPI GetFinalPathNameByHandleA(HANDLE file, LPSTR path, DWORD size, DWORD flags)
{
    WCHAR w[MAX_PATH * 4 + 16];
    DWORD n = GetFinalPathNameByHandleW(file, w, RTL_NUMBER_OF(w), flags);
    if (!n || n >= RTL_NUMBER_OF(w))
        return n;
    return KbWideResultToAnsi(w, path, size);
}

/* ------------------------------------------------------------------ deleting and directories */

BOOL WINAPI DeleteFileW(LPCWSTR name)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE h;
    NTSTATUS status = open_path(name, DELETE | FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT, &h);
    if (!NT_SUCCESS(status)) {
        if (status == STATUS_FILE_IS_A_DIRECTORY)
            KbSetError(ERROR_ACCESS_DENIED);
        else
            set_path_status(status);
        return FALSE;
    }
    FILE_DISPOSITION_INFORMATION disp = {TRUE};
    IO_STATUS_BLOCK iosb;
    status = NtSetInformationFile(h, &iosb, &disp, sizeof(disp), FileDispositionInformation);
    NtClose(h);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI DeleteFileA(LPCSTR name)
{
    PWSTR w = KbAnsiToWide(name);
    if (!w) {
        if (!name)
            KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL r = DeleteFileW(w);
    KbFree(w);
    return r;
}

BOOL WINAPI CreateDirectoryW(LPCWSTR name, LPSECURITY_ATTRIBUTES sa)
{
    UNICODE_STRING nt;
    if (!KbDosPathToNt(name, &nt))
        return FALSE;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, sa ? sa->lpSecurityDescriptor : nullptr);
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    NTSTATUS status = NtCreateFile(&h, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb, nullptr, FILE_ATTRIBUTE_NORMAL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE,
                                   FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT,
                                   nullptr, 0);
    RtlFreeUnicodeString(&nt);
    if (!NT_SUCCESS(status)) {
        if (status == STATUS_OBJECT_NAME_INVALID)
            KbSetError(ERROR_INVALID_NAME);
        else
            set_path_status(status);
        return FALSE;
    }
    NtClose(h);
    return TRUE;
}

BOOL WINAPI CreateDirectoryA(LPCSTR name, LPSECURITY_ATTRIBUTES sa)
{
    PWSTR w = KbAnsiToWide(name);
    if (!w) {
        if (!name)
            KbSetError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    BOOL r = CreateDirectoryW(w, sa);
    KbFree(w);
    return r;
}

BOOL WINAPI CreateDirectoryExW(LPCWSTR template_dir, LPCWSTR name, LPSECURITY_ATTRIBUTES sa)
{
    return CreateDirectoryW(name, sa);
}

/* true when a directory handle has entries besides "." and "..". */
static BOOL directory_has_entries(HANDLE dir)
{
    UCHAR buf[1024];
    IO_STATUS_BLOCK iosb;
    BOOLEAN restart = TRUE;
    for (;;) {
        NTSTATUS status =
            NtQueryDirectoryFile(dir, nullptr, nullptr, nullptr, &iosb, buf, sizeof(buf), FileNamesInformation, FALSE, nullptr, restart);
        restart = FALSE;
        if (!NT_SUCCESS(status))
            return FALSE;
        FILE_NAMES_INFORMATION *e = (FILE_NAMES_INFORMATION *)buf;
        for (;;) {
            ULONG n = e->FileNameLength / sizeof(WCHAR);
            if (!(n == 1 && e->FileName[0] == '.') && !(n == 2 && e->FileName[0] == '.' && e->FileName[1] == '.'))
                return TRUE;
            if (!e->NextEntryOffset)
                break;
            e = (FILE_NAMES_INFORMATION *)((PUCHAR)e + e->NextEntryOffset);
        }
    }
}

BOOL WINAPI RemoveDirectoryW(LPCWSTR name)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE h;
    NTSTATUS status = open_path(name, DELETE | FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT, &h);
    if (!NT_SUCCESS(status)) {
        if (status == STATUS_NOT_A_DIRECTORY)
            KbSetError(ERROR_DIRECTORY);
        else
            set_path_status(status);
        return FALSE;
    }
    if (directory_has_entries(h)) {
        NtClose(h);
        KbSetError(ERROR_DIR_NOT_EMPTY);
        return FALSE;
    }
    FILE_DISPOSITION_INFORMATION disp = {TRUE};
    IO_STATUS_BLOCK iosb;
    status = NtSetInformationFile(h, &iosb, &disp, sizeof(disp), FileDispositionInformation);
    NtClose(h);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI RemoveDirectoryA(LPCSTR name)
{
    PWSTR w = KbAnsiToWide(name);
    if (!w) {
        if (!name)
            KbSetError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    BOOL r = RemoveDirectoryW(w);
    KbFree(w);
    return r;
}

/* ------------------------------------------------------------------ copying */

#define COPY_CHUNK (64 * 1024)

BOOL WINAPI CopyFileExW(LPCWSTR source, LPCWSTR destination, LPPROGRESS_ROUTINE progress, LPVOID data,
                        LPBOOL cancel, DWORD flags)
{
    if (!source || !destination) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE in = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (in == INVALID_HANDLE_VALUE)
        return FALSE;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(in, &info)) {
        CloseHandle(in);
        return FALSE;
    }
    DWORD attributes = info.dwFileAttributes & ~(DWORD)FILE_ATTRIBUTE_DIRECTORY;
    HANDLE out = CreateFileW(destination, GENERIC_WRITE, 0, nullptr,
                             (flags & COPY_FILE_FAIL_IF_EXISTS) ? CREATE_NEW : CREATE_ALWAYS,
                             attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        CloseHandle(in);
        KbSetError(error);
        return FALSE;
    }
    PUCHAR buffer = KbAlloc(COPY_CHUNK);
    BOOL ok = buffer != nullptr;
    if (!ok)
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
    LARGE_INTEGER total, done, zero = {};
    total.LowPart = info.nFileSizeLow;
    total.HighPart = (LONG)info.nFileSizeHigh;
    done.QuadPart = 0;
    DWORD reason = CALLBACK_STREAM_SWITCH;
    while (ok) {
        if (progress) {
            DWORD r = progress(total, done, total, done, 1, reason, in, out, data);
            reason = CALLBACK_CHUNK_FINISHED;
            if (r == PROGRESS_CANCEL || r == PROGRESS_STOP) {
                KbSetError(ERROR_REQUEST_ABORTED);
                ok = FALSE;
                break;
            }
            if (r == PROGRESS_QUIET)
                progress = nullptr;
        }
        if (cancel && *cancel) {
            KbSetError(ERROR_REQUEST_ABORTED);
            ok = FALSE;
            break;
        }
        DWORD got = 0, put;
        if (!ReadFile(in, buffer, COPY_CHUNK, &got, nullptr)) {
            ok = FALSE;
            break;
        }
        if (!got)
            break;
        if (!WriteFile(out, buffer, got, &put, nullptr) || put != got) {
            ok = FALSE;
            break;
        }
        done.QuadPart += got;
    }
    (void)zero;
    KbFree(buffer);
    if (ok) {
        /* the copy is a new file, made now, with the contents of the original as last written. */
        SetFileTime(out, nullptr, nullptr, &info.ftLastWriteTime);
        CloseHandle(out);
        if (attributes & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesW(destination, attributes);
    } else {
        DWORD error = GetLastError();
        CloseHandle(out);
        DeleteFileW(destination);
        KbSetError(error);
    }
    CloseHandle(in);
    return ok;
}

BOOL WINAPI CopyFileW(LPCWSTR source, LPCWSTR destination, BOOL fail_if_exists)
{
    return CopyFileExW(source, destination, nullptr, nullptr, nullptr, fail_if_exists ? COPY_FILE_FAIL_IF_EXISTS : 0);
}

BOOL WINAPI CopyFileA(LPCSTR source, LPCSTR destination, BOOL fail_if_exists)
{
    PWSTR s = KbAnsiToWide(source), d = KbAnsiToWide(destination);
    BOOL r = FALSE;
    if (s && d)
        r = CopyFileW(s, d, fail_if_exists);
    else if (!source || !destination)
        KbSetError(ERROR_INVALID_PARAMETER);
    KbFree(s);
    KbFree(d);
    return r;
}

/* ------------------------------------------------------------------ moving */

static NTSTATUS rename_by_handle(LPCWSTR source, LPCWSTR destination, BOOL replace)
{
    UNICODE_STRING nt_dest;
    if (!RtlDosPathNameToNtPathName_U(destination, &nt_dest, nullptr, nullptr))
        return STATUS_OBJECT_PATH_NOT_FOUND;
    HANDLE h;
    NTSTATUS status = open_path(source, DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT, &h);
    if (NT_SUCCESS(status)) {
        ULONG size = (ULONG)(offsetof(KB_FILE_RENAME_INFORMATION, FileName) + nt_dest.Length);
        KB_FILE_RENAME_INFORMATION *info = KbAlloc(size);
        if (info) {
            info->ReplaceIfExists = (BOOLEAN)replace;
            info->RootDirectory = nullptr;
            info->FileNameLength = nt_dest.Length;
            memcpy(info->FileName, nt_dest.Buffer, nt_dest.Length);
            IO_STATUS_BLOCK iosb;
            status = NtSetInformationFile(h, &iosb, info, size, FileRenameInformation);
            KbFree(info);
        } else {
            status = STATUS_NO_MEMORY;
        }
        NtClose(h);
    }
    RtlFreeUnicodeString(&nt_dest);
    return status;
}

BOOL WINAPI MoveFileWithProgressW(LPCWSTR source, LPCWSTR destination, LPPROGRESS_ROUTINE progress, LPVOID data,
                                  DWORD flags)
{
    if (!source) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags & MOVEFILE_DELAY_UNTIL_REBOOT) {
        KbSetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!destination) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD src_attr = GetFileAttributesW(source);
    if (src_attr == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    DWORD dst_attr = GetFileAttributesW(destination);
    if (dst_attr != INVALID_FILE_ATTRIBUTES) {
        WCHAR a[MAX_PATH * 2], b[MAX_PATH * 2];
        BOOL same = GetFullPathNameW(source, RTL_NUMBER_OF(a), a, nullptr) &&
                    GetFullPathNameW(destination, RTL_NUMBER_OF(b), b, nullptr) && !_wcsicmp(a, b);
        if (!same && (!(flags & MOVEFILE_REPLACE_EXISTING) || (dst_attr & FILE_ATTRIBUTE_DIRECTORY))) {
            KbSetError(ERROR_ALREADY_EXISTS);
            return FALSE;
        }
    }
    NTSTATUS status = rename_by_handle(source, destination, (flags & MOVEFILE_REPLACE_EXISTING) != 0);
    if (NT_SUCCESS(status))
        return TRUE;
    if (status != STATUS_INVALID_INFO_CLASS && status != STATUS_NOT_IMPLEMENTED && status != STATUS_NOT_SUPPORTED &&
        status != STATUS_NOT_SAME_DEVICE)
        return KbSetStatus(status);
    /* the file system cannot rename: copy and delete instead. */
    if (src_attr & FILE_ATTRIBUTE_DIRECTORY) {
        KbSetError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (!CopyFileExW(source, destination, progress, data, nullptr, 0))
        return FALSE;
    if (!DeleteFileW(source)) {
        DWORD error = GetLastError();
        DeleteFileW(destination);
        KbSetError(error);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI MoveFileExW(LPCWSTR source, LPCWSTR destination, DWORD flags)
{
    return MoveFileWithProgressW(source, destination, nullptr, nullptr, flags);
}

BOOL WINAPI MoveFileW(LPCWSTR source, LPCWSTR destination)
{
    return MoveFileExW(source, destination, MOVEFILE_COPY_ALLOWED);
}

BOOL WINAPI MoveFileExA(LPCSTR source, LPCSTR destination, DWORD flags)
{
    PWSTR s = KbAnsiToWide(source), d = KbAnsiToWide(destination);
    BOOL r = FALSE;
    if (s && (d || !destination))
        r = MoveFileExW(s, d, flags);
    else if (!source)
        KbSetError(ERROR_INVALID_PARAMETER);
    KbFree(s);
    KbFree(d);
    return r;
}

BOOL WINAPI MoveFileA(LPCSTR source, LPCSTR destination)
{
    return MoveFileExA(source, destination, MOVEFILE_COPY_ALLOWED);
}

BOOL WINAPI ReplaceFileW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags, LPVOID exclude,
                         LPVOID reserved)
{
    if (!replaced || !replacement) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (GetFileAttributesW(replaced) == INVALID_FILE_ATTRIBUTES) {
        KbSetError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    if (backup && !CopyFileW(replaced, backup, FALSE)) {
        KbSetError(ERROR_UNABLE_TO_REMOVE_REPLACED);
        return FALSE;
    }
    if (!MoveFileExW(replacement, replaced, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        KbSetError(ERROR_UNABLE_TO_MOVE_REPLACEMENT);
        return FALSE;
    }
    return TRUE;
}

/*
 * the file is opened for writing its attributes, the one right a name added to it needs, and the link is set through
 * the open handle. the name is a full path, which the file system opens the directory of.
 */
BOOL WINAPI CreateHardLinkW(LPCWSTR link, LPCWSTR existing, LPSECURITY_ATTRIBUTES sa)
{
    if (!link || !existing) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE file;
    NTSTATUS status = open_path(existing, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT, &file);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    UNICODE_STRING nt_link;
    if (!RtlDosPathNameToNtPathName_U(link, &nt_link, nullptr, nullptr)) {
        NtClose(file);
        KbSetError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    ULONG size = (ULONG)(offsetof(KB_FILE_RENAME_INFORMATION, FileName) + nt_link.Length);
    KB_FILE_RENAME_INFORMATION *info = KbAllocZero(size);
    if (!info) {
        RtlFreeUnicodeString(&nt_link);
        NtClose(file);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    info->ReplaceIfExists = FALSE;
    info->RootDirectory = nullptr;
    info->FileNameLength = nt_link.Length;
    memcpy(info->FileName, nt_link.Buffer, nt_link.Length);
    IO_STATUS_BLOCK iosb;
    status = NtSetInformationFile(file, &iosb, info, size, FileLinkInformation);
    KbFree(info);
    RtlFreeUnicodeString(&nt_link);
    NtClose(file);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

BOOL WINAPI CreateHardLinkA(LPCSTR link, LPCSTR existing, LPSECURITY_ATTRIBUTES sa)
{
    if (!link) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR wide_link = KbAnsiToWide(link), wide_existing = existing ? KbAnsiToWide(existing) : nullptr;
    BOOL ok = FALSE;
    if (wide_link && (wide_existing || !existing))
        ok = CreateHardLinkW(wide_link, wide_existing, sa);
    KbFree(wide_link);
    KbFree(wide_existing);
    return ok;
}

NTSYSAPI NTSTATUS NTAPI RtlAcquirePrivilege(PULONG privileges, ULONG count, ULONG flags, PVOID *state);
NTSYSAPI void NTAPI RtlReleasePrivilege(PVOID state);

#define KB_SE_CREATE_SYMBOLIC_LINK_PRIVILEGE 35

/*
 * the link is made as an empty file or directory that is then given its reparse data. the privilege to make links is
 * switched on for the call, and without it the file system decides, which lets a program of a machine in developer mode
 * go on when it says so with SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE. a target that is a full path is kept as the
 * name of an object, one that is not as it is, and marked relative. the print name is the target as it was given.
 */
BOOLEAN WINAPI CreateSymbolicLinkW(LPCWSTR link, LPCWSTR target, DWORD flags)
{
    if (!link || !target) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KbSetError(0);
    PVOID state = nullptr;
    ULONG privilege = KB_SE_CREATE_SYMBOLIC_LINK_PRIVILEGE;
    NTSTATUS status = RtlAcquirePrivilege(&privilege, 1, 0, &state);
    if (!NT_SUCCESS(status) && !(flags & SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        KbSetStatus(status);
        return FALSE;
    }
    PWSTR full = nullptr;
    UNICODE_STRING substitute = {}, nt_link = {};
    BOOLEAN relative = FALSE, own_substitute = FALSE, ok = FALSE;
    HANDLE file = nullptr;
    REPARSE_DATA_BUFFER *buffer = nullptr;
    switch (RtlDetermineDosPathNameType_U(target)) {
    case RtlPathTypeDriveRelative:
        full = KbAlloc(MAX_PATH * 4 * sizeof(WCHAR));
        if (!full || !GetFullPathNameW(target, MAX_PATH * 4, full, nullptr)) {
            if (full)
                KbSetStatus(STATUS_OBJECT_PATH_NOT_FOUND);
            else
                KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            goto done;
        }
        target = full;
        [[fallthrough]];
    case RtlPathTypeUncAbsolute:
    case RtlPathTypeDriveAbsolute:
    case RtlPathTypeLocalDevice:
    case RtlPathTypeRootLocalDevice:
        if (!RtlDosPathNameToNtPathName_U(target, &substitute, nullptr, nullptr)) {
            KbSetError(ERROR_INVALID_PARAMETER);
            goto done;
        }
        own_substitute = TRUE;
        break;
    default:
        /* a path with no drive, or a name, stays what it is and is relative to the directory of the link. */
        substitute.Buffer = (PWSTR)target;
        substitute.Length = substitute.MaximumLength = (USHORT)(wcslen(target) * sizeof(WCHAR));
        relative = TRUE;
        break;
    }
    {
        ULONG print_bytes = (ULONG)(wcslen(target) * sizeof(WCHAR));
        ULONG size = (ULONG)(offsetof(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.PathBuffer) + print_bytes +
                             substitute.Length);
        buffer = KbAllocZero(size);
        if (!buffer) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            goto done;
        }
        buffer->ReparseTag = IO_REPARSE_TAG_SYMLINK;
        buffer->ReparseDataLength = (USHORT)(size - REPARSE_DATA_BUFFER_HEADER_SIZE);
        buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset = (USHORT)print_bytes;
        buffer->SymbolicLinkReparseBuffer.SubstituteNameLength = substitute.Length;
        buffer->SymbolicLinkReparseBuffer.PrintNameOffset = 0;
        buffer->SymbolicLinkReparseBuffer.PrintNameLength = (USHORT)print_bytes;
        buffer->SymbolicLinkReparseBuffer.Flags = relative ? SYMLINK_FLAG_RELATIVE : 0;
        memcpy(buffer->SymbolicLinkReparseBuffer.PathBuffer, target, print_bytes);
        memcpy((PUCHAR)buffer->SymbolicLinkReparseBuffer.PathBuffer + print_bytes, substitute.Buffer, substitute.Length);
        if (!RtlDosPathNameToNtPathName_U(link, &nt_link, nullptr, nullptr)) {
            KbSetError(ERROR_PATH_NOT_FOUND);
            goto done;
        }
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &nt_link, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
        IO_STATUS_BLOCK iosb;
        ULONG options = ((flags & SYMBOLIC_LINK_FLAG_DIRECTORY) ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE) |
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT;
        status = NtCreateFile(&file, DELETE | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, nullptr,
                              FILE_ATTRIBUTE_NORMAL, 0, FILE_CREATE, options, nullptr, 0);
        if (!NT_SUCCESS(status)) {
            KbSetStatus(status);
            goto done;
        }
        status = NtFsControlFile(file, nullptr, nullptr, nullptr, &iosb, FSCTL_SET_REPARSE_POINT, buffer, size, nullptr, 0);
        if (!NT_SUCCESS(status)) {
            KbSetStatus(status);
            /* what was made for the link does not stay. */
            FILE_DISPOSITION_INFORMATION remove = {TRUE};
            NtSetInformationFile(file, &iosb, &remove, sizeof(remove), FileDispositionInformation);
            goto done;
        }
        ok = TRUE;
    }
done:
    if (state)
        RtlReleasePrivilege(state);
    KbFree(full);
    KbFree(buffer);
    if (own_substitute)
        RtlFreeUnicodeString(&substitute);
    if (nt_link.Buffer)
        RtlFreeUnicodeString(&nt_link);
    if (file)
        NtClose(file);
    return ok;
}

BOOLEAN WINAPI CreateSymbolicLinkA(LPCSTR link, LPCSTR target, DWORD flags)
{
    if (!link || !target) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR wide_link = KbAnsiToWide(link), wide_target = KbAnsiToWide(target);
    BOOLEAN ok = FALSE;
    if (wide_link && wide_target)
        ok = CreateSymbolicLinkW(wide_link, wide_target, flags);
    KbFree(wide_link);
    KbFree(wide_target);
    return ok;
}

BOOL WINAPI ReplaceFileA(LPCSTR replaced, LPCSTR replacement, LPCSTR backup, DWORD flags, LPVOID exclude,
                         LPVOID reserved)
{
    if (!replaced || !replacement) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR wide_replaced = KbAnsiToWide(replaced), wide_replacement = KbAnsiToWide(replacement);
    PWSTR wide_backup = backup ? KbAnsiToWide(backup) : nullptr;
    BOOL ok = FALSE;
    if (wide_replaced && wide_replacement && (wide_backup || !backup))
        ok = ReplaceFileW(wide_replaced, wide_replacement, wide_backup, flags, exclude, reserved);
    KbFree(wide_replaced);
    KbFree(wide_replacement);
    KbFree(wide_backup);
    return ok;
}

/* ------------------------------------------------------------------ enumeration */

#define FIND_MAGIC 0x444E4946u /* "FIND". */
#define FIND_BUFFER_SIZE 8192

typedef struct FIND_STATE {
    ULONG Magic;
    HANDLE Directory;
    ULONG Offset; /* of the next entry, ~0 when the buffer is used up. */
    FINDEX_INFO_LEVELS Level;
    RTL_CRITICAL_SECTION Lock;
    ULONG_PTR Buffer[FIND_BUFFER_SIZE / sizeof(ULONG_PTR)];
} FIND_STATE;

/* rewrites a win32 pattern with the dos wildcards the file system matches, as windows does. */
static void convert_pattern(PCWSTR in, SIZE_T n, PWSTR out)
{
    if (n == 3 && in[0] == '*' && in[1] == '.' && in[2] == '*') {
        out[0] = '*';
        out[1] = 0;
        return;
    }
    for (SIZE_T i = 0; i < n; i++) {
        WCHAR c = in[i], next = i + 1 < n ? in[i + 1] : 0;
        if (c == '?')
            out[i] = '>';
        else if (c == '*' && next == '.')
            out[i] = '<';
        else if (c == '.' && (next == '?' || next == '*' || !next))
            out[i] = '"';
        else
            out[i] = c;
    }
    out[n] = 0;
}

static void fill_find_data(const FILE_BOTH_DIR_INFORMATION *e, FINDEX_INFO_LEVELS level, LPWIN32_FIND_DATAW data)
{
    memset(data, 0, sizeof(*data));
    data->dwFileAttributes = e->FileAttributes;
    set_filetime(&data->ftCreationTime, e->CreationTime.QuadPart);
    set_filetime(&data->ftLastAccessTime, e->LastAccessTime.QuadPart);
    set_filetime(&data->ftLastWriteTime, e->LastWriteTime.QuadPart);
    data->nFileSizeHigh = (DWORD)e->EndOfFile.HighPart;
    data->nFileSizeLow = e->EndOfFile.LowPart;
    if (e->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        data->dwReserved0 = e->EaSize;
    SIZE_T n = e->FileNameLength / sizeof(WCHAR);
    if (n >= MAX_PATH)
        n = MAX_PATH - 1;
    memcpy(data->cFileName, e->FileName, n * sizeof(WCHAR));
    data->cFileName[n] = 0;
    if (level == FindExInfoStandard && e->ShortNameLength) {
        SIZE_T s = (SIZE_T)e->ShortNameLength / sizeof(WCHAR);
        if (s > 13)
            s = 13;
        memcpy(data->cAlternateFileName, e->ShortName, s * sizeof(WCHAR));
        data->cAlternateFileName[s] = 0;
    }
}

/* refills the buffer as needed; NULL with the last error set at the end. */
static const FILE_BOTH_DIR_INFORMATION *next_entry(FIND_STATE *f, PUNICODE_STRING pattern, BOOLEAN first)
{
    if (f->Offset == (ULONG)~0u) {
        IO_STATUS_BLOCK iosb;
        NTSTATUS status = NtQueryDirectoryFile(f->Directory, nullptr, nullptr, nullptr, &iosb, f->Buffer, sizeof(f->Buffer),
                                               FileBothDirectoryInformation, FALSE, pattern, first);
        if (!NT_SUCCESS(status)) {
            if (status == STATUS_NO_MORE_FILES || status == STATUS_NO_SUCH_FILE ||
                status == STATUS_OBJECT_NAME_NOT_FOUND)
                KbSetError(first ? ERROR_FILE_NOT_FOUND : ERROR_NO_MORE_FILES);
            else
                KbSetStatus(status);
            return nullptr;
        }
        f->Offset = 0;
    }
    const FILE_BOTH_DIR_INFORMATION *e = (const FILE_BOTH_DIR_INFORMATION *)((PUCHAR)f->Buffer + f->Offset);
    f->Offset = e->NextEntryOffset ? f->Offset + e->NextEntryOffset : (ULONG)~0u;
    return e;
}

HANDLE WINAPI FindFirstFileExW(LPCWSTR name, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS op,
                               LPVOID filter, DWORD flags)
{
    if (!name || !data || (level != FindExInfoStandard && level != FindExInfoBasic) ||
        (op != FindExSearchNameMatch && op != FindExSearchLimitToDirectories) || filter) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    if (!*name) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    SIZE_T len = wcslen(name);
    if (name[len - 1] == '\\' || name[len - 1] == '/') {
        KbSetError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    PCWSTR file = name + len;
    while (file > name && file[-1] != '\\' && file[-1] != '/' && !(file - name == 2 && name[1] == ':'))
        file--;
    SIZE_T dir_len = (SIZE_T)(file - name);
    PWSTR dir = KbAlloc((dir_len + 2) * sizeof(WCHAR));
    PWSTR pattern = KbAlloc((len - dir_len + 1) * sizeof(WCHAR));
    FIND_STATE *f = KbAllocZero(sizeof(FIND_STATE));
    if (!dir || !pattern || !f) {
        KbFree(dir);
        KbFree(pattern);
        KbFree(f);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    if (dir_len) {
        memcpy(dir, name, dir_len * sizeof(WCHAR));
        dir[dir_len] = 0;
    } else {
        wcscpy(dir, L".");
    }
    convert_pattern(file, len - dir_len, pattern);
    NTSTATUS status = open_path(dir, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT, &f->Directory);
    if (!NT_SUCCESS(status) && (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND) &&
        MimeFeatureFaultFromRuntime(dir, __builtin_return_address(0)))
        status = open_path(dir, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT, &f->Directory);
    KbFree(dir);
    if (!NT_SUCCESS(status)) {
        KbFree(pattern);
        KbFree(f);
        if (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_NOT_A_DIRECTORY)
            KbSetError(ERROR_PATH_NOT_FOUND);
        else
            set_path_status(status);
        return INVALID_HANDLE_VALUE;
    }
    f->Magic = FIND_MAGIC;
    f->Offset = (ULONG)~0u;
    f->Level = level;
    RtlInitializeCriticalSection(&f->Lock);
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, pattern);
    const FILE_BOTH_DIR_INFORMATION *e = next_entry(f, &us, TRUE);
    KbFree(pattern);
    if (!e) {
        DWORD error = GetLastError();
        NtClose(f->Directory);
        RtlDeleteCriticalSection(&f->Lock);
        KbFree(f);
        KbSetError(error);
        return INVALID_HANDLE_VALUE;
    }
    fill_find_data(e, level, data);
    return f;
}

HANDLE WINAPI FindFirstFileW(LPCWSTR name, LPWIN32_FIND_DATAW data)
{
    return FindFirstFileExW(name, FindExInfoStandard, data, FindExSearchNameMatch, nullptr, 0);
}

static FIND_STATE *find_state(HANDLE h)
{
    FIND_STATE *f = h;
    if (!f || f == INVALID_HANDLE_VALUE || ((ULONG_PTR)f & 7) || f->Magic != FIND_MAGIC) {
        KbSetError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    return f;
}

BOOL WINAPI FindNextFileW(HANDLE find, LPWIN32_FIND_DATAW data)
{
    FIND_STATE *f = find_state(find);
    if (!f)
        return FALSE;
    if (!data) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlEnterCriticalSection(&f->Lock);
    const FILE_BOTH_DIR_INFORMATION *e = next_entry(f, nullptr, FALSE);
    if (e)
        fill_find_data(e, f->Level, data);
    RtlLeaveCriticalSection(&f->Lock);
    return e != nullptr;
}

#define NAME_FIND_MAGIC 0x4E444E46u /* "FNDN". */

/* FindFirstFileNameW's handle: the file, kept open for the ids of the directories, and the names it has. */
typedef struct NAME_FIND {
    ULONG Magic;
    HANDLE File;
    FILE_LINKS_INFORMATION *Links;
    PUCHAR Cursor; /* the entry of the next name, or the end of the entries. */
    ULONG Size;    /* the bytes the file system wrote. */
    LONGLONG ParentId;                /* the directory last named, which the next names are most likely in. */
    FILE_NAME_INFORMATION *Parent;
} NAME_FIND;

#define STREAM_FIND_MAGIC 0x53444E46u /* "FNDS". */

/* FindFirstStreamW's handle: the streams are not kept, a file system without them has the one stream of the data. */
typedef struct STREAM_FIND {
    ULONG Magic;
    BOOL Given;
} STREAM_FIND;

BOOL WINAPI FindClose(HANDLE find)
{
    STREAM_FIND *streams = find;
    if (streams && streams != INVALID_HANDLE_VALUE && !((ULONG_PTR)streams & 7) && streams->Magic == STREAM_FIND_MAGIC) {
        streams->Magic = 0;
        KbFree(streams);
        return TRUE;
    }
    NAME_FIND *names = find;
    if (names && names != INVALID_HANDLE_VALUE && !((ULONG_PTR)names & 7) && names->Magic == NAME_FIND_MAGIC) {
        names->Magic = 0;
        if (names->File && names->File != INVALID_HANDLE_VALUE)
            NtClose(names->File);
        KbFree(names->Links);
        KbFree(names->Parent);
        KbFree(names);
        return TRUE;
    }
    FIND_STATE *f = find_state(find);
    if (!f)
        return FALSE;
    f->Magic = 0;
    NtClose(f->Directory);
    RtlDeleteCriticalSection(&f->Lock);
    KbFree(f);
    return TRUE;
}

static void find_data_to_ansi(const WIN32_FIND_DATAW *w, LPWIN32_FIND_DATAA a)
{
    memcpy(a, w, offsetof(WIN32_FIND_DATAA, cFileName));
    UINT cp = KbFileApiCodePage();
    if (!WideCharToMultiByte(cp, 0, w->cFileName, -1, a->cFileName, sizeof(a->cFileName), nullptr, nullptr))
        a->cFileName[sizeof(a->cFileName) - 1] = 0;
    if (!WideCharToMultiByte(cp, 0, w->cAlternateFileName, -1, a->cAlternateFileName, sizeof(a->cAlternateFileName),
                             nullptr, nullptr))
        a->cAlternateFileName[0] = 0;
}

HANDLE WINAPI FindFirstFileExA(LPCSTR name, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS op,
                               LPVOID filter, DWORD flags)
{
    if (!data) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    PWSTR w = KbAnsiToWide(name);
    if (!w) {
        if (!name)
            KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    WIN32_FIND_DATAW wd;
    HANDLE h = FindFirstFileExW(w, level, &wd, op, filter, flags);
    KbFree(w);
    if (h != INVALID_HANDLE_VALUE)
        find_data_to_ansi(&wd, data);
    return h;
}

HANDLE WINAPI FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA data)
{
    return FindFirstFileExA(name, FindExInfoStandard, data, FindExSearchNameMatch, nullptr, 0);
}

BOOL WINAPI FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data)
{
    if (!data) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WIN32_FIND_DATAW wd;
    if (!FindNextFileW(find, &wd))
        return FALSE;
    find_data_to_ansi(&wd, data);
    return TRUE;
}

/* ------------------------------------------------------------------ directory change notifications */

#define FILE_NOTIFY_VALID_MASK 0x00000FFF

/* the filter and tree flag of the handle, so FindNextChangeNotification can repeat the request. */
typedef struct _CHANGE_HANDLE {
    struct _CHANGE_HANDLE *Next;
    HANDLE Directory;
    DWORD Filter;
    BOOL Subtree;
} CHANGE_HANDLE;

static SRWLOCK g_change_lock = SRWLOCK_INIT;
static CHANGE_HANDLE *g_changes;

/* the directory is opened for listing only, fully shared and asynchronous so a request waits apart. */
HANDLE WINAPI FindFirstChangeNotificationW(LPCWSTR path, BOOL subtree, DWORD filter)
{
    if (!path || !filter || (filter & ~FILE_NOTIFY_VALID_MASK)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE directory = CreateFileW(path, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directory == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    CHANGE_HANDLE *c = HeapAlloc(GetProcessHeap(), 0, sizeof(*c));
    if (!c) {
        CloseHandle(directory);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    c->Directory = directory;
    c->Filter = filter;
    c->Subtree = subtree != 0;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtNotifyChangeDirectoryFile(directory, nullptr, nullptr, nullptr, &iosb, nullptr, 0, filter,
                                                  c->Subtree);
    if (NT_ERROR(status)) {
        HeapFree(GetProcessHeap(), 0, c);
        CloseHandle(directory);
        KbSetStatus(status);
        return INVALID_HANDLE_VALUE;
    }
    AcquireSRWLockExclusive(&g_change_lock);
    c->Next = g_changes;
    g_changes = c;
    ReleaseSRWLockExclusive(&g_change_lock);
    return directory;
}

HANDLE WINAPI FindFirstChangeNotificationA(LPCSTR path, BOOL subtree, DWORD filter)
{
    WCHAR wide[MAX_PATH];
    if (!path || !MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    return FindFirstChangeNotificationW(wide, subtree, filter);
}

/* waits for the next change; what happened since the handle last signalled is kept for it. */
BOOL WINAPI FindNextChangeNotification(HANDLE handle)
{
    CHANGE_HANDLE c = {};
    BOOL found = FALSE;
    AcquireSRWLockShared(&g_change_lock);
    for (CHANGE_HANDLE *p = g_changes; p; p = p->Next)
        if (p->Directory == handle) {
            c = *p;
            found = TRUE;
            break;
        }
    ReleaseSRWLockShared(&g_change_lock);
    if (!found) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtNotifyChangeDirectoryFile(handle, nullptr, nullptr, nullptr, &iosb, nullptr, 0, c.Filter, c.Subtree);
    return NT_ERROR(status) ? KbSetStatus(status) : TRUE;
}

BOOL WINAPI FindCloseChangeNotification(HANDLE handle)
{
    AcquireSRWLockExclusive(&g_change_lock);
    for (CHANGE_HANDLE **link = &g_changes; *link; link = &(*link)->Next)
        if ((*link)->Directory == handle) {
            CHANGE_HANDLE *dead = *link;
            *link = dead->Next;
            HeapFree(GetProcessHeap(), 0, dead);
            break;
        }
    ReleaseSRWLockExclusive(&g_change_lock);
    return CloseHandle(handle);
}

/*
 * fills the buffer with FILE_NOTIFY_INFORMATION records. an overlapped request finishes through its event, completion
 * port or routine, and the call succeeds once the request is made. zero bytes returned means the buffer was too small,
 * and the caller rereads the directory.
 */
BOOL WINAPI ReadDirectoryChangesW(HANDLE directory, LPVOID buffer, DWORD length, BOOL subtree, DWORD filter,
                                  LPDWORD returned, LPOVERLAPPED overlapped, LPOVERLAPPED_COMPLETION_ROUTINE routine)
{
    if (returned)
        *returned = 0;
    if (!buffer || !length || !filter || (filter & ~FILE_NOTIFY_VALID_MASK)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (overlapped) {
        PIO_STATUS_BLOCK iosb = (PIO_STATUS_BLOCK)overlapped;
        iosb->Status = STATUS_PENDING;
        iosb->Information = 0;
        NTSTATUS status;
        if (routine)
            status = NtNotifyChangeDirectoryFile(directory, nullptr, (PIO_APC_ROUTINE)completion_apc, (PVOID)routine, iosb,
                                                 buffer, length, filter, subtree != 0);
        else
            status = NtNotifyChangeDirectoryFile(directory, overlapped_event(overlapped), nullptr,
                                                 overlapped_port_context(overlapped), iosb, buffer, length, filter,
                                                 subtree != 0);
        /* unlike a read, a request that was started is a success, and the call says so whether it is done or not. */
        if (NT_ERROR(status)) {
            iosb->Status = status;
            return KbSetStatus(status);
        }
        if (status != STATUS_PENDING && returned && !routine)
            *returned = (DWORD)iosb->Information;
        return TRUE;
    }
    IO_STATUS_BLOCK local;
    NTSTATUS status = NtNotifyChangeDirectoryFile(directory, nullptr, nullptr, nullptr, &local, buffer, length, filter,
                                                  subtree != 0);
    if (status == STATUS_PENDING) {
        NtWaitForSingleObject(directory, FALSE, nullptr);
        status = local.Status;
    }
    if (NT_ERROR(status))
        return KbSetStatus(status);
    if (returned)
        *returned = (DWORD)local.Information;
    return TRUE;
}

/* opens the same file again, through its path. */
HANDLE WINAPI ReOpenFile(HANDLE original, DWORD access, DWORD share, DWORD flags)
{
    WCHAR path[MAX_PATH + 8];
    DWORD n = GetFinalPathNameByHandleW(original, path, RTL_NUMBER_OF(path), 0);
    if (!n || n >= RTL_NUMBER_OF(path)) {
        if (n)
            KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return INVALID_HANDLE_VALUE;
    }
    return CreateFileW(path, access, share, nullptr, OPEN_EXISTING, flags, nullptr);
}

/* the kernel posts every completion, so skipping the port on success is refused and callers keep packets. */
BOOL WINAPI SetFileCompletionNotificationModes(HANDLE file, UCHAR flags)
{
    FILE_IO_COMPLETION_NOTIFICATION_INFORMATION info = {flags};
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtSetInformationFile(file, &iosb, &info, sizeof(info), FileIoCompletionNotificationInformation);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

/* ------------------------------------------------------------------ names of links and compressed sizes */

/*
 * the path of the directory a link is in, as the file system names it: the file system opens the directory by its id,
 * with the open file for the volume, and says what it is called. the answer is a FILE_NAME_INFORMATION.
 */
static NTSTATUS find_parent(HANDLE file, LONGLONG id, FILE_NAME_INFORMATION **name)
{
    UNICODE_STRING id_name = {sizeof(id), sizeof(id), (PWSTR)&id};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &id_name, OBJ_CASE_INSENSITIVE, file, nullptr);
    HANDLE directory;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtCreateFile(&directory, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, nullptr, FILE_ATTRIBUTE_NORMAL,
                                   0, FILE_OPEN, FILE_OPEN_BY_FILE_ID | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    if (!NT_SUCCESS(status))
        return status;
    ULONG size = 512;
    *name = nullptr;
    for (;;) {
        FILE_NAME_INFORMATION *buffer = KbAlloc(size);
        if (!buffer) {
            status = STATUS_NO_MEMORY;
            break;
        }
        status = NtQueryInformationFile(directory, &iosb, buffer, size, FileNameInformation);
        if (status == STATUS_BUFFER_OVERFLOW) {
            size = buffer->FileNameLength + sizeof(ULONG) + sizeof(WCHAR);
            KbFree(buffer);
            continue;
        }
        if (NT_SUCCESS(status))
            *name = buffer;
        else
            KbFree(buffer);
        break;
    }
    NtClose(directory);
    return status;
}

/*
 * the file's own name, for a file that is the one entry of its list and whose directory the file system cannot open by
 * id. the name it was opened by is the only name it has.
 */
static NTSTATUS own_name(HANDLE file, const FILE_LINK_ENTRY_INFORMATION *entry, FILE_NAME_INFORMATION **parent,
                         PCWSTR *leaf, ULONG *leaf_chars)
{
    ULONG size = 512;
    for (;;) {
        FILE_NAME_INFORMATION *buffer = KbAlloc(size);
        IO_STATUS_BLOCK iosb;
        if (!buffer)
            return STATUS_NO_MEMORY;
        NTSTATUS status = NtQueryInformationFile(file, &iosb, buffer, size, FileNameInformation);
        if (status == STATUS_BUFFER_OVERFLOW) {
            size = buffer->FileNameLength + sizeof(ULONG) + sizeof(WCHAR);
            KbFree(buffer);
            continue;
        }
        if (!NT_SUCCESS(status)) {
            KbFree(buffer);
            return status;
        }
        ULONG chars = buffer->FileNameLength / sizeof(WCHAR), start = chars;
        while (start && buffer->FileName[start - 1] != '\\')
            start--;
        if (chars - start != entry->FileNameLength || _wcsnicmp(buffer->FileName + start, entry->FileName, chars - start)) {
            KbFree(buffer);
            return STATUS_OBJECT_PATH_NOT_FOUND;
        }
        /* the directory part, with a backslash kept only for the root. */
        buffer->FileNameLength = (start > 1 ? start - 1 : start) * sizeof(WCHAR);
        *parent = buffer;
        *leaf = entry->FileName;
        *leaf_chars = entry->FileNameLength;
        return STATUS_SUCCESS;
    }
}

/*
 * copies the name of the entry at the cursor to the caller's buffer as a path from the volume's root, or reports the
 * length it needs, which is not an end of the list. the cursor moves on only when the name was given.
 */
static BOOL give_link_name(NAME_FIND *f, LPDWORD length, PWSTR buffer)
{
    const FILE_LINK_ENTRY_INFORMATION *entry = (const FILE_LINK_ENTRY_INFORMATION *)f->Cursor;
    FILE_NAME_INFORMATION *parent = nullptr;
    PCWSTR leaf = entry->FileName;
    ULONG leaf_chars = entry->FileNameLength;
    NTSTATUS status = STATUS_SUCCESS;
    BOOL owned = FALSE;
    /* names of one directory are listed together, and its path is asked of the file system once. */
    if (f->Parent && f->ParentId == entry->ParentFileId) {
        parent = f->Parent;
    } else {
        status = find_parent(f->File, entry->ParentFileId, &parent);
        if (NT_SUCCESS(status)) {
            KbFree(f->Parent);
            f->Parent = parent;
            f->ParentId = entry->ParentFileId;
        } else {
            status = own_name(f->File, entry, &parent, &leaf, &leaf_chars);
            owned = TRUE;
        }
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    ULONG parent_chars = parent->FileNameLength / sizeof(WCHAR);
    BOOL root = parent_chars == 1;
    /* the path, the separator the root already has, the name and the end. */
    DWORD need = parent_chars + (root ? 0 : 1) + leaf_chars + 1;
    DWORD have = *length;
    *length = need;
    if (have < need) {
        if (owned)
            KbFree(parent);
        KbSetError(ERROR_MORE_DATA);
        return FALSE;
    }
    memcpy(buffer, parent->FileName, parent_chars * sizeof(WCHAR));
    DWORD at = parent_chars;
    if (!root)
        buffer[at++] = L'\\';
    memcpy(buffer + at, leaf, leaf_chars * sizeof(WCHAR));
    buffer[at + leaf_chars] = 0;
    if (owned)
        KbFree(parent);
    f->Cursor = entry->NextEntryOffset ? f->Cursor + entry->NextEntryOffset : (PUCHAR)f->Links + f->Size;
    return TRUE;
}

/*
 * the names of a file are asked of the file system in one list, and each is turned into a path when the caller asks for
 * it. an unknown flag, or a name that is no path, fails as windows does; a buffer too small fails the first call with
 * ERROR_MORE_DATA and the length it needs.
 */
HANDLE WINAPI FindFirstFileNameW(LPCWSTR path, DWORD flags, LPDWORD length, PWSTR buffer)
{
    if (flags || !length) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    UNICODE_STRING nt;
    if (!path || !RtlDosPathNameToNtPathName_U(path, &nt, nullptr, nullptr)) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE file;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtCreateFile(&file, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, nullptr, FILE_ATTRIBUTE_NORMAL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                   FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    RtlFreeUnicodeString(&nt);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return INVALID_HANDLE_VALUE;
    }
    NAME_FIND *f = KbAllocZero(sizeof(*f));
    if (!f) {
        NtClose(file);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    f->Magic = NAME_FIND_MAGIC;
    f->File = file;
    ULONG size = 96;
    for (int attempt = 0; attempt < 8; attempt++) {
        KbFree(f->Links);
        f->Links = KbAlloc(size);
        if (!f->Links) {
            status = STATUS_NO_MEMORY;
            break;
        }
        status = NtQueryInformationFile(file, &iosb, f->Links, size, FileHardLinkInformation);
        /* the names grow with the file's links: the file system says how much room they take. */
        if (status != STATUS_BUFFER_OVERFLOW || f->Links->BytesNeeded <= size)
            break;
        size = f->Links->BytesNeeded;
    }
    if (NT_SUCCESS(status) && (!iosb.Information || !f->Links->EntriesReturned))
        status = STATUS_END_OF_FILE;
    if (NT_SUCCESS(status)) {
        f->Size = (ULONG)iosb.Information;
        f->Cursor = (PUCHAR)&f->Links->Entry;
        if (give_link_name(f, length, buffer))
            return f;
    } else {
        KbSetStatus(status);
    }
    FindClose(f);
    return INVALID_HANDLE_VALUE;
}

BOOL WINAPI FindNextFileNameW(HANDLE find, LPDWORD length, PWSTR buffer)
{
    NAME_FIND *f = find;
    if (!f || f == INVALID_HANDLE_VALUE || ((ULONG_PTR)f & 7) || f->Magic != NAME_FIND_MAGIC) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!length) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (f->Cursor == (PUCHAR)f->Links + f->Size) {
        KbSetStatus(STATUS_END_OF_FILE);
        return FALSE;
    }
    return give_link_name(f, length, buffer);
}

/*
 * a file has the stream of its data and no other, as the file system keeps no alternate streams: its name is ::$DATA and
 * its size the size of the file. a directory has no stream, which is the end of the file.
 */
HANDLE WINAPI FindFirstStreamW(LPCWSTR path, STREAM_INFO_LEVELS level, LPVOID data, DWORD flags)
{
    if (!path || level != FindStreamInfoStandard || !data || flags) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE file = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    BOOL ok = GetFileInformationByHandle(file, &info);
    NtClose(file);
    if (!ok)
        return INVALID_HANDLE_VALUE;
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        KbSetError(ERROR_HANDLE_EOF);
        return INVALID_HANDLE_VALUE;
    }
    STREAM_FIND *f = KbAllocZero(sizeof(*f));
    if (!f) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    f->Magic = STREAM_FIND_MAGIC;
    f->Given = TRUE;
    WIN32_FIND_STREAM_DATA *out = data;
    out->StreamSize.QuadPart = (LONGLONG)(((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow);
    wcscpy(out->cStreamName, L"::$DATA");
    return f;
}

BOOL WINAPI FindNextStreamW(HANDLE find, LPVOID data)
{
    STREAM_FIND *f = find;
    if (!f || f == INVALID_HANDLE_VALUE || ((ULONG_PTR)f & 7) || f->Magic != STREAM_FIND_MAGIC) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    KbSetError(ERROR_HANDLE_EOF);
    return FALSE;
}

#define FILE_COMPRESSION_INFORMATION_CLASS 28

/* the on disk size of a compressed or sparse file, or the file size when the file system does not say. */
DWORD WINAPI GetCompressedFileSizeW(LPCWSTR name, LPDWORD high)
{
    HANDLE file = CreateFileW(name, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return INVALID_FILE_SIZE;
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER compressed[2] = {};
    if (!NT_SUCCESS(NtQueryInformationFile(file, &iosb, compressed, sizeof(compressed),
                                           FILE_COMPRESSION_INFORMATION_CLASS))) {
        DWORD size = GetFileSize(file, high);
        DWORD error = GetLastError();
        CloseHandle(file);
        KbSetError(error);
        return size;
    }
    CloseHandle(file);
    if (high)
        *high = (DWORD)compressed[0].HighPart;
    if (compressed[0].LowPart == INVALID_FILE_SIZE)
        KbSetError(ERROR_SUCCESS);
    return compressed[0].LowPart;
}

DWORD WINAPI GetCompressedFileSizeA(LPCSTR name, LPDWORD high)
{
    PWSTR w = KbAnsiToWide(name);
    if (!w) {
        if (!name)
            KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_FILE_SIZE;
    }
    DWORD size = GetCompressedFileSizeW(w, high);
    DWORD error = GetLastError();
    KbFree(w);
    KbSetError(error);
    return size;
}

/* ------------------------------------------------------------------ CopyFile2 */

/* progress for the CopyFile2 callback: a stream start message, then one per chunk with the totals so far. */
typedef struct COPY2_CONTEXT {
    PCOPYFILE2_PROGRESS_ROUTINE Routine;
    PVOID Context;
    ULONGLONG Chunks;
} COPY2_CONTEXT;

static DWORD CALLBACK copy2_progress(LARGE_INTEGER total, LARGE_INTEGER transferred, LARGE_INTEGER stream_size,
                                     LARGE_INTEGER stream_transferred, DWORD stream, DWORD reason, HANDLE source,
                                     HANDLE destination, LPVOID data)
{
    COPY2_CONTEXT *c = data;
    COPYFILE2_MESSAGE m = {};
    if (reason == CALLBACK_STREAM_SWITCH) {
        m.Type = COPYFILE2_CALLBACK_STREAM_STARTED;
        m.Info.StreamStarted.dwStreamNumber = stream;
        m.Info.StreamStarted.hSourceFile = source;
        m.Info.StreamStarted.hDestinationFile = destination;
        m.Info.StreamStarted.uliStreamSize.QuadPart = (ULONGLONG)stream_size.QuadPart;
        m.Info.StreamStarted.uliTotalFileSize.QuadPart = (ULONGLONG)total.QuadPart;
    } else {
        m.Type = COPYFILE2_CALLBACK_CHUNK_FINISHED;
        m.Info.ChunkFinished.dwStreamNumber = stream;
        m.Info.ChunkFinished.hSourceFile = source;
        m.Info.ChunkFinished.hDestinationFile = destination;
        m.Info.ChunkFinished.uliChunkNumber.QuadPart = c->Chunks++;
        m.Info.ChunkFinished.uliChunkSize.QuadPart = (ULONGLONG)transferred.QuadPart;
        m.Info.ChunkFinished.uliStreamSize.QuadPart = (ULONGLONG)stream_size.QuadPart;
        m.Info.ChunkFinished.uliStreamBytesTransferred.QuadPart = (ULONGLONG)stream_transferred.QuadPart;
        m.Info.ChunkFinished.uliTotalFileSize.QuadPart = (ULONGLONG)total.QuadPart;
        m.Info.ChunkFinished.uliTotalBytesTransferred.QuadPart = (ULONGLONG)transferred.QuadPart;
    }
    switch (c->Routine(&m, c->Context)) {
    case COPYFILE2_PROGRESS_CANCEL:
        return PROGRESS_CANCEL;
    case COPYFILE2_PROGRESS_STOP:
        return PROGRESS_STOP;
    case COPYFILE2_PROGRESS_QUIET:
        return PROGRESS_QUIET;
    default:
        return PROGRESS_CONTINUE;
    }
}

HRESULT WINAPI CopyFile2(PCWSTR source, PCWSTR destination, COPYFILE2_EXTENDED_PARAMETERS *parameters)
{
    COPY2_CONTEXT context = {};
    DWORD flags = 0;
    BOOL *cancel = nullptr;
    if (parameters) {
        /* the larger structure adds fields, accepted here only when all zero. */
        if (parameters->dwSize != sizeof(*parameters)) {
            if (parameters->dwSize != 112)
                return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
            const ULONGLONG *tail = (const ULONGLONG *)((const BYTE *)parameters + 64);
            for (int i = 0; i < 6; i++) {
                if (tail[i])
                    return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
            }
        }
        flags = parameters->dwCopyFlags;
        cancel = parameters->pfCancel;
        context.Routine = parameters->pProgressRoutine;
        context.Context = parameters->pvCallbackContext;
    }
    if ((flags & 0x40000000) || ((flags & 0x4002) && (flags & 0x08000000)))
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    if (!CopyFileExW(source, destination, context.Routine ? copy2_progress : nullptr, &context, cancel, flags)) {
        DWORD error = GetLastError();
        return error ? (HRESULT)((error & 0xFFFF) | 0x80070000) : E_FAIL;
    }
    return S_OK;
}
