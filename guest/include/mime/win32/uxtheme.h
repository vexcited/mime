/*
 * visual styles api of uxtheme.dll.
 */
#ifndef MIME_WIN32_UXTHEME_H
#define MIME_WIN32_UXTHEME_H

#include <mime/win32/winuser.h>

#ifdef MIME_BUILD_UXTHEME
#define THEMEAPI DECLSPEC_EXPORT HRESULT WINAPI
#define THEMEAPI_(type) DECLSPEC_EXPORT type WINAPI
#else
#define THEMEAPI DECLSPEC_IMPORT HRESULT WINAPI
#define THEMEAPI_(type) DECLSPEC_IMPORT type WINAPI
#endif

typedef HANDLE HTHEME;
typedef HANDLE HPAINTBUFFER;
typedef HANDLE HANIMATIONBUFFER;

#define MAX_THEMECOLOR 64
#define MAX_THEMESIZE 64
#define MAX_INTLIST_COUNT 402

typedef struct _MARGINS {
    int cxLeftWidth;
    int cxRightWidth;
    int cyTopHeight;
    int cyBottomHeight;
} MARGINS, *PMARGINS;

typedef struct _INTLIST {
    int iValueCount;
    int iValues[MAX_INTLIST_COUNT];
} INTLIST, *PINTLIST;

typedef enum THEMESIZE {
    TS_MIN,
    TS_TRUE,
    TS_DRAW,
} THEMESIZE;

typedef enum PROPERTYORIGIN {
    PO_STATE,
    PO_PART,
    PO_CLASS,
    PO_GLOBAL,
    PO_NOTFOUND,
} PROPERTYORIGIN;

/* OpenThemeDataEx flags. */
#define OTD_FORCE_RECT_SIZING 0x00000001
#define OTD_NONCLIENT 0x00000002
#define OTD_VALIDBITS (OTD_FORCE_RECT_SIZING | OTD_NONCLIENT)

/* DrawThemeBackgroundEx options. */
#define DTBG_CLIPRECT 0x00000001
#define DTBG_DRAWSOLID 0x00000002
#define DTBG_OMITBORDER 0x00000004
#define DTBG_OMITCONTENT 0x00000008
#define DTBG_COMPUTINGREGION 0x00000010
#define DTBG_MIRRORDC 0x00000020
#define DTBG_NOMIRROR 0x00000040
#define DTBG_VALIDBITS 0x0000007F

typedef struct _DTBGOPTS {
    DWORD dwSize;
    DWORD dwFlags;
    RECT rcClip;
} DTBGOPTS, *PDTBGOPTS;

/* DrawThemeTextEx options. */
#define DTT_TEXTCOLOR (1UL << 0)
#define DTT_BORDERCOLOR (1UL << 1)
#define DTT_SHADOWCOLOR (1UL << 2)
#define DTT_SHADOWTYPE (1UL << 3)
#define DTT_SHADOWOFFSET (1UL << 4)
#define DTT_BORDERSIZE (1UL << 5)
#define DTT_FONTPROP (1UL << 6)
#define DTT_COLORPROP (1UL << 7)
#define DTT_STATEID (1UL << 8)
#define DTT_CALCRECT (1UL << 9)
#define DTT_APPLYOVERLAY (1UL << 10)
#define DTT_GLOWSIZE (1UL << 11)
#define DTT_CALLBACK (1UL << 12)
#define DTT_COMPOSITED (1UL << 13)
#define DTT_VALIDBITS 0x00003FFF
#define DTT_GRAYED 0x00000001
#define DTT_FLAGS2VALIDBITS DTT_GRAYED

typedef int(WINAPI *DTT_CALLBACK_PROC)(HDC hdc, LPWSTR text, int count, LPRECT rect, UINT flags, LPARAM param);

typedef struct _DTTOPTS {
    DWORD dwSize;
    DWORD dwFlags;
    COLORREF crText;
    COLORREF crBorder;
    COLORREF crShadow;
    int iTextShadowType;
    POINT ptShadowOffset;
    int iBorderSize;
    int iFontPropId;
    int iColorPropId;
    int iStateId;
    BOOL fApplyOverlay;
    int iGlowSize;
    DTT_CALLBACK_PROC pfnDrawTextCallback;
    LPARAM lParam;
} DTTOPTS, *PDTTOPTS;

