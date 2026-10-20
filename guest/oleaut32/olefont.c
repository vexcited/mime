/*
 * the standard font object of OleCreateFontIndirect. Objects that ask for the same font share one cached gdi font,
 * which stays alive as long as an object or a held reference uses it.
 */
#include "oleaut32_private.h"

#define FONT_ITALIC 0x02
#define FONT_UNDERLINE 0x04
#define FONT_STRIKE 0x08
#define FONT_NAME_SET 0x01
#define FONT_CHARSET_SET 0x02

typedef struct {
    LONGLONG size;
    LONG logical, himetric;
    SHORT charset, weight;
    BYTE flags;
    WCHAR face[32];
} FONT_RECORD;

typedef struct {
    FONT_RECORD request, actual;
    HFONT hfont;
    ULONG refs;
    TEXTMETRICW metrics;
} FONT_CACHE;

typedef struct FONT {
    IFont Font;
    IDispatch Disp;
    IPersistStream Persist;
    LONG Refs;
    HDC Hdc;
    HFONT Hfont;
    int Cache;
    FONT_RECORD Record;
    BYTE Priority;
} FONT;

#define FONT_FROM_DISP(p) CONTAINING_RECORD(p, FONT, Disp)
#define FONT_FROM_PERSIST(p) CONTAINING_RECORD(p, FONT, Persist)

static SRWLOCK font_lock = SRWLOCK_INIT;
static FONT_CACHE *font_cache;
static int font_cache_count;
static LONG font_objects;
static HDC font_dc;

MIME_DEFINE_GUID(IID_StdOle, 0x00020430, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

/* the default of a description that is not given: eight points of the system's sans serif. */
static const FONTDESC default_desc = {sizeof(FONTDESC), (LPOLESTR)L"MS Sans Serif", {.int64 = 80000}, 400, 1, 0, 0, 0};

/* ------------------------------------------------------------------ arithmetic */

LONGLONG AutRoundDiv64(LONGLONG numerator, LONGLONG denominator)
{
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
    }
    LONGLONG half = denominator / 2;
    return (numerator < 0 ? numerator - half : numerator + half) / denominator;
}

/* a face name of at most 31 characters. */
static void font_copy_face(WCHAR *dest, const WCHAR *source)
{
    int i = 0;
    for (; i < 31 && source[i]; i++)
        dest[i] = source[i];
    dest[i] = 0;
}

static HDC font_screen_dc(void)
{
    HDC dc = font_dc;
    if (dc)
        return dc;
    dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return nullptr;
    HDC previous = InterlockedCompareExchangePointer((PVOID *)&font_dc, dc, nullptr);
    if (previous) {
        DeleteDC(dc);
        dc = previous;
    }
    return dc;
}

static HDC font_hdc(const FONT *font)
{
    return font->Hdc ? font->Hdc : font_screen_dc();
}

static int font_dpi(const FONT *font)
{
    HDC dc = font_hdc(font);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 0;
    return dpi ? dpi : 96;
}

/* ------------------------------------------------------------------ the cache */

static BOOL font_record_equal(const FONT_RECORD *a, const FONT_RECORD *b)
{
    return a->size == b->size && !_wcsicmp(a->face, b->face) && a->flags == b->flags && a->weight == b->weight &&
           a->charset == b->charset && a->logical == b->logical && a->himetric == b->himetric;
}

static int font_find(const FONT_RECORD *record)
{
    for (int i = 0; i < font_cache_count; i++) {
        FONT_CACHE *entry = &font_cache[i];
        if (entry->hfont && (font_record_equal(&entry->request, record) || font_record_equal(&entry->actual, record)))
            return i;
    }
    return -1;
}

/* an entry nobody uses, its gdi font deleted, or a new one at the end. */
static int font_new_entry(void)
{
    for (int i = 0; i < font_cache_count; i++) {
        FONT_CACHE *entry = &font_cache[i];
        if (entry->refs == 0) {
            if (entry->hfont)
                DeleteObject(entry->hfont);
            memset(entry, 0, sizeof(*entry));
            return i;
        }
    }
    int count = font_cache_count + 8;
    FONT_CACHE *grown = AutAllocZero((SIZE_T)count * sizeof(FONT_CACHE));
    if (!grown)
        return -1;
    if (font_cache_count)
        memcpy(grown, font_cache, (SIZE_T)font_cache_count * sizeof(FONT_CACHE));
    AutFree(font_cache);
    int index = font_cache_count;
    font_cache = grown;
    font_cache_count = count;
    return index;
}

static void font_cache_free(void)
{
    for (int i = 0; i < font_cache_count; i++) {
        if (font_cache[i].hfont)
            DeleteObject(font_cache[i].hfont);
    }
    AutFree(font_cache);
    font_cache = nullptr;
    font_cache_count = 0;
}

static HRESULT font_hresult(DWORD error)
{
    return error ? (HRESULT)((error & 0xFFFF) | 0x80070000) : E_FAIL;
}

