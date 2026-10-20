/*
 * the image loader: process and thread initialization, dll mapping,
 * import binding, thread local storage and dll notifications.
 */
#include "sxs.h"

#define LDRP_DONT_CALL_FOR_THREADS 0x00040000
#define LDRP_LOAD_IN_PROGRESS 0x00001000
#define LDRP_STATIC_LINK 0x00000002
#define LDRP_COR_ILONLY 0x01000000

typedef struct LDR_COR_HEADER {
    ULONG Size;
    USHORT MajorVersion, MinorVersion;
    IMAGE_DATA_DIRECTORY Metadata;
    ULONG Flags, EntryPoint;
    IMAGE_DATA_DIRECTORY Resources, StrongName, CodeManager, VTableFixups, ExportJumps, NativeHeader;
} LDR_COR_HEADER;

#define COR_ILONLY 1u
#define COR_32BITREQUIRED 2u
#define COR_NATIVE_ENTRYPOINT 16u
#define COR_DIRECTORY 14

extern IMAGE_DOS_HEADER __ImageBase;

PVOID LdrpNtdllBase;
RTL_CRITICAL_SECTION LdrpLoaderLock = RTL_CRITICAL_SECTION_INIT;
PVOID LdrpKernel32ThreadInitThunk;

static PEB_LDR_DATA g_ldr;
static BOOLEAN g_process_initialized;

/*
 * a 32-bit program on x64 runs on this ntdll and wow64.dll, as on windows:
 * the 64-bit side loads wow64.dll and nothing of the program, and every
 * thread then runs Wow64LdrpInitialize, which never returns.
 */
static void(NTAPI *g_wow64_initialize)(PCONTEXT context);
void(NTAPI *LdrpWow64PrepareForException)(PEXCEPTION_RECORD record, PCONTEXT context);
static BOOLEAN g_process_detaching;
static RTL_CRITICAL_SECTION g_peb_lock = RTL_CRITICAL_SECTION_INIT;

#ifdef __i386__
static constexpr WCHAR g_system32[] = L"C:\\Windows\\SysWOW64\\";
#define LDRP_MACHINE IMAGE_FILE_MACHINE_I386
#elif defined(__x86_64__) && !defined(MIME_X64_NATIVE)
/* the x64 system dlls, for programs run by the translator. */
static constexpr WCHAR g_system32[] = L"C:\\Windows\\SysX64\\";
#define LDRP_MACHINE IMAGE_FILE_MACHINE_AMD64
#elifdef __x86_64__
static constexpr WCHAR g_system32[] = L"C:\\Windows\\System32\\";
#define LDRP_MACHINE IMAGE_FILE_MACHINE_AMD64
#else
static constexpr WCHAR g_system32[] = L"C:\\Windows\\System32\\";
#define LDRP_MACHINE IMAGE_FILE_MACHINE_ARM64
#endif
static constexpr WCHAR g_windows[] = L"C:\\Windows\\";

/* modules with static thread local storage, indexed by their tls slot. */
typedef struct TLS_MODULE {
    PLDR_DATA_TABLE_ENTRY Entry;
    IMAGE_TLS_DIRECTORY *Directory;
} TLS_MODULE;
static TLS_MODULE g_tls_modules[64];
static ULONG g_tls_count;

/* a thread's tls slots, listed so a dll loaded later reaches every thread. */
typedef struct TLS_VECTOR {
    LIST_ENTRY Link;
    PVOID Slots[RTL_NUMBER_OF(g_tls_modules)];
} TLS_VECTOR;
static LIST_ENTRY g_tls_vectors = {&g_tls_vectors, &g_tls_vectors};

typedef struct LOAD_CONTEXT {
    BOOLEAN Static; /* part of the initial process image */
    PCWSTR SearchPath;
} LOAD_CONTEXT;

static NTSTATUS load_dll(PCWSTR name, LOAD_CONTEXT *ctx, PLDR_DATA_TABLE_ENTRY *out);

/* ------------------------------------------------------------------ image helpers */

PIMAGE_NT_HEADERS LdrpNtHeaders(PVOID base)
{
    IMAGE_DOS_HEADER *dos = base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PUCHAR)base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

PVOID LdrpImageDirectory(PVOID base, USHORT index, PULONG size)
{
    PIMAGE_NT_HEADERS nt = LdrpNtHeaders(base);
    if (!nt || index >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
        return nullptr;
    IMAGE_OPTIONAL_HEADER32 *opt32 = (void *)&nt->OptionalHeader;
    IMAGE_OPTIONAL_HEADER64 *opt64 = (void *)&nt->OptionalHeader;
    BOOLEAN pe32 = opt32->Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    ULONG count = pe32 ? opt32->NumberOfRvaAndSizes : opt64->NumberOfRvaAndSizes;
    if (index >= count)
        return nullptr;
    IMAGE_DATA_DIRECTORY *dir = pe32 ? &opt32->DataDirectory[index] : &opt64->DataDirectory[index];
    if (!dir->VirtualAddress || !dir->Size || dir->VirtualAddress >= opt32->SizeOfImage ||
        dir->Size > opt32->SizeOfImage - dir->VirtualAddress)
        return nullptr;
    if (size)
        *size = dir->Size;
    return (PUCHAR)base + dir->VirtualAddress;
}

static BOOLEAN is_il_only(PVOID base)
{
    ULONG size = 0;
    const LDR_COR_HEADER *cor = LdrpImageDirectory(base, COR_DIRECTORY, &size);
    if (!cor || size < sizeof(*cor) || cor->Size < sizeof(*cor) || cor->Size > size ||
        (cor->Flags & (COR_ILONLY | COR_NATIVE_ENTRYPOINT)) != COR_ILONLY)
        return FALSE;
    PIMAGE_NT_HEADERS nt = LdrpNtHeaders(base);
    ULONG image_size = nt->OptionalHeader.SizeOfImage;
    return cor->Metadata.VirtualAddress && cor->Metadata.Size >= 16 && cor->Metadata.VirtualAddress < image_size &&
        cor->Metadata.Size <= image_size - cor->Metadata.VirtualAddress;
}

static BOOLEAN is_portable_il(PVOID base)
{
    PIMAGE_NT_HEADERS nt = LdrpNtHeaders(base);
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || !is_il_only(base))
        return FALSE;
    const LDR_COR_HEADER *cor = LdrpImageDirectory(base, COR_DIRECTORY, nullptr);
    return !(cor->Flags & COR_32BITREQUIRED) && !cor->NativeHeader.VirtualAddress && !cor->NativeHeader.Size;
}

static PVOID heap_alloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size);
}

