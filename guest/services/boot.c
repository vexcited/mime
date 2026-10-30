/*
 * services.exe: starting the machine's services at boot, timers, and shutdown.
 */
#include "services.h"

/* ------------------------------------------------------------------ timers */

typedef struct TIMER {
    struct TIMER *Next;
    ULONGLONG Due;
    void (*Callback)(void *);
    void *Context;
} TIMER;

static CRITICAL_SECTION g_timer_lock;
static TIMER *g_timers;
static HANDLE g_timer_event;

void TimerAdd(DWORD delay, void (*callback)(void *), void *context)
{
    TIMER *t = SvcAllocZero(sizeof(*t));
    if (!t)
        return;
    t->Due = GetTickCount64() + delay;
    t->Callback = callback;
    t->Context = context;
    EnterCriticalSection(&g_timer_lock);
    TIMER **link = &g_timers;
    while (*link && (*link)->Due <= t->Due)
        link = &(*link)->Next;
    t->Next = *link;
    *link = t;
    LeaveCriticalSection(&g_timer_lock);
    SetEvent(g_timer_event);
}

static DWORD WINAPI timer_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        EnterCriticalSection(&g_timer_lock);
        TIMER *t = g_timers;
        ULONGLONG now = GetTickCount64();
        DWORD wait = INFINITE;
        if (t) {
            if (t->Due <= now) {
                g_timers = t->Next;
                LeaveCriticalSection(&g_timer_lock);
                t->Callback(t->Context);
                SvcFree(t);
                continue;
            }
            wait = (DWORD)(t->Due - now);
        }
        LeaveCriticalSection(&g_timer_lock);
        WaitForSingleObject(g_timer_event, wait);
    }
}

void TimerInit(void)
{
    InitializeCriticalSection(&g_timer_lock);
    g_timer_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, timer_thread, nullptr, 0, nullptr);
    if (t)
        CloseHandle(t);
}

/* ------------------------------------------------------------------ boot start */

static HANDLE g_autostart_event;

static BOOL autostart_candidate(SERVICE *s)
{
    return DbLoadRecord(s) && (s->Type & (SERVICE_WIN32 | SERVICE_USER_SERVICE)) && !(s->Type & SERVICE_USERSERVICE_INSTANCE) &&
           !(s->Type & SERVICE_USER_SERVICE) && s->Start == SERVICE_AUTO_START;
}

/* the lists are sorted by the position of the group in ServiceGroupOrder, services of one group in the order met. */
static void sort_services(SERVICE **list, DWORD n)
{
    for (DWORD i = 1; i < n; i++) {
        SERVICE *s = list[i];
        DWORD key = DbGroupIndex(s->Group), j = i;
        while (j > 0 && DbGroupIndex(list[j - 1]->Group) > key) {
            list[j] = list[j - 1];
            j--;
        }
        list[j] = s;
    }
}

/* the record of a list is held while the lock is let go, and deleted services leave it. */
static void pin_all(SERVICE **list, DWORD n)
{
    for (DWORD i = 0; i < n; i++)
        list[i]->OpenHandles++;
}

static void unpin_all(SERVICE **list, DWORD n)
{
    for (DWORD i = 0; i < n; i++)
        if (list[i]->OpenHandles)
            list[i]->OpenHandles--;
    MainCollect();
}

typedef struct DELAYED {
    SERVICE **List;
    DWORD Count;
} DELAYED;

