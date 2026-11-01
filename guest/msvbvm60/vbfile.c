/*
 * the files of a program: Open and Close, Line Input, Input #, Print #, Write #, Get and Put, and the functions that ask where
 * a file is. a file has a mode, 1 to read in text, 2 to write it, 8 to append, 4 for records and 32 for bytes.
 */
#include "vb.h"

typedef struct VbFile {
    HANDLE handle;
    BYTE mode;
    LONG record_length;
    LONG width;
    LONG column;
    /* the number of the record a Get or Put without one takes next. */
    LONG record;
    /* bytes read ahead, which are what lies ahead of the file position of the handle. */
    BYTE *buffer;
    DWORD read_position, read_length;
} VbFile;

#define MAX_FILES 512
#define READ_AHEAD 4096
static VbFile g_files[MAX_FILES + 1];

static VbFile *file_of(LONG number)
{
    if (number < 1 || number > MAX_FILES || !g_files[number].handle)
        VbRaise(VBERR_BAD_FILE_NUMBER);
    return &g_files[number];
}

/* the bytes read ahead are given back, so that the handle stands where the program thinks the file does. */
static void unread(VbFile *file)
{
    if (file->read_length > file->read_position)
        SetFilePointer(file->handle, -(LONG)(file->read_length - file->read_position), nullptr, FILE_CURRENT);
    file->read_position = file->read_length = 0;
}

static LONG tell(VbFile *file)
{
    LONG position = (LONG)SetFilePointer(file->handle, 0, nullptr, FILE_CURRENT);
    return position - (LONG)(file->read_length - file->read_position);
}

static void seek_to(VbFile *file, LONG position)
{
    file->read_position = file->read_length = 0;
    SetFilePointer(file->handle, position, nullptr, FILE_BEGIN);
}

static LONG file_size(VbFile *file)
{
    return (LONG)GetFileSize(file->handle, nullptr);
}

/* the next byte of a text file, or -1 at its end, which a control Z also is. */
static int peek_byte(VbFile *file)
{
    if (file->read_position == file->read_length) {
        if (!file->buffer)
            file->buffer = HeapAlloc(GetProcessHeap(), 0, READ_AHEAD);
        DWORD got = 0;
        file->read_position = file->read_length = 0;
        if (!file->buffer || !ReadFile(file->handle, file->buffer, READ_AHEAD, &got, nullptr) || got == 0)
            return -1;
        file->read_length = got;
    }
    int c = file->buffer[file->read_position];
    return c == 0x1A && file->mode == 1 ? -1 : c;
}

static int read_byte(VbFile *file)
{
    int c = peek_byte(file);
    if (c >= 0)
        file->read_position++;
    return c;
}

/* ------------------------------------------------------------------ Open and Close */

/* Open: the mode in the low byte of the first word, the access and the sharing above it. */
VB_STD void vb___vbaFileOpen(SHORT mode_word, SHORT record_length, SHORT number, const WCHAR *name)
{
    BYTE mode = (BYTE)mode_word;
    ULONG access_bits = (USHORT)mode_word & 0xF00;
    ULONG share_bits = (USHORT)mode_word & 0xF000;
    if (number < 1 || number > MAX_FILES)
        VbRaise(VBERR_BAD_FILE_NUMBER);
    if (g_files[number].handle)
        VbRaise(VBERR_FILE_ALREADY_OPEN);
    if (record_length != -1 && record_length <= 0)
        VbRaise(VBERR_INVALID_CALL);
    ULONG access = GENERIC_READ | GENERIC_WRITE, creation = OPEN_ALWAYS;
    switch (mode) {
    case 1: access = GENERIC_READ; creation = OPEN_EXISTING; break;
    case 2: access = GENERIC_WRITE; creation = CREATE_ALWAYS; break;
    case 8: access = GENERIC_WRITE; creation = OPEN_ALWAYS; break;
    case 4:
    case 32: break;
    default: VbRaise(VBERR_INVALID_CALL);
    }
    if (access_bits == 0x100)
        access = GENERIC_READ;
    else if (access_bits == 0x200)
        access = GENERIC_WRITE;
    ULONG share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    if (share_bits == 0x1000)
        share = FILE_SHARE_READ;
    else if (share_bits == 0x2000)
        share = FILE_SHARE_WRITE;
    else if (share_bits == 0x3000)
        share = 0;
    HANDLE handle = CreateFileW(name, access, share, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND: VbRaise(VBERR_FILE_NOT_FOUND);
        case ERROR_PATH_NOT_FOUND: VbRaise(VBERR_PATH_NOT_FOUND);
        case ERROR_ACCESS_DENIED: VbRaise(70);
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION: VbRaise(70);
        case ERROR_INVALID_NAME: VbRaise(VBERR_BAD_FILE_NAME);
        default: VbRaise(VBERR_IO);
        }
    }
    if (mode == 8)
        SetFilePointer(handle, 0, nullptr, FILE_END);
    VbFile *file = &g_files[number];
    file->handle = handle;
    file->mode = mode;
    file->record_length = mode == 32 ? 1 : record_length == -1 ? (mode == 4 ? 128 : 512) : record_length;
    file->width = 0;
    file->column = 0;
    file->record = 1;
    file->read_position = file->read_length = 0;
}

static void close_file(VbFile *file)
{
    CloseHandle(file->handle);
    HeapFree(GetProcessHeap(), 0, file->buffer);
    memset(file, 0, sizeof(*file));
}

