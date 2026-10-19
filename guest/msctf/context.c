/*
 * contexts: a program's document as the framework sees it. the program's text store is behind it; edit sessions
 * are how a text service reaches the text, each run while the store holds a lock for the context. this file keeps
 * the lock: it asks the store for one, runs the sessions it grants it for, and tells the sinks what they changed.
 */
#include "msctf_private.h"

static const IID IID_ITfTextLayoutSink_ = {0x2af2d06a, 0xdd5b, 0x4927, {0xa0, 0xb4, 0x54, 0xf1, 0x9c, 0x91, 0xfa, 0xde}};
static const IID IID_ITfStatusSink_ = {0x6b7d8d73, 0xb267, 0x4f69, {0xb3, 0x2e, 0x1c, 0xa3, 0x21, 0xce, 0x4f, 0x45}};
static const IID IID_ITfEditTransactionSink_ = {0x708fbf70, 0xb520, 0x416b, {0xb0, 0x6c, 0x2c, 0x41, 0xab, 0x44, 0xf8, 0xba}};

/* the cookie a context owner gets from CreateContext for its own use. */
#define OWNER_COOKIE 1

extern const ITfContextVtbl g_context_vtbl;

CTX *CtxFromInterface(ITfContext *iface)
{
    return iface && iface->lpVtbl == &g_context_vtbl ? (CTX *)iface : nullptr;
}

ITfContext *CtxInterface(CTX *ctx)
{
    return &ctx->Context;
}

void CtxAddRef(CTX *ctx)
{
    InterlockedIncrement(&ctx->Refs);
}

void CtxSetDoc(CTX *ctx, DOC *doc)
{
    if (!doc && ctx->Doc)
        CompAbortAll(ctx);
    ctx->Doc = doc;
    if (!ctx->Store)
        return;
    /* the store is told of the framework while the context is in a document. */
    if (doc && !ctx->SinkAdvised) {
        HRESULT hr = ctx->Store->lpVtbl->AdviseSink(ctx->Store, &IID_ITextStoreACPSink, (IUnknown *)&ctx->StoreSink,
                                                    TS_AS_ALL_SINKS);
        CTF_TRACE("msctf: advise text store sink %08lx\n", hr);
        ctx->SinkAdvised = SUCCEEDED(hr);
    } else if (!doc && ctx->SinkAdvised) {
        ctx->Store->lpVtbl->UnadviseSink(ctx->Store, (IUnknown *)&ctx->StoreSink);
        ctx->SinkAdvised = FALSE;
    }
}

static void ctx_free(CTX *ctx)
{
    ctx->Refs = 1000;
    ctx->Destroying = TRUE;
    if (ctx->SinkAdvised && ctx->Store)
        ctx->Store->lpVtbl->UnadviseSink(ctx->Store, (IUnknown *)&ctx->StoreSink);
    CompFreeAll(ctx);
    RngDetachAll(ctx);
    PropsFree(ctx);
    for (PENDING *p = ctx->Pending; p;) {
        PENDING *next = p->Next;
        p->Session->lpVtbl->Release(p->Session);
        CtfFree(p);
        p = next;
    }
    SinksFree(&ctx->Sinks);
    CompartmentsFree(ctx->Compartments);
    if (ctx->OwnerSink)
        ctx->OwnerSink->lpVtbl->Release(ctx->OwnerSink);
    if (ctx->Store)
        ctx->Store->lpVtbl->Release(ctx->Store);
    if (ctx->Unknown)
        ctx->Unknown->lpVtbl->Release(ctx->Unknown);
    CtfFree(ctx);
    CtfObjectDestroyed();
}

void CtxRelease(CTX *ctx)
{
    if (!InterlockedDecrement(&ctx->Refs))
        ctx_free(ctx);
}

/* ------------------------------------------------------------------ the text store, called in a session */

HRESULT CtxStoreGetSelection(CTX *ctx, LONG *start, LONG *end)
{
    TS_SELECTION_ACP selection;
    ULONG fetched = 0;
    if (!ctx->Store)
        return E_FAIL;
    HRESULT hr = ctx->Store->lpVtbl->GetSelection(ctx->Store, TS_DEFAULT_SELECTION, 1, &selection, &fetched);
    if (FAILED(hr))
        return hr;
    if (!fetched)
        return TS_E_NOSELECTION;
    *start = selection.acpStart;
    *end = selection.acpEnd;
    return S_OK;
}

HRESULT CtxStoreGetEnd(CTX *ctx, LONG *end)
{
    return ctx->Store ? ctx->Store->lpVtbl->GetEndACP(ctx->Store, end) : E_FAIL;
}

HRESULT CtxStoreTextExt(CTX *ctx, LONG start, LONG end, RECT *rect)
{
    TsViewCookie view;
    BOOL clipped = FALSE;
    if (!ctx->Store)
        return E_FAIL;
    HRESULT hr = ctx->Store->lpVtbl->GetActiveView(ctx->Store, &view);
    if (FAILED(hr))
        return hr;
    return ctx->Store->lpVtbl->GetTextExt(ctx->Store, view, start, end, rect, &clipped);
}

BOOL CtxStoreWindow(CTX *ctx, HWND *hwnd)
{
    TsViewCookie view;
    if (!ctx->Store || FAILED(ctx->Store->lpVtbl->GetActiveView(ctx->Store, &view)))
        return FALSE;
    *hwnd = nullptr;
    return SUCCEEDED(ctx->Store->lpVtbl->GetWnd(ctx->Store, view, hwnd)) && *hwnd;
}

