#include "../../ntos.h"
#include <seh/x64seh.h>

PVOID NTAPI RtlLookupFunctionEntry(ULONG64 pc, PULONG64 image_base, PVOID history);
PEXCEPTION_ROUTINE NTAPI RtlVirtualUnwind(ULONG type, ULONG64 base, ULONG64 pc, PRUNTIME_FUNCTION function, PCONTEXT c,
                                          PVOID *handler_data, PULONG64 establisher_frame,
                                          PKNONVOLATILE_CONTEXT_POINTERS pointers);

/*
 * drivers mime did not build, as anti-cheat ones: their imports of mime's code go through a stub per export, which
 * counts the calls for the statistics, and the clock samples which kernel code called the driver code it interrupts.
 *
 * the system threads such a driver starts run its code without the kernel lock, unless the option foreign-locked. its
 * calls take the lock on the way in and let it go on the way out, but for the exports that touch nothing the lock
 * guards. the lock is kept while the driver is at dispatch level or has interrupts off, which keeps its spin locks
 * the kernel lock's as everywhere else. an interrupt or a fault in its code takes the lock to its end, see
 * KiTrapCommon.
 */
#define FOREIGN_RECORDS 1024
#define FOREIGN_STUB 32
#define FOREIGN_IMAGES 8
#define FOREIGN_SITES 32

#define FOREIGN_FREE 1

typedef struct _KI_FOREIGN_IMPORT {
    PVOID Target;           /* read by KiForeignCall at 0. */
    volatile ULONG64 Calls; /* at 8. */
    ULONG64 Flags;          /* at 16. */
    PVOID Export;
    char Name[48];
} KI_FOREIGN_IMPORT;

extern char KiForeignCall[], KiForeignReturn[];
BOOLEAN KiForeignDirect, KiForeignUnlock;
LONG_PTR FASTCALL KiForeignDereference(PVOID object);

/* exports that touch nothing the kernel lock guards: reads of a thread or a process, references, ids and strings. */
static const char *const g_free[] = {
    "PsLookupThreadByThreadId", "PsLookupProcessByProcessId", "ObfReferenceObject", "ObfDereferenceObject",
    "PsGetThreadProcessId", "PsGetCurrentProcessId", "PsGetCurrentThreadId", "PsGetProcessId",
    "PsGetProcessInheritedFromUniqueProcessId", "IoGetCurrentProcess", "PsGetProcessImageFileName",
    "PsGetThreadProcess", "_stricmp", "_strnicmp", "wcsncmp", "RtlInitUnicodeString", "RtlInitAnsiString",
};

/* exports with a calling convention of their own, or that work on their caller's frame: no stub between. */
static const char *const g_direct[] = {
    "__chkstk", "_chkstk", "_alloca_probe", "_setjmp", "_setjmpex", "setjmp", "longjmp", "RtlCaptureContext",
    "RtlCaptureContext2", "RtlRestoreContext", "RtlVirtualUnwind", "RtlLookupFunctionEntry",
};

static struct {
    ULONG64 Threads, Calls, Kept, Traps, Resumed;
    ULONG64 Rip[16], Count[16];
} g_unlocked;

static KI_FOREIGN_IMPORT *g_records;
static ULONG64 g_stub_pages[FOREIGN_RECORDS * FOREIGN_STUB / PAGE_SIZE];
static PUCHAR g_stubs;
static ULONG g_count;
static struct {
    ULONG_PTR Start, End;
} g_images[FOREIGN_IMAGES];
static ULONG g_image_count;

static struct {
    ULONG64 Samples, ByIrql[3];
    ULONG64 Site[FOREIGN_SITES], Count[FOREIGN_SITES];
    PETHREAD Thread[FOREIGN_SITES];
    ULONG Reason[FOREIGN_SITES];
    PVOID Start[FOREIGN_SITES];
    BOOLEAN System[FOREIGN_SITES];
    char Image[FOREIGN_SITES][16];
    ULONG64 Lost;
    ULONG64 Cr8Seen, Cr8Highest, Cr8Rip;
} g_seen;

void KiNoteForeignImage(PVOID base, SIZE_T size)
{
    if (g_image_count < FOREIGN_IMAGES) {
        g_images[g_image_count].Start = (ULONG_PTR)base;
        g_images[g_image_count].End = (ULONG_PTR)base + size;
        __atomic_store_n(&g_image_count, g_image_count + 1, __ATOMIC_RELEASE);
    }
}

