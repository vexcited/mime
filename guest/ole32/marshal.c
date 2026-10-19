/*
 * marshaling within a process. an interface marshaled for another
 * apartment of the same process travels as an OBJREF whose standard part
 * names the object directly: mime has no proxies, so the unmarshaled
 * pointer is the object itself, a documented difference from windows,
 * which builds a proxy. objects implementing IMarshal, such as the free
 * threaded marshaler, write custom OBJREFs as on windows.
 */
#include "ole32_private.h"
#include <mime/win32/objidl.h>

#define OBJREF_SIGNATURE 0x574F454D /* "MEOW" */
#define OBJREF_STANDARD 0x1
#define OBJREF_CUSTOM 0x4
#define MSHLFLAGS_NORMAL 0
#define MSHLFLAGS_TABLESTRONG 1
#define MSHLFLAGS_TABLEWEAK 2
#define MSHCTX_INPROC 3
#define CO_E_OBJNOTCONNECTED ((HRESULT)0x800401FDL)
#define RPC_E_CALL_COMPLETE ((HRESULT)0x80010117L)
#define RPC_E_INVALID_OBJREF ((HRESULT)0x8001011DL)

typedef struct IMarshal IMarshal;

typedef struct IMarshalVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IMarshal *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IMarshal *);
    ULONG(STDMETHODCALLTYPE *Release)(IMarshal *);
    HRESULT(STDMETHODCALLTYPE *GetUnmarshalClass)(IMarshal *, REFIID, void *, DWORD, void *, DWORD, CLSID *);
    HRESULT(STDMETHODCALLTYPE *GetMarshalSizeMax)(IMarshal *, REFIID, void *, DWORD, void *, DWORD, DWORD *);
    HRESULT(STDMETHODCALLTYPE *MarshalInterface)(IMarshal *, IStream *, REFIID, void *, DWORD, void *, DWORD);
    HRESULT(STDMETHODCALLTYPE *UnmarshalInterface)(IMarshal *, IStream *, REFIID, void **);
    HRESULT(STDMETHODCALLTYPE *ReleaseMarshalData)(IMarshal *, IStream *);
    HRESULT(STDMETHODCALLTYPE *DisconnectObject)(IMarshal *, DWORD);
} IMarshalVtbl;

struct IMarshal {
    const IMarshalVtbl *lpVtbl;
};

static const IID IID_IMarshal = {0x00000003, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID IID_IAgileObject = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const IID IID_IAgileReference = {0xc03f6a43, 0x65a4, 0x9818, {0x98, 0x7e, 0xe0, 0xb8, 0x10, 0xd2, 0xa6, 0xf2}};
static const CLSID CLSID_InProcFreeMarshaler = {0x0000033A, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

/* the common head of every OBJREF. */
typedef struct OBJREF_HEAD {
    DWORD Signature;
    DWORD Flags;
    IID Iid;
} OBJREF_HEAD;

/* the standard part: where windows names an exporter, mime names the object and its marshaling. */
typedef struct OBJREF_DIRECT {
    DWORD MarshalFlags;
    DWORD ProcessId;
    ULONG64 Object;
    ULONG64 Reserved[2];
} OBJREF_DIRECT;

typedef struct OBJREF_CUSTOM_HEAD {
    CLSID Unmarshaler;
    DWORD Extension;
    DWORD Size;
} OBJREF_CUSTOM_HEAD;

static HRESULT write_all(IStream *stream, const void *data, ULONG size)
{
    ULONG done = 0;
    HRESULT hr = stream->lpVtbl->Write(stream, data, size, &done);
    return FAILED(hr) ? hr : done == size ? S_OK : STG_E_MEDIUMFULL;
}

static HRESULT read_all(IStream *stream, void *data, ULONG size)
{
    ULONG done = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, data, size, &done);
    return FAILED(hr) ? hr : done == size ? S_OK : STG_E_READFAULT;
}

/* ------------------------------------------------------------------ the free threaded marshaler */

/*
 * in process, the marshaler writes the object pointer itself, so every
 * apartment talks to the object directly; other contexts use standard
 * marshaling, as windows' marshaler hands them to the standard one.
 */
typedef struct FTM {
    IMarshal Marshal;
    IUnknown Inner;
    IUnknown *Outer;
    LONG Refs;
} FTM;

typedef struct FTM_DATA {
    DWORD Flags;
    DWORD Reserved;
    ULONG64 Object;
    GUID Check;
} FTM_DATA;

static const GUID g_ftm_check = {0x4d494d45, 0x4654, 0x4d31, {0x6d, 0x69, 0x6d, 0x65, 0x66, 0x74, 0x6d, 0x31}};

static HRESULT marshal_standard(IStream *stream, REFIID iid, IUnknown *object, DWORD flags);

static FTM *ftm_from_marshal(IMarshal *m)
{
    return CONTAINING_RECORD(m, FTM, Marshal);
}

static HRESULT STDMETHODCALLTYPE ftm_inner_qi(IUnknown *self, REFIID iid, void **out)
{
    FTM *f = CONTAINING_RECORD(self, FTM, Inner);
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown))
        *out = &f->Inner;
    else if (IsEqualGUID(iid, &IID_IMarshal))
        *out = &f->Marshal;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ((IUnknown *)*out)->lpVtbl->AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE ftm_inner_add_ref(IUnknown *self)
{
    return InterlockedIncrement(&CONTAINING_RECORD(self, FTM, Inner)->Refs);
}

