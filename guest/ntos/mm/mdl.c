#include "../ntos.h"

/* the page frame numbers follow the header, one per page the buffer spans. */
#define MDL_PFNS(mdl) ((PULONG64)((PMDL)(mdl) + 1))
#define MDL_PAGES(mdl) ADDRESS_AND_SIZE_TO_SPAN_PAGES((PUCHAR)(mdl)->StartVa + (mdl)->ByteOffset, (mdl)->ByteCount)

static void initialize_mdl(PMDL mdl, PVOID va, SIZE_T length)
{
    mdl->Next = nullptr;
    mdl->Size = (CSHORT)(sizeof(MDL) + sizeof(ULONG64) * ADDRESS_AND_SIZE_TO_SPAN_PAGES(va, length));
    mdl->MdlFlags = 0;
    mdl->Process = nullptr;
    mdl->MappedSystemVa = nullptr;
    mdl->StartVa = PAGE_ALIGN(va);
    mdl->ByteCount = (ULONG)length;
    mdl->ByteOffset = BYTE_OFFSET(va);
}

PMDL NTAPI IoAllocateMdl(PVOID va, ULONG length, BOOLEAN secondary, BOOLEAN charge, PIRP irp)
{
    SIZE_T pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(va, length);
    if (!length || pages > 0x2000)
        return nullptr;
    PMDL mdl = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MDL) + pages * sizeof(ULONG64), KI_TAG('I', 'o', 'M', 'd'));
    if (!mdl)
        return nullptr;
    initialize_mdl(mdl, va, length);
    if (irp) {
        if (!secondary) {
            irp->MdlAddress = mdl;
        } else {
            PMDL last = irp->MdlAddress;
            while (last && last->Next)
                last = last->Next;
            if (last)
                last->Next = mdl;
            else
                irp->MdlAddress = mdl;
        }
    }
    return mdl;
}

void NTAPI IoFreeMdl(PMDL mdl)
{
    ExFreePool(mdl);
}

SIZE_T NTAPI MmSizeOfMdl(PVOID base, SIZE_T length)
{
    return sizeof(MDL) + sizeof(ULONG64) * ADDRESS_AND_SIZE_TO_SPAN_PAGES(base, length);
}

PMDL NTAPI MmCreateMdl(PMDL mdl, PVOID base, SIZE_T length)
{
    if (!mdl) {
        mdl = ExAllocatePool2(POOL_FLAG_NON_PAGED, MmSizeOfMdl(base, length), KI_TAG('I', 'o', 'M', 'd'));
        if (!mdl)
            return nullptr;
    }
    initialize_mdl(mdl, base, length);
    return mdl;
}

/* the frame behind a mapped address of the kernel or of the given address space, or 0. */
static ULONG64 frame_of(ULONG64 *root, ULONG_PTR va)
{
    ULONG64 pte = MiReadPte(root, va);
    if (!(pte & MM_PTE_VALID))
        return 0;
    return (pte & MM_PTE_ADDR_MASK) >> PAGE_SHIFT;
}

void NTAPI MmBuildMdlForNonPagedPool(PMDL mdl)
{
    PUCHAR va = mdl->StartVa;
    SIZE_T pages = MDL_PAGES(mdl);
    for (SIZE_T i = 0; i < pages; i++)
        MDL_PFNS(mdl)[i] = frame_of(MiKernelRoot(), (ULONG_PTR)va + i * PAGE_SIZE);
    mdl->MappedSystemVa = va + mdl->ByteOffset;
    mdl->MdlFlags |= MDL_SOURCE_IS_NONPAGED_POOL;
}

/*
 * the pages are made present, written once for an operation that writes, and their frames recorded. a page that
 * cannot be raises STATUS_ACCESS_VIOLATION, which callers catch, as on windows.
 */
