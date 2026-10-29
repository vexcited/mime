/* common controls of comctl32.dll version 6. property sheets come through prsht.h. */
#ifndef MIME_WIN32_COMMCTRL_H
#define MIME_WIN32_COMMCTRL_H

#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/objbase.h>
#include <mime/win32/prsht.h>

#ifndef WINCOMMCTRLAPI
#define WINCOMMCTRLAPI DECLSPEC_IMPORT
#endif

struct IStream;

/* ------------------------------------------------------------------ initialization */

typedef struct tagINITCOMMONCONTROLSEX {
    DWORD dwSize;
    DWORD dwICC;
} INITCOMMONCONTROLSEX, *LPINITCOMMONCONTROLSEX;

#define ICC_LISTVIEW_CLASSES 0x00000001
#define ICC_TREEVIEW_CLASSES 0x00000002
#define ICC_BAR_CLASSES 0x00000004
#define ICC_TAB_CLASSES 0x00000008
#define ICC_UPDOWN_CLASS 0x00000010
#define ICC_PROGRESS_CLASS 0x00000020
#define ICC_HOTKEY_CLASS 0x00000040
#define ICC_ANIMATE_CLASS 0x00000080
#define ICC_WIN95_CLASSES 0x000000FF
#define ICC_DATE_CLASSES 0x00000100
#define ICC_USEREX_CLASSES 0x00000200
#define ICC_COOL_CLASSES 0x00000400
#define ICC_INTERNET_CLASSES 0x00000800
#define ICC_PAGESCROLLER_CLASS 0x00001000
#define ICC_NATIVEFNTCTL_CLASS 0x00002000
#define ICC_STANDARD_CLASSES 0x00004000
#define ICC_LINK_CLASS 0x00008000

typedef LRESULT(CALLBACK *SUBCLASSPROC)(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id,
                                        DWORD_PTR ref_data);

typedef struct _DLLVERSIONINFO {
    DWORD cbSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformID;
} DLLVERSIONINFO;

typedef struct _DLLVERSIONINFO2 {
    DLLVERSIONINFO info1;
    DWORD dwFlags;
    ULONGLONG ullVersion;
} DLLVERSIONINFO2;

#define DLLVER_PLATFORM_WINDOWS 0x00000001
#define DLLVER_PLATFORM_NT 0x00000002

WINCOMMCTRLAPI void WINAPI InitCommonControls(void);
WINCOMMCTRLAPI BOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX *init);
WINCOMMCTRLAPI BOOL WINAPI SetWindowSubclass(HWND hwnd, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR ref_data);
WINCOMMCTRLAPI BOOL WINAPI GetWindowSubclass(HWND hwnd, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR *ref_data);
WINCOMMCTRLAPI BOOL WINAPI RemoveWindowSubclass(HWND hwnd, SUBCLASSPROC proc, UINT_PTR id);
WINCOMMCTRLAPI LRESULT WINAPI DefSubclassProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

/* ------------------------------------------------------------------ shared */

#define CLR_NONE 0xFFFFFFFFL
#define CLR_DEFAULT 0xFF000000L
#define CLR_INVALID 0xFFFFFFFF
#define CLR_HILIGHT CLR_DEFAULT

#define LPSTR_TEXTCALLBACKW ((LPWSTR)(LONG_PTR)-1)
#define LPSTR_TEXTCALLBACKA ((LPSTR)(LONG_PTR)-1)
#define LPSTR_TEXTCALLBACK LPSTR_TEXTCALLBACKW
#define I_IMAGECALLBACK (-1)
#define I_IMAGENONE (-2)
#define I_INDENTCALLBACK (-1)
#define I_CHILDRENCALLBACK (-1)

#define CCM_FIRST 0x2000
#define CCM_SETBKCOLOR (CCM_FIRST + 1)
#define CCM_SETCOLORSCHEME (CCM_FIRST + 2)
#define CCM_GETCOLORSCHEME (CCM_FIRST + 3)
#define CCM_GETDROPTARGET (CCM_FIRST + 4)
#define CCM_SETUNICODEFORMAT (CCM_FIRST + 5)
#define CCM_GETUNICODEFORMAT (CCM_FIRST + 6)
#define CCM_SETVERSION (CCM_FIRST + 7)
#define CCM_GETVERSION (CCM_FIRST + 8)
#define CCM_SETNOTIFYWINDOW (CCM_FIRST + 9)
#define CCM_SETWINDOWTHEME (CCM_FIRST + 11)
#define CCM_DPISCALE (CCM_FIRST + 12)

#define CCS_TOP 0x00000001L
#define CCS_NOMOVEY 0x00000002L
#define CCS_BOTTOM 0x00000003L
#define CCS_NORESIZE 0x00000004L
#define CCS_NOPARENTALIGN 0x00000008L
#define CCS_ADJUSTABLE 0x00000020L
#define CCS_NODIVIDER 0x00000040L
#define CCS_VERT 0x00000080L
#define CCS_LEFT (CCS_VERT | CCS_TOP)
#define CCS_RIGHT (CCS_VERT | CCS_BOTTOM)
#define CCS_NOMOVEX (CCS_VERT | CCS_NOMOVEY)

/* notification codes shared by the controls. */
#define NM_FIRST 0U
#define NM_OUTOFMEMORY (NM_FIRST - 1)
#define NM_CLICK (NM_FIRST - 2)
#define NM_DBLCLK (NM_FIRST - 3)
#define NM_RETURN (NM_FIRST - 4)
#define NM_RCLICK (NM_FIRST - 5)
#define NM_RDBLCLK (NM_FIRST - 6)
#define NM_SETFOCUS (NM_FIRST - 7)
#define NM_KILLFOCUS (NM_FIRST - 8)
#define NM_CUSTOMDRAW (NM_FIRST - 12)
#define NM_HOVER (NM_FIRST - 13)
#define NM_NCHITTEST (NM_FIRST - 14)
#define NM_KEYDOWN (NM_FIRST - 15)
#define NM_RELEASEDCAPTURE (NM_FIRST - 16)
#define NM_SETCURSOR (NM_FIRST - 17)
#define NM_CHAR (NM_FIRST - 18)
#define NM_TOOLTIPSCREATED (NM_FIRST - 19)
#define NM_LDOWN (NM_FIRST - 20)
#define NM_RDOWN (NM_FIRST - 21)
#define NM_THEMECHANGED (NM_FIRST - 22)
#define NM_LAST (NM_FIRST - 99)

#define LVN_FIRST (0U - 100U)
#define HDN_FIRST (0U - 300U)
#define TVN_FIRST (0U - 400U)
#define TTN_FIRST (0U - 520U)
#define TCN_FIRST (0U - 550U)
#define TBN_FIRST (0U - 700U)
#define UDN_FIRST (0U - 721U)
#define SBN_FIRST (0U - 880U)
#define TRBN_FIRST (0U - 1501U)

typedef struct tagNMMOUSE {
    NMHDR hdr;
    DWORD_PTR dwItemSpec;
    DWORD_PTR dwItemData;
    POINT pt;
    LPARAM dwHitInfo;
} NMMOUSE, *LPNMMOUSE;

typedef NMMOUSE NMCLICK, *LPNMCLICK;

typedef struct tagNMKEY {
    NMHDR hdr;
    UINT nVKey;
    UINT uFlags;
} NMKEY, *LPNMKEY;

typedef struct tagNMCHAR {
    NMHDR hdr;
    UINT ch;
    DWORD dwItemPrev;
    DWORD dwItemNext;
} NMCHAR, *LPNMCHAR;

#define ODT_HEADER 100
#define ODT_TAB 101
#define ODT_LISTVIEW 102

/* custom draw. */
#define CDRF_DODEFAULT 0x00000000
#define CDRF_NEWFONT 0x00000002
#define CDRF_SKIPDEFAULT 0x00000004
#define CDRF_DOERASE 0x00000008
#define CDRF_SKIPPOSTPAINT 0x00000100
#define CDRF_NOTIFYPOSTPAINT 0x00000010
#define CDRF_NOTIFYITEMDRAW 0x00000020
#define CDRF_NOTIFYSUBITEMDRAW 0x00000020
#define CDRF_NOTIFYPOSTERASE 0x00000040
#define CDDS_PREPAINT 0x00000001
#define CDDS_POSTPAINT 0x00000002
#define CDDS_PREERASE 0x00000003
#define CDDS_POSTERASE 0x00000004
#define CDDS_ITEM 0x00010000
#define CDDS_ITEMPREPAINT (CDDS_ITEM | CDDS_PREPAINT)
#define CDDS_ITEMPOSTPAINT (CDDS_ITEM | CDDS_POSTPAINT)
#define CDDS_ITEMPREERASE (CDDS_ITEM | CDDS_PREERASE)
#define CDDS_ITEMPOSTERASE (CDDS_ITEM | CDDS_POSTERASE)
#define CDDS_SUBITEM 0x00020000
#define CDIS_SELECTED 0x0001
#define CDIS_GRAYED 0x0002
#define CDIS_DISABLED 0x0004
#define CDIS_CHECKED 0x0008
#define CDIS_FOCUS 0x0010
#define CDIS_DEFAULT 0x0020
#define CDIS_HOT 0x0040
#define CDIS_MARKED 0x0080
#define CDIS_INDETERMINATE 0x0100
#define CDIS_SHOWKEYBOARDCUES 0x0200
#define CDIS_NEARHOT 0x0400
#define CDIS_OTHERSIDEHOT 0x0800
#define CDIS_DROPHILITED 0x1000

typedef struct tagNMCUSTOMDRAWINFO {
    NMHDR hdr;
    DWORD dwDrawStage;
    HDC hdc;
    RECT rc;
    DWORD_PTR dwItemSpec;
    UINT uItemState;
    LPARAM lItemlParam;
} NMCUSTOMDRAW, *LPNMCUSTOMDRAW;

/* ------------------------------------------------------------------ image lists */

struct _IMAGELIST;
typedef struct _IMAGELIST *HIMAGELIST;

#define ILC_MASK 0x00000001
#define ILC_COLOR 0x00000000
#define ILC_COLORDDB 0x000000FE
#define ILC_COLOR4 0x00000004
#define ILC_COLOR8 0x00000008
#define ILC_COLOR16 0x00000010
#define ILC_COLOR24 0x00000018
#define ILC_COLOR32 0x00000020
#define ILC_PALETTE 0x00000800
#define ILC_MIRROR 0x00002000
#define ILC_PERITEMMIRROR 0x00008000
#define ILC_ORIGINALSIZE 0x00010000
#define ILC_HIGHQUALITYSCALE 0x00020000

#define ILD_NORMAL 0x00000000
#define ILD_TRANSPARENT 0x00000001
#define ILD_MASK 0x00000010
#define ILD_IMAGE 0x00000020
#define ILD_ROP 0x00000040
#define ILD_BLEND25 0x00000002
#define ILD_BLEND50 0x00000004
#define ILD_OVERLAYMASK 0x00000F00
#define INDEXTOOVERLAYMASK(i) ((i) << 8)
#define ILD_PRESERVEALPHA 0x00001000
#define ILD_SCALE 0x00002000
#define ILD_DPISCALE 0x00004000
#define ILD_ASYNC 0x00008000
#define ILD_SELECTED ILD_BLEND50
#define ILD_FOCUS ILD_BLEND25
#define ILD_BLEND ILD_BLEND50

#define ILS_NORMAL 0x00000000
#define ILS_GLOW 0x00000001
#define ILS_SHADOW 0x00000002
#define ILS_SATURATE 0x00000004
#define ILS_ALPHA 0x00000008

#define ILCF_MOVE 0x00000000
#define ILCF_SWAP 0x00000001

#define ILP_NORMAL 0
#define ILP_DOWNLEVEL 1

typedef struct _IMAGEINFO {
    HBITMAP hbmImage;
    HBITMAP hbmMask;
    int Unused1;
    int Unused2;
    RECT rcImage;
} IMAGEINFO, *LPIMAGEINFO;

typedef struct _IMAGELISTDRAWPARAMS {
    DWORD cbSize;
    HIMAGELIST himl;
    int i;
    HDC hdcDst;
    int x;
    int y;
    int cx;
    int cy;
    int xBitmap;
    int yBitmap;
    COLORREF rgbBk;
    COLORREF rgbFg;
    UINT fStyle;
    DWORD dwRop;
    DWORD fState;
    DWORD Frame;
    COLORREF crEffect;
} IMAGELISTDRAWPARAMS, *LPIMAGELISTDRAWPARAMS;

#define IMAGELISTDRAWPARAMS_V3_SIZE ((UINT)(ULONG_PTR) & ((IMAGELISTDRAWPARAMS *)0)->fState)

WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_Create(int cx, int cy, UINT flags, int initial, int grow);
WINCOMMCTRLAPI BOOL WINAPI ImageList_Destroy(HIMAGELIST himl);
WINCOMMCTRLAPI int WINAPI ImageList_GetImageCount(HIMAGELIST himl);
WINCOMMCTRLAPI BOOL WINAPI ImageList_SetImageCount(HIMAGELIST himl, UINT count);
WINCOMMCTRLAPI int WINAPI ImageList_Add(HIMAGELIST himl, HBITMAP image, HBITMAP mask);

typedef struct _COLORMAP {
    COLORREF from;
    COLORREF to;
} COLORMAP, *LPCOLORMAP;

#define CMB_MASKED 0x02
#define CMB_DIBSECTION 0x04

WINCOMMCTRLAPI HBITMAP WINAPI CreateMappedBitmap(HINSTANCE instance, INT_PTR id, UINT flags, LPCOLORMAP map, int count);
WINCOMMCTRLAPI BOOL WINAPI InitializeFlatSB(HWND hwnd);
WINCOMMCTRLAPI HRESULT WINAPI UninitializeFlatSB(HWND hwnd);
WINCOMMCTRLAPI BOOL WINAPI FlatSB_GetScrollProp(HWND hwnd, int property, LPINT value);
WINCOMMCTRLAPI BOOL WINAPI FlatSB_SetScrollProp(HWND hwnd, UINT property, INT_PTR value, BOOL redraw);
WINCOMMCTRLAPI BOOL WINAPI FlatSB_GetScrollPropPtr(HWND hwnd, int property, INT_PTR *value);
WINCOMMCTRLAPI BOOL WINAPI FlatSB_EnableScrollBar(HWND hwnd, int bar, UINT arrows);
WINCOMMCTRLAPI BOOL WINAPI FlatSB_ShowScrollBar(HWND hwnd, int bar, BOOL show);
WINCOMMCTRLAPI BOOL WINAPI FlatSB_GetScrollRange(HWND hwnd, int bar, LPINT min, LPINT max);
WINCOMMCTRLAPI BOOL WINAPI FlatSB_GetScrollInfo(HWND hwnd, int bar, LPSCROLLINFO info);
WINCOMMCTRLAPI int WINAPI FlatSB_GetScrollPos(HWND hwnd, int bar);
WINCOMMCTRLAPI int WINAPI FlatSB_SetScrollPos(HWND hwnd, int bar, int pos, BOOL redraw);
WINCOMMCTRLAPI int WINAPI FlatSB_SetScrollInfo(HWND hwnd, int bar, LPSCROLLINFO info, BOOL redraw);
WINCOMMCTRLAPI int WINAPI FlatSB_SetScrollRange(HWND hwnd, int bar, int min, int max, BOOL redraw);
WINCOMMCTRLAPI int WINAPI ImageList_ReplaceIcon(HIMAGELIST himl, int i, HICON icon);
WINCOMMCTRLAPI COLORREF WINAPI ImageList_SetBkColor(HIMAGELIST himl, COLORREF color);
WINCOMMCTRLAPI COLORREF WINAPI ImageList_GetBkColor(HIMAGELIST himl);
WINCOMMCTRLAPI BOOL WINAPI ImageList_SetOverlayImage(HIMAGELIST himl, int image, int overlay);
WINCOMMCTRLAPI BOOL WINAPI ImageList_Draw(HIMAGELIST himl, int i, HDC dc, int x, int y, UINT style);
WINCOMMCTRLAPI BOOL WINAPI ImageList_Replace(HIMAGELIST himl, int i, HBITMAP image, HBITMAP mask);
WINCOMMCTRLAPI int WINAPI ImageList_AddMasked(HIMAGELIST himl, HBITMAP image, COLORREF mask);
WINCOMMCTRLAPI BOOL WINAPI ImageList_DrawEx(HIMAGELIST himl, int i, HDC dc, int x, int y, int dx, int dy,
                                            COLORREF bk, COLORREF fg, UINT style);
