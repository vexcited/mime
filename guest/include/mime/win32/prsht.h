/* comctl32.dll property sheets and wizards: pages, headers, sheet messages and page notifications. */
#ifndef MIME_WIN32_PRSHT_H
#define MIME_WIN32_PRSHT_H

#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>

#ifndef WINCOMMCTRLAPI
#define WINCOMMCTRLAPI DECLSPEC_IMPORT
#endif

#ifndef MIME_HPROPSHEETPAGE_DEFINED
#define MIME_HPROPSHEETPAGE_DEFINED
typedef struct _PSP *HPROPSHEETPAGE;
#endif

/* ------------------------------------------------------------------ pages */

#define PSP_DEFAULT 0x00000000
#define PSP_DLGINDIRECT 0x00000001
#define PSP_USEHICON 0x00000002
#define PSP_USEICONID 0x00000004
#define PSP_USETITLE 0x00000008
#define PSP_RTLREADING 0x00000010
#define PSP_HASHELP 0x00000020
#define PSP_USEREFPARENT 0x00000040
#define PSP_USECALLBACK 0x00000080
#define PSP_PREMATURE 0x00000400
#define PSP_HIDEHEADER 0x00000800
#define PSP_USEHEADERTITLE 0x00001000
#define PSP_USEHEADERSUBTITLE 0x00002000
#define PSP_USEFUSIONCONTEXT 0x00004000

#define PSPCB_ADDREF 0
#define PSPCB_RELEASE 1
#define PSPCB_CREATE 2

struct _PROPSHEETPAGEA;
struct _PROPSHEETPAGEW;
typedef UINT(CALLBACK *LPFNPSPCALLBACKA)(HWND hwnd, UINT msg, struct _PROPSHEETPAGEA *page);
typedef UINT(CALLBACK *LPFNPSPCALLBACKW)(HWND hwnd, UINT msg, struct _PROPSHEETPAGEW *page);

/* the four sizes of a page structure only differ by what follows pcRefParent. */
#define MIME_PSP_V1(CHAR_T, CALLBACK_T)                                                                               \
    DWORD dwSize;                                                                                                     \
    DWORD dwFlags;                                                                                                    \
    HINSTANCE hInstance;                                                                                              \
    union {                                                                                                           \
        LPCDLGTEMPLATEW pResource;                                                                                    \
        const CHAR_T *pszTemplate;                                                                                    \
    };                                                                                                                \
    union {                                                                                                           \
        HICON hIcon;                                                                                                  \
        const CHAR_T *pszIcon;                                                                                        \
    };                                                                                                                \
    const CHAR_T *pszTitle;                                                                                           \
    DLGPROC pfnDlgProc;                                                                                               \
    LPARAM lParam;                                                                                                    \
    CALLBACK_T pfnCallback;                                                                                           \
    UINT *pcRefParent;

#define MIME_PSP_V2(CHAR_T)                                                                                           \
    const CHAR_T *pszHeaderTitle;                                                                                     \
    const CHAR_T *pszHeaderSubTitle;

#define MIME_PSP_V4(CHAR_T)                                                                                           \
    union {                                                                                                           \
        HBITMAP hbmHeader;                                                                                            \
        const CHAR_T *pszbmHeader;                                                                                    \
    };

typedef struct _PROPSHEETPAGEA_V1 {
    MIME_PSP_V1(char, LPFNPSPCALLBACKA)
} PROPSHEETPAGEA_V1, *LPPROPSHEETPAGEA_V1;
typedef const PROPSHEETPAGEA_V1 *LPCPROPSHEETPAGEA_V1;

typedef struct _PROPSHEETPAGEA_V2 {
    MIME_PSP_V1(char, LPFNPSPCALLBACKA)
    MIME_PSP_V2(char)
} PROPSHEETPAGEA_V2, *LPPROPSHEETPAGEA_V2;
typedef const PROPSHEETPAGEA_V2 *LPCPROPSHEETPAGEA_V2;

typedef struct _PROPSHEETPAGEA_V3 {
    MIME_PSP_V1(char, LPFNPSPCALLBACKA)
    MIME_PSP_V2(char)
    HANDLE hActCtx;
} PROPSHEETPAGEA_V3, *LPPROPSHEETPAGEA_V3;
typedef const PROPSHEETPAGEA_V3 *LPCPROPSHEETPAGEA_V3;

