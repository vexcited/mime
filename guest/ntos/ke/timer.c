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

/*
 * the performance counter is the hal's counter from boot. a tsc counts in
 * gigahertz where windows programs expect the 10 MHz of windows' counter,
 * and some multiply it by a billion: it is slowed down by a power of two
 * to between 10 and 20 MHz. user mode reads it itself, from the bias and
 * shift in KUSER_SHARED_DATA.
 */
static UCHAR qpc_shift(void)
{
    ULONG64 f = HalBootInfo->timer_frequency;
    UCHAR shift = 0;
    if (f > 100000000)
        while ((f >> shift) > 20000000)
            shift++;
    return shift;
}

ULONG64 KiQpcFrequency(void)
{
    return HalBootInfo->timer_frequency >> qpc_shift();
}

static ULONG64 qpc_of(ULONG64 counter)
{
    return (counter - g_boot_counter) >> qpc_shift();
}

/* 100ns per count in 64.64 fixed point: 10^7 * 2^64 / frequency, by long division, or 0 below 10 MHz. */
static ULONG64 qpc_increment(void)
{
    static ULONG64 cached;
    ULONG64 f = KiQpcFrequency();
    if (cached || f <= 10000000)
        return cached;
    ULONG64 remainder = 10000000, quotient = 0;
    for (int bit = 63; bit >= 0; bit--) {
        /* remainder < f, so doubling it never overflows while f stays below 2^63. */
        remainder <<= 1;
        if (remainder >= f) {
            remainder -= f;
            quotient |= 1ull << bit;
        }
    }
    cached = quotient;
    return cached;
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
        frequency->QuadPart = (LONGLONG)KiQpcFrequency();
    r.QuadPart = (LONGLONG)qpc_of(HalReadCounter());
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
#ifdef __x86_64__
    /*
     * the timer fires once. writing it exits to the host, and a thread that waits and wakes in a few
     * microseconds would do it twice each time, so a timer that is armed already for the same time or
     * an earlier one is left to fire: its interrupt finds nothing to do, or this deadline not come, and
     * arms the next. there is no disarming either, the interrupt does that.
     */
    if (deadline == ~0ull) {
        if (KiOptions & KI_OPT_EAGER_TIMER) {
            HalDisarmTimer();
            prcb->TimerArmed = 0;
        }
        return;
    }
    {
        ULONG64 now = KiQueryInterruptTime();
        if (deadline > now + 2000)
            deadline = now + (deadline - now) / 8 * 7;
    }
    ULONG64 target = g_boot_counter + HalHnsToCounter(deadline);
    if (prcb->TimerArmed && prcb->TimerArmed <= target && !(KiOptions & KI_OPT_EAGER_TIMER))
        return;
    prcb->TimerArmed = target;
    HalArmTimer(target);
#else
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
#endif
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
    BOOLEAN was_set = timer->TimerListEntry.Flink != nullptr;
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
    BOOLEAN was_set = timer->TimerListEntry.Flink != nullptr;
    if (was_set) {
        RemoveEntryList(&timer->TimerListEntry);
        timer->TimerListEntry.Flink = nullptr;
    }
    KiReleaseDispatcherLock(s);
    return was_set;
}

BOOLEAN NTAPI KeReadStateTimer(PKTIMER timer)
{
    return timer->Header.SignalState != 0;
}

/*
 * the times at this tick, and what user mode needs to add the time since:
 * the performance counter then and 100ns per count in 64.64 fixed point,
 * under TimeUpdateLock, odd while it changes.
 */
static void update_shared_time(ULONG64 counter)
{
    if (!SharedUserData)
        return;
    ULONG64 now = HalCounterToHns(counter - g_boot_counter);
    ULONG64 system = KeBootFiletime + now;
    ULONG64 ticks = now / KI_CLOCK_TICK_HNS;
    ULONG64 frequency = KiQpcFrequency();
    ULONG64 increment = qpc_increment();
    __atomic_add_fetch(&SharedUserData->TimeUpdateLock, 1, __ATOMIC_RELEASE);
    SharedUserData->QpcBias = -g_boot_counter;
    SharedUserData->QpcShift = qpc_shift();
    SharedUserData->QpcFrequency = (LONGLONG)frequency;
    SharedUserData->BaselineSystemTimeQpc = SharedUserData->BaselineInterruptTimeQpc = qpc_of(counter);
    SharedUserData->QpcSystemTimeIncrement = SharedUserData->QpcInterruptTimeIncrement = increment;
    SharedUserData->QpcSystemTimeIncrementShift = SharedUserData->QpcInterruptTimeIncrementShift = 0;
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
    __atomic_add_fetch(&SharedUserData->TimeUpdateLock, 1, __ATOMIC_RELEASE);
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

/*
 * time spent in an interrupt handler or in dpcs is the processor's kernel
 * time and its interrupt or dpc time, as windows reports them, and not the
 * interrupted thread's: its next clock tick skips it.
 */
void KiChargeServiceTime(PKPRCB prcb, ULONG64 start_counter, BOOLEAN dpc)
{
    ULONG64 elapsed = HalCounterToHns(HalReadCounter() - start_counter);
    if (dpc)
        prcb->DpcTime += elapsed;
    else
        prcb->InterruptTime += elapsed;
    prcb->KernelTime += elapsed;
    if (prcb->CurrentThread != prcb->IdleThread)
        prcb->TickTime += elapsed;
}

/* interrupts masked, dispatcher database owned. */
void KiClockInterrupt(PKTRAP_FRAME frame)
{
    KeGetCurrentPrcb()->TimerArmed = 0;
    ULONG64 counter = HalReadCounter();
    ULONG64 now = HalCounterToHns(counter - g_boot_counter);
    PKPRCB prcb = KeGetCurrentPrcb();

    if (now >= g_next_tick) {
        while (g_next_tick <= now)
            g_next_tick += KI_CLOCK_TICK_HNS;
        update_shared_time(counter);
        check_stalled_dispatch(now);
    }

    PKTHREAD cur = prcb->CurrentThread;
    if (cur != prcb->IdleThread && now >= prcb->TickTime + KI_CLOCK_TICK_HNS) {
        ULONG64 elapsed = now - prcb->TickTime;
        prcb->TickTime = now;
        if (frame && KI_TRAP_FROM_USER(frame)) {
            cur->UserTime += elapsed;
            prcb->UserTime += elapsed;
        } else {
            cur->KernelTime += elapsed;
            prcb->KernelTime += elapsed;
        }
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
        t->TimerListEntry.Flink = nullptr;
        t->Header.SignalState = 1;
        KiSignalWaiters(&t->Header, 0);
        if (t->Dpc)
            KeInsertQueueDpc(t->Dpc, (PVOID)(ULONG_PTR)now, nullptr);
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
    ULONG64 counter = HalReadCounter();
    g_next_tick = HalCounterToHns(counter - g_boot_counter) + KI_CLOCK_TICK_HNS;
    update_shared_time(counter);
    KiProgramNextTimer();
}
