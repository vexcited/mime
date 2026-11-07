#include "../ntos.h"
#include "../se/se.h"

#define SE_LOAD_DRIVER_PRIVILEGE 10
#define SERVICE_FILE_SYSTEM_DRIVER 0x2
#define SERVICE_RECOGNIZER_DRIVER 0x8

static UNICODE_STRING g_hardware_database = RTL_CONSTANT_STRING(L"\\REGISTRY\\MACHINE\\HARDWARE\\DESCRIPTION\\SYSTEM");

typedef struct {
    UNICODE_STRING Key;
    BOOLEAN Unload;
    NTSTATUS Status;
    KEVENT Done;
} IOP_DRIVER_REQUEST;

#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_DWORD 4

static NTSTATUS read_value(PCWSTR key, PCWSTR name, ULONG *type, PVOID data, ULONG size, ULONG *length)
{
    *length = size;
    return CmQueryValue(key, name, type, data, length);
}

/* the service's name is the last component of its key. */
static UNICODE_STRING service_name(PCUNICODE_STRING key)
{
    const WCHAR *end = key->Buffer + key->Length / sizeof(WCHAR), *leaf = end;
    while (leaf > key->Buffer && leaf[-1] != L'\\')
        leaf--;
    UNICODE_STRING name = {(USHORT)((end - leaf) * sizeof(WCHAR)), (USHORT)((end - leaf) * sizeof(WCHAR)), (PWSTR)leaf};
    return name;
}

static NTSTATUS append(PUNICODE_STRING out, PCWSTR text, PCUNICODE_STRING tail)
{
    SIZE_T prefix = wcslen(text) * sizeof(WCHAR), total = prefix + (tail ? tail->Length : 0);
    if (total > 0xFFF0)
        return STATUS_NAME_TOO_LONG;
    out->Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, total + sizeof(WCHAR), KI_TAG('I', 'o', 'D', 'r'));
    if (!out->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(out->Buffer, text, prefix);
    if (tail)
        memcpy((PUCHAR)out->Buffer + prefix, tail->Buffer, tail->Length);
    out->Length = (USHORT)total;
    out->MaximumLength = (USHORT)(total + sizeof(WCHAR));
    out->Buffer[total / sizeof(WCHAR)] = 0;
    return STATUS_SUCCESS;
}

/*
 * the image a service names: ImagePath as it is when it starts with a backslash, under \SystemRoot\ otherwise,
 * and System32\drivers\<service>.sys when there is none. like windows, environment strings are not expanded.
 */
static NTSTATUS image_path(PCWSTR key, PCUNICODE_STRING service, PUNICODE_STRING out)
{
    WCHAR text[512];
    ULONG type, length;
    NTSTATUS status = read_value(key, L"ImagePath", &type, text, sizeof(text) - sizeof(WCHAR), &length);
    if (!NT_SUCCESS(status) || (type != REG_SZ && type != REG_EXPAND_SZ) || length < sizeof(WCHAR)) {
        UNICODE_STRING sys = RTL_CONSTANT_STRING(L".sys");
        UNICODE_STRING name;
        status = append(&name, L"\\SystemRoot\\System32\\drivers\\", service);
        if (!NT_SUCCESS(status))
            return status;
        status = append(out, name.Buffer, &sys);
        ExFreePool(name.Buffer);
        return status;
    }
    text[length / sizeof(WCHAR)] = 0;
    UNICODE_STRING value;
    RtlInitUnicodeString(&value, text);
    return append(out, text[0] == L'\\' ? L"" : L"\\SystemRoot\\", &value);
}

static NTSTATUS driver_object_name(PCWSTR key, PCUNICODE_STRING service, PUNICODE_STRING out)
{
    ULONG type = 0, kind = 0, length;
    if (NT_SUCCESS(read_value(key, L"Type", &type, &kind, sizeof(kind), &length)) && type == REG_DWORD &&
        (kind & (SERVICE_FILE_SYSTEM_DRIVER | SERVICE_RECOGNIZER_DRIVER)))
        return append(out, L"\\FileSystem\\", service);
    return append(out, L"\\Driver\\", service);
}

static PDRIVER_OBJECT reference_driver(PUNICODE_STRING name)
{
    PDRIVER_OBJECT driver = nullptr;
    if (!NT_SUCCESS(ObReferenceObjectByName(name, OBJ_CASE_INSENSITIVE, nullptr, 0, IoDriverObjectType, KernelMode,
                                            nullptr, (PVOID *)&driver)))
        return nullptr;
    return driver;
}

