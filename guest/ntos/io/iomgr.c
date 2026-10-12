/*
 * i/o manager core: drivers, devices, irps and the device parse routine
 * that turns object namespace lookups into IRP_MJ_CREATE requests.
 */
#include "../ntos.h"

POBJECT_TYPE IoDeviceObjectType;
POBJECT_TYPE IoDriverObjectType;
POBJECT_TYPE IoFileObjectTypeObject;
POBJECT_TYPE *IoFileObjectType = &IoFileObjectTypeObject;

/* carried through ObLookupObjectName into IopParseDevice. */
typedef struct _OPEN_PACKET {
    ULONG Magic;
    ACCESS_MASK DesiredAccess;
    ULONG Disposition;
    ULONG Options;
    ULONG FileAttributes;
    ULONG ShareAccess;
    PFILE_OBJECT RelatedFile;
    IO_STATUS_BLOCK Iosb;
    BOOLEAN QueryOnly;
} OPEN_PACKET, *POPEN_PACKET;

#define OPEN_PACKET_MAGIC 0x4F50454Eu

static NTSTATUS NTAPI invalid_request(PDEVICE_OBJECT device, PIRP irp)
{
    irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    irp->IoStatus.Information = 0;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS IoCreateDriver(PCWSTR name, PDRIVER_INITIALIZE init, PDRIVER_OBJECT *out)
{
    PDRIVER_OBJECT driver;
    NTSTATUS status = ObCreateObject(IoDriverObjectType, NULL, KernelMode,
                                     sizeof(DRIVER_OBJECT) + sizeof(DRIVER_EXTENSION), (PVOID *)&driver);
    if (!NT_SUCCESS(status))
        return status;
    memset(driver, 0, sizeof(DRIVER_OBJECT) + sizeof(DRIVER_EXTENSION));
    driver->Type = IO_TYPE_DRIVER;
    driver->Size = sizeof(DRIVER_OBJECT);
    driver->DriverExtension = (PDRIVER_EXTENSION)(driver + 1);
    driver->DriverExtension->DriverObject = driver;
    driver->DriverInit = init;
    for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
        driver->MajorFunction[i] = invalid_request;
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, name);
    driver->DriverName.Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, n.Length + sizeof(WCHAR), KI_TAG('I', 'o', 'D', 'n'));
    if (driver->DriverName.Buffer) {
        memcpy(driver->DriverName.Buffer, n.Buffer, n.Length + sizeof(WCHAR));
        driver->DriverName.Length = n.Length;
        driver->DriverName.MaximumLength = (USHORT)(n.Length + sizeof(WCHAR));
    }
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &n, OBJ_PERMANENT | OBJ_KERNEL_HANDLE, NULL, NULL);
    ObReferenceObject(driver);
    status = ObInsertObjectNamed(driver, &oa, 0, KernelMode, NULL);
    UNICODE_STRING registry = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Services");
    status = init(driver, &registry);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(driver);
        return status;
    }
    if (out)
        *out = driver;
    else
        ObDereferenceObject(driver);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI IoCreateDevice(PDRIVER_OBJECT driver, ULONG extension_size, PUNICODE_STRING name, ULONG type,
                              ULONG characteristics, BOOLEAN exclusive, PDEVICE_OBJECT *out)
{
    PDEVICE_OBJECT device;
    ULONG size = sizeof(DEVICE_OBJECT) + ((extension_size + 15) & ~15u);
    NTSTATUS status = ObCreateObject(IoDeviceObjectType, NULL, KernelMode, size, (PVOID *)&device);
    if (!NT_SUCCESS(status))
        return status;
    memset(device, 0, size);
    device->Type = IO_TYPE_DEVICE;
    device->Size = (USHORT)(sizeof(DEVICE_OBJECT) + extension_size);
    device->ReferenceCount = 0;
    device->DriverObject = driver;
    device->DeviceType = type;
    device->Characteristics = characteristics;
    device->StackSize = 1;
    device->Flags = DO_DEVICE_INITIALIZING | (exclusive ? DO_EXCLUSIVE : 0);
    device->AlignmentRequirement = 0;
    device->SectorSize = 512;
    if (extension_size)
        device->DeviceExtension = device + 1;
    KeInitializeEvent(&device->DeviceLock, SynchronizationEvent, TRUE);
    device->NextDevice = driver->DeviceObject;
    driver->DeviceObject = device;
    if (name && name->Length) {
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, name, OBJ_PERMANENT | OBJ_KERNEL_HANDLE, NULL, NULL);
        ObReferenceObject(device);
        status = ObInsertObjectNamed(device, &oa, 0, KernelMode, NULL);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(device);
            return status;
        }
    }
    *out = device;
    return STATUS_SUCCESS;
}

