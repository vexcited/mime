#include "../ntos.h"

/*
 * windows keeps every image of the kernel in PsLoadedModuleList: the kernel, the hal, the boot drivers and each driver
 * loaded since. most of what those images do is built into this kernel, so the list is the one of the reference, row
 * by row in its load order: a row with no object is part of every installation, a row with one is listed while that
 * driver exists, and a row mime loads an image of its own for is that image.
 *
 * the other rows are microsoft's files, which the manifest names: the record of such a row describes the image of its
 * file and its memory is that image, with the relocations of the base it stands at and the base in its headers, as
 * the loader of windows leaves it. nothing runs it and nothing resolves its imports. a row whose file this machine
 * does not have is not listed.
 */
typedef struct {
    PCSTR Path;
    ULONG Flags;
    USHORT LoadCount;
    PCWSTR Object;
} SYSTEM_IMAGE_ROW;

static const SYSTEM_IMAGE_ROW g_rows[] = {
#include "sysmodules.h"
};

/*
 * the pages of an image come on their first use, out of the file: 160 images are 72 MB and a driver reads a few
 * pages of some. the first page of the headers is there from the start. a page is read by the host into a frame of
 * its own, relocated and mapped read only, with interrupts off under one lock, so a read at any irql finds it: on
 * windows the sections that are not pageable are resident, and a pageable one that is resident reads the same.
 */
typedef struct {
    /* the record linked for the row: the one of its file's image below, or the one of an image mime loaded. */
    PKLDR_DATA_TABLE_ENTRY Entry;
    BOOLEAN Tried;
    PUCHAR Base;
    volatile ULONG Pages; /* 0 until the image can be read. */
    ULONG64 Handle;       /* the host's handle of the file. */
    ULONG64 Headers;      /* the frame of the first page. */
    ULONG_PTR Preferred;  /* the base the file was built for. */
    ULONG RelocRva, RelocSize, SizeOfHeaders;
} SYSTEM_IMAGE;

static SYSTEM_IMAGE g_images[RTL_NUMBER_OF(g_rows)];
static KLDR_DATA_TABLE_ENTRY g_records[RTL_NUMBER_OF(g_rows)];
static FAST_MUTEX g_sync_lock;
static volatile LONG g_fill_lock;
static ULONG64 g_scratch; /* a frame for the bytes of a fixup that lies across two pages. */

static WCHAR fold(WCHAR c)
{
    return c >= L'A' && c <= L'Z' ? (WCHAR)(c + 32) : c;
}

static PCSTR leaf_of(PCSTR path)
{
    PCSTR leaf = path + strlen(path);
    while (leaf > path && leaf[-1] != '\\')
        leaf--;
    return leaf;
}

static BOOLEAN leaf_is(PCSTR leaf, PCUNICODE_STRING name)
{
    ULONG n = name->Length / sizeof(WCHAR), i = 0;
    for (; i < n && leaf[i]; i++)
        if (fold((WCHAR)(UCHAR)leaf[i]) != fold(name->Buffer[i]))
            return FALSE;
    return i == n && !leaf[i];
}

BOOLEAN MiIsSystemImageRecord(PKLDR_DATA_TABLE_ENTRY module)
{
    return module >= g_records && module < g_records + RTL_NUMBER_OF(g_records);
}

/* ------------------------------------------------------------------ the pages */

static const IMAGE_NT_HEADERS64 *headers_of(const SYSTEM_IMAGE *image)
{
    const UCHAR *page = HalPhysmap(image->Headers);
    return (const void *)(page + ((const IMAGE_DOS_HEADER *)page)->e_lfanew);
}

static BOOLEAN read_file(const SYSTEM_IMAGE *image, ULONG64 offset, ULONG64 pa, ULONG length)
{
    ULONG64 got = 0;
    return HalHypercall(MIME_HC_FS_READ, image->Handle, offset, pa, length, 0, 0, &got) == MIME_HC_STATUS_OK &&
           got == length;
}

