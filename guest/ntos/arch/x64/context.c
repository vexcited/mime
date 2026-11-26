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
/* the bits of mxcsr the processor has, as fxsave reports them. */
ULONG KiMxcsrMask = 0xFFBF;
static ULONG g_xsave_exact, g_xstate_size;
static BOOLEAN g_xsave_compact;
static struct {
    ULONG Standard, User, Size;
    BOOLEAN Aligned;
} g_xfeature[64];
/*
 * the supervisor state windows 11 enables and shows to user mode: the user shadow stack, feature 11. no thread has a
 * shadow stack here, so the state is its initial one, sixteen zero bytes, and the system never saves it.
 */
static ULONG64 g_xsave_supervisor;
#define MSR_XSS 0xDA0
#define XFEATURE_CET_USER 11

static ULONG64 g_idt[512];
static DECLSPEC_ALIGN(16) UCHAR g_fault_stacks[MAXIMUM_PROCESSORS][8192];
/* the stack of the debug trap: it can arrive on the first instruction of a kernel entry, see KiDebugTrap. */
static DECLSPEC_ALIGN(16) UCHAR g_debug_stacks[MAXIMUM_PROCESSORS][1024];
extern UCHAR KiDebugTrap[];

#define CR4_PGE (1ull << 7)
#define CR4_OSFXSR (1ull << 9)
#define CR4_OSXMMEXCPT (1ull << 10)
#define CR4_OSXSAVE (1ull << 18)
#define CR4_SMEP (1ull << 20)
#define CR4_FSGSBASE (1ull << 16)

/* the legacy area of the user extended state, as in XSAVE_FORMAT. */
#define XSAVE_HEADER 512
#define XSAVE_LEGACY_MXCSR 24

void KiSwitchAddressSpace(PKPROCESS process)
{
    if (KiReadCr3() == process->DirectoryTableBase)
        return;
    /* a root without the kernel's half leaves the next fault nowhere to go and the processor resets: stop before. */
    ULONG kernel = (MIME_KVA_KERNEL >> 39) & 0x1FF;
    if (((ULONG64 *)HalPhysmap(process->DirectoryTableBase))[kernel] != MiKernelRoot()[kernel])
        KeBugCheckEx(0x0000001A, process->DirectoryTableBase, (ULONG_PTR)process,
                     (ULONG_PTR)__builtin_return_address(0), 0); /* MEMORY_MANAGEMENT */
    KiWriteCr3(process->DirectoryTableBase);
}

/* the base of fs, read without an exit where rdfsbase is allowed: the msr is only written when it changes. */
static BOOLEAN g_fsgsbase;

static void set_fs_base(ULONG64 base)
{
    ULONG64 now = ~base;
    if (g_fsgsbase)
        __asm__ volatile("rdfsbase %0" : "=r"(now));
    if (now != base)
        KiWriteMsr(MSR_FS_BASE, base);
}

/* in 32-bit code, fs of a wow64 thread points at its 32-bit teb. */
static void set_teb32(ULONG_PTR teb32)
{
    ULONG64 *entry = &KeGetPcr()->GdtBase[KGDT64_R3_CMTEB / 8];
    ULONG64 value = (*entry & ~(0xFFFFFFull << 16 | 0xFFull << 56)) | (teb32 & 0xFFFFFF) << 16 | ((teb32 >> 24) & 0xFF) << 56;
    if (*entry != value)
        *entry = value;
    set_fs_base(teb32);
}

/*
 * dr7, as windows 11 26300 keeps it: the local enables of the four breakpoints, the exact enables and the conditions
 * and lengths, never the global enables or general detect. the breakpoints are on when an enable is left.
 */
#define DR7_USER_MASK 0xFFFF0355ull
#define DR7_ACTIVE 0x355ull
#define DR6_RESET 0xFFFF0FF0ull
/* the highest address a program has, 64 KiB under the end of user space. */
#define DR_HIGHEST_ADDRESS 0x7FFFFFFEFFFFull

static void load_debug(PKTHREAD thread)
{
    ULONG64 dr7 = thread->UserDr7 & DR7_ACTIVE ? thread->UserDr7 : 0;
    __asm__ volatile("mov %0, %%dr7" ::"r"(0ull));
    if (dr7) {
        __asm__ volatile("mov %0, %%dr0" ::"r"(thread->UserDr[0]));
        __asm__ volatile("mov %0, %%dr1" ::"r"(thread->UserDr[1]));
        __asm__ volatile("mov %0, %%dr2" ::"r"(thread->UserDr[2]));
        __asm__ volatile("mov %0, %%dr3" ::"r"(thread->UserDr[3]));
        __asm__ volatile("mov %0, %%dr7" ::"r"(dr7));
        /* from here the thread reads them as the processor holds them. */
        if (!thread->UserDrLoaded) {
            thread->UserDrLoaded = TRUE;
            thread->UserDr6 = DR6_RESET;
        }
    }
}

/*
 * like windows, a context's breakpoints name user addresses only, others become 0, and dr7 keeps what user mode may
 * use. a dr7 that leaves no breakpoint on drops them all, and the dr6 of the context is not taken: it is the
 * processor's. the thread loads them when it runs, at once when it is the caller.
 */
void KiSetThreadDebug(PKTHREAD thread, const CONTEXT *ctx)
{
    const ULONG64 dr[4] = {ctx->Dr0, ctx->Dr1, ctx->Dr2, ctx->Dr3};
    ULONG64 dr7 = ctx->Dr7 & DR7_USER_MASK;
    BOOLEAN active = (dr7 & DR7_ACTIVE) != 0;
    for (int i = 0; i < 4; i++)
        thread->UserDr[i] = active && dr[i] <= DR_HIGHEST_ADDRESS ? dr[i] : 0;
    thread->UserDr6 = 0;
    thread->UserDr7 = active ? dr7 : 0;
    thread->UserDrLoaded = FALSE;
    if (thread == KiCurrentThread())
        load_debug(thread);
}

/*
 * a thread without breakpoints reads zeroes. one with them reads what was set until it runs with them: from then
 * dr6 and dr7 are as the processor holds them, dr7 with bit 10 set.
 */
void KiGetThreadDebug(PKTHREAD thread, CONTEXT *ctx)
{
    BOOLEAN active = (thread->UserDr7 & DR7_ACTIVE) != 0;
    ctx->Dr0 = active ? thread->UserDr[0] : 0;
    ctx->Dr1 = active ? thread->UserDr[1] : 0;
    ctx->Dr2 = active ? thread->UserDr[2] : 0;
    ctx->Dr3 = active ? thread->UserDr[3] : 0;
    ctx->Dr6 = active ? thread->UserDr6 : 0;
    ctx->Dr7 = active ? thread->UserDr7 | (thread->UserDrLoaded ? 0x400 : 0) : 0;
}