void NTAPI IoDeleteDevice(PDEVICE_OBJECT device)
{
    PDRIVER_OBJECT driver = device->DriverObject;
    PDEVICE_OBJECT *link = &driver->DeviceObject;
    while (*link && *link != device)
        link = &(*link)->NextDevice;
    if (*link)
        *link = device->NextDevice;
    ObDereferenceObject(device);
}

PDEVICE_OBJECT NTAPI IoGetAttachedDevice(PDEVICE_OBJECT device)
{
    while (device->AttachedDevice)
        device = device->AttachedDevice;
    return device;
}

PDEVICE_OBJECT NTAPI IoAttachDeviceToDeviceStack(PDEVICE_OBJECT source, PDEVICE_OBJECT target)
{
    PDEVICE_OBJECT top = IoGetAttachedDevice(target);
    top->AttachedDevice = source;
    source->StackSize = (CCHAR)(top->StackSize + 1);
    return top;
}

void NTAPI IoDetachDevice(PDEVICE_OBJECT target)
{
    target->AttachedDevice = NULL;
}

PDEVICE_OBJECT NTAPI IoGetRelatedDeviceObject(PFILE_OBJECT file)
{
    return IoGetAttachedDevice(file->DeviceObject);
}

PIRP NTAPI IoAllocateIrp(CCHAR stack_size, BOOLEAN charge_quota)
{
    ULONG size = sizeof(IRP) + (ULONG)stack_size * sizeof(IO_STACK_LOCATION);
    PIRP irp = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('I', 'r', 'p', ' '));
    if (!irp)
        return NULL;
    irp->Type = IO_TYPE_IRP;
    irp->Size = (USHORT)size;
    irp->StackCount = stack_size;
    irp->CurrentLocation = (CHAR)(stack_size + 1);
    InitializeListHead(&irp->ThreadListEntry);
    irp->Tail.Overlay.CurrentStackLocation = (PIO_STACK_LOCATION)(irp + 1) + stack_size;
    return irp;
}

void NTAPI IoFreeIrp(PIRP irp)
{
    ExFreePool(irp);
}

NTSTATUS FASTCALL IofCallDriver(PDEVICE_OBJECT device, PIRP irp)
{
    irp->CurrentLocation--;
    if (irp->CurrentLocation <= 0)
        KeBugCheckEx(0x00000035, (ULONG_PTR)irp, 0, 0, 0); /* NO_MORE_IRP_STACK_LOCATIONS */
    PIO_STACK_LOCATION stack = --irp->Tail.Overlay.CurrentStackLocation;
    stack->DeviceObject = device;
    return device->DriverObject->MajorFunction[stack->MajorFunction](device, irp);
}

#define STATUS_MORE_PROCESSING_REQUIRED ((NTSTATUS)0xC0000016L)

void FASTCALL IofCompleteRequest(PIRP irp, CCHAR boost)
{
    PIO_STACK_LOCATION top = (PIO_STACK_LOCATION)(irp + 1) + irp->StackCount;
    while (irp->Tail.Overlay.CurrentStackLocation < top) {
        PIO_STACK_LOCATION stack = irp->Tail.Overlay.CurrentStackLocation;
        irp->PendingReturned = (stack->Control & SL_PENDING_RETURNED) != 0;
        irp->CurrentLocation++;
        irp->Tail.Overlay.CurrentStackLocation++;
        BOOLEAN ok = NT_SUCCESS(irp->IoStatus.Status);
        if (stack->CompletionRoutine &&
            ((ok && (stack->Control & SL_INVOKE_ON_SUCCESS)) || (!ok && (stack->Control & SL_INVOKE_ON_ERROR)) ||
             (irp->Cancel && (stack->Control & SL_INVOKE_ON_CANCEL)))) {
            PDEVICE_OBJECT dev = irp->Tail.Overlay.CurrentStackLocation < top
                                     ? irp->Tail.Overlay.CurrentStackLocation->DeviceObject
                                     : NULL;
            if (stack->CompletionRoutine(dev, irp, stack->Context) == STATUS_MORE_PROCESSING_REQUIRED)
                return;
        }
    }
    if (irp->UserIosb)
        *irp->UserIosb = irp->IoStatus;
    if (irp->UserEvent)
        KeSetEvent(irp->UserEvent, boost, FALSE);
}

