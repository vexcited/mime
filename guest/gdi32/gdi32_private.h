#pragma once
#include <mime/nt.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>

/* palettes live in gdi32: win32k drives a true color display only. */
HPALETTE PalDefault(void);
BOOL PalIsPalette(HGDIOBJ handle);
BOOL PalDelete(HPALETTE palette);
int PalGetObject(HPALETTE palette, int size, LPVOID buffer);
HPALETTE PalCurrent(HDC dc);
void PalForgetDC(HDC dc);
BOOL GdiSwapBrushOrigin(HDC dc, int x, int y, LPPOINT old);

/* dib color table and bits sizes, shared with the metafile code. */
DWORD GdiDibTableSize(const BITMAPINFOHEADER *header, UINT usage);
DWORD GdiDibBitsSize(const BITMAPINFOHEADER *header);

/* ------------------------------------------------------------------ metafiles */

/*
 * enhanced metafile dcs are win32k metafile dcs whose calls gdi32 also
 * records. windows metafile dcs exist only in gdi32. handle type bytes match
 * windows: 0x66 for a windows metafile dc, 0x46 for an enhanced metafile,
 * 0x26 for a windows metafile.
 */
#define MF_TYPE_WMF_DC 0x66
#define MF_TYPE_EMF 0x46
#define MF_TYPE_WMF 0x26

extern volatile LONG MfDcCount;
struct EMF_DC;
struct EMF_DC *EmfDc(HDC dc);

static inline BOOL MfIsWmfDc(HDC dc)
{
    return (((ULONG_PTR)dc >> 16) & 0xFF) == MF_TYPE_WMF_DC;
}

/* either kind of metafile dc, cheap while none exists. */
static inline BOOL MfActive(HDC dc)
{
    return MfDcCount && (MfIsWmfDc(dc) || EmfDc(dc));
}

/*
 * records a call on a metafile dc. a failed record returns fail, a windows
 * metafile dc then returns wmf, and an enhanced one goes on to win32k, which
 * keeps its state.
 */
#define MF_RECORD(dc, call, fail, wmf)                                                                                 \
    do {                                                                                                               \
        if (MfActive(dc)) {                                                                                            \
            BOOL recorded_ = (call);                                                                                   \
            if (!recorded_)                                                                                            \
                return (fail);                                                                                         \
            if (MfIsWmfDc(dc))                                                                                         \
                return (wmf);                                                                                          \
        }                                                                                                              \
    } while (0)

/* recorders for both kinds of metafile dc. */
BOOL MfSetValue(HDC dc, UINT emr, UINT meta, DWORD value);
BOOL MfSetColor(HDC dc, UINT emr, UINT meta, COLORREF color);
BOOL MfSetPoint(HDC dc, UINT emr, UINT meta, int x, int y);
BOOL MfOffsetOrg(HDC dc, BOOL viewport, int x, int y);
BOOL MfScaleExt(HDC dc, BOOL viewport, int xn, int xd, int yn, int yd);
BOOL MfSetMapMode(HDC dc, int mode);
BOOL MfRecordEmpty(HDC dc, UINT emr, UINT meta);
BOOL MfRestoreDC(HDC dc, int level);
BOOL MfSelectObject(HDC dc, HGDIOBJ object, HGDIOBJ *previous);
BOOL MfSelectPalette(HDC dc, HPALETTE palette);
void MfDeleteObject(HGDIOBJ object);
BOOL MfSetWorldTransform(HDC dc, const XFORM *xform, DWORD mode, BOOL modify);
BOOL MfSetMiterLimit(HDC dc, FLOAT limit);
BOOL MfClipRect(HDC dc, UINT emr, UINT meta, int left, int top, int right, int bottom);
BOOL MfSelectClipRgn(HDC dc, HRGN region, int mode);
BOOL MfSelectClipPath(HDC dc, int mode);
BOOL MfMoveTo(HDC dc, int x, int y);
BOOL MfLineTo(HDC dc, int x, int y);
BOOL MfBox(HDC dc, UINT emr, UINT meta, int left, int top, int right, int bottom);
BOOL MfRoundRect(HDC dc, int left, int top, int right, int bottom, int width, int height);
BOOL MfArc(HDC dc, UINT emr, UINT meta, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2);
BOOL MfAngleArc(HDC dc, int x, int y, DWORD radius, FLOAT start, FLOAT sweep);
BOOL MfPoly(HDC dc, UINT emr, UINT meta, const POINT *points, DWORD count);
BOOL MfPolyPoly(HDC dc, UINT emr, UINT meta, const POINT *points, const DWORD *counts, DWORD polys);
BOOL MfPolyDraw(HDC dc, const POINT *points, const BYTE *types, DWORD count);
BOOL MfSetPixel(HDC dc, int x, int y, COLORREF color);
BOOL MfExtFloodFill(HDC dc, int x, int y, COLORREF color, UINT type);
BOOL FloodFillDc(HDC dc, int x, int y, COLORREF color, UINT type);
BOOL MfTextOut(HDC dc, int x, int y, UINT options, const RECT *rect, LPCWSTR text, UINT count, const INT *dx);
BOOL MfTextOutA(HDC dc, int x, int y, UINT options, const RECT *rect, LPCSTR text, UINT count, const INT *dx);
BOOL MfPath(HDC dc, UINT emr);
BOOL MfBitBlt(HDC dc, int x, int y, int width, int height, HDC src, int sx, int sy, int sw, int sh, DWORD rop,
              UINT emr);