typedef struct _PROPSHEETPAGEA {
    MIME_PSP_V1(char, LPFNPSPCALLBACKA)
    MIME_PSP_V2(char)
    HANDLE hActCtx;
    MIME_PSP_V4(char)
} PROPSHEETPAGEA, *LPPROPSHEETPAGEA;
typedef const PROPSHEETPAGEA *LPCPROPSHEETPAGEA;

typedef struct _PROPSHEETPAGEW_V1 {
    MIME_PSP_V1(WCHAR, LPFNPSPCALLBACKW)
} PROPSHEETPAGEW_V1, *LPPROPSHEETPAGEW_V1;
typedef const PROPSHEETPAGEW_V1 *LPCPROPSHEETPAGEW_V1;

typedef struct _PROPSHEETPAGEW_V2 {
    MIME_PSP_V1(WCHAR, LPFNPSPCALLBACKW)
    MIME_PSP_V2(WCHAR)
} PROPSHEETPAGEW_V2, *LPPROPSHEETPAGEW_V2;
typedef const PROPSHEETPAGEW_V2 *LPCPROPSHEETPAGEW_V2;

typedef struct _PROPSHEETPAGEW_V3 {
    MIME_PSP_V1(WCHAR, LPFNPSPCALLBACKW)
    MIME_PSP_V2(WCHAR)
    HANDLE hActCtx;
} PROPSHEETPAGEW_V3, *LPPROPSHEETPAGEW_V3;
typedef const PROPSHEETPAGEW_V3 *LPCPROPSHEETPAGEW_V3;

typedef struct _PROPSHEETPAGEW {
    MIME_PSP_V1(WCHAR, LPFNPSPCALLBACKW)
    MIME_PSP_V2(WCHAR)
    HANDLE hActCtx;
    MIME_PSP_V4(WCHAR)
} PROPSHEETPAGEW, *LPPROPSHEETPAGEW;
typedef const PROPSHEETPAGEW *LPCPROPSHEETPAGEW;

/* ------------------------------------------------------------------ headers */

#define PSH_DEFAULT 0x00000000
#define PSH_PROPTITLE 0x00000001
#define PSH_USEHICON 0x00000002
#define PSH_USEICONID 0x00000004
#define PSH_PROPSHEETPAGE 0x00000008
#define PSH_WIZARDHASFINISH 0x00000010
#define PSH_WIZARD 0x00000020
#define PSH_USEPSTARTPAGE 0x00000040
#define PSH_NOAPPLYNOW 0x00000080
#define PSH_USECALLBACK 0x00000100
#define PSH_HASHELP 0x00000200
#define PSH_MODELESS 0x00000400
#define PSH_RTLREADING 0x00000800
#define PSH_WIZARDCONTEXTHELP 0x00001000
#define PSH_WIZARD97_OLD 0x00002000
#define PSH_AEROWIZARD 0x00004000
#define PSH_WATERMARK 0x00008000
#define PSH_USEHBMWATERMARK 0x00010000
#define PSH_USEHPLWATERMARK 0x00020000
#define PSH_STRETCHWATERMARK 0x00040000
#define PSH_HEADER 0x00080000
#define PSH_USEHBMHEADER 0x00100000
#define PSH_USEPAGELANG 0x00200000
#define PSH_WIZARD_LITE 0x00400000
#define PSH_NOCONTEXTHELP 0x02000000
#define PSH_RESIZABLE 0x04000000
#define PSH_HEADERBITMAP 0x08000000
#define PSH_NOMARGIN 0x10000000
#define PSH_WIZARD97 0x01000000

#define PSCB_INITIALIZED 1
#define PSCB_PRECREATE 2
#define PSCB_BUTTONPRESSED 3

#define PSBTN_BACK 0
#define PSBTN_NEXT 1
#define PSBTN_FINISH 2
#define PSBTN_OK 3
#define PSBTN_APPLYNOW 4
#define PSBTN_CANCEL 5
#define PSBTN_HELP 6
#define PSBTN_MAX 6

typedef int(CALLBACK *PFNPROPSHEETCALLBACK)(HWND hwnd, UINT msg, LPARAM param);

