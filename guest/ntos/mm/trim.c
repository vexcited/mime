/*
 * gives memory back when the machine is short of it. the host watches its own memory and raises MIME_IRQ_MEMORY
 * with MimeSharedPage.memory_pressure; this thread then drops what the guest only caches and hands every free page
 * to the host. it also keeps the two memory resource notification events of windows, \KernelObjects\LowMemoryCondition
 * and \KernelObjects\HighMemoryCondition: low is set while the available memory is at or below the low threshold or
 * the host is short, high while it is above the high one and the host is not.
 *
 * windows trims working sets and repurposes the standby list to make room. this memory manager never pages out, so
 * its working sets are the memory its processes hold and have no part to give: what it caches is the idle file windows
 * in fscache.c and the pages processors keep for their next faults and the free blocks in pfn.c.
 */
#include "../ntos.h"
#include <mime/ntrtlsec.h>

#define TRIM_POLL_MS 2000
#define SWEEP_QUIET_POLLS 15            /* 30 s without a free before the leftovers are handed back. */
#define SWEEP_MIN_PAGES ((16ull << 20) >> PAGE_SHIFT)

static KEVENT g_wake;
static PKEVENT g_low, g_high;
static ULONG g_seen_serial;
static ULONG g_level;
static SIZE_T g_sweep_seen;
static ULONG g_sweep_quiet;

/*
 * as the memory manager of windows derives them from the size of the memory: a unit of a 32nd of the pages up to 8 GiB and
 * a 128th of the pages above that, kept between 1152 pages and 16 GiB, the low threshold twice the unit and the high one
 * twice the low, each pulled back toward the middle where it would pass the total.
 */
static void thresholds(SIZE_T *low, SIZE_T *high)
{
    SIZE_T total = MmGetTotalPages();
    SIZE_T unit = total <= 0x200000 ? total >> 5 : ((total - 0x200000) >> 7) + 0x10000;
    if (unit < 1152)
        unit = 1152;
    if (unit > 0x400000)
        unit = 0x400000;
    SIZE_T l = 2 * unit, h;
    if (l > total)
        l = total / 2 + unit / 2;
    h = 2 * l;
    if (h > total)
        h = l + (total - l) / 2;
    *low = l;
    *high = h;
}

/* windows lets everyone read and wait on the two events, and the administrators and the system do all. */
static PKEVENT make_condition(PCWSTR name)
{
    static const UCHAR world[] = {1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    static const UCHAR system[] = {1, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0};
    static const UCHAR admins[] = {1, 2, 0, 0, 0, 0, 0, 5, 32, 0, 0, 0, 32, 2, 0, 0};
    UCHAR acl[128];
    SECURITY_DESCRIPTOR sd;
    RtlCreateAcl((PACL)acl, sizeof(acl), 2);
    RtlAddAccessAllowedAce((PACL)acl, 2, READ_CONTROL | SYNCHRONIZE | EVENT_QUERY_STATE, (PSID)world);
    RtlAddAccessAllowedAce((PACL)acl, 2, EVENT_ALL_ACCESS, (PSID)admins);
    RtlAddAccessAllowedAce((PACL)acl, 2, EVENT_ALL_ACCESS, (PSID)system);
    RtlCreateSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    RtlSetDaclSecurityDescriptor(&sd, TRUE, (PACL)acl, FALSE);
    UNICODE_STRING text;
    RtlInitUnicodeString(&text, name);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &text, OBJ_PERMANENT, nullptr, &sd);
    HANDLE handle;
    if (!NT_SUCCESS(ZwCreateEvent(&handle, EVENT_ALL_ACCESS, &oa, NotificationEvent, FALSE)))
        return nullptr;
    PKEVENT event = nullptr;
    if (!NT_SUCCESS(ObReferenceHandle(handle, ExEventType, KernelMode, (PVOID *)&event, nullptr)))
        return nullptr;
    return event;
}

static void update_conditions(void)
{
    if (!g_low || !g_high)
        return;
    SIZE_T low, high, free = MmGetFreePages();
    thresholds(&low, &high);
    BOOLEAN is_low = free <= low || g_level != MIME_MEMORY_NORMAL;
    BOOLEAN is_high = free > high && g_level == MIME_MEMORY_NORMAL;
    static int last_low = -1, last_high = -1;
    if (last_low != is_low || last_high != is_high) {
        KiLogPrintf("mm: memory conditions low %d, high %d, host pressure %u, %llu MiB free", is_low, is_high, g_level,
                    (ULONG64)free >> 8);
        last_low = is_low;
        last_high = is_high;
    }
    if (is_low)
        KeSetEvent(g_low, IO_NO_INCREMENT, FALSE);
    else
        KeResetEvent(g_low);
    if (is_high)
        KeSetEvent(g_high, IO_NO_INCREMENT, FALSE);
    else
        KeResetEvent(g_high);
}

static void trim(ULONG level)
{
    /* low keeps a few windows for the files that are opened again at once, critical keeps none. */
    ULONG windows = FsCacheTrim(level >= MIME_MEMORY_CRITICAL ? 0 : 16);
    ULONG64 released = MmReleaseFreeMemory();
    KiLogPrintf("mm: trim for host pressure %u, %u file windows closed, %llu MiB handed back (%llu MiB free)", level, windows,
                released >> 20, (ULONG64)MmGetFreePages() >> 8);
}

/*
 * pieces smaller than a huge page of the host pile up as programs free memory in a scatter, and the usual hand back
 * leaves them. once nothing has been freed for a while they go too, which splits the huge pages of the host that are
 * partly in use. a busy machine never gets here, its next allocations would only fault them back.
 */
static void sweep_when_quiet(void)
{
    SIZE_T freed = MmFreedSinceSweep();
    if (freed != g_sweep_seen)
        g_sweep_quiet = 0;
    else if (freed >= SWEEP_MIN_PAGES && ++g_sweep_quiet >= SWEEP_QUIET_POLLS) {
        ULONG64 released = MmReleaseFreeMemory();
        KiLogPrintf("mm: quiet, %llu MiB of scattered free memory handed back", released >> 20);
        g_sweep_quiet = 0;
    }
    g_sweep_seen = MmFreedSinceSweep();
}

static void worker(PVOID context)
{
    g_low = make_condition(L"\\KernelObjects\\LowMemoryCondition");
    g_high = make_condition(L"\\KernelObjects\\HighMemoryCondition");
    if (!g_low || !g_high)
        KiLogPrintf("mm: cannot make the memory condition events");
    for (;;) {
        LARGE_INTEGER timeout;
        timeout.QuadPart = -(LONGLONG)TRIM_POLL_MS * 10000;
        KeWaitForSingleObject(&g_wake, Executive, KernelMode, FALSE, &timeout);
        ULONG level = __atomic_load_n(&HalSharedPage->memory_pressure, __ATOMIC_ACQUIRE);
        ULONG serial = __atomic_load_n(&HalSharedPage->memory_serial, __ATOMIC_ACQUIRE);
        g_level = level;
        FsCacheAge(KeQueryInterruptTime());
        sweep_when_quiet();
        update_conditions();
        if (serial != g_seen_serial) {
            g_seen_serial = serial;
            if (level != MIME_MEMORY_NORMAL) {
                trim(level);
                update_conditions();
            }
        }
    }
}

void MmTrimInitialize(void)
{
    KeInitializeEvent(&g_wake, SynchronizationEvent, FALSE);
    PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, worker, nullptr);
}

/* called from the session interrupt on MIME_IRQ_MEMORY. */
void MmTrimInterrupt(void)
{
    KeSetEvent(&g_wake, IO_NO_INCREMENT, FALSE);
}
