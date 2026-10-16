#include <mime/ntuser.h>
#include <mime/win32/winbase.h>

WINGDIAPI HRGN WINAPI CreatePolygonRgn(const POINT *points, int count, int mode)
{
    if (count < 0 || (count && !points) || (mode != ALTERNATE && mode != WINDING)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (count < 3)
        return CreateRectRgn(0, 0, 0, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return nullptr;
    HRGN region = nullptr;
    SetPolyFillMode(dc, mode);
    if (BeginPath(dc) && Polygon(dc, points, count) && EndPath(dc))
        region = PathToRegion(dc);
    DeleteDC(dc);
    return region;
}

/* an ellipse is a round rectangle whose corners span the whole box, as on windows. */
WINGDIAPI HRGN WINAPI CreateEllipticRgn(int left, int top, int right, int bottom)
{
    int width = right > left ? right - left : left - right;
    int height = bottom > top ? bottom - top : top - bottom;
    return CreateRoundRectRgn(left, top, right, bottom, width, height);
}