static ULONG STDMETHODCALLTYPE ftm_inner_release(IUnknown *self)
{
    FTM *f = CONTAINING_RECORD(self, FTM, Inner);
    ULONG refs = InterlockedDecrement(&f->Refs);
    if (!refs)
        OleFree(f);
    return refs;
}

static const IUnknownVtbl g_ftm_inner_vtbl = {ftm_inner_qi, ftm_inner_add_ref, ftm_inner_release};

static HRESULT STDMETHODCALLTYPE ftm_qi(IMarshal *self, REFIID iid, void **out)
{
    FTM *f = ftm_from_marshal(self);
    return f->Outer->lpVtbl->QueryInterface(f->Outer, iid, out);
}

static ULONG STDMETHODCALLTYPE ftm_add_ref(IMarshal *self)
{
    FTM *f = ftm_from_marshal(self);
    return f->Outer->lpVtbl->AddRef(f->Outer);
}

static ULONG STDMETHODCALLTYPE ftm_release(IMarshal *self)
{
    FTM *f = ftm_from_marshal(self);
    return f->Outer->lpVtbl->Release(f->Outer);
}

static HRESULT STDMETHODCALLTYPE ftm_get_unmarshal_class(IMarshal *self, REFIID iid, void *object, DWORD context,
                                                          void *reserved, DWORD flags, CLSID *clsid)
{
    if (!clsid)
        return E_POINTER;
    *clsid = CLSID_InProcFreeMarshaler;
    return context == MSHCTX_INPROC ? S_OK : CO_E_NOT_SUPPORTED;
}

static HRESULT STDMETHODCALLTYPE ftm_get_marshal_size_max(IMarshal *self, REFIID iid, void *object, DWORD context,
                                                           void *reserved, DWORD flags, DWORD *size)
{
    if (!size)
        return E_POINTER;
    *size = sizeof(FTM_DATA);
    return context == MSHCTX_INPROC ? S_OK : CO_E_NOT_SUPPORTED;
}

static HRESULT STDMETHODCALLTYPE ftm_marshal_interface(IMarshal *self, IStream *stream, REFIID iid, void *object,
                                                       DWORD context, void *reserved, DWORD flags)
{
    if (context != MSHCTX_INPROC)
        return CO_E_NOT_SUPPORTED;
    IUnknown *unknown;
    HRESULT hr = ((IUnknown *)object)->lpVtbl->QueryInterface(object, iid, (void **)&unknown);
    if (FAILED(hr))
        return hr;
    FTM_DATA data = {flags, 0, (ULONG64)(ULONG_PTR)unknown, g_ftm_check};
    hr = write_all(stream, &data, sizeof(data));
    if (FAILED(hr) || flags == MSHLFLAGS_TABLEWEAK)
        unknown->lpVtbl->Release(unknown);
    return hr;
}