/* HitTestThemeBackground options. */
#define HTTB_BACKGROUNDSEG 0x00000000
#define HTTB_FIXEDBORDER 0x00000002
#define HTTB_CAPTION 0x00000004
#define HTTB_RESIZINGBORDER_LEFT 0x00000010
#define HTTB_RESIZINGBORDER_TOP 0x00000020
#define HTTB_RESIZINGBORDER_RIGHT 0x00000040
#define HTTB_RESIZINGBORDER_BOTTOM 0x00000080
#define HTTB_RESIZINGBORDER                                                                                         \
    (HTTB_RESIZINGBORDER_LEFT | HTTB_RESIZINGBORDER_TOP | HTTB_RESIZINGBORDER_RIGHT | HTTB_RESIZINGBORDER_BOTTOM)
#define HTTB_SIZINGTEMPLATE 0x00000100
#define HTTB_SYSTEMSIZINGMARGINS 0x00000200

/* theme app properties. */
#define STAP_ALLOW_NONCLIENT (1UL << 0)
#define STAP_ALLOW_CONTROLS (1UL << 1)
#define STAP_ALLOW_WEBCONTENT (1UL << 2)
#define STAP_VALIDBITS (STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS | STAP_ALLOW_WEBCONTENT)

/* EnableThemeDialogTexture flags. */
#define ETDT_DISABLE 0x00000001
#define ETDT_ENABLE 0x00000002
#define ETDT_USETABTEXTURE 0x00000004
#define ETDT_ENABLETAB (ETDT_ENABLE | ETDT_USETABTEXTURE)
#define ETDT_USEAEROWIZARDTABTEXTURE 0x00000008
#define ETDT_ENABLEAEROWIZARDTAB (ETDT_ENABLE | ETDT_USEAEROWIZARDTABTEXTURE)
#define ETDT_VALIDBITS (ETDT_DISABLE | ETDT_ENABLE | ETDT_USETABTEXTURE | ETDT_USEAEROWIZARDTABTEXTURE)

/* GetThemeBitmap flags. */
#define GBF_DIRECT 0x00000001
#define GBF_COPY 0x00000002
#define GBF_VALIDBITS (GBF_DIRECT | GBF_COPY)

/* DrawThemeIcon and DrawThemeEdge share the DrawEdge flags; this one is uxtheme specific. */
#define DTPB_WINDOWDC 0x00000001
#define DTPB_USECTLCOLORSTATIC 0x00000002
#define DTPB_USEERASEBKGND 0x00000004

/* buffered paint. */
typedef enum _BP_BUFFERFORMAT {
    BPBF_COMPATIBLEBITMAP,
    BPBF_DIB,
    BPBF_TOPDOWNDIB,
    BPBF_TOPDOWNMONODIB,
} BP_BUFFERFORMAT;

#define BPBF_COMPOSITED BPBF_TOPDOWNDIB

typedef enum _BP_ANIMATIONSTYLE {
    BPAS_NONE,
    BPAS_LINEAR,
    BPAS_CUBIC,
    BPAS_SINE,
} BP_ANIMATIONSTYLE;

typedef struct _BP_ANIMATIONPARAMS {
    DWORD cbSize;
    DWORD dwFlags;
    BP_ANIMATIONSTYLE style;
    DWORD dwDuration;
} BP_ANIMATIONPARAMS, *PBP_ANIMATIONPARAMS;

#define BPPF_ERASE 0x0001
#define BPPF_NOCLIP 0x0002
#define BPPF_NONCLIENT 0x0004

typedef struct _BP_PAINTPARAMS {
    DWORD cbSize;
    DWORD dwFlags;
    const RECT *prcExclude;
    const BLENDFUNCTION *pBlendFunction;
} BP_PAINTPARAMS, *PBP_PAINTPARAMS;

typedef enum WINDOWTHEMEATTRIBUTETYPE {
    WTA_NONCLIENT = 1,
} WINDOWTHEMEATTRIBUTETYPE;

typedef struct _WTA_OPTIONS {
    DWORD dwFlags;
    DWORD dwMask;
} WTA_OPTIONS, *PWTA_OPTIONS;

#define WTNCA_NODRAWCAPTION 0x00000001
#define WTNCA_NODRAWICON 0x00000002
#define WTNCA_NOSYSMENU 0x00000004
#define WTNCA_NOMIRRORHELP 0x00000008
#define WTNCA_VALIDBITS (WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU | WTNCA_NOMIRRORHELP)

