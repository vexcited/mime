/*
 * x64 threads and contexts: descriptor tables, syscall msrs, extended state,
 * switch and user entry frames, CONTEXT conversion.
 */
#include "../../ntos.h"

void KiSwapContextInternal(ULONG64 *old_sp, ULONG64 new_sp, PVOID old_xsave, PVOID new_xsave);
void KiSystemCall64(void);
void KiSystemCall32(void);
extern UCHAR KiIsrBase[];
extern UCHAR KiFlushIpi[];

ULONG KiXsaveSize;
ULONG64 KiXsaveMask;

static ULONG64 g_idt[512];
static DECLSPEC_ALIGN(16) UCHAR g_fault_stacks[MAXIMUM_PROCESSORS][8192];

#define CR4_PGE (1ull << 7)
#define CR4_OSFXSR (1ull << 9)
#define CR4_OSXMMEXCPT (1ull << 10)
#define CR4_OSXSAVE (1ull << 18)
#define CR4_SMEP (1ull << 20)

/* the legacy area of the user extended state, as in XSAVE_FORMAT. */
#define XSAVE_HEADER 512
#define XSAVE_LEGACY_MXCSR 24

void KiSwitchAddressSpace(PKPROCESS process)
{
    if (KiReadCr3() != process->DirectoryTableBase)
        KiWriteCr3(process->DirectoryTableBase);
}

/* in 32-bit code, fs of a wow64 thread points at its 32-bit teb. */
static void set_teb32(ULONG_PTR teb32)
{
    ULONG64 *entry = &KeGetPcr()->GdtBase[KGDT64_R3_CMTEB / 8];
    *entry = (*entry & ~(0xFFFFFFull << 16 | 0xFFull << 56)) | (teb32 & 0xFFFFFF) << 16 | ((teb32 >> 24) & 0xFF) << 56;
    KiWriteMsr(MSR_FS_BASE, teb32);
}

/* dr7: the enables of the four breakpoints and their conditions and lengths, never general detect. */
#define DR7_USER_MASK 0xFFFF00FFull

static void load_debug(PKTHREAD thread)
{
    ULONG64 dr7 = thread->UserDr7 & 0xFF ? thread->UserDr7 : 0;
    __asm__ volatile("mov %0, %%dr7" ::"r"(0ull));
    if (dr7) {
        __asm__ volatile("mov %0, %%dr0" ::"r"(thread->UserDr[0]));
        __asm__ volatile("mov %0, %%dr1" ::"r"(thread->UserDr[1]));
        __asm__ volatile("mov %0, %%dr2" ::"r"(thread->UserDr[2]));
        __asm__ volatile("mov %0, %%dr3" ::"r"(thread->UserDr[3]));
        __asm__ volatile("mov %0, %%dr7" ::"r"(dr7));
    }
}

/*
 * like windows, a context's breakpoints name user addresses only, others become 0, and dr7 keeps what user mode may
 * use. the thread loads them when it runs, at once when it is the caller.
 */
void KiSetThreadDebug(PKTHREAD thread, const CONTEXT *ctx)
{
    const ULONG64 dr[4] = {ctx->Dr0, ctx->Dr1, ctx->Dr2, ctx->Dr3};
    for (int i = 0; i < 4; i++)
        thread->UserDr[i] = dr[i] <= MIME_USER_ADDRESS_LIMIT ? dr[i] : 0;
    thread->UserDr6 = ctx->Dr6;
    thread->UserDr7 = ctx->Dr7 & DR7_USER_MASK;
    if (thread == KiCurrentThread())
        load_debug(thread);
}

void KiGetThreadDebug(PKTHREAD thread, CONTEXT *ctx)
{
    ctx->Dr0 = thread->UserDr[0];
    ctx->Dr1 = thread->UserDr[1];
    ctx->Dr2 = thread->UserDr[2];
    ctx->Dr3 = thread->UserDr[3];
    ctx->Dr6 = thread->UserDr6;
    ctx->Dr7 = thread->UserDr7;
}

void KiSwapContext(PKTHREAD old, PKTHREAD next)
{
    /* the processor holds the breakpoints of the thread that runs, and none for threads without. */
    if ((next->UserDr7 & 0xFF) || (old->UserDr7 & 0xFF))
        load_debug(next);
    /* in user mode gs is the teb, swapped in at each return there. */
    if (next->Teb)
        KiWriteMsr(MSR_KERNEL_GS_BASE, (ULONG64)next->Teb);
    if (next->Teb32)
        set_teb32((ULONG_PTR)next->Teb32);
    KiSwapContextInternal(&old->KernelSp, next->KernelSp, old->XSaveArea, next->XSaveArea);
}

