/*
 * what gdi32 and winspool share to print: the page records of the "MIME BITMAP 1.0" data type, a raster
 * page that gdi32 draws and the print processor encodes.
 */
#ifndef MIME_PRINTING_H
#define MIME_PRINTING_H

#include <mime/win32/winspool.h>

#define MIME_DATATYPE_BITMAP L"MIME BITMAP 1.0"
#define MIME_PRINT_PAGE_MAGIC 0x4D504731

/* the header that starts each page written to a printer with the bitmap data type, rows of BGRA follow. */
typedef struct _MIME_PRINT_PAGE {
    DWORD Magic;
    DWORD Width, Height;
    DWORD XDpi, YDpi;
    /* 32 bits for a pixel, a row after the other, top first. */
    DWORD Stride;
    DWORD Reserved;
} MIME_PRINT_PAGE;

#endif