static HRESULT ftm_read(IStream *stream, FTM_DATA *data)
{
    HRESULT hr = read_all(stream, data, sizeof(*data));
    if (SUCCEEDED(hr) && !IsEqualGUID(&data->Check, &g_ftm_check))
        hr = E_UNEXPECTED;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ftm_unmarshal_interface(IMarshal *self, IStream *stream, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FTM_DATA data;
    HRESULT hr = ftm_read(stream, &data);
    if (FAILED(hr))
        return hr;
    IUnknown *unknown = (IUnknown *)(ULONG_PTR)data.Object;
    hr = unknown->lpVtbl->QueryInterface(unknown, iid, out);
    if (data.Flags == MSHLFLAGS_NORMAL)
        unknown->lpVtbl->Release(unknown);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ftm_release_marshal_data(IMarshal *self, IStream *stream)
{
    FTM_DATA data;
    HRESULT hr = ftm_read(stream, &data);
    if (SUCCEEDED(hr) && data.Flags != MSHLFLAGS_TABLEWEAK)
        ((IUnknown *)(ULONG_PTR)data.Object)->lpVtbl->Release((IUnknown *)(ULONG_PTR)data.Object);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ftm_disconnect_object(IMarshal *self, DWORD reserved)
{
    return S_OK;
}

static const IMarshalVtbl g_ftm_vtbl = {
    ftm_qi,
    ftm_add_ref,
    ftm_release,
    ftm_get_unmarshal_class,
    ftm_get_marshal_size_max,
    ftm_marshal_interface,
    ftm_unmarshal_interface,
    ftm_release_marshal_data,
    ftm_disconnect_object,
};

static FTM *new_ftm(IUnknown *outer)
{
    FTM *f = OleAllocZero(sizeof(*f));
    if (!f)
        return nullptr;
    f->Marshal.lpVtbl = &g_ftm_vtbl;
    f->Inner.lpVtbl = &g_ftm_inner_vtbl;
    f->Outer = outer ? outer : &f->Inner;
    f->Refs = 1;
    return f;
}

WINOLEAPI HRESULT WINAPI CoCreateFreeThreadedMarshaler(IUnknown *outer, IUnknown **result)
{
    if (!result)
        return E_INVALIDARG;
    FTM *f = new_ftm(outer);
    *result = f ? &f->Inner : nullptr;
    return f ? S_OK : E_OUTOFMEMORY;
}

/* the unmarshaler a custom OBJREF names; the free threaded one is built in. */
static HRESULT unmarshaler(REFCLSID clsid, IMarshal **out)
{
    if (IsEqualGUID(clsid, &CLSID_InProcFreeMarshaler)) {
        FTM *f = new_ftm(nullptr);
        if (!f)
            return E_OUTOFMEMORY;
        *out = &f->Marshal;
        return S_OK;
    }
    return CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, &IID_IMarshal, (void **)out);
}

/* ------------------------------------------------------------------ OBJREFs */

static HRESULT marshal_standard(IStream *stream, REFIID iid, IUnknown *object, DWORD flags)
{
    IUnknown *unknown;
    HRESULT hr = object->lpVtbl->QueryInterface(object, iid, (void **)&unknown);
    if (FAILED(hr))
        return hr;
    OBJREF_HEAD head = {OBJREF_SIGNATURE, OBJREF_STANDARD, *iid};
    OBJREF_DIRECT direct = {flags, GetCurrentProcessId(), (ULONG64)(ULONG_PTR)unknown, {}};
    hr = write_all(stream, &head, sizeof(head));
    if (SUCCEEDED(hr))
        hr = write_all(stream, &direct, sizeof(direct));
    if (FAILED(hr) || flags == MSHLFLAGS_TABLEWEAK)
        unknown->lpVtbl->Release(unknown);
    return hr;
}

static IMarshal *custom_marshal(IUnknown *object)
{
    IMarshal *marshal = nullptr;
    if (FAILED(object->lpVtbl->QueryInterface(object, &IID_IMarshal, (void **)&marshal)))
        return nullptr;
    return marshal;
}

WINOLEAPI HRESULT WINAPI CoGetMarshalSizeMax(ULONG *size, REFIID iid, IUnknown *object, DWORD destination,
                                             void *context, DWORD flags)
{
    if (!size)
        return E_POINTER;
    *size = 0;
    if (!object)
        return E_INVALIDARG;
    IMarshal *marshal = custom_marshal(object);
    if (marshal) {
        DWORD custom = 0;
        HRESULT hr = marshal->lpVtbl->GetMarshalSizeMax(marshal, iid, object, destination, context, flags, &custom);
        marshal->lpVtbl->Release(marshal);
        if (SUCCEEDED(hr)) {
            *size = sizeof(OBJREF_HEAD) + sizeof(OBJREF_CUSTOM_HEAD) + custom;
            return S_OK;
        }
    }
    *size = sizeof(OBJREF_HEAD) + sizeof(OBJREF_DIRECT);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoMarshalInterface(IStream *stream, REFIID iid, IUnknown *object, DWORD destination,
                                            void *context, DWORD flags)
{
    if (!stream || !object || !iid)
        return E_INVALIDARG;
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr))
        return hr;
    IMarshal *marshal = custom_marshal(object);
    if (marshal) {
        CLSID clsid;
        DWORD size = 0;
        hr = marshal->lpVtbl->GetUnmarshalClass(marshal, iid, object, destination, context, flags, &clsid);
        if (SUCCEEDED(hr))
            hr = marshal->lpVtbl->GetMarshalSizeMax(marshal, iid, object, destination, context, flags, &size);
        if (SUCCEEDED(hr)) {
            OBJREF_HEAD head = {OBJREF_SIGNATURE, OBJREF_CUSTOM, *iid};
            OBJREF_CUSTOM_HEAD custom = {clsid, 0, size};
            hr = write_all(stream, &head, sizeof(head));
            if (SUCCEEDED(hr))
                hr = write_all(stream, &custom, sizeof(custom));
            if (SUCCEEDED(hr))
                hr = marshal->lpVtbl->MarshalInterface(marshal, stream, iid, object, destination, context, flags);
            marshal->lpVtbl->Release(marshal);
            return hr;
        }
        marshal->lpVtbl->Release(marshal);
    }
    return marshal_standard(stream, iid, object, flags);
}

static HRESULT read_objref(IStream *stream, OBJREF_HEAD *head)
{
    HRESULT hr = read_all(stream, head, sizeof(*head));
    if (FAILED(hr))
        return hr;
    if (head->Signature != OBJREF_SIGNATURE ||
        (head->Flags != OBJREF_STANDARD && head->Flags != OBJREF_CUSTOM))
        return RPC_E_INVALID_OBJREF;
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoUnmarshalInterface(IStream *stream, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!stream || !iid)
        return E_INVALIDARG;
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr))
        return hr;
    OBJREF_HEAD head;
    hr = read_objref(stream, &head);
    if (FAILED(hr))
        return hr;
    REFIID wanted = IsEqualGUID(iid, &GUID_NULL) ? &head.Iid : iid;
    if (head.Flags == OBJREF_CUSTOM) {
        OBJREF_CUSTOM_HEAD custom;
        IMarshal *marshal;
        hr = read_all(stream, &custom, sizeof(custom));
        if (SUCCEEDED(hr))
            hr = unmarshaler(&custom.Unmarshaler, &marshal);
        if (FAILED(hr))
            return hr;
        hr = marshal->lpVtbl->UnmarshalInterface(marshal, stream, wanted, result);
        marshal->lpVtbl->Release(marshal);
        return hr;
    }
    OBJREF_DIRECT direct;
    hr = read_all(stream, &direct, sizeof(direct));
    if (FAILED(hr))
        return hr;
    if (direct.ProcessId != GetCurrentProcessId())
        return CO_E_OBJNOTCONNECTED;
    IUnknown *unknown = (IUnknown *)(ULONG_PTR)direct.Object;
    hr = unknown->lpVtbl->QueryInterface(unknown, wanted, result);
    if (direct.MarshalFlags == MSHLFLAGS_NORMAL)
        unknown->lpVtbl->Release(unknown);
    return hr;
}

