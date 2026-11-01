/*
 * the Err object, CreateObject and GetObject, and DoEvents.
 */
#include "vb.h"
#include <mime/win32/objidl.h>

/* ------------------------------------------------------------------ Err */

typedef struct ErrObject ErrObject;
struct ErrObject {
    const struct ErrVtbl {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(ErrObject *, REFIID, void **);
        ULONG(STDMETHODCALLTYPE *AddRef)(ErrObject *);
        ULONG(STDMETHODCALLTYPE *Release)(ErrObject *);
        HRESULT(STDMETHODCALLTYPE *GetTypeInfoCount)(ErrObject *, UINT *);
        HRESULT(STDMETHODCALLTYPE *GetTypeInfo)(ErrObject *, UINT, LCID, ITypeInfo **);
        HRESULT(STDMETHODCALLTYPE *GetIDsOfNames)(ErrObject *, REFIID, LPOLESTR *, UINT, LCID, DISPID *);
        HRESULT(STDMETHODCALLTYPE *Invoke)(ErrObject *, DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
        HRESULT(STDMETHODCALLTYPE *GetNumber)(ErrObject *, LONG *);
        HRESULT(STDMETHODCALLTYPE *PutNumber)(ErrObject *, LONG);
        HRESULT(STDMETHODCALLTYPE *GetSource)(ErrObject *, BSTR *);
        HRESULT(STDMETHODCALLTYPE *PutSource)(ErrObject *, BSTR);
        HRESULT(STDMETHODCALLTYPE *GetDescription)(ErrObject *, BSTR *);
        HRESULT(STDMETHODCALLTYPE *PutDescription)(ErrObject *, BSTR);
        HRESULT(STDMETHODCALLTYPE *GetHelpFile)(ErrObject *, BSTR *);
        HRESULT(STDMETHODCALLTYPE *PutHelpFile)(ErrObject *, BSTR);
        HRESULT(STDMETHODCALLTYPE *GetHelpContext)(ErrObject *, LONG *);
        HRESULT(STDMETHODCALLTYPE *PutHelpContext)(ErrObject *, LONG);
        HRESULT(STDMETHODCALLTYPE *Raise)(ErrObject *, LONG, VARIANT *, VARIANT *, VARIANT *, VARIANT *);
        HRESULT(STDMETHODCALLTYPE *Clear)(ErrObject *);
        HRESULT(STDMETHODCALLTYPE *GetLastDllError)(ErrObject *, LONG *);
    } *lpVtbl;
};

static const IID IID_ErrObject = {0xA4C466B8, 0x499F, 0x101B, {0xBB, 0x78, 0x00, 0xAA, 0x00, 0x38, 0x3C, 0xBB}};
static const IID IID_ErrObject2 = {0xA4C46654, 0x499F, 0x101B, {0xBB, 0x78, 0x00, 0xAA, 0x00, 0x38, 0x3C, 0xBB}};

static BSTR copy_of(BSTR text)
{
    return text ? SysAllocString(text) : SysAllocString(L"");
}

static HRESULT STDMETHODCALLTYPE err_query(ErrObject *self, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDispatch) || IsEqualGUID(iid, &IID_ErrObject) || IsEqualGUID(iid, &IID_ErrObject2)) {
        *out = self;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE err_addref(ErrObject *self) { return 2; }
static ULONG STDMETHODCALLTYPE err_release(ErrObject *self) { return 1; }
static HRESULT STDMETHODCALLTYPE err_typeinfocount(ErrObject *self, UINT *count)
{
    *count = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE err_typeinfo(ErrObject *self, UINT index, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE err_get_number(ErrObject *self, LONG *out)
{
    *out = VbThreadState()->err_number;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE err_put_number(ErrObject *self, LONG number)
{
    VbThread *t = VbThreadState();
    t->err_number = number;
    t->hresult = number > 0 && number <= 0xFFFF ? (LONG)(0x800A0000u | (ULONG)number) : number;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE err_get_source(ErrObject *self, BSTR *out)
{
    VbThread *t = VbThreadState();
    if (t->err_source)
        *out = SysAllocString(t->err_source);
    else
        *out = SysAllocString(t->err_number ? VbProjectName() : L"");
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE err_put_source(ErrObject *self, BSTR text)
{
    VbThread *t = VbThreadState();
    SysFreeString(t->err_source);
    t->err_source = copy_of(text);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE err_get_description(ErrObject *self, BSTR *out)
{
    VbThread *t = VbThreadState();
    if (t->err_description && t->err_description[0])
        *out = SysAllocString(t->err_description);
    else
        *out = SysAllocString(t->err_number ? VbErrorText(t->err_number) : L"");
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE err_put_description(ErrObject *self, BSTR text)
{
    VbThread *t = VbThreadState();
    SysFreeString(t->err_description);
    t->err_description = copy_of(text);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE err_get_helpfile(ErrObject *self, BSTR *out)
{
    VbThread *t = VbThreadState();
    *out = copy_of(t->err_helpfile);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE err_put_helpfile(ErrObject *self, BSTR text)
{
    VbThread *t = VbThreadState();
    SysFreeString(t->err_helpfile);
    t->err_helpfile = copy_of(text);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE err_get_helpcontext(ErrObject *self, LONG *out)
{
    *out = VbThreadState()->err_helpcontext;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE err_put_helpcontext(ErrObject *self, LONG value)
{
    VbThreadState()->err_helpcontext = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE err_get_lastdll(ErrObject *self, LONG *out)
{
    *out = VbThreadState()->last_dll_error;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE err_clear(ErrObject *self)
{
    VbClearError();
    return S_OK;
}

/* an argument of Raise that was left out is an error variant that says so, or no variant at all. */
static BOOL given(const VARIANT *v)
{
    if (!v)
        return FALSE;
    if (V_VT(v) == VT_ERROR && V_ERROR(v) == DISP_E_PARAMNOTFOUND)
        return FALSE;
    if (V_VT(v) == (VT_BYREF | VT_VARIANT) && V_VARIANTREF(v))
        return given(V_VARIANTREF(v));
    return TRUE;
}

static BSTR text_of(const VARIANT *v)
{
    VARIANT in, out;
    VariantInit(&in);
    VariantInit(&out);
    VbCheck(VbCopyInd(&in, (VARIANT *)v));
    HRESULT hr = VariantChangeTypeEx(&out, &in, VbLocale(), 0, VT_BSTR);
    VariantClear(&in);
    VbCheck(hr);
    return V_BSTR(&out);
}

static LONG long_of(const VARIANT *v)
{
    VARIANT in, out;
    VariantInit(&in);
    VariantInit(&out);
    VbCheck(VbCopyInd(&in, (VARIANT *)v));
    HRESULT hr = VariantChangeTypeEx(&out, &in, VbLocale(), 0, VT_I4);
    VariantClear(&in);
    VbCheck(hr);
    return V_I4(&out);
}

/* Raise sets what the error carries, then raises it. a number of 0, or above 65535 and not an error of Automation, is not one. */
static HRESULT STDMETHODCALLTYPE err_raise(ErrObject *self, LONG number, VARIANT *source, VARIANT *description, VARIANT *helpfile, VARIANT *helpcontext)
{
    if (number == 0 || number > 0xFFFF)
        VbRaise(VBERR_INVALID_CALL);
    VbThread *t = VbThreadState();
    BSTR new_source = given(source) ? text_of(source) : nullptr;
    BSTR new_description = given(description) ? text_of(description) : nullptr;
    BSTR new_helpfile = given(helpfile) ? text_of(helpfile) : nullptr;
    LONG context = given(helpcontext) ? long_of(helpcontext) : 0;
    SysFreeString(t->err_source);
    SysFreeString(t->err_description);
    SysFreeString(t->err_helpfile);
    t->err_source = new_source ? new_source : SysAllocString(VbProjectName());
    t->err_description = new_description;
    t->err_helpfile = new_helpfile;
    t->err_helpcontext = context;
    VbRaiseKeep(number);
}

/* the members by name, with the numbers they have for Invoke. */
enum { DISP_NUMBER = 0, DISP_SOURCE = 1, DISP_DESCRIPTION = 2, DISP_HELPFILE = 3, DISP_HELPCONTEXT = 4, DISP_RAISE = 5, DISP_CLEAR = 6, DISP_LASTDLL = 7 };

static const struct {
    const WCHAR *name;
    DISPID id;
} g_err_names[] = {
    {L"Number", DISP_NUMBER}, {L"Source", DISP_SOURCE}, {L"Description", DISP_DESCRIPTION}, {L"HelpFile", DISP_HELPFILE},
    {L"HelpContext", DISP_HELPCONTEXT}, {L"Raise", DISP_RAISE}, {L"Clear", DISP_CLEAR}, {L"LastDllError", DISP_LASTDLL},
};

static HRESULT STDMETHODCALLTYPE err_names(ErrObject *self, REFIID iid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    HRESULT result = S_OK;
    for (UINT i = 0; i < count; i++) {
        ids[i] = DISPID_UNKNOWN;
        if (i)
            continue;
        for (size_t k = 0; k < sizeof(g_err_names) / sizeof(g_err_names[0]); k++)
            if (!lstrcmpiW(names[0], g_err_names[k].name))
                ids[0] = g_err_names[k].id;
    }
    for (UINT i = 0; i < count; i++)
        if (ids[i] == DISPID_UNKNOWN)
            result = DISP_E_UNKNOWNNAME;
    return result;
}

/* the arguments of a call arrive last first, and the argument at the index counted from the first. */
static VARIANT *argument(const DISPPARAMS *params, UINT index)
{
    return index < params->cArgs ? &params->rgvarg[params->cArgs - 1 - index] : nullptr;
}

static HRESULT STDMETHODCALLTYPE err_invoke(ErrObject *self, DISPID id, REFIID iid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *info, UINT *bad)
{
    BOOL put = (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0;
    VARIANT dummy;
    if (!result)
        result = &dummy;
    VariantInit(result);
    switch (id) {
    case DISP_NUMBER:
    case DISP_HELPCONTEXT:
    case DISP_LASTDLL: {
        if (put && id != DISP_LASTDLL) {
            if (!params->cArgs)
                return DISP_E_BADPARAMCOUNT;
            VARIANT in;
            VariantInit(&in);
            if (FAILED(VariantChangeTypeEx(&in, &params->rgvarg[0], lcid, 0, VT_I4)))
                return DISP_E_TYPEMISMATCH;
            LONG value = V_I4(&in);
            return id == DISP_NUMBER ? err_put_number(self, value) : err_put_helpcontext(self, value);
        }
        if (put)
            return DISP_E_MEMBERNOTFOUND;
        LONG value = 0;
        if (id == DISP_NUMBER)
            err_get_number(self, &value);
        else if (id == DISP_HELPCONTEXT)
            err_get_helpcontext(self, &value);
        else
            err_get_lastdll(self, &value);
        V_VT(result) = VT_I4;
        V_I4(result) = value;
        return S_OK;
    }
    case DISP_SOURCE:
    case DISP_DESCRIPTION:
    case DISP_HELPFILE: {
        if (put) {
            if (!params->cArgs)
                return DISP_E_BADPARAMCOUNT;
            VARIANT in;
            VariantInit(&in);
            if (FAILED(VariantChangeTypeEx(&in, &params->rgvarg[0], lcid, 0, VT_BSTR)))
                return DISP_E_TYPEMISMATCH;
            HRESULT hr = id == DISP_SOURCE ? err_put_source(self, V_BSTR(&in)) : id == DISP_DESCRIPTION ? err_put_description(self, V_BSTR(&in)) : err_put_helpfile(self, V_BSTR(&in));
            VariantClear(&in);
            return hr;
        }
        BSTR text;
        if (id == DISP_SOURCE)
            err_get_source(self, &text);
        else if (id == DISP_DESCRIPTION)
            err_get_description(self, &text);
        else
            err_get_helpfile(self, &text);
        V_VT(result) = VT_BSTR;
        V_BSTR(result) = text;
        return S_OK;
    }
    case DISP_CLEAR: return err_clear(self);
    case DISP_RAISE: {
        if (!params->cArgs)
            return DISP_E_BADPARAMCOUNT;
        LONG number = long_of(argument(params, 0));
        return err_raise(self, number, argument(params, 1), argument(params, 2), argument(params, 3), argument(params, 4));
    }
    }
    return DISP_E_MEMBERNOTFOUND;
}

static const struct ErrVtbl g_err_vtbl = {
    err_query, err_addref, err_release, err_typeinfocount, err_typeinfo, err_names, err_invoke, err_get_number, err_put_number,
    err_get_source, err_put_source, err_get_description, err_put_description, err_get_helpfile, err_put_helpfile,
    err_get_helpcontext, err_put_helpcontext, err_raise, err_clear, err_get_lastdll,
};

static ErrObject g_err = {&g_err_vtbl};

VB_STD IUnknown *vb_rtcErrObj(void)
{
    return (IUnknown *)&g_err;
}

/* ------------------------------------------------------------------ CreateObject and GetObject */

static const IID IID_PersistStreamInit = {0x7FD52380, 0x4E07, 0x101B, {0xAE, 0x2D, 0x08, 0x00, 0x2B, 0x2E, 0xC7, 0x13}};
static const IID IID_PersistPropertyBag = {0x37D84F60, 0x42CB, 0x11CE, {0x81, 0x35, 0x00, 0xAA, 0x00, 0x4B, 0xB8, 0x51}};
static const IID IID_PersistFile = {0x0000010B, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

typedef struct {
    const struct {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
        ULONG(STDMETHODCALLTYPE *AddRef)(void *);
        ULONG(STDMETHODCALLTYPE *Release)(void *);
        void *slot[16];
    } *lpVtbl;
} RawObject;

static HRESULT call_none(void *object, int index)
{
    return ((HRESULT(STDMETHODCALLTYPE *)(void *))((RawObject *)object)->lpVtbl->slot[index - 3])(object);
}

static HRESULT call_load(void *object, int index, const WCHAR *file, DWORD mode)
{
    return ((HRESULT(STDMETHODCALLTYPE *)(void *, const WCHAR *, DWORD))((RawObject *)object)->lpVtbl->slot[index - 3])(object, file, mode);
}

/* an object that can start itself empty does that, through the interface of a form or of a property bag. */
static HRESULT init_new(IUnknown *object)
{
    void *persist;
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_PersistStreamInit, &persist))) {
        HRESULT hr = call_none(persist, 8);
        ((IUnknown *)persist)->lpVtbl->Release(persist);
        return hr;
    }
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_PersistPropertyBag, &persist))) {
        HRESULT hr = call_none(persist, 4);
        ((IUnknown *)persist)->lpVtbl->Release(persist);
        return hr;
    }
    return S_OK;
}

/* the object comes back as a dispatch interface when it has one, else as an unknown. */
static VARIANT *result_of(VARIANT *out, IUnknown *object)
{
    IDispatch *dispatch;
    VariantInit(out);
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_IDispatch, (void **)&dispatch))) {
        object->lpVtbl->Release(object);
        V_VT(out) = VT_DISPATCH;
        V_DISPATCH(out) = dispatch;
    } else {
        V_VT(out) = VT_UNKNOWN;
        V_UNKNOWN(out) = object;
    }
    return out;
}

static BOOL is_local(const WCHAR *server)
{
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    while (*server == L'\\')
        server++;
    return GetComputerNameW(name, &size) && !lstrcmpiW(name, server);
}

/* a name that is no program id may be a moniker, such as the one of a script object. */
static HRESULT bind_name(const WCHAR *name, IUnknown **object)
{
    IBindCtx *context;
    IMoniker *moniker;
    ULONG eaten = 0;
    *object = nullptr;
    HRESULT hr = CreateBindCtx(0, &context);
    if (FAILED(hr))
        return hr;
    hr = MkParseDisplayName(context, name, &eaten, &moniker);
    context->lpVtbl->Release(context);
    if (FAILED(hr))
        return hr;
    hr = BindMoniker(moniker, 0, &IID_IUnknown, (void **)object);
    moniker->lpVtbl->Release(moniker);
    if (SUCCEEDED(hr) && !*object)
        return (HRESULT)0x800A01AD;
    return hr;
}

VB_STD VARIANT *vb_rtcCreateObject2(VARIANT *out, BSTR progid, BSTR server)
{
    if (!progid || !progid[0])
        VbRaise(VBERR_CANNOT_CREATE_OBJECT);
    CLSID clsid;
    IUnknown *object = nullptr;
    HRESULT hr;
    SetErrorInfo(0, nullptr);
    if (server && server[0] && !is_local(server)) {
        hr = CLSIDFromProgID(progid, &clsid);
        if (hr == (HRESULT)0x800401F3)
            hr = (HRESULT)0x800A01CF;
        if (FAILED(hr))
            VbRaiseHr(hr);
        COSERVERINFO info = {};
        MULTI_QI query = {&IID_IUnknown, nullptr, 0};
        info.pwszName = server;
        hr = CoCreateInstanceEx(&clsid, nullptr, CLSCTX_REMOTE_SERVER, &info, 1, &query);
        if (SUCCEEDED(hr))
            hr = query.hr;
        if (FAILED(hr))
            VbRaiseHr(hr);
        object = (IUnknown *)query.pItf;
    } else if (SUCCEEDED(CLSIDFromProgID(progid, &clsid))) {
        hr = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, &IID_IUnknown, (void **)&object);
        if (SUCCEEDED(hr))
            hr = init_new(object);
        if (FAILED(hr)) {
            if (object)
                object->lpVtbl->Release(object);
            VbRaiseHr(hr);
        }
    } else if (FAILED(bind_name(progid, &object))) {
        char text[200];
        char narrow[120];
        WideCharToMultiByte(CP_ACP, 0, progid, -1, narrow, sizeof(narrow), nullptr, nullptr);
        narrow[sizeof(narrow) - 1] = 0;
        wsprintfA(text, "msvbvm60: CreateObject of %s failed, the class is not registered\n", narrow);
        VbOutputDebug(text);
        VbRaise(VBERR_CANNOT_CREATE_OBJECT);
    }
    if (!object)
        VbRaise(VBERR_CANNOT_CREATE_OBJECT);
    return result_of(out, object);
}

VB_STD VARIANT *vb_rtcCreateObject(VARIANT *out, BSTR progid)
{
    return vb_rtcCreateObject2(out, progid, nullptr);
}

static BOOL left_out(const VARIANT *v)
{
    return !v || (V_VT(v) == VT_ERROR && V_ERROR(v) == DISP_E_PARAMNOTFOUND);
}

/* GetObject(path, class) opens a file with the class, takes the running object of the class, or binds the path as a moniker. */
VB_STD VARIANT *vb_rtcGetObject(VARIANT *out, VARIANT *path, VARIANT *class_name)
{
    IUnknown *object = nullptr;
    HRESULT hr;
    if (left_out(class_name)) {
        if (left_out(path))
            VbRaise(VBERR_INVALID_CALL);
        BSTR name = text_of(path);
        hr = bind_name(name, &object);
        SysFreeString(name);
        if (FAILED(hr))
            VbRaiseHr(hr);
        return result_of(out, object);
    }
    CLSID clsid;
    BSTR progid = text_of(class_name);
    hr = CLSIDFromProgID(progid, &clsid);
    SysFreeString(progid);
    if (FAILED(hr))
        VbRaiseHr(hr);
    if (left_out(path)) {
        hr = GetActiveObject(&clsid, nullptr, &object);
        if (FAILED(hr))
            VbRaiseHr(hr);
        if (!object)
            VbRaise(VBERR_CANNOT_CREATE_OBJECT);
        return result_of(out, object);
    }
    hr = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, &IID_IUnknown, (void **)&object);
    if (FAILED(hr))
        VbRaiseHr(hr);
    if (!object)
        VbRaise(VBERR_CANNOT_CREATE_OBJECT);
    BSTR file = text_of(path);
    if (!file[0]) {
        hr = init_new(object);
    } else {
        void *persist;
        if (FAILED(object->lpVtbl->QueryInterface(object, &IID_PersistFile, &persist))) {
            hr = (HRESULT)0x800A01B0;
        } else {
            hr = call_load(persist, 5, file, 0);
            ((IUnknown *)persist)->lpVtbl->Release(persist);
            if (hr == S_FALSE)
                hr = (HRESULT)0x800A01AD;
        }
    }
    SysFreeString(file);
    if (FAILED(hr)) {
        object->lpVtbl->Release(object);
        VbRaiseHr(hr);
    }
    return result_of(out, object);
}

/* ------------------------------------------------------------------ DoEvents */

/* DoEvents handles the messages that wait, and answers with the number of forms that are open. */
VB_STD SHORT vb_rtcDoEvents(void)
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Sleep(0);
    return (SHORT)VbFormCount();
}

/* ------------------------------------------------------------------ Declare */

static void raise_described(LONG number, const WCHAR *text)
{
    VbThread *t = VbThreadState();
    SysFreeString(t->err_description);
    SysFreeString(t->err_source);
    t->err_description = SysAllocString(text);
    t->err_source = SysAllocString(VbProjectName());
    VbRaiseKeep(number);
}

/*
 * the address of a function a Declare names. the description holds the names of the library and of the function, or the number of
 * the function, and a cell where the library and the address are kept after the first call.
 */
VB_STD void *vb_DllFunctionCall(const BYTE *desc)
{
    const char *library = *(const char *const *)desc;
    const char *name = *(const char *const *)(desc + 4);
    WORD ordinal = *(const WORD *)(desc + 8);
    BOOL by_ordinal = (desc[10] & 2) != 0;
    DWORD *cache = *(DWORD *const *)(desc + 12);
    WCHAR text[300], wide_library[MAX_PATH], wide_name[100];
    MultiByteToWideChar(CP_ACP, 0, library, -1, wide_library, MAX_PATH);
    UINT mode = SetErrorMode(0x8001);
    if (cache[1] < 0x20)
        cache[1] = (DWORD)(ULONG_PTR)LoadLibraryA(library);
    SetErrorMode(mode);
    if (cache[1] < 0x20) {
        cache[1] = 0;
        wsprintfW(text, L"File not found: %s", wide_library);
        raise_described(48, text);
    }
    FARPROC address = GetProcAddress((HMODULE)(ULONG_PTR)cache[1], by_ordinal ? (LPCSTR)(ULONG_PTR)ordinal : name);
    if (!address) {
        if (by_ordinal)
            wsprintfW(wide_name, L"#%d", (int)ordinal);
        else
            MultiByteToWideChar(CP_ACP, 0, name, -1, wide_name, 100);
        FreeLibrary((HMODULE)(ULONG_PTR)cache[1]);
        cache[1] = 0;
        wsprintfW(text, L"Can't find DLL entry point %s in %s", wide_name, wide_library);
        raise_described(453, text);
    }
    cache[2] = (DWORD)(ULONG_PTR)address;
    return (void *)address;
}
