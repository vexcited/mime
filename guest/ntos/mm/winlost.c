/*
 * pages of file windows that the host took away. a program of the host can shorten a file that a view of the guest maps,
 * which windows does not allow for a local file, but a network share does from its other side: the pages past the new end
 * cannot be read any more. windows answers the access with an in page error in the thread that made it, with the kind of
 * access, the address and the status of the failed read as parameters, and keeps the rest of the program going.
 *
 * the host tells the guest before it gives the pages up (MIME_HC_FS_WINDOW_LOST, after MIME_IRQ_WINDOW). a thread of the
 * kernel marks every section on the window with the range that is gone, drops the cache entries of the window, and takes
 * the pages out of the page tables of every process that maps them, leaving the entries a view leaves for the first touch.
 * the touch goes to the section, which refuses a page in its range, and the fault becomes the in page error:
 * STATUS_IN_PAGE_ERROR with three parameters for a program, and the status as the result of a system call that was reading
 * or writing such a page for it. the range belongs to the section and not to the addresses: a window that is unmapped and
 * placed again at the same addresses for another file has none of it.
 */
#include "../ntos.h"

/* the status of the read that failed: the file is no longer what the view was made of. */
#define STATUS_FILE_INVALID_VALUE ((NTSTATUS)0xC0000098L)
#define BATCH 32

static LIST_ENTRY g_sections = {&g_sections, &g_sections};
static KEVENT g_event;
static MimeWindowLost g_io;

/* sections on a window of the host, the ones a loss can reach. */
void MmWindowSectionAttach(PSECTION s)
{
    if (!s->SourceRef)
        return;
    ULONG64 state = KiAcquireDispatcherLock();
    InsertTailList(&g_sections, &s->WindowLinks);
    KiReleaseDispatcherLock(state);
}

void MmWindowSectionDetach(PSECTION s)
{
    if (!s->WindowLinks.Flink)
        return;
    ULONG64 state = KiAcquireDispatcherLock();
    RemoveEntryList(&s->WindowLinks);
    s->WindowLinks.Flink = nullptr;
    KiReleaseDispatcherLock(state);
}

/* what the lost range is for each section that has pages in it; the pages come in the order of the file, the loss is its end. */
static void mark_sections(ULONG64 begin, ULONG64 end)
{
    ULONG64 state = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = g_sections.Flink; e != &g_sections; e = e->Flink) {
        PSECTION s = CONTAINING_RECORD(e, SECTION, WindowLinks);
        ULONG64 from = s->SourcePa, to = s->SourcePa + ROUND_TO_PAGES(s->SourceSize);
        if (begin >= to || end <= from)
            continue;
        ULONG64 lo = begin > from ? begin : from, hi = end < to ? end : to;
        ULONG64 old_begin = s->LostBegin, old_end = s->LostEnd;
        if (old_end > old_begin) {
            if (old_begin < lo)
                lo = old_begin;
            if (old_end > hi)
                hi = old_end;
        }
        __atomic_store_n(&s->LostBegin, lo, __ATOMIC_RELEASE);
        __atomic_store_n(&s->LostEnd, hi, __ATOMIC_RELEASE);
    }
    KiReleaseDispatcherLock(state);
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
        if (!s || !s->SourceRef || s->LostEnd <= s->LostBegin)
            continue;
        /* the view's pages as indexes of the section, then the addresses of the view that hold a page of the range. */
        ULONG64 view_first = vad->SectionOffset >> PAGE_SHIFT;
        ULONG64 view_last = view_first + ((vad->EndVa - vad->StartVa) >> PAGE_SHIFT);
        ULONG_PTR va = vad->StartVa;
        for (ULONG64 index = view_first; index < view_last;) {
            ULONG_PTR table_end = (va & ~(ULONG_PTR)0x1FFFFF) + 0x200000;
            ULONG64 *run = MiWalk(as->Root, va, FALSE, 3);
            ULONG64 values[512];
            ULONG count = 0;
            ULONG_PTR run_va = va;
            BOOLEAN changed = FALSE;
            while (index < view_last && va < table_end) {
                if (run) {
                    ULONG64 old = run[count], pa = old & MM_PTE_ADDR_MASK;
                    values[count] = old;
                    if ((old & MM_PTE_VALID) && pa >= begin && pa < end) {
                        values[count] = MiSectionSwPte(MiPteToProtect(old));
                        changed = TRUE;
                    }
                }
                count++;
                index++;
                va += PAGE_SIZE;
            }
            if (changed)
                MiRewritePtes(run, run_va, count, values, as->Asid);
        }
    }
    MiUnlockAddressSpace(as);
    KiAttachProcess(saved);
}

/* every address space that has a view of a section on the window. */
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
            mark_sections(begin, end);
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
