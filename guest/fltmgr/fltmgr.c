#include <mime/ddk.h>
#include "rtl.h"

/*
 * the filter manager drivers import from fltmgr.sys. this first part registers filters and answers routine lookups,
 * logging what drivers ask for that it does not have yet.
 */

#define FLT_TAG 'tlFM'
#define STATUS_FLT_INVALID_NAME_REQUEST ((NTSTATUS)0xC01C0005L)

extern IMAGE_DOS_HEADER __ImageBase;
PEPROCESS NTAPI IoThreadToProcess(PETHREAD thread);

typedef struct _FLT_FILTER {
    LIST_ENTRY Links;
    PDRIVER_OBJECT Driver;
    const void *Registration;
    BOOLEAN Filtering;
} FLT_FILTER, *PFLT_FILTER;

static LIST_ENTRY g_filters = {&g_filters, &g_filters};
static FAST_MUTEX g_lock;

/* the routines are this image's exports. */
static PVOID own_export(PCSTR name)
{
    PUCHAR base = (PUCHAR)&__ImageBase;
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)(base + __ImageBase.e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return nullptr;
    PIMAGE_EXPORT_DIRECTORY exports = (PIMAGE_EXPORT_DIRECTORY)(base + dir.VirtualAddress);
    PULONG names = (PULONG)(base + exports->AddressOfNames), functions = (PULONG)(base + exports->AddressOfFunctions);
    PUSHORT ordinals = (PUSHORT)(base + exports->AddressOfNameOrdinals);
    for (ULONG i = 0; i < exports->NumberOfNames; i++)
        if (!strcmp((const char *)base + names[i], name))
            return base + functions[ordinals[i]];
    return nullptr;
}

#ifdef MIME_FLTMGR_DISCOVER
static ULONG_PTR NTAPI missing_routine(void)
{
    DbgPrint("fltmgr: a missing routine was called from %p\n", __builtin_return_address(0));
    return 0;
}
#endif

PVOID NTAPI FltGetRoutineAddress(PCSTR name)
{
    PVOID routine = name ? own_export(name) : nullptr;
#ifdef MIME_FLTMGR_DISCOVER
    if (!routine)
        routine = (PVOID)missing_routine;
#endif
    DbgPrint("fltmgr: FltGetRoutineAddress(%s) = %p\n", name ? name : "(null)", routine);
    return routine;
}

NTSTATUS NTAPI FltRegisterFilter(PDRIVER_OBJECT driver, const void *registration, PFLT_FILTER *filter)
{
    PFLT_FILTER f = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*f), FLT_TAG);
    if (!f)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(f, 0, sizeof(*f));
    f->Driver = driver;
    f->Registration = registration;
    ExAcquireFastMutex(&g_lock);
    InsertTailList(&g_filters, &f->Links);
    ExReleaseFastMutex(&g_lock);
    DbgPrint("fltmgr: %wZ registered filter %p\n", &driver->DriverName, f);
    *filter = f;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI FltStartFiltering(PFLT_FILTER filter)
{
    filter->Filtering = TRUE;
    DbgPrint("fltmgr: filter %p started\n", filter);
    return STATUS_SUCCESS;
}

void NTAPI FltUnregisterFilter(PFLT_FILTER filter)
{
    ExAcquireFastMutex(&g_lock);
    RemoveEntryList(&filter->Links);
    ExReleaseFastMutex(&g_lock);
    DbgPrint("fltmgr: filter %p unregistered\n", filter);
    ExFreePool(filter);
}

/* windows' layouts of what callbacks get, which drivers read at fixed offsets. */
typedef struct _FLT_IO_PARAMETER_BLOCK {
    ULONG IrpFlags;
    UCHAR MajorFunction;
    UCHAR MinorFunction;
    UCHAR OperationFlags;
    UCHAR Reserved;
    PFILE_OBJECT TargetFileObject;
    PVOID TargetInstance;
    UCHAR Parameters[0x28];
} FLT_IO_PARAMETER_BLOCK;

typedef struct _FLT_CALLBACK_DATA {
    ULONG Flags;
    PETHREAD Thread;
    FLT_IO_PARAMETER_BLOCK *Iopb;
    IO_STATUS_BLOCK IoStatus;
    PVOID TagData;
    PVOID FilterContext[4];
    KPROCESSOR_MODE RequestorMode;
} FLT_CALLBACK_DATA;
MIME_STATIC_ASSERT(offsetof(FLT_CALLBACK_DATA, Iopb) == 0x10, "FLT_CALLBACK_DATA");
MIME_STATIC_ASSERT(offsetof(FLT_CALLBACK_DATA, RequestorMode) == 0x50, "FLT_CALLBACK_DATA");
MIME_STATIC_ASSERT(offsetof(FLT_IO_PARAMETER_BLOCK, TargetFileObject) == 0x8, "FLT_IO_PARAMETER_BLOCK");

typedef struct _FLT_FILE_NAME_INFORMATION {
    USHORT Size;
    USHORT NamesParsed;
    ULONG Format;
    UNICODE_STRING Name;
    UNICODE_STRING Volume;
    UNICODE_STRING Share;
    UNICODE_STRING Extension;
    UNICODE_STRING Stream;
    UNICODE_STRING FinalComponent;
    UNICODE_STRING ParentDir;
} FLT_FILE_NAME_INFORMATION, *PFLT_FILE_NAME_INFORMATION;

#define FLT_FILE_NAME_NORMALIZED 0x01
#define FLT_FILE_NAME_OPENED 0x02