/*
 * the status of the breakpoints at an exception, read from the processor and reset there. the processor never
 * clears it, so what one exception left would show in the next.
 */
void KiCaptureDebugStatus(PKTHREAD thread)
{
    ULONG64 dr6;
    __asm__ volatile("mov %%dr6, %0" : "=r"(dr6));
    __asm__ volatile("mov %0, %%dr6" ::"r"(DR6_RESET));
    thread->UserDr6 = thread->UserDr7 & DR7_ACTIVE ? dr6 : 0;
}

/*
 * a load of ss holds a data breakpoint it hits back, with the single step, until the instruction after it is done, and
 * the processor then reports both. when that instruction is a cpuid the hypervisor runs it and raises the step itself:
 * kvm reports the step alone and the breakpoint the processor held is lost. it is put back here, from the instructions
 * before the trap. measured on windows: behind a mov to ss a nop or an rdtsc gives both bits, a cpuid the step only.
 */
void KiRestoreHeldBreakpoint(PKTHREAD thread, PKTRAP_FRAME frame)
{
    enum { WINDOW = 16 };
    ULONG64 dr7 = thread->UserDr7;
    UCHAR code[WINDOW] = {};
    if (!(thread->UserDr6 & 0x4000) || !(dr7 & DR7_ACTIVE) || frame->Rip < 0x10000 + WINDOW)
        return;
    /* the bytes before the trap may start at the first of a page. */
    int have = WINDOW;
    while (have >= 4 && !NT_SUCCESS(KiCopyFromUser(code + WINDOW - have, (PVOID)(frame->Rip - have), have)))
        have--;
    if (have < 4 || code[WINDOW - 2] != 0x0F || code[WINDOW - 1] != 0xA2)
        return;
    /* the mov to ss from memory that ends where the cpuid starts, whatever its prefixes and its address form. */
    const ULONG64 registers[16] = {frame->Rax, frame->Rcx, frame->Rdx, frame->Rbx, frame->Rsp, frame->Rbp,
                                   frame->Rsi, frame->Rdi, frame->R8,  frame->R9,  frame->R10, frame->R11,
                                   frame->R12, frame->R13, frame->R14, frame->R15};
    BOOLEAN found = FALSE, known = FALSE;
    ULONG64 address = 0;
    for (int start = WINDOW - 4; start >= WINDOW - have && !found; start--) {
        int at = start;
        UCHAR rex = 0;
        BOOLEAN based = FALSE;
        while (at < WINDOW - 2 && (code[at] == 0x66 || code[at] == 0x2E || code[at] == 0x36 || code[at] == 0x3E ||
                                   code[at] == 0x26 || code[at] == 0x64 || code[at] == 0x65)) {
            based |= code[at] == 0x64 || code[at] == 0x65;
            at++;
        }
        if (at < WINDOW - 2 && (code[at] & 0xF0) == 0x40)
            rex = code[at++];
        if (at + 2 > WINDOW - 2 || code[at] != 0x8E)
            continue;
        UCHAR modrm = code[at + 1], mod = modrm >> 6, rm = modrm & 7;
        if (mod == 3 || (modrm >> 3 & 7) != 2)
            continue;
        at += 2;
        /* cpuid wrote over rax, rbx, rcx and rdx: an address made of one of them is not known any more. */
        BOOLEAN clobbered = based;
        ULONG64 ea = 0;
        int base = -1, index = -1, scale = 0;
        if (rm == 4) {
            if (at >= WINDOW - 2)
                continue;
            UCHAR sib = code[at++];
            scale = sib >> 6;
            index = (sib >> 3 & 7) | (rex & 2 ? 8 : 0);
            if (index == 4)
                index = -1;
            base = (sib & 7) | (rex & 1 ? 8 : 0);
            if ((sib & 7) == 5 && mod == 0)
                base = -1;
        } else if (rm == 5 && mod == 0) {
            base = -2; /* relative to the instruction after, the cpuid. */
        } else {
            base = rm | (rex & 1 ? 8 : 0);
        }
        int displacement = mod == 1 ? 1 : mod == 2 || base < 0 ? 4 : 0;
        if (at + displacement != WINDOW - 2)
            continue;
        if (displacement == 1)
            ea = (ULONG64)(LONG64)(CHAR)code[at];
        else if (displacement == 4)
            ea = (ULONG64)(LONG64)(LONG)(code[at] | code[at + 1] << 8 | code[at + 2] << 16 | (ULONG)code[at + 3] << 24);
        if (base == -2)
            ea += frame->Rip - 2;
        else if (base >= 0)
            ea += registers[base], clobbered |= base < 4;
        if (index >= 0)
            ea += registers[index] << scale, clobbered |= index < 4;
        found = TRUE;
        known = !clobbered;
        address = ea;
    }
    if (!found)
        return;
    for (int n = 0; n < 4; n++) {
        static const ULONG64 sizes[4] = {1, 2, 8, 4};
        if (!(dr7 >> (n * 2) & 3) || (dr7 >> (16 + n * 4) & 3) != 3)
            continue;
        ULONG64 size = sizes[dr7 >> (18 + n * 4) & 3], watched = thread->UserDr[n] & ~(size - 1);
        BOOLEAN hit = FALSE;
        if (known) {
            hit = address < watched + size && watched < address + 2;
        } else {
            /* the address is gone: the breakpoint was hit when the selector ss now holds is read under it. */
            for (ULONG64 from = watched ? watched - 1 : 0; from < watched + size && !hit; from++) {
                USHORT word;
                hit = NT_SUCCESS(KiCopyFromUser(&word, (PVOID)from, sizeof(word))) && word == (USHORT)frame->Ss;
            }
        }
        if (hit)
            thread->UserDr6 |= 1ull << n;
    }
}

/*
 * a service that returns with the trap flag in its frame, set before the syscall or by a context set meanwhile.
 * measured on windows 11 26300: the single step is raised at the address the service returns to, before any
 * instruction there runs, with the flag off in its context and the step bit in dr6. a return of the processor
 * would trap one instruction later, which is what NtContinue does, there and here.
 */
