/*
 * kernel mode image loader for drivers, and the system dlls (ntdll.dll,
 * and mimex86.dll for x86 and x64 programs) mapped into every process.
 */
#include "../ntos.h"

typedef struct LOADED_MODULE {
    struct LOADED_MODULE *Next;
    PVOID Base;
    SIZE_T Size;
    WCHAR Name[64];
} LOADED_MODULE;

static LOADED_MODULE *g_modules;

static PVOID find_export(PVOID base, const char *name, USHORT ordinal_hint, BOOLEAN by_ordinal)
{
    PIMAGE_DOS_HEADER dos = base;
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((PUCHAR)base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return NULL;
    PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)base + dir.VirtualAddress);
    PULONG functions = (PULONG)((PUCHAR)base + exp->AddressOfFunctions);
    if (by_ordinal) {
        ULONG index = ordinal_hint - exp->Base;
        return index < exp->NumberOfFunctions && functions[index] ? (PUCHAR)base + functions[index] : NULL;
    }
    PULONG names = (PULONG)((PUCHAR)base + exp->AddressOfNames);
    PUSHORT ordinals = (PUSHORT)((PUCHAR)base + exp->AddressOfNameOrdinals);
    LONG lo = 0, hi = (LONG)exp->NumberOfNames - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        int c = strcmp(name, (const char *)base + names[mid]);
        if (c == 0)
            return (PUCHAR)base + functions[ordinals[mid]];
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return NULL;
}

PVOID MiFindKernelExport(PVOID image_base, const char *name)
{
    return find_export(image_base, name, 0, FALSE);
}

PVOID NTAPI MmGetSystemRoutineAddress(PUNICODE_STRING name)
{
    char ansi[128];
    ULONG n = name->Length / sizeof(WCHAR);
    if (n >= sizeof(ansi))
        return NULL;
    for (ULONG i = 0; i < n; i++)
        ansi[i] = (char)name->Buffer[i];
    ansi[n] = 0;
    return find_export((PVOID)HalBootInfo->kernel_virt, ansi, 0, FALSE);
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

NTSTATUS MmLoadSystemImage(PCUNICODE_STRING path, PVOID *base_out, SIZE_T *size_out, PVOID *entry_out)
{
    PUCHAR file;
    ULONG file_size;
    NTSTATUS status = read_whole_file(path, &file, &file_size);
    if (!NT_SUCCESS(status))
        return status;
    MI_IMAGE_INFO info;
    status = MiParseImageHeaders(file, file_size, &info);
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
            ExFreePool(file);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        MiMapPage(MiKernelRoot(), (ULONG_PTR)base + i * PAGE_SIZE, pa, MM_KERNEL_RW);
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

    /* relocate. */
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

    /* resolve imports against the kernel. */
    IMAGE_DATA_DIRECTORY imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    PVOID kernel = (PVOID)HalBootInfo->kernel_virt;
    for (PIMAGE_IMPORT_DESCRIPTOR d = imp.VirtualAddress ? (PIMAGE_IMPORT_DESCRIPTOR)(base + imp.VirtualAddress) : NULL;
         NT_SUCCESS(status) && d && d->Name; d++) {
        const char *dll = (const char *)base + d->Name;
        if (_stricmp(dll, "ntoskrnl.exe") != 0 && _stricmp(dll, "hal.dll") != 0) {
            KiLogPrintf("driver import from unsupported module %s", dll);
            status = STATUS_DLL_NOT_FOUND;
            break;
        }
        PIMAGE_THUNK_DATA64 names = (PIMAGE_THUNK_DATA64)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        PIMAGE_THUNK_DATA64 iat = (PIMAGE_THUNK_DATA64)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            PVOID target;
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                target = find_export(kernel, NULL, (USHORT)IMAGE_ORDINAL64(names->u1.Ordinal), TRUE);
            } else {
                PIMAGE_IMPORT_BY_NAME by_name = (PIMAGE_IMPORT_BY_NAME)(base + names->u1.AddressOfData);
                target = find_export(kernel, by_name->Name, 0, FALSE);
                if (!target)
                    KiLogPrintf("unresolved kernel import %s!%s", dll, by_name->Name);
            }
            if (!target) {
                status = STATUS_ENTRYPOINT_NOT_FOUND;
                break;
            }
            iat->u1.Function = (ULONG64)target;
        }
    }

    if (NT_SUCCESS(status)) {
        /* apply final page protections. */
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
        LOADED_MODULE *m = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*m), KI_TAG('M', 'm', 'L', 'm'));
        if (m) {
            m->Base = base;
            m->Size = info.SizeOfImage;
            const WCHAR *slash = path->Buffer + path->Length / sizeof(WCHAR);
            while (slash > path->Buffer && slash[-1] != '\\')
                slash--;
            ULONG n = (ULONG)(path->Buffer + path->Length / sizeof(WCHAR) - slash);
            if (n >= RTL_NUMBER_OF(m->Name))
                n = RTL_NUMBER_OF(m->Name) - 1;
            memcpy(m->Name, slash, n * sizeof(WCHAR));
            m->Name[n] = 0;
            m->Next = g_modules;
            g_modules = m;
        }
        KiLogPrintf("loaded %wZ at %p", path, base);
        *base_out = base;
        *size_out = info.SizeOfImage;
        *entry_out = base + info.EntryPoint;
    }
    ExFreePool(file);
    return status;
}

/* reads an export of an image section from its prototype pages through the physmap. */
static PVOID section_export(PSECTION s, const char *name)
{
#define AT(rva) ((PUCHAR)HalPhysmap(MiSectionPage(s, (rva) >> PAGE_SHIFT)) + ((rva) & (PAGE_SIZE - 1)))
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)AT(0);
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)AT((ULONG)dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return NULL;
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
    return NULL;
#undef AT
}

PSP_SYSTEM_DLL PspNativeSystemDll;
PSP_SYSTEM_DLL PspX86SystemDll;

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
    /* x86 and x64 programs run only when the translator is present. */
    if (!NT_SUCCESS(load_system_dll(L"\\SystemRoot\\System32\\mimex86.dll", &PspX86SystemDll)))
        memset(&PspX86SystemDll, 0, sizeof(PspX86SystemDll));
    return STATUS_SUCCESS;
}

/* the system dll a program of this machine runs on, NULL when there is none. */
PPSP_SYSTEM_DLL PspSystemDllForMachine(USHORT machine)
{
    if (machine == MIME_NATIVE_MACHINE)
        return &PspNativeSystemDll;
    switch (machine) {
    case IMAGE_FILE_MACHINE_I386:
    case IMAGE_FILE_MACHINE_AMD64:
        return PspX86SystemDll.Section ? &PspX86SystemDll : NULL;
    default:
        return NULL;
    }
}
