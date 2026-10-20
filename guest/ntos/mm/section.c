/*
 * section objects: image sections built from pe files, file backed and
 * pagefile backed data sections, and mapping views of them.
 *
 * an image section keeps one prototype copy of the image laid out at its
 * preferred base. views at that base share read-only pages; writable
 * pages are private copies. relocated views get private copies of every
 * page with the base relocations applied.
 *
 * prototype pages are built on first touch from the file's read only
 * pages, the rom for the system files and the host window for the others
 * (IoGetFileWindow). a page is the file's own page whenever the file page
 * is byte for byte the loaded page, which the 4K file alignment of mime's
 * images makes the common case. pagefile backed pages start as demand
 * zero. images of the system files are built once and cached.
 */
#include "../ntos.h"

POBJECT_TYPE MmSectionType;
POBJECT_TYPE *MmSectionObjectType = &MmSectionType;

static void forget_file_image(PSECTION s);

static void section_delete(PVOID object)
{
    PSECTION s = object;
    forget_file_image(s);
    if (s->SystemVa) {
        for (ULONG i = 0; i < s->PageCount; i++)
            MiUnmapPage(MiKernelRoot(), (ULONG_PTR)s->SystemVa + (ULONG_PTR)i * PAGE_SIZE, 0);
        MmFreeSystemVa(s->SystemVa, s->PageCount);
    }
    if (s->Pages) {
        /* rom and window pages are not ram and have no pfn. */
        for (ULONG i = 0; i < s->PageCount && !(s->Flags & MI_SECTION_PHYSICAL); i++)
            if (s->Pages[i] && MiGetPfn(s->Pages[i]))
                MmFreePage(s->Pages[i]);
        ExFreePool(s->Pages);
    }
    if (s->RelocPages) {
        for (ULONG i = 0; i < s->PageCount; i++)
            if (s->RelocPages[i])
                MmFreePage(s->RelocPages[i]);
        ExFreePool(s->RelocPages);
    }
    if (s->RelocIndex)
        ExFreePool(s->RelocIndex);
    if (s->PageProtect)
        ExFreePool(s->PageProtect);
    MmWindowSectionDetach(s);
    IoReleaseFileWindow(s->SourceRef);
    if (s->File)
        ObDereferenceObject(s->File);
    if (s->FileName.Buffer)
        ExFreePool(s->FileName.Buffer);
}

/* a section whose pages are all absent until built by MiSectionPage. */
static NTSTATUS alloc_section(ULONG64 size, PSECTION *out)
{
    PSECTION s;
    NTSTATUS status = ObCreateObject(MmSectionType, nullptr, KernelMode, sizeof(SECTION), (PVOID *)&s);
    if (!NT_SUCCESS(status))
        return status;
    memset(s, 0, sizeof(*s));
    s->Size = size;
    s->PageCount = (ULONG)BYTES_TO_PAGES(size);
    s->Pages = ExAllocatePool2(POOL_FLAG_NON_PAGED, (s->PageCount + 1) * sizeof(ULONG64), KI_TAG('M', 'm', 'S', 'p'));
    if (!s->Pages) {
        ObDereferenceObject(s);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    *out = s;
    return STATUS_SUCCESS;
}

static IMAGE_DATA_DIRECTORY image_directory(const IMAGE_NT_HEADERS64 *nt, ULONG index)
{
    if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        const IMAGE_OPTIONAL_HEADER32 *opt = (const void *)&nt->OptionalHeader;
        return index < opt->NumberOfRvaAndSizes ? opt->DataDirectory[index] : (IMAGE_DATA_DIRECTORY){0};
    }
    return index < nt->OptionalHeader.NumberOfRvaAndSizes ? nt->OptionalHeader.DataDirectory[index] : (IMAGE_DATA_DIRECTORY){0};
}

static void load_image_page(const UCHAR *file, ULONG64 file_size, const IMAGE_NT_HEADERS64 *nt, ULONG page, PUCHAR out);

/* builds prototype page `index`, returning a new ram page or a page of the source. */
static ULONG64 build_page(PSECTION s, ULONG index)
{
    ULONG64 off = (ULONG64)index << PAGE_SHIFT;
    ULONG64 source_pages = ROUND_TO_PAGES(s->SourceSize); /* the rom and windows pad files with zeros */
    if (!s->SourcePa || ((s->Flags & SEC_IMAGE) == 0 && off >= s->SourceSize))
        return MmAllocatePage();
    const UCHAR *src = HalPhysmap(s->SourcePa);
    if (!(s->Flags & SEC_IMAGE) && (s->Flags & (MI_SECTION_SHARE_SOURCE | MI_SECTION_WRITABLE_WINDOW)) &&
        off + PAGE_SIZE <= source_pages)
        return s->SourcePa + off;
    ULONG64 pa = MmAllocatePageNoZero();
    if (!pa)
        return 0;
    PUCHAR page = HalPhysmap(pa);
    if (s->Flags & SEC_IMAGE) {
        const IMAGE_NT_HEADERS64 *nt = (const void *)(src + ((const IMAGE_DOS_HEADER *)src)->e_lfanew);
        load_image_page(src, s->SourceSize, nt, index, page);
        if (off + PAGE_SIZE <= source_pages && memcmp(page, src + off, PAGE_SIZE) == 0) {
            MmFreePage(pa);
            return s->SourcePa + off;
        }
    } else {
        ULONG64 n = s->SourceSize - off < PAGE_SIZE ? s->SourceSize - off : PAGE_SIZE;
        memcpy(page, src + off, n);
        memset(page + n, 0, PAGE_SIZE - n);
    }
    return pa;
}

/* the prototype page `index` of a section, built on first use. 0 when out of memory. */
ULONG64 MiSectionPage(PSECTION s, ULONG index)
{
    ULONG64 pa = __atomic_load_n(&s->Pages[index], __ATOMIC_ACQUIRE);
    if (pa)
        return pa;
    pa = build_page(s, index);
    if (!pa)
        return 0;
    ULONG64 expected = 0;
    if (!__atomic_compare_exchange_n(&s->Pages[index], &expected, pa, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        if (MiGetPfn(pa))
            MmFreePage(pa);
        pa = expected;
    }
    return pa;
}

/* the pte of a view page left for the first touch, see MmAccessFault. */
ULONG64 MiSectionSwPte(ULONG protect)
{
    return MM_SWPTE_COMMITTED | MM_SWPTE_SECTION | ((ULONG64)(protect & 0x7FF) << MM_SWPTE_PROT_SHIFT);
}

/*
 * the attributes of a view page. the rom and the read only windows are read only to
 * the guest, so writable views of their pages copy on write.
 */
ULONG64 MiSectionPteAttrs(PSECTION s, ULONG protect, ULONG64 pa, BOOLEAN shared)
{
    ULONG64 attrs = MiProtectToPte(protect, TRUE) | (shared ? MM_PTE_SW_SHARED : 0);
    /* the pages of a file's writable window are the file's, and what a view writes goes there. */
    if (!MiGetPfn(pa) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) && !(s->Flags & MI_SECTION_WRITABLE_WINDOW))
        attrs = MI_PTE_SET_RO(attrs) | MM_PTE_SW_COW;
    return attrs;
}

/*
 * relocating a view of an image away from its base. windows relocates the pages that the base relocation
 * table touches and leaves the others shared with the file. so does mime, but when a page is first
 * touched and not when the view is made, which for a browser's image of hundreds of megabytes is the
 * difference between a few thousand pages and every one. the pages relocated for the base the first view
 * was made at belong to the section, and views at that base share them; a view elsewhere gets private ones.
 */
#define RELOC_NO_BLOCK 0x7FFFFFFFu
#define RELOC_BLOCK_MASK 0x7FFFFFFFu
#define RELOC_STRADDLED 0x80000000u /* a fixup of the page before reaches into this page */

/* bytes of the image as the file has it, from the prototype pages. */
static BOOLEAN read_prototype(PSECTION s, ULONG64 rva, void *out, ULONG length)
{
    PUCHAR bytes = out;
    while (length) {
        ULONG64 index = rva >> PAGE_SHIFT;
        ULONG64 pa = index < s->PageCount ? MiSectionPage(s, (ULONG)index) : 0;
        if (!pa)
            return FALSE;
        ULONG in = (ULONG)(rva & (PAGE_SIZE - 1));
        ULONG n = PAGE_SIZE - in < length ? PAGE_SIZE - in : length;
        memcpy(bytes, (PUCHAR)HalPhysmap(pa) + in, n);
        bytes += n;
        rva += n;
        length -= n;
    }
    return TRUE;
}

