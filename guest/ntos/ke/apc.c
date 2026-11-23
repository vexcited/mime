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

#ifdef __x86_64__
/*
 * the x64 contexts follow windows 11 26300 field by field, measured across threads with scratch/dlls/probe/xctx.c
 * of the research repository: see KiContextRequest, KiCaptureThreadContext and KiApplyThreadContext.
 */
NTSTATUS NTAPI NtContinue(PCONTEXT context, BOOLEAN test_alert)
{
    PKTHREAD thread = KiCurrentThread();
    PKTRAP_FRAME frame = thread->TrapFrame;
    CONTEXT ctx;
    if ((ULONG_PTR)context & 15)
        return STATUS_DATATYPE_MISALIGNMENT;
    if (!NT_SUCCESS(KiCopyFromUser(&ctx, context, sizeof(ctx))))
        return STATUS_ACCESS_VIOLATION;
    KI_CONTEXT_REQUEST request;
    KiContextRequest(ctx.ContextFlags, &request);
    /*
     * the frame of the service is the one the thread resumes with: without the integers it keeps the status in rax,
     * the first argument in rcx, and in r8 to r11 and xmm0 to xmm5 what the frame last held, which a service does
     * not save.
     */
    if ((request.Parts & CONTEXT_INTEGER) != CONTEXT_INTEGER) {
        frame->Rax = STATUS_SUCCESS;
        frame->Rcx = frame->R10;
        frame->R8 = thread->ServiceStale[0];
        frame->R9 = thread->ServiceStale[1];
        frame->R10 = thread->ServiceStale[2];
        frame->R11 = thread->ServiceStale[3];
    }
    if ((request.Parts & CONTEXT_FLOATING_POINT) != CONTEXT_FLOATING_POINT)
        memcpy(frame->Xmm, thread->ServiceStaleXmm, sizeof(thread->ServiceStaleXmm));
    KiApplyThreadContext(thread, frame, FALSE, &ctx, request.Parts);
    /* a context that carries extended state resumes with it: a handler may have changed the upper halves. */
    if (request.Xstate) {
        KI_USER_XSTATE where;
        UCHAR state[KI_XSTATE_MAXIMUM];
        if (NT_SUCCESS(KiProbeUserXstate(context, &where)) && NT_SUCCESS(KiLoadUserXstate(&where, state)))
            KiRestoreXstate(thread, state);
    }
    if (test_alert && !IsListEmpty(&thread->ApcListHead[UserMode]))
        thread->UserApcPending = TRUE;
    /* a continue resumes the context as it is, without the instrumentation callback. */
    thread->InstrumentReturn = FALSE;
    KiExitToUserMode(frame);
    KiEnterUserMode(frame);
}

/*
 * like PspGetSetContextSpecialApc: a thread that runs is asked for its context by a kernel apc, which it takes the
 * next time it enters the kernel, at the latest on the processor's next interrupt. the routine reads or writes the
 * user frame the thread entered with, then wakes the caller.
 */
typedef struct CONTEXT_REQUEST {
    KAPC Apc;
    KEVENT Done;
    CONTEXT *Context;
    ULONG Parts;
    BOOLEAN Set;
    BOOLEAN InService;
    NTSTATUS Status;
    PUCHAR Xstate; /* the extended state to read or to set with the context, or none. */
} CONTEXT_REQUEST;

/* a thread in a system service: one that never ran has the frame of its start, which is none. */
static BOOLEAN frame_in_service(PETHREAD thread, PKTRAP_FRAME frame)
{
    return !thread->FirstEntryContext && KI_TRAP_IN_SERVICE(frame);
}

static void apply_context(PETHREAD thread, PKTRAP_FRAME frame, const CONTEXT *ctx, ULONG parts)
{
    /* the caller's own frame is that of this service: it returns with its status and what a service leaves. */
    BOOLEAN in_service = frame_in_service(thread, frame);
    KiApplyThreadContext(&thread->Tcb, frame, in_service, ctx, parts);
    if (in_service)
        thread->Tcb.ContextSetInService |= KI_CONTEXT_SET_ANY;
}

