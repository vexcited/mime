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
#include "coninput.h"

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

/*
 * input waiting to be read: the bytes of a redirected input, or records,
 * which is what a terminal's input is decoded into and what programs
 * write. a chunk of no bytes is an end of file.
 */
typedef struct CON_CHUNK {
    LIST_ENTRY Links;
    BOOLEAN Records;
    ULONG Length;    /* bytes or records */
    ULONG First;     /* the first not read */
    ULONG Offset;    /* bytes of the first record's character already read */
    ULONG HostBytes; /* acknowledged to the host once read */
    union {
        UCHAR Data[1];
        MIME_CONDRV_RECORD Record[1];
    };
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
    ULONG InputBytes;  /* in byte chunks */
    ULONG InputEvents; /* records not read */
    KEVENT InputReady; /* set while input, an end of file or the closing waits */
    UCHAR Carry[16];   /* terminal input that does not make a whole key yet */
    ULONG CarryLength;
    ULONG PendingAck; /* host bytes that made no record yet */
    BOOLEAN HasAnsiCarry;
    MIME_CONDRV_RECORD AnsiCarry; /* the lead byte WriteConsoleInputA got without its trail byte */
    BOOLEAN CursorApplication;    /* the program asked for application cursor keys */
    BOOLEAN OutputPaused;         /* by the pause key */
    KEVENT OutputResumed;
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
    KeInitializeEvent(&c->OutputResumed, NotificationEvent, TRUE);
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

/* ------------------------------------------------------------ the input buffer */

static CON_CHUNK *new_chunk(BOOLEAN records, ULONG length)
{
    SIZE_T size = FIELD_OFFSET(CON_CHUNK, Data) + (records ? length * sizeof(MIME_CONDRV_RECORD) : length);
    CON_CHUNK *k = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('C', 'o', 'n', 'i'));
    if (k)
        k->Records = records;
    return k;
}

static void queue_chunk(MIME_CONSOLE *c, CON_CHUNK *k, BOOLEAN head)
{
    if (head)
        InsertHeadList(&c->Input, &k->Links);
    else
        InsertTailList(&c->Input, &k->Links);
    if (k->Records)
        c->InputEvents += k->Length - k->First;
    else
        c->InputBytes += k->Length - k->First;
    KeSetEvent(&c->InputReady, 0, FALSE);
}

static void free_chunk(MIME_CONSOLE *c, CON_CHUNK *k, ULONG *acked)
{
    RemoveEntryList(&k->Links);
    if (k->Records)
        c->InputEvents -= k->Length - k->First;
    else
        c->InputBytes -= k->Length - k->First;
    *acked += k->HostBytes;
    ExFreePool(k);
}

/* the last record waiting, which an event written alone may merge into. */
static MIME_CONDRV_RECORD *last_record(MIME_CONSOLE *c)
{
    if (IsListEmpty(&c->Input))
        return NULL;
    CON_CHUNK *k = CONTAINING_RECORD(c->Input.Blink, CON_CHUNK, Links);
    return k->Records && k->First < k->Length ? &k->Record[k->Length - 1] : NULL;
}

static void set_output_paused(MIME_CONSOLE *c, BOOLEAN paused)
{
    c->OutputPaused = paused;
    if (paused)
        KeClearEvent(&c->OutputResumed);
    else
        KeSetEvent(&c->OutputResumed, 0, FALSE);
}

/*
 * the pause key in line input pauses the output and any other key press
 * resumes it, as on windows; both are taken out. returns the records left.
 */
static ULONG take_pause_keys(MIME_CONSOLE *c, MIME_CONDRV_RECORD *r, ULONG count)
{
    ULONG kept = 0;
    for (ULONG i = 0; i < count; i++) {
        if (r[i].EventType == CON_KEY_EVENT && r[i].Key.KeyDown) {
            if (c->OutputPaused && !ConIsSystemKey(r[i].Key.VirtualKey)) {
                set_output_paused(c, FALSE);
                continue;
            }
            if ((c->InputMode & CON_LINE_INPUT) && r[i].Key.VirtualKey == CON_VK_PAUSE) {
                set_output_paused(c, TRUE);
                continue;
            }
        }
        r[kept++] = r[i];
    }
    return kept;
}

