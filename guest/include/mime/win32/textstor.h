/*
 * the text services framework text store of textstor.h: how a document offers its text, selection and layout to
 * the text services that edit it. interfaces are C structs whose first member points to a vtable.
 */
#ifndef MIME_WIN32_TEXTSTOR_H
#define MIME_WIN32_TEXTSTOR_H

#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/oleidl.h>

#define TS_E_INVALIDPOS ((HRESULT)0x80040200L)
#define TS_E_NOLOCK ((HRESULT)0x80040201L)
#define TS_E_NOOBJECT ((HRESULT)0x80040202L)
#define TS_E_NOSERVICE ((HRESULT)0x80040203L)
#define TS_E_NOINTERFACE ((HRESULT)0x80040204L)
#define TS_E_NOSELECTION ((HRESULT)0x80040205L)
#define TS_E_NOLAYOUT ((HRESULT)0x80040206L)
#define TS_E_INVALIDPOINT ((HRESULT)0x80040207L)
#define TS_E_SYNCHRONOUS ((HRESULT)0x80040208L)
#define TS_E_READONLY ((HRESULT)0x80040209L)
#define TS_E_FORMAT ((HRESULT)0x8004020AL)
#define TS_S_ASYNC ((HRESULT)0x00040300L)

#define TS_DEFAULT_SELECTION (~0u)

/* TS_STATUS dynamic and static flags. */
#define TS_SD_READONLY 0x1
#define TS_SD_LOADING 0x2
#define TS_SS_DISJOINTSEL 0x1
#define TS_SS_REGIONS 0x2
#define TS_SS_TRANSITORY 0x4
#define TS_SS_NOHIDDENTEXT 0x8

/* what a sink asks to be told about. */
#define TS_AS_TEXT_CHANGE 0x1
#define TS_AS_SEL_CHANGE 0x2
#define TS_AS_LAYOUT_CHANGE 0x4
#define TS_AS_ATTR_CHANGE 0x8
#define TS_AS_STATUS_CHANGE 0x10
#define TS_AS_ALL_SINKS 0x1F

#define TS_LF_SYNC 0x1
#define TS_LF_READ 0x2
#define TS_LF_READWRITE 0x6

#define TS_CHAR_EMBEDDED 0xFFFC
#define TS_CHAR_REGION 0x0
#define TS_CHAR_REPLACEMENT 0xFFFD

#define TS_IAS_NOQUERY 0x1
#define TS_IAS_QUERYONLY 0x2
#define TS_ST_CORRECTION 0x1
#define TS_TC_CORRECTION 0x1

typedef DWORD TsViewCookie;
typedef GUID TS_ATTRID;

typedef enum {
    TS_AE_NONE = 0,
    TS_AE_START = 1,
    TS_AE_END = 2,
} TsActiveSelEnd;

typedef enum {
    TS_RT_PLAIN = 0,
    TS_RT_HIDDEN = 1,
    TS_RT_OPAQUE = 2,
} TsRunType;

typedef enum {
    TS_LC_CREATE = 0,
    TS_LC_CHANGE = 1,
    TS_LC_DESTROY = 2,
} TsLayoutCode;

typedef struct TS_STATUS {
    DWORD dwDynamicFlags;
    DWORD dwStaticFlags;
} TS_STATUS;

typedef struct TS_TEXTCHANGE {
    LONG acpStart;
    LONG acpOldEnd;
    LONG acpNewEnd;
} TS_TEXTCHANGE;

typedef struct TS_SELECTIONSTYLE {
    TsActiveSelEnd ase;
    BOOL fInterimChar;
} TS_SELECTIONSTYLE;

typedef struct TS_SELECTION_ACP {
    LONG acpStart;
    LONG acpEnd;
    TS_SELECTIONSTYLE style;
} TS_SELECTION_ACP;

typedef struct TS_RUNINFO {
    ULONG uCount;
    TsRunType type;
} TS_RUNINFO;