WINCOMMCTRLAPI BOOL WINAPI ImageList_DrawIndirect(IMAGELISTDRAWPARAMS *params);
WINCOMMCTRLAPI BOOL WINAPI ImageList_Remove(HIMAGELIST himl, int i);
WINCOMMCTRLAPI HICON WINAPI ImageList_GetIcon(HIMAGELIST himl, int i, UINT flags);
WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_LoadImageA(HINSTANCE inst, LPCSTR name, int cx, int grow, COLORREF mask,
                                                      UINT type, UINT flags);
WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_LoadImageW(HINSTANCE inst, LPCWSTR name, int cx, int grow, COLORREF mask,
                                                      UINT type, UINT flags);
WINCOMMCTRLAPI BOOL WINAPI ImageList_Copy(HIMAGELIST dst, int i_dst, HIMAGELIST src, int i_src, UINT flags);
WINCOMMCTRLAPI BOOL WINAPI ImageList_BeginDrag(HIMAGELIST track, int i, int hot_x, int hot_y);
WINCOMMCTRLAPI void WINAPI ImageList_EndDrag(void);
WINCOMMCTRLAPI BOOL WINAPI ImageList_DragEnter(HWND lock, int x, int y);
WINCOMMCTRLAPI BOOL WINAPI ImageList_DragLeave(HWND lock);
WINCOMMCTRLAPI BOOL WINAPI ImageList_DragMove(int x, int y);
WINCOMMCTRLAPI BOOL WINAPI ImageList_SetDragCursorImage(HIMAGELIST drag, int i, int hot_x, int hot_y);
WINCOMMCTRLAPI BOOL WINAPI ImageList_DragShowNolock(BOOL show);
WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_GetDragImage(POINT *pt, POINT *hot);
WINCOMMCTRLAPI BOOL WINAPI ImageList_GetIconSize(HIMAGELIST himl, int *cx, int *cy);
WINCOMMCTRLAPI BOOL WINAPI ImageList_SetIconSize(HIMAGELIST himl, int cx, int cy);
WINCOMMCTRLAPI BOOL WINAPI ImageList_GetImageInfo(HIMAGELIST himl, int i, IMAGEINFO *info);
WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_Merge(HIMAGELIST himl1, int i1, HIMAGELIST himl2, int i2, int dx, int dy);
WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_Duplicate(HIMAGELIST himl);
WINCOMMCTRLAPI DWORD WINAPI ImageList_GetFlags(HIMAGELIST himl);
WINCOMMCTRLAPI BOOL WINAPI ImageList_SetFlags(HIMAGELIST himl, DWORD flags);
WINCOMMCTRLAPI HIMAGELIST WINAPI ImageList_Read(struct IStream *stream);
WINCOMMCTRLAPI BOOL WINAPI ImageList_Write(HIMAGELIST himl, struct IStream *stream);
WINCOMMCTRLAPI HRESULT WINAPI ImageList_ReadEx(DWORD flags, struct IStream *stream, REFIID riid, void **object);
WINCOMMCTRLAPI HRESULT WINAPI ImageList_WriteEx(HIMAGELIST himl, DWORD flags, struct IStream *stream);
WINCOMMCTRLAPI HRESULT WINAPI ImageList_CoCreateInstance(REFCLSID clsid, const IUnknown *outer, REFIID riid,
                                                         void **object);
WINCOMMCTRLAPI HRESULT WINAPI HIMAGELIST_QueryInterface(HIMAGELIST himl, REFIID riid, void **object);

/*
 * the com side of image lists, from commoncontrols.h. an HIMAGELIST points at its list's IImageList2, so programs
 * cast one to the other.
 */
MIME_DEFINE_GUID(IID_IImageList, 0x46EB5926, 0x582E, 0x4017, 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50);
MIME_DEFINE_GUID(IID_IImageList2, 0x192B9D83, 0x50FC, 0x457B, 0x90, 0xA0, 0x2B, 0x82, 0xA8, 0xB5, 0xDA, 0xE1);
MIME_DEFINE_GUID(CLSID_ImageList, 0x7C476BA2, 0x02B1, 0x48F4, 0x80, 0x48, 0xB2, 0x46, 0x19, 0xDD, 0xC0, 0x58);

#define ILIF_ALPHA 0x00000001
#define ILIF_LOWQUALITY 0x00000002
#define ILDRF_IMAGELOWQUALITY 0x00000001
#define ILDRF_OVERLAYLOWQUALITY 0x00000010
#define ILR_DEFAULT 0x0000
#define ILR_HORIZONTAL_LEFT 0x0000
#define ILR_HORIZONTAL_CENTER 0x0001
#define ILR_HORIZONTAL_RIGHT 0x0002
#define ILR_VERTICAL_TOP 0x0000
#define ILR_VERTICAL_CENTER 0x0010
#define ILR_VERTICAL_BOTTOM 0x0020
#define ILR_SCALE_CLIP 0x0000
#define ILR_SCALE_ASPECTRATIO 0x0100
#define ILGOS_ALWAYS 0x00000000
#define ILGOS_FROMSTANDBY 0x00000001
#define ILFIP_ALWAYS 0x00000000
#define ILFIP_FROMSTANDBY 0x00000001
#define ILDI_PURGE 0x00000001
#define ILDI_STANDBY 0x00000002
#define ILDI_RESETACCESS 0x00000004
#define ILDI_QUERYACCESS 0x00000008

typedef struct tagIMAGELISTSTATS {
    DWORD cbSize;
    int cAlloc;
    int cUsed;
    int cStandby;
} IMAGELISTSTATS;

typedef struct IImageList IImageList;
typedef struct IImageList2 IImageList2;

#define MIME_IIMAGELIST_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(T *This, REFIID riid, void **object);                                  \
    ULONG(STDMETHODCALLTYPE *AddRef)(T *This);                                                                        \
    ULONG(STDMETHODCALLTYPE *Release)(T *This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Add)(T *This, HBITMAP image, HBITMAP mask, int *index);                                \
    HRESULT(STDMETHODCALLTYPE *ReplaceIcon)(T *This, int i, HICON icon, int *index);                                  \
    HRESULT(STDMETHODCALLTYPE *SetOverlayImage)(T *This, int image, int overlay);                                     \
    HRESULT(STDMETHODCALLTYPE *Replace)(T *This, int i, HBITMAP image, HBITMAP mask);                                 \
    HRESULT(STDMETHODCALLTYPE *AddMasked)(T *This, HBITMAP image, COLORREF key, int *index);                          \
    HRESULT(STDMETHODCALLTYPE *Draw)(T *This, IMAGELISTDRAWPARAMS *params);                                           \
    HRESULT(STDMETHODCALLTYPE *Remove)(T *This, int i);                                                               \
    HRESULT(STDMETHODCALLTYPE *GetIcon)(T *This, int i, UINT flags, HICON *icon);                                     \
    HRESULT(STDMETHODCALLTYPE *GetImageInfo)(T *This, int i, IMAGEINFO *info);                                        \
    HRESULT(STDMETHODCALLTYPE *Copy)(T *This, int dst, IUnknown *source, int src, UINT flags);                        \
    HRESULT(STDMETHODCALLTYPE *Merge)(T *This, int i1, IUnknown *list2, int i2, int dx, int dy, REFIID riid,          \
                                      void **object);                                                                 \
    HRESULT(STDMETHODCALLTYPE *Clone)(T *This, REFIID riid, void **object);                                           \
    HRESULT(STDMETHODCALLTYPE *GetImageRect)(T *This, int i, RECT *rect);                                             \
    HRESULT(STDMETHODCALLTYPE *GetIconSize)(T *This, int *cx, int *cy);                                               \
    HRESULT(STDMETHODCALLTYPE *SetIconSize)(T *This, int cx, int cy);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetImageCount)(T *This, int *count);                                                   \
    HRESULT(STDMETHODCALLTYPE *SetImageCount)(T *This, UINT count);                                                   \
    HRESULT(STDMETHODCALLTYPE *SetBkColor)(T *This, COLORREF color, COLORREF *old);                                   \
    HRESULT(STDMETHODCALLTYPE *GetBkColor)(T *This, COLORREF *color);                                                 \
    HRESULT(STDMETHODCALLTYPE *BeginDrag)(T *This, int track, int hot_x, int hot_y);                                  \
    HRESULT(STDMETHODCALLTYPE *EndDrag)(T *This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *DragEnter)(T *This, HWND lock, int x, int y);                                          \
    HRESULT(STDMETHODCALLTYPE *DragLeave)(T *This, HWND lock);                                                        \
    HRESULT(STDMETHODCALLTYPE *DragMove)(T *This, int x, int y);                                                      \
    HRESULT(STDMETHODCALLTYPE *SetDragCursorImage)(T *This, IUnknown *drag, int i, int hot_x, int hot_y);             \
    HRESULT(STDMETHODCALLTYPE *DragShowNolock)(T *This, BOOL show);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetDragImage)(T *This, POINT *pt, POINT *hot, REFIID riid, void **object);             \
    HRESULT(STDMETHODCALLTYPE *GetItemFlags)(T *This, int i, DWORD *flags);                                           \
    HRESULT(STDMETHODCALLTYPE *GetOverlayImage)(T *This, int overlay, int *index);

typedef struct IImageListVtbl {
    MIME_IIMAGELIST_METHODS(IImageList)
} IImageListVtbl;
struct IImageList {
    const IImageListVtbl *lpVtbl;
};

typedef struct IImageList2Vtbl {
    MIME_IIMAGELIST_METHODS(IImageList2)
    HRESULT(STDMETHODCALLTYPE *Resize)(IImageList2 *This, int cx, int cy);
    HRESULT(STDMETHODCALLTYPE *GetOriginalSize)(IImageList2 *This, int i, DWORD flags, int *cx, int *cy);
    HRESULT(STDMETHODCALLTYPE *SetOriginalSize)(IImageList2 *This, int i, int cx, int cy);
    HRESULT(STDMETHODCALLTYPE *SetCallback)(IImageList2 *This, IUnknown *callback);
    HRESULT(STDMETHODCALLTYPE *GetCallback)(IImageList2 *This, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *ForceImagePresent)(IImageList2 *This, int i, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *DiscardImages)(IImageList2 *This, int first, int last, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *PreloadImages)(IImageList2 *This, IMAGELISTDRAWPARAMS *params);
    HRESULT(STDMETHODCALLTYPE *GetStatistics)(IImageList2 *This, IMAGELISTSTATS *stats);
    HRESULT(STDMETHODCALLTYPE *Initialize)(IImageList2 *This, int cx, int cy, UINT flags, int initial, int grow);
    HRESULT(STDMETHODCALLTYPE *Replace2)(IImageList2 *This, int i, HBITMAP image, HBITMAP mask, IUnknown *decorator,
                                         DWORD flags);
    HRESULT(STDMETHODCALLTYPE *ReplaceFromImageList)(IImageList2 *This, int i, IImageList *source, int src,
                                                     IUnknown *decorator, DWORD flags);
} IImageList2Vtbl;
struct IImageList2 {
    const IImageList2Vtbl *lpVtbl;
};

#define ImageList_AddIcon(himl, icon) ImageList_ReplaceIcon(himl, -1, icon)
#define ImageList_RemoveAll(himl) ImageList_Remove(himl, -1)
#define ImageList_ExtractIcon(inst, himl, i) ImageList_GetIcon(himl, i, 0)
#define ImageList_LoadBitmap(inst, name, cx, grow, mask)                                                            \
    ImageList_LoadImageW(inst, name, cx, grow, mask, IMAGE_BITMAP, 0)

/* ------------------------------------------------------------------ header */

#define WC_HEADERA "SysHeader32"
#define WC_HEADERW L"SysHeader32"
#define WC_HEADER WC_HEADERW

#define HDS_HORZ 0x0000
#define HDS_BUTTONS 0x0002
#define HDS_HOTTRACK 0x0004
#define HDS_HIDDEN 0x0008
#define HDS_DRAGDROP 0x0040
#define HDS_FULLDRAG 0x0080
#define HDS_FILTERBAR 0x0100
#define HDS_FLAT 0x0200
#define HDS_CHECKBOXES 0x0400
#define HDS_NOSIZING 0x0800
#define HDS_OVERFLOW 0x1000

#define HDFT_ISSTRING 0x0000
#define HDFT_ISNUMBER 0x0001
#define HDFT_ISDATE 0x0002
#define HDFT_HASNOVALUE 0x8000

typedef struct _HD_TEXTFILTERA {
    LPSTR pszText;
    INT cchTextMax;
} HD_TEXTFILTERA, *LPHD_TEXTFILTERA;

typedef struct _HD_TEXTFILTERW {
    LPWSTR pszText;
    INT cchTextMax;
} HD_TEXTFILTERW, *LPHD_TEXTFILTERW;

typedef struct _HD_ITEMA {
    UINT mask;
    int cxy;
    LPSTR pszText;
    HBITMAP hbm;
    int cchTextMax;
    int fmt;
    LPARAM lParam;
    int iImage;
    int iOrder;
    UINT type;
    void *pvFilter;
    UINT state;
} HDITEMA, *LPHDITEMA;

typedef struct _HD_ITEMW {
    UINT mask;
    int cxy;
    LPWSTR pszText;
    HBITMAP hbm;
    int cchTextMax;
    int fmt;
    LPARAM lParam;
    int iImage;
    int iOrder;
    UINT type;
    void *pvFilter;
    UINT state;
} HDITEMW, *LPHDITEMW;

typedef HDITEMW HDITEM;
typedef LPHDITEMW LPHDITEM;
#define HDITEMA_V1_SIZE ((UINT)(ULONG_PTR) & ((HDITEMA *)0)->iImage)
#define HDITEMW_V1_SIZE ((UINT)(ULONG_PTR) & ((HDITEMW *)0)->iImage)

#define HDI_WIDTH 0x0001
#define HDI_HEIGHT HDI_WIDTH
#define HDI_TEXT 0x0002
#define HDI_FORMAT 0x0004
#define HDI_LPARAM 0x0008
#define HDI_BITMAP 0x0010
#define HDI_IMAGE 0x0020
#define HDI_DI_SETITEM 0x0040
#define HDI_ORDER 0x0080
#define HDI_FILTER 0x0100
#define HDI_STATE 0x0200

#define HDIS_FOCUSED 0x00000001

