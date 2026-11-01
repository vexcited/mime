/*
 * text streams: a file, the console or a pipe read and written as lines of text in the ansi code page or in unicode, with the
 * line and column that Scripting.TextStream counts and the errors Windows gives for a stream in the wrong mode or closed.
 */
#include "scrrun_private.h"

#define OBJECT_OF(type, member, pointer) ((type *)((char *)(pointer) - offsetof(type, member)))

DECLSPEC_IMPORT BOOL WINAPI IsTextUnicode(const void *buffer, int size, LPINT result);

/* ------------------------------------------------------------------ the bytes under a stream */

struct BYTE_STREAM {
    LONG Refs;
    HANDLE Handle;
    STREAM_KIND Kind;
    /* a console that takes wide characters, which a program writes as they are. */
    BOOL WideConsole;
    BOOL Console;
};

HRESULT ByteStreamFromHandle(HANDLE handle, STREAM_KIND kind, BOOL wide_console, BYTE_STREAM **out)
{
    BYTE_STREAM *stream = ScrAlloc(sizeof(*stream));
    if (!stream)
        return E_OUTOFMEMORY;
    stream->Refs = 1;
    stream->Handle = handle;
    stream->Kind = kind;
    if (kind == STREAM_CONSOLE) {
        DWORD mode;
        stream->WideConsole = wide_console;
        stream->Console = GetConsoleMode(handle, &mode);
    }
    ScrLockDll();
    *out = stream;
    return S_OK;
}

static void byte_stream_add_ref(BYTE_STREAM *stream)
{
    InterlockedIncrement(&stream->Refs);
}

void ByteStreamRelease(BYTE_STREAM *stream)
{
    if (InterlockedDecrement(&stream->Refs))
        return;
    if (stream->Kind != STREAM_CONSOLE)
        CloseHandle(stream->Handle);
    ScrFree(stream);
    ScrUnlockDll();
}

static HRESULT bs_read(BYTE_STREAM *stream, void *buffer, DWORD size, DWORD *got)
{
    DWORD read = 0;
    BOOL ok = TRUE;
    if (stream->Kind == STREAM_PIPE) {
        DWORD available;
        ok = PeekNamedPipe(stream->Handle, nullptr, 0, nullptr, &available, nullptr);
    }
    if (ok)
        ok = ReadFile(stream->Handle, buffer, size, &read, nullptr);
    if (ok) {
        *got = read;
        return S_OK;
    }
    HRESULT hr = TranslateWin32Error(GetLastError());
    if (stream->Kind != STREAM_FILE && hr == TranslateWin32Error(ERROR_BROKEN_PIPE)) {
        *got = 0;
        return S_OK;
    }
    return hr;
}

static HRESULT bs_write(BYTE_STREAM *stream, const void *buffer, DWORD size)
{
    if (!size)
        return S_OK;
    DWORD written = 0;
    if (stream->Kind == STREAM_CONSOLE && stream->WideConsole && stream->Console) {
        DWORD count = size >> 1;
        const WCHAR *text = buffer;
        while (count) {
            DWORD part = count > 0x3FFF ? 0x3FFF : count, done = 0;
            if (!WriteConsoleW(stream->Handle, text, part, &done, nullptr))
                return TranslateWin32Error(GetLastError());
            text += done;
            count -= done;
        }
        return S_OK;
    }
    if (!WriteFile(stream->Handle, buffer, size, &written, nullptr))
        return TranslateWin32Error(GetLastError());
    return S_OK;
}

static HRESULT bs_seek(BYTE_STREAM *stream, LONGLONG offset, DWORD origin, ULONGLONG *position)
{
    LARGE_INTEGER move, result;
    move.QuadPart = offset;
    if (!SetFilePointerEx(stream->Handle, move, &result, origin))
        return TranslateWin32Error(GetLastError());
    if (position)
        *position = (ULONGLONG)result.QuadPart;
    return S_OK;
}

static HRESULT bs_size(BYTE_STREAM *stream, ULONGLONG *size)
{
    LARGE_INTEGER value;
    if (!GetFileSizeEx(stream->Handle, &value))
        return TranslateWin32Error(GetLastError());
    *size = (ULONGLONG)value.QuadPart;
    return S_OK;
}

