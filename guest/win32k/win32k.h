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
} GDI_OBJECT;

HANDLE GdiCreateHandle(GDI_OBJECT *object, UCHAR type);
void GdiFreeHandle(GDI_OBJECT *object);
GDI_OBJECT *GdiLookup(HANDLE handle, UCHAR type); /* type 0 accepts any. */
UCHAR GdiHandleType(HANDLE handle);
void GdiDeleteObject(GDI_OBJECT *object);
void GdiReleaseProcessObjects(HANDLE pid);
GDI_OBJECT *GdiStockObject(int index);
HANDLE GdiStockHandle(int index);
void GdiInitializeStockObjects(void);

/* surfaces back bitmaps, DIB sections and window contents. */
#define SURF_OWNS_BITS 0x01   /* bits allocated from pool. */
#define SURF_USER_BITS 0x02   /* DIB section bits in the owner process. */
#define SURF_DEVICE 0x04      /* window surface: writes are opaque, alpha 255. */
#define SURF_CONTIGUOUS 0x08  /* physically contiguous, presentable to the host. */
#define SURF_TOPDOWN 0x10
#define SURF_RETIRED 0x20     /* replaced window surface, freed when the last DC releases it. */
#define SURF_FOREIGN 0x40     /* user bits the caller owns, from D3DKMTCreateDCFromMemory. */

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
} SURFACE;

SURFACE *SurfaceCreate(LONG width, LONG height, UCHAR bpp, ULONG flags);
SURFACE *SurfaceWrap(LONG width, LONG height, UCHAR bpp, PVOID bits, LONG stride, const BITMAPINFO *info);
void SurfaceDestroy(SURFACE *s);
void SurfaceMarkDirty(SURFACE *s, const RECT *r);
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
    POINT ViewportOrg;
    POINT WindowOrg;
    POINT CurrentPos;
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
    struct FONT_INSTANCE *Realized;
    FONT *RealizedFor;
    RECT Bounds;
    ULONG PathState;
    GDI_PATH Path;
    int PixelFormat; /* memory DCs; window DCs use the window's. */
    BOOLEAN OfMonitor; /* a DC of the device \\.\DISPLAYn, which is only that monitor. */
    ULONG Monitor;
    SIZE VirtualPixels; /* SetVirtualResolution, zero when unset. */
    SIZE VirtualMm;
} DC;

DC *DcLookup(HDC dc);
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
void DrawRoundRectAA(SURFACE *s, const REGION *clip, const RECT *rect, LONG rx, LONG ry, BOOLEAN filled, ULONG fill,
                     ULONG border, LONG border_width);
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
} GLYPH;

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
    float ScaleX;
    float ScaleY;
    TEXTMETRICW Metrics;
    LONG UnderlinePos;
    LONG UnderlineSize;
    LONG StrikeoutPos;
    ULONG RefCount;
    ULONG *CharPages[256]; /* glyph index + 1 for code points by high byte, BMP only, 0 when unknown. */
    GLYPH *Pages[256];     /* rendered glyphs by glyph index high byte. */
} FONT_INSTANCE;

void FontInitialize(void);
FONT_INSTANCE *FontRealize(const LOGFONTW *lf, UINT dpi);
const GLYPH *FontGetGlyph(FONT_INSTANCE *fi, ULONG codepoint);
const GLYPH *FontGetGlyphIndex(FONT_INSTANCE *fi, USHORT index);
USHORT FontGlyphIndex(FONT_INSTANCE *fi, ULONG codepoint); /* 0 for missing glyphs. */
void FontGlyphAbcFloat(FONT_INSTANCE *fi, USHORT index, ABCFLOAT *abc);
UCHAR *FontRenderTransformed(FONT_INSTANCE *fi, USHORT index, const XFORM *m, LONG *left, LONG *top, LONG *width,
                             LONG *height);
void FontReleaseProcess(HANDLE pid);
void FontGetFaceName(FONT_INSTANCE *fi, PWSTR name, ULONG chars);
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
    USER_TYPE_HOOK = 5,
    USER_TYPE_MONITOR = 12,
};

typedef struct USER_OBJECT {
    HANDLE Handle;
    UCHAR Type;
    BOOLEAN Destroyed;
    USHORT Pad;
    LONG RefCount;
} USER_OBJECT;

