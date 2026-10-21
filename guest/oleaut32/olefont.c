/*
 * the standard font: OleCreateFontIndirect makes an object with IFont, its dispatch form and IPersistStream. What a
 * font is lives in a record. The system fonts made from records are shared by all objects of the process through a
 * small cache, so two objects that describe the same font hold the same HFONT.
 */
#include "olestd.h"

#define FACE_CHARS 32
#define STYLE_ITALIC 0x02
#define STYLE_UNDERLINE 0x04
#define STYLE_STRIKEOUT 0x08

/* the sizes a font may have, as a CY value and as the number of pixels it is multiplied by its resolution to. */
#define SIZE_LIMIT 0x270FD8F0
#define SIZE_PIXEL_LIMIT 0x240000

#define CACHE_GROWTH 8

#define PRIORITY_NAME 0x01
#define PRIORITY_CHARSET 0x02

#define STREAM_VERSION 1
#define STREAM_HEADER 11

typedef struct FONT_SPEC {
    CY Size;
    LONG Logical, Himetric;
    WORD Charset;
    SHORT Weight;
    BYTE Style;
    WCHAR Face[FACE_CHARS];
} FONT_SPEC;

/* a font the system made, with the record asked for and the one that came out. */
typedef struct FONT_SLOT {
    FONT_SPEC Asked, Got;
    HFONT Handle;
    LONG Uses;
    TEXTMETRICW Metrics;
} FONT_SLOT;

typedef struct FONT_OBJECT {
    const IFontVtbl *Font;
    const IDispatchVtbl *Dispatch;
    const IPersistStreamVtbl *Persist;
    LONG Refs;
    LONG Slot;
    BYTE Priority;
    HDC Dc;
    FONT_SPEC Spec;
} FONT_OBJECT;

static SRWLOCK g_cache_lock = SRWLOCK_INIT;
static FONT_SLOT *g_slots;
static ULONG g_slot_count;
static LONG g_screen_dpi;

static FONT_OBJECT *object_of_font(IFont *p)
{
    return (FONT_OBJECT *)p;
}

static FONT_OBJECT *object_of_dispatch(IDispatch *p)
{
    return (FONT_OBJECT *)((char *)p - offsetof(FONT_OBJECT, Dispatch));
}

static FONT_OBJECT *object_of_persist(IPersistStream *p)
{
    return (FONT_OBJECT *)((char *)p - offsetof(FONT_OBJECT, Persist));
}

/* ------------------------------------------------------------------ records */

static void spec_set_face(FONT_SPEC *spec, const WCHAR *name)
{
    SIZE_T length = wcslen(name);
    if (length >= FACE_CHARS)
        length = FACE_CHARS - 1;
    memcpy(spec->Face, name, length * sizeof(WCHAR));
    spec->Face[length] = 0;
}

static BOOL spec_equal(const FONT_SPEC *a, const FONT_SPEC *b)
{
    return a->Size.int64 == b->Size.int64 && a->Style == b->Style && a->Weight == b->Weight &&
           a->Charset == b->Charset && a->Logical == b->Logical && a->Himetric == b->Himetric &&
           !_wcsicmp(a->Face, b->Face);
}

static HRESULT check_face(const WCHAR *name)
{
    return !name || wcslen(name) >= FACE_CHARS ? CTL_E_INVALIDPROPERTYVALUE : S_OK;
}

static HRESULT last_error_result(void)
{
    DWORD error = GetLastError();
    return error ? (HRESULT)((error & 0xFFFF) | 0x80070000) : E_FAIL;
}

/* ------------------------------------------------------------------ the cache of system fonts */

static LONG slot_find(const FONT_SPEC *spec)
{
    for (ULONG i = 0; i < g_slot_count; i++) {
        FONT_SLOT *slot = &g_slots[i];
        if (slot->Handle && (spec_equal(&slot->Asked, spec) || spec_equal(&slot->Got, spec)))
            return (LONG)i;
    }
    return -1;
}

static LONG slot_find_handle(HFONT handle)
{
    for (ULONG i = 0; i < g_slot_count; i++) {
        if (g_slots[i].Handle == handle)
            return (LONG)i;
    }
    return -1;
}

/* an empty slot, taken from the ones nobody uses or from a bigger table. */
static LONG slot_claim(void)
{
    for (ULONG i = 0; i < g_slot_count; i++) {
        if (g_slots[i].Uses == 0) {
            if (g_slots[i].Handle)
                DeleteObject(g_slots[i].Handle);
            memset(&g_slots[i], 0, sizeof(g_slots[i]));
            return (LONG)i;
        }
    }
    ULONG count = g_slot_count + CACHE_GROWTH;
    FONT_SLOT *table = g_slots ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_slots, count * sizeof(FONT_SLOT))
                               : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(FONT_SLOT));
    if (!table)
        return -1;
    LONG first = (LONG)g_slot_count;
    g_slots = table;
    g_slot_count = count;
    return first;
}

