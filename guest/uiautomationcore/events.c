/*
 * events between processes. a client says what it listens to in a table every process can read, with the object that
 * hears it marshaled beside the entry. a provider that raises an event finds the entries it matches, and a thread of its
 * own tells each listener, so raising never waits for a client. the table also answers UiaClientsAreListening.
 */
#include "uia.h"

extern HRESULT UiaExportValue(VARIANT *value);

#define REG_MAX 64
#define BLOB_MAX 512
#define WINDOW_MAX 4
#define RID_MAX 16
#define PROP_MAX 16
#define REG_MAGIC 0x41495545

typedef struct REG_ENTRY {
    LONG used;
    DWORD pid;
    DWORD cookie;
    int event_id;
    int scope;
    int nprops;
    int rid_len;
    DWORD window;
    DWORD windows[WINDOW_MAX];
    DWORD blob_size;
    int props[PROP_MAX];
    int rid[RID_MAX];
    BYTE blob[BLOB_MAX];
} REG_ENTRY;

typedef struct REG {
    DWORD magic;
    LONG sequence;
    DWORD next_cookie;
    REG_ENTRY entries[REG_MAX];
    DWORD providers[256];
} REG;

static HANDLE g_mutex;
static REG *g_reg;
static CRITICAL_SECTION g_state_lock;
static INIT_ONCE g_state_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK state_init(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    InitializeCriticalSection(&g_state_lock);
    return TRUE;
}

static void state_lock(void)
{
    InitOnceExecuteOnce(&g_state_once, state_init, nullptr, nullptr);
    EnterCriticalSection(&g_state_lock);
}

static BOOL reg_open(void)
{
    state_lock();
    if (!g_reg) {
        HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(REG),
                                            L"Local\\MimeUiaRegistry");
        HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\MimeUiaRegistryLock");
        REG *reg = mapping ? MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(REG)) : nullptr;
        if (reg && mutex) {
            g_mutex = mutex;
            g_reg = reg;
            if (!reg->magic) {
                WaitForSingleObject(mutex, INFINITE);
                if (!reg->magic) {
                    reg->next_cookie = 1;
                    reg->magic = REG_MAGIC;
                }
                ReleaseMutex(mutex);
            }
        }
    }
    BOOL ok = g_reg != nullptr;
    LeaveCriticalSection(&g_state_lock);
    return ok;
}

static void reg_lock(void)
{
    WaitForSingleObject(g_mutex, INFINITE);
}

static void reg_unlock(void)
{
    ReleaseMutex(g_mutex);
}

static BOOL process_alive(DWORD pid)
{
    if (pid == GetCurrentProcessId())
        return TRUE;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process)
        return FALSE;
    BOOL alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

/* entries of processes that are gone are dropped, with the marshaled objects they left. */
static void reg_sweep_locked(void)
{
    for (int i = 0; i < REG_MAX; i++) {
        REG_ENTRY *e = &g_reg->entries[i];
        if (e->used && !process_alive(e->pid)) {
            e->used = 0;
            InterlockedIncrement(&g_reg->sequence);
        }
    }
}

