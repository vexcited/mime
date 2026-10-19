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
typedef struct GpMatrix GpMatrix;
typedef struct GpBrush GpBrush;
typedef struct GpPen GpPen;

typedef struct GpPointF {
    float X;
    float Y;
} GpPointF;

typedef struct GpPoint {
    INT X;
    INT Y;
} GpPoint;

typedef struct GpRectF {
    float X;
    float Y;
    float Width;
    float Height;
} GpRectF;

typedef enum MatrixOrder {
    MatrixOrderPrepend = 0,
    MatrixOrderAppend = 1,
} MatrixOrder;

typedef enum GpUnit {
    UnitWorld = 0,
    UnitDisplay = 1,
    UnitPixel = 2,
    UnitPoint = 3,
    UnitInch = 4,
    UnitDocument = 5,
    UnitMillimeter = 6,
} GpUnit;

typedef enum SmoothingMode {
    SmoothingModeInvalid = -1,
    SmoothingModeDefault = 0,
    SmoothingModeHighSpeed = 1,
    SmoothingModeHighQuality = 2,
    SmoothingModeNone = 3,
    SmoothingModeAntiAlias8x4 = 4,
    SmoothingModeAntiAlias = 4,
    SmoothingModeAntiAlias8x8 = 5,
} SmoothingMode;

typedef enum CompositingMode {
    CompositingModeSourceOver = 0,
    CompositingModeSourceCopy = 1,
} CompositingMode;

typedef enum CompositingQuality {
    CompositingQualityInvalid = -1,
    CompositingQualityDefault = 0,
    CompositingQualityHighSpeed = 1,
    CompositingQualityHighQuality = 2,
    CompositingQualityGammaCorrected = 3,
    CompositingQualityAssumeLinear = 4,
} CompositingQuality;

typedef enum PixelOffsetMode {
    PixelOffsetModeInvalid = -1,
    PixelOffsetModeDefault = 0,
    PixelOffsetModeHighSpeed = 1,
    PixelOffsetModeHighQuality = 2,
    PixelOffsetModeNone = 3,
    PixelOffsetModeHalf = 4,
} PixelOffsetMode;

typedef enum BrushType {
    BrushTypeSolidColor = 0,
    BrushTypeHatchFill = 1,
    BrushTypeTextureFill = 2,
    BrushTypePathGradient = 3,
    BrushTypeLinearGradient = 4,
} BrushType;

typedef enum DashStyle {
    DashStyleSolid = 0,
    DashStyleDash = 1,
    DashStyleDot = 2,
    DashStyleDashDot = 3,
    DashStyleDashDotDot = 4,
    DashStyleCustom = 5,
} DashStyle;

typedef enum RotateFlipType {
    RotateNoneFlipNone = 0,
    Rotate90FlipNone = 1,
    Rotate180FlipNone = 2,
    Rotate270FlipNone = 3,
    RotateNoneFlipX = 4,
    Rotate90FlipX = 5,
    Rotate180FlipX = 6,
    Rotate270FlipX = 7,
} RotateFlipType;

typedef enum ImageFlags {
    ImageFlagsNone = 0,
    ImageFlagsScalable = 0x0001,
    ImageFlagsHasAlpha = 0x0002,
    ImageFlagsHasTranslucent = 0x0004,
    ImageFlagsPartiallyScalable = 0x0008,
    ImageFlagsColorSpaceRGB = 0x0010,
    ImageFlagsColorSpaceCMYK = 0x0020,
    ImageFlagsColorSpaceGRAY = 0x0040,
    ImageFlagsColorSpaceYCBCR = 0x0080,
    ImageFlagsColorSpaceYCCK = 0x0100,
    ImageFlagsHasRealDPI = 0x1000,
    ImageFlagsHasRealPixelSize = 0x2000,
    ImageFlagsReadOnly = 0x00010000,
    ImageFlagsCaching = 0x00020000,
} ImageFlags;

