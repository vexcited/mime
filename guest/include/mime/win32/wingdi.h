/*
 * gdi definitions shared by gdi32, user32, win32k and applications.
 */
#ifndef MIME_WIN32_WINGDI_H
#define MIME_WIN32_WINGDI_H

#include <mime/win32/windef.h>

#ifdef MIME_BUILD_GDI32
#undef WINGDIAPI
#define WINGDIAPI __declspec(dllexport)
#endif

#define LF_FACESIZE 32
#define LF_FULLFACESIZE 64

/* stock objects. */
#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
#define NULL_BRUSH 5
#define HOLLOW_BRUSH NULL_BRUSH
#define WHITE_PEN 6
#define BLACK_PEN 7
#define NULL_PEN 8
#define OEM_FIXED_FONT 10
#define ANSI_FIXED_FONT 11
#define ANSI_VAR_FONT 12
#define SYSTEM_FONT 13
#define DEVICE_DEFAULT_FONT 14
#define DEFAULT_PALETTE 15
#define SYSTEM_FIXED_FONT 16
#define DEFAULT_GUI_FONT 17
#define DC_BRUSH 18
#define DC_PEN 19
#define STOCK_LAST 19

/* object types. */
#define OBJ_PEN 1
#define OBJ_BRUSH 2
#define OBJ_DC 3
#define OBJ_METADC 4
#define OBJ_PAL 5
#define OBJ_FONT 6
#define OBJ_BITMAP 7
#define OBJ_REGION 8
#define OBJ_METAFILE 9
#define OBJ_MEMDC 10
#define OBJ_EXTPEN 11
#define OBJ_ENHMETADC 12
#define OBJ_ENHMETAFILE 13
#define OBJ_COLORSPACE 14

/* ternary raster operations. */
#define SRCCOPY 0x00CC0020
#define SRCPAINT 0x00EE0086
#define SRCAND 0x008800C6
#define SRCINVERT 0x00660046
#define SRCERASE 0x00440328
#define NOTSRCCOPY 0x00330008
#define NOTSRCERASE 0x001100A6
#define MERGECOPY 0x00C000CA
#define MERGEPAINT 0x00BB0226
#define PATCOPY 0x00F00021
#define PATPAINT 0x00FB0A09
#define PATINVERT 0x005A0049
#define DSTINVERT 0x00550009
#define BLACKNESS 0x00000042
#define WHITENESS 0x00FF0062
#define NOMIRRORBITMAP 0x80000000
#define CAPTUREBLT 0x40000000

/* binary raster operations. */
#define R2_BLACK 1
#define R2_NOTMERGEPEN 2
#define R2_MASKNOTPEN 3
#define R2_NOTCOPYPEN 4
#define R2_MASKPENNOT 5
#define R2_NOT 6
#define R2_XORPEN 7
#define R2_NOTMASKPEN 8
#define R2_MASKPEN 9
#define R2_NOTXORPEN 10
#define R2_NOP 11
#define R2_MERGENOTPEN 12
#define R2_COPYPEN 13
#define R2_MERGEPENNOT 14
#define R2_MERGEPEN 15
#define R2_WHITE 16

#define TRANSPARENT 1
#define OPAQUE 2

#define CLR_INVALID 0xFFFFFFFF
#define GDI_ERROR 0xFFFFFFFFL
#define HGDI_ERROR ((HANDLE)(LONG_PTR)-1)

/* region results and combine modes. */
#define ERROR 0
#define NULLREGION 1
#define SIMPLEREGION 2
#define COMPLEXREGION 3
#define RGN_ERROR ERROR
#define RGN_AND 1
#define RGN_OR 2
#define RGN_XOR 3
#define RGN_DIFF 4
#define RGN_COPY 5

/* text alignment. */
#define TA_NOUPDATECP 0
#define TA_UPDATECP 1
#define TA_LEFT 0
#define TA_RIGHT 2
#define TA_CENTER 6
#define TA_TOP 0
#define TA_BOTTOM 8
#define TA_BASELINE 24
#define TA_RTLREADING 256

#define ETO_OPAQUE 0x0002
#define ETO_CLIPPED 0x0004
#define ETO_GLYPH_INDEX 0x0010
#define ETO_RTLREADING 0x0080
#define ETO_IGNORELANGUAGE 0x1000
#define ETO_PDY 0x2000

/* device capabilities. */
#define DRIVERVERSION 0
#define TECHNOLOGY 2
#define HORZSIZE 4
#define VERTSIZE 6
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define PLANES 14
#define NUMBRUSHES 16
#define NUMPENS 18
#define NUMFONTS 22
#define NUMCOLORS 24
#define ASPECTX 40
#define ASPECTY 42
#define ASPECTXY 44
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define SIZEPALETTE 104
#define NUMRESERVED 106
#define COLORRES 108
#define PHYSICALWIDTH 110
#define PHYSICALHEIGHT 111
#define VREFRESH 116
#define DESKTOPVERTRES 117
#define DESKTOPHORZRES 118
#define BLTALIGNMENT 119
#define SHADEBLENDCAPS 120
#define RASTERCAPS 38
#define CURVECAPS 28
#define LINECAPS 30
#define POLYGONALCAPS 32
#define TEXTCAPS 34
#define CLIPCAPS 36
#define DT_RASDISPLAY 1
#define RC_BITBLT 1
#define RC_STRETCHBLT 0x0800
#define RC_DI_BITMAP 0x0080
#define RC_DIBTODEV 0x0200
#define RC_STRETCHDIB 0x2000

/* fonts. */
#define FW_DONTCARE 0
#define FW_THIN 100
#define FW_EXTRALIGHT 200
#define FW_LIGHT 300
#define FW_NORMAL 400
#define FW_MEDIUM 500
#define FW_SEMIBOLD 600
#define FW_BOLD 700
#define FW_EXTRABOLD 800
#define FW_HEAVY 900

#define ANSI_CHARSET 0
#define DEFAULT_CHARSET 1
#define SYMBOL_CHARSET 2
#define SHIFTJIS_CHARSET 128
#define GB2312_CHARSET 134
#define CHINESEBIG5_CHARSET 136
#define OEM_CHARSET 255

#define OUT_DEFAULT_PRECIS 0
#define OUT_STRING_PRECIS 1
#define OUT_TT_PRECIS 4
#define OUT_TT_ONLY_PRECIS 7
#define OUT_OUTLINE_PRECIS 8
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define DRAFT_QUALITY 1
#define PROOF_QUALITY 2
#define NONANTIALIASED_QUALITY 3
#define ANTIALIASED_QUALITY 4
#define CLEARTYPE_QUALITY 5
#define CLEARTYPE_NATURAL_QUALITY 6
#define DEFAULT_PITCH 0
#define FIXED_PITCH 1
#define VARIABLE_PITCH 2
#define FF_DONTCARE (0 << 4)
#define FF_ROMAN (1 << 4)
#define FF_SWISS (2 << 4)
#define FF_MODERN (3 << 4)
#define FF_SCRIPT (4 << 4)
#define FF_DECORATIVE (5 << 4)
#define TMPF_FIXED_PITCH 0x01
#define TMPF_VECTOR 0x02
#define TMPF_DEVICE 0x08
#define TMPF_TRUETYPE 0x04

