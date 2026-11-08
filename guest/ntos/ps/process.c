/* processes: object type, system process, client ids and termination. */
#include "../ntos.h"
#include "../se/se.h"

POBJECT_TYPE PsProcessTypeObject;
POBJECT_TYPE PsThreadTypeObject;
PEPROCESS PsInitialSystemProcess;
PEPROCESS PsIdleProcess;
LIST_ENTRY PsActiveProcessHead;

static PVOID *g_cid_table;
static ULONG g_cid_capacity;
static KSPIN_LOCK g_cid_lock;
static USHORT g_next_asid = 1;
static ULONG64 g_system_root;


NTSTATUS PspAllocateId(PVOID object, PHANDLE id)
{
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_cid_lock);
    for (ULONG i = 2; i < g_cid_capacity; i++) {
        if (!g_cid_table[i]) {
            g_cid_table[i] = object;
            KeReleaseSpinLock(&g_cid_lock, irql);
            *id = (HANDLE)(ULONG_PTR)(i * 4);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_cid_lock, irql);
    return STATUS_INSUFFICIENT_RESOURCES;
}

void PspFreeId(HANDLE id)
{
    ULONG i = (ULONG)((ULONG_PTR)id / 4);
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_cid_lock);
    if (i < g_cid_capacity)
        g_cid_table[i] = nullptr;
    KeReleaseSpinLock(&g_cid_lock, irql);
}

static PVOID lookup_cid(HANDLE id, POBJECT_TYPE type)
{
    ULONG i = (ULONG)((ULONG_PTR)id / 4);
    PVOID object = nullptr;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_cid_lock);
    if (i < g_cid_capacity && g_cid_table[i] && OBJECT_TO_HEADER(g_cid_table[i])->Type == type) {
        object = g_cid_table[i];
        ObReferenceObject(object);
    }
    KeReleaseSpinLock(&g_cid_lock, irql);
    return object;
}

PEPROCESS PspLookupProcess(HANDLE pid)
{
    return lookup_cid(pid, PsProcessTypeObject);
}

PETHREAD PspLookupThread(HANDLE tid)
{
    return lookup_cid(tid, PsThreadTypeObject);
}

NTSTATUS NTAPI PsLookupProcessByProcessId(HANDLE id, PEPROCESS *process)
{
    *process = PspLookupProcess(id);
    return *process ? STATUS_SUCCESS : STATUS_INVALID_CID;
}

NTSTATUS NTAPI PsLookupThreadByThreadId(HANDLE id, PETHREAD *thread)
{
    *thread = PspLookupThread(id);
    return *thread ? STATUS_SUCCESS : STATUS_INVALID_CID;
}

/* user mode callers need the asked rights in the handle. the caller's pseudo handles have them all. */
NTSTATUS PsReferenceProcessHandle(HANDLE handle, ACCESS_MASK access, PEPROCESS *process)
{
    ACCESS_MASK granted = 0;
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    NTSTATUS status = ObReferenceHandle(handle, PsProcessTypeObject, mode, (PVOID *)process, &granted);
    if (NT_SUCCESS(status) && mode == UserMode && (granted & access) != access) {
        ObDereferenceObject(*process);
        *process = nullptr;
        status = STATUS_ACCESS_DENIED;
    }
    return status;
}

NTSTATUS PsReferenceThreadHandle(HANDLE handle, ACCESS_MASK access, PETHREAD *thread)
{
    ACCESS_MASK granted = 0;
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    NTSTATUS status = ObReferenceHandle(handle, PsThreadTypeObject, mode, (PVOID *)thread, &granted);
    if (NT_SUCCESS(status) && mode == UserMode && (granted & access) != access) {
        ObDereferenceObject(*thread);
        *thread = nullptr;
        status = STATUS_ACCESS_DENIED;
    }
    return status;
}

PEPROCESS NTAPI PsGetCurrentProcess(void)
{
    return PsCurrentProcess();
}

PETHREAD NTAPI PsGetCurrentThread(void)
{
    return PsCurrentThread();
}

HANDLE NTAPI PsGetCurrentProcessId(void)
{
    return PsCurrentProcess()->UniqueProcessId;
}

HANDLE NTAPI PsGetCurrentThreadId(void)
{
    return PsCurrentThread()->Cid.UniqueThread;
}

HANDLE NTAPI PsGetProcessId(PEPROCESS process)
{
    return process->UniqueProcessId;
}

PVOID NTAPI PsGetProcessWin32Process(PEPROCESS process)
{
    return process->Win32Process;
}

NTSTATUS NTAPI PsSetProcessWin32Process(PEPROCESS process, PVOID win32, PVOID old_win32)
{
    process->Win32Process = win32;
    return STATUS_SUCCESS;
}

PVOID NTAPI PsGetProcessPeb(PEPROCESS process)
{
    return process->Peb;
}