HRESULT CtxStoreSetText(CTX *ctx, DWORD flags, LONG start, LONG end, const WCHAR *text, LONG cch, TS_TEXTCHANGE *change)
{
    if (!ctx->Store)
        return E_FAIL;
    return ctx->Store->lpVtbl->SetText(ctx->Store, flags, start, end, text, (ULONG)cch, change);
}

HRESULT CtxStoreGetText(CTX *ctx, LONG start, LONG end, WCHAR *buffer, ULONG max, ULONG *got)
{
    TS_RUNINFO runs[2];
    ULONG run_count = 0;
    LONG next = 0;
    if (!ctx->Store)
        return E_FAIL;
    return ctx->Store->lpVtbl->GetText(ctx->Store, start, end, buffer, max, got, runs, 2, &run_count, &next);
}

HRESULT CtxStoreSetSelection(CTX *ctx, LONG start, LONG end, TsActiveSelEnd ase)
{
    TS_SELECTION_ACP selection = {start, end, {ase, FALSE}};
    if (!ctx->Store)
        return E_FAIL;
    return ctx->Store->lpVtbl->SetSelection(ctx->Store, 1, &selection);
}

void CtxTextChanged(CTX *ctx, LONG start, LONG old_end, LONG new_end, BOOL by_service)
{
    RngAdjust(ctx, start, old_end, new_end);
    PropsAdjust(ctx, start, old_end, new_end);
    if (by_service && ctx->Record)
        EditRecordText(ctx->Record, start, new_end);
    if (by_service)
        CompMarkAll(ctx);
}

/* ------------------------------------------------------------------ cookies and locks */

BOOL CtxCheckCookie(CTX *ctx, TfEditCookie cookie, BOOL write)
{
    if (!ctx->Doc || !cookie)
        return FALSE;
    /* the owner's cookie only reads, and only while a lock is held. */
    if (cookie == OWNER_COOKIE)
        return ctx->Lock && !write;
    if (!ctx->Lock)
        return FALSE;
    if (write)
        return cookie == ctx->CookieWrite && ctx->Lock == TS_LF_READWRITE;
    return cookie == ctx->CookieRead || (cookie == ctx->CookieWrite && ctx->Lock == TS_LF_READWRITE);
}

/* a cookie is made for each session and never repeats soon, so a stale one is caught. */
static TfEditCookie new_cookie(CTX *ctx, BOOL write)
{
    return ((++ctx->CookieSerial & 0x3FFFFFFF) << 2 | (write ? 2 : 1)) | 0x40000000;
}

static BOOL pending_writes(PENDING *p)
{
    return (p->Flags & TF_ES_READWRITE) == TF_ES_READWRITE;
}

BOOL CtxStoreReadOnly(CTX *ctx)
{
    TS_STATUS status = {};
    return ctx->Store && SUCCEEDED(ctx->Store->lpVtbl->GetStatus(ctx->Store, &status)) &&
           (status.dwDynamicFlags & TS_SD_READONLY);
}

static void notify_end_edit(CTX *ctx, TfEditCookie cookie)
{
    ITfEditRecord *record = ctx->Record;
    if (!record)
        return;
    if (EditRecordChanged(record)) {
        IUnknown **sinks;
        ULONG n = SinksSnapshot(&ctx->Sinks, &IID_ITfTextEditSink, &sinks);
        ctx->InNotify = TRUE;
        for (ULONG i = 0; i < n; i++) {
            ITfTextEditSink *sink = (ITfTextEditSink *)sinks[i];
            sink->lpVtbl->OnEndEdit(sink, &ctx->Context, cookie, record);
        }
        ctx->InNotify = FALSE;
        SinksSnapshotFree(sinks, n);
    }
    ctx->Record = nullptr;
    record->lpVtbl->Release(record);
}

/* runs one session under the lock held, with cookies of its own. */
static HRESULT run_session(CTX *ctx, ITfEditSession *session, TfClientId client, BOOL write)
{
    TfEditCookie read = ctx->CookieRead, written = ctx->CookieWrite;
    BOOL was_session = ctx->InSession, was_write = ctx->SessionWrite;
    TfClientId was_client = ctx->SessionClient;
    ITfEditRecord *was_record = ctx->Record;
    TfEditCookie was_cookie = ctx->SessionCookie;
    ctx->CookieRead = new_cookie(ctx, FALSE);
    ctx->CookieWrite = write ? new_cookie(ctx, TRUE) : 0;
    ctx->InSession = TRUE;
    ctx->SessionWrite = write;
    ctx->SessionClient = client;
    ctx->Record = nullptr;
    if (write)
        EditRecordCreate(ctx, &ctx->Record);
    ctx->SessionCookie = write ? ctx->CookieWrite : ctx->CookieRead;
    HRESULT hr = session->lpVtbl->DoEditSession(session, ctx->SessionCookie);
    if (write)
        notify_end_edit(ctx, ctx->CookieRead);
    ctx->InSession = was_session;
    ctx->SessionWrite = was_write;
    ctx->SessionClient = was_client;
    ctx->Record = was_record;
    ctx->SessionCookie = was_cookie;
    ctx->CookieRead = read;
    ctx->CookieWrite = written;
    return hr;
}

static void run_entry(CTX *ctx, PENDING *p)
{
    HRESULT hr = run_session(ctx, p->Session, p->Client, pending_writes(p));
    if (p->Result)
        *p->Result = hr;
    p->Done = TRUE;
    p->Session->lpVtbl->Release(p->Session);
    CtfFree(p);
}

