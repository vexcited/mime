/*
 * user address spaces: vads, commit state, page faults and the virtual
 * memory system services.
 *
 * reservations are tracked by vads; commit state lives in the page
 * tables. a committed page that was never touched is an invalid pte
 * carrying MM_SWPTE_COMMITTED and its protection (like MMPTE_SOFTWARE).
 */
#include "../ntos.h"

#define MM_LOWEST_USER_ADDRESS 0x10000ull
#define MM_HIGHEST_USER_ADDRESS 0x7FFFFFFEFFFFull
#define MM_ALLOCATION_GRANULARITY 0x10000ull
#define MM_TOP_DOWN_START 0x7FF000000000ull

static ULONG64 g_kuser_page;

void MiSetKuserPage(ULONG64 pa)
{
    g_kuser_page = pa;
}

static inline ULONG64 sw_pte(ULONG protect)
{
    return MM_SWPTE_COMMITTED | ((ULONG64)(protect & 0x7FF) << MM_SWPTE_PROT_SHIFT);
}

static inline BOOLEAN pte_committed(ULONG64 pte)
{
    return (pte & MM_PTE_VALID) || (pte & MM_SWPTE_COMMITTED);
}

static BOOLEAN valid_protect(ULONG protect)
{
    switch (protect & 0xFF) {
    case PAGE_NOACCESS:
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return TRUE;
    default:
        return FALSE;
    }
}

NTSTATUS MmCreateAddressSpace(PMMADDRESS_SPACE as)
{
    InitializeListHead(&as->VadList);
    ExInitializeFastMutex(&as->Lock);
    as->RootPhys = MmAllocatePage();
    if (!as->RootPhys)
        return STATUS_INSUFFICIENT_RESOURCES;
    as->Root = HalPhysmap(as->RootPhys);
    MiInitializeProcessRoot(as->Root);
    PMMVAD vad = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MMVAD), KI_TAG('M', 'm', 'V', 'd'));
    if (!vad)
        return STATUS_INSUFFICIENT_RESOURCES;
    vad->StartVa = MIME_USER_KUSER_SHARED;
    vad->EndVa = MIME_USER_KUSER_SHARED + PAGE_SIZE;
    vad->Type = MiVadShared;
    vad->InitialProtect = PAGE_READONLY;
    MiInsertVad(as, vad);
    if (g_kuser_page)
        MiMapPage(as->Root, MIME_USER_KUSER_SHARED, g_kuser_page,
                  MiProtectToPte(PAGE_READONLY, TRUE) | MM_PTE_SW_SHARED);
    return STATUS_SUCCESS;
}

PMMVAD MiFindVad(PMMADDRESS_SPACE as, ULONG_PTR va)
{
    if (as->Hint && va >= as->Hint->StartVa && va < as->Hint->EndVa)
        return as->Hint;
    for (PLIST_ENTRY e = as->VadList.Flink; e != &as->VadList; e = e->Flink) {
        PMMVAD v = CONTAINING_RECORD(e, MMVAD, Links);
        if (va < v->StartVa)
            return nullptr;
        if (va < v->EndVa) {
            as->Hint = v;
            return v;
        }
    }
    return nullptr;
}

BOOLEAN MiRangeIsFree(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    if (start < MM_LOWEST_USER_ADDRESS || end > MM_HIGHEST_USER_ADDRESS + 1 || end <= start)
        return FALSE;
    for (PLIST_ENTRY e = as->VadList.Flink; e != &as->VadList; e = e->Flink) {
        PMMVAD v = CONTAINING_RECORD(e, MMVAD, Links);
        if (v->StartVa >= end)
            break;
        if (v->EndVa > start)
            return FALSE;
    }
    return TRUE;
}

NTSTATUS MiInsertVad(PMMADDRESS_SPACE as, PMMVAD vad)
{
    PLIST_ENTRY e = as->VadList.Flink;
    while (e != &as->VadList) {
        PMMVAD v = CONTAINING_RECORD(e, MMVAD, Links);
        if (v->StartVa >= vad->EndVa)
            break;
        e = e->Flink;
    }
    vad->Links.Flink = e;
    vad->Links.Blink = e->Blink;
    e->Blink->Flink = &vad->Links;
    e->Blink = &vad->Links;
    return STATUS_SUCCESS;
}

void MiRemoveVad(PMMADDRESS_SPACE as, PMMVAD vad)
{
    if (as->Hint == vad)
        as->Hint = nullptr;
    RemoveEntryList(&vad->Links);
}

