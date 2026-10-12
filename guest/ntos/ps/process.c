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
        g_cid_table[i] = NULL;
    KeReleaseSpinLock(&g_cid_lock, irql);
}

static PVOID lookup_cid(HANDLE id, POBJECT_TYPE type)
{
    ULONG i = (ULONG)((ULONG_PTR)id / 4);
    PVOID object = NULL;
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

NTSTATUS PsReferenceProcessHandle(HANDLE handle, ACCESS_MASK access, PEPROCESS *process)
{
    return ObReferenceHandle(handle, PsProcessTypeObject, KeGetPreviousMode(), (PVOID *)process, NULL);
}

NTSTATUS PsReferenceThreadHandle(HANDLE handle, ACCESS_MASK access, PETHREAD *thread)
{
    return ObReferenceHandle(handle, PsThreadTypeObject, KeGetPreviousMode(), (PVOID *)thread, NULL);
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
            g_process_notify[i] = NULL;
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
    ULONG64 s = KiAcquireDispatcherLock();
    if (process->ActiveProcessLinks.Flink)
        RemoveEntryList(&process->ActiveProcessLinks);
    KiReleaseDispatcherLock(s);
}

/* allocates a process object with its own address space. */
NTSTATUS PspCreateProcessObject(PEPROCESS parent, PEPROCESS *out)
{
    PEPROCESS process;
    NTSTATUS status = ObCreateObject(PsProcessTypeObject, NULL, KernelMode, sizeof(EPROCESS), (PVOID *)&process);
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
    process->Pcb.Ttbr0 = process->AddressSpace.RootPhys | ((ULONG64)process->AddressSpace.Asid << 48);
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
    PsWin32ProcessCallout(process, FALSE);
    PspNotifyProcess(process, FALSE);
    ObKillProcessHandles(process);
    /* leave the address space before tearing it down. */
    KiCurrentThread()->ApcStateProcess = &PsInitialSystemProcess->Pcb;
    KI_WRITE_SYSREG(ttbr0_el1, g_system_root);
    KiInstructionBarrier();
    MmDeleteAddressSpace(&process->AddressSpace);
    process->Exited = TRUE;
    KeQuerySystemTime((PLARGE_INTEGER)&process->ExitTime);
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

NTSTATUS NTAPI NtQueryInformationProcess(HANDLE handle, PROCESSINFOCLASS klass, PVOID info, ULONG length,
                                         PULONG returned)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(handle, PROCESS_QUERY_INFORMATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    union {
        PROCESS_BASIC_INFORMATION basic;
        ULONG_PTR ptr;
        ULONG u32;
        UCHAR bytes[1040];
    } out;
    ULONG size = 0;
    memset(&out, 0, sizeof(out));
    switch (klass) {
    case ProcessBasicInformation:
        out.basic.ExitStatus = process->Exited ? process->ExitStatus : STATUS_PENDING;
        out.basic.PebBaseAddress = process->Peb;
        out.basic.AffinityMask = 1;
        out.basic.BasePriority = 8;
        out.basic.UniqueProcessId = (ULONG_PTR)process->UniqueProcessId;
        out.basic.InheritedFromUniqueProcessId = (ULONG_PTR)process->InheritedFromUniqueProcessId;
        size = sizeof(out.basic);
        break;
    case ProcessDebugPort:
    case ProcessWow64Information:
        size = sizeof(ULONG_PTR);
        break;
    case ProcessDebugFlags:
        out.u32 = 1;
        size = sizeof(ULONG);
        break;
    case ProcessDefaultHardErrorMode:
    case ProcessExecuteFlags:
        size = sizeof(ULONG);
        break;
    case ProcessSessionInformation:
        out.u32 = 1;
        size = sizeof(ULONG);
        break;
    case ProcessCookie:
        out.u32 = SharedUserData->Cookie ^ (ULONG)(ULONG_PTR)process->UniqueProcessId * 0x9E3779B1u;
        size = sizeof(ULONG);
        break;
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
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtOpenProcess(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PCLIENT_ID cid)
{
    CLIENT_ID id;
    if (!cid || !NT_SUCCESS(KiCopyFromUser(&id, cid, sizeof(id))))
        return STATUS_INVALID_PARAMETER;
    PEPROCESS process = PspLookupProcess(id.UniqueProcess);
    if (!process)
        return STATUS_INVALID_CID;
    HANDLE h;
    NTSTATUS status = ObInsertObject(process, access, 0, &h);
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

    OBJECT_TYPE_INITIALIZER init = {0};
    init.ValidAccessMask = PROCESS_ALL_ACCESS;
    init.DeleteProcedure = process_delete;
    init.WaitObject = process_wait_object;
    PsProcessTypeObject = ObCreateObjectType(L"Process", &init);
    init.ValidAccessMask = THREAD_ALL_ACCESS;
    init.DeleteProcedure = PspThreadDelete;
    init.WaitObject = thread_wait_object;
    PsThreadTypeObject = ObCreateObjectType(L"Thread", &init);

    /* the system process owns kernel handles and system threads. */
    PEPROCESS system;
    ObCreateObject(PsProcessTypeObject, NULL, KernelMode, sizeof(EPROCESS), (PVOID *)&system);
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
    g_system_root = MmAllocatePage();
    system->Pcb.Ttbr0 = g_system_root;
    KeInitializeEvent(&system->ExitEvent, NotificationEvent, FALSE);
    InsertTailList(&PsActiveProcessHead, &system->ActiveProcessLinks);
    PsInitialSystemProcess = system;
    PsIdleProcess = system;
    KI_WRITE_SYSREG(ttbr0_el1, g_system_root);
    KiInstructionBarrier();

    PsInitializeIdleThread();
}