BOOLEAN KiServiceSingleStep(PKTHREAD thread, PKTRAP_FRAME frame, PEXCEPTION_RECORD record)
{
    if (!(frame->Rflags & 0x100))
        return FALSE;
    /* the step is the processor's, whose flags always hold bit 1, even when a context set them without. */
    frame->Rflags = (frame->Rflags & ~0x100ull) | 0x2;
    thread->UserDr6 = thread->UserDr7 & DR7_ACTIVE ? DR6_RESET | 0x4000 : 0;
    memset(record, 0, sizeof(*record));
    record->ExceptionCode = (NTSTATUS)0x80000004L;
    record->ExceptionAddress = (PVOID)frame->Rip;
    return TRUE;
}

void KiSwapContext(PKTHREAD old, PKTHREAD next)
{
    /* the processor holds the breakpoints of the thread that runs, and none for threads without. */
    if ((next->UserDr7 & DR7_ACTIVE) || (old->UserDr7 & DR7_ACTIVE))
        load_debug(next);
    /* in user mode gs is the teb, swapped in at each return there; a program may change it with wrgsbase. */
    if (old->Teb && next->Teb) {
        ULONG lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(MSR_KERNEL_GS_BASE));
        old->UserGsBase = (ULONG64)hi << 32 | lo;
    }
    if (next->Teb)
        KiWriteMsr(MSR_KERNEL_GS_BASE, next->UserGsBase >= 0x10000ull ? next->UserGsBase : (ULONG64)next->Teb);
    if (next->Teb32)
        set_teb32((ULONG_PTR)next->Teb32);
    else
        set_fs_base(0); /* 64-bit windows keeps the teb in gs and leaves fs zero. */
    KiSwapContextInternal(&old->KernelSp, next->KernelSp, old->XSaveArea, next->XSaveArea);
}

/*
 * an idle processor, which has no kernel lock, watches its work flags for a while. a waker that sees it
 * watching, see KiRequestDispatch, skips the interrupt, which would cost both sides exits to the host.
 * only then does it halt, and a halted processor is interrupted.
 *
 * how long it watches follows how its halts end, as linux's haltpoll governor does: see KiIdleHaltEnded.
 *
 * hlt ends only on an interrupt taken with interrupts enabled. the clock and a request from another processor
 * work under the dispatcher lock alone. any other interrupt finds IdleSleeping and takes the kernel lock first,
 * see KiTrapCommon, and the processor holds it after. none of them delivers dpcs or switches threads here, see
 * IdleWaiting: the idle loop does, once the wait is over.
 */
static ULONG64 g_idle_poll_shortest, g_idle_poll_longest;

/*
 * pause leaves the core to the other thread of it. the host counts the pauses that follow each other within
 * 128 cycles and, at 3000, takes the processor for one waiting on a lock, and looks for its holder to run
 * instead. rows of them with a gap between never get there.
 */
#define KI_IDLE_PAUSE_ROW 64
#define KI_IDLE_PAUSE_GAP 512

FORCEINLINE BOOLEAN idle_work(PKPCR pcr, PKPRCB prcb)
{
    return !pcr->IdleSleeping || prcb->DispatchPending || prcb->DpcPending;
}

void KiIdleWait(void)
{
    PKPCR pcr = KeGetPcr();
    PKPRCB prcb = pcr->CurrentPrcb;
    if (!prcb->IdlePoll) {
        g_idle_poll_shortest = HalBootInfo->timer_frequency / 80000; /* 12.5 microseconds */
        g_idle_poll_longest = HalBootInfo->timer_frequency / 5000;   /* 200 microseconds */
        prcb->IdlePoll = g_idle_poll_shortest;
    }
    ULONG64 poll = prcb->IdlePoll;
    pcr->IdleSleeping = 1;
    prcb->IdleWaiting = TRUE;
    if (poll)
        __atomic_store_n(&prcb->IdleState, KI_IDLE_POLLING, __ATOMIC_SEQ_CST);
    __asm__ volatile("sti" ::: "memory");
    ULONG64 since = HalReadCounter(), now = since;
    while (now - since < poll) {
        for (int i = 0; i < KI_IDLE_PAUSE_ROW && !idle_work(pcr, prcb); i++)
            __builtin_ia32_pause();
        ULONG64 gap = HalReadCounter();
        do
            now = HalReadCounter();
        while (now - gap < KI_IDLE_PAUSE_GAP && !idle_work(pcr, prcb));
        if (idle_work(pcr, prcb))
            break;
    }
    __asm__ volatile("cli" ::: "memory");
    if (!idle_work(pcr, prcb)) {
        /* published before looking again, so a request that came meanwhile is seen or sends the interrupt. */
        __atomic_store_n(&prcb->IdleState, KI_IDLE_HALTED, __ATOMIC_SEQ_CST);
        if (!prcb->DispatchPending && !prcb->DpcPending) {
            prcb->HaltedSince = since;
            __asm__ volatile("sti; hlt; cli" ::: "memory");
            prcb->HaltedSince = 0;
        }
    } else if (pcr->IdleSleeping) {
        prcb->IdlePolls++;
    }
    __atomic_store_n(&prcb->IdleState, KI_IDLE_RUNNING, __ATOMIC_SEQ_CST);
    prcb->IdleWaiting = FALSE;
    pcr->IdleSleeping = 0;
}

/*
 * an interrupt ended a halt. it is told here and not after the hlt: the interrupt may switch away from the idle
 * thread, which then only goes on the next time the processor has nothing to run.
 *
 * a request from another processor that came within the longest watch would have been seen without the
 * interrupt, had the processor watched on, so it watches for twice as long next time. a wait longer than that is
 * not worth watching through, and halves it.
 */