HRESULT UiaRegistryAdd(DWORD kind, int event_id, int scope, HWND window, const int *props, int nprops, const int *rid,
                       int rid_len, IUnknown *sink, DWORD *cookie)
{
    UiaEnsureCom();
    if (!reg_open())
        return E_FAIL;
    if (nprops > PROP_MAX || rid_len > RID_MAX)
        return E_INVALIDARG;
    IStream *stream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr))
        return hr;
    hr = CoMarshalInterface(stream, &IID_IDispatch, sink, MSHCTX_LOCAL, nullptr, MSHLFLAGS_TABLESTRONG);
    if (FAILED(hr)) {
        stream->lpVtbl->Release(stream);
        return hr;
    }
    HGLOBAL global = nullptr;
    LARGE_INTEGER zero = {};
    ULARGE_INTEGER end = {};
    GetHGlobalFromStream(stream, &global);
    stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_END, &end);
    stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
    void *data = global ? GlobalLock(global) : nullptr;
    if (!data || end.QuadPart > BLOB_MAX) {
        if (data)
            GlobalUnlock(global);
        stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
        CoReleaseMarshalData(stream);
        stream->lpVtbl->Release(stream);
        return E_OUTOFMEMORY;
    }
    reg_lock();
    reg_sweep_locked();
    REG_ENTRY *slot = nullptr;
    for (int i = 0; i < REG_MAX; i++)
        if (!g_reg->entries[i].used) {
            slot = &g_reg->entries[i];
            break;
        }
    if (slot) {
        memset(slot, 0, sizeof(*slot));
        slot->pid = GetCurrentProcessId();
        slot->cookie = g_reg->next_cookie++;
        slot->event_id = event_id;
        slot->scope = scope;
        slot->window = (DWORD)(ULONG_PTR)window;
        slot->nprops = nprops;
        memcpy(slot->props, props, (SIZE_T)nprops * sizeof(int));
        slot->rid_len = rid_len;
        memcpy(slot->rid, rid, (SIZE_T)rid_len * sizeof(int));
        slot->blob_size = (DWORD)end.QuadPart;
        memcpy(slot->blob, data, slot->blob_size);
        *cookie = slot->cookie;
        InterlockedExchange(&slot->used, 1);
        InterlockedIncrement(&g_reg->sequence);
    }
    reg_unlock();
    GlobalUnlock(global);
    if (!slot) {
        stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
        CoReleaseMarshalData(stream);
        hr = HRESULT_FROM_WIN32(ERROR_TOO_MANY_POSTS);
    }
    stream->lpVtbl->Release(stream);
    return hr;
}

void UiaRegistryRemove(DWORD cookie)
{
    if (!reg_open())
        return;
    BYTE blob[BLOB_MAX];
    DWORD size = 0;
    reg_lock();
    for (int i = 0; i < REG_MAX; i++) {
        REG_ENTRY *e = &g_reg->entries[i];
        if (e->used && e->cookie == cookie && e->pid == GetCurrentProcessId()) {
            size = e->blob_size;
            memcpy(blob, e->blob, size);
            e->used = 0;
            InterlockedIncrement(&g_reg->sequence);
        }
    }
    reg_unlock();
    if (size) {
        /* the marshaled reference of the entry goes back. */
        HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, size);
        void *copy = global ? GlobalLock(global) : nullptr;
        IStream *stream = nullptr;
        if (copy) {
            memcpy(copy, blob, size);
            GlobalUnlock(global);
            if (SUCCEEDED(CreateStreamOnHGlobal(global, TRUE, &stream))) {
                CoReleaseMarshalData(stream);
                stream->lpVtbl->Release(stream);
                global = nullptr;
            }
        }
        if (global)
            GlobalFree(global);
    }
}

void UiaRegistryAddWindow(DWORD cookie, HWND hwnd)
{
    if (!reg_open())
        return;
    reg_lock();
    for (int i = 0; i < REG_MAX; i++) {
        REG_ENTRY *e = &g_reg->entries[i];
        if (e->used && e->cookie == cookie)
            for (int w = 0; w < WINDOW_MAX; w++)
                if (!e->windows[w] || e->windows[w] == (DWORD)(ULONG_PTR)hwnd) {
                    e->windows[w] = (DWORD)(ULONG_PTR)hwnd;
                    break;
                }
    }
    reg_unlock();
}

void UiaRegistryRemoveWindow(DWORD cookie, HWND hwnd)
{
    if (!reg_open())
        return;
    reg_lock();
    for (int i = 0; i < REG_MAX; i++) {
        REG_ENTRY *e = &g_reg->entries[i];
        if (e->used && e->cookie == cookie)
            for (int w = 0; w < WINDOW_MAX; w++)
                if (e->windows[w] == (DWORD)(ULONG_PTR)hwnd)
                    e->windows[w] = 0;
    }
    reg_unlock();
}

/* ------------------------------------------------------------------ who listens */

