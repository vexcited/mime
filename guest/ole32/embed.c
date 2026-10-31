/*
 * embedded objects: creating one of a class, loading one from a storage, and the picture of the icon of a class. the
 * objects of classes that have no server of their own go through the default handler, which is not here, and
 * the ones made from data, from a file or as a link need the data cache and the link object that are not here either.
 */
#include "ole32_private.h"
#include <mime/stub.h>

#define SLOT(object, index) ((*(void ***)(object))[index])
#define CALL(type, object, index, ...) ((type)SLOT(object, index))(object __VA_OPT__(, ) __VA_ARGS__)
#define RELEASE(object) ((ULONG(STDMETHODCALLTYPE *)(void *))SLOT(object, 2))(object)

#define OLERENDER_NONE 0
#define OLERENDER_DRAW 1
#define OLERENDER_FORMAT 2
#define OLERENDER_ASIS 3
#define CREATE_NEW 1
#define CREATE_LOAD 2
#define OLEMISC_SETCLIENTSITEFIRST_ 0x20000
#define DV_E_FORMATETC_ ((HRESULT)0x80040064)
#define DV_E_CLIPFORMAT_ ((HRESULT)0x8004006A)
#define DV_E_TYMED_ ((HRESULT)0x80040069)
#define DV_E_LINDEX_ ((HRESULT)0x80040068)
#define DV_E_DVASPECT_ ((HRESULT)0x8004006B)

MIME_DEFINE_GUID(IID_EmbedOleObject, 0x00000112, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_EmbedPersistStorage, 0x0000010a, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_EmbedOleCache, 0x0000011e, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_EmbedOleLink, 0x0000011d, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_EmbedDataObject, 0x0000010e, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_PictureMetafile, 0x00000315, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_PictureDib, 0x00000316, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_PictureEnhMetafile, 0x00000319, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

/* ------------------------------------------------------------------ the formats a picture is asked in */

static DWORD format_tymed(CLIPFORMAT format)
{
    switch (format) {
    case CF_BITMAP:
        return TYMED_GDI;
    case CF_METAFILEPICT:
        return TYMED_MFPICT;
    case CF_ENHMETAFILE:
        return TYMED_ENHMF;
    }
    return TYMED_HGLOBAL;
}

/* checks one format of a request, and copies it or the default draw format to the own one. */
static HRESULT validate_format(DWORD render, const FORMATETC *given, FORMATETC *own)
{
    if (render == OLERENDER_NONE || render == OLERENDER_ASIS)
        return S_OK;
    if (render == OLERENDER_DRAW && !given) {
        own->cfFormat = 0;
        own->ptd = nullptr;
        own->dwAspect = DVASPECT_CONTENT;
        own->lindex = -1;
        own->tymed = TYMED_NULL;
        return S_OK;
    }
    if (!given || (render != OLERENDER_DRAW && render != OLERENDER_FORMAT))
        return E_INVALIDARG;
    if (render == OLERENDER_FORMAT) {
        if (!given->cfFormat)
            return E_INVALIDARG;
        if (given->tymed != format_tymed(given->cfFormat))
            return DV_E_TYMED_;
    } else {
        if (given->cfFormat)
            return DV_E_CLIPFORMAT_;
        if (given->tymed != TYMED_NULL)
            return DV_E_TYMED_;
    }
    if (given->lindex != -1 && given->dwAspect != DVASPECT_ICON)
        return DV_E_LINDEX_;
    DWORD aspect = given->dwAspect;
    if (!aspect || (aspect & (aspect - 1)) || aspect > 8)
        return DV_E_DVASPECT_;
    *own = *given;
    return S_OK;
}

static HRESULT validate_create_params(DWORD flags, DWORD render, ULONG formats, const DWORD *advf,
                                      const FORMATETC *format, IAdviseSink *sink, IStorage *storage,
                                      IOleClientSite *site)
{
    if (flags & ~1u)
        return E_INVALIDARG;
    if (render == OLERENDER_DRAW && formats > 1)
        return E_INVALIDARG;
    if (render != OLERENDER_FORMAT && sink)
        return E_INVALIDARG;
    if (formats ? (!advf || !format) : (advf || format))
        return E_INVALIDARG;
    for (ULONG i = 0; i < formats; i++)
        if (advf[i] & ~0x7fu)
            return E_INVALIDARG;
    if (!storage)
        return E_INVALIDARG;
    (void)site;
    return S_OK;
}

/* ------------------------------------------------------------------ making the object */

/* the object of a class: made, given its client site and a new or loaded storage. */
static HRESULT create_object(const CLSID *class_id, REFIID riid, IOleClientSite *site, IStorage *storage, int mode,
                             void **object)
{
    CLSID clsid = *class_id, converted;
    *object = nullptr;
    if (mode == CREATE_NEW && OleGetAutoConvert(&clsid, &converted) == S_OK)
        clsid = converted;
    HRESULT hr = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | 0x400, riid, object);
    if (FAILED(hr))
        return hr;
    void *ole_object = nullptr;
    DWORD status = 0;
    if (site) {
        hr = ((IUnknown *)*object)->lpVtbl->QueryInterface(*object, &IID_EmbedOleObject, &ole_object);
        if (FAILED(hr))
            goto fail;
        if (FAILED(CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD, DWORD *), ole_object, 22, DVASPECT_CONTENT,
                        &status))) {
            status = 0;
        } else if (status & OLEMISC_SETCLIENTSITEFIRST_) {
            hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, IOleClientSite *), ole_object, 3, site);
            if (FAILED(hr)) {
                RELEASE(ole_object);
                goto fail;
            }
        }
    }
    void *persist;
    hr = ((IUnknown *)*object)->lpVtbl->QueryInterface(*object, &IID_EmbedPersistStorage, &persist);
    if (FAILED(hr)) {
        if (ole_object)
            RELEASE(ole_object);
        goto fail;
    }
    if (mode == CREATE_NEW) {
        hr = WriteClassStg(storage, &clsid);
        if (SUCCEEDED(hr))
            hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, IStorage *), persist, 5, storage);
    } else {
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, IStorage *), persist, 6, storage);
    }
    RELEASE(persist);
    if (SUCCEEDED(hr) && site) {
        if (!(status & OLEMISC_SETCLIENTSITEFIRST_))
            hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, IOleClientSite *), ole_object, 3, site);
        RELEASE(ole_object);
    } else if (ole_object) {
        RELEASE(ole_object);
    }
    if (SUCCEEDED(hr))
        return S_OK;
