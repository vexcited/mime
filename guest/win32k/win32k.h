/* win32k window manager and GDI interfaces. */
#ifndef MIME_WIN32K_H
#define MIME_WIN32K_H

#define MIME_BUILD_WIN32K 1
#include <mime/ddk.h>
#include <mime/abi.h>
#include <mime/ntuser.h>
#include <mime/win32/winerror.h>

#include "rtl.h"

#define W32_TAG(a, b, c, d) ((ULONG)(a) | (ULONG)(b) << 8 | (ULONG)(c) << 16 | (ULONG)(d) << 24)

void *W32Alloc(SIZE_T size);  /* zeroed nonpaged pool. */
void W32Free(void *p);
PWSTR W32StrDup(PCWSTR s, SIZE_T chars);
#define W32Trace(...) DbgPrint(__VA_ARGS__)
void W32SetLastError(ULONG error); /* the calling thread's TEB LastErrorValue. */

static inline LONG W32Min(LONG a, LONG b)
{
    return a < b ? a : b;
}

static inline LONG W32Max(LONG a, LONG b)
{
    return a > b ? a : b;
}

static inline LONG W32MulDiv(LONG a, LONG b, LONG c)
{
    LONG64 v = (LONG64)a * b;
    return (LONG)(v >= 0 ? (v + c / 2) / c : (v - c / 2) / c);
}

/* lock. */

/*
 * one recursive lock serializes the window manager and GDI, like the Windows user critical section. release it around
 * user callbacks and while sleeping in a message loop.
 */
void UserEnter(void);
void UserEnterRead(void);
void UserReenterRead(ULONG depth);
void UserLeave(void);
ULONG UserLeaveAll(void);
void UserReenter(ULONG depth);
BOOLEAN UserIsOwner(void);

/* geometry. */

static inline BOOLEAN RectEmpty(const RECT *r)
{
    return r->left >= r->right || r->top >= r->bottom;
}

static inline BOOLEAN RectIntersect(RECT *out, const RECT *a, const RECT *b)
{
    out->left = W32Max(a->left, b->left);
    out->top = W32Max(a->top, b->top);
    out->right = W32Min(a->right, b->right);
    out->bottom = W32Min(a->bottom, b->bottom);
    if (RectEmpty(out)) {
        *out = (RECT){0, 0, 0, 0};
        return FALSE;
    }
    return TRUE;
}

static inline void RectOffset(RECT *r, LONG dx, LONG dy)
{
    r->left += dx;
    r->right += dx;
    r->top += dy;
    r->bottom += dy;
}

