/* ole data transfer: data objects, storage mediums, drag and drop and the ole32.dll clipboard. */
#ifndef MIME_WIN32_OLEIDL_H
#define MIME_WIN32_OLEIDL_H

#include <mime/win32/objidl.h>
#include <mime/win32/winuser.h>

/* ------------------------------------------------------------------ errors */

#ifndef OLE_E_ADVISENOTSUPPORTED
#define OLE_E_ADVISENOTSUPPORTED ((HRESULT)0x80040003L)
#endif
#ifndef OLE_E_ENUM_NOMORE
#define OLE_E_ENUM_NOMORE ((HRESULT)0x80040002L)
#endif
#ifndef OLE_E_NOCONNECTION
#define OLE_E_NOCONNECTION ((HRESULT)0x80040004L)
#endif
#ifndef OLE_E_STATIC
#define OLE_E_STATIC ((HRESULT)0x8004000BL)
#endif
#ifndef OLEOBJ_E_NOVERBS
#define OLEOBJ_E_NOVERBS ((HRESULT)0x80040180L)
#endif
#ifndef OLE_E_NOTRUNNING
#define OLE_E_NOTRUNNING ((HRESULT)0x80040005L)
#endif
#ifndef DV_E_FORMATETC
#define DV_E_FORMATETC ((HRESULT)0x80040064L)
#endif
#ifndef DV_E_LINDEX
#define DV_E_LINDEX ((HRESULT)0x80040068L)
#endif
#ifndef DV_E_TYMED
#define DV_E_TYMED ((HRESULT)0x80040069L)
#endif
#ifndef DV_E_DVASPECT
#define DV_E_DVASPECT ((HRESULT)0x8004006BL)
#endif
#ifndef DATA_S_SAMEFORMATETC
#define DATA_S_SAMEFORMATETC ((HRESULT)0x00040130L)
#endif
#ifndef DRAGDROP_E_NOTREGISTERED
#define DRAGDROP_E_NOTREGISTERED ((HRESULT)0x80040100L)
#endif
#ifndef DRAGDROP_E_ALREADYREGISTERED
#define DRAGDROP_E_ALREADYREGISTERED ((HRESULT)0x80040101L)
#endif
#ifndef DRAGDROP_E_INVALIDHWND
#define DRAGDROP_E_INVALIDHWND ((HRESULT)0x80040102L)
#endif
#ifndef DRAGDROP_S_DROP
#define DRAGDROP_S_DROP ((HRESULT)0x00040100L)
#endif
#ifndef DRAGDROP_S_CANCEL
#define DRAGDROP_S_CANCEL ((HRESULT)0x00040101L)
#endif
#ifndef DRAGDROP_S_USEDEFAULTCURSORS
#define DRAGDROP_S_USEDEFAULTCURSORS ((HRESULT)0x00040102L)
#endif
#ifndef CLIPBRD_E_CANT_OPEN
#define CLIPBRD_E_CANT_OPEN ((HRESULT)0x800401D0L)
#endif
#ifndef CLIPBRD_E_CANT_EMPTY
#define CLIPBRD_E_CANT_EMPTY ((HRESULT)0x800401D1L)
#endif
#ifndef CLIPBRD_E_CANT_SET
#define CLIPBRD_E_CANT_SET ((HRESULT)0x800401D2L)
#endif
#ifndef CLIPBRD_E_BAD_DATA
#define CLIPBRD_E_BAD_DATA ((HRESULT)0x800401D3L)
#endif
#ifndef CLIPBRD_E_CANT_CLOSE
#define CLIPBRD_E_CANT_CLOSE ((HRESULT)0x800401D4L)
#endif

/* ------------------------------------------------------------------ standard clipboard formats */

#ifndef CF_TEXT
#define CF_TEXT 1
#define CF_BITMAP 2
#define CF_METAFILEPICT 3
#define CF_SYLK 4
#define CF_DIF 5
#define CF_TIFF 6
#define CF_OEMTEXT 7
#define CF_DIB 8
#define CF_PALETTE 9
#define CF_PENDATA 10
#define CF_RIFF 11
#define CF_WAVE 12
#define CF_UNICODETEXT 13
#define CF_ENHMETAFILE 14
#define CF_HDROP 15
#define CF_LOCALE 16
#define CF_DIBV5 17
#endif