HANDLE UserCreateHandle(USER_OBJECT *object, UCHAR type);
void UserFreeHandle(USER_OBJECT *object);
USER_OBJECT *UserLookup(HANDLE handle, UCHAR type);
void UserReference(USER_OBJECT *object);
void UserDereference(USER_OBJECT *object);

/* threads, processes, queues. */

typedef struct W32PROCESS {
    PEPROCESS Process;
    HANDLE Pid;
    LIST_ENTRY Classes;
    LIST_ENTRY Links;
    ULONG DpiAwareness;
    MIME_CLIENT_INFO Client;
    BOOLEAN ClientValid;
    BOOLEAN Wow;        /* a 32-bit program, its structures are converted at its boundary. */
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
} W32PROCESS;

typedef struct QUEUED_MESSAGE {
    LIST_ENTRY Links;
    MSG Msg;
    ULONG_PTR ExtraInfo;
    ULONG Kind; /* QM_* */
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
    HWND Focus;
    HWND Active;
    HWND Capture;
    ULONG CaptureNc;  /* nc hit code of a capture held by the frame. */
    UCHAR KeyState[256]; /* bit 7 down, bit 0 toggled, as seen by this thread. */
    HWND MouseWindow;    /* window under the mouse for leave tracking. */
    TRACKMOUSEEVENT Track;
    ULONG CallbackDepth;
    ULONG LastRead;          /* tick of the last time the thread looked at its queue. */
    BOOLEAN InWait;          /* sleeping in a message wait or in a send, serving its queue. */
    struct HOOK *CurrentHook; /* the hook procedure running, for CallNextHookEx. */
    BOOLEAN Destroying;
    HCURSOR Cursor;
    LONG CursorShowCount;
    struct W32DESKTOP *Desktop;  /* referenced, winsta.c. */
    HANDLE DesktopHandle;
    HWND TsfSink;          /* text services window for host input methods, ime.c. */
    BOOLEAN AltAlone;      /* Alt or F10 went down and no other key or button has since, for the menu bar. */
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
void QueueWake(W32THREAD *t, ULONG bits);
BOOLEAN QueuePost(W32THREAD *t, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
/* the clock's offset changed: every top level window is told with WM_TIMECHANGE. */
void NTAPI W32TimeChange(void);
BOOLEAN QueueWindowOperation(W32THREAD *t, HWND hwnd, UINT command, const WINDOWPLACEMENT *placement);
void QueuePostInput(W32THREAD *t, const MimeInputEvent *ev, WPARAM mouse_state);
DWORD W32TickCount(void);
/* tick count of the last host or injected input event. */
extern DWORD InputLastTime;
ULONG GdiCountProcessObjects(HANDLE pid);
ULONG WndCountProcessObjects(W32PROCESS *p);

/* classes and windows. */

typedef struct WND_CLASS {
    LIST_ENTRY Links;
    ATOM Atom;
    PWSTR Name;
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
    ULONG Fnid;
    W32PROCESS *Process; /* NULL for global and system classes. */
    LONG Windows;
    PUCHAR Extra;
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

typedef struct WND {
    USER_OBJECT Header;
    struct WND *Parent;
    struct WND *Owner;
    struct WND *FirstChild; /* top of the z order. */
    struct WND *LastChild;
    struct WND *Next;       /* below this window. */
    struct WND *Prev;       /* above this window. */
    W32THREAD *Thread;
    WND_CLASS *Class;
    DWORD Style;
    DWORD ExStyle;
    ULONG Flags;
    RECT WindowRect;  /* screen coordinates. */
    RECT ClientRect;  /* screen coordinates. */
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
    PWSTR Text;
    ULONG TextLength;
    REGION Update;    /* client relative update region, screen coordinates. */
    HICON Icon;
    HICON IconSmall;
    UINT Dpi;
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
    ULONG TouchFlags;
    ULONG DisplayAffinity; /* WDA_*, how captures show the window. */
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
WND *WndTopLevel(WND *w);
BOOLEAN WndIsVisible(WND *w);  /* the window and all its ancestors are visible. */
BOOLEAN WndIsChildOf(WND *child, WND *parent);
void WndDestroy(WND *w);
void WndDestroyThreadWindows(W32THREAD *t);
WND *WndFromPoint(WND *top, POINT pt);
WND *WndChildFromPoint(WND *parent, POINT pt, BOOLEAN skip_disabled);
void WndSetPos(WND *w, WND *after, LONG x, LONG y, LONG cx, LONG cy, UINT flags);
void WndDisplayChanged(void);
void WndShow(WND *w, int cmd);
void WndGetMinMaxInfo(WND *w, MINMAXINFO *info);
void WndFullscreen(WND *w, BOOLEAN enter, const RECT *rect);
BOOL WndSetPlacement(WND *w, const WINDOWPLACEMENT *placement);
void WndInvalidate(WND *w, const RECT *client_rect, BOOLEAN erase, BOOLEAN children);
void WndInvalidateRegion(WND *w, const REGION *screen_rgn, ULONG flags);
void WndValidate(WND *w, const RECT *client_rect);
void WndUpdateHostConfig(WND *w, BOOLEAN activate);
void WndEnsureSurface(WND *w); /* top level windows: creates or resizes the surface and the host window. */
void WndComputeVisRgn(WND *w, BOOLEAN client, ULONG dcx_flags, REGION *out);
WND *WndFindNeedsPaint(W32THREAD *t, HWND filter);
void WndActivate(WND *w, BOOLEAN click);
extern BOOLEAN WndActivatedByHost;
void WndSetFocus(WND *w);
void WndCenterOnOwner(WND *w);
UINT WndGetDpi(WND *w);
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
/* a color of the Window or Menu class of the visual style, light or dark. */
BOOLEAN FrameThemeColor(BOOLEAN menu, BOOLEAN dark, int part, int state, int prop, COLORREF *out);
BOOLEAN FrameBorderColor(BOOLEAN dark, BOOLEAN active, ULONG *argb);
void FrameDrawText(SURFACE *s, const REGION *clip, FONT_INSTANCE *fi, LONG x, LONG baseline, PCWSTR text, ULONG count,
                   LONG max_right, ULONG pixel);
void FrameBleed(WND *w);
void FrameGetButtonRect(WND *w, int button, RECT *r); /* window relative. */
void FrameGetVisibleRect(WND *w, RECT *r);              /* window relative. */
ULONG FrameGetDragRects(WND *w, RECT *rects, ULONG max); /* window relative caption areas. */
LRESULT FrameMouse(WND *w, UINT msg, LONG hit, POINT pt);

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
LRESULT MsgSendKernel(WND *w, UINT msg, WPARAM wparam, LPARAM lparam, ULONG lparam_kind, ULONG lparam_size);
LRESULT MsgCallWindowProc(WND *w, WNDPROC proc, BOOLEAN ansi, UINT msg, WPARAM wparam, LPARAM lparam,
                          ULONG lparam_kind, ULONG lparam_size);
LRESULT DefWindowProcKernel(WND *w, UINT msg, WPARAM wparam, LPARAM lparam); /* kernel lparam pointers. */
LRESULT DefWindowProcUser(WND *w, UINT msg, WPARAM wparam, LPARAM lparam);   /* user lparam pointers. */
BOOLEAN MsgPeek(W32THREAD *t, MSG *out, HWND hwnd, UINT min, UINT max, UINT flags, BOOLEAN wait);
void MsgProcessSent(W32THREAD *t);
ULONG MsgInSendFlags(W32THREAD *t);
BOOLEAN MsgReply(W32THREAD *t, LRESULT result);
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
extern BOOLEAN InputPointerOutside; /* the host said the last pointer event was outside its window. */
/* raw input thread owns the desktop window, like the Windows csrss threads. */
extern HANDLE InputThreadId, InputProcessId;
extern UCHAR InputAsyncKeys[256];
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
void InputCursorShown(void);
void InputForegroundChanged(void);
BOOLEAN RawInputEvent(const MimeInputEvent *ev, BOOLEAN injected);
void RawPadEvent(const MimeInputEvent *ev);
/* host toasts and launcher integration, shell.c. */
void ShellInitialize(void);
void ShellHostEvent(const MimeInputEvent *ev);
void ShellFreeProcess(W32PROCESS *p);
void RawFreeProcess(W32PROCESS *p);

/* painting and presentation. */

void PaintFlushThread(W32THREAD *t);
void PaintPresent(WND *top);
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
BOOLEAN LayerHitTest(WND *w, POINT pt); /* FALSE when the window shape excludes the screen point. */
void MenuFreeProcess(W32PROCESS *p);
void PropFreeWindow(WND *w);
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
void InputReleaseLeftButton(void);         /* another host program pastes a clipboard format owned by the session. */
void GpuFreeProcess(W32PROCESS *p);
void VkFreeProcess(W32PROCESS *p);
void AudioFreeProcess(W32PROCESS *p);
void GfxShareFreeProcess(W32PROCESS *p);
void HostSetCursor(ULONG cursor);
void HostBeep(UINT type);

/* system. */

typedef struct W32_DISPLAY {
    LONG Width;
    LONG Height;
    RECT Work;
    UINT Dpi;
    UINT RefreshHz;
    DWORD Bits; /* of the display mode a program set, 32 otherwise. */
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
RECT MonitorVirtualScreen(void);
BOOLEAN MonitorCoveredBy(const RECT *rect);
LONG MonitorFromDeviceName(PCWSTR name);
HMONITOR MonitorHandle(ULONG index);
LONG MonitorIndexOfHandle(HMONITOR monitor);
ULONG_PTR MonitorCall(ULONG_PTR param1, ULONG_PTR param2, ULONG routine);
void WndHostDisplay(WND *w, ULONG monitor);
void WndHostSize(WND *w, LONG width, LONG height);
BOOLEAN HostMoveSize(WND *w, ULONG edge);
void HostPart(WND *w, RECT *part);

/* tray.c: the notification area. */
void TrayHostEvent(const MimeInputEvent *ev);
void TrayWindowDestroyed(WND *w);
/* list menu items and submenus with their ids for the host, menu.c. */
ULONG MenuFlattenForHost(HMENU handle, MimeTrayItem *items, ULONG *ids, ULONG max);
void WndCheckMonitorDpi(WND *w);
UINT WndHostDpi(WND *w);
int MetricsGet(int index, UINT dpi);
BOOLEAN MetricsKeyboardCues(void);
COLORREF MetricsSysColor(int index);

/* screen and desktop DC surface, composed from windows before reads, screen.c. */
SURFACE *ScreenSurface(void);
POINT ScreenOrigin(void);
BOOLEAN ScreenMonitorRect(ULONG index, RECT *out);
void ScreenVisibleRegion(REGION *out);
BOOLEAN ScreenIsSurface(const SURFACE *s);
void ScreenCompose(const RECT *area);
BOOLEAN ScreenWindowPixels(WND *w, BOOLEAN client_only, BOOLEAN host_pixels, PUCHAR bits, LONG stride, LONG width,
                           LONG height);
ULONG64 ScreenHostSerial(void);
extern ULONG ScreenSerial;
HBRUSH MetricsSysColorBrush(int index);
BOOL MetricsParametersInfo(UINT action, UINT param, PVOID data, UINT ini, UINT dpi);
void MetricsInitialize(void);
void MetricsDisplayChanged(void);
/* display.c. */
void DisplayApplyMode(void);
void DisplayProcessExited(HANDLE pid);
void CursorInitialize(void);
void CursorDrawIcon(DC *dc, LONG x, LONG y, HICON icon, LONG cx, LONG cy, UINT flags);
void CursorSet(HCURSOR cursor);
SURFACE *CursorIconImage(HICON icon);
BOOLEAN CursorValid(HICON icon);
SURFACE *CursorImage(HICON cursor);
void CursorHotspot(HICON cursor, LONG *x, LONG *y);
/* capture.c. */
void CaptureFreeProcess(W32PROCESS *p);
void CaptureCursorKind(ULONG kind);
void CaptureCursorShown(HCURSOR cursor);
BOOLEAN WinStaOnInputDesktop(void);

/* callbacks into user32 for the system classes. */
WNDPROC W32ClientProc(W32PROCESS *p, ULONG fnid);
WNDPROC ClassProc(const WND_CLASS *c, W32PROCESS *p);
WND_CLASS *ClassFind(W32PROCESS *p, PCWSTR name, ATOM atom, HINSTANCE instance);
void ClassInitialize(void);
void ClassFreeProcess(W32PROCESS *p);
ATOM AtomAdd(PCWSTR name);
ATOM AtomFind(PCWSTR name);
ULONG AtomGetName(ATOM atom, PWSTR buffer, ULONG chars);

/* user mode pointer helpers. */
NTSTATUS W32CaptureString(PUNICODE_STRING user, PWSTR *out, SIZE_T *chars);
#define W32Probe(...) ((void)0)

#endif
