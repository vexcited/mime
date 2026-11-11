/*
 * resource lookup in mapped images.
 *
 * values below 0x10000 are integer ids, anything else points to a string,
 * and "#123" strings are converted to ids.
 */
#include "ntdll_private.h"

#define IS_INTRESOURCE(x) (((ULONG_PTR)(x) >> 16) == 0)

/* LoadLibraryEx sets the low bits of the handle for data file mappings. */
static PVOID image_base(PVOID handle, BOOLEAN *mapped_as_image)
{
    *mapped_as_image = !((ULONG_PTR)handle & 3);
    return (PVOID)((ULONG_PTR)handle & ~(ULONG_PTR)3);
}

static IMAGE_RESOURCE_DIRECTORY *root_directory(PVOID handle, PULONG size, PUCHAR *base_out, BOOLEAN *image)
{
    PUCHAR base = image_base(handle, image);
    *base_out = base;
    PIMAGE_NT_HEADERS nt = LdrpNtHeaders(base);
    if (!nt)
        return nullptr;
    /* follow the magic, not this build: resource only images like mui files are often pe32, with data directories
       16 bytes earlier, and a 32-bit process reads 64-bit images too. */
    PUCHAR optional = (PUCHAR)&nt->OptionalHeader;
    BOOLEAN pe32 = *(PUSHORT)optional == 0x10B;
    ULONG count = *(PULONG)(optional + (pe32 ? 92 : 108));
    IMAGE_DATA_DIRECTORY *dirs = (IMAGE_DATA_DIRECTORY *)(optional + (pe32 ? 96 : 112));
    if (count <= IMAGE_DIRECTORY_ENTRY_RESOURCE)
        return nullptr;
    IMAGE_DATA_DIRECTORY *dir = &dirs[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    if (!dir->VirtualAddress)
        return nullptr;
    *size = dir->Size;
    if (*image)
        return (IMAGE_RESOURCE_DIRECTORY *)(base + dir->VirtualAddress);
    /* data file mappings keep the raw file layout, so translate the rva through the section table. */
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (ULONG i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (dir->VirtualAddress >= sec->VirtualAddress &&
            dir->VirtualAddress < sec->VirtualAddress + (sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData))
            return (IMAGE_RESOURCE_DIRECTORY *)(base + sec->PointerToRawData + (dir->VirtualAddress - sec->VirtualAddress));
    }
    return nullptr;
}

/* like windows, the names of a file are compared as they are: kernelbase asks for them in upper case. */
static int compare_name(const IMAGE_RESOURCE_DIR_STRING_U *s, PCWSTR name)
{
    SIZE_T n = wcslen(name);
    SIZE_T m = s->Length;
    for (SIZE_T i = 0; i < n && i < m; i++) {
        if (s->NameString[i] != name[i])
            return s->NameString[i] < name[i] ? -1 : 1;
    }
    return m == n ? 0 : (m < n ? -1 : 1);
}

static IMAGE_RESOURCE_DIRECTORY_ENTRY *find_entry(IMAGE_RESOURCE_DIRECTORY *root, IMAGE_RESOURCE_DIRECTORY *dir,
                                                  ULONG_PTR id)
{
    IMAGE_RESOURCE_DIRECTORY_ENTRY *entries = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
    if (IS_INTRESOURCE(id)) {
        IMAGE_RESOURCE_DIRECTORY_ENTRY *ids = entries + dir->NumberOfNamedEntries;
        LONG lo = 0, hi = (LONG)dir->NumberOfIdEntries - 1;
        while (lo <= hi) {
            LONG mid = (lo + hi) / 2;
            USHORT v = (USHORT)ids[mid].Name;
            if (v == (USHORT)id)
                return &ids[mid];
            if (v < (USHORT)id)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        return nullptr;
    }
    LONG lo = 0, hi = (LONG)dir->NumberOfNamedEntries - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        const IMAGE_RESOURCE_DIR_STRING_U *s =
            (const IMAGE_RESOURCE_DIR_STRING_U *)((PUCHAR)root + (entries[mid].Name & 0x7FFFFFFF));
        int c = compare_name(s, (PCWSTR)id);
        if (!c)
            return &entries[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return nullptr;
}

static IMAGE_RESOURCE_DIRECTORY *subdirectory(IMAGE_RESOURCE_DIRECTORY *root, IMAGE_RESOURCE_DIRECTORY_ENTRY *e)
{
    if (!(e->OffsetToData & 0x80000000))
        return nullptr;
    return (IMAGE_RESOURCE_DIRECTORY *)((PUCHAR)root + (e->OffsetToData & 0x7FFFFFFF));
}

/*
 * the language, as windows 11 picks it. a language is tried as asked, then without its sublanguage, then as no
 * language at all, and nothing else stands in for it. no language, and the two defaults that have none, take
 * what the thread and then english would, and failing that whatever the directory holds first.
 */
static IMAGE_RESOURCE_DIRECTORY_ENTRY *find_language(IMAGE_RESOURCE_DIRECTORY *root, IMAGE_RESOURCE_DIRECTORY *dir,
                                                     ULONG_PTR lang)
{
    if (lang & 0x3FF) {
        USHORT candidates[] = {(USHORT)lang, (USHORT)(lang & 0x3FF), 0};
        for (ULONG i = 0; i < RTL_NUMBER_OF(candidates); i++) {
            IMAGE_RESOURCE_DIRECTORY_ENTRY *e = find_entry(root, dir, candidates[i]);
            if (e)
                return e;
        }
        return nullptr;
    }
    USHORT thread = (USHORT)(NtCurrentTeb()->CurrentLocale & 0xFFFF);
    USHORT candidates[] = {0, thread, (USHORT)(thread & 0x3FF), 0x0409, 0x0009};
    for (ULONG i = 0; i < RTL_NUMBER_OF(candidates); i++) {
        IMAGE_RESOURCE_DIRECTORY_ENTRY *e = find_entry(root, dir, candidates[i]);
        if (e)
            return e;
    }
    if (dir->NumberOfNamedEntries + dir->NumberOfIdEntries)
        return (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
    return nullptr;
}

/* level 1 = type directory, 2 = name directory, 3 = data entry. */
static NTSTATUS find_resource(PVOID handle, PLDR_RESOURCE_INFO info, ULONG level, PVOID *out, BOOLEAN want_dir)
{
    ULONG size;
    PUCHAR base;
    BOOLEAN image;
    IMAGE_RESOURCE_DIRECTORY *root = root_directory(handle, &size, &base, &image);
    if (!root)
        return STATUS_RESOURCE_DATA_NOT_FOUND;
    if (want_dir && level == 0) {
        *out = root;
        return STATUS_SUCCESS;
    }
    IMAGE_RESOURCE_DIRECTORY_ENTRY *e = find_entry(root, root, info->Type);
    if (!e)
        return STATUS_RESOURCE_TYPE_NOT_FOUND;
    IMAGE_RESOURCE_DIRECTORY *dir = subdirectory(root, e);
    if (!dir)
        return STATUS_RESOURCE_TYPE_NOT_FOUND;
    if (level == 1) {
        *out = want_dir ? (PVOID)dir : (PVOID)e;
        return STATUS_SUCCESS;
    }
    e = find_entry(root, dir, info->Name);
    if (!e)
        return STATUS_RESOURCE_NAME_NOT_FOUND;
    dir = subdirectory(root, e);
    if (!dir)
        return STATUS_RESOURCE_NAME_NOT_FOUND;
    if (level == 2) {
        *out = want_dir ? (PVOID)dir : (PVOID)e;
        return STATUS_SUCCESS;
    }
    e = find_language(root, dir, info->Language);
    if (!e || (e->OffsetToData & 0x80000000))
        return STATUS_RESOURCE_LANG_NOT_FOUND;
    *out = (PUCHAR)root + e->OffsetToData;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI LdrFindResource_U(PVOID handle, PLDR_RESOURCE_INFO info, ULONG level,
                                              IMAGE_RESOURCE_DATA_ENTRY **entry)
{
    return find_resource(handle, info, level, (PVOID *)entry, FALSE);
}

NTDLL_EXPORT NTSTATUS NTAPI LdrFindResourceDirectory_U(PVOID handle, PLDR_RESOURCE_INFO info, ULONG level,
                                                       IMAGE_RESOURCE_DIRECTORY **dir)
{
    return find_resource(handle, info, level, (PVOID *)dir, TRUE);
}

NTDLL_EXPORT NTSTATUS NTAPI LdrAccessResource(PVOID handle, const IMAGE_RESOURCE_DATA_ENTRY *entry, PVOID *data,
                                              PULONG size)
{
    BOOLEAN image;
    PUCHAR base = image_base(handle, &image);
    PIMAGE_NT_HEADERS nt = LdrpNtHeaders(base);
    if (!nt)
        return STATUS_INVALID_IMAGE_FORMAT;
    ULONG rva = entry->OffsetToData;
    if (image) {
        *data = base + rva;
    } else {
        IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
        *data = nullptr;
        for (ULONG i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            ULONG span = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
            if (rva >= sec->VirtualAddress && rva < sec->VirtualAddress + span) {
                *data = base + sec->PointerToRawData + (rva - sec->VirtualAddress);
                break;
            }
        }
        if (!*data)
            return STATUS_RESOURCE_DATA_NOT_FOUND;
    }
    if (size)
        *size = entry->Size;
    return STATUS_SUCCESS;
}
