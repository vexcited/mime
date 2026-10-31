/* ole user interface dialogs of oledlg.dll. */
#ifndef MIME_WIN32_OLEDLG_H
#define MIME_WIN32_OLEDLG_H

#include <mime/win32/objbase.h>
#include <mime/win32/oleidl.h>

#ifndef OLEDLGAPI
#define OLEDLGAPI DECLSPEC_IMPORT
#endif

#define OLEUI_FALSE 0
#define OLEUI_SUCCESS 1
#define OLEUI_OK 1
#define OLEUI_CANCEL 2
#define OLEUI_ERR_STANDARDMIN 100
#define OLEUI_ERR_OLEMEMALLOC 100
#define OLEUI_ERR_STRUCTURENULL 101
#define OLEUI_ERR_STRUCTUREINVALID 102
#define OLEUI_ERR_CBSTRUCTINCORRECT 103
#define OLEUI_ERR_HWNDOWNERINVALID 104
#define OLEUI_ERR_LPSZCAPTIONINVALID 105
#define OLEUI_ERR_LPFNHOOKINVALID 106
#define OLEUI_ERR_HINSTANCEINVALID 107
#define OLEUI_ERR_LPSZTEMPLATEINVALID 108
#define OLEUI_ERR_HRESOURCEINVALID 109
#define OLEUI_ERR_FINDTEMPLATEFAILURE 110
#define OLEUI_ERR_LOADTEMPLATEFAILURE 111
#define OLEUI_ERR_DIALOGFAILURE 112
#define OLEUI_ERR_LOCALMEMALLOC 113
#define OLEUI_ERR_GLOBALMEMALLOC 114
#define OLEUI_ERR_LOADSTRING 115

#define OLEUI_BZERR_HTASKINVALID (OLEUI_ERR_STANDARDMAX + 0)
#define OLEUI_BZ_SWITCHTOSELECTED (OLEUI_ERR_STANDARDMAX + 1)
#define OLEUI_BZ_RETRYSELECTED (OLEUI_ERR_STANDARDMAX + 2)
#define OLEUI_BZ_CALLUNBLOCKED (OLEUI_ERR_STANDARDMAX + 3)
#define OLEUI_ERR_STANDARDMAX 116

