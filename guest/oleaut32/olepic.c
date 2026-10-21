/*
 * the standard picture: OleCreatePictureIndirect and OleLoadPictureEx make an object with IPicture, IPicture2, its
 * dispatch form and IPersistStream. The object keeps one image, the file it came from when it may be written back
 * as it was, and which handles it is to delete.
 */
#include "picture.h"

#define PERSIST_MARK_BYTES STREAM_HEADER_SIZE

typedef struct PICTURE_OBJECT {
    const IPictureVtbl *Pic;
    const IPicture2Vtbl *Pic2;
    const IDispatchVtbl *Dispatch;
    const IPersistStreamVtbl *Persist;
    LONG Refs;
    PIC_IMAGE Image;
    BOOL Keep;
    BOOL OwnHandles, OwnPalette;
    BYTE *Original;
    ULONG OriginalSize;
} PICTURE_OBJECT;

static PICTURE_OBJECT *object_of_pic(IPicture *p)
{
    return (PICTURE_OBJECT *)p;
}

static PICTURE_OBJECT *object_of_pic2(IPicture2 *p)
{
    return (PICTURE_OBJECT *)((char *)p - offsetof(PICTURE_OBJECT, Pic2));
}

static PICTURE_OBJECT *object_of_dispatch(IDispatch *p)
{
    return (PICTURE_OBJECT *)((char *)p - offsetof(PICTURE_OBJECT, Dispatch));
}

static PICTURE_OBJECT *object_of_persist(IPersistStream *p)
{
    return (PICTURE_OBJECT *)((char *)p - offsetof(PICTURE_OBJECT, Persist));
}

static void put_u32(BYTE *p, ULONG v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
    p[2] = (BYTE)(v >> 16);
    p[3] = (BYTE)(v >> 24);
}

/* a picture that was never given anything is as good as an empty one to the outside. */
static SHORT kind_of(const PICTURE_OBJECT *object)
{
    return object->Image.Kind == PIC_UNSET ? PICTYPE_NONE : object->Image.Kind;
}

static void picture_empty(PICTURE_OBJECT *object)
{
    PicImageFree(&object->Image, object->OwnHandles, object->OwnPalette);
    AutFree(object->Original);
    object->Original = nullptr;
    object->OriginalSize = 0;
}

static HRESULT picture_query(PICTURE_OBJECT *object, REFIID iid, void **result);

static ULONG picture_add_ref(PICTURE_OBJECT *object)
{
    return InterlockedIncrement(&object->Refs);
}

static ULONG picture_release(PICTURE_OBJECT *object)
{
    LONG refs = InterlockedDecrement(&object->Refs);
    if (!refs) {
        picture_empty(object);
        AutFree(object);
    }
    return refs;
}

/* ------------------------------------------------------------------ handles */

/* the handle a picture of this kind is drawn with. */
static HRESULT handle_of(const PICTURE_OBJECT *object, ULONG_PTR *handle)
{
    *handle = 0;
    switch (kind_of(object)) {
    case PICTYPE_NONE: return S_OK;
    case PICTYPE_BITMAP: *handle = (ULONG_PTR)object->Image.Bitmap; return S_OK;
    case PICTYPE_METAFILE: *handle = (ULONG_PTR)object->Image.Wmf; return S_OK;
    case PICTYPE_ICON: *handle = (ULONG_PTR)object->Image.Icon; return S_OK;
    case PICTYPE_ENHMETAFILE: *handle = (ULONG_PTR)object->Image.Emf; return S_OK;
    default: return E_INVALIDARG;
    }
}

/* a handle as 32 bits. S_FALSE tells that it does not fit, as a value that grows with zeros or with its sign. */
static HRESULT narrow(ULONG_PTR handle, OLE_HANDLE *result)
{
    *result = (OLE_HANDLE)handle;
    return handle == (ULONG_PTR)(OLE_HANDLE)handle || handle == (ULONG_PTR)(INT_PTR)(LONG)(OLE_HANDLE)handle ? S_OK : S_FALSE;
}

static HRESULT palette_of(const PICTURE_OBJECT *object, ULONG_PTR *palette)
{
    *palette = 0;
    if (kind_of(object) != PICTYPE_BITMAP)
        return E_FAIL;
    *palette = (ULONG_PTR)object->Image.Palette;
    return S_OK;
}

