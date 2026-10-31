/*
 * the writer of xmllite.dll: markup goes into a buffer of text, the elements that are open and the namespaces in scope are
 * kept so end tags and prefixes follow, and a flush turns the text into the bytes of the encoding of the output and hands
 * them to the stream.
 */
#include "xmllite.h"

typedef struct OPEN_ELEMENT {
    WCHAR *qname;
    ULONG bindings;
    BOOLEAN children, text;
} OPEN_ELEMENT;

typedef struct SCOPE_BINDING {
    WCHAR *prefix, *uri;
} SCOPE_BINDING;

typedef struct WRITER {
    const IXmlWriterVtbl *lpVtbl;
    LONG references;
    ISequentialStream *sink;
    UINT code_page;
    LONG_PTR multi_language, indent, byte_order_mark, omit_declaration, conformance;

    WCHAR *buffer;
    SIZE_T length, capacity;
    BOOLEAN signature_written, anything_written, start_open, document_ended, generated_counter_used;
    ULONG generated;

    OPEN_ELEMENT *elements;
    ULONG element_count, element_capacity;
    SCOPE_BINDING *bindings;
    ULONG binding_count, binding_capacity;
    WCHAR **attributes;
    ULONG attribute_count, attribute_capacity;
    BOOLEAN root_started;
} WRITER;

/* ------------------------------------------------------------------ the buffer */

static HRESULT append(WRITER *w, const WCHAR *text, SIZE_T count)
{
    if (w->length + count + 1 > w->capacity) {
        SIZE_T capacity = w->capacity ? w->capacity : 1024;
        while (capacity < w->length + count + 1)
            capacity *= 2;
        WCHAR *larger = XmlRealloc(w->buffer, capacity * sizeof(WCHAR));
        if (!larger)
            return E_OUTOFMEMORY;
        w->buffer = larger;
        w->capacity = capacity;
    }
    memcpy(w->buffer + w->length, text, count * sizeof(WCHAR));
    w->length += count;
    w->buffer[w->length] = 0;
    w->anything_written = TRUE;
    return S_OK;
}

static HRESULT append_text(WRITER *w, const WCHAR *text)
{
    return append(w, text, wcslen(text));
}

static HRESULT append_char(WRITER *w, WCHAR c)
{
    return append(w, &c, 1);
}

static BOOLEAN is_xml_char(WCHAR c)
{
    return c >= 0x20 || c == '\t' || c == '\n' || c == '\r';
}

/* the text, a number after it and the characters that end it, written into out. */
static void format_number(WCHAR *out, const WCHAR *head, ULONG value, BOOLEAN hex, const WCHAR *tail)
{
    static const WCHAR digits[] = L"0123456789ABCDEF";
    WCHAR reversed[12];
    ULONG n = 0;
    do {
        reversed[n++] = digits[value % (hex ? 16 : 10)];
        value /= hex ? 16 : 10;
    } while (value);
    while (*head)
        *out++ = *head++;
    while (n)
        *out++ = reversed[--n];
    while (*tail)
        *out++ = *tail++;
    *out = 0;
}

static BOOLEAN is_xml_name(const WCHAR *name)
{
    return (name[0] | 0x20) == 'x' && (name[1] | 0x20) == 'm' && (name[2] | 0x20) == 'l' && !name[3];
}

