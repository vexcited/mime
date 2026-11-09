/*
 * kernel initialization, logging and bug checks.
 */
#include "../ntos.h"

KPCR KiBootPcr;
static KPCR g_secondary_pcrs[MAXIMUM_PROCESSORS - 1];

void KiLogPrintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = mime_vformat(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((SIZE_T)n > sizeof(buf) - 2)
        n = (int)sizeof(buf) - 2;
    if (n == 0 || buf[n - 1] != '\n')
        buf[n++] = '\n';
    HalLog(buf, (SIZE_T)n);
}

ULONG DbgPrint(PCSTR format, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, format);
    int n = mime_vformat(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;
    if (n > 0)
        HalLog(buf, (SIZE_T)n);
    return STATUS_SUCCESS;
}

ULONG vDbgPrintEx(ULONG component, ULONG level, PCSTR format, va_list ap)
{
    char buf[512];
    int n = mime_vformat(buf, sizeof(buf), format, ap);
    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;
    if (n > 0)
        HalLog(buf, (SIZE_T)n);
    return STATUS_SUCCESS;
}

ULONG DbgPrintEx(ULONG component, ULONG level, PCSTR format, ...)
{
    va_list ap;
    va_start(ap, format);
    vDbgPrintEx(component, level, format, ap);
    va_end(ap);
    return STATUS_SUCCESS;
}

void NTAPI KeBugCheckEx(ULONG code, ULONG_PTR p1, ULONG_PTR p2, ULONG_PTR p3, ULONG_PTR p4)
{
    char buf[256];
    KiDisableInterrupts();
    ULONG64 fp = (ULONG64)__builtin_frame_address(0);
    KiLogPrintf("bug check on processor %u, thread %p", KeGetCurrentPrcb()->Number, KiCurrentThread());
    for (int depth = 0; depth < 16 && fp && !(fp & 7) && MmIsAddressValid((PVOID)fp) &&
                        MmIsAddressValid((PVOID)(fp + 8));
         depth++) {
        KiLogPrintf("  frame %d: %p", depth, (PVOID)((ULONG64 *)fp)[1]);
        fp = ((ULONG64 *)fp)[0];
    }
    if (MmVerifyPool)
        MiSpecialPoolBugCheckReport();
    mime_format(buf, sizeof(buf), "*** STOP: 0x%08X (0x%llX, 0x%llX, 0x%llX, 0x%llX)", code, (ULONG64)p1,
                (ULONG64)p2, (ULONG64)p3, (ULONG64)p4);
    HalPanic(buf);
}

void NTAPI KeBugCheck(ULONG code)
{
    KeBugCheckEx(code, 0, 0, 0, 0);
}

void KiAssertFailed(const char *expr, const char *file, int line)
{
    char buf[256];
    KiDisableInterrupts();
    mime_format(buf, sizeof(buf), "assertion failed: %s (%s:%d)", expr, file, line);
    HalPanic(buf);
}

static void KiPhase1Initialization(PVOID context);

/* the phases of the boot with the time each began, logged in one line when the session is ready. */
static struct {
    const char *Name;
    ULONG64 Ticks;
} g_boot_stamps[16];
static ULONG g_boot_stamp_count;

void KiBootStamp(const char *what)
{
    if (g_boot_stamp_count < RTL_NUMBER_OF(g_boot_stamps)) {
        g_boot_stamps[g_boot_stamp_count].Name = what;
        g_boot_stamps[g_boot_stamp_count++].Ticks = HalReadCounter();
    }
}

static void KiBootReport(void)
{
    char line[400];
    int n = mime_format(line, sizeof(line), "boot:");
    for (ULONG i = 1; i < g_boot_stamp_count && n < (int)sizeof(line) - 40; i++)
        n += mime_format(line + n, sizeof(line) - n, "%s %s %llu us", i > 1 ? "," : "", g_boot_stamps[i].Name,
                         HalCounterToHns(g_boot_stamps[i].Ticks - g_boot_stamps[i - 1].Ticks) / 10);
    KiLogPrintf("%s", line);
}

/*
 * every processor ends here, holding the kernel lock. it lets go only
 * while sleeping in KiIdleWait, so an interrupt is always taken with the
 * lock held and a wakeup posted meanwhile ends the sleep.
 */
static DECLSPEC_NORETURN void idle_loop(void)
{
    PKPRCB prcb = KeGetCurrentPrcb();
    for (;;) {
        KiDisableInterrupts();
        KiDispatchPendingWork();
        if (!prcb->DispatchPending && !prcb->DpcPending) {
            prcb->IdleTicks++;
            if (prcb->Number == 0)
                MmDiscardIdle();
            KiIdleWait();
        }
        KiEnableInterrupts();
        PspReapDeadThreads();
    }
}

