/*
 * file system services: NtCreateFile, NtReadFile and friends.
 */
#include "../ntos.h"

#define FILE_USE_FILE_POINTER_POSITION 0xFFFFFFFE
#define FILE_WRITE_TO_END_OF_FILE 0xFFFFFFFF
#define IO_BOUNCE_SIZE (256 * 1024)

static NTSTATUS store_iosb(PIO_STATUS_BLOCK user, const IO_STATUS_BLOCK *iosb)
{
    if (!user)
        return STATUS_SUCCESS;
    ULONG64 image[2];
    PVOID address;
    ULONG length = IopIosbImage(user, iosb->Status, iosb->Information, &address, image);
    if (KeGetPreviousMode() == UserMode)
        return KiCopyToUser(address, image, length);
    memcpy(address, image, length);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI IoCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                            PLARGE_INTEGER allocation, ULONG file_attributes, ULONG share, ULONG disposition,
                            ULONG options, PVOID ea, ULONG ea_length, ULONG create_file_type, PVOID parameters,
                            ULONG flags)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    if (!oa.ObjectName) {
        KiFreeCapturedString(&name);
        return STATUS_OBJECT_NAME_INVALID;
    }
    IO_STATUS_BLOCK local = {};
    HANDLE h = nullptr;
    /* the security quality of service asked of the file, which a named pipe's server impersonates by. */
    PETHREAD thread = PsCurrentThread();
    thread->CreateQos = 0;
    if (oa.SecurityQualityOfService) {
        ULONG qos[3];
        if (mode == UserMode ? NT_SUCCESS(KiCopyFromUser(qos, oa.SecurityQualityOfService, sizeof(qos)))
                             : (memcpy(qos, oa.SecurityQualityOfService, sizeof(qos)), TRUE)) {
            if (qos[0] == sizeof(qos) && qos[1] <= 3) {
                thread->CreateQos = (UCHAR)(qos[1] + 1);
                thread->CreateQosEffective = ((PUCHAR)&qos[2])[1] != 0;
            }
        }
    }
    status = IopCreateFile(&h, access, &oa, &local, file_attributes, share, disposition, options, mode);
    thread->CreateQos = 0;
    KiFreeCapturedString(&name);
    if (!NT_SUCCESS(status)) {
        local.Status = status;
        store_iosb(iosb, &local);
        return status;
    }
    local.Status = status;
    store_iosb(iosb, &local);
    return ObStoreHandle(handle, h);
}

NTSTATUS NTAPI NtCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                            PLARGE_INTEGER allocation, ULONG file_attributes, ULONG share, ULONG disposition,
                            ULONG options, PVOID ea, ULONG ea_length)
{
    return IoCreateFile(handle, access, attributes, iosb, allocation, file_attributes, share, disposition, options, ea,
                        ea_length, 0, nullptr, 0);
}

NTSTATUS NTAPI NtOpenFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                          ULONG share, ULONG options)
{
    return IoCreateFile(handle, access, attributes, iosb, nullptr, 0, share, FILE_OPEN, options, nullptr, 0, 0, nullptr, 0);
}

#define ZW_WRAP(call)                                                                                             \
    PKTHREAD t_ = KiCurrentThread();                                                                              \
    KPROCESSOR_MODE old_ = t_->PreviousMode;                                                                      \
    t_->PreviousMode = KernelMode;                                                                                \
    NTSTATUS st_ = call;                                                                                          \
    t_->PreviousMode = old_;                                                                                      \
    return st_

NTSTATUS NTAPI ZwCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                            PLARGE_INTEGER allocation, ULONG file_attributes, ULONG share, ULONG disposition,
                            ULONG options, PVOID ea, ULONG ea_length)
{
    ZW_WRAP(NtCreateFile(handle, access, attributes, iosb, allocation, file_attributes, share, disposition, options, ea,
                         ea_length));
}

NTSTATUS NTAPI ZwOpenFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                          ULONG share, ULONG options)
{
    ZW_WRAP(NtOpenFile(handle, access, attributes, iosb, share, options));
}