/* the windows events that automation events are told as, which a hook of a client of active accessibility may name. */
static BOOL hooks_installed(void)
{
    static const DWORD events[] = {2,      4,      5,      6,      7,      18,     0x10A,  0x10C,  0x11B,  0x121,
                                   0x8005, 0x8006, 0x8007, 0x8008, 0x8009, 0x800A, 0x800B, 0x800C, 0x800D, 0x800E,
                                   0x8010, 0x8014, 0x8015, 0x8019, 0x8020, 0x8021, 0x8022, 0x8023, 0x8024, 0x8025,
                                   0x8026, 0x8030};
    for (int i = 0; i < (int)RTL_NUMBER_OF(events); i++)
        if (IsWinEventHookInstalled(events[i]))
            return TRUE;
    /* events that programs define for themselves lie in two ranges. */
    for (DWORD id = 0x4E00; id < 0x4E40; id++)
        if (IsWinEventHookInstalled(id))
            return TRUE;
    for (DWORD id = 0x7500; id < 0x75B0; id++)
        if (IsWinEventHookInstalled(id))
            return TRUE;
    return FALSE;
}

BOOL UiaAnyClientListening(void)
{
    if (hooks_installed())
        return TRUE;
    if (!reg_open())
        return FALSE;
    BOOL found = FALSE;
    reg_lock();
    reg_sweep_locked();
    for (int i = 0; i < REG_MAX && !found; i++)
        found = g_reg->entries[i].used != 0;
    reg_unlock();
    return found;
}

BOOL WINAPI UiaClientsAreListening(void)
{
    return UiaAnyClientListening();
}

/* ------------------------------------------------------------------ delivering */

typedef struct JOB {
    struct JOB *next;
    DWORD cookie;
    IDispatch *source;
    int type;
    int event_id;
    VARIANT v[4];
} JOB;

typedef struct SINK_CACHE {
    DWORD cookie;
    IDispatch *sink;
} SINK_CACHE;

static JOB *g_jobs_head, *g_jobs_tail;
static HANDLE g_job_event;
static HANDLE g_worker;
static SINK_CACHE g_sinks[REG_MAX];

static IDispatch *sink_for(DWORD cookie, const REG_ENTRY *entry)
{
    for (int i = 0; i < REG_MAX; i++)
        if (g_sinks[i].cookie == cookie)
            return g_sinks[i].sink;
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, entry->blob_size);
    void *copy = global ? GlobalLock(global) : nullptr;
    if (!copy) {
        if (global)
            GlobalFree(global);
        return nullptr;
    }
    memcpy(copy, entry->blob, entry->blob_size);
    GlobalUnlock(global);
    IStream *stream = nullptr;
    IDispatch *sink = nullptr;
    if (SUCCEEDED(CreateStreamOnHGlobal(global, TRUE, &stream))) {
        CoUnmarshalInterface(stream, &IID_IDispatch, (void **)&sink);
        stream->lpVtbl->Release(stream);
    } else {
        GlobalFree(global);
    }
    if (sink)
        for (int i = 0; i < REG_MAX; i++)
            if (!g_sinks[i].cookie) {
                g_sinks[i].cookie = cookie;
                g_sinks[i].sink = sink;
                break;
            }
    return sink;
}

static void sinks_prune(const REG_ENTRY *live, int count)
{
    for (int i = 0; i < REG_MAX; i++) {
        if (!g_sinks[i].cookie)
            continue;
        BOOL found = FALSE;
        for (int j = 0; j < count; j++)
            found |= live[j].cookie == g_sinks[i].cookie;
        if (!found) {
            g_sinks[i].sink->lpVtbl->Release(g_sinks[i].sink);
            g_sinks[i].cookie = 0;
            g_sinks[i].sink = nullptr;
        }
    }
}

static void job_free(JOB *job)
{
    if (job->source)
        job->source->lpVtbl->Release(job->source);
    for (int i = 0; i < 4; i++)
        VariantClear(&job->v[i]);
    UiaFree(job);
}

