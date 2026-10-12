/*
 * interrupts, exceptions and the return path to user mode.
 */
#include "../ntos.h"

void KiInterruptHandler(PKTRAP_FRAME frame)
{
    PKPCR pcr = KeGetPcr();
    PKPRCB prcb = pcr->CurrentPrcb;
    KIRQL old = pcr->Irql;
    pcr->Irql = CLOCK_LEVEL;
    prcb->InterruptCount++;

    for (;;) {
        ULONG intid = HalAcknowledgeInterrupt();
        if (intid >= 1020)
            break;
        if (intid == HalBootInfo->timer_intid) {
            ULONG64 ctl = KI_READ_SYSREG(cntv_ctl_el0);
            if ((ctl & 5) == 5 && !(ctl & 2))
                KiClockInterrupt(frame);
        } else if (intid == HalBootInfo->irq_intid) {
            ULONG pending = __atomic_exchange_n(&HalSharedPage->irq_pending, 0, __ATOMIC_ACQ_REL);
            if (pending)
                MimeSessionInterrupt(pending);
        }
        HalEndOfInterrupt(intid);
    }

    pcr->Irql = old;
    if (old < DISPATCH_LEVEL) {
        if (prcb->DpcPending) {
            pcr->Irql = DISPATCH_LEVEL;
            KiDeliverDpcs();
            pcr->Irql = old;
        }
        KiCheckPreemption();
        if ((frame->Spsr & 0xF) == PSR_MODE_EL0)
            KiExitToUserMode(frame);
    }
}

void KiSErrorHandler(PKTRAP_FRAME frame, ULONG64 from_user)
{
    KeBugCheckEx(0x00000124, frame->Esr, frame->Pc, frame->Far, from_user); /* WHEA_UNCORRECTABLE_ERROR */
}

void KiInvalidVectorHandler(PKTRAP_FRAME frame)
{
    KeBugCheckEx(0x0000007F, frame->Esr, frame->Pc, frame->Far, 0); /* UNEXPECTED_KERNEL_MODE_TRAP */
}

static void dump_frame(const char *what, PKTRAP_FRAME frame)
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

static NTSTATUS exception_code(PKTRAP_FRAME frame, ULONG ec)
{
    switch (ec) {
    case 0x20:
    case 0x21:
    case 0x24:
    case 0x25:
        return STATUS_ACCESS_VIOLATION;
    case 0x22:
    case 0x26:
        return STATUS_DATATYPE_MISALIGNMENT;
    case 0x3C: {
        ULONG imm = (ULONG)(frame->Esr & 0xFFFF);
        if (imm == 0xF003)
            return STATUS_STACK_BUFFER_OVERRUN; /* __fastfail */
        if (imm == 0xF004)
            return STATUS_INTEGER_DIVIDE_BY_ZERO;
        return STATUS_BREAKPOINT;
    }
    case 0x2C:
        return (NTSTATUS)0xC000008E; /* STATUS_FLOAT_DIVIDE_BY_ZERO */
    case 0x0E:
        return STATUS_ILLEGAL_INSTRUCTION;
    case 0x00:
    default:
        return STATUS_ILLEGAL_INSTRUCTION;
    }
}

BOOLEAN KiTraceUserExceptions;

