/*
 * gdi+ flat api: the C entry points of gdiplus.dll, its types and constants.
 * the C++ wrapper classes of the sdk call these, so programs import only
 * the Gdip names. every function uses the WINAPI convention and returns a
 * GpStatus. the objects are opaque, as in the sdk headers for C.
 *
 * generated from the public sdk interface by tools/gen_gdiplus_header.py.
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
#define GDIPCONST const

typedef float REAL;
typedef ULONG PROPID;
typedef DWORD ARGB;
typedef INT PixelFormat;
typedef struct IDirectDrawSurface7 IDirectDrawSurface7;
/* ------------------------------------------------------------------ types */
typedef enum GpStatus {
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
	ProfileNotFound = 21
} GpStatus;


typedef struct Size {
	INT Width;
	INT Height;

} Size;

typedef struct SizeF {
	REAL Width;
	REAL Height;

} SizeF;

typedef struct Point {
	INT X;
	INT Y;

} Point;

typedef struct PointF {
	REAL X;
	REAL Y;

} PointF;

typedef struct Rect {
	INT X;
	INT Y;
	INT Width;
	INT Height;

} Rect;

typedef struct RectF {
	REAL X;
	REAL Y;
	REAL Width;
	REAL Height;

} RectF;

/* FIXME: Are descendants of this class, when compiled with g++,
   binary compatible with MSVC++ code (especially GDIPLUS.DLL of course)? */
typedef struct GdiplusAbort GdiplusAbort;  /* incomplete type */

typedef struct CharacterRange {
	INT First;
	INT Length;

} CharacterRange;

typedef struct PathData {
	INT Count;
	PointF *Points;
	BYTE *Types;

} PathData;

/* Callback function types */
/* ------------------------------------------------------------------ enumerations */
typedef enum BrushType {
	BrushTypeSolidColor = 0,
	BrushTypeHatchFill = 1,
	BrushTypeTextureFill = 2,
	BrushTypePathGradient = 3,
	BrushTypeLinearGradient = 4
} BrushType;

typedef enum CombineMode {
	CombineModeReplace = 0,
	CombineModeIntersect = 1,
	CombineModeUnion = 2,
	CombineModeXor = 3,
	CombineModeExclude = 4,
	CombineModeComplement = 5
} CombineMode;

typedef enum CompositingMode {
	CompositingModeSourceOver = 0,
	CompositingModeSourceCopy = 1
} CompositingMode;

typedef enum CompositingQuality {
	CompositingQualityDefault = 0,
	CompositingQualityHighSpeed = 1,
	CompositingQualityHighQuality = 2,
	CompositingQualityGammaCorrected = 3,
	CompositingQualityAssumeLinear = 4
} CompositingQuality;

typedef enum CoordinateSpace {
	CoordinateSpaceWorld = 0,
	CoordinateSpacePage = 1,
	CoordinateSpaceDevice = 2
} CoordinateSpace;

typedef enum CustomLineCapType {
	CustomLineCapTypeDefault = 0,
	CustomLineCapTypeAdjustableArrow = 1
} CustomLineCapType;

typedef enum DashCap {
	DashCapFlat = 0,
	DashCapRound = 2,
	DashCapTriangle = 3
} DashCap;

typedef enum DashStyle {
	DashStyleSolid = 0,
	DashStyleDash = 1,
	DashStyleDot = 2,
	DashStyleDashDot = 3,
	DashStyleDashDotDot = 4,
	DashStyleCustom = 5
} DashStyle;

typedef enum DitherType {
	DitherTypeNone = 0,
	DitherTypeSolid = 1,
	DitherTypeOrdered4x4 = 2,
	DitherTypeOrdered8x8 = 3,
	DitherTypeOrdered16x16 = 4,
	DitherTypeOrdered91x91 = 5,
	DitherTypeSpiral4x4 = 6,
	DitherTypeSpiral8x8 = 7,
	DitherTypeDualSpiral4x4 = 8,
	DitherTypeDualSpiral8x8 = 9,
	DitherTypeErrorDiffusion = 10
} DitherType;

typedef enum DriverStringOptions {
	DriverStringOptionsCmapLookup = 1,
	DriverStringOptionsVertical = 2,
	DriverStringOptionsRealizedAdvance = 4,
	DriverStringOptionsLimitSubpixel = 8
} DriverStringOptions;

#define GDIP_WMF_RECORD_TO_EMFPLUS(meta) ((meta) | 0x10000)
#define GDIP_EMFPLUS_RECORD_BASE (0x4000)
typedef enum EmfPlusRecordType {
	WmfRecordTypeSetBkColor                    = (0x0201 | 0x10000),
	WmfRecordTypeSetBkMode                     = (0x0102 | 0x10000),
	WmfRecordTypeSetMapMode                    = (0x0103 | 0x10000),
	WmfRecordTypeSetROP2                       = (0x0104 | 0x10000),
	WmfRecordTypeSetRelAbs                     = (0x0105 | 0x10000),
	WmfRecordTypeSetPolyFillMode               = (0x0106 | 0x10000),
	WmfRecordTypeSetStretchBltMode             = (0x0107 | 0x10000),
	WmfRecordTypeSetTextCharExtra              = (0x0108 | 0x10000),
	WmfRecordTypeSetTextColor                  = (0x0209 | 0x10000),
	WmfRecordTypeSetTextJustification          = (0x020A | 0x10000),
	WmfRecordTypeSetWindowOrg                  = (0x020B | 0x10000),
	WmfRecordTypeSetWindowExt                  = (0x020C | 0x10000),
	WmfRecordTypeSetViewportOrg                = (0x020D | 0x10000),
	WmfRecordTypeSetViewportExt                = (0x020E | 0x10000),
	WmfRecordTypeOffsetWindowOrg               = (0x020F | 0x10000),
	WmfRecordTypeScaleWindowExt                = (0x0410 | 0x10000),
	WmfRecordTypeOffsetViewportOrg             = (0x0211 | 0x10000),
	WmfRecordTypeScaleViewportExt              = (0x0412 | 0x10000),
	WmfRecordTypeLineTo                        = (0x0213 | 0x10000),
	WmfRecordTypeMoveTo                        = (0x0214 | 0x10000),
	WmfRecordTypeExcludeClipRect               = (0x0415 | 0x10000),
	WmfRecordTypeIntersectClipRect             = (0x0416 | 0x10000),
	WmfRecordTypeArc                           = (0x0817 | 0x10000),
	WmfRecordTypeEllipse                       = (0x0418 | 0x10000),
	WmfRecordTypeFloodFill                     = (0x0419 | 0x10000),
	WmfRecordTypePie                           = (0x081A | 0x10000),
	WmfRecordTypeRectangle                     = (0x041B | 0x10000),
	WmfRecordTypeRoundRect                     = (0x061C | 0x10000),
	WmfRecordTypePatBlt                        = (0x061D | 0x10000),
	WmfRecordTypeSaveDC                        = (0x001E | 0x10000),
	WmfRecordTypeSetPixel                      = (0x041F | 0x10000),
	WmfRecordTypeOffsetClipRgn                 = (0x0220 | 0x10000),
	WmfRecordTypeTextOut                       = (0x0521 | 0x10000),
	WmfRecordTypeBitBlt                        = (0x0922 | 0x10000),
	WmfRecordTypeStretchBlt                    = (0x0B23 | 0x10000),
	WmfRecordTypePolygon                       = (0x0324 | 0x10000),
	WmfRecordTypePolyline                      = (0x0325 | 0x10000),
	WmfRecordTypeEscape                        = (0x0626 | 0x10000),
	WmfRecordTypeRestoreDC                     = (0x0127 | 0x10000),
	WmfRecordTypeFillRegion                    = (0x0228 | 0x10000),
	WmfRecordTypeFrameRegion                   = (0x0429 | 0x10000),
	WmfRecordTypeInvertRegion                  = (0x012A | 0x10000),
	WmfRecordTypePaintRegion                   = (0x012B | 0x10000),
	WmfRecordTypeSelectClipRegion              = (0x012C | 0x10000),
	WmfRecordTypeSelectObject                  = (0x012D | 0x10000),
	WmfRecordTypeSetTextAlign                  = (0x012E | 0x10000),
	WmfRecordTypeDrawText                      = GDIP_WMF_RECORD_TO_EMFPLUS(0x062F),
	WmfRecordTypeChord                         = (0x0830 | 0x10000),
	WmfRecordTypeSetMapperFlags                = (0x0231 | 0x10000),
	WmfRecordTypeExtTextOut                    = (0x0a32 | 0x10000),
	WmfRecordTypeSetDIBToDev                   = (0x0d33 | 0x10000),
	WmfRecordTypeSelectPalette                 = (0x0234 | 0x10000),
	WmfRecordTypeRealizePalette                = (0x0035 | 0x10000),
	WmfRecordTypeAnimatePalette                = (0x0436 | 0x10000),
	WmfRecordTypeSetPalEntries                 = (0x0037 | 0x10000),
	WmfRecordTypePolyPolygon                   = (0x0538 | 0x10000),
	WmfRecordTypeResizePalette                 = (0x0139 | 0x10000),
	WmfRecordTypeDIBBitBlt                     = (0x0940 | 0x10000),
	WmfRecordTypeDIBStretchBlt                 = (0x0b41 | 0x10000),
	WmfRecordTypeDIBCreatePatternBrush         = (0x0142 | 0x10000),
	WmfRecordTypeStretchDIB                    = (0x0f43 | 0x10000),
	WmfRecordTypeExtFloodFill                  = (0x0548 | 0x10000),
	WmfRecordTypeSetLayout                     = GDIP_WMF_RECORD_TO_EMFPLUS(0x0149),
	WmfRecordTypeResetDC                       = GDIP_WMF_RECORD_TO_EMFPLUS(0x014C),
	WmfRecordTypeStartDoc                      = GDIP_WMF_RECORD_TO_EMFPLUS(0x014D),
	WmfRecordTypeStartPage                     = GDIP_WMF_RECORD_TO_EMFPLUS(0x004F),
	WmfRecordTypeEndPage                       = GDIP_WMF_RECORD_TO_EMFPLUS(0x0050),
	WmfRecordTypeAbortDoc                      = GDIP_WMF_RECORD_TO_EMFPLUS(0x0052),
	WmfRecordTypeEndDoc                        = GDIP_WMF_RECORD_TO_EMFPLUS(0x005E),
	WmfRecordTypeDeleteObject                  = (0x01f0 | 0x10000),
	WmfRecordTypeCreatePalette                 = (0x00f7 | 0x10000),
	WmfRecordTypeCreateBrush                   = GDIP_WMF_RECORD_TO_EMFPLUS(0x00F8),
	WmfRecordTypeCreatePatternBrush            = (0x01F9 | 0x10000),
	WmfRecordTypeCreatePenIndirect             = (0x02FA | 0x10000),
	WmfRecordTypeCreateFontIndirect            = (0x02FB | 0x10000),
	WmfRecordTypeCreateBrushIndirect           = (0x02FC | 0x10000),
	WmfRecordTypeCreateBitmapIndirect          = GDIP_WMF_RECORD_TO_EMFPLUS(0x02FD),
	WmfRecordTypeCreateBitmap                  = GDIP_WMF_RECORD_TO_EMFPLUS(0x06FE),
	WmfRecordTypeCreateRegion                  = (0x06FF | 0x10000),
	EmfRecordTypeHeader                        = EMR_HEADER,
	EmfRecordTypePolyBezier                    = EMR_POLYBEZIER,
	EmfRecordTypePolygon                       = EMR_POLYGON,
	EmfRecordTypePolyline                      = EMR_POLYLINE,
	EmfRecordTypePolyBezierTo                  = EMR_POLYBEZIERTO,
	EmfRecordTypePolyLineTo                    = EMR_POLYLINETO,
	EmfRecordTypePolyPolyline                  = EMR_POLYPOLYLINE,
	EmfRecordTypePolyPolygon                   = EMR_POLYPOLYGON,
	EmfRecordTypeSetWindowExtEx                = EMR_SETWINDOWEXTEX,
	EmfRecordTypeSetWindowOrgEx                = EMR_SETWINDOWORGEX,
	EmfRecordTypeSetViewportExtEx              = EMR_SETVIEWPORTEXTEX,
	EmfRecordTypeSetViewportOrgEx              = EMR_SETVIEWPORTORGEX,
	EmfRecordTypeSetBrushOrgEx                 = EMR_SETBRUSHORGEX,
	EmfRecordTypeEOF                           = EMR_EOF,
	EmfRecordTypeSetPixelV                     = EMR_SETPIXELV,
	EmfRecordTypeSetMapperFlags                = EMR_SETMAPPERFLAGS,
	EmfRecordTypeSetMapMode                    = EMR_SETMAPMODE,
	EmfRecordTypeSetBkMode                     = EMR_SETBKMODE,
	EmfRecordTypeSetPolyFillMode               = EMR_SETPOLYFILLMODE,
	EmfRecordTypeSetROP2                       = EMR_SETROP2,
	EmfRecordTypeSetStretchBltMode             = EMR_SETSTRETCHBLTMODE,
	EmfRecordTypeSetTextAlign                  = EMR_SETTEXTALIGN,
	EmfRecordTypeSetColorAdjustment            = EMR_SETCOLORADJUSTMENT,
	EmfRecordTypeSetTextColor                  = EMR_SETTEXTCOLOR,
	EmfRecordTypeSetBkColor                    = EMR_SETBKCOLOR,
	EmfRecordTypeOffsetClipRgn                 = EMR_OFFSETCLIPRGN,
	EmfRecordTypeMoveToEx                      = EMR_MOVETOEX,
	EmfRecordTypeSetMetaRgn                    = EMR_SETMETARGN,
	EmfRecordTypeExcludeClipRect               = EMR_EXCLUDECLIPRECT,
	EmfRecordTypeIntersectClipRect             = EMR_INTERSECTCLIPRECT,
	EmfRecordTypeScaleViewportExtEx            = EMR_SCALEVIEWPORTEXTEX,
	EmfRecordTypeScaleWindowExtEx              = EMR_SCALEWINDOWEXTEX,
	EmfRecordTypeSaveDC                        = EMR_SAVEDC,
	EmfRecordTypeRestoreDC                     = EMR_RESTOREDC,
	EmfRecordTypeSetWorldTransform             = EMR_SETWORLDTRANSFORM,
	EmfRecordTypeModifyWorldTransform          = EMR_MODIFYWORLDTRANSFORM,
	EmfRecordTypeSelectObject                  = EMR_SELECTOBJECT,
	EmfRecordTypeCreatePen                     = EMR_CREATEPEN,
	EmfRecordTypeCreateBrushIndirect           = EMR_CREATEBRUSHINDIRECT,
	EmfRecordTypeDeleteObject                  = EMR_DELETEOBJECT,
	EmfRecordTypeAngleArc                      = EMR_ANGLEARC,
	EmfRecordTypeEllipse                       = EMR_ELLIPSE,
	EmfRecordTypeRectangle                     = EMR_RECTANGLE,
	EmfRecordTypeRoundRect                     = EMR_ROUNDRECT,
	EmfRecordTypeArc                           = EMR_ARC,
	EmfRecordTypeChord                         = EMR_CHORD,
	EmfRecordTypePie                           = EMR_PIE,
	EmfRecordTypeSelectPalette                 = EMR_SELECTPALETTE,
	EmfRecordTypeCreatePalette                 = EMR_CREATEPALETTE,
	EmfRecordTypeSetPaletteEntries             = EMR_SETPALETTEENTRIES,
	EmfRecordTypeResizePalette                 = EMR_RESIZEPALETTE,
	EmfRecordTypeRealizePalette                = EMR_REALIZEPALETTE,
	EmfRecordTypeExtFloodFill                  = EMR_EXTFLOODFILL,
	EmfRecordTypeLineTo                        = EMR_LINETO,
	EmfRecordTypeArcTo                         = EMR_ARCTO,
	EmfRecordTypePolyDraw                      = EMR_POLYDRAW,
	EmfRecordTypeSetArcDirection               = EMR_SETARCDIRECTION,
	EmfRecordTypeSetMiterLimit                 = EMR_SETMITERLIMIT,
	EmfRecordTypeBeginPath                     = EMR_BEGINPATH,
	EmfRecordTypeEndPath                       = EMR_ENDPATH,
	EmfRecordTypeCloseFigure                   = EMR_CLOSEFIGURE,
	EmfRecordTypeFillPath                      = EMR_FILLPATH,
	EmfRecordTypeStrokeAndFillPath             = EMR_STROKEANDFILLPATH,
	EmfRecordTypeStrokePath                    = EMR_STROKEPATH,
	EmfRecordTypeFlattenPath                   = EMR_FLATTENPATH,
	EmfRecordTypeWidenPath                     = EMR_WIDENPATH,
	EmfRecordTypeSelectClipPath                = EMR_SELECTCLIPPATH,
	EmfRecordTypeAbortPath                     = EMR_ABORTPATH,
	EmfRecordTypeReserved_069                  = 69,
	EmfRecordTypeGdiComment                    = EMR_GDICOMMENT,
	EmfRecordTypeFillRgn                       = EMR_FILLRGN,
	EmfRecordTypeFrameRgn                      = EMR_FRAMERGN,
	EmfRecordTypeInvertRgn                     = EMR_INVERTRGN,
	EmfRecordTypePaintRgn                      = EMR_PAINTRGN,
	EmfRecordTypeExtSelectClipRgn              = EMR_EXTSELECTCLIPRGN,
	EmfRecordTypeBitBlt                        = EMR_BITBLT,
	EmfRecordTypeStretchBlt                    = EMR_STRETCHBLT,
	EmfRecordTypeMaskBlt                       = EMR_MASKBLT,
	EmfRecordTypePlgBlt                        = EMR_PLGBLT,
	EmfRecordTypeSetDIBitsToDevice             = EMR_SETDIBITSTODEVICE,
	EmfRecordTypeStretchDIBits                 = EMR_STRETCHDIBITS,
	EmfRecordTypeExtCreateFontIndirect         = EMR_EXTCREATEFONTINDIRECTW,
	EmfRecordTypeExtTextOutA                   = EMR_EXTTEXTOUTA,
	EmfRecordTypeExtTextOutW                   = EMR_EXTTEXTOUTW,
	EmfRecordTypePolyBezier16                  = EMR_POLYBEZIER16,
	EmfRecordTypePolygon16                     = EMR_POLYGON16,
	EmfRecordTypePolyline16                    = EMR_POLYLINE16,
	EmfRecordTypePolyBezierTo16                = EMR_POLYBEZIERTO16,
	EmfRecordTypePolylineTo16                  = EMR_POLYLINETO16,
	EmfRecordTypePolyPolyline16                = EMR_POLYPOLYLINE16,
	EmfRecordTypePolyPolygon16                 = EMR_POLYPOLYGON16,
	EmfRecordTypePolyDraw16                    = EMR_POLYDRAW16,
	EmfRecordTypeCreateMonoBrush               = EMR_CREATEMONOBRUSH,
	EmfRecordTypeCreateDIBPatternBrushPt       = EMR_CREATEDIBPATTERNBRUSHPT,
	EmfRecordTypeExtCreatePen                  = EMR_EXTCREATEPEN,
	EmfRecordTypePolyTextOutA                  = EMR_POLYTEXTOUTA,
	EmfRecordTypePolyTextOutW                  = EMR_POLYTEXTOUTW,
	EmfRecordTypeSetICMMode                    = 98,
	EmfRecordTypeCreateColorSpace              = 99,
	EmfRecordTypeSetColorSpace                 = 100,
	EmfRecordTypeDeleteColorSpace              = 101,
	EmfRecordTypeGLSRecord                     = 102,
	EmfRecordTypeGLSBoundedRecord              = 103,
	EmfRecordTypePixelFormat                   = 104,
	EmfRecordTypeDrawEscape                    = 105,
	EmfRecordTypeExtEscape                     = 106,
	EmfRecordTypeStartDoc                      = 107,
	EmfRecordTypeSmallTextOut                  = 108,
	EmfRecordTypeForceUFIMapping               = 109,
	EmfRecordTypeNamedEscape                   = 110,
	EmfRecordTypeColorCorrectPalette           = 111,
	EmfRecordTypeSetICMProfileA                = 112,
	EmfRecordTypeSetICMProfileW                = 113,
	EmfRecordTypeAlphaBlend                    = 114,
	EmfRecordTypeSetLayout                     = 115,
	EmfRecordTypeTransparentBlt                = 116,
	EmfRecordTypeReserved_117                  = 117,
	EmfRecordTypeGradientFill                  = 118,
	EmfRecordTypeSetLinkedUFIs                 = 119,
	EmfRecordTypeSetTextJustification          = 120,
	EmfRecordTypeColorMatchToTargetW           = 121,
	EmfRecordTypeCreateColorSpaceW             = 122,
	EmfRecordTypeMax                           = 122,
	EmfRecordTypeMin                           = 1,
	EmfPlusRecordTypeInvalid                   = GDIP_EMFPLUS_RECORD_BASE,
	EmfPlusRecordTypeHeader,
	EmfPlusRecordTypeEndOfFile,
	EmfPlusRecordTypeComment,
	EmfPlusRecordTypeGetDC,
	EmfPlusRecordTypeMultiFormatStart,
	EmfPlusRecordTypeMultiFormatSection,
	EmfPlusRecordTypeMultiFormatEnd,
	EmfPlusRecordTypeObject,
	EmfPlusRecordTypeClear,
	EmfPlusRecordTypeFillRects,
	EmfPlusRecordTypeDrawRects,
	EmfPlusRecordTypeFillPolygon,
	EmfPlusRecordTypeDrawLines,
	EmfPlusRecordTypeFillEllipse,
	EmfPlusRecordTypeDrawEllipse,
	EmfPlusRecordTypeFillPie,
	EmfPlusRecordTypeDrawPie,
	EmfPlusRecordTypeDrawArc,
	EmfPlusRecordTypeFillRegion,
	EmfPlusRecordTypeFillPath,
	EmfPlusRecordTypeDrawPath,
	EmfPlusRecordTypeFillClosedCurve,
	EmfPlusRecordTypeDrawClosedCurve,
	EmfPlusRecordTypeDrawCurve,
	EmfPlusRecordTypeDrawBeziers,
	EmfPlusRecordTypeDrawImage,
	EmfPlusRecordTypeDrawImagePoints,
	EmfPlusRecordTypeDrawString,
	EmfPlusRecordTypeSetRenderingOrigin,
	EmfPlusRecordTypeSetAntiAliasMode,
	EmfPlusRecordTypeSetTextRenderingHint,
	EmfPlusRecordTypeSetTextContrast,
	EmfPlusRecordTypeSetGammaValue,
	EmfPlusRecordTypeSetInterpolationMode,
	EmfPlusRecordTypeSetPixelOffsetMode,
	EmfPlusRecordTypeSetCompositingMode,
	EmfPlusRecordTypeSetCompositingQuality,
	EmfPlusRecordTypeSave,
	EmfPlusRecordTypeRestore,
	EmfPlusRecordTypeBeginContainer,
	EmfPlusRecordTypeBeginContainerNoParams,
	EmfPlusRecordTypeEndContainer,
	EmfPlusRecordTypeSetWorldTransform,
	EmfPlusRecordTypeResetWorldTransform,
	EmfPlusRecordTypeMultiplyWorldTransform,
	EmfPlusRecordTypeTranslateWorldTransform,
	EmfPlusRecordTypeScaleWorldTransform,
	EmfPlusRecordTypeRotateWorldTransform,
	EmfPlusRecordTypeSetPageTransform,
	EmfPlusRecordTypeResetClip,
	EmfPlusRecordTypeSetClipRect,
	EmfPlusRecordTypeSetClipPath,
	EmfPlusRecordTypeSetClipRegion,
	EmfPlusRecordTypeOffsetClip,
	EmfPlusRecordTypeDrawDriverString,
	EmfPlusRecordTypeStrokeFillPath,
	EmfPlusRecordTypeSerializableObject,
	EmfPlusRecordTypeSetTSGraphics,
	EmfPlusRecordTypeSetTSClip,
	EmfPlusRecordTotal,
	EmfPlusRecordTypeMax = EmfPlusRecordTotal-1,
	EmfPlusRecordTypeMin = EmfPlusRecordTypeHeader
} EmfPlusRecordType;