#define HDF_LEFT 0x0000
#define HDF_RIGHT 0x0001
#define HDF_CENTER 0x0002
#define HDF_JUSTIFYMASK 0x0003
#define HDF_RTLREADING 0x0004
#define HDF_BITMAP 0x2000
#define HDF_STRING 0x4000
#define HDF_OWNERDRAW 0x8000
#define HDF_IMAGE 0x0800
#define HDF_BITMAP_ON_RIGHT 0x1000
#define HDF_SORTUP 0x0400
#define HDF_SORTDOWN 0x0200
#define HDF_CHECKBOX 0x0040
#define HDF_CHECKED 0x0080
#define HDF_FIXEDWIDTH 0x0100
#define HDF_SPLITBUTTON 0x1000000

typedef struct _HD_LAYOUT {
    RECT *prc;
    WINDOWPOS *pwpos;
} HDLAYOUT, *LPHDLAYOUT;

#define HHT_NOWHERE 0x0001
#define HHT_ONHEADER 0x0002
#define HHT_ONDIVIDER 0x0004
#define HHT_ONDIVOPEN 0x0008
#define HHT_ONFILTER 0x0010
#define HHT_ONFILTERBUTTON 0x0020
#define HHT_ABOVE 0x0100
#define HHT_BELOW 0x0200
#define HHT_TORIGHT 0x0400
#define HHT_TOLEFT 0x0800
#define HHT_ONITEMSTATEICON 0x1000
#define HHT_ONDROPDOWN 0x2000
#define HHT_ONOVERFLOW 0x4000

typedef struct _HD_HITTESTINFO {
    POINT pt;
    UINT flags;
    int iItem;
} HDHITTESTINFO, *LPHDHITTESTINFO;

#define HDSIL_NORMAL 0
#define HDSIL_STATE 1

#define HDM_FIRST 0x1200
#define HDM_GETITEMCOUNT (HDM_FIRST + 0)
#define HDM_INSERTITEMA (HDM_FIRST + 1)
#define HDM_INSERTITEMW (HDM_FIRST + 10)
#define HDM_INSERTITEM HDM_INSERTITEMW
#define HDM_DELETEITEM (HDM_FIRST + 2)
#define HDM_GETITEMA (HDM_FIRST + 3)
#define HDM_GETITEMW (HDM_FIRST + 11)
#define HDM_GETITEM HDM_GETITEMW
#define HDM_SETITEMA (HDM_FIRST + 4)
#define HDM_SETITEMW (HDM_FIRST + 12)
#define HDM_SETITEM HDM_SETITEMW
#define HDM_LAYOUT (HDM_FIRST + 5)
#define HDM_HITTEST (HDM_FIRST + 6)
#define HDM_GETITEMRECT (HDM_FIRST + 7)
#define HDM_SETIMAGELIST (HDM_FIRST + 8)
#define HDM_GETIMAGELIST (HDM_FIRST + 9)
#define HDM_ORDERTOINDEX (HDM_FIRST + 15)
#define HDM_CREATEDRAGIMAGE (HDM_FIRST + 16)
#define HDM_GETORDERARRAY (HDM_FIRST + 17)
#define HDM_SETORDERARRAY (HDM_FIRST + 18)
#define HDM_SETHOTDIVIDER (HDM_FIRST + 19)
#define HDM_SETBITMAPMARGIN (HDM_FIRST + 20)
#define HDM_GETBITMAPMARGIN (HDM_FIRST + 21)
#define HDM_SETUNICODEFORMAT CCM_SETUNICODEFORMAT
#define HDM_GETUNICODEFORMAT CCM_GETUNICODEFORMAT
#define HDM_SETFILTERCHANGETIMEOUT (HDM_FIRST + 22)
#define HDM_EDITFILTER (HDM_FIRST + 23)
#define HDM_CLEARFILTER (HDM_FIRST + 24)
#define HDM_GETITEMDROPDOWNRECT (HDM_FIRST + 25)
#define HDM_GETOVERFLOWRECT (HDM_FIRST + 26)
#define HDM_GETFOCUSEDITEM (HDM_FIRST + 27)
#define HDM_SETFOCUSEDITEM (HDM_FIRST + 28)

#define HDN_ITEMCHANGINGA (HDN_FIRST - 0)
#define HDN_ITEMCHANGINGW (HDN_FIRST - 20)
#define HDN_ITEMCHANGEDA (HDN_FIRST - 1)
#define HDN_ITEMCHANGEDW (HDN_FIRST - 21)
#define HDN_ITEMCLICKA (HDN_FIRST - 2)
#define HDN_ITEMCLICKW (HDN_FIRST - 22)
#define HDN_ITEMDBLCLICKA (HDN_FIRST - 3)
#define HDN_ITEMDBLCLICKW (HDN_FIRST - 23)
#define HDN_DIVIDERDBLCLICKA (HDN_FIRST - 5)
#define HDN_DIVIDERDBLCLICKW (HDN_FIRST - 25)
#define HDN_BEGINTRACKA (HDN_FIRST - 6)
#define HDN_BEGINTRACKW (HDN_FIRST - 26)
#define HDN_ENDTRACKA (HDN_FIRST - 7)
#define HDN_ENDTRACKW (HDN_FIRST - 27)
#define HDN_TRACKA (HDN_FIRST - 8)
#define HDN_TRACKW (HDN_FIRST - 28)
#define HDN_GETDISPINFOA (HDN_FIRST - 9)
#define HDN_GETDISPINFOW (HDN_FIRST - 29)
#define HDN_BEGINDRAG (HDN_FIRST - 10)
#define HDN_ENDDRAG (HDN_FIRST - 11)
#define HDN_FILTERCHANGE (HDN_FIRST - 12)
#define HDN_FILTERBTNCLICK (HDN_FIRST - 13)
#define HDN_BEGINFILTEREDIT (HDN_FIRST - 14)
#define HDN_ENDFILTEREDIT (HDN_FIRST - 15)
#define HDN_ITEMSTATEICONCLICK (HDN_FIRST - 16)
#define HDN_ITEMKEYDOWN (HDN_FIRST - 17)
#define HDN_DROPDOWN (HDN_FIRST - 18)
#define HDN_OVERFLOWCLICK (HDN_FIRST - 19)
#define HDN_ITEMCHANGING HDN_ITEMCHANGINGW
#define HDN_ITEMCHANGED HDN_ITEMCHANGEDW
#define HDN_ITEMCLICK HDN_ITEMCLICKW
#define HDN_ITEMDBLCLICK HDN_ITEMDBLCLICKW
#define HDN_DIVIDERDBLCLICK HDN_DIVIDERDBLCLICKW
#define HDN_BEGINTRACK HDN_BEGINTRACKW
#define HDN_ENDTRACK HDN_ENDTRACKW
#define HDN_TRACK HDN_TRACKW
#define HDN_GETDISPINFO HDN_GETDISPINFOW

typedef struct tagNMHEADERA {
    NMHDR hdr;
    int iItem;
    int iButton;
    HDITEMA *pitem;
} NMHEADERA, *LPNMHEADERA;

typedef struct tagNMHEADERW {
    NMHDR hdr;
    int iItem;
    int iButton;
    HDITEMW *pitem;
} NMHEADERW, *LPNMHEADERW;

typedef NMHEADERW NMHEADER;
typedef LPNMHEADERW LPNMHEADER;

typedef struct tagNMHDDISPINFOW {
    NMHDR hdr;
    int iItem;
    UINT mask;
    LPWSTR pszText;
    int cchTextMax;
    int iImage;
    LPARAM lParam;
} NMHDDISPINFOW, *LPNMHDDISPINFOW;

typedef struct tagNMHDDISPINFOA {
    NMHDR hdr;
    int iItem;
    UINT mask;
    LPSTR pszText;
    int cchTextMax;
    int iImage;
    LPARAM lParam;
} NMHDDISPINFOA, *LPNMHDDISPINFOA;

typedef struct tagNMHDFILTERBTNCLICK {
    NMHDR hdr;
    INT iItem;
    RECT rc;
} NMHDFILTERBTNCLICK, *LPNMHDFILTERBTNCLICK;

#define Header_GetItemCount(hwnd) (int)SendMessageW((hwnd), HDM_GETITEMCOUNT, 0, 0)
#define Header_InsertItem(hwnd, i, phdi) (int)SendMessageW((hwnd), HDM_INSERTITEMW, (WPARAM)(int)(i), (LPARAM)(phdi))
#define Header_DeleteItem(hwnd, i) (BOOL)SendMessageW((hwnd), HDM_DELETEITEM, (WPARAM)(int)(i), 0)
#define Header_GetItem(hwnd, i, phdi) (BOOL)SendMessageW((hwnd), HDM_GETITEMW, (WPARAM)(int)(i), (LPARAM)(phdi))
#define Header_SetItem(hwnd, i, phdi) (BOOL)SendMessageW((hwnd), HDM_SETITEMW, (WPARAM)(int)(i), (LPARAM)(phdi))
#define Header_Layout(hwnd, playout) (BOOL)SendMessageW((hwnd), HDM_LAYOUT, 0, (LPARAM)(playout))

/* ------------------------------------------------------------------ list view */

#define WC_LISTVIEWA "SysListView32"
#define WC_LISTVIEWW L"SysListView32"
#define WC_LISTVIEW WC_LISTVIEWW

#define LVS_ICON 0x0000
#define LVS_REPORT 0x0001
#define LVS_SMALLICON 0x0002
#define LVS_LIST 0x0003
#define LVS_TYPEMASK 0x0003
#define LVS_SINGLESEL 0x0004
#define LVS_SHOWSELALWAYS 0x0008
#define LVS_SORTASCENDING 0x0010
#define LVS_SORTDESCENDING 0x0020
#define LVS_SHAREIMAGELISTS 0x0040
#define LVS_NOLABELWRAP 0x0080
#define LVS_AUTOARRANGE 0x0100
#define LVS_EDITLABELS 0x0200
#define LVS_OWNERDATA 0x1000
#define LVS_NOSCROLL 0x2000
#define LVS_TYPESTYLEMASK 0xfc00
#define LVS_ALIGNTOP 0x0000
#define LVS_ALIGNLEFT 0x0800
#define LVS_ALIGNMASK 0x0c00
#define LVS_OWNERDRAWFIXED 0x0400
#define LVS_NOCOLUMNHEADER 0x4000
#define LVS_NOSORTHEADER 0x8000

#define LVS_EX_GRIDLINES 0x00000001
#define LVS_EX_SUBITEMIMAGES 0x00000002
#define LVS_EX_CHECKBOXES 0x00000004
#define LVS_EX_TRACKSELECT 0x00000008
#define LVS_EX_HEADERDRAGDROP 0x00000010
#define LVS_EX_FULLROWSELECT 0x00000020
#define LVS_EX_ONECLICKACTIVATE 0x00000040
#define LVS_EX_TWOCLICKACTIVATE 0x00000080
#define LVS_EX_FLATSB 0x00000100
#define LVS_EX_REGIONAL 0x00000200
#define LVS_EX_INFOTIP 0x00000400
#define LVS_EX_UNDERLINEHOT 0x00000800
#define LVS_EX_UNDERLINECOLD 0x00001000
#define LVS_EX_MULTIWORKAREAS 0x00002000
#define LVS_EX_LABELTIP 0x00004000
#define LVS_EX_BORDERSELECT 0x00008000
#define LVS_EX_DOUBLEBUFFER 0x00010000
#define LVS_EX_HIDELABELS 0x00020000
#define LVS_EX_SINGLEROW 0x00040000
#define LVS_EX_SNAPTOGRID 0x00080000
#define LVS_EX_SIMPLESELECT 0x00100000
#define LVS_EX_JUSTIFYCOLUMNS 0x00200000
#define LVS_EX_TRANSPARENTBKGND 0x00400000
#define LVS_EX_TRANSPARENTSHADOWTEXT 0x00800000
#define LVS_EX_AUTOAUTOARRANGE 0x01000000
#define LVS_EX_HEADERINALLVIEWS 0x02000000
#define LVS_EX_AUTOCHECKSELECT 0x08000000
#define LVS_EX_AUTOSIZECOLUMNS 0x10000000
#define LVS_EX_COLUMNSNAPPOINTS 0x40000000
#define LVS_EX_COLUMNOVERFLOW 0x80000000

#define LVSIL_NORMAL 0
#define LVSIL_SMALL 1
#define LVSIL_STATE 2
#define LVSIL_GROUPHEADER 3

#define LVIF_TEXT 0x00000001
#define LVIF_IMAGE 0x00000002
#define LVIF_PARAM 0x00000004
#define LVIF_STATE 0x00000008
#define LVIF_INDENT 0x00000010
#define LVIF_NORECOMPUTE 0x00000800
#define LVIF_GROUPID 0x00000100
#define LVIF_COLUMNS 0x00000200
#define LVIF_COLFMT 0x00010000
#define LVIF_DI_SETITEM 0x1000

#define LVIS_FOCUSED 0x0001
#define LVIS_SELECTED 0x0002
#define LVIS_CUT 0x0004
#define LVIS_DROPHILITED 0x0008
#define LVIS_GLOW 0x0010
#define LVIS_ACTIVATING 0x0020
#define LVIS_OVERLAYMASK 0x0F00
#define LVIS_STATEIMAGEMASK 0xF000
#define INDEXTOSTATEIMAGEMASK(i) ((i) << 12)

typedef struct tagLVITEMA {
    UINT mask;
    int iItem;
    int iSubItem;
    UINT state;
    UINT stateMask;
    LPSTR pszText;
    int cchTextMax;
    int iImage;
    LPARAM lParam;
    int iIndent;
    int iGroupId;
    UINT cColumns;
    PUINT puColumns;
    int *piColFmt;
    int iGroup;
} LVITEMA, *LPLVITEMA;

typedef struct tagLVITEMW {
    UINT mask;
    int iItem;
    int iSubItem;
    UINT state;
    UINT stateMask;
    LPWSTR pszText;
    int cchTextMax;
    int iImage;
    LPARAM lParam;
    int iIndent;
    int iGroupId;
    UINT cColumns;
    PUINT puColumns;
    int *piColFmt;
    int iGroup;
} LVITEMW, *LPLVITEMW;

typedef LVITEMW LVITEM;
typedef LPLVITEMW LPLVITEM;
#define LVITEMA_V1_SIZE ((UINT)(ULONG_PTR) & ((LVITEMA *)0)->iIndent)
#define LVITEMW_V1_SIZE ((UINT)(ULONG_PTR) & ((LVITEMW *)0)->iIndent)

typedef struct tagLVCOLUMNA {
    UINT mask;
    int fmt;
    int cx;
    LPSTR pszText;
    int cchTextMax;
    int iSubItem;
    int iImage;
    int iOrder;
    int cxMin;
    int cxDefault;
    int cxIdeal;
} LVCOLUMNA, *LPLVCOLUMNA;

typedef struct tagLVCOLUMNW {
    UINT mask;
    int fmt;
    int cx;
    LPWSTR pszText;
    int cchTextMax;
    int iSubItem;
    int iImage;
    int iOrder;
    int cxMin;
    int cxDefault;
    int cxIdeal;
} LVCOLUMNW, *LPLVCOLUMNW;

typedef LVCOLUMNW LVCOLUMN;
typedef LPLVCOLUMNW LPLVCOLUMN;

#define LVCF_FMT 0x0001
#define LVCF_WIDTH 0x0002
#define LVCF_TEXT 0x0004
#define LVCF_SUBITEM 0x0008
#define LVCF_IMAGE 0x0010
#define LVCF_ORDER 0x0020
#define LVCF_MINWIDTH 0x0040
#define LVCF_DEFAULTWIDTH 0x0080
#define LVCF_IDEALWIDTH 0x0100

