/* what the parts of ddraw share. */
#ifndef DDRAW_PRIVATE_H
#define DDRAW_PRIVATE_H

#define MIME_BUILD_DDRAW
#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/winspool.h>
#include <mime/win32/ddraw.h>
#include <mime/win32/d3dkmthk.h>

#ifdef _WIN64
static_assert(sizeof(DDSURFACEDESC2) == 136 && sizeof(DDSURFACEDESC) == 120);
#else
static_assert(sizeof(DDSURFACEDESC2) == 124 && sizeof(DDSURFACEDESC) == 108);
#endif
static_assert(sizeof(DDCAPS) == 380 && sizeof(DDPIXELFORMAT) == 32);

typedef struct DDRAW DDRAW;
typedef struct DDSURF DDSURF;
typedef struct DDPAL DDPAL;
typedef struct DDCLIP DDCLIP;

/* the interface versions, as the vtables of an object know them. */
enum { DDV1 = 1, DDV2 = 2, DDV3 = 3, DDV4 = 4, DDV7 = 7 };

/* ------------------------------------------------------------------ the directdraw object */

/* cooperative state. */
#define DDF_LEVELSET 0x0001   /* SetCooperativeLevel was called */
#define DDF_FULLSCREEN 0x0002
#define DDF_EXCLUSIVE 0x0004  /* holds exclusive mode now */
#define DDF_MODESET 0x0008    /* changed the display mode */
#define DDF_MODEX 0x0010      /* allowed mode x */
#define DDF_ACTIVE 0x0020     /* the exclusive window is active */
#define DDF_SUBCLASSED 0x0040 /* the focus window's procedure is ddraw's */
#define DDF_CLIPPED 0x0080    /* clipped the cursor to the screen */
#define DDF_DEVICEWINDOW 0x0100 /* created its device window */

struct DDRAW {
    IDirectDraw iface1;
    IDirectDraw2 iface2;
    IDirectDraw4 iface4;
    IDirectDraw7 iface7;
    LONG ref1, ref2, ref4, ref7;
    LONG ref;  /* all interfaces together */
    DWORD magic;
    BOOL initialized;
    BOOL dx7;     /* made by DirectDrawCreateEx, as directx 7 makes its objects */
    DWORD flags;
    DWORD level;  /* the DDSCL flags of the last call */
    BOOL level_v1; /* set through IDirectDraw, whose mode outlives exclusive mode */
    HWND focus;   /* the focus window */
    HWND device;  /* the device window, the focus window unless one was set */
    /* the mode the program set, 0 for the desktop's. */
    DWORD mode_width, mode_height, mode_bpp, mode_refresh;
    DDSURF *primary;
    DWORD snapshot_width, snapshot_height, snapshot_bpp; /* the mode TestCooperativeLevel compares with */
    RECT dirty;   /* of the primary, waiting for the next refresh */
    LIST_ENTRY surfaces;
    LIST_ENTRY palettes;
    LIST_ENTRY clippers;
    struct D3DOBJ *d3d; /* its direct3d object, made at the first query */
    LIST_ENTRY links; /* in the process's list */
};

#define DDRAW_MAGIC 0x57415244

extern RTL_CRITICAL_SECTION DdLock;
extern HINSTANCE DdModule;
extern LIST_ENTRY DdObjects;

static inline void DdEnter(void)
{
    RtlEnterCriticalSection(&DdLock);
}

static inline HRESULT DdLeave(HRESULT hr)
{
    RtlLeaveCriticalSection(&DdLock);
    return hr;
}

DDRAW *DdFromIface(void *iface);
HRESULT DdCreate(const GUID *guid, DWORD version, void **out, BOOL initialize);
/* caller is the version of the interface queried. */
HRESULT DdQuery(DDRAW *dd, DWORD caller, REFIID riid, void **out);
ULONG DdAddRef(DDRAW *dd, LONG *iface_ref);
ULONG DdRelease(DDRAW *dd, LONG *iface_ref);
void *DdIface(DDRAW *dd, DWORD version);