static void heap_free(PVOID p)
{
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

static PWSTR heap_wcsdup(PCWSTR s, SIZE_T chars)
{
    PWSTR copy = heap_alloc((chars + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, s, chars * sizeof(WCHAR));
        copy[chars] = 0;
    }
    return copy;
}

/* ------------------------------------------------------------------ module list */

static PLDR_DATA_TABLE_ENTRY create_entry(PVOID base, PCWSTR full_path)
{
    PLDR_DATA_TABLE_ENTRY e = heap_alloc(sizeof(*e));
    PIMAGE_NT_HEADERS nt = LdrpNtHeaders(base);
    if (!e || !nt)
        return nullptr;
    SIZE_T chars = wcslen(full_path);
    PWSTR copy = heap_wcsdup(full_path, chars);
    if (!copy) {
        heap_free(e);
        return nullptr;
    }
    PCWSTR file = copy + chars;
    while (file > copy && file[-1] != '\\' && file[-1] != '/')
        file--;
    e->DllBase = base;
    e->SizeOfImage = nt->OptionalHeader.SizeOfImage;
    e->EntryPoint = nt->OptionalHeader.AddressOfEntryPoint ? (PUCHAR)base + nt->OptionalHeader.AddressOfEntryPoint : nullptr;
    e->FullDllName.Buffer = copy;
    e->FullDllName.Length = (USHORT)(chars * sizeof(WCHAR));
    e->FullDllName.MaximumLength = (USHORT)((chars + 1) * sizeof(WCHAR));
    e->BaseDllName.Buffer = (PWSTR)file;
    e->BaseDllName.Length = (USHORT)((copy + chars - file) * sizeof(WCHAR));
    e->BaseDllName.MaximumLength = (USHORT)(e->BaseDllName.Length + sizeof(WCHAR));
    e->TimeDateStamp = nt->FileHeader.TimeDateStamp;
    e->OriginalBase = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ?
        ((IMAGE_OPTIONAL_HEADER32 *)&nt->OptionalHeader)->ImageBase :
        ((IMAGE_OPTIONAL_HEADER64 *)&nt->OptionalHeader)->ImageBase;
    e->ObsoleteLoadCount = 1;
    e->ReferenceCount = 1;
    if (nt->FileHeader.Characteristics & IMAGE_FILE_DLL)
        e->Flags |= LDRP_IMAGE_DLL;
    if (is_il_only(base)) {
        e->Flags |= LDRP_COR_ILONLY;
        e->EntryPoint = nullptr;
    }
    InitializeListHead(&e->HashLinks);
    InitializeListHead(&e->InInitializationOrderLinks);
    InsertTailList(&g_ldr.InLoadOrderModuleList, &e->InLoadOrderLinks);
    InsertTailList(&g_ldr.InMemoryOrderModuleList, &e->InMemoryOrderLinks);
    return e;
}

static void destroy_entry(PLDR_DATA_TABLE_ENTRY e)
{
    RtlReleaseActivationContext(e->EntryPointActivationContext);
    RemoveEntryList(&e->InLoadOrderLinks);
    RemoveEntryList(&e->InMemoryOrderLinks);
    if (!IsListEmpty(&e->InInitializationOrderLinks))
        RemoveEntryList(&e->InInitializationOrderLinks);
    heap_free(e->FullDllName.Buffer);
    heap_free(e);
}

/* compares base names, ignoring case and an omitted ".dll" extension. */
static BOOLEAN name_matches(PCUNICODE_STRING base_name, PCWSTR name)
{
    SIZE_T n = wcslen(name);
    SIZE_T m = base_name->Length / sizeof(WCHAR);
    if (m == n)
        return _wcsnicmp(base_name->Buffer, name, n) == 0;
    if (m == n + 4 && !wcschr(name, '.'))
        return _wcsnicmp(base_name->Buffer, name, n) == 0 && _wcsicmp(base_name->Buffer + n, L".dll") == 0;
    return FALSE;
}

static PLDR_DATA_TABLE_ENTRY find_by_name(PCWSTR name)
{
    for (PLIST_ENTRY l = g_ldr.InLoadOrderModuleList.Flink; l != &g_ldr.InLoadOrderModuleList; l = l->Flink) {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (name_matches(&e->BaseDllName, name))
            return e;
    }
    return nullptr;
}

static PLDR_DATA_TABLE_ENTRY find_by_path(PCWSTR path)
{
    SIZE_T chars = wcslen(path);
    for (PLIST_ENTRY l = g_ldr.InLoadOrderModuleList.Flink; l != &g_ldr.InLoadOrderModuleList; l = l->Flink) {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (e->FullDllName.Length == chars * sizeof(WCHAR) && !_wcsnicmp(e->FullDllName.Buffer, path, chars))
            return e;
    }
    return nullptr;
}

static PLDR_DATA_TABLE_ENTRY find_by_base(PVOID base)
{
    for (PLIST_ENTRY l = g_ldr.InLoadOrderModuleList.Flink; l != &g_ldr.InLoadOrderModuleList; l = l->Flink) {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (e->DllBase == base)
            return e;
    }
    return nullptr;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrFindEntryForAddress(PVOID address, PLDR_DATA_TABLE_ENTRY *entry)
{
    for (PLIST_ENTRY l = g_ldr.InLoadOrderModuleList.Flink; l != &g_ldr.InLoadOrderModuleList; l = l->Flink) {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if ((PUCHAR)address >= (PUCHAR)e->DllBase && (PUCHAR)address < (PUCHAR)e->DllBase + e->SizeOfImage) {
            *entry = e;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MORE_ENTRIES;
}

/* the image view holding an address, as the memory manager knows it; NULL for a partial or data view. */
static PVOID image_base_from_memory(PVOID pc)
{
    MEMORY_IMAGE_INFORMATION mi;
    if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), pc, MemoryImageInformation, &mi, sizeof(mi), nullptr)))
        return nullptr;
    if (!mi.ImageBase || (mi.ImageFlags & (MEMORY_IMAGE_PARTIAL_MAP | MEMORY_IMAGE_NOT_EXECUTABLE)) ||
        (ULONG_PTR)pc < (ULONG_PTR)mi.ImageBase)
        return nullptr;
    return mi.ImageBase;
}

/*
 * the base of the image an address lies in, or NULL, also stored in *base,
 * which may not be NULL. the loaded modules answer, not the code tables
 * added at run time; until the process is initialized the memory manager
 * does, as windows asks it before its loader is ready.
 */
NTDLL_EXPORT PVOID NTAPI RtlPcToFileHeader(PVOID pc, PVOID *base)
{
    PLDR_DATA_TABLE_ENTRY entry;
    PVOID found = nullptr;
    if (!g_process_initialized)
        found = image_base_from_memory(pc);
    else if (NT_SUCCESS(LdrFindEntryForAddress(pc, &entry)))
        found = entry->DllBase;
    *base = found;
    return found;
}

/* ------------------------------------------------------------------ api sets */

/*
 * api set contracts resolve to the dll that hosts them. the exact table is windows' own schema, keyed by
 * the name up to the last hyphen so that every version matches, and keeps the contracts whose host Mime has.
 * the prefix rules below it catch the rest of a family and name the dll that implements it here.
 */
typedef struct API_SET_RULE {
    PCWSTR Prefix;
    PCWSTR Host;
    ULONG Version;
} API_SET_RULE;

static const API_SET_RULE g_api_set_hosts[] = {
#include "apiset.h"
};

static const API_SET_RULE g_api_sets[] = {
    {L"api-ms-win-crt-", L"ucrtbase.dll", 0},
    {L"api-ms-win-core-winrt-", L"combase.dll", 0},
    {L"api-ms-win-core-synch-", L"kernelbase.dll", 0},
    {L"api-ms-win-core-rtlsupport-", L"ntdll.dll", 0},
    {L"api-ms-win-core-", L"kernelbase.dll", 0},
    {L"api-ms-win-eventing-consumer-", L"sechost.dll", 0},
    {L"api-ms-win-eventing-controller-", L"sechost.dll", 0},
    {L"api-ms-win-eventing-obsolete-", L"sechost.dll", 0},
    {L"api-ms-win-eventing-", L"kernelbase.dll", 0},
    {L"api-ms-win-security-base-", L"kernelbase.dll", 0},
    {L"api-ms-win-security-lsalookup-l1-", L"sechost.dll", 0},
    {L"api-ms-win-security-lsapolicy-", L"sechost.dll", 0},
    {L"api-ms-win-security-credentials-", L"sechost.dll", 0},
    {L"api-ms-win-security-audit-", L"sechost.dll", 0},
    {L"api-ms-win-security-sddl-ansi-", L"advapi32.dll", 0},
    {L"api-ms-win-security-sddl-", L"sechost.dll", 0},
    {L"api-ms-win-security-sddlparsecond-", L"sechost.dll", 0},
    {L"api-ms-win-security-accesshlpr-", L"sechost.dll", 0},
    {L"api-ms-win-security-capability-", L"sechost.dll", 0},
    {L"api-ms-win-security-isolation", L"sechost.dll", 0},
    {L"api-ms-win-security-", L"advapi32.dll", 0},
    {L"api-ms-win-service-", L"sechost.dll", 0},
    {L"api-ms-win-shcore-", L"shcore.dll", 0},
    {L"api-ms-win-power-", L"powrprof.dll", 0},
    {L"api-ms-win-ntuser-", L"user32.dll", 0},
    {L"api-ms-win-rtcore-ntuser-", L"user32.dll", 0},
    {L"api-ms-win-gdi-", L"gdi32.dll", 0},
    {L"api-ms-win-shell-", L"shell32.dll", 0},
    {L"ext-ms-win-ntuser-", L"user32.dll", 0},
    {L"ext-ms-win-gdi-", L"gdi32.dll", 0},
    {L"ext-ms-win-", L"kernelbase.dll", 0},
};

/*
 * the schema's rule for a contract name, which ends in a hyphen and a version, perhaps followed by the extension
 * of the dll. The name's length without the extension is length, and version_at is where the version starts.
 */
static const API_SET_RULE *find_contract(PCWSTR name, SIZE_T length, SIZE_T *version_at)
{
    WCHAR key[96];
    SIZE_T cut = length;
    while (cut && name[cut - 1] >= '0' && name[cut - 1] <= '9')
        cut--;
    if (cut == length || cut < 2 || name[cut - 1] != '-' || cut - 1 >= RTL_NUMBER_OF(key))
        return nullptr;
    for (SIZE_T i = 0; i < cut - 1; i++)
        key[i] = name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i];
    key[cut - 1] = 0;
    if (version_at)
        *version_at = cut;
    SIZE_T low = 0, high = RTL_NUMBER_OF(g_api_set_hosts);
    while (low < high) {
        SIZE_T middle = (low + high) / 2;
        int order = wcscmp(key, g_api_set_hosts[middle].Prefix);
        if (!order)
            return &g_api_set_hosts[middle];
        if (order < 0)
            high = middle;
        else
            low = middle + 1;
    }
    return nullptr;
}

/* the host of a contract in the schema table, or null; a name is the contract, a hyphen and a version, and .dll. */
static PCWSTR find_api_set_host(PCWSTR name)
{
    SIZE_T length = wcslen(name);
    if (length > 4 && !_wcsicmp(name + length - 4, L".dll"))
        length -= 4;
    const API_SET_RULE *rule = find_contract(name, length, nullptr);
    return rule ? rule->Host : nullptr;
}

static PCWSTR resolve_api_set(PCWSTR name)
{
    if (_wcsnicmp(name, L"api-", 4) && _wcsnicmp(name, L"ext-", 4))
        return name;
    PCWSTR host = find_api_set_host(name);
    if (host)
        return host;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_api_sets); i++) {
        SIZE_T n = wcslen(g_api_sets[i].Prefix);
        if (!_wcsnicmp(name, g_api_sets[i].Prefix, n))
            return g_api_sets[i].Host;
    }
    return name;
}

/*
 * whether a contract has a host: any version of it will do, and the name may have an extension. A name that is
 * no contract is not present, and says so without an error.
 */