#define LVCFMT_LEFT 0x0000
#define LVCFMT_RIGHT 0x0001
#define LVCFMT_CENTER 0x0002
#define LVCFMT_JUSTIFYMASK 0x0003
#define LVCFMT_IMAGE 0x0800
#define LVCFMT_BITMAP_ON_RIGHT 0x1000
#define LVCFMT_COL_HAS_IMAGES 0x8000
#define LVCFMT_FIXED_WIDTH 0x00100
#define LVCFMT_NO_DPI_SCALE 0x40000
#define LVCFMT_FIXED_RATIO 0x80000
#define LVCFMT_SPLITBUTTON 0x1000000

#define LVSCW_AUTOSIZE -1
#define LVSCW_AUTOSIZE_USEHEADER -2

#define LVNI_ALL 0x0000
#define LVNI_FOCUSED 0x0001
#define LVNI_SELECTED 0x0002
#define LVNI_CUT 0x0004
#define LVNI_DROPHILITED 0x0008
#define LVNI_STATEMASK (LVNI_FOCUSED | LVNI_SELECTED | LVNI_CUT | LVNI_DROPHILITED)
#define LVNI_VISIBLEORDER 0x0010
#define LVNI_PREVIOUS 0x0020
#define LVNI_VISIBLEONLY 0x0040
#define LVNI_SAMEGROUPONLY 0x0080
#define LVNI_ABOVE 0x0100
#define LVNI_BELOW 0x0200
#define LVNI_TOLEFT 0x0400
#define LVNI_TORIGHT 0x0800
#define LVNI_DIRECTIONMASK (LVNI_ABOVE | LVNI_BELOW | LVNI_TOLEFT | LVNI_TORIGHT)

#define LVIR_BOUNDS 0
#define LVIR_ICON 1
#define LVIR_LABEL 2
#define LVIR_SELECTBOUNDS 3

#define LVHT_NOWHERE 0x00000001
#define LVHT_ONITEMICON 0x00000002
#define LVHT_ONITEMLABEL 0x00000004
#define LVHT_ONITEMSTATEICON 0x00000008
#define LVHT_ONITEM (LVHT_ONITEMICON | LVHT_ONITEMLABEL | LVHT_ONITEMSTATEICON)
#define LVHT_ABOVE 0x00000008
#define LVHT_BELOW 0x00000010
#define LVHT_TORIGHT 0x00000020
#define LVHT_TOLEFT 0x00000040
#define LVHT_EX_GROUP_HEADER 0x10000000
#define LVHT_EX_GROUP_FOOTER 0x20000000
#define LVHT_EX_GROUP_COLLAPSE 0x40000000
#define LVHT_EX_GROUP_BACKGROUND 0x80000000
#define LVHT_EX_GROUP_STATEICON 0x01000000
#define LVHT_EX_GROUP_SUBSETLINK 0x02000000
#define LVHT_EX_GROUP 0xF3000000
#define LVHT_EX_ONCONTENTS 0x04000000
#define LVHT_EX_FOOTER 0x08000000

typedef struct tagLVHITTESTINFO {
    POINT pt;
    UINT flags;
    int iItem;
    int iSubItem;
    int iGroup;
} LVHITTESTINFO, *LPLVHITTESTINFO;

#define LVFI_PARAM 0x0001
#define LVFI_STRING 0x0002
#define LVFI_SUBSTRING 0x0004
#define LVFI_PARTIAL 0x0008
#define LVFI_WRAP 0x0020
#define LVFI_NEARESTXY 0x0040

typedef struct tagLVFINDINFOA {
    UINT flags;
    LPCSTR psz;
    LPARAM lParam;
    POINT pt;
    UINT vkDirection;
} LVFINDINFOA, *LPFINDINFOA;

typedef struct tagLVFINDINFOW {
    UINT flags;
    LPCWSTR psz;
    LPARAM lParam;
    POINT pt;
    UINT vkDirection;
} LVFINDINFOW, *LPFINDINFOW;

typedef LVFINDINFOW LVFINDINFO;

#define LVA_DEFAULT 0x0000
#define LVA_ALIGNLEFT 0x0001
#define LVA_ALIGNTOP 0x0002
#define LVA_SNAPTOGRID 0x0005

#define LV_VIEW_ICON 0x0000
#define LV_VIEW_DETAILS 0x0001
#define LV_VIEW_SMALLICON 0x0002
#define LV_VIEW_LIST 0x0003
#define LV_VIEW_TILE 0x0004
#define LV_VIEW_MAX 0x0004

#define LVSICF_NOINVALIDATEALL 0x00000001
#define LVSICF_NOSCROLL 0x00000002

typedef int(CALLBACK *PFNLVCOMPARE)(LPARAM a, LPARAM b, LPARAM param);

#define LVM_FIRST 0x1000
#define LVM_GETBKCOLOR (LVM_FIRST + 0)
#define LVM_SETBKCOLOR (LVM_FIRST + 1)
#define LVM_GETIMAGELIST (LVM_FIRST + 2)
#define LVM_SETIMAGELIST (LVM_FIRST + 3)
#define LVM_GETITEMCOUNT (LVM_FIRST + 4)
#define LVM_GETITEMA (LVM_FIRST + 5)
#define LVM_GETITEMW (LVM_FIRST + 75)
#define LVM_SETITEMA (LVM_FIRST + 6)
#define LVM_SETITEMW (LVM_FIRST + 76)
#define LVM_INSERTITEMA (LVM_FIRST + 7)
#define LVM_INSERTITEMW (LVM_FIRST + 77)
#define LVM_DELETEITEM (LVM_FIRST + 8)
#define LVM_DELETEALLITEMS (LVM_FIRST + 9)
#define LVM_GETCALLBACKMASK (LVM_FIRST + 10)
#define LVM_SETCALLBACKMASK (LVM_FIRST + 11)
#define LVM_GETNEXTITEM (LVM_FIRST + 12)
#define LVM_FINDITEMA (LVM_FIRST + 13)
#define LVM_FINDITEMW (LVM_FIRST + 83)
#define LVM_GETITEMRECT (LVM_FIRST + 14)
#define LVM_SETITEMPOSITION (LVM_FIRST + 15)
#define LVM_GETITEMPOSITION (LVM_FIRST + 16)
#define LVM_GETSTRINGWIDTHA (LVM_FIRST + 17)
#define LVM_GETSTRINGWIDTHW (LVM_FIRST + 87)
#define LVM_HITTEST (LVM_FIRST + 18)
#define LVM_ENSUREVISIBLE (LVM_FIRST + 19)
#define LVM_SCROLL (LVM_FIRST + 20)
#define LVM_REDRAWITEMS (LVM_FIRST + 21)
#define LVM_ARRANGE (LVM_FIRST + 22)
#define LVM_EDITLABELA (LVM_FIRST + 23)
#define LVM_EDITLABELW (LVM_FIRST + 118)
#define LVM_GETEDITCONTROL (LVM_FIRST + 24)
#define LVM_GETCOLUMNA (LVM_FIRST + 25)
#define LVM_GETCOLUMNW (LVM_FIRST + 95)
#define LVM_SETCOLUMNA (LVM_FIRST + 26)
#define LVM_SETCOLUMNW (LVM_FIRST + 96)
#define LVM_INSERTCOLUMNA (LVM_FIRST + 27)
#define LVM_INSERTCOLUMNW (LVM_FIRST + 97)
#define LVM_DELETECOLUMN (LVM_FIRST + 28)
#define LVM_GETCOLUMNWIDTH (LVM_FIRST + 29)
#define LVM_SETCOLUMNWIDTH (LVM_FIRST + 30)
#define LVM_GETHEADER (LVM_FIRST + 31)
#define LVM_CREATEDRAGIMAGE (LVM_FIRST + 33)
#define LVM_GETVIEWRECT (LVM_FIRST + 34)
#define LVM_GETTEXTCOLOR (LVM_FIRST + 35)
#define LVM_SETTEXTCOLOR (LVM_FIRST + 36)
#define LVM_GETTEXTBKCOLOR (LVM_FIRST + 37)
#define LVM_SETTEXTBKCOLOR (LVM_FIRST + 38)
#define LVM_GETTOPINDEX (LVM_FIRST + 39)
#define LVM_GETCOUNTPERPAGE (LVM_FIRST + 40)
#define LVM_GETORIGIN (LVM_FIRST + 41)
#define LVM_UPDATE (LVM_FIRST + 42)
#define LVM_SETITEMSTATE (LVM_FIRST + 43)
#define LVM_GETITEMSTATE (LVM_FIRST + 44)
#define LVM_GETITEMTEXTA (LVM_FIRST + 45)
#define LVM_GETITEMTEXTW (LVM_FIRST + 115)
#define LVM_SETITEMTEXTA (LVM_FIRST + 46)
#define LVM_SETITEMTEXTW (LVM_FIRST + 116)
#define LVM_SETITEMCOUNT (LVM_FIRST + 47)
#define LVM_SORTITEMS (LVM_FIRST + 48)
#define LVM_SETITEMPOSITION32 (LVM_FIRST + 49)
#define LVM_GETSELECTEDCOUNT (LVM_FIRST + 50)
#define LVM_GETITEMSPACING (LVM_FIRST + 51)
#define LVM_GETISEARCHSTRINGA (LVM_FIRST + 52)
#define LVM_GETISEARCHSTRINGW (LVM_FIRST + 117)
#define LVM_SETICONSPACING (LVM_FIRST + 53)
#define LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 54)
#define LVM_GETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 55)
#define LVM_GETSUBITEMRECT (LVM_FIRST + 56)
#define LVM_SUBITEMHITTEST (LVM_FIRST + 57)
#define LVM_SETCOLUMNORDERARRAY (LVM_FIRST + 58)
#define LVM_GETCOLUMNORDERARRAY (LVM_FIRST + 59)
#define LVM_SETHOTITEM (LVM_FIRST + 60)
#define LVM_GETHOTITEM (LVM_FIRST + 61)
#define LVM_SETHOTCURSOR (LVM_FIRST + 62)
#define LVM_GETHOTCURSOR (LVM_FIRST + 63)
#define LVM_APPROXIMATEVIEWRECT (LVM_FIRST + 64)
#define LVM_SETWORKAREAS (LVM_FIRST + 65)
#define LVM_GETSELECTIONMARK (LVM_FIRST + 66)
#define LVM_SETSELECTIONMARK (LVM_FIRST + 67)
#define LVM_SETBKIMAGEA (LVM_FIRST + 68)
#define LVM_GETBKIMAGEA (LVM_FIRST + 69)
#define LVM_GETWORKAREAS (LVM_FIRST + 70)
#define LVM_SETHOVERTIME (LVM_FIRST + 71)
#define LVM_GETHOVERTIME (LVM_FIRST + 72)
#define LVM_GETNUMBEROFWORKAREAS (LVM_FIRST + 73)
#define LVM_SETTOOLTIPS (LVM_FIRST + 74)
#define LVM_GETTOOLTIPS (LVM_FIRST + 78)
#define LVM_SORTITEMSEX (LVM_FIRST + 81)
#define LVM_SETBKIMAGEW (LVM_FIRST + 138)
#define LVM_GETBKIMAGEW (LVM_FIRST + 139)
#define LVM_SETSELECTEDCOLUMN (LVM_FIRST + 140)
#define LVM_SETVIEW (LVM_FIRST + 142)
#define LVM_GETVIEW (LVM_FIRST + 143)
#define LVM_ENABLEGROUPVIEW (LVM_FIRST + 157)
#define LVM_ISGROUPVIEWENABLED (LVM_FIRST + 175)
#define LVM_GETSELECTEDCOLUMN (LVM_FIRST + 174)
#define LVM_ISITEMVISIBLE (LVM_FIRST + 182)
#define LVM_SETUNICODEFORMAT CCM_SETUNICODEFORMAT
#define LVM_GETUNICODEFORMAT CCM_GETUNICODEFORMAT
#define LVM_GETITEM LVM_GETITEMW
#define LVM_SETITEM LVM_SETITEMW
#define LVM_INSERTITEM LVM_INSERTITEMW
#define LVM_FINDITEM LVM_FINDITEMW
#define LVM_GETSTRINGWIDTH LVM_GETSTRINGWIDTHW
#define LVM_EDITLABEL LVM_EDITLABELW
#define LVM_GETCOLUMN LVM_GETCOLUMNW
#define LVM_SETCOLUMN LVM_SETCOLUMNW
#define LVM_INSERTCOLUMN LVM_INSERTCOLUMNW
#define LVM_GETITEMTEXT LVM_GETITEMTEXTW
#define LVM_SETITEMTEXT LVM_SETITEMTEXTW

#define LVN_ITEMCHANGING (LVN_FIRST - 0)
#define LVN_ITEMCHANGED (LVN_FIRST - 1)
#define LVN_INSERTITEM (LVN_FIRST - 2)
#define LVN_DELETEITEM (LVN_FIRST - 3)
#define LVN_DELETEALLITEMS (LVN_FIRST - 4)
#define LVN_BEGINLABELEDITA (LVN_FIRST - 5)
#define LVN_BEGINLABELEDITW (LVN_FIRST - 75)
#define LVN_ENDLABELEDITA (LVN_FIRST - 6)
#define LVN_ENDLABELEDITW (LVN_FIRST - 76)
#define LVN_COLUMNCLICK (LVN_FIRST - 8)
#define LVN_BEGINDRAG (LVN_FIRST - 9)
#define LVN_BEGINRDRAG (LVN_FIRST - 11)
#define LVN_ODCACHEHINT (LVN_FIRST - 13)
#define LVN_ODFINDITEMA (LVN_FIRST - 52)
#define LVN_ODFINDITEMW (LVN_FIRST - 79)
#define LVN_ITEMACTIVATE (LVN_FIRST - 14)
#define LVN_ODSTATECHANGED (LVN_FIRST - 15)
#define LVN_HOTTRACK (LVN_FIRST - 21)
#define LVN_GETDISPINFOA (LVN_FIRST - 50)
#define LVN_GETDISPINFOW (LVN_FIRST - 77)
#define LVN_SETDISPINFOA (LVN_FIRST - 51)
#define LVN_SETDISPINFOW (LVN_FIRST - 78)
#define LVN_KEYDOWN (LVN_FIRST - 55)
#define LVN_MARQUEEBEGIN (LVN_FIRST - 56)
#define LVN_GETINFOTIPA (LVN_FIRST - 57)
#define LVN_GETINFOTIPW (LVN_FIRST - 58)
#define LVN_INCREMENTALSEARCHA (LVN_FIRST - 62)
#define LVN_INCREMENTALSEARCHW (LVN_FIRST - 63)
#define LVN_BEGINSCROLL (LVN_FIRST - 80)
#define LVN_ENDSCROLL (LVN_FIRST - 81)
#define LVN_GETDISPINFO LVN_GETDISPINFOW
#define LVN_SETDISPINFO LVN_SETDISPINFOW
#define LVN_BEGINLABELEDIT LVN_BEGINLABELEDITW
#define LVN_ENDLABELEDIT LVN_ENDLABELEDITW
#define LVN_ODFINDITEM LVN_ODFINDITEMW

typedef struct tagNMLISTVIEW {
    NMHDR hdr;
    int iItem;
    int iSubItem;
    UINT uNewState;
    UINT uOldState;
    UINT uChanged;
    POINT ptAction;
    LPARAM lParam;
} NMLISTVIEW, *LPNMLISTVIEW;

typedef struct tagNMITEMACTIVATE {
    NMHDR hdr;
    int iItem;
    int iSubItem;
    UINT uNewState;
    UINT uOldState;
    UINT uChanged;
    POINT ptAction;
    LPARAM lParam;
    UINT uKeyFlags;
} NMITEMACTIVATE, *LPNMITEMACTIVATE;