void KiIdleHaltEnded(PKPRCB prcb, ULONG64 now, BOOLEAN by_request)
{
    ULONG64 waited = now - prcb->HaltedSince, poll = prcb->IdlePoll;
    prcb->HaltedSince = 0;
    prcb->IdleHalts++;
    if (waited > g_idle_poll_longest)
        poll /= 2;
    else if (by_request)
        poll *= 2;
    if (poll < g_idle_poll_shortest)
        poll = g_idle_poll_shortest;
    if (poll > g_idle_poll_longest)
        poll = g_idle_poll_longest;
    prcb->IdlePoll = poll;
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
        /* int3, into, __fastfail, assertions and debug services come from user code. int 2e does not: windows opens it only
           when SharedUserData tells the stubs to use it, and a program that tries it takes an access violation. */
        if (v == 3 || v == 4 || v == 0x29 || v == 0x2C || v == 0x2D)
            dpl = 3;
        set_gate(v, KiIsrBase + v * 16, dpl, v == 8 || v == 2 || v == 18 ? 1 : 0);
    }
    set_gate(KI_VECTOR_FLUSH, KiFlushIpi, 0, 0);
    set_gate(1, KiDebugTrap, 0, 2);
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
    g_xsave_exact = regs[1];
    /* where each feature beyond sse lies: in the standard area the processor saves, and in the area a user context
       carries, which starts at the header and is compacted when the processor can compact. */
    ULONG sub[4];
    KiCpuid(0xD, 1, sub);
    g_xsave_compact = (sub[0] & 2) != 0;
    ULONG compact = sizeof(XSAVE_AREA_HEADER);
    for (ULONG i = 2; i < 64; i++) {
        if (!(KiXsaveMask >> i & 1))
            continue;
        ULONG f[4];
        KiCpuid(0xD, i, f);
        if (f[2] & 2)
            compact = (compact + 63) & ~63u;
        g_xfeature[i].Standard = f[1];
        g_xfeature[i].Size = f[0];
        g_xfeature[i].Aligned = (f[2] & 2) != 0;
        g_xfeature[i].User = g_xsave_compact ? compact : f[1] - XSAVE_HEADER;
        compact += f[0];
    }
    if (KiXsaveMask & ~3ull)
        g_xstate_size = g_xsave_compact ? compact : g_xsave_exact - XSAVE_HEADER;
    /* the supervisor state is enabled where the processor compacts and saves it, as windows does. */
    ULONG64 supervisor = (ULONG64)sub[3] << 32 | sub[2];
    if ((sub[0] & 8) && g_xsave_compact && (supervisor >> XFEATURE_CET_USER & 1)) {
        ULONG f[4];
        KiCpuid(0xD, XFEATURE_CET_USER, f);
        g_xfeature[XFEATURE_CET_USER].Size = f[0];
        g_xfeature[XFEATURE_CET_USER].Aligned = (f[2] & 2) != 0;
        g_xsave_supervisor = 1ull << XFEATURE_CET_USER;
        KiWriteMsr(MSR_XSS, g_xsave_supervisor);
    }
    static DECLSPEC_ALIGN(16) UCHAR legacy[512];
    __asm__ volatile("fxsave64 %0" : "=m"(legacy));
    if (*(ULONG *)(legacy + 0x1C))
        KiMxcsrMask = *(ULONG *)(legacy + 0x1C);
}

/* where a feature lies in an area laid out for a set of them, from the header: feature 64 is the end of them all. */
static ULONG xfeature_at(ULONG64 layout, ULONG feature)
{
    if (!g_xsave_compact)
        return feature < 64 ? g_xfeature[feature].Standard - XSAVE_HEADER : g_xsave_exact - XSAVE_HEADER;
    ULONG at = sizeof(XSAVE_AREA_HEADER);
    for (ULONG i = 2; i < 64; i++) {
        if (!(layout >> i & 1))
            continue;
        if (g_xfeature[i].Aligned)
            at = (at + 63) & ~63u;
        if (i == feature)
            return at;
        at += g_xfeature[i].Size;
    }
    return at;
}

/* what the shared page tells user mode of the extended state, the way windows 11 fills it from cpuid leaf 0xD. */
void KiSetExtendedState(XSTATE_CONFIGURATION *x)
{
    ULONG regs[4], sub[4];
    KiCpuid(0xD, 0, regs);
    KiCpuid(0xD, 1, sub);
    ULONG64 user = (ULONG64)regs[3] << 32 | regs[0], supervisor = (ULONG64)sub[3] << 32 | sub[2];
    x->EnabledFeatures = x->EnabledVolatileFeatures = KiXsaveMask;
    x->Size = g_xsave_exact;
    x->ControlFlags = sub[0] & 3;
    x->Features[0].Size = x->AllFeatures[0] = 0xA0;
    x->Features[1].Offset = 0xA0;
    x->Features[1].Size = x->AllFeatures[1] = 0x100;
    for (ULONG i = 2; i < 64; i++) {
        if (!((user | supervisor) >> i & 1))
            continue;
        ULONG f[4];
        KiCpuid(0xD, i, f);
        x->AllFeatures[i] = f[0];
        if (user >> i & 1) {
            x->Features[i].Offset = f[1];
            x->Features[i].Size = f[0];
        }
        if ((f[2] & 2) && (KiXsaveMask >> i & 1))
            x->AlignedFeatures |= 1ull << i;
    }
    x->EnabledSupervisorFeatures = x->EnabledUserVisibleSupervisorFeatures = g_xsave_supervisor;
    x->AllFeatureSize = x->AllNonLargeFeatureSize =
        g_xsave_compact ? XSAVE_HEADER + xfeature_at((KiXsaveMask & ~3ull) | g_xsave_supervisor, 64) : g_xsave_exact;
}

ULONG KiXstateSize(void)
{
    return g_xstate_size;
}

/* room for a standard area of the features beyond sse, holding what the processor has of them now when asked. */
static PUCHAR live_area(BOOLEAN save, PVOID *room)
{
    *room = ExAllocatePoolWithTag(NonPagedPool, KiXsaveSize + 64, 'tsXK');
    if (!*room)
        return nullptr;
    PUCHAR area = (PUCHAR)(((ULONG_PTR)*room + 63) & ~(ULONG_PTR)63);
    memset(area, 0, KiXsaveSize);
    if (save) {
        ULONG64 mask = KiXsaveMask & ~3ull;
        __asm__ volatile("xsave64 (%[area])" : : [area] "r"(area), "a"((ULONG)mask), "d"((ULONG)(mask >> 32)) : "memory");
    }
    return area;
}

/*
 * the extended state of a thread beyond sse, in the layout a user context carries: the features asked for that
 * the thread has in use, the others as zeroes. the caller's own is what the processor holds, another thread's is
 * what its last switch saved.
 */
void KiCaptureXstate(PKTHREAD thread, ULONG64 asked, PUCHAR out)
{
    memset(out, 0, g_xstate_size);
    if (!g_xstate_size)
        return;
    ULONG64 mask = KiXsaveMask & ~3ull;
    BOOLEAN live = thread == KiCurrentThread();
    PVOID room = nullptr;
    PUCHAR area = live ? live_area(TRUE, &room) : thread->XSaveArea;
    XSAVE_AREA_HEADER *header = (XSAVE_AREA_HEADER *)out;
    header->CompactionMask = g_xsave_compact ? 1ull << 63 | mask : 0;
    if (!area)
        return;
    header->Mask = *(ULONG64 *)(area + XSAVE_HEADER) & mask & asked;
    for (ULONG i = 2; i < 64; i++)
        if (header->Mask >> i & 1)
            memcpy(out + g_xfeature[i].User, area + g_xfeature[i].Standard, g_xfeature[i].Size);
    if (live)
        ExFreePool(room);
}

