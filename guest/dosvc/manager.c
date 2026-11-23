#include "dosvc.h"

typedef struct MANAGER {
    IDOManager manager;
    IDeliveryOptimizationMgrInternal internal;
    IDeliveryOptimizationUsage usage;
    IDeliveryOptimizationPolicy policy;
    IUnknown *bits;
    LONG refs;
} MANAGER;

#define FROM(iface, field) CONTAINING_RECORD(iface, MANAGER, field)

/* the usage sinks programs advise, kept for the service; with no peers and no uploads there is no usage to send them. */
static SRWLOCK g_sinks_lock = SRWLOCK_INIT;
static IDeliveryOptimizationUsageCallback *g_sinks[16];

HRESULT BitsManagerCreate(IUnknown *outer, IUnknown **out);

static HRESULT manager_qi(MANAGER *m, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDOManager))
        *out = &m->manager;
    else if (IsEqualGUID(iid, &IID_IDeliveryOptimizationMgrInternal))
        *out = &m->internal;
    else if (IsEqualGUID(iid, &IID_IDeliveryOptimizationUsage))
        *out = &m->usage;
    else if (IsEqualGUID(iid, &IID_IDeliveryOptimizationPolicy))
        *out = &m->policy;
    else if (m->bits)
        return m->bits->lpVtbl->QueryInterface(m->bits, iid, out);
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&m->refs);
    return S_OK;
}

static ULONG manager_addref(MANAGER *m)
{
    return (ULONG)InterlockedIncrement(&m->refs);
}

static ULONG manager_release(MANAGER *m)
{
    LONG refs = InterlockedDecrement(&m->refs);
    if (!refs) {
        if (m->bits)
            m->bits->lpVtbl->Release(m->bits);
        LocalFree(m);
        DoLockService(FALSE);
    }
    return (ULONG)refs;
}