static ULONG fixup_width(USHORT entry)
{
    ULONG type = entry >> 12;
    return type == IMAGE_REL_BASED_DIR64 ? 8 : type == IMAGE_REL_BASED_HIGHLOW ? 4 : 0;
}

/* the largest block of the relocation table: one page of 16 bit entries and the header. */
#define RELOC_BLOCK_ENTRIES 2048

/* reads a block of the table. false when it is not one that fits a page, or reaches past the table. */
static BOOLEAN read_reloc_block(PSECTION s, ULONG offset, IMAGE_BASE_RELOCATION *block, USHORT *entries, ULONG *count)
{
    if (offset + sizeof(*block) > s->RelocSize || !read_prototype(s, (ULONG64)s->RelocRva + offset, block, sizeof(*block)))
        return FALSE;
    if (block->SizeOfBlock < sizeof(*block) || offset + block->SizeOfBlock > s->RelocSize)
        return FALSE;
    ULONG n = (block->SizeOfBlock - sizeof(*block)) / 2;
    if (n > RELOC_BLOCK_ENTRIES)
        return FALSE;
    *count = n;
    return !n || read_prototype(s, (ULONG64)s->RelocRva + offset + sizeof(*block), entries, n * 2);
}

/* the index of the table, or null when it is not one the index can describe. */
static ULONG *reloc_index(PSECTION s)
{
    ULONG state = __atomic_load_n(&s->RelocIndexState, __ATOMIC_ACQUIRE);
    if (state == 1)
        return s->RelocIndex;
    if (state == 2 || !s->RelocSize || s->RelocSize >= RELOC_BLOCK_MASK)
        return nullptr;
    ULONG *index = ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, ((SIZE_T)s->PageCount + 1) * sizeof(ULONG),
                                   KI_TAG('M', 'm', 'R', 'x'));
    if (!index)
        return nullptr;
    for (ULONG i = 0; i <= s->PageCount; i++)
        index[i] = RELOC_NO_BLOCK;
    USHORT entries[RELOC_BLOCK_ENTRIES];
    BOOLEAN ok = TRUE;
    ULONG offset = 0;
    while (ok && offset + sizeof(IMAGE_BASE_RELOCATION) <= s->RelocSize) {
        IMAGE_BASE_RELOCATION block;
        ULONG count;
        if (!read_reloc_block(s, offset, &block, entries, &count) || (block.VirtualAddress & (PAGE_SIZE - 1))) {
            ok = FALSE;
            break;
        }
        ULONG page = block.VirtualAddress >> PAGE_SHIFT;
        if (page < s->PageCount) {
            if ((index[page] & RELOC_BLOCK_MASK) != RELOC_NO_BLOCK) {
                ok = FALSE; /* two blocks for a page */
                break;
            }
            index[page] = (index[page] & RELOC_STRADDLED) | offset;
            for (ULONG i = 0; i < count; i++) {
                ULONG width = fixup_width(entries[i]);
                if (width && (entries[i] & 0xFFF) + width > PAGE_SIZE && page + 1 < s->PageCount)
                    index[page + 1] |= RELOC_STRADDLED;
            }
        }
        offset += block.SizeOfBlock;
    }
    if (!ok) {
        ExFreePool(index);
        __atomic_store_n(&s->RelocIndexState, 2, __ATOMIC_RELEASE);
        return nullptr;
    }
    s->RelocIndex = index;
    __atomic_store_n(&s->RelocIndexState, 1, __ATOMIC_RELEASE);
    return index;
}

static BOOLEAN page_needs_relocation(PSECTION s, ULONG64 index)
{
    ULONG entry = s->RelocIndex[index];
    return (entry & RELOC_BLOCK_MASK) != RELOC_NO_BLOCK || (entry & RELOC_STRADDLED);
}

/*
 * fixes up one page of the image in out, a copy of the file's page. the entries of the page's own block that
 * start in it change its bytes, and the ones of the block before that start in the page before and end in
 * this one change the first of them: the part of the new value that falls in this page is written.
 */
static BOOLEAN relocate_block(PSECTION s, ULONG offset, ULONG block_page, ULONG page, LONG64 delta, PUCHAR out)
{
    USHORT entries[RELOC_BLOCK_ENTRIES];
    IMAGE_BASE_RELOCATION block;
    ULONG count;
    if (!read_reloc_block(s, offset, &block, entries, &count))
        return FALSE;
    for (ULONG i = 0; i < count; i++) {
        ULONG width = fixup_width(entries[i]);
        if (!width)
            continue;
        ULONG in = entries[i] & 0xFFF;
        ULONG64 rva = ((ULONG64)block_page << PAGE_SHIFT) + in;
        if (rva + width > s->Size)
            continue;
        BOOLEAN straddles = in + width > PAGE_SIZE;
        if (block_page != page && !straddles)
            continue;
        UCHAR bytes[8];
        if (!straddles) {
            memcpy(bytes, out + in, width);
        } else if (!read_prototype(s, rva, bytes, width)) {
            return FALSE;
        }
        ULONG64 v = 0;
        memcpy(&v, bytes, width);
        v += (ULONG64)delta;
        memcpy(bytes, &v, width);
        for (ULONG b = 0; b < width; b++)
            if (((rva + b) >> PAGE_SHIFT) == page)
                out[(rva + b) & (PAGE_SIZE - 1)] = bytes[b];
    }
    return TRUE;
}

static BOOLEAN build_relocated_page(PSECTION s, ULONG page, LONG64 delta, PUCHAR out)
{
    ULONG64 prototype = MiSectionPage(s, page);
    if (!prototype)
        return FALSE;
    memcpy(out, HalPhysmap(prototype), PAGE_SIZE);
    ULONG entry = s->RelocIndex[page];
    if ((entry & RELOC_BLOCK_MASK) != RELOC_NO_BLOCK && !relocate_block(s, entry & RELOC_BLOCK_MASK, page, page, delta, out))
        return FALSE;
    if ((entry & RELOC_STRADDLED) && page > 0) {
        ULONG before = s->RelocIndex[page - 1] & RELOC_BLOCK_MASK;
        if (before != RELOC_NO_BLOCK && !relocate_block(s, before, page - 1, page, delta, out))
            return FALSE;
    }
    return TRUE;
}

/* the relocated page the section keeps for its base, built if it is not there yet; 0 when out of memory. */
static ULONG64 shared_relocated_page(PSECTION s, ULONG page, LONG64 delta)
{
    ULONG64 pa = __atomic_load_n(&s->RelocPages[page], __ATOMIC_ACQUIRE);
    if (pa)
        return pa;
    pa = MmAllocatePageNoZero();
    if (!pa)
        return 0;
    if (!build_relocated_page(s, page, delta, HalPhysmap(pa))) {
        MmFreePage(pa);
        return 0;
    }
    ULONG64 expected = 0;
    if (!__atomic_compare_exchange_n(&s->RelocPages[page], &expected, pa, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        MmFreePage(pa);
        return expected;
    }
    return pa;
}

BOOLEAN MiSectionFaultPage(PMMVAD vad, ULONG_PTR page, ULONG protect, ULONG64 *pa, BOOLEAN *shared)
{
    PSECTION s = vad->Section;
    ULONG64 index = (vad->SectionOffset + (page - vad->StartVa)) >> PAGE_SHIFT;
    if (index >= s->PageCount)
        return FALSE;
    ULONG64 source = MiSectionPage(s, (ULONG)index);
    if (!source)
        return FALSE;
    /* a page the host took away is an error for the thread that touches it, however many times it does. */
    if (MmWindowPageLost(s, source)) {
        MmWindowLostFault();
        return FALSE;
    }
    if ((vad->Flags & MM_VAD_RELOCATED) && page_needs_relocation(s, index)) {
        if (vad->Flags & MM_VAD_RELOCATED_SHARED) {
            source = shared_relocated_page(s, (ULONG)index, vad->RelocDelta);
            if (!source)
                return FALSE;
        } else {
            ULONG64 own = MmAllocatePageNoZero();
            if (!own)
                return FALSE;
            if (!build_relocated_page(s, (ULONG)index, vad->RelocDelta, HalPhysmap(own))) {
                MmFreePage(own);
                return FALSE;
            }
            *pa = own;
            *shared = FALSE;
            return TRUE;
        }
    }
    if ((s->Flags & SEC_IMAGE) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
        ULONG64 copy = MmAllocatePageNoZero();
        if (!copy)
            return FALSE;
        memcpy(HalPhysmap(copy), HalPhysmap(source), PAGE_SIZE);
        *pa = copy;
        *shared = FALSE;
        return TRUE;
    }
    *pa = source;
    *shared = TRUE;
    return TRUE;
}

ULONG64 MiSectionReadyPage(PMMVAD vad, ULONG64 index, ULONG protect)
{
    PSECTION s = vad->Section;
    if (index >= s->PageCount || ((s->Flags & SEC_IMAGE) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))))
        return 0;
    ULONG64 source = __atomic_load_n(&s->Pages[index], __ATOMIC_ACQUIRE);
    if (source && MmWindowPageLost(s, source))
        return 0;
    if (source && (vad->Flags & MM_VAD_RELOCATED) && page_needs_relocation(s, index))
        source = (vad->Flags & MM_VAD_RELOCATED_SHARED) ? __atomic_load_n(&s->RelocPages[index], __ATOMIC_ACQUIRE) : 0;
    return source;
}

