/*
 * imagehlp.dll: certificates stored after the sections of a signed file, the header checksum, and directory and
 * section lookups in mapped images. the security directory address is a file offset, not an rva. each certificate
 * record is a length, revision, type and data, aligned to 8 bytes.
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

#define SECURITY_ENTRY_OFFSET 32 /* the fifth data directory, after four 8 byte entries. */

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

/* ------------------------------------------------------------------ security directory */

typedef struct {
    HANDLE File;
    DWORD DirectoryPosition; /* file offset of the directory entry. */
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
    /* the optional header follows the 4 byte signature and the 20 byte file header. */
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

/* the file offset and header of record number index. */
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
    /* a directory at the end of the file gives its room back. */
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
    /* the new record goes after the directory when it ends the file, else at the end of the file. */
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
    WORD magic = nt->OptionalHeader.Magic;
    if (magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC && magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return nullptr;
    /* the directories follow the optional header of the file's own kind, whatever the kind of this build. */
    PIMAGE_DATA_DIRECTORY dir =
        (PIMAGE_DATA_DIRECTORY)((BYTE *)nt + 24 + (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? 112 : 96));
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

/* ------------------------------------------------------------------ mapping an image by name */

typedef struct _LOADED_IMAGE {
    PSTR ModuleName;
    HANDLE hFile;
    PUCHAR MappedAddress;
    PIMAGE_NT_HEADERS FileHeader;
    PIMAGE_SECTION_HEADER LastRvaSection;
    ULONG NumberOfSections;
    PIMAGE_SECTION_HEADER Sections;
    ULONG Characteristics;
    BOOLEAN fSystemImage;
    BOOLEAN fDOSImage;
    BOOLEAN fReadOnly;
    UCHAR Version;
    LIST_ENTRY Links;
    ULONG SizeOfImage;
} LOADED_IMAGE, *PLOADED_IMAGE;

IMAGEHLP BOOL WINAPI UnMapAndLoad(PLOADED_IMAGE image);

/* the headers have to be whole inside the file, or the view is dropped. */
static BOOL locate_headers(LOADED_IMAGE *image)
{
    BYTE *base = image->MappedAddress;
    BYTE *end = base + image->SizeOfImage;
    BYTE *nt = nullptr;
    if (image->SizeOfImage >= 0x40 && *(WORD *)base == IMAGE_DOS_SIGNATURE) {
        DWORD offset = *(DWORD *)(base + 0x3C);
        if (!offset) {
            image->fDOSImage = TRUE;
        } else if (offset >= 0x40 && offset <= image->SizeOfImage && image->SizeOfImage - offset >= 0x108) {
            nt = base + offset;
        }
    } else if (image->SizeOfImage >= 0x40 && *(WORD *)base == (WORD)IMAGE_NT_SIGNATURE) {
        nt = base;
    }
    if (nt && nt + 264 <= end) {
        PIMAGE_NT_HEADERS headers = (PIMAGE_NT_HEADERS)nt;
        image->FileHeader = headers;
        if (headers->Signature != IMAGE_NT_SIGNATURE) {
            WORD kind = (WORD)headers->Signature;
            if (kind == 0x454C || kind == 0x454E)
                image->fDOSImage = TRUE;
            UnmapViewOfFile(base);
            SetLastError(ERROR_INVALID_ACCESS);
            return FALSE;
        } else {
            image->fDOSImage = FALSE;
            BYTE *optional = nt + 24;
            BYTE *sections = optional + headers->FileHeader.SizeOfOptionalHeader;
            WORD magic = headers->OptionalHeader.Magic;
            BOOL fits = sections <= end;
            if (fits && magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
                DWORD count = *(DWORD *)(optional + 92);
                image->fSystemImage = *(DWORD *)(optional + 28) >= 0x80000000;
                fits = optional + 92 + 4 <= end && optional + 96 + 8 * (ULONG_PTR)count <= end &&
                       !(optional[2] < 3 && optional[3] < 5);
            } else if (fits) {
                image->fSystemImage = FALSE;
                if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                    DWORD count = *(DWORD *)(optional + 108);
                    fits = optional + 112 + 8 * (ULONG_PTR)count <= end;
                }
            }
            if (fits && headers->FileHeader.NumberOfSections) {
                image->Sections = image->LastRvaSection = (PIMAGE_SECTION_HEADER)sections;
                image->Links.Flink = image->Links.Blink = &image->Links;
                image->Characteristics = headers->FileHeader.Characteristics;
                image->NumberOfSections = headers->FileHeader.NumberOfSections;
                if ((BYTE *)(image->Sections + image->NumberOfSections) <= end)
                    return TRUE;
            }
        }
    }
    UnmapViewOfFile(base);
    SetLastError(ERROR_INVALID_ACCESS);
    return FALSE;
}

IMAGEHLP BOOL WINAPI MapAndLoad(PCSTR name, PCSTR path, PLOADED_IMAGE image, BOOL dll, BOOL read_only)
{
    if (!image) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(image, 0, sizeof(*image));
    image->hFile = INVALID_HANDLE_VALUE;
    char found[MAX_PATH];
    PCSTR file = name;
    HANDLE handle;
    /* the name is tried as it is, and then searched for with the extension of the kind of image. */
    for (int attempt = 0;; attempt++) {
        handle = CreateFileA(file, read_only ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle != INVALID_HANDLE_VALUE)
            break;
        WCHAR wide_path[MAX_PATH], wide_name[MAX_PATH], located[MAX_PATH];
        BOOL have_path = path && MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, path, -1, wide_path, MAX_PATH) > 0;
        if (attempt || MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, name, -1, wide_name, MAX_PATH) <= 0)
            return FALSE;
        DWORD length = SearchPathW(have_path ? wide_path : nullptr, wide_name, dll ? L".dll" : L".exe", MAX_PATH, located,
                                   nullptr);
        if (length - 1 > MAX_PATH - 2 ||
            WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, located, -1, found, MAX_PATH, nullptr, nullptr) <= 0)
            return FALSE;
        file = found;
    }
    image->fReadOnly = read_only != 0;
    image->Version = 1;
    DWORD size = GetFileSize(handle, nullptr);
    image->SizeOfImage = size;
    HANDLE mapping = size != INVALID_FILE_SIZE ?
                         CreateFileMappingW(handle, nullptr, read_only ? PAGE_READONLY : PAGE_READWRITE, 0, 0, nullptr) :
                         nullptr;
    if (mapping) {
        image->MappedAddress = MapViewOfFile(mapping, read_only ? FILE_MAP_READ : FILE_MAP_WRITE, 0, 0, 0);
        CloseHandle(mapping);
    }
    if (!image->MappedAddress || !locate_headers(image)) {
        CloseHandle(handle);
        memset(image, 0, sizeof(*image));
        image->hFile = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    image->hFile = handle;
    size_t bytes = strlen(file) + 16;
    image->ModuleName = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!image->ModuleName) {
        UnMapAndLoad(image);
        memset(image, 0, sizeof(*image));
        image->hFile = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    lstrcpyA(image->ModuleName, file);
    return TRUE;
}