static void job_deliver(JOB *job)
{
    REG_ENTRY entry;
    BOOL have = FALSE;
    if (reg_open()) {
        reg_lock();
        for (int i = 0; i < REG_MAX; i++)
            if (g_reg->entries[i].used && g_reg->entries[i].cookie == job->cookie) {
                entry = g_reg->entries[i];
                have = TRUE;
            }
        reg_unlock();
    }
    if (!have)
        return;
    IDispatch *sink = sink_for(job->cookie, &entry);
    if (!sink)
        return;
    /* arguments are in reverse order, the first is last. */
    VARIANT ordered[7];
    ordered[6].vt = VT_I4;
    ordered[6].lVal = job->type;
    ordered[5].vt = VT_DISPATCH;
    ordered[5].pdispVal = job->source;
    ordered[4].vt = VT_I4;
    ordered[4].lVal = job->event_id;
    for (int i = 0; i < 4; i++)
        ordered[3 - i] = job->v[i];
    DISPPARAMS params = {ordered, nullptr, 7, 0};
    VARIANT result;
    VariantInit(&result);
    sink->lpVtbl->Invoke(sink, SINK_DISPID_EVENT, &IID_NULL, 0, DISPATCH_METHOD, &params, &result, nullptr, nullptr);
    VariantClear(&result);
}

static DWORD WINAPI worker_main(void *unused)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        JOB *job = nullptr;
        state_lock();
        if (g_jobs_head) {
            job = g_jobs_head;
            g_jobs_head = job->next;
            if (!g_jobs_head)
                g_jobs_tail = nullptr;
        }
        LeaveCriticalSection(&g_state_lock);
        if (!job) {
            WaitForSingleObject(g_job_event, 1000);
            continue;
        }
        job_deliver(job);
        job_free(job);
    }
    return 0;
}

static void enqueue(JOB *job)
{
    state_lock();
    if (!g_worker) {
        g_job_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_worker = CreateThread(nullptr, 0, worker_main, nullptr, 0, nullptr);
    }
    job->next = nullptr;
    if (g_jobs_tail)
        g_jobs_tail->next = job;
    else
        g_jobs_head = job;
    g_jobs_tail = job;
    LeaveCriticalSection(&g_state_lock);
    SetEvent(g_job_event);
}

/* the number of steps from the node up to the root of an entry, or -1 when it is not under it. */
static int distance_to(NODE *source, const REG_ENTRY *e)
{
    HWND desktop = GetDesktopWindow();
    if (e->window == (DWORD)(ULONG_PTR)desktop && !e->rid_len) {
        HWND owner = NodeOwnerWindow(source);
        if (!owner)
            return 2;
        HWND parent = GetAncestor(owner, GA_PARENT);
        return parent == desktop || !parent ? 1 : 2;
    }
    HWND roots[1 + WINDOW_MAX];
    int nroots = 0;
    if (e->window)
        roots[nroots++] = (HWND)(ULONG_PTR)e->window;
    for (int w = 0; w < WINDOW_MAX; w++)
        if (e->windows[w])
            roots[nroots++] = (HWND)(ULONG_PTR)e->windows[w];
    HWND owner = NodeOwnerWindow(source);
    if (owner && nroots && !e->rid_len) {
        for (int i = 0; i < nroots; i++) {
            if (owner == roots[i])
                return 0;
            if (IsChild(roots[i], owner))
                return 2;
        }
        return -1;
    }
    if (!e->rid_len)
        return nroots ? -1 : 2;
    NODE *current = source;
    NodeAddRef(current);
    int depth = 0;
    while (current && depth < 128) {
        int *ids = nullptr, n = 0;
        BOOL same = FALSE;
        if (SUCCEEDED(NodeRuntimeId(current, &ids, &n)))
            same = n && !UiaCompareRuntimeIds(ids, n, e->rid, e->rid_len);
        UiaFree(ids);
        if (same) {
            NodeRelease(current);
            return depth;
        }
        NODE *parent = nullptr;
        NodeNavigate(current, NavigateDirection_Parent, &parent);
        NodeRelease(current);
        current = parent;
        depth++;
    }
    NodeRelease(current);
    return -1;
}

static BOOL scope_matches(int scope, int distance)
{
    if (distance == 0)
        return (scope & TreeScope_Element) != 0;
    if (distance == 1)
        return (scope & (TreeScope_Children | TreeScope_Descendants)) != 0;
    return (scope & TreeScope_Descendants) != 0;
}