NTDLL_EXPORT NTSTATUS NTAPI ApiSetQueryApiSetPresence(PCUNICODE_STRING name, PBOOLEAN present)
{
    *present = FALSE;
    if (name->Length < 4 * sizeof(WCHAR) || (_wcsnicmp(name->Buffer, L"api-", 4) && _wcsnicmp(name->Buffer, L"ext-", 4)))
        return STATUS_SUCCESS;
    SIZE_T length = name->Length / sizeof(WCHAR);
    SIZE_T hyphen = length;
    while (hyphen > 1 && name->Buffer[hyphen - 1] != '-')
        hyphen--;
    if (hyphen <= 1)
        return STATUS_SUCCESS;
    WCHAR key[96];
    if (hyphen - 1 >= RTL_NUMBER_OF(key))
        return STATUS_SUCCESS;
    for (SIZE_T i = 0; i < hyphen - 1; i++)
        key[i] = name->Buffer[i] >= 'A' && name->Buffer[i] <= 'Z' ? name->Buffer[i] + 32 : name->Buffer[i];
    key[hyphen - 1] = 0;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_api_set_hosts); i++)
        if (!wcscmp(key, g_api_set_hosts[i].Prefix)) {
            *present = TRUE;
            break;
        }
    return STATUS_SUCCESS;
}

/*
 * STATUS_SUCCESS when a contract of at least the version asked is implemented, STATUS_NOT_FOUND when it is not.
 * The name has no extension; one that does not end in a number is an invalid parameter, as is a name that does
 * not start like a contract.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlIsApiSetImplemented(PCSTR name)
{
    SIZE_T length = strlen(name);
    if (length < 4 || length > 0xFFFF)
        return STATUS_INVALID_PARAMETER;
    WCHAR wide[96];
    if (length >= RTL_NUMBER_OF(wide))
        return STATUS_INVALID_PARAMETER;
    for (SIZE_T i = 0; i < length; i++)
        wide[i] = (UCHAR)name[i];
    wide[length] = 0;
    if (_wcsnicmp(wide, L"api-", 4) && _wcsnicmp(wide, L"ext-", 4))
        return STATUS_INVALID_PARAMETER;
    SIZE_T version_at;
    const API_SET_RULE *rule = find_contract(wide, length, &version_at);
    if (!rule) {
        /* a name that ends in no version is not a name at all, one with a version nobody has is absent. */
        return length && wide[length - 1] >= '0' && wide[length - 1] <= '9' ? STATUS_NOT_FOUND : STATUS_INVALID_PARAMETER;
    }
    ULONG version = 0;
    for (SIZE_T i = version_at; i < length; i++)
        version = version * 10 + (ULONG)(wide[i] - '0');
    return version > rule->Version ? STATUS_NOT_FOUND : STATUS_SUCCESS;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlDllShutdownInProgress(void)
{
    return g_process_detaching;
}

/* ------------------------------------------------------------------ mapping */

