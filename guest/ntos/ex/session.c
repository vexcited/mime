/*
 * mime session manager: the control channel with the host.
 *
 * the host sends run and shutdown requests; the kernel answers with
 * process lifecycle and debug output events. this plays the part smss and
 * csrss have on windows until those exist as real user mode processes.
 */
#include "../ntos.h"

KEVENT MimeInputReadyEvent;
KEVENT MimeGpuEvent; /* set when the gpu reaches a fence a thread waits for */

static KEVENT g_ctrl_event;
static KDPC g_irq_dpc;
static volatile LONG g_dump_pending;
static volatile LONG g_irq_bits;
static UCHAR g_recv_buffer[MIME_CTRL_MAX_MESSAGE] __attribute__((aligned(16)));
static UCHAR g_send_buffer[MIME_CTRL_MAX_MESSAGE] __attribute__((aligned(16)));
static BOOLEAN g_ready;
static FAST_MUTEX g_console_lock;

/* the buffers live in the kernel image, which is physically contiguous. */
static void send_message(const void *message, ULONG size)
{
    if (size > sizeof(g_send_buffer))
        return;
    ULONG64 state = KiDisableInterrupts();
    memcpy(g_send_buffer, message, size);
    HalHypercall(MIME_HC_CTRL_SEND, HalPhysicalOf(g_send_buffer), size, 0, 0, 0, 0, nullptr);
    KiRestoreInterrupts(state);
}

static void send_process_event(ULONG type, ULONG64 request_id, ULONG status, ULONG pid)
{
    MimeEvtProcess evt = {};
    evt.header.type = type;
    evt.header.size = sizeof(evt);
    evt.request_id = request_id;
    evt.status = status;
    evt.process_id = pid;
    send_message(&evt, sizeof(evt));
}

void MimeSendDebugOutput(PEPROCESS process, const char *text, SIZE_T length)
{
    UCHAR buffer[1024];
    MimeEvtDebugOutput *evt = (MimeEvtDebugOutput *)buffer;
    ULONG64 request = process ? process->MimeRequestId : 0;
    while (length) {
        SIZE_T n = length;
        if (n > sizeof(buffer) - sizeof(*evt))
            n = sizeof(buffer) - sizeof(*evt);
        evt->header.type = MIME_EVT_DEBUG_OUTPUT;
        evt->header.size = (uint32_t)(sizeof(*evt) + n);
        evt->request_id = request;
        evt->process_id = process ? (uint32_t)(ULONG_PTR)process->UniqueProcessId : 0;
        evt->length = (uint32_t)n;
        memcpy(evt + 1, text, n);
        send_message(evt, evt->header.size);
        text += n;
        length -= n;
    }
}

/* console output and notices for the terminal of a run, split to fit the channel. */
void MimeSendConsole(ULONG type, ULONG64 request, ULONG value, const void *data, ULONG length)
{
    static UCHAR buffer[MIME_CTRL_MAX_MESSAGE];
    ExAcquireFastMutex(&g_console_lock);
    MimeCtrlConsole *evt = (MimeCtrlConsole *)buffer;
    const UCHAR *p = data;
    do {
        ULONG n = length;
        if (n > sizeof(buffer) - sizeof(*evt))
            n = sizeof(buffer) - sizeof(*evt);
        evt->header.type = type;
        evt->header.size = (uint32_t)(sizeof(*evt) + n);
        evt->request_id = request;
        evt->value = value;
        evt->length = n;
        memcpy(evt + 1, p, n);
        send_message(evt, evt->header.size);
        p += n;
        length -= n;
    } while (length);
    ExReleaseFastMutex(&g_console_lock);
}

void MimeSessionProcessExited(PEPROCESS process)
{
    if (process->MimeRequestId)
        send_process_event(MIME_EVT_PROCESS_EXIT, process->MimeRequestId, (ULONG)process->ExitStatus,
                           (ULONG)(ULONG_PTR)process->UniqueProcessId);
}

static void irq_dpc(PKDPC dpc, PVOID context, PVOID a1, PVOID a2)
{
    ULONG bits = (ULONG)__atomic_exchange_n(&g_irq_bits, 0, __ATOMIC_ACQ_REL);
    if (bits & MIME_IRQ_CTRL)
        KeSetEvent(&g_ctrl_event, 1, FALSE);
    if (bits & MIME_IRQ_INPUT)
        KeSetEvent(&MimeInputReadyEvent, 1, FALSE);
    if (bits & MIME_IRQ_GPU)
        KeSetEvent(&MimeGpuEvent, 1, FALSE);
    if (bits & MIME_IRQ_NET)
        AfdInterrupt();
    /* the dump reads user stacks, which the session thread does at passive level. */
    if (bits & MIME_IRQ_DUMP) {
        __atomic_store_n(&g_dump_pending, 1, __ATOMIC_RELEASE);
        KeSetEvent(&g_ctrl_event, 1, FALSE);
    }
}

/* interrupt level: defer the work to a dpc. */
void MimeSessionInterrupt(ULONG bits)
{
    __atomic_fetch_or(&g_irq_bits, (LONG)bits, __ATOMIC_ACQ_REL);
    if (g_ready)
        KeInsertQueueDpc(&g_irq_dpc, nullptr, nullptr);
}

