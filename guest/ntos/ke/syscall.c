/*
 * system service dispatch and user memory access.
 */
#include "../ntos.h"

#define MIME_NT_SERVICES_DEFINE_NAMES
#include "mime_svc_nt.h"
#define MIME_WIN32K_SERVICES_DEFINE_NAMES
#include "mime_svc_win32k.h"

typedef struct KI_SERVICE_TABLE {
    PKSERVICE_DESCRIPTOR Table;
    ULONG Count;
} KI_SERVICE_TABLE;

static KSERVICE_DESCRIPTOR g_nt_services[MIME_NT_SERVICES_COUNT];
static KI_SERVICE_TABLE g_tables[2] = {{g_nt_services, MIME_NT_SERVICES_COUNT}, {nullptr, 0}};
static UCHAR g_reported[2][4096 / 8];
/* calls of each service, for the thread dump; racy counts are good enough for that. */
static ULONG64 g_calls[2][4096];
/* ticks each service held the kernel lock, minus the time its thread waited, and its longest call. */
static ULONG64 g_ticks[2][4096], g_longest[2][4096];

/* long kernel lock holds, by the service or vector that held it, see KiNoteLongHold. */
#define KI_HOLD_REASONS (0x2001 + 256)
static ULONG64 g_long_ticks[KI_HOLD_REASONS], g_long_max[KI_HOLD_REASONS], g_long_calls[KI_HOLD_REASONS];
static ULONG64 g_long_total;
void AfdDumpStats(void);
/* per hypercall: calls, time taken, and how much of it with the kernel lock held. the x64 hal counts them. */
ULONG64 HalHypercallCalls[64], HalHypercallTicks[64], HalHypercallLongest[64], HalHypercallLockedTicks[64];

/* the time processors waited for the lock, by what they took it for. 0 is a thread that goes on after a wait. */
static ULONG64 g_wait_ticks[KI_HOLD_REASONS], g_wait_calls[KI_HOLD_REASONS];

/* called with the lock, once what it was taken for is known. the fifth counter is the wait already told. */
void KiNoteLockWait(ULONG reason)
{
    ULONG64 *stats = KiLockStats[KeGetCurrentPrcb()->Number];
    ULONG64 waited = stats[2] - stats[4];
    if (!waited || stats[2] < stats[4]) {
        stats[4] = stats[2];
        return;
    }
    stats[4] = stats[2];
    if (reason >= KI_HOLD_REASONS)
        reason = 0;
    g_wait_ticks[reason] += waited;
    g_wait_calls[reason]++;
}

/* called as the lock is released after being held for longer than KiLongHoldTicks, with the lock still held. */
void KiNoteLongHold(ULONG64 held)
{
    ULONG reason = KiKernelLock[KI_LOCK_REASON];
    if (reason >= KI_HOLD_REASONS)
        reason = 0;
    g_long_ticks[reason] += held;
    g_long_calls[reason]++;
    g_long_total++;
    if (held > g_long_max[reason])
        g_long_max[reason] = held;
}

static WIN32_CALLOUTS g_win32;

void NTAPI PsEstablishWin32Callouts(WIN32_CALLOUTS *callouts)
{
    g_win32 = *callouts;
}

NTSTATUS PsWin32ProcessCallout(PEPROCESS process, BOOLEAN initialize)
{
    return g_win32.ProcessCallout ? g_win32.ProcessCallout(process, initialize) : STATUS_SUCCESS;
}

void PsWin32TimeChange(void)
{
    if (g_win32.TimeChange)
        g_win32.TimeChange();
}

ULONG PsWin32ShutdownProcess(PEPROCESS process, const MX_SHUTDOWN_CONTEXT *context)
{
    return g_win32.ShutdownProcess ? g_win32.ShutdownProcess(process, context) : MX_SHUTDOWN_PROCESS_PASS;
}

NTSTATUS PsWin32ThreadCallout(PETHREAD thread, BOOLEAN initialize)
{
    return g_win32.ThreadCallout ? g_win32.ThreadCallout(thread, initialize) : STATUS_SUCCESS;
}

static NTSTATUS convert_to_gui_thread(void)
{
    PETHREAD thread = PsCurrentThread();
    PEPROCESS process = PsCurrentProcess();
    if (!g_win32.ThreadCallout)
        return STATUS_INVALID_SYSTEM_SERVICE;
    if (!process->Win32Process) {
        NTSTATUS status = g_win32.ProcessCallout(process, TRUE);
        if (!NT_SUCCESS(status))
            return status;
    }
    return g_win32.ThreadCallout(thread, TRUE);
}