#define ImageCodecFlagsEncoder 0x00000001
#define ImageCodecFlagsDecoder 0x00000002
#define ImageCodecFlagsSupportBitmap 0x00000004
#define ImageCodecFlagsSupportVector 0x00000008
#define ImageCodecFlagsSeekableEncode 0x00000010
#define ImageCodecFlagsBlockingDecode 0x00000020
#define ImageCodecFlagsBuiltin 0x00010000
#define ImageCodecFlagsSystem 0x00020000
#define ImageCodecFlagsUser 0x00040000

typedef struct ImageCodecInfo {
    CLSID Clsid;
    GUID FormatID;
    const WCHAR *CodecName;
    const WCHAR *DllName;
    const WCHAR *FormatDescription;
    const WCHAR *FilenameExtension;
    const WCHAR *MimeType;
    DWORD Flags;
    DWORD Version;
    DWORD SigCount;
    DWORD SigSize;
    const BYTE *SigPattern;
    const BYTE *SigMask;
} ImageCodecInfo;

typedef struct EncoderParameter {
    GUID Guid;
    ULONG NumberOfValues;
    ULONG Type;
    VOID *Value;
} EncoderParameter;

typedef struct EncoderParameters {
    UINT Count;
    EncoderParameter Parameter[1];
} EncoderParameters;

typedef struct PropertyItem {
    ULONG id;
    ULONG length;
    WORD type;
    VOID *value;
} PropertyItem;

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


/* ------------------------------------------------------------------ images */

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromResource(HINSTANCE instance, const WCHAR *name, GpBitmap **bitmap);
WINGDIPAPI GpStatus WINAPI GdipCreateHBITMAPFromBitmap(GpBitmap *bitmap, HBITMAP *hbitmap, ARGB background);
WINGDIPAPI GpStatus WINAPI GdipLoadImageFromFile(const WCHAR *file, GpImage **image);
WINGDIPAPI GpStatus WINAPI GdipLoadImageFromFileICM(const WCHAR *file, GpImage **image);
WINGDIPAPI GpStatus WINAPI GdipLoadImageFromStream(IStream *stream, GpImage **image);
WINGDIPAPI GpStatus WINAPI GdipLoadImageFromStreamICM(IStream *stream, GpImage **image);
WINGDIPAPI GpStatus WINAPI GdipGetImageType(GpImage *image, ImageType *type);
WINGDIPAPI GpStatus WINAPI GdipGetImageDimension(GpImage *image, float *width, float *height);
WINGDIPAPI GpStatus WINAPI GdipGetImageBounds(GpImage *image, GpRectF *bounds, GpUnit *unit);
WINGDIPAPI GpStatus WINAPI GdipGetImageFlags(GpImage *image, UINT *flags);
WINGDIPAPI GpStatus WINAPI GdipGetImageRawFormat(GpImage *image, GUID *format);
WINGDIPAPI GpStatus WINAPI GdipGetImageHorizontalResolution(GpImage *image, float *resolution);
WINGDIPAPI GpStatus WINAPI GdipGetImageVerticalResolution(GpImage *image, float *resolution);
WINGDIPAPI GpStatus WINAPI GdipBitmapSetResolution(GpBitmap *bitmap, float xdpi, float ydpi);
WINGDIPAPI GpStatus WINAPI GdipImageGetFrameDimensionsCount(GpImage *image, UINT *count);
WINGDIPAPI GpStatus WINAPI GdipImageGetFrameDimensionsList(GpImage *image, GUID *dimensions, UINT count);
WINGDIPAPI GpStatus WINAPI GdipImageGetFrameCount(GpImage *image, const GUID *dimension, UINT *count);
WINGDIPAPI GpStatus WINAPI GdipImageSelectActiveFrame(GpImage *image, const GUID *dimension, UINT index);
WINGDIPAPI GpStatus WINAPI GdipImageRotateFlip(GpImage *image, RotateFlipType type);
typedef BOOL(WINAPI *GetThumbnailImageAbort)(void *data);
WINGDIPAPI GpStatus WINAPI GdipGetImageThumbnail(GpImage *image, UINT width, UINT height, GpImage **thumbnail,
                                                 GetThumbnailImageAbort callback, void *data);