THEMEAPI_(HTHEME) OpenThemeData(HWND hwnd, LPCWSTR class_list);
THEMEAPI_(HTHEME) OpenThemeDataEx(HWND hwnd, LPCWSTR class_list, DWORD flags);
THEMEAPI_(HTHEME) OpenThemeDataForDpi(HWND hwnd, LPCWSTR class_list, UINT dpi);
THEMEAPI CloseThemeData(HTHEME theme);
THEMEAPI_(HTHEME) GetWindowTheme(HWND hwnd);
THEMEAPI SetWindowTheme(HWND hwnd, LPCWSTR sub_app, LPCWSTR sub_id_list);
THEMEAPI SetWindowThemeAttribute(HWND hwnd, enum WINDOWTHEMEATTRIBUTETYPE type, PVOID attribute, DWORD size);
THEMEAPI_(BOOL) IsThemeActive(void);
THEMEAPI_(BOOL) IsAppThemed(void);
THEMEAPI_(BOOL) IsCompositionActive(void);
THEMEAPI_(DWORD) GetThemeAppProperties(void);
THEMEAPI_(void) SetThemeAppProperties(DWORD flags);
THEMEAPI EnableTheming(BOOL enable);
THEMEAPI EnableThemeDialogTexture(HWND hwnd, DWORD flags);
THEMEAPI_(BOOL) IsThemeDialogTextureEnabled(HWND hwnd);
THEMEAPI GetCurrentThemeName(LPWSTR name, int name_size, LPWSTR color, int color_size, LPWSTR size, int size_size);
THEMEAPI GetThemeDocumentationProperty(LPCWSTR theme_name, LPCWSTR prop_name, LPWSTR value, int value_size);

THEMEAPI DrawThemeBackground(HTHEME theme, HDC dc, int part, int state, LPCRECT rect, LPCRECT clip);
THEMEAPI DrawThemeBackgroundEx(HTHEME theme, HDC dc, int part, int state, LPCRECT rect, const DTBGOPTS *options);
THEMEAPI DrawThemeParentBackground(HWND hwnd, HDC dc, const RECT *rect);
THEMEAPI DrawThemeParentBackgroundEx(HWND hwnd, HDC dc, DWORD flags, const RECT *rect);
THEMEAPI DrawThemeEdge(HTHEME theme, HDC dc, int part, int state, LPCRECT dest, UINT edge, UINT flags,
                       LPRECT content);
THEMEAPI DrawThemeIcon(HTHEME theme, HDC dc, int part, int state, LPCRECT rect, HANDLE image_list, int index);
THEMEAPI DrawThemeText(HTHEME theme, HDC dc, int part, int state, LPCWSTR text, int count, DWORD flags,
                       DWORD flags2, LPCRECT rect);
THEMEAPI DrawThemeTextEx(HTHEME theme, HDC dc, int part, int state, LPCWSTR text, int count, DWORD flags,
                         LPRECT rect, const DTTOPTS *options);
THEMEAPI GetThemeTextExtent(HTHEME theme, HDC dc, int part, int state, LPCWSTR text, int count, DWORD flags,
                            LPCRECT bounds, LPRECT extent);
THEMEAPI GetThemeTextMetrics(HTHEME theme, HDC dc, int part, int state, TEXTMETRICW *metrics);
THEMEAPI GetThemeBackgroundContentRect(HTHEME theme, HDC dc, int part, int state, LPCRECT bounds, LPRECT content);
THEMEAPI GetThemeBackgroundExtent(HTHEME theme, HDC dc, int part, int state, LPCRECT content, LPRECT extent);
THEMEAPI GetThemeBackgroundRegion(HTHEME theme, HDC dc, int part, int state, LPCRECT rect, HRGN *region);
THEMEAPI GetThemePartSize(HTHEME theme, HDC dc, int part, int state, LPCRECT rect, enum THEMESIZE size_type,
                          SIZE *size);
THEMEAPI HitTestThemeBackground(HTHEME theme, HDC dc, int part, int state, DWORD options, LPCRECT rect,
                                HRGN region, POINT test, WORD *code);
THEMEAPI_(BOOL) IsThemeBackgroundPartiallyTransparent(HTHEME theme, int part, int state);
THEMEAPI_(BOOL) IsThemePartDefined(HTHEME theme, int part, int state);

