/*
 * lz32 expansion of files made by compress.exe, the SZDD format. The 14 byte header holds the 8 byte signature, the
 * mode letter A, the character the extension's last letter was cut to, and the expanded size. Data come in groups of
 * eight items under a control byte: each bit, lowest first, means a literal byte when set, else a 2 byte reference
 * into the last 4096 bytes with 12 bits of position and 4 of length minus three. The window starts filled with spaces
 * and is written from 16 bytes before its end.
 *
 * LZInit and LZOpenFile return a number below 16 standing for the file handle when the file is not compressed, and
 * 1024 and up when it is, which then expands as it is read.
 */
#include "kernelbase_private.h"

#define LZERROR_BADINHANDLE (-1)
#define LZERROR_BADOUTHANDLE (-2)
#define LZERROR_READ (-3)
#define LZERROR_WRITE (-4)
#define LZERROR_GLOBALLOC (-5)
#define LZERROR_GLOBLOCK (-6)
#define LZERROR_BADVALUE (-7)
#define LZERROR_UNKNOWNALG (-8)

#define LZ_HEADER 14
#define LZ_WINDOW 4096
#define LZ_START (LZ_WINDOW - 16)
#define LZ_SLOTS 16
#define LZ_FIRST 1024

static const BYTE g_signature[8] = {'S', 'Z', 'D', 'D', 0x88, 0xF0, 0x27, 0x33};

typedef struct {
    BYTE Mode;
    BYTE Missing;
    DWORD Size;
} LZ_HEADER_INFO;

/* seeking backwards restarts the expansion from the front of the file. */
typedef struct {
    HFILE File;
    LZ_HEADER_INFO Header;
    DWORD Position; /* offset of the next read. */
    DWORD Produced; /* bytes decoded so far. */
    BYTE Window[LZ_WINDOW];
    UINT Write;
    UINT Control;
    BYTE Input[2048];
    UINT InputLength, InputAt;
    BYTE Pending[18];
    UINT PendingLength, PendingAt;
} LZ_FILE;

static RTL_SRWLOCK g_lock;
static LZ_FILE *g_files[LZ_SLOTS];
static UINT g_next_file;
static HFILE g_dos[LZ_SLOTS];

static HANDLE win32_handle(HFILE file)
{
    return (HANDLE)(LONG_PTR)file;
}

/* the small numbers of uncompressed files index the handle table. */
static HFILE dos_to_win32(INT handle)
{
    if ((UINT)handle < LZ_SLOTS && g_dos[handle])
        return g_dos[handle];
    return handle;
}

static INT win32_to_dos(HFILE file)
{
    RtlAcquireSRWLockExclusive(&g_lock);
    INT slot = 0;
    while (slot < LZ_SLOTS && g_dos[slot])
        slot++;
    if (slot < LZ_SLOTS)
        g_dos[slot] = file;
    RtlReleaseSRWLockExclusive(&g_lock);
    return slot < LZ_SLOTS ? slot : LZERROR_GLOBALLOC;
}

/* 1 when read, LZERROR_READ for a file too short, -1 when it cannot be read. */
static INT read_header(HFILE file, LZ_HEADER_INFO *info, BOOL *compressed, DWORD *file_size)
{
    DWORD size = SetFilePointer(win32_handle(file), 0, nullptr, FILE_END);
    *file_size = size;
    *compressed = FALSE;
    if ((LONG)size < 0 || _llseek(file, 0, 0) != 0)
        return -1;
    BYTE raw[LZ_HEADER];
    UINT got = _lread(file, raw, sizeof(raw));
    if (got != sizeof(raw))
        return got == (UINT)HFILE_ERROR ? -1 : LZERROR_READ;
    *compressed = !memcmp(raw, g_signature, sizeof(g_signature));
    info->Mode = raw[8];
    info->Missing = raw[9];
    info->Size = raw[10] | raw[11] << 8 | raw[12] << 16 | (DWORD)raw[13] << 24;
    return 1;
}

