#include "evr_private.h"

#define FCC(a, b, c, d) ((DWORD)(BYTE)(a) | (DWORD)(BYTE)(b) << 8 | (DWORD)(BYTE)(c) << 16 | (DWORD)(BYTE)(d) << 24)

/* the formats of the table evr shares with mfplat. a format outside it is treated as rgb. */
static const DWORD KNOWN[] = {
    118, FCC('4', 'P', 'x', 'x'), 41, 24, 25, 22, 21, 23, 20, 26, 30, 35, 31, 36, 113, 116, 50, 81, 80, 32, 102,
    FCC('C', 'R', 'A', 'M'), FCC('C', 'R', 'A', 'N'), FCC('M', 'J', 'P', 'G'), FCC('I', '4', '2', '0'),
    FCC('I', 'Y', 'U', 'V'), FCC('N', 'V', '1', '2'), FCC('N', 'V', '2', '4'), FCC('Y', 'V', '1', '2'),
    FCC('Y', '4', '1', 'P'), FCC('I', 'M', 'C', '2'), FCC('I', 'M', 'C', '4'), FCC('I', 'M', 'C', '1'),
    FCC('I', 'M', 'C', '3'), FCC('4', '2', '0', 'O'), FCC('S', '3', '4', '0'), FCC('P', '0', '1', '0'),
    FCC('P', '0', '1', '6'), FCC('N', 'V', '1', '1'), FCC('Y', '4', '1', '1'), FCC('P', '2', '0', '8'),
    FCC('Y', 'U', 'Y', '2'), FCC('U', 'Y', 'V', 'Y'), FCC('Y', 'V', 'Y', 'U'), FCC('v', '2', '1', '0'),
    FCC('H', 'D', '1', '0'), FCC('Y', '2', '1', '0'), FCC('Y', '2', '1', '6'), FCC('P', '2', '1', '0'),
    FCC('P', '2', '1', '6'), FCC('A', 'Y', 'U', 'V'), FCC('Y', '4', '1', '0'), FCC('Y', '4', '1', '6'),
    FCC('v', '4', '1', '0'), FCC('Y', 'V', 'U', '9'), FCC('Y', '2', '1', '1'), FCC('I', 'A', '4', '4'),
    FCC('A', 'I', '4', '4'), FCC('I', '4', '2', '2'), FCC('I', '4', '4', '4'),
};

static BOOL is_rgb(DWORD fourcc)
{
    static const DWORD RGB[] = {118, FCC('4', 'P', 'x', 'x'), 41, 24, 25, 21, 32, 23, 20, 22,
                                26,  30,                      35, 31, 36, 113, 116, 50, 81, 80};
    for (ULONG i = 0; i < RTL_NUMBER_OF(RGB); i++)
        if (RGB[i] == fourcc)
            return TRUE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(KNOWN); i++)
        if (KNOWN[i] == fourcc)
            return FALSE;
    return TRUE;
}

/*
 * evr's MapMFVideoInfoToExtendedFormat2. An rgb format keeps no chroma siting and no matrix, and with output set its
 * range and lighting are left unknown, as the mixer asks for the destination of a blit.
 */
UINT EvrExtendedFormat(const MFVIDEOFORMAT *format, UINT interlace, BOOL output)
{
    const MFVIDEOINFO *info = &format->videoInfo;
    BOOL rgb = is_rgb(format->surfaceInfo.Format);
    if (interlace == MFVideoInterlace_Unknown)
        interlace = 2;
    else if (interlace == MFVideoInterlace_MixedInterlaceOrProgressive)
        interlace = 3;
    UINT value = interlace & 0xFF;
    value |= (info->SourceChromaSubsampling << 8) & 0xF00;
    value |= (info->NominalRange << 12) & 0x7000;
    value |= (info->TransferMatrix << 15) & 0x38000;
    value |= (info->SourceLighting << 18) & 0x3C0000;
    value |= (info->ColorPrimaries << 22) & 0x7C00000;
    value = (value & 0x7FFFFFF) | info->TransferFunction << 27;
    if (rgb) {
        value &= 0xFFFC70FF;
    } else {
        if (!(value & 0xF00))
            value = (value & 0xFFFFF0FF) | 0x500;
        if (!(value & 0x38000))
            value = (value & 0xFFFC7FFF) | (info->dwHeight <= 576 ? 0x10000 : 0x8000);
    }
    if (!(value & 0x7C00000))
        value = (value & 0xF83FFFFF) | 0x800000;
    if (!rgb || !output) {
        if (!(value & 0x7000))
            value = (value & 0xFFFF8FFF) | 0x2000;
        if (!(value & 0x3C0000))
            value = (value & 0xFFC3FFFF) | 0xC0000;
        if (value < 0x8000000)
            value = (value & 0x7FFFFFF) | 0x28000000;
    }
    return value;
}

