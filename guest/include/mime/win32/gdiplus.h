/*
 * gdi+ flat api: the C entry points of gdiplus.dll. the C++ wrapper classes
 * of the sdk call these, so programs import only the Gdip names. every
 * function uses the WINAPI convention and returns a GpStatus.
 */
#ifndef MIME_WIN32_GDIPLUS_H
#define MIME_WIN32_GDIPLUS_H

#include <mime/win32/objidl.h>
#include <mime/win32/wingdi.h>

#ifdef MIME_BUILD_GDIPLUS
#define WINGDIPAPI DECLSPEC_EXPORT
#else
#define WINGDIPAPI DECLSPEC_IMPORT
#endif

typedef enum Status {
    Ok = 0,
    GenericError = 1,
    InvalidParameter = 2,
    OutOfMemory = 3,
    ObjectBusy = 4,
    InsufficientBuffer = 5,
    NotImplemented = 6,
    Win32Error = 7,
    WrongState = 8,
    Aborted = 9,
    FileNotFound = 10,
    ValueOverflow = 11,
    AccessDenied = 12,
    UnknownImageFormat = 13,
    FontFamilyNotFound = 14,
    FontStyleNotFound = 15,
    NotTrueTypeFont = 16,
    UnsupportedGdiplusVersion = 17,
    GdiplusNotInitialized = 18,
    PropertyNotFound = 19,
    PropertyNotSupported = 20,
    ProfileNotFound = 21,
} Status;
typedef Status GpStatus;

typedef DWORD ARGB;
typedef INT PixelFormat;

/* pixel format bits: the bits per pixel are in bits 8 to 15. */
#define PixelFormatIndexed 0x00010000
#define PixelFormatGDI 0x00020000
#define PixelFormatAlpha 0x00040000
#define PixelFormatPAlpha 0x00080000
#define PixelFormatExtended 0x00100000
#define PixelFormatCanonical 0x00200000
#define PixelFormatUndefined 0
#define PixelFormatDontCare 0
#define PixelFormat1bppIndexed (1 | (1 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define PixelFormat4bppIndexed (2 | (4 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define PixelFormat8bppIndexed (3 | (8 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define PixelFormat16bppGrayScale (4 | (16 << 8) | PixelFormatExtended)
#define PixelFormat16bppRGB555 (5 | (16 << 8) | PixelFormatGDI)
#define PixelFormat16bppRGB565 (6 | (16 << 8) | PixelFormatGDI)
#define PixelFormat16bppARGB1555 (7 | (16 << 8) | PixelFormatAlpha | PixelFormatGDI)
#define PixelFormat24bppRGB (8 | (24 << 8) | PixelFormatGDI)
#define PixelFormat32bppRGB (9 | (32 << 8) | PixelFormatGDI)
#define PixelFormat32bppARGB (10 | (32 << 8) | PixelFormatAlpha | PixelFormatGDI | PixelFormatCanonical)
#define PixelFormat32bppPARGB (11 | (32 << 8) | PixelFormatAlpha | PixelFormatPAlpha | PixelFormatGDI)
#define PixelFormat48bppRGB (12 | (48 << 8) | PixelFormatExtended)
#define PixelFormat64bppARGB (13 | (64 << 8) | PixelFormatAlpha | PixelFormatCanonical | PixelFormatExtended)
#define PixelFormat64bppPARGB (14 | (64 << 8) | PixelFormatAlpha | PixelFormatPAlpha | PixelFormatExtended)
#define PixelFormat32bppCMYK (15 | (32 << 8))
#define PixelFormatMax 16

#define GetPixelFormatSize(format) (((format) >> 8) & 0xff)
#define IsIndexedPixelFormat(format) (((format) & PixelFormatIndexed) != 0)
#define IsAlphaPixelFormat(format) (((format) & PixelFormatAlpha) != 0)

typedef enum ImageLockMode {
    ImageLockModeRead = 1,
    ImageLockModeWrite = 2,
    ImageLockModeUserInputBuf = 4,
} ImageLockMode;

typedef enum PaletteFlags {
    PaletteFlagsHasAlpha = 1,
    PaletteFlagsGrayScale = 2,
    PaletteFlagsHalftone = 4,
} PaletteFlags;