/*
 * an idle processor releases the kernel lock and spins briefly on its work flags. a waker that sees it
 * spinning, see KiRequestDispatch, skips the interrupt, which would cost both sides an exit to the host.
 * only then does it halt, and a halted processor is interrupted.
 *
 * hlt ends only on an interrupt taken with interrupts enabled. an interrupt entry that finds IdleSleeping
 * takes the lock back first, so interrupts still run with the lock held and the processor holds it after.
 */
#define KI_IDLE_SPIN_TICKS 40000

void KiIdleWait(void)
{
    PKPCR pcr = KeGetPcr();
    PKPRCB prcb = pcr->CurrentPrcb;
    pcr->IdleSleeping = 1;
    if (!(KiOptions & KI_OPT_NO_IDLE_SPIN))
        __atomic_store_n(&prcb->IdleState, KI_IDLE_POLLING, __ATOMIC_SEQ_CST);
    KiReleaseKernelLock();
    __asm__ volatile("sti" ::: "memory");
    ULONG64 end = HalReadCounter() + ((KiOptions & KI_OPT_NO_IDLE_SPIN) ? 0 : KI_IDLE_SPIN_TICKS);
    /* no pause: the host would take a vcpu looping on it for one waiting on a lock. */
    while (pcr->IdleSleeping && !prcb->DispatchPending && !prcb->DpcPending && HalReadCounter() < end)
        ;
    __asm__ volatile("cli" ::: "memory");
    if (pcr->IdleSleeping && !prcb->DispatchPending && !prcb->DpcPending) {
        /* published before looking again, so a request that came meanwhile is seen or sends the interrupt. */
        __atomic_store_n(&prcb->IdleState, KI_IDLE_HALTED, __ATOMIC_SEQ_CST);
        if (!prcb->DispatchPending && !prcb->DpcPending)
            __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
    __atomic_store_n(&prcb->IdleState, KI_IDLE_RUNNING, __ATOMIC_SEQ_CST);
    if (pcr->IdleSleeping) {
        pcr->IdleSleeping = 0;
        KiAcquireKernelLock();
    }
}

/* x64 keeps instruction fetches coherent with stores. */
void KiFlushInstructionCache(void)
{
}

static void set_gate(ULONG vector, PVOID handler, ULONG dpl, ULONG ist)
{
    ULONG64 address = (ULONG64)handler;
    g_idt[vector * 2] = (address & 0xFFFF) | ((ULONG64)KGDT64_R0_CODE << 16) | ((ULONG64)ist << 32) |
                        ((ULONG64)(0x8E | dpl << 5) << 40) | ((address >> 16) & 0xFFFF) << 48;
    g_idt[vector * 2 + 1] = address >> 32;
}

static void initialize_idt(void)
{
    for (ULONG v = 0; v < 256; v++) {
        ULONG dpl = 0;
        /* int3, into, __fastfail, assertions, debug services and int 2e system calls come from user code. */
        if (v == 3 || v == 4 || v == 0x29 || v == 0x2C || v == 0x2D || v == 0x2E)
            dpl = 3;
        set_gate(v, KiIsrBase + v * 16, dpl, v == 8 || v == 2 || v == 18 ? 1 : 0);
    }
    set_gate(KI_VECTOR_FLUSH, KiFlushIpi, 0, 0);
}

static ULONG64 segment(ULONG64 access, ULONG64 flags)
{
    return 0xFFFFull | access << 40 | 0xFull << 48 | flags << 52;
}

/* the extended state saved for user threads: x87, sse, avx and avx-512. */
static void initialize_xsave(void)
{
    ULONG regs[4];
    KiCpuid(1, 0, regs);
    if (!(regs[2] & (1u << 26)))
        KeBugCheckEx(0x0000005D, 1, 0, 0, 0); /* UNSUPPORTED_PROCESSOR */
    KiCpuid(0xD, 0, regs);
    ULONG64 supported = (ULONG64)regs[3] << 32 | regs[0];
    KiXsaveMask = supported & (0x7 | 0xE0); /* x87, sse, avx, opmask and the zmm registers */
    __asm__ volatile("xsetbv" ::"c"(0), "a"((ULONG)KiXsaveMask), "d"((ULONG)(KiXsaveMask >> 32)));
    KiCpuid(0xD, 0, regs);
    KiXsaveSize = (regs[1] + 63) & ~63u;
}

void KiInitializeProcessorArch(void)
{
    PKPCR pcr = KeGetPcr();
    ULONG number = pcr->Prcb.Number;
    if (number == 0)
        initialize_idt();

    ULONG64 cr4;
    __asm__ volatile("movq %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_PGE | CR4_OSFXSR | CR4_OSXMMEXCPT | CR4_OSXSAVE;
    ULONG regs[4];
    KiCpuid(7, 0, regs);
    if (regs[1] & (1u << 7))
        cr4 |= CR4_SMEP;
    __asm__ volatile("movq %0, %%cr4" ::"r"(cr4) : "memory");
    if (number == 0)
        initialize_xsave();
    else
        __asm__ volatile("xsetbv" ::"c"(0), "a"((ULONG)KiXsaveMask), "d"((ULONG)(KiXsaveMask >> 32)));

    /* the windows gdt layout: kernel code and data, 32-bit and 64-bit user code, user data, the tss. */
    ULONG64 *gdt = pcr->Gdt;
    memset(pcr->Gdt, 0, sizeof(pcr->Gdt));
    gdt[KGDT64_R0_CODE / 8] = segment(0x9B, 0x2);
    gdt[KGDT64_R0_DATA / 8] = segment(0x93, 0xC);
    gdt[KGDT64_R3_CMCODE / 8] = segment(0xFB, 0xC);
    gdt[KGDT64_R3_DATA / 8] = segment(0xF3, 0xC);
    gdt[KGDT64_R3_CODE / 8] = segment(0xFB, 0x2);
    gdt[KGDT64_R3_CMTEB / 8] = segment(0xF3, 0x4) & ~0xFFFFull; /* the 32-bit teb, base set per thread */
    gdt[KGDT64_R3_CMTEB / 8] |= 0xFFF;
    ULONG64 tss = (ULONG64)pcr->Tss;
    memset(pcr->Tss, 0, sizeof(pcr->Tss));
    *(ULONG64 *)(pcr->Tss + 0x24) = (ULONG64)(g_fault_stacks[number] + sizeof(g_fault_stacks[number])); /* ist1 */
    *(USHORT *)(pcr->Tss + 0x66) = sizeof(pcr->Tss); /* no i/o permission bitmap */
    gdt[KGDT64_SYS_TSS / 8] = (sizeof(pcr->Tss) - 1) | (tss & 0xFFFFFF) << 16 | 0x89ull << 40 |
                              ((tss >> 24) & 0xFF) << 56;
    gdt[KGDT64_SYS_TSS / 8 + 1] = tss >> 32;
    pcr->GdtBase = gdt;
    pcr->TssBase = pcr->Tss;
    pcr->IdtBase = g_idt;

    struct __attribute__((packed)) {
        USHORT limit;
        ULONG64 base;
    } gdtr = {sizeof(pcr->Gdt) - 1, (ULONG64)gdt}, idtr = {sizeof(g_idt) - 1, (ULONG64)g_idt};
    /* loading gs zeroes its base, so it is written back right after. */
    __asm__ volatile("lgdt %0\n\t"
                     "lidt %1\n\t"
                     "pushq %[cs]\n\t"
                     "leaq 1f(%%rip), %%rax\n\t"
                     "pushq %%rax\n\t"
                     "lretq\n"
                     "1:\n\t"
                     "movl %[ds], %%eax\n\t"
                     "movl %%eax, %%ss\n\t"
                     "movl %[udata], %%eax\n\t"
                     "movl %%eax, %%ds\n\t"
                     "movl %%eax, %%es\n\t"
                     "movl %%eax, %%gs\n\t"
                     "movl %[teb], %%eax\n\t"
                     "movl %%eax, %%fs\n\t"
                     "movl %[tr], %%eax\n\t"
                     "ltr %%ax"
                     :
                     : "m"(gdtr), "m"(idtr), [cs] "i"(KGDT64_R0_CODE), [ds] "i"(KGDT64_R0_DATA),
                       [udata] "i"(KGDT64_R3_DATA | RPL_USER), [teb] "i"(KGDT64_R3_CMTEB | RPL_USER),
                       [tr] "i"(KGDT64_SYS_TSS)
                     : "rax", "memory");
    KiWriteMsr(MSR_GS_BASE, (ULONG64)pcr);
    KiWriteMsr(MSR_KERNEL_GS_BASE, 0);

    /* syscall enters at KiSystemCall64 with interrupts, tracing, direction and alignment checks off. */
    KiWriteMsr(MSR_STAR, (ULONG64)((KGDT64_R3_CMCODE | RPL_USER) << 16 | KGDT64_R0_CODE) << 32);
    KiWriteMsr(MSR_LSTAR, (ULONG64)KiSystemCall64);
    KiWriteMsr(MSR_CSTAR, (ULONG64)KiSystemCall32);
    KiWriteMsr(MSR_FMASK, 0x47700);
    /* the default PAT, with write combining at index 4. */
    KiWriteMsr(MSR_PAT, 0x0007040100070406ull);
}

/*
 * a new thread's switch frame returns into KiThreadStartup. a user thread
 * keeps its extended state at the top of its kernel stack, above its trap frame.
 */
void KiInitializeSwitchFrame(PKTHREAD thread, PKSTART_ROUTINE routine, PVOID context)
{
    PUCHAR top = thread->InitialStack;
    if (!((PEPROCESS)thread->Process)->SystemProcess) {
        top = (PUCHAR)(((ULONG_PTR)top - KiXsaveSize) & ~(ULONG_PTR)63);
        memset(top, 0, KiXsaveSize);
        *(ULONG *)(top + XSAVE_LEGACY_MXCSR) = MXCSR_DEFAULT;
        *(USHORT *)top = 0x27F; /* the x87 control word windows starts threads with */
        thread->XSaveArea = top;
        thread->InitialStack = top;
    }
    PUCHAR trap = (PUCHAR)((ULONG_PTR)(top - KTRAP_FRAME_SIZE) & ~(ULONG_PTR)15);
    ULONG64 *frame = (ULONG64 *)(trap - KSWITCH_FRAME_SIZE - 8);
    memset(frame, 0, KSWITCH_FRAME_SIZE + 8);
    frame[0] = (ULONG64)routine;                       /* rbx */
    frame[3] = (ULONG64)context;                       /* rsi */
    frame[KSWITCH_RETURN / 8] = (ULONG64)KiThreadStartup;
    thread->KernelSp = (ULONG64)frame;
    thread->TrapFrame = (PKTRAP_FRAME)trap;
}

/* 32-bit code leaves the upper half of rsp undefined. */
static ULONG64 user_stack(const KTRAP_FRAME *frame)
{
    return frame->Cs == (KGDT64_R3_CMCODE | RPL_USER) ? (ULONG)frame->Rsp : frame->Rsp;
}

static void user_segments(PKTRAP_FRAME frame)
{
    frame->Cs = KGDT64_R3_CODE | RPL_USER;
    frame->Ss = KGDT64_R3_DATA | RPL_USER;
}

/* the code starts as if called on the caller's sp, with a return address slot pushed. */
void KiInitializeUserFrame(PKTRAP_FRAME frame, ULONG64 pc, ULONG64 sp, ULONG64 arg0, ULONG64 arg1, ULONG64 teb)
{
    memset(frame, 0, sizeof(*frame));
    frame->Rip = pc;
    frame->Rsp = sp - 8;
    frame->Rcx = arg0;
    frame->Rdx = arg1;
    frame->Rflags = EFLAGS_IF | 0x2;
    frame->MxCsr = MXCSR_DEFAULT;
    frame->Flags = KTRAP_FLAG_FP_SAVED; /* start with zeroed registers */
    frame->Vector = KTRAP_VECTOR_SYSCALL;
    user_segments(frame);
}

void KiInitializeUserContext(PCONTEXT context, ULONG64 pc, ULONG64 sp, ULONG64 arg0, ULONG64 arg1, ULONG64 teb)
{
    memset(context, 0, sizeof(*context));
    context->ContextFlags = CONTEXT_FULL;
    context->Rip = pc;
    context->Rsp = sp - 8;
    context->Rcx = arg0;
    context->Rdx = arg1;
    context->EFlags = EFLAGS_IF | 0x2;
    context->MxCsr = MXCSR_DEFAULT;
    context->FltSave.MxCsr = MXCSR_DEFAULT;
    context->FltSave.ControlWord = 0x27F;
    context->SegCs = KGDT64_R3_CODE | RPL_USER;
    context->SegSs = context->SegDs = context->SegEs = context->SegGs = KGDT64_R3_DATA | RPL_USER;
    context->SegFs = KGDT64_R3_CMTEB | RPL_USER;
}

void KiTrapFrameToContext(const KTRAP_FRAME *frame, CONTEXT *ctx, ULONG flags)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->ContextFlags = flags;
    ctx->Rax = frame->Rax;
    ctx->Rcx = frame->Rcx;
    ctx->Rdx = frame->Rdx;
    ctx->Rbx = frame->Rbx;
    ctx->Rsp = frame->Rsp;
    ctx->Rbp = frame->Rbp;
    ctx->Rsi = frame->Rsi;
    ctx->Rdi = frame->Rdi;
    ctx->R8 = frame->R8;
    ctx->R9 = frame->R9;
    ctx->R10 = frame->R10;
    ctx->R11 = frame->R11;
    ctx->R12 = frame->R12;
    ctx->R13 = frame->R13;
    ctx->R14 = frame->R14;
    ctx->R15 = frame->R15;
    ctx->Rip = frame->Rip;
    ctx->EFlags = (ULONG)frame->Rflags;
    ctx->SegCs = (USHORT)frame->Cs;
    ctx->SegSs = (USHORT)frame->Ss;
    ctx->SegDs = ctx->SegEs = ctx->SegGs = KGDT64_R3_DATA | RPL_USER;
    ctx->SegFs = KGDT64_R3_CMTEB | RPL_USER;
    ctx->MxCsr = frame->MxCsr;
    ctx->FltSave.MxCsr = frame->MxCsr;
    ctx->FltSave.MxCsr_Mask = 0xFFFF;
    ctx->FltSave.ControlWord = 0x27F;
    if (frame->Flags & KTRAP_FLAG_FP_SAVED)
        memcpy(ctx->FltSave.XmmRegisters, frame->Xmm, sizeof(frame->Xmm));
}

/* copies the parts of a context the flags name into another. */
void KiMergeContext(CONTEXT *to, const CONTEXT *from, ULONG flags)
{
    if ((flags & CONTEXT_CONTROL) == CONTEXT_CONTROL) {
        to->Rip = from->Rip;
        to->Rsp = from->Rsp;
        to->EFlags = from->EFlags;
        to->SegCs = from->SegCs;
        to->SegSs = from->SegSs;
    }
    if ((flags & CONTEXT_INTEGER) == CONTEXT_INTEGER) {
        to->Rax = from->Rax;
        to->Rcx = from->Rcx;
        to->Rdx = from->Rdx;
        to->Rbx = from->Rbx;
        to->Rbp = from->Rbp;
        to->Rsi = from->Rsi;
        to->Rdi = from->Rdi;
        to->R8 = from->R8;
        to->R9 = from->R9;
        to->R10 = from->R10;
        to->R11 = from->R11;
        to->R12 = from->R12;
        to->R13 = from->R13;
        to->R14 = from->R14;
        to->R15 = from->R15;
    }
    if ((flags & CONTEXT_SEGMENTS) == CONTEXT_SEGMENTS) {
        to->SegDs = from->SegDs;
        to->SegEs = from->SegEs;
        to->SegFs = from->SegFs;
        to->SegGs = from->SegGs;
    }
    if ((flags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT) {
        to->MxCsr = from->MxCsr;
        to->FltSave = from->FltSave;
    }
    if ((flags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS) {
        to->Dr0 = from->Dr0;
        to->Dr1 = from->Dr1;
        to->Dr2 = from->Dr2;
        to->Dr3 = from->Dr3;
        to->Dr6 = from->Dr6;
        to->Dr7 = from->Dr7;
    }
    to->ContextFlags = flags;
}

void KiContextToTrapFrame(const CONTEXT *ctx, PKTRAP_FRAME frame, ULONG flags, KPROCESSOR_MODE mode)
{
    if ((flags & CONTEXT_INTEGER) == CONTEXT_INTEGER) {
        frame->Rax = ctx->Rax;
        frame->Rcx = ctx->Rcx;
        frame->Rdx = ctx->Rdx;
        frame->Rbx = ctx->Rbx;
        frame->Rbp = ctx->Rbp;
        frame->Rsi = ctx->Rsi;
        frame->Rdi = ctx->Rdi;
        frame->R8 = ctx->R8;
        frame->R9 = ctx->R9;
        frame->R10 = ctx->R10;
        frame->R11 = ctx->R11;
        frame->R12 = ctx->R12;
        frame->R13 = ctx->R13;
        frame->R14 = ctx->R14;
        frame->R15 = ctx->R15;
    }
    if ((flags & CONTEXT_CONTROL) == CONTEXT_CONTROL) {
        frame->Rip = ctx->Rip;
        frame->Rsp = ctx->Rsp;
        if (mode == UserMode) {
            /* user mode may change only the arithmetic and trace flags, and stays in user mode, 64 or 32-bit. */
            frame->Rflags = (ctx->EFlags & EFLAGS_USER_MASK) | EFLAGS_IF | 0x2;
            user_segments(frame);
            if (ctx->SegCs == (KGDT64_R3_CMCODE | RPL_USER))
                frame->Cs = KGDT64_R3_CMCODE | RPL_USER;
        } else {
            frame->Rflags = ctx->EFlags;
            frame->Cs = ctx->SegCs;
            frame->Ss = ctx->SegSs;
        }
    }
    if ((flags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT) {
        memcpy(frame->Xmm, ctx->FltSave.XmmRegisters, sizeof(frame->Xmm));
        frame->MxCsr = ctx->MxCsr & 0xFFFF;
        frame->Flags |= KTRAP_FLAG_FP_SAVED;
    }
}

/*
 * like on windows, the user apc dispatcher finds the interrupted CONTEXT at its
 * sp, the routine and arguments in the home slots, then a machine frame for unwinding.
 */
BOOLEAN KiSetupUserApc(PKTRAP_FRAME frame, PVOID dispatcher, PKNORMAL_ROUTINE routine, PVOID context, PVOID arg1,
                       PVOID arg2)
{
    CONTEXT ctx;
    KiTrapFrameToContext(frame, &ctx, CONTEXT_FULL);
    ctx.P1Home = (ULONG64)context;
    ctx.P2Home = (ULONG64)arg1;
    ctx.P3Home = (ULONG64)arg2;
    ctx.P4Home = (ULONG64)routine;
    ULONG64 machine = ((user_stack(frame) - 0x80) & ~15ull) - 0x30;
    ULONG64 sp = machine - sizeof(CONTEXT);
    ULONG64 mframe[5] = {frame->Rip, frame->Cs, frame->Rflags, frame->Rsp, frame->Ss};
    if (!NT_SUCCESS(KiCopyToUser((PVOID)machine, mframe, sizeof(mframe))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)sp, &ctx, sizeof(ctx))))
        return FALSE;
    frame->Rsp = sp;
    frame->Rip = (ULONG64)dispatcher;
    user_segments(frame);
    /* like KiInitializeUserApc, the apc dispatcher is reached through the instrumentation callback. */
    KiCurrentThread()->InstrumentReturn = TRUE;
    return TRUE;
}

/*
 * windows' layout for KiUserExceptionDispatcher: at its sp the CONTEXT and its CONTEXT_EX, the EXCEPTION_RECORD at
 * 0x4F0, then at 0x590 a machine frame with the faulting rip and rsp, as its unwind codes describe. it takes nothing
 * in registers.
 */
BOOLEAN KiSetupUserException(PKTRAP_FRAME frame, PVOID dispatcher, PEXCEPTION_RECORD record)
{
    CONTEXT ctx;
    KiTrapFrameToContext(frame, &ctx, CONTEXT_FULL);
    ULONG64 machine = ((user_stack(frame) - 0x80) & ~15ull) - 0x30;
    ULONG64 ctx_addr = machine - 0x590;
    ULONG64 rec = ctx_addr + 0x4F0;
    MIME_STATIC_ASSERT(sizeof(EXCEPTION_RECORD) <= 0xA0 && sizeof(CONTEXT) == 0x4D0, "dispatcher layout");
    /* CONTEXT_EX: the whole and the legacy part start at the context, there is no extended state. */
    LONG context_ex[6] = {-(LONG)sizeof(CONTEXT), (LONG)sizeof(CONTEXT) + 0x18, -(LONG)sizeof(CONTEXT),
                          (LONG)sizeof(CONTEXT), 0x18, 0};
    ULONG64 mframe[5] = {frame->Rip, frame->Cs, frame->Rflags, frame->Rsp, frame->Ss};
    if (!NT_SUCCESS(KiCopyToUser((PVOID)machine, mframe, sizeof(mframe))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)rec, record, sizeof(*record))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)(ctx_addr + sizeof(CONTEXT)), context_ex, sizeof(context_ex))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)ctx_addr, &ctx, sizeof(ctx))))
        return FALSE;
    frame->Rsp = ctx_addr;
    /* the dispatcher runs without the trap flag, the context keeps it. */
    frame->Rflags &= ~0x100ull;
    frame->Rip = (ULONG64)dispatcher;
    KiCurrentThread()->InstrumentReturn = TRUE;
    user_segments(frame);
    return TRUE;
}

