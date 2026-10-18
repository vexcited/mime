/*
 * processes: object type, system process, client ids and termination.
 */
#include "../ntos.h"

POBJECT_TYPE PsProcessTypeObject;
POBJECT_TYPE PsThreadTypeObject;
POBJECT_TYPE *PsProcessType = &PsProcessTypeObject;
POBJECT_TYPE *PsThreadType = &PsThreadTypeObject;
PEPROCESS PsInitialSystemProcess;
PEPROCESS PsIdleProcess;
LIST_ENTRY PsActiveProcessHead;

static PVOID *g_cid_table;
static ULONG g_cid_capacity;
static KSPIN_LOCK g_cid_lock;
static USHORT g_next_asid = 1;
static ULONG64 g_system_root;

typedef void(NTAPI *PCREATE_PROCESS_NOTIFY)(HANDLE, HANDLE, BOOLEAN);
static PCREATE_PROCESS_NOTIFY g_process_notify[8];

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

/*
 * a process or thread by handle. a caller in user mode needs the rights it asks for in the handle, the pseudo
 * handles of the caller have them all.
 */
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
    return process->ExitStatus;
}

void NTAPI MmSetProcessCallbackTable(PEPROCESS process, PVOID table)
{
    process->KernelCallbackTable = table;
}

NTSTATUS NTAPI PsSetCreateProcessNotifyRoutine(PCREATE_PROCESS_NOTIFY_ROUTINE routine, BOOLEAN remove)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_process_notify); i++) {
        if (remove && g_process_notify[i] == routine) {
            g_process_notify[i] = nullptr;
            return STATUS_SUCCESS;
        }
        if (!remove && !g_process_notify[i]) {
            g_process_notify[i] = routine;
            return STATUS_SUCCESS;
        }
    }
    return remove ? STATUS_PROCEDURE_NOT_FOUND : STATUS_INVALID_PARAMETER;
}

void PspNotifyProcess(PEPROCESS process, BOOLEAN create)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_process_notify); i++)
        if (g_process_notify[i])
            g_process_notify[i](process->InheritedFromUniqueProcessId, process->UniqueProcessId, create);
}

static void process_delete(PVOID object)
{
    PEPROCESS process = object;
    if (process->AddressSpace.Root)
        MmDeleteAddressSpace(&process->AddressSpace);
    if (process->ImageSection)
        ObDereferenceObject(process->ImageSection);
    if (process->UniqueProcessId)
        PspFreeId(process->UniqueProcessId);
    SepFreeProcessToken(process);
    ULONG64 s = KiAcquireDispatcherLock();
    if (process->ActiveProcessLinks.Flink)
        RemoveEntryList(&process->ActiveProcessLinks);
    KiReleaseDispatcherLock(s);
}

/* allocates a process object with its own address space. */
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

