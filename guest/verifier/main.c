/*
 * verifier.dll: the standard provider of the application verifier. the loader brings it into a process whose global
 * flag asks for verification (FLG_APPLICATION_VERIFIER); programs also load it by hand to look at it. no process
 * here is verified, so every export gives the answer of windows 11 26300 in a process that is not.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>

#define FLG_APPLICATION_VERIFIER 0x100
/* the runtime flag the rpc page heap exports look at. */
#define RUNTIME_FLAG_RPC_PAGE_HEAP 0x80
#define FREE_MEMORY_CALLBACKS 16
#define PROVIDER_HELPER_SIZE 0xF8
#define AVRF_ENUM_RESOURCES_FLAGS_SUSPEND 1
#define AVRF_ENUM_RESOURCES_FLAGS_DONT_RESOLVE_TRACES 2

typedef void (NTAPI *VERIFIER_STOP_ROUTINE)(ULONG_PTR code, PCSTR message, ULONG_PTR param1, PCSTR description1,
                                            ULONG_PTR param2, PCSTR description2, ULONG_PTR param3,
                                            PCSTR description3, ULONG_PTR param4, PCSTR description4);
typedef ULONG (NTAPI *AVRF_RESOURCE_ENUMERATE_CALLBACK)(PVOID description, PVOID context, PULONG level);

typedef struct {
    ULONG64 HeapHandle;
    ULONG64 UserAllocation;
    ULONG64 UserAllocationSize;
    ULONG64 Allocation;
    ULONG64 AllocationSize;
    ULONG UserAllocationState;
    ULONG HeapState;
    ULONG64 HeapContext;
    PVOID BackTraceInformation;
} AVRF_HEAP_ALLOCATION;

/* the entry RtlWalkHeap fills. */
typedef struct {
    PVOID DataAddress;
    SIZE_T DataSize;
    UCHAR OverheadBytes;
    UCHAR SegmentIndex;
    USHORT Flags;
    PVOID Reserved[4];
} WALK_ENTRY;
#define WALK_BUSY 0x0001
#define WALK_SEGMENT 0x0002

static PVOID g_free_memory_callbacks[FREE_MEMORY_CALLBACKS];
static VERIFIER_STOP_ROUTINE g_stop_routine;
static LONG g_runtime_flags;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}

static BOOLEAN verifying(void)
{
    return (NtCurrentPeb()->NtGlobalFlag & FLG_APPLICATION_VERIFIER) != 0;
}

/* the exports that read another process only take one of the width of this library. */
static BOOLEAN native_process(HANDLE process)
{
    ULONG_PTR wow64 = 0;
    if (!NT_SUCCESS(NtQueryInformationProcess(process, ProcessWow64Information, &wow64, sizeof(wow64), nullptr)))
        return TRUE;
    return wow64 == 0;
}

/* the thunk of a verified api, looked up by the address it replaces: none is replaced, so the address itself. */
PVOID NTAPI AVrfAPILookupCallback(PVOID module, PVOID name, PVOID original, LONG kind)
{
    (void)module;
    (void)name;
    return kind >= 3 ? nullptr : original;
}

NTSTATUS NTAPI VerifierAddFreeMemoryCallback(PVOID callback)
{
    if (!verifying())
        return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < FREE_MEMORY_CALLBACKS; i++)
        if (!InterlockedCompareExchangePointer(&g_free_memory_callbacks[i], callback, nullptr))
            return STATUS_SUCCESS;
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI VerifierDeleteFreeMemoryCallback(PVOID callback)
{
    if (!verifying())
        return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < FREE_MEMORY_CALLBACKS; i++)
        if (InterlockedCompareExchangePointer(&g_free_memory_callbacks[i], nullptr, callback) == callback)
            return STATUS_SUCCESS;
    return STATUS_UNSUCCESSFUL;
}

/* windows looks the block up in a page heap it never made when the process is not verified, and faults on it. */
BOOLEAN NTAPI VerifierCheckPageHeapAllocation(PVOID heap, PVOID block)
{
    (void)heap;
    (void)block;
    return *(volatile BOOLEAN *)(ULONG_PTR)8 && FALSE;
}

PVOID NTAPI VerifierCreateRpcPageHeap(ULONG flags, PVOID base, SIZE_T reserve, SIZE_T commit, PVOID lock, PVOID parameters)
{
    (void)flags;
    (void)base;
    (void)reserve;
    (void)commit;
    (void)lock;
    (void)parameters;
    return nullptr;
}