fail:
    RELEASE(*object);
    *object = nullptr;
    return hr;
}

static HRESULT icon_data(const CLSID *clsid, STGMEDIUM *medium)
{
    HGLOBAL picture = OleGetIconOfClass(clsid, nullptr, TRUE);
    if (!picture)
        return E_OUTOFMEMORY;
    medium->tymed = TYMED_MFPICT;
    medium->hGlobal = picture;
    medium->pUnkForRelease = nullptr;
    return S_OK;
}

/* the formats the caller asked to be cached are given to the object, when it keeps a cache. */
static HRESULT initialize_cache(const CLSID *clsid, DWORD render, ULONG formats, const DWORD *advf,
                                FORMATETC *format, void *object)
{
    void *cache = nullptr;
    HRESULT hr = S_OK;
    if (render == OLERENDER_NONE || render == OLERENDER_ASIS)
        return S_OK;
    if (FAILED(((IUnknown *)object)->lpVtbl->QueryInterface(object, &IID_EmbedOleCache, &cache))) {
        if (render == OLERENDER_FORMAT) {
            void *data;
            hr = ((IUnknown *)object)->lpVtbl->QueryInterface(object, &IID_EmbedDataObject, &data);
            if (SUCCEEDED(hr)) {
                FORMATETC probe = {format->cfFormat, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL | TYMED_GDI |
                                   TYMED_MFPICT | TYMED_ENHMF};
                if (CALL(HRESULT(STDMETHODCALLTYPE *)(void *, FORMATETC *), data, 5, &probe) != S_OK)
                    hr = DV_E_CLIPFORMAT_;
                RELEASE(data);
            }
        }
        return hr;
    }
    for (ULONG i = 0; i < formats; i++) {
        DWORD flags = advf ? advf[i] : ADVF_PRIMEFIRST;
        BOOL icon = FALSE;
        DWORD connection = 0;
        if (format[i].dwAspect == DVASPECT_ICON) {
            if (!format[i].cfFormat) {
                format[i].cfFormat = CF_METAFILEPICT;
                format[i].tymed = TYMED_MFPICT;
                icon = TRUE;
            } else if (format[i].cfFormat == CF_METAFILEPICT) {
                icon = TRUE;
            }
        }
        if (icon)
            flags = ADVF_NODATA;
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, FORMATETC *, DWORD, DWORD *), cache, 3, &format[i], flags,
                  &connection);
        if (FAILED(hr) && (!connection || !icon))
            break;
        hr = S_OK;
        if (icon) {
            STGMEDIUM medium = {};
            hr = icon_data(clsid, &medium);
            if (SUCCEEDED(hr)) {
                hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, FORMATETC *, STGMEDIUM *, BOOL), cache, 7, &format[i],
                          &medium, TRUE);
                if (FAILED(hr))
                    ReleaseStgMedium(&medium);
            }
        }
        if (FAILED(hr))
            break;
    }
    RELEASE(cache);
    return hr;
}

