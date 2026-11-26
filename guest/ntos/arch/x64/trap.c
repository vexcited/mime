/*
 * x64 interrupts and exceptions. every vector enters KiTrapDispatch with
 * the kernel lock held, see entry.S.
 */
#include "../../ntos.h"


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
#define STATUS_WX86_SINGLE_STEP_CODE ((NTSTATUS)0x4000001EL)
#define STATUS_WX86_BREAKPOINT_CODE ((NTSTATUS)0x4000001FL)
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

/*
 * the instructions windows 11 answers with STATUS_PRIVILEGED_INSTRUCTION instead of an access violation, measured
 * with scratch/dlls/probe/excodes.c of the research repository: hlt, cli, sti, in, out and their string forms,
 * lldt, ltr, lgdt, lidt, lmsw, invlpg, swapgs, clts, sysret, invd, wbinvd, the moves of control and debug
 * registers, wrmsr, rdmsr, rdpmc and sysexit. vmcall and xsetbv are plain access violations.
 */
static BOOLEAN privileged_instruction(ULONG64 rip)
{
    UCHAR code[15] = {};
    ULONG length = sizeof(code);
    /* the instruction may end at the last byte of a page. */
    while (length && !NT_SUCCESS(KiCopyFromUser(code, (PVOID)rip, length)))
        length--;
    ULONG i = 0;
    for (; i < length; i++) {
        UCHAR b = code[i];
        BOOLEAN prefix = b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 || b == 0x2E || b == 0x36 ||
                         b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65 || (b & 0xF0) == 0x40;
        if (!prefix)
            break;
    }
    if (i >= length)
        return FALSE;
    UCHAR op = code[i];
    if (op == 0xF4 || op == 0xFA || op == 0xFB || (op >= 0xE4 && op <= 0xE7) || (op >= 0xEC && op <= 0xEF) ||
        (op >= 0x6C && op <= 0x6F))
        return TRUE;
    if (op != 0x0F || i + 1 >= length)
        return FALSE;
    op = code[i + 1];
    if (op == 0x06 || op == 0x07 || op == 0x08 || op == 0x09 || (op >= 0x20 && op <= 0x23) || op == 0x30 || op == 0x32 ||
        op == 0x33 || op == 0x35)
        return TRUE;
    if ((op != 0x00 && op != 0x01) || i + 2 >= length)
        return FALSE;
    UCHAR modrm = code[i + 2], reg = (modrm >> 3) & 7;
    if (op == 0x00)
        return reg == 2 || reg == 3;
    if (modrm == 0xF8)
        return TRUE;
    return reg == 6 || ((modrm & 0xC0) != 0xC0 && (reg == 2 || reg == 3 || reg == 7));
}

/* 0F 01 D9 (vmmcall) is ill-formed at ring 3 on the reference processor, where 0F 01 C1 (vmcall) is merely
   privileged; windows answers the two differently and a probe compares them. */
static BOOLEAN illegal_instruction(ULONG64 rip)
{
    UCHAR code[8] = {};
    ULONG length = sizeof(code);
    while (length && !NT_SUCCESS(KiCopyFromUser(code, (PVOID)rip, length)))
        length--;
    ULONG i = 0;
    for (; i < length; i++) {
        UCHAR b = code[i];
        BOOLEAN prefix = b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 || b == 0x2E || b == 0x36 ||
                         b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65 || (b & 0xF0) == 0x40;
        if (!prefix)
            break;
    }
    return i + 2 < length && code[i] == 0x0F && code[i + 1] == 0x01 && code[i + 2] == 0xD9;
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
    case 1: {
        record->ExceptionCode = STATUS_SINGLE_STEP_CODE;
        /* handlers get the context with the trap flag off, as on windows: one that continues is not trapped again. */
        frame->Rflags &= ~0x100ull;
        break;
    }
    case 3:
        /*
         * int3 leaves rip after itself; windows reports and resumes at the breakpoint. the trap flag stays in the
         * context, so a handler that continues is stepped one instruction later. measured on windows 11 with
         * scratch/dlls/crash3/sysflags.c and scratch/dlls/probe/step.c of the research repository.
         */
        frame->Rip--;
        record->ExceptionAddress = (PVOID)frame->Rip;
        record->ExceptionCode = STATUS_BREAKPOINT;
        record->NumberParameters = 1;
        break;
    case 4:
        /* into, or int 4: windows reports the byte before where it resumes. */
        record->ExceptionAddress = (PVOID)(frame->Rip - 1);
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
        if (frame->ErrorCode == 0 && illegal_instruction(frame->Rip)) {
            record->ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
            break;
        }
        if (frame->ErrorCode == 0 && privileged_instruction(frame->Rip)) {
            record->ExceptionCode = STATUS_PRIVILEGED_INSTRUCTION_CODE;
            break;
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
        /* an assertion is reported and resumed at its int 2c. */
        frame->Rip -= 2;
        record->ExceptionAddress = (PVOID)frame->Rip;
        record->ExceptionCode = STATUS_ASSERTION_FAILURE_CODE;
        break;
    case 0x2D:
        /* debug service: the breakpoint is reported past the byte that follows, and resumes on that byte. */
        record->ExceptionAddress = (PVOID)(frame->Rip + 1);
        record->ExceptionCode = STATUS_BREAKPOINT;
        record->NumberParameters = 1;
        /* the service asked for, in rax. */
        record->ExceptionInformation[0] = frame->Rax;
        break;
    default:
        record->ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
        break;
    }
    /*
     * a trap taken in the 32 bit code segment carries the wx86 codes, measured on windows 11 26300 with
     * scratch/dlls/probe/gate32tf.c of the research repository. a program that steps through a far jump into
     * that segment waits for these and ignores the 64 bit ones.
     */
    if ((frame->Cs & 0xFFF8) == 0x20) {
        if (record->ExceptionCode == STATUS_SINGLE_STEP_CODE)
            record->ExceptionCode = STATUS_WX86_SINGLE_STEP_CODE;
        else if (record->ExceptionCode == STATUS_BREAKPOINT)
            record->ExceptionCode = STATUS_WX86_BREAKPOINT_CODE;
    }
}

