/*
 * the arm64 half of threads and contexts: switch frames, user mode entry
 * frames, the CONTEXT conversions and the address space switch.
 */
#include "../../ntos.h"

void KiSwapContextInternal(ULONG64 *old_sp, ULONG64 new_sp);

void KiSwitchAddressSpace(PKPROCESS process)
{
    KI_WRITE_SYSREG(ttbr0_el1, process->DirectoryTableBase);
    KiInstructionBarrier();
}

void KiSwapContext(PKTHREAD old, PKTHREAD next)
{
    KiSwapContextInternal(&old->KernelSp, next->KernelSp);
}

/* wfi ends on a pending interrupt even while masked, so the lock is taken back before handling it. */
void KiIdleWait(void)
{
    KiReleaseKernelLock();
    __asm__ volatile("wfi");
    KiAcquireKernelLock();
}

void KiFlushInstructionCache(void)
{
    __asm__ volatile("dsb ish; ic ialluis; dsb ish; isb" ::: "memory");
}

void KiInitializeProcessorArch(void)
{
}

void KiInitializeSwitchFrame(PKTHREAD thread, PKSTART_ROUTINE routine, PVOID context)
{
    PUCHAR stack_top = thread->InitialStack;
    ULONG64 *frame = (ULONG64 *)(stack_top - KTRAP_FRAME_SIZE - KSWITCH_FRAME_SIZE);
    memset(frame, 0, KSWITCH_FRAME_SIZE);
    frame[0] = (ULONG64)routine;          /* x19 */
    frame[1] = (ULONG64)context;          /* x20 */
    frame[11] = (ULONG64)KiThreadStartup; /* x30 */
    thread->KernelSp = (ULONG64)frame;
    thread->TrapFrame = (PKTRAP_FRAME)(stack_top - KTRAP_FRAME_SIZE);
}

void KiInitializeUserFrame(PKTRAP_FRAME frame, ULONG64 pc, ULONG64 sp, ULONG64 arg0, ULONG64 arg1, ULONG64 teb)
{
    memset(frame, 0, sizeof(*frame));
    frame->Pc = pc;
    frame->Sp = sp;
    frame->X[0] = arg0;
    frame->X[1] = arg1;
    frame->X[18] = teb;
    frame->Spsr = PSR_MODE_EL0;
    frame->Flags = KTRAP_FLAG_FP_SAVED; /* start with zeroed fp state */
}

void KiInitializeUserContext(PCONTEXT context, ULONG64 pc, ULONG64 sp, ULONG64 arg0, ULONG64 arg1, ULONG64 teb)
{
    memset(context, 0, sizeof(*context));
    context->ContextFlags = CONTEXT_FULL;
    context->Pc = pc;
    context->Sp = sp;
    context->X0 = arg0;
    context->X1 = arg1;
    context->X18 = teb;
    context->Cpsr = PSR_MODE_EL0;
}

void KiTrapFrameToContext(const KTRAP_FRAME *frame, CONTEXT *ctx, ULONG flags)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->ContextFlags = flags & ~(ULONG)CONTEXT_UNWOUND_TO_CALL;
    for (int i = 0; i < 31; i++)
        ctx->X[i] = frame->X[i];
    ctx->Sp = frame->Sp;
    ctx->Pc = frame->Pc;
    ctx->Cpsr = (ULONG)frame->Spsr;
    if ((flags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT && (frame->Flags & KTRAP_FLAG_FP_SAVED)) {
        memcpy(ctx->V, frame->V, sizeof(ctx->V));
        ctx->Fpcr = frame->Fpcr;
        ctx->Fpsr = frame->Fpsr;
    }
    /* a software exception raised from a call has the return address for its pc, which only the caller knows. */
    if ((flags & CONTEXT_UNWOUND_TO_CALL) && (frame->Flags & KTRAP_FLAG_UNWOUND_TO_CALL))
        ctx->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
}