/* ------------------------------------------------------------------ the text stream */

#define MODE_MASK 3
#define FORMAT_ANSI 4
#define FORMAT_UNICODE 8
#define FORMAT_REVERSED 12
#define FORMAT_MASK 12
#define BLOCK_CHARS 256

typedef struct TEXT_STREAM {
    ITextStream Text;
    IProvideClassInfo Info;
    LONG Refs;
    CRITICAL_SECTION Lock;
    BYTE_STREAM *Bytes;
    DWORD Mode, Format;
    STREAM_KIND Kind;
    /* the line and the column of the next character: the line of the start and the lines since, and the column after the
     * last line break or from the column of the start. */
    LONG BaseLine, Lines, BaseColumn, Column;
    ULONGLONG StartPosition;
    WCHAR Block[BLOCK_CHARS];
    DWORD BlockPosition, BlockLength;
    /* a character looked at and not taken, and one more behind it. */
    BOOL Peeked, PeekedSecond;
    WCHAR PeekChar, PeekSecondChar;
    /* the first byte of a multibyte character that a block ended in. */
    BOOL HasLead;
    BYTE Lead;
} TEXT_STREAM;

static const ITextStreamVtbl g_text_vtbl;
static const IProvideClassInfoVtbl g_text_info_vtbl;

MIME_DEFINE_GUID(CLSID_ScrTextStream, 0x0BB02EC0, 0xEF49, 0x11CF, 0x89, 0x40, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x28);

static TEXT_STREAM *text_of(ITextStream *This)
{
    return OBJECT_OF(TEXT_STREAM, Text, This);
}

static void reverse_bytes(WCHAR *text, DWORD count)
{
    for (DWORD i = 0; i < count; i++)
        text[i] = (WCHAR)((text[i] << 8) | (text[i] >> 8));
}

/* the next block of characters from the bytes of the stream: none at the end. */
static HRESULT read_block(TEXT_STREAM *text)
{
    if (!text->Bytes)
        return E_VB_NOOBJECT;
    text->BlockPosition = 0;
    BYTE ansi[128];
    BYTE *target;
    DWORD want;
    if ((text->Format & FORMAT_MASK) == FORMAT_ANSI) {
        target = ansi;
        want = sizeof(ansi);
    } else {
        target = (BYTE *)text->Block;
        want = BLOCK_CHARS * sizeof(WCHAR);
    }
    DWORD carried = 0;
    if (text->HasLead) {
        *target++ = text->Lead;
        want--;
        carried = 1;
    }
    DWORD got = 0;
    HRESULT hr = bs_read(text->Bytes, target, want, &got);
    if (FAILED(hr))
        return hr;
    DWORD total = got + carried;
    if (text->HasLead) {
        text->HasLead = FALSE;
        target--;
    }
    if ((text->Format & FORMAT_MASK) == FORMAT_ANSI) {
        DWORD i = 0;
        while (i < total) {
            if (IsDBCSLeadByte(ansi[i]) && ++i == total) {
                text->Lead = ansi[i - 1];
                text->HasLead = TRUE;
                total--;
            }
            i++;
        }
        int chars = total ? MultiByteToWideChar(CP_ACP, 0, (const CHAR *)ansi, (int)total, text->Block, BLOCK_CHARS) : 0;
        text->BlockLength = (DWORD)(chars > 0 ? chars : 0);
    } else {
        if (total & 1) {
            if (got == 0) {
                target[total] = 0;
                total++;
            } else {
                total--;
                text->HasLead = TRUE;
                text->Lead = target[total];
            }
        }
        text->BlockLength = total >> 1;
        if ((text->Format & FORMAT_MASK) == FORMAT_REVERSED)
            reverse_bytes(text->Block, text->BlockLength);
    }
    return S_OK;
}

/* the next character of the stream: 0 with it, 1 at the end of the stream. */
static HRESULT get_char_core(TEXT_STREAM *text, WCHAR *out)
{
    if (text->BlockPosition == text->BlockLength) {
        HRESULT hr = read_block(text);
        if (FAILED(hr))
            return hr;
        if (!text->BlockLength)
            return 1;
    }
    *out = text->Block[text->BlockPosition++];
    return S_OK;
}

