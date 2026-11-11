/*
 * asynchronous procedure calls, thread contexts, user mode exception
 * dispatch and win32k user mode callbacks.
 */
#include "../ntos.h"
#include <mime/mx64.h>

static inline PPSP_SYSTEM_DLL current_system_dll(void)
{
    return PsCurrentProcess()->SystemDll;
}

void KeInitializeApc(PKAPC apc, PKTHREAD thread, KAPC_ENVIRONMENT env, PKKERNEL_ROUTINE kernel_routine,
                     PKRUNDOWN_ROUTINE rundown, PKNORMAL_ROUTINE normal, KPROCESSOR_MODE mode, PVOID context)
{
    memset(apc, 0, sizeof(*apc));
    apc->Type = ApcObject;
    apc->Size = sizeof(KAPC);
    apc->Thread = thread;
    apc->KernelRoutine = kernel_routine;
    apc->RundownRoutine = rundown;
    apc->NormalRoutine = normal;
    apc->NormalContext = context;
    apc->ApcMode = normal ? mode : KernelMode;
}

BOOLEAN KeInsertQueueApc(PKAPC apc, PVOID arg1, PVOID arg2, KPRIORITY increment)
{
    PKTHREAD thread = apc->Thread;
    ULONG64 s = KiAcquireDispatcherLock();
    if (apc->Inserted || thread->State == Terminated) {
        KiReleaseDispatcherLock(s);
        return FALSE;
    }
    apc->SystemArgument1 = arg1;
    apc->SystemArgument2 = arg2;
    apc->Inserted = TRUE;
    if (apc->ApcMode == KernelMode) {
        if (!apc->NormalRoutine)
            InsertHeadList(&thread->ApcListHead[KernelMode], &apc->ApcListEntry);
        else
            InsertTailList(&thread->ApcListHead[KernelMode], &apc->ApcListEntry);
        thread->KernelApcPending = TRUE;
        if (thread->State == Waiting && !thread->KernelApcDisable && !thread->SpecialApcDisable)
            KiUnwaitThread(thread, STATUS_KERNEL_APC);
        else
            KiInterruptThread(thread);
    } else {
        InsertTailList(&thread->ApcListHead[UserMode], &apc->ApcListEntry);
        if (thread->State == Waiting && thread->WaitMode == UserMode && thread->Alertable) {
            thread->UserApcPending = TRUE;
            KiUnwaitThread(thread, STATUS_USER_APC);
        }
    }
    KiReleaseDispatcherLock(s);
    return TRUE;
}

void KiDeliverApcs(KPROCESSOR_MODE mode, PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    for (;;) {
        ULONG64 s = KiAcquireDispatcherLock();
        if (thread->KernelApcDisable || thread->SpecialApcDisable) {
            KiReleaseDispatcherLock(s);
            return;
        }
        if (IsListEmpty(&thread->ApcListHead[KernelMode])) {
            thread->KernelApcPending = FALSE;
            KiReleaseDispatcherLock(s);
            break;
        }
        PKAPC apc = CONTAINING_RECORD(RemoveHeadList(&thread->ApcListHead[KernelMode]), KAPC, ApcListEntry);
        apc->Inserted = FALSE;
        KiReleaseDispatcherLock(s);
        PKNORMAL_ROUTINE normal = apc->NormalRoutine;
        PVOID ctx = apc->NormalContext, a1 = apc->SystemArgument1, a2 = apc->SystemArgument2;
        apc->KernelRoutine(apc, &normal, &ctx, &a1, &a2);
        if (normal)
            normal(ctx, a1, a2);
    }
    if (mode != UserMode || !frame)
        return;
    ULONG64 s = KiAcquireDispatcherLock();
    thread->UserApcPending = FALSE;
    if (IsListEmpty(&thread->ApcListHead[UserMode])) {
        KiReleaseDispatcherLock(s);
        return;
    }
    PKAPC apc = CONTAINING_RECORD(RemoveHeadList(&thread->ApcListHead[UserMode]), KAPC, ApcListEntry);
    apc->Inserted = FALSE;
    KiReleaseDispatcherLock(s);
    PKNORMAL_ROUTINE normal = apc->NormalRoutine;
    PVOID ctx = apc->NormalContext, a1 = apc->SystemArgument1, a2 = apc->SystemArgument2;
    apc->KernelRoutine(apc, &normal, &ctx, &a1, &a2);
    if (normal && current_system_dll() && current_system_dll()->KiUserApcDispatcher &&
        !KiSetupUserApc(frame, current_system_dll()->KiUserApcDispatcher, normal, ctx, a1, a2))
        PspTerminateProcess(PsCurrentProcess(), STATUS_ACCESS_VIOLATION);
}

