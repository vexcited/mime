/*
 * the consoles behind \Device\ConDrv. a console has an input buffer of records, a terminal with its screen buffers,
 * and a backend carrying the terminal's bytes: a host run's terminal, a pseudo console, or nothing. a console
 * program is attached to the console it is created in. a gui program is not, but may attach with AttachConsole or
 * AllocConsole.
 *
 * host input waits here until a process reads it, writes go to the screen buffers and on to the terminal, and a ctrl
 * event runs the ctrl routine kernelbase registered on a new thread of each attached process, as csrss does on
 * windows.
 *
 * when the terminal goes away the processes get CTRL_CLOSE_EVENT and five seconds to exit before they are terminated.
 */
#include "console_p.h"

FAST_MUTEX g_con_lock;
static LIST_ENTRY g_consoles;
static ULONG64 g_next_console = 1;

#define WINDOW_COLUMNS 120
#define WINDOW_ROWS 30
#define WINDOW_BUFFER_ROWS 9001

/* ------------------------------------------------------------ memory for the screen module */

void *CsAllocate(size_t size)
{
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, KI_TAG('C', 'o', 'n', 's'));
}

void CsFree(void *memory)
{
    if (memory)
        ExFreePool(memory);
}

/* ------------------------------------------------------------ lifetime */

void ConReference(MIME_CONSOLE *c)
{
    ExAcquireFastMutex(&g_con_lock);
    c->References++;
    ExReleaseFastMutex(&g_con_lock);
}

static void destroy(MIME_CONSOLE *c)
{
    while (!IsListEmpty(&c->Input))
        ExFreePool(CONTAINING_RECORD(RemoveHeadList(&c->Input), CON_CHUNK, Links));
    while (!IsListEmpty(&c->PtyOut))
        ExFreePool(CONTAINING_RECORD(RemoveHeadList(&c->PtyOut), CON_PTY_CHUNK, Links));
    CsTerminalDestroy(&c->Term);
    ConLineStateDestroy(c->Lines);
    if (c->Scratch)
        ExFreePool(c->Scratch);
    if (c->LinePending)
        ExFreePool(c->LinePending);
    if (c->BytePending)
        ExFreePool(c->BytePending);
    ExFreePool(c);
}

void ConRelease(MIME_CONSOLE *c)
{
    ExAcquireFastMutex(&g_con_lock);
    BOOLEAN last = --c->References == 0;
    if (last)
        RemoveEntryList(&c->Links);
    ExReleaseFastMutex(&g_con_lock);
    if (last)
        destroy(c);
}

/* the process's console, referenced, or NULL. */
MIME_CONSOLE *ConProcessConsole(PEPROCESS p)
{
    ExAcquireFastMutex(&g_con_lock);
    MIME_CONSOLE *c = p->Console;
    if (c)
        c->References++;
    ExReleaseFastMutex(&g_con_lock);
    return c;
}

/* the host run's console, referenced, or NULL. called with the lock held. */
static MIME_CONSOLE *find_run(ULONG64 request)
{
    for (PLIST_ENTRY e = g_consoles.Flink; e != &g_consoles; e = e->Flink) {
        MIME_CONSOLE *c = CONTAINING_RECORD(e, MIME_CONSOLE, Links);
        if (c->Backend == CON_BACKEND_RUN && c->RequestId == request && !(c->Flags & MIME_CONDRV_CLOSED)) {
            c->References++;
            return c;
        }
    }
    return nullptr;
}

/* size is columns | rows << 16, else the usual console window size. */
MIME_CONSOLE *ConCreateConsole(CON_BACKEND backend, ULONG64 request, ULONG tty_flags, ULONG size)
{
    MIME_CONSOLE *c = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), KI_TAG('C', 'o', 'n', 'S'));
    if (!c)
        return nullptr;
    c->References = 1;
    c->RequestId = request;
    c->Backend = backend;
    c->Flags = tty_flags & (MIME_CONDRV_TTY_INPUT | MIME_CONDRV_TTY_OUTPUT | MIME_CONDRV_TTY_ERROR);
    c->InputMode = DEFAULT_INPUT_MODE;
    c->QuickEdit = c->InsertDefault = c->AutoPosition = TRUE;
    c->InputCp = c->OutputCp = 65001;
    c->OutStream = (c->Flags & MIME_CONDRV_TTY_OUTPUT) ? MIME_CONDRV_STDOUT : MIME_CONDRV_STDERR;
    InitializeListHead(&c->Input);
    InitializeListHead(&c->InputHandles);
    InitializeListHead(&c->PtyOut);
    KeInitializeEvent(&c->InputReady, NotificationEvent, FALSE);
    KeInitializeEvent(&c->OutputResumed, NotificationEvent, TRUE);
    KeInitializeEvent(&c->PtyReady, NotificationEvent, FALSE);
    KeInitializeEvent(&c->PtySpace, NotificationEvent, TRUE);
    KeInitializeEvent(&c->HostReady, NotificationEvent, backend != CON_BACKEND_WINDOW);
    int cols = size & 0xFFFF ? (int)(size & 0xFFFF) : 80;
    int rows = size >> 16 ? (int)(size >> 16) : 25;
    int buffer_rows = rows;
    if (backend == CON_BACKEND_WINDOW) {
        cols = WINDOW_COLUMNS;
        rows = WINDOW_ROWS;
        buffer_rows = WINDOW_BUFFER_ROWS;
    }
    c->Lines = ConLineStateCreate();
    if (!c->Lines || !CsTerminalInit(&c->Term, cols, rows)) {
        ConLineStateDestroy(c->Lines);
        ExFreePool(c);
        return nullptr;
    }
    c->Term.Headless = backend == CON_BACKEND_NONE || (backend == CON_BACKEND_RUN && !(c->Flags & (MIME_CONDRV_TTY_OUTPUT | MIME_CONDRV_TTY_ERROR)));
    if (backend == CON_BACKEND_WINDOW) {
        CsTerminalSetWindowed(&c->Term, 0, 0);
        c->Flags |= MIME_CONDRV_HAS_WINDOW;
    } else if (c->Term.Headless) {
        c->Flags |= MIME_CONDRV_HEADLESS;
    }
    if (backend == CON_BACKEND_PTY)
        c->Flags |= MIME_CONDRV_PTY_HOST;
    c->Main = CsBufferCreate(&c->Term, cols, buffer_rows);
    if (!c->Main) {
        CsTerminalDestroy(&c->Term);
        ConLineStateDestroy(c->Lines);
        ExFreePool(c);
        return nullptr;
    }
    CsShow(&c->Term, c->Main);
    c->Main->Refs = 1;
    ExAcquireFastMutex(&g_con_lock);
    c->Id = g_next_console++;
    InsertTailList(&g_consoles, &c->Links);
    ExReleaseFastMutex(&g_con_lock);
    return c;
}

/* ------------------------------------------------------------ the host side */