static void unlink_pending(CTX *ctx, PENDING *p)
{
    for (PENDING **link = &ctx->Pending; *link; link = &(*link)->Next) {
        if (*link == p) {
            *link = p->Next;
            return;
        }
    }
}

/* the store grants its lock: the sessions waiting for it run, the oldest first. */
static HRESULT grant(CTX *ctx, DWORD flags)
{
    HRESULT result = S_OK;
    if ((flags & ~7u) || !(flags & TS_LF_READ))
        return E_INVALIDARG;
    DWORD lock = (flags & TS_LF_READWRITE) == TS_LF_READWRITE ? TS_LF_READWRITE : TS_LF_READ;
    if (ctx->Lock || !ctx->Doc)
        return E_UNEXPECTED;
    CtxAddRef(ctx);
    ctx->Lock = lock;
    CTF_TRACE("msctf: lock granted %lx\n", lock);
    PENDING *last = ctx->Pending;
    while (last && last->Next)
        last = last->Next;
    if (last && (last->Flags & TF_ES_SYNC)) {
        /* a synchronous request waits at the end and is the one this grant is for. */
        unlink_pending(ctx, last);
        if (pending_writes(last) && lock != TS_LF_READWRITE) {
            if (last->Result)
                *last->Result = E_UNEXPECTED;
            result = E_UNEXPECTED;
            last->Session->lpVtbl->Release(last->Session);
            CtfFree(last);
        } else {
            HRESULT hr = run_session(ctx, last->Session, last->Client, pending_writes(last));
            if (last->Result)
                *last->Result = hr;
            result = hr;
            last->Session->lpVtbl->Release(last->Session);
            CtfFree(last);
        }
    } else {
        while (ctx->Pending) {
            PENDING *p = ctx->Pending;
            if (pending_writes(p) && lock != TS_LF_READWRITE) {
                /* the write waits for a lock of its own. */
                DWORD more = TS_LF_READWRITE;
                HRESULT session_hr = E_FAIL;
                ctx->Lock = 0;
                ctx->Store->lpVtbl->RequestLock(ctx->Store, more, &session_hr);
                ctx->Lock = lock;
                break;
            }
            ctx->Pending = p->Next;
            run_entry(ctx, p);
        }
    }
    ctx->Lock = 0;
    if (ctx->Pending)
        ServicePostLock(ctx);
    CtxRelease(ctx);
    return result;
}

/* the lock asked for earlier, from the message loop, for the sessions that cannot run in the call that queued them. */
void CtxRunPostponed(CTX *ctx)
{
    ctx->LockPosted = FALSE;
    if (!ctx->Pending || ctx->Lock || !ctx->Store || !ctx->Doc)
        return;
    BOOL write = FALSE;
    for (PENDING *p = ctx->Pending; p; p = p->Next)
        write = write || pending_writes(p);
    HRESULT session_hr = E_FAIL;
    CtxAddRef(ctx);
    HRESULT hr = ctx->Store->lpVtbl->RequestLock(ctx->Store, write ? TS_LF_READWRITE : TS_LF_READ, &session_hr);
    CTF_TRACE("msctf: postponed lock -> %lx session %lx\n", hr, session_hr);
    CtxRelease(ctx);
}

static void queue_pending(CTX *ctx, PENDING *p)
{
    PENDING **link = &ctx->Pending;
    while (*link)
        link = &(*link)->Next;
    *link = p;
}

HRESULT CtxRequestSession(CTX *ctx, TfClientId tid, ITfEditSession *session, DWORD flags, HRESULT *result)
{
    if (!result)
        return E_INVALIDARG;
    *result = E_FAIL;
    if (!ctx->Doc)
        return TF_E_DISCONNECTED;
    if (!session || (flags & 0xFFFFFFE2u) != 2 || (flags & (TF_ES_SYNC | TF_ES_ASYNC)) == (TF_ES_SYNC | TF_ES_ASYNC))
        return E_INVALIDARG;
    BOOL write = (flags & TF_ES_READWRITE) == TF_ES_READWRITE;
    BOOL sync = (flags & TF_ES_SYNC) != 0;
    if (!ctx->Store)
        return S_OK;

    /* inside the sinks of the end of an edit, a write cannot be waited for. */
    if (ctx->InNotify && write && sync) {
        *result = TS_E_SYNCHRONOUS;
        return S_OK;
    }
    if (ctx->Lock && (ctx->InSession || ctx->InNotify)) {
        BOOL covered = ctx->Lock == TS_LF_READWRITE || !write;
        if (sync && !ctx->InNotify) {
            if (!covered || (write && !ctx->SessionWrite)) {
                *result = TF_E_LOCKED;
                return TF_E_LOCKED;
            }
            /* the same lock serves it at once. */
            *result = session->lpVtbl->DoEditSession(session, ctx->SessionCookie);
            return S_OK;
        }
    }
    if (write && CtxStoreReadOnly(ctx)) {
        *result = TS_E_READONLY;
        return S_OK;
    }
    PENDING *p = CtfAlloc(sizeof(*p));
    if (!p)
        return E_OUTOFMEMORY;
    p->Session = session;
    session->lpVtbl->AddRef(session);
    p->Flags = flags;
    p->Client = tid;
    p->Result = result;

    /* an asynchronous request, or one behind others, waits for a lock asked for from the message loop. */
    if ((flags & TF_ES_ASYNC) || ctx->Pending || ctx->Lock) {
        p->Result = nullptr;
        queue_pending(ctx, p);
        if (!ctx->Lock && !ctx->LockPosted)
            ctx->LockPosted = ServicePostLock(ctx);
        if (!ctx->LockPosted && !ctx->Lock)
            CtxRunPostponed(ctx);
        *result = TS_S_ASYNC;
        return S_OK;
    }

    queue_pending(ctx, p);
    DWORD lock_flags = (write ? TS_LF_READWRITE : TS_LF_READ) | (sync ? TS_LF_SYNC : 0);
    HRESULT session_hr = E_FAIL;
    CtxAddRef(ctx);
    HRESULT hr = ctx->Store->lpVtbl->RequestLock(ctx->Store, lock_flags, &session_hr);
    CTF_TRACE("msctf: request lock %lx -> %lx session %lx\n", lock_flags, hr, session_hr);
    BOOL queued = FALSE;
    for (PENDING *q = ctx->Pending; q; q = q->Next)
        queued = queued || q == p;
    if (queued) {
        if (FAILED(hr) || FAILED(session_hr)) {
            unlink_pending(ctx, p);
            session->lpVtbl->Release(session);
            CtfFree(p);
            *result = session_hr;
        } else {
            /* granted later: the caller's result is not written then. */
            p->Result = nullptr;
            *result = TS_S_ASYNC;
        }
    }
    CtxRelease(ctx);
    return hr;
}

