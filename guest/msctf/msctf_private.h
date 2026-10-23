#ifndef MIME_MSCTF_PRIVATE_H
#define MIME_MSCTF_PRIVATE_H

#define TFAPI DECLSPEC_EXPORT
#include <mime/win32/msctf.h>
#include <mime/win32/winuser.h>

void *CtfAlloc(SIZE_T size);
void CtfFree(void *p);
void CtfTrace(const char *format, ...);
extern BOOL g_ctf_trace;
#define CTF_TRACE(...) do { if (g_ctf_trace) CtfTrace(__VA_ARGS__); } while (0)

void CtfObjectCreated(void);
void CtfObjectDestroyed(void);

/* the object that embeds the interface. */
#define CTF_OBJ(type, field, iface) CONTAINING_RECORD(iface, type, field)

typedef struct SINK {
    DWORD Cookie;
    DWORD Index;
    GUID Iid;
    IUnknown *Sink;
} SINK;

typedef struct SINKS {
    SINK *Items;
    ULONG Count, Capacity;
} SINKS;

/*
 * the cookie holds the interface's index in `allowed` in its top byte and a per-interface sink number below.
 */
HRESULT SinksAdvise(SINKS *sinks, const IID *const *allowed, ULONG allowed_count, REFIID iid, IUnknown *sink,
                    DWORD *cookie);
HRESULT SinksUnadvise(SINKS *sinks, DWORD cookie);
void SinksFree(SINKS *sinks);
/* referenced copies of an interface's sinks, safe to call while the list changes. */
ULONG SinksSnapshot(SINKS *sinks, REFIID iid, IUnknown ***out);
void SinksSnapshotFree(IUnknown **items, ULONG count);

/* category.c */

TfGuidAtom CtfGuidAtom(REFGUID guid);
BOOL CtfAtomGuid(TfGuidAtom atom, GUID *guid);
/* the display attribute guid the host's input method uses for a TF_ATTR_* kind of text. */
BOOL CtfDisplayAttributeGuid(TF_DA_ATTR_INFO kind, GUID *guid);

typedef struct TM TM;
typedef struct DOC DOC;
typedef struct CTX CTX;
typedef struct RNG RNG;
typedef struct COMP COMP;
typedef struct COMPARTMENTS COMPARTMENTS;

/* kept in the context's list so its positions follow the text. */
struct RNG {
    ITfRangeACP Iface;
    LONG Refs;
    CTX *Context;
    LONG Start, End;
    TfGravity GravityStart, GravityEnd;
    BOOL Weak;  /* held by a composition, does not keep the context alive. */
    RNG *Next, *Prev;
};

struct COMP {
    ITfComposition Iface;
    ITfCompositionView View;
    LONG Refs;
    CTX *Context;
    RNG *Range;
    ITfCompositionSink *Sink;
    CLSID Owner;
    BOOL Ended;
    COMP *Next;
};

/* the running edit session and the ones waiting for the lock. */
typedef struct PENDING {
    ITfEditSession *Session;
    DWORD Flags;
    TfClientId Client;
    HRESULT *Result;
    BOOL Done, Waited;
    struct PENDING *Next;
} PENDING;

typedef struct PROP_SEGMENT {
    LONG Start, End;
    VARIANT Value;
} PROP_SEGMENT;

typedef struct PROP_ENTRY {
    GUID Guid;
    PROP_SEGMENT *Segments;
    ULONG Count, Capacity;
} PROP_ENTRY;

struct CTX {
    ITfContext Context;
    ITfInsertAtSelection Insert;
    ITfContextComposition Composition;
    ITfContextOwnerCompositionServices Services;
    ITfSource Source;
    ITfCompartmentMgr CompartmentMgr;
    ITfContextOwnerServices OwnerServices;
    ITextStoreACPSink StoreSink;
    LONG Refs;
    TM *Tm;
    DOC *Doc;
    TfClientId Client;
    IUnknown *Unknown;
    ITextStoreACP *Store;
    ITfContextOwnerCompositionSink *OwnerSink;
    BOOL SinkAdvised;
    SINKS Sinks;
    COMPARTMENTS *Compartments;
    RNG *Ranges;
    COMP *Compositions;
    PROP_ENTRY *Props;
    ULONG PropCount, PropCapacity;
    /* the lock the store granted, the running session's cookies and the notification state. */
    DWORD Lock;
    TfEditCookie CookieRead, CookieWrite, SessionCookie;
    DWORD CookieSerial;
    BOOL InSession, InNotify, SessionWrite, LockPosted;
    TfClientId SessionClient;
    PENDING *Pending;
    ITfEditRecord *Record;
    BOOL Destroying;
};

/* thread.c */
TM *TmCurrent(void);
HRESULT TmCreateObject(REFIID riid, void **object);
DOC *TmFocusDoc(TM *tm);
TfClientId TmClientId(TM *tm);
BOOL TmValidClient(TM *tm, TfClientId tid);
void *TmService(TM *tm);
void TmSetService(TM *tm, void *service);
void TmDocumentDestroyed(TM *tm, DOC *doc);
void DocDetachThread(DOC *doc);
void DocPushed(DOC *doc, BOOL first);
void TmFireContext(TM *tm, CTX *ctx, BOOL push);
HRESULT TmFireInitDocument(TM *tm, DOC *doc, BOOL init);
DOC *TmDocumentForWindow(TM *tm, HWND hwnd);

/* doc.c */
HRESULT DocCreate(TM *tm, DOC **out);
DOC *DocFromInterface(ITfDocumentMgr *iface);
void DocAddRef(DOC *doc);
void DocRelease(DOC *doc);
CTX *DocTopContext(DOC *doc);
ITfDocumentMgr *DocInterface(DOC *doc);