/*
 * a file opened for synchronous i/o is used by one request at a time, as on windows: the position that the
 * requests share and the state of an enumeration would not survive two at once. the requests wait for the
 * file in turn. a request waits and the kernel lock goes to the others while it talks to the host, so
 * without this a second request on the same handle could start in the middle of the first.
 */
static void lock_file(PFILE_OBJECT file)
{
    if (!(file->Flags & FO_SYNCHRONOUS_IO))
        return;
    while (__atomic_exchange_n(&file->Busy, 1, __ATOMIC_ACQUIRE)) {
        __atomic_add_fetch(&file->Waiters, 1, __ATOMIC_RELAXED);
        KeWaitForSingleObject(&file->Lock, Executive, KernelMode, FALSE, nullptr);
        __atomic_sub_fetch(&file->Waiters, 1, __ATOMIC_RELAXED);
    }
}

static void unlock_file(PFILE_OBJECT file)
{
    if (!(file->Flags & FO_SYNCHRONOUS_IO))
        return;
    __atomic_store_n(&file->Busy, 0, __ATOMIC_RELEASE);
    if (__atomic_load_n(&file->Waiters, __ATOMIC_RELAXED))
        KeSetEvent(&file->Lock, IO_NO_INCREMENT, FALSE);
}

static NTSTATUS reference_file(HANDLE handle, PFILE_OBJECT *file)
{
    return ObReferenceHandle(handle, IoFileObjectTypeObject, KeGetPreviousMode(), (PVOID *)file, nullptr);
}

static NTSTATUS rw_file(BOOLEAN write, HANDLE handle, HANDLE event, PVOID apc_routine, PVOID apc_context,
                        PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, PLARGE_INTEGER byte_offset)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    if (IopIsNamedPipe(file)) {
        status = NpfsReadWrite(file, write, event, apc_routine, apc_context, iosb, buffer, length);
        ObDereferenceObject(file);
        return status;
    }
    if (IopIsAfd(file)) {
        status = AfdReadWrite(file, write, event, apc_routine, apc_context, iosb, buffer, length);
        ObDereferenceObject(file);
        return status;
    }
    if (IopIsPad(file)) {
        status = PadReadWrite(file, write, event, apc_routine, apc_context, iosb, buffer, length);
        ObDereferenceObject(file);
        return status;
    }
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    LARGE_INTEGER offset;
    offset.QuadPart = -1;
    if (byte_offset) {
        if (mode == UserMode)
            status = KiCopyFromUser(&offset, byte_offset, sizeof(offset));
        else
            offset = *byte_offset;
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(file);
            return status;
        }
    }
    BOOLEAN use_position = offset.QuadPart == -1 || (offset.LowPart == FILE_USE_FILE_POINTER_POSITION && offset.HighPart == -1);
    /* a write goes to the end of the file when the handle only appends, or when it names the offset of the end. */
    BOOLEAN to_end = write && ((file->Flags & FO_APPEND_ONLY) ||
                               (byte_offset && offset.LowPart == FILE_WRITE_TO_END_OF_FILE && offset.HighPart == -1));
    lock_file(file);
    if (to_end) {
        ULONG64 size = 0;
        /* a device that has no size is written where it is. */
        offset.QuadPart = NT_SUCCESS(IoQueryFileSize(file, &size)) ? (LONGLONG)size : file->CurrentByteOffset.QuadPart;
    } else if (use_position) {
        offset = file->CurrentByteOffset;
    }

    PUCHAR bounce = nullptr;
    if (mode == UserMode && length) {
        bounce = ExAllocatePool2(POOL_FLAG_NON_PAGED, length < IO_BOUNCE_SIZE ? length : IO_BOUNCE_SIZE, KI_TAG('I', 'o', 'B', 'n'));
        if (!bounce) {
            unlock_file(file);
            ObDereferenceObject(file);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    ULONG done = 0;
    IO_STATUS_BLOCK io = {};
    while (done < length || length == 0) {
        ULONG chunk = length - done;
        if (bounce && chunk > IO_BOUNCE_SIZE)
            chunk = IO_BOUNCE_SIZE;
        PVOID buf = bounce ? bounce : (PUCHAR)buffer + done;
        if (write && bounce) {
            status = KiCopyFromUser(bounce, (PUCHAR)buffer + done, chunk);
            if (!NT_SUCCESS(status))
                break;
        }
        IO_STACK_LOCATION params;
        memset(&params, 0, sizeof(params));
        params.Parameters.Read.Length = chunk;
        params.Parameters.Read.ByteOffset.QuadPart = offset.QuadPart + done;
        status = IopSynchronousRequest(file, write ? IRP_MJ_WRITE : IRP_MJ_READ, 0, buf, chunk, &params, &io);
        if (!NT_SUCCESS(status))
            break;
        ULONG got = (ULONG)io.Information;
        if (!write && bounce && got) {
            status = KiCopyToUser((PUCHAR)buffer + done, bounce, got);
            if (!NT_SUCCESS(status))
                break;
        }
        done += got;
        if (got < chunk || length == 0)
            break;
    }
    if (bounce)
        ExFreePool(bounce);
    if (status == STATUS_END_OF_FILE && done)
        status = STATUS_SUCCESS;
    if (NT_SUCCESS(status) && (use_position || (file->Flags & FO_SYNCHRONOUS_IO)))
        file->CurrentByteOffset.QuadPart = offset.QuadPart + done;
    unlock_file(file);
    io.Status = status;
    io.Information = done;
    store_iosb(iosb, &io);
    if (event) {
        PKEVENT ev;
        if (NT_SUCCESS(ObReferenceHandle(event, ExEventType, mode, (PVOID *)&ev, nullptr))) {
            KeSetEvent(ev, 0, FALSE);
            ObDereferenceObject(ev);
        }
    }
    IopSignalFile(file);
    /* errors detected before the request started post nothing, like windows. */
    if (!NT_ERROR(status) || done) {
        if (apc_routine)
            IopQueueCompletionApc(PsCurrentThread(), apc_routine, apc_context, iosb);
        else
            IopPostCompletion(file, apc_context, status, done, TRUE);
    }
    ObDereferenceObject(file);
    return status;
}

NTSTATUS NTAPI NtReadFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                          PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key)
{
    PVOID routine = (PVOID)apc;
    if (KeGetPreviousMode() == UserMode)
        IopTakeIosb32(&routine, &iosb);
    return rw_file(FALSE, handle, event, routine, apc_context, iosb, buffer, length, offset);
}