/* a writable image gets its checksum refreshed, and the file is cut to the size it was mapped with. */
IMAGEHLP BOOL WINAPI UnMapAndLoad(PLOADED_IMAGE image)
{
    if (!image) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (image->MappedAddress) {
        DWORD header_sum, check_sum;
        if (!image->fReadOnly && CheckSumMappedFile(image->MappedAddress, image->SizeOfImage, &header_sum, &check_sum)) {
            WORD magic = image->FileHeader->OptionalHeader.Magic;
            if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC || magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                image->FileHeader->OptionalHeader.CheckSum = check_sum;
            FlushViewOfFile(image->MappedAddress, image->SizeOfImage);
            UnmapViewOfFile(image->MappedAddress);
            if (image->SizeOfImage != GetFileSize(image->hFile, nullptr)) {
                LONG high = 0;
                SetFilePointer(image->hFile, (LONG)image->SizeOfImage, &high, FILE_BEGIN);
                SetEndOfFile(image->hFile);
            }
        } else {
            UnmapViewOfFile(image->MappedAddress);
        }
    }
    if (image->hFile && image->hFile != INVALID_HANDLE_VALUE)
        CloseHandle(image->hFile);
    image->hFile = INVALID_HANDLE_VALUE;
    if (image->ModuleName)
        HeapFree(GetProcessHeap(), 0, image->ModuleName);
    return TRUE;
}

/* ------------------------------------------------------------------ digest */

#define CERT_PE_IMAGE_DIGEST_DEBUG_INFO 0x01
#define CERT_PE_IMAGE_DIGEST_RESOURCES 0x02
#define CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO 0x04

typedef BOOL(WINAPI *DIGEST_FUNCTION)(PVOID handle, PBYTE data, DWORD length);

/* byte ranges of the mapped file that the digest leaves out, by address. */
typedef struct {
    const BYTE *Address;
    DWORD Size;
} DIGEST_SKIP;

typedef struct {
    DIGEST_FUNCTION Digest;
    PVOID Handle;
    DIGEST_SKIP *Skips;
    DWORD Count, Capacity;
    PIMAGE_NT_HEADERS Nt;
    BYTE *Base;
} DIGEST;

