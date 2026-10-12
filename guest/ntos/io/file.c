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
    if (KeGetPreviousMode() == UserMode)
        return KiCopyToUser(user, iosb, sizeof(*iosb));
    *user = *iosb;
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
    IO_STATUS_BLOCK local = {0};
    HANDLE h = NULL;
    status = IopCreateFile(&h, access, &oa, &local, file_attributes, share, disposition, options, mode);
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
                        ea_length, 0, NULL, 0);
}

NTSTATUS NTAPI NtOpenFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                          ULONG share, ULONG options)
{
    return IoCreateFile(handle, access, attributes, iosb, NULL, 0, share, FILE_OPEN, options, NULL, 0, 0, NULL, 0);
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

static NTSTATUS reference_file(HANDLE handle, PFILE_OBJECT *file)
{
    return ObReferenceHandle(handle, IoFileObjectTypeObject, KeGetPreviousMode(), (PVOID *)file, NULL);
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
    if (use_position)
        offset = file->CurrentByteOffset;

    PUCHAR bounce = NULL;
    if (mode == UserMode && length) {
        bounce = ExAllocatePool2(POOL_FLAG_NON_PAGED, length < IO_BOUNCE_SIZE ? length : IO_BOUNCE_SIZE, KI_TAG('I', 'o', 'B', 'n'));
        if (!bounce) {
            ObDereferenceObject(file);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    ULONG done = 0;
    IO_STATUS_BLOCK io = {0};
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
    io.Status = status;
    io.Information = done;
    store_iosb(iosb, &io);
    if (event) {
        PKEVENT ev;
        if (NT_SUCCESS(ObReferenceHandle(event, ExEventType, mode, (PVOID *)&ev, NULL))) {
            KeSetEvent(ev, 0, FALSE);
            ObDereferenceObject(ev);
        }
    }
    KeSetEvent(&file->Event, 0, FALSE);
    /* errors detected before the request started post nothing, like windows. */
    if (!NT_ERROR(status) || done) {
        if (apc_routine)
            IopQueueCompletionApc(PsCurrentThread(), apc_routine, apc_context, iosb);
        else
            IopPostCompletion(file, apc_context, status, done);
    }
    ObDereferenceObject(file);
    return status;
}

NTSTATUS NTAPI NtReadFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                          PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key)
{
    return rw_file(FALSE, handle, event, (PVOID)apc, apc_context, iosb, buffer, length, offset);
}

NTSTATUS NTAPI NtWriteFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                           PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key)
{
    return rw_file(TRUE, handle, event, (PVOID)apc, apc_context, iosb, buffer, length, offset);
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
    ZW_WRAP(rw_file(FALSE, handle, event, NULL, NULL, iosb, buffer, length, offset));
}

NTSTATUS NTAPI ZwWriteFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                           PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key)
{
    ZW_WRAP(rw_file(TRUE, handle, event, NULL, NULL, iosb, buffer, length, offset));
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

NTSTATUS NTAPI NtQueryInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length,
                                      FILE_INFORMATION_CLASS klass)
{
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
    IO_STATUS_BLOCK io = {0};
    if (NT_SUCCESS(status)) {
        if (klass == FilePositionInformation && length >= sizeof(FILE_POSITION_INFORMATION)) {
            file->CurrentByteOffset = ((PFILE_POSITION_INFORMATION)buf)->CurrentByteOffset;
        } else if (klass == FileCompletionInformation) {
            /* port and key, handled by the i/o manager itself. */
            status = length >= 2 * sizeof(PVOID) ? IopSetCompletion(file, ((HANDLE *)buf)[0], ((PVOID *)buf)[1])
                                                 : STATUS_INFO_LENGTH_MISMATCH;
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
    status = IopCreateFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &io, 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT,
                           KernelMode);
    if (NT_SUCCESS(status)) {
        PFILE_OBJECT file;
        status = ObReferenceHandle(h, IoFileObjectTypeObject, KernelMode, (PVOID *)&file, NULL);
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
    UNICODE_STRING pat = {0};
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
    params.Parameters.QueryDirectory.FileName = pattern ? &pat : NULL;
    params.Parameters.QueryDirectory.FileInformationClass = klass;
    IO_STATUS_BLOCK io;
    status = IopSynchronousRequest(file, IRP_MJ_DIRECTORY_CONTROL, IRP_MN_QUERY_DIRECTORY, buf, length, &params, &io);
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
    IO_STATUS_BLOCK io = {0};
    store_iosb(iosb, &io);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtDeviceIoControlFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                     PIO_STATUS_BLOCK iosb, ULONG code, PVOID input, ULONG in_len, PVOID output,
                                     ULONG out_len)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    ULONG size = in_len > out_len ? in_len : out_len;
    PUCHAR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, KI_TAG('I', 'o', 'C', 't'));
    if (!buf) {
        ObDereferenceObject(file);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (in_len)
        status = KiCopyFromUser(buf, input, in_len);
    IO_STATUS_BLOCK io = {0};
    if (NT_SUCCESS(status)) {
        IO_STACK_LOCATION params;
        memset(&params, 0, sizeof(params));
        params.Parameters.DeviceIoControl.IoControlCode = code;
        params.Parameters.DeviceIoControl.InputBufferLength = in_len;
        params.Parameters.DeviceIoControl.OutputBufferLength = out_len;
        status = IopSynchronousRequest(file, IRP_MJ_DEVICE_CONTROL, 0, buf, size, &params, &io);
        if (NT_SUCCESS(status) && out_len && io.Information)
            KiCopyToUser(output, buf, (ULONG)(io.Information < out_len ? io.Information : out_len));
    }
    io.Status = status;
    store_iosb(iosb, &io);
    if (event) {
        PKEVENT ev;
        if (NT_SUCCESS(ObReferenceHandle(event, ExEventType, KeGetPreviousMode(), (PVOID *)&ev, NULL))) {
            KeSetEvent(ev, 0, FALSE);
            ObDereferenceObject(ev);
        }
    }
    if (!NT_ERROR(status))
        IopPostCompletion(file, apc_context, status, io.Information);
    ExFreePool(buf);
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
    if (IopIsNamedPipe(file))
        status = NpfsFsControl(file, event, (PVOID)apc, apc_context, iosb, code, input, in_len, output, out_len);
    else
        status = STATUS_INVALID_DEVICE_REQUEST;
    ObDereferenceObject(file);
    return status;
}

/* only named pipes have requests that stay pending; everything else completed already. */
NTSTATUS NTAPI NtCancelIoFile(HANDLE handle, PIO_STATUS_BLOCK iosb)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    if (IopIsNamedPipe(file))
        NpfsCancel(file, PsCurrentProcess(), FALSE, NULL);
    ObDereferenceObject(file);
    IO_STATUS_BLOCK io = {0};
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
    status = IopIsNamedPipe(file) ? NpfsCancel(file, PsCurrentProcess(), request != NULL, request) : STATUS_NOT_FOUND;
    ObDereferenceObject(file);
    IO_STATUS_BLOCK io = {{status}, 0};
    store_iosb(iosb, &io);
    return status;
}