NTSTATUS NTAPI NtWriteFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                           PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key)
{
    PVOID routine = (PVOID)apc;
    if (KeGetPreviousMode() == UserMode)
        IopTakeIosb32(&routine, &iosb);
    return rw_file(TRUE, handle, event, routine, apc_context, iosb, buffer, length, offset);
}

NTSTATUS NTAPI ZwQueryDirectoryFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                    PIO_STATUS_BLOCK iosb, PVOID info, ULONG length, FILE_INFORMATION_CLASS klass,
                                    BOOLEAN single, PUNICODE_STRING pattern, BOOLEAN restart)
{
    ZW_WRAP(NtQueryDirectoryFile(handle, event, apc, apc_context, iosb, info, length, klass, single, pattern, restart));
}

NTSTATUS NTAPI ZwReadFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                          PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key)
{
    ZW_WRAP(rw_file(FALSE, handle, event, nullptr, nullptr, iosb, buffer, length, offset));
}

NTSTATUS NTAPI ZwWriteFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                           PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key)
{
    ZW_WRAP(rw_file(TRUE, handle, event, nullptr, nullptr, iosb, buffer, length, offset));
}

static NTSTATUS query_file(PFILE_OBJECT file, PVOID info, ULONG length, FILE_INFORMATION_CLASS klass, PULONG written)
{
    if (klass == FilePositionInformation) {
        if (length < sizeof(FILE_POSITION_INFORMATION))
            return STATUS_INFO_LENGTH_MISMATCH;
        ((PFILE_POSITION_INFORMATION)info)->CurrentByteOffset = file->CurrentByteOffset;
        *written = sizeof(FILE_POSITION_INFORMATION);
        return STATUS_SUCCESS;
    }
    if (klass == FileModeInformation) {
        if (length < sizeof(ULONG))
            return STATUS_INFO_LENGTH_MISMATCH;
        *(PULONG)info = (file->Flags & FO_SYNCHRONOUS_IO) ? FILE_SYNCHRONOUS_IO_NONALERT : 0;
        *written = sizeof(ULONG);
        return STATUS_SUCCESS;
    }
    IO_STACK_LOCATION params;
    memset(&params, 0, sizeof(params));
    params.Parameters.QueryFile.Length = length;
    params.Parameters.QueryFile.FileInformationClass = klass;
    IO_STATUS_BLOCK io;
    NTSTATUS status = IopSynchronousRequest(file, IRP_MJ_QUERY_INFORMATION, 0, info, length, &params, &io);
    *written = (ULONG)io.Information;
    return status;
}