/* issues an irp to the file's device and waits for completion. */
NTSTATUS IopSynchronousRequest(PFILE_OBJECT file, UCHAR major, UCHAR minor, PVOID buffer, ULONG length,
                               PIO_STACK_LOCATION params, PIO_STATUS_BLOCK iosb)
{
    PDEVICE_OBJECT device = IoGetRelatedDeviceObject(file);
    PIRP irp = IoAllocateIrp(device->StackSize, FALSE);
    if (!irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    KEVENT event;
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    IO_STATUS_BLOCK local = {0};
    irp->UserEvent = &event;
    irp->UserIosb = &local;
    irp->UserBuffer = buffer;
    irp->AssociatedIrp.SystemBuffer = buffer;
    irp->RequestorMode = KernelMode;
    irp->Tail.Overlay.Thread = PsCurrentThread();
    irp->Tail.Overlay.OriginalFileObject = file;
    irp->Flags = IRP_SYNCHRONOUS_API;
    PIO_STACK_LOCATION stack = IoGetNextIrpStackLocation(irp);
    if (params)
        *stack = *params;
    else
        memset(stack, 0, sizeof(*stack));
    stack->MajorFunction = major;
    stack->MinorFunction = minor;
    stack->FileObject = file;
    NTSTATUS status = IofCallDriver(device, irp);
    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = local.Status;
    } else {
        local = irp->IoStatus;
        local.Status = status;
    }
    if (iosb)
        *iosb = local;
    IoFreeIrp(irp);
    return status;
}

NTSTATUS IoReadFileObject(PFILE_OBJECT file, PVOID buffer, ULONG length, ULONG64 offset, PULONG read)
{
    IO_STACK_LOCATION params;
    memset(&params, 0, sizeof(params));
    params.Parameters.Read.Length = length;
    params.Parameters.Read.ByteOffset.QuadPart = (LONGLONG)offset;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = IopSynchronousRequest(file, IRP_MJ_READ, 0, buffer, length, &params, &iosb);
    if (read)
        *read = NT_SUCCESS(status) ? (ULONG)iosb.Information : 0;
    return status;
}

NTSTATUS IoQueryFileSize(PFILE_OBJECT file, ULONG64 *size)
{
    FILE_STANDARD_INFORMATION info;
    IO_STACK_LOCATION params;
    memset(&params, 0, sizeof(params));
    params.Parameters.QueryFile.Length = sizeof(info);
    params.Parameters.QueryFile.FileInformationClass = FileStandardInformation;
    NTSTATUS status = IopSynchronousRequest(file, IRP_MJ_QUERY_INFORMATION, 0, &info, sizeof(info), &params, NULL);
    if (NT_SUCCESS(status))
        *size = (ULONG64)info.EndOfFile.QuadPart;
    return status;
}