NTSTATUS NTAPI NtContinue(PCONTEXT context, BOOLEAN test_alert)
{
    PKTHREAD thread = KiCurrentThread();
    PKTRAP_FRAME frame = thread->TrapFrame;
    CONTEXT ctx;
    if (!NT_SUCCESS(KiCopyFromUser(&ctx, context, sizeof(ctx))))
        return STATUS_ACCESS_VIOLATION;
    KiContextToTrapFrame(&ctx, frame, ctx.ContextFlags, UserMode);
    if ((ctx.ContextFlags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS)
        KiSetThreadDebug(thread, &ctx);
    if (test_alert && !IsListEmpty(&thread->ApcListHead[UserMode]))
        thread->UserApcPending = TRUE;
    /* a continue resumes the context as it is, without the instrumentation callback. */
    thread->InstrumentReturn = FALSE;
    KiExitToUserMode(frame);
    KiEnterUserMode(frame);
}

/* the caller's own user context, or that of a thread stopped in the kernel. */
/*
 * like PspGetSetContextSpecialApc: a thread that runs is asked for its context by a kernel apc, which it takes the
 * next time it enters the kernel, at the latest on the processor's next interrupt. the routine reads or writes the
 * user frame the thread entered with, then wakes the caller.
 */
typedef struct CONTEXT_REQUEST {
    KAPC Apc;
    KEVENT Done;
    CONTEXT *Context;
    ULONG Flags;
    BOOLEAN Set;
    NTSTATUS Status;
} CONTEXT_REQUEST;

static void NTAPI context_apc(PKAPC apc, PKNORMAL_ROUTINE *normal, PVOID *context, PVOID *arg1, PVOID *arg2)
{
    CONTEXT_REQUEST *r = CONTAINING_RECORD(apc, CONTEXT_REQUEST, Apc);
    PKTRAP_FRAME frame = KiCurrentThread()->TrapFrame;
    if (!frame || !KI_TRAP_FROM_USER(frame)) {
        r->Status = STATUS_UNSUCCESSFUL;
    } else {
        r->Status = STATUS_SUCCESS;
        BOOLEAN debug = (r->Flags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS;
        if (r->Set) {
            KiContextToTrapFrame(r->Context, frame, r->Flags, UserMode);
            if (debug)
                KiSetThreadDebug(KiCurrentThread(), r->Context);
        } else {
            KiTrapFrameToContext(frame, r->Context, r->Flags & CONTEXT_ALL);
            if (debug)
                KiGetThreadDebug(KiCurrentThread(), r->Context);
        }
    }
    KeSetEvent(&r->Done, 0, FALSE);
}

static NTSTATUS context_by_apc(PETHREAD thread, CONTEXT *context, ULONG flags, BOOLEAN set)
{
    CONTEXT_REQUEST r;
    KeInitializeEvent(&r.Done, NotificationEvent, FALSE);
    r.Context = context;
    r.Flags = flags;
    r.Set = set;
    r.Status = STATUS_UNSUCCESSFUL;
    KeInitializeApc(&r.Apc, &thread->Tcb, OriginalApcEnvironment, context_apc, nullptr, nullptr, KernelMode, nullptr);
    if (!KeInsertQueueApc(&r.Apc, nullptr, nullptr, 0))
        return STATUS_UNSUCCESSFUL;
    PVOID objects[2] = {&r.Done, thread};
    NTSTATUS status = KeWaitForMultipleObjects(2, objects, WaitAny, Executive, KernelMode, FALSE, nullptr, nullptr);
    return status == STATUS_WAIT_0 ? r.Status : STATUS_THREAD_IS_TERMINATING;
}

/* the frame a thread that does not run left its user state in, or none when it runs and must be asked. */
static PKTRAP_FRAME stopped_frame(PETHREAD thread, NTSTATUS *status)
{
    PKTRAP_FRAME frame = thread->Tcb.TrapFrame;
    if (!frame)
        *status = STATUS_UNSUCCESSFUL;
    return frame && (thread == PsCurrentThread() || thread->Tcb.State != Running) ? frame : nullptr;
}

/* the caller's own user context, or that of another thread, stopped or asked by an apc. */
NTSTATUS NTAPI NtGetContextThread(HANDLE handle, PCONTEXT context)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_GET_CONTEXT, &thread);
    if (!NT_SUCCESS(status))
        return status;
    /* like PspGetContext, only the parts the flags ask for are written, the rest of the caller's record stays. */
    CONTEXT out;
    status = KiCopyFromUser(&out, context, sizeof(out));
    ULONG flags = out.ContextFlags;
    if (NT_SUCCESS(status))
        status = PspWaitForSuspension(thread);
    CONTEXT ctx;
    if (NT_SUCCESS(status)) {
        ULONG64 state = KiAcquireDispatcherLock();
        PKTRAP_FRAME frame = stopped_frame(thread, &status);
        if (frame) {
            KiTrapFrameToContext(frame, &ctx, flags & CONTEXT_ALL);
            if ((flags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS)
                KiGetThreadDebug(&thread->Tcb, &ctx);
        }
        KiReleaseDispatcherLock(state);
        if (NT_SUCCESS(status) && !frame)
            status = context_by_apc(thread, &ctx, flags, FALSE);
    }
    if (NT_SUCCESS(status)) {
        KiMergeContext(&out, &ctx, flags);
        status = KiCopyToUser(context, &out, sizeof(out));
    }
    ObDereferenceObject(thread);
    return status;
}

NTSTATUS NTAPI NtSetContextThread(HANDLE handle, PCONTEXT context)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_SET_CONTEXT, &thread);
    if (!NT_SUCCESS(status))
        return status;
    CONTEXT ctx;
    status = KiCopyFromUser(&ctx, context, sizeof(ctx));
    if (NT_SUCCESS(status))
        status = PspWaitForSuspension(thread);
    PKTRAP_FRAME frame = nullptr;
    if (NT_SUCCESS(status)) {
        ULONG64 state = KiAcquireDispatcherLock();
        frame = stopped_frame(thread, &status);
        if (frame) {
            KiContextToTrapFrame(&ctx, frame, ctx.ContextFlags, UserMode);
            if ((ctx.ContextFlags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS)
                KiSetThreadDebug(&thread->Tcb, &ctx);
            if (thread != PsCurrentThread() && !thread->FirstEntryContext && KI_TRAP_IN_SERVICE(frame))
                thread->Tcb.ContextSetInService = TRUE;
        }
        KiReleaseDispatcherLock(state);
        if (NT_SUCCESS(status) && !frame)
            status = context_by_apc(thread, &ctx, ctx.ContextFlags, TRUE);
    }
    /* the caller's own registers resume at once; its breakpoints alone take effect without, and it gets the status. */
    if (NT_SUCCESS(status) && frame && thread == PsCurrentThread() && (ctx.ContextFlags & 0xB)) {
        ObDereferenceObject(thread);
        KiExitToUserMode(frame);
        KiEnterUserMode(frame);
    }
    ObDereferenceObject(thread);
    return status;
}

