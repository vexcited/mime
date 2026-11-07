#include <mime/ddk.h>
#include "rtl.h"

/*
 * the filter manager drivers import from fltmgr.sys. this first part registers filters and answers routine lookups,
 * logging what drivers ask for that it does not have yet.
 */

#define FLT_TAG 'tlFM'

ULONG __cdecl DbgPrint(PCSTR format, ...);
extern IMAGE_DOS_HEADER __ImageBase;

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

PVOID NTAPI FltGetRoutineAddress(PCSTR name)
{
    PVOID routine = name ? own_export(name) : nullptr;
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