/* the access the handle grants, its generic rights mapped to a file's: the i/o manager's to answer, not a driver's. */
static NTSTATUS query_access(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length)
{
    PFILE_OBJECT file;
    ACCESS_MASK granted = 0;
    NTSTATUS status = ObReferenceHandle(handle, IoFileObjectTypeObject, KeGetPreviousMode(), (PVOID *)&file, &granted);
    if (!NT_SUCCESS(status))
        return status;
    ObDereferenceObject(file);
    if (granted & (GENERIC_ALL | MAXIMUM_ALLOWED))
        granted |= FILE_ALL_ACCESS;
    if (granted & GENERIC_READ)
        granted |= FILE_GENERIC_READ;
    if (granted & GENERIC_WRITE)
        granted |= FILE_GENERIC_WRITE;
    if (granted & GENERIC_EXECUTE)
        granted |= FILE_GENERIC_EXECUTE;
    granted &= ~(GENERIC_ALL | GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | MAXIMUM_ALLOWED);
    if (length < sizeof(granted))
        return STATUS_INFO_LENGTH_MISMATCH;
    status = KeGetPreviousMode() == UserMode ? KiCopyToUser(info, &granted, sizeof(granted))
                                             : (memcpy(info, &granted, sizeof(granted)), STATUS_SUCCESS);
    IO_STATUS_BLOCK io = {.Status = status, .Information = sizeof(granted)};
    store_iosb(iosb, &io);
    return status;
}

NTSTATUS NTAPI NtQueryInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length,
                                      FILE_INFORMATION_CLASS klass)
{
    if (klass == FileAccessInformation)
        return query_access(handle, iosb, info, length);
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    PUCHAR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, length ? length : 1, KI_TAG('I', 'o', 'Q', 'i'));
    if (!buf) {
        ObDereferenceObject(file);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ULONG written = 0;
    status = query_file(file, buf, length, klass, &written);
    if (NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW) {
        if (KeGetPreviousMode() == UserMode) {
            NTSTATUS st = KiCopyToUser(info, buf, written);
            if (!NT_SUCCESS(st))
                status = st;
        } else {
            memcpy(info, buf, written);
        }
    }
    IO_STATUS_BLOCK io = {.Status = status, .Information = written};
    store_iosb(iosb, &io);
    ExFreePool(buf);
    ObDereferenceObject(file);
    return status;
}

NTSTATUS NTAPI ZwQueryInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length,
                                      FILE_INFORMATION_CLASS klass)
{
    ZW_WRAP(NtQueryInformationFile(handle, iosb, info, length, klass));
}

/*
 * a rename names the new place of a file, and the file system is given the directory it is in, opened here:
 * the new name up to its last backslash is that directory, and the rest is the name in it. the names are
 * those of a handle of the same file system, a full path, as nothing but ntdll's callers set them.
 */