#define LVKF_ALT 0x0001
#define LVKF_CONTROL 0x0002
#define LVKF_SHIFT 0x0004

typedef struct tagLVDISPINFO {
    NMHDR hdr;
    LVITEMA item;
} NMLVDISPINFOA, *LPNMLVDISPINFOA;

typedef struct tagLVDISPINFOW {
    NMHDR hdr;
    LVITEMW item;
} NMLVDISPINFOW, *LPNMLVDISPINFOW;

typedef NMLVDISPINFOW NMLVDISPINFO;

typedef struct tagLVKEYDOWN {
    NMHDR hdr;
    WORD wVKey;
    UINT flags;
} NMLVKEYDOWN, *LPNMLVKEYDOWN;

typedef struct tagNMLVCACHEHINT {
    NMHDR hdr;
    int iFrom;
    int iTo;
} NMLVCACHEHINT, *LPNMLVCACHEHINT;

typedef struct tagNMLVFINDITEMW {
    NMHDR hdr;
    int iStart;
    LVFINDINFOW lvfi;
} NMLVFINDITEMW, *LPNMLVFINDITEMW;

typedef struct tagNMLVODSTATECHANGE {
    NMHDR hdr;
    int iFrom;
    int iTo;
    UINT uNewState;
    UINT uOldState;
} NMLVODSTATECHANGE, *LPNMLVODSTATECHANGE;

typedef struct tagNMLVSCROLL {
    NMHDR hdr;
    int dx;
    int dy;
} NMLVSCROLL, *LPNMLVSCROLL;

typedef struct tagNMLVCUSTOMDRAW {
    NMCUSTOMDRAW nmcd;
    COLORREF clrText;
    COLORREF clrTextBk;
    int iSubItem;
    DWORD dwItemType;
    COLORREF clrFace;
    int iIconEffect;
    int iIconPhase;
    int iPartId;
    int iStateId;
    RECT rcText;
    UINT uAlign;
} NMLVCUSTOMDRAW, *LPNMLVCUSTOMDRAW;

#define ListView_GetItemCount(hwnd) (int)SendMessageW((hwnd), LVM_GETITEMCOUNT, 0, 0)
#define ListView_InsertItem(hwnd, pitem) (int)SendMessageW((hwnd), LVM_INSERTITEMW, 0, (LPARAM)(pitem))
#define ListView_SetItem(hwnd, pitem) (BOOL)SendMessageW((hwnd), LVM_SETITEMW, 0, (LPARAM)(pitem))
#define ListView_GetItem(hwnd, pitem) (BOOL)SendMessageW((hwnd), LVM_GETITEMW, 0, (LPARAM)(pitem))
#define ListView_DeleteItem(hwnd, i) (BOOL)SendMessageW((hwnd), LVM_DELETEITEM, (WPARAM)(int)(i), 0)
#define ListView_DeleteAllItems(hwnd) (BOOL)SendMessageW((hwnd), LVM_DELETEALLITEMS, 0, 0)
#define ListView_GetNextItem(hwnd, i, flags) (int)SendMessageW((hwnd), LVM_GETNEXTITEM, (WPARAM)(int)(i), MAKELPARAM((flags), 0))
#define ListView_EnsureVisible(hwnd, i, partial) (BOOL)SendMessageW((hwnd), LVM_ENSUREVISIBLE, (WPARAM)(int)(i), MAKELPARAM((partial), 0))
#define ListView_SetColumnWidth(hwnd, col, cx) (BOOL)SendMessageW((hwnd), LVM_SETCOLUMNWIDTH, (WPARAM)(int)(col), MAKELPARAM((cx), 0))
#define ListView_GetColumnWidth(hwnd, col) (int)SendMessageW((hwnd), LVM_GETCOLUMNWIDTH, (WPARAM)(int)(col), 0)
#define ListView_InsertColumn(hwnd, i, col) (int)SendMessageW((hwnd), LVM_INSERTCOLUMNW, (WPARAM)(int)(i), (LPARAM)(col))
#define ListView_GetHeader(hwnd) (HWND)SendMessageW((hwnd), LVM_GETHEADER, 0, 0)
#define ListView_SetExtendedListViewStyle(hwnd, style) (DWORD)SendMessageW((hwnd), LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (style))
#define ListView_SetImageList(hwnd, himl, which) (HIMAGELIST)SendMessageW((hwnd), LVM_SETIMAGELIST, (WPARAM)(which), (LPARAM)(himl))

/* ------------------------------------------------------------------ progress bar */

#define PROGRESS_CLASSA "msctls_progress32"
#define ANIMATE_CLASSW L"SysAnimate32"
#define ANIMATE_CLASS ANIMATE_CLASSW
#define ACS_CENTER 0x0001
#define ACS_TRANSPARENT 0x0002
#define ACS_AUTOPLAY 0x0004
#define ACS_TIMER 0x0008
#define ACM_OPENA (WM_USER + 100)
#define ACM_PLAY (WM_USER + 101)
#define ACM_STOP (WM_USER + 102)
#define ACM_OPENW (WM_USER + 103)
#define ACM_ISPLAYING (WM_USER + 104)
#define ACN_START 1
#define ACN_STOP 2
#define ACM_OPEN ACM_OPENW

#define PROGRESS_CLASSW L"msctls_progress32"
#define PROGRESS_CLASS PROGRESS_CLASSW

#define PBS_SMOOTH 0x01
#define PBS_VERTICAL 0x04
#define PBS_MARQUEE 0x08
#define PBS_SMOOTHREVERSE 0x10

#define PBM_SETRANGE (WM_USER + 1)
#define PBM_SETPOS (WM_USER + 2)
#define PBM_DELTAPOS (WM_USER + 3)
#define PBM_SETSTEP (WM_USER + 4)
#define PBM_STEPIT (WM_USER + 5)
#define PBM_SETRANGE32 (WM_USER + 6)
#define PBM_GETRANGE (WM_USER + 7)
#define PBM_GETPOS (WM_USER + 8)
#define PBM_SETBARCOLOR (WM_USER + 9)
#define PBM_SETBKCOLOR CCM_SETBKCOLOR
#define PBM_SETMARQUEE (WM_USER + 10)
#define PBM_GETSTEP (WM_USER + 13)
#define PBM_GETBKCOLOR (WM_USER + 14)
#define PBM_GETBARCOLOR (WM_USER + 15)
#define PBM_SETSTATE (WM_USER + 16)
#define PBM_GETSTATE (WM_USER + 17)

#define PBST_NORMAL 0x0001
#define PBST_ERROR 0x0002
#define PBST_PAUSED 0x0003

typedef struct {
    int iLow;
    int iHigh;
} PBRANGE, *PPBRANGE;

/* ------------------------------------------------------------------ status bar */

#define STATUSCLASSNAMEA "msctls_statusbar32"
#define STATUSCLASSNAMEW L"msctls_statusbar32"
#define STATUSCLASSNAME STATUSCLASSNAMEW

#define SBARS_SIZEGRIP 0x0100
#define SBARS_TOOLTIPS 0x0800
#define SBT_TOOLTIPS 0x0800

#define SB_SETTEXTA (WM_USER + 1)
#define SB_SETTEXTW (WM_USER + 11)
#define SB_GETTEXTA (WM_USER + 2)
#define SB_GETTEXTW (WM_USER + 13)
#define SB_GETTEXTLENGTHA (WM_USER + 3)
#define SB_GETTEXTLENGTHW (WM_USER + 12)
#define SB_SETPARTS (WM_USER + 4)
#define SB_GETPARTS (WM_USER + 6)
#define SB_GETBORDERS (WM_USER + 7)
#define SB_SETMINHEIGHT (WM_USER + 8)
#define SB_SIMPLE (WM_USER + 9)
#define SB_GETRECT (WM_USER + 10)
#define SB_ISSIMPLE (WM_USER + 14)
#define SB_SETICON (WM_USER + 15)
#define SB_SETTIPTEXTA (WM_USER + 16)
#define SB_SETTIPTEXTW (WM_USER + 17)
#define SB_GETTIPTEXTA (WM_USER + 18)
#define SB_GETTIPTEXTW (WM_USER + 19)
#define SB_GETICON (WM_USER + 20)
#define SB_SETUNICODEFORMAT CCM_SETUNICODEFORMAT
#define SB_GETUNICODEFORMAT CCM_GETUNICODEFORMAT
#define SB_SETBKCOLOR CCM_SETBKCOLOR
#define SB_SETTEXT SB_SETTEXTW
#define SB_GETTEXT SB_GETTEXTW
#define SB_GETTEXTLENGTH SB_GETTEXTLENGTHW

#define SBT_OWNERDRAW 0x1000
#define SBT_NOBORDERS 0x0100
#define SBT_POPOUT 0x0200
#define SBT_RTLREADING 0x0400
#define SBT_NOTABPARSING 0x0800

#define SBN_SIMPLEMODECHANGE (SBN_FIRST - 0)
#define SB_SIMPLEID 0x00ff

WINCOMMCTRLAPI HWND WINAPI CreateStatusWindowA(LONG style, LPCSTR text, HWND parent, UINT id);
WINCOMMCTRLAPI HWND WINAPI CreateStatusWindowW(LONG style, LPCWSTR text, HWND parent, UINT id);
WINCOMMCTRLAPI void WINAPI DrawStatusTextA(HDC dc, LPCRECT rect, LPCSTR text, UINT flags);
WINCOMMCTRLAPI void WINAPI DrawStatusTextW(HDC dc, LPCRECT rect, LPCWSTR text, UINT flags);

/* ------------------------------------------------------------------ tooltips */

#define TOOLTIPS_CLASSA "tooltips_class32"
#define TOOLTIPS_CLASSW L"tooltips_class32"
#define TOOLTIPS_CLASS TOOLTIPS_CLASSW

#define TTS_ALWAYSTIP 0x01
#define TTS_NOPREFIX 0x02
#define TTS_NOANIMATE 0x10
#define TTS_NOFADE 0x20
#define TTS_BALLOON 0x40
#define TTS_CLOSE 0x80
#define TTS_USEVISUALSTYLE 0x100

#define TTF_IDISHWND 0x0001
#define TTF_CENTERTIP 0x0002
#define TTF_RTLREADING 0x0004
#define TTF_SUBCLASS 0x0010
#define TTF_TRACK 0x0020
#define TTF_ABSOLUTE 0x0080
#define TTF_TRANSPARENT 0x0100
#define TTF_PARSELINKS 0x1000
#define TTF_DI_SETITEM 0x8000

#define TTDT_AUTOMATIC 0
#define TTDT_RESHOW 1
#define TTDT_AUTOPOP 2
#define TTDT_INITIAL 3

typedef struct tagTOOLINFOA {
    UINT cbSize;
    UINT uFlags;
    HWND hwnd;
    UINT_PTR uId;
    RECT rect;
    HINSTANCE hinst;
    LPSTR lpszText;
    LPARAM lParam;
    void *lpReserved;
} TTTOOLINFOA, *LPTTTOOLINFOA, TOOLINFOA;

typedef struct tagTOOLINFOW {
    UINT cbSize;
    UINT uFlags;
    HWND hwnd;
    UINT_PTR uId;
    RECT rect;
    HINSTANCE hinst;
    LPWSTR lpszText;
    LPARAM lParam;
    void *lpReserved;
} TTTOOLINFOW, *LPTTTOOLINFOW, TOOLINFOW;

typedef TTTOOLINFOW TTTOOLINFO;
typedef TOOLINFOW TOOLINFO;
#define TTTOOLINFOA_V1_SIZE ((UINT)(ULONG_PTR) & ((TTTOOLINFOA *)0)->lParam)
#define TTTOOLINFOW_V1_SIZE ((UINT)(ULONG_PTR) & ((TTTOOLINFOW *)0)->lParam)
#define TTTOOLINFOA_V2_SIZE ((UINT)(ULONG_PTR) & ((TTTOOLINFOA *)0)->lpReserved)
#define TTTOOLINFOW_V2_SIZE ((UINT)(ULONG_PTR) & ((TTTOOLINFOW *)0)->lpReserved)

typedef struct _TT_HITTESTINFOW {
    HWND hwnd;
    POINT pt;
    TTTOOLINFOW ti;
} TTHITTESTINFOW, *LPTTHITTESTINFOW;

typedef struct _TTGETTITLE {
    DWORD dwSize;
    UINT uTitleBitmap;
    UINT cch;
    WCHAR *pszTitle;
} TTGETTITLE, *PTTGETTITLE;

#define TTM_ACTIVATE (WM_USER + 1)
#define TTM_SETDELAYTIME (WM_USER + 3)
#define TTM_ADDTOOLA (WM_USER + 4)
#define TTM_ADDTOOLW (WM_USER + 50)
#define TTM_DELTOOLA (WM_USER + 5)
#define TTM_DELTOOLW (WM_USER + 51)
#define TTM_NEWTOOLRECTA (WM_USER + 6)
#define TTM_NEWTOOLRECTW (WM_USER + 52)
#define TTM_RELAYEVENT (WM_USER + 7)
#define TTM_GETTOOLINFOA (WM_USER + 8)
#define TTM_GETTOOLINFOW (WM_USER + 53)
#define TTM_SETTOOLINFOA (WM_USER + 9)
#define TTM_SETTOOLINFOW (WM_USER + 54)
#define TTM_HITTESTA (WM_USER + 10)
#define TTM_HITTESTW (WM_USER + 55)
#define TTM_GETTEXTA (WM_USER + 11)
#define TTM_GETTEXTW (WM_USER + 56)
#define TTM_UPDATETIPTEXTA (WM_USER + 12)
#define TTM_UPDATETIPTEXTW (WM_USER + 57)
#define TTM_GETTOOLCOUNT (WM_USER + 13)
#define TTM_ENUMTOOLSA (WM_USER + 14)
#define TTM_ENUMTOOLSW (WM_USER + 58)
#define TTM_GETCURRENTTOOLA (WM_USER + 15)
#define TTM_GETCURRENTTOOLW (WM_USER + 59)
#define TTM_WINDOWFROMPOINT (WM_USER + 16)
#define TTM_TRACKACTIVATE (WM_USER + 17)
#define TTM_TRACKPOSITION (WM_USER + 18)
#define TTM_SETTIPBKCOLOR (WM_USER + 19)
#define TTM_SETTIPTEXTCOLOR (WM_USER + 20)
#define TTM_GETDELAYTIME (WM_USER + 21)
#define TTM_GETTIPBKCOLOR (WM_USER + 22)
#define TTM_GETTIPTEXTCOLOR (WM_USER + 23)
#define TTM_SETMAXTIPWIDTH (WM_USER + 24)
#define TTM_GETMAXTIPWIDTH (WM_USER + 25)
#define TTM_SETMARGIN (WM_USER + 26)
#define TTM_GETMARGIN (WM_USER + 27)
#define TTM_POP (WM_USER + 28)
#define TTM_UPDATE (WM_USER + 29)
#define TTM_GETBUBBLESIZE (WM_USER + 30)
#define TTM_ADJUSTRECT (WM_USER + 31)
#define TTM_SETTITLEA (WM_USER + 32)
#define TTM_SETTITLEW (WM_USER + 33)
#define TTM_POPUP (WM_USER + 34)
#define TTM_GETTITLE (WM_USER + 35)
#define TTM_SETWINDOWTHEME CCM_SETWINDOWTHEME
#define TTM_ADDTOOL TTM_ADDTOOLW
#define TTM_DELTOOL TTM_DELTOOLW
#define TTM_GETTOOLINFO TTM_GETTOOLINFOW
#define TTM_SETTOOLINFO TTM_SETTOOLINFOW
#define TTM_UPDATETIPTEXT TTM_UPDATETIPTEXTW
#define TTM_GETTEXT TTM_GETTEXTW
#define TTM_ENUMTOOLS TTM_ENUMTOOLSW