typedef struct TS_ATTRVAL {
    TS_ATTRID idAttr;
    DWORD dwOverlapId;
    VARIANT varValue;
} TS_ATTRVAL;

MIME_DEFINE_GUID(IID_ITextStoreACPSink, 0x22D44C94, 0xA419, 0x4542, 0xA2, 0x72, 0xAE, 0x26, 0x09, 0x3E, 0xCE, 0xCF);
MIME_DEFINE_GUID(IID_ITextStoreACP, 0x28888FE3, 0xC2A0, 0x483A, 0xA3, 0xEA, 0x8C, 0xB1, 0xCE, 0x51, 0xFF, 0x3D);

typedef struct ITextStoreACP ITextStoreACP;
typedef struct ITextStoreACPSink ITextStoreACPSink;

typedef struct ITextStoreACPVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITextStoreACP *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITextStoreACP *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITextStoreACP *This);
    HRESULT(STDMETHODCALLTYPE *AdviseSink)(ITextStoreACP *This, REFIID riid, IUnknown *punk, DWORD dwMask);
    HRESULT(STDMETHODCALLTYPE *UnadviseSink)(ITextStoreACP *This, IUnknown *punk);
    HRESULT(STDMETHODCALLTYPE *RequestLock)(ITextStoreACP *This, DWORD dwLockFlags, HRESULT *phrSession);
    HRESULT(STDMETHODCALLTYPE *GetStatus)(ITextStoreACP *This, TS_STATUS *pdcs);
    HRESULT(STDMETHODCALLTYPE *QueryInsert)(ITextStoreACP *This, LONG acpTestStart, LONG acpTestEnd, ULONG cch, LONG *pacpResultStart, LONG *pacpResultEnd);
    HRESULT(STDMETHODCALLTYPE *GetSelection)(ITextStoreACP *This, ULONG ulIndex, ULONG ulCount, TS_SELECTION_ACP *pSelection, ULONG *pcFetched);
    HRESULT(STDMETHODCALLTYPE *SetSelection)(ITextStoreACP *This, ULONG ulCount, const TS_SELECTION_ACP *pSelection);
    HRESULT(STDMETHODCALLTYPE *GetText)(ITextStoreACP *This, LONG acpStart, LONG acpEnd, WCHAR *pchPlain, ULONG cchPlainReq, ULONG *pcchPlainRet, TS_RUNINFO *prgRunInfo, ULONG cRunInfoReq, ULONG *pcRunInfoRet, LONG *pacpNext);
    HRESULT(STDMETHODCALLTYPE *SetText)(ITextStoreACP *This, DWORD dwFlags, LONG acpStart, LONG acpEnd, const WCHAR *pchText, ULONG cch, TS_TEXTCHANGE *pChange);
    HRESULT(STDMETHODCALLTYPE *GetFormattedText)(ITextStoreACP *This, LONG acpStart, LONG acpEnd, IDataObject **ppDataObject);
    HRESULT(STDMETHODCALLTYPE *GetEmbedded)(ITextStoreACP *This, LONG acpPos, REFGUID rguidService, REFIID riid, IUnknown **ppunk);
    HRESULT(STDMETHODCALLTYPE *QueryInsertEmbedded)(ITextStoreACP *This, const GUID *pguidService, const FORMATETC *pFormatEtc, BOOL *pfInsertable);
    HRESULT(STDMETHODCALLTYPE *InsertEmbedded)(ITextStoreACP *This, DWORD dwFlags, LONG acpStart, LONG acpEnd, IDataObject *pDataObject, TS_TEXTCHANGE *pChange);
    HRESULT(STDMETHODCALLTYPE *InsertTextAtSelection)(ITextStoreACP *This, DWORD dwFlags, const WCHAR *pchText, ULONG cch, LONG *pacpStart, LONG *pacpEnd, TS_TEXTCHANGE *pChange);
    HRESULT(STDMETHODCALLTYPE *InsertEmbeddedAtSelection)(ITextStoreACP *This, DWORD dwFlags, IDataObject *pDataObject, LONG *pacpStart, LONG *pacpEnd, TS_TEXTCHANGE *pChange);
    HRESULT(STDMETHODCALLTYPE *RequestSupportedAttrs)(ITextStoreACP *This, DWORD dwFlags, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs);
    HRESULT(STDMETHODCALLTYPE *RequestAttrsAtPosition)(ITextStoreACP *This, LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs, DWORD dwFlags);
    HRESULT(STDMETHODCALLTYPE *RequestAttrsTransitioningAtPosition)(ITextStoreACP *This, LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs, DWORD dwFlags);
    HRESULT(STDMETHODCALLTYPE *FindNextAttrTransition)(ITextStoreACP *This, LONG acpStart, LONG acpHalt, ULONG cFilterAttrs, const TS_ATTRID *paFilterAttrs, DWORD dwFlags, LONG *pacpNext, BOOL *pfFound, LONG *plFoundOffset);
    HRESULT(STDMETHODCALLTYPE *RetrieveRequestedAttrs)(ITextStoreACP *This, ULONG ulCount, TS_ATTRVAL *paAttrVals, ULONG *pcFetched);
    HRESULT(STDMETHODCALLTYPE *GetEndACP)(ITextStoreACP *This, LONG *pacp);
    HRESULT(STDMETHODCALLTYPE *GetActiveView)(ITextStoreACP *This, TsViewCookie *pvcView);
    HRESULT(STDMETHODCALLTYPE *GetACPFromPoint)(ITextStoreACP *This, TsViewCookie vcView, const POINT *ptScreen, DWORD dwFlags, LONG *pacp);
    HRESULT(STDMETHODCALLTYPE *GetTextExt)(ITextStoreACP *This, TsViewCookie vcView, LONG acpStart, LONG acpEnd, RECT *prc, BOOL *pfClipped);
    HRESULT(STDMETHODCALLTYPE *GetScreenExt)(ITextStoreACP *This, TsViewCookie vcView, RECT *prc);
    HRESULT(STDMETHODCALLTYPE *GetWnd)(ITextStoreACP *This, TsViewCookie vcView, HWND *phwnd);
} ITextStoreACPVtbl;
struct ITextStoreACP {
    const ITextStoreACPVtbl *lpVtbl;
};