NTSTATUS KeRegisterSystemServices(ULONG table, PKSERVICE_DESCRIPTOR services, ULONG count)
{
    if (table == 0) {
        for (ULONG i = 0; i < count; i++)
            g_nt_services[i] = services[i];
    }
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI KeAddSystemServiceTable(PKSERVICE_DESCRIPTOR table, PVOID unused, ULONG count, PVOID arguments,
                                      ULONG index)
{
    if (index != 1 || g_tables[1].Table)
        return FALSE;
    g_tables[1].Table = table;
    g_tables[1].Count = count;
    return TRUE;
}

void KiRegisterService(ULONG number, PVOID routine, UCHAR stack_args)
{
    if (number < MIME_NT_SERVICES_COUNT) {
        g_nt_services[number].Routine = routine;
        g_nt_services[number].StackArgs = stack_args;
        g_nt_services[number].Implemented = 1;
    }
}

/* the services the generated list does not carry yet. */
void KiRegisterLateServices(void)
{
    extern void NtQuerySystemInformationEx(void);
    KiRegisterService(MIME_SVC_NtQuerySystemInformationEx, (void *)NtQuerySystemInformationEx,
                      6 > KI_SERVICE_REGISTER_ARGS ? 6 - KI_SERVICE_REGISTER_ARGS : 0);
}

static const char *service_name(ULONG table, ULONG index)
{
    if (table == 2)
        return index == 0 ? "fault-fast" : index == 1 ? "fault-slow" : index == 2 ? "fault-fast-miss"
               : index == 3 ? "ioc-post" : "fault";
    if (table == 0 && index < MIME_NT_SERVICES_COUNT && MIME_NT_SERVICES_NAMES[index])
        return MIME_NT_SERVICES_NAMES[index];
    if (table == 1 && index < MIME_WIN32K_SERVICES_COUNT && MIME_WIN32K_SERVICES_NAMES[index])
        return MIME_WIN32K_SERVICES_NAMES[index];
    return "?";
}

/* what a service leaves to do once it has its result. the kernel lock is held and interrupts are on. */
static void finish_service(PKTHREAD thread, PKTRAP_FRAME frame, ULONG table, ULONG index, ULONG64 result,
                           ULONG64 entry_pc)
{
    /* the level belongs to the processor, so a service that leaves it raised would break the next thread to wait here. */
    KIRQL left = KiCurrentIrql();
    if (left != PASSIVE_LEVEL) {
        KiLogPrintf("service %s returned at irql %u", service_name(table, index), (ULONG)left);
        KeLowerIrql(PASSIVE_LEVEL);
    }
    KI_SERVICE_RESULT(frame) = result;
#ifdef __x86_64__
    thread->ContextSetInService = FALSE;
    KiServiceExitVolatiles(frame);
#else
    if (thread->ContextSetInService) {
        thread->ContextSetInService = FALSE;
        KiServiceExitVolatiles(frame);
    }
#endif
    PETHREAD ethread = PsCurrentThread();
    BOOLEAN raised = FALSE;
    if (ethread->PendingException) {
        raised = TRUE;
        EXCEPTION_RECORD record = {};
        record.ExceptionCode = ethread->PendingException;
        record.ExceptionAddress = (PVOID)KI_TRAP_PC(frame);
        ethread->PendingException = 0;
        if (PspIsWow64Process(PsCurrentProcess())) {
            /* the program is emulated, so the emulator raises it for the calling code, as the code in the teb tells it. */
            ULONG code = (ULONG)record.ExceptionCode;
            KiCopyToUser(&ethread->Tcb.Teb->ExceptionCode, &code, sizeof(code));
        } else if (!KiDispatchUserException(&record, frame)) {
            PspTerminateProcess(PsCurrentProcess(), record.ExceptionCode);
        }
    }
#ifdef __x86_64__
    /* an exception raised for the service left the frame of its dispatcher, which is no return of the service. */
    thread->ServiceExit = !raised;
#else
    /* a service that returns where it was called from goes through the instrumentation callback. */
    if (KI_TRAP_PC(frame) == entry_pc)
        thread->InstrumentReturn = TRUE;
#endif
    KiExitToUserMode(frame);
}

#ifdef __x86_64__
void KiLeaveFastService(PKTHREAD thread)
{
    PKPCR pcr = KeGetPcr();
    if (KiKernelLock[KI_LOCK_OWNER] != pcr->Prcb.Number) {
        /* a processor that changes an address space waits for those in user mode, and the wait for the lock answers none. */
        pcr->UserMode = 0;
        KiAcquireKernelLock();
    }
    /* the frame may hold the result by now, not the number. */
    KiKernelLock[KI_LOCK_REASON] = thread->FastReason;
    KiNoteLockWait(thread->FastReason);
    thread->FastService = FALSE;
    pcr->Prcb.FastServicesLocked++;
    KiEnableInterrupts();
}

enum {
    FAST_WAIT,
    FAST_RELEASE,
    FAST_SET,
    FAST_RESET,
    FAST_DELAY,
    FAST_WAIT_MANY,
    FAST_REMOVE,
    FAST_POST,
    FAST_PARK,
    FAST_UNPARK,
    FAST_TEST_ALERT,
    FAST_QUERY,
};
static_assert(FAST_QUERY + 1 == KI_FAST_SERVICE_KINDS);

/* a wait on more objects than this is left to the usual way: the wait blocks are on the stack. */
#define FAST_WAIT_OBJECTS 8
#define FAST_OUTPUTS 7

typedef struct FAST_OUTPUT {
    PVOID Address;
    ULONG64 Value;
    ULONG Size;
} FAST_OUTPUT;

static BOOLEAN fast_timeout(ULONG64 address, PLARGE_INTEGER storage, PLARGE_INTEGER *timeout)
{
    *timeout = nullptr;
    if (!address)
        return TRUE;
    if (!KiIsUserRange((PVOID)address, sizeof(*storage)) || !KiFastReadUser64((PVOID)address, (ULONG64 *)&storage->QuadPart))
        return FALSE;
    *timeout = storage;
    return TRUE;
}

/*
 * like ProbeForWrite, for a range of at most two pages. a locked instruction on a value that spans two cache lines
 * stalls the whole machine, so a pointer that is not aligned is left to the usual way.
 */
static BOOLEAN fast_probe(ULONG64 address, ULONG length)
{
    return !(address & 3) && KiIsUserRange((PVOID)address, length) && KiFastProbeUser32((PVOID)address) &&
           KiFastProbeUser32((PVOID)(address + length - sizeof(ULONG)));
}

static BOOLEAN fast_stack_argument(PKTRAP_FRAME frame, ULONG index, ULONG64 *value)
{
    PVOID address = (PVOID)(KI_SERVICE_STACK_ARGS(frame) + index * sizeof(ULONG64));
    return KiIsUserRange(address, sizeof(*value)) && KiFastReadUser64(address, value);
}

/*
 * a call that is found not to be a plain one once it holds objects. the last reference to one ends it, which is
 * the kernel lock's work, and the call then goes the usual way from here.
 */
static ULONG fast_decline(PKTHREAD thread, PKTRAP_FRAME frame, PVOID objects[], ULONG held)
{
    while (held && ObDereferenceObjectFast(objects[held - 1]))
        held--;
    if (!held)
        return 0;
    KeGetPcr()->UserMode = 0;
    KiAcquireKernelLock();
    thread->TrapFrame = frame;
    thread->PreviousMode = UserMode;
    KiEnableInterrupts();
    while (held)
        ObDereferenceObject(objects[--held]);
    KiDisableInterrupts();
    KiSystemService(frame);
    return 2;
}

/*
 * the services that work on what the dispatcher lock guards are tried without the kernel lock: the waits, on one
 * object or a few, on a completion port and for an alert by thread id, the delays, and what ends such waits, a
 * semaphore release, an event set, a packet posted to a port and the alert. so is the query of a region of the
 * caller's memory, a reader of its address space's gate. interrupts stay off from the entry to
 * the return, as they are for a holder of the dispatcher lock. UserMode stays set as well, so the caller's memory
 * is read as user mode would read it: a processor that unmaps it waits for this one.
 *
 * anything else such a call can need takes the lock, before the work, when the handle or the arguments are not
 * the plain ones, or after it: an apc, the end of the thread, a context set meanwhile, the last reference to an
 * object. a thread that waited comes back on any processor, which may hold the lock from the thread before.
 */
/* gdi32 left records in the batch of the teb, which a service of win32k runs first, see gdi/batch.c of win32k. */
static BOOLEAN gdi_batch_pending(PKTHREAD thread)
{
    PTEB teb = thread->Teb;
    return teb && *(volatile ULONG *)&teb->GdiBatchCount != 0;
}

ULONG KiFastSystemService(PKTRAP_FRAME frame)
{
    ULONG number = KI_SERVICE_NUMBER(frame);
#ifdef __x86_64__
    /* the single step of a service that returns is raised the usual way, see KiServiceSingleStep. */
    if (frame->Rflags & 0x100)
        return 0;
#endif
    /* the callback a service returns through is the one of a process that had one as the service was entered. */
    KiCurrentThread()->ServiceInstrumented = ((PEPROCESS)KiCurrentThread()->Process)->InstrumentationCallback != nullptr;
    if (number >= MIME_WIN32K_SERVICES_BASE) {
        /* nothing is done when win32k declines, and whatever is pending for the thread is left to the usual way. */
        PKTHREAD current = KiCurrentThread();
        PKPCR pcr = KeGetPcr();
        ULONG64 answer;
        if (!g_win32.FastService ||
            pcr->Irql != PASSIVE_LEVEL || current->KernelApcPending || current->Terminating || current->UserApcPending ||
            pcr->Prcb.DpcPending || current->ContextSetInService || ((PETHREAD)current)->PendingException ||
            gdi_batch_pending(current) || !g_win32.FastService(number, KI_SERVICE_REGISTER_ARG(frame, 0), &answer))
            return 0;
        KI_SERVICE_RESULT(frame) = answer;
#ifdef __x86_64__
        KiServiceExitVolatiles(frame);
#endif
        if (current->ServiceInstrumented)
            KiInstrumentServiceReturn(frame, ((PEPROCESS)current->Process)->InstrumentationCallback);
        pcr->Prcb.FastWin32Services++;
        return 1;
    }
    ULONG kind;
    switch (number) {
    case MIME_SVC_NtWaitForSingleObject:
        kind = FAST_WAIT;
        break;
    case MIME_SVC_NtReleaseSemaphore:
        kind = FAST_RELEASE;
        break;
    case MIME_SVC_NtSetEvent:
        kind = FAST_SET;
        break;
    case MIME_SVC_NtResetEvent:
    case MIME_SVC_NtClearEvent:
        kind = FAST_RESET;
        break;
    case MIME_SVC_NtDelayExecution:
    case MIME_SVC_NtYieldExecution:
        kind = FAST_DELAY;
        break;
    case MIME_SVC_NtWaitForMultipleObjects:
        kind = FAST_WAIT_MANY;
        break;
    case MIME_SVC_NtRemoveIoCompletion:
        kind = FAST_REMOVE;
        break;
    case MIME_SVC_NtSetIoCompletion:
        kind = FAST_POST;
        break;
    case MIME_SVC_NtWaitForAlertByThreadId:
        kind = FAST_PARK;
        break;
    case MIME_SVC_NtAlertThreadByThreadId:
    case MIME_SVC_NtAlertThreadByThreadIdEx:
        kind = FAST_UNPARK;
        break;
    case MIME_SVC_NtTestAlert:
        kind = FAST_TEST_ALERT;
        break;
    case MIME_SVC_NtQueryVirtualMemory:
        kind = FAST_QUERY;
        break;
    default:
        return 0;
    }
    if (KeGetPcr()->Irql != PASSIVE_LEVEL)
        return 0;

    /* what is not plain is left to the usual way, with its status and its exception: nothing is done before. */
    PKTHREAD thread = KiCurrentThread();
    ULONG64 a0 = KI_SERVICE_REGISTER_ARG(frame, 0), a1 = KI_SERVICE_REGISTER_ARG(frame, 1);
    ULONG64 a2 = KI_SERVICE_REGISTER_ARG(frame, 2), a3 = KI_SERVICE_REGISTER_ARG(frame, 3), a4 = 0, a5 = 0;
    PVOID objects[FAST_WAIT_OBJECTS], waits[FAST_WAIT_OBJECTS];
    KWAIT_BLOCK blocks[FAST_WAIT_OBJECTS];
    ULONG held = 0, count = 0;
    LARGE_INTEGER time = {};
    PLARGE_INTEGER timeout = nullptr;
    BOOLEAN alertable = FALSE;
    WAIT_TYPE wait_type = WaitAny;
    PVOID previous = nullptr;
    ULONG64 region[sizeof(MEMORY_BASIC_INFORMATION) / sizeof(ULONG64)];
    static_assert(sizeof(region) == sizeof(MEMORY_BASIC_INFORMATION) && FAST_OUTPUTS > sizeof(region) / sizeof(ULONG64));
    switch (kind) {
    case FAST_WAIT:
        alertable = (BOOLEAN)a1;
        if (!fast_timeout(a2, &time, &timeout))
            return 0;
        objects[0] = ObReferenceHandleFast((HANDLE)a0, SYNCHRONIZE, nullptr, nullptr);
        if (!objects[0])
            return 0;
        held = count = 1;
        waits[0] = ObGetWaitObject(objects[0]);
        if (!waits[0])
            return fast_decline(thread, frame, objects, held);
        break;
    case FAST_RELEASE:
    case FAST_SET:
    case FAST_RESET: {
        previous = kind == FAST_RELEASE ? (PVOID)a2 : number == MIME_SVC_NtClearEvent ? nullptr : (PVOID)a1;
        /* a pointer that cannot be written fails the call before it does anything. */
        if (previous && !fast_probe((ULONG64)previous, sizeof(LONG)))
            return 0;
        POBJECT_TYPE type = kind == FAST_RELEASE ? ExSemaphoreType : ExEventType;
        /* EVENT_MODIFY_STATE and SEMAPHORE_MODIFY_STATE are one bit. */
        objects[0] = ObReferenceHandleFast((HANDLE)a0, EVENT_MODIFY_STATE, type, type);
        if (!objects[0])
            return 0;
        held = 1;
        break;
    }
    case FAST_DELAY:
        if (number == MIME_SVC_NtDelayExecution) {
            alertable = (BOOLEAN)a0;
            if (!KiIsUserRange((PVOID)a1, sizeof(time)) || !KiFastReadUser64((PVOID)a1, (ULONG64 *)&time.QuadPart))
                return 0;
        }
        break;
    case FAST_WAIT_MANY:
        count = (ULONG)a0;
        if (!count || count > FAST_WAIT_OBJECTS || !KiIsUserRange((PVOID)a1, count * sizeof(HANDLE)))
            return 0;
        wait_type = (ULONG)a2 == WaitAll ? WaitAll : WaitAny;
        alertable = (BOOLEAN)a3;
        if (!fast_stack_argument(frame, 0, &a4) || !fast_timeout(a4, &time, &timeout))
            return 0;
        for (; held < count; held++) {
            ULONG64 handle;
            if (!KiFastReadUser64((PVOID)(a1 + held * sizeof(HANDLE)), &handle))
                return fast_decline(thread, frame, objects, held);
            objects[held] = ObReferenceHandleFast((HANDLE)handle, SYNCHRONIZE, nullptr, nullptr);
            if (!objects[held])
                return fast_decline(thread, frame, objects, held);
            waits[held] = ObGetWaitObject(objects[held]);
            if (!waits[held])
                return fast_decline(thread, frame, objects, held + 1);
        }
        break;
    case FAST_REMOVE:
        /* pointers that cannot be written fail the call before it takes a packet. */
        if (!fast_probe(a1, sizeof(PVOID)) || !fast_probe(a2, sizeof(PVOID)) || !fast_probe(a3, sizeof(IO_STATUS_BLOCK)) ||
            !fast_stack_argument(frame, 0, &a4) || !fast_timeout(a4, &time, &timeout))
            return 0;
        objects[0] = ObReferenceHandleFast((HANDLE)a0, 0x2 /* IO_COMPLETION_MODIFY_STATE */, IoCompletionObjectType, IoCompletionObjectType);
        if (!objects[0])
            return 0;
        held = count = 1;
        /* the semaphore that counts the packets is the first thing in a port. */
        waits[0] = objects[0];
        break;
    case FAST_POST:
        if (!fast_stack_argument(frame, 0, &a4))
            return 0;
        objects[0] = ObReferenceHandleFast((HANDLE)a0, 0x2 /* IO_COMPLETION_MODIFY_STATE */, IoCompletionObjectType, IoCompletionObjectType);
        if (!objects[0])
            return 0;
        held = 1;
        break;
    case FAST_PARK:
        if (!fast_timeout(a1, &time, &timeout))
            return 0;
        waits[0] = &thread->AlertByIdEvent;
        count = 1;
        break;
    case FAST_UNPARK:
        objects[0] = PspLookupThread((HANDLE)a0);
        if (!objects[0])
            return 0;
        held = 1;
        break;
    case FAST_QUERY:
        /* the regions of a process's memory. the answer is found here, which changes nothing. */
        if ((ULONG)a2 != MemoryBasicInformation || !fast_stack_argument(frame, 0, &a4) || a4 < sizeof(region) ||
            !fast_stack_argument(frame, 1, &a5))
            return 0;
        if ((HANDLE)a0 != NtCurrentProcess()) {
            objects[0] = ObReferenceHandleFast((HANDLE)a0, PROCESS_QUERY_INFORMATION, PsProcessTypeObject, PsProcessTypeObject);
            if (!objects[0])
                return 0;
            held = 1;
        }
        if (!MmFastQueryVirtual(held ? objects[0] : nullptr, (PVOID)a1, (PMEMORY_BASIC_INFORMATION)region))
            return held ? fast_decline(thread, frame, objects, held) : 0;
        break;
    default: {
        /* the suspension of an emulated thread, a suspension, an apc and an alert are the usual way's. */
        if (thread->UserSuspendGate)
            return 0;
        ULONG64 s = KiAcquireDispatcherLock();
        BOOLEAN plain = !thread->SuspendCount && IsListEmpty(&thread->ApcListHead[UserMode]) && !thread->Alerted[UserMode];
        KiReleaseDispatcherLock(s);
        if (!plain)
            return 0;
        break;
    }
    }

    ULONG64 entry_pc = KI_TRAP_PC(frame);
    thread->TrapFrame = frame;
    thread->PreviousMode = UserMode;
    thread->FastService = TRUE;
    thread->FastReason = number + 1;
    FAST_OUTPUT outputs[FAST_OUTPUTS];
    ULONG output_count = 0, written = 0;
    NTSTATUS status = STATUS_SUCCESS;
    LONG old = 0;
    switch (kind) {
    case FAST_WAIT:
    case FAST_WAIT_MANY:
    case FAST_REMOVE:
    case FAST_PARK:
        status = KiWaitObjects(count, waits, wait_type, UserRequest, UserMode, alertable, timeout,
                               count > THREAD_WAIT_OBJECTS ? blocks : nullptr);
        if (kind == FAST_PARK && status == STATUS_WAIT_0) {
            status = STATUS_ALERTED;
        } else if (kind == FAST_REMOVE && status == STATUS_SUCCESS) {
            ULONG64 values[4];
            if (IopRemoveCompletionPacket(objects[0], values)) {
                outputs[0] = (FAST_OUTPUT){(PVOID)a1, values[0], sizeof(ULONG64)};
                outputs[1] = (FAST_OUTPUT){(PVOID)a2, values[1], sizeof(ULONG64)};
                outputs[2] = (FAST_OUTPUT){(PVOID)a3, values[2], sizeof(ULONG64)};
                outputs[3] = (FAST_OUTPUT){(PVOID)(a3 + sizeof(ULONG64)), values[3], sizeof(ULONG64)};
                output_count = 4;
            }
        }
        break;
    case FAST_RELEASE:
        old = KeReleaseSemaphore(objects[0], 1, (LONG)a1, FALSE);
        if (old < 0) {
            status = STATUS_SEMAPHORE_LIMIT_EXCEEDED;
            previous = nullptr;
        }
        break;
    case FAST_SET:
        old = KeSetEvent(objects[0], 1, TRUE);
        break;
    case FAST_RESET:
        old = KeResetEvent(objects[0]);
        break;
    case FAST_DELAY:
        status = KeDelayExecutionThread(UserMode, alertable, &time);
        if (number == MIME_SVC_NtDelayExecution && status == STATUS_NO_YIELD_PERFORMED)
            status = STATUS_SUCCESS;
        break;
    case FAST_POST:
        status = IopQueueCompletion(objects[0], (PVOID)a1, (PVOID)a2, (NTSTATUS)a3, (ULONG_PTR)a4);
        break;
    case FAST_UNPARK:
        if (((PKTHREAD)objects[0])->Process == thread->Process)
            KeSetEvent(&((PKTHREAD)objects[0])->AlertByIdEvent, 0, TRUE);
        else
            status = STATUS_ACCESS_DENIED;
        break;
    case FAST_QUERY:
        for (; output_count < sizeof(region) / sizeof(ULONG64); output_count++)
            outputs[output_count] = (FAST_OUTPUT){(PVOID)(a3 + output_count * sizeof(ULONG64)), region[output_count], sizeof(ULONG64)};
        if (a5)
            outputs[output_count++] = (FAST_OUTPUT){(PVOID)a5, sizeof(region), sizeof(ULONG64)};
        break;
    default:
        break;
    }
    if (previous) {
        outputs[0] = (FAST_OUTPUT){previous, (ULONG)old, sizeof(LONG)};
        output_count = 1;
    }
    /* windows leaves the status in eax, the upper half of rax clear. */
    ULONG64 result = (ULONG)status;
    /* a thread switched away from below shows its result to whoever reads its context. */
    KI_SERVICE_RESULT(frame) = result;
#ifdef __x86_64__
    KiServiceExitVolatiles(frame);
#endif

    /* still set unless a kernel apc in the wait made it a service like any other, see KiWaitObjects. */
    if (thread->FastService) {
        PKPCR pcr;
        BOOLEAN owned;
        for (;;) {
            pcr = KeGetPcr();
            owned = KiKernelLock[KI_LOCK_OWNER] == pcr->Prcb.Number;
            if (owned || !pcr->Prcb.DispatchPending || pcr->Irql != PASSIVE_LEVEL)
                break;
            /* a thread it woke may take this processor, and this one resumes on any. */
            KiCheckPreemption();
        }
        if (!owned && pcr->Irql == PASSIVE_LEVEL) {
            if (output_count) {
                /*
                 * the processor a thread resumes on was not in user mode for those that change the address space.
                 * the order is that of the return to user mode: either this one flushes or the other one waits.
                 */
                __atomic_store_n(&pcr->UserMode, 1, __ATOMIC_SEQ_CST);
                if (pcr->FlushPending)
                    KiSettleTlbFlush();
                /* a write that would fault is made again with the kernel lock. */
                for (; written < output_count; written++) {
                    FAST_OUTPUT *o = &outputs[written];
                    if (!KiIsUserRange(o->Address, o->Size) ||
                        !(o->Size == sizeof(ULONG64) ? KiFastWriteUser64(o->Address, o->Value)
                                                      : KiFastWriteUser32(o->Address, (ULONG)o->Value)))
                        break;
                }
            }
            /*
             * whoever asks for more after this finds the thread running and interrupts it, which is taken in user
             * mode. the instrumentation callback is a change of the frame alone.
             */
            if (written == output_count && !thread->KernelApcPending && !thread->Terminating && !thread->UserApcPending &&
                !pcr->Prcb.DpcPending && !thread->ContextSetInService && !((PETHREAD)thread)->PendingException) {
                while (held && ObDereferenceObjectFast(objects[held - 1]))
                    held--;
                if (!held) {
                    thread->FastService = FALSE;
                    if (thread->ServiceInstrumented)
                        KiInstrumentServiceReturn(frame, ((PEPROCESS)thread->Process)->InstrumentationCallback);
                    pcr->Prcb.FastServices[kind]++;
                    return 1;
                }
            }
        }
        KiLeaveFastService(thread);
    }
    while (held)
        ObDereferenceObject(objects[--held]);
    for (; written < output_count; written++) {
        FAST_OUTPUT *o = &outputs[written];
        if (NT_SUCCESS(KiCopyToUser(o->Address, &o->Value, o->Size)))
            continue;
        if (kind == FAST_QUERY) {
            /* the length is stored whatever became of the region. */
            if (written < sizeof(region) / sizeof(ULONG64)) {
                result = (ULONG)STATUS_ACCESS_VIOLATION;
                written = sizeof(region) / sizeof(ULONG64) - 1;
            }
            continue;
        }
        /* a packet whose values cannot be stored is lost, and the previous state of an object is not asked for again. */
        if (kind == FAST_REMOVE)
            result = (ULONG)STATUS_ACCESS_VIOLATION;
        break;
    }
    finish_service(thread, frame, 0, number, result, entry_pc);
    return 2;
}
#endif

void KiSystemService(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    thread->TrapFrame = frame;
    thread->PreviousMode = UserMode;
    ULONG64 entry_pc = KI_TRAP_PC(frame);
    thread->ServiceInstrumented = ((PEPROCESS)thread->Process)->InstrumentationCallback != nullptr;
    KiEnableInterrupts();

    ULONG number = KI_SERVICE_NUMBER(frame);
    ULONG table = number >> 12;
    ULONG index = number & 0xFFF;
    ULONG64 result;
    ULONG64 started = HalReadCounter(), blocked = thread->BlockedTicks;
    KiKernelLock[KI_LOCK_REASON] = (table << 12 | index) + 1;
    KiNoteLockWait((table << 12 | index) + 1);
    if (table < 2)
        g_calls[table][index]++;
    KI_SERVICE_TABLE *t = table < 2 ? &g_tables[table] : nullptr;
    /* a process with win32k system calls disabled never becomes a gui process and its first call fails, a guess based on the policy's audit only mode. */
    BOOLEAN locked = table == 1 && !thread->Win32Thread &&
                     (PsCurrentProcess()->MitigationPolicy[MIME_POLICY_SYSTEM_CALL_DISABLE] & MIME_SYSTEM_CALL_DISABLE_WIN32K);
    if (table == 1 && !locked && !thread->Win32Thread && g_win32.ThreadCallout) {
        NTSTATUS status = convert_to_gui_thread();
        if (!NT_SUCCESS(status))
            KiLogPrintf("cannot convert thread to a gui thread: %08x", status);
    }
    if (table == 1 && !locked && thread->Win32Thread && g_win32.FlushUserBatch)
        g_win32.FlushUserBatch();
    if (locked) {
        result = (ULONG)STATUS_INVALID_SYSTEM_SERVICE;
    } else if (table == 0 && !strcmp(service_name(table, index), "?")) {
        /* a number past the end of the table of windows. */
        result = (ULONG)STATUS_INVALID_SYSTEM_SERVICE;
    } else if (!t || !t->Table || index >= t->Count || !t->Table[index].Routine) {
        if (table < 2 && !(g_reported[table][index / 8] & (1u << (index % 8)))) {
            g_reported[table][index / 8] |= (UCHAR)(1u << (index % 8));
            KiLogPrintf("unimplemented system service %s (0x%04x) called by %s", service_name(table, index), number,
                        PsCurrentProcess()->ImageFileName);
            /* who calls it, for the first call. */
            KiTraceUserStack(KI_TRAP_PC(frame), KI_TRAP_SP(frame));
        }
        result = table == 1 ? 0 : (ULONG)STATUS_NOT_IMPLEMENTED;
    } else {
        ULONG64 args[16];
        for (int i = 0; i < KI_SERVICE_REGISTER_ARGS; i++)
            args[i] = KI_SERVICE_REGISTER_ARG(frame, i);
        ULONG stack = t->Table[index].StackArgs;
        if (stack > 16 - KI_SERVICE_REGISTER_ARGS)
            stack = 16 - KI_SERVICE_REGISTER_ARGS;
        BOOLEAN copied = !stack || NT_SUCCESS(KiCopyFromUser(&args[KI_SERVICE_REGISTER_ARGS],
                                                             (PVOID)KI_SERVICE_STACK_ARGS(frame),
                                                             stack * sizeof(ULONG64)));
        if (!copied)
            result = (ULONG)STATUS_ACCESS_VIOLATION;
        else
            result = KiCallService(t->Table[index].Routine, args, stack);
    }
    if (table < 2) {
        ULONG64 held = HalReadCounter() - started - (thread->BlockedTicks - blocked);
        g_ticks[table][index] += held;
        if (held > g_longest[table][index])
            g_longest[table][index] = held;
    }
    finish_service(thread, frame, table, index, result, entry_pc);
}

BOOLEAN KiIsUserRange(const void *address, SIZE_T length)
{
    ULONG_PTR a = (ULONG_PTR)address;
    return a + length >= a && a + length <= MIME_USER_ADDRESS_LIMIT + 1;
}

NTSTATUS KiFaultResult(void)
{
    PKTHREAD thread = KiCurrentThread();
    if (!thread->FaultStatus)
        return STATUS_ACCESS_VIOLATION;
    NTSTATUS status = thread->FaultStatus;
    thread->FaultStatus = 0;
    /* the guard page and the end of the stack are what the probe raises, any other failure is an in page error. */
    return status == STATUS_GUARD_PAGE_VIOLATION || status == STATUS_STACK_OVERFLOW ? status : STATUS_IN_PAGE_ERROR;
}

/* copies from caller memory; kernel mode callers, the Zw* services, pass kernel buffers. */
NTSTATUS KiCopyFromUser(void *dst, const void *user_src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (KiCurrentThread()->PreviousMode == KernelMode && !KiIsUserRange(user_src, length)) {
        memcpy(dst, user_src, length);
        return STATUS_SUCCESS;
    }
    if (!KiIsUserRange(user_src, length))
        return STATUS_ACCESS_VIOLATION;
    KJUMP_BUFFER jb;
    if (KI_TRY_USER(jb)) {
        memcpy(dst, user_src, length);
        KI_END_TRY_USER(jb);
        return STATUS_SUCCESS;
    }
    return KiFaultResult();
}

NTSTATUS KiCopyToUser(void *user_dst, const void *src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (KiCurrentThread()->PreviousMode == KernelMode && !KiIsUserRange(user_dst, length)) {
        memcpy(user_dst, src, length);
        return STATUS_SUCCESS;
    }
    if (!KiIsUserRange(user_dst, length))
        return STATUS_ACCESS_VIOLATION;
    KJUMP_BUFFER jb;
    if (KI_TRY_USER(jb)) {
        memcpy(user_dst, src, length);
        KI_END_TRY_USER(jb);
        return STATUS_SUCCESS;
    }
    return KiFaultResult();
}

/* driver facing copies that always treat the pointer as user memory, with fault recovery. */
NTSTATUS NTAPI MimeCopyFromUser(void *dst, const void *user_src, SIZE_T length)
{
    KPROCESSOR_MODE old = KiCurrentThread()->PreviousMode;
    KiCurrentThread()->PreviousMode = UserMode;
    NTSTATUS status = KiCopyFromUser(dst, user_src, length);
    KiCurrentThread()->PreviousMode = old;
    return status;
}

NTSTATUS NTAPI MimeCopyToUser(void *user_dst, const void *src, SIZE_T length)
{
    KPROCESSOR_MODE old = KiCurrentThread()->PreviousMode;
    KiCurrentThread()->PreviousMode = UserMode;
    NTSTATUS status = KiCopyToUser(user_dst, src, length);
    KiCurrentThread()->PreviousMode = old;
    return status;
}

/* captures a caller supplied UNICODE_STRING into pool, honoring the previous mode. */
NTSTATUS KiCaptureUnicodeString(PUNICODE_STRING copy, PCUNICODE_STRING src)
{
    UNICODE_STRING s;
    copy->Buffer = nullptr;
    copy->Length = copy->MaximumLength = 0;
    if (!src)
        return STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode) {
        if (!NT_SUCCESS(KiCopyFromUser(&s, src, sizeof(s))))
            return STATUS_ACCESS_VIOLATION;
    } else {
        s = *src;
    }
    return KiCaptureUnicodeStringValue(copy, s);
}