WINOLEAPI HRESULT WINAPI CoReleaseMarshalData(IStream *stream)
{
    if (!stream)
        return E_INVALIDARG;
    OBJREF_HEAD head;
    HRESULT hr = read_objref(stream, &head);
    if (FAILED(hr))
        return hr;
    if (head.Flags == OBJREF_CUSTOM) {
        OBJREF_CUSTOM_HEAD custom;
        IMarshal *marshal;
        hr = read_all(stream, &custom, sizeof(custom));
        if (SUCCEEDED(hr))
            hr = unmarshaler(&custom.Unmarshaler, &marshal);
        if (FAILED(hr))
            return hr;
        hr = marshal->lpVtbl->ReleaseMarshalData(marshal, stream);
        marshal->lpVtbl->Release(marshal);
        return hr;
    }
    OBJREF_DIRECT direct;
    hr = read_all(stream, &direct, sizeof(direct));
    if (FAILED(hr))
        return hr;
    if (direct.ProcessId != GetCurrentProcessId())
        return CO_E_OBJNOTCONNECTED;
    if (direct.MarshalFlags != MSHLFLAGS_TABLEWEAK)
        ((IUnknown *)(ULONG_PTR)direct.Object)->lpVtbl->Release((IUnknown *)(ULONG_PTR)direct.Object);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoMarshalInterThreadInterfaceInStream(REFIID iid, IUnknown *object, IStream **stream)
{
    if (!stream)
        return E_INVALIDARG;
    *stream = nullptr;
    IStream *s;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &s);
    if (FAILED(hr))
        return hr;
    hr = CoMarshalInterface(s, iid, object, MSHCTX_INPROC, nullptr, MSHLFLAGS_NORMAL);
    if (SUCCEEDED(hr)) {
        LARGE_INTEGER zero = {};
        s->lpVtbl->Seek(s, zero, 0, nullptr);
        *stream = s;
    } else {
        s->lpVtbl->Release(s);
    }
    return hr;
}