VB_STD void vb___vbaFileClose(SHORT number)
{
    if (number == 0) {
        for (int i = 1; i <= MAX_FILES; i++)
            if (g_files[i].handle)
                close_file(&g_files[i]);
        return;
    }
    close_file(file_of(number));
}

VB_STD void vb___vbaFileCloseAll(void) { vb___vbaFileClose(0); }

/* Reset closes every file. */
VB_STD void vb_rtcFileReset(void) { vb___vbaFileClose(0); }

/* FreeFile hands out the lowest number that is free, in the range 1 to 255 or 256 to 511. */
VB_STD SHORT vb_rtcFreeFile(VARIANT *range)
{
    int low = 1, high = 255;
    if (range && !(V_VT(range) == VT_ERROR && V_ERROR(range) == DISP_E_PARAMNOTFOUND)) {
        VARIANT v;
        VariantInit(&v);
        VbCheck(VariantChangeTypeEx(&v, range, VbLocale(), 0, VT_I4));
        if (V_I4(&v) == 1) {
            low = 256;
            high = 511;
        } else if (V_I4(&v) != 0) {
            VbRaise(VBERR_INVALID_CALL);
        }
    }
    for (int i = low; i <= high; i++)
        if (!g_files[i].handle)
            return (SHORT)i;
    VbRaise(67);
}

/* ------------------------------------------------------------------ where a file is */

static VbFile *seekable_file(SHORT number)
{
    return file_of(number);
}

VB_STD SHORT vb_rtcEndOfFile(SHORT number)
{
    VbFile *file = seekable_file(number);
    if (file->mode == 2 || file->mode == 8)
        VbRaise(54);
    if (file->mode == 4 || file->mode == 32)
        return tell(file) >= file_size(file) ? -1 : 0;
    return peek_byte(file) < 0 ? -1 : 0;
}

VB_STD LONG vb_rtcFileLength(SHORT number)
{
    VbFile *file = seekable_file(number);
    return file_size(file);
}

/* Loc is the number of the record last read or written, the byte last read or written, or the block of 128 bytes in a text file. */
VB_STD LONG vb_rtcFileLocation(SHORT number)
{
    VbFile *file = seekable_file(number);
    if (file->mode == 4)
        return file->record - 1;
    LONG position = tell(file);
    if (file->mode == 32)
        return position;
    return (position + 127) / 128;
}

VB_STD LONG vb_rtcFileSeek(SHORT number)
{
    VbFile *file = seekable_file(number);
    if (file->mode == 4)
        return file->record;
    return tell(file) + 1;
}

/* the Seek statement: the next record, or the next byte. */
VB_STD void vb___vbaFileSeek(LONG position, SHORT number)
{
    VbFile *file = seekable_file(number);
    if (position < 1)
        VbRaise(63);
    if (file->mode == 4) {
        file->record = position;
        return;
    }
    seek_to(file, position - 1);
}

VB_STD void vb_rtcFileWidth(SHORT number, SHORT width)
{
    VbFile *file = file_of(number);
    if ((USHORT)width >= 0x100)
        VbRaise(VBERR_INVALID_CALL);
    file->width = width;
}

/* ------------------------------------------------------------------ text in */

/* one line of a file in the ansi code page, without its end of line. */
static BSTR ansi_to_bstr(const char *text, int length)
{
    int wide = length ? MultiByteToWideChar(CP_ACP, 0, text, length, nullptr, 0) : 0;
    BSTR result = SysAllocStringLen(nullptr, (UINT)wide);
    if (!result)
        VbRaise(VBERR_OUT_OF_MEMORY);
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, text, length, result, wide);
    return result;
}

typedef struct Bytes {
    char *data;
    DWORD length, capacity;
} Bytes;

static void bytes_add(Bytes *b, char c)
{
    if (b->length + 1 >= b->capacity) {
        DWORD capacity = b->capacity ? b->capacity * 2 : 128;
        char *larger = b->data ? HeapReAlloc(GetProcessHeap(), 0, b->data, capacity) : HeapAlloc(GetProcessHeap(), 0, capacity);
        if (!larger)
            VbRaise(VBERR_OUT_OF_MEMORY);
        b->data = larger;
        b->capacity = capacity;
    }
    b->data[b->length++] = c;
}

static void bytes_free(Bytes *b) { HeapFree(GetProcessHeap(), 0, b->data); }

static BSTR read_line(VbFile *file)
{
    Bytes line = {};
    BOOL any = FALSE;
    for (;;) {
        int c = read_byte(file);
        if (c < 0)
            break;
        any = TRUE;
        if (c == '\n')
            break;
        if (c == '\r') {
            if (peek_byte(file) == '\n')
                read_byte(file);
            break;
        }
        bytes_add(&line, (char)c);
    }
    if (!any) {
        bytes_free(&line);
        VbRaise(VBERR_END_OF_FILE);
    }
    BSTR text = ansi_to_bstr(line.data, (int)line.length);
    bytes_free(&line);
    return text;
}

static VbFile *text_input_file(SHORT number)
{
    VbFile *file = file_of(number);
    if (file->mode != 1 && file->mode != 4 && file->mode != 32)
        VbRaise(54);
    return file;
}

VB_STD BSTR vb___vbaLineInputStr(BSTR *slot, SHORT number)
{
    VbFile *file = text_input_file(number);
    BSTR text = read_line(file);
    SysFreeString(*slot);
    *slot = text;
    return text;
}

