/*
 * msimg32: the gdi extension dll. its three blending entry points are the
 * gdi32 Gdi* implementations, as on windows.
 */
#define MIME_BUILD_MSIMG32
#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/wingdi.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

MSIMG32API BOOL WINAPI AlphaBlend(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                  int src_width, int src_height, BLENDFUNCTION blend)
{
    return GdiAlphaBlend(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, blend);
}

MSIMG32API BOOL WINAPI TransparentBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                      int src_width, int src_height, UINT transparent)
{
    return GdiTransparentBlt(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, transparent);
}

MSIMG32API BOOL WINAPI GradientFill(HDC dc, PTRIVERTEX vertices, ULONG vertex_count, PVOID mesh, ULONG mesh_count,
                                    ULONG mode)
{
    return GdiGradientFill(dc, vertices, vertex_count, mesh, mesh_count, mode);
}

/* a windows 2000 era hook that does nothing on current windows. */
DECLSPEC_EXPORT void WINAPI vSetDdrawflag(void)
{
}
