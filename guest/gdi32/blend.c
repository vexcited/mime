/*
 * color keyed blits and gradient fills, composed in user mode from 32 bpp dib
 * sections, StretchDIBits and GdiAlphaBlend.
 */
#include "gdi32_private.h"

typedef struct CANVAS {
    HDC Dc;
    HBITMAP Bitmap;
    HGDIOBJ Old;
    DWORD *Bits;
} CANVAS;

/* a top down 32 bpp dib section selected in a memory dc. */
static BOOL canvas_open(CANVAS *c, HDC ref, int width, int height)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    c->Dc = CreateCompatibleDC(ref);
    if (!c->Dc)
        return FALSE;
    void *bits = nullptr;
    c->Bitmap = CreateDIBSection(c->Dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!c->Bitmap || !bits) {
        if (c->Bitmap)
            DeleteObject(c->Bitmap);
        DeleteDC(c->Dc);
        return FALSE;
    }
    c->Bits = bits;
    c->Old = SelectObject(c->Dc, c->Bitmap);
    return TRUE;
}

static void canvas_close(CANVAS *c)
{
    SelectObject(c->Dc, c->Old);
    DeleteObject(c->Bitmap);
    DeleteDC(c->Dc);
}

static const BLENDFUNCTION per_pixel_alpha = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

/* only the key bits that survive the source format count: 15 and 16 bpp keep 5 bits a channel. */
static DWORD key_mask(HDC src)
{
    BITMAP bm;
    HGDIOBJ h = GetCurrentObject(src, OBJ_BITMAP);
    if (h && GetObjectW(h, sizeof(bm), &bm) && (bm.bmBitsPixel == 15 || bm.bmBitsPixel == 16))
        return 0xF8F8F8;
    return 0xFFFFFF;
}

/* stretches the source into a 32 bpp canvas, clears the key color to transparent and blends the rest. */
WINGDIAPI BOOL WINAPI GdiTransparentBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                        int src_width, int src_height, UINT transparent)
{
    if (MfActive(dst))
        return MfBitBlt(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, transparent,
                        EMR_TRANSPARENTBLT);
    if (width < 0 || height < 0 || src_width < 0 || src_height < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!width || !height || !src_width || !src_height)
        return TRUE;
    CANVAS c;
    if (!canvas_open(&c, dst, width, height))
        return StretchBlt(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, SRCCOPY);
    SetStretchBltMode(c.Dc, STRETCH_DELETESCANS);
    SetTextColor(c.Dc, GetTextColor(dst));
    SetBkColor(c.Dc, GetBkColor(dst));
    BOOL ok = StretchBlt(c.Dc, 0, 0, width, height, src, src_x, src_y, src_width, src_height, SRCCOPY);
    if (ok) {
        GdiFlush();
        DWORD mask = key_mask(src);
        DWORD key = ((DWORD)GetRValue(transparent) << 16 | (DWORD)GetGValue(transparent) << 8 |
                     GetBValue(transparent)) & mask;
        SIZE_T n = (SIZE_T)width * (SIZE_T)height;
        for (SIZE_T i = 0; i < n; i++) {
            DWORD p = c.Bits[i] & 0xFFFFFF;
            c.Bits[i] = (p & mask) == key ? 0 : p | 0xFF000000;
        }
        ok = GdiAlphaBlend(dst, x, y, width, height, c.Dc, 0, 0, width, height, per_pixel_alpha);
    }
    canvas_close(&c);
    return ok;
}

#define CHANNEL(v) ((DWORD)((v) >> 8))
#define MAX_EXTENT 16384

static DWORD lerp_pixel(const TRIVERTEX *a, const TRIVERTEX *b, DWORD i, DWORD n)
{
    DWORD r = (CHANNEL(a->Red) * (n - i) + CHANNEL(b->Red) * i) / n;
    DWORD g = (CHANNEL(a->Green) * (n - i) + CHANNEL(b->Green) * i) / n;
    DWORD bl = (CHANNEL(a->Blue) * (n - i) + CHANNEL(b->Blue) * i) / n;
    return r << 16 | g << 8 | bl;
}

/* one row or column of the gradient, stretched over the rectangle. */
static BOOL gradient_rect(HDC dc, const TRIVERTEX *a, const TRIVERTEX *b, BOOL vertical)
{
    if (vertical ? a->y > b->y : a->x > b->x) {
        const TRIVERTEX *t = a;
        a = b;
        b = t;
    }
    LONG left = a->x < b->x ? a->x : b->x, right = a->x < b->x ? b->x : a->x;
    LONG top = a->y < b->y ? a->y : b->y, bottom = a->y < b->y ? b->y : a->y;
    LONG width = right - left, height = bottom - top;
    if (width <= 0 || height <= 0)
        return TRUE;
    DWORD n = (DWORD)(vertical ? height : width);
    if (n > MAX_EXTENT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD *bits = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, n * sizeof(DWORD));
    if (!bits) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    for (DWORD i = 0; i < n; i++)
        bits[i] = lerp_pixel(a, b, i, n);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = vertical ? 1 : (LONG)n;
    bi.bmiHeader.biHeight = vertical ? -(LONG)n : 1;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    BOOL ok = StretchDIBits(dc, left, top, width, height, 0, 0, bi.bmiHeader.biWidth, (int)(vertical ? n : 1), bits,
                            &bi, DIB_RGB_COLORS, SRCCOPY) != 0;
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, bits);
    return ok;
}