static NTSTATUS rename_file(PFILE_OBJECT file, PUCHAR buf, ULONG length, FILE_INFORMATION_CLASS klass)
{
    const ULONG name_offset = 20;
    if (length < name_offset)
        return STATUS_INFO_LENGTH_MISMATCH;
    ULONG name_length = *(PULONG)(buf + 16);
    if ((name_length & 1) || name_length > length - name_offset)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (*(HANDLE *)(buf + 8))
        return STATUS_NOT_IMPLEMENTED;
    BOOLEAN replace = klass == FileRenameInformationEx ? (*(PULONG)buf & 1) != 0 : buf[0] != 0;
    PWCHAR name = (PWCHAR)(buf + name_offset);
    ULONG chars = name_length / sizeof(WCHAR), slash = chars;
    while (slash && name[slash - 1] != '\\')
        slash--;
    /* a name with no directory part, or one that ends in a backslash, names nothing. */
    if (slash < 2 || slash == chars)
        return STATUS_OBJECT_NAME_INVALID;
    UNICODE_STRING parent = {(USHORT)((slash - 1) * sizeof(WCHAR)), (USHORT)((slash - 1) * sizeof(WCHAR)), name};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &parent, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    HANDLE h;
    IO_STATUS_BLOCK io;
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    t->PreviousMode = KernelMode;
    NTSTATUS status = IopCreateFile(&h, FILE_TRAVERSE | SYNCHRONIZE, &oa, &io, 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                    FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, KernelMode);
    t->PreviousMode = old;
    if (!NT_SUCCESS(status))
        return status == STATUS_OBJECT_NAME_NOT_FOUND ? STATUS_OBJECT_PATH_NOT_FOUND : status;
    PFILE_OBJECT directory;
    status = ObReferenceHandle(h, IoFileObjectTypeObject, KernelMode, (PVOID *)&directory, nullptr);
    if (NT_SUCCESS(status)) {
        if (directory->DeviceObject != file->DeviceObject) {
            status = STATUS_NOT_SAME_DEVICE;
        } else {
            IO_STACK_LOCATION params;
            memset(&params, 0, sizeof(params));
            params.Parameters.SetFile.Length = length;
            params.Parameters.SetFile.FileInformationClass = klass;
            params.Parameters.SetFile.FileObject = directory;
            params.Parameters.SetFile.ReplaceIfExists = replace;
            IO_STATUS_BLOCK ignored = {};
            status = IopSynchronousRequest(file, IRP_MJ_SET_INFORMATION, 0, buf, length, &params, &ignored);
        }
        ObDereferenceObject(directory);
    }
    ObCloseHandle(h, KernelMode);
    return status;
}

/* the kernel's own rename of an open file to a full path, replacing a file of that name when asked. */
NTSTATUS IoRenameFileObject(PFILE_OBJECT file, PCWSTR new_name, BOOLEAN replace)
{
    ULONG name_length = (ULONG)wcslen(new_name) * sizeof(WCHAR), size = 20 + name_length;
    PUCHAR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('I', 'o', 'R', 'n'));
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(buf, 0, 20);
    buf[0] = replace;
    *(PULONG)(buf + 16) = name_length;
    memcpy(buf + 20, new_name, name_length);
    NTSTATUS status = rename_file(file, buf, size, FileRenameInformation);
    ExFreePool(buf);
    return status;
}

NTSTATUS NTAPI NtSetInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length,
                                    FILE_INFORMATION_CLASS klass)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    PUCHAR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, length ? length : 1, KI_TAG('I', 'o', 'S', 'i'));
    if (!buf) {
        ObDereferenceObject(file);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (KeGetPreviousMode() == UserMode)
        status = KiCopyFromUser(buf, info, length);
    else
        memcpy(buf, info, length);
    IO_STATUS_BLOCK io = {};
    if (NT_SUCCESS(status)) {
        if (klass == FilePositionInformation && length >= sizeof(FILE_POSITION_INFORMATION)) {
            file->CurrentByteOffset = ((PFILE_POSITION_INFORMATION)buf)->CurrentByteOffset;
        } else if (klass == FileIoCompletionNotificationInformation) {
            /* the modes only add up, as on windows. */
            ULONG flags = length >= sizeof(ULONG) ? *(PULONG)buf : 0;
            if (length < sizeof(ULONG))
                status = STATUS_INFO_LENGTH_MISMATCH;
            else if (flags & ~7u)
                status = STATUS_INVALID_PARAMETER;
            else
                file->Flags |= (flags & 1 ? FO_SKIP_COMPLETION_PORT : 0) | (flags & 2 ? FO_SKIP_SET_EVENT : 0);
        } else if (klass == FileCompletionInformation) {
            /* port and key, handled by the i/o manager itself. */
            status = length >= 2 * sizeof(PVOID) ? IopSetCompletion(file, ((HANDLE *)buf)[0], ((PVOID *)buf)[1])
                                                 : STATUS_INFO_LENGTH_MISMATCH;
        } else if ((klass == FileRenameInformation || klass == FileRenameInformationEx) && file->DeviceObject &&
                   file->DeviceObject->DeviceType == FILE_DEVICE_DISK_FILE_SYSTEM) {
            status = rename_file(file, buf, length, klass);
        } else {
            IO_STACK_LOCATION params;
            memset(&params, 0, sizeof(params));
            params.Parameters.SetFile.Length = length;
            params.Parameters.SetFile.FileInformationClass = klass;
            status = IopSynchronousRequest(file, IRP_MJ_SET_INFORMATION, 0, buf, length, &params, &io);
        }
    }
    io.Status = status;
    store_iosb(iosb, &io);
    ExFreePool(buf);
    ObDereferenceObject(file);
    return status;
}