VB_STD BSTR vb___vbaLineInputVar(VARIANT *v, SHORT number)
{
    if (V_VT(v) == VT_BSTR)
        return vb___vbaLineInputStr(&V_BSTR(v), number);
    if (V_VT(v) & VT_BYREF) {
        if (V_VT(v) != (VT_BSTR | VT_BYREF))
            VbRaise(VBERR_TYPE_MISMATCH);
        return vb___vbaLineInputStr(v->pbstrVal, number);
    }
    VariantClear(v);
    V_VT(v) = VT_BSTR;
    V_BSTR(v) = nullptr;
    return vb___vbaLineInputStr(&V_BSTR(v), number);
}

/* Input$(n, #file) reads n bytes. */
VB_STD BSTR vb_rtcInputCount(LONG count, SHORT number)
{
    if (count < 0 || count >= 0x40000000)
        VbRaise(VBERR_INVALID_CALL);
    VbFile *file = text_input_file(number);
    Bytes bytes = {};
    for (LONG i = 0; i < count; i++) {
        int c = read_byte(file);
        if (c < 0) {
            bytes_free(&bytes);
            VbRaise(VBERR_END_OF_FILE);
        }
        bytes_add(&bytes, (char)c);
    }
    BSTR text = ansi_to_bstr(bytes.data, (int)bytes.length);
    bytes_free(&bytes);
    return text;
}

/*
 * one field of Input #: a text in quotes, or what stands up to the next comma or end of line, with the spaces around it left out.
 * the field says whether it was a text in quotes, which stays one whatever the variable asks for.
 */
static BSTR read_field(VbFile *file, BOOL *quoted, BOOL *end_of_line)
{
    int c;
    *quoted = FALSE;
    *end_of_line = FALSE;
    do
        c = read_byte(file);
    while (c == ' ' || c == '\t');
    if (c < 0)
        VbRaise(VBERR_END_OF_FILE);
    Bytes field = {};
    if (c == '\r' || c == '\n') {
        /* an empty field at the end of a line. */
        if (c == '\r' && peek_byte(file) == '\n')
            read_byte(file);
        *end_of_line = TRUE;
    } else if (c == ',') {
        /* an empty field. */
    } else if (c == '"') {
        *quoted = TRUE;
        for (;;) {
            c = read_byte(file);
            if (c < 0)
                break;
            if (c == '"') {
                if (peek_byte(file) == '"') {
                    read_byte(file);
                    bytes_add(&field, '"');
                    continue;
                }
                break;
            }
            bytes_add(&field, (char)c);
        }
        for (;;) {
            c = peek_byte(file);
            if (c == ' ' || c == '\t') {
                read_byte(file);
                continue;
            }
            if (c == ',') {
                read_byte(file);
            } else if (c == '\r' || c == '\n') {
                read_byte(file);
                if (c == '\r' && peek_byte(file) == '\n')
                    read_byte(file);
                *end_of_line = TRUE;
            }
            break;
        }
    } else {
        while (c >= 0 && c != ',' && c != '\r' && c != '\n') {
            bytes_add(&field, (char)c);
            c = read_byte(file);
        }
        if (c == '\r' || c == '\n') {
            if (c == '\r' && peek_byte(file) == '\n')
                read_byte(file);
            *end_of_line = TRUE;
        }
        while (field.length && (field.data[field.length - 1] == ' ' || field.data[field.length - 1] == '\t'))
            field.length--;
    }
    BSTR text = ansi_to_bstr(field.data, (int)field.length);
    bytes_free(&field);
    return text;
}

static const LCID LCID_ENGLISH = 0x0409;

static BOOL starts_with(const WCHAR *text, const WCHAR *prefix)
{
    while (*prefix)
        if (*text++ != *prefix++)
            return FALSE;
    return TRUE;
}

static LONG to_long(const WCHAR *text)
{
    LONG n = 0;
    while (*text >= L'0' && *text <= L'9')
        n = n * 10 + (*text++ - L'0');
    return n;
}

/* a number written with a point or an exponent is not a whole one. */
static BOOL has_mark(const WCHAR *text)
{
    for (; *text; text++)
        if (*text == L'.' || *text == L'e' || *text == L'E')
            return TRUE;
    return FALSE;
}

