/*
 * the streams ole keeps in a storage: "\1CompObj" with the class name, clipboard format and ProgID of the object, and
 * "\1Ole" with its flags. and the questions whether a data object holds something to embed or to link.
 */
#include "ole32_private.h"

#define COMPOBJ_NAME L"\001CompObj"
#define OLE_STREAM_NAME L"\001Ole"
#define COMPOBJ_UNICODE_MARKER 0x71B239F4
#define OLE_VERSION 0x02000001
#define STG_E_FILENOTFOUND_ ((HRESULT)0x80030002L)
#define OLE_S_STATIC_ ((HRESULT)0x00040001L)
#define OLE_S_USEREG_ 0x00040000

/* the tag of a clipboard format in the stream, 0 for none, -1 and -2 for a standard format and the length of a name. */
#define FORMAT_REGISTERED 400
#define FORMAT_NAME_MAX 400

typedef struct {
    DWORD first, version, unused;
    CLSID clsid;
} COMPOBJ_HEADER;

/* the class name and ProgID are kept in ANSI when it holds them and in Unicode when it does not. */
typedef struct {
    COMPOBJ_HEADER header;
    char *user_type, *prog_id;
    WCHAR *user_type_w, *prog_id_w;
    DWORD user_type_length, prog_id_length;
    DWORD tag, format;
} COMPOBJ;

static void compobj_free(COMPOBJ *cod)
{
    CoTaskMemFree(cod->user_type);
    CoTaskMemFree(cod->user_type_w);
    CoTaskMemFree(cod->prog_id);
    CoTaskMemFree(cod->prog_id_w);
    memset(cod, 0, sizeof(*cod));
}

static BOOL has_nul(const char *text, DWORD length)
{
    while (length--)
        if (!*text++)
            return TRUE;
    return FALSE;
}

/* the bytes of a stream, as many as there are. */
static HRESULT read_exact(IStream *stream, void *buffer, ULONG size)
{
    ULONG read = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, buffer, size, &read);
    if (FAILED(hr))
        return hr;
    return read == size ? S_OK : STG_E_READFAULT;
}

static HRESULT write_exact(IStream *stream, const void *buffer, ULONG size)
{
    ULONG written = 0;
    HRESULT hr = stream->lpVtbl->Write(stream, buffer, size, &written);
    if (FAILED(hr))
        return hr;
    return written == size ? S_OK : STG_E_WRITEFAULT;
}

static HRESULT read_ansi(IStream *stream, char **text, DWORD *length)
{
    DWORD count;
    *text = nullptr;
    *length = 0;
    HRESULT hr = read_exact(stream, &count, 4);
    if (FAILED(hr) || !count)
        return hr;
    char *copy = CoTaskMemAlloc(count);
    if (!copy)
        return E_OUTOFMEMORY;
    hr = read_exact(stream, copy, count);
    if (FAILED(hr) || !has_nul(copy, count)) {
        CoTaskMemFree(copy);
        return FAILED(hr) ? hr : E_INVALIDARG;
    }
    *text = copy;
    *length = count;
    return S_OK;
}

/* the Unicode strings are counted in bytes. */
static HRESULT read_wide(IStream *stream, WCHAR **text)
{
    DWORD bytes;
    *text = nullptr;
    HRESULT hr = read_exact(stream, &bytes, 4);
    if (hr != S_OK || !bytes)
        return hr;
    WCHAR *copy = CoTaskMemAlloc(bytes + sizeof(WCHAR));
    if (!copy)
        return E_OUTOFMEMORY;
    hr = read_exact(stream, copy, bytes);
    copy[bytes / sizeof(WCHAR)] = 0;
    if (hr != S_OK) {
        CoTaskMemFree(copy);
        return hr;
    }
    *text = copy;
    return S_OK;
}

