/*
 * windowscodecs.dll: the imaging factory and the objects it makes, the codecs of image formats, the converters,
 * scalers and the metadata. The objects share the layout below: a counted base, and one slot for each interface.
 */
#ifndef WIC_PRIVATE_H
#define WIC_PRIVATE_H

#define MIME_BUILD_WINDOWSCODECS
#include <mime/win32/winbase.h>
#include <mime/win32/wincodec.h>
#include <mime/stub.h>
#include <alloc.h>

#define VTBL_FN(f) ((void *)(f))
#define WIC_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------------ memory */

void *WicAlloc(SIZE_T size);
void *WicAllocZero(SIZE_T size);
void *WicRealloc(void *p, SIZE_T size);
void WicFree(void *p);
/* the allocator the codec libraries take. */
extern const MimeAllocator WicAllocator;
WCHAR *WicStrDup(const WCHAR *s);
/* a size times a count that fits 32 bits, or FALSE. */
BOOL WicMul32(UINT a, UINT b, UINT *result);

/* ------------------------------------------------------------------ objects */

/*
 * every interface of an object is a slot: the table pointer, then the distance back to the base. The three
 * methods of IUnknown are the same for all of them.
 */
typedef struct WicSlot {
    const void *lpVtbl;
    INT delta;
} WicSlot;

typedef struct WicIfaceEntry {
    const GUID *iid;
    INT offset; /* from the base to the slot. */
} WicIfaceEntry;

typedef struct WicBase WicBase;

typedef struct WicClass {
    void (*destroy)(WicBase *base);
    const WicIfaceEntry *ifaces;
    UINT iface_count;
} WicClass;

struct WicBase {
    LONG refs;
    const WicClass *cls;
};

/* the base of a slot. */
static inline WicBase *WicSlotBase(void *slot) { return (WicBase *)((char *)slot - ((WicSlot *)slot)->delta); }

HRESULT STDMETHODCALLTYPE WicQueryInterface(void *This, REFIID riid, void **object);
ULONG STDMETHODCALLTYPE WicAddRef(void *This);
ULONG STDMETHODCALLTYPE WicRelease(void *This);

#define WIC_IUNKNOWN_FUNCS VTBL_FN(WicQueryInterface), VTBL_FN(WicAddRef), VTBL_FN(WicRelease)

/* sets the slot member of the object at its table, the base member being named base. */
#define WIC_SLOT(obj, member, vtbl)                                                                                    \
    ((obj)->member.lpVtbl = (vtbl), (obj)->member.delta = (INT)(__builtin_offsetof(__typeof__(*(obj)), member) -       \
                                                               __builtin_offsetof(__typeof__(*(obj)), base)))
#define WIC_ENTRY(type, iid, member) {&(iid), (INT)(__builtin_offsetof(type, member) - __builtin_offsetof(type, base))}

/* the object around a slot of its type. */
#define WIC_FROM_SLOT(type, member, slot) ((type *)((char *)(slot) - __builtin_offsetof(type, member)))

/* makes an object of size bytes, zeroed, with one reference. */
void *WicNew(SIZE_T size, const WicClass *cls);
/* the interface of an object, referenced, or an error. */
HRESULT WicQueryBase(WicBase *base, REFIID riid, void **object);

/* ------------------------------------------------------------------ hresults */

#define WIC_E_INVALIDARG E_INVALIDARG
#define WIC_E_POINTER E_POINTER

/* ------------------------------------------------------------------ pixel formats */

enum {
    WIC_KIND_INDEXED,
    WIC_KIND_GRAY,
    WIC_KIND_RGB,
    WIC_KIND_CMYK,
    WIC_KIND_OTHER,
};

typedef struct WicFormat {
    const GUID *guid;
    UINT bpp;
    UINT channels;
    BOOL alpha;
    UINT numeric; /* WICPixelFormatNumericRepresentation. */
    UINT mask_size;
    const BYTE *masks;
    const WCHAR *name;
} WicFormat;

const WicFormat *WicFindFormat(const GUID *guid);
UINT WicFormatCount(void);
const WicFormat *WicFormatAt(UINT index);
/* the bytes of a row of the width. */
BOOL WicRowBytes(const GUID *format, UINT width, UINT *bytes);
UINT WicFormatBits(const GUID *format);
BOOL WicFormatIsIndexed(const GUID *format);
BOOL WicFormatHasAlpha(const GUID *format);
UINT WicFormatPaletteBits(const GUID *format);

