/*
 * the dc all pictures are drawn through, and picture sizes. One screen compatible memory dc serves the process, and
 * its lock draws one picture at a time, since a bitmap can be selected into only one dc.
 */
#include "picture.h"

static SRWLOCK g_draw_lock = SRWLOCK_INIT;
static SRWLOCK g_make_lock = SRWLOCK_INIT;
static HDC g_dc;
static HBITMAP g_blank;
static int g_dpi[2];

void PicLock(void)
{
    AcquireSRWLockExclusive(&g_draw_lock);
}

void PicUnlock(void)
{
    ReleaseSRWLockExclusive(&g_draw_lock);
}

/* created on first use with a 1x1 bitmap selected, standing in for the default one. */
HDC PicDc(void)
{
    AcquireSRWLockExclusive(&g_make_lock);
    if (!g_dc) {
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP blank = dc ? CreateBitmap(1, 1, 1, 1, nullptr) : nullptr;
        if (blank) {
            SelectObject(dc, blank);
            g_dpi[0] = GetDeviceCaps(dc, LOGPIXELSX);
            g_dpi[1] = GetDeviceCaps(dc, LOGPIXELSY);
            g_blank = blank;
            g_dc = dc;
        } else if (dc) {
            DeleteDC(dc);
        }
    }
    HDC dc = g_dc;
    ReleaseSRWLockExclusive(&g_make_lock);
    return dc;
}

HBITMAP PicBlank(void)
{
    return PicDc() ? g_blank : nullptr;
}

int PicDpiX(void)
{
    return PicDc() ? g_dpi[0] : 0;
}

int PicDpiY(void)
{
    return PicDc() ? g_dpi[1] : 0;
}

/* at the display's dpi, rounded to nearest. */
HRESULT PicPixelsToHimetric(const SIZE *pixels, SIZE *himetric)
{
    int dpi_x = PicDpiX(), dpi_y = PicDpiY();
    if (!dpi_x || !dpi_y)
        return DISP_E_DIVBYZERO;
    himetric->cx = (LONG)OleStdRoundDiv((LONGLONG)pixels->cx * HIMETRIC_PER_INCH, dpi_x);
    himetric->cy = (LONG)OleStdRoundDiv((LONGLONG)pixels->cy * HIMETRIC_PER_INCH, dpi_y);
    return S_OK;
}

static HRESULT measure_bitmap(PIC_IMAGE *image)
{
    BITMAP info = {};
    if (image->Bitmap)
        GetObjectW(image->Bitmap, sizeof(info), &info);
    image->Pixels.cx = info.bmWidth;
    image->Pixels.cy = info.bmHeight;
    SIZE stored = {};
    if (image->Bitmap && GetBitmapDimensionEx(image->Bitmap, &stored) && stored.cx) {
        image->Himetric = stored;
        return S_OK;
    }
    return PicPixelsToHimetric(&image->Pixels, &image->Himetric);
}

HRESULT PicMeasure(PIC_IMAGE *image)
{
    switch (image->Kind) {
    case PICTYPE_NONE:
        return S_OK;
    case PICTYPE_BITMAP:
        return measure_bitmap(image);
    case PICTYPE_METAFILE:
        if (image->WmfCx <= 0)
            return E_FAIL;
        image->Himetric.cx = (LONG)OleStdRoundDiv((LONGLONG)image->WmfCx * HIMETRIC_PER_INCH, TWIPS_PER_INCH);
        image->Himetric.cy = (LONG)OleStdRoundDiv((LONGLONG)image->WmfCy * HIMETRIC_PER_INCH, TWIPS_PER_INCH);
        return S_OK;
    case PICTYPE_ICON: {
        BITMAP info = {};
        if (image->IconAnd && GetObjectW(image->IconAnd, sizeof(info), &info)) {
            image->Pixels.cx = info.bmWidth;
            image->Pixels.cy = info.bmHeight;
        } else {
            image->Pixels.cx = GetSystemMetrics(SM_CXICON);
            image->Pixels.cy = GetSystemMetrics(SM_CYICON);
        }
        return PicPixelsToHimetric(&image->Pixels, &image->Himetric);
    }
    case PICTYPE_ENHMETAFILE: {
        ENHMETAHEADER header = {};
        if (!GetEnhMetaFileHeader(image->Emf, sizeof(header), &header))
            return E_FAIL;
        image->Himetric.cx = header.rclFrame.right - header.rclFrame.left;
        image->Himetric.cy = header.rclFrame.bottom - header.rclFrame.top;
        return S_OK;
    }
    default:
        return E_UNEXPECTED;
    }
}

void PicImageFree(PIC_IMAGE *image, BOOL own_handles, BOOL own_palette)
{
    if (image->Home)
        PicTakeOut(image);
    if (own_handles) {
        if (image->Bitmap)
            DeleteObject(image->Bitmap);
        if (image->Wmf)
            DeleteMetaFile(image->Wmf);
        if (image->Icon)
            DestroyIcon(image->Icon);
        if (image->Emf)
            DeleteEnhMetaFile(image->Emf);
        if (image->Palette && own_palette)
            DeleteObject(image->Palette);
    }
    if (image->Mask)
        DeleteObject(image->Mask);
    if (image->IconAnd)
        DeleteObject(image->IconAnd);
    if (image->IconXor)
        DeleteObject(image->IconXor);
    memset(image, 0, sizeof(*image));
    image->Kind = PIC_UNSET;
}