static void NTAPI context_apc(PKAPC apc, PKNORMAL_ROUTINE *normal, PVOID *context, PVOID *arg1, PVOID *arg2)
{
    CONTEXT_REQUEST *r = CONTAINING_RECORD(apc, CONTEXT_REQUEST, Apc);
    PKTHREAD thread = KiCurrentThread();
    PKTRAP_FRAME frame = thread->TrapFrame;
    if (!frame || !KI_TRAP_FROM_USER(frame)) {
        r->Status = STATUS_UNSUCCESSFUL;
    } else {
        r->Status = STATUS_SUCCESS;
        r->InService = frame_in_service((PETHREAD)thread, frame);
        if (r->Set) {
            /* the thread itself is in the service, so its frame is marked like one another thread wrote. */
            KiApplyThreadContext(thread, frame, r->InService, r->Context, r->Parts);
            if (r->InService)
                thread->ContextSetInService |= KI_CONTEXT_SET_ANY;
            if (r->Xstate)
                KiRestoreXstate(thread, r->Xstate);
        } else {
            KiCaptureThreadContext(thread, frame, r->InService, r->Context);
            if (r->Xstate)
                KiCaptureXstate(thread, ~0ull, r->Xstate);
        }
    }
    KeSetEvent(&r->Done, 0, FALSE);
}

static NTSTATUS context_by_apc(PETHREAD thread, CONTEXT *context, ULONG parts, BOOLEAN set, PUCHAR xstate,
                               BOOLEAN *in_service)
{
    CONTEXT_REQUEST r;
    KeInitializeEvent(&r.Done, NotificationEvent, FALSE);
    r.Context = context;
    r.Parts = parts;
    r.Set = set;
    r.Xstate = xstate;
    r.InService = FALSE;
    r.Status = STATUS_UNSUCCESSFUL;
    KeInitializeApc(&r.Apc, &thread->Tcb, OriginalApcEnvironment, context_apc, nullptr, nullptr, KernelMode, nullptr);
    if (!KeInsertQueueApc(&r.Apc, nullptr, nullptr, 0))
        return STATUS_UNSUCCESSFUL;
    PVOID objects[2] = {&r.Done, thread};
    NTSTATUS status = KeWaitForMultipleObjects(2, objects, WaitAny, Executive, KernelMode, FALSE, nullptr, nullptr);
    if (in_service)
        *in_service = r.InService;
    return status == STATUS_WAIT_0 ? r.Status : STATUS_UNSUCCESSFUL;
}

/* the frame a thread that does not run left its user state in, or none when it runs and must be asked. */
static PKTRAP_FRAME stopped_frame(PETHREAD thread, NTSTATUS *status)
{
    PKTRAP_FRAME frame = thread->Tcb.TrapFrame;
    if (!frame)
        *status = STATUS_UNSUCCESSFUL;
    return frame && (thread == PsCurrentThread() || thread->Tcb.State != Running) ? frame : nullptr;
}

/* a thread that ended has no context: windows answers both ways with the same status. */
static NTSTATUS wait_for_context(PETHREAD thread)
{
    NTSTATUS status = PspWaitForSuspension(thread);
    return status == STATUS_THREAD_IS_TERMINATING ? STATUS_UNSUCCESSFUL : status;
}

static NTSTATUS get_context(PETHREAD thread, PCONTEXT context)
{
    if ((ULONG_PTR)context & 15)
        return STATUS_DATATYPE_MISALIGNMENT;
    /* like PspGetContext, only the parts the flags ask for are written, the rest of the caller's record stays. */
    CONTEXT out;
    NTSTATUS status = KiCopyFromUser(&out, context, sizeof(out));
    if (!NT_SUCCESS(status))
        return status;
    KI_CONTEXT_REQUEST request;
    status = KiContextRequest(out.ContextFlags, &request);
    if (!NT_SUCCESS(status))
        return status;
    KI_USER_XSTATE where;
    UCHAR area[KI_XSTATE_MAXIMUM];
    PUCHAR xstate = nullptr;
    if (request.Xstate) {
        status = KiProbeUserXstate(context, &where);
        if (!NT_SUCCESS(status))
            return status;
        xstate = area;
    }
    status = wait_for_context(thread);
    if (!NT_SUCCESS(status))
        return status;
    CONTEXT ctx;
    BOOLEAN in_service = FALSE, never_ran = FALSE;
    ULONG64 state = KiAcquireDispatcherLock();
    PKTRAP_FRAME frame = stopped_frame(thread, &status);
    if (frame) {
        never_ran = thread->FirstEntryContext != 0;
        in_service = frame_in_service(thread, frame);
        KiCaptureThreadContext(&thread->Tcb, frame, in_service, &ctx);
        if (xstate && thread != PsCurrentThread())
            KiCaptureXstate(&thread->Tcb, ~0ull, xstate);
    }
    KiReleaseDispatcherLock(state);
    if (frame && xstate && thread == PsCurrentThread())
        KiCaptureXstate(&thread->Tcb, ~0ull, xstate);
    if (NT_SUCCESS(status) && !frame)
        status = context_by_apc(thread, &ctx, request.Parts, FALSE, xstate, &in_service);
    if (!NT_SUCCESS(status))
        return status;
    KiMergeContext(&out, &ctx, request.Parts);
    out.ContextFlags = request.Out;
    if (request.Report)
        out.ContextFlags |= CONTEXT_EXCEPTION_REPORTING |
                            (never_ran ? CONTEXT_EXCEPTION_ACTIVE : in_service ? CONTEXT_SERVICE_ACTIVE : 0);
    status = KiCopyToUser(context, &out, sizeof(out));
    if (NT_SUCCESS(status) && xstate)
        status = KiStoreUserXstate(&where, xstate);
    return status;
}