typedef enum EmfToWmfBitsFlags {
	EmfToWmfBitsFlagsDefault = 0,
	EmfToWmfBitsFlagsEmbedEmf = 1,
	EmfToWmfBitsFlagsIncludePlaceable = 2,
	EmfToWmfBitsFlagsNoXORClip = 4
} EmfToWmfBitsFlags;

typedef enum EmfType {
	EmfTypeEmfOnly = 3,
	EmfTypeEmfPlusOnly = 4,
	EmfTypeEmfPlusDual = 5
} EmfType;

typedef enum EncoderParameterValueType {
	EncoderParameterValueTypeByte = 1,
	EncoderParameterValueTypeASCII = 2,
	EncoderParameterValueTypeShort = 3,
	EncoderParameterValueTypeLong = 4,
	EncoderParameterValueTypeRational = 5,
	EncoderParameterValueTypeLongRange = 6,
	EncoderParameterValueTypeUndefined = 7,
	EncoderParameterValueTypeRationalRange = 8,
	EncoderParameterValueTypePointer = 9
} EncoderParameterValueType;

typedef enum EncoderValue {
	EncoderValueColorTypeCMYK = 0,
	EncoderValueColorTypeYCCK = 1,
	EncoderValueCompressionLZW = 2,
	EncoderValueCompressionCCITT3 = 3,
	EncoderValueCompressionCCITT4 = 4,
	EncoderValueCompressionRle = 5,
	EncoderValueCompressionNone = 6,
	EncoderValueScanMethodInterlaced = 7,
	EncoderValueScanMethodNonInterlaced = 8,
	EncoderValueVersionGif87 = 9,
	EncoderValueVersionGif89 = 10,
	EncoderValueRenderProgressive = 11,
	EncoderValueRenderNonProgressive = 12,
	EncoderValueTransformRotate90 = 13,
	EncoderValueTransformRotate180 = 14,
	EncoderValueTransformRotate270 = 15,
	EncoderValueTransformFlipHorizontal = 16,
	EncoderValueTransformFlipVertical = 17,
	EncoderValueMultiFrame = 18,
	EncoderValueLastFrame = 19,
	EncoderValueFlush = 20,
	EncoderValueFrameDimensionTime = 21,
	EncoderValueFrameDimensionResolution = 22,
	EncoderValueFrameDimensionPage = 23
} EncoderValue;

typedef enum FillMode {
	FillModeAlternate = 0,
	FillModeWinding = 1
} FillMode;

typedef enum FlushIntention {
	FlushIntentionFlush = 0,
	FlushIntentionSync = 1
} FlushIntention;

typedef enum FontStyle {
	FontStyleRegular = 0,
	FontStyleBold = 1,
	FontStyleItalic = 2,
	FontStyleBoldItalic = 3,
	FontStyleUnderline = 4,
	FontStyleStrikeout = 8
} FontStyle;

typedef enum HatchStyle {
	HatchStyleHorizontal = 0,
	HatchStyleVertical = 1,
	HatchStyleForwardDiagonal = 2,
	HatchStyleBackwardDiagonal = 3,
	HatchStyleCross = 4,
	HatchStyleLargeGrid = 4,
	HatchStyleDiagonalCross = 5,
	HatchStyle05Percent = 6,
	HatchStyle10Percent = 7,
	HatchStyle20Percent = 8,
	HatchStyle25Percent = 9,
	HatchStyle30Percent = 10,
	HatchStyle40Percent = 11,
	HatchStyle50Percent = 12,
	HatchStyle60Percent = 13,
	HatchStyle70Percent = 14,
	HatchStyle75Percent = 15,
	HatchStyle80Percent = 16,
	HatchStyle90Percent = 17,
	HatchStyleLightDownwardDiagonal = 18,
	HatchStyleLightUpwardDiagonal = 19,
	HatchStyleDarkDownwardDiagonal = 20,
	HatchStyleDarkUpwardDiagonal = 21,
	HatchStyleWideDownwardDiagonal = 22,
	HatchStyleWideUpwardDiagonal = 23,
	HatchStyleLightVertical = 24,
	HatchStyleLightHorizontal = 25,
	HatchStyleNarrowVertical = 26,
	HatchStyleNarrowHorizontal = 27,
	HatchStyleDarkVertical = 28,
	HatchStyleDarkHorizontal = 29,
	HatchStyleDashedDownwardDiagonal = 30,
	HatchStyleDashedUpwardDiagonal = 31,
	HatchStyleDashedHorizontal = 32,
	HatchStyleDashedVertical = 33,
	HatchStyleSmallConfetti = 34,
	HatchStyleLargeConfetti = 35,
	HatchStyleZigZag = 36,
	HatchStyleWave = 37,
	HatchStyleDiagonalBrick = 38,
	HatchStyleHorizontalBrick = 39,
	HatchStyleWeave = 40,
	HatchStylePlaid = 41,
	HatchStyleDivot = 42,
	HatchStyleDottedGrid = 43,
	HatchStyleDottedDiamond = 44,
	HatchStyleShingle = 45,
	HatchStyleTrellis = 46,
	HatchStyleSphere = 47,
	HatchStyleSmallGrid = 48,
	HatchStyleSmallCheckerBoard = 49,
	HatchStyleLargeCheckerBoard = 50,
	HatchStyleOutlinedDiamond = 51,
	HatchStyleSolidDiamond = 52,
	HatchStyleTotal = 53,
	HatchStyleMin = HatchStyleHorizontal,
	HatchStyleMax = HatchStyleTotal - 1
} HatchStyle;

typedef enum HotkeyPrefix {
	HotkeyPrefixNone = 0,
	HotkeyPrefixShow = 1,
	HotkeyPrefixHide = 2
} HotkeyPrefix;

typedef enum ImageType {
	ImageTypeUnknown = 0,
	ImageTypeBitmap = 1,
	ImageTypeMetafile = 2
} ImageType;

/* TODO: InterpolationMode */
typedef enum InterpolationMode {
	InterpolationModeInvalid = -1,
	InterpolationModeDefault = 0,
	InterpolationModeLowQuality = 1,
	InterpolationModeHighQuality = 2,
	InterpolationModeBilinear = 3,
	InterpolationModeBicubic = 4,
	InterpolationModeNearestNeighbor = 5,
	InterpolationModeHighQualityBilinear = 6,
	InterpolationModeHighQualityBicubic = 7
} InterpolationMode;

typedef enum LinearGradientMode {
	LinearGradientModeHorizontal = 0,
	LinearGradientModeVertical = 1,
	LinearGradientModeForwardDiagonal = 2,
	LinearGradientModeBackwardDiagonal = 3 
} LinearGradientMode;

typedef enum LineCap {
	LineCapFlat = 0,
	LineCapSquare = 1,
	LineCapRound = 2,
	LineCapTriangle = 3,
	LineCapNoAnchor = 16,
	LineCapSquareAnchor = 17,
	LineCapRoundAnchor = 18,
	LineCapDiamondAnchor = 19,
	LineCapArrowAnchor = 20,
	LineCapCustom = 255
} LineCap;

typedef enum LineJoin {
	LineJoinMiter = 0,
	LineJoinBevel = 1,
	LineJoinRound = 2,
	LineJoinMiterClipped = 3
} LineJoin;

typedef enum MatrixOrder {
	MatrixOrderPrepend = 0,
	MatrixOrderAppend = 1
} MatrixOrder;

typedef enum MetafileFrameUnit {
	MetafileFrameUnitPixel = 2,
	MetafileFrameUnitPoint = 3,
	MetafileFrameUnitInch = 4,
	MetafileFrameUnitDocument = 5,
	MetafileFrameUnitMillimeter = 6,
	MetafileFrameUnitGdi = 7
} MetafileFrameUnit;

typedef enum MetafileType {
	MetafileTypeInvalid = 0,
	MetafileTypeWmf = 1,
	MetafileTypeWmfPlaceable = 2,
	MetafileTypeEmf = 3,
	MetafileTypeEmfPlusOnly = 4,
	MetafileTypeEmfPlusDual = 5
} MetafileType;

typedef enum ObjectType {
	ObjectTypeInvalid = 0,
	ObjectTypeBrush = 1,
	ObjectTypePen = 2,
	ObjectTypePath = 3,
	ObjectTypeRegion = 4,
	ObjectTypeFont = 5,
	ObjectTypeStringFormat = 6,
	ObjectTypeImageAttributes = 7,
	ObjectTypeCustomLineCap = 8,
	ObjectTypeGraphics = 9,
	ObjectTypeMin = ObjectTypeBrush,
	ObjectTypeMax = ObjectTypeGraphics
} ObjectType;

typedef enum PathPointType {
	PathPointTypeStart = 0x00,
	PathPointTypeLine = 0x01,
	PathPointTypeBezier = 0x03,
	PathPointTypeBezier3 = 0x03,
	PathPointTypePathTypeMask = 0x07,
	PathPointTypePathDashMode = 0x10,
	PathPointTypePathMarker = 0x20,
	PathPointTypeCloseSubpath = 0x80
} PathPointType;

typedef enum PenAlignment {
	PenAlignmentCenter = 0,
	PenAlignmentInset = 1
} PenAlignment;

typedef enum PenType {
	PenTypeUnknown = -1, 
	PenTypeSolidColor = 0,
	PenTypeHatchFill = 1,
	PenTypeTextureFill = 2,
	PenTypePathGradient = 3,
	PenTypeLinearGradient = 4
} PenType;

/* TODO: PixelOffsetMode */
typedef enum PixelOffsetMode {
	PixelOffsetModeInvalid = -1,
	PixelOffsetModeDefault = 0,
	PixelOffsetModeHighSpeed = 1,
	PixelOffsetModeHighQuality = 2,
	PixelOffsetModeNone = 3,
	PixelOffsetModeHalf = 4
} PixelOffsetMode;

/* TODO: QualityMode */
typedef enum QualityMode {
	QualityModeInvalid = -1,
	QualityModeDefault = 0,
	QualityModeLow = 1,
	QualityModeHigh = 2
} QualityMode;