/*
 * the alignment fault fixup of windows. a thread that asked for it, or whose process did, has the aligned packed
 * move that raised #gp rewritten in place to the unaligned one, and runs it again: movaps and movapd become movups
 * and movupd, movdqa becomes movdqu. measured on windows 11 26300: one prefix of each kind at most, a segment, 66,
 * 67 and rex, in any order. F3 lands on the first byte of a movdqa whatever that byte was. the operand is not
 * looked at, so a move that faulted for another reason is rewritten too and faults again. the page keeps its
 * protection.
 */
static BOOLEAN fix_alignment_fault(PKTRAP_FRAME frame)
{
    if (!KiCurrentThread()->AutoAlignment && !(PsCurrentProcess()->DefaultHardErrorMode & 4 /* SEM_NOALIGNMENTFAULTEXCEPT */))
        return FALSE;
    UCHAR code[6];
    ULONG length = 0;
    while (length < sizeof(code) && NT_SUCCESS(KiCopyFromUser(&code[length], (PVOID)(frame->Rip + length), 1)))
        length++;
    ULONG seen = 0, i = 0;
    for (; i < length; i++) {
        UCHAR byte = code[i];
        ULONG kind;
        if (byte == 0x26 || byte == 0x2E || byte == 0x36 || byte == 0x3E || byte == 0x64 || byte == 0x65)
            kind = 1;
        else if (byte == 0x66)
            kind = 2;
        else if (byte == 0x67)
            kind = 4;
        else if ((byte & 0xF0) == 0x40)
            kind = 8;
        else
            break;
        if (seen & kind)
            return FALSE;
        seen |= kind;
    }
    if (i + 1 >= length || code[i] != 0x0F)
        return FALSE;
    UCHAR op = code[i + 1];
    NTSTATUS status;
    if (op == 0x28 || op == 0x29)
        status = MmPatchUserCode((PVOID)(frame->Rip + i + 1), (UCHAR)(op - 0x18));
    else if ((op == 0x6F || op == 0x7F) && (seen & 2))
        status = MmPatchUserCode((PVOID)frame->Rip, 0xF3);
    else
        return FALSE;
    if (!NT_SUCCESS(status))
        return FALSE;
    KiFlushInstructionCache();
    return TRUE;
}