/* the reverse: the features the area holds are set, the others the system saves go back to their initial state. */
void KiRestoreXstate(PKTHREAD thread, const UCHAR *in)
{
    if (!g_xstate_size)
        return;
    ULONG64 mask = KiXsaveMask & ~3ull, held = ((const XSAVE_AREA_HEADER *)in)->Mask & mask;
    BOOLEAN live = thread == KiCurrentThread();
    PVOID room = nullptr;
    PUCHAR area = live ? live_area(FALSE, &room) : thread->XSaveArea;
    if (!area)
        return;
    ULONG64 *present = (ULONG64 *)(area + XSAVE_HEADER);
    *present = (*present & ~mask) | held;
    for (ULONG i = 2; i < 64; i++)
        if (held >> i & 1)
            memcpy(area + g_xfeature[i].Standard, in + g_xfeature[i].User, g_xfeature[i].Size);
    if (live) {
        /* the standard form reloads mxcsr with the avx state: give it the one in force. */
        __asm__ volatile("stmxcsr %[m]" : [m] "=m"(*(ULONG *)(area + XSAVE_LEGACY_MXCSR)));
        __asm__ volatile("xrstor64 (%[area])" : : [area] "r"(area), "a"((ULONG)mask), "d"((ULONG)(mask >> 32)) : "memory");
        ExFreePool(room);
    }
}

/*
 * the extended state area a user context names in the CONTEXT_EX behind it, as windows 11 26300 takes it. both chunks
 * start at the context, the legacy one holds it all, the area lies behind the CONTEXT_EX and inside the whole, on a 64
 * byte boundary, and has room for its header. the header names the layout: the compaction bit, enabled features alone,
 * and no feature in the mask that the layout lacks.
 */
NTSTATUS KiProbeUserXstate(PCONTEXT context, KI_USER_XSTATE *where)
{
    CONTEXT_EX ex;
    NTSTATUS status = KiCopyFromUser(&ex, context + 1, sizeof(ex));
    if (!NT_SUCCESS(status))
        return status;
    LONG legacy = -(LONG)sizeof(CONTEXT);
    ULONG all = (ULONG)ex.All.Length, length = (ULONG)ex.XState.Length;
    if (ex.All.Offset != legacy || ex.Legacy.Offset != legacy || (ULONG)ex.Legacy.Length < sizeof(CONTEXT) ||
        all < sizeof(CONTEXT) || ex.XState.Offset < 0x20 ||
        (ULONG64)ex.XState.Offset + length > all - sizeof(CONTEXT))
        return STATUS_INVALID_PARAMETER;
    where->Area = (PUCHAR)(context + 1) + ex.XState.Offset;
    where->Length = length;
    if ((ULONG_PTR)where->Area & 63)
        return STATUS_DATATYPE_MISALIGNMENT;
    if (length < sizeof(XSAVE_AREA_HEADER))
        return STATUS_BUFFER_OVERFLOW;
    XSAVE_AREA_HEADER header;
    status = KiCopyFromUser(&header, where->Area, sizeof(header));
    if (!NT_SUCCESS(status))
        return status;
    where->Mask = header.Mask;
    where->Layout = KiXsaveMask & ~3ull;
    if (g_xsave_compact) {
        ULONG64 held = header.CompactionMask & ~(1ull << 63);
        if (!(header.CompactionMask >> 63) || (held & ~(KiXsaveMask | g_xsave_supervisor)) ||
            (header.Mask & ~held & ~3ull))
            return STATUS_INVALID_PARAMETER;
        where->Layout = held & ~3ull;
    }
    return STATUS_SUCCESS;
}

/*
 * the shadow stack state is not a thread's to change: a context that names it must hold what the thread has, which
 * is the initial state, or windows denies the whole context.
 */
BOOLEAN KiUserXstateDenied(const KI_USER_XSTATE *where)
{
    for (ULONG i = 2; i < 64; i++) {
        if (!((where->Mask & where->Layout & g_xsave_supervisor) >> i & 1))
            continue;
        UCHAR held[64];
        ULONG at = xfeature_at(where->Layout, i), size = g_xfeature[i].Size;
        if (size > sizeof(held) || (ULONG64)at + size > where->Length)
            continue;
        if (!NT_SUCCESS(KiCopyFromUser(held, where->Area + at, size)))
            return TRUE;
        for (ULONG k = 0; k < size; k++)
            if (held[k])
                return TRUE;
    }
    return FALSE;
}

/*
 * what a thread holds, see KiCaptureXstate, into the area of a user context in the layout its header names. the mask
 * is what the thread has in use of that layout, whatever the caller asked for, and the shadow stack state always is.
 * a feature the area has no room for is left out and the status tells.
 */
NTSTATUS KiStoreUserXstate(const KI_USER_XSTATE *where, const UCHAR *buffer)
{
    static const UCHAR zero[64];
    ULONG64 mask = (((const XSAVE_AREA_HEADER *)buffer)->Mask | g_xsave_supervisor) & where->Layout;
    NTSTATUS status = KiCopyToUser(where->Area, &mask, sizeof(mask));
    if (!NT_SUCCESS(status))
        return status;
    for (ULONG i = 2; i < 64; i++) {
        if (!(mask >> i & 1))
            continue;
        ULONG at = xfeature_at(where->Layout, i), size = g_xfeature[i].Size;
        if ((ULONG64)at + size > where->Length) {
            status = STATUS_BUFFER_OVERFLOW;
            continue;
        }
        BOOLEAN supervisor = (g_xsave_supervisor >> i & 1) != 0;
        NTSTATUS copied = KiCopyToUser(where->Area + at, supervisor ? zero : buffer + g_xfeature[i].User,
                                       supervisor && size > sizeof(zero) ? sizeof(zero) : size);
        if (!NT_SUCCESS(copied))
            return copied;
    }
    return status;
}

