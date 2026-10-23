/* shared windowscodecs objects: a reference-counted base and one slot per interface. */
#ifndef WIC_PRIVATE_H
#define WIC_PRIVATE_H

#define MIME_BUILD_WINDOWSCODECS
#include <mime/win32/winbase.h>
#include <mime/win32/wincodec.h>
#include <mime/stub.h>
#include <alloc.h>

#define VTBL_FN(f) ((void *)(f))
#define WIC_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))


void *WicAlloc(SIZE_T size);
void *WicAllocZero(SIZE_T size);
void *WicRealloc(void *p, SIZE_T size);
void WicFree(void *p);
extern const MimeAllocator WicAllocator;
WCHAR *WicStrDup(const WCHAR *s);
/* multiply two values, failing if the result exceeds 32 bits. */
BOOL WicMul32(UINT a, UINT b, UINT *result);


/* interface slots hold a vtable and offset back to the base. all slots share the three IUnknown methods. */
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

static inline WicBase *WicSlotBase(void *slot) { return (WicBase *)((char *)slot - ((WicSlot *)slot)->delta); }

HRESULT STDMETHODCALLTYPE WicQueryInterface(void *This, REFIID riid, void **object);
ULONG STDMETHODCALLTYPE WicAddRef(void *This);
ULONG STDMETHODCALLTYPE WicRelease(void *This);

#define WIC_IUNKNOWN_FUNCS VTBL_FN(WicQueryInterface), VTBL_FN(WicAddRef), VTBL_FN(WicRelease)

/* initialize the interface slot; the object must name its base member base. */
#define WIC_SLOT(obj, member, vtbl)                                                                                    \
    ((obj)->member.lpVtbl = (vtbl), (obj)->member.delta = (INT)(__builtin_offsetof(__typeof__(*(obj)), member) -       \
                                                               __builtin_offsetof(__typeof__(*(obj)), base)))
#define WIC_ENTRY(type, iid, member) {&(iid), (INT)(__builtin_offsetof(type, member) - __builtin_offsetof(type, base))}

#define WIC_FROM_SLOT(type, member, slot) ((type *)((char *)(slot) - __builtin_offsetof(type, member)))

/* allocate a zeroed object with one reference. */
void *WicNew(SIZE_T size, const WicClass *cls);
/* return a referenced interface or an error. */
HRESULT WicQueryBase(WicBase *base, REFIID riid, void **object);


#define WIC_E_INVALIDARG E_INVALIDARG
#define WIC_E_POINTER E_POINTER


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
BOOL WicRowBytes(const GUID *format, UINT width, UINT *bytes);
UINT WicFormatBits(const GUID *format);
BOOL WicFormatIsIndexed(const GUID *format);
BOOL WicFormatHasAlpha(const GUID *format);
UINT WicFormatPaletteBits(const GUID *format);


HRESULT WicPaletteCreate(IWICPalette **palette);
HRESULT WicPaletteSet(IWICPalette *palette, const WICColor *colors, UINT count);
/* copy palette colors into a new array, or return NULL with count 0. */
HRESULT WicPaletteGet(IWICPalette *palette, WICColor **colors, UINT *count);
/* fills the colors of a predefined palette, returns their count or 0. */
UINT WicPredefinedPalette(WICBitmapPaletteType type, BOOL transparent, WICColor *colors);
/* a palette of the colors of the source, by median cut. */
HRESULT WicPaletteFromBitmap(IWICBitmapSource *source, UINT count, BOOL transparent, WICColor *colors, UINT *used);


/* validate the supplied rectangle, or use the whole image. */
HRESULT WicCheckRect(const WICRect *rect, UINT width, UINT height, WICRect *out);
HRESULT WicCheckBuffer(const WICRect *rect, const GUID *format, UINT stride, UINT size, BYTE *buffer);
/* copy rows from memory; the source starts at the top row and bit x marks the left edge. */
void WicCopyBits(BYTE *dest, UINT dest_stride, const BYTE *src, UINT src_stride, UINT x, UINT y, UINT width,
                 UINT height, UINT bpp);

HRESULT WicBitmapCreate(UINT width, UINT height, const GUID *format, BYTE *memory, UINT stride, UINT size,
                        BOOL copy, IWICBitmap **bitmap);
HRESULT WicBitmapFromSource(IWICBitmapSource *source, const WICRect *rect, IWICBitmap **bitmap);
/* bitmap pixels remain valid for the bitmap lifetime. */
BYTE *WicBitmapPixels(IWICBitmap *bitmap, UINT *stride);


typedef void (*WicRowConvert)(BYTE *dest, const BYTE *src, UINT count);
/* return the row converter, or NULL for unsupported format pairs. */
BOOL WicCanConvert(const GUID *from, const GUID *to);
/* convert rows using the source palette for indexed input; the caller applies any destination palette. */
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
/* reuse sources already in the requested format; otherwise wrap a converter. */
HRESULT WicEnsureFormat(IWICBitmapSource *source, const GUID *format, IWICBitmapSource **result);


/* read the remaining stream into a new buffer. */
HRESULT WicReadStream(IStream *stream, BYTE **data, SIZE_T *size, SIZE_T limit);
HRESULT WicWriteStream(IStream *stream, const void *data, SIZE_T size);
/* return a new IStream reference over read/write memory. */
HRESULT WicMemoryStreamCreate(const BYTE *data, SIZE_T size, BOOL copy, IStream **stream);


typedef struct WicCodecDesc WicCodecDesc;
typedef struct WicDecoderBackend WicDecoderBackend;
typedef struct WicEncoderBackend WicEncoderBackend;


BOOL WicIsEqual(const GUID *a, const GUID *b);

#endif
