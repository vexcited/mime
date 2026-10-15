/*
 * timers and the clock interrupt.
 *
 * each processor's virtual timer is one-shot. a busy processor programs
 * it for its next quantum tick; the timer processor, the last one that
 * queued the earliest timer, also programs the first expiring timer and
 * the periodic tick (15.625 ms) keeping KUSER_SHARED_DATA fresh. short
 * sleeps are precise without a fast tick, and idle processors sleep.
 */
#include "../ntos.h"

static LIST_ENTRY g_timer_list; /* sorted by due interrupt time */
static ULONG64 g_next_tick;
static ULONG64 g_boot_counter;
static ULONG g_timer_cpu;
ULONG64 KeBootFiletime;

ULONG64 KiQueryInterruptTime(void)
{
    return HalCounterToHns(HalReadCounter() - g_boot_counter);
}

ULONGLONG NTAPI KeQueryInterruptTime(void)
{
    return KiQueryInterruptTime();
}

void NTAPI KeQuerySystemTime(PLARGE_INTEGER time)
{
    time->QuadPart = (LONGLONG)(KeBootFiletime + KiQueryInterruptTime());
}

void NTAPI KeQueryTickCount(PLARGE_INTEGER count)
{
    count->QuadPart = (LONGLONG)(KiQueryInterruptTime() / KI_CLOCK_TICK_HNS);
}

ULONG NTAPI KeQueryTimeIncrement(void)
{
    return KI_CLOCK_TICK_HNS;
}

LARGE_INTEGER NTAPI KeQueryPerformanceCounter(PLARGE_INTEGER frequency)
{
    LARGE_INTEGER r;
    if (frequency)
        frequency->QuadPart = (LONGLONG)HalBootInfo->timer_frequency;
    r.QuadPart = (LONGLONG)HalReadCounter();
    return r;
}

void KiProgramNextTimer(void)
{
    PKPRCB prcb = KeGetCurrentPrcb();
    ULONG64 deadline = ~0ull;
    if (prcb->CurrentThread != prcb->IdleThread)
        deadline = prcb->TickTime + KI_CLOCK_TICK_HNS;
    if (prcb->Number == g_timer_cpu) {
        if (g_next_tick < deadline)
            deadline = g_next_tick;
        if (!IsListEmpty(&g_timer_list)) {
            PKTIMER first = CONTAINING_RECORD(g_timer_list.Flink, KTIMER, TimerListEntry);
            if ((ULONG64)first->DueTime.QuadPart < deadline)
                deadline = first->DueTime.QuadPart;
        }
    }
    if (deadline == ~0ull) {
        HalDisarmTimer();
        return;
    }
    /*
     * macos wakes an idle processor late by about an eighth of its wait:
     * aiming at 7/8 of a long wait, the early interrupt arms the rest.
     */
    ULONG64 now = KiQueryInterruptTime();
    if (deadline > now + 2000)
        deadline = now + (deadline - now) / 8 * 7;
    HalArmTimer(g_boot_counter + HalHnsToCounter(deadline));
}

/* dispatcher lock held. */
void KiInsertTimer(PKTIMER timer, ULONG64 due)
{
    if (timer->TimerListEntry.Flink)
        RemoveEntryList(&timer->TimerListEntry);
    timer->DueTime.QuadPart = due;
    PLIST_ENTRY e = g_timer_list.Flink;
    while (e != &g_timer_list) {
        PKTIMER t = CONTAINING_RECORD(e, KTIMER, TimerListEntry);
        if (t->DueTime.QuadPart > due)
            break;
        e = e->Flink;
    }
    /* insert before e. */
    timer->TimerListEntry.Flink = e;
    timer->TimerListEntry.Blink = e->Blink;
    e->Blink->Flink = &timer->TimerListEntry;
    e->Blink = &timer->TimerListEntry;
    if (g_timer_list.Flink == &timer->TimerListEntry) {
        /* this processor takes the earliest timer; the previous owner wakes once more and lets go. */
        g_timer_cpu = KeGetCurrentPrcb()->Number;
        KiProgramNextTimer();
    }
}

void NTAPI KeInitializeTimerEx(PKTIMER timer, TIMER_TYPE type)
{
    memset(timer, 0, sizeof(*timer));
    timer->Header.Type = type == NotificationTimer ? TimerNotificationObject : TimerSynchronizationObject;
    timer->Header.Size = sizeof(KTIMER) / 8;
    InitializeListHead(&timer->Header.WaitListHead);
}

void NTAPI KeInitializeTimer(PKTIMER timer)
{
    KeInitializeTimerEx(timer, NotificationTimer);
}

BOOLEAN NTAPI KeSetTimerEx(PKTIMER timer, LARGE_INTEGER due, LONG period, PKDPC dpc)
{
    ULONG64 s = KiAcquireDispatcherLock();
    BOOLEAN was_set = timer->TimerListEntry.Flink != NULL;
    ULONG64 abs_due;
    if (due.QuadPart < 0)
        abs_due = KiQueryInterruptTime() + (ULONG64)(-due.QuadPart);
    else
        abs_due = (ULONG64)due.QuadPart > KeBootFiletime ? (ULONG64)due.QuadPart - KeBootFiletime : 0;
    timer->Header.SignalState = 0;
    timer->Dpc = dpc;
    timer->Period = period;
    KiInsertTimer(timer, abs_due);
    KiReleaseDispatcherLock(s);
    return was_set;
}

