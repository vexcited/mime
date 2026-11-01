/*
 * objects of external classes, a search in a variant and the files of a program.
 */
#include "vb.h"

/* ------------------------------------------------------------------ New */

/*
 * the description of a class the compiler writes: flags, where 1 is a class of the project and 2 an object that is
 * running already, the class id, the interface asked for and a license key.
 */
typedef struct VbClassInfo {
    BYTE flags;
    BYTE reserved[3];
    const CLSID *clsid;
    const IID *iid;
    const WCHAR *license;
} VbClassInfo;

/* the factory that makes objects of a class that is licensed. */
typedef struct LicensedFactory LicensedFactory;
struct LicensedFactory {
    const struct {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(LicensedFactory *, REFIID, void **);
        ULONG(STDMETHODCALLTYPE *AddRef)(LicensedFactory *);
        ULONG(STDMETHODCALLTYPE *Release)(LicensedFactory *);
        HRESULT(STDMETHODCALLTYPE *CreateInstance)(LicensedFactory *, IUnknown *, REFIID, void **);
        HRESULT(STDMETHODCALLTYPE *LockServer)(LicensedFactory *, BOOL);
        HRESULT(STDMETHODCALLTYPE *GetLicInfo)(LicensedFactory *, void *);
        HRESULT(STDMETHODCALLTYPE *RequestLicKey)(LicensedFactory *, DWORD, BSTR *);
        HRESULT(STDMETHODCALLTYPE *CreateInstanceLic)(LicensedFactory *, IUnknown *, IUnknown *, REFIID, BSTR, void **);
    } *lpVtbl;
};

static const IID IID_LicensedFactory = {0xB196B28F, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};

VB_STD LONG vb___vbaInStr(LONG locale_mode, BSTR find, BSTR haystack, LONG start);

static HRESULT create_object(const VbClassInfo *info, void **object)
{
    HRESULT hr = E_FAIL;
    *object = nullptr;
    if (info->flags & 2) {
        IUnknown *running = nullptr;
        hr = GetActiveObject(info->clsid, nullptr, &running);
        if (SUCCEEDED(hr)) {
            hr = running->lpVtbl->QueryInterface(running, info->iid, object);
            running->lpVtbl->Release(running);
            return hr;
        }
    }
    if (IsEqualGUID(info->clsid, &CLSID_VbGlobal)) {
        void *global = VbNewGlobal();
        *object = global;
        ((IUnknown *)global)->lpVtbl->AddRef(global);
        return S_OK;
    }
    if (IsEqualGUID(info->clsid, &CLSID_VbCollection)) {
        void *collection = VbNewCollection();
        if (!collection)
            return E_OUTOFMEMORY;
        hr = ((IUnknown *)collection)->lpVtbl->QueryInterface(collection, info->iid, object);
        ((IUnknown *)collection)->lpVtbl->Release(collection);
        return hr;
    }
    if (info->license) {
        LicensedFactory *factory = nullptr;
        hr = CoGetClassObject(info->clsid, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, nullptr, &IID_LicensedFactory, (void **)&factory);
        if (FAILED(hr))
            return hr;
        BSTR key = SysAllocString(info->license);
        hr = factory->lpVtbl->CreateInstanceLic(factory, nullptr, nullptr, info->iid, key, object);
        SysFreeString(key);
        factory->lpVtbl->Release(factory);
        return hr;
    }
    return CoCreateInstance(info->clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, info->iid, object);
}

/* a class that fails to start is error 429, one that lacks the interface 430. */
static void make(const VbClassInfo *info, void **slot)
{
    if (info->flags & 1) {
        void *form = VbNewProjectObject(info);
        if (!form) {
            VbOutputDebug("msvbvm60: New of a class module of the project is not implemented\n");
            VbRaise(VBERR_CANNOT_CREATE_OBJECT);
        }
        *slot = form;
        return;
    }
    void *object;
    HRESULT hr = create_object(info, &object);
    if (FAILED(hr)) {
        char text[160];
        const CLSID *c = info->clsid;
        wsprintfA(text, "msvbvm60: New failed %08X for {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n", (unsigned)hr, (unsigned)c->Data1, c->Data2,
                  c->Data3, c->Data4[0], c->Data4[1], c->Data4[2], c->Data4[3], c->Data4[4], c->Data4[5], c->Data4[6], c->Data4[7]);
        VbOutputDebug(text);
        VbRaiseHr(hr);
    }
    *slot = object;
}

VB_STD void *vb___vbaNew(const VbClassInfo *info)
{
    void *object;
    make(info, &object);
    return object;
}

VB_STD LONG vb___vbaNew2(const VbClassInfo *info, void **slot)
{
    void *object;
    make(info, &object);
    *slot = object;
    return 0;
}

/* ------------------------------------------------------------------ InStr */

static BSTR text_of(VARIANT *v)
{
    VARIANT t;
    VariantInit(&t);
    HRESULT hr = VariantChangeTypeEx(&t, v, VbLocale(), 0, VT_BSTR);
    if (hr == DISP_E_TYPEMISMATCH && V_VT(v) == VT_DISPATCH)
        VbCheck(VariantChangeTypeEx(&t, v, VbLocale(), 0, VT_BSTR));
    VbCheck(hr);
    return V_BSTR(&t);
}

VB_STD VARIANT *vb___vbaInStrVar(VARIANT *dest, LONG mode, VARIANT *find, VARIANT *text, LONG start)
{
    VariantInit(dest);
    if (V_VT(text) == VT_NULL || V_VT(find) == VT_NULL) {
        V_VT(dest) = VT_NULL;
        return dest;
    }
    BSTR haystack = text_of(text), needle = text_of(find);
    LONG position = vb___vbaInStr(mode, needle, haystack, start);
    SysFreeString(haystack);
    SysFreeString(needle);
    V_VT(dest) = VT_I4;
    V_I4(dest) = position;
    return dest;
}