#define MIME_PSH_V1(CHAR_T, PAGE_T)                                                                                   \
    DWORD dwSize;                                                                                                     \
    DWORD dwFlags;                                                                                                    \
    HWND hwndParent;                                                                                                  \
    HINSTANCE hInstance;                                                                                              \
    union {                                                                                                           \
        HICON hIcon;                                                                                                  \
        const CHAR_T *pszIcon;                                                                                        \
    };                                                                                                                \
    const CHAR_T *pszCaption;                                                                                         \
    UINT nPages;                                                                                                      \
    union {                                                                                                           \
        UINT nStartPage;                                                                                              \
        const CHAR_T *pStartPage;                                                                                     \
    };                                                                                                                \
    union {                                                                                                           \
        const PAGE_T *ppsp;                                                                                           \
        HPROPSHEETPAGE *phpage;                                                                                       \
    };                                                                                                                \
    PFNPROPSHEETCALLBACK pfnCallback;

#define MIME_PSH_V2(CHAR_T)                                                                                           \
    union {                                                                                                           \
        HBITMAP hbmWatermark;                                                                                         \
        const CHAR_T *pszbmWatermark;                                                                                 \
    };                                                                                                                \
    HPALETTE hplWatermark;                                                                                            \
    union {                                                                                                           \
        HBITMAP hbmHeader;                                                                                            \
        const CHAR_T *pszbmHeader;                                                                                    \
    };

typedef struct _PROPSHEETHEADERA_V1 {
    MIME_PSH_V1(char, PROPSHEETPAGEA)
} PROPSHEETHEADERA_V1, *LPPROPSHEETHEADERA_V1;
typedef const PROPSHEETHEADERA_V1 *LPCPROPSHEETHEADERA_V1;

typedef struct _PROPSHEETHEADERA_V2 {
    MIME_PSH_V1(char, PROPSHEETPAGEA)
    MIME_PSH_V2(char)
} PROPSHEETHEADERA_V2, *LPPROPSHEETHEADERA_V2, PROPSHEETHEADERA, *LPPROPSHEETHEADERA;
typedef const PROPSHEETHEADERA *LPCPROPSHEETHEADERA;

typedef struct _PROPSHEETHEADERW_V1 {
    MIME_PSH_V1(WCHAR, PROPSHEETPAGEW)
} PROPSHEETHEADERW_V1, *LPPROPSHEETHEADERW_V1;
typedef const PROPSHEETHEADERW_V1 *LPCPROPSHEETHEADERW_V1;

typedef struct _PROPSHEETHEADERW_V2 {
    MIME_PSH_V1(WCHAR, PROPSHEETPAGEW)
    MIME_PSH_V2(WCHAR)
} PROPSHEETHEADERW_V2, *LPPROPSHEETHEADERW_V2, PROPSHEETHEADERW, *LPPROPSHEETHEADERW;
typedef const PROPSHEETHEADERW *LPCPROPSHEETHEADERW;

WINCOMMCTRLAPI HPROPSHEETPAGE WINAPI CreatePropertySheetPageA(LPCPROPSHEETPAGEA page);
WINCOMMCTRLAPI HPROPSHEETPAGE WINAPI CreatePropertySheetPageW(LPCPROPSHEETPAGEW page);
WINCOMMCTRLAPI HPROPSHEETPAGE WINAPI CreatePropertySheetPage(LPCPROPSHEETPAGEA page);
WINCOMMCTRLAPI BOOL WINAPI DestroyPropertySheetPage(HPROPSHEETPAGE page);
WINCOMMCTRLAPI INT_PTR WINAPI PropertySheetA(LPCPROPSHEETHEADERA header);
WINCOMMCTRLAPI INT_PTR WINAPI PropertySheetW(LPCPROPSHEETHEADERW header);
WINCOMMCTRLAPI INT_PTR WINAPI PropertySheet(LPCPROPSHEETHEADERA header);

/* ------------------------------------------------------------------ notifications */

#define PSN_FIRST (0U - 200U)
#define PSN_LAST (0U - 299U)
#define PSN_SETACTIVE (PSN_FIRST - 0)
#define PSN_KILLACTIVE (PSN_FIRST - 1)
#define PSN_APPLY (PSN_FIRST - 2)
#define PSN_RESET (PSN_FIRST - 3)
#define PSN_HELP (PSN_FIRST - 5)
#define PSN_WIZBACK (PSN_FIRST - 6)
#define PSN_WIZNEXT (PSN_FIRST - 7)
#define PSN_WIZFINISH (PSN_FIRST - 8)
#define PSN_QUERYCANCEL (PSN_FIRST - 9)
#define PSN_GETOBJECT (PSN_FIRST - 10)
#define PSN_LASTCHANCEAPPLY (PSN_FIRST - 11)
#define PSN_TRANSLATEACCELERATOR (PSN_FIRST - 12)
#define PSN_QUERYINITIALFOCUS (PSN_FIRST - 13)

