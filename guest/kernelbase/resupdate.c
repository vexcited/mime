/*
 * updating the resources of a file. BeginUpdateResource collects the resources the file has, UpdateResource changes
 * the collection, and EndUpdateResource writes the file again with a resource section made from it, through a
 * temporary file beside it as windows does.
 */
#include "kernelbase_private.h"

#define OPT_INITIALIZED_DATA 8
#define OPT_ENTRY_POINT 16
#define OPT_BASE_OF_CODE 20
#define OPT_BASE_OF_DATA 24
#define OPT_SECTION_ALIGNMENT 32
#define OPT_FILE_ALIGNMENT 36
#define OPT_SIZE_OF_IMAGE 56
#define OPT_SIZE_OF_HEADERS 60
#define OPT_CHECKSUM 64

#define DEBUG_ENTRY_SIZE 28
#define DEBUG_ADDRESS_OF_RAW_DATA 20
#define DEBUG_POINTER_TO_RAW_DATA 24

/* what windows writes for every resource, whatever the file said before. */
#define DEFAULT_CODE_PAGE 1252
#define SUBDIRECTORY 0x80000000u

/* a type or a name: a string in upper case, or a number when Text is null. */
typedef struct RES_KEY {
    PWSTR Text;
    USHORT Length;
    USHORT Id;
} RES_KEY;

typedef struct RES_ITEM {
    RES_KEY Type, Name;
    USHORT Language;
    ULONG CodePage;
    PUCHAR Data;
    ULONG Size;
} RES_ITEM;

typedef struct RES_UPDATE {
    struct RES_UPDATE *Next;
    PWSTR File;
    DWORD Failed; /* the error of a change that failed, after which windows refuses the others and the writing. */
    RES_ITEM *Items;
    ULONG Count, Capacity;
} RES_UPDATE;

typedef struct PE {
    PUCHAR Base;
    ULONG Size;
    PIMAGE_FILE_HEADER File;
    PUCHAR Optional;
    PIMAGE_DATA_DIRECTORY Dirs;
    ULONG DirCount;
    PIMAGE_SECTION_HEADER Sections;
    BOOL Wide;
} PE;

static RTL_SRWLOCK g_lock;
static RES_UPDATE *g_updates;

#define OPT(pe, offset) (*(PULONG)((pe)->Optional + (offset)))