/* brushes and pens. */
#define BS_SOLID 0
#define BS_NULL 1
#define BS_HOLLOW BS_NULL
#define BS_HATCHED 2
#define BS_PATTERN 3
#define BS_DIBPATTERN 5
#define BS_DIBPATTERNPT 6
#define PS_SOLID 0
#define PS_DASH 1
#define PS_DOT 2
#define PS_DASHDOT 3
#define PS_DASHDOTDOT 4
#define PS_NULL 5
#define PS_INSIDEFRAME 6
#define PS_STYLE_MASK 0x0000000F
#define PS_GEOMETRIC 0x00010000
#define PS_COSMETIC 0x00000000

/* bitmaps. */
#define BI_RGB 0
#define BI_RLE8 1
#define BI_RLE4 2
#define BI_BITFIELDS 3
#define BI_JPEG 4
#define BI_PNG 5
#define DIB_RGB_COLORS 0
#define DIB_PAL_COLORS 1
#define CBM_INIT 0x04

#define AC_SRC_OVER 0x00
#define AC_SRC_ALPHA 0x01

#define GM_COMPATIBLE 1
#define GM_ADVANCED 2
#define MM_TEXT 1
#define MM_ANISOTROPIC 8
#define MM_ISOTROPIC 7

#define STRETCH_ANDSCANS 1
#define STRETCH_ORSCANS 2
#define STRETCH_DELETESCANS 3
#define STRETCH_HALFTONE 4
#define BLACKONWHITE STRETCH_ANDSCANS
#define COLORONCOLOR STRETCH_DELETESCANS
#define HALFTONE STRETCH_HALFTONE

#define DCX_WINDOW 0x00000001
#define DCX_CACHE 0x00000002
#define DCX_NORESETATTRS 0x00000004
#define DCX_CLIPCHILDREN 0x00000008
#define DCX_CLIPSIBLINGS 0x00000010
#define DCX_PARENTCLIP 0x00000020
#define DCX_EXCLUDERGN 0x00000040
#define DCX_INTERSECTRGN 0x00000080
#define DCX_EXCLUDEUPDATE 0x00000100
#define DCX_INTERSECTUPDATE 0x00000200
#define DCX_LOCKWINDOWUPDATE 0x00000400
#define DCX_VALIDATE 0x00200000

typedef struct tagLOGFONTW {
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    BYTE lfItalic;
    BYTE lfUnderline;
    BYTE lfStrikeOut;
    BYTE lfCharSet;
    BYTE lfOutPrecision;
    BYTE lfClipPrecision;
    BYTE lfQuality;
    BYTE lfPitchAndFamily;
    WCHAR lfFaceName[LF_FACESIZE];
} LOGFONTW, *PLOGFONTW, *LPLOGFONTW;