MIME_CONSOLE *ConsoleCreate(ULONG64 request, ULONG run_flags, ULONG size, ULONG cursor)
{
    ULONG tty = 0;
    if (run_flags & MIME_RUN_TTY_INPUT)
        tty |= MIME_CONDRV_TTY_INPUT;
    if (run_flags & MIME_RUN_TTY_OUTPUT)
        tty |= MIME_CONDRV_TTY_OUTPUT;
    if (run_flags & MIME_RUN_TTY_ERROR)
        tty |= MIME_CONDRV_TTY_ERROR;
    MIME_CONSOLE *c = ConCreateConsole(CON_BACKEND_RUN, request, tty, size);
    if (c && (cursor & 0xFFFF) && (cursor >> 16)) {
        /* the terminal had text above the program's first line: the console starts at the cursor. */
        ExAcquireFastMutex(&g_con_lock);
        CsSetHostCursor(&c->Term, (int)(cursor & 0xFFFF) - 1, (int)(cursor >> 16) - 1);
        ExReleaseFastMutex(&g_con_lock);
    }
    return c;
}

void ConsoleDereference(MIME_CONSOLE *c)
{
    ConRelease(c);
}

NTSTATUS ConsoleAttach(PEPROCESS p, MIME_CONSOLE *c)
{
    ExAcquireFastMutex(&g_con_lock);
    NTSTATUS status = STATUS_ACCESS_DENIED;
    if (c->Ended) {
        status = STATUS_INVALID_HANDLE;
    } else if (!p->Console) {
        p->Console = c;
        p->ConsoleGroup = (ULONG)(ULONG_PTR)p->UniqueProcessId;
        c->References++;
        status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&g_con_lock);
    return status;
}

/* the processes attached to a console. called with the lock held. */
static ULONG attached_count(MIME_CONSOLE *c)
{
    ULONG total = 0;
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->Console == c && !p->Exited)
            total++;
    }
    KiReleaseDispatcherLock(s);
    return total;
}

void ConsoleDetach(PEPROCESS p)
{
    ExAcquireFastMutex(&g_con_lock);
    MIME_CONSOLE *c = p->Console;
    p->Console = nullptr;
    /* a hung up pseudo console waits for the last process to leave. */
    if (c && c->Backend == CON_BACKEND_PTY)
        KeSetEvent(&c->PtyReady, 0, FALSE);
    /* a console window closes with its last process. */
    if (c && c->Backend == CON_BACKEND_WINDOW && !c->Ended && !attached_count(c)) {
        c->Ended = TRUE;
        KeSetEvent(&c->HostReady, 0, FALSE);
        c->HostPending |= MIME_CONDRV_CHANGED_END;
        if (c->HostEvent)
            KeSetEvent(c->HostEvent, 0, FALSE);
    }
    ExReleaseFastMutex(&g_con_lock);
    if (c)
        ConRelease(c);
}

void ConsoleProcessExit(PEPROCESS p)
{
    ConsoleDetach(p);
    ExAcquireFastMutex(&g_con_lock);
    MIME_CONSOLE *hint = p->ConsoleHint;
    p->ConsoleHint = nullptr;
    ExReleaseFastMutex(&g_con_lock);
    if (hint)
        ConRelease(hint);
}

/* the terminal a process can still reach with AllocConsole: the run it descends from. */
void ConsoleSetHint(PEPROCESS p, MIME_CONSOLE *c)
{
    ExAcquireFastMutex(&g_con_lock);
    if (c && !p->ConsoleHint && c->Backend == CON_BACKEND_RUN) {
        c->References++;
        p->ConsoleHint = c;
    }
    ExReleaseFastMutex(&g_con_lock);
}

/* a console shows its first program's title until a program sets another. */
void ConsoleInitialTitle(MIME_CONSOLE *c, PCUNICODE_STRING title)
{
    ExAcquireFastMutex(&g_con_lock);
    if (!c->OriginalTitle[0] && title && title->Length) {
        ULONG n = title->Length / sizeof(WCHAR);
        if (n > RTL_NUMBER_OF(c->Title) - 1)
            n = RTL_NUMBER_OF(c->Title) - 1;
        memcpy(c->OriginalTitle, title->Buffer, n * sizeof(WCHAR));
        c->OriginalTitle[n] = 0;
        if (!c->TitleSet) {
            memcpy(c->Title, c->OriginalTitle, (n + 1) * sizeof(WCHAR));
        }
    }
    ExReleaseFastMutex(&g_con_lock);
}

/* ------------------------------------------------------------ the input buffer */

CON_CHUNK *ConNewChunk(BOOLEAN records, ULONG length)
{
    SIZE_T size = FIELD_OFFSET(CON_CHUNK, Data) + (records ? length * sizeof(MIME_CONDRV_RECORD) : length);
    CON_CHUNK *k = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('C', 'o', 'n', 'i'));
    if (k)
        k->Records = records;
    return k;
}

BOOLEAN ConInputWaiting(MIME_CONSOLE *c)
{
    return !IsListEmpty(&c->Input) || (c->Flags & MIME_CONDRV_CLOSED);
}

/* input handles are signaled while input waits, so they can be waited on. */
void ConUpdateInputEvents(MIME_CONSOLE *c)
{
    BOOLEAN waiting = ConInputWaiting(c);
    for (PLIST_ENTRY e = c->InputHandles.Flink; e != &c->InputHandles; e = e->Flink) {
        CON_HANDLE *h = CONTAINING_RECORD(e, CON_HANDLE, Links);
        if (waiting)
            KeSetEvent(&h->File->Event, 0, FALSE);
        else
            KeClearEvent(&h->File->Event);
    }
}

void ConQueueChunk(MIME_CONSOLE *c, CON_CHUNK *k, BOOLEAN head)
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
    ConUpdateInputEvents(c);
}

void ConFreeChunk(MIME_CONSOLE *c, CON_CHUNK *k, ULONG *acked)
{
    RemoveEntryList(&k->Links);
    if (k->Records)
        c->InputEvents -= k->Length - k->First;
    else
        c->InputBytes -= k->Length - k->First;
    *acked += k->HostBytes;
    ExFreePool(k);
}

static void send_ack(MIME_CONSOLE *c, ULONG acked)
{
    if (acked && c->Backend == CON_BACKEND_RUN)
        MimeSendConsole(MIME_EVT_CONSOLE_READ, c->RequestId, acked, nullptr, 0);
}

/* the last record waiting, which an event written alone may merge into. */
static MIME_CONDRV_RECORD *last_record(MIME_CONSOLE *c)
{
    if (IsListEmpty(&c->Input))
        return nullptr;
    CON_CHUNK *k = CONTAINING_RECORD(c->Input.Blink, CON_CHUNK, Links);
    return k->Records && k->First < k->Length ? &k->Record[k->Length - 1] : nullptr;
}