static NTSTATUS load_driver(PUNICODE_STRING key_name)
{
    PCWSTR key = key_name->Buffer;
    ULONG type, kind, length;
    NTSTATUS status = read_value(key, L"Type", &type, &kind, sizeof(kind), &length);
    if (!NT_SUCCESS(status))
        return status;
    UNICODE_STRING service = service_name(key_name);
    UNICODE_STRING image = {}, name = {};
    status = image_path(key, &service, &image);
    if (NT_SUCCESS(status))
        status = driver_object_name(key, &service, &name);
    PDRIVER_OBJECT existing = NT_SUCCESS(status) ? reference_driver(&name) : nullptr;
    if (existing) {
        ObDereferenceObject(existing);
        status = STATUS_IMAGE_ALREADY_LOADED;
    }
    PKLDR_DATA_TABLE_ENTRY module = nullptr;
    if (NT_SUCCESS(status))
        status = MmLoadSystemImage(&image, &module);
    if (NT_SUCCESS(status)) {
        PDRIVER_OBJECT driver;
        status = IopCreateDriverObject(&name, key_name, &service, module, &driver);
        if (NT_SUCCESS(status)) {
            KiLogPrintf("driver %wZ started from %wZ", &name, &image);
            ObDereferenceObject(driver);
        } else {
            KiLogPrintf("driver %wZ from %wZ failed: %08x", &name, &image, status);
            MmUnloadSystemImage(module);
        }
    } else if (status != STATUS_IMAGE_ALREADY_LOADED) {
        KiLogPrintf("cannot load the driver of %wZ from %wZ: %08x", key_name, &image, status);
    }
    if (image.Buffer)
        ExFreePool(image.Buffer);
    if (name.Buffer)
        ExFreePool(name.Buffer);
    return status;
}

static NTSTATUS unload_driver(PUNICODE_STRING key_name)
{
    PCWSTR key = key_name->Buffer;
    ULONG type, kind, length;
    NTSTATUS status = read_value(key, L"Type", &type, &kind, sizeof(kind), &length);
    if (!NT_SUCCESS(status))
        return status;
    UNICODE_STRING service = service_name(key_name);
    UNICODE_STRING name = {};
    status = driver_object_name(key, &service, &name);
    if (!NT_SUCCESS(status))
        return status;
    PDRIVER_OBJECT driver = reference_driver(&name);
    if (!driver) {
        ExFreePool(name.Buffer);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    /* a driver without an unload routine, or one of mime's own, stays. */
    if (!driver->DriverUnload || !driver->DriverSection) {
        ObDereferenceObject(driver);
        ExFreePool(name.Buffer);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    driver->DriverUnload(driver);
    driver->DriverUnload = nullptr;
    KiLogPrintf("driver %wZ stopped", &name);
    ObMakeTemporaryObject(driver);
    ObDereferenceObject(driver);
    ExFreePool(name.Buffer);
    return STATUS_SUCCESS;
}

/* drivers load and unload on a thread of the system process, as on windows. */
static void NTAPI driver_worker(PVOID context)
{
    IOP_DRIVER_REQUEST *request = context;
    request->Status = request->Unload ? unload_driver(&request->Key) : load_driver(&request->Key);
    KeSetEvent(&request->Done, 0, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static NTSTATUS run_request(PUNICODE_STRING key, BOOLEAN unload)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    if (mode == UserMode && !SepSinglePrivilegeCheck(SE_LOAD_DRIVER_PRIVILEGE, mode))
        return STATUS_PRIVILEGE_NOT_HELD;
    IOP_DRIVER_REQUEST *request = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*request), KI_TAG('I', 'o', 'D', 'q'));
    if (!request)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(request, 0, sizeof(*request));
    NTSTATUS status = mode == UserMode ? KiCaptureUnicodeString(&request->Key, key) : KiCaptureUnicodeStringValue(&request->Key, *key);
    if (NT_SUCCESS(status) && !request->Key.Length)
        status = STATUS_OBJECT_NAME_INVALID;
    if (!NT_SUCCESS(status)) {
        KiFreeCapturedString(&request->Key);
        ExFreePool(request);
        return status;
    }
    request->Unload = unload;
    KeInitializeEvent(&request->Done, NotificationEvent, FALSE);
    HANDLE thread;
    status = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, nullptr, nullptr, nullptr, driver_worker, request);
    if (NT_SUCCESS(status)) {
        ObCloseHandle(thread, KernelMode);
        KeWaitForSingleObject(&request->Done, Executive, KernelMode, FALSE, nullptr);
        status = request->Status;
    }
    KiFreeCapturedString(&request->Key);
    ExFreePool(request);
    return status;
}