PVOID NTAPI VerifierDestroyRpcPageHeap(PVOID heap)
{
    (void)heap;
    return nullptr;
}

ULONG NTAPI VerifierDisableFaultInjectionExclusionRange(ULONG_PTR start, ULONG_PTR end)
{
    (void)start;
    (void)end;
    return 0;
}

ULONG NTAPI VerifierDisableFaultInjectionTargetRange(ULONG_PTR start, ULONG_PTR end)
{
    (void)start;
    (void)end;
    return 0;
}

ULONG NTAPI VerifierEnableFaultInjectionExclusionRange(ULONG_PTR start, ULONG_PTR end)
{
    (void)start;
    (void)end;
    return 0;
}

ULONG NTAPI VerifierEnableFaultInjectionTargetRange(ULONG_PTR start, ULONG_PTR end)
{
    (void)start;
    (void)end;
    return 0;
}

void NTAPI VerifierSetFaultInjectionProbability(ULONG kind, ULONG probability)
{
    (void)kind;
    (void)probability;
}

/* every block of every heap of this process, busy or free, as a process without page heaps has them told. */
static ULONG enumerate_heaps(AVRF_RESOURCE_ENUMERATE_CALLBACK callback, PVOID context)
{
    PVOID heaps[64];
    ULONG count = RtlGetProcessHeaps(64, heaps);
    if (count > 64)
        count = 64;
    for (ULONG i = 0; i < count; i++) {
        WALK_ENTRY entry = {};
        while (NT_SUCCESS(RtlWalkHeap(heaps[i], &entry))) {
            if (entry.Flags & WALK_SEGMENT)
                continue;
            AVRF_HEAP_ALLOCATION allocation = {};
            allocation.HeapHandle = (ULONG_PTR)heaps[i];
            allocation.UserAllocation = (ULONG_PTR)entry.DataAddress;
            allocation.UserAllocationSize = entry.DataSize;
            allocation.Allocation = (ULONG_PTR)entry.DataAddress - entry.OverheadBytes;
            allocation.AllocationSize = entry.DataSize + entry.OverheadBytes;
            allocation.UserAllocationState = (entry.Flags & WALK_BUSY) ? 1 : 2;
            ULONG level = 0;
            ULONG status = callback(&allocation, context, &level);
            if (status)
                return status;
        }
    }
    return 0;
}

ULONG NTAPI VerifierEnumerateResource(HANDLE process, ULONG flags, ULONG type, AVRF_RESOURCE_ENUMERATE_CALLBACK callback,
                                      PVOID context)
{
    if (!callback)
        return ERROR_INVALID_PARAMETER;
    if (!process)
        process = NtCurrentProcess();
    /* heap allocations and handle traces; the traces only exist in a verified process. */
    if (type == 1)
        return ERROR_INVALID_PARAMETER;
    if (type != 0)
        return ERROR_NOT_SUPPORTED;
    if (flags & ~(AVRF_ENUM_RESOURCES_FLAGS_SUSPEND | AVRF_ENUM_RESOURCES_FLAGS_DONT_RESOLVE_TRACES))
        return ERROR_INVALID_PARAMETER;
    /* a process cannot be suspended by itself, and the heaps of another one are not read. */
    if ((flags & AVRF_ENUM_RESOURCES_FLAGS_SUSPEND) || process != NtCurrentProcess())
        return ERROR_INVALID_PARAMETER;
    return enumerate_heaps(callback, context);
}