/* the delayed services start one after the other, each with what it needs. */
static DWORD WINAPI delayed_start(LPVOID context)
{
    DELAYED *d = context;
    EnterCriticalSection(&g_lock);
    /* records deleted since the list was made are not started. */
    SERVICE **live = SvcAlloc((d->Count + 1) * sizeof(*live));
    DWORD n = 0;
    for (DWORD i = 0; i < d->Count; i++) {
        for (SERVICE *s = g_services; s; s = s->Next)
            if (s == d->List[i]) {
                live[n++] = s;
                break;
            }
    }
    if (live) {
        pin_all(live, n);
        for (DWORD i = 0; i < n && !g_shutdown; i++) {
            if (live[i]->Status.dwCurrentState != SERVICE_STOPPED)
                continue;
            DWORD error = RunStart(live[i], 0, nullptr, START_DELAYED);
            if (error && error != ERROR_SERVICE_ALREADY_RUNNING)
                SvcLog("%ls did not start after the boot: %lu", live[i]->Name, error);
        }
        unpin_all(live, n);
    }
    SvcFree(live);
    LeaveCriticalSection(&g_lock);
    SvcFree(d->List);
    SvcFree(d);
    return 0;
}

static void delayed_timer(void *context)
{
    HANDLE t = CreateThread(nullptr, 0, delayed_start, context, 0, nullptr);
    if (t) {
        CloseHandle(t);
    } else {
        DELAYED *d = context;
        SvcFree(d->List);
        SvcFree(d);
    }
}

/* a service of the second phase starts in a thread of its own, with the services it needs. */
static DWORD WINAPI auto_worker(LPVOID context)
{
    SERVICE *s = context;
    EnterCriticalSection(&g_lock);
    DWORD error = RunStart(s, 0, nullptr, START_AUTO);
    if (error && error != ERROR_SERVICE_ALREADY_RUNNING)
        SvcLog("%ls did not start at boot: %lu", s->Name, error);
    if (s->OpenHandles)
        s->OpenHandles--;
    LeaveCriticalSection(&g_lock);
    return 0;
}

/*
 * the second phase: the automatic services by group, up to 64 of a group at once. The next batch waits until every
 * service of this one has started, not until it runs.
 */
static void start_second_phase(SERVICE **list, DWORD n)
{
    RunStartTimeoutInit();
    for (DWORD i = 0; i < n && !g_shutdown;) {
        DWORD group = DbGroupIndex(list[i]->Group);
        SERVICE *batch[64];
        DWORD marked = 0;
        while (i < n && marked < RTL_NUMBER_OF(batch)) {
            SERVICE *s = list[i];
            if (RunStartableNow(s)) {
                if (DbGroupIndex(s->Group) != group)
                    break;
                if (RunMarkForStart(s))
                    batch[marked++] = s;
            }
            i++;
        }
        HANDLE threads[64];
        DWORD started = 0;
        for (DWORD j = 0; j < marked; j++) {
            SERVICE *s = batch[j];
            if (s->Status.dwCurrentState != SERVICE_STOPPED || s->StartState == SS_FAILED)
                continue;
            s->OpenHandles++;
            threads[started] = CreateThread(nullptr, 0, auto_worker, s, 0, nullptr);
            if (threads[started])
                started++;
            else
                s->OpenHandles--;
        }
        if (started) {
            LeaveCriticalSection(&g_lock);
            if (WaitForMultipleObjects(started, threads, TRUE, g_start_timeout) == WAIT_TIMEOUT)
                SvcLog("a batch of services did not finish starting in time");
            for (DWORD j = 0; j < started; j++)
                CloseHandle(threads[j]);
            EnterCriticalSection(&g_lock);
        }
    }
}

/* the services Control\EarlyStartServices names, which start first with what they depend on. */
static DWORD collect_early(SERVICE **list, DWORD n, SERVICE **roots)
{
    HKEY key;
    WCHAR *names = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        names = DbReadString(key, L"EarlyStartServices", nullptr);
        RegCloseKey(key);
    }
    DWORD count = 0;
    for (DWORD i = 0; i < n; i++) {
        BOOL early = !names;
        for (const WCHAR *name = names; name && *name && !early; name += wcslen(name) + 1)
            early = !_wcsicmp(name, list[i]->Name);
        if (early)
            roots[count++] = list[i];
    }
    SvcFree(names);
    return count;
}