static void decoder_reset(LZ_FILE *z)
{
    memset(z->Window, ' ', sizeof(z->Window));
    z->Write = LZ_START;
    z->Control = 0;
    z->InputLength = z->InputAt = 0;
    z->PendingLength = z->PendingAt = 0;
    z->Produced = 0;
}

static INT input_byte(LZ_FILE *z)
{
    if (z->InputAt == z->InputLength) {
        UINT got = _lread(z->File, z->Input, sizeof(z->Input));
        if (got == (UINT)HFILE_ERROR)
            return LZERROR_READ;
        z->InputLength = got;
        z->InputAt = 0;
        if (!got)
            return -100;
    }
    return z->Input[z->InputAt++];
}

/* the byte, -100 at the end of the data, or an error. */
static INT decode_byte(LZ_FILE *z)
{
    if (z->PendingAt < z->PendingLength) {
        z->Produced++;
        return z->Pending[z->PendingAt++];
    }
    for (;;) {
        if (!(z->Control & 0x100)) {
            INT control = input_byte(z);
            if (control < 0)
                return control;
            z->Control = (UINT)control | 0xFF00;
        }
        UINT bit = z->Control & 1;
        z->Control >>= 1;
        INT first = input_byte(z);
        if (first < 0)
            return first;
        if (bit) {
            z->Window[z->Write] = (BYTE)first;
            z->Write = (z->Write + 1) & (LZ_WINDOW - 1);
            z->Produced++;
            return first;
        }
        INT second = input_byte(z);
        if (second < 0)
            return second == -100 ? LZERROR_READ : second;
        UINT from = first | (UINT)(second & 0xF0) << 4, length = (UINT)(second & 0x0F) + 3;
        z->PendingLength = length;
        z->PendingAt = 0;
        for (UINT i = 0; i < length; i++) {
            BYTE b = z->Window[(from + i) & (LZ_WINDOW - 1)];
            z->Window[z->Write] = b;
            z->Write = (z->Write + 1) & (LZ_WINDOW - 1);
            z->Pending[i] = b;
        }
        z->Produced++;
        return z->Pending[z->PendingAt++];
    }
}

/* fewer than count bytes at the end of the data, negative on error. */
static INT decode_run(LZ_FILE *z, BYTE *out, UINT count)
{
    UINT n = 0;
    while (n < count) {
        INT b = decode_byte(z);
        if (b == -100)
            break;
        if (b < 0)
            return b;
        if (out)
            out[n] = (BYTE)b;
        n++;
    }
    return (INT)n;
}

static void start_decoder(LZ_FILE *z)
{
    decoder_reset(z);
    _llseek(z->File, LZ_HEADER, 0);
}

static LZ_FILE *slot_file(INT handle)
{
    return (UINT)(handle - LZ_FIRST) < LZ_SLOTS ? g_files[handle - LZ_FIRST] : nullptr;
}

DECLSPEC_EXPORT INT WINAPI LZInit(INT source)
{
    LZ_HEADER_INFO info;
    BOOL compressed;
    DWORD size;
    INT status = read_header(source, &info, &compressed, &size);
    if (status == 1 && compressed) {
        if (info.Mode != 'A')
            return LZERROR_UNKNOWNALG;
        LZ_FILE *z = KbAllocZero(sizeof(*z));
        if (!z)
            return LZERROR_GLOBALLOC;
        z->File = source;
        z->Header = info;
        RtlAcquireSRWLockExclusive(&g_lock);
        INT handle = LZERROR_GLOBALLOC;
        for (UINT i = 0; i < LZ_SLOTS; i++) {
            UINT slot = (g_next_file + i) % LZ_SLOTS;
            if (!g_files[slot]) {
                g_files[slot] = z;
                g_next_file = (slot + 1) % LZ_SLOTS;
                handle = LZ_FIRST + (INT)slot;
                break;
            }
        }
        RtlReleaseSRWLockExclusive(&g_lock);
        if (handle < 0)
            KbFree(z);
        else
            start_decoder(z);
        return handle;
    }
    /* a file that is not compressed, or too short to be, is read as it is. */
    if (status != 1 && size >= LZ_HEADER)
        return -1;
    if (_llseek(source, 0, 0) != 0)
        return -1;
    return win32_to_dos(source);
}