/* a field becomes the value of the type the variable has. */
static void field_to_variant(BSTR text, BOOL quoted, int type, VARIANT *out)
{
    VariantInit(out);
    if (type == VT_BSTR || (quoted && type == VT_VARIANT)) {
        V_VT(out) = VT_BSTR;
        V_BSTR(out) = text;
        return;
    }
    UINT length = SysStringLen(text);
    if (type == VT_VARIANT) {
        if (length >= 2 && text[0] == L'#' && text[length - 1] == L'#') {
            BSTR inner = SysAllocStringLen(text + 1, length - 2);
            if (!lstrcmpiW(inner, L"NULL")) {
                V_VT(out) = VT_NULL;
            } else if (!lstrcmpiW(inner, L"TRUE") || !lstrcmpiW(inner, L"FALSE")) {
                V_VT(out) = VT_BOOL;
                V_BOOL(out) = lstrcmpiW(inner, L"TRUE") ? VARIANT_FALSE : VARIANT_TRUE;
            } else if (starts_with(inner, L"ERROR ")) {
                V_VT(out) = VT_ERROR;
                V_ERROR(out) = (SCODE)(0x800A0000u | (ULONG)to_long(inner + 6));
            } else {
                double date;
                if (SUCCEEDED(VarDateFromStr(inner, LCID_ENGLISH, 0, &date))) {
                    V_VT(out) = VT_DATE;
                    V_DATE(out) = date;
                } else {
                    V_VT(out) = VT_BSTR;
                    V_BSTR(out) = SysAllocString(text);
                }
            }
            SysFreeString(inner);
            SysFreeString(text);
            return;
        }
        double number;
        if (length && SUCCEEDED(VarR8FromStr(text, LCID_ENGLISH, 0, &number))) {
            BOOL plain = !has_mark(text);
            if (number == (double)(SHORT)number && plain) {
                V_VT(out) = VT_I2;
                V_I2(out) = (SHORT)number;
            } else if (number == (double)(LONG)number && plain) {
                V_VT(out) = VT_I4;
                V_I4(out) = (LONG)number;
            } else {
                V_VT(out) = VT_R8;
                V_R8(out) = number;
            }
            SysFreeString(text);
            return;
        }
        V_VT(out) = VT_BSTR;
        V_BSTR(out) = text;
        return;
    }
    VARIANT in;
    VariantInit(&in);
    V_VT(&in) = VT_BSTR;
    V_BSTR(&in) = text;
    if (type == VT_BOOL) {
        if (!lstrcmpiW(text, L"#TRUE#") || !lstrcmpiW(text, L"True")) {
            V_VT(out) = VT_BOOL;
            V_BOOL(out) = VARIANT_TRUE;
        } else if (!lstrcmpiW(text, L"#FALSE#") || !lstrcmpiW(text, L"False")) {
            V_VT(out) = VT_BOOL;
            V_BOOL(out) = VARIANT_FALSE;
        } else {
            double number = 0;
            VarR8FromStr(text, LCID_ENGLISH, 0, &number);
            V_VT(out) = VT_BOOL;
            V_BOOL(out) = number != 0 ? VARIANT_TRUE : VARIANT_FALSE;
        }
        SysFreeString(text);
        return;
    }
    if (type == VT_DATE && length >= 2 && text[0] == L'#') {
        BSTR inner = SysAllocStringLen(text + 1, length - 2);
        V_VT(&in) = VT_BSTR;
        V_BSTR(&in) = inner;
        HRESULT hr = VariantChangeTypeEx(out, &in, LCID_ENGLISH, 0, VT_DATE);
        SysFreeString(inner);
        SysFreeString(text);
        if (FAILED(hr)) {
            V_VT(out) = VT_DATE;
            V_DATE(out) = 0;
        }
        return;
    }
    /* a number that does not read as one is zero. */
    if (FAILED(VariantChangeTypeEx(out, &in, LCID_ENGLISH, 0, (VARTYPE)type))) {
        VariantInit(out);
        V_VT(out) = (VARTYPE)type;
    }
    SysFreeString(text);
}

/* Input #n, a, b, c: the descriptor says the type of each variable, and the variables follow as pointers. */
VB_C void vb___vbaInputFile(const USHORT *desc, int number_word, ...)
{
    VbFile *file = text_input_file((SHORT)number_word);
    __builtin_va_list list;
    __builtin_va_start(list, number_word);
    const BYTE *types = (const BYTE *)(desc + 1);
    for (int i = 0; i < desc[0]; i++) {
        int type = types[i] & 0x3F;
        void *target = __builtin_va_arg(list, void *);
        BOOL quoted, end;
        BSTR text = read_field(file, &quoted, &end);
        VARIANT value;
        field_to_variant(text, quoted, type, &value);
        switch (type) {
        case VT_VARIANT:
            VariantClear((VARIANT *)target);
            *(VARIANT *)target = value;
            break;
        case VT_BSTR:
            SysFreeString(*(BSTR *)target);
            *(BSTR *)target = V_BSTR(&value);
            break;
        case VT_I2:
        case VT_BOOL: *(SHORT *)target = V_I2(&value); break;
        case VT_I4: *(LONG *)target = V_I4(&value); break;
        case VT_R4: *(float *)target = V_R4(&value); break;
        case VT_R8:
        case VT_DATE: *(double *)target = V_R8(&value); break;
        case VT_CY: *(CY *)target = V_CY(&value); break;
        case 0x11: *(BYTE *)target = V_UI1(&value); break;
        default: VariantClear(&value); break;
        }
    }
    __builtin_va_end(list);
}

/* ------------------------------------------------------------------ text out */