/* the services the manager starts first by name, when the machine has them. */
static void start_by_name(const WCHAR *name)
{
    SERVICE *s = DbFind(name);
    if (s && DbLoadRecord(s) && RunImageExists(s) && s->Status.dwCurrentState == SERVICE_STOPPED)
        RunStart(s, 0, nullptr, START_WAIT);
}

static DWORD WINAPI autostart_thread(LPVOID unused)
{
    (void)unused;
    EnterCriticalSection(&g_lock);
    g_autostart_running = TRUE;
    DWORD capacity = 64, normal_count = 0, delayed_count = 0;
    SERVICE **normal = SvcAlloc(capacity * sizeof(*normal));
    SERVICE **delayed = SvcAlloc(capacity * sizeof(*delayed));
    for (SERVICE *s = g_services; s && normal && delayed; s = s->Next) {
        if (!autostart_candidate(s))
            continue;
        SERVICE ***target = s->DelayedStart ? &delayed : &normal;
        DWORD *count = s->DelayedStart ? &delayed_count : &normal_count;
        if (*count == capacity) {
            /* both lists share one capacity, so they grow together. */
            capacity *= 2;
            SERVICE **a = SvcAlloc(capacity * sizeof(*a)), **b = SvcAlloc(capacity * sizeof(*b));
            if (!a || !b) {
                SvcFree(a);
                SvcFree(b);
                break;
            }
            memcpy(a, normal, normal_count * sizeof(*a));
            memcpy(b, delayed, delayed_count * sizeof(*b));
            SvcFree(normal);
            SvcFree(delayed);
            normal = a;
            delayed = b;
        }
        (*target)[(*count)++] = s;
    }
    if (normal) {
        pin_all(normal, normal_count);
        start_by_name(L"PlugPlay");
        start_by_name(L"Power");
        /* the first phase: the early services and what they need, one group after the other, each waited for. */
        SERVICE **roots = SvcAlloc((normal_count + 1) * sizeof(*roots));
        if (roots) {
            g_phase1_running = TRUE;
            DWORD n = collect_early(normal, normal_count, roots);
            if (n)
                RunStartMany(roots, n, START_AUTO);
            g_phase1_running = FALSE;
            SvcFree(roots);
        }
        sort_services(normal, normal_count);
        start_second_phase(normal, normal_count);
        unpin_all(normal, normal_count);
    }
    g_autostart_running = FALSE;
    g_database_ready = TRUE;
    LeaveCriticalSection(&g_lock);
    SvcFree(normal);
    if (g_autostart_event)
        SetEvent(g_autostart_event);
    if (delayed && delayed_count) {
        sort_services(delayed, delayed_count);
        DELAYED *d = SvcAllocZero(sizeof(*d));
        DWORD delay = 120000;
        HKEY key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_READ, &key) == ERROR_SUCCESS) {
            DbReadDword(key, L"AutostartDelay", &delay);
            RegCloseKey(key);
        }
        if (d) {
            d->List = delayed;
            d->Count = delayed_count;
            TimerAdd(delay, delayed_timer, d);
            delayed = nullptr;
        }
    }
    SvcFree(delayed);
    return 0;
}

void BootStart(void)
{
    /* programs wait on this event for the services to finish starting. */
    g_autostart_event = SvcCreateWorldEvent(SCM_AUTOSTART_EVENT);
    HANDLE t = CreateThread(nullptr, 0, autostart_thread, nullptr, 0, nullptr);
    if (t)
        CloseHandle(t);
}

/* ------------------------------------------------------------------ shutdown */

#define POLL_MS 10
#define PRESHUTDOWN_TIME_MS 10000
#define SHUTDOWN_HINT_MS 20000
#define SHUTDOWN_LIMIT_MS 30000
#define IN_FLIGHT_GRACE_MS 1000