static HRESULT pack_args(NODE *source, const EVENT_ARGS *a, JOB *job)
{
    job->event_id = a->event_id;
    job->type = a->type;
    VARIANT *v = job->v;
    switch (a->type) {
    case EventArgsType_PropertyChanged:
        v[0].vt = VT_I4;
        v[0].lVal = a->property;
        VariantCopy(&v[1], (VARIANT *)&a->old_value);
        VariantCopy(&v[2], (VARIANT *)&a->new_value);
        UiaExportValue(&v[1]);
        UiaExportValue(&v[2]);
        break;
    case EventArgsType_StructureChanged: {
        v[0].vt = VT_I4;
        v[0].lVal = a->structure;
        int *own = nullptr, n = 0;
        const int *ids = a->runtime_id;
        n = a->runtime_len;
        if (!ids || !n)
            NodeRuntimeId(source, &own, &n), ids = own;
        else if (ids[0] == UiaAppendRuntimeId) {
            int *full = nullptr, fn = 0;
            if (SUCCEEDED(NodeRuntimeId(source, &full, &fn)) && fn) {
                /* an id to append continues the id of the element that raised the event. */
                int *joined = UiaAlloc(((SIZE_T)fn + n) * sizeof(int));
                if (joined) {
                    memcpy(joined, full, (SIZE_T)fn * sizeof(int));
                    memcpy(joined + fn, ids + 1, (SIZE_T)(n - 1) * sizeof(int));
                    UiaFree(own);
                    own = joined;
                    ids = joined;
                    n = fn + n - 1;
                }
            }
            UiaFree(full);
        }
        if (ids && n && SUCCEEDED(UiaIntArrayToSafeArray(ids, n, &v[1].parray)))
            v[1].vt = VT_ARRAY | VT_I4;
        UiaFree(own);
        break;
    }
    case EventArgsType_AsyncContentLoaded:
        v[0].vt = VT_I4;
        v[0].lVal = a->loaded_state;
        v[1].vt = VT_R8;
        v[1].dblVal = a->loaded_percent;
        break;
    case EventArgsType_TextEditTextChanged:
        v[0].vt = VT_I4;
        v[0].lVal = a->edit_type;
        if (a->edit_data) {
            SAFEARRAY *copy = nullptr;
            if (SUCCEEDED(SafeArrayCopy(a->edit_data, &copy))) {
                VARTYPE vt = 0;
                SafeArrayGetVartype(copy, &vt);
                v[1].vt = VT_ARRAY | vt;
                v[1].parray = copy;
            }
        }
        break;
    case EventArgsType_Notification:
        v[0].vt = VT_I4;
        v[0].lVal = a->notification_kind;
        v[1].vt = VT_I4;
        v[1].lVal = a->notification_processing;
        v[2].vt = VT_BSTR;
        v[2].bstrVal = a->display ? SysAllocString(a->display) : nullptr;
        v[3].vt = VT_BSTR;
        v[3].bstrVal = a->activity ? SysAllocString(a->activity) : nullptr;
        break;
    case EventArgsType_Changes: {
        SAFEARRAY *array = SafeArrayCreateVector(VT_VARIANT, 0, (ULONG)a->change_count * 3);
        VARIANT *items;
        if (array && SUCCEEDED(SafeArrayAccessData(array, (void **)&items))) {
            for (int i = 0; i < a->change_count; i++) {
                items[i * 3].vt = VT_I4;
                items[i * 3].lVal = a->changes[i].uiaId;
                VariantCopy(&items[i * 3 + 1], &a->changes[i].payload);
                VariantCopy(&items[i * 3 + 2], &a->changes[i].extraInfo);
            }
            SafeArrayUnaccessData(array);
            v[0].vt = VT_ARRAY | VT_VARIANT;
            v[0].parray = array;
        }
        break;
    }
    default:
        break;
    }
    return S_OK;
}

