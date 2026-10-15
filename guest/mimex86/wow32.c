/* wow64 process structures and system-call argument conversion. */
#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>
#include <mime/wow32.h>
#include <mime/condrv.h>
#include "mime_svc_nt.h"
#include "x86.h"

/* advapi32_private.h declares it for the dlls that use it. */
NTSYSCALLAPI NTSTATUS NTAPI NtQuerySecurityObject(HANDLE handle, ULONG info, PVOID descriptor, ULONG length,
                                                  PULONG needed);

static PVOID pointer32(ULONG value)
{
    return (PVOID)(ULONG_PTR)value;
}

static HANDLE handle32(ULONG value)
{
    return (HANDLE)(LONG_PTR)(LONG)value;
}

static UNICODE_STRING string32(ULONG address)
{
    const MIME_STRING32 *s = pointer32(address);
    return (UNICODE_STRING){s->Length, s->MaximumLength, pointer32(s->Buffer)};
}

static OBJECT_ATTRIBUTES attributes32(ULONG address, UNICODE_STRING *name)
{
    const MIME_OBJECT_ATTRIBUTES32 *a = pointer32(address);
    OBJECT_ATTRIBUTES out = {sizeof(out)};
    if (!a)
        return out;
    out.RootDirectory = handle32(a->RootDirectory);
    out.Attributes = a->Attributes;
    if (a->ObjectName) {
        *name = string32(a->ObjectName);
        out.ObjectName = name;
    }
    out.SecurityDescriptor = pointer32(a->SecurityDescriptor);
    out.SecurityQualityOfService = pointer32(a->SecurityQualityOfService);
    return out;
}

static void store32(ULONG address, ULONG_PTR value)
{
    if (address)
        *(PULONG)pointer32(address) = (ULONG)value;
}

static void store_iosb32(ULONG address, const IO_STATUS_BLOCK *io)
{
    if (address) {
        MIME_IO_STATUS32 *out = pointer32(address);
        out->Status = io->Status;
        out->Information = (ULONG)io->Information;
    }
}

static PVOID allocate32(SIZE_T size)
{
    PVOID base = NULL;
    NTSTATUS status = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 32, &size,
                                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(status))
        return NULL;
    if ((ULONG_PTR)base + size > 0x80000000ull) {
        SIZE_T release = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &base, &release, MEM_RELEASE);
        return NULL;
    }
    return base;
}

static MIME_STRING32 copy_string32(PUCHAR *cursor, const UNICODE_STRING *source)
{
    PWSTR text = (PWSTR)*cursor;
    memcpy(text, source->Buffer, source->Length);
    text[source->Length / sizeof(WCHAR)] = 0;
    *cursor += source->Length + sizeof(WCHAR);
    return (MIME_STRING32){source->Length, (USHORT)(source->Length + sizeof(WCHAR)), (ULONG)(ULONG_PTR)text};
}

/* fills a 32-bit peb from the native one, its process parameters and environment in memory below 2 GiB. */
BOOLEAN MxFillPeb32(PVOID memory)
{
    PPEB native = NtCurrentPeb();
    PRTL_USER_PROCESS_PARAMETERS pp = native->ProcessParameters;
    SIZE_T strings = pp->ImagePathName.Length + pp->CommandLine.Length + pp->CurrentDirectory.DosPath.Length +
                     pp->WindowTitle.Length + pp->DesktopInfo.Length + pp->ShellInfo.Length + pp->RuntimeData.Length + 32;
    SIZE_T size = sizeof(MIME_PARAMETERS32) + strings + pp->EnvironmentSize;
    MIME_PARAMETERS32 *out = allocate32(size);
    if (!out)
        return FALSE;
    MIME_PEB32 *peb = memory;
    PUCHAR cursor = (void *)(out + 1);
    peb->ImageBaseAddress = (ULONG)(ULONG_PTR)native->ImageBaseAddress;
    peb->ProcessParameters = (ULONG)(ULONG_PTR)out;
    peb->NumberOfProcessors = native->NumberOfProcessors;
    peb->OSMajorVersion = native->OSMajorVersion;
    peb->OSMinorVersion = native->OSMinorVersion;
    peb->OSBuildNumber = native->OSBuildNumber;
    peb->OSPlatformId = native->OSPlatformId;
    peb->ImageSubsystem = native->ImageSubsystem;
    peb->ImageSubsystemMajorVersion = native->ImageSubsystemMajorVersion;
    peb->ImageSubsystemMinorVersion = native->ImageSubsystemMinorVersion;
    peb->ActiveProcessAffinityMask = (ULONG)native->ActiveProcessAffinityMask;
    peb->CriticalSectionTimeout = native->CriticalSectionTimeout;
    peb->HeapSegmentReserve = (ULONG)native->HeapSegmentReserve;
    peb->HeapSegmentCommit = (ULONG)native->HeapSegmentCommit;
    peb->HeapDeCommitTotalFreeThreshold = (ULONG)native->HeapDeCommitTotalFreeThreshold;
    peb->HeapDeCommitFreeBlockThreshold = (ULONG)native->HeapDeCommitFreeBlockThreshold;
    peb->MaximumNumberOfHeaps = native->MaximumNumberOfHeaps;
    peb->SessionId = native->SessionId;
    out->MaximumLength = out->Length = sizeof(*out);
    out->Flags = RTL_USER_PROC_PARAMS_NORMALIZED;
    out->ConsoleHandle = (ULONG)(ULONG_PTR)pp->ConsoleHandle;
    out->StandardInput = (ULONG)(ULONG_PTR)pp->StandardInput;
    out->StandardOutput = (ULONG)(ULONG_PTR)pp->StandardOutput;
    out->StandardError = (ULONG)(ULONG_PTR)pp->StandardError;
    out->ShowWindowFlags = pp->ShowWindowFlags;
    out->ImagePathName = copy_string32(&cursor, &pp->ImagePathName);
    out->CommandLine = copy_string32(&cursor, &pp->CommandLine);
    out->CurrentDirectory = copy_string32(&cursor, &pp->CurrentDirectory.DosPath);
    out->WindowTitle = copy_string32(&cursor, &pp->WindowTitle);
    out->DesktopInfo = copy_string32(&cursor, &pp->DesktopInfo);
    out->ShellInfo = copy_string32(&cursor, &pp->ShellInfo);
    out->RuntimeData = copy_string32(&cursor, &pp->RuntimeData);
    out->Environment = (ULONG)(ULONG_PTR)cursor;
    out->EnvironmentSize = (ULONG)pp->EnvironmentSize;
    memcpy(cursor, pp->Environment, pp->EnvironmentSize);
    return TRUE;
}