/* object manager parse procedure of device objects. */
static NTSTATUS parse_device(PVOID object, PVOID type, ULONG attributes, PUNICODE_STRING complete,
                             PUNICODE_STRING remaining, PVOID context, PVOID *found)
{
    PDEVICE_OBJECT device = object;
    POPEN_PACKET op = context;
    if (!op || op->Magic != OPEN_PACKET_MAGIC) {
        if (remaining->Length)
            return STATUS_OBJECT_PATH_NOT_FOUND;
        ObReferenceObject(device);
        *found = device;
        return STATUS_SUCCESS;
    }
    PFILE_OBJECT file;
    NTSTATUS status = ObCreateObject(IoFileObjectTypeObject, NULL, KernelMode, sizeof(FILE_OBJECT), (PVOID *)&file);
    if (!NT_SUCCESS(status))
        return status;
    memset(file, 0, sizeof(*file));
    file->Type = IO_TYPE_FILE;
    file->Size = sizeof(FILE_OBJECT);
    file->DeviceObject = device;
    file->RelatedFileObject = op->RelatedFile;
    KeInitializeEvent(&file->Lock, SynchronizationEvent, FALSE);
    KeInitializeEvent(&file->Event, NotificationEvent, FALSE);
    if (op->Options & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT))
        file->Flags |= FO_SYNCHRONOUS_IO;
    if (op->Options & FILE_DELETE_ON_CLOSE)
        file->Flags |= FO_DELETE_ON_CLOSE;
    file->ReadAccess = (op->DesiredAccess & (FILE_READ_DATA | GENERIC_READ | GENERIC_ALL | MAXIMUM_ALLOWED)) != 0;
    file->WriteAccess = (op->DesiredAccess & (FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_WRITE | GENERIC_ALL)) != 0;
    file->DeleteAccess = (op->DesiredAccess & DELETE) != 0;
    if (remaining->Length) {
        file->FileName.Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, remaining->Length + sizeof(WCHAR), KI_TAG('I', 'o', 'F', 'n'));
        if (!file->FileName.Buffer) {
            ObDereferenceObject(file);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        memcpy(file->FileName.Buffer, remaining->Buffer, remaining->Length);
        file->FileName.Buffer[remaining->Length / sizeof(WCHAR)] = 0;
        file->FileName.Length = remaining->Length;
        file->FileName.MaximumLength = (USHORT)(remaining->Length + sizeof(WCHAR));
    }

    IO_SECURITY_CONTEXT security = {0};
    security.DesiredAccess = op->DesiredAccess;
    IO_STACK_LOCATION params;
    memset(&params, 0, sizeof(params));
    params.Parameters.Create.SecurityContext = &security;
    params.Parameters.Create.Options = (op->Disposition << 24) | (op->Options & 0x00FFFFFF);
    params.Parameters.Create.FileAttributes = (USHORT)op->FileAttributes;
    params.Parameters.Create.ShareAccess = (USHORT)op->ShareAccess;
    status = IopSynchronousRequest(file, IRP_MJ_CREATE, 0, NULL, 0, &params, &op->Iosb);
    if (!NT_SUCCESS(status)) {
        file->DeviceObject = NULL; /* never opened, no close irp */
        ObDereferenceObject(file);
        return status;
    }
    file->Flags |= FO_HANDLE_CREATED;
    *found = file;
    return STATUS_SUCCESS;
}

static void file_close(PEPROCESS process, PVOID object, ULONG handle_count)
{
    PFILE_OBJECT file = object;
    if (handle_count == 1 && file->DeviceObject)
        IopSynchronousRequest(file, IRP_MJ_CLEANUP, 0, NULL, 0, NULL, NULL);
}

static void file_delete(PVOID object)
{
    PFILE_OBJECT file = object;
    if (file->DeviceObject && (file->Flags & FO_HANDLE_CREATED))
        IopSynchronousRequest(file, IRP_MJ_CLOSE, 0, NULL, 0, NULL, NULL);
    if (file->FileName.Buffer)
        ExFreePool(file->FileName.Buffer);
    IopReleaseCompletion(file);
}

static PVOID file_wait_object(PVOID object)
{
    return &((PFILE_OBJECT)object)->Event;
}