static NTSTATUS open_image(PCWSTR dos_path, PHANDLE file)
{
    UNICODE_STRING nt_path;
    if (!RtlDosPathNameToNtPathName_U(dos_path, &nt_path, nullptr, nullptr))
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt_path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtOpenFile(file, FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE, &oa, &iosb,
                                 FILE_SHARE_READ | FILE_SHARE_DELETE,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    RtlFreeUnicodeString(&nt_path);
    return status;
}

static NTSTATUS map_image(HANDLE file, PVOID *base)
{
    HANDLE section;
    NTSTATUS status = NtCreateSection(&section, SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY, nullptr, nullptr,
                                      PAGE_EXECUTE, SEC_IMAGE, file);
    if (!NT_SUCCESS(status))
        return status;
    SIZE_T size = 0;
    *base = nullptr;
    status = NtMapViewOfSection(section, NtCurrentProcess(), base, 0, 0, nullptr, &size, ViewShare, 0,
                                PAGE_EXECUTE_WRITECOPY);
    NtClose(section);
    if (status == STATUS_IMAGE_NOT_AT_BASE)
        status = STATUS_SUCCESS;
    /* portable il is consumed by the managed runtime, without executing its native bootstrap. */
    if (NT_SUCCESS(status) && LdrpNtHeaders(*base)->FileHeader.Machine != LDRP_MACHINE && !is_portable_il(*base)) {
        NtUnmapViewOfSection(NtCurrentProcess(), *base);
        status = STATUS_INVALID_IMAGE_FORMAT;
    }
    return status;
}

static BOOLEAN file_exists(PCWSTR dos_path)
{
    UNICODE_STRING nt_path;
    if (!RtlDosPathNameToNtPathName_U(dos_path, &nt_path, nullptr, nullptr))
        return FALSE;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt_path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    FILE_BASIC_INFORMATION info;
    NTSTATUS status = NtQueryAttributesFile(&oa, &info);
    RtlFreeUnicodeString(&nt_path);
    return NT_SUCCESS(status) && !(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOLEAN try_directory(PCWSTR dir, SIZE_T dir_chars, PCWSTR name, PWSTR out, SIZE_T out_chars)
{
    SIZE_T name_chars = wcslen(name);
    BOOLEAN slash = dir_chars && dir[dir_chars - 1] != '\\';
    if (dir_chars + slash + name_chars + 5 >= out_chars)
        return FALSE;
    memcpy(out, dir, dir_chars * sizeof(WCHAR));
    SIZE_T n = dir_chars;
    if (slash)
        out[n++] = '\\';
    memcpy(out + n, name, (name_chars + 1) * sizeof(WCHAR));
    if (file_exists(out))
        return TRUE;
    if (!wcschr(name, '.')) {
        wcscat(out, L".dll");
        return file_exists(out);
    }
    return FALSE;
}

#if defined(__i386__) || (defined(__x86_64__) && !defined(MIME_X64_NATIVE))
/* System32 holds the dlls of another processor, so a search through it means this processor's copies. */
static BOOLEAN is_native_system32(PCWSTR dir, SIZE_T chars)
{
    static const WCHAR system32[] = L"C:\\Windows\\System32";
    SIZE_T n = RTL_NUMBER_OF(system32) - 1;
    if (chars == n + 1 && dir[n] == '\\')
        chars = n;
    return chars == n && !_wcsnicmp(dir, system32, n);
}
#else
static BOOLEAN is_native_system32(PCWSTR dir, SIZE_T chars)
{
    return FALSE;
}
#endif

static BOOLEAN search_list(PCWSTR list, PCWSTR name, PWSTR out, SIZE_T out_chars)
{
    while (list && *list) {
        PCWSTR end = list;
        while (*end && *end != ';')
            end++;
        SIZE_T chars = (SIZE_T)(end - list);
        PCWSTR dir = list;
        if (is_native_system32(list, chars)) {
            dir = g_system32;
            chars = wcslen(g_system32);
        }
        if (chars && try_directory(dir, chars, name, out, out_chars))
            return TRUE;
        list = *end ? end + 1 : end;
    }
    return FALSE;
}

/* finds a dll the way windows does for desktop applications with safe search. */
static NTSTATUS find_dll(PCWSTR name, PCWSTR search_path, PWSTR out, SIZE_T out_chars)
{
    if (wcschr(name, '\\') || wcschr(name, '/') || (name[0] && name[1] == ':')) {
        ULONG n = RtlGetFullPathName_U(name, (ULONG)(out_chars * sizeof(WCHAR)), out, nullptr);
        if (!n || n >= out_chars * sizeof(WCHAR))
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
#if defined(__i386__) || (defined(__x86_64__) && !defined(MIME_X64_NATIVE))
        /* System32 holds arm64 dlls: paths into it, such as com servers, mean this processor's copies. */
        static const WCHAR arm64_system32[] = L"C:\\Windows\\System32\\";
        SIZE_T prefix = RTL_NUMBER_OF(arm64_system32) - 1;
        WCHAR redirected[MAX_PATH];
        if (!_wcsnicmp(out, arm64_system32, prefix) && out_chars <= RTL_NUMBER_OF(redirected) &&
            try_directory(g_system32, wcslen(g_system32), out + prefix, redirected, out_chars)) {
            wcscpy(out, redirected);
            return STATUS_SUCCESS;
        }
#endif
        return file_exists(out) ? STATUS_SUCCESS : STATUS_DLL_NOT_FOUND;
    }
    if (search_path && search_list(search_path, name, out, out_chars))
        return STATUS_SUCCESS;
    PRTL_USER_PROCESS_PARAMETERS pp = NtCurrentPeb()->ProcessParameters;
    if (!search_path && pp->DllPath.Length &&
        try_directory(pp->DllPath.Buffer, pp->DllPath.Length / sizeof(WCHAR), name, out, out_chars))
        return STATUS_SUCCESS;
    if (try_directory(g_system32, wcslen(g_system32), name, out, out_chars) ||
        try_directory(g_windows, wcslen(g_windows), name, out, out_chars))
        return STATUS_SUCCESS;
    UNICODE_STRING cwd = pp->CurrentDirectory.DosPath;
    if (try_directory(cwd.Buffer, cwd.Length / sizeof(WCHAR), name, out, out_chars))
        return STATUS_SUCCESS;
    WCHAR path_value[2048];
    UNICODE_STRING var = RTL_CONSTANT_STRING(L"PATH");
    UNICODE_STRING value = {0, sizeof(path_value) - sizeof(WCHAR), path_value};
    if (NT_SUCCESS(RtlQueryEnvironmentVariable_U(nullptr, &var, &value))) {
        path_value[value.Length / sizeof(WCHAR)] = 0;
        if (search_list(path_value, name, out, out_chars))
            return STATUS_SUCCESS;
    }
    return STATUS_DLL_NOT_FOUND;
}

/* ------------------------------------------------------------------ exports */

static NTSTATUS resolve_export(PLDR_DATA_TABLE_ENTRY module, PCSTR name, USHORT hint, ULONG ordinal, PVOID *address,
                               LOAD_CONTEXT *ctx, ULONG depth);

static NTSTATUS resolve_forwarder(PCSTR forward, PVOID *address, LOAD_CONTEXT *ctx, ULONG depth)
{
    PCSTR dot = strrchr(forward, '.');
    if (!dot || dot == forward || depth > 16)
        return STATUS_INVALID_IMAGE_FORMAT;
    WCHAR dll[MAX_PATH];
    SIZE_T n = (SIZE_T)(dot - forward);
    if (n + 5 >= RTL_NUMBER_OF(dll))
        return STATUS_NAME_TOO_LONG;
    for (SIZE_T i = 0; i < n; i++)
        dll[i] = (UCHAR)forward[i];
    dll[n] = 0;
    if (!wcschr(dll, '.') || (_wcsnicmp(dll, L"api-", 4) && _wcsnicmp(dll, L"ext-", 4)))
        wcscat(dll, L".dll");
    PLDR_DATA_TABLE_ENTRY target;
    NTSTATUS status = load_dll(dll, ctx, &target);
    if (!NT_SUCCESS(status))
        return status;
    if (dot[1] == '#') {
        ULONG ordinal = 0;
        for (PCSTR p = dot + 2; *p >= '0' && *p <= '9'; p++)
            ordinal = ordinal * 10 + (ULONG)(*p - '0');
        return resolve_export(target, nullptr, 0, ordinal, address, ctx, depth + 1);
    }
    return resolve_export(target, dot + 1, 0, 0, address, ctx, depth + 1);
}

static NTSTATUS resolve_export(PLDR_DATA_TABLE_ENTRY module, PCSTR name, USHORT hint, ULONG ordinal, PVOID *address,
                               LOAD_CONTEXT *ctx, ULONG depth)
{
    PUCHAR base = module->DllBase;
    ULONG dir_size;
    IMAGE_EXPORT_DIRECTORY *exp = LdrpImageDirectory(base, IMAGE_DIRECTORY_ENTRY_EXPORT, &dir_size);
    if (!exp)
        return name ? STATUS_ENTRYPOINT_NOT_FOUND : STATUS_ORDINAL_NOT_FOUND;
    PULONG functions = (PULONG)(base + exp->AddressOfFunctions);
    PULONG names = (PULONG)(base + exp->AddressOfNames);
    PUSHORT ordinals = (PUSHORT)(base + exp->AddressOfNameOrdinals);
    ULONG index;
    if (name) {
        if (hint < exp->NumberOfNames && !strcmp((PCSTR)(base + names[hint]), name)) {
            index = ordinals[hint];
        } else {
            LONG lo = 0, hi = (LONG)exp->NumberOfNames - 1;
            index = (ULONG)-1;
            while (lo <= hi) {
                LONG mid = (lo + hi) / 2;
                int c = strcmp((PCSTR)(base + names[mid]), name);
                if (!c) {
                    index = ordinals[mid];
                    break;
                }
                if (c < 0)
                    lo = mid + 1;
                else
                    hi = mid - 1;
            }
            if (index == (ULONG)-1)
                return STATUS_ENTRYPOINT_NOT_FOUND;
        }
    } else {
        index = ordinal - exp->Base;
    }
    if (index >= exp->NumberOfFunctions || !functions[index])
        return name ? STATUS_ENTRYPOINT_NOT_FOUND : STATUS_ORDINAL_NOT_FOUND;
    PUCHAR target = base + functions[index];
    if (target >= (PUCHAR)exp && target < (PUCHAR)exp + dir_size)
        return resolve_forwarder((PCSTR)target, address, ctx, depth);
    *address = target;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ imports */

static void report_missing(PLDR_DATA_TABLE_ENTRY importer, PCSTR dll, PCSTR name, ULONG ordinal, NTSTATUS status)
{
    if (name)
        DbgPrint("ntdll: %wZ imports %s from %s: not found (%08x)\n", &importer->BaseDllName, name, dll, status);
    else
        DbgPrint("ntdll: %wZ imports ordinal %u from %s: not found (%08x)\n", &importer->BaseDllName, ordinal, dll,
                 status);
}

static NTSTATUS snap_descriptor(PLDR_DATA_TABLE_ENTRY module, IMAGE_IMPORT_DESCRIPTOR *desc, LOAD_CONTEXT *ctx)
{
    PUCHAR base = module->DllBase;
    PCSTR dll_name = (PCSTR)(base + desc->Name);
    WCHAR wide[MAX_PATH];
    SIZE_T n = strlen(dll_name);
    if (n >= RTL_NUMBER_OF(wide))
        return STATUS_NAME_TOO_LONG;
    for (SIZE_T i = 0; i <= n; i++)
        wide[i] = (UCHAR)dll_name[i];
    PLDR_DATA_TABLE_ENTRY dep;
    NTSTATUS status = load_dll(wide, ctx, &dep);
    if (!NT_SUCCESS(status)) {
        DbgPrint("ntdll: %wZ needs %s, which cannot be loaded (%08x)\n", &module->BaseDllName, dll_name, status);
        return status;
    }
    IMAGE_THUNK_DATA *lookup = (IMAGE_THUNK_DATA *)(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk
                                                                                          : desc->FirstThunk));
    IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk);
    for (; lookup->u1.AddressOfData; lookup++, iat++) {
        PVOID address;
        if (IMAGE_SNAP_BY_ORDINAL(lookup->u1.Ordinal)) {
            ULONG ordinal = (ULONG)IMAGE_ORDINAL64(lookup->u1.Ordinal);
            status = resolve_export(dep, nullptr, 0, ordinal, &address, ctx, 0);
            if (!NT_SUCCESS(status)) {
                report_missing(module, dll_name, nullptr, ordinal, status);
                return status;
            }
        } else {
            IMAGE_IMPORT_BY_NAME *by_name = (IMAGE_IMPORT_BY_NAME *)(base + lookup->u1.AddressOfData);
            status = resolve_export(dep, (PCSTR)by_name->Name, by_name->Hint, 0, &address, ctx, 0);
            if (!NT_SUCCESS(status)) {
                report_missing(module, dll_name, (PCSTR)by_name->Name, 0, status);
                return status;
            }
        }
        iat->u1.Function = (ULONG_PTR)address;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS snap_imports(PLDR_DATA_TABLE_ENTRY module, LOAD_CONTEXT *ctx)
{
    if (module->Flags & LDRP_COR_ILONLY)
        return STATUS_SUCCESS;
    ULONG size;
    IMAGE_IMPORT_DESCRIPTOR *desc = LdrpImageDirectory(module->DllBase, IMAGE_DIRECTORY_ENTRY_IMPORT, &size);
    if (!desc)
        return STATUS_SUCCESS;
    /* the import address table usually lives in read only data. */
    ULONG iat_size = 0;
    PVOID iat = LdrpImageDirectory(module->DllBase, IMAGE_DIRECTORY_ENTRY_IAT, &iat_size);
    PVOID protect_base = iat ? iat : module->DllBase;
    SIZE_T protect_size = iat ? iat_size : module->SizeOfImage;
    ULONG old;
    NTSTATUS status = NtProtectVirtualMemory(NtCurrentProcess(), &protect_base, &protect_size, PAGE_READWRITE, &old);
    if (!NT_SUCCESS(status))
        return status;
    for (; desc->Name; desc++) {
        status = snap_descriptor(module, desc, ctx);
        if (!NT_SUCCESS(status))
            break;
    }
    ULONG ignored;
    NtProtectVirtualMemory(NtCurrentProcess(), &protect_base, &protect_size, old, &ignored);
    NtFlushInstructionCache(NtCurrentProcess(), nullptr, 0);
    return status;
}

/* ------------------------------------------------------------------ thread local storage */

static void register_tls(PLDR_DATA_TABLE_ENTRY e)
{
    if (e->Flags & LDRP_COR_ILONLY)
        return;
    ULONG size;
    IMAGE_TLS_DIRECTORY *tls = LdrpImageDirectory(e->DllBase, IMAGE_DIRECTORY_ENTRY_TLS, &size);
    if (!tls || g_tls_count >= RTL_NUMBER_OF(g_tls_modules))
        return;
    ULONG index = g_tls_count++;
    g_tls_modules[index].Entry = e;
    g_tls_modules[index].Directory = tls;
    e->TlsIndex = (USHORT)index;
    if (tls->AddressOfIndex)
        *(PULONG)tls->AddressOfIndex = index;
}

/* fills the empty slots of a thread with fresh copies of the static tls blocks. */
static NTSTATUS fill_tls(TLS_VECTOR *v)
{
    for (ULONG i = 0; i < g_tls_count; i++) {
        if (v->Slots[i])
            continue;
        IMAGE_TLS_DIRECTORY *tls = g_tls_modules[i].Directory;
        SIZE_T raw = (SIZE_T)(tls->EndAddressOfRawData - tls->StartAddressOfRawData);
        PUCHAR block = heap_alloc(raw + tls->SizeOfZeroFill + 16);
        if (!block)
            return STATUS_NO_MEMORY;
        memcpy(block, (PVOID)tls->StartAddressOfRawData, raw);
        v->Slots[i] = block;
    }
    return STATUS_SUCCESS;
}

/* gives the current thread its copy of every static tls block. */
static NTSTATUS allocate_thread_tls(void)
{
    if (!g_tls_count)
        return STATUS_SUCCESS;
    PTEB teb = NtCurrentTeb();
    if (teb->ThreadLocalStoragePointer)
        return fill_tls(CONTAINING_RECORD(teb->ThreadLocalStoragePointer, TLS_VECTOR, Slots));
    TLS_VECTOR *v = heap_alloc(sizeof(*v));
    if (!v)
        return STATUS_NO_MEMORY;
    InsertTailList(&g_tls_vectors, &v->Link);
    teb->ThreadLocalStoragePointer = v->Slots;
    return fill_tls(v);
}

/* a dll with tls loaded after threads started: as on windows, each running thread gets its blocks. */
static NTSTATUS allocate_process_tls(void)
{
    NTSTATUS status = allocate_thread_tls();
    for (PLIST_ENTRY l = g_tls_vectors.Flink; NT_SUCCESS(status) && l != &g_tls_vectors; l = l->Flink)
        status = fill_tls(CONTAINING_RECORD(l, TLS_VECTOR, Link));
    return status;
}

static void free_thread_tls(void)
{
    PTEB teb = NtCurrentTeb();
    PVOID *slots = teb->ThreadLocalStoragePointer;
    if (!slots)
        return;
    teb->ThreadLocalStoragePointer = nullptr;
    TLS_VECTOR *v = CONTAINING_RECORD(slots, TLS_VECTOR, Slots);
    RemoveEntryList(&v->Link);
    for (ULONG i = 0; i < g_tls_count; i++)
        heap_free(v->Slots[i]);
    heap_free(v);
}

static void call_tls_callbacks(PLDR_DATA_TABLE_ENTRY e, ULONG reason)
{
    if (e->Flags & LDRP_COR_ILONLY)
        return;
    ULONG size;
    IMAGE_TLS_DIRECTORY *tls = LdrpImageDirectory(e->DllBase, IMAGE_DIRECTORY_ENTRY_TLS, &size);
    if (!tls || !tls->AddressOfCallBacks)
        return;
    for (PIMAGE_TLS_CALLBACK *cb = (PIMAGE_TLS_CALLBACK *)tls->AddressOfCallBacks; *cb; cb++)
        (*cb)(e->DllBase, reason, nullptr);
}

/* ------------------------------------------------------------------ dll entry points */

/* tls callbacks and DllMain run with the module's activation context active, the process default when it has none. */
static BOOLEAN call_entry(PLDR_DATA_TABLE_ENTRY e, ULONG reason, PVOID reserved)
{
    LDRP_SXS_FRAME frame;
    LdrpSxsActivate(&frame, e->EntryPointActivationContext);
    call_tls_callbacks(e, reason);
    BOOLEAN ok = TRUE;
    if (e->EntryPoint && (e->Flags & LDRP_IMAGE_DLL)) {
        PVOID saved = g_ldr.EntryInProgress;
        g_ldr.EntryInProgress = e;
        ok = ((PDLL_INIT_ROUTINE)e->EntryPoint)(e->DllBase, reason, reserved);
        g_ldr.EntryInProgress = saved;
    }
    LdrpSxsDeactivate(&frame);
    return ok;
}

/* runs DllMain(PROCESS_ATTACH) for every module not yet initialized, dependencies first. */
static NTSTATUS run_initializers(PVOID reserved)
{
    for (PLIST_ENTRY l = g_ldr.InInitializationOrderModuleList.Flink; l != &g_ldr.InInitializationOrderModuleList;
         l = l->Flink) {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InInitializationOrderLinks);
        if (e->Flags & (LDRP_PROCESS_ATTACH_CALLED | LDRP_ENTRY_PROCESSED))
            continue;
        e->Flags |= LDRP_ENTRY_PROCESSED;
        if (!(e->Flags & LDRP_IMAGE_DLL))
            continue;
        if (!call_entry(e, DLL_PROCESS_ATTACH, reserved)) {
            DbgPrint("ntdll: initialization of %wZ failed\n", &e->BaseDllName);
            return STATUS_DLL_INIT_FAILED;
        }
        e->Flags |= LDRP_PROCESS_ATTACH_CALLED;
    }
    return STATUS_SUCCESS;
}

/* initializes one module after the modules it imports, the ones a DllMain that loads a library has to wait for. */
static NTSTATUS init_module(PLDR_DATA_TABLE_ENTRY e, PVOID reserved)
{
    if (e->Flags & (LDRP_PROCESS_ATTACH_CALLED | LDRP_ENTRY_PROCESSED))
        return STATUS_SUCCESS;
    e->Flags |= LDRP_ENTRY_PROCESSED;
    ULONG size;
    IMAGE_IMPORT_DESCRIPTOR *desc = (e->Flags & LDRP_COR_ILONLY) ? nullptr :
                                    LdrpImageDirectory(e->DllBase, IMAGE_DIRECTORY_ENTRY_IMPORT, &size);
    for (; desc && desc->Name; desc++) {
        PCSTR name = (PCSTR)((PUCHAR)e->DllBase + desc->Name);
        WCHAR wide[MAX_PATH];
        SIZE_T n = strlen(name);
        if (n >= RTL_NUMBER_OF(wide))
            continue;
        for (SIZE_T i = 0; i <= n; i++)
            wide[i] = (UCHAR)name[i];
        PCWSTR full = resolve_api_set(wide);
        PCWSTR base_name = full + wcslen(full);
        while (base_name > full && base_name[-1] != '\\' && base_name[-1] != '/')
            base_name--;
        PLDR_DATA_TABLE_ENTRY dependency = find_by_name(base_name);
        if (dependency && dependency != e) {
            NTSTATUS status = init_module(dependency, reserved);
            if (!NT_SUCCESS(status))
                return status;
        }
    }
    if (!(e->Flags & LDRP_IMAGE_DLL))
        return STATUS_SUCCESS;
    if (!call_entry(e, DLL_PROCESS_ATTACH, reserved)) {
        DbgPrint("ntdll: initialization of %wZ failed\n", &e->BaseDllName);
        return STATUS_DLL_INIT_FAILED;
    }
    e->Flags |= LDRP_PROCESS_ATTACH_CALLED;
    return STATUS_SUCCESS;
}

/*
 * what a load or a lookup added to the initialization list, and what it was asked for: a DllMain that loads a
 * library or looks up a procedure must not start the modules that wait for the outer load.
 */
static NTSTATUS run_new_initializers(PLIST_ENTRY before, PLDR_DATA_TABLE_ENTRY target)
{
    NTSTATUS status = target ? init_module(target, nullptr) : STATUS_SUCCESS;
    for (PLIST_ENTRY l = before->Flink; NT_SUCCESS(status) && l != &g_ldr.InInitializationOrderModuleList; l = l->Flink)
        status = init_module(CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InInitializationOrderLinks), nullptr);
    return status;
}

/* ------------------------------------------------------------------ load */

static NTSTATUS load_dll(PCWSTR name, LOAD_CONTEXT *ctx, PLDR_DATA_TABLE_ENTRY *out)
{
    name = resolve_api_set(name);
    PCWSTR base_name = name + wcslen(name);
    while (base_name > name && base_name[-1] != '\\' && base_name[-1] != '/')
        base_name--;
    /* the active activation context may redirect a plain name before any search. */
    WCHAR path[MAX_PATH];
    BOOLEAN redirected = NT_SUCCESS(LdrpSxsRedirectDll(name, path, RTL_NUMBER_OF(path)));
    PLDR_DATA_TABLE_ENTRY e = redirected ? find_by_path(path) : find_by_name(base_name);
    if (e) {
        e->ReferenceCount++;
        e->ObsoleteLoadCount++;
        *out = e;
        return STATUS_SUCCESS;
    }
    NTSTATUS status = redirected ? STATUS_SUCCESS : find_dll(name, ctx->SearchPath, path, RTL_NUMBER_OF(path));
    if (!NT_SUCCESS(status))
        return status;
    HANDLE file;
    status = open_image(path, &file);
    if (!NT_SUCCESS(status))
        return status;
    PVOID base;
    status = map_image(file, &base);
    NtClose(file);
    if (!NT_SUCCESS(status))
        return status;
    e = create_entry(base, path);
    if (!e) {
        NtUnmapViewOfSection(NtCurrentProcess(), base);
        return STATUS_NO_MEMORY;
    }
    e->Flags |= LDRP_LOAD_IN_PROGRESS | (ctx->Static ? LDRP_STATIC_LINK : 0);
    /* a dll's own manifest is its context, active while its imports resolve. */
    if (!(e->Flags & LDRP_COR_ILONLY))
        status = LdrpSxsProbeDll(base, path, (PACTIVATION_CONTEXT *)&e->EntryPointActivationContext);
    if (NT_SUCCESS(status)) {
        LDRP_SXS_FRAME frame;
        LdrpSxsActivate(&frame, e->EntryPointActivationContext);
        status = snap_imports(e, ctx);
        LdrpSxsDeactivate(&frame);
    }
    if (!NT_SUCCESS(status)) {
        destroy_entry(e);
        NtUnmapViewOfSection(NtCurrentProcess(), base);
        return status;
    }
    e->Flags &= ~LDRP_LOAD_IN_PROGRESS;
    register_tls(e);
    /* post order: a module enters the init list after everything it imports. */
    InsertTailList(&g_ldr.InInitializationOrderModuleList, &e->InInitializationOrderLinks);
    *out = e;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ process and thread init */

static void normalize_parameters(PRTL_USER_PROCESS_PARAMETERS pp)
{
    if (!pp || (pp->Flags & RTL_USER_PROC_PARAMS_NORMALIZED))
        return;
#define FIX(s)                                                                                                    \
    if ((s).Buffer)                                                                                               \
    (s).Buffer = (PWSTR)((PUCHAR)pp + (ULONG_PTR)(s).Buffer)
    FIX(pp->CurrentDirectory.DosPath);
    FIX(pp->DllPath);
    FIX(pp->ImagePathName);
    FIX(pp->CommandLine);
    FIX(pp->WindowTitle);
    FIX(pp->DesktopInfo);
    FIX(pp->ShellInfo);
    FIX(pp->RuntimeData);
#undef FIX
    pp->Flags |= RTL_USER_PROC_PARAMS_NORMALIZED;
}

static void fatal(PCSTR what, NTSTATUS status)
{
    DbgPrint("ntdll: %s (%08x)\n", what, status);
    NtTerminateProcess(NtCurrentProcess(), status);
}

#if defined(__x86_64__) && defined(MIME_X64_NATIVE)
static void load_wow64(LOAD_CONTEXT *ctx)
{
    WCHAR path[MAX_PATH];
    wcscpy(path, g_system32);
    wcscat(path, L"wow64.dll");
    PLDR_DATA_TABLE_ENTRY wow64;
    NTSTATUS status = load_dll(path, ctx, &wow64);
    if (!NT_SUCCESS(status))
        fatal("cannot load wow64.dll for a 32-bit program", status);
    static const struct {
        const char *name;
        PVOID *target;
    } entries[] = {
        {"Wow64LdrpInitialize", (PVOID *)&g_wow64_initialize},
        {"Wow64PrepareForException", (PVOID *)&LdrpWow64PrepareForException},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(entries); i++) {
        ANSI_STRING name;
        RtlInitAnsiString(&name, entries[i].name);
        status = LdrGetProcedureAddress(wow64->DllBase, &name, 0, entries[i].target);
        if (!NT_SUCCESS(status))
            fatal("wow64.dll lacks an entry point", status);
    }
}
#endif

static void initialize_process(PCONTEXT context)
{
    PPEB peb = NtCurrentPeb();
    LdrpNtdllBase = &__ImageBase;
    LdrpInitializeProcessHeap();
    if (!peb->ProcessHeap)
        fatal("cannot create the process heap", STATUS_NO_MEMORY);

    g_ldr.Length = sizeof(g_ldr);
    g_ldr.Initialized = TRUE;
    InitializeListHead(&g_ldr.InLoadOrderModuleList);
    InitializeListHead(&g_ldr.InMemoryOrderModuleList);
    InitializeListHead(&g_ldr.InInitializationOrderModuleList);
    peb->Ldr = &g_ldr;
    peb->LoaderLock = &LdrpLoaderLock;
    peb->FastPebLock = &g_peb_lock;
    peb->TlsBitmap = peb->TlsBitmapBits;
    peb->TlsExpansionBitmap = peb->TlsExpansionBitmapBits;
    normalize_parameters(peb->ProcessParameters);

    RtlEnterCriticalSection(&LdrpLoaderLock);
    PLDR_DATA_TABLE_ENTRY exe = create_entry(peb->ImageBaseAddress, peb->ProcessParameters->ImagePathName.Buffer);
    WCHAR ntdll_path[MAX_PATH];
    wcscpy(ntdll_path, g_system32);
    wcscat(ntdll_path, L"ntdll.dll");
    PLDR_DATA_TABLE_ENTRY ntdll = create_entry(LdrpNtdllBase, ntdll_path);
    if (!exe || !ntdll)
        fatal("cannot create loader entries", STATUS_NO_MEMORY);
    LdrpSxsInitializeThread();
    ntdll->Flags |= LDRP_ENTRY_PROCESSED | LDRP_PROCESS_ATTACH_CALLED;
    InsertTailList(&g_ldr.InInitializationOrderModuleList, &ntdll->InInitializationOrderLinks);

    LOAD_CONTEXT ctx = {TRUE, nullptr};
    NTSTATUS status;
#if defined(__x86_64__) && defined(MIME_X64_NATIVE)
    ULONG_PTR peb32 = 0;
    if (NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessWow64Information, &peb32, sizeof(peb32),
                                             nullptr)) &&
        peb32) {
        load_wow64(&ctx);
        status = allocate_thread_tls();
        if (NT_SUCCESS(status))
            status = run_initializers(context);
        if (!NT_SUCCESS(status))
            fatal("wow64 initialization failed", status);
        RtlLeaveCriticalSection(&LdrpLoaderLock);
        return;
    }
#endif
    /* the process default activation context, built from the program's manifest as csrss does on windows. */
    status = LdrpSxsInitializeProcess(peb->ImageBaseAddress, peb->ProcessParameters->ImagePathName.Buffer);
    if (!NT_SUCCESS(status))
        fatal("the program's side by side configuration is incorrect", status);
    /* every win32 process gets kernel32, imported or not. */
    if (peb->ImageSubsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI || peb->ImageSubsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI) {
        PLDR_DATA_TABLE_ENTRY k32;
        status = load_dll(L"kernel32.dll", &ctx, &k32);
        if (NT_SUCCESS(status)) {
            ANSI_STRING thunk = RTL_CONSTANT_STRING("BaseThreadInitThunk");
            LdrGetProcedureAddress(k32->DllBase, &thunk, 0, &LdrpKernel32ThreadInitThunk);
        } else {
            DbgPrint("ntdll: kernel32.dll is not available (%08x)\n", status);
        }
    }
    status = snap_imports(exe, &ctx);
    if (!NT_SUCCESS(status))
        fatal("cannot resolve the imports of the program", status);
    register_tls(exe);
    status = allocate_thread_tls();
    if (!NT_SUCCESS(status))
        fatal("cannot allocate thread local storage", status);
    status = run_initializers(context);
    if (!NT_SUCCESS(status))
        fatal("dll initialization failed", status);
    exe->Flags |= LDRP_ENTRY_PROCESSED;
    call_tls_callbacks(exe, DLL_PROCESS_ATTACH);
    RtlLeaveCriticalSection(&LdrpLoaderLock);
}

static void initialize_thread(void)
{
    LdrpSxsInitializeThread();
    RtlEnterCriticalSection(&LdrpLoaderLock);
    allocate_thread_tls();
    for (PLIST_ENTRY l = g_ldr.InInitializationOrderModuleList.Flink; l != &g_ldr.InInitializationOrderModuleList;
         l = l->Flink) {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InInitializationOrderLinks);
        if ((e->Flags & LDRP_PROCESS_ATTACH_CALLED) && !(e->Flags & LDRP_DONT_CALL_FOR_THREADS) && e->DllBase != LdrpNtdllBase)
            call_entry(e, DLL_THREAD_ATTACH, nullptr);
    }
    PLDR_DATA_TABLE_ENTRY exe = find_by_base(NtCurrentPeb()->ImageBaseAddress);
    if (exe)
        call_tls_callbacks(exe, DLL_THREAD_ATTACH);
    RtlLeaveCriticalSection(&LdrpLoaderLock);
}