typedef struct tagLOGFONTA {
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    BYTE lfItalic;
    BYTE lfUnderline;
    BYTE lfStrikeOut;
    BYTE lfCharSet;
    BYTE lfOutPrecision;
    BYTE lfClipPrecision;
    BYTE lfQuality;
    BYTE lfPitchAndFamily;
    CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA, *PLOGFONTA, *LPLOGFONTA;

typedef struct tagTEXTMETRICW {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    WCHAR tmFirstChar;
    WCHAR tmLastChar;
    WCHAR tmDefaultChar;
    WCHAR tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICW, *PTEXTMETRICW, *LPTEXTMETRICW;

typedef struct tagTEXTMETRICA {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    BYTE tmFirstChar;
    BYTE tmLastChar;
    BYTE tmDefaultChar;
    BYTE tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICA, *PTEXTMETRICA, *LPTEXTMETRICA;

typedef struct tagRGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD, *LPRGBQUAD;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO, *PBITMAPINFO, *LPBITMAPINFO;

#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER {
    WORD bfType;
    DWORD bfSize;
    WORD bfReserved1;
    WORD bfReserved2;
    DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;
#pragma pack(pop)

typedef struct tagBITMAP {
    LONG bmType;
    LONG bmWidth;
    LONG bmHeight;
    LONG bmWidthBytes;
    WORD bmPlanes;
    WORD bmBitsPixel;
    LPVOID bmBits;
} BITMAP, *PBITMAP, *LPBITMAP;

typedef struct tagDIBSECTION {
    BITMAP dsBm;
    BITMAPINFOHEADER dsBmih;
    DWORD dsBitfields[3];
    HANDLE dshSection;
    DWORD dsOffset;
} DIBSECTION, *PDIBSECTION, *LPDIBSECTION;

typedef struct tagLOGBRUSH {
    UINT lbStyle;
    COLORREF lbColor;
    ULONG_PTR lbHatch;
} LOGBRUSH, *PLOGBRUSH, *LPLOGBRUSH;

typedef struct tagLOGPEN {
    UINT lopnStyle;
    POINT lopnWidth;
    COLORREF lopnColor;
} LOGPEN, *PLOGPEN, *LPLOGPEN;

typedef struct _BLENDFUNCTION {
    BYTE BlendOp;
    BYTE BlendFlags;
    BYTE SourceConstantAlpha;
    BYTE AlphaFormat;
} BLENDFUNCTION, *PBLENDFUNCTION;

typedef struct _ABC {
    int abcA;
    UINT abcB;
    int abcC;
} ABC, *PABC, *LPABC;

typedef struct tagXFORM {
    FLOAT eM11;
    FLOAT eM12;
    FLOAT eM21;
    FLOAT eM22;
    FLOAT eDx;
    FLOAT eDy;
} XFORM, *PXFORM, *LPXFORM;

typedef struct _RGNDATAHEADER {
    DWORD dwSize;
    DWORD iType;
    DWORD nCount;
    DWORD nRgnSize;
    RECT rcBound;
} RGNDATAHEADER, *PRGNDATAHEADER;

typedef struct _RGNDATA {
    RGNDATAHEADER rdh;
    char Buffer[1];
} RGNDATA, *PRGNDATA, *LPRGNDATA;
#define RDH_RECTANGLES 1

typedef int(CALLBACK *FONTENUMPROCW)(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param);

/* ------------------------------------------------------------------ gdi32 exports */

WINGDIAPI HGDIOBJ WINAPI GetStockObject(int index);
WINGDIAPI HGDIOBJ WINAPI SelectObject(HDC dc, HGDIOBJ object);
WINGDIAPI BOOL WINAPI DeleteObject(HGDIOBJ object);
WINGDIAPI DWORD WINAPI GetObjectType(HGDIOBJ object);
WINGDIAPI int WINAPI GetObjectW(HANDLE object, int size, LPVOID buffer);
WINGDIAPI HDC WINAPI CreateCompatibleDC(HDC dc);
WINGDIAPI HDC WINAPI CreateDCW(LPCWSTR driver, LPCWSTR device, LPCWSTR port, const void *mode);
WINGDIAPI BOOL WINAPI DeleteDC(HDC dc);
WINGDIAPI int WINAPI SaveDC(HDC dc);
WINGDIAPI BOOL WINAPI RestoreDC(HDC dc, int saved);
WINGDIAPI int WINAPI GetDeviceCaps(HDC dc, int index);
WINGDIAPI HBITMAP WINAPI CreateCompatibleBitmap(HDC dc, int width, int height);
WINGDIAPI HBITMAP WINAPI CreateBitmap(int width, int height, UINT planes, UINT bpp, const void *bits);
WINGDIAPI HBITMAP WINAPI CreateDIBSection(HDC dc, const BITMAPINFO *info, UINT usage, void **bits, HANDLE section,
                                          DWORD offset);
WINGDIAPI HBITMAP WINAPI CreateDIBitmap(HDC dc, const BITMAPINFOHEADER *header, DWORD init, const void *bits,
                                        const BITMAPINFO *info, UINT usage);
WINGDIAPI int WINAPI GetDIBits(HDC dc, HBITMAP bitmap, UINT start, UINT lines, LPVOID bits, LPBITMAPINFO info,
                               UINT usage);
WINGDIAPI int WINAPI SetDIBitsToDevice(HDC dc, int x, int y, DWORD width, DWORD height, int src_x, int src_y,
                                       UINT start, UINT lines, const void *bits, const BITMAPINFO *info, UINT usage);
WINGDIAPI int WINAPI StretchDIBits(HDC dc, int x, int y, int width, int height, int src_x, int src_y, int src_width,
                                   int src_height, const void *bits, const BITMAPINFO *info, UINT usage, DWORD rop);
WINGDIAPI BOOL WINAPI BitBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y, DWORD rop);
WINGDIAPI BOOL WINAPI StretchBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                 int src_width, int src_height, DWORD rop);
WINGDIAPI BOOL WINAPI PatBlt(HDC dc, int x, int y, int width, int height, DWORD rop);
WINGDIAPI BOOL WINAPI GdiAlphaBlend(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                    int src_width, int src_height, BLENDFUNCTION blend);
WINGDIAPI BOOL WINAPI GdiFlush(void);
WINGDIAPI HBRUSH WINAPI CreateSolidBrush(COLORREF color);
WINGDIAPI HBRUSH WINAPI CreateBrushIndirect(const LOGBRUSH *brush);
WINGDIAPI HPEN WINAPI CreatePen(int style, int width, COLORREF color);
WINGDIAPI HPEN WINAPI CreatePenIndirect(const LOGPEN *pen);
WINGDIAPI HFONT WINAPI CreateFontIndirectW(const LOGFONTW *font);
WINGDIAPI HFONT WINAPI CreateFontW(int height, int width, int escapement, int orientation, int weight, DWORD italic,
                                   DWORD underline, DWORD strikeout, DWORD charset, DWORD out_precision,
                                   DWORD clip_precision, DWORD quality, DWORD pitch_family, LPCWSTR face);
WINGDIAPI COLORREF WINAPI SetTextColor(HDC dc, COLORREF color);
WINGDIAPI COLORREF WINAPI GetTextColor(HDC dc);
WINGDIAPI COLORREF WINAPI SetBkColor(HDC dc, COLORREF color);
WINGDIAPI COLORREF WINAPI GetBkColor(HDC dc);
WINGDIAPI int WINAPI SetBkMode(HDC dc, int mode);
WINGDIAPI int WINAPI GetBkMode(HDC dc);
WINGDIAPI UINT WINAPI SetTextAlign(HDC dc, UINT align);
WINGDIAPI UINT WINAPI GetTextAlign(HDC dc);
WINGDIAPI COLORREF WINAPI SetDCBrushColor(HDC dc, COLORREF color);
WINGDIAPI COLORREF WINAPI SetDCPenColor(HDC dc, COLORREF color);
WINGDIAPI int WINAPI SetROP2(HDC dc, int rop);
WINGDIAPI BOOL WINAPI TextOutW(HDC dc, int x, int y, LPCWSTR text, int count);
WINGDIAPI BOOL WINAPI ExtTextOutW(HDC dc, int x, int y, UINT options, const RECT *rect, LPCWSTR text, UINT count,
                                  const INT *dx);
WINGDIAPI BOOL WINAPI GetTextExtentPoint32W(HDC dc, LPCWSTR text, int count, LPSIZE size);
WINGDIAPI BOOL WINAPI GetTextExtentExPointW(HDC dc, LPCWSTR text, int count, int max_extent, LPINT fit,
                                            LPINT dx, LPSIZE size);
WINGDIAPI BOOL WINAPI GetTextMetricsW(HDC dc, LPTEXTMETRICW metrics);
WINGDIAPI int WINAPI GetTextFaceW(HDC dc, int count, LPWSTR name);
WINGDIAPI BOOL WINAPI GetCharWidth32W(HDC dc, UINT first, UINT last, LPINT widths);
WINGDIAPI BOOL WINAPI GetCharABCWidthsW(HDC dc, UINT first, UINT last, LPABC abc);
WINGDIAPI BOOL WINAPI MoveToEx(HDC dc, int x, int y, LPPOINT old);
WINGDIAPI BOOL WINAPI GetCurrentPositionEx(HDC dc, LPPOINT point);
WINGDIAPI BOOL WINAPI LineTo(HDC dc, int x, int y);
WINGDIAPI BOOL WINAPI Rectangle(HDC dc, int left, int top, int right, int bottom);
WINGDIAPI BOOL WINAPI RoundRect(HDC dc, int left, int top, int right, int bottom, int width, int height);
WINGDIAPI BOOL WINAPI Ellipse(HDC dc, int left, int top, int right, int bottom);
WINGDIAPI BOOL WINAPI Polyline(HDC dc, const POINT *points, int count);
WINGDIAPI BOOL WINAPI Polygon(HDC dc, const POINT *points, int count);
WINGDIAPI COLORREF WINAPI SetPixel(HDC dc, int x, int y, COLORREF color);
WINGDIAPI COLORREF WINAPI GetPixel(HDC dc, int x, int y);
WINGDIAPI BOOL WINAPI SetViewportOrgEx(HDC dc, int x, int y, LPPOINT old);
WINGDIAPI BOOL WINAPI GetViewportOrgEx(HDC dc, LPPOINT point);
WINGDIAPI BOOL WINAPI SetWindowOrgEx(HDC dc, int x, int y, LPPOINT old);
WINGDIAPI BOOL WINAPI GetWindowOrgEx(HDC dc, LPPOINT point);
WINGDIAPI BOOL WINAPI OffsetViewportOrgEx(HDC dc, int x, int y, LPPOINT old);
WINGDIAPI BOOL WINAPI LPtoDP(HDC dc, LPPOINT points, int count);
WINGDIAPI BOOL WINAPI DPtoLP(HDC dc, LPPOINT points, int count);
WINGDIAPI HRGN WINAPI CreateRectRgn(int left, int top, int right, int bottom);
WINGDIAPI HRGN WINAPI CreateRectRgnIndirect(const RECT *rect);
WINGDIAPI HRGN WINAPI CreateRoundRectRgn(int left, int top, int right, int bottom, int width, int height);
WINGDIAPI int WINAPI CombineRgn(HRGN dst, HRGN a, HRGN b, int mode);
WINGDIAPI BOOL WINAPI SetRectRgn(HRGN region, int left, int top, int right, int bottom);
WINGDIAPI int WINAPI OffsetRgn(HRGN region, int x, int y);
WINGDIAPI int WINAPI GetRgnBox(HRGN region, LPRECT rect);
WINGDIAPI BOOL WINAPI PtInRegion(HRGN region, int x, int y);
WINGDIAPI BOOL WINAPI RectInRegion(HRGN region, const RECT *rect);
WINGDIAPI BOOL WINAPI EqualRgn(HRGN a, HRGN b);
WINGDIAPI BOOL WINAPI FillRgn(HDC dc, HRGN region, HBRUSH brush);
WINGDIAPI BOOL WINAPI FrameRgn(HDC dc, HRGN region, HBRUSH brush, int width, int height);
WINGDIAPI BOOL WINAPI PaintRgn(HDC dc, HRGN region);
WINGDIAPI DWORD WINAPI GetRegionData(HRGN region, DWORD size, LPRGNDATA data);
WINGDIAPI int WINAPI SelectClipRgn(HDC dc, HRGN region);
WINGDIAPI int WINAPI ExtSelectClipRgn(HDC dc, HRGN region, int mode);
WINGDIAPI int WINAPI GetClipBox(HDC dc, LPRECT rect);
WINGDIAPI int WINAPI GetClipRgn(HDC dc, HRGN region);
WINGDIAPI int WINAPI IntersectClipRect(HDC dc, int left, int top, int right, int bottom);
WINGDIAPI int WINAPI ExcludeClipRect(HDC dc, int left, int top, int right, int bottom);
WINGDIAPI HGDIOBJ WINAPI GetCurrentObject(HDC dc, UINT type);
WINGDIAPI int WINAPI SetStretchBltMode(HDC dc, int mode);
WINGDIAPI int WINAPI SetGraphicsMode(HDC dc, int mode);
WINGDIAPI int WINAPI SetMapMode(HDC dc, int mode);
WINGDIAPI int WINAPI EnumFontFamiliesExW(HDC dc, LPLOGFONTW lf, FONTENUMPROCW proc, LPARAM param, DWORD flags);
WINGDIAPI int WINAPI AddFontResourceExW(LPCWSTR name, DWORD flags, PVOID reserved);

WINGDIAPI LONG WINAPI GdiGetCharDimensions(HDC dc, LPTEXTMETRICW tm, LONG *height);
WINGDIAPI BOOL WINAPI GetTextExtentPointW(HDC dc, LPCWSTR text, int count, LPSIZE size);
WINGDIAPI DWORD WINAPI SetLayout(HDC dc, DWORD layout);
WINGDIAPI int WINAPI SetPolyFillMode(HDC dc, int mode);

/* ------------------------------------------------------------------ font enumeration and outlines */

#define RASTER_FONTTYPE 0x0001
#define DEVICE_FONTTYPE 0x0002
#define TRUETYPE_FONTTYPE 0x0004

#define NTM_REGULAR 0x00000040L
#define NTM_BOLD 0x00000020L
#define NTM_ITALIC 0x00000001L
#define NTM_NONNEGATIVE_AC 0x00010000
#define NTM_PS_OPENTYPE 0x00020000
#define NTM_TT_OPENTYPE 0x00040000
#define NTM_MULTIPLEMASTER 0x00080000
#define NTM_TYPE1 0x00100000
#define NTM_DSIG 0x00200000

#define FR_PRIVATE 0x10
#define FR_NOT_ENUM 0x20

#define HANGUL_CHARSET 129
#define HANGEUL_CHARSET 129
#define GREEK_CHARSET 161
#define TURKISH_CHARSET 162
#define VIETNAMESE_CHARSET 163
#define HEBREW_CHARSET 177
#define ARABIC_CHARSET 178
#define BALTIC_CHARSET 186
#define RUSSIAN_CHARSET 204
#define THAI_CHARSET 222
#define EASTEUROPE_CHARSET 238
#define MAC_CHARSET 77
#define JOHAB_CHARSET 130

typedef struct tagFONTSIGNATURE {
    DWORD fsUsb[4];
    DWORD fsCsb[2];
} FONTSIGNATURE, *PFONTSIGNATURE, *LPFONTSIGNATURE;

typedef struct tagCHARSETINFO {
    UINT ciCharset;
    UINT ciACP;
    FONTSIGNATURE fs;
} CHARSETINFO, *PCHARSETINFO, *LPCHARSETINFO;

#define TCI_SRCCHARSET 1
#define TCI_SRCCODEPAGE 2
#define TCI_SRCFONTSIG 3
#define TCI_SRCLOCALE 0x1000

typedef struct tagENUMLOGFONTEXW {
    LOGFONTW elfLogFont;
    WCHAR elfFullName[LF_FULLFACESIZE];
    WCHAR elfStyle[LF_FACESIZE];
    WCHAR elfScript[LF_FACESIZE];
} ENUMLOGFONTEXW, *LPENUMLOGFONTEXW;

typedef struct tagENUMLOGFONTEXA {
    LOGFONTA elfLogFont;
    BYTE elfFullName[LF_FULLFACESIZE];
    BYTE elfStyle[LF_FACESIZE];
    BYTE elfScript[LF_FACESIZE];
} ENUMLOGFONTEXA, *LPENUMLOGFONTEXA;

typedef struct tagNEWTEXTMETRICW {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    WCHAR tmFirstChar;
    WCHAR tmLastChar;
    WCHAR tmDefaultChar;
    WCHAR tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
    DWORD ntmFlags;
    UINT ntmSizeEM;
    UINT ntmCellHeight;
    UINT ntmAvgWidth;
} NEWTEXTMETRICW, *PNEWTEXTMETRICW, *LPNEWTEXTMETRICW;

typedef struct tagNEWTEXTMETRICA {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    BYTE tmFirstChar;
    BYTE tmLastChar;
    BYTE tmDefaultChar;
    BYTE tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
    DWORD ntmFlags;
    UINT ntmSizeEM;
    UINT ntmCellHeight;
    UINT ntmAvgWidth;
} NEWTEXTMETRICA, *PNEWTEXTMETRICA, *LPNEWTEXTMETRICA;

typedef struct tagNEWTEXTMETRICEXW {
    NEWTEXTMETRICW ntmTm;
    FONTSIGNATURE ntmFontSig;
} NEWTEXTMETRICEXW;

typedef struct tagNEWTEXTMETRICEXA {
    NEWTEXTMETRICA ntmTm;
    FONTSIGNATURE ntmFontSig;
} NEWTEXTMETRICEXA;

typedef int(CALLBACK *FONTENUMPROCA)(const LOGFONTA *lf, const TEXTMETRICA *tm, DWORD type, LPARAM param);

typedef struct tagPANOSE {
    BYTE bFamilyType;
    BYTE bSerifStyle;
    BYTE bWeight;
    BYTE bProportion;
    BYTE bContrast;
    BYTE bStrokeVariation;
    BYTE bArmStyle;
    BYTE bLetterform;
    BYTE bMidline;
    BYTE bXHeight;
} PANOSE, *LPPANOSE;

typedef struct _OUTLINETEXTMETRICW {
    UINT otmSize;
    TEXTMETRICW otmTextMetrics;
    BYTE otmFiller;
    PANOSE otmPanoseNumber;
    UINT otmfsSelection;
    UINT otmfsType;
    int otmsCharSlopeRise;
    int otmsCharSlopeRun;
    int otmItalicAngle;
    UINT otmEMSquare;
    int otmAscent;
    int otmDescent;
    UINT otmLineGap;
    UINT otmsCapEmHeight;
    UINT otmsXHeight;
    RECT otmrcFontBox;
    int otmMacAscent;
    int otmMacDescent;
    UINT otmMacLineGap;
    UINT otmusMinimumPPEM;
    POINT otmptSubscriptSize;
    POINT otmptSubscriptOffset;
    POINT otmptSuperscriptSize;
    POINT otmptSuperscriptOffset;
    UINT otmsStrikeoutSize;
    int otmsStrikeoutPosition;
    int otmsUnderscoreSize;
    int otmsUnderscorePosition;
    PSTR otmpFamilyName;
    PSTR otmpFaceName;
    PSTR otmpStyleName;
    PSTR otmpFullName;
} OUTLINETEXTMETRICW, *POUTLINETEXTMETRICW, *LPOUTLINETEXTMETRICW;

typedef struct _OUTLINETEXTMETRICA {
    UINT otmSize;
    TEXTMETRICA otmTextMetrics;
    BYTE otmFiller;
    PANOSE otmPanoseNumber;
    UINT otmfsSelection;
    UINT otmfsType;
    int otmsCharSlopeRise;
    int otmsCharSlopeRun;
    int otmItalicAngle;
    UINT otmEMSquare;
    int otmAscent;
    int otmDescent;
    UINT otmLineGap;
    UINT otmsCapEmHeight;
    UINT otmsXHeight;
    RECT otmrcFontBox;
    int otmMacAscent;
    int otmMacDescent;
    UINT otmMacLineGap;
    UINT otmusMinimumPPEM;
    POINT otmptSubscriptSize;
    POINT otmptSubscriptOffset;
    POINT otmptSuperscriptSize;
    POINT otmptSuperscriptOffset;
    UINT otmsStrikeoutSize;
    int otmsStrikeoutPosition;
    int otmsUnderscoreSize;
    int otmsUnderscorePosition;
    PSTR otmpFamilyName;
    PSTR otmpFaceName;
    PSTR otmpStyleName;
    PSTR otmpFullName;
} OUTLINETEXTMETRICA, *POUTLINETEXTMETRICA, *LPOUTLINETEXTMETRICA;

typedef struct _FIXED {
    WORD fract;
    short value;
} FIXED;

typedef struct _MAT2 {
    FIXED eM11;
    FIXED eM12;
    FIXED eM21;
    FIXED eM22;
} MAT2, *LPMAT2;

typedef struct _GLYPHMETRICS {
    UINT gmBlackBoxX;
    UINT gmBlackBoxY;
    POINT gmptGlyphOrigin;
    short gmCellIncX;
    short gmCellIncY;
} GLYPHMETRICS, *LPGLYPHMETRICS;

typedef struct tagPOINTFX {
    FIXED x;
    FIXED y;
} POINTFX, *LPPOINTFX;

typedef struct tagTTPOLYCURVE {
    WORD wType;
    WORD cpfx;
    POINTFX apfx[1];
} TTPOLYCURVE, *LPTTPOLYCURVE;

typedef struct tagTTPOLYGONHEADER {
    DWORD cb;
    DWORD dwType;
    POINTFX pfxStart;
} TTPOLYGONHEADER, *LPTTPOLYGONHEADER;

#define GGO_METRICS 0
#define GGO_BITMAP 1
#define GGO_NATIVE 2
#define GGO_BEZIER 3
#define GGO_GRAY2_BITMAP 4
#define GGO_GRAY4_BITMAP 5
#define GGO_GRAY8_BITMAP 6
#define GGO_GLYPH_INDEX 0x0080
#define GGO_UNHINTED 0x0100
#define TT_POLYGON_TYPE 24
#define TT_PRIM_LINE 1
#define TT_PRIM_QSPLINE 2
#define TT_PRIM_CSPLINE 3

typedef struct tagWCRANGE {
    WCHAR wcLow;
    USHORT cGlyphs;
} WCRANGE, *PWCRANGE, *LPWCRANGE;

typedef struct tagGLYPHSET {
    DWORD cbThis;
    DWORD flAccel;
    DWORD cGlyphsSupported;
    DWORD cRanges;
    WCRANGE ranges[1];
} GLYPHSET, *PGLYPHSET, *LPGLYPHSET;

#define GS_8BIT_INDICES 0x00000001
#define GGI_MARK_NONEXISTING_GLYPHS 0x0001

typedef struct _ABCFLOAT {
    FLOAT abcfA;
    FLOAT abcfB;
    FLOAT abcfC;
} ABCFLOAT, *PABCFLOAT, *LPABCFLOAT;

typedef struct tagGCP_RESULTSW {
    DWORD lStructSize;
    LPWSTR lpOutString;
    UINT *lpOrder;
    int *lpDx;
    int *lpCaretPos;
    LPSTR lpClass;
    LPWSTR lpGlyphs;
    UINT nGlyphs;
    int nMaxFit;
} GCP_RESULTSW, *LPGCP_RESULTSW;

typedef struct tagGCP_RESULTSA {
    DWORD lStructSize;
    LPSTR lpOutString;
    UINT *lpOrder;
    int *lpDx;
    int *lpCaretPos;
    LPSTR lpClass;
    LPWSTR lpGlyphs;
    UINT nGlyphs;
    int nMaxFit;
} GCP_RESULTSA, *LPGCP_RESULTSA;

#define GCP_DBCS 0x0001
#define GCP_REORDER 0x0002
#define GCP_USEKERNING 0x0008
#define GCP_GLYPHSHAPE 0x0010
#define GCP_LIGATE 0x0020
#define GCP_DIACRITIC 0x0100
#define GCP_KASHIDA 0x0400
#define GCP_ERROR 0x8000
#define GCP_JUSTIFY 0x00010000L
#define GCP_CLASSIN 0x00080000L
#define GCP_MAXEXTENT 0x00100000L
#define GCP_JUSTIFYIN 0x00200000L
#define GCP_DISPLAYZWG 0x00400000L
#define GCP_SYMSWAPOFF 0x00800000L
#define GCP_NUMERICOVERRIDE 0x01000000L
#define GCP_NEUTRALOVERRIDE 0x02000000L
#define GCP_NUMERICSLATIN 0x04000000L
#define GCP_NUMERICSLOCAL 0x08000000L

#define MM_MAX_NUMAXES 16
typedef struct tagDESIGNVECTOR {
    DWORD dvReserved;
    DWORD dvNumAxes;
    LONG dvValues[MM_MAX_NUMAXES];
} DESIGNVECTOR, *PDESIGNVECTOR, *LPDESIGNVECTOR;

/* ------------------------------------------------------------------ paths and transforms */

#define PT_CLOSEFIGURE 0x01
#define PT_LINETO 0x02
#define PT_BEZIERTO 0x04
#define PT_MOVETO 0x06

#define ALTERNATE 1
#define WINDING 2

#define MWT_IDENTITY 1
#define MWT_LEFTMULTIPLY 2
#define MWT_RIGHTMULTIPLY 3
#define MWT_SET 4

#define AD_COUNTERCLOCKWISE 1
#define AD_CLOCKWISE 2

/* ------------------------------------------------------------------ printing and escapes */

#define SP_NOTREPORTED 0x4000
#define SP_ERROR (-1)
#define SP_APPABORT (-2)
#define SP_USERABORT (-3)
#define SP_OUTOFDISK (-4)
#define SP_OUTOFMEMORY (-5)

#define QUERYESCSUPPORT 8
#define PASSTHROUGH 19
#define GETTECHNOLOGY 20
#define POSTSCRIPT_PASSTHROUGH 4115
#define CHECKJPEGFORMAT 4119
#define CHECKPNGFORMAT 4120

typedef struct _DOCINFOW {
    int cbSize;
    LPCWSTR lpszDocName;
    LPCWSTR lpszOutput;
    LPCWSTR lpszDatatype;
    DWORD fwType;
} DOCINFOW, *LPDOCINFOW;

typedef struct _DOCINFOA {
    int cbSize;
    LPCSTR lpszDocName;
    LPCSTR lpszOutput;
    LPCSTR lpszDatatype;
    DWORD fwType;
} DOCINFOA, *LPDOCINFOA;

typedef int(CALLBACK *ABORTPROC)(HDC dc, int code);

/* ------------------------------------------------------------------ pixel formats */

typedef struct tagPIXELFORMATDESCRIPTOR {
    WORD nSize;
    WORD nVersion;
    DWORD dwFlags;
    BYTE iPixelType;
    BYTE cColorBits;
    BYTE cRedBits;
    BYTE cRedShift;
    BYTE cGreenBits;
    BYTE cGreenShift;
    BYTE cBlueBits;
    BYTE cBlueShift;
    BYTE cAlphaBits;
    BYTE cAlphaShift;
    BYTE cAccumBits;
    BYTE cAccumRedBits;
    BYTE cAccumGreenBits;
    BYTE cAccumBlueBits;
    BYTE cAccumAlphaBits;
    BYTE cDepthBits;
    BYTE cStencilBits;
    BYTE cAuxBuffers;
    BYTE iLayerType;
    BYTE bReserved;
    DWORD dwLayerMask;
    DWORD dwVisibleMask;
    DWORD dwDamageMask;
} PIXELFORMATDESCRIPTOR, *PPIXELFORMATDESCRIPTOR, *LPPIXELFORMATDESCRIPTOR;

typedef struct tagLAYERPLANEDESCRIPTOR {
    WORD nSize;
    WORD nVersion;
    DWORD dwFlags;
    BYTE iPixelType;
    BYTE cColorBits;
    BYTE cRedBits;
    BYTE cRedShift;
    BYTE cGreenBits;
    BYTE cGreenShift;
    BYTE cBlueBits;
    BYTE cBlueShift;
    BYTE cAlphaBits;
    BYTE cAlphaShift;
    BYTE cAccumBits;
    BYTE cAccumRedBits;
    BYTE cAccumGreenBits;
    BYTE cAccumBlueBits;
    BYTE cAccumAlphaBits;
    BYTE cDepthBits;
    BYTE cStencilBits;
    BYTE cAuxBuffers;
    BYTE iLayerPlane;
    BYTE bReserved;
    COLORREF crTransparent;
} LAYERPLANEDESCRIPTOR, *PLAYERPLANEDESCRIPTOR, *LPLAYERPLANEDESCRIPTOR;

typedef struct _POINTFLOAT {
    FLOAT x;
    FLOAT y;
} POINTFLOAT, *PPOINTFLOAT;

typedef struct _GLYPHMETRICSFLOAT {
    FLOAT gmfBlackBoxX;
    FLOAT gmfBlackBoxY;
    POINTFLOAT gmfptGlyphOrigin;
    FLOAT gmfCellIncX;
    FLOAT gmfCellIncY;
} GLYPHMETRICSFLOAT, *PGLYPHMETRICSFLOAT, *LPGLYPHMETRICSFLOAT;

#define PFD_TYPE_RGBA 0
#define PFD_TYPE_COLORINDEX 1
#define PFD_MAIN_PLANE 0
#define PFD_OVERLAY_PLANE 1
#define PFD_UNDERLAY_PLANE (-1)
#define PFD_DOUBLEBUFFER 0x00000001
#define PFD_STEREO 0x00000002
#define PFD_DRAW_TO_WINDOW 0x00000004
#define PFD_DRAW_TO_BITMAP 0x00000008
#define PFD_SUPPORT_GDI 0x00000010
#define PFD_SUPPORT_OPENGL 0x00000020
#define PFD_GENERIC_FORMAT 0x00000040
#define PFD_NEED_PALETTE 0x00000080
#define PFD_NEED_SYSTEM_PALETTE 0x00000100
#define PFD_SWAP_EXCHANGE 0x00000200
#define PFD_SWAP_COPY 0x00000400
#define PFD_SWAP_LAYER_BUFFERS 0x00000800
#define PFD_GENERIC_ACCELERATED 0x00001000
#define PFD_SUPPORT_DIRECTDRAW 0x00002000
#define PFD_DIRECT3D_ACCELERATED 0x00004000
#define PFD_SUPPORT_COMPOSITION 0x00008000
#define PFD_DEPTH_DONTCARE 0x20000000
#define PFD_DOUBLEBUFFER_DONTCARE 0x40000000
#define PFD_STEREO_DONTCARE 0x80000000

/* ------------------------------------------------------------------ more gdi32 exports */

WINGDIAPI int WINAPI GetPixelFormat(HDC dc);
WINGDIAPI int WINAPI ChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR *pfd);
WINGDIAPI int WINAPI DescribePixelFormat(HDC dc, int format, UINT size, LPPIXELFORMATDESCRIPTOR pfd);
WINGDIAPI BOOL WINAPI SetPixelFormat(HDC dc, int format, const PIXELFORMATDESCRIPTOR *pfd);
WINGDIAPI BOOL WINAPI SwapBuffers(HDC dc);
WINGDIAPI BOOL WINAPI BeginPath(HDC dc);
WINGDIAPI BOOL WINAPI EndPath(HDC dc);
WINGDIAPI BOOL WINAPI AbortPath(HDC dc);
WINGDIAPI BOOL WINAPI CloseFigure(HDC dc);
WINGDIAPI BOOL WINAPI FillPath(HDC dc);
WINGDIAPI BOOL WINAPI StrokePath(HDC dc);
WINGDIAPI BOOL WINAPI StrokeAndFillPath(HDC dc);
WINGDIAPI BOOL WINAPI FlattenPath(HDC dc);
WINGDIAPI BOOL WINAPI SelectClipPath(HDC dc, int mode);
WINGDIAPI HRGN WINAPI PathToRegion(HDC dc);
WINGDIAPI int WINAPI GetPath(HDC dc, LPPOINT points, LPBYTE types, int count);
WINGDIAPI BOOL WINAPI PolyBezier(HDC dc, const POINT *points, DWORD count);
WINGDIAPI BOOL WINAPI PolyBezierTo(HDC dc, const POINT *points, DWORD count);
WINGDIAPI BOOL WINAPI PolylineTo(HDC dc, const POINT *points, DWORD count);
WINGDIAPI BOOL WINAPI SetWorldTransform(HDC dc, const XFORM *xform);
WINGDIAPI BOOL WINAPI GetWorldTransform(HDC dc, LPXFORM xform);
WINGDIAPI BOOL WINAPI ModifyWorldTransform(HDC dc, const XFORM *xform, DWORD mode);
WINGDIAPI int WINAPI StartDocW(HDC dc, const DOCINFOW *info);
WINGDIAPI int WINAPI StartDocA(HDC dc, const DOCINFOA *info);
WINGDIAPI int WINAPI EndDoc(HDC dc);
WINGDIAPI int WINAPI StartPage(HDC dc);
WINGDIAPI int WINAPI EndPage(HDC dc);
WINGDIAPI int WINAPI AbortDoc(HDC dc);
WINGDIAPI int WINAPI SetAbortProc(HDC dc, ABORTPROC proc);
WINGDIAPI HDC WINAPI ResetDCW(HDC dc, const void *mode);
WINGDIAPI int WINAPI ExtEscape(HDC dc, int escape, int in_size, LPCSTR in, int out_size, LPSTR out);
WINGDIAPI int WINAPI Escape(HDC dc, int escape, int in_size, LPCSTR in, LPVOID out);
WINGDIAPI LONG WINAPI GetBitmapBits(HBITMAP bitmap, LONG size, LPVOID bits);
WINGDIAPI LONG WINAPI SetBitmapBits(HBITMAP bitmap, DWORD size, const void *bits);
WINGDIAPI HANDLE WINAPI AddFontMemResourceEx(PVOID data, DWORD size, PVOID reserved, DWORD *count);
WINGDIAPI BOOL WINAPI RemoveFontMemResourceEx(HANDLE handle);
WINGDIAPI BOOL WINAPI RemoveFontResourceExW(LPCWSTR name, DWORD flags, PVOID reserved);
WINGDIAPI BOOL WINAPI RemoveFontResourceW(LPCWSTR name);
WINGDIAPI UINT WINAPI GetOutlineTextMetricsW(HDC dc, UINT size, LPOUTLINETEXTMETRICW otm);
WINGDIAPI UINT WINAPI GetOutlineTextMetricsA(HDC dc, UINT size, LPOUTLINETEXTMETRICA otm);
WINGDIAPI DWORD WINAPI GetFontUnicodeRanges(HDC dc, LPGLYPHSET set);
WINGDIAPI DWORD WINAPI GetGlyphOutlineW(HDC dc, UINT ch, UINT format, LPGLYPHMETRICS gm, DWORD size, LPVOID buffer,
                                        const MAT2 *mat);
