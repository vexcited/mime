/*
 * the static control of the common controls 6, for what it does that the one of user32 does not. measured on windows 11
 * 26300: a 32 bit bitmap with a pixel that is not transparent is kept as a copy with its colors multiplied by its
 * alpha, which STM_GETIMAGE then returns, and the control paints it by filling its client area with the brush of
 * WM_CTLCOLORSTATIC and blending the copy over it. everything else is the control of user32, which this one calls.
 */
#include "comctl32_private.h"

typedef struct STATIC_COPY {
    struct STATIC_COPY *Next;
    HWND Hwnd;
    HBITMAP Bitmap;
} STATIC_COPY;

static SRWLOCK g_lock = SRWLOCK_INIT;
static STATIC_COPY *g_copies;
static WNDPROC g_base;

static HBITMAP copy_of(HWND hwnd)
{
    HBITMAP bitmap = nullptr;
    AcquireSRWLockShared(&g_lock);
    for (STATIC_COPY *c = g_copies; c && !bitmap; c = c->Next)
        if (c->Hwnd == hwnd)
            bitmap = c->Bitmap;
    ReleaseSRWLockShared(&g_lock);
    return bitmap;
}

/* the copy a window holds from now on, nullptr for none. the one it held is returned, and stays the caller's. */
static HBITMAP set_copy(HWND hwnd, HBITMAP bitmap)
{
    HBITMAP old = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    STATIC_COPY **link = &g_copies;
    while (*link && (*link)->Hwnd != hwnd)
        link = &(*link)->Next;
    if (*link) {
        STATIC_COPY *c = *link;
        old = c->Bitmap;
        if (bitmap) {
            c->Bitmap = bitmap;
        } else {
            *link = c->Next;
            CcFree(c);
        }
    } else if (bitmap) {
        STATIC_COPY *c = CcAlloc(sizeof(*c));
        if (c) {
            c->Hwnd = hwnd;
            c->Bitmap = bitmap;
            c->Next = g_copies;
            g_copies = c;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return old;
}

/* the bitmap with its colors multiplied by its alpha, or nullptr when it has no alpha channel in use. */
static HBITMAP premultiplied(HBITMAP bitmap)
{
    BITMAP bm;
    if (!bitmap || !GetObjectW(bitmap, sizeof(bm), &bm) || bm.bmBitsPixel != 32 || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return nullptr;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = bm.bmWidth;
    info.bmiHeader.biHeight = bm.bmHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    DWORD *bits = nullptr;
    HBITMAP copy = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&bits, nullptr, 0) : nullptr;
    BOOL alpha = FALSE;
    if (copy && GetDIBits(dc, bitmap, 0, (UINT)bm.bmHeight, bits, &info, DIB_RGB_COLORS) == bm.bmHeight) {
        SIZE_T count = (SIZE_T)bm.bmWidth * (SIZE_T)bm.bmHeight;
        for (SIZE_T i = 0; i < count && !alpha; i++)
            alpha = bits[i] >> 24 != 0;
        for (SIZE_T i = 0; i < count && alpha; i++) {
            DWORD c = bits[i], a = c >> 24;
            DWORD r = ((c >> 16 & 0xFF) * a + 127) / 255, g = ((c >> 8 & 0xFF) * a + 127) / 255, b = ((c & 0xFF) * a + 127) / 255;
            bits[i] = a << 24 | r << 16 | g << 8 | b;
        }
    }
    if (dc)
        DeleteDC(dc);
    if (copy && !alpha) {
        DeleteObject(copy);
        copy = nullptr;
    }
    return copy;
}

static void paint(HWND hwnd, HDC dc, HBITMAP bitmap)
{
    RECT client;
    BITMAP bm;
    GetClientRect(hwnd, &client);
    HWND parent = GetParent(hwnd);
    HBRUSH brush = (HBRUSH)SendMessageW(parent ? parent : hwnd, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)hwnd);
    if (!brush)
        brush = (HBRUSH)DefWindowProcW(hwnd, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)hwnd);
    FillRect(dc, &client, brush);
    HDC memory = CreateCompatibleDC(dc);
    if (!memory || !GetObjectW(bitmap, sizeof(bm), &bm)) {
        if (memory)
            DeleteDC(memory);
        return;
    }
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    int x = 0, y = 0, width = bm.bmWidth, height = bm.bmHeight;
    if (style & SS_REALSIZECONTROL) {
        width = client.right;
        height = client.bottom;
    } else if (style & SS_CENTERIMAGE) {
        x = (client.right - width) / 2;
        y = (client.bottom - height) / 2;
    }
    HGDIOBJ old = SelectObject(memory, bitmap);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(dc, x, y, width, height, memory, 0, 0, bm.bmWidth, bm.bmHeight, blend);
    SelectObject(memory, old);
    DeleteDC(memory);
}

static BOOL shows_bitmap(HWND hwnd)
{
    return (GetWindowLongW(hwnd, GWL_STYLE) & SS_TYPEMASK) == SS_BITMAP;
}

static LRESULT CALLBACK StaticProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case STM_SETIMAGE: {
        if (wparam != IMAGE_BITMAP || !shows_bitmap(hwnd))
            break;
        HBITMAP copy = premultiplied((HBITMAP)lparam);
        set_copy(hwnd, copy);
        if (!copy)
            break;
        /* the control of user32 draws the image as it takes it, opaque: it is kept from drawing, and this one does. */
        BOOL visible = IsWindowVisible(hwnd);
        if (visible)
            CallWindowProcW(g_base, hwnd, WM_SETREDRAW, FALSE, 0);
        LRESULT previous = CallWindowProcW(g_base, hwnd, message, wparam, (LPARAM)copy);
        if (visible) {
            CallWindowProcW(g_base, hwnd, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
        }
        return previous;
    }
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        HBITMAP copy = copy_of(hwnd);
        if (!copy || !shows_bitmap(hwnd) || (HBITMAP)CallWindowProcW(g_base, hwnd, STM_GETIMAGE, IMAGE_BITMAP, 0) != copy)
            break;
        if (wparam) {
            paint(hwnd, (HDC)wparam, copy);
        } else {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (dc)
                paint(hwnd, dc, copy);
            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    case WM_NCDESTROY: {
        /* the copy is the control's while the control still shows it. */
        HBITMAP copy = set_copy(hwnd, nullptr);
        if (copy && (HBITMAP)CallWindowProcW(g_base, hwnd, STM_GETIMAGE, IMAGE_BITMAP, 0) == copy)
            DeleteObject(copy);
        break;
    }
    }
    return CallWindowProcW(g_base, hwnd, message, wparam, lparam);
}

BOOL RegisterStaticClass(void)
{
    if (!g_base)
        g_base = CcRegisterOverUser(L"Static", StaticProc);
    return g_base != nullptr;
}