static HRESULT font_create_matching(FONT *font, HFONT *result)
{
    const FONT_RECORD *r = &font->Record;
    LOGFONTW lf = {};
    font_copy_face(lf.lfFaceName, r->face);
    lf.lfWeight = r->weight;
    lf.lfCharSet = (BYTE)r->charset;
    lf.lfItalic = (r->flags & FONT_ITALIC) != 0;
    lf.lfUnderline = (r->flags & FONT_UNDERLINE) != 0;
    lf.lfStrikeOut = (r->flags & FONT_STRIKE) != 0;
    LONGLONG height;
    if (r->logical == 1 && r->himetric == 1) {
        height = AutRoundDiv64(r->size * font_dpi(font), 720000);
    } else {
        LONGLONG himetric = AutRoundDiv64(r->size * 254, 72000);
        height = AutRoundDiv64(himetric * r->logical, r->himetric);
    }
    if (height <= 0)
        height = 1;
    lf.lfHeight = (LONG)-height;
    HFONT hfont = CreateFontIndirectW(&lf);
    if (!hfont)
        return font_hresult(GetLastError());
    *result = hfont;
    return S_OK;
}

static int CALLBACK font_charset_callback(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    DWORD *data = (DWORD *)param;
    const NEWTEXTMETRICEXW *ntm = (const NEWTEXTMETRICEXW *)tm;
    DWORD charset = ntm->ntmTm.tmCharSet;
    if (data[0] != 0xFFFFFFFF && data[1] != charset)
        return TRUE;
    data[0] = charset;
    return data[1] != charset;
}

/* the character set a face has, preferring the asked one. */
static BOOL font_charset_of_face(HDC dc, const WCHAR *face, DWORD preferred, SHORT *charset, BOOL name_priority)
{
    DWORD data[2] = {0xFFFFFFFF, preferred};
    LOGFONTW lf = {};
    lf.lfCharSet = name_priority ? DEFAULT_CHARSET : (BYTE)preferred;
    font_copy_face(lf.lfFaceName, face);
    EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)font_charset_callback, (LPARAM)data, 0);
    if (data[0] == 0xFFFFFFFF)
        return FALSE;
    *charset = (SHORT)data[0];
    return TRUE;
}

/* fills the actual record from what the gdi font turned out to be. */
static void font_actual(FONT *font, HFONT hfont, const TEXTMETRICW *tm, const WCHAR *face)
{
    FONT_RECORD *r = &font->Record;
    font->Hfont = hfont;
    r->flags = (BYTE)((r->flags & ~(FONT_ITALIC | FONT_UNDERLINE | FONT_STRIKE)) | (tm->tmItalic ? FONT_ITALIC : 0) |
                      (tm->tmUnderlined ? FONT_UNDERLINE : 0) | (tm->tmStruckOut ? FONT_STRIKE : 0));
    r->weight = (SHORT)tm->tmWeight;
    r->charset = (SHORT)tm->tmCharSet;
    LONGLONG cell = tm->tmHeight - tm->tmInternalLeading;
    if (r->logical == 1 && r->himetric == 1) {
        r->size = AutRoundDiv64(cell * 720000, font_dpi(font));
    } else {
        LONGLONG step = AutRoundDiv64(cell * 72000, 254);
        step = AutRoundDiv64(step * r->himetric, r->logical);
        r->size = 100 * AutRoundDiv64(step, 100);
    }
    font_copy_face(r->face, face);
}

/* the cached gdi font of the object, made when no object asked for the same one before. */
static HRESULT font_update(FONT *font)
{
    if (font->Cache != -1)
        return S_OK;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&font_lock);
    int found = font_find(&font->Record);
    if (found >= 0) {
        FONT_CACHE *entry = &font_cache[found];
        font->Cache = found;
        entry->refs++;
        font->Hfont = entry->hfont;
        font->Record = entry->actual;
        font->Priority &= (BYTE) ~(FONT_NAME_SET | FONT_CHARSET_SET);
    } else {
        int index = font_new_entry();
        if (index < 0) {
            hr = E_OUTOFMEMORY;
        } else {
            /* a context of its own for the query, so that no earlier font is mistaken for this one. */
            HDC temp = font->Hdc ? nullptr : CreateCompatibleDC(nullptr);
            HDC dc = font->Hdc ? font->Hdc : temp;
            if (font->Priority & (FONT_NAME_SET | FONT_CHARSET_SET)) {
                SHORT charset;
                if (dc && font_charset_of_face(dc, font->Record.face, (DWORD)(WORD)font->Record.charset, &charset,
                                               !(font->Priority & FONT_CHARSET_SET)) &&
                    font->Record.charset != charset)
                    font->Record.charset = charset;
            }
            HFONT hfont = nullptr;
            hr = font_create_matching(font, &hfont);
            if (SUCCEEDED(hr)) {
                TEXTMETRICW tm = {};
                WCHAR face[32] = L"";
                if (dc) {
                    HGDIOBJ previous = SelectObject(dc, hfont);
                    GetTextMetricsW(dc, &tm);
                    GetTextFaceW(dc, 32, face);
                    SelectObject(dc, previous);
                }
                FONT_CACHE *entry = &font_cache[index];
                entry->refs = 1;
                entry->hfont = hfont;
                entry->request = font->Record;
                font_actual(font, hfont, &tm, face);
                entry->metrics = tm;
                entry->actual = font->Record;
                font->Cache = index;
                font->Priority &= (BYTE) ~(FONT_NAME_SET | FONT_CHARSET_SET);
            }
            if (temp)
                DeleteDC(temp);
        }
    }
    ReleaseSRWLockExclusive(&font_lock);
    return hr;
}

