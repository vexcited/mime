/*
 * x64 interrupts and exceptions. every vector enters KiTrapDispatch with
 * the kernel lock held, see entry.S.
 */
#include "../../ntos.h"

extern BOOLEAN KiTraceUserExceptions;

#define STATUS_ARRAY_BOUNDS_EXCEEDED_CODE ((NTSTATUS)0xC000008CL)
#define STATUS_FLOAT_DENORMAL_OPERAND ((NTSTATUS)0xC000008DL)
#define STATUS_FLOAT_DIVIDE_BY_ZERO ((NTSTATUS)0xC000008EL)
#define STATUS_FLOAT_INEXACT_RESULT ((NTSTATUS)0xC000008FL)
#define STATUS_FLOAT_INVALID_OPERATION ((NTSTATUS)0xC0000090L)
#define STATUS_FLOAT_OVERFLOW ((NTSTATUS)0xC0000091L)
#define STATUS_FLOAT_STACK_CHECK ((NTSTATUS)0xC0000092L)
#define STATUS_FLOAT_UNDERFLOW ((NTSTATUS)0xC0000093L)
#define STATUS_INTEGER_OVERFLOW_CODE ((NTSTATUS)0xC0000095L)
#define STATUS_PRIVILEGED_INSTRUCTION_CODE ((NTSTATUS)0xC0000096L)
#define STATUS_SINGLE_STEP_CODE ((NTSTATUS)0x80000004L)
#define STATUS_ASSERTION_FAILURE_CODE ((NTSTATUS)0xC0000420L)
#define STATUS_FLOAT_MULTIPLE_FAULTS ((NTSTATUS)0xC00002B4L)
#define STATUS_FLOAT_MULTIPLE_TRAPS ((NTSTATUS)0xC00002B5L)

/* the sse exception code for the unmasked flags mxcsr reports. */
static NTSTATUS simd_status(ULONG mxcsr)
{
    ULONG raised = mxcsr & ~(mxcsr >> 7) & 0x3F;
    if (raised & 0x01)
        return STATUS_FLOAT_INVALID_OPERATION;
    if (raised & 0x04)
        return STATUS_FLOAT_DIVIDE_BY_ZERO;
    if (raised & 0x02)
        return STATUS_FLOAT_DENORMAL_OPERAND;
    if (raised & 0x08)
        return STATUS_FLOAT_OVERFLOW;
    if (raised & 0x10)
        return STATUS_FLOAT_UNDERFLOW;
    if (raised & 0x20)
        return STATUS_FLOAT_INEXACT_RESULT;
    return STATUS_FLOAT_MULTIPLE_TRAPS;
}

static NTSTATUS x87_status(void)
{
    USHORT status, control;
    __asm__ volatile("fnstsw %0; fnstcw %1" : "=m"(status), "=m"(control));
    ULONG raised = status & ~control & 0x3F;
    if (raised & 0x01)
        return (status & 0x40) ? STATUS_FLOAT_STACK_CHECK : STATUS_FLOAT_INVALID_OPERATION;
    if (raised & 0x04)
        return STATUS_FLOAT_DIVIDE_BY_ZERO;
    if (raised & 0x02)
        return STATUS_FLOAT_DENORMAL_OPERAND;
    if (raised & 0x08)
        return STATUS_FLOAT_OVERFLOW;
    if (raised & 0x10)
        return STATUS_FLOAT_UNDERFLOW;
    if (raised & 0x20)
        return STATUS_FLOAT_INEXACT_RESULT;
    return STATUS_FLOAT_MULTIPLE_FAULTS;
}

