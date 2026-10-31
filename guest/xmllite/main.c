/*
 * xmllite.dll, the pull parser and the writer of xml that programs link when they want less than a document object model.
 * main.c has the entry points and the objects that name a stream and its encoding, reader.c reads, writer.c writes.
 */
#include "xmllite.h"

#define XMLLITE_EXPORT DECLSPEC_EXPORT

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

void *XmlAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *XmlRealloc(void *block, SIZE_T size)
{
    if (!block)
        return XmlAlloc(size);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, block, size ? size : 1);
}

void XmlFree(void *block)
{
    if (block)
        HeapFree(GetProcessHeap(), 0, block);
}

WCHAR *XmlDuplicate(const WCHAR *text, SIZE_T length)
{
    WCHAR *copy = XmlAlloc((length + 1) * sizeof(WCHAR));
    if (copy && length)
        memcpy(copy, text, length * sizeof(WCHAR));
    return copy;
}

/* ------------------------------------------------------------------ encodings */

static const struct {
    const WCHAR *name;
    UINT code_page;
} g_encodings[] = {
    {L"utf-8", 65001},       {L"utf8", 65001},     {L"utf-16", 1200},      {L"utf-16le", 1200},  {L"utf-16be", 1201},
    {L"unicode", 1200},      {L"iso-8859-1", 28591}, {L"latin1", 28591},   {L"windows-1252", 1252}, {L"us-ascii", 20127},
    {L"ascii", 20127},       {L"iso-8859-15", 28605}, {L"windows-1250", 1250}, {L"windows-1251", 1251},
};

static WCHAR lower(WCHAR c)
{
    return c >= 'A' && c <= 'Z' ? (WCHAR)(c + 32) : c;
}

UINT XmlCodePageOfName(const WCHAR *name, SIZE_T length)
{
    for (SIZE_T i = 0; i < ARRAYSIZE(g_encodings); i++) {
        const WCHAR *known = g_encodings[i].name;
        SIZE_T k = 0;
        while (k < length && known[k] && lower(name[k]) == known[k])
            k++;
        if (k == length && !known[k])
            return g_encodings[i].code_page;
    }
    return 0;
}

/* ------------------------------------------------------------------ input and output objects */

static HRESULT STDMETHODCALLTYPE stream_object_query(IUnknown *This, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualGUID(iid, &IID_IUnknown))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE stream_object_add_ref(IUnknown *This)
{
    return InterlockedIncrement(&((XML_STREAM_OBJECT *)This)->references);
}

static ULONG STDMETHODCALLTYPE stream_object_release(IUnknown *This)
{
    XML_STREAM_OBJECT *object = (XML_STREAM_OBJECT *)This;
    LONG left = InterlockedDecrement(&object->references);
    if (!left) {
        if (object->stream)
            object->stream->lpVtbl->Release(object->stream);
        XmlFree(object->base_uri);
        XmlFree(object);
    }
    return left;
}

const IUnknownVtbl XmlInputVtbl = {stream_object_query, stream_object_add_ref, stream_object_release};
const IUnknownVtbl XmlOutputVtbl = {stream_object_query, stream_object_add_ref, stream_object_release};

static HRESULT make_stream_object(const IUnknownVtbl *vtbl, IUnknown *stream, UINT code_page, BOOL hint, const WCHAR *base_uri,
                                  XML_STREAM_OBJECT **result)
{
    XML_STREAM_OBJECT *object = XmlAlloc(sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    object->lpVtbl = vtbl;
    object->references = 1;
    object->code_page = code_page;
    object->hint = hint;
    if (base_uri && !(object->base_uri = XmlDuplicate(base_uri, wcslen(base_uri)))) {
        XmlFree(object);
        return E_OUTOFMEMORY;
    }
    if (stream) {
        stream->lpVtbl->AddRef(stream);
        object->stream = stream;
    }
    *result = object;
    return S_OK;
}

/* ------------------------------------------------------------------ exports */

XMLLITE_EXPORT HRESULT WINAPI CreateXmlReader(REFIID iid, void **object, void *malloc_interface)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    return XmlCreateReader(iid, object);
}

XMLLITE_EXPORT HRESULT WINAPI CreateXmlReaderInputWithEncodingCodePage(IUnknown *stream, void *malloc_interface, UINT code_page,
                                                                       BOOL hint, const WCHAR *base_uri, IUnknown **input)
{
    if (!input)
        return E_INVALIDARG;
    *input = nullptr;
    if (!stream)
        return E_INVALIDARG;
    XML_STREAM_OBJECT *object;
    HRESULT hr = make_stream_object(&XmlInputVtbl, stream, code_page, hint, base_uri, &object);
    if (SUCCEEDED(hr))
        *input = (IUnknown *)object;
    return hr;
}

XMLLITE_EXPORT HRESULT WINAPI CreateXmlReaderInputWithEncodingName(IUnknown *stream, void *malloc_interface,
                                                                   const WCHAR *encoding, BOOL hint, const WCHAR *base_uri,
                                                                   IUnknown **input)
{
    UINT code_page = 0;
    if (encoding) {
        code_page = XmlCodePageOfName(encoding, wcslen(encoding));
        if (!code_page)
            return MX_E_ENCODING;
    }
    return CreateXmlReaderInputWithEncodingCodePage(stream, malloc_interface, code_page, hint, base_uri, input);
}

XMLLITE_EXPORT HRESULT WINAPI CreateXmlWriter(REFIID iid, void **object, void *malloc_interface)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    return XmlCreateWriter(iid, object);
}

XMLLITE_EXPORT HRESULT WINAPI CreateXmlWriterOutputWithEncodingCodePage(IUnknown *stream, void *malloc_interface, UINT code_page,
                                                                        IUnknown **output)
{
    if (!output)
        return E_INVALIDARG;
    *output = nullptr;
    if (!stream)
        return E_INVALIDARG;
    XML_STREAM_OBJECT *object;
    HRESULT hr = make_stream_object(&XmlOutputVtbl, stream, code_page ? code_page : 65001, FALSE, nullptr, &object);
    if (SUCCEEDED(hr))
        *output = (IUnknown *)object;
    return hr;
}

XMLLITE_EXPORT HRESULT WINAPI CreateXmlWriterOutputWithEncodingName(IUnknown *stream, void *malloc_interface,
                                                                    const WCHAR *encoding, IUnknown **output)
{
    UINT code_page = 0;
    if (encoding) {
        code_page = XmlCodePageOfName(encoding, wcslen(encoding));
        if (!code_page)
            return MX_E_ENCODING;
    }
    return CreateXmlWriterOutputWithEncodingCodePage(stream, malloc_interface, code_page, output);
}