WINOLEAPI HRESULT WINAPI OleCreate(REFCLSID clsid, REFIID riid, DWORD render, FORMATETC *format, IOleClientSite *site,
                                   IStorage *storage, void **object)
{
    DWORD advf = ADVF_PRIMEFIRST;
    ULONG formats = format != nullptr;
    FORMATETC own = {};
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    HRESULT hr = validate_create_params(0, render, formats, formats ? &advf : nullptr, format, nullptr, storage, site);
    if (FAILED(hr))
        return hr;
    if (render == OLERENDER_DRAW || render == OLERENDER_FORMAT) {
        hr = validate_format(render, format, &own);
        if (FAILED(hr))
            return hr;
        formats = 1;
    }
    hr = create_object(clsid, riid, site, storage, CREATE_NEW, object);
    if (SUCCEEDED(hr) && render != OLERENDER_NONE && render != OLERENDER_ASIS) {
        hr = OleRun((IUnknown *)*object);
        if (SUCCEEDED(hr))
            hr = initialize_cache(clsid, render, formats, formats ? &advf : nullptr, &own, *object);
        if (FAILED(hr)) {
            void *ole_object;
            if (SUCCEEDED(((IUnknown *)*object)->lpVtbl->QueryInterface(*object, &IID_EmbedOleObject, &ole_object))) {
                CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD), ole_object, 6, 1);
                RELEASE(ole_object);
            }
        }
    }
    if (FAILED(hr)) {
        if (*object) {
            RELEASE(*object);
            *object = nullptr;
        }
        if ((hr & 0x7fffffff) == 0x40170)
            return DV_E_FORMATETC_;
    }
    return hr;
}

WINOLEAPI HRESULT WINAPI OleLoad(IStorage *storage, REFIID riid, IOleClientSite *site, void **object)
{
    CLSID clsid, converted;
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!storage)
        return E_INVALIDARG;
    HRESULT hr = ReadClassStg(storage, &clsid);
    if (FAILED(hr))
        return hr;
    if (OleGetAutoConvert(&clsid, &converted) == S_OK) {
        clsid = converted;
        WriteClassStg(storage, &clsid);
    }
    hr = create_object(&clsid, riid, site, storage, CREATE_LOAD, object);
    if (SUCCEEDED(hr) && site) {
        void *link;
        if (SUCCEEDED(((IUnknown *)*object)->lpVtbl->QueryInterface(*object, &IID_EmbedOleLink, &link))) {
            CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD, void *, void *, void *, DWORD), link, 8, 0, nullptr,
                 nullptr, nullptr, 0);
            RELEASE(link);
        }
    }
    return hr;
}

/* ------------------------------------------------------------------ objects of other things */