/* the exception record windows builds for a user mode fault. */
static void build_record(PKTRAP_FRAME frame, PEXCEPTION_RECORD record)
{
    memset(record, 0, sizeof(*record));
    record->ExceptionAddress = (PVOID)frame->Rip;
    switch (frame->Vector) {
    case 0:
        record->ExceptionCode = STATUS_INTEGER_DIVIDE_BY_ZERO;
        break;
    case 1:
        record->ExceptionCode = STATUS_SINGLE_STEP_CODE;
        break;
    case 3:
        /* int3 leaves rip after itself; windows reports and resumes at the breakpoint. */
        frame->Rip--;
        record->ExceptionAddress = (PVOID)frame->Rip;
        record->ExceptionCode = STATUS_BREAKPOINT;
        record->NumberParameters = 1;
        break;
    case 4:
        record->ExceptionCode = STATUS_INTEGER_OVERFLOW_CODE;
        break;
    case 5:
        record->ExceptionCode = STATUS_ARRAY_BOUNDS_EXCEEDED_CODE;
        break;
    case 6:
        record->ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
        break;
    case 13:
        /* privileged instructions and non canonical addresses both raise #gp; windows tells them apart by decoding. */
        if (frame->ErrorCode == 0) {
            PUCHAR rip = (PUCHAR)frame->Rip;
            UCHAR op[2] = {};
            if (NT_SUCCESS(KiCopyFromUser(op, rip, 2)) &&
                (op[0] == 0xF4 || op[0] == 0xFA || op[0] == 0xFB || op[0] == 0xE4 || op[0] == 0xE5 || op[0] == 0xE6 ||
                 op[0] == 0xE7 || op[0] == 0xEC || op[0] == 0xED || op[0] == 0xEE || op[0] == 0xEF ||
                 (op[0] == 0x0F && (op[1] == 0x01 || op[1] == 0x06 || op[1] == 0x08 || op[1] == 0x09 ||
                                    op[1] == 0x20 || op[1] == 0x22 || op[1] == 0x30 || op[1] == 0x32)))) {
                record->ExceptionCode = STATUS_PRIVILEGED_INSTRUCTION_CODE;
                break;
            }
        }
        record->ExceptionCode = STATUS_ACCESS_VIOLATION;
        record->NumberParameters = 2;
        record->ExceptionInformation[0] = 0;
        record->ExceptionInformation[1] = ~0ull;
        break;
    case 14:
        record->ExceptionCode = STATUS_ACCESS_VIOLATION;
        record->NumberParameters = 2;
        record->ExceptionInformation[0] = (frame->ErrorCode & 0x10) ? 8 : (frame->ErrorCode & 0x2) ? 1 : 0;
        record->ExceptionInformation[1] = frame->Cr2;
        break;
    case 16:
        record->ExceptionCode = x87_status();
        break;
    case 17:
        record->ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
        break;
    case 19:
        record->ExceptionCode = simd_status(frame->MxCsr);
        break;
    case 0x29:
        /* __fastfail: rcx holds the failure code. */
        record->ExceptionCode = STATUS_STACK_BUFFER_OVERRUN;
        record->ExceptionFlags = EXCEPTION_NONCONTINUABLE;
        record->NumberParameters = 1;
        record->ExceptionInformation[0] = frame->Rcx;
        break;
    case 0x2C:
        record->ExceptionCode = STATUS_ASSERTION_FAILURE_CODE;
        break;
    case 0x2D:
        /* debug service: no debugger listens, so resume past the int3 that follows. */
        frame->Rip++;
        record->ExceptionAddress = (PVOID)frame->Rip;
        record->ExceptionCode = STATUS_BREAKPOINT;
        record->NumberParameters = 1;
        break;
    default:
        record->ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
        break;
    }
}