static HRESULT read_format(IStream *stream, DWORD *tag, DWORD *format, BOOL unicode)
{
    DWORD value;
    HRESULT hr = read_exact(stream, &value, 4);
    if (FAILED(hr))
        return hr;
    *tag = value;
    if (!value) {
        *format = 0;
        return S_OK;
    }
    if (value == 0xFFFFFFFF || value == 0xFFFFFFFE) {
        hr = read_exact(stream, format, 4);
        return FAILED(hr) ? hr : value == 0xFFFFFFFE ? 0x00040002 : S_OK;
    }
    if (value > FORMAT_NAME_MAX)
        return CO_E_CLASSSTRING;
    if (unicode) {
        WCHAR name[FORMAT_NAME_MAX + 2] = {};
        hr = read_exact(stream, name, value * 2);
        if (FAILED(hr))
            return hr;
        *format = RegisterClipboardFormatW(name);
        return *format ? S_OK : (HRESULT)0x8004006A;
    }
    char name[FORMAT_NAME_MAX + 2] = {};
    hr = read_exact(stream, name, value);
    if (FAILED(hr))
        return hr;
    *format = RegisterClipboardFormatA(name);
    return *format ? S_OK : CO_E_CLASSSTRING;
}

static HRESULT read_compobj(IStorage *storage, COMPOBJ *cod)
{
    IStream *stream = nullptr;
    memset(cod, 0, sizeof(*cod));
    HRESULT hr = storage->lpVtbl->OpenStream(storage, COMPOBJ_NAME, nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stream);
    if (FAILED(hr))
        return hr;
    hr = read_exact(stream, &cod->header, sizeof(cod->header));
    if (SUCCEEDED(hr))
        hr = read_ansi(stream, &cod->user_type, &cod->user_type_length);
    if (SUCCEEDED(hr))
        hr = read_format(stream, &cod->tag, &cod->format, FALSE);
    if (SUCCEEDED(hr)) {
        DWORD length = 0;
        if (read_exact(stream, &length, 4) != S_OK)
            length = 0;
        if (length - 1 <= 0x27) {
            cod->prog_id = CoTaskMemAlloc(length);
            if (!cod->prog_id) {
                hr = E_OUTOFMEMORY;
            } else {
                if (read_exact(stream, cod->prog_id, length) == S_OK && has_nul(cod->prog_id, length)) {
                    cod->prog_id_length = length;
                    length = 0;
                    if (read_exact(stream, &length, 4) == S_OK && length == COMPOBJ_UNICODE_MARKER) {
                        hr = read_wide(stream, &cod->user_type_w);
                        if (SUCCEEDED(hr) && cod->user_type_w)
                            cod->user_type_length = (DWORD)wcslen(cod->user_type_w) + 1;
                        if (SUCCEEDED(hr)) {
                            DWORD tag = 0, format = 0;
                            hr = read_format(stream, &tag, &format, TRUE);
                            if (SUCCEEDED(hr) && tag) {
                                cod->tag = tag;
                                cod->format = format;
                            }
                        }
                        if (SUCCEEDED(hr))
                            hr = read_wide(stream, &cod->prog_id_w);
                        if (SUCCEEDED(hr) && cod->prog_id_w)
                            cod->prog_id_length = (DWORD)wcslen(cod->prog_id_w) + 1;
                    }
                } else {
                    CoTaskMemFree(cod->prog_id);
                    cod->prog_id = nullptr;
                    hr = E_INVALIDARG;
                }
            }
        }
    }
    stream->lpVtbl->Release(stream);
    if (FAILED(hr))
        compobj_free(cod);
    return hr;
}

