/*
 * file system services: NtCreateFile, NtReadFile and friends.
 */
#include "../ntos.h"

#define FILE_USE_FILE_POINTER_POSITION 0xFFFFFFFE
#define FILE_WRITE_TO_END_OF_FILE 0xFFFFFFFF
#define IO_BOUNCE_SIZE (256 * 1024)
/* the longest write at the end of a file that no other writer can get into the middle of. */
#define IO_APPEND_ATOMIC (1024 * 1024)
/* the offset of a request that writes at the end of the file, as in an irp of windows. */
#define IO_WRITE_TO_END (-1LL)
/* the longest scatter or gather request, in pages. */
#define IO_MAX_SEGMENTS (64 * 1024)

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

/* the kernel option trace-files logs the name and the status of every open by a program that fails. */
BOOLEAN IoTraceFiles;

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
    /* the security quality of service a named pipe server impersonates the client with. */
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
    PVOID ea_copy = nullptr;
    if (ea && ea_length) {
        if (ea_length > 0x10000) {
            KiFreeCapturedString(&name);
            return ((NTSTATUS)0xC0000050L);
        }
        ea_copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, ea_length, KI_TAG('I', 'o', 'E', 'a'));
        if (!ea_copy) {
            KiFreeCapturedString(&name);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        status = mode == UserMode ? KiCopyFromUser(ea_copy, ea, ea_length) : (memcpy(ea_copy, ea, ea_length), STATUS_SUCCESS);
        if (!NT_SUCCESS(status)) {
            ExFreePool(ea_copy);
            KiFreeCapturedString(&name);
            return status;
        }
    }
    PVOID outer_ea = thread->CreateEa;
    ULONG outer_ea_length = thread->CreateEaLength;
    thread->CreateEa = ea_copy;
    thread->CreateEaLength = ea_copy ? ea_length : 0;
    status = IopCreateFile(&h, access, &oa, &local, file_attributes, share, disposition, options, mode);
    if (!NT_SUCCESS(status) && IoTraceFiles && mode == UserMode) {
        char text[120];
        ULONG n = 0, chars = name.Length / sizeof(WCHAR);
        for (ULONG i = chars > 110 ? chars - 110 : 0; i < chars && n < sizeof(text) - 1; i++)
            text[n++] = name.Buffer[i] < 0x80 && name.Buffer[i] >= 0x20 ? (char)name.Buffer[i] : '?';
        text[n] = 0;
        KiLogPrintf("open of '%s' failed with %08x, %s pid %u", text, status, PsCurrentProcess()->ImageFileName,
                    (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId);
    }
    thread->CreateEa = outer_ea;
    thread->CreateEaLength = outer_ea_length;
    if (ea_copy)
        ExFreePool(ea_copy);
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
 * like windows, a synchronous i/o file serves one request at a time: the shared position and enumeration state would
 * not survive two at once. a request releases the kernel lock while it talks to the host, so without this a second
 * request on the same handle could start in the middle of the first.
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

/*
 * moves the bytes from done on between a kernel buffer and the user's: one buffer, or the pages of a scatter or gather
 * request, whose addresses are in an array.
 */
static NTSTATUS copy_user(BOOLEAN to_user, PVOID buffer, const ULONG64 *pages, ULONG done, PUCHAR kernel, ULONG count)
{
    while (count) {
        PUCHAR user = pages ? (PUCHAR)(ULONG_PTR)pages[done / PAGE_SIZE] + done % PAGE_SIZE : (PUCHAR)buffer + done;
        ULONG n = pages ? PAGE_SIZE - done % PAGE_SIZE : count;
        if (n > count)
            n = count;
        NTSTATUS status = to_user ? KiCopyToUser(user, kernel, n) : KiCopyFromUser(kernel, user, n);
        if (!NT_SUCCESS(status))
            return status;
        done += n;
        kernel += n;
        count -= n;
    }
    return STATUS_SUCCESS;
}

/* a request with pages reads or writes one buffer made of the pages, in the order of the array. */
static NTSTATUS rw_file(BOOLEAN write, HANDLE handle, HANDLE event, PVOID apc_routine, PVOID apc_context,
                        PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, PLARGE_INTEGER byte_offset, PULONG key,
                        const ULONG64 *pages)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    /* scatter and gather i/o is for files with no buffering that do not serve one request at a time. */
    if (pages && ((file->Flags & FO_SYNCHRONOUS_IO) || !(file->Flags & FO_NO_INTERMEDIATE_BUFFERING))) {
        ObDereferenceObject(file);
        return STATUS_INVALID_PARAMETER;
    }
    /* a request with no event of its own is waited for on the file, which is signaled again when this one is done. */
    if (!event && !(file->Flags & FO_SKIP_SET_EVENT))
        KeClearEvent(&file->Event);
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
    ULONG lock_key = 0;
    if (key) {
        status = mode == UserMode ? KiCopyFromUser(&lock_key, key, sizeof(lock_key)) :
                                    (lock_key = *key, STATUS_SUCCESS);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(file);
            return status;
        }
    }
    BOOLEAN use_position = offset.QuadPart == -1 || (offset.LowPart == FILE_USE_FILE_POINTER_POSITION && offset.HighPart == -1);
    /* writes go to the end of the file when the handle only appends, or when the offset names the end. */
    BOOLEAN to_end = write && ((file->Flags & FO_APPEND_ONLY) ||
                               (byte_offset && offset.LowPart == FILE_WRITE_TO_END_OF_FILE && offset.HighPart == -1));
    /*
     * the file system finds the end of the file itself, in the same step as the write, because the size read here
     * would be out of date when another writer got in between. the others get the size now.
     */
    BOOLEAN at_end = to_end && IopIsMimeFs(file);
    lock_file(file);
    if (at_end) {
        ULONG64 size = 0;
        offset.QuadPart = FsLocksHeld(file) && NT_SUCCESS(IoQueryFileSize(file, &size)) ? (LONGLONG)size : 0;
    } else if (to_end) {
        ULONG64 size = 0;
        /* a device with no size is written at its current position. */
        offset.QuadPart = NT_SUCCESS(IoQueryFileSize(file, &size)) ? (LONGLONG)size : file->CurrentByteOffset.QuadPart;
    } else if (use_position) {
        offset = file->CurrentByteOffset;
    }

    status = FsLocksCheck(file, write, offset.QuadPart, length, lock_key);
    if (!NT_SUCCESS(status)) {
        unlock_file(file);
        IO_STATUS_BLOCK denied = {{status}, 0};
        store_iosb(iosb, &denied);
        ObDereferenceObject(file);
        return status;
    }
    ULONG done = 0;
    IO_STATUS_BLOCK io = {};
    if (!pages && IopIsLoadedDriverDevice(file)) {
        status = IopDriverReadWrite(file, write, buffer, length, offset.QuadPart, lock_key, mode, &io);
        done = (ULONG)io.Information;
    } else {
        PUCHAR bounce = nullptr;
        ULONG bounce_size = at_end ? IO_APPEND_ATOMIC : IO_BOUNCE_SIZE;
        if (mode == UserMode && length) {
            bounce = ExAllocatePool2(POOL_FLAG_NON_PAGED, length < bounce_size ? length : bounce_size, KI_TAG('I', 'o', 'B', 'n'));
            if (!bounce) {
                unlock_file(file);
                ObDereferenceObject(file);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        while (done < length || length == 0) {
            ULONG chunk = length - done;
            if (bounce && chunk > bounce_size)
                chunk = bounce_size;
            PVOID buf = bounce ? bounce : (PUCHAR)buffer + done;
            if (write && bounce) {
                status = copy_user(FALSE, buffer, pages, done, bounce, chunk);
                if (!NT_SUCCESS(status))
                    break;
            }
            IO_STACK_LOCATION params;
            memset(&params, 0, sizeof(params));
            params.Parameters.Read.Length = chunk;
            params.Parameters.Read.ByteOffset.QuadPart = at_end ? IO_WRITE_TO_END : offset.QuadPart + done;
            status = IopSynchronousRequest(file, write ? IRP_MJ_WRITE : IRP_MJ_READ, 0, buf, chunk, &params, &io);
            if (!NT_SUCCESS(status))
                break;
            ULONG got = (ULONG)io.Information;
            if (!write && bounce && got) {
                status = copy_user(TRUE, buffer, pages, done, bounce, got);
                if (!NT_SUCCESS(status))
                    break;
            }
            done += got;
            if (got < chunk || length == 0)
                break;
        }
        if (bounce)
            ExFreePool(bounce);
    }
    if (status == STATUS_END_OF_FILE && done)
        status = STATUS_SUCCESS;
    /* the file system leaves the position of a handle that wrote at the end where the data ended. */
    if (NT_SUCCESS(status) && !at_end && (use_position || (file->Flags & FO_SYNCHRONOUS_IO)))
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
    return rw_file(FALSE, handle, event, routine, apc_context, iosb, buffer, length, offset, key, nullptr);
}

NTSTATUS NTAPI NtWriteFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                           PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key)
{
    PVOID routine = (PVOID)apc;
    if (KeGetPreviousMode() == UserMode)
        IopTakeIosb32(&routine, &iosb);
    return rw_file(TRUE, handle, event, routine, apc_context, iosb, buffer, length, offset, key, nullptr);
}