/* the bytes the file has for a run of the image that lies in one page, into zeroed memory at pa. */
static BOOLEAN read_image(const SYSTEM_IMAGE *image, ULONG rva, ULONG length, ULONG64 pa)
{
    const IMAGE_NT_HEADERS64 *nt = headers_of(image);
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    ULONG end = rva + length;
    if (rva < image->SizeOfHeaders) {
        ULONG stop = end < image->SizeOfHeaders ? end : image->SizeOfHeaders;
        if (!read_file(image, rva, pa, stop - rva))
            return FALSE;
    }
    for (USHORT i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        ULONG size = section[i].Misc.VirtualSize ? section[i].Misc.VirtualSize : section[i].SizeOfRawData;
        ULONG raw = section[i].SizeOfRawData < size ? section[i].SizeOfRawData : size;
        ULONG from = rva > section[i].VirtualAddress ? rva : section[i].VirtualAddress;
        ULONG to = end < section[i].VirtualAddress + raw ? end : section[i].VirtualAddress + raw;
        if (from >= to || !section[i].PointerToRawData)
            continue;
        if (!read_file(image, section[i].PointerToRawData + (from - section[i].VirtualAddress), pa + (from - rva),
                       to - from))
            return FALSE;
    }
    return TRUE;
}

static BOOLEAN ensure_page(SYSTEM_IMAGE *image, ULONG index);

/*
 * the fixups of one page, read from the table in the image itself. a fixup that starts in the page before or ends in
 * the page after is computed from the bytes of the file, and the part of it in this page is written.
 */
static BOOLEAN relocate_page(SYSTEM_IMAGE *image, ULONG rva, PUCHAR page)
{
    ULONG64 delta = (ULONG64)((ULONG_PTR)image->Base - image->Preferred);
    if (!delta || !image->RelocSize)
        return TRUE;
    /* the table has no fixups of its own. */
    if (rva + PAGE_SIZE > image->RelocRva && rva < image->RelocRva + image->RelocSize)
        return TRUE;
    for (ULONG at = 0; at + sizeof(IMAGE_BASE_RELOCATION) <= image->RelocSize;) {
        ULONG block_rva = image->RelocRva + at;
        if (!ensure_page(image, block_rva >> PAGE_SHIFT) ||
            !ensure_page(image, (block_rva + sizeof(IMAGE_BASE_RELOCATION) - 1) >> PAGE_SHIFT))
            return FALSE;
        IMAGE_BASE_RELOCATION block;
        memcpy(&block, image->Base + block_rva, sizeof(block));
        if (block.SizeOfBlock < sizeof(block) || block.SizeOfBlock > image->RelocSize - at)
            break;
        if (block.VirtualAddress == rva || block.VirtualAddress + PAGE_SIZE == rva) {
            for (ULONG p = block_rva >> PAGE_SHIFT; p <= (block_rva + block.SizeOfBlock - 1) >> PAGE_SHIFT; p++)
                if (!ensure_page(image, p))
                    return FALSE;
            const UCHAR *entries = image->Base + block_rva + sizeof(block);
            for (ULONG i = 0; i < (block.SizeOfBlock - sizeof(block)) / sizeof(USHORT); i++) {
                USHORT entry;
                memcpy(&entry, entries + i * sizeof(USHORT), sizeof(entry));
                if ((entry >> 12) != IMAGE_REL_BASED_DIR64)
                    continue;
                ULONG target = block.VirtualAddress + (entry & 0xFFF);
                if (target + sizeof(ULONG64) <= rva || target >= rva + PAGE_SIZE)
                    continue;
                ULONG64 value;
                if (target >= rva && target + sizeof(ULONG64) <= rva + PAGE_SIZE) {
                    memcpy(&value, page + (target - rva), sizeof(value));
                    value += delta;
                    memcpy(page + (target - rva), &value, sizeof(value));
                    continue;
                }
                PUCHAR bytes = HalPhysmap(g_scratch);
                memset(bytes, 0, 2 * sizeof(ULONG64));
                ULONG first = PAGE_SIZE - (target & (PAGE_SIZE - 1));
                if (!read_image(image, target, first, g_scratch) ||
                    !read_image(image, target + first, sizeof(ULONG64) - first, g_scratch + first))
                    return FALSE;
                memcpy(&value, bytes, sizeof(value));
                value += delta;
                memcpy(bytes, &value, sizeof(value));
                for (ULONG k = 0; k < sizeof(ULONG64); k++)
                    if (target + k >= rva && target + k < rva + PAGE_SIZE)
                        page[target + k - rva] = bytes[k];
            }
        }
        at += block.SizeOfBlock;
    }
    return TRUE;
}