typedef UINT(CALLBACK *LPFNOLEUIHOOK)(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

typedef struct tagOLEUIBUSYW {
    DWORD cbStruct;
    DWORD dwFlags;
    HWND hWndOwner;
    LPCWSTR lpszCaption;
    LPFNOLEUIHOOK lpfnHook;
    LPARAM lCustData;
    HINSTANCE hInstance;
    LPCWSTR lpszTemplate;
    HRSRC hResource;
    HTASK hTask;
    HWND *lphWndDialog;
} OLEUIBUSYW, *POLEUIBUSYW, *LPOLEUIBUSYW;

OLEDLGAPI UINT WINAPI OleUIBusyW(LPOLEUIBUSYW busy);

/* what the dialogs report in the fields of their structures. */
#define IOF_SELECTCREATENEW 0x00000002
#define IOF_SELECTCREATEFROMFILE 0x00000004
#define IOF_CHECKLINK 0x00000008
#define IOF_CHECKDISPLAYASICON 0x00000010
#define IOF_CREATENEWOBJECT 0x00000020
#define IOF_CREATEFILEOBJECT 0x00000040
#define IOF_CREATELINKOBJECT 0x00000080
#define IOF_DISABLELINK 0x00000100
#define IOF_VERIFYSERVERSEXIST 0x00000200
#define IOF_DISABLEDISPLAYASICON 0x00000400
#define PSF_SELECTPASTE 0x00000002
#define PSF_SELECTPASTELINK 0x00000004
#define PSF_CHECKDISPLAYASICON 0x00000008
#define PSF_DISABLEDISPLAYASICON 0x00000010
#define OLEUI_IOERR_LPSZFILEINVALID 116
#define OLEUI_IOERR_LPFORMATETCINVALID 119
#define OLEUI_IOERR_PPVOBJINVALID 120
#define OLEUI_IOERR_LPIOLECLIENTSITEINVALID 121
#define OLEUI_IOERR_LPISTORAGEINVALID 122
#define OLEUI_IOERR_SCODEHASERROR 123
#define OLEUI_IOERR_LPCLSIDEXCLUDEINVALID 124
#define OLEUI_IOERR_CCHFILEINVALID 125
#define OLEUI_PSERR_SRCDATAOBJECTINVALID 116
#define OLEUI_PSERR_ARRPASTEENTRIESINVALID 117
#define OLEUI_PSERR_ARRLINKTYPESINVALID 118
#define OLEUI_PSERR_GETCLIPBOARDFAILED 120
#define OLEUI_CSERR_LINKCNTRNULL 116
#define OLEUI_CSERR_FROMNOTNULL 118
#define OLEUI_CSERR_TONOTNULL 119
#define OLEUI_CSERR_SOURCENULL 120
#define OLEUI_CSERR_SOURCEINVALID 121
#define OLEUI_ELERR_LINKCNTRNULL 116
#define OLEUI_ELERR_LINKCNTRINVALID 117
#define OLEUI_CTERR_CLASSIDINVALID 117
#define OLEUI_CTERR_DVASPECTINVALID 118
#define OLEUI_CTERR_CBFORMATINVALID 119
#define OLEUI_CTERR_HMETAPICTINVALID 120
#define OLEUI_CTERR_STRINGINVALID 121
#define OLEUI_CIERR_MUSTHAVECLSID 116
#define OLEUI_CIERR_MUSTHAVECURRENTMETAPICT 117
#define OLEUI_CIERR_SZICONEXEINVALID 118

#define OLEUI_STANDARD_FIELDS                                                                                          \
    DWORD cbStruct;                                                                                                    \
    DWORD dwFlags;                                                                                                     \
    HWND hWndOwner;                                                                                                    \
    LPCWSTR lpszCaption;                                                                                               \
    LPFNOLEUIHOOK lpfnHook;                                                                                            \
    LPARAM lCustData;                                                                                                  \
    HINSTANCE hInstance;                                                                                               \
    LPCWSTR lpszTemplate;                                                                                              \
    HRSRC hResource

typedef struct tagOLEUIINSERTOBJECTW {
    OLEUI_STANDARD_FIELDS;
    CLSID clsid;
    LPWSTR lpszFile;
    UINT cchFile;
    UINT cClsidExclude;
    LPCLSID lpClsidExclude;
    IID iid;
    DWORD oleRender;
    FORMATETC *lpFormatEtc;
    IOleClientSite *lpIOleClientSite;
    IStorage *lpIStorage;
    void **ppvObj;
    LONG sc;
    HGLOBAL hMetaPict;
} OLEUIINSERTOBJECTW, *POLEUIINSERTOBJECTW, *LPOLEUIINSERTOBJECTW;

typedef struct tagOLEUIPASTEENTRYW {
    FORMATETC fmtetc;
    LPCWSTR lpstrFormatName;
    LPCWSTR lpstrResultText;
    DWORD dwFlags;
    DWORD dwScratchSpace;
} OLEUIPASTEENTRYW, *POLEUIPASTEENTRYW, *LPOLEUIPASTEENTRYW;

typedef struct tagOLEUIPASTESPECIALW {
    OLEUI_STANDARD_FIELDS;
    IDataObject *lpSrcDataObj;
    OLEUIPASTEENTRYW *arrPasteEntries;
    int cPasteEntries;
    UINT *arrLinkTypes;
    int cLinkTypes;
    UINT cClsidExclude;
    LPCLSID lpClsidExclude;
    int nSelectedIndex;
    BOOL fLink;
    HGLOBAL hMetaPict;
    SIZEL sizel;
} OLEUIPASTESPECIALW, *POLEUIPASTESPECIALW, *LPOLEUIPASTESPECIALW;

typedef struct IOleUILinkContainerW IOleUILinkContainerW, *LPOLEUILINKCONTAINERW;
struct IOleUILinkContainerW {
    const struct IOleUILinkContainerWVtbl {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(IOleUILinkContainerW *This, REFIID riid, void **object);
        ULONG(STDMETHODCALLTYPE *AddRef)(IOleUILinkContainerW *This);
        ULONG(STDMETHODCALLTYPE *Release)(IOleUILinkContainerW *This);
        DWORD(STDMETHODCALLTYPE *GetNextLink)(IOleUILinkContainerW *This, DWORD link);
        HRESULT(STDMETHODCALLTYPE *SetLinkUpdateOptions)(IOleUILinkContainerW *This, DWORD link, DWORD options);
        HRESULT(STDMETHODCALLTYPE *GetLinkUpdateOptions)(IOleUILinkContainerW *This, DWORD link, DWORD *options);
        HRESULT(STDMETHODCALLTYPE *SetLinkSource)(IOleUILinkContainerW *This, DWORD link, LPWSTR display_name,
                                                  ULONG file_length, ULONG *eaten, BOOL validate);
        HRESULT(STDMETHODCALLTYPE *GetLinkSource)(IOleUILinkContainerW *This, DWORD link, LPWSTR *display_name,
                                                  ULONG *file_length, LPWSTR *full_type, LPWSTR *short_type,
                                                  BOOL *available, BOOL *selected);
        HRESULT(STDMETHODCALLTYPE *OpenLinkSource)(IOleUILinkContainerW *This, DWORD link);
        HRESULT(STDMETHODCALLTYPE *UpdateLink)(IOleUILinkContainerW *This, DWORD link, BOOL show_error,
                                               BOOL error_action);
        HRESULT(STDMETHODCALLTYPE *CancelLink)(IOleUILinkContainerW *This, DWORD link);
    } *lpVtbl;
};

typedef struct tagOLEUICHANGESOURCEW {
    OLEUI_STANDARD_FIELDS;
    void *lpOFN;
    DWORD dwReserved1[4];
    LPOLEUILINKCONTAINERW lpOleUILinkContainer;
    DWORD dwLink;
    LPWSTR lpszDisplayName;
    ULONG nFileLength;
    LPWSTR lpszFrom;
    LPWSTR lpszTo;
} OLEUICHANGESOURCEW, *POLEUICHANGESOURCEW, *LPOLEUICHANGESOURCEW;

typedef struct tagOLEUICHANGEICONW {
    OLEUI_STANDARD_FIELDS;
    HGLOBAL hMetaPict;
    CLSID clsid;
    WCHAR szIconExe[MAX_PATH];
    int cchIconExe;
} OLEUICHANGEICONW, *POLEUICHANGEICONW, *LPOLEUICHANGEICONW;

typedef struct tagOLEUICONVERTW {
    OLEUI_STANDARD_FIELDS;
    CLSID clsid;
    CLSID clsidConvertDefault;
    CLSID clsidActivateDefault;
    CLSID clsidNew;
    DWORD dvAspect;
    WORD wFormat;
    BOOL fIsLinkedObject;
    HGLOBAL hMetaPict;
    LPWSTR lpszUserType;
    BOOL fObjectsIconChanged;
    LPWSTR lpszDefLabel;
    UINT cClsidExclude;
    LPCLSID lpClsidExclude;
} OLEUICONVERTW, *POLEUICONVERTW, *LPOLEUICONVERTW;

typedef struct tagOLEUIEDITLINKSW {
    OLEUI_STANDARD_FIELDS;
    LPOLEUILINKCONTAINERW lpOleUILinkContainer;
} OLEUIEDITLINKSW, *POLEUIEDITLINKSW, *LPOLEUIEDITLINKSW;

OLEDLGAPI UINT WINAPI OleUIInsertObjectW(LPOLEUIINSERTOBJECTW insert);
OLEDLGAPI UINT WINAPI OleUIPasteSpecialW(LPOLEUIPASTESPECIALW paste);
OLEDLGAPI UINT WINAPI OleUIChangeSourceW(LPOLEUICHANGESOURCEW change);
OLEDLGAPI UINT WINAPI OleUIChangeIconW(LPOLEUICHANGEICONW change);
OLEDLGAPI UINT WINAPI OleUIConvertW(LPOLEUICONVERTW convert);
OLEDLGAPI UINT WINAPI OleUIEditLinksW(LPOLEUIEDITLINKSW edit);

#endif
