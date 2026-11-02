/*
 * the scripting engine object: IActiveScript and the parsers over it, the states a host moves it through, and how
 * errors reach the site.
 */
#include "vbs.h"

HINSTANCE g_vbs_module;

#define ENGINE_OF(field, ptr) ((Engine *)((BYTE *)(ptr) - offsetof(Engine, field)))

static void engine_destroy(Engine *e);

void engine_release(Engine *e)
{
    if (InterlockedDecrement(&e->Refs) == 0)
        engine_destroy(e);
}

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
        vbs_free(e->F.Text);
        vbs_free(e);
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
    if (cookie)
        *cookie = (DWORD)e->F.Cookie;
    if (line)
        *line = e->F.Line;
    if (character)
        *character = (LONG)e->F.Col;
    return S_OK;
}

/* IActiveScriptError64 adds this call after the three of the interface it extends, with a source context of 64 bits. */
static HRESULT STDMETHODCALLTYPE error_position64(IActiveScriptError *This, DWORD64 *cookie, ULONG *line, LONG *character)
{
    ScriptError *e = (ScriptError *)This;
    if (cookie)
        *cookie = (DWORD64)e->F.Cookie;
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

/* the table has the extra call of the 64-bit interface after the others, which a caller of the plain interface never reaches. */
static const struct {
    IActiveScriptErrorVtbl Base;
    HRESULT(STDMETHODCALLTYPE *GetSourcePosition64)(IActiveScriptError *, DWORD64 *, ULONG *, LONG *);
} g_error_table = {{error_qi, error_addref, error_release, error_info, error_position, error_line}, error_position64};
#define g_error_vtbl g_error_table.Base

/* tells the site about the fault in progress, then clears it. */
void engine_report(Engine *e, BOOL terminate)
{
    if (!e->Fault.Set)
        return;
    if (e->Site) {
        ScriptError *err = vbs_alloc(sizeof(*err));
        err->Error.lpVtbl = &g_error_vtbl;
        err->Refs = 1;
        err->F = e->Fault;
        err->F.Source = e->Fault.Source ? SysAllocStringLen(e->Fault.Source, SysStringLen(e->Fault.Source)) : nullptr;
        err->F.Description = e->Fault.Description ? SysAllocStringLen(e->Fault.Description, SysStringLen(e->Fault.Description)) : nullptr;
        err->F.Text = e->Fault.Text ? vbs_wcsdup(e->Fault.Text) : nullptr;
        e->Site->lpVtbl->OnScriptError(e->Site, &err->Error);
        err->Error.lpVtbl->Release(&err->Error);
    }
    fault_clear(e);
}

HRESULT engine_enter(Engine *e)
{
    if (e->NestedRun++ == 0) {
        e->Interrupted = FALSE;
        if (e->Site)
            e->Site->lpVtbl->OnEnterScript(e->Site);
    }
    return S_OK;
}

void engine_leave(Engine *e)
{
    if (--e->NestedRun == 0 && e->Site)
        e->Site->lpVtbl->OnLeaveScript(e->Site);
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
        *list = u->Next;
        unit_free(u);
    }
}