static HRESULT palette_set(PICTURE_OBJECT *object, ULONG_PTR palette)
{
    PIC_IMAGE *image = &object->Image;
    if (kind_of(object) != PICTYPE_BITMAP)
        return E_FAIL;
    if (object->OwnHandles && object->OwnPalette && image->Palette && image->Palette != (HPALETTE)palette)
        DeleteObject(image->Palette);
    image->Palette = (HPALETTE)palette;
    return S_OK;
}

static HRESULT get_handle_wide(PICTURE_OBJECT *object, HHANDLE *handle)
{
    if (!handle)
        return E_POINTER;
    ULONG_PTR value;
    HRESULT hr = handle_of(object, &value);
    *handle = value;
    return hr;
}

static HRESULT get_handle_narrow(PICTURE_OBJECT *object, OLE_HANDLE *handle)
{
    if (!handle)
        return E_POINTER;
    ULONG_PTR value;
    HRESULT hr = handle_of(object, &value);
    if (FAILED(hr)) {
        *handle = 0;
        return hr;
    }
    return narrow(value, handle);
}

static HRESULT get_palette_wide(PICTURE_OBJECT *object, HHANDLE *palette)
{
    if (!palette)
        return E_POINTER;
    ULONG_PTR value;
    HRESULT hr = palette_of(object, &value);
    *palette = value;
    return hr;
}

static HRESULT get_palette_narrow(PICTURE_OBJECT *object, OLE_HANDLE *palette)
{
    if (!palette)
        return E_POINTER;
    ULONG_PTR value;
    HRESULT hr = palette_of(object, &value);
    if (FAILED(hr)) {
        *palette = 0;
        return hr;
    }
    return narrow(value, palette);
}

/* ------------------------------------------------------------------ the methods both interfaces share */

static HRESULT picture_get_type(PICTURE_OBJECT *object, SHORT *type)
{
    if (!type)
        return E_POINTER;
    *type = kind_of(object);
    return S_OK;
}

static HRESULT picture_get_width(PICTURE_OBJECT *object, LONG *width)
{
    if (!width)
        return E_POINTER;
    *width = object->Image.Himetric.cx;
    return S_OK;
}

static HRESULT picture_get_height(PICTURE_OBJECT *object, LONG *height)
{
    if (!height)
        return E_POINTER;
    *height = object->Image.Himetric.cy;
    return S_OK;
}

static HRESULT picture_get_dc(PICTURE_OBJECT *object, HDC *dc)
{
    if (!dc)
        return E_POINTER;
    *dc = nullptr;
    if (kind_of(object) != PICTYPE_BITMAP)
        return E_FAIL;
    *dc = object->Image.Home;
    return S_OK;
}

static HRESULT picture_get_keep(PICTURE_OBJECT *object, BOOL *keep)
{
    if (!keep)
        return E_POINTER;
    *keep = object->Keep;
    return S_OK;
}

static HRESULT picture_put_keep(PICTURE_OBJECT *object, BOOL keep)
{
    if (keep && !object->Keep)
        return E_FAIL;
    object->Keep = keep != 0;
    if (!keep) {
        AutFree(object->Original);
        object->Original = nullptr;
        object->OriginalSize = 0;
    }
    return S_OK;
}

static HRESULT picture_changed(PICTURE_OBJECT *object)
{
    if (kind_of(object) == PICTYPE_BITMAP)
        PicMeasure(&object->Image);
    return S_OK;
}

static HRESULT picture_get_attributes(PICTURE_OBJECT *object, DWORD *attributes)
{
    if (!attributes)
        return E_POINTER;
    *attributes = 0;
    switch (kind_of(object)) {
    case PICTYPE_NONE: return S_OK;
    case PICTYPE_BITMAP: *attributes = object->Image.Mask ? PICTURE_TRANSPARENT : 0; return S_OK;
    case PICTYPE_METAFILE:
    case PICTYPE_ENHMETAFILE: *attributes = PICTURE_SCALABLE | PICTURE_TRANSPARENT; return S_OK;
    case PICTYPE_ICON: *attributes = PICTURE_TRANSPARENT; return S_OK;
    default: return E_FAIL;
    }
}

