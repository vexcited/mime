/*
 * the scripting engine object: IActiveScript and the parsers over it, the states a host moves it through, and how
 * errors reach the site.
 */
#include "js.h"

enum { PARSE_PROGRAM, PARSE_EVAL, PARSE_FUNCTION };

HINSTANCE g_js_module;
LONG g_js_objects;

#define ENGINE_OF(field, ptr) ((Engine *)((BYTE *)(ptr) - offsetof(Engine, field)))

static void engine_destroy(Engine *e);

static PCWSTR const g_src_runtime = L"Microsoft JScript runtime error";
static PCWSTR const g_src_compile = L"Microsoft JScript compilation error";

/* ------------------------------------------------------------------ errors reaching the site */

typedef struct ScriptError {
    IActiveScriptError Error;
    LONG Refs;
    Fault F;
} ScriptError;

static HRESULT STDMETHODCALLTYPE error_qi(IActiveScriptError *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IActiveScriptError) ||
        (sizeof(void *) == 8 && IsEqualIID(riid, &IID_IActiveScriptError64))) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE error_addref(IActiveScriptError *This)
{
    return InterlockedIncrement(&((ScriptError *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE error_release(IActiveScriptError *This)
{
    ScriptError *e = (ScriptError *)This;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        SysFreeString(e->F.Source);
        SysFreeString(e->F.Description);
        SysFreeString(e->F.HelpFile);
        js_free(e->F.Text);
        js_free(e);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE error_info(IActiveScriptError *This, EXCEPINFO *info)
{
    ScriptError *e = (ScriptError *)This;
    if (!info)
        return E_POINTER;
    memset(info, 0, sizeof(*info));
    info->scode = e->F.Scode;
    if (e->F.Source)
        info->bstrSource = SysAllocStringLen(e->F.Source, SysStringLen(e->F.Source));
    if (e->F.Description)
        info->bstrDescription = SysAllocStringLen(e->F.Description, SysStringLen(e->F.Description));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE error_position(IActiveScriptError *This, DWORD *cookie, ULONG *line, LONG *character)
{
    ScriptError *e = (ScriptError *)This;
    if (cookie) {
        if (sizeof(void *) == 8)
            *(DWORD_PTR *)cookie = e->F.Cookie;
        else
            *cookie = (DWORD)e->F.Cookie;
    }
    if (line)
        *line = e->F.Line;
    if (character)
        *character = (LONG)e->F.Col;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE error_line(IActiveScriptError *This, BSTR *text)
{
    ScriptError *e = (ScriptError *)This;
    if (!text)
        return E_POINTER;
    *text = nullptr;
    if (!e->F.Text)
        return E_FAIL;
    *text = SysAllocString(e->F.Text);
    return S_OK;
}

static const IActiveScriptErrorVtbl g_error_vtbl = {error_qi, error_addref, error_release, error_info, error_position,
                                                    error_line};

void fault_clear(Engine *e)
{
    SysFreeString(e->Fault.Source);
    SysFreeString(e->Fault.Description);
    SysFreeString(e->Fault.HelpFile);
    js_free(e->Fault.Text);
    memset(&e->Fault, 0, sizeof(e->Fault));
}

void fault_from_syntax(Engine *e, HRESULT scode, ULONG line, ULONG col, const WCHAR *text_line, DWORD_PTR cookie)
{
    fault_clear(e);
    e->Fault.Set = TRUE;
    e->Fault.Scode = scode;
    e->Fault.Compile = TRUE;
    e->Fault.Source = SysAllocString(g_src_compile);
    e->Fault.Description = SysAllocString(error_text(scode & 0xFFFF));
    e->Fault.Line = line;
    e->Fault.Col = col;
    e->Fault.Text = text_line ? js_wcsdup(text_line) : nullptr;
    e->Fault.Cookie = cookie;
}

/* the fault of the exception that ended the script. */
void fault_from_exception(Engine *e)
{
    fault_clear(e);
    e->Fault.Set = TRUE;
    Val x = e->Exception;
    HRESULT scode = (HRESULT)0x800A139E;
    BSTR source = nullptr, description = nullptr;
    BOOL known_source = TRUE;
    if (e->ErrFromHost) {
        scode = e->ErrScode;
        source = e->ErrSource ? SysAllocString(e->ErrSource) : nullptr;
        description = e->ErrDescription ? SysAllocString(e->ErrDescription) : nullptr;
        known_source = FALSE;
    } else if (x.t == T_OBJ && x.u.o->cls == C_ERROR) {
        Val n, d;
        BOOL has_number = FALSE;
        obj_get(e, x.u.o, str_intern_ascii(e, "number"), &n);
        obj_get(e, x.u.o, str_intern_ascii(e, "description"), &d);
        if (n.t == T_NUM && !is_nan(n.u.n)) {
            ULONG v = (ULONG)d_to_int32(n.u.n);
            if (n.u.n >= 2147483648.0 && n.u.n < 4294967296.0)
                v = (ULONG)(LONGLONG)n.u.n;
            has_number = TRUE;
            if ((v >> 16) == 0x800A)
                scode = (HRESULT)v;
            else {
                scode = (HRESULT)0x80004005;
                known_source = FALSE;
            }
        }
        if (d.t == T_STR)
            description = SysAllocStringLen(d.u.s->s, d.u.s->len);
        else if (!has_number)
            description = SysAllocString(error_text(5022));
        else
            description = SysAllocString(L"");
        if (x.u.o->flags & OF_COMPILE)
            source = SysAllocString(g_src_compile);
        else if (known_source)
            source = SysAllocString(g_src_runtime);
    } else {
        description = SysAllocString(error_text(5022));
        source = SysAllocString(g_src_runtime);
    }
    if (!source && known_source)
        source = SysAllocString(g_src_runtime);
    e->Fault.Scode = scode;
    e->Fault.Source = source;
    e->Fault.Description = description;
    e->Fault.Line = e->ErrLine;
    e->Fault.Col = e->ErrCol;
    e->Fault.Cookie = e->ErrCookie;
}

int fill_excepinfo_from_exception(Engine *e, EXCEPINFO *ei)
{
    fault_from_exception(e);
    memset(ei, 0, sizeof(*ei));
    ei->scode = e->Fault.Scode;
    ei->bstrSource = SysAllocString(e->Fault.Source);
    ei->bstrDescription = SysAllocString(e->Fault.Description);
    fault_clear(e);
    return RC_OK;
}

/* tells the site about the fault in progress, then clears it. */
void engine_report(Engine *e)
{
    if (!e->Fault.Set) {
        if (e->State == SCRIPTSTATE_CLOSED)
            return;
        fault_from_exception(e);
    }
    if (e->Site) {
        ScriptError *err = js_alloc(sizeof(*err));
        err->Error.lpVtbl = &g_error_vtbl;
        err->Refs = 1;
        err->F = e->Fault;
        err->F.Source = e->Fault.Source ? SysAllocStringLen(e->Fault.Source, SysStringLen(e->Fault.Source)) : nullptr;
        err->F.Description = e->Fault.Description ? SysAllocStringLen(e->Fault.Description, SysStringLen(e->Fault.Description)) : nullptr;
        err->F.HelpFile = nullptr;
        err->F.Text = e->Fault.Text ? js_wcsdup(e->Fault.Text) : nullptr;
        e->Site->lpVtbl->OnScriptError(e->Site, &err->Error);
        err->Error.lpVtbl->Release(&err->Error);
    }
    fault_clear(e);
}

static void stack_base(Engine *e)
{
    SIZE_T base;
#ifdef _WIN64
    __asm__ volatile("movq %%gs:8, %0" : "=r"(base));
#else
    __asm__ volatile("movl %%fs:4, %0" : "=r"(base));
#endif
    e->StackBase = (void *)base;
}

HRESULT engine_enter(Engine *e)
{
    if (e->NestedRun++ == 0) {
        e->Interrupted = FALSE;
        e->Abort = FALSE;
        e->EnterNotified = FALSE;
        stack_base(e);
    }
    return S_OK;
}

/* the site hears of the script at the first code that runs. */
void engine_note_code(Engine *e)
{
    if (!e->EnterNotified && e->NestedRun) {
        e->EnterNotified = TRUE;
        if (e->Site)
            e->Site->lpVtbl->OnEnterScript(e->Site);
    }
}

void engine_leave(Engine *e)
{
    if (--e->NestedRun == 0 && e->EnterNotified) {
        e->EnterNotified = FALSE;
        if (e->Site)
            e->Site->lpVtbl->OnLeaveScript(e->Site);
    }
}

static void notify(Engine *e, SCRIPTSTATE state)
{
    e->State = state;
    if (e->Site)
        e->Site->lpVtbl->OnStateChange(e->Site, state);
}

/* ------------------------------------------------------------------ running units */

static void free_units(Unit **list)
{
    while (*list) {
        Unit *u = *list;
        *list = u->next;
        unit_free(u);
    }
}

/* the failed run of a unit is told to the site. */
static int run_and_report(Engine *e, Unit *u)
{
    Val result;
    e->ErrCookie = u->cookie;
    e->Exception = UNDEF;
    int rc = run_program(e, u, nullptr, &result, FALSE);
    if (rc == RC_THROW && e->Abort) {
        e->Abort = FALSE;
        return RC_THROW;
    }
    if (rc == RC_THROW) {
        fault_from_exception(e);
        engine_report(e);
        return RC_THROW;
    }
    return RC_OK;
}

int engine_run_pending(Engine *e)
{
    int rc = RC_OK;
    engine_enter(e);
    while (e->Pending && rc == RC_OK) {
        Unit *u = e->Pending;
        e->Pending = u->next;
        u->next = e->Units;
        e->Units = u;
        rc = run_and_report(e, u);
    }
    engine_leave(e);
    return rc;
}

/* ------------------------------------------------------------------ IActiveScript */

static HRESULT STDMETHODCALLTYPE script_qi(IActiveScript *This, REFIID riid, void **object)
{
    Engine *e = ENGINE_OF(Script, This);
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IActiveScript))
        *object = &e->Script;
    else if (IsEqualIID(riid, sizeof(void *) == 8 ? &IID_IActiveScriptParse64 : &IID_IActiveScriptParse))
        *object = &e->Parse;
    else if (IsEqualIID(riid, sizeof(void *) == 8 ? &IID_IActiveScriptParseProcedure2_64 : &IID_IActiveScriptParseProcedure2))
        *object = &e->Procedure;
    else if (IsEqualIID(riid, &IID_IActiveScriptProperty))
        *object = &e->Property;
    else if (IsEqualIID(riid, &IID_IObjectSafetyScript))
        *object = &e->Safety;
    else if (IsEqualIID(riid, &IID_IActiveScriptGarbageCollector))
        *object = &e->Collector;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&e->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE script_addref(IActiveScript *This)
{
    return InterlockedIncrement(&ENGINE_OF(Script, This)->Refs);
}

static ULONG STDMETHODCALLTYPE script_release(IActiveScript *This)
{
    Engine *e = ENGINE_OF(Script, This);
    LONG refs = InterlockedDecrement(&e->Refs);
    if (refs == 0)
        engine_destroy(e);
    return refs;
}

static HRESULT STDMETHODCALLTYPE script_set_site(IActiveScript *This, IActiveScriptSite *site)
{
    Engine *e = ENGINE_OF(Script, This);
    if (!site)
        return E_POINTER;
    if (e->Site || e->State == SCRIPTSTATE_CLOSED)
        return E_UNEXPECTED;
    e->Site = site;
    site->lpVtbl->AddRef(site);
    LCID lcid;
    if (SUCCEEDED(site->lpVtbl->GetLCID(site, &lcid)))
        e->Lcid = lcid;
    else
        e->Lcid = GetUserDefaultLCID();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE script_get_site(IActiveScript *This, REFIID riid, void **site)
{
    Engine *e = ENGINE_OF(Script, This);
    if (!site)
        return E_POINTER;
    *site = nullptr;
    if (!e->Site)
        return E_FAIL;
    return e->Site->lpVtbl->QueryInterface(e->Site, riid, site);
}

static void notify_all(Engine *e, const int *list)
{
    for (; *list >= 0; list++)
        notify(e, (SCRIPTSTATE)*list);
}

static HRESULT STDMETHODCALLTYPE script_set_state(IActiveScript *This, SCRIPTSTATE state)
{
    Engine *e = ENGINE_OF(Script, This);
    SCRIPTSTATE c = e->State;
    if (c == SCRIPTSTATE_CLOSED || c == SCRIPTSTATE_UNINITIALIZED || !e->Site)
        return E_UNEXPECTED;
    static const int none[] = {-1};
    static const int to_started_from_connected[] = {3, 5, 1, -1};
    static const int to_init_from_connected[] = {3, 5, 5, -1};
    static const int to_uninit[] = {3, 5, 0, -1};
    switch (state) {
    case SCRIPTSTATE_STARTED:
    case SCRIPTSTATE_CONNECTED:
        InterlockedIncrement(&e->Refs);
        if (c == SCRIPTSTATE_CONNECTED && state == SCRIPTSTATE_STARTED)
            notify_all(e, to_started_from_connected);
        else if (c == state)
            ;
        else {
            if (c == SCRIPTSTATE_DISCONNECTED && state == SCRIPTSTATE_STARTED)
                notify(e, SCRIPTSTATE_INITIALIZED);
            engine_run_pending(e);
            notify(e, state);
        }
        script_release(This);
        return S_OK;
    case SCRIPTSTATE_DISCONNECTED:
        if (c == SCRIPTSTATE_STARTED || c == SCRIPTSTATE_INITIALIZED)
            return E_UNEXPECTED;
        if (c == SCRIPTSTATE_CONNECTED)
            notify(e, state);
        return S_OK;
    case SCRIPTSTATE_INITIALIZED:
        if (c == SCRIPTSTATE_CONNECTED)
            notify_all(e, to_init_from_connected);
        else if (c != SCRIPTSTATE_INITIALIZED)
            notify(e, state);
        return S_OK;
    case SCRIPTSTATE_CLOSED:
        engine_close(e);
        return S_OK;
    case SCRIPTSTATE_UNINITIALIZED:
        if (c == SCRIPTSTATE_CONNECTED || c == SCRIPTSTATE_DISCONNECTED || c == SCRIPTSTATE_STARTED || c == SCRIPTSTATE_INITIALIZED)
            notify_all(e, to_uninit);
        (void)none;
        return S_OK;
    default:
        return E_UNEXPECTED;
    }
}

static HRESULT STDMETHODCALLTYPE script_get_state(IActiveScript *This, SCRIPTSTATE *state)
{
    if (!state)
        return E_POINTER;
    *state = ENGINE_OF(Script, This)->State;
    return S_OK;
}

void engine_close(Engine *e)
{
    if (e->State == SCRIPTSTATE_CLOSED)
        return;
    e->Closing = TRUE;
    SCRIPTSTATE c = e->State;
    if (c == SCRIPTSTATE_CONNECTED)
        notify(e, SCRIPTSTATE_DISCONNECTED);
    if (c == SCRIPTSTATE_CONNECTED || c == SCRIPTSTATE_DISCONNECTED)
        notify(e, SCRIPTSTATE_INITIALIZED);
    /* the items go, which releases the objects the script holds. */
    for (NamedItem *it = e->Items; it; it = it->next)
        it->value = UNDEF;
    e->GlobalThis = UNDEF;
    gc_collect(e);
    if (e->PollSite && e->PollSite != (IActiveScriptSiteInterruptPoll *)(SIZE_T)1)
        e->PollSite->lpVtbl->Release(e->PollSite);
    e->PollSite = nullptr;
    if (e->Site) {
        IActiveScriptSite *site = e->Site;
        if (c == SCRIPTSTATE_UNINITIALIZED)
            e->State = SCRIPTSTATE_CLOSED;
        else
            notify(e, SCRIPTSTATE_CLOSED);
        e->Site = nullptr;
        site->lpVtbl->Release(site);
    } else
        e->State = SCRIPTSTATE_CLOSED;
}

static HRESULT STDMETHODCALLTYPE script_close(IActiveScript *This)
{
    Engine *e = ENGINE_OF(Script, This);
    engine_close(e);
    return S_OK;
}

int item_load(Engine *e, NamedItem *it)
{
    if (!e->Site)
        return RC_OK;
    IUnknown *unk = nullptr;
    HRESULT hr = e->Site->lpVtbl->GetItemInfo(e->Site, it->name->s, SCRIPTINFO_IUNKNOWN, &unk, nullptr);
    if (FAILED(hr) || !unk) {
        it->flags |= 0x80000000;
        return RC_OK;
    }
    IDispatch *d = nullptr;
    unk->lpVtbl->QueryInterface(unk, &IID_IDispatch, (void **)&d);
    unk->lpVtbl->Release(unk);
    if (!d) {
        it->flags |= 0x80000000;
        return RC_OK;
    }
    it->value = v_obj(host_wrap(e, d));
    d->lpVtbl->Release(d);
    if ((it->flags & SCRIPTITEM_GLOBALMEMBERS) && e->GlobalThis.t != T_OBJ)
        e->GlobalThis = it->value;
    return RC_OK;
}

int lookup_item_by_name(Engine *e, Str *name, Val *out, BOOL *found)
{
    *found = FALSE;
    for (NamedItem *it = e->Items; it; it = it->next)
        if ((it->flags & SCRIPTITEM_ISVISIBLE) && it->name->len == name->len && !_wcsicmp(it->name->s, name->s)) {
            if (it->value.t == T_UNDEF && !(it->flags & 0x80000000))
                item_load(e, it);
            if (it->value.t == T_OBJ) {
                *out = it->value;
                *found = TRUE;
            }
            break;
        }
    return RC_OK;
}

static HRESULT STDMETHODCALLTYPE script_add_item(IActiveScript *This, LPCOLESTR name, DWORD flags)
{
    Engine *e = ENGINE_OF(Script, This);
    if (!name || !*name)
        return E_INVALIDARG;
    if (e->State == SCRIPTSTATE_UNINITIALIZED || e->State == SCRIPTSTATE_CLOSED || !e->Site)
        return E_UNEXPECTED;
    if (flags & ~SCRIPTITEM_ALL_FLAGS)
        return E_INVALIDARG;
    NamedItem *it = js_alloc(sizeof(NamedItem));
    BOOL saved = e->NoGc;
    e->NoGc = TRUE;
    it->name = str_intern_z(e, name);
    e->NoGc = saved;
    it->flags = flags;
    it->next = nullptr;
    NamedItem **tail = &e->Items;
    while (*tail)
        tail = &(*tail)->next;
    *tail = it;
    if (flags & (SCRIPTITEM_ISVISIBLE | SCRIPTITEM_GLOBALMEMBERS))
        item_load(e, it);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE script_add_typelib(IActiveScript *This, REFGUID lib, DWORD major, DWORD minor, DWORD flags)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE script_get_dispatch(IActiveScript *This, LPCOLESTR item, IDispatch **dispatch)
{
    Engine *e = ENGINE_OF(Script, This);
    if (!dispatch)
        return E_POINTER;
    *dispatch = nullptr;
    if (e->State == SCRIPTSTATE_CLOSED || e->State == SCRIPTSTATE_UNINITIALIZED)
        return E_UNEXPECTED;
    *dispatch = script_dispatch_create(e);
    return *dispatch ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE script_thread_id(IActiveScript *This, SCRIPTTHREADID *id)
{
    if (!id)
        return E_POINTER;
    *id = GetCurrentThreadId();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE script_thread_of(IActiveScript *This, DWORD thread, SCRIPTTHREADID *id)
{
    if (!id)
        return E_POINTER;
    *id = thread;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE script_thread_state(IActiveScript *This, SCRIPTTHREADID id, SCRIPTTHREADSTATE *state)
{
    Engine *e = ENGINE_OF(Script, This);
    if (!state)
        return E_POINTER;
    *state = e->NestedRun ? SCRIPTTHREADSTATE_RUNNING : SCRIPTTHREADSTATE_NOTINSCRIPT;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE script_interrupt(IActiveScript *This, SCRIPTTHREADID id, const EXCEPINFO *info, DWORD flags)
{
    if (flags & ~SCRIPTINTERRUPT_ALL_FLAGS)
        return E_INVALIDARG;
    *(volatile BOOL *)&ENGINE_OF(Script, This)->Interrupted = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE script_clone(IActiveScript *This, IActiveScript **clone)
{
    if (clone)
        *clone = nullptr;
    return E_NOTIMPL;
}

static const IActiveScriptVtbl g_script_vtbl = {
    script_qi, script_addref, script_release, script_set_site, script_get_site, script_set_state, script_get_state,
    script_close, script_add_item, script_add_typelib, script_get_dispatch, script_thread_id, script_thread_of,
    script_thread_state, script_interrupt, script_clone,
};

/* ------------------------------------------------------------------ IActiveScriptParse */

static HRESULT STDMETHODCALLTYPE parse_qi(IActiveScriptParse *This, REFIID riid, void **object)
{
    return script_qi(&ENGINE_OF(Parse, This)->Script, riid, object);
}

static ULONG STDMETHODCALLTYPE parse_addref(IActiveScriptParse *This)
{
    return InterlockedIncrement(&ENGINE_OF(Parse, This)->Refs);
}

static ULONG STDMETHODCALLTYPE parse_release(IActiveScriptParse *This)
{
    return script_release(&ENGINE_OF(Parse, This)->Script);
}

static HRESULT STDMETHODCALLTYPE parse_init_new(IActiveScriptParse *This)
{
    Engine *e = ENGINE_OF(Parse, This);
    if (!e->Site || e->State != SCRIPTSTATE_UNINITIALIZED)
        return E_UNEXPECTED;
    notify(e, SCRIPTSTATE_INITIALIZED);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE parse_scriptlet(IActiveScriptParse *This, LPCOLESTR default_name, LPCOLESTR code,
                                                 LPCOLESTR item, LPCOLESTR subitem, LPCOLESTR event, LPCOLESTR delimiter,
                                                 DWORD_PTR cookie, ULONG line, DWORD flags, BSTR *name, EXCEPINFO *info)
{
    Engine *e = ENGINE_OF(Parse, This);
    if (name)
        *name = nullptr;
    if (!code)
        return E_POINTER;
    if (e->State == SCRIPTSTATE_CLOSED || e->State == SCRIPTSTATE_UNINITIALIZED)
        return E_UNEXPECTED;
    WCHAR fname[256];
    if (item && *item && event && *event)
        _snwprintf(fname, 256, L"%s%s%s_%s", item, subitem && *subitem ? L"_" : L"", subitem && *subitem ? subitem : L"", event);
    else
        _snwprintf(fname, 256, L"%s", default_name && *default_name ? default_name : L"anonymous");
    SIZE_T n = wcslen(code) + wcslen(fname) + 64;
    WCHAR *text = js_alloc(n * sizeof(WCHAR));
    _snwprintf(text, n, L"function %s() {\n%s\n}\n", fname, code);
    HRESULT hr = This->lpVtbl->ParseScriptText(This, text, nullptr, nullptr, nullptr, cookie, line, SCRIPTTEXT_ISVISIBLE, nullptr, info);
    js_free(text);
    if (SUCCEEDED(hr) && name)
        *name = SysAllocString(fname);
    return hr;
}

static HRESULT STDMETHODCALLTYPE parse_text(IActiveScriptParse *This, LPCOLESTR code, LPCOLESTR item, IUnknown *context,
                                            LPCOLESTR delimiter, DWORD_PTR cookie, ULONG line, DWORD flags,
                                            VARIANT *result, EXCEPINFO *info)
{
    Engine *e = ENGINE_OF(Parse, This);
    if (result)
        VariantInit(result);
    if (!code)
        return E_POINTER;
    if (e->State == SCRIPTSTATE_CLOSED || e->State == SCRIPTSTATE_UNINITIALIZED)
        return E_UNEXPECTED;
    BOOL expression = (flags & SCRIPTTEXT_ISEXPRESSION) != 0;
    InterlockedIncrement(&e->Refs);
    HRESULT hr = S_OK;
    ParseError err = {};
    e->NoGc = FALSE;
    stack_base(e);
    SIZE_T code_length = wcslen(code);
    WCHAR *decoded = e->Encoded ? script_decode(code, code_length, &code_length) : nullptr;
    Unit *unit = parse_unit(e, decoded ? decoded : code, code_length, line, cookie, expression ? PARSE_EVAL : PARSE_PROGRAM, &err);
    js_free(decoded);
    if (!unit) {
        fault_from_syntax(e, (HRESULT)(0x800A0000u | err.Code), err.Line, err.Col, err.LineText, cookie);
        js_free(err.LineText);
        engine_report(e);
        script_release(&e->Script);
        return SCRIPT_E_REPORTED;
    }
    if (expression) {
        engine_enter(e);
        unit->next = e->Units;
        e->Units = unit;
        Val v = UNDEF;
        e->ErrCookie = cookie;
        int rc = run_program(e, unit, nullptr, &v, TRUE);
        if (rc == RC_THROW) {
            fault_from_exception(e);
            engine_report(e);
            hr = SCRIPT_E_REPORTED;
        } else if (result)
            val_to_variant(e, &v, result);
        engine_leave(e);
        script_release(&e->Script);
        return hr;
    }
    if (e->State == SCRIPTSTATE_INITIALIZED || (flags & SCRIPTTEXT_DELAYEXECUTION)) {
        Unit **tail = &e->Pending;
        while (*tail)
            tail = &(*tail)->next;
        *tail = unit;
        unit->next = nullptr;
    } else {
        unit->next = e->Pending;
        e->Pending = unit;
        int rc = engine_run_pending(e);
        hr = rc == RC_OK ? S_OK : SCRIPT_E_REPORTED;
    }
    script_release(&e->Script);
    return hr;
}

static const IActiveScriptParseVtbl g_parse_vtbl = {parse_qi, parse_addref, parse_release, parse_init_new, parse_scriptlet,
                                                    parse_text};

/* ------------------------------------------------------------------ IActiveScriptParseProcedure2 */

static HRESULT STDMETHODCALLTYPE proc_qi(IActiveScriptParseProcedure2 *This, REFIID riid, void **object)
{
    return script_qi(&ENGINE_OF(Procedure, This)->Script, riid, object);
}

static ULONG STDMETHODCALLTYPE proc_addref(IActiveScriptParseProcedure2 *This)
{
    return InterlockedIncrement(&ENGINE_OF(Procedure, This)->Refs);
}

static ULONG STDMETHODCALLTYPE proc_release(IActiveScriptParseProcedure2 *This)
{
    return script_release(&ENGINE_OF(Procedure, This)->Script);
}

static HRESULT STDMETHODCALLTYPE proc_parse(IActiveScriptParseProcedure2 *This, LPCOLESTR code, LPCOLESTR params,
                                            LPCOLESTR name, LPCOLESTR item, IUnknown *context, LPCOLESTR delimiter,
                                            DWORD_PTR cookie, ULONG line, DWORD flags, IDispatch **out)
{
    Engine *e = ENGINE_OF(Procedure, This);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!code)
        return E_POINTER;
    if (e->State == SCRIPTSTATE_CLOSED || e->State == SCRIPTSTATE_UNINITIALIZED)
        return E_UNEXPECTED;
    BOOL expression = (flags & SCRIPTPROC_ISEXPRESSION) != 0;
    SIZE_T length = wcslen(code) + (params ? wcslen(params) : 0) + (name ? wcslen(name) : 0) + 64;
    WCHAR *text = js_alloc(length * sizeof(WCHAR));
    _snwprintf(text, length, expression ? L"(function %s(%s) {\nreturn (%s);\n})" : L"(function %s(%s) {\n%s\n})",
               name ? name : L"", params ? params : L"", code);
    InterlockedIncrement(&e->Refs);
    stack_base(e);
    engine_enter(e);
    ParseError err = {};
    Unit *unit = parse_unit(e, text, wcslen(text), 0, cookie, PARSE_EVAL, &err);
    js_free(text);
    HRESULT hr = S_OK;
    if (!unit) {
        fault_from_syntax(e, (HRESULT)(0x800A0000u | err.Code), err.Line, err.Col, err.LineText, cookie);
        js_free(err.LineText);
        engine_report(e);
        hr = SCRIPT_E_REPORTED;
    } else {
        unit->next = e->Units;
        e->Units = unit;
        Val fv = UNDEF;
        int rc = run_program(e, unit, nullptr, &fv, TRUE);
        if (rc == RC_THROW || fv.t != T_OBJ)
            hr = E_FAIL;
        else
            *out = dispatch_for_obj(e, fv.u.o);
    }
    engine_leave(e);
    script_release(&e->Script);
    return hr;
}

static const IActiveScriptParseProcedure2Vtbl g_proc_vtbl = {proc_qi, proc_addref, proc_release, proc_parse};

/* ------------------------------------------------------------------ the rest of the interfaces */

static HRESULT STDMETHODCALLTYPE prop_qi(IActiveScriptProperty *This, REFIID riid, void **object)
{
    return script_qi(&ENGINE_OF(Property, This)->Script, riid, object);
}

static ULONG STDMETHODCALLTYPE prop_addref(IActiveScriptProperty *This)
{
    return InterlockedIncrement(&ENGINE_OF(Property, This)->Refs);
}

static ULONG STDMETHODCALLTYPE prop_release(IActiveScriptProperty *This)
{
    return script_release(&ENGINE_OF(Property, This)->Script);
}

static HRESULT STDMETHODCALLTYPE prop_get(IActiveScriptProperty *This, DWORD id, VARIANT *arg, VARIANT *value)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE prop_set(IActiveScriptProperty *This, DWORD id, VARIANT *arg, VARIANT *value)
{
    return E_NOTIMPL;
}

static const IActiveScriptPropertyVtbl g_prop_vtbl = {prop_qi, prop_addref, prop_release, prop_get, prop_set};

static HRESULT STDMETHODCALLTYPE safety_qi(IObjectSafety *This, REFIID riid, void **object)
{
    return script_qi(&ENGINE_OF(Safety, This)->Script, riid, object);
}

static ULONG STDMETHODCALLTYPE safety_addref(IObjectSafety *This)
{
    return InterlockedIncrement(&ENGINE_OF(Safety, This)->Refs);
}

static ULONG STDMETHODCALLTYPE safety_release(IObjectSafety *This)
{
    return script_release(&ENGINE_OF(Safety, This)->Script);
}

static HRESULT STDMETHODCALLTYPE safety_get(IObjectSafety *This, REFIID riid, DWORD *supported, DWORD *enabled)
{
    Engine *e = ENGINE_OF(Safety, This);
    if (!supported || !enabled)
        return E_POINTER;
    *supported = INTERFACESAFE_FOR_UNTRUSTED_CALLER | INTERFACESAFE_FOR_UNTRUSTED_DATA;
    *enabled = e->SafetyOptions;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE safety_set(IObjectSafety *This, REFIID riid, DWORD mask, DWORD options)
{
    Engine *e = ENGINE_OF(Safety, This);
    DWORD supported = INTERFACESAFE_FOR_UNTRUSTED_CALLER | INTERFACESAFE_FOR_UNTRUSTED_DATA;
    if (mask & ~supported)
        return E_FAIL;
    e->SafetyOptions = (e->SafetyOptions & ~mask) | (options & mask);
    return S_OK;
}

static const IObjectSafetyVtbl g_safety_vtbl = {safety_qi, safety_addref, safety_release, safety_get, safety_set};

static HRESULT STDMETHODCALLTYPE gc_qi(IActiveScriptGarbageCollector *This, REFIID riid, void **object)
{
    return script_qi(&ENGINE_OF(Collector, This)->Script, riid, object);
}

static ULONG STDMETHODCALLTYPE gc_addref(IActiveScriptGarbageCollector *This)
{
    return InterlockedIncrement(&ENGINE_OF(Collector, This)->Refs);
}

static ULONG STDMETHODCALLTYPE gc_release(IActiveScriptGarbageCollector *This)
{
    return script_release(&ENGINE_OF(Collector, This)->Script);
}

static HRESULT STDMETHODCALLTYPE gc_collect_fn(IActiveScriptGarbageCollector *This, SCRIPTGCTYPE type)
{
    Engine *e = ENGINE_OF(Collector, This);
    stack_base(e);
    gc_collect(e);
    return S_OK;
}

static const IActiveScriptGarbageCollectorVtbl g_gc_vtbl = {gc_qi, gc_addref, gc_release, gc_collect_fn};

/* ------------------------------------------------------------------ the engine */

Engine *engine_create(void)
{
    Engine *e = js_alloc(sizeof(Engine));
    if (!e)
        return nullptr;
    e->Script.lpVtbl = &g_script_vtbl;
    e->Parse.lpVtbl = &g_parse_vtbl;
    e->Procedure.lpVtbl = &g_proc_vtbl;
    e->Property.lpVtbl = &g_prop_vtbl;
    e->Safety.lpVtbl = &g_safety_vtbl;
    e->Collector.lpVtbl = &g_gc_vtbl;
    e->Refs = 1;
    e->Lcid = GetUserDefaultLCID();
    e->Threshold = 8u * 1024 * 1024;
    stack_base(e);
    e->NoGc = TRUE;
    init_globals_all(e);
    e->NoGc = FALSE;
    InterlockedIncrement(&g_js_objects);
    return e;
}

static void engine_destroy(Engine *e)
{
    e->Refs = 1000;
    if (e->State != SCRIPTSTATE_CLOSED)
        engine_close(e);
    e->Closing = TRUE;
    /* the dispatches the host still holds lose their engine. */
    for (struct JsDisp *d = e->Disps; d;) {
        struct JsDisp *next = jsdisp_orphan(d);
        d = next;
    }
    e->Disps = nullptr;
    /* everything the heap holds goes, with the objects it keeps from the host. */
    e->RootCount = 0;
    e->StackBase = nullptr;
    e->Global = nullptr;
    e->NoGc = FALSE;
    free_heap(e);
    while (e->DeferredCount) {
        IUnknown *u = e->Deferred[--e->DeferredCount];
        u->lpVtbl->Release(u);
    }
    js_free(e->Deferred);
    js_free(e->Roots);
    js_free(e->Interned);
    js_free(e->DispNames);
    for (NamedItem *it = e->Items; it;) {
        NamedItem *n = it->next;
        js_free(it);
        it = n;
    }
    free_units(&e->Units);
    free_units(&e->Pending);
    fault_clear(e);
    SysFreeString(e->ErrSource);
    InterlockedDecrement(&g_js_objects);
    js_free(e);
}

void init_globals_all(Engine *e)
{
    Obj *op = obj_new(e, C_OBJECT, nullptr);
    e->ObjectProto = op;
    Obj *fp = obj_new(e, C_FUNCTION, op);
    fp->u.fn.native = nullptr;
    obj_define(e, fp, str_intern_ascii(e, "length"), v_num(0), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    e->FunctionProto = fp;
    e->Global = obj_new(e, C_GLOBAL, nullptr);
    init_error(e);
    init_object(e);
    init_function(e);
    init_array(e);
    init_string(e);
    init_number(e);
    init_math(e);
    init_date(e);
    init_regexp(e);
    init_host(e);
    init_globals(e);
}