/* opens the named file just long enough to ask its file system. */
static NTSTATUS query_by_name(POBJECT_ATTRIBUTES attributes, PVOID info, ULONG length, FILE_INFORMATION_CLASS klass)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    HANDLE h;
    IO_STATUS_BLOCK io;
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    oa.Attributes |= OBJ_KERNEL_HANDLE;
    t->PreviousMode = KernelMode;
    /* a name only asked about is not opened on the host: the open gives the attributes. */
    status = IopCreateFileEx(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &io, 0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                             FILE_SYNCHRONOUS_IO_NONALERT, KernelMode, TRUE);
    if (NT_SUCCESS(status)) {
        PFILE_OBJECT file;
        status = ObReferenceHandle(h, IoFileObjectTypeObject, KernelMode, (PVOID *)&file, nullptr);
        if (NT_SUCCESS(status)) {
            ULONG written;
            status = query_file(file, info, length, klass, &written);
            ObDereferenceObject(file);
        }
        ObCloseHandle(h, KernelMode);
    }
    t->PreviousMode = old;
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtQueryAttributesFile(POBJECT_ATTRIBUTES attributes, PFILE_BASIC_INFORMATION info)
{
    FILE_BASIC_INFORMATION basic;
    NTSTATUS status = query_by_name(attributes, &basic, sizeof(basic), FileBasicInformation);
    if (NT_SUCCESS(status))
        status = KeGetPreviousMode() == UserMode ? KiCopyToUser(info, &basic, sizeof(basic))
                                                 : (memcpy(info, &basic, sizeof(basic)), STATUS_SUCCESS);
    return status;
}

NTSTATUS NTAPI NtQueryFullAttributesFile(POBJECT_ATTRIBUTES attributes, PFILE_NETWORK_OPEN_INFORMATION info)
{
    FILE_NETWORK_OPEN_INFORMATION net;
    NTSTATUS status = query_by_name(attributes, &net, sizeof(net), FileNetworkOpenInformation);
    if (NT_SUCCESS(status))
        status = KeGetPreviousMode() == UserMode ? KiCopyToUser(info, &net, sizeof(net))
                                                 : (memcpy(info, &net, sizeof(net)), STATUS_SUCCESS);
    return status;
}

NTSTATUS NTAPI NtQueryDirectoryFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                    PIO_STATUS_BLOCK iosb, PVOID info, ULONG length, FILE_INFORMATION_CLASS klass,
                                    BOOLEAN single, PUNICODE_STRING pattern, BOOLEAN restart)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    UNICODE_STRING pat = {};
    if (pattern) {
        status = KiCaptureUnicodeString(&pat, pattern);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(file);
            return status;
        }
    }
    PUCHAR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, length ? length : 1, KI_TAG('I', 'o', 'Q', 'd'));
    if (!buf) {
        KiFreeCapturedString(&pat);
        ObDereferenceObject(file);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    IO_STACK_LOCATION params;
    memset(&params, 0, sizeof(params));
    params.Flags = (single ? SL_RETURN_SINGLE_ENTRY : 0) | (restart ? SL_RESTART_SCAN : 0);
    params.Parameters.QueryDirectory.Length = length;
    params.Parameters.QueryDirectory.FileName = pattern ? &pat : nullptr;
    params.Parameters.QueryDirectory.FileInformationClass = klass;
    IO_STATUS_BLOCK io;
    lock_file(file);
    status = IopSynchronousRequest(file, IRP_MJ_DIRECTORY_CONTROL, IRP_MN_QUERY_DIRECTORY, buf, length, &params, &io);
    unlock_file(file);
    if (NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW) {
        NTSTATUS st = KeGetPreviousMode() == UserMode ? KiCopyToUser(info, buf, (ULONG)io.Information)
                                                      : (memcpy(info, buf, io.Information), STATUS_SUCCESS);
        if (!NT_SUCCESS(st))
            status = st;
    }
    io.Status = status;
    store_iosb(iosb, &io);
    ExFreePool(buf);
    KiFreeCapturedString(&pat);
    ObDereferenceObject(file);
    return status;
}