/* builds every page, for mappings that cannot fault such as system space views. */
static BOOLEAN commit_all(PSECTION s)
{
    for (ULONG i = 0; i < s->PageCount; i++)
        if (!MiSectionPage(s, i))
            return FALSE;
    return TRUE;
}

/* copies bytes into the section's page array at a byte offset. */
static void scatter(PSECTION s, ULONG64 offset, const void *data, SIZE_T length)
{
    const UCHAR *src = data;
    while (length) {
        ULONG page = (ULONG)(offset >> PAGE_SHIFT);
        ULONG in = (ULONG)(offset & (PAGE_SIZE - 1));
        SIZE_T n = PAGE_SIZE - in < length ? PAGE_SIZE - in : length;
        if (page >= s->PageCount || !MiSectionPage(s, page))
            return;
        memcpy((PUCHAR)HalPhysmap(s->Pages[page]) + in, src, n);
        src += n;
        offset += n;
        length -= n;
    }
}

static NTSTATUS copy_file_into(PSECTION s, PFILE_OBJECT file, ULONG64 file_offset, ULONG64 section_offset,
                               ULONG64 length)
{
    const ULONG chunk = 64 * 1024;
    PUCHAR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, chunk, KI_TAG('M', 'm', 'C', 'f'));
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = STATUS_SUCCESS;
    while (length) {
        ULONG n = length < chunk ? (ULONG)length : chunk;
        ULONG got = 0;
        status = IoReadFileObject(file, buf, n, file_offset, &got);
        if (status == STATUS_END_OF_FILE) {
            status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(status))
            break;
        scatter(s, section_offset, buf, got);
        if (got < n)
            break;
        file_offset += got;
        section_offset += got;
        length -= got;
    }
    ExFreePool(buf);
    return status;
}