HRESULT UiaDispatchEvent(NODE *source, const EVENT_ARGS *args)
{
    if (!reg_open())
        return S_OK;
    REG_ENTRY matches[REG_MAX];
    int count = 0;
    reg_lock();
    for (int i = 0; i < REG_MAX; i++) {
        REG_ENTRY *e = &g_reg->entries[i];
        if (!e->used)
            continue;
        BOOL wanted = e->event_id == args->event_id;
        if (wanted && args->event_id == UIA_AutomationPropertyChangedEventId && e->nprops) {
            wanted = FALSE;
            for (int p = 0; p < e->nprops; p++)
                wanted |= e->props[p] == args->property;
        }
        if (wanted)
            matches[count++] = *e;
    }
    reg_unlock();
    state_lock();
    sinks_prune(matches, count);
    LeaveCriticalSection(&g_state_lock);
    for (int i = 0; i < count; i++) {
        int distance = distance_to(source, &matches[i]);
        if (distance < 0 || !scope_matches(matches[i].scope, distance))
            continue;
        JOB *job = UiaAlloc(sizeof(*job));
        if (!job)
            return E_OUTOFMEMORY;
        job->cookie = matches[i].cookie;
        if (FAILED(ServerWrapNode(source, &job->source))) {
            UiaFree(job);
            continue;
        }
        pack_args(source, args, job);
        enqueue(job);
    }
    return S_OK;
}

/* ------------------------------------------------------------------ telling providers what is wanted */

#define ROOT_MAX 16

typedef struct WATCHED {
    NODE *node;
    DWORD thread;
    HWND hwnd;
} WATCHED;

static WATCHED g_roots[ROOT_MAX];
static HANDLE g_watcher;
static int g_advised[64];
static int g_advised_count;

typedef struct ADVISE_CALL {
    NODE *node;
    BOOL added;
    int event;
    SAFEARRAY *props;
} ADVISE_CALL;

static HRESULT advise_on_thread(void *context)
{
    ADVISE_CALL *c = context;
    return c->node->ops->advise(c->node, c->added, c->event, c->props);
}

static void advise_root(const WATCHED *root, BOOL added, int event, const int *props, int nprops)
{
    SAFEARRAY *array = nullptr;
    if (nprops)
        UiaIntArrayToSafeArray(props, nprops, &array);
    ADVISE_CALL call = {root->node, added, event, array};
    UiaRunOnThread(root->thread, advise_on_thread, &call);
    if (array)
        SafeArrayDestroy(array);
}

/* the events and properties clients want now, as ids, with the properties of property changes. */
static int events_wanted(int *events, int max, int *props, int *nprops)
{
    int n = 0;
    *nprops = 0;
    if (!reg_open())
        return 0;
    reg_lock();
    for (int i = 0; i < REG_MAX; i++) {
        REG_ENTRY *e = &g_reg->entries[i];
        if (!e->used)
            continue;
        BOOL seen = FALSE;
        for (int j = 0; j < n; j++)
            seen |= events[j] == e->event_id;
        if (!seen && n < max)
            events[n++] = e->event_id;
        if (e->event_id == UIA_AutomationPropertyChangedEventId)
            for (int p = 0; p < e->nprops && *nprops < PROP_MAX; p++) {
                BOOL have = FALSE;
                for (int j = 0; j < *nprops; j++)
                    have |= props[j] == e->props[p];
                if (!have)
                    props[(*nprops)++] = e->props[p];
            }
    }
    reg_unlock();
    return n;
}

static void sync_advice(void)
{
    int events[64], props[PROP_MAX], nprops;
    int n = events_wanted(events, 64, props, &nprops);
    state_lock();
    WATCHED roots[ROOT_MAX];
    memcpy(roots, g_roots, sizeof(roots));
    for (int i = 0; i < ROOT_MAX; i++)
        if (roots[i].node)
            NodeAddRef(roots[i].node);
    int advised[64], advised_count = g_advised_count;
    memcpy(advised, g_advised, sizeof(advised));
    memcpy(g_advised, events, sizeof(int) * (size_t)n);
    g_advised_count = n;
    LeaveCriticalSection(&g_state_lock);
    for (int r = 0; r < ROOT_MAX; r++) {
        if (!roots[r].node)
            continue;
        for (int i = 0; i < n; i++) {
            BOOL known = FALSE;
            for (int j = 0; j < advised_count; j++)
                known |= advised[j] == events[i];
            if (!known)
                advise_root(&roots[r], TRUE, events[i], events[i] == UIA_AutomationPropertyChangedEventId ? props : nullptr,
                            events[i] == UIA_AutomationPropertyChangedEventId ? nprops : 0);
        }
        for (int j = 0; j < advised_count; j++) {
            BOOL still = FALSE;
            for (int i = 0; i < n; i++)
                still |= advised[j] == events[i];
            if (!still)
                advise_root(&roots[r], FALSE, advised[j], nullptr, 0);
        }
        NodeRelease(roots[r].node);
    }
}