static void set_output_paused(MIME_CONSOLE *c, BOOLEAN paused)
{
    c->OutputPaused = paused;
    if (paused)
        KeClearEvent(&c->OutputResumed);
    else
        KeSetEvent(&c->OutputResumed, 0, FALSE);
}

/* the pause key and ctrl+s, which pause the output in line input. */
static BOOLEAN is_pause_request(const MIME_CONDRV_RECORD *r)
{
    if (r->Key.VirtualKey == CON_VK_PAUSE)
        return TRUE;
    ULONG state = r->Key.ControlKeyState;
    return r->Key.VirtualKey == 'S' && (state & (CON_LEFT_CTRL | CON_RIGHT_CTRL)) && !(state & (CON_LEFT_ALT | CON_RIGHT_ALT));
}

/*
 * keyboard output pausing. records are handled in order, each seeing the state the earlier ones left. a pressed
 * non-system key resumes paused output and is swallowed, and in line input the pause keys pause it and are swallowed.
 * the rest move to the front of the array and are counted.
 */
static ULONG take_pause_keys(MIME_CONSOLE *c, MIME_CONDRV_RECORD *r, ULONG count)
{
    BOOLEAN line = (c->InputMode & CON_LINE_INPUT) != 0;
    ULONG kept = 0;
    for (ULONG i = 0; i < count; i++) {
        if (r[i].EventType == CON_KEY_EVENT && r[i].Key.KeyDown) {
            if (c->OutputPaused && !ConIsSystemKey(r[i].Key.VirtualKey)) {
                set_output_paused(c, FALSE);
                continue;
            }
            if (line && is_pause_request(&r[i])) {
                set_output_paused(c, TRUE);
                continue;
            }
        }
        r[kept++] = r[i];
    }
    return kept;
}


/*
 * queues records like the console takes written and typed events: in vt input keys become their sequences, and an
 * event written alone merges into the last one when it repeats it. head puts them before what waits. returns the
 * events taken. called with the lock held.
 */
