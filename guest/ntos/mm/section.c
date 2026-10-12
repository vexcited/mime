/*
 * section objects: image sections built from pe files, file backed and
 * pagefile backed data sections, and mapping views of them.
 *
 * an image section keeps one prototype copy of the image laid out at its
 * preferred base. views at that base share read-only pages; writable
 * pages are private copies. relocated views get private copies of every
 * page with the base relocations applied.
 *
 * images of the system files in the rom are built once and cached. their
 * prototype pages are the rom pages themselves whenever the file page is
 * byte for byte the loaded page, which the 4K file alignment of mime's
 * images makes the common case.
 */
#include "../ntos.h"

POBJECT_TYPE MmSectionType;
POBJECT_TYPE *MmSectionObjectType = &MmSectionType;

static void section_delete(PVOID object)
{
    PSECTION s = object;
    if (s->SystemVa) {
        for (ULONG i = 0; i < s->PageCount; i++)
            MiUnmapPage(MiKernelRoot(), (ULONG_PTR)s->SystemVa + (ULONG_PTR)i * PAGE_SIZE, 0);
        MmFreeSystemVa(s->SystemVa, s->PageCount);
    }
    if (s->Pages) {
        for (ULONG i = 0; i < s->PageCount && !(s->Flags & MI_SECTION_PHYSICAL); i++)
            if (s->Pages[i] && !HalIsRomPhysical(s->Pages[i]))
                MmFreePage(s->Pages[i]);
        ExFreePool(s->Pages);
    }
    if (s->PageProtect)
        ExFreePool(s->PageProtect);
    if (s->File)
        ObDereferenceObject(s->File);
    if (s->FileName.Buffer)
        ExFreePool(s->FileName.Buffer);
}

