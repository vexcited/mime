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
#define CERT_PE_IMAGE_DIGEST_NON_PE_INFO 0x80

typedef BOOL(WINAPI *DIGEST_FUNCTION)(PVOID handle, PBYTE data, DWORD length);

/* hands a range of the file to the digest function in pieces. */
static BOOL digest_range(HANDLE file, DIGEST_FUNCTION digest, PVOID handle, DWORD from, DWORD to)
{
    static BYTE buffer[65536];
    while (from < to) {
        DWORD n = to - from < sizeof buffer ? to - from : (DWORD)sizeof buffer;
        if (!read_at(file, from, buffer, n)) {
            SetLastError(ERROR_READ_FAULT);
            return FALSE;
        }
        if (!digest(handle, buffer, n))
            return FALSE;
        from += n;
    }
    return TRUE;
}

/*
 * the bytes of a signed image in the order its digest takes them: the headers without the checksum and the
 * certificate directory entry, then the data of each section by file position, then the rest of the file but for the
 * certificates when the level asks for data outside the image. The levels leave out the data of the debug directory
 * and of the resource section unless they name them. The import tables are always in, the level has no effect on them.
 */
IMAGEHLP BOOL WINAPI ImageGetDigestStream(HANDLE file, DWORD level, DIGEST_FUNCTION digest, PVOID handle)
{
    SECDIR dir;
    IMAGE_DOS_HEADER dos;
    IMAGE_FILE_HEADER header;
    WORD magic = 0;
    if (!digest || !open_directory(file, &dir) || !read_at(file, 0, &dos, sizeof dos) ||
        !read_at(file, (DWORD)dos.e_lfanew + 4, &header, sizeof header)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD optional = (DWORD)dos.e_lfanew + 4 + sizeof header;
    read_at(file, optional, &magic, 2);
    BOOL pe64 = magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    DWORD checksum_at = optional + 64;
    DWORD headers_size = 0, directories = optional + (pe64 ? 112 : 96);
    if (!read_at(file, optional + 60, &headers_size, 4)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (headers_size > dir.FileSize)
        headers_size = dir.FileSize;
    /* the rva and size of the resource and debug directories, to find their data in the sections. */
    DWORD debug[2] = {}, resource[2] = {};
    read_at(file, directories + 2 * 8, resource, sizeof resource);
    read_at(file, directories + 6 * 8, debug, sizeof debug);
    if (!digest_range(file, digest, handle, 0, checksum_at) ||
        !digest_range(file, digest, handle, checksum_at + 4, dir.DirectoryPosition) ||
        !digest_range(file, digest, handle, dir.DirectoryPosition + 8, headers_size))
        return FALSE;
    IMAGE_SECTION_HEADER sections[96];
    DWORD count = header.NumberOfSections < 96 ? header.NumberOfSections : 96;
    if (!read_at(file, optional + header.SizeOfOptionalHeader, sections, count * (DWORD)sizeof(sections[0]))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD end = headers_size;
    for (DWORD done = 0; done < count; done++) {
        /* the next section by position in the file. */
        DWORD next = count;
        for (DWORD i = 0; i < count; i++)
            if (sections[i].SizeOfRawData && sections[i].PointerToRawData >= end &&
                (next == count || sections[i].PointerToRawData < sections[next].PointerToRawData))
                next = i;
        if (next == count)
            break;
        const IMAGE_SECTION_HEADER *s = &sections[next];
        DWORD from = s->PointerToRawData, to = from + s->SizeOfRawData;
        if (to > dir.FileSize)
            to = dir.FileSize;
        BOOL skip_debug = !(level & CERT_PE_IMAGE_DIGEST_DEBUG_INFO) && debug[0] && debug[0] >= s->VirtualAddress &&
            debug[0] - s->VirtualAddress < s->SizeOfRawData;
        BOOL skip_resources = !(level & CERT_PE_IMAGE_DIGEST_RESOURCES) && resource[0] && !memcmp(s->Name, ".rsrc", 6);
        DWORD cut_from = to, cut_to = to;
        if (skip_resources) {
            cut_from = from;
        } else if (skip_debug) {
            cut_from = from + (debug[0] - s->VirtualAddress);
            cut_to = cut_from + debug[1] < to ? cut_from + debug[1] : to;
        }
        if (!digest_range(file, digest, handle, from, cut_from) ||
            (cut_to < to && !digest_range(file, digest, handle, cut_to, to)))
            return FALSE;
        end = to;
    }
    if ((level & CERT_PE_IMAGE_DIGEST_NON_PE_INFO) && end < dir.FileSize) {
        /* what follows the sections, but for the certificates, which a signature cannot cover. */
        DWORD stop = dir.Size && dir.Offset >= end ? dir.Offset : dir.FileSize;
        if (!digest_range(file, digest, handle, end, stop))
            return FALSE;
    }
    return TRUE;
}