NTSTATUS MiParseImageHeaders(const void *headers, SIZE_T length, MI_IMAGE_INFO *info)
{
    const IMAGE_DOS_HEADER *dos = headers;
    if (length < sizeof(*dos) || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return STATUS_INVALID_IMAGE_NOT_MZ;
    SIZE_T nt_offset = offsetof(IMAGE_NT_HEADERS64, OptionalHeader);
    if (dos->e_lfanew < 0 || (SIZE_T)dos->e_lfanew > length || length - dos->e_lfanew < nt_offset)
        return STATUS_INVALID_IMAGE_FORMAT;
    const IMAGE_NT_HEADERS64 *nt = (const void *)((const UCHAR *)headers + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;
    BOOLEAN pe32 = nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386;
    SIZE_T optional_size = pe32 ? sizeof(IMAGE_OPTIONAL_HEADER32) : sizeof(IMAGE_OPTIONAL_HEADER64);
    if (nt->FileHeader.SizeOfOptionalHeader < optional_size ||
        length - dos->e_lfanew - nt_offset < nt->FileHeader.SizeOfOptionalHeader)
        return STATUS_INVALID_IMAGE_FORMAT;
    IMAGE_OPTIONAL_HEADER64 normalized;
    const IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;
    if (pe32) {
        const IMAGE_OPTIONAL_HEADER32 *o = (const void *)opt;
        if (o->Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return STATUS_INVALID_IMAGE_FORMAT;
        memset(&normalized, 0, sizeof(normalized));
        normalized.ImageBase = o->ImageBase;
        normalized.SizeOfImage = o->SizeOfImage;
        normalized.AddressOfEntryPoint = o->AddressOfEntryPoint;
        normalized.SizeOfHeaders = o->SizeOfHeaders;
        normalized.Subsystem = o->Subsystem;
        normalized.MajorSubsystemVersion = o->MajorSubsystemVersion;
        normalized.MinorSubsystemVersion = o->MinorSubsystemVersion;
        normalized.DllCharacteristics = o->DllCharacteristics;
        normalized.SizeOfStackReserve = o->SizeOfStackReserve;
        normalized.SizeOfStackCommit = o->SizeOfStackCommit;
        normalized.NumberOfRvaAndSizes = o->NumberOfRvaAndSizes;
        memcpy(normalized.DataDirectory, o->DataDirectory, sizeof(normalized.DataDirectory));
        opt = &normalized;
    } else if ((nt->FileHeader.Machine != IMAGE_FILE_MACHINE_ARM64 && nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) ||
               opt->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    if (opt->NumberOfRvaAndSizes > IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
        (pe32 && opt->ImageBase + opt->SizeOfImage > 0x80000000ull))
        return STATUS_INVALID_IMAGE_FORMAT;
    if (opt->SizeOfImage == 0 || opt->SizeOfImage > (1u << 30) || opt->SizeOfHeaders > opt->SizeOfImage)
        return STATUS_INVALID_IMAGE_FORMAT;
    SIZE_T sections_end = (SIZE_T)dos->e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                          nt->FileHeader.SizeOfOptionalHeader +
                          (SIZE_T)nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
    if (sections_end > length)
        return STATUS_INVALID_IMAGE_FORMAT;
    info->ImageBase = opt->ImageBase;
    info->SizeOfImage = opt->SizeOfImage;
    info->EntryPoint = opt->AddressOfEntryPoint;
    info->SizeOfHeaders = opt->SizeOfHeaders;
    info->Machine = nt->FileHeader.Machine;
    info->Subsystem = opt->Subsystem;
    info->SubsystemMajor = opt->MajorSubsystemVersion;
    info->SubsystemMinor = opt->MinorSubsystemVersion;
    info->DllCharacteristics = opt->DllCharacteristics;
    info->Characteristics = nt->FileHeader.Characteristics;
    info->StackReserve = opt->SizeOfStackReserve;
    info->StackCommit = opt->SizeOfStackCommit;
    /* images with no absolute references need no relocation records. */
    info->Relocatable = !(nt->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED);
    return STATUS_SUCCESS;
}

static UCHAR protect_of(ULONG chars)
{
    BOOLEAN x = (chars & IMAGE_SCN_MEM_EXECUTE) != 0, w = (chars & IMAGE_SCN_MEM_WRITE) != 0;
    if (x)
        return w ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    return w ? PAGE_READWRITE : PAGE_READONLY;
}

static UCHAR merge_protect(UCHAR a, UCHAR b)
{
    BOOLEAN x = (a | b) & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE);
    BOOLEAN w = (a | b) & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE);
    if (x)
        return w ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    return w ? PAGE_READWRITE : PAGE_READONLY;
}

static NTSTATUS set_file_name(PSECTION s, PFILE_OBJECT file)
{
    if (!file->FileName.Length)
        return STATUS_SUCCESS;
    s->FileName.Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, file->FileName.Length + sizeof(WCHAR), KI_TAG('M', 'm', 'F', 'n'));
    if (!s->FileName.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(s->FileName.Buffer, file->FileName.Buffer, file->FileName.Length);
    s->FileName.Buffer[file->FileName.Length / sizeof(WCHAR)] = 0;
    s->FileName.Length = file->FileName.Length;
    s->FileName.MaximumLength = (USHORT)(file->FileName.Length + sizeof(WCHAR));
    return STATUS_SUCCESS;
}

/* fills the per page protections of an image section from its section headers. */
static NTSTATUS image_protections(PSECTION s, const IMAGE_NT_HEADERS64 *nt)
{
    s->PageProtect = ExAllocatePool2(POOL_FLAG_NON_PAGED, s->PageCount, KI_TAG('M', 'm', 'P', 'p'));
    if (!s->PageProtect)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG header_pages = (ULONG)BYTES_TO_PAGES(s->Image.SizeOfHeaders);
    for (ULONG i = 0; i < header_pages && i < s->PageCount; i++)
        s->PageProtect[i] = PAGE_READONLY;
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (USHORT i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        ULONG vsize = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if ((ULONG64)sec[i].VirtualAddress + vsize > s->Image.SizeOfImage)
            return STATUS_INVALID_IMAGE_FORMAT;
        UCHAR p = protect_of(sec[i].Characteristics);
        ULONG first = sec[i].VirtualAddress >> PAGE_SHIFT;
        ULONG last = (ULONG)((sec[i].VirtualAddress + vsize + PAGE_SIZE - 1) >> PAGE_SHIFT);
        for (ULONG pg = first; pg < last && pg < s->PageCount; pg++)
            s->PageProtect[pg] = s->PageProtect[pg] ? merge_protect(s->PageProtect[pg], p) : p;
    }
    for (ULONG i = 0; i < s->PageCount; i++)
        if (!s->PageProtect[i])
            s->PageProtect[i] = PAGE_READONLY;
    return STATUS_SUCCESS;
}

/* builds image page `page` of a pe file held in memory, as the loader lays it out. */
static void load_image_page(const UCHAR *file, ULONG64 file_size, const IMAGE_NT_HEADERS64 *nt, ULONG page, PUCHAR out)
{
    ULONG64 start = (ULONG64)page << PAGE_SHIFT, end = start + PAGE_SIZE;
    memset(out, 0, PAGE_SIZE);
    ULONG64 headers = nt->OptionalHeader.SizeOfHeaders < file_size ? nt->OptionalHeader.SizeOfHeaders : file_size;
    if (start < headers)
        memcpy(out, file + start, (headers < end ? headers : end) - start);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (USHORT i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        ULONG vsize = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        ULONG raw = sec[i].SizeOfRawData < vsize ? sec[i].SizeOfRawData : vsize;
        if (!raw || !sec[i].PointerToRawData)
            continue;
        ULONG64 lo = sec[i].VirtualAddress, hi = lo + raw;
        if (hi <= start || lo >= end)
            continue;
        ULONG64 from = lo > start ? lo : start, to = hi < end ? hi : end;
        memcpy(out + (from - start), file + sec[i].PointerToRawData + (from - lo), to - from);
    }
}

static PSECTION g_rom_images[64];
static NTSTATUS create_copied_image(PFILE_OBJECT file, PSECTION *out);

/* an image section built lazily from file data the guest can read in place. takes over `ref`. */
static NTSTATUS create_source_image(PFILE_OBJECT file, ULONG64 pa, ULONG64 size, PVOID ref, PSECTION *out)
{
    const UCHAR *data = HalPhysmap(pa);
    MI_IMAGE_INFO info;
    NTSTATUS status = MiParseImageHeaders(data, size, &info);
    if (!NT_SUCCESS(status)) {
        IoReleaseFileWindow(ref);
        return status;
    }
    const IMAGE_NT_HEADERS64 *nt = (const void *)(data + ((const IMAGE_DOS_HEADER *)data)->e_lfanew);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (USHORT i = 0; NT_SUCCESS(status) && i < nt->FileHeader.NumberOfSections; i++)
        if ((ULONG64)sec[i].PointerToRawData + sec[i].SizeOfRawData > size)
            status = STATUS_INVALID_IMAGE_FORMAT;
    PSECTION s = nullptr;
    if (NT_SUCCESS(status))
        status = alloc_section(info.SizeOfImage, &s);
    if (!NT_SUCCESS(status)) {
        IoReleaseFileWindow(ref);
        return status;
    }
    s->Flags = SEC_IMAGE;
    s->Protect = PAGE_EXECUTE_WRITECOPY;
    s->Image = info;
    s->RelocRva = image_directory(nt, IMAGE_DIRECTORY_ENTRY_BASERELOC).VirtualAddress;
    s->RelocSize = image_directory(nt, IMAGE_DIRECTORY_ENTRY_BASERELOC).Size;
    s->SourcePa = pa;
    s->SourceSize = size;
    s->SourceRef = ref;
    MmWindowSectionAttach(s);
    status = image_protections(s, nt);
    if (NT_SUCCESS(status))
        status = set_file_name(s, file);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(s);
        return status;
    }
    ObReferenceObject(file);
    s->File = file;
    *out = s;
    return STATUS_SUCCESS;
}

static PSECTION g_rom_images[64];

/*
 * the image sections of files, weakly: a section leaves the table when it is deleted. the window of the
 * file stays mapped as long as a section holds it, so the same address and size mean the same bytes.
 */
static PSECTION g_file_images[128];

static PSECTION find_file_image(ULONG64 pa, ULONG64 size)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_file_images); i++) {
        PSECTION s = g_file_images[i];
        if (s && s->SourcePa == pa && s->SourceSize == size)
            return s;
    }
    return nullptr;
}

static void remember_file_image(PSECTION s)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_file_images); i++) {
        if (!g_file_images[i]) {
            g_file_images[i] = s;
            s->ImageSlot = i + 1;
            return;
        }
    }
}

static void forget_file_image(PSECTION s)
{
    if (s->ImageSlot && g_file_images[s->ImageSlot - 1] == s)
        g_file_images[s->ImageSlot - 1] = nullptr;
}

NTSTATUS MiCreateImageSection(PFILE_OBJECT file, PSECTION *out)
{
    ULONG64 pa, size;
    PVOID ref;
    const MimeRomEntry *rom = IoGetRomFile(file);
    ULONG index = rom ? (ULONG)(rom - HalRomEntry(0)) : RTL_NUMBER_OF(g_rom_images);
    if (index < RTL_NUMBER_OF(g_rom_images)) {
        PSECTION cached = __atomic_load_n(&g_rom_images[index], __ATOMIC_ACQUIRE);
        if (cached) {
            ObReferenceObject(cached);
            *out = cached;
            return STATUS_SUCCESS;
        }
    }
    if (IoGetFileWindow(file, &pa, &size, &ref)) {
        /* the processes that run an image share its section: the pages built and relocated for one serve the rest. */
        PSECTION shared = index >= RTL_NUMBER_OF(g_rom_images) ? find_file_image(pa, size) : nullptr;
        if (shared) {
            IoReleaseFileWindow(ref);
            ObReferenceObject(shared);
            *out = shared;
            return STATUS_SUCCESS;
        }
        NTSTATUS status = create_source_image(file, pa, size, ref, out);
        if (NT_SUCCESS(status) && index < RTL_NUMBER_OF(g_rom_images)) {
            PSECTION expected = nullptr;
            ObReferenceObject(*out);
            if (!__atomic_compare_exchange_n(&g_rom_images[index], &expected, *out, FALSE, __ATOMIC_ACQ_REL,
                                             __ATOMIC_ACQUIRE))
                ObDereferenceObject(*out);
        } else if (NT_SUCCESS(status)) {
            remember_file_image(*out);
        }
        return status;
    }
    return create_copied_image(file, out);
}