static void count_char(TEXT_STREAM *text, WCHAR c)
{
    if (c == '\n') {
        text->Lines++;
        text->Column = 1;
    } else {
        text->Column++;
    }
}

/* takes the next character, which counts for the line and column. */
static HRESULT get_char(TEXT_STREAM *text, WCHAR *out)
{
    if (text->Peeked) {
        *out = text->PeekChar;
        if (text->PeekedSecond) {
            text->PeekedSecond = FALSE;
            text->PeekChar = text->PeekSecondChar;
        } else {
            text->Peeked = FALSE;
        }
    } else {
        HRESULT hr = get_char_core(text, out);
        if (hr != S_OK)
            return hr;
    }
    count_char(text, *out);
    return S_OK;
}

static HRESULT peek_char0(TEXT_STREAM *text, WCHAR *out)
{
    if (text->Peeked) {
        *out = text->PeekChar;
        return S_OK;
    }
    HRESULT hr = get_char_core(text, &text->PeekChar);
    if (hr == S_OK) {
        text->Peeked = TRUE;
        *out = text->PeekChar;
    }
    return hr;
}

static HRESULT peek_char1(TEXT_STREAM *text, WCHAR *out)
{
    if (text->PeekedSecond) {
        *out = text->PeekSecondChar;
        return S_OK;
    }
    WCHAR first;
    HRESULT hr = peek_char0(text, &first);
    if (hr != S_OK)
        return hr;
    hr = get_char_core(text, &text->PeekSecondChar);
    if (hr == S_OK) {
        text->PeekedSecond = TRUE;
        *out = text->PeekSecondChar;
    }
    return hr;
}

/* ------------------------------------------------------------------ the format of what is read */

/* fills the first block with bytes that were read to find the format. */
static void fill_first_block(TEXT_STREAM *text, const BYTE *bytes, DWORD size)
{
    if ((text->Format & FORMAT_MASK) == FORMAT_ANSI) {
        DWORD usable = size;
        for (DWORD i = 0; i < size; i++) {
            if (IsDBCSLeadByte(bytes[i]) && ++i == size) {
                text->Lead = bytes[i - 1];
                text->HasLead = TRUE;
                usable--;
            }
        }
        int chars = usable ? MultiByteToWideChar(CP_ACP, 0, (const CHAR *)bytes, (int)usable, text->Block, BLOCK_CHARS) : 0;
        text->BlockLength = (DWORD)(chars > 0 ? chars : 0);
    } else {
        text->BlockLength = size >> 1;
        memcpy(text->Block, bytes, (size_t)text->BlockLength * 2);
        if ((text->Format & FORMAT_MASK) == FORMAT_REVERSED)
            reverse_bytes(text->Block, text->BlockLength);
    }
    text->BlockPosition = 0;
}

/* the format of a file that is read or appended to, from its first bytes: the byte order mark, what the text looks like or
 * the default of ansi when it is not told to be unicode. */
static HRESULT determine_format(TEXT_STREAM *text, BOOL assume_unicode)
{
    if (!assume_unicode) {
        ULONGLONG size;
        if (SUCCEEDED(bs_size(text->Bytes, &size)) && (size & 1)) {
            text->Format = (text->Format & ~FORMAT_MASK) | FORMAT_ANSI;
            return S_OK;
        }
    }
    BYTE bytes[128];
    DWORD got = 0;
    HRESULT hr = bs_read(text->Bytes, bytes, sizeof(bytes), &got);
    if (FAILED(hr))
        return hr;
    DWORD format = text->Format & ~FORMAT_MASK;
    const BYTE *data = bytes;
    DWORD size = got;
    if (!assume_unicode && (!got || (got & 1))) {
        format |= FORMAT_ANSI;
    } else {
        int result = 494;
        IsTextUnicode(bytes, (int)got, &result);
        if (!assume_unicode && (result & 0x100)) {
            format |= FORMAT_ANSI;
        } else if (result & 8) {
            format |= FORMAT_UNICODE;
            data += 2;
            size -= 2;
        } else if (result & 0x80) {
            format |= FORMAT_REVERSED;
            data += 2;
            size -= 2;
        } else if (result & 4) {
            format |= FORMAT_UNICODE;
        } else if (result & 0x40) {
            format |= FORMAT_REVERSED;
        } else if (result & 2) {
            format |= FORMAT_UNICODE;
        } else if (result & 0x20) {
            format |= FORMAT_REVERSED;
        } else {
            format |= assume_unicode ? FORMAT_UNICODE : FORMAT_ANSI;
        }
    }
    text->Format = format;
    if ((text->Mode & MODE_MASK) == 1)
        fill_first_block(text, data, size);
    return S_OK;
}

