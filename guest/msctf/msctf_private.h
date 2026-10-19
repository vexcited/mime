/*
 * msctf.dll internals shared by its source files.
 */
#ifndef MIME_MSCTF_PRIVATE_H
#define MIME_MSCTF_PRIVATE_H

#define TFAPI DECLSPEC_EXPORT
#include <mime/win32/msctf.h>
#include <mime/win32/winuser.h>

/* ------------------------------------------------------------------ helpers */

void *CtfAlloc(SIZE_T size);
void CtfFree(void *p);
void CtfTrace(const char *format, ...);
extern BOOL g_ctf_trace;
#define CTF_TRACE(...) do { if (g_ctf_trace) CtfTrace(__VA_ARGS__); } while (0)

void CtfObjectCreated(void);
void CtfObjectDestroyed(void);

/* an interface of an object that embeds several: the object around it. */
#define CTF_OBJ(type, field, iface) CONTAINING_RECORD(iface, type, field)

/* ------------------------------------------------------------------ sinks */

typedef struct SINK {
    DWORD Cookie;
    DWORD Index;
    GUID Iid;
    IUnknown *Sink;
} SINK;

typedef struct SINKS {
    SINK *Items;
    ULONG Count, Capacity;
    DWORD Last[16];
} SINKS;

/*
 * keeps a reference to the sink as the interface it is advised for. the interfaces a source takes are listed, and
 * the cookie is the position of the interface in the list in its top byte and a number counting that interface's
 * sinks below.
 */
HRESULT SinksAdvise(SINKS *sinks, const IID *const *allowed, ULONG allowed_count, REFIID iid, IUnknown *sink,
                    DWORD *cookie);
HRESULT SinksUnadvise(SINKS *sinks, DWORD cookie);
void SinksFree(SINKS *sinks);
/* the referenced sinks of an interface, to call without the list changing under the call. */
ULONG SinksSnapshot(SINKS *sinks, REFIID iid, IUnknown ***out);
void SinksSnapshotFree(IUnknown **items, ULONG count);

/* ------------------------------------------------------------------ guid atoms, category.c */

TfGuidAtom CtfGuidAtom(REFGUID guid);
BOOL CtfAtomGuid(TfGuidAtom atom, GUID *guid);
/* the display attribute the host's input method uses for a kind of composed text: a TF_ATTR_*. */
BOOL CtfDisplayAttributeGuid(TF_DA_ATTR_INFO kind, GUID *guid);

/* ------------------------------------------------------------------ objects */

typedef struct TM TM;
typedef struct DOC DOC;
typedef struct CTX CTX;
typedef struct RNG RNG;
typedef struct COMP COMP;
typedef struct COMPARTMENTS COMPARTMENTS;

/* a range of a context, kept in the context's list so its positions follow the text. */
struct RNG {
    ITfRangeACP Iface;
    LONG Refs;
    CTX *Context;
    LONG Start, End;
    TfGravity GravityStart, GravityEnd;
    BOOL Weak;  /* a range a composition keeps, which does not keep the context alive */
    RNG *Next, *Prev;
};

/* a composition: its range, who is told when it ends and the view of it the program gets. */
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

/* the edit session being run, and the ones waiting for a lock. */
typedef struct PENDING {
    ITfEditSession *Session;
    DWORD Flags;
    TfClientId Client;
    HRESULT *Result;
    BOOL Done;
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
    /* the lock: what the store granted, the cookies of the session running under it and where the sinks are told. */
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
/* the text store calls, valid in an edit session only. */
HRESULT CtxStoreGetSelection(CTX *ctx, LONG *start, LONG *end);
HRESULT CtxStoreGetEnd(CTX *ctx, LONG *end);
HRESULT CtxStoreTextExt(CTX *ctx, LONG start, LONG end, RECT *rect);
BOOL CtxStoreWindow(CTX *ctx, HWND *hwnd);
HRESULT CtxStoreSetText(CTX *ctx, DWORD flags, LONG start, LONG end, const WCHAR *text, LONG cch, TS_TEXTCHANGE *change);
HRESULT CtxStoreGetText(CTX *ctx, LONG start, LONG end, WCHAR *buffer, ULONG max, ULONG *got);
HRESULT CtxStoreSetSelection(CTX *ctx, LONG start, LONG end, TsActiveSelEnd ase);
/* the text of the context changed by the program or by a text service: ranges, properties and compositions follow. */
void CtxTextChanged(CTX *ctx, LONG start, LONG old_end, LONG new_end, BOOL by_service);

/* range.c */
HRESULT RngCreate(CTX *ctx, LONG start, LONG end, ITfRange **range);
HRESULT RngCreateWeak(CTX *ctx, LONG start, LONG end, RNG **range);
RNG *RngFromInterface(ITfRange *iface);
void RngAdjust(CTX *ctx, LONG start, LONG old_end, LONG new_end);
void RngDetachAll(CTX *ctx);

/* property.c */
HRESULT PropsGetProperty(CTX *ctx, REFGUID guid, ITfProperty **property);
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

/* enum.c: a generic array of interface pointers as a COM enumerator, and one of guids. */
HRESULT EnumCreate(REFIID iid, IUnknown **items, ULONG count, void **out);
HRESULT EnumGuidsCreate(const GUID *items, ULONG count, IEnumGUID **out);

/* service.c: the host's input method as a text service. */
void ServiceThreadAttach(TM *tm);
void ServiceThreadDetach(TM *tm);
void ServiceDocumentChanged(TM *tm);
void ServiceStoreNotify(CTX *ctx, BOOL layout);
extern const CLSID g_ctf_service_clsid;
extern HMODULE g_ctf_module;
/* asks for the lock of a context later, from the thread's message loop, to run the sessions waiting for it. */
BOOL ServicePostLock(CTX *ctx);

#endif