/* the range goes in before the first one that starts at the same address or later. */
static void digest_skip(DIGEST *d, const BYTE *address, DWORD size)
{
    if (!address)
        return;
    if (d->Count == d->Capacity) {
        DWORD capacity = d->Capacity ? d->Capacity * 2 : 32;
        DIGEST_SKIP *grown = d->Skips ? HeapReAlloc(GetProcessHeap(), 0, d->Skips, capacity * sizeof(*grown)) :
                                        HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*grown));
        if (!grown)
            return;
        d->Skips = grown;
        d->Capacity = capacity;
    }
    DWORD at = 0;
    while (at < d->Count && d->Skips[at].Address < address)
        at++;
    memmove(d->Skips + at + 1, d->Skips + at, (d->Count - at) * sizeof(*d->Skips));
    d->Skips[at].Address = address;
    d->Skips[at].Size = size;
    d->Count++;
}

static void digest_skip_rva(DIGEST *d, ULONG rva, DWORD size)
{
    digest_skip(d, ImageRvaToVa(d->Nt, d->Base, rva, nullptr), size);
}

/* hands the bytes to the digest function but for the skipped ranges, which are the part of the file that may
 * differ between two copies of the same program: the checksum, the certificates, the resources.
 */
static BOOL digest_emit(DIGEST *d, const BYTE *data, DWORD length)
{
    for (DWORD i = 0; i < d->Count && length; i++) {
        const DIGEST_SKIP *skip = &d->Skips[i];
        if (skip->Address >= data) {
            DWORD n = (DWORD)(skip->Address - data);
            if (n > length)
                n = length;
            if (n) {
                if (!d->Digest(d->Handle, (PBYTE)data, n))
                    return FALSE;
                data += n;
                length -= n;
            }
        }
        if (length && skip->Address + skip->Size >= data) {
            DWORD n = (DWORD)(skip->Address + skip->Size - data);
            if (n > length)
                n = length;
            data += n;
            length -= n;
        }
    }
    return !length || d->Digest(d->Handle, (PBYTE)data, length);
}

/*
 * the digest of a program as the authenticode hash of the certificate stores takes it (read from imagehlp.dll of
 * build 26300). The bytes go out in this order: everything before the NT headers, a copy of the NT headers in
 * which the checksum, the certificate entry, and unless the level asks for them the resource entry, the size of
 * the image and of the initialized data are zeroed, the section table with the entries that depend on what
 * was left out zeroed, and the data of the sections in the order of the table.
 *
 * The level keeps in what it names, bit 1 the section named .debug, bit 2 the resources and bit 4 the import
 * tables. Without the last the import descriptors lose their time stamp and forwarder chain and the address
 * tables are left out. The data after the last section (the certificates and whatever follows) is never in,
 * the level of non PE information has no effect here.
 */
