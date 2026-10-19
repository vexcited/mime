/*
 * the thread manager. one lives for each thread that uses the framework; it keeps the thread's document managers,
 * which of them has the focus, and the sink window the host's input method reaches the thread through.
 */
#include "msctf_private.h"

#define MAX_ASSOC 64

typedef struct ASSOC {
    HWND Window;
    DOC *Doc;
} ASSOC;

typedef struct UIELEMENT {
    ITfUIElement *Element;
    DWORD Id;
    BOOL Shown;
} UIELEMENT;

struct TM {
    ITfThreadMgrEx Mgr;
    ITfSource Source;
    ITfKeystrokeMgr Keystroke;
    ITfMessagePump Pump;
    ITfClientId ClientIds;
    ITfUIElementMgr UiMgr;
    ITfCompartmentMgr CompartmentMgr;
    COMPARTMENTS *Compartments;
    LONG Refs;
    DWORD Thread;
    LONG Activations;
    TfClientId Client;
    DWORD ActiveFlags;
    DOC **Docs;
    ULONG DocCount, DocCapacity;
    DOC *Focus;
    DOC *NotifiedFocus;
    ASSOC Assoc[MAX_ASSOC];
    ULONG AssocCount;
    SINKS Sinks;
    UIELEMENT *Elements;
    ULONG ElementCount, ElementCapacity;
    DWORD NextElement;
    BOOL KeySinkAdvised;
    BOOL Destroying, Deactivating;
    void *Service;
};

/* the thread's manager, found without keeping it alive: it goes when its last user lets go. */
WINUSERAPI BOOL WINAPI PeekMessageA(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove);
WINUSERAPI BOOL WINAPI GetMessageA(LPMSG msg, HWND hwnd, UINT min, UINT max);

static DWORD g_tls = TLS_OUT_OF_INDEXES;

/* the client id every program gets, the atom of windows' own id for programs. */
#define APP_CLIENT 32

static DWORD tls_index(void)
{
    if (g_tls == TLS_OUT_OF_INDEXES) {
        DWORD index = TlsAlloc();
        if (InterlockedCompareExchange((volatile LONG *)&g_tls, (LONG)index, (LONG)TLS_OUT_OF_INDEXES) !=
            (LONG)TLS_OUT_OF_INDEXES)
            TlsFree(index);
    }
    return g_tls;
}

/* ------------------------------------------------------------------ the object */

static HRESULT tm_query(TM *tm, REFIID riid, void **object)
{
    void *found = nullptr;
    if (!object)
        return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfThreadMgr) || IsEqualIID(riid, &IID_ITfThreadMgrEx))
        found = &tm->Mgr;
    else if (IsEqualIID(riid, &IID_ITfSource))
        found = &tm->Source;
    else if (IsEqualIID(riid, &IID_ITfKeystrokeMgr))
        found = &tm->Keystroke;
    else if (IsEqualIID(riid, &IID_ITfMessagePump))
        found = &tm->Pump;
    else if (IsEqualIID(riid, &IID_ITfClientId))
        found = &tm->ClientIds;
    else if (IsEqualIID(riid, &IID_ITfUIElementMgr))
        found = &tm->UiMgr;
    else if (IsEqualIID(riid, &IID_ITfCompartmentMgr))
        found = &tm->CompartmentMgr;
    else if (IsEqualIID(riid, &IID_ITfCategoryMgr))
        return CategoryMgrCreate(riid, object);
    else if (IsEqualIID(riid, &IID_ITfDisplayAttributeMgr))
        return DisplayAttributeMgrCreate(riid, object);
    if (!found) {
        CTF_TRACE("msctf: thread manager query %08lx-%04x refused\n", riid->Data1, riid->Data2);
        *object = nullptr;
        return E_NOINTERFACE;
    }
    *object = found;
    InterlockedIncrement(&tm->Refs);
    return S_OK;
}

static void tm_free(TM *tm);

static ULONG tm_release(TM *tm)
{
    LONG refs = InterlockedDecrement(&tm->Refs);
    if (!refs) {
        /* the references the object keeps on itself through its documents must not revive it. */
        tm->Refs = 0x1000;
        tm_free(tm);
    }
    return refs;
}