/* the file's name as the object manager has it, \Device\<volume>\<path>, with the volume and parts parsed. */
NTSTATUS NTAPI FltGetFileNameInformationUnsafe(PFILE_OBJECT file, PVOID instance, ULONG options,
                                               PFLT_FILE_NAME_INFORMATION *out)
{
    if (!file || !out)
        return STATUS_INVALID_PARAMETER;
    ULONG needed = 0;
    ObQueryNameString(file, nullptr, 0, &needed);
    if (needed <= sizeof(OBJECT_NAME_INFORMATION))
        return STATUS_FLT_INVALID_NAME_REQUEST;
    PFLT_FILE_NAME_INFORMATION info = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(*info) + needed, FLT_TAG);
    if (!info)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(info, 0, sizeof(*info));
    POBJECT_NAME_INFORMATION name = (POBJECT_NAME_INFORMATION)(info + 1);
    NTSTATUS status = ObQueryNameString(file, name, needed, &needed);
    if (!NT_SUCCESS(status)) {
        ExFreePool(info);
        return status;
    }
    info->Size = sizeof(*info);
    info->Format = options & 0xFF ? options & 0xFF : FLT_FILE_NAME_OPENED;
    info->Name = name->Name;
    *out = info;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI FltGetFileNameInformation(FLT_CALLBACK_DATA *data, ULONG options, PFLT_FILE_NAME_INFORMATION *out)
{
    if (!data || !data->Iopb)
        return STATUS_INVALID_PARAMETER;
    return FltGetFileNameInformationUnsafe(data->Iopb->TargetFileObject, data->Iopb->TargetInstance, options, out);
}

void NTAPI FltReferenceFileNameInformation(PFLT_FILE_NAME_INFORMATION info)
{
}

void NTAPI FltReleaseFileNameInformation(PFLT_FILE_NAME_INFORMATION info)
{
    if (info)
        ExFreePool(info);
}

/* the volume share and the parts of the name: the final component, its extension and the parent directory. */
NTSTATUS NTAPI FltParseFileNameInformation(PFLT_FILE_NAME_INFORMATION info)
{
    UNICODE_STRING *name = &info->Name;
    USHORT n = name->Length / sizeof(WCHAR), slashes = 0, volume_end = n, last = 0;
    for (USHORT i = 0; i < n; i++) {
        if (name->Buffer[i] != L'\\')
            continue;
        if (++slashes == 3)
            volume_end = i;
        last = i;
    }
    info->Volume = (UNICODE_STRING){(USHORT)(volume_end * sizeof(WCHAR)), (USHORT)(volume_end * sizeof(WCHAR)), name->Buffer};
    USHORT final = last + 1 < n ? last + 1 : n;
    info->FinalComponent = (UNICODE_STRING){(USHORT)((n - final) * sizeof(WCHAR)), (USHORT)((n - final) * sizeof(WCHAR)),
                                            name->Buffer + final};
    USHORT parent = volume_end < last ? volume_end : last;
    info->ParentDir = (UNICODE_STRING){(USHORT)((final - parent) * sizeof(WCHAR)),
                                       (USHORT)((final - parent) * sizeof(WCHAR)), name->Buffer + parent};
    for (USHORT i = n; i > final; i--) {
        if (name->Buffer[i - 1] == L'.') {
            info->Extension = (UNICODE_STRING){(USHORT)((n - i) * sizeof(WCHAR)), (USHORT)((n - i) * sizeof(WCHAR)),
                                               name->Buffer + i};
            break;
        }
    }
    info->NamesParsed = 0x0F;
    return STATUS_SUCCESS;
}

PEPROCESS NTAPI FltGetRequestorProcess(FLT_CALLBACK_DATA *data)
{
    return data && data->Thread ? IoThreadToProcess(data->Thread) : PsGetCurrentProcess();
}

ULONG NTAPI FltGetRequestorProcessId(FLT_CALLBACK_DATA *data)
{
    return (ULONG)(ULONG_PTR)PsGetProcessId(FltGetRequestorProcess(data));
}

/* filters read and query below themselves; with no filters below, that is the file itself. */
static NTSTATUS file_handle(PFILE_OBJECT file, HANDLE *handle)
{
    return ObOpenObjectByPointer(file, OBJ_KERNEL_HANDLE, nullptr, 0, IoFileObjectType, KernelMode, handle);
}

NTSTATUS NTAPI FltQueryInformationFile(PVOID instance, PFILE_OBJECT file, PVOID info, ULONG length,
                                       FILE_INFORMATION_CLASS klass, PULONG returned)
{
    HANDLE handle;
    NTSTATUS status = file_handle(file, &handle);
    if (!NT_SUCCESS(status))
        return status;
    IO_STATUS_BLOCK iosb = {};
    status = ZwQueryInformationFile(handle, &iosb, info, length, klass);
    ZwClose(handle);
    if (returned)
        *returned = (ULONG)iosb.Information;
    return status;
}

NTSTATUS NTAPI FltReadFile(PVOID instance, PFILE_OBJECT file, PLARGE_INTEGER offset, ULONG length, PVOID buffer,
                           ULONG flags, PULONG read, PVOID callback, PVOID context)
{
    if (callback)
        return STATUS_NOT_SUPPORTED;
    HANDLE handle;
    NTSTATUS status = file_handle(file, &handle);
    if (!NT_SUCCESS(status))
        return status;
    IO_STATUS_BLOCK iosb = {};
    status = ZwReadFile(handle, nullptr, nullptr, nullptr, &iosb, buffer, length, offset, nullptr);
    ZwClose(handle);
    if (read)
        *read = (ULONG)iosb.Information;
    return status;
}

NTSTATUS NTAPI DllInitialize(PUNICODE_STRING registry)
{
    ExInitializeFastMutex(&g_lock);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DllUnload(void)
{
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    return STATUS_SUCCESS;
}