/* runs the units that wait for the script to start. */
int engine_run_global(Engine *e)
{
    int rc = R_OK;
    engine_enter(e);
    while (e->Pending && rc == R_OK) {
        Unit *u = e->Pending;
        e->Pending = u->Next;
        u->Next = e->Units;
        e->Units = u;
        e->Global.Unit = u;
        rc = run_unit(e, u, &e->Global);
        if (rc >= R_EXIT_FOR)
            rc = R_OK;
    }
    if (rc != R_OK)
        engine_report(e, FALSE);
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
    if (e->InitDone)
        notify(e, SCRIPTSTATE_INITIALIZED);
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

static HRESULT STDMETHODCALLTYPE script_set_state(IActiveScript *This, SCRIPTSTATE state)
{
    Engine *e = ENGINE_OF(Script, This);
    if (state == SCRIPTSTATE_UNINITIALIZED && e->State == SCRIPTSTATE_UNINITIALIZED)
        return S_OK;
    if (e->State == SCRIPTSTATE_CLOSED || e->State == SCRIPTSTATE_UNINITIALIZED || !e->Site)
        return E_UNEXPECTED;
    switch (state) {
    case SCRIPTSTATE_STARTED:
    case SCRIPTSTATE_CONNECTED:
        engine_run_global(e);
        notify(e, state);
        return S_OK;
    case SCRIPTSTATE_DISCONNECTED:
        notify(e, state);
        return S_OK;
    case SCRIPTSTATE_INITIALIZED:
        notify(e, state);
        return S_OK;
    case SCRIPTSTATE_CLOSED:
        return e->Script.lpVtbl->Close(&e->Script);
    case SCRIPTSTATE_UNINITIALIZED: {
        /* the site goes, the next SetScriptSite starts the script again. */
        notify(e, state);
        IActiveScriptSite *site = e->Site;
        e->Site = nullptr;
        site->lpVtbl->Release(site);
        return S_OK;
    }
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
    if (e->State == SCRIPTSTATE_CONNECTED)
        notify(e, SCRIPTSTATE_DISCONNECTED);
    if (e->State >= SCRIPTSTATE_STARTED && e->State <= SCRIPTSTATE_DISCONNECTED)
        notify(e, SCRIPTSTATE_INITIALIZED);
    /* the global variables and items go, which releases the objects the script holds. */
    for (UINT i = 0; i < 128; i++)
        for (Sym *s = e->Buckets[i]; s; s = s->Next)
            if (s->Kind == SYM_VAR || s->Kind == SYM_CONST || s->Kind == SYM_ITEM)
                VariantClear(&s->Value);
    if (e->Site) {
        IActiveScriptSite *site = e->Site;
        notify(e, SCRIPTSTATE_CLOSED);
        e->Site = nullptr;
        site->lpVtbl->Release(site);
    } else
        e->State = SCRIPTSTATE_CLOSED;
}

static HRESULT STDMETHODCALLTYPE script_close(IActiveScript *This)
{
    Engine *e = ENGINE_OF(Script, This);
    if (e->State == SCRIPTSTATE_CLOSED)
        return E_UNEXPECTED;
    engine_close(e);
    return S_OK;
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
    Sym *old = sym_find(e, name, name_hash(name));
    if (old)
        return E_INVALIDARG;
    Sym *s = sym_add(e, name, SYM_ITEM);
    if (!s)
        return E_OUTOFMEMORY;
    s->ItemFlags = flags;
    s->Public = 1;
    Frame f = {};
    f.E = e;
    VARIANT v;
    VariantInit(&v);
    if (flags & SCRIPTITEM_ISVISIBLE) {
        extern int vbs_item_load(Frame * f, Sym * s);
        vbs_item_load(&f, s);
    }
    VariantClear(&v);
    return S_OK;
}

int vbs_item_load(Frame *f, Sym *s)
{
    Engine *e = f->E;
    IUnknown *unk = nullptr;
    HRESULT hr = e->Site->lpVtbl->GetItemInfo(e->Site, s->Name, SCRIPTINFO_IUNKNOWN, &unk, nullptr);
    if (FAILED(hr) || !unk)
        return R_ERR;
    IDispatch *d = nullptr;
    unk->lpVtbl->QueryInterface(unk, &IID_IDispatch, (void **)&d);
    unk->lpVtbl->Release(unk);
    if (!d)
        return R_ERR;
    VariantClear(&s->Value);
    s->Value.vt = VT_DISPATCH;
    s->Value.pdispVal = d;
    return R_OK;
}

/* the constants of a type library become constants of the script: the members of its enumerations and of its modules. */
static HRESULT STDMETHODCALLTYPE script_add_typelib(IActiveScript *This, REFGUID lib, DWORD major, DWORD minor, DWORD flags)
{
    Engine *e = ENGINE_OF(Script, This);
    ITypeLib *library;
    HRESULT hr = LoadRegTypeLib(lib, (WORD)major, (WORD)minor, 0, &library);
    if (FAILED(hr))
        return hr;
    UINT count = library->lpVtbl->GetTypeInfoCount(library);
    for (UINT i = 0; i < count; i++) {
        TYPEKIND kind;
        ITypeInfo *info;
        if (FAILED(library->lpVtbl->GetTypeInfoType(library, i, &kind)) || (kind != TKIND_ENUM && kind != TKIND_MODULE))
            continue;
        if (FAILED(library->lpVtbl->GetTypeInfo(library, i, &info)))
            continue;
        TYPEATTR *attr;
        if (SUCCEEDED(info->lpVtbl->GetTypeAttr(info, &attr))) {
            for (UINT v = 0; v < attr->cVars; v++) {
                VARDESC *var;
                if (FAILED(info->lpVtbl->GetVarDesc(info, v, &var)))
                    continue;
                BSTR name = nullptr;
                UINT got = 0;
                if (var->varkind == VAR_CONST && var->lpvarValue && SUCCEEDED(info->lpVtbl->GetNames(info, var->memid, &name, 1, &got)) && got &&
                    !sym_find(e, name, name_hash(name))) {
                    Sym *symbol = sym_add(e, name, SYM_CONST);
                    if (symbol) {
                        symbol->Public = 1;
                        symbol->Declared = 1;
                        VariantCopy(&symbol->Value, var->lpvarValue);
                    }
                }
                SysFreeString(name);
                info->lpVtbl->ReleaseVarDesc(info, var);
            }
            info->lpVtbl->ReleaseTypeAttr(info, attr);
        }
        info->lpVtbl->Release(info);
    }
    library->lpVtbl->Release(library);
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
    Engine *e = ENGINE_OF(Script, This);
    e->InterruptCode = info ? info->scode : 0;
    e->Interrupted = TRUE;
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
    if (e->InitDone || e->State == SCRIPTSTATE_CLOSED)
        return E_UNEXPECTED;
    e->InitDone = TRUE;
    if (e->Site)
        notify(e, SCRIPTSTATE_INITIALIZED);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE parse_scriptlet(IActiveScriptParse *This, LPCOLESTR default_name, LPCOLESTR code,
                                                 LPCOLESTR item, LPCOLESTR subitem, LPCOLESTR event, LPCOLESTR delimiter,
                                                 DWORD_PTR cookie, ULONG line, DWORD flags, BSTR *name, EXCEPINFO *info)
{
    return E_NOTIMPL;
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
    Unit *unit = parse_unit(e, code, line, cookie, expression, FALSE);
    if (!unit) {
        engine_report(e, FALSE);
        engine_release(e);
        return SCRIPT_E_REPORTED;
    }
    unit->Global = TRUE;
    if (expression) {
        engine_enter(e);
        e->Global.Unit = unit;
        unit_link(e, unit, &e->Global);
        VARIANT v;
        VariantInit(&v);
        Stmt *s = unit->Statements;
        e->Global.Line = e->Global.Col = 0;
        int rc = eval_unit_expression(e, s->u.Call->u.Operand, &e->Global, &v);
        if (rc != R_OK) {
            engine_report(e, FALSE);
            hr = SCRIPT_E_REPORTED;
        } else if (result)
            *result = v;
        else
            VariantClear(&v);
        engine_leave(e);
        unit->Next = e->Units;
        e->Units = unit;
        engine_release(e);
        return hr;
    }
    if (!unit_declare(e, unit, &e->Global)) {
        /* a name declared twice is found when the script starts. */
        engine_report(e, FALSE);
        unit_free(unit);
        engine_release(e);
        return S_OK;
    }
    unit_link(e, unit, &e->Global);
    unit->Next = nullptr;
    if (e->State == SCRIPTSTATE_INITIALIZED) {
        Unit **tail = &e->Pending;
        while (*tail)
            tail = &(*tail)->Next;
        *tail = unit;
    } else if (flags & SCRIPTTEXT_DELAYEXECUTION) {
        Unit **tail = &e->Pending;
        while (*tail)
            tail = &(*tail)->Next;
        *tail = unit;
    } else {
        unit->Next = e->Pending;
        e->Pending = unit;
        int rc = engine_run_global(e);
        hr = rc == R_OK ? S_OK : SCRIPT_E_REPORTED;
    }
    engine_release(e);
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
    SIZE_T length = wcslen(code) + (params ? wcslen(params) : 0) + 128;
    WCHAR *text = vbs_alloc(length * sizeof(WCHAR));
    _snwprintf(text, length, expression ? L"Function __anonymous(%s)\n__anonymous = (%s)\nEnd Function\n"
                                        : L"Sub __anonymous(%s)\n%s\nEnd Sub\n",
               params ? params : L"", code);
    Unit *unit = parse_unit(e, text, 0, cookie, FALSE, FALSE);
    vbs_free(text);
    if (!unit) {
        engine_report(e, FALSE);
        return SCRIPT_E_REPORTED;
    }
    unit->Global = TRUE;
    Proc *proc = unit->Procedures;
    if (!proc) {
        unit_free(unit);
        return E_FAIL;
    }
    unit_link(e, unit, &e->Global);
    unit->Next = e->Units;
    e->Units = unit;
    *out = proc_ref_create(e, proc, nullptr);
    return S_OK;
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
    *supported = 14;
    *enabled = e->SafetyOptions;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE safety_set(IObjectSafety *This, REFIID riid, DWORD mask, DWORD options)
{
    Engine *e = ENGINE_OF(Safety, This);
    if ((e->State != SCRIPTSTATE_UNINITIALIZED && e->State != SCRIPTSTATE_INITIALIZED) || (mask & ~14u))
        return E_FAIL;
    e->SafetyOptions = (mask & (options | INTERFACESAFE_FOR_UNTRUSTED_CALLER)) | (e->SafetyOptions & ~mask);
    return S_OK;
}

static const IObjectSafetyVtbl g_safety_vtbl = {safety_qi, safety_addref, safety_release, safety_get, safety_set};

/* ------------------------------------------------------------------ the engine */

Engine *engine_create(void)
{
    Engine *e = vbs_alloc(sizeof(Engine));
    if (!e)
        return nullptr;
    e->Script.lpVtbl = &g_script_vtbl;
    e->Parse.lpVtbl = &g_parse_vtbl;
    e->Procedure.lpVtbl = &g_proc_vtbl;
    e->Property.lpVtbl = &g_prop_vtbl;
    e->Safety.lpVtbl = &g_safety_vtbl;
    e->Refs = 1;
    e->Lcid = GetUserDefaultLCID();
    e->Global.E = e;
    e->ErrObject = err_object_create(e);
    InterlockedIncrement(&g_vbs_objects);
    return e;
}

static void engine_destroy(Engine *e)
{
    e->Refs = 1000;
    if (e->State != SCRIPTSTATE_CLOSED)
        engine_close(e);
    e->Closing = TRUE;
    for (UINT i = 0; i < 128; i++) {
        Sym *s = e->Buckets[i];
        while (s) {
            Sym *next = s->Next;
            VariantClear(&s->Value);
            vbs_free(s->Name);
            vbs_free(s);
            s = next;
        }
        e->Buckets[i] = nullptr;
    }
    free_units(&e->Units);
    free_units(&e->Pending);
    err_clear(e);
    fault_clear(e);
    vbs_free(e->ErrObject);
    InterlockedDecrement(&g_vbs_objects);
    vbs_free(e);
}
