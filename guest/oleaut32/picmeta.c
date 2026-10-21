/*
 * metafiles for pictures: windows metafiles, with or without the placeable header that tells their size, and
 * enhanced metafiles.
 */
#include "picture.h"

#define WMF_HEADER_BYTES 18
#define WMF_HEADER_WORDS 9
#define WMF_MAX_WORDS 0x7FFFFFF6
#define WMF_WORDS_PER_RECORD 5
#define WMF_RECORD_BYTES (WMF_WORDS_PER_RECORD * 2)
#define WMF_SET_WINDOW_ORG 0x020B
#define WMF_SET_WINDOW_EXT 0x020C
#define EMF_SIGNATURE 0x464D4520
#define EMF_SIGNATURE_AT 40

static UINT get_u16(const BYTE *p)
{
    return p[0] | p[1] << 8;
}

static ULONG get_u32(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (ULONG)p[3] << 24;
}

static void put_u16(BYTE *p, UINT v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
}

static void put_u32(BYTE *p, ULONG v)
{
    put_u16(p, v & 0xFFFF);
    put_u16(p + 2, v >> 16);
}

static void put_record(BYTE *p, UINT function, int first, int second)
{
    put_u32(p, WMF_WORDS_PER_RECORD);
    put_u16(p + 4, function);
    put_u16(p + 6, (UINT)first & 0xFFFF);
    put_u16(p + 8, (UINT)second & 0xFFFF);
}

/* the records are there already when the data goes straight from the header to a window origin and extent. */
static BOOL has_window_records(const BYTE *data, ULONG size)
{
    if (size < WMF_HEADER_BYTES + 2 * WMF_RECORD_BYTES)
        return FALSE;
    const BYTE *first = data + WMF_HEADER_BYTES, *second = first + WMF_RECORD_BYTES;
    return get_u32(first) == WMF_WORDS_PER_RECORD && get_u16(first + 4) == WMF_SET_WINDOW_ORG &&
           get_u32(second) == WMF_WORDS_PER_RECORD && get_u16(second + 4) == WMF_SET_WINDOW_EXT;
}

/* the metafile of the bytes, given the area its coordinates cover when a placeable header said what that is. */
static HRESULT make_metafile(const BYTE *data, ULONG size, const RECT *area, HMETAFILE *result)
{
    *result = nullptr;
    if (size < WMF_HEADER_BYTES)
        return E_OUTOFMEMORY;
    if (get_u16(data + 2) != WMF_HEADER_WORDS || get_u32(data + 6) - WMF_HEADER_WORDS > WMF_MAX_WORDS)
        return CTL_E_INVALIDPICTURE;

    BYTE *copy = nullptr;
    const BYTE *bits = data;
    ULONG count = size;
    if (area && !has_window_records(data, size)) {
        count = size + 2 * WMF_RECORD_BYTES;
        copy = AutAlloc(count);
        if (!copy)
            return E_OUTOFMEMORY;
        memcpy(copy, data, WMF_HEADER_BYTES);
        put_record(copy + WMF_HEADER_BYTES, WMF_SET_WINDOW_ORG, area->top, area->left);
        put_record(copy + WMF_HEADER_BYTES + WMF_RECORD_BYTES, WMF_SET_WINDOW_EXT, area->bottom - area->top,
                   area->right - area->left);
        memcpy(copy + WMF_HEADER_BYTES + 2 * WMF_RECORD_BYTES, data + WMF_HEADER_BYTES, size - WMF_HEADER_BYTES);
        put_u32(copy + 6, get_u32(copy + 6) + 2 * WMF_WORDS_PER_RECORD);
        bits = copy;
    }
    *result = SetMetaFileBitsEx(count, bits);
    AutFree(copy);
    return *result ? S_OK : CTL_E_INVALIDPICTURE;
}

HRESULT PicFromMetafile(const BYTE *data, ULONG size, PIC_IMAGE *image)
{
    if (size >= EMF_SIGNATURE_AT + 4 && get_u32(data + EMF_SIGNATURE_AT) == EMF_SIGNATURE) {
        HENHMETAFILE emf = SetEnhMetaFileBits(size, data);
        if (!emf)
            return E_OUTOFMEMORY;
        image->Kind = PICTYPE_ENHMETAFILE;
        image->Emf = emf;
        return S_OK;
    }
    HMETAFILE wmf;
    HRESULT hr = make_metafile(data, size, nullptr, &wmf);
    if (FAILED(hr))
        return hr;
    image->Kind = PICTYPE_METAFILE;
    image->Wmf = wmf;
    return S_OK;
}

HRESULT PicFromPlaceable(const BYTE *data, ULONG size, PIC_IMAGE *image)
{
    if (size < PLACEABLE_HEADER || get_u32(data) != PLACEABLE_KEY)
        return CTL_E_INVALIDPICTURE;
    RECT area = {(short)get_u16(data + 6), (short)get_u16(data + 8), (short)get_u16(data + 10), (short)get_u16(data + 12)};
    UINT per_inch = get_u16(data + 14);
    if (!per_inch)
        per_inch = TWIPS_PER_INCH;
    HMETAFILE wmf;
    HRESULT hr = make_metafile(data + PLACEABLE_HEADER, size - PLACEABLE_HEADER, &area, &wmf);
    if (FAILED(hr))
        return hr;
    image->Kind = PICTYPE_METAFILE;
    image->Wmf = wmf;
    image->WmfCx = (int)OleStdRoundDiv((LONGLONG)(area.right - area.left) * TWIPS_PER_INCH, per_inch);
    image->WmfCy = (int)OleStdRoundDiv((LONGLONG)(area.bottom - area.top) * TWIPS_PER_INCH, per_inch);
    return S_OK;
}