/* TODO: SmoothingMode */
typedef enum SmoothingMode {
	SmoothingModeInvalid = QualityModeInvalid,
	SmoothingModeDefault = 0,
	SmoothingModeHighSpeed = 1,
	SmoothingModeHighQuality = 2,
	SmoothingModeNone = 3,
	SmoothingModeAntiAlias8x4 = 4,
	SmoothingModeAntiAlias = 4,
	SmoothingModeAntiAlias8x8 = 5
} SmoothingMode;

typedef enum StringAlignment {
	StringAlignmentNear = 0,
	StringAlignmentCenter = 1,
	StringAlignmentFar = 2
} StringAlignment;

typedef enum StringDigitSubstitute {
	StringDigitSubstituteUser = 0,
	StringDigitSubstituteNone = 1,
	StringDigitSubstituteNational = 2,
	StringDigitSubstituteTraditional = 3
} StringDigitSubstitute;

typedef enum StringFormatFlags {
	StringFormatFlagsDirectionRightToLeft = 0x00000001,
	StringFormatFlagsDirectionVertical = 0x00000002,
	StringFormatFlagsNoFitBlackBox = 0x00000004,
	StringFormatFlagsDisplayFormatControl = 0x00000020,
	StringFormatFlagsNoFontFallback = 0x00000400,
	StringFormatFlagsMeasureTrailingSpaces = 0x00000800,
	StringFormatFlagsNoWrap = 0x00001000,
	StringFormatFlagsLineLimit = 0x00002000,
	StringFormatFlagsNoClip = 0x00004000
} StringFormatFlags;

typedef enum StringTrimming {
	StringTrimmingNone = 0,
	StringTrimmingCharacter = 1,
	StringTrimmingWord = 2,
	StringTrimmingEllipsisCharacter = 3,
	StringTrimmingEllipsisWord = 4,
	StringTrimmingEllipsisPath = 5
} StringTrimming;

typedef enum TextRenderingHint {
	TextRenderingHintSystemDefault = 0,
	TextRenderingHintSingleBitPerPixelGridFit = 1,
	TextRenderingHintSingleBitPerPixel = 2,
	TextRenderingHintAntiAliasGridFit = 3,
	TextRenderingHintAntiAlias = 4,
	TextRenderingHintClearTypeGridFit = 5
} TextRenderingHint;

typedef enum Unit {
	UnitWorld = 0,
	UnitDisplay = 1,
	UnitPixel = 2,
	UnitPoint = 3,
	UnitInch = 4,
	UnitDocument = 5,
	UnitMillimeter = 6
} Unit;

typedef enum WarpMode {
	WarpModePerspective = 0,
	WarpModeBilinear = 1
} WarpMode;

typedef enum WrapMode {
	WrapModeTile = 0,
	WrapModeTileFlipX = 1,
	WrapModeTileFlipY = 2,
	WrapModeTileFlipXY = 3,
	WrapModeClamp = 4
} WrapMode;

typedef enum GpTestControlEnum {
	TestControlForceBilinear = 0,
	TestControlForceNoICM = 1,
	TestControlGetBuildNumber = 2
} GpTestControlEnum;

/* Opaque handles to information blocks on an internal GDI+ stack. */
typedef DWORD GraphicsContainer;
typedef DWORD GraphicsState;

/* Default flatness for GraphicsPath methods Flatten, Outline, Warp, Widen */
/* FIXME: Is FlatnessDefault = 0.25f correct? */
#define FlatnessDefault ((REAL) 0.25f)

static __inline__ BOOL ObjectTypeIsValid(ObjectType type)
{
	switch (type) {
		case ObjectTypeBrush:
		case ObjectTypePen:
		case ObjectTypePath:
		case ObjectTypeRegion:
		case ObjectTypeFont:
		case ObjectTypeStringFormat:
		case ObjectTypeImageAttributes:
		case ObjectTypeCustomLineCap:
		case ObjectTypeGraphics:
			return TRUE;
		default:
			return FALSE;
	}
}
typedef void (WINAPI *DebugEventProc)(INT level, CHAR *message);
typedef BOOL (CALLBACK *EnumerateMetafileProc)(EmfPlusRecordType,UINT,UINT,const BYTE*,VOID*);
typedef BOOL (CALLBACK *DrawImageAbort)(VOID *data);
typedef BOOL (CALLBACK *GetThumbnailImageAbort)(VOID *data);
/* ------------------------------------------------------------------ pixel formats and colors */
typedef DWORD ARGB;
typedef INT PixelFormat;

#define PixelFormatIndexed ((INT) 0x00010000)
#define PixelFormatGDI ((INT) 0x00020000)
#define PixelFormatAlpha ((INT) 0x00040000)
#define PixelFormatPAlpha ((INT) 0x00080000)
#define PixelFormatExtended ((INT) 0x00100000)
#define PixelFormatCanonical ((INT) 0x00200000)
#define PixelFormatUndefined ((INT) 0)
#define PixelFormatDontCare ((INT) 0)
#define PixelFormat1bppIndexed ((INT) \
	(1 | (1<<8) | PixelFormatIndexed | PixelFormatGDI))
#define PixelFormat4bppIndexed ((INT) \
	(2 | (4<<8) | PixelFormatIndexed | PixelFormatGDI))
#define PixelFormat8bppIndexed ((INT) \
	(3 | (8<<8) | PixelFormatIndexed | PixelFormatGDI))
#define PixelFormat16bppGrayScale ((INT) \
	(4 | (16<<8) | PixelFormatExtended))
#define PixelFormat16bppRGB555 ((INT) \
	(5 | (16<<8) | PixelFormatGDI))
#define PixelFormat16bppRGB565 ((INT) \
	(6 | (16<<8) | PixelFormatGDI))
#define PixelFormat16bppARGB1555 ((INT) \
	(7 | (16<<8) | PixelFormatAlpha | PixelFormatGDI))
#define PixelFormat24bppRGB ((INT) \
	(8 | (24<<8) | PixelFormatGDI))
#define PixelFormat32bppRGB ((INT) \
	(9 | (32<<8) | PixelFormatGDI))
#define PixelFormat32bppARGB ((INT) \
	(10 | (32<<8) | PixelFormatAlpha | PixelFormatGDI | PixelFormatCanonical))
#define PixelFormat32bppPARGB ((INT) \
	(11 | (32<<8) | PixelFormatAlpha | PixelFormatPAlpha | PixelFormatGDI))
#define PixelFormat48bppRGB ((INT) \
	(12 | (48<<8) | PixelFormatExtended))
#define PixelFormat64bppARGB ((INT) \
	(13 | (64<<8) | PixelFormatAlpha | PixelFormatCanonical | PixelFormatExtended))
#define PixelFormat64bppPARGB ((INT) \
	(14 | (64<<8) | PixelFormatAlpha | PixelFormatPAlpha | PixelFormatExtended))
#define PixelFormat32bppCMYK ((PixelFormat) (15 | (32 << 8)))
#define PixelFormatMax ((INT) 16)

typedef enum PaletteFlags {
	PaletteFlagsHasAlpha = 1,
	PaletteFlagsGrayScale = 2,
	PaletteFlagsHalftone = 4
} PaletteFlags;

typedef enum PaletteType {
	PaletteTypeCustom = 0,
	PaletteTypeOptimal = 1,
	PaletteTypeFixedBW = 2,
	PaletteTypeFixedHalftone8 = 3,
	PaletteTypeFixedHalftone27 = 4,
	PaletteTypeFixedHalftone64 = 5,
	PaletteTypeFixedHalftone125 = 6,
	PaletteTypeFixedHalftone216 = 7,
	PaletteTypeFixedHalftone252 = 8,
	PaletteTypeFixedHalftone256 = 9 
} PaletteType;

typedef struct ColorPalette {
	UINT Flags;
	UINT Count;
	ARGB Entries[1];
} ColorPalette;

static __inline__ UINT GetPixelFormatSize(PixelFormat pixfmt)
{
	return (((UINT) pixfmt) & 0xff00U) >> 8;
}

static __inline__ BOOL IsAlphaPixelFormat(PixelFormat pixfmt)
{
	return (pixfmt & PixelFormatAlpha) != 0;
}

static __inline__ BOOL IsCanonicalPixelFormat(PixelFormat pixfmt)
{
	return (pixfmt & PixelFormatCanonical) != 0;
}

static __inline__ BOOL IsExtendedPixelFormat(PixelFormat pixfmt)
{
	return (pixfmt & PixelFormatExtended) != 0;
}

static __inline__ BOOL IsIndexedPixelFormat(PixelFormat pixfmt)
{
	return (pixfmt & PixelFormatIndexed) != 0;
}
typedef enum ColorChannelFlags {
	ColorChannelFlagsC = 0,
	ColorChannelFlagsM = 1,
	ColorChannelFlagsY = 2,
	ColorChannelFlagsK = 3,
	ColorChannelFlagsLast = 4
} ColorChannelFlags;

typedef struct Color
{
	ARGB Value;

} Color;
typedef enum ColorAdjustType {
	ColorAdjustTypeDefault = 0,
	ColorAdjustTypeBitmap = 1,
	ColorAdjustTypeBrush = 2,
	ColorAdjustTypePen = 3,
	ColorAdjustTypeText = 4,
	ColorAdjustTypeCount = 5,
	ColorAdjustTypeAny = 6
} ColorAdjustType;

typedef enum ColorMatrixFlags {
	ColorMatrixFlagsDefault = 0,
	ColorMatrixFlagsSkipGrays = 1,
	ColorMatrixFlagsAltGray = 2
} ColorMatrixFlags;

typedef enum HistogramFormat {
	HistogramFormatARGB = 0,
	HistogramFormatPARGB = 1,
	HistogramFormatRGB = 2,
	HistogramFormatGray = 3,
	HistogramFormatB = 4,
	HistogramFormatG = 5,
	HistogramFormatR = 6,
	HistogramFormatA = 7
} HistogramFormat;

typedef struct ColorMap {
	Color oldColor;
	Color newColor;
} ColorMap;

typedef struct ColorMatrix {
	REAL m[5][5];
} ColorMatrix;

typedef BYTE ColorChannelLUT[256];
/* ------------------------------------------------------------------ images */
typedef enum ImageCodecFlags {
	ImageCodecFlagsEncoder = 0x00000001,
	ImageCodecFlagsDecoder = 0x00000002,
	ImageCodecFlagsSupportBitmap = 0x00000004,
	ImageCodecFlagsSupportVector = 0x00000008,
	ImageCodecFlagsSeekableEncode = 0x00000010,
	ImageCodecFlagsBlockingDecode = 0x00000020,
	ImageCodecFlagsBuiltin = 0x00010000,
	ImageCodecFlagsSystem = 0x00020000,
	ImageCodecFlagsUser = 0x00040000
} ImageCodecFlags;

typedef enum ImageFlags {
	ImageFlagsNone = 0,
	ImageFlagsScalable = 0x00000001,
	ImageFlagsHasAlpha = 0x00000002,
	ImageFlagsHasTranslucent = 0x00000004,
	ImageFlagsPartiallyScalable = 0x00000008,
	ImageFlagsColorSpaceRGB = 0x00000010,
	ImageFlagsColorSpaceCMYK = 0x00000020,
	ImageFlagsColorSpaceGRAY = 0x00000040,
	ImageFlagsColorSpaceYCBCR = 0x00000080,
	ImageFlagsColorSpaceYCCK = 0x00000100,
	ImageFlagsHasRealDPI = 0x00001000,
	ImageFlagsHasRealPixelSize = 0x00002000,
	ImageFlagsReadOnly = 0x00010000,
	ImageFlagsCaching = 0x00020000
} ImageFlags;

typedef enum ImageLockMode {
	ImageLockModeRead = 1,
	ImageLockModeWrite = 2,
	ImageLockModeUserInputBuf = 4
} ImageLockMode;

typedef enum ItemDataPosition {
	ItemDataPositionAfterHeader = 0,
	ItemDataPositionAfterPalette = 1,
	ItemDataPositionAfterBits = 2
} ItemDataPosition;

typedef enum RotateFlipType {
	RotateNoneFlipNone = 0,
	Rotate90FlipNone = 1,
	Rotate180FlipNone = 2,
	Rotate270FlipNone = 3,
	RotateNoneFlipX = 4,
	Rotate90FlipX = 5,
	Rotate180FlipX = 6,
	Rotate270FlipX = 7,
	Rotate180FlipXY = 0,
	Rotate270FlipXY = 1, 
	RotateNoneFlipXY = 2,
	Rotate90FlipXY = 3,
	Rotate180FlipY = 4,
	Rotate270FlipY = 5,
	RotateNoneFlipY = 6,
	Rotate90FlipY = 7
} RotateFlipType;

typedef struct BitmapData {
	UINT Width;
	UINT Height;
	INT Stride;
	INT PixelFormat;  /* MSDN: "PixelFormat PixelFormat;" */
	VOID *Scan0;
	UINT_PTR Reserved;
} BitmapData;

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

typedef struct ImageCodecInfo {
	CLSID Clsid;
	GUID FormatID;
	WCHAR *CodecName;
	WCHAR *DllName;
	WCHAR *FormatDescription;
	WCHAR *FilenameExtension;
	WCHAR *MimeType;
	DWORD Flags;
	DWORD Version;
	DWORD SigCount;
	DWORD SigSize;
	BYTE *SigPattern;
	BYTE *SigMask;
} ImageCodecInfo;

/* FIXME: The order of fields is probably wrong. Please don't use this
 * structure until this problem is resolved!  Can't test because
 * ImageItemData is not supported by the redistributable GDI+ 1.0 DLL. */
typedef struct ImageItemData {
	UINT Size;
	UINT Position;
	VOID *Desc;
	UINT DescSize;
	UINT *Data;
	UINT DataSize;
	UINT Cookie;
} ImageItemData;

typedef struct PropertyItem {
	PROPID id;
	ULONG length;
	WORD type;
	VOID *value;
} PropertyItem;

