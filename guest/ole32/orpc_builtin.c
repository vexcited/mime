/*
 * proxies and stubs for the interfaces ole32 itself carries between processes, such as IClassFactory, through which
 * every local server is reached, and INotificationActivationCallback, through which toasts are activated. they come
 * from a generated proxy file of stubless proxies like midl writes, and rpcrt4's interpreter does the rest.
 */
#include "orpc.h"
#include <mime/win32/notificationactivationcallback.h>
#include <mime/win32/shobjidl.h>

/* named by the generated proxy file: local forms of methods whose wire form differs. */
HRESULT STDMETHODCALLTYPE ISequentialStream_Read_Proxy(ISequentialStream *This, void *pv, ULONG cb, ULONG *read);
HRESULT STDMETHODCALLTYPE ISequentialStream_Write_Proxy(ISequentialStream *This, const void *pv, ULONG cb, ULONG *written);
HRESULT STDMETHODCALLTYPE IStream_Seek_Proxy(IStream *This, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position);
HRESULT STDMETHODCALLTYPE IStream_CopyTo_Proxy(IStream *This, IStream *to, ULARGE_INTEGER cb, ULARGE_INTEGER *read,
                                               ULARGE_INTEGER *written);
HRESULT STDMETHODCALLTYPE IEnumString_Next_Proxy(IEnumString *This, ULONG celt, LPOLESTR *items, ULONG *fetched);
HRESULT STDMETHODCALLTYPE IEnumUnknown_Next_Proxy(IEnumUnknown *This, ULONG celt, IUnknown **items, ULONG *fetched);
HRESULT STDMETHODCALLTYPE IEnumSTATSTG_Next_Proxy(IEnumSTATSTG *This, ULONG celt, STATSTG *items, ULONG *fetched);
HRESULT STDMETHODCALLTYPE ILockBytes_ReadAt_Proxy(ILockBytes *This, ULARGE_INTEGER offset, void *pv, ULONG cb, ULONG *read);
HRESULT STDMETHODCALLTYPE ILockBytes_WriteAt_Proxy(ILockBytes *This, ULARGE_INTEGER offset, const void *pv, ULONG cb,
                                                   ULONG *written);
HRESULT STDMETHODCALLTYPE IStorage_OpenStream_Proxy(IStorage *This, const OLECHAR *name, void *reserved1, DWORD mode,
                                                    DWORD reserved2, IStream **stream);
HRESULT STDMETHODCALLTYPE IStorage_OpenStream_Stub(IStorage *This, const OLECHAR *name, unsigned long cb, byte *reserved1,
                                                   unsigned long mode, unsigned long reserved2, IStream **stream);
HRESULT STDMETHODCALLTYPE IStorage_EnumElements_Proxy(IStorage *This, DWORD reserved1, void *reserved2, DWORD reserved3,
                                                      IEnumSTATSTG **enumerator);
HRESULT STDMETHODCALLTYPE IStorage_EnumElements_Stub(IStorage *This, unsigned long reserved1, unsigned long cb,
                                                     byte *reserved2, unsigned long reserved3, IEnumSTATSTG **enumerator);

#include "ole32/builtin_ps.h"

static BOOL builtin(REFIID iid)
{
    int index;
    return orpcbuiltin_IID_Lookup(iid, &index) != 0;
}

/* ------------------------------------------------------------------ methods with their own wire form */

HRESULT STDMETHODCALLTYPE ISequentialStream_Read_Proxy(ISequentialStream *This, void *pv, ULONG cb, ULONG *read)
{
    ULONG count = 0;
    HRESULT hr = ISequentialStream_RemoteRead_Proxy(This, pv, cb, &count);
    if (read)
        *read = count;
    return hr;
}

HRESULT STDMETHODCALLTYPE ISequentialStream_Write_Proxy(ISequentialStream *This, const void *pv, ULONG cb, ULONG *written)
{
    ULONG count = 0;
    HRESULT hr = ISequentialStream_RemoteWrite_Proxy(This, (byte *)pv, cb, &count);
    if (written)
        *written = count;
    return hr;
}

HRESULT STDMETHODCALLTYPE IStream_Seek_Proxy(IStream *This, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    ULARGE_INTEGER where;
    where.QuadPart = 0;
    HRESULT hr = IStream_RemoteSeek_Proxy(This, move, origin, &where);
    if (position)
        *position = where;
    return hr;
}

HRESULT STDMETHODCALLTYPE IStream_CopyTo_Proxy(IStream *This, IStream *to, ULARGE_INTEGER cb, ULARGE_INTEGER *read,
                                               ULARGE_INTEGER *written)
{
    ULARGE_INTEGER r, w;
    r.QuadPart = w.QuadPart = 0;
    HRESULT hr = IStream_RemoteCopyTo_Proxy(This, to, cb, &r, &w);
    if (read)
        *read = r;
    if (written)
        *written = w;
    return hr;
}

