/*
 * document managers: a stack of contexts, the top one is what a text service edits. a program makes one for each
 * place the user types, and the thread manager tracks which one has the focus.
 */
#include "msctf_private.h"

#define STACK_SIZE 2

struct DOC {
    ITfDocumentMgr Iface;
    ITfCompartmentMgr CompartmentMgr;
    LONG Refs;
    TM *Tm;
    CTX *Stack[STACK_SIZE];
    ULONG Depth;
    COMPARTMENTS *Compartments;
};

extern const ITfDocumentMgrVtbl g_doc_vtbl;

DOC *DocFromInterface(ITfDocumentMgr *iface)
{
    return iface && iface->lpVtbl == &g_doc_vtbl ? (DOC *)iface : nullptr;
}

ITfDocumentMgr *DocInterface(DOC *doc)
{
    return &doc->Iface;
}

void DocAddRef(DOC *doc)
{
    InterlockedIncrement(&doc->Refs);
}

static void doc_free(DOC *doc)
{
    doc->Refs = 1000;
    while (doc->Depth) {
        CTX *ctx = doc->Stack[--doc->Depth];
        doc->Stack[doc->Depth] = nullptr;
        CtxSetDoc(ctx, nullptr);
        CtxRelease(ctx);
    }
    if (doc->Tm)
        TmDocumentDestroyed(doc->Tm, doc);
    CompartmentsFree(doc->Compartments);
    CtfFree(doc);
    CtfObjectDestroyed();
}

void DocRelease(DOC *doc)
{
    if (!InterlockedDecrement(&doc->Refs))
        doc_free(doc);
}

void DocDetachThread(DOC *doc)
{
    doc->Tm = nullptr;
}

CTX *DocTopContext(DOC *doc)
{
    return doc->Depth ? doc->Stack[doc->Depth - 1] : nullptr;
}