static inline BOOLEAN RectContains(const RECT *r, LONG x, LONG y)
{
    return x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

void RectUnion(RECT *out, const RECT *a, const RECT *b);

/* regions. */

/* y-x banded rectangles: sorted by top, then left, bands share top and bottom. */
typedef struct REGION {
    ULONG Count;
    ULONG Capacity;
    RECT Extents;
    RECT *Rects;
} REGION;

void RgnInit(REGION *r);
void RgnFree(REGION *r);
void RgnClear(REGION *r);
BOOLEAN RgnSetRect(REGION *r, const RECT *rect);
BOOLEAN RgnCopy(REGION *dst, const REGION *src);
BOOLEAN RgnCombine(REGION *dst, const REGION *a, const REGION *b, int mode);
BOOLEAN RgnCombineRect(REGION *dst, const REGION *a, const RECT *rect, int mode);
void RgnOffset(REGION *r, LONG dx, LONG dy);
BOOLEAN RgnIsEmpty(const REGION *r);
int RgnComplexity(const REGION *r);
BOOLEAN RgnPtIn(const REGION *r, LONG x, LONG y);
BOOLEAN RgnRectIn(const REGION *r, const RECT *rect);
BOOLEAN RgnEqual(const REGION *a, const REGION *b);
BOOLEAN RgnFromRects(REGION *r, const RECT *rects, ULONG count);
BOOLEAN RgnRoundRect(REGION *r, LONG left, LONG top, LONG right, LONG bottom, LONG ew, LONG eh);

/* GDI objects. */

enum {
    GDI_TYPE_DC = 0x01,
    GDI_TYPE_REGION = 0x04,
    GDI_TYPE_BITMAP = 0x05,
    GDI_TYPE_PALETTE = 0x08,
    GDI_TYPE_FONT = 0x0A,
    GDI_TYPE_BRUSH = 0x10,
    GDI_TYPE_PEN = 0x30,
    GDI_TYPE_EXTPEN = 0x50,
};

#define GDI_FLAG_STOCK 0x01
#define GDI_FLAG_DELETE_PENDING 0x02 /* deleted while selected into a dc. */
#define GDI_FLAG_SYSTEM 0x04          /* owned by win32k, never freed by applications. */

typedef struct GDI_OBJECT {
    HANDLE Handle;
    UCHAR Type;
    UCHAR Flags;
    USHORT SelectCount; /* DCs that currently select the object. */
    HANDLE OwnerPid;
    PUCHAR User; /* the attributes of a brush, a region or a font in the program that owns it, gdi/shared.c. */
} GDI_OBJECT;

HANDLE GdiCreateHandle(GDI_OBJECT *object, UCHAR type);
void GdiFreeHandle(GDI_OBJECT *object);
GDI_OBJECT *GdiLookup(HANDLE handle, UCHAR type); /* type 0 accepts any. */
UCHAR GdiHandleType(HANDLE handle);
void GdiDeleteObject(GDI_OBJECT *object);
void GdiReleaseProcessObjects(HANDLE pid);
GDI_OBJECT *GdiStockObject(int index);
BOOLEAN GdiLargeStockFonts(void);
HANDLE GdiStockHandle(int index);
void GdiInitializeStockObjects(void);
/*
 * the bitmap GetCurrentObject gives for a dc of the screen or of a window, one for all of them as the surface of the
 * display device on windows: GetObject fails on it, no memory dc takes it and DeleteObject leaves it.
 */
struct SURFACE *GdiDisplayBitmap(void);
struct GDI_REGION *GdiCreateRegion(void);
/* gdi/shared.c: the table gdi32 reads. */
void GdiSharedSet(const GDI_OBJECT *object);
void GdiSharedClear(ULONG index);
void GdiSharedSetUser(HANDLE handle, PVOID user);
void GdiSharedSetFlags(HANDLE handle, UCHAR flags);
void GdiSharedDisplayCaps(void);
void DcAttrFlush(void);
void GdiSharedStock(int index, HANDLE handle);
PUCHAR GdiAttrAlloc(ULONG size);
void GdiAttrFree(GDI_OBJECT *object, ULONG size);
void GdiObjectAttrPull(GDI_OBJECT *object);
void GdiObjectAttrFlush(void);
void GdiObjectAttrForget(GDI_OBJECT *object);
void GdiRegionAttrAttach(GDI_OBJECT *object);
void GdiBrushAttrAttach(GDI_OBJECT *object);
void GdiForEachRegionOf(HANDLE pid, void (*fn)(GDI_OBJECT *object));
/* gdi/batch.c: what gdi32 queued in the teb, run on entry to a service of win32k. */
void NTAPI GdiFlushUserBatch(void);
void GdiThreadExitBatch(void);


/* surfaces back bitmaps, DIB sections and window contents. */
#define SURF_OWNS_BITS 0x01   /* bits allocated from pool. */
#define SURF_USER_BITS 0x02   /* DIB section bits in the owner process. */
#define SURF_DEVICE 0x04      /* window surface: writes are opaque, alpha 255. */
#define SURF_CONTIGUOUS 0x08  /* physically contiguous, presentable to the host. */
#define SURF_TOPDOWN 0x10
#define SURF_RETIRED 0x20     /* replaced window surface, freed when the last DC releases it. */
#define SURF_FOREIGN 0x40     /* user bits the caller owns, from D3DKMTCreateDCFromMemory. */
#define SURF_VSCREEN 0x80     /* the screen in the pixels of a program that is not per monitor aware, see screen.c. */

typedef struct SURFACE {
    GDI_OBJECT Header;
    LONG Width;
    LONG Height;
    LONG Stride;  /* bytes between rows as displayed, negative for bottom-up dibs. */
    UCHAR Bpp;    /* 1, 4, 8, 16, 24 or 32. */
    UCHAR Flags;
    USHORT ColorCount;
    PUCHAR Bits;  /* first row as displayed, row y is at Bits + y * Stride. */
    RGBQUAD Colors[256];
    DWORD Masks[3]; /* 16 and 32 bpp bitfields. */
    PVOID UserBits;  /* user mode address of a DIB section. */
    SIZE_T UserSize;
    PEPROCESS UserOwner;
    PVOID UserViewBase;
    HANDLE UserSection;
    DWORD UserSectionOffset;
    BITMAPINFOHEADER Info; /* as created, for GetObject. */
    RECT Dirty;      /* device surfaces: area changed since the last present. */
    struct WND *Window;
    /*
     * window surfaces. Unpainted: what was never drawn of a window made without a border, which shows what is under it,
     * null for a window that had one. Alpha: what windows' redirection surface keeps as alpha for each pixel while
     * SURF_DEVICE holds 255 there, null while all of it is 0.
     */
    REGION *Unpainted;
    PUCHAR Alpha;
} SURFACE;

SURFACE *SurfaceCreate(LONG width, LONG height, UCHAR bpp, ULONG flags);
SURFACE *SurfaceWrap(LONG width, LONG height, UCHAR bpp, PVOID bits, LONG stride, const BITMAPINFO *info);
void SurfaceDestroy(SURFACE *s);
void SurfaceMarkDirty(SURFACE *s, const RECT *r);
void SurfaceMarkDrawn(SURFACE *s, const RECT *r, const REGION *clip);
void SurfaceSetAlpha(SURFACE *s, LONG x, LONG y, LONG count, UCHAR alpha);
void SurfaceCopiedAlpha(SURFACE *dst, LONG x, LONG y, const SURFACE *src, LONG sx, LONG sy, LONG count);
UCHAR SurfaceAlphaAt(const SURFACE *s, LONG x, LONG y);
PUCHAR SurfaceAlphaPlane(SURFACE *s); /* made on first use, width bytes a row. */
void SurfaceForgetWindow(SURFACE *s);
ULONG SurfaceRawOf(SURFACE *s, ULONG pixel);
ULONG SurfaceGetRaw(SURFACE *s, LONG x, LONG y);
void SurfacePutRaw(SURFACE *s, LONG x, LONG y, ULONG raw);
SURFACE *SurfaceLookup(HBITMAP bitmap);

typedef struct BRUSH {
    GDI_OBJECT Header;
    UINT Style;
    COLORREF Color;
    ULONG_PTR Hatch;
    SURFACE *Pattern;
    int SysColor; /* system color brushes follow color changes, -1 otherwise. */
} BRUSH;

/* ExtCreatePen preserves the pen type, end cap and join. */
typedef struct PEN {
    GDI_OBJECT Header;
    UINT Style;
    LONG Width;
    COLORREF Color;
    UINT BrushStyle;
    ULONG_PTR Hatch;
} PEN;

static inline UINT PenStyle(const PEN *p)
{
    return p->Style & PS_STYLE_MASK;
}

struct FONT_FACE;
struct FONT_INSTANCE;

typedef struct FONT {
    GDI_OBJECT Header;
    LOGFONTW Logical;
} FONT;

typedef struct PALETTE {
    GDI_OBJECT Header;
    ULONG Count;
    PALETTEENTRY *Entries;
} PALETTE;

typedef struct GDI_REGION {
    GDI_OBJECT Header;
    REGION Region;
} GDI_REGION;

/* device contexts. */

typedef struct DC_STATE {
    COLORREF TextColor;
    COLORREF BkColor;
    COLORREF BrushColor;
    COLORREF PenColor;
    int BkMode;
    UINT TextAlign;
    int Rop2;
    int StretchMode;
    int GraphicsMode;
    int MapMode;
    int TextCharExtra;
    LONG BreakExtra; /* SetTextJustification, spread over BreakCount break characters. */
    LONG BreakCount;
    DWORD Layout; /* LAYOUT_RTL and the other flags of SetLayout, kept and not drawn yet. */
    POINT ViewportOrg;
    POINT WindowOrg;
    POINT CurrentPos;
    POINT BrushOrg; /* SetBrushOrgEx, device pixels from the origin of the dc. */
    int RelAbs;     /* SetRelAbs, kept by the kernel only. */
    DWORD AttrFlag; /* DC_ATTR+0xec, which SetMapperFlags writes and NtGdiGetAndSetDCDword 7 swaps. */
    BRUSH *Brush;
    PEN *Pen;
    FONT *Font;
    REGION ClipRgn; /* application clip in device coordinates relative to the DC origin. */
    BOOLEAN HasClip;
    BOOLEAN HasXform; /* World is not the identity. */
    int PolyFillMode;
    int ArcDirection;
    XFORM World;      /* world to page, GM_ADVANCED. */
    SIZE WindowExt;
    SIZE ViewportExt;
    BOOLEAN HasPage;  /* the extents scale or flip. */
    BOOLEAN HasMeta;
    REGION MetaRgn;   /* SetMetaRgn, device coordinates like the clip region. */
    float MiterLimit;
} DC_STATE;

/* a path in logical coordinates with GetPath point types. */
typedef struct GDI_PATH {
    ULONG Count;
    ULONG Capacity;
    POINT *Points;
    BYTE *Types;
    BOOLEAN NewFigure; /* the next point starts a figure. */
} GDI_PATH;

enum {
    PATH_NONE = 0,
    PATH_OPEN = 1,   /* between BeginPath and EndPath, drawing calls add to the path. */
    PATH_CLOSED = 2, /* ready for FillPath or StrokePath. */
};

typedef struct DC_SAVED {
    struct DC_SAVED *Next;
    DC_STATE State;
    SURFACE *Surface;
} DC_SAVED;

#define DC_MEMORY 0x01
#define DC_WINDOW 0x02  /* obtained from a window. */
#define DC_CACHE 0x04   /* returned to the cache by ReleaseDC. */
#define DC_PAINT 0x08   /* BeginPaint dc. */
#define DC_INFO 0x10
#define DC_METAFILE 0x20 /* the win32k half of an enhanced metafile DC, without a surface. */

typedef struct DC {
    GDI_OBJECT Header;
    ULONG Flags;
    SURFACE *Surface;
    SURFACE *DefaultBitmap; /* the 1x1 bitmap of memory dcs. */
    struct WND *Window;
    POINT Origin;  /* surface coordinates of device point 0,0. */
    REGION VisRgn; /* surface coordinates. */
    DC_STATE State;
    DC_SAVED *Saved;
    int SaveLevel;
    UINT Dpi;
    UCHAR Awareness; /* the DPI awareness of the window or thread it belongs to: what HORZRES and mapping modes show. */
    struct FONT_INSTANCE *Realized;
    FONT *RealizedFor;
    RECT Bounds;      /* what was drawn since SetBoundsRect reset it, in device units, empty for none. */
    BOOLEAN BoundsOn; /* SetBoundsRect enabled the accumulation. */
    ULONG PathState;
    GDI_PATH Path;
    int PixelFormat; /* memory DCs; window DCs use the window's. */
    BOOLEAN OfMonitor; /* a DC of the device \\.\DISPLAYn, which is only that monitor. */
    ULONG Monitor;
    SIZE VirtualPixels; /* SetVirtualResolution, zero when unset. */
    SIZE VirtualMm;
    PUCHAR Attr;         /* the attributes gdi32 reads and writes in the program that owns it, 0 when it has none. */
    UCHAR AttrSeen[0x170]; /* what was there when the kernel last read or wrote them. */
    BOOLEAN AttrTouched;
    PALETTE *Palette; /* selected, null for the stock one. */
    ULONG AttrCharSet; /* what NtGdiGetCharSet answered, for the attributes at the next flush; 0 when none. */
} DC;

BOOL GdiPatBltDc(DC *dc, int x, int y, int width, int height, DWORD rop, BRUSH *brush);
BOOL GdiPolyPatBltDc(DC *dc, DWORD rop, const UCHAR *list, ULONG count);
int DcSelectClip(DC *dc, const REGION *region, int mode);
HFONT DcSelectFont(DC *dc, FONT *f);
BRUSH *GdiCreateBrush(UINT style, COLORREF color, ULONG_PTR hatch);
struct FONT *GdiCreateFont(const LOGFONTW *lf);
BOOL GdiBltFromSurface(HDC hdst, int x, int y, int width, int height, struct SURFACE *src, int src_x, int src_y, DWORD rop);
/* oembitmap.c: the check marks and radio buttons user32 draws, made again after the colours of the system change. */
void OemBitmapsInvalidate(void);
void OemDrawScrollArrow(SURFACE *s, const REGION *clip, const RECT *r, ULONG direction, ULONG state);
DC *DcLookup(HDC dc);
void DcSelectPalette(DC *dc, PALETTE *palette);
/* gdi/palette.c. */
PALETTE *PaletteLookup(HPALETTE handle);
PALETTE *PaletteCreateStock(void);
void PaletteFree(PALETTE *p);
COLORREF PaletteResolve(DC *dc, COLORREF color);
ULONG DcColorPixel(DC *dc, COLORREF color);
ULONG GdiDibColorTable(HDC hdc, WORD start, WORD count, PVOID user_entries, BOOLEAN set);
DC *GdiSelectingDc(const SURFACE *s);
DC *DcCreate(ULONG flags);
void DcFree(DC *dc);
void DcSetSurface(DC *dc, SURFACE *s);
void DcResetState(DC *dc);
void DcSelectDefaults(DC *dc);
/* final clip in surface coordinates for drawing; returns FALSE when nothing is visible. */
BOOLEAN DcGetClip(DC *dc, REGION *out);
void DcLogicalToDevice(DC *dc, LONG *x, LONG *y);
void DcLogicalToSurface(DC *dc, LONG *x, LONG *y);
LONG DcScaleToDevice(DC *dc, LONG v);
void DcTransformPoint(DC *dc, LONG *x, LONG *y); /* world transform only. */
/* world to device without the DC origin, and whether it scales, flips or rotates. */
void DcGetXform(DC *dc, XFORM *out);
BOOLEAN DcIsScaled(DC *dc);
void DcDeviceToLogical(DC *dc, LONG *x, LONG *y);
/* what a drawing touched, in surface units: marked drawn on the surface, and kept by SetBoundsRect when enabled. */
void DcMarkDrawn(DC *dc, const RECT *r, const REGION *clip);
void DcAccumulateBounds(DC *dc, const RECT *r, const REGION *clip);
LONG DcPenWidth(DC *dc); /* the selected pen's width in device pixels, at least 1. */
BOOLEAN DcIsPathOpen(DC *dc);
/* path recording, logical coordinates; used by the drawing services while a path is open. */
BOOLEAN PathMoveTo(DC *dc, LONG x, LONG y);
BOOLEAN PathLineTo(DC *dc, LONG x, LONG y);
BOOLEAN PathBezierTo(DC *dc, const POINT *pts, ULONG count);
BOOLEAN PathPolygon(DC *dc, const POINT *pts, ULONG count, BOOLEAN close);
BOOLEAN PathRectangle(DC *dc, LONG left, LONG top, LONG right, LONG bottom);
BOOLEAN PathEllipse(DC *dc, LONG left, LONG top, LONG right, LONG bottom);
void PathFree(GDI_PATH *p);
/* scan converts closed polygons in surface coordinates into a region. */
BOOLEAN RgnFromPolygons(REGION *out, const POINT *pts, const ULONG *counts, ULONG polygons, int fill_mode);
/* rectangles through a transform; rotations go through the polygon scan converter. */
BOOLEAN RgnTransform(REGION *out, const RECT *rects, ULONG count, const XFORM *m);
void DrawLine(DC *dc, const REGION *clip, LONG x0, LONG y0, LONG x1, LONG y1, RECT *dirty);
struct FONT_INSTANCE *DcGetFont(DC *dc);
/* what win32kfull's dwGetFontLanguageInfo answers for a realized font: FLI_GLYPHS, GCP_USEKERNING with kerning pairs. */
ULONG FontLanguageInfo(struct FONT_INSTANCE *fi);
UINT FontCharSetCodePage(UCHAR charset);

/* drawing primitives. */

/*
 * 32-bit surface colors are little-endian 0xAARRGGBB, stored as B, G, R, A. GDI writes alpha 0 in bitmaps;
 * device surfaces force alpha 255.
 */
static inline ULONG ColorToPixel(COLORREF c)
{
    return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

static inline COLORREF PixelToColor(ULONG p)
{
    return ((p >> 16) & 0xFF) | (p & 0xFF00) | ((p & 0xFF) << 16);
}

void DrawFillRect(SURFACE *s, const REGION *clip, const RECT *rect, ULONG pixel);
void DrawFillRectRop(SURFACE *s, const REGION *clip, const RECT *rect, ULONG pixel, int rop2);
void DrawBrushRect(DC *dc, const REGION *clip, const RECT *rect, BRUSH *brush, DWORD rop);
ULONG SurfaceGetPixel(SURFACE *s, LONG x, LONG y);
void SurfacePutPixel(SURFACE *s, LONG x, LONG y, ULONG pixel);
void DrawCoverage(SURFACE *s, const REGION *clip, LONG x, LONG y, const UCHAR *coverage, LONG width, LONG height,
                  LONG stride, ULONG pixel);
BOOLEAN MetricsFontSmoothing(UINT *type, UINT *contrast, UINT *orientation);
BOOLEAN BltSurface(SURFACE *dst, const REGION *clip, LONG dx, LONG dy, LONG width, LONG height, SURFACE *src,
                   LONG sx, LONG sy, DWORD rop, DC *dst_dc);
BOOLEAN StretchSurface(SURFACE *dst, const REGION *clip, const RECT *dst_rect, SURFACE *src, const RECT *src_rect,
                       DWORD rop, int mode);
BOOLEAN AlphaBlendSurface(SURFACE *dst, const REGION *clip, const RECT *dst_rect, SURFACE *src, const RECT *src_rect,
                          BLENDFUNCTION blend);

/* fonts. */

typedef struct GLYPH {
    USHORT Index;
    SHORT Advance;   /* pixels. */
    SHORT Left;      /* bitmap offset from the pen position. */
    SHORT Top;       /* bitmap offset from the baseline, negative is above. */
    USHORT Width;
    USHORT Height;
    UCHAR Ready;     /* rendered. */
    UCHAR *Coverage;
    /* ClearType levels from 0 to 6, three for each pixel from left to right, with a box of their own. */
    UCHAR *Subpixel;
    SHORT SubLeft;
    SHORT SubTop;
    USHORT SubWidth;
    USHORT SubHeight;
} GLYPH;

/* how a font is smoothed. */
enum {
    FONT_SMOOTH_ALIASED,
    FONT_SMOOTH_GRAY,
    FONT_SMOOTH_CLEARTYPE,
    FONT_SMOOTH_NATURAL, /* ClearType with the widths of the font itself. */
};

#define FONT_LINKS 24

typedef struct FONT_INSTANCE {
    LIST_ENTRY Links;
    struct FONT_FACE *Face;
    LONG EmPixels;   /* requested character height. */
    LONG Width;      /* lfWidth, 0 for the natural width. */
    LONG Weight;
    BOOLEAN Italic;
    BOOLEAN Underline;
    BOOLEAN StrikeOut;
    BOOLEAN SimBold;
    BOOLEAN SimItalic;
    UCHAR Smooth; /* FONT_SMOOTH_*. */
    float ScaleX;
    float ScaleY;
    TEXTMETRICW Metrics;
    LONG UnderlinePos;
    LONG UnderlineSize;
    LONG StrikeoutPos;
    ULONG RefCount;
    struct MimeTtfHinter *Hinter; /* runs the glyph programs at this size, null when the size is not hinted. */
    BOOLEAN HinterTried;
    struct MimeTtfHinter *HinterAliased; /* the same for text drawn without anti-aliasing. */
    BOOLEAN HinterAliasedTried;
    struct MimeTtfHinter *HinterCt; /* the same for ClearType. */
    BOOLEAN HinterCtTried;
    BOOLEAN CtVertical; /* ClearType also smooths down, five rows to a pixel. */
    /* by high byte, BMP only, 0 when unknown: glyph index + 1, and from bit 17 the linked font that has it. */
    ULONG *CharPages[256];
    GLYPH *Pages[256]; /* rendered glyphs by glyph index high byte. */
    struct FONT_INSTANCE *Linked[FONT_LINKS]; /* the fonts that draw the characters the face lacks, at its size. */
    UCHAR LinkedTried[FONT_LINKS];
    ULONG Generation; /* of the fonts when the links were looked for. */
    ULONG Id;         /* tells realizations apart in NtGdiGetRealizationInfo. */
    UCHAR CharSet;    /* the charset asked for when the face has it, else the one it falls back to. */
} FONT_INSTANCE;

void FontInitialize(void);
FONT_INSTANCE *FontRealize(const LOGFONTW *lf, UINT dpi);
const GLYPH *FontGetGlyph(FONT_INSTANCE *fi, ULONG codepoint);
const GLYPH *FontGetGlyphIndex(FONT_INSTANCE *fi, USHORT index);
USHORT FontGlyphIndex(FONT_INSTANCE *fi, ULONG codepoint); /* 0 for missing glyphs. */
/* the glyph of a character in the font or in the font linked to it that has it, and the font it is in. */
USHORT FontLinkedGlyph(FONT_INSTANCE *fi, ULONG codepoint, FONT_INSTANCE **owner);
struct MimeTtfHinter *FontHinter(FONT_INSTANCE *fi);
struct MimeTtfHinter *FontHinterAliased(FONT_INSTANCE *fi);
struct MimeTtfHinter *FontHinterClearType(FONT_INSTANCE *fi);
/* blends a glyph in a color, with ClearType when the glyph has it and the surface can take it. */
void DrawGlyph(SURFACE *s, const REGION *clip, LONG x, LONG y, const GLYPH *glyph, ULONG pixel);
LONG FontGlyphAdvance(FONT_INSTANCE *fi, USHORT index);
UCHAR *FontRenderTransformed(FONT_INSTANCE *fi, USHORT index, const XFORM *m, LONG *left, LONG *top, LONG *width,
                             LONG *height);
void FontReleaseProcess(HANDLE pid);
void FontGetFaceName(FONT_INSTANCE *fi, PCWSTR requested, PWSTR name, ULONG chars);
LONG FontTextWidth(FONT_INSTANCE *fi, PCWSTR text, ULONG count, LONG extra);
void TextDraw(DC *dc, LONG x, LONG y, UINT options, const RECT *rect, PCWSTR text, ULONG count, const INT *dx);
extern LOGFONTW W32SystemFont;  /* default GUI font for the system dpi. */
LOGFONTW W32MessageFontForDpi(UINT dpi);
LOGFONTW W32CaptionFontForDpi(UINT dpi);

/* user objects. */

enum {
    USER_TYPE_FREE = 0,
    USER_TYPE_WINDOW = 1,
    USER_TYPE_MENU = 2,
    USER_TYPE_CURSOR = 3,
    USER_TYPE_SETWINDOWPOS = 4,
    USER_TYPE_HOOK = 5,
    USER_TYPE_ACCELTABLE = 8,
    USER_TYPE_MONITOR = 12,
};

typedef struct USER_OBJECT {
    HANDLE Handle;
    UCHAR Type;
    BOOLEAN Destroyed;
    USHORT Pad;
    LONG RefCount;
    ULONG DeskOffset; /* where the desktop heap mirrors it, 0 while it does not, deskheap.c. */
    ULONG DeskScroll; /* the scroll bars of a window there. */
    LIST_ENTRY Live;  /* the objects of its type that have a handle. */
} USER_OBJECT;

HANDLE UserCreateHandle(USER_OBJECT *object, UCHAR type);
void UserFreeHandle(USER_OBJECT *object);
USER_OBJECT *UserLookup(HANDLE handle, UCHAR type);
void UserReference(USER_OBJECT *object);
void UserDereference(USER_OBJECT *object);
/* the next object of a type from an index on, which moves past it. */
USER_OBJECT *UserNextObject(ULONG *index, UCHAR type);
/* the next object of a type that has a handle, from null, in no order. */
USER_OBJECT *UserNextLive(USER_OBJECT *previous, UCHAR type);

/* threads, processes, queues. */

typedef struct W32PROCESS {
    PEPROCESS Process;
    HANDLE Pid;
    LIST_ENTRY Classes;
    LIST_ENTRY Links;
    ULONG DpiAwareness; /* DPI_AWARENESS_* as 0 to 3, unaware, system, per monitor and per monitor v2. */
    BOOLEAN DpiSet;     /* the manifest or a call chose the awareness, so it cannot change again. */
    BOOLEAN DpiGdiScaled; /* chosen as DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED, which works as unaware. */
    BOOLEAN Wow;        /* a 32-bit program, its structures are converted at its boundary. */
    DWORD LpkFlags;     /* NtUserRegisterLPK. */
    HINSTANCE ClientModule; /* user32 as the process loaded it, known from the first control class it registers. */
    DWORD StartupFlags; /* NtUserModifyUserStartupInfoFlags. */
    HICON ExeIcon;      /* the first icon of the program's file, for a window that has none. */
    BOOLEAN ExeIconTried;
    ULONG RotationPreference; /* ORIENTATION_PREFERENCE of SetDisplayAutoRotationPreferences, display.c. */
    struct W32GPU *Gpu; /* gpu.c. */
    struct W32VK *Vk;   /* vulkan.c. */
    struct W32AUDIO *Audio; /* audio.c. */
    struct W32RAW *Raw;     /* rawinput.c. */
    ULONG ImageChars;
    WCHAR ImagePath[512];
    WCHAR DesktopInfo[128];      /* the winsta\desktop it was started on, winsta.c. */
    struct W32WINSTA *WinSta;    /* referenced. */
    HANDLE WinStaHandle;
    ULONG_PTR DndHandler;        /* the process's code for drops, dnd.c. */
    BOOLEAN InputIdle;           /* WaitForInputIdle succeeds immediately after the first input wait. */
    PVOID SharedBase, HandleBase; /* its views of the server information and of the handle table, usershared.c. */
    PVOID GdiBase;                /* its view of the table of gdi, gdi/shared.c. */
    PUCHAR DcAttrChunk;           /* the memory of the program its device contexts keep their attributes in, gdi/dc.c. */
    ULONG DcAttrUsed;
    PUCHAR DcAttrFree[32];
    ULONG DcAttrFreeCount;
    PUCHAR SmallAttrFree[64];     /* freed blocks of 0x20 bytes for brushes and regions, gdi/shared.c. */
    ULONG SmallAttrFreeCount;
    PVOID DeskBase;               /* its view of the heap of the desktop, deskheap.c. */
    struct CURICON *Cursors;      /* its shared cursors and icons, which NtUserFindExistingCursorIcon looks through. */
} W32PROCESS;

typedef struct QUEUED_MESSAGE {
    LIST_ENTRY Links;
    MSG Msg;
    ULONG_PTR ExtraInfo;
    ULONG Kind; /* QM_* */
    ULONG Origin; /* IMO_* of an input message: the hardware, SendInput or the system. */
    union {
        MimeInputEvent Input;
        WINDOWPLACEMENT Placement;
    };
} QUEUED_MESSAGE;

enum {
    QM_POSTED = 0,
    QM_INPUT = 1, /* raw host event processed by the receiving thread. */
    QM_WINDOW_SHOW = 2,
    QM_WINDOW_PLACEMENT = 3,
};

typedef struct SENT_MESSAGE {
    LIST_ENTRY Links;
    struct W32THREAD *Sender;
    HWND Hwnd;
    UINT Msg;
    WPARAM Wparam;
    LPARAM Lparam;        /* kernel pointer for MIME_LPARAM_IN and MIME_LPARAM_INOUT. */
    ULONG LparamKind;
    ULONG LparamSize;
    LRESULT Result;
    BOOLEAN Completed;
    BOOLEAN Notify;       /* SendNotifyMessage: nobody waits, the receiver frees it. */
    BOOLEAN SenderGone;
    BOOLEAN Ansi;
    BOOLEAN Owned;        /* Lparam is pool memory that belongs to the message once its sender stops waiting. */
    BOOLEAN Aborted;      /* the window or its thread went before the procedure ran or finished. */
    BOOLEAN Callback;     /* SendMessageCallback: the sender gets the result later. */
    BOOLEAN ResultCall;   /* not a message: the result of a callback, to call in the sender's thread. */
    struct SENT_MESSAGE *Outer; /* the message the receiving thread was processing when this one came in. */
    HANDLE SenderTid;
    SENDASYNCPROC AsyncProc;
    ULONG_PTR AsyncData;
    HHOOK Hook;           /* a hook call instead of a message, Msg holds the hook code. */
    KEVENT Done;
} SENT_MESSAGE;

typedef struct W32THREAD {
    PETHREAD Thread;
    HANDLE Tid;
    W32PROCESS *Process;
    LIST_ENTRY Links;
    KEVENT Event;           /* signalled when the queue gets work. */
    LIST_ENTRY Posted;
    LIST_ENTRY Input;
    LIST_ENTRY Sent;        /* messages sent to this thread by others. */
    ULONG DeskInfo;         /* its CLIENTTHREADINFO in the desktop heap, see deskheap.c. */
    ULONG NoPaintSerial;    /* the state of DeskCleanSerial in which no window of the thread needed painting. */
    ULONG TimerSerial;      /* the state of DeskCleanSerial in which TimerDue was found. */
    LONG64 TimerDue;
    BOOLEAN ClientSetup;    /* ClientThreadSetup of user32 was asked for, see service_enter of main.c. */
    SENT_MESSAGE *Receiving; /* sent message being processed. */
    BOOLEAN Replied;         /* the message being processed was answered before its procedure returned. */
    ULONG ReceivingKind;     /* 1 for a sent message being processed, 2 when its sender does not wait, else 0. */
    ULONG WakeBits;
    ULONG ChangeBits;
    BOOLEAN QuitPosted;
    int QuitCode;
    DWORD MessageTime;
    POINT MessagePos;
    ULONG_PTR ExtraInfo;
    /* IMDT_* and IMO_* of the input message the thread retrieved last, for GetCurrentInputMessageSource. */
    UCHAR InputDevice, InputOrigin;
    HWND Focus;
    HWND Active;
    BOOLEAN AppActive;       /* WM_ACTIVATEAPP told the thread's windows that the thread is the active one. */
    HWND Capture;
    ULONG CaptureNc;  /* nc hit code of a capture held by the frame. */
    UCHAR KeyState[256]; /* bit 7 down, bit 0 toggled, as seen by this thread. */
    BOOLEAN SendAnsi; /* the NtUserMessageCall under way gave ansi strings. */
    DWORD StateFlags; /* NtUserSetThreadState. */
    struct {
        HWND Hwnd;
        HBITMAP Bitmap;
        int Width, Height, X, Y;
        int Hide;    /* HideCaret less ShowCaret, it shows at 0. */
        BOOLEAN On;  /* drawn now. */
    } Caret;         /* control.c. */
    /* what GetKeyState last answered this thread, key by key, good while InputKeyGeneration stands. */
    LONG KeyCacheGeneration;
    ULONG KeyCacheValid[8];
    UCHAR KeyCache[256];
    struct W32THREAD *InputShare; /* the thread whose key state and focus AttachThreadInput shares, or null. */
    HWND MouseWindow;    /* window under the mouse for leave tracking. */
    TRACKMOUSEEVENT Track;
    ULONG CallbackDepth;
    ULONG LastRead;          /* tick of the last time the thread looked at its queue. */
    BOOLEAN InWait;          /* sleeping in a message wait or in a send, serving its queue. */
    SENT_MESSAGE *Sending;   /* the message it waits to have answered, the innermost when sends nest. */
    struct HOOK *CurrentHook; /* the hook procedure running, for CallNextHookEx. */
    BOOLEAN Destroying;
    HCURSOR Cursor;
    struct CURICON *CursorLock; /* the object of Cursor, which stays alive while the thread shows it. */
    LONG CursorShowCount;
    struct W32DESKTOP *Desktop;  /* referenced, winsta.c. */
    HANDLE DesktopHandle;
    HWND TsfSink;          /* text services window for host input methods, ime.c. */
    BOOLEAN AltAlone;      /* Alt or F10 went down and no other key or button has since, for the menu bar. */
    /* the last key presses the thread took, with the characters the host typed for them, which TranslateMessage posts. */
    struct KEY_CHARS {
        ULONG Scan;
        ULONG Time;
        BOOLEAN Used, Translated;
        UCHAR Count;
        struct {
            USHORT Message;
            WCHAR Char;
        } Chars[4];
    } KeyChars[8];
    ULONG KeyCharsNext;
    ULONG DpiOverride;     /* the awareness set for the thread plus one, 0 to follow the process. */
    ULONG DpiHosting;      /* the DPI hosting behavior of the thread, 0 default and 1 mixed. */
} W32THREAD;

/* caller job restrictions, JOB_OBJECT_UILIMIT_*. */
#define W32_UILIMIT_HANDLES 0x01
#define W32_UILIMIT_READCLIPBOARD 0x02
#define W32_UILIMIT_WRITECLIPBOARD 0x04
#define W32_UILIMIT_SYSTEMPARAMETERS 0x08
#define W32_UILIMIT_DISPLAYSETTINGS 0x10
#define W32_UILIMIT_GLOBALATOMS 0x20
#define W32_UILIMIT_DESKTOP 0x40
#define W32_UILIMIT_EXITWINDOWS 0x80

static inline BOOLEAN W32UiLimited(ULONG limit)
{
    return (MimeProcessUiLimits(PsGetCurrentProcess()) & limit) != 0;
}

W32THREAD *W32GetThread(void);  /* current thread, created on demand. */
W32PROCESS *W32GetProcess(void);
void UserSharedInitialize(void);
PUCHAR UserSharedHandles(void); /* the table user32 reads, and how many entries of it have pages. */
ULONG UserSharedHandleCount(void);
void UserSharedMonitors(void);
/* deskheap.c: the heap of the desktop user32 reads. */
struct WND_CLASS;
void DeskSync(void);
void DeskTouch(void);
ULONG DeskCleanSerial(void);
void DeskObjectFreed(USER_OBJECT *object);
NTSTATUS DeskAttachThread(struct W32PROCESS *p);
void DeskProcessExited(struct W32PROCESS *p);
ULONG64 DeskUserWindow(struct WND *w);
void PaintSync(struct WND *w);
/* control.c. */
BOOLEAN CaretTimer(struct W32THREAD *t, HWND hwnd, UINT_PTR id);
void CaretDestroyThread(struct W32THREAD *t);
void DeskClassFreed(struct WND_CLASS *c);

/* client.c: the calls into the genuine user32 of the program. */
void ClientThreadSetup(void);
ULONG64 ClientPfn(ULONG index);
PVOID ClientAllocExtraBytes(ULONG size);
void ClientFreeExtraBytes(struct WND *w, PVOID bytes);
BOOL ClientMonitorEnumProc(HMONITOR monitor, HDC dc, const RECT *rect, LPARAM lparam, ULONG64 proc);
void WndExtraChanged(struct WND *w, ULONG offset, ULONG size);
LRESULT ClientCallWindowProc(struct WND *w, WNDPROC proc, BOOLEAN ansi, UINT msg, WPARAM wparam, LPARAM lparam,
                             ULONG kind, ULONG size);
NTSTATUS GdiSharedMap(W32PROCESS *p);
/* release process or thread window station and desktop references, winsta.c. */
void WinStaFreeProcess(W32PROCESS *p);
void WinStaFreeThread(W32THREAD *t);
BOOL WndThreadHasWindows(W32THREAD *t);
/* shutdown.c. */
ULONG NTAPI W32ShutdownProcess(PEPROCESS process, const MX_SHUTDOWN_CONTEXT *context);
ULONG W32ProcessThreads(W32PROCESS *p, HANDLE *tids, ULONG max);
BOOLEAN MsgThreadHung(W32THREAD *t);
enum {
    MSGSYS_OK = 1,
    MSGSYS_GONE,    /* the window or its thread ended. */
    MSGSYS_TIMEOUT,
    MSGSYS_EVENT0,  /* the first event of the caller was set, the second MSGSYS_EVENT0 + 1. */
};
ULONG MsgSendSystem(HANDLE tid, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, ULONG64 timeout_ms,
                    PKEVENT *events, ULONG event_count, LRESULT *result);
extern LIST_ENTRY W32Processes;
W32THREAD *W32LookupThread(HANDLE tid);
BOOL W32AttachInput(W32THREAD *a, W32THREAD *b, BOOL attach);
HWND W32SharedWindow(W32THREAD *t, SIZE_T offset);

/* the thread that holds the input state the given one reads: the one it is attached to, else itself. */
static inline W32THREAD *W32InputOwner(W32THREAD *t)
{
    return t->InputShare ? t->InputShare : t;
}
void QueueWake(W32THREAD *t, ULONG bits);
BOOLEAN QueuePost(W32THREAD *t, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
/* the clock's offset changed: every top level window is told with WM_TIMECHANGE. */
void NTAPI W32TimeChange(void);
BOOLEAN QueueWindowOperation(W32THREAD *t, HWND hwnd, UINT command, const WINDOWPLACEMENT *placement);
void QueuePostInput(W32THREAD *t, const MimeInputEvent *ev, WPARAM mouse_state, ULONG origin, ULONG_PTR extra);
DWORD W32TickCount(void);
/* tick count of the last host or injected input event. */
extern DWORD InputLastTime;
void GdiWindowDestroyed(struct WND *w);
ULONG GdiCountProcessObjects(HANDLE pid);
ULONG WndCountProcessObjects(W32PROCESS *p);

/* classes and windows. */

/* the numbers of the procedures of the classes of the system, as windows numbers them from 0x29a. */
#define FNID_FIRST 0x029A
#define FNID_SCROLLBAR 0x029A
#define FNID_ICONTITLE 0x029B
#define FNID_MENU 0x029C
#define FNID_DESKTOP 0x029D
#define FNID_DEFWINDOWPROC 0x029E
#define FNID_MESSAGEWND 0x029F
#define FNID_SWITCH 0x02A0
#define FNID_BUTTON 0x02A1
#define FNID_COMBOBOX 0x02A2
#define FNID_COMBOLISTBOX 0x02A3
#define FNID_DIALOG 0x02A4
#define FNID_EDIT 0x02A5
#define FNID_LISTBOX 0x02A6
#define FNID_MDICLIENT 0x02A7
#define FNID_STATIC 0x02A8
#define FNID_IME 0x02A9
#define FNID_GHOST 0x02AA
#define FNID_LAST_CLIENT 0x02AA
/* the kinds of NtUserMessageCall, by the number of the procedure that answers them. */
#define FNID_SENDMESSAGE 0x02B1
#define FNID_SENDMESSAGEFF 0x02B2
#define FNID_SENDMESSAGEEX 0x02B3
#define FNID_CALLWINDOWPROC 0x02B4
#define FNID_SENDMESSAGEBSM 0x02B5
#define FNID_SENDNOTIFYMESSAGE 0x02B7
#define FNID_SENDMESSAGECALLBACK 0x02B8

typedef struct WND_CLASS {
    LIST_ENTRY Links;
    ATOM Atom;        /* of the name of its version, which windows find it by. */
    PWSTR Name;
    ATOM PlainAtom;   /* of the name the program gave, which GetClassName answers. */
    PWSTR PlainName;  /* NULL when it is Name. */
    UINT Style;
    WNDPROC Proc;
    BOOLEAN Ansi;
    BOOLEAN System;
    int ClassExtra;
    int WindowExtra;
    HINSTANCE Instance;
    HICON Icon;
    HICON IconSmall;
    HCURSOR Cursor;
    HBRUSH Background;
    PWSTR MenuName;
    ULONG Fnid;          /* FNID_*, the number windows gives the procedures of user32 and win32k. */
    USHORT CsfFlags;     /* the flags of NtUserRegisterClassExWOW. */
    W32PROCESS *Process; /* NULL for global and system classes. */
    LONG Windows;
    PUCHAR Extra;
    ULONG DeskOffset, DeskSize;     /* its block in the heap of the desktop, deskheap.c. */
    ULONG DeskName, DeskNameSize;   /* its ansi name there. */
    ULONG DeskWritten;              /* the sync that last wrote it. */
} WND_CLASS;

#define WNDF_DESTROYING 0x0001
#define WNDF_NEEDS_ERASE 0x0002
#define WNDF_INTERNAL_PAINT 0x0004
#define WNDF_FRAME_DIRTY 0x0008    /* the frame must be redrawn by win32k. */
#define WNDF_SENT_CREATE 0x0010
#define WNDF_ACTIVE_FRAME 0x0020   /* caption drawn active. */
#define WNDF_DIALOG 0x0040
#define WNDF_HOST_WINDOW 0x0080    /* has a host window. */
#define WNDF_RESTORETOMAX 0x0100
#define WNDF_PAINT_PENDING 0x0200  /* WM_PAINT will be generated. */
#define WNDF_MOUSE_TRACKED 0x0400
#define WNDF_ERASE_PENDING 0x0800  /* BeginPaint must send WM_ERASEBKGND. */
#define WNDF_NCPAINT_SENT 0x1000
#define WNDF_HOST_MOVING 0x2000    /* the host is moving the window, do not echo the position back. */
#define WNDF_PRESENTED 0x4000      /* the host has received contents, the window may be shown. */
#define WNDF_ACTIVATE_PENDING 0x8000
#define WNDF_NOCLOSE 0x10000       /* SC_CLOSE removed from the system menu. */
#define WNDF_FULLSCREEN 0x20000
#define WNDF_DROPTARGET 0x80000    /* takes drops through a handler of its process, dnd.c. */
#define WNDF_APP_FULLSCREEN 0x40000 /* the program made it cover the screen before the host made it fullscreen. */
#define WNDF_DPI_CHILDREN 0x100000 /* the dpi change reaches the children, per monitor v2 windows have it from the start. */
#define WNDF_NCDPI 0x200000        /* the frame is drawn at the dpi of the window, not the one of the system. */
#define WNDF_DPI_DEFERRED 0x400000 /* the program asked for the changes of its dpi to wait, DeferWindowDpiChanges. */
#define WNDF_DPI_PENDING 0x800000  /* the dpi changed and the program has not been told. */
#define WNDF_HOST_PLACED 0x1000000 /* the host tells where the window is, a wayland compositor does not. */
#define WNDF_PRESENTS 0x2000000    /* a swap chain or a GL context asked where the window lies, so it has a layer. */
#define WNDF_SENDSIZEMOVE 0x4000000 /* WM_SIZE and WM_MOVE wait for the first showing, WNDS_SENDSIZEMOVEMSGS. */
#define WNDF_HASNAME 0x8000000     /* made with a name, WNDS_HASCREATESTRUCTNAME. */
#define WNDF_DLGPOINTER 0x10000000 /* user32 gave its dialog structure, WNDS_DIALOGWINDOW. */
#define WNDF_MSGBOX 0x20000000     /* NtUserSetMsgBox, WNDS_MSGBOX. */
#define WNDF_HIDDENPOPUP 0x40000000 /* hidden with its owner and shown again with it, WNDS_HIDDENPOPUP. */
#define WNDF_SURFACE_GONE 0x80000000u /* hidden: windows freed its redirection surface, the next show starts over. */

/* caption buttons for hot and pressed tracking. */
enum {
    CAPTION_NONE = 0,
    CAPTION_CLOSE = 1,
    CAPTION_MAX = 2,
    CAPTION_MIN = 3,
    CAPTION_HELP = 4,
};

typedef struct WND_PROP {
    ATOM Atom;
    HANDLE Data;
} WND_PROP;

/* a scroll bar of the frame, SBDATA of windows. */
typedef struct SCROLL_DATA {
    LONG Min, Max;
    UINT Page;
    LONG Pos;
} SCROLL_DATA;

/* SBINFO of windows: the arrows ESB_* disables, the horizontal bar then the vertical one. */
typedef struct SCROLL_INFO {
    ULONG Disable;
    SCROLL_DATA Bar[2];
} SCROLL_INFO;

typedef struct WND {
    USER_OBJECT Header;
    struct WND *Parent;
    struct WND *Owner;
    struct WND *HostOwner; /* the top level window a child of the desktop was taken from, which its host window hangs from. */
    struct WND *FirstChild; /* top of the z order. */
    struct WND *LastChild;
    struct WND *Next;       /* below this window. */
    struct WND *Prev;       /* above this window. */
    W32THREAD *Thread;
    WND_CLASS *Class;
    DWORD Style;
    DWORD ExStyle;
    ULONG Flags;
    DWORD CreateStyle; /* the style CreateWindowEx was asked for. */
    BOOLEAN ShownInactive; /* shown without activation: the focus the host gives it next is not the program's. */
    USHORT Fnid;       /* FNID_* of the procedure user32 runs it with, from its class or NtUserSetWindowFNID. */
    RECT WindowRect;  /* screen coordinates of the window's DPI awareness. */
    RECT ClientRect;  /* screen coordinates of the window's DPI awareness. */
    RECT NormalRect;
    POINT MinPosition;
    POINT MaxPosition;
    RECT FullscreenRect;
    RECT FullscreenNormalRect;
    DWORD FullscreenStyle;
    DWORD FullscreenExStyle;
    ULONG FullscreenRestoreFlags;
    WNDPROC Proc;
    BOOLEAN Ansi;
    HINSTANCE Instance;
    UINT_PTR Id;      /* control id or menu. */
    LONG_PTR UserData;
    PUCHAR Extra;
    ULONG ExtraSize;
    PUCHAR UserExtra; /* the copy of the extra bytes user32 reads, in the memory of the process of the window. */
    SCROLL_INFO *Scroll; /* the scroll bars of the frame, control.c, NULL until one is set. */
    PWSTR Text;
    ULONG TextLength;
    REGION Update;    /* client relative update region, screen coordinates. */
    HICON Icon;
    HICON IconSmall;
    UINT Dpi;
    UCHAR Awareness;  /* the DPI awareness it was created with, its rectangles are in that awareness' coordinates. */
    UINT MonitorDpi;  /* the dpi of the monitor the window was last on, which is the one it follows when it moves. */
    HWND InheritMonitor; /* the window whose monitor this one follows instead of its own, NtUserInheritWindowMonitor. */
    USHORT DeferredDpi; /* the dpi the program was last told of while its changes are deferred, else 0. */
    UCHAR Hosting;    /* the DPI hosting behavior of its thread when it was created, 1 is mixed. */
    UCHAR ControlDpi; /* DCDC_*, what a dialog does to this control when the dpi changes. */
    POINT MinTrack, MaxTrack; /* from the last WM_GETMINMAXINFO, 0 until there was one. */
    HICON HostIcon;
    ULONG HostStateRequest;
    SURFACE *Surface; /* top level windows. */
    UCHAR CaptionHot;
    UCHAR CaptionPressed;
    UCHAR CaptionTracking; /* button pressed at mouse-down. */
    DWORD ContextHelpId;
    HMENU SystemMenu;
    struct MENUBAR *MenuBar; /* layout of the menu bar, menubar.c. */
    LONG MenuBarHeight;      /* pixels the last WM_NCCALCSIZE reserved for the bar and its line, 0 without a bar. */
    LONG MenuHot;            /* bar item selected by the menu loop, plus one. */
    LONG MenuPushed;         /* bar item whose popup is open, plus one. */
    UCHAR UiState;           /* UISF_HIDEFOCUS and UISF_HIDEACCEL, set while the keyboard cues are hidden. */
    BOOLEAN MenuCues;        /* Alt is down, so the bar shows its mnemonics whatever the keyboard cues say. */
    BOOLEAN MenuIcon;        /* the menu loop has the system menu icon selected. */
    REGION *Shape;     /* SetWindowRgn, window relative, NULL for none. */
    ULONG LayerFlags;  /* LWA_* for SetLayeredWindowAttributes, MIME_LAYER_UPDATE for UpdateLayeredWindow. */
    COLORREF LayerKey;
    BYTE LayerAlpha;
    SURFACE *Present;  /* composed copy shown to the host when the surface needs alpha or a shape. */
    RECT LayerShown;   /* where the host was told the layer of a window that presents is, see hold_area. */
    ULONG TouchFlags;
    ULONG DisplayAffinity; /* WDA_*, how captures show the window. */
    USHORT *MessageFilter;  /* ChangeWindowMessageFilterEx, the messages let in from below, sorted. */
    ULONG MessageFilterCount, MessageFilterCapacity;
    PEPROCESS *BoostProcesses; /* SetAdditionalForegroundBoostProcesses, referenced. */
    ULONG BoostCount;
    UCHAR PointerNotify;    /* RegisterPointerDeviceNotifications: 1 registered, 2 for range messages too. */
    int PixelFormat;
    WND_PROP *Props;
    ULONG PropCount;
    ULONG PropCapacity;
    PWSTR ShutdownReason; /* ShutdownBlockReasonCreate. */
} WND;

#define MIME_LAYER_UPDATE 0x80000000u /* contents come from UpdateLayeredWindow, premultiplied. */

WND *WndLookup(HWND hwnd);
WND *WndDesktop(void);
LRESULT ClientShutdown(WND *w, ULONG flags); /* shutdown.c. */
static inline HWND WndHandle(WND *w)
{
    return w ? (HWND)w->Header.Handle : nullptr;
}
BOOLEAN WndIsTopLevel(WND *w); /* direct child of the desktop. */
void WndLaunchPolicyDone(PEPROCESS process, BOOLEAN exited); /* SetProcessLaunchForegroundPolicy. */
/* false for a top level window on a host desktop that has no place for a minimized one: it has no minimize button
 * then and stays as it is when it is asked to minimize, which windows itself never does. */
BOOLEAN WndCanMinimize(WND *w);
void WndPhysicalRect(const WND *w, RECT *out);
UINT WndMonitorDpi(const WND *w);
LONG WndToPhysical(const WND *w, LONG value);
BOOLEAN WndCoversMonitor(WND *w);
WND *WndTopLevel(WND *w);
BOOLEAN WndIsVisible(WND *w);  /* the window and all its ancestors are visible. */
BOOLEAN WndIsChildOf(WND *child, WND *parent);
void WndDestroy(WND *w);
void WndDestroyThreadWindows(W32THREAD *t);
WND *WndFromPoint(WND *top, POINT pt);
WND *WndChildFromPoint(WND *parent, POINT pt, BOOLEAN skip_disabled);
void WndSetPos(WND *w, WND *after, LONG x, LONG y, LONG cx, LONG cy, UINT flags);
/* the deferred positions and the window update lock a thread leaves behind. */
void WndPosThreadGone(W32THREAD *t);
void WndDisplayChanged(void);
void WndShow(WND *w, int cmd);
void WndShowOwned(WND *owner, UINT cmd);
void WndGetMinMaxInfo(WND *w, MINMAXINFO *info);
void WndFullscreen(WND *w, BOOLEAN enter, const RECT *rect);
BOOL WndSetPlacement(WND *w, const WINDOWPLACEMENT *placement);
void WndInvalidate(WND *w, const RECT *client_rect, BOOLEAN erase, BOOLEAN children);
void WndInvalidateRegion(WND *w, const REGION *screen_rgn, ULONG flags);
void WndValidate(WND *w, const RECT *client_rect);
void WndUpdateHostConfig(WND *w, BOOLEAN activate);
void WndEnsureSurface(WND *w); /* top level windows: creates or resizes the surface and the host window. */
void WndEnsureSurfaceShown(WND *w); /* the same, starting over after a hide. */
BOOLEAN WndLayerRects(WND *w, const POINT *pos, const SIZE *size);
void WndComputeVisRgn(WND *w, BOOLEAN client, ULONG dcx_flags, REGION *out);
WND *WndFindNeedsPaint(W32THREAD *t, HWND filter);
void WndActivate(WND *w, BOOLEAN click);
void WndSendActivateApp(W32THREAD *t, BOOLEAN active, HANDLE other);
extern BOOLEAN WndActivatedByHost;
extern struct WND *WndHostFocus, *WndHostPending;
extern struct WND *WndShell;
extern struct WND *WndGuestActivated;
extern ULONG WndGuestActivatedTick;
void WndSetFocus(WND *w);
void WndCenterOnOwner(WND *w);
UINT WndGetDpi(WND *w);

/*
 * dpi.c: DPI awareness and the coordinates it gives. a window keeps its rectangles in the pixels of its awareness: an
 * unaware window lives in a 96 dpi screen the host scales up by the system dpi, any other one in physical pixels.
 */
enum {
    DPI_AWARE_UNAWARE,
    DPI_AWARE_SYSTEM,
    DPI_AWARE_PER_MONITOR,
    DPI_AWARE_PER_MONITOR_V2,
    DPI_AWARE_GDISCALED, /* unaware, only a thread can have it. */
};
/* the coordinate space of the monitors: the one a per monitor program works in. */
#define DPI_PHYSICAL DPI_AWARE_PER_MONITOR
ULONG DpiThreadAwareness(const W32THREAD *t);
ULONG DpiCurrentAwareness(void);
UINT DpiScaleOf(ULONG awareness);
BOOLEAN DpiVirtual(ULONG awareness);
LONG DpiScale(LONG value, UINT from, UINT to);
void DpiRectConvert(RECT *r, ULONG from, ULONG to);
void DpiPointConvert(POINT *p, ULONG from, ULONG to);
void DpiPixelConvert(POINT *p, ULONG from, ULONG to);
LONG DpiLength(LONG value, ULONG from, ULONG to, UINT monitor_dpi);
RECT DpiVirtualScreen(ULONG awareness);
ULONG DpiVirtualMonitors(ULONG awareness, RECT *out);
void DpiConvertRow(ULONG from, ULONG to, LONG y, LONG x, ULONG count, POINT *out);
RECT DpiWorkArea(ULONG awareness);
void DpiProcessAwarenessChanged(W32PROCESS *p, ULONG old);
UINT DpiProcessSetAwareness(W32PROCESS *p, ULONG awareness, BOOLEAN force);
ULONG DpiThreadSetContext(W32THREAD *t, ULONG context);
BOOLEAN DpiChildIsolated(const WND *parent, ULONG awareness);
BOOLEAN WndBroadcastsDpi(const WND *w);
BOOLEAN DpiChildMessageEnabled(WND *w);
UINT DpiWindowDpi(WND *w);
void WndNotifyDpiChanged(WND *w, UINT old, const RECT *suggested);
BOOLEAN WndForceDpi(WND *w, UINT dpi);
BOOLEAN WndDeferDpiChanges(WND *w);
BOOLEAN WndRestoreDpiChanges(WND *w);
UINT WndFrameDpi(WND *w);
void WndSetText(WND *w, PCWSTR text, ULONG chars);

/* frame. */

typedef struct FRAME_METRICS {
    LONG Border;      /* left, right and bottom non client width, visible border included. */
    LONG Top;         /* top non client height, caption included. */
    LONG Caption;     /* caption height. */
    LONG VisibleInset;/* invisible resize border on left, right and bottom. */
    LONG ButtonWidth;
    LONG CornerRadius;
} FRAME_METRICS;

BOOLEAN FrameHasCaption(DWORD style, DWORD ex_style);
void FrameGetMetrics(DWORD style, DWORD ex_style, UINT dpi, FRAME_METRICS *m);
void FrameAdjustRect(RECT *r, DWORD style, DWORD ex_style, UINT dpi);
void FrameClientFromWindow(RECT *r, DWORD style, DWORD ex_style, UINT dpi);
LRESULT FrameHitTest(WND *w, POINT pt);
void FramePaint(WND *w);
BOOLEAN FrameIsDark(WND *w);
BOOLEAN FrameThemed(void);
BOOLEAN FrameDrawScrollPart(SURFACE *s, const REGION *clip, const RECT *r, int part, int state, UINT dpi);

/* scrollbar.c: the scroll bars of a frame, drawn and tracked as DefWindowProc does. */
void ScrollPaint(WND *w, SURFACE *s, const REGION *clip);
void ScrollRedraw(WND *w);
void ScrollMouse(WND *w, UINT msg, LONG hit, POINT pt);
void ScrollTrack(WND *w, int bar, POINT pt);
BOOLEAN ScrollTrackPos(const WND *w, int bar, LONG *pos);
void ScrollWindowDestroyed(WND *w);
void ScrollBarInfo(const WND *w, int bar, SCROLLBARINFO *info);
/* high contrast is on: the system colors are the high contrast ones and the visual style is off. */
BOOLEAN MetricsHighContrast(void);
/* the system colors or the visual style changed: every window is told and drawn again. */
/* the colours of the system changed, and with them the theme when it says so. */
void WndSystemChanged(BOOLEAN theme);
/* a color of the Window or Menu class of the visual style, light or dark. */
BOOLEAN FrameThemeColor(BOOLEAN menu, BOOLEAN dark, int part, int state, int prop, COLORREF *out);
void FrameDrawText(SURFACE *s, const REGION *clip, FONT_INSTANCE *fi, LONG x, LONG baseline, PCWSTR text, ULONG count,
                   LONG max_right, ULONG pixel);
void FrameBleed(WND *w);
void FrameGetButtonRect(WND *w, int button, RECT *r); /* window relative. */
BOOLEAN FrameClientOnly(const WND *w);
void FrameGetVisibleRect(WND *w, RECT *r);              /* window relative. */
ULONG FrameGetDragRects(WND *w, RECT *rects, ULONG max); /* window relative caption areas. */
LRESULT FrameMouse(WND *w, UINT msg, LONG hit, POINT pt);
void FrameGetTitleBarInfo(WND *w, TITLEBARINFOEX *info); /* screen coordinates of the window's own pixels. */
BOOLEAN FrameExtension(WND *w, REGION *out); /* window relative client part the frame was extended into. */
BOOLEAN FrameDwmButtons(WND *w);             /* the caption buttons are drawn over the client area. */
void FrameCompose(WND *w, SURFACE *dst, const RECT *area);
void FrameSurfaceAlpha(WND *w);

/* messages and callbacks. */

/* message parameter copying between processes, thunk.c. */
typedef enum MSG_CLASS {
    MC_VALUE,         /* both parameters are plain values. */
    MC_KERNEL,        /* only the window manager sends it. */
    MC_CREATE,        /* WM_CREATE and WM_NCCREATE. */
    MC_STRING_IN,     /* lparam is a string, or NULL. */
    MC_ITEM_LB,       /* the string of a list box command, or an item's data. */
    MC_ITEM_CB,       /* the same for a combo box. */
    MC_STRING_OUT,    /* wparam characters of room at lparam. */
    MC_LINE_OUT,      /* EM_GETLINE, the room is in the first word of the buffer. */
    MC_ITEM_OUT,      /* LB_GETTEXT, the room is as long as the text. */
    MC_FORMAT_OUT,    /* WM_ASKCBFORMATNAME */
    MC_STRUCT_IN,
    MC_STRUCT_INOUT,
    MC_STRUCT_OUT,
    MC_TITLEBAR,      /* a structure that must say how large it is. */
    MC_HELP,
    MC_OPTMSG,        /* an optional MSG. */
    MC_COPYDATA,
    MC_NCCALCSIZE,
    MC_MDICREATE,
    MC_PTR2,          /* wparam and lparam both optionally point at a DWORD to fill. */
    MC_WPTR,          /* wparam points at a DWORD to fill. */
    MC_UINTS_IN,
    MC_UINTS_OUT,
    MC_DEVCHANGE,
    MC_POWER,
    MC_WINHELP,
    MC_COPYGLOBAL,
} MSG_CLASS;

typedef struct MSG_RULE {
    UCHAR Class;
    UCHAR Layout;  /* pointer or handle fields require a different 32-bit layout. */
    USHORT Size;   /* bytes of a structure of fixed size. */
} MSG_RULE;

typedef struct MSG_CAPTURE {
    PVOID Buffer;      /* the copy, pool memory, or NULL when the parameters pass as they are. */
    ULONG Size;
    ULONG Kind;        /* how the callback gets it, MIME_LPARAM_*. */
    UINT Msg;
    UCHAR Class;
    BOOLEAN ItemData;
    BOOLEAN Ansi;      /* the sender gave ansi strings, NtUserMessageCall with bAnsi. */
    WPARAM Wparam;     /* what the procedure is given. */
    LPARAM Lparam;
    WPARAM UserWparam; /* what the sender passed. */
    LPARAM UserLparam;
    LPARAM UserPos;
    ULONG Chars;       /* characters the copy has room for. */
    ULONG Capacity;    /* characters the sender's buffer has room for. */
    LRESULT Refused;   /* the result of a send that was refused. */
} MSG_CAPTURE;

MSG_RULE MsgRuleFor(UINT msg);
BOOLEAN MsgCapture(MSG_CAPTURE *c, WND *target, UINT msg, WPARAM wparam, LPARAM lparam, MSG_RULE rule, LONG hint);
void MsgCaptureFree(MSG_CAPTURE *c);
void MsgCaptureFinish(MSG_CAPTURE *c, LRESULT *result, BOOLEAN completed);

LRESULT MsgSend(WND *w, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT MsgSendEvent(WND *w, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT MsgSendKernel(WND *w, UINT msg, WPARAM wparam, LPARAM lparam, ULONG lparam_kind, ULONG lparam_size);
LRESULT MsgCallWindowProc(WND *w, WNDPROC proc, BOOLEAN ansi, UINT msg, WPARAM wparam, LPARAM lparam,
                          ULONG lparam_kind, ULONG lparam_size);
LRESULT DefWindowProcKernel(WND *w, UINT msg, WPARAM wparam, LPARAM lparam); /* kernel lparam pointers. */
LRESULT DefWindowProcUser(WND *w, UINT msg, WPARAM wparam, LPARAM lparam);   /* user lparam pointers. */
ULONG MsgPostedBits(const QUEUED_MESSAGE *qm); /* the QS_* kind of a posted message. */
BOOLEAN HotkeyRegister(W32THREAD *t, HWND hwnd, int id, UINT modifiers, UINT vk);
BOOLEAN HotkeyUnregister(W32THREAD *t, HWND hwnd, int id);
void HotkeyRemoveWindow(WND *w);
void HotkeyRemoveThread(W32THREAD *t);
BOOLEAN HotkeyInput(const MimeInputEvent *ev);
BOOLEAN MsgPeek(W32THREAD *t, MSG *out, HWND hwnd, UINT min, UINT max, UINT flags, BOOLEAN wait);
void MsgProcessSent(W32THREAD *t);
ULONG MsgInSendFlags(W32THREAD *t);
BOOLEAN MsgReply(W32THREAD *t, LRESULT result);
BOOLEAN MsgDirectProc(WND *w, UINT msg, BOOLEAN send, WNDPROC *proc, BOOLEAN *ansi);
void MsgPaintDispatched(WND *w);
BOOLEAN MsgFilterMatches(HWND hwnd, UINT msg, HWND filter, UINT min, UINT max);
void TimerProcessExpired(W32THREAD *t, LONG64 *next_due);
void TimerRemoveWindow(WND *w);
void TimerRemoveThread(W32THREAD *t);
BOOLEAN TimerGetMessage(W32THREAD *t, HWND hwnd, UINT min, UINT max, BOOLEAN remove, MSG *out);
/* set or kill a thread timer with the lock held; system timers post WM_SYSTIMER. */
UINT_PTR TimerSet(W32THREAD *t, HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc, BOOLEAN system);
BOOLEAN TimerKill(W32THREAD *t, HWND hwnd, UINT_PTR id, BOOLEAN system);
LONG64 TimerNextDue(W32THREAD *t);

/* hooks. */

typedef struct HOOK {
    USER_OBJECT Header;
    LIST_ENTRY Links;
    int Id;
    HOOKPROC Proc;
    BOOLEAN Ansi;
    W32THREAD *Owner;      /* the installing thread, where low level hooks run. */
    W32THREAD *Target;     /* the hooked thread, NULL for every thread. */
    W32PROCESS *Process;
} HOOK;

BOOLEAN HookActive(int id, W32THREAD *t);
/* calls the first hook of the chain the thread sees; data is the lparam structure, size 0 for a value. */
LRESULT HookCall(int id, W32THREAD *t, int code, WPARAM wparam, LPARAM lparam, ULONG size, BOOLEAN *called);
LRESULT HookInvoke(HOOK *h, int code, WPARAM wparam, LPARAM lparam, ULONG size); /* in the current thread. */
BOOLEAN HookCbt(WND *w, int code, WPARAM wparam, LPARAM lparam, ULONG size); /* TRUE when the hook prevents it. */
HOOK *HookLookup(HHOOK handle);
void HookRemoveThread(W32THREAD *t);

/* input. */

void InputInitialize(void);
/* turns a raw host event into a message; consumed means the event produces nothing and is dropped. */
BOOLEAN InputTranslate(W32THREAD *t, QUEUED_MESSAGE *qm, MSG *out, HWND filter, UINT min, UINT max, BOOLEAN remove,
                       BOOLEAN *consumed);
extern POINT InputCursorPos;
extern ULONG InputHostPress; /* the left button is held on the host: a number of its own for each press, or zero. */
extern POINT InputPressPos;  /* where the cursor was at that press. */
extern BOOLEAN InputCursorByProgram; /* a program set the cursor and no hardware has moved it since. */
extern BOOLEAN InputPointerOutside; /* the host said the last pointer event was outside its window. */
/* raw input thread owns the desktop window, like the Windows csrss threads. */
extern HANDLE InputThreadId, InputProcessId;
extern UCHAR InputAsyncKeys[256];
/* counts the changes to anything GetKeyState reads: a key, the foreground, the focus owner, the attached threads. */
extern volatile LONG InputKeyGeneration;
void UserSharedKeysChanged(void);
void UserSharedForeground(HWND window);
static inline void InputKeysChanged(void)
{
    __atomic_add_fetch(&InputKeyGeneration, 1, __ATOMIC_SEQ_CST);
    UserSharedKeysChanged();
}
BOOLEAN NTAPI W32FastService(ULONG number, ULONG64 argument, ULONG64 *result);
extern WND *InputForeground;
/* the thread that set the focus last, for a foreground window that has windows of several threads. */
extern W32THREAD *InputFocusOwner;
void InputSetForeground(WND *w);
void InputSetCapture(W32THREAD *t, WND *w, ULONG nc_hit);
extern BOOLEAN InputCursorHidden; /* the host shows no cursor. */
/* rawinput.c. */
POINT InputClampCursor(POINT pt);
void InputUpdatePointer(void);
void InputCursorMoved(void);
void InputSetCursorPos(POINT physical);
void InputCursorShown(void);
void InputForegroundChanged(void);
BOOLEAN RawInputEvent(const MimeInputEvent *ev, BOOLEAN injected);
BOOLEAN RawInputNoHotkeys(void);
void RawPadEvent(const MimeInputEvent *ev);
/* host toasts and launcher integration, shell.c. */
void ShellInitialize(void);
void ShellHostEvent(const MimeInputEvent *ev);
void ShellFreeProcess(W32PROCESS *p);
void RawFreeProcess(W32PROCESS *p);

/* painting and presentation. */

void PaintFlushThread(W32THREAD *t);
void PaintPresent(WND *top);
void PaintUpdateNow(WND *w);
HDC PaintGetDC(WND *w, HRGN clip, ULONG flags);
HDC PaintGetMonitorDC(ULONG index);
void PaintReleaseDC(DC *dc);
HDC PaintBegin(WND *w, PAINTSTRUCT *ps);
void PaintEnd(WND *w, const PAINTSTRUCT *ps);
void PaintRetireSurface(SURFACE *s);
void HostWindowCreate(WND *w);
void HostWindowDestroy(WND *w);
void HostConfigure(WND *w, BOOLEAN activate);
void HostPresent(WND *w, const RECT *dirty);
void LayerWindowDestroyed(WND *w);
SURFACE *LayerPrepare(WND *w, const RECT *dirty); /* surface to present, composed when needed. */
void LayerStyleChanged(WND *w);
BOOLEAN LayerHitTest(WND *w, POINT pt); /* FALSE when the window shape or a clear pixel of a layered window lets the screen point through. */
void MenuFreeProcess(W32PROCESS *p);
void PropFreeWindow(WND *w);
ATOM PropAtom(PCWSTR name); /* the atom of a property set by name from user mode, 0 when unused. */
HANDLE PropGetData(WND *w, ATOM atom);
void MenuWindowDestroyed(WND *w);
BOOLEAN MenuCommandEnabled(WND *w, UINT command);
/* menubar.c. */
LONG MenuBarCompute(WND *w, LONG x, LONG y, LONG width);
void MenuBarFree(WND *w);
BOOLEAN MenuBarRect(WND *w, LONG item, RECT *screen);
BOOLEAN MenuBarArea(WND *w, RECT *screen);
LONG MenuBarHit(WND *w, POINT screen);
void MenuBarPaint(WND *w, SURFACE *s, const REGION *clip, POINT origin);
void MenuBarRedraw(WND *w);
void MenuBarPaintIcon(WND *w, SURFACE *s, const REGION *clip, const RECT *area);
ULONG_PTR MenuBarCall(WND *w, PVOID user_request);
extern WND *MenuBarCalcWindow;               /* the window a WM_NCCALCSIZE from the window manager is for. */
void MenuWindowSetMenu(WND *w, HMENU menu); /* a top level window's menu bar, with the count of its users. */
void MenuRefreshWindowState(WND *w);
void ClipboardWindowDestroyed(WND *w);
void ClipboardDisown(WND *w);
void ClipboardThreadDestroyed(W32THREAD *t);
void ClipboardHostChanged(ULONG mask, ULONG generation); /* the MIME_CLIP_* kinds the host clipboard holds. */
void ClipboardHostRequest(ULONG kind, ULONG id);
UINT ClipboardFormatOfKind(ULONG kind);
ULONG ClipboardKindOfFormat(UINT format);

/* ime.c. */
void ImeHostEvent(const MimeInputEvent *ev);
void ImeWindowDestroyed(WND *w);
void ImeFocusChanged(void);
LRESULT ImeDefaultCompositionChars(WND *w);

/* dnd.c. */
void DndHostEvent(const MimeInputEvent *ev);
void DndThreadDestroyed(W32THREAD *t);
void DndWindowDestroyed(WND *w);
/* release a left button whose up event was consumed by the host, input.c. */
void InputHostDragStep(WND *w, LONG dx, LONG dy, LONG cursor_dx, LONG cursor_dy);
void InputReleaseLeftButton(void);         /* another host program pastes a clipboard format owned by the session. */
void GpuFreeProcess(W32PROCESS *p);
void VkFreeProcess(W32PROCESS *p);
void AudioFreeProcess(W32PROCESS *p);
void GfxShareFreeProcess(W32PROCESS *p);
void HostSetCursor(ULONG cursor);
BOOLEAN HostSetCursorImage(HCURSOR cursor);
void HostForgetCursor(HCURSOR cursor);
BOOLEAN HostHoldCursor(void);
void HostReleaseCursor(PETHREAD thread);
void HostBeep(UINT type);

/* system. */

typedef struct W32_DISPLAY {
    LONG Width;
    LONG Height;
    RECT Work;
    UINT Dpi;
    UINT RefreshHz;
    DWORD Bits; /* of the display mode a program set, 32 otherwise. */
    BOOLEAN Stretch; /* the host stretches the windows of a program that is not dpi aware. */
    BOOLEAN NoMinimize; /* the desktop of the host has no place for a minimized window. */
} W32_DISPLAY;

extern W32_DISPLAY W32Display;
UINT W32SystemDpi(void);

/* monitor.c: the host's monitors in the virtual screen, the primary at 0, 0. */
void MonitorsRefresh(void);
BOOLEAN MonitorsTakeChanged(void);
ULONG MonitorCount(void);
BOOLEAN MonitorGet(ULONG index, MIME_MONITOR_INFO *out);
LONG MonitorFromRectIndex(const RECT *rect, BOOLEAN nearest);
RECT MonitorWorkOfRect(const RECT *rect);
UINT MonitorDpiOfRect(const RECT *rect);
ULONG MonitorGeneration(void);

/* the handle of the host's layout, the one layout every thread has. */
HKL KeyboardCurrentLayout(void);
RECT MonitorVirtualScreen(void);
BOOLEAN MonitorCoveredBy(const RECT *rect);
LONG MonitorFromDeviceName(PCWSTR name);
HMONITOR MonitorHandle(ULONG index);
LONG MonitorIndexOfHandle(HMONITOR monitor);
void MonitorEdidSize(ULONG index, ULONG mm[2], ULONG cm[2]); /* what its edid tells, in millimetres and centimetres. */
ULONG_PTR MonitorCall(ULONG_PTR param1, ULONG_PTR param2, ULONG routine);
void WndHostDisplay(WND *w, ULONG monitor);
void WndHostSize(WND *w, LONG width, LONG height);
BOOLEAN HostMoveSize(WND *w, ULONG edge, BOOLEAN held);
BOOLEAN MoveSizeDefault(WND *w, UINT msg, WPARAM wparam, LPARAM lparam);
BOOLEAN MoveSizeHostStep(WND *w, LONG *cx, LONG *cy);
void MoveSizeHostEnd(WND *w);
W32THREAD *W32NextThread(W32THREAD *previous);
void DeskThreadFreed(W32THREAD *t);
BOOLEAN FontFamilyInstalled(PCWSTR name);
void FontAskFor(PCWSTR face);
HICON CursorProcessIcon(W32PROCESS *p);
void HostPart(WND *w, RECT *part);
ULONG HostBorderColor(WND *w);

/* tray.c: the notification area. */
void TrayHostEvent(const MimeInputEvent *ev);
void TrayWindowDestroyed(WND *w);
void TrayFreeProcess(W32PROCESS *p);
/* list menu items and submenus with their ids for the host, menu.c. */
ULONG MenuFlattenForHost(HMENU handle, MimeTrayItem *items, ULONG *ids, ULONG max);
void WndCheckMonitorDpi(WND *w);
UINT WndHostDpi(WND *w);
int MetricsGet(int index, UINT dpi);
int MetricsGetFor(int index, UINT dpi, ULONG awareness);
BOOLEAN MetricsKeyboardCues(void);
COLORREF MetricsSysColor(int index);

/* screen and desktop DC surface, composed from windows before reads, screen.c. */
SURFACE *ScreenSurface(void);
SURFACE *ScreenVirtualSurface(ULONG awareness, POINT *origin);
void ScreenVirtualVisibleRegion(ULONG awareness, REGION *out);
void ScreenVirtualFlush(SURFACE *s, const RECT *dirty);
void ScreenKeep(const RECT *area);
void ScreenComposeFor(const SURFACE *s, const RECT *area);
POINT ScreenOrigin(void);
BOOLEAN ScreenMonitorRect(ULONG index, RECT *out);
void ScreenVisibleRegion(REGION *out);
BOOLEAN ScreenIsSurface(const SURFACE *s);
void ScreenCompose(const RECT *area);
BOOLEAN ScreenWindowPixels(WND *w, BOOLEAN client_only, BOOLEAN host_pixels, PUCHAR bits, LONG stride, LONG width,
                           LONG height);
void ScreenReadUnpainted(WND *top, const RECT *area, HDC hdc, LONG x, LONG y);
BOOLEAN ScreenUnpaintedPixel(SURFACE *s, LONG x, LONG y, ULONG *pixel);
ULONG64 ScreenHostSerial(void);
void ScreenLayer(WND *w, MIME_PRESENT_LAYER *out);
void ScreenLayersChanged(WND *w);
void ScreenHeldDrawn(const SURFACE *s, const RECT *drawn);
void ScreenHeldFlush(WND *top);
void ScreenHeldGone(const WND *w);
extern LONG ScreenHeldCount;
/* the count of the last ScreenLayer, see MimeGpuSurfaceConfig. */
extern ULONG ScreenLayerStamp;
extern ULONG ScreenSerial;
extern ULONG VkProcessCount;
HBRUSH MetricsSysColorBrush(int index);
HBRUSH MetricsGrayBrush(void);
void UserSharedColor(int index, COLORREF unmatched, COLORREF color);
BOOL MetricsParametersInfo(UINT action, UINT param, PVOID data, UINT ini, UINT dpi);
void MetricsInitialize(void);
void MetricsDisplayChanged(void);
ULONG MetricsDisplayUniqueness(void);
/* display.c. */
void DisplayApplyMode(void);
void DisplayProcessExited(HANDLE pid);
void CursorInitialize(void);
void CursorDrawIcon(DC *dc, LONG x, LONG y, HICON icon, LONG cx, LONG cy, UINT flags);
void CursorSet(HCURSOR cursor);
HCURSOR CursorSystem(ULONG idc);
SURFACE *CursorIconImage(HICON icon);
BOOLEAN CursorValid(HICON icon);
SURFACE *CursorImage(HICON cursor);
ULONG CursorAwareness(HICON cursor);
void CursorHotspot(HICON cursor, LONG *x, LONG *y);
void CursorFreeProcess(W32PROCESS *p);
void CursorFreeThread(W32THREAD *t);
/* capture.c. */
void CaptureFreeProcess(W32PROCESS *p);
void CaptureCursorKind(ULONG kind);
void CaptureCursorShown(HCURSOR cursor);
BOOLEAN CaptureShownImage(PUCHAR out, ULONG capacity, MIME_CAPTURE_SHAPE *shape);
BOOLEAN WinStaOnInputDesktop(void);

/* callbacks into user32 for the system classes. */
WNDPROC ClassProc(const WND_CLASS *c, W32PROCESS *p);
WND_CLASS *ClassFind(W32PROCESS *p, PCWSTR name, ATOM atom, HINSTANCE instance);
ATOM ClassIntegerAtom(PCWSTR name);
void ClassInitialize(void);
void ClassFreeProcess(W32PROCESS *p);
ATOM AtomAdd(PCWSTR name);
ATOM AtomFind(PCWSTR name);
ULONG AtomGetName(ATOM atom, PWSTR buffer, ULONG chars);
void AtomDelete(ATOM atom);

/* user mode pointer helpers. */
NTSTATUS W32CaptureString(PUNICODE_STRING user, PWSTR *out, SIZE_T *chars);
#define W32Probe(...) ((void)0)

#endif
