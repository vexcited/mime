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
static volatile LONG g_irq_bits;
static UCHAR g_recv_buffer[MIME_CTRL_MAX_MESSAGE] __attribute__((aligned(16)));
static UCHAR g_send_buffer[MIME_CTRL_MAX_MESSAGE] __attribute__((aligned(16)));
static BOOLEAN g_ready;

/* the buffers live in the kernel image, which is physically contiguous. */
static void send_message(const void *message, ULONG size)
{
    if (size > sizeof(g_send_buffer))
        return;
    ULONG64 state = KiDisableInterrupts();
    memcpy(g_send_buffer, message, size);
    HalHypercall(MIME_HC_CTRL_SEND, HalPhysicalOf(g_send_buffer), size, 0, 0, 0, 0, NULL);
    KiRestoreInterrupts(state);
}

static void send_process_event(ULONG type, ULONG64 request_id, ULONG status, ULONG pid)
{
    MimeEvtProcess evt = {0};
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
}

/* interrupt level: defer the work to a dpc. */
void MimeSessionInterrupt(ULONG bits)
{
    __atomic_fetch_or(&g_irq_bits, (LONG)bits, __ATOMIC_ACQ_REL);
    if (g_ready)
        KeInsertQueueDpc(&g_irq_dpc, NULL, NULL);
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
    PEPROCESS process;
    NTSTATUS status = PsCreateUserProcess(&image, &cmdline, run->cwd_chars ? &cwd : NULL, run->request_id, &process);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("cannot start %wZ: %08x", &image, status);
        send_process_event(MIME_EVT_RUN_FAILED, run->request_id, (ULONG)status, 0);
        return;
    }
    send_process_event(MIME_EVT_PROCESS_STARTED, run->request_id, 0, (ULONG)(ULONG_PTR)process->UniqueProcessId);
    ObDereferenceObject(process);
}

static void shutdown_machine(void)
{
    KiLogPrintf("shutting down");
    PLIST_ENTRY e;
    for (e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (!p->SystemProcess && !p->Terminating)
            PspTerminateProcess(p, STATUS_SYSTEM_SHUTDOWN);
    }
    HalHypercall(MIME_HC_HALT, 0, 0, 0, 0, 0, 0, NULL);
    for (;;)
        __asm__ volatile("wfi");
}

/* runs on the phase 1 system thread and never returns. */
void MiSessionInitialize(void)
{
    KeInitializeEvent(&g_ctrl_event, SynchronizationEvent, FALSE);
    KeInitializeDpc(&g_irq_dpc, irq_dpc, NULL);
    g_ready = TRUE;

    MimeCtrlHeader ready = {MIME_EVT_READY, sizeof(ready)};
    HalSharedPage->guest_ready = 1;
    send_message(&ready, sizeof(ready));
    KiLogPrintf("session ready");

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
            case MIME_CTRL_SHUTDOWN:
                shutdown_machine();
            default:
                KiLogPrintf("unknown control message %u", header->type);
                break;
            }
        }
        KeWaitForSingleObject(&g_ctrl_event, Executive, KernelMode, FALSE, NULL);
    }
}
