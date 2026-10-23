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

/* the kernel has no byte range locks yet: locking always succeeds. */
BOOL WINAPI LockFile(HANDLE file, DWORD offset_low, DWORD offset_high, DWORD length_low, DWORD length_high)
{
    return TRUE;
}

BOOL WINAPI LockFileEx(HANDLE file, DWORD flags, DWORD reserved, DWORD length_low, DWORD length_high,
                       LPOVERLAPPED overlapped)
{
    if (reserved || !overlapped) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    overlapped->Internal = STATUS_SUCCESS;
    overlapped->InternalHigh = 0;
    return TRUE;
}

BOOL WINAPI UnlockFile(HANDLE file, DWORD offset_low, DWORD offset_high, DWORD length_low, DWORD length_high)
{
    return TRUE;
}

BOOL WINAPI UnlockFileEx(HANDLE file, DWORD reserved, DWORD length_low, DWORD length_high, LPOVERLAPPED overlapped)
{
    if (reserved || !overlapped) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
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
    NTSTATUS status = NtSetInformationFile(file, &iosb, info, size, nt_class);
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

/* finds the drive letter of the volume a handle lives on by comparing serial numbers. */
static WCHAR drive_of_handle(HANDLE file)
{
    ULONG serial = volume_serial(file);
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(drives & (1u << i)))
            continue;
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
        SetFileTime(out, &info.ftCreationTime, &info.ftLastAccessTime, &info.ftLastWriteTime);
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

BOOL WINAPI CreateHardLinkW(LPCWSTR link, LPCWSTR existing, LPSECURITY_ATTRIBUTES sa)
{
    KbSetError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

BOOLEAN WINAPI CreateSymbolicLinkW(LPCWSTR link, LPCWSTR target, DWORD flags)
{
    KbSetError(ERROR_PRIVILEGE_NOT_HELD);
    return FALSE;
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

/* FindFirstFileNameW's handle: the file's link names, one by one. */
typedef struct NAME_FIND {
    ULONG Magic;
    ULONG Next;
    ULONG Count;
    PWSTR Names[1];
} NAME_FIND;

BOOL WINAPI FindClose(HANDLE find)
{
    NAME_FIND *names = find;
    if (names && names != INVALID_HANDLE_VALUE && !((ULONG_PTR)names & 7) && names->Magic == NAME_FIND_MAGIC) {
        names->Magic = 0;
        for (ULONG i = 0; i < names->Count; i++)
            KbFree(names->Names[i]);
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

/* copies the next name into the caller's buffer, or reports the length needed. */
static BOOL give_link_name(NAME_FIND *f, LPDWORD length, PWSTR buffer)
{
    PCWSTR name = f->Names[f->Next];
    DWORD need = (DWORD)wcslen(name) + 1;
    DWORD have = *length;
    *length = need;
    if (have < need) {
        KbSetError(ERROR_MORE_DATA);
        return FALSE;
    }
    memcpy(buffer, name, need * sizeof(WCHAR));
    f->Next++;
    return TRUE;
}

/*
 * the file system only reports the name a file was opened by, so the enumeration holds one link: the path from the
 * volume root.
 */
HANDLE WINAPI FindFirstFileNameW(LPCWSTR path, DWORD flags, LPDWORD length, PWSTR buffer)
{
    if (flags || !path || !length || (*length && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return INVALID_HANDLE_VALUE;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    WCHAR full[MAX_PATH];
    DWORD full_length = GetFullPathNameW(path, MAX_PATH, full, nullptr);
    if (!full_length || full_length >= MAX_PATH)
        return INVALID_HANDLE_VALUE;
    PCWSTR link = full;
    if (full[0] && full[1] == ':')
        link = full + 2;
    NAME_FIND *f = KbAllocZero(sizeof(*f));
    PWSTR copy = KbAlloc((wcslen(link) + 1) * sizeof(WCHAR));
    if (!f || !copy) {
        KbFree(f);
        KbFree(copy);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    wcscpy(copy, link);
    f->Magic = NAME_FIND_MAGIC;
    f->Count = 1;
    f->Names[0] = copy;
    if (!give_link_name(f, length, buffer)) {
        FindClose(f);
        return INVALID_HANDLE_VALUE;
    }
    return f;
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
    if (f->Next >= f->Count) {
        KbSetError(ERROR_HANDLE_EOF);
        return FALSE;
    }
    return give_link_name(f, length, buffer);
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
