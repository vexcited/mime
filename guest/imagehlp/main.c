/*
 * imagehlp.dll, the helpers for executable images: the certificates stored after the sections of a signed file, the
 * checksum in the header, and the lookups of directories and sections in a mapped image. The certificates are the
 * records of the security directory, whose address is a file offset and not a relative address. Each record is a
 * length, a revision, a type and the bytes, and starts on an eight byte boundary.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <rtl.h>

#define IMAGEHLP DECLSPEC_EXPORT

#define CERT_SECTION_TYPE_ANY 0xFF

#define CHECKSUM_SUCCESS 0
#define CHECKSUM_OPEN_FAILURE 1
#define CHECKSUM_MAP_FAILURE 2
#define CHECKSUM_MAPVIEW_FAILURE 3
#define CHECKSUM_UNICODE_FAILURE 4

#define SECURITY_ENTRY_OFFSET 32 /* the fifth directory, after four of eight bytes. */

typedef struct _WIN_CERTIFICATE {
    DWORD dwLength;
    WORD wRevision;
    WORD wCertificateType;
    BYTE bCertificate[1];
} WIN_CERTIFICATE, *LPWIN_CERTIFICATE;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* ------------------------------------------------------------------ the security directory of a file */

typedef struct {
    HANDLE File;
    DWORD DirectoryPosition; /* where the directory entry sits in the file. */
    DWORD Offset;            /* the certificates, from the entry. */
    DWORD Size;
    DWORD FileSize;
} SECDIR;

static BOOL read_at(HANDLE file, DWORD position, void *data, DWORD size)
{
    LARGE_INTEGER at = {.QuadPart = position};
    DWORD done;
    return SetFilePointerEx(file, at, nullptr, FILE_BEGIN) && ReadFile(file, data, size, &done, nullptr) && done == size;
}

static BOOL write_at(HANDLE file, DWORD position, const void *data, DWORD size)
{
    LARGE_INTEGER at = {.QuadPart = position};
    DWORD done;
    return SetFilePointerEx(file, at, nullptr, FILE_BEGIN) && WriteFile(file, data, size, &done, nullptr) && done == size;
}