/* the bytes the picture is written as: the file it came from when asked for and kept, else a fresh copy. */
static HRESULT picture_bytes(PICTURE_OBJECT *object, BOOL copy, const BYTE **bytes, ULONG *size, BYTE **owned)
{
    *owned = nullptr;
    if (!copy) {
        if (object->Original) {
            *bytes = object->Original;
            *size = object->OriginalSize;
            return S_OK;
        }
        if (kind_of(object) != PICTYPE_NONE)
            return E_FAIL;
        *bytes = nullptr;
        *size = 0;
        return S_OK;
    }
    HRESULT hr = PicSerialize(&object->Image, owned, size);
    *bytes = *owned;
    return hr;
}

static HRESULT write_all(IStream *stream, const void *bytes, ULONG size)
{
    ULONG written = 0;
    HRESULT hr = size ? stream->lpVtbl->Write(stream, bytes, size, &written) : S_OK;
    return SUCCEEDED(hr) && size && written != size ? E_FAIL : hr;
}

static HRESULT picture_save(PICTURE_OBJECT *object, IStream *stream, BOOL copy, LONG *size)
{
    if (!stream)
        return E_POINTER;
    const BYTE *bytes;
    ULONG count;
    BYTE *owned;
    HRESULT hr = picture_bytes(object, copy, &bytes, &count, &owned);
    if (SUCCEEDED(hr)) {
        hr = write_all(stream, bytes, count);
        if (SUCCEEDED(hr) && size)
            *size = (LONG)count;
    }
    AutFree(owned);
    return hr;
}

/* ------------------------------------------------------------------ IPicture */

static HRESULT STDMETHODCALLTYPE ipic_query(IPicture *This, REFIID iid, void **result)
{
    return picture_query(object_of_pic(This), iid, result);
}

static ULONG STDMETHODCALLTYPE ipic_add_ref(IPicture *This)
{
    return picture_add_ref(object_of_pic(This));
}

static ULONG STDMETHODCALLTYPE ipic_release(IPicture *This)
{
    return picture_release(object_of_pic(This));
}

static HRESULT STDMETHODCALLTYPE ipic_get_handle(IPicture *This, OLE_HANDLE *handle)
{
    return get_handle_narrow(object_of_pic(This), handle);
}

static HRESULT STDMETHODCALLTYPE ipic_get_palette(IPicture *This, OLE_HANDLE *palette)
{
    return get_palette_narrow(object_of_pic(This), palette);
}

static HRESULT STDMETHODCALLTYPE ipic_get_type(IPicture *This, SHORT *type)
{
    return picture_get_type(object_of_pic(This), type);
}

static HRESULT STDMETHODCALLTYPE ipic_get_width(IPicture *This, OLE_XSIZE_HIMETRIC *width)
{
    return picture_get_width(object_of_pic(This), width);
}

static HRESULT STDMETHODCALLTYPE ipic_get_height(IPicture *This, OLE_YSIZE_HIMETRIC *height)
{
    return picture_get_height(object_of_pic(This), height);
}

static HRESULT STDMETHODCALLTYPE ipic_render(IPicture *This, HDC dc, LONG x, LONG y, LONG cx, LONG cy,
                                             OLE_XPOS_HIMETRIC xs, OLE_YPOS_HIMETRIC ys, OLE_XSIZE_HIMETRIC cxs,
                                             OLE_YSIZE_HIMETRIC cys, const RECT *bounds)
{
    return PicRender(&object_of_pic(This)->Image, dc, x, y, cx, cy, xs, ys, cxs, cys, bounds);
}

static HRESULT STDMETHODCALLTYPE ipic_set_palette(IPicture *This, OLE_HANDLE palette)
{
    return palette_set(object_of_pic(This), (ULONG_PTR)(INT_PTR)(LONG)palette);
}

static HRESULT STDMETHODCALLTYPE ipic_get_dc(IPicture *This, HDC *dc)
{
    return picture_get_dc(object_of_pic(This), dc);
}