void KiContextToTrapFrame(const CONTEXT *ctx, PKTRAP_FRAME frame, ULONG flags, KPROCESSOR_MODE mode)
{
    if ((flags & CONTEXT_INTEGER) == CONTEXT_INTEGER) {
        for (int i = 0; i < 29; i++)
            frame->X[i] = ctx->X[i];
    }
    if ((flags & CONTEXT_CONTROL) == CONTEXT_CONTROL) {
        frame->X[29] = ctx->Fp;
        frame->X[30] = ctx->Lr;
        frame->Sp = ctx->Sp;
        frame->Pc = ctx->Pc;
        /* user mode may only change the condition flags. */
        frame->Spsr = (mode == UserMode) ? ((ctx->Cpsr & 0xF0000000u) | PSR_MODE_EL0) : ctx->Cpsr;
        if (ctx->ContextFlags & CONTEXT_UNWOUND_TO_CALL)
            frame->Flags |= KTRAP_FLAG_UNWOUND_TO_CALL;
        else
            frame->Flags &= ~(ULONG64)KTRAP_FLAG_UNWOUND_TO_CALL;
    }
    if ((flags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT) {
        memcpy(frame->V, ctx->V, sizeof(frame->V));
        frame->Fpcr = ctx->Fpcr;
        frame->Fpsr = ctx->Fpsr;
        frame->Flags |= KTRAP_FLAG_FP_SAVED;
    }
}

BOOLEAN KiSetupUserApc(PKTRAP_FRAME frame, PVOID dispatcher, PKNORMAL_ROUTINE routine, PVOID context, PVOID arg1,
                       PVOID arg2)
{
    CONTEXT ctx;
    KiTrapFrameToContext(frame, &ctx, CONTEXT_FULL);
    ULONG64 sp = (frame->Sp - sizeof(CONTEXT)) & ~15ull;
    if (!NT_SUCCESS(KiCopyToUser((PVOID)sp, &ctx, sizeof(ctx))))
        return FALSE;
    frame->Sp = sp;
    frame->Pc = (ULONG64)dispatcher;
    frame->X[0] = (ULONG64)context;
    frame->X[1] = (ULONG64)arg1;
    frame->X[2] = (ULONG64)arg2;
    frame->X[3] = (ULONG64)routine;
    frame->X[4] = sp;
    return TRUE;
}

BOOLEAN KiSetupUserException(PKTRAP_FRAME frame, PVOID dispatcher, PEXCEPTION_RECORD record)
{
    CONTEXT ctx;
    KiTrapFrameToContext(frame, &ctx, CONTEXT_FULL | CONTEXT_UNWOUND_TO_CALL);
    ULONG64 sp = (frame->Sp - sizeof(CONTEXT) - sizeof(EXCEPTION_RECORD) - 64) & ~15ull;
    ULONG64 ctx_addr = sp;
    ULONG64 rec_addr = sp + ((sizeof(CONTEXT) + 15) & ~15ull);
    if (!NT_SUCCESS(KiCopyToUser((PVOID)ctx_addr, &ctx, sizeof(ctx))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)rec_addr, record, sizeof(*record))))
        return FALSE;
    frame->Sp = sp;
    frame->Pc = (ULONG64)dispatcher;
    frame->X[0] = rec_addr;
    frame->X[1] = ctx_addr;
    return TRUE;
}

BOOLEAN KiSetupUserCallback(PKTRAP_FRAME frame, const KTRAP_FRAME *trap, PVOID dispatcher, ULONG api,
                            const void *input, ULONG input_length)
{
    ULONG64 args = (trap->Sp - input_length - 32) & ~15ull;
    if (input_length && !NT_SUCCESS(KiCopyToUser((PVOID)args, input, input_length)))
        return FALSE;
    memset(frame, 0, offsetof(KTRAP_FRAME, V));
    frame->Pc = (ULONG64)dispatcher;
    frame->Sp = args - 16;
    frame->Spsr = PSR_MODE_EL0;
    frame->X[18] = trap->X[18];
    frame->X[0] = api;
    frame->X[1] = args;
    frame->X[2] = input_length;
    return TRUE;
}

void KiDumpTrapFrame(const char *what, const KTRAP_FRAME *frame)
{
    KiLogPrintf("%s: pc %p far %p esr %08llx spsr %llx sp %p lr %p", what, (PVOID)frame->Pc, (PVOID)frame->Far,
                frame->Esr, frame->Spsr, (PVOID)frame->Sp, (PVOID)frame->X[30]);
    for (int i = 0; i < 30; i += 3)
        KiLogPrintf("  x%-2d %016llx  x%-2d %016llx  x%-2d %016llx", i, frame->X[i], i + 1, frame->X[i + 1], i + 2,
                    frame->X[i + 2]);
    /* walk the frame pointer chain. */
    ULONG64 fp = frame->X[29];
    for (int depth = 0; depth < 16 && fp && (fp & 7) == 0; depth++) {
        if (!MmIsAddressValid((PVOID)fp) || !MmIsAddressValid((PVOID)(fp + 8)))
            break;
        ULONG64 *rec = (ULONG64 *)fp;
        KiLogPrintf("  frame %d: %p", depth, (PVOID)rec[1]);
        fp = rec[0];
    }
}
