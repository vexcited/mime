/* common dialogs of comdlg32.dll: file, color, font and print dialogs. */
#ifndef MIME_WIN32_COMMDLG_H
#define MIME_WIN32_COMMDLG_H

#include <mime/win32/objbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/winspool.h>

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
#ifndef MIME_HPROPSHEETPAGE_DEFINED
#define MIME_HPROPSHEETPAGE_DEFINED
typedef struct _PSP *HPROPSHEETPAGE;
#endif

/* ------------------------------------------------------------------ find and replace */

typedef UINT_PTR(CALLBACK *LPFRHOOKPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagFINDREPLACEW {
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    DWORD Flags;
    LPWSTR lpstrFindWhat;
    LPWSTR lpstrReplaceWith;
    WORD wFindWhatLen;
    WORD wReplaceWithLen;
    LPARAM lCustData;
    LPFRHOOKPROC lpfnHook;
    LPCWSTR lpTemplateName;
} FINDREPLACEW, *LPFINDREPLACEW;

typedef struct tagFINDREPLACEA {
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    DWORD Flags;
    LPSTR lpstrFindWhat;
    LPSTR lpstrReplaceWith;
    WORD wFindWhatLen;
    WORD wReplaceWithLen;
    LPARAM lCustData;
    LPFRHOOKPROC lpfnHook;
    LPCSTR lpTemplateName;
} FINDREPLACEA, *LPFINDREPLACEA;

#define FR_DOWN 0x00000001
#define FR_WHOLEWORD 0x00000002
#define FR_MATCHCASE 0x00000004
#define FR_FINDNEXT 0x00000008
#define FR_REPLACE 0x00000010
#define FR_REPLACEALL 0x00000020
#define FR_DIALOGTERM 0x00000040
#define FR_SHOWHELP 0x00000080
#define FR_ENABLEHOOK 0x00000100
#define FR_ENABLETEMPLATE 0x00000200
#define FR_NOUPDOWN 0x00000400
#define FR_NOMATCHCASE 0x00000800
#define FR_NOWHOLEWORD 0x00001000
#define FR_ENABLETEMPLATEHANDLE 0x00002000
#define FR_HIDEUPDOWN 0x00004000
#define FR_HIDEMATCHCASE 0x00008000
#define FR_HIDEWHOLEWORD 0x00010000
#define FR_RAW 0x00020000
#define FR_SHOWWRAPAROUND 0x00040000
#define FR_NOWRAPAROUND 0x00080000
#define FR_WRAPAROUND 0x00100000
#define FR_MATCHDIAC 0x20000000
#define FR_MATCHKASHIDA 0x40000000
#define FR_MATCHALEFHAMZA 0x80000000

#define FINDMSGSTRINGW L"commdlg_FindReplace"
#define FINDMSGSTRINGA "commdlg_FindReplace"

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
#define CFERR_NOFONTS 0x2001
#define CFERR_MAXLESSTHANMIN 0x2002
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

#define CC_RGBINIT 0x00000001
#define CC_FULLOPEN 0x00000002
#define CC_PREVENTFULLOPEN 0x00000004
#define CC_SHOWHELP 0x00000008
#define CC_ENABLEHOOK 0x00000010
#define CC_ENABLETEMPLATE 0x00000020
#define CC_ENABLETEMPLATEHANDLE 0x00000040
#define CC_SOLIDCOLOR 0x00000080
#define CC_ANYCOLOR 0x00000100

#define COLOROKSTRINGW L"commdlg_ColorOK"
#define SETRGBSTRINGW L"commdlg_SetRGBColor"
#define HELPMSGSTRINGW L"commdlg_help"
#define COLOROKSTRINGA "commdlg_ColorOK"
#define SETRGBSTRINGA "commdlg_SetRGBColor"
#define HELPMSGSTRINGA "commdlg_help"

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

typedef struct tagCHOOSECOLORA {
    DWORD lStructSize;
    HWND hwndOwner;
    HWND hInstance;
    COLORREF rgbResult;
    COLORREF *lpCustColors;
    DWORD Flags;
    LPARAM lCustData;
    LPCCHOOKPROC lpfnHook;
    LPCSTR lpTemplateName;
} CHOOSECOLORA, *LPCHOOSECOLORA;

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

typedef struct tagCHOOSEFONTA {
    DWORD lStructSize;
    HWND hwndOwner;
    HDC hDC;
    LOGFONTA *lpLogFont;
    INT iPointSize;
    DWORD Flags;
    COLORREF rgbColors;
    LPARAM lCustData;
    LPCFHOOKPROC lpfnHook;
    LPCSTR lpTemplateName;
    HINSTANCE hInstance;
    LPSTR lpszStyle;
    WORD nFontType;
    WORD Alignment;
    INT nSizeMin;
    INT nSizeMax;
} CHOOSEFONTA, *LPCHOOSEFONTA;

#define CF_SCREENFONTS 0x00000001
#define CF_PRINTERFONTS 0x00000002
#define CF_BOTH (CF_SCREENFONTS | CF_PRINTERFONTS)
#define CF_SHOWHELP 0x00000004
#define CF_ENABLEHOOK 0x00000008
#define CF_ENABLETEMPLATE 0x00000010
#define CF_ENABLETEMPLATEHANDLE 0x00000020
#define CF_INITTOLOGFONTSTRUCT 0x00000040
#define CF_USESTYLE 0x00000080
#define CF_EFFECTS 0x00000100
#define CF_APPLY 0x00000200
#define CF_ANSIONLY 0x00000400
#define CF_SCRIPTSONLY CF_ANSIONLY
#define CF_NOVECTORFONTS 0x00000800
#define CF_NOOEMFONTS CF_NOVECTORFONTS
#define CF_NOSIMULATIONS 0x00001000
#define CF_LIMITSIZE 0x00002000
#define CF_FIXEDPITCHONLY 0x00004000
#define CF_WYSIWYG 0x00008000
#define CF_FORCEFONTEXIST 0x00010000
#define CF_SCALABLEONLY 0x00020000
#define CF_TTONLY 0x00040000
#define CF_NOFACESEL 0x00080000
#define CF_NOSTYLESEL 0x00100000
#define CF_NOSIZESEL 0x00200000
#define CF_SELECTSCRIPT 0x00400000
#define CF_NOSCRIPTSEL 0x00800000
#define CF_NOVERTFONTS 0x01000000
#define CF_INACTIVEFONTS 0x02000000

#define SIMULATED_FONTTYPE 0x8000
#define PRINTER_FONTTYPE 0x4000
#define SCREEN_FONTTYPE 0x2000
#define BOLD_FONTTYPE 0x0100
#define ITALIC_FONTTYPE 0x0200
#define REGULAR_FONTTYPE 0x0400

#define WM_CHOOSEFONT_GETLOGFONT (WM_USER + 1)
#define WM_CHOOSEFONT_SETLOGFONT (WM_USER + 101)
#define WM_CHOOSEFONT_SETFLAGS (WM_USER + 102)


/* ------------------------------------------------------------------ print dialogs */

#define PD_ALLPAGES 0x00000000
#define PD_SELECTION 0x00000001
#define PD_PAGENUMS 0x00000002
#define PD_NOSELECTION 0x00000004
#define PD_NOPAGENUMS 0x00000008
#define PD_COLLATE 0x00000010
#define PD_PRINTTOFILE 0x00000020
#define PD_PRINTSETUP 0x00000040
#define PD_NOWARNING 0x00000080
#define PD_RETURNDC 0x00000100
#define PD_RETURNIC 0x00000200
#define PD_RETURNDEFAULT 0x00000400
#define PD_SHOWHELP 0x00000800
#define PD_ENABLEPRINTHOOK 0x00001000
#define PD_ENABLESETUPHOOK 0x00002000
#define PD_ENABLEPRINTTEMPLATE 0x00004000
#define PD_ENABLESETUPTEMPLATE 0x00008000
#define PD_ENABLEPRINTTEMPLATEHANDLE 0x00010000
#define PD_ENABLESETUPTEMPLATEHANDLE 0x00020000
#define PD_USEDEVMODECOPIES 0x00040000
#define PD_USEDEVMODECOPIESANDCOLLATE 0x00040000
#define PD_DISABLEPRINTTOFILE 0x00080000
#define PD_HIDEPRINTTOFILE 0x00100000
#define PD_NONETWORKBUTTON 0x00200000
#define PD_CURRENTPAGE 0x00400000
#define PD_NOCURRENTPAGE 0x00800000
#define PD_EXCLUSIONFLAGS 0x01000000
#define PD_USELARGETEMPLATE 0x10000000

#define PD_RESULT_CANCEL 0
#define PD_RESULT_PRINT 1
#define PD_RESULT_APPLY 2
#define START_PAGE_GENERAL 0xFFFFFFFF
#define PD_EXCL_COPIESANDCOLLATE (DM_COPIES | DM_COLLATE)

#define PSD_DEFAULTMINMARGINS 0x00000000
#define PSD_INWININIINTLMEASURE 0x00000000
#define PSD_MINMARGINS 0x00000001
#define PSD_MARGINS 0x00000002
#define PSD_INTHOUSANDTHSOFINCHES 0x00000004
#define PSD_INHUNDREDTHSOFMILLIMETERS 0x00000008
#define PSD_DISABLEMARGINS 0x00000010
#define PSD_DISABLEPRINTER 0x00000020
#define PSD_NOWARNING 0x00000080
#define PSD_DISABLEORIENTATION 0x00000100
#define PSD_RETURNDEFAULT 0x00000400
#define PSD_DISABLEPAPER 0x00000200
#define PSD_SHOWHELP 0x00000800
#define PSD_ENABLEPAGESETUPHOOK 0x00002000
#define PSD_ENABLEPAGESETUPTEMPLATE 0x00008000
#define PSD_ENABLEPAGESETUPTEMPLATEHANDLE 0x00020000
#define PSD_ENABLEPAGEPAINTHOOK 0x00040000
#define PSD_DISABLEPAGEPAINTING 0x00080000
#define PSD_NONETWORKBUTTON 0x00200000

#define WM_PSD_PAGESETUPDLG (WM_USER)
#define WM_PSD_FULLPAGERECT (WM_USER + 1)
#define WM_PSD_MINMARGINRECT (WM_USER + 2)
#define WM_PSD_MARGINRECT (WM_USER + 3)
#define WM_PSD_GREEKTEXTRECT (WM_USER + 4)
#define WM_PSD_ENVSTAMPRECT (WM_USER + 5)
#define WM_PSD_YAFULLPAGERECT (WM_USER + 6)

typedef struct tagDEVNAMES {
    WORD wDriverOffset;
    WORD wDeviceOffset;
    WORD wOutputOffset;
    WORD wDefault;
} DEVNAMES, *LPDEVNAMES;
#define DN_DEFAULTPRN 0x0001

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

typedef struct tagPRINTDLGA {
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
    LPCSTR lpPrintTemplateName;
    LPCSTR lpSetupTemplateName;
    HGLOBAL hPrintTemplate;
    HGLOBAL hSetupTemplate;
} PRINTDLGA, *LPPRINTDLGA;

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

typedef struct tagPDEXA {
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
    LPCSTR lpPrintTemplateName;
    LPUNKNOWN lpCallback;
    DWORD nPropertyPages;
    HPROPSHEETPAGE *lphPropertyPages;
    DWORD nStartPage;
    DWORD dwResultAction;
} PRINTDLGEXA, *LPPRINTDLGEXA;

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

typedef struct tagPSDA {
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
    LPCSTR lpPageSetupTemplateName;
    HGLOBAL hPageSetupTemplate;
} PAGESETUPDLGA, *LPPAGESETUPDLGA;

/* the PrintDlgEx callback and the services it gets. */
typedef struct IPrintDialogCallback IPrintDialogCallback;
typedef struct IPrintDialogServices IPrintDialogServices;

typedef struct IPrintDialogCallbackVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPrintDialogCallback *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPrintDialogCallback *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPrintDialogCallback *This);
    HRESULT(STDMETHODCALLTYPE *InitDone)(IPrintDialogCallback *This);
    HRESULT(STDMETHODCALLTYPE *SelectionChange)(IPrintDialogCallback *This);
    HRESULT(STDMETHODCALLTYPE *HandleMessage)(IPrintDialogCallback *This, HWND dialog, UINT message, WPARAM wparam,
                                              LPARAM lparam, LRESULT *result);
} IPrintDialogCallbackVtbl;
struct IPrintDialogCallback {
    const IPrintDialogCallbackVtbl *lpVtbl;
};