/* the compressed name ends its extension with an underscore. */
static void compressed_name(char *name, SIZE_T capacity)
{
    SIZE_T length = strlen(name), at = length;
    while (at > 0) {
        at--;
        if (name[at] == '.') {
            if (at && length - at > 3) {
                name[length - 1] = '_';
                return;
            }
            if (at) {
                if (length + 1 < capacity)
                    strcat(name, "_");
                return;
            }
            break;
        }
        if (name[at] == '\\' || name[at] == ':')
            break;
    }
    if (length + 2 < capacity)
        strcat(name, "._");
}

DECLSPEC_EXPORT INT WINAPI LZOpenFileA(LPSTR name, LPOFSTRUCT record, WORD style)
{
    char path[262];
    if (strlen(name) >= sizeof(path))
        return LZERROR_BADVALUE;
    strcpy(path, name);
    HFILE file = OpenFile(path, record, style);
    if (file == HFILE_ERROR) {
        if (record->nErrCode != ERROR_FILE_NOT_FOUND)
            return -1;
        compressed_name(path, sizeof(path));
        file = OpenFile(path, record, style);
        if (file == HFILE_ERROR)
            return -1;
    }
    INT handle;
    if (style & 0xFF0F) {
        handle = win32_to_dos(file);
        if (handle == LZERROR_GLOBALLOC)
            _lclose(file);
    } else {
        handle = LZInit(file);
        if (handle < 0)
            _lclose(file);
    }
    return handle;
}

DECLSPEC_EXPORT INT WINAPI LZOpenFileW(LPWSTR name, LPOFSTRUCT record, WORD style)
{
    char path[262];
    if (!WideCharToMultiByte(CP_ACP, 0, name, -1, path, sizeof(path), nullptr, nullptr))
        return LZERROR_GLOBALLOC;
    return LZOpenFileA(path, record, style);
}

/* unlike the narrow form, this cuts at the first dot of the last component. */
static void compressed_name_wide(WCHAR *name, SIZE_T capacity)
{
    WCHAR *part = name;
    for (WCHAR *p = name; *p; p++)
        if (*p == '\\' || *p == ':')
            part = p + 1;
    WCHAR *dot = part;
    while (*dot && *dot != '.')
        dot++;
    SIZE_T length = wcslen(name);
    if (*dot == '.' && wcslen(dot + 1) >= 3) {
        name[length - 1] = '_';
    } else if (*dot == '.') {
        if (length + 1 < capacity)
            wcscat(name, L"_");
    } else if (length + 2 < capacity) {
        wcscat(name, L"._");
    }
}