static void write_text(VbFile *file, const WCHAR *text, int length)
{
    if (length <= 0)
        return;
    unread(file);
    char stack[256];
    char *bytes = stack;
    int size = WideCharToMultiByte(CP_ACP, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (size > (int)sizeof(stack))
        bytes = HeapAlloc(GetProcessHeap(), 0, size);
    if (!bytes)
        VbRaise(VBERR_OUT_OF_MEMORY);
    WideCharToMultiByte(CP_ACP, 0, text, length, bytes, size, nullptr, nullptr);
    DWORD wrote;
    BOOL ok = WriteFile(file->handle, bytes, (DWORD)size, &wrote, nullptr);
    for (int i = 0; i < size; i++)
        file->column = bytes[i] == '\n' ? 0 : file->column + 1;
    if (bytes != stack)
        HeapFree(GetProcessHeap(), 0, bytes);
    if (!ok || wrote != (DWORD)size)
        VbRaise(VBERR_IO);
}

static void write_spaces(VbFile *file, int count)
{
    WCHAR spaces[64];
    for (int i = 0; i < 64; i++)
        spaces[i] = L' ';
    while (count > 0) {
        int n = count < 64 ? count : 64;
        write_text(file, spaces, n);
        count -= n;
    }
}

/* a line that has a width does not take a text that would go past it. */
static BOOL wraps(const VbFile *file, int length)
{
    return file->width && file->column && file->column + length > file->width;
}

/* Spc moves on by a number of columns, Tab to a column of the line. */
static void move_column(VbFile *file, int count, BOOL absolute)
{
    int column = file->column;
    int target = count;
    int width = file->width;
    if (width) {
        if (target >= width)
            target %= width;
        if (!absolute && column + target > width) {
            target = column + target - width;
            write_text(file, L"\r\n", 2);
            column = 0;
        } else if (!absolute) {
            target += column;
        }
    } else if (!absolute) {
        target += column;
    }
    if (target < column) {
        write_text(file, L"\r\n", 2);
        column = 0;
    }
    write_spaces(file, target - column);
}

static BSTR date_for_write(double date)
{
    SYSTEMTIME st;
    WCHAR text[64];
    if (!VariantTimeToSystemTime(date, &st))
        VbRaise(VBERR_OVERFLOW);
    BOOL has_date = date <= -1.0 || date >= 1.0, has_time = st.wHour || st.wMinute || st.wSecond;
    if (has_date && has_time)
        wsprintfW(text, L"#%04d-%02d-%02d %02d:%02d:%02d#", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    else if (has_date)
        wsprintfW(text, L"#%04d-%02d-%02d#", st.wYear, st.wMonth, st.wDay);
    else
        wsprintfW(text, L"#%02d:%02d:%02d#", st.wHour, st.wMinute, st.wSecond);
    return SysAllocString(text);
}

/* one value of a Print or Write, with what separates it from the next: 0 a tab stop, 1 nothing, 2 the end of the line. */
static void put_value(VbFile *file, BOOL write, VARIANT *v, int separator)
{
    BSTR text = nullptr;
    BOOL numeric = FALSE, quote = FALSE, own = FALSE;
    VARIANT converted;
    VariantInit(&converted);
    switch (V_VT(v)) {
    case VT_DISPATCH:
    case VT_UNKNOWN: VbRaise(VBERR_TYPE_MISMATCH);
    case VT_EMPTY: break;
    case VT_NULL: text = SysAllocString(write ? L"#NULL#" : L"Null"); own = TRUE; break;
    case VT_BOOL: text = SysAllocString(write ? (V_BOOL(v) ? L"#TRUE#" : L"#FALSE#") : (V_BOOL(v) ? L"True" : L"False")); own = TRUE; break;
    case VT_ERROR: {
        WCHAR t[64];
        LONG number = VbErrorOfHresult(V_ERROR(v));
        if (number == VBERR_APPLICATION_DEFINED && V_ERROR(v) != (SCODE)0x800A01A8)
            number = V_ERROR(v) & 0xFFFF ? V_ERROR(v) & 0xFFFF : number;
        wsprintfW(t, write ? L"#ERROR %d#" : L"Error %d", (int)number);
        text = SysAllocString(t);
        own = TRUE;
        break;
    }
    case VT_BSTR: text = V_BSTR(v); quote = write; break;
    case VT_DATE:
        if (write) {
            text = date_for_write(V_DATE(v));
            own = TRUE;
            break;
        }
        VbCheck(VariantChangeTypeEx(&converted, v, VbLocale(), 0, VT_BSTR));
        text = V_BSTR(&converted);
        own = TRUE;
        {
            UINT length = SysStringLen(text);
            BSTR spaced = SysAllocStringLen(text, length + 1);
            spaced[length] = L' ';
            SysFreeString(text);
            text = spaced;
        }
        break;
    case VT_I2:
    case VT_I4:
    case VT_R4:
    case VT_R8:
    case VT_CY:
    case VT_DECIMAL:
    case VT_UI1: numeric = TRUE; [[fallthrough]];
    default:
        VbCheck(VariantChangeTypeEx(&converted, v, write ? LCID_ENGLISH : VbLocale(), 0, VT_BSTR));
        text = V_BSTR(&converted);
        own = TRUE;
        if (numeric && !write) {
            UINT length = SysStringLen(text);
            BOOL minus = length && text[0] == L'-';
            BSTR spaced = SysAllocStringLen(nullptr, length + (minus ? 1 : 2));
            int k = 0;
            if (!minus)
                spaced[k++] = L' ';
            memcpy(spaced + k, text, length * sizeof(WCHAR));
            spaced[k + length] = L' ';
            SysFreeString(text);
            text = spaced;
        }
        break;
    }
    int length = text ? (int)SysStringLen(text) : 0;
    if (!write && wraps(file, length))
        write_text(file, L"\r\n", 2);
    if (quote) {
        write_text(file, L"\"", 1);
        int from = 0;
        for (int i = 0; i < length; i++) {
            if (text[i] == L'"') {
                write_text(file, text + from, i + 1 - from);
                write_text(file, L"\"", 1);
                from = i + 1;
            }
        }
        write_text(file, text + from, length - from);
        write_text(file, L"\"", 1);
    } else {
        write_text(file, text, length);
    }
    if (own)
        SysFreeString(text);
    VariantClear(&converted);
    if (separator == 2) {
        write_text(file, L"\r\n", 2);
    } else if (write) {
        write_text(file, L",", 1);
    } else if (separator == 0) {
        int distance = 14 - file->column % 14;
        if (wraps(file, distance + 14))
            write_text(file, L"\r\n", 2);
        else
            write_spaces(file, distance);
    }
}

/*
 * Print # and Write #: a descriptor of a count and one byte for each item, the type in the low six bits and what follows the
 * item above them, then the items on the stack, each as wide as its type. Spc and Tab have types of their own.
 */
static void put_items(VbFile *file, BOOL write, const USHORT *desc, char *args)
{
    const BYTE *types = (const BYTE *)(desc + 1);
    for (int i = 0; i < desc[0]; i++) {
        int type = types[i] & 0x3F, separator = types[i] >> 6;
        VARIANT v;
        VariantInit(&v);
        BOOL by_pointer = FALSE;
        VARIANT *pointer = nullptr;
        switch (type) {
        case 0x20:
        case 0x21: {
            SHORT n = *(SHORT *)args;
            args += 4;
            if (type == 0x20)
                move_column(file, n < 0 ? 0 : n, FALSE);
            else
                move_column(file, (n < 1 ? 1 : n) - 1, TRUE);
            continue;
        }
        case VT_I2:
        case VT_BOOL: V_VT(&v) = (VARTYPE)type; V_I2(&v) = *(SHORT *)args; args += 4; break;
        case VT_I4:
        case VT_BSTR:
        case VT_DISPATCH:
        case VT_ERROR: V_VT(&v) = (VARTYPE)type; V_I4(&v) = *(LONG *)args; args += 4; break;
        case VT_R4: V_VT(&v) = VT_R4; V_R4(&v) = *(float *)args; args += 4; break;
        case VT_R8:
        case VT_DATE: V_VT(&v) = (VARTYPE)type; V_R8(&v) = *(double *)args; args += 8; break;
        case VT_CY: V_VT(&v) = VT_CY; V_CY(&v) = *(CY *)args; args += 8; break;
        case 0x11: V_VT(&v) = VT_UI1; V_UI1(&v) = *(BYTE *)args; args += 4; break;
        case VT_VARIANT: pointer = *(VARIANT **)args; args += 4; by_pointer = TRUE; break;
        default: VbRaise(VBERR_TYPE_MISMATCH);
        }
        if (by_pointer) {
            VARIANT *p = pointer;
            while (V_VT(p) == (VT_VARIANT | VT_BYREF) && V_VARIANTREF(p))
                p = V_VARIANTREF(p);
            if (V_VT(p) & VT_BYREF) {
                VariantInit(&v);
                VbCheck(VariantCopyInd(&v, p));
                put_value(file, write, &v, separator);
                VariantClear(&v);
            } else {
                put_value(file, write, p, separator);
            }
        } else {
            put_value(file, write, &v, separator);
        }
    }
}

static VbFile *output_file(SHORT number)
{
    VbFile *file = file_of(number);
    if (file->mode & 0x25)
        VbRaise(54);
    return file;
}

VB_C void vb___vbaPrintFile(const USHORT *desc, int number_word, ...)
{
    VbFile *file = output_file((SHORT)number_word);
    __builtin_va_list list;
    __builtin_va_start(list, number_word);
    put_items(file, FALSE, desc, (char *)list);
    __builtin_va_end(list);
}

VB_C void vb___vbaWriteFile(const USHORT *desc, int number_word, ...)
{
    VbFile *file = output_file((SHORT)number_word);
    __builtin_va_list list;
    __builtin_va_start(list, number_word);
    put_items(file, TRUE, desc, (char *)list);
    __builtin_va_end(list);
}

/* ------------------------------------------------------------------ Get and Put */

/*
 * a Get or Put works on one record of a file of records, or on the bytes at the position of a file of bytes. a record is read
 * whole first, the items take their bytes from it in turn, and a Put writes it back whole.
 */
typedef struct Transfer {
    VbFile *file;
    BOOL get;
    LONG record;
    BYTE *bytes;
    LONG used;
    /* the items of a record, whose strings keep their lengths even in a file of bytes. */
    BOOL in_record;
} Transfer;

static void transfer_begin(Transfer *t, SHORT number, LONG record, BOOL has_record, BOOL get)
{
    VbFile *file = file_of(number);
    t->file = file;
    t->get = get;
    t->used = 0;
    t->bytes = nullptr;
    t->in_record = FALSE;
    if (file->mode != 4 && file->mode != 32)
        VbRaise(54);
    if (file->mode == 32) {
        if (has_record) {
            if (record < 1)
                VbRaise(63);
            seek_to(file, record - 1);
        } else {
            unread(file);
        }
        return;
    }
    t->record = has_record ? record : file->record;
    if (t->record < 1)
        VbRaise(63);
    t->bytes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, file->record_length);
    if (!t->bytes)
        VbRaise(VBERR_OUT_OF_MEMORY);
    seek_to(file, (t->record - 1) * file->record_length);
    if (get) {
        DWORD got;
        ReadFile(file->handle, t->bytes, (DWORD)file->record_length, &got, nullptr);
    } else {
        /* a record that is overwritten keeps what the new items do not reach. */
        DWORD got;
        ReadFile(file->handle, t->bytes, (DWORD)file->record_length, &got, nullptr);
    }
}

static void transfer_bytes(Transfer *t, void *data, LONG size)
{
    VbFile *file = t->file;
    if (file->mode == 32) {
        DWORD done;
        BOOL ok;
        if (t->get) {
            memset(data, 0, (size_t)size);
            ok = ReadFile(file->handle, data, (DWORD)size, &done, nullptr);
        } else {
            ok = WriteFile(file->handle, data, (DWORD)size, &done, nullptr);
        }
        if (!ok)
            VbRaise(VBERR_IO);
        return;
    }
    if (t->used + size > file->record_length)
        VbRaise(59);
    if (t->get)
        memcpy(data, t->bytes + t->used, (size_t)size);
    else
        memcpy(t->bytes + t->used, data, (size_t)size);
    t->used += size;
}

static void transfer_end(Transfer *t)
{
    VbFile *file = t->file;
    if (file->mode == 4) {
        if (!t->get) {
            seek_to(file, (t->record - 1) * file->record_length);
            DWORD wrote;
            if (!WriteFile(file->handle, t->bytes, (DWORD)file->record_length, &wrote, nullptr))
                VbRaise(VBERR_IO);
        }
        file->record = t->record + 1;
    }
    HeapFree(GetProcessHeap(), 0, t->bytes);
}

/* a string in a file of records has its length in front, in two bytes, or in six when it is long. */
static void transfer_string(Transfer *t, BSTR *slot)
{
    VbFile *file = t->file;
    if (file->mode == 32 && !t->in_record) {
        /* in a file of bytes the string has the length it has, and a Get fills the string it finds. */
        LONG length = *slot ? (LONG)SysStringLen(*slot) : 0;
        if (t->get) {
            char *bytes = HeapAlloc(GetProcessHeap(), 0, length + 1);
            transfer_bytes(t, bytes, length);
            BSTR text = ansi_to_bstr(bytes, length);
            HeapFree(GetProcessHeap(), 0, bytes);
            SysFreeString(*slot);
            *slot = text;
        } else {
            int size = length ? WideCharToMultiByte(CP_ACP, 0, *slot, length, nullptr, 0, nullptr, nullptr) : 0;
            char *bytes = HeapAlloc(GetProcessHeap(), 0, size + 1);
            if (size)
                WideCharToMultiByte(CP_ACP, 0, *slot, length, bytes, size, nullptr, nullptr);
            transfer_bytes(t, bytes, size);
            HeapFree(GetProcessHeap(), 0, bytes);
        }
        return;
    }
    if (t->get) {
        USHORT prefix;
        LONG length;
        transfer_bytes(t, &prefix, 2);
        if (prefix == 0xFFFF)
            transfer_bytes(t, &length, 4);
        else
            length = prefix;
        char *bytes = HeapAlloc(GetProcessHeap(), 0, length + 1);
        transfer_bytes(t, bytes, length);
        BSTR text = ansi_to_bstr(bytes, length);
        HeapFree(GetProcessHeap(), 0, bytes);
        SysFreeString(*slot);
        *slot = text;
    } else {
        LONG length = *slot ? (LONG)SysStringLen(*slot) : 0;
        int size = length ? WideCharToMultiByte(CP_ACP, 0, *slot, length, nullptr, 0, nullptr, nullptr) : 0;
        char *bytes = HeapAlloc(GetProcessHeap(), 0, size + 1);
        if (size)
            WideCharToMultiByte(CP_ACP, 0, *slot, length, bytes, size, nullptr, nullptr);
        USHORT prefix = size >= 0xFFFF ? 0xFFFF : (USHORT)size;
        transfer_bytes(t, &prefix, 2);
        if (prefix == 0xFFFF)
            transfer_bytes(t, &size, 4);
        transfer_bytes(t, bytes, size);
        HeapFree(GetProcessHeap(), 0, bytes);
    }
}

/* a variant in a file has its type in front, in two bytes, and then what the type holds. */
static void transfer_variant(Transfer *t, VARIANT *v)
{
    USHORT type = t->get ? 0 : (USHORT)V_VT(v);
    if (!t->get && (type & VT_BYREF)) {
        VARIANT copy;
        VariantInit(&copy);
        VbCheck(VariantCopyInd(&copy, v));
        transfer_variant(t, &copy);
        VariantClear(&copy);
        return;
    }
    transfer_bytes(t, &type, 2);
    if (t->get) {
        VariantClear(v);
        V_VT(v) = (VARTYPE)type;
    }
    switch (type & 0xFFF) {
    case VT_EMPTY:
    case VT_NULL: break;
    case VT_I2:
    case VT_BOOL: transfer_bytes(t, &V_I2(v), 2); break;
    case VT_I4:
    case VT_R4:
    case VT_ERROR: transfer_bytes(t, &V_I4(v), 4); break;
    case VT_R8:
    case VT_DATE:
    case VT_CY: transfer_bytes(t, &V_R8(v), 8); break;
    case VT_UI1: transfer_bytes(t, &V_UI1(v), 1); break;
    case VT_DECIMAL: transfer_bytes(t, (BYTE *)v + 2, 14); break;
    case VT_BSTR:
        if (t->get)
            V_BSTR(v) = nullptr;
        transfer_string(t, &V_BSTR(v));
        break;
    default: VbRaise(VBERR_TYPE_MISMATCH);
    }
}

/* size 0 is a string, -1 a variant, and anything else that many bytes of the variable. */
static void get_or_put(SHORT number, LONG record, BOOL has_record, LONG size, void *data, BOOL get)
{
    Transfer t;
    transfer_begin(&t, number, record, has_record, get);
    if (size == 0)
        transfer_string(&t, (BSTR *)data);
    else if (size == -1)
        transfer_variant(&t, (VARIANT *)data);
    else
        transfer_bytes(&t, data, size);
    transfer_end(&t);
}

VB_STD void vb___vbaGet3(LONG size, void *data, SHORT number) { get_or_put(number, 0, FALSE, size, data, TRUE); }
VB_STD void vb___vbaGet4(LONG size, void *data, LONG record, SHORT number) { get_or_put(number, record, TRUE, size, data, TRUE); }
VB_STD void vb___vbaPut3(LONG size, void *data, SHORT number) { get_or_put(number, 0, FALSE, size, data, FALSE); }
VB_STD void vb___vbaPut4(LONG size, void *data, LONG record, SHORT number) { get_or_put(number, record, TRUE, size, data, FALSE); }

/* a string of a fixed length is that many characters, as bytes in the file. */
static void fixed_string(SHORT number, LONG record, BOOL has_record, LONG count, WCHAR *text, BOOL get)
{
    Transfer t;
    transfer_begin(&t, number, record, has_record, get);
    char *bytes = HeapAlloc(GetProcessHeap(), 0, count + 1);
    if (get) {
        transfer_bytes(&t, bytes, count);
        MultiByteToWideChar(CP_ACP, 0, bytes, count, text, count);
    } else {
        WideCharToMultiByte(CP_ACP, 0, text, count, bytes, count, nullptr, nullptr);
        transfer_bytes(&t, bytes, count);
    }
    HeapFree(GetProcessHeap(), 0, bytes);
    transfer_end(&t);
}

VB_STD void vb___vbaGetFxStr3(LONG count, WCHAR *text, SHORT number) { fixed_string(number, 0, FALSE, count, text, TRUE); }
VB_STD void vb___vbaGetFxStr4(LONG count, WCHAR *text, LONG record, SHORT number) { fixed_string(number, record, TRUE, count, text, TRUE); }
VB_STD void vb___vbaPutFxStr3(LONG count, WCHAR *text, SHORT number) { fixed_string(number, 0, FALSE, count, text, FALSE); }
VB_STD void vb___vbaPutFxStr4(LONG count, WCHAR *text, LONG record, SHORT number) { fixed_string(number, record, TRUE, count, text, FALSE); }

/* ------------------------------------------------------------------ records of the compiler

 * a record has a table of the compiler: its size, the number of items, and items of twelve bytes from the tenth byte on.
 * each says how many bytes lie in front of it, what it is, and the count or size that goes with it. the bytes in front of
 * an item and the rest after the last one go to the file as they are, an item of a kind that needs a conversion goes
 * through the conversion, and the padding the compiler added is skipped.
 */
static void transfer_fixed_string(Transfer *t, WCHAR *text, LONG chars)
{
    char *bytes = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)chars + 1);
    if (!bytes)
        VbRaise(VBERR_OUT_OF_MEMORY);
    if (t->get) {
        transfer_bytes(t, bytes, chars);
        MultiByteToWideChar(CP_ACP, 0, bytes, chars, text, chars);
    } else {
        WideCharToMultiByte(CP_ACP, 0, text, chars, bytes, chars, nullptr, nullptr);
        transfer_bytes(t, bytes, chars);
    }
    HeapFree(GetProcessHeap(), 0, bytes);
}

