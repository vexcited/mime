/*
 * what a windows that has started holds before any program of the user runs: the devices of its drivers, the named
 * objects of its shell, the pool blocks of both and the handles kept on them. mime builds those drivers and that shell
 * into the kernel and the host, so their objects are made here, measured on windows 11 26300: each one exists and
 * answers nothing. a list a program sizes, the devices, the session's names, the pool tags, the big pool and the
 * handles, then overflows the room it overflows there.
 */
#include "../ntos.h"

enum { NAMED_EVENT, NAMED_MUTANT, NAMED_SEMAPHORE, NAMED_SECTION, NAMED_DIRECTORY, NAMED_LINK };

#include "population.h"

#define SESSION_NAMES L"\\Sessions\\1\\BaseNamedObjects\\"

static PVOID g_session_objects[RTL_NUMBER_OF(g_session)];
static ULONG g_session_object_count;

static PDRIVER_OBJECT driver_named(PCWSTR name)
{
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, name);
    PDRIVER_OBJECT driver = nullptr;
    if (!NT_SUCCESS(ObReferenceObjectByName(&n, OBJ_CASE_INSENSITIVE, nullptr, 0, IoDriverObjectType, KernelMode, nullptr,
                                            (PVOID *)&driver)))
        return nullptr;
    ObDereferenceObject(driver);
    return driver;
}

static NTSTATUS NTAPI empty_driver(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    return STATUS_SUCCESS;
}

/*
 * a device goes to the driver that owns it on windows when mime has that driver and it is an empty one, and to one
 * driver without a name otherwise: a named one would add its image to the list of modules, which is the reference's.
 */
static void devices(void)
{
    PDRIVER_OBJECT own = nullptr, owners[RTL_NUMBER_OF(g_owners)] = {};
    if (!NT_SUCCESS(IoCreateDriver(nullptr, empty_driver, &own)))
        return;
    for (ULONG i = 1; i < RTL_NUMBER_OF(g_owners); i++)
        owners[i] = driver_named(g_owners[i]);
    ObCreateDirectory(L"\\Device\\cimfs", nullptr);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_devices); i++) {
        WCHAR path[96] = L"\\Device\\";
        wcscat(path, g_devices[i].Name);
        UNICODE_STRING name;
        RtlInitUnicodeString(&name, path);
        PDRIVER_OBJECT driver = owners[g_devices[i].Owner] ? owners[g_devices[i].Owner] : own;
        PDEVICE_OBJECT device;
        if (NT_SUCCESS(IoCreateDevice(driver, 0, &name, FILE_DEVICE_UNKNOWN, 0, FALSE, &device)))
            device->Flags &= ~(ULONG)DO_DEVICE_INITIALIZING;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_device_links); i++) {
        WCHAR path[96] = L"\\Device\\";
        wcscat(path, g_device_links[i].Name);
        ObCreateSymbolicLink(path, g_device_links[i].Target);
    }
    UNICODE_STRING safe = RTL_CONSTANT_STRING(L"\\Device\\VolumesSafeForWriteAccess");
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &safe, OBJ_PERMANENT, nullptr, nullptr);
    HANDLE handle;
    NtCreateEvent(&handle, EVENT_ALL_ACCESS, &oa, NotificationEvent, TRUE);
}

/* a name of the table, with the sid of the user after \x01 and their name after \x02. the target of a link follows \x03. */
static PCWSTR expand(PCWSTR from, PWSTR to, ULONG chars)
{
    WCHAR sid[80] = {}, user[RTL_NUMBER_OF(HalBootInfo->user_name) + 1] = {};
    _snwprintf(sid, RTL_NUMBER_OF(sid) - 1, L"S-1-5-21-%u-%u-%u-1001", HalBootInfo->user_sid[0], HalBootInfo->user_sid[1],
               HalBootInfo->user_sid[2]);
    for (ULONG i = 0; i < RTL_NUMBER_OF(HalBootInfo->user_name) && HalBootInfo->user_name[i]; i++)
        user[i] = (WCHAR)(UCHAR)HalBootInfo->user_name[i];
    ULONG at = 0;
    for (; *from && *from != 3; from++) {
        PCWSTR part = *from == 1 ? sid : *from == 2 ? user : nullptr;
        if (!part && at < chars - 1)
            to[at++] = *from;
        for (; part && *part && at < chars - 1; part++)
            to[at++] = *part;
    }
    to[at] = 0;
    return *from ? from + 1 : nullptr;
}