DECLSPEC_EXPORT INT WINAPI LZCreateFileW(LPCWSTR name, DWORD access, DWORD share, DWORD creation, LPWSTR expanded)
{
    WCHAR path[262];
    if (wcslen(name) >= RTL_NUMBER_OF(path))
        return LZERROR_BADVALUE;
    wcscpy(path, name);
    HANDLE file = CreateFileW(path, access, share, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) {
        compressed_name_wide(path, RTL_NUMBER_OF(path));
        file = CreateFileW(path, access, share, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (file == INVALID_HANDLE_VALUE)
        return -1;
    INT handle;
    if (creation == OPEN_EXISTING) {
        handle = LZInit((INT)(LONG_PTR)file);
        if (handle < 0)
            CloseHandle(file);
        if (expanded) {
            if (wcslen(path) >= 260)
                return LZERROR_BADVALUE;
            wcscpy(expanded, path);
        }
    } else {
        handle = win32_to_dos((INT)(LONG_PTR)file);
        if (handle == LZERROR_GLOBALLOC)
            CloseHandle(file);
    }
    return handle;
}

DECLSPEC_EXPORT INT WINAPI LZRead(INT handle, CHAR *buffer, INT count)
{
    if ((UINT)handle > LZ_FIRST + LZ_SLOTS - 1)
        return LZERROR_BADINHANDLE;
    if (count < 0)
        return LZERROR_BADVALUE;
    if (handle < LZ_FIRST)
        return (INT)_lread(dos_to_win32(handle), buffer, count);
    LZ_FILE *z = slot_file(handle);
    if (!z)
        return LZERROR_GLOBLOCK;
    if (z->Header.Mode != 'A')
        return LZERROR_UNKNOWNALG;
    /* seeking back means decoding again from the start. */
    if (z->Position < z->Produced)
        start_decoder(z);
    if (z->Position > z->Produced) {
        INT skipped = decode_run(z, nullptr, z->Position - z->Produced);
        if (skipped < 0)
            return skipped;
    }
    UINT want = z->Position < z->Header.Size ? z->Header.Size - z->Position : 0;
    if ((UINT)count < want)
        want = count;
    INT got = decode_run(z, (BYTE *)buffer, want);
    if (got < 0)
        return got;
    z->Position += got;
    return got;
}

DECLSPEC_EXPORT LONG WINAPI LZSeek(INT handle, LONG offset, INT origin)
{
    if ((UINT)handle > LZ_FIRST + LZ_SLOTS - 1)
        return LZERROR_BADINHANDLE;
    if (handle < LZ_FIRST)
        return _llseek(dos_to_win32(handle), offset, origin);
    LZ_FILE *z = slot_file(handle);
    if (!z)
        return LZERROR_GLOBLOCK;
    LONG base;
    switch (origin) {
    case 0:
        base = 0;
        break;
    case 1:
        base = (LONG)z->Position;
        break;
    case 2:
        base = (LONG)z->Header.Size;
        break;
    default:
        return LZERROR_BADVALUE;
    }
    LONG place = offset + base;
    if (place < 0)
        return LZERROR_BADVALUE;
    z->Position = (DWORD)place;
    return place;
}

static void close_handle(INT handle, BOOL as_handle)
{
    if ((UINT)handle > LZ_FIRST + LZ_SLOTS - 1)
        return;
    HFILE file;
    if (handle >= LZ_FIRST) {
        RtlAcquireSRWLockExclusive(&g_lock);
        LZ_FILE *z = slot_file(handle);
        g_files[handle - LZ_FIRST] = nullptr;
        RtlReleaseSRWLockExclusive(&g_lock);
        if (!z)
            return;
        file = z->File;
        KbFree(z);
    } else {
        file = dos_to_win32(handle);
        if (handle < LZ_SLOTS)
            g_dos[handle] = 0;
    }
    if (as_handle)
        CloseHandle(win32_handle(file));
    else
        _lclose(file);
}

DECLSPEC_EXPORT void WINAPI LZClose(INT handle)
{
    close_handle(handle, FALSE);
}

DECLSPEC_EXPORT void WINAPI LZCloseFile(INT handle)
{
    close_handle(handle, TRUE);
}

static void copy_time(HFILE source, HFILE dest)
{
    FILETIME created, accessed, written;
    if (GetFileTime(win32_handle(source), &created, &accessed, &written))
        SetFileTime(win32_handle(dest), &created, &accessed, &written);
}

DECLSPEC_EXPORT LONG WINAPI LZCopy(INT source, INT dest)
{
    HFILE in;
    if (source < LZ_FIRST) {
        in = dos_to_win32(source);
    } else {
        LZ_FILE *z = slot_file(source);
        if (!z)
            return LZERROR_BADINHANDLE;
        in = z->File;
    }
    HFILE out = dos_to_win32(dest);
    LZ_HEADER_INFO info;
    BOOL compressed;
    DWORD size;
    INT status = read_header(in, &info, &compressed, &size);
    if (status != 1 && size >= LZ_HEADER)
        return -1;
    if (compressed && info.Mode != 'A')
        return LZERROR_UNKNOWNALG;
    LZ_FILE *z = nullptr;
    BYTE *buffer = KbAlloc(0x8000);
    if (compressed)
        z = KbAllocZero(sizeof(*z));
    if (!buffer || (compressed && !z)) {
        KbFree(buffer);
        KbFree(z);
        return LZERROR_GLOBALLOC;
    }
    LONG total = 0;
    LONG result = 0;
    if (z) {
        z->File = in;
        z->Header = info;
        start_decoder(z);
        if (_llseek(out, 0, 0) != 0)
            result = LZERROR_BADOUTHANDLE;
    } else {
        _llseek(in, 0, 0);
        if (_llseek(out, 0, 0) != 0)
            result = LZERROR_BADOUTHANDLE;
    }
    while (!result) {
        INT got;
        if (z) {
            UINT left = info.Size - z->Produced;
            got = left ? decode_run(z, buffer, left < 0x8000 ? left : 0x8000) : 0;
        } else {
            UINT read = _lread(in, buffer, 0x8000);
            got = read == (UINT)HFILE_ERROR ? LZERROR_READ : (INT)read;
        }
        if (got < 0) {
            result = got;
            break;
        }
        if (!got)
            break;
        if (_lwrite(out, (LPCCH)buffer, got) != (UINT)got) {
            result = LZERROR_WRITE;
            break;
        }
        total += got;
    }
    if (!result && z && (DWORD)total != info.Size)
        result = LZERROR_READ;
    if (!result)
        copy_time(in, out);
    KbFree(buffer);
    KbFree(z);
    return result ? result : total;
}

DECLSPEC_EXPORT LONG WINAPI CopyLZFile(INT source, INT dest)
{
    return LZCopy(source, dest);
}

/* restores the missing last letter, in the case of the name's first letter. */
static void expanded_name(char *name, BYTE missing)
{
    char *part = name;
    for (char *p = name; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':')
            part = p + 1;
    char *dot = part;
    while (*dot && *dot != '.')
        dot++;
    if (!*dot)
        return;
    BYTE letter = missing;
    if ((missing >= 'A' && missing <= 'Z') || (missing >= 'a' && missing <= 'z')) {
        char *p = name;
        while (*p && !((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')))
            p++;
        if (*p >= 'a' && *p <= 'z' && missing <= 'Z')
            letter = missing + 32;
        else if (*p >= 'A' && *p <= 'Z' && missing >= 'a')
            letter = missing - 32;
    }
    char *extension = dot + 1;
    SIZE_T length = strlen(extension);
    if (length && extension[length - 1] == '_')
        extension[length - 1] = (char)letter;
    /* a zero missing letter leaves an empty extension, so the dot goes too. */
    if (!*extension && extension[-1] == '.')
        extension[-1] = 0;
}

DECLSPEC_EXPORT INT WINAPI GetExpandedNameA(LPSTR source, LPSTR buffer)
{
    OFSTRUCT record = {};
    HFILE file = OpenFile(source, &record, OF_READ);
    if (file == HFILE_ERROR)
        return LZERROR_BADVALUE;
    LZ_HEADER_INFO info;
    BOOL compressed;
    DWORD size;
    INT status = read_header(file, &info, &compressed, &size);
    _lclose(file);
    if (status != 1 && size >= LZ_HEADER)
        return LZERROR_BADVALUE;
    if (strlen(source) >= 260)
        return LZERROR_BADVALUE;
    strcpy(buffer, source);
    if (status == 1 && compressed)
        expanded_name(buffer, info.Missing);
    return 1;
}

DECLSPEC_EXPORT INT WINAPI GetExpandedNameW(LPWSTR source, LPWSTR buffer)
{
    char narrow[272], out[272];
    if (!WideCharToMultiByte(CP_ACP, 0, source, -1, narrow, sizeof(narrow), nullptr, nullptr))
        return LZERROR_GLOBALLOC;
    INT status = GetExpandedNameA(narrow, out);
    if (status == -1)
        return status;
    if (status < 0)
        return LZERROR_BADVALUE;
    if (!MultiByteToWideChar(CP_ACP, 0, out, -1, buffer, 260))
        return LZERROR_GLOBALLOC;
    return status;
}

DECLSPEC_EXPORT INT WINAPI LZStart(void)
{
    return 1;
}

DECLSPEC_EXPORT void WINAPI LZDone(void)
{
}