/* a caller asking for one item may omit the fetched count. */
#define ENUM_NEXT(iface, type)                                                                                         \
    HRESULT STDMETHODCALLTYPE iface##_Next_Proxy(iface *This, ULONG celt, type *items, ULONG *fetched)                  \
    {                                                                                                                  \
        ULONG count = 0;                                                                                               \
        if (!fetched && celt != 1)                                                                                     \
            return E_INVALIDARG;                                                                                       \
        HRESULT hr = iface##_RemoteNext_Proxy(This, celt, items, &count);                                              \
        if (fetched)                                                                                                   \
            *fetched = count;                                                                                          \
        return hr;                                                                                                     \
    }

ENUM_NEXT(IEnumString, LPOLESTR)
ENUM_NEXT(IEnumUnknown, IUnknown *)
ENUM_NEXT(IEnumSTATSTG, STATSTG)

HRESULT STDMETHODCALLTYPE ILockBytes_ReadAt_Proxy(ILockBytes *This, ULARGE_INTEGER offset, void *pv, ULONG cb, ULONG *read)
{
    ULONG count = 0;
    HRESULT hr = ILockBytes_RemoteReadAt_Proxy(This, offset, pv, cb, &count);
    if (read)
        *read = count;
    return hr;
}

HRESULT STDMETHODCALLTYPE ILockBytes_WriteAt_Proxy(ILockBytes *This, ULARGE_INTEGER offset, const void *pv, ULONG cb,
                                                   ULONG *written)
{
    ULONG count = 0;
    HRESULT hr = ILockBytes_RemoteWriteAt_Proxy(This, offset, (byte *)pv, cb, &count);
    if (written)
        *written = count;
    return hr;
}

/* the reserved storage arguments must be empty and are never sent. */
HRESULT STDMETHODCALLTYPE IStorage_OpenStream_Proxy(IStorage *This, const OLECHAR *name, void *reserved1, DWORD mode,
                                                    DWORD reserved2, IStream **stream)
{
    if (stream)
        *stream = nullptr;
    if (reserved1)
        return STG_E_INVALIDPARAMETER;
    return IStorage_RemoteOpenStream_Proxy(This, (unsigned short *)name, 0, nullptr, mode, reserved2, stream);
}

HRESULT STDMETHODCALLTYPE IStorage_OpenStream_Stub(IStorage *This, const OLECHAR *name, unsigned long cb, byte *reserved1,
                                                   unsigned long mode, unsigned long reserved2, IStream **stream)
{
    *stream = nullptr;
    if (cb || reserved1)
        return STG_E_INVALIDPARAMETER;
    return This->lpVtbl->OpenStream(This, name, nullptr, mode, reserved2, stream);
}

HRESULT STDMETHODCALLTYPE IStorage_EnumElements_Proxy(IStorage *This, DWORD reserved1, void *reserved2, DWORD reserved3,
                                                      IEnumSTATSTG **enumerator)
{
    if (enumerator)
        *enumerator = nullptr;
    if (reserved2)
        return STG_E_INVALIDPARAMETER;
    return IStorage_RemoteEnumElements_Proxy(This, reserved1, 0, nullptr, reserved3, enumerator);
}

HRESULT STDMETHODCALLTYPE IStorage_EnumElements_Stub(IStorage *This, unsigned long reserved1, unsigned long cb,
                                                     byte *reserved2, unsigned long reserved3, IEnumSTATSTG **enumerator)
{
    *enumerator = nullptr;
    if (cb || reserved2)
        return STG_E_INVALIDPARAMETER;
    return This->lpVtbl->EnumElements(This, reserved1, nullptr, reserved3, enumerator);
}

HRESULT OrpcBuiltinFactory(REFIID iid, IPSFactoryBuffer **factory)
{
    if (!builtin(iid))
        return E_NOINTERFACE;
    return orpcbuiltin_DllGetClassObject(&orpcbuiltin_ProxyClsid, &IID_IPSFactoryBuffer, (void **)factory);
}

/*
 * automation and connection point interfaces are marshaled by oleaut32's classes, as windows registers them: a type
 * library interface's proxy stub class is registered with the library, and these are the ones the system registers
 * itself. they are found in oleaut32 when no server is registered for them.
 */
#define OA_CLASS(n) {0x00020420 + (n), 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}