/*
 * scatter and gather i/o: the file is read or written through an array of pages that ends with an element of zero.
 * the offset is not optional and the length is whole pages of the array, as windows requires.
 */
static NTSTATUS sg_file(BOOLEAN write, HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                        PIO_STATUS_BLOCK iosb, PVOID segments, ULONG length, PLARGE_INTEGER offset, PULONG key)
{
    PVOID routine = (PVOID)apc;
    if (KeGetPreviousMode() != UserMode)
        return STATUS_NOT_IMPLEMENTED;
    IopTakeIosb32(&routine, &iosb);
    if (!offset || (length & (PAGE_SIZE - 1)) || length / PAGE_SIZE > IO_MAX_SEGMENTS)
        return STATUS_INVALID_PARAMETER;
    /* a handle that is not a file is refused before the array is looked at. */
    PFILE_OBJECT probe;
    NTSTATUS status = reference_file(handle, &probe);
    if (!NT_SUCCESS(status))
        return status;
    ObDereferenceObject(probe);
    ULONG count = length / PAGE_SIZE;
    ULONG64 *pages = nullptr;
    if (count) {
        pages = ExAllocatePool2(POOL_FLAG_NON_PAGED, count * sizeof(ULONG64), KI_TAG('I', 'o', 'S', 'g'));
        if (!pages)
            return STATUS_INSUFFICIENT_RESOURCES;
        status = KiCopyFromUser(pages, segments, count * sizeof(ULONG64));
        for (ULONG i = 0; NT_SUCCESS(status) && i < count; i++) {
            if (!pages[i] || (pages[i] & (PAGE_SIZE - 1)))
                status = STATUS_INVALID_PARAMETER;
        }
        if (!NT_SUCCESS(status)) {
            ExFreePool(pages);
            return status;
        }
    }
    status = rw_file(write, handle, event, routine, apc_context, iosb, nullptr, length, offset, key, pages);
    if (pages)
        ExFreePool(pages);
    return status;
}