/* text with the characters that markup uses written as references. */
static HRESULT append_escaped(WRITER *w, const WCHAR *text, SIZE_T count, BOOLEAN attribute)
{
    for (SIZE_T i = 0; i < count; i++) {
        WCHAR c = text[i];
        const WCHAR *replacement = nullptr;
        if (c == '&')
            replacement = L"&amp;";
        else if (c == '<')
            replacement = L"&lt;";
        else if (c == '>')
            replacement = L"&gt;";
        else if (c == '\r')
            replacement = L"&#xD;";
        else if (attribute && c == '"')
            replacement = L"&quot;";
        else if (attribute && c == '\t')
            replacement = L"&#x9;";
        else if (attribute && c == '\n')
            replacement = L"&#xA;";
        else if (!is_xml_char(c) && !(c >= 0xD800 && c < 0xE000))
            return WC_E_XMLCHARACTER;
        HRESULT hr = replacement ? append_text(w, replacement) : append_char(w, c);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static HRESULT flush_buffer(WRITER *w)
{
    if (!w->sink)
        return E_UNEXPECTED;
    BYTE *bytes = nullptr;
    SIZE_T size = 0;
    BOOLEAN signature = w->byte_order_mark && !w->signature_written && (w->code_page == 65001 || w->code_page == 1200);
    if (!w->length && !signature)
        return S_OK;
    if (w->code_page == 1200) {
        size = w->length * sizeof(WCHAR);
        bytes = XmlAlloc(size + 2);
        if (!bytes)
            return E_OUTOFMEMORY;
        memcpy(bytes, w->buffer, size);
    } else {
        int needed = w->length ? WideCharToMultiByte(w->code_page, 0, w->buffer, (int)w->length, nullptr, 0, nullptr, nullptr) : 0;
        if (w->length && needed <= 0)
            return MX_E_ENCODING;
        bytes = XmlAlloc((SIZE_T)needed + 1);
        if (!bytes)
            return E_OUTOFMEMORY;
        if (needed)
            WideCharToMultiByte(w->code_page, 0, w->buffer, (int)w->length, (char *)bytes, needed, nullptr, nullptr);
        size = (SIZE_T)needed;
    }
    if (signature) {
        static const BYTE utf8[] = {0xEF, 0xBB, 0xBF}, utf16[] = {0xFF, 0xFE};
        const BYTE *mark = w->code_page == 1200 ? utf16 : utf8;
        SIZE_T mark_size = w->code_page == 1200 ? sizeof(utf16) : sizeof(utf8);
        ULONG written;
        HRESULT hr = w->sink->lpVtbl->Write(w->sink, mark, (ULONG)mark_size, &written);
        if (FAILED(hr)) {
            XmlFree(bytes);
            return hr;
        }
        w->signature_written = TRUE;
    }
    HRESULT hr = S_OK;
    for (SIZE_T done = 0; done < size && SUCCEEDED(hr);) {
        ULONG written = 0;
        hr = w->sink->lpVtbl->Write(w->sink, bytes + done, (ULONG)(size - done), &written);
        if (SUCCEEDED(hr) && !written)
            hr = E_FAIL;
        done += written;
    }
    XmlFree(bytes);
    if (SUCCEEDED(hr))
        w->length = 0;
    return hr;
}

/* ------------------------------------------------------------------ state of the document */

static void drop_bindings(WRITER *w, ULONG base)
{
    while (w->binding_count > base) {
        SCOPE_BINDING *b = &w->bindings[--w->binding_count];
        XmlFree(b->prefix);
        XmlFree(b->uri);
    }
}

static void forget_attributes(WRITER *w)
{
    for (ULONG i = 0; i < w->attribute_count; i++)
        XmlFree(w->attributes[i]);
    w->attribute_count = 0;
}

static void reset_state(WRITER *w)
{
    while (w->element_count) {
        OPEN_ELEMENT *e = &w->elements[--w->element_count];
        XmlFree(e->qname);
    }
    drop_bindings(w, 0);
    forget_attributes(w);
    w->length = 0;
    w->signature_written = w->anything_written = w->start_open = w->document_ended = w->root_started = FALSE;
    w->generated = 0;
}

static HRESULT push_binding(WRITER *w, const WCHAR *prefix, const WCHAR *uri)
{
    if (w->binding_count == w->binding_capacity) {
        ULONG capacity = w->binding_capacity ? w->binding_capacity * 2 : 16;
        SCOPE_BINDING *larger = XmlRealloc(w->bindings, capacity * sizeof(*larger));
        if (!larger)
            return E_OUTOFMEMORY;
        w->bindings = larger;
        w->binding_capacity = capacity;
    }
    SCOPE_BINDING *b = &w->bindings[w->binding_count];
    b->prefix = XmlDuplicate(prefix, wcslen(prefix));
    b->uri = XmlDuplicate(uri, wcslen(uri));
    if (!b->prefix || !b->uri) {
        XmlFree(b->prefix);
        XmlFree(b->uri);
        return E_OUTOFMEMORY;
    }
    w->binding_count++;
    return S_OK;
}

static const WCHAR *uri_of_prefix(const WRITER *w, const WCHAR *prefix)
{
    if (!wcscmp(prefix, L"xml"))
        return L"http://www.w3.org/XML/1998/namespace";
    for (ULONG i = w->binding_count; i > 0; i--)
        if (!wcscmp(w->bindings[i - 1].prefix, prefix))
            return w->bindings[i - 1].uri;
    return nullptr;
}

/* a prefix that stands for the uri and that has not been hidden by a nearer declaration. */
static const WCHAR *prefix_of_uri(const WRITER *w, const WCHAR *uri, BOOLEAN need_prefix)
{
    for (ULONG i = w->binding_count; i > 0; i--) {
        const SCOPE_BINDING *b = &w->bindings[i - 1];
        if (wcscmp(b->uri, uri) != 0 || (need_prefix && !*b->prefix))
            continue;
        const WCHAR *current = uri_of_prefix(w, b->prefix);
        if (current && !wcscmp(current, uri))
            return b->prefix;
    }
    return nullptr;
}

static BOOLEAN valid_name(const WCHAR *name, SIZE_T length, BOOLEAN token)
{
    if (!length)
        return FALSE;
    for (SIZE_T i = 0; i < length; i++) {
        WCHAR c = name[i];
        BOOLEAN start = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':' || c >= 0xC0;
        BOOLEAN more = start || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == 0xB7;
        if (!(i == 0 && !token ? start : more))
            return FALSE;
    }
    return TRUE;
}

static HRESULT indent_here(WRITER *w, ULONG depth)
{
    if (!w->indent || !w->anything_written)
        return S_OK;
    if (w->element_count) {
        OPEN_ELEMENT *parent = &w->elements[w->element_count - 1];
        if (parent->text)
            return S_OK;
    }
    HRESULT hr = append_text(w, L"\r\n");
    for (ULONG i = 0; i < depth && SUCCEEDED(hr); i++)
        hr = append_text(w, L"  ");
    return hr;
}

static HRESULT close_start_tag(WRITER *w)
{
    if (!w->start_open)
        return S_OK;
    w->start_open = FALSE;
    forget_attributes(w);
    return append_char(w, '>');
}

/* the state before any markup is written that belongs to the content of the document. */
static HRESULT prepare_content(WRITER *w, BOOLEAN is_text, BOOLEAN is_element)
{
    if (!w->sink)
        return E_UNEXPECTED;
    if (w->document_ended && (is_text || is_element))
        return WR_E_INVALIDACTION;
    if (!w->element_count) {
        if (is_text && w->conformance == XmlConformanceLevel_Document)
            return WR_E_INVALIDACTION;
        if (is_element && w->conformance == XmlConformanceLevel_Document && w->root_started && !w->element_count)
            return WR_E_INVALIDACTION;
    }
    HRESULT hr = close_start_tag(w);
    if (FAILED(hr))
        return hr;
    if (w->element_count) {
        OPEN_ELEMENT *parent = &w->elements[w->element_count - 1];
        if (is_text)
            parent->text = TRUE;
        else
            parent->children = TRUE;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ the interface */

static HRESULT STDMETHODCALLTYPE writer_query(IXmlWriter *This, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IXmlWriter))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE writer_add_ref(IXmlWriter *This)
{
    return InterlockedIncrement(&((WRITER *)This)->references);
}

static ULONG STDMETHODCALLTYPE writer_release(IXmlWriter *This)
{
    WRITER *w = (WRITER *)This;
    LONG left = InterlockedDecrement(&w->references);
    if (!left) {
        if (w->sink) {
            flush_buffer(w);
            w->sink->lpVtbl->Release(w->sink);
        }
        reset_state(w);
        XmlFree(w->buffer);
        XmlFree(w->elements);
        XmlFree(w->bindings);
        XmlFree(w->attributes);
        XmlFree(w);
    }
    return left;
}

static HRESULT STDMETHODCALLTYPE writer_set_output(IXmlWriter *This, IUnknown *output)
{
    WRITER *w = (WRITER *)This;
    if (w->sink) {
        w->sink->lpVtbl->Release(w->sink);
        w->sink = nullptr;
    }
    reset_state(w);
    w->code_page = 65001;
    if (!output)
        return S_OK;
    IUnknown *stream = output;
    if (output->lpVtbl == &XmlOutputVtbl) {
        XML_STREAM_OBJECT *object = (XML_STREAM_OBJECT *)output;
        stream = object->stream;
        w->code_page = object->code_page ? object->code_page : 65001;
    }
    HRESULT hr = stream->lpVtbl->QueryInterface(stream, &IID_ISequentialStream, (void **)&w->sink);
    if (FAILED(hr)) {
        w->sink = nullptr;
        return hr == E_NOINTERFACE ? E_INVALIDARG : hr;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE writer_get_property(IXmlWriter *This, UINT property, LONG_PTR *value)
{
    WRITER *w = (WRITER *)This;
    if (!value)
        return E_INVALIDARG;
    switch (property) {
    case XmlWriterProperty_MultiLanguage:
        *value = w->multi_language;
        return S_OK;
    case XmlWriterProperty_Indent:
        *value = w->indent;
        return S_OK;
    case XmlWriterProperty_ByteOrderMark:
        *value = w->byte_order_mark;
        return S_OK;
    case XmlWriterProperty_OmitXmlDeclaration:
        *value = w->omit_declaration;
        return S_OK;
    case XmlWriterProperty_ConformanceLevel:
        *value = w->conformance;
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE writer_set_property(IXmlWriter *This, UINT property, LONG_PTR value)
{
    WRITER *w = (WRITER *)This;
    switch (property) {
    case XmlWriterProperty_MultiLanguage:
        w->multi_language = value != 0;
        return S_OK;
    case XmlWriterProperty_Indent:
        w->indent = value != 0;
        return S_OK;
    case XmlWriterProperty_ByteOrderMark:
        w->byte_order_mark = value != 0;
        return S_OK;
    case XmlWriterProperty_OmitXmlDeclaration:
        w->omit_declaration = value != 0;
        return S_OK;
    case XmlWriterProperty_ConformanceLevel:
        if (value < XmlConformanceLevel_Auto || value > XmlConformanceLevel_Document)
            return E_INVALIDARG;
        w->conformance = value;
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT write_start_element(WRITER *w, const WCHAR *prefix, const WCHAR *local, const WCHAR *uri)
{
    if (!local || !valid_name(local, wcslen(local), FALSE))
        return local ? WC_E_NAME : E_INVALIDARG;
    HRESULT hr = prepare_content(w, FALSE, TRUE);
    if (FAILED(hr))
        return hr;
    BOOLEAN declare = FALSE;
    const WCHAR *use = prefix ? prefix : L"";
    if (uri && *uri) {
        if (!*use) {
            const WCHAR *known = prefix_of_uri(w, uri, FALSE);
            if (known)
                use = known;
            else
                declare = TRUE;
        } else {
            const WCHAR *bound = uri_of_prefix(w, use);
            declare = !bound || wcscmp(bound, uri) != 0;
        }
    } else if (*use && !uri_of_prefix(w, use)) {
        return WR_E_NAMESPACEUNDECLARED;
    } else if (uri && !*uri && !*use) {
        const WCHAR *bound = uri_of_prefix(w, L"");
        declare = bound && *bound;
    }
    hr = indent_here(w, w->element_count);
    if (FAILED(hr))
        return hr;
    SIZE_T length = wcslen(use) + wcslen(local) + 2;
    WCHAR *qname = XmlAlloc(length * sizeof(WCHAR));
    if (!qname)
        return E_OUTOFMEMORY;
    if (*use) {
        wcscpy(qname, use);
        wcscat(qname, L":");
    }
    wcscat(qname, local);
    if (w->element_count == w->element_capacity) {
        ULONG capacity = w->element_capacity ? w->element_capacity * 2 : 16;
        OPEN_ELEMENT *larger = XmlRealloc(w->elements, capacity * sizeof(*larger));
        if (!larger) {
            XmlFree(qname);
            return E_OUTOFMEMORY;
        }
        w->elements = larger;
        w->element_capacity = capacity;
    }
    OPEN_ELEMENT *element = &w->elements[w->element_count++];
    memset(element, 0, sizeof(*element));
    element->qname = qname;
    element->bindings = w->binding_count;
    w->root_started = TRUE;
    w->start_open = TRUE;
    forget_attributes(w);
    hr = append_char(w, '<');
    if (SUCCEEDED(hr))
        hr = append_text(w, qname);
    if (SUCCEEDED(hr) && declare) {
        hr = append_text(w, *use ? L" xmlns:" : L" xmlns");
        if (SUCCEEDED(hr) && *use)
            hr = append_text(w, use);
        if (SUCCEEDED(hr))
            hr = append_text(w, L"=\"");
        if (SUCCEEDED(hr))
            hr = append_escaped(w, uri, wcslen(uri), TRUE);
        if (SUCCEEDED(hr))
            hr = append_char(w, '"');
        if (SUCCEEDED(hr))
            hr = push_binding(w, use, uri);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE writer_start_element(IXmlWriter *This, LPCWSTR prefix, LPCWSTR local, LPCWSTR uri)
{
    return write_start_element((WRITER *)This, prefix, local, uri);
}

static HRESULT note_attribute(WRITER *w, const WCHAR *qname)
{
    for (ULONG i = 0; i < w->attribute_count; i++)
        if (!wcscmp(w->attributes[i], qname))
            return WR_E_DUPLICATEATTRIBUTE;
    if (w->attribute_count == w->attribute_capacity) {
        ULONG capacity = w->attribute_capacity ? w->attribute_capacity * 2 : 8;
        WCHAR **larger = XmlRealloc(w->attributes, capacity * sizeof(*larger));
        if (!larger)
            return E_OUTOFMEMORY;
        w->attributes = larger;
        w->attribute_capacity = capacity;
    }
    WCHAR *copy = XmlDuplicate(qname, wcslen(qname));
    if (!copy)
        return E_OUTOFMEMORY;
    w->attributes[w->attribute_count++] = copy;
    return S_OK;
}

static HRESULT write_attribute_text(WRITER *w, const WCHAR *qname, const WCHAR *value)
{
    HRESULT hr = note_attribute(w, qname);
    if (SUCCEEDED(hr))
        hr = append_char(w, ' ');
    if (SUCCEEDED(hr))
        hr = append_text(w, qname);
    if (SUCCEEDED(hr))
        hr = append_text(w, L"=\"");
    if (SUCCEEDED(hr))
        hr = append_escaped(w, value, wcslen(value), TRUE);
    if (SUCCEEDED(hr))
        hr = append_char(w, '"');
    return hr;
}

static HRESULT write_attribute(WRITER *w, const WCHAR *prefix, const WCHAR *local, const WCHAR *uri, const WCHAR *value)
{
    if (!w->sink)
        return E_UNEXPECTED;
    if (!local)
        return E_INVALIDARG;
    if (!w->start_open)
        return WR_E_INVALIDACTION;
    if (!value)
        value = L"";
    if (!prefix)
        prefix = L"";
    if (!valid_name(local, wcslen(local), FALSE))
        return WC_E_NAME;

    /* the names that declare a namespace bind a prefix for the element and its content. */
    BOOLEAN default_declaration = !*prefix && !wcscmp(local, L"xmlns");
    if (default_declaration || !wcscmp(prefix, L"xmlns")) {
        const WCHAR *bound_prefix = default_declaration ? L"" : local;
        if (!default_declaration && !*value)
            return WR_E_NSPREFIXWITHEMPTYNSURI;
        if (!default_declaration && !wcscmp(local, L"xmlns"))
            return WR_E_NSPREFIXDECLARED;
        OPEN_ELEMENT *element = &w->elements[w->element_count - 1];
        for (ULONG i = element->bindings; i < w->binding_count; i++)
            if (!wcscmp(w->bindings[i].prefix, bound_prefix))
                return !wcscmp(w->bindings[i].uri, value) ? S_OK : WR_E_NSPREFIXDECLARED;
        HRESULT hr = push_binding(w, bound_prefix, value);
        if (SUCCEEDED(hr)) {
            WCHAR *qname = XmlAlloc((wcslen(local) + 8) * sizeof(WCHAR));
            if (!qname)
                return E_OUTOFMEMORY;
            wcscpy(qname, L"xmlns");
            if (!default_declaration) {
                wcscat(qname, L":");
                wcscat(qname, local);
            }
            hr = write_attribute_text(w, qname, value);
            XmlFree(qname);
        }
        return hr;
    }

    const WCHAR *use = prefix;
    if (uri && *uri) {
        if (!*use) {
            use = prefix_of_uri(w, uri, TRUE);
            if (!use) {
                WCHAR generated[24];
                do {
                    format_number(generated, L"p", ++w->generated, FALSE, L"");
                } while (uri_of_prefix(w, generated));
                HRESULT hr = write_attribute(w, L"xmlns", generated, nullptr, uri);
                if (FAILED(hr))
                    return hr;
                return write_attribute(w, generated, local, uri, value);
            }
        } else {
            const WCHAR *bound = uri_of_prefix(w, use);
            if (!bound || wcscmp(bound, uri) != 0) {
                HRESULT hr = write_attribute(w, L"xmlns", use, nullptr, uri);
                if (FAILED(hr))
                    return hr;
            }
        }
    } else if (*use && !uri_of_prefix(w, use)) {
        return WR_E_NAMESPACEUNDECLARED;
    }
    WCHAR *qname = XmlAlloc((wcslen(use) + wcslen(local) + 2) * sizeof(WCHAR));
    if (!qname)
        return E_OUTOFMEMORY;
    if (*use) {
        wcscpy(qname, use);
        wcscat(qname, L":");
    }
    wcscat(qname, local);
    HRESULT hr = write_attribute_text(w, qname, value);
    XmlFree(qname);
    return hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_attribute_string(IXmlWriter *This, LPCWSTR prefix, LPCWSTR local, LPCWSTR uri,
                                                               LPCWSTR value)
{
    return write_attribute((WRITER *)This, prefix, local, uri, value);
}

static HRESULT end_element(WRITER *w, BOOLEAN full)
{
    if (!w->sink)
        return E_UNEXPECTED;
    if (!w->element_count)
        return WR_E_INVALIDACTION;
    OPEN_ELEMENT *element = &w->elements[w->element_count - 1];
    HRESULT hr;
    if (w->start_open && !full) {
        w->start_open = FALSE;
        forget_attributes(w);
        hr = append_text(w, L"/>");
    } else {
        hr = close_start_tag(w);
        if (SUCCEEDED(hr) && element->children && !element->text)
            hr = indent_here(w, w->element_count - 1);
        if (SUCCEEDED(hr))
            hr = append_text(w, L"</");
        if (SUCCEEDED(hr))
            hr = append_text(w, element->qname);
        if (SUCCEEDED(hr))
            hr = append_char(w, '>');
    }
    drop_bindings(w, element->bindings);
    XmlFree(element->qname);
    w->element_count--;
    return hr;
}

static HRESULT STDMETHODCALLTYPE writer_end_element(IXmlWriter *This)
{
    return end_element((WRITER *)This, FALSE);
}

static HRESULT STDMETHODCALLTYPE writer_full_end_element(IXmlWriter *This)
{
    return end_element((WRITER *)This, TRUE);
}

static HRESULT STDMETHODCALLTYPE writer_end_document(IXmlWriter *This)
{
    WRITER *w = (WRITER *)This;
    if (!w->sink)
        return E_UNEXPECTED;
    HRESULT hr = S_OK;
    while (w->element_count && SUCCEEDED(hr))
        hr = end_element(w, FALSE);
    if (SUCCEEDED(hr))
        hr = close_start_tag(w);
    w->document_ended = TRUE;
    if (SUCCEEDED(hr))
        hr = flush_buffer(w);
    return hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_string(IXmlWriter *This, LPCWSTR text)
{
    WRITER *w = (WRITER *)This;
    if (!text)
        return S_OK;
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    return SUCCEEDED(hr) ? append_escaped(w, text, wcslen(text), FALSE) : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_chars(IXmlWriter *This, const WCHAR *chars, UINT count)
{
    WRITER *w = (WRITER *)This;
    if (!chars && count)
        return E_INVALIDARG;
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    return SUCCEEDED(hr) ? append_escaped(w, chars, count, FALSE) : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_cdata(IXmlWriter *This, LPCWSTR text)
{
    WRITER *w = (WRITER *)This;
    if (!text)
        text = L"";
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    if (FAILED(hr))
        return hr;
    hr = append_text(w, L"<![CDATA[");
    for (const WCHAR *p = text; *p && SUCCEEDED(hr); p++) {
        if (!is_xml_char(*p))
            return WC_E_XMLCHARACTER;
        if (p[0] == ']' && p[1] == ']' && p[2] == '>') {
            hr = append_text(w, L"]]]]><![CDATA[");
            p++;
            continue;
        }
        hr = append_char(w, *p);
    }
    return SUCCEEDED(hr) ? append_text(w, L"]]>") : hr;
}

static HRESULT write_character_reference(WRITER *w, ULONG code)
{
    WCHAR text[16];
    format_number(text, L"&#x", code, TRUE, L";");
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    return SUCCEEDED(hr) ? append_text(w, text) : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_char_entity(IXmlWriter *This, WCHAR character)
{
    if (character >= 0xD800 && character < 0xE000)
        return WR_E_INVALIDSURROGATEPAIR;
    return write_character_reference((WRITER *)This, character);
}

static HRESULT STDMETHODCALLTYPE writer_write_surrogate(IXmlWriter *This, WCHAR low, WCHAR high)
{
    if (high < 0xD800 || high > 0xDBFF || low < 0xDC00 || low > 0xDFFF)
        return WR_E_INVALIDSURROGATEPAIR;
    return write_character_reference((WRITER *)This, 0x10000 + (((ULONG)high - 0xD800) << 10) + (low - 0xDC00));
}

static HRESULT STDMETHODCALLTYPE writer_write_comment(IXmlWriter *This, LPCWSTR text)
{
    WRITER *w = (WRITER *)This;
    if (!text)
        text = L"";
    SIZE_T length = wcslen(text);
    if (wcsstr(text, L"--") || (length && text[length - 1] == '-'))
        return WC_E_COMMENT;
    HRESULT hr = prepare_content(w, FALSE, FALSE);
    if (SUCCEEDED(hr))
        hr = indent_here(w, w->element_count);
    if (SUCCEEDED(hr))
        hr = append_text(w, L"<!--");
    if (SUCCEEDED(hr))
        hr = append(w, text, length);
    return SUCCEEDED(hr) ? append_text(w, L"-->") : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_doctype(IXmlWriter *This, LPCWSTR name, LPCWSTR public_id, LPCWSTR system_id,
                                                     LPCWSTR subset)
{
    WRITER *w = (WRITER *)This;
    if (!name)
        return E_INVALIDARG;
    if (!valid_name(name, wcslen(name), FALSE))
        return WC_E_NAME;
    if (w->root_started || w->element_count)
        return WR_E_INVALIDACTION;
    HRESULT hr = prepare_content(w, FALSE, FALSE);
    if (SUCCEEDED(hr))
        hr = indent_here(w, 0);
    if (SUCCEEDED(hr))
        hr = append_text(w, L"<!DOCTYPE ");
    if (SUCCEEDED(hr))
        hr = append_text(w, name);
    if (SUCCEEDED(hr) && public_id) {
        hr = append_text(w, L" PUBLIC \"");
        if (SUCCEEDED(hr))
            hr = append_text(w, public_id);
        if (SUCCEEDED(hr))
            hr = append_text(w, L"\" \"");
        if (SUCCEEDED(hr))
            hr = append_text(w, system_id ? system_id : L"");
        if (SUCCEEDED(hr))
            hr = append_char(w, '"');
    } else if (SUCCEEDED(hr) && system_id) {
        hr = append_text(w, L" SYSTEM \"");
        if (SUCCEEDED(hr))
            hr = append_text(w, system_id);
        if (SUCCEEDED(hr))
            hr = append_char(w, '"');
    }
    if (SUCCEEDED(hr) && subset) {
        hr = append_text(w, L" [");
        if (SUCCEEDED(hr))
            hr = append_text(w, subset);
        if (SUCCEEDED(hr))
            hr = append_char(w, ']');
    }
    return SUCCEEDED(hr) ? append_char(w, '>') : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_element_string(IXmlWriter *This, LPCWSTR prefix, LPCWSTR local, LPCWSTR uri,
                                                            LPCWSTR value)
{
    WRITER *w = (WRITER *)This;
    HRESULT hr = write_start_element(w, prefix, local, uri);
    if (SUCCEEDED(hr) && value && *value)
        hr = writer_write_string(This, value);
    if (SUCCEEDED(hr))
        hr = end_element(w, FALSE);
    return hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_entity_ref(IXmlWriter *This, LPCWSTR name)
{
    WRITER *w = (WRITER *)This;
    if (!name)
        return E_INVALIDARG;
    if (!valid_name(name, wcslen(name), FALSE))
        return WC_E_NAME;
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    if (SUCCEEDED(hr))
        hr = append_char(w, '&');
    if (SUCCEEDED(hr))
        hr = append_text(w, name);
    return SUCCEEDED(hr) ? append_char(w, ';') : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_name(IXmlWriter *This, LPCWSTR name)
{
    WRITER *w = (WRITER *)This;
    if (!name)
        return E_INVALIDARG;
    if (!valid_name(name, wcslen(name), FALSE))
        return WC_E_NAME;
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    return SUCCEEDED(hr) ? append_text(w, name) : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_nmtoken(IXmlWriter *This, LPCWSTR token)
{
    WRITER *w = (WRITER *)This;
    if (!token)
        return E_INVALIDARG;
    if (!valid_name(token, wcslen(token), TRUE))
        return WC_E_NAMECHARACTER;
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    return SUCCEEDED(hr) ? append_text(w, token) : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_pi(IXmlWriter *This, LPCWSTR name, LPCWSTR text)
{
    WRITER *w = (WRITER *)This;
    if (!name)
        return E_INVALIDARG;
    if (!valid_name(name, wcslen(name), FALSE))
        return WC_E_NAME;
    if (text && wcsstr(text, L"?>"))
        return WC_E_PI;
    if (is_xml_name(name) && w->anything_written)
        return WR_E_INVALIDACTION;
    HRESULT hr = prepare_content(w, FALSE, FALSE);
    if (SUCCEEDED(hr))
        hr = indent_here(w, w->element_count);
    if (SUCCEEDED(hr))
        hr = append_text(w, L"<?");
    if (SUCCEEDED(hr))
        hr = append_text(w, name);
    if (SUCCEEDED(hr) && text && *text) {
        hr = append_char(w, ' ');
        if (SUCCEEDED(hr))
            hr = append_text(w, text);
    }
    return SUCCEEDED(hr) ? append_text(w, L"?>") : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_qualified_name(IXmlWriter *This, LPCWSTR local, LPCWSTR uri)
{
    WRITER *w = (WRITER *)This;
    if (!local)
        return E_INVALIDARG;
    if (!valid_name(local, wcslen(local), FALSE))
        return WC_E_NAME;
    const WCHAR *prefix = L"";
    if (uri && *uri) {
        prefix = prefix_of_uri(w, uri, FALSE);
        if (!prefix)
            return WR_E_NAMESPACEUNDECLARED;
    }
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    if (SUCCEEDED(hr) && *prefix) {
        hr = append_text(w, prefix);
        if (SUCCEEDED(hr))
            hr = append_char(w, ':');
    }
    return SUCCEEDED(hr) ? append_text(w, local) : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_raw(IXmlWriter *This, LPCWSTR text)
{
    WRITER *w = (WRITER *)This;
    if (!text)
        return S_OK;
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    return SUCCEEDED(hr) ? append_text(w, text) : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_raw_chars(IXmlWriter *This, const WCHAR *chars, UINT count)
{
    WRITER *w = (WRITER *)This;
    if (!chars && count)
        return E_INVALIDARG;
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    return SUCCEEDED(hr) ? append(w, chars, count) : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_whitespace(IXmlWriter *This, LPCWSTR whitespace)
{
    WRITER *w = (WRITER *)This;
    if (!whitespace)
        return S_OK;
    for (const WCHAR *p = whitespace; *p; p++)
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            return WR_E_NONWHITESPACE;
    HRESULT hr = prepare_content(w, TRUE, FALSE);
    return SUCCEEDED(hr) ? append_text(w, whitespace) : hr;
}

static HRESULT STDMETHODCALLTYPE writer_write_start_document(IXmlWriter *This, UINT standalone)
{
    WRITER *w = (WRITER *)This;
    if (!w->sink)
        return E_UNEXPECTED;
    if (w->anything_written || w->element_count || w->document_ended)
        return WR_E_INVALIDACTION;
    if (standalone > 2)
        return E_INVALIDARG;
    if (w->omit_declaration)
        return S_OK;
    const WCHAR *encoding = w->code_page == 1200 ? L"UTF-16" : w->code_page == 65001 ? L"UTF-8" : nullptr;
    HRESULT hr = append_text(w, L"<?xml version=\"1.0\"");
    if (SUCCEEDED(hr) && encoding) {
        hr = append_text(w, L" encoding=\"");
        if (SUCCEEDED(hr))
            hr = append_text(w, encoding);
        if (SUCCEEDED(hr))
            hr = append_char(w, '"');
    }
    if (SUCCEEDED(hr) && standalone)
        hr = append_text(w, standalone == 1 ? L" standalone=\"yes\"" : L" standalone=\"no\"");
    return SUCCEEDED(hr) ? append_text(w, L"?>") : hr;
}

static HRESULT STDMETHODCALLTYPE writer_flush(IXmlWriter *This)
{
    return flush_buffer((WRITER *)This);
}

/* ------------------------------------------------------------------ copying from a reader */

static HRESULT reader_string(IXmlReader *reader, HRESULT(STDMETHODCALLTYPE *getter)(IXmlReader *, LPCWSTR *, UINT *),
                             LPCWSTR *text)
{
    UINT length;
    return getter(reader, text, &length);
}

static HRESULT write_current_attribute(WRITER *w, IXmlReader *reader)
{
    LPCWSTR prefix, local, uri, value;
    HRESULT hr = reader_string(reader, reader->lpVtbl->GetPrefix, &prefix);
    if (SUCCEEDED(hr))
        hr = reader_string(reader, reader->lpVtbl->GetLocalName, &local);
    if (SUCCEEDED(hr))
        hr = reader_string(reader, reader->lpVtbl->GetNamespaceUri, &uri);
    if (SUCCEEDED(hr))
        hr = reader_string(reader, reader->lpVtbl->GetValue, &value);
    if (FAILED(hr))
        return hr;
    /* the declarations of namespaces carry the xmlns namespace, which is not a namespace of the attribute here. */
    BOOLEAN declaration = !wcscmp(prefix, L"xmlns") || (!*prefix && !wcscmp(local, L"xmlns"));
    return write_attribute(w, prefix, local, declaration ? nullptr : (*uri ? uri : nullptr), value);
}

static HRESULT STDMETHODCALLTYPE writer_write_attributes(IXmlWriter *This, IXmlReader *reader, BOOL defaults)
{
    WRITER *w = (WRITER *)This;
    if (!reader)
        return E_INVALIDARG;
    XmlNodeType type;
    HRESULT hr = reader->lpVtbl->GetNodeType(reader, &type);
    if (FAILED(hr))
        return hr;
    if (type == XmlNodeType_Element || type == XmlNodeType_XmlDeclaration) {
        hr = reader->lpVtbl->MoveToFirstAttribute(reader);
        if (hr != S_OK)
            return hr == S_FALSE ? S_OK : hr;
    } else if (type != XmlNodeType_Attribute) {
        return E_UNEXPECTED;
    }
    do {
        hr = write_current_attribute(w, reader);
        if (FAILED(hr))
            return hr;
    } while (reader->lpVtbl->MoveToNextAttribute(reader) == S_OK);
    return S_OK;
}

static HRESULT write_shallow(IXmlWriter *This, IXmlReader *reader)
{
    WRITER *w = (WRITER *)This;
    XmlNodeType type;
    HRESULT hr = reader->lpVtbl->GetNodeType(reader, &type);
    if (FAILED(hr))
        return hr;
    LPCWSTR prefix = L"", local = L"", uri = L"", value = L"";
    UINT length;
    switch (type) {
    case XmlNodeType_Element:
        reader->lpVtbl->GetPrefix(reader, &prefix, &length);
        reader->lpVtbl->GetLocalName(reader, &local, &length);
        reader->lpVtbl->GetNamespaceUri(reader, &uri, &length);
        hr = write_start_element(w, *prefix ? prefix : nullptr, local, *uri ? uri : nullptr);
        if (SUCCEEDED(hr))
            hr = writer_write_attributes(This, reader, FALSE);
        if (SUCCEEDED(hr))
            reader->lpVtbl->MoveToElement(reader);
        if (SUCCEEDED(hr) && reader->lpVtbl->IsEmptyElement(reader))
            hr = end_element(w, FALSE);
        return hr;
    case XmlNodeType_Attribute:
        return write_current_attribute(w, reader);
    case XmlNodeType_Text:
    case XmlNodeType_Whitespace:
        reader->lpVtbl->GetValue(reader, &value, &length);
        return type == XmlNodeType_Text ? writer_write_string(This, value) : writer_write_whitespace(This, value);
    case XmlNodeType_CDATA:
        reader->lpVtbl->GetValue(reader, &value, &length);
        return writer_write_cdata(This, value);
    case XmlNodeType_Comment:
        reader->lpVtbl->GetValue(reader, &value, &length);
        return writer_write_comment(This, value);
    case XmlNodeType_ProcessingInstruction:
        reader->lpVtbl->GetLocalName(reader, &local, &length);
        reader->lpVtbl->GetValue(reader, &value, &length);
        return writer_write_pi(This, local, value);
    case XmlNodeType_DocumentType:
        reader->lpVtbl->GetLocalName(reader, &local, &length);
        reader->lpVtbl->GetValue(reader, &value, &length);
        return writer_write_doctype(This, local, nullptr, nullptr, *value ? value : nullptr);
    case XmlNodeType_EndElement:
        return end_element(w, FALSE);
    case XmlNodeType_XmlDeclaration: {
        UINT standalone = 0;
        if (reader->lpVtbl->MoveToAttributeByName(reader, L"standalone", nullptr) == S_OK) {
            reader->lpVtbl->GetValue(reader, &value, &length);
            standalone = !wcscmp(value, L"yes") ? 1 : !wcscmp(value, L"no") ? 2 : 0;
            reader->lpVtbl->MoveToElement(reader);
        }
        return writer_write_start_document(This, standalone);
    }
    default:
        return S_OK;
    }
}

static HRESULT STDMETHODCALLTYPE writer_write_node_shallow(IXmlWriter *This, IXmlReader *reader, BOOL defaults)
{
    if (!reader)
        return E_INVALIDARG;
    XmlNodeType type;
    HRESULT hr = reader->lpVtbl->GetNodeType(reader, &type);
    if (hr == S_FALSE) {
        hr = reader->lpVtbl->Read(reader, &type);
        if (hr != S_OK)
            return hr;
    } else if (FAILED(hr)) {
        return hr;
    }
    return write_shallow(This, reader);
}

static HRESULT STDMETHODCALLTYPE writer_write_node(IXmlWriter *This, IXmlReader *reader, BOOL defaults)
{
    if (!reader)
        return E_INVALIDARG;
    XmlNodeType type;
    HRESULT hr = reader->lpVtbl->GetNodeType(reader, &type);
    if (hr == S_FALSE) {
        hr = reader->lpVtbl->Read(reader, &type);
        if (hr != S_OK)
            return hr;
    } else if (FAILED(hr)) {
        return hr;
    }
    UINT depth;
    reader->lpVtbl->GetDepth(reader, &depth);
    hr = write_shallow(This, reader);
    if (FAILED(hr) || type != XmlNodeType_Element || reader->lpVtbl->IsEmptyElement(reader))
        return hr;
    for (;;) {
        hr = reader->lpVtbl->Read(reader, &type);
        if (hr != S_OK)
            return hr == S_FALSE ? E_UNEXPECTED : hr;
        UINT now;
        reader->lpVtbl->GetDepth(reader, &now);
        hr = write_shallow(This, reader);
        if (FAILED(hr))
            return hr;
        if (type == XmlNodeType_EndElement && now == depth)
            return S_OK;
    }
}

static const IXmlWriterVtbl g_writer_vtbl = {
    writer_query,           writer_add_ref,          writer_release,           writer_set_output,
    writer_get_property,    writer_set_property,     writer_write_attributes,  writer_write_attribute_string,
    writer_write_cdata,     writer_write_char_entity, writer_write_chars,      writer_write_comment,
    writer_write_doctype,   writer_write_element_string, writer_end_document,   writer_end_element,
    writer_write_entity_ref, writer_full_end_element, writer_write_name,       writer_write_nmtoken,
    writer_write_node,      writer_write_node_shallow, writer_write_pi,        writer_write_qualified_name,
    writer_write_raw,       writer_write_raw_chars,  writer_write_start_document, writer_start_element,
    writer_write_string,    writer_write_surrogate,  writer_write_whitespace,  writer_flush,
};

HRESULT XmlCreateWriter(REFIID iid, void **object)
{
    if (!IsEqualGUID(iid, &IID_IXmlWriter) && !IsEqualGUID(iid, &IID_IUnknown))
        return E_NOINTERFACE;
    WRITER *w = XmlAlloc(sizeof(*w));
    if (!w)
        return E_OUTOFMEMORY;
    w->lpVtbl = &g_writer_vtbl;
    w->references = 1;
    w->code_page = 65001;
    w->byte_order_mark = TRUE;
    *object = w;
    return S_OK;
}