static void user_exception(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    thread->TrapFrame = frame;
    thread->PreviousMode = UserMode;
    KiEnableInterrupts();

    if (frame->Vector == 14 && !(frame->ErrorCode & 0x8)) {
        BOOLEAN write = (frame->ErrorCode & 0x2) != 0;
        BOOLEAN exec = (frame->ErrorCode & 0x10) != 0;
        extern char KiTimelineImage[16];
        ULONG64 started = KiTimelineImage[0] ? HalReadCounter() : 0;
        BOOLEAN resolved = MmAccessFault(frame->Cr2, write, exec, UserMode);
        if (started)
            KiTimelineFault(started, 1, (ULONG)(frame->Cr2 >> 12));
        if (resolved) {
            KiExitToUserMode(frame);
            return;
        }
    }

    EXCEPTION_RECORD record;
    build_record(frame, &record);
    /* a fast fail ends the process at once, no handler of the program sees it. */
    if (frame->Vector == 0x29) {
        PEPROCESS process = PsCurrentProcess();
        KiLogPrintf("fast fail %llu in process %u (%s)", (ULONGLONG)frame->Rcx,
                    (ULONG)(ULONG_PTR)process->UniqueProcessId, process->ImageFileName);
        KiDumpTrapFrame("user context", frame);
        KiTraceUserFrames(frame);
        PspTerminateProcess(process, STATUS_STACK_BUFFER_OVERRUN);
        KiExitToUserMode(frame);
        return;
    }
    KiApplyFaultStatus(&record);
    if (KiTraceUserExceptions) {
        KiLogPrintf("first chance exception %08x at %p in %s", record.ExceptionCode, (PVOID)frame->Rip,
                    PsCurrentProcess()->ImageFileName);
        KiDumpTrapFrame("user context", frame);
        KiTraceUserFrames(frame);
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

static void kernel_exception(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    if (frame->Vector == 14) {
        ULONG_PTR address = frame->Cr2;
        BOOLEAN write = (frame->ErrorCode & 0x2) != 0;
        if (__builtin_expect(MmVerifyPool, 0))
            MiSpecialPoolFault(address, (ULONG)frame->ErrorCode, frame->Rip);
        if (address <= MIME_USER_ADDRESS_LIMIT && KI_TRAP_INTERRUPTS_ENABLED(frame)) {
            /* demand faults on user memory touched by the kernel, only from interruptible code. */
            KiEnableInterrupts();
            if (MmAccessFault(address, write, FALSE, KernelMode))
                return;
        }
        if (address <= MIME_USER_ADDRESS_LIMIT && thread && thread->FaultRecovery) {
            KJUMP_BUFFER *recovery = thread->FaultRecovery;
            thread->FaultRecovery = nullptr;
            KeGetPcr()->Irql = PASSIVE_LEVEL;
            KiEnableInterrupts();
            KiLongJump(recovery, 1);
        }
    }
    if (frame->Vector == 13 && thread && thread->FaultRecovery) {
        /* a non canonical user pointer. */
        KJUMP_BUFFER *recovery = thread->FaultRecovery;
        thread->FaultRecovery = nullptr;
        KeGetPcr()->Irql = PASSIVE_LEVEL;
        KiEnableInterrupts();
        KiLongJump(recovery, 1);
    }
    if (KiDispatchKernelTrap(frame))
        return;
    KiDumpTrapFrame("kernel exception", frame);
    if (frame->Vector == 14)
        KeBugCheckEx(0x00000050, frame->Cr2, frame->ErrorCode, frame->Rip, 0); /* PAGE_FAULT_IN_NONPAGED_AREA */
    if (frame->Vector == 8)
        KeBugCheckEx(0x0000007F, 8, 0, frame->Rip, 0); /* UNEXPECTED_KERNEL_MODE_TRAP, double fault */
    KeBugCheckEx(0x0000001E, frame->Vector, frame->ErrorCode, frame->Rip, 0); /* KMODE_EXCEPTION_NOT_HANDLED */
}

static void interrupt(PKTRAP_FRAME frame)
{
    if (KI_TRAP_FROM_USER(frame)) {
        KiCurrentThread()->TrapFrame = frame;
        KiCurrentThread()->PreviousMode = UserMode;
    }
    PKPCR pcr = KeGetPcr();
    PKPRCB prcb = pcr->CurrentPrcb;
    KIRQL old = pcr->Irql;
    pcr->Irql = CLOCK_LEVEL;
    prcb->InterruptCount++;
    ULONG64 start = HalReadCounter();

    switch (frame->Vector) {
    case KI_VECTOR_CLOCK:
        KiClockInterrupt(frame);
        break;
    case KI_VECTOR_IPI:
        prcb->IpiReceived++;
        break;
    case KI_VECTOR_HOST: {
        ULONG pending = __atomic_exchange_n(&HalSharedPage->irq_pending, 0, __ATOMIC_ACQ_REL);
        if (pending)
            MimeSessionInterrupt(pending);
        break;
    }
    }
    if (frame->Vector != KI_VECTOR_SPURIOUS)
        HalEndOfInterrupt((ULONG)frame->Vector);
    KiChargeServiceTime(prcb, start, FALSE);

    pcr->Irql = old;
    if (old < DISPATCH_LEVEL) {
        if (prcb->DpcPending) {
            pcr->Irql = DISPATCH_LEVEL;
            KiDeliverDpcs();
            pcr->Irql = old;
        }
        KiCheckPreemption();
        if (KI_TRAP_FROM_USER(frame))
            KiExitToUserMode(frame);
    }
}

/* first try at a user not present page fault, before the kernel lock, see entry.S. */
BOOLEAN KiFastPageFault(PKTRAP_FRAME frame)
{
    extern char KiTimelineImage[16];
    ULONG64 started = KiTimelineImage[0] ? HalReadCounter() : 0;
    BOOLEAN done = MmFastFault(frame->Cr2, (frame->ErrorCode & 0x2) != 0, (frame->ErrorCode & 0x10) != 0);
    if (started)
        KiTimelineFault(started, done ? 0 : 2, (ULONG)(frame->Cr2 >> 12));
    return done;
}

void KiTrapDispatch(PKTRAP_FRAME frame)
{
    /* the lock holder for statistics: the vector, past the system service numbers. */
    KiKernelLock[3] = 0x2001 + (frame->Vector & 0xFF);
    /* int 2e, the system call of ntdll's stubs when SharedUserData says syscall is unavailable. */
    if (frame->Vector == 0x2E && KI_TRAP_FROM_USER(frame)) {
        frame->Vector = KTRAP_VECTOR_SYSCALL;
        KiSystemService(frame);
        return;
    }
    if (frame->Vector >= 32 && frame->Vector != 0x29 && frame->Vector != 0x2C && frame->Vector != 0x2D)
        interrupt(frame);
    else if (KI_TRAP_FROM_USER(frame))
        user_exception(frame);
    else
        kernel_exception(frame);
}