BOOL MfStretchDIBits(HDC dc, int x, int y, int width, int height, int sx, int sy, int sw, int sh, const void *bits,
                     const BITMAPINFO *info, UINT usage, DWORD rop);
BOOL MfSetDIBitsToDevice(HDC dc, int x, int y, DWORD width, DWORD height, int sx, int sy, UINT start, UINT lines,
                         const void *bits, const BITMAPINFO *info, UINT usage);
BOOL MfGradientFill(HDC dc, const TRIVERTEX *vertices, ULONG count, const void *mesh, ULONG mesh_count, ULONG mode);
BOOL MfRegion(HDC dc, UINT emr, UINT meta, HRGN region, HBRUSH brush, int width, int height);
BOOL MfComment(HDC dc, UINT size, const BYTE *data);
BOOL MfSetDCColor(HDC dc, BOOL pen, COLORREF color);
int MfGetDeviceCaps(HDC dc, int index, BOOL *handled);
DWORD MfObjectType(HGDIOBJ handle);
BOOL MfDeleteDC(HDC dc);
BOOL MfGetBounds(HDC dc, LPRECT rect, UINT flags, UINT *result);
void MfSetBounds(struct EMF_DC *emf, const RECT *rect, UINT flags);

/* metafile.c: windows metafile dcs. */
BOOL WmfRecord(HDC dc, WORD function, UINT count, const WORD *params);
BOOL WmfSelectObject(HDC dc, HGDIOBJ object, HGDIOBJ *previous);
void WmfDeleteObject(HGDIOBJ object);
BOOL WmfDeleteDC(HDC dc);
BOOL WmfDibRecord(HDC dc, WORD function, const WORD *params, UINT count, const BITMAPINFO *info, UINT info_size,
                  const void *bits, UINT bits_size);
HDC WmfCreateDC(void);
DWORD WmfObjectType(HGDIOBJ handle);

struct EMF_DC *EmfCreateDC(HDC ref, LPCWSTR file, const RECT *frame, LPCWSTR description, UINT desc_chars);
void *EmfRecordAlloc(struct EMF_DC *emf, DWORD type, DWORD size);
void EmfDestroy(struct EMF_DC *emf, BOOL delete_file);

/* an enhanced metafile: a copy of the bits and the state of its playback. */
typedef struct EMF_PLAY EMF_PLAY;
typedef struct EMF {
    BYTE *Data;
    DWORD Size;
    EMF_PLAY *Play;
} EMF;

HENHMETAFILE EmfCreateHandle(BYTE *data, DWORD size);
EMF *EmfLookup(HENHMETAFILE handle);
BOOL EmfValid(const BYTE *data, DWORD size);
BOOL EmfCancelPlay(HDC dc);

/* ------------------------------------------------------------------ printers, printer.c */

extern volatile LONG PrnDcCount;
BOOL PrnIsPrinterDc(HDC dc);
HDC PrnCreateDC(LPCWSTR driver, LPCWSTR device, const void *mode, BOOL info_only, BOOL *handled);
BOOL PrnDeleteDC(HDC dc);
BOOL PrnGetDeviceCaps(HDC dc, int index, int *value);
int PrnEscape(HDC dc, int escape, int in_size, LPCSTR in, int out_size, LPSTR out, BOOL *handled);
HDC PrnCreateDCA(LPCSTR driver, LPCSTR device, const void *mode, BOOL info_only, BOOL *handled);
