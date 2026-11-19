/*
 * the national language services of the kernel, measured on windows 11 26300. the system dll has no tables of its own:
 * it asks for the code pages, the case tables and the normalization forms with NtGetNlsSectionPtr and for the locales
 * with NtInitializeNlsFiles, and each answer is a new read-only view of a file of System32. the languages of the
 * interface come as one block of NtGetMUIRegistryInfo, made of the registry and the licence.
 */
#include "../ntos.h"
#include "../cm/cm.h"

BOOLEAN ExQueryLicenseValue(PCUNICODE_STRING name, PULONG type, const void **data, PULONG size);

/* reads the bytes and writes them back, so a pointer that cannot take the answer fails before anything is done. */
static BOOLEAN probe_write(PVOID address, SIZE_T length)
{
    UCHAR bytes[8];
    return KiIsUserRange(address, length) && NT_SUCCESS(KiCopyFromUser(bytes, address, length)) &&
           NT_SUCCESS(KiCopyToUser(address, bytes, length));
}

static NTSTATUS map_file(PCWSTR name, PVOID *base, SIZE_T *size)
{
    WCHAR path[64];
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"\\SystemRoot\\System32\\%ws", name);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    UNICODE_STRING file_name;
    RtlInitUnicodeString(&file_name, path);
    PFILE_OBJECT file;
    if (!NT_SUCCESS(IopOpenFileObject(&file_name, FILE_READ_DATA | SYNCHRONIZE, FILE_NON_DIRECTORY_FILE, &file)))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    PSECTION section;
    NTSTATUS status = MiCreateDataSection(file, 0, PAGE_READONLY, SEC_COMMIT, &section);
    ObDereferenceObject(file);
    if (!NT_SUCCESS(status))
        return status;
    *base = nullptr;
    *size = 0;
    status = MiMapViewOfSection(section, PsCurrentProcess(), base, 0, size, PAGE_READONLY, 0);
    ObDereferenceObject(section);
    return status;
}

/*
 * type 11 is the code page of the number, type 12 the normalization form of the number and type 14 the case tables,
 * whatever the number. every call maps a view of its own, which the caller unmaps. the third argument must be null.
 */
NTSTATUS NTAPI NtGetNlsSectionPtr(ULONG type, ULONG id, PVOID context, PVOID *pointer, PSIZE_T size)
{
    UCHAR byte;
    if (context && !NT_SUCCESS(KiCopyFromUser(&byte, context, sizeof(byte))))
        return STATUS_ACCESS_VIOLATION;
    if ((pointer && !probe_write(pointer, sizeof(*pointer))) || (size && !probe_write(size, sizeof(*size))))
        return STATUS_ACCESS_VIOLATION;
    if (!pointer)
        return STATUS_INVALID_PARAMETER;
    if (context)
        return STATUS_INVALID_PARAMETER_3;
    WCHAR name[24];
    switch (type) {
    case 11:
        _snwprintf(name, RTL_NUMBER_OF(name) - 1, L"C_%03u.NLS", id);
        name[RTL_NUMBER_OF(name) - 1] = 0;
        break;
    case 12: {
        PCWSTR form = id == 1 ? L"normnfc.nls" : id == 2 ? L"normnfd.nls" : id == 5 ? L"normnfkc.nls" :
                      id == 6 ? L"normnfkd.nls" : id == 13 ? L"normidna.nls" : nullptr;
        if (!form)
            return STATUS_OBJECT_NAME_NOT_FOUND;
        wcscpy(name, form);
        break;
    }
    case 14:
        wcscpy(name, L"l_intl.nls");
        break;
    default:
        return STATUS_INVALID_PARAMETER_1;
    }
    PVOID base;
    SIZE_T view;
    NTSTATUS status = map_file(name, &base, &view);
    if (!NT_SUCCESS(status))
        return status;
    KiCopyToUser(pointer, &base, sizeof(base));
    if (size)
        KiCopyToUser(size, &view, sizeof(view));
    return STATUS_SUCCESS;
}