BOOLEAN KiIsForeignCode(ULONG_PTR pc)
{
    ULONG n = __atomic_load_n(&g_image_count, __ATOMIC_ACQUIRE);
    for (ULONG i = 0; i < n; i++)
        if (pc >= g_images[i].Start && pc < g_images[i].End)
            return TRUE;
    return FALSE;
}

/* an export may be data, as PsProcessType is: only code gets a stub. */
static BOOLEAN is_code(PVOID target)
{
    PKLDR_DATA_TABLE_ENTRY m = MiLookupModule((ULONG_PTR)target);
    if (!m)
        return FALSE;
    PUCHAR base = m->DllBase;
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)(base + ((PIMAGE_DOS_HEADER)base)->e_lfanew);
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    ULONG rva = (ULONG)((PUCHAR)target - base);
    for (USHORT i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        ULONG size = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if (rva >= sec[i].VirtualAddress && rva - sec[i].VirtualAddress < size)
            return (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    }
    return FALSE;
}

static BOOLEAN make_room(void)
{
    if (g_records)
        return TRUE;
    KI_FOREIGN_IMPORT *records =
        ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*records) * FOREIGN_RECORDS, KI_TAG('K', 'i', 'F', 'r'));
    PUCHAR stubs = MmAllocateSystemVa(RTL_NUMBER_OF(g_stub_pages));
    if (!records || !stubs)
        return FALSE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_stub_pages); i++) {
        g_stub_pages[i] = MmAllocatePage();
        if (!g_stub_pages[i])
            return FALSE;
        memset(HalPhysmap(g_stub_pages[i]), 0xCC, PAGE_SIZE);
        MiMapPage(MiKernelRoot(), (ULONG_PTR)stubs + i * PAGE_SIZE, g_stub_pages[i], MM_KERNEL_RX);
    }
    g_stubs = stubs;
    g_records = records;
    return TRUE;
}

/* what a foreign driver calls for an export: its stub, or the export itself when it is data or there is no room. */
PVOID KiForeignImport(PVOID target, const char *name)
{
    if (KiForeignDirect || !target || !is_code(target) || !make_room())
        return target;
    for (ULONG k = 0; k < RTL_NUMBER_OF(g_direct); k++)
        if (!strcmp(name, g_direct[k]))
            return target;
    for (ULONG i = 0; i < g_count; i++)
        if (g_records[i].Export == target)
            return g_stubs + i * FOREIGN_STUB;
    if (g_count == FOREIGN_RECORDS)
        return target;
    ULONG i = g_count;
    KI_FOREIGN_IMPORT *r = &g_records[i];
    r->Target = r->Export = target;
    r->Calls = 0;
    r->Flags = 0;
    strncpy(r->Name, name, sizeof(r->Name) - 1);
    r->Name[sizeof(r->Name) - 1] = 0;
    for (ULONG k = 0; k < RTL_NUMBER_OF(g_free); k++)
        if (!strcmp(name, g_free[k]))
            r->Flags = FOREIGN_FREE;
    /* the last reference ends the object, which is the kernel lock's work. */
    if (!strcmp(name, "ObfDereferenceObject"))
        r->Target = (PVOID)KiForeignDereference;
    /* mov r11, record; jmp [rip]; the address of KiForeignCall. the stub page is written through the physmap. */
    PUCHAR w = (PUCHAR)HalPhysmap(g_stub_pages[i * FOREIGN_STUB / PAGE_SIZE]) + i * FOREIGN_STUB % PAGE_SIZE;
    w[0] = 0x49;
    w[1] = 0xBB;
    *(ULONG64 *)(w + 2) = (ULONG64)r;
    w[10] = 0xFF;
    w[11] = 0x25;
    *(LONG *)(w + 12) = 0;
    *(ULONG64 *)(w + 16) = (ULONG64)KiForeignCall;
    __atomic_store_n(&g_count, i + 1, __ATOMIC_RELEASE);
    return g_stubs + i * FOREIGN_STUB;
}

/*
 * the way into an export for a driver's code that runs without the kernel lock, from KiForeignCallLocked: the
 * arguments past the fourth are copied into its frame, and the lock is taken. TRUE when the call is to let it go.
 * a call made at dispatch level or with interrupts off left it held, and this call takes it over.
 */
/* a frame that holds the lock for a thread without it, newest last. those past eight are counted only. */
static void push_frame(PKTHREAD thread, ULONG_PTR frame)
{
    if (thread->ForeignCount < RTL_NUMBER_OF(thread->ForeignFrames))
        thread->ForeignFrames[thread->ForeignCount] = frame;
    if (thread->ForeignCount < 0xFF)
        thread->ForeignCount++;
}