/* the reverse, for KiRestoreXstate: the features the mask of the area names, the others go back to their start. */
NTSTATUS KiLoadUserXstate(const KI_USER_XSTATE *where, PUCHAR buffer)
{
    memset(buffer, 0, g_xstate_size > sizeof(XSAVE_AREA_HEADER) ? g_xstate_size : sizeof(XSAVE_AREA_HEADER));
    XSAVE_AREA_HEADER *held = (XSAVE_AREA_HEADER *)buffer;
    held->Mask = where->Mask & where->Layout & KiXsaveMask & ~3ull;
    for (ULONG i = 2; i < 64; i++) {
        if (!(held->Mask >> i & 1))
            continue;
        ULONG at = xfeature_at(where->Layout, i), size = g_xfeature[i].Size;
        if ((ULONG64)at + size > where->Length) {
            held->Mask &= ~(1ull << i);
            continue;
        }
        NTSTATUS status = KiCopyFromUser(buffer + g_xfeature[i].User, where->Area + at, size);
        if (!NT_SUCCESS(status))
            return status;
    }
    return STATUS_SUCCESS;
}

/* a thread starts with the features beyond sse in their initial state. */
void KiResetXstate(PKTHREAD thread)
{
    UCHAR none[KI_XSTATE_MAXIMUM];
    if (!g_xstate_size)
        return;
    memset(none, 0, g_xstate_size);
    KiRestoreXstate(thread, none);
}

/*
 * the x87 state of a thread, the first 0xA0 bytes of what fxsave writes: the caller's own is the processor's, that
 * of a thread which does not run is what its last switch saved, or the processor's initial state when it saved none.
 * the six bytes behind each register are zero.
 */
static void capture_x87(PKTHREAD thread, XSAVE_FORMAT *out)
{
    DECLSPEC_ALIGN(16) UCHAR area[512];
    const UCHAR *from = nullptr;
    if (thread == KiCurrentThread()) {
        __asm__ volatile("fxsave64 %0" : "=m"(area));
        from = area;
    } else if (thread->XSaveArea && (*(ULONG64 *)((PUCHAR)thread->XSaveArea + XSAVE_HEADER) & 1)) {
        from = thread->XSaveArea;
    }
    if (from) {
        memcpy(out, from, 0xA0);
    } else {
        memset(out, 0, 0xA0);
        out->ControlWord = 0x37F;
    }
    for (ULONG i = 0; i < 8; i++)
        memset((PUCHAR)&out->FloatRegisters[i] + 10, 0, 6);
}

/*
 * the reverse. the processor takes the state and gives it back, with the bits it does not have dropped, the busy and
 * summary flags of the status made from the rest and the pointers of the last instruction cleared: a thread that
 * does not run keeps that in its save area, the caller keeps it loaded. the sse registers and mxcsr stay as they are.
 */