/* a string kept in ANSI when the code page holds all of it and in Unicode when it does not. */
static HRESULT put_text(const WCHAR *text, char **ansi, WCHAR **wide, DWORD *length)
{
    CoTaskMemFree(*ansi);
    CoTaskMemFree(*wide);
    *ansi = nullptr;
    *wide = nullptr;
    *length = 0;
    int count = (int)wcslen(text) + 1;
    char *narrow = CoTaskMemAlloc(2 * count + 2);
    if (!narrow)
        return E_OUTOFMEMORY;
    BOOL lossy = FALSE;
    if (WideCharToMultiByte(CP_ACP, 0, text, count, narrow, 2 * count + 2, "?", &lossy) && !lossy) {
        *length = (DWORD)strlen(narrow) + 1;
        *ansi = CoTaskMemAlloc(*length);
        if (*ansi)
            memcpy(*ansi, narrow, *length);
        CoTaskMemFree(narrow);
        return *ansi ? S_OK : E_OUTOFMEMORY;
    }
    CoTaskMemFree(narrow);
    *wide = CoTaskMemAlloc(count * sizeof(WCHAR));
    if (!*wide)
        return E_OUTOFMEMORY;
    memcpy(*wide, text, count * sizeof(WCHAR));
    *length = count;
    return S_OK;
}

static WCHAR *text_wide(const char *ansi, const WCHAR *wide, DWORD length)
{
    if (!ansi && !wide)
        return nullptr;
    WCHAR *result = CoTaskMemAlloc((length + 1) * sizeof(WCHAR));
    if (!result)
        return nullptr;
    if (wide) {
        DWORD i = 0;
        for (; i < length && wide[i]; i++)
            result[i] = wide[i];
        result[i] = 0;
    } else if (!MultiByteToWideChar(CP_ACP, 0, ansi, length, result, length)) {
        CoTaskMemFree(result);
        return nullptr;
    }
    return result;
}

WINOLEAPI HRESULT WINAPI ReadFmtUserTypeStg(IStorage *storage, CLIPFORMAT *format, LPOLESTR *user_type)
{
    COMPOBJ cod;
    if (!storage)
        return E_INVALIDARG;
    HRESULT hr = read_compobj(storage, &cod);
    if (FAILED(hr)) {
        if (format)
            *format = 0;
        if (user_type)
            *user_type = nullptr;
        return hr;
    }
    if (format)
        *format = (CLIPFORMAT)cod.format;
    hr = S_OK;
    if (user_type) {
        *user_type = nullptr;
        if (cod.user_type_length) {
            *user_type = text_wide(cod.user_type, cod.user_type_w, cod.user_type_length);
            if (!*user_type) {
                hr = E_OUTOFMEMORY;
                if (format)
                    *format = 0;
            }
        }
    }
    compobj_free(&cod);
    return hr;
}

static HRESULT write_ansi(IStream *stream, const char *text)
{
    DWORD length = text && *text ? (DWORD)strlen(text) + 1 : 0;
    HRESULT hr = write_exact(stream, &length, 4);
    return SUCCEEDED(hr) && length ? write_exact(stream, text, length) : hr;
}

static HRESULT write_wide(IStream *stream, const WCHAR *text)
{
    DWORD bytes = text ? (DWORD)(wcslen(text) + 1) * sizeof(WCHAR) : 0;
    HRESULT hr = write_exact(stream, &bytes, 4);
    return SUCCEEDED(hr) && bytes ? write_exact(stream, text, bytes) : hr;
}

static HRESULT write_format(IStream *stream, DWORD tag, DWORD format)
{
    if (!tag)
        return write_exact(stream, &tag, 4);
    if (tag == 0xFFFFFFFF || tag == 0xFFFFFFFE) {
        HRESULT hr = write_exact(stream, &tag, 4);
        return SUCCEEDED(hr) ? write_exact(stream, &format, 4) : hr;
    }
    char name[FORMAT_NAME_MAX + 1] = {};
    int length = GetClipboardFormatNameA(format, name, FORMAT_NAME_MAX);
    if (!length) {
        DWORD error = GetLastError();
        return error > 0 ? HRESULT_FROM_WIN32(error & 0xFFFF) : (HRESULT)error;
    }
    name[length] = 0;
    DWORD count = length + 1;
    HRESULT hr = write_exact(stream, &count, 4);
    return SUCCEEDED(hr) ? write_exact(stream, name, count) : hr;
}

