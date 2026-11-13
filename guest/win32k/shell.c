/*
 * host integration for toasts, dialogs, window identity and launcher entries. combase, explorerframe and
 * OptionalFeatures.exe submit NtUserMimeShell
 * commands. the kernel assigns toast ids, forwards MIME_HC_SHELL commands, and queues MIME_INPUT_SHELL replies for
 * the creating process, whose notification thread waits here.
 */
#include "win32k.h"

#define OWNER_MAX 1024
#define EVENT_MAX 256

typedef struct SHELL_OWNER {
    ULONG Id;
    ULONG Pid;
} SHELL_OWNER;

typedef struct SHELL_QUEUED {
    BOOLEAN Used;
    ULONG Pid;
    MIME_SHELL_EVENT Event;
} SHELL_QUEUED;

typedef union SHELL_COMMAND {
    ULONG Op;
    MimeShellToast Toast;
    MimeShellWindow Window;
    MimeShellJumpList Jump;
    MimeShellDialog Dialog;
    MimeShellFolder Folder;
    MimeShellUrl Url;
    MimeShellProtocols Protocols;
} SHELL_COMMAND;

static KSPIN_LOCK g_lock;
static KEVENT g_event;
static BOOLEAN g_ready;
static SHELL_OWNER g_owners[OWNER_MAX];
static ULONG g_owner_next;
static SHELL_QUEUED g_queue[EVENT_MAX];
static ULONG g_next_id;
static SHELL_COMMAND *g_command;

void ShellInitialize(void)
{
    KeInitializeSpinLock(&g_lock);
    KeInitializeEvent(&g_event, NotificationEvent, FALSE);
    g_ready = TRUE;
}

static NTSTATUS from_host(ULONG64 status)
{
    switch (status) {
    case MIME_HC_STATUS_OK:
        return STATUS_SUCCESS;
    case MIME_HC_STATUS_INVALID:
        return STATUS_INVALID_PARAMETER;
    case MIME_HC_STATUS_NOT_FOUND:
        return STATUS_NOT_FOUND;
    case MIME_HC_STATUS_NO_MEMORY:
        return STATUS_INSUFFICIENT_RESOURCES;
    case MIME_HC_STATUS_UNSUPPORTED:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_IO_DEVICE_ERROR;
    }
}

static ULONG current_pid(void)
{
    W32PROCESS *p = W32GetProcess();
    return p ? (ULONG)(ULONG_PTR)p->Pid : 0;
}

static void owner_set(ULONG id, ULONG pid)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    /* the ring evicts the oldest toast when full. */
    g_owners[g_owner_next % OWNER_MAX].Id = id;
    g_owners[g_owner_next % OWNER_MAX].Pid = pid;
    g_owner_next++;
    KeReleaseSpinLock(&g_lock, irql);
}

static ULONG owner_of(ULONG id)
{
    ULONG pid = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    for (ULONG i = 0; i < OWNER_MAX; i++)
        if (g_owners[i].Id == id) {
            pid = g_owners[i].Pid;
            break;
        }
    KeReleaseSpinLock(&g_lock, irql);
    return pid;
}

static void owner_forget(ULONG id)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    for (ULONG i = 0; i < OWNER_MAX; i++)
        if (g_owners[i].Id == id)
            g_owners[i].Id = 0;
    KeReleaseSpinLock(&g_lock, irql);
}