/* ------------------------------------------------------------------ formats and mediums */

#define DVASPECT_CONTENT 1
#define DVASPECT_THUMBNAIL 2
#define DVASPECT_ICON 4
#define DVASPECT_DOCPRINT 8

#define TYMED_NULL 0
#define TYMED_HGLOBAL 1
#define TYMED_FILE 2
#define TYMED_ISTREAM 4
#define TYMED_ISTORAGE 8
#define TYMED_GDI 16
#define TYMED_MFPICT 32
#define TYMED_ENHMF 64

#define DATADIR_GET 1
#define DATADIR_SET 2

typedef struct tagDVTARGETDEVICE {
    DWORD tdSize;
    WORD tdDriverNameOffset;
    WORD tdDeviceNameOffset;
    WORD tdPortNameOffset;
    WORD tdExtDevmodeOffset;
    BYTE tdData[1];
} DVTARGETDEVICE;

typedef struct tagFORMATETC {
    CLIPFORMAT cfFormat;
    DVTARGETDEVICE *ptd;
    DWORD dwAspect;
    LONG lindex;
    DWORD tymed;
} FORMATETC, *LPFORMATETC;

typedef struct IStream IStream;
typedef struct IStorage IStorage;
typedef struct IAdviseSink IAdviseSink;
typedef struct IEnumSTATDATA IEnumSTATDATA;
typedef HANDLE HMETAFILEPICT;

typedef struct tagSTGMEDIUM {
    DWORD tymed;
    union {
        HBITMAP hBitmap;
        HMETAFILEPICT hMetaFilePict;
        HENHMETAFILE hEnhMetaFile;
        HGLOBAL hGlobal;
        LPOLESTR lpszFileName;
        IStream *pstm;
        IStorage *pstg;
    };
    IUnknown *pUnkForRelease;
} STGMEDIUM, *LPSTGMEDIUM;

/* ------------------------------------------------------------------ IEnumFORMATETC */

typedef struct IEnumFORMATETC IEnumFORMATETC;
typedef struct IEnumFORMATETCVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumFORMATETC *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumFORMATETC *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumFORMATETC *This);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumFORMATETC *This, ULONG count, FORMATETC *items, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumFORMATETC *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumFORMATETC *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumFORMATETC *This, IEnumFORMATETC **clone);
} IEnumFORMATETCVtbl;
struct IEnumFORMATETC {
    const IEnumFORMATETCVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ IDataObject */

typedef struct IDataObject IDataObject, *LPDATAOBJECT;
typedef struct IDataObjectVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDataObject *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDataObject *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDataObject *This);
    HRESULT(STDMETHODCALLTYPE *GetData)(IDataObject *This, FORMATETC *format, STGMEDIUM *medium);
    HRESULT(STDMETHODCALLTYPE *GetDataHere)(IDataObject *This, FORMATETC *format, STGMEDIUM *medium);
    HRESULT(STDMETHODCALLTYPE *QueryGetData)(IDataObject *This, FORMATETC *format);
    HRESULT(STDMETHODCALLTYPE *GetCanonicalFormatEtc)(IDataObject *This, FORMATETC *in, FORMATETC *out);
    HRESULT(STDMETHODCALLTYPE *SetData)(IDataObject *This, FORMATETC *format, STGMEDIUM *medium, BOOL release);
    HRESULT(STDMETHODCALLTYPE *EnumFormatEtc)(IDataObject *This, DWORD direction, IEnumFORMATETC **enumerator);
    HRESULT(STDMETHODCALLTYPE *DAdvise)(IDataObject *This, FORMATETC *format, DWORD flags, IAdviseSink *sink,
                                        DWORD *connection);
    HRESULT(STDMETHODCALLTYPE *DUnadvise)(IDataObject *This, DWORD connection);
    HRESULT(STDMETHODCALLTYPE *EnumDAdvise)(IDataObject *This, IEnumSTATDATA **enumerator);
} IDataObjectVtbl;
struct IDataObject {
    const IDataObjectVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ advise sinks and holders */

#define ADVF_NODATA 1
#define ADVF_PRIMEFIRST 2
#define ADVF_ONLYONCE 4
#define ADVF_DATAONSTOP 64
#define ADVFCACHE_NOHANDLER 8
#define ADVFCACHE_FORCEBUILTIN 16
#define ADVFCACHE_ONSAVE 32

typedef struct IAdviseSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IAdviseSink *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IAdviseSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(IAdviseSink *This);
    void(STDMETHODCALLTYPE *OnDataChange)(IAdviseSink *This, FORMATETC *format, STGMEDIUM *medium);
    void(STDMETHODCALLTYPE *OnViewChange)(IAdviseSink *This, DWORD aspect, LONG index);
    void(STDMETHODCALLTYPE *OnRename)(IAdviseSink *This, IMoniker *moniker);
    void(STDMETHODCALLTYPE *OnSave)(IAdviseSink *This);
    void(STDMETHODCALLTYPE *OnClose)(IAdviseSink *This);
} IAdviseSinkVtbl;
struct IAdviseSink {
    const IAdviseSinkVtbl *lpVtbl;
};

