/*
 * the memory device context pictures are drawn through, shared by the process and used by one thread at a time, and
 * the conversions between pixels and himetric.
 */
#include "picture.h"

static SRWLOCK pic_lock = SRWLOCK_INIT;
typedef struct {
    HDC dc;
    HBITMAP stock;
} PIC_SYSTEM;

static PIC_SYSTEM *pic_system;
static int pic_dpi_x, pic_dpi_y;

void PicLock(void)
{
    AcquireSRWLockExclusive(&pic_lock);
}

void PicUnlock(void)
{
    ReleaseSRWLockExclusive(&pic_lock);
}

/* the context and a one by one bitmap of ours that stands in for what a new context starts with. */
static PIC_SYSTEM *pic_get_system(void)
{
    PIC_SYSTEM *system = pic_system;
    if (system)
        return system;
    system = AutAllocZero(sizeof(PIC_SYSTEM));
    if (!system)
        return nullptr;
    system->dc = CreateCompatibleDC(nullptr);
    system->stock = CreateBitmap(1, 1, 1, 1, nullptr);
    if (!system->dc || !system->stock) {
        if (system->dc)
            DeleteDC(system->dc);
        if (system->stock)
            DeleteObject(system->stock);
        AutFree(system);
        return nullptr;
    }
    SelectObject(system->dc, system->stock);
    PIC_SYSTEM *previous = InterlockedCompareExchangePointer((PVOID *)&pic_system, system, nullptr);
    if (previous) {
        DeleteDC(system->dc);
        DeleteObject(system->stock);
        AutFree(system);
        return previous;
    }
    return system;
}

HDC PicDc(void)
{
    PIC_SYSTEM *system = pic_get_system();
    return system ? system->dc : nullptr;
}

/* the bitmap to put back into the shared context when a picture's bitmap has to leave it. */
HBITMAP PicStock(void)
{
    PIC_SYSTEM *system = pic_get_system();
    return system ? system->stock : nullptr;
}

static void pic_dpi(void)
{
    HDC dc = PicDc();
    if (!dc)
        return;
    pic_dpi_x = GetDeviceCaps(dc, LOGPIXELSX);
    pic_dpi_y = GetDeviceCaps(dc, LOGPIXELSY);
}

int PicDpiX(void)
{
    if (!pic_dpi_x)
        pic_dpi();
    return pic_dpi_x;
}

int PicDpiY(void)
{
    if (!pic_dpi_y)
        pic_dpi();
    return pic_dpi_y;
}

/* pixels to hundredths of a millimeter, rounded to the nearest. */
HRESULT PicHimetric(SIZE *size)
{
    int dpi_x = PicDpiX(), dpi_y = PicDpiY();
    if (!dpi_x || !dpi_y)
        return DISP_E_DIVBYZERO;
    size->cx = (LONG)AutRoundDiv64((LONGLONG)2540 * size->cx, dpi_x);
    size->cy = (LONG)AutRoundDiv64((LONGLONG)2540 * size->cy, dpi_y);
    return S_OK;
}

/* the rectangle a picture of this size covers in himetric, with its top at the height and its bottom at zero. */
void PicRectToHimetric(const SIZE *pixels, RECT *himetric)
{
    int dpi_x = PicDpiX(), dpi_y = PicDpiY();
    if (!dpi_x || !dpi_y) {
        dpi_x = dpi_y = 96;
    }
    himetric->left = himetric->bottom = 0;
    himetric->right = (LONG)AutRoundDiv64((LONGLONG)2540 * pixels->cx, dpi_x);
    himetric->top = (LONG)AutRoundDiv64((LONGLONG)2540 * pixels->cy, dpi_y);
}