/* the caller's own user context, or that of another thread, stopped or asked by an apc. */
NTSTATUS NTAPI NtGetContextThread(HANDLE handle, PCONTEXT context)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_GET_CONTEXT, &thread);
    if (!NT_SUCCESS(status))
        return status;
    status = get_context(thread, context);
    ObDereferenceObject(thread);
    return status;
}

static NTSTATUS set_context(PETHREAD thread, PCONTEXT context)
{
    if ((ULONG_PTR)context & 15)
        return STATUS_DATATYPE_MISALIGNMENT;
    CONTEXT ctx;
    NTSTATUS status = KiCopyFromUser(&ctx, context, sizeof(ctx));
    if (!NT_SUCCESS(status))
        return status;
    KI_CONTEXT_REQUEST request;
    KiContextRequest(ctx.ContextFlags, &request);
    UCHAR area[KI_XSTATE_MAXIMUM];
    PUCHAR xstate = nullptr;
    if (request.Xstate) {
        KI_USER_XSTATE where;
        status = KiProbeUserXstate(context, &where);
        if (!NT_SUCCESS(status))
            return status;
        /* the shadow stack state is not a thread's to set. */
        if (KiUserXstateDenied(&where))
            return STATUS_SET_CONTEXT_DENIED;
        status = KiLoadUserXstate(&where, area);
        if (!NT_SUCCESS(status))
            return status;
        xstate = area;
    }
    status = wait_for_context(thread);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 state = KiAcquireDispatcherLock();
    PKTRAP_FRAME frame = stopped_frame(thread, &status);
    if (frame) {
        apply_context(thread, frame, &ctx, request.Parts);
        if (xstate && thread != PsCurrentThread())
            KiRestoreXstate(&thread->Tcb, xstate);
    }
    KiReleaseDispatcherLock(state);
    if (frame && xstate && thread == PsCurrentThread())
        KiRestoreXstate(&thread->Tcb, xstate);
    if (NT_SUCCESS(status) && !frame)
        status = context_by_apc(thread, &ctx, request.Parts, TRUE, xstate, nullptr);
    return status;
}

NTSTATUS NTAPI NtSetContextThread(HANDLE handle, PCONTEXT context)
{
    PETHREAD thread;
    NTSTATUS status = PsReferenceThreadHandle(handle, THREAD_SET_CONTEXT, &thread);
    if (!NT_SUCCESS(status))
        return status;
    /*
     * the caller's own context is the frame of this service, measured on windows 11 26300 with
     * scratch/dlls/probe/icb.c of the research repository: it returns there with its status in rax and what a
     * service leaves in the volatile registers, through the instrumentation callback, and a trap flag steps at once.
     */
    status = set_context(thread, context);
    ObDereferenceObject(thread);
    return status;
}

/*
 * the whole user context of a thread into a kernel record, as NtGetContextThread reads it, for the wow64 context
 * classes: what PspGetContextThreadInternal gives PspWow64GetContextThread. report gets the context flags that tell
 * whether the thread is in a service or never ran.
 */
NTSTATUS PsGetUserContextThread(PETHREAD thread, CONTEXT *ctx, PULONG report)
{
    NTSTATUS status = wait_for_context(thread);
    if (!NT_SUCCESS(status))
        return status;
    BOOLEAN in_service = FALSE, never_ran = FALSE;
    ULONG64 state = KiAcquireDispatcherLock();
    PKTRAP_FRAME frame = stopped_frame(thread, &status);
    if (frame) {
        never_ran = thread->FirstEntryContext != 0;
        in_service = frame_in_service(thread, frame);
        KiCaptureThreadContext(&thread->Tcb, frame, in_service, ctx);
    }
    KiReleaseDispatcherLock(state);
    if (NT_SUCCESS(status) && !frame)
        status = context_by_apc(thread, ctx, CONTEXT_ALL, FALSE, nullptr, &in_service);
    ctx->ContextFlags = CONTEXT_ALL;
    *report = CONTEXT_EXCEPTION_REPORTING | (never_ran ? CONTEXT_EXCEPTION_ACTIVE : in_service ? CONTEXT_SERVICE_ACTIVE : 0);
    return status;
}