NTSTATUS ConWriteRecords(MIME_CONSOLE *c, MIME_CONDRV_RECORD *r, ULONG count, BOOLEAN head, ULONG *written)
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
    CON_CHUNK *k = ConNewChunk(TRUE, room);
    if (!k)
        return STATUS_INSUFFICIENT_RESOURCES;
    BOOLEAN app_cursor = (c->Term.Modes & CS_MODE_APP_CURSOR) != 0;
    for (ULONG i = 0; i < count; i++) {
        ULONG made;
        if (vt && ConTranslateKey(&r[i], app_cursor, &k->Record[k->Length], &made)) {
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
    MIME_CONDRV_RECORD *alone = nullptr;
    if (head && !IsListEmpty(&c->Input) && c->Input.Flink == c->Input.Blink) {
        CON_CHUNK *old = CONTAINING_RECORD(c->Input.Flink, CON_CHUNK, Links);
        if (old->Records && old->Length - old->First == 1 && !old->Offset)
            alone = &old->Record[old->First];
    }
    if (alone && ConMergeRecord(&k->Record[k->Length - 1], alone)) {
        ULONG acked = 0;
        CON_CHUNK *old = CONTAINING_RECORD(c->Input.Flink, CON_CHUNK, Links);
        k->HostBytes = old->HostBytes;
        ConFreeChunk(c, old, &acked);
    }
    ConQueueChunk(c, k, head);
    return STATUS_SUCCESS;
}

/* queues the terminal's answers to escape sequences as input, as if typed. */
static void inject_text(MIME_CONSOLE *c, const UCHAR *text, ULONG length)
{
    CON_CHUNK *k = ConNewChunk(TRUE, length * 2);
    if (!k)
        return;
    for (ULONG i = 0; i < length; i++) {
        for (int down = 1; down >= 0; down--) {
            MIME_CONDRV_RECORD *r = &k->Record[k->Length++];
            memset(r, 0, sizeof(*r));
            r->EventType = CON_KEY_EVENT;
            r->Key.KeyDown = down;
            r->Key.RepeatCount = 1;
            r->Key.Char = text[i];
        }
    }
    ConQueueChunk(c, k, FALSE);
}

/* ------------------------------------------------------------ the terminal's bytes */

void ConSendOutput(MIME_CONSOLE *c, const void *data, ULONG length, ULONG stream)
{
    if (!length)
        return;
    if (c->Backend == CON_BACKEND_RUN) {
        MimeSendConsole(MIME_EVT_CONSOLE_OUTPUT, c->RequestId, stream, data, length);
    } else if (c->Backend == CON_BACKEND_PTY) {
        const UCHAR *p = data;
        while (length) {
            ULONG n = length > 4096 ? 4096 : length;
            CON_PTY_CHUNK *k = ExAllocatePool2(POOL_FLAG_NON_PAGED, FIELD_OFFSET(CON_PTY_CHUNK, Data) + n, KI_TAG('C', 'o', 'n', 'p'));
            if (!k)
                return;
            k->Length = n;
            memcpy(k->Data, p, n);
            InsertTailList(&c->PtyOut, &k->Links);
            c->PtyQueued += n;
            p += n;
            length -= n;
        }
        if (c->PtyQueued > 256 * 1024)
            KeClearEvent(&c->PtySpace);
        KeSetEvent(&c->PtyReady, 0, FALSE);
    }
}

/* sends the terminal its bytes and queues its replies as input. called with the lock held. */
void ConFlush(MIME_CONSOLE *c)
{
    CsTerminal *t = &c->Term;
    if (t->Changed & CS_CHANGED_TITLE) {
        ULONG n = t->TitleLength;
        memcpy(c->Title, t->Title, n * sizeof(WCHAR));
        c->Title[n] = 0;
        c->TitleSet = TRUE;
        t->Changed &= ~(unsigned)CS_CHANGED_TITLE;
        CsSetTitle(t, t->Title, n);
        c->HostPending |= MIME_CONDRV_CHANGED_TITLE;
    }
    size_t n;
    const uint8_t *out = CsTakeOut(t, &n);
    if (n)
        ConSendOutput(c, out, (ULONG)n, c->OutStream);
    const uint8_t *reply = CsTakeReply(t, &n);
    if (n) {
        UCHAR copy[256];
        if (n > sizeof(copy))
            n = sizeof(copy);
        memcpy(copy, reply, n);
        inject_text(c, copy, (ULONG)n);
    }
    ConNotify(c);
}

/* tells the window of a console what changed. called with the lock held. */
void ConNotify(MIME_CONSOLE *c)
{
    CsTerminal *t = &c->Term;
    if (c->Backend != CON_BACKEND_WINDOW)
        return;
    if (t->Generation != c->SeenGeneration) {
        c->SeenGeneration = t->Generation;
        c->HostPending |= MIME_CONDRV_CHANGED_CELLS;
    }
    if (t->Changed & CS_CHANGED_WINDOW) {
        t->Changed &= ~(unsigned)CS_CHANGED_WINDOW;
        c->HostPending |= MIME_CONDRV_CHANGED_WINDOW;
    }
    if (t->BellCount != c->SeenBells) {
        c->SeenBells = t->BellCount;
        c->HostPending |= MIME_CONDRV_CHANGED_BELL;
    }
    if (c->HostPending && c->HostEvent)
        KeSetEvent(c->HostEvent, 0, FALSE);
}

void ConSetTitle(MIME_CONSOLE *c, const WCHAR *title, ULONG count)
{
    if (count > RTL_NUMBER_OF(c->Title) - 1)
        count = RTL_NUMBER_OF(c->Title) - 1;
    memcpy(c->Title, title, count * sizeof(WCHAR));
    c->Title[count] = 0;
    c->TitleSet = TRUE;
    CsSetTitle(&c->Term, title, count);
    c->HostPending |= MIME_CONDRV_CHANGED_TITLE;
    ConFlush(c);
}

/* mouse reports are requested while the program has the mouse and quick edit is off. */
static void apply_mouse(MIME_CONSOLE *c)
{
    uint32_t api = 0;
    if ((c->InputMode & ENABLE_MOUSE_INPUT) && !c->QuickEdit)
        api |= CS_MODE_MOUSE_ANY | CS_MODE_MOUSE_SGR;
    c->Term.ApiModes = api;
    CsRender(&c->Term);
    ConFlush(c);
}

/*
 * the mode is kept even when refused. the four extended flag bits are not stored in the mode: when any is given they
 * set the three booleans, and only then are they reported back.
 */
NTSTATUS ConSetInputMode(MIME_CONSOLE *c, ULONG mode)
{
    c->InputMode = mode & ~INPUT_MODE_EXTENDED;
    c->ExtendedGiven = (mode & INPUT_MODE_EXTENDED) != 0;
    if (c->ExtendedGiven) {
        c->InsertDefault = (mode & ENABLE_INSERT_MODE) != 0;
        c->QuickEdit = (mode & ENABLE_QUICK_EDIT_MODE) != 0;
        c->AutoPosition = (mode & ENABLE_AUTO_POSITION) != 0;
    }
    apply_mouse(c);
    c->HostPending |= MIME_CONDRV_CHANGED_MODE;
    ConNotify(c);
    if ((mode & ~INPUT_MODE_MASK) || ((mode & ENABLE_ECHO_INPUT) && !(mode & ENABLE_LINE_INPUT)))
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

ULONG ConGetInputMode(MIME_CONSOLE *c)
{
    ULONG mode = c->InputMode;
    if (c->ExtendedGiven) {
        mode |= ENABLE_EXTENDED_FLAGS;
        if (c->InsertDefault)
            mode |= ENABLE_INSERT_MODE;
        if (c->QuickEdit)
            mode |= ENABLE_QUICK_EDIT_MODE;
        if (c->AutoPosition)
            mode |= ENABLE_AUTO_POSITION;
    }
    return mode;
}


/* a ctrl+c or ctrl+break ends the reads that wait; ctrl+break empties the input first. */
void ConAbortReads(MIME_CONSOLE *c, BOOLEAN flush)
{
    ExAcquireFastMutex(&g_con_lock);
    c->AbortGeneration++;
    if (flush) {
        ConFlushInput(c);
        if (c->LinePending) {
            ExFreePool(c->LinePending);
            c->LinePending = nullptr;
            c->LinePendingLength = 0;
            c->LinePendingLines = 0;
        }
    }
    KeSetEvent(&c->InputReady, 0, FALSE);
    ExReleaseFastMutex(&g_con_lock);
}

/* ------------------------------------------------------------ typed input */

/* removes processed input's ctrl+c presses from decoded records. returns the records left. */
static ULONG take_ctrl_c(MIME_CONDRV_RECORD *r, ULONG count, BOOLEAN *found)
{
    ULONG kept = 0;
    for (ULONG i = 0; i < count; i++) {
        if (r[i].EventType == CON_KEY_EVENT && r[i].Key.Char == 3 && r[i].Key.VirtualKey == 'C' &&
            (r[i].Key.ControlKeyState & (CON_LEFT_CTRL | CON_RIGHT_CTRL))) {
            if (r[i].Key.KeyDown)
                *found = TRUE;
            continue;
        }
        r[kept++] = r[i];
    }
    return kept;
}

/* decodes typed input into keys, or queues redirected input bytes. called with the lock held. */
static BOOLEAN host_input(MIME_CONSOLE *c, const UCHAR *data, ULONG length, BOOLEAN *ctrl_c)
{
    if (!(c->Flags & MIME_CONDRV_TTY_INPUT) || (!length && !c->CarryLength)) {
        CON_CHUNK *k = ConNewChunk(FALSE, length);
        if (!k)
            return FALSE;
        memcpy(k->Data, data, length);
        k->Length = length;
        k->HostBytes = length + c->PendingAck;
        c->PendingAck = 0;
        ConQueueChunk(c, k, FALSE);
        return TRUE;
    }
    /* a pseudo console that waits for the cursor of its terminal takes the report before anything else. */
    if (c->AwaitCursor) {
        UCHAR joined[sizeof(c->Carry)];
        ULONG had = c->CarryLength;
        ULONG add = length < sizeof(joined) - had ? length : (ULONG)sizeof(joined) - had;
        memcpy(joined, c->Carry, had);
        memcpy(joined + had, data, add);
        ULONG have = had + add;
        ULONG row = 0, col = 0, i = 2;
        BOOLEAN prefix = have >= 1 && joined[0] == 0x1B && (have < 2 || joined[1] == '[');
        BOOLEAN complete = FALSE;
        if (prefix && have >= 2) {
            while (i < have && joined[i] >= '0' && joined[i] <= '9')
                row = row * 10 + (joined[i++] - '0');
            if (i < have && joined[i] == ';') {
                i++;
                while (i < have && joined[i] >= '0' && joined[i] <= '9')
                    col = col * 10 + (joined[i++] - '0');
                if (i < have && joined[i] == 'R')
                    complete = TRUE;
                else if (i < have)
                    prefix = FALSE;
            } else if (i < have) {
                prefix = FALSE;
            }
        }
        if (complete && row && col) {
            ULONG used = i + 1 - had;
            c->AwaitCursor = FALSE;
            CsSetHostCursor(&c->Term, (int)col - 1, (int)row - 1);
            c->CarryLength = 0;
            c->PendingAck += used;
            data += used;
            length -= used;
            if (!length)
                return TRUE;
        } else if (prefix && !complete && have < sizeof(joined)) {
            memcpy(c->Carry, joined, have);
            c->CarryLength = have;
            c->PendingAck += length;
            return TRUE;
        }
    }
    ULONG total = c->CarryLength + length;
    PUCHAR bytes = ExAllocatePool2(POOL_FLAG_NON_PAGED, total, KI_TAG('C', 'o', 'n', 'i'));
    CON_CHUNK *k = bytes ? ConNewChunk(TRUE, 2 * total + 8) : nullptr;
    if (!k) {
        if (bytes)
            ExFreePool(bytes);
        return FALSE;
    }
    memcpy(bytes, c->Carry, c->CarryLength);
    memcpy(bytes + c->CarryLength, data, length);
    /* the end of the input decodes what was left. */
    BOOLEAN more = length != 0;
    CON_DECODE decode = {};
    decode.Vt = (c->InputMode & CON_VIRTUAL_TERMINAL_INPUT) != 0;
    decode.Mouse = (c->InputMode & ENABLE_MOUSE_INPUT) != 0;
    decode.Buttons = c->MouseButtons;
    decode.LastClick = c->LastClick;
    decode.LastClickX = c->LastClickX;
    decode.LastClickY = c->LastClickY;
    decode.LastClickButton = c->LastClickButton;
    decode.Now = KeQueryInterruptTime() / 10000;
    ULONG used = ConDecodeInput(&decode, bytes, total, more, k->Record, 2 * total + 8, &k->Length);
    c->MouseButtons = decode.Buttons;
    c->LastClick = decode.LastClick;
    c->LastClickX = decode.LastClickX;
    c->LastClickY = decode.LastClickY;
    c->LastClickButton = decode.LastClickButton;
    c->CarryLength = total - used <= sizeof(c->Carry) ? total - used : 0;
    memcpy(c->Carry, bytes + total - c->CarryLength, c->CarryLength);
    ExFreePool(bytes);
    if (c->InputMode & ENABLE_PROCESSED_INPUT)
        k->Length = take_ctrl_c(k->Record, k->Length, ctrl_c);
    k->Length = take_pause_keys(c, k->Record, k->Length);
    c->PendingAck += length;
    if (k->Length) {
        k->HostBytes = c->PendingAck;
        c->PendingAck = 0;
        ConQueueChunk(c, k, FALSE);
    } else {
        ExFreePool(k);
    }
    if (!more) {
        CON_CHUNK *end = ConNewChunk(FALSE, 0);
        if (end)
            ConQueueChunk(c, end, FALSE);
    }
    return TRUE;
}

/* typed or piped input, empty means end of file. */
void ConHostInputLocked(MIME_CONSOLE *c, const UCHAR *data, ULONG length, BOOLEAN *ctrl_c)
{
    BOOLEAN queued = host_input(c, data, length, ctrl_c);
    ULONG unacked = 0;
    if (!queued) {
        unacked = c->PendingAck + length;
        c->PendingAck = 0;
    } else if (c->PendingAck && IsListEmpty(&c->Input)) {
        /* nothing waits that would acknowledge it later. */
        unacked = c->PendingAck;
        c->PendingAck = 0;
    }
    ConUpdateInputEvents(c);
    send_ack(c, unacked);
}

void ConsoleHostInput(ULONG64 request, const UCHAR *data, ULONG length)
{
    ExAcquireFastMutex(&g_con_lock);
    MIME_CONSOLE *c = find_run(request);
    if (!c) {
        ExReleaseFastMutex(&g_con_lock);
        return;
    }
    BOOLEAN ctrl_c = FALSE;
    ConHostInputLocked(c, data, length, &ctrl_c);
    ExReleaseFastMutex(&g_con_lock);
    if (ctrl_c) {
        ConAbortReads(c, FALSE);
        ConDispatchEvent(c, CTRL_C_EVENT, 0);
    }
    ConRelease(c);
}

void ConResize(MIME_CONSOLE *c, ULONG size)
{
    int cols = (int)(size & 0xFFFF), rows = (int)(size >> 16);
    if (!cols || !rows)
        return;
    ExAcquireFastMutex(&g_con_lock);
    CsTerminalResize(&c->Term, cols, rows);
    ConFlush(c);
    if (c->InputMode & ENABLE_WINDOW_INPUT) {
        CON_CHUNK *k = ConNewChunk(TRUE, 1);
        if (k) {
            MIME_CONDRV_RECORD *r = &k->Record[0];
            memset(r, 0, sizeof(*r));
            r->EventType = CON_WINDOW_BUFFER_SIZE_EVENT;
            CsBuffer *b = CsDisplayed(&c->Term);
            r->Value = (ULONG)(USHORT)(b ? b->Grid.Width : cols) | ((ULONG)(USHORT)(b ? b->Grid.Height : rows) << 16);
            k->Length = 1;
            ConQueueChunk(c, k, FALSE);
        }
    }
    ExReleaseFastMutex(&g_con_lock);
}

/*
 * a pseudo console starts like conhost's: the terminal is asked for win32 input and focus reports and the screen is
 * cleared, unless the creator asked to keep what the terminal shows, in which case the terminal is asked for its
 * cursor position.
 */
void ConPtyStart(MIME_CONSOLE *c, ULONG size, ULONG flags)
{
    ExAcquireFastMutex(&g_con_lock);
    c->PtyFlags = flags;
    ExReleaseFastMutex(&g_con_lock);
    ConResize(c, size);
    ExAcquireFastMutex(&g_con_lock);
    c->Term.Modes |= CS_MODE_WIN32_INPUT | CS_MODE_FOCUS;
    CsRender(&c->Term);
    if (flags & 1) {
        /* PSEUDOCONSOLE_INHERIT_CURSOR: the screen is the terminal's own until its cursor is known. */
        c->AwaitCursor = TRUE;
        CsEmit(&c->Term, "\x1b[6n");
    } else {
        CsEmit(&c->Term, "\x1b[?25l\x1b[2J\x1b[m\x1b[H\x1b[?25h");
    }
    CsShadowBlank(&c->Term);
    c->Term.HostKnown = TRUE;
    c->Term.HostX = c->Term.HostY = 0;
    ConFlush(c);
    ExReleaseFastMutex(&g_con_lock);
}

void ConsoleHostSize(ULONG64 request, ULONG size)
{
    ExAcquireFastMutex(&g_con_lock);
    MIME_CONSOLE *c = find_run(request);
    ExReleaseFastMutex(&g_con_lock);
    if (!c)
        return;
    ConResize(c, size);
    ConRelease(c);
}

/* ------------------------------------------------------------ ctrl events */

/*
 * runs the ctrl routine of every process attached to the console, or of one process group. a process that never
 * registered one ends at once.
 */
ULONG ConDispatchEvent(MIME_CONSOLE *c, ULONG event, ULONG group)
{
    PEPROCESS targets[MAX_TARGETS];
    PVOID routines[MAX_TARGETS];
    ULONG n = 0;
    ExAcquireFastMutex(&g_con_lock);
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
    ExReleaseFastMutex(&g_con_lock);
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

/* terminates the processes a closed console still holds after their grace time. */
static void NTAPI close_watchdog(PVOID context)
{
    MIME_CONSOLE *c = context;
    LARGE_INTEGER delay = {.QuadPart = -(LONGLONG)CLOSE_TIMEOUT_MS * 10000};
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    PEPROCESS targets[MAX_TARGETS];
    ULONG n = 0;
    ExAcquireFastMutex(&g_con_lock);
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead && n < MAX_TARGETS; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->Console == c && !p->Terminating && !p->Exited) {
            ObReferenceObject(p);
            targets[n++] = p;
        }
    }
    KiReleaseDispatcherLock(s);
    ExReleaseFastMutex(&g_con_lock);
    for (ULONG i = 0; i < n; i++) {
        PspTerminateProcess(targets[i], STATUS_CONTROL_C_EXIT);
        ObDereferenceObject(targets[i]);
    }
    ConRelease(c);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* the terminal is gone: release waiting readers, tell the programs and give them time. */
void ConClose(MIME_CONSOLE *c)
{
    ExAcquireFastMutex(&g_con_lock);
    c->Flags |= MIME_CONDRV_CLOSED;
    KeSetEvent(&c->InputReady, 0, FALSE);
    KeSetEvent(&c->PtyReady, 0, FALSE);
    KeSetEvent(&c->PtySpace, 0, FALSE);
    ConUpdateInputEvents(c);
    ExReleaseFastMutex(&g_con_lock);
    ULONG attached = ConDispatchEvent(c, CTRL_CLOSE_EVENT, 0);
    ConReference(c);
    if (!attached || !NT_SUCCESS(PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, close_watchdog, c)))
        ConRelease(c);
}

void ConsoleHostEvent(ULONG64 request, ULONG event)
{
    ExAcquireFastMutex(&g_con_lock);
    MIME_CONSOLE *c = find_run(request);
    ExReleaseFastMutex(&g_con_lock);
    if (!c)
        return;
    if (event == CTRL_CLOSE_EVENT) {
        ConClose(c);
        /* the run's reference goes with the terminal. */
        ConRelease(c);
    } else if (event <= CTRL_BREAK_EVENT) {
        if (event == CTRL_BREAK_EVENT)
            ConAbortReads(c, TRUE);
        ConDispatchEvent(c, event, 0);
    }
    ConRelease(c);
}

/* ------------------------------------------------------------ reads */

/* after a read, updates readiness and input events to match what is left and tells the host what was read. */
void ConAfterRead(MIME_CONSOLE *c, ULONG acked, BOOLEAN clear)
{
    BOOLEAN closed = (c->Flags & MIME_CONDRV_CLOSED) != 0;
    if (clear && !closed && IsListEmpty(&c->Input))
        KeClearEvent(&c->InputReady);
    ConUpdateInputEvents(c);
    send_ack(c, acked);
}

void ConFlushInput(MIME_CONSOLE *c)
{
    ULONG acked = 0;
    while (!IsListEmpty(&c->Input))
        ConFreeChunk(c, CONTAINING_RECORD(c->Input.Flink, CON_CHUNK, Links), &acked);
    acked += c->PendingAck;
    c->PendingAck = 0;
    c->CarryLength = 0;
    c->HasAnsiCarry = FALSE;
    ConUpdateInputEvents(c);
    send_ack(c, acked);
}

/* copies bytes of redirected input, consuming it unless peeking. stops before an end of file once data was copied. */
ULONG ConTakeInput(MIME_CONSOLE *c, PUCHAR out, ULONG length, BOOLEAN peek, BOOLEAN *eof, ULONG *acked)
{
    ULONG done = 0;
    *eof = FALSE;
    *acked = 0;
    PLIST_ENTRY e = c->Input.Flink;
    while (e != &c->Input && done < length) {
        CON_CHUNK *k = CONTAINING_RECORD(e, CON_CHUNK, Links);
        e = e->Flink;
        if (k->Records)
            break;
        if (k->Length == 0) {
            if (done)
                break;
            *eof = TRUE;
            if (!peek)
                ConFreeChunk(c, k, acked);
            break;
        }
        ULONG n = k->Length - k->First;
        if (n > length - done)
            n = length - done;
        memcpy(out + done, k->Data + k->First, n);
        done += n;
        if (peek)
            continue;
        k->First += n;
        c->InputBytes -= n;
        if (k->First == k->Length)
            ConFreeChunk(c, k, acked);
    }
    return done;
}

/* turns waiting bytes into records for a record reader, as a console reads its keyboard. */
static void bytes_to_records(MIME_CONSOLE *c)
{
    for (PLIST_ENTRY e = c->Input.Flink; e != &c->Input; e = e->Flink) {
        CON_CHUNK *k = CONTAINING_RECORD(e, CON_CHUNK, Links);
        ULONG length = k->Length - k->First;
        if (k->Records || !length)
            continue;
        CON_CHUNK *r = ConNewChunk(TRUE, 2 * length);
        if (!r)
            return;
        CON_DECODE decode = {};
        decode.Vt = (c->InputMode & CON_VIRTUAL_TERMINAL_INPUT) != 0;
        ConDecodeInput(&decode, k->Data + k->First, length, FALSE, r->Record, 2 * length, &r->Length);
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
ULONG ConTakeRecords(MIME_CONSOLE *c, MIME_CONDRV_RECORD *out, ULONG room, BOOLEAN peek, BOOLEAN *eof, ULONG *acked)
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
                ConFreeChunk(c, k, acked);
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
            ConFreeChunk(c, k, acked);
    }
    return done;
}

/* ------------------------------------------------------------ processes and attaching */

ULONG ConListProcesses(MIME_CONSOLE *c, PULONG out, ULONG max)
{
    ULONG total = 0;
    ExAcquireFastMutex(&g_con_lock);
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
    ExReleaseFastMutex(&g_con_lock);
    return total;
}

NTSTATUS ConAttachProcess(PEPROCESS self, ULONG pid)
{
    MIME_CONSOLE *c = nullptr;
    if (pid == ATTACH_PARENT_PROCESS) {
        /* the parent of a program `mime run` started is the terminal itself. */
        ExAcquireFastMutex(&g_con_lock);
        if (self->MimeRequestId)
            c = find_run(self->MimeRequestId);
        else if (self->ConsoleHint && !(self->ConsoleHint->Flags & MIME_CONDRV_CLOSED)) {
            c = self->ConsoleHint;
            c->References++;
        }
        ExReleaseFastMutex(&g_con_lock);
    } else {
        PEPROCESS other;
        if (NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &other))) {
            c = ConProcessConsole(other);
            ObDereferenceObject(other);
        } else {
            return STATUS_INVALID_PARAMETER;
        }
    }
    if (!c)
        return STATUS_INVALID_HANDLE;
    NTSTATUS status = ConsoleAttach(self, c);
    ConRelease(c);
    return status;
}