static HRESULT STDMETHODCALLTYPE ipic_select(IPicture *This, HDC in, HDC *out, OLE_HANDLE *bitmap)
{
    HBITMAP displaced = nullptr;
    HRESULT hr = PicSelect(&object_of_pic(This)->Image, in, out, &displaced);
    if (SUCCEEDED(hr) && bitmap)
        *bitmap = (OLE_HANDLE)(ULONG_PTR)displaced;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ipic_get_keep(IPicture *This, BOOL *keep)
{
    return picture_get_keep(object_of_pic(This), keep);
}

static HRESULT STDMETHODCALLTYPE ipic_put_keep(IPicture *This, BOOL keep)
{
    return picture_put_keep(object_of_pic(This), keep);
}

static HRESULT STDMETHODCALLTYPE ipic_changed(IPicture *This)
{
    return picture_changed(object_of_pic(This));
}

static HRESULT STDMETHODCALLTYPE ipic_save(IPicture *This, IStream *stream, BOOL copy, LONG *size)
{
    return picture_save(object_of_pic(This), stream, copy, size);
}

static HRESULT STDMETHODCALLTYPE ipic_get_attributes(IPicture *This, DWORD *attributes)
{
    return picture_get_attributes(object_of_pic(This), attributes);
}

static const IPictureVtbl g_picture_vtbl = {
    ipic_query,       ipic_add_ref,  ipic_release,    ipic_get_handle, ipic_get_palette, ipic_get_type,
    ipic_get_width,   ipic_get_height, ipic_render,   ipic_set_palette, ipic_get_dc,     ipic_select,
    ipic_get_keep,    ipic_put_keep, ipic_changed,    ipic_save,       ipic_get_attributes,
};

/* ------------------------------------------------------------------ IPicture2 */

static HRESULT STDMETHODCALLTYPE ipic2_query(IPicture2 *This, REFIID iid, void **result)
{
    return picture_query(object_of_pic2(This), iid, result);
}

static ULONG STDMETHODCALLTYPE ipic2_add_ref(IPicture2 *This)
{
    return picture_add_ref(object_of_pic2(This));
}

static ULONG STDMETHODCALLTYPE ipic2_release(IPicture2 *This)
{
    return picture_release(object_of_pic2(This));
}

static HRESULT STDMETHODCALLTYPE ipic2_get_handle(IPicture2 *This, HHANDLE *handle)
{
    return get_handle_wide(object_of_pic2(This), handle);
}

static HRESULT STDMETHODCALLTYPE ipic2_get_palette(IPicture2 *This, HHANDLE *palette)
{
    return get_palette_wide(object_of_pic2(This), palette);
}

static HRESULT STDMETHODCALLTYPE ipic2_get_type(IPicture2 *This, SHORT *type)
{
    return picture_get_type(object_of_pic2(This), type);
}

static HRESULT STDMETHODCALLTYPE ipic2_get_width(IPicture2 *This, OLE_XSIZE_HIMETRIC *width)
{
    return picture_get_width(object_of_pic2(This), width);
}

static HRESULT STDMETHODCALLTYPE ipic2_get_height(IPicture2 *This, OLE_YSIZE_HIMETRIC *height)
{
    return picture_get_height(object_of_pic2(This), height);
}

static HRESULT STDMETHODCALLTYPE ipic2_render(IPicture2 *This, HDC dc, LONG x, LONG y, LONG cx, LONG cy,
                                              OLE_XPOS_HIMETRIC xs, OLE_YPOS_HIMETRIC ys, OLE_XSIZE_HIMETRIC cxs,
                                              OLE_YSIZE_HIMETRIC cys, const RECT *bounds)
{
    return PicRender(&object_of_pic2(This)->Image, dc, x, y, cx, cy, xs, ys, cxs, cys, bounds);
}

static HRESULT STDMETHODCALLTYPE ipic2_set_palette(IPicture2 *This, HHANDLE palette)
{
    return palette_set(object_of_pic2(This), palette);
}

static HRESULT STDMETHODCALLTYPE ipic2_get_dc(IPicture2 *This, HDC *dc)
{
    return picture_get_dc(object_of_pic2(This), dc);
}

static HRESULT STDMETHODCALLTYPE ipic2_select(IPicture2 *This, HDC in, HDC *out, HHANDLE *bitmap)
{
    HBITMAP displaced = nullptr;
    HRESULT hr = PicSelect(&object_of_pic2(This)->Image, in, out, &displaced);
    if (SUCCEEDED(hr) && bitmap)
        *bitmap = (HHANDLE)displaced;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ipic2_get_keep(IPicture2 *This, BOOL *keep)
{
    return picture_get_keep(object_of_pic2(This), keep);
}

static HRESULT STDMETHODCALLTYPE ipic2_put_keep(IPicture2 *This, BOOL keep)
{
    return picture_put_keep(object_of_pic2(This), keep);
}

static HRESULT STDMETHODCALLTYPE ipic2_changed(IPicture2 *This)
{
    return picture_changed(object_of_pic2(This));
}

static HRESULT STDMETHODCALLTYPE ipic2_save(IPicture2 *This, IStream *stream, BOOL copy, LONG *size)
{
    return picture_save(object_of_pic2(This), stream, copy, size);
}

static HRESULT STDMETHODCALLTYPE ipic2_get_attributes(IPicture2 *This, DWORD *attributes)
{
    return picture_get_attributes(object_of_pic2(This), attributes);
}

static const IPicture2Vtbl g_picture2_vtbl = {
    ipic2_query,       ipic2_add_ref,  ipic2_release,    ipic2_get_handle, ipic2_get_palette, ipic2_get_type,
    ipic2_get_width,   ipic2_get_height, ipic2_render,   ipic2_set_palette, ipic2_get_dc,     ipic2_select,
    ipic2_get_keep,    ipic2_put_keep, ipic2_changed,    ipic2_save,       ipic2_get_attributes,
};

/* ------------------------------------------------------------------ IPictureDisp */

#define DISPID_RENDER_ARGUMENTS 10

static const OLESTD_MEMBER g_members[] = {
    {L"Handle", DISPID_PICT_HANDLE}, {L"hPal", DISPID_PICT_HPAL},       {L"Type", DISPID_PICT_TYPE},
    {L"Width", DISPID_PICT_WIDTH},   {L"Height", DISPID_PICT_HEIGHT},   {L"Render", DISPID_PICT_RENDER},
};

static HRESULT STDMETHODCALLTYPE idisp_query(IDispatch *This, REFIID iid, void **result)
{
    return picture_query(object_of_dispatch(This), iid, result);
}

static ULONG STDMETHODCALLTYPE idisp_add_ref(IDispatch *This)
{
    return picture_add_ref(object_of_dispatch(This));
}

static ULONG STDMETHODCALLTYPE idisp_release(IDispatch *This)
{
    return picture_release(object_of_dispatch(This));
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
    return OleStdTypeInfo(index, &IID_IPictureDisp, info);
}

static HRESULT STDMETHODCALLTYPE idisp_ids_of_names(IDispatch *This, REFIID iid, LPOLESTR *names, UINT count, LCID lcid,
                                                    DISPID *ids)
{
    return OleStdIdsOfNames(g_members, sizeof(g_members) / sizeof(g_members[0]), iid, names, count, ids);
}

static HRESULT disp_get(PICTURE_OBJECT *object, DISPID id, VARIANT *result)
{
    HRESULT hr;
    switch (id) {
    case DISPID_PICT_HANDLE: {
        OLE_HANDLE handle;
        hr = get_handle_narrow(object, &handle);
        V_VT(result) = VT_I4;
        V_I4(result) = (LONG)handle;
        return hr;
    }
    case DISPID_PICT_HPAL: {
        OLE_HANDLE palette;
        hr = get_palette_narrow(object, &palette);
        V_VT(result) = VT_I4;
        V_I4(result) = (LONG)palette;
        return hr;
    }
    case DISPID_PICT_TYPE: {
        SHORT type;
        hr = picture_get_type(object, &type);
        V_VT(result) = VT_I2;
        V_I2(result) = type;
        return hr;
    }
    case DISPID_PICT_WIDTH:
        V_VT(result) = VT_I4;
        return picture_get_width(object, &V_I4(result));
    case DISPID_PICT_HEIGHT:
        V_VT(result) = VT_I4;
        return picture_get_height(object, &V_I4(result));
    case DISPID_PICT_HANDLE64: {
        HHANDLE handle;
        hr = get_handle_wide(object, &handle);
        V_VT(result) = VT_I8;
        V_I8(result) = (LONGLONG)handle;
        return hr;
    }
    case DISPID_PICT_HPAL64: {
        HHANDLE palette;
        hr = get_palette_wide(object, &palette);
        V_VT(result) = VT_I8;
        V_I8(result) = (LONGLONG)palette;
        return hr;
    }
    default: return DISP_E_MEMBERNOTFOUND;
    }
}

/* the whole number of a variant, when it is one. */
static BOOL whole_number(const VARIANT *v, BOOL *negative, ULONGLONG *magnitude)
{
    LONGLONG value;
    *negative = FALSE;
    switch (V_VT(v)) {
    case VT_I1: value = V_I1(v); break;
    case VT_UI1: value = V_UI1(v); break;
    case VT_I2: value = V_I2(v); break;
    case VT_UI2: value = V_UI2(v); break;
    case VT_I4:
    case VT_INT: value = V_I4(v); break;
    case VT_UI4:
    case VT_UINT: value = V_UI4(v); break;
    case VT_I8: value = V_I8(v); break;
    case VT_UI8: *magnitude = V_UI8(v); return TRUE;
    default: return FALSE;
    }
    *negative = value < 0;
    *magnitude = value < 0 ? (ULONGLONG)-value : (ULONGLONG)value;
    return TRUE;
}

static HRESULT disp_put_palette(PICTURE_OBJECT *object, DISPID id, const VARIANT *value)
{
    BOOL negative;
    ULONGLONG magnitude;
    if (!whole_number(value, &negative, &magnitude))
        return DISP_E_TYPEMISMATCH;
    ULONG_PTR palette;
    if (id == DISPID_PICT_HPAL) {
        if (negative ? magnitude > 0x80000000u : magnitude > 0xFFFFFFFFu)
            return CTL_E_OVERFLOW;
        LONGLONG signed_value = negative ? -(LONGLONG)magnitude : (LONGLONG)magnitude;
        palette = (ULONG_PTR)(INT_PTR)(LONG)(ULONG)signed_value;
    } else {
        palette = (ULONG_PTR)(negative ? (ULONGLONG)-(LONGLONG)magnitude : magnitude);
    }
    return palette_set(object, palette);
}

/* an integer argument of Render: 32 bits, or 16 or 64 where the signature allows. */
static BOOL render_integer(const VARIANT *v, BOOL allow_small, BOOL allow_wide, LONGLONG *value)
{
    if (V_VT(v) == VT_I4) {
        *value = V_I4(v);
        return TRUE;
    }
    if (allow_small && V_VT(v) == VT_I2) {
        *value = V_I2(v);
        return TRUE;
    }
    if (allow_wide && V_VT(v) == VT_I8) {
        *value = V_I8(v);
        return TRUE;
    }
    return FALSE;
}

static HRESULT disp_render(PICTURE_OBJECT *object, const DISPPARAMS *params)
{
    if (params->cArgs != DISPID_RENDER_ARGUMENTS)
        return DISP_E_BADPARAMCOUNT;
    if (!params->rgvarg)
        return DISP_E_PARAMNOTOPTIONAL;
    const VARIANT *args = params->rgvarg;
    LONGLONG bounds, dc, number[8];
    if (!render_integer(&args[0], TRUE, FALSE, &bounds) || !render_integer(&args[9], FALSE, TRUE, &dc))
        return DISP_E_TYPEMISMATCH;
    for (int i = 0; i < 8; i++) {
        if (!render_integer(&args[8 - i], FALSE, FALSE, &number[i]))
            return DISP_E_TYPEMISMATCH;
    }
    const RECT *rect = (const RECT *)(ULONG_PTR)(V_VT(&args[0]) == VT_I2 ? (ULONG_PTR)(USHORT)bounds : (ULONG_PTR)(ULONG)bounds);
    return PicRender(&object->Image, (HDC)(ULONG_PTR)dc, (LONG)number[0], (LONG)number[1], (LONG)number[2], (LONG)number[3],
                     (LONG)number[4], (LONG)number[5], (LONG)number[6], (LONG)number[7], rect);
}

static HRESULT STDMETHODCALLTYPE idisp_invoke(IDispatch *This, DISPID id, REFIID iid, LCID lcid, WORD flags,
                                              DISPPARAMS *params, VARIANT *result, EXCEPINFO *info, UINT *arg_error)
{
    if (!iid || !IsEqualIID(iid, &GUID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    PICTURE_OBJECT *object = object_of_dispatch(This);
    BOOL is_render = id == DISPID_PICT_RENDER || id == DISPID_PICT_RENDER64;
    HRESULT hr;
    if ((flags & DISPATCH_METHOD) && is_render) {
        hr = params ? disp_render(object, params) : DISP_E_PARAMNOTOPTIONAL;
    } else if (flags & DISPATCH_PROPERTYGET) {
        if (!params)
            hr = DISP_E_PARAMNOTOPTIONAL;
        else if (!result)
            hr = DISP_E_PARAMNOTOPTIONAL;
        else if (params->cArgs)
            hr = DISP_E_BADPARAMCOUNT;
        else
            hr = disp_get(object, id, result);
    } else if (flags & DISPATCH_PROPERTYPUT) {
        if (!params)
            hr = DISP_E_PARAMNOTOPTIONAL;
        else if (params->cArgs != 1)
            hr = DISP_E_BADPARAMCOUNT;
        else if (!params->rgvarg)
            hr = DISP_E_PARAMNOTOPTIONAL;
        else if (id != DISPID_PICT_HPAL && id != DISPID_PICT_HPAL64)
            hr = DISP_E_MEMBERNOTFOUND;
        else
            hr = disp_put_palette(object, id, &params->rgvarg[0]);
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
    return picture_query(object_of_persist(This), iid, result);
}

static ULONG STDMETHODCALLTYPE ipersist_add_ref(IPersistStream *This)
{
    return picture_add_ref(object_of_persist(This));
}

static ULONG STDMETHODCALLTYPE ipersist_release(IPersistStream *This)
{
    return picture_release(object_of_persist(This));
}

static HRESULT STDMETHODCALLTYPE ipersist_class_id(IPersistStream *This, CLSID *id)
{
    if (!id)
        return E_INVALIDARG;
    *id = CLSID_StdPicture;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ipersist_is_dirty(IPersistStream *This)
{
    return S_OK;
}

/* the picture takes what the stream holds in place of its own, and only when all of it was understood. */
static HRESULT STDMETHODCALLTYPE ipersist_load(IPersistStream *This, IStream *stream)
{
    PICTURE_OBJECT *object = object_of_persist(This);
    if (!stream)
        return E_POINTER;
    PIC_IMAGE image;
    BYTE *original;
    ULONG original_size;
    HRESULT hr = PicReadStream(stream, 0, object->Keep, 0, 0, 0, &image, &original, &original_size);
    if (FAILED(hr))
        return hr;
    picture_empty(object);
    object->Image = image;
    object->OwnHandles = TRUE;
    object->OwnPalette = FALSE;
    object->Original = original;
    object->OriginalSize = original_size;
    return S_OK;
}

/* what is written for the picture: the mark of the stream format, the length and the file. */
static HRESULT STDMETHODCALLTYPE ipersist_save(IPersistStream *This, IStream *stream, BOOL clear_dirty)
{
    PICTURE_OBJECT *object = object_of_persist(This);
    if (!stream)
        return E_INVALIDARG;
    const BYTE *bytes;
    ULONG count;
    BYTE *owned;
    HRESULT hr = picture_bytes(object, !object->Original, &bytes, &count, &owned);
    if (SUCCEEDED(hr)) {
        BYTE mark[PERSIST_MARK_BYTES] = {'l', 't'};
        put_u32(mark + 4, count);
        hr = write_all(stream, mark, sizeof(mark));
        if (SUCCEEDED(hr))
            hr = write_all(stream, bytes, count);
    }
    AutFree(owned);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ipersist_size_max(IPersistStream *This, ULARGE_INTEGER *size)
{
    PICTURE_OBJECT *object = object_of_persist(This);
    if (!size)
        return E_INVALIDARG;
    const BYTE *bytes;
    ULONG count;
    BYTE *owned;
    HRESULT hr = picture_bytes(object, !object->Original, &bytes, &count, &owned);
    AutFree(owned);
    size->QuadPart = SUCCEEDED(hr) ? (ULONGLONG)count + PERSIST_MARK_BYTES : 0;
    return hr;
}

static const IPersistStreamVtbl g_persist_vtbl = {
    ipersist_query,    ipersist_add_ref, ipersist_release, ipersist_class_id,
    ipersist_is_dirty, ipersist_load,    ipersist_save,    ipersist_size_max,
};

/* ------------------------------------------------------------------ creation */

static HRESULT picture_query(PICTURE_OBJECT *object, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!iid)
        return E_INVALIDARG;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IPicture))
        *result = &object->Pic;
    else if (IsEqualIID(iid, &IID_IPicture2))
        *result = &object->Pic2;
    else if (IsEqualIID(iid, &IID_IDispatch) || IsEqualIID(iid, &IID_IPictureDisp))
        *result = &object->Dispatch;
    else if (IsEqualIID(iid, &IID_IPersistStream))
        *result = &object->Persist;
    else
        return E_NOINTERFACE;
    picture_add_ref(object);
    return S_OK;
}

static PICTURE_OBJECT *picture_new(BOOL keep)
{
    PICTURE_OBJECT *object = AutAllocZero(sizeof(*object));
    if (!object)
        return nullptr;
    object->Pic = &g_picture_vtbl;
    object->Pic2 = &g_picture2_vtbl;
    object->Dispatch = &g_dispatch_vtbl;
    object->Persist = &g_persist_vtbl;
    object->Refs = 1;
    object->Image.Kind = PIC_UNSET;
    object->Keep = keep;
    return object;
}

/* the object is handed out through the interface asked for, and the reference of its maker goes. */
static HRESULT picture_hand_out(PICTURE_OBJECT *object, REFIID riid, void **result)
{
    HRESULT hr = picture_query(object, riid, result);
    picture_release(object);
    return hr;
}

static HRESULT take_description(PICTURE_OBJECT *object, const PICTDESC *desc, BOOL own)
{
    PIC_IMAGE *image = &object->Image;
    image->Kind = (SHORT)desc->picType;
    if (desc->picType > PICTYPE_ENHMETAFILE)
        image->Kind = (SHORT)0x7FFF;
    object->OwnHandles = own;
    switch (desc->picType) {
    case PICTYPE_NONE: break;
    case PICTYPE_BITMAP:
        image->Bitmap = desc->bmp.hbitmap;
        image->Palette = desc->bmp.hpal;
        object->OwnPalette = own;
        break;
    case PICTYPE_METAFILE:
        image->Wmf = desc->wmf.hmeta;
        image->WmfCx = desc->wmf.xExt;
        image->WmfCy = desc->wmf.yExt;
        break;
    case PICTYPE_ICON: {
        image->Icon = desc->icon.hicon;
        HRESULT hr = PicIconPlanes(desc->icon.hicon, &image->IconAnd, &image->IconXor);
        if (FAILED(hr))
            return hr;
        break;
    }
    case PICTYPE_ENHMETAFILE: image->Emf = desc->emf.hemf; break;
    default: break;
    }
    return PicMeasure(image);
}

WINOLEAUTAPI HRESULT WINAPI OleCreatePictureIndirect(LPPICTDESC desc, REFIID riid, BOOL own, LPVOID *result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    PICTURE_OBJECT *object = picture_new(FALSE);
    if (!object)
        return E_OUTOFMEMORY;
    if (desc) {
        HRESULT hr = take_description(object, desc, own);
        if (FAILED(hr)) {
            picture_release(object);
            return hr;
        }
    }
    return picture_hand_out(object, riid, result);
}

WINOLEAUTAPI HRESULT WINAPI OleLoadPictureEx(LPSTREAM stream, LONG size, BOOL run_mode, REFIID riid, DWORD cx_desired,
                                             DWORD cy_desired, DWORD flags, LPVOID *result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!stream)
        return E_POINTER;
    PICTURE_OBJECT *object = picture_new(!run_mode);
    if (!object)
        return E_OUTOFMEMORY;
    HRESULT hr = PicReadStream(stream, (ULONG)size, object->Keep, cx_desired, cy_desired, flags, &object->Image,
                               &object->Original, &object->OriginalSize);
    if (FAILED(hr)) {
        picture_release(object);
        return hr;
    }
    object->OwnHandles = TRUE;
    return picture_hand_out(object, riid, result);
}