typedef struct tagSTATDATA {
    FORMATETC formatetc;
    DWORD advf;
    IAdviseSink *pAdvSink;
    DWORD dwConnection;
} STATDATA, *LPSTATDATA;

typedef struct IEnumSTATDATAVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumSTATDATA *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumSTATDATA *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumSTATDATA *This);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumSTATDATA *This, ULONG count, STATDATA *items, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumSTATDATA *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumSTATDATA *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumSTATDATA *This, IEnumSTATDATA **clone);
} IEnumSTATDATAVtbl;
struct IEnumSTATDATA {
    const IEnumSTATDATAVtbl *lpVtbl;
};

typedef struct IDataAdviseHolder IDataAdviseHolder, *LPDATAADVISEHOLDER;
typedef struct IDataAdviseHolderVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDataAdviseHolder *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDataAdviseHolder *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDataAdviseHolder *This);
    HRESULT(STDMETHODCALLTYPE *Advise)(IDataAdviseHolder *This, IDataObject *data, FORMATETC *format, DWORD advf,
                                       IAdviseSink *sink, DWORD *connection);
    HRESULT(STDMETHODCALLTYPE *Unadvise)(IDataAdviseHolder *This, DWORD connection);
    HRESULT(STDMETHODCALLTYPE *EnumAdvise)(IDataAdviseHolder *This, IEnumSTATDATA **enumerator);
    HRESULT(STDMETHODCALLTYPE *SendOnDataChange)(IDataAdviseHolder *This, IDataObject *data, DWORD reserved,
                                                 DWORD advf);
} IDataAdviseHolderVtbl;
struct IDataAdviseHolder {
    const IDataAdviseHolderVtbl *lpVtbl;
};