NTSTATUS NTAPI PsGetProcessExitStatus(PEPROCESS process)
{
    return process->ExitStatusReady ? process->ExitStatus : STATUS_PENDING;
}

void NTAPI MmSetProcessCallbackTable(PEPROCESS process, PVOID table)
{
    process->KernelCallbackTable = table;
}

/* the base priority of the threads of each priority class. */
static const SCHAR g_class_base[] = {8, 4, 8, 13, 24, 6, 10};

static void process_delete(PVOID object)
{
    PEPROCESS process = object;
    if (process->AddressSpace.Root)
        MmDeleteAddressSpace(&process->AddressSpace);
    if (process->MmLockedRanges)
        ExFreePool(process->MmLockedRanges);
    PSECTION image = __atomic_exchange_n(&process->ImageSection, nullptr, __ATOMIC_ACQ_REL);
    if (image)
        ObDereferenceObject(image);
    if (process->UniqueProcessId)
        PspFreeId(process->UniqueProcessId);
    SepFreeProcessToken(process);
    ULONG64 s = KiAcquireDispatcherLock();
    if (process->ActiveProcessLinks.Flink)
        RemoveEntryList(&process->ActiveProcessLinks);
    KiReleaseDispatcherLock(s);
}

NTSTATUS PspCreateProcessObject(PEPROCESS parent, PEPROCESS *out)
{
    PEPROCESS process;
    NTSTATUS status = ObCreateObject(PsProcessTypeObject, nullptr, KernelMode, sizeof(EPROCESS), (PVOID *)&process);
    if (!NT_SUCCESS(status))
        return status;
    memset(process, 0, sizeof(*process));
    process->Pcb.Header.Type = ProcessObject;
    process->Pcb.Header.Size = sizeof(KPROCESS) / 8;
    InitializeListHead(&process->Pcb.Header.WaitListHead);
    InitializeListHead(&process->Pcb.ThreadListHead);
    InitializeListHead(&process->ThreadListHead);
    process->Pcb.BasePriority = 8;
    process->PriorityClass = PROCESS_PRIORITY_CLASS_NORMAL;
    process->ShutdownLevel = MX_SHUTDOWN_LEVEL_DEFAULT;
    KeInitializeEvent(&process->ExitEvent, NotificationEvent, FALSE);

    status = ObInitializeHandleTable(&process->HandleTable);
    if (NT_SUCCESS(status)) {
        process->AddressSpace.Asid = g_next_asid++;
        if (g_next_asid == 0)
            g_next_asid = 1;
        status = MmCreateAddressSpace(&process->AddressSpace);
    }
    if (NT_SUCCESS(status))
        status = PspAllocateId(process, &process->UniqueProcessId);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(process);
        return status;
    }
    /* a child inherits its parent's affinity, and its priority class when that is idle or below normal. */
    if (parent) {
        process->Pcb.Affinity = parent->Pcb.Affinity;
        if (parent->PriorityClass == PROCESS_PRIORITY_CLASS_IDLE ||
            parent->PriorityClass == PROCESS_PRIORITY_CLASS_BELOW_NORMAL) {
            process->PriorityClass = parent->PriorityClass;
            process->Pcb.BasePriority = g_class_base[process->PriorityClass];
        }
    }
    process->Pcb.Asid = process->AddressSpace.Asid;
    process->Pcb.PageDirectory = process->AddressSpace.Root;
    process->Pcb.DirectoryTableBase = MiDirectoryTableBase(&process->AddressSpace);
    process->InheritedFromUniqueProcessId = parent ? parent->UniqueProcessId : 0;
    KeQuerySystemTime((PLARGE_INTEGER)&process->CreateTime);
    ULONG64 s = KiAcquireDispatcherLock();
    InsertTailList(&PsActiveProcessHead, &process->ActiveProcessLinks);
    KiReleaseDispatcherLock(s);
    *out = process;
    return STATUS_SUCCESS;
}

/* runs on the last exiting thread of the process. */
void PspProcessExit(PEPROCESS process)
{
    KiLogPrintf("process %u (%s) exited with status %08x", (ULONG)(ULONG_PTR)process->UniqueProcessId,
                process->ImageFileName, process->ExitStatus);
    process->ExitStatusReady = TRUE;
    PspJobProcessExit(process);
    ExReleaseTimerResolution(process);
    PsWin32ProcessCallout(process, FALSE);
    PspNotifyProcess(process, FALSE);
    ObKillProcessHandles(process);
    /* leave the address space before tearing it down. */
    KiCurrentThread()->ApcStateProcess = &PsInitialSystemProcess->Pcb;
    KiSwitchAddressSpace(&PsInitialSystemProcess->Pcb);
    MmDeleteAddressSpace(&process->AddressSpace);
    /* the program's file is no longer mapped once it has exited, even while handles keep the process. */
    PSECTION image = __atomic_exchange_n(&process->ImageSection, nullptr, __ATOMIC_ACQ_REL);
    if (image)
        ObDereferenceObject(image);
    process->Exited = TRUE;
    KeQuerySystemTime((PLARGE_INTEGER)&process->ExitTime);
    ConsoleProcessExit(process);
    MimeSessionProcessExited(process);
    ULONG64 s = KiAcquireDispatcherLock();
    process->Pcb.Header.SignalState = 1;
    KiSignalWaiters(&process->Pcb.Header, 0);
    KiReleaseDispatcherLock(s);
    KeSetEvent(&process->ExitEvent, 0, FALSE);
}