#define TM_THUNKS(Iface, field)                                                                                        \
    static HRESULT STDMETHODCALLTYPE field##_query(Iface *This, REFIID riid, void **object)                            \
    {                                                                                                                  \
        return tm_query(CTF_OBJ(TM, field, This), riid, object);                                                       \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE field##_add_ref(Iface *This)                                                        \
    {                                                                                                                  \
        return InterlockedIncrement(&CTF_OBJ(TM, field, This)->Refs);                                                  \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE field##_release(Iface *This)                                                        \
    {                                                                                                                  \
        return tm_release(CTF_OBJ(TM, field, This));                                                                   \
    }

TM_THUNKS(ITfThreadMgrEx, Mgr)
TM_THUNKS(ITfSource, Source)
TM_THUNKS(ITfKeystrokeMgr, Keystroke)
TM_THUNKS(ITfMessagePump, Pump)
TM_THUNKS(ITfClientId, ClientIds)
TM_THUNKS(ITfUIElementMgr, UiMgr)
TM_THUNKS(ITfCompartmentMgr, CompartmentMgr)

static void doc_list_remove(TM *tm, DOC *doc)
{
    for (ULONG i = 0; i < tm->DocCount; i++) {
        if (tm->Docs[i] == doc) {
            memmove(&tm->Docs[i], &tm->Docs[i + 1], (tm->DocCount - i - 1) * sizeof(DOC *));
            tm->DocCount--;
            return;
        }
    }
}

void TmDocumentDestroyed(TM *tm, DOC *doc)
{
    doc_list_remove(tm, doc);
    if (tm->NotifiedFocus == doc)
        tm->NotifiedFocus = nullptr;
}

static void tm_free(TM *tm)
{
    tm->Destroying = TRUE;
    if (tm->Activations)
        ServiceThreadDetach(tm);
    if (TlsGetValue(tls_index()) == tm)
        TlsSetValue(tls_index(), nullptr);
    for (ULONG i = 0; i < tm->DocCount; i++)
        DocDetachThread(tm->Docs[i]);
    tm->DocCount = 0;
    CompartmentsFree(tm->Compartments);
    if (tm->Focus)
        DocRelease(tm->Focus);
    for (ULONG i = 0; i < tm->AssocCount; i++)
        if (tm->Assoc[i].Doc)
            DocRelease(tm->Assoc[i].Doc);
    for (ULONG i = 0; i < tm->ElementCount; i++)
        tm->Elements[i].Element->lpVtbl->Release(tm->Elements[i].Element);
    CtfFree(tm->Elements);
    SinksFree(&tm->Sinks);
    CtfFree(tm->Docs);
    CtfFree(tm);
    CtfObjectDestroyed();
}

TM *TmCurrent(void)
{
    return TlsGetValue(tls_index());
}

TfClientId TmClientId(TM *tm)
{
    return tm->Client;
}

/* ------------------------------------------------------------------ focus */

static void fire_set_focus(TM *tm, DOC *focus, DOC *previous)
{
    IUnknown **sinks;
    ULONG n = SinksSnapshot(&tm->Sinks, &IID_ITfThreadMgrEventSink, &sinks);
    for (ULONG i = 0; i < n; i++) {
        ITfThreadMgrEventSink *sink = (ITfThreadMgrEventSink *)sinks[i];
        sink->lpVtbl->OnSetFocus(sink, focus ? DocInterface(focus) : nullptr, previous ? DocInterface(previous) : nullptr);
    }
    SinksSnapshotFree(sinks, n);
}

DOC *TmDocumentForWindow(TM *tm, HWND hwnd)
{
    for (HWND h = hwnd; h; h = GetParent(h)) {
        for (ULONG i = 0; i < tm->AssocCount; i++)
            if (tm->Assoc[i].Window == h)
                return tm->Assoc[i].Doc;
    }
    return nullptr;
}

/* whether a document set as the focus is for the window that has it: the window of its text store, or one around it. */
static BOOL document_fits(DOC *doc, HWND focus)
{
    CTX *ctx = DocTopContext(doc);
    HWND window;
    if (!ctx || !CtxStoreWindow(ctx, &window))
        return TRUE;
    return window == focus || IsChild(window, focus) || IsChild(focus, window);
}

/* the document with the focus now: the one the focus window is associated with, else the one the program set, once
   the thread has a window with the focus that it is for. */
DOC *TmFocusDoc(TM *tm)
{
    HWND focus = GetFocus();
    DOC *doc = focus && tm->AssocCount ? TmDocumentForWindow(tm, focus) : nullptr;
    if (!doc && focus && tm->Focus && document_fits(tm->Focus, focus))
        doc = tm->Focus;
    return doc;
}

static void tm_notify_focus(TM *tm)
{
    DOC *doc = TmFocusDoc(tm);
    if (doc == tm->NotifiedFocus)
        return;
    DOC *previous = tm->NotifiedFocus;
    tm->NotifiedFocus = doc;
    fire_set_focus(tm, doc, previous);
    ServiceDocumentChanged(tm);
}

/* ------------------------------------------------------------------ ITfThreadMgr */

static HRESULT activate(TM *tm, TfClientId *id, DWORD flags)
{
    HRESULT hr = S_OK;
    if (!id)
        return E_INVALIDARG;
    if (flags & ~0x7Fu)
        return E_INVALIDARG;
    if (tm->Deactivating)
        return E_UNEXPECTED;
    if (!tm->Activations) {
        tm->Client = APP_CLIENT;
        tm->ActiveFlags = flags | 0x80000000u;
        ServiceThreadAttach(tm);
    } else {
        hr = S_FALSE;
    }
    tm->Activations++;
    *id = tm->Client;
    CTF_TRACE("msctf: activate flags %lx client %lu count %ld\n", flags, tm->Client, tm->Activations);
    return hr;
}

static HRESULT STDMETHODCALLTYPE tm_activate(ITfThreadMgrEx *This, TfClientId *id)
{
    /* a second activation is just another reference to the first. */
    return SUCCEEDED(activate(CTF_OBJ(TM, Mgr, This), id, 0)) ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE tm_activate_ex(ITfThreadMgrEx *This, TfClientId *id, DWORD flags)
{
    return activate(CTF_OBJ(TM, Mgr, This), id, flags);
}

static HRESULT STDMETHODCALLTYPE tm_get_active_flags(ITfThreadMgrEx *This, DWORD *flags)
{
    TM *tm = CTF_OBJ(TM, Mgr, This);
    if (!flags)
        return E_INVALIDARG;
    *flags = tm->Activations ? tm->ActiveFlags : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tm_deactivate(ITfThreadMgrEx *This)
{
    TM *tm = CTF_OBJ(TM, Mgr, This);
    CTF_TRACE("msctf: deactivate count %ld\n", tm->Activations);
    if (!tm->Activations)
        return E_UNEXPECTED;
    if (!--tm->Activations) {
        tm->Deactivating = TRUE;
        ServiceThreadDetach(tm);
        tm->Client = 0;
        tm->ActiveFlags = 0;
        tm->Deactivating = FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tm_create_document(ITfThreadMgrEx *This, ITfDocumentMgr **out)
{
    TM *tm = CTF_OBJ(TM, Mgr, This);
    DOC *doc;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    HRESULT hr = DocCreate(tm, &doc);
    if (FAILED(hr))
        return hr;
    if (tm->DocCount == tm->DocCapacity) {
        ULONG capacity = tm->DocCapacity ? tm->DocCapacity * 2 : 4;
        DOC **docs = CtfAlloc(capacity * sizeof(DOC *));
        if (!docs) {
            DocRelease(doc);
            return E_OUTOFMEMORY;
        }
        if (tm->Docs)
            memcpy(docs, tm->Docs, tm->DocCount * sizeof(DOC *));
        CtfFree(tm->Docs);
        tm->Docs = docs;
        tm->DocCapacity = capacity;
    }
    tm->Docs[tm->DocCount++] = doc;
    *out = DocInterface(doc);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tm_enum_documents(ITfThreadMgrEx *This, IEnumTfDocumentMgrs **out)
{
    TM *tm = CTF_OBJ(TM, Mgr, This);
    IUnknown *items[64];
    ULONG n = tm->DocCount < 64 ? tm->DocCount : 64;
    if (!out)
        return E_INVALIDARG;
    for (ULONG i = 0; i < n; i++)
        items[i] = (IUnknown *)DocInterface(tm->Docs[i]);
    return EnumCreate(&IID_IEnumTfDocumentMgrs, items, n, (void **)out);
}

static HRESULT STDMETHODCALLTYPE tm_get_focus(ITfThreadMgrEx *This, ITfDocumentMgr **out)
{
    TM *tm = CTF_OBJ(TM, Mgr, This);
    if (!out)
        return E_INVALIDARG;
    /* the document in focus, else the one waiting for its window to get it. */
    DOC *doc = TmFocusDoc(tm);
    if (!doc)
        doc = tm->Focus;
    *out = doc ? DocInterface(doc) : nullptr;
    if (!doc)
        return S_FALSE;
    (*out)->lpVtbl->AddRef(*out);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tm_set_focus(ITfThreadMgrEx *This, ITfDocumentMgr *document)
{
    TM *tm = CTF_OBJ(TM, Mgr, This);
    DOC *doc = document ? DocFromInterface(document) : nullptr;
    CTF_TRACE("msctf: set focus %p\n", doc);
    if (tm->Deactivating)
        return E_UNEXPECTED;
    if (document && !doc)
        return E_INVALIDARG;
    if (doc)
        DocAddRef(doc);
    if (tm->Focus)
        DocRelease(tm->Focus);
    tm->Focus = doc;
    tm_notify_focus(tm);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tm_associate_focus(ITfThreadMgrEx *This, HWND hwnd, ITfDocumentMgr *document,
                                                    ITfDocumentMgr **previous)
{
    TM *tm = CTF_OBJ(TM, Mgr, This);
    DOC *doc = document ? DocFromInterface(document) : nullptr;
    CTF_TRACE("msctf: associate focus window %p document %p\n", hwnd, doc);
    if (!hwnd || !previous || (document && !doc))
        return E_INVALIDARG;
    *previous = nullptr;
    ULONG i;
    for (i = 0; i < tm->AssocCount; i++)
        if (tm->Assoc[i].Window == hwnd)
            break;
    if (i < tm->AssocCount) {
        DOC *old = tm->Assoc[i].Doc;
        if (old) {
            *previous = DocInterface(old);
            (*previous)->lpVtbl->AddRef(*previous);
            DocRelease(old);
        }
        if (doc) {
            DocAddRef(doc);
            tm->Assoc[i].Doc = doc;
        } else {
            memmove(&tm->Assoc[i], &tm->Assoc[i + 1], (tm->AssocCount - i - 1) * sizeof(ASSOC));
            tm->AssocCount--;
        }
    } else if (doc) {
        if (tm->AssocCount == MAX_ASSOC) {
            /* the oldest association makes room: a window gone for long. */
            DocRelease(tm->Assoc[0].Doc);
            memmove(&tm->Assoc[0], &tm->Assoc[1], (MAX_ASSOC - 1) * sizeof(ASSOC));
            tm->AssocCount--;
        }
        DocAddRef(doc);
        tm->Assoc[tm->AssocCount].Window = hwnd;
        tm->Assoc[tm->AssocCount].Doc = doc;
        tm->AssocCount++;
    }
    tm_notify_focus(tm);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tm_is_thread_focus(ITfThreadMgrEx *This, BOOL *focus)
{
    if (!focus)
        return E_INVALIDARG;
    *focus = GetFocus() != nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tm_get_function_provider(ITfThreadMgrEx *This, REFCLSID clsid, ITfFunctionProvider **out)
{
    if (!clsid || !out)
        return E_INVALIDARG;
    *out = nullptr;
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE tm_enum_function_providers(ITfThreadMgrEx *This, IEnumTfFunctionProviders **out)
{
    if (!out)
        return E_INVALIDARG;
    return EnumCreate(&IID_IEnumTfFunctionProviders, nullptr, 0, (void **)out);
}

static HRESULT STDMETHODCALLTYPE tm_get_global_compartment(ITfThreadMgrEx *This, ITfCompartmentMgr **out)
{
    if (!out)
        return E_INVALIDARG;
    return GlobalCompartmentMgr(out);
}

static const ITfThreadMgrExVtbl mgr_vtbl = {Mgr_query, Mgr_add_ref, Mgr_release, tm_activate, tm_deactivate,
                                            tm_create_document, tm_enum_documents, tm_get_focus, tm_set_focus,
                                            tm_associate_focus, tm_is_thread_focus, tm_get_function_provider,
                                            tm_enum_function_providers, tm_get_global_compartment, tm_activate_ex,
                                            tm_get_active_flags};

/* ------------------------------------------------------------------ events for sinks */

HRESULT TmFireInitDocument(TM *tm, DOC *doc, BOOL init)
{
    IUnknown **sinks;
    ULONG n = SinksSnapshot(&tm->Sinks, &IID_ITfThreadMgrEventSink, &sinks);
    for (ULONG i = 0; i < n; i++) {
        ITfThreadMgrEventSink *sink = (ITfThreadMgrEventSink *)sinks[i];
        if (init)
            sink->lpVtbl->OnInitDocumentMgr(sink, DocInterface(doc));
        else
            sink->lpVtbl->OnUninitDocumentMgr(sink, DocInterface(doc));
    }
    SinksSnapshotFree(sinks, n);
    return S_OK;
}

void TmFireContext(TM *tm, CTX *ctx, BOOL push)
{
    IUnknown **sinks;
    ULONG n = SinksSnapshot(&tm->Sinks, &IID_ITfThreadMgrEventSink, &sinks);
    for (ULONG i = 0; i < n; i++) {
        ITfThreadMgrEventSink *sink = (ITfThreadMgrEventSink *)sinks[i];
        if (push)
            sink->lpVtbl->OnPushContext(sink, CtxInterface(ctx));
        else
            sink->lpVtbl->OnPopContext(sink, CtxInterface(ctx));
    }
    SinksSnapshotFree(sinks, n);
    if (tm->Focus || tm->AssocCount)
        ServiceDocumentChanged(tm);
}

/* ------------------------------------------------------------------ ITfSource */

/* the sinks a thread manager takes, in the order the cookies number them. */
static const IID g_iid_private = {0xad56f402, 0xe162, 0x4f25, {0x90, 0x8f, 0x7d, 0x57, 0x7c, 0xf9, 0xbd, 0xa9}};
static const IID g_iid_preserved_key = {0x6f77c993, 0xd2b1, 0x446e, {0x85, 0x3e, 0x59, 0x12, 0xef, 0xc8, 0xa2, 0x86}};
static const IID g_iid_handwriting = {0xc6b8d7d1, 0x5c1e, 0x4f8c, {0x9a, 0x44, 0x2b, 0x6e, 0x31, 0x7a, 0x08, 0x5d}};
static const IID *const g_thread_sinks[] = {
    &g_iid_private, &IID_ITfActiveLanguageProfileNotifySink, &IID_ITfThreadFocusSink, &g_iid_preserved_key,
    &IID_ITfThreadMgrEventSink, &IID_ITfKeyTraceEventSink, &IID_ITfUIElementSink,
    &IID_ITfInputProcessorProfileActivationSink, &g_iid_handwriting};

static HRESULT STDMETHODCALLTYPE source_advise(ITfSource *This, REFIID riid, IUnknown *sink, DWORD *cookie)
{
    TM *tm = CTF_OBJ(TM, Source, This);
    return SinksAdvise(&tm->Sinks, g_thread_sinks, 9, riid, sink, cookie);
}

static HRESULT STDMETHODCALLTYPE source_unadvise(ITfSource *This, DWORD cookie)
{
    return SinksUnadvise(&CTF_OBJ(TM, Source, This)->Sinks, cookie);
}

static const ITfSourceVtbl source_vtbl = {Source_query, Source_add_ref, Source_release, source_advise, source_unadvise};

/* ------------------------------------------------------------------ ITfKeystrokeMgr */

static HRESULT STDMETHODCALLTYPE key_advise(ITfKeystrokeMgr *This, TfClientId tid, ITfKeyEventSink *sink, BOOL foreground)
{
    if (!sink)
        return E_INVALIDARG;
    CTF_OBJ(TM, Keystroke, This)->KeySinkAdvised = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE key_unadvise(ITfKeystrokeMgr *This, TfClientId tid)
{
    CTF_OBJ(TM, Keystroke, This)->KeySinkAdvised = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE key_get_foreground(ITfKeystrokeMgr *This, CLSID *clsid)
{
    if (!clsid)
        return E_INVALIDARG;
    memset(clsid, 0, sizeof(*clsid));
    return S_FALSE;
}

/* keys are never eaten: the host's input method takes its keys before the program sees them. */
static HRESULT key_eat(WPARAM wparam, LPARAM lparam, BOOL *eaten)
{
    if (!eaten)
        return E_INVALIDARG;
    *eaten = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE key_test_down(ITfKeystrokeMgr *This, WPARAM wparam, LPARAM lparam, BOOL *eaten)
{
    return key_eat(wparam, lparam, eaten);
}

static HRESULT STDMETHODCALLTYPE key_test_up(ITfKeystrokeMgr *This, WPARAM wparam, LPARAM lparam, BOOL *eaten)
{
    return key_eat(wparam, lparam, eaten);
}

static HRESULT STDMETHODCALLTYPE key_down(ITfKeystrokeMgr *This, WPARAM wparam, LPARAM lparam, BOOL *eaten)
{
    return key_eat(wparam, lparam, eaten);
}

static HRESULT STDMETHODCALLTYPE key_up(ITfKeystrokeMgr *This, WPARAM wparam, LPARAM lparam, BOOL *eaten)
{
    return key_eat(wparam, lparam, eaten);
}

static HRESULT STDMETHODCALLTYPE key_get_preserved(ITfKeystrokeMgr *This, ITfContext *context,
                                                   const TF_PRESERVEDKEY *key, GUID *guid)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE key_is_preserved(ITfKeystrokeMgr *This, REFGUID guid, const TF_PRESERVEDKEY *key,
                                                  BOOL *registered)
{
    if (!guid || !key || !registered)
        return E_INVALIDARG;
    *registered = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE key_preserve(ITfKeystrokeMgr *This, TfClientId tid, REFGUID guid,
                                              const TF_PRESERVEDKEY *key, const WCHAR *description, ULONG cch)
{
    if (!guid || !key)
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE key_unpreserve(ITfKeystrokeMgr *This, REFGUID guid, const TF_PRESERVEDKEY *key)
{
    if (!guid || !key)
        return E_INVALIDARG;
    return CONNECT_E_NOCONNECTION;
}

static HRESULT STDMETHODCALLTYPE key_set_description(ITfKeystrokeMgr *This, REFGUID guid, const WCHAR *description,
                                                     ULONG cch)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE key_get_description(ITfKeystrokeMgr *This, REFGUID guid, BSTR *description)
{
    if (!description)
        return E_INVALIDARG;
    *description = nullptr;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE key_simulate(ITfKeystrokeMgr *This, ITfContext *context, REFGUID guid, BOOL *eaten)
{
    if (!eaten)
        return E_INVALIDARG;
    *eaten = FALSE;
    return S_OK;
}

static const ITfKeystrokeMgrVtbl keystroke_vtbl = {Keystroke_query, Keystroke_add_ref, Keystroke_release, key_advise,
                                                   key_unadvise, key_get_foreground, key_test_down, key_test_up,
                                                   key_down, key_up, key_get_preserved, key_is_preserved,
                                                   key_preserve, key_unpreserve, key_set_description,
                                                   key_get_description, key_simulate};

/* ------------------------------------------------------------------ ITfMessagePump */

static HRESULT STDMETHODCALLTYPE pump_peek_a(ITfMessagePump *This, LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove,
                                             BOOL *result)
{
    if (!msg || !result)
        return E_INVALIDARG;
    *result = PeekMessageA(msg, hwnd, min, max, remove);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pump_get_a(ITfMessagePump *This, LPMSG msg, HWND hwnd, UINT min, UINT max, BOOL *result)
{
    if (!msg || !result)
        return E_INVALIDARG;
    *result = GetMessageA(msg, hwnd, min, max);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pump_peek_w(ITfMessagePump *This, LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove,
                                             BOOL *result)
{
    if (!msg || !result)
        return E_INVALIDARG;
    *result = PeekMessageW(msg, hwnd, min, max, remove);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pump_get_w(ITfMessagePump *This, LPMSG msg, HWND hwnd, UINT min, UINT max, BOOL *result)
{
    if (!msg || !result)
        return E_INVALIDARG;
    *result = GetMessageW(msg, hwnd, min, max);
    return S_OK;
}

static const ITfMessagePumpVtbl pump_vtbl = {Pump_query, Pump_add_ref, Pump_release, pump_peek_a, pump_get_a,
                                             pump_peek_w, pump_get_w};

/* ------------------------------------------------------------------ ITfClientId */

static HRESULT STDMETHODCALLTYPE client_get(ITfClientId *This, REFCLSID clsid, TfClientId *id)
{
    if (!clsid || !id)
        return E_INVALIDARG;
    /* a client is known by the atom of its class id. */
    *id = CtfGuidAtom(clsid);
    return *id ? S_OK : E_OUTOFMEMORY;
}

static const ITfClientIdVtbl client_vtbl = {ClientIds_query, ClientIds_add_ref, ClientIds_release, client_get};

/* ------------------------------------------------------------------ ITfUIElementMgr */

static UIELEMENT *find_element(TM *tm, DWORD id)
{
    for (ULONG i = 0; i < tm->ElementCount; i++)
        if (tm->Elements[i].Id == id)
            return &tm->Elements[i];
    return nullptr;
}

static HRESULT STDMETHODCALLTYPE ui_begin(ITfUIElementMgr *This, ITfUIElement *element, BOOL *show, DWORD *id)
{
    TM *tm = CTF_OBJ(TM, UiMgr, This);
    if (!element || !show || !id)
        return E_INVALIDARG;
    if (tm->ElementCount == tm->ElementCapacity) {
        ULONG capacity = tm->ElementCapacity ? tm->ElementCapacity * 2 : 4;
        UIELEMENT *items = CtfAlloc(capacity * sizeof(UIELEMENT));
        if (!items)
            return E_OUTOFMEMORY;
        if (tm->Elements)
            memcpy(items, tm->Elements, tm->ElementCount * sizeof(UIELEMENT));
        CtfFree(tm->Elements);
        tm->Elements = items;
        tm->ElementCapacity = capacity;
    }
    UIELEMENT *entry = &tm->Elements[tm->ElementCount++];
    entry->Element = element;
    element->lpVtbl->AddRef(element);
    entry->Id = ++tm->NextElement;
    *id = entry->Id;
    /* the sinks of the thread decide whether the service draws the element itself. */
    BOOL shown = TRUE;
    IUnknown **sinks;
    ULONG n = SinksSnapshot(&tm->Sinks, &IID_ITfUIElementSink, &sinks);
    for (ULONG i = 0; i < n; i++) {
        ITfUIElementSink *sink = (ITfUIElementSink *)sinks[i];
        BOOL one = TRUE;
        sink->lpVtbl->BeginUIElement(sink, entry->Id, &one);
        shown = shown && one;
    }
    SinksSnapshotFree(sinks, n);
    entry->Shown = shown;
    *show = shown;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ui_update(ITfUIElementMgr *This, DWORD id)
{
    TM *tm = CTF_OBJ(TM, UiMgr, This);
    if (!find_element(tm, id))
        return E_INVALIDARG;
    IUnknown **sinks;
    ULONG n = SinksSnapshot(&tm->Sinks, &IID_ITfUIElementSink, &sinks);
    for (ULONG i = 0; i < n; i++) {
        ITfUIElementSink *sink = (ITfUIElementSink *)sinks[i];
        sink->lpVtbl->UpdateUIElement(sink, id);
    }
    SinksSnapshotFree(sinks, n);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ui_end(ITfUIElementMgr *This, DWORD id)
{
    TM *tm = CTF_OBJ(TM, UiMgr, This);
    UIELEMENT *entry = find_element(tm, id);
    if (!entry)
        return E_INVALIDARG;
    IUnknown **sinks;
    ULONG n = SinksSnapshot(&tm->Sinks, &IID_ITfUIElementSink, &sinks);
    for (ULONG i = 0; i < n; i++) {
        ITfUIElementSink *sink = (ITfUIElementSink *)sinks[i];
        sink->lpVtbl->EndUIElement(sink, id);
    }
    SinksSnapshotFree(sinks, n);
    entry = find_element(tm, id);
    if (entry) {
        ITfUIElement *element = entry->Element;
        *entry = tm->Elements[--tm->ElementCount];
        element->lpVtbl->Release(element);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ui_get(ITfUIElementMgr *This, DWORD id, ITfUIElement **element)
{
    UIELEMENT *entry;
    if (!element)
        return E_INVALIDARG;
    *element = nullptr;
    entry = find_element(CTF_OBJ(TM, UiMgr, This), id);
    if (!entry)
        return E_INVALIDARG;
    *element = entry->Element;
    (*element)->lpVtbl->AddRef(*element);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ui_enum(ITfUIElementMgr *This, IEnumTfUIElements **out)
{
    TM *tm = CTF_OBJ(TM, UiMgr, This);
    IUnknown *items[32];
    ULONG n = tm->ElementCount < 32 ? tm->ElementCount : 32;
    if (!out)
        return E_INVALIDARG;
    for (ULONG i = 0; i < n; i++)
        items[i] = (IUnknown *)tm->Elements[i].Element;
    return EnumCreate(&IID_IEnumTfUIElements, items, n, (void **)out);
}

static const ITfUIElementMgrVtbl ui_vtbl = {UiMgr_query, UiMgr_add_ref, UiMgr_release, ui_begin, ui_update, ui_end,
                                            ui_get, ui_enum};

/* ------------------------------------------------------------------ the thread's own compartments */

static HRESULT STDMETHODCALLTYPE tc_get(ITfCompartmentMgr *This, REFGUID guid, ITfCompartment **out)
{
    return CompartmentsGet(CTF_OBJ(TM, CompartmentMgr, This)->Compartments, guid, out);
}

static HRESULT STDMETHODCALLTYPE tc_clear(ITfCompartmentMgr *This, TfClientId tid, REFGUID guid)
{
    return CompartmentsClear(CTF_OBJ(TM, CompartmentMgr, This)->Compartments, guid);
}

static HRESULT STDMETHODCALLTYPE tc_enum(ITfCompartmentMgr *This, IEnumGUID **out)
{
    return CompartmentsEnum(CTF_OBJ(TM, CompartmentMgr, This)->Compartments, out);
}

static const ITfCompartmentMgrVtbl thread_compartments_vtbl = {CompartmentMgr_query, CompartmentMgr_add_ref,
                                                               CompartmentMgr_release, tc_get, tc_clear, tc_enum};

/* ------------------------------------------------------------------ creation */

HRESULT TmCreateObject(REFIID riid, void **object)
{
    TM *tm = TlsGetValue(tls_index());
    if (!object)
        return E_POINTER;
    if (!tm) {
        tm = CtfAlloc(sizeof(*tm));
        if (!tm)
            return E_OUTOFMEMORY;
        tm->Mgr.lpVtbl = &mgr_vtbl;
        tm->Source.lpVtbl = &source_vtbl;
        tm->Keystroke.lpVtbl = &keystroke_vtbl;
        tm->Pump.lpVtbl = &pump_vtbl;
        tm->ClientIds.lpVtbl = &client_vtbl;
        tm->UiMgr.lpVtbl = &ui_vtbl;
        tm->CompartmentMgr.lpVtbl = &thread_compartments_vtbl;
        tm->Compartments = CompartmentsCreate();
        if (!tm->Compartments) {
            CtfFree(tm);
            return E_OUTOFMEMORY;
        }
        tm->Thread = GetCurrentThreadId();
        tm->Refs = 1;
        CtfObjectCreated();
        TlsSetValue(tls_index(), tm);
        HRESULT hr = tm_query(tm, riid, object);
        tm_release(tm);
        return hr;
    }
    return tm_query(tm, riid, object);
}

/* whether an id was handed out: the program's own, the system's or one made from a class id. */
BOOL TmValidClient(TM *tm, TfClientId tid)
{
    GUID guid;
    return tid && CtfAtomGuid(tid, &guid);
}

void *TmService(TM *tm)
{
    return tm->Service;
}

void TmSetService(TM *tm, void *service)
{
    tm->Service = service;
}
