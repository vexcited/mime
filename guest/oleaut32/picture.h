/*
 * what the picture objects of oleaut32 share: the image a picture holds, the loaders of the file formats, the context
 * drawing goes through, and drawing and saving.
 */
#ifndef MIME_OLEAUT32_PICTURE_H
#define MIME_OLEAUT32_PICTURE_H

#include "olestd.h"
#include "picdecode.h"

/* the kind of a picture nothing was loaded into. It acts as PICTYPE_NONE. */
#define PIC_UNSET PICTYPE_UNINITIALIZED

/* the key that opens a placeable metafile, as a 32 bit value and as its first word. */
#define PLACEABLE_KEY 0x9AC6CDD7
#define PLACEABLE_HEADER 22

/* the marker of a picture in a stream of OLE: "lt", two more bytes and the length of what follows. */
#define STREAM_MARK 0x746C
#define STREAM_HEADER_SIZE 8

/* one inch in twips and in himetric units, the units of metafiles and of pictures. */
#define TWIPS_PER_INCH 1440
#define HIMETRIC_PER_INCH 2540

typedef struct PIC_IMAGE {
    SHORT Kind;
    /* a bitmap, perhaps with a palette and a mask for the pixels that show what is behind. */
    HBITMAP Bitmap;
    HPALETTE Palette;
    HBITMAP Mask;
    /* where the bitmap sits when a program has it in a context, and what it replaced there. */
    HDC Home;
    HBITMAP Replaced;
    /* a windows metafile and its size in twips */
    HMETAFILE Wmf;
    int WmfCx, WmfCy;
    /* an icon, drawn from its mask and its colors */
    HICON Icon;
    HBITMAP IconAnd, IconXor;
    HENHMETAFILE Emf;
    /* the size in pixels where there is one, and in himetric units for everything */
    SIZE Pixels, Himetric;
} PIC_IMAGE;

/* picdc.c: the one context pictures are drawn through, and sizes. */
void PicLock(void);
void PicUnlock(void);
HDC PicDc(void);
HBITMAP PicBlank(void);
int PicDpiX(void);
int PicDpiY(void);
HRESULT PicPixelsToHimetric(const SIZE *pixels, SIZE *himetric);
HRESULT PicMeasure(PIC_IMAGE *image);

/* picstream.c: a picture from a stream, in any of the formats. */
HRESULT PicReadStream(IStream *stream, ULONG length, BOOL keep, DWORD cx, DWORD cy, DWORD flags, PIC_IMAGE *image,
                      BYTE **original, ULONG *original_size);

/* the loaders of the formats, each given the whole file. */
HRESULT PicFromBitmapFile(const BYTE *data, ULONG got, ULONG length, PIC_IMAGE *image);
HRESULT PicFromDecoded(const AUT_IMAGE *decoded, PIC_IMAGE *image);
HRESULT PicFromIconFile(const BYTE *data, ULONG size, DWORD cx, DWORD cy, DWORD flags, PIC_IMAGE *image);
HRESULT PicFromMetafile(const BYTE *data, ULONG size, PIC_IMAGE *image);
HRESULT PicFromPlaceable(const BYTE *data, ULONG size, PIC_IMAGE *image);
HRESULT PicIconPlanes(HICON icon, HBITMAP *mask, HBITMAP *colors);

/* picdraw.c */
HRESULT PicTakeOut(PIC_IMAGE *image);
HRESULT PicRender(PIC_IMAGE *image, HDC dc, LONG x, LONG y, LONG cx, LONG cy, LONG xs, LONG ys, LONG cxs, LONG cys,
                  const RECT *bounds);
HRESULT PicSelect(PIC_IMAGE *image, HDC in, HDC *out, HBITMAP *displaced);

/* picsave.c: the picture as the bytes of a file, whatever it was loaded from. */
HRESULT PicSerialize(PIC_IMAGE *image, BYTE **bytes, ULONG *size);

/* the handles of an image go away: the objects only when the picture owns them. */
void PicImageFree(PIC_IMAGE *image, BOOL own_handles, BOOL own_palette);

#endif