/* ------------------------------------------------------------------ resolving a record to a system font */

static LONG screen_dpi(void)
{
    LONG dpi = g_screen_dpi;
    if (!dpi) {
        HDC screen = GetDC(nullptr);
        if (screen) {
            dpi = GetDeviceCaps(screen, LOGPIXELSY);
            ReleaseDC(nullptr, screen);
        }
        g_screen_dpi = dpi;
    }
    return dpi;
}

/* the vertical resolution of the context the object measures with, 96 when nobody can say. */
static LONG object_dpi(const FONT_OBJECT *object)
{
    LONG dpi = object->Dc ? GetDeviceCaps(object->Dc, LOGPIXELSY) : screen_dpi();
    return dpi > 0 ? dpi : 96;
}

typedef struct CHARSET_SEARCH {
    int Preferred;
    int Found;
} CHARSET_SEARCH;

static int CALLBACK note_charset(const LOGFONTW *face, const TEXTMETRICW *metrics, DWORD type, LPARAM param)
{
    CHARSET_SEARCH *search = (CHARSET_SEARCH *)param;
    if (search->Found < 0)
        search->Found = face->lfCharSet;
    if (face->lfCharSet == search->Preferred) {
        search->Found = face->lfCharSet;
        return 0;
    }
    return 1;
}

/* the character set the face is available in, preferring the one the record has. -1 when there is none. */
static int find_charset(HDC dc, const FONT_SPEC *spec, BOOL charset_leads)
{
    LOGFONTW query = {};
    query.lfCharSet = charset_leads ? (BYTE)spec->Charset : DEFAULT_CHARSET;
    memcpy(query.lfFaceName, spec->Face, sizeof(spec->Face));
    CHARSET_SEARCH search = {(int)spec->Charset, -1};
    EnumFontFamiliesExW(dc, &query, note_charset, (LPARAM)&search, 0);
    return search.Found;
}

static int pixel_height(const FONT_OBJECT *object, const FONT_SPEC *spec)
{
    LONGLONG height;
    if (spec->Logical == 1 && spec->Himetric == 1) {
        height = OleStdRoundDiv(spec->Size.int64 * object_dpi(object), 720000);
    } else {
        LONGLONG himetric = OleStdRoundDiv(spec->Size.int64 * 254, 72000);
        height = OleStdRoundDiv(himetric * spec->Logical, spec->Himetric);
    }
    return height < 1 ? 1 : height > 0x7FFFFFFF ? 0x7FFFFFFF : (int)height;
}

/* the record of the font the system really gave, from its metrics. */
static void spec_from_system(const FONT_OBJECT *object, const FONT_SPEC *asked, const TEXTMETRICW *metrics,
                             const WCHAR *face, FONT_SPEC *got)
{
    *got = *asked;
    got->Style = 0;
    if (metrics->tmItalic)
        got->Style |= STYLE_ITALIC;
    if (metrics->tmUnderlined)
        got->Style |= STYLE_UNDERLINE;
    if (metrics->tmStruckOut)
        got->Style |= STYLE_STRIKEOUT;
    got->Weight = (SHORT)metrics->tmWeight;
    got->Charset = metrics->tmCharSet;
    spec_set_face(got, face);
    LONGLONG cell = metrics->tmHeight - metrics->tmInternalLeading;
    if (asked->Logical == 1 && asked->Himetric == 1) {
        got->Size.int64 = OleStdRoundDiv(cell * 720000, object_dpi(object));
    } else {
        LONGLONG step = OleStdRoundDiv(cell * 72000, 254);
        step = OleStdRoundDiv(step * asked->Himetric, asked->Logical);
        got->Size.int64 = 100 * OleStdRoundDiv(step, 100);
    }
}

