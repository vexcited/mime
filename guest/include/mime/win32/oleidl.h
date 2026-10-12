/*
 * ole data transfer: data objects, storage mediums, drag and drop and the
 * ole clipboard of ole32.dll.
 */
#ifndef MIME_WIN32_OLEIDL_H
#define MIME_WIN32_OLEIDL_H

#include <mime/win32/objbase.h>
#include <mime/win32/winuser.h>

/* ------------------------------------------------------------------ errors */

#ifndef OLE_E_ADVISENOTSUPPORTED
#define OLE_E_ADVISENOTSUPPORTED ((HRESULT)0x80040003L)
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