/* called from LdrInitializeThunk on every new thread before it starts. */
void NTAPI LdrpInitialize(PCONTEXT context)
{
    if (!g_process_initialized) {
        initialize_process(context);
        g_process_initialized = TRUE;
        if (g_wow64_initialize)
            g_wow64_initialize(context);
    } else {
        if (g_wow64_initialize)
            g_wow64_initialize(context);
        initialize_thread();
    }
}

NTDLL_EXPORT void NTAPI LdrShutdownThread(void)
{
    RtlEnterCriticalSection(&LdrpLoaderLock);
    for (PLIST_ENTRY l = g_ldr.InInitializationOrderModuleList.Blink; l != &g_ldr.InInitializationOrderModuleList;
         l = l->Blink) {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InInitializationOrderLinks);
        if ((e->Flags & LDRP_PROCESS_ATTACH_CALLED) && !(e->Flags & LDRP_DONT_CALL_FOR_THREADS) && e->DllBase != LdrpNtdllBase)
            call_entry(e, DLL_THREAD_DETACH, nullptr);
    }
    free_thread_tls();
    RtlLeaveCriticalSection(&LdrpLoaderLock);
    RtlFreeThreadActivationContextStack();
}

NTDLL_EXPORT void NTAPI LdrShutdownProcess(void)
{
    RtlEnterCriticalSection(&LdrpLoaderLock);
    g_process_detaching = TRUE;
    g_ldr.ShutdownInProgress = TRUE;
    g_ldr.ShutdownThreadId = NtCurrentThreadId();
    PLDR_DATA_TABLE_ENTRY exe = find_by_base(NtCurrentPeb()->ImageBaseAddress);
    if (exe)
        call_tls_callbacks(exe, DLL_PROCESS_DETACH);
    for (PLIST_ENTRY l = g_ldr.InInitializationOrderModuleList.Blink; l != &g_ldr.InInitializationOrderModuleList;
         l = l->Blink) {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InInitializationOrderLinks);
        if (e->Flags & LDRP_PROCESS_ATTACH_CALLED) {
            e->Flags &= ~LDRP_PROCESS_ATTACH_CALLED;
            call_entry(e, DLL_PROCESS_DETACH, (PVOID)1);
        }
    }
    RtlLeaveCriticalSection(&LdrpLoaderLock);
}