static void restore_x87(PKTHREAD thread, const XSAVE_FORMAT *in)
{
    DECLSPEC_ALIGN(16) UCHAR mine[512], theirs[512];
    BOOLEAN live = thread == KiCurrentThread();
    if (!live && !thread->XSaveArea)
        return;
    ULONG64 flags = KiDisableInterrupts();
    __asm__ volatile("fxsave64 %0" : "=m"(mine));
    memcpy(theirs, mine, sizeof(theirs));
    memcpy(theirs, in, 0x18);
    memcpy(theirs + 0x20, (const UCHAR *)in + 0x20, 0x80);
    __asm__ volatile("fxrstor64 %0" : : "m"(theirs));
    if (!live) {
        __asm__ volatile("fxsave64 %0" : "=m"(theirs));
        __asm__ volatile("fxrstor64 %0" : : "m"(mine));
        PUCHAR area = thread->XSaveArea;
        memcpy(area, theirs, 0x18);
        memcpy(area + 0x20, theirs + 0x20, 0x80);
        *(ULONG64 *)(area + XSAVE_HEADER) |= 1;
    }
    KiRestoreInterrupts(flags);
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
    /* the processor feature is advertised to user mode, so rd/wrfsbase and rd/wrgsbase must be legal there. */
    if (regs[1] & (1u << 0)) {
        cr4 |= CR4_FSGSBASE;
        g_fsgsbase = TRUE;
    }
    __asm__ volatile("movq %0, %%cr4" ::"r"(cr4) : "memory");
    /*
     * windows checks alignment in user mode: a thread that sets the flag faults on a misaligned access, and its
     * exception dispatcher runs with the flag still set. measured on windows.
     */
    ULONG64 cr0;
    __asm__ volatile("movq %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("movq %0, %%cr0" ::"r"(cr0 | (1ull << 18)) : "memory");
    if (number == 0)
        initialize_xsave();
    else {
        __asm__ volatile("xsetbv" ::"c"(0), "a"((ULONG)KiXsaveMask), "d"((ULONG)(KiXsaveMask >> 32)));
        if (g_xsave_supervisor)
            KiWriteMsr(MSR_XSS, g_xsave_supervisor);
    }

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
    *(ULONG64 *)(pcr->Tss + 0x2C) = (ULONG64)(g_debug_stacks[number] + sizeof(g_debug_stacks[number])); /* ist2 */
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
        /* the x87 control word windows starts threads with: the area says it holds x87 state, so it is loaded. */
        *(USHORT *)top = 0x27F;
        *(ULONG64 *)(top + XSAVE_HEADER) = 1;
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
    /* as a context reads it before the thread runs: the processor sets bit 1 by itself. */
    frame->Rflags = EFLAGS_IF;
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
    /* the x87 state is the processor's: the kernel does not use it, so it is that of the thread which runs. */
    capture_x87(KiCurrentThread(), &ctx->FltSave);
    ctx->MxCsr = frame->MxCsr;
    ctx->FltSave.MxCsr = frame->MxCsr;
    ctx->FltSave.MxCsr_Mask = KiMxcsrMask;
    if (frame->Flags & KTRAP_FLAG_FP_SAVED)
        memcpy(ctx->FltSave.XmmRegisters, frame->Xmm, sizeof(frame->Xmm));
}

/*
 * the whole user context of a thread from the frame it entered the kernel with, as windows 11 26300 reads it. a system
 * service saves rax, its first two arguments as rcx and rdx and the nonvolatile registers: r8 to r11 and xmm0 to xmm5
 * read as what the last interrupt or context left in the frame, see KiNoteUserTrap. the x87 state of another thread is
 * what it saved.
 */
void KiCaptureThreadContext(PKTHREAD thread, PKTRAP_FRAME frame, BOOLEAN in_service, CONTEXT *ctx)
{
    KiTrapFrameToContext(frame, ctx, CONTEXT_ALL);
    if (in_service) {
        if (!(thread->ContextSetInService & KI_CONTEXT_SET_INTEGER))
            ctx->Rcx = frame->R10;
        ctx->R8 = thread->ServiceStale[0];
        ctx->R9 = thread->ServiceStale[1];
        ctx->R10 = thread->ServiceStale[2];
        ctx->R11 = thread->ServiceStale[3];
        /*
         * a service entered with an instrumentation callback saves more: on windows 11 26300 r8 to r10 read as the
         * service was entered with them, and r11 as its first argument, seen in one measure with icb.c.
         */
        if (thread->ServiceInstrumented) {
            ctx->R8 = frame->R8;
            ctx->R9 = frame->R9;
            ctx->R10 = frame->R10;
            ctx->R11 = frame->R10;
        }
        memcpy(ctx->FltSave.XmmRegisters, thread->ServiceStaleXmm, sizeof(thread->ServiceStaleXmm));
    }
    if (thread != KiCurrentThread())
        capture_x87(thread, &ctx->FltSave);
    ctx->FltSave.MxCsr = frame->MxCsr;
    ctx->FltSave.MxCsr_Mask = KiMxcsrMask;
    KiGetThreadDebug(thread, ctx);
}

/* what a system service leaves unsaved in its frame is what was last written there: an interrupt taken in user mode. */
void KiNoteUserTrap(PKTHREAD thread, const KTRAP_FRAME *frame)
{
    thread->ServiceStale[0] = frame->R8;
    thread->ServiceStale[1] = frame->R9;
    thread->ServiceStale[2] = frame->R10;
    thread->ServiceStale[3] = frame->R11;
    memcpy(thread->ServiceStaleXmm, frame->Xmm, sizeof(thread->ServiceStaleXmm));
}

/*
 * a context never takes a native thread into 32-bit code, whoever sets it and through whichever service: measured on
 * windows 11 26300, a handler that continues from a fault of 32-bit code with the selector as delivered resumes in the
 * 64-bit segment at the same address. only the thread of a 32-bit program keeps the selector.
 */
void KiNativeCodeSegment(PKTHREAD thread, PKTRAP_FRAME frame, ULONG parts)
{
    if ((parts & CONTEXT_CONTROL) == CONTEXT_CONTROL && !PspIsWow64Process((PEPROCESS)thread->Process))
        frame->Cs = KGDT64_R3_CODE | RPL_USER;
}

/*
 * the parts of a context a thread takes, as windows 11 26300 does: the instruction pointer sign extended from bit 47,
 * the flags user mode may set, the 64-bit segments, mxcsr from the context and not from the save area, the x87
 * state through the processor. a thread in a system service keeps the frame it returns with, see
 * KiServiceExitVolatiles, and the frame reads back as written until then.
 */
void KiApplyThreadContext(PKTHREAD thread, PKTRAP_FRAME frame, BOOLEAN in_service, const CONTEXT *ctx, ULONG parts)
{
    KiContextToTrapFrame(ctx, frame, parts, UserMode);
    KiNativeCodeSegment(thread, frame, parts);
    if ((parts & CONTEXT_INTEGER) == CONTEXT_INTEGER) {
        thread->ServiceStale[0] = ctx->R8;
        thread->ServiceStale[1] = ctx->R9;
        thread->ServiceStale[2] = ctx->R10;
        thread->ServiceStale[3] = ctx->R11;
        if (in_service)
            thread->ContextSetInService |= KI_CONTEXT_SET_INTEGER;
    }
    if ((parts & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT) {
        memcpy(thread->ServiceStaleXmm, ctx->FltSave.XmmRegisters, sizeof(thread->ServiceStaleXmm));
        restore_x87(thread, &ctx->FltSave);
    }
    if ((parts & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS)
        KiSetThreadDebug(thread, ctx);
}

/*
 * what windows makes of the flags of a context it is asked for. the parts are the low five bits whatever the
 * architecture bits say, and the amd64 bit is set in the answer. the extended state and the kernel shadow stack
 * count only in a context that is amd64 alone, without bit 5: the first needs a CONTEXT_EX, the second is not
 * supported. the unwound mark stays, the exception request is answered with the report bit and what the thread
 * was in, the other marks are dropped.
 */
NTSTATUS KiContextRequest(ULONG flags, KI_CONTEXT_REQUEST *request)
{
    BOOLEAN extended = (flags & CONTEXT_AMD64) && !(flags & 0x10020);
    request->Parts = CONTEXT_AMD64 | (flags & 0x1F);
    request->Xstate = extended && (flags & 0x40);
    request->Report = (flags & CONTEXT_EXCEPTION_REQUEST) != 0;
    request->Out = request->Parts | (request->Xstate ? 0x40 : 0) | (flags & (CONTEXT_UNWOUND_TO_CALL | CONTEXT_EXCEPTION_REQUEST));
    if (extended && (flags & 0x80))
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
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
        /* the x87 and sse registers: the 96 bytes behind them in the save area stay the caller's. */
        to->MxCsr = from->MxCsr;
        memcpy(&to->FltSave, &from->FltSave, FIELD_OFFSET(XSAVE_FORMAT, XmmRegisters) + sizeof(from->FltSave.XmmRegisters));
    }
    if ((flags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS) {
        to->Dr0 = from->Dr0;
        to->Dr1 = from->Dr1;
        to->Dr2 = from->Dr2;
        to->Dr3 = from->Dr3;
        to->Dr6 = from->Dr6;
        to->Dr7 = from->Dr7;
        /* the branch records go with the breakpoints, zero as nothing records them. */
        to->LastBranchToRip = to->LastBranchFromRip = 0;
        to->LastExceptionToRip = to->LastExceptionFromRip = 0;
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
            /* like windows, an instruction pointer that is not canonical is made so from its low 48 bits. */
            frame->Rip = (ULONG64)((LONG64)(ctx->Rip << 16) >> 16);
            /* user mode may change only the arithmetic, trace and identification flags, and stays in user mode, 64 or
               32-bit. bit 1 is the processor's and reads back clear until the thread runs. */
            frame->Rflags = (ctx->EFlags & EFLAGS_USER_MASK) | EFLAGS_IF;
            user_segments(frame);
            if (ctx->SegCs == (KGDT64_R3_CMCODE | RPL_USER))
                frame->Cs = KGDT64_R3_CMCODE | RPL_USER;
        } else {
            frame->Rflags = ctx->EFlags & ~0x3000ull;
            frame->Cs = ctx->SegCs;
            frame->Ss = ctx->SegSs;
        }
    }
    if ((flags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT) {
        memcpy(frame->Xmm, ctx->FltSave.XmmRegisters, sizeof(frame->Xmm));
        frame->MxCsr = ctx->MxCsr & KiMxcsrMask;
        frame->Flags |= KTRAP_FLAG_FP_SAVED;
    }
}

/*
 * the layout of windows 11 26300 for KiUserApcDispatcher: the interrupted CONTEXT at its sp with the breakpoints, the
 * routine and its arguments in the home slots, and 0x530 above a machine frame that ends at the interrupted sp, of
 * which the rip and rsp alone are written. the instrumentation callback is the business of the service that returns
 * here, see KiExitToUserMode.
 */
BOOLEAN KiSetupUserApc(PKTRAP_FRAME frame, PVOID dispatcher, PKNORMAL_ROUTINE routine, PVOID context, PVOID arg1,
                       PVOID arg2)
{
    CONTEXT ctx;
    KiTrapFrameToContext(frame, &ctx, CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS);
    KiGetThreadDebug(KiCurrentThread(), &ctx);
    ctx.P1Home = (ULONG64)context;
    ctx.P2Home = (ULONG64)arg1;
    ctx.P3Home = (ULONG64)arg2;
    ctx.P4Home = (ULONG64)routine;
    ULONG64 machine = (user_stack(frame) - 0x28) & ~15ull;
    ULONG64 sp = machine - 0x530;
    /* at 0x4F0 the KCONTINUE_ARGUMENT the dispatcher gives NtContinueEx: resume, test alert and deliver apcs. */
    ULONG64 resume[3] = {0x300000001ull, 0, 0};
    if (!NT_SUCCESS(KiCopyToUser((PVOID)machine, &frame->Rip, sizeof(ULONG64))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)(machine + 0x18), &frame->Rsp, sizeof(ULONG64))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)(sp + 0x4F0), resume, sizeof(resume))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)sp, &ctx, sizeof(ctx))))
        return FALSE;
    frame->Rsp = sp;
    frame->Rip = (ULONG64)dispatcher;
    user_segments(frame);
    return TRUE;
}