/* display.c */
typedef struct DDMODE {
    DWORD width, height, bpp, refresh;
} DDMODE;
void DdCurrentMode(DDMODE *mode);
void DdPixelFormatForBpp(DWORD bpp, DDPIXELFORMAT *pf);
HRESULT DdSetCooperativeLevel(DDRAW *dd, DWORD version, HWND window, DWORD flags);
HRESULT DdSetDisplayMode(DDRAW *dd, DWORD width, DWORD height, DWORD bpp, DWORD refresh, DWORD flags);
HRESULT DdRestoreDisplayMode(DDRAW *dd);
HRESULT DdEnumDisplayModes(DDRAW *dd, DWORD version, DWORD flags, const DDSURFACEDESC2 *filter, void *context,
                           void *callback);
void DdFillModeDesc(DWORD version, DDSURFACEDESC2 *desc, DWORD width, DWORD height, DWORD bpp, DWORD refresh);
HRESULT DdWaitForVerticalBlank(DWORD flags);
HRESULT DdGetScanLine(DWORD *line);
void DdReleaseExclusive(DDRAW *dd);
BOOL DdIsExclusiveOwner(DDRAW *dd);
/* the primary's gamma ramp on the display while its object is the active exclusive owner, else taken off. */
void DdShowGamma(DDRAW *dd);
void DdHideGamma(DDSURF *primary);
DDRAW *DdExclusiveOwner(void);

/* ------------------------------------------------------------------ surfaces */

/* how a surface's pixels are laid out. */
typedef struct DDFORMAT {
    DDPIXELFORMAT pf;
    DWORD bpp;       /* bits a pixel, 0 for block compressed */
    DWORD block;     /* bytes of a 4x4 block for dxt, 0 otherwise */
    D3DDDIFORMAT gdi; /* what a dc over it is, D3DDDIFMT_UNKNOWN for none */
} DDFORMAT;

typedef struct DDLOCKED {
    RECT rect;
    BYTE *pointer;    /* what Lock returned, which the first interfaces unlock with */
} DDLOCKED;

typedef struct DDPRIVATE {
    LIST_ENTRY links;
    GUID tag;
    DWORD flags;
    DWORD uniqueness; /* the surface's when set, for DDSPD_VOLATILE */
    DWORD size;
    BYTE data[];
} DDPRIVATE;

/* surface memory, which duplicates of a surface share. */
typedef struct DDMEM {
    LONG ref;
    BYTE *base;
} DDMEM;

/* a surface attached to another, holding a reference on the interface given unless implicit. */
typedef struct DDATTACH {
    LIST_ENTRY links;
    DDSURF *surface;
    DWORD version;    /* of the interface the reference is on */
    BOOL implicit;
} DDATTACH;