WINGDIAPI DWORD WINAPI GetGlyphOutlineA(HDC dc, UINT ch, UINT format, LPGLYPHMETRICS gm, DWORD size, LPVOID buffer,
                                        const MAT2 *mat);
WINGDIAPI DWORD WINAPI GetGlyphIndicesW(HDC dc, LPCWSTR text, int count, LPWORD indices, DWORD flags);
WINGDIAPI BOOL WINAPI GetCharABCWidthsFloatW(HDC dc, UINT first, UINT last, LPABCFLOAT abc);
WINGDIAPI BOOL WINAPI GetCharABCWidthsI(HDC dc, UINT first, UINT count, LPWORD glyphs, LPABC abc);
WINGDIAPI BOOL WINAPI GetCharWidth32A(HDC dc, UINT first, UINT last, LPINT widths);
WINGDIAPI BOOL WINAPI GetCharWidthI(HDC dc, UINT first, UINT count, LPWORD glyphs, LPINT widths);
WINGDIAPI BOOL WINAPI GetTextExtentPointI(HDC dc, LPWORD glyphs, int count, LPSIZE size);
WINGDIAPI DWORD WINAPI GetCharacterPlacementW(HDC dc, LPCWSTR text, int count, int max_extent,
                                              LPGCP_RESULTSW results, DWORD flags);