static ULONG align_up(ULONG value, ULONG alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

/* ------------------------------------------------------------------ the collection */

static void free_key(RES_KEY *key)
{
    KbFree(key->Text);
    key->Text = nullptr;
}

static void free_item(RES_ITEM *item)
{
    free_key(&item->Type);
    free_key(&item->Name);
    KbFree(item->Data);
}

static void free_update(RES_UPDATE *u)
{
    for (ULONG i = 0; i < u->Count; i++)
        free_item(&u->Items[i]);
    KbFree(u->Items);
    KbFree(u->File);
    KbFree(u);
}

/* the order of a resource directory: strings before numbers, a string that starts another before it. */
static int compare_keys(const RES_KEY *a, const RES_KEY *b)
{
    if (!a->Text != !b->Text)
        return a->Text ? -1 : 1;
    if (!a->Text)
        return a->Id < b->Id ? -1 : a->Id > b->Id;
    USHORT n = a->Length < b->Length ? a->Length : b->Length;
    for (USHORT i = 0; i < n; i++)
        if (a->Text[i] != b->Text[i])
            return a->Text[i] < b->Text[i] ? -1 : 1;
    return a->Length < b->Length ? -1 : a->Length > b->Length;
}

static int compare_items(const RES_ITEM *a, const RES_ITEM *b)
{
    int c = compare_keys(&a->Type, &b->Type);
    if (!c)
        c = compare_keys(&a->Name, &b->Name);
    if (!c)
        c = a->Language < b->Language ? -1 : a->Language > b->Language;
    return c;
}

static BOOL key_from_text(const WCHAR *text, SIZE_T chars, BOOL upcase, RES_KEY *key)
{
    memset(key, 0, sizeof(*key));
    if (chars > 0xFFFF) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    key->Text = KbAlloc((chars + 1) * sizeof(WCHAR));
    if (!key->Text) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    /* windows raises the letters a to z only, so a name with others is not one FindResource finds. */
    for (SIZE_T i = 0; i < chars; i++)
        key->Text[i] = upcase && text[i] >= 'a' && text[i] <= 'z' ? (WCHAR)(text[i] - 'a' + 'A') : text[i];
    key->Text[chars] = 0;
    key->Length = (USHORT)chars;
    return TRUE;
}

/* a number, or a string kept in upper case. unlike FindResource, "#" and digits stay a string. */
static BOOL key_from_argument(LPCWSTR argument, RES_KEY *key)
{
    memset(key, 0, sizeof(*key));
    if (IS_INTRESOURCE(argument)) {
        key->Id = LOWORD((ULONG_PTR)argument);
        return TRUE;
    }
    return key_from_text(argument, wcslen(argument), TRUE, key);
}

static RES_ITEM *find_item(RES_UPDATE *u, const RES_KEY *type, const RES_KEY *name, USHORT language)
{
    for (ULONG i = 0; i < u->Count; i++) {
        RES_ITEM *item = &u->Items[i];
        if (item->Language == language && !compare_keys(&item->Type, type) && !compare_keys(&item->Name, name))
            return item;
    }
    return nullptr;
}

/* takes the keys. a resource of the same type, name and language is replaced. */
static BOOL set_item(RES_UPDATE *u, RES_KEY *type, RES_KEY *name, USHORT language, ULONG code_page, const void *data,
                     ULONG size)
{
    PUCHAR copy = KbAlloc(size ? size : 1);
    if (!copy) {
        free_key(type);
        free_key(name);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memcpy(copy, data, size);
    RES_ITEM *item = find_item(u, type, name, language);
    if (item) {
        free_key(type);
        free_key(name);
        KbFree(item->Data);
    } else {
        if (u->Count == u->Capacity) {
            ULONG capacity = u->Capacity ? u->Capacity * 2 : 32;
            RES_ITEM *items = KbAlloc(capacity * sizeof(RES_ITEM));
            if (!items) {
                KbFree(copy);
                free_key(type);
                free_key(name);
                KbSetError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            if (u->Count)
                memcpy(items, u->Items, u->Count * sizeof(RES_ITEM));
            KbFree(u->Items);
            u->Items = items;
            u->Capacity = capacity;
        }
        item = &u->Items[u->Count++];
        item->Type = *type;
        item->Name = *name;
        item->Language = language;
    }
    item->CodePage = code_page;
    item->Data = copy;
    item->Size = size;
    return TRUE;
}

static BOOL remove_item(RES_UPDATE *u, const RES_KEY *type, const RES_KEY *name, USHORT language)
{
    RES_ITEM *item = find_item(u, type, name, language);
    if (!item)
        return FALSE;
    free_item(item);
    *item = u->Items[--u->Count];
    return TRUE;
}

/* the update behind a handle, taken off the list when the handle is closing. null for anything else. */
static RES_UPDATE *update_of(HANDLE handle, BOOL closing)
{
    if (!handle)
        return nullptr;
    RES_UPDATE *found = nullptr;
    RtlAcquireSRWLockExclusive(&g_lock);
    for (RES_UPDATE **link = &g_updates; *link; link = &(*link)->Next) {
        if (*link != (RES_UPDATE *)handle)
            continue;
        found = *link;
        if (closing)
            *link = found->Next;
        break;
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    return found;
}

/* ------------------------------------------------------------------ the file */

static DWORD read_file(LPCWSTR path, PUCHAR *data, ULONG *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    LARGE_INTEGER length;
    DWORD error = ERROR_SUCCESS;
    PUCHAR buffer = nullptr;
    if (!GetFileSizeEx(file, &length))
        error = GetLastError();
    else if (length.QuadPart > 0x7FF00000)
        error = ERROR_BAD_EXE_FORMAT;
    else if (!(buffer = KbAlloc((SIZE_T)length.QuadPart + 1)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG done = 0; !error && done < (ULONG)length.QuadPart;) {
        DWORD got = 0;
        if (!ReadFile(file, buffer + done, (ULONG)length.QuadPart - done, &got, nullptr))
            error = GetLastError();
        else if (!got)
            error = ERROR_HANDLE_EOF;
        done += got;
    }
    CloseHandle(file);
    if (error) {
        KbFree(buffer);
        return error;
    }
    *data = buffer;
    *size = (ULONG)length.QuadPart;
    return ERROR_SUCCESS;
}

static BOOL parse_image(PUCHAR base, ULONG size, PE *pe)
{
    memset(pe, 0, sizeof(*pe));
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    if (size < sizeof(*dos) || dos->e_magic != 0x5A4D || dos->e_lfanew < 0)
        return FALSE;
    ULONG at = (ULONG)dos->e_lfanew;
    if (at > size || size - at < 4 + sizeof(IMAGE_FILE_HEADER) + 2 || memcmp(base + at, "PE\0\0", 4))
        return FALSE;
    pe->Base = base;
    pe->Size = size;
    pe->File = (PIMAGE_FILE_HEADER)(base + at + 4);
    pe->Optional = (PUCHAR)(pe->File + 1);
    USHORT magic = *(PUSHORT)pe->Optional;
    if (magic != 0x10B && magic != 0x20B)
        return FALSE;
    pe->Wide = magic == 0x20B;
    ULONG fixed = pe->Wide ? 112 : 96, optional = pe->File->SizeOfOptionalHeader;
    ULONG room = size - (ULONG)(pe->Optional - base);
    if (optional < fixed || optional > room)
        return FALSE;
    pe->DirCount = *(PULONG)(pe->Optional + fixed - 4);
    if (pe->DirCount > (optional - fixed) / sizeof(IMAGE_DATA_DIRECTORY))
        return FALSE;
    pe->Dirs = (PIMAGE_DATA_DIRECTORY)(pe->Optional + fixed);
    pe->Sections = (PIMAGE_SECTION_HEADER)(pe->Optional + optional);
    if ((ULONG64)pe->File->NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > room - optional)
        return FALSE;
    return OPT(pe, OPT_SECTION_ALIGNMENT) && OPT(pe, OPT_FILE_ALIGNMENT);
}

/* where the bytes at an address of the image are in the file, false when the file does not hold all of them. */
static BOOL file_offset(const PE *pe, ULONG rva, ULONG length, ULONG *offset)
{
    for (USHORT i = 0; i < pe->File->NumberOfSections; i++) {
        const IMAGE_SECTION_HEADER *s = &pe->Sections[i];
        if (rva < s->VirtualAddress || rva - s->VirtualAddress > s->SizeOfRawData ||
            length > s->SizeOfRawData - (rva - s->VirtualAddress))
            continue;
        ULONG at = s->PointerToRawData + (rva - s->VirtualAddress);
        if (at > pe->Size || length > pe->Size - at)
            return FALSE;
        *offset = at;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ reading the resources of a file */

typedef struct RES_TREE {
    const PE *Image;
    const UCHAR *Base;
    ULONG Size;
} RES_TREE;

static const IMAGE_RESOURCE_DIRECTORY_ENTRY *tree_entries(const RES_TREE *t, ULONG offset, ULONG *count)
{
    if (offset > t->Size || t->Size - offset < sizeof(IMAGE_RESOURCE_DIRECTORY))
        return nullptr;
    const IMAGE_RESOURCE_DIRECTORY *dir = (const void *)(t->Base + offset);
    *count = (ULONG)dir->NumberOfNamedEntries + dir->NumberOfIdEntries;
    if (*count > (t->Size - offset - sizeof(*dir)) / sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY))
        return nullptr;
    return (const void *)(dir + 1);
}

static BOOL tree_key(const RES_TREE *t, ULONG name, RES_KEY *key)
{
    if (!(name & SUBDIRECTORY)) {
        memset(key, 0, sizeof(*key));
        key->Id = (USHORT)name;
        return TRUE;
    }
    ULONG offset = name & ~SUBDIRECTORY;
    if (offset > t->Size || t->Size - offset < sizeof(USHORT)) {
        KbSetError(ERROR_BAD_EXE_FORMAT);
        return FALSE;
    }
    USHORT chars = *(const USHORT *)(t->Base + offset);
    if ((ULONG)chars * sizeof(WCHAR) > t->Size - offset - sizeof(USHORT)) {
        KbSetError(ERROR_BAD_EXE_FORMAT);
        return FALSE;
    }
    return key_from_text((const WCHAR *)(t->Base + offset + sizeof(USHORT)), chars, FALSE, key);
}

static BOOL collect_resources(RES_UPDATE *u, const PE *pe)
{
    if (pe->DirCount <= IMAGE_DIRECTORY_ENTRY_RESOURCE || !pe->Dirs[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress)
        return TRUE;
    IMAGE_DATA_DIRECTORY where = pe->Dirs[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    ULONG start;
    if (!file_offset(pe, where.VirtualAddress, sizeof(IMAGE_RESOURCE_DIRECTORY), &start)) {
        KbSetError(ERROR_BAD_EXE_FORMAT);
        return FALSE;
    }
    /* offsets of the tree are from its root and stay in the section that holds it. */
    RES_TREE tree = {pe, pe->Base + start, 0};
    for (USHORT i = 0; i < pe->File->NumberOfSections; i++) {
        const IMAGE_SECTION_HEADER *s = &pe->Sections[i];
        if (start >= s->PointerToRawData && start - s->PointerToRawData < s->SizeOfRawData)
            tree.Size = s->SizeOfRawData - (start - s->PointerToRawData);
    }
    if (tree.Size > pe->Size - start)
        tree.Size = pe->Size - start;
    ULONG types, names, languages;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY *type = tree_entries(&tree, 0, &types);
    if (!type) {
        KbSetError(ERROR_BAD_EXE_FORMAT);
        return FALSE;
    }
    for (ULONG i = 0; i < types; i++) {
        if (!(type[i].OffsetToData & SUBDIRECTORY))
            continue;
        const IMAGE_RESOURCE_DIRECTORY_ENTRY *name = tree_entries(&tree, type[i].OffsetToData & ~SUBDIRECTORY, &names);
        for (ULONG j = 0; name && j < names; j++) {
            if (!(name[j].OffsetToData & SUBDIRECTORY))
                continue;
            const IMAGE_RESOURCE_DIRECTORY_ENTRY *language =
                tree_entries(&tree, name[j].OffsetToData & ~SUBDIRECTORY, &languages);
            for (ULONG k = 0; language && k < languages; k++) {
                ULONG at = language[k].OffsetToData, data;
                if ((at & SUBDIRECTORY) || at > tree.Size || tree.Size - at < sizeof(IMAGE_RESOURCE_DATA_ENTRY))
                    continue;
                const IMAGE_RESOURCE_DATA_ENTRY *entry = (const void *)(tree.Base + at);
                if (!file_offset(pe, entry->OffsetToData, entry->Size, &data))
                    continue;
                RES_KEY type_key, name_key;
                if (!tree_key(&tree, type[i].Name, &type_key))
                    return FALSE;
                if (!tree_key(&tree, name[j].Name, &name_key)) {
                    free_key(&type_key);
                    return FALSE;
                }
                if (!set_item(u, &type_key, &name_key, (USHORT)language[k].Name, DEFAULT_CODE_PAGE, pe->Base + data,
                              entry->Size))
                    return FALSE;
            }
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------ making the resource section */

static void sort_items(RES_UPDATE *u)
{
    for (ULONG i = 1; i < u->Count; i++) {
        RES_ITEM item = u->Items[i];
        ULONG j = i;
        for (; j && compare_items(&item, &u->Items[j - 1]) < 0; j--)
            u->Items[j] = u->Items[j - 1];
        u->Items[j] = item;
    }
}

/* windows fills what it leaves between the data, and after the section in the file, with this. */
static void put_padding(PUCHAR at, ULONG length)
{
    static const char pattern[] = "PADDINGXXPADDING";
    for (ULONG i = 0; i < length; i++)
        at[i] = (UCHAR)pattern[i % (sizeof(pattern) - 1)];
}

static ULONG put_key(PUCHAR section, ULONG *strings, const RES_KEY *key)
{
    if (!key->Text)
        return key->Id;
    ULONG at = *strings;
    *(PUSHORT)(section + at) = key->Length;
    memcpy(section + at + sizeof(USHORT), key->Text, key->Length * sizeof(WCHAR));
    *strings += sizeof(USHORT) + key->Length * sizeof(WCHAR);
    return SUBDIRECTORY | at;
}

static void put_directory(PUCHAR section, ULONG at, ULONG named, ULONG ids)
{
    IMAGE_RESOURCE_DIRECTORY *dir = (void *)(section + at);
    dir->MajorVersion = 4;
    dir->NumberOfNamedEntries = (USHORT)named;
    dir->NumberOfIdEntries = (USHORT)ids;
}

/*
 * the section as windows lays it out, byte for byte: the directory of types, those of the names of each type, those
 * of the languages of each name, the data entries, the strings, then the data on four byte boundaries.
 */
static PUCHAR build_section(RES_UPDATE *u, ULONG rva, ULONG *size)
{
    sort_items(u);
    const RES_ITEM *it = u->Items;
    ULONG count = u->Count, types = 0, names = 0, strings = 0;
    ULONG64 data_bytes = 0;
    for (ULONG i = 0; i < count; i++) {
        BOOL new_type = !i || compare_keys(&it[i].Type, &it[i - 1].Type);
        BOOL new_name = new_type || compare_keys(&it[i].Name, &it[i - 1].Name);
        if (new_type) {
            types++;
            if (it[i].Type.Text)
                strings += sizeof(USHORT) + it[i].Type.Length * sizeof(WCHAR);
        }
        if (new_name) {
            names++;
            if (it[i].Name.Text)
                strings += sizeof(USHORT) + it[i].Name.Length * sizeof(WCHAR);
        }
        data_bytes += align_up(it[i].Size, 4);
    }
    const ULONG header = sizeof(IMAGE_RESOURCE_DIRECTORY), slot = sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY);
    ULONG name_dir = header + slot * types;
    ULONG language_dir = name_dir + header * types + slot * names;
    ULONG entries = language_dir + header * names + slot * count;
    ULONG string_at = entries + sizeof(IMAGE_RESOURCE_DATA_ENTRY) * count;
    ULONG data = align_up(string_at + strings, 4);
    if (data + data_bytes > 0x7FF00000) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    *size = data + (ULONG)data_bytes;
    PUCHAR section = KbAllocZero(*size);
    if (!section) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    ULONG type_index = 0, named_types = 0;
    for (ULONG i = 0; i < count;) {
        ULONG type_end = i;
        while (type_end < count && !compare_keys(&it[type_end].Type, &it[i].Type))
            type_end++;
        IMAGE_RESOURCE_DIRECTORY_ENTRY *type = (void *)(section + header + slot * type_index++);
        type->Name = put_key(section, &string_at, &it[i].Type);
        type->OffsetToData = SUBDIRECTORY | name_dir;
        named_types += it[i].Type.Text != nullptr;
        ULONG name_index = 0, named_names = 0;
        for (ULONG k = i; k < type_end;) {
            ULONG name_end = k;
            while (name_end < type_end && !compare_keys(&it[name_end].Name, &it[k].Name))
                name_end++;
            IMAGE_RESOURCE_DIRECTORY_ENTRY *name = (void *)(section + name_dir + header + slot * name_index++);
            name->Name = put_key(section, &string_at, &it[k].Name);
            name->OffsetToData = SUBDIRECTORY | language_dir;
            named_names += it[k].Name.Text != nullptr;
            put_directory(section, language_dir, 0, name_end - k);
            for (ULONG x = k; x < name_end; x++) {
                IMAGE_RESOURCE_DIRECTORY_ENTRY *language = (void *)(section + language_dir + header + slot * (x - k));
                language->Name = it[x].Language;
                language->OffsetToData = entries + sizeof(IMAGE_RESOURCE_DATA_ENTRY) * x;
                IMAGE_RESOURCE_DATA_ENTRY *entry = (void *)(section + language->OffsetToData);
                entry->OffsetToData = rva + data;
                entry->Size = it[x].Size;
                entry->CodePage = it[x].CodePage;
                memcpy(section + data, it[x].Data, it[x].Size);
                put_padding(section + data + it[x].Size, align_up(it[x].Size, 4) - it[x].Size);
                data += align_up(it[x].Size, 4);
            }
            language_dir += header + slot * (name_end - k);
            k = name_end;
        }
        put_directory(section, name_dir, named_names, name_index - named_names);
        name_dir += header + slot * name_index;
        i = type_end;
    }
    put_directory(section, 0, named_types, types - named_types);
    return section;
}

/* ------------------------------------------------------------------ writing the file again */

/*
 * the image with the resources of the update, made the way windows makes it, as its files show. the section that
 * holds the resources gets the new ones in its place, and an image without one gets a .rsrc section, before the
 * relocations when they are the last section. the sections after it follow in the file and in memory, closing up
 * or making room, and the directories that point into them follow too. only the headers and the sections are
 * written: what a file holds after its last section, a symbol table for one, is left out, and the checksum is not
 * made again.
 */
static DWORD rewrite_image(RES_UPDATE *u, PUCHAR old, ULONG size, PUCHAR *result, ULONG *result_size)
{
    PE pe;
    *result = nullptr;
    if (!parse_image(old, size, &pe) || pe.DirCount <= IMAGE_DIRECTORY_ENTRY_RESOURCE)
        return ERROR_BAD_EXE_FORMAT;
    const ULONG section_alignment = OPT(&pe, OPT_SECTION_ALIGNMENT), file_alignment = OPT(&pe, OPT_FILE_ALIGNMENT);
    const USHORT count = pe.File->NumberOfSections;
    const ULONG resource_rva = pe.Dirs[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress;
    const ULONG relocation_rva =
        pe.DirCount > IMAGE_DIRECTORY_ENTRY_BASERELOC ? pe.Dirs[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress : 0;
    LONG own = -1;
    ULONG image_end = 0, raw_end = OPT(&pe, OPT_SIZE_OF_HEADERS), first_raw = ~0u;
    for (USHORT i = 0; i < count; i++) {
        const IMAGE_SECTION_HEADER *s = &pe.Sections[i];
        ULONG span = s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData;
        if (s->VirtualAddress + span > image_end)
            image_end = s->VirtualAddress + span;
        if (s->PointerToRawData && s->SizeOfRawData) {
            if (s->PointerToRawData > size || s->SizeOfRawData > size - s->PointerToRawData)
                return ERROR_BAD_EXE_FORMAT;
            if (s->PointerToRawData + s->SizeOfRawData > raw_end)
                raw_end = s->PointerToRawData + s->SizeOfRawData;
            if (s->PointerToRawData < first_raw)
                first_raw = s->PointerToRawData;
            if (resource_rva && s->VirtualAddress == resource_rva)
                own = i;
        }
    }
    if (raw_end > size)
        raw_end = size;
    /*
     * rva and at are where the section goes, tail is where what follows it in the file starts, and boundary is the
     * address from which the image moves.
     */
    ULONG rva, at, tail, old_raw = 0, boundary;
    USHORT index;
    if (own >= 0) {
        const IMAGE_SECTION_HEADER *s = &pe.Sections[own];
        index = (USHORT)own;
        rva = s->VirtualAddress;
        at = s->PointerToRawData;
        old_raw = s->SizeOfRawData;
        tail = at + old_raw;
        boundary = 0;
        for (USHORT i = 0; i < count; i++)
            if (pe.Sections[i].VirtualAddress > rva && (!boundary || pe.Sections[i].VirtualAddress < boundary))
                boundary = pe.Sections[i].VirtualAddress;
    } else {
        ULONG table_end = (ULONG)((PUCHAR)&pe.Sections[count + 1] - old);
        if (count == 0xFFFF || table_end > OPT(&pe, OPT_SIZE_OF_HEADERS) || table_end > first_raw)
            return ERROR_BAD_EXE_FORMAT;
        const IMAGE_SECTION_HEADER *last = count ? &pe.Sections[count - 1] : nullptr;
        if (last && relocation_rva && last->VirtualAddress == relocation_rva && last->PointerToRawData) {
            index = count - 1;
            rva = last->VirtualAddress;
            at = tail = last->PointerToRawData;
            boundary = rva;
        } else {
            index = count;
            rva = align_up(image_end, section_alignment);
            at = align_up(raw_end, file_alignment);
            tail = raw_end;
            boundary = 0;
        }
    }
    ULONG section_size;
    PUCHAR section = build_section(u, rva, &section_size);
    if (!section)
        return GetLastError();
    ULONG new_raw = align_up(section_size, file_alignment);
    /* how far what follows moves: in memory to the page after the section, in the file by the bytes it changed. */
    LONG moved = boundary ? (LONG)(rva + align_up(section_size, section_alignment)) - (LONG)boundary : 0;
    LONG shift = (LONG)(at + new_raw) - (LONG)tail;
    ULONG out_size = at + new_raw + (raw_end - tail);
    PUCHAR out = KbAllocZero(out_size);
    if (!out) {
        KbFree(section);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(out, old, at < tail ? at : tail);
    memcpy(out + at, section, section_size);
    put_padding(out + at + section_size, new_raw - section_size);
    memcpy(out + at + new_raw, old + tail, raw_end - tail);
    KbFree(section);

    PE now;
    parse_image(out, out_size, &now);
    if (own < 0) {
        /* the table makes room for the new section where it goes. */
        memmove(&now.Sections[index + 1], &now.Sections[index], (count - index) * sizeof(IMAGE_SECTION_HEADER));
        IMAGE_SECTION_HEADER *s = &now.Sections[index];
        memset(s, 0, sizeof(*s));
        memcpy(s->Name, ".rsrc", 5);
        s->VirtualAddress = rva;
        s->PointerToRawData = at;
        s->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
        now.File->NumberOfSections = count + 1;
    }
    now.Sections[index].Misc.VirtualSize = section_size;
    now.Sections[index].SizeOfRawData = new_raw;
    ULONG initialized = 0, end = 0;
    for (USHORT i = 0; i < now.File->NumberOfSections; i++) {
        IMAGE_SECTION_HEADER *s = &now.Sections[i];
        if (i != index) {
            if (s->PointerToRawData && s->PointerToRawData >= tail)
                s->PointerToRawData += shift;
            if (boundary && s->VirtualAddress >= boundary)
                s->VirtualAddress += moved;
        }
        if (s->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)
            initialized += s->SizeOfRawData;
        ULONG span = s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData;
        if (s->VirtualAddress + span > end)
            end = s->VirtualAddress + span;
    }
    for (ULONG i = 0; boundary && moved && i < now.DirCount; i++) {
        /* the certificates are found by their place in the file. */
        if (i != IMAGE_DIRECTORY_ENTRY_SECURITY && now.Dirs[i].VirtualAddress >= boundary)
            now.Dirs[i].VirtualAddress += moved;
    }
    now.Dirs[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress = rva;
    now.Dirs[IMAGE_DIRECTORY_ENTRY_RESOURCE].Size = section_size;
    OPT(&now, OPT_INITIALIZED_DATA) = initialized;
    OPT(&now, OPT_SIZE_OF_IMAGE) = align_up(end, section_alignment);

    ULONG offset;
    if (now.DirCount > IMAGE_DIRECTORY_ENTRY_DEBUG) {
        IMAGE_DATA_DIRECTORY debug = now.Dirs[IMAGE_DIRECTORY_ENTRY_DEBUG];
        if (debug.VirtualAddress && file_offset(&now, debug.VirtualAddress, debug.Size, &offset)) {
            for (ULONG i = 0; i + DEBUG_ENTRY_SIZE <= debug.Size; i += DEBUG_ENTRY_SIZE) {
                PULONG address = (PULONG)(out + offset + i + DEBUG_ADDRESS_OF_RAW_DATA);
                PULONG pointer = (PULONG)(out + offset + i + DEBUG_POINTER_TO_RAW_DATA);
                if (boundary && *address >= boundary)
                    *address += moved;
                if (*pointer >= tail)
                    *pointer += shift;
            }
        }
    }
    *result = out;
    *result_size = out_size;
    return ERROR_SUCCESS;
}

static DWORD write_update(RES_UPDATE *u)
{
    PUCHAR old, image;
    ULONG size, image_size;
    /* like windows, a file someone holds open against writers is ERROR_OPEN_FAILED. */
    HANDLE probe = CreateFileW(u->File, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe == INVALID_HANDLE_VALUE)
        return ERROR_OPEN_FAILED;
    CloseHandle(probe);
    DWORD error = read_file(u->File, &old, &size);
    if (error)
        return error;
    error = rewrite_image(u, old, size, &image, &image_size);
    KbFree(old);
    if (error || !image)
        return error;
    /* the new file is made beside the old one and takes its name once it is whole. */
    WCHAR directory[MAX_PATH], temporary[MAX_PATH];
    SIZE_T chars = wcslen(u->File);
    while (chars && u->File[chars - 1] != '\\' && u->File[chars - 1] != '/' && u->File[chars - 1] != ':')
        chars--;
    if (chars >= MAX_PATH - 14) {
        KbFree(image);
        return ERROR_FILENAME_EXCED_RANGE;
    }
    memcpy(directory, u->File, chars * sizeof(WCHAR));
    directory[chars] = 0;
    if (!chars)
        wcscpy(directory, L".");
    if (!GetTempFileNameW(directory, L"RCX", 0, temporary)) {
        error = GetLastError();
        KbFree(image);
        return error;
    }
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        error = GetLastError();
    for (ULONG done = 0; !error && done < image_size;) {
        DWORD wrote = 0;
        if (!WriteFile(file, image + done, image_size - done, &wrote, nullptr) || !wrote)
            error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        done += wrote;
    }
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    KbFree(image);
    if (!error && !MoveFileExW(temporary, u->File, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        error = GetLastError();
    if (error)
        DeleteFileW(temporary);
    return error;
}

/* ------------------------------------------------------------------ the api */

/* the errors are those of windows 11, read off a real one. */
HANDLE WINAPI BeginUpdateResourceW(LPCWSTR file, BOOL delete_existing)
{
    if (!file || !file[0]) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    /* a file that cannot be written to is refused before anything is read. */
    DWORD attributes = GetFileAttributesW(file);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return nullptr;
    if (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_DIRECTORY)) {
        KbSetError(ERROR_WRITE_PROTECT);
        return nullptr;
    }
    RES_UPDATE *u = KbAllocZero(sizeof(*u));
    SIZE_T bytes = (wcslen(file) + 1) * sizeof(WCHAR);
    if (!u || !(u->File = KbAlloc(bytes))) {
        KbFree(u);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    memcpy(u->File, file, bytes);
    /* the file has to be an image even when its resources are not kept. */
    PUCHAR data = nullptr;
    ULONG size = 0;
    PE pe;
    DWORD error = read_file(file, &data, &size);
    if (!error && !parse_image(data, size, &pe))
        error = ERROR_BAD_EXE_FORMAT;
    if (!error && !delete_existing && !collect_resources(u, &pe))
        error = GetLastError();
    KbFree(data);
    if (error) {
        free_update(u);
        KbSetError(error);
        return nullptr;
    }
    RtlAcquireSRWLockExclusive(&g_lock);
    u->Next = g_updates;
    g_updates = u;
    RtlReleaseSRWLockExclusive(&g_lock);
    KbSetError(ERROR_SUCCESS);
    return (HANDLE)u;
}

BOOL WINAPI UpdateResourceW(HANDLE update, LPCWSTR type, LPCWSTR name, WORD language, LPVOID data, DWORD size)
{
    RES_UPDATE *u = update_of(update, FALSE);
    /* data and a size go together, and neither removes the resource. */
    if (!u || !data != !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (u->Failed) {
        KbSetError(ERROR_INTERNAL_ERROR);
        return FALSE;
    }
    RES_KEY type_key, name_key;
    if (!key_from_argument(type, &type_key))
        return FALSE;
    if (!key_from_argument(name, &name_key)) {
        free_key(&type_key);
        return FALSE;
    }
    if (data) {
        if (!set_item(u, &type_key, &name_key, language, DEFAULT_CODE_PAGE, data, size))
            return FALSE;
        KbSetError(ERROR_SUCCESS);
        return TRUE;
    }
    BOOL removed = remove_item(u, &type_key, &name_key, language);
    free_key(&type_key);
    free_key(&name_key);
    if (!removed) {
        /* removing what is not there fails, and nothing more is taken or written. */
        u->Failed = ERROR_INVALID_PARAMETER;
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KbSetError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI EndUpdateResourceW(HANDLE update, BOOL discard)
{
    if (!update) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RES_UPDATE *u = update_of(update, TRUE);
    if (!u) {
        /* windows frees what it is given: discarding is no failure, writing is ERROR_INVALID_HANDLE. */
        KbSetError(discard ? ERROR_SUCCESS : ERROR_INVALID_HANDLE);
        return discard != 0;
    }
    DWORD error = discard ? ERROR_SUCCESS : u->Failed ? ERROR_INTERNAL_ERROR : write_update(u);
    free_update(u);
    KbSetError(error);
    return !error;
}

HANDLE WINAPI BeginUpdateResourceA(LPCSTR file, BOOL delete_existing)
{
    if (!file) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    PWSTR wide = KbAnsiToWide(file);
    if (!wide) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    HANDLE update = BeginUpdateResourceW(wide, delete_existing);
    DWORD error = GetLastError();
    KbFree(wide);
    if (!update)
        KbSetError(error);
    return update;
}

BOOL WINAPI UpdateResourceA(HANDLE update, LPCSTR type, LPCSTR name, WORD language, LPVOID data, DWORD size)
{
    PWSTR wide_type = IS_INTRESOURCE(type) ? (PWSTR)type : KbAnsiToWideCp(CP_ACP, type);
    PWSTR wide_name = IS_INTRESOURCE(name) ? (PWSTR)name : KbAnsiToWideCp(CP_ACP, name);
    BOOL ok = FALSE;
    DWORD error = ERROR_NOT_ENOUGH_MEMORY;
    if ((wide_type || IS_INTRESOURCE(type)) && (wide_name || IS_INTRESOURCE(name))) {
        ok = UpdateResourceW(update, wide_type, wide_name, language, data, size);
        error = GetLastError();
    }
    if (!IS_INTRESOURCE(type))
        KbFree(wide_type);
    if (!IS_INTRESOURCE(name))
        KbFree(wide_name);
    if (!ok)
        KbSetError(error);
    return ok;
}

BOOL WINAPI EndUpdateResourceA(HANDLE update, BOOL discard)
{
    return EndUpdateResourceW(update, discard);
}
