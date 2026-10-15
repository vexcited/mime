/*
 * the console driver, \Device\ConDrv. every host run brings a console, the
 * terminal of `mime run`, and a console program is attached to it when it
 * is created; a gui program is not, but may attach later. input the host
 * sends waits here until a process reads it, writes go straight to the
 * terminal, and a ctrl event starts the ctrl routine kernelbase registered
 * on a new thread of each attached process, as csrss does on windows.
 *
 * when the terminal goes away the processes get CTRL_CLOSE_EVENT and five
 * seconds to exit before they are terminated.
 *
 * a standard stream `mime run` was given a pipe or a file for is no
 * console: \Device\ConDrvStream\Input, Output and Error are its ends,
 * files of a named pipe device as a redirected handle is on windows, read
 * and written in bytes and asked the questions of a pipe.
 */
#include "../ntos.h"
#include <mime/condrv.h>

#define CTRL_C_EVENT 0
#define CTRL_BREAK_EVENT 1
#define CTRL_CLOSE_EVENT 2
#define STATUS_CONTROL_C_EXIT ((NTSTATUS)0xC000013AL)
#define STATUS_PIPE_BROKEN ((NTSTATUS)0xC000014BL)
#define ATTACH_PARENT_PROCESS 0xFFFFFFFFu
#define CLOSE_TIMEOUT_MS 5000
#define MAX_TARGETS 64

#define DEFAULT_INPUT_MODE 0x1F7 /* processed, line, echo, insert, quick edit, extended flags, auto position */
#define DEFAULT_OUTPUT_MODE 0x3  /* processed, wrap at end of line */
#define ENABLE_LINE_INPUT 0x2

/* input as the host sent it; an empty chunk is an end of file. */
typedef struct CON_CHUNK {
    LIST_ENTRY Links;
    ULONG Length;
    ULONG Offset;
    ULONG HostBytes; /* acknowledged to the host once read */
    UCHAR Data[];
} CON_CHUNK;

struct _MIME_CONSOLE {
    LIST_ENTRY Links;
    LONG References; /* attached processes, the host run and waiting readers */
    ULONG64 RequestId;
    ULONG Flags; /* MIME_CONDRV_TTY_... and MIME_CONDRV_CLOSED */
    ULONG InputMode;
    ULONG OutputMode;
    USHORT Columns;
    USHORT Rows;
    LIST_ENTRY Input;
    ULONG InputBytes;
    KEVENT InputReady; /* set while input, an end of file or the closing waits */
};

static FAST_MUTEX g_lock;
static LIST_ENTRY g_consoles;
static PDEVICE_OBJECT g_device;
static PDEVICE_OBJECT g_stream_device;

#define STREAM_INPUT 1
#define STREAM_OUTPUT 2
#define STREAM_ERROR 3

#define FSCTL_PIPE_PEEK 0x0011400C
#define FILE_PIPE_CONNECTED_STATE 3
#define FILE_DEVICE_NAMED_PIPE 0x11

static void release(MIME_CONSOLE *c)
{
    ExAcquireFastMutex(&g_lock);
    BOOLEAN last = --c->References == 0;
    if (last)
        RemoveEntryList(&c->Links);
    ExReleaseFastMutex(&g_lock);
    if (!last)
        return;
    while (!IsListEmpty(&c->Input))
        ExFreePool(CONTAINING_RECORD(RemoveHeadList(&c->Input), CON_CHUNK, Links));
    ExFreePool(c);
}

/* the console of a process with a reference, or NULL. */
static MIME_CONSOLE *process_console(PEPROCESS p)
{
    ExAcquireFastMutex(&g_lock);
    MIME_CONSOLE *c = p->Console;
    if (c)
        c->References++;
    ExReleaseFastMutex(&g_lock);
    return c;
}

/* the console of a host run with a reference, or NULL. called with the lock held. */
static MIME_CONSOLE *find_run(ULONG64 request)
{
    for (PLIST_ENTRY e = g_consoles.Flink; e != &g_consoles; e = e->Flink) {
        MIME_CONSOLE *c = CONTAINING_RECORD(e, MIME_CONSOLE, Links);
        if (c->RequestId == request && !(c->Flags & MIME_CONDRV_CLOSED)) {
            c->References++;
            return c;
        }
    }
    return NULL;
}