#define PropertyTagGpsVer ((PROPID) 0x0000)
#define PropertyTagGpsLatitudeRef ((PROPID) 0x0001)
#define PropertyTagGpsLatitude ((PROPID) 0x0002)
#define PropertyTagGpsLongitudeRef ((PROPID) 0x0003)
#define PropertyTagGpsLongitude ((PROPID) 0x0004)
#define PropertyTagGpsAltitudeRef ((PROPID) 0x0005)
#define PropertyTagGpsAltitude ((PROPID) 0x0006)
#define PropertyTagGpsGpsTime ((PROPID) 0x0007)
#define PropertyTagGpsGpsSatellites ((PROPID) 0x0008)
#define PropertyTagGpsGpsStatus ((PROPID) 0x0009)
#define PropertyTagGpsGpsMeasureMode ((PROPID) 0x000A)
#define PropertyTagGpsGpsDop ((PROPID) 0x000B)
#define PropertyTagGpsSpeedRef ((PROPID) 0x000C)
#define PropertyTagGpsSpeed ((PROPID) 0x000D)
#define PropertyTagGpsTrackRef ((PROPID) 0x000E)
#define PropertyTagGpsTrack ((PROPID) 0x000F)
#define PropertyTagGpsImgDirRef ((PROPID) 0x0010)
#define PropertyTagGpsImgDir ((PROPID) 0x0011)
#define PropertyTagGpsMapDatum ((PROPID) 0x0012)
#define PropertyTagGpsDestLatRef ((PROPID) 0x0013)
#define PropertyTagGpsDestLat ((PROPID) 0x0014)
#define PropertyTagGpsDestLongRef ((PROPID) 0x0015)
#define PropertyTagGpsDestLong ((PROPID) 0x0016)
#define PropertyTagGpsDestBearRef ((PROPID) 0x0017)
#define PropertyTagGpsDestBear ((PROPID) 0x0018)
#define PropertyTagGpsDestDistRef ((PROPID) 0x0019)
#define PropertyTagGpsDestDist ((PROPID) 0x001A)
#define PropertyTagNewSubfileType ((PROPID) 0x00FE)
#define PropertyTagSubfileType ((PROPID) 0x00FF)
#define PropertyTagImageWidth ((PROPID) 0x0100)
#define PropertyTagImageHeight ((PROPID) 0x0101)
#define PropertyTagBitsPerSample ((PROPID) 0x0102)
#define PropertyTagCompression ((PROPID) 0x0103)
#define PropertyTagPhotometricInterp ((PROPID) 0x0106)
#define PropertyTagThreshHolding ((PROPID) 0x0107)
#define PropertyTagCellWidth ((PROPID) 0x0108)
#define PropertyTagCellHeight ((PROPID) 0x0109)
#define PropertyTagFillOrder ((PROPID) 0x010A)
#define PropertyTagDocumentName ((PROPID) 0x010D)
#define PropertyTagImageDescription ((PROPID) 0x010E)
#define PropertyTagEquipMake ((PROPID) 0x010F)
#define PropertyTagEquipModel ((PROPID) 0x0110)
#define PropertyTagStripOffsets ((PROPID) 0x0111)
#define PropertyTagOrientation ((PROPID) 0x0112)
#define PropertyTagSamplesPerPixel ((PROPID) 0x0115)
#define PropertyTagRowsPerStrip ((PROPID) 0x0116)
#define PropertyTagStripBytesCount ((PROPID) 0x0117)
#define PropertyTagMinSampleValue ((PROPID) 0x0118)
#define PropertyTagMaxSampleValue ((PROPID) 0x0119)
#define PropertyTagXResolution ((PROPID) 0x011A)
#define PropertyTagYResolution ((PROPID) 0x011B)
#define PropertyTagPlanarConfig ((PROPID) 0x011C)
#define PropertyTagPageName ((PROPID) 0x011D)
#define PropertyTagXPosition ((PROPID) 0x011E)
#define PropertyTagYPosition ((PROPID) 0x011F)
#define PropertyTagFreeOffset ((PROPID) 0x0120)
#define PropertyTagFreeByteCounts ((PROPID) 0x0121)
#define PropertyTagGrayResponseUnit ((PROPID) 0x0122)
#define PropertyTagGrayResponseCurve ((PROPID) 0x0123)
#define PropertyTagT4Option ((PROPID) 0x0124)
#define PropertyTagT6Option ((PROPID) 0x0125)
#define PropertyTagResolutionUnit ((PROPID) 0x0128)
#define PropertyTagPageNumber ((PROPID) 0x0129)
#define PropertyTagTransferFunction ((PROPID) 0x012D)
#define PropertyTagSoftwareUsed ((PROPID) 0x0131)
#define PropertyTagDateTime ((PROPID) 0x0132)
#define PropertyTagArtist ((PROPID) 0x013B)
#define PropertyTagHostComputer ((PROPID) 0x013C)
#define PropertyTagPredictor ((PROPID) 0x013D)
#define PropertyTagWhitePoint ((PROPID) 0x013E)
#define PropertyTagPrimaryChromaticities ((PROPID) 0x013F)
#define PropertyTagColorMap ((PROPID) 0x0140)
#define PropertyTagHalftoneHints ((PROPID) 0x0141)
#define PropertyTagTileWidth ((PROPID) 0x0142)
#define PropertyTagTileLength ((PROPID) 0x0143)
#define PropertyTagTileOffset ((PROPID) 0x0144)
#define PropertyTagTileByteCounts ((PROPID) 0x0145)
#define PropertyTagInkSet ((PROPID) 0x014C)
#define PropertyTagInkNames ((PROPID) 0x014D)
#define PropertyTagNumberOfInks ((PROPID) 0x014E)
#define PropertyTagDotRange ((PROPID) 0x0150)
#define PropertyTagTargetPrinter ((PROPID) 0x0151)
#define PropertyTagExtraSamples ((PROPID) 0x0152)
#define PropertyTagSampleFormat ((PROPID) 0x0153)
#define PropertyTagSMinSampleValue ((PROPID) 0x0154)
#define PropertyTagSMaxSampleValue ((PROPID) 0x0155)
#define PropertyTagTransferRange ((PROPID) 0x0156)
#define PropertyTagJPEGProc ((PROPID) 0x0200)
#define PropertyTagJPEGInterFormat ((PROPID) 0x0201)
#define PropertyTagJPEGInterLength ((PROPID) 0x0202)
#define PropertyTagJPEGRestartInterval ((PROPID) 0x0203)
#define PropertyTagJPEGLosslessPredictors ((PROPID) 0x0205)
#define PropertyTagJPEGPointTransforms ((PROPID) 0x0206)
#define PropertyTagJPEGQTables ((PROPID) 0x0207)
#define PropertyTagJPEGDCTables ((PROPID) 0x0208)
#define PropertyTagJPEGACTables ((PROPID) 0x0209)
#define PropertyTagYCbCrCoefficients ((PROPID) 0x0211)
#define PropertyTagYCbCrSubsampling ((PROPID) 0x0212)
#define PropertyTagYCbCrPositioning ((PROPID) 0x0213)
#define PropertyTagREFBlackWhite ((PROPID) 0x0214)
#define PropertyTagGamma ((PROPID) 0x0301)
#define PropertyTagICCProfileDescriptor ((PROPID) 0x0302)
#define PropertyTagSRGBRenderingIntent ((PROPID) 0x0303)
#define PropertyTagImageTitle ((PROPID) 0x0320)
#define PropertyTagResolutionXUnit ((PROPID) 0x5001)
#define PropertyTagResolutionYUnit ((PROPID) 0x5002)
#define PropertyTagResolutionXLengthUnit ((PROPID) 0x5003)
#define PropertyTagResolutionYLengthUnit ((PROPID) 0x5004)
#define PropertyTagPrintFlags ((PROPID) 0x5005)
#define PropertyTagPrintFlagsVersion ((PROPID) 0x5006)
#define PropertyTagPrintFlagsCrop ((PROPID) 0x5007)
#define PropertyTagPrintFlagsBleedWidth ((PROPID) 0x5008)
#define PropertyTagPrintFlagsBleedWidthScale ((PROPID) 0x5009)
#define PropertyTagHalftoneLPI ((PROPID) 0x500A)
#define PropertyTagHalftoneLPIUnit ((PROPID) 0x500B)
#define PropertyTagHalftoneDegree ((PROPID) 0x500C)
#define PropertyTagHalftoneShape ((PROPID) 0x500D)
#define PropertyTagHalftoneMisc ((PROPID) 0x500E)
#define PropertyTagHalftoneScreen ((PROPID) 0x500F)
#define PropertyTagJPEGQuality ((PROPID) 0x5010)
#define PropertyTagGridSize ((PROPID) 0x5011)
#define PropertyTagThumbnailFormat ((PROPID) 0x5012)
#define PropertyTagThumbnailWidth ((PROPID) 0x5013)
#define PropertyTagThumbnailHeight ((PROPID) 0x5014)
#define PropertyTagThumbnailColorDepth ((PROPID) 0x5015)
#define PropertyTagThumbnailPlanes ((PROPID) 0x5016)
#define PropertyTagThumbnailRawBytes ((PROPID) 0x5017)
#define PropertyTagThumbnailSize ((PROPID) 0x5018)
#define PropertyTagThumbnailCompressedSize ((PROPID) 0x5019)
#define PropertyTagColorTransferFunction ((PROPID) 0x501A)
#define PropertyTagThumbnailData ((PROPID) 0x501B)
#define PropertyTagThumbnailImageWidth ((PROPID) 0x5020)
#define PropertyTagThumbnailImageHeight ((PROPID) 0x5021)
#define PropertyTagThumbnailBitsPerSample ((PROPID) 0x5022)
#define PropertyTagThumbnailCompression ((PROPID) 0x5023)
#define PropertyTagThumbnailPhotometricInterp ((PROPID) 0x5024)
#define PropertyTagThumbnailImageDescription ((PROPID) 0x5025)
#define PropertyTagThumbnailEquipMake ((PROPID) 0x5026)
#define PropertyTagThumbnailEquipModel ((PROPID) 0x5027)
#define PropertyTagThumbnailStripOffsets ((PROPID) 0x5028)
#define PropertyTagThumbnailOrientation ((PROPID) 0x5029)
#define PropertyTagThumbnailSamplesPerPixel ((PROPID) 0x502A)
#define PropertyTagThumbnailRowsPerStrip ((PROPID) 0x502B)
#define PropertyTagThumbnailStripBytesCount ((PROPID) 0x502C)
#define PropertyTagThumbnailResolutionX ((PROPID) 0x502D)
#define PropertyTagThumbnailResolutionY ((PROPID) 0x502E)
#define PropertyTagThumbnailPlanarConfig ((PROPID) 0x502F)
#define PropertyTagThumbnailResolutionUnit ((PROPID) 0x5030)
#define PropertyTagThumbnailTransferFunction ((PROPID) 0x5031)
#define PropertyTagThumbnailSoftwareUsed ((PROPID) 0x5032)
#define PropertyTagThumbnailDateTime ((PROPID) 0x5033)
#define PropertyTagThumbnailArtist ((PROPID) 0x5034)
#define PropertyTagThumbnailWhitePoint ((PROPID) 0x5035)
#define PropertyTagThumbnailPrimaryChromaticities ((PROPID) 0x5036)
#define PropertyTagThumbnailYCbCrCoefficients ((PROPID) 0x5037)
#define PropertyTagThumbnailYCbCrSubsampling ((PROPID) 0x5038)
#define PropertyTagThumbnailYCbCrPositioning ((PROPID) 0x5039)
#define PropertyTagThumbnailRefBlackWhite ((PROPID) 0x503A)
#define PropertyTagThumbnailCopyRight ((PROPID) 0x503B)
#define PropertyTagLuminanceTable ((PROPID) 0x5090)
#define PropertyTagChrominanceTable ((PROPID) 0x5091)
#define PropertyTagFrameDelay ((PROPID) 0x5100)
#define PropertyTagLoopCount ((PROPID) 0x5101)
#define PropertyTagGlobalPalette ((PROPID) 0x5102)
#define PropertyTagIndexBackground ((PROPID) 0x5103)
#define PropertyTagIndexTransparent ((PROPID) 0x5104)
#define PropertyTagPixelUnit ((PROPID) 0x5110)
#define PropertyTagPixelPerUnitX ((PROPID) 0x5111)
#define PropertyTagPixelPerUnitY ((PROPID) 0x5112)
#define PropertyTagPaletteHistogram ((PROPID) 0x5113)
#define PropertyTagCopyright ((PROPID) 0x8298)
#define PropertyTagExifExposureTime ((PROPID) 0x829A)
#define PropertyTagExifFNumber ((PROPID) 0x829D)
#define PropertyTagExifIFD ((PROPID) 0x8769)
#define PropertyTagICCProfile ((PROPID) 0x8773)
#define PropertyTagExifExposureProg ((PROPID) 0x8822)
#define PropertyTagExifSpectralSense ((PROPID) 0x8824)
#define PropertyTagGpsIFD ((PROPID) 0x8825)
#define PropertyTagExifISOSpeed ((PROPID) 0x8827)
#define PropertyTagExifOECF ((PROPID) 0x8828)
#define PropertyTagExifVer ((PROPID) 0x9000)
#define PropertyTagExifDTOrig ((PROPID) 0x9003)
#define PropertyTagExifDTDigitized ((PROPID) 0x9004)
#define PropertyTagExifCompConfig ((PROPID) 0x9101)
#define PropertyTagExifCompBPP ((PROPID) 0x9102)
#define PropertyTagExifShutterSpeed ((PROPID) 0x9201)
#define PropertyTagExifAperture ((PROPID) 0x9202)
#define PropertyTagExifBrightness ((PROPID) 0x9203)
#define PropertyTagExifExposureBias ((PROPID) 0x9204)
#define PropertyTagExifMaxAperture ((PROPID) 0x9205)
#define PropertyTagExifSubjectDist ((PROPID) 0x9206)
#define PropertyTagExifMeteringMode ((PROPID) 0x9207)
#define PropertyTagExifLightSource ((PROPID) 0x9208)
#define PropertyTagExifFlash ((PROPID) 0x9209)
#define PropertyTagExifFocalLength ((PROPID) 0x920A)
#define PropertyTagExifMakerNote ((PROPID) 0x927C)
#define PropertyTagExifUserComment ((PROPID) 0x9286)
#define PropertyTagExifDTSubsec ((PROPID) 0x9290)
#define PropertyTagExifDTOrigSS ((PROPID) 0x9291)
#define PropertyTagExifDTDigSS ((PROPID) 0x9292)
#define PropertyTagExifFPXVer ((PROPID) 0xA000)
#define PropertyTagExifColorSpace ((PROPID) 0xA001)
#define PropertyTagExifPixXDim ((PROPID) 0xA002)
#define PropertyTagExifPixYDim ((PROPID) 0xA003)
#define PropertyTagExifRelatedWav ((PROPID) 0xA004)
#define PropertyTagExifInterop ((PROPID) 0xA005)
#define PropertyTagExifFlashEnergy ((PROPID) 0xA20B)
#define PropertyTagExifSpatialFR ((PROPID) 0xA20C)
#define PropertyTagExifFocalXRes ((PROPID) 0xA20E)
#define PropertyTagExifFocalYRes ((PROPID) 0xA20F)
#define PropertyTagExifFocalResUnit ((PROPID) 0xA210)
#define PropertyTagExifSubjectLoc ((PROPID) 0xA214)
#define PropertyTagExifExposureIndex ((PROPID) 0xA215)
#define PropertyTagExifSensingMethod ((PROPID) 0xA217)
#define PropertyTagExifFileSource ((PROPID) 0xA300)
#define PropertyTagExifSceneType ((PROPID) 0xA301)
#define PropertyTagExifCfaPattern ((PROPID) 0xA302)

#define PropertyTagTypeByte ((WORD) 1)
#define PropertyTagTypeASCII ((WORD) 2)
#define PropertyTagTypeShort ((WORD) 3)
#define PropertyTagTypeLong ((WORD) 4)
#define PropertyTagTypeRational ((WORD) 5)
#define PropertyTagTypeUndefined ((WORD) 7)
#define PropertyTagTypeSLONG ((WORD) 9)
#define PropertyTagTypeSRational ((WORD) 10)


extern const GUID EncoderChrominanceTable;   /* f2e455dc-09b3-4316-8260-676ada32481c */
extern const GUID EncoderColorDepth;         /* 66087055-ad66-4c7c-9a18-38a2310b8337 */
extern const GUID EncoderColorSpace;         /* ? */
extern const GUID EncoderCompression;        /* e09d739d-ccd4-44ee-8eba-3fbf8be4fc58 */
extern const GUID EncoderImageItems;         /* ? */
extern const GUID EncoderLuminanceTable;     /* edb33bce-0266-4a77-b904-27216099e717 */
extern const GUID EncoderQuality;            /* 1d5be4b5-fa4a-452d-9cdd-5db35105e7eb */
extern const GUID EncoderRenderMethod;       /* 6d42c53a-229a-4825-8bb7-5c99e2b9a8b8 */
extern const GUID EncoderSaveAsCMYK;         /* ? */
extern const GUID EncoderSaveFlag;           /* 292266fc-ac40-47bf-8cfc-a85b89a655de */
extern const GUID EncoderScanMethod;         /* 3a4e2661-3109-4e56-8536-42c156e7dcfa */
extern const GUID EncoderTransformation;     /* 8d0eb2d1-a58e-4ea8-aa14-108074b7b6f9 */
extern const GUID EncoderVersion;            /* 24d18c76-814a-41a4-bf53-1c219cccf797 */

extern const GUID ImageFormatBMP;            /* b96b3cab-0728-11d3-9d7b-0000f81ef32e */
extern const GUID ImageFormatEMF;            /* b96b3cac-0728-11d3-9d7b-0000f81ef32e */
extern const GUID ImageFormatEXIF;           /* ? */
extern const GUID ImageFormatGIF;            /* b96b3cb0-0728-11d3-9d7b-0000f81ef32e */
extern const GUID ImageFormatIcon;           /* b96b3cb5-0728-11d3-9d7b-0000f81ef32e */
extern const GUID ImageFormatJPEG;           /* b96b3cae-0728-11d3-9d7b-0000f81ef32e */
extern const GUID ImageFormatMemoryBMP;      /* b96b3caa-0728-11d3-9d7b-0000f81ef32e */
extern const GUID ImageFormatPNG;            /* b96b3caf-0728-11d3-9d7b-0000f81ef32e */
extern const GUID ImageFormatTIFF;           /* b96b3cb1-0728-11d3-9d7b-0000f81ef32e */
extern const GUID ImageFormatUndefined;      /* ? */
extern const GUID ImageFormatWMF;            /* b96b3cad-0728-11d3-9d7b-0000f81ef32e */

extern const GUID FrameDimensionPage;        /* 7462dc86-6180-4c7e-8e3f-ee7333a7a483 */
extern const GUID FrameDimensionResolution;  /* ? */
extern const GUID FrameDimensionTime;        /* 6aedbd6d-3fb5-418a-83a6-7f45229dc872 */



/*
 * FIXME: is 1 the correct value for GDIP_EMFPLUSFLAGS_DISPLAY? This number
 * has been determined by calling Metafile::GetMetafileHeader() on a EMF+
 * metafile which was recorded on a display device context (SampleMetafile.emf).
 */
#define GDIP_EMFPLUSFLAGS_DISPLAY ((UINT) 1)

typedef struct tagENHMETAHEADER3 {
	DWORD iType;
	DWORD nSize;
	RECTL rclBounds;
	RECTL rclFrame;
	DWORD dSignature;
	DWORD nVersion;
	DWORD nBytes;
	DWORD nRecords;
	WORD nHandles;
	WORD sReserved;
	DWORD nDescription;
	DWORD offDescription;
	DWORD nPalEntries;
	SIZEL szlDevice;
	SIZEL szlMillimeters;
} ENHMETAHEADER3,*LPENHMETAHEADER3;

typedef struct PWMFRect16 {
	INT16 Left;
	INT16 Top;
	INT16 Right;
	INT16 Bottom;
} PWMFRect16;

typedef struct WmfPlaceableFileHeader {
	UINT32 Key;
	INT16 Hmf;
	PWMFRect16 BoundingBox;
	INT16 Inch;
	UINT32 Reserved;
	INT16 Checksum;
} WmfPlaceableFileHeader;

