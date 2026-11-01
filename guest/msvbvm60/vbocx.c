/*
 * the container of the controls of other libraries: a control of a form that is not one of Visual Basic is an object of a library
 * the program refers to, made from the class of its record in the external table, loaded from the data the form holds for it and
 * activated in place in the window of its container. The program gets the extender of the control from the form, an object that
 * answers for the members the container gives every control and hands everything else to the control. The events of the control come
 * to a sink that calls the procedure of the form for the event.
 */
#include "vbctl.h"
#include <mime/win32/olectl.h>
#include <mime/win32/objidl.h>

#define S __stdcall
#define EXTENDER_ID 0x70000000

typedef struct VbOcx VbOcx;

/* the objects the container gives the control, each of them a pointer to a table of functions as the objects of COM are. */
typedef struct {
    void *const *client;
    void *const *inplace;
    void *const *control;
    void *const *ambient;
    void *const *sink;
    VbOcx *ocx;
} VbSite;

struct VbOcx {
    VbSite site;
    VbCtl *ctl;
    /* the interfaces of the control, each counted. */
    void *unknown;
    void *object;
    void *inplace_object;
    IDispatch *dispatch;
    /* the window of the control after it was activated. */
    HWND window;
    BOOL active;
    /* the events: the interface the control sends them by, its description and the connection. */
    GUID events;
    BOOL has_events;
    ITypeInfo *events_info;
    void *point;
    DWORD cookie;
    /* what the extender keeps for the control. */
    BSTR tag;
    BSTR tooltip;
    int align;
};