WINGDIAPI DWORD WINAPI GetCharacterPlacementA(HDC dc, LPCSTR text, int count, int max_extent,
                                              LPGCP_RESULTSA results, DWORD flags);
WINGDIAPI BOOL WINAPI TranslateCharsetInfo(DWORD *source, LPCHARSETINFO info, DWORD flags);
WINGDIAPI int WINAPI EnumFontFamiliesExA(HDC dc, LPLOGFONTA lf, FONTENUMPROCA proc, LPARAM param, DWORD flags);
WINGDIAPI int WINAPI EnumFontFamiliesW(HDC dc, LPCWSTR family, FONTENUMPROCW proc, LPARAM param);
WINGDIAPI DWORD WINAPI GetFontData(HDC dc, DWORD table, DWORD offset, PVOID buffer, DWORD size);
WINGDIAPI HRGN WINAPI ExtCreateRegion(const XFORM *xform, DWORD size, const RGNDATA *data);

/* ------------------------------------------------------------------ palettes */

typedef struct tagPALETTEENTRY {
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
} PALETTEENTRY, *PPALETTEENTRY, *LPPALETTEENTRY;

typedef struct tagLOGPALETTE {
    WORD palVersion;
    WORD palNumEntries;
    PALETTEENTRY palPalEntry[1];
} LOGPALETTE, *PLOGPALETTE, *LPLOGPALETTE;