WINOLEAPI HRESULT WINAPI CoGetInterfaceAndReleaseStream(IStream *stream, REFIID iid, void **result)
{
    if (!stream)
        return E_INVALIDARG;
    HRESULT hr = CoUnmarshalInterface(stream, iid, result);
    stream->lpVtbl->Release(stream);
    return hr;
}

/* ------------------------------------------------------------------ agile references */

typedef struct IAgileReference IAgileReference;

typedef struct IAgileReferenceVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IAgileReference *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IAgileReference *);
    ULONG(STDMETHODCALLTYPE *Release)(IAgileReference *);
    HRESULT(STDMETHODCALLTYPE *Resolve)(IAgileReference *, REFIID, void **);
} IAgileReferenceVtbl;

struct IAgileReference {
    const IAgileReferenceVtbl *lpVtbl;
};

typedef struct AGILE_REF {
    IAgileReference Interface;
    LONG Refs;
    IUnknown *Object;
} AGILE_REF;

static HRESULT STDMETHODCALLTYPE agile_qi(IAgileReference *self, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileReference) ||
        IsEqualGUID(iid, &IID_IAgileObject)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE agile_add_ref(IAgileReference *self)
{
    return InterlockedIncrement(&((AGILE_REF *)self)->Refs);
}

static ULONG STDMETHODCALLTYPE agile_release(IAgileReference *self)
{
    AGILE_REF *a = (AGILE_REF *)self;
    ULONG refs = InterlockedDecrement(&a->Refs);
    if (!refs) {
        a->Object->lpVtbl->Release(a->Object);
        OleFree(a);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE agile_resolve(IAgileReference *self, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    AGILE_REF *a = (AGILE_REF *)self;
    return a->Object->lpVtbl->QueryInterface(a->Object, iid, out);
}

static const IAgileReferenceVtbl g_agile_vtbl = {agile_qi, agile_add_ref, agile_release, agile_resolve};

WINOLEAPI HRESULT WINAPI RoGetAgileReference(DWORD options, REFIID iid, IUnknown *object, IAgileReference **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (options > 1 || !object)
        return E_INVALIDARG;
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr))
        return hr;
    IUnknown *held;
    hr = object->lpVtbl->QueryInterface(object, iid, (void **)&held);
    if (FAILED(hr))
        return hr;
    AGILE_REF *a = OleAllocZero(sizeof(*a));
    if (!a) {
        held->lpVtbl->Release(held);
        return E_OUTOFMEMORY;
    }
    a->Interface.lpVtbl = &g_agile_vtbl;
    a->Refs = 1;
    a->Object = held;
    *result = &a->Interface;
    return S_OK;
}

