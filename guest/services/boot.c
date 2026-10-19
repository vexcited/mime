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
        SERVICE ***target = (s->Flags & SVF_DELAYED) ? &delayed : &normal;
        DWORD *count = (s->Flags & SVF_DELAYED) ? &delayed_count : &normal_count;
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
    g_autostart_event = CreateEventW(nullptr, TRUE, FALSE, SCM_AUTOSTART_EVENT);
    HANDLE t = CreateThread(nullptr, 0, autostart_thread, nullptr, 0, nullptr);
    if (t)
        CloseHandle(t);
}

/* ------------------------------------------------------------------ the end */

typedef struct ORDERED {
    SERVICE *Service;
    ULONGLONG Sent;
    BOOL Failed;
} ORDERED;

static DWORD preshutdown_timeout(SERVICE *s)
{
    DWORD timeout = 10000;
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (key) {
        DbReadDword(key, L"PreshutdownTimeout", &timeout);
        RegCloseKey(key);
    }
    return timeout;
}

/*
 * waits for a service to stop after the control, polling as windows does every ten milliseconds, for the time its
 * key gives it. a service that counts a checkpoint while it stops is given more time, but never more than the whole.
 */
static BOOL wait_stopped(SERVICE *s, DWORD timeout)
{
    ULONGLONG deadline = GetTickCount64() + timeout;
    for (;;) {
        DWORD state = s->Status.dwCurrentState;
        if (state == SERVICE_STOPPED)
            return TRUE;
        if (GetTickCount64() >= deadline)
            return FALSE;
        SvcSleepChanged(10);
    }
}

typedef struct SHUTDOWN_JOB {
    SERVICE *Service;
    HANDLE Done;
} SHUTDOWN_JOB;

static DWORD WINAPI shutdown_control(LPVOID context)
{
    SHUTDOWN_JOB *job = context;
    EnterCriticalSection(&g_lock);
    DWORD result;
    RunControl(job->Service, SERVICE_CONTROL_SHUTDOWN, 0, nullptr, 0, &result);
    LeaveCriticalSection(&g_lock);
    SetEvent(job->Done);
    return 0;
}

static void preshutdown(void)
{
    /* the services that take the control, those the order names first. */
    DWORD capacity = 32, n = 0, ordered = 0;
    SERVICE **list = SvcAlloc(capacity * sizeof(*list));
    if (!list)
        return;
    HKEY key;
    WCHAR *order = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        order = DbReadString(key, L"PreshutdownOrder", nullptr);
        RegCloseKey(key);
    }
    for (const WCHAR *name = order; name && *name; name += wcslen(name) + 1) {
        SERVICE *s = DbFind(name);
        if (s && DbLoadRecord(s) && (s->Type & SERVICE_WIN32) && s->Image &&
            (s->Status.dwControlsAccepted & SERVICE_ACCEPT_PRESHUTDOWN) &&
            s->Status.dwCurrentState != SERVICE_STOPPED && s->Status.dwCurrentState != SERVICE_STOP_PENDING && n < capacity)
            list[n++] = s;
    }
    ordered = n;
    for (SERVICE *s = g_services; s; s = s->Next) {
        if (!(s->Loaded && (s->Type & SERVICE_WIN32) && s->Image && (s->Status.dwControlsAccepted & SERVICE_ACCEPT_PRESHUTDOWN)) ||
            s->Status.dwCurrentState == SERVICE_STOPPED || s->Status.dwCurrentState == SERVICE_STOP_PENDING)
            continue;
        BOOL there = FALSE;
        for (DWORD i = 0; i < ordered; i++)
            there |= list[i] == s;
        if (!there && n < capacity)
            list[n++] = s;
    }
    SvcFree(order);
    DWORD result;
    /* the ordered ones one at a time, each given its time to stop before the next is asked. */
    for (DWORD i = 0; i < ordered; i++) {
        if (RunControl(list[i], SERVICE_CONTROL_PRESHUTDOWN, 0, nullptr, 0, &result) == ERROR_SUCCESS)
            wait_stopped(list[i], preshutdown_timeout(list[i]));
    }
    /* the others are all asked, and then waited for. */
    BOOL sent[32] = {};
    for (DWORD i = ordered; i < n; i++)
        sent[i] = RunControl(list[i], SERVICE_CONTROL_PRESHUTDOWN, 0, nullptr, 0, &result) == ERROR_SUCCESS;
    for (DWORD i = ordered; i < n; i++)
        if (sent[i])
            wait_stopped(list[i], preshutdown_timeout(list[i]));
    SvcFree(list);
}

static void shutdown_services(void)
{
    DWORD capacity = 64, n = 0, timeout = 0;
    SHUTDOWN_JOB *jobs = SvcAllocZero(capacity * sizeof(*jobs));
    if (!jobs)
        return;
    for (SERVICE *s = g_services; s; s = s->Next) {
        if (!s->Loaded || !(s->Type & SERVICE_WIN32) || !s->Image || !(s->Status.dwControlsAccepted & SERVICE_ACCEPT_SHUTDOWN) ||
            s->Status.dwCurrentState == SERVICE_STOPPED || s->Status.dwCurrentState == SERVICE_STOP_PENDING || n == capacity)
            continue;
        jobs[n].Service = s;
        jobs[n].Done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD hint = s->Status.dwWaitHint;
        if (hint)
            timeout = hint > timeout ? hint : timeout;
        else if (timeout <= 20000)
            timeout = 20000;
        n++;
    }
    if (timeout > 30000)
        timeout = 30000;
    for (DWORD i = 0; i < n; i++) {
        HANDLE t = CreateThread(nullptr, 0, shutdown_control, &jobs[i], 0, nullptr);
        if (t)
            CloseHandle(t);
        else
            SetEvent(jobs[i].Done);
    }
    ULONGLONG deadline = GetTickCount64() + timeout;
    for (;;) {
        BOOL all = TRUE;
        for (DWORD i = 0; i < n; i++)
            if (jobs[i].Service->Status.dwCurrentState != SERVICE_STOPPED)
                all = FALSE;
        if (all || GetTickCount64() >= deadline)
            break;
        SvcSleepChanged(10);
    }
    /* the controls in flight are given a moment to return, and the services left are not waited for. */
    LeaveCriticalSection(&g_lock);
    for (DWORD i = 0; i < n; i++) {
        WaitForSingleObject(jobs[i].Done, 1000);
        CloseHandle(jobs[i].Done);
    }
    EnterCriticalSection(&g_lock);
    SvcFree(jobs);
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