#define TTN_GETDISPINFOA (TTN_FIRST - 0)
#define TTN_GETDISPINFOW (TTN_FIRST - 10)
#define TTN_SHOW (TTN_FIRST - 1)
#define TTN_POP (TTN_FIRST - 2)
#define TTN_LINKCLICK (TTN_FIRST - 3)
#define TTN_GETDISPINFO TTN_GETDISPINFOW
#define TTN_NEEDTEXTA TTN_GETDISPINFOA
#define TTN_NEEDTEXTW TTN_GETDISPINFOW

typedef struct tagNMTTDISPINFOW {
    NMHDR hdr;
    LPWSTR lpszText;
    WCHAR szText[80];
    HINSTANCE hinst;
    UINT uFlags;
    LPARAM lParam;
} NMTTDISPINFOW, *LPNMTTDISPINFOW;

typedef struct tagNMTTDISPINFOA {
    NMHDR hdr;
    LPSTR lpszText;
    char szText[80];
    HINSTANCE hinst;
    UINT uFlags;
    LPARAM lParam;
} NMTTDISPINFOA, *LPNMTTDISPINFOA;

/* ------------------------------------------------------------------ up down */

#define UPDOWN_CLASSA "msctls_updown32"
#define UPDOWN_CLASSW L"msctls_updown32"
#define UPDOWN_CLASS UPDOWN_CLASSW

#define UD_MAXVAL 0x7fff
#define UD_MINVAL (-UD_MAXVAL)

#define UDS_WRAP 0x0001
#define UDS_SETBUDDYINT 0x0002
#define UDS_ALIGNRIGHT 0x0004
#define UDS_ALIGNLEFT 0x0008
#define UDS_AUTOBUDDY 0x0010
#define UDS_ARROWKEYS 0x0020
#define UDS_HORZ 0x0040
#define UDS_NOTHOUSANDS 0x0080
#define UDS_HOTTRACK 0x0100

typedef struct _UDACCEL {
    UINT nSec;
    UINT nInc;
} UDACCEL, *LPUDACCEL;

#define UDM_SETRANGE (WM_USER + 101)
#define UDM_GETRANGE (WM_USER + 102)
#define UDM_SETPOS (WM_USER + 103)
#define UDM_GETPOS (WM_USER + 104)
#define UDM_SETBUDDY (WM_USER + 105)
#define UDM_GETBUDDY (WM_USER + 106)
#define UDM_SETACCEL (WM_USER + 107)
#define UDM_GETACCEL (WM_USER + 108)
#define UDM_SETBASE (WM_USER + 109)
#define UDM_GETBASE (WM_USER + 110)
#define UDM_SETRANGE32 (WM_USER + 111)
#define UDM_GETRANGE32 (WM_USER + 112)
#define UDM_SETPOS32 (WM_USER + 113)
#define UDM_GETPOS32 (WM_USER + 114)

#define UDN_DELTAPOS (UDN_FIRST - 1)

typedef struct _NM_UPDOWN {
    NMHDR hdr;
    int iPos;
    int iDelta;
} NMUPDOWN, *LPNMUPDOWN;

WINCOMMCTRLAPI HWND WINAPI CreateUpDownControl(DWORD style, int x, int y, int cx, int cy, HWND parent, int id,
                                               HINSTANCE inst, HWND buddy, int upper, int lower, int pos);

/* ------------------------------------------------------------------ trackbar */

#define TRACKBAR_CLASSA "msctls_trackbar32"
#define TRACKBAR_CLASSW L"msctls_trackbar32"
#define TRACKBAR_CLASS TRACKBAR_CLASSW

#define TBS_AUTOTICKS 0x0001
#define TBS_VERT 0x0002
#define TBS_HORZ 0x0000
#define TBS_TOP 0x0004
#define TBS_BOTTOM 0x0000
#define TBS_LEFT 0x0004
#define TBS_RIGHT 0x0000
#define TBS_BOTH 0x0008
#define TBS_NOTICKS 0x0010
#define TBS_ENABLESELRANGE 0x0020
#define TBS_FIXEDLENGTH 0x0040
#define TBS_NOTHUMB 0x0080
#define TBS_TOOLTIPS 0x0100
#define TBS_REVERSED 0x0200
#define TBS_DOWNISLEFT 0x0400
#define TBS_NOTIFYBEFOREMOVE 0x0800
#define TBS_TRANSPARENTBKGND 0x1000

#define TBM_GETPOS (WM_USER)
#define TBM_GETRANGEMIN (WM_USER + 1)
#define TBM_GETRANGEMAX (WM_USER + 2)
#define TBM_GETTIC (WM_USER + 3)
#define TBM_SETTIC (WM_USER + 4)
#define TBM_SETPOS (WM_USER + 5)
#define TBM_SETRANGE (WM_USER + 6)
#define TBM_SETRANGEMIN (WM_USER + 7)
#define TBM_SETRANGEMAX (WM_USER + 8)
#define TBM_CLEARTICS (WM_USER + 9)
#define TBM_SETSEL (WM_USER + 10)
#define TBM_SETSELSTART (WM_USER + 11)
#define TBM_SETSELEND (WM_USER + 12)
#define TBM_GETPTICS (WM_USER + 14)
#define TBM_GETTICPOS (WM_USER + 15)
#define TBM_GETNUMTICS (WM_USER + 16)
#define TBM_GETSELSTART (WM_USER + 17)
#define TBM_GETSELEND (WM_USER + 18)
#define TBM_CLEARSEL (WM_USER + 19)
#define TBM_SETTICFREQ (WM_USER + 20)
#define TBM_SETPAGESIZE (WM_USER + 21)
#define TBM_GETPAGESIZE (WM_USER + 22)
#define TBM_SETLINESIZE (WM_USER + 23)
#define TBM_GETLINESIZE (WM_USER + 24)
#define TBM_GETTHUMBRECT (WM_USER + 25)
#define TBM_GETCHANNELRECT (WM_USER + 26)
#define TBM_SETTHUMBLENGTH (WM_USER + 27)
#define TBM_GETTHUMBLENGTH (WM_USER + 28)
#define TBM_SETTOOLTIPS (WM_USER + 29)
#define TBM_GETTOOLTIPS (WM_USER + 30)
#define TBM_SETTIPSIDE (WM_USER + 31)
#define TBM_SETBUDDY (WM_USER + 32)
#define TBM_GETBUDDY (WM_USER + 33)
#define TBM_SETPOSNOTIFY (WM_USER + 34)

#define TB_LINEUP 0
#define TB_LINEDOWN 1
#define TB_PAGEUP 2
#define TB_PAGEDOWN 3
#define TB_THUMBPOSITION 4
#define TB_THUMBTRACK 5
#define TB_TOP 6
#define TB_BOTTOM 7
#define TB_ENDTRACK 8

#define TRBN_THUMBPOSCHANGING (TRBN_FIRST - 1)

typedef struct tagTRBTHUMBPOSCHANGING {
    NMHDR hdr;
    DWORD dwPos;
    int nReason;
} NMTRBTHUMBPOSCHANGING;

/* ------------------------------------------------------------------ tab control */

#define WC_TABCONTROLA "SysTabControl32"
#define WC_TABCONTROLW L"SysTabControl32"
#define WC_TABCONTROL WC_TABCONTROLW

#define TCS_SCROLLOPPOSITE 0x0001
#define TCS_BOTTOM 0x0002
#define TCS_RIGHT 0x0002
#define TCS_MULTISELECT 0x0004
#define TCS_FLATBUTTONS 0x0008
#define TCS_FORCEICONLEFT 0x0010
#define TCS_FORCELABELLEFT 0x0020
#define TCS_HOTTRACK 0x0040
#define TCS_VERTICAL 0x0080
#define TCS_TABS 0x0000
#define TCS_BUTTONS 0x0100
#define TCS_SINGLELINE 0x0000
#define TCS_MULTILINE 0x0200
#define TCS_RIGHTJUSTIFY 0x0000
#define TCS_FIXEDWIDTH 0x0400
#define TCS_RAGGEDRIGHT 0x0800
#define TCS_FOCUSONBUTTONDOWN 0x1000
#define TCS_OWNERDRAWFIXED 0x2000
#define TCS_TOOLTIPS 0x4000
#define TCS_FOCUSNEVER 0x8000

#define TCIF_TEXT 0x0001
#define TCIF_IMAGE 0x0002
#define TCIF_RTLREADING 0x0004
#define TCIF_PARAM 0x0008
#define TCIF_STATE 0x0010

#define TCIS_BUTTONPRESSED 0x0001
#define TCIS_HIGHLIGHTED 0x0002

typedef struct tagTCITEMA {
    UINT mask;
    DWORD dwState;
    DWORD dwStateMask;
    LPSTR pszText;
    int cchTextMax;
    int iImage;
    LPARAM lParam;
} TCITEMA, *LPTCITEMA;

typedef struct tagTCITEMW {
    UINT mask;
    DWORD dwState;
    DWORD dwStateMask;
    LPWSTR pszText;
    int cchTextMax;
    int iImage;
    LPARAM lParam;
} TCITEMW, *LPTCITEMW;

typedef TCITEMW TCITEM;

#define TCHT_NOWHERE 0x0001
#define TCHT_ONITEMICON 0x0002
#define TCHT_ONITEMLABEL 0x0004
#define TCHT_ONITEM (TCHT_ONITEMICON | TCHT_ONITEMLABEL)

typedef struct tagTCHITTESTINFO {
    POINT pt;
    UINT flags;
} TCHITTESTINFO, *LPTCHITTESTINFO;

#define TCM_FIRST 0x1300
#define TCM_GETIMAGELIST (TCM_FIRST + 2)
#define TCM_SETIMAGELIST (TCM_FIRST + 3)
#define TCM_GETITEMCOUNT (TCM_FIRST + 4)
#define TCM_GETITEMA (TCM_FIRST + 5)
#define TCM_GETITEMW (TCM_FIRST + 60)
#define TCM_SETITEMA (TCM_FIRST + 6)
#define TCM_SETITEMW (TCM_FIRST + 61)
#define TCM_INSERTITEMA (TCM_FIRST + 7)
#define TCM_INSERTITEMW (TCM_FIRST + 62)
#define TCM_DELETEITEM (TCM_FIRST + 8)
#define TCM_DELETEALLITEMS (TCM_FIRST + 9)
#define TCM_GETITEMRECT (TCM_FIRST + 10)
#define TCM_GETCURSEL (TCM_FIRST + 11)
#define TCM_SETCURSEL (TCM_FIRST + 12)
#define TCM_HITTEST (TCM_FIRST + 13)
#define TCM_SETITEMEXTRA (TCM_FIRST + 14)
#define TCM_ADJUSTRECT (TCM_FIRST + 40)
#define TCM_SETITEMSIZE (TCM_FIRST + 41)
#define TCM_REMOVEIMAGE (TCM_FIRST + 42)
#define TCM_SETPADDING (TCM_FIRST + 43)
#define TCM_GETROWCOUNT (TCM_FIRST + 44)
#define TCM_GETTOOLTIPS (TCM_FIRST + 45)
#define TCM_SETTOOLTIPS (TCM_FIRST + 46)
#define TCM_GETCURFOCUS (TCM_FIRST + 47)
#define TCM_SETCURFOCUS (TCM_FIRST + 48)
#define TCM_SETMINTABWIDTH (TCM_FIRST + 49)
#define TCM_DESELECTALL (TCM_FIRST + 50)
#define TCM_HIGHLIGHTITEM (TCM_FIRST + 51)
#define TCM_SETEXTENDEDSTYLE (TCM_FIRST + 52)
#define TCM_GETEXTENDEDSTYLE (TCM_FIRST + 53)
#define TCM_GETITEM TCM_GETITEMW
#define TCM_SETITEM TCM_SETITEMW
#define TCM_INSERTITEM TCM_INSERTITEMW

#define TCN_KEYDOWN (TCN_FIRST - 0)
#define TCN_SELCHANGE (TCN_FIRST - 1)
#define TCN_SELCHANGING (TCN_FIRST - 2)
#define TCN_GETOBJECT (TCN_FIRST - 3)
#define TCN_FOCUSCHANGE (TCN_FIRST - 4)

/* ------------------------------------------------------------------ tree view */

#define WC_TREEVIEWA "SysTreeView32"
#define WC_TREEVIEWW L"SysTreeView32"
#define WC_TREEVIEW WC_TREEVIEWW

#define TVS_HASBUTTONS 0x0001
#define TVS_HASLINES 0x0002
#define TVS_LINESATROOT 0x0004
#define TVS_EDITLABELS 0x0008
#define TVS_DISABLEDRAGDROP 0x0010
#define TVS_SHOWSELALWAYS 0x0020
#define TVS_RTLREADING 0x0040
#define TVS_NOTOOLTIPS 0x0080
#define TVS_CHECKBOXES 0x0100
#define TVS_TRACKSELECT 0x0200
#define TVS_SINGLEEXPAND 0x0400
#define TVS_INFOTIP 0x0800
#define TVS_FULLROWSELECT 0x1000
#define TVS_NOSCROLL 0x2000
#define TVS_NONEVENHEIGHT 0x4000
#define TVS_NOHSCROLL 0x8000

struct _TREEITEM;
typedef struct _TREEITEM *HTREEITEM;

#define TVIF_TEXT 0x0001
#define TVIF_IMAGE 0x0002
#define TVIF_PARAM 0x0004
#define TVIF_STATE 0x0008
#define TVIF_HANDLE 0x0010
#define TVIF_SELECTEDIMAGE 0x0020
#define TVIF_CHILDREN 0x0040
#define TVIF_INTEGRAL 0x0080
#define TVIF_STATEEX 0x0100
#define TVIF_EXPANDEDIMAGE 0x0200
#define TVIF_DI_SETITEM 0x1000

#define TVIS_SELECTED 0x0002
#define TVIS_CUT 0x0004
#define TVIS_DROPHILITED 0x0008
#define TVIS_BOLD 0x0010
#define TVIS_EXPANDED 0x0020
#define TVIS_EXPANDEDONCE 0x0040
#define TVIS_EXPANDPARTIAL 0x0080
#define TVIS_OVERLAYMASK 0x0F00
#define TVIS_STATEIMAGEMASK 0xF000
#define TVIS_USERMASK 0xF000

typedef struct tagTVITEMA {
    UINT mask;
    HTREEITEM hItem;
    UINT state;
    UINT stateMask;
    LPSTR pszText;
    int cchTextMax;
    int iImage;
    int iSelectedImage;
    int cChildren;
    LPARAM lParam;
} TVITEMA, *LPTVITEMA;

typedef struct tagTVITEMW {
    UINT mask;
    HTREEITEM hItem;
    UINT state;
    UINT stateMask;
    LPWSTR pszText;
    int cchTextMax;
    int iImage;
    int iSelectedImage;
    int cChildren;
    LPARAM lParam;
} TVITEMW, *LPTVITEMW;

typedef struct tagTVITEMEXW {
    UINT mask;
    HTREEITEM hItem;
    UINT state;
    UINT stateMask;
    LPWSTR pszText;
    int cchTextMax;
    int iImage;
    int iSelectedImage;
    int cChildren;
    LPARAM lParam;
    int iIntegral;
    UINT uStateEx;
    HWND hwnd;
    int iExpandedImage;
    int iReserved;
} TVITEMEXW, *LPTVITEMEXW;