static const GUID IID_ClassFactory2 = {0xB196B28F, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};
static const GUID IID_OleObject = {0x112, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_OleInPlaceObject = {0x113, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_OleClientSite = {0x118, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_OleWindow = {0x114, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_OleInPlaceSite = {0x119, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_OleControlSite = {0xB196B289, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};
static const GUID IID_PersistStreamInit = {0x7FD52380, 0x4E07, 0x101B, {0xAE, 0x2D, 0x08, 0x00, 0x2B, 0x2E, 0xC7, 0x13}};
static const GUID IID_ProvideClassInfo2 = {0xA6BC3AC0, 0xDBAA, 0x11CE, {0x9D, 0xE3, 0x00, 0xAA, 0x00, 0x4B, 0xB8, 0x51}};
static const GUID IID_OleControl = {0xB196B288, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};

#define OLEIVERB_SHOW (-1)
#define OLEIVERB_INPLACEACTIVATE (-5)

#define SLOT(p, i) ((*(void ***)(p))[i])

static void release(void *p)
{
    if (p)
        ((ULONG(S *)(void *))SLOT(p, 2))(p);
}

static HRESULT query(void *p, const GUID *iid, void **out)
{
    return ((HRESULT(S *)(void *, const GUID *, void **))SLOT(p, 0))(p, iid, out);
}

static BOOL same(const GUID *a, const GUID *b)
{
    return IsEqualGUID(a, b);
}

/* ------------------------------------------------------------------ the record of the library of a control */

/* the records of the external table are the libraries and controls the program refers to: sizes and offsets of the parts of each. */
enum { REC_GUIDS = 1, REC_KEY = 7, REC_KEY_SIZE = 8, REC_FILE = 10, REC_PROGID = 11, REC_CLASS = 12 };

static const BYTE *find_record(const char *progid)
{
    const BYTE *p = (const BYTE *)(ULONG_PTR)g_vb_header->external_table;
    for (int i = 0; i < g_vb_header->external_count && p; i++) {
        const DWORD *w = (const DWORD *)p;
        if (!w[0] || w[0] > 0x2000)
            break;
        if (w[REC_PROGID] && !lstrcmpiA((const char *)p + w[REC_PROGID], progid))
            return p;
        p += w[0];
    }
    return nullptr;
}

/* ------------------------------------------------------------------ the site the control is given */

#define SITE(this_, field) ((VbSite *)((char *)(this_) - __builtin_offsetof(VbSite, field)))

static HRESULT S site_query(VbSite *s, const GUID *iid, void **out)
{
    *out = nullptr;
    if (same(iid, &IID_IUnknown) || same(iid, &IID_OleClientSite))
        *out = &s->client;
    else if (same(iid, &IID_OleInPlaceSite) || same(iid, &IID_OleWindow))
        *out = &s->inplace;
    else if (same(iid, &IID_OleControlSite))
        *out = &s->control;
    else if (same(iid, &IID_IDispatch))
        *out = &s->ambient;
    return *out ? S_OK : E_NOINTERFACE;
}

static HRESULT S client_query(void *p, const GUID *iid, void **out) { return site_query(SITE(p, client), iid, out); }
static HRESULT S inplace_query(void *p, const GUID *iid, void **out) { return site_query(SITE(p, inplace), iid, out); }
static HRESULT S control_query(void *p, const GUID *iid, void **out) { return site_query(SITE(p, control), iid, out); }
static HRESULT S ambient_query(void *p, const GUID *iid, void **out) { return site_query(SITE(p, ambient), iid, out); }
/* the site lives as long as the control it was made for. */
static ULONG S site_ref(void *p) { return 1; }

/* every function says how many bytes of arguments it takes, as the callee of stdcall removes them. */
static HRESULT S ok4(void *p) { return S_OK; }
static HRESULT S ok8(void *p, DWORD a) { return S_OK; }
static HRESULT S fail4(void *p) { return E_NOTIMPL; }
static HRESULT S fail12(void *p, DWORD a, DWORD b) { return E_NOTIMPL; }
static HRESULT S fail16(void *p, DWORD a, DWORD b, DWORD c) { return E_NOTIMPL; }

static HRESULT S client_container(void *p, void **out)
{
    *out = nullptr;
    return E_NOINTERFACE;
}

static HRESULT S inplace_window(void *p, HWND *out)
{
    *out = VbCtlParentWindow(SITE(p, inplace)->ocx->ctl);
    return S_OK;
}

typedef struct {
    UINT cb;
    BOOL mdi;
    HWND frame;
    HANDLE accelerators;
    UINT accelerator_count;
} FrameInfo;

static HRESULT S inplace_context(void *p, void **frame, void **doc, RECT *position, RECT *clip, FrameInfo *info)
{
    VbCtl *c = SITE(p, inplace)->ocx->ctl;
    *frame = nullptr;
    *doc = nullptr;
    *position = c->bounds;
    *clip = c->bounds;
    info->cb = sizeof(*info);
    info->mdi = FALSE;
    info->frame = c->form->self->hwnd;
    info->accelerators = nullptr;
    info->accelerator_count = 0;
    return S_OK;
}

static HRESULT S inplace_position(void *p, const RECT *rect) { return S_OK; }

static HRESULT S control_extended(void *p, void **out)
{
    VbCtl *c = SITE(p, control)->ocx->ctl;
    InterlockedIncrement(&c->refs);
    *out = c;
    return S_OK;
}

static HRESULT S control_lock(void *p, BOOL lock) { return S_OK; }

/* between the units of the container, which are twips, and the himetric of the control. */
static HRESULT S control_transform(void *p, const POINT *himetric, float *container, DWORD flags)
{
    BOOL size = flags & 1, to_himetric = flags & 2;
    (void)size;
    if (to_himetric) {
        ((LONG *)himetric)[0] = (LONG)(container[0] * 2540.0f / 1440.0f);
        ((LONG *)himetric)[1] = (LONG)(container[1] * 2540.0f / 1440.0f);
    } else {
        container[0] = himetric->x * 1440.0f / 2540.0f;
        container[1] = himetric->y * 1440.0f / 2540.0f;
    }
    return S_OK;
}

static HRESULT S control_accelerator(void *p, const MSG *msg, DWORD modifiers) { return S_FALSE; }

/* the ambient properties of the form, by their numbers. */
static HRESULT S ambient_count(void *p, UINT *n)
{
    *n = 0;
    return S_OK;
}

static HRESULT S ambient_info(void *p, UINT index, LCID lcid, void **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT S ambient_names(void *p, const GUID *iid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    for (UINT i = 0; i < count; i++)
        ids[i] = DISPID_UNKNOWN;
    return DISP_E_UNKNOWNNAME;
}

static IDispatch *make_font(VbCtl *form)
{
    FONTDESC fd = {sizeof(fd)};
    WCHAR name[64] = L"MS Sans Serif";
    if (form->font_name)
        MultiByteToWideChar(CP_ACP, 0, form->font_name, -1, name, 64);
    fd.lpstrName = name;
    float size = form->font_size > 0 ? form->font_size : 8.25f;
    fd.cySize.int64 = (LONGLONG)(size * 10000.0f);
    fd.sWeight = (SHORT)(form->font_weight ? form->font_weight : 400);
    fd.sCharset = DEFAULT_CHARSET;
    fd.fItalic = form->font_italic;
    fd.fUnderline = form->font_underline;
    fd.fStrikethrough = form->font_strike;
    IDispatch *font = nullptr;
    if (FAILED(OleCreateFontIndirect(&fd, &IID_IFontDisp, (void **)&font)))
        return nullptr;
    return font;
}

static HRESULT S ambient_invoke(void *p, DISPID id, const GUID *iid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *ei, UINT *bad)
{
    VbCtl *form = SITE(p, ambient)->ocx->ctl->form->self;
    if (!result || !(flags & DISPATCH_PROPERTYGET))
        return DISP_E_MEMBERNOTFOUND;
    VariantInit(result);
    switch (id) {
    case -701: /* BackColor */
        V_VT(result) = VT_I4;
        V_I4(result) = (LONG)VbCtlNumber(form, "BackColor");
        return S_OK;
    case -702: /* DisplayName */
        V_VT(result) = VT_BSTR;
        V_BSTR(result) = SysAllocString(SITE(p, ambient)->ocx->ctl->name[0] ? L"" : L"");
        return S_OK;
    case -703: { /* Font */
        IDispatch *font = make_font(form);
        if (!font)
            return DISP_E_MEMBERNOTFOUND;
        V_VT(result) = VT_DISPATCH;
        V_DISPATCH(result) = font;
        return S_OK;
    }
    case -704: /* ForeColor */
        V_VT(result) = VT_I4;
        V_I4(result) = (LONG)VbCtlNumber(form, "ForeColor");
        return S_OK;
    case -705: /* LocaleID */
        V_VT(result) = VT_I4;
        V_I4(result) = (LONG)VbLocale();
        return S_OK;
    case -706: /* MessageReflect */
    case -709: /* UserMode */
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = VARIANT_TRUE;
        return S_OK;
    case -707: /* ScaleUnits */
        V_VT(result) = VT_BSTR;
        V_BSTR(result) = SysAllocString(L"Twip");
        return S_OK;
    case -708: /* TextAlign */
        V_VT(result) = VT_I2;
        V_I2(result) = 0;
        return S_OK;
    case -710: /* UIDead */
    case -711: /* ShowGrabHandles */
    case -712: /* ShowHatching */
    case -713: /* DisplayAsDefault */
    case -714: /* SupportsMnemonics */
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = VARIANT_FALSE;
        return S_OK;
    case -715: /* AutoClip */
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = VARIANT_TRUE;
        return S_OK;
    case -716: /* Appearance */
        V_VT(result) = VT_I2;
        V_I2(result) = 1;
        return S_OK;
    }
    return DISP_E_MEMBERNOTFOUND;
}

static void *const g_client[] = {client_query, site_ref, site_ref, ok4, fail16, client_container, ok4, ok8, ok4};
static void *const g_inplace[] = {inplace_query, site_ref, site_ref, inplace_window, ok8, ok4, ok4, ok4, inplace_context, fail12, ok8, ok4, ok4, fail4,
                                   inplace_position};
static void *const g_control[] = {control_query, site_ref, site_ref, ok4, control_lock, control_extended, control_transform, control_accelerator, ok8,
                                   fail4};
static void *const g_ambient[] = {ambient_query, site_ref, site_ref, ambient_count, ambient_info, ambient_names, ambient_invoke};

/* ------------------------------------------------------------------ the events */

static HRESULT S sink_query(void *p, const GUID *iid, void **out)
{
    *out = p;
    return S_OK;
}

/* the index of the event in the interface the control sends them by, which is the place of its procedure in the table of the form. */
static int event_index(VbOcx *o, DISPID id, FUNCDESC **desc)
{
    if (!o->events_info)
        return -1;
    TYPEATTR *attr = nullptr;
    if (FAILED(o->events_info->lpVtbl->GetTypeAttr(o->events_info, &attr)))
        return -1;
    int found = -1;
    for (UINT i = 0; i < attr->cFuncs && found < 0; i++) {
        FUNCDESC *fd = nullptr;
        if (FAILED(o->events_info->lpVtbl->GetFuncDesc(o->events_info, i, &fd)))
            continue;
        if (fd->memid == id) {
            found = (int)i;
            *desc = fd;
        } else {
            o->events_info->lpVtbl->ReleaseFuncDesc(o->events_info, fd);
        }
    }
    o->events_info->lpVtbl->ReleaseTypeAttr(o->events_info, attr);
    return found;
}

static HRESULT S sink_invoke(void *p, DISPID id, const GUID *iid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *ei, UINT *bad)
{
    VbOcx *o = SITE(p, sink)->ocx;
    VbCtl *c = o->ctl;
    FUNCDESC *fd = nullptr;
    int index = event_index(o, id, &fd);
    if (index < 0)
        return S_OK;
    DWORD table = c->entry ? *(const DWORD *)(c->entry + 0x18) : 0;
    void *handler = table ? (void *)(ULONG_PTR) * (const DWORD *)(table + (6 + index) * 4) : nullptr;
    HRESULT hr = S_OK;
    if (handler && (ULONG_PTR)handler >= 0x1000) {
        DWORD argv[64];
        int n = 0;
        argv[n++] = (DWORD)(ULONG_PTR)(c->form->instance + *(const DWORD *)(c->entry + 4));
        for (int i = (int)params->cArgs - 1; i >= 0 && n < 56; i--) {
            VARIANT *v = &params->rgvarg[i];
            if (V_VT(v) & VT_BYREF) {
                argv[n++] = (DWORD)(ULONG_PTR)V_BYREF(v);
                continue;
            }
            switch (V_VT(v)) {
            case VT_I2:
            case VT_BOOL: argv[n++] = (DWORD)(LONG)V_I2(v); break;
            case VT_UI1: argv[n++] = V_UI1(v); break;
            case VT_R8:
            case VT_DATE:
            case VT_CY:
            case VT_I8:
                argv[n++] = ((const DWORD *)&V_R8(v))[0];
                argv[n++] = ((const DWORD *)&V_R8(v))[1];
                break;
            case VT_VARIANT:
                memcpy(&argv[n], v, 16);
                n += 4;
                break;
            default: argv[n++] = (DWORD)V_I4(v); break;
            }
        }
        VbCallMember(handler, argv, n);
    }
    o->events_info->lpVtbl->ReleaseFuncDesc(o->events_info, fd);
    return hr;
}

static void *const g_sink[] = {sink_query, site_ref, site_ref, ambient_count, ambient_info, ambient_names, sink_invoke};

static void connect_events(VbOcx *o, const BYTE *rec)
{
    void *container = nullptr;
    if (FAILED(query(o->unknown, &IID_IConnectionPointContainer, &container)))
        return;
    GUID iid = *(const GUID *)(rec + ((const DWORD *)rec)[REC_GUIDS] + 16);
    void *point = nullptr;
    HRESULT hr = ((HRESULT(S *)(void *, const GUID *, void **))SLOT(container, 4))(container, &iid, &point);
    if (FAILED(hr)) {
        /* the control says which interface it sends events by. */
        void *info = nullptr;
        if (SUCCEEDED(query(o->unknown, &IID_ProvideClassInfo2, &info))) {
            hr = ((HRESULT(S *)(void *, DWORD, GUID *))SLOT(info, 4))(info, 1, &iid);
            release(info);
            if (SUCCEEDED(hr))
                hr = ((HRESULT(S *)(void *, const GUID *, void **))SLOT(container, 4))(container, &iid, &point);
        }
    }
    release(container);
    if (FAILED(hr) || !point)
        return;
    o->events = iid;
    o->has_events = TRUE;
    o->point = point;
    /* the description of the events comes from the library of the control. */
    ITypeInfo *info = nullptr;
    ITypeLib *lib = nullptr;
    UINT index;
    if (o->dispatch && SUCCEEDED(o->dispatch->lpVtbl->GetTypeInfo(o->dispatch, 0, 0, &info)) &&
        SUCCEEDED(info->lpVtbl->GetContainingTypeLib(info, &lib, &index))) {
        lib->lpVtbl->GetTypeInfoOfGuid(lib, &iid, &o->events_info);
        lib->lpVtbl->Release(lib);
    }
    if (info)
        info->lpVtbl->Release(info);
    hr = ((HRESULT(S *)(void *, void *, DWORD *))SLOT(point, 5))(point, &o->site.sink, &o->cookie);
    if (FAILED(hr)) {
        release(point);
        o->point = nullptr;
    }
}

/* ------------------------------------------------------------------ making and activating the control */

static LONG himetric(double twips) { return (LONG)(twips * 2540.0 / 1440.0 + (twips < 0 ? -0.5 : 0.5)); }

static void raise_with_text(LONG number, const WCHAR *text)
{
    VbThread *t = VbThreadState();
    SysFreeString(t->err_description);
    t->err_description = SysAllocString(text);
    VbRaiseKeep(number);
}

/* a control that cannot be made is the error that says which component is missing. */
static void fail_creation(VbCtl *c, const BYTE *rec, HRESULT hr)
{
    WCHAR file[64] = L"", text[300];
    const DWORD *w = (const DWORD *)rec;
    if (rec && w[REC_FILE])
        MultiByteToWideChar(CP_ACP, 0, (const char *)rec + w[REC_FILE], -1, file, 64);
    char debug[160];
    wsprintfA(debug, "msvbvm60: the control %s of %s cannot be made: 0x%08lX\n", c->name, c->progid, (unsigned long)hr);
    VbOutputDebug(debug);
    switch ((ULONG)hr) {
    case 0x80040154: /* REGDB_E_CLASSNOTREG */
    case 0x80040111: /* CLASS_E_CLASSNOTAVAILABLE */
    case 0x8007007E: /* the library is not found */
    case 0x800700C1: /* the library is not a program for this system */
    case 0x80029C4A: /* the library of types cannot be loaded */
        wsprintfW(text, L"Component '%s' or one of its dependencies not correctly registered: a file is missing or invalid", file);
        raise_with_text(339, text);
    }
    VbRaiseHr(hr);
}

static HRESULT make_control(VbOcx *o, const BYTE *rec)
{
    const DWORD *w = (const DWORD *)rec;
    const GUID *clsid = (const GUID *)(rec + w[REC_GUIDS]);
    void *factory = nullptr;
    HRESULT hr = CoGetClassObject(clsid, CLSCTX_INPROC_SERVER, nullptr, &IID_ClassFactory2, &factory);
    if (SUCCEEDED(hr) && w[REC_KEY_SIZE]) {
        /* a licensed control is made with the runtime key the program holds. */
        BSTR key = SysAllocStringLen((const WCHAR *)(rec + w[REC_KEY]), w[REC_KEY_SIZE] / 2);
        hr = ((HRESULT(S *)(void *, void *, void *, const GUID *, BSTR, void **))SLOT(factory, 7))(factory, nullptr, nullptr, &IID_IUnknown, key,
                                                                                                      &o->unknown);
        SysFreeString(key);
        release(factory);
        return hr;
    }
    if (SUCCEEDED(hr))
        release(factory);
    return CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, &IID_IUnknown, &o->unknown);
}

/* the data the form kept for the control goes into it through a stream, or the control starts as new. */
static HRESULT load_control(VbOcx *o)
{
    VbCtl *c = o->ctl;
    void *persist = nullptr;
    HRESULT hr = query(o->unknown, &IID_PersistStreamInit, &persist);
    if (FAILED(hr))
        return S_OK;
    if (c->blob && c->blob_size) {
        void *stream = nullptr;
        hr = CreateStreamOnHGlobal(nullptr, TRUE, (IStream **)&stream);
        if (SUCCEEDED(hr)) {
            ULONG written;
            ((HRESULT(S *)(void *, const void *, ULONG, ULONG *))SLOT(stream, 4))(stream, c->blob, c->blob_size, &written);
            LARGE_INTEGER zero = {};
            ((HRESULT(S *)(void *, DWORD, DWORD, DWORD, void *))SLOT(stream, 5))(stream, zero.LowPart, zero.HighPart, 0, nullptr);
            hr = ((HRESULT(S *)(void *, void *))SLOT(persist, 5))(persist, stream);
            release(stream);
        }
    } else {
        hr = ((HRESULT(S *)(void *))SLOT(persist, 8))(persist);
    }
    release(persist);
    return hr;
}


/* the control gets its place in the window of its container, as a rectangle of pixels in it. */
static void position(VbOcx *o)
{
    VbCtl *c = o->ctl;
    if (!o->inplace_object)
        return;
    RECT r = c->bounds;
    ((HRESULT(S *)(void *, const RECT *, const RECT *))SLOT(o->inplace_object, 7))(o->inplace_object, &r, &r);
}

void VbOcxMoved(VbCtl *c)
{
    VbOcx *o = c->ocx;
    if (!o || !o->active)
        return;
    SIZEL extent = {himetric(VbCtlNumber(c, "Width")), himetric(VbCtlNumber(c, "Height"))};
    ((HRESULT(S *)(void *, DWORD, SIZEL *))SLOT(o->object, 17))(o->object, 1, &extent);
    position(o);
    if (o->window)
        RedrawWindow(o->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void VbOcxShow(VbCtl *c, BOOL visible)
{
    VbOcx *o = c->ocx;
    if (o && o->window)
        ShowWindow(o->window, visible ? SW_SHOWNA : SW_HIDE);
}

void VbOcxCreate(VbCtl *c)
{
    const BYTE *rec = find_record(c->progid);
    if (!rec)
        fail_creation(c, nullptr, (HRESULT)0x80040154);
    VbOcx *o = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*o));
    o->ctl = c;
    o->site.client = (void *const *)g_client;
    o->site.inplace = (void *const *)g_inplace;
    o->site.control = (void *const *)g_control;
    o->site.ambient = (void *const *)g_ambient;
    o->site.sink = (void *const *)g_sink;
    o->site.ocx = o;
    c->ocx = o;
    HRESULT hr = make_control(o, rec);
    if (FAILED(hr)) {
        c->ocx = nullptr;
        HeapFree(GetProcessHeap(), 0, o);
        fail_creation(c, rec, hr);
    }
    query(o->unknown, &IID_OleObject, &o->object);
    query(o->unknown, &IID_OleInPlaceObject, &o->inplace_object);
    query(o->unknown, &IID_IDispatch, (void **)&o->dispatch);
    if (!o->object) {
        VbOutputDebug("msvbvm60: a control without IOleObject cannot be hosted\n");
        VbRaiseHr(E_NOINTERFACE);
    }
    /* the order a container follows: the site, the data, the extent, the events, then the control goes live. */
    ((HRESULT(S *)(void *, void *))SLOT(o->object, 3))(o->object, &o->site.client);
    hr = load_control(o);
    if (FAILED(hr)) {
        char debug[120];
        wsprintfA(debug, "msvbvm60: the control %s did not load its data: 0x%08lX\n", c->name, (unsigned long)hr);
        VbOutputDebug(debug);
    }
    o->align = c->stream_align;
    SIZEL extent = {himetric(VbCtlNumber(c, "Width")), himetric(VbCtlNumber(c, "Height"))};
    ((HRESULT(S *)(void *, DWORD, SIZEL *))SLOT(o->object, 17))(o->object, 1, &extent);
    connect_events(o, rec);
    RECT r = c->bounds;
    HWND parent = VbCtlParentWindow(c);
    hr = ((HRESULT(S *)(void *, LONG, void *, void *, LONG, HWND, const RECT *))SLOT(o->object, 11))(o->object, OLEIVERB_INPLACEACTIVATE, nullptr,
                                                                                                          &o->site.client, 0, parent, &r);
    if (FAILED(hr))
        hr = ((HRESULT(S *)(void *, LONG, void *, void *, LONG, HWND, const RECT *))SLOT(o->object, 11))(o->object, OLEIVERB_SHOW, nullptr,
                                                                                                           &o->site.client, 0, parent, &r);
    o->active = SUCCEEDED(hr);
    if (o->inplace_object)
        ((HRESULT(S *)(void *, HWND *))SLOT(o->inplace_object, 3))(o->inplace_object, &o->window);
    c->hwnd = o->window;
    char debug[200];
    wsprintfA(debug, "msvbvm60: hosted %s (%s) window %p events %s active %d\n", c->name, c->progid, o->window, o->has_events ? "yes" : "no", o->active);
    VbOutputDebug(debug);
    position(o);
    if (o->window && VbCtlNumber(c, "Visible") == 0)
        ShowWindow(o->window, SW_HIDE);
    /* the control is told that its container is ready for the ambient properties. */
    void *control = nullptr;
    if (SUCCEEDED(query(o->unknown, &IID_OleControl, &control))) {
        ((HRESULT(S *)(void *, DISPID))SLOT(control, 5))(control, DISPID_UNKNOWN);
        release(control);
    }
}

void VbOcxDestroy(VbCtl *c)
{
    VbOcx *o = c->ocx;
    if (!o)
        return;
    c->ocx = nullptr;
    if (o->point) {
        ((HRESULT(S *)(void *, DWORD))SLOT(o->point, 6))(o->point, o->cookie);
        release(o->point);
    }
    if (o->events_info)
        o->events_info->lpVtbl->Release(o->events_info);
    if (o->inplace_object && o->active)
        ((HRESULT(S *)(void *))SLOT(o->inplace_object, 5))(o->inplace_object);
    if (o->object) {
        ((HRESULT(S *)(void *, DWORD))SLOT(o->object, 6))(o->object, 1);
        ((HRESULT(S *)(void *, void *))SLOT(o->object, 3))(o->object, nullptr);
    }
    if (o->dispatch)
        o->dispatch->lpVtbl->Release(o->dispatch);
    release(o->inplace_object);
    release(o->object);
    release(o->unknown);
    SysFreeString(o->tag);
    SysFreeString(o->tooltip);
    c->hwnd = nullptr;
    HeapFree(GetProcessHeap(), 0, o);
}

/* ------------------------------------------------------------------ the extender */

typedef enum {
    EXT_NAME = 1, EXT_INDEX, EXT_LEFT, EXT_TOP, EXT_WIDTH, EXT_HEIGHT, EXT_VISIBLE, EXT_TAG, EXT_PARENT, EXT_CONTAINER, EXT_TABINDEX, EXT_TABSTOP,
    EXT_OBJECT, EXT_TOOLTIP, EXT_MOVE, EXT_SETFOCUS, EXT_REFRESH, EXT_ZORDER, EXT_DRAG, EXT_DRAGMODE, EXT_HELPCONTEXT, EXT_WHATSTHIS, EXT_ENABLED,
    EXT_HWND, EXT_ALIGN
} ExtMember;

static const struct {
    const char *name;
    ExtMember id;
} g_extender_names[] = {
    {"Name", EXT_NAME}, {"Index", EXT_INDEX}, {"Left", EXT_LEFT}, {"Top", EXT_TOP}, {"Width", EXT_WIDTH}, {"Height", EXT_HEIGHT},
    {"Visible", EXT_VISIBLE}, {"Tag", EXT_TAG}, {"Parent", EXT_PARENT}, {"Container", EXT_CONTAINER}, {"TabIndex", EXT_TABINDEX},
    {"TabStop", EXT_TABSTOP}, {"Object", EXT_OBJECT}, {"ToolTipText", EXT_TOOLTIP}, {"Move", EXT_MOVE}, {"SetFocus", EXT_SETFOCUS},
    {"Refresh", EXT_REFRESH}, {"ZOrder", EXT_ZORDER}, {"Drag", EXT_DRAG}, {"DragMode", EXT_DRAGMODE}, {"HelpContextID", EXT_HELPCONTEXT},
    {"WhatsThisHelpID", EXT_WHATSTHIS}, {"Enabled", EXT_ENABLED}, {"hWnd", EXT_HWND}, {"Align", EXT_ALIGN},
};

static VbCtl *extender_of(void *self)
{
    VbCtl *c = *(VbCtl **)((BYTE *)self + 0x10);
    return c && c->magic == MAGIC && c->ocx ? c : nullptr;
}

static HRESULT S ext_query(void *self, const GUID *iid, void **out)
{
    VbCtl *c = extender_of(self);
    *out = nullptr;
    if (!c)
        return E_POINTER;
    if (same(iid, &IID_IUnknown) || same(iid, &IID_IDispatch)) {
        *out = self;
        InterlockedIncrement(&c->refs);
        return S_OK;
    }
    /* the control answers for its own interfaces, as the program asks the object of the form for the type of the control. */
    return query(c->ocx->unknown, iid, out);
}

static ULONG S ext_addref(void *self)
{
    VbCtl *c = *(VbCtl **)((BYTE *)self + 0x10);
    return c && c->magic == MAGIC ? (ULONG)InterlockedIncrement(&c->refs) : 1;
}

static ULONG S ext_release(void *self)
{
    VbCtl *c = *(VbCtl **)((BYTE *)self + 0x10);
    if (!c || c->magic != MAGIC)
        return 0;
    LONG n = InterlockedDecrement(&c->refs);
    return n > 0 ? (ULONG)n : 1;
}

static HRESULT S ext_count(void *self, UINT *n)
{
    VbCtl *c = extender_of(self);
    return c ? c->ocx->dispatch->lpVtbl->GetTypeInfoCount(c->ocx->dispatch, n) : E_POINTER;
}

static HRESULT S ext_info(void *self, UINT index, LCID lcid, void **info)
{
    VbCtl *c = extender_of(self);
    return c ? c->ocx->dispatch->lpVtbl->GetTypeInfo(c->ocx->dispatch, index, lcid, (ITypeInfo **)info) : E_POINTER;
}

static HRESULT S ext_names(void *self, const GUID *iid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    VbCtl *c = extender_of(self);
    if (!c)
        return E_POINTER;
    /* the control's own members win over the ones the container gives every control. */
    HRESULT hr = c->ocx->dispatch ? c->ocx->dispatch->lpVtbl->GetIDsOfNames(c->ocx->dispatch, iid, names, count, lcid, ids) : DISP_E_UNKNOWNNAME;
    if (SUCCEEDED(hr) || count != 1)
        return hr;
    char narrow[64];
    WideCharToMultiByte(CP_ACP, 0, names[0], -1, narrow, sizeof(narrow), nullptr, nullptr);
    for (size_t i = 0; i < sizeof(g_extender_names) / sizeof(g_extender_names[0]); i++)
        if (!lstrcmpiA(g_extender_names[i].name, narrow)) {
            ids[0] = EXTENDER_ID + g_extender_names[i].id;
            return S_OK;
        }
    return hr;
}

static void set_variant_long(VARIANT *result, LONG v, VARTYPE vt)
{
    if (!result)
        return;
    VariantClear(result);
    V_VT(result) = vt;
    V_I4(result) = v;
}

static double argument_number(const DISPPARAMS *params, int index, double fallback)
{
    if (index >= (int)params->cArgs)
        return fallback;
    VARIANT *v = &params->rgvarg[params->cArgs - 1 - index];
    if (V_VT(v) == VT_ERROR || V_VT(v) == VT_EMPTY)
        return fallback;
    VARIANT t;
    VariantInit(&t);
    if (FAILED(VariantChangeType(&t, v, 0, VT_R8)))
        return fallback;
    return V_R8(&t);
}

static HRESULT extender_member(VbCtl *c, ExtMember m, WORD flags, DISPPARAMS *params, VARIANT *result)
{
    VbOcx *o = c->ocx;
    BOOL put = (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0;
    double number = put && params->cArgs ? argument_number(params, 0, 0) : 0;
    switch (m) {
    case EXT_NAME:
        if (put)
            return DISP_E_TYPEMISMATCH;
        if (result) {
            WCHAR w[48];
            MultiByteToWideChar(CP_ACP, 0, c->name, -1, w, 48);
            VariantClear(result);
            V_VT(result) = VT_BSTR;
            V_BSTR(result) = SysAllocString(w);
        }
        return S_OK;
    case EXT_INDEX:
        if (result) {
            VariantClear(result);
            V_VT(result) = VT_I2;
            V_I2(result) = 0;
        }
        return S_OK;
    case EXT_LEFT:
    case EXT_TOP:
    case EXT_WIDTH:
    case EXT_HEIGHT: {
        const char *name = m == EXT_LEFT ? "Left" : m == EXT_TOP ? "Top" : m == EXT_WIDTH ? "Width" : "Height";
        if (put) {
            VbCtlSetNumber(c, name, number);
            VbCtlPlace(c);
        } else if (result) {
            VariantClear(result);
            V_VT(result) = VT_R4;
            V_R4(result) = (float)VbCtlNumber(c, name);
        }
        return S_OK;
    }
    case EXT_VISIBLE:
        if (put) {
            VbCtlSetNumber(c, "Visible", number ? -1 : 0);
            VbOcxShow(c, number != 0);
        } else if (o->window) {
            set_variant_long(result, IsWindowVisible(o->window) ? -1 : 0, VT_BOOL);
        } else {
            set_variant_long(result, VbCtlNumber(c, "Visible") ? -1 : 0, VT_BOOL);
        }
        return S_OK;
    case EXT_ENABLED:
        if (put) {
            if (o->window)
                EnableWindow(o->window, number != 0);
        } else {
            set_variant_long(result, o->window && !IsWindowEnabled(o->window) ? 0 : -1, VT_BOOL);
        }
        return S_OK;
    case EXT_TAG:
    case EXT_TOOLTIP: {
        BSTR *slot = m == EXT_TAG ? &o->tag : &o->tooltip;
        if (put) {
            VARIANT t;
            VariantInit(&t);
            if (!params->cArgs || FAILED(VariantChangeType(&t, &params->rgvarg[0], 0, VT_BSTR)))
                return DISP_E_TYPEMISMATCH;
            SysFreeString(*slot);
            *slot = V_BSTR(&t);
        } else if (result) {
            VariantClear(result);
            V_VT(result) = VT_BSTR;
            V_BSTR(result) = SysAllocString(*slot ? *slot : L"");
        }
        return S_OK;
    }
    case EXT_TABINDEX:
    case EXT_TABSTOP:
    case EXT_DRAGMODE:
    case EXT_HELPCONTEXT:
    case EXT_WHATSTHIS:
        if (!put)
            set_variant_long(result, 0, m == EXT_TABSTOP ? VT_BOOL : VT_I4);
        return S_OK;
    case EXT_PARENT:
    case EXT_CONTAINER:
        if (result) {
            VariantClear(result);
            V_VT(result) = VT_DISPATCH;
            V_DISPATCH(result) = (IDispatch *)c->form->instance;
            ((ULONG(S *)(void *))SLOT(c->form->instance, 1))(c->form->instance);
        }
        return S_OK;
    case EXT_OBJECT:
        if (result) {
            VariantClear(result);
            V_VT(result) = VT_DISPATCH;
            V_DISPATCH(result) = o->dispatch;
            o->dispatch->lpVtbl->AddRef(o->dispatch);
        }
        return S_OK;
    case EXT_HWND:
        set_variant_long(result, (LONG)(LONG_PTR)o->window, VT_I4);
        return S_OK;
    case EXT_ALIGN:
        if (put) {
            o->align = (int)number;
            VbFormDock(c->form);
        } else {
            set_variant_long(result, o->align, VT_I2);
        }
        return S_OK;
    case EXT_MOVE:
        if (!params->cArgs)
            return DISP_E_BADPARAMCOUNT;
        VbCtlSetNumber(c, "Left", argument_number(params, 0, VbCtlNumber(c, "Left")));
        VbCtlSetNumber(c, "Top", argument_number(params, 1, VbCtlNumber(c, "Top")));
        VbCtlSetNumber(c, "Width", argument_number(params, 2, VbCtlNumber(c, "Width")));
        VbCtlSetNumber(c, "Height", argument_number(params, 3, VbCtlNumber(c, "Height")));
        VbCtlPlace(c);
        return S_OK;
    case EXT_SETFOCUS:
        if (o->window)
            SetFocus(o->window);
        return S_OK;
    case EXT_REFRESH:
        if (o->window)
            RedrawWindow(o->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return S_OK;
    case EXT_ZORDER:
        if (o->window)
            SetWindowPos(o->window, number == 1 ? HWND_BOTTOM : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return S_OK;
    case EXT_DRAG:
        return S_OK;
    }
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT S ext_invoke(void *self, DISPID id, const GUID *iid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *ei, UINT *bad)
{
    VbCtl *c = extender_of(self);
    if (!c)
        return E_POINTER;
    if (id >= EXTENDER_ID && id < EXTENDER_ID + 0x1000)
        return extender_member(c, (ExtMember)(id - EXTENDER_ID), flags, params, result);
    HRESULT hr = c->ocx->dispatch->lpVtbl->Invoke(c->ocx->dispatch, id, iid, lcid, flags, params, result, ei, bad);
    if (FAILED(hr)) {
        char debug[160];
        wsprintfA(debug, "msvbvm60: Invoke of %d with flags %X on %s failed: 0x%08lX\n", (int)id, (unsigned)flags, c->name, (unsigned long)hr);
        VbOutputDebug(debug);
    }
    return hr;
}

#define EXTENDER_SLOTS 200

/* the slots of the interface of the extender that compiled code calls directly come here with their number. */
LONG VbOcxSlot(VbCtl *c, int slot, DWORD *args)
{
    char debug[120];
    wsprintfA(debug, "msvbvm60: slot %d of the extender of %s is called with %08lX %08lX\n", slot, c->name, (unsigned long)args[0], (unsigned long)args[1]);
    VbOutputDebug(debug);
    return E_NOTIMPL;
}

void **VbOcxVtable(void)
{
    static void **table;
    if (!table) {
        table = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, EXTENDER_SLOTS * sizeof(void *));
        void *const first[] = {ext_query, ext_addref, ext_release, ext_count, ext_info, ext_names, ext_invoke};
        for (int i = 0; i < EXTENDER_SLOTS; i++)
            table[i] = i < 7 ? first[i] : VbMakeThunk(i, 8);
    }
    return table;
}

int VbOcxAlign(VbCtl *c)
{
    return c->ocx ? c->ocx->align : 0;
}
