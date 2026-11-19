/*
 * timers and the clock interrupt.
 *
 * each processor's virtual timer is one-shot. a busy processor programs
 * it for its next quantum tick; the timer processor, the last one that
 * queued the earliest timer, also programs the first expiring timer and
 * the periodic 15.625 ms tick that keeps KUSER_SHARED_DATA fresh. short
 * sleeps are precise without a fast tick, and idle processors sleep.
 */
#include "../ntos.h"

static LIST_ENTRY g_timer_list; /* sorted by due interrupt time. */
static ULONG64 g_next_tick;
static ULONG64 g_boot_counter;
static ULONG g_timer_cpu;
ULONG64 KeBootFiletime;

ULONG64 KiQueryInterruptTime(void)
{
    return HalCounterToHns(HalReadCounter() - g_boot_counter);
}

/*
 * the performance counter is the hal's counter since boot. a tsc runs in
 * gigahertz while windows programs expect tens of megahertz, and some
 * multiply it by a billion, so it is divided by a power of two below 30 MHz.
 * a counter that already is, like the 24 MHz of the architected timer, comes
 * through whole. user mode reads it itself, with the bias and shift in
 * KUSER_SHARED_DATA.
 */
static UCHAR qpc_shift(void)
{
    ULONG64 f = HalBootInfo->timer_frequency;
    UCHAR shift = 0;
    if (f > 100000000)
        while ((f >> shift) > 30000000)
            shift++;
    return shift;
}

/*
 * under microsoft's hypervisor windows runs its performance counter at 10 MHz, one count per 100 ns, and scales the
 * tsc to it through the page the hypervisor shares: the high half of the tsc times a multiplier, plus a bias. the
 * kernel and user mode read it the same way, so they agree to the count. FALSE without that hypervisor, where the
 * counter is the timer shifted down.
 */
static MIME_HYPERVISOR_SHARED_PAGE *g_hypervisor_page;

static BOOLEAN hypervisor_qpc(void)
{
    return g_hypervisor_page != nullptr;
}

/* 10^7 * 2^64 / frequency by long division, for a frequency above 10 MHz. */
static ULONG64 hns_per_count(ULONG64 f)
{
    ULONG64 remainder = 10000000, quotient = 0;
    for (int bit = 63; bit >= 0; bit--) {
        /* remainder < f, so doubling it never overflows while f stays below 2^63. */
        remainder <<= 1;
        if (remainder >= f) {
            remainder -= f;
            quotient |= 1ull << bit;
        }
    }
    return quotient;
}

static void initialize_hypervisor_page(void)
{
#ifndef __aarch64__
    ULONG leaves[1][4];
    if (!KiQueryHypervisorLeaves(leaves, 1) || HalBootInfo->timer_frequency <= 10000000)
        return;
    ULONG64 pa = MmAllocatePage();
    if (!pa)
        return;
    MIME_HYPERVISOR_SHARED_PAGE *page = HalPhysmap(pa);
    page->QpcMultiplier = hns_per_count(HalBootInfo->timer_frequency);
    page->QpcBias = -MimeMulHigh(g_boot_counter, page->QpcMultiplier);
    page->TimeUpdateLock = 1;
    MiSetHypervisorSharedPage(pa);
    g_hypervisor_page = page;
#endif
}

ULONG64 KiQpcFrequency(void)
{
    return hypervisor_qpc() ? 10000000 : HalBootInfo->timer_frequency >> qpc_shift();
}

static ULONG64 qpc_of(ULONG64 counter)
{
    if (hypervisor_qpc())
        return g_hypervisor_page->QpcBias + MimeMulHigh(counter, g_hypervisor_page->QpcMultiplier);
    return (counter - g_boot_counter) >> qpc_shift();
}

/*
 * a counter of 10 MHz or less advances by one 100 ns unit or more per count, which 64.64 fixed point cannot hold:
 * windows halves the increment until it fits and has the reader shift the count left by as much. at 10 MHz, the
 * frequency under microsoft's hypervisor, this is 2^63 with a shift of 1, as the reference machine shows.
 */
static UCHAR qpc_increment_shift(void)
{
    ULONG64 f = KiQpcFrequency();
    UCHAR shift = 0;
    while (f && (f << shift) <= 10000000)
        shift++;
    return shift;
}

