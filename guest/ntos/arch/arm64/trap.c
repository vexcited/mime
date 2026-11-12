/*
 * arm64 interrupts and exceptions.
 */
#include "../../ntos.h"
#include <mime/hyperv.h>

void KiInterruptHandler(PKTRAP_FRAME frame)
{
    if ((frame->Spsr & 0xF) == PSR_MODE_EL0) {
        KiCurrentThread()->TrapFrame = frame;
        KiCurrentThread()->PreviousMode = UserMode;
    }
    PKPCR pcr = KeGetPcr();
    PKPRCB prcb = pcr->CurrentPrcb;
    KIRQL old = pcr->Irql;
    pcr->Irql = CLOCK_LEVEL;
    prcb->InterruptCount++;
    ULONG64 start = HalReadCounter();

    for (;;) {
        ULONG intid = HalAcknowledgeInterrupt();
        if (intid >= 1020)
            break;
        if (intid == HalBootInfo->timer_intid) {
            ULONG64 ctl = KI_READ_SYSREG(cntv_ctl_el0);
            if ((ctl & 5) == 5 && !(ctl & 2))
                KiClockInterrupt(frame);
        } else if (intid == HAL_IPI_INTID) {
            prcb->IpiReceived++;
        } else if (intid == HalBootInfo->irq_intid) {
            ULONG pending = __atomic_exchange_n(&HalSharedPage->irq_pending, 0, __ATOMIC_ACQ_REL);
            if (pending)
                MimeSessionInterrupt(pending);
        }
        HalEndOfInterrupt(intid);
    }
    KiChargeServiceTime(prcb, start, FALSE);

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
            return STATUS_STACK_BUFFER_OVERRUN; /* __fastfail. */
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

extern BOOLEAN KiTraceUserExceptions;

/* carries an EL0 hypercall to EL2 for the program, its x0 to x17 registers in and out. */
static void user_hypercall(PKTRAP_FRAME frame)
{
    register ULONG64 x0 __asm__("x0") = frame->X[0];
    register ULONG64 x1 __asm__("x1") = frame->X[1];
    register ULONG64 x2 __asm__("x2") = frame->X[2];
    register ULONG64 x3 __asm__("x3") = frame->X[3];
    register ULONG64 x4 __asm__("x4") = frame->X[4];
    register ULONG64 x5 __asm__("x5") = frame->X[5];
    register ULONG64 x6 __asm__("x6") = frame->X[6];
    register ULONG64 x7 __asm__("x7") = frame->X[7];
    register ULONG64 x8 __asm__("x8") = frame->X[8];
    register ULONG64 x9 __asm__("x9") = frame->X[9];
    register ULONG64 x10 __asm__("x10") = frame->X[10];
    register ULONG64 x11 __asm__("x11") = frame->X[11];
    register ULONG64 x12 __asm__("x12") = frame->X[12];
    register ULONG64 x13 __asm__("x13") = frame->X[13];
    register ULONG64 x14 __asm__("x14") = frame->X[14];
    register ULONG64 x15 __asm__("x15") = frame->X[15];
    register ULONG64 x16 __asm__("x16") = frame->X[16];
    register ULONG64 x17 __asm__("x17") = frame->X[17];
    __asm__ volatile("hvc #0"
                     : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7),
                       "+r"(x8), "+r"(x9), "+r"(x10), "+r"(x11), "+r"(x12), "+r"(x13), "+r"(x14), "+r"(x15),
                       "+r"(x16), "+r"(x17)
                     :
                     : "memory");
    frame->X[0] = x0;
    frame->X[1] = x1;
    frame->X[2] = x2;
    frame->X[3] = x3;
    frame->X[4] = x4;
    frame->X[5] = x5;
    frame->X[6] = x6;
    frame->X[7] = x7;
    frame->X[8] = x8;
    frame->X[9] = x9;
    frame->X[10] = x10;
    frame->X[11] = x11;
    frame->X[12] = x12;
    frame->X[13] = x13;
    frame->X[14] = x14;
    frame->X[15] = x15;
    frame->X[16] = x16;
    frame->X[17] = x17;
}