NTSTATUS PspTerminateProcess(PEPROCESS process, NTSTATUS status)
{
    ULONG64 s = KiAcquireDispatcherLock();
    if (!process->Terminating) {
        process->Terminating = TRUE;
        process->ExitStatus = status;
    }
    for (PLIST_ENTRY e = process->ThreadListHead.Flink; e != &process->ThreadListHead; e = e->Flink) {
        PETHREAD t = CONTAINING_RECORD(e, ETHREAD, ThreadListEntry);
        if (!t->Tcb.Terminating) {
            t->Tcb.Terminating = TRUE;
            t->ExitStatus = status;
        }
        if (t->Tcb.State == Waiting)
            KiUnwaitThread(&t->Tcb, STATUS_KERNEL_APC);
        else if (t->Tcb.State == Initialized)
            KiReadyThread(&t->Tcb);
        else
            KiInterruptThread(&t->Tcb);
    }
    KiReleaseDispatcherLock(s);
    return STATUS_SUCCESS;
}

/* ends every thread of the process but the caller's, which ExitProcess does before the libraries are told. */
static void PspTerminateOtherThreads(PEPROCESS process, NTSTATUS status)
{
    PETHREAD self = PsGetCurrentThread();
    ULONG64 s = KiAcquireDispatcherLock();
    if (!process->Terminating) {
        process->Terminating = TRUE;
        process->ExitStatus = status;
    }
    for (PLIST_ENTRY e = process->ThreadListHead.Flink; e != &process->ThreadListHead; e = e->Flink) {
        PETHREAD t = CONTAINING_RECORD(e, ETHREAD, ThreadListEntry);
        if (t == self)
            continue;
        if (!t->Tcb.Terminating) {
            t->Tcb.Terminating = TRUE;
            t->ExitStatus = status;
        }
        if (t->Tcb.State == Waiting)
            KiUnwaitThread(&t->Tcb, STATUS_KERNEL_APC);
        else if (t->Tcb.State == Initialized)
            KiReadyThread(&t->Tcb);
        else
            KiInterruptThread(&t->Tcb);
    }
    KiReleaseDispatcherLock(s);
}

NTSTATUS NTAPI NtTerminateProcess(HANDLE handle, NTSTATUS exit_status)
{
    PEPROCESS process;
    /* no handle means the threads of the calling process other than the caller. */
    if (!handle) {
        PEPROCESS current = PsCurrentProcess();
        if (current->SystemProcess)
            return STATUS_ACCESS_DENIED;
        PspTerminateOtherThreads(current, exit_status);
        return STATUS_SUCCESS;
    }
    NTSTATUS status = PsReferenceProcessHandle(handle, PROCESS_TERMINATE, &process);
    if (!NT_SUCCESS(status))
        return status;
    if (process->SystemProcess) {
        ObDereferenceObject(process);
        return STATUS_ACCESS_DENIED;
    }
    BOOLEAN self = process == PsCurrentProcess();
    PspTerminateProcess(process, exit_status);
    ObDereferenceObject(process);
    if (self)
        PspExitThread(exit_status);
    return STATUS_SUCCESS;
}

/* the limited rights are for what anyone may see. */
static ACCESS_MASK query_rights(PROCESSINFOCLASS klass)
{
    switch ((ULONG)klass) {
    case ProcessBasicInformation:
    case ProcessTimes:
    case ProcessPriorityClass:
    case ProcessPriorityBoost:
    case ProcessImageFileName:
    case ProcessImageFileNameWin32:
    case ProcessHandleCount:
    case ProcessWow64Information:
    case ProcessSessionInformation:
    case ProcessProtectionInformation:
        return PROCESS_QUERY_LIMITED_INFORMATION;
    default:
        return PROCESS_QUERY_INFORMATION;
    }
}

/* full query or set rights imply the limited ones. */
static ACCESS_MASK imply_process_access(ACCESS_MASK granted)
{
    if ((granted & PROCESS_QUERY_INFORMATION) || (granted & (PROCESS_VM_OPERATION | PROCESS_VM_WRITE)) ==
                                                     (PROCESS_VM_OPERATION | PROCESS_VM_WRITE))
        granted |= PROCESS_QUERY_LIMITED_INFORMATION;
    if (granted & PROCESS_SET_INFORMATION)
        granted |= PROCESS_SET_LIMITED_INFORMATION;
    return granted;
}