static HRESULT text_init(TEXT_STREAM *text, IOMode mode, Tristate format)
{
    if (!text->Bytes)
        return E_FAIL;
    switch (mode) {
    case ForReading:
        text->Mode = 1;
        break;
    case ForWriting:
        text->Mode = 2;
        break;
    case ForAppending:
        text->Mode = 3;
        text->BaseLine = 0;
        text->BaseColumn = 0;
        break;
    default:
        return E_INVALIDARG;
    }
    HRESULT hr;
    switch (format) {
    case TristateUseDefault:
        if (text->Mode == 2) {
            text->Format = (text->Format & ~FORMAT_MASK) | FORMAT_ANSI;
            goto positioned;
        }
        hr = determine_format(text, FALSE);
        break;
    case TristateTrue:
        if (text->Mode == 2 ||
            (text->Kind == STREAM_CONSOLE && GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &(DWORD){0}))) {
            text->Format = (text->Format & ~FORMAT_MASK) | FORMAT_UNICODE;
            goto positioned;
        }
        hr = determine_format(text, TRUE);
        break;
    case TristateFalse:
        text->Format = (text->Format & ~FORMAT_MASK) | FORMAT_ANSI;
        goto positioned;
    default:
        return E_INVALIDARG;
    }
    if (FAILED(hr))
        return hr;
positioned:
    if (text->Mode == 3) {
        hr = bs_seek(text->Bytes, 0, FILE_END, &text->StartPosition);
        if (FAILED(hr))
            return hr;
        if ((text->Format & FORMAT_MASK) == FORMAT_ANSI)
            return S_OK;
        if (text->StartPosition & 1) {
            BYTE zero = 0;
            hr = bs_write(text->Bytes, &zero, 1);
            if (FAILED(hr))
                return hr;
            text->StartPosition++;
            return S_OK;
        }
        if ((text->Format & FORMAT_MASK) != FORMAT_UNICODE || text->StartPosition)
            return S_OK;
    } else if (text->Mode != 2 || (text->Format & FORMAT_MASK) != FORMAT_UNICODE || text->Kind == STREAM_CONSOLE) {
        return S_OK;
    }
    WCHAR mark = 0xFEFF;
    return bs_write(text->Bytes, &mark, 2);
}