#define PSNRET_NOERROR 0
#define PSNRET_INVALID 1
#define PSNRET_INVALID_NOCHANGEPAGE 2
#define PSNRET_MESSAGEHANDLED 3

typedef struct _PSHNOTIFY {
    NMHDR hdr;
    LPARAM lParam;
} PSHNOTIFY, *LPPSHNOTIFY;

/* ------------------------------------------------------------------ messages */

#define PSWIZB_BACK 0x00000001
#define PSWIZB_NEXT 0x00000002
#define PSWIZB_FINISH 0x00000004
#define PSWIZB_DISABLEDFINISH 0x00000008

#define PSM_SETCURSEL (WM_USER + 101)
#define PSM_REMOVEPAGE (WM_USER + 102)
#define PSM_ADDPAGE (WM_USER + 103)
#define PSM_CHANGED (WM_USER + 104)
#define PSM_RESTARTWINDOWS (WM_USER + 105)
#define PSM_REBOOTSYSTEM (WM_USER + 106)
#define PSM_CANCELTOCLOSE (WM_USER + 107)
#define PSM_QUERYSIBLINGS (WM_USER + 108)
#define PSM_UNCHANGED (WM_USER + 109)
#define PSM_APPLY (WM_USER + 110)
#define PSM_SETTITLEA (WM_USER + 111)
#define PSM_SETWIZBUTTONS (WM_USER + 112)
#define PSM_PRESSBUTTON (WM_USER + 113)
#define PSM_SETCURSELID (WM_USER + 114)
#define PSM_SETFINISHTEXTA (WM_USER + 115)
#define PSM_GETTABCONTROL (WM_USER + 116)
#define PSM_ISDIALOGMESSAGE (WM_USER + 117)
#define PSM_GETCURRENTPAGEHWND (WM_USER + 118)
#define PSM_INSERTPAGE (WM_USER + 119)
#define PSM_SETTITLEW (WM_USER + 120)
#define PSM_SETFINISHTEXTW (WM_USER + 121)
#define PSM_SETHEADERTITLEA (WM_USER + 125)
#define PSM_SETHEADERTITLEW (WM_USER + 126)
#define PSM_SETHEADERSUBTITLEA (WM_USER + 127)
#define PSM_SETHEADERSUBTITLEW (WM_USER + 128)
#define PSM_HWNDTOINDEX (WM_USER + 129)
#define PSM_INDEXTOHWND (WM_USER + 130)
#define PSM_PAGETOINDEX (WM_USER + 131)
#define PSM_INDEXTOPAGE (WM_USER + 132)
#define PSM_IDTOINDEX (WM_USER + 133)
#define PSM_INDEXTOID (WM_USER + 134)
#define PSM_GETRESULT (WM_USER + 135)
#define PSM_RECALCPAGESIZES (WM_USER + 136)
#define PSM_SETTITLE PSM_SETTITLEW
#define PSM_SETFINISHTEXT PSM_SETFINISHTEXTW
#define PSM_SETHEADERTITLE PSM_SETHEADERTITLEW
#define PSM_SETHEADERSUBTITLE PSM_SETHEADERSUBTITLEW

#define ID_PSRESTARTWINDOWS 0x2
#define ID_PSREBOOTSYSTEM (ID_PSRESTARTWINDOWS | 0x1)

