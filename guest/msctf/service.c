/*
 * the host's input method as a text service. win32k sends the desktop input method's preedit and committed text to a
 * window of the thread, which edits the focused document like a Windows text service: it asks for an edit session,
 * starts a composition, sets its text, marks it with display attributes and ends it. programs that do not use the
 * framework never get that window and keep imm32's input method messages.
 */
#include <mime/nt.h>
#include <mime/abi.h>
#include <mime/ntuser.h>
#include "msctf_private.h"

#define WM_POST_LOCK (WM_USER + 2)
#define WM_UPDATE_AREA (WM_USER + 3)
#define IME_MAX 256

typedef struct SERVICE {
    ITfCompositionSink Sink;
    LONG Refs;
    TM *Tm;
    HWND Window;
    CTX *Context;
    ITfComposition *Composition;
    LONG Cursor;
    BOOL AreaPending;
} SERVICE;

typedef struct EVENT {
    UINT Kind;
    ULONG Count;
    LONG Begin, End;
    WCHAR Text[IME_MAX];
} EVENT;

/* ------------------------------------------------------------------ the composition sink */

static SERVICE *from_sink(ITfCompositionSink *This)
{
    return CONTAINING_RECORD(This, SERVICE, Sink);
}

static HRESULT STDMETHODCALLTYPE sink_query(ITfCompositionSink *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfCompositionSink)) {
        *object = This;
        InterlockedIncrement(&from_sink(This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE sink_add_ref(ITfCompositionSink *This)
{
    return InterlockedIncrement(&from_sink(This)->Refs);
}

static void service_free(SERVICE *svc);

static ULONG STDMETHODCALLTYPE sink_release(ITfCompositionSink *This)
{
    SERVICE *svc = from_sink(This);
    LONG refs = InterlockedDecrement(&svc->Refs);
    if (!refs)
        service_free(svc);
    return refs;
}

static void forget_composition(SERVICE *svc)
{
    ITfComposition *comp = svc->Composition;
    CTX *ctx = svc->Context;
    svc->Composition = nullptr;
    svc->Context = nullptr;
    svc->Cursor = 0;
    if (comp)
        comp->lpVtbl->Release(comp);
    if (ctx)
        CtxRelease(ctx);
}

/* the program ended the composition itself, such as on a focus change, so the desktop input method drops it. */
static HRESULT STDMETHODCALLTYPE sink_terminated(ITfCompositionSink *This, TfEditCookie cookie, ITfComposition *comp)
{
    SERVICE *svc = from_sink(This);
    CTF_TRACE("msctf: composition terminated by the program\n");
    if (svc->Composition == comp) {
        forget_composition(svc);
        NtUserMimeIme(MIME_IME_RESET, 0, 0, nullptr, 0);
    }
    return S_OK;
}

static const ITfCompositionSinkVtbl sink_vtbl = {sink_query, sink_add_ref, sink_release, sink_terminated};

/* ------------------------------------------------------------------ editing the document */

/* tells the desktop input method where text goes: the composition, else the selection. */
static void update_area(SERVICE *svc, CTX *ctx)
{
    LONG start, end, caret;
    RECT all = {}, at = {};
    HWND window;
    if (CompExtent(ctx, &start, &end)) {
        caret = start + (svc->Context == ctx ? svc->Cursor : 0);
        if (caret > end)
            caret = end;
    } else if (SUCCEEDED(CtxStoreGetSelection(ctx, &start, &end))) {
        caret = end;
    } else {
        return;
    }
    /* some stores give an empty range no extent, so the character before it stands in. */
    HRESULT hr = CtxStoreTextExt(ctx, start, end, &all);
    if (FAILED(hr) && start == end && start > 0) {
        hr = CtxStoreTextExt(ctx, start - 1, start, &all);
        all.left = all.right;
    }
    if (FAILED(hr) || !CtxStoreWindow(ctx, &window))
        return;
    if (FAILED(CtxStoreTextExt(ctx, caret, caret, &at)))
        at = all;
    POINT top_left = {all.left, all.top}, bottom_right = {all.right, all.bottom}, caret_at = {at.left, at.top};
    ScreenToClient(window, &top_left);
    ScreenToClient(window, &bottom_right);
    ScreenToClient(window, &caret_at);
    LONG area[5] = {top_left.x, top_left.y, bottom_right.x, bottom_right.y, caret_at.x};
    NtUserMimeIme(MIME_IME_AREA, (ULONG_PTR)window, 0, (PUCHAR)area, sizeof(area));
}

static TF_DA_ATTR_INFO attribute_kind(const EVENT *ev, LONG index)
{
    if (ev->Begin >= 0 && ev->End > ev->Begin && ev->End <= (LONG)ev->Count)
        return index >= ev->Begin && index < ev->End ? TF_ATTR_TARGET_CONVERTED : TF_ATTR_CONVERTED;
    return TF_ATTR_INPUT;
}

/* marks each run of the composed text with its display attribute. */
static void mark_text(CTX *ctx, const EVENT *ev, LONG start)
{
    LONG run = 0;
    while (run < (LONG)ev->Count) {
        TF_DA_ATTR_INFO kind = attribute_kind(ev, run);
        LONG next = run + 1;
        while (next < (LONG)ev->Count && attribute_kind(ev, next) == kind)
            next++;
        GUID guid;
        if (CtfDisplayAttributeGuid(kind, &guid)) {
            VARIANT value;
            VariantInit(&value);
            V_VT(&value) = VT_I4;
            V_I4(&value) = (LONG)CtfGuidAtom(&guid);
            PropsSet(ctx, &GUID_PROP_ATTRIBUTE, start + run, start + next, &value);
        }
        run = next;
    }
}

static HRESULT select_at(CTX *ctx, TfEditCookie cookie, LONG position)
{
    TF_SELECTION selection = {};
    HRESULT hr = RngCreate(ctx, position, position, &selection.range);
    if (FAILED(hr))
        return hr;
    selection.style.ase = TF_AE_NONE;
    hr = ctx->Context.lpVtbl->SetSelection(&ctx->Context, cookie, 1, &selection);
    selection.range->lpVtbl->Release(selection.range);
    return hr;
}

static HRESULT composition_range(SERVICE *svc, ITfRange **range)
{
    return svc->Composition->lpVtbl->GetRange(svc->Composition, range);
}

/* new preedit text replaces the composition's content. */
static HRESULT do_preedit(SERVICE *svc, CTX *ctx, TfEditCookie cookie, const EVENT *ev)
{
    HRESULT hr;
    if (!svc->Composition) {
        ITfRange *at = nullptr;
        ITfComposition *comp = nullptr;
        hr = ctx->Insert.lpVtbl->InsertTextAtSelection(&ctx->Insert, cookie, TF_IAS_QUERYONLY, nullptr, 0, &at);
        if (FAILED(hr))
            return hr;
        hr = CompStart(ctx, cookie, at, &svc->Sink, &g_ctf_service_clsid, &comp);
        at->lpVtbl->Release(at);
        if (FAILED(hr))
            return hr;
        if (!comp)
            return TF_E_COMPOSITION_REJECTED;
        svc->Composition = comp;
        svc->Context = ctx;
        CtxAddRef(ctx);
    }
    ITfRange *range;
    hr = composition_range(svc, &range);
    if (FAILED(hr))
        return hr;
    hr = range->lpVtbl->SetText(range, cookie, 0, ev->Text, (LONG)ev->Count);
    if (SUCCEEDED(hr)) {
        LONG start, length;
        ((ITfRangeACP *)range)->lpVtbl->GetExtent((ITfRangeACP *)range, &start, &length);
        VARIANT one;
        VariantInit(&one);
        V_VT(&one) = VT_I4;
        V_I4(&one) = 1;
        PropsSet(ctx, &GUID_PROP_COMPOSING, start, start + length, &one);
        PropsSet(ctx, &GUID_PROP_ATTRIBUTE, start, start + length, nullptr);
        mark_text(ctx, ev, start);
        /* the caret goes at the start of the converted part, else at the end the input method gives. */
        LONG cursor = ev->Begin >= 0 && ev->End > ev->Begin ? ev->Begin : ev->End >= 0 ? ev->End : (LONG)ev->Count;
        if (cursor > (LONG)ev->Count)
            cursor = (LONG)ev->Count;
        svc->Cursor = cursor;
        select_at(ctx, cookie, start + cursor);
    }
    range->lpVtbl->Release(range);
    return hr;
}

/* ends the composition, replacing its text with `text`, which may be empty. */
static HRESULT do_end(SERVICE *svc, CTX *ctx, TfEditCookie cookie, const WCHAR *text, ULONG count)
{
    HRESULT hr = S_OK;
    if (!svc->Composition) {
        if (!count)
            return S_OK;
        ITfRange *range;
        hr = ctx->Insert.lpVtbl->InsertTextAtSelection(&ctx->Insert, cookie, TF_IAS_NO_DEFAULT_COMPOSITION, text,
                                                       (LONG)count, &range);
        if (SUCCEEDED(hr))
            range->lpVtbl->Release(range);
        return hr;
    }
    ITfRange *range;
    hr = composition_range(svc, &range);
    if (FAILED(hr))
        return hr;
    hr = range->lpVtbl->SetText(range, cookie, 0, text, (LONG)count);
    if (SUCCEEDED(hr)) {
        LONG start, length;
        ((ITfRangeACP *)range)->lpVtbl->GetExtent((ITfRangeACP *)range, &start, &length);
        PropsSet(ctx, &GUID_PROP_ATTRIBUTE, start, start + length, nullptr);
        select_at(ctx, cookie, start + length);
        ITfComposition *comp = svc->Composition;
        comp->lpVtbl->AddRef(comp);
        forget_composition(svc);
        hr = comp->lpVtbl->EndComposition(comp, cookie);
        comp->lpVtbl->Release(comp);
    }
    range->lpVtbl->Release(range);
    return hr;
}

typedef struct SESSION {
    ITfEditSession Iface;
    LONG Refs;
    SERVICE *Service;
    CTX *Context;
    EVENT Event;
    BOOL AreaOnly;
} SESSION;

static HRESULT STDMETHODCALLTYPE session_query(ITfEditSession *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditSession)) {
        *object = This;
        InterlockedIncrement(&((SESSION *)This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE session_add_ref(ITfEditSession *This)
{
    return InterlockedIncrement(&((SESSION *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE session_release(ITfEditSession *This)
{
    SESSION *session = (SESSION *)This;
    LONG refs = InterlockedDecrement(&session->Refs);
    if (!refs) {
        CtxRelease(session->Context);
        sink_release(&session->Service->Sink);
        CtfFree(session);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE session_do(ITfEditSession *This, TfEditCookie cookie)
{
    SESSION *session = (SESSION *)This;
    SERVICE *svc = session->Service;
    CTX *ctx = session->Context;
    HRESULT hr = S_OK;
    if (!session->AreaOnly) {
        const EVENT *ev = &session->Event;
        switch (ev->Kind) {
        case MIME_IME_PREEDIT:
            hr = ev->Count ? do_preedit(svc, ctx, cookie, ev) : do_end(svc, ctx, cookie, L"", 0);
            break;
        case MIME_IME_COMMIT:
            hr = do_end(svc, ctx, cookie, ev->Text, ev->Count);
            break;
        case MIME_IME_CANCELLED:
            hr = do_end(svc, ctx, cookie, L"", 0);
            break;
        }
        CTF_TRACE("msctf: host event %u of %lu characters -> %lx\n", ev->Kind, ev->Count, hr);
    }
    if (SUCCEEDED(hr) && svc->Context == ctx)
        update_area(svc, ctx);
    else if (SUCCEEDED(hr) && session->AreaOnly)
        update_area(svc, ctx);
    return hr;
}

static const ITfEditSessionVtbl session_vtbl = {session_query, session_add_ref, session_release, session_do};

static SESSION *session_new(SERVICE *svc, CTX *ctx)
{
    SESSION *session = CtfAlloc(sizeof(*session));
    if (!session)
        return nullptr;
    session->Iface.lpVtbl = &session_vtbl;
    session->Refs = 1;
    session->Service = svc;
    InterlockedIncrement(&svc->Refs);
    session->Context = ctx;
    CtxAddRef(ctx);
    return session;
}

/* ------------------------------------------------------------------ the sink window */

static LRESULT host_event(SERVICE *svc, UINT kind, LPARAM selection)
{
    DOC *doc = TmFocusDoc(svc->Tm);
    CTX *ctx = doc ? DocTopContext(doc) : nullptr;
    if (!ctx || !ctx->Store)
        return 0;
    /* a composition does not follow the focus to another document. */
    if (svc->Context && svc->Context != ctx) {
        CTX *old = svc->Context;
        CtxAddRef(old);
        CompTerminateAll(old, nullptr);
        forget_composition(svc);
        CtxRelease(old);
        NtUserMimeIme(MIME_IME_RESET, 0, 0, nullptr, 0);
    }
    SESSION *session = session_new(svc, ctx);
    if (!session)
        return 0;
    EVENT *ev = &session->Event;
    ev->Kind = kind;
    ev->Begin = (LONG)(selection & 0xFFFF) - 1;
    ev->End = (LONG)((selection >> 16) & 0xFFFF) - 1;
    ULONG bytes = NtUserMimeIme(MIME_IME_PENDING, 0, 0, (PUCHAR)ev->Text, sizeof(ev->Text));
    ev->Count = bytes / sizeof(WCHAR);
    HRESULT result = E_FAIL;
    HRESULT hr = CtxRequestSession(ctx, TmClientId(svc->Tm), &session->Iface, TF_ES_READWRITE, &result);
    session->Iface.lpVtbl->Release(&session->Iface);
    CTF_TRACE("msctf: host event %u session request %lx result %lx\n", kind, hr, result);
    return SUCCEEDED(hr) && (SUCCEEDED(result) || result == TS_S_ASYNC);
}

static void area_request(SERVICE *svc)
{
    svc->AreaPending = FALSE;
    DOC *doc = TmFocusDoc(svc->Tm);
    CTX *ctx = doc ? DocTopContext(doc) : nullptr;
    if (!ctx || !ctx->Store || svc->Context)
        return;
    SESSION *session = session_new(svc, ctx);
    if (!session)
        return;
    session->AreaOnly = TRUE;
    HRESULT result = E_FAIL;
    CtxRequestSession(ctx, TmClientId(svc->Tm), &session->Iface, TF_ES_READ, &result);
    session->Iface.lpVtbl->Release(&session->Iface);
}

static LRESULT CALLBACK service_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    SERVICE *svc = (SERVICE *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case MIME_TSF_MESSAGE:
        return svc ? host_event(svc, (UINT)wparam, lparam) : 0;
    case WM_POST_LOCK: {
        CTX *ctx = (CTX *)lparam;
        CtxRunPostponed(ctx);
        CtxRelease(ctx);
        return 0;
    }
    case WM_UPDATE_AREA:
        if (svc)
            area_request(svc);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static BOOL register_class(void)
{
    static LONG registered;
    WNDCLASSW wc = {};
    if (registered)
        return TRUE;
    wc.lpfnWndProc = service_proc;
    wc.hInstance = g_ctf_module;
    wc.lpszClassName = L"MimeTsfSink";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
    registered = 1;
    return TRUE;
}

void ServiceThreadAttach(TM *tm)
{
    SERVICE *svc;
    if (TmService(tm) || !register_class())
        return;
    svc = CtfAlloc(sizeof(*svc));
    if (!svc)
        return;
    svc->Sink.lpVtbl = &sink_vtbl;
    svc->Refs = 1;
    svc->Tm = tm;
    svc->Window = CreateWindowExW(0, L"MimeTsfSink", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_ctf_module, nullptr);
    if (!svc->Window) {
        CtfFree(svc);
        return;
    }
    SetWindowLongPtrW(svc->Window, GWLP_USERDATA, (LONG_PTR)svc);
    TmSetService(tm, svc);
    NtUserMimeIme(MIME_IME_TSF, (ULONG_PTR)svc->Window, 0, nullptr, 0);
    CTF_TRACE("msctf: text service window %p\n", svc->Window);
}

void ServiceThreadDetach(TM *tm)
{
    SERVICE *svc = TmService(tm);
    if (!svc)
        return;
    NtUserMimeIme(MIME_IME_TSF, 0, 0, nullptr, 0);
    if (svc->Context) {
        CTX *ctx = svc->Context;
        CtxAddRef(ctx);
        CompTerminateAll(ctx, nullptr);
        forget_composition(svc);
        CtxRelease(ctx);
        NtUserMimeIme(MIME_IME_RESET, 0, 0, nullptr, 0);
    }
    TmSetService(tm, nullptr);
    SetWindowLongPtrW(svc->Window, GWLP_USERDATA, 0);
    DestroyWindow(svc->Window);
    svc->Window = nullptr;
    svc->Tm = nullptr;
    sink_release(&svc->Sink);
}

static void service_free(SERVICE *svc)
{
    CtfFree(svc);
}

void ServiceDocumentChanged(TM *tm)
{
    SERVICE *svc = TmService(tm);
    if (!svc || !svc->Context)
        return;
    DOC *doc = TmFocusDoc(tm);
    if (doc && DocTopContext(doc) == svc->Context)
        return;
    CTX *ctx = svc->Context;
    CtxAddRef(ctx);
    CompTerminateAll(ctx, nullptr);
    forget_composition(svc);
    CtxRelease(ctx);
    NtUserMimeIme(MIME_IME_RESET, 0, 0, nullptr, 0);
}

void ServiceStoreNotify(CTX *ctx, BOOL layout)
{
    SERVICE *svc = ctx->Tm ? TmService(ctx->Tm) : nullptr;
    if (!svc || svc->AreaPending)
        return;
    svc->AreaPending = PostMessageW(svc->Window, WM_UPDATE_AREA, 0, 0);
}

BOOL ServicePostLock(CTX *ctx)
{
    SERVICE *svc = ctx->Tm ? TmService(ctx->Tm) : nullptr;
    if (!svc)
        return FALSE;
    CtxAddRef(ctx);
    if (!PostMessageW(svc->Window, WM_POST_LOCK, 0, (LPARAM)ctx)) {
        CtxRelease(ctx);
        return FALSE;
    }
    return TRUE;
}
