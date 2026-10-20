/*
 * the consoles of the console driver, \Device\ConDrv, see condev.c for the
 * device programs open. a console has an input buffer of records, a
 * terminal with its screen buffers (conscreen.c) and a backend that carries
 * the terminal's bytes: the terminal of a host run, a pseudo console, or
 * nothing. a console program is attached to the console it is created in; a
 * gui program is not, but may attach with AttachConsole or AllocConsole.
 *
 * input the host sends waits here until a process reads it, writes go to
 * the screen buffers and from there to the terminal, and a ctrl event
 * starts the ctrl routine kernelbase registered on a new thread of each
 * attached process, as csrss does on windows.
 *
 * when the terminal goes away the processes get CTRL_CLOSE_EVENT and five
 * seconds to exit before they are terminated.
 */
#include "console_p.h"

FAST_MUTEX g_con_lock;
static LIST_ENTRY g_consoles;

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

/* the console of a process with a reference, or NULL. */
MIME_CONSOLE *ConProcessConsole(PEPROCESS p)
{
    ExAcquireFastMutex(&g_con_lock);
    MIME_CONSOLE *c = p->Console;
    if (c)
        c->References++;
    ExReleaseFastMutex(&g_con_lock);
    return c;
}

/* the console of a host run with a reference, or NULL. called with the lock held. */
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

/* the size of a new console: columns | rows << 16, or the usual window of a console. */
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
    int cols = size & 0xFFFF ? (int)(size & 0xFFFF) : 80;
    int rows = size >> 16 ? (int)(size >> 16) : 25;
    c->Lines = ConLineStateCreate();
    if (!c->Lines || !CsTerminalInit(&c->Term, cols, rows)) {
        ConLineStateDestroy(c->Lines);
        ExFreePool(c);
        return nullptr;
    }
    c->Term.Headless = backend == CON_BACKEND_NONE || (backend == CON_BACKEND_RUN && !(c->Flags & (MIME_CONDRV_TTY_OUTPUT | MIME_CONDRV_TTY_ERROR)));
    if (c->Term.Headless)
        c->Flags |= MIME_CONDRV_HEADLESS;
    if (backend == CON_BACKEND_PTY)
        c->Flags |= MIME_CONDRV_PTY_HOST;
    c->Main = CsBufferCreate(&c->Term, cols, rows);
    if (!c->Main) {
        CsTerminalDestroy(&c->Term);
        ConLineStateDestroy(c->Lines);
        ExFreePool(c);
        return nullptr;
    }
    CsShow(&c->Term, c->Main);
    c->Main->Refs = 1;
    ExAcquireFastMutex(&g_con_lock);
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
    if (!p->Console) {
        p->Console = c;
        p->ConsoleGroup = (ULONG)(ULONG_PTR)p->UniqueProcessId;
        c->References++;
        status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&g_con_lock);
    return status;
}

void ConsoleDetach(PEPROCESS p)
{
    ExAcquireFastMutex(&g_con_lock);
    MIME_CONSOLE *c = p->Console;
    p->Console = nullptr;
    /* a pseudo console that is hung up waits for the last process to leave. */
    if (c && c->Backend == CON_BACKEND_PTY)
        KeSetEvent(&c->PtyReady, 0, FALSE);
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

/* the title a console starts with: the first program's, which its window shows until a program sets another. */
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

/* the input handles are signaled while input waits, which makes them waitable. */
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

/* characters typed by nobody, as a terminal would answer: the reports of the escape sequences. */
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

/* the bytes the terminal needs, and what it answers with that the program reads as input. called with the lock held. */
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
}

void ConSetTitle(MIME_CONSOLE *c, const WCHAR *title, ULONG count)
{
    if (count > RTL_NUMBER_OF(c->Title) - 1)
        count = RTL_NUMBER_OF(c->Title) - 1;
    memcpy(c->Title, title, count * sizeof(WCHAR));
    c->Title[count] = 0;
    c->TitleSet = TRUE;
    CsSetTitle(&c->Term, title, count);
    ConFlush(c);
}

/* the mouse reports the input mode asks the terminal for: while the program has the mouse and quick edit does not. */
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
 * the mode is stored, and the flags of the extended bits are taken from it,
 * before it is checked: a mode that is refused has changed the console.
 */
NTSTATUS ConSetInputMode(MIME_CONSOLE *c, ULONG mode)
{
    c->InputMode = mode & ~(ULONG)INPUT_MODE_EXTENDED;
    if (mode & INPUT_MODE_EXTENDED) {
        c->QuickEdit = (mode & ENABLE_QUICK_EDIT_MODE) != 0;
        c->AutoPosition = (mode & ENABLE_AUTO_POSITION) != 0;
        c->InsertDefault = (mode & ENABLE_INSERT_MODE) != 0;
        c->ExtendedGiven = TRUE;
    } else {
        c->ExtendedGiven = FALSE;
    }
    apply_mouse(c);
    if ((mode & ~(ULONG)INPUT_MODE_MASK) || ((mode & ENABLE_ECHO_INPUT) && !(mode & ENABLE_LINE_INPUT)))
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

/* removes the ctrl+c presses of processed input from decoded records; returns the records left. */
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

/* typed input decoded into keys, or the bytes of a redirected input, with the lock held. */
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

/* typed or piped input, an end of file when empty. */
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
 * a pseudo console starts as conhost's does: the terminal is asked for the keys that carry win32 input and
 * the focus, and the screen is cleared, unless the program that made it asked to keep what the terminal
 * shows, and then the terminal is asked where its cursor is.
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
 * starts the ctrl routine of every process attached to the console, or of
 * one process group; a process that never registered one ends at once.
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

/* the processes a closed console still holds after their grace time end. */
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

/* the terminal of the console is gone: waiting readers are released, the programs are told and given time. */
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

/* after a read: the readiness and the input events follow what is left, and the host learns what was read. */
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

/* turns the bytes waiting into records for a reader of records, as a console reads its keyboard. */
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
 * windows makes a new console window for a process without a console. the
 * host has no window to give: a process that descends from a host run takes
 * the terminal of that run, as the program of the run does, and any other
 * gets a console that nothing shows, which programs can still use and
 * others attach to.
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
    if (!c)
        c = ConCreateConsole(CON_BACKEND_NONE, 0, MIME_CONDRV_TTY_INPUT | MIME_CONDRV_TTY_OUTPUT | MIME_CONDRV_TTY_ERROR, 0);
    if (!c)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = ConsoleAttach(self, c);
    ConRelease(c);
    return status;
}

/* a console for a process that asks for a new one at creation. */
MIME_CONSOLE *ConsoleCreateNew(void)
{
    return ConCreateConsole(CON_BACKEND_NONE, 0, MIME_CONDRV_TTY_INPUT | MIME_CONDRV_TTY_OUTPUT | MIME_CONDRV_TTY_ERROR, 0);
}

void IopInitializeConsole(void)
{
    ExInitializeFastMutex(&g_con_lock);
    InitializeListHead(&g_consoles);
    IopInitializeConsoleDevice();
}