NTSTATUS KiCaptureUnicodeStringValue(PUNICODE_STRING copy, UNICODE_STRING s)
{
    copy->Buffer = nullptr;
    copy->Length = copy->MaximumLength = 0;
    if (s.Length & 1 || s.Length > 0xFFFC)
        return STATUS_OBJECT_NAME_INVALID;
    copy->Buffer = ExAllocatePool2(POOL_FLAG_PAGED, s.Length + sizeof(WCHAR), KI_TAG('C', 'a', 'p', 'S'));
    if (!copy->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode)
        status = KiCopyFromUser(copy->Buffer, s.Buffer, s.Length);
    else
        memcpy(copy->Buffer, s.Buffer, s.Length);
    if (!NT_SUCCESS(status)) {
        ExFreePool(copy->Buffer);
        copy->Buffer = nullptr;
        return status;
    }
    copy->Buffer[s.Length / sizeof(WCHAR)] = 0;
    copy->Length = s.Length;
    copy->MaximumLength = (USHORT)(s.Length + sizeof(WCHAR));
    return STATUS_SUCCESS;
}

/*
 * the probe windows makes on a caller's return length before it looks at anything else: read and written back, with
 * no alignment asked of it.
 */
NTSTATUS KiProbeReturnLength(PULONG returned)
{
    if (!returned || KeGetPreviousMode() != UserMode)
        return STATUS_SUCCESS;
    ULONG value;
    if (!KiIsUserRange(returned, sizeof(ULONG)) || !NT_SUCCESS(KiCopyFromUser(&value, returned, sizeof(value))) ||
        !NT_SUCCESS(KiCopyToUser(returned, &value, sizeof(value))))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

/* the probe of a query's buffer, which comes before the return length's: alignment first, then the range. */
NTSTATUS KiProbeQueryBuffer(PVOID info, ULONG length, ULONG alignment, BOOLEAN range)
{
    if (!length || KeGetPreviousMode() != UserMode)
        return STATUS_SUCCESS;
    if ((ULONG_PTR)info & (alignment - 1))
        return STATUS_DATATYPE_MISALIGNMENT;
    return !range || KiIsUserRange(info, length) ? STATUS_SUCCESS : STATUS_ACCESS_VIOLATION;
}

void KiFreeCapturedString(PUNICODE_STRING s)
{
    if (s->Buffer)
        ExFreePool(s->Buffer);
    s->Buffer = nullptr;
}

/* the counters of the dump, started over. */
static void reset_dump_counters(void)
{
    memset(HalHypercallCalls, 0, sizeof(HalHypercallCalls));
    memset(HalHypercallTicks, 0, sizeof(HalHypercallTicks));
    memset(HalHypercallLockedTicks, 0, sizeof(HalHypercallLockedTicks));
    memset(HalHypercallLongest, 0, sizeof(HalHypercallLongest));
    memset(g_long_ticks, 0, sizeof(g_long_ticks));
    memset(g_long_calls, 0, sizeof(g_long_calls));
    memset(g_long_max, 0, sizeof(g_long_max));
    g_long_total = 0;
    KiKernelLock[KI_LOCK_HELD] = KiKernelLock[KI_LOCK_HELD + 1] = 0;
    memset(g_calls, 0, sizeof(g_calls));
    memset(g_ticks, 0, sizeof(g_ticks));
    memset(g_longest, 0, sizeof(g_longest));
    memset(KiLockStats, 0, sizeof(KiLockStats));
    memset(MmFaultStats, 0, sizeof(MmFaultStats));
}

/* the services called most since the last dump, then the counts start over. */
void KiDumpServiceCalls(void)
{
    MM_FAULT_STATS total = {};
    for (ULONG i = 0; i < MAXIMUM_PROCESSORS; i++) {
        total.Fast += MmFaultStats[i].Fast;
        total.Gated += MmFaultStats[i].Gated;
        total.Rejected += MmFaultStats[i].Rejected;
        total.Slow += MmFaultStats[i].Slow;
    }
    KiLogPrintf("faults: %llu without the kernel lock, %llu turned away at the gate, %llu not demand zero, %llu with it",
                total.Fast, total.Gated, total.Rejected, total.Slow);
    ULONG64 acquires = 0, contended = 0, waited = 0, longest = 0;
    for (ULONG i = 0; i < MAXIMUM_PROCESSORS; i++) {
        acquires += KiLockStats[i][0];
        contended += KiLockStats[i][1];
        waited += KiLockStats[i][2];
        if (KiLockStats[i][3] > longest)
            longest = KiLockStats[i][3];
    }
    KiLogPrintf("kernel lock: %llu taken, %llu after waiting, %llu us waited in all, longest wait %llu us", acquires,
                contended, HalCounterToHns(waited) / 10, HalCounterToHns(longest) / 10);
    /* the services that held the lock longest, with how often and how long at most. */
    for (int round = 0; round < 12; round++) {
        ULONG64 best = 0;
        ULONG bt = 0, bi = 0;
        for (ULONG t = 0; t < 2; t++)
            for (ULONG i = 0; i < 4096; i++)
                if (g_ticks[t][i] > best)
                    best = g_ticks[t][i], bt = t, bi = i;
        if (!best)
            break;
        ULONG64 calls = g_calls[bt][bi];
        KiLogPrintf("calls %s: %llu, held %llu us in all, %llu ns each, longest %llu us", service_name(bt, bi), calls,
                    HalCounterToHns(best) / 10, calls ? HalCounterToHns(best) * 100 / calls : 0,
                    HalCounterToHns(g_longest[bt][bi]) / 10);
        g_ticks[bt][bi] = 0;
    }
    /* the services called most often, which the list above leaves out when each call is short. */
    for (int round = 0; round < 16; round++) {
        ULONG64 best = 0;
        ULONG bt = 0, bi = 0;
        for (ULONG t = 0; t < 2; t++)
            for (ULONG i = 0; i < 4096; i++)
                if (g_calls[t][i] > best)
                    best = g_calls[t][i], bt = t, bi = i;
        if (!best)
            break;
        KiLogPrintf("most called %s: %llu", service_name(bt, bi), best);
        g_calls[bt][bi] = 0;
    }
    /* the lock as a whole: the time it was held, and what held it for a long time. */
    ULONG64 held = KiKernelLock[KI_LOCK_HELD] | (ULONG64)KiKernelLock[KI_LOCK_HELD + 1] << 32;
    KiLogPrintf("kernel lock: held %llu us in all, %llu holds over %llu us", HalCounterToHns(held) / 10, g_long_total,
                HalCounterToHns(KiLongHoldTicks) / 10);
    for (int round = 0; round < 10; round++) {
        ULONG64 best = 0;
        ULONG bi = 0;
        for (ULONG i = 0; i < KI_HOLD_REASONS; i++)
            if (g_long_ticks[i] > best)
                best = g_long_ticks[i], bi = i;
        if (!best)
            break;
        const char *name = bi < 0x2001 && bi ? service_name((bi - 1) >> 12, (bi - 1) & 0xFFF)
                         : bi == KI_LOCK_REASON_FOREIGN ? "foreign driver call"
                                                         : "trap";
        KiLogPrintf("long holds by %s %u: %llu, %llu us in all, at most %llu us", name,
                    bi < 0x2001 ? 0 : bi - 0x2001, g_long_calls[bi], HalCounterToHns(best) / 10,
                    HalCounterToHns(g_long_max[bi]) / 10);
        g_long_ticks[bi] = 0;
    }
    for (int round = 0; round < 12; round++) {
        ULONG64 best = 0;
        ULONG bi = 0;
        for (ULONG i = 0; i < KI_HOLD_REASONS; i++)
            if (g_wait_ticks[i] > best)
                best = g_wait_ticks[i], bi = i;
        if (!best)
            break;
        const char *name = bi == KI_LOCK_REASON_FOREIGN ? "foreign driver call"
                         : bi >= 0x2001                ? "trap"
                         : bi                          ? service_name((bi - 1) >> 12, (bi - 1) & 0xFFF)
                                                       : "a thread back";
        KiLogPrintf("lock waits by %s %u: %llu, %llu us in all", name, bi < 0x2001 ? 0 : bi - 0x2001, g_wait_calls[bi],
                    HalCounterToHns(best) / 10);
        g_wait_ticks[bi] = 0;
    }
    memset(g_wait_calls, 0, sizeof(g_wait_calls));
    /* the hypercalls made with the lock held, the host's work that every other processor waited for. */
    for (int round = 0; round < 8; round++) {
        ULONG64 best = 0;
        ULONG bi = 0;
        for (ULONG i = 0; i < 64; i++)
            if (HalHypercallTicks[i] > best)
                best = HalHypercallTicks[i], bi = i;
        if (!best)
            break;
        KiLogPrintf("hypercall 0x%02x: %llu, %llu us in all, %llu us of it with the lock, at most %llu us", bi,
                    HalHypercallCalls[bi], HalCounterToHns(best) / 10, HalCounterToHns(HalHypercallLockedTicks[bi]) / 10,
                    HalCounterToHns(HalHypercallLongest[bi]) / 10);
        HalHypercallTicks[bi] = 0;
    }
    /* the idle waits, and how long the processors watch their flags before halting, see KiIdleWait. */
    static ULONG64 ipis_before;
    ULONG64 polls = 0, halts = 0, ipis = 0, watch = 0;
    for (ULONG i = 0; i < KeNumberProcessors; i++) {
        PKPRCB p = KiProcessorBlock[i];
        if (!p)
            continue;
        polls += p->IdlePolls;
        halts += p->IdleHalts;
        ipis += p->IpiSent;
        watch += p->IdlePoll;
        p->IdlePolls = p->IdleHalts = 0;
    }
    KiLogPrintf("idle: %llu waits ended while watching, %llu by an interrupt after a halt, %llu interrupts asked for, "
                "%llu us watched on average",
                polls, halts, ipis - ipis_before, HalCounterToHns(watch / KeNumberProcessors) / 10);
    ipis_before = ipis;
#ifdef __x86_64__
    ULONG64 taken = 0, spun = 0, spin_ticks = 0;
    for (ULONG i = 0; i < KeNumberProcessors; i++) {
        PKPRCB p = KiProcessorBlock[i];
        if (!p)
            continue;
        taken += p->DispatcherTaken;
        spun += p->DispatcherWaited;
        spin_ticks += p->DispatcherWaitTicks;
        p->DispatcherTaken = p->DispatcherWaited = p->DispatcherWaitTicks = 0;
    }
    KiLogPrintf("dispatcher lock: %llu taken, %llu after waiting, %llu us waited in all", taken, spun,
                HalCounterToHns(spin_ticks) / 10);
    ULONG64 fast[KI_FAST_SERVICE_KINDS] = {}, fast_locked = 0, fast_win32 = 0, fast_interrupts = 0, fast_interrupts_locked = 0;
    for (ULONG i = 0; i < KeNumberProcessors; i++) {
        PKPRCB p = KiProcessorBlock[i];
        if (!p)
            continue;
        for (ULONG k = 0; k < KI_FAST_SERVICE_KINDS; k++) {
            fast[k] += p->FastServices[k];
            p->FastServices[k] = 0;
        }
        fast_locked += p->FastServicesLocked;
        fast_win32 += p->FastWin32Services;
        fast_interrupts += p->FastInterrupts;
        fast_interrupts_locked += p->FastInterruptsLocked;
        p->FastServicesLocked = p->FastWin32Services = p->FastInterrupts = p->FastInterruptsLocked = 0;
    }
    KiLogPrintf("services without the kernel lock: %llu waits, %llu semaphore releases, %llu event sets, %llu more "
                "took it to end, %llu answered by win32k", fast[0], fast[1], fast[2], fast_locked, fast_win32);
    KiLogPrintf("more services without the kernel lock: %llu event resets, %llu delays, %llu waits on several, %llu "
                "packets removed, %llu packets posted, %llu waits for an alert, %llu alerts, %llu alert tests, %llu memory "
                "queries",
                fast[FAST_RESET], fast[FAST_DELAY], fast[FAST_WAIT_MANY], fast[FAST_REMOVE], fast[FAST_POST],
                fast[FAST_PARK], fast[FAST_UNPARK], fast[FAST_TEST_ALERT], fast[FAST_QUERY]);
    KiLogPrintf("interrupts in user mode without the kernel lock: %llu, %llu more took it to end", fast_interrupts,
                fast_interrupts_locked);
    KiDumpForeignStats();
#endif
    AfdDumpStats();
    reset_dump_counters();
}