#define PC_RESERVED 0x01
#define PC_EXPLICIT 0x02
#define PC_NOCOLLAPSE 0x04
#define PALETTERGB(r, g, b) (0x02000000 | RGB(r, g, b))
#define PALETTEINDEX(i) ((COLORREF)(0x01000000 | (DWORD)(WORD)(i)))

WINGDIAPI HPALETTE WINAPI CreatePalette(const LOGPALETTE *palette);
WINGDIAPI HPALETTE WINAPI SelectPalette(HDC dc, HPALETTE palette, BOOL background);
WINGDIAPI UINT WINAPI RealizePalette(HDC dc);
WINGDIAPI UINT WINAPI GetPaletteEntries(HPALETTE palette, UINT start, UINT count, LPPALETTEENTRY entries);
WINGDIAPI UINT WINAPI SetPaletteEntries(HPALETTE palette, UINT start, UINT count, const PALETTEENTRY *entries);
WINGDIAPI UINT WINAPI GetNearestPaletteIndex(HPALETTE palette, COLORREF color);
WINGDIAPI UINT WINAPI GetSystemPaletteEntries(HDC dc, UINT start, UINT count, LPPALETTEENTRY entries);
WINGDIAPI UINT WINAPI SetDIBColorTable(HDC dc, UINT start, UINT count, const RGBQUAD *colors);
WINGDIAPI COLORREF WINAPI GetNearestColor(HDC dc, COLORREF color);