/* a view of locale.nls and the locale of the system. the third argument is never read nor written. */
NTSTATUS NTAPI NtInitializeNlsFiles(PVOID *base, LCID *locale, PLARGE_INTEGER size)
{
    if (!probe_write(base, sizeof(*base)) || !probe_write(locale, sizeof(*locale)))
        return STATUS_ACCESS_VIOLATION;
    PVOID view;
    SIZE_T length;
    NTSTATUS status = map_file(L"locale.nls", &view, &length);
    if (!NT_SUCCESS(status))
        return status;
    LCID lcid = HalBootInfo->locale_lcid ? HalBootInfo->locale_lcid : 0x0409;
    KiCopyToUser(base, &view, sizeof(view));
    KiCopyToUser(locale, &lcid, sizeof(lcid));
    return STATUS_SUCCESS;
}

/* the language of the interface was chosen when the system was installed. */
NTSTATUS NTAPI NtIsUILanguageComitted(void)
{
    return STATUS_SUCCESS;
}

/* no hot patch is known to a machine without the feature, whatever the class; the length it would return is 0. */
NTSTATUS NTAPI NtManageHotPatch(ULONG klass, PVOID data, ULONG length, PULONG returned)
{
    ULONG none = 0;
    if (!NT_SUCCESS(KiCopyToUser(returned, &none, sizeof(none))))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ the languages of the interface */

/*
 * the block is a header of 0xa8 bytes whose pointers are offsets, then the installed languages, the pool of their
 * names, the list of language configurations and the languages of the licence, each on 8 bytes. the system dll turns
 * the offsets back into pointers and makes the same block from the registry itself when the kernel gives none.
 * measured on windows 11 26300 with scratch/dlls/probe/muilist.c of the research repository: the list has room for a
 * number of languages that is a multiple of 4, the pool for 4 names or as many as it holds, and for 40 characters or
 * one less than it uses, since a name sits one character after the one before.
 */
#define MUI_HEADER 0xA8
#define MUI_LANGUAGE_STEP 4
#define MUI_STRINGS 4
#define MUI_CHARS 40
#define MUI_NAME_CHARS 96

#pragma pack(push, 1)
typedef struct MUI_LANGUAGE {
    USHORT Flags;
    USHORT Reserved;
    USHORT Language;
    USHORT Name; /* its place in the pool. */
    UCHAR Fallbacks[20];
} MUI_LANGUAGE;

typedef struct MUI_LANGUAGE_LIST {
    ULONG Size;
    USHORT Capacity;
    USHORT Count;
    ULONG64 Reserved;
    ULONG64 Entries;
    MUI_LANGUAGE Language[];
} MUI_LANGUAGE_LIST;

/* the places of the names, a word each, then the characters. */
typedef struct MUI_STRING_POOL {
    ULONG Size;
    USHORT Capacity;
    USHORT Count;
    USHORT CharCapacity;
    USHORT CharCount;
    ULONG Reserved;
    ULONG64 Offsets;
    ULONG64 Chars;
    USHORT Offset[];
} MUI_STRING_POOL;

typedef struct MUI_CONFIG_LIST {
    ULONG Size;
    USHORT Count;
    USHORT Capacity;
    ULONG64 Entries;
    UCHAR Entry[12];
} MUI_CONFIG_LIST;

typedef struct MUI_INFO {
    ULONG Flags;
    USHORT InstallLanguage;
    USHORT InstallFallback[2];
    USHORT Reserved0;
    ULONG Generation;
    ULONG64 Reserved1;
    ULONG64 Languages;
    ULONG64 Strings;
    ULONG64 Configurations;
    UCHAR Reserved2[0x74 - 0x30];
    ULONG ExcludedProcesses;
    ULONG LanguagesAllowed;
    ULONG Reserved3;
    ULONG64 Allowed;
    ULONG64 Sku;
    ULONG SkuSize;
    ULONG AllowedSize;
    ULONG64 Disallowed;
    ULONG DisallowedSize;
    ULONG Reserved4;
} MUI_INFO;
#pragma pack(pop)

static_assert(sizeof(MUI_LANGUAGE) == 28);
static_assert(sizeof(MUI_LANGUAGE_LIST) == 0x18);
static_assert(sizeof(MUI_STRING_POOL) == 0x20);
static_assert(sizeof(MUI_CONFIG_LIST) == 0x1C);
static_assert(sizeof(MUI_INFO) == MUI_HEADER);

/* a language of the registry that the licence lets through. */
typedef struct MUI_INSTALLED {
    WCHAR Name[MUI_NAME_CHARS];
    USHORT Type;
    USHORT Lcid;
} MUI_INSTALLED;

static BOOLEAN licence_text(PCWSTR name, PCWSTR *text, PULONG size)
{
    UNICODE_STRING value;
    RtlInitUnicodeString(&value, name);
    ULONG type;
    const void *data;
    if (!ExQueryLicenseValue(&value, &type, &data, size) || type != REG_SZ)
        return FALSE;
    *text = data;
    return TRUE;
}

/* a list of the licence is names between semicolons; windows ships the word EMPTY for a list that holds none. */
static BOOLEAN licence_list(PCWSTR name, PCWSTR *text, PULONG size)
{
    if (licence_text(name, text, size) && _wcsicmp(*text, L"EMPTY"))
        return TRUE;
    *text = nullptr;
    *size = 0;
    return FALSE;
}

static BOOLEAN listed(PCWSTR list, PCWSTR name)
{
    SIZE_T length = wcslen(name);
    for (PCWSTR at = list; *at;) {
        PCWSTR end = at;
        while (*end && *end != L';')
            end++;
        if ((SIZE_T)(end - at) == length && !_wcsnicmp(at, name, length))
            return TRUE;
        at = *end ? end + 1 : end;
    }
    return FALSE;
}

/* the type of a language in the registry as the system accepts it, or 0. only whole languages are taken here. */
static ULONG language_type(ULONG type)
{
    static const ULONG invalid[] = {0x18, 0x6, 0x3, 0x5, 0x404, 0x204, 0x180, 0x1000};
    if (!(type & 0x18) || !(type & 7))
        return 0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(invalid); i++)
        if ((type & invalid[i]) == invalid[i])
            return 0;
    if ((type & 3) && !(type & 0x10))
        return 0;
    type &= 0x419F;
    if ((type & 7) != 1 || (type & 0x18) != 0x10)
        return 0;
    if ((type & 0x180) != 0x80 && (type & 0x180) != 0x100)
        type = (type & ~0x180u) | 0x80;
    return type;
}