/* the caller holds the fill lock. */
static BOOLEAN ensure_page(SYSTEM_IMAGE *image, ULONG index)
{
    ULONG_PTR va = (ULONG_PTR)image->Base + (ULONG_PTR)index * PAGE_SIZE;
    if (MiReadPte(MiKernelRoot(), va) & MM_PTE_VALID)
        return TRUE;
    ULONG64 pa = MmAllocatePage();
    if (!pa)
        return FALSE;
    ULONG rva = index << PAGE_SHIFT;
    if (!read_image(image, rva, PAGE_SIZE, pa) || !relocate_page(image, rva, HalPhysmap(pa)) ||
        !MiMapPage(MiKernelRoot(), va, pa, MM_KERNEL_RO)) {
        MmFreePage(pa);
        return FALSE;
    }
    return TRUE;
}

/*
 * a page of such an image that is not there yet is made present, at any irql: for the page fault of a read, and for
 * the routines that ask whether an address is valid or which frame it has. FALSE for any other address.
 */
BOOLEAN MiSystemImageFault(ULONG_PTR va)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_images); i++) {
        SYSTEM_IMAGE *image = &g_images[i];
        ULONG pages = __atomic_load_n(&image->Pages, __ATOMIC_ACQUIRE);
        if (!pages || va < (ULONG_PTR)image->Base || va >= (ULONG_PTR)image->Base + (ULONG_PTR)pages * PAGE_SIZE)
            continue;
        ULONG64 state = KiDisableInterrupts();
        while (__atomic_exchange_n(&g_fill_lock, 1, __ATOMIC_ACQUIRE))
            KiPause();
        BOOLEAN ok = ensure_page(image, (ULONG)((va - (ULONG_PTR)image->Base) >> PAGE_SHIFT));
        __atomic_store_n(&g_fill_lock, 0, __ATOMIC_RELEASE);
        KiRestoreInterrupts(state);
        return ok;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ the records */

/* the file a row stands on, in the rom: \SystemRoot is \Windows there. */
static const MimeRomEntry *row_file(PCSTR path)
{
    static const char root[] = "\\SystemRoot\\";
    if (strncmp(path, root, sizeof(root) - 1))
        return nullptr;
    WCHAR wide[160] = L"\\Windows\\";
    ULONG n = 9;
    PCSTR leaf = leaf_of(path);
    /*
     * the crash dump stack loads its drivers under dump_ names. measured on windows 11 26100: no file has such a name,
     * and the size, the checksum and the time stamp of the row are those of the driver's file.
     */
    BOOLEAN dump = !_strnicmp(leaf, "dump_", 5);
    for (PCSTR p = path + sizeof(root) - 1; *p && n < RTL_NUMBER_OF(wide); p++) {
        if (dump && p == leaf)
            p += 5;
        wide[n++] = (WCHAR)(UCHAR)*p;
    }
    const MimeRomEntry *rom = n < RTL_NUMBER_OF(wide) ? HalRomFind(wide, n) : nullptr;
    return rom && rom->genuine ? rom : nullptr;
}

static void row_path(const SYSTEM_IMAGE_ROW *row, CHAR *path, SIZE_T size)
{
    strncpy(path, row->Path, size - 1);
    path[size - 1] = 0;
#if defined(__x86_64__)
    /* the microcode library is the one of the maker of the processor. */
    CHAR *maker = strstr(path, "AuthenticAMD");
    if (maker && KiProcessorSmbiosManufacturer()[0] == 'I')
        memcpy(maker, "GenuineIntel", 12);
#endif
}

/* opens the file of a row, makes the room of its image and fills its record. */
static BOOLEAN prepare(ULONG index)
{
    const SYSTEM_IMAGE_ROW *row = &g_rows[index];
    SYSTEM_IMAGE *image = &g_images[index];
    CHAR path[160];
    row_path(row, path, sizeof(path));
    const MimeRomEntry *rom = row_file(path);
    if (!rom)
        return FALSE;
    ULONG chars = (ULONG)strlen(path);
    MimeFsInfo *info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*info), KI_TAG('M', 'm', 'S', 'i'));
    PWSTR name = ExAllocatePool2(POOL_FLAG_NON_PAGED, (chars + 1) * sizeof(WCHAR), KI_TAG('M', 'm', 'S', 'i'));
    ULONG64 first = MmAllocatePage(), handle = 0;
    if (!g_scratch)
        g_scratch = MmAllocatePage();
    BOOLEAN ok = info && name && first && g_scratch &&
                 HalHypercallUnlocked(MIME_HC_GENUINE_OPEN, rom->genuine - 1, HalPhysicalOf(info), 0, 0, 0, 0, &handle) ==
                     MIME_HC_STATUS_OK &&
                 handle;
    ULONG length = ok && info->size < PAGE_SIZE ? (ULONG)info->size : PAGE_SIZE;
    ULONG64 got = 0;
    ok = ok && HalHypercallUnlocked(MIME_HC_FS_READ, handle, 0, first, length, 0, 0, &got) == MIME_HC_STATUS_OK &&
         got == length;
    MI_IMAGE_INFO parsed;
    ok = ok && NT_SUCCESS(MiParseImageHeaders(HalPhysmap(first), length, &parsed)) &&
         parsed.Machine == MIME_NATIVE_MACHINE && parsed.BaseFieldWidth == sizeof(ULONG64) &&
         parsed.BaseFieldRva + sizeof(ULONG64) <= PAGE_SIZE;
    PUCHAR page = HalPhysmap(first);
    const IMAGE_NT_HEADERS64 *nt = ok ? (const void *)(page + ((const IMAGE_DOS_HEADER *)page)->e_lfanew) : nullptr;
    /* the table of sections is read from the first page for as long as the image stands. */
    ok = ok && (const UCHAR *)(IMAGE_FIRST_SECTION(nt) + nt->FileHeader.NumberOfSections) <= page + length;
    SIZE_T pages = ok ? BYTES_TO_PAGES(parsed.SizeOfImage) : 0;
    /* measured on windows 11 26100: every image of the list stands on a multiple of 64 KB. */
    PUCHAR room = pages ? MmAllocateSystemVa(pages + 15) : nullptr;
    PUCHAR base = (PUCHAR)(((ULONG_PTR)room + 0xFFFF) & ~(ULONG_PTR)0xFFFF);
    for (SIZE_T i = 0; room && i < pages; i++)
        if (!MiWalk(MiKernelRoot(), (ULONG_PTR)base + i * PAGE_SIZE, TRUE, 3))
            ok = FALSE;
    if (!ok || !room) {
        if (handle)
            HalHypercallUnlocked(MIME_HC_FS_CLOSE, handle, 0, 0, 0, 0, 0, nullptr);
        if (first)
            MmFreePage(first);
        if (info)
            ExFreePool(info);
        if (name)
            ExFreePool(name);
        return FALSE;
    }
    ExFreePool(info);
    /* a file aligned on less than a page has data of its sections after the headers, the image has zeroes. */
    if (parsed.SizeOfHeaders < PAGE_SIZE)
        memset(page + parsed.SizeOfHeaders, 0, PAGE_SIZE - parsed.SizeOfHeaders);
    /* the headers tell the base the image stands at, as the loader of windows leaves them. */
    ULONG64 stands = (ULONG_PTR)base;
    memcpy(page + parsed.BaseFieldRva, &stands, sizeof(stands));
    MiMapPage(MiKernelRoot(), (ULONG_PTR)base, first, MM_KERNEL_RO);

    image->Base = base;
    image->Handle = handle;
    image->Headers = first;
    image->Preferred = parsed.ImageBase;
    image->SizeOfHeaders = parsed.SizeOfHeaders;
    image->RelocRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    image->RelocSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
    if ((ULONG64)image->RelocRva + image->RelocSize > parsed.SizeOfImage)
        image->RelocSize = 0;

    PKLDR_DATA_TABLE_ENTRY m = &g_records[index];
    for (ULONG i = 0; i <= chars; i++)
        name[i] = (WCHAR)(UCHAR)path[i];
    m->FullDllName.Buffer = name;
    m->FullDllName.Length = (USHORT)(chars * sizeof(WCHAR));
    m->FullDllName.MaximumLength = (USHORT)((chars + 1) * sizeof(WCHAR));
    ULONG leaf = (ULONG)(leaf_of(path) - path);
    m->BaseDllName.Buffer = name + leaf;
    m->BaseDllName.Length = (USHORT)((chars - leaf) * sizeof(WCHAR));
    m->BaseDllName.MaximumLength = (USHORT)(m->BaseDllName.Length + sizeof(WCHAR));
    m->DllBase = base;
    m->EntryPoint = parsed.EntryPoint ? base + parsed.EntryPoint : nullptr;
    m->SizeOfImage = parsed.SizeOfImage;
    IMAGE_DATA_DIRECTORY exceptions = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exceptions.VirtualAddress && (ULONG64)exceptions.VirtualAddress + exceptions.Size <= parsed.SizeOfImage) {
        m->ExceptionTable = base + exceptions.VirtualAddress;
        m->ExceptionTableSize = exceptions.Size;
    }
    m->CheckSum = parsed.CheckSum;
    m->TimeDateStamp = nt->FileHeader.TimeDateStamp;
    m->SizeOfImageNotRounded = parsed.SizeOfImage;
    m->Flags = row->Flags;
    m->LoadCount = row->LoadCount;
    __atomic_store_n(&image->Pages, (ULONG)pages, __ATOMIC_RELEASE);
    return TRUE;
}