PVOID MxCreatePeb32(void)
{
    PVOID peb = allocate32(0x1000);
    return peb && MxFillPeb32(peb) ? peb : NULL;
}

PVOID MxCreateTeb32(PVOID peb)
{
    PULONG teb = allocate32(MIME_TEB_SIZE);
    if (!teb)
        return NULL;
    PTEB native = NtCurrentTeb();
    teb[0] = 0xFFFFFFFFu;
    teb[1] = (ULONG)(ULONG_PTR)native->NtTib.StackBase;
    teb[2] = (ULONG)(ULONG_PTR)native->NtTib.StackLimit;
    teb[MIME_TEB32_SELF / 4] = (ULONG)(ULONG_PTR)teb;
    teb[MIME_TEB32_CLIENT_ID / 4] = (ULONG)(ULONG_PTR)native->ClientId.UniqueProcess;
    teb[MIME_TEB32_CLIENT_ID / 4 + 1] = (ULONG)(ULONG_PTR)native->ClientId.UniqueThread;
    teb[MIME_TEB32_REAL_CLIENT_ID / 4] = (ULONG)(ULONG_PTR)native->ClientId.UniqueProcess;
    teb[MIME_TEB32_REAL_CLIENT_ID / 4 + 1] = (ULONG)(ULONG_PTR)native->ClientId.UniqueThread;
    teb[MIME_TEB32_PEB / 4] = (ULONG)(ULONG_PTR)peb;
    teb[MIME_TEB32_LOCALE / 4] = native->CurrentLocale;
    teb[MIME_TEB32_DEALLOCATION_STACK / 4] = teb[2];
    MIME_STRING32 *s = (void *)((PUCHAR)teb + MIME_TEB32_STATIC_STRING);
    s->MaximumLength = 522;
    s->Buffer = (ULONG)(ULONG_PTR)teb + MIME_TEB32_STATIC_BUFFER;
    teb[MIME_TEB32_TLS_LINKS / 4] = teb[MIME_TEB32_TLS_LINKS / 4 + 1] = (ULONG)(ULONG_PTR)teb + MIME_TEB32_TLS_LINKS;
    return teb;
}

PVOID MxCurrentPeb32(void);
PVOID MxCurrentTeb32(void);

ULONG Mx32Win32kCall(ULONG number, const ULONG *arguments);

typedef struct {
    LARGE_INTEGER KernelTime, UserTime, CreateTime;
    ULONG WaitTime, StartAddress, UniqueProcess, UniqueThread;
    LONG Priority, BasePriority;
    ULONG ContextSwitches, ThreadState, WaitReason;
} THREAD_INFORMATION32;

typedef struct {
    ULONG NextEntryOffset, NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount, NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime, UserTime, KernelTime;
    MIME_STRING32 ImageName;
    LONG BasePriority;
    ULONG UniqueProcessId, InheritedFromUniqueProcessId, HandleCount, SessionId, UniqueProcessKey;
    ULONG PeakVirtualSize, VirtualSize, PageFaultCount, PeakWorkingSetSize, WorkingSetSize;
    ULONG QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage;
    ULONG PagefileUsage, PeakPagefileUsage, PrivatePageCount;
    LARGE_INTEGER IoCounters[6];
} PROCESS_INFORMATION32;

_Static_assert(sizeof(THREAD_INFORMATION32) == 0x40, "x86 thread record");
_Static_assert(sizeof(PROCESS_INFORMATION32) == 0xB8, "x86 process record");