/* makes the object hold a system font for its record, from the cache or new. The cache lock is held. */
static HRESULT resolve_locked(FONT_OBJECT *object)
{
    LONG hit = slot_find(&object->Spec);
    if (hit >= 0) {
        g_slots[hit].Uses++;
        object->Spec = g_slots[hit].Got;
        object->Priority = 0;
        object->Slot = hit;
        return S_OK;
    }
    LONG index = slot_claim();
    if (index < 0)
        return E_OUTOFMEMORY;

    HDC dc = object->Dc ? object->Dc : GetDC(nullptr);
    if (!dc)
        return E_OUTOFMEMORY;
    FONT_SPEC asked = object->Spec;
    if (object->Priority) {
        int charset = find_charset(dc, &asked, (object->Priority & PRIORITY_CHARSET) != 0);
        if (charset >= 0 && charset != (int)asked.Charset)
            asked.Charset = (WORD)charset;
    }

    LOGFONTW logical = {};
    logical.lfHeight = -pixel_height(object, &asked);
    logical.lfWeight = (LONG)asked.Weight;
    logical.lfItalic = (asked.Style & STYLE_ITALIC) != 0;
    logical.lfUnderline = (asked.Style & STYLE_UNDERLINE) != 0;
    logical.lfStrikeOut = (asked.Style & STYLE_STRIKEOUT) != 0;
    logical.lfCharSet = (BYTE)asked.Charset;
    memcpy(logical.lfFaceName, asked.Face, sizeof(asked.Face));
    HFONT font = CreateFontIndirectW(&logical);
    HRESULT hr = S_OK;
    if (!font) {
        hr = last_error_result();
    } else {
        TEXTMETRICW metrics = {};
        WCHAR face[FACE_CHARS] = {};
        HGDIOBJ previous = SelectObject(dc, font);
        GetTextMetricsW(dc, &metrics);
        GetTextFaceW(dc, FACE_CHARS - 1, face);
        SelectObject(dc, previous);

        FONT_SLOT *slot = &g_slots[index];
        slot->Asked = asked;
        slot->Uses = 1;
        slot->Handle = font;
        spec_from_system(object, &asked, &metrics, face, &slot->Got);
        slot->Metrics = metrics;
        object->Spec = slot->Got;
        object->Slot = index;
        object->Priority = 0;
    }
    if (!object->Dc)
        ReleaseDC(nullptr, dc);
    return hr;
}

static HRESULT font_ready(FONT_OBJECT *object)
{
    if (object->Slot >= 0)
        return S_OK;
    AcquireSRWLockExclusive(&g_cache_lock);
    HRESULT hr = object->Slot >= 0 ? S_OK : resolve_locked(object);
    ReleaseSRWLockExclusive(&g_cache_lock);
    return hr;
}

/* the object lets go of its system font, which the next question asks for again. */
static void font_forget(FONT_OBJECT *object)
{
    if (object->Slot < 0)
        return;
    AcquireSRWLockExclusive(&g_cache_lock);
    if (g_slots[object->Slot].Uses > 0)
        g_slots[object->Slot].Uses--;
    ReleaseSRWLockExclusive(&g_cache_lock);
    object->Slot = -1;
}

/* ------------------------------------------------------------------ IFont */

static HRESULT font_query(FONT_OBJECT *object, REFIID iid, void **result);

static ULONG font_add_ref(FONT_OBJECT *object)
{
    return InterlockedIncrement(&object->Refs);
}