NTSTATUS MiFindFreeRange(PMMADDRESS_SPACE as, SIZE_T size, ULONG_PTR alignment, BOOLEAN top_down, ULONG_PTR *out)
{
    ULONG_PTR maximum = as->AllocationLimit ? as->AllocationLimit : MM_HIGHEST_USER_ADDRESS + 1;
    if (size > maximum - MM_LOWEST_USER_ADDRESS)
        return STATUS_NO_MEMORY;
    if (!alignment)
        alignment = MM_ALLOCATION_GRANULARITY;
    if (top_down) {
        ULONG_PTR limit = maximum < MM_TOP_DOWN_START ? maximum : MM_TOP_DOWN_START;
        for (PLIST_ENTRY e = as->VadList.Blink;; e = e->Blink) {
            ULONG_PTR low = MM_LOWEST_USER_ADDRESS;
            if (e != &as->VadList) {
                PMMVAD v = CONTAINING_RECORD(e, MMVAD, Links);
                if (v->StartVa >= limit)
                    continue;
                low = v->EndVa;
            }
            if (limit > size) {
                ULONG_PTR candidate = (limit - size) & ~(alignment - 1);
                if (candidate >= low && candidate >= MM_LOWEST_USER_ADDRESS) {
                    *out = candidate;
                    return STATUS_SUCCESS;
                }
            }
            if (e == &as->VadList)
                break;
            limit = CONTAINING_RECORD(e, MMVAD, Links)->StartVa;
        }
        return STATUS_NO_MEMORY;
    }
    ULONG_PTR candidate = MM_LOWEST_USER_ADDRESS;
    for (PLIST_ENTRY e = as->VadList.Flink; e != &as->VadList; e = e->Flink) {
        PMMVAD v = CONTAINING_RECORD(e, MMVAD, Links);
        if (candidate + size <= v->StartVa)
            break;
        if (v->EndVa > candidate)
            candidate = (v->EndVa + alignment - 1) & ~(alignment - 1);
    }
    if (candidate > maximum - size)
        return STATUS_NO_MEMORY;
    *out = candidate;
    return STATUS_SUCCESS;
}

/* frees the private pages of a range and clears its ptes. */
static void release_pages(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    for (ULONG_PTR va = start; va < end; va += PAGE_SIZE) {
        ULONG64 *pte = MiWalk(as->Root, va, FALSE, 3);
        if (!pte) {
            /* skip to the next l3 table boundary. */
            va = ((va + (1ull << 21)) & ~((1ull << 21) - 1)) - PAGE_SIZE;
            continue;
        }
        ULONG64 old = *pte;
        if (!old)
            continue;
        *pte = 0;
        if (old & MM_PTE_VALID) {
            MiFlushTlbPage(va, as->Asid, FALSE);
            if (!(old & MM_PTE_SW_SHARED))
                MmFreePage(old & MM_PTE_ADDR_MASK);
            as->CommittedPages--;
        }
    }
}

static void free_tables(ULONG64 *table, int level)
{
    /* the kernel half of an x64 root belongs to every process. */
    for (int i = 0; i < (level ? 512 : MI_USER_ROOT_ENTRIES); i++) {
        ULONG64 e = table[i];
        if (!(e & MM_PTE_VALID) || !MI_PTE_IS_TABLE(e, level))
            continue;
        ULONG64 *child = HalPhysmap(e & MM_PTE_ADDR_MASK);
        if (level < 2)
            free_tables(child, level + 1);
        MmFreePage(e & MM_PTE_ADDR_MASK);
    }
}

void MmDeleteAddressSpace(PMMADDRESS_SPACE as)
{
    if (!as->Root)
        return;
    while (!IsListEmpty(&as->VadList)) {
        PMMVAD v = CONTAINING_RECORD(as->VadList.Flink, MMVAD, Links);
        release_pages(as, v->StartVa, v->EndVa);
        MiRemoveVad(as, v);
        if (v->Section)
            ObDereferenceObject(v->Section);
        ExFreePool(v);
    }
    free_tables(as->Root, 0);
    MmFreePage(as->RootPhys);
    MiFlushTlbAsid(as->Asid);
    as->Root = nullptr;
    as->RootPhys = 0;
}