#define IUNKNOWN(prefix, type, field)                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_qi(type *iface, REFIID iid, void **out)                                  \
    {                                                                                                                  \
        return manager_qi(FROM(iface, field), iid, out);                                                               \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_addref(type *iface)                                                        \
    {                                                                                                                  \
        return manager_addref(FROM(iface, field));                                                                     \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_release(type *iface)                                                       \
    {                                                                                                                  \
        return manager_release(FROM(iface, field));                                                                    \
    }

/* ------------------------------------------------------------------ IDOManager */

IUNKNOWN(dom, IDOManager, manager)

static HRESULT STDMETHODCALLTYPE dom_create(IDOManager *iface, IDODownload **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!DoServiceRunning())
        return DO_E_NO_SERVICE;
    DO_JOB *job;
    HRESULT hr = JobCreate(&job);
    if (FAILED(hr))
        return hr;
    hr = ExtFor(job, out);
    if (FAILED(hr))
        JobAbort(job);
    JobRelease(job);
    return hr;
}

/* an enumerator of the download objects the caller may see, taken when it is made. */
typedef struct ENUM {
    IEnumUnknown iface;
    LONG refs;
    ULONG at, count;
    IUnknown *items[];
} ENUM;

static HRESULT STDMETHODCALLTYPE enum_qi(IEnumUnknown *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IEnumUnknown)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enum_addref(IEnumUnknown *iface)
{
    return (ULONG)InterlockedIncrement(&((ENUM *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumUnknown *iface)
{
    ENUM *e = (ENUM *)iface;
    LONG refs = InterlockedDecrement(&e->refs);
    if (!refs) {
        for (ULONG i = 0; i < e->count; i++)
            e->items[i]->lpVtbl->Release(e->items[i]);
        LocalFree(e);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumUnknown *iface, ULONG count, IUnknown **items, ULONG *fetched)
{
    ENUM *e = (ENUM *)iface;
    if (!items || (count > 1 && !fetched))
        return E_POINTER;
    ULONG n = 0;
    while (n < count && e->at < e->count) {
        IUnknown *item = e->items[e->at++];
        item->lpVtbl->AddRef(item);
        items[n++] = item;
    }
    if (fetched)
        *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumUnknown *iface, ULONG count)
{
    ENUM *e = (ENUM *)iface;
    ULONG left = e->count - e->at;
    e->at += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumUnknown *iface)
{
    ((ENUM *)iface)->at = 0;
    return S_OK;
}

static HRESULT make_enum(IUnknown **items, ULONG count, ULONG at, IEnumUnknown **out);

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumUnknown *iface, IEnumUnknown **out)
{
    ENUM *e = (ENUM *)iface;
    if (!out)
        return E_POINTER;
    for (ULONG i = 0; i < e->count; i++)
        e->items[i]->lpVtbl->AddRef(e->items[i]);
    HRESULT hr = make_enum(e->items, e->count, e->at, out);
    if (FAILED(hr))
        for (ULONG i = 0; i < e->count; i++)
            e->items[i]->lpVtbl->Release(e->items[i]);
    return hr;
}

static const IEnumUnknownVtbl g_enum_vtbl = {
    enum_qi, enum_addref, enum_release, enum_next, enum_skip, enum_reset, enum_clone,
};

static HRESULT make_enum(IUnknown **items, ULONG count, ULONG at, IEnumUnknown **out)
{
    ENUM *e = LocalAlloc(LMEM_ZEROINIT, sizeof(*e) + count * sizeof(IUnknown *));
    if (!e)
        return E_OUTOFMEMORY;
    e->iface.lpVtbl = &g_enum_vtbl;
    e->refs = 1;
    e->at = at;
    e->count = count;
    memcpy(e->items, items, count * sizeof(IUnknown *));
    *out = &e->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dom_enum(IDOManager *iface, const DO_DOWNLOAD_ENUM_CATEGORY *category, IEnumUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (category && !category->Value)
        return E_INVALIDARG;
    int id = -1;
    if (category) {
        if ((unsigned)category->Property >= 30)
            return DO_E_UNKNOWN_PROPERTY_ID;
        id = (int)category->Property;
    }
    if (!DoServiceRunning())
        return DO_E_NO_SERVICE;
    DO_JOB *jobs[256];
    ULONG count = 0;
    JobListLock(FALSE);
    for (DO_JOB *job = JobList(); job && count < ARRAYSIZE(jobs); job = job->next) {
        JobAddRef(job);
        jobs[count++] = job;
    }
    JobListUnlock(FALSE);
    IUnknown *items[256];
    ULONG found = 0;
    HRESULT hr = S_OK;
    for (ULONG i = 0; i < count; i++) {
        DO_JOB *job = jobs[i];
        if (SUCCEEDED(hr) && !job->bits && JobVisible(job)) {
            /* only the strings that name a download can choose them, the others are refused once one is seen. */
            if (id > P_LOCAL_PATH)
                hr = E_INVALIDARG;
            else if (id < 0 || JobMatches(job, id, category->Value)) {
                IDODownload *download;
                if (SUCCEEDED(ExtFor(job, &download)))
                    items[found++] = (IUnknown *)download;
            }
        }
        JobRelease(job);
    }
    if (SUCCEEDED(hr) && !found)
        hr = DO_E_NO_DOWNLOADS;
    if (SUCCEEDED(hr))
        hr = make_enum(items, found, 0, out);
    if (FAILED(hr))
        for (ULONG i = 0; i < found; i++)
            items[i]->lpVtbl->Release(items[i]);
    return hr;
}

static const IDOManagerVtbl g_dom_vtbl = {dom_qi, dom_addref, dom_release, dom_create, dom_enum};

/* ------------------------------------------------------------------ IDeliveryOptimizationMgrInternal */

IUNKNOWN(mi, IDeliveryOptimizationMgrInternal, internal)

/* Mime keeps no peer cache: there is nothing to size, pin, expire or delete, and the user token is the caller's. */
static HRESULT STDMETHODCALLTYPE mi_token(IDeliveryOptimizationMgrInternal *iface)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mi_size(IDeliveryOptimizationMgrInternal *iface, UINT64 *size)
{
    if (!size)
        return E_POINTER;
    *size = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mi_delete(IDeliveryOptimizationMgrInternal *iface)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mi_pin(IDeliveryOptimizationMgrInternal *iface, const WCHAR *id, LONG pinned)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mi_expire(IDeliveryOptimizationMgrInternal *iface, UINT64 when, const WCHAR *id)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mi_swarms(IDeliveryOptimizationMgrInternal *iface, const WCHAR *id, LONG all)
{
    return S_OK;
}

static const IDeliveryOptimizationMgrInternalVtbl g_mi_vtbl = {
    mi_qi, mi_addref, mi_release, mi_token, mi_size, mi_delete, mi_pin, mi_expire, mi_swarms,
};

/* ------------------------------------------------------------------ IDeliveryOptimizationUsage */

IUNKNOWN(us, IDeliveryOptimizationUsage, usage)

static HRESULT STDMETHODCALLTYPE us_advise(IDeliveryOptimizationUsage *iface, IDeliveryOptimizationUsageCallback *sink)
{
    if (!sink)
        return E_POINTER;
    AcquireSRWLockExclusive(&g_sinks_lock);
    HRESULT hr = S_OK;
    int free_slot = -1;
    for (int i = 0; i < (int)ARRAYSIZE(g_sinks); i++) {
        if (g_sinks[i] == sink)
            goto done;
        if (!g_sinks[i] && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0)
        hr = E_OUTOFMEMORY;
    else {
        sink->lpVtbl->AddRef(sink);
        g_sinks[free_slot] = sink;
    }
done:
    ReleaseSRWLockExclusive(&g_sinks_lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE us_unadvise(IDeliveryOptimizationUsage *iface, IDeliveryOptimizationUsageCallback *sink)
{
    IDeliveryOptimizationUsageCallback *found = nullptr;
    AcquireSRWLockExclusive(&g_sinks_lock);
    for (int i = 0; i < (int)ARRAYSIZE(g_sinks); i++) {
        if (sink && g_sinks[i] == sink) {
            found = g_sinks[i];
            g_sinks[i] = nullptr;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_sinks_lock);
    if (found)
        found->lpVtbl->Release(found);
    return S_OK;
}

static const IDeliveryOptimizationUsageVtbl g_us_vtbl = {us_qi, us_addref, us_release, us_advise, us_unadvise};

/* ------------------------------------------------------------------ IDeliveryOptimizationPolicy */

IUNKNOWN(po, IDeliveryOptimizationPolicy, policy)

static HRESULT STDMETHODCALLTYPE po_set(IDeliveryOptimizationPolicy *iface, const DOUsagePolicy *policy)
{
    if (!policy)
        return E_POINTER;
    if (!XferCallerIsAdmin())
        return E_ACCESSDENIED;
    HKEY key;
    LSTATUS status = RegCreateKeyExW(StoreRoot(), L"Config", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status)
        return HRESULT_FROM_WIN32(status);
    status = RegSetValueExW(key, L"UsagePolicy", 0, REG_BINARY, (const BYTE *)policy, sizeof(*policy));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(status);
}

static HRESULT STDMETHODCALLTYPE po_get(IDeliveryOptimizationPolicy *iface, DOUsagePolicy *policy)
{
    if (!policy)
        return E_POINTER;
    memset(policy, 0, sizeof(*policy));
    DWORD size = sizeof(*policy), type;
    DOUsagePolicy stored;
    if (!RegGetValueW(StoreRoot(), L"Config", L"UsagePolicy", RRF_RT_REG_BINARY, &type, &stored, &size) &&
        size == sizeof(stored))
        *policy = stored;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE po_stats(IDeliveryOptimizationPolicy *iface, DOUsageStats *stats)
{
    if (!stats)
        return E_POINTER;
    memset(stats, 0, sizeof(*stats));
    return S_OK;
}

static const IDeliveryOptimizationPolicyVtbl g_po_vtbl = {po_qi, po_addref, po_release, po_set, po_get, po_stats};

HRESULT ManagerCreate(REFIID iid, void **out)
{
    MANAGER *m = LocalAlloc(LMEM_ZEROINIT, sizeof(*m));
    if (!m)
        return E_OUTOFMEMORY;
    m->manager.lpVtbl = &g_dom_vtbl;
    m->internal.lpVtbl = &g_mi_vtbl;
    m->usage.lpVtbl = &g_us_vtbl;
    m->policy.lpVtbl = &g_po_vtbl;
    m->refs = 1;
    DoLockService(TRUE);
    BitsManagerCreate((IUnknown *)&m->manager, &m->bits);
    HRESULT hr = manager_qi(m, iid, out);
    manager_release(m);
    return hr;
}
