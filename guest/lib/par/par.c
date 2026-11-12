#include "par.h"

#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>

#define MAX_WORKERS 3
/* how long a worker stays awake after a job, in microseconds. waking a sleeping thread costs more than short jobs. */
#define AWAKE_US 200

static struct {
    LONG volatile busy;
    LONG volatile started;
    LONG workers;
    LONG volatile open;
    LONG volatile active;
    LONG volatile seq;
    LONG volatile sleepers;
    LONG volatile next;
    LONG end, grain;
    MimeParFn fn;
    void *ctx;
    LONGLONG awake_ticks;
} par;

static void take_rows(void)
{
    for (;;) {
        LONG first = InterlockedExchangeAdd(&par.next, par.grain);
        if (first >= par.end)
            return;
        LONG end = first + par.grain;
        par.fn(par.ctx, first, end < par.end ? end : par.end);
    }
}

static DWORD WINAPI worker(void *arg)
{
    (void)arg;
    for (;;) {
        LONG seen = par.seq;
        /* the caller closes the job and then waits for active to fall, so a job seen open stays whole. */
        InterlockedIncrement(&par.active);
        if (par.open)
            take_rows();
        InterlockedDecrement(&par.active);
        LARGE_INTEGER from, now;
        QueryPerformanceCounter(&from);
        while (par.seq == seen) {
            for (int i = 0; i < 32 && par.seq == seen; i++)
                YieldProcessor();
            QueryPerformanceCounter(&now);
            if (now.QuadPart - from.QuadPart > par.awake_ticks)
                break;
        }
        if (par.seq != seen)
            continue;
        InterlockedIncrement(&par.sleepers);
        if (par.seq == seen)
            RtlWaitOnAddress((const void *)&par.seq, &seen, sizeof(seen), nullptr);
        InterlockedDecrement(&par.sleepers);
    }
}

static void start_workers(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    LONG count = (LONG)info.dwNumberOfProcessors - 1;
    if (count > MAX_WORKERS)
        count = MAX_WORKERS;
    if (count < 1)
        return;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    par.awake_ticks = freq.QuadPart * AWAKE_US / 1000000;
    /* the workers run this module's code for the life of the process. */
    HMODULE self;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (const WCHAR *)(void *)start_workers, &self))
        return;
    for (LONG i = 0; i < count; i++) {
        HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (!thread)
            break;
        CloseHandle(thread);
        par.workers++;
    }
}

void mime_par_rows(int first, int end, int grain, MimeParFn fn, void *ctx)
{
    if (end <= first)
        return;
    if (grain < 1)
        grain = 1;
    if (end - first <= grain || InterlockedExchange(&par.busy, 1)) {
        fn(ctx, first, end);
        return;
    }
    if (!par.started) {
        par.started = 1;
        start_workers();
    }
    if (!par.workers) {
        InterlockedExchange(&par.busy, 0);
        fn(ctx, first, end);
        return;
    }
    par.fn = fn;
    par.ctx = ctx;
    par.end = end;
    par.grain = grain;
    InterlockedExchange(&par.next, first);
    InterlockedExchange(&par.open, 1);
    InterlockedIncrement(&par.seq);
    if (par.sleepers)
        RtlWakeAddressAll((const void *)&par.seq);
    take_rows();
    InterlockedExchange(&par.open, 0);
    while (par.active)
        YieldProcessor();
    InterlockedExchange(&par.busy, 0);
}