typedef struct MetafileHeader {
	MetafileType Type;
	UINT Size;
	UINT Version;
	UINT EmfPlusFlags;
	REAL DpiX;
	REAL DpiY;
	INT X;
	INT Y;
	INT Width;
	INT Height;
	__extension__ union {
		METAHEADER WmfHeader;
		ENHMETAHEADER3 EmfHeader;
	};
	INT EmfPlusHeaderSize;
	INT LogicalDpiX;
	INT LogicalDpiY;

} MetafileHeader;
/* ------------------------------------------------------------------ effects */
typedef enum CurveAdjustments {
	AdjustExposure = 0,
	AdjustDensity = 1,
	AdjustContrast = 2,
	AdjustHighlight = 3,
	AdjustShadow = 4,
	AdjustMidtone = 5,
	AdjustWhiteSaturation = 6,
	AdjustBlackSaturation = 7
} CurveAdjustments;

typedef enum CurveChannel {
	CurveChannelAll = 0,
	CurveChannelRed = 1,
	CurveChannelGreen = 2,
	CurveChannelBlue = 3
} CurveChannel;

typedef struct BlurParams {
	REAL radius;
	BOOL expandEdge;
} BlurParams;

typedef struct BrightnessContrastParams {
	INT brightnessLevel;
	INT contrastLevel;
} BrightnessContrastParams;

typedef struct ColorBalanceParams {
	INT cyanRed;
	INT magentaGreen;
	INT yellowBlue;
} ColorBalanceParams;

typedef struct ColorCurveParams {
	CurveAdjustments adjustment;
	CurveChannel channel;
	INT adjustValue;
} ColorCurveParams;

typedef struct ColorLUTParams {
	ColorChannelLUT lutB;
	ColorChannelLUT lutG;
	ColorChannelLUT lutR;
	ColorChannelLUT lutA;
} ColorLUTParams;

typedef struct HueSaturationLightnessParams {
	INT hueLevel;
	INT saturationLevel;
	INT lightnessLevel;
} HueSaturationLightnessParams;

typedef struct LevelsParams {
	INT highlight;
	INT midtone;
	INT shadow;
} LevelsParams;

typedef struct RedEyeCorrectionParams {
	UINT numberOfAreas;
	RECT *areas;
} RedEyeCorrectionParams;

typedef struct SharpenParams {
	REAL radius;
	REAL amount;
} SharpenParams;

typedef struct TintParams {
	INT hue;
	INT amount;
} TintParams;

extern const GUID BlurEffectGuid;                    /* ? */
extern const GUID BrightnessContrastEffectGuid;      /* ? */
extern const GUID ColorBalanceEffectGuid;            /* ? */
extern const GUID ColorCurveEffectGuid;              /* ? */
extern const GUID ColorLUTEffectGuid;                /* ? */
extern const GUID ColorMatrixEffectGuid;             /* ? */
extern const GUID HueSaturationLightnessEffectGuid;  /* ? */
extern const GUID LevelsEffectGuid;                  /* ? */
extern const GUID RedEyeCorrectionEffectGuid;        /* ? */
extern const GUID SharpenEffectGuid;                 /* ? */
extern const GUID TintEffectGuid;                    /* ? */
/* ------------------------------------------------------------------ startup */
typedef struct GdiplusStartupInput {
	UINT32 GdiplusVersion;
	DebugEventProc DebugEventCallback;
	BOOL SuppressBackgroundThread;
	BOOL SuppressExternalCodecs;

} GdiplusStartupInput;

typedef GpStatus (WINAPI *NotificationHookProc)(ULONG_PTR *token);
typedef VOID (WINAPI *NotificationUnhookProc)(ULONG_PTR token);

typedef struct GdiplusStartupOutput {
	NotificationHookProc NotificationHook;
	NotificationUnhookProc NotificationUnhook;

} GdiplusStartupOutput;


WINGDIPAPI GpStatus WINAPI GdiplusStartup(ULONG_PTR*,const GdiplusStartupInput*,GdiplusStartupOutput*);
WINGDIPAPI void* WINAPI GdipAlloc(SIZE_T);
WINGDIPAPI void WINAPI GdipFree(void*);
WINGDIPAPI VOID WINAPI GdiplusShutdown(ULONG_PTR);
WINGDIPAPI GpStatus WINAPI GdiplusNotificationHook(ULONG_PTR*);
WINGDIPAPI VOID WINAPI GdiplusNotificationUnhook(ULONG_PTR);
/* ------------------------------------------------------------------ objects */
typedef Point GpPoint;
typedef PointF GpPointF;
typedef Rect GpRect;
typedef RectF GpRectF;
typedef Size GpSize;
typedef SizeF GpSizeF;
typedef enum BrushType GpBrushType;
typedef enum CombineMode GpCombineMode;
typedef enum CompositingMode GpCompositingMode;
typedef enum CompositingQuality GpCompositingQuality;
typedef enum CoordinateSpace GpCoordinateSpace;
typedef enum CustomLineCapType GpCustomLineCapType;
typedef enum DashCap GpDashCap;
typedef enum DashStyle GpDashStyle;
typedef enum DitherType GpDitherType;
typedef enum DriverStringOptions GpDriverStringOptions;
typedef enum EmfPlusRecordType GpEmfPlusRecordType;
typedef enum EmfToWmfBitsFlags GpEmfToWmfBitsFlags;
typedef enum EmfType GpEmfType;
typedef enum EncoderParameterValueType GpEncoderParameterValueType;
typedef enum EncoderValue GpEncoderValue;
typedef enum FillMode GpFillMode;
typedef enum FlushIntention GpFlushIntention;
typedef enum FontStyle GpFontStyle;
typedef enum HatchStyle GpHatchStyle;
typedef enum HotkeyPrefix GpHotkeyPrefix;
typedef enum ImageType GpImageType;
typedef enum InterpolationMode GpInterpolationMode;
typedef enum LinearGradientMode GpLinearGradientMode;
typedef enum LineCap GpLineCap;
typedef enum LineJoin GpLineJoin;
typedef enum MatrixOrder GpMatrixOrder;
typedef enum MetafileFrameUnit GpMetafileFrameUnit;
typedef enum MetafileType GpMetafileType;
typedef enum ObjectType GpObjectType;
typedef enum PathPointType GpPathPointType;
typedef enum PenAlignment GpPenAlignment;
typedef enum PenType GpPenType;
typedef enum PixelOffsetMode GpPixelOffsetMode;
typedef enum QualityMode GpQualityMode;
typedef enum SmoothingMode GpSmoothingMode;
typedef enum StringAlignment GpStringAlignment;
typedef enum StringDigitSubstitute GpStringDigitSubstitute;
typedef enum StringFormatFlags GpStringFormatFlags;
typedef enum StringTrimming GpStringTrimming;
typedef enum TextRenderingHint GpTextRenderingHint;
typedef enum Unit GpUnit;
typedef enum WarpMode GpWarpMode;
typedef enum WrapMode GpWrapMode;
typedef void GpAdjustableArrowCap;
typedef void GpBitmap;
typedef void GpBrush;
typedef void GpCachedBitmap;
typedef void GpCustomLineCap;
typedef void GpFont;
typedef void GpFontFamily;
typedef void GpFontCollection;
typedef void GpGraphics;
typedef void GpHatch;
typedef void GpImage;
typedef void GpImageAttributes;
typedef void GpLineGradient;
typedef void GpMatrix;
typedef void GpMetafile;
typedef void GpPath;
typedef void GpPathData;
typedef void GpPathGradient;
typedef void GpPathIterator;
typedef void GpPen;
typedef void GpRegion;
typedef void GpSolidFill;
typedef void GpStringFormat;
typedef void GpTexture;

typedef void CGpEffect;
typedef void GpAdjustableArrowCap;
typedef void GpBitmap;
typedef void GpBrush;
typedef void GpCachedBitmap;
typedef void GpCustomLineCap;
typedef void GpFont;
typedef void GpFontFamily;
typedef void GpFontCollection;
typedef void GpGraphics;
typedef void GpHatch;
typedef void GpImage;
typedef void GpImageAttributes;
typedef void GpLineGradient;
typedef void GpMatrix;
typedef void GpMetafile;
typedef void GpPath;
typedef void GpPathData;
typedef void GpPathGradient;
typedef void GpPathIterator;
typedef void GpPen;
typedef void GpRegion;
typedef void GpSolidFill;
typedef void GpStringFormat;
typedef void GpTexture;

/* ------------------------------------------------------------------ functions */