typedef TVITEMW TVITEM;

#define TVI_ROOT ((HTREEITEM)(ULONG_PTR)-0x10000)
#define TVI_FIRST ((HTREEITEM)(ULONG_PTR)-0x0FFFF)
#define TVI_LAST ((HTREEITEM)(ULONG_PTR)-0x0FFFE)
#define TVI_SORT ((HTREEITEM)(ULONG_PTR)-0x0FFFD)

typedef struct tagTVINSERTSTRUCTA {
    HTREEITEM hParent;
    HTREEITEM hInsertAfter;
    TVITEMA item;
} TVINSERTSTRUCTA, *LPTVINSERTSTRUCTA;

typedef struct tagTVINSERTSTRUCTW {
    HTREEITEM hParent;
    HTREEITEM hInsertAfter;
    union {
        TVITEMEXW itemex;
        TVITEMW item;
    };
} TVINSERTSTRUCTW, *LPTVINSERTSTRUCTW;

typedef TVINSERTSTRUCTW TVINSERTSTRUCT;

#define TVM_FIRST 0x1100
#define TVM_INSERTITEMA (TVM_FIRST + 0)
#define TVM_INSERTITEMW (TVM_FIRST + 50)
#define TVM_DELETEITEM (TVM_FIRST + 1)
#define TVM_EXPAND (TVM_FIRST + 2)
#define TVM_GETITEMRECT (TVM_FIRST + 4)
#define TVM_GETCOUNT (TVM_FIRST + 5)
#define TVM_GETINDENT (TVM_FIRST + 6)
#define TVM_SETINDENT (TVM_FIRST + 7)
#define TVM_GETIMAGELIST (TVM_FIRST + 8)
#define TVM_SETIMAGELIST (TVM_FIRST + 9)
#define TVM_GETNEXTITEM (TVM_FIRST + 10)
#define TVM_SELECTITEM (TVM_FIRST + 11)
#define TVM_GETITEMA (TVM_FIRST + 12)
#define TVM_GETITEMW (TVM_FIRST + 62)
#define TVM_SETITEMA (TVM_FIRST + 13)
#define TVM_SETITEMW (TVM_FIRST + 63)
#define TVM_EDITLABELA (TVM_FIRST + 14)
#define TVM_EDITLABELW (TVM_FIRST + 65)
#define TVM_GETEDITCONTROL (TVM_FIRST + 15)
#define TVM_GETVISIBLECOUNT (TVM_FIRST + 16)
#define TVM_HITTEST (TVM_FIRST + 17)
#define TVM_CREATEDRAGIMAGE (TVM_FIRST + 18)
#define TVM_SORTCHILDREN (TVM_FIRST + 19)
#define TVM_ENSUREVISIBLE (TVM_FIRST + 20)
#define TVM_SORTCHILDRENCB (TVM_FIRST + 21)
#define TVM_ENDEDITLABELNOW (TVM_FIRST + 22)
#define TVM_SETTOOLTIPS (TVM_FIRST + 24)
#define TVM_GETTOOLTIPS (TVM_FIRST + 25)
#define TVM_SETINSERTMARK (TVM_FIRST + 26)
#define TVM_SETITEMHEIGHT (TVM_FIRST + 27)
#define TVM_GETITEMHEIGHT (TVM_FIRST + 28)
#define TVM_SETBKCOLOR (TVM_FIRST + 29)
#define TVM_SETTEXTCOLOR (TVM_FIRST + 30)
#define TVM_GETBKCOLOR (TVM_FIRST + 31)
#define TVM_GETTEXTCOLOR (TVM_FIRST + 32)
#define TVM_SETSCROLLTIME (TVM_FIRST + 33)
#define TVM_GETSCROLLTIME (TVM_FIRST + 34)
#define TVM_SETINSERTMARKCOLOR (TVM_FIRST + 37)
#define TVM_GETINSERTMARKCOLOR (TVM_FIRST + 38)
#define TVM_GETITEMSTATE (TVM_FIRST + 39)
#define TVM_SETLINECOLOR (TVM_FIRST + 40)
#define TVM_GETLINECOLOR (TVM_FIRST + 41)
#define TVM_SETEXTENDEDSTYLE (TVM_FIRST + 44)
#define TVM_GETEXTENDEDSTYLE (TVM_FIRST + 45)
#define TVM_SETUNICODEFORMAT CCM_SETUNICODEFORMAT
#define TVM_GETUNICODEFORMAT CCM_GETUNICODEFORMAT
#define TVM_INSERTITEM TVM_INSERTITEMW
#define TVM_GETITEM TVM_GETITEMW
#define TVM_SETITEM TVM_SETITEMW

#define TVE_COLLAPSE 0x0001
#define TVE_EXPAND 0x0002
#define TVE_TOGGLE 0x0003
#define TVE_EXPANDPARTIAL 0x4000
#define TVE_COLLAPSERESET 0x8000

#define TVSIL_NORMAL 0
#define TVSIL_STATE 2

#define TVGN_ROOT 0x0000
#define TVGN_NEXT 0x0001
#define TVGN_PREVIOUS 0x0002
#define TVGN_PARENT 0x0003
#define TVGN_CHILD 0x0004
#define TVGN_FIRSTVISIBLE 0x0005
#define TVGN_NEXTVISIBLE 0x0006
#define TVGN_PREVIOUSVISIBLE 0x0007
#define TVGN_DROPHILITE 0x0008
#define TVGN_CARET 0x0009
#define TVGN_LASTVISIBLE 0x000A
#define TVGN_NEXTSELECTED 0x000B
#define TVSI_NOSINGLEEXPAND 0x8000

#define TVHT_NOWHERE 0x0001
#define TVHT_ONITEMICON 0x0002
#define TVHT_ONITEMLABEL 0x0004
#define TVHT_ONITEMINDENT 0x0008
#define TVHT_ONITEMBUTTON 0x0010
#define TVHT_ONITEMRIGHT 0x0020
#define TVHT_ONITEMSTATEICON 0x0040
#define TVHT_ONITEM (TVHT_ONITEMICON | TVHT_ONITEMLABEL | TVHT_ONITEMSTATEICON)
#define TVHT_ABOVE 0x0100
#define TVHT_BELOW 0x0200
#define TVHT_TORIGHT 0x0400
#define TVHT_TOLEFT 0x0800

typedef struct tagTVHITTESTINFO {
    POINT pt;
    UINT flags;
    HTREEITEM hItem;
} TVHITTESTINFO, *LPTVHITTESTINFO;

typedef int(CALLBACK *PFNTVCOMPARE)(LPARAM a, LPARAM b, LPARAM param);

typedef struct tagTVSORTCB {
    HTREEITEM hParent;
    PFNTVCOMPARE lpfnCompare;
    LPARAM lParam;
} TVSORTCB, *LPTVSORTCB;

#define TVN_SELCHANGINGA (TVN_FIRST - 1)
#define TVN_SELCHANGINGW (TVN_FIRST - 50)
#define TVN_SELCHANGEDA (TVN_FIRST - 2)
#define TVN_SELCHANGEDW (TVN_FIRST - 51)
#define TVN_GETDISPINFOA (TVN_FIRST - 3)
#define TVN_GETDISPINFOW (TVN_FIRST - 52)
#define TVN_SETDISPINFOA (TVN_FIRST - 4)
#define TVN_SETDISPINFOW (TVN_FIRST - 53)
#define TVN_ITEMEXPANDINGA (TVN_FIRST - 5)
#define TVN_ITEMEXPANDINGW (TVN_FIRST - 54)
#define TVN_ITEMEXPANDEDA (TVN_FIRST - 6)
#define TVN_ITEMEXPANDEDW (TVN_FIRST - 55)
#define TVN_BEGINDRAGA (TVN_FIRST - 7)
#define TVN_BEGINDRAGW (TVN_FIRST - 56)
#define TVN_DELETEITEMA (TVN_FIRST - 9)
#define TVN_DELETEITEMW (TVN_FIRST - 58)
#define TVN_KEYDOWN (TVN_FIRST - 12)
#define TVN_SELCHANGING TVN_SELCHANGINGW
#define TVN_SELCHANGED TVN_SELCHANGEDW
#define TVN_GETDISPINFO TVN_GETDISPINFOW
#define TVN_ITEMEXPANDING TVN_ITEMEXPANDINGW
#define TVN_ITEMEXPANDED TVN_ITEMEXPANDEDW
#define TVN_DELETEITEM TVN_DELETEITEMW

#define TVC_UNKNOWN 0x0000
#define TVC_BYMOUSE 0x0001
#define TVC_BYKEYBOARD 0x0002

typedef struct tagNMTREEVIEWW {
    NMHDR hdr;
    UINT action;
    TVITEMW itemOld;
    TVITEMW itemNew;
    POINT ptDrag;
} NMTREEVIEWW, *LPNMTREEVIEWW;

typedef struct tagNMTREEVIEWA {
    NMHDR hdr;
    UINT action;
    TVITEMA itemOld;
    TVITEMA itemNew;
    POINT ptDrag;
} NMTREEVIEWA, *LPNMTREEVIEWA;

typedef NMTREEVIEWW NMTREEVIEW;

typedef struct tagTVDISPINFOW {
    NMHDR hdr;
    TVITEMW item;
} NMTVDISPINFOW, *LPNMTVDISPINFOW;

typedef struct tagTVDISPINFOA {
    NMHDR hdr;
    TVITEMA item;
} NMTVDISPINFOA, *LPNMTVDISPINFOA;

/* ------------------------------------------------------------------ toolbar */

#define TOOLBARCLASSNAMEA "ToolbarWindow32"
#define TOOLBARCLASSNAMEW L"ToolbarWindow32"
#define TOOLBARCLASSNAME TOOLBARCLASSNAMEW

typedef struct _TBBUTTON {
    int iBitmap;
    int idCommand;
    BYTE fsState;
    BYTE fsStyle;
#ifdef _WIN64
    BYTE bReserved[6];
#else
    BYTE bReserved[2];
#endif
    DWORD_PTR dwData;
    INT_PTR iString;
} TBBUTTON, *PTBBUTTON, *LPTBBUTTON;
typedef const TBBUTTON *LPCTBBUTTON;

typedef struct tagTBADDBITMAP {
    HINSTANCE hInst;
    UINT_PTR nID;
} TBADDBITMAP, *LPTBADDBITMAP;

#define HINST_COMMCTRL ((HINSTANCE)(LONG_PTR)-1)
#define IDB_STD_SMALL_COLOR 0
#define IDB_STD_LARGE_COLOR 1
#define IDB_VIEW_SMALL_COLOR 4
#define IDB_VIEW_LARGE_COLOR 5
#define IDB_HIST_SMALL_COLOR 8
#define IDB_HIST_LARGE_COLOR 9

#define TBSTATE_CHECKED 0x01
#define TBSTATE_PRESSED 0x02
#define TBSTATE_ENABLED 0x04
#define TBSTATE_HIDDEN 0x08
#define TBSTATE_INDETERMINATE 0x10
#define TBSTATE_WRAP 0x20
#define TBSTATE_ELLIPSES 0x40
#define TBSTATE_MARKED 0x80

#define TBSTYLE_BUTTON 0x0000
#define TBSTYLE_SEP 0x0001
#define TBSTYLE_CHECK 0x0002
#define TBSTYLE_GROUP 0x0004
#define TBSTYLE_CHECKGROUP (TBSTYLE_GROUP | TBSTYLE_CHECK)
#define TBSTYLE_DROPDOWN 0x0008
#define TBSTYLE_AUTOSIZE 0x0010
#define TBSTYLE_NOPREFIX 0x0020
#define TBSTYLE_TOOLTIPS 0x0100
#define TBSTYLE_WRAPABLE 0x0200
#define TBSTYLE_ALTDRAG 0x0400
#define TBSTYLE_FLAT 0x0800
#define TBSTYLE_LIST 0x1000
#define TBSTYLE_CUSTOMERASE 0x2000
#define TBSTYLE_REGISTERDROP 0x4000
#define TBSTYLE_TRANSPARENT 0x8000
#define BTNS_BUTTON TBSTYLE_BUTTON
#define BTNS_SEP TBSTYLE_SEP
#define BTNS_CHECK TBSTYLE_CHECK
#define BTNS_GROUP TBSTYLE_GROUP
#define BTNS_CHECKGROUP TBSTYLE_CHECKGROUP
#define BTNS_DROPDOWN TBSTYLE_DROPDOWN
#define BTNS_AUTOSIZE TBSTYLE_AUTOSIZE
#define BTNS_NOPREFIX TBSTYLE_NOPREFIX
#define BTNS_SHOWTEXT 0x0040
#define BTNS_WHOLEDROPDOWN 0x0080

#define TBSTYLE_EX_DRAWDDARROWS 0x00000001
#define TBSTYLE_EX_MIXEDBUTTONS 0x00000008
#define TBSTYLE_EX_HIDECLIPPEDBUTTONS 0x00000010
#define TBSTYLE_EX_DOUBLEBUFFER 0x00000080

#define TB_ENABLEBUTTON (WM_USER + 1)
#define TB_CHECKBUTTON (WM_USER + 2)
#define TB_PRESSBUTTON (WM_USER + 3)
#define TB_HIDEBUTTON (WM_USER + 4)
#define TB_INDETERMINATE (WM_USER + 5)
#define TB_MARKBUTTON (WM_USER + 6)
#define TB_ISBUTTONENABLED (WM_USER + 9)
#define TB_ISBUTTONCHECKED (WM_USER + 10)
#define TB_ISBUTTONPRESSED (WM_USER + 11)
#define TB_ISBUTTONHIDDEN (WM_USER + 12)
#define TB_ISBUTTONINDETERMINATE (WM_USER + 13)
#define TB_ISBUTTONHIGHLIGHTED (WM_USER + 14)
#define TB_SETSTATE (WM_USER + 17)
#define TB_GETSTATE (WM_USER + 18)
#define TB_ADDBITMAP (WM_USER + 19)
#define TB_ADDBUTTONSA (WM_USER + 20)
#define TB_INSERTBUTTONA (WM_USER + 21)
#define TB_DELETEBUTTON (WM_USER + 22)
#define TB_GETBUTTON (WM_USER + 23)
#define TB_BUTTONCOUNT (WM_USER + 24)
#define TB_COMMANDTOINDEX (WM_USER + 25)
#define TB_CUSTOMIZE (WM_USER + 27)
#define TB_ADDSTRINGA (WM_USER + 28)
#define TB_GETITEMRECT (WM_USER + 29)
#define TB_BUTTONSTRUCTSIZE (WM_USER + 30)
#define TB_SETBUTTONSIZE (WM_USER + 31)
#define TB_SETBITMAPSIZE (WM_USER + 32)
#define TB_AUTOSIZE (WM_USER + 33)
#define TB_GETTOOLTIPS (WM_USER + 35)
#define TB_SETTOOLTIPS (WM_USER + 36)
#define TB_SETPARENT (WM_USER + 37)
#define TB_SETROWS (WM_USER + 39)
#define TB_GETROWS (WM_USER + 40)
#define TB_SETCMDID (WM_USER + 42)
#define TB_CHANGEBITMAP (WM_USER + 43)
#define TB_GETBITMAP (WM_USER + 44)
#define TB_GETBUTTONTEXTA (WM_USER + 45)
#define TB_REPLACEBITMAP (WM_USER + 46)
#define TB_SETINDENT (WM_USER + 47)
#define TB_SETIMAGELIST (WM_USER + 48)
#define TB_GETIMAGELIST (WM_USER + 49)
#define TB_LOADIMAGES (WM_USER + 50)
#define TB_GETRECT (WM_USER + 51)
#define TB_SETHOTIMAGELIST (WM_USER + 52)
#define TB_GETHOTIMAGELIST (WM_USER + 53)
#define TB_SETDISABLEDIMAGELIST (WM_USER + 54)
#define TB_GETDISABLEDIMAGELIST (WM_USER + 55)
#define TB_SETSTYLE (WM_USER + 56)
#define TB_GETSTYLE (WM_USER + 57)
#define TB_GETBUTTONSIZE (WM_USER + 58)
#define TB_SETBUTTONWIDTH (WM_USER + 59)
#define TB_SETMAXTEXTROWS (WM_USER + 60)
#define TB_GETTEXTROWS (WM_USER + 61)
#define TB_GETHOTITEM (WM_USER + 71)
#define TB_SETHOTITEM (WM_USER + 72)
#define TB_GETBUTTONINFOW (WM_USER + 63)
#define TB_SETBUTTONINFOW (WM_USER + 64)
#define TB_GETBUTTONINFOA (WM_USER + 65)
#define TB_SETBUTTONINFOA (WM_USER + 66)
#define TB_INSERTBUTTONW (WM_USER + 67)
#define TB_ADDBUTTONSW (WM_USER + 68)
#define TB_HITTEST (WM_USER + 69)
#define TB_SETDRAWTEXTFLAGS (WM_USER + 70)
#define TB_GETBUTTONTEXTW (WM_USER + 75)
#define TB_ADDSTRINGW (WM_USER + 77)
#define TB_SETEXTENDEDSTYLE (WM_USER + 84)
#define TB_GETEXTENDEDSTYLE (WM_USER + 85)
#define TB_GETPADDING (WM_USER + 86)
#define TB_SETPADDING (WM_USER + 87)
#define TB_GETMAXSIZE (WM_USER + 83)
#define TB_ADDBUTTONS TB_ADDBUTTONSW
#define TB_INSERTBUTTON TB_INSERTBUTTONW