struct DDSURF {
    IDirectDrawSurface iface1;
    IDirectDrawSurface2 iface2;
    IDirectDrawSurface3 iface3;
    IDirectDrawSurface4 iface4;
    IDirectDrawSurface7 iface7;
    IDirectDrawGammaControl gamma_iface;
    IDirectDrawColorControl color_iface;
    LONG ref1, ref2, ref3, ref4, ref7, ref_gamma, ref_color;
    LONG ref;          /* all interfaces together */
    DWORD magic;
    DWORD version;     /* of the interface that created it */
    DDRAW *dd;         /* null once its directdraw object is gone */
    void *creator;     /* the directdraw interface a version 4 or 7 surface holds */
    void *creator_iface; /* the directdraw interface it was made through, for GetDDInterface */
    DDSURFACEDESC2 desc; /* as GetSurfaceDesc reports it, lpSurface null */
    DDFORMAT format;
    BOOL explicit_format; /* created with DDSD_PIXELFORMAT */
    BYTE *memory;      /* the pixels, rows desc.lPitch apart */
    struct DDMEM *mem; /* what memory was allocated as, shared by duplicates; null for client memory */
    SIZE_T bytes;      /* of the memory */
    BOOL sysmem_requested; /* created with DDSCAPS_SYSTEMMEMORY, so a program may give its memory */
    BOOL lost;
    BOOL freeing;      /* in its final release */
    BOOL gdi;          /* holds the memory gdi shows, the front of the primary's chain */
    DDPAL *palette;
    DDCLIP *clipper;
    DWORD ckeys;       /* the DDSD_CK flags of keys set */
    LIST_ENTRY attached;  /* DDATTACH of surfaces attached to it, newest first */
    BOOL implicit;     /* created with its complex surface, freed with its root */
    BOOL created_front, created_back; /* caps it had at creation, back after a detach */
    DWORD usage;       /* locks and dcs out */
    DWORD rect_locks;
    DDLOCKED locks[16];
    BOOL readonly_lock;
    HDC dc;
    HANDLE dc_bitmap;
    DWORD uniqueness;
    DWORD priority, lod;
    LIST_ENTRY private_data;
    DDGAMMARAMP *gamma; /* of the primary, null for none */
    BOOL gamma_shown;   /* on the display, as the exclusive owner's */
    DDMODE mode;       /* the display mode it was made in */
    DWORD generation;  /* counts every change of its pixels */
    struct D3DSURFX *d3d; /* its copy where direct3d draws, which travels with the memory */
    LIST_ENTRY links;  /* in the directdraw object's list */
};

#define SURF_MAGIC 0x46525553

DDSURF *DdSurfFromIface(void *iface);
DWORD DdSurfVersionOf(void *iface);
void *DdSurfIface(DDSURF *s, DWORD version);
LONG *DdSurfRefOf(DDSURF *s, DWORD version);
HRESULT DdCreateSurface(DDRAW *dd, DWORD version, const DDSURFACEDESC2 *desc, DDSURF **out);
HRESULT DdSurfQuery(DDSURF *s, REFIID riid, void **out);
HRESULT DdSurfQueryD3D(DDSURF *s, REFIID riid, void **out);
void DdSurfInitIfaces(DDSURF *s);
ULONG DdSurfAddRef(DDSURF *s, LONG *iface_ref);
ULONG DdSurfRelease(DDSURF *s, LONG *iface_ref);
void DdSurfKill(DDSURF *s);
void DdLoseSurfaces(DDRAW *dd);
void DdSurfDescFromV1(const DDSURFACEDESC *in, DDSURFACEDESC2 *out);
void DdSurfDescToV1(const DDSURFACEDESC2 *in, DDSURFACEDESC *out);
void DdSurfFillDesc(DDSURF *s, DWORD version, DDSURFACEDESC2 *out);
BOOL DdSurfMatches(DDSURF *s, const DDSURFACEDESC2 *d);
BOOL DdFormatFromPixelFormat(const DDPIXELFORMAT *pf, DDFORMAT *out);
DWORD DdSurfBytes(const DDSURF *s, LONG x);
BOOL DdSurfBusy(const DDSURF *s);
void DdSurfWritten(DDSURF *s, const RECT *rect);
HRESULT DdSurfDuplicate(DDRAW *dd, DDSURF *src, DDSURF **out);
HRESULT DdSurfRestore(DDSURF *s);
HRESULT DdSurfFlipToGdi(DDSURF *primary);
DDSURF *DdSurfGdiSurface(DDSURF *primary);
HRESULT DdEnumCreatable(DDRAW *dd, DWORD version, DDSURFACEDESC2 *desc, void *context, void *callback);
ULONG64 DdVideoMemoryUsed(void);
void DdAdapterIds(DWORD *vendor, DWORD *device, DWORD *subsys, DWORD *revision);