/* the creator keeps its handle, as the shell does, and the object stays while the machine runs. */
static void session_objects(void)
{
    ObCreateDirectory(L"\\Sessions\\1\\AppContainerNamedObjects", nullptr);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_session); i++) {
        WCHAR path[400] = SESSION_NAMES;
        ULONG prefix = RTL_NUMBER_OF(SESSION_NAMES) - 1;
        PCWSTR target = expand(g_session[i].Name, path + prefix, RTL_NUMBER_OF(path) - prefix);
        UNICODE_STRING name;
        RtlInitUnicodeString(&name, path);
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &name, OBJ_PERMANENT, nullptr, nullptr);
        HANDLE handle = nullptr;
        NTSTATUS status = STATUS_SUCCESS;
        ULONG a = g_session[i].A, b = g_session[i].B;
        switch (g_session[i].Kind) {
        case NAMED_EVENT:
            status = NtCreateEvent(&handle, EVENT_ALL_ACCESS, &oa, a ? SynchronizationEvent : NotificationEvent, b != 0);
            break;
        case NAMED_MUTANT:
            status = NtCreateMutant(&handle, MUTANT_ALL_ACCESS, &oa, FALSE);
            break;
        case NAMED_SEMAPHORE:
            status = NtCreateSemaphore(&handle, SEMAPHORE_ALL_ACCESS, &oa, (LONG)a, (LONG)b);
            break;
        case NAMED_SECTION: {
            LARGE_INTEGER size = {.QuadPart = a};
            status = NtCreateSection(&handle, SECTION_ALL_ACCESS, &oa, &size, PAGE_READWRITE, b, nullptr);
            break;
        }
        case NAMED_DIRECTORY:
            ObCreateDirectory(path, nullptr);
            break;
        case NAMED_LINK:
            ObCreateSymbolicLink(path, target);
            break;
        }
        PVOID object;
        if (handle && NT_SUCCESS(status) &&
            NT_SUCCESS(ObReferenceObjectByHandle(handle, 0, nullptr, KernelMode, &object, nullptr))) {
            /* the shell of the user made it, so the user reaches it. */
            ObSetInteractiveSecurity(object, TRUE);
            g_session_objects[g_session_object_count++] = object;
        }
    }
}

/*
 * every tag of the reference holds a block, and the big pool holds as many blocks of whole pages under the same tags.
 * a block is the smallest its list takes and is never written, so the host gives the big ones no memory.
 */
static void pool_blocks(void)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_tags); i++)
        ExAllocatePool2((g_tags[i].Paged ? POOL_FLAG_PAGED : POOL_FLAG_NON_PAGED) | POOL_FLAG_UNINITIALIZED, 1, g_tags[i].Tag);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_big); i++)
        for (ULONG k = 0; k < g_big[i].Count; k++)
            ExAllocatePool2((g_big[i].NonPaged ? POOL_FLAG_NON_PAGED : POOL_FLAG_PAGED) | POOL_FLAG_UNINITIALIZED, PAGE_SIZE,
                            g_big[i].Tag);
}

/*
 * windows holds the handles of the reference in the hundred and sixty processes of its services and its shell. here
 * those are the system process, which keeps as many on the objects of the session.
 */
static void handles(void)
{
    if (!g_session_object_count)
        return;
    PHANDLE_TABLE table = &PsInitialSystemProcess->HandleTable;
    for (ULONG i = 0; table->Count < REFERENCE_HANDLES; i++) {
        PVOID object = g_session_objects[i % g_session_object_count];
        HANDLE handle;
        ObReferenceObject(object);
        if (!NT_SUCCESS(ObCreateHandle(PsInitialSystemProcess, object, ObGetObjectType(object)->Init.GenericMapping[3], 0,
                                       &handle))) {
            ObDereferenceObject(object);
            break;
        }
    }
}

void ExPopulateReference(void)
{
    devices();
    session_objects();
    pool_blocks();
    handles();
}