/*
 * gives every attached process's input event the state of the console:
 * set while input, an end of file or the closing waits to be read. called
 * with the lock held; the events are set after it is released.
 */
static ULONG collect_events(MIME_CONSOLE *c, PKEVENT *events, ULONG max)
{
    ULONG n = 0;
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead && n < max; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->Console == c && p->ConsoleInputEvent) {
            ObReferenceObject(p->ConsoleInputEvent);
            events[n++] = p->ConsoleInputEvent;
        }
    }
    KiReleaseDispatcherLock(s);
    return n;
}

static BOOLEAN input_waiting(MIME_CONSOLE *c)
{
    return !IsListEmpty(&c->Input) || (c->Flags & MIME_CONDRV_CLOSED);
}

static void signal_events(PKEVENT *events, ULONG n, BOOLEAN set)
{
    for (ULONG i = 0; i < n; i++) {
        if (set)
            KeSetEvent(events[i], 0, FALSE);
        else
            KeClearEvent(events[i]);
        ObDereferenceObject(events[i]);
    }
}

static void send(MIME_CONSOLE *c, ULONG type, ULONG value, const void *data, ULONG length)
{
    MimeSendConsole(type, c->RequestId, value, data, length);
}

/* ------------------------------------------------------------ the host side */

MIME_CONSOLE *ConsoleCreate(ULONG64 request, ULONG run_flags, ULONG size)
{
    MIME_CONSOLE *c = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), KI_TAG('C', 'o', 'n', 's'));
    if (!c)
        return NULL;
    c->References = 1;
    c->RequestId = request;
    if (run_flags & MIME_RUN_TTY_INPUT)
        c->Flags |= MIME_CONDRV_TTY_INPUT;
    if (run_flags & MIME_RUN_TTY_OUTPUT)
        c->Flags |= MIME_CONDRV_TTY_OUTPUT;
    if (run_flags & MIME_RUN_TTY_ERROR)
        c->Flags |= MIME_CONDRV_TTY_ERROR;
    c->InputMode = DEFAULT_INPUT_MODE;
    c->OutputMode = DEFAULT_OUTPUT_MODE;
    c->Columns = size & 0xFFFF ? (USHORT)(size & 0xFFFF) : 80;
    c->Rows = size >> 16 ? (USHORT)(size >> 16) : 25;
    InitializeListHead(&c->Input);
    KeInitializeEvent(&c->InputReady, NotificationEvent, FALSE);
    ExAcquireFastMutex(&g_lock);
    InsertTailList(&g_consoles, &c->Links);
    ExReleaseFastMutex(&g_lock);
    return c;
}

void ConsoleDereference(MIME_CONSOLE *c)
{
    release(c);
}

NTSTATUS ConsoleAttach(PEPROCESS p, MIME_CONSOLE *c)
{
    ExAcquireFastMutex(&g_lock);
    NTSTATUS status = STATUS_ACCESS_DENIED;
    if (!p->Console) {
        p->Console = c;
        p->ConsoleGroup = (ULONG)(ULONG_PTR)p->UniqueProcessId;
        c->References++;
        status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&g_lock);
    return status;
}

void ConsoleDetach(PEPROCESS p)
{
    ExAcquireFastMutex(&g_lock);
    MIME_CONSOLE *c = p->Console;
    p->Console = NULL;
    ExReleaseFastMutex(&g_lock);
    if (c)
        release(c);
}

void ConsoleProcessExit(PEPROCESS p)
{
    ConsoleDetach(p);
    PKEVENT event = p->ConsoleInputEvent;
    p->ConsoleInputEvent = NULL;
    if (event)
        ObDereferenceObject(event);
}

/* queues input with the lock held; host_bytes is what to acknowledge to the host once it is read. */
static BOOLEAN queue_input(MIME_CONSOLE *c, const UCHAR *data, ULONG length, ULONG host_bytes, BOOLEAN crlf)
{
    ULONG size = length;
    for (ULONG i = 0; crlf && i < length; i++)
        size += data[i] == '\n' && (i == 0 || data[i - 1] != '\r');
    CON_CHUNK *k = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*k) + size, KI_TAG('C', 'o', 'n', 'i'));
    if (!k)
        return FALSE;
    ULONG n = 0;
    for (ULONG i = 0; i < length; i++) {
        if (crlf && data[i] == '\n' && (i == 0 || data[i - 1] != '\r'))
            k->Data[n++] = '\r';
        k->Data[n++] = data[i];
    }
    k->Length = n;
    k->Offset = 0;
    k->HostBytes = host_bytes;
    InsertTailList(&c->Input, &k->Links);
    c->InputBytes += n;
    KeSetEvent(&c->InputReady, 0, FALSE);
    return TRUE;
}