#define TBN_GETBUTTONINFOA (TBN_FIRST - 0)
#define TBN_BEGINDRAG (TBN_FIRST - 1)
#define TBN_ENDDRAG (TBN_FIRST - 2)
#define TBN_DROPDOWN (TBN_FIRST - 10)
#define TBN_HOTITEMCHANGE (TBN_FIRST - 13)

typedef struct tagNMTOOLBARW {
    NMHDR hdr;
    int iItem;
    TBBUTTON tbButton;
    int cchText;
    LPWSTR pszText;
    RECT rcButton;
} NMTOOLBARW, *LPNMTOOLBARW;

WINCOMMCTRLAPI HWND WINAPI CreateToolbarEx(HWND hwnd, DWORD style, UINT id, int bitmaps, HINSTANCE bm_inst,
                                           UINT_PTR bm_id, LPCTBBUTTON buttons, int count, int dx_button,
                                           int dy_button, int dx_bitmap, int dy_bitmap, UINT struct_size);

/* ------------------------------------------------------------------ task dialogs */

#pragma pack(push, 1)

typedef HRESULT(CALLBACK *PFTASKDIALOGCALLBACK)(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                                LONG_PTR ref_data);

enum _TASKDIALOG_FLAGS {
    TDF_ENABLE_HYPERLINKS = 0x0001,
    TDF_USE_HICON_MAIN = 0x0002,
    TDF_USE_HICON_FOOTER = 0x0004,
    TDF_ALLOW_DIALOG_CANCELLATION = 0x0008,
    TDF_USE_COMMAND_LINKS = 0x0010,
    TDF_USE_COMMAND_LINKS_NO_ICON = 0x0020,
    TDF_EXPAND_FOOTER_AREA = 0x0040,
    TDF_EXPANDED_BY_DEFAULT = 0x0080,
    TDF_VERIFICATION_FLAG_CHECKED = 0x0100,
    TDF_SHOW_PROGRESS_BAR = 0x0200,
    TDF_SHOW_MARQUEE_PROGRESS_BAR = 0x0400,
    TDF_CALLBACK_TIMER = 0x0800,
    TDF_POSITION_RELATIVE_TO_WINDOW = 0x1000,
    TDF_RTL_LAYOUT = 0x2000,
    TDF_NO_DEFAULT_RADIO_BUTTON = 0x4000,
    TDF_CAN_BE_MINIMIZED = 0x8000,
    TDF_NO_SET_FOREGROUND = 0x00010000,
    TDF_SIZE_TO_CONTENT = 0x01000000,
};
typedef int TASKDIALOG_FLAGS;

typedef enum _TASKDIALOG_MESSAGES {
    TDM_NAVIGATE_PAGE = WM_USER + 101,
    TDM_CLICK_BUTTON = WM_USER + 102,
    TDM_SET_MARQUEE_PROGRESS_BAR = WM_USER + 103,
    TDM_SET_PROGRESS_BAR_STATE = WM_USER + 104,
    TDM_SET_PROGRESS_BAR_RANGE = WM_USER + 105,
    TDM_SET_PROGRESS_BAR_POS = WM_USER + 106,
    TDM_SET_PROGRESS_BAR_MARQUEE = WM_USER + 107,
    TDM_SET_ELEMENT_TEXT = WM_USER + 108,
    TDM_CLICK_RADIO_BUTTON = WM_USER + 110,
    TDM_ENABLE_BUTTON = WM_USER + 111,
    TDM_ENABLE_RADIO_BUTTON = WM_USER + 112,
    TDM_CLICK_VERIFICATION = WM_USER + 113,
    TDM_UPDATE_ELEMENT_TEXT = WM_USER + 114,
    TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE = WM_USER + 115,
    TDM_UPDATE_ICON = WM_USER + 116,
} TASKDIALOG_MESSAGES;

typedef enum _TASKDIALOG_NOTIFICATIONS {
    TDN_CREATED = 0,
    TDN_NAVIGATED = 1,
    TDN_BUTTON_CLICKED = 2,
    TDN_HYPERLINK_CLICKED = 3,
    TDN_TIMER = 4,
    TDN_DESTROYED = 5,
    TDN_RADIO_BUTTON_CLICKED = 6,
    TDN_DIALOG_CONSTRUCTED = 7,
    TDN_VERIFICATION_CLICKED = 8,
    TDN_HELP = 9,
    TDN_EXPANDO_BUTTON_CLICKED = 10,
} TASKDIALOG_NOTIFICATIONS;

typedef struct _TASKDIALOG_BUTTON {
    int nButtonID;
    PCWSTR pszButtonText;
} TASKDIALOG_BUTTON;

typedef enum _TASKDIALOG_ELEMENTS {
    TDE_CONTENT,
    TDE_EXPANDED_INFORMATION,
    TDE_FOOTER,
    TDE_MAIN_INSTRUCTION,
} TASKDIALOG_ELEMENTS;

typedef enum _TASKDIALOG_ICON_ELEMENTS {
    TDIE_ICON_MAIN,
    TDIE_ICON_FOOTER,
} TASKDIALOG_ICON_ELEMENTS;

#define TD_WARNING_ICON MAKEINTRESOURCEW(-1)
#define TD_ERROR_ICON MAKEINTRESOURCEW(-2)
#define TD_INFORMATION_ICON MAKEINTRESOURCEW(-3)
#define TD_SHIELD_ICON MAKEINTRESOURCEW(-4)

enum _TASKDIALOG_COMMON_BUTTON_FLAGS {
    TDCBF_OK_BUTTON = 0x0001,
    TDCBF_YES_BUTTON = 0x0002,
    TDCBF_NO_BUTTON = 0x0004,
    TDCBF_CANCEL_BUTTON = 0x0008,
    TDCBF_RETRY_BUTTON = 0x0010,
    TDCBF_CLOSE_BUTTON = 0x0020,
};
typedef int TASKDIALOG_COMMON_BUTTON_FLAGS;

typedef struct _TASKDIALOGCONFIG {
    UINT cbSize;
    HWND hwndParent;
    HINSTANCE hInstance;
    TASKDIALOG_FLAGS dwFlags;
    TASKDIALOG_COMMON_BUTTON_FLAGS dwCommonButtons;
    PCWSTR pszWindowTitle;
    union {
        HICON hMainIcon;
        PCWSTR pszMainIcon;
    };
    PCWSTR pszMainInstruction;
    PCWSTR pszContent;
    UINT cButtons;
    const TASKDIALOG_BUTTON *pButtons;
    int nDefaultButton;
    UINT cRadioButtons;
    const TASKDIALOG_BUTTON *pRadioButtons;
    int nDefaultRadioButton;
    PCWSTR pszVerificationText;
    PCWSTR pszExpandedInformation;
    PCWSTR pszExpandedControlText;
    PCWSTR pszCollapsedControlText;
    union {
        HICON hFooterIcon;
        PCWSTR pszFooterIcon;
    };
    PCWSTR pszFooter;
    PFTASKDIALOGCALLBACK pfCallback;
    LONG_PTR lpCallbackData;
    UINT cxWidth;
} TASKDIALOGCONFIG;

#pragma pack(pop)

WINCOMMCTRLAPI HRESULT WINAPI TaskDialogIndirect(const TASKDIALOGCONFIG *config, int *button, int *radio_button,
                                                 BOOL *verification);
WINCOMMCTRLAPI HRESULT WINAPI TaskDialog(HWND parent, HINSTANCE inst, PCWSTR title, PCWSTR instruction,
                                         PCWSTR content, TASKDIALOG_COMMON_BUTTON_FLAGS buttons, PCWSTR icon,
                                         int *button);

/* ------------------------------------------------------------------ dynamic arrays */

struct _DSA;
typedef struct _DSA *HDSA;
struct _DPA;
typedef struct _DPA *HDPA;

#define DSA_APPEND 0x7fffffff
#define DSA_ERR (-1)
#define DPA_APPEND 0x7fffffff
#define DPA_ERR (-1)
#define DPAS_SORTED 0x0001
#define DPAS_INSERTBEFORE 0x0002
#define DPAS_INSERTAFTER 0x0004

typedef int(CALLBACK *PFNDAENUMCALLBACK)(void *p, void *data);
typedef int(CALLBACK *PFNDACOMPARE)(void *p1, void *p2, LPARAM param);
typedef PFNDAENUMCALLBACK PFNDPAENUMCALLBACK;
typedef PFNDAENUMCALLBACK PFNDSAENUMCALLBACK;
typedef PFNDACOMPARE PFNDPACOMPARE;
typedef PFNDACOMPARE PFNDSACOMPARE;

#define DSA_GetItemCount(hdsa) (*(int *)(hdsa))
#define DPA_GetPtrCount(hdpa) (*(int *)(hdpa))
#define DPA_GetPtrPtr(hdpa) (*((void ***)((BYTE *)(hdpa) + sizeof(void *))))
#define DPA_FastGetPtr(hdpa, i) (DPA_GetPtrPtr(hdpa)[i])
#define DPA_AppendPtr(hdpa, p) DPA_InsertPtr(hdpa, DA_LAST, p)
#define DA_LAST 0x7FFFFFFF

WINCOMMCTRLAPI HDSA WINAPI DSA_Create(int item_size, int grow);
WINCOMMCTRLAPI BOOL WINAPI DSA_Destroy(HDSA hdsa);
WINCOMMCTRLAPI void WINAPI DSA_DestroyCallback(HDSA hdsa, PFNDAENUMCALLBACK callback, void *data);
WINCOMMCTRLAPI void WINAPI DSA_EnumCallback(HDSA hdsa, PFNDAENUMCALLBACK callback, void *data);
WINCOMMCTRLAPI BOOL WINAPI DSA_GetItem(HDSA hdsa, int i, void *item);
WINCOMMCTRLAPI void *WINAPI DSA_GetItemPtr(HDSA hdsa, int i);
WINCOMMCTRLAPI int WINAPI DSA_InsertItem(HDSA hdsa, int i, const void *item);
WINCOMMCTRLAPI BOOL WINAPI DSA_SetItem(HDSA hdsa, int i, const void *item);
WINCOMMCTRLAPI BOOL WINAPI DSA_DeleteItem(HDSA hdsa, int i);
WINCOMMCTRLAPI BOOL WINAPI DSA_DeleteAllItems(HDSA hdsa);
WINCOMMCTRLAPI HDSA WINAPI DSA_Clone(HDSA hdsa);
WINCOMMCTRLAPI BOOL WINAPI DSA_Sort(HDSA hdsa, PFNDACOMPARE compare, LPARAM param);
WINCOMMCTRLAPI ULONGLONG WINAPI DSA_GetSize(HDSA hdsa);
WINCOMMCTRLAPI HDPA WINAPI DPA_Create(int grow);
WINCOMMCTRLAPI HDPA WINAPI DPA_CreateEx(int grow, HANDLE heap);
WINCOMMCTRLAPI HDPA WINAPI DPA_Clone(HDPA hdpa, HDPA into);
WINCOMMCTRLAPI BOOL WINAPI DPA_Destroy(HDPA hdpa);
WINCOMMCTRLAPI void WINAPI DPA_DestroyCallback(HDPA hdpa, PFNDAENUMCALLBACK callback, void *data);
WINCOMMCTRLAPI void *WINAPI DPA_DeletePtr(HDPA hdpa, int i);
WINCOMMCTRLAPI BOOL WINAPI DPA_DeleteAllPtrs(HDPA hdpa);
WINCOMMCTRLAPI void WINAPI DPA_EnumCallback(HDPA hdpa, PFNDAENUMCALLBACK callback, void *data);
WINCOMMCTRLAPI BOOL WINAPI DPA_Grow(HDPA hdpa, int count);
WINCOMMCTRLAPI int WINAPI DPA_InsertPtr(HDPA hdpa, int i, void *p);
WINCOMMCTRLAPI BOOL WINAPI DPA_SetPtr(HDPA hdpa, int i, void *p);
WINCOMMCTRLAPI void *WINAPI DPA_GetPtr(HDPA hdpa, INT_PTR i);
WINCOMMCTRLAPI int WINAPI DPA_GetPtrIndex(HDPA hdpa, const void *p);
WINCOMMCTRLAPI BOOL WINAPI DPA_Sort(HDPA hdpa, PFNDACOMPARE compare, LPARAM param);
WINCOMMCTRLAPI int WINAPI DPA_Search(HDPA hdpa, void *find, int start, PFNDACOMPARE compare, LPARAM param,
                                     UINT options);
WINCOMMCTRLAPI ULONGLONG WINAPI DPA_GetSize(HDPA hdpa);
WINCOMMCTRLAPI BOOL WINAPI Str_SetPtrW(LPWSTR *slot, LPCWSTR text);

/* ------------------------------------------------------------------ miscellaneous */

WINCOMMCTRLAPI void WINAPI GetEffectiveClientRect(HWND hwnd, LPRECT rect, const INT *info);
WINCOMMCTRLAPI HRESULT WINAPI LoadIconMetric(HINSTANCE inst, PCWSTR name, int metric, HICON *icon);
WINCOMMCTRLAPI HRESULT WINAPI LoadIconWithScaleDown(HINSTANCE inst, PCWSTR name, int cx, int cy, HICON *icon);
WINCOMMCTRLAPI int WINAPI DrawShadowText(HDC dc, LPCWSTR text, UINT count, RECT *rect, DWORD flags, COLORREF text_color,
                                         COLORREF shadow, int dx, int dy);

#define LIM_SMALL 0
#define LIM_LARGE 1

#endif