void KiUserExceptionHandler(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    thread->TrapFrame = frame;
    thread->PreviousMode = UserMode;
    ULONG ec = (ULONG)(frame->Esr >> 26) & 0x3F;
    KiEnableInterrupts();

    if (ec == 0x20 || ec == 0x24) {
        ULONG dfsc = (ULONG)frame->Esr & 0x3F;
        BOOLEAN write = ec == 0x24 && (frame->Esr & (1u << 6)) && !(frame->Esr & (1u << 8));
        BOOLEAN exec = ec == 0x20;
        BOOLEAN fault_kind_ok = (dfsc & 0x3C) == 0x04 || (dfsc & 0x3C) == 0x08 || (dfsc & 0x3C) == 0x0C;
        if (fault_kind_ok && MmAccessFault(frame->Far, write, exec, UserMode)) {
            KiExitToUserMode(frame);
            return;
        }
    }

    EXCEPTION_RECORD record;
    memset(&record, 0, sizeof(record));
    record.ExceptionCode = exception_code(frame, ec);
    record.ExceptionAddress = (PVOID)frame->Pc;
    if (record.ExceptionCode == STATUS_ACCESS_VIOLATION) {
        record.NumberParameters = 2;
        record.ExceptionInformation[0] = ec == 0x20 ? 8 : ((frame->Esr & (1u << 6)) ? 1 : 0);
        record.ExceptionInformation[1] = frame->Far;
    }
    if (record.ExceptionCode == STATUS_BREAKPOINT) {
        /* brk does not advance the pc; windows reports the brk address and resumes after it. */
        frame->Pc += 4;
        record.ExceptionAddress = (PVOID)(frame->Pc - 4);
    }
    if (KiTraceUserExceptions) {
        KiLogPrintf("first chance exception %08x at %p in %s, pte of far %016llx", record.ExceptionCode,
                    (PVOID)frame->Pc, PsCurrentProcess()->ImageFileName,
                    MiReadPte(PsCurrentProcess()->AddressSpace.Root, frame->Far & ~0xFFFull));
        dump_frame("user context", frame);
    }
    if (!KiDispatchUserException(&record, frame)) {
        PEPROCESS process = PsCurrentProcess();
        KiLogPrintf("unhandled exception %08x in process %u (%s)", record.ExceptionCode,
                    (ULONG)(ULONG_PTR)process->UniqueProcessId, process->ImageFileName);
        dump_frame("user context", frame);
        PspTerminateProcess(process, record.ExceptionCode);
    }
    KiExitToUserMode(frame);
}

void KiKernelExceptionHandler(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    ULONG ec = (ULONG)(frame->Esr >> 26) & 0x3F;

    if (ec == 0x25 || ec == 0x21) {
        BOOLEAN write = ec == 0x25 && (frame->Esr & (1u << 6)) && !(frame->Esr & (1u << 8));
        ULONG_PTR far = frame->Far;
        if (far <= MIME_USER_ADDRESS_LIMIT && !(frame->Spsr & (1u << 7))) {
            /* demand faults on user memory touched by the kernel, only from interruptible code. */
            KiEnableInterrupts();
            if (MmAccessFault(far, write, FALSE, KernelMode))
                return;
        }
        if (far <= MIME_USER_ADDRESS_LIMIT && thread && thread->FaultRecovery) {
            KJUMP_BUFFER *recovery = thread->FaultRecovery;
            thread->FaultRecovery = NULL;
            KeGetPcr()->Irql = PASSIVE_LEVEL;
            KiEnableInterrupts();
            KiLongJump(recovery, 1);
        }
    }
    dump_frame("kernel exception", frame);
    KeBugCheckEx(ec == 0x25 ? 0x00000050 : 0x0000001E, frame->Far, frame->Esr, frame->Pc, 0);
}

/*
 * final processing before returning to user mode: kernel apcs, thread
 * termination, user apcs and preemption. returns with interrupts masked.
 */
void KiExitToUserMode(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    for (;;) {
        KiDisableInterrupts();
        if (thread->KernelApcPending && !thread->KernelApcDisable) {
            KiEnableInterrupts();
            KiDeliverApcs(KernelMode, frame);
            continue;
        }
        if (thread->Terminating) {
            KiEnableInterrupts();
            PspExitThread(((PETHREAD)thread)->ExitStatus);
        }
        if (thread->UserApcPending) {
            KiEnableInterrupts();
            KiDeliverApcs(UserMode, frame);
            continue;
        }
        if (KeGetCurrentPrcb()->DispatchPending || KeGetCurrentPrcb()->DpcPending) {
            KiDispatchPendingWork();
            continue;
        }
        break;
    }
    thread->PreviousMode = UserMode;
}