/* the line and the column at the end of the file that is appended to, found by reading what is in it. */
static HRESULT compute_base_position(TEXT_STREAM *text)
{
    if (!text->Bytes)
        return E_VB_NOOBJECT;
    ULONGLONG saved;
    HRESULT hr = bs_seek(text->Bytes, 0, FILE_CURRENT, &saved);
    if (FAILED(hr))
        return hr;
    hr = bs_seek(text->Bytes, 0, FILE_BEGIN, nullptr);
    if (FAILED(hr))
        return hr;
    text->BaseLine = 1;
    text->BaseColumn = 1;
    ULONGLONG remaining = text->StartPosition;
    DWORD format = text->Format & FORMAT_MASK;
    /* the count of a unicode file reads no bytes, so it stays at the first line and column. */
    if (format != FORMAT_ANSI)
        remaining = 0;
    BOOL first = TRUE, lead = FALSE;
    BYTE buffer[512];
    while (remaining) {
        DWORD got = 0;
        DWORD want = remaining < sizeof(buffer) ? (DWORD)remaining : (DWORD)sizeof(buffer);
        hr = bs_read(text->Bytes, buffer, want, &got);
        if (FAILED(hr) || !got)
            break;
        remaining -= got;
        if (format == FORMAT_ANSI) {
            for (DWORD i = 0; i < got; i++) {
                if (lead) {
                    lead = FALSE;
                } else if (buffer[i] == '\n') {
                    text->BaseLine++;
                    text->BaseColumn = 1;
                } else {
                    lead = IsDBCSLeadByte(buffer[i]);
                    text->BaseColumn++;
                }
            }
        } else {
            for (DWORD i = 0; i + 1 < got; i += 2) {
                WCHAR c = format == FORMAT_UNICODE ? (WCHAR)(buffer[i] | buffer[i + 1] << 8)
                                                    : (WCHAR)(buffer[i + 1] | buffer[i] << 8);
                if (first && c == 0xFEFF) {
                    first = FALSE;
                    continue;
                }
                first = FALSE;
                if (c == '\n') {
                    text->BaseLine++;
                    text->BaseColumn = 1;
                } else {
                    text->BaseColumn++;
                }
            }
        }
    }
    bs_seek(text->Bytes, (LONGLONG)saved, FILE_BEGIN, nullptr);
    return S_OK;
}

/* ------------------------------------------------------------------ the interface */

