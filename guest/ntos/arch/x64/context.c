/*
 * the x64 half of threads and contexts: descriptor tables, the system
 * call registers, the extended state, switch frames, user mode entry
 * frames and the CONTEXT conversions.
 */
#include "../../ntos.h"

void KiSwapContextInternal(ULONG64 *old_sp, ULONG64 new_sp, PVOID old_xsave, PVOID new_xsave);
void KiSystemCall64(void);
void KiSystemCall32(void);
extern UCHAR KiIsrBase[];

ULONG KiXsaveSize;
ULONG64 KiXsaveMask;

static ULONG64 g_idt[512];
static DECLSPEC_ALIGN(16) UCHAR g_fault_stacks[MAXIMUM_PROCESSORS][8192];

#define CR4_PGE (1ull << 7)
#define CR4_OSFXSR (1ull << 9)
#define CR4_OSXMMEXCPT (1ull << 10)
#define CR4_OSXSAVE (1ull << 18)
#define CR4_SMEP (1ull << 20)

/* the fixed part of the user mode extended state, see the CONTEXT and XSAVE_FORMAT layouts. */
#define XSAVE_HEADER 512
#define XSAVE_LEGACY_MXCSR 24

void KiSwitchAddressSpace(PKPROCESS process)
{
    if (KiReadCr3() != process->DirectoryTableBase)
        KiWriteCr3(process->DirectoryTableBase);
}

/* fs of a wow64 thread in 32-bit code is its 32-bit teb, through the segment and its loaded base. */
static void set_teb32(ULONG_PTR teb32)
{
    ULONG64 *entry = &KeGetPcr()->GdtBase[KGDT64_R3_CMTEB / 8];
    *entry = (*entry & ~(0xFFFFFFull << 16 | 0xFFull << 56)) | (teb32 & 0xFFFFFF) << 16 | ((teb32 >> 24) & 0xFF) << 56;
    KiWriteMsr(MSR_FS_BASE, teb32);
}

void KiSwapContext(PKTHREAD old, PKTHREAD next)
{
    /* the teb of the thread is gs in user mode, swapped in at each return there. */
    if (next->Teb)
        KiWriteMsr(MSR_KERNEL_GS_BASE, (ULONG64)next->Teb);
    if (next->Teb32)
        set_teb32((ULONG_PTR)next->Teb32);
    KiSwapContextInternal(&old->KernelSp, next->KernelSp, old->XSaveArea, next->XSaveArea);
}

/*
 * hlt only ends on an interrupt taken with interrupts enabled. the entry
 * of an interrupt that finds IdleSleeping takes the lock back before
 * handling it, so interrupts still run with the lock held.
 */
void KiIdleWait(void)
{
    PKPCR pcr = KeGetPcr();
    pcr->IdleSleeping = 1;
    KiReleaseKernelLock();
    __asm__ volatile("sti; hlt; cli" ::: "memory");
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
}

static ULONG64 segment(ULONG64 access, ULONG64 flags)
{
    return 0xFFFFull | access << 40 | 0xFull << 48 | flags << 52;
}

/* the extended state components the kernel saves for user threads: x87, sse, avx and avx-512. */
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

/* descriptor tables, system call registers and control bits of the running processor. */
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

    /* the windows layout: kernel code and data, 32-bit and 64-bit user code, user data, the tss. */
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
    /* loading gs zeroes its base, which the kernel needs: it is written back right after. */
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
    /* the default attributes, with write combining at index 4. */
    KiWriteMsr(MSR_PAT, 0x0007040100070406ull);
}

/*
 * the switch frame of a new thread returns into KiThreadStartup. a user
 * thread keeps its extended state at the top of its kernel stack, above
 * its trap frame.
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
    frame[KSWITCH_RETURN / 8] = (ULONG64)KiThreadStartup; /* return address */
    thread->KernelSp = (ULONG64)frame;
    thread->TrapFrame = (PKTRAP_FRAME)trap;
}

/* the user stack pointer; 32-bit code leaves the upper half of rsp undefined. */
static ULONG64 user_stack(const KTRAP_FRAME *frame)
{
    return frame->Cs == (KGDT64_R3_CMCODE | RPL_USER) ? (ULONG)frame->Rsp : frame->Rsp;
}

static void user_segments(PKTRAP_FRAME frame)
{
    frame->Cs = KGDT64_R3_CODE | RPL_USER;
    frame->Ss = KGDT64_R3_DATA | RPL_USER;
}

/* sp is where the caller left its stack: the code starts as if called, with a return address slot pushed. */
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
            /* user mode may only change the arithmetic and trace flags, and stays in user mode, 64 or 32-bit. */
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
 * the user apc dispatcher finds the interrupted CONTEXT at its stack
 * pointer, the routine and its arguments in the home slots, as on windows,
 * then a machine frame its unwind codes describe.
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
    return TRUE;
}

/*
 * the layout KiUserExceptionDispatcher's unwind codes describe: from its
 * stack pointer, 0x28 bytes, the context, the record, then a machine frame
 * with the faulting rip and rsp, so unwinding goes on there.
 */
BOOLEAN KiSetupUserException(PKTRAP_FRAME frame, PVOID dispatcher, PEXCEPTION_RECORD record)
{
    CONTEXT ctx;
    KiTrapFrameToContext(frame, &ctx, CONTEXT_FULL);
    ULONG64 machine = ((user_stack(frame) - 0x80) & ~15ull) - 0x30;
    ULONG64 rec = machine - 0xA0;
    ULONG64 ctx_addr = rec - sizeof(CONTEXT);
    MIME_STATIC_ASSERT(sizeof(EXCEPTION_RECORD) <= 0xA0 && sizeof(CONTEXT) == 0x4D0, "dispatcher layout");
    ULONG64 mframe[5] = {frame->Rip, frame->Cs, frame->Rflags, frame->Rsp, frame->Ss};
    if (!NT_SUCCESS(KiCopyToUser((PVOID)machine, mframe, sizeof(mframe))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)rec, record, sizeof(*record))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)ctx_addr, &ctx, sizeof(ctx))))
        return FALSE;
    frame->Rsp = ctx_addr - 0x30;
    frame->Rcx = rec;
    frame->Rdx = ctx_addr;
    frame->Rip = (ULONG64)dispatcher;
    user_segments(frame);
    return TRUE;
}

/* KiUserCallbackDispatcher(api, arguments, length), entered as if called, below the arguments. */
BOOLEAN KiSetupUserCallback(PKTRAP_FRAME frame, const KTRAP_FRAME *trap, PVOID dispatcher, ULONG api,
                            const void *input, ULONG input_length)
{
    ULONG64 args = (trap->Rsp - 0x80 - input_length) & ~15ull;
    if (input_length && !NT_SUCCESS(KiCopyToUser((PVOID)args, input, input_length)))
        return FALSE;
    ULONG64 sp = args - 0x30;
    ULONG64 zero = 0;
    if (!NT_SUCCESS(KiCopyToUser((PVOID)(sp - 8), &zero, sizeof(zero))))
        return FALSE;
    memset(frame, 0, sizeof(*frame));
    frame->Rip = (ULONG64)dispatcher;
    frame->Rsp = sp - 8;
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
    /* the top of the stack, where a call through a bad pointer left its return address. */
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
