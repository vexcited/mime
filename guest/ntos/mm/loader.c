/*
 * kernel mode image loader for drivers, and the system dlls mapped into
 * every process: ntdll.dll, and xta64.dll for x86 and x64 programs.
 */
#include "../ntos.h"

/*
 * the kernel and every image loaded after it, in load order, as KLDR_DATA_TABLE_ENTRY records drivers read through
 * DriverSection. entries are only added and unlinked under the mutex, and an unlinked entry stays allocated, so a
 * reader such as the exception dispatcher walks the list without a lock at any irql.
 */
LIST_ENTRY PsLoadedModuleList = {&PsLoadedModuleList, &PsLoadedModuleList};
ERESOURCE PsLoadedModuleResource;
static FAST_MUTEX g_modules_lock;
static PKLDR_DATA_TABLE_ENTRY g_kernel_module;
BOOLEAN MiTraceRoutines;

#define MI_MAX_IMPORT_DEPTH 8

static PIMAGE_NT_HEADERS64 image_headers(PVOID base)
{
    return (PIMAGE_NT_HEADERS64)((PUCHAR)base + ((PIMAGE_DOS_HEADER)base)->e_lfanew);
}

static PVOID find_export_in(PKLDR_DATA_TABLE_ENTRY module, const char *name, USHORT ordinal, BOOLEAN by_ordinal,
                            ULONG depth);

/* a forwarder names another module's export as "module.name" or "module.#ordinal". */
static PVOID resolve_forwarder(const char *forwarder, ULONG depth);

static PVOID find_export_raw(PVOID base, const char *name, USHORT ordinal, BOOLEAN by_ordinal, const char **forwarder)
{
    *forwarder = nullptr;
    IMAGE_DATA_DIRECTORY dir = image_headers(base)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return nullptr;
    PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)base + dir.VirtualAddress);
    PULONG functions = (PULONG)((PUCHAR)base + exp->AddressOfFunctions);
    ULONG rva = 0;
    if (by_ordinal) {
        ULONG index = (ULONG)ordinal - exp->Base;
        if (index >= exp->NumberOfFunctions)
            return nullptr;
        rva = functions[index];
    } else {
        PULONG names = (PULONG)((PUCHAR)base + exp->AddressOfNames);
        PUSHORT ordinals = (PUSHORT)((PUCHAR)base + exp->AddressOfNameOrdinals);
        LONG lo = 0, hi = (LONG)exp->NumberOfNames - 1;
        while (lo <= hi) {
            LONG mid = (lo + hi) / 2;
            int c = strcmp(name, (const char *)base + names[mid]);
            if (c == 0) {
                rva = functions[ordinals[mid]];
                break;
            }
            if (c < 0)
                hi = mid - 1;
            else
                lo = mid + 1;
        }
    }
    if (!rva)
        return nullptr;
    if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) {
        *forwarder = (const char *)base + rva;
        return nullptr;
    }
    return (PUCHAR)base + rva;
}

static PVOID find_export(PVOID base, const char *name, USHORT ordinal, BOOLEAN by_ordinal)
{
    const char *forwarder;
    PVOID found = find_export_raw(base, name, ordinal, by_ordinal, &forwarder);
    return found ? found : forwarder ? resolve_forwarder(forwarder, 0) : nullptr;
}

static BOOLEAN ascii_equal_unicode(const char *ascii, PCUNICODE_STRING name)
{
    ULONG n = name->Length / sizeof(WCHAR), i = 0;
    for (; i < n && ascii[i]; i++)
        if (RtlUpcaseUnicodeChar((WCHAR)(UCHAR)ascii[i]) != RtlUpcaseUnicodeChar(name->Buffer[i]))
            return FALSE;
    return i == n && !ascii[i];
}

