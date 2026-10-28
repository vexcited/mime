/*
 * the reading of a zip file: the central directory into a list of entries ordered by name, and an entry as a stream.
 *
 * names are taken as the zip file says: UTF-8 when it says so or has the unicode path field, else the OEM code
 * page. an entry whose name leaves the root of the archive, with a part named ".." or a drive, is left out.
 */
#include "zipfldr_private.h"
#include <png/inflate.h>

#define SIG_LOCAL 0x04034B50u
#define SIG_CENTRAL 0x02014B50u
#define SIG_END 0x06054B50u
#define SIG_END64 0x06064B50u
#define SIG_LOCATOR64 0x07064B50u

#define LOCAL_SIZE 30
#define CENTRAL_SIZE 46
#define END_SIZE 22
#define END64_SIZE 56
#define LOCATOR_SIZE 20

#define FLAG_ENCRYPTED 0x0001
#define FLAG_UTF8 0x0800

#define METHOD_STORED 0
#define METHOD_DEFLATED 8

#define EXTRA_ZIP64 0x0001
#define EXTRA_UNICODE_PATH 0x7075
#define EXTRA_TIMESTAMP 0x5455

#define MAX_DIRECTORY 0x40000000u
#define MAX_COMMENT 0xFFFF

static DWORD g_crc_table[256];

void ZipInit(void)
{
    for (DWORD n = 0; n < 256; n++) {
        DWORD c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crc_table[n] = c;
    }
}