static BOOLEAN driver_exists(PCWSTR name)
{
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, name);
    PDRIVER_OBJECT driver = nullptr;
    if (!NT_SUCCESS(ObReferenceObjectByName(&n, OBJ_CASE_INSENSITIVE, nullptr, 0, IoDriverObjectType, KernelMode,
                                            nullptr, (PVOID *)&driver)))
        return FALSE;
    ObDereferenceObject(driver);
    return TRUE;
}

/* the link a record of a row goes after: the record of the closest row before it that is listed. */
static PLIST_ENTRY place_of(ULONG index)
{
    while (index--)
        if (g_images[index].Entry)
            return &g_images[index].Entry->InLoadOrderLinks;
    return PsLoadedModuleList.Blink;
}

/*
 * where the record of an image mime loaded goes in the list, called with the list locked: at the place of its row
 * with the flags and the count of the row, or at the end.
 */
PLIST_ENTRY MiPlaceLoadedModule(PKLDR_DATA_TABLE_ENTRY module)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_rows); i++) {
        if (g_images[i].Entry || !leaf_is(leaf_of(g_rows[i].Path), &module->BaseDllName))
            continue;
        g_images[i].Entry = module;
        module->Flags = g_rows[i].Flags;
        module->LoadCount = g_rows[i].LoadCount;
        return i ? place_of(i) : &PsLoadedModuleList;
    }
    return PsLoadedModuleList.Blink;
}