WINGDIPAPI GpStatus WINAPI GdipGetPropertyCount(GpImage *image, UINT *count);
WINGDIPAPI GpStatus WINAPI GdipGetPropertyIdList(GpImage *image, UINT count, ULONG *ids);
WINGDIPAPI GpStatus WINAPI GdipGetPropertyItemSize(GpImage *image, ULONG id, UINT *size);
WINGDIPAPI GpStatus WINAPI GdipGetPropertyItem(GpImage *image, ULONG id, UINT size, PropertyItem *item);
WINGDIPAPI GpStatus WINAPI GdipGetPropertySize(GpImage *image, UINT *total, UINT *count);
WINGDIPAPI GpStatus WINAPI GdipGetAllPropertyItems(GpImage *image, UINT total, UINT count, PropertyItem *items);
WINGDIPAPI GpStatus WINAPI GdipSetPropertyItem(GpImage *image, const PropertyItem *item);
WINGDIPAPI GpStatus WINAPI GdipRemovePropertyItem(GpImage *image, ULONG id);

WINGDIPAPI GpStatus WINAPI GdipGetImageEncodersSize(UINT *count, UINT *size);
WINGDIPAPI GpStatus WINAPI GdipGetImageEncoders(UINT count, UINT size, ImageCodecInfo *encoders);
WINGDIPAPI GpStatus WINAPI GdipGetImageDecodersSize(UINT *count, UINT *size);
WINGDIPAPI GpStatus WINAPI GdipGetImageDecoders(UINT count, UINT size, ImageCodecInfo *decoders);
WINGDIPAPI GpStatus WINAPI GdipGetEncoderParameterListSize(GpImage *image, const CLSID *encoder, UINT *size);
WINGDIPAPI GpStatus WINAPI GdipGetEncoderParameterList(GpImage *image, const CLSID *encoder, UINT size,
                                                       EncoderParameters *parameters);
WINGDIPAPI GpStatus WINAPI GdipSaveImageToStream(GpImage *image, IStream *stream, const CLSID *encoder,
                                                 const EncoderParameters *parameters);
WINGDIPAPI GpStatus WINAPI GdipSaveImageToFile(GpImage *image, const WCHAR *file, const CLSID *encoder,
                                               const EncoderParameters *parameters);
WINGDIPAPI GpStatus WINAPI GdipSaveAdd(GpImage *image, const EncoderParameters *parameters);
WINGDIPAPI GpStatus WINAPI GdipSaveAddImage(GpImage *image, GpImage *added, const EncoderParameters *parameters);

/* ------------------------------------------------------------------ matrices */

WINGDIPAPI GpStatus WINAPI GdipCreateMatrix(GpMatrix **matrix);
WINGDIPAPI GpStatus WINAPI GdipCreateMatrix2(float m11, float m12, float m21, float m22, float dx, float dy,
                                             GpMatrix **matrix);
WINGDIPAPI GpStatus WINAPI GdipCreateMatrix3(const GpRectF *rect, const GpPointF *points, GpMatrix **matrix);
WINGDIPAPI GpStatus WINAPI GdipCreateMatrix3I(const GpRect *rect, const GpPoint *points, GpMatrix **matrix);
WINGDIPAPI GpStatus WINAPI GdipCloneMatrix(GpMatrix *matrix, GpMatrix **clone);
WINGDIPAPI GpStatus WINAPI GdipDeleteMatrix(GpMatrix *matrix);
WINGDIPAPI GpStatus WINAPI GdipSetMatrixElements(GpMatrix *matrix, float m11, float m12, float m21, float m22,
                                                 float dx, float dy);