typedef struct ITextStoreACPSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITextStoreACPSink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITextStoreACPSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITextStoreACPSink *This);
    HRESULT(STDMETHODCALLTYPE *OnTextChange)(ITextStoreACPSink *This, DWORD dwFlags, const TS_TEXTCHANGE *pChange);
    HRESULT(STDMETHODCALLTYPE *OnSelectionChange)(ITextStoreACPSink *This);
    HRESULT(STDMETHODCALLTYPE *OnLayoutChange)(ITextStoreACPSink *This, TsLayoutCode lcode, TsViewCookie vcView);
    HRESULT(STDMETHODCALLTYPE *OnStatusChange)(ITextStoreACPSink *This, DWORD dwFlags);
    HRESULT(STDMETHODCALLTYPE *OnAttrsChange)(ITextStoreACPSink *This, LONG acpStart, LONG acpEnd, ULONG cAttrs, const TS_ATTRID *paAttrs);
    HRESULT(STDMETHODCALLTYPE *OnLockGranted)(ITextStoreACPSink *This, DWORD dwLockFlags);
    HRESULT(STDMETHODCALLTYPE *OnStartEditTransaction)(ITextStoreACPSink *This);
    HRESULT(STDMETHODCALLTYPE *OnEndEditTransaction)(ITextStoreACPSink *This);
} ITextStoreACPSinkVtbl;
struct ITextStoreACPSink {
    const ITextStoreACPSinkVtbl *lpVtbl;
};
#endif