#define MUI_LANGUAGES_KEY L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\MUI\\UILanguages"

typedef struct MUI_KEY_NAME {
    LARGE_INTEGER LastWriteTime;
    ULONG TitleIndex;
    ULONG NameLength;
    WCHAR Name[96];
} MUI_KEY_NAME;

NTSTATUS NTAPI ZwOpenKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSTATUS NTAPI ZwEnumerateKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length, PULONG returned);

static ULONG key_dword(PCWSTR key, PCWSTR name)
{
    ULONG type = 0, value = 0, length = sizeof(value);
    if (!NT_SUCCESS(CmQueryValue(key, name, &type, &value, &length)) || type != REG_DWORD || length != sizeof(value))
        return 0;
    return value;
}

/* every language of the registry the licence lets through, in the order of their names as the registry gives them. */
static MUI_INSTALLED *installed_languages(PCWSTR sku, PCWSTR allowed, PCWSTR disallowed, ULONG *count)
{
    MUI_INSTALLED *found = nullptr;
    ULONG room = 0;
    *count = 0;
    UNICODE_STRING path;
    RtlInitUnicodeString(&path, MUI_LANGUAGES_KEY);
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    HANDLE root;
    if (!NT_SUCCESS(ZwOpenKey(&root, 0x20019, &attributes)))
        return nullptr;
    for (ULONG index = 0;; index++) {
        MUI_KEY_NAME entry;
        ULONG returned;
        /* class 0 is the name of the key. */
        NTSTATUS status = ZwEnumerateKey(root, index, 0, &entry, sizeof(entry) - sizeof(WCHAR), &returned);
        if (status == STATUS_NO_MORE_ENTRIES)
            break;
        if (!NT_SUCCESS(status))
            continue;
        PWSTR name = entry.Name;
        name[entry.NameLength / sizeof(WCHAR)] = 0;
        WCHAR key[RTL_NUMBER_OF(MUI_LANGUAGES_KEY) + RTL_NUMBER_OF(entry.Name) + 1];
        _snwprintf(key, RTL_NUMBER_OF(key) - 1, L"%ws\\%ws", MUI_LANGUAGES_KEY, name);
        key[RTL_NUMBER_OF(key) - 1] = 0;
        ULONG type = language_type(key_dword(key, L"Type"));
        ULONG lcid = key_dword(key, L"LCID");
        if (!type || !lcid || (sku && !listed(sku, name)) ||
            (allowed ? !listed(allowed, name) : disallowed && listed(disallowed, name)))
            continue;
        if (*count == room) {
            ULONG more = room ? room * 2 : 8;
            MUI_INSTALLED *grown = ExAllocatePool2(POOL_FLAG_NON_PAGED, more * sizeof(*grown), KI_TAG('M', 'u', 'i', 'L'));
            if (!grown)
                break;
            if (found) {
                memcpy(grown, found, *count * sizeof(*found));
                ExFreePool(found);
            }
            found = grown;
            room = more;
        }
        /* the names go up, whatever order the keys come in. */
        ULONG at = *count;
        while (at && _wcsicmp(found[at - 1].Name, name) > 0) {
            found[at] = found[at - 1];
            at--;
        }
        wcscpy(found[at].Name, name);
        found[at].Type = (USHORT)type;
        found[at].Lcid = (USHORT)lcid;
        ++*count;
    }
    ZwClose(root);
    return found;
}