NTSTATUS NTAPI NtReadFileScatter(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                 PIO_STATUS_BLOCK iosb, PVOID segments, ULONG length, PLARGE_INTEGER offset,
                                 PULONG key)
{
    return sg_file(FALSE, handle, event, apc, apc_context, iosb, segments, length, offset, key);
}

NTSTATUS NTAPI NtWriteFileGather(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                 PIO_STATUS_BLOCK iosb, PVOID segments, ULONG length, PLARGE_INTEGER offset,
                                 PULONG key)
{
    return sg_file(TRUE, handle, event, apc, apc_context, iosb, segments, length, offset, key);
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
    ZW_WRAP(rw_file(FALSE, handle, event, nullptr, nullptr, iosb, buffer, length, offset, key, nullptr));
}

NTSTATUS NTAPI ZwWriteFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                           PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key)
{
    ZW_WRAP(rw_file(TRUE, handle, event, nullptr, nullptr, iosb, buffer, length, offset, key, nullptr));
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

/* the handle's granted access with generic rights mapped to file rights. the i/o manager answers this, not drivers. */
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
 * a name that is replaced goes as a file deleted does: the program needs the right to delete it, or to delete the
 * entries of its directory. a name that is not there is nothing to replace.
 */
static NTSTATUS check_replaced_name(HANDLE root, PWCHAR name, ULONG bytes)
{
    UNICODE_STRING target = {(USHORT)bytes, (USHORT)bytes, name};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &target, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, root, nullptr);
    HANDLE h;
    IO_STATUS_BLOCK io;
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    t->PreviousMode = KernelMode;
    NTSTATUS status = IopCreateFileChecked(&h, DELETE | SYNCHRONIZE, &oa, &io,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                           FILE_SYNCHRONOUS_IO_NONALERT);
    t->PreviousMode = old;
    if (NT_SUCCESS(status)) {
        ObCloseHandle(h, KernelMode);
        return STATUS_SUCCESS;
    }
    return status == STATUS_ACCESS_DENIED ? status : STATUS_SUCCESS;
}

/*
 * rename and link give the file system the directory the new name goes in, opened here, and the name in it. a name
 * that starts with a backslash is a full path, one given with a root directory is relative to that directory, and
 * one that is neither is a name in the file's own directory, which the file system finds itself. a link is made as the
 * program that asked, so the directory must allow it to add a file; the kernel's own renames are not held to that.
 */