/* the host starts each secondary processor on its idle thread's stack. */
void KiStartProcessors(void)
{
    ULONG count = HalBootInfo->cpu_count;
    if (count > MAXIMUM_PROCESSORS)
        count = MAXIMUM_PROCESSORS;
    for (ULONG i = 1; i < count; i++) {
        PKPCR pcr = &g_secondary_pcrs[i - 1];
        memset(pcr, 0, sizeof(*pcr));
        pcr->Self = pcr;
        pcr->CurrentPrcb = &pcr->Prcb;
        pcr->Irql = PASSIVE_LEVEL;
        pcr->Prcb.Number = i;
        PVOID stack = PsCreateIdleThread(&pcr->Prcb);
        if (!stack)
            break;
        KiProcessorBlock[i] = &pcr->Prcb;
        if (HalHypercall(MIME_HC_CPU_START, i, (ULONG64)KiSecondaryStartup, (ULONG64)pcr, (ULONG64)stack, 0, 0,
                         nullptr) != MIME_HC_STATUS_OK) {
            KiProcessorBlock[i] = nullptr;
            break;
        }
        KeNumberProcessors = i + 1;
    }
    KiLogPrintf("%u processors", KeNumberProcessors);
}

/* a secondary processor, on its idle thread with interrupts masked. */
void KiInitializeProcessor(PKPCR pcr)
{
    KiAcquireKernelLock();
    KiInitializeProcessorArch();
    KiSwitchAddressSpace(&PsInitialSystemProcess->Pcb);
    HalInitializeProcessor();
    KiProgramNextTimer();
    idle_loop();
}

/*
 * entered from KiSystemStartup on the boot stack with interrupts masked.
 * the boot context ends up as the idle thread of processor 0.
 */