/* the same for a thread, and suspend implies resume. */
static ACCESS_MASK imply_thread_access(ACCESS_MASK granted)
{
    if (granted & THREAD_QUERY_INFORMATION)
        granted |= THREAD_QUERY_LIMITED_INFORMATION;
    if (granted & THREAD_SET_INFORMATION)
        granted |= THREAD_SET_LIMITED_INFORMATION;
    if (granted & THREAD_SUSPEND_RESUME)
        granted |= THREAD_RESUME;
    return granted;
}

/* ProcessHandleTable: an array of 32-bit handle values. */
static NTSTATUS query_handle_table(PEPROCESS process, PVOID info, ULONG length, ULONG *size)
{
    PHANDLE_TABLE table = &process->HandleTable;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
    ULONG capacity = table->Capacity;
    KeReleaseSpinLock(&table->Lock, irql);
    PULONG values = ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)(capacity ? capacity : 1) * sizeof(ULONG), KI_TAG('P', 's', 'H', 'T'));
    if (!values)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG count = 0;
    irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
    for (ULONG i = 0; i < table->Capacity && i < capacity; i++)
        if (table->Entries[i].Object)
            values[count++] = (i + 1) * 4;
    KeReleaseSpinLock(&table->Lock, irql);
    *size = count * sizeof(ULONG);
    NTSTATUS status = length < *size ? STATUS_INFO_LENGTH_MISMATCH : KiCopyToUser(info, values, *size);
    ExFreePool(values);
    return status;
}

/* the sum of the times of its threads. */
static void process_times(PEPROCESS process, ULONG64 *times)
{
    ULONG64 kernel = 0, user = 0;
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = process->ThreadListHead.Flink; e != &process->ThreadListHead; e = e->Flink) {
        PETHREAD t = CONTAINING_RECORD(e, ETHREAD, ThreadListEntry);
        kernel += t->Tcb.KernelTime;
        user += t->Tcb.UserTime;
    }
    KiReleaseDispatcherLock(s);
    times[0] = process->CreateTime;
    times[1] = process->Exited ? process->ExitTime : 0;
    times[2] = kernel;
    times[3] = user;
}