void EvrVideoDesc(const MFVIDEOFORMAT *format, DXVA2_VideoDesc *desc)
{
    memset(desc, 0, sizeof(*desc));
    desc->SampleWidth = format->videoInfo.dwWidth;
    desc->SampleHeight = format->videoInfo.dwHeight;
    desc->SampleFormat.value = EvrExtendedFormat(format, format->videoInfo.InterlaceMode, FALSE);
    desc->Format = (D3DFORMAT)format->surfaceInfo.Format;
    desc->InputSampleFreq.Numerator = format->videoInfo.FramesPerSecond.Numerator;
    desc->InputSampleFreq.Denominator = format->videoInfo.FramesPerSecond.Denominator;
    desc->OutputFrameFreq = desc->InputSampleFreq;
    if ((desc->SampleFormat.value & 0xFF) - 3 <= 1)
        desc->OutputFrameFreq.Numerator *= 2;
}

/* IMFVideoMediaType::GetVideoFormat follows the 38 methods of IMFMediaType. */
const MFVIDEOFORMAT *EvrVideoFormat(IUnknown *video)
{
    return ((const MFVIDEOFORMAT *(STDMETHODCALLTYPE **)(IUnknown *))video->lpVtbl)[38](video);
}

/* IMFVideoMediaType::IsCompressedFormat is the eleventh method of IMFMediaType. */
static HRESULT is_compressed(IUnknown *video, BOOL *compressed)
{
    return ((HRESULT(STDMETHODCALLTYPE **)(IUnknown *, BOOL *))video->lpVtbl)[34](video, compressed);
}

static double fmin(double a, double b)
{
    return a < b ? a : b;
}

static double fmax(double a, double b)
{
    return a > b ? a : b;
}

static double offset_of(MFOffset o)
{
    return o.fract * (1.0 / 65536.0) + o.value;
}

/* evr's ValidateVideoFormatGeometry: no aperture may reach past the picture. */
static HRESULT validate_geometry(const MFVIDEOFORMAT *format)
{
    const MFVIDEOINFO *info = &format->videoInfo;
    int width = (int)info->dwWidth, height = (int)info->dwHeight;
    if (info->InterlaceMode - 5 <= 1)
        height *= 2;
    const MFVideoArea *areas[3] = {&info->GeometricAperture, &info->MinimumDisplayAperture, &info->PanScanAperture};
    for (int i = 0; i < 3; i++) {
        int x = (int)(float)((float)areas[i]->OffsetX.value + 0.0f);
        int y = (int)((float)areas[i]->OffsetY.value + 0.0f);
        if ((int)(areas[i]->Area.cx + x) > width || y + areas[i]->Area.cy > height)
            return MF_E_INVALIDMEDIATYPE;
    }
    return S_OK;
}