NTSTATUS IopCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                       ULONG file_attributes, ULONG share, ULONG disposition, ULONG options, KPROCESSOR_MODE mode)
{
    OPEN_PACKET op;
    memset(&op, 0, sizeof(op));
    op.Magic = OPEN_PACKET_MAGIC;
    op.DesiredAccess = access;
    op.Disposition = disposition;
    op.Options = options;
    op.FileAttributes = file_attributes;
    op.ShareAccess = share;
    PFILE_OBJECT related = NULL;
    if (attributes->RootDirectory) {
        NTSTATUS st = ObReferenceHandle(attributes->RootDirectory, NULL, mode, (PVOID *)&related, NULL);
        if (!NT_SUCCESS(st))
            return st;
        if (OBJECT_TO_HEADER(related)->Type == IoFileObjectTypeObject) {
            /* relative opens: prepend the related file name and open on its device. */
            PFILE_OBJECT rel = related;
            UNICODE_STRING *name = attributes->ObjectName;
            ULONG total = rel->FileName.Length + sizeof(WCHAR) + (name ? name->Length : 0);
            PWSTR buf = ExAllocatePool2(POOL_FLAG_PAGED, total + sizeof(WCHAR), KI_TAG('I', 'o', 'R', 'l'));
            if (!buf) {
                ObDereferenceObject(related);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            ULONG n = rel->FileName.Length / sizeof(WCHAR);
            memcpy(buf, rel->FileName.Buffer, rel->FileName.Length);
            if (name && name->Length) {
                if (n == 0 || buf[n - 1] != '\\')
                    buf[n++] = '\\';
                memcpy(buf + n, name->Buffer, name->Length);
                n += name->Length / sizeof(WCHAR);
            }
            buf[n] = 0;
            UNICODE_STRING remaining = {(USHORT)(n * 2), (USHORT)(n * 2 + 2), buf};
            PVOID found;
            NTSTATUS st = parse_device(rel->DeviceObject, NULL, attributes->Attributes, &remaining, &remaining, &op, &found);
            ExFreePool(buf);
            ObDereferenceObject(related);
            if (iosb)
                *iosb = op.Iosb;
            if (!NT_SUCCESS(st))
                return st;
            return ObInsertObject(found, access, mode == UserMode ? (attributes->Attributes & ~OBJ_KERNEL_HANDLE) : attributes->Attributes, handle);
        }
        ObDereferenceObject(related);
    }
    PVOID found;
    NTSTATUS status = ObLookupObjectName(attributes->ObjectName, attributes->RootDirectory, attributes->Attributes,
                                         NULL, mode, &op, &found);
    if (iosb)
        *iosb = op.Iosb;
    if (!NT_SUCCESS(status))
        return status;
    if (OBJECT_TO_HEADER(found)->Type != IoFileObjectTypeObject) {
        ObDereferenceObject(found);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    ULONG attrs = attributes->Attributes;
    if (mode == UserMode)
        attrs &= ~OBJ_KERNEL_HANDLE;
    return ObInsertObject(found, access, attrs, handle);
}

NTSTATUS IopOpenFileObject(PCUNICODE_STRING path, ACCESS_MASK access, ULONG options, PFILE_OBJECT *out)
{
    OPEN_PACKET op;
    memset(&op, 0, sizeof(op));
    op.Magic = OPEN_PACKET_MAGIC;
    op.DesiredAccess = access;
    op.Disposition = FILE_OPEN;
    op.Options = options | FILE_SYNCHRONOUS_IO_NONALERT;
    op.ShareAccess = FILE_SHARE_READ | FILE_SHARE_DELETE;
    UNICODE_STRING p = *path;
    PVOID found;
    NTSTATUS status = ObLookupObjectName(&p, NULL, OBJ_CASE_INSENSITIVE, NULL, KernelMode, &op, &found);
    if (!NT_SUCCESS(status))
        return status;
    if (OBJECT_TO_HEADER(found)->Type != IoFileObjectTypeObject) {
        ObDereferenceObject(found);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    *out = found;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI IoGetDeviceObjectPointer(PUNICODE_STRING name, ACCESS_MASK access, PFILE_OBJECT *file,
                                        PDEVICE_OBJECT *device)
{
    PFILE_OBJECT f;
    NTSTATUS status = IopOpenFileObject(name, access, 0, &f);
    if (!NT_SUCCESS(status))
        return status;
    *file = f;
    *device = IoGetRelatedDeviceObject(f);
    return STATUS_SUCCESS;
}

PMDL NTAPI IoAllocateMdl(PVOID va, ULONG length, BOOLEAN secondary, BOOLEAN charge, PIRP irp)
{
    PMDL mdl = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MDL), KI_TAG('I', 'o', 'M', 'd'));
    if (!mdl)
        return NULL;
    mdl->StartVa = PAGE_ALIGN(va);
    mdl->ByteOffset = BYTE_OFFSET(va);
    mdl->ByteCount = length;
    mdl->MappedSystemVa = va;
    mdl->MdlFlags = MDL_SOURCE_IS_NONPAGED_POOL;
    if (irp && !secondary)
        irp->MdlAddress = mdl;
    return mdl;
}

void NTAPI IoFreeMdl(PMDL mdl)
{
    ExFreePool(mdl);
}

PIRP NTAPI IoBuildSynchronousFsdRequest(ULONG major, PDEVICE_OBJECT device, PVOID buffer, ULONG length,
                                        PLARGE_INTEGER offset, PKEVENT event, PIO_STATUS_BLOCK iosb)
{
    PIRP irp = IoAllocateIrp(device->StackSize, FALSE);
    if (!irp)
        return NULL;
    irp->UserBuffer = buffer;
    irp->AssociatedIrp.SystemBuffer = buffer;
    irp->UserEvent = event;
    irp->UserIosb = iosb;
    irp->Tail.Overlay.Thread = PsCurrentThread();
    PIO_STACK_LOCATION stack = IoGetNextIrpStackLocation(irp);
    stack->MajorFunction = (UCHAR)major;
    if (major == IRP_MJ_READ || major == IRP_MJ_WRITE) {
        stack->Parameters.Read.Length = length;
        if (offset)
            stack->Parameters.Read.ByteOffset = *offset;
    }
    return irp;
}

PIRP NTAPI IoBuildDeviceIoControlRequest(ULONG code, PDEVICE_OBJECT device, PVOID input, ULONG in_len, PVOID output,
                                         ULONG out_len, BOOLEAN internal, PKEVENT event, PIO_STATUS_BLOCK iosb)
{
    PIRP irp = IoAllocateIrp(device->StackSize, FALSE);
    if (!irp)
        return NULL;
    irp->UserBuffer = output;
    irp->AssociatedIrp.SystemBuffer = input;
    irp->UserEvent = event;
    irp->UserIosb = iosb;
    irp->Tail.Overlay.Thread = PsCurrentThread();
    PIO_STACK_LOCATION stack = IoGetNextIrpStackLocation(irp);
    stack->MajorFunction = internal ? IRP_MJ_INTERNAL_DEVICE_CONTROL : IRP_MJ_DEVICE_CONTROL;
    stack->Parameters.DeviceIoControl.IoControlCode = code;
    stack->Parameters.DeviceIoControl.InputBufferLength = in_len;
    stack->Parameters.DeviceIoControl.OutputBufferLength = out_len;
    stack->Parameters.DeviceIoControl.Type3InputBuffer = input;
    return irp;
}

void IoInitialize(void)
{
    OBJECT_TYPE_INITIALIZER init = {0};
    init.ValidAccessMask = FILE_ALL_ACCESS;
    init.ParseProcedure = parse_device;
    IoDeviceObjectType = ObCreateObjectType(L"Device", &init);
    memset(&init, 0, sizeof(init));
    IoDriverObjectType = ObCreateObjectType(L"Driver", &init);
    init.ValidAccessMask = FILE_ALL_ACCESS;
    init.CloseProcedure = file_close;
    init.DeleteProcedure = file_delete;
    init.WaitObject = file_wait_object;
    IoFileObjectTypeObject = ObCreateObjectType(L"File", &init);
    IopInitializeCompletion();
    IopInitializeNamedPipes();

    NTSTATUS status = IoCreateDriver(L"\\FileSystem\\Ntfs", MimeFsDriverEntry, NULL);
    if (!NT_SUCCESS(status))
        KiLogPrintf("mimefs failed to start: %08x", status);
    ObCreateSymbolicLink(L"\\GLOBAL??\\C:", L"\\Device\\HarddiskVolume1");
    ObCreateSymbolicLink(L"\\GLOBAL??\\Z:", L"\\Device\\HarddiskVolume2");
    ObCreateSymbolicLink(L"\\GLOBAL??\\SystemRoot", L"\\Device\\HarddiskVolume1\\Windows");
}
