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

#endif