/* AdjustableArrowCap functions */
WINGDIPAPI GpStatus WINAPI GdipCreateAdjustableArrowCap(REAL,REAL,BOOL,GpAdjustableArrowCap**);
WINGDIPAPI GpStatus WINAPI GdipSetAdjustableArrowCapHeight(GpAdjustableArrowCap*,REAL);
WINGDIPAPI GpStatus WINAPI GdipGetAdjustableArrowCapHeight(GpAdjustableArrowCap*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipSetAdjustableArrowCapWidth(GpAdjustableArrowCap*,REAL);
WINGDIPAPI GpStatus WINAPI GdipGetAdjustableArrowCapWidth(GpAdjustableArrowCap*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipSetAdjustableArrowCapMiddleInset(GpAdjustableArrowCap*,REAL);
WINGDIPAPI GpStatus WINAPI GdipGetAdjustableArrowCapMiddleInset(GpAdjustableArrowCap*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipSetAdjustableArrowCapFillState(GpAdjustableArrowCap*,BOOL);
WINGDIPAPI GpStatus WINAPI GdipGetAdjustableArrowCapFillState(GpAdjustableArrowCap*,BOOL*);

/* Bitmap functions */
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromStream(IStream*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromFile(const WCHAR*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromStreamICM(IStream*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromFileICM(const WCHAR*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromScan0(INT,INT,INT,PixelFormat,BYTE*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromGraphics(INT,INT,GpGraphics*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromDirectDrawSurface(IDirectDrawSurface7*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromGdiDib(const BITMAPINFO*,VOID*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromHBITMAP(HBITMAP,HPALETTE,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCreateHBITMAPFromBitmap(GpBitmap*,HBITMAP*,ARGB);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromHICON(HICON,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCreateHICONFromBitmap(GpBitmap*,HICON*);
WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromResource(HINSTANCE,const WCHAR*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCloneBitmapArea(REAL,REAL,REAL,REAL,PixelFormat,GpBitmap*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipCloneBitmapAreaI(INT,INT,INT,INT,PixelFormat,GpBitmap*,GpBitmap**);
WINGDIPAPI GpStatus WINAPI GdipBitmapLockBits(GpBitmap*,const GpRect*,UINT,PixelFormat,BitmapData*);
WINGDIPAPI GpStatus WINAPI GdipBitmapUnlockBits(GpBitmap*,BitmapData*);
WINGDIPAPI GpStatus WINAPI GdipBitmapGetPixel(GpBitmap*,INT,INT,ARGB*);
WINGDIPAPI GpStatus WINAPI GdipBitmapSetPixel(GpBitmap*,INT,INT,ARGB);
WINGDIPAPI GpStatus WINAPI GdipBitmapSetResolution(GpBitmap*,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipBitmapConvertFormat(GpBitmap*,PixelFormat,DitherType,PaletteType,ColorPalette*,REAL);
WINGDIPAPI GpStatus WINAPI GdipInitializePalette(ColorPalette*,PaletteType,INT,BOOL,GpBitmap*);
WINGDIPAPI GpStatus WINAPI GdipBitmapApplyEffect(GpBitmap*,CGpEffect*,RECT*,BOOL,VOID**,INT*);
WINGDIPAPI GpStatus WINAPI GdipBitmapCreateApplyEffect(GpBitmap**,INT,CGpEffect*,RECT*,RECT*,GpBitmap**,BOOL,VOID**,INT*);
WINGDIPAPI GpStatus WINAPI GdipBitmapGetHistogram(GpBitmap*,HistogramFormat,UINT,UINT*,UINT*,UINT*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipBitmapGetHistogramSize(HistogramFormat,UINT*);

/* Brush functions */
WINGDIPAPI GpStatus WINAPI GdipCloneBrush(GpBrush*,GpBrush**);
WINGDIPAPI GpStatus WINAPI GdipDeleteBrush(GpBrush*);
WINGDIPAPI GpStatus WINAPI GdipGetBrushType(GpBrush*,GpBrushType*);

/* CachedBitmap functions */
WINGDIPAPI GpStatus WINAPI GdipCreateCachedBitmap(GpBitmap*,GpGraphics*,GpCachedBitmap**);
WINGDIPAPI GpStatus WINAPI GdipDeleteCachedBitmap(GpCachedBitmap*);
WINGDIPAPI GpStatus WINAPI GdipDrawCachedBitmap(GpGraphics*,GpCachedBitmap*,INT,INT);

/* CustomLineCap functions */
WINGDIPAPI GpStatus WINAPI GdipCreateCustomLineCap(GpPath*,GpPath*,GpLineCap,REAL,GpCustomLineCap**);
WINGDIPAPI GpStatus WINAPI GdipDeleteCustomLineCap(GpCustomLineCap*);
WINGDIPAPI GpStatus WINAPI GdipCloneCustomLineCap(GpCustomLineCap*,GpCustomLineCap**);
WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapType(GpCustomLineCap*,CustomLineCapType*);
WINGDIPAPI GpStatus WINAPI GdipSetCustomLineCapStrokeCaps(GpCustomLineCap*,GpLineCap,GpLineCap);
WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapStrokeCaps(GpCustomLineCap*,GpLineCap*,GpLineCap*);
WINGDIPAPI GpStatus WINAPI GdipSetCustomLineCapStrokeJoin(GpCustomLineCap*,GpLineJoin);
WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapStrokeJoin(GpCustomLineCap*,GpLineJoin*);
WINGDIPAPI GpStatus WINAPI GdipSetCustomLineCapBaseCap(GpCustomLineCap*,GpLineCap);
WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapBaseCap(GpCustomLineCap*,GpLineCap*);
WINGDIPAPI GpStatus WINAPI GdipSetCustomLineCapBaseInset(GpCustomLineCap*,REAL);
WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapBaseInset(GpCustomLineCap*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipSetCustomLineCapWidthScale(GpCustomLineCap*,REAL);
WINGDIPAPI GpStatus WINAPI GdipGetCustomLineCapWidthScale(GpCustomLineCap*,REAL*);

/* Effect functions */
WINGDIPAPI GpStatus WINAPI GdipCreateEffect(const GUID,CGpEffect**);
WINGDIPAPI GpStatus WINAPI GdipDeleteEffect(CGpEffect*);
WINGDIPAPI GpStatus WINAPI GdipGetEffectParameterSize(CGpEffect*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipSetEffectParameters(CGpEffect*,const VOID*,UINT);
WINGDIPAPI GpStatus WINAPI GdipGetEffectParameters(CGpEffect*,UINT*,VOID*);

/* Font functions */
WINGDIPAPI GpStatus WINAPI GdipCreateFontFromDC(HDC,GpFont**);
WINGDIPAPI GpStatus WINAPI GdipCreateFontFromLogfontA(HDC,const LOGFONTA*,GpFont**);
WINGDIPAPI GpStatus WINAPI GdipCreateFontFromLogfontW(HDC,const LOGFONTW*,GpFont**);
WINGDIPAPI GpStatus WINAPI GdipCreateFont(const GpFontFamily*,REAL,INT,Unit,GpFont**);
WINGDIPAPI GpStatus WINAPI GdipCloneFont(GpFont*,GpFont**);
WINGDIPAPI GpStatus WINAPI GdipDeleteFont(GpFont*);
WINGDIPAPI GpStatus WINAPI GdipGetFamily(GpFont*,GpFontFamily**);
WINGDIPAPI GpStatus WINAPI GdipGetFontStyle(GpFont*,INT*);
WINGDIPAPI GpStatus WINAPI GdipGetFontSize(GpFont*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipGetFontUnit(GpFont*,Unit*);
WINGDIPAPI GpStatus WINAPI GdipGetFontHeight(const GpFont*,const GpGraphics*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipGetFontHeightGivenDPI(const GpFont*,REAL,REAL*);
WINGDIPAPI GpStatus WINAPI GdipGetLogFontA(GpFont*,GpGraphics*,LOGFONTA*);
WINGDIPAPI GpStatus WINAPI GdipGetLogFontW(GpFont*,GpGraphics*,LOGFONTW*);
WINGDIPAPI GpStatus WINAPI GdipNewInstalledFontCollection(GpFontCollection**);
WINGDIPAPI GpStatus WINAPI GdipNewPrivateFontCollection(GpFontCollection**);
WINGDIPAPI GpStatus WINAPI GdipDeletePrivateFontCollection(GpFontCollection**);
WINGDIPAPI GpStatus WINAPI GdipGetFontCollectionFamilyCount(GpFontCollection*,INT*);
WINGDIPAPI GpStatus WINAPI GdipGetFontCollectionFamilyList(GpFontCollection*,INT,GpFontFamily**,INT*);
WINGDIPAPI GpStatus WINAPI GdipPrivateAddFontFile(GpFontCollection*,const WCHAR*);
WINGDIPAPI GpStatus WINAPI GdipPrivateAddMemoryFont(GpFontCollection*,const void*,INT);

/* FontFamily functions */
WINGDIPAPI GpStatus WINAPI GdipCreateFontFamilyFromName(const WCHAR*,GpFontCollection*,GpFontFamily**);
WINGDIPAPI GpStatus WINAPI GdipDeleteFontFamily(GpFontFamily*);
WINGDIPAPI GpStatus WINAPI GdipCloneFontFamily(GpFontFamily*,GpFontFamily**);
WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilySansSerif(GpFontFamily**);
WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilySerif(GpFontFamily**);
WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilyMonospace(GpFontFamily**);
WINGDIPAPI GpStatus WINAPI GdipGetFamilyName(const GpFontFamily*,WCHAR[LF_FACESIZE],LANGID);
WINGDIPAPI GpStatus WINAPI GdipIsStyleAvailable(const GpFontFamily*,INT,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipGetEmHeight(const GpFontFamily*,INT,UINT16*);
WINGDIPAPI GpStatus WINAPI GdipGetCellAscent(const GpFontFamily*,INT,UINT16*);
WINGDIPAPI GpStatus WINAPI GdipGetCellDescent(const GpFontFamily*,INT,UINT16*);
WINGDIPAPI GpStatus WINAPI GdipGetLineSpacing(const GpFontFamily*,INT,UINT16*);

/* Graphics functions */
WINGDIPAPI GpStatus WINAPI GdipFlush(GpGraphics*,GpFlushIntention);
WINGDIPAPI GpStatus WINAPI GdipCreateFromHDC(HDC,GpGraphics**);
WINGDIPAPI GpStatus WINAPI GdipCreateFromHDC2(HDC,HANDLE,GpGraphics**);
WINGDIPAPI GpStatus WINAPI GdipCreateFromHWND(HWND,GpGraphics**);
WINGDIPAPI GpStatus WINAPI GdipCreateFromHWNDICM(HWND,GpGraphics**);
WINGDIPAPI GpStatus WINAPI GdipDeleteGraphics(GpGraphics*);
WINGDIPAPI GpStatus WINAPI GdipGetDC(GpGraphics*,HDC*);
WINGDIPAPI GpStatus WINAPI GdipReleaseDC(GpGraphics*,HDC);
WINGDIPAPI GpStatus WINAPI GdipSetCompositingMode(GpGraphics*,CompositingMode);
WINGDIPAPI GpStatus WINAPI GdipGetCompositingMode(GpGraphics*,CompositingMode*);
WINGDIPAPI GpStatus WINAPI GdipSetRenderingOrigin(GpGraphics*,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipGetRenderingOrigin(GpGraphics*,INT*,INT*);
WINGDIPAPI GpStatus WINAPI GdipSetCompositingQuality(GpGraphics*,CompositingQuality);
WINGDIPAPI GpStatus WINAPI GdipGetCompositingQuality(GpGraphics*,CompositingQuality*);
WINGDIPAPI GpStatus WINAPI GdipSetSmoothingMode(GpGraphics*,SmoothingMode);
WINGDIPAPI GpStatus WINAPI GdipGetSmoothingMode(GpGraphics*,SmoothingMode*);
WINGDIPAPI GpStatus WINAPI GdipSetPixelOffsetMode(GpGraphics*,PixelOffsetMode);
WINGDIPAPI GpStatus WINAPI GdipGetPixelOffsetMode(GpGraphics*,PixelOffsetMode*);
WINGDIPAPI GpStatus WINAPI GdipSetTextRenderingHint(GpGraphics*,TextRenderingHint);
WINGDIPAPI GpStatus WINAPI GdipGetTextRenderingHint(GpGraphics*,TextRenderingHint*);
WINGDIPAPI GpStatus WINAPI GdipSetTextContrast(GpGraphics*,UINT);
WINGDIPAPI GpStatus WINAPI GdipGetTextContrast(GpGraphics*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipSetInterpolationMode(GpGraphics*,InterpolationMode);
WINGDIPAPI GpStatus WINAPI GdipGraphicsSetAbort(GpGraphics*,GdiplusAbort*);
WINGDIPAPI GpStatus WINAPI GdipGetInterpolationMode(GpGraphics*,InterpolationMode*);
WINGDIPAPI GpStatus WINAPI GdipSetWorldTransform(GpGraphics*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipResetWorldTransform(GpGraphics*);
WINGDIPAPI GpStatus WINAPI GdipMultiplyWorldTransform(GpGraphics*,const GpMatrix*,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipTranslateWorldTransform(GpGraphics*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipScaleWorldTransform(GpGraphics*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipRotateWorldTransform(GpGraphics*,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipGetWorldTransform(GpGraphics*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipResetPageTransform(GpGraphics*);
WINGDIPAPI GpStatus WINAPI GdipGetPageUnit(GpGraphics*,GpUnit*);
WINGDIPAPI GpStatus WINAPI GdipGetPageScale(GpGraphics*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipSetPageUnit(GpGraphics*,GpUnit);
WINGDIPAPI GpStatus WINAPI GdipSetPageScale(GpGraphics*,REAL);
WINGDIPAPI GpStatus WINAPI GdipGetDpiX(GpGraphics*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipGetDpiY(GpGraphics*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipTransformPoints(GpGraphics*,GpCoordinateSpace,GpCoordinateSpace,GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipTransformPointsI(GpGraphics*,GpCoordinateSpace,GpCoordinateSpace,GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetNearestColor(GpGraphics*,ARGB*);
WINGDIPAPI HPALETTE WINAPI GdipCreateHalftonePalette(void);
WINGDIPAPI GpStatus WINAPI GdipDrawLine(GpGraphics*,GpPen*,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawLineI(GpGraphics*,GpPen*,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawLines(GpGraphics*,GpPen*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawLinesI(GpGraphics*,GpPen*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawArc(GpGraphics*,GpPen*,REAL,REAL,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawArcI(GpGraphics*,GpPen*,INT,INT,INT,INT,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawBezier(GpGraphics*,GpPen*,REAL,REAL,REAL,REAL,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawBezierI(GpGraphics*,GpPen*,INT,INT,INT,INT,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawBeziers(GpGraphics*,GpPen*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawBeziersI(GpGraphics*,GpPen*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawRectangle(GpGraphics*,GpPen*,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawRectangleI(GpGraphics*,GpPen*,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawRectangles(GpGraphics*,GpPen*,const GpRectF*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawRectanglesI(GpGraphics*,GpPen*,const GpRect*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawEllipse(GpGraphics*,GpPen*,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawEllipseI(GpGraphics*,GpPen*,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawPie(GpGraphics*,GpPen*,REAL,REAL,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawPieI(GpGraphics*,GpPen*,INT,INT,INT,INT,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawPolygon(GpGraphics*,GpPen*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawPolygonI(GpGraphics*,GpPen*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawPath(GpGraphics*,GpPen*,GpPath*);
WINGDIPAPI GpStatus WINAPI GdipDrawCurve(GpGraphics*,GpPen*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawCurveI(GpGraphics*,GpPen*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawCurve2(GpGraphics*,GpPen*,const GpPointF*,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawCurve2I(GpGraphics*,GpPen*,const GpPoint*,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawCurve3(GpGraphics*,GpPen*,const GpPointF*,INT,INT,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawCurve3I(GpGraphics*,GpPen*,const GpPoint*,INT,INT,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawClosedCurve(GpGraphics*,GpPen*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawClosedCurveI(GpGraphics*,GpPen*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawClosedCurve2(GpGraphics*,GpPen*,const GpPointF*,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawClosedCurve2I(GpGraphics*,GpPen*,const GpPoint*,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipGraphicsClear(GpGraphics*,ARGB);
WINGDIPAPI GpStatus WINAPI GdipFillRectangle(GpGraphics*,GpBrush*,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipFillRectangleI(GpGraphics*,GpBrush*,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipFillRectangles(GpGraphics*,GpBrush*,const GpRectF*,INT);
WINGDIPAPI GpStatus WINAPI GdipFillRectanglesI(GpGraphics*,GpBrush*,const GpRect*,INT);
WINGDIPAPI GpStatus WINAPI GdipFillPolygon(GpGraphics*,GpBrush*,const GpPointF*,INT,GpFillMode);
WINGDIPAPI GpStatus WINAPI GdipFillPolygonI(GpGraphics*,GpBrush*,const GpPoint*,INT,GpFillMode);
WINGDIPAPI GpStatus WINAPI GdipFillPolygon2(GpGraphics*,GpBrush*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipFillPolygon2I(GpGraphics*,GpBrush*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipFillEllipse(GpGraphics*,GpBrush*,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipFillEllipseI(GpGraphics*,GpBrush*,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipFillPie(GpGraphics*,GpBrush*,REAL,REAL,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipFillPieI(GpGraphics*,GpBrush*,INT,INT,INT,INT,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipFillPath(GpGraphics*,GpBrush*,GpPath*);
WINGDIPAPI GpStatus WINAPI GdipFillClosedCurve(GpGraphics*,GpBrush*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipFillClosedCurveI(GpGraphics*,GpBrush*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipFillClosedCurve2(GpGraphics*,GpBrush*,const GpPointF*,INT,REAL,GpFillMode);
WINGDIPAPI GpStatus WINAPI GdipFillClosedCurve2I(GpGraphics*,GpBrush*,const GpPoint*,INT,REAL,GpFillMode);
WINGDIPAPI GpStatus WINAPI GdipFillRegion(GpGraphics*,GpBrush*,GpRegion*);
WINGDIPAPI GpStatus WINAPI GdipDrawImage(GpGraphics*,GpImage*,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawImageI(GpGraphics*,GpImage*,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawImageRect(GpGraphics*,GpImage*,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipDrawImageRectI(GpGraphics*,GpImage*,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawImagePoints(GpGraphics*,GpImage*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsI(GpGraphics*,GpImage*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipDrawImagePointRect(GpGraphics*,GpImage*,REAL,REAL,REAL,REAL,REAL,REAL,GpUnit);
WINGDIPAPI GpStatus WINAPI GdipDrawImagePointRectI(GpGraphics*,GpImage*,INT,INT,INT,INT,INT,INT,GpUnit);
WINGDIPAPI GpStatus WINAPI GdipDrawImageRectRect(GpGraphics*,GpImage*,REAL,REAL,REAL,REAL,REAL,REAL,REAL,REAL,GpUnit,const GpImageAttributes*,DrawImageAbort,VOID*);
WINGDIPAPI GpStatus WINAPI GdipDrawImageRectRectI(GpGraphics*,GpImage*,INT,INT,INT,INT,INT,INT,INT,INT,GpUnit,const GpImageAttributes*,DrawImageAbort,VOID*);
WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsRect(GpGraphics*,GpImage*,const GpPointF*,INT,REAL,REAL,REAL,REAL,GpUnit,const GpImageAttributes*,DrawImageAbort,VOID*);
WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsRectI(GpGraphics*,GpImage*,const GpPoint*,INT,INT,INT,INT,INT,GpUnit,const GpImageAttributes*,DrawImageAbort,VOID*);
WINGDIPAPI GpStatus WINAPI GdipDrawImageFX(GpGraphics*,GpImage*,GpRectF*,GpMatrix*,CGpEffect*,GpImageAttributes*,GpUnit);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPoint(GpGraphics*,const GpMetafile*,const PointF*,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPointI(GpGraphics*,const GpMetafile*,const Point*,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestRect(GpGraphics*,const GpMetafile*,const RectF*,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestRectI(GpGraphics*,const GpMetafile*,const Rect*,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPoints(GpGraphics*,const GpMetafile*,const PointF*,INT,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPointsI(GpGraphics*,const GpMetafile*,const Point*,INT,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPoint(GpGraphics*,const GpMetafile*,const PointF*,const RectF*,Unit,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPointI(GpGraphics*,const GpMetafile*,const Point*,const Rect*,Unit,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestRect(GpGraphics*,const GpMetafile*,const RectF*,const RectF*,Unit,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestRectI(GpGraphics*,const GpMetafile*,const Rect*,const Rect*,Unit,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPoints(GpGraphics*,const GpMetafile*,const PointF*,INT,const RectF*,Unit,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPointsI(GpGraphics*,const GpMetafile*,const Point*,INT,const Rect*,Unit,EnumerateMetafileProc,VOID*,const GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipSetClipGraphics(GpGraphics*,GpGraphics*,CombineMode);
WINGDIPAPI GpStatus WINAPI GdipSetClipRect(GpGraphics*,REAL,REAL,REAL,REAL,CombineMode);
WINGDIPAPI GpStatus WINAPI GdipSetClipRectI(GpGraphics*,INT,INT,INT,INT,CombineMode);
WINGDIPAPI GpStatus WINAPI GdipSetClipPath(GpGraphics*,GpPath*,CombineMode);
WINGDIPAPI GpStatus WINAPI GdipSetClipRegion(GpGraphics*,GpRegion*,CombineMode);
WINGDIPAPI GpStatus WINAPI GdipSetClipHrgn(GpGraphics*,HRGN,CombineMode);
WINGDIPAPI GpStatus WINAPI GdipResetClip(GpGraphics*);
WINGDIPAPI GpStatus WINAPI GdipTranslateClip(GpGraphics*,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipTranslateClipI(GpGraphics*,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipGetClip(GpGraphics*,GpRegion*);
WINGDIPAPI GpStatus WINAPI GdipGetClipBounds(GpGraphics*,GpRectF*);
WINGDIPAPI GpStatus WINAPI GdipGetClipBoundsI(GpGraphics*,GpRect*);
WINGDIPAPI GpStatus WINAPI GdipIsClipEmpty(GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipGetVisibleClipBounds(GpGraphics*,GpRectF*);
WINGDIPAPI GpStatus WINAPI GdipGetVisibleClipBoundsI(GpGraphics*,GpRect*);
WINGDIPAPI GpStatus WINAPI GdipIsVisibleClipEmpty(GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsVisiblePoint(GpGraphics*,REAL,REAL,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsVisiblePointI(GpGraphics*,INT,INT,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsVisibleRect(GpGraphics*,REAL,REAL,REAL,REAL,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsVisibleRectI(GpGraphics*,INT,INT,INT,INT,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipSaveGraphics(GpGraphics*,GraphicsState*);
WINGDIPAPI GpStatus WINAPI GdipRestoreGraphics(GpGraphics*,GraphicsState);
WINGDIPAPI GpStatus WINAPI GdipBeginContainer(GpGraphics*,const GpRectF*,const GpRectF*,GpUnit,GraphicsContainer*);
WINGDIPAPI GpStatus WINAPI GdipBeginContainerI(GpGraphics*,const GpRect*,const GpRect*,GpUnit,GraphicsContainer*);
WINGDIPAPI GpStatus WINAPI GdipBeginContainer2(GpGraphics*,GraphicsContainer*);
WINGDIPAPI GpStatus WINAPI GdipEndContainer(GpGraphics*,GraphicsContainer);
WINGDIPAPI GpStatus WINAPI GdipComment(GpGraphics*,UINT,const BYTE*);

/* GraphicsPath functions */
WINGDIPAPI GpStatus WINAPI GdipCreatePath(GpFillMode,GpPath**);
WINGDIPAPI GpStatus WINAPI GdipCreatePath2(const GpPointF*,const BYTE*,INT,GpFillMode,GpPath**);
WINGDIPAPI GpStatus WINAPI GdipCreatePath2I(const GpPoint*,const BYTE*,INT,GpFillMode,GpPath**);
WINGDIPAPI GpStatus WINAPI GdipClonePath(GpPath*,GpPath**);
WINGDIPAPI GpStatus WINAPI GdipDeletePath(GpPath*);
WINGDIPAPI GpStatus WINAPI GdipResetPath(GpPath*);
WINGDIPAPI GpStatus WINAPI GdipGetPointCount(GpPath*,INT*);
WINGDIPAPI GpStatus WINAPI GdipGetPathTypes(GpPath*,BYTE*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetPathPoints(GpPath*,GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetPathPointsI(GpPath*,GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetPathFillMode(GpPath*,GpFillMode*);
WINGDIPAPI GpStatus WINAPI GdipSetPathFillMode(GpPath*,GpFillMode);
WINGDIPAPI GpStatus WINAPI GdipGetPathData(GpPath*,GpPathData*);
WINGDIPAPI GpStatus WINAPI GdipStartPathFigure(GpPath*);
WINGDIPAPI GpStatus WINAPI GdipClosePathFigure(GpPath*);
WINGDIPAPI GpStatus WINAPI GdipClosePathFigures(GpPath*);
WINGDIPAPI GpStatus WINAPI GdipSetPathMarker(GpPath*);
WINGDIPAPI GpStatus WINAPI GdipClearPathMarkers(GpPath*);
WINGDIPAPI GpStatus WINAPI GdipReversePath(GpPath*);
WINGDIPAPI GpStatus WINAPI GdipGetPathLastPoint(GpPath*,GpPointF*);
WINGDIPAPI GpStatus WINAPI GdipAddPathLine(GpPath*,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathLine2(GpPath*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathArc(GpPath*,REAL,REAL,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathBezier(GpPath*,REAL,REAL,REAL,REAL,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathBeziers(GpPath*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathCurve(GpPath*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathCurve2(GpPath*,const GpPointF*,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathCurve3(GpPath*,const GpPointF*,INT,INT,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathClosedCurve(GpPath*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathClosedCurve2(GpPath*,const GpPointF*,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathRectangle(GpPath*,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathRectangles(GpPath*,const GpRectF*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathEllipse(GpPath*,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathPie(GpPath*,REAL,REAL,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathPolygon(GpPath*,const GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathPath(GpPath*,const GpPath*,BOOL);
WINGDIPAPI GpStatus WINAPI GdipAddPathString(GpPath*,const WCHAR*,INT,const GpFontFamily*,INT,REAL,const RectF*,const GpStringFormat*);
WINGDIPAPI GpStatus WINAPI GdipAddPathStringI(GpPath*,const WCHAR*,INT,const GpFontFamily*,INT,REAL,const Rect*,const GpStringFormat*);
WINGDIPAPI GpStatus WINAPI GdipAddPathLineI(GpPath*,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathLine2I(GpPath*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathArcI(GpPath*,INT,INT,INT,INT,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathBezierI(GpPath*,INT,INT,INT,INT,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathBeziersI(GpPath*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathCurveI(GpPath*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathCurve2I(GpPath*,const GpPoint*,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathCurve3I(GpPath*,const GpPoint*,INT,INT,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathClosedCurveI(GpPath*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathClosedCurve2I(GpPath*,const GpPoint*,INT,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathRectangleI(GpPath*,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathRectanglesI(GpPath*,const GpRect*,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathEllipseI(GpPath*,INT,INT,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipAddPathPieI(GpPath*,INT,INT,INT,INT,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipAddPathPolygonI(GpPath*,const GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipFlattenPath(GpPath*,GpMatrix*,REAL);
WINGDIPAPI GpStatus WINAPI GdipWindingModeOutline(GpPath*,GpMatrix*,REAL);
WINGDIPAPI GpStatus WINAPI GdipWidenPath(GpPath*,GpPen*,GpMatrix*,REAL);
WINGDIPAPI GpStatus WINAPI GdipWarpPath(GpPath*,GpMatrix*,const GpPointF*,INT,REAL,REAL,REAL,REAL,WarpMode,REAL);
WINGDIPAPI GpStatus WINAPI GdipTransformPath(GpPath*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipGetPathWorldBounds(GpPath*,GpRectF*,const GpMatrix*,const GpPen*);
WINGDIPAPI GpStatus WINAPI GdipGetPathWorldBoundsI(GpPath*,GpRect*,const GpMatrix*,const GpPen*);
WINGDIPAPI GpStatus WINAPI GdipIsVisiblePathPoint(GpPath*,REAL,REAL,GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsVisiblePathPointI(GpPath*,INT,INT,GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsOutlineVisiblePathPoint(GpPath*,REAL,REAL,GpPen*,GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsOutlineVisiblePathPointI(GpPath*,INT,INT,GpPen*,GpGraphics*,BOOL*);

/* HatchBrush functions */
WINGDIPAPI GpStatus WINAPI GdipCreateHatchBrush(GpHatchStyle,ARGB,ARGB,GpHatch**);
WINGDIPAPI GpStatus WINAPI GdipGetHatchStyle(GpHatch*,GpHatchStyle*);
WINGDIPAPI GpStatus WINAPI GdipGetHatchForegroundColor(GpHatch*,ARGB*);
WINGDIPAPI GpStatus WINAPI GdipGetHatchBackgroundColor(GpHatch*,ARGB*);

/* Image functions */
WINGDIPAPI GpStatus WINAPI GdipLoadImageFromStream(IStream*,GpImage**);
WINGDIPAPI GpStatus WINAPI GdipLoadImageFromFile(const WCHAR*,GpImage**);
WINGDIPAPI GpStatus WINAPI GdipLoadImageFromStreamICM(IStream*,GpImage**);
WINGDIPAPI GpStatus WINAPI GdipLoadImageFromFileICM(const WCHAR*,GpImage**);
WINGDIPAPI GpStatus WINAPI GdipCloneImage(GpImage*,GpImage**);
WINGDIPAPI GpStatus WINAPI GdipDisposeImage(GpImage*);
WINGDIPAPI GpStatus WINAPI GdipSaveImageToFile(GpImage*,const WCHAR*,const CLSID*,const EncoderParameters*);
WINGDIPAPI GpStatus WINAPI GdipSaveImageToStream(GpImage*,IStream*,const CLSID*,const EncoderParameters*);
WINGDIPAPI GpStatus WINAPI GdipSaveAdd(GpImage*,const EncoderParameters*);
WINGDIPAPI GpStatus WINAPI GdipSaveAddImage(GpImage*,GpImage*,const EncoderParameters*);
WINGDIPAPI GpStatus WINAPI GdipGetImageGraphicsContext(GpImage*,GpGraphics**);
WINGDIPAPI GpStatus WINAPI GdipGetImageBounds(GpImage*,GpRectF*,GpUnit*);
WINGDIPAPI GpStatus WINAPI GdipGetImageDimension(GpImage*,REAL*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipGetImageType(GpImage*,ImageType*);
WINGDIPAPI GpStatus WINAPI GdipGetImageWidth(GpImage*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipGetImageHeight(GpImage*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipGetImageHorizontalResolution(GpImage*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipGetImageVerticalResolution(GpImage*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipGetImageFlags(GpImage*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipGetImageRawFormat(GpImage*,GUID*);
WINGDIPAPI GpStatus WINAPI GdipGetImagePixelFormat(GpImage*,PixelFormat*);
WINGDIPAPI GpStatus WINAPI GdipGetImageThumbnail(GpImage*,UINT,UINT,GpImage**,GetThumbnailImageAbort,VOID*);
WINGDIPAPI GpStatus WINAPI GdipGetEncoderParameterListSize(GpImage*,const CLSID*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipGetEncoderParameterList(GpImage*,const CLSID*,UINT,EncoderParameters*);
WINGDIPAPI GpStatus WINAPI GdipImageGetFrameDimensionsCount(GpImage*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipImageGetFrameDimensionsList(GpImage*,GUID*,UINT);
WINGDIPAPI GpStatus WINAPI GdipImageGetFrameCount(GpImage*,const GUID*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipImageSelectActiveFrame(GpImage*,const GUID*,UINT);
WINGDIPAPI GpStatus WINAPI GdipImageRotateFlip(GpImage*,RotateFlipType);
WINGDIPAPI GpStatus WINAPI GdipGetImagePalette(GpImage*,ColorPalette*,INT);
WINGDIPAPI GpStatus WINAPI GdipSetImagePalette(GpImage*,const ColorPalette*);
WINGDIPAPI GpStatus WINAPI GdipGetImagePaletteSize(GpImage*,INT*);
WINGDIPAPI GpStatus WINAPI GdipGetPropertyCount(GpImage*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipGetPropertyIdList(GpImage*,UINT,PROPID*);
WINGDIPAPI GpStatus WINAPI GdipGetPropertyItemSize(GpImage*,PROPID,UINT*);
WINGDIPAPI GpStatus WINAPI GdipGetPropertyItem(GpImage*,PROPID,UINT,PropertyItem*);
WINGDIPAPI GpStatus WINAPI GdipGetPropertySize(GpImage*,UINT*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipGetAllPropertyItems(GpImage*,UINT,UINT,PropertyItem*);
WINGDIPAPI GpStatus WINAPI GdipRemovePropertyItem(GpImage*,PROPID);
WINGDIPAPI GpStatus WINAPI GdipSetPropertyItem(GpImage*,const PropertyItem*);
WINGDIPAPI GpStatus WINAPI GdipFindFirstImageItem(GpImage*,ImageItemData*);
WINGDIPAPI GpStatus WINAPI GdipFindNextImageItem(GpImage*,ImageItemData*);
WINGDIPAPI GpStatus WINAPI GdipGetImageItemData(GpImage*,ImageItemData*);
WINGDIPAPI GpStatus WINAPI GdipImageSetAbort(GpImage*,GdiplusAbort*);
WINGDIPAPI GpStatus WINAPI GdipImageForceValidation(GpImage*);

/* Image codec functions */
WINGDIPAPI GpStatus WINAPI GdipGetImageDecodersSize(UINT*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipGetImageDecoders(UINT,UINT,ImageCodecInfo*);
WINGDIPAPI GpStatus WINAPI GdipGetImageEncodersSize(UINT*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipGetImageEncoders(UINT,UINT,ImageCodecInfo*);

/* ImageAttributes functions */
WINGDIPAPI GpStatus WINAPI GdipCreateImageAttributes(GpImageAttributes**);
WINGDIPAPI GpStatus WINAPI GdipCloneImageAttributes(const GpImageAttributes*,GpImageAttributes**);
WINGDIPAPI GpStatus WINAPI GdipDisposeImageAttributes(GpImageAttributes*);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesToIdentity(GpImageAttributes*,ColorAdjustType);
WINGDIPAPI GpStatus WINAPI GdipResetImageAttributes(GpImageAttributes*,ColorAdjustType);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesColorMatrix(GpImageAttributes*,ColorAdjustType,BOOL,const ColorMatrix*,const ColorMatrix*,ColorMatrixFlags);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesThreshold(GpImageAttributes*,ColorAdjustType,BOOL,REAL);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesGamma(GpImageAttributes*,ColorAdjustType,BOOL,REAL);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesNoOp(GpImageAttributes*,ColorAdjustType,BOOL);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesColorKeys(GpImageAttributes*,ColorAdjustType,BOOL,ARGB,ARGB);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesOutputChannel(GpImageAttributes*,ColorAdjustType,BOOL,ColorChannelFlags);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesOutputChannelColorProfile(GpImageAttributes*,ColorAdjustType,BOOL,const WCHAR*);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesRemapTable(GpImageAttributes*,ColorAdjustType,BOOL,UINT,const ColorMap*);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesWrapMode(GpImageAttributes*,WrapMode,ARGB,BOOL);
WINGDIPAPI GpStatus WINAPI GdipGetImageAttributesAdjustedPalette(GpImageAttributes*,ColorPalette*,ColorAdjustType);
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesCachedBackground(GpImageAttributes*,BOOL);

/* LinearGradientBrush functions */
WINGDIPAPI GpStatus WINAPI GdipCreateLineBrush(const GpPointF*,const GpPointF*,ARGB,ARGB,GpWrapMode,GpLineGradient**);
WINGDIPAPI GpStatus WINAPI GdipCreateLineBrushI(const GpPoint*,const GpPoint*,ARGB,ARGB,GpWrapMode,GpLineGradient**);
WINGDIPAPI GpStatus WINAPI GdipCreateLineBrushFromRect(const GpRectF*,ARGB,ARGB,LinearGradientMode,GpWrapMode,GpLineGradient**);
WINGDIPAPI GpStatus WINAPI GdipCreateLineBrushFromRectI(const GpRect*,ARGB,ARGB,LinearGradientMode,GpWrapMode,GpLineGradient**);
WINGDIPAPI GpStatus WINAPI GdipCreateLineBrushFromRectWithAngle(const GpRectF*,ARGB,ARGB,REAL,BOOL,GpWrapMode,GpLineGradient**);
WINGDIPAPI GpStatus WINAPI GdipCreateLineBrushFromRectWithAngleI(const GpRect*,ARGB,ARGB,REAL,BOOL,GpWrapMode,GpLineGradient**);
WINGDIPAPI GpStatus WINAPI GdipSetLineColors(GpLineGradient*,ARGB,ARGB);
WINGDIPAPI GpStatus WINAPI GdipGetLineColors(GpLineGradient*,ARGB*);
WINGDIPAPI GpStatus WINAPI GdipGetLineRect(GpLineGradient*,GpRectF*);
WINGDIPAPI GpStatus WINAPI GdipGetLineRectI(GpLineGradient*,GpRect*);
WINGDIPAPI GpStatus WINAPI GdipSetLineGammaCorrection(GpLineGradient*,BOOL);
WINGDIPAPI GpStatus WINAPI GdipGetLineGammaCorrection(GpLineGradient*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipGetLineBlendCount(GpLineGradient*,INT*);
WINGDIPAPI GpStatus WINAPI GdipGetLineBlend(GpLineGradient*,REAL*,REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipSetLineBlend(GpLineGradient*,const REAL*,const REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetLinePresetBlendCount(GpLineGradient*,INT*);
WINGDIPAPI GpStatus WINAPI GdipGetLinePresetBlend(GpLineGradient*,ARGB*,REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipSetLinePresetBlend(GpLineGradient*,const ARGB*,const REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipSetLineSigmaBlend(GpLineGradient*,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipSetLineLinearBlend(GpLineGradient*,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipSetLineWrapMode(GpLineGradient*,GpWrapMode);
WINGDIPAPI GpStatus WINAPI GdipGetLineWrapMode(GpLineGradient*,GpWrapMode*);
WINGDIPAPI GpStatus WINAPI GdipGetLineTransform(GpLineGradient*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipSetLineTransform(GpLineGradient*,const GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipResetLineTransform(GpLineGradient*);
WINGDIPAPI GpStatus WINAPI GdipMultiplyLineTransform(GpLineGradient*,const GpMatrix*,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipTranslateLineTransform(GpLineGradient*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipScaleLineTransform(GpLineGradient*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipRotateLineTransform(GpLineGradient*,REAL,GpMatrixOrder);

/* Matrix functions */
WINGDIPAPI GpStatus WINAPI GdipCreateMatrix(GpMatrix**);
WINGDIPAPI GpStatus WINAPI GdipCreateMatrix2(REAL,REAL,REAL,REAL,REAL,REAL,GpMatrix**);
WINGDIPAPI GpStatus WINAPI GdipCreateMatrix3(const GpRectF*,const GpPointF*,GpMatrix**);
WINGDIPAPI GpStatus WINAPI GdipCreateMatrix3I(const GpRect*,const GpPoint*,GpMatrix**);
WINGDIPAPI GpStatus WINAPI GdipCloneMatrix(GpMatrix*,GpMatrix**);
WINGDIPAPI GpStatus WINAPI GdipDeleteMatrix(GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipSetMatrixElements(GpMatrix*,REAL,REAL,REAL,REAL,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipMultiplyMatrix(GpMatrix*,GpMatrix*,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipTranslateMatrix(GpMatrix*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipScaleMatrix(GpMatrix*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipRotateMatrix(GpMatrix*,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipShearMatrix(GpMatrix*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipInvertMatrix(GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipTransformMatrixPoints(GpMatrix*,GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipTransformMatrixPointsI(GpMatrix*,GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipVectorTransformMatrixPoints(GpMatrix*,GpPointF*,INT);
WINGDIPAPI GpStatus WINAPI GdipVectorTransformMatrixPointsI(GpMatrix*,GpPoint*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetMatrixElements(const GpMatrix*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipIsMatrixInvertible(const GpMatrix*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsMatrixIdentity(const GpMatrix*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsMatrixEqual(const GpMatrix*,const GpMatrix*,BOOL*);

/* Metafile functions */
WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromEmf(HENHMETAFILE,MetafileHeader*);
WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromFile(const WCHAR*,MetafileHeader*);
WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromStream(IStream*,MetafileHeader*);
WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromMetafile(GpMetafile*,MetafileHeader*);
WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromWmf(HMETAFILE,const WmfPlaceableFileHeader*,MetafileHeader*);
WINGDIPAPI GpStatus WINAPI GdipPlayTSClientRecord(HDC,INT,const BYTE*,UINT,void*);
WINGDIPAPI GpStatus WINAPI GdipGetHemfFromMetafile(GpMetafile*,HENHMETAFILE*);
WINGDIPAPI GpStatus WINAPI GdipCreateStreamOnFile(const WCHAR*,UINT,IStream**);
WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromWmf(HMETAFILE,BOOL,const WmfPlaceableFileHeader*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromEmf(HENHMETAFILE,BOOL,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromFile(const WCHAR*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromWmfFile(const WCHAR*,const WmfPlaceableFileHeader*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromStream(IStream*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipRecordMetafile(HDC,EmfType,const GpRectF*,MetafileFrameUnit,const WCHAR*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipRecordMetafileI(HDC,EmfType,const GpRect*,MetafileFrameUnit,const WCHAR*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipRecordMetafileFileName(const WCHAR*,HDC,EmfType,const GpRectF*,MetafileFrameUnit,const WCHAR*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipRecordMetafileFileNameI(const WCHAR*,HDC,EmfType,const GpRect*,MetafileFrameUnit,const WCHAR*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipRecordMetafileStream(IStream*,HDC,EmfType,const GpRectF*,MetafileFrameUnit,const WCHAR*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipRecordMetafileStreamI(IStream*,HDC,EmfType,const GpRect*,MetafileFrameUnit,const WCHAR*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipPlayMetafileRecord(const GpMetafile*,EmfPlusRecordType,UINT,UINT,const BYTE*);
WINGDIPAPI GpStatus WINAPI GdipSetMetafileDownLevelRasterizationLimit(GpMetafile*,UINT);
WINGDIPAPI GpStatus WINAPI GdipGetMetafileDownLevelRasterizationLimit(const GpMetafile*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipConvertToEmfPlus(const GpGraphics*,GpMetafile*,BOOL*,EmfType,const WCHAR*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipConvertToEmfPlusToFile(const GpGraphics*,GpMetafile*,BOOL*,const WCHAR*,EmfType,const WCHAR*,GpMetafile**);
WINGDIPAPI GpStatus WINAPI GdipConvertToEmfPlusToStream(const GpGraphics*,GpMetafile*,BOOL*,IStream*,EmfType,const WCHAR*,GpMetafile**);
WINGDIPAPI UINT WINAPI GdipEmfToWmfBits(HENHMETAFILE,UINT,LPBYTE,INT,INT);

/* PathGradientBrush functions */
WINGDIPAPI GpStatus WINAPI GdipCreatePathGradient(const GpPointF*,INT,GpWrapMode,GpPathGradient**);
WINGDIPAPI GpStatus WINAPI GdipCreatePathGradientI(const GpPoint*,INT,GpWrapMode,GpPathGradient**);
WINGDIPAPI GpStatus WINAPI GdipCreatePathGradientFromPath(const GpPath*,GpPathGradient**);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientCenterColor(GpPathGradient*,ARGB*);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientCenterColor(GpPathGradient*,ARGB);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientSurroundColorsWithCount(GpPathGradient*,ARGB*,INT*);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientSurroundColorsWithCount(GpPathGradient*,const ARGB*,INT*);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientPath(GpPathGradient*,GpPath*);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientPath(GpPathGradient*,const GpPath*);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientCenterPoint(GpPathGradient*,GpPointF*);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientCenterPointI(GpPathGradient*,GpPoint*);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientCenterPoint(GpPathGradient*,const GpPointF*);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientCenterPointI(GpPathGradient*,const GpPoint*);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientRect(GpPathGradient*,GpRectF*);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientRectI(GpPathGradient*,GpRect*);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientPointCount(GpPathGradient*,INT*);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientSurroundColorCount(GpPathGradient*,INT*);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientGammaCorrection(GpPathGradient*,BOOL);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientGammaCorrection(GpPathGradient*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientBlendCount(GpPathGradient*,INT*);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientBlend(GpPathGradient*,REAL*,REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientBlend(GpPathGradient*,const REAL*,const REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientPresetBlendCount(GpPathGradient*,INT*);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientPresetBlend(GpPathGradient*,ARGB*,REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientPresetBlend(GpPathGradient*,const ARGB*,const REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientSigmaBlend(GpPathGradient*,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientLinearBlend(GpPathGradient*,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientWrapMode(GpPathGradient*,GpWrapMode*);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientWrapMode(GpPathGradient*,GpWrapMode);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientTransform(GpPathGradient*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientTransform(GpPathGradient*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipResetPathGradientTransform(GpPathGradient*);
WINGDIPAPI GpStatus WINAPI GdipMultiplyPathGradientTransform(GpPathGradient*,const GpMatrix*,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipTranslatePathGradientTransform(GpPathGradient*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipScalePathGradientTransform(GpPathGradient*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipRotatePathGradientTransform(GpPathGradient*,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipGetPathGradientFocusScales(GpPathGradient*,REAL*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipSetPathGradientFocusScales(GpPathGradient*,REAL,REAL);

/* PathIterator functions */
WINGDIPAPI GpStatus WINAPI GdipCreatePathIter(GpPathIterator**,GpPath*);
WINGDIPAPI GpStatus WINAPI GdipDeletePathIter(GpPathIterator*);
WINGDIPAPI GpStatus WINAPI GdipPathIterNextSubpath(GpPathIterator*,INT*,INT*,INT*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipPathIterNextSubpathPath(GpPathIterator*,INT*,GpPath*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipPathIterNextPathType(GpPathIterator*,INT*,BYTE*,INT*,INT*);
WINGDIPAPI GpStatus WINAPI GdipPathIterNextMarker(GpPathIterator*,INT*,INT*,INT*);
WINGDIPAPI GpStatus WINAPI GdipPathIterNextMarkerPath(GpPathIterator*,INT*,GpPath*);
WINGDIPAPI GpStatus WINAPI GdipPathIterGetCount(GpPathIterator*,INT*);
WINGDIPAPI GpStatus WINAPI GdipPathIterGetSubpathCount(GpPathIterator*,INT*);
WINGDIPAPI GpStatus WINAPI GdipPathIterIsValid(GpPathIterator*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipPathIterHasCurve(GpPathIterator*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipPathIterRewind(GpPathIterator*);
WINGDIPAPI GpStatus WINAPI GdipPathIterEnumerate(GpPathIterator*,INT*,GpPointF*,BYTE*,INT);
WINGDIPAPI GpStatus WINAPI GdipPathIterCopyData(GpPathIterator*,INT*,GpPointF*,BYTE*,INT,INT);

/* Pen functions */
WINGDIPAPI GpStatus WINAPI GdipCreatePen1(ARGB,REAL,GpUnit,GpPen**);
WINGDIPAPI GpStatus WINAPI GdipCreatePen2(GpBrush*,REAL,GpUnit,GpPen**);
WINGDIPAPI GpStatus WINAPI GdipClonePen(GpPen*,GpPen**);
WINGDIPAPI GpStatus WINAPI GdipDeletePen(GpPen*);
WINGDIPAPI GpStatus WINAPI GdipSetPenWidth(GpPen*,REAL);
WINGDIPAPI GpStatus WINAPI GdipGetPenWidth(GpPen*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipSetPenUnit(GpPen*,GpUnit);
WINGDIPAPI GpStatus WINAPI GdipGetPenUnit(GpPen*,GpUnit*);
WINGDIPAPI GpStatus WINAPI GdipSetPenLineCap197819(GpPen*,GpLineCap,GpLineCap,GpDashCap);
WINGDIPAPI GpStatus WINAPI GdipSetPenStartCap(GpPen*,GpLineCap);
WINGDIPAPI GpStatus WINAPI GdipSetPenEndCap(GpPen*,GpLineCap);
WINGDIPAPI GpStatus WINAPI GdipSetPenDashCap197819(GpPen*,GpDashCap);
WINGDIPAPI GpStatus WINAPI GdipGetPenStartCap(GpPen*,GpLineCap*);
WINGDIPAPI GpStatus WINAPI GdipGetPenEndCap(GpPen*,GpLineCap*);
WINGDIPAPI GpStatus WINAPI GdipGetPenDashCap197819(GpPen*,GpDashCap*);
WINGDIPAPI GpStatus WINAPI GdipSetPenLineJoin(GpPen*,GpLineJoin);
WINGDIPAPI GpStatus WINAPI GdipGetPenLineJoin(GpPen*,GpLineJoin*);
WINGDIPAPI GpStatus WINAPI GdipSetPenCustomStartCap(GpPen*,GpCustomLineCap*);
WINGDIPAPI GpStatus WINAPI GdipGetPenCustomStartCap(GpPen*,GpCustomLineCap**);
WINGDIPAPI GpStatus WINAPI GdipSetPenCustomEndCap(GpPen*,GpCustomLineCap*);
WINGDIPAPI GpStatus WINAPI GdipGetPenCustomEndCap(GpPen*,GpCustomLineCap**);
WINGDIPAPI GpStatus WINAPI GdipSetPenMiterLimit(GpPen*,REAL);
WINGDIPAPI GpStatus WINAPI GdipGetPenMiterLimit(GpPen*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipSetPenMode(GpPen*,GpPenAlignment);
WINGDIPAPI GpStatus WINAPI GdipGetPenMode(GpPen*,GpPenAlignment*);
WINGDIPAPI GpStatus WINAPI GdipSetPenTransform(GpPen*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipGetPenTransform(GpPen*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipResetPenTransform(GpPen*);
WINGDIPAPI GpStatus WINAPI GdipMultiplyPenTransform(GpPen*,const GpMatrix*,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipTranslatePenTransform(GpPen*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipScalePenTransform(GpPen*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipRotatePenTransform(GpPen*,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipSetPenColor(GpPen*,ARGB);
WINGDIPAPI GpStatus WINAPI GdipGetPenColor(GpPen*,ARGB*);
WINGDIPAPI GpStatus WINAPI GdipSetPenBrushFill(GpPen*,GpBrush*);
WINGDIPAPI GpStatus WINAPI GdipGetPenBrushFill(GpPen*,GpBrush**);
WINGDIPAPI GpStatus WINAPI GdipGetPenFillType(GpPen*,GpPenType*);
WINGDIPAPI GpStatus WINAPI GdipGetPenDashStyle(GpPen*,GpDashStyle*);
WINGDIPAPI GpStatus WINAPI GdipSetPenDashStyle(GpPen*,GpDashStyle);
WINGDIPAPI GpStatus WINAPI GdipGetPenDashOffset(GpPen*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipSetPenDashOffset(GpPen*,REAL);
WINGDIPAPI GpStatus WINAPI GdipGetPenDashCount(GpPen*,INT*);
WINGDIPAPI GpStatus WINAPI GdipSetPenDashArray(GpPen*,const REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetPenDashArray(GpPen*,REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetPenCompoundCount(GpPen*,INT*);
WINGDIPAPI GpStatus WINAPI GdipSetPenCompoundArray(GpPen*,const REAL*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetPenCompoundArray(GpPen*,REAL*,INT);

/* Region functions */
WINGDIPAPI GpStatus WINAPI GdipCreateRegion(GpRegion**);
WINGDIPAPI GpStatus WINAPI GdipCreateRegionRect(const GpRectF*,GpRegion**);
WINGDIPAPI GpStatus WINAPI GdipCreateRegionRectI(const GpRect*,GpRegion**);
WINGDIPAPI GpStatus WINAPI GdipCreateRegionPath(GpPath*,GpRegion**);
WINGDIPAPI GpStatus WINAPI GdipCreateRegionRgnData(const BYTE*,INT,GpRegion**);
WINGDIPAPI GpStatus WINAPI GdipCreateRegionHrgn(HRGN,GpRegion**);
WINGDIPAPI GpStatus WINAPI GdipCloneRegion(GpRegion*,GpRegion**);
WINGDIPAPI GpStatus WINAPI GdipDeleteRegion(GpRegion*);
WINGDIPAPI GpStatus WINAPI GdipSetInfinite(GpRegion*);
WINGDIPAPI GpStatus WINAPI GdipSetEmpty(GpRegion*);
WINGDIPAPI GpStatus WINAPI GdipCombineRegionRect(GpRegion*,const GpRectF*,CombineMode);
WINGDIPAPI GpStatus WINAPI GdipCombineRegionRectI(GpRegion*,const GpRect*,CombineMode);
WINGDIPAPI GpStatus WINAPI GdipCombineRegionPath(GpRegion*,GpPath*,CombineMode);
WINGDIPAPI GpStatus WINAPI GdipCombineRegionRegion(GpRegion*,GpRegion*,CombineMode);
WINGDIPAPI GpStatus WINAPI GdipTranslateRegion(GpRegion*,REAL,REAL);
WINGDIPAPI GpStatus WINAPI GdipTranslateRegionI(GpRegion*,INT,INT);
WINGDIPAPI GpStatus WINAPI GdipTransformRegion(GpRegion*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipGetRegionBounds(GpRegion*,GpGraphics*,GpRectF*);
WINGDIPAPI GpStatus WINAPI GdipGetRegionBoundsI(GpRegion*,GpGraphics*,GpRect*);
WINGDIPAPI GpStatus WINAPI GdipGetRegionHRgn(GpRegion*,GpGraphics*,HRGN*);
WINGDIPAPI GpStatus WINAPI GdipIsEmptyRegion(GpRegion*,GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsInfiniteRegion(GpRegion*,GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsEqualRegion(GpRegion*,GpRegion*,GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipGetRegionDataSize(GpRegion*,UINT*);
WINGDIPAPI GpStatus WINAPI GdipGetRegionData(GpRegion*,BYTE*,UINT,UINT*);
WINGDIPAPI GpStatus WINAPI GdipIsVisibleRegionPoint(GpRegion*,REAL,REAL,GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsVisibleRegionPointI(GpRegion*,INT,INT,GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsVisibleRegionRect(GpRegion*,REAL,REAL,REAL,REAL,GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipIsVisibleRegionRectI(GpRegion*,INT,INT,INT,INT,GpGraphics*,BOOL*);
WINGDIPAPI GpStatus WINAPI GdipGetRegionScansCount(GpRegion*,UINT*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipGetRegionScans(GpRegion*,GpRectF*,INT*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipGetRegionScansI(GpRegion*,GpRect*,INT*,GpMatrix*);

/* SolidBrush functions */
WINGDIPAPI GpStatus WINAPI GdipCreateSolidFill(ARGB,GpSolidFill**);
WINGDIPAPI GpStatus WINAPI GdipSetSolidFillColor(GpSolidFill*,ARGB);
WINGDIPAPI GpStatus WINAPI GdipGetSolidFillColor(GpSolidFill*,ARGB*);

/* StringFormat functions */
WINGDIPAPI GpStatus WINAPI GdipCreateStringFormat(INT,LANGID,GpStringFormat**);
WINGDIPAPI GpStatus WINAPI GdipStringFormatGetGenericDefault(GpStringFormat**);
WINGDIPAPI GpStatus WINAPI GdipStringFormatGetGenericTypographic(GpStringFormat**);
WINGDIPAPI GpStatus WINAPI GdipDeleteStringFormat(GpStringFormat*);
WINGDIPAPI GpStatus WINAPI GdipCloneStringFormat(const GpStringFormat*,GpStringFormat**);
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatFlags(GpStringFormat*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatFlags(const GpStringFormat*,INT*);
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatAlign(GpStringFormat*,StringAlignment);
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatAlign(const GpStringFormat*,StringAlignment*);
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatLineAlign(GpStringFormat*,StringAlignment);
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatLineAlign(const GpStringFormat*,StringAlignment*);
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatTrimming(GpStringFormat*,StringTrimming);
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatTrimming(const GpStringFormat*,StringTrimming*);
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatHotkeyPrefix(GpStringFormat*,INT);
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatHotkeyPrefix(const GpStringFormat*,INT*);
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatTabStops(GpStringFormat*,REAL,INT,const REAL*);
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatTabStops(const GpStringFormat*,INT,REAL*,REAL*);
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatTabStopCount(const GpStringFormat*,INT*);
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatDigitSubstitution(GpStringFormat*,LANGID,StringDigitSubstitute);
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatDigitSubstitution(const GpStringFormat*,LANGID*,StringDigitSubstitute*);
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatMeasurableCharacterRangeCount(const GpStringFormat*,INT*);
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatMeasurableCharacterRanges(GpStringFormat*,INT,const CharacterRange*);

/* Text functions */
WINGDIPAPI GpStatus WINAPI GdipDrawString(GpGraphics*,const WCHAR*,INT,const GpFont*,const RectF*,const GpStringFormat*,const GpBrush*);
WINGDIPAPI GpStatus WINAPI GdipMeasureString(GpGraphics*,const WCHAR*,INT,const GpFont*,const RectF*,const GpStringFormat*,RectF*,INT*,INT*);
WINGDIPAPI GpStatus WINAPI GdipMeasureCharacterRanges(GpGraphics*,const WCHAR*,INT,const GpFont*,const RectF*,const GpStringFormat*,INT,GpRegion**);
WINGDIPAPI GpStatus WINAPI GdipDrawDriverString(GpGraphics*,const UINT16*,INT,const GpFont*,const GpBrush*,const PointF*,INT,const GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipMeasureDriverString(GpGraphics*,const UINT16*,INT,const GpFont*,const PointF*,INT,const GpMatrix*,RectF*);

/* TextureBrush functions */
WINGDIPAPI GpStatus WINAPI GdipCreateTexture(GpImage*,GpWrapMode,GpTexture**);
WINGDIPAPI GpStatus WINAPI GdipCreateTexture2(GpImage*,GpWrapMode,REAL,REAL,REAL,REAL,GpTexture**);
WINGDIPAPI GpStatus WINAPI GdipCreateTexture2I(GpImage*,GpWrapMode,INT,INT,INT,INT,GpTexture**);
WINGDIPAPI GpStatus WINAPI GdipCreateTextureIA(GpImage*,const GpImageAttributes*,REAL,REAL,REAL,REAL,GpTexture**);
WINGDIPAPI GpStatus WINAPI GdipCreateTextureIAI(GpImage*,const GpImageAttributes*,INT,INT,INT,INT,GpTexture**);
WINGDIPAPI GpStatus WINAPI GdipGetTextureTransform(GpTexture*,GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipSetTextureTransform(GpTexture*,const GpMatrix*);
WINGDIPAPI GpStatus WINAPI GdipResetTextureTransform(GpTexture*);
WINGDIPAPI GpStatus WINAPI GdipMultiplyTextureTransform(GpTexture*,const GpMatrix*,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipTranslateTextureTransform(GpTexture*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipScaleTextureTransform(GpTexture*,REAL,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipRotateTextureTransform(GpTexture*,REAL,GpMatrixOrder);
WINGDIPAPI GpStatus WINAPI GdipSetTextureWrapMode(GpTexture*,GpWrapMode);
WINGDIPAPI GpStatus WINAPI GdipGetTextureWrapMode(GpTexture*,GpWrapMode*);
WINGDIPAPI GpStatus WINAPI GdipGetTextureImage(GpTexture*,GpImage**);

/* uncategorized functions */
WINGDIPAPI GpStatus WINAPI GdipTestControl(GpTestControlEnum,void*);

#endif
