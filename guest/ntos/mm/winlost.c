/*
 * pages of file windows that the host took away. a program of the host can shorten a file that a view of the guest maps,
 * which windows does not allow for a local file, but a network share does from its other side: the pages past the new end
 * cannot be read any more. windows answers the access with an in page error in the thread that made it, with the kind of
 * access, the address and the status of the failed read as parameters, and keeps the rest of the program going.
 *
 * the host tells the guest before it gives the pages up (MIME_HC_FS_WINDOW_LOST, after MIME_IRQ_WINDOW). a thread of the
 * kernel records the range, drops the entries of the file cache over it and takes the pages out of the page tables of every
 * process that maps them, leaving the entries a view leaves for the first touch. the touch then goes to the section, which
 * refuses a page in a lost range, and the fault becomes the in page error: STATUS_IN_PAGE_ERROR with three parameters for
 * a program, and the status as the result of a system call that was reading or writing such a page for it.
 */
#include "../ntos.h"

#define MAX_LOST 64
/* the status of the read that failed: the file is no longer what the view was made of. */
#define STATUS_FILE_INVALID_VALUE ((NTSTATUS)0xC0000098L)
#define BATCH 32

typedef struct LOST_RANGE {
    ULONG64 Window; /* where the window it belongs to starts, to forget the range with it */
    ULONG64 Begin, End;
} LOST_RANGE;

static LOST_RANGE g_ranges[MAX_LOST];
volatile ULONG MmWindowDamaged;
static KEVENT g_event;
static MimeWindowLost g_io;

BOOLEAN MmWindowPageLostSlow(ULONG64 pa)
{
    ULONG n = __atomic_load_n(&MmWindowDamaged, __ATOMIC_ACQUIRE);
    for (ULONG i = 0; i < n; i++)
        if (pa >= g_ranges[i].Begin && pa < g_ranges[i].End)
            return TRUE;
    return FALSE;
}

/* a window goes away, and the addresses it had can be given to another file. */
void MmWindowForget(ULONG64 window)
{
    if (!__atomic_load_n(&MmWindowDamaged, __ATOMIC_ACQUIRE))
        return;
    ULONG64 s = KiAcquireDispatcherLock();
    ULONG n = MmWindowDamaged;
    for (ULONG i = 0; i < n;) {
        if (g_ranges[i].Window == window) {
            g_ranges[i] = g_ranges[--n];
            __atomic_store_n(&MmWindowDamaged, n, __ATOMIC_RELEASE);
        } else {
            i++;
        }
    }
    KiReleaseDispatcherLock(s);
}

static void record(ULONG64 window, ULONG64 begin, ULONG64 end)
{
    ULONG64 s = KiAcquireDispatcherLock();
    ULONG n = MmWindowDamaged;
    for (ULONG i = 0; i < n; i++) {
        if (g_ranges[i].Window == window && begin <= g_ranges[i].End && end >= g_ranges[i].Begin) {
            if (begin < g_ranges[i].Begin)
                g_ranges[i].Begin = begin;
            if (end > g_ranges[i].End)
                g_ranges[i].End = end;
            KiReleaseDispatcherLock(s);
            return;
        }
    }
    if (n < MAX_LOST) {
        g_ranges[n] = (LOST_RANGE){window, begin, end};
        __atomic_store_n(&MmWindowDamaged, n + 1, __ATOMIC_RELEASE);
    } else {
        KiLogPrintf("window: no room to record pages that are gone at %llx", begin);
    }
    KiReleaseDispatcherLock(s);
}

