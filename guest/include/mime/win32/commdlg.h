/*
 * common dialogs of comdlg32.dll: file, color, font and print dialogs.
 */
#ifndef MIME_WIN32_COMMDLG_H
#define MIME_WIN32_COMMDLG_H

#include <mime/win32/objbase.h>
#include <mime/win32/winuser.h>

#ifdef __i386__
#pragma pack(push, 1)
#endif

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
MIME_STATIC_ASSERT(sizeof(OPENFILENAMEW) == (sizeof(void *) == 4 ? 88 : 152), "OPENFILENAMEW size");

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

#define OFN_READONLY 0x00000001
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_HIDEREADONLY 0x00000004
#define OFN_NOCHANGEDIR 0x00000008
#define OFN_SHOWHELP 0x00000010
#define OFN_ENABLEHOOK 0x00000020
#define OFN_ENABLETEMPLATE 0x00000040
#define OFN_ENABLETEMPLATEHANDLE 0x00000080
#define OFN_NOVALIDATE 0x00000100
#define OFN_ALLOWMULTISELECT 0x00000200
#define OFN_EXTENSIONDIFFERENT 0x00000400
#define OFN_PATHMUSTEXIST 0x00000800
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_CREATEPROMPT 0x00002000
#define OFN_SHAREAWARE 0x00004000
#define OFN_NOREADONLYRETURN 0x00008000
#define OFN_NOTESTFILECREATE 0x00010000
#define OFN_NONETWORKBUTTON 0x00020000
#define OFN_NOLONGNAMES 0x00040000
#define OFN_EXPLORER 0x00080000
#define OFN_NODEREFERENCELINKS 0x00100000
#define OFN_LONGNAMES 0x00200000
#define OFN_ENABLEINCLUDENOTIFY 0x00400000
#define OFN_ENABLESIZING 0x00800000
#define OFN_DONTADDTORECENT 0x02000000
#define OFN_FORCESHOWHIDDEN 0x10000000
#define OFN_EX_NOPLACESBAR 0x00000001

#define CDN_FIRST (0U - 601U)
#define CDN_INITDONE (CDN_FIRST - 0x0000)
#define CDN_SELCHANGE (CDN_FIRST - 0x0001)
#define CDN_FOLDERCHANGE (CDN_FIRST - 0x0002)
#define CDN_SHAREVIOLATION (CDN_FIRST - 0x0003)
#define CDN_HELP (CDN_FIRST - 0x0004)
#define CDN_FILEOK (CDN_FIRST - 0x0005)
#define CDN_TYPECHANGE (CDN_FIRST - 0x0006)
#define CDN_INCLUDEITEM (CDN_FIRST - 0x0007)

#define CDM_FIRST (WM_USER + 100)
#define CDM_GETSPEC (CDM_FIRST + 0x0000)
#define CDM_GETFILEPATH (CDM_FIRST + 0x0001)
#define CDM_GETFOLDERPATH (CDM_FIRST + 0x0002)
#define CDM_GETFOLDERIDLIST (CDM_FIRST + 0x0003)
#define CDM_SETCONTROLTEXT (CDM_FIRST + 0x0004)
#define CDM_HIDECONTROL (CDM_FIRST + 0x0005)
#define CDM_SETDEFEXT (CDM_FIRST + 0x0006)

#define SHAREVISTRING L"commdlg_ShareViolation"
#define FILEOKSTRING L"commdlg_FileNameOK"
#define LBSELCHSTRING L"commdlg_LBSelChangedNotify"

typedef struct _OFNOTIFYW {
    NMHDR hdr;
    LPOPENFILENAMEW lpOFN;
    LPWSTR pszFile;
} OFNOTIFYW, *LPOFNOTIFYW;

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
MIME_STATIC_ASSERT(sizeof(CHOOSECOLORW) == (sizeof(void *) == 4 ? 36 : 72), "CHOOSECOLORW size");

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
    WORD Alignment;
    INT nSizeMin;
    INT nSizeMax;
} CHOOSEFONTW, *LPCHOOSEFONTW;
MIME_STATIC_ASSERT(sizeof(CHOOSEFONTW) == (sizeof(void *) == 4 ? 60 : 104), "CHOOSEFONTW size");

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
MIME_STATIC_ASSERT(sizeof(PRINTDLGW) == (sizeof(void *) == 4 ? 66 : 120), "PRINTDLGW size");

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
MIME_STATIC_ASSERT(sizeof(PRINTDLGEXW) == (sizeof(void *) == 4 ? 84 : 136), "PRINTDLGEXW size");

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
MIME_STATIC_ASSERT(sizeof(PAGESETUPDLGW) == (sizeof(void *) == 4 ? 84 : 128), "PAGESETUPDLGW size");

/* ------------------------------------------------------------------ comdlg32 functions */

CDAPI DWORD WINAPI CommDlgExtendedError(void);
CDAPI BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW ofn);
CDAPI BOOL WINAPI GetOpenFileNameA(LPOPENFILENAMEA ofn);
CDAPI BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW ofn);
CDAPI BOOL WINAPI GetSaveFileNameA(LPOPENFILENAMEA ofn);
CDAPI short WINAPI GetFileTitleW(LPCWSTR file, LPWSTR title, WORD chars);
CDAPI short WINAPI GetFileTitleA(LPCSTR file, LPSTR title, WORD chars);
CDAPI BOOL WINAPI ChooseColorW(LPCHOOSECOLORW cc);
CDAPI BOOL WINAPI ChooseFontW(LPCHOOSEFONTW cf);
CDAPI BOOL WINAPI PrintDlgW(LPPRINTDLGW pd);
CDAPI HRESULT WINAPI PrintDlgExW(LPPRINTDLGEXW pd);
CDAPI BOOL WINAPI PageSetupDlgW(LPPAGESETUPDLGW psd);

#ifdef __i386__
#pragma pack(pop)
#endif

#endif