static HRESULT STDMETHODCALLTYPE doc_query(ITfDocumentMgr *This, REFIID riid, void **object)
{
    DOC *doc = (DOC *)This;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfDocumentMgr))
        *object = &doc->Iface;
    else if (IsEqualIID(riid, &IID_ITfCompartmentMgr))
        *object = &doc->CompartmentMgr;
    else {
        CTF_TRACE("msctf: document manager query %08lx refused\n", riid->Data1);
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&doc->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE doc_add_ref(ITfDocumentMgr *This)
{
    return InterlockedIncrement(&((DOC *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE doc_release(ITfDocumentMgr *This)
{
    DOC *doc = (DOC *)This;
    LONG refs = InterlockedDecrement(&doc->Refs);
    if (!refs)
        doc_free(doc);
    return refs;
}

static HRESULT STDMETHODCALLTYPE doc_create_context(ITfDocumentMgr *This, TfClientId tid, DWORD flags, IUnknown *unknown,
                                                    ITfContext **context, TfEditCookie *cookie)
{
    DOC *doc = (DOC *)This;
    if (cookie)
        *cookie = 0;
    if (!context)
        return E_INVALIDARG;
    *context = nullptr;
    if (!cookie || (flags & ~1u) || !doc->Tm || !TmValidClient(doc->Tm, tid))
        return E_INVALIDARG;
    return CtxCreate(doc->Tm, tid, flags, unknown, context, cookie);
}

static HRESULT STDMETHODCALLTYPE doc_push(ITfDocumentMgr *This, ITfContext *context)
{
    DOC *doc = (DOC *)This;
    CTX *ctx = context ? CtxFromInterface(context) : nullptr;
    if (!ctx)
        return E_INVALIDARG;
    if (doc->Depth == STACK_SIZE)
        return TF_E_STACKFULL;
    if (ctx->Doc)
        return E_INVALIDARG;
    CtxAddRef(ctx);
    BOOL first = doc->Depth == 0;
    doc->Stack[doc->Depth++] = ctx;
    CtxSetDoc(ctx, doc);
    if (doc->Tm) {
        if (first)
            TmFireInitDocument(doc->Tm, doc, TRUE);
        TmFireContext(doc->Tm, ctx, TRUE);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE doc_pop(ITfDocumentMgr *This, DWORD flags)
{
    DOC *doc = (DOC *)This;
    if (flags & ~TF_POPF_ALL)
        return E_INVALIDARG;
    if (!doc->Depth)
        return E_FAIL;
    /* the base context stays unless TF_POPF_ALL is given. */
    if (!(flags & TF_POPF_ALL) && doc->Depth == 1)
        return E_FAIL;
    do {
        CTX *ctx = doc->Stack[--doc->Depth];
        doc->Stack[doc->Depth] = nullptr;
        if (doc->Tm)
            TmFireContext(doc->Tm, ctx, FALSE);
        CtxSetDoc(ctx, nullptr);
        CtxRelease(ctx);
    } while ((flags & TF_POPF_ALL) && doc->Depth);
    if ((flags & TF_POPF_ALL) && doc->Tm)
        TmFireInitDocument(doc->Tm, doc, FALSE);
    return S_OK;
}

static HRESULT get_context(DOC *doc, ULONG index, ITfContext **context)
{
    if (!context)
        return E_INVALIDARG;
    if (index >= doc->Depth) {
        /* an empty stack still hands out the empty context. */
        *context = CtxEmpty();
        return S_OK;
    }
    *context = CtxInterface(doc->Stack[index]);
    (*context)->lpVtbl->AddRef(*context);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE doc_get_top(ITfDocumentMgr *This, ITfContext **context)
{
    DOC *doc = (DOC *)This;
    return get_context(doc, doc->Depth ? doc->Depth - 1 : 0, context);
}

static HRESULT STDMETHODCALLTYPE doc_get_base(ITfDocumentMgr *This, ITfContext **context)
{
    return get_context((DOC *)This, 0, context);
}

static HRESULT STDMETHODCALLTYPE doc_enum_contexts(ITfDocumentMgr *This, IEnumTfContexts **out)
{
    DOC *doc = (DOC *)This;
    IUnknown *items[STACK_SIZE];
    if (!out)
        return E_POINTER;
    for (ULONG i = 0; i < doc->Depth; i++)
        items[i] = (IUnknown *)CtxInterface(doc->Stack[doc->Depth - 1 - i]);
    return EnumCreate(&IID_IEnumTfContexts, items, doc->Depth, (void **)out);
}

const ITfDocumentMgrVtbl g_doc_vtbl = {doc_query, doc_add_ref, doc_release, doc_create_context, doc_push, doc_pop,
                                       doc_get_top, doc_get_base, doc_enum_contexts};

static DOC *from_compartments(ITfCompartmentMgr *This)
{
    return CONTAINING_RECORD(This, DOC, CompartmentMgr);
}

static HRESULT STDMETHODCALLTYPE dc_query(ITfCompartmentMgr *This, REFIID riid, void **object)
{
    return doc_query(&from_compartments(This)->Iface, riid, object);
}

static ULONG STDMETHODCALLTYPE dc_add_ref(ITfCompartmentMgr *This)
{
    return doc_add_ref(&from_compartments(This)->Iface);
}

static ULONG STDMETHODCALLTYPE dc_release(ITfCompartmentMgr *This)
{
    return doc_release(&from_compartments(This)->Iface);
}

static HRESULT STDMETHODCALLTYPE dc_get(ITfCompartmentMgr *This, REFGUID guid, ITfCompartment **out)
{
    return CompartmentsGet(from_compartments(This)->Compartments, guid, out);
}

static HRESULT STDMETHODCALLTYPE dc_clear(ITfCompartmentMgr *This, TfClientId tid, REFGUID guid)
{
    return CompartmentsClear(from_compartments(This)->Compartments, guid);
}

static HRESULT STDMETHODCALLTYPE dc_enum(ITfCompartmentMgr *This, IEnumGUID **out)
{
    return CompartmentsEnum(from_compartments(This)->Compartments, out);
}

static const ITfCompartmentMgrVtbl compartments_vtbl = {dc_query, dc_add_ref, dc_release, dc_get, dc_clear, dc_enum};

HRESULT DocCreate(TM *tm, DOC **out)
{
    DOC *doc = CtfAlloc(sizeof(*doc));
    if (!doc)
        return E_OUTOFMEMORY;
    doc->Compartments = CompartmentsCreate();
    if (!doc->Compartments) {
        CtfFree(doc);
        return E_OUTOFMEMORY;
    }
    doc->Iface.lpVtbl = &g_doc_vtbl;
    doc->CompartmentMgr.lpVtbl = &compartments_vtbl;
    doc->Refs = 1;
    doc->Tm = tm;
    CtfObjectCreated();
    *out = doc;
    return S_OK;
}
