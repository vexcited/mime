#ifndef MIME_COMCTL32_PRIVATE_H
#define MIME_COMCTL32_PRIVATE_H

#define WINCOMMCTRLAPI DECLSPEC_EXPORT
#include <mime/win32/commctrl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/uxtheme.h>
#include <mime/win32/vssym32.h>
#include <rtl.h>
#include <limits.h>

/* user32 exports that winuser.h does not declare yet. */
WINUSERAPI DWORD WINAPI GetWindowThreadProcessId(HWND hwnd, LPDWORD process);
WINUSERAPI HWND WINAPI CreateDialogIndirectParamA(HINSTANCE instance, LPCDLGTEMPLATEA tmpl, HWND parent, DLGPROC proc,
                                                  LPARAM param);

/* 32-bit windows has no pointer sized window longs; its headers map the macros to the 32-bit ones. */
#ifndef _WIN64
#define GetWindowLongPtrW GetWindowLongW
#define SetWindowLongPtrW(hwnd, index, value) SetWindowLongW(hwnd, index, (LONG)(value))
#endif

extern HINSTANCE Comctl32Instance;

void SubclassProcessAttach(void);
void SubclassProcessFree(void);

/* registers the classes the ICC bits select. */
void RegisterControlClasses(DWORD icc);
/* the static control, registered when user32 asks for it by name: see static.c. */
BOOL RegisterStaticClass(void);
/* registers a class under the name of one of user32, with its layout and proc in front of the one returned. */
WNDPROC CcRegisterOverUser(LPCWSTR name, WNDPROC proc);
BOOL RegisterFieldClass(LPCWSTR name);

void *CcAlloc(SIZE_T size);
void *CcReAlloc(void *p, SIZE_T size);
void CcFree(void *p);
LPWSTR CcStrDupW(LPCWSTR s);
LPWSTR CcStrDupAtoW(LPCSTR s);
/* replaces *slot with a copy of text, keeping LPSTR_TEXTCALLBACKW as is. */
BOOL CcSetText(LPWSTR *slot, LPCWSTR text);
BOOL CcSetTextA(LPWSTR *slot, LPCSTR text);
/* copies into a buffer of max characters, terminated; returns the characters copied. */
int CcCopyText(LPWSTR dst, int max, LPCWSTR src);
int CcCopyTextA(LPSTR dst, int max, LPCWSTR src);

static inline int CcAbs(int v)
{
    return v < 0 ? -v : v;
}

static inline BOOL CcIsCallback(LPCWSTR text)
{
    return text == LPSTR_TEXTCALLBACKW;
}

/* controls notify their parent with WM_NOTIFY in the character set it asked for. */
typedef struct CC_NOTIFY {
    HWND Hwnd;
    HWND Parent;
    BOOL Unicode;
} CC_NOTIFY;

void CcNotifyInit(CC_NOTIFY *n, HWND hwnd, HWND parent);
/* ansi parents get the ansi twin of the code. */
LRESULT CcNotify(CC_NOTIFY *n, UINT code, NMHDR *hdr);
LRESULT CcNotifyHdr(CC_NOTIFY *n, UINT code);
/* handles WM_NOTIFYFORMAT sent to the control. */
LRESULT CcNotifyFormat(CC_NOTIFY *n, HWND from, LPARAM command);
/* NM_ click notifications with an NMMOUSE. */
LRESULT CcNotifyMouse(CC_NOTIFY *n, UINT code, DWORD_PTR item, DWORD_PTR data, POINT pt);

typedef struct CC_DRAW_BUFFER {
    HDC Target;
    HDC Dc;
    HBITMAP Bitmap;
    HGDIOBJ Old;
    RECT Rect;
} CC_DRAW_BUFFER;

