/*
 * file window pages the host took away. a host program can shorten a file that a guest view maps, which
 * Windows forbids for a local file but a network share allows from its other side, so pages past the new
 * end become unreadable. Windows raises an in page error in the faulting thread, with the access kind,
 * address and failed read status as parameters, and the rest of the program keeps going.
 *
 * before giving the pages up, the host raises MIME_IRQ_WINDOW and MIME_HC_FS_WINDOW_LOST returns the range. a kernel
 * thread marks every section on the window with the lost range, drops the window's cache entries, and
 * resets the ptes of every mapping process to first touch entries. the touch reaches the section, which
 * refuses pages in its lost range, so a program gets STATUS_IN_PAGE_ERROR with three parameters and a
 * system call touching such a page returns the status. the range belongs to the section, not to the
 * addresses, so a window later placed at the same addresses for another file is unaffected.
 */
#include "../ntos.h"

/* the failed read status: the file is no longer what the view was made of. */
#define STATUS_FILE_INVALID_VALUE ((NTSTATUS)0xC0000098L)
#define BATCH 32

static LIST_ENTRY g_sections = {&g_sections, &g_sections};
static KEVENT g_event;
static MimeWindowLost g_io;

/* tracks sections on a host window, the ones a loss can reach. */
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

/* pages come in file order, so what is lost is the end of each section. */
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

/*
 * in one address space, replaces mapped pages of the range with first touch entries: in the views of sections that
 * lost pages, or in those of one section when it is given.
 */
static void take_out(PEPROCESS process, ULONG64 begin, ULONG64 end, PSECTION only)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(&process->Pcb);
    MiLockAddressSpace(as);
    for (ULONG v = 0; as->Root && v < as->VadCount; v++) {
        PMMVAD vad = as->Vads[v];
        PSECTION s = vad->Section;
        if (only ? s != only : !s || !s->SourceRef || s->LostEnd <= s->LostBegin)
            continue;
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

static void take_out_everywhere(ULONG64 begin, ULONG64 end, PSECTION only)
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
                take_out(batch[i], begin, end, only);
            ObDereferenceObject(batch[i]);
        }
    }
}

void MmWindowRedirect(ULONG volume, ULONG64 file_id, PSECTION copy)
{
    PSECTION found[BATCH];
    ULONG count = 0;
    ULONG64 state = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = g_sections.Flink; e != &g_sections && count < BATCH; e = e->Flink) {
        PSECTION s = CONTAINING_RECORD(e, SECTION, WindowLinks);
        if (s == copy || (s->Flags & SEC_IMAGE) || s->Redirect || s->FileVolume != volume || s->FileId != file_id || !s->FileId)
            continue;
        ObReferenceObject(copy);
        __atomic_store_n(&s->Redirect, copy, __ATOMIC_RELEASE);
        ObReferenceObject(s);
        found[count++] = s;
    }
    KiReleaseDispatcherLock(state);
    /* what the views already show of the file's pages is touched again, and finds the copy. */
    for (ULONG i = 0; i < count; i++) {
        take_out_everywhere(found[i]->SourcePa, found[i]->SourcePa + ROUND_TO_PAGES(found[i]->SourceSize), found[i]);
        ObDereferenceObject(found[i]);
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
            take_out_everywhere(begin, end, nullptr);
            acked = g_io.serial;
        }
    }
}

void MmWindowLostInitialize(void)
{
    KeInitializeEvent(&g_event, SynchronizationEvent, FALSE);
    PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, worker, nullptr);
}

/* called from the session interrupt on MIME_IRQ_WINDOW. */
void MmWindowLostInterrupt(void)
{
    KeSetEvent(&g_event, IO_NO_INCREMENT, FALSE);
}

/* sets the status the exception after a fault on a lost page reports. */
void MmWindowLostFault(void)
{
    KiCurrentThread()->FaultStatus = STATUS_FILE_INVALID_VALUE;
}