/* route toast events to the process that showed it. */
void ShellHostEvent(const MimeInputEvent *ev)
{
    if (!g_ready)
        return;
    ULONG pid = owner_of(ev->pad);
    if (!pid)
        return;
    MIME_SHELL_EVENT e = {};
    e.Kind = ev->flags;
    e.Id = ev->pad;
    e.Arg = ev->x;
    e.ReplyLength = (ULONG)ev->y;
    if (e.Kind == MIME_SHELL_EVENT_TOAST_REPLIED && e.ReplyLength) {
        /* the text waits on the host. */
        PHYSICAL_ADDRESS highest;
        highest.QuadPart = -1;
        MimeShellToast *c = MmAllocateContiguousMemory(sizeof(*c), highest);
        if (c) {
            memset(c, 0, sizeof(*c));
            c->op = MIME_SHELL_TOAST_REPLY;
            c->id = e.Id;
            ULONG64 got = 0;
            if (HalMimeHypercall(MIME_HC_SHELL, (ULONG64)MmGetPhysicalAddress(c).QuadPart, sizeof(*c), 0, 0, 0, 0,
                                 &got) == MIME_HC_STATUS_OK) {
                ULONG n = (ULONG)got < RTL_NUMBER_OF(e.Reply) - 1 ? (ULONG)got : RTL_NUMBER_OF(e.Reply) - 1;
                memcpy(e.Reply, c->body, n * sizeof(USHORT));
                e.ReplyLength = n;
            } else {
                e.ReplyLength = 0;
            }
            MmFreeContiguousMemory(c);
        } else {
            e.ReplyLength = 0;
        }
    }
    BOOLEAN done = e.Kind == MIME_SHELL_EVENT_TOAST_DISMISSED || e.Kind == MIME_SHELL_EVENT_TOAST_FAILED ||
                   e.Kind == MIME_SHELL_EVENT_DIALOG_ANSWERED;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    SHELL_QUEUED *slot = nullptr;
    for (ULONG i = 0; i < EVENT_MAX && !slot; i++)
        if (!g_queue[i].Used)
            slot = &g_queue[i];
    if (slot) {
        slot->Used = TRUE;
        slot->Pid = pid;
        slot->Event = e;
    }
    KeReleaseSpinLock(&g_lock, irql);
    if (done)
        owner_forget(ev->pad);
    KeSetEvent(&g_event, IO_NO_INCREMENT, FALSE);
}

/* discard toast events when their process exits. */
void ShellFreeProcess(W32PROCESS *p)
{
    if (!g_ready)
        return;
    ULONG pid = (ULONG)(ULONG_PTR)p->Pid;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    for (ULONG i = 0; i < OWNER_MAX; i++)
        if (g_owners[i].Pid == pid)
            g_owners[i].Id = 0;
    for (ULONG i = 0; i < EVENT_MAX; i++)
        if (g_queue[i].Used && g_queue[i].Pid == pid)
            g_queue[i].Used = FALSE;
    KeReleaseSpinLock(&g_lock, irql);
}

static LONG wait_event(ULONG timeout_ms, PUCHAR out)
{
    ULONG pid = current_pid();
    ULONGLONG deadline = KeQueryInterruptTime() + (ULONGLONG)timeout_ms * 10000;
    for (;;) {
        MIME_SHELL_EVENT e;
        BOOLEAN found = FALSE;
        KIRQL irql;
        KeClearEvent(&g_event);
        KeAcquireSpinLock(&g_lock, &irql);
        for (ULONG i = 0; i < EVENT_MAX && !found; i++)
            if (g_queue[i].Used && g_queue[i].Pid == pid) {
                e = g_queue[i].Event;
                g_queue[i].Used = FALSE;
                found = TRUE;
            }
        KeReleaseSpinLock(&g_lock, irql);
        if (found)
            return NT_SUCCESS(MimeCopyToUser(out, &e, sizeof(e))) ? 1 : (LONG)STATUS_ACCESS_VIOLATION;
        LARGE_INTEGER t;
        if (timeout_ms == 0xFFFFFFFF) {
            t.QuadPart = -100000000; /* ten seconds, looking again for a thread that was alerted. */
        } else {
            ULONGLONG now = KeQueryInterruptTime();
            if (now >= deadline)
                return 0;
            t.QuadPart = -(LONGLONG)(deadline - now);
        }
        NTSTATUS s = KeWaitForSingleObject(&g_event, UserRequest, UserMode, TRUE, &t);
        /* the wait must return APCs, alerts and thread termination to the caller. */
        if (s != STATUS_SUCCESS && s != STATUS_TIMEOUT)
            return (LONG)s;
    }
}

static void zero_end(USHORT *text, ULONG chars)
{
    text[chars - 1] = 0;
}

