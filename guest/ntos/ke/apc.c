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
    if (test_alert && !IsListEmpty(&thread->ApcListHead[UserMode]))
        thread->UserApcPending = TRUE;
    KiExitToUserMode(frame);
    KiEnterUserMode(frame);
}

/* the caller's own user context, or that of a thread stopped in the kernel. */
NTSTATUS NTAPI NtGetContextThread(HANDLE handle, PCONTEXT context)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_GET_CONTEXT, &thread);
    if (!NT_SUCCESS(status))
        return status;
    ULONG flags;
    status = KiCopyFromUser(&flags, &context->ContextFlags, sizeof(flags));
    if (NT_SUCCESS(status))
        status = PspWaitForSuspension(thread);
    ULONG64 state = KiAcquireDispatcherLock();
    PKTRAP_FRAME frame = thread->Tcb.TrapFrame;
    if (NT_SUCCESS(status) && (!frame || (thread != PsCurrentThread() && thread->Tcb.State == Running)))
        status = STATUS_UNSUCCESSFUL;
    CONTEXT ctx;
    if (NT_SUCCESS(status))
        KiTrapFrameToContext(frame, &ctx, flags & CONTEXT_ALL);
    KiReleaseDispatcherLock(state);
    if (NT_SUCCESS(status)) {
        status = KiCopyToUser(context, &ctx, sizeof(ctx));
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
    ULONG64 state = KiAcquireDispatcherLock();
    PKTRAP_FRAME frame = thread->Tcb.TrapFrame;
    if (NT_SUCCESS(status) && (!frame || (thread != PsCurrentThread() && thread->Tcb.State == Running)))
        status = STATUS_UNSUCCESSFUL;
    if (NT_SUCCESS(status)) {
        KiContextToTrapFrame(&ctx, frame, ctx.ContextFlags, UserMode);
    }
    KiReleaseDispatcherLock(state);
    if (NT_SUCCESS(status)) {
        if (thread == PsCurrentThread()) {
            ObDereferenceObject(thread);
            KiExitToUserMode(frame);
            KiEnterUserMode(frame);
        }
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

NTSTATUS NTAPI KeUserModeCallback(ULONG api, PVOID input, ULONG input_length, PVOID *output, PULONG output_length)
{
    PKTHREAD thread = KiCurrentThread();
    PKTRAP_FRAME trap = thread->TrapFrame;
    PPSP_SYSTEM_DLL dll = current_system_dll();
    if (!trap || !dll || !dll->KiUserCallbackDispatcher || ((PEPROCESS)thread->Process)->SystemProcess)
        return STATUS_NOT_SUPPORTED;
    if (thread->CallbackDepth > 48)
        return STATUS_STACK_OVERFLOW;
    /* every nested callback keeps its kernel frames, so the depth limit alone does not keep the stack from ending. */
    if ((ULONG_PTR)__builtin_frame_address(0) - (ULONG_PTR)thread->StackLimit < CALLBACK_STACK_RESERVE)
        return STATUS_STACK_OVERFLOW;

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