static void user_exception(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    thread->TrapFrame = frame;
    thread->PreviousMode = UserMode;
    ULONG64 gs_base = KiReadMsr(MSR_KERNEL_GS_BASE);
    KiEnableInterrupts();
    if (frame->Vector == 14 && !(frame->ErrorCode & 0x8)) {
        BOOLEAN write = (frame->ErrorCode & 0x2) != 0;
        BOOLEAN exec = (frame->ErrorCode & 0x10) != 0;
        if (MmAccessFault(frame->Cr2, write, exec, UserMode)) {
            KiExitToUserMode(frame);
            return;
        }
        /*
         * like KiPageFault of ntoskrnl 26300: a failed access of 64-bit code while its gs base is not the teb, as
         * after a load of the gs selector, gets the teb back as gs base and runs again. wow64cpu loads the 32-bit
         * data selector into gs and then reads the 64-bit teb through gs. the gs base is read at the fault, as
         * windows keeps it in the trap frame: a switch since then has given the thread its teb back.
         */
        PVOID teb = thread->Teb;
        if (frame->Cs == 0x33 && teb && gs_base != (ULONG64)teb) {
            KiWriteMsr(MSR_KERNEL_GS_BASE, (ULONG64)teb);
            KiExitToUserMode(frame);
            return;
        }
    }

    if (frame->Vector == 13 && fix_alignment_fault(frame)) {
        KiExitToUserMode(frame);
        return;
    }

    /*
     * the debug services a kernel without a debugger takes for itself, measured on windows 11 26300: the print, the
     * two notices of symbols and the command string raise nothing, rax stays, and the thread goes on past the byte
     * that follows the int 2d. the others are breakpoints. the text of a print is debug output of the program.
     */
    if (frame->Vector == 0x2D && (frame->Rax == 1 || (frame->Rax >= 3 && frame->Rax <= 5))) {
        char text[512];
        ULONG length = frame->Rdx < sizeof(text) ? (ULONG)frame->Rdx : (ULONG)sizeof(text);
        if (frame->Rax == 1 && length && NT_SUCCESS(KiCopyFromUser(text, (PVOID)frame->Rcx, length)))
            MimeSendDebugOutput(PsCurrentProcess(), text, length);
        frame->Rip++;
        KiExitToUserMode(frame);
        return;
    }
    EXCEPTION_RECORD record;
    KiCaptureDebugStatus(thread);
    if (frame->Vector == 1)
        KiRestoreHeldBreakpoint(thread, frame);
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
    /* a user data breakpoint on memory the kernel copied: windows lets the kernel go on, the program never sees it. */
    if (frame->Vector == 1) {
        __asm__ volatile("mov %0, %%dr6" ::"r"(0xFFFF0FF0ull));
        return;
    }
    /* a page that is not there of an image the list of modules names is read from its file, at any irql. */
    if (frame->Vector == 14 && frame->Cr2 > MIME_USER_ADDRESS_LIMIT && !(frame->ErrorCode & 1) &&
        MiSystemImageFault(frame->Cr2))
        return;
    /* a fault on kernel memory, or a trap that is not a page fault, is rare enough to keep a line of. */
    if (frame->Vector != 14 || frame->Cr2 > MIME_USER_ADDRESS_LIMIT)
        KiLogPrintf("kernel exception %u at rip %p rsp %p cr2 %p err %x", frame->Vector, (PVOID)frame->Rip,
                    (PVOID)frame->Rsp, (PVOID)frame->Cr2, frame->ErrorCode);
    if (frame->Vector == 14) {
        ULONG_PTR address = frame->Cr2;
        BOOLEAN write = (frame->ErrorCode & 0x2) != 0;
        if (address <= MIME_USER_ADDRESS_LIMIT && KI_TRAP_INTERRUPTS_ENABLED(frame)) {
            /* demand faults on user memory touched by the kernel, only from interruptible code. */
            KiEnableInterrupts();
            if (MmAccessFault(address, write, FALSE, KernelMode))
                return;
        }
        if (address <= MIME_USER_ADDRESS_LIMIT && thread && thread->FaultRecovery) {
            KJUMP_BUFFER *recovery = thread->FaultRecovery;
            thread->FaultRecovery = recovery->Outer;
            KeGetPcr()->Irql = PASSIVE_LEVEL;
            KiEnableInterrupts();
            KiLongJump(recovery, 1);
        }
    }
    if (frame->Vector == 13 && thread && thread->FaultRecovery) {
        /* a non canonical user pointer. */
        KJUMP_BUFFER *recovery = thread->FaultRecovery;
        thread->FaultRecovery = recovery->Outer;
        KeGetPcr()->Irql = PASSIVE_LEVEL;
        KiEnableInterrupts();
        KiLongJump(recovery, 1);
    }
    if (frame->Vector == 8)
        KiLogPrintf("double fault at rip %p rsp %p cr2 %p cs %x err %x", (PVOID)frame->Rip, (PVOID)frame->Rsp,
                    (PVOID)frame->Cr2, frame->Cs, frame->ErrorCode);
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
    if (!KI_TRAP_FROM_USER(frame))
        KiObserveKernelInterrupt(frame, old);
    pcr->Irql = CLOCK_LEVEL;
    prcb->InterruptCount++;
    ULONG64 start = HalReadCounter();
    if (prcb->HaltedSince)
        KiIdleHaltEnded(prcb, start, frame->Vector == KI_VECTOR_IPI);

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
    /* the interrupt is already ended, before the kernel lock was taken: see KiTrapCommon. */
    KiChargeServiceTime(prcb, start, FALSE);

    pcr->Irql = old;
    /* an idle processor in its wait may be without the kernel lock: its idle loop does this once the wait is over. */
    if (old < DISPATCH_LEVEL && !prcb->IdleWaiting) {
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

/*
 * the clock or the request of another processor taken in user mode, before the kernel lock. both are the dispatcher
 * lock's work, as on an idle processor, and so is the switch to another thread. interrupts stay off and UserMode
 * stays set, as in KiFastSystemService, and the lock is taken for what is left: a dpc, an apc, the end of the thread.
 * 0 leaves all of it to the usual way, 1 goes back to user mode without the lock and 2 with it.
 */
ULONG KiFastUserInterrupt(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    PKPCR pcr = KeGetPcr();
    PKPRCB prcb = pcr->CurrentPrcb;
    ULONG vector = (ULONG)frame->Vector;
    thread->TrapFrame = frame;
    thread->PreviousMode = UserMode;
    KiNoteUserTrap(thread, frame);
    pcr->Irql = CLOCK_LEVEL;
    prcb->InterruptCount++;
    ULONG64 start = HalReadCounter();
    if (vector == KI_VECTOR_CLOCK)
        KiClockInterrupt(frame);
    else
        prcb->IpiReceived++;
    KiChargeServiceTime(prcb, start, FALSE);
    pcr->Irql = PASSIVE_LEVEL;

    BOOLEAN owned;
    for (;;) {
        /* the thread resumes on any processor, which may hold the lock from the thread before. */
        pcr = KeGetPcr();
        owned = KiKernelLock[KI_LOCK_OWNER] == pcr->Prcb.Number;
        if (owned || !pcr->Prcb.DispatchPending || pcr->Irql != PASSIVE_LEVEL)
            break;
        KiCheckPreemption();
    }
    /* whoever asks for more after this finds the thread running and interrupts it again. */
    if (!owned && !pcr->Prcb.DpcPending && !thread->KernelApcPending && !thread->Terminating && !thread->UserApcPending &&
        pcr->Irql == PASSIVE_LEVEL) {
        pcr->Prcb.FastInterrupts++;
        return 1;
    }
    if (!owned) {
        /* a processor that changes an address space waits for those in user mode, and the wait for the lock answers none. */
        pcr->UserMode = 0;
        KiAcquireKernelLock();
    }
    KiKernelLock[KI_LOCK_REASON] = 0x2001 + vector;
    KiNoteLockWait(0x2001 + vector);
    pcr->Prcb.FastInterruptsLocked++;
    if (pcr->Prcb.DpcPending) {
        KIRQL old = pcr->Irql;
        pcr->Irql = DISPATCH_LEVEL;
        KiDeliverDpcs();
        pcr->Irql = old;
    }
    KiCheckPreemption();
    KiExitToUserMode(frame);
    return 2;
}

/* first try at a user not present page fault, before the kernel lock, see entry.S. */
BOOLEAN KiFastPageFault(PKTRAP_FRAME frame)
{
    KiNoteUserTrap(KiCurrentThread(), frame);
    return MmFastFault(frame->Cr2, (frame->ErrorCode & 0x2) != 0, (frame->ErrorCode & 0x10) != 0);
}

void KiTrapDispatch(PKTRAP_FRAME frame)
{
    /*
     * the lock holder for statistics: the vector, past the system service numbers. a holder this trap interrupted
     * keeps its own, which the release clears.
     */
    if (frame->Flags & KTRAP_FLAG_LOCK_TAKEN)
        KiForeignTrapTaken(frame);
    if (KiKernelLock[KI_LOCK_OWNER] == KeGetCurrentPrcb()->Number) {
        if (!KiKernelLock[KI_LOCK_REASON])
            KiKernelLock[KI_LOCK_REASON] = 0x2001 + (frame->Vector & 0xFF);
        KiNoteLockWait(0x2001 + (frame->Vector & 0xFF));
    }
    /* this frame holds every register of user mode, which the frame of a system service does not. */
    if (KI_TRAP_FROM_USER(frame))
        KiNoteUserTrap(KiCurrentThread(), frame);
    if (frame->Vector >= KI_VECTOR_FIRST)
        interrupt(frame);
    else if (KI_TRAP_FROM_USER(frame))
        user_exception(frame);
    else
        kernel_exception(frame);
}