typedef struct IPrintDialogServicesVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPrintDialogServices *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPrintDialogServices *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPrintDialogServices *This);
    HRESULT(STDMETHODCALLTYPE *GetCurrentDevMode)(IPrintDialogServices *This, DEVMODEW *devmode, UINT *size);
    HRESULT(STDMETHODCALLTYPE *GetCurrentPrinterName)(IPrintDialogServices *This, LPWSTR name, UINT *size);
    HRESULT(STDMETHODCALLTYPE *GetCurrentPortName)(IPrintDialogServices *This, LPWSTR name, UINT *size);
} IPrintDialogServicesVtbl;
struct IPrintDialogServices {
    const IPrintDialogServicesVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ functions */

CDAPI DWORD WINAPI CommDlgExtendedError(void);
CDAPI BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW ofn);
CDAPI BOOL WINAPI GetOpenFileNameA(LPOPENFILENAMEA ofn);
CDAPI BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW ofn);
CDAPI BOOL WINAPI GetSaveFileNameA(LPOPENFILENAMEA ofn);
CDAPI short WINAPI GetFileTitleW(LPCWSTR file, LPWSTR title, WORD chars);
CDAPI short WINAPI GetFileTitleA(LPCSTR file, LPSTR title, WORD chars);
CDAPI BOOL WINAPI ChooseColorW(LPCHOOSECOLORW cc);
CDAPI BOOL WINAPI ChooseColorA(LPCHOOSECOLORA cc);
CDAPI BOOL WINAPI ChooseFontW(LPCHOOSEFONTW cf);
CDAPI BOOL WINAPI ChooseFontA(LPCHOOSEFONTA cf);
CDAPI BOOL WINAPI PrintDlgW(LPPRINTDLGW pd);
CDAPI BOOL WINAPI PrintDlgA(LPPRINTDLGA pd);
CDAPI HRESULT WINAPI PrintDlgExW(LPPRINTDLGEXW pd);
CDAPI HRESULT WINAPI PrintDlgExA(LPPRINTDLGEXA pd);
CDAPI BOOL WINAPI PageSetupDlgW(LPPAGESETUPDLGW psd);
CDAPI BOOL WINAPI PageSetupDlgA(LPPAGESETUPDLGA psd);
CDAPI HWND WINAPI FindTextW(LPFINDREPLACEW fr);
CDAPI HWND WINAPI FindTextA(LPFINDREPLACEA fr);
CDAPI HWND WINAPI ReplaceTextW(LPFINDREPLACEW fr);
CDAPI HWND WINAPI ReplaceTextA(LPFINDREPLACEA fr);

#ifdef __i386__
#pragma pack(pop)
#endif

#endif