static const struct {
    IID Interface;
    CLSID Class;
} g_automation[] = {
    {{0x00020400, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(0)}, /* IDispatch */
    {{0x00020404, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(1)}, /* IEnumVARIANT */
    {{0x00020401, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(2)}, /* ITypeInfo */
    {{0x00020412, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(2)}, /* ITypeInfo2 */
    {{0x00020402, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(3)}, /* ITypeLib */
    {{0x00020411, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(3)}, /* ITypeLib2 */
    {{0x00020403, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(5)}, /* ITypeComp */
    {{0x1CF2B120, 0x547D, 0x101B, {0x8E, 0x65, 0x08, 0x00, 0x2B, 0x2B, 0xD1, 0x19}}, OA_CLASS(0)}, /* IErrorInfo */
    {{0x22F03340, 0x547D, 0x101B, {0x8E, 0x65, 0x08, 0x00, 0x2B, 0x2B, 0xD1, 0x19}}, OA_CLASS(0)}, /* ICreateErrorInfo */
    {{0xDF0B3D60, 0x548F, 0x101B, {0x8E, 0x65, 0x08, 0x00, 0x2B, 0x2B, 0xD1, 0x19}}, OA_CLASS(0)}, /* ISupportErrorInfo */
    {{0xB196B284, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}}, OA_CLASS(0)}, /* IConnectionPointContainer */
    {{0xB196B285, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}}, OA_CLASS(0)}, /* IEnumConnectionPoints */
    {{0xB196B286, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}}, OA_CLASS(0)}, /* IConnectionPoint */
    {{0xB196B287, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}}, OA_CLASS(0)}, /* IEnumConnections */
};

BOOL OrpcAutomationClass(REFIID iid, CLSID *clsid)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_automation); i++) {
        if (IsEqualGUID(iid, &g_automation[i].Interface)) {
            *clsid = g_automation[i].Class;
            return TRUE;
        }
    }
    return FALSE;
}

/* windows registers the automation proxy classes, here oleaut32 serves them unregistered. */
BOOL OrpcBuiltinServer(REFCLSID clsid, const WCHAR **dll)
{
    if (clsid->Data1 >= 0x00020420 && clsid->Data1 <= 0x00020426 && !clsid->Data2 && !clsid->Data3 &&
        clsid->Data4[0] == 0xC0 && clsid->Data4[7] == 0x46) {
        *dll = L"oleaut32.dll";
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ process global options */

/* CLSID_GlobalOptions settings are kept, but nothing reads them. */
typedef struct IGlobalOptions IGlobalOptions;
typedef struct IGlobalOptionsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IGlobalOptions *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IGlobalOptions *This);
    ULONG(STDMETHODCALLTYPE *Release)(IGlobalOptions *This);
    HRESULT(STDMETHODCALLTYPE *Set)(IGlobalOptions *This, DWORD property, ULONG_PTR value);
    HRESULT(STDMETHODCALLTYPE *Query)(IGlobalOptions *This, DWORD property, ULONG_PTR *value);
} IGlobalOptionsVtbl;
struct IGlobalOptions {
    const IGlobalOptionsVtbl *lpVtbl;
};

static const GUID g_clsid_global_options = {0x0000034B, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID g_iid_global_options = {0x0000015B, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
/* the runtime settings default to 0x100: the class catalog is the machine's only. */
static ULONG_PTR g_global_options[6] = {0, 0, 0, 0, 0x100, 0};

static HRESULT STDMETHODCALLTYPE gopt_query(IGlobalOptions *This, REFIID riid, void **object)
{
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &g_iid_global_options)) {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE gopt_add_ref(IGlobalOptions *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE gopt_release(IGlobalOptions *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE gopt_set(IGlobalOptions *This, DWORD property, ULONG_PTR value)
{
    if (property < 1 || property > 5)
        return E_INVALIDARG;
    g_global_options[property] = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gopt_get(IGlobalOptions *This, DWORD property, ULONG_PTR *value)
{
    if (property < 1 || property > 5 || !value)
        return E_INVALIDARG;
    *value = g_global_options[property];
    return S_OK;
}

static const IGlobalOptionsVtbl g_gopt_vtbl = {gopt_query, gopt_add_ref, gopt_release, gopt_set, gopt_get};
static IGlobalOptions g_gopt = {&g_gopt_vtbl};

static HRESULT STDMETHODCALLTYPE gfac_query(IClassFactory *This, REFIID riid, void **object)
{
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IClassFactory)) {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE gfac_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE gfac_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE gfac_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return gopt_query(&g_gopt, riid, object);
}

static HRESULT STDMETHODCALLTYPE gfac_lock(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_gfac_vtbl = {gfac_query, gfac_add_ref, gfac_release, gfac_create, gfac_lock};
static IClassFactory g_gfac = {&g_gfac_vtbl};

/* this file's factory is the class object of the marshaling class. */
BOOL OrpcBuiltinClassObject(REFCLSID clsid, REFIID iid, void **object, HRESULT *hr)
{
    if (IsEqualGUID(clsid, &g_clsid_global_options)) {
        *hr = gfac_query(&g_gfac, iid, object);
        return TRUE;
    }
    if (!IsEqualGUID(clsid, &orpcbuiltin_ProxyClsid))
        return FALSE;
    *hr = orpcbuiltin_DllGetClassObject(clsid, iid, object);
    return TRUE;
}

/* for CoGetPSClsid. */
BOOL OrpcBuiltinClass(REFIID iid, CLSID *clsid)
{
    if (!builtin(iid))
        return FALSE;
    *clsid = orpcbuiltin_ProxyClsid;
    return TRUE;
}
