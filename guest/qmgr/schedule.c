#include "qmgr.h"

#define REG_NOTIFY_NAME_ 0x1
#define REG_NOTIFY_LAST_SET_ 0x4
#define TICKS_PER_MS 10000ull
#define MAX_WAIT_MS 60000

static HANDLE g_wake;
static HANDLE g_policy_event;
static HANDLE g_thread;
static volatile BOOL g_stopping;
static BJOB *g_background;

void QmWake(void)
{
    if (g_wake)
        SetEvent(g_wake);
}

void RunRelease(RUN *run)
{
    if (InterlockedDecrement(&run->refs))
        return;
    if (run->thread)
        CloseHandle(run->thread);
    if (run->cancel)
        CloseHandle(run->cancel);
    LocalFree(run);
}

/* with the lock held: the worker still owns the transfer of its job. */
BOOL RunCurrent(RUN *run)
{
    return !run->stop && run->job->run == run;
}

/* the worker leaves: the job may get the next one, which the scheduler starts. */
void RunFinish(RUN *run)
{
    BJOB *job = run->job;
    Lock();
    if (job->run == run) {
        job->run = nullptr;
        RunRelease(run);
    }
    if (job->run_tick && job->state != BG_JOB_STATE_CONNECTING && job->state != BG_JOB_STATE_TRANSFERRING) {
        job->elapsed_ms += GetTickCount64() - job->run_tick;
        job->run_tick = 0;
    }
    Unlock();
    RunRelease(run);
    JobRelease(job);
    QmWake();
}

/* CJobManager::InterruptJob, with the lock held: the worker is told to stop and its request is cut, it is not waited for. */
void ScheduleInterrupt(BJOB *job)
{
    RUN *run = job->run;
    if (run && !run->stop) {
        run->stop = TRUE;
        SetEvent(run->cancel);
        XferAbort(&run->active);
    }
    if (g_background == job)
        g_background = nullptr;
    QmWake();
}

/* with the lock held exclusively: the lock is let go while the worker of the job ends, the scheduler leaves the job be. */
void ScheduleWaitIdle(BJOB *job)
{
    job->holding = TRUE;
    while (job->run) {
        RUN *run = job->run;
        if (!run->stop)
            ScheduleInterrupt(job);
        InterlockedIncrement(&run->refs);
        Unlock();
        WaitForSingleObject(run->thread, INFINITE);
        Lock();
        RunRelease(run);
    }
    job->holding = FALSE;
}

static void start(BJOB *job)
{
    RUN *run = LocalAlloc(LMEM_ZEROINIT, sizeof(*run));
    HANDLE cancel = run ? CreateEventW(nullptr, TRUE, FALSE, nullptr) : nullptr;
    if (!run || !cancel) {
        LocalFree(run);
        return;
    }
    run->refs = 2;
    run->job = job;
    run->cancel = cancel;
    JobAddRef(job);
    job->run = run;
    JobSetState(job, BG_JOB_STATE_CONNECTING);
    HRESULT hr = QmStartThread(job->type == BG_JOB_TYPE_DOWNLOAD ? DownloadWorker : UploadWorker, run, &run->thread);
    if (FAILED(hr)) {
        job->run = nullptr;
        RunRelease(run);
        RunRelease(run);
        JobRelease(job);
        JobTransientError(job, BG_ERROR_CONTEXT_GENERAL_QUEUE_MANAGER, hr, SOURCE_HRESULT, -1);
    }
}

static void consider(UINT64 *next, UINT64 when)
{
    if (when && when < *next)
        *next = when;
}

/* CJob::OnInactivityTimeout: a job left alone for JobInactivityTimeout is cancelled with its temporary files. */
static BOOL inactive(BJOB *job, UINT64 now, UINT64 *next)
{
    if (job->run || job->holding || job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING ||
        job->state == BG_JOB_STATE_QUEUED || job->callback_busy)
        return FALSE;
    UINT64 deadline = (UINT64)job->accessed + g_config.inactivity;
    UINT64 touched = QmTime(&job->modified) + g_config.inactivity;
    if (touched > deadline)
        deadline = touched;
    if (now < deadline) {
        consider(next, deadline);
        return FALSE;
    }
    JobDeleteTempFiles(job);
    NotifyCancel(job);
    JobSetState(job, BG_JOB_STATE_CANCELLED);
    JobRemove(job);
    return TRUE;
}

/* the timers of a job: retry, no progress and the maximum download time. */
static void timers(BJOB *job, UINT64 now, UINT64 *next)
{
    if (job->state == BG_JOB_STATE_TRANSIENT_ERROR) {
        if (job->no_progress_at && now >= job->no_progress_at) {
            /* CJob::OnNoProgress: the transient error becomes the job's fatal one. */
            JobSetState(job, BG_JOB_STATE_ERROR);
            job->pending_reason = PENDING_ERROR;
            NotifySchedule(job, 0);
            StoreSaveJob(job);
            return;
        }
        if (job->retry_at && now >= job->retry_at) {
            JobSetState(job, BG_JOB_STATE_QUEUED);
            StoreSaveJob(job);
        } else {
            consider(next, job->retry_at);
            consider(next, job->no_progress_at);
        }
    }
    if ((job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING) && job->max_time) {
        UINT64 elapsed = job->elapsed_ms + (job->run_tick ? GetTickCount64() - job->run_tick : 0);
        UINT64 limit = (UINT64)job->max_time * 1000;
        if (elapsed >= limit) {
            ScheduleInterrupt(job);
            JobFatalError(job, BG_ERROR_CONTEXT_GENERAL_QUEUE_MANAGER, BG_E_MAXDOWNLOAD_TIMEOUT, SOURCE_HRESULT, -1);
        } else
            consider(next, now + (limit - elapsed) * TICKS_PER_MS);
    }
}