/* a picture class holds a bitmap or a metafile that was copied, which needs a cache the objects of data have. */
WINOLEAPI HRESULT WINAPI OleCreateStaticFromData(IDataObject *data, REFIID riid, DWORD render, FORMATETC *format,
                                                 IOleClientSite *site, IStorage *storage, void **object)
{
    FORMATETC own = {};
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!data || !storage || render == OLERENDER_NONE || render == OLERENDER_ASIS)
        return E_INVALIDARG;
    HRESULT hr = validate_format(render, format, &own);
    if (FAILED(hr))
        return hr;
    if (render == OLERENDER_DRAW) {
        static const WORD preferred[] = {CF_METAFILEPICT, CF_ENHMETAFILE, CF_DIB, CF_BITMAP};
        hr = DV_E_CLIPFORMAT_;
        for (size_t i = 0; i < RTL_NUMBER_OF(preferred); i++) {
            FORMATETC probe = {preferred[i], nullptr, DVASPECT_CONTENT, -1, format_tymed(preferred[i])};
            if (data->lpVtbl->QueryGetData(data, &probe) == S_OK) {
                own = probe;
                hr = S_OK;
                break;
            }
        }
        if (FAILED(hr))
            return hr;
    }
    const CLSID *picture;
    switch (own.cfFormat) {
    case CF_METAFILEPICT:
        picture = &CLSID_PictureMetafile;
        break;
    case CF_BITMAP:
    case CF_DIB:
        picture = &CLSID_PictureDib;
        break;
    case CF_ENHMETAFILE:
        picture = &CLSID_PictureEnhMetafile;
        break;
    default:
        return DV_E_CLIPFORMAT_;
    }
    STGMEDIUM medium = {};
    hr = data->lpVtbl->GetData(data, &own, &medium);
    if (FAILED(hr))
        return hr;
    void *ole_object = nullptr;
    hr = create_object(picture, &IID_EmbedOleObject, site, storage, CREATE_NEW, &ole_object);
    if (FAILED(hr)) {
        ReleaseStgMedium(&medium);
        return hr;
    }
    void *cache;
    hr = ((IUnknown *)ole_object)->lpVtbl->QueryInterface(ole_object, &IID_EmbedOleCache, &cache);
    if (SUCCEEDED(hr)) {
        FORMATETC cached = own;
        cached.tymed = format_tymed(own.cfFormat);
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, FORMATETC *, DWORD, DWORD *), cache, 3, &cached, ADVF_PRIMEFIRST,
                  nullptr);
        if (SUCCEEDED(hr))
            hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, FORMATETC *, STGMEDIUM *, BOOL), cache, 7, &cached, &medium,
                      TRUE);
        RELEASE(cache);
    }
    if (FAILED(hr))
        ReleaseStgMedium(&medium);
    if (SUCCEEDED(hr))
        hr = ((IUnknown *)ole_object)->lpVtbl->QueryInterface(ole_object, riid, object);
    RELEASE(ole_object);
    return hr;
}

/* the checks the other creators make before they need what is not here. */
static HRESULT check_creation(void *source, IStorage *storage, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!source || !storage)
        return E_INVALIDARG;
    return E_NOTIMPL;
}

WINOLEAPI HRESULT WINAPI CreateDataCache(IUnknown *outer, REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (outer && !IsEqualIID(riid, &IID_IUnknown))
        return E_INVALIDARG;
    return E_NOTIMPL;
}

WINOLEAPI HRESULT WINAPI OleCreateFromData(IDataObject *data, REFIID riid, DWORD render, FORMATETC *format,
                                           IOleClientSite *site, IStorage *storage, void **object)
{
    return check_creation(data, storage, object);
}

WINOLEAPI HRESULT WINAPI OleCreateLinkFromData(IDataObject *data, REFIID riid, DWORD render, FORMATETC *format,
                                               IOleClientSite *site, IStorage *storage, void **object)
{
    return check_creation(data, storage, object);
}

WINOLEAPI HRESULT WINAPI OleCreateLinkToFile(LPCOLESTR path, REFIID riid, DWORD render, FORMATETC *format,
                                             IOleClientSite *site, IStorage *storage, void **object)
{
    return check_creation((void *)path, storage, object);
}