typedef struct IOleAdviseHolder IOleAdviseHolder, *LPOLEADVISEHOLDER;
typedef struct IOleAdviseHolderVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IOleAdviseHolder *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IOleAdviseHolder *This);
    ULONG(STDMETHODCALLTYPE *Release)(IOleAdviseHolder *This);
    HRESULT(STDMETHODCALLTYPE *Advise)(IOleAdviseHolder *This, IAdviseSink *sink, DWORD *connection);
    HRESULT(STDMETHODCALLTYPE *Unadvise)(IOleAdviseHolder *This, DWORD connection);
    HRESULT(STDMETHODCALLTYPE *EnumAdvise)(IOleAdviseHolder *This, IEnumSTATDATA **enumerator);
    HRESULT(STDMETHODCALLTYPE *SendOnRename)(IOleAdviseHolder *This, IMoniker *moniker);
    HRESULT(STDMETHODCALLTYPE *SendOnSave)(IOleAdviseHolder *This);
    HRESULT(STDMETHODCALLTYPE *SendOnClose)(IOleAdviseHolder *This);
} IOleAdviseHolderVtbl;
struct IOleAdviseHolder {
    const IOleAdviseHolderVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ embedded objects */

typedef struct IOleObject IOleObject, *LPOLEOBJECT;
typedef struct IOleClientSite IOleClientSite, *LPOLECLIENTSITE;
typedef struct IOleInPlaceActiveObject IOleInPlaceActiveObject, *LPOLEINPLACEACTIVEOBJECT;

#define USERCLASSTYPE_FULL 1
#define USERCLASSTYPE_SHORT 2
#define USERCLASSTYPE_APPNAME 3

#define OLERENDER_NONE 0
#define OLERENDER_DRAW 1
#define OLERENDER_FORMAT 2
#define OLERENDER_ASIS 3

typedef struct IRunnableObject IRunnableObject, *LPRUNNABLEOBJECT;
typedef struct IRunnableObjectVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IRunnableObject *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IRunnableObject *This);
    ULONG(STDMETHODCALLTYPE *Release)(IRunnableObject *This);
    HRESULT(STDMETHODCALLTYPE *GetRunningClass)(IRunnableObject *This, CLSID *clsid);
    HRESULT(STDMETHODCALLTYPE *Run)(IRunnableObject *This, IBindCtx *context);
    BOOL(STDMETHODCALLTYPE *IsRunning)(IRunnableObject *This);
    HRESULT(STDMETHODCALLTYPE *LockRunning)(IRunnableObject *This, BOOL lock, BOOL last_unlock_closes);
    HRESULT(STDMETHODCALLTYPE *SetContainedObject)(IRunnableObject *This, BOOL contained);
} IRunnableObjectVtbl;
struct IRunnableObject {
    const IRunnableObjectVtbl *lpVtbl;
};

typedef struct tagOLEVERB {
    LONG lVerb;
    LPOLESTR lpszVerbName;
    DWORD fuFlags;
    DWORD grfAttribs;
} OLEVERB, *LPOLEVERB;

typedef struct IEnumOLEVERB IEnumOLEVERB, *LPENUMOLEVERB;
typedef struct IEnumOLEVERBVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumOLEVERB *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumOLEVERB *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumOLEVERB *This);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumOLEVERB *This, ULONG count, OLEVERB *items, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumOLEVERB *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumOLEVERB *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumOLEVERB *This, IEnumOLEVERB **clone);
} IEnumOLEVERBVtbl;
struct IEnumOLEVERB {
    const IEnumOLEVERBVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ in place activation frames */

typedef HGLOBAL HOLEMENU;
typedef RECT BORDERWIDTHS, *LPBORDERWIDTHS;
typedef const RECT *LPCBORDERWIDTHS;

typedef struct tagOleMenuGroupWidths {
    LONG width[6];
} OLEMENUGROUPWIDTHS, *LPOLEMENUGROUPWIDTHS;

typedef struct tagOIFI {
    UINT cb;
    BOOL fMDIApp;
    HWND hwndFrame;
    HACCEL haccel;
    UINT cAccelEntries;
} OLEINPLACEFRAMEINFO, *LPOLEINPLACEFRAMEINFO;

typedef struct IOleInPlaceFrame IOleInPlaceFrame, *LPOLEINPLACEFRAME;
typedef struct IOleInPlaceFrameVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IOleInPlaceFrame *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IOleInPlaceFrame *This);
    ULONG(STDMETHODCALLTYPE *Release)(IOleInPlaceFrame *This);
    HRESULT(STDMETHODCALLTYPE *GetWindow)(IOleInPlaceFrame *This, HWND *hwnd);
    HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)(IOleInPlaceFrame *This, BOOL enter);
    HRESULT(STDMETHODCALLTYPE *GetBorder)(IOleInPlaceFrame *This, RECT *border);
    HRESULT(STDMETHODCALLTYPE *RequestBorderSpace)(IOleInPlaceFrame *This, LPCBORDERWIDTHS widths);
    HRESULT(STDMETHODCALLTYPE *SetBorderSpace)(IOleInPlaceFrame *This, LPCBORDERWIDTHS widths);
    HRESULT(STDMETHODCALLTYPE *SetActiveObject)(IOleInPlaceFrame *This, IOleInPlaceActiveObject *object,
                                                LPCOLESTR name);
    HRESULT(STDMETHODCALLTYPE *InsertMenus)(IOleInPlaceFrame *This, HMENU shared, OLEMENUGROUPWIDTHS *widths);
    HRESULT(STDMETHODCALLTYPE *SetMenu)(IOleInPlaceFrame *This, HMENU shared, HOLEMENU descriptor,
                                        HWND active_object);
    HRESULT(STDMETHODCALLTYPE *RemoveMenus)(IOleInPlaceFrame *This, HMENU shared);
    HRESULT(STDMETHODCALLTYPE *SetStatusText)(IOleInPlaceFrame *This, LPCOLESTR text);
    HRESULT(STDMETHODCALLTYPE *EnableModeless)(IOleInPlaceFrame *This, BOOL enable);
    HRESULT(STDMETHODCALLTYPE *TranslateAccelerator)(IOleInPlaceFrame *This, MSG *msg, WORD id);
} IOleInPlaceFrameVtbl;
struct IOleInPlaceFrame {
    const IOleInPlaceFrameVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ drag and drop */

#define DROPEFFECT_NONE 0
#define DROPEFFECT_COPY 1
#define DROPEFFECT_MOVE 2
#define DROPEFFECT_LINK 4
#define DROPEFFECT_SCROLL 0x80000000

#ifndef MK_ALT
#define MK_ALT 0x0020
#endif

typedef struct IDropSource IDropSource, *LPDROPSOURCE;
typedef struct IDropSourceVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDropSource *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDropSource *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDropSource *This);
    HRESULT(STDMETHODCALLTYPE *QueryContinueDrag)(IDropSource *This, BOOL escape, DWORD key_state);
    HRESULT(STDMETHODCALLTYPE *GiveFeedback)(IDropSource *This, DWORD effect);
} IDropSourceVtbl;
struct IDropSource {
    const IDropSourceVtbl *lpVtbl;
};