/* ------------------------------------------------------------------ palettes */

HRESULT WicPaletteCreate(IWICPalette **palette);
HRESULT WicPaletteSet(IWICPalette *palette, const WICColor *colors, UINT count);
/* the colors of the palette, in a new array, or NULL with count 0. */
HRESULT WicPaletteGet(IWICPalette *palette, WICColor **colors, UINT *count);
/* fills the colors of a predefined palette, returns their count or 0. */
UINT WicPredefinedPalette(WICBitmapPaletteType type, BOOL transparent, WICColor *colors);
/* a palette of the colors of the source, by median cut. */
HRESULT WicPaletteFromBitmap(IWICBitmapSource *source, UINT count, BOOL transparent, WICColor *colors, UINT *used);

/* ------------------------------------------------------------------ bitmaps and sources */

/* the rectangle or the whole image, checked. */
HRESULT WicCheckRect(const WICRect *rect, UINT width, UINT height, WICRect *out);
/* checks the stride and the size of a buffer for the rectangle. */
HRESULT WicCheckBuffer(const WICRect *rect, const GUID *format, UINT stride, UINT size, BYTE *buffer);
/* copies from rows in memory, whose top row is the first byte of a rectangle's left edge at bit x. */
void WicCopyBits(BYTE *dest, UINT dest_stride, const BYTE *src, UINT src_stride, UINT x, UINT y, UINT width,
                 UINT height, UINT bpp);

HRESULT WicBitmapCreate(UINT width, UINT height, const GUID *format, BYTE *memory, UINT stride, UINT size,
                        BOOL copy, IWICBitmap **bitmap);
HRESULT WicBitmapFromSource(IWICBitmapSource *source, const WICRect *rect, IWICBitmap **bitmap);
/* the pixels of the bitmap, which stay valid as long as it lives. */
BYTE *WicBitmapPixels(IWICBitmap *bitmap, UINT *stride);

/* ------------------------------------------------------------------ pixel conversion */

typedef void (*WicRowConvert)(BYTE *dest, const BYTE *src, UINT count);
/* the conversion of rows, or NULL when the formats have none. */
BOOL WicCanConvert(const GUID *from, const GUID *to);
/* converts a block of rows. The palette is that of an indexed source, a destination palette is applied by the caller. */
HRESULT WicConvertRows(const GUID *from, const GUID *to, const BYTE *src, UINT src_stride, BYTE *dest,
                       UINT dest_stride, UINT width, UINT height, const WICColor *palette, UINT palette_count,
                       const WICColor *dest_palette, UINT dest_count, WICBitmapDitherType dither, UINT first_col,
                       UINT first_row, double threshold);
HRESULT WicFormatConverterCreate(IWICFormatConverter **converter);
HRESULT WicScalerCreate(IWICBitmapScaler **scaler);
HRESULT WicClipperCreate(IWICBitmapClipper **clipper);
HRESULT WicFlipRotatorCreate(IWICBitmapFlipRotator **rotator);
HRESULT WicColorContextCreate(IWICColorContext **context);
HRESULT WicColorTransformCreate(IWICColorTransform **transform);
HRESULT WicStreamCreate(IWICStream **stream);
/* the converter to the format when needed, the source itself when it already has it. */
HRESULT WicEnsureFormat(IWICBitmapSource *source, const GUID *format, IWICBitmapSource **result);

/* ------------------------------------------------------------------ streams */

/* the whole stream from its current position, in a new buffer. */
HRESULT WicReadStream(IStream *stream, BYTE **data, SIZE_T *size, SIZE_T limit);
HRESULT WicWriteStream(IStream *stream, const void *data, SIZE_T size);
/* an IStream that reads and writes a block of memory, as a new reference. */
HRESULT WicMemoryStreamCreate(const BYTE *data, SIZE_T size, BOOL copy, IStream **stream);

/* ------------------------------------------------------------------ components */

typedef struct WicCodecDesc WicCodecDesc;
typedef struct WicDecoderBackend WicDecoderBackend;
typedef struct WicEncoderBackend WicEncoderBackend;

/* ------------------------------------------------------------------ guids */

BOOL WicIsEqual(const GUID *a, const GUID *b);

#endif