/* a failure to write the stream goes unreported, only a lack of memory does not. */
WINOLEAPI HRESULT WINAPI WriteFmtUserTypeStg(IStorage *storage, CLIPFORMAT format, LPOLESTR user_type)
{
    if (!storage)
        return E_INVALIDARG;
    COMPOBJ cod;
    HRESULT hr = read_compobj(storage, &cod);
    if (hr == E_OUTOFMEMORY)
        return hr;
    if (user_type) {
        hr = put_text(user_type, &cod.user_type, &cod.user_type_w, &cod.user_type_length);
        if (FAILED(hr)) {
            compobj_free(&cod);
            return hr;
        }
    }
    CLSID clsid;
    if (ReadClassStg(storage, &clsid) != S_OK)
        memset(&clsid, 0, sizeof(clsid));
    LPOLESTR prog_id = nullptr;
    if (SUCCEEDED(ProgIDFromCLSID(&clsid, &prog_id))) {
        put_text(prog_id, &cod.prog_id, &cod.prog_id_w, &cod.prog_id_length);
        CoTaskMemFree(prog_id);
    }
    if (format >= 0xC000) {
        cod.tag = FORMAT_REGISTERED;
        cod.format = format;
    } else if (!format) {
        cod.tag = 0;
        cod.format = 0;
    } else {
        cod.tag = 0xFFFFFFFF;
        cod.format = format;
    }
    IStream *stream = nullptr;
    hr = storage->lpVtbl->CreateStream(storage, COMPOBJ_NAME,
                                       STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &stream);
    if (SUCCEEDED(hr)) {
        COMPOBJ_HEADER header = {0xFFFE0001, 0x0A03, 0xFFFFFFFF, clsid};
        DWORD marker = COMPOBJ_UNICODE_MARKER, none = 0;
        hr = write_exact(stream, &header, sizeof(header));
        if (SUCCEEDED(hr))
            hr = write_ansi(stream, cod.user_type);
        if (SUCCEEDED(hr))
            hr = write_format(stream, cod.tag, cod.format);
        if (SUCCEEDED(hr))
            hr = write_ansi(stream, cod.prog_id);
        if (SUCCEEDED(hr))
            hr = write_exact(stream, &marker, 4);
        if (SUCCEEDED(hr))
            hr = write_wide(stream, cod.user_type_w);
        if (SUCCEEDED(hr))
            hr = write_exact(stream, &none, 4);
        if (SUCCEEDED(hr))
            hr = write_wide(stream, cod.prog_id_w);
        stream->lpVtbl->Release(stream);
    }
    compobj_free(&cod);
    return hr == E_OUTOFMEMORY ? hr : S_OK;
}

/* the flags of the object, 4 is the one that marks it as to be converted. */
static HRESULT set_ole_flags(IStorage *storage, DWORD mask, DWORD value)
{
    IStream *stream = nullptr;
    if (!storage)
        return E_INVALIDARG;
    HRESULT hr = storage->lpVtbl->OpenStream(storage, OLE_STREAM_NAME, nullptr, STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
                                             0, &stream);
    if (hr == STG_E_FILENOTFOUND_) {
        DWORD initial[5] = {OLE_VERSION, 0, 0, 0, 0};
        hr = storage->lpVtbl->CreateStream(storage, OLE_STREAM_NAME, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0,
                                           &stream);
        if (SUCCEEDED(hr))
            hr = write_exact(stream, initial, sizeof(initial));
    }
    if (SUCCEEDED(hr)) {
        LARGE_INTEGER position = {.QuadPart = 4};
        DWORD flags = 0;
        hr = stream->lpVtbl->Seek(stream, position, STREAM_SEEK_SET, nullptr);
        if (SUCCEEDED(hr))
            hr = read_exact(stream, &flags, 4);
        if (SUCCEEDED(hr)) {
            flags = value | (flags & mask);
            hr = stream->lpVtbl->Seek(stream, position, STREAM_SEEK_SET, nullptr);
        }
        if (SUCCEEDED(hr))
            hr = write_exact(stream, &flags, 4);
    }
    if (stream)
        stream->lpVtbl->Release(stream);
    return hr;
}

