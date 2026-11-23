/*
 * the questions of gdi32full about the font of a device context that its text calls ask before they draw, as the
 * genuine gdi32full of 26300.9457 makes them.
 */
#include "win32k.h"

/* the widths the font may reach over its cell, none for the fonts of mime, which draw within their advance. */
BOOL NTAPI NtGdiGetCharWidthInfo(HDC hdc, PVOID user_info)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    BOOLEAN font = dc && DcGetFont(dc);
    UserLeave();
    if (!font)
        return FALSE;
    /* CHWIDTHINFO is three longs: the edit control of user32 keeps it right under its stack cookie. */
    LONG info[3] = {0, 0, 0};
    return NT_SUCCESS(MimeCopyToUser(user_info, info, sizeof(info)));
}