#define PropSheet_SetCurSel(hdlg, hpage, index) SendMessageW(hdlg, PSM_SETCURSEL, (WPARAM)(index), (LPARAM)(hpage))
#define PropSheet_RemovePage(hdlg, index, hpage) SendMessageW(hdlg, PSM_REMOVEPAGE, (WPARAM)(index), (LPARAM)(hpage))
#define PropSheet_AddPage(hdlg, hpage) SendMessageW(hdlg, PSM_ADDPAGE, 0, (LPARAM)(hpage))
#define PropSheet_Changed(hdlg, hwnd_page) SendMessageW(hdlg, PSM_CHANGED, (WPARAM)(hwnd_page), 0)
#define PropSheet_RestartWindows(hdlg) SendMessageW(hdlg, PSM_RESTARTWINDOWS, 0, 0)
#define PropSheet_RebootSystem(hdlg) SendMessageW(hdlg, PSM_REBOOTSYSTEM, 0, 0)
#define PropSheet_CancelToClose(hdlg) SendMessageW(hdlg, PSM_CANCELTOCLOSE, 0, 0)
#define PropSheet_QuerySiblings(hdlg, wparam, lparam)                                                                 \
    SendMessageW(hdlg, PSM_QUERYSIBLINGS, (WPARAM)(wparam), (LPARAM)(lparam))
#define PropSheet_UnChanged(hdlg, hwnd_page) SendMessageW(hdlg, PSM_UNCHANGED, (WPARAM)(hwnd_page), 0)
#define PropSheet_Apply(hdlg) ((BOOL)SendMessageW(hdlg, PSM_APPLY, 0, 0))
#define PropSheet_SetTitle(hdlg, style, text) SendMessageW(hdlg, PSM_SETTITLEW, (WPARAM)(style), (LPARAM)(text))
#define PropSheet_SetWizButtons(hdlg, flags) PostMessageW(hdlg, PSM_SETWIZBUTTONS, 0, (LPARAM)(flags))
#define PropSheet_PressButton(hdlg, button) SendMessageW(hdlg, PSM_PRESSBUTTON, (WPARAM)(button), 0)
#define PropSheet_SetCurSelByID(hdlg, id) SendMessageW(hdlg, PSM_SETCURSELID, 0, (LPARAM)(id))
#define PropSheet_SetFinishText(hdlg, text) SendMessageW(hdlg, PSM_SETFINISHTEXTW, 0, (LPARAM)(text))
#define PropSheet_GetTabControl(hdlg) ((HWND)SendMessageW(hdlg, PSM_GETTABCONTROL, 0, 0))
#define PropSheet_IsDialogMessage(hdlg, msg) ((BOOL)SendMessageW(hdlg, PSM_ISDIALOGMESSAGE, 0, (LPARAM)(msg)))
#define PropSheet_GetCurrentPageHwnd(hdlg) ((HWND)SendMessageW(hdlg, PSM_GETCURRENTPAGEHWND, 0, 0))
#define PropSheet_InsertPage(hdlg, index_or_hpage, hpage)                                                             \
    SendMessageW(hdlg, PSM_INSERTPAGE, (WPARAM)(index_or_hpage), (LPARAM)(hpage))
#define PropSheet_SetHeaderTitle(hdlg, index, text)                                                                   \
    SendMessageW(hdlg, PSM_SETHEADERTITLEW, (WPARAM)(index), (LPARAM)(text))
#define PropSheet_SetHeaderSubTitle(hdlg, index, text)                                                                \
    SendMessageW(hdlg, PSM_SETHEADERSUBTITLEW, (WPARAM)(index), (LPARAM)(text))
#define PropSheet_HwndToIndex(hdlg, hwnd_page) ((int)SendMessageW(hdlg, PSM_HWNDTOINDEX, (WPARAM)(hwnd_page), 0))
#define PropSheet_IndexToHwnd(hdlg, index) ((HWND)SendMessageW(hdlg, PSM_INDEXTOHWND, (WPARAM)(index), 0))
#define PropSheet_PageToIndex(hdlg, hpage) ((int)SendMessageW(hdlg, PSM_PAGETOINDEX, 0, (LPARAM)(hpage)))
#define PropSheet_IndexToPage(hdlg, index) ((HPROPSHEETPAGE)SendMessageW(hdlg, PSM_INDEXTOPAGE, (WPARAM)(index), 0))
#define PropSheet_IdToIndex(hdlg, id) ((int)SendMessageW(hdlg, PSM_IDTOINDEX, 0, (LPARAM)(id)))
#define PropSheet_IndexToId(hdlg, index) SendMessageW(hdlg, PSM_INDEXTOID, (WPARAM)(index), 0)
#define PropSheet_GetResult(hdlg) SendMessageW(hdlg, PSM_GETRESULT, 0, 0)
#define PropSheet_RecalcPageSizes(hdlg) ((BOOL)SendMessageW(hdlg, PSM_RECALCPAGESIZES, 0, 0))

#endif