WINGDIPAPI GpStatus WINAPI GdipGetMatrixElements(const GpMatrix *matrix, float *elements);
WINGDIPAPI GpStatus WINAPI GdipMultiplyMatrix(GpMatrix *matrix, const GpMatrix *other, MatrixOrder order);
WINGDIPAPI GpStatus WINAPI GdipTranslateMatrix(GpMatrix *matrix, float dx, float dy, MatrixOrder order);
WINGDIPAPI GpStatus WINAPI GdipScaleMatrix(GpMatrix *matrix, float sx, float sy, MatrixOrder order);
WINGDIPAPI GpStatus WINAPI GdipRotateMatrix(GpMatrix *matrix, float angle, MatrixOrder order);
WINGDIPAPI GpStatus WINAPI GdipShearMatrix(GpMatrix *matrix, float shear_x, float shear_y, MatrixOrder order);
WINGDIPAPI GpStatus WINAPI GdipInvertMatrix(GpMatrix *matrix);
WINGDIPAPI GpStatus WINAPI GdipTransformMatrixPoints(GpMatrix *matrix, GpPointF *points, INT count);
WINGDIPAPI GpStatus WINAPI GdipTransformMatrixPointsI(GpMatrix *matrix, GpPoint *points, INT count);
WINGDIPAPI GpStatus WINAPI GdipVectorTransformMatrixPoints(GpMatrix *matrix, GpPointF *points, INT count);
WINGDIPAPI GpStatus WINAPI GdipVectorTransformMatrixPointsI(GpMatrix *matrix, GpPoint *points, INT count);
WINGDIPAPI GpStatus WINAPI GdipIsMatrixInvertible(const GpMatrix *matrix, BOOL *result);
WINGDIPAPI GpStatus WINAPI GdipIsMatrixIdentity(const GpMatrix *matrix, BOOL *result);
WINGDIPAPI GpStatus WINAPI GdipIsMatrixEqual(const GpMatrix *matrix, const GpMatrix *other, BOOL *result);

/* ------------------------------------------------------------------ brushes and pens */

WINGDIPAPI GpStatus WINAPI GdipCreateSolidFill(ARGB color, GpBrush **brush);
WINGDIPAPI GpStatus WINAPI GdipSetSolidFillColor(GpBrush *brush, ARGB color);
WINGDIPAPI GpStatus WINAPI GdipGetSolidFillColor(GpBrush *brush, ARGB *color);
WINGDIPAPI GpStatus WINAPI GdipGetBrushType(GpBrush *brush, BrushType *type);
WINGDIPAPI GpStatus WINAPI GdipCloneBrush(GpBrush *brush, GpBrush **clone);
WINGDIPAPI GpStatus WINAPI GdipDeleteBrush(GpBrush *brush);
WINGDIPAPI GpStatus WINAPI GdipCreatePen1(ARGB color, float width, GpUnit unit, GpPen **pen);
WINGDIPAPI GpStatus WINAPI GdipCreatePen2(GpBrush *brush, float width, GpUnit unit, GpPen **pen);
WINGDIPAPI GpStatus WINAPI GdipClonePen(GpPen *pen, GpPen **clone);
WINGDIPAPI GpStatus WINAPI GdipDeletePen(GpPen *pen);
WINGDIPAPI GpStatus WINAPI GdipSetPenColor(GpPen *pen, ARGB color);
WINGDIPAPI GpStatus WINAPI GdipGetPenColor(GpPen *pen, ARGB *color);
WINGDIPAPI GpStatus WINAPI GdipSetPenWidth(GpPen *pen, float width);
WINGDIPAPI GpStatus WINAPI GdipGetPenWidth(GpPen *pen, float *width);
WINGDIPAPI GpStatus WINAPI GdipSetPenBrushFill(GpPen *pen, GpBrush *brush);
WINGDIPAPI GpStatus WINAPI GdipGetPenBrushFill(GpPen *pen, GpBrush **brush);
WINGDIPAPI GpStatus WINAPI GdipGetPenFillType(GpPen *pen, BrushType *type);
WINGDIPAPI GpStatus WINAPI GdipSetPenDashStyle(GpPen *pen, DashStyle style);
WINGDIPAPI GpStatus WINAPI GdipGetPenDashStyle(GpPen *pen, DashStyle *style);
WINGDIPAPI GpStatus WINAPI GdipGetPenUnit(GpPen *pen, GpUnit *unit);
WINGDIPAPI GpStatus WINAPI GdipSetPenUnit(GpPen *pen, GpUnit unit);