/* ------------------------------------------------------------------ servers */

static LONG g_server_refs;

/* the process wide reference of a local server; the last release suspends its class objects. */
WINOLEAPI ULONG WINAPI CoAddRefServerProcess(void)
{
    return (ULONG)InterlockedIncrement(&g_server_refs);
}

WINOLEAPI ULONG WINAPI CoReleaseServerProcess(void)
{
    LONG refs = InterlockedDecrement(&g_server_refs);
    if (refs < 0) {
        InterlockedIncrement(&g_server_refs);
        return 0;
    }
    return (ULONG)refs;
}

WINOLEAPI HRESULT WINAPI CoResumeClassObjects(void)
{
    OleLocalServersResume();
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoSuspendClassObjects(void)
{
    return S_OK;
}

/* the context of the call being served: the object that CoSwitchCallContext put there, or RPC_E_CALL_COMPLETE. */
WINOLEAPI HRESULT WINAPI CoGetCallContext(REFIID iid, void **context)
{
    if (!context)
        return E_INVALIDARG;
    *context = nullptr;
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t || !t->CallContext)
        return RPC_E_CALL_COMPLETE;
    return t->CallContext->lpVtbl->QueryInterface(t->CallContext, iid, context);
}

/* only a proxy has a server process to let forward; a direct pointer has none. */
WINOLEAPI HRESULT WINAPI CoAllowSetForegroundWindow(IUnknown *object, void *reserved)
{
    if (reserved || !object)
        return E_INVALIDARG;
    return E_NOINTERFACE;
}

/* ------------------------------------------------------------------ proxy and stub classes */

/*
 * the class that marshals an interface. A class registered by CoRegisterPSClsid in this process comes first, then
 * the ProxyStubClsid32 of the interface in the registry. Nothing is marshaled by a proxy and a stub here, so the
 * answer only serves programs that ask. An interface nothing is registered for is REGDB_E_IIDNOTREG.
 */
#define PS_MAX 128

static struct {
    IID Interface;
    CLSID Class;
} g_ps_table[PS_MAX];
static ULONG g_ps_count;
static SRWLOCK g_ps_lock = SRWLOCK_INIT;

WINOLEAPI HRESULT WINAPI CoRegisterPSClsid(REFIID iid, REFCLSID clsid)
{
    if (!iid || !clsid)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&g_ps_lock);
    ULONG i;
    for (i = 0; i < g_ps_count; i++)
        if (IsEqualIID(&g_ps_table[i].Interface, iid))
            break;
    if (i == g_ps_count && g_ps_count == PS_MAX) {
        hr = E_OUTOFMEMORY;
    } else {
        g_ps_table[i].Interface = *iid;
        g_ps_table[i].Class = *clsid;
        if (i == g_ps_count)
            g_ps_count++;
    }
    ReleaseSRWLockExclusive(&g_ps_lock);
    return hr;
}

WINOLEAPI HRESULT WINAPI CoGetPSClsid(REFIID iid, CLSID *clsid)
{
    if (!iid || !clsid)
        return E_INVALIDARG;
    AcquireSRWLockShared(&g_ps_lock);
    for (ULONG i = 0; i < g_ps_count; i++) {
        if (IsEqualIID(&g_ps_table[i].Interface, iid)) {
            *clsid = g_ps_table[i].Class;
            ReleaseSRWLockShared(&g_ps_lock);
            return S_OK;
        }
    }
    ReleaseSRWLockShared(&g_ps_lock);
    WCHAR path[96], text[64];
    wcscpy(path, L"Interface\\");
    OleFormatGuid(iid, path + 10);
    wcscat(path, L"\\ProxyStubClsid32");
    DWORD size = sizeof(text);
    if (RegGetValueW(HKEY_CLASSES_ROOT, path, nullptr, RRF_RT_REG_SZ, nullptr, text, &size))
        return REGDB_E_IIDNOTREG;
    return OleParseGuid(text, clsid) ? S_OK : REGDB_E_IIDNOTREG;
}