/*
 * writes records the way the console takes written and typed events: in
 * vt input the keys become their sequences, and an event written alone
 * merges into the last one when it repeats it. the head ones go ahead of
 * what waits. returns the events taken, called with the lock held.
 */
static NTSTATUS write_records(MIME_CONSOLE *c, MIME_CONDRV_RECORD *r, ULONG count, BOOLEAN head, ULONG *written)
{
    *written = 0;
    count = take_pause_keys(c, r, count);
    if (!count)
        return STATUS_SUCCESS;
    BOOLEAN vt = (c->InputMode & CON_VIRTUAL_TERMINAL_INPUT) != 0;
    if (count == 1 && !head && !vt) {
        MIME_CONDRV_RECORD *last = last_record(c);
        if (last && ConMergeRecord(last, r)) {
            *written = 1;
            return STATUS_SUCCESS;
        }
    }
    ULONG room = vt ? count * CON_MAX_SEQUENCE : count;
    CON_CHUNK *k = new_chunk(TRUE, room);
    if (!k)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (ULONG i = 0; i < count; i++) {
        ULONG made;
        if (vt && ConTranslateKey(&r[i], c->CursorApplication, &k->Record[k->Length], &made)) {
            k->Length += made;
        } else if (vt && count == 1 && !head && last_record(c) && ConMergeRecord(last_record(c), &r[i])) {
            ExFreePool(k);
            *written = 1;
            return STATUS_SUCCESS;
        } else {
            k->Record[k->Length++] = r[i];
        }
    }
    *written = count;
    if (!k->Length) {
        ExFreePool(k);
        return STATUS_SUCCESS;
    }
    /* a single event that waited before a prepend may merge into the new ones, as it is written again after them. */
    MIME_CONDRV_RECORD *alone = NULL;
    if (head && !IsListEmpty(&c->Input) && c->Input.Flink == c->Input.Blink) {
        CON_CHUNK *old = CONTAINING_RECORD(c->Input.Flink, CON_CHUNK, Links);
        if (old->Records && old->Length - old->First == 1 && !old->Offset)
            alone = &old->Record[old->First];
    }
    if (alone && ConMergeRecord(&k->Record[k->Length - 1], alone)) {
        ULONG acked = 0;
        CON_CHUNK *old = CONTAINING_RECORD(c->Input.Flink, CON_CHUNK, Links);
        k->HostBytes = old->HostBytes;
        free_chunk(c, old, &acked);
    }
    queue_chunk(c, k, head);
    return STATUS_SUCCESS;
}

/* typed input decoded into keys, or the bytes of a redirected input, with the lock held. */
static BOOLEAN host_input(MIME_CONSOLE *c, const UCHAR *data, ULONG length)
{
    if (!(c->Flags & MIME_CONDRV_TTY_INPUT) || (!length && !c->CarryLength)) {
        CON_CHUNK *k = new_chunk(FALSE, length);
        if (!k)
            return FALSE;
        memcpy(k->Data, data, length);
        k->Length = length;
        k->HostBytes = length + c->PendingAck;
        c->PendingAck = 0;
        queue_chunk(c, k, FALSE);
        return TRUE;
    }
    ULONG total = c->CarryLength + length;
    PUCHAR bytes = ExAllocatePool2(POOL_FLAG_NON_PAGED, total, KI_TAG('C', 'o', 'n', 'i'));
    CON_CHUNK *k = bytes ? new_chunk(TRUE, 2 * total) : NULL;
    if (!k) {
        if (bytes)
            ExFreePool(bytes);
        return FALSE;
    }
    memcpy(bytes, c->Carry, c->CarryLength);
    memcpy(bytes + c->CarryLength, data, length);
    /* the end of the input decodes what was left. */
    BOOLEAN more = length != 0;
    ULONG used = ConDecodeInput(bytes, total, (c->InputMode & CON_VIRTUAL_TERMINAL_INPUT) != 0, more, k->Record,
                                2 * total, &k->Length);
    c->CarryLength = total - used <= sizeof(c->Carry) ? total - used : 0;
    memcpy(c->Carry, bytes + total - c->CarryLength, c->CarryLength);
    ExFreePool(bytes);
    k->Length = take_pause_keys(c, k->Record, k->Length);
    c->PendingAck += length;
    if (k->Length) {
        k->HostBytes = c->PendingAck;
        c->PendingAck = 0;
        queue_chunk(c, k, FALSE);
    } else {
        ExFreePool(k);
    }
    if (!more) {
        CON_CHUNK *end = new_chunk(FALSE, 0);
        if (end)
            queue_chunk(c, end, FALSE);
    }
    return TRUE;
}