static void handle_run(const MimeCtrlRun *run, ULONG size)
{
    ULONG64 chars = (ULONG64)run->image_chars + run->cmdline_chars + run->cwd_chars;
    if (sizeof(*run) + chars * sizeof(WCHAR) > size || run->image_chars == 0) {
        send_process_event(MIME_EVT_RUN_FAILED, run->request_id, (ULONG)STATUS_INVALID_PARAMETER, 0);
        return;
    }
    const WCHAR *text = (const WCHAR *)(run + 1);
    UNICODE_STRING image = {(USHORT)(run->image_chars * sizeof(WCHAR)), (USHORT)(run->image_chars * sizeof(WCHAR)),
                            (PWSTR)text};
    UNICODE_STRING cmdline = {(USHORT)(run->cmdline_chars * sizeof(WCHAR)),
                              (USHORT)(run->cmdline_chars * sizeof(WCHAR)), (PWSTR)(text + run->image_chars)};
    UNICODE_STRING cwd = {(USHORT)(run->cwd_chars * sizeof(WCHAR)), (USHORT)(run->cwd_chars * sizeof(WCHAR)),
                          (PWSTR)(text + run->image_chars + run->cmdline_chars)};
    KiLogPrintf("run request %llu: %wZ", run->request_id, &image);
    /* the terminal of the run is a console, which a console program is attached to from the start. */
    MIME_CONSOLE *console = ConsoleCreate(run->request_id, run->flags, run->console_size);
    if (!console) {
        send_process_event(MIME_EVT_RUN_FAILED, run->request_id, (ULONG)STATUS_INSUFFICIENT_RESOURCES, 0);
        return;
    }
    PEPROCESS process;
    NTSTATUS status =
        PsCreateUserProcess(&image, &cmdline, run->cwd_chars ? &cwd : nullptr, run->request_id, console, &process);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("cannot start %wZ: %08x", &image, status);
        ConsoleDereference(console);
        send_process_event(MIME_EVT_RUN_FAILED, run->request_id, (ULONG)status, 0);
        return;
    }
    send_process_event(MIME_EVT_PROCESS_STARTED, run->request_id, process->ImageSection->Image.Subsystem,
                       (ULONG)(ULONG_PTR)process->UniqueProcessId);
    ObDereferenceObject(process);
}

/*
 * the host asks for a shutdown as a logon session does: the programs are told and given their time, the rest
 * ends, the files are written. a second request that forces it takes the time away from the first.
 */
static void handle_shutdown(const MimeCtrlShutdown *request, ULONG size)
{
    ULONG flags = 0, timeout = MIME_SHUTDOWN_DEFAULT_MS;
    if (size >= sizeof(*request)) {
        flags = request->flags;
        if (request->timeout_ms)
            timeout = request->timeout_ms;
    }
    /* a forced one does not wait for the programs, and cuts the one that does. */
    if (flags & MIME_SHUTDOWN_FORCE)
        PsShutdownSystem(ShutdownPowerOff);
    NTSTATUS status = MimeShutdownStart(EWX_SHUTDOWN | EWX_POWEROFF | EWX_FORCEIFHUNG, MX_SHUTDOWN_REASON_HOST, 0,
                                        MX_SHUTDOWN_RETRY_FORCED, timeout, FALSE, FALSE);
    if (!NT_SUCCESS(status))
        KiLogPrintf("shutdown request: %08x", status);
}

/* runs on the phase 1 system thread and never returns. */
void MiSessionInitialize(void)
{
    KeInitializeEvent(&g_ctrl_event, SynchronizationEvent, FALSE);
    PsShutdownInitialize();
    ExInitializeFastMutex(&g_console_lock);
    KeInitializeDpc(&g_irq_dpc, irq_dpc, nullptr);
    g_ready = TRUE;

    MimeCtrlHeader ready = {MIME_EVT_READY, sizeof(ready)};
    HalSharedPage->guest_ready = 1;
    send_message(&ready, sizeof(ready));
    KiLogPrintf("session ready");
    /* the machine answers the host first; the services start alongside the first requests. */
    PsStartServiceController();

    for (;;) {
        for (;;) {
            ULONG64 length = 0;
            ULONG64 state = KiDisableInterrupts();
            ULONG64 rc = HalHypercall(MIME_HC_CTRL_RECV, HalPhysicalOf(g_recv_buffer), sizeof(g_recv_buffer), 0, 0,
                                      0, 0, &length);
            KiRestoreInterrupts(state);
            if (rc != MIME_HC_STATUS_OK || length < sizeof(MimeCtrlHeader))
                break;
            static UCHAR message[MIME_CTRL_MAX_MESSAGE];
            memcpy(message, g_recv_buffer, length);
            MimeCtrlHeader *header = (MimeCtrlHeader *)message;
            switch (header->type) {
            case MIME_CTRL_RUN:
                if (length >= sizeof(MimeCtrlRun))
                    handle_run((MimeCtrlRun *)message, (ULONG)length);
                break;
            case MIME_CTRL_CONSOLE_INPUT:
            case MIME_CTRL_CONSOLE_EVENT:
            case MIME_CTRL_CONSOLE_SIZE: {
                MimeCtrlConsole *c = (MimeCtrlConsole *)message;
                if (length < sizeof(*c) || sizeof(*c) + c->length > length)
                    break;
                if (header->type == MIME_CTRL_CONSOLE_INPUT)
                    ConsoleHostInput(c->request_id, (const UCHAR *)(c + 1), c->length);
                else if (header->type == MIME_CTRL_CONSOLE_EVENT)
                    ConsoleHostEvent(c->request_id, c->value);
                else
                    ConsoleHostSize(c->request_id, c->value);
                break;
            }
            case MIME_CTRL_SHUTDOWN:
                handle_shutdown((MimeCtrlShutdown *)message, (ULONG)length);
                break;
            default:
                KiLogPrintf("unknown control message %u", header->type);
                break;
            }
        }
        if (__atomic_exchange_n(&g_dump_pending, 0, __ATOMIC_ACQ_REL)) {
            PsDumpThreads();
            KiDumpServiceCalls();
        }
        KeWaitForSingleObject(&g_ctrl_event, Executive, KernelMode, FALSE, nullptr);
    }
}