/* files no window covers, such as files opened for writing: the image is read into memory. */
static NTSTATUS create_copied_image(PFILE_OBJECT file, PSECTION *out)
{
    PUCHAR headers = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, KI_TAG('M', 'm', 'H', 'd'));
    if (!headers)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG got = 0;
    NTSTATUS status = IoReadFileObject(file, headers, PAGE_SIZE, 0, &got);
    MI_IMAGE_INFO info;
    if (NT_SUCCESS(status))
        status = MiParseImageHeaders(headers, got, &info);
    if (!NT_SUCCESS(status)) {
        ExFreePool(headers);
        return status == STATUS_END_OF_FILE ? STATUS_INVALID_IMAGE_FORMAT : status;
    }
    const IMAGE_NT_HEADERS64 *nt = (const void *)(headers + ((PIMAGE_DOS_HEADER)headers)->e_lfanew);
    PSECTION s;
    status = alloc_section(info.SizeOfImage, &s);
    if (!NT_SUCCESS(status)) {
        ExFreePool(headers);
        return status;
    }
    s->Flags = SEC_IMAGE;
    s->Protect = PAGE_EXECUTE_WRITECOPY;
    s->Image = info;
    s->RelocRva = image_directory(nt, IMAGE_DIRECTORY_ENTRY_BASERELOC).VirtualAddress;
    s->RelocSize = image_directory(nt, IMAGE_DIRECTORY_ENTRY_BASERELOC).Size;
    s->PageProtect = ExAllocatePool2(POOL_FLAG_NON_PAGED, s->PageCount, KI_TAG('M', 'm', 'P', 'p'));
    if (!s->PageProtect) {
        ExFreePool(headers);
        ObDereferenceObject(s);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (ULONG i = 0; i < s->PageCount; i++)
        s->PageProtect[i] = 0;
    ULONG header_pages = (ULONG)BYTES_TO_PAGES(info.SizeOfHeaders);
    for (ULONG i = 0; i < header_pages && i < s->PageCount; i++)
        s->PageProtect[i] = PAGE_READONLY;

    status = copy_file_into(s, file, 0, 0, info.SizeOfHeaders < got ? info.SizeOfHeaders : got);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (USHORT i = 0; i < nt->FileHeader.NumberOfSections && NT_SUCCESS(status); i++) {
        ULONG vsize = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        ULONG raw = sec[i].SizeOfRawData < vsize ? sec[i].SizeOfRawData : vsize;
        if ((ULONG64)sec[i].VirtualAddress + vsize > info.SizeOfImage) {
            status = STATUS_INVALID_IMAGE_FORMAT;
            break;
        }
        if (raw && sec[i].PointerToRawData)
            status = copy_file_into(s, file, sec[i].PointerToRawData, sec[i].VirtualAddress, raw);
        UCHAR p = protect_of(sec[i].Characteristics);
        ULONG first = sec[i].VirtualAddress >> PAGE_SHIFT;
        ULONG last = (ULONG)((sec[i].VirtualAddress + vsize + PAGE_SIZE - 1) >> PAGE_SHIFT);
        for (ULONG pg = first; pg < last && pg < s->PageCount; pg++)
            s->PageProtect[pg] = s->PageProtect[pg] ? merge_protect(s->PageProtect[pg], p) : p;
    }
    for (ULONG i = 0; i < s->PageCount; i++)
        if (!s->PageProtect[i])
            s->PageProtect[i] = PAGE_READONLY;
    ExFreePool(headers);
    if (NT_SUCCESS(status))
        status = set_file_name(s, file);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(s);
        return status;
    }
    ObReferenceObject(file);
    s->File = file;
    *out = s;
    return STATUS_SUCCESS;
}

NTSTATUS MiCreateDataSection(PFILE_OBJECT file, ULONG64 size, ULONG protect, ULONG flags, PSECTION *out)
{
    if (file && size == 0) {
        NTSTATUS status = IoQueryFileSize(file, &size);
        if (!NT_SUCCESS(status))
            return status;
        if (size == 0)
            return (NTSTATUS)0xC000011FL; /* STATUS_MAPPED_FILE_SIZE_ZERO */
    }
    if (size == 0)
        return STATUS_INVALID_PARAMETER_4;
    PSECTION s;
    NTSTATUS status = alloc_section(size, &s);
    if (!NT_SUCCESS(status))
        return status;
    s->Flags = flags ? flags : SEC_COMMIT;
    s->Protect = protect;
    if (file) {
        ULONG64 pa, file_size;
        BOOLEAN writable = (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) != 0;
        if (writable && IoGetFileWindowEx(file, TRUE, size, &pa, &file_size, &s->SourceRef)) {
            /* the views write the file's own pages, which a read or a write of the file sees as they are. */
            s->SourcePa = pa;
            s->SourceSize = file_size;
            s->Flags |= MI_SECTION_WRITABLE_WINDOW;
            MmWindowSectionAttach(s);
        } else if (!writable && IoGetFileWindow(file, &pa, &file_size, &s->SourceRef)) {
            /* views of a read only section share the file's pages. */
            s->SourcePa = pa;
            s->SourceSize = file_size;
            s->Flags |= MI_SECTION_SHARE_SOURCE;
            MmWindowSectionAttach(s);
        } else {
            status = copy_file_into(s, file, 0, 0, size);
        }
        if (NT_SUCCESS(status))
            status = set_file_name(s, file);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(s);
            return status;
        }
        ObReferenceObject(file);
        s->File = file;
    }
    *out = s;
    return STATUS_SUCCESS;
}

/* bytes of the image from its private pages, which are not next to each other in memory. */
static void read_private(const ULONG64 *pages, ULONG64 rva, void *out, ULONG length)
{
    PUCHAR bytes = out;
    for (ULONG i = 0; i < length; i++, rva++)
        bytes[i] = *((PUCHAR)HalPhysmap(pages[rva >> PAGE_SHIFT]) + (rva & (PAGE_SIZE - 1)));
}

static void apply_relocations(PSECTION s, ULONG64 *pages, ULONG_PTR base)
{
    LONG64 delta = (LONG64)(base - s->Image.ImageBase);
    ULONG off = 0;
    while (off + sizeof(IMAGE_BASE_RELOCATION) <= s->RelocSize) {
        IMAGE_BASE_RELOCATION block;
        ULONG64 rva = s->RelocRva + off;
        if (rva + sizeof(block) > s->Size)
            return;
        read_private(pages, rva, &block, sizeof(block));
        if (block.SizeOfBlock < sizeof(block) || off + block.SizeOfBlock > s->RelocSize)
            return;
        for (ULONG i = sizeof(block); i + 2 <= block.SizeOfBlock; i += 2) {
            ULONG64 erva = s->RelocRva + off + i;
            USHORT entry;
            read_private(pages, erva, &entry, sizeof(entry));
            ULONG type = entry >> 12;
            int width = type == IMAGE_REL_BASED_DIR64 ? 8 : type == IMAGE_REL_BASED_HIGHLOW ? 4 : 0;
            if (!width)
                continue;
            ULONG64 target = block.VirtualAddress + (entry & 0xFFF);
            if (target + width > s->Size)
                continue;
            /* a relocation may straddle two pages. */
            UCHAR bytes[8];
            for (int b = 0; b < width; b++)
                bytes[b] = *((PUCHAR)HalPhysmap(pages[(target + b) >> PAGE_SHIFT]) + ((target + b) & (PAGE_SIZE - 1)));
            ULONG64 v = 0;
            memcpy(&v, bytes, width);
            v += (ULONG64)delta;
            memcpy(bytes, &v, width);
            for (int b = 0; b < width; b++)
                *((PUCHAR)HalPhysmap(pages[(target + b) >> PAGE_SHIFT]) + ((target + b) & (PAGE_SIZE - 1))) = bytes[b];
        }
        off += block.SizeOfBlock;
    }
}