/* typed or piped input. a terminal ends lines with \n, a windows console with \r\n. */
void ConsoleHostInput(ULONG64 request, const UCHAR *data, ULONG length)
{
    ExAcquireFastMutex(&g_lock);
    MIME_CONSOLE *c = find_run(request);
    if (!c) {
        ExReleaseFastMutex(&g_lock);
        return;
    }
    BOOLEAN crlf = (c->Flags & MIME_CONDRV_TTY_INPUT) && (c->InputMode & ENABLE_LINE_INPUT);
    BOOLEAN queued = queue_input(c, data, length, length, crlf);
    PKEVENT events[MAX_TARGETS];
    ULONG count = queued ? collect_events(c, events, MAX_TARGETS) : 0;
    ExReleaseFastMutex(&g_lock);
    signal_events(events, count, TRUE);
    if (!queued)
        send(c, MIME_EVT_CONSOLE_READ, length, NULL, 0);
    release(c);
}

void ConsoleHostSize(ULONG64 request, ULONG size)
{
    ExAcquireFastMutex(&g_lock);
    MIME_CONSOLE *c = find_run(request);
    if (c && (size & 0xFFFF) && (size >> 16)) {
        c->Columns = (USHORT)(size & 0xFFFF);
        c->Rows = (USHORT)(size >> 16);
    }
    ExReleaseFastMutex(&g_lock);
    if (c)
        release(c);
}

/*
 * starts the ctrl routine of every process attached to the console, or of
 * one process group; a process that never registered one ends at once.
 */
static ULONG dispatch_event(MIME_CONSOLE *c, ULONG event, ULONG group)
{
    PEPROCESS targets[MAX_TARGETS];
    PVOID routines[MAX_TARGETS];
    ULONG n = 0;
    ExAcquireFastMutex(&g_lock);
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead && n < MAX_TARGETS; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->Console != c || p->Terminating || p->Exited || (group && p->ConsoleGroup != group))
            continue;
        ObReferenceObject(p);
        routines[n] = p->ConsoleCtrlRoutine;
        targets[n++] = p;
    }
    KiReleaseDispatcherLock(s);
    ExReleaseFastMutex(&g_lock);
    for (ULONG i = 0; i < n; i++) {
        PETHREAD thread;
        if (routines[i] && NT_SUCCESS(PspCreateUserThread(targets[i], routines[i], (PVOID)(ULONG_PTR)event, FALSE, 0,
                                                          0, &thread)))
            ObDereferenceObject(thread);
        else
            PspTerminateProcess(targets[i], STATUS_CONTROL_C_EXIT);
        ObDereferenceObject(targets[i]);
    }
    return n;
}

/* the processes a closed console still holds after their grace time end. */
static void NTAPI close_watchdog(PVOID context)
{
    MIME_CONSOLE *c = context;
    LARGE_INTEGER delay = {.QuadPart = -(LONGLONG)CLOSE_TIMEOUT_MS * 10000};
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    PEPROCESS targets[MAX_TARGETS];
    ULONG n = 0;
    ExAcquireFastMutex(&g_lock);
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead && n < MAX_TARGETS; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->Console == c && !p->Terminating && !p->Exited) {
            ObReferenceObject(p);
            targets[n++] = p;
        }
    }
    KiReleaseDispatcherLock(s);
    ExReleaseFastMutex(&g_lock);
    for (ULONG i = 0; i < n; i++) {
        PspTerminateProcess(targets[i], STATUS_CONTROL_C_EXIT);
        ObDereferenceObject(targets[i]);
    }
    release(c);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