/* validate the command, add kernel-owned data and forward it to the host. */
static LONG run_command(PUCHAR request, ULONG size)
{
    ULONG op;
    if (size < sizeof(op) || !NT_SUCCESS(MimeCopyFromUser(&op, request, sizeof(op))))
        return STATUS_INVALID_PARAMETER;
    ULONG want;
    switch (op) {
    case MIME_SHELL_TOAST_SHOW:
    case MIME_SHELL_TOAST_HIDE:
    case MIME_SHELL_TOAST_REMOVE:
    case MIME_SHELL_TOAST_PROBE:
        want = sizeof(MimeShellToast);
        break;
    case MIME_SHELL_WINDOW:
    case MIME_SHELL_LAUNCHER:
    case MIME_SHELL_FLASH:
        want = sizeof(MimeShellWindow);
        break;
    case MIME_SHELL_JUMPLIST:
        want = sizeof(MimeShellJumpList);
        break;
    case MIME_SHELL_DIALOG:
        want = sizeof(MimeShellDialog);
        break;
    case MIME_SHELL_FOLDER:
        want = sizeof(MimeShellFolder);
        break;
    case MIME_SHELL_URL:
        want = sizeof(MimeShellUrl);
        break;
    case MIME_SHELL_PROTOCOLS:
        want = sizeof(MimeShellProtocols);
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }
    if (size != want)
        return STATUS_INVALID_PARAMETER;
    if (!g_command) {
        PHYSICAL_ADDRESS highest;
        highest.QuadPart = -1;
        g_command = MmAllocateContiguousMemory(sizeof(*g_command), highest);
        if (!g_command)
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    LONG result = STATUS_SUCCESS;
    UserEnter();
    memset(g_command, 0, sizeof(*g_command));
    if (!NT_SUCCESS(MimeCopyFromUser(g_command, request, size))) {
        UserLeave();
        return STATUS_ACCESS_VIOLATION;
    }
    ULONG pid = current_pid();
    ULONG id = 0;
    if (want == sizeof(MimeShellToast)) {
        MimeShellToast *t = &g_command->Toast;
        zero_end(t->aumid, RTL_NUMBER_OF(t->aumid));
        zero_end(t->app, RTL_NUMBER_OF(t->app));
        zero_end(t->tag, RTL_NUMBER_OF(t->tag));
        zero_end(t->group, RTL_NUMBER_OF(t->group));
        zero_end(t->title, RTL_NUMBER_OF(t->title));
        zero_end(t->body, RTL_NUMBER_OF(t->body));
        zero_end(t->logo, RTL_NUMBER_OF(t->logo));
        zero_end(t->hero, RTL_NUMBER_OF(t->hero));
        zero_end(t->picture, RTL_NUMBER_OF(t->picture));
        zero_end(t->placeholder, RTL_NUMBER_OF(t->placeholder));
        if (t->action_count > MIME_TOAST_ACTIONS)
            t->action_count = MIME_TOAST_ACTIONS;
        for (ULONG i = 0; i < MIME_TOAST_ACTIONS; i++)
            zero_end(t->actions[i].label, RTL_NUMBER_OF(t->actions[i].label));
        t->process_id = pid;
        if (op == MIME_SHELL_TOAST_SHOW) {
            id = ++g_next_id;
            if (!id)
                id = ++g_next_id;
            t->id = id;
            owner_set(id, pid);
        } else if (op == MIME_SHELL_TOAST_HIDE && owner_of(t->id) != pid) {
            UserLeave();
            return STATUS_NOT_FOUND;
        }
    } else if (op == MIME_SHELL_DIALOG) {
        MimeShellDialog *d = &g_command->Dialog;
        zero_end(d->title, RTL_NUMBER_OF(d->title));
        zero_end(d->instruction, RTL_NUMBER_OF(d->instruction));
        zero_end(d->content, RTL_NUMBER_OF(d->content));
        if (d->button_count > MIME_DIALOG_BUTTONS)
            d->button_count = MIME_DIALOG_BUTTONS;
        for (ULONG i = 0; i < MIME_DIALOG_BUTTONS; i++)
            zero_end(d->buttons[i], RTL_NUMBER_OF(d->buttons[i]));
        d->process_id = pid;
        if (d->kind == MIME_DIALOG_UPDATE || d->kind == MIME_DIALOG_END) {
            if (owner_of(d->id) != pid) {
                UserLeave();
                return STATUS_NOT_FOUND;
            }
        } else if (d->kind >= MIME_DIALOG_ASK && d->kind <= MIME_DIALOG_PROGRESS) {
            id = ++g_next_id;
            if (!id)
                id = ++g_next_id;
            d->id = id;
            owner_set(id, pid);
        } else {
            UserLeave();
            return STATUS_INVALID_PARAMETER;
        }
    } else if (op == MIME_SHELL_FOLDER) {
        MimeShellFolder *f = &g_command->Folder;
        zero_end(f->path, RTL_NUMBER_OF(f->path));
        f->process_id = pid;
    } else if (op == MIME_SHELL_URL) {
        MimeShellUrl *u = &g_command->Url;
        zero_end(u->url, RTL_NUMBER_OF(u->url));
        u->process_id = pid;
    } else if (op == MIME_SHELL_PROTOCOLS) {
        MimeShellProtocols *p = &g_command->Protocols;
        if (p->count > MIME_PROTOCOLS)
            p->count = MIME_PROTOCOLS;
        for (ULONG i = 0; i < MIME_PROTOCOLS; i++) {
            zero_end(p->scheme[i], RTL_NUMBER_OF(p->scheme[i]));
            zero_end(p->program[i], RTL_NUMBER_OF(p->program[i]));
            zero_end(p->name[i], RTL_NUMBER_OF(p->name[i]));
        }
        p->process_id = pid;
    } else if (want == sizeof(MimeShellJumpList)) {
        MimeShellJumpList *j = &g_command->Jump;
        zero_end(j->aumid, RTL_NUMBER_OF(j->aumid));
        if (j->task_count > MIME_JUMPLIST_TASKS)
            j->task_count = MIME_JUMPLIST_TASKS;
        for (ULONG i = 0; i < MIME_JUMPLIST_TASKS; i++) {
            zero_end(j->tasks[i].title, RTL_NUMBER_OF(j->tasks[i].title));
            zero_end(j->tasks[i].path, RTL_NUMBER_OF(j->tasks[i].path));
            zero_end(j->tasks[i].arguments, RTL_NUMBER_OF(j->tasks[i].arguments));
            zero_end(j->tasks[i].directory, RTL_NUMBER_OF(j->tasks[i].directory));
            zero_end(j->tasks[i].icon, RTL_NUMBER_OF(j->tasks[i].icon));
        }
        j->process_id = pid;
    } else {
        MimeShellWindow *w = &g_command->Window;
        zero_end(w->aumid, RTL_NUMBER_OF(w->aumid));
        w->process_id = pid;
        WND *wnd = w->window_id ? WndLookup((HWND)(ULONG_PTR)w->window_id) : nullptr;
        if (w->window_id && (!wnd || !wnd->Thread || !wnd->Thread->Process || wnd->Thread->Process->Pid != (HANDLE)(ULONG_PTR)pid)) {
            UserLeave();
            return STATUS_INVALID_HANDLE;
        }
    }
    ULONG64 value = 0;
    ULONG64 status = HalMimeHypercall(MIME_HC_SHELL, (ULONG64)MmGetPhysicalAddress(g_command).QuadPart, size, 0, 0, 0,
                                      0, &value);
    result = (LONG)from_host(status);
    if (NT_SUCCESS(result)) {
        if (op == MIME_SHELL_TOAST_SHOW || (op == MIME_SHELL_DIALOG && id))
            result = (LONG)id;
        else if (op == MIME_SHELL_TOAST_PROBE)
            result = (LONG)value;
        if (op == MIME_SHELL_DIALOG && g_command->Dialog.kind == MIME_DIALOG_END)
            owner_forget(g_command->Dialog.id);
    } else if (op == MIME_SHELL_TOAST_SHOW || (op == MIME_SHELL_DIALOG && id)) {
        owner_forget(id);
    }
    UserLeave();
    return result;
}

LONG NTAPI NtUserMimeShell(ULONG op, ULONG id, PUCHAR request, ULONG size)
{
    switch (op) {
    case MIME_SHELLOP_COMMAND:
        return run_command(request, size);
    case MIME_SHELLOP_WAIT:
        if (size != sizeof(MIME_SHELL_EVENT))
            return STATUS_INVALID_PARAMETER;
        return wait_event(id, request);
    default:
        return STATUS_INVALID_PARAMETER;
    }
}