static void pop_frame(PKTHREAD thread)
{
    if (thread->ForeignCount)
        thread->ForeignCount--;
}

ULONG64 KiForeignEnter(ULONG64 *to, const ULONG64 *from)
{
    PKTHREAD thread = KiCurrentThread();
    if (to) {
        ULONG64 top = (ULONG64)thread->InitialStack;
        for (ULONG i = 0; i < 16 && ((ULONG64)from >= top || (ULONG64)(from + i + 1) <= top); i++)
            to[i] = from[i];
    }
    ULONG64 s = KiDisableInterrupts();
    PKPCR pcr = KeGetPcr();
    ULONG64 took = 0;
    if (KiKernelLock[KI_LOCK_OWNER] != pcr->Prcb.Number) {
        pcr->UserMode = 0;
        KiAcquireKernelLock();
        KiKernelLock[KI_LOCK_REASON] = KI_LOCK_REASON_FOREIGN;
        KiNoteLockWait(KI_LOCK_REASON_FOREIGN);
        g_unlocked.Calls++;
        took = 1;
    } else if (thread->ForeignKept) {
        thread->ForeignKept = FALSE;
        took = 1;
    }
    if (took)
        push_frame(thread, to ? (ULONG_PTR)to : (ULONG_PTR)__builtin_frame_address(0));
    KiRestoreInterrupts(s);
    return took;
}

/* the way out: the lock goes as on a return to user mode, unless the driver is at dispatch level or masked. */
void KiForeignLeave(ULONG64 took)
{
    if (!took)
        return;
    ULONG64 s = KiDisableInterrupts();
    PKPCR pcr = KeGetPcr();
    pop_frame(pcr->Prcb.CurrentThread);
    if (KiKernelLock[KI_LOCK_OWNER] == pcr->Prcb.Number) {
        if (pcr->Irql >= DISPATCH_LEVEL || !(s & 0x200)) {
            pcr->Prcb.CurrentThread->ForeignKept = TRUE;
            g_unlocked.Kept++;
        } else {
            if (pcr->FlushPending)
                KiSettleTlbFlush();
            __atomic_store_n(&pcr->UserMode, 1, __ATOMIC_SEQ_CST);
            KiReleaseKernelLock();
        }
    }
    KiRestoreInterrupts(s);
}

/* the end of an interrupt or a fault taken in a driver's code without the lock, from KiExceptionExit. */
void KiForeignTrapTaken(PKTRAP_FRAME frame)
{
    push_frame(KiCurrentThread(), (ULONG_PTR)frame);
}

void KiForeignTrapExit(void)
{
    PKPCR pcr = KeGetPcr();
    g_unlocked.Traps++;
    pop_frame(pcr->Prcb.CurrentThread);
    if (KiKernelLock[KI_LOCK_OWNER] != pcr->Prcb.Number)
        return;
    if (pcr->FlushPending)
        KiSettleTlbFlush();
    __atomic_store_n(&pcr->UserMode, 1, __ATOMIC_SEQ_CST);
    KiReleaseKernelLock();
}

LONG_PTR FASTCALL KiForeignDereference(PVOID object)
{
    if (ObDereferenceObjectFast(object))
        return 1;
    ULONG64 took = KiForeignEnter(nullptr, nullptr);
    LONG_PTR count = ObfDereferenceObject(object);
    KiForeignLeave(took);
    return count;
}

/*
 * an exception handled in a driver's code goes on in it from RtlRestoreContext, past the frames that took the lock
 * for the fault and for the calls between. with none of them left, the driver's code is back to running without it.
 */
void KiForeignRestore(PCONTEXT context)
{
    PKTHREAD thread = KiCurrentThread();
    if (!thread || !thread->ForeignUnlocked)
        return;
    ULONG64 s = KiDisableInterrupts();
    PKPCR pcr = KeGetPcr();
    ULONG recorded = thread->ForeignCount < RTL_NUMBER_OF(thread->ForeignFrames) ? thread->ForeignCount
                                                                                 : RTL_NUMBER_OF(thread->ForeignFrames);
    /* frames are pushed going down the stack: those below the resumed one are being unwound. */
    if (recorded && thread->ForeignFrames[recorded - 1] < context->Rsp)
        thread->ForeignCount = (UCHAR)recorded;
    while (thread->ForeignCount && thread->ForeignCount <= RTL_NUMBER_OF(thread->ForeignFrames) &&
           thread->ForeignFrames[thread->ForeignCount - 1] < context->Rsp)
        thread->ForeignCount--;
    if (!thread->ForeignCount && KiIsForeignCode(context->Rip) && KiKernelLock[KI_LOCK_OWNER] == pcr->Prcb.Number &&
        pcr->Irql < DISPATCH_LEVEL && (context->EFlags & 0x200)) {
        thread->ForeignKept = FALSE;
        g_unlocked.Resumed++;
        if (pcr->FlushPending)
            KiSettleTlbFlush();
        __atomic_store_n(&pcr->UserMode, 1, __ATOMIC_SEQ_CST);
        KiReleaseKernelLock();
    }
    KiRestoreInterrupts(s);
}

