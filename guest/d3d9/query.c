/*
 * IDirect3DQuery9 over d3d11's queries: events, occlusion counts,
 * timestamps and their frequency and disjointness. the vertex cache's
 * description is a constant; the other kinds of windows' drivers report
 * no more than this one does, D3DERR_NOTAVAILABLE.
 */
#include "d3d9_private.h"

typedef struct D9Query {
    IDirect3DQuery9 iface;
    D9Resource base;
    D3DQUERYTYPE type;
    ID3D11Query *query;
    BOOL issued, began;
} D9Query;

static D9Query *impl(IDirect3DQuery9 *iface) { return CONTAINING_RECORD(iface, D9Query, iface); }

static void destroy(D9Query *q)
{
    if (q->query)
        q->query->lpVtbl->Release(q->query);
    D9ResourceClear(&q->base);
    HeapFree(GetProcessHeap(), 0, q);
}

static void destroy_resource(D9Resource *r) { destroy(CONTAINING_RECORD(r, D9Query, base)); }

static DWORD data_size(D3DQUERYTYPE type)
{
    switch (type) {
    case D3DQUERYTYPE_EVENT:
    case D3DQUERYTYPE_OCCLUSION:
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
        return 4;
    case D3DQUERYTYPE_TIMESTAMP:
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        return 8;
    case D3DQUERYTYPE_VCACHE:
        return sizeof(D3DDEVINFO_VCACHE);
    default:
        return 0;
    }
}

static D3D11_QUERY d3d11_type(D3DQUERYTYPE type)
{
    switch (type) {
    case D3DQUERYTYPE_EVENT:
        return D3D11_QUERY_EVENT;
    case D3DQUERYTYPE_OCCLUSION:
        return D3D11_QUERY_OCCLUSION;
    case D3DQUERYTYPE_TIMESTAMP:
        return D3D11_QUERY_TIMESTAMP;
    default:
        return D3D11_QUERY_TIMESTAMP_DISJOINT;
    }
}