NTSTATUS NTAPI NtTestAlert(void)
{
    PKTHREAD thread = KiCurrentThread();
    if (!thread->SuspendCount && thread->UserSuspendGate) {
        ULONG pending = 0;
        NTSTATUS status = KiCopyToUser((PVOID)&((MX_SUSPEND_GATE *)thread->UserSuspendGate)->Pending, &pending, sizeof(pending));
        if (!NT_SUCCESS(status))
            return status;
    }
    PspCheckForSuspendApc(thread);
    BOOLEAN apcs = !IsListEmpty(&thread->ApcListHead[UserMode]);
    if (apcs)
        thread->UserApcPending = TRUE;
    if (thread->Alerted[UserMode]) {
        thread->Alerted[UserMode] = FALSE;
        return STATUS_ALERTED;
    }
    /* the status survives apc delivery, so the caller learns that apcs ran. */
    return apcs ? STATUS_USER_APC : STATUS_SUCCESS;
}

/* first chance: hand the exception to ntdll!KiUserExceptionDispatcher. */
BOOLEAN KiDispatchUserException(PEXCEPTION_RECORD record, PKTRAP_FRAME frame)
{
    extern BOOLEAN KiTraceUserExceptions;
    if (KiTraceUserExceptions)
        KiLogPrintf("dispatch %08x at %p to thread %u of %s", record->ExceptionCode, record->ExceptionAddress,
                    (ULONG)(ULONG_PTR)PsGetCurrentThreadId(), PsCurrentProcess()->ImageFileName);
    PPSP_SYSTEM_DLL dll = current_system_dll();
    if (!dll || !dll->KiUserExceptionDispatcher)
        return FALSE;
    return KiSetupUserException(frame, dll->KiUserExceptionDispatcher, record);
}