/* lets go of the cached font, which is made again on the next use. */
static void font_discard(FONT *font)
{
    AcquireSRWLockExclusive(&font_lock);
    if (font->Cache != -1) {
        if (font_cache[font->Cache].refs)
            font_cache[font->Cache].refs--;
        font->Cache = -1;
    }
    ReleaseSRWLockExclusive(&font_lock);
}

/* ------------------------------------------------------------------ IFont */

static HRESULT font_check_name(const WCHAR *name)
{
    if (!name)
        return CTL_E_INVALIDPROPERTYVALUE;
    return wcslen(name) >= 32 ? CTL_E_INVALIDPROPERTYVALUE : S_OK;
}

static HRESULT STDMETHODCALLTYPE font_query(IFont *This, REFIID riid, void **object)
{
    FONT *font = (FONT *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!riid)
        return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IFont))
        *object = &font->Font;
    else if (IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IFontDisp))
        *object = &font->Disp;
    else if (IsEqualIID(riid, &IID_IPersistStream) || IsEqualIID(riid, &IID_IPersist))
        *object = &font->Persist;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&font->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE font_addref(IFont *This)
{
    return InterlockedIncrement(&((FONT *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE font_release(IFont *This)
{
    FONT *font = (FONT *)This;
    LONG refs = InterlockedDecrement(&font->Refs);
    if (refs)
        return refs;
    AcquireSRWLockExclusive(&font_lock);
    if (font->Cache != -1 && font_cache[font->Cache].refs)
        font_cache[font->Cache].refs--;
    if (--font_objects == 0)
        font_cache_free();
    ReleaseSRWLockExclusive(&font_lock);
    AutFree(font);
    return 0;
}

static HRESULT STDMETHODCALLTYPE font_get_name(IFont *This, BSTR *name)
{
    FONT *font = (FONT *)This;
    if (!name)
        return E_POINTER;
    HRESULT hr = font_update(font);
    if (FAILED(hr)) {
        *name = nullptr;
        return hr;
    }
    /* two names of old fonts are reported as the faces that replaced them. */
    const WCHAR *face = font->Record.face;
    if (!_wcsicmp(face, L"Helv"))
        face = L"MS Sans Serif";
    else if (!_wcsicmp(face, L"Tms Rmn"))
        face = L"MS Serif";
    *name = SysAllocString(face);
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE font_put_name(IFont *This, BSTR name)
{
    FONT *font = (FONT *)This;
    HRESULT hr = font_check_name(name);
    if (FAILED(hr))
        return hr;
    if (_wcsicmp(font->Record.face, name)) {
        font_copy_face(font->Record.face, name);
        font->Priority |= FONT_NAME_SET;
        font_discard(font);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_get_size(IFont *This, CY *size)
{
    FONT *font = (FONT *)This;
    if (!size)
        return E_POINTER;
    HRESULT hr = font_update(font);
    size->int64 = SUCCEEDED(hr) ? font->Record.size : 0;
    return hr;
}

static HRESULT STDMETHODCALLTYPE font_put_size(IFont *This, CY size)
{
    FONT *font = (FONT *)This;
    if (size.int64 <= 0 || size.Hi < 0)
        return CTL_E_INVALIDPROPERTYVALUE;
    if ((ULONGLONG)size.int64 > 0x270FD8F0ull)
        return CTL_E_OVERFLOW;
    if ((LONGLONG)(size.Lo / 10000) * font_dpi(font) >= 0x240000)
        return CTL_E_OVERFLOW;
    if ((ULONG)font->Record.size != size.Lo) {
        font->Record.size = size.int64;
        font_discard(font);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_get_bold(IFont *This, BOOL *bold)
{
    FONT *font = (FONT *)This;
    if (!bold)
        return E_POINTER;
    HRESULT hr = font_update(font);
    *bold = SUCCEEDED(hr) && font->Record.weight > 550;
    return hr;
}

static HRESULT STDMETHODCALLTYPE font_put_bold(IFont *This, BOOL bold)
{
    FONT *font = (FONT *)This;
    SHORT weight = bold ? 700 : 400;
    if (font->Record.weight != weight) {
        font->Record.weight = weight;
        font_discard(font);
    }
    return S_OK;
}

static HRESULT font_get_flag(IFont *This, BOOL *value, BYTE flag)
{
    FONT *font = (FONT *)This;
    if (!value)
        return E_POINTER;
    HRESULT hr = font_update(font);
    *value = SUCCEEDED(hr) && (font->Record.flags & flag);
    return hr;
}

static HRESULT font_put_flag(IFont *This, BOOL value, BYTE flag)
{
    FONT *font = (FONT *)This;
    if (((font->Record.flags & flag) != 0) != (value != 0)) {
        font->Record.flags ^= flag;
        font_discard(font);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_get_italic(IFont *This, BOOL *value)
{
    return font_get_flag(This, value, FONT_ITALIC);
}

static HRESULT STDMETHODCALLTYPE font_put_italic(IFont *This, BOOL value)
{
    return font_put_flag(This, value, FONT_ITALIC);
}

static HRESULT STDMETHODCALLTYPE font_get_underline(IFont *This, BOOL *value)
{
    return font_get_flag(This, value, FONT_UNDERLINE);
}

static HRESULT STDMETHODCALLTYPE font_put_underline(IFont *This, BOOL value)
{
    return font_put_flag(This, value, FONT_UNDERLINE);
}

static HRESULT STDMETHODCALLTYPE font_get_strikethrough(IFont *This, BOOL *value)
{
    return font_get_flag(This, value, FONT_STRIKE);
}

static HRESULT STDMETHODCALLTYPE font_put_strikethrough(IFont *This, BOOL value)
{
    return font_put_flag(This, value, FONT_STRIKE);
}

static HRESULT STDMETHODCALLTYPE font_get_weight(IFont *This, SHORT *weight)
{
    FONT *font = (FONT *)This;
    if (!weight)
        return E_POINTER;
    HRESULT hr = font_update(font);
    *weight = SUCCEEDED(hr) ? font->Record.weight : 0;
    return hr;
}

static HRESULT STDMETHODCALLTYPE font_put_weight(IFont *This, SHORT weight)
{
    FONT *font = (FONT *)This;
    if ((USHORT)weight > 1000)
        return CTL_E_INVALIDPROPERTYVALUE;
    if (font->Record.weight != weight) {
        font->Record.weight = weight;
        font_discard(font);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_get_charset(IFont *This, SHORT *charset)
{
    FONT *font = (FONT *)This;
    if (!charset)
        return E_POINTER;
    HRESULT hr = font_update(font);
    *charset = SUCCEEDED(hr) ? font->Record.charset : 0;
    return hr;
}

static HRESULT STDMETHODCALLTYPE font_put_charset(IFont *This, SHORT charset)
{
    FONT *font = (FONT *)This;
    if ((USHORT)charset > 0xFF)
        return CTL_E_INVALIDPROPERTYVALUE;
    if (font->Record.charset != charset) {
        font->Priority |= FONT_CHARSET_SET;
        font->Record.charset = charset;
        font_discard(font);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_get_hfont(IFont *This, HFONT *hfont)
{
    FONT *font = (FONT *)This;
    if (!hfont)
        return E_POINTER;
    HRESULT hr = font_update(font);
    *hfont = SUCCEEDED(hr) ? font->Hfont : nullptr;
    return hr;
}

static FONT *font_new(void);

static HRESULT STDMETHODCALLTYPE font_clone(IFont *This, IFont **result)
{
    FONT *font = (FONT *)This;
    if (!result)
        return E_POINTER;
    HRESULT hr = font_update(font);
    FONT *copy = SUCCEEDED(hr) ? font_new() : nullptr;
    if (!copy) {
        *result = nullptr;
        return SUCCEEDED(hr) ? E_OUTOFMEMORY : hr;
    }
    AcquireSRWLockExclusive(&font_lock);
    copy->Hfont = font->Hfont;
    copy->Record = font->Record;
    copy->Cache = font->Cache;
    if (copy->Cache != -1)
        font_cache[copy->Cache].refs++;
    ReleaseSRWLockExclusive(&font_lock);
    *result = &copy->Font;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_is_equal(IFont *This, IFont *other)
{
    if (!other)
        return E_POINTER;
    /* an IPersist starts with the same methods as the IPersistStream that is asked for in its place. */
    IPersistStream *persist = nullptr;
    CLSID clsid = {};
    HFONT mine = nullptr, theirs = nullptr;
    HRESULT hr = other->lpVtbl->QueryInterface(other, &IID_IPersist, (void **)&persist);
    if (SUCCEEDED(hr)) {
        hr = persist->lpVtbl->GetClassID(persist, &clsid);
        persist->lpVtbl->Release(persist);
    }
    if (FAILED(hr) || (!IsEqualCLSID(&clsid, &CLSID_StdFont) && !IsEqualCLSID(&clsid, &CLSID_OldFont)))
        return S_FALSE;
    if (FAILED(This->lpVtbl->get_hFont(This, &mine)) || FAILED(other->lpVtbl->get_hFont(other, &theirs)))
        return S_FALSE;
    return mine == theirs ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE font_set_ratio(IFont *This, LONG logical, LONG himetric)
{
    FONT *font = (FONT *)This;
    if (!logical || !himetric)
        return E_FAIL;
    if (logical != font->Record.logical || himetric != font->Record.himetric) {
        font->Record.logical = logical;
        font->Record.himetric = himetric;
        font_discard(font);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_query_text_metrics(IFont *This, TEXTMETRICW *metrics)
{
    FONT *font = (FONT *)This;
    if (!metrics)
        return E_POINTER;
    HRESULT hr = font_update(font);
    if (FAILED(hr))
        return hr;
    AcquireSRWLockExclusive(&font_lock);
    *metrics = font_cache[font->Cache].metrics;
    ReleaseSRWLockExclusive(&font_lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_addref_hfont(IFont *This, HFONT hfont)
{
    if (!hfont)
        return E_INVALIDARG;
    HRESULT hr = S_FALSE;
    AcquireSRWLockExclusive(&font_lock);
    for (int i = 0; i < font_cache_count; i++) {
        if (font_cache[i].hfont == hfont) {
            font_cache[i].refs++;
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockExclusive(&font_lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE font_release_hfont(IFont *This, HFONT hfont)
{
    if (!hfont)
        return E_INVALIDARG;
    HRESULT hr = S_FALSE;
    AcquireSRWLockExclusive(&font_lock);
    for (int i = 0; i < font_cache_count; i++) {
        if (font_cache[i].hfont == hfont) {
            if (font_cache[i].refs) {
                font_cache[i].refs--;
                hr = S_OK;
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&font_lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE font_set_hdc(IFont *This, HDC hdc)
{
    ((FONT *)This)->Hdc = hdc;
    return S_OK;
}

static const IFontVtbl font_vtbl = {
    font_query,
    font_addref,
    font_release,
    font_get_name,
    font_put_name,
    font_get_size,
    font_put_size,
    font_get_bold,
    font_put_bold,
    font_get_italic,
    font_put_italic,
    font_get_underline,
    font_put_underline,
    font_get_strikethrough,
    font_put_strikethrough,
    font_get_weight,
    font_put_weight,
    font_get_charset,
    font_put_charset,
    font_get_hfont,
    font_clone,
    font_is_equal,
    font_set_ratio,
    font_query_text_metrics,
    font_addref_hfont,
    font_release_hfont,
    font_set_hdc,
};

/* ------------------------------------------------------------------ IPersistStream */

static HRESULT STDMETHODCALLTYPE font_persist_query(IPersistStream *This, REFIID riid, void **object)
{
    FONT *font = FONT_FROM_PERSIST(This);
    return font_query(&font->Font, riid, object);
}

static ULONG STDMETHODCALLTYPE font_persist_addref(IPersistStream *This)
{
    return InterlockedIncrement(&FONT_FROM_PERSIST(This)->Refs);
}

static ULONG STDMETHODCALLTYPE font_persist_release(IPersistStream *This)
{
    return font_release(&FONT_FROM_PERSIST(This)->Font);
}

static HRESULT STDMETHODCALLTYPE font_get_class_id(IPersistStream *This, CLSID *clsid)
{
    if (!clsid)
        return E_POINTER;
    *clsid = CLSID_StdFont;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_is_dirty(IPersistStream *This)
{
    return S_OK;
}

static HRESULT font_read(IStream *stream, void *buffer, ULONG size)
{
    ULONG read = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, buffer, size, &read);
    if (FAILED(hr))
        return hr;
    return read == size ? S_OK : E_FAIL;
}

/*
 * the stream holds a version byte of 0 or 1, in version 1 the character set, the flags, the weight, the size, the
 * length of the face and the face in the ansi code page.
 */
static HRESULT STDMETHODCALLTYPE font_load(IPersistStream *This, IStream *stream)
{
    FONT *font = FONT_FROM_PERSIST(This);
    if (!stream)
        return E_POINTER;
    if (font->Cache != -1)
        font_discard(font);
    BYTE version = 0;
    HRESULT hr = font_read(stream, &version, 1);
    if (FAILED(hr))
        return hr;
    if (version > 1)
        return E_FAIL;
    SHORT charset = font->Record.charset;
    if (version) {
        hr = font_read(stream, &charset, 2);
        if (FAILED(hr))
            return hr;
    }
    BYTE data[8];
    hr = font_read(stream, data, 8);
    if (FAILED(hr))
        return hr;
    BYTE flags = data[0];
    SHORT weight = (SHORT)(data[1] | data[2] << 8);
    ULONG height = data[3] | data[4] << 8 | data[5] << 16 | (ULONG)data[6] << 24;
    BYTE length = data[7];
    if (length >= 32)
        return E_FAIL;
    char ansi[32];
    hr = font_read(stream, ansi, length);
    if (FAILED(hr))
        return hr;
    ansi[length] = 0;
    WCHAR face[32];
    if (!MultiByteToWideChar(CP_ACP, 0, ansi, length + 1, face, 32))
        return E_OUTOFMEMORY;
    font_copy_face(font->Record.face, face);
    font->Record.flags = flags;
    font->Record.weight = weight;
    font->Record.size = height;
    font->Record.charset = charset;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_save(IPersistStream *This, IStream *stream, BOOL clear_dirty)
{
    FONT *font = FONT_FROM_PERSIST(This);
    if (!stream)
        return E_POINTER;
    char ansi[33];
    WideCharToMultiByte(CP_ACP, 0, font->Record.face, -1, ansi, 33, nullptr, nullptr);
    ansi[32] = 0;
    BYTE length = (BYTE)lstrlenA(ansi);
    BYTE data[11];
    ULONG height = (ULONG)font->Record.size;
    data[0] = 1;
    data[1] = (BYTE)font->Record.charset;
    data[2] = (BYTE)((USHORT)font->Record.charset >> 8);
    data[3] = font->Record.flags;
    data[4] = (BYTE)font->Record.weight;
    data[5] = (BYTE)((USHORT)font->Record.weight >> 8);
    data[6] = (BYTE)height;
    data[7] = (BYTE)(height >> 8);
    data[8] = (BYTE)(height >> 16);
    data[9] = (BYTE)(height >> 24);
    data[10] = length;
    HRESULT hr = stream->lpVtbl->Write(stream, data, sizeof(data), nullptr);
    if (FAILED(hr))
        return hr;
    return stream->lpVtbl->Write(stream, ansi, length, nullptr);
}

static HRESULT STDMETHODCALLTYPE font_get_size_max(IPersistStream *This, ULARGE_INTEGER *size)
{
    if (!size)
        return E_POINTER;
    size->QuadPart = 43;
    return S_OK;
}

static const IPersistStreamVtbl font_persist_vtbl = {
    font_persist_query,  font_persist_addref, font_persist_release, font_get_class_id,
    font_is_dirty,       font_load,           font_save,            font_get_size_max,
};

/* ------------------------------------------------------------------ IFontDisp */

static HRESULT STDMETHODCALLTYPE font_disp_query(IDispatch *This, REFIID riid, void **object)
{
    return font_query(&FONT_FROM_DISP(This)->Font, riid, object);
}

static ULONG STDMETHODCALLTYPE font_disp_addref(IDispatch *This)
{
    return InterlockedIncrement(&FONT_FROM_DISP(This)->Refs);
}

static ULONG STDMETHODCALLTYPE font_disp_release(IDispatch *This)
{
    return font_release(&FONT_FROM_DISP(This)->Font);
}

static const struct {
    const WCHAR *name;
    DISPID id;
} font_names[] = {
    {L"Name", DISPID_FONT_NAME},         {L"Size", DISPID_FONT_SIZE},     {L"Bold", DISPID_FONT_BOLD},
    {L"Italic", DISPID_FONT_ITALIC},     {L"Underline", DISPID_FONT_UNDER}, {L"Strikethrough", DISPID_FONT_STRIKE},
    {L"Weight", DISPID_FONT_WEIGHT},     {L"Charset", DISPID_FONT_CHARSET},
};

static HRESULT STDMETHODCALLTYPE font_get_type_info_count(IDispatch *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

/* the type information comes from the standard type library, which has to be registered. */
HRESULT AutStdOleTypeInfo(UINT index, REFGUID id, ITypeInfo **info)
{
    if (index != 0)
        return DISP_E_BADINDEX;
    void *library = nullptr;
    HRESULT hr = LoadRegTypeLib(&IID_StdOle, 2, 0, 0, &library);
    if (FAILED(hr))
        return hr;
    void **table = *(void ***)library;
    hr = ((HRESULT(STDMETHODCALLTYPE *)(void *, REFGUID, ITypeInfo **))table[6])(library, id, info);
    ((ULONG(STDMETHODCALLTYPE *)(void *))table[2])(library);
    return hr;
}

static HRESULT STDMETHODCALLTYPE font_get_type_info(IDispatch *This, UINT index, LCID lcid, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    return AutStdOleTypeInfo(index, &IID_IFontDisp, info);
}

HRESULT AutNamesToIds(const WCHAR *const *table, const DISPID *ids, int count, REFIID riid, LPOLESTR *names, UINT cnames,
                      DISPID *result)
{
    if (!IsEqualIID(riid, &GUID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    if (!names || !result || !cnames)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    for (UINT i = 0; i < cnames; i++) {
        result[i] = DISPID_UNKNOWN;
        for (int k = 0; i == 0 && k < count; k++) {
            if (names[0] && !_wcsicmp(names[0], table[k]))
                result[0] = ids[k];
        }
        if (result[i] == DISPID_UNKNOWN)
            hr = DISP_E_UNKNOWNNAME;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE font_get_ids_of_names(IDispatch *This, REFIID riid, LPOLESTR *names, UINT cnames,
                                                       LCID lcid, DISPID *ids)
{
    const WCHAR *table[ARRAYSIZE(font_names)];
    DISPID dispids[ARRAYSIZE(font_names)];
    for (int i = 0; i < (int)ARRAYSIZE(font_names); i++) {
        table[i] = font_names[i].name;
        dispids[i] = font_names[i].id;
    }
    return AutNamesToIds(table, dispids, ARRAYSIZE(font_names), riid, names, cnames, ids);
}

/* ------------------------------------------------------------------ arguments */

static HRESULT font_exception(HRESULT hr, EXCEPINFO *excepinfo)
{
    if (((ULONG)hr & 0x1FFF0000) == 0xA0000 && excepinfo) {
        memset(excepinfo, 0, sizeof(*excepinfo));
        excepinfo->scode = hr;
        return DISP_E_EXCEPTION;
    }
    return hr;
}

static BOOL variant_text_number(BSTR text, double *value)
{
    if (!text)
        return FALSE;
    const WCHAR *p = text;
    while (*p == ' ')
        p++;
    BOOL negative = FALSE, digits = FALSE;
    if (*p == '-' || *p == '+')
        negative = *p++ == '-';
    double result = 0, scale = 0.1;
    for (; *p >= '0' && *p <= '9'; p++, digits = TRUE)
        result = result * 10 + (*p - '0');
    if (*p == '.') {
        for (p++; *p >= '0' && *p <= '9'; p++, digits = TRUE, scale /= 10)
            result += (*p - '0') * scale * 10;
    }
    while (*p == ' ')
        p++;
    if (!digits || *p)
        return FALSE;
    *value = negative ? -result : result;
    return TRUE;
}

/* the number a variant holds, or its text read as one. */
static HRESULT variant_number(const VARIANT *source, double *value)
{
    VARTYPE vt = source->vt;
    BOOL by_ref = (vt & VT_BYREF) != 0;
    const void *p = by_ref ? source->byref : &source->llVal;
    vt &= (VARTYPE) ~VT_BYREF;
    if (by_ref && !p)
        return DISP_E_TYPEMISMATCH;
    switch (vt) {
    case VT_EMPTY: *value = 0; return S_OK;
    case VT_I1: *value = *(const signed char *)p; return S_OK;
    case VT_UI1: *value = *(const BYTE *)p; return S_OK;
    case VT_I2:
    case VT_BOOL: *value = *(const SHORT *)p; return S_OK;
    case VT_UI2: *value = *(const USHORT *)p; return S_OK;
    case VT_I4:
    case VT_INT: *value = *(const LONG *)p; return S_OK;
    case VT_UI4:
    case VT_UINT: *value = *(const ULONG *)p; return S_OK;
    case VT_I8: *value = (double)*(const LONGLONG *)p; return S_OK;
    case VT_UI8: *value = (double)*(const ULONGLONG *)p; return S_OK;
    case VT_R4: *value = *(const float *)p; return S_OK;
    case VT_R8:
    case VT_DATE: *value = *(const double *)p; return S_OK;
    case VT_CY: *value = (double)((const CY *)p)->int64 / 10000.0; return S_OK;
    case VT_BSTR: return variant_text_number(*(BSTR const *)p, value) ? S_OK : DISP_E_TYPEMISMATCH;
    default: return DISP_E_TYPEMISMATCH;
    }
}

/* rounds half to even, the way the conversions of automation do. */
static LONGLONG variant_round(double value)
{
    LONGLONG whole = (LONGLONG)value;
    double fraction = value - (double)whole;
    if (fraction > 0.5 || (fraction == 0.5 && (whole & 1)))
        whole++;
    else if (fraction < -0.5 || (fraction == -0.5 && (whole & 1)))
        whole--;
    return whole;
}

static BSTR variant_bstr(const VARIANT *source)
{
    if ((source->vt & VT_TYPEMASK) != VT_BSTR)
        return nullptr;
    return (source->vt & VT_BYREF) ? (source->pbstrVal ? *source->pbstrVal : nullptr) : source->bstrVal;
}

/* converts the argument of a property put to what the property takes. */
static HRESULT font_argument(const VARIANT *source, VARTYPE vt, LONGLONG *value, BSTR *text)
{
    if (vt == VT_BSTR) {
        *text = variant_bstr(source);
        return source->vt == VT_BSTR || source->vt == (VT_BSTR | VT_BYREF) ? S_OK : DISP_E_TYPEMISMATCH;
    }
    if (vt == VT_BOOL && (source->vt & VT_TYPEMASK) == VT_BSTR) {
        BSTR s = variant_bstr(source);
        if (s && !_wcsicmp(s, L"true")) {
            *value = -1;
            return S_OK;
        }
        if (s && !_wcsicmp(s, L"false")) {
            *value = 0;
            return S_OK;
        }
    }
    double number;
    HRESULT hr = variant_number(source, &number);
    if (FAILED(hr))
        return hr;
    if (vt == VT_BOOL) {
        *value = number != 0 ? -1 : 0;
    } else if (vt == VT_CY) {
        if (number >= 922337203685477.0 || number <= -922337203685477.0)
            return DISP_E_OVERFLOW;
        *value = variant_round(number * 10000.0);
    } else {
        if (number >= 32767.5 || number <= -32768.5)
            return DISP_E_OVERFLOW;
        *value = variant_round(number);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE font_invoke(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                             DISPPARAMS *params, VARIANT *result, EXCEPINFO *excepinfo,
                                             UINT *arg_error)
{
    IFont *f = &FONT_FROM_DISP(This)->Font;
    if (!IsEqualIID(riid, &GUID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    if (!(flags & DISPATCH_PROPERTYGET)) {
        if (!(flags & DISPATCH_PROPERTYPUT))
            return DISP_E_MEMBERNOTFOUND;
        if (!params)
            return DISP_E_PARAMNOTOPTIONAL;
        if (params->cArgs != 1)
            return DISP_E_BADPARAMCOUNT;
        if (!params->rgvarg)
            return DISP_E_PARAMNOTOPTIONAL;
        static const VARTYPE types[] = {[DISPID_FONT_NAME] = VT_BSTR, [DISPID_FONT_SIZE] = VT_CY,
                                        [DISPID_FONT_BOLD] = VT_BOOL, [DISPID_FONT_ITALIC] = VT_BOOL,
                                        [DISPID_FONT_UNDER] = VT_BOOL, [DISPID_FONT_STRIKE] = VT_BOOL,
                                        [DISPID_FONT_WEIGHT] = VT_I2, [DISPID_FONT_CHARSET] = VT_I2};
        if (id < 0 || id >= (DISPID)ARRAYSIZE(types) || id == 1)
            return DISP_E_MEMBERNOTFOUND;
        LONGLONG value = 0;
        BSTR text = nullptr;
        HRESULT hr = font_argument(params->rgvarg, types[id], &value, &text);
        if (FAILED(hr)) {
            /* an overflow of the number is the overflow of a property. */
            if (hr == DISP_E_OVERFLOW)
                hr = CTL_E_OVERFLOW;
            return font_exception(hr, excepinfo);
        }
        switch (id) {
        case DISPID_FONT_NAME: hr = f->lpVtbl->put_Name(f, text); break;
        case DISPID_FONT_SIZE: hr = f->lpVtbl->put_Size(f, (CY){.int64 = value}); break;
        case DISPID_FONT_BOLD: hr = f->lpVtbl->put_Bold(f, (BOOL)(value != 0)); break;
        case DISPID_FONT_ITALIC: hr = f->lpVtbl->put_Italic(f, (BOOL)(value != 0)); break;
        case DISPID_FONT_UNDER: hr = f->lpVtbl->put_Underline(f, (BOOL)(value != 0)); break;
        case DISPID_FONT_STRIKE: hr = f->lpVtbl->put_Strikethrough(f, (BOOL)(value != 0)); break;
        case DISPID_FONT_WEIGHT: hr = f->lpVtbl->put_Weight(f, (SHORT)value); break;
        default: hr = f->lpVtbl->put_Charset(f, (SHORT)value); break;
        }
        return font_exception(hr, excepinfo);
    }
    if (!result)
        return DISP_E_PARAMNOTOPTIONAL;
    HRESULT hr;
    BOOL flag = FALSE;
    switch (id) {
    case DISPID_FONT_NAME:
        result->vt = VT_BSTR;
        hr = f->lpVtbl->get_Name(f, &result->bstrVal);
        break;
    case DISPID_FONT_SIZE:
        result->vt = VT_CY;
        hr = f->lpVtbl->get_Size(f, &result->cyVal);
        break;
    case DISPID_FONT_BOLD:
        hr = f->lpVtbl->get_Bold(f, &flag);
        break;
    case DISPID_FONT_ITALIC:
        hr = f->lpVtbl->get_Italic(f, &flag);
        break;
    case DISPID_FONT_UNDER:
        hr = f->lpVtbl->get_Underline(f, &flag);
        break;
    case DISPID_FONT_STRIKE:
        hr = f->lpVtbl->get_Strikethrough(f, &flag);
        break;
    case DISPID_FONT_WEIGHT:
        result->vt = VT_I2;
        hr = f->lpVtbl->get_Weight(f, &result->iVal);
        break;
    case DISPID_FONT_CHARSET:
        result->vt = VT_I2;
        hr = f->lpVtbl->get_Charset(f, &result->iVal);
        break;
    default:
        return DISP_E_MEMBERNOTFOUND;
    }
    if (id >= DISPID_FONT_BOLD && id <= DISPID_FONT_STRIKE) {
        result->vt = VT_BOOL;
        result->boolVal = flag ? VARIANT_TRUE : VARIANT_FALSE;
    }
    if (FAILED(hr)) {
        result->vt = VT_EMPTY;
        return font_exception(hr, excepinfo);
    }
    return hr;
}

static const IDispatchVtbl font_disp_vtbl = {
    font_disp_query,           font_disp_addref,    font_disp_release, font_get_type_info_count,
    font_get_type_info,        font_get_ids_of_names, font_invoke,
};

/* ------------------------------------------------------------------ creation */

static FONT *font_new(void)
{
    FONT *font = AutAllocZero(sizeof(FONT));
    if (!font)
        return nullptr;
    font->Font.lpVtbl = &font_vtbl;
    font->Disp.lpVtbl = &font_disp_vtbl;
    font->Persist.lpVtbl = &font_persist_vtbl;
    font->Refs = 1;
    font->Cache = -1;
    font->Record.size = 80000;
    font->Record.logical = 1;
    font->Record.himetric = 1;
    font->Record.charset = 1;
    AcquireSRWLockExclusive(&font_lock);
    font_objects++;
    ReleaseSRWLockExclusive(&font_lock);
    return font;
}

WINOLEAUTAPI HRESULT WINAPI OleCreateFontIndirect(LPFONTDESC desc, REFIID riid, LPVOID *object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    FONT *font = font_new();
    if (!font)
        return E_OUTOFMEMORY;
    const FONTDESC *d = desc ? desc : &default_desc;
    font->Record.weight = d->sWeight;
    font->Record.charset = d->sCharset;
    /* the flags take the lowest bit of the booleans. */
    font->Record.flags = (BYTE)(((d->fItalic << 1) & FONT_ITALIC) | ((d->fUnderline << 2) & FONT_UNDERLINE) |
                                ((d->fStrikethrough << 3) & FONT_STRIKE));
    font->Record.size = d->cySize.int64;
    HRESULT hr = font_check_name(d->lpstrName);
    if (SUCCEEDED(hr)) {
        font_copy_face(font->Record.face, d->lpstrName);
        hr = font_query(&font->Font, riid, object);
    }
    font_release(&font->Font);
    return hr;
}