/* converts the native process list into the x86 layout, threads and names included. */
static NTSTATUS query_processes32(ULONG out, ULONG length, ULONG returned)
{
    SIZE_T size = 0x10000;
    PVOID native;
    NTSTATUS status;
    for (;;) {
        native = NULL;
        SIZE_T alloc = size;
        status = NtAllocateVirtualMemory(NtCurrentProcess(), &native, 0, &alloc, MEM_RESERVE | MEM_COMMIT,
                                         PAGE_READWRITE);
        if (!NT_SUCCESS(status))
            return status;
        ULONG need = 0;
        status = NtQuerySystemInformation(SystemProcessInformation, native, (ULONG)size, &need);
        if (status != STATUS_INFO_LENGTH_MISMATCH || size >= 0x1000000)
            break;
        SIZE_T release = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &native, &release, MEM_RELEASE);
        size = need > size ? need + 0x1000 : size * 2;
    }
    ULONG total = 0;
    if (NT_SUCCESS(status)) {
        /* first pass sizes, second pass writes when the caller's buffer holds everything. */
        for (int pass = 0; pass < 2 && NT_SUCCESS(status); pass++) {
            ULONG used = 0;
            PROCESS_INFORMATION32 *last = NULL;
            for (SYSTEM_PROCESS_INFORMATION *p = native;;
                 p = (SYSTEM_PROCESS_INFORMATION *)((PUCHAR)p + p->NextEntryOffset)) {
                ULONG name = p->ImageName.Length ? p->ImageName.Length + sizeof(WCHAR) : 0;
                ULONG record = (sizeof(PROCESS_INFORMATION32) + p->NumberOfThreads * sizeof(THREAD_INFORMATION32) +
                                name + 7) & ~7u;
                if (pass) {
                    PROCESS_INFORMATION32 *q = pointer32(out + used);
                    memset(q, 0, record);
                    q->NumberOfThreads = p->NumberOfThreads;
                    q->NumberOfThreadsHighWatermark = p->NumberOfThreadsHighWatermark;
                    q->CreateTime = p->CreateTime;
                    q->BasePriority = p->BasePriority;
                    q->UniqueProcessId = (ULONG)(ULONG_PTR)p->UniqueProcessId;
                    q->InheritedFromUniqueProcessId = (ULONG)(ULONG_PTR)p->InheritedFromUniqueProcessId;
                    q->HandleCount = p->HandleCount;
                    q->SessionId = p->SessionId;
                    q->UniqueProcessKey = (ULONG)p->UniqueProcessKey;
                    SYSTEM_THREAD_INFORMATION *t = (SYSTEM_THREAD_INFORMATION *)(p + 1);
                    THREAD_INFORMATION32 *u = (THREAD_INFORMATION32 *)(q + 1);
                    for (ULONG i = 0; i < p->NumberOfThreads; i++, t++, u++) {
                        u->CreateTime = t->CreateTime;
                        u->StartAddress = (ULONG)(ULONG_PTR)t->StartAddress;
                        u->UniqueProcess = (ULONG)(ULONG_PTR)t->ClientId.UniqueProcess;
                        u->UniqueThread = (ULONG)(ULONG_PTR)t->ClientId.UniqueThread;
                        u->Priority = t->Priority;
                        u->BasePriority = t->BasePriority;
                        u->ThreadState = t->ThreadState;
                        u->WaitReason = t->WaitReason;
                    }
                    if (name) {
                        memcpy(u, p->ImageName.Buffer, p->ImageName.Length);
                        q->ImageName.Length = p->ImageName.Length;
                        q->ImageName.MaximumLength = (USHORT)name;
                        q->ImageName.Buffer = out + used + (ULONG)((PUCHAR)u - (PUCHAR)q);
                    }
                    if (last)
                        last->NextEntryOffset = (ULONG)((PUCHAR)q - (PUCHAR)last);
                    last = q;
                }
                used += record;
                if (!p->NextEntryOffset)
                    break;
            }
            total = used;
            if (!pass && total > length)
                status = STATUS_INFO_LENGTH_MISMATCH;
        }
    }
    SIZE_T release = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &native, &release, MEM_RELEASE);
    if (NT_SUCCESS(status) || status == STATUS_INFO_LENGTH_MISMATCH)
        store32(returned, total);
    return status;
}

static BOOLEAN synchronous_file(HANDLE file)
{
    IO_STATUS_BLOCK io;
    ULONG mode = 0;
    return NT_SUCCESS(NtQueryInformationFile(file, &io, &mode, sizeof(mode), FileModeInformation)) &&
           (mode & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT));
}

