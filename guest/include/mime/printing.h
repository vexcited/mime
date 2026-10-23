/* the "MIME BITMAP 1.0" print data type: raster pages that gdi32 draws and the print processor encodes. */
#ifndef MIME_PRINTING_H
#define MIME_PRINTING_H

#include <mime/win32/winspool.h>

#define MIME_DATATYPE_BITMAP L"MIME BITMAP 1.0"
#define MIME_PRINT_PAGE_MAGIC 0x4D504731

/* starts each page; BGRA rows follow. */
typedef struct _MIME_PRINT_PAGE {
    DWORD Magic;
    DWORD Width, Height;
    DWORD XDpi, YDpi;
    /* 32 bits per pixel, rows top first. */
    DWORD Stride;
    DWORD Reserved;
} MIME_PRINT_PAGE;

#endif