static BOOLEAN is_kernel_alias(const char *name)
{
    static const char *const aliases[] = {"ntoskrnl.exe", "ntkrnlmp.exe", "ntkrnlpa.exe", "ntkrpamp.exe", "hal.dll"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(aliases); i++)
        if (!_stricmp(name, aliases[i]))
            return TRUE;
    return FALSE;
}

/* a loaded module by file name, the kernel for the names windows gives its kernel and hal. */
static PKLDR_DATA_TABLE_ENTRY module_by_name(const char *name)
{
    if (is_kernel_alias(name))
        return g_kernel_module;
    for (PLIST_ENTRY l = PsLoadedModuleList.Flink; l != &PsLoadedModuleList; l = l->Flink) {
        PKLDR_DATA_TABLE_ENTRY m = CONTAINING_RECORD(l, KLDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (ascii_equal_unicode(name, &m->BaseDllName))
            return m;
    }
    return nullptr;
}

PKLDR_DATA_TABLE_ENTRY MiLookupModule(ULONG_PTR address)
{
    for (PLIST_ENTRY l = PsLoadedModuleList.Flink; l != &PsLoadedModuleList; l = l->Flink) {
        PKLDR_DATA_TABLE_ENTRY m = CONTAINING_RECORD(l, KLDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (address >= (ULONG_PTR)m->DllBase && address < (ULONG_PTR)m->DllBase + m->SizeOfImage)
            return m;
    }
    return nullptr;
}

static PVOID find_export_in(PKLDR_DATA_TABLE_ENTRY module, const char *name, USHORT ordinal, BOOLEAN by_ordinal,
                            ULONG depth)
{
    const char *forwarder;
    PVOID found = find_export_raw(module->DllBase, name, ordinal, by_ordinal, &forwarder);
    return found ? found : forwarder ? resolve_forwarder(forwarder, depth + 1) : nullptr;
}

static PVOID resolve_forwarder(const char *forwarder, ULONG depth)
{
    const char *dot = strchr(forwarder, '.');
    if (!dot || depth > MI_MAX_IMPORT_DEPTH || dot - forwarder > 60)
        return nullptr;
    char module[72];
    memcpy(module, forwarder, (SIZE_T)(dot - forwarder));
    module[dot - forwarder] = 0;
    if (!strchr(module, '.'))
        strcat(module, ".sys");
    PKLDR_DATA_TABLE_ENTRY m = module_by_name(module);
    if (!m && !strcmp(module + (dot - forwarder), ".sys")) {
        module[dot - forwarder] = 0;
        strcat(module, ".dll");
        m = module_by_name(module);
    }
    if (!m)
        return nullptr;
    if (dot[1] == '#') {
        ULONG ordinal = 0;
        for (const char *p = dot + 2; *p >= '0' && *p <= '9'; p++)
            ordinal = ordinal * 10 + (ULONG)(*p - '0');
        return find_export_in(m, nullptr, (USHORT)ordinal, TRUE, depth);
    }
    return find_export_in(m, dot + 1, 0, FALSE, depth);
}

static PKLDR_DATA_TABLE_ENTRY add_module(PVOID base, SIZE_T size, PVOID entry, PCUNICODE_STRING full_name)
{
    PKLDR_DATA_TABLE_ENTRY m = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*m) + full_name->Length + sizeof(WCHAR),
                                               KI_TAG('M', 'm', 'L', 'm'));
    if (!m)
        return nullptr;
    memset(m, 0, sizeof(*m));
    PWSTR name = (PWSTR)(m + 1);
    memcpy(name, full_name->Buffer, full_name->Length);
    name[full_name->Length / sizeof(WCHAR)] = 0;
    m->FullDllName.Buffer = name;
    m->FullDllName.Length = full_name->Length;
    m->FullDllName.MaximumLength = (USHORT)(full_name->Length + sizeof(WCHAR));
    PWSTR leaf = name + full_name->Length / sizeof(WCHAR);
    while (leaf > name && leaf[-1] != L'\\')
        leaf--;
    m->BaseDllName.Buffer = leaf;
    m->BaseDllName.Length = (USHORT)(full_name->Length - (USHORT)((PUCHAR)leaf - (PUCHAR)name));
    m->BaseDllName.MaximumLength = (USHORT)(m->BaseDllName.Length + sizeof(WCHAR));
    m->DllBase = base;
    m->EntryPoint = entry;
    m->SizeOfImage = (ULONG)size;
    PIMAGE_NT_HEADERS64 nt = image_headers(base);
    IMAGE_DATA_DIRECTORY exceptions = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exceptions.VirtualAddress) {
        m->ExceptionTable = (PUCHAR)base + exceptions.VirtualAddress;
        m->ExceptionTableSize = exceptions.Size;
    }
    m->CheckSum = nt->OptionalHeader.CheckSum;
    m->TimeDateStamp = nt->FileHeader.TimeDateStamp;
    m->SizeOfImageNotRounded = nt->OptionalHeader.SizeOfImage;
    m->Flags = LDRP_ENTRY_PROCESSED;
    m->LoadCount = 1;
    ExAcquireFastMutex(&g_modules_lock);
    InsertTailList(&PsLoadedModuleList, &m->InLoadOrderLinks);
    ExReleaseFastMutex(&g_modules_lock);
    return m;
}