WINOLEAPI HRESULT WINAPI OleCreateFromFile(REFCLSID clsid, LPCOLESTR path, REFIID riid, DWORD render,
                                           FORMATETC *format, IOleClientSite *site, IStorage *storage, void **object)
{
    return check_creation((void *)path, storage, object);
}

/* ------------------------------------------------------------------ the picture of an icon with its label */

static int StrToIntW_(const WCHAR *text)
{
    int sign = 1, value = 0;
    if (*text == L'-') {
        sign = -1;
        text++;
    }
    for (; *text >= L'0' && *text <= L'9'; text++)
        value = value * 10 + (*text - L'0');
    return sign * value;
}

static BOOL read_class_value(const CLSID *clsid, LPCWSTR subkey, WCHAR *out, DWORD chars)
{
    WCHAR guid[40], path[160];
    HKEY key;
    StringFromGUID2(clsid, guid, 40);
    _snwprintf(path, RTL_NUMBER_OF(path), L"CLSID\\%s%s%s", guid, subkey ? L"\\" : L"", subkey ? subkey : L"");
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return FALSE;
    DWORD size = chars * sizeof(WCHAR), type;
    LSTATUS status = RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)out, &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && *out;
}

/* the file and the index of the icon of a class: its DefaultIcon, or the program that serves it. */
static HICON class_icon(const CLSID *clsid, WCHAR *file, DWORD chars, UINT *index)
{
    WCHAR value[MAX_PATH + 16];
    *index = 0;
    if (read_class_value(clsid, L"DefaultIcon", value, RTL_NUMBER_OF(value))) {
        WCHAR *comma = wcsrchr(value, L',');
        if (comma) {
            *comma = 0;
            *index = (UINT)StrToIntW_(comma + 1);
        }
    } else if (!read_class_value(clsid, L"LocalServer32", value, RTL_NUMBER_OF(value)) &&
               !read_class_value(clsid, L"InprocServer32", value, RTL_NUMBER_OF(value))) {
        return nullptr;
    }
    ExpandEnvironmentStringsW(value, file, chars);
    HMODULE shell = LoadLibraryW(L"shell32.dll");
    HICON(WINAPI * extract)(HINSTANCE, LPCWSTR, UINT) =
        shell ? (HICON(WINAPI *)(HINSTANCE, LPCWSTR, UINT))GetProcAddress(shell, "ExtractIconW") : nullptr;
    HICON icon = extract ? extract(GetModuleHandleW(L"ole32.dll"), file, *index) : nullptr;
    return (ULONG_PTR)icon >= 0x20 ? icon : nullptr;
}

static void metafile_comment(HDC dc, const void *data, int size)
{
    Escape(dc, MFCOMMENT, size, data, nullptr);
}