NTSTATUS Mx32SystemCall(ULONG number, const ULONG *a)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK io = {0};
    HANDLE handle = NULL;
    NTSTATUS status;
    if ((number >> 12) == 1)
        return (NTSTATUS)Mx32Win32kCall(number, a);
    switch (number) {
    case MIME_SVC_NtTerminateProcess:
        return NtTerminateProcess(handle32(a[0]), a[1]);
    case MIME_SVC_NtTerminateThread:
        return NtTerminateThread(handle32(a[0]), a[1]);
    case MIME_SVC_NtClose:
        return NtClose(handle32(a[0]));
    case MIME_SVC_NtTestAlert:
        return NtTestAlert();
    case MIME_SVC_NtDisplayString:
        name = string32(a[0]);
        return NtDisplayString(&name);
    case MIME_SVC_NtAllocateVirtualMemory: {
        PVOID base = pointer32(*(PULONG)pointer32(a[1]));
        SIZE_T size = *(PULONG)pointer32(a[3]);
        if ((ULONG_PTR)base + size > 0x7FFF0000ull)
            return STATUS_NO_MEMORY;
        status = NtAllocateVirtualMemory(handle32(a[0]), &base, 32, &size, a[4], a[5]);
        if (NT_SUCCESS(status) && (ULONG_PTR)base + size > 0x7FFF0000ull) {
            SIZE_T release = 0;
            NtFreeVirtualMemory(handle32(a[0]), &base, &release, MEM_RELEASE);
            return STATUS_NO_MEMORY;
        }
        if (NT_SUCCESS(status)) {
            store32(a[1], (ULONG_PTR)base);
            store32(a[3], size);
        }
        return status;
    }
    case MIME_SVC_NtFreeVirtualMemory: {
        PVOID base = pointer32(*(PULONG)pointer32(a[1]));
        SIZE_T size = *(PULONG)pointer32(a[2]);
        status = NtFreeVirtualMemory(handle32(a[0]), &base, &size, a[3]);
        if (NT_SUCCESS(status)) {
            store32(a[1], (ULONG_PTR)base);
            store32(a[2], size);
        }
        return status;
    }
    case MIME_SVC_NtProtectVirtualMemory: {
        PVOID base = pointer32(*(PULONG)pointer32(a[1]));
        SIZE_T size = *(PULONG)pointer32(a[2]);
        status = NtProtectVirtualMemory(handle32(a[0]), &base, &size, a[3], pointer32(a[4]));
        if (NT_SUCCESS(status)) {
            store32(a[1], (ULONG_PTR)base);
            store32(a[2], size);
        }
        return status;
    }
    case MIME_SVC_NtCreateUserProcess: {
        if (a[4] || a[5] || a[9] || a[10])
            return STATUS_NOT_SUPPORTED;
        MIME_PARAMETERS32 *p = pointer32(a[8]);
        RTL_USER_PROCESS_PARAMETERS pp = {0};
        pp.MaximumLength = pp.Length = sizeof(pp);
        pp.Flags = RTL_USER_PROC_PARAMS_NORMALIZED;
        pp.ImagePathName = string32((ULONG)(ULONG_PTR)&p->ImagePathName);
        pp.CommandLine = string32((ULONG)(ULONG_PTR)&p->CommandLine);
        pp.CurrentDirectory.DosPath = string32((ULONG)(ULONG_PTR)&p->CurrentDirectory);
        HANDLE process = NULL, thread = NULL;
        status = NtCreateUserProcess(&process, &thread, a[2], a[3], NULL, NULL, a[6], a[7], &pp, NULL, NULL);
        if (NT_SUCCESS(status)) {
            store32(a[0], (ULONG_PTR)process);
            store32(a[1], (ULONG_PTR)thread);
        }
        return status;
    }
    case MIME_SVC_NtCreateThreadEx:
        if (a[10])
            return STATUS_NOT_SUPPORTED;
        oa = attributes32(a[2], &name);
        status = NtCreateThreadEx(&handle, a[1], a[2] ? &oa : NULL, handle32(a[3]), pointer32(a[4]),
                                  pointer32(a[5]), a[6], a[7], a[8], a[9], NULL);
        if (NT_SUCCESS(status))
            store32(a[0], (ULONG_PTR)handle);
        return status;
    case MIME_SVC_NtQueueApcThread:
        return NtQueueApcThread(handle32(a[0]), pointer32(a[1]), pointer32(a[2]), pointer32(a[3]), pointer32(a[4]));
    case MIME_SVC_NtResumeThread:
        return NtResumeThread(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtSuspendThread:
        return NtSuspendThread(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtCreateEvent:
        oa = attributes32(a[2], &name);
        status = NtCreateEvent(&handle, a[1], a[2] ? &oa : NULL, a[3], a[4]);
        if (NT_SUCCESS(status))
            store32(a[0], (ULONG_PTR)handle);
        return status;
    case MIME_SVC_NtCreateMutant:
    case MIME_SVC_NtOpenMutant:
        oa = attributes32(a[2], &name);
        if (number == MIME_SVC_NtCreateMutant)
            status = NtCreateMutant(&handle, a[1], a[2] ? &oa : NULL, a[3]);
        else
            status = NtOpenMutant(&handle, a[1], a[2] ? &oa : NULL);
        if (NT_SUCCESS(status))
            store32(a[0], (ULONG_PTR)handle);
        return status;
    case MIME_SVC_NtReleaseMutant:
        return NtReleaseMutant(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtSetEvent:
        return NtSetEvent(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtResetEvent:
        return NtResetEvent(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtYieldExecution:
        return NtYieldExecution();
    case MIME_SVC_NtQuerySecurityObject:
        /* a self relative descriptor has no pointers, so the buffer passes through. */
        return NtQuerySecurityObject(handle32(a[0]), a[1], pointer32(a[2]), a[3], pointer32(a[4]));
    case MIME_SVC_NtWaitForSingleObject:
        return NtWaitForSingleObject(handle32(a[0]), a[1], pointer32(a[2]));
    case MIME_SVC_NtWaitForMultipleObjects: {
        /* the handle array holds 32-bit handles, sign extended like every other x86 handle. */
        if (!a[0] || a[0] > 64)
            return STATUS_INVALID_PARAMETER_1;
        HANDLE handles[64]; /* MAXIMUM_WAIT_OBJECTS */
        const ULONG *source = pointer32(a[1]);
        for (ULONG i = 0; i < a[0]; i++)
            handles[i] = handle32(source[i]);
        return NtWaitForMultipleObjects(a[0], handles, a[2], (BOOLEAN)a[3], pointer32(a[4]));
    }
    case MIME_SVC_NtDeviceIoControlFile:
        /* only the console protocol has fixed-width buffers and synchronous completion. */
        if (a[2] || a[5] < IOCTL_CONDRV_STATE || a[5] > IOCTL_CONDRV_SYNC_EVENT || (a[5] & 3))
            return STATUS_NOT_SUPPORTED;
        status = NtDeviceIoControlFile(handle32(a[0]), handle32(a[1]), NULL, pointer32(a[3]), &io,
                                       a[5], pointer32(a[6]), a[7], pointer32(a[8]), a[9]);
        store_iosb32(a[4], &io);
        return status;
    case MIME_SVC_NtReadFile:
    case MIME_SVC_NtWriteFile:
        if (a[2] || !synchronous_file(handle32(a[0])))
            return STATUS_NOT_SUPPORTED;
        if (number == MIME_SVC_NtReadFile)
            status = NtReadFile(handle32(a[0]), handle32(a[1]), NULL, pointer32(a[3]), &io,
                                pointer32(a[5]), a[6], pointer32(a[7]), pointer32(a[8]));
        else
            status = NtWriteFile(handle32(a[0]), handle32(a[1]), NULL, pointer32(a[3]), &io,
                                 pointer32(a[5]), a[6], pointer32(a[7]), pointer32(a[8]));
        store_iosb32(a[4], &io);
        return status;
    case MIME_SVC_NtQueryDirectoryFile:
    case MIME_SVC_NtQueryDirectoryFileEx:
        if (a[2] || !synchronous_file(handle32(a[0])))
            return STATUS_NOT_SUPPORTED;
        name = a[9] ? string32(a[9]) : (UNICODE_STRING){0};
        if (number == MIME_SVC_NtQueryDirectoryFile)
            status = NtQueryDirectoryFile(handle32(a[0]), handle32(a[1]), NULL, pointer32(a[3]), &io,
                                           pointer32(a[5]), a[6], a[7], a[8], a[9] ? &name : NULL, a[10]);
        else
            status = NtQueryDirectoryFileEx(handle32(a[0]), handle32(a[1]), NULL, pointer32(a[3]), &io,
                                             pointer32(a[5]), a[6], a[7], a[8], a[9] ? &name : NULL);
        store_iosb32(a[4], &io);
        return status;
    case MIME_SVC_NtQueryDefaultLocale:
        return NtQueryDefaultLocale(a[0], pointer32(a[1]));
    case MIME_SVC_NtQueryDefaultUILanguage:
        return NtQueryDefaultUILanguage(pointer32(a[0]));
    case MIME_SVC_NtOpenKey:
    case MIME_SVC_NtOpenKeyEx:
    case MIME_SVC_NtCreateKey: {
        oa = attributes32(a[2], &name);
        if (number == MIME_SVC_NtCreateKey) {
            UNICODE_STRING klass = a[4] ? string32(a[4]) : (UNICODE_STRING){0};
            status = NtCreateKey(&handle, a[1], &oa, a[3], a[4] ? &klass : NULL, a[5], pointer32(a[6]));
        } else if (number == MIME_SVC_NtOpenKeyEx) {
            status = NtOpenKeyEx(&handle, a[1], &oa, a[3]);
        } else {
            status = NtOpenKey(&handle, a[1], &oa);
        }
        if (NT_SUCCESS(status))
            store32(a[0], (ULONG_PTR)handle);
        return status;
    }
    case MIME_SVC_NtQueryKey:
        return NtQueryKey(handle32(a[0]), a[1], pointer32(a[2]), a[3], pointer32(a[4]));
    case MIME_SVC_NtQueryValueKey:
        name = string32(a[1]);
        return NtQueryValueKey(handle32(a[0]), &name, a[2], pointer32(a[3]), a[4], pointer32(a[5]));
    case MIME_SVC_NtEnumerateKey:
        return NtEnumerateKey(handle32(a[0]), a[1], a[2], pointer32(a[3]), a[4], pointer32(a[5]));
    case MIME_SVC_NtEnumerateValueKey:
        return NtEnumerateValueKey(handle32(a[0]), a[1], a[2], pointer32(a[3]), a[4], pointer32(a[5]));
    case MIME_SVC_NtSetValueKey:
        name = string32(a[1]);
        return NtSetValueKey(handle32(a[0]), &name, a[2], a[3], pointer32(a[4]), a[5]);
    case MIME_SVC_NtDeleteKey:
        return NtDeleteKey(handle32(a[0]));
    case MIME_SVC_NtDeleteValueKey:
        name = string32(a[1]);
        return NtDeleteValueKey(handle32(a[0]), &name);
    case MIME_SVC_NtFlushBuffersFile:
        status = NtFlushBuffersFile(handle32(a[0]), &io);
        store_iosb32(a[1], &io);
        return status;
    case MIME_SVC_NtFlushKey:
        return NtFlushKey(handle32(a[0]));
    case MIME_SVC_NtOpenProcessToken:
    case MIME_SVC_NtOpenProcessTokenEx:
        if (number == MIME_SVC_NtOpenProcessToken)
            status = NtOpenProcessToken(handle32(a[0]), a[1], &handle);
        else
            status = NtOpenProcessTokenEx(handle32(a[0]), a[1], a[2], &handle);
        if (NT_SUCCESS(status))
            store32(a[number == MIME_SVC_NtOpenProcessToken ? 2 : 3], (ULONG_PTR)handle);
        return status;
    case MIME_SVC_NtOpenThreadToken:
    case MIME_SVC_NtOpenThreadTokenEx:
        if (number == MIME_SVC_NtOpenThreadToken)
            status = NtOpenThreadToken(handle32(a[0]), a[1], (BOOLEAN)a[2], &handle);
        else
            status = NtOpenThreadTokenEx(handle32(a[0]), a[1], (BOOLEAN)a[2], a[3], &handle);
        if (NT_SUCCESS(status))
            store32(a[number == MIME_SVC_NtOpenThreadToken ? 3 : 4], (ULONG_PTR)handle);
        return status;
    case MIME_SVC_NtQueryInformationToken: {
        /* token user, owner, primary group and integrity label contain a sid pointer. */
        if (a[1] == 1 || a[1] == 4 || a[1] == 5 || a[1] == 25) {
            ULONG64 buffer[128];
            ULONG length = 0;
            status = NtQueryInformationToken(handle32(a[0]), a[1], buffer, sizeof(buffer), &length);
            if (!NT_SUCCESS(status))
                return status;
            PVOID sid = *(PVOID *)buffer;
            ULONG bytes = 8 + 4 * ((PUCHAR)sid)[1];
            ULONG header = a[1] == 1 || a[1] == 25 ? 8 : 4;
            store32(a[4], header + bytes);
            if (a[3] < header + bytes)
                return STATUS_BUFFER_TOO_SMALL;
            PULONG out = pointer32(a[2]);
            out[0] = a[2] + header;
            if (header == 8)
                out[1] = ((PULONG)buffer)[2];
            memcpy((PUCHAR)out + header, sid, bytes);
            return STATUS_SUCCESS;
        }
        /* groups, restricted sids and the logon sid: a count, sid pointers with attributes, then the sids. */
        if (a[1] == 2 || a[1] == 11 || a[1] == 28) {
            ULONG64 buffer[1024];
            ULONG length = 0;
            status = NtQueryInformationToken(handle32(a[0]), a[1], buffer, sizeof(buffer), &length);
            if (!NT_SUCCESS(status))
                return status;
            const ULONG count = *(PULONG)buffer;
            const struct {
                PVOID Sid;
                ULONG Attributes;
            } *groups = (const void *)((PUCHAR)buffer + sizeof(PVOID));
            ULONG need = 4 + 8 * count;
            for (ULONG i = 0; i < count; i++)
                need += 8 + 4 * ((PUCHAR)groups[i].Sid)[1];
            store32(a[4], need);
            if (a[3] < need)
                return STATUS_BUFFER_TOO_SMALL;
            PULONG out = pointer32(a[2]);
            ULONG at = 4 + 8 * count;
            out[0] = count;
            for (ULONG i = 0; i < count; i++) {
                ULONG bytes = 8 + 4 * ((PUCHAR)groups[i].Sid)[1];
                out[1 + 2 * i] = a[2] + at;
                out[2 + 2 * i] = groups[i].Attributes;
                memcpy((PUCHAR)out + at, groups[i].Sid, bytes);
                at += bytes;
            }
            return STATUS_SUCCESS;
        }
        /* pointer-free token classes supported by the native token provider. */
        if (a[1] == 3 || a[1] == 7 || a[1] == 8 || a[1] == 10 || a[1] == 12 ||
            a[1] == 18 || a[1] == 20 || a[1] == 29)
            return NtQueryInformationToken(handle32(a[0]), a[1], pointer32(a[2]), a[3], pointer32(a[4]));
        return STATUS_NOT_IMPLEMENTED;
    }
    case MIME_SVC_NtOpenDirectoryObject:
        oa = attributes32(a[2], &name);
        status = NtOpenDirectoryObject(&handle, a[1], &oa);
        if (NT_SUCCESS(status))
            store32(a[0], (ULONG_PTR)handle);
        return status;
    case MIME_SVC_NtOpenSymbolicLinkObject:
        oa = attributes32(a[2], &name);
        status = NtOpenSymbolicLinkObject(&handle, a[1], &oa);
        if (NT_SUCCESS(status))
            store32(a[0], (ULONG_PTR)handle);
        return status;
    case MIME_SVC_NtCreateSymbolicLinkObject: {
        oa = attributes32(a[2], &name);
        UNICODE_STRING target = {0};
        if (a[3])
            target = string32(a[3]);
        status = NtCreateSymbolicLinkObject(&handle, a[1], &oa, a[3] ? &target : NULL);
        if (NT_SUCCESS(status))
            store32(a[0], (ULONG_PTR)handle);
        return status;
    }
    case MIME_SVC_NtMakeTemporaryObject:
        return NtMakeTemporaryObject(handle32(a[0]));
    case MIME_SVC_NtQuerySymbolicLinkObject: {
        name = string32(a[1]);
        status = NtQuerySymbolicLinkObject(handle32(a[0]), &name, pointer32(a[2]));
        ((MIME_STRING32 *)pointer32(a[1]))->Length = name.Length;
        return status;
    }
    case MIME_SVC_NtSetInformationFile:
        switch (a[4]) {
        case FileRenameInformation: {
            const ULONG *p = pointer32(a[2]);
            if (!p || a[3] < 12 || (p[2] & 1) || p[2] > a[3] - 12 || p[2] > 65534)
                return STATUS_INVALID_PARAMETER;
            struct RENAME {
                BOOLEAN Replace;
                HANDLE Root;
                ULONG Length;
                WCHAR Name[1];
            };
            SIZE_T size = offsetof(struct RENAME, Name) + p[2];
            PVOID memory = NULL;
            SIZE_T allocation = size;
            status = NtAllocateVirtualMemory(NtCurrentProcess(), &memory, 0, &allocation,
                                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (!NT_SUCCESS(status))
                return status;
            struct RENAME *info = memory;
            info->Replace = *(BYTE *)p;
            info->Root = handle32(p[1]);
            info->Length = p[2];
            memcpy(info->Name, p + 3, p[2]);
            status = NtSetInformationFile(handle32(a[0]), &io, info, (ULONG)size, a[4]);
            allocation = 0;
            NtFreeVirtualMemory(NtCurrentProcess(), &memory, &allocation, MEM_RELEASE);
            store_iosb32(a[1], &io);
            return status;
        }
        case FileBasicInformation:
        case FileDispositionInformation:
        case FilePositionInformation:
        case FileModeInformation:
        case FileAllocationInformation:
        case FileEndOfFileInformation:
        case FilePipeInformation:
        case FileDispositionInformationEx:
            status = NtSetInformationFile(handle32(a[0]), &io, pointer32(a[2]), a[3], a[4]);
            store_iosb32(a[1], &io);
            return status;
        default:
            return STATUS_NOT_IMPLEMENTED;
        }
    case MIME_SVC_NtQueryInformationFile:
        status = NtQueryInformationFile(handle32(a[0]), &io, pointer32(a[2]), a[3], a[4]);
        store_iosb32(a[1], &io);
        return status;
    case MIME_SVC_NtQueryVolumeInformationFile:
        status = NtQueryVolumeInformationFile(handle32(a[0]), &io, pointer32(a[2]), a[3], a[4]);
        store_iosb32(a[1], &io);
        return status;
    case MIME_SVC_NtQueryAttributesFile:
        oa = attributes32(a[0], &name);
        return NtQueryAttributesFile(&oa, pointer32(a[1]));
    case MIME_SVC_NtQueryFullAttributesFile:
        oa = attributes32(a[0], &name);
        return NtQueryFullAttributesFile(&oa, pointer32(a[1]));
    case MIME_SVC_NtOpenFile:
    case MIME_SVC_NtCreateFile:
        oa = attributes32(a[2], &name);
        if (number == MIME_SVC_NtOpenFile)
            status = NtOpenFile(&handle, a[1], &oa, &io, a[4], a[5]);
        else
            status = NtCreateFile(&handle, a[1], &oa, &io, pointer32(a[4]), a[5], a[6], a[7], a[8], pointer32(a[9]), a[10]);
        store_iosb32(a[3], &io);
        if (NT_SUCCESS(status))
            store32(a[0], (ULONG_PTR)handle);
        return status;
    case MIME_SVC_NtCreateSection:
        oa = attributes32(a[2], &name);
        status = NtCreateSection(&handle, a[1], a[2] ? &oa : NULL, pointer32(a[3]), a[4], a[5], handle32(a[6]));
        if (NT_SUCCESS(status))
            store32(a[0], (ULONG_PTR)handle);
        return status;
    case MIME_SVC_NtMapViewOfSection: {
        PVOID base = pointer32(*(PULONG)pointer32(a[2]));
        SIZE_T size = *(PULONG)pointer32(a[6]);
        if ((ULONG_PTR)base + size > 0x7FFF0000ull)
            return STATUS_NO_MEMORY;
        status = NtMapViewOfSection(handle32(a[0]), handle32(a[1]), &base, 32, a[4], pointer32(a[5]), &size,
                                    a[7], a[8], a[9]);
        if (NT_SUCCESS(status) && (ULONG_PTR)base + size > 0x7FFF0000ull) {
            NtUnmapViewOfSection(handle32(a[1]), base);
            return STATUS_NO_MEMORY;
        }
        if (NT_SUCCESS(status)) {
            store32(a[2], (ULONG_PTR)base);
            store32(a[6], size);
        }
        return status;
    }
    case MIME_SVC_NtUnmapViewOfSection:
        return NtUnmapViewOfSection(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtFlushInstructionCache:
        return NtFlushInstructionCache(handle32(a[0]), pointer32(a[1]), a[2]);
    case MIME_SVC_NtSetInformationThread:
        /* classes with a pointer sized value are widened, the others have the same layout. */
        if (a[1] == 4 /* ThreadAffinityMask */ || a[1] == 9 /* ThreadQuerySetWin32StartAddress */) {
            if (a[3] != 4)
                return STATUS_INFO_LENGTH_MISMATCH;
            ULONG_PTR value = *(const ULONG *)pointer32(a[2]);
            return NtSetInformationThread(handle32(a[0]), a[1], &value, sizeof(value));
        }
        if (a[1] == 38 /* ThreadNameInformation */) {
            if (a[3] != sizeof(MIME_STRING32))
                return STATUS_INFO_LENGTH_MISMATCH;
            name = string32(a[2]);
            return NtSetInformationThread(handle32(a[0]), a[1], &name, sizeof(name));
        }
        return NtSetInformationThread(handle32(a[0]), a[1], pointer32(a[2]), a[3]);
    case MIME_SVC_NtQueryInformationThread:
        if (a[1] == ThreadBasicInformation) {
            if (a[3] < 28)
                return STATUS_INFO_LENGTH_MISMATCH;
            THREAD_BASIC_INFORMATION info;
            status = NtQueryInformationThread(handle32(a[0]), a[1], &info, sizeof(info), NULL);
            if (NT_SUCCESS(status)) {
                ULONG out[7] = {(ULONG)info.ExitStatus,
                    a[0] == (ULONG)-2 ? (ULONG)(ULONG_PTR)MxCurrentTeb32() : 0,
                    (ULONG)(ULONG_PTR)info.ClientId.UniqueProcess, (ULONG)(ULONG_PTR)info.ClientId.UniqueThread,
                    (ULONG)info.AffinityMask, (ULONG)info.Priority, (ULONG)info.BasePriority};
                memcpy(pointer32(a[2]), out, sizeof(out));
                store32(a[4], sizeof(out));
            }
            return status;
        }
        if (a[1] == ThreadAmILastThread)
            return NtQueryInformationThread(handle32(a[0]), a[1], pointer32(a[2]), a[3], pointer32(a[4]));
        return STATUS_NOT_IMPLEMENTED;
    case MIME_SVC_NtQueryInformationProcess:
        if (a[1] == ProcessBasicInformation) {
            if (a[3] < 24)
                return STATUS_INFO_LENGTH_MISMATCH;
            PROCESS_BASIC_INFORMATION info;
            status = NtQueryInformationProcess(handle32(a[0]), a[1], &info, sizeof(info), NULL);
            if (NT_SUCCESS(status)) {
                ULONG out[6] = {(ULONG)info.ExitStatus,
                    a[0] == (ULONG)-1 ? (ULONG)(ULONG_PTR)MxCurrentPeb32() : 0,
                    (ULONG)info.AffinityMask, (ULONG)info.BasePriority,
                    (ULONG)info.UniqueProcessId, (ULONG)info.InheritedFromUniqueProcessId};
                memcpy(pointer32(a[2]), out, sizeof(out));
                store32(a[4], sizeof(out));
            }
            return status;
        }
        if (a[1] == 36)
            return NtQueryInformationProcess(handle32(a[0]), a[1], pointer32(a[2]), a[3], pointer32(a[4]));
        return STATUS_NOT_IMPLEMENTED;
    case MIME_SVC_NtQuerySystemInformation:
        if (a[0] == SystemBasicInformation) {
            store32(a[3], 44);
            if (a[2] < 44)
                return STATUS_INFO_LENGTH_MISMATCH;
            SYSTEM_BASIC_INFORMATION info;
            status = NtQuerySystemInformation(a[0], &info, sizeof(info), NULL);
            if (NT_SUCCESS(status)) {
                ULONG out[11] = {info.Reserved, info.TimerResolution, info.PageSize, info.NumberOfPhysicalPages,
                    info.LowestPhysicalPageNumber, info.HighestPhysicalPageNumber, info.AllocationGranularity,
                    0x10000, 0x7FFEFFFF, (ULONG)info.ActiveProcessorsAffinityMask, (UCHAR)info.NumberOfProcessors};
                memcpy(pointer32(a[1]), out, sizeof(out));
            }
            return status;
        }
        if (a[0] == SystemProcessInformation)
            return query_processes32(a[1], a[2], a[3]);
        if (a[0] == SystemProcessorInformation || a[0] == SystemTimeOfDayInformation)
            return NtQuerySystemInformation(a[0], pointer32(a[1]), a[2], pointer32(a[3]));
        return STATUS_NOT_IMPLEMENTED;
    case MIME_SVC_NtQueryPerformanceCounter:
        return NtQueryPerformanceCounter(pointer32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtQuerySystemTime:
        return NtQuerySystemTime(pointer32(a[0]));
    case MIME_SVC_NtDelayExecution:
        return NtDelayExecution(a[0], pointer32(a[1]));
    default:
        return STATUS_NOT_IMPLEMENTED;
    }
}