NTSTATUS NTAPI NtQueryInformationProcess(HANDLE handle, PROCESSINFOCLASS klass, PVOID info, ULONG length,
                                         PULONG returned)
{
    NTSTATUS status = KiProbeReturnLength(returned);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process;
    status = PsReferenceProcessHandle(handle, query_rights(klass), &process);
    if (!NT_SUCCESS(status))
        return status;
    union {
        PROCESS_BASIC_INFORMATION basic;
        ULONG_PTR ptr;
        ULONG u32[2];
        ULONG64 times[4];
        UCHAR bytes[1040];
    } out;
    ULONG size = 0;
    memset(&out, 0, sizeof(out));
    switch ((ULONG)klass) {
    case ProcessBasicInformation:
        out.basic.ExitStatus = process->ExitStatusReady ? process->ExitStatus : STATUS_PENDING;
        out.basic.PebBaseAddress = process->Peb;
        out.basic.AffinityMask = KiProcessAffinity(&process->Pcb);
        out.basic.BasePriority = process->Pcb.BasePriority;
        out.basic.UniqueProcessId = (ULONG_PTR)process->UniqueProcessId;
        out.basic.InheritedFromUniqueProcessId = (ULONG_PTR)process->InheritedFromUniqueProcessId;
        size = sizeof(out.basic);
        break;
    case ProcessIoCounters:
        size = 48;
        break;
    case MX_PROCESS_SHUTDOWN_PARAMETERS:
        out.u32[0] = process->ShutdownLevel;
        out.u32[1] = process->ShutdownFlags & MX_SHUTDOWN_NORETRY;
        size = sizeof(MX_SHUTDOWN_PARAMETERS);
        break;
    case ProcessPriorityClass:
        out.bytes[0] = process->Foreground;
        out.bytes[1] = process->PriorityClass;
        size = sizeof(PROCESS_PRIORITY_CLASS);
        break;
    case ProcessPriorityBoost:
        out.u32[0] = process->DisableBoost;
        size = sizeof(ULONG);
        break;
    case ProcessTimes:
        process_times(process, out.times);
        size = sizeof(out.times);
        break;
    case ProcessQuotaLimits: {
        /* default working set limits, which bind nothing here. */
        ULONG64 *quota = (ULONG64 *)out.bytes;
        quota[2] = 200 * PAGE_SIZE;
        quota[3] = 1345 * PAGE_SIZE;
        *(PULONG)(out.bytes + 0x50) = 0xA;
        size = length >= 0x58 ? 0x58 : 0x30;
        break;
    }
    case ProcessVmCounters:
        size = MmQueryVmCounters(process, out.bytes, length);
        break;
    case ProcessDebugPort:
        size = sizeof(ULONG_PTR);
        break;
    case ProcessHandleCount:
        /* current and peak handle count. */
        out.u32[0] = out.u32[1] = (ULONG)process->HandleTable.Count;
        size = 8;
        break;
    case ProcessWow64Information:
        /* the 32-bit peb of a wow64 process, else 0. */
        out.ptr = (ULONG_PTR)process->Wow64Peb;
        size = sizeof(ULONG_PTR);
        break;
    case ProcessDebugObjectHandle: {
        /* mime has no debug objects, so no process has a debugger: a null handle and no port, as windows answers. */
        HANDLE none = nullptr;
        size = sizeof(HANDLE);
        status = length != size ? STATUS_INFO_LENGTH_MISMATCH : STATUS_PORT_NOT_SET;
        if (status == STATUS_PORT_NOT_SET) {
            if (KeGetPreviousMode() != UserMode)
                *(PHANDLE)info = none;
            else if (!NT_SUCCESS(KiCopyToUser(info, &none, size)))
                status = STATUS_ACCESS_VIOLATION;
        }
        if (returned) {
            if (KeGetPreviousMode() == UserMode)
                KiCopyToUser(returned, &size, sizeof(size));
            else
                *returned = size;
        }
        ObDereferenceObject(process);
        return status;
    }
    case ProcessDebugFlags:
        out.u32[0] = 1;
        size = sizeof(ULONG);
        break;
    case ProcessDefaultHardErrorMode:
        out.u32[0] = process->DefaultHardErrorMode;
        size = sizeof(ULONG);
        break;
    case ProcessExecuteFlags:
        /* permanent DEP, with the dispatch and sehop defaults of a 64-bit process. */
        out.u32[0] = 0x39;
        size = sizeof(ULONG);
        break;
    case ProcessProtectionInformation:
        size = 1;
        break;
    case ProcessSessionInformation:
        out.u32[0] = process->SessionId;
        size = sizeof(ULONG);
        break;
    case ProcessCookie:
        out.u32[0] = SharedUserData->Cookie ^ (ULONG)(ULONG_PTR)process->UniqueProcessId * 0x9E3779B1u;
        size = sizeof(ULONG);
        break;
    case ProcessMitigationPolicy:
        status = PspQueryMitigationPolicy(process, info, length, returned);
        ObDereferenceObject(process);
        return status;
    case ProcessHandleTable:
        status = query_handle_table(process, info, length, &size);
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        ObDereferenceObject(process);
        return status;
    case ProcessImageFileName:
    case ProcessImageFileNameWin32: {
        UNICODE_STRING *us = (UNICODE_STRING *)out.bytes;
        PUCHAR text = out.bytes + sizeof(UNICODE_STRING);
        const WCHAR *path = process->ImagePath.Buffer;
        ULONG n = process->ImagePath.Length;
        ULONG prefix = 0;
        /* ProcessImageFileName gives the device path, the win32 class the drive path. */
        if ((ULONG)klass == ProcessImageFileName && n >= 2 * sizeof(WCHAR) && path[1] == L':') {
            static const WCHAR system_volume[] = L"\\Device\\HarddiskVolume1", host_volume[] = L"\\Device\\HarddiskVolume2";
            const WCHAR *volume = (path[0] | 0x20) == L'c' ? system_volume : host_volume;
            prefix = (ULONG)wcslen(volume) * sizeof(WCHAR);
            memcpy(text, volume, prefix);
            path += 2;
            n -= 2 * sizeof(WCHAR);
        }
        us->Length = (USHORT)(prefix + n);
        us->MaximumLength = (USHORT)(prefix + n + 2);
        us->Buffer = (PWSTR)((PUCHAR)info + sizeof(UNICODE_STRING));
        memcpy(text + prefix, path, n);
        size = sizeof(UNICODE_STRING) + prefix + n + 2;
        break;
    }
    default:
        status = STATUS_INVALID_INFO_CLASS;
        break;
    }
    if (NT_SUCCESS(status)) {
        if (length < size)
            status = STATUS_INFO_LENGTH_MISMATCH;
        else if (KeGetPreviousMode() == UserMode)
            status = KiCopyToUser(info, &out, size);
        else
            memcpy(info, &out, size);
        if (returned) {
            if (KeGetPreviousMode() == UserMode)
                KiCopyToUser(returned, &size, sizeof(size));
            else
                *returned = size;
        }
    }
    ObDereferenceObject(process);
    return status;
}

/* a realtime class needs the increase priority privilege. */
static NTSTATUS set_priority_class(PEPROCESS process, UCHAR klass)
{
    if (klass >= RTL_NUMBER_OF(g_class_base))
        return STATUS_INVALID_PARAMETER;
    if (klass == PROCESS_PRIORITY_CLASS_REALTIME && process->PriorityClass != klass &&
        !SepSinglePrivilegeCheck(SE_INC_BASE_PRIORITY_PRIVILEGE, KeGetPreviousMode()))
        return STATUS_PRIVILEGE_NOT_HELD;
    process->PriorityClass = klass;
    KiSetBasePriorityProcess(&process->Pcb, g_class_base[klass]);
    return STATUS_SUCCESS;
}