/*
 * windows makes a new console window for a process without a console, but the host has no window to give. a process
 * descending from a host run takes that run's terminal, and any other gets a console nothing shows, which programs
 * can still use and attach to.
 */
NTSTATUS ConAllocFor(PEPROCESS self, ULONG flags)
{
    MIME_CONSOLE *c = nullptr;
    ExAcquireFastMutex(&g_con_lock);
    if (self->Console) {
        ExReleaseFastMutex(&g_con_lock);
        return STATUS_ACCESS_DENIED;
    }
    if ((flags & MIME_CONDRV_ALLOC_RUN_TERMINAL) && !(flags & MIME_CONDRV_ALLOC_PREFER_NEW)) {
        if (self->MimeRequestId)
            c = find_run(self->MimeRequestId);
        else if (self->ConsoleHint && !(self->ConsoleHint->Flags & MIME_CONDRV_CLOSED)) {
            c = self->ConsoleHint;
            c->References++;
        }
    }
    ExReleaseFastMutex(&g_con_lock);
    BOOLEAN window = FALSE;
    if (!c && (flags & MIME_CONDRV_ALLOC_WINDOW) && self->SessionId != 0) {
        c = ConCreateConsole(CON_BACKEND_WINDOW, 0, MIME_CONDRV_TTY_INPUT | MIME_CONDRV_TTY_OUTPUT | MIME_CONDRV_TTY_ERROR, 0);
        window = c != nullptr;
    }
    if (!c)
        c = ConCreateConsole(CON_BACKEND_NONE, 0, MIME_CONDRV_TTY_INPUT | MIME_CONDRV_TTY_OUTPUT | MIME_CONDRV_TTY_ERROR, 0);
    if (!c)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = ConsoleAttach(self, c);
    if (window) {
        ConsoleInitialTitle(c, &self->ImagePath);
        if (NT_SUCCESS(status))
            ConsoleStartHost(c, self);
    }
    ConRelease(c);
    return status;
}