static ULONG font_release(FONT_OBJECT *object)
{
    LONG refs = InterlockedDecrement(&object->Refs);
    if (!refs) {
        font_forget(object);
        AutFree(object);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE ifont_query(IFont *This, REFIID iid, void **result)
{
    return font_query(object_of_font(This), iid, result);
}

static ULONG STDMETHODCALLTYPE ifont_add_ref(IFont *This)
{
    return font_add_ref(object_of_font(This));
}

static ULONG STDMETHODCALLTYPE ifont_release(IFont *This)
{
    return font_release(object_of_font(This));
}

static HRESULT STDMETHODCALLTYPE ifont_get_name(IFont *This, BSTR *name)
{
    if (!name)
        return E_POINTER;
    FONT_OBJECT *object = object_of_font(This);
    HRESULT hr = font_ready(object);
    if (FAILED(hr)) {
        *name = nullptr;
        return hr;
    }
    const WCHAR *face = object->Spec.Face;
    if (!_wcsicmp(face, L"Helv"))
        face = L"MS Sans Serif";
    else if (!_wcsicmp(face, L"Tms Rmn"))
        face = L"MS Serif";
    *name = SysAllocString(face);
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE ifont_put_name(IFont *This, BSTR name)
{
    HRESULT hr = check_face(name);
    if (FAILED(hr))
        return hr;
    FONT_OBJECT *object = object_of_font(This);
    if (_wcsicmp(name, object->Spec.Face)) {
        spec_set_face(&object->Spec, name);
        object->Priority |= PRIORITY_NAME;
        font_forget(object);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ifont_get_size(IFont *This, CY *size)
{
    if (!size)
        return E_POINTER;
    FONT_OBJECT *object = object_of_font(This);
    HRESULT hr = font_ready(object);
    size->int64 = FAILED(hr) ? 0 : object->Spec.Size.int64;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ifont_put_size(IFont *This, CY size)
{
    FONT_OBJECT *object = object_of_font(This);
    if (size.int64 <= 0)
        return CTL_E_INVALIDPROPERTYVALUE;
    if (size.int64 > SIZE_LIMIT)
        return CTL_E_OVERFLOW;
    if ((ULONGLONG)((ULONG)size.int64 / 10000) * (ULONGLONG)object_dpi(object) >= SIZE_PIXEL_LIMIT)
        return CTL_E_OVERFLOW;
    if ((ULONG)size.int64 != (ULONG)object->Spec.Size.int64) {
        object->Spec.Size = size;
        font_forget(object);
    }
    return S_OK;
}

/* the questions about a flag or number of the record: they all resolve the font first. */
static HRESULT ready_for(IFont *This, FONT_OBJECT **object, void *output)
{
    if (!output)
        return E_POINTER;
    *object = object_of_font(This);
    return font_ready(*object);
}

static HRESULT STDMETHODCALLTYPE ifont_get_bold(IFont *This, BOOL *bold)
{
    FONT_OBJECT *object;
    HRESULT hr = ready_for(This, &object, bold);
    if (bold)
        *bold = SUCCEEDED(hr) && object->Spec.Weight > 550;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ifont_put_bold(IFont *This, BOOL bold)
{
    FONT_OBJECT *object = object_of_font(This);
    SHORT weight = bold ? 700 : 400;
    if (object->Spec.Weight != weight) {
        object->Spec.Weight = weight;
        font_forget(object);
    }
    return S_OK;
}

static HRESULT get_style_flag(IFont *This, BYTE flag, BOOL *value)
{
    FONT_OBJECT *object;
    HRESULT hr = ready_for(This, &object, value);
    if (value)
        *value = SUCCEEDED(hr) && (object->Spec.Style & flag) != 0;
    return hr;
}

static HRESULT put_style_flag(IFont *This, BYTE flag, BOOL value)
{
    FONT_OBJECT *object = object_of_font(This);
    if (((object->Spec.Style & flag) != 0) != (value != 0)) {
        object->Spec.Style ^= flag;
        font_forget(object);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ifont_get_italic(IFont *This, BOOL *value)
{
    return get_style_flag(This, STYLE_ITALIC, value);
}

static HRESULT STDMETHODCALLTYPE ifont_put_italic(IFont *This, BOOL value)
{
    return put_style_flag(This, STYLE_ITALIC, value);
}

static HRESULT STDMETHODCALLTYPE ifont_get_underline(IFont *This, BOOL *value)
{
    return get_style_flag(This, STYLE_UNDERLINE, value);
}

static HRESULT STDMETHODCALLTYPE ifont_put_underline(IFont *This, BOOL value)
{
    return put_style_flag(This, STYLE_UNDERLINE, value);
}

static HRESULT STDMETHODCALLTYPE ifont_get_strikethrough(IFont *This, BOOL *value)
{
    return get_style_flag(This, STYLE_STRIKEOUT, value);
}

static HRESULT STDMETHODCALLTYPE ifont_put_strikethrough(IFont *This, BOOL value)
{
    return put_style_flag(This, STYLE_STRIKEOUT, value);
}

static HRESULT STDMETHODCALLTYPE ifont_get_weight(IFont *This, SHORT *weight)
{
    FONT_OBJECT *object;
    HRESULT hr = ready_for(This, &object, weight);
    if (weight)
        *weight = SUCCEEDED(hr) ? object->Spec.Weight : 0;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ifont_put_weight(IFont *This, SHORT weight)
{
    if ((USHORT)weight > 1000)
        return CTL_E_INVALIDPROPERTYVALUE;
    FONT_OBJECT *object = object_of_font(This);
    if (object->Spec.Weight != weight) {
        object->Spec.Weight = weight;
        font_forget(object);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ifont_get_charset(IFont *This, SHORT *charset)
{
    FONT_OBJECT *object;
    HRESULT hr = ready_for(This, &object, charset);
    if (charset)
        *charset = SUCCEEDED(hr) ? (SHORT)object->Spec.Charset : 0;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ifont_put_charset(IFont *This, SHORT charset)
{
    if ((USHORT)charset > 255)
        return CTL_E_INVALIDPROPERTYVALUE;
    FONT_OBJECT *object = object_of_font(This);
    if (object->Spec.Charset != (WORD)charset) {
        object->Priority |= PRIORITY_CHARSET;
        object->Spec.Charset = (WORD)charset;
        font_forget(object);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ifont_get_hfont(IFont *This, HFONT *font)
{
    FONT_OBJECT *object;
    HRESULT hr = ready_for(This, &object, font);
    if (!font)
        return hr;
    *font = nullptr;
    if (SUCCEEDED(hr)) {
        AcquireSRWLockExclusive(&g_cache_lock);
        *font = g_slots[object->Slot].Handle;
        ReleaseSRWLockExclusive(&g_cache_lock);
    }
    return hr;
}

static HRESULT font_clone(FONT_OBJECT *object, IFont **copy)
{
    FONT_OBJECT *twin = AutAllocZero(sizeof(*twin));
    if (!twin)
        return E_OUTOFMEMORY;
    *twin = *object;
    twin->Refs = 1;
    AcquireSRWLockExclusive(&g_cache_lock);
    g_slots[twin->Slot].Uses++;
    ReleaseSRWLockExclusive(&g_cache_lock);
    *copy = (IFont *)twin;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ifont_clone(IFont *This, IFont **copy)
{
    if (!copy)
        return E_POINTER;
    *copy = nullptr;
    FONT_OBJECT *object = object_of_font(This);
    HRESULT hr = font_ready(object);
    return FAILED(hr) ? hr : font_clone(object, copy);
}

static BOOL is_font_class(IFont *other)
{
    IPersistStream *persist = nullptr;
    if (FAILED(other->lpVtbl->QueryInterface(other, &IID_IPersist, (void **)&persist)))
        return FALSE;
    CLSID id = {};
    HRESULT hr = persist->lpVtbl->GetClassID(persist, &id);
    persist->lpVtbl->Release(persist);
    return SUCCEEDED(hr) && (IsEqualCLSID(&id, &CLSID_StdFont) || IsEqualCLSID(&id, &CLSID_OldFont));
}

static HRESULT STDMETHODCALLTYPE ifont_is_equal(IFont *This, IFont *other)
{
    if (!other)
        return E_POINTER;
    if (!is_font_class(other))
        return S_FALSE;
    HFONT mine = nullptr, theirs = nullptr;
    if (FAILED(This->lpVtbl->get_hFont(This, &mine)) || FAILED(other->lpVtbl->get_hFont(other, &theirs)))
        return S_FALSE;
    return mine == theirs ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE ifont_set_ratio(IFont *This, LONG logical, LONG himetric)
{
    if (!logical || !himetric)
        return E_FAIL;
    FONT_OBJECT *object = object_of_font(This);
    if (object->Spec.Logical != logical || object->Spec.Himetric != himetric) {
        object->Spec.Logical = logical;
        object->Spec.Himetric = himetric;
        font_forget(object);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ifont_query_text_metrics(IFont *This, TEXTMETRICW *metrics)
{
    if (!metrics)
        return E_POINTER;
    FONT_OBJECT *object = object_of_font(This);
    HRESULT hr = font_ready(object);
    if (SUCCEEDED(hr)) {
        AcquireSRWLockExclusive(&g_cache_lock);
        *metrics = g_slots[object->Slot].Metrics;
        ReleaseSRWLockExclusive(&g_cache_lock);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE ifont_add_ref_hfont(IFont *This, HFONT font)
{
    if (!font)
        return E_INVALIDARG;
    HRESULT hr = S_FALSE;
    AcquireSRWLockExclusive(&g_cache_lock);
    LONG index = slot_find_handle(font);
    if (index >= 0) {
        g_slots[index].Uses++;
        hr = S_OK;
    }
    ReleaseSRWLockExclusive(&g_cache_lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ifont_release_hfont(IFont *This, HFONT font)
{
    if (!font)
        return E_INVALIDARG;
    HRESULT hr = S_FALSE;
    AcquireSRWLockExclusive(&g_cache_lock);
    LONG index = slot_find_handle(font);
    if (index >= 0 && g_slots[index].Uses) {
        g_slots[index].Uses--;
        hr = S_OK;
    }
    ReleaseSRWLockExclusive(&g_cache_lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ifont_set_hdc(IFont *This, HDC dc)
{
    object_of_font(This)->Dc = dc;
    return S_OK;
}

static const IFontVtbl g_font_vtbl = {
    ifont_query,
    ifont_add_ref,
    ifont_release,
    ifont_get_name,
    ifont_put_name,
    ifont_get_size,
    ifont_put_size,
    ifont_get_bold,
    ifont_put_bold,
    ifont_get_italic,
    ifont_put_italic,
    ifont_get_underline,
    ifont_put_underline,
    ifont_get_strikethrough,
    ifont_put_strikethrough,
    ifont_get_weight,
    ifont_put_weight,
    ifont_get_charset,
    ifont_put_charset,
    ifont_get_hfont,
    ifont_clone,
    ifont_is_equal,
    ifont_set_ratio,
    ifont_query_text_metrics,
    ifont_add_ref_hfont,
    ifont_release_hfont,
    ifont_set_hdc,
};

/* ------------------------------------------------------------------ IFontDisp */

static const OLESTD_MEMBER g_members[] = {
    {L"Name", DISPID_FONT_NAME},      {L"Size", DISPID_FONT_SIZE},          {L"Bold", DISPID_FONT_BOLD},
    {L"Italic", DISPID_FONT_ITALIC},  {L"Underline", DISPID_FONT_UNDER},    {L"Strikethrough", DISPID_FONT_STRIKE},
    {L"Weight", DISPID_FONT_WEIGHT},  {L"Charset", DISPID_FONT_CHARSET},
};

static HRESULT STDMETHODCALLTYPE idisp_query(IDispatch *This, REFIID iid, void **result)
{
    return font_query(object_of_dispatch(This), iid, result);
}

static ULONG STDMETHODCALLTYPE idisp_add_ref(IDispatch *This)
{
    return font_add_ref(object_of_dispatch(This));
}

static ULONG STDMETHODCALLTYPE idisp_release(IDispatch *This)
{
    return font_release(object_of_dispatch(This));
}

static HRESULT STDMETHODCALLTYPE idisp_type_info_count(IDispatch *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE idisp_type_info(IDispatch *This, UINT index, LCID lcid, ITypeInfo **info)
{
    return OleStdTypeInfo(index, &IID_IFontDisp, info);
}

static HRESULT STDMETHODCALLTYPE idisp_ids_of_names(IDispatch *This, REFIID iid, LPOLESTR *names, UINT count, LCID lcid,
                                                    DISPID *ids)
{
    return OleStdIdsOfNames(g_members, sizeof(g_members) / sizeof(g_members[0]), iid, names, count, ids);
}

static HRESULT font_get_member(IFont *font, DISPID id, VARIANT *result)
{
    BOOL flag = FALSE;
    SHORT number = 0;
    HRESULT hr;
    switch (id) {
    case DISPID_FONT_NAME: {
        BSTR name = nullptr;
        hr = font->lpVtbl->get_Name(font, &name);
        V_VT(result) = VT_BSTR;
        V_BSTR(result) = name;
        return hr;
    }
    case DISPID_FONT_SIZE:
        V_VT(result) = VT_CY;
        return font->lpVtbl->get_Size(font, &V_CY(result));
    case DISPID_FONT_BOLD:
        hr = font->lpVtbl->get_Bold(font, &flag);
        break;
    case DISPID_FONT_ITALIC:
        hr = font->lpVtbl->get_Italic(font, &flag);
        break;
    case DISPID_FONT_UNDER:
        hr = font->lpVtbl->get_Underline(font, &flag);
        break;
    case DISPID_FONT_STRIKE:
        hr = font->lpVtbl->get_Strikethrough(font, &flag);
        break;
    case DISPID_FONT_WEIGHT:
        hr = font->lpVtbl->get_Weight(font, &number);
        V_VT(result) = VT_I2;
        V_I2(result) = number;
        return hr;
    case DISPID_FONT_CHARSET:
        hr = font->lpVtbl->get_Charset(font, &number);
        V_VT(result) = VT_I2;
        V_I2(result) = number;
        return hr;
    default:
        return DISP_E_MEMBERNOTFOUND;
    }
    V_VT(result) = VT_BOOL;
    V_BOOL(result) = flag ? VARIANT_TRUE : VARIANT_FALSE;
    return hr;
}

/* the type a member is set with. */
static VARTYPE member_type(DISPID id)
{
    switch (id) {
    case DISPID_FONT_NAME: return VT_BSTR;
    case DISPID_FONT_SIZE: return VT_CY;
    case DISPID_FONT_BOLD:
    case DISPID_FONT_ITALIC:
    case DISPID_FONT_UNDER:
    case DISPID_FONT_STRIKE: return VT_BOOL;
    case DISPID_FONT_WEIGHT:
    case DISPID_FONT_CHARSET: return VT_I2;
    default: return VT_ILLEGAL;
    }
}

static HRESULT font_put_member(IFont *font, DISPID id, const VARIANT *value, LCID lcid)
{
    VARTYPE type = member_type(id);
    VARIANT converted;
    VariantInit(&converted);
    HRESULT hr = VariantChangeTypeEx(&converted, (VARIANT *)value, lcid, 0, type);
    if (FAILED(hr))
        return id == DISPID_FONT_SIZE && hr == DISP_E_OVERFLOW ? CTL_E_OVERFLOW : hr;
    switch (id) {
    case DISPID_FONT_NAME: hr = font->lpVtbl->put_Name(font, V_BSTR(&converted)); break;
    case DISPID_FONT_SIZE: hr = font->lpVtbl->put_Size(font, V_CY(&converted)); break;
    case DISPID_FONT_BOLD: hr = font->lpVtbl->put_Bold(font, V_BOOL(&converted) != 0); break;
    case DISPID_FONT_ITALIC: hr = font->lpVtbl->put_Italic(font, V_BOOL(&converted) != 0); break;
    case DISPID_FONT_UNDER: hr = font->lpVtbl->put_Underline(font, V_BOOL(&converted) != 0); break;
    case DISPID_FONT_STRIKE: hr = font->lpVtbl->put_Strikethrough(font, V_BOOL(&converted) != 0); break;
    case DISPID_FONT_WEIGHT: hr = font->lpVtbl->put_Weight(font, V_I2(&converted)); break;
    default: hr = font->lpVtbl->put_Charset(font, V_I2(&converted)); break;
    }
    VariantClear(&converted);
    return hr;
}

static HRESULT STDMETHODCALLTYPE idisp_invoke(IDispatch *This, DISPID id, REFIID iid, LCID lcid, WORD flags,
                                              DISPPARAMS *params, VARIANT *result, EXCEPINFO *info, UINT *arg_error)
{
    if (!iid || !IsEqualIID(iid, &GUID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    IFont *font = (IFont *)object_of_dispatch(This);
    HRESULT hr;
    if (flags & DISPATCH_PROPERTYGET) {
        hr = result ? font_get_member(font, id, result) : DISP_E_PARAMNOTOPTIONAL;
    } else if (flags & DISPATCH_PROPERTYPUT) {
        if (!params)
            hr = DISP_E_PARAMNOTOPTIONAL;
        else if (params->cArgs != 1)
            hr = DISP_E_BADPARAMCOUNT;
        else if (!params->rgvarg)
            hr = DISP_E_PARAMNOTOPTIONAL;
        else if (member_type(id) == VT_ILLEGAL)
            hr = DISP_E_MEMBERNOTFOUND;
        else
            hr = font_put_member(font, id, &params->rgvarg[0], lcid);
    } else {
        hr = DISP_E_MEMBERNOTFOUND;
    }
    return OleStdException(hr, info);
}

static const IDispatchVtbl g_dispatch_vtbl = {
    idisp_query, idisp_add_ref, idisp_release, idisp_type_info_count, idisp_type_info, idisp_ids_of_names, idisp_invoke,
};

/* ------------------------------------------------------------------ IPersistStream */

static HRESULT STDMETHODCALLTYPE ipersist_query(IPersistStream *This, REFIID iid, void **result)
{
    return font_query(object_of_persist(This), iid, result);
}

static ULONG STDMETHODCALLTYPE ipersist_add_ref(IPersistStream *This)
{
    return font_add_ref(object_of_persist(This));
}

static ULONG STDMETHODCALLTYPE ipersist_release(IPersistStream *This)
{
    return font_release(object_of_persist(This));
}

static HRESULT STDMETHODCALLTYPE ipersist_class_id(IPersistStream *This, CLSID *id)
{
    if (!id)
        return E_POINTER;
    *id = CLSID_StdFont;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ipersist_is_dirty(IPersistStream *This)
{
    return S_OK;
}

/* exactly `count` bytes of a stream, a short read being a failure. */
static HRESULT read_exact(IStream *stream, void *buffer, ULONG count)
{
    BYTE *at = buffer;
    while (count) {
        ULONG got = 0;
        HRESULT hr = stream->lpVtbl->Read(stream, at, count, &got);
        if (FAILED(hr))
            return hr;
        if (!got)
            return E_FAIL;
        at += got;
        count -= got;
    }
    return S_OK;
}

static ULONG get_le32(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (ULONG)p[3] << 24;
}

static WORD get_le16(const BYTE *p)
{
    return (WORD)(p[0] | p[1] << 8);
}

static void put_le16(BYTE *p, ULONG v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
}

static void put_le32(BYTE *p, ULONG v)
{
    put_le16(p, v & 0xFFFF);
    put_le16(p + 2, v >> 16);
}

static HRESULT STDMETHODCALLTYPE ipersist_load(IPersistStream *This, IStream *stream)
{
    if (!stream)
        return E_POINTER;
    FONT_OBJECT *object = object_of_persist(This);
    font_forget(object);

    BYTE version;
    HRESULT hr = read_exact(stream, &version, 1);
    if (FAILED(hr))
        return hr;
    if (version > STREAM_VERSION)
        return E_FAIL;
    BYTE head[2];
    WORD charset = object->Spec.Charset;
    if (version == 1) {
        hr = read_exact(stream, head, 2);
        if (FAILED(hr))
            return hr;
        charset = get_le16(head);
    }
    BYTE fields[8];
    hr = read_exact(stream, fields, sizeof(fields));
    if (FAILED(hr))
        return hr;
    ULONG length = fields[7];
    if (length >= FACE_CHARS)
        return E_FAIL;
    char ansi[FACE_CHARS];
    if (length) {
        hr = read_exact(stream, ansi, length);
        if (FAILED(hr))
            return hr;
    }
    WCHAR face[FACE_CHARS] = {};
    if (length && !MultiByteToWideChar(CP_ACP, 0, ansi, (int)length, face, FACE_CHARS - 1))
        return E_OUTOFMEMORY;

    spec_set_face(&object->Spec, face);
    object->Spec.Style = fields[0];
    object->Spec.Weight = (SHORT)get_le16(fields + 1);
    object->Spec.Size.int64 = get_le32(fields + 3);
    object->Spec.Charset = charset;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ipersist_save(IPersistStream *This, IStream *stream, BOOL clear_dirty)
{
    if (!stream)
        return E_POINTER;
    FONT_OBJECT *object = object_of_persist(This);
    char ansi[FACE_CHARS + 1] = {};
    WideCharToMultiByte(CP_ACP, 0, object->Spec.Face, -1, ansi, sizeof(ansi), nullptr, nullptr);
    ansi[FACE_CHARS] = 0;
    ULONG length = (ULONG)lstrlenA(ansi);

    BYTE data[STREAM_HEADER + FACE_CHARS];
    data[0] = STREAM_VERSION;
    put_le16(data + 1, object->Spec.Charset);
    data[3] = object->Spec.Style;
    put_le16(data + 4, (WORD)object->Spec.Weight);
    put_le32(data + 6, (ULONG)object->Spec.Size.int64);
    data[10] = (BYTE)length;
    memcpy(data + STREAM_HEADER, ansi, length);
    ULONG written = 0;
    HRESULT hr = stream->lpVtbl->Write(stream, data, STREAM_HEADER + length, &written);
    return SUCCEEDED(hr) && written != STREAM_HEADER + length ? E_FAIL : hr;
}

static HRESULT STDMETHODCALLTYPE ipersist_size_max(IPersistStream *This, ULARGE_INTEGER *size)
{
    if (!size)
        return E_POINTER;
    size->QuadPart = STREAM_HEADER + FACE_CHARS;
    return S_OK;
}

static const IPersistStreamVtbl g_persist_vtbl = {
    ipersist_query,   ipersist_add_ref, ipersist_release, ipersist_class_id,
    ipersist_is_dirty, ipersist_load,   ipersist_save,    ipersist_size_max,
};

/* ------------------------------------------------------------------ creation */

static HRESULT font_query(FONT_OBJECT *object, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!iid)
        return E_INVALIDARG;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IFont))
        *result = &object->Font;
    else if (IsEqualIID(iid, &IID_IDispatch) || IsEqualIID(iid, &IID_IFontDisp))
        *result = &object->Dispatch;
    else if (IsEqualIID(iid, &IID_IPersist) || IsEqualIID(iid, &IID_IPersistStream))
        *result = &object->Persist;
    else
        return E_NOINTERFACE;
    font_add_ref(object);
    return S_OK;
}

static FONT_OBJECT *font_new(void)
{
    FONT_OBJECT *object = AutAllocZero(sizeof(*object));
    if (!object)
        return nullptr;
    object->Font = &g_font_vtbl;
    object->Dispatch = &g_dispatch_vtbl;
    object->Persist = &g_persist_vtbl;
    object->Refs = 1;
    object->Slot = -1;
    object->Spec.Size.int64 = 80000;
    object->Spec.Logical = 1;
    object->Spec.Himetric = 1;
    object->Spec.Charset = DEFAULT_CHARSET;
    return object;
}

WINOLEAUTAPI HRESULT WINAPI OleCreateFontIndirect(LPFONTDESC desc, REFIID riid, LPVOID *result)
{
    static const FONTDESC fallback = {sizeof(FONTDESC), (LPOLESTR)L"MS Sans Serif", {.int64 = 80000}, 400, DEFAULT_CHARSET};
    if (!result)
        return E_POINTER;
    *result = nullptr;
    FONT_OBJECT *object = font_new();
    if (!object)
        return E_OUTOFMEMORY;
    if (!desc)
        desc = (LPFONTDESC)&fallback;

    object->Spec.Weight = desc->sWeight;
    object->Spec.Charset = (WORD)desc->sCharset;
    object->Spec.Style = (BYTE)(((desc->fItalic & 1) << 1) | ((desc->fUnderline & 1) << 2) | ((desc->fStrikethrough & 1) << 3));
    object->Spec.Size = desc->cySize;
    HRESULT hr = check_face(desc->lpstrName);
    if (SUCCEEDED(hr)) {
        spec_set_face(&object->Spec, desc->lpstrName);
        hr = font_query(object, riid, result);
    }
    font_release(object);
    return hr;
}