IMAGEHLP BOOL WINAPI ImageGetDigestStream(HANDLE file, DWORD level, DIGEST_FUNCTION digest, PVOID handle)
{
    DIGEST d = {.Digest = digest, .Handle = handle};
    LOADED_IMAGE image = {};
    BYTE *sections = nullptr;
    DWORD error = ERROR_INVALID_PARAMETER;
    BOOL ok = FALSE;
    image.SizeOfImage = GetFileSize(file, nullptr);
    if (image.SizeOfImage == INVALID_FILE_SIZE || !digest)
        goto done;
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping)
        goto done;
    image.MappedAddress = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);
    if (!image.MappedAddress || !locate_headers(&image))
        goto done;
    BYTE *base = image.MappedAddress;
    BYTE *end = base + image.SizeOfImage;
    PIMAGE_NT_HEADERS nt = image.FileHeader;
    if (image.fDOSImage)
        goto unmap;
    WORD magic = nt->OptionalHeader.Magic;
    if (magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC && magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        goto unmap;
    BOOL pe32 = magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    d.Nt = nt;
    d.Base = base;
    error = ERROR_OPERATION_ABORTED;
    if ((BYTE *)nt != base && !digest_emit(&d, base, (DWORD)((BYTE *)nt - base)))
        goto unmap;

    /* the copy of the headers and of the section table, which the skipped ranges never touch. */
    DWORD headers_size = pe32 ? 248 : 264, count = image.NumberOfSections;
    alignas(8) BYTE headers[264];
    memcpy(headers, nt, headers_size);
    sections = HeapAlloc(GetProcessHeap(), 0, 40 * (SIZE_T)count);
    if (!sections || (BYTE *)(image.Sections + count) > end)
        goto unmap;
    memcpy(sections, image.Sections, 40 * (SIZE_T)count);
    DWORD *directories = (DWORD *)(headers + 24 + (pe32 ? 96 : 112));
    DWORD *resource = directories + 4, *certificate = directories + 8, *relocation = directories + 10;
    DWORD resource_rva = resource[0], resource_size = resource[1], relocation_rva = relocation[0],
          relocation_size = relocation[1], debug_rva = 0;
    DWORD *size_of_image = (DWORD *)(headers + 80), *size_of_data = (DWORD *)(headers + 32);
    BOOL debug_found = FALSE;
    LONG relocation_section = -1;
    *(DWORD *)(headers + 88) = 0;
    certificate[0] = certificate[1] = 0;
    for (DWORD i = 0; i < count; i++) {
        const IMAGE_SECTION_HEADER *s = &image.Sections[i];
        BYTE *copy = sections + 40 * i;
        if (relocation_size && s->VirtualAddress <= relocation_rva &&
            s->Misc.VirtualSize + s->VirtualAddress >= relocation_size + relocation_rva)
            relocation_section = (LONG)i;
        if (resource_size && !(level & CERT_PE_IMAGE_DIGEST_RESOURCES) && s->VirtualAddress <= resource_rva &&
            s->Misc.VirtualSize + s->VirtualAddress >= resource_size + resource_rva)
            memset(copy + 8, 0, 16);
        if (!(level & CERT_PE_IMAGE_DIGEST_DEBUG_INFO) && !strncmp((const char *)s->Name, ".debug", 7)) {
            debug_rva = s->VirtualAddress;
            digest_skip(&d, base + s->PointerToRawData, s->SizeOfRawData);
            memset(copy + 8, 0, 16);
            debug_found = TRUE;
        }
    }
    /* what the relocation section is when the removed data came before it: its place and its directory entry go too. */
    BOOL zero_relocations = FALSE;
    if (!(level & CERT_PE_IMAGE_DIGEST_RESOURCES)) {
        zero_relocations = resource_rva < relocation_rva;
        resource[0] = resource[1] = 0;
        *size_of_image = *size_of_data = 0;
        digest_skip_rva(&d, resource_rva, resource_size);
    }
    if (!(level & CERT_PE_IMAGE_DIGEST_DEBUG_INFO) && debug_found) {
        *size_of_image = *size_of_data = 0;
        zero_relocations |= debug_rva < relocation_rva;
    }
    if (zero_relocations && relocation_section >= 0) {
        relocation[0] = 0;
        memset(sections + 40 * relocation_section + 12, 0, 4);
        memset(sections + 40 * relocation_section + 20, 0, 4);
    }
    if (!digest_emit(&d, headers, headers_size) || !digest_emit(&d, sections, 40 * count))
        goto unmap;

    if (!(level & CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO)) {
        ULONG import_size = 0, address_size = 0;
        DWORD *descriptor = ImageDirectoryEntryToDataEx(base, FALSE, IMAGE_DIRECTORY_ENTRY_IMPORT, &import_size, nullptr);
        if (import_size && descriptor) {
            void *addresses = ImageDirectoryEntryToDataEx(base, FALSE, IMAGE_DIRECTORY_ENTRY_IAT, &address_size, nullptr);
            if (addresses)
                digest_skip(&d, addresses, address_size);
            for (; (BYTE *)(descriptor + 5) <= end && *descriptor; descriptor += 5) {
                digest_skip(&d, (BYTE *)(descriptor + 1), 8);
                if (addresses)
                    continue;
                /* without a directory of address tables the thunks of each descriptor are left out. */
                BYTE *first = ImageRvaToVa(nt, base, *descriptor, nullptr), *at = first;
                DWORD width = pe32 ? 4 : 8;
                if (!first)
                    continue;
                while (at + width <= end && (pe32 ? *(DWORD *)at : *(UINT64 *)at))
                    at += width;
                digest_skip_rva(&d, descriptor[4], (DWORD)(at - first) + width);
            }
        }
    }
    for (DWORD i = 0; i < count; i++) {
        const IMAGE_SECTION_HEADER *s = &image.Sections[i];
        if (s->PointerToRawData > image.SizeOfImage || s->SizeOfRawData > image.SizeOfImage - s->PointerToRawData)
            goto unmap;
        if (!digest_emit(&d, base + s->PointerToRawData, s->SizeOfRawData))
            goto unmap;
    }
    error = 0;
    ok = TRUE;
unmap:
    UnmapViewOfFile(image.MappedAddress);
done:
    if (d.Skips)
        HeapFree(GetProcessHeap(), 0, d.Skips);
    if (sections)
        HeapFree(GetProcessHeap(), 0, sections);
    SetLastError(error);
    return ok;
}