/* the parts of a context a thread takes from a kernel record, as NtSetContextThread gives them. */
NTSTATUS PsSetUserContextThread(PETHREAD thread, const CONTEXT *ctx, ULONG parts)
{
    NTSTATUS status = wait_for_context(thread);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 state = KiAcquireDispatcherLock();
    PKTRAP_FRAME frame = stopped_frame(thread, &status);
    if (frame)
        apply_context(thread, frame, ctx, parts);
    KiReleaseDispatcherLock(state);
    if (NT_SUCCESS(status) && !frame)
        status = context_by_apc(thread, (CONTEXT *)ctx, parts, TRUE, nullptr, nullptr);
    return status;
}
#else
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
    PUCHAR Xstate; /* x64: the extended state to read or to set with the context, or none. */
    ULONG64 Asked;
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
#ifdef __x86_64__
        if (r->Xstate && r->Set)
            KiRestoreXstate(KiCurrentThread(), r->Xstate);
        else if (r->Xstate)
            KiCaptureXstate(KiCurrentThread(), r->Asked, r->Xstate);
#endif
    }
    KeSetEvent(&r->Done, 0, FALSE);
}

static NTSTATUS context_by_apc(PETHREAD thread, CONTEXT *context, ULONG flags, BOOLEAN set, PUCHAR xstate, ULONG64 asked)
{
    CONTEXT_REQUEST r;
    KeInitializeEvent(&r.Done, NotificationEvent, FALSE);
    r.Context = context;
    r.Flags = flags;
    r.Set = set;
    r.Xstate = xstate;
    r.Asked = asked;
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
    /* x64: the extended state goes with a context that asks for it, the features its area names. */
    PUCHAR xstate = nullptr;
    ULONG64 asked = 0;
#ifdef __x86_64__
    UCHAR area[KI_XSTATE_MAXIMUM];
    if (NT_SUCCESS(status) && (flags & CONTEXT_AMD64_XSTATE) == CONTEXT_AMD64_XSTATE && KiReadUserXstate(context, area)) {
        xstate = area;
        asked = ((XSAVE_AREA_HEADER *)area)->Mask;
    }
#endif
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
#ifdef __x86_64__
            if (xstate && thread != PsCurrentThread())
                KiCaptureXstate(&thread->Tcb, asked, xstate);
#endif
        }
        KiReleaseDispatcherLock(state);
#ifdef __x86_64__
        if (frame && xstate && thread == PsCurrentThread())
            KiCaptureXstate(&thread->Tcb, asked, xstate);
#endif
        if (NT_SUCCESS(status) && !frame)
            status = context_by_apc(thread, &ctx, flags, FALSE, xstate, asked);
    }
    if (NT_SUCCESS(status)) {
        KiMergeContext(&out, &ctx, flags);
        status = KiCopyToUser(context, &out, sizeof(out));
#ifdef __x86_64__
        if (NT_SUCCESS(status) && xstate)
            KiWriteUserXstate(context, xstate);
#endif
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
    PUCHAR xstate = nullptr;
#ifdef __x86_64__
    UCHAR area[KI_XSTATE_MAXIMUM];
    if (NT_SUCCESS(status) && (ctx.ContextFlags & CONTEXT_AMD64_XSTATE) == CONTEXT_AMD64_XSTATE &&
        KiReadUserXstate(context, area))
        xstate = area;
#endif
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
#ifdef __x86_64__
            if (xstate && thread != PsCurrentThread())
                KiRestoreXstate(&thread->Tcb, xstate);
#endif
        }
        KiReleaseDispatcherLock(state);
#ifdef __x86_64__
        if (frame && xstate && thread == PsCurrentThread())
            KiRestoreXstate(&thread->Tcb, xstate);
#endif
        if (NT_SUCCESS(status) && !frame)
            status = context_by_apc(thread, &ctx, ctx.ContextFlags, TRUE, xstate, 0);
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

#endif

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
    /* like windows, the apcs run as the service returns and its status tells nothing of them. */
    return STATUS_SUCCESS;
}

/* every exception dispatched since boot, which SystemExceptionInformation tells. */
volatile LONG KeExceptionDispatchCount;

/* first chance: hand the exception to ntdll!KiUserExceptionDispatcher. */
BOOLEAN KiDispatchUserException(PEXCEPTION_RECORD record, PKTRAP_FRAME frame)
{
    __atomic_add_fetch(&KeExceptionDispatchCount, 1, __ATOMIC_RELAXED);
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
#ifdef __x86_64__
    KiNativeCodeSegment(thread, frame, CONTEXT_FULL);
#endif
    if (!first_chance)
        __atomic_add_fetch(&KeExceptionDispatchCount, 1, __ATOMIC_RELAXED);
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