/* a console for a process that asks for a new one at creation, and wants no window. */
MIME_CONSOLE *ConsoleCreateNew(void)
{
    return ConCreateConsole(CON_BACKEND_NONE, 0, MIME_CONDRV_TTY_INPUT | MIME_CONDRV_TTY_OUTPUT | MIME_CONDRV_TTY_ERROR, 0);
}

#define STARTF_USECOUNTCHARS_ 0x8
#define STARTF_USEFILLATTRIBUTE_ 0x10

/* a console with a window, which a host process draws once it is started with ConsoleStartHost. */
MIME_CONSOLE *ConsoleCreateWindow(const CON_STARTUP *startup)
{
    MIME_CONSOLE *c = ConCreateConsole(CON_BACKEND_WINDOW, 0, MIME_CONDRV_TTY_INPUT | MIME_CONDRV_TTY_OUTPUT | MIME_CONDRV_TTY_ERROR, 0);
    if (!c)
        return nullptr;
    ExAcquireFastMutex(&g_con_lock);
    if (startup)
        c->Startup = *startup;
    CsBuffer *b = c->Main;
    if ((c->Startup.Flags & STARTF_USEFILLATTRIBUTE_)) {
        CsSetDefaultAttributes(b, (uint16_t)c->Startup.FillAttribute);
    }
    if ((c->Startup.Flags & STARTF_USECOUNTCHARS_) && c->Startup.CountCharsX && c->Startup.CountCharsY) {
        ULONG x = c->Startup.CountCharsX > 9999 ? 9999 : c->Startup.CountCharsX;
        ULONG y = c->Startup.CountCharsY > 9999 ? 9999 : c->Startup.CountCharsY;
        (void)CsResizeBuffer(b, (int)x, (int)y);
    }
    ExReleaseFastMutex(&g_con_lock);
    return c;
}