/* twice the signed area of a, b, p in doubled coordinates. */
static LONGLONG edge(LONGLONG ax, LONGLONG ay, LONGLONG bx, LONGLONG by, LONGLONG px, LONGLONG py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/* barycentric interpolation at pixel centers, blended through a per pixel alpha canvas. */
static BOOL gradient_triangle(HDC dc, const TRIVERTEX *v0, const TRIVERTEX *v1, const TRIVERTEX *v2)
{
    LONG left = v0->x, right = v0->x, top = v0->y, bottom = v0->y;
    const TRIVERTEX *v[3] = {v0, v1, v2};
    for (int i = 1; i < 3; i++) {
        left = v[i]->x < left ? v[i]->x : left;
        right = v[i]->x > right ? v[i]->x : right;
        top = v[i]->y < top ? v[i]->y : top;
        bottom = v[i]->y > bottom ? v[i]->y : bottom;
    }
    LONG width = right - left, height = bottom - top;
    if (width <= 0 || height <= 0)
        return TRUE;
    if (width > MAX_EXTENT || height > MAX_EXTENT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* doubled coordinates relative to the box, so pixel centers are integers. */
    LONGLONG x[3], y[3];
    for (int i = 0; i < 3; i++) {
        x[i] = 2 * (LONGLONG)(v[i]->x - left);
        y[i] = 2 * (LONGLONG)(v[i]->y - top);
    }
    LONGLONG area = edge(x[0], y[0], x[1], y[1], x[2], y[2]);
    if (!area)
        return TRUE;
    if (area < 0) {
        const TRIVERTEX *t = v[1];
        v[1] = v[2];
        v[2] = t;
        LONGLONG tx = x[1], ty = y[1];
        x[1] = x[2], y[1] = y[2];
        x[2] = tx, y[2] = ty;
        area = -area;
    }
    /* scale the weights down so the sums stay in 32 bits. */
    int shift = 0;
    while ((area >> shift) >= (1 << 22))
        shift++;
    DWORD den = (DWORD)(area >> shift);
    if (!den)
        return TRUE;
    CANVAS c;
    if (!canvas_open(&c, dc, width, height)) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    for (LONG row = 0; row < height; row++) {
        LONGLONG py = 2 * (LONGLONG)row + 1;
        for (LONG col = 0; col < width; col++) {
            LONGLONG px = 2 * (LONGLONG)col + 1;
            LONGLONG w0 = edge(x[1], y[1], x[2], y[2], px, py);
            LONGLONG w1 = edge(x[2], y[2], x[0], y[0], px, py);
            LONGLONG w2 = edge(x[0], y[0], x[1], y[1], px, py);
            DWORD *out = &c.Bits[(SIZE_T)row * (SIZE_T)width + (SIZE_T)col];
            if (w0 < 0 || w1 < 0 || w2 < 0) {
                *out = 0;
                continue;
            }
            DWORD a = (DWORD)(w0 >> shift), b = (DWORD)(w1 >> shift), d = (DWORD)(w2 >> shift);
            DWORD r = (CHANNEL(v[0]->Red) * a + CHANNEL(v[1]->Red) * b + CHANNEL(v[2]->Red) * d) / den;
            DWORD g = (CHANNEL(v[0]->Green) * a + CHANNEL(v[1]->Green) * b + CHANNEL(v[2]->Green) * d) / den;
            DWORD bl = (CHANNEL(v[0]->Blue) * a + CHANNEL(v[1]->Blue) * b + CHANNEL(v[2]->Blue) * d) / den;
            *out = 0xFF000000 | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (bl > 255 ? 255 : bl);
        }
    }
    BOOL ok = GdiAlphaBlend(dc, left, top, width, height, c.Dc, 0, 0, width, height, per_pixel_alpha);
    canvas_close(&c);
    return ok;
}

WINGDIAPI BOOL WINAPI GdiGradientFill(HDC dc, PTRIVERTEX vertices, ULONG vertex_count, PVOID mesh, ULONG mesh_count,
                                      ULONG mode)
{
    if (MfActive(dc))
        return vertices && mesh && MfGradientFill(dc, vertices, vertex_count, mesh, mesh_count, mode);
    if (!vertices || !vertex_count || !mesh || !mesh_count || mode > GRADIENT_FILL_TRIANGLE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (ULONG i = 0; i < mesh_count; i++) {
        BOOL ok;
        if (mode == GRADIENT_FILL_TRIANGLE) {
            const GRADIENT_TRIANGLE *t = (const GRADIENT_TRIANGLE *)mesh + i;
            if (t->Vertex1 >= vertex_count || t->Vertex2 >= vertex_count || t->Vertex3 >= vertex_count) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            ok = gradient_triangle(dc, &vertices[t->Vertex1], &vertices[t->Vertex2], &vertices[t->Vertex3]);
        } else {
            const GRADIENT_RECT *r = (const GRADIENT_RECT *)mesh + i;
            if (r->UpperLeft >= vertex_count || r->LowerRight >= vertex_count) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            ok = gradient_rect(dc, &vertices[r->UpperLeft], &vertices[r->LowerRight], mode == GRADIENT_FILL_RECT_V);
        }
        if (!ok)
            return FALSE;
    }
    return TRUE;
}
