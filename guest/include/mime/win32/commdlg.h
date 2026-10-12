/*
 * common dialogs of comdlg32.dll: file, color, font and print dialogs.
 */
#ifndef MIME_WIN32_COMMDLG_H
#define MIME_WIN32_COMMDLG_H

#include <mime/win32/objbase.h>
#include <mime/win32/winuser.h>

#ifndef CDAPI
#define CDAPI DECLSPEC_IMPORT
#endif

typedef UINT_PTR(CALLBACK *LPCCHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR(CALLBACK *LPOFNHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR(CALLBACK *LPCFHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR(CALLBACK *LPPRINTHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR(CALLBACK *LPSETUPHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR(CALLBACK *LPPAGESETUPHOOK)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR(CALLBACK *LPPAGEPAINTHOOK)(HWND, UINT, WPARAM, LPARAM);
typedef struct _PSP *HPROPSHEETPAGE;

/* ------------------------------------------------------------------ extended errors */

#define CDERR_DIALOGFAILURE 0xFFFF
#define CDERR_GENERALCODES 0x0000
#define CDERR_STRUCTSIZE 0x0001
#define CDERR_INITIALIZATION 0x0002
#define CDERR_NOTEMPLATE 0x0003
#define CDERR_NOHINSTANCE 0x0004
#define CDERR_LOADSTRFAILURE 0x0005
#define CDERR_FINDRESFAILURE 0x0006
#define CDERR_LOADRESFAILURE 0x0007
#define CDERR_LOCKRESFAILURE 0x0008
#define CDERR_MEMALLOCFAILURE 0x0009
#define CDERR_MEMLOCKFAILURE 0x000A
#define CDERR_NOHOOK 0x000B
#define CDERR_REGISTERMSGFAIL 0x000C
#define PDERR_SETUPFAILURE 0x1001
#define PDERR_PARSEFAILURE 0x1002
#define PDERR_RETDEFFAILURE 0x1003
#define PDERR_LOADDRVFAILURE 0x1004
#define PDERR_GETDEVMODEFAIL 0x1005
#define PDERR_INITFAILURE 0x1006
#define PDERR_NODEVICES 0x1007
#define PDERR_NODEFAULTPRN 0x1008
#define FNERR_SUBCLASSFAILURE 0x3001
#define FNERR_INVALIDFILENAME 0x3002
#define FNERR_BUFFERTOOSMALL 0x3003

/* ------------------------------------------------------------------ file dialogs */

typedef struct tagOFNW {
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    LPCWSTR lpstrFilter;
    LPWSTR lpstrCustomFilter;
    DWORD nMaxCustFilter;
    DWORD nFilterIndex;
    LPWSTR lpstrFile;
    DWORD nMaxFile;
    LPWSTR lpstrFileTitle;
    DWORD nMaxFileTitle;
    LPCWSTR lpstrInitialDir;
    LPCWSTR lpstrTitle;
    DWORD Flags;
    WORD nFileOffset;
    WORD nFileExtension;
    LPCWSTR lpstrDefExt;
    LPARAM lCustData;
    LPOFNHOOKPROC lpfnHook;
    LPCWSTR lpTemplateName;
    void *pvReserved;
    DWORD dwReserved;
    DWORD FlagsEx;
} OPENFILENAMEW, *LPOPENFILENAMEW;
#define OPENFILENAME_SIZE_VERSION_400W offsetof(OPENFILENAMEW, pvReserved)
MIME_STATIC_ASSERT(sizeof(OPENFILENAMEW) == 152, "OPENFILENAMEW size");

typedef struct tagOFNA {
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    LPCSTR lpstrFilter;
    LPSTR lpstrCustomFilter;
    DWORD nMaxCustFilter;
    DWORD nFilterIndex;
    LPSTR lpstrFile;
    DWORD nMaxFile;
    LPSTR lpstrFileTitle;
    DWORD nMaxFileTitle;
    LPCSTR lpstrInitialDir;
    LPCSTR lpstrTitle;
    DWORD Flags;
    WORD nFileOffset;
    WORD nFileExtension;
    LPCSTR lpstrDefExt;
    LPARAM lCustData;
    LPOFNHOOKPROC lpfnHook;
    LPCSTR lpTemplateName;
    void *pvReserved;
    DWORD dwReserved;
    DWORD FlagsEx;
} OPENFILENAMEA, *LPOPENFILENAMEA;
#define OPENFILENAME_SIZE_VERSION_400A offsetof(OPENFILENAMEA, pvReserved)

/* ------------------------------------------------------------------ color and font dialogs */

typedef struct tagCHOOSECOLORW {
    DWORD lStructSize;
    HWND hwndOwner;
    HWND hInstance;
    COLORREF rgbResult;
    COLORREF *lpCustColors;
    DWORD Flags;
    LPARAM lCustData;
    LPCCHOOKPROC lpfnHook;
    LPCWSTR lpTemplateName;
} CHOOSECOLORW, *LPCHOOSECOLORW;
MIME_STATIC_ASSERT(sizeof(CHOOSECOLORW) == 72, "CHOOSECOLORW size");

typedef struct tagCHOOSEFONTW {
    DWORD lStructSize;
    HWND hwndOwner;
    HDC hDC;
    LOGFONTW *lpLogFont;
    INT iPointSize;
    DWORD Flags;
    COLORREF rgbColors;
    LPARAM lCustData;
    LPCFHOOKPROC lpfnHook;
    LPCWSTR lpTemplateName;
    HINSTANCE hInstance;
    LPWSTR lpszStyle;
    WORD nFontType;
    INT nSizeMin;
    INT nSizeMax;
} CHOOSEFONTW, *LPCHOOSEFONTW;
MIME_STATIC_ASSERT(sizeof(CHOOSEFONTW) == 104, "CHOOSEFONTW size");

/* ------------------------------------------------------------------ print dialogs */

#define PD_RETURNDEFAULT 0x00000400
#define PSD_RETURNDEFAULT 0x00000400

#define PD_RESULT_CANCEL 0
#define PD_RESULT_PRINT 1
#define PD_RESULT_APPLY 2

typedef struct tagPRINTDLGW {
    DWORD lStructSize;
    HWND hwndOwner;
    HGLOBAL hDevMode;
    HGLOBAL hDevNames;
    HDC hDC;
    DWORD Flags;
    WORD nFromPage;
    WORD nToPage;
    WORD nMinPage;
    WORD nMaxPage;
    WORD nCopies;
    HINSTANCE hInstance;
    LPARAM lCustData;
    LPPRINTHOOKPROC lpfnPrintHook;
    LPSETUPHOOKPROC lpfnSetupHook;
    LPCWSTR lpPrintTemplateName;
    LPCWSTR lpSetupTemplateName;
    HGLOBAL hPrintTemplate;
    HGLOBAL hSetupTemplate;
} PRINTDLGW, *LPPRINTDLGW;
MIME_STATIC_ASSERT(sizeof(PRINTDLGW) == 120, "PRINTDLGW size");

typedef struct tagPRINTPAGERANGE {
    DWORD nFromPage;
    DWORD nToPage;
} PRINTPAGERANGE, *LPPRINTPAGERANGE;

typedef struct tagPDEXW {
    DWORD lStructSize;
    HWND hwndOwner;
    HGLOBAL hDevMode;
    HGLOBAL hDevNames;
    HDC hDC;
    DWORD Flags;
    DWORD Flags2;
    DWORD ExclusionFlags;
    DWORD nPageRanges;
    DWORD nMaxPageRanges;
    LPPRINTPAGERANGE lpPageRanges;
    DWORD nMinPage;
    DWORD nMaxPage;
    DWORD nCopies;
    HINSTANCE hInstance;
    LPCWSTR lpPrintTemplateName;
    LPUNKNOWN lpCallback;
    DWORD nPropertyPages;
    HPROPSHEETPAGE *lphPropertyPages;
    DWORD nStartPage;
    DWORD dwResultAction;
} PRINTDLGEXW, *LPPRINTDLGEXW;
MIME_STATIC_ASSERT(sizeof(PRINTDLGEXW) == 136, "PRINTDLGEXW size");

typedef struct tagPSDW {
    DWORD lStructSize;
    HWND hwndOwner;
    HGLOBAL hDevMode;
    HGLOBAL hDevNames;
    DWORD Flags;
    POINT ptPaperSize;
    RECT rtMinMargin;
    RECT rtMargin;
    HINSTANCE hInstance;
    LPARAM lCustData;
    LPPAGESETUPHOOK lpfnPageSetupHook;
    LPPAGEPAINTHOOK lpfnPagePaintHook;
    LPCWSTR lpPageSetupTemplateName;
    HGLOBAL hPageSetupTemplate;
} PAGESETUPDLGW, *LPPAGESETUPDLGW;
MIME_STATIC_ASSERT(sizeof(PAGESETUPDLGW) == 128, "PAGESETUPDLGW size");

/* ------------------------------------------------------------------ comdlg32 functions */

CDAPI DWORD WINAPI CommDlgExtendedError(void);
CDAPI BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW ofn);
CDAPI BOOL WINAPI GetOpenFileNameA(LPOPENFILENAMEA ofn);
CDAPI BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW ofn);
CDAPI BOOL WINAPI GetSaveFileNameA(LPOPENFILENAMEA ofn);
CDAPI short WINAPI GetFileTitleW(LPCWSTR file, LPWSTR title, WORD chars);
CDAPI BOOL WINAPI ChooseColorW(LPCHOOSECOLORW cc);
CDAPI BOOL WINAPI ChooseFontW(LPCHOOSEFONTW cf);
CDAPI BOOL WINAPI PrintDlgW(LPPRINTDLGW pd);
CDAPI HRESULT WINAPI PrintDlgExW(LPPRINTDLGEXW pd);
CDAPI BOOL WINAPI PageSetupDlgW(LPPAGESETUPDLGW psd);

#endif
