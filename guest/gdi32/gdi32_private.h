/*
 * gdi32 internals shared between its files.
 */
#pragma once
#include <mime/nt.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>

/* palettes live in gdi32: win32k drives a true color display only. */
HPALETTE PalDefault(void);
BOOL PalIsPalette(HGDIOBJ handle);
BOOL PalDelete(HPALETTE palette);
int PalGetObject(HPALETTE palette, int size, LPVOID buffer);
HPALETTE PalCurrent(HDC dc);
void PalForgetDC(HDC dc);