NTSTATUS NTAPI NtRaiseException(PEXCEPTION_RECORD record, PCONTEXT context, BOOLEAN first_chance)
{
    EXCEPTION_RECORD rec;
    CONTEXT ctx;
    if (!NT_SUCCESS(KiCopyFromUser(&rec, record, sizeof(rec))) ||
        !NT_SUCCESS(KiCopyFromUser(&ctx, context, sizeof(ctx))))
        return STATUS_ACCESS_VIOLATION;
    PKTHREAD thread = KiCurrentThread();
    PKTRAP_FRAME frame = thread->TrapFrame;
    KiContextToTrapFrame(&ctx, frame, CONTEXT_FULL, UserMode);
    if (first_chance && KiDispatchUserException(&rec, frame)) {
        KiExitToUserMode(frame);
        KiEnterUserMode(frame);
    }
    PEPROCESS process = PsCurrentProcess();
    KiLogPrintf("unhandled exception %08x at %p in process %u (%s)", rec.ExceptionCode, rec.ExceptionAddress,
                (ULONG)(ULONG_PTR)process->UniqueProcessId, process->ImageFileName);
    if (rec.ExceptionCode == STATUS_ACCESS_VIOLATION && rec.NumberParameters >= 2)
        KiLogPrintf("  %s of address %p", rec.ExceptionInformation[0] == 1 ? "write" : rec.ExceptionInformation[0] == 8 ? "execute" : "read",
                    (PVOID)rec.ExceptionInformation[1]);
    KiDumpTrapFrame("raised from", frame);
    PspTerminateProcess(process, rec.ExceptionCode);
    KiExitToUserMode(frame);
    return STATUS_SUCCESS;
}

#define STATUS_NO_CALLBACK_ACTIVE ((NTSTATUS)0xC0000258L)
/* what the kernel code between two callbacks may still need, win32k nests deeply while it sends a message. */
#define CALLBACK_STACK_RESERVE (12 * 1024)
#define CALLBACK_STACK_STEP (64 * 1024)

NTSTATUS NTAPI KeUserModeCallback(ULONG api, PVOID input, ULONG input_length, PVOID *output, PULONG output_length)
{
    PKTHREAD thread = KiCurrentThread();
    PKTRAP_FRAME trap = thread->TrapFrame;
    PPSP_SYSTEM_DLL dll = current_system_dll();
    if (!trap || !dll || !dll->KiUserCallbackDispatcher || ((PEPROCESS)thread->Process)->SystemProcess)
        return STATUS_NOT_SUPPORTED;
    /*
     * every nested callback keeps its kernel frames. windows has no limit of depth, only the stacks end the nesting:
     * the stack gets more mapped room below it, as much as was reserved for it.
     */
    while ((ULONG_PTR)__builtin_frame_address(0) - (ULONG_PTR)thread->StackLimit < CALLBACK_STACK_RESERVE)
        if (!thread->StackAllocation || !MmGrowKernelStack(thread, CALLBACK_STACK_STEP)) {
            static ULONG told;
            if (told++ < 16)
                KiLogPrintf("callback %u of thread %u is refused, the kernel stack ended %u callbacks deep", api,
                            (ULONG)(ULONG_PTR)PsCurrentThread()->Cid.UniqueThread, thread->CallbackDepth);
            return STATUS_STACK_OVERFLOW;
        }

    KTRAP_FRAME frame;
    if (!KiSetupUserCallback(&frame, trap, dll->KiUserCallbackDispatcher, api, input, input_length))
        return STATUS_ACCESS_VIOLATION;

    PKTRAP_FRAME saved_trap = thread->TrapFrame;
    KPROCESSOR_MODE saved_mode = thread->PreviousMode;
    thread->CallbackDepth++;
    /* time in user mode does not count as the service holding the lock in the service statistics. */
    ULONG64 left = HalReadCounter(), blocked = thread->BlockedTicks;
    NTSTATUS status = KiCallUserMode(&thread->CallbackSp, &frame);
    /* waits during the callback were already counted. */
    thread->BlockedTicks += HalReadCounter() - left - (thread->BlockedTicks - blocked);
    thread->CallbackDepth--;
    thread->TrapFrame = saved_trap;
    thread->PreviousMode = saved_mode;
    if (output)
        *output = thread->CallbackResult;
    if (output_length)
        *output_length = thread->CallbackResultLength;
    return status;
}

NTSTATUS NTAPI NtCallbackReturn(PVOID result, ULONG result_length, NTSTATUS status)
{
    PKTHREAD thread = KiCurrentThread();
    if (!thread->CallbackSp || !thread->CallbackDepth)
        return STATUS_NO_CALLBACK_ACTIVE;
    thread->CallbackResult = result;
    thread->CallbackResultLength = result_length;
    ULONG64 sp = thread->CallbackSp;
    thread->CallbackSp = *(ULONG64 *)(sp + KCALLOUT_PREVIOUS_SP);
    KiCallbackReturnInternal(sp, status);
}