/* ------------------------------------------------------------------ public loader api */

static void unicode_to_cstr(PCUNICODE_STRING s, PWSTR out, SIZE_T out_chars)
{
    SIZE_T n = s->Length / sizeof(WCHAR);
    if (n >= out_chars)
        n = out_chars - 1;
    memcpy(out, s->Buffer, n * sizeof(WCHAR));
    out[n] = 0;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrLoadDll(PCWSTR search_path, PULONG flags, PCUNICODE_STRING name, PVOID *base)
{
    WCHAR dll[MAX_PATH];
    unicode_to_cstr(name, dll, RTL_NUMBER_OF(dll));
    /* the search path argument is a ';' separated list unless it is a small flags value. */
    LOAD_CONTEXT ctx = {FALSE, ((ULONG_PTR)search_path & ~(ULONG_PTR)0xFFFF) ? search_path : nullptr};
    RtlEnterCriticalSection(&LdrpLoaderLock);
    PLDR_DATA_TABLE_ENTRY e;
    PLIST_ENTRY before = g_ldr.InInitializationOrderModuleList.Blink;
    NTSTATUS status = load_dll(dll, &ctx, &e);
    if (NT_SUCCESS(status)) {
        allocate_process_tls();
        status = run_new_initializers(before, e);
        if (NT_SUCCESS(status))
            *base = e->DllBase;
    }
    RtlLeaveCriticalSection(&LdrpLoaderLock);
    return status;
}

/*
 * the last 64 modules to be unloaded, which crash reporters ask for to say what had been loaded when a process fell: the
 * entries windows keeps, in the layout of RtlGetUnloadEventTraceEx. an entry has its module's address and size, a serial
 * number, the time stamp and checksum of its headers, the first 31 characters of its name and, where windows puts the
 * version of the file, zeros. the first event takes the serial that follows the start value of windows, and the slot of
 * its serial modulo the number of slots.
 */
typedef struct _RTL_UNLOAD_EVENT_TRACE_EX {
    PVOID BaseAddress;
    SIZE_T SizeOfImage;
    ULONG Sequence;
    ULONG TimeDateStamp;
    ULONG CheckSum;
    WCHAR ImageName[32];
    ULONG Version[2];
} RTL_UNLOAD_EVENT_TRACE_EX;

#define UNLOAD_EVENT_SLOTS 64
static RTL_UNLOAD_EVENT_TRACE_EX g_unload_events[UNLOAD_EVENT_SLOTS];
static ULONG g_unload_event_size = sizeof(RTL_UNLOAD_EVENT_TRACE_EX);
static ULONG g_unload_event_count = UNLOAD_EVENT_SLOTS;
static ULONG g_unload_event_index = 0xFFFFFFFF;

static void record_unload(PLDR_DATA_TABLE_ENTRY e)
{
    ULONG index = g_unload_event_index++;
    RTL_UNLOAD_EVENT_TRACE_EX *event = &g_unload_events[(USHORT)(index % UNLOAD_EVENT_SLOTS)];
    memset(event, 0, sizeof(*event));
    event->Sequence = index;
    event->BaseAddress = e->DllBase;
    event->SizeOfImage = e->SizeOfImage;
    PIMAGE_NT_HEADERS nt = LdrpNtHeaders(e->DllBase);
    if (nt) {
        event->TimeDateStamp = nt->FileHeader.TimeDateStamp;
        event->CheckSum = nt->OptionalHeader.CheckSum;
    }
    USHORT bytes = e->BaseDllName.Length > sizeof(event->ImageName) ? sizeof(event->ImageName) : e->BaseDllName.Length;
    memcpy(event->ImageName, e->BaseDllName.Buffer, bytes);
    if (bytes < sizeof(event->ImageName))
        event->ImageName[bytes / sizeof(WCHAR)] = 0;
}

/* the sizes and the entries of the trace by address: the pointers stay valid for the life of the process. */
NTDLL_EXPORT PVOID NTAPI RtlGetUnloadEventTraceEx(PULONG *element_size, PULONG *element_count, PVOID *trace)
{
    *element_size = &g_unload_event_size;
    *element_count = &g_unload_event_count;
    *trace = g_unload_events;
    return g_unload_events;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrUnloadDll(PVOID base)
{
    RtlEnterCriticalSection(&LdrpLoaderLock);
    PLDR_DATA_TABLE_ENTRY e = find_by_base(base);
    NTSTATUS status = STATUS_DLL_NOT_FOUND;
    if (e) {
        status = STATUS_SUCCESS;
        if (e->ObsoleteLoadCount != 0xFFFF && !(e->Flags & LDRP_STATIC_LINK) && --e->ReferenceCount == 0) {
            if (e->Flags & LDRP_PROCESS_ATTACH_CALLED)
                call_entry(e, DLL_PROCESS_DETACH, nullptr);
            /* dependencies keep their references; they stay loaded. */
            record_unload(e);
            destroy_entry(e);
            NtUnmapViewOfSection(NtCurrentProcess(), base);
        }
    }
    RtlLeaveCriticalSection(&LdrpLoaderLock);
    return status;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrGetDllHandleEx(ULONG flags, PCWSTR path, PULONG characteristics,
                                              PCUNICODE_STRING name, PVOID *base)
{
    WCHAR dll[MAX_PATH];
    unicode_to_cstr(name, dll, RTL_NUMBER_OF(dll));
    PCWSTR resolved = resolve_api_set(dll);
    PCWSTR file = resolved + wcslen(resolved);
    while (file > resolved && file[-1] != '\\' && file[-1] != '/')
        file--;
    WCHAR redirected[MAX_PATH];
    BOOLEAN is_redirected = NT_SUCCESS(LdrpSxsRedirectDll(resolved, redirected, RTL_NUMBER_OF(redirected)));
    RtlEnterCriticalSection(&LdrpLoaderLock);
    PLDR_DATA_TABLE_ENTRY e = is_redirected ? find_by_path(redirected) : find_by_name(file);
    if (e && !(flags & 2)) /* LDR_GET_DLL_HANDLE_EX_UNCHANGED_REFCOUNT */
        e->ReferenceCount++;
    RtlLeaveCriticalSection(&LdrpLoaderLock);
    if (!e)
        return STATUS_DLL_NOT_FOUND;
    *base = e->DllBase;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrGetDllHandle(PCWSTR path, PULONG characteristics, PCUNICODE_STRING name, PVOID *base)
{
    return LdrGetDllHandleEx(2, path, characteristics, name, base);
}

NTDLL_EXPORT NTSTATUS NTAPI LdrAddRefDll(ULONG flags, PVOID base)
{
    RtlEnterCriticalSection(&LdrpLoaderLock);
    PLDR_DATA_TABLE_ENTRY e = find_by_base(base);
    if (e) {
        if (flags & 1) /* LDR_ADDREF_DLL_PIN */
            e->ObsoleteLoadCount = 0xFFFF;
        e->ReferenceCount++;
    }
    RtlLeaveCriticalSection(&LdrpLoaderLock);
    return e ? STATUS_SUCCESS : STATUS_DLL_NOT_FOUND;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrGetProcedureAddressEx(PVOID base, PCANSI_STRING name, ULONG ordinal, PVOID *address,
                                                     ULONG flags)
{
    char buf[256];
    PCSTR cname = nullptr;
    if (name) {
        if (name->Length >= sizeof(buf))
            return STATUS_NAME_TOO_LONG;
        memcpy(buf, name->Buffer, name->Length);
        buf[name->Length] = 0;
        cname = buf;
    }
    RtlEnterCriticalSection(&LdrpLoaderLock);
    PLDR_DATA_TABLE_ENTRY e = find_by_base(base);
    NTSTATUS status = STATUS_DLL_NOT_FOUND;
    if (e) {
        LOAD_CONTEXT ctx = {FALSE, nullptr};
        PLIST_ENTRY before = g_ldr.InInitializationOrderModuleList.Blink;
        status = resolve_export(e, cname, 0, ordinal, address, &ctx, 0);
        if (NT_SUCCESS(status))
            status = run_new_initializers(before, nullptr);
    }
    RtlLeaveCriticalSection(&LdrpLoaderLock);
    return status;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrGetProcedureAddress(PVOID base, PCANSI_STRING name, ULONG ordinal, PVOID *address)
{
    return LdrGetProcedureAddressEx(base, name, ordinal, address, 0);
}

/* ------------------------------------------------------------------ delay loads */

/*
 * what a failure hook is told: the delay load descriptor, the thunk, the name of the dll and what is imported,
 * the module if it was loaded, and the error. Notifications: 3 for a dll that did not load, 4 for a procedure
 * that was not found.
 */
typedef struct DELAYLOAD_INFO_ {
    ULONG Size;
    const IMAGE_DELAYLOAD_DESCRIPTOR *Descriptor;
    PVOID *ThunkAddress;
    PCSTR TargetDllName;
    struct {
        BOOLEAN ImportedByName;
        union {
            PCSTR Name;
            ULONG Ordinal;
        };
    } TargetApi;
    PVOID TargetModuleBase;
    PVOID Unused;
    ULONG LastError;
} DELAYLOAD_INFO_;

typedef PVOID(NTAPI *DELAYLOAD_DLL_HOOK)(ULONG reason, DELAYLOAD_INFO_ *info);
typedef PVOID(NTAPI *DELAYLOAD_SYSTEM_HOOK)(PCSTR dll, PCSTR procedure);

#define VCPP_LOAD_FAILED 0xC06D007E
#define VCPP_PROC_FAILED 0xC06D007F

/*
 * the address that an import of a delay load descriptor stands for, loaded on the first use: the dll is
 * loaded by the name in the descriptor and kept in its module slot, the export is found by name or ordinal
 * from the import name table entry at the thunk's index, and the thunk of the import address table is
 * replaced by it. A thunk that no longer points into the image is already resolved and gives what it holds.
 * A dll or an export that is missing goes to the hook of the program, then to the system hook, and is
 * raised as an exception when neither gives an address.
 */
NTDLL_EXPORT PVOID NTAPI LdrResolveDelayLoadedAPI(PVOID base, const IMAGE_DELAYLOAD_DESCRIPTOR *descriptor,
                                                  DELAYLOAD_DLL_HOOK dll_hook, DELAYLOAD_SYSTEM_HOOK system_hook,
                                                  PVOID *thunk, ULONG flags)
{
    if (!(descriptor->Attributes & 1) || !base)
        return nullptr;
    PLDR_DATA_TABLE_ENTRY owner;
    if (!NT_SUCCESS(LdrFindEntryForAddress(base, &owner)) || owner->DllBase != base)
        return nullptr;
    if ((ULONG_PTR)*thunk - (ULONG_PTR)base >= owner->SizeOfImage)
        return *thunk;
    PUCHAR image = base;
    PCSTR dll_name = (PCSTR)(image + descriptor->DllNameRVA);
    ULONG index = (ULONG)(((PUCHAR)thunk - (image + descriptor->ImportAddressTableRVA)) / sizeof(PVOID));
    ULONG_PTR entry = ((ULONG_PTR *)(image + descriptor->ImportNameTableRVA))[index];
    BOOLEAN by_name = !(entry >> (sizeof(ULONG_PTR) * 8 - 1));
    PCSTR procedure = by_name ? (PCSTR)(image + (entry & 0x7fffffff) + 2) : nullptr;
    ULONG ordinal = by_name ? 0 : (ULONG)(entry & 0xffff);

    DELAYLOAD_INFO_ info = {sizeof(info), descriptor, thunk, dll_name};
    info.TargetApi.ImportedByName = by_name;
    if (by_name)
        info.TargetApi.Name = procedure;
    else
        info.TargetApi.Ordinal = ordinal;

    PVOID *slot = (PVOID *)(image + descriptor->ModuleHandleRVA);
    PVOID module = *slot;
    PVOID address = nullptr;
    NTSTATUS status = STATUS_SUCCESS;
    if (!module) {
        WCHAR wide[MAX_PATH];
        SIZE_T n = strlen(dll_name);
        if (n >= RTL_NUMBER_OF(wide))
            n = RTL_NUMBER_OF(wide) - 1;
        for (SIZE_T i = 0; i < n; i++)
            wide[i] = (UCHAR)dll_name[i];
        wide[n] = 0;
        UNICODE_STRING name;
        RtlInitUnicodeString(&name, wide);
        status = LdrLoadDll(nullptr, nullptr, &name, &module);
        if (NT_SUCCESS(status)) {
            /* another thread may have been first; its module is the one kept. */
            PVOID raced = __atomic_exchange_n(slot, module, __ATOMIC_ACQ_REL);
            (void)raced;
        }
    }
    info.TargetModuleBase = NT_SUCCESS(status) ? module : nullptr;
    ULONG notification = 3;
    if (NT_SUCCESS(status)) {
        ANSI_STRING ansi;
        if (by_name)
            RtlInitAnsiString(&ansi, procedure);
        status = LdrGetProcedureAddress(module, by_name ? &ansi : nullptr, ordinal, &address);
        notification = 4;
    }
    if (NT_SUCCESS(status)) {
        PVOID target = thunk;
        SIZE_T size = sizeof(PVOID);
        ULONG old;
        if (NT_SUCCESS(NtProtectVirtualMemory(NtCurrentProcess(), &target, &size, PAGE_READWRITE, &old))) {
            *thunk = address;
            NtProtectVirtualMemory(NtCurrentProcess(), &target, &size, old, &old);
        }
        return address;
    }
    info.LastError = RtlNtStatusToDosError(status);
    PVOID substitute = nullptr;
    if (dll_hook)
        substitute = dll_hook(notification, &info);
    if (!substitute && system_hook && (flags == 0 || flags == 8))
        substitute = system_hook(dll_name, by_name ? procedure : (PCSTR)(ULONG_PTR)ordinal);
    if (substitute)
        return substitute;
    EXCEPTION_RECORD record = {};
    record.ExceptionCode = notification == 3 ? VCPP_LOAD_FAILED : VCPP_PROC_FAILED;
    record.NumberParameters = 1;
    record.ExceptionInformation[0] = (ULONG_PTR)&info;
    RtlRaiseException(&record);
    return nullptr;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrDisableThreadCalloutsForDll(PVOID base)
{
    RtlEnterCriticalSection(&LdrpLoaderLock);
    PLDR_DATA_TABLE_ENTRY e = find_by_base(base);
    if (e)
        e->Flags |= LDRP_DONT_CALL_FOR_THREADS;
    RtlLeaveCriticalSection(&LdrpLoaderLock);
    return e ? STATUS_SUCCESS : STATUS_DLL_NOT_FOUND;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrLockLoaderLock(ULONG flags, PULONG disposition, PULONG_PTR cookie)
{
    if (flags & 2) { /* LDR_LOCK_LOADER_LOCK_FLAG_TRY_ONLY */
        BOOLEAN got = RtlTryEnterCriticalSection(&LdrpLoaderLock);
        if (disposition)
            *disposition = got ? 1 : 2;
    } else {
        RtlEnterCriticalSection(&LdrpLoaderLock);
        if (disposition)
            *disposition = 1;
    }
    if (cookie)
        *cookie = (ULONG_PTR)NtCurrentThreadId();
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrUnlockLoaderLock(ULONG flags, ULONG_PTR cookie)
{
    RtlLeaveCriticalSection(&LdrpLoaderLock);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrQueryImageFileExecutionOptions(PUNICODE_STRING key, PCWSTR value, ULONG type,
                                                              PVOID buffer, ULONG length, PULONG returned)
{
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrGetDllFullName(PVOID base, PUNICODE_STRING name)
{
    PLDR_DATA_TABLE_ENTRY e = base ? find_by_base(base) : find_by_base(NtCurrentPeb()->ImageBaseAddress);
    if (!e)
        return STATUS_DLL_NOT_FOUND;
    RtlCopyUnicodeString(name, &e->FullDllName);
    return name->MaximumLength >= e->FullDllName.Length ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

/* ------------------------------------------------------------------ thread start and exit */

typedef ULONG(NTAPI *PTHREAD_START_ROUTINE)(PVOID parameter);
typedef void(NTAPI *PBASE_THREAD_INIT_THUNK)(ULONG flags, PVOID entry, PVOID parameter);

NTDLL_EXPORT DECLSPEC_NORETURN void NTAPI RtlUserThreadStart(PVOID entry, PVOID parameter)
{
    if (LdrpKernel32ThreadInitThunk)
        ((PBASE_THREAD_INIT_THUNK)LdrpKernel32ThreadInitThunk)(0, entry, parameter);
    RtlExitUserThread((NTSTATUS)((PTHREAD_START_ROUTINE)entry)(parameter));
}

NTDLL_EXPORT DECLSPEC_NORETURN void NTAPI RtlExitUserThread(NTSTATUS status)
{
    ULONG last = 0;
    NtQueryInformationThread(NtCurrentThread(), ThreadAmILastThread, &last, sizeof(last), nullptr);
    if (last)
        RtlExitUserProcess(status);
    LdrShutdownThread();
    for (;;)
        NtTerminateThread(NtCurrentThread(), status);
}

NTDLL_EXPORT DECLSPEC_NORETURN void NTAPI RtlExitUserProcess(NTSTATUS status)
{
    if (!g_process_detaching)
        LdrShutdownProcess();
    for (;;)
        NtTerminateProcess(NtCurrentProcess(), status);
}

/* ------------------------------------------------------------------ peb lock */

NTDLL_EXPORT void NTAPI RtlAcquirePebLock(void)
{
    RtlEnterCriticalSection(&g_peb_lock);
}

NTDLL_EXPORT BOOLEAN NTAPI RtlTryAcquirePebLock(void)
{
    return RtlTryEnterCriticalSection(&g_peb_lock);
}

NTDLL_EXPORT void NTAPI RtlReleasePebLock(void)
{
    RtlLeaveCriticalSection(&g_peb_lock);
}

NTDLL_EXPORT PPEB NTAPI RtlGetCurrentPeb(void)
{
    return NtCurrentPeb();
}