/*
 * windows' layout for KiUserExceptionDispatcher, measured on windows 11 26300: at its sp the CONTEXT and its
 * CONTEXT_EX, the EXCEPTION_RECORD at 0x4F0, at 0x590 a machine frame of which the faulting rip and rsp alone are
 * written, and at 0x5C0 the extended state beyond sse. that area is the first 64 byte boundary with room for the state
 * below the faulting sp, the frame hangs under it, and the CONTEXT_EX gives the area the length up to the faulting sp.
 * the home slots that open the context are left as they were. it takes nothing in registers.
 */
BOOLEAN KiSetupUserException(PKTRAP_FRAME frame, PVOID dispatcher, PEXCEPTION_RECORD record)
{
    CONTEXT ctx;
    /* the context of an exception always carries the segments and the breakpoints, zeroes when there are none. */
    KiTrapFrameToContext(frame, &ctx, CONTEXT_FULL | CONTEXT_SEGMENTS | CONTEXT_DEBUG_REGISTERS);
    PKTHREAD thread = KiCurrentThread();
    KiGetThreadDebug(thread, &ctx);
    MIME_STATIC_ASSERT(sizeof(EXCEPTION_RECORD) <= 0xA0 && sizeof(CONTEXT) == 0x4D0, "dispatcher layout");
    ULONG64 sp = user_stack(frame), ctx_addr, machine_sp = frame->Rsp;
    /* under 32-bit code the frame is laid out from the stack pointer rounded down to 16, and its machine frame says
       that one, while the context keeps the real one. see gate32res.c beside xctx.c. */
    if (frame->Cs == (KGDT64_R3_CMCODE | RPL_USER))
        machine_sp = sp &= ~15ull;
    /* the CONTEXT_EX takes 0x20 bytes, its last quad zero. */
    struct {
        CONTEXT_EX Ex;
        ULONG64 Zero;
    } tail = {{{-(LONG)sizeof(CONTEXT), sizeof(CONTEXT) + 0x20}, {-(LONG)sizeof(CONTEXT), sizeof(CONTEXT)}, {0x21, 0}}, 0};
#define ex tail.Ex
    UCHAR state[KI_XSTATE_MAXIMUM];
    ULONG size = KiXstateSize();
    if (size) {
        ULONG64 area = (sp - size) & ~63ull;
        ctx_addr = area - 0x5C0;
        ex.XState.Offset = 0x5C0 - (LONG)sizeof(CONTEXT);
        ex.XState.Length = (ULONG)(sp - area);
        ex.All.Length = sizeof(CONTEXT) + (ULONG)ex.XState.Offset + ex.XState.Length;
        ctx.ContextFlags |= CONTEXT_AMD64_XSTATE;
        KiCaptureXstate(thread, ~0ull, state);
        if (!NT_SUCCESS(KiCopyToUser((PVOID)area, state, size)))
            return FALSE;
    } else {
        ctx_addr = ((sp & ~15ull) - 0x5C0) & ~63ull;
    }
    ULONG64 rec = ctx_addr + 0x4F0, machine = ctx_addr + 0x590;
    if (!NT_SUCCESS(KiCopyToUser((PVOID)machine, &frame->Rip, sizeof(ULONG64))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)(machine + 0x18), &machine_sp, sizeof(ULONG64))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)rec, record, sizeof(*record))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)(ctx_addr + sizeof(CONTEXT)), &tail, sizeof(tail))) ||
        !NT_SUCCESS(KiCopyToUser((PVOID)(ctx_addr + FIELD_OFFSET(CONTEXT, ContextFlags)), &ctx.ContextFlags,
                                 sizeof(ctx) - FIELD_OFFSET(CONTEXT, ContextFlags))))
        return FALSE;
    /* every register but the stack pointer reaches the dispatcher as the exception found it. */
    frame->Rsp = ctx_addr;
    KiCurrentThread()->ExceptionContext = ctx_addr;
    /* the dispatcher runs without the trap flag, the context keeps it. */
    frame->Rflags &= ~0x100ull;
    frame->Rip = (ULONG64)dispatcher;
    KiCurrentThread()->InstrumentReturn = TRUE;
    user_segments(frame);
    return TRUE;
#undef ex
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
    /* like windows, the dispatcher is reached through the instrumentation callback of the service that calls out. */
    if (KiCurrentThread()->ServiceInstrumented)
        KiInstrumentReturn(frame, PsCurrentProcess()->InstrumentationCallback);
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