static HGLOBAL icon_picture(HICON icon, LPCWSTR label, LPCWSTR source, UINT index)
{
    static const char icon_only[] = "IconOnly";
    static const char label_next[] = "OLE: Icon label next (Unicode)";
    static const char source_next[] = "OLE: Icon source next (Unicode)";
    char ansi_source[2 * MAX_PATH + 2] = "", ansi_label[48] = "";
    WCHAR short_label[43] = L"";
    BOOL source_lost = FALSE, label_lost = FALSE;
    int source_length = 0, label_length = 0;
    if (!icon)
        return nullptr;
    HDC screen = GetDC(nullptr);
    if (!screen)
        return nullptr;
    if (source) {
        BOOL used = FALSE;
        source_length = lstrlenW(source) + 1;
        WideCharToMultiByte(CP_ACP, 0, source, source_length, ansi_source, sizeof(ansi_source), "?", &used);
        source_lost = used;
    }
    if (label) {
        BOOL used = FALSE;
        lstrcpynW(short_label, label, 43);
        label_length = lstrlenW(short_label) + 1;
        WideCharToMultiByte(CP_ACP, 0, short_label, label_length, ansi_label, 43, "?", &used);
        label_lost = used;
    }
    HDC meta = CreateMetaFileA(nullptr);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, 0x18);
    if (!meta || !global) {
        if (meta)
            DeleteMetaFile(CloseMetaFile(meta));
        if (global)
            GlobalFree(global);
        ReleaseDC(nullptr, screen);
        return nullptr;
    }
    LOGFONTW font = {};
    SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(font), &font, 0);
    HFONT handle = CreateFontIndirectW(&font);
    HGDIOBJ old = SelectObject(screen, handle);
    TEXTMETRICW metrics = {};
    GetTextMetricsW(screen, &metrics);
    SelectObject(screen, old);
    int text_height = 3 * metrics.tmHeight;
    int icon_width = GetSystemMetrics(SM_CXICON), icon_height = GetSystemMetrics(SM_CYICON);
    int width = ansi_label[0] ? (3 * icon_width > icon_width ? 3 * icon_width : icon_width) : icon_width + icon_width / 4;
    int height = text_height + 4 + icon_height;
    SetMapMode(meta, MM_ANISOTROPIC);
    SetWindowOrgEx(meta, 0, 0, nullptr);
    SetWindowExtEx(meta, width, height, nullptr);
    DrawIcon(meta, (width - icon_width) / 2, 0, icon);
    metafile_comment(meta, icon_only, sizeof(icon_only));
    SetTextColor(meta, GetSysColor(COLOR_WINDOWTEXT));
    SetBkMode(meta, TRANSPARENT);
    if (ansi_label[0]) {
        RECT area = {0, icon_height + 4, width, height};
        HGDIOBJ previous = SelectObject(meta, handle);
        DrawTextA(meta, ansi_label, -1, &area, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(meta, previous);
    }
    if (source) {
        char number[24];
        metafile_comment(meta, ansi_source, source_length);
        int digits = _snprintf(number, sizeof(number), "%u", index);
        metafile_comment(meta, number, digits + 1);
    } else if (label_lost || source_lost) {
        metafile_comment(meta, "", 1);
        metafile_comment(meta, "0", 2);
    }
    if (label_lost) {
        metafile_comment(meta, label_next, sizeof(label_next));
        metafile_comment(meta, short_label, 2 * label_length);
    }
    if (source_lost) {
        metafile_comment(meta, source_next, sizeof(source_next));
        metafile_comment(meta, source, 2 * source_length);
    }
    HMETAFILE closed = CloseMetaFile(meta);
    if (!closed) {
        GlobalFree(global);
        global = nullptr;
    } else {
        DWORD *header = GlobalLock(global);
        header[0] = MM_ANISOTROPIC;
        header[1] = MulDiv(width, 2540, GetDeviceCaps(screen, LOGPIXELSX));
        header[2] = MulDiv(height, 2540, GetDeviceCaps(screen, LOGPIXELSY));
        *(HMETAFILE *)(header + 4) = closed;
        GlobalUnlock(global);
    }
    if (handle)
        DeleteObject(handle);
    ReleaseDC(nullptr, screen);
    return global;
}

WINOLEAPI HGLOBAL WINAPI OleGetIconOfClass(REFCLSID clsid, LPOLESTR label, BOOL use_type_as_label)
{
    WCHAR text[48] = L"", file[MAX_PATH];
    UINT index = 0;
    if (!use_type_as_label) {
        if (label)
            lstrcpynW(text, label, 42);
    } else if (!read_class_value(clsid, L"AuxUserType\\2", text, 42) && !read_class_value(clsid, nullptr, text, 42)) {
        if (!label)
            wcscpy(text, L"Document");
        else
            lstrcpynW(text, label, 42);
    }
    HICON icon = class_icon(clsid, file, MAX_PATH, &index);
    if (!icon) {
        HMODULE self = GetModuleHandleW(L"ole32.dll");
        if (!GetModuleFileNameW(self, file, MAX_PATH))
            return nullptr;
        icon = LoadIconW(self, MAKEINTRESOURCEW(8));
        index = 0;
    }
    HGLOBAL picture = icon_picture(icon, text, file, index);
    if (icon)
        DestroyIcon(icon);
    return picture;
}