typedef struct SERVICE_SET {
    SERVICE **Items;
    DWORD Count;
    DWORD Capacity;
} SERVICE_SET;

static BOOL set_has(const SERVICE_SET *set, const SERVICE *s)
{
    for (DWORD i = 0; i < set->Count; i++)
        if (set->Items[i] == s)
            return TRUE;
    return FALSE;
}

static void set_add(SERVICE_SET *set, SERVICE *s)
{
    if (set->Count == set->Capacity) {
        DWORD capacity = set->Capacity ? set->Capacity * 2 : 32;
        SERVICE **grown = SvcAlloc(capacity * sizeof(*grown));
        if (!grown)
            return;
        if (set->Count)
            memcpy(grown, set->Items, set->Count * sizeof(*grown));
        SvcFree(set->Items);
        set->Items = grown;
        set->Capacity = capacity;
    }
    set->Items[set->Count++] = s;
}

/* a program whose process is neither stopped nor stopping. */
static BOOL still_up(const SERVICE *s)
{
    DWORD state = s->Status.dwCurrentState;
    return s->Loaded && (s->Type & SERVICE_WIN32) && s->Image && state != SERVICE_STOPPED &&
           state != SERVICE_STOP_PENDING;
}

static BOOL takes(const SERVICE *s, DWORD accepted_bit)
{
    return (s->Status.dwControlsAccepted & accepted_bit) != 0;
}

/* the handle count keeps a record alive if it is deleted while the lock is released. */
static void pin(SERVICE *s)
{
    s->OpenHandles++;
}

static void unpin(SERVICE *s)
{
    if (s->OpenHandles)
        s->OpenHandles--;
}

/* waits for the service to stop or the time to run out, releasing the lock in between. */
static BOOL stopped_within(SERVICE *s, DWORD ms)
{
    ULONGLONG limit = GetTickCount64() + ms;
    pin(s);
    while (s->Status.dwCurrentState != SERVICE_STOPPED && GetTickCount64() < limit)
        SvcSleepChanged(POLL_MS);
    unpin(s);
    return s->Status.dwCurrentState == SERVICE_STOPPED;
}

/* the service key's PreshutdownTimeout, in milliseconds. */
static DWORD preshutdown_allowance(const SERVICE *s)
{
    DWORD allowance = PRESHUTDOWN_TIME_MS;
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (key) {
        DWORD configured;
        if (DbReadDword(key, L"PreshutdownTimeout", &configured))
            allowance = configured;
        RegCloseKey(key);
    }
    return allowance;
}

/* the services PreshutdownOrder names, in its order. */
static void collect_named_preshutdown(SERVICE_SET *named)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;
    WCHAR *order = DbReadString(key, L"PreshutdownOrder", nullptr);
    RegCloseKey(key);
    for (const WCHAR *name = order; name && *name; name += wcslen(name) + 1) {
        SERVICE *s = DbFind(name);
        if (s && DbLoadRecord(s) && still_up(s) && takes(s, SERVICE_ACCEPT_PRESHUTDOWN) && !set_has(named, s))
            set_add(named, s);
    }
    SvcFree(order);
}

static void preshutdown(void)
{
    SERVICE_SET named = {};
    collect_named_preshutdown(&named);
    DWORD ignored;
    for (DWORD i = 0; i < named.Count; i++) {
        SERVICE *s = named.Items[i];
        if (RunControl(s, SERVICE_CONTROL_PRESHUTDOWN, 0, nullptr, 0, &ignored) == ERROR_SUCCESS)
            stopped_within(s, preshutdown_allowance(s));
    }

    /* the others are all notified first, then waited for one by one. */
    SERVICE_SET others = {};
    for (SERVICE *s = g_services; s; s = s->Next)
        if (still_up(s) && takes(s, SERVICE_ACCEPT_PRESHUTDOWN) && !set_has(&named, s))
            set_add(&others, s);
    SERVICE_SET asked = {};
    for (DWORD i = 0; i < others.Count; i++)
        if (RunControl(others.Items[i], SERVICE_CONTROL_PRESHUTDOWN, 0, nullptr, 0, &ignored) == ERROR_SUCCESS)
            set_add(&asked, others.Items[i]);
    for (DWORD i = 0; i < asked.Count; i++)
        stopped_within(asked.Items[i], preshutdown_allowance(asked.Items[i]));

    SvcFree(named.Items);
    SvcFree(others.Items);
    SvcFree(asked.Items);
}

