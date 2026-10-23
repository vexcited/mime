/* shared by the picture objects: the image, the format loaders, the drawing dc, drawing and saving. */
#ifndef MIME_OLEAUT32_PICTURE_H
#define MIME_OLEAUT32_PICTURE_H

#include "olestd.h"
#include "picdecode.h"

/* never loaded, behaves as PICTYPE_NONE. */
#define PIC_UNSET PICTYPE_UNINITIALIZED

/* placeable metafile key, as a 32-bit value and as its first word. */
#define PLACEABLE_KEY 0x9AC6CDD7
#define PLACEABLE_HEADER 22

/* OLE stream marker: "lt", two more bytes, then the length of what follows. */
#define STREAM_MARK 0x746C
#define STREAM_HEADER_SIZE 8

/* one inch in twips and in himetric units, the units of metafiles and of pictures. */
#define TWIPS_PER_INCH 1440
#define HIMETRIC_PER_INCH 2540

typedef struct PIC_IMAGE {
    SHORT Kind;
    /* palette and transparency mask are optional. */
    HBITMAP Bitmap;
    HPALETTE Palette;
    HBITMAP Mask;
    /* the dc a program selected the bitmap into, and what it replaced there. */
    HDC Home;
    HBITMAP Replaced;
    /* WmfCx and WmfCy are in twips. */
    HMETAFILE Wmf;
    int WmfCx, WmfCy;
    /* the icon is drawn from IconAnd and IconXor. */
    HICON Icon;
    HBITMAP IconAnd, IconXor;
    HENHMETAFILE Emf;
    /* pixels when the kind has them, himetric always. */
    SIZE Pixels, Himetric;
} PIC_IMAGE;

/* picdc.c. */
void PicLock(void);
void PicUnlock(void);
HDC PicDc(void);
HBITMAP PicBlank(void);
int PicDpiX(void);
int PicDpiY(void);
HRESULT PicPixelsToHimetric(const SIZE *pixels, SIZE *himetric);
HRESULT PicMeasure(PIC_IMAGE *image);

/* picstream.c. */
HRESULT PicReadStream(IStream *stream, ULONG length, BOOL keep, DWORD cx, DWORD cy, DWORD flags, PIC_IMAGE *image,
                      BYTE **original, ULONG *original_size);

/* format loaders, each given the whole file. */
HRESULT PicFromBitmapFile(const BYTE *data, ULONG got, ULONG length, PIC_IMAGE *image);
HRESULT PicFromDecoded(const AUT_IMAGE *decoded, PIC_IMAGE *image);
HRESULT PicFromIconFile(const BYTE *data, ULONG size, DWORD cx, DWORD cy, DWORD flags, PIC_IMAGE *image);
HRESULT PicFromMetafile(const BYTE *data, ULONG size, PIC_IMAGE *image);
HRESULT PicFromPlaceable(const BYTE *data, ULONG size, PIC_IMAGE *image);
HRESULT PicIconPlanes(HICON icon, HBITMAP *mask, HBITMAP *colors);

/* picdraw.c. */
HRESULT PicTakeOut(PIC_IMAGE *image);
HRESULT PicRender(PIC_IMAGE *image, HDC dc, LONG x, LONG y, LONG cx, LONG cy, LONG xs, LONG ys, LONG cxs, LONG cys,
                  const RECT *bounds);
HRESULT PicSelect(PIC_IMAGE *image, HDC in, HDC *out, HBITMAP *displaced);

/* picsave.c. */
HRESULT PicSerialize(PIC_IMAGE *image, BYTE **bytes, ULONG *size);

/* GDI objects are deleted only when the picture owns them. */
void PicImageFree(PIC_IMAGE *image, BOOL own_handles, BOOL own_palette);

#endif