/* starts the host process of a console window for the process that created it. */
void ConsoleStartHost(MIME_CONSOLE *c, PEPROCESS parent)
{
    ULONG pid = 0;
    NTSTATUS status = PsStartConsoleHost(parent, c->Id, &pid);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("cannot start the console host: %08x", status);
        KeSetEvent(&c->HostReady, 0, FALSE);
    } else {
        c->HostPid = pid;
        c->HostWanted = TRUE;
    }
}

/*
 * what the window of a console decides, its largest size and its font, is not known before its host has attached. a
 * program that asks that early waits a moment for it, as it would find the host ready on Windows, and a host that does not
 * come stops being waited for.
 */
void ConWaitForHost(MIME_CONSOLE *c)
{
    if (c->Backend != CON_BACKEND_WINDOW)
        return;
    LARGE_INTEGER limit = {.QuadPart = -(LONGLONG)5000 * 10000};
    if (KeWaitForSingleObject(&c->HostReady, UserRequest, KernelMode, FALSE, &limit) == STATUS_TIMEOUT)
        KeSetEvent(&c->HostReady, 0, FALSE);
}

/* ------------------------------------------------------------ the window host */

MIME_CONSOLE *ConFindWindow(ULONG64 id)
{
    MIME_CONSOLE *found = nullptr;
    for (PLIST_ENTRY e = g_consoles.Flink; e != &g_consoles; e = e->Flink) {
        MIME_CONSOLE *c = CONTAINING_RECORD(e, MIME_CONSOLE, Links);
        if (c->Backend == CON_BACKEND_WINDOW && c->Id == id) {
            found = c;
            c->References++;
            break;
        }
    }
    return found;
}

/* the host has gone: the programs of the console lose their window. */
void ConHostDetached(MIME_CONSOLE *c)
{
    ExAcquireFastMutex(&g_con_lock);
    BOOLEAN ended = c->Ended;
    KeSetEvent(&c->HostReady, 0, FALSE);
    PKEVENT event = c->HostEvent;
    c->HostHandle = nullptr;
    c->HostEvent = nullptr;
    ExReleaseFastMutex(&g_con_lock);
    if (event)
        ObDereferenceObject(event);
    if (!ended)
        ConClose(c);
}