static HRESULT STDMETHODCALLTYPE text_query_interface(ITextStream *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    TEXT_STREAM *text = text_of(This);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_ITextStream))
        *object = &text->Text;
    else if (IsEqualIID(riid, &IID_IProvideClassInfo))
        *object = &text->Info;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&text->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE text_add_ref(ITextStream *This)
{
    return (ULONG)InterlockedIncrement(&text_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE text_release(ITextStream *This)
{
    TEXT_STREAM *text = text_of(This);
    LONG refs = InterlockedDecrement(&text->Refs);
    if (!refs) {
        if (text->Bytes)
            ByteStreamRelease(text->Bytes);
        DeleteCriticalSection(&text->Lock);
        ScrFree(text);
        ScrUnlockDll();
    }
    return (ULONG)refs;
}

SCR_DISPATCH_METHODS(text, ITextStream, &IID_ITextStream)

#define LOCK(text) EnterCriticalSection(&(text)->Lock)
#define UNLOCK(text) LeaveCriticalSection(&(text)->Lock)

static HRESULT STDMETHODCALLTYPE text_get_Line(ITextStream *This, LONG *out)
{
    if (!out)
        return E_POINTER;
    *out = 0;
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr = S_OK;
    if (!text->Bytes)
        hr = E_VB_NOOBJECT;
    else if (text->BaseLine || SUCCEEDED(hr = compute_base_position(text))) {
        *out = text->BaseLine + text->Lines;
        hr = S_OK;
    }
    UNLOCK(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_get_Column(ITextStream *This, LONG *out)
{
    if (!out)
        return E_POINTER;
    *out = 0;
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr = S_OK;
    if (!text->Bytes) {
        hr = E_VB_NOOBJECT;
    } else if (text->Lines || text->BaseColumn || SUCCEEDED(hr = compute_base_position(text))) {
        LONG column = text->Column;
        if (!text->Lines)
            column += text->BaseColumn;
        *out = column;
        hr = S_OK;
    }
    UNLOCK(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_get_AtEndOfStream(ITextStream *This, VARIANT_BOOL *out)
{
    if (!out)
        return E_POINTER;
    *out = VARIANT_TRUE;
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr;
    if (!text->Bytes) {
        hr = E_VB_NOOBJECT;
    } else if ((text->Mode & MODE_MASK) != 1) {
        hr = E_VB_BADFILEMODE;
    } else {
        WCHAR c;
        hr = peek_char0(text, &c);
        if (SUCCEEDED(hr)) {
            *out = hr == S_OK ? VARIANT_FALSE : VARIANT_TRUE;
            hr = S_OK;
        }
    }
    UNLOCK(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_get_AtEndOfLine(ITextStream *This, VARIANT_BOOL *out)
{
    if (!out)
        return E_POINTER;
    *out = VARIANT_FALSE;
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr;
    if ((text->Mode & MODE_MASK) != 1) {
        hr = E_VB_BADFILEMODE;
    } else {
        WCHAR c = 0;
        hr = peek_char0(text, &c);
        if (SUCCEEDED(hr)) {
            if (hr == 1 || c == '\n') {
                *out = VARIANT_TRUE;
            } else if (c == '\r') {
                WCHAR next = 0;
                HRESULT second = peek_char1(text, &next);
                if (FAILED(second)) {
                    UNLOCK(text);
                    return second;
                }
                if (second == S_OK && next == '\n')
                    *out = VARIANT_TRUE;
            }
            hr = S_OK;
        }
    }
    UNLOCK(text);
    return hr;
}

/* the string of characters taken: reading nothing at the end of the stream is an error, except at the end of a pipe. */
static HRESULT finish_read(TEXT_STREAM *text, const WCHAR *chars, DWORD count, BOOL at_end, BSTR *out)
{
    if (count || !at_end || text->Kind == STREAM_PIPE) {
        *out = SysAllocStringLen(chars, count);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    return E_VB_PASTEOF;
}

static HRESULT STDMETHODCALLTYPE text_Read(ITextStream *This, LONG characters, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr;
    if ((text->Mode & MODE_MASK) != 1) {
        hr = E_VB_BADFILEMODE;
    } else if (characters < 0) {
        hr = E_INVALIDARG;
    } else if (!characters) {
        hr = S_OK;
    } else {
        WCHAR *chars = ScrAlloc((SIZE_T)characters * sizeof(WCHAR));
        if (!chars) {
            UNLOCK(text);
            return E_OUTOFMEMORY;
        }
        LONG n = 0;
        hr = S_OK;
        while (n < characters && (hr = get_char(text, &chars[n])) == S_OK)
            n++;
        if (n == characters || hr == 1)
            hr = finish_read(text, chars, (DWORD)n, hr == 1, out);
        ScrFree(chars);
    }
    UNLOCK(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_ReadLine(ITextStream *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr;
    if ((text->Mode & MODE_MASK) != 1) {
        hr = E_VB_BADFILEMODE;
    } else {
        WCHAR *chars = nullptr;
        DWORD count = 0, capacity = 0;
        hr = S_OK;
        for (;;) {
            WCHAR c;
            hr = get_char(text, &c);
            if (hr != S_OK)
                break;
            if (c == '\n')
                break;
            if (c == '\r') {
                WCHAR next = 0;
                if (peek_char0(text, &next) == S_OK && next == '\n') {
                    get_char(text, &next);
                    break;
                }
            }
            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 256;
                WCHAR *grown = ScrAlloc(capacity * sizeof(WCHAR));
                if (!grown) {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                if (chars)
                    memcpy(grown, chars, count * sizeof(WCHAR));
                ScrFree(chars);
                chars = grown;
            }
            chars[count++] = c;
        }
        if (hr == S_OK || hr == 1)
            hr = finish_read(text, chars, count, hr == 1, out);
        ScrFree(chars);
    }
    UNLOCK(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_ReadAll(ITextStream *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr;
    if ((text->Mode & MODE_MASK) != 1) {
        hr = E_VB_BADFILEMODE;
    } else {
        WCHAR *chars = nullptr;
        DWORD count = 0, capacity = 0;
        for (;;) {
            WCHAR c;
            hr = get_char(text, &c);
            if (hr != S_OK)
                break;
            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 256;
                WCHAR *grown = ScrAlloc(capacity * sizeof(WCHAR));
                if (!grown) {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                if (chars)
                    memcpy(grown, chars, count * sizeof(WCHAR));
                ScrFree(chars);
                chars = grown;
            }
            chars[count++] = c;
        }
        if (hr == 1)
            hr = finish_read(text, chars, count, TRUE, out);
        ScrFree(chars);
    }
    UNLOCK(text);
    return hr;
}

static HRESULT write_text(TEXT_STREAM *text, const WCHAR *chars, DWORD count)
{
    if (!text->Bytes)
        return E_VB_NOOBJECT;
    if ((text->Mode & MODE_MASK) == 1)
        return E_VB_BADFILEMODE;
    if (!count)
        return S_OK;
    HRESULT hr;
    DWORD format = text->Format & FORMAT_MASK;
    if (format == FORMAT_ANSI) {
        char *bytes = ScrAlloc((SIZE_T)count * 2 + 1);
        if (!bytes)
            return E_OUTOFMEMORY;
        BOOL used_default = FALSE;
        int size = WideCharToMultiByte(CP_ACP, 0, chars, (int)count, bytes, (int)count * 2, nullptr, &used_default);
        if (!size || used_default) {
            ScrFree(bytes);
            return E_VB_INVALID;
        }
        hr = bs_write(text->Bytes, bytes, (DWORD)size);
        ScrFree(bytes);
    } else if (format == FORMAT_REVERSED) {
        WCHAR *copy = ScrCopy(chars, count);
        if (!copy)
            return E_OUTOFMEMORY;
        reverse_bytes(copy, count);
        hr = bs_write(text->Bytes, copy, count * 2);
        ScrFree(copy);
    } else {
        hr = bs_write(text->Bytes, chars, count * 2);
    }
    if (FAILED(hr))
        return hr;
    for (DWORD i = 0; i < count; i++)
        count_char(text, chars[i]);
    return S_OK;
}

static HRESULT write_blank_lines(TEXT_STREAM *text, LONG lines)
{
    if (!text->Bytes)
        return E_VB_NOOBJECT;
    if ((text->Mode & MODE_MASK) == 1)
        return E_VB_BADFILEMODE;
    if (lines < 0)
        return E_INVALIDARG;
    if (!lines)
        return S_OK;
    DWORD format = text->Format & FORMAT_MASK;
    static const BYTE ansi[2] = {'\r', '\n'};
    static const BYTE little[4] = {'\r', 0, '\n', 0};
    static const BYTE big[4] = {0, '\r', 0, '\n'};
    const BYTE *unit = format == FORMAT_ANSI ? ansi : format == FORMAT_REVERSED ? big : little;
    DWORD unit_size = format == FORMAT_ANSI ? 2 : 4;
    for (LONG i = 0; i < lines; i++) {
        HRESULT hr = bs_write(text->Bytes, unit, unit_size);
        if (FAILED(hr))
            return hr;
    }
    text->Lines += lines;
    text->Column = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_Write(ITextStream *This, BSTR string)
{
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr = write_text(text, string ? string : L"", SysStringLen(string));
    UNLOCK(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_WriteLine(ITextStream *This, BSTR string)
{
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr;
    if ((text->Mode & MODE_MASK) == 1) {
        hr = E_VB_BADFILEMODE;
    } else {
        hr = write_text(text, string ? string : L"", SysStringLen(string));
        if (SUCCEEDED(hr))
            hr = write_blank_lines(text, 1);
    }
    UNLOCK(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_WriteBlankLines(ITextStream *This, LONG lines)
{
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr = write_blank_lines(text, lines);
    UNLOCK(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_Skip(ITextStream *This, LONG characters)
{
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr = S_OK;
    if ((text->Mode & MODE_MASK) != 1) {
        hr = E_VB_BADFILEMODE;
    } else if (characters < 0) {
        hr = E_INVALIDARG;
    } else if (characters) {
        WCHAR c;
        LONG n = 0;
        do {
            hr = get_char(text, &c);
            if (hr != S_OK)
                break;
            n++;
        } while (n < characters);
        if (!n && hr == 1)
            hr = E_VB_PASTEOF;
    }
    UNLOCK(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_SkipLine(ITextStream *This)
{
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr;
    if ((text->Mode & MODE_MASK) != 1) {
        hr = E_VB_BADFILEMODE;
    } else {
        WCHAR c = 0;
        LONG n = 0;
        hr = get_char(text, &c);
        while (hr == S_OK && c != '\n') {
            n++;
            hr = get_char(text, &c);
        }
        if (!n && hr == 1)
            hr = E_VB_PASTEOF;
    }
    UNLOCK(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_Close(ITextStream *This)
{
    TEXT_STREAM *text = text_of(This);
    LOCK(text);
    HRESULT hr = S_OK;
    if (text->Bytes) {
        ByteStreamRelease(text->Bytes);
        text->Bytes = nullptr;
    } else {
        hr = S_FALSE;
    }
    UNLOCK(text);
    return hr;
}

static const ITextStreamVtbl g_text_vtbl = {
    text_query_interface, text_add_ref, text_release, text_GetTypeInfoCount, text_GetTypeInfo, text_GetIDsOfNames,
    text_Invoke, text_get_Line, text_get_Column, text_get_AtEndOfStream, text_get_AtEndOfLine, text_Read,
    text_ReadLine, text_ReadAll, text_Write, text_WriteLine, text_WriteBlankLines, text_Skip, text_SkipLine, text_Close,
};

static HRESULT STDMETHODCALLTYPE text_info_query_interface(IProvideClassInfo *This, REFIID riid, void **object)
{
    return text_query_interface(&OBJECT_OF(TEXT_STREAM, Info, This)->Text, riid, object);
}

static ULONG STDMETHODCALLTYPE text_info_add_ref(IProvideClassInfo *This)
{
    return text_add_ref(&OBJECT_OF(TEXT_STREAM, Info, This)->Text);
}

static ULONG STDMETHODCALLTYPE text_info_release(IProvideClassInfo *This)
{
    return text_release(&OBJECT_OF(TEXT_STREAM, Info, This)->Text);
}

static HRESULT STDMETHODCALLTYPE text_get_class_info(IProvideClassInfo *This, ITypeInfo **info)
{
    return info ? ScrClassInfo(&CLSID_ScrTextStream, info) : E_POINTER;
}

static const IProvideClassInfoVtbl g_text_info_vtbl = {
    text_info_query_interface, text_info_add_ref, text_info_release, text_get_class_info,
};

HRESULT TextStreamCreate(BYTE_STREAM *bytes, IOMode mode, Tristate format, STREAM_KIND kind, ITextStream **out)
{
    TEXT_STREAM *text = ScrAlloc(sizeof(*text));
    if (!text)
        return E_OUTOFMEMORY;
    text->Text.lpVtbl = &g_text_vtbl;
    text->Info.lpVtbl = &g_text_info_vtbl;
    text->Refs = 1;
    InitializeCriticalSection(&text->Lock);
    byte_stream_add_ref(bytes);
    text->Bytes = bytes;
    text->Kind = kind;
    text->BaseLine = 1;
    text->BaseColumn = 1;
    ScrLockDll();
    HRESULT hr = text_init(text, mode, format);
    if (FAILED(hr)) {
        text_release(&text->Text);
        return hr;
    }
    *out = &text->Text;
    return S_OK;
}

HRESULT TextStreamFromFile(const WCHAR *path, DWORD access, DWORD share, DWORD disposition, IOMode mode, Tristate format,
                           ITextStream **out)
{
    HANDLE file = CreateFileW(path, access, share, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return TranslateWin32Error(GetLastError());
    BYTE_STREAM *bytes;
    HRESULT hr = ByteStreamFromHandle(file, STREAM_FILE, FALSE, &bytes);
    if (FAILED(hr)) {
        CloseHandle(file);
        return hr;
    }
    hr = TextStreamCreate(bytes, mode, format, STREAM_FILE, out);
    ByteStreamRelease(bytes);
    return hr;
}

HRESULT TextStreamStandard(DWORD handle_id, Tristate format, ITextStream **out)
{
    HANDLE handle = GetStdHandle(handle_id);
    if (handle == INVALID_HANDLE_VALUE)
        return TranslateWin32Error(ERROR_INVALID_HANDLE);
    BYTE_STREAM *bytes;
    HRESULT hr = ByteStreamFromHandle(handle, STREAM_CONSOLE, format == TristateTrue, &bytes);
    if (FAILED(hr))
        return hr;
    IOMode mode = handle_id == STD_OUTPUT_HANDLE || handle_id == STD_ERROR_HANDLE ? ForWriting
                  : handle_id == STD_INPUT_HANDLE                                 ? ForReading
                                                                                  : (IOMode)0;
    hr = TextStreamCreate(bytes, mode, format, STREAM_CONSOLE, out);
    ByteStreamRelease(bytes);
    return hr;
}