NTSTATUS NTAPI NtQueryDirectoryFileEx(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                      PIO_STATUS_BLOCK iosb, PVOID info, ULONG length, FILE_INFORMATION_CLASS klass,
                                      ULONG flags, PUNICODE_STRING pattern)
{
    return NtQueryDirectoryFile(handle, event, apc, apc_context, iosb, info, length, klass, (flags & 2) != 0,
                                pattern, (flags & 1) != 0);
}

NTSTATUS NTAPI NtQueryVolumeInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length,
                                            FS_INFORMATION_CLASS klass)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    PUCHAR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, length ? length : 1, KI_TAG('I', 'o', 'Q', 'v'));
    if (!buf) {
        ObDereferenceObject(file);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    IO_STACK_LOCATION params;
    memset(&params, 0, sizeof(params));
    params.Parameters.QueryVolume.Length = length;
    params.Parameters.QueryVolume.FsInformationClass = klass;
    IO_STATUS_BLOCK io;
    status = IopSynchronousRequest(file, IRP_MJ_QUERY_VOLUME_INFORMATION, 0, buf, length, &params, &io);
    /* the device's type and characteristics, for drivers that leave the question to the i/o manager. */
    if (status == STATUS_INVALID_DEVICE_REQUEST && klass == FileFsDeviceInformation && file->DeviceObject) {
        FILE_FS_DEVICE_INFORMATION d = {file->DeviceObject->DeviceType, file->DeviceObject->Characteristics};
        status = length < sizeof(d) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_SUCCESS;
        if (NT_SUCCESS(status)) {
            memcpy(buf, &d, sizeof(d));
            io.Information = sizeof(d);
        }
    }
    if (NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW)
        KiCopyToUser(info, buf, (ULONG)io.Information);
    io.Status = status;
    store_iosb(iosb, &io);
    ExFreePool(buf);
    ObDereferenceObject(file);
    return status;
}

NTSTATUS NTAPI NtFlushBuffersFile(HANDLE handle, PIO_STATUS_BLOCK iosb)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    IO_STATUS_BLOCK io = {};
    status = IopSynchronousRequest(file, IRP_MJ_FLUSH_BUFFERS, 0, nullptr, 0, nullptr, &io);
    ObDereferenceObject(file);
    io.Status = status;
    NTSTATUS stored = store_iosb(iosb, &io);
    return NT_SUCCESS(status) ? stored : status;
}

