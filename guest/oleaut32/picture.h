/*
 * what the picture objects of oleaut32 share: the state of a picture, loading it from a stream and the memory
 * device context drawing goes through.
 */
#ifndef MIME_OLEAUT32_PICTURE_H
#define MIME_OLEAUT32_PICTURE_H

#include <mime/win32/winuser.h>
#include "oleaut32_private.h"
#include "picdecode.h"

/* the picture type of an object nothing was loaded into. */
#define PIC_UNSET ((UINT)-1)

typedef struct PICSTATE {
    UINT type;
    HBITMAP bitmap;
    HPALETTE palette;
    HBITMAP mask;
    BOOL transparent;
    HMETAFILE wmf;
    int xext, yext;
    HICON icon;
    HBITMAP and_bmp, xor_bmp;
    HENHMETAFILE emf;
    SIZE pixels, himetric;
} PICSTATE;

/* What picsys.c has. */
void PicLock(void);
void PicUnlock(void);
HDC PicDc(void);
HBITMAP PicStock(void);
int PicDpiX(void);
int PicDpiY(void);
HRESULT PicHimetric(SIZE *size);
void PicRectToHimetric(const SIZE *pixels, RECT *himetric);

/* What picload.c has. */
HRESULT PicLoad(IStream *stream, ULONG length, BOOL run_mode, DWORD cx, DWORD cy, DWORD flags, PICSTATE *state,
                BYTE **original, ULONG *original_length, BOOL *header_seen);
HRESULT PicIconBitmaps(HICON icon, HBITMAP *and_bmp, HBITMAP *xor_bmp);
HRESULT PicStateSize(PICSTATE *state);
void PicStateFree(PICSTATE *state, BOOL own_gdi, BOOL own_handles);

#endif
