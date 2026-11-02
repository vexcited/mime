/*
 * the document of a web browser control, as a program writes a page into it: IHTMLDocument2::write, writeln, open and
 * close, and IPersistStreamInit::Load. mshtml's whole object model is not here, the page is what the program writes and
 * the browser's engine shows it when the document is closed. a method of IHTMLDocument2 that is not done fails.
 */
#include "ieframe_private.h"
#include <mime/win32/oleauto.h>
#include <mime/win32/ocidl.h>

#define SLOT_COUNT 160

typedef struct DOCUMENT {
    const void *const *Vtbl;
    IPersistStreamInit Persist;
    LONG Refs;
    BROWSER *Browser;
    LPWSTR Html;
    SIZE_T Length;
    SIZE_T Capacity;
    BOOL Open;
} DOCUMENT;

IF_GUID(IID_IHTMLDocument, 0x626FC520, 0xA41E, 0x11CF, 0xA7, 0x31, 0x00, 0xA0, 0xC9, 0x08, 0x26, 0x37);
IF_GUID(IID_IHTMLDocument2, 0x332C4425, 0x26CB, 0x11D0, 0xB4, 0x83, 0x00, 0xC0, 0x4F, 0xD9, 0x01, 0x19);

static HRESULT STDMETHODCALLTYPE doc_query(DOCUMENT *self, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDispatch) || IsEqualGUID(iid, &IID_IHTMLDocument) ||
        IsEqualGUID(iid, &IID_IHTMLDocument2))
        *object = &self->Vtbl;
    else if (IsEqualGUID(iid, &IID_IPersistStreamInit) || IsEqualGUID(iid, &IID_IPersistStream) ||
             IsEqualGUID(iid, &IID_IPersist))
        *object = &self->Persist;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&self->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE doc_add_ref(DOCUMENT *self)
{
    return (ULONG)InterlockedIncrement(&self->Refs);
}

static ULONG STDMETHODCALLTYPE doc_release(DOCUMENT *self)
{
    LONG refs = InterlockedDecrement(&self->Refs);
    if (!refs) {
        BrowserRelease(self->Browser);
        HeapFree(GetProcessHeap(), 0, self->Html);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return (ULONG)refs;
}

/* ------------------------------------------------------------------ the text of the page */

static BOOL append(DOCUMENT *d, const WCHAR *text, SIZE_T length)
{
    if (d->Length + length + 1 > d->Capacity) {
        SIZE_T capacity = d->Capacity ? d->Capacity * 2 : 4096;
        while (capacity < d->Length + length + 1)
            capacity *= 2;
        LPWSTR html = d->Html ? HeapReAlloc(GetProcessHeap(), 0, d->Html, capacity * sizeof(WCHAR))
                              : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(WCHAR));
        if (!html)
            return FALSE;
        d->Html = html;
        d->Capacity = capacity;
    }
    memcpy(d->Html + d->Length, text, length * sizeof(WCHAR));
    d->Length += length;
    d->Html[d->Length] = 0;
    return TRUE;
}

static void restart(DOCUMENT *d)
{
    d->Length = 0;
    if (d->Html)
        d->Html[0] = 0;
    d->Open = TRUE;
}

/* the strings of a safe array of variants, put at the end of the page. */
static HRESULT write_array(DOCUMENT *d, SAFEARRAY *array, BOOL line)
{
    if (!array)
        return E_INVALIDARG;
    LONG low, high;
    VARIANT *items;
    if (FAILED(SafeArrayGetLBound(array, 1, &low)) || FAILED(SafeArrayGetUBound(array, 1, &high)) ||
        FAILED(SafeArrayAccessData(array, (void **)&items)))
        return E_INVALIDARG;
    if (!d->Open)
        restart(d);
    HRESULT hr = S_OK;
    for (LONG i = 0; i <= high - low && SUCCEEDED(hr); i++) {
        VARIANT text;
        VariantInit(&text);
        if (SUCCEEDED(VariantChangeType(&text, &items[i], 0, VT_BSTR))) {
            if (!append(d, text.bstrVal ? text.bstrVal : L"", SysStringLen(text.bstrVal)))
                hr = E_OUTOFMEMORY;
            VariantClear(&text);
        }
    }
    if (line && SUCCEEDED(hr) && !append(d, L"\r\n", 2))
        hr = E_OUTOFMEMORY;
    SafeArrayUnaccessData(array);
    return hr;
}