static DWORD WINAPI watcher_main(void *unused)
{
    LONG seen = -1;
    for (;;) {
        Sleep(200);
        if (!reg_open())
            continue;
        LONG now = g_reg->sequence;
        if (now != seen) {
            seen = now;
            sync_advice();
        }
        /* roots of windows that are gone are let go. */
        state_lock();
        for (int i = 0; i < ROOT_MAX; i++)
            if (g_roots[i].node && !IsWindow(g_roots[i].hwnd)) {
                NODE *dead = g_roots[i].node;
                g_roots[i].node = nullptr;
                LeaveCriticalSection(&g_state_lock);
                NodeRelease(dead);
                state_lock();
            }
        LeaveCriticalSection(&g_state_lock);
    }
    return 0;
}

void UiaProviderWatchStart(void)
{
    state_lock();
    if (!g_watcher)
        g_watcher = CreateThread(nullptr, 0, watcher_main, nullptr, 0, nullptr);
    LeaveCriticalSection(&g_state_lock);
}

void UiaProviderWatchRemember(NODE *root)
{
    HWND hwnd = NodeOwnerWindow(root);
    UiaProviderWatchStart();
    state_lock();
    int slot = -1;
    for (int i = 0; i < ROOT_MAX; i++)
        if (g_roots[i].node && g_roots[i].hwnd == hwnd && hwnd)
            slot = i;
    for (int i = 0; slot < 0 && i < ROOT_MAX; i++)
        if (!g_roots[i].node)
            slot = i;
    NODE *old = nullptr;
    if (slot >= 0) {
        old = g_roots[slot].node;
        NodeAddRef(root);
        g_roots[slot].node = root;
        g_roots[slot].thread = GetCurrentThreadId();
        g_roots[slot].hwnd = hwnd;
    }
    LeaveCriticalSection(&g_state_lock);
    NodeRelease(old);
    if (slot < 0)
        return;
    /* a root that appears after clients registered hears what they want at once. */
    int events[64], props[PROP_MAX], nprops;
    int n = events_wanted(events, 64, props, &nprops);
    WATCHED w = {root, GetCurrentThreadId(), hwnd};
    for (int i = 0; i < n; i++)
        w.node->ops->advise(w.node, TRUE, events[i], nullptr);
    (void)props;
}

void UiaEventsProcessDetach(void)
{
    if (!g_reg || !g_mutex)
        return;
    DWORD pid = GetCurrentProcessId();
    for (int i = 0; i < REG_MAX; i++) {
        REG_ENTRY *e = &g_reg->entries[i];
        if (e->used && e->pid == pid) {
            e->used = 0;
            InterlockedIncrement(&g_reg->sequence);
        }
    }
}

/* ------------------------------------------------------------------ windows with providers */

void UiaProviderTableAdd(HWND hwnd)
{
    if (!reg_open())
        return;
    DWORD id = (DWORD)(ULONG_PTR)hwnd;
    reg_lock();
    int free_slot = -1;
    BOOL found = FALSE;
    for (int i = 0; i < 256; i++) {
        DWORD v = g_reg->providers[i];
        if (v == id)
            found = TRUE;
        else if (v && !IsWindow((HWND)(ULONG_PTR)v))
            g_reg->providers[i] = 0;
        if (!g_reg->providers[i] && free_slot < 0)
            free_slot = i;
    }
    if (!found && free_slot >= 0)
        g_reg->providers[free_slot] = id;
    reg_unlock();
}

BOOL UiaProviderTableHas(HWND hwnd)
{
    if (!reg_open())
        return FALSE;
    DWORD id = (DWORD)(ULONG_PTR)hwnd;
    BOOL found = FALSE;
    reg_lock();
    for (int i = 0; i < 256 && !found; i++)
        found = g_reg->providers[i] == id;
    reg_unlock();
    return found;
}