/* threads are held to the process mask, and a thread left with no processor in it gets the whole mask. */
static void set_process_affinity(PEPROCESS process, ULONG64 mask)
{
    ULONG64 s = KiAcquireDispatcherLock();
    process->Pcb.Affinity = mask;
    for (PLIST_ENTRY e = process->Pcb.ThreadListHead.Flink; e != &process->Pcb.ThreadListHead; e = e->Flink) {
        PKTHREAD t = CONTAINING_RECORD(e, KTHREAD, ThreadListEntry);
        t->Affinity = (t->Affinity & mask) ? (t->Affinity & mask) : mask;
    }
    KiReleaseDispatcherLock(s);
    if (process->Peb && process == PsCurrentProcess())
        process->Peb->ActiveProcessAffinityMask = mask;
}

/*
 * ProcessInstrumentationCallback, like windows: the callback alone or PROCESS_INSTRUMENTATION_CALLBACK_INFORMATION
 * with version and reserved zero. another process than the caller's needs the debug privilege. a null callback turns
 * it off.
 */
static NTSTATUS set_instrumentation_callback(HANDLE handle, PVOID info, ULONG length)
{
    if (length != 8 && length != 16)
        return STATUS_INFO_LENGTH_MISMATCH;
    ULONG64 in[2] = {};
    NTSTATUS status = KiCopyFromUser(length == 8 ? &in[1] : in, info, length);
    if (!NT_SUCCESS(status))
        return status;
    if (in[0] >> 32)
        return STATUS_INVALID_PARAMETER;
    if ((ULONG)in[0])
        return (NTSTATUS)0xC0000058L; /* STATUS_UNKNOWN_REVISION */
    if ((LONG64)(in[1] << 16) >> 16 != (LONG64)in[1])
        return STATUS_INVALID_PARAMETER;
    PEPROCESS process;
    status = PsReferenceProcessHandle(handle, PROCESS_SET_INFORMATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    if (process != PsCurrentProcess() && !SeThreadHasPrivilege(SE_DEBUG_PRIVILEGE))
        status = STATUS_PRIVILEGE_NOT_HELD;
    else
        process->InstrumentationCallback = (PVOID)in[1];
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI NtSetInformationProcess(HANDLE handle, PROCESSINFOCLASS klass, PVOID info, ULONG length)
{
    if (klass == ProcessAccessToken)
        return SepSetProcessAccessToken(handle, info, length);
    if ((ULONG)klass == 40) /* ProcessInstrumentationCallback */
        return set_instrumentation_callback(handle, info, length);
    if (klass == ProcessMitigationPolicy)
        return PspSetMitigationPolicy(handle, info, length);
    ULONG expected = 0;
    switch ((ULONG)klass) {
    case ProcessBasePriority:
    case ProcessPriorityBoost:
    case ProcessDefaultHardErrorMode:
    case MX_PROCESS_PRIORITY_CLASS_EX:
        expected = sizeof(ULONG);
        break;
    case ProcessPriorityClass:
        expected = sizeof(PROCESS_PRIORITY_CLASS);
        break;
    case ProcessAffinityMask:
        expected = sizeof(ULONG64);
        break;
    case MX_PROCESS_SHUTDOWN_PARAMETERS:
        expected = sizeof(MX_SHUTDOWN_PARAMETERS);
        break;
    default:
        break;
    }
    ULONG64 value = 0;
    if (expected) {
        if (length != expected)
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!NT_SUCCESS(KiCopyFromUser(&value, info, expected)))
            return STATUS_ACCESS_VIOLATION;
    }
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(handle, PROCESS_SET_INFORMATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    switch ((ULONG)klass) {
    case ProcessBasePriority: {
        /* the top bit asks for a bigger working set, which means nothing here. */
        LONG base = (LONG)(ULONG)value & 0x7FFFFFFF;
        if (base < 1 || base > 31)
            status = STATUS_INVALID_PARAMETER;
        else if (base > process->Pcb.BasePriority && !SepSinglePrivilegeCheck(SE_INC_BASE_PRIORITY_PRIVILEGE, KeGetPreviousMode()))
            status = STATUS_PRIVILEGE_NOT_HELD;
        else
            KiSetBasePriorityProcess(&process->Pcb, base);
        break;
    }
    case ProcessPriorityClass:
        status = set_priority_class(process, (UCHAR)(value >> 8));
        break;
    case MX_PROCESS_PRIORITY_CLASS_EX: {
        ULONG flags = (ULONG)value & 0xFFFF;
        if (!flags || (flags & ~(ULONG)(PROCESS_PRIORITY_CLASS_EX_FOREGROUND | PROCESS_PRIORITY_CLASS_EX_CLASS)))
            status = STATUS_INVALID_PARAMETER;
        else if (flags & PROCESS_PRIORITY_CLASS_EX_CLASS)
            status = set_priority_class(process, (UCHAR)(value >> 16));
        if (NT_SUCCESS(status) && (flags & PROCESS_PRIORITY_CLASS_EX_FOREGROUND))
            process->Foreground = (UCHAR)(value >> 24) != 0;
        break;
    }
    case ProcessAffinityMask: {
        ULONG64 mask = value;
        if (!mask || (mask & ~KiAllProcessors()))
            status = STATUS_INVALID_PARAMETER;
        else
            set_process_affinity(process, mask);
        break;
    }
    case ProcessPriorityBoost:
        process->DisableBoost = (ULONG)value != 0;
        break;
    case ProcessDefaultHardErrorMode:
        /* the bits a job forces on its processes cannot be cleared. */
        process->DefaultHardErrorMode = (ULONG)value | PspJobForcedErrorMode(process);
        break;
    case ProcessWorkingSetWatch:
        /* page faults are not recorded, so there is nothing to start. */
        status = STATUS_INVALID_INFO_CLASS;
        break;
    case MX_PROCESS_SHUTDOWN_PARAMETERS:
        /* any level is taken. SHUTDOWN_NORETRY is the only flag. */
        if ((ULONG)(value >> 32) & ~(ULONG)MX_SHUTDOWN_NORETRY) {
            status = MX_STATUS_BAD_SHUTDOWN_FLAGS;
        } else {
            process->ShutdownLevel = (ULONG)value;
            process->ShutdownFlags = (ULONG)(value >> 32);
        }
        break;
    default:
        break;
    }
    ObDereferenceObject(process);
    return status;
}

static ACCESS_MASK map_process_access(ACCESS_MASK access)
{
    if (access & (MAXIMUM_ALLOWED | GENERIC_ALL))
        return PROCESS_ALL_ACCESS;
    ACCESS_MASK mapped = access & PROCESS_ALL_ACCESS;
    if (access & GENERIC_READ)
        mapped |= READ_CONTROL | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION;
    if (access & GENERIC_WRITE)
        mapped |= READ_CONTROL | PROCESS_CREATE_PROCESS | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                  PROCESS_VM_WRITE | PROCESS_DUP_HANDLE | PROCESS_TERMINATE | PROCESS_SET_QUOTA |
                  PROCESS_SET_INFORMATION | PROCESS_SUSPEND_RESUME;
    if (access & GENERIC_EXECUTE)
        mapped |= READ_CONTROL | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE;
    return mapped;
}

static ACCESS_MASK map_thread_access(ACCESS_MASK access)
{
    if (access & (MAXIMUM_ALLOWED | GENERIC_ALL))
        return THREAD_ALL_ACCESS;
    ACCESS_MASK mapped = access & THREAD_ALL_ACCESS;
    if (access & GENERIC_READ)
        mapped |= READ_CONTROL | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION;
    if (access & GENERIC_WRITE)
        mapped |= READ_CONTROL | THREAD_SUSPEND_RESUME | THREAD_SET_CONTEXT | THREAD_SET_INFORMATION |
                  THREAD_SET_LIMITED_INFORMATION | THREAD_SET_THREAD_TOKEN | THREAD_TERMINATE;
    if (access & GENERIC_EXECUTE)
        mapped |= READ_CONTROL | SYNCHRONIZE | THREAD_QUERY_LIMITED_INFORMATION | THREAD_TERMINATE;
    return mapped;
}

/* of the attributes, only handle inheritance matters. */
static NTSTATUS open_attributes(POBJECT_ATTRIBUTES attributes, PCLIENT_ID cid, CLIENT_ID *id, ULONG *handle_attributes)
{
    /*
     * like PsOpenProcess and PsOpenThread: the attributes are read without looking at their length, only the
     * name, which must be absent since these are found by id, and the handle attributes.
     */
    OBJECT_ATTRIBUTES oa;
    *handle_attributes = 0;
    if (!attributes || !NT_SUCCESS(KiCopyFromUser(&oa, attributes, sizeof(oa))))
        return STATUS_ACCESS_VIOLATION;
    if (oa.ObjectName)
        return STATUS_INVALID_PARAMETER_MIX;
    *handle_attributes = oa.Attributes & OBJ_INHERIT;
    if (!cid || !NT_SUCCESS(KiCopyFromUser(id, cid, sizeof(*id))))
        return STATUS_INVALID_PARAMETER_MIX;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtOpenProcess(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PCLIENT_ID cid)
{
    CLIENT_ID id;
    ULONG handle_attributes;
    NTSTATUS status = open_attributes(attributes, cid, &id, &handle_attributes);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process = PspLookupProcess(id.UniqueProcess);
    if (!process)
        return STATUS_INVALID_CID;
    if (id.UniqueThread && (HANDLE)(ULONG_PTR)id.UniqueThread != nullptr) {
        /* a thread id must belong to the process asked for. */
        PETHREAD thread = PspLookupThread(id.UniqueThread);
        if (!thread || (PEPROCESS)thread->Tcb.Process != process) {
            if (thread)
                ObDereferenceObject(thread);
            ObDereferenceObject(process);
            return STATUS_INVALID_CID;
        }
        ObDereferenceObject(thread);
    }
    /* the debug privilege opens any process, otherwise the process's descriptor decides. */
    ACCESS_MASK granted = access;
    if (!SeThreadHasPrivilege(SE_DEBUG_PRIVILEGE))
        status = ObCheckOpenAccess(process, access, UserMode, &granted);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(process);
        return status;
    }
    HANDLE h;
    status = ObInsertObject(process, map_process_access(granted), handle_attributes, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    return status;
}

NTSTATUS NTAPI NtOpenThread(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PCLIENT_ID cid)
{
    CLIENT_ID id;
    ULONG handle_attributes;
    NTSTATUS status = open_attributes(attributes, cid, &id, &handle_attributes);
    if (!NT_SUCCESS(status))
        return status;
    if (!id.UniqueThread)
        return STATUS_INVALID_CID;
    PETHREAD thread = PspLookupThread(id.UniqueThread);
    if (!thread)
        return STATUS_INVALID_CID;
    if (id.UniqueProcess && (HANDLE)(ULONG_PTR)id.UniqueProcess != ((PEPROCESS)thread->Tcb.Process)->UniqueProcessId) {
        ObDereferenceObject(thread);
        return STATUS_INVALID_CID;
    }
    ACCESS_MASK granted = access;
    if (!SeThreadHasPrivilege(SE_DEBUG_PRIVILEGE))
        status = ObCheckOpenAccess(thread, access, UserMode, &granted);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(thread);
        return status;
    }
    HANDLE h;
    status = ObInsertObject(thread, map_thread_access(granted), handle_attributes, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    return status;
}

static PVOID process_wait_object(PVOID object)
{
    return &((PEPROCESS)object)->Pcb.Header;
}

static PVOID thread_wait_object(PVOID object)
{
    return &((PETHREAD)object)->Tcb.Header;
}

void PspThreadDelete(PVOID object);

void PsInitialize(void)
{
    InitializeListHead(&PsActiveProcessHead);
    g_cid_capacity = 65536 / 4;
    g_cid_table = ExAllocatePool2(POOL_FLAG_NON_PAGED, g_cid_capacity * sizeof(PVOID), KI_TAG('P', 's', 'C', 'i'));

    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = PROCESS_ALL_ACCESS;
    init.DeleteProcedure = process_delete;
    init.ImplyAccess = imply_process_access;
    init.WaitObject = process_wait_object;
    ObSetGenericMapping(&init, 0x20410, 0x20BEA, 0x121001, 0x1FFFFF);
    PsProcessTypeObject = ObCreateObjectType(L"Process", &init);
    init.ImplyAccess = imply_thread_access;
    init.ValidAccessMask = THREAD_ALL_ACCESS;
    init.DeleteProcedure = PspThreadDelete;
    init.WaitObject = thread_wait_object;
    ObSetGenericMapping(&init, 0x20048, 0x20437, 0x121800, 0x1FFFFF);
    PsThreadTypeObject = ObCreateObjectType(L"Thread", &init);
    PspInitializeJobs();

    /* the system process owns kernel handles and system threads. */
    PEPROCESS system;
    ObCreateObject(PsProcessTypeObject, nullptr, KernelMode, sizeof(EPROCESS), (PVOID *)&system);
    memset(system, 0, sizeof(*system));
    system->Pcb.Header.Type = ProcessObject;
    InitializeListHead(&system->Pcb.Header.WaitListHead);
    InitializeListHead(&system->Pcb.ThreadListHead);
    InitializeListHead(&system->ThreadListHead);
    ObInitializeHandleTable(&system->HandleTable);
    system->SystemProcess = TRUE;
    system->Pcb.BasePriority = 8;
    system->PriorityClass = PROCESS_PRIORITY_CLASS_NORMAL;
    system->UniqueProcessId = (HANDLE)4;
    g_cid_table[1] = system;
    memcpy(system->ImageFileName, "System", 7);
    g_system_root = MiCreateSystemRoot();
    system->Pcb.DirectoryTableBase = g_system_root;
    /* the user half of the system root is the system process's own, where drivers map views as on windows. */
    ExInitializeFastMutex(&system->AddressSpace.Lock);
    system->AddressSpace.RootPhys = g_system_root;
    system->AddressSpace.Root = HalPhysmap(g_system_root);
    KeInitializeEvent(&system->ExitEvent, NotificationEvent, FALSE);
    InsertTailList(&PsActiveProcessHead, &system->ActiveProcessLinks);
    PsInitialSystemProcess = system;
    PsIdleProcess = system;
    KiSwitchAddressSpace(&system->Pcb);

    PsInitializeIdleThread();
}