static DWORD crc32(const BYTE *data, SIZE_T size)
{
    DWORD crc = 0xFFFFFFFFu;
    while (size--)
        crc = g_crc_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static WORD get16(const BYTE *p)
{
    WORD value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static DWORD get32(const BYTE *p)
{
    DWORD value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static ULONGLONG get64(const BYTE *p)
{
    ULONGLONG value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static BOOL read_at(HANDLE file, ULONGLONG offset, void *buffer, SIZE_T size)
{
    BYTE *p = buffer;
    LARGE_INTEGER position;
    position.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN))
        return FALSE;
    while (size) {
        DWORD want = size > 0x10000000 ? 0x10000000 : (DWORD)size, got;
        if (!ReadFile(file, p, want, &got, nullptr) || !got)
            return FALSE;
        p += got;
        size -= got;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ names */

int ZipCompare(LPCWSTR a, SIZE_T a_length, LPCWSTR b, SIZE_T b_length)
{
    SIZE_T n = a_length < b_length ? a_length : b_length;
    for (SIZE_T i = 0; i < n; i++) {
        if (a[i] == b[i])
            continue;
        WCHAR x = towlower(a[i]), y = towlower(b[i]);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return a_length == b_length ? 0 : (a_length < b_length ? -1 : 1);
}

BOOL ZipHasPrefix(LPCWSTR name, LPCWSTR prefix, SIZE_T length)
{
    for (SIZE_T i = 0; i < length; i++) {
        if (!name[i])
            return FALSE;
        if (name[i] != prefix[i] && towlower(name[i]) != towlower(prefix[i]))
            return FALSE;
    }
    return TRUE;
}

ULONG ZipLowerBound(const ZIP_ARCHIVE *archive, LPCWSTR prefix, SIZE_T length)
{
    ULONG low = 0, high = archive->Count;
    while (low < high) {
        ULONG middle = low + (high - low) / 2;
        const ZIP_ENTRY *entry = &archive->Entries[archive->Order[middle]];
        if (ZipCompare(entry->Name, entry->Length, prefix, length) < 0)
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

LONG ZipFind(const ZIP_ARCHIVE *archive, LPCWSTR name, SIZE_T length)
{
    ULONG at = ZipLowerBound(archive, name, length);
    if (at >= archive->Count)
        return -1;
    ULONG number = archive->Order[at];
    const ZIP_ENTRY *entry = &archive->Entries[number];
    return ZipCompare(entry->Name, entry->Length, name, length) ? -1 : (LONG)number;
}

static LPWSTR convert_name(const BYTE *name, SIZE_T length, BOOL utf8)
{
    if (!length || length > 0x7FFF)
        return nullptr;
    UINT code_page = utf8 ? CP_UTF8 : CP_OEMCP;
    int n = MultiByteToWideChar(code_page, 0, (LPCSTR)name, (int)length, nullptr, 0);
    if (n <= 0)
        return nullptr;
    LPWSTR text = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!text)
        return nullptr;
    if (MultiByteToWideChar(code_page, 0, (LPCSTR)name, (int)length, text, n) != n) {
        HeapFree(GetProcessHeap(), 0, text);
        return nullptr;
    }
    text[n] = 0;
    return text;
}

/* the name with backslashes, empty and "." parts dropped. a name that goes above the root or to a drive is refused. */
static BOOL normalize_name(LPWSTR name, BOOL *folder)
{
    SIZE_T length = wcslen(name), out = 0, i = 0;
    if (length && (name[length - 1] == L'/' || name[length - 1] == L'\\'))
        *folder = TRUE;
    BOOL first = TRUE;
    while (i < length) {
        while (i < length && (name[i] == L'/' || name[i] == L'\\'))
            i++;
        SIZE_T start = i;
        while (i < length && name[i] != L'/' && name[i] != L'\\')
            i++;
        SIZE_T part = i - start;
        if (!part || (part == 1 && name[start] == L'.'))
            continue;
        if (part == 2 && name[start] == L'.' && name[start + 1] == L'.')
            return FALSE;
        if (first && part >= 2 && name[start + 1] == L':')
            return FALSE;
        if (out)
            name[out++] = L'\\';
        memmove(name + out, name + start, part * sizeof(WCHAR));
        out += part;
        first = FALSE;
    }
    name[out] = 0;
    return out != 0;
}

static void dos_filetime(WORD date, WORD time, FILETIME *result)
{
    FILETIME local;
    result->dwLowDateTime = result->dwHighDateTime = 0;
    if (DosDateTimeToFileTime(date, time, &local))
        LocalFileTimeToFileTime(&local, result);
}

static void unix_filetime(DWORD seconds, FILETIME *result)
{
    ULONGLONG ticks = (ULONGLONG)seconds * 10000000ull + 116444736000000000ull;
    result->dwLowDateTime = (DWORD)ticks;
    result->dwHighDateTime = (DWORD)(ticks >> 32);
}

/* ------------------------------------------------------------------ the central directory */

static HRESULT parse_entry(const BYTE **cursor, const BYTE *end, ZIP_ENTRY *entry, BOOL *keep)
{
    const BYTE *p = *cursor;
    *keep = FALSE;
    if (end - p < CENTRAL_SIZE || get32(p) != SIG_CENTRAL)
        return HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);
    WORD made_by = get16(p + 4), flags = get16(p + 8), method = get16(p + 10);
    WORD time = get16(p + 12), date = get16(p + 14);
    DWORD crc = get32(p + 16), compressed = get32(p + 20), size = get32(p + 24);
    WORD name_length = get16(p + 28), extra_length = get16(p + 30), comment_length = get16(p + 32);
    DWORD attributes = get32(p + 38), offset = get32(p + 42);
    SIZE_T total = (SIZE_T)CENTRAL_SIZE + name_length + extra_length + comment_length;
    if ((SIZE_T)(end - p) < total)
        return HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);
    const BYTE *name = p + CENTRAL_SIZE, *extra = name + name_length, *extra_end = extra + extra_length;
    *cursor = p + total;

    entry->Size = size;
    entry->CompressedSize = compressed;
    entry->Offset = offset;
    entry->Crc = crc;
    entry->Method = method;
    entry->Encrypted = (flags & FLAG_ENCRYPTED) != 0;
    dos_filetime(date, time, &entry->Time);

    LPWSTR unicode = nullptr;
    for (const BYTE *q = extra; extra_end - q >= 4;) {
        WORD id = get16(q), length = get16(q + 2);
        const BYTE *data = q + 4;
        if (extra_end - data < length)
            break;
        q = data + length;
        if (id == EXTRA_ZIP64) {
            const BYTE *field = data, *field_end = data + length;
            if (size == 0xFFFFFFFFu && field_end - field >= 8) {
                entry->Size = get64(field);
                field += 8;
            }
            if (compressed == 0xFFFFFFFFu && field_end - field >= 8) {
                entry->CompressedSize = get64(field);
                field += 8;
            }
            if (offset == 0xFFFFFFFFu && field_end - field >= 8)
                entry->Offset = get64(field);
        } else if (id == EXTRA_UNICODE_PATH && length >= 5 && data[0] == 1 && get32(data + 1) == crc32(name, name_length)) {
            HeapFree(GetProcessHeap(), 0, unicode);
            unicode = convert_name(data + 5, length - 5, TRUE);
        } else if (id == EXTRA_TIMESTAMP && length >= 5 && (data[0] & 1)) {
            unix_filetime(get32(data + 1), &entry->Time);
        }
    }

    LPWSTR text = unicode ? unicode : convert_name(name, name_length, (flags & FLAG_UTF8) != 0);
    if (!text)
        return S_OK;
    BOOL folder = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if ((made_by >> 8) == 3 && ((attributes >> 16) & 0xF000) == 0x4000)
        folder = TRUE;
    if (!normalize_name(text, &folder)) {
        HeapFree(GetProcessHeap(), 0, text);
        return S_OK;
    }
    entry->Name = text;
    entry->Length = wcslen(text);
    entry->Folder = folder;
    *keep = TRUE;
    return S_OK;
}

/* a stable merge sort of the entry numbers by name. */
static void sort_order(const ZIP_ENTRY *entries, ULONG *order, ULONG *scratch, ULONG count)
{
    for (ULONG width = 1; width < count; width *= 2) {
        for (ULONG start = 0; start < count; start += 2 * width) {
            ULONG middle = start + width < count ? start + width : count;
            ULONG stop = start + 2 * width < count ? start + 2 * width : count;
            ULONG a = start, b = middle, out = start;
            while (a < middle && b < stop) {
                const ZIP_ENTRY *x = &entries[order[a]], *y = &entries[order[b]];
                scratch[out++] = ZipCompare(y->Name, y->Length, x->Name, x->Length) < 0 ? order[b++] : order[a++];
            }
            while (a < middle)
                scratch[out++] = order[a++];
            while (b < stop)
                scratch[out++] = order[b++];
        }
        memcpy(order, scratch, count * sizeof(*order));
    }
}

static HRESULT locate_directory(HANDLE file, ULONGLONG size, ULONGLONG *count, ULONGLONG *directory_size,
                                ULONGLONG *directory_offset, ULONGLONG *shift)
{
    SIZE_T tail_size = size < MAX_COMMENT + END_SIZE + LOCATOR_SIZE ? (SIZE_T)size
                                                                   : MAX_COMMENT + END_SIZE + LOCATOR_SIZE;
    BYTE *tail = HeapAlloc(GetProcessHeap(), 0, tail_size);
    if (!tail)
        return E_OUTOFMEMORY;
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
    ULONGLONG tail_start = size - tail_size;
    if (!read_at(file, tail_start, tail, tail_size)) {
        HeapFree(GetProcessHeap(), 0, tail);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    for (SIZE_T i = tail_size - END_SIZE; (LONG_PTR)i >= 0; i--) {
        if (get32(tail + i) != SIG_END || i + END_SIZE + get16(tail + i + 20) > tail_size)
            continue;
        const BYTE *end = tail + i;
        ULONGLONG end_position = tail_start + i;
        *count = get16(end + 10);
        *directory_size = get32(end + 12);
        *directory_offset = get32(end + 16);
        *shift = 0;
        hr = S_OK;
        BOOL large = *count == 0xFFFF || *directory_size == 0xFFFFFFFFu || *directory_offset == 0xFFFFFFFFu;
        if (large && i >= LOCATOR_SIZE && get32(tail + i - LOCATOR_SIZE) == SIG_LOCATOR64) {
            ULONGLONG position = get64(tail + i - LOCATOR_SIZE + 8);
            BYTE record[END64_SIZE];
            if (!read_at(file, position, record, sizeof(record)) || get32(record) != SIG_END64) {
                hr = HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);
                break;
            }
            *count = get64(record + 32);
            *directory_size = get64(record + 40);
            *directory_offset = get64(record + 48);
            end_position = position;
        }
        if (*directory_size <= end_position && *directory_offset <= end_position - *directory_size)
            *shift = end_position - *directory_size - *directory_offset;
        break;
    }
    HeapFree(GetProcessHeap(), 0, tail);
    return hr;
}

void ZipAddRef(ZIP_ARCHIVE *archive)
{
    InterlockedIncrement(&archive->Refs);
}

void ZipRelease(ZIP_ARCHIVE *archive)
{
    if (InterlockedDecrement(&archive->Refs))
        return;
    for (ULONG i = 0; i < archive->Count; i++)
        HeapFree(GetProcessHeap(), 0, archive->Entries[i].Name);
    HeapFree(GetProcessHeap(), 0, archive->Entries);
    HeapFree(GetProcessHeap(), 0, archive->Order);
    HeapFree(GetProcessHeap(), 0, archive);
}

HRESULT ZipOpen(LPCWSTR path, ZIP_ARCHIVE **result)
{
    *result = nullptr;
    if (wcslen(path) >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < END_SIZE) {
        CloseHandle(file);
        return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
    }
    ULONGLONG count, directory_size, directory_offset, shift;
    HRESULT hr = locate_directory(file, (ULONGLONG)size.QuadPart, &count, &directory_size, &directory_offset, &shift);
    BYTE *directory = nullptr;
    if (SUCCEEDED(hr) && (directory_size > MAX_DIRECTORY || count > directory_size / CENTRAL_SIZE ||
                          directory_offset + shift + directory_size > (ULONGLONG)size.QuadPart))
        hr = HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);
    if (SUCCEEDED(hr)) {
        directory = HeapAlloc(GetProcessHeap(), 0, directory_size ? (SIZE_T)directory_size : 1);
        if (!directory)
            hr = E_OUTOFMEMORY;
        else if (!read_at(file, directory_offset + shift, directory, (SIZE_T)directory_size))
            hr = HRESULT_FROM_WIN32(GetLastError());
    }
    CloseHandle(file);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, directory);
        return hr;
    }

    ZIP_ARCHIVE *archive = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*archive));
    if (archive) {
        archive->Entries = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ((SIZE_T)count + 1) * sizeof(ZIP_ENTRY));
        archive->Order = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)count + 1) * sizeof(ULONG));
    }
    ULONG *scratch = archive ? HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)count + 1) * sizeof(ULONG)) : nullptr;
    if (!archive || !archive->Entries || !archive->Order || !scratch) {
        if (archive)
            ZipRelease(archive);
        HeapFree(GetProcessHeap(), 0, scratch);
        HeapFree(GetProcessHeap(), 0, directory);
        return E_OUTOFMEMORY;
    }
    archive->Refs = 1;
    wcscpy(archive->Path, path);

    const BYTE *cursor = directory, *end = directory + directory_size;
    for (ULONGLONG i = 0; i < count && SUCCEEDED(hr); i++) {
        ZIP_ENTRY *entry = &archive->Entries[archive->Count];
        BOOL keep;
        hr = parse_entry(&cursor, end, entry, &keep);
        if (SUCCEEDED(hr) && keep) {
            entry->Offset += shift;
            archive->Order[archive->Count] = archive->Count;
            archive->Count++;
        }
    }
    HeapFree(GetProcessHeap(), 0, directory);
    if (SUCCEEDED(hr)) {
        sort_order(archive->Entries, archive->Order, scratch, archive->Count);
        *result = archive;
    } else {
        ZipRelease(archive);
    }
    HeapFree(GetProcessHeap(), 0, scratch);
    return hr;
}

