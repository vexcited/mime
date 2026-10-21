/*
 * services.exe: the start of the machine's services, the timers that delay things, and the end of them all.
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

/* ------------------------------------------------------------------ the start */

static HANDLE g_autostart_event;

static BOOL autostart_candidate(SERVICE *s)
{
    return DbLoadRecord(s) && (s->Type & (SERVICE_WIN32 | SERVICE_USER_SERVICE)) && !(s->Type & SERVICE_USERSERVICE_INSTANCE) &&
           !(s->Type & SERVICE_USER_SERVICE) && s->Start == SERVICE_AUTO_START;
}

/* the order windows starts them in: by group, in the order ServiceGroupOrder lists them, and by name within. */
static int by_group(const void *a, const void *b)
{
    SERVICE *x = *(SERVICE *const *)a, *y = *(SERVICE *const *)b;
    DWORD gx = DbGroupIndex(x->Group), gy = DbGroupIndex(y->Group);
    if (gx != gy)
        return gx < gy ? -1 : 1;
    if (x->Tag != y->Tag)
        return x->Tag < y->Tag ? -1 : 1;
    return _wcsicmp(x->Name, y->Name);
}

static void sort_services(SERVICE **list, DWORD n)
{
    /* insertion sort: the lists are short. */
    for (DWORD i = 1; i < n; i++) {
        SERVICE *s = list[i];
        DWORD j = i;
        while (j > 0 && by_group(&list[j - 1], &s) > 0) {
            list[j] = list[j - 1];
            j--;
        }
        list[j] = s;
    }
}

static void start_list(SERVICE **list, DWORD n, DWORD reason)
{
    for (DWORD i = 0; i < n; i++) {
        SERVICE *s = list[i];
        if (g_shutdown)
            return;
        if (s->Status.dwCurrentState != SERVICE_STOPPED)
            continue;
        /* a service whose program is not in the installation is not tried: windows logs an event for each. */
        if (!RunImageExists(s)) {
            s->StartError = ERROR_FILE_NOT_FOUND;
            continue;
        }
        DWORD error = RunStart(s, 0, nullptr, reason);
        if (error && error != ERROR_SERVICE_ALREADY_RUNNING)
            SvcLog("%ls did not start at boot: %lu", s->Name, error);
    }
}

typedef struct DELAYED {
    SERVICE **List;
    DWORD Count;
} DELAYED;

static DWORD WINAPI delayed_start(LPVOID context)
{
    DELAYED *d = context;
    EnterCriticalSection(&g_lock);
    /* the names, not the records, were kept; a record that went is not started. */
    SERVICE **live = SvcAlloc((d->Count + 1) * sizeof(*live));
    DWORD n = 0;
    for (DWORD i = 0; i < d->Count; i++) {
        for (SERVICE *s = g_services; s; s = s->Next)
            if (s == d->List[i]) {
                live[n++] = s;
                break;
            }
    }
    if (live)
        start_list(live, n, 2);
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
            /* both lists grow together: they are of one capacity. */
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
        sort_services(normal, normal_count);
        start_list(normal, normal_count, 2);
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
    /* programs wait for the services to be done with their start on this event. */
    g_autostart_event = SvcCreateWorldEvent(SCM_AUTOSTART_EVENT);
    HANDLE t = CreateThread(nullptr, 0, autostart_thread, nullptr, 0, nullptr);
    if (t)
        CloseHandle(t);
}

/* ------------------------------------------------------------------ the end */

#define POLL_MS 10
#define PRESHUTDOWN_TIME_MS 10000
#define SHUTDOWN_HINT_MS 20000
#define SHUTDOWN_LIMIT_MS 30000
#define IN_FLIGHT_GRACE_MS 1000

/* a set of services the end of the machine works on. */
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

/* a program with a process that has not stopped yet and is not on its way to. */
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

/* a record that is deleted while the lock is let go of must not go; the handle count keeps it. */
static void pin(SERVICE *s)
{
    s->OpenHandles++;
}

static void unpin(SERVICE *s)
{
    if (s->OpenHandles)
        s->OpenHandles--;
}

/* waits, with the lock let go of in between, for the service to stop or for the time to pass. */
static BOOL stopped_within(SERVICE *s, DWORD ms)
{
    ULONGLONG limit = GetTickCount64() + ms;
    pin(s);
    while (s->Status.dwCurrentState != SERVICE_STOPPED && GetTickCount64() < limit)
        SvcSleepChanged(POLL_MS);
    unpin(s);
    return s->Status.dwCurrentState == SERVICE_STOPPED;
}

/* the time the key of a service allows its preparation for the end, in milliseconds. */
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

/* the services the configuration names to be prepared first, in its order. */
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

    /* the others are all asked first, and then waited for one after the other. */
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

/* how long the machine waits for the services it told to shut down: the slowest of them, within the limit. */
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

/* one shutdown control on its own thread, so that a slow handler holds up nobody else. */
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

    /* a control still being sent gets a moment to come back, the lock free for it to finish. */
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
    /* no more services are started or created from here on. */
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
    /* below the programs' level, which is 0x280, and above the service processes'. */
    SetProcessShutdownParameters(0x1FF, 0);
}