void KiInitializeKernel(MimeBootInfo *boot)
{
    PKPCR pcr = &KiBootPcr;
    pcr->Self = pcr;
    pcr->CurrentPrcb = &pcr->Prcb;
    KiInitializeProcessorArch();
    /* interrupts stay masked in daif until the idle loop, so init code may freely raise and lower. */
    pcr->Irql = PASSIVE_LEVEL;
    KiAcquireKernelLock();

    HalInitialize(boot);
    KiBootStamp("hal");
    KiKernelLockPhys = HalPhysicalOf((const void *)KiKernelLock);
    /* lock holds over 100 microseconds are recorded with what held the lock. */
    KiLongHoldTicks = HalHnsToCounter(1000);
    if (boot->magic != MIME_BOOT_MAGIC || boot->version != MIME_ABI_VERSION)
        HalPanic("boot information mismatch between the vmm and the kernel");
    KiLogPrintf("Windows NT kernel %u.%u build %u (%s), %llu MiB of memory", MIME_NT_MAJOR_VERSION,
                MIME_NT_MINOR_VERSION, MIME_NT_BUILD_NUMBER, MIME_ARCH_NAME, boot->ram_size >> 20);

    KiOptions = (strstr(boot->cmdline, "nofastfault") ? KI_OPT_NO_FAST_FAULT : 0) |
                (strstr(boot->cmdline, "nosectionfault") ? KI_OPT_NO_SECTION_FAULT : 0) |
                (strstr(boot->cmdline, "noidlespin") ? KI_OPT_NO_IDLE_SPIN : 0) |
                (strstr(boot->cmdline, "eagertimer") ? KI_OPT_EAGER_TIMER : 0) |
                (strstr(boot->cmdline, "noattrcache") ? KI_OPT_NO_ATTR_CACHE : 0) |
                (strstr(boot->cmdline, "precisetimers") ? KI_OPT_PRECISE_TIMERS : 0);

    KiNoLockFlush = strstr(boot->cmdline, "nolockflush") != nullptr;
    extern BOOLEAN KiTraceUserExceptions;
    KiTraceUserExceptions = strstr(boot->cmdline, "trace-exceptions") != nullptr;
    /* verify-pool gives every pool block guard pages, verify-pool-fill also fills blocks that are not asked zeroed. */
    MmVerifyPool = strstr(boot->cmdline, "verify-pool") != nullptr;
    MmVerifyPoolFill = strstr(boot->cmdline, "verify-pool-fill") != nullptr;
    /* the kernel options no-access-checks, which opens every object to every program, and trace-access. */
    SeAccessEnforced = strstr(boot->cmdline, "no-access-checks") == nullptr;
    SeTraceAccess = strstr(boot->cmdline, "trace-access") != nullptr;
    MiTraceRoutines = strstr(boot->cmdline, "trace-routines") != nullptr;
    extern BOOLEAN MiDiscoverRoutines;
    MiDiscoverRoutines = strstr(boot->cmdline, "discover-routines") != nullptr;
    extern BOOLEAN IoTraceFiles;
    IoTraceFiles = strstr(boot->cmdline, "trace-files") != nullptr;
    extern char KiTraceSyscallImage[16];
    const char *trace = strstr(boot->cmdline, "trace-syscalls=");
    extern char KiTraceStackService[40];
    const char *stack = strstr(boot->cmdline, "trace-stack=");
    if (stack) {
        stack += 12;
        for (ULONG i = 0; i < sizeof(KiTraceStackService) - 1 && stack[i] && stack[i] != ' '; i++)
            KiTraceStackService[i] = stack[i];
    }
    extern char KiTraceFilter[256];
    const char *filter = strstr(boot->cmdline, "trace-filter=");
    if (filter) {
        filter += 13;
        for (ULONG i = 0; i < sizeof(KiTraceFilter) - 1 && filter[i] && filter[i] != ' '; i++)
            KiTraceFilter[i] = filter[i];
    }
    extern ULONG KiTraceEvery;
    const char *every = strstr(boot->cmdline, "trace-every=");
    for (every = every ? every + 12 : ""; *every >= '0' && *every <= '9'; every++)
        KiTraceEvery = KiTraceEvery * 10 + (ULONG)(*every - '0');
    extern char KiTimelineImage[16];
    const char *timeline = strstr(boot->cmdline, "svc-timeline=");
    if (timeline) {
        timeline += 13;
        for (ULONG i = 0; i < sizeof(KiTimelineImage) - 1 && timeline[i] && timeline[i] != ' '; i++)
            KiTimelineImage[i] = timeline[i];
    }
    if (trace) {
        trace += 15;
        for (ULONG i = 0; i < sizeof(KiTraceSyscallImage) - 1 && trace[i] && trace[i] != ' '; i++)
            KiTraceSyscallImage[i] = trace[i];
    }

    MmInitialize(boot);
    KiBootStamp("mm");
    ExInitializePool();
    KiInitializeScheduler();
    ObInitialize();
    ExInitialize();
    PsInitialize();
    SepInitialize();
    KiInitializeSystemServices();
    KiInitializeTimers();
    if (KiTimelineImage[0]) {
        extern void KiEnableTimeline(void);
        KiEnableTimeline();
    }
    KiBootStamp("executive");

    PETHREAD phase1;
    NTSTATUS status = PspCreateThread(PsInitialSystemProcess, TRUE, KiPhase1Initialization, nullptr, &phase1);
    if (!NT_SUCCESS(status))
        KeBugCheckEx(0x00000032, status, 0, 0, 0); /* PHASE1_INITIALIZATION_FAILED. */
    KiStartThread(&phase1->Tcb);
    ObDereferenceObject(phase1);
    KiStartProcessors();

    KiEnableInterrupts();
    idle_loop();
}

static void KiPhase1Initialization(PVOID context)
{
    KiBootStamp("phase1");
    PspStartReaper();
    MiInitializeLoadedModules();
    IoInitialize();
    KiBootStamp("io");
    CmInitSystem1();
    KiBootStamp("registry");
    ExInitializeTimeZone();
    IopStartCameras();
    NTSTATUS status = PspInitializeSystemDll();
    KiBootStamp("system dll");
    if (!NT_SUCCESS(status))
        KeBugCheckEx(0x0000006B, status, 0, 0, 0); /* PROCESS1_INITIALIZATION_FAILED. */

    UNICODE_STRING win32k = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\win32k.sys");
    PKLDR_DATA_TABLE_ENTRY module;
    status = MmLoadSystemImage(&win32k, &module);
    if (NT_SUCCESS(status)) {
        PDRIVER_OBJECT driver;
        status = IoCreateDriver(L"\\Driver\\Win32k", (PDRIVER_INITIALIZE)module->EntryPoint, &driver);
        if (!NT_SUCCESS(status))
            KiLogPrintf("win32k initialization failed: %08x", status);
    } else {
        KiLogPrintf("cannot load win32k.sys: %08x", status);
    }

    KiBootStamp("win32k");
    KiBootReport();
    MiSessionInitialize();
}