NTSTATUS MiMapViewOfSectionEx(PSECTION s, PEPROCESS process, PVOID *base, ULONG64 offset, SIZE_T *view_size,
                              ULONG protect, ULONG allocation_type, ULONG_PTR zero_bits, const MI_EXTENDED *ext)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    MI_EXTENDED none = {};
    if (!ext)
        ext = &none;
    BOOLEAN image = (s->Flags & SEC_IMAGE) != 0;
    BOOLEAN replace = (allocation_type & MEM_REPLACE_PLACEHOLDER) != 0;
    if (replace && (image || !*base))
        return image ? STATUS_INVALID_PARAMETER : STATUS_CONFLICTING_ADDRESSES;
    if (zero_bits && !*base) {
        ULONG_PTR probe = ~0ull;
        NTSTATUS status = MiZeroBitsHighest(zero_bits, &probe);
        if (!NT_SUCCESS(status))
            return status;
    }
    ULONG64 first_page = image ? 0 : offset >> PAGE_SHIFT;
    if (first_page >= s->PageCount)
        return STATUS_INVALID_VIEW_SIZE;
    SIZE_T pages = s->PageCount - (ULONG)first_page;
    if (!image && *view_size) {
        SIZE_T want = BYTES_TO_PAGES(*view_size + (offset & (PAGE_SIZE - 1)));
        if (want > pages)
            return STATUS_INVALID_VIEW_SIZE;
        pages = want;
    }
    SIZE_T bytes = pages << PAGE_SHIFT;

    MiLockAddressSpace(as);
    ULONG_PTR va = (ULONG_PTR)*base;
    BOOLEAN relocated = FALSE;
    NTSTATUS status = STATUS_SUCCESS;
    PMMVAD placeholder = nullptr;
    if (image) {
        if (!va)
            va = s->Image.ImageBase;
        if (!MiRangeIsFree(as, va, va + bytes)) {
            if (*base || !s->Image.Relocatable) {
                MiUnlockAddressSpace(as);
                return STATUS_CONFLICTING_ADDRESSES;
            }
            /* the views of an image away from its base share relocated pages when they are all at one base. */
            if (s->RelocBase && MiRangeIsFree(as, s->RelocBase, s->RelocBase + bytes))
                va = s->RelocBase;
            else
                status = MiFindFreeRange(as, bytes, 0x10000, TRUE, &va);
            relocated = TRUE;
        }
        relocated = relocated || va != s->Image.ImageBase;
    } else if (replace) {
        va &= ~(ULONG_PTR)(PAGE_SIZE - 1);
        PMMVAD vad = MiFindVad(as, va);
        if (vad && (vad->Flags & MM_VAD_PLACEHOLDER) && vad->StartVa == va && vad->EndVa == va + bytes)
            placeholder = vad;
        else
            status = STATUS_CONFLICTING_ADDRESSES;
    } else if (va) {
        va &= ~(ULONG_PTR)(PAGE_SIZE - 1);
        if (!MiRangeIsFree(as, va, va + bytes))
            status = STATUS_CONFLICTING_ADDRESSES;
    } else {
        ULONG_PTR highest = ext->Highest;
        if (zero_bits) {
            ULONG_PTR limit = as->AllocationLimit ? as->AllocationLimit - 1 : 0x7FFFFFFEFFFFull;
            status = MiZeroBitsHighest(zero_bits, &limit);
            if (NT_SUCCESS(status) && (!highest || highest > limit))
                highest = limit;
        }
        if (NT_SUCCESS(status))
            status = MiFindFreeRangeEx(as, bytes, ext->Alignment, ext->Lowest, highest,
                                       (allocation_type & MEM_TOP_DOWN) != 0, &va);
    }
    if (!NT_SUCCESS(status)) {
        MiUnlockAddressSpace(as);
        return status;
    }

    PMMVAD vad = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MMVAD), KI_TAG('M', 'm', 'V', 'd'));
    ULONG64 *private_pages = nullptr;
    /* a table the index cannot describe is relocated whole, now, in pages of the view's own. */
    BOOLEAN lazy = vad && image && relocated && reloc_index(s) != nullptr;
    if (vad && image && relocated && !lazy)
        private_pages = ExAllocatePool2(POOL_FLAG_NON_PAGED, pages * sizeof(ULONG64), KI_TAG('M', 'm', 'R', 'p'));
    if (!vad || (image && relocated && !lazy && !private_pages)) {
        ExFreePool(vad);
        MiUnlockAddressSpace(as);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    vad->StartVa = va;
    vad->EndVa = va + bytes;
    vad->Type = image ? MiVadImage : MiVadMapped;
    vad->InitialProtect = image ? PAGE_EXECUTE_WRITECOPY : protect;
    vad->Section = s;
    vad->SectionOffset = first_page << PAGE_SHIFT;
    vad->Flags = replace ? MM_VAD_REPLACED : 0;
    vad->RelocDelta = 0;
    if (lazy) {
        vad->Flags |= MM_VAD_RELOCATED;
        vad->RelocDelta = (LONG64)(va - s->Image.ImageBase);
        if (!s->RelocBase) {
            s->RelocPages = ExAllocatePool2(POOL_FLAG_NON_PAGED, ((SIZE_T)s->PageCount + 1) * sizeof(ULONG64),
                                            KI_TAG('M', 'm', 'R', 's'));
            if (s->RelocPages)
                s->RelocBase = va;
        }
        if (s->RelocBase == va)
            vad->Flags |= MM_VAD_RELOCATED_SHARED;
    }
    ObReferenceObject(s);
    if (placeholder) {
        MiSwapVad(as, placeholder, vad);
        ExFreePool(placeholder);
    } else {
        status = MiInsertVad(as, vad);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(s);
            ExFreePool(vad);
            if (private_pages)
                ExFreePool(private_pages);
            MiUnlockAddressSpace(as);
            return status;
        }
    }

    if (image && relocated && !lazy) {
        for (SIZE_T i = 0; i < pages; i++) {
            ULONG64 src = MiSectionPage(s, (ULONG)i);
            private_pages[i] = src ? MmAllocatePageNoZero() : 0;
            if (!private_pages[i]) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            memcpy(HalPhysmap(private_pages[i]), HalPhysmap(src), PAGE_SIZE);
        }
        if (NT_SUCCESS(status) && s->RelocSize)
            apply_relocations(s, private_pages, va);
    }

    /*
     * pages the section already holds are mapped now; the others, and the
     * writable image pages that need a private copy, on first touch.
     */
    for (SIZE_T i = 0; i < pages && NT_SUCCESS(status); i++) {
        ULONG_PTR pva = va + (i << PAGE_SHIFT);
        ULONG prot = image ? s->PageProtect[i] : protect;
        BOOLEAN writable = (prot & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) != 0;
        if (lazy) {
            /* the pages that are ready and shared are mapped, the others come with the first touch. */
            ULONG64 ready = MiSectionReadyPage(vad, i, prot);
            if (ready) {
                if (!MiMapPage(as->Root, pva, ready, MiSectionPteAttrs(s, prot, ready, TRUE)))
                    status = STATUS_INSUFFICIENT_RESOURCES;
            } else {
                ULONG64 *pte = MiWalk(as->Root, pva, TRUE, 3);
                if (!pte)
                    status = STATUS_INSUFFICIENT_RESOURCES;
                else
                    *pte = MiSectionSwPte(prot);
            }
            continue;
        }
        ULONG64 pa = relocated ? private_pages[i] : __atomic_load_n(&s->Pages[first_page + i], __ATOMIC_ACQUIRE);
        if (!pa || (image && !relocated && writable)) {
            ULONG64 *pte = MiWalk(as->Root, pva, TRUE, 3);
            if (!pte)
                status = STATUS_INSUFFICIENT_RESOURCES;
            else
                *pte = MiSectionSwPte(prot);
            continue;
        }
        ULONG64 attrs = MiSectionPteAttrs(s, prot, pa, !relocated);
        if (!MiMapPage(as->Root, pva, pa, attrs))
            status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (relocated && !lazy)
        as->CommittedPages += pages;
    if (private_pages)
        ExFreePool(private_pages);
    if (!NT_SUCCESS(status)) {
        MiUnlockAddressSpace(as);
        MiUnmapViewOfSection(process, (PVOID)va);
        return status;
    }
    MiUnlockAddressSpace(as);
    *base = (PVOID)va;
    *view_size = bytes;
    return relocated ? STATUS_IMAGE_NOT_AT_BASE : STATUS_SUCCESS;
}

NTSTATUS MiMapViewOfSection(PSECTION s, PEPROCESS process, PVOID *base, ULONG64 offset, SIZE_T *view_size,
                            ULONG protect, ULONG allocation_type)
{
    return MiMapViewOfSectionEx(s, process, base, offset, view_size, protect, allocation_type, 0, nullptr);
}

/*
 * unmaps the view holding an address. with preserve the range stays
 * reserved as a placeholder, which only a view made in one can do.
 */