/* runs in the context of the last exiting thread of the process. */
void PspProcessExit(PEPROCESS process)
{
    KiLogPrintf("process %u (%s) exited with status %08x", (ULONG)(ULONG_PTR)process->UniqueProcessId,
                process->ImageFileName, process->ExitStatus);
    PspJobProcessExit(process);
    PsWin32ProcessCallout(process, FALSE);
    PspNotifyProcess(process, FALSE);
    ObKillProcessHandles(process);
    /* leave the address space before tearing it down. */
    KiCurrentThread()->ApcStateProcess = &PsInitialSystemProcess->Pcb;
    KiSwitchAddressSpace(&PsInitialSystemProcess->Pcb);
    MmDeleteAddressSpace(&process->AddressSpace);
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

NTSTATUS NTAPI NtTerminateProcess(HANDLE handle, NTSTATUS exit_status)
{
    PEPROCESS process;
    if (!handle)
        handle = NtCurrentProcess();
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

/* the rights a class of NtQueryInformationProcess needs: the limited ones are for what anyone may see. */
static ACCESS_MASK query_rights(PROCESSINFOCLASS klass)
{
    switch ((ULONG)klass) {
    case ProcessBasicInformation:
    case ProcessTimes:
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

/* the handle values of a process, ProcessHandleTable: an array of 32-bit values. */
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

/* the kernel and user time of a process: the sum of its threads. */
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
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(handle, query_rights(klass), &process);
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
        out.basic.ExitStatus = process->Exited ? process->ExitStatus : STATUS_PENDING;
        out.basic.PebBaseAddress = process->Peb;
        out.basic.AffinityMask = 1;
        out.basic.BasePriority = 8;
        out.basic.UniqueProcessId = (ULONG_PTR)process->UniqueProcessId;
        out.basic.InheritedFromUniqueProcessId = (ULONG_PTR)process->InheritedFromUniqueProcessId;
        size = sizeof(out.basic);
        break;
    case ProcessIoCounters:
        size = 48;
        break;
    case ProcessTimes:
        process_times(process, out.times);
        size = sizeof(out.times);
        break;
    case ProcessQuotaLimits: {
        /* the default working set limits; they bind nothing in mime. */
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
        /* the handles now and the most there were. */
        out.u32[0] = out.u32[1] = (ULONG)process->HandleTable.Count;
        size = 8;
        break;
    case ProcessWow64Information:
        /* the 32-bit peb of a wow64 process, 0 for any other. */
        out.ptr = (ULONG_PTR)process->Wow64Peb;
        size = sizeof(ULONG_PTR);
        break;
    case ProcessDebugFlags:
        out.u32[0] = 1;
        size = sizeof(ULONG);
        break;
    case ProcessDefaultHardErrorMode:
        size = sizeof(ULONG);
        break;
    case ProcessExecuteFlags:
        /* execution disabled for data, permanently, with the dispatch and sehop defaults of a 64-bit process. */
        out.u32[0] = 0x39;
        size = sizeof(ULONG);
        break;
    case ProcessProtectionInformation:
        size = 1;
        break;
    case ProcessSessionInformation:
        out.u32[0] = 1;
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
        ULONG n = process->ImagePath.Length;
        us->Length = (USHORT)n;
        us->MaximumLength = (USHORT)(n + 2);
        us->Buffer = (PWSTR)((PUCHAR)info + sizeof(UNICODE_STRING));
        memcpy(out.bytes + sizeof(UNICODE_STRING), process->ImagePath.Buffer, n);
        size = sizeof(UNICODE_STRING) + n + 2;
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

NTSTATUS NTAPI NtSetInformationProcess(HANDLE handle, PROCESSINFOCLASS klass, PVOID info, ULONG length)
{
    if (klass == ProcessAccessToken)
        return SepSetProcessAccessToken(handle, info, length);
    if (klass == ProcessMitigationPolicy)
        return PspSetMitigationPolicy(handle, info, length);
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(handle, PROCESS_SET_INFORMATION, &process);
    if (NT_SUCCESS(status))
        ObDereferenceObject(process);
    return status;
}

/* generic rights as the rights of a process. */
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

/* the client id and the attributes that matter of an open: whether the handle is inherited. */
static NTSTATUS open_attributes(POBJECT_ATTRIBUTES attributes, PCLIENT_ID cid, CLIENT_ID *id, ULONG *handle_attributes)
{
    OBJECT_ATTRIBUTES oa;
    *handle_attributes = 0;
    if (attributes) {
        if (!NT_SUCCESS(KiCopyFromUser(&oa, attributes, sizeof(oa))))
            return STATUS_ACCESS_VIOLATION;
        if (oa.Length != sizeof(oa))
            return STATUS_INVALID_PARAMETER;
        /* processes and threads are found by their ids, not by name. */
        if (oa.ObjectName)
            return STATUS_INVALID_PARAMETER_MIX;
        *handle_attributes = oa.Attributes & OBJ_INHERIT;
    }
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
        /* a thread id names the process of that thread, which must be this one. */
        PETHREAD thread = PspLookupThread(id.UniqueThread);
        if (!thread || (PEPROCESS)thread->Tcb.Process != process) {
            if (thread)
                ObDereferenceObject(thread);
            ObDereferenceObject(process);
            return STATUS_INVALID_CID;
        }
        ObDereferenceObject(thread);
    }
    HANDLE h;
    status = ObInsertObject(process, map_process_access(access), handle_attributes, &h);
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
    HANDLE h;
    status = ObInsertObject(thread, map_thread_access(access), handle_attributes, &h);
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
    init.WaitObject = process_wait_object;
    PsProcessTypeObject = ObCreateObjectType(L"Process", &init);
    init.ValidAccessMask = THREAD_ALL_ACCESS;
    init.DeleteProcedure = PspThreadDelete;
    init.WaitObject = thread_wait_object;
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
    system->UniqueProcessId = (HANDLE)4;
    g_cid_table[1] = system;
    memcpy(system->ImageFileName, "System", 7);
    g_system_root = MiCreateSystemRoot();
    system->Pcb.DirectoryTableBase = g_system_root;
    KeInitializeEvent(&system->ExitEvent, NotificationEvent, FALSE);
    InsertTailList(&PsActiveProcessHead, &system->ActiveProcessLinks);
    PsInitialSystemProcess = system;
    PsIdleProcess = system;
    KiSwitchAddressSpace(&system->Pcb);

    PsInitializeIdleThread();
}