/* ------------------------------------------------------------------ an entry as a stream */

/*
 * a stored entry is read from the file as it is asked for. a deflated entry is inflated into memory when the stream is made,
 * whole, since the decoder needs the output of an entry as its window.
 */
typedef struct _ZIP_STREAM {
    IStream Interface;
    LONG Refs;
    ZIP_ARCHIVE *Archive;
    ULONG Number;
    HANDLE File;
    ULONGLONG Start;
    ULONGLONG Size;
    ULONGLONG Position;
    BYTE *Data;
} ZIP_STREAM;

static const IStreamVtbl g_stream_vtbl;

static HRESULT STDMETHODCALLTYPE stream_query_interface(IStream *This, REFIID riid, void **object)
{
    static const GUID sequential = {0x0C733A30, 0x2A1C, 0x11CE, {0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D}};
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IStream) && !IsEqualIID(riid, &sequential))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((ZIP_STREAM *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE stream_add_ref(IStream *This)
{
    return (ULONG)InterlockedIncrement(&((ZIP_STREAM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE stream_release(IStream *This)
{
    ZIP_STREAM *s = (ZIP_STREAM *)This;
    LONG refs = InterlockedDecrement(&s->Refs);
    if (!refs) {
        if (s->File != INVALID_HANDLE_VALUE)
            CloseHandle(s->File);
        HeapFree(GetProcessHeap(), 0, s->Data);
        ZipRelease(s->Archive);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE stream_read(IStream *This, void *buffer, ULONG count, ULONG *read)
{
    ZIP_STREAM *s = (ZIP_STREAM *)This;
    ULONG done = 0;
    if (read)
        *read = 0;
    if (!buffer && count)
        return STG_E_INVALIDPOINTER;
    if (s->Position < s->Size) {
        ULONGLONG left = s->Size - s->Position;
        ULONG want = left < count ? (ULONG)left : count;
        if (s->Data) {
            memcpy(buffer, s->Data + (SIZE_T)s->Position, want);
            done = want;
        } else if (want) {
            LARGE_INTEGER at;
            at.QuadPart = (LONGLONG)(s->Start + s->Position);
            if (!SetFilePointerEx(s->File, at, nullptr, FILE_BEGIN) || !ReadFile(s->File, buffer, want, &done, nullptr))
                return STG_E_READFAULT;
        }
        s->Position += done;
    }
    if (read)
        *read = done;
    return done == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE stream_write(IStream *This, const void *buffer, ULONG count, ULONG *written)
{
    if (written)
        *written = 0;
    return STG_E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE stream_seek(IStream *This, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    ZIP_STREAM *s = (ZIP_STREAM *)This;
    LONGLONG base;
    switch (origin) {
    case STREAM_SEEK_SET:
        base = 0;
        break;
    case STREAM_SEEK_CUR:
        base = (LONGLONG)s->Position;
        break;
    case STREAM_SEEK_END:
        base = (LONGLONG)s->Size;
        break;
    default:
        return STG_E_INVALIDFUNCTION;
    }
    LONGLONG target = base + move.QuadPart;
    if (target < 0)
        return STG_E_INVALIDFUNCTION;
    s->Position = (ULONGLONG)target;
    if (position)
        position->QuadPart = s->Position;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_set_size(IStream *This, ULARGE_INTEGER size)
{
    return STG_E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE stream_copy_to(IStream *This, IStream *target, ULARGE_INTEGER count,
                                                ULARGE_INTEGER *read, ULARGE_INTEGER *written)
{
    BYTE buffer[16384];
    ULONGLONG total_read = 0, total_written = 0, left = count.QuadPart;
    HRESULT hr = S_OK;
    if (!target)
        return STG_E_INVALIDPOINTER;
    while (left) {
        ULONG want = left < sizeof(buffer) ? (ULONG)left : (ULONG)sizeof(buffer), got = 0, put = 0;
        hr = stream_read(This, buffer, want, &got);
        if (FAILED(hr) || !got)
            break;
        total_read += got;
        hr = target->lpVtbl->Write(target, buffer, got, &put);
        total_written += put;
        if (FAILED(hr) || put != got)
            break;
        left -= got;
    }
    if (read)
        read->QuadPart = total_read;
    if (written)
        written->QuadPart = total_written;
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_commit(IStream *This, DWORD flags)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_revert(IStream *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_lock_region(IStream *This, ULARGE_INTEGER offset, ULARGE_INTEGER size,
                                                    DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT STDMETHODCALLTYPE stream_stat(IStream *This, STATSTG *stat, DWORD flags)
{
    ZIP_STREAM *s = (ZIP_STREAM *)This;
    if (!stat)
        return STG_E_INVALIDPOINTER;
    memset(stat, 0, sizeof(*stat));
    const ZIP_ENTRY *entry = &s->Archive->Entries[s->Number];
    if (!(flags & STATFLAG_NONAME)) {
        LPCWSTR name = wcsrchr(entry->Name, L'\\');
        name = name ? name + 1 : entry->Name;
        stat->pwcsName = CoTaskMemAlloc((wcslen(name) + 1) * sizeof(WCHAR));
        if (!stat->pwcsName)
            return STG_E_INSUFFICIENTMEMORY;
        wcscpy(stat->pwcsName, name);
    }
    stat->type = STGTY_STREAM;
    stat->cbSize.QuadPart = s->Size;
    stat->mtime = entry->Time;
    stat->grfMode = STGM_READ;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_clone(IStream *This, IStream **clone)
{
    if (clone)
        *clone = nullptr;
    return E_NOTIMPL;
}

static const IStreamVtbl g_stream_vtbl = {
    stream_query_interface,
    stream_add_ref,
    stream_release,
    stream_read,
    stream_write,
    stream_seek,
    stream_set_size,
    stream_copy_to,
    stream_commit,
    stream_revert,
    stream_lock_region,
    stream_lock_region,
    stream_stat,
    stream_clone,
};

/* inflates the data of an entry at the start of its compressed bytes into memory. */
static HRESULT inflate_entry(HANDLE file, ULONGLONG start, const ZIP_ENTRY *entry, BYTE **data)
{
    *data = nullptr;
    if (entry->Size > 0x7FFFFFFFu || entry->CompressedSize > 0x7FFFFFFFu)
        return E_OUTOFMEMORY;
    SIZE_T size = (SIZE_T)entry->Size, compressed = (SIZE_T)entry->CompressedSize;
    BYTE *source = HeapAlloc(GetProcessHeap(), 0, compressed ? compressed : 1);
    BYTE *output = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    HRESULT hr = S_OK;
    if (!source || !output) {
        hr = E_OUTOFMEMORY;
    } else if (!read_at(file, start, source, compressed)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
    } else {
        size_t produced;
        int result = mime_inflate(source, compressed, output, size, &produced, 0);
        if (result != MIME_INFLATE_OK || produced != size || crc32(output, size) != entry->Crc)
            hr = HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);
    }
    HeapFree(GetProcessHeap(), 0, source);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, output);
        return hr;
    }
    *data = output;
    return S_OK;
}

HRESULT ZipOpenStream(ZIP_ARCHIVE *archive, ULONG number, IStream **stream)
{
    *stream = nullptr;
    if (number >= archive->Count)
        return E_INVALIDARG;
    const ZIP_ENTRY *entry = &archive->Entries[number];
    if (entry->Folder)
        return E_INVALIDARG;
    if (entry->Encrypted || (entry->Method != METHOD_STORED && entry->Method != METHOD_DEFLATED))
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    HANDLE file = CreateFileW(archive->Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    BYTE header[LOCAL_SIZE];
    HRESULT hr = S_OK;
    if (!read_at(file, entry->Offset, header, sizeof(header)))
        hr = HRESULT_FROM_WIN32(GetLastError());
    else if (get32(header) != SIG_LOCAL)
        hr = HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);
    ZIP_STREAM *s = nullptr;
    if (SUCCEEDED(hr)) {
        s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
        if (!s)
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr)) {
        s->Start = entry->Offset + LOCAL_SIZE + get16(header + 26) + get16(header + 28);
        s->Size = entry->Size;
        if (entry->Method == METHOD_DEFLATED)
            hr = inflate_entry(file, s->Start, entry, &s->Data);
        else if (entry->Size != entry->CompressedSize)
            hr = HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);
    }
    if (FAILED(hr)) {
        CloseHandle(file);
        HeapFree(GetProcessHeap(), 0, s);
        return hr;
    }
    s->Interface.lpVtbl = &g_stream_vtbl;
    s->Refs = 1;
    s->Archive = archive;
    ZipAddRef(archive);
    s->Number = number;
    if (s->Data) {
        CloseHandle(file);
        s->File = INVALID_HANDLE_VALUE;
    } else {
        s->File = file;
    }
    *stream = &s->Interface;
    return S_OK;
}