/* the entries a view leaves for the first touch, in place of the pages of the lost range, in one address space. */
static void take_out(PEPROCESS process, ULONG64 begin, ULONG64 end)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(&process->Pcb);
    MiLockAddressSpace(as);
    for (ULONG v = 0; as->Root && v < as->VadCount; v++) {
        PMMVAD vad = as->Vads[v];
        PSECTION s = vad->Section;
        if (!s || !s->SourcePa)
            continue;
        ULONG64 source_end = s->SourcePa + ROUND_TO_PAGES(s->SourceSize);
        if (begin >= source_end || end <= s->SourcePa)
            continue;
        /* the pages of the view that the section's lost bytes are, as indexes of the section and then addresses. */
        ULONG64 first = ((begin > s->SourcePa ? begin : s->SourcePa) - s->SourcePa) >> PAGE_SHIFT;
        ULONG64 last = (((end < source_end ? end : source_end) - s->SourcePa) + PAGE_SIZE - 1) >> PAGE_SHIFT;
        ULONG64 view_first = vad->SectionOffset >> PAGE_SHIFT;
        ULONG64 view_last = view_first + ((vad->EndVa - vad->StartVa) >> PAGE_SHIFT);
        if (first < view_first)
            first = view_first;
        if (last > view_last)
            last = view_last;
        for (ULONG_PTR va = vad->StartVa + ((first - view_first) << PAGE_SHIFT);
             first < last;) {
            /* one table at a time: the entries up to the end of the 2 MiB the address is in. */
            ULONG_PTR table_end = (va & ~(ULONG_PTR)0x1FFFFF) + 0x200000;
            ULONG count = 0;
            ULONG64 *pte = MiWalk(as->Root, va, FALSE, 3);
            ULONG64 values[512];
            ULONG64 *run = pte;
            ULONG_PTR run_va = va;
            BOOLEAN changed = FALSE;
            while (first < last && va < table_end) {
                if (pte) {
                    ULONG64 old = pte[count];
                    values[count] = old;
                    ULONG64 pa = old & MM_PTE_ADDR_MASK;
                    if ((old & MM_PTE_VALID) && pa >= begin && pa < end) {
                        values[count] = MiSectionSwPte(MiPteToProtect(old));
                        changed = TRUE;
                    }
                }
                count++;
                first++;
                va += PAGE_SIZE;
            }
            if (changed)
                MiRewritePtes(run, run_va, count, values, as->Asid);
        }
    }
    MiUnlockAddressSpace(as);
    KiAttachProcess(saved);
}

/* every address space that maps pages of the range. */
static void take_out_everywhere(ULONG64 begin, ULONG64 end)
{
    for (ULONG skipped = 0;;) {
        PEPROCESS batch[BATCH];
        ULONG count = 0, seen = 0;
        ULONG64 s = KiAcquireDispatcherLock();
        for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead && count < BATCH; e = e->Flink) {
            PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
            if (seen++ < skipped)
                continue;
            skipped++;
            if (p->SystemProcess || p->Terminating || !p->AddressSpace.Root)
                continue;
            ObReferenceObject(p);
            batch[count++] = p;
        }
        KiReleaseDispatcherLock(s);
        if (!count)
            return;
        for (ULONG i = 0; i < count; i++) {
            if (batch[i]->AddressSpace.Root && !batch[i]->Terminating)
                take_out(batch[i], begin, end);
            ObDereferenceObject(batch[i]);
        }
    }
}

static void worker(PVOID context)
{
    ULONG64 acked = 0;
    for (;;) {
        KeWaitForSingleObject(&g_event, Executive, KernelMode, FALSE, nullptr);
        for (;;) {
            g_io.acked = acked;
            if (HalHypercallUnlocked(MIME_HC_FS_WINDOW_LOST, HalPhysicalOf(&g_io), 0, 0, 0, 0, 0, nullptr) != MIME_HC_STATUS_OK ||
                !g_io.serial)
                break;
            ULONG64 begin = g_io.ipa, end = g_io.ipa + g_io.size;
            KiLogPrintf("window: the pages at %llx for %llx bytes are gone, the file was shortened", begin, g_io.size);
            record(g_io.window, begin, end);
            FsCacheWindowLost(begin, end);
            take_out_everywhere(begin, end);
            acked = g_io.serial;
        }
    }
}

void MmWindowLostInitialize(void)
{
    KeInitializeEvent(&g_event, SynchronizationEvent, FALSE);
    PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, worker, nullptr);
}

/* at the interrupt of the session. */
void MmWindowLostInterrupt(void)
{
    KeSetEvent(&g_event, IO_NO_INCREMENT, FALSE);
}

/* what a fault on a page the section lost leaves for the exception that follows. */
void MmWindowLostFault(void)
{
    KiCurrentThread()->FaultStatus = STATUS_FILE_INVALID_VALUE;
}