/* ------------------------------------------------------------------ ITextStoreACPSink */

static CTX *from_sink(ITextStoreACPSink *This)
{
    return CONTAINING_RECORD(This, CTX, StoreSink);
}

static HRESULT STDMETHODCALLTYPE ss_query(ITextStoreACPSink *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITextStoreACPSink)) {
        *object = This;
        InterlockedIncrement(&from_sink(This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ss_add_ref(ITextStoreACPSink *This)
{
    return InterlockedIncrement(&from_sink(This)->Refs);
}

static ULONG STDMETHODCALLTYPE ss_release(ITextStoreACPSink *This)
{
    CTX *ctx = from_sink(This);
    LONG refs = InterlockedDecrement(&ctx->Refs);
    if (!refs)
        ctx_free(ctx);
    return refs;
}

static HRESULT STDMETHODCALLTYPE ss_on_text_change(ITextStoreACPSink *This, DWORD flags, const TS_TEXTCHANGE *change)
{
    CTX *ctx = from_sink(This);
    if (!change)
        return E_INVALIDARG;
    /* the store tells of the changes it makes itself, not of the ones a session asked for. */
    if (ctx->InSession)
        return TS_E_NOLOCK;
    if (change->acpStart == change->acpOldEnd && change->acpStart == change->acpNewEnd)
        return S_OK;
    CTF_TRACE("msctf: text changed %ld %ld %ld\n", change->acpStart, change->acpOldEnd, change->acpNewEnd);
    CtxTextChanged(ctx, change->acpStart, change->acpOldEnd, change->acpNewEnd, FALSE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ss_on_selection_change(ITextStoreACPSink *This)
{
    CTX *ctx = from_sink(This);
    if (ctx->InSession)
        return TS_E_NOLOCK;
    ServiceStoreNotify(ctx, FALSE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ss_on_layout_change(ITextStoreACPSink *This, TsLayoutCode code, TsViewCookie view)
{
    ServiceStoreNotify(from_sink(This), TRUE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ss_on_status_change(ITextStoreACPSink *This, DWORD flags)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ss_on_attrs_change(ITextStoreACPSink *This, LONG start, LONG end, ULONG count,
                                                    const TS_ATTRID *attrs)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ss_on_lock_granted(ITextStoreACPSink *This, DWORD flags)
{
    return grant(from_sink(This), flags);
}

static HRESULT STDMETHODCALLTYPE ss_on_start_transaction(ITextStoreACPSink *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ss_on_end_transaction(ITextStoreACPSink *This)
{
    return S_OK;
}

static const ITextStoreACPSinkVtbl store_sink_vtbl = {
    ss_query, ss_add_ref, ss_release, ss_on_text_change, ss_on_selection_change, ss_on_layout_change,
    ss_on_status_change, ss_on_attrs_change, ss_on_lock_granted, ss_on_start_transaction, ss_on_end_transaction};

/* ------------------------------------------------------------------ ITfContext */

#define CTX_THUNKS(Iface, field)                                                                                       \
    static HRESULT STDMETHODCALLTYPE field##_query(Iface *This, REFIID riid, void **object)                            \
    {                                                                                                                  \
        return ctx_query(CTF_OBJ(CTX, field, This), riid, object);                                                     \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE field##_add_ref(Iface *This)                                                        \
    {                                                                                                                  \
        return InterlockedIncrement(&CTF_OBJ(CTX, field, This)->Refs);                                                 \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE field##_release(Iface *This)                                                        \
    {                                                                                                                  \
        CTX *ctx = CTF_OBJ(CTX, field, This);                                                                          \
        LONG refs = InterlockedDecrement(&ctx->Refs);                                                                  \
        if (!refs)                                                                                                     \
            ctx_free(ctx);                                                                                             \
        return refs;                                                                                                   \
    }

static HRESULT ctx_query(CTX *ctx, REFIID riid, void **object)
{
    void *found = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfContext))
        found = &ctx->Context;
    else if (IsEqualIID(riid, &IID_ITfInsertAtSelection))
        found = &ctx->Insert;
    else if (IsEqualIID(riid, &IID_ITfContextComposition))
        found = &ctx->Composition;
    else if (IsEqualIID(riid, &IID_ITfContextOwnerCompositionServices))
        found = &ctx->Services;
    else if (IsEqualIID(riid, &IID_ITfSource))
        found = &ctx->Source;
    else if (IsEqualIID(riid, &IID_ITfCompartmentMgr))
        found = &ctx->CompartmentMgr;
    else if (IsEqualIID(riid, &IID_ITfContextOwnerServices))
        found = &ctx->OwnerServices;
    if (!found) {
        CTF_TRACE("msctf: context query %08lx-%04x refused\n", riid->Data1, riid->Data2);
        *object = nullptr;
        return E_NOINTERFACE;
    }
    *object = found;
    InterlockedIncrement(&ctx->Refs);
    return S_OK;
}

CTX_THUNKS(ITfContext, Context)
CTX_THUNKS(ITfInsertAtSelection, Insert)
CTX_THUNKS(ITfContextComposition, Composition)
CTX_THUNKS(ITfContextOwnerCompositionServices, Services)
CTX_THUNKS(ITfSource, Source)
CTX_THUNKS(ITfCompartmentMgr, CompartmentMgr)
CTX_THUNKS(ITfContextOwnerServices, OwnerServices)

static HRESULT STDMETHODCALLTYPE c_request(ITfContext *This, TfClientId tid, ITfEditSession *session, DWORD flags,
                                           HRESULT *result)
{
    return CtxRequestSession(CTF_OBJ(CTX, Context, This), tid, session, flags, result);
}

static HRESULT STDMETHODCALLTYPE c_in_write(ITfContext *This, TfClientId tid, BOOL *in_write)
{
    CTX *ctx = CTF_OBJ(CTX, Context, This);
    if (!in_write)
        return E_INVALIDARG;
    *in_write = ctx->Lock == TS_LF_READWRITE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE c_get_selection(ITfContext *This, TfEditCookie cookie, ULONG index, ULONG count,
                                                 TF_SELECTION *selection, ULONG *fetched)
{
    CTX *ctx = CTF_OBJ(CTX, Context, This);
    if (!selection || !fetched)
        return E_INVALIDARG;
    *fetched = 0;
    if (!CtxCheckCookie(ctx, cookie, FALSE))
        return TF_E_NOLOCK;
    if (!count)
        return S_OK;
    TS_SELECTION_ACP *acp = CtfAlloc(count * sizeof(*acp));
    if (!acp)
        return E_OUTOFMEMORY;
    ULONG got = 0;
    HRESULT hr = ctx->Store->lpVtbl->GetSelection(ctx->Store, index, count, acp, &got);
    if (hr == TS_E_NOSELECTION)
        hr = TF_E_NOSELECTION;
    for (ULONG i = 0; SUCCEEDED(hr) && i < got; i++) {
        hr = RngCreate(ctx, acp[i].acpStart, acp[i].acpEnd, &selection[i].range);
        selection[i].style.ase = (TfActiveSelEnd)acp[i].style.ase;
        selection[i].style.fInterimChar = acp[i].style.fInterimChar;
        if (SUCCEEDED(hr))
            *fetched = i + 1;
    }
    CtfFree(acp);
    return hr;
}

static HRESULT STDMETHODCALLTYPE c_set_selection(ITfContext *This, TfEditCookie cookie, ULONG count,
                                                 const TF_SELECTION *selection)
{
    CTX *ctx = CTF_OBJ(CTX, Context, This);
    if (!count || !selection)
        return E_INVALIDARG;
    if (!CtxCheckCookie(ctx, cookie, TRUE))
        return TF_E_NOLOCK;
    TS_SELECTION_ACP *acp = CtfAlloc(count * sizeof(*acp));
    if (!acp)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    for (ULONG i = 0; i < count && SUCCEEDED(hr); i++) {
        RNG *rng = RngFromInterface(selection[i].range);
        if (!rng || rng->Context != ctx) {
            hr = E_INVALIDARG;
            break;
        }
        acp[i].acpStart = rng->Start;
        acp[i].acpEnd = rng->End;
        acp[i].style.ase = (TsActiveSelEnd)selection[i].style.ase;
        acp[i].style.fInterimChar = selection[i].style.fInterimChar;
    }
    if (SUCCEEDED(hr))
        hr = ctx->Store->lpVtbl->SetSelection(ctx->Store, count, acp);
    CtfFree(acp);
    if (SUCCEEDED(hr) && ctx->Record)
        EditRecordSelection(ctx->Record);
    return hr;
}

static HRESULT STDMETHODCALLTYPE c_get_start(ITfContext *This, TfEditCookie cookie, ITfRange **range)
{
    CTX *ctx = CTF_OBJ(CTX, Context, This);
    if (!range)
        return E_INVALIDARG;
    *range = nullptr;
    if (!CtxCheckCookie(ctx, cookie, FALSE))
        return TF_E_NOLOCK;
    return RngCreate(ctx, 0, 0, range);
}

static HRESULT STDMETHODCALLTYPE c_get_end(ITfContext *This, TfEditCookie cookie, ITfRange **range)
{
    CTX *ctx = CTF_OBJ(CTX, Context, This);
    LONG end;
    if (!range)
        return E_INVALIDARG;
    *range = nullptr;
    if (!CtxCheckCookie(ctx, cookie, FALSE))
        return TF_E_NOLOCK;
    HRESULT hr = CtxStoreGetEnd(ctx, &end);
    return FAILED(hr) ? hr : RngCreate(ctx, end, end, range);
}

static HRESULT STDMETHODCALLTYPE c_get_active_view(ITfContext *This, ITfContextView **view)
{
    if (!view)
        return E_INVALIDARG;
    *view = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE c_enum_views(ITfContext *This, IEnumTfContextViews **out)
{
    if (!out)
        return E_INVALIDARG;
    return EnumCreate(&IID_IEnumTfContextViews, nullptr, 0, (void **)out);
}

static HRESULT STDMETHODCALLTYPE c_get_status(ITfContext *This, TF_STATUS *status)
{
    CTX *ctx = CTF_OBJ(CTX, Context, This);
    TS_STATUS ts = {};
    if (!status)
        return E_INVALIDARG;
    if (!ctx->Store)
        return E_FAIL;
    HRESULT hr = ctx->Store->lpVtbl->GetStatus(ctx->Store, &ts);
    if (SUCCEEDED(hr)) {
        status->dwDynamicFlags = ts.dwDynamicFlags;
        status->dwStaticFlags = ts.dwStaticFlags;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE c_get_property(ITfContext *This, REFGUID guid, ITfProperty **property)
{
    return PropsGetProperty(CTF_OBJ(CTX, Context, This), guid, property);
}

static HRESULT STDMETHODCALLTYPE c_get_app_property(ITfContext *This, REFGUID guid, ITfReadOnlyProperty **property)
{
    if (!property)
        return E_INVALIDARG;
    *property = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE c_track(ITfContext *This, const GUID **props, ULONG count, const GUID **app_props,
                                         ULONG app_count, ITfReadOnlyProperty **property)
{
    if (!property)
        return E_INVALIDARG;
    *property = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE c_enum_properties(ITfContext *This, IEnumTfProperties **out)
{
    if (!out)
        return E_INVALIDARG;
    return EnumCreate(&IID_IEnumTfProperties, nullptr, 0, (void **)out);
}

static HRESULT STDMETHODCALLTYPE c_get_document(ITfContext *This, ITfDocumentMgr **doc)
{
    CTX *ctx = CTF_OBJ(CTX, Context, This);
    if (!doc)
        return E_INVALIDARG;
    *doc = nullptr;
    if (!ctx->Doc)
        return S_FALSE;
    *doc = DocInterface(ctx->Doc);
    (*doc)->lpVtbl->AddRef(*doc);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE c_create_backup(ITfContext *This, TfEditCookie cookie, ITfRange *range,
                                                 ITfRangeBackup **backup)
{
    if (!backup)
        return E_INVALIDARG;
    *backup = nullptr;
    return E_NOTIMPL;
}

const ITfContextVtbl g_context_vtbl = {Context_query, Context_add_ref, Context_release, c_request, c_in_write,
                                       c_get_selection, c_set_selection, c_get_start, c_get_end, c_get_active_view,
                                       c_enum_views, c_get_status, c_get_property, c_get_app_property, c_track,
                                       c_enum_properties, c_get_document, c_create_backup};

/* ------------------------------------------------------------------ the empty context */

/*
 * a document that has no context gives this one when asked for its top or its base: every request of it is
 * answered that the context is empty.
 */
static HRESULT STDMETHODCALLTYPE em_query(ITfContext *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfContext)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE em_add_ref(ITfContext *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE em_release(ITfContext *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE em_request(ITfContext *This, TfClientId tid, ITfEditSession *session, DWORD flags,
                                            HRESULT *result)
{
    if (result)
        *result = TF_E_EMPTYCONTEXT;
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_in_write(ITfContext *This, TfClientId tid, BOOL *in_write)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_get_selection(ITfContext *This, TfEditCookie cookie, ULONG index, ULONG count,
                                                  TF_SELECTION *selection, ULONG *fetched)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_set_selection(ITfContext *This, TfEditCookie cookie, ULONG count,
                                                  const TF_SELECTION *selection)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_get_range(ITfContext *This, TfEditCookie cookie, ITfRange **range)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_get_view(ITfContext *This, ITfContextView **view)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_enum_views(ITfContext *This, IEnumTfContextViews **views)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_get_status(ITfContext *This, TF_STATUS *status)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_get_property(ITfContext *This, REFGUID guid, ITfProperty **property)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_get_app_property(ITfContext *This, REFGUID guid, ITfReadOnlyProperty **property)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_track(ITfContext *This, const GUID **props, ULONG count, const GUID **app_props,
                                          ULONG app_count, ITfReadOnlyProperty **property)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_enum_properties(ITfContext *This, IEnumTfProperties **out)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_get_document(ITfContext *This, ITfDocumentMgr **doc)
{
    return TF_E_EMPTYCONTEXT;
}

static HRESULT STDMETHODCALLTYPE em_backup(ITfContext *This, TfEditCookie cookie, ITfRange *range,
                                           ITfRangeBackup **backup)
{
    return TF_E_EMPTYCONTEXT;
}

static const ITfContextVtbl empty_vtbl = {em_query, em_add_ref, em_release, em_request, em_in_write, em_get_selection,
                                          em_set_selection, em_get_range, em_get_range, em_get_view, em_enum_views,
                                          em_get_status, em_get_property, em_get_app_property, em_track,
                                          em_enum_properties, em_get_document, em_backup};
static ITfContext g_empty_context = {&empty_vtbl};

ITfContext *CtxEmpty(void)
{
    return &g_empty_context;
}

/* ------------------------------------------------------------------ ITfInsertAtSelection */

static HRESULT STDMETHODCALLTYPE ins_text(ITfInsertAtSelection *This, TfEditCookie cookie, DWORD flags,
                                          const WCHAR *text, LONG cch, ITfRange **range)
{
    CTX *ctx = CTF_OBJ(CTX, Insert, This);
    if (!range)
        return E_INVALIDARG;
    *range = nullptr;
    BOOL query = (flags & TF_IAS_QUERYONLY) != 0;
    if (!CtxCheckCookie(ctx, cookie, !query))
        return TF_E_NOLOCK;
    if (!query && !text && cch)
        return E_INVALIDARG;
    if (cch < 0 && text) {
        cch = 0;
        while (text[cch])
            cch++;
    }
    LONG start = 0, end = 0;
    TS_TEXTCHANGE change = {};
    DWORD ts_flags = (query ? TS_IAS_QUERYONLY : 0) | (flags & TF_IAS_NOQUERY ? TS_IAS_NOQUERY : 0);
    HRESULT hr = ctx->Store->lpVtbl->InsertTextAtSelection(ctx->Store, ts_flags, text, (ULONG)cch, &start, &end, &change);
    if (FAILED(hr))
        return hr;
    if (!query) {
        CtxTextChanged(ctx, change.acpStart, change.acpOldEnd, change.acpNewEnd, TRUE);
        if (flags & TF_IAS_NOQUERY) {
            start = change.acpStart;
            end = change.acpNewEnd;
        }
    }
    return RngCreate(ctx, start, end, range);
}

static HRESULT STDMETHODCALLTYPE ins_embedded(ITfInsertAtSelection *This, TfEditCookie cookie, DWORD flags,
                                              IDataObject *object, ITfRange **range)
{
    if (!range)
        return E_INVALIDARG;
    *range = nullptr;
    return TS_E_FORMAT;
}

static const ITfInsertAtSelectionVtbl insert_vtbl = {Insert_query, Insert_add_ref, Insert_release, ins_text,
                                                     ins_embedded};

/* ------------------------------------------------------------------ compositions */

static HRESULT STDMETHODCALLTYPE cc_start(ITfContextComposition *This, TfEditCookie cookie, ITfRange *range,
                                          ITfCompositionSink *sink, ITfComposition **out)
{
    CTX *ctx = CTF_OBJ(CTX, Composition, This);
    CLSID owner = {};
    return CompStart(ctx, cookie, range, sink, &owner, out);
}

static HRESULT STDMETHODCALLTYPE cc_enum(ITfContextComposition *This, IEnumITfCompositionView **out)
{
    return CompEnum(CTF_OBJ(CTX, Composition, This), nullptr, out);
}

static HRESULT STDMETHODCALLTYPE cc_find(ITfContextComposition *This, TfEditCookie cookie, ITfRange *test,
                                         IEnumITfCompositionView **out)
{
    CTX *ctx = CTF_OBJ(CTX, Composition, This);
    if (!CtxCheckCookie(ctx, cookie, FALSE))
        return TF_E_NOLOCK;
    return CompEnum(ctx, test, out);
}

static HRESULT STDMETHODCALLTYPE cc_take(ITfContextComposition *This, TfEditCookie cookie, ITfCompositionView *view,
                                         ITfCompositionSink *sink, ITfComposition **out)
{
    return CompTakeOwnership(CTF_OBJ(CTX, Composition, This), cookie, view, sink, out);
}

static const ITfContextCompositionVtbl composition_vtbl = {Composition_query, Composition_add_ref,
                                                           Composition_release, cc_start, cc_enum, cc_find, cc_take};

static HRESULT STDMETHODCALLTYPE sv_start(ITfContextOwnerCompositionServices *This, TfEditCookie cookie,
                                          ITfRange *range, ITfCompositionSink *sink, ITfComposition **out)
{
    CTX *ctx = CTF_OBJ(CTX, Services, This);
    CLSID owner = {};
    return CompStart(ctx, cookie, range, sink, &owner, out);
}

static HRESULT STDMETHODCALLTYPE sv_enum(ITfContextOwnerCompositionServices *This, IEnumITfCompositionView **out)
{
    return CompEnum(CTF_OBJ(CTX, Services, This), nullptr, out);
}

static HRESULT STDMETHODCALLTYPE sv_find(ITfContextOwnerCompositionServices *This, TfEditCookie cookie,
                                         ITfRange *test, IEnumITfCompositionView **out)
{
    CTX *ctx = CTF_OBJ(CTX, Services, This);
    if (!CtxCheckCookie(ctx, cookie, FALSE))
        return TF_E_NOLOCK;
    return CompEnum(ctx, test, out);
}

static HRESULT STDMETHODCALLTYPE sv_take(ITfContextOwnerCompositionServices *This, TfEditCookie cookie,
                                         ITfCompositionView *view, ITfCompositionSink *sink, ITfComposition **out)
{
    return CompTakeOwnership(CTF_OBJ(CTX, Services, This), cookie, view, sink, out);
}

static HRESULT STDMETHODCALLTYPE sv_terminate(ITfContextOwnerCompositionServices *This, ITfCompositionView *view)
{
    return CompTerminateAll(CTF_OBJ(CTX, Services, This), view);
}

static const ITfContextOwnerCompositionServicesVtbl services_vtbl = {
    Services_query, Services_add_ref, Services_release, sv_start, sv_enum, sv_find, sv_take, sv_terminate};

/* ------------------------------------------------------------------ ITfSource */

/* the sinks a context takes, in the order the cookies number them. */
static const IID g_iid_context_private = {0xb9cd19cb, 0x2919, 0x4935, {0x87, 0x68, 0xef, 0x30, 0xba, 0xe9, 0xa0, 0xcc}};
static const IID *const g_context_sinks[] = {&IID_ITfTextEditSink, &IID_ITfTextLayoutSink_, &IID_ITfStatusSink_,
                                            &g_iid_context_private, &IID_ITfEditTransactionSink_};

static HRESULT STDMETHODCALLTYPE src_advise(ITfSource *This, REFIID riid, IUnknown *sink, DWORD *cookie)
{
    CTX *ctx = CTF_OBJ(CTX, Source, This);
    return SinksAdvise(&ctx->Sinks, g_context_sinks, 5, riid, sink, cookie);
}

static HRESULT STDMETHODCALLTYPE src_unadvise(ITfSource *This, DWORD cookie)
{
    return SinksUnadvise(&CTF_OBJ(CTX, Source, This)->Sinks, cookie);
}

static const ITfSourceVtbl source_vtbl = {Source_query, Source_add_ref, Source_release, src_advise, src_unadvise};

/* ------------------------------------------------------------------ ITfCompartmentMgr */

static HRESULT STDMETHODCALLTYPE cm_get(ITfCompartmentMgr *This, REFGUID guid, ITfCompartment **out)
{
    return CompartmentsGet(CTF_OBJ(CTX, CompartmentMgr, This)->Compartments, guid, out);
}

static HRESULT STDMETHODCALLTYPE cm_clear(ITfCompartmentMgr *This, TfClientId tid, REFGUID guid)
{
    return CompartmentsClear(CTF_OBJ(CTX, CompartmentMgr, This)->Compartments, guid);
}

static HRESULT STDMETHODCALLTYPE cm_enum(ITfCompartmentMgr *This, IEnumGUID **out)
{
    return CompartmentsEnum(CTF_OBJ(CTX, CompartmentMgr, This)->Compartments, out);
}

static const ITfCompartmentMgrVtbl compartments_vtbl = {CompartmentMgr_query, CompartmentMgr_add_ref,
                                                        CompartmentMgr_release, cm_get, cm_clear, cm_enum};

/* ------------------------------------------------------------------ ITfContextOwnerServices */

static HRESULT STDMETHODCALLTYPE os_layout(ITfContextOwnerServices *This)
{
    ServiceStoreNotify(CTF_OBJ(CTX, OwnerServices, This), TRUE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE os_status(ITfContextOwnerServices *This, DWORD flags)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE os_attribute(ITfContextOwnerServices *This, REFGUID guid)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE os_serialize(ITfContextOwnerServices *This, ITfProperty *property, ITfRange *range,
                                              TF_PERSISTENT_PROPERTY_HEADER_ACP *header, IStream *stream)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE os_unserialize(ITfContextOwnerServices *This, ITfProperty *property,
                                                const TF_PERSISTENT_PROPERTY_HEADER_ACP *header, IStream *stream,
                                                ITfPersistentPropertyLoaderACP *loader)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE os_force_load(ITfContextOwnerServices *This, ITfProperty *property)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE os_create_range(ITfContextOwnerServices *This, LONG start, LONG end,
                                                 ITfRangeACP **range)
{
    if (!range)
        return E_INVALIDARG;
    return RngCreate(CTF_OBJ(CTX, OwnerServices, This), start, end, (ITfRange **)range);
}

static const ITfContextOwnerServicesVtbl owner_services_vtbl = {
    OwnerServices_query, OwnerServices_add_ref, OwnerServices_release, os_layout, os_status, os_attribute,
    os_serialize, os_unserialize, os_force_load, os_create_range};

/* ------------------------------------------------------------------ creation */

HRESULT CtxCreate(TM *tm, TfClientId owner, DWORD flags, IUnknown *unknown, ITfContext **context, TfEditCookie *cookie)
{
    CTX *ctx = CtfAlloc(sizeof(*ctx));
    if (!ctx)
        return E_OUTOFMEMORY;
    ctx->Context.lpVtbl = &g_context_vtbl;
    ctx->Insert.lpVtbl = &insert_vtbl;
    ctx->Composition.lpVtbl = &composition_vtbl;
    ctx->Services.lpVtbl = &services_vtbl;
    ctx->Source.lpVtbl = &source_vtbl;
    ctx->CompartmentMgr.lpVtbl = &compartments_vtbl;
    ctx->OwnerServices.lpVtbl = &owner_services_vtbl;
    ctx->StoreSink.lpVtbl = &store_sink_vtbl;
    ctx->Refs = 1;
    ctx->Tm = tm;
    ctx->Client = owner;
    ctx->Compartments = CompartmentsCreate();
    if (!ctx->Compartments) {
        CtfFree(ctx);
        return E_OUTOFMEMORY;
    }
    if (unknown) {
        ctx->Unknown = unknown;
        unknown->lpVtbl->AddRef(unknown);
        if (FAILED(unknown->lpVtbl->QueryInterface(unknown, &IID_ITextStoreACP, (void **)&ctx->Store)))
            ctx->Store = nullptr;
        if (FAILED(unknown->lpVtbl->QueryInterface(unknown, &IID_ITfContextOwnerCompositionSink,
                                                   (void **)&ctx->OwnerSink)))
            ctx->OwnerSink = nullptr;
        CTF_TRACE("msctf: create context store %p composition sink %p\n", ctx->Store, ctx->OwnerSink);
    }
    CtfObjectCreated();
    if (cookie)
        *cookie = ctx->Store ? OWNER_COOKIE : 0;
    *context = &ctx->Context;
    return S_OK;
}