static LONG transfer_owner(Transfer *t, const BYTE *table, BYTE *data)
{
    UINT32 size = *(const UINT32 *)table, count = *(const UINT32 *)(table + 4);
    const BYTE *item = table + 10;
    LONG done = 0;
    for (UINT32 i = 0; i < count; i++, item += 12) {
        const UINT16 *w = (const UINT16 *)item;
        UINT32 gap = *(const UINT32 *)item;
        UINT16 flags = w[2];
        UINT16 extra = w[5];
        UINT32 many = (flags & 0x10) ? *(const UINT32 *)(item + 6) : 1;
        if (gap) {
            transfer_bytes(t, data + done, (LONG)gap);
            done += (LONG)gap;
        }
        if (flags & 0x60)
            VbRaise(458);
        switch (flags & 0xF) {
        case 0:
            for (UINT32 k = 0; k < many; k++, done += 4)
                transfer_string(t, (BSTR *)(data + done));
            break;
        case 1:
            for (UINT32 k = 0; k < many; k++, done += 16)
                transfer_variant(t, (VARIANT *)(data + done));
            break;
        case 2: {
            const BYTE *nested = item + extra;
            for (UINT32 k = 0; k < many; k++)
                done += transfer_owner(t, nested, data + done);
            break;
        }
        case 3:
            for (UINT32 k = 0; k < many; k++, done += extra)
                transfer_bytes(t, data + done, extra);
            break;
        case 4:
            for (UINT32 k = 0; k < many; k++, done += flags >> 8)
                transfer_bytes(t, data + done, flags >> 8);
            break;
        case 5: done += (LONG)many * (flags >> 8); break;
        case 6:
            for (UINT32 k = 0; k < many; k++, done += 2 * extra)
                transfer_fixed_string(t, (WCHAR *)(data + done), extra);
            break;
        default: break;
        }
    }
    if (done < (LONG)size)
        transfer_bytes(t, data + done, (LONG)size - done);
    return done > (LONG)size ? done : (LONG)size;
}

static void owner_transfer(const BYTE *table, BYTE *data, SHORT number, LONG record, BOOL has_record, BOOL get)
{
    Transfer t;
    transfer_begin(&t, number, record, has_record, get);
    t.in_record = TRUE;
    transfer_owner(&t, table, data);
    transfer_end(&t);
}

VB_STD void vb___vbaGetOwner3(const BYTE *table, BYTE *data, SHORT number) { owner_transfer(table, data, number, 0, FALSE, TRUE); }
VB_STD void vb___vbaGetOwner4(const BYTE *table, BYTE *data, LONG record, SHORT number) { owner_transfer(table, data, number, record, TRUE, TRUE); }
VB_STD void vb___vbaPutOwner3(const BYTE *table, BYTE *data, SHORT number) { owner_transfer(table, data, number, 0, FALSE, FALSE); }
VB_STD void vb___vbaPutOwner4(const BYTE *table, BYTE *data, LONG record, SHORT number) { owner_transfer(table, data, number, record, TRUE, FALSE); }