/*
 * CJobManager::RunJobs and ChooseBackgroundJob: every runnable FOREGROUND job runs, and one background job, the one of
 * the lowest priority value, first in list order on ties. Its quantum spent, it goes to the tail of the list.
 */
static void run_jobs(UINT64 now, UINT64 *next)
{
    if (g_background && (!JobRunnable(g_background) || g_background->priority == BG_JOB_PRIORITY_FOREGROUND))
        g_background = nullptr;
    if (g_background && g_background->quantum_at && now >= g_background->quantum_at) {
        BJOB *expired = g_background;
        g_background = nullptr;
        expired->quantum_at = 0;
        JobLink(expired, TRUE);
    }
    if (!g_background) {
        BJOB *best = nullptr;
        for (BJOB *job = g_online.next; job != &g_online; job = job->next)
            if (JobRunnable(job) && !job->holding && job->priority != BG_JOB_PRIORITY_FOREGROUND &&
                (!best || job->priority < best->priority))
                best = job;
        g_background = best;
        if (best)
            best->quantum_at = now + (UINT64)g_config.quantum * 10000000ull;
    }
    if (g_background)
        consider(next, g_background->quantum_at);
    for (BJOB *job = g_online.next, *following; job != &g_online; job = following) {
        following = job->next;
        BOOL wanted = JobRunnable(job) && !job->holding &&
                      (job->priority == BG_JOB_PRIORITY_FOREGROUND || job == g_background);
        if (wanted && !job->run && job->state == BG_JOB_STATE_QUEUED)
            start(job);
        else if (!wanted && job->run && !job->run->stop &&
                 (job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING)) {
            ScheduleInterrupt(job);
            JobSetState(job, BG_JOB_STATE_QUEUED);
        }
    }
}

/* CJobManager::ScheduleAnotherGroup, with the lock held: a job that may run now starts before the call returns. */
void ScheduleNow(void)
{
    if (g_stopping || !g_thread)
        return;
    UINT64 next = (UINT64)-1;
    run_jobs(QmNow(), &next);
    QmWake();
}

static void watch_policy(void)
{
    HKEY key = ConfigPolicyKey();
    if (key && g_policy_event)
        RegNotifyChangeKeyValue(key, TRUE, REG_NOTIFY_NAME_ | REG_NOTIFY_LAST_SET_, g_policy_event, TRUE);
}

static DWORD WINAPI scheduler(void *argument)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    watch_policy();
    DWORD wait = 0;
    for (;;) {
        HANDLE events[2] = {g_wake, g_policy_event};
        DWORD signalled = WaitForMultipleObjects(g_policy_event ? 2 : 1, events, FALSE, wait);
        if (g_stopping)
            break;
        if (signalled == WAIT_OBJECT_0 + 1) {
            /* CJobManager::OnGroupPolicyChange. */
            ConfigLoadPolicy();
            watch_policy();
        }
        UINT64 now = QmNow(), next = (UINT64)-1;
        Lock();
        for (int pass = 0; pass < 2; pass++) {
            BJOB *head = pass ? &g_offline : &g_online;
            for (BJOB *job = head->next, *following; job != head; job = following) {
                following = job->next;
                JobAddRef(job);
                if (!inactive(job, now, &next)) {
                    timers(job, now, &next);
                    if (job->callback_at)
                        consider(&next, job->callback_at);
                    if (job->callback_at && now >= job->callback_at)
                        NotifySchedule(job, now);
                }
                JobRelease(job);
            }
        }
        run_jobs(now, &next);
        Unlock();
        wait = MAX_WAIT_MS;
        if (next != (UINT64)-1) {
            UINT64 now2 = QmNow();
            UINT64 delta = next > now2 ? (next - now2) / TICKS_PER_MS + 1 : 0;
            wait = delta < MAX_WAIT_MS ? (DWORD)delta : MAX_WAIT_MS;
        }
    }
    CoUninitialize();
    return 0;
}

void ScheduleStart(void)
{
    g_stopping = FALSE;
    g_wake = CreateEventW(nullptr, FALSE, TRUE, nullptr);
    g_policy_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (FAILED(QmStartThread(scheduler, nullptr, &g_thread)))
        g_thread = nullptr;
}

void ScheduleStop(void)
{
    g_stopping = TRUE;
    QmWake();
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
    }
    g_thread = nullptr;
    g_background = nullptr;
    if (g_policy_event)
        CloseHandle(g_policy_event);
    g_policy_event = nullptr;
}

/*
 * CJob::ReevaluateIdleStopConditions: a job holds the service while it is QUEUED, CONNECTING, TRANSFERRING or
 * TRANSIENT_ERROR, or while a notification of it waits.
 */
BOOL ScheduleIdle(void)
{
    BOOL idle = TRUE;
    LockShared();
    for (BJOB *job = g_online.next; job != &g_online && idle; job = job->next)
        if (job->run || job->state <= BG_JOB_STATE_TRANSFERRING || job->state == BG_JOB_STATE_TRANSIENT_ERROR ||
            NotifyPending(job))
            idle = FALSE;
    UnlockShared();
    return idle;
}