static NTSTATUS commit_range(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end, ULONG protect)
{
    for (ULONG_PTR va = start; va < end; va += PAGE_SIZE) {
        ULONG64 *pte = MiWalk(as->Root, va, TRUE, 3);
        if (!pte)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (*pte & MM_PTE_VALID) {
            ULONG64 attrs = MiProtectToPte(protect, TRUE) | (*pte & MM_PTE_SW_SHARED);
            if ((*pte & MM_PTE_SW_SHARED) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
                attrs = MI_PTE_SET_RO(attrs) | MM_PTE_SW_COW;
            MiWritePte(as->Root, va, (*pte & MM_PTE_ADDR_MASK) | attrs | MM_PTE_VALID | MM_PTE_PAGE, as->Asid);
        } else {
            *pte = sw_pte(protect) | (*pte & MM_SWPTE_SECTION);
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS MiAllocateVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG type, ULONG protect)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    if (!(type & (MEM_COMMIT | MEM_RESERVE | MEM_RESET)) || !valid_protect(protect & ~(PAGE_GUARD | PAGE_NOCACHE)))
        return STATUS_INVALID_PARAMETER;
    if (type & MEM_RESET)
        return STATUS_SUCCESS;
    if (*size == 0)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&as->Lock);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR start = (ULONG_PTR)*base;
    if ((type & MEM_RESERVE) || !start) {
        SIZE_T bytes = ROUND_TO_PAGES(*size + (start & (PAGE_SIZE - 1)));
        if (start) {
            start &= ~(MM_ALLOCATION_GRANULARITY - 1);
            bytes = ROUND_TO_PAGES((ULONG_PTR)*base + *size) - start;
            if (!MiRangeIsFree(as, start, start + bytes)) {
                ExReleaseFastMutex(&as->Lock);
                return STATUS_CONFLICTING_ADDRESSES;
            }
        } else {
            status = MiFindFreeRange(as, bytes, MM_ALLOCATION_GRANULARITY, (type & MEM_TOP_DOWN) != 0, &start);
            if (!NT_SUCCESS(status)) {
                ExReleaseFastMutex(&as->Lock);
                return status;
            }
        }
        PMMVAD vad = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MMVAD), KI_TAG('M', 'm', 'V', 'd'));
        if (!vad) {
            ExReleaseFastMutex(&as->Lock);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        vad->StartVa = start;
        vad->EndVa = start + bytes;
        vad->Type = MiVadPrivate;
        vad->InitialProtect = protect;
        MiInsertVad(as, vad);
        if (type & MEM_COMMIT)
            status = commit_range(as, start, start + bytes, protect);
        *base = (PVOID)start;
        *size = bytes;
    } else {
        ULONG_PTR end = ROUND_TO_PAGES(start + *size);
        start &= ~(ULONG_PTR)(PAGE_SIZE - 1);
        PMMVAD vad = MiFindVad(as, start);
        if (!vad || end > vad->EndVa || vad->Type == MiVadShared) {
            ExReleaseFastMutex(&as->Lock);
            return STATUS_CONFLICTING_ADDRESSES;
        }
        status = commit_range(as, start, end, protect);
        *base = (PVOID)start;
        *size = end - start;
    }
    ExReleaseFastMutex(&as->Lock);
    return status;
}

NTSTATUS MiFreeVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG type)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    ULONG_PTR start = (ULONG_PTR)*base & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ExAcquireFastMutex(&as->Lock);
    PMMVAD vad = MiFindVad(as, start);
    if (!vad || vad->Type != MiVadPrivate) {
        ExReleaseFastMutex(&as->Lock);
        return STATUS_MEMORY_NOT_ALLOCATED;
    }
    NTSTATUS status = STATUS_SUCCESS;
    if (type & MEM_RELEASE) {
        if (*size != 0 && start == vad->StartVa && ROUND_TO_PAGES(start + *size) < vad->EndVa) {
            /* partial release: split off the tail as a new reservation. */
            ULONG_PTR split = ROUND_TO_PAGES(start + *size);
            PMMVAD tail = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MMVAD), KI_TAG('M', 'm', 'V', 'd'));
            if (!tail) {
                ExReleaseFastMutex(&as->Lock);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            *tail = *vad;
            tail->StartVa = split;
            release_pages(as, start, split);
            vad->EndVa = split;
            MiRemoveVad(as, vad);
            MiInsertVad(as, tail);
            *size = split - start;
            ExFreePool(vad);
        } else if (start != vad->StartVa) {
            status = STATUS_FREE_VM_NOT_AT_BASE;
        } else {
            *size = vad->EndVa - vad->StartVa;
            release_pages(as, vad->StartVa, vad->EndVa);
            MiRemoveVad(as, vad);
            ExFreePool(vad);
        }
        *base = (PVOID)start;
    } else if (type & MEM_DECOMMIT) {
        ULONG_PTR end = *size ? ROUND_TO_PAGES((ULONG_PTR)*base + *size) : vad->EndVa;
        if (end > vad->EndVa)
            end = vad->EndVa;
        release_pages(as, start, end);
        *base = (PVOID)start;
        *size = end - start;
    } else {
        status = STATUS_INVALID_PARAMETER;
    }
    ExReleaseFastMutex(&as->Lock);
    return status;
}