/* hvc #0 to the hypervisor from a program. the framework reports it as an unknown exception, which the instruction tells apart. */
static BOOLEAN user_hvc(PKTRAP_FRAME frame, ULONG ec)
{
    if (ec != 0x16 && ec != 0x00)
        return FALSE;
    ULONG instruction;
    if (!NT_SUCCESS(KiCopyFromUser(&instruction, (const void *)frame->Pc, sizeof(instruction))))
        return FALSE;
    return instruction == 0xD4000002 && frame->X[0] == MIME_HV_SMCCC_HYPERCALL;
}

void KiUserExceptionHandler(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    thread->TrapFrame = frame;
    thread->PreviousMode = UserMode;
    ULONG ec = (ULONG)(frame->Esr >> 26) & 0x3F;
    KiEnableInterrupts();

    if (user_hvc(frame, ec)) {
        user_hypercall(frame);
        frame->Pc += 4;
        KiExitToUserMode(frame);
        return;
    }

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
        KiApplyFaultStatus(&record);
    }
    if (record.ExceptionCode == STATUS_BREAKPOINT) {
        /* brk does not advance the pc, windows reports the brk address and resumes after it. */
        frame->Pc += 4;
        record.ExceptionAddress = (PVOID)(frame->Pc - 4);
    }
    if (KiTraceUserExceptions) {
        KiLogPrintf("first chance exception %08x at %p in %s, pte of far %016llx", record.ExceptionCode,
                    (PVOID)frame->Pc, PsCurrentProcess()->ImageFileName,
                    MiReadPte(PsCurrentProcess()->AddressSpace.Root, frame->Far & ~0xFFFull));
        KiDumpTrapFrame("user context", frame);
        KiTraceUserStack(KI_TRAP_PC(frame), KI_TRAP_SP(frame));
    }
    if (!KiDispatchUserException(&record, frame)) {
        PEPROCESS process = PsCurrentProcess();
        KiLogPrintf("unhandled exception %08x in process %u (%s)", record.ExceptionCode,
                    (ULONG)(ULONG_PTR)process->UniqueProcessId, process->ImageFileName);
        KiDumpTrapFrame("user context", frame);
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
        if (__builtin_expect(MmVerifyPool, 0))
            MiSpecialPoolFault(far, write ? 2 : 0, frame->Pc);
        if (far <= MIME_USER_ADDRESS_LIMIT && !(frame->Spsr & (1u << 7))) {
            /* demand faults on user memory touched by the kernel, from interruptible code only. */
            KiEnableInterrupts();
            if (MmAccessFault(far, write, FALSE, KernelMode))
                return;
        }
        if (far <= MIME_USER_ADDRESS_LIMIT && thread && thread->FaultRecovery) {
            KJUMP_BUFFER *recovery = thread->FaultRecovery;
            thread->FaultRecovery = nullptr;
            KeGetPcr()->Irql = PASSIVE_LEVEL;
            KiEnableInterrupts();
            KiLongJump(recovery, 1);
        }
    }
    KiDumpTrapFrame("kernel exception", frame);
    KeBugCheckEx(ec == 0x25 ? 0x00000050 : 0x0000001E, frame->Far, frame->Esr, frame->Pc, 0);
}


/* what drivers compare user pointers against, as windows exports it. */
ULONG_PTR MmUserProbeAddress = 0x7FFFFFFF0000;

/* the arm64 kernel has no seh, so a raise lands in the thread's fault recovery, as a fault on user memory does. */
DECLSPEC_NORETURN void NTAPI ExRaiseStatus(NTSTATUS status)
{
    PKTHREAD thread = KiCurrentThread();
    if (thread && thread->FaultRecovery) {
        KJUMP_BUFFER *recovery = thread->FaultRecovery;
        thread->FaultRecovery = nullptr;
        KiLongJump(recovery, 1);
    }
    KeBugCheckEx(0x0000001E, (ULONG_PTR)status, (ULONG_PTR)__builtin_return_address(0), 0, 0); /* KMODE_EXCEPTION_NOT_HANDLED */
}