void NTAPI MmProbeAndLockPages(PMDL mdl, KPROCESSOR_MODE mode, LOCK_OPERATION operation)
{
    PUCHAR va = mdl->StartVa;
    SIZE_T pages = MDL_PAGES(mdl);
    BOOLEAN user = (ULONG_PTR)va <= MIME_USER_ADDRESS_LIMIT;
    if (user && (ULONG_PTR)va + (SIZE_T)mdl->ByteOffset + mdl->ByteCount > MmUserProbeAddress)
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    if (!user && mode == UserMode)
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    PEPROCESS process = user ? PsGetCurrentProcess() : nullptr;
    ULONG64 *root = user ? process->AddressSpace.Root : MiKernelRoot();
    for (SIZE_T i = 0; i < pages; i++) {
        volatile UCHAR *page = va + i * PAGE_SIZE + (i ? 0 : mdl->ByteOffset);
        UCHAR value = *page;
        if (operation != IoReadAccess && user)
            *page = value;
        ULONG64 frame = frame_of(root, (ULONG_PTR)page);
        if (!frame)
            ExRaiseStatus(STATUS_ACCESS_VIOLATION);
        MDL_PFNS(mdl)[i] = frame;
    }
    mdl->Process = process;
    mdl->MdlFlags |= MDL_PAGES_LOCKED;
}

void NTAPI MmUnlockPages(PMDL mdl)
{
    if (mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
        MmUnmapLockedPages(mdl->MappedSystemVa, mdl);
    mdl->MdlFlags &= ~MDL_PAGES_LOCKED;
}

/* a second mapping of the frames in system space, writable whatever the first one is. */
PVOID NTAPI MmMapLockedPagesSpecifyCache(PMDL mdl, KPROCESSOR_MODE mode, MEMORY_CACHING_TYPE cache, PVOID address,
                                         ULONG bugcheck, ULONG priority)
{
    if (mode != KernelMode) {
        KiLogPrintf("MmMapLockedPagesSpecifyCache: user mode mappings are not supported");
        return nullptr;
    }
    if (mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL))
        return mdl->MappedSystemVa;
    SIZE_T pages = MDL_PAGES(mdl);
    ULONG64 frames[64], *list = pages <= RTL_NUMBER_OF(frames) ? frames : ExAllocatePool2(POOL_FLAG_NON_PAGED, pages * 8, KI_TAG('M', 'm', 'M', 'l'));
    if (!list)
        return nullptr;
    for (SIZE_T i = 0; i < pages; i++)
        list[i] = MDL_PFNS(mdl)[i] << PAGE_SHIFT;
    PUCHAR va = MmMapPagesInSystem(list, pages, MM_KERNEL_RW);
    if (list != frames)
        ExFreePool(list);
    if (!va) {
        if (bugcheck)
            KeBugCheckEx(0x00000041, (ULONG_PTR)mdl, pages, 0, 0); /* MUST_SUCCEED_POOL_EMPTY */
        return nullptr;
    }
    mdl->MappedSystemVa = va + mdl->ByteOffset;
    mdl->MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;
    return mdl->MappedSystemVa;
}

PVOID NTAPI MmMapLockedPages(PMDL mdl, KPROCESSOR_MODE mode)
{
    return MmMapLockedPagesSpecifyCache(mdl, mode, MmCached, nullptr, TRUE, 16);
}

void NTAPI MmUnmapLockedPages(PVOID base, PMDL mdl)
{
    if (!(mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA) || base != mdl->MappedSystemVa)
        return;
    PUCHAR va = PAGE_ALIGN(base);
    SIZE_T pages = MDL_PAGES(mdl);
    for (SIZE_T i = 0; i < pages; i++)
        MiUnmapPage(MiKernelRoot(), (ULONG_PTR)va + i * PAGE_SIZE, 0);
    MmFreeSystemVa(va, pages);
    mdl->MdlFlags &= ~MDL_MAPPED_TO_SYSTEM_VA;
    mdl->MappedSystemVa = nullptr;
}

NTSTATUS NTAPI MmProtectMdlSystemAddress(PMDL mdl, ULONG protect)
{
    if (!(mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA))
        return STATUS_INVALID_PARAMETER;
    ULONG64 attrs = protect == PAGE_READONLY ? MM_KERNEL_RO : protect == PAGE_READWRITE ? MM_KERNEL_RW : 0;
    if (!attrs)
        return STATUS_INVALID_PAGE_PROTECTION;
    PUCHAR va = PAGE_ALIGN(mdl->MappedSystemVa);
    for (SIZE_T i = 0; i < MDL_PAGES(mdl); i++)
        MiMapPage(MiKernelRoot(), (ULONG_PTR)va + i * PAGE_SIZE, MDL_PFNS(mdl)[i] << PAGE_SHIFT, attrs);
    return STATUS_SUCCESS;
}