NTSTATUS MiProtectVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG protect, PULONG old)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    if (!valid_protect(protect & ~(PAGE_GUARD | PAGE_NOCACHE)))
        return STATUS_INVALID_PAGE_PROTECTION;
    ULONG_PTR start = (ULONG_PTR)*base & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ULONG_PTR end = ROUND_TO_PAGES((ULONG_PTR)*base + *size);
    ExAcquireFastMutex(&as->Lock);
    PMMVAD vad = MiFindVad(as, start);
    if (!vad || end > vad->EndVa) {
        ExReleaseFastMutex(&as->Lock);
        return STATUS_CONFLICTING_ADDRESSES;
    }
    ULONG first_old = 0;
    for (ULONG_PTR va = start; va < end; va += PAGE_SIZE) {
        ULONG64 *pte = MiWalk(as->Root, va, FALSE, 3);
        ULONG64 v = pte ? *pte : 0;
        if (!pte_committed(v)) {
            ExReleaseFastMutex(&as->Lock);
            return STATUS_NOT_COMMITTED;
        }
        if (va == start)
            first_old = MiPteToProtect(v);
        if (v & MM_PTE_VALID) {
            ULONG64 attrs = MiProtectToPte(protect, TRUE) | (v & MM_PTE_SW_SHARED);
            /* writable private copies keep their data; shared pages become copy on write. */
            if ((v & MM_PTE_SW_SHARED) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
                attrs = MI_PTE_SET_RO(attrs) | MM_PTE_SW_COW;
            MiWritePte(as->Root, va, (v & MM_PTE_ADDR_MASK) | attrs | MM_PTE_VALID | MM_PTE_PAGE, as->Asid);
        } else {
            *pte = sw_pte(protect) | (v & MM_SWPTE_SECTION);
        }
    }
    ExReleaseFastMutex(&as->Lock);
    if (old)
        *old = first_old;
    *base = (PVOID)start;
    *size = end - start;
    return STATUS_SUCCESS;
}

NTSTATUS MiQueryVirtual(PEPROCESS process, PVOID base, PMEMORY_BASIC_INFORMATION info)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    ULONG_PTR va = (ULONG_PTR)base & ~(ULONG_PTR)(PAGE_SIZE - 1);
    if (va > MM_HIGHEST_USER_ADDRESS)
        return STATUS_INVALID_PARAMETER;
    memset(info, 0, sizeof(*info));
    ExAcquireFastMutex(&as->Lock);
    PMMVAD vad = MiFindVad(as, va);
    if (!vad) {
        ULONG_PTR next = MM_HIGHEST_USER_ADDRESS + 1;
        for (PLIST_ENTRY e = as->VadList.Flink; e != &as->VadList; e = e->Flink) {
            PMMVAD v = CONTAINING_RECORD(e, MMVAD, Links);
            if (v->StartVa > va) {
                next = v->StartVa;
                break;
            }
        }
        info->BaseAddress = (PVOID)va;
        info->RegionSize = next - va;
        info->State = MEM_FREE;
        info->Protect = PAGE_NOACCESS;
        ExReleaseFastMutex(&as->Lock);
        return STATUS_SUCCESS;
    }
    ULONG64 pte = MiReadPte(as->Root, va);
    BOOLEAN committed = pte_committed(pte);
    ULONG prot = committed ? MiPteToProtect(pte) : 0;
    ULONG_PTR end = va + PAGE_SIZE;
    while (end < vad->EndVa) {
        ULONG64 p = MiReadPte(as->Root, end);
        if (pte_committed(p) != committed || (committed && MiPteToProtect(p) != prot))
            break;
        end += PAGE_SIZE;
    }
    info->BaseAddress = (PVOID)va;
    info->AllocationBase = (PVOID)vad->StartVa;
    info->AllocationProtect = vad->InitialProtect;
    info->RegionSize = end - va;
    info->State = committed ? MEM_COMMIT : MEM_RESERVE;
    info->Protect = prot;
    info->Type = vad->Type == MiVadImage ? MEM_IMAGE : vad->Type == MiVadPrivate ? MEM_PRIVATE : MEM_MAPPED;
    ExReleaseFastMutex(&as->Lock);
    return STATUS_SUCCESS;
}