/* ------------------------------------------------------------------ graphics state and drawing */

WINGDIPAPI GpStatus WINAPI GdipSetSmoothingMode(GpGraphics *graphics, SmoothingMode mode);
WINGDIPAPI GpStatus WINAPI GdipGetSmoothingMode(GpGraphics *graphics, SmoothingMode *mode);
WINGDIPAPI GpStatus WINAPI GdipSetCompositingMode(GpGraphics *graphics, CompositingMode mode);
WINGDIPAPI GpStatus WINAPI GdipGetCompositingMode(GpGraphics *graphics, CompositingMode *mode);
WINGDIPAPI GpStatus WINAPI GdipSetCompositingQuality(GpGraphics *graphics, CompositingQuality quality);
WINGDIPAPI GpStatus WINAPI GdipGetCompositingQuality(GpGraphics *graphics, CompositingQuality *quality);
WINGDIPAPI GpStatus WINAPI GdipSetPixelOffsetMode(GpGraphics *graphics, PixelOffsetMode mode);
WINGDIPAPI GpStatus WINAPI GdipGetPixelOffsetMode(GpGraphics *graphics, PixelOffsetMode *mode);
WINGDIPAPI GpStatus WINAPI GdipGetInterpolationMode(GpGraphics *graphics, InterpolationMode *mode);
WINGDIPAPI GpStatus WINAPI GdipSetPageUnit(GpGraphics *graphics, GpUnit unit);
WINGDIPAPI GpStatus WINAPI GdipGetPageUnit(GpGraphics *graphics, GpUnit *unit);
WINGDIPAPI GpStatus WINAPI GdipSetPageScale(GpGraphics *graphics, float scale);
WINGDIPAPI GpStatus WINAPI GdipGetPageScale(GpGraphics *graphics, float *scale);
WINGDIPAPI GpStatus WINAPI GdipSetWorldTransform(GpGraphics *graphics, GpMatrix *matrix);
WINGDIPAPI GpStatus WINAPI GdipGetWorldTransform(GpGraphics *graphics, GpMatrix *matrix);
WINGDIPAPI GpStatus WINAPI GdipResetWorldTransform(GpGraphics *graphics);
WINGDIPAPI GpStatus WINAPI GdipMultiplyWorldTransform(GpGraphics *graphics, const GpMatrix *matrix, MatrixOrder order);
WINGDIPAPI GpStatus WINAPI GdipTranslateWorldTransform(GpGraphics *graphics, float dx, float dy, MatrixOrder order);
WINGDIPAPI GpStatus WINAPI GdipScaleWorldTransform(GpGraphics *graphics, float sx, float sy, MatrixOrder order);
WINGDIPAPI GpStatus WINAPI GdipFillRectangleI(GpGraphics *graphics, GpBrush *brush, INT x, INT y, INT width,
                                              INT height);
WINGDIPAPI GpStatus WINAPI GdipFillRectangle(GpGraphics *graphics, GpBrush *brush, float x, float y, float width,
                                             float height);
WINGDIPAPI GpStatus WINAPI GdipDrawRectangleI(GpGraphics *graphics, GpPen *pen, INT x, INT y, INT width, INT height);
WINGDIPAPI GpStatus WINAPI GdipDrawRectangle(GpGraphics *graphics, GpPen *pen, float x, float y, float width,
                                             float height);
WINGDIPAPI GpStatus WINAPI GdipDrawImagePointRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT src_x,
                                                   INT src_y, INT src_width, INT src_height, GpUnit src_unit);
WINGDIPAPI GpStatus WINAPI GdipDrawImageRectRectI(GpGraphics *graphics, GpImage *image, INT dst_x, INT dst_y,
                                                  INT dst_width, INT dst_height, INT src_x, INT src_y, INT src_width,
                                                  INT src_height, GpUnit src_unit, void *attributes,
                                                  void *callback, void *data);

#endif