typedef enum InterpolationMode {
    InterpolationModeInvalid = -1,
    InterpolationModeDefault = 0,
    InterpolationModeLowQuality = 1,
    InterpolationModeHighQuality = 2,
    InterpolationModeBilinear = 3,
    InterpolationModeBicubic = 4,
    InterpolationModeNearestNeighbor = 5,
    InterpolationModeHighQualityBilinear = 6,
    InterpolationModeHighQualityBicubic = 7,
} InterpolationMode;

typedef enum ImageType {
    ImageTypeUnknown = 0,
    ImageTypeBitmap = 1,
    ImageTypeMetafile = 2,
} ImageType;

typedef struct GpRect {
    INT X;
    INT Y;
    INT Width;
    INT Height;
} GpRect;

typedef struct BitmapData {
    UINT Width;
    UINT Height;
    INT Stride;
    PixelFormat PixelFormat;
    void *Scan0;
    UINT_PTR Reserved;
} BitmapData;

typedef struct ColorPalette {
    UINT Flags;
    UINT Count;
    ARGB Entries[1];
} ColorPalette;

typedef void(WINAPI *DebugEventProc)(INT level, CHAR *message);
typedef GpStatus(WINAPI *NotificationHookProc)(ULONG_PTR *token);
typedef void(WINAPI *NotificationUnhookProc)(ULONG_PTR token);

typedef struct GdiplusStartupInput {
    UINT32 GdiplusVersion;
    DebugEventProc DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef struct GdiplusStartupOutput {
    NotificationHookProc NotificationHook;
    NotificationUnhookProc NotificationUnhook;
} GdiplusStartupOutput;

typedef struct GpImage GpImage;
typedef struct GpBitmap GpBitmap;
typedef struct GpGraphics GpGraphics;

WINGDIPAPI GpStatus WINAPI GdiplusStartup(ULONG_PTR *token, const GdiplusStartupInput *input,
                                          GdiplusStartupOutput *output);
WINGDIPAPI void WINAPI GdiplusShutdown(ULONG_PTR token);
WINGDIPAPI void *WINAPI GdipAlloc(SIZE_T size);
WINGDIPAPI void WINAPI GdipFree(void *p);

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromFile(const WCHAR *file, GpBitmap **bitmap);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromFileICM(const WCHAR *file, GpBitmap **bitmap);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromStream(IStream *stream, GpBitmap **bitmap);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromStreamICM(IStream *stream, GpBitmap **bitmap);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromHBITMAP(HBITMAP hbm, HPALETTE palette, GpBitmap **bitmap);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, PixelFormat format,
                                                     BYTE *scan0, GpBitmap **bitmap);
WINGDIPAPI GpStatus WINAPI GdipBitmapLockBits(GpBitmap *bitmap, const GpRect *rect, UINT flags, PixelFormat format,
                                              BitmapData *data);
WINGDIPAPI GpStatus WINAPI GdipBitmapUnlockBits(GpBitmap *bitmap, BitmapData *data);

WINGDIPAPI GpStatus WINAPI GdipGetImageWidth(GpImage *image, UINT *width);
WINGDIPAPI GpStatus WINAPI GdipGetImageHeight(GpImage *image, UINT *height);
WINGDIPAPI GpStatus WINAPI GdipGetImagePixelFormat(GpImage *image, PixelFormat *format);
WINGDIPAPI GpStatus WINAPI GdipGetImagePaletteSize(GpImage *image, INT *size);
WINGDIPAPI GpStatus WINAPI GdipGetImagePalette(GpImage *image, ColorPalette *palette, INT size);
WINGDIPAPI GpStatus WINAPI GdipCloneImage(GpImage *image, GpImage **clone);
WINGDIPAPI GpStatus WINAPI GdipDisposeImage(GpImage *image);
WINGDIPAPI GpStatus WINAPI GdipGetImageGraphicsContext(GpImage *image, GpGraphics **graphics);

WINGDIPAPI GpStatus WINAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
WINGDIPAPI GpStatus WINAPI GdipDeleteGraphics(GpGraphics *graphics);
WINGDIPAPI GpStatus WINAPI GdipSetInterpolationMode(GpGraphics *graphics, InterpolationMode mode);
WINGDIPAPI GpStatus WINAPI GdipDrawImageI(GpGraphics *graphics, GpImage *image, INT x, INT y);
WINGDIPAPI GpStatus WINAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT width,
                                              INT height);

#endif
