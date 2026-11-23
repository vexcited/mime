/* processes: object type, system process, client ids and termination. */
#include "../ntos.h"
#include "../se/se.h"
#include "classref.h"

POBJECT_TYPE PsProcessTypeObject;
POBJECT_TYPE PsThreadTypeObject;
PEPROCESS PsInitialSystemProcess;
PEPROCESS PsIdleProcess;
LIST_ENTRY PsActiveProcessHead;

static PVOID *g_cid_table;
static ULONG g_cid_capacity;
/* a real lock: a thread is looked up by its id without the kernel lock, see KiFastSystemService. */
static volatile LONG g_cid_lock;
static USHORT g_next_asid = 1;
static ULONG64 g_system_root;


NTSTATUS PspAllocateId(PVOID object, PHANDLE id)
{
    ULONG64 state = KiAcquireRawLock(&g_cid_lock);
    for (ULONG i = 2; i < g_cid_capacity; i++) {
        if (!g_cid_table[i]) {
            g_cid_table[i] = object;
            KiReleaseRawLock(&g_cid_lock, state);
            *id = (HANDLE)(ULONG_PTR)(i * 4);
            return STATUS_SUCCESS;
        }
    }
    KiReleaseRawLock(&g_cid_lock, state);
    return STATUS_INSUFFICIENT_RESOURCES;
}

void PspFreeId(HANDLE id)
{
    ULONG i = (ULONG)((ULONG_PTR)id / 4);
    ULONG64 state = KiAcquireRawLock(&g_cid_lock);
    if (i < g_cid_capacity)
        g_cid_table[i] = nullptr;
    KiReleaseRawLock(&g_cid_lock, state);
}