static HRESULT show(DOCUMENT *d)
{
    d->Open = FALSE;
    return BrowserShowHtml(d->Browser, d->Html ? d->Html : L"");
}

/* ------------------------------------------------------------------ IHTMLDocument2 */

static HRESULT STDMETHODCALLTYPE hd_query(IDispatch *self, REFIID iid, void **object)
{
    return doc_query((DOCUMENT *)self, iid, object);
}

static ULONG STDMETHODCALLTYPE hd_add_ref(IDispatch *self)
{
    return doc_add_ref((DOCUMENT *)self);
}

static ULONG STDMETHODCALLTYPE hd_release(IDispatch *self)
{
    return doc_release((DOCUMENT *)self);
}

static HRESULT STDMETHODCALLTYPE hd_type_info_count(IDispatch *self, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hd_type_info(IDispatch *self, UINT index, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE hd_names(IDispatch *self, REFIID iid, LPOLESTR *names, UINT count, LCID lcid,
                                          DISPID *ids)
{
    for (UINT i = 0; i < count; i++)
        ids[i] = DISPID_UNKNOWN;
    return DISP_E_UNKNOWNNAME;
}

static HRESULT STDMETHODCALLTYPE hd_invoke(IDispatch *self, DISPID id, REFIID iid, LCID lcid, WORD flags,
                                           DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT STDMETHODCALLTYPE hd_write(IDispatch *self, SAFEARRAY *array)
{
    return write_array((DOCUMENT *)self, array, FALSE);
}

static HRESULT STDMETHODCALLTYPE hd_writeln(IDispatch *self, SAFEARRAY *array)
{
    return write_array((DOCUMENT *)self, array, TRUE);
}

static HRESULT STDMETHODCALLTYPE hd_open(IDispatch *self, BSTR url, VARIANT name, VARIANT features, VARIANT replace,
                                         IDispatch **window)
{
    restart((DOCUMENT *)self);
    if (window) {
        *window = nullptr;
        return self->lpVtbl->QueryInterface(self, &IID_IDispatch, (void **)window);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hd_close(IDispatch *self)
{
    DOCUMENT *d = (DOCUMENT *)self;
    return d->Open ? show(d) : S_OK;
}

static HRESULT STDMETHODCALLTYPE hd_clear(IDispatch *self)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hd_get_ready_state(IDispatch *self, BSTR *state)
{
    *state = SysAllocString(L"complete");
    return *state ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE hd_get_url(IDispatch *self, BSTR *url)
{
    *url = SysAllocString(L"about:blank");
    return *url ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE hd_fail(void)
{
    return E_NOTIMPL;
}

#define SLOT_PUT(table, index, function) table[index] = (const void *)function

static const void *g_slots[SLOT_COUNT];

static void fill_slots(void)
{
    if (g_slots[0])
        return;
    for (int i = 0; i < SLOT_COUNT; i++)
        g_slots[i] = (const void *)hd_fail;
    SLOT_PUT(g_slots, 0, hd_query);
    SLOT_PUT(g_slots, 1, hd_add_ref);
    SLOT_PUT(g_slots, 2, hd_release);
    SLOT_PUT(g_slots, 3, hd_type_info_count);
    SLOT_PUT(g_slots, 4, hd_type_info);
    SLOT_PUT(g_slots, 5, hd_names);
    SLOT_PUT(g_slots, 6, hd_invoke);
    SLOT_PUT(g_slots, 22, hd_get_ready_state);
    SLOT_PUT(g_slots, 40, hd_get_url);
    SLOT_PUT(g_slots, 59, hd_write);
    SLOT_PUT(g_slots, 60, hd_writeln);
    SLOT_PUT(g_slots, 61, hd_open);
    SLOT_PUT(g_slots, 62, hd_close);
    SLOT_PUT(g_slots, 63, hd_clear);
}

/* ------------------------------------------------------------------ IPersistStreamInit */

#define DOCUMENT_FROM_PERSIST(self) ((DOCUMENT *)((BYTE *)(self) - offsetof(DOCUMENT, Persist)))

static HRESULT STDMETHODCALLTYPE ps_query(IPersistStreamInit *self, REFIID iid, void **object)
{
    return doc_query(DOCUMENT_FROM_PERSIST(self), iid, object);
}

static ULONG STDMETHODCALLTYPE ps_add_ref(IPersistStreamInit *self)
{
    return doc_add_ref(DOCUMENT_FROM_PERSIST(self));
}

static ULONG STDMETHODCALLTYPE ps_release(IPersistStreamInit *self)
{
    return doc_release(DOCUMENT_FROM_PERSIST(self));
}

static HRESULT STDMETHODCALLTYPE ps_get_class_id(IPersistStreamInit *self, CLSID *clsid)
{
    static const CLSID html = {0x25336920, 0x03F9, 0x11CF, {0x8F, 0xD0, 0x00, 0xAA, 0x00, 0x68, 0x6B, 0x6E}};
    *clsid = html;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ps_is_dirty(IPersistStreamInit *self)
{
    return S_FALSE;
}

/* the page of a stream: utf-16 with a byte order mark, else utf-8, else the ansi code page. */
static HRESULT STDMETHODCALLTYPE ps_load(IPersistStreamInit *self, IStream *stream)
{
    DOCUMENT *d = DOCUMENT_FROM_PERSIST(self);
    if (!stream)
        return E_INVALIDARG;
    BYTE *bytes = nullptr;
    SIZE_T size = 0, capacity = 0;
    for (;;) {
        if (size + 4096 > capacity) {
            capacity = capacity ? capacity * 2 : 16384;
            BYTE *grown = bytes ? HeapReAlloc(GetProcessHeap(), 0, bytes, capacity) : HeapAlloc(GetProcessHeap(), 0, capacity);
            if (!grown) {
                HeapFree(GetProcessHeap(), 0, bytes);
                return E_OUTOFMEMORY;
            }
            bytes = grown;
        }
        ULONG got = 0;
        HRESULT hr = stream->lpVtbl->Read(stream, bytes + size, 4096, &got);
        if (FAILED(hr) || !got)
            break;
        size += got;
    }
    restart(d);
    BOOL ok;
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        ok = append(d, (const WCHAR *)(bytes + 2), (size - 2) / sizeof(WCHAR));
    } else {
        SIZE_T skip = size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
        int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCSTR)bytes + skip, (int)(size - skip), nullptr, 0);
        UINT page = chars ? CP_UTF8 : CP_ACP;
        chars = MultiByteToWideChar(page, 0, (LPCSTR)bytes + skip, (int)(size - skip), nullptr, 0);
        LPWSTR text = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)chars + 1) * sizeof(WCHAR));
        ok = text != nullptr;
        if (ok) {
            MultiByteToWideChar(page, 0, (LPCSTR)bytes + skip, (int)(size - skip), text, chars);
            ok = append(d, text, (SIZE_T)chars);
            HeapFree(GetProcessHeap(), 0, text);
        }
    }
    HeapFree(GetProcessHeap(), 0, bytes);
    return ok ? show(d) : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE ps_save(IPersistStreamInit *self, IStream *stream, BOOL clear)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ps_get_size_max(IPersistStreamInit *self, ULARGE_INTEGER *size)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ps_init_new(IPersistStreamInit *self)
{
    restart(DOCUMENT_FROM_PERSIST(self));
    return S_OK;
}

static const IPersistStreamInitVtbl g_persist_vtbl = {
    ps_query, ps_add_ref, ps_release, ps_get_class_id, ps_is_dirty, ps_load, ps_save, ps_get_size_max, ps_init_new,
};

IDispatch *DocumentCreate(BROWSER *browser)
{
    fill_slots();
    DOCUMENT *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return nullptr;
    d->Vtbl = g_slots;
    d->Persist.lpVtbl = &g_persist_vtbl;
    d->Refs = 1;
    d->Browser = browser;
    BrowserAddRef(browser);
    return (IDispatch *)d;
}