typedef struct IDropTarget IDropTarget, *LPDROPTARGET;
typedef struct IDropTargetVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDropTarget *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDropTarget *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDropTarget *This);
    HRESULT(STDMETHODCALLTYPE *DragEnter)(IDropTarget *This, IDataObject *data, DWORD key_state, POINTL pt,
                                          DWORD *effect);
    HRESULT(STDMETHODCALLTYPE *DragOver)(IDropTarget *This, DWORD key_state, POINTL pt, DWORD *effect);
    HRESULT(STDMETHODCALLTYPE *DragLeave)(IDropTarget *This);
    HRESULT(STDMETHODCALLTYPE *Drop)(IDropTarget *This, IDataObject *data, DWORD key_state, POINTL pt,
                                     DWORD *effect);
} IDropTargetVtbl;
struct IDropTarget {
    const IDropTargetVtbl *lpVtbl;
};

MIME_DEFINE_GUID(IID_IDataObject, 0x0000010E, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IEnumFORMATETC, 0x00000103, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IDropSource, 0x00000121, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IDropTarget, 0x00000122, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IEnumOLEVERB, 0x00000104, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IEnumSTATDATA, 0x00000105, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IAdviseSink, 0x0000010F, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IDataAdviseHolder, 0x00000110, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IOleAdviseHolder, 0x00000111, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IOleObject, 0x00000112, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IOleInPlaceFrame, 0x00000116, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IRunnableObject, 0x00000126, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

/* ------------------------------------------------------------------ ole32 ole functions */

WINOLEAPI HRESULT WINAPI OleInitialize(void *reserved);
WINOLEAPI void WINAPI OleUninitialize(void);
WINOLEAPI HRESULT WINAPI OleSetClipboard(IDataObject *data);
WINOLEAPI HRESULT WINAPI OleGetClipboard(IDataObject **data);
WINOLEAPI HRESULT WINAPI OleFlushClipboard(void);
WINOLEAPI HRESULT WINAPI OleIsCurrentClipboard(IDataObject *data);
WINOLEAPI HRESULT WINAPI RegisterDragDrop(HWND hwnd, IDropTarget *target);
WINOLEAPI HRESULT WINAPI RevokeDragDrop(HWND hwnd);
WINOLEAPI HRESULT WINAPI DoDragDrop(IDataObject *data, IDropSource *source, DWORD ok_effects, DWORD *effect);
WINOLEAPI void WINAPI ReleaseStgMedium(STGMEDIUM *medium);

#endif