/* what the host needs to draw and to size its window. called with the lock held. */
void ConHostFillState(MIME_CONSOLE *c, MIME_CONDRV_HOST_STATE *st, BOOLEAN take)
{
    CsTerminal *t = &c->Term;
    memset(st, 0, sizeof(*st));
    CsBuffer *b = CsDisplayed(t);
    if (!b)
        b = c->Main;
    ConNotify(c);
    st->Changed = c->HostPending;
    if (take)
        c->HostPending = 0;
    st->Flags = c->Flags | (c->Ended ? MIME_CONDRV_CLOSED : 0);
    st->Generation = t->Generation;
    st->Size.X = (SHORT)b->Grid.Width;
    st->Size.Y = (SHORT)b->Grid.Height;
    st->Window.Left = b->Window.Left;
    st->Window.Top = b->Window.Top;
    st->Window.Right = b->Window.Right;
    st->Window.Bottom = b->Window.Bottom;
    st->Cursor.X = (SHORT)b->CursorX;
    st->Cursor.Y = (SHORT)b->CursorY;
    st->CursorSize = (ULONG)b->CursorSize;
    st->CursorVisible = b->CursorVisible;
    st->InputMode = ConGetInputMode(c);
    st->OutputMode = b->Mode;
    st->Scrolls = b->Scrolls;
    st->BellCount = t->BellCount;
    st->Attributes = b->Attr.Attr;
    st->PopupAttributes = b->PopupAttr;
    st->Alternate = b->Main != nullptr;
    st->QuickEdit = c->QuickEdit;
    st->InsertMode = c->InsertDefault;
    memcpy(st->ColorTable, b->ColorTable, sizeof(st->ColorTable));
    unsigned size, buffers, flags;
    ConHistoryGetInfo(c->Lines, &size, &buffers, &flags);
    st->HistoryBufferSize = size;
    st->HistoryBuffers = buffers;
    st->HistoryFlags = flags;
    st->StartupFlags = c->Startup.Flags;
    st->ShowWindow = c->Startup.ShowWindow;
    st->StartX = c->Startup.X;
    st->StartY = c->Startup.Y;
    st->SizeX = c->Startup.SizeX;
    st->SizeY = c->Startup.SizeY;
    ULONG n = (ULONG)wcslen(c->Title);
    st->TitleLength = n;
    memcpy(st->Title, c->Title, n * sizeof(WCHAR));
}

/*
 * input a window made: keys, the mouse, focus. Ctrl+C is the event it stands for when the input is processed, Ctrl+Break
 * always is, and neither is a record then. the lock is not held.
 */
void ConHostRecords(MIME_CONSOLE *c, MIME_CONDRV_RECORD *r, ULONG count)
{
    BOOLEAN ctrl_c = FALSE, ctrl_break = FALSE;
    ULONG kept = 0;
    ExAcquireFastMutex(&g_con_lock);
    BOOLEAN processed = (c->InputMode & ENABLE_PROCESSED_INPUT) != 0;
    BOOLEAN mouse = (c->InputMode & ENABLE_MOUSE_INPUT) && !c->QuickEdit;
    BOOLEAN window = (c->InputMode & ENABLE_WINDOW_INPUT) != 0;
    for (ULONG i = 0; i < count; i++) {
        MIME_CONDRV_RECORD *k = &r[i];
        if (k->EventType == CON_KEY_EVENT) {
            BOOLEAN ctrl = (k->Key.ControlKeyState & (CON_LEFT_CTRL | CON_RIGHT_CTRL)) != 0;
            if (ctrl && k->Key.VirtualKey == 3 /* VK_CANCEL */) {
                if (k->Key.KeyDown)
                    ctrl_break = TRUE;
                continue;
            }
            if (processed && ctrl && k->Key.VirtualKey == 'C' && k->Key.Char == 3 &&
                !(k->Key.ControlKeyState & (CON_LEFT_ALT | CON_RIGHT_ALT))) {
                if (k->Key.KeyDown)
                    ctrl_c = TRUE;
                continue;
            }
        } else if (k->EventType == CON_MOUSE_EVENT) {
            if (!mouse)
                continue;
        } else if (k->EventType == CON_FOCUS_EVENT) {
            if (!window)
                continue;
        } else {
            continue;
        }
        r[kept++] = *k;
    }
    if (kept) {
        ULONG written;
        ConWriteRecords(c, r, kept, FALSE, &written);
    }
    ExReleaseFastMutex(&g_con_lock);
    if (ctrl_break) {
        ConAbortReads(c, TRUE);
        ConDispatchEvent(c, CTRL_BREAK_EVENT, 0);
    } else if (ctrl_c) {
        ConAbortReads(c, FALSE);
        ConDispatchEvent(c, CTRL_C_EVENT, 0);
    }
}

/* the choices of the properties dialog. */
NTSTATUS ConHostSettings(MIME_CONSOLE *c, const MIME_CONDRV_HOST_SETTINGS *s)
{
    NTSTATUS status = STATUS_SUCCESS;
    ExAcquireFastMutex(&g_con_lock);
    CsBuffer *b = CsDisplayed(&c->Term);
    if (!b)
        b = c->Main;
    if (s->Set & 1) {
        CsInfo info;
        CsGetInfo(b, &info);
        memcpy(info.ColorTable, s->ColorTable, sizeof(info.ColorTable));
        info.Attributes = s->Attributes;
        info.PopupAttributes = s->PopupAttributes;
        if (CsSetInfo(b, &info) != CS_OK)
            status = STATUS_INVALID_PARAMETER;
        memcpy(c->Main->ColorTable, s->ColorTable, sizeof(c->Main->ColorTable));
        c->HostPending |= MIME_CONDRV_CHANGED_PALETTE;
    }
    if (s->Set & 2) {
        c->QuickEdit = s->QuickEdit != 0;
        c->InsertDefault = s->Insert != 0;
        apply_mouse(c);
        c->HostPending |= MIME_CONDRV_CHANGED_MODE;
    }
    if ((s->Set & 4) && !ConHistorySetInfo(c->Lines, s->HistoryBufferSize, s->HistoryBuffers, s->HistoryFlags))
        status = STATUS_INVALID_PARAMETER;
    if ((s->Set & 8) && CsSetSize(b, (CsCoord){s->Size.X, s->Size.Y}) != CS_OK)
        status = STATUS_INVALID_PARAMETER;
    ConFlush(c);
    ExReleaseFastMutex(&g_con_lock);
    return status;
}

/* the user sized or scrolled the window. */
void ConHostWindow(MIME_CONSOLE *c, const MIME_CONDRV_HOST_WINDOW *w)
{
    ExAcquireFastMutex(&g_con_lock);
    CsTerminal *t = &c->Term;
    if (w->Op == MIME_CONDRV_WINDOW_RESIZE) {
        CsWindowResize(t, w->Columns, w->Rows);
        CsBuffer *b = CsDisplayed(t);
        if ((c->InputMode & ENABLE_WINDOW_INPUT) && b) {
            CON_CHUNK *k = ConNewChunk(TRUE, 1);
            if (k) {
                MIME_CONDRV_RECORD *r = &k->Record[0];
                memset(r, 0, sizeof(*r));
                r->EventType = CON_WINDOW_BUFFER_SIZE_EVENT;
                r->Value = (ULONG)(USHORT)b->Grid.Width | ((ULONG)(USHORT)b->Grid.Height << 16);
                k->Length = 1;
                ConQueueChunk(c, k, FALSE);
            }
        }
    } else if (w->Op == MIME_CONDRV_WINDOW_SCROLL) {
        CsWindowScroll(t, w->Left, w->Top);
    }
    ConFlush(c);
    ExReleaseFastMutex(&g_con_lock);
}

void IopInitializeConsole(void)
{
    ExInitializeFastMutex(&g_con_lock);
    InitializeListHead(&g_consoles);
    IopInitializeConsoleDevice();
}