/* a system thread of a foreign driver, from KiThreadStartupC: its code runs without the kernel lock. */
void KiForeignRun(PKSTART_ROUTINE routine, PVOID context)
{
    PKTHREAD thread = KiCurrentThread();
    g_unlocked.Threads++;
    thread->ForeignCount = 0;
    thread->ForeignUnlocked = TRUE;
    ULONG64 s = KiDisableInterrupts();
    if (KeGetPcr()->FlushPending)
        KiSettleTlbFlush();
    __atomic_store_n(&KeGetPcr()->UserMode, 1, __ATOMIC_SEQ_CST);
    KiReleaseKernelLock();
    KiRestoreInterrupts(s);
    routine(context);
    KiForeignEnter(nullptr, nullptr);
    thread->ForeignUnlocked = FALSE;
    thread->ForeignKept = FALSE;
    thread->ForeignCount = 0;
}

/* the caller of an export is a foreign driver when the export was reached through KiForeignCallLocked. */
BOOLEAN KiIsForeignCaller(ULONG_PTR return_address)
{
    return KiIsForeignCode(return_address) || return_address == (ULONG_PTR)KiForeignReturn;
}

/* the first frame out of foreign code below an interrupt that came in it: the kernel code that called the driver. */
static ULONG64 entered_from(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    if (!thread)
        return 0;
    ULONG64 top = (ULONG64)thread->InitialStack, limit = (ULONG64)thread->StackLimit - 0x100000;
    CONTEXT c;
    memset(&c, 0, sizeof(c));
    c.Rip = frame->Rip;
    c.Rsp = frame->Rsp;
    c.Rbp = frame->Rbp;
    c.Rbx = frame->Rbx;
    c.Rsi = frame->Rsi;
    c.Rdi = frame->Rdi;
    c.R12 = frame->R12;
    c.R13 = frame->R13;
    c.R14 = frame->R14;
    c.R15 = frame->R15;
    for (ULONG depth = 0; depth < 64; depth++) {
        if (!KiIsForeignCode(c.Rip))
            return MiIsCodeAddress(c.Rip) ? c.Rip : 0;
        if (c.Rsp < limit || c.Rsp + 8 > top || (c.Rsp & 7))
            return 0;
        ULONG64 base;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(c.Rip, &base, nullptr);
        if (function) {
            PVOID data;
            ULONG64 establisher;
            RtlVirtualUnwind(0, base, c.Rip, function, &c, &data, &establisher, nullptr);
        } else {
            c.Rip = *(ULONG64 *)c.Rsp;
            c.Rsp += 8;
        }
    }
    return 0;
}

/* a clock or other interrupt taken in kernel mode, with the kernel lock held. */
void KiObserveKernelInterrupt(PKTRAP_FRAME frame, KIRQL irql)
{
    ULONG64 cr8;
    __asm__ volatile("mov %%cr8, %0" : "=r"(cr8));
    if (cr8) {
        g_seen.Cr8Seen++;
        if (cr8 > g_seen.Cr8Highest)
            g_seen.Cr8Highest = cr8;
        g_seen.Cr8Rip = frame->Rip;
    }
    /* mime's code that ran without the lock: the exports above and the stubs, nothing else. */
    if (frame->Flags & KTRAP_FLAG_LOCK_TAKEN && !KiIsForeignCode(frame->Rip)) {
        ULONG i = 0;
        while (i < 16 && g_unlocked.Count[i] && g_unlocked.Rip[i] != frame->Rip)
            i++;
        if (i < 16) {
            g_unlocked.Rip[i] = frame->Rip;
            g_unlocked.Count[i]++;
        }
    }
    if (frame->Vector != KI_VECTOR_CLOCK || !KiIsForeignCode(frame->Rip))
        return;
    g_seen.Samples++;
    g_seen.ByIrql[irql >= DISPATCH_LEVEL ? 2 : irql]++;
    ULONG64 site = entered_from(frame);
    PETHREAD thread = PsCurrentThread();
    /* the clock names a holder that came for no service, see KiTrapDispatch. */
    ULONG reason = KiKernelLock[KI_LOCK_REASON];
    if (reason >= 0x2001)
        reason = 0;
    for (ULONG i = 0; i < FOREIGN_SITES; i++) {
        if (g_seen.Count[i] && g_seen.Site[i] == site && g_seen.Thread[i] == thread && g_seen.Reason[i] == reason) {
            g_seen.Count[i]++;
            return;
        }
        if (!g_seen.Count[i]) {
            g_seen.Site[i] = site;
            g_seen.Thread[i] = thread;
            g_seen.Reason[i] = reason;
            g_seen.Start[i] = thread ? thread->StartAddress : nullptr;
            g_seen.System[i] = thread && thread->SystemThread;
            if (thread)
                memcpy(g_seen.Image[i], ((PEPROCESS)thread->Tcb.Process)->ImageFileName, 15);
            g_seen.Count[i] = 1;
            return;
        }
    }
    g_seen.Lost++;
}