static PVOID lookup_cid(HANDLE id, POBJECT_TYPE type)
{
    ULONG i = (ULONG)((ULONG_PTR)id / 4);
    PVOID object = nullptr;
    ULONG64 state = KiAcquireRawLock(&g_cid_lock);
    if (i < g_cid_capacity && g_cid_table[i] && OBJECT_TO_HEADER(g_cid_table[i])->Type == type) {
        /* an object on its way out is still listed until its end frees the id, and is no longer to be referenced. */
        OBJECT_HEADER *h = OBJECT_TO_HEADER(g_cid_table[i]);
        LONG_PTR count = __atomic_load_n(&h->PointerCount, __ATOMIC_RELAXED);
        while (count > 0 &&
               !__atomic_compare_exchange_n(&h->PointerCount, &count, count + 1, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
            ;
        if (count > 0)
            object = g_cid_table[i];
    }
    KiReleaseRawLock(&g_cid_lock, state);
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
    MimeSessionUserProgramGone(process);
    MmFreeAddressSpaceRoot(&process->AddressSpace);
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
    static volatile LONG64 made;
    process->SequenceNumber = (ULONG64)__atomic_add_fetch(&made, 1, __ATOMIC_RELAXED);
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
    PETHREAD box = __atomic_exchange_n(&process->HardErrorThread, nullptr, __ATOMIC_ACQ_REL);
    if (box)
        ObDereferenceObject(box);
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
        if (t->HasTerminated)
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
        if (t->HasTerminated)
            continue;
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

/* the image's device path, as SeLocateProcessImageName keeps it: \Device\HarddiskVolumeN then the path. bytes, no nul. */
ULONG PsGetDeviceImagePath(PEPROCESS process, PWCHAR out, ULONG capacity)
{
    const WCHAR *path = process->ImagePath.Buffer;
    ULONG n = process->ImagePath.Length, prefix = 0;
    if (n >= 2 * sizeof(WCHAR) && path[1] == L':') {
        WCHAR letter = path[0] & ~0x20;
        ULONG host = letter == L'C' ? MIME_FS_VOLUME_SYSTEM
                     : letter >= MIME_FS_DRIVE_FIRST && letter <= MIME_FS_DRIVE_LAST ? MIME_FS_VOLUME_DRIVE + letter - L'A'
                                                                                     : MIME_FS_VOLUME_HOST;
        WCHAR volume[40];
        prefix = MimeFsVolumeDeviceName(host, volume, RTL_NUMBER_OF(volume)) * sizeof(WCHAR);
        if (prefix > capacity)
            return 0;
        memcpy(out, volume, prefix);
        path += 2;
        n -= 2 * sizeof(WCHAR);
    }
    if (prefix + n > capacity)
        n = capacity - prefix;
    memcpy((PUCHAR)out + prefix, path, n);
    return prefix + n;
}

/*
 * the right each class needs, as NtQueryInformationProcess asks ObReferenceObjectByHandle for it: most need only the
 * limited query right, a few the full one, some also memory or handle rights.
 */
static ACCESS_MASK query_rights(PROCESSINFOCLASS klass)
{
    switch ((ULONG)klass) {
    case 7: case 14: case 19: case 23: case 30: case 31: case 32: case 44: case 49: case 54: case 55: case 59:
    case 72: case 73: case 87: case 96:
        return PROCESS_QUERY_INFORMATION;
    case 10: case 34:
        return PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;
    case 28: case 58:
        return PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE;
    case 84:
        return PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_OPERATION;
    default:
        return PROCESS_QUERY_LIMITED_INFORMATION;
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
    ULONG64 state = ObLockHandleTable(table);
    ULONG capacity = table->Capacity;
    ObUnlockHandleTable(table, state);
    PULONG values = ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)(capacity ? capacity : 1) * sizeof(ULONG), KI_TAG('P', 's', 'H', 'T'));
    if (!values)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG count = 0;
    state = ObLockHandleTable(table);
    for (ULONG i = 0; i < table->Capacity && i < capacity; i++)
        if (table->Entries[i].Object)
            values[count++] = (i + 1) * 4;
    ObUnlockHandleTable(table, state);
    /* measured on windows 11 26300: the handles that fit are given, and a room for none is refused. */
    *size = count * sizeof(ULONG) < (length & ~3u) ? count * sizeof(ULONG) : (length & ~3u);
    NTSTATUS status = length < sizeof(ULONG) ? STATUS_INFO_LENGTH_MISMATCH : KiCopyToUser(info, values, *size);
    ExFreePool(values);
    return status;
}

/* the sum of the times of its threads. */
static void process_times(PEPROCESS process, ULONG64 *times)
{
    ULONG64 s = KiAcquireDispatcherLock();
    ULONG64 kernel = process->GoneKernelTime, user = process->GoneUserTime;
    for (PLIST_ENTRY e = process->ThreadListHead.Flink; e != &process->ThreadListHead; e = e->Flink) {
        PETHREAD t = CONTAINING_RECORD(e, ETHREAD, ThreadListEntry);
        /* windows charges whole clock ticks to each thread. */
        kernel += t->Tcb.KernelTime - t->Tcb.KernelTime % KI_CLOCK_TICK_HNS;
        user += t->Tcb.UserTime - t->Tcb.UserTime % KI_CLOCK_TICK_HNS;
    }
    KiReleaseDispatcherLock(s);
    times[0] = process->CreateTime;
    times[1] = process->Exited ? process->ExitTime : 0;
    times[2] = kernel;
    times[3] = user;
}

#define ProcessDeviceMap 23
#ifndef FILE_DEVICE_CD_ROM
#define FILE_DEVICE_CD_ROM 0x00000002
#endif
#ifndef FILE_REMOVABLE_MEDIA
#define FILE_REMOVABLE_MEDIA 0x00000001
#endif

/*
 * like ObQueryDeviceMapInformation: a bit for each drive letter the dos device directory has, and its type, a
 * DOSDEVICE_DRIVE value, from the device the link leads to.
 */
static void query_device_map(PULONG drives, PUCHAR types)
{
    *drives = 0;
    memset(types, 0, 32);
    for (ULONG i = 0; i < 26; i++) {
        WCHAR path[] = L"\\GLOBAL??\\X:";
        path[10] = (WCHAR)(L'A' + i);
        UNICODE_STRING name;
        RtlInitUnicodeString(&name, path);
        PVOID link;
        if (!NT_SUCCESS(ObLookupObjectName(&name, nullptr, OBJ_OPENLINK | OBJ_CASE_INSENSITIVE, ObSymbolicLinkType,
                                           KernelMode, nullptr, &link)))
            continue;
        ObDereferenceObject(link);
        PDEVICE_OBJECT device;
        if (!NT_SUCCESS(ObLookupObjectName(&name, nullptr, OBJ_CASE_INSENSITIVE, IoDeviceObjectType, KernelMode, nullptr,
                                           (PVOID *)&device)))
            continue;
        *drives |= 1u << i;
        types[i] = 3; /* DOSDEVICE_DRIVE_FIXED */
        if (device->DeviceType == FILE_DEVICE_CD_ROM)
            types[i] = 5;
        else if (device->Characteristics & FILE_REMOTE_DEVICE)
            types[i] = 4;
        else if (device->Characteristics & FILE_REMOVABLE_MEDIA)
            types[i] = 2;
        ObDereferenceObject(device);
    }
}

/* reads the process parameters' bytes at an offset in the process, the 64-bit block even for wow64. */
static NTSTATUS read_parameters(PEPROCESS process, ULONG offset, PVOID out, ULONG size)
{
    if (!process->Peb)
        return STATUS_PROCESS_IS_TERMINATING;
    PKPROCESS saved;
    KeStackAttachProcess(&process->Pcb, &saved);
    ULONG_PTR parameters = 0;
    NTSTATUS status = KiCopyFromUser(&parameters, (PUCHAR)process->Peb + 0x20, sizeof(parameters));
    if (NT_SUCCESS(status))
        status = parameters ? KiCopyFromUser(out, (PUCHAR)parameters + offset, size) : STATUS_UNSUCCESSFUL;
    KeUnstackDetachProcess(&saved);
    return status;
}

/* a string of the process parameters, at most capacity bytes, read from the process. */
static NTSTATUS read_parameter_string(PEPROCESS process, ULONG offset, PWCHAR out, ULONG capacity, PULONG bytes)
{
    UNICODE_STRING s;
    NTSTATUS status = read_parameters(process, offset, &s, sizeof(s));
    if (!NT_SUCCESS(status))
        return status;
    *bytes = s.Length;
    if (s.Length > capacity)
        return STATUS_BUFFER_OVERFLOW;
    PKPROCESS saved;
    KeStackAttachProcess(&process->Pcb, &saved);
    status = s.Length ? KiCopyFromUser(out, s.Buffer, s.Length) : STATUS_SUCCESS;
    KeUnstackDetachProcess(&saved);
    return status;
}

/*
 * ProcessCommandLineInformation, ProcessWindowInformation, ProcessUptimeInformation and
 * ProcessSecurityDomainInformation, as windows answers them; FALSE for any other class.
 */
static BOOLEAN query_more(PEPROCESS process, ULONG klass, PVOID info, ULONG length, PULONG returned, NTSTATUS *result)
{
    enum { TEXT_BYTES = 0x8000 };
    if (klass != 50 && klass != 60 && klass != 88 && klass != 92 && klass != 94)
        return FALSE;
    PWCHAR text = nullptr;
    if (klass == 50 || klass == 60) {
        text = ExAllocatePool2(POOL_FLAG_NON_PAGED, TEXT_BYTES + sizeof(WCHAR), KI_TAG('P', 's', 'P', 'a'));
        if (!text) {
            *result = STATUS_INSUFFICIENT_RESOURCES;
            return TRUE;
        }
    }
    ULONG size = 0;
    NTSTATUS status;
    switch (klass) {
    case 60: { /* a UNICODE_STRING and its text after it, its buffer pointing there. */
        ULONG bytes = 0;
        status = read_parameter_string(process, 0x70, text, TEXT_BYTES, &bytes);
        size = sizeof(UNICODE_STRING) + bytes + sizeof(WCHAR);
        if (NT_SUCCESS(status) && length < size)
            status = STATUS_INFO_LENGTH_MISMATCH;
        if (NT_SUCCESS(status)) {
            text[bytes / sizeof(WCHAR)] = 0;
            UNICODE_STRING us = {(USHORT)bytes, (USHORT)(bytes + sizeof(WCHAR)), (PWSTR)((PUCHAR)info + sizeof(UNICODE_STRING))};
            status = KiCopyToUser(info, &us, sizeof(us));
            if (NT_SUCCESS(status))
                status = KiCopyToUser((PUCHAR)info + sizeof(us), text, bytes + sizeof(WCHAR));
        }
        break;
    }
    case 50: { /* the window flags, the title's length and the title, which only user mode may ask for. */
        if (KeGetPreviousMode() != UserMode) {
            status = STATUS_UNSUCCESSFUL;
            break;
        }
        ULONG flags = 0;
        status = read_parameters(process, 0xA4, &flags, sizeof(flags));
        ULONG bytes = 0;
        if (NT_SUCCESS(status))
            status = read_parameter_string(process, 0xB0, text, TEXT_BYTES, &bytes);
        size = 6 + bytes;
        if (NT_SUCCESS(status) && length < size)
            status = STATUS_INFO_LENGTH_MISMATCH;
        if (NT_SUCCESS(status)) {
            UCHAR head[6];
            *(PULONG)head = flags;
            *(PUSHORT)(head + 4) = (USHORT)bytes;
            status = KiCopyToUser(info, head, sizeof(head));
            if (NT_SUCCESS(status) && bytes)
                status = KiCopyToUser((PUCHAR)info + 6, text, bytes);
        }
        break;
    }
    case 88: { /* PROCESS_UPTIME_INFORMATION, cut to the buffer. */
        ULONG64 u[7] = {};
        LARGE_INTEGER system_time;
        KeQuerySystemTime(&system_time);
        ULONG64 now = KeQueryInterruptTime(), system = (ULONG64)system_time.QuadPart;
        u[0] = now;
        u[1] = now;
        u[2] = process->Exited ? now - (system - process->ExitTime) : 0;
        u[3] = (process->Exited ? process->ExitTime : system) - process->CreateTime;
        u[4] = u[3];
        u[5] = 0;
        u[6] = process->Exited ? 0x200 : 0; /* Terminated */
        size = length < sizeof(u) ? length : sizeof(u);
        status = KiCopyToUser(info, u, size);
        break;
    }
    case 92:   /* measured on windows 11 26300: a count that grows with every process made. */
    case 94: { /* the security domain, one for each process. */
        if (length != sizeof(ULONG64)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        ULONG64 domain = (ULONG)klass == 92 ? process->SequenceNumber
                                            : 0x100000000ull | (ULONG64)(ULONG_PTR)process->UniqueProcessId;
        size = sizeof(domain);
        status = KiCopyToUser(info, &domain, size);
        break;
    }
    }
    if (text)
        ExFreePool(text);
    if (returned && (NT_SUCCESS(status) || status == STATUS_INFO_LENGTH_MISMATCH))
        KiCopyToUser(returned, &size, sizeof(size));
    *result = status;
    return TRUE;
}

static NTSTATUS query_process(HANDLE handle, PROCESSINFOCLASS klass, PVOID info, ULONG length, PULONG returned);

/*
 * the buffer and the returned length are probed first, then the length is held against what windows takes for the
 * class, see classref.h, and only then is the process looked at. the two classes this system keeps for itself
 * are left to it.
 */
NTSTATUS NTAPI NtQueryInformationProcess(HANDLE handle, PROCESSINFOCLASS klass, PVOID info, ULONG length,
                                         PULONG returned)
{
    ULONG alignment = sizeof(ULONG);
    switch ((ULONG)klass) {
    case 18:
    case 61:
    case 70:
    case 74:
    case 87:
        alignment = 1;
        break;
    case 47:
        alignment = 2;
        break;
    case 65:
    case 92:
    case 94:
    case 115:
        alignment = 8;
        break;
    default:
        break;
    }
    NTSTATUS status = KiProbeQueryBuffer(info, length, alignment, TRUE);
    if (NT_SUCCESS(status))
        status = KiProbeReturnLength(returned);
    if (!NT_SUCCESS(status))
        return status;
    PS_CLASS_ANSWER reference;
    /* the shutdown parameters are asked with eight bytes by kernel32; windows has a class of four bytes at that number. */
    BOOLEAN known = KeGetPreviousMode() == UserMode && (ULONG)klass != MX_PROCESS_PRIORITY_CLASS_EX &&
                    !((ULONG)klass == MX_PROCESS_SHUTDOWN_PARAMETERS && length == 2 * sizeof(ULONG)) &&
                    (ULONG)klass != ProcessHandleTable && /* as many handles as the room holds: no length of its own */
                    /* the reference was taken in steps of eight: it missed the 36 bytes windows takes, and its 48 bytes
                       had flags of zero. query_process checks both lengths and the flags as windows does. */
                    (ULONG)klass != ProcessDeviceMap &&
                    PspClassAnswer(PspProcessClassRuns, RTL_NUMBER_OF(PspProcessClassRuns), (ULONG)klass, length, &reference);
    if (!known)
        return query_process(handle, klass, info, length, returned);
    if (reference.Decided) {
        if (returned && reference.Returned != PS_CLASS_NO_LENGTH)
            KiCopyToUser(returned, &reference.Returned, sizeof(ULONG));
        return reference.Status;
    }
    /* a class of one length returns that length or leaves the caller's alone, as windows does. */
    ULONG before = 0;
    BOOLEAN fixed = returned && !reference.Variable;
    if (fixed)
        KiCopyFromUser(&before, returned, sizeof(before));
    status = query_process(handle, klass, info, length, returned);
    if (status == STATUS_INVALID_INFO_CLASS)
        return PspClassDefault(&reference, info, length, returned);
    if (fixed && NT_SUCCESS(status))
        KiCopyToUser(returned, reference.Returned != PS_CLASS_NO_LENGTH ? &reference.Returned : &before, sizeof(ULONG));
    return status;
}

static NTSTATUS query_process(HANDLE handle, PROCESSINFOCLASS klass, PVOID info, ULONG length, PULONG returned)
{
    NTSTATUS status;
    /* like windows, these two check the length before the handle. */
    if ((ULONG)klass == ProcessDeviceMap) {
        /* PROCESS_DEVICEMAP_INFORMATION, or the _EX form whose flags may only ask for the luid device map. */
        if (length != 36 && length != 48)
            return STATUS_INFO_LENGTH_MISMATCH;
        if (length == 48) {
            ULONG flags;
            status = KiCopyFromUser(&flags, (PUCHAR)info + 40, sizeof(flags));
            if (!NT_SUCCESS(status))
                return status;
            if (flags & ~1u)
                return STATUS_INVALID_PARAMETER;
        }
    } else if ((ULONG)klass == ProcessImageInformation && length != sizeof(SECTION_IMAGE_INFORMATION)) {
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    PEPROCESS process;
    status = PsReferenceProcessHandle(handle, query_rights(klass), &process);
    if (!NT_SUCCESS(status))
        return status;
    if (query_more(process, (ULONG)klass, info, length, returned, &status)) {
        ObDereferenceObject(process);
        return status;
    }
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
    case ProcessDeviceMap: {
        ULONG drives = 0, flags = 0;
        UCHAR types[32] = {};
        if (length == 48)
            KiCopyFromUser(&flags, (PUCHAR)info + 40, sizeof(flags));
        /* flag 1 asks for the drives of the logon session alone, and mime has none: windows then answers no drive. */
        if (!(flags & 1))
            query_device_map(&drives, types);
        status = KiCopyToUser(info, &drives, sizeof(drives));
        if (NT_SUCCESS(status))
            status = KiCopyToUser((PUCHAR)info + 4, types, sizeof(types));
        if (NT_SUCCESS(status) && returned)
            KiCopyToUser(returned, &length, sizeof(length));
        ObDereferenceObject(process);
        return status;
    }
    case ProcessImageInformation:
        if (!process->ImageSection) {
            /* a live process without a user image, like System or the memory compression process, answers the way
               windows 11 26300 does, measured with scratch/dlls/probe/pimg.c: invalid parameter, not terminating. */
            status = process->Terminating || process->ExitStatusReady ? STATUS_PROCESS_IS_TERMINATING
                                                                      : STATUS_INVALID_PARAMETER;
            break;
        }
        MmGetSectionImageInformation(process->ImageSection, (SECTION_IMAGE_INFORMATION *)out.bytes);
        size = sizeof(SECTION_IMAGE_INFORMATION);
        break;
    case MX_PROCESS_SHUTDOWN_PARAMETERS:
        if (length == sizeof(ULONG)) { /* the class windows has at this number: a zero dword. */
            size = sizeof(ULONG);
            break;
        }
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
        /*
         * default working set limits, which bind nothing here. measured on windows 11: no limit on the page file, the
         * time and the working set reads as all ones, and the pool limits are those of the session's quota block.
         * GlobalMemoryStatusEx caps the commit limit by the page file limit, so a zero there reads as no page file.
         */
        ULONG64 *quota = (ULONG64 *)out.bytes;
        quota[0] = 0x3b05faa;
        quota[1] = 0x13ac0c4;
        quota[2] = 200 * PAGE_SIZE;
        quota[3] = 1345 * PAGE_SIZE;
        quota[4] = ~0ull;
        quota[5] = ~0ull;
        if (length >= 0x58)
            quota[6] = ~0ull;
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
        /* the count alone, or with the peak after it: dbghelp asks for the four bytes, and a dump with handles failed. */
        out.u32[0] = out.u32[1] = (ULONG)process->HandleTable.Count;
        size = length == 4 ? 4 : 8;
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
        /* permanent DEP. a 64-bit process of windows 11 answers 0xd: disabled, no thunk emulation, permanent. */
        out.u32[0] = PspIsWow64Process(process) ? 0x39 : 0xD;
        size = sizeof(ULONG);
        break;
    case ProcessProtectionInformation:
        size = 1;
        break;
    case ProcessSessionInformation:
        out.u32[0] = process->SessionId;
        size = sizeof(ULONG);
        break;
    case ProcessAffinityMask: /* the mask alone, or with the group it is in: ten bytes of the sixteen are written. */
        out.times[0] = KiProcessAffinity(&process->Pcb);
        size = length == 0x10 ? 0x10 : sizeof(ULONG64);
        if (length == 0x10) {
            ObDereferenceObject(process);
            if (returned)
                KiCopyToUser(returned, &size, sizeof(size));
            return KiCopyToUser(info, &out, 10);
        }
        break;
    case 28: /* ProcessLUIDDeviceMapsEnabled: each logon has its own drive letters. */
        out.u32[0] = 1;
        size = sizeof(ULONG);
        break;
    case 33: /* ProcessIoPriority: normal. */
        out.u32[0] = 2;
        size = sizeof(ULONG);
        break;
    case 39: /* ProcessPagePriority: normal. */
        out.u32[0] = 5;
        size = sizeof(ULONG);
        break;
    case 71: /* whether NtClose may raise for an invalid handle. windows leaves the returned length alone. */
        out.u32[0] = process->HandleTable.RaiseOnInvalidClose;
        ObDereferenceObject(process);
        if (length != sizeof(ULONG))
            return STATUS_INFO_LENGTH_MISMATCH;
        return KiCopyToUser(info, &out, sizeof(ULONG));
    case 89: /* 0xc0 on windows 11 26300 for a process started at normal priority. */
        out.times[0] = 0xC0;
        size = sizeof(ULONG64);
        break;
    case 54: /* ProcessHandleCheckingMode and ProcessCheckStackExtentsMode: off. */
    case 59:
        size = sizeof(ULONG);
        break;
    case 55: /* ProcessKeepAliveCount: nothing keeps it. */
        size = sizeof(ULONG64);
        break;
    case 87: /* one byte whatever the room, 2 on windows 11 26300 for a process of the logged on user. */
    case 96: /* and 0x3e here. */
        ObDereferenceObject(process);
        if (!length)
            return STATUS_INFO_LENGTH_MISMATCH;
        out.bytes[0] = (ULONG)klass == 87 ? 2 : 0x3E;
        return KiCopyToUser(info, &out, 1);
    case 106: /* 0x10 on windows 11 26300 for a process started at normal priority. */
    case 110: /* the io and page priorities in effect: normal. */
    case 111:
        out.u32[0] = (ULONG)klass == 106 ? 0x10 : (ULONG)klass == 110 ? 2 : 5;
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
        if (returned && NT_SUCCESS(status))
            KiCopyToUser(returned, &size, sizeof(size));
        ObDereferenceObject(process);
        return status;
    case ProcessImageFileName:
    case ProcessImageFileNameWin32: {
        UNICODE_STRING *us = (UNICODE_STRING *)out.bytes;
        PUCHAR text = out.bytes + sizeof(UNICODE_STRING);
        ULONG capacity = sizeof(out.bytes) - sizeof(UNICODE_STRING) - sizeof(WCHAR);
        /* ProcessImageFileName gives the device path, the win32 class the drive path. */
        ULONG n = (ULONG)klass == ProcessImageFileName ? PsGetDeviceImagePath(process, (PWCHAR)text, capacity)
                  : process->ImagePath.Length <= capacity ? process->ImagePath.Length : capacity;
        if ((ULONG)klass != ProcessImageFileName)
            memcpy(text, process->ImagePath.Buffer, n);
        us->Length = (USHORT)n;
        us->MaximumLength = (USHORT)(n + 2);
        us->Buffer = (PWSTR)((PUCHAR)info + sizeof(UNICODE_STRING));
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

static void set_process_affinity(PEPROCESS process, ULONG64 mask)
{
    KiSetAffinityProcess(&process->Pcb, mask);
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

/*
 * ProcessTlsInformation, measured on windows 11 26300 with scratch/dlls/probe/tlsinfo.c of the research repository.
 * the loader gives one entry for each of its threads when a dll with thread local storage arrives after they
 * started. operation 0 puts the block of each entry in a slot of the vector the teb of a thread points to, operation 1
 * replaces the vector itself and brings over its first slots, as many as the header tells. the threads are taken
 * in the order they were made, as many as there are entries; one that has no vector yet, because it never ran, is
 * passed over and uses no entry. an entry that was used tells 2 in its flags and what was there before, and the id
 * of the thread when the vector was replaced.
 */
static NTSTATUS set_tls_information(HANDLE handle, PVOID info, ULONG length)
{
    struct {
        ULONG Flags, Operation, Count, Index;
    } head;
    typedef struct {
        ULONG Flags, Pad;
        ULONG64 Data, ThreadId;
    } TLS_ENTRY;
    if (length < sizeof(head))
        return STATUS_INFO_LENGTH_MISMATCH;
    NTSTATUS status = KiCopyFromUser(&head, info, sizeof(head));
    if (!NT_SUCCESS(status))
        return status;
    if (head.Operation > 1 || (head.Flags & ~1u) || !head.Count ||
        length != sizeof(head) + (ULONG64)head.Count * sizeof(TLS_ENTRY))
        return STATUS_INFO_LENGTH_MISMATCH;
    PEPROCESS process;
    status = PsReferenceProcessHandle(handle, PROCESS_SET_INFORMATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    /* flag 1, as ntoskrnl 26300 has it: the vectors of the 32-bit tebs of a wow64 process, of 32-bit pointers. */
    BOOLEAN wow64 = (head.Flags & 1) != 0;
    if (wow64 && !PspIsWow64Process(process)) {
        ObDereferenceObject(process);
        return STATUS_INVALID_PARAMETER;
    }
    ULONG pointer = wow64 ? sizeof(ULONG) : sizeof(PVOID);
    TLS_ENTRY *entries = ExAllocatePool2(POOL_FLAG_NON_PAGED, head.Count * sizeof(TLS_ENTRY), KI_TAG('P', 's', 'T', 'l'));
    typedef struct {
        PTEB Teb;
        HANDLE Id;
    } TLS_THREAD;
    ULONG capacity = (ULONG)process->ThreadCount + 16, count = 0;
    TLS_THREAD *threads = ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity * sizeof(TLS_THREAD), KI_TAG('P', 's', 'T', 'l'));
    if (!entries || !threads)
        status = STATUS_INSUFFICIENT_RESOURCES;
    /* the tebs are written through the address space of the caller. */
    else if (process != PsCurrentProcess())
        status = STATUS_NOT_SUPPORTED;
    else
        status = KiCopyFromUser(entries, (PUCHAR)info + sizeof(head), head.Count * sizeof(TLS_ENTRY));
    for (ULONG i = 0; NT_SUCCESS(status) && i < head.Count; i++)
        if (entries[i].Flags)
            status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(status)) {
        ULONG64 state = KiAcquireDispatcherLock();
        for (PLIST_ENTRY e = process->ThreadListHead.Flink; e != &process->ThreadListHead && count < capacity; e = e->Flink) {
            PETHREAD thread = CONTAINING_RECORD(e, ETHREAD, ThreadListEntry);
            if (thread->Tcb.Teb && !thread->Tcb.Terminating)
                threads[count++] = (TLS_THREAD){thread->Tcb.Teb, thread->Cid.UniqueThread};
        }
        KiReleaseDispatcherLock(state);
        ULONG used = 0;
        for (ULONG i = 0; i < count && used < head.Count; i++) {
            PVOID field = wow64 ? (PVOID)((PUCHAR)threads[i].Teb + MIME_TEB_SIZE + 0x2C)
                                : (PVOID)&threads[i].Teb->ThreadLocalStoragePointer;
            ULONG64 vector = 0;
            if (!NT_SUCCESS(KiCopyFromUser(&vector, field, pointer)) || !vector)
                continue;
            TLS_ENTRY *entry = &entries[used];
            ULONG64 next = entry->Data;
            if (head.Operation == 0) {
                ULONG64 old = 0;
                PVOID slot = (PUCHAR)vector + (SIZE_T)head.Index * pointer;
                if (!NT_SUCCESS(KiCopyFromUser(&old, slot, pointer)) || !NT_SUCCESS(KiCopyToUser(slot, &next, pointer)))
                    continue;
                entry->Data = old;
            } else {
                BOOLEAN moved = TRUE;
                for (ULONG k = 0; moved && k < head.Index; k++) {
                    ULONG64 slot = 0;
                    moved = NT_SUCCESS(KiCopyFromUser(&slot, (PUCHAR)vector + (SIZE_T)k * pointer, pointer)) &&
                            NT_SUCCESS(KiCopyToUser((PUCHAR)next + (SIZE_T)k * pointer, &slot, pointer));
                }
                if (!moved || !NT_SUCCESS(KiCopyToUser(field, &next, pointer)))
                    continue;
                entry->Data = vector;
                entry->ThreadId = (ULONG64)threads[i].Id;
            }
            entry->Flags = 2;
            used++;
        }
        status = KiCopyToUser((PUCHAR)info + sizeof(head), entries, head.Count * sizeof(TLS_ENTRY));
    }
    if (entries)
        ExFreePool(entries);
    if (threads)
        ExFreePool(threads);
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI NtSetInformationProcess(HANDLE handle, PROCESSINFOCLASS klass, PVOID info, ULONG length)
{
    if ((ULONG)klass == 35) /* ProcessTlsInformation */
        return set_tls_information(handle, info, length);
    if ((ULONG)klass == 53) { /* ProcessDynamicFunctionTableInformation */
        struct {
            PVOID Table;
            BOOLEAN Remove;
        } table = {};
        if (length != sizeof(table))
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!NT_SUCCESS(KiCopyFromUser(&table, info, sizeof(table))))
            return STATUS_ACCESS_VIOLATION;
        /* the tables unwind dynamic code: user mode unwinds its own, so they are only checked here. */
        if (!table.Table && !table.Remove)
            return STATUS_INVALID_PARAMETER;
        return STATUS_SUCCESS;
    }
    if (klass == ProcessAccessToken)
        return SepSetProcessAccessToken(handle, info, length);
    if ((ULONG)klass == 40) /* ProcessInstrumentationCallback */
        return set_instrumentation_callback(handle, info, length);
    if (klass == ProcessMitigationPolicy)
        return PspSetMitigationPolicy(handle, info, length);
    if ((ULONG)klass == 48) { /* ProcessTokenVirtualizationEnabled, as ntoskrnl 26300 sets it on the primary token. */
        ULONG enable;
        if (length != sizeof(enable))
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!NT_SUCCESS(KiCopyFromUser(&enable, info, sizeof(enable))))
            return STATUS_ACCESS_VIOLATION;
        PEPROCESS process;
        NTSTATUS status = PsReferenceProcessHandle(handle, PROCESS_SET_INFORMATION, &process);
        if (!NT_SUCCESS(status))
            return status;
        PTOKEN token = SepReferencePrimaryToken(process);
        if (token) {
            SeSetVirtualizationToken(token, enable != 0);
            ObDereferenceObject(token);
        }
        ObDereferenceObject(process);
        return STATUS_SUCCESS;
    }
    if ((ULONG)klass == 71) {
        /*
         * ProcessRaiseUMExceptionOnInvalidHandleClose. kernelbase clears it from CloseHandle until the call
         * succeeds, unless the process is debugged. as ntoskrnl 26300 does: four bytes, the set information
         * right, 0 clears the flag of the handle table, 1 sets it, and any other value is taken and ignored.
         */
        ULONG raise;
        NTSTATUS probed = KiProbeQueryBuffer((PVOID)info, length, sizeof(ULONG), TRUE);
        if (!NT_SUCCESS(probed))
            return probed;
        if (length != sizeof(raise))
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!NT_SUCCESS(KiCopyFromUser(&raise, info, sizeof(raise))))
            return STATUS_ACCESS_VIOLATION;
        PEPROCESS process;
        NTSTATUS status = PsReferenceProcessHandle(handle, PROCESS_SET_INFORMATION, &process);
        if (!NT_SUCCESS(status))
            return status;
        if (raise <= 1)
            process->HandleTable.RaiseOnInvalidClose = raise != 0;
        ObDereferenceObject(process);
        return STATUS_SUCCESS;
    }
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
    case ProcessEnableAlignmentFaultFixup:
        expected = sizeof(BOOLEAN);
        break;
    case MX_PROCESS_SHUTDOWN_PARAMETERS:
        expected = sizeof(MX_SHUTDOWN_PARAMETERS);
        break;
    default:
        return STATUS_INVALID_INFO_CLASS;
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
    case ProcessEnableAlignmentFaultFixup:
        /* the fixup of the process is the SEM_NOALIGNMENTFAULTEXCEPT bit of its error mode. */
        if ((UCHAR)value)
            process->DefaultHardErrorMode |= 4;
        else
            process->DefaultHardErrorMode &= ~4u;
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
extern PEPROCESS PsSubsystemProcess;

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
    else if (process == PsSubsystemProcess && (access & ~(PROCESS_QUERY_LIMITED_INFORMATION | 0x2000 | SYNCHRONIZE)))
        status = STATUS_ACCESS_DENIED; /* a protected process: the debug privilege opens it for the limited rights only. */
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
    else if ((PEPROCESS)thread->Tcb.Process == PsSubsystemProcess && (access & ~(0x800 | 0x400 | SYNCHRONIZE)))
        status = STATUS_ACCESS_DENIED;
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

/*
 * NtGetNextProcess and NtGetNextThread, as ntoskrnl 26300 does them. the handle of the caller is cleared first. a
 * process or thread the caller may not open is passed over, any other failure of the open ends the walk, and the end
 * of the list is STATUS_NO_MORE_ENTRIES. attributes a caller may not give are dropped, and those a process or a thread
 * refuses fail the open. processes and threads come in the order they were made, those that ended too, as long as
 * something holds them.
 */
#define NEXT_USER_ATTRIBUTES 0x1DF2
#define NEXT_KERNEL_ATTRIBUTES 0x11FF2
#define NEXT_INVALID_ATTRIBUTES 0xB0 /* OBJ_PERMANENT, OBJ_EXCLUSIVE and OBJ_OPENIF. */
#define GENERIC_BITS (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL)

static NTSTATUS clear_out_handle(PHANDLE out, KPROCESSOR_MODE mode)
{
    HANDLE none = nullptr;
    if (mode == UserMode)
        return KiCopyToUser(out, &none, sizeof(none));
    *out = nullptr;
    return STATUS_SUCCESS;
}

/* opens the process or thread for the caller as NtOpenProcess and NtOpenThread do. the caller's reference stays. */
static NTSTATUS open_next(PVOID object, ACCESS_MASK access, ULONG attributes, BOOLEAN thread, PHANDLE out)
{
    if (attributes & NEXT_INVALID_ATTRIBUTES)
        return STATUS_INVALID_PARAMETER;
    /* windows maps the generic rights of a thread with the mapping of processes. */
    if (thread && (access & GENERIC_BITS) && !(access & MAXIMUM_ALLOWED))
        access = ((access & ~GENERIC_BITS) | map_process_access(access & GENERIC_BITS)) & THREAD_ALL_ACCESS;
    PEPROCESS owner = thread ? (PEPROCESS)((PETHREAD)object)->Tcb.Process : (PEPROCESS)object;
    ACCESS_MASK granted = access;
    NTSTATUS status = STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode && !SeThreadHasPrivilege(SE_DEBUG_PRIVILEGE))
        status = ObCheckOpenAccess(object, access, UserMode, &granted);
    else if (owner == PsSubsystemProcess &&
             (access & ~(thread ? (0x800 | 0x400 | SYNCHRONIZE) : (PROCESS_QUERY_LIMITED_INFORMATION | 0x2000 | SYNCHRONIZE))))
        status = STATUS_ACCESS_DENIED;
    if (!NT_SUCCESS(status))
        return status;
    ObReferenceObject(object);
    HANDLE h;
    status = ObInsertObject(object, thread ? map_thread_access(granted) : map_process_access(granted), attributes, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(out, h);
    return status;
}

/* the process after the one given, or before it, with a reference. the first or the last for none. */
static PEPROCESS next_process(PEPROCESS previous, BOOLEAN reverse)
{
    PEPROCESS found = nullptr;
    ULONG64 s = KiAcquireDispatcherLock();
    PLIST_ENTRY e = previous ? (reverse ? previous->ActiveProcessLinks.Blink : previous->ActiveProcessLinks.Flink)
                             : (reverse ? PsActiveProcessHead.Blink : PsActiveProcessHead.Flink);
    for (; e != &PsActiveProcessHead && !found; e = reverse ? e->Blink : e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (ObReferenceObjectSafe(p))
            found = p;
    }
    KiReleaseDispatcherLock(s);
    return found;
}

NTSTATUS NTAPI NtGetNextProcess(HANDLE process_handle, ACCESS_MASK access, ULONG attributes, ULONG flags, PHANDLE out)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    attributes &= mode == UserMode ? NEXT_USER_ATTRIBUTES : NEXT_KERNEL_ATTRIBUTES;
    NTSTATUS status = clear_out_handle(out, mode);
    if (!NT_SUCCESS(status))
        return status;
    if (flags & ~1u)
        return STATUS_INVALID_PARAMETER;
    PEPROCESS previous = nullptr;
    if (process_handle) {
        status = ObReferenceHandle(process_handle, PsProcessTypeObject, mode, (PVOID *)&previous, nullptr);
        if (!NT_SUCCESS(status))
            return status;
    }
    for (;;) {
        PEPROCESS next = next_process(previous, (flags & 1) != 0);
        if (previous)
            ObDereferenceObject(previous);
        previous = next;
        if (!next)
            return STATUS_NO_MORE_ENTRIES;
        status = open_next(next, access, attributes, FALSE, out);
        if (status != STATUS_ACCESS_DENIED) {
            ObDereferenceObject(next);
            return status;
        }
    }
}

/* the thread of the process made after the one given, with a reference, or the first for none. */
static PETHREAD next_thread(PEPROCESS process, PETHREAD previous)
{
    ULONG64 after = previous ? previous->ListSequence : 0;
    PETHREAD found = nullptr;
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = process->ThreadListHead.Flink; e != &process->ThreadListHead && !found; e = e->Flink) {
        PETHREAD t = CONTAINING_RECORD(e, ETHREAD, ThreadListEntry);
        if (t->ListSequence > after && ObReferenceObjectSafe(t))
            found = t;
    }
    KiReleaseDispatcherLock(s);
    return found;
}

NTSTATUS NTAPI NtGetNextThread(HANDLE process_handle, HANDLE thread_handle, ACCESS_MASK access, ULONG attributes,
                               ULONG flags, PHANDLE out)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    attributes &= mode == UserMode ? NEXT_USER_ATTRIBUTES : NEXT_KERNEL_ATTRIBUTES;
    NTSTATUS status = clear_out_handle(out, mode);
    if (!NT_SUCCESS(status))
        return status;
    if (flags)
        return STATUS_INVALID_PARAMETER;
    PEPROCESS process;
    status = ObReferenceHandleAccess(process_handle, PROCESS_QUERY_INFORMATION, PsProcessTypeObject, mode,
                                     (PVOID *)&process, "NtGetNextThread");
    if (!NT_SUCCESS(status))
        return status;
    PETHREAD previous = nullptr;
    if (thread_handle) {
        status = ObReferenceHandle(thread_handle, PsThreadTypeObject, mode, (PVOID *)&previous, nullptr);
        if (NT_SUCCESS(status) && (PEPROCESS)previous->Tcb.Process != process) {
            ObDereferenceObject(previous);
            status = STATUS_INVALID_PARAMETER;
        }
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(process);
            return status;
        }
    }
    for (;;) {
        PETHREAD next = next_thread(process, previous);
        if (previous)
            ObDereferenceObject(previous);
        previous = next;
        if (!next) {
            status = STATUS_NO_MORE_ENTRIES;
            break;
        }
        status = open_next(next, access, attributes, TRUE, out);
        if (status != STATUS_ACCESS_DENIED) {
            ObDereferenceObject(next);
            break;
        }
    }
    ObDereferenceObject(process);
    return status;
}

/*
 * the windows subsystem process of the interactive session. windows 11 runs a csrss.exe there, which owns the window
 * of the desktop: the process list shows it, and a program finds its ids through the window but opens neither the
 * process nor its threads, whose descriptors let the system alone in, c0000022 at any access. here win32k is the
 * whole subsystem, so the entry is made by the kernel and its one thread waits for ever, as the desktop thread does
 * most of the time.
 */
PEPROCESS PsSubsystemProcess;
static PETHREAD g_subsystem_thread;

static void NTAPI subsystem_thread(PVOID context)
{
    static KEVENT never;
    KeInitializeEvent(&never, NotificationEvent, FALSE);
    for (;;)
        KeWaitForSingleObject(&never, WrUserRequest, KernelMode, FALSE, nullptr);
}

void PspCreateSubsystemProcess(void)
{
    /* owner and group the system, and one ace that gives it everything. */
    static const UCHAR system_only[] = {
        0x01, 0x00, 0x04, 0x80, 0x14, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x12, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x12, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x1c, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x14, 0x00, 0xff, 0xff, 0x1f, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x12, 0x00, 0x00, 0x00,
    };
    static const WCHAR image[] = L"C:\\Windows\\System32\\csrss.exe";
    PEPROCESS process;
    if (!NT_SUCCESS(PspCreateProcessObject(nullptr, &process)))
        return;
    /* the session manager that started it is gone: the parent is an id no process has. */
    HANDLE parent = nullptr;
    if (NT_SUCCESS(PspAllocateId(process, &parent)))
        process->InheritedFromUniqueProcessId = parent;
    process->SystemProcess = TRUE;
    process->SessionId = 1;
    process->Pcb.BasePriority = 13;
    process->PriorityClass = PROCESS_PRIORITY_CLASS_HIGH;
    process->DefaultHardErrorMode = 1;
    process->Token = PsInitialSystemProcess->Token;
    if (process->Token)
        ObReferenceObject(process->Token);
    memcpy(process->ImagePathBuffer, image, sizeof(image));
    RtlInitUnicodeString(&process->ImagePath, process->ImagePathBuffer);
    memcpy(process->ImageFileName, "csrss.exe", 10);
    SD_BLOB *sd;
    if (NT_SUCCESS(SeSdFromBytes(system_only, sizeof(system_only), &sd)))
        ObAttachSecurity(process, sd);
    PETHREAD thread;
    if (NT_SUCCESS(PspCreateThread(process, TRUE, subsystem_thread, nullptr, &thread))) {
        if (NT_SUCCESS(SeSdFromBytes(system_only, sizeof(system_only), &sd)))
            ObAttachSecurity(thread, sd);
        KiStartThread(&thread->Tcb);
        g_subsystem_thread = thread;
    }
    if (parent)
        PspFreeId(parent);
    PsSubsystemProcess = process;
    /* measured with prebox.exe: a program of the user does not open the system process either, c0000022 at the limited query right. */
    if (NT_SUCCESS(SeSdFromBytes(system_only, sizeof(system_only), &sd)))
        ObAttachSecurity(PsInitialSystemProcess, sd);
}

/* the process and the thread that own the window of the desktop. */
void NTAPI PsQuerySubsystemIds(PHANDLE process, PHANDLE thread)
{
    *process = PsSubsystemProcess ? PsSubsystemProcess->UniqueProcessId : nullptr;
    *thread = g_subsystem_thread ? g_subsystem_thread->Cid.UniqueThread : nullptr;
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
    extern FAST_MUTEX PspSharedSlotLock;
    ExInitializeFastMutex(&PspSharedSlotLock);
    PspInitializePebBlocks();
    PspSxsInitialize();
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
    /* the mode every process starts from: hard errors raise their box. the win32 side shows the bit inverted. */
    system->DefaultHardErrorMode = 1;
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
    /* its first handle is itself, as on windows. */
    HANDLE self;
    ObCreateHandle(system, system, PROCESS_ALL_ACCESS, OBJ_KERNEL_HANDLE, &self);

    PsInitializeIdleThread();
}