static HRESULT STDMETHODCALLTYPE query_query(IDirect3DQuery9 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DQuery9)) {
        *out = iface;
        D9ResourceAddRef(&impl(iface)->base);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE query_add_ref(IDirect3DQuery9 *iface) { return D9ResourceAddRef(&impl(iface)->base); }

static ULONG STDMETHODCALLTYPE query_release(IDirect3DQuery9 *iface) { return D9ResourceRelease(&impl(iface)->base); }

static HRESULT STDMETHODCALLTYPE query_get_device(IDirect3DQuery9 *iface, IDirect3DDevice9 **device)
{
    return D9ResourceGetDevice(&impl(iface)->base, device);
}

static D3DQUERYTYPE STDMETHODCALLTYPE query_get_type(IDirect3DQuery9 *iface) { return impl(iface)->type; }

static DWORD STDMETHODCALLTYPE query_get_data_size(IDirect3DQuery9 *iface) { return data_size(impl(iface)->type); }

/* occlusion and disjointness bracket work with a begin; the others are points in it, ended alone. */
static BOOL brackets(D3DQUERYTYPE type)
{
    return type == D3DQUERYTYPE_OCCLUSION || type == D3DQUERYTYPE_TIMESTAMPDISJOINT;
}

static HRESULT STDMETHODCALLTYPE query_issue(IDirect3DQuery9 *iface, DWORD flags)
{
    D9Query *q = impl(iface);
    D9Device *d = q->base.device;
    if ((flags & ~(D3DISSUE_BEGIN | D3DISSUE_END)) || (flags & D3DISSUE_BEGIN && !brackets(q->type)))
        return D3DERR_INVALIDCALL;
    if (!q->query)
        return D3D_OK;
    D9Lock(d);
    ID3D11DeviceContext1 *ctx = d->context;
    if (flags & D3DISSUE_BEGIN) {
        if (q->began)
            ctx->lpVtbl->End(ctx, (ID3D11Asynchronous *)q->query);
        ctx->lpVtbl->Begin(ctx, (ID3D11Asynchronous *)q->query);
        q->began = TRUE;
        q->issued = FALSE;
    }
    if (flags & D3DISSUE_END) {
        /* an end without a begin brackets nothing, as on windows, which reports zero samples. */
        if (!q->began && brackets(q->type))
            ctx->lpVtbl->Begin(ctx, (ID3D11Asynchronous *)q->query);
        if (q->type == D3DQUERYTYPE_TIMESTAMPFREQ)
            ctx->lpVtbl->Begin(ctx, (ID3D11Asynchronous *)q->query);
        ctx->lpVtbl->End(ctx, (ID3D11Asynchronous *)q->query);
        q->began = FALSE;
        q->issued = TRUE;
    }
    D9Unlock(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE query_get_data(IDirect3DQuery9 *iface, void *data, DWORD size, DWORD flags)
{
    D9Query *q = impl(iface);
    D9Device *d = q->base.device;
    DWORD needed = data_size(q->type);
    if (data && size < needed && size)
        return D3DERR_INVALIDCALL;
    if (q->type == D3DQUERYTYPE_VCACHE) {
        D3DDEVINFO_VCACHE cache = {MAKEFOURCC('C', 'A', 'C', 'H'), 1, 24, 7};
        if (data && size)
            memcpy(data, &cache, sizeof(cache));
        return S_OK;
    }
    /* a query never issued, or still begun, has nothing to tell. */
    if (!q->issued) {
        if (q->began)
            return S_FALSE;
        if (data && size)
            memset(data, 0, size);
        return S_OK;
    }
    D9Lock(d);
    ID3D11DeviceContext1 *ctx = d->context;
    union {
        BOOL done;
        UINT64 count;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
    } result;
    UINT result_size = q->type == D3DQUERYTYPE_EVENT                                                      ? sizeof(BOOL)
                       : q->type == D3DQUERYTYPE_TIMESTAMPDISJOINT || q->type == D3DQUERYTYPE_TIMESTAMPFREQ ? sizeof(result.disjoint)
                                                                                                          : sizeof(UINT64);
    HRESULT hr = ctx->lpVtbl->GetData(ctx, (ID3D11Asynchronous *)q->query, &result, result_size,
                                      flags & D3DGETDATA_FLUSH ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
    D9Unlock(d);
    if (hr != S_OK)
        return hr == S_FALSE ? S_FALSE : D3DERR_DEVICELOST;
    if (!data || !size)
        return S_OK;
    switch (q->type) {
    case D3DQUERYTYPE_EVENT:
        *(BOOL *)data = TRUE;
        break;
    case D3DQUERYTYPE_OCCLUSION:
        *(DWORD *)data = (DWORD)D9_MIN(result.count, 0xFFFFFFFFull);
        break;
    case D3DQUERYTYPE_TIMESTAMP:
        *(UINT64 *)data = result.count;
        break;
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
        *(BOOL *)data = result.disjoint.Disjoint;
        break;
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        *(UINT64 *)data = result.disjoint.Frequency;
        break;
    default:
        break;
    }
    return S_OK;
}

static const IDirect3DQuery9Vtbl query_vtbl = {
    query_query,    query_add_ref,       query_release, query_get_device,
    query_get_type, query_get_data_size, query_issue,   query_get_data,
};

HRESULT D9CreateQuery(D9Device *d, D3DQUERYTYPE type, IDirect3DQuery9 **out)
{
    if (!data_size(type))
        return D3DERR_NOTAVAILABLE;
    /* without an out pointer, whether the kind is supported. */
    if (!out)
        return D3D_OK;
    *out = nullptr;
    D9Query *q = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*q));
    if (!q)
        return E_OUTOFMEMORY;
    q->iface.lpVtbl = &query_vtbl;
    q->type = type;
    if (type != D3DQUERYTYPE_VCACHE) {
        D3D11_QUERY_DESC desc = {d3d11_type(type)};
        HRESULT hr = d->d3d11->lpVtbl->CreateQuery(d->d3d11, &desc, &q->query);
        if (FAILED(hr)) {
            HeapFree(GetProcessHeap(), 0, q);
            return D3DERR_NOTAVAILABLE;
        }
    }
    D9ResourceInit(&q->base, d, 0, D3DPOOL_DEFAULT, 0, D3DFMT_UNKNOWN, destroy_resource);
    *out = &q->iface;
    return D3D_OK;
}