void KiDumpForeignStats(void)
{
    if (!g_image_count)
        return;
    ULONG64 all = 0;
    for (ULONG i = 0; i < g_count; i++)
        all += g_records[i].Calls;
    KiLogPrintf("foreign calls: %llu to %u exports", all, g_count);
    static ULONG64 taken[FOREIGN_RECORDS];
    for (ULONG i = 0; i < g_count; i++)
        taken[i] = __atomic_exchange_n(&g_records[i].Calls, 0, __ATOMIC_RELAXED);
    for (int round = 0; round < 40; round++) {
        ULONG64 best = 0;
        ULONG bi = 0;
        for (ULONG i = 0; i < g_count; i++)
            if (taken[i] > best)
                best = taken[i], bi = i;
        if (!best)
            break;
        KiLogPrintf("foreign calls to %s: %llu", g_records[bi].Name, best);
        taken[bi] = 0;
    }
    KiLogPrintf("foreign code sampled %llu times: %llu at passive, %llu at apc, %llu at dispatch or above, %llu "
                "from sites past the table",
                g_seen.Samples, g_seen.ByIrql[0], g_seen.ByIrql[1], g_seen.ByIrql[2], g_seen.Lost);
    for (ULONG i = 0; i < FOREIGN_SITES && g_seen.Count[i]; i++) {
        char where[96] = "an unknown frame";
        if (g_seen.Site[i])
            MiDescribeCodeAddress(g_seen.Site[i], where, sizeof(where));
        char start[96] = "";
        if (g_seen.Start[i])
            MiDescribeCodeAddress((ULONG_PTR)g_seen.Start[i], start, sizeof(start));
        KiLogPrintf("foreign code entered from %s %p: %llu, thread %p %s of %s started at %s, service %#x", where,
                    (PVOID)g_seen.Site[i], g_seen.Count[i], g_seen.Thread[i],
                    g_seen.System[i] ? "a system thread" : "a user thread", g_seen.Image[i], start,
                    g_seen.Reason[i] ? g_seen.Reason[i] - 1 : 0);
    }
    if (g_seen.Cr8Seen) {
        char where[96];
        MiDescribeCodeAddress(g_seen.Cr8Rip, where, sizeof(where));
        KiLogPrintf("kernel mode interrupts with cr8 raised: %llu, highest %llu, last at %s", g_seen.Cr8Seen,
                    g_seen.Cr8Highest, where);
    }
    memset(&g_seen, 0, sizeof(g_seen));
    if (g_unlocked.Threads) {
        KiLogPrintf("foreign threads without the kernel lock: %llu, which took it for %llu calls, kept it at dispatch "
                    "level %llu times, took it for %llu interrupts and faults and went on without it after %llu "
                    "exceptions",
                    g_unlocked.Threads, g_unlocked.Calls, g_unlocked.Kept, g_unlocked.Traps, g_unlocked.Resumed);
        for (ULONG i = 0; i < 16 && g_unlocked.Count[i]; i++) {
            char where[96];
            MiDescribeCodeAddress(g_unlocked.Rip[i], where, sizeof(where));
            KiLogPrintf("mime's code interrupted without the kernel lock at %s: %llu", where, g_unlocked.Count[i]);
        }
        g_unlocked.Calls = g_unlocked.Kept = g_unlocked.Traps = g_unlocked.Resumed = 0;
        memset(g_unlocked.Rip, 0, sizeof(g_unlocked.Rip));
        memset(g_unlocked.Count, 0, sizeof(g_unlocked.Count));
    }
}
