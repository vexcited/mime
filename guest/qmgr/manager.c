#include "qmgr.h"

/* CJobManagerExternal: the manager, with the peer cache administration that answers as it does with peer caching off. */
typedef struct MANAGER {
    IBackgroundCopyManager iface;
    IBitsPeerCacheAdministration peers;
    LONG refs;
} MANAGER;

#define MX(iface) CONTAINING_RECORD(iface, MANAGER, iface)
#define PX(iface) CONTAINING_RECORD(iface, MANAGER, peers)

static HRESULT manager_qi(MANAGER *m, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IBackgroundCopyManager))
        *out = &m->iface;
    else if (IsEqualGUID(iid, &IID_IBitsPeerCacheAdministration))
        *out = &m->peers;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&m->refs);
    return S_OK;
}

static ULONG manager_release(MANAGER *m)
{
    LONG refs = InterlockedDecrement(&m->refs);
    if (refs)
        return (ULONG)refs;
    LocalFree(m);
    QmObjectRelease();
    return 0;
}

static HRESULT STDMETHODCALLTYPE m_qi(IBackgroundCopyManager *iface, REFIID iid, void **out)
{
    return manager_qi(MX(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE m_addref(IBackgroundCopyManager *iface)
{
    return (ULONG)InterlockedIncrement(&MX(iface)->refs);
}

static ULONG STDMETHODCALLTYPE m_release(IBackgroundCopyManager *iface)
{
    return manager_release(MX(iface));
}

static HRESULT enter(CALLER *c)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    hr = CallerOpen(c);
    if (SUCCEEDED(hr)) {
        hr = CallerCheckManager(c);
        if (FAILED(hr))
            CallerClose(c);
    }
    if (FAILED(hr))
        QmLeave();
    return hr;
}

static HRESULT leave(CALLER *c, HRESULT hr)
{
    CallerClose(c);
    QmLeave();
    return hr;
}

/* a client that calls is logged on, so its jobs that waited offline come online. */
static void caller_online(CALLER *c)
{
    LockShared();
    BOOL waiting = FALSE;
    for (BJOB *job = g_offline.next; job != &g_offline && !waiting; job = job->next)
        waiting = EqualSid(job->owner, c->sid);
    UnlockShared();
    if (waiting)
        JobUserLogon(c->sid);
}

/* CJobManagerExternal::CreateJob and CJobManager::CreateJob. */
static HRESULT STDMETHODCALLTYPE m_create(IBackgroundCopyManager *iface, LPCWSTR name, BG_JOB_TYPE type, GUID *id,
                                          IBackgroundCopyJob **out)
{
    CALLER c;
    if (out)
        *out = nullptr;
    HRESULT hr = enter(&c);
    if (FAILED(hr))
        return hr;
    if (!name || !id || !out)
        return leave(&c, E_INVALIDARG);
    if ((DWORD)type > BG_JOB_TYPE_UPLOAD_REPLY)
        return leave(&c, E_NOTIMPL);
    caller_online(&c);
    Lock();
    BJOB *job;
    hr = JobCreate(&c, name, type, nullptr, &job);
    if (SUCCEEDED(hr)) {
        hr = JobExtFor(job, out);
        if (SUCCEEDED(hr))
            *id = job->id;
        else
            JobRemove(job);
        JobRelease(job);
    }
    Unlock();
    return leave(&c, hr);
}

/* CJobManager::GetJob: a job that exists and is not the caller's to open is E_ACCESSDENIED. */
static HRESULT STDMETHODCALLTYPE m_get(IBackgroundCopyManager *iface, REFGUID id, IBackgroundCopyJob **out)
{
    CALLER c;
    if (out)
        *out = nullptr;
    HRESULT hr = enter(&c);
    if (FAILED(hr))
        return hr;
    if (!id || !out)
        return leave(&c, E_INVALIDARG);
    caller_online(&c);
    LockShared();
    BJOB *job = JobFind(id);
    if (!job)
        hr = BG_E_NOT_FOUND;
    else if (FAILED(JobCheckAccess(job, &c, BITS_READ, nullptr)))
        hr = E_ACCESSDENIED;
    else
        hr = JobExtFor(job, out);
    UnlockShared();
    return leave(&c, hr);
}

/* CJobManager::EnumJobs: the caller's own jobs, or every job for an administrator who asks for them all. */
static HRESULT STDMETHODCALLTYPE m_enum(IBackgroundCopyManager *iface, DWORD flags, IEnumBackgroundCopyJobs **out)
{
    CALLER c;
    if (out)
        *out = nullptr;
    HRESULT hr = enter(&c);
    if (FAILED(hr))
        return hr;
    if (!out)
        return leave(&c, E_INVALIDARG);
    if (flags & ~(DWORD)BG_JOB_ENUM_ALL_USERS)
        return leave(&c, E_NOTIMPL);
    if ((flags & BG_JOB_ENUM_ALL_USERS) && !c.admin)
        return leave(&c, E_ACCESSDENIED);
    caller_online(&c);
    LockShared();
    DWORD count = JobCountAll(), n = 0;
    IUnknown **items = LocalAlloc(LMEM_FIXED, (count ? count : 1) * sizeof(*items));
    if (!items)
        hr = E_OUTOFMEMORY;
    for (int pass = 0; pass < 2 && SUCCEEDED(hr); pass++) {
        BJOB *head = pass ? &g_offline : &g_online;
        for (BJOB *job = head->next; job != head && SUCCEEDED(hr); job = job->next) {
            if (!(flags & BG_JOB_ENUM_ALL_USERS) &&
                (!JobIsOwner(job, &c) || FAILED(JobCheckAccess(job, &c, BITS_READ, nullptr))))
                continue;
            hr = JobExtFor(job, (IBackgroundCopyJob **)&items[n]);
            if (SUCCEEDED(hr))
                n++;
        }
    }
    if (SUCCEEDED(hr))
        hr = EnumCreate(&IID_IEnumBackgroundCopyJobs, items, n, (void **)out);
    UnlockShared();
    for (DWORD i = 0; i < n; i++)
        QM_RELEASE(items[i]);
    LocalFree(items);
    return leave(&c, hr);
}

static HRESULT STDMETHODCALLTYPE m_description(IBackgroundCopyManager *iface, HRESULT code, DWORD language,
                                               LPWSTR *text)
{
    CALLER c;
    if (text)
        *text = nullptr;
    HRESULT hr = enter(&c);
    if (FAILED(hr))
        return hr;
    return leave(&c, text ? QmDescribe(code, language, text) : E_INVALIDARG);
}

static const IBackgroundCopyManagerVtbl g_manager_vtbl = {m_qi,  m_addref, m_release,    m_create,
                                                          m_get, m_enum,   m_description};

/* ------------------------------------------------------------------ IBitsPeerCacheAdministration */

static HRESULT STDMETHODCALLTYPE p_qi(IBitsPeerCacheAdministration *iface, REFIID iid, void **out)
{
    return manager_qi(PX(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE p_addref(IBitsPeerCacheAdministration *iface)
{
    return (ULONG)InterlockedIncrement(&PX(iface)->refs);
}

static ULONG STDMETHODCALLTYPE p_release(IBitsPeerCacheAdministration *iface)
{
    return manager_release(PX(iface));
}

static HRESULT peer_answer(void)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    QmLeave();
    return S_FALSE;
}

static HRESULT peer_zero(DWORD *value)
{
    if (value)
        *value = 0;
    return peer_answer();
}

static HRESULT STDMETHODCALLTYPE p_get_size(IBitsPeerCacheAdministration *iface, DWORD *bytes)
{
    return peer_zero(bytes);
}

static HRESULT STDMETHODCALLTYPE p_set_dword(IBitsPeerCacheAdministration *iface, DWORD value)
{
    return peer_answer();
}

static HRESULT STDMETHODCALLTYPE p_get_age(IBitsPeerCacheAdministration *iface, ULONG *seconds)
{
    return peer_zero(seconds);
}

static HRESULT STDMETHODCALLTYPE p_set_age(IBitsPeerCacheAdministration *iface, ULONG seconds)
{
    return peer_answer();
}

static HRESULT STDMETHODCALLTYPE p_get_flags(IBitsPeerCacheAdministration *iface, DWORD *flags)
{
    return peer_zero(flags);
}

static HRESULT STDMETHODCALLTYPE p_enum_records(IBitsPeerCacheAdministration *iface, IEnumBitsPeerCacheRecords **out)
{
    if (out)
        *out = nullptr;
    return peer_answer();
}

/* CJobManagerExternal::GetRecordInternal: no record exists, a null out is the only error. */
static HRESULT STDMETHODCALLTYPE p_get_record(IBitsPeerCacheAdministration *iface, REFGUID id,
                                              IBitsPeerCacheRecord **record)
{
    if (!record)
        return E_INVALIDARG;
    *record = nullptr;
    return peer_answer();
}

static HRESULT STDMETHODCALLTYPE p_none(IBitsPeerCacheAdministration *iface)
{
    return peer_answer();
}

static HRESULT STDMETHODCALLTYPE p_delete_record(IBitsPeerCacheAdministration *iface, REFGUID id)
{
    return peer_answer();
}

static HRESULT STDMETHODCALLTYPE p_delete_url(IBitsPeerCacheAdministration *iface, LPCWSTR url)
{
    return peer_answer();
}

static HRESULT STDMETHODCALLTYPE p_enum_peers(IBitsPeerCacheAdministration *iface, IEnumBitsPeers **out)
{
    if (out)
        *out = nullptr;
    return peer_answer();
}

static const IBitsPeerCacheAdministrationVtbl g_peers_vtbl = {
    p_qi,          p_addref,       p_release,     p_get_size,      p_set_dword,  p_get_age,    p_set_age, p_get_flags,
    p_set_dword,   p_enum_records, p_get_record,  p_none,          p_delete_record, p_delete_url, p_enum_peers,
    p_none,        p_none,
};

HRESULT ManagerCreate(REFIID iid, void **out)
{
    MANAGER *m = LocalAlloc(LMEM_ZEROINIT, sizeof(*m));
    if (!m)
        return E_OUTOFMEMORY;
    m->iface.lpVtbl = &g_manager_vtbl;
    m->peers.lpVtbl = &g_peers_vtbl;
    m->refs = 1;
    QmObjectAdd();
    HRESULT hr = manager_qi(m, iid, out);
    manager_release(m);
    return hr;
}