/* the wait for the services told to shut down: the slowest one's hint, capped at the limit. */
static DWORD shutdown_budget(const SERVICE_SET *set)
{
    DWORD budget = 0;
    for (DWORD i = 0; i < set->Count; i++) {
        DWORD hint = set->Items[i]->Status.dwWaitHint;
        DWORD needs = hint ? hint : SHUTDOWN_HINT_MS;
        if (needs > budget)
            budget = needs;
    }
    return budget < SHUTDOWN_LIMIT_MS ? budget : SHUTDOWN_LIMIT_MS;
}

/* one thread per shutdown control, so a slow handler holds up nobody else. */
static DWORD WINAPI send_shutdown(LPVOID context)
{
    SERVICE *s = context;
    DWORD ignored;
    EnterCriticalSection(&g_lock);
    RunControl(s, SERVICE_CONTROL_SHUTDOWN, 0, nullptr, 0, &ignored);
    unpin(s);
    LeaveCriticalSection(&g_lock);
    return 0;
}

static BOOL all_stopped(const SERVICE_SET *set)
{
    for (DWORD i = 0; i < set->Count; i++)
        if (set->Items[i]->Status.dwCurrentState != SERVICE_STOPPED)
            return FALSE;
    return TRUE;
}

static void shutdown_services(void)
{
    SERVICE_SET chosen = {};
    for (SERVICE *s = g_services; s; s = s->Next)
        if (still_up(s) && takes(s, SERVICE_ACCEPT_SHUTDOWN))
            set_add(&chosen, s);

    HANDLE *senders = chosen.Count ? SvcAllocZero(chosen.Count * sizeof(*senders)) : nullptr;
    for (DWORD i = 0; senders && i < chosen.Count; i++) {
        SERVICE *s = chosen.Items[i];
        s->ShutdownStopping = TRUE;
        pin(s);
        senders[i] = CreateThread(nullptr, 0, send_shutdown, s, 0, nullptr);
        if (!senders[i])
            unpin(s);
    }

    ULONGLONG limit = GetTickCount64() + shutdown_budget(&chosen);
    while (chosen.Count && !all_stopped(&chosen) && GetTickCount64() < limit)
        SvcSleepChanged(POLL_MS);

    /* controls still in flight get a short grace period, with the lock free so they can finish. */
    LeaveCriticalSection(&g_lock);
    for (DWORD i = 0; senders && i < chosen.Count; i++) {
        if (senders[i]) {
            WaitForSingleObject(senders[i], IN_FLIGHT_GRACE_MS);
            CloseHandle(senders[i]);
        }
    }
    EnterCriticalSection(&g_lock);
    SvcFree(senders);
    SvcFree(chosen.Items);
}

static BOOL WINAPI console_handler(DWORD event)
{
    if (event != 6 /* CTRL_SHUTDOWN_EVENT */)
        return TRUE;
    EnterCriticalSection(&g_lock);
    /* no services are started or created from here on. */
    g_shutdown = TRUE;
    WakeAllConditionVariable(&g_changed);
    preshutdown();
    shutdown_services();
    LeaveCriticalSection(&g_lock);
    return TRUE;
}

void ShutdownInit(void)
{
    SetConsoleCtrlHandler(console_handler, TRUE);
    /* below the programs' level of 0x280 and above the service processes'. */
    SetProcessShutdownParameters(0x1FF, 0);
}