static ULONG put_list(UCHAR *block, ULONG at, PCWSTR text, ULONG size, ULONG64 *offset, ULONG *length)
{
    if (!text)
        return at;
    /* the names end with a null each, and the block keeps four bytes more than the value. */
    WCHAR *out = (WCHAR *)(block + at);
    memcpy(out, text, size);
    for (ULONG i = 0; i < size / sizeof(WCHAR); i++)
        if (out[i] == L';')
            out[i] = 0;
    *offset = at;
    *length = size + 4;
    return (at + size + 4 + 7) & ~7u;
}

static NTSTATUS build_mui_info(UCHAR **out, ULONG *out_size)
{
    PCWSTR sku = nullptr, allowed = nullptr, disallowed = nullptr;
    ULONG sku_size = 0, allowed_size = 0, disallowed_size = 0;
    licence_list(L"Kernel-MUI-Language-SKU", &sku, &sku_size);
    licence_list(L"Kernel-MUI-Language-Allowed", &allowed, &allowed_size);
    /* the forbidden languages only count without a list of the allowed ones. */
    if (!allowed)
        licence_list(L"Kernel-MUI-Language-Disallowed", &disallowed, &disallowed_size);
    ULONG count;
    MUI_INSTALLED *installed = installed_languages(sku, allowed, disallowed, &count);
    /* the first name of the pool is the empty one, and each name starts one character after the null of the last. */
    ULONG names = count + 1, used = 1;
    for (ULONG i = 0; i < count; i++)
        used += (ULONG)wcslen(installed[i].Name) + 2;
    ULONG capacity = count ? (count + MUI_LANGUAGE_STEP - 1) / MUI_LANGUAGE_STEP * MUI_LANGUAGE_STEP : MUI_LANGUAGE_STEP;
    ULONG name_capacity = names > MUI_STRINGS ? names : MUI_STRINGS;
    ULONG char_capacity = used - 1 > MUI_CHARS ? used - 1 : MUI_CHARS;
    ULONG list_size = sizeof(MUI_LANGUAGE_LIST) + capacity * sizeof(MUI_LANGUAGE);
    ULONG pool_size = sizeof(MUI_STRING_POOL) + name_capacity * sizeof(USHORT) + char_capacity * sizeof(WCHAR);
    ULONG pool_at = MUI_HEADER + list_size;
    ULONG configurations_at = (pool_at + pool_size + 7) & ~7u;
    ULONG lists_at = (configurations_at + (ULONG)sizeof(MUI_CONFIG_LIST) + 7) & ~7u;
    ULONG room = lists_at + sku_size + allowed_size + disallowed_size + 3 * 12;
    UCHAR *block = ExAllocatePool2(POOL_FLAG_NON_PAGED, room, KI_TAG('M', 'u', 'i', 'R'));
    if (!block) {
        if (installed)
            ExFreePool(installed);
        return STATUS_NO_MEMORY;
    }
    memset(block, 0, room);
    MUI_INFO *info = (MUI_INFO *)block;
    MUI_LANGUAGE_LIST *languages = (MUI_LANGUAGE_LIST *)(block + MUI_HEADER);
    MUI_STRING_POOL *pool = (MUI_STRING_POOL *)(block + pool_at);
    MUI_CONFIG_LIST *configurations = (MUI_CONFIG_LIST *)(block + configurations_at);
    PWCHAR chars = (PWCHAR)&pool->Offset[name_capacity];
    info->Flags = 0x400;
    info->InstallLanguage = 0x0409;
    info->Generation = SharedUserData->LangGenerationCount;
    info->Languages = MUI_HEADER;
    info->Strings = pool_at;
    info->Configurations = configurations_at;
    languages->Size = list_size;
    languages->Capacity = (USHORT)capacity;
    languages->Count = (USHORT)count;
    pool->Size = pool_size;
    pool->Capacity = (USHORT)name_capacity;
    pool->Count = (USHORT)names;
    pool->CharCapacity = (USHORT)char_capacity;
    pool->CharCount = (USHORT)used;
    configurations->Size = sizeof(*configurations);
    configurations->Capacity = 1;
    ULONG at = 1;
    for (ULONG i = 0; i < count; i++) {
        SIZE_T length = wcslen(installed[i].Name);
        /* the last gap is counted and not kept, so the last null is the last character the pool has room for. */
        memcpy(&chars[at], installed[i].Name, length * sizeof(WCHAR));
        pool->Offset[i + 1] = (USHORT)at;
        at += (ULONG)length + 2;
        languages->Language[i].Flags = (USHORT)(installed[i].Type | 0x820);
        languages->Language[i].Language = installed[i].Lcid;
        languages->Language[i].Name = (USHORT)(i + 1);
    }
    if (installed)
        ExFreePool(installed);

    UNICODE_STRING number;
    RtlInitUnicodeString(&number, L"Kernel-MUI-Number-Allowed");
    ULONG type, size;
    const void *data;
    info->LanguagesAllowed = ExQueryLicenseValue(&number, &type, &data, &size) && type == REG_DWORD ? *(const ULONG *)data : ~0u;
    at = lists_at;
    at = put_list(block, at, allowed, allowed_size, &info->Allowed, &info->AllowedSize);
    at = put_list(block, at, sku, sku_size, &info->Sku, &info->SkuSize);
    at = put_list(block, at, disallowed, disallowed_size, &info->Disallowed, &info->DisallowedSize);
    *out = block;
    *out_size = at;
    return STATUS_SUCCESS;
}