/* ------------------------------------------------------------------ metafiles */

typedef struct tagMETARECORD {
    DWORD rdSize;
    WORD rdFunction;
    WORD rdParm[1];
} METARECORD, *PMETARECORD, *LPMETARECORD;

typedef struct tagHANDLETABLE {
    HGDIOBJ objectHandle[1];
} HANDLETABLE, *PHANDLETABLE, *LPHANDLETABLE;

typedef int(CALLBACK *MFENUMPROC)(HDC dc, HANDLETABLE *table, METARECORD *record, int objects, LPARAM param);

WINGDIAPI HDC WINAPI CreateMetaFileW(LPCWSTR file);
WINGDIAPI HMETAFILE WINAPI CloseMetaFile(HDC dc);
WINGDIAPI BOOL WINAPI DeleteMetaFile(HMETAFILE metafile);
WINGDIAPI BOOL WINAPI PlayMetaFile(HDC dc, HMETAFILE metafile);
WINGDIAPI BOOL WINAPI PlayMetaFileRecord(HDC dc, LPHANDLETABLE table, LPMETARECORD record, UINT handles);
WINGDIAPI BOOL WINAPI EnumMetaFile(HDC dc, HMETAFILE metafile, MFENUMPROC proc, LPARAM param);
WINGDIAPI HMETAFILE WINAPI CopyMetaFileW(HMETAFILE metafile, LPCWSTR file);