/* paints into an off screen bitmap that CcEndBuffer copies to the target. */
HDC CcBeginBuffer(CC_DRAW_BUFFER *b, HDC target, const RECT *rect);
void CcEndBuffer(CC_DRAW_BUFFER *b);
void CcFillSolid(HDC dc, const RECT *rect, COLORREF color);
HFONT CcIconTitleFont(void);
BOOL CcFocusCuesHidden(HWND hwnd);
/* draws the client edge of a window as the border of its theme class, the 2 pixel edge in one pixel of border color. */
void CcDrawThemedEdge(HWND hwnd, HTHEME theme);
/* the theme class at the window's dpi, shared by the windows of the process, nullptr while the program is not themed. */
HTHEME CcClassTheme(HWND hwnd, LPCWSTR name);
/* draws the client edge of a window as one part and state of a theme class. */
void CcDrawThemedEdgePart(HWND hwnd, HTHEME theme, int part, int state);
/* flags a control keeps for a window whose extra bytes are not its own. zero forgets the window. */
UINT CcWindowFlags(HWND hwnd);
void CcSetWindowFlags(HWND hwnd, UINT flags);
/* scales 96 dpi pixels to the window's dpi. */
int CcScale(HWND hwnd, int value);
/* a magic number of a control, scaled by the dpi when the control scales them: see CcMagicScale in common.c. */
int CcMagicScale(HWND hwnd, BOOL forced, BOOL themed, int value);

/* theme part ids of the classes the controls draw. */
#define CC_PP_BAR 1
#define CC_PP_BARVERT 2
#define CC_PP_FILL 5
#define CC_PP_FILLVERT 6
#define CC_PP_PULSEOVERLAY 7
#define CC_PP_MOVEOVERLAY 8
#define CC_PP_PULSEOVERLAYVERT 9
#define CC_PP_MOVEOVERLAYVERT 10
#define CC_PP_TRANSPARENTBAR 11
#define CC_PP_TRANSPARENTBARVERT 12
#define CC_HP_HEADERITEM 1
#define CC_HP_HEADERSORTARROW 4
#define CC_LVP_LISTITEM 1
#define CC_SP_PANE 1
#define CC_SP_GRIPPERPANE 2
#define CC_SP_GRIPPER 3
#define CC_TTP_STANDARD 1
#define CC_SPNP_UP 1
#define CC_SPNP_DOWN 2
#define CC_SPNP_UPHORZ 3
#define CC_SPNP_DOWNHORZ 4
#define CC_TKP_TRACK 1
#define CC_TKP_TRACKVERT 2
#define CC_TKP_THUMB 3
#define CC_TKP_THUMBBOTTOM 4
#define CC_TKP_THUMBVERT 6
#define CC_TABP_TABITEM 1
#define CC_TABP_PANE 9
#define CC_TVP_TREEITEM 1
#define CC_TVP_GLYPH 2
#define CC_TP_BUTTON 1
#define CC_BP_PUSHBUTTON 1
#define CC_BP_RADIOBUTTON 2
#define CC_BP_CHECKBOX 3
#define CC_BP_GROUPBOX 4
#define CC_PBS_NORMAL 1
#define CC_PBS_HOT 2
#define CC_PBS_PRESSED 3
#define CC_PBS_DISABLED 4
#define CC_PBS_DEFAULTED 5
#define CC_EP_EDITBORDER_NOSCROLL 6
#define CC_LBCP_BORDER_HSCROLL 1
#define CC_LBCP_BORDER_HVSCROLL 2
#define CC_LBCP_BORDER_NOSCROLL 3
#define CC_LBCP_BORDER_VSCROLL 4
#define CC_CP_BORDER 4
#define CC_CP_READONLY 5
#define CC_CP_DROPDOWNBUTTONRIGHT 6

LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK AnimateProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK LinkProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK HeaderProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK ListViewProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK StatusProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK TooltipsProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK UpDownProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK TrackbarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK TabProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK TreeViewProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK RebarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

/* the width for an item's text, image and sort arrow, used by list view autosizing. */
int HeaderItemWidth(HWND header, int index);

#endif