/* 100 ns per count in 64.64 fixed point, 10^7 * 2^64 / frequency by long division, lowered by the shift above. */
static ULONG64 qpc_increment(void)
{
    static ULONG64 cached, cached_frequency;
    ULONG64 f = KiQpcFrequency();
    if (f != cached_frequency) {
        cached = f ? hns_per_count(f << qpc_increment_shift()) : 0;
        cached_frequency = f;
    }
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

/* dispatcher lock held. */
void KiProgramNextTimer(void)
{
    KI_ASSERT_DISPATCHER_LOCK("the timer set");
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
     * the timer is one-shot and writing it exits to the host, which a thread that waits and wakes within a few
     * microseconds would pay twice each time. so a timer already armed for the same or an earlier time is left to
     * fire: its interrupt finds nothing due, or this deadline not yet come, and arms the next. there is no disarming
     * either, the interrupt does that.
     */
    if (deadline == ~0ull) {
        if (KiOptions & KI_OPT_EAGER_TIMER) {
            HalDisarmTimer();
            prcb->TimerArmed = 0;
            prcb->UnlockWork &= (UCHAR)~KI_UNLOCK_TIMER;
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
    /* the write waits until the dispatcher lock is let go, see KiDispatcherUnlockWork. */
    if (KiDispatcherLockHeld())
        prcb->UnlockWork |= KI_UNLOCK_TIMER;
    else
        HalArmTimer(target);
#else
    if (deadline == ~0ull) {
        HalDisarmTimer();
        return;
    }
    /*
     * macOS wakes an idle processor about an eighth of its wait late, so a
     * long wait aims at 7/8 and the early interrupt arms the rest.
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
    KI_ASSERT_DISPATCHER_LOCK("a timer queued");
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
 * the performance counter then and 100 ns per count in 64.64 fixed point,
 * under TimeUpdateLock, which is odd while it changes.
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
    SharedUserData->QpcBias = hypervisor_qpc() ? 0 : -g_boot_counter;
    SharedUserData->QpcShift = hypervisor_qpc() ? 0 : qpc_shift();
    SharedUserData->QpcBypassEnabled = hypervisor_qpc() ? MIME_QPC_BYPASS_ENABLED | MIME_QPC_BYPASS_USE_HV_PAGE |
                                                              MIME_QPC_BYPASS_USE_RDTSCP
                                                        : MIME_QPC_BYPASS_ENABLED;
    SharedUserData->QpcFrequency = (LONGLONG)frequency;
    SharedUserData->BaselineSystemTimeQpc = SharedUserData->BaselineInterruptTimeQpc = qpc_of(counter);
    SharedUserData->QpcSystemTimeIncrement = SharedUserData->QpcInterruptTimeIncrement = increment;
    SharedUserData->QpcSystemTimeIncrementShift = SharedUserData->QpcInterruptTimeIncrementShift = qpc_increment_shift();
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
            KiSendIpi(i);
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

/* interrupts masked. */
void KiClockInterrupt(PKTRAP_FRAME frame)
{
    ULONG64 s = KiAcquireDispatcherLock();
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
    KiReleaseDispatcherLock(s);
}

void KiInitializeTimers(void)
{
    InitializeListHead(&g_timer_list);
    g_boot_counter = HalBootInfo->boot_counter;
    KeBootFiletime = HalBootInfo->boot_filetime;
    initialize_hypervisor_page();
    /* the kernel option time-offset=<seconds> shifts the clock from the host's, for instance to sit at a daylight
       saving change. */
    const char *offset = strstr(HalBootInfo->cmdline, "time-offset=");
    if (offset) {
        offset += 12;
        BOOLEAN negative = *offset == '-';
        if (*offset == '-' || *offset == '+')
            offset++;
        ULONG64 seconds = 0;
        while (*offset >= '0' && *offset <= '9')
            seconds = seconds * 10 + (ULONG64)(*offset++ - '0');
        KeBootFiletime = negative ? KeBootFiletime - seconds * 10000000ull : KeBootFiletime + seconds * 10000000ull;
    }
    ULONG64 counter = HalReadCounter();
    g_next_tick = HalCounterToHns(counter - g_boot_counter) + KI_CLOCK_TICK_HNS;
    update_shared_time(counter);
    ULONG64 s = KiAcquireDispatcherLock();
    KiProgramNextTimer();
    KiReleaseDispatcherLock(s);
}