static NTSTATUS retarget_file(PFILE_OBJECT file, PUCHAR buf, ULONG length, FILE_INFORMATION_CLASS klass,
                              ACCESS_MASK granted)
{
    const ULONG name_offset = 20;
    BOOLEAN link = klass == FileLinkInformation || klass == FileLinkInformationEx;
    BOOLEAN user = KeGetPreviousMode() == UserMode;
    if (length < name_offset || (user && length < 24))
        return STATUS_INFO_LENGTH_MISMATCH;
    ULONG name_length = *(PULONG)(buf + 16);
    if (!name_length || (name_length & 1) || name_length > length - name_offset)
        return STATUS_INVALID_PARAMETER;
    HANDLE root = *(HANDLE *)(buf + 8);
    BOOLEAN ex = klass == FileRenameInformationEx || klass == FileLinkInformationEx;
    BOOLEAN replace = ex ? (*(PULONG)buf & 1) != 0 : buf[0] != 0;
    PWCHAR name = (PWCHAR)(buf + name_offset);
    ULONG chars = name_length / sizeof(WCHAR), slash = chars;
    while (slash && name[slash - 1] != '\\')
        slash--;
    /* a name that ends in a backslash has no last part. */
    if (slash == chars)
        return STATUS_OBJECT_NAME_INVALID;

    IO_STACK_LOCATION params;
    memset(&params, 0, sizeof(params));
    params.Parameters.SetFile.Length = length;
    params.Parameters.SetFile.FileInformationClass = klass;
    params.Parameters.SetFile.ReplaceIfExists = replace;
    /* a program that opened the file without the right to write its attributes is held to the descriptor for a link. */
    if (link && user && !(granted & FILE_WRITE_ATTRIBUTES))
        params.Flags |= SL_LINK_CHECK_SOURCE;
    IO_STATUS_BLOCK ignored = {};
    if (!root && name[0] != '\\') {
        /* a name of the file's own directory has no directory part. */
        if (slash)
            return STATUS_OBJECT_NAME_INVALID;
        return IopSynchronousRequest(file, IRP_MJ_SET_INFORMATION, 0, buf, length, &params, &ignored);
    }

    PFILE_OBJECT directory = nullptr;
    HANDLE h = nullptr;
    NTSTATUS status;
    if (root && slash <= 1) {
        /* the leaf is a name in the root directory itself. */
        status = ObReferenceHandle(root, IoFileObjectTypeObject, KeGetPreviousMode(), (PVOID *)&directory, nullptr);
    } else {
        if (!root && slash < 2)
            return STATUS_OBJECT_NAME_INVALID;
        UNICODE_STRING parent = {(USHORT)((slash - 1) * sizeof(WCHAR)), (USHORT)((slash - 1) * sizeof(WCHAR)), name};
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &parent, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, root, nullptr);
        IO_STATUS_BLOCK io;
        PKTHREAD t = KiCurrentThread();
        KPROCESSOR_MODE old = t->PreviousMode;
        t->PreviousMode = KernelMode;
        if (link)
            status = IopCreateFileChecked(&h, FILE_ADD_FILE | SYNCHRONIZE, &oa, &io,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                          FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
        else
            status = IopCreateFile(&h, FILE_TRAVERSE | SYNCHRONIZE, &oa, &io, 0,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                   FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, KernelMode);
        t->PreviousMode = old;
        if (!NT_SUCCESS(status))
            return status == STATUS_OBJECT_NAME_NOT_FOUND ? STATUS_OBJECT_PATH_NOT_FOUND : status;
        status = ObReferenceHandle(h, IoFileObjectTypeObject, KernelMode, (PVOID *)&directory, nullptr);
    }
    if (NT_SUCCESS(status)) {
        if (directory->DeviceObject != file->DeviceObject) {
            status = STATUS_NOT_SAME_DEVICE;
        } else if (link && user && replace && !NT_SUCCESS(status = check_replaced_name(root, name, name_length))) {
            /* the program may not delete what the link would replace. */
        } else {
            params.Parameters.SetFile.FileObject = directory;
            status = IopSynchronousRequest(file, IRP_MJ_SET_INFORMATION, 0, buf, length, &params, &ignored);
        }
        ObDereferenceObject(directory);
    }
    if (h)
        ObCloseHandle(h, KernelMode);
    return status;
}

/* renames an open file to a full path for the kernel, replacing an existing file when asked. */
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
    NTSTATUS status = retarget_file(file, buf, size, FileRenameInformation, 0);
    ExFreePool(buf);
    return status;
}