/* ------------------------------------------------------------------ gradients and blending */

typedef USHORT COLOR16;

typedef struct _TRIVERTEX {
    LONG x;
    LONG y;
    COLOR16 Red;
    COLOR16 Green;
    COLOR16 Blue;
    COLOR16 Alpha;
} TRIVERTEX, *PTRIVERTEX, *LPTRIVERTEX;

typedef struct _GRADIENT_RECT {
    ULONG UpperLeft;
    ULONG LowerRight;
} GRADIENT_RECT, *PGRADIENT_RECT, *LPGRADIENT_RECT;

typedef struct _GRADIENT_TRIANGLE {
    ULONG Vertex1;
    ULONG Vertex2;
    ULONG Vertex3;
} GRADIENT_TRIANGLE, *PGRADIENT_TRIANGLE, *LPGRADIENT_TRIANGLE;

#define GRADIENT_FILL_RECT_H 0x00000000
#define GRADIENT_FILL_RECT_V 0x00000001
#define GRADIENT_FILL_TRIANGLE 0x00000002
#define GRADIENT_FILL_OP_FLAG 0x000000FF

WINGDIAPI BOOL WINAPI GdiTransparentBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                        int src_width, int src_height, UINT transparent);
WINGDIAPI BOOL WINAPI GdiGradientFill(HDC dc, PTRIVERTEX vertices, ULONG vertex_count, PVOID mesh, ULONG mesh_count,
                                      ULONG mode);

/* msimg32 exports. */
#ifdef MIME_BUILD_MSIMG32
#define MSIMG32API __declspec(dllexport)
#else
#define MSIMG32API DECLSPEC_IMPORT
#endif

MSIMG32API BOOL WINAPI AlphaBlend(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                  int src_width, int src_height, BLENDFUNCTION blend);
MSIMG32API BOOL WINAPI TransparentBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                      int src_width, int src_height, UINT transparent);
MSIMG32API BOOL WINAPI GradientFill(HDC dc, PTRIVERTEX vertices, ULONG vertex_count, PVOID mesh, ULONG mesh_count,
                                    ULONG mode);

/* ------------------------------------------------------------------ mapping, clipping and miscellany */

typedef struct tagCOLORADJUSTMENT {
    WORD caSize;
    WORD caFlags;
    WORD caIlluminantIndex;
    WORD caRedGamma;
    WORD caGreenGamma;
    WORD caBlueGamma;
    WORD caReferenceBlack;
    WORD caReferenceWhite;
    SHORT caContrast;
    SHORT caBrightness;
    SHORT caColorfulness;
    SHORT caRedGreenTint;
} COLORADJUSTMENT, *PCOLORADJUSTMENT, *LPCOLORADJUSTMENT;

#define DCB_RESET 0x0001
#define DCB_ACCUMULATE 0x0002
#define DCB_DIRTY DCB_ACCUMULATE
#define DCB_SET (DCB_RESET | DCB_ACCUMULATE)
#define DCB_ENABLE 0x0004
#define DCB_DISABLE 0x0008

#define FLOODFILLBORDER 0
#define FLOODFILLSURFACE 1

#define ASPECT_FILTERING 0x0001

WINGDIAPI BOOL WINAPI GetWindowExtEx(HDC dc, LPSIZE size);
WINGDIAPI BOOL WINAPI GetViewportExtEx(HDC dc, LPSIZE size);
WINGDIAPI BOOL WINAPI ScaleViewportExtEx(HDC dc, int x_num, int x_denom, int y_num, int y_denom, LPSIZE old);
WINGDIAPI BOOL WINAPI ScaleWindowExtEx(HDC dc, int x_num, int x_denom, int y_num, int y_denom, LPSIZE old);
WINGDIAPI HRGN WINAPI CreateEllipticRgn(int left, int top, int right, int bottom);
WINGDIAPI BOOL WINAPI RectVisible(HDC dc, const RECT *rect);
WINGDIAPI BOOL WINAPI PtVisible(HDC dc, int x, int y);
WINGDIAPI int WINAPI OffsetClipRgn(HDC dc, int x, int y);
WINGDIAPI HBRUSH WINAPI CreatePatternBrush(HBITMAP bitmap);
WINGDIAPI HBRUSH WINAPI CreateDIBPatternBrushPt(const void *packed, UINT usage);
WINGDIAPI BOOL WINAPI GetBitmapDimensionEx(HBITMAP bitmap, LPSIZE size);
WINGDIAPI BOOL WINAPI SetBitmapDimensionEx(HBITMAP bitmap, int width, int height, LPSIZE old);
WINGDIAPI BOOL WINAPI SetTextJustification(HDC dc, int extra, int breaks);
WINGDIAPI DWORD WINAPI SetMapperFlags(HDC dc, DWORD flags);
WINGDIAPI BOOL WINAPI SetColorAdjustment(HDC dc, const COLORADJUSTMENT *adjustment);
WINGDIAPI UINT WINAPI GetBoundsRect(HDC dc, LPRECT rect, UINT flags);
WINGDIAPI BOOL WINAPI ArcTo(HDC dc, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2);
WINGDIAPI BOOL WINAPI PolyDraw(HDC dc, const POINT *points, const BYTE *types, int count);
WINGDIAPI BOOL WINAPI ExtFloodFill(HDC dc, int x, int y, COLORREF color, UINT type);

#endif