/* an image mime loaded leaves the list, called with the list locked. */
void MiForgetLoadedModule(PKLDR_DATA_TABLE_ENTRY module)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_images); i++)
        if (g_images[i].Entry == module)
            g_images[i].Entry = nullptr;
}

/*
 * brings the list to what the rows say now: the record of each row whose driver exists is linked at its place, the
 * one of a driver that went is unlinked. called before the list is told to a program and before a foreign driver
 * starts, so the files are opened by the first of them and not when the machine starts.
 */
void MiSyncSystemImages(void)
{
    ExAcquireFastMutex(&g_sync_lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_rows); i++) {
        SYSTEM_IMAGE *image = &g_images[i];
        PKLDR_DATA_TABLE_ENTRY own = &g_records[i];
        /* a row with "*" is an image mime loads itself. */
        if (g_rows[i].Object && g_rows[i].Object[0] == L'*')
            continue;
        BOOLEAN wanted = !g_rows[i].Object || driver_exists(g_rows[i].Object);
        if (wanted && !image->Tried && !image->Entry) {
            image->Tried = TRUE;
            prepare(i);
        }
        MiLockLoadedModules();
        if (wanted && !image->Entry && image->Pages) {
            InsertHeadList(place_of(i), &own->InLoadOrderLinks);
            image->Entry = own;
        } else if (!wanted && image->Entry == own) {
            /* the record keeps its links, a reader that stands on it goes on from there. */
            RemoveEntryList(&own->InLoadOrderLinks);
            image->Entry = nullptr;
        }
        MiUnlockLoadedModules();
    }
    ExReleaseFastMutex(&g_sync_lock);
}

void MiInitializeSystemImages(void)
{
    ExInitializeFastMutex(&g_sync_lock);
}