/*
 * the layout of windows' KiUserCallbackDispatcher: from its sp, the home slots, the api index and the arguments with
 * their length, then a machine frame with the rip and rsp of the system call, which its unwind codes describe. the
 * registers carry the arguments too, for xta64's way in.
 */
BOOLEAN KiSetupUserCallback(PKTRAP_FRAME frame, const KTRAP_FRAME *trap, PVOID dispatcher, ULONG api,
                            const void *input, ULONG input_length)
{
    ULONG64 args = (trap->Rsp - 0x80 - input_length) & ~15ull;
    if (input_length && !NT_SUCCESS(KiCopyToUser((PVOID)args, input, input_length)))
        return FALSE;
    ULONG64 machine = args - 0x30;
    ULONG64 sp = machine - 0x30;
    ULONG64 mframe[5] = {trap->Rip, trap->Cs, trap->Rflags, trap->Rsp, trap->Ss};
    ULONG64 home[6] = {0, 0, 0, 0, args, (ULONG64)input_length | (ULONG64)api << 32};
    if (!NT_SUCCESS(KiCopyToUser((PVOID)machine, mframe, sizeof(mframe))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)sp, home, sizeof(home))))
        return FALSE;
    memset(frame, 0, sizeof(*frame));
    frame->Rip = (ULONG64)dispatcher;
    frame->Rsp = sp;
    frame->Rcx = api;
    frame->Rdx = args;
    frame->R8 = input_length;
    frame->Rflags = EFLAGS_IF | 0x2;
    frame->MxCsr = trap->MxCsr;
    frame->Vector = KTRAP_VECTOR_SYSCALL;
    user_segments(frame);
    return TRUE;
}

