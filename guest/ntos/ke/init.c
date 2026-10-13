/*
 * kernel initialization, logging and bug checks.
 */
#include "../ntos.h"

KPCR KiBootPcr;

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

/*
 * entered from KiSystemStartup on the boot stack with interrupts masked.
 * the boot context ends up as the idle thread of processor 0.
 */
void KiInitializeKernel(MimeBootInfo *boot)
{
    PKPCR pcr = &KiBootPcr;
    pcr->Self = pcr;
    pcr->CurrentPrcb = &pcr->Prcb;
    /* interrupts stay masked in daif until the idle loop, so init code may freely raise and lower. */
    pcr->Irql = PASSIVE_LEVEL;

    HalInitialize(boot);
    if (boot->magic != MIME_BOOT_MAGIC || boot->version != MIME_ABI_VERSION)
        HalPanic("boot information mismatch between the vmm and the kernel");
    KiLogPrintf("Mime NT kernel %u.%u build %u (arm64), %llu MiB of memory", MIME_NT_MAJOR_VERSION,
                MIME_NT_MINOR_VERSION, MIME_NT_BUILD_NUMBER, boot->ram_size >> 20);

    extern BOOLEAN KiTraceUserExceptions;
    KiTraceUserExceptions = strstr(boot->cmdline, "trace-exceptions") != NULL;

    MmInitialize(boot);
    ExInitializePool();
    KiInitializeScheduler();
    ObInitialize();
    ExInitialize();
    PsInitialize();
    KiInitializeSystemServices();
    KiInitializeTimers();

    PETHREAD phase1;
    NTSTATUS status = PspCreateThread(PsInitialSystemProcess, TRUE, KiPhase1Initialization, NULL, &phase1);
    if (!NT_SUCCESS(status))
        KeBugCheckEx(0x00000032, status, 0, 0, 0); /* PHASE1_INITIALIZATION_FAILED */
    KiStartThread(&phase1->Tcb);
    ObDereferenceObject(phase1);

    KiEnableInterrupts();

    /* idle loop. */
    for (;;) {
        KiDisableInterrupts();
        KiDispatchPendingWork();
        if (!KeGetCurrentPrcb()->DispatchPending && !KeGetCurrentPrcb()->DpcPending) {
            KeGetCurrentPrcb()->IdleTicks++;
            MmDiscardIdle();
            __asm__ volatile("wfi");
        }
        KiEnableInterrupts();
        PspReapDeadThreads();
    }
}

static void KiPhase1Initialization(PVOID context)
{
    IoInitialize();
    CmInitSystem1();
    NTSTATUS status = PspInitializeSystemDll();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(0x0000006B, status, 0, 0, 0); /* PROCESS1_INITIALIZATION_FAILED */

    UNICODE_STRING win32k = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\win32k.sys");
    PVOID base, entry;
    SIZE_T size;
    status = MmLoadSystemImage(&win32k, &base, &size, &entry);
    if (NT_SUCCESS(status)) {
        PDRIVER_OBJECT driver;
        status = IoCreateDriver(L"\\Driver\\Win32k", (PDRIVER_INITIALIZE)entry, &driver);
        if (!NT_SUCCESS(status))
            KiLogPrintf("win32k initialization failed: %08x", status);
    } else {
        KiLogPrintf("cannot load win32k.sys: %08x", status);
    }

    MiSessionInitialize();
}