NTSTATUS NTAPI NtLoadDriver(PUNICODE_STRING key)
{
    return run_request(key, FALSE);
}

NTSTATUS NTAPI NtUnloadDriver(PUNICODE_STRING key)
{
    return run_request(key, TRUE);
}

NTSTATUS NTAPI ZwLoadDriver(PUNICODE_STRING key)
{
    return run_request(key, FALSE);
}

NTSTATUS NTAPI ZwUnloadDriver(PUNICODE_STRING key)
{
    return run_request(key, TRUE);
}

/*
 * a driver object for a loaded image: its section, start and size, service key and hardware database, then
 * DriverEntry. the devices it made in DriverEntry are done initializing when it returns.
 */
NTSTATUS IopCreateDriverObject(PCUNICODE_STRING name, PCUNICODE_STRING registry, PCUNICODE_STRING service,
                               PKLDR_DATA_TABLE_ENTRY module, PDRIVER_OBJECT *out)
{
    *out = nullptr;
    ULONG extra = sizeof(DRIVER_EXTENSION) + name->Length + service->Length + registry->Length + 3 * sizeof(WCHAR);
    PDRIVER_OBJECT driver;
    NTSTATUS status = ObCreateObject(IoDriverObjectType, nullptr, KernelMode, sizeof(DRIVER_OBJECT) + extra, (PVOID *)&driver);
    if (!NT_SUCCESS(status))
        return status;
    memset(driver, 0, sizeof(DRIVER_OBJECT) + extra);
    driver->Type = IO_TYPE_DRIVER;
    driver->Size = sizeof(DRIVER_OBJECT);
    driver->DriverExtension = (PDRIVER_EXTENSION)(driver + 1);
    driver->DriverExtension->DriverObject = driver;
    driver->DriverStart = module->DllBase;
    driver->DriverSize = module->SizeOfImage;
    driver->DriverSection = module;
    driver->DriverInit = (PDRIVER_INITIALIZE)module->EntryPoint;
    driver->HardwareDatabase = &g_hardware_database;
    for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
        driver->MajorFunction[i] = IopInvalidDeviceRequest;
    PWSTR text = (PWSTR)(driver->DriverExtension + 1);
    PCUNICODE_STRING sources[] = {name, service, registry};
    UNICODE_STRING copies[3];
    for (ULONG i = 0; i < 3; i++) {
        memcpy(text, sources[i]->Buffer, sources[i]->Length);
        copies[i] = (UNICODE_STRING){sources[i]->Length, (USHORT)(sources[i]->Length + sizeof(WCHAR)), text};
        text += sources[i]->Length / sizeof(WCHAR) + 1;
    }
    driver->DriverName = copies[0];
    driver->DriverExtension->ServiceKeyName = copies[1];
    UNICODE_STRING registry_path = copies[2];
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &driver->DriverName, OBJ_PERMANENT | OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               nullptr, nullptr);
    ObReferenceObject(driver);
    status = ObInsertObjectNamed(driver, &oa, 0, KernelMode, nullptr);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(driver);
        return status;
    }
    status = driver->DriverInit(driver, &registry_path);
    if (!NT_SUCCESS(status)) {
        /* what DriverEntry left is torn down without its unload routine, as windows does. */
        while (driver->DeviceObject)
            IoDeleteDevice(driver->DeviceObject);
        driver->DriverSection = nullptr;
        ObMakeTemporaryObject(driver);
        ObDereferenceObject(driver);
        return status;
    }
    for (PDEVICE_OBJECT device = driver->DeviceObject; device; device = device->NextDevice)
        device->Flags &= ~DO_DEVICE_INITIALIZING;
    *out = driver;
    return STATUS_SUCCESS;
}

/* an image the loader brought in for a driver goes with the driver object. */
void IopDeleteDriverObject(PVOID object)
{
    PDRIVER_OBJECT driver = object;
    if (driver->DriverSection)
        MmUnloadSystemImage(driver->DriverSection);
}