void KiDumpTrapFrame(const char *what, const KTRAP_FRAME *frame)
{
    KiLogPrintf("%s: rip %p rsp %p cr2 %p vector %llx error %llx rflags %llx cs %llx", what, (PVOID)frame->Rip,
                (PVOID)frame->Rsp, (PVOID)frame->Cr2, frame->Vector, frame->ErrorCode, frame->Rflags, frame->Cs);
    KiLogPrintf("  rax %016llx rbx %016llx rcx %016llx rdx %016llx", frame->Rax, frame->Rbx, frame->Rcx, frame->Rdx);
    KiLogPrintf("  rsi %016llx rdi %016llx rbp %016llx r8  %016llx", frame->Rsi, frame->Rdi, frame->Rbp, frame->R8);
    KiLogPrintf("  r9  %016llx r10 %016llx r11 %016llx r12 %016llx", frame->R9, frame->R10, frame->R11, frame->R12);
    KiLogPrintf("  r13 %016llx r14 %016llx r15 %016llx", frame->R13, frame->R14, frame->R15);
    /* a call through a bad pointer left its return address at the stack top. */
    ULONG64 sp = frame->Rsp;
    if (sp && !(sp & 7) && MmIsAddressValid((PVOID)sp) && MmIsAddressValid((PVOID)(sp + 24))) {
        ULONG64 *top = (ULONG64 *)sp;
        KiLogPrintf("  stack %016llx %016llx %016llx %016llx", top[0], top[1], top[2], top[3]);
    }
    /* the frame pointer chain, when the code keeps one. */
    ULONG64 fp = frame->Rbp;
    for (int depth = 0; depth < 16 && fp && (fp & 7) == 0; depth++) {
        if (!MmIsAddressValid((PVOID)fp) || !MmIsAddressValid((PVOID)(fp + 8)))
            break;
        ULONG64 *rec = (ULONG64 *)fp;
        KiLogPrintf("  frame %d: %p", depth, (PVOID)rec[1]);
        fp = rec[0];
    }
}