/* typed or piped input, an end of file when empty. */
void ConsoleHostInput(ULONG64 request, const UCHAR *data, ULONG length)
{
    ExAcquireFastMutex(&g_lock);
    MIME_CONSOLE *c = find_run(request);
    if (!c) {
        ExReleaseFastMutex(&g_lock);
        return;
    }
    BOOLEAN queued = host_input(c, data, length);
    ULONG unacked = 0;
    if (!queued) {
        unacked = c->PendingAck + length;
        c->PendingAck = 0;
    } else if (c->PendingAck && IsListEmpty(&c->Input)) {
        /* nothing waits that would acknowledge it later. */
        unacked = c->PendingAck;
        c->PendingAck = 0;
    }
    PKEVENT events[MAX_TARGETS];
    ULONG count = input_waiting(c) ? collect_events(c, events, MAX_TARGETS) : 0;
    ExReleaseFastMutex(&g_lock);
    signal_events(events, count, TRUE);
    if (unacked)
        send(c, MIME_EVT_CONSOLE_READ, unacked, NULL, 0);
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

/*
 * the utf-8 a record reads as from ReadConsole and ReadFile: the character
 * of a key press, an enter as \r\n in line input, nothing for the other
 * events. *used is the records it takes, two for a surrogate pair.
 */
static ULONG record_text(MIME_CONSOLE *c, CON_CHUNK *k, ULONG index, UCHAR out[8], ULONG *used)
{
    MIME_CONDRV_RECORD *r = &k->Record[index];
    *used = 1;
    if (r->EventType != CON_KEY_EVENT || !r->Key.KeyDown || !r->Key.Char)
        return 0;
    WCHAR w[2] = {r->Key.Char, 0};
    ULONG units = 1;
    if (w[0] >= 0xD800 && w[0] < 0xDC00 && index + 1 < k->Length) {
        MIME_CONDRV_RECORD *low = &k->Record[index + 1];
        if (low->EventType == CON_KEY_EVENT && low->Key.KeyDown && low->Key.Char >= 0xDC00 &&
            low->Key.Char < 0xE000) {
            w[1] = low->Key.Char;
            units = 2;
            *used = 2;
        }
    }
    ULONG n = 0;
    if (!NT_SUCCESS(RtlUnicodeToUTF8N((PCHAR)out, 8, &n, w, units * sizeof(WCHAR))))
        n = 0;
    if (w[0] == '\r' && (c->InputMode & CON_LINE_INPUT))
        out[n++] = '\n';
    return n;
}

/*
 * copies input as bytes, consuming it unless peeking, and the events that
 * read as nothing with it. stops before an end of file once data was copied.
 */
static ULONG take_input(MIME_CONSOLE *c, PUCHAR out, ULONG length, BOOLEAN peek, BOOLEAN *eof, ULONG *acked)
{
    ULONG done = 0;
    *eof = FALSE;
    *acked = 0;
    PLIST_ENTRY e = c->Input.Flink;
    while (e != &c->Input && done < length) {
        CON_CHUNK *k = CONTAINING_RECORD(e, CON_CHUNK, Links);
        e = e->Flink;
        if (!k->Records && k->Length == 0) {
            if (done)
                break;
            *eof = TRUE;
            if (!peek)
                free_chunk(c, k, acked);
            break;
        }
        ULONG first = k->First, offset = k->Offset;
        if (!k->Records) {
            ULONG n = k->Length - first;
            if (n > length - done)
                n = length - done;
            memcpy(out + done, k->Data + first, n);
            done += n;
            first += n;
            if (!peek)
                c->InputBytes -= n;
        }
        USHORT repeat_left = 0;
        while (k->Records && first < k->Length && done < length) {
            MIME_CONDRV_RECORD *r = &k->Record[first];
            UCHAR text[8];
            ULONG used;
            ULONG n = record_text(c, k, first, text, &used);
            USHORT repeat = repeat_left ? repeat_left : r->Key.RepeatCount ? r->Key.RepeatCount : 1;
            if (n) {
                ULONG part = n - offset < length - done ? n - offset : length - done;
                memcpy(out + done, text + offset, part);
                done += part;
                offset += part;
                if (offset < n)
                    break;
                offset = 0;
                if (repeat > 1 && used == 1) {
                    repeat_left = repeat - 1;
                    if (!peek)
                        r->Key.RepeatCount = repeat_left;
                    continue;
                }
            }
            repeat_left = 0;
            first += used;
            if (!peek)
                c->InputEvents -= used;
        }
        if (peek)
            continue;
        k->First = first;
        k->Offset = offset;
        if (first == k->Length)
            free_chunk(c, k, acked);
    }
    return done;
}

/* turns the bytes waiting into records for a reader of records, as a console reads its keyboard. */
static void bytes_to_records(MIME_CONSOLE *c)
{
    for (PLIST_ENTRY e = c->Input.Flink; e != &c->Input; e = e->Flink) {
        CON_CHUNK *k = CONTAINING_RECORD(e, CON_CHUNK, Links);
        ULONG length = k->Length - k->First;
        if (k->Records || !length)
            continue;
        CON_CHUNK *r = new_chunk(TRUE, 2 * length);
        if (!r)
            return;
        ConDecodeInput(k->Data + k->First, length, (c->InputMode & CON_VIRTUAL_TERMINAL_INPUT) != 0, FALSE,
                       r->Record, 2 * length, &r->Length);
        r->HostBytes = k->HostBytes;
        InsertHeadList(&k->Links, &r->Links);
        c->InputEvents += r->Length;
        c->InputBytes -= length;
        RemoveEntryList(&k->Links);
        ExFreePool(k);
        e = &r->Links;
    }
}

/* copies records, consuming them unless peeking. an end of file heading the input ends the read. */
static ULONG take_records(MIME_CONSOLE *c, MIME_CONDRV_RECORD *out, ULONG room, BOOLEAN peek, BOOLEAN *eof,
                          ULONG *acked)
{
    ULONG done = 0;
    *eof = FALSE;
    *acked = 0;
    bytes_to_records(c);
    PLIST_ENTRY e = c->Input.Flink;
    while (e != &c->Input && done < room) {
        CON_CHUNK *k = CONTAINING_RECORD(e, CON_CHUNK, Links);
        e = e->Flink;
        if (!k->Records) {
            if (done || k->Length)
                break;
            *eof = TRUE;
            if (!peek)
                free_chunk(c, k, acked);
            break;
        }
        ULONG n = k->Length - k->First < room - done ? k->Length - k->First : room - done;
        memcpy(out + done, k->Record + k->First, n * sizeof(*out));
        done += n;
        if (peek)
            continue;
        k->First += n;
        k->Offset = 0;
        c->InputEvents -= n;
        if (k->First == k->Length)
            free_chunk(c, k, acked);
    }
    return done;
}

/* after a read: the readiness and the input events follow what is left, and the host learns what was read. */
static void after_read(MIME_CONSOLE *c, ULONG acked, BOOLEAN clear)
{
    BOOLEAN closed = (c->Flags & MIME_CONDRV_CLOSED) != 0;
    if (clear && !closed && IsListEmpty(&c->Input))
        KeClearEvent(&c->InputReady);
    PKEVENT events[MAX_TARGETS];
    ULONG count = input_waiting(c) ? 0 : collect_events(c, events, MAX_TARGETS);
    ExReleaseFastMutex(&g_lock);
    signal_events(events, count, FALSE);
    if (acked)
        send(c, MIME_EVT_CONSOLE_READ, acked, NULL, 0);
}

static NTSTATUS read_input(MIME_CONSOLE *c, PUCHAR out, ULONG length, ULONG_PTR *information)
{
    for (;;) {
        ExAcquireFastMutex(&g_lock);
        BOOLEAN eof;
        ULONG acked;
        ULONG n = take_input(c, out, length, FALSE, &eof, &acked);
        BOOLEAN closed = (c->Flags & MIME_CONDRV_CLOSED) != 0;
        after_read(c, acked, !n && !eof);
        if (n || eof || closed || !length) {
            *information = n;
            return STATUS_SUCCESS;
        }
        NTSTATUS status = KeWaitForSingleObject(&c->InputReady, UserRequest, UserMode, FALSE, NULL);
        if (status != STATUS_SUCCESS)
            return status;
    }
}

/* ReadConsoleInput: the total waiting, then the records. */
static NTSTATUS read_records(MIME_CONSOLE *c, ULONG flags, PUCHAR buf, ULONG out_len, ULONG_PTR *information)
{
    if (out_len < sizeof(ULONG))
        return STATUS_BUFFER_TOO_SMALL;
    ULONG room = (out_len - sizeof(ULONG)) / sizeof(MIME_CONDRV_RECORD);
    MIME_CONDRV_RECORD *records = (MIME_CONDRV_RECORD *)(buf + sizeof(ULONG));
    BOOLEAN peek = (flags & MIME_CONDRV_READ_PEEK) != 0;
    for (;;) {
        ExAcquireFastMutex(&g_lock);
        BOOLEAN eof;
        ULONG acked;
        ULONG n = take_records(c, records, room, peek, &eof, &acked);
        *(PULONG)buf = c->InputEvents;
        BOOLEAN closed = (c->Flags & MIME_CONDRV_CLOSED) != 0;
        after_read(c, acked, !n && !eof);
        if (n || eof || closed || !room || (flags & MIME_CONDRV_READ_NOWAIT)) {
            *information = sizeof(ULONG) + n * sizeof(MIME_CONDRV_RECORD);
            return STATUS_SUCCESS;
        }
        NTSTATUS status = KeWaitForSingleObject(&c->InputReady, UserRequest, UserMode, FALSE, NULL);
        if (status != STATUS_SUCCESS)
            return status;
    }
}

/* events as WriteConsoleInput keeps them: known types only, the flags a console stores as booleans. */
static BOOLEAN normalize_records(MIME_CONDRV_RECORD *r, ULONG count)
{
    for (ULONG i = 0; i < count; i++) {
        r[i].Reserved = 0;
        switch (r[i].EventType) {
        case CON_KEY_EVENT:
            r[i].Key.KeyDown = r[i].Key.KeyDown != 0;
            break;
        case CON_MOUSE_EVENT:
            break;
        case CON_FOCUS_EVENT:
            r[i].Value = r[i].Value != 0;
            /* fall through */
        case CON_WINDOW_BUFFER_SIZE_EVENT:
        case CON_MENU_EVENT: {
            ULONG value = r[i].Value;
            memset(&r[i].Key, 0, sizeof(r[i].Key));
            r[i].Value = value;
            break;
        }
        default:
            return FALSE;
        }
    }
    return TRUE;
}

/* the cursor key mode a program sets with DECCKM decides what vt input sends for the arrows. */
static void watch_output(MIME_CONSOLE *c, const UCHAR *p, ULONG length)
{
    for (ULONG i = 0; i + 4 < length; i++) {
        if (p[i] == 0x1B && p[i + 1] == '[' && p[i + 2] == '?' && p[i + 3] == '1' &&
            (p[i + 4] == 'h' || p[i + 4] == 'l'))
            c->CursorApplication = p[i + 4] == 'h';
    }
}

/* output waits while the pause key holds it. */
static NTSTATUS wait_output(MIME_CONSOLE *c)
{
    if (!c->OutputPaused)
        return STATUS_SUCCESS;
    return KeWaitForSingleObject(&c->OutputResumed, UserRequest, UserMode, FALSE, NULL);
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
                st->InputEvents = c->InputEvents;
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
        } else if ((status = wait_output(c)) == STATUS_SUCCESS) {
            ULONG stream = *(PULONG)buf == MIME_CONDRV_STDERR ? MIME_CONDRV_STDERR : MIME_CONDRV_STDOUT;
            ExAcquireFastMutex(&g_lock);
            watch_output(c, buf + sizeof(ULONG), in_len - sizeof(ULONG));
            ExReleaseFastMutex(&g_lock);
            send(c, MIME_EVT_CONSOLE_OUTPUT, stream, buf + sizeof(ULONG), in_len - sizeof(ULONG));
            information = in_len - sizeof(ULONG);
        }
        break;
    case IOCTL_CONDRV_READ:
        status = read_input(c, buf, out_len, &information);
        break;
    case IOCTL_CONDRV_PEEK: {
        BOOLEAN eof;
        ULONG acked;
        ExAcquireFastMutex(&g_lock);
        information = take_input(c, buf, out_len, TRUE, &eof, &acked);
        ExReleaseFastMutex(&g_lock);
        break;
    }
    case IOCTL_CONDRV_READ_INPUT:
        if (in_len < sizeof(ULONG))
            status = STATUS_INVALID_PARAMETER;
        else
            status = read_records(c, *(PULONG)buf, buf, out_len, &information);
        break;
    case IOCTL_CONDRV_WRITE_INPUT: {
        ULONG count = in_len >= sizeof(MIME_CONDRV_WRITE_INPUT)
                          ? (in_len - sizeof(MIME_CONDRV_WRITE_INPUT)) / sizeof(MIME_CONDRV_RECORD)
                          : 0;
        MIME_CONDRV_RECORD *records = (MIME_CONDRV_RECORD *)(buf + sizeof(MIME_CONDRV_WRITE_INPUT));
        if (in_len < sizeof(MIME_CONDRV_WRITE_INPUT) || out_len < sizeof(ULONG) ||
            !normalize_records(records, count)) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        BOOLEAN append = (((MIME_CONDRV_WRITE_INPUT *)buf)->Flags & MIME_CONDRV_WRITE_APPEND) != 0;
        ULONG written = 0;
        ExAcquireFastMutex(&g_lock);
        BOOLEAN was_waiting = input_waiting(c);
        status = write_records(c, records, count, !append, &written);
        PKEVENT events[MAX_TARGETS];
        ULONG n = !was_waiting && input_waiting(c) ? collect_events(c, events, MAX_TARGETS) : 0;
        ExReleaseFastMutex(&g_lock);
        signal_events(events, n, TRUE);
        *(PULONG)buf = written;
        information = sizeof(ULONG);
        break;
    }
    case IOCTL_CONDRV_ANSI_CARRY: {
        MIME_CONDRV_RECORD kept;
        ExAcquireFastMutex(&g_lock);
        BOOLEAN had = c->HasAnsiCarry;
        kept = c->AnsiCarry;
        c->HasAnsiCarry = in_len >= sizeof(MIME_CONDRV_RECORD);
        if (c->HasAnsiCarry)
            c->AnsiCarry = *(MIME_CONDRV_RECORD *)buf;
        ExReleaseFastMutex(&g_lock);
        if (had && out_len >= sizeof(kept)) {
            *(MIME_CONDRV_RECORD *)buf = kept;
            information = sizeof(kept);
        }
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
        while (!IsListEmpty(&c->Input))
            free_chunk(c, CONTAINING_RECORD(c->Input.Flink, CON_CHUNK, Links), &acked);
        acked += c->PendingAck;
        c->PendingAck = 0;
        c->CarryLength = 0;
        c->HasAnsiCarry = FALSE;
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
        } else if (length && (status = wait_output(c)) == STATUS_SUCCESS) {
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
        CON_CHUNK *head = IsListEmpty(&c->Input) ? NULL : CONTAINING_RECORD(c->Input.Flink, CON_CHUNK, Links);
        BOOLEAN at_end = head && !head->Records && !head->Length;
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