THEMEAPI GetThemeColor(HTHEME theme, int part, int state, int prop, COLORREF *color);
THEMEAPI GetThemeMetric(HTHEME theme, HDC dc, int part, int state, int prop, int *value);
THEMEAPI GetThemeString(HTHEME theme, int part, int state, int prop, LPWSTR buffer, int size);
THEMEAPI GetThemeBool(HTHEME theme, int part, int state, int prop, BOOL *value);
THEMEAPI GetThemeInt(HTHEME theme, int part, int state, int prop, int *value);
THEMEAPI GetThemeEnumValue(HTHEME theme, int part, int state, int prop, int *value);
THEMEAPI GetThemePosition(HTHEME theme, int part, int state, int prop, POINT *point);
THEMEAPI GetThemeFont(HTHEME theme, HDC dc, int part, int state, int prop, LOGFONTW *font);
THEMEAPI GetThemeRect(HTHEME theme, int part, int state, int prop, LPRECT rect);
THEMEAPI GetThemeMargins(HTHEME theme, HDC dc, int part, int state, int prop, LPCRECT rect, MARGINS *margins);
THEMEAPI GetThemeIntList(HTHEME theme, int part, int state, int prop, INTLIST *list);
THEMEAPI GetThemePropertyOrigin(HTHEME theme, int part, int state, int prop, enum PROPERTYORIGIN *origin);
THEMEAPI GetThemeFilename(HTHEME theme, int part, int state, int prop, LPWSTR buffer, int size);
THEMEAPI GetThemeBitmap(HTHEME theme, int part, int state, int prop, ULONG flags, HBITMAP *bitmap);
THEMEAPI GetThemeStream(HTHEME theme, int part, int state, int prop, void **stream, DWORD *size, HINSTANCE inst);
THEMEAPI GetThemeTransitionDuration(HTHEME theme, int part, int state_from, int state_to, int prop,
                                    DWORD *duration);

THEMEAPI_(COLORREF) GetThemeSysColor(HTHEME theme, int color);
THEMEAPI_(HBRUSH) GetThemeSysColorBrush(HTHEME theme, int color);
THEMEAPI_(BOOL) GetThemeSysBool(HTHEME theme, int bool_id);
THEMEAPI_(int) GetThemeSysSize(HTHEME theme, int size_id);
THEMEAPI GetThemeSysFont(HTHEME theme, int font_id, LOGFONTW *font);
THEMEAPI GetThemeSysString(HTHEME theme, int string_id, LPWSTR buffer, int size);
THEMEAPI GetThemeSysInt(HTHEME theme, int int_id, int *value);

THEMEAPI BufferedPaintInit(void);
THEMEAPI BufferedPaintUnInit(void);
THEMEAPI_(HPAINTBUFFER) BeginBufferedPaint(HDC target, const RECT *rect, BP_BUFFERFORMAT format,
                                           BP_PAINTPARAMS *params, HDC *dc);
THEMEAPI EndBufferedPaint(HPAINTBUFFER buffer, BOOL update_target);
THEMEAPI GetBufferedPaintTargetRect(HPAINTBUFFER buffer, RECT *rect);
THEMEAPI_(HDC) GetBufferedPaintTargetDC(HPAINTBUFFER buffer);
THEMEAPI_(HDC) GetBufferedPaintDC(HPAINTBUFFER buffer);
THEMEAPI GetBufferedPaintBits(HPAINTBUFFER buffer, RGBQUAD **bits, int *row_pixels);
THEMEAPI BufferedPaintClear(HPAINTBUFFER buffer, const RECT *rect);
THEMEAPI BufferedPaintSetAlpha(HPAINTBUFFER buffer, const RECT *rect, BYTE alpha);
THEMEAPI_(HANIMATIONBUFFER) BeginBufferedAnimation(HWND hwnd, HDC target, const RECT *rect,
                                                   BP_BUFFERFORMAT format, BP_PAINTPARAMS *paint_params,
                                                   BP_ANIMATIONPARAMS *animation_params, HDC *from, HDC *to);
THEMEAPI EndBufferedAnimation(HANIMATIONBUFFER buffer, BOOL update_target);
THEMEAPI_(BOOL) BufferedPaintRenderAnimation(HWND hwnd, HDC target);
THEMEAPI BufferedPaintStopAllAnimations(HWND hwnd);

#endif