static NTSTATUS alloc_section(ULONG64 size, PSECTION *out)
{
    PSECTION s;
    NTSTATUS status = ObCreateObject(MmSectionType, NULL, KernelMode, sizeof(SECTION), (PVOID *)&s);
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
    for (ULONG i = 0; i < s->PageCount; i++) {
        s->Pages[i] = MmAllocatePage();
        if (!s->Pages[i]) {
            ObDereferenceObject(s);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    *out = s;
    return STATUS_SUCCESS;
}

/* copies bytes into the section's page array at a byte offset. */
static void scatter(PSECTION s, ULONG64 offset, const void *data, SIZE_T length)
{
    const UCHAR *src = data;
    while (length) {
        ULONG page = (ULONG)(offset >> PAGE_SHIFT);
        ULONG in = (ULONG)(offset & (PAGE_SIZE - 1));
        SIZE_T n = PAGE_SIZE - in < length ? PAGE_SIZE - in : length;
        if (page >= s->PageCount)
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
    if (dos->e_lfanew < 0 || (SIZE_T)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > length)
        return STATUS_INVALID_IMAGE_FORMAT;
    const IMAGE_NT_HEADERS64 *nt = (const void *)((const UCHAR *)headers + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;
    if ((nt->FileHeader.Machine != IMAGE_FILE_MACHINE_ARM64 && nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return STATUS_INVALID_IMAGE_FORMAT;
    const IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;
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
    info->Relocatable = !(nt->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED) &&
                        opt->NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_BASERELOC &&
                        opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size != 0;
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

static NTSTATUS create_rom_image(PFILE_OBJECT file, const MimeRomEntry *rom, PSECTION *out)
{
    ULONG index = (ULONG)(rom - HalRomEntry(0));
    BOOLEAN cacheable = index < RTL_NUMBER_OF(g_rom_images);
    if (cacheable) {
        PSECTION cached = __atomic_load_n(&g_rom_images[index], __ATOMIC_ACQUIRE);
        if (cached) {
            ObReferenceObject(cached);
            *out = cached;
            return STATUS_SUCCESS;
        }
    }

    ULONG64 rom_pa = HalRomPhysical(rom);
    const UCHAR *data = HalPhysmap(rom_pa);
    MI_IMAGE_INFO info;
    NTSTATUS status = MiParseImageHeaders(data, rom->size, &info);
    if (!NT_SUCCESS(status))
        return status;
    const IMAGE_NT_HEADERS64 *nt = (const void *)(data + ((const IMAGE_DOS_HEADER *)data)->e_lfanew);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (USHORT i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if ((ULONG64)sec[i].PointerToRawData + sec[i].SizeOfRawData > rom->size)
            return STATUS_INVALID_IMAGE_FORMAT;

    PSECTION s;
    status = ObCreateObject(MmSectionType, NULL, KernelMode, sizeof(SECTION), (PVOID *)&s);
    if (!NT_SUCCESS(status))
        return status;
    memset(s, 0, sizeof(*s));
    s->Flags = SEC_IMAGE;
    s->Protect = PAGE_EXECUTE_WRITECOPY;
    s->Image = info;
    s->Size = info.SizeOfImage;
    s->PageCount = (ULONG)BYTES_TO_PAGES(info.SizeOfImage);
    s->RelocRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    s->RelocSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
    s->Pages = ExAllocatePool2(POOL_FLAG_NON_PAGED, (s->PageCount + 1) * sizeof(ULONG64), KI_TAG('M', 'm', 'S', 'p'));
    PUCHAR scratch = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, KI_TAG('M', 'm', 'R', 'i'));
    status = s->Pages && scratch ? image_protections(s, nt) : STATUS_INSUFFICIENT_RESOURCES;

    ULONG64 rom_bytes = (rom->size + PAGE_SIZE - 1) & ~(ULONG64)(PAGE_SIZE - 1); /* the rom pads files with zeros */
    for (ULONG i = 0; i < s->PageCount && NT_SUCCESS(status); i++) {
        ULONG64 off = (ULONG64)i << PAGE_SHIFT;
        load_image_page(data, rom->size, nt, i, scratch);
        if (off + PAGE_SIZE <= rom_bytes && memcmp(scratch, data + off, PAGE_SIZE) == 0) {
            s->Pages[i] = rom_pa + off;
            continue;
        }
        s->Pages[i] = MmAllocatePageNoZero();
        if (!s->Pages[i])
            status = STATUS_INSUFFICIENT_RESOURCES;
        else
            memcpy(HalPhysmap(s->Pages[i]), scratch, PAGE_SIZE);
    }
    ExFreePool(scratch);
    if (NT_SUCCESS(status))
        status = set_file_name(s, file);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(s);
        return status;
    }
    ObReferenceObject(file);
    s->File = file;
    if (cacheable) {
        PSECTION expected = NULL;
        ObReferenceObject(s);
        if (!__atomic_compare_exchange_n(&g_rom_images[index], &expected, s, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            ObDereferenceObject(s);
    }
    *out = s;
    return STATUS_SUCCESS;
}

NTSTATUS MiCreateImageSection(PFILE_OBJECT file, PSECTION *out)
{
    const MimeRomEntry *rom = IoGetRomFile(file);
    if (rom)
        return create_rom_image(file, rom, out);

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
    s->RelocRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    s->RelocSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
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
        status = copy_file_into(s, file, 0, 0, size);
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

static void apply_relocations(PSECTION s, ULONG64 *pages, ULONG_PTR base)
{
    LONG64 delta = (LONG64)(base - s->Image.ImageBase);
    ULONG off = 0;
    while (off + sizeof(IMAGE_BASE_RELOCATION) <= s->RelocSize) {
        IMAGE_BASE_RELOCATION block;
        ULONG64 rva = s->RelocRva + off;
        if (rva + sizeof(block) > s->Size)
            return;
        memcpy(&block, (PUCHAR)HalPhysmap(pages[rva >> PAGE_SHIFT]) + (rva & (PAGE_SIZE - 1)), sizeof(block));
        if (block.SizeOfBlock < sizeof(block) || off + block.SizeOfBlock > s->RelocSize)
            return;
        for (ULONG i = sizeof(block); i + 2 <= block.SizeOfBlock; i += 2) {
            ULONG64 erva = s->RelocRva + off + i;
            USHORT entry;
            memcpy(&entry, (PUCHAR)HalPhysmap(pages[erva >> PAGE_SHIFT]) + (erva & (PAGE_SIZE - 1)), 2);
            if ((entry >> 12) != IMAGE_REL_BASED_DIR64)
                continue;
            ULONG64 target = block.VirtualAddress + (entry & 0xFFF);
            if (target + 8 > s->Size)
                continue;
            /* the 8 bytes may straddle two pages. */
            UCHAR bytes[8];
            for (int b = 0; b < 8; b++)
                bytes[b] = *((PUCHAR)HalPhysmap(pages[(target + b) >> PAGE_SHIFT]) + ((target + b) & (PAGE_SIZE - 1)));
            ULONG64 v;
            memcpy(&v, bytes, 8);
            v += (ULONG64)delta;
            memcpy(bytes, &v, 8);
            for (int b = 0; b < 8; b++)
                *((PUCHAR)HalPhysmap(pages[(target + b) >> PAGE_SHIFT]) + ((target + b) & (PAGE_SIZE - 1))) = bytes[b];
        }
        off += block.SizeOfBlock;
    }
}

NTSTATUS MiMapViewOfSection(PSECTION s, PEPROCESS process, PVOID *base, ULONG64 offset, SIZE_T *view_size,
                            ULONG protect, ULONG allocation_type)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    BOOLEAN image = (s->Flags & SEC_IMAGE) != 0;
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

    ExAcquireFastMutex(&as->Lock);
    ULONG_PTR va = (ULONG_PTR)*base;
    BOOLEAN relocated = FALSE;
    NTSTATUS status = STATUS_SUCCESS;
    if (image) {
        if (!va)
            va = s->Image.ImageBase;
        if (!MiRangeIsFree(as, va, va + bytes)) {
            if (*base || !s->Image.Relocatable) {
                ExReleaseFastMutex(&as->Lock);
                return STATUS_CONFLICTING_ADDRESSES;
            }
            status = MiFindFreeRange(as, bytes, 0x10000, TRUE, &va);
            relocated = TRUE;
        }
        relocated = relocated || va != s->Image.ImageBase;
    } else if (va) {
        va &= ~(ULONG_PTR)(PAGE_SIZE - 1);
        if (!MiRangeIsFree(as, va, va + bytes))
            status = STATUS_CONFLICTING_ADDRESSES;
    } else {
        status = MiFindFreeRange(as, bytes, 0x10000, (allocation_type & MEM_TOP_DOWN) != 0, &va);
    }
    if (!NT_SUCCESS(status)) {
        ExReleaseFastMutex(&as->Lock);
        return status;
    }

    PMMVAD vad = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MMVAD), KI_TAG('M', 'm', 'V', 'd'));
    ULONG64 *private_pages = NULL;
    if (vad && image && relocated)
        private_pages = ExAllocatePool2(POOL_FLAG_NON_PAGED, pages * sizeof(ULONG64), KI_TAG('M', 'm', 'R', 'p'));
    if (!vad || (image && relocated && !private_pages)) {
        ExFreePool(vad);
        ExReleaseFastMutex(&as->Lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    vad->StartVa = va;
    vad->EndVa = va + bytes;
    vad->Type = image ? MiVadImage : MiVadMapped;
    vad->InitialProtect = image ? PAGE_EXECUTE_WRITECOPY : protect;
    vad->Section = s;
    vad->SectionOffset = first_page << PAGE_SHIFT;
    ObReferenceObject(s);
    MiInsertVad(as, vad);

    if (image && relocated) {
        for (SIZE_T i = 0; i < pages; i++) {
            private_pages[i] = MmAllocatePageNoZero();
            if (!private_pages[i]) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            memcpy(HalPhysmap(private_pages[i]), HalPhysmap(s->Pages[i]), PAGE_SIZE);
        }
        if (NT_SUCCESS(status) && s->RelocSize)
            apply_relocations(s, private_pages, va);
    }

    for (SIZE_T i = 0; i < pages && NT_SUCCESS(status); i++) {
        ULONG_PTR pva = va + (i << PAGE_SHIFT);
        ULONG64 pa;
        ULONG prot;
        ULONG64 extra = 0;
        if (image) {
            prot = s->PageProtect[i];
            if (relocated) {
                pa = private_pages[i];
            } else if (prot & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) {
                pa = MmAllocatePageNoZero();
                if (!pa) {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                memcpy(HalPhysmap(pa), HalPhysmap(s->Pages[i]), PAGE_SIZE);
            } else {
                pa = s->Pages[i];
                extra = MM_PTE_SW_SHARED;
            }
        } else {
            prot = protect;
            pa = s->Pages[first_page + i];
            extra = MM_PTE_SW_SHARED;
        }
        if (!MiMapPage(as->Root, pva, pa, MiProtectToPte(prot, TRUE) | extra))
            status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (private_pages)
        ExFreePool(private_pages);
    if (!NT_SUCCESS(status)) {
        ExReleaseFastMutex(&as->Lock);
        MiUnmapViewOfSection(process, (PVOID)va);
        return status;
    }
    ExReleaseFastMutex(&as->Lock);
    *base = (PVOID)va;
    *view_size = bytes;
    return relocated ? STATUS_IMAGE_NOT_AT_BASE : STATUS_SUCCESS;
}

NTSTATUS MiUnmapViewOfSection(PEPROCESS process, PVOID base)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    ExAcquireFastMutex(&as->Lock);
    PMMVAD vad = MiFindVad(as, (ULONG_PTR)base);
    if (!vad || (vad->Type != MiVadImage && vad->Type != MiVadMapped)) {
        ExReleaseFastMutex(&as->Lock);
        return STATUS_NOT_MAPPED_VIEW;
    }
    for (ULONG_PTR va = vad->StartVa; va < vad->EndVa; va += PAGE_SIZE) {
        ULONG64 *pte = MiWalk(as->Root, va, FALSE, 3);
        if (!pte || !*pte)
            continue;
        ULONG64 old = *pte;
        *pte = 0;
        if (old & MM_PTE_VALID) {
            MiFlushTlbPage(va, as->Asid, FALSE);
            if (!(old & MM_PTE_SW_SHARED))
                MmFreePage(old & MM_PTE_ADDR_MASK);
        }
    }
    MiRemoveVad(as, vad);
    ExReleaseFastMutex(&as->Lock);
    if (vad->Section)
        ObDereferenceObject(vad->Section);
    ExFreePool(vad);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------ kernel shared sections */

NTSTATUS NTAPI MmCreateSharedSection(SIZE_T size, PVOID *kernel_base, PVOID *section)
{
    PSECTION s;
    NTSTATUS status = MiCreateDataSection(NULL, size, PAGE_READWRITE, SEC_COMMIT, &s);
    if (!NT_SUCCESS(status))
        return status;
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
    NTSTATUS status = ObCreateObject(MmSectionType, NULL, KernelMode, sizeof(SECTION), (PVOID *)&s);
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
    *user_base = NULL;
    return MiMapViewOfSection(section, process, user_base, 0, &size, writable ? PAGE_READWRITE : PAGE_READONLY,
                              MEM_TOP_DOWN);
}

NTSTATUS NTAPI MmMapViewInSystemSpace(PVOID section, PVOID *base, PSIZE_T size)
{
    PSECTION s = section;
    if (!s->SystemVa)
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
    LARGE_INTEGER size = {0};
    if (maximum_size && !NT_SUCCESS(KiCopyFromUser(&size, maximum_size, sizeof(size))))
        return STATUS_ACCESS_VIOLATION;
    PFILE_OBJECT file = NULL;
    NTSTATUS status;
    if (file_handle) {
        status = ObReferenceHandle(file_handle, IoFileObjectTypeObject, KeGetPreviousMode(), (PVOID *)&file, NULL);
        if (!NT_SUCCESS(status))
            return status;
    }
    PSECTION section;
    if (allocation & SEC_IMAGE) {
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
    status = ObInsertObjectNamed(section, attributes ? &oa : NULL, access, KeGetPreviousMode(), &h);
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
    status = ObOpenObjectByName(&oa, MmSectionType, KeGetPreviousMode(), access, NULL, &h);
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
    LARGE_INTEGER off = {0};
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) ||
        (view_size && !NT_SUCCESS(KiCopyFromUser(&vs, view_size, sizeof(vs)))) ||
        (offset && !NT_SUCCESS(KiCopyFromUser(&off, offset, sizeof(off)))))
        return STATUS_ACCESS_VIOLATION;
    PSECTION section;
    NTSTATUS status = ObReferenceHandle(section_handle, MmSectionType, KeGetPreviousMode(), (PVOID *)&section, NULL);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process;
    status = PsReferenceProcessHandle(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(section);
        return status;
    }
    status = MiMapViewOfSection(section, process, &b, (ULONG64)off.QuadPart, &vs, protect, allocation_type);
    if (NT_SUCCESS(status)) {
        KiCopyToUser(base, &b, sizeof(b));
        if (view_size)
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
    NTSTATUS status = ObReferenceHandle(section, MmSectionType, KernelMode, (PVOID *)&s, NULL);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS p;
    status = ObReferenceHandle(process, PsProcessTypeObject, KernelMode, (PVOID *)&p, NULL);
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
    return NtUnmapViewOfSection(process_handle, base);
}

NTSTATUS NTAPI ZwUnmapViewOfSection(HANDLE process, PVOID base)
{
    PEPROCESS p;
    NTSTATUS status = ObReferenceHandle(process, PsProcessTypeObject, KernelMode, (PVOID *)&p, NULL);
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
    NTSTATUS status = ObReferenceHandle(handle, MmSectionType, KeGetPreviousMode(), (PVOID *)&s, NULL);
    if (!NT_SUCCESS(status))
        return status;
    union {
        SECTION_BASIC_INFORMATION basic;
        SECTION_IMAGE_INFORMATION image;
    } out;
    SIZE_T size;
    memset(&out, 0, sizeof(out));
    if (klass == 0) {
        out.basic.AllocationAttributes = s->Flags;
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
    OBJECT_TYPE_INITIALIZER init = {0};
    init.ValidAccessMask = SECTION_ALL_ACCESS;
    init.DeleteProcedure = section_delete;
    MmSectionType = ObCreateObjectType(L"Section", &init);
}