/* context.c */
HRESULT CtxCreate(TM *tm, TfClientId owner, DWORD flags, IUnknown *unknown, ITfContext **context, TfEditCookie *cookie);
CTX *CtxFromInterface(ITfContext *iface);
ITfContext *CtxEmpty(void);
void CtxAddRef(CTX *ctx);
void CtxRelease(CTX *ctx);
void CtxSetDoc(CTX *ctx, DOC *doc);
void CtxRunPostponed(CTX *ctx);
BOOL CtxStoreReadOnly(CTX *ctx);
ITfContext *CtxInterface(CTX *ctx);
HRESULT CtxRequestSession(CTX *ctx, TfClientId tid, ITfEditSession *session, DWORD flags, HRESULT *result);
BOOL CtxCheckCookie(CTX *ctx, TfEditCookie cookie, BOOL write);
/* text store calls, valid only inside an edit session. */
HRESULT CtxStoreGetSelection(CTX *ctx, LONG *start, LONG *end);
HRESULT CtxStoreGetEnd(CTX *ctx, LONG *end);
HRESULT CtxStoreTextExt(CTX *ctx, LONG start, LONG end, RECT *rect);
BOOL CtxStoreWindow(CTX *ctx, HWND *hwnd);
HRESULT CtxStoreSetText(CTX *ctx, DWORD flags, LONG start, LONG end, const WCHAR *text, LONG cch, TS_TEXTCHANGE *change);
HRESULT CtxStoreGetText(CTX *ctx, LONG start, LONG end, WCHAR *buffer, ULONG max, ULONG *got);
HRESULT CtxStoreSetSelection(CTX *ctx, LONG start, LONG end, TsActiveSelEnd ase);
/* moves ranges, properties and compositions after the program or a text service changed the text. */
void CtxTextChanged(CTX *ctx, LONG start, LONG old_end, LONG new_end, BOOL by_service);

/* range.c */
HRESULT RngCreate(CTX *ctx, LONG start, LONG end, ITfRange **range);
HRESULT RngCreateWeak(CTX *ctx, LONG start, LONG end, RNG **range);
RNG *RngFromInterface(ITfRange *iface);
void RngAdjust(CTX *ctx, LONG start, LONG old_end, LONG new_end);
void RngDetachAll(CTX *ctx);

/* property.c */
HRESULT PropsGetProperty(CTX *ctx, REFGUID guid, ITfProperty **property);
HRESULT PropsTrack(CTX *ctx, const GUID **guids, ULONG count, ITfReadOnlyProperty **property);
void PropsAdjust(CTX *ctx, LONG start, LONG old_end, LONG new_end);
void PropsFree(CTX *ctx);
void PropsSet(CTX *ctx, REFGUID guid, LONG start, LONG end, const VARIANT *value);
BOOL PropsGetInt(CTX *ctx, REFGUID guid, LONG position, LONG *value, LONG *start, LONG *end);
HRESULT EditRecordCreate(CTX *ctx, ITfEditRecord **record);
void EditRecordText(ITfEditRecord *record, LONG start, LONG end);
void EditRecordProperty(ITfEditRecord *record, REFGUID guid, LONG start, LONG end);
void EditRecordSelection(ITfEditRecord *record);
BOOL EditRecordChanged(ITfEditRecord *record);
HRESULT EnumRangesCreate(CTX *ctx, const LONG (*spans)[2], ULONG count, IEnumTfRanges **out);

/* compose.c */
HRESULT CompStart(CTX *ctx, TfEditCookie cookie, ITfRange *range, ITfCompositionSink *sink, const CLSID *owner,
                  ITfComposition **out);
HRESULT CompEnum(CTX *ctx, ITfRange *test, IEnumITfCompositionView **out);
HRESULT CompTerminateAll(CTX *ctx, ITfCompositionView *view);
void CompFreeAll(CTX *ctx);
void CompAbortAll(CTX *ctx);
void CompMarkAll(CTX *ctx);
HRESULT CompTakeOwnership(CTX *ctx, TfEditCookie cookie, ITfCompositionView *view, ITfCompositionSink *sink,
                          ITfComposition **out);
/* the first composition's extent, for the service. */
BOOL CompExtent(CTX *ctx, LONG *start, LONG *end);

/* compartment.c */
COMPARTMENTS *CompartmentsCreate(void);
void CompartmentsFree(COMPARTMENTS *comps);
HRESULT CompartmentsGet(COMPARTMENTS *comps, REFGUID guid, ITfCompartment **out);
HRESULT CompartmentsClear(COMPARTMENTS *comps, REFGUID guid);
HRESULT CompartmentsEnum(COMPARTMENTS *comps, IEnumGUID **out);
HRESULT GlobalCompartmentMgr(ITfCompartmentMgr **out);

/* profile.c and category.c */
HRESULT ProfilesCreate(REFIID riid, void **object);
HRESULT CategoryMgrCreate(REFIID riid, void **object);
HRESULT DisplayAttributeMgrCreate(REFIID riid, void **object);

/* enum.c: COM enumerators over interface pointers and over guids. */
HRESULT EnumCreate(REFIID iid, IUnknown **items, ULONG count, void **out);
HRESULT EnumGuidsCreate(const GUID *items, ULONG count, IEnumGUID **out);

/* service.c: the host's input method as a text service. */
void ServiceThreadAttach(TM *tm);
void ServiceThreadDetach(TM *tm);
void ServiceDocumentChanged(TM *tm);
void ServiceStoreNotify(CTX *ctx, BOOL layout);
extern const CLSID g_ctf_service_clsid;
extern HMODULE g_ctf_module;
/* requests the context's lock later from the thread's message loop, to run the waiting sessions. */
BOOL ServicePostLock(CTX *ctx);

#endif
