#include "kshim.h"

#define RESOURCE_DIRECTORY 2
#define RT_MANIFEST_ID 24

/* the bytes of an image at a relative address, or nothing when they are not all inside it. */
static const void *at_rva(const SXS_IMAGE *image, ULONG rva, ULONG length)
{
    if (!image->File)
        return rva <= image->Size && length <= image->Size - rva ? image->Base + rva : nullptr;
    const IMAGE_DOS_HEADER *dos = (const void *)image->Base;
    const IMAGE_NT_HEADERS *nt = (const void *)(image->Base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER *sections =
        (const void *)((const UCHAR *)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader);
    if ((SIZE_T)((const UCHAR *)(sections + nt->FileHeader.NumberOfSections) - image->Base) > image->Size)
        return nullptr;
    for (ULONG i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        const IMAGE_SECTION_HEADER *s = &sections[i];
        if (rva < s->VirtualAddress || rva - s->VirtualAddress >= s->SizeOfRawData)
            continue;
        ULONG64 offset = (ULONG64)s->PointerToRawData + (rva - s->VirtualAddress);
        if (length > s->SizeOfRawData - (rva - s->VirtualAddress) || offset + length > image->Size)
            return nullptr;
        return image->Base + offset;
    }
    return nullptr;
}

/* the resource directory of an image of either width, and its size. */
static const UCHAR *resource_root(const SXS_IMAGE *image, ULONG *size)
{
    if (image->Size < sizeof(IMAGE_DOS_HEADER))
        return nullptr;
    const IMAGE_DOS_HEADER *dos = (const void *)image->Base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        (SIZE_T)dos->e_lfanew > image->Size - sizeof(IMAGE_DOS_HEADER) ||
        image->Size - (SIZE_T)dos->e_lfanew < sizeof(IMAGE_NT_HEADERS))
        return nullptr;
    const IMAGE_NT_HEADERS *nt = (const void *)(image->Base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;
    const UCHAR *optional = (const UCHAR *)&nt->OptionalHeader;
    BOOLEAN wide = *(const USHORT *)optional == 0x20B;
    if (!wide && *(const USHORT *)optional != 0x10B)
        return nullptr;
    /* the count of directories, then the directories of 8 bytes each. */
    const ULONG *count = (const void *)(optional + (wide ? 0x6C : 0x5C));
    if (*count <= RESOURCE_DIRECTORY)
        return nullptr;
    const ULONG *entry = count + 1 + RESOURCE_DIRECTORY * 2;
    if (!entry[0] || entry[1] < sizeof(IMAGE_RESOURCE_DIRECTORY))
        return nullptr;
    *size = entry[1];
    return at_rva(image, entry[0], entry[1]);
}

static BOOLEAN name_matches(const UCHAR *root, ULONG size, const IMAGE_RESOURCE_DIRECTORY_ENTRY *e, ULONG_PTR name)
{
    if (name >> 16 == 0)
        return !(e->Name & 0x80000000) && e->Name == (ULONG)name;
    if (!(e->Name & 0x80000000))
        return FALSE;
    ULONG offset = e->Name & 0x7FFFFFFF;
    if (offset > size - sizeof(USHORT))
        return FALSE;
    const IMAGE_RESOURCE_DIR_STRING_U *text = (const void *)(root + offset);
    PCWSTR want = (PCWSTR)name;
    if (text->Length > (size - offset - sizeof(USHORT)) / sizeof(WCHAR) || wcslen(want) != text->Length)
        return FALSE;
    return !_wcsnicmp(want, text->NameString, text->Length);
}

/* the directory an entry of a directory opens, by the name or the number of the entry. */
static const IMAGE_RESOURCE_DIRECTORY *subdirectory(const UCHAR *root, ULONG size, const IMAGE_RESOURCE_DIRECTORY *dir,
                                                    ULONG_PTR name)
{
    ULONG count = (ULONG)dir->NumberOfNamedEntries + dir->NumberOfIdEntries;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY *entries = (const void *)(dir + 1);
    if ((SIZE_T)((const UCHAR *)(entries + count) - root) > size)
        return nullptr;
    for (ULONG i = 0; i < count; i++) {
        if (!name_matches(root, size, &entries[i], name) || !(entries[i].OffsetToData & 0x80000000))
            continue;
        ULONG offset = entries[i].OffsetToData & 0x7FFFFFFF;
        if (offset > size - sizeof(IMAGE_RESOURCE_DIRECTORY))
            return nullptr;
        return (const void *)(root + offset);
    }
    return nullptr;
}

static NTSTATUS find_directory(const SXS_IMAGE *image, const LDR_RESOURCE_INFO *info, ULONG level, const UCHAR **root,
                               ULONG *size, const IMAGE_RESOURCE_DIRECTORY **dir)
{
    *root = resource_root(image, size);
    if (!*root)
        return STATUS_RESOURCE_DATA_NOT_FOUND;
    *dir = (const void *)*root;
    if (level == 0)
        return STATUS_SUCCESS;
    *dir = subdirectory(*root, *size, *dir, info->Type);
    if (!*dir)
        return STATUS_RESOURCE_TYPE_NOT_FOUND;
    if (level == 1)
        return STATUS_SUCCESS;
    *dir = subdirectory(*root, *size, *dir, info->Name);
    return *dir ? STATUS_SUCCESS : STATUS_RESOURCE_NAME_NOT_FOUND;
}

/* the manifest of an image under a name, in its first language, as the generator takes it. */
NTSTATUS SxsKFindManifest(const SXS_IMAGE *image, ULONG_PTR name, const void **data, SIZE_T *size)
{
    LDR_RESOURCE_INFO info = {RT_MANIFEST_ID, name, 0};
    const UCHAR *root;
    ULONG root_size;
    const IMAGE_RESOURCE_DIRECTORY *dir;
    NTSTATUS status = find_directory(image, &info, 2, &root, &root_size, &dir);
    if (!NT_SUCCESS(status))
        return status;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY *entry = (const void *)(dir + 1);
    if (!((ULONG)dir->NumberOfNamedEntries + dir->NumberOfIdEntries) ||
        (SIZE_T)((const UCHAR *)(entry + 1) - root) > root_size || (entry->OffsetToData & 0x80000000) ||
        entry->OffsetToData > root_size - sizeof(IMAGE_RESOURCE_DATA_ENTRY))
        return STATUS_RESOURCE_LANG_NOT_FOUND;
    const IMAGE_RESOURCE_DATA_ENTRY *found = (const void *)(root + entry->OffsetToData);
    *data = at_rva(image, found->OffsetToData, found->Size);
    *size = found->Size;
    return *data ? STATUS_SUCCESS : STATUS_RESOURCE_DATA_NOT_FOUND;
}

/* the generator names a file it read by its address with the low bit set, as the loader names a data file. */
static BOOLEAN image_of(PVOID module, SXS_IMAGE *image)
{
    if (!((ULONG_PTR)module & 1))
        return FALSE;
    image->Base = (const UCHAR *)((ULONG_PTR)module & ~(ULONG_PTR)1);
    image->Size = SxsKFileSize(image->Base);
    image->File = TRUE;
    return image->Size != 0;
}

NTSTATUS NTAPI SxsKFindResourceDirectory(PVOID module, PLDR_RESOURCE_INFO info, ULONG level, IMAGE_RESOURCE_DIRECTORY **dir)
{
    SXS_IMAGE image;
    const UCHAR *root;
    ULONG size;
    if (!image_of(module, &image))
        return STATUS_RESOURCE_DATA_NOT_FOUND;
    return find_directory(&image, info, level, &root, &size, (const IMAGE_RESOURCE_DIRECTORY **)dir);
}

NTSTATUS NTAPI SxsKAccessResource(PVOID module, IMAGE_RESOURCE_DATA_ENTRY *entry, PVOID *data, PULONG size)
{
    SXS_IMAGE image;
    if (!image_of(module, &image) || (SIZE_T)((const UCHAR *)entry - image.Base) > image.Size - sizeof(*entry))
        return STATUS_RESOURCE_DATA_NOT_FOUND;
    *data = (PVOID)at_rva(&image, entry->OffsetToData, entry->Size);
    *size = entry->Size;
    return *data ? STATUS_SUCCESS : STATUS_RESOURCE_DATA_NOT_FOUND;
}

RTL_PATH_TYPE NTAPI SxsKPathType(PCWSTR path)
{
    BOOLEAN slash0 = path[0] == '\\' || path[0] == '/';
    if (slash0 && (path[1] == '\\' || path[1] == '/')) {
        if (path[2] != '.' && path[2] != '?')
            return RtlPathTypeUncAbsolute;
        if (path[3] == '\\' || path[3] == '/')
            return RtlPathTypeLocalDevice;
        return path[3] ? RtlPathTypeUncAbsolute : RtlPathTypeRootLocalDevice;
    }
    if (slash0)
        return RtlPathTypeRooted;
    if (path[0] && path[1] == ':')
        return path[2] == '\\' || path[2] == '/' ? RtlPathTypeDriveAbsolute : RtlPathTypeDriveRelative;
    return RtlPathTypeRelative;
}

/* a manifest in a single byte encoding is read as windows-1252, whose first 256 characters but 32 are unicode's. */
NTSTATUS NTAPI SxsKMultiByteToUnicode(PWSTR dst, ULONG dst_bytes, PULONG written, const CHAR *src, ULONG src_bytes)
{
    static const WCHAR high[32] = {0x20AC, 0x81,   0x201A, 0x192,  0x201E, 0x2026, 0x2020, 0x2021, 0x2C6,  0x2030, 0x160,
                                   0x2039, 0x152,  0x8D,   0x17D,  0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                   0x2013, 0x2014, 0x2DC,  0x2122, 0x161,  0x203A, 0x153,  0x9D,   0x17E,  0x178};
    ULONG n = dst_bytes / sizeof(WCHAR) < src_bytes ? dst_bytes / sizeof(WCHAR) : src_bytes;
    for (ULONG i = 0; i < n; i++) {
        UCHAR c = (UCHAR)src[i];
        dst[i] = c >= 0x80 && c < 0xA0 ? high[c - 0x80] : c;
    }
    if (written)
        *written = n * sizeof(WCHAR);
    return STATUS_SUCCESS;
}