/* blt.c */
HRESULT DdBlt(DDSURF *dst, const RECT *dst_rect, DDSURF *src, DWORD src_version, const RECT *src_rect, DWORD flags,
              DDBLTFX *fx, DWORD dst_version);
HRESULT DdBltFast(DDSURF *dst, DWORD x, DWORD y, DDSURF *src, const RECT *src_rect, DWORD flags, DWORD dst_version);
HRESULT DdBltBatch(DDSURF *dst, const DDBLTBATCH *batch, DWORD count);
/* convert.c: between formats, and the blocks of four character code surfaces. */
BOOL DdCanConvert(const DDSURF *src, const DDSURF *dst);
void DdBlitConvert(DDSURF *dst, const RECT *d, DDSURF *src, const RECT *s, BOOL mirror_x, BOOL mirror_y);
HRESULT DdBltBlocks(DDSURF *dst, const RECT *d, DDSURF *src, const RECT *s);

/* ------------------------------------------------------------------ palettes and clippers */

struct DDPAL {
    IDirectDrawPalette iface;
    LONG ref;
    DWORD magic;
    DDRAW *dd;
    DWORD caps;
    DWORD count;     /* entries */
    PALETTEENTRY entries[256];
    DWORD uniqueness; /* changes with every SetEntries */
    void *owner;      /* the IDirectDraw4 or 7 that made it, which it holds */
    LIST_ENTRY links;
};

#define PAL_MAGIC 0x4C415044

DDPAL *DdPalFromIface(void *iface);
HRESULT DdCreatePalette(DDRAW *dd, DWORD flags, const PALETTEENTRY *entries, DDPAL **out);
void DdPalKill(DDPAL *p);

struct DDCLIP {
    IDirectDrawClipper iface;
    LONG ref;
    DWORD magic;
    DDRAW *dd;        /* null for one of DirectDrawCreateClipper */
    BOOL initialized;
    HWND window;
    RGNDATA *list;    /* a clip list set, when there is no window */
    DWORD version;    /* of the object that made it, 0 for none */
    void *owner;      /* the IDirectDraw4 or 7 that made it, which it holds */
    LIST_ENTRY links;
};

#define CLIP_MAGIC 0x50494C43

DDCLIP *DdClipFromIface(void *iface);
HRESULT DdCreateClipper(DDRAW *dd, DWORD flags, DDCLIP **out, BOOL initialized);
void DdClipKill(DDCLIP *c);
/* the clip rectangles in screen coordinates, or null for none; free with HeapFree. */
RGNDATA *DdClipRegion(DDCLIP *c, const RECT *bounds);

/* present.c: the primary surface reaching the screen. */
void DdPresentDirty(DDSURF *primary, const RECT *rect);
void DdPresentFlip(DDSURF *primary);
void DdPresentStop(DDRAW *dd);

/* x86/: direct3d 1 to 7, which only 32-bit windows has. */
#ifdef _WIN64
static inline void DdSurfSync(DDSURF *s) {}
static inline void D3dSurfFree(DDSURF *s) {}
static inline void D3dSurfDepthFill(DDSURF *s, const RECT *rect, DWORD value) {}
#else
/* the memory current before the cpu reads or writes it, direct3d's drawing read back. */
void DdSurfSync(DDSURF *s);
void D3dSurfFree(DDSURF *s);
/* a depth fill reaches the depth buffer direct3d draws with. */
void D3dSurfDepthFill(DDSURF *s, const RECT *rect, DWORD value);
HRESULT D3dObjQuery(DDRAW *dd, DWORD version, REFIID riid, void **out);
void D3dObjDestroy(DDRAW *dd);
#endif

/* caps.c */
HRESULT DdGetCaps(DDRAW *dd, DDCAPS *driver, DDCAPS *emulation);
DWORD DdFourCCCount(void);
HRESULT DdGetVidMem(DDRAW *dd, DWORD caps, DWORD *total, DWORD *free_bytes);

#endif