void ConsoleHostEvent(ULONG64 request, ULONG event)
{
    ExAcquireFastMutex(&g_lock);
    MIME_CONSOLE *c = find_run(request);
    PKEVENT events[MAX_TARGETS];
    ULONG count = 0;
    if (c && event == CTRL_CLOSE_EVENT) {
        c->Flags |= MIME_CONDRV_CLOSED;
        KeSetEvent(&c->InputReady, 0, FALSE);
        count = collect_events(c, events, MAX_TARGETS);
    }
    ExReleaseFastMutex(&g_lock);
    signal_events(events, count, TRUE);
    if (!c)
        return;
    ULONG attached = event <= CTRL_CLOSE_EVENT ? dispatch_event(c, event, 0) : 0;
    if (event == CTRL_CLOSE_EVENT) {
        /* the run's reference goes with the terminal; a watchdog takes ours. */
        release(c);
        if (attached && NT_SUCCESS(PsCreateSystemThread(NULL, 0, NULL, NULL, NULL, close_watchdog, c)))
            return;
    }
    release(c);
}

/* ------------------------------------------------------------ the device */

static NTSTATUS complete(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS NTAPI con_success(PDEVICE_OBJECT device, PIRP irp)
{
    return complete(irp, STATUS_SUCCESS, 0);
}

/* copies queued input, consuming it unless peeking. stops before an end of file once data was copied. */
static ULONG take_input(MIME_CONSOLE *c, PUCHAR out, ULONG length, BOOLEAN peek, BOOLEAN *eof, ULONG *acked)
{
    ULONG done = 0;
    *eof = FALSE;
    *acked = 0;
    PLIST_ENTRY e = c->Input.Flink;
    while (e != &c->Input && done < length) {
        CON_CHUNK *k = CONTAINING_RECORD(e, CON_CHUNK, Links);
        e = e->Flink;
        if (k->Length == 0) {
            if (done)
                break;
            *eof = TRUE;
            if (!peek) {
                RemoveEntryList(&k->Links);
                *acked += k->HostBytes;
                ExFreePool(k);
            }
            break;
        }
        ULONG n = k->Length - k->Offset;
        if (n > length - done)
            n = length - done;
        memcpy(out + done, k->Data + k->Offset, n);
        done += n;
        if (peek)
            continue;
        k->Offset += n;
        c->InputBytes -= n;
        if (k->Offset == k->Length) {
            RemoveEntryList(&k->Links);
            *acked += k->HostBytes;
            ExFreePool(k);
        }
    }
    return done;
}

static NTSTATUS read_input(MIME_CONSOLE *c, PUCHAR out, ULONG length, ULONG_PTR *information)
{
    for (;;) {
        ExAcquireFastMutex(&g_lock);
        BOOLEAN eof;
        ULONG acked;
        ULONG n = take_input(c, out, length, FALSE, &eof, &acked);
        BOOLEAN closed = (c->Flags & MIME_CONDRV_CLOSED) != 0;
        if (!n && !eof && !closed)
            KeClearEvent(&c->InputReady);
        PKEVENT events[MAX_TARGETS];
        BOOLEAN waiting = input_waiting(c);
        ULONG count = waiting ? 0 : collect_events(c, events, MAX_TARGETS);
        ExReleaseFastMutex(&g_lock);
        signal_events(events, count, FALSE);
        if (acked)
            send(c, MIME_EVT_CONSOLE_READ, acked, NULL, 0);
        if (n || eof || closed || !length) {
            *information = n;
            return STATUS_SUCCESS;
        }
        NTSTATUS status = KeWaitForSingleObject(&c->InputReady, UserRequest, UserMode, FALSE, NULL);
        if (status != STATUS_SUCCESS)
            return status;
    }
}

static ULONG list_processes(MIME_CONSOLE *c, PULONG out, ULONG max)
{
    ULONG total = 0;
    ExAcquireFastMutex(&g_lock);
    ULONG64 s = KiAcquireDispatcherLock();
    /* newest first, as windows lists them. */
    for (PLIST_ENTRY e = PsActiveProcessHead.Blink; e != &PsActiveProcessHead; e = e->Blink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->Console != c || p->Exited)
            continue;
        if (total < max)
            out[total] = (ULONG)(ULONG_PTR)p->UniqueProcessId;
        total++;
    }
    KiReleaseDispatcherLock(s);
    ExReleaseFastMutex(&g_lock);
    return total;
}