NTSTATUS NTAPI VerifierForceNormalHeap(HANDLE process, PVOID verifier)
{
    (void)verifier;
    return native_process(process) ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI VerifierGetInfoForException(HANDLE process, PVOID verifier, PEXCEPTION_RECORD record, PBOOLEAN found,
                                           PULONG_PTR kind)
{
    (void)verifier;
    *found = FALSE;
    if (!native_process(process))
        return STATUS_NOT_SUPPORTED;
    *found = TRUE;
    if (kind)
        *kind = record->ExceptionCode == STATUS_ACCESS_VIOLATION ? 2 : 1;
    return STATUS_SUCCESS;
}

/* the ranges of this library a dump of the process should carry: its stop data, one range. */
NTSTATUS NTAPI VerifierGetMemoryForDump(HANDLE process, PVOID verifier, PULONG_PTR ranges, PULONG count)
{
    static ULONG_PTR stop_data[16];
    extern IMAGE_DOS_HEADER __ImageBase;
    if (!native_process(process))
        return STATUS_NOT_SUPPORTED;
    if (*count == 0) {
        *count = 1;
        return STATUS_BUFFER_TOO_SMALL;
    }
    ranges[0] = (ULONG_PTR)stop_data - (ULONG_PTR)&__ImageBase + (ULONG_PTR)verifier;
    ranges[1] = sizeof(stop_data);
    *count = 1;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI VerifierGetPropertyValueByName(PVOID provider, PVOID name, PVOID value, PVOID size)
{
    (void)provider;
    (void)name;
    (void)value;
    (void)size;
    return STATUS_NOT_FOUND;
}

/* the routine other providers set the stop of a thread through: no thread is tracked, nothing is kept. */
static void NTAPI provider_set_stop(PVOID stop)
{
    (void)stop;
}

/* the table of helpers handed to other providers: its size, then routines that stay unset outside verification. */
NTSTATUS NTAPI VerifierGetProviderHelper(PULONG helper)
{
    if (!helper)
        return STATUS_INVALID_PARAMETER;
    union {
        UCHAR Bytes[PROVIDER_HELPER_SIZE];
        ULONG Size;
    } table = {};
    ULONG size = *helper < PROVIDER_HELPER_SIZE ? *helper : PROVIDER_HELPER_SIZE;
    table.Size = size;
    PVOID routine = provider_set_stop;
    memcpy(table.Bytes + PROVIDER_HELPER_SIZE - sizeof(PVOID), &routine, sizeof(routine));
    /* the size is written even when it was asked smaller than itself. */
    memcpy(helper, table.Bytes, size < sizeof(ULONG) ? sizeof(ULONG) : size);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI VerifierIsAddressInAnyPageHeap(HANDLE process, PVOID verifier, PVOID address, PBOOLEAN inside)
{
    (void)process;
    (void)verifier;
    (void)address;
    *inside = FALSE;
    return STATUS_SUCCESS;
}

BOOL NTAPI VerifierIsCurrentThreadHoldingLocks(void)
{
    return FALSE;
}

BOOL NTAPI VerifierIsDllEntryActive(PVOID reserved)
{
    (void)reserved;
    return FALSE;
}

BOOL NTAPI VerifierIsPerUserSettingsEnabled(void)
{
    return USER_SHARED_DATA->ImageFileExecutionOptions & 1;
}

NTSTATUS NTAPI VerifierQueryRuntimeFlags(PULONG verifying_now, PULONG flags)
{
    if (!verifying_now || !flags)
        return STATUS_INVALID_PARAMETER;
    *verifying_now = verifying();
    *flags = verifying() ? (ULONG)g_runtime_flags : 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI VerifierSetRuntimeFlags(ULONG flags)
{
    if (!verifying())
        return STATUS_INVALID_PARAMETER;
    if (flags & RUNTIME_FLAG_RPC_PAGE_HEAP)
        InterlockedOr(&g_runtime_flags, RUNTIME_FLAG_RPC_PAGE_HEAP);
    else
        InterlockedAnd(&g_runtime_flags, ~RUNTIME_FLAG_RPC_PAGE_HEAP);
    return STATUS_SUCCESS;
}

/* the flags are kept under the image file execution options of the named program, which nothing here reads. */
NTSTATUS NTAPI VerifierSetFlags(PUNICODE_STRING program, ULONG flags, PVOID details)
{
    (void)program;
    (void)flags;
    (void)details;
    return STATUS_INVALID_PARAMETER;
}

void NTAPI VerifierRedirectStopFunctions(VERIFIER_STOP_ROUTINE routine)
{
    g_stop_routine = routine;
}

void NTAPI VerifierStopMessage(ULONG_PTR code, PCSTR message, ULONG_PTR param1, PCSTR description1, ULONG_PTR param2,
                               PCSTR description2, ULONG_PTR param3, PCSTR description3, ULONG_PTR param4,
                               PCSTR description4)
{
    if (g_stop_routine) {
        g_stop_routine(code, message, param1, description1, param2, description2, param3, description3, param4,
                       description4);
        return;
    }
    DbgPrint("VERIFIER STOP %p: %s\n\t%p : %s\n\t%p : %s\n\t%p : %s\n\t%p : %s\n", (PVOID)code, message,
             (PVOID)param1, description1, (PVOID)param2, description2, (PVOID)param3, description3, (PVOID)param4,
             description4);
    DbgBreakPoint();
}