BOOLEAN NTAPI KeSetTimer(PKTIMER timer, LARGE_INTEGER due, PKDPC dpc)
{
    return KeSetTimerEx(timer, due, 0, dpc);
}

BOOLEAN NTAPI KeCancelTimer(PKTIMER timer)
{
    ULONG64 s = KiAcquireDispatcherLock();
    BOOLEAN was_set = timer->TimerListEntry.Flink != NULL;
    if (was_set) {
        RemoveEntryList(&timer->TimerListEntry);
        timer->TimerListEntry.Flink = NULL;
    }
    KiReleaseDispatcherLock(s);
    return was_set;
}

BOOLEAN NTAPI KeReadStateTimer(PKTIMER timer)
{
    return timer->Header.SignalState != 0;
}

static void update_shared_time(ULONG64 now)
{
    if (!SharedUserData)
        return;
    ULONG64 system = KeBootFiletime + now;
    ULONG64 ticks = now / KI_CLOCK_TICK_HNS;
    /* writers update High2 first and High1 last so readers detect torn reads. */
    SharedUserData->InterruptTime.High2Time = (LONG)(now >> 32);
    SharedUserData->InterruptTime.LowPart = (ULONG)now;
    SharedUserData->InterruptTime.High1Time = (LONG)(now >> 32);
    SharedUserData->SystemTime.High2Time = (LONG)(system >> 32);
    SharedUserData->SystemTime.LowPart = (ULONG)system;
    SharedUserData->SystemTime.High1Time = (LONG)(system >> 32);
    SharedUserData->TickCount.High2Time = (LONG)(ticks >> 32);
    SharedUserData->TickCount.LowPart = (ULONG)ticks;
    SharedUserData->TickCount.High1Time = (LONG)(ticks >> 32);
    SharedUserData->TickCountLowDeprecated = (ULONG)ticks;
}

/*
 * an idle processor asked to dispatch does so within microseconds, unless
 * the kernel lock is held long, as while a process is created. one still
 * waiting after eight ticks gets its ipi again and is logged.
 */
static void check_stalled_dispatch(ULONG64 now)
{
    for (ULONG i = 0; i < KeNumberProcessors; i++) {
        PKPRCB p = KiProcessorBlock[i];
        BOOLEAN pending = p->DispatchPending && p->CurrentThread == p->IdleThread && KiReadySummary;
        if (!pending) {
            p->PendingSince = 0;
        } else if (!p->PendingSince) {
            p->PendingSince = now;
        } else if (now - p->PendingSince > 8 * KI_CLOCK_TICK_HNS && p != KeGetCurrentPrcb()) {
            KiLogPrintf("processor %u still idle %llu ms after a dispatch request (ipi sent %llu, received %llu)", i,
                        (now - p->PendingSince) / 10000, p->IpiSent, p->IpiReceived);
            p->PendingSince = now;
            p->IpiSent++;
            HalSendIpi(i);
        }
    }
}

/* interrupts masked, dispatcher database owned. */
void KiClockInterrupt(PKTRAP_FRAME frame)
{
    ULONG64 now = KiQueryInterruptTime();
    PKPRCB prcb = KeGetCurrentPrcb();

    if (now >= g_next_tick) {
        while (g_next_tick <= now)
            g_next_tick += KI_CLOCK_TICK_HNS;
        update_shared_time(now);
        check_stalled_dispatch(now);
    }

    PKTHREAD cur = prcb->CurrentThread;
    if (cur != prcb->IdleThread && now >= prcb->TickTime + KI_CLOCK_TICK_HNS) {
        ULONG64 elapsed = now - prcb->TickTime;
        prcb->TickTime = now;
        if (frame && KI_TRAP_FROM_USER(frame))
            cur->UserTime += elapsed;
        else
            cur->KernelTime += elapsed;
        cur->Quantum -= (LONG)elapsed;
        if (cur->Quantum <= 0) {
            cur->Quantum = 0;
            /* only threads of the same or higher priority get the processor. */
            if (KiReadySummary >> cur->Priority)
                prcb->DispatchPending = TRUE;
        }
    }

    while (!IsListEmpty(&g_timer_list)) {
        PKTIMER t = CONTAINING_RECORD(g_timer_list.Flink, KTIMER, TimerListEntry);
        if ((ULONG64)t->DueTime.QuadPart > now)
            break;
        RemoveEntryList(&t->TimerListEntry);
        t->TimerListEntry.Flink = NULL;
        t->Header.SignalState = 1;
        KiSignalWaiters(&t->Header, 0);
        if (t->Dpc)
            KeInsertQueueDpc(t->Dpc, (PVOID)(ULONG_PTR)now, NULL);
        if (t->Period > 0)
            KiInsertTimer(t, now + (ULONG64)t->Period * 10000);
    }

    KiProgramNextTimer();
}

void KiInitializeTimers(void)
{
    InitializeListHead(&g_timer_list);
    g_boot_counter = HalBootInfo->boot_counter;
    KeBootFiletime = HalBootInfo->boot_filetime;
    ULONG64 now = KiQueryInterruptTime();
    g_next_tick = now + KI_CLOCK_TICK_HNS;
    update_shared_time(now);
    KiProgramNextTimer();
}