NTSTATUS MiUnmapViewOfSectionEx(PEPROCESS process, PVOID base, ULONG flags)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    BOOLEAN preserve = (flags & UNMAP_PRESERVE_PLACEHOLDER) != 0;
    MiLockAddressSpace(as);
    PMMVAD vad = MiFindVad(as, (ULONG_PTR)base);
    if (!vad || (vad->Type != MiVadImage && vad->Type != MiVadMapped)) {
        MiUnlockAddressSpace(as);
        return STATUS_NOT_MAPPED_VIEW;
    }
    if (preserve && ((ULONG_PTR)base != vad->StartVa || !(vad->Flags & MM_VAD_REPLACED))) {
        MiUnlockAddressSpace(as);
        return STATUS_CONFLICTING_ADDRESSES;
    }
    MiReleasePages(as, vad->StartVa, vad->EndVa);
    PSECTION section = vad->Section;
    if (preserve) {
        vad->Section = nullptr;
        vad->SectionOffset = 0;
        vad->Type = MiVadPrivate;
        vad->InitialProtect = PAGE_NOACCESS;
        vad->Flags = MM_VAD_PLACEHOLDER;
    } else {
        MiRemoveVad(as, vad);
    }
    MiUnlockAddressSpace(as);
    if (section)
        ObDereferenceObject(section);
    if (!preserve)
        ExFreePool(vad);
    return STATUS_SUCCESS;
}

NTSTATUS MiUnmapViewOfSection(PEPROCESS process, PVOID base)
{
    return MiUnmapViewOfSectionEx(process, base, 0);
}

/* ------------------------------------------------ kernel shared sections */