/* evr's ValidateVideoMediaType, the check every type set on the mixer or the presenter goes through. */
HRESULT EvrValidateVideoType(IUnknown *video)
{
    const MFVIDEOFORMAT *format = EvrVideoFormat(video);
    if (!format)
        return E_INVALIDARG;
    static const BYTE BASE[8] = {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
    if (format->surfaceInfo.Format != format->guidFormat.Data1 || format->guidFormat.Data2 ||
        format->guidFormat.Data3 != 0x10 || memcmp(format->guidFormat.Data4, BASE, 8))
        return MF_E_INVALIDMEDIATYPE;
    BOOL compressed = FALSE;
    HRESULT hr = is_compressed(video, &compressed);
    if (FAILED(hr))
        return hr;
    if (compressed)
        return MF_E_INVALIDMEDIATYPE;
    DWORD fourcc = MFGetUncompressedVideoFormat(format);
    if (fourcc == 28 || fourcc == 40 || fourcc == 41 || fourcc == 50 || fourcc - 51 <= 1)
        return MF_E_INVALIDMEDIATYPE;
    if (EvrIsFormatYUV(fourcc)) {
        if (format->videoInfo.VideoFlags & 0x400000)
            return E_INVALIDARG;
    } else if (format->videoInfo.VideoFlags & 0x100000) {
        return E_INVALIDARG;
    }
    return validate_geometry(format);
}

typedef struct {
    double left, top, right, bottom;
} RECTD;

static void cropped(const MFVIDEOINFO *info, double width, double height, RECTD *out)
{
    const MFVideoArea *area = info->VideoFlags & 0x20000 ? &info->PanScanAperture : &info->GeometricAperture;
    out->left = offset_of(area->OffsetX);
    out->top = offset_of(area->OffsetY);
    out->right = area->Area.cx + out->left;
    out->bottom = area->Area.cy + out->top;
    double w = out->right - out->left, h = out->bottom - out->top;
    if (w <= 0 || w > width || h <= 0 || h > height) {
        out->left = out->top = 0;
        out->right = width;
        out->bottom = height;
    }
}

/* the picture of a stream in square pixels, padded to 4:3 or 16:9 when its flags ask. */
static void padded(const MFVIDEOINFO *info, double *w, double *h)
{
    double a, b, d;
    if (info->VideoFlags & 2) {
        d = 9.0;
        a = *w * 9.0;
        b = *h * 16.0;
        if (a > b) {
            *h = a * 0.0625;
            return;
        }
    } else if (info->VideoFlags & 1) {
        d = 3.0;
        a = *w * 3.0;
        b = *h * 4.0;
        if (a > b) {
            *h = a * 0.25;
            return;
        }
    } else {
        return;
    }
    if (b > a)
        *w = b / d;
}

/* evr's GetReferenceSourceRectFromVF: the size a stream takes in the pixels of the reference stream. */
void EvrReferenceSourceRect(const MFVIDEOFORMAT *stream, const MFVIDEOFORMAT *reference, RECT *out)
{
    memset(out, 0, sizeof(*out));
    double height = (int)stream->videoInfo.dwHeight;
    if (stream->videoInfo.InterlaceMode - 5 <= 1)
        height += height;
    RECTD crop;
    cropped(&stream->videoInfo, (int)stream->videoInfo.dwWidth, height, &crop);
    double w = crop.right - crop.left, h = crop.bottom - crop.top;
    if (w <= 0 || h <= 0)
        return;
    const MFRatio *par = &stream->videoInfo.PixelAspectRatio;
    if (par->Numerator < par->Denominator)
        h = (double)par->Denominator * h / (int)par->Numerator;
    else if (par->Numerator > par->Denominator)
        w = (double)(int)par->Numerator * w / par->Denominator;
    padded(&stream->videoInfo, &w, &h);
    par = &reference->videoInfo.PixelAspectRatio;
    if (par->Numerator < par->Denominator)
        h = (double)(int)par->Numerator * h / par->Denominator;
    else if (par->Numerator > par->Denominator)
        w = (double)par->Denominator * w / (int)par->Numerator;
    out->right = (int)(w + 0.5);
    out->bottom = (int)(h + 0.5);
}

/*
 * evr's GetSourceAndDestinationRectFromVF: the part of a stream that is shown and where it lands in the output,
 * from the stream's apertures and pixel shape, the output picture, the zoom of the mixer and the stream's rectangle.
 */
void EvrSourceAndDestRects(const MFVIDEOFORMAT *in, const MFVIDEOFORMAT *out, const MFVIDEOFORMAT *presentation,
                           const MFVideoNormalizedRect *zoom, const MFVideoNormalizedRect *rect, RECT *src, RECT *dst)
{
    memset(src, 0, sizeof(*src));
    memset(dst, 0, sizeof(*dst));
    const MFVIDEOINFO *info = &in->videoInfo;
    double height = (int)info->dwHeight;
    if (info->InterlaceMode - 5 <= 1)
        height += height;
    const MFVideoArea *area = info->VideoFlags & 0x20000 ? &info->PanScanAperture : &info->GeometricAperture;
    double sx = offset_of(area->OffsetX), sy = offset_of(area->OffsetY);
    double sr = area->Area.cx + sx, sb = area->Area.cy + sy;
    if (sr - sx <= 0 || sr - sx > (int)info->dwWidth || sb - sy <= 0 || sb - sy > height) {
        sx = sy = 0;
        sr = (int)info->dwWidth;
        sb = height;
    }
    double sw = sr - sx, sh = sb - sy;
    if (sw <= 0 || sh <= 0)
        return;
    double pw = sw, ph = sh;
    if (info->PixelAspectRatio.Numerator < info->PixelAspectRatio.Denominator)
        ph = (int)info->PixelAspectRatio.Denominator * sh / (int)info->PixelAspectRatio.Numerator;
    else if (info->PixelAspectRatio.Numerator > info->PixelAspectRatio.Denominator)
        pw = (int)info->PixelAspectRatio.Numerator * sw / (int)info->PixelAspectRatio.Denominator;
    double w1 = pw, h1 = ph;
    padded(info, &w1, &h1);

    double px = (int)presentation->videoInfo.dwWidth - 0.0;
    if (px <= 0)
        return;
    double py = (int)presentation->videoInfo.dwHeight - 0.0;
    if (py <= 0)
        return;
    const MFRatio *ppar = &presentation->videoInfo.PixelAspectRatio;
    if (ppar->Numerator < ppar->Denominator)
        py = py * (int)ppar->Denominator / (int)ppar->Numerator;
    else if (ppar->Numerator > ppar->Denominator)
        px = px * (int)ppar->Numerator / (int)ppar->Denominator;
    double full_w = px, full_h = py;
    double l = 0, t = 0, r = px, b = py;
    if (h1 * px > w1 * py) {
        double fit = w1 * py / h1;
        l = (full_w - fit) * 0.5;
        r = l + fit;
    } else if (w1 * py > h1 * px) {
        double fit = h1 * px / w1;
        t = (full_h - fit) * 0.5;
        b = t + fit;
    }
    if (ph * (r - l) > pw * (b - t)) {
        double fit = pw * (b - t) / ph;
        l = (r - l - fit) * 0.5 + l;
        r = fit + l;
    } else if (pw * (b - t) > ph * (r - l)) {
        double fit = ph * (r - l) / pw;
        t = (b - t - fit) * 0.5 + t;
        b = t + fit;
    }
    double ow = full_w, oh = full_h;
    const MFRatio *opar = &out->videoInfo.PixelAspectRatio;
    if (opar->Numerator < opar->Denominator)
        oh = (int)opar->Numerator * full_h / (int)opar->Denominator;
    else if (opar->Numerator > opar->Denominator)
        ow = (int)opar->Denominator * full_w / (int)opar->Numerator;
    const MFVideoArea *g = &out->videoInfo.GeometricAperture;
    double gx = offset_of(g->OffsetX), gy = offset_of(g->OffsetY);
    double gr = g->Area.cx + gx, gb = g->Area.cy + gy;
    double gw = gr - gx;
    if (gw <= 0)
        return;
    double gh = gb - gy;
    if (gh <= 0)
        return;
    double fx = gx, fy = gy, fr = gr, fb = gb;
    if (gw * oh > gh * ow) {
        double fit = gh * ow / oh;
        fx = (gw - fit) * 0.5 + gx;
        fr = fit + fx;
    } else if (gh * ow > gw * oh) {
        double fit = gw * oh / ow;
        fy = (gh - fit) * 0.5 + gy;
        fb = fit + fy;
    }
    double kx = (fr - fx) / full_w, ky = (fb - fy) / full_h;
    double ax = l * kx + fx, ay = t * ky + fy;
    double aw = (r - l) * kx + ax - ax, ah = (b - t) * ky + ay - ay;
    double dl = rect->left * aw + ax, dr = rect->right * aw + ax;
    double dt = rect->top * ah + ay, db = rect->bottom * ah + ay;
    if (dr - dl <= 0 || db - dt <= 0)
        return;
    double zl = zoom->left * (fr - fx) + fx, zr = zoom->right * (fr - fx) + fx;
    double zt = zoom->top * (fb - fy) + fy, zb = zoom->bottom * (fb - fy) + fy;
    if (zr - zl <= 0 || zb - zt <= 0)
        return;
    double il = fmax(dl, zl), ir = fmin(dr, zr), it = fmax(dt, zt), ib = fmin(db, zb);
    if (ir - il <= 0 || ib - it <= 0)
        return;
    double s_l = (il - dl) / (dr - dl) * sw + sx, s_r = (ir - dl) / (dr - dl) * sw + sx;
    double s_t = (it - dt) / (db - dt) * sh + sy, s_b = (ib - dt) / (db - dt) * sh + sy;
    if (s_r - s_l <= 0 || s_b - s_t <= 0)
        return;
    double zw = zr - zl, zh = zb - zt;
    double ox = gx, oy = gy, orr, ob = gb;
    if (gw * zh > gh * zw) {
        double fit = gh * zw / zh;
        ox = (gw - fit) * 0.5 + gx;
        orr = ox + fit;
    } else {
        if (gh * zw > gw * zh) {
            double fit = gw * zh / zw;
            oy = (gh - fit) * 0.5 + gy;
            ob = oy + fit;
        }
        orr = g->Area.cx + ox;
    }
    double mx = (orr - ox) / zw, my = (ob - oy) / zh;
    double d_l = (il - zl) * mx + ox, d_t = (it - zt) * my + oy;
    src->left = (int)(s_l + 0.5);
    src->top = (int)(s_t + 0.5);
    src->right = (int)(s_r + 0.5);
    src->bottom = (int)(s_b + 0.5);
    dst->left = (int)(d_l + 0.5);
    dst->top = (int)(d_t + 0.5);
    dst->right = (int)((ir - il) * mx + d_l + 0.5);
    dst->bottom = (int)((ib - it) * my + d_t + 0.5);
}