WINOLEAPI HRESULT WINAPI SetConvertStg(IStorage *storage, BOOL convert)
{
    return set_ole_flags(storage, convert ? 0xFFFFFFFF : 0xFFFFFFFB, convert ? 4 : 0);
}

/* ------------------------------------------------------------------ what a data object holds */

#define CF_BITMAP_ 2
#define CF_METAFILEPICT_ 3
#define CF_DIB_ 8
#define CF_ENHMETAFILE_ 14
#define FORMAT_BATCH 20

static CLIPFORMAT register_format(LPCWSTR name)
{
    return (CLIPFORMAT)RegisterClipboardFormatW(name);
}

static BOOL is_file_name(CLIPFORMAT format)
{
    return format == register_format(L"FileName") || format == register_format(L"FileNameW");
}

/* the formats a data object lists for getting. A data object that asks for the registry has no listing here. */
static HRESULT enumerate_formats(IDataObject *data, IEnumFORMATETC **enumerator)
{
    *enumerator = nullptr;
    HRESULT hr = data->lpVtbl->EnumFormatEtc(data, DATADIR_GET, enumerator);
    if (hr == OLE_S_USEREG_)
        return E_FAIL;
    if (hr == S_OK && !*enumerator)
        return E_FAIL;
    return hr;
}

/* bit 0 when it holds something to embed and bit 1 when it holds a picture. */
static DWORD query_embed_formats(IDataObject *data)
{
    IEnumFORMATETC *enumerator;
    DWORD result = 0;
    if (enumerate_formats(data, &enumerator) != S_OK)
        return 0;
    for (;;) {
        FORMATETC formats[FORMAT_BATCH];
        ULONG count = 0;
        if (FAILED(enumerator->lpVtbl->Next(enumerator, FORMAT_BATCH, formats, &count)) || !count)
            break;
        BOOL found = FALSE;
        for (ULONG i = 0; i < count; i++) {
            CoTaskMemFree(formats[i].ptd);
            if (found)
                continue;
            CLIPFORMAT format = formats[i].cfFormat;
            if (format == register_format(L"Embed Source") || format == register_format(L"Embedded Object") ||
                is_file_name(format)) {
                result |= 1;
                found = TRUE;
            } else if (format == CF_BITMAP_ || format == CF_METAFILEPICT_ || format == CF_DIB_ ||
                       format == CF_ENHMETAFILE_) {
                result = 2;
            }
        }
        if (found)
            break;
    }
    if (!(result & 1)) {
        IPersistStorage *persist = nullptr;
        if (SUCCEEDED(data->lpVtbl->QueryInterface(data, &IID_IPersistStorage, (void **)&persist))) {
            persist->lpVtbl->Release(persist);
            result |= 1;
        }
    }
    enumerator->lpVtbl->Release(enumerator);
    return result;
}

WINOLEAPI HRESULT WINAPI OleQueryCreateFromData(IDataObject *data)
{
    if (!data)
        return E_INVALIDARG;
    DWORD formats = query_embed_formats(data);
    if (formats & 1)
        return S_OK;
    return (formats & 2) ? OLE_S_STATIC_ : S_FALSE;
}

WINOLEAPI HRESULT WINAPI OleQueryLinkFromData(IDataObject *data)
{
    IEnumFORMATETC *enumerator;
    if (!data)
        return E_INVALIDARG;
    if (enumerate_formats(data, &enumerator) != S_OK)
        return S_FALSE;
    BOOL found = FALSE;
    while (!found) {
        FORMATETC formats[FORMAT_BATCH];
        ULONG count = 0;
        if (FAILED(enumerator->lpVtbl->Next(enumerator, FORMAT_BATCH, formats, &count)) || !count)
            break;
        for (ULONG i = 0; i < count; i++) {
            CoTaskMemFree(formats[i].ptd);
            if (formats[i].cfFormat == register_format(L"Link Source") || is_file_name(formats[i].cfFormat))
                found = TRUE;
        }
    }
    enumerator->lpVtbl->Release(enumerator);
    return found ? S_OK : S_FALSE;
}