/* a device or file system control with buffered input and output, completed before it returns. */
static NTSTATUS buffered_control(PFILE_OBJECT file, UCHAR major, HANDLE event, PVOID apc_context,
                                 PIO_STATUS_BLOCK iosb, ULONG code, PVOID input, ULONG in_len, PVOID output,
                                 ULONG out_len)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG size = in_len > out_len ? in_len : out_len;
    PUCHAR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, KI_TAG('I', 'o', 'C', 't'));
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (in_len)
        status = KiCopyFromUser(buf, input, in_len);
    IO_STATUS_BLOCK io = {};
    if (NT_SUCCESS(status)) {
        IO_STACK_LOCATION params;
        memset(&params, 0, sizeof(params));
        params.Parameters.DeviceIoControl.IoControlCode = code;
        params.Parameters.DeviceIoControl.InputBufferLength = in_len;
        params.Parameters.DeviceIoControl.OutputBufferLength = out_len;
        status = IopSynchronousRequest(file, major, 0, buf, size, &params, &io);
        /* an overflow is a partial answer, copied like a whole one. */
        if ((NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW) && out_len && io.Information)
            KiCopyToUser(output, buf, (ULONG)(io.Information < out_len ? io.Information : out_len));
    }
    io.Status = status;
    store_iosb(iosb, &io);
    if (event) {
        PKEVENT ev;
        if (NT_SUCCESS(ObReferenceHandle(event, ExEventType, KeGetPreviousMode(), (PVOID *)&ev, nullptr))) {
            KeSetEvent(ev, 0, FALSE);
            ObDereferenceObject(ev);
        }
    }
    if (!NT_ERROR(status))
        IopPostCompletion(file, apc_context, status, io.Information, TRUE);
    ExFreePool(buf);
    return status;
}

NTSTATUS NTAPI NtDeviceIoControlFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                     PIO_STATUS_BLOCK iosb, ULONG code, PVOID input, ULONG in_len, PVOID output,
                                     ULONG out_len)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    PVOID routine = (PVOID)apc;
    if (KeGetPreviousMode() == UserMode)
        IopTakeIosb32(&routine, &iosb);
    /* sockets and game controllers read the caller's buffers themselves and may stay pending. */
    if (IopIsAfd(file))
        status = AfdDeviceControl(file, event, routine, apc_context, iosb, code, input, in_len, output, out_len);
    else if (IopIsPad(file))
        status = PadDeviceControl(file, event, routine, apc_context, iosb, code, input, in_len, output, out_len);
    else
        status = buffered_control(file, IRP_MJ_DEVICE_CONTROL, event, apc_context, iosb, code, input, in_len, output,
                                  out_len);
    ObDereferenceObject(file);
    return status;
}

NTSTATUS NTAPI NtFsControlFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                               PIO_STATUS_BLOCK iosb, ULONG code, PVOID input, ULONG in_len, PVOID output,
                               ULONG out_len)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    PVOID routine = (PVOID)apc;
    if (KeGetPreviousMode() == UserMode)
        IopTakeIosb32(&routine, &iosb);
    if (IopIsNamedPipe(file))
        status = NpfsFsControl(file, event, routine, apc_context, iosb, code, input, in_len, output, out_len);
    else
        status = buffered_control(file, IRP_MJ_FILE_SYSTEM_CONTROL, event, apc_context, iosb, code, input, in_len,
                                  output, out_len);
    ObDereferenceObject(file);
    return status;
}

/* only named pipes and sockets have requests that stay pending; everything else completed already. */
NTSTATUS NTAPI NtCancelIoFile(HANDLE handle, PIO_STATUS_BLOCK iosb)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    if (IopIsNamedPipe(file))
        NpfsCancel(file, PsCurrentProcess(), FALSE, nullptr);
    else if (IopIsAfd(file))
        AfdCancel(file, PsCurrentThread(), nullptr);
    else if (IopIsPad(file))
        PadCancel(file, PsCurrentThread(), nullptr);
    else if (IopIsMimeFs(file))
        FsNotifyCancel(file, PsCurrentThread(), nullptr);
    ObDereferenceObject(file);
    IO_STATUS_BLOCK io = {};
    store_iosb(iosb, &io);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtCancelIoFileEx(HANDLE handle, PIO_STATUS_BLOCK request, PIO_STATUS_BLOCK iosb)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    /* the request is named by its i/o status block, the start of its OVERLAPPED. */
    if (IopIsNamedPipe(file))
        status = NpfsCancel(file, PsCurrentProcess(), request != nullptr, request);
    else if (IopIsAfd(file))
        status = AfdCancel(file, nullptr, request);
    else if (IopIsPad(file))
        status = PadCancel(file, nullptr, request);
    else if (IopIsMimeFs(file))
        status = FsNotifyCancel(file, nullptr, request);
    else
        status = STATUS_NOT_FOUND;
    ObDereferenceObject(file);
    IO_STATUS_BLOCK io = {{status}, 0};
    store_iosb(iosb, &io);
    return status;
}