/*
 * flags 0 and 1 copy the block, 2 and 8 ask the kernel to read the registry again, which it does here on every call,
 * 8 without the copy also counts a change of the languages, in the block and in the shared data, and other bits are
 * refused. a query without a buffer answers the size, with a short one the size and
 * STATUS_BUFFER_TOO_SMALL, and a buffer larger than the block is cleared to its end.
 */
NTSTATUS NTAPI NtGetMUIRegistryInfo(ULONG flags, PULONG size, PVOID data)
{
    if (flags & ~0xBu)
        return STATUS_INVALID_PARAMETER;
    ULONG given = 0;
    if (size && !NT_SUCCESS(KiCopyFromUser(&given, size, sizeof(given))))
        return STATUS_ACCESS_VIOLATION;
    if (flags && !(flags & 1)) {
        UCHAR byte;
        if (data && !NT_SUCCESS(KiCopyFromUser(&byte, data, sizeof(byte))))
            return STATUS_ACCESS_VIOLATION;
        if (flags & 8)
            __atomic_add_fetch(&SharedUserData->LangGenerationCount, 1, __ATOMIC_RELAXED);
        return STATUS_SUCCESS;
    }
    if (!size || !given != !data)
        return STATUS_INVALID_PARAMETER;
    UCHAR *block;
    ULONG needed;
    NTSTATUS status = build_mui_info(&block, &needed);
    if (!NT_SUCCESS(status))
        return status;
    status = KiCopyToUser(size, &needed, sizeof(needed));
    if (NT_SUCCESS(status) && given) {
        if (given < needed) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            static const UCHAR zeros[256];
            /* the whole buffer must take the answer before any of it is written. */
            for (ULONG at = 0; NT_SUCCESS(status) && at < given; at += PAGE_SIZE)
                if (!probe_write((UCHAR *)data + at, 1))
                    status = STATUS_ACCESS_VIOLATION;
            if (NT_SUCCESS(status) && !probe_write((UCHAR *)data + given - 1, 1))
                status = STATUS_ACCESS_VIOLATION;
            if (NT_SUCCESS(status))
                status = KiCopyToUser(data, block, needed);
            for (ULONG at = needed; NT_SUCCESS(status) && at < given; at += sizeof(zeros))
                status = KiCopyToUser((UCHAR *)data + at, zeros, given - at < sizeof(zeros) ? given - at : sizeof(zeros));
        }
    }
    ExFreePool(block);
    return NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL ? status : STATUS_ACCESS_VIOLATION;
}