NTSTATUS NTAPI MmCreateSharedSection(SIZE_T size, PVOID *kernel_base, PVOID *section)
{
    PSECTION s;
    NTSTATUS status = MiCreateDataSection(nullptr, size, PAGE_READWRITE, SEC_COMMIT, &s);
    if (!NT_SUCCESS(status))
        return status;
    if (commit_all(s))
        s->SystemVa = MmMapPagesInSystem(s->Pages, s->PageCount, MM_KERNEL_RW);
    if (!s->SystemVa) {
        ObDereferenceObject(s);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    *kernel_base = s->SystemVa;
    *section = s;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI MimeMmCreatePhysicalSection(ULONG64 pa, SIZE_T size, PVOID *section)
{
    PSECTION s;
    NTSTATUS status = ObCreateObject(MmSectionType, nullptr, KernelMode, sizeof(SECTION), (PVOID *)&s);
    if (!NT_SUCCESS(status))
        return status;
    memset(s, 0, sizeof(*s));
    s->Flags = SEC_COMMIT | MI_SECTION_PHYSICAL;
    s->Protect = PAGE_READWRITE;
    s->Size = size;
    s->PageCount = (ULONG)BYTES_TO_PAGES(size);
    s->Pages = ExAllocatePool2(POOL_FLAG_NON_PAGED, (s->PageCount + 1) * sizeof(ULONG64), KI_TAG('M', 'm', 'S', 'p'));
    if (!s->Pages) {
        ObDereferenceObject(s);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (ULONG i = 0; i < s->PageCount; i++)
        s->Pages[i] = pa + ((ULONG64)i << PAGE_SHIFT);
    *section = s;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI MmMapSharedSectionUser(PVOID section, PEPROCESS process, PVOID *user_base, BOOLEAN writable)
{
    SIZE_T size = 0;
    *user_base = nullptr;
    return MiMapViewOfSection(section, process, user_base, 0, &size, writable ? PAGE_READWRITE : PAGE_READONLY,
                              MEM_TOP_DOWN);
}

NTSTATUS NTAPI MmMapViewInSystemSpace(PVOID section, PVOID *base, PSIZE_T size)
{
    PSECTION s = section;
    if (!s->SystemVa && commit_all(s))
        s->SystemVa = MmMapPagesInSystem(s->Pages, s->PageCount, MM_KERNEL_RW);
    if (!s->SystemVa)
        return STATUS_INSUFFICIENT_RESOURCES;
    *base = s->SystemVa;
    *size = (SIZE_T)s->PageCount << PAGE_SHIFT;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI MmUnmapViewInSystemSpace(PVOID base)
{
    return STATUS_SUCCESS;
}

/* -------------------------------------------------------- system services */

NTSTATUS NTAPI NtCreateSection(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                               PLARGE_INTEGER maximum_size, ULONG protect, ULONG allocation, HANDLE file_handle)
{
    LARGE_INTEGER size = {};
    if (maximum_size && !NT_SUCCESS(KiCopyFromUser(&size, maximum_size, sizeof(size))))
        return STATUS_ACCESS_VIOLATION;
    PFILE_OBJECT file = nullptr;
    NTSTATUS status;
    if (file_handle) {
        status = ObReferenceHandle(file_handle, IoFileObjectTypeObject, KeGetPreviousMode(), (PVOID *)&file, nullptr);
        if (!NT_SUCCESS(status))
            return status;
    }
    PSECTION section;
    if (allocation & SEC_IMAGE) {
        /* a process that refuses images with a low label does not get a section of one. */
        if (file && (PsGetCurrentProcess()->MitigationPolicy[MIME_POLICY_IMAGE_LOAD] & MIME_IMAGE_LOAD_NO_LOW_LABEL)) {
            SD_BLOB *sd;
            if (NT_SUCCESS(ObReferenceSecurity(file, &sd)) && sd) {
                BOOLEAN low = SeSdIntegrityLevel(sd) < 0x2000;
                SeSdDereference(sd);
                if (low) {
                    ObDereferenceObject(file);
                    return STATUS_ACCESS_DENIED;
                }
            }
        }
        status = file ? MiCreateImageSection(file, &section) : STATUS_INVALID_FILE_FOR_SECTION;
    } else {
        status = MiCreateDataSection(file, (ULONG64)size.QuadPart, protect, allocation, &section);
    }
    if (file)
        ObDereferenceObject(file);
    if (!NT_SUCCESS(status))
        return status;
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(section);
        return status;
    }
    HANDLE h;
    status = ObInsertObjectNamed(section, attributes ? &oa : nullptr, access, KeGetPreviousMode(), &h);
    KiFreeCapturedString(&name);
    if (NT_SUCCESS(status)) {
        NTSTATUS st = ObStoreHandle(handle, h);
        if (!NT_SUCCESS(st))
            status = st;
    }
    return status;
}

NTSTATUS NTAPI ZwCreateSection(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PLARGE_INTEGER size,
                               ULONG protect, ULONG allocation, HANDLE file)
{
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    t->PreviousMode = KernelMode;
    NTSTATUS status = NtCreateSection(handle, access, attributes, size, protect, allocation, file);
    t->PreviousMode = old;
    return status;
}

NTSTATUS NTAPI NtOpenSection(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    HANDLE h;
    status = ObOpenObjectByName(&oa, MmSectionType, KeGetPreviousMode(), access, nullptr, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtMapViewOfSection(HANDLE section_handle, HANDLE process_handle, PVOID *base, ULONG_PTR zero_bits,
                                  SIZE_T commit_size, PLARGE_INTEGER offset, PSIZE_T view_size, SECTION_INHERIT inherit,
                                  ULONG allocation_type, ULONG protect)
{
    PVOID b;
    SIZE_T vs = 0;
    LARGE_INTEGER off = {};
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) ||
        (view_size && !NT_SUCCESS(KiCopyFromUser(&vs, view_size, sizeof(vs)))) ||
        (offset && !NT_SUCCESS(KiCopyFromUser(&off, offset, sizeof(off)))))
        return STATUS_ACCESS_VIOLATION;
    PSECTION section;
    NTSTATUS status = ObReferenceHandle(section_handle, MmSectionType, KeGetPreviousMode(), (PVOID *)&section, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process;
    status = PsReferenceProcessHandle(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(section);
        return status;
    }
    /* data mapped as code is dynamic code. */
    if ((protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
        !(section->Flags & SEC_IMAGE) && PsDynamicCodeBlocked(process))
        status = (NTSTATUS)0xC0000604L; /* STATUS_DYNAMIC_CODE_BLOCKED */
    else
        status = MiMapViewOfSectionEx(section, process, &b, (ULONG64)off.QuadPart, &vs, protect, allocation_type,
                                      zero_bits, nullptr);
    if (NT_SUCCESS(status)) {
        KiCopyToUser(base, &b, sizeof(b));
        if (view_size)
            KiCopyToUser(view_size, &vs, sizeof(vs));
    }
    ObDereferenceObject(process);
    ObDereferenceObject(section);
    return status;
}

/*
 * the view services with extended parameters. the section's protection
 * limits come from the access of its handle in windows; mime checks the
 * allocation type and the protection only.
 */
NTSTATUS NTAPI NtMapViewOfSectionEx(HANDLE section_handle, HANDLE process_handle, PVOID *base, PLARGE_INTEGER offset,
                                    PSIZE_T view_size, ULONG allocation_type, ULONG protect,
                                    PMEM_EXTENDED_PARAMETER parameters, ULONG count)
{
    PVOID b;
    SIZE_T vs;
    LARGE_INTEGER off = {};
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) || !NT_SUCCESS(KiCopyFromUser(&vs, view_size, sizeof(vs))) ||
        (offset && !NT_SUCCESS(KiCopyFromUser(&off, offset, sizeof(off)))))
        return STATUS_ACCESS_VIOLATION;
    if (!MiProtectionIsValid(protect & ~(ULONG)PAGE_TARGETS_INVALID))
        return STATUS_INVALID_PAGE_PROTECTION;
    if ((ULONG_PTR)b > 0x7FFFFFFEFFFFull || vs > 0x7FFFFFFF0000ull - (ULONG_PTR)b)
        return STATUS_INVALID_PARAMETER;
    PSECTION section;
    NTSTATUS status = ObReferenceHandle(section_handle, MmSectionType, KeGetPreviousMode(), (PVOID *)&section, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process;
    status = PsReferenceProcessHandle(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(section);
        return status;
    }
    MI_EXTENDED ext;
    status = MiCaptureExtendedParameters(parameters, count, 0x66, KeGetPreviousMode() == UserMode, &ext);
    if (NT_SUCCESS(status) && ((ext.Attributes & ~0x260ull) || ext.Node > 1 || (allocation_type & ~(ULONG)0x20906000)))
        status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(status) && (allocation_type & MEM_REPLACE_PLACEHOLDER) &&
        (ext.Lowest || ext.Highest || ext.Alignment))
        status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(status) && ext.Alignment &&
        (ext.Alignment < 0x10000 || (ext.Alignment & (ext.Alignment - 1)) || ext.Alignment >= 0x7FFFFFFF0000ull))
        status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(status) && b && (ext.Lowest || ext.Highest || ext.Alignment))
        status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(status) && (allocation_type & (MEM_LARGE_PAGES | MEM_ROTATE)))
        status = STATUS_NOT_SUPPORTED;
    if (NT_SUCCESS(status))
        status = MiMapViewOfSectionEx(section, process, &b, (ULONG64)off.QuadPart, &vs,
                                      protect & ~(ULONG)PAGE_TARGETS_INVALID, allocation_type, 0, &ext);
    if (NT_SUCCESS(status)) {
        KiCopyToUser(base, &b, sizeof(b));
        KiCopyToUser(view_size, &vs, sizeof(vs));
    }
    ObDereferenceObject(process);
    ObDereferenceObject(section);
    return status;
}

NTSTATUS NTAPI ZwMapViewOfSection(HANDLE section, HANDLE process, PVOID *base, ULONG_PTR zero_bits, SIZE_T commit,
                                  PLARGE_INTEGER offset, PSIZE_T view_size, SECTION_INHERIT inherit, ULONG allocation,
                                  ULONG protect)
{
    PSECTION s;
    NTSTATUS status = ObReferenceHandle(section, MmSectionType, KernelMode, (PVOID *)&s, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS p;
    status = ObReferenceHandle(process, PsProcessTypeObject, KernelMode, (PVOID *)&p, nullptr);
    if (NT_SUCCESS(status)) {
        SIZE_T vs = view_size ? *view_size : 0;
        status = MiMapViewOfSection(s, p, base, offset ? (ULONG64)offset->QuadPart : 0, &vs, protect, allocation);
        if (NT_SUCCESS(status) && view_size)
            *view_size = vs;
        ObDereferenceObject(p);
    }
    ObDereferenceObject(s);
    return status;
}

NTSTATUS NTAPI NtUnmapViewOfSection(HANDLE process_handle, PVOID base)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    status = MiUnmapViewOfSection(process, base);
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI NtUnmapViewOfSectionEx(HANDLE process_handle, PVOID base, ULONG flags)
{
    if (flags & ~(ULONG)(UNMAP_WITH_TRANSIENT_BOOST | UNMAP_PRESERVE_PLACEHOLDER))
        return STATUS_INVALID_PARAMETER_3;
    if (KeGetPreviousMode() == UserMode && (ULONG_PTR)base > 0x7FFFFFFEFFFFull)
        return STATUS_NOT_MAPPED_VIEW;
    if ((flags & UNMAP_PRESERVE_PLACEHOLDER) && ((ULONG_PTR)base & (PAGE_SIZE - 1)))
        return STATUS_INVALID_PARAMETER_2;
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    status = MiUnmapViewOfSectionEx(process, base, flags);
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI ZwUnmapViewOfSection(HANDLE process, PVOID base)
{
    PEPROCESS p;
    NTSTATUS status = ObReferenceHandle(process, PsProcessTypeObject, KernelMode, (PVOID *)&p, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    status = MiUnmapViewOfSection(p, base);
    ObDereferenceObject(p);
    return status;
}

typedef struct _SECTION_BASIC_INFORMATION {
    PVOID BaseAddress;
    ULONG AllocationAttributes;
    LARGE_INTEGER MaximumSize;
} SECTION_BASIC_INFORMATION;

typedef struct _SECTION_IMAGE_INFORMATION {
    PVOID TransferAddress;
    ULONG ZeroBits;
    SIZE_T MaximumStackSize;
    SIZE_T CommittedStackSize;
    ULONG SubSystemType;
    USHORT SubSystemMinorVersion;
    USHORT SubSystemMajorVersion;
    ULONG OperatingSystemVersion;
    USHORT ImageCharacteristics;
    USHORT DllCharacteristics;
    USHORT Machine;
    BOOLEAN ImageContainsCode;
    UCHAR ImageFlags;
    ULONG LoaderFlags;
    ULONG ImageFileSize;
    ULONG CheckSum;
} SECTION_IMAGE_INFORMATION;

NTSTATUS NTAPI NtQuerySection(HANDLE handle, ULONG klass, PVOID info, SIZE_T length, PSIZE_T returned)
{
    PSECTION s;
    NTSTATUS status = ObReferenceHandle(handle, MmSectionType, KeGetPreviousMode(), (PVOID *)&s, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    union {
        SECTION_BASIC_INFORMATION basic;
        SECTION_IMAGE_INFORMATION image;
    } out;
    SIZE_T size;
    memset(&out, 0, sizeof(out));
    if (klass == 0) {
        out.basic.AllocationAttributes = s->Flags & ~(MI_SECTION_PHYSICAL | MI_SECTION_SHARE_SOURCE | MI_SECTION_WRITABLE_WINDOW);
        out.basic.MaximumSize.QuadPart = (LONGLONG)s->Size;
        size = sizeof(out.basic);
    } else if (klass == 1 && (s->Flags & SEC_IMAGE)) {
        out.image.TransferAddress = (PVOID)(s->Image.ImageBase + s->Image.EntryPoint);
        out.image.MaximumStackSize = s->Image.StackReserve;
        out.image.CommittedStackSize = s->Image.StackCommit;
        out.image.SubSystemType = s->Image.Subsystem;
        out.image.SubSystemMajorVersion = s->Image.SubsystemMajor;
        out.image.SubSystemMinorVersion = s->Image.SubsystemMinor;
        out.image.ImageCharacteristics = s->Image.Characteristics;
        out.image.DllCharacteristics = s->Image.DllCharacteristics;
        out.image.Machine = s->Image.Machine;
        out.image.ImageContainsCode = TRUE;
        size = sizeof(out.image);
    } else {
        ObDereferenceObject(s);
        return STATUS_INVALID_INFO_CLASS;
    }
    if (length < size)
        status = STATUS_INFO_LENGTH_MISMATCH;
    else
        status = KiCopyToUser(info, &out, size);
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    ObDereferenceObject(s);
    return status;
}

void MiInitializeSections(void)
{
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = SECTION_ALL_ACCESS;
    init.DeleteProcedure = section_delete;
    ObSetGenericMapping(&init, 0x20005, 0x20002, 0x20008, 0xF001F);
    MmSectionType = ObCreateObjectType(L"Section", &init);
}