/* the kernel is the first module, as on windows, under the name windows gives it. */
void MiInitializeLoadedModules(void)
{
    ExInitializeFastMutex(&g_modules_lock);
    ExInitializeResourceLite(&PsLoadedModuleResource);
    PVOID base = (PVOID)HalBootInfo->kernel_virt;
    PIMAGE_NT_HEADERS64 nt = image_headers(base);
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\SystemRoot\\system32\\ntoskrnl.exe");
    g_kernel_module =
        add_module(base, nt->OptionalHeader.SizeOfImage, (PUCHAR)base + nt->OptionalHeader.AddressOfEntryPoint, &name);
}

PKLDR_DATA_TABLE_ENTRY MiKernelModule(void)
{
    return g_kernel_module;
}

/* the images loaded into the kernel, the kernel first. */
ULONG MiEnumerateSystemImages(MI_SYSTEM_IMAGE_CALLBACK callback, PVOID context)
{
    ULONG index = 0;
    ExAcquireFastMutex(&g_modules_lock);
    for (PLIST_ENTRY l = PsLoadedModuleList.Flink; l != &PsLoadedModuleList; l = l->Flink) {
        PKLDR_DATA_TABLE_ENTRY m = CONTAINING_RECORD(l, KLDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        callback(context, index++, m->DllBase, m->SizeOfImage, &m->FullDllName);
    }
    ExReleaseFastMutex(&g_modules_lock);
    return index;
}

BOOLEAN MiIsCodeAddress(ULONG_PTR address)
{
    return MiLookupModule(address) != nullptr;
}

/* image name and offset in the image, which the linker maps give the function for. */
void MiDescribeCodeAddress(ULONG_PTR address, char *out, SIZE_T size)
{
    PKLDR_DATA_TABLE_ENTRY m = MiLookupModule(address);
    if (!m) {
        mime_format(out, size, "%p", (PVOID)address);
        return;
    }
    char text[64];
    ULONG n = 0;
    for (; n < m->BaseDllName.Length / sizeof(WCHAR) && n < sizeof(text) - 1; n++) {
        WCHAR c = m->BaseDllName.Buffer[n];
        text[n] = c < 0x80 ? (char)c : '?';
    }
    text[n] = 0;
    /* the kernel keeps its old short name in logs, which tools symbolize from. */
    if (m == g_kernel_module)
        mime_format(out, size, "ntoskrnl+0x%llx", (ULONG64)(address - (ULONG_PTR)m->DllBase));
    else
        mime_format(out, size, "%s+0x%llx", text, (ULONG64)(address - (ULONG_PTR)m->DllBase));
}

PVOID MiFindKernelExport(PVOID image_base, const char *name)
{
    return find_export(image_base, name, 0, FALSE);
}

/* a stand in for routines the kernel lacks, while finding out which ones a driver looks up. */
BOOLEAN MiDiscoverRoutines;

static ULONG_PTR NTAPI MiMissingRoutine(void)
{
    char caller[96];
    MiDescribeCodeAddress((ULONG_PTR)__builtin_return_address(0), caller, sizeof(caller));
    KiLogPrintf("a missing routine was called from %s", caller);
    return 0;
}

/* the routines of the kernel and the hal, which mime's kernel holds both of. */
PVOID NTAPI MmGetSystemRoutineAddress(PUNICODE_STRING name)
{
    char ansi[128];
    ULONG n = name ? name->Length / sizeof(WCHAR) : 0;
    if (!n || n >= sizeof(ansi))
        return nullptr;
    for (ULONG i = 0; i < n; i++)
        ansi[i] = (char)name->Buffer[i];
    ansi[n] = 0;
    PVOID found = find_export((PVOID)HalBootInfo->kernel_virt, ansi, 0, FALSE);
    if (!found && MiDiscoverRoutines)
        found = (PVOID)MiMissingRoutine;
    if (!found || MiTraceRoutines) {
        char caller[96];
        MiDescribeCodeAddress((ULONG_PTR)__builtin_return_address(0), caller, sizeof(caller));
        KiLogPrintf("MmGetSystemRoutineAddress(%s) = %p from %s", ansi, found, caller);
    }
    return found;
}

/* an export of a loaded module, for the filter manager's FltGetRoutineAddress. */
PVOID MiFindModuleExport(const char *module, const char *name)
{
    PKLDR_DATA_TABLE_ENTRY m = module_by_name(module);
    return m ? find_export_in(m, name, 0, FALSE, 0) : nullptr;
}

NTSTATUS MiReadFileBytes(PFILE_OBJECT file, ULONG64 offset, PVOID buffer, ULONG length, PULONG read)
{
    return IoReadFileObject(file, buffer, length, offset, read);
}

static NTSTATUS read_whole_file(PCUNICODE_STRING path, PUCHAR *data, ULONG *size)
{
    PFILE_OBJECT file;
    NTSTATUS status = IopOpenFileObject(path, FILE_READ_DATA | SYNCHRONIZE, FILE_NON_DIRECTORY_FILE, &file);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 file_size;
    status = IoQueryFileSize(file, &file_size);
    if (NT_SUCCESS(status) && (file_size == 0 || file_size > 64 * 1024 * 1024))
        status = STATUS_INVALID_IMAGE_FORMAT;
    if (NT_SUCCESS(status)) {
        *data = ExAllocatePool2(POOL_FLAG_NON_PAGED, file_size, KI_TAG('M', 'm', 'L', 'd'));
        if (!*data)
            status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (NT_SUCCESS(status)) {
        ULONG got = 0;
        status = IoReadFileObject(file, *data, (ULONG)file_size, 0, &got);
        if (NT_SUCCESS(status) && got != file_size)
            status = STATUS_END_OF_FILE;
        if (!NT_SUCCESS(status))
            ExFreePool(*data);
        *size = (ULONG)file_size;
    }
    ObDereferenceObject(file);
    return status;
}

static void free_image_pages(PUCHAR base, SIZE_T pages)
{
    for (SIZE_T i = 0; i < pages; i++) {
        ULONG64 pa = MiUnmapPage(MiKernelRoot(), (ULONG_PTR)base + i * PAGE_SIZE, 0) & MM_PTE_ADDR_MASK;
        if (pa)
            MmFreePage(pa);
    }
    MmFreeSystemVa(base, pages);
}

static NTSTATUS load_image(PCUNICODE_STRING path, ULONG depth, PKLDR_DATA_TABLE_ENTRY *out);

/* the module an import names: loaded already, or a driver of System32\drivers that is loaded for it. */
static PKLDR_DATA_TABLE_ENTRY import_module(const char *dll, ULONG depth, NTSTATUS *status)
{
    *status = STATUS_SUCCESS;
    PKLDR_DATA_TABLE_ENTRY m = module_by_name(dll);
    if (m)
        return m;
    if (depth >= MI_MAX_IMPORT_DEPTH || strlen(dll) > 60) {
        *status = STATUS_DLL_NOT_FOUND;
        return nullptr;
    }
    WCHAR path[128] = L"\\SystemRoot\\System32\\drivers\\";
    ULONG at = (ULONG)wcslen(path);
    for (ULONG i = 0; dll[i]; i++)
        path[at++] = (WCHAR)(UCHAR)dll[i];
    path[at] = 0;
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    *status = load_image(&name, depth + 1, &m);
    if (*status == STATUS_IMAGE_ALREADY_LOADED)
        *status = STATUS_SUCCESS;
    if (!NT_SUCCESS(*status)) {
        KiLogPrintf("driver import from %s: %08x", dll, *status);
        return nullptr;
    }
    /* an export driver gets its DllInitialize with its registry path, as windows calls it. */
    NTSTATUS(NTAPI * initialize)(PUNICODE_STRING) =
        (NTSTATUS(NTAPI *)(PUNICODE_STRING))find_export_in(m, "DllInitialize", 0, FALSE, 0);
    if (initialize) {
        UNICODE_STRING registry = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Services");
        initialize(&registry);
    }
    return m;
}

static NTSTATUS load_image(PCUNICODE_STRING path, ULONG depth, PKLDR_DATA_TABLE_ENTRY *out)
{
    *out = nullptr;
    /* an image is loaded once, whatever path names it. */
    const WCHAR *leaf = path->Buffer + path->Length / sizeof(WCHAR);
    while (leaf > path->Buffer && leaf[-1] != L'\\')
        leaf--;
    UNICODE_STRING leaf_name = {(USHORT)(path->Length - (USHORT)((const UCHAR *)leaf - (const UCHAR *)path->Buffer)), 0,
                                (PWSTR)leaf};
    leaf_name.MaximumLength = leaf_name.Length;
    for (PLIST_ENTRY l = PsLoadedModuleList.Flink; l != &PsLoadedModuleList; l = l->Flink) {
        PKLDR_DATA_TABLE_ENTRY m = CONTAINING_RECORD(l, KLDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (RtlEqualUnicodeString(&m->BaseDllName, &leaf_name, TRUE)) {
            *out = m;
            return STATUS_IMAGE_ALREADY_LOADED;
        }
    }
    PUCHAR file;
    ULONG file_size;
    NTSTATUS status = read_whole_file(path, &file, &file_size);
    if (!NT_SUCCESS(status))
        return status;
    MI_IMAGE_INFO info;
    status = MiParseImageHeaders(file, file_size, &info);
    if (NT_SUCCESS(status) && info.Machine != IMAGE_FILE_MACHINE_AMD64)
        status = STATUS_INVALID_IMAGE_FORMAT;
    if (!NT_SUCCESS(status)) {
        ExFreePool(file);
        return status;
    }
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)(file + ((PIMAGE_DOS_HEADER)file)->e_lfanew);
    SIZE_T pages = BYTES_TO_PAGES(info.SizeOfImage);
    PUCHAR base = MmAllocateSystemVa(pages);
    if (!base) {
        ExFreePool(file);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (SIZE_T i = 0; i < pages; i++) {
        ULONG64 pa = MmAllocatePage();
        if (!pa) {
            free_image_pages(base, i);
            ExFreePool(file);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        MiMapPage(MiKernelRoot(), (ULONG_PTR)base + i * PAGE_SIZE, pa, MM_KERNEL_RW);
        memset(base + i * PAGE_SIZE, 0, PAGE_SIZE);
    }
    memcpy(base, file, info.SizeOfHeaders < file_size ? info.SizeOfHeaders : file_size);
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    for (USHORT i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        ULONG vsize = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        ULONG raw = sec[i].SizeOfRawData < vsize ? sec[i].SizeOfRawData : vsize;
        if ((ULONG64)sec[i].VirtualAddress + vsize > info.SizeOfImage || (ULONG64)sec[i].PointerToRawData + raw > file_size) {
            status = STATUS_INVALID_IMAGE_FORMAT;
            break;
        }
        memcpy(base + sec[i].VirtualAddress, file + sec[i].PointerToRawData, raw);
    }

    LONG64 delta = (LONG64)((ULONG_PTR)base - info.ImageBase);
    IMAGE_DATA_DIRECTORY rel = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for (ULONG off = 0; NT_SUCCESS(status) && delta && off + 8 <= rel.Size;) {
        PIMAGE_BASE_RELOCATION block = (PIMAGE_BASE_RELOCATION)(base + rel.VirtualAddress + off);
        if (block->SizeOfBlock < 8)
            break;
        PUSHORT entries = (PUSHORT)(block + 1);
        for (ULONG i = 0; i < (block->SizeOfBlock - 8) / 2; i++) {
            if ((entries[i] >> 12) == IMAGE_REL_BASED_DIR64)
                *(PULONG64)(base + block->VirtualAddress + (entries[i] & 0xFFF)) += (ULONG64)delta;
        }
        off += block->SizeOfBlock;
    }
    /* the image's own optional header tells its new base, as the windows loader leaves it. */
    if (NT_SUCCESS(status))
        image_headers(base)->OptionalHeader.ImageBase = (ULONG_PTR)base;

    IMAGE_DATA_DIRECTORY imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (PIMAGE_IMPORT_DESCRIPTOR d = imp.VirtualAddress ? (PIMAGE_IMPORT_DESCRIPTOR)(base + imp.VirtualAddress) : nullptr;
         NT_SUCCESS(status) && d && d->Name; d++) {
        const char *dll = (const char *)base + d->Name;
        PKLDR_DATA_TABLE_ENTRY from = import_module(dll, depth, &status);
        if (!from)
            break;
        PIMAGE_THUNK_DATA64 names = (PIMAGE_THUNK_DATA64)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        PIMAGE_THUNK_DATA64 iat = (PIMAGE_THUNK_DATA64)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            PVOID target;
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                target = find_export_in(from, nullptr, (USHORT)IMAGE_ORDINAL64(names->u1.Ordinal), TRUE, 0);
                if (!target)
                    KiLogPrintf("unresolved driver import %s!#%u", dll, (ULONG)IMAGE_ORDINAL64(names->u1.Ordinal));
            } else {
                PIMAGE_IMPORT_BY_NAME by_name = (PIMAGE_IMPORT_BY_NAME)(base + names->u1.AddressOfData);
                target = find_export_in(from, by_name->Name, 0, FALSE, 0);
                if (!target)
                    KiLogPrintf("unresolved driver import %s!%s", dll, by_name->Name);
            }
            if (!target) {
                status = STATUS_ENTRYPOINT_NOT_FOUND;
                break;
            }
            iat->u1.Function = (ULONG64)target;
        }
    }

    if (NT_SUCCESS(status)) {
        for (SIZE_T i = 0; i < BYTES_TO_PAGES(info.SizeOfHeaders); i++) {
            ULONG64 pte = MiReadPte(MiKernelRoot(), (ULONG_PTR)base + i * PAGE_SIZE);
            MiMapPage(MiKernelRoot(), (ULONG_PTR)base + i * PAGE_SIZE, pte & MM_PTE_ADDR_MASK, MM_KERNEL_RO);
        }
        for (USHORT i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            ULONG vsize = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
            ULONG64 attrs = (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) ? MM_KERNEL_RX
                          : (sec[i].Characteristics & IMAGE_SCN_MEM_WRITE) ? MM_KERNEL_RW
                                                                            : MM_KERNEL_RO;
            ULONG first = sec[i].VirtualAddress >> PAGE_SHIFT;
            ULONG last = (ULONG)((sec[i].VirtualAddress + vsize + PAGE_SIZE - 1) >> PAGE_SHIFT);
            for (ULONG pg = first; pg < last; pg++) {
                ULONG_PTR va = (ULONG_PTR)base + (ULONG_PTR)pg * PAGE_SIZE;
                ULONG64 pte = MiReadPte(MiKernelRoot(), va);
                MiMapPage(MiKernelRoot(), va, pte & MM_PTE_ADDR_MASK, attrs);
            }
        }
        KiFlushInstructionCache();
        *out = add_module(base, info.SizeOfImage, base + info.EntryPoint, path);
        if (!*out)
            status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (NT_SUCCESS(status))
        KiLogPrintf("loaded %wZ at %p", path, base);
    else
        free_image_pages(base, pages);
    ExFreePool(file);
    return status;
}

NTSTATUS MmLoadSystemImage(PCUNICODE_STRING path, PKLDR_DATA_TABLE_ENTRY *out)
{
    return load_image(path, 0, out);
}

/* the image's pages go, the record leaves the list and stays for readers that may still hold it. */
void MmUnloadSystemImage(PKLDR_DATA_TABLE_ENTRY m)
{
    if (!m || m == g_kernel_module)
        return;
    ExAcquireFastMutex(&g_modules_lock);
    RemoveEntryList(&m->InLoadOrderLinks);
    InitializeListHead(&m->InLoadOrderLinks);
    ExReleaseFastMutex(&g_modules_lock);
    KiLogPrintf("unloaded %wZ from %p", &m->FullDllName, m->DllBase);
    free_image_pages(m->DllBase, BYTES_TO_PAGES(m->SizeOfImage));
    m->DllBase = nullptr;
    m->SizeOfImage = 0;
}

/* reads an export of an image section from its prototype pages through the physmap. */
static PVOID section_export(PSECTION s, const char *name)
{
#define AT(rva) ((PUCHAR)HalPhysmap(MiSectionPage(s, (rva) >> PAGE_SHIFT)) + ((rva) & (PAGE_SIZE - 1)))
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)AT(0);
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)AT((ULONG)dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return nullptr;
    IMAGE_EXPORT_DIRECTORY exp;
    memcpy(&exp, AT(dir.VirtualAddress), sizeof(exp));
    for (ULONG i = 0; i < exp.NumberOfNames; i++) {
        ULONG name_rva;
        memcpy(&name_rva, AT(exp.AddressOfNames + i * 4), 4);
        /* names do not cross pages in practice; compare byte by byte to be safe. */
        ULONG k = 0;
        for (;; k++) {
            char c = (char)*AT(name_rva + k);
            if (c != name[k])
                break;
            if (!c)
                break;
        }
        if (name[k] == 0 && *AT(name_rva + k) == 0) {
            USHORT ordinal;
            ULONG func;
            memcpy(&ordinal, AT(exp.AddressOfNameOrdinals + i * 2), 2);
            memcpy(&func, AT(exp.AddressOfFunctions + ordinal * 4), 4);
            return (PVOID)(s->Image.ImageBase + func);
        }
    }
    return nullptr;
#undef AT
}

PSP_SYSTEM_DLL PspNativeSystemDll;
PSP_SYSTEM_DLL PspX86SystemDll;
/* the 32-bit ntdll of wow64 programs, mapped by the kernel at their birth so that its stubs can be patched before they run. */
PSECTION PspWow64NtdllSection;
/* the x64 ntdll of x64 programs on arm64, mapped at their birth for the same reason. */
PSECTION PspX64NtdllSection;

static PSECTION guest_ntdll_section(PCWSTR name)
{
    UNICODE_STRING path;
    RtlInitUnicodeString(&path, name);
    PFILE_OBJECT file;
    PSECTION section = nullptr;
    if (NT_SUCCESS(IopOpenFileObject(&path, FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE, FILE_NON_DIRECTORY_FILE, &file))) {
        if (!NT_SUCCESS(MiCreateImageSection(file, &section)))
            section = nullptr;
        ObDereferenceObject(file);
    }
    return section;
}

/* creates a system dll's image section once; every process maps it at its preferred base. */
static NTSTATUS load_system_dll(PCWSTR path_string, PPSP_SYSTEM_DLL dll)
{
    UNICODE_STRING path;
    RtlInitUnicodeString(&path, path_string);
    PFILE_OBJECT file;
    NTSTATUS status = IopOpenFileObject(&path, FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE, FILE_NON_DIRECTORY_FILE, &file);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("cannot open %wZ: %08x", &path, status);
        return status;
    }
    status = MiCreateImageSection(file, &dll->Section);
    ObDereferenceObject(file);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("invalid %wZ: %08x", &path, status);
        return status;
    }
    dll->Base = (PVOID)dll->Section->Image.ImageBase;
    dll->LdrInitializeThunk = section_export(dll->Section, "LdrInitializeThunk");
    dll->RtlUserThreadStart = section_export(dll->Section, "RtlUserThreadStart");
    dll->KiUserExceptionDispatcher = section_export(dll->Section, "KiUserExceptionDispatcher");
    dll->KiUserApcDispatcher = section_export(dll->Section, "KiUserApcDispatcher");
    dll->KiUserCallbackDispatcher = section_export(dll->Section, "KiUserCallbackDispatcher");
    if (!dll->LdrInitializeThunk || !dll->RtlUserThreadStart || !dll->KiUserCallbackDispatcher)
        return STATUS_ENTRYPOINT_NOT_FOUND;
    return STATUS_SUCCESS;
}