static BOOL open_directory(HANDLE file, SECDIR *dir)
{
    IMAGE_DOS_HEADER dos;
    DWORD signature;
    WORD magic;
    LARGE_INTEGER size;

    if (!read_at(file, 0, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE)
        return FALSE;
    if (!read_at(file, (DWORD)dos.e_lfanew, &signature, 4) || signature != IMAGE_NT_SIGNATURE)
        return FALSE;
    /* the optional header follows the four bytes of signature and the twenty of the file header. */
    DWORD optional = (DWORD)dos.e_lfanew + 4 + 20;
    if (!read_at(file, optional, &magic, 2))
        return FALSE;
    DWORD directories = magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? 112 : magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ? 96 : 0;
    if (!directories || !GetFileSizeEx(file, &size))
        return FALSE;
    dir->File = file;
    dir->DirectoryPosition = optional + directories + SECURITY_ENTRY_OFFSET;
    DWORD entry[2];
    if (!read_at(file, dir->DirectoryPosition, entry, sizeof entry))
        return FALSE;
    dir->Offset = entry[0];
    dir->Size = entry[1];
    dir->FileSize = (DWORD)size.QuadPart;
    if (dir->Size && (dir->Offset > dir->FileSize || dir->Size > dir->FileSize - dir->Offset))
        return FALSE;
    return TRUE;
}

static BOOL write_entry(SECDIR *dir)
{
    DWORD entry[2] = {dir->Offset, dir->Size};
    return write_at(dir->File, dir->DirectoryPosition, entry, sizeof entry);
}

/* the header of a record and where it starts, for the record of the index. */
static BOOL find_record(SECDIR *dir, DWORD index, DWORD *where, WIN_CERTIFICATE *header)
{
    DWORD at = 0;
    for (DWORD i = 0;; i++) {
        if (at + 8 > dir->Size || !read_at(dir->File, dir->Offset + at, header, 8))
            return FALSE;
        if (header->dwLength < 8 || header->dwLength > dir->Size - at)
            return FALSE;
        if (i == index) {
            *where = at;
            return TRUE;
        }
        at += (header->dwLength + 7) & ~7u;
    }
}

IMAGEHLP BOOL WINAPI ImageEnumerateCertificates(HANDLE file, WORD filter, PDWORD count, PDWORD indices, DWORD index_count)
{
    SECDIR dir;
    WIN_CERTIFICATE header;
    DWORD found = 0, at = 0;

    if (!open_directory(file, &dir)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    while (at + 8 <= dir.Size && read_at(file, dir.Offset + at, &header, 8) && header.dwLength >= 8 &&
           header.dwLength <= dir.Size - at) {
        if (filter == CERT_SECTION_TYPE_ANY || header.wCertificateType == filter) {
            if (indices && found < index_count)
                indices[found] = (DWORD)(found);
            found++;
        }
        at += (header.dwLength + 7) & ~7u;
    }
    if (count)
        *count = found;
    return TRUE;
}

IMAGEHLP BOOL WINAPI ImageGetCertificateHeader(HANDLE file, DWORD index, LPWIN_CERTIFICATE out)
{
    SECDIR dir;
    WIN_CERTIFICATE header;
    DWORD where;

    if (!open_directory(file, &dir) || !find_record(&dir, index, &where, &header)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    out->dwLength = header.dwLength;
    out->wRevision = header.wRevision;
    out->wCertificateType = header.wCertificateType;
    return TRUE;
}

IMAGEHLP BOOL WINAPI ImageGetCertificateData(HANDLE file, DWORD index, LPWIN_CERTIFICATE out, PDWORD required)
{
    SECDIR dir;
    WIN_CERTIFICATE header;
    DWORD where;

    if (!open_directory(file, &dir) || !find_record(&dir, index, &where, &header)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (*required < header.dwLength) {
        *required = header.dwLength;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    *required = header.dwLength;
    return read_at(file, dir.Offset + where, out, header.dwLength);
}

IMAGEHLP BOOL WINAPI ImageRemoveCertificate(HANDLE file, DWORD index)
{
    SECDIR dir;
    WIN_CERTIFICATE header;
    DWORD where;

    if (!open_directory(file, &dir) || !find_record(&dir, index, &where, &header)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD length = (header.dwLength + 7) & ~7u;
    if (length > dir.Size - where)
        length = dir.Size - where;
    DWORD tail = dir.Size - where - length;
    if (tail) {
        BYTE *rest = HeapAlloc(GetProcessHeap(), 0, tail);
        if (!rest)
            return FALSE;
        BOOL ok = read_at(file, dir.Offset + where + length, rest, tail) && write_at(file, dir.Offset + where, rest, tail);
        HeapFree(GetProcessHeap(), 0, rest);
        if (!ok)
            return FALSE;
    }
    dir.Size -= length;
    /* a directory that is last in the file gives the room back. */
    if (dir.Offset + dir.Size + length == dir.FileSize) {
        LARGE_INTEGER end = {.QuadPart = (LONGLONG)dir.Offset + dir.Size};
        if (!SetFilePointerEx(file, end, nullptr, FILE_BEGIN) || !SetEndOfFile(file))
            return FALSE;
    }
    if (!dir.Size)
        dir.Offset = 0;
    return write_entry(&dir);
}

IMAGEHLP BOOL WINAPI ImageAddCertificate(HANDLE file, LPWIN_CERTIFICATE certificate, PDWORD index)
{
    SECDIR dir;

    if (!certificate || certificate->dwLength < 8 || !open_directory(file, &dir)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* the new record goes after the directory when it is last in the file, else after the file. */
    DWORD count = 0, at = 0;
    WIN_CERTIFICATE header;
    while (at + 8 <= dir.Size && read_at(file, dir.Offset + at, &header, 8) && header.dwLength >= 8 &&
           header.dwLength <= dir.Size - at) {
        count++;
        at += (header.dwLength + 7) & ~7u;
    }
    DWORD start = dir.Size && dir.Offset + dir.Size == dir.FileSize ? dir.Offset + dir.Size : (dir.FileSize + 7) & ~7u;
    if (dir.Size && start != dir.Offset + dir.Size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD padded = (certificate->dwLength + 7) & ~7u;
    BYTE *record = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, padded);
    if (!record)
        return FALSE;
    memcpy(record, certificate, certificate->dwLength);
    BOOL ok = write_at(file, start, record, padded);
    HeapFree(GetProcessHeap(), 0, record);
    if (!ok)
        return FALSE;
    if (!dir.Size)
        dir.Offset = start;
    dir.Size = start + padded - dir.Offset;
    if (index)
        *index = count;
    return write_entry(&dir);
}

/* ------------------------------------------------------------------ mapped images */

IMAGEHLP PIMAGE_NT_HEADERS WINAPI ImageNtHeader(PVOID base)
{
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    if (!base || base == (PVOID)(LONG_PTR)-1 || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE *)base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

IMAGEHLP PIMAGE_SECTION_HEADER WINAPI ImageRvaToSection(PIMAGE_NT_HEADERS nt, PVOID base, ULONG rva)
{
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        ULONG size = section->Misc.VirtualSize > section->SizeOfRawData ? section->Misc.VirtualSize : section->SizeOfRawData;
        if (rva >= section->VirtualAddress && rva < section->VirtualAddress + size)
            return section;
    }
    return nullptr;
}

IMAGEHLP PVOID WINAPI ImageRvaToVa(PIMAGE_NT_HEADERS nt, PVOID base, ULONG rva, PIMAGE_SECTION_HEADER *last)
{
    PIMAGE_SECTION_HEADER section = last ? *last : nullptr;
    if (!section || rva < section->VirtualAddress || rva >= section->VirtualAddress + section->SizeOfRawData) {
        section = ImageRvaToSection(nt, base, rva);
        if (!section)
            return nullptr;
        if (last)
            *last = section;
    }
    return (BYTE *)base + (rva - section->VirtualAddress) + section->PointerToRawData;
}

IMAGEHLP PVOID WINAPI ImageDirectoryEntryToDataEx(PVOID base, BOOLEAN image, USHORT entry, PULONG size,
                                                  PIMAGE_SECTION_HEADER *found)
{
    /* a module loaded as data has the low bit of its handle set. */
    if ((ULONG_PTR)base & 3) {
        base = (PVOID)((ULONG_PTR)base & ~(ULONG_PTR)3);
        image = FALSE;
    }
    if (found)
        *found = nullptr;
    PIMAGE_NT_HEADERS nt = ImageNtHeader(base);
    if (!nt)
        return nullptr;
    PIMAGE_DATA_DIRECTORY dir = &nt->OptionalHeader.DataDirectory[0];
    WORD magic = nt->OptionalHeader.Magic;
    if (magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC && magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return nullptr;
    if (magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        dir = (PIMAGE_DATA_DIRECTORY)((BYTE *)nt + 24 + 96);
    if (entry >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES || !dir[entry].VirtualAddress || !dir[entry].Size)
        return nullptr;
    if (size)
        *size = dir[entry].Size;
    if (image || entry == IMAGE_DIRECTORY_ENTRY_SECURITY || dir[entry].VirtualAddress < nt->OptionalHeader.SizeOfHeaders)
        return (BYTE *)base + dir[entry].VirtualAddress;
    return ImageRvaToVa(nt, base, dir[entry].VirtualAddress, found);
}

IMAGEHLP PVOID WINAPI ImageDirectoryEntryToData(PVOID base, BOOLEAN image, USHORT entry, PULONG size)
{
    return ImageDirectoryEntryToDataEx(base, image, entry, size, nullptr);
}

/* ------------------------------------------------------------------ checksums */

IMAGEHLP PIMAGE_NT_HEADERS WINAPI CheckSumMappedFile(PVOID base, DWORD length, PDWORD header_sum, PDWORD check_sum)
{
    PIMAGE_NT_HEADERS nt = ImageNtHeader(base);
    const WORD *words = (const WORD *)base;
    const WORD *skip = nullptr;
    ULONG sum = 0;

    if (nt) {
        /* the checksum field sits 64 bytes into the optional header of both formats. */
        skip = (const WORD *)((BYTE *)&nt->OptionalHeader + 64);
        if (header_sum)
            *header_sum = *(const DWORD *)skip;
    }
    for (DWORD i = 0; i < length / 2; i++, words++) {
        if (words == skip || words == skip + 1)
            continue;
        sum += *words;
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    if (length & 1) {
        sum += *(const BYTE *)words;
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    sum = (sum & 0xFFFF) + (sum >> 16);
    if (check_sum)
        *check_sum = (sum & 0xFFFF) + length;
    return nt;
}

IMAGEHLP DWORD WINAPI MapFileAndCheckSumW(PCWSTR name, PDWORD header_sum, PDWORD check_sum)
{
    HANDLE file = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return CHECKSUM_OPEN_FAILURE;
    LARGE_INTEGER size;
    HANDLE mapping = GetFileSizeEx(file, &size) && size.QuadPart ?
                         CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    DWORD result = CHECKSUM_MAP_FAILURE;
    if (mapping) {
        PVOID view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        result = CHECKSUM_MAPVIEW_FAILURE;
        if (view) {
            CheckSumMappedFile(view, (DWORD)size.QuadPart, header_sum, check_sum);
            UnmapViewOfFile(view);
            result = CHECKSUM_SUCCESS;
        }
        CloseHandle(mapping);
    }
    CloseHandle(file);
    return result;
}

IMAGEHLP DWORD WINAPI MapFileAndCheckSumA(PCSTR name, PDWORD header_sum, PDWORD check_sum)
{
    WCHAR wide[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, name, -1, wide, MAX_PATH))
        return CHECKSUM_UNICODE_FAILURE;
    return MapFileAndCheckSumW(wide, header_sum, check_sum);
}