static NTSTATUS attach(PEPROCESS self, ULONG pid)
{
    MIME_CONSOLE *c = NULL;
    if (pid == ATTACH_PARENT_PROCESS) {
        /* the parent of a program `mime run` started is the terminal itself. */
        ExAcquireFastMutex(&g_lock);
        if (self->MimeRequestId)
            c = find_run(self->MimeRequestId);
        ExReleaseFastMutex(&g_lock);
    } else {
        PEPROCESS other;
        if (NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &other))) {
            c = process_console(other);
            ObDereferenceObject(other);
        } else {
            return STATUS_INVALID_PARAMETER;
        }
    }
    if (!c)
        return STATUS_INVALID_HANDLE;
    NTSTATUS status = ConsoleAttach(self, c);
    release(c);
    return status;
}

static NTSTATUS NTAPI con_control(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PUCHAR buf = irp->AssociatedIrp.SystemBuffer;
    PEPROCESS self = PsCurrentProcess();
    ULONG_PTR information = 0;
    NTSTATUS status = STATUS_SUCCESS;

    /* requests that work without a console. */
    switch (code) {
    case IOCTL_CONDRV_REGISTER: {
        if (in_len < sizeof(MIME_CONDRV_REGISTER))
            return complete(irp, STATUS_INVALID_PARAMETER, 0);
        MIME_CONDRV_REGISTER *r = (MIME_CONDRV_REGISTER *)buf;
        PKEVENT event = NULL;
        if (r->InputEvent) {
            status = ObReferenceHandle((HANDLE)(ULONG_PTR)r->InputEvent, ExEventType, UserMode, (PVOID *)&event, NULL);
            if (!NT_SUCCESS(status))
                return complete(irp, status, 0);
        }
        self->ConsoleCtrlRoutine = (PVOID)(ULONG_PTR)r->CtrlRoutine;
        PKEVENT old = self->ConsoleInputEvent;
        self->ConsoleInputEvent = event;
        if (old)
            ObDereferenceObject(old);
        return complete(irp, STATUS_SUCCESS, 0);
    }
    case IOCTL_CONDRV_ATTACH:
        if (in_len < sizeof(ULONG))
            return complete(irp, STATUS_INVALID_PARAMETER, 0);
        return complete(irp, attach(self, *(PULONG)buf), 0);
    case IOCTL_CONDRV_FREE:
        ConsoleDetach(self);
        return complete(irp, STATUS_SUCCESS, 0);
    }

    MIME_CONSOLE *c = process_console(self);
    if (code == IOCTL_CONDRV_STATE) {
        if (out_len < sizeof(MIME_CONDRV_STATE)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            MIME_CONDRV_STATE *st = (MIME_CONDRV_STATE *)buf;
            memset(st, 0, sizeof(*st));
            if (c) {
                ExAcquireFastMutex(&g_lock);
                st->Flags = c->Flags | MIME_CONDRV_ATTACHED;
                st->InputMode = c->InputMode;
                st->OutputMode = c->OutputMode;
                st->Columns = c->Columns;
                st->Rows = c->Rows;
                st->InputBytes = c->InputBytes;
                st->ProcessGroup = self->ConsoleGroup;
                ExReleaseFastMutex(&g_lock);
            }
            information = sizeof(*st);
        }
        if (c)
            release(c);
        return complete(irp, status, information);
    }
    if (!c)
        return complete(irp, STATUS_INVALID_HANDLE, 0);

    switch (code) {
    case IOCTL_CONDRV_WRITE:
        if (in_len < sizeof(ULONG)) {
            status = STATUS_INVALID_PARAMETER;
        } else if (c->Flags & MIME_CONDRV_CLOSED) {
            status = STATUS_PIPE_BROKEN;
        } else {
            ULONG stream = *(PULONG)buf == MIME_CONDRV_STDERR ? MIME_CONDRV_STDERR : MIME_CONDRV_STDOUT;
            send(c, MIME_EVT_CONSOLE_OUTPUT, stream, buf + sizeof(ULONG), in_len - sizeof(ULONG));
            information = in_len - sizeof(ULONG);
        }
        break;
    case IOCTL_CONDRV_READ:
        status = read_input(c, buf, out_len, &information);
        break;
    case IOCTL_CONDRV_WRITE_INPUT: {
        if (!in_len)
            break;
        ExAcquireFastMutex(&g_lock);
        BOOLEAN queued = queue_input(c, buf, in_len, 0, FALSE);
        PKEVENT events[MAX_TARGETS];
        ULONG count = queued ? collect_events(c, events, MAX_TARGETS) : 0;
        ExReleaseFastMutex(&g_lock);
        signal_events(events, count, TRUE);
        if (queued)
            information = in_len;
        else
            status = STATUS_INSUFFICIENT_RESOURCES;
        break;
    }
    case IOCTL_CONDRV_PEEK: {
        BOOLEAN eof;
        ULONG acked;
        ExAcquireFastMutex(&g_lock);
        information = take_input(c, buf, out_len, TRUE, &eof, &acked);
        ExReleaseFastMutex(&g_lock);
        break;
    }
    case IOCTL_CONDRV_SET_MODE:
        if (in_len < 2 * sizeof(ULONG)) {
            status = STATUS_INVALID_PARAMETER;
        } else {
            ExAcquireFastMutex(&g_lock);
            ULONG old = c->InputMode;
            c->InputMode = ((PULONG)buf)[0];
            c->OutputMode = ((PULONG)buf)[1];
            ULONG mode = c->InputMode;
            BOOLEAN tty = (c->Flags & MIME_CONDRV_TTY_INPUT) != 0;
            ExReleaseFastMutex(&g_lock);
            /* the terminal does the line editing, echo and ctrl+c the mode asks for. */
            if (tty && mode != old)
                send(c, MIME_EVT_CONSOLE_MODE, mode, NULL, 0);
        }
        break;
    case IOCTL_CONDRV_GENERATE:
        if (in_len < 2 * sizeof(ULONG) || ((PULONG)buf)[0] > CTRL_BREAK_EVENT)
            status = STATUS_INVALID_PARAMETER;
        else
            dispatch_event(c, ((PULONG)buf)[0], ((PULONG)buf)[1]);
        break;
    case IOCTL_CONDRV_PROCESSES: {
        if (out_len < sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        PULONG out = (PULONG)buf;
        ULONG max = out_len / sizeof(ULONG) - 1;
        out[0] = list_processes(c, out + 1, max);
        information = (1 + (out[0] < max ? out[0] : max)) * sizeof(ULONG);
        break;
    }
    case IOCTL_CONDRV_SYNC_EVENT: {
        ExAcquireFastMutex(&g_lock);
        BOOLEAN waiting = input_waiting(c);
        PKEVENT event = self->ConsoleInputEvent;
        if (event)
            ObReferenceObject(event);
        ExReleaseFastMutex(&g_lock);
        if (event)
            signal_events(&event, 1, waiting);
        break;
    }
    case IOCTL_CONDRV_FLUSH_INPUT: {
        ULONG acked = 0;
        ExAcquireFastMutex(&g_lock);
        while (!IsListEmpty(&c->Input)) {
            CON_CHUNK *k = CONTAINING_RECORD(RemoveHeadList(&c->Input), CON_CHUNK, Links);
            acked += k->HostBytes;
            ExFreePool(k);
        }
        c->InputBytes = 0;
        PKEVENT events[MAX_TARGETS];
        ULONG count = input_waiting(c) ? 0 : collect_events(c, events, MAX_TARGETS);
        ExReleaseFastMutex(&g_lock);
        signal_events(events, count, FALSE);
        if (acked)
            send(c, MIME_EVT_CONSOLE_READ, acked, NULL, 0);
        break;
    }
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    release(c);
    return complete(irp, status, information);
}

/* ------------------------------------------------------------ the redirected streams */

static ULONG stream_of(PIRP irp)
{
    return (ULONG)(ULONG_PTR)IoGetCurrentIrpStackLocation(irp)->FileObject->FsContext2;
}

static NTSTATUS NTAPI con_create(PDEVICE_OBJECT device, PIRP irp)
{
    if (device != g_stream_device)
        return complete(irp, STATUS_SUCCESS, 0);
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    static const struct {
        PCWSTR Name;
        ULONG Stream;
    } names[] = {{L"\\Input", STREAM_INPUT}, {L"\\Output", STREAM_OUTPUT}, {L"\\Error", STREAM_ERROR}};
    for (ULONG i = 0; i < RTL_NUMBER_OF(names); i++) {
        UNICODE_STRING n;
        RtlInitUnicodeString(&n, names[i].Name);
        if (RtlEqualUnicodeString(&file->FileName, &n, TRUE)) {
            file->FsContext2 = (PVOID)(ULONG_PTR)names[i].Stream;
            return complete(irp, STATUS_SUCCESS, 0);
        }
    }
    return complete(irp, STATUS_OBJECT_NAME_NOT_FOUND, 0);
}

/* waits for input without taking it, as a zero byte read of a pipe does. */
static NTSTATUS wait_input(MIME_CONSOLE *c)
{
    for (;;) {
        ExAcquireFastMutex(&g_lock);
        BOOLEAN ready = !IsListEmpty(&c->Input) || (c->Flags & MIME_CONDRV_CLOSED);
        if (!ready)
            KeClearEvent(&c->InputReady);
        ExReleaseFastMutex(&g_lock);
        if (ready)
            return STATUS_SUCCESS;
        NTSTATUS status = KeWaitForSingleObject(&c->InputReady, UserRequest, UserMode, FALSE, NULL);
        if (status != STATUS_SUCCESS)
            return status;
    }
}

static NTSTATUS NTAPI con_read_write(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG stream = stream_of(irp), length = stack->Parameters.Read.Length;
    BOOLEAN write = stack->MajorFunction == IRP_MJ_WRITE;
    if (device != g_stream_device || (write ? stream == STREAM_INPUT : stream != STREAM_INPUT))
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    MIME_CONSOLE *c = process_console(PsCurrentProcess());
    if (!c)
        return complete(irp, STATUS_PIPE_BROKEN, 0);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR information = 0;
    if (write) {
        if (c->Flags & MIME_CONDRV_CLOSED) {
            status = STATUS_PIPE_BROKEN;
        } else if (length) {
            send(c, MIME_EVT_CONSOLE_OUTPUT, stream == STREAM_ERROR ? MIME_CONDRV_STDERR : MIME_CONDRV_STDOUT,
                 irp->UserBuffer, length);
            information = length;
        }
    } else if (!length) {
        status = wait_input(c);
    } else {
        status = read_input(c, irp->UserBuffer, length, &information);
        /* the end of the input is the other end of the pipe closing. */
        if (NT_SUCCESS(status) && !information)
            status = STATUS_PIPE_BROKEN;
    }
    release(c);
    return complete(irp, status, information);
}

typedef struct STREAM_PIPE_LOCAL {
    ULONG NamedPipeType;
    ULONG NamedPipeConfiguration;
    ULONG MaximumInstances;
    ULONG CurrentInstances;
    ULONG InboundQuota;
    ULONG ReadDataAvailable;
    ULONG OutboundQuota;
    ULONG WriteQuotaAvailable;
    ULONG NamedPipeState;
    ULONG NamedPipeEnd;
} STREAM_PIPE_LOCAL;

static ULONG input_available(void)
{
    MIME_CONSOLE *c = process_console(PsCurrentProcess());
    if (!c)
        return 0;
    ExAcquireFastMutex(&g_lock);
    ULONG n = c->InputBytes;
    ExReleaseFastMutex(&g_lock);
    release(c);
    return n;
}

/* a byte mode pipe in blocking mode, the client end, always connected. */
static NTSTATUS NTAPI con_query(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG length = stack->Parameters.QueryFile.Length;
    PVOID buf = irp->AssociatedIrp.SystemBuffer;
    if (device != g_stream_device)
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    BOOLEAN input = stream_of(irp) == STREAM_INPUT;
    switch (stack->Parameters.QueryFile.FileInformationClass) {
    case FilePipeInformation:
        if (length < 2 * sizeof(ULONG))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        memset(buf, 0, 2 * sizeof(ULONG));
        return complete(irp, STATUS_SUCCESS, 2 * sizeof(ULONG));
    case FilePipeLocalInformation: {
        if (length < sizeof(STREAM_PIPE_LOCAL))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        STREAM_PIPE_LOCAL p = {0, input ? 0 : 1, 1, 1, 4096, input ? input_available() : 0, 4096, 4096,
                               FILE_PIPE_CONNECTED_STATE, 0};
        memcpy(buf, &p, sizeof(p));
        return complete(irp, STATUS_SUCCESS, sizeof(p));
    }
    case FileStandardInformation: {
        if (length < sizeof(FILE_STANDARD_INFORMATION))
            return complete(irp, STATUS_INFO_LENGTH_MISMATCH, 0);
        FILE_STANDARD_INFORMATION s = {0};
        s.AllocationSize.QuadPart = 4096;
        s.EndOfFile.QuadPart = input ? input_available() : 0;
        s.NumberOfLinks = 1;
        memcpy(buf, &s, sizeof(s));
        return complete(irp, STATUS_SUCCESS, sizeof(s));
    }
    default:
        return complete(irp, STATUS_INVALID_PARAMETER, 0);
    }
}

/* the modes of a pipe end: byte reads, blocking or not, both accepted as they change nothing here. */
static NTSTATUS NTAPI con_set(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    if (device != g_stream_device || stack->Parameters.SetFile.FileInformationClass != FilePipeInformation)
        return complete(irp, STATUS_INVALID_PARAMETER, 0);
    return complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI con_fs_control(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PUCHAR buf = irp->AssociatedIrp.SystemBuffer;
    if (device != g_stream_device || code != FSCTL_PIPE_PEEK)
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    /* the header: state, bytes available, messages and message length; then as much data as fits. */
    if (out_len < 4 * sizeof(ULONG))
        return complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
    ULONG *head = (ULONG *)buf;
    memset(head, 0, 4 * sizeof(ULONG));
    head[0] = FILE_PIPE_CONNECTED_STATE;
    ULONG copied = 0;
    MIME_CONSOLE *c = stream_of(irp) == STREAM_INPUT ? process_console(PsCurrentProcess()) : NULL;
    if (c) {
        BOOLEAN eof;
        ULONG acked;
        ExAcquireFastMutex(&g_lock);
        head[1] = c->InputBytes;
        copied = take_input(c, buf + 4 * sizeof(ULONG), out_len - 4 * sizeof(ULONG), TRUE, &eof, &acked);
        /* the end of the input is next when its empty chunk heads the queue, whatever room the peek has. */
        BOOLEAN at_end = !IsListEmpty(&c->Input) && !CONTAINING_RECORD(c->Input.Flink, CON_CHUNK, Links)->Length;
        BOOLEAN broken = at_end || (!c->InputBytes && (c->Flags & MIME_CONDRV_CLOSED));
        ExReleaseFastMutex(&g_lock);
        release(c);
        if (broken)
            return complete(irp, STATUS_PIPE_BROKEN, 0);
    }
    return complete(irp, copied < head[1] ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS, 4 * sizeof(ULONG) + copied);
}

static NTSTATUS NTAPI ConDrvEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = con_create;
    driver->MajorFunction[IRP_MJ_CLEANUP] = con_success;
    driver->MajorFunction[IRP_MJ_CLOSE] = con_success;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = con_control;
    driver->MajorFunction[IRP_MJ_READ] = con_read_write;
    driver->MajorFunction[IRP_MJ_WRITE] = con_read_write;
    driver->MajorFunction[IRP_MJ_QUERY_INFORMATION] = con_query;
    driver->MajorFunction[IRP_MJ_SET_INFORMATION] = con_set;
    driver->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = con_fs_control;
    UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_CONDRV_DEVICE);
    NTSTATUS status = IoCreateDevice(driver, 0, &name, 0x50 /* FILE_DEVICE_CONSOLE */, 0, FALSE, &g_device);
    UNICODE_STRING stream = RTL_CONSTANT_STRING(MIME_CONDRV_STREAM_DEVICE);
    if (NT_SUCCESS(status))
        status = IoCreateDevice(driver, 0, &stream, FILE_DEVICE_NAMED_PIPE, 0, FALSE, &g_stream_device);
    return status;
}

void IopInitializeConsole(void)
{
    ExInitializeFastMutex(&g_lock);
    InitializeListHead(&g_consoles);
    NTSTATUS status = IoCreateDriver(L"\\Driver\\ConDrv", ConDrvEntry, NULL);
    if (!NT_SUCCESS(status))
        KiLogPrintf("condrv failed to start: %08x", status);
}
