/*
 * the readers of a PE image's headers and directories: where the headers are, where a directory's data is, in an
 * image mapped as the loader maps it or as a file is mapped as data, and what an export table holds.
 */
#include "ntdll_private.h"

#define MZ_SIGNATURE 0x5a4d
#define PE_SIGNATURE 0x4550
#define RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK 1

/*
 * the headers of the image at base. With the flag, the size is not looked at and the headers are trusted to be
 * mapped; without, nothing past size is read. The headers must lie in the first 256 MiB of the image.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlImageNtHeaderEx(ULONG flags, PVOID base, ULONG64 size, PIMAGE_NT_HEADERS *headers)
{
    if (!headers)
        return STATUS_INVALID_PARAMETER;
    *headers = nullptr;
    if ((flags & ~RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK) || !base || base == (PVOID)(ULONG_PTR)-1)
        return STATUS_INVALID_PARAMETER;
    BOOLEAN check = !(flags & RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK);
    if (check && size < sizeof(IMAGE_DOS_HEADER))
        return STATUS_INVALID_IMAGE_FORMAT;
    const IMAGE_DOS_HEADER *dos = base;
    if (dos->e_magic != MZ_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;
    ULONG offset = (ULONG)dos->e_lfanew;
    if (check && (offset >= size || offset >= 0xffffffe7u || (ULONG64)offset + 24 >= size))
        return STATUS_INVALID_IMAGE_FORMAT;
    if (offset >= 0x10000000)
        return STATUS_INVALID_IMAGE_FORMAT;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PUCHAR)base + offset);
    if ((ULONG_PTR)nt < (ULONG_PTR)base || nt->Signature != PE_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;
    *headers = nt;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT PIMAGE_NT_HEADERS NTAPI RtlImageNtHeader(PVOID base)
{
    PIMAGE_NT_HEADERS nt = nullptr;
    RtlImageNtHeaderEx(RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK, base, 0, &nt);
    return nt;
}

static PIMAGE_SECTION_HEADER first_section(const IMAGE_NT_HEADERS *nt)
{
    return (PIMAGE_SECTION_HEADER)((PUCHAR)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader);
}

/* the section of the headers whose raw data holds the address, by the raw size alone as the loader's tables do. */
NTDLL_EXPORT PIMAGE_SECTION_HEADER NTAPI RtlSectionTableFromVirtualAddress(PIMAGE_NT_HEADERS nt, PVOID base,
                                                                          ULONG address)
{
    PIMAGE_SECTION_HEADER section = first_section(nt);
    for (ULONG i = 0; i < nt->FileHeader.NumberOfSections; i++, section++)
        if (address >= section->VirtualAddress && address < section->VirtualAddress + section->SizeOfRawData)
            return section;
    return nullptr;
}

/* the address of a relative address in an image mapped as a file: through the raw data of its section. */
NTDLL_EXPORT PVOID NTAPI RtlAddressInSectionTable(PIMAGE_NT_HEADERS nt, PVOID base, ULONG address)
{
    PIMAGE_SECTION_HEADER section = RtlSectionTableFromVirtualAddress(nt, base, address);
    if (!section)
        return nullptr;
    return (PUCHAR)base + section->PointerToRawData + (address - section->VirtualAddress);
}

/*
 * the data of a directory and its size. The two low bits of the base say how the image is mapped: bit 0 set
 * for a file mapped as data, whatever mapped_as_image says. A directory the image does not have, or has empty,
 * gives null; the size is set for an empty one.
 */
NTDLL_EXPORT PVOID NTAPI RtlImageDirectoryEntryToData(PVOID image, BOOLEAN mapped_as_image, USHORT directory,
                                                      PULONG size)
{
    PUCHAR base = image;
    if ((ULONG_PTR)image & 3) {
        base = (PUCHAR)((ULONG_PTR)image & ~(ULONG_PTR)3);
        if ((ULONG_PTR)image & 1)
            mapped_as_image = FALSE;
    }
    PIMAGE_NT_HEADERS nt;
    if (RtlImageNtHeaderEx(RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK, base, 0, &nt) < 0)
        return nullptr;
    const IMAGE_DATA_DIRECTORY *entries;
    ULONG count, headers;
    if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        const IMAGE_OPTIONAL_HEADER32 *optional = (const IMAGE_OPTIONAL_HEADER32 *)&nt->OptionalHeader;
        entries = optional->DataDirectory;
        count = optional->NumberOfRvaAndSizes;
        headers = optional->SizeOfHeaders;
    } else if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        const IMAGE_OPTIONAL_HEADER64 *optional = (const IMAGE_OPTIONAL_HEADER64 *)&nt->OptionalHeader;
        entries = optional->DataDirectory;
        count = optional->NumberOfRvaAndSizes;
        headers = optional->SizeOfHeaders;
    } else {
        return nullptr;
    }
    if (directory >= count || !entries[directory].VirtualAddress)
        return nullptr;
    ULONG address = entries[directory].VirtualAddress;
    *size = entries[directory].Size;
    if (mapped_as_image || address < headers)
        return base + address;
    return RtlAddressInSectionTable(nt, base, address);
}

/* an export of a loaded image by name, by a binary search of its sorted name table; a forwarder gives null. */
NTDLL_EXPORT PVOID NTAPI RtlFindExportedRoutineByName(PVOID image, PCSTR name)
{
    ULONG size = 0;
    PUCHAR base = image;
    PIMAGE_EXPORT_DIRECTORY exports = RtlImageDirectoryEntryToData(image, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &size);
    if (!exports || !exports->NumberOfNames)
        return nullptr;
    const ULONG *names = (const ULONG *)(base + exports->AddressOfNames);
    const USHORT *ordinals = (const USHORT *)(base + exports->AddressOfNameOrdinals);
    LONG low = 0, high = (LONG)exports->NumberOfNames - 1;
    while (low <= high) {
        LONG middle = (low + high) / 2;
        int order = strcmp(name, (PCSTR)(base + names[middle]));
        if (order == 0) {
            if (ordinals[middle] >= exports->NumberOfFunctions)
                return nullptr;
            PUCHAR target = base + ((const ULONG *)(base + exports->AddressOfFunctions))[ordinals[middle]];
            if (target > (PUCHAR)exports && target < (PUCHAR)exports + size)
                return nullptr;
            return target;
        }
        if (order < 0) {
            if (!middle)
                break;
            high = middle - 1;
        } else {
            low = middle + 1;
        }
    }
    return nullptr;
}