NTSTATUS NTAPI NtSetInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length,
                                    FILE_INFORMATION_CLASS klass)
{
    PFILE_OBJECT file;
    ACCESS_MASK granted = 0;
    NTSTATUS status = ObReferenceHandle(handle, IoFileObjectTypeObject, KeGetPreviousMode(), (PVOID *)&file, &granted);
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
            /* like windows, modes can only be added. */
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
        } else if ((klass == FileRenameInformation || klass == FileRenameInformationEx || klass == FileLinkInformation ||
                    klass == FileLinkInformationEx) &&
                   file->DeviceObject && file->DeviceObject->DeviceType == FILE_DEVICE_DISK_FILE_SYSTEM) {
            status = retarget_file(file, buf, length, klass, granted);
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
    /* the open runs as kernel mode and would read a caller's descriptor as kernel memory, and nothing is created anyway. */
    oa.SecurityDescriptor = nullptr;
    oa.SecurityQualityOfService = nullptr;
    t->PreviousMode = KernelMode;
    /* a name only queried is not opened on the host, the open returns the attributes. */
    status = IopCreateFileEx(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &io, 0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                             FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT, KernelMode, TRUE);
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

NTSTATUS NTAPI NtSetVolumeInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length,
                                          FS_INFORMATION_CLASS klass)
{
    PFILE_OBJECT file;
    NTSTATUS status = reference_file(handle, &file);
    if (!NT_SUCCESS(status))
        return status;
    PUCHAR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, length ? length : 1, KI_TAG('I', 'o', 'S', 'v'));
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
        IO_STACK_LOCATION params;
        memset(&params, 0, sizeof(params));
        params.Parameters.QueryVolume.Length = length;
        params.Parameters.QueryVolume.FsInformationClass = klass;
        status = IopSynchronousRequest(file, IRP_MJ_SET_VOLUME_INFORMATION, 0, buf, length, &params, &io);
    }
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

/* the flags choose the data or the metadata too, and the file systems of the machine flush a file whole either way. */
NTSTATUS NTAPI NtFlushBuffersFileEx(HANDLE handle, ULONG flags, PVOID parameters, ULONG length, PIO_STATUS_BLOCK iosb)
{
    if (flags & ~0x7u || (!parameters && length) || (parameters && !length))
        return STATUS_INVALID_PARAMETER;
    return NtFlushBuffersFile(handle, iosb);
}

/* a device or file system control with buffered input and output, completed before it returns. */
static NTSTATUS buffered_control(PFILE_OBJECT file, UCHAR major, HANDLE event, PVOID apc_context,
                                 PIO_STATUS_BLOCK iosb, ULONG code, PVOID input, ULONG in_len, PVOID output,
                                 ULONG out_len)
{
    NTSTATUS status = STATUS_SUCCESS;
    IO_STATUS_BLOCK io = {};
    PUCHAR buf = nullptr;
    ULONG size = in_len > out_len ? in_len : out_len;
    if (IopIsLoadedDriverDevice(file)) {
        status = IopDriverControl(file, major, code, input, in_len, output, out_len, KeGetPreviousMode(), &io);
    } else {
        buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, KI_TAG('I', 'o', 'C', 't'));
        if (!buf)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (in_len)
            status = KiCopyFromUser(buf, input, in_len);
    }
    if (buf && NT_SUCCESS(status)) {
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
    if (buf)
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
    else if (IopIsHttp(file))
        status = HttpDeviceControl(file, event, routine, apc_context, iosb, code, input, in_len, output, out_len);
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
    else if (IopIsHttp(file))
        HttpCancel(file, PsCurrentThread(), nullptr);
    else if (IopIsPad(file))
        PadCancel(file, PsCurrentThread(), nullptr);
    else if (IopIsMimeFs(file)) {
        FsNotifyCancel(file, PsCurrentThread(), nullptr);
        FsLocksCancel(file, PsCurrentThread(), nullptr);
    }
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
    /* the request is identified by its i/o status block, the start of its OVERLAPPED. */
    if (IopIsNamedPipe(file))
        status = NpfsCancel(file, PsCurrentProcess(), request != nullptr, request);
    else if (IopIsAfd(file))
        status = AfdCancel(file, nullptr, request);
    else if (IopIsHttp(file))
        status = HttpCancel(file, nullptr, request);
    else if (IopIsPad(file))
        status = PadCancel(file, nullptr, request);
    else if (IopIsMimeFs(file)) {
        status = FsNotifyCancel(file, nullptr, request);
        NTSTATUS locks = FsLocksCancel(file, nullptr, request);
        if (NT_SUCCESS(locks))
            status = locks;
    }
    else
        status = STATUS_NOT_FOUND;
    ObDereferenceObject(file);
    IO_STATUS_BLOCK io = {{status}, 0};
    store_iosb(iosb, &io);
    return status;
}