/* maps the first touched page of a section view: the prototype page, or a private copy of a writable image page. */
static BOOLEAN section_fault(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR page, ULONG64 *pte, ULONG protect)
{
    PSECTION s = vad->Section;
    ULONG64 index = (vad->SectionOffset + (page - vad->StartVa)) >> PAGE_SHIFT;
    ULONG64 pa = index < s->PageCount ? MiSectionPage(s, (ULONG)index) : 0;
    if (!pa)
        return FALSE;
    BOOLEAN shared = TRUE;
    if ((s->Flags & SEC_IMAGE) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
        ULONG64 copy = MmAllocatePageNoZero();
        if (!copy)
            return FALSE;
        memcpy(HalPhysmap(copy), HalPhysmap(pa), PAGE_SIZE);
        pa = copy;
        shared = FALSE;
    }
    *pte = pa | MiSectionPteAttrs(protect, pa, shared) | MM_PTE_VALID | MM_PTE_PAGE;
    KiDataSyncBarrier();
    KiInstructionBarrier();
    return TRUE;
}

static BOOLEAN map_zero_page(PMMADDRESS_SPACE as, ULONG64 *pte, ULONG64 v)
{
    ULONG protect = (ULONG)((v & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
    ULONG64 pa = MmAllocatePage();
    if (!pa)
        return FALSE;
    ULONG64 attrs = MiProtectToPte(protect, TRUE) & ~MM_PTE_SW_COW;
    if (protect & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
        attrs = MI_PTE_SET_RW(attrs);
    *pte = pa | attrs | MM_PTE_VALID | MM_PTE_PAGE;
    as->CommittedPages++;
    return TRUE;
}

/*
 * a demand zero fault maps the other committed pages of its window too:
 * the 16 KiB host page holding them is backed as a whole anyway, so this
 * saves faults without using more host memory. touching that goes on
 * where the last window ended gets a 64 KiB window, ahead of the program.
 */
#define FAULT_WINDOW (16 * 1024)
#define FAULT_WINDOW_SEQUENTIAL (64 * 1024)

static BOOLEAN demand_zero(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR page, ULONG64 *pte, ULONG64 v)
{
    if (!map_zero_page(as, pte, v))
        return FALSE;
    ULONG_PTR size = page >= as->LastFaultEnd && page < as->LastFaultEnd + FAULT_WINDOW ? FAULT_WINDOW_SEQUENTIAL
                                                                                          : FAULT_WINDOW;
    ULONG_PTR start = page & ~(size - 1), end = start + size;
    if (start < vad->StartVa)
        start = vad->StartVa;
    if (end > vad->EndVa)
        end = vad->EndVa;
    /* one translation table holds the whole window, so its ptes sit side by side. */
    ULONG64 *first = pte - ((page - start) >> PAGE_SHIFT);
    for (ULONG_PTR va = start; va < end; va += PAGE_SIZE) {
        ULONG64 *p = first + ((va - start) >> PAGE_SHIFT), w = *p;
        ULONG protect = (ULONG)((w & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
        if (va == page || (w & MM_PTE_VALID) || !(w & MM_SWPTE_COMMITTED) || (w & MM_SWPTE_SECTION) ||
            (protect & 0xFF) == PAGE_NOACCESS || (protect & PAGE_GUARD))
            continue;
        if (!map_zero_page(as, p, w))
            break;
    }
    as->LastFaultEnd = end;
    KiDataSyncBarrier();
    KiInstructionBarrier();
    return TRUE;
}

/* resolves demand zero, section and copy on write faults. returns FALSE for real access violations. */
BOOLEAN MmAccessFault(ULONG_PTR va, BOOLEAN write, BOOLEAN execute, KPROCESSOR_MODE mode)
{
    if (va > MM_HIGHEST_USER_ADDRESS)
        return FALSE;
    PEPROCESS process = (PEPROCESS)KiCurrentThread()->ApcStateProcess;
    PMMADDRESS_SPACE as = &process->AddressSpace;
    if (!as->Root)
        return FALSE;
    ULONG_PTR page = va & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ExAcquireFastMutex(&as->Lock);
    BOOLEAN handled = FALSE;
    PMMVAD vad = MiFindVad(as, page);
    ULONG64 *pte = vad ? MiWalk(as->Root, page, FALSE, 3) : nullptr;
    if (pte) {
        ULONG64 v = *pte;
        ULONG sw_protect = (ULONG)((v & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
        if (!(v & MM_PTE_VALID) && (v & MM_SWPTE_SECTION) && vad->Section && (sw_protect & 0xFF) != PAGE_NOACCESS &&
            !(sw_protect & PAGE_GUARD)) {
            handled = section_fault(as, vad, page, pte, sw_protect);
        } else if (!(v & MM_PTE_VALID) && (v & MM_SWPTE_COMMITTED)) {
            ULONG protect = (ULONG)((v & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
            if ((protect & 0xFF) != PAGE_NOACCESS && !(protect & PAGE_GUARD)) {
                handled = demand_zero(as, vad, page, pte, v);
            } else if (protect & PAGE_GUARD) {
                *pte = sw_pte(protect & ~PAGE_GUARD) | (v & MM_SWPTE_SECTION);
                handled = FALSE;
            }
        } else if ((v & MM_PTE_VALID) && write && (v & MM_PTE_SW_COW)) {
            ULONG64 pa = MmAllocatePageNoZero();
            if (pa) {
                memcpy(HalPhysmap(pa), HalPhysmap(v & MM_PTE_ADDR_MASK), PAGE_SIZE);
                ULONG64 attrs = MI_PTE_SET_RW(v & ~(MM_PTE_ADDR_MASK | MM_PTE_SW_COW | MM_PTE_SW_SHARED));
                MiWritePte(as->Root, page, pa | attrs, as->Asid);
                handled = TRUE;
            }
        } else if ((v & MM_PTE_VALID) && mode == UserMode && !MI_PTE_IS_USER(v) && MI_PTE_IS_GUARD(v)) {
            /* a guard page: the first touch faults and takes the guard away, like windows. */
            MiWritePte(as->Root, page, MI_PTE_CLEAR_GUARD(v), as->Asid);
            handled = FALSE;
        } else if ((v & MM_PTE_VALID) && (mode == KernelMode || MI_PTE_IS_USER(v)) &&
                   (!write || !MI_PTE_IS_RO(v)) && (!execute || MI_PTE_USER_EXEC(v))) {
            /* another thread, possibly on another processor, fixed the page up first. */
            handled = TRUE;
        }
    }
    ExReleaseFastMutex(&as->Lock);
    return handled;
}

/* -------------------------------------------------------- system services */

NTSTATUS NTAPI NtAllocateVirtualMemory(HANDLE process_handle, PVOID *base, ULONG_PTR zero_bits, PSIZE_T size,
                                       ULONG type, ULONG protect)
{
    PVOID b;
    SIZE_T s;
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) || !NT_SUCCESS(KiCopyFromUser(&s, size, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    status = MiAllocateVirtual(process, &b, &s, type, protect);
    ObDereferenceObject(process);
    if (NT_SUCCESS(status)) {
        KiCopyToUser(base, &b, sizeof(b));
        KiCopyToUser(size, &s, sizeof(s));
    }
    return status;
}

NTSTATUS NTAPI ZwAllocateVirtualMemory(HANDLE process, PVOID *base, ULONG_PTR zero_bits, PSIZE_T size, ULONG type,
                                       ULONG protect)
{
    PEPROCESS p;
    NTSTATUS status = ObReferenceHandle(process, PsProcessTypeObject, KernelMode, (PVOID *)&p, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    status = MiAllocateVirtual(p, base, size, type, protect);
    ObDereferenceObject(p);
    return status;
}

NTSTATUS NTAPI NtFreeVirtualMemory(HANDLE process_handle, PVOID *base, PSIZE_T size, ULONG type)
{
    PVOID b;
    SIZE_T s;
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) || !NT_SUCCESS(KiCopyFromUser(&s, size, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    status = MiFreeVirtual(process, &b, &s, type);
    ObDereferenceObject(process);
    if (NT_SUCCESS(status)) {
        KiCopyToUser(base, &b, sizeof(b));
        KiCopyToUser(size, &s, sizeof(s));
    }
    return status;
}

NTSTATUS NTAPI ZwFreeVirtualMemory(HANDLE process, PVOID *base, PSIZE_T size, ULONG type)
{
    PEPROCESS p;
    NTSTATUS status = ObReferenceHandle(process, PsProcessTypeObject, KernelMode, (PVOID *)&p, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    status = MiFreeVirtual(p, base, size, type);
    ObDereferenceObject(p);
    return status;
}

NTSTATUS NTAPI NtProtectVirtualMemory(HANDLE process_handle, PVOID *base, PSIZE_T size, ULONG protect, PULONG old)
{
    PVOID b;
    SIZE_T s;
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) || !NT_SUCCESS(KiCopyFromUser(&s, size, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    if (&process->Pcb != saved)
        KiAttachProcess(&process->Pcb);
    ULONG o = 0;
    status = MiProtectVirtual(process, &b, &s, protect, &o);
    if (&process->Pcb != saved)
        KiAttachProcess(saved);
    ObDereferenceObject(process);
    if (NT_SUCCESS(status)) {
        KiCopyToUser(base, &b, sizeof(b));
        KiCopyToUser(size, &s, sizeof(s));
        if (old)
            KiCopyToUser(old, &o, sizeof(o));
    }
    return status;
}

/*
 * the attributes of each page of a caller's list. committed pages count as
 * resident, at the normal priority 5, shared when not private memory.
 */
static NTSTATUS query_working_set_ex(PEPROCESS process, PVOID info, SIZE_T length)
{
    if (length < 16)
        return STATUS_INFO_LENGTH_MISMATCH;
    PUCHAR entry = info;
    for (SIZE_T i = 0; i < length / 16; i++, entry += 16) {
        ULONG_PTR va;
        NTSTATUS status = KiCopyFromUser(&va, entry, sizeof(va));
        if (!NT_SUCCESS(status))
            return status;
        MEMORY_BASIC_INFORMATION mbi;
        ULONG64 attributes = 0;
        if (va <= MM_HIGHEST_USER_ADDRESS && NT_SUCCESS(MiQueryVirtual(process, (PVOID)va, &mbi)) &&
            mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            attributes = 1 | (ULONG64)(mbi.Protect & 0x7FF) << 4 | 5ull << 24;
            if (mbi.Type != MEM_PRIVATE)
                attributes |= 1ull << 1 | 1ull << 15;
        }
        status = KiCopyToUser(entry + 8, &attributes, sizeof(attributes));
        if (!NT_SUCCESS(status))
            return status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryVirtualMemory(HANDLE process_handle, PVOID base, MEMORY_INFORMATION_CLASS klass, PVOID info,
                                    SIZE_T length, PSIZE_T returned)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_QUERY_INFORMATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    if (klass == MemoryBasicInformation) {
        MEMORY_BASIC_INFORMATION mbi;
        if (length < sizeof(mbi)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            status = MiQueryVirtual(process, base, &mbi);
            if (NT_SUCCESS(status))
                status = KiCopyToUser(info, &mbi, sizeof(mbi));
            SIZE_T n = sizeof(mbi);
            if (returned)
                KiCopyToUser(returned, &n, sizeof(n));
        }
    } else if (klass == MemoryImageInformation) {
        /* the image view holding the address; zeros for any other memory, as windows answers. */
        MEMORY_IMAGE_INFORMATION mi = {};
        PMMADDRESS_SPACE as = &process->AddressSpace;
        if (length < sizeof(mi)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            ExAcquireFastMutex(&as->Lock);
            PMMVAD vad = (ULONG_PTR)base <= MM_HIGHEST_USER_ADDRESS ? MiFindVad(as, (ULONG_PTR)base) : nullptr;
            if (!vad) {
                status = STATUS_INVALID_ADDRESS;
            } else if (vad->Type == MiVadImage && vad->Section) {
                mi.ImageBase = (PVOID)vad->StartVa;
                mi.SizeOfImage = (SIZE_T)vad->Section->Size;
                if (vad->SectionOffset + (vad->EndVa - vad->StartVa) < vad->Section->Size)
                    mi.ImageFlags |= MEMORY_IMAGE_PARTIAL_MAP;
            }
            ExReleaseFastMutex(&as->Lock);
            if (NT_SUCCESS(status)) {
                status = KiCopyToUser(info, &mi, sizeof(mi));
                SIZE_T n = sizeof(mi);
                if (returned)
                    KiCopyToUser(returned, &n, sizeof(n));
            }
        }
    } else if (klass == MemoryWorkingSetExInformation) {
        status = query_working_set_ex(process, info, length);
    } else if (klass == MemoryMappedFilenameInformation) {
        PMMADDRESS_SPACE as = &process->AddressSpace;
        ExAcquireFastMutex(&as->Lock);
        PMMVAD vad = MiFindVad(as, (ULONG_PTR)base);
        PSECTION section = vad ? vad->Section : nullptr;
        if (section)
            ObReferenceObject(section);
        ExReleaseFastMutex(&as->Lock);
        if (!section || !section->FileName.Buffer) {
            status = STATUS_INVALID_ADDRESS;
        } else {
            SIZE_T need = sizeof(UNICODE_STRING) + section->FileName.Length + sizeof(WCHAR);
            if (returned)
                KiCopyToUser(returned, &need, sizeof(need));
            if (length < need) {
                status = STATUS_BUFFER_OVERFLOW;
            } else {
                UNICODE_STRING us;
                us.Length = section->FileName.Length;
                us.MaximumLength = (USHORT)(us.Length + sizeof(WCHAR));
                us.Buffer = (PWSTR)((PUCHAR)info + sizeof(UNICODE_STRING));
                status = KiCopyToUser(info, &us, sizeof(us));
                if (NT_SUCCESS(status))
                    status = KiCopyToUser(us.Buffer, section->FileName.Buffer, us.Length + sizeof(WCHAR));
            }
        }
        if (section)
            ObDereferenceObject(section);
    } else {
        status = STATUS_INVALID_INFO_CLASS;
    }
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI NtReadVirtualMemory(HANDLE process_handle, PVOID base, PVOID buffer, SIZE_T size, PSIZE_T read)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_VM_READ, &process);
    if (!NT_SUCCESS(status))
        return status;
    SIZE_T done = 0;
    if (process == PsCurrentProcess()) {
        PUCHAR tmp = ExAllocatePool2(POOL_FLAG_PAGED, size ? size : 1, KI_TAG('M', 'm', 'R', 'v'));
        if (!tmp) {
            ObDereferenceObject(process);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        status = KiCopyFromUser(tmp, base, size);
        if (NT_SUCCESS(status))
            status = KiCopyToUser(buffer, tmp, size);
        if (NT_SUCCESS(status))
            done = size;
        ExFreePool(tmp);
    } else {
        status = MmCopyVirtualMemory(process, base, PsCurrentProcess(), buffer, size, UserMode, &done);
    }
    if (read)
        KiCopyToUser(read, &done, sizeof(done));
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI NtWriteVirtualMemory(HANDLE process_handle, PVOID base, PVOID buffer, SIZE_T size, PSIZE_T written)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_VM_WRITE, &process);
    if (!NT_SUCCESS(status))
        return status;
    SIZE_T done = 0;
    status = MmCopyVirtualMemory(PsCurrentProcess(), buffer, process, base, size, UserMode, &done);
    if (written)
        KiCopyToUser(written, &done, sizeof(done));
    ObDereferenceObject(process);
    return status;
}

/* copies between address spaces through a kernel bounce buffer. */
NTSTATUS NTAPI MmCopyVirtualMemory(PEPROCESS from_process, PVOID from, PEPROCESS to_process, PVOID to, SIZE_T size,
                                   KPROCESSOR_MODE mode, PSIZE_T copied)
{
    PUCHAR tmp = ExAllocatePool2(POOL_FLAG_PAGED, 4096, KI_TAG('M', 'm', 'C', 'v'));
    if (!tmp)
        return STATUS_INSUFFICIENT_RESOURCES;
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T done = 0;
    while (done < size) {
        SIZE_T n = size - done < 4096 ? size - done : 4096;
        KiAttachProcess(&from_process->Pcb);
        status = KiCopyFromUser(tmp, (PUCHAR)from + done, n);
        KiAttachProcess(&to_process->Pcb);
        if (NT_SUCCESS(status))
            status = KiCopyToUser((PUCHAR)to + done, tmp, n);
        if (!NT_SUCCESS(status))
            break;
        done += n;
    }
    KiAttachProcess(saved);
    ExFreePool(tmp);
    if (copied)
        *copied = done;
    return status == STATUS_ACCESS_VIOLATION ? (NTSTATUS)0x8000000D /* STATUS_PARTIAL_COPY */ : status;
}

NTSTATUS NTAPI NtFlushInstructionCache(HANDLE process, PVOID base, SIZE_T size)
{
    KiFlushInstructionCache();
    return STATUS_SUCCESS;
}

/*
 * every processor running this process in user mode takes an interrupt,
 * and its entry into the kernel publishes its stores; the caller waits
 * for each to leave user mode.
 */
NTSTATUS NTAPI NtFlushProcessWriteBuffers(void)
{
    PKPROCESS process = KiCurrentThread()->Process;
    ULONG64 targets = 0;
    KiDataSyncBarrier();
    for (ULONG i = 0; i < KeNumberProcessors; i++) {
        PKPRCB p = KiProcessorBlock[i];
        PKPCR pcr = CONTAINING_RECORD(p, KPCR, Prcb);
        if (p != KeGetCurrentPrcb() && pcr->UserMode && p->CurrentThread->Process == process) {
            targets |= 1ull << i;
            HalSendIpi(i);
        }
    }
    while (targets) {
        ULONG i = (ULONG)__builtin_ctzll(targets);
        PKPCR pcr = CONTAINING_RECORD(KiProcessorBlock[i], KPCR, Prcb);
        if (!__atomic_load_n(&pcr->UserMode, __ATOMIC_ACQUIRE) || KiProcessorBlock[i]->CurrentThread->Process != process)
            targets &= targets - 1;
        else
            KiPause();
    }
    KiDataSyncBarrier();
    return STATUS_SUCCESS;
}