NTSTATUS PspInitializeSystemDll(void)
{
    NTSTATUS status = load_system_dll(L"\\SystemRoot\\System32\\ntdll.dll", &PspNativeSystemDll);
    if (!NT_SUCCESS(status))
        return status;
    /* x86 and x64 programs run on arm64 only when the translator is present; x64 runs x86 through wow64. */
    if (MIME_NATIVE_MACHINE != IMAGE_FILE_MACHINE_AMD64 &&
        !NT_SUCCESS(load_system_dll(L"\\SystemRoot\\System32\\xta64.dll", &PspX86SystemDll)))
        memset(&PspX86SystemDll, 0, sizeof(PspX86SystemDll));
    if (MIME_NATIVE_MACHINE == IMAGE_FILE_MACHINE_AMD64 || PspX86SystemDll.Section)
        PspWow64NtdllSection = guest_ntdll_section(L"\\SystemRoot\\SysWOW64\\ntdll.dll");
    if (MIME_NATIVE_MACHINE != IMAGE_FILE_MACHINE_AMD64 && PspX86SystemDll.Section)
        PspX64NtdllSection = guest_ntdll_section(L"\\SystemRoot\\SysX64\\ntdll.dll");
    return STATUS_SUCCESS;
}

/* the system dll this machine's programs run on, or NULL. */
PPSP_SYSTEM_DLL PspSystemDllForMachine(USHORT machine)
{
    if (machine == MIME_NATIVE_MACHINE)
        return &PspNativeSystemDll;
    /* wow64: the 64-bit ntdll loads wow64.dll, which runs the 32-bit program. */
    if (MIME_NATIVE_MACHINE == IMAGE_FILE_MACHINE_AMD64 && machine == IMAGE_FILE_MACHINE_I386)
        return &PspNativeSystemDll;
    switch (machine) {
    case IMAGE_FILE_MACHINE_I386:
    case IMAGE_FILE_MACHINE_AMD64:
        return PspX86SystemDll.Section ? &PspX86SystemDll : nullptr;
    default:
        return nullptr;
    }
}
