/*
 * drag and drop with host programs. a host drag over a guest window comes as input events: the window under it, or
 * its nearest ancestor that takes drops, gets WM_MIME_HOSTDND posted to its thread, where user32 and ole32 run the
 * IDropTarget or the legacy WM_DROPFILES. nothing is read from the host until the target asks for a format. the
 * other way round, DoDragDrop starts a host drag when the drag leaves its window, and the host's data requests and
 * the end of the drag reach that thread as the same message.
 *
 * one host drag at a time, like one pointer. the session is the host's id for it.
 */
#include "win32k.h"

#define HOST_CHUNK 65536
#ifndef MK_ALT
#define MK_ALT 0x0020
#endif
#define DROPEFFECT_NONE 0
#define MAX_REQUESTS 16
#define NO_RESULT MIME_DND_GOING

typedef struct DND {
    /* a host drag over the guest's windows. */
    BOOLEAN Active;
    BOOLEAN Dropped;
    ULONG Id;
    ULONG Kinds;
    ULONG Allowed;
    ULONG Keys;
    POINT Pt;
    HWND Target;      /* the window that got the enter, if any. */
    BOOLEAN Files;    /* the target only takes WM_DROPFILES. */
    BOOLEAN Entered;
    /* the guest's drag, taken over by the host. */
    ULONG SourceId;
    W32THREAD *SourceThread;
    ULONG_PTR SourceHandler;
    ULONG SourceResult;
    PVOID Icon;       /* the image the next host drag shows, in memory the host can read. */
    ULONG IconBytes;
    struct {
        BOOLEAN Used;
        ULONG Kind, Id;
    } Requests[MAX_REQUESTS];
} DND;

static DND g_dnd;

static ULONG keys_of(ULONG modifiers)
{
    ULONG keys = MK_LBUTTON;
    if (modifiers & MIME_MOD_SHIFT)
        keys |= MK_SHIFT;
    if (modifiers & MIME_MOD_CONTROL)
        keys |= MK_CONTROL;
    if (modifiers & MIME_MOD_ALT)
        keys |= MK_ALT;
    return keys;
}

/* the deepest window at the point, or its nearest ancestor that registered. */
static WND *find_target(WND *top, POINT pt, BOOLEAN *files)
{
    WND *desktop = WndDesktop();
    DpiPixelConvert(&pt, DPI_AWARE_SYSTEM, top->Awareness);
    for (WND *w = WndFromPoint(top, pt); w && w != desktop; w = w->Parent) {
        if (w->Style & WS_DISABLED)
            return nullptr;
        if (w->Flags & WNDF_DROPTARGET) {
            *files = FALSE;
            return w;
        }
        if (w->ExStyle & WS_EX_ACCEPTFILES) {
            *files = TRUE;
            return w;
        }
    }
    return nullptr;
}

static void post_to_window(WND *w, BOOLEAN files, ULONG phase)
{
    W32THREAD *t = w->Thread;
    ULONG_PTR handler = files || !t || !t->Process ? 0 : t->Process->DndHandler;
    if (!files && !handler)
        return;
    QueuePost(t, WndHandle(w), WM_MIME_HOSTDND, handler, (LPARAM)(phase | g_dnd.Id << 4));
}

static void host_effect(ULONG effect)
{
    HalMimeHypercall(MIME_HC_DND_EFFECT, g_dnd.Id, effect, 0, 0, 0, 0, nullptr);
}

static void leave_target(void)
{
    WND *w = g_dnd.Entered ? WndLookup(g_dnd.Target) : nullptr;
    if (w)
        post_to_window(w, g_dnd.Files, MIME_DND_PHASE_LEAVE);
    g_dnd.Target = nullptr;
    g_dnd.Entered = FALSE;
}

/* tells the window under the drag, and the one it left. */
static void host_over(const MimeInputEvent *ev, BOOLEAN enter)
{
    if (enter) {
        if (g_dnd.Active && g_dnd.Entered)
            leave_target();
        memset(&g_dnd, 0, offsetof(DND, SourceId));
        g_dnd.Active = TRUE;
        g_dnd.Id = ev->pad;
        g_dnd.Kinds = ev->flags;
        g_dnd.Allowed = (ULONG)ev->wheel;
    } else if (!g_dnd.Active || g_dnd.Id != ev->pad || g_dnd.Dropped) {
        return;
    }
    g_dnd.Keys = keys_of(ev->modifiers);
    g_dnd.Pt = (POINT){ev->x, ev->y};
    WND *top = WndLookup((HWND)(ULONG_PTR)ev->window_id);
    BOOLEAN files = FALSE;
    WND *target = top ? find_target(top, g_dnd.Pt, &files) : nullptr;
    HWND handle = target ? WndHandle(target) : nullptr;
    if (handle != g_dnd.Target) {
        if (g_dnd.Entered)
            leave_target();
        g_dnd.Target = handle;
        g_dnd.Files = files;
    }
    if (!target) {
        host_effect(DROPEFFECT_NONE);
        return;
    }
    post_to_window(target, files, g_dnd.Entered ? MIME_DND_PHASE_OVER : MIME_DND_PHASE_ENTER);
    g_dnd.Entered = TRUE;
}

static void source_request(const MimeInputEvent *ev)
{
    W32THREAD *t = g_dnd.SourceThread;
    for (ULONG i = 0; t && i < MAX_REQUESTS; i++) {
        if (g_dnd.Requests[i].Used)
            continue;
        g_dnd.Requests[i].Used = TRUE;
        g_dnd.Requests[i].Kind = (ULONG)ev->x;
        g_dnd.Requests[i].Id = ev->button;
        if (QueuePost(t, nullptr, WM_MIME_HOSTDND, g_dnd.SourceHandler, (LPARAM)(MIME_DND_PHASE_REQUEST | i << 4)))
            return;
        g_dnd.Requests[i].Used = FALSE;
        break;
    }
    /* nobody to ask: the host program gets nothing. */
    HalMimeHypercall(MIME_HC_CLIP_PROVIDE, ev->button, 0, 0, 0, MIME_CLIP_NONE, 0, nullptr);
}

void DndHostEvent(const MimeInputEvent *ev)
{
    switch (ev->type) {
    case MIME_INPUT_DND_ENTER:
        host_over(ev, TRUE);
        break;
    case MIME_INPUT_DND_OVER:
        host_over(ev, FALSE);
        break;
    case MIME_INPUT_DND_LEAVE:
        if (g_dnd.Active && g_dnd.Id == ev->pad && !g_dnd.Dropped) {
            if (g_dnd.Entered)
                leave_target();
            g_dnd.Active = FALSE;
        }
        break;
    case MIME_INPUT_DND_DROP: {
        if (!g_dnd.Active || g_dnd.Id != ev->pad)
            break;
        WND *w = g_dnd.Entered ? WndLookup(g_dnd.Target) : nullptr;
        g_dnd.Dropped = TRUE;
        if (w)
            post_to_window(w, g_dnd.Files, MIME_DND_PHASE_DROP);
        else {
            HalMimeHypercall(MIME_HC_DND_FINISH, g_dnd.Id, DROPEFFECT_NONE, 0, 0, 0, 0, nullptr);
            g_dnd.Active = FALSE;
        }
        break;
    }
    case MIME_INPUT_DND_REQUEST:
        source_request(ev);
        break;
    case MIME_INPUT_DND_END:
        if (g_dnd.SourceThread && g_dnd.SourceId == ev->pad) {
            g_dnd.SourceResult = ev->x ? (ev->flags & 0xFF) | 0x100 : 0;
            QueuePost(g_dnd.SourceThread, nullptr, WM_MIME_HOSTDND, g_dnd.SourceHandler, (LPARAM)MIME_DND_PHASE_END);
            /* the host kept the button release from the guest. */
            InputReleaseLeftButton();
        }
        break;
    }
}

void DndThreadDestroyed(W32THREAD *t)
{
    if (g_dnd.SourceThread == t) {
        HalMimeHypercall(MIME_HC_DND_STOP, g_dnd.SourceId, 0, 0, 0, 0, 0, nullptr);
        g_dnd.SourceThread = nullptr;
        g_dnd.SourceId = 0;
    }
    if (g_dnd.Active && g_dnd.Target) {
        WND *w = WndLookup(g_dnd.Target);
        if (w && w->Thread == t)
            g_dnd.Target = nullptr, g_dnd.Entered = FALSE;
    }
}

void DndWindowDestroyed(WND *w)
{
    if (g_dnd.Target == WndHandle(w)) {
        g_dnd.Target = nullptr;
        g_dnd.Entered = FALSE;
        if (g_dnd.Active && g_dnd.Dropped) {
            HalMimeHypercall(MIME_HC_DND_FINISH, g_dnd.Id, DROPEFFECT_NONE, 0, 0, 0, 0, nullptr);
            g_dnd.Active = FALSE;
        }
    }
}

/* ------------------------------------------------------------------ services */

static ULONG formats_of(ULONG kinds, PULONG formats)
{
    ULONG n = 0;
    for (ULONG kind = 1; kind <= MIME_CLIP_URL && n < MIME_DND_STATE_SIZE - 6; kind <<= 1)
        if (kinds & kind)
            formats[n++] = ClipboardFormatOfKind(kind);
    return n;
}

ULONG_PTR NTAPI NtUserMimeDnd(ULONG op, ULONG_PTR a, ULONG_PTR b, PULONG out)
{
    ULONG_PTR result = 0;
    UserEnter();
    W32THREAD *t = W32GetThread();
    switch (op) {
    case MIME_DND_REGISTER: {
        WND *w = WndLookup((HWND)a);
        if (w && t && w->Thread && w->Thread->Process == t->Process) {
            w->Flags |= WNDF_DROPTARGET;
            t->Process->DndHandler = b;
            result = 1;
        }
        break;
    }
    case MIME_DND_REVOKE: {
        WND *w = WndLookup((HWND)a);
        if (w && (w->Flags & WNDF_DROPTARGET)) {
            w->Flags &= ~WNDF_DROPTARGET;
            result = 1;
        }
        break;
    }
    case MIME_DND_REPLY: {
        ULONG phase = (ULONG)(b >> 8), effect = (ULONG)(b & 0xFF);
        if (g_dnd.Active && g_dnd.Id == (ULONG)a) {
            if (phase == MIME_DND_PHASE_DROP && g_dnd.Dropped) {
                HalMimeHypercall(MIME_HC_DND_FINISH, g_dnd.Id, effect, 0, 0, 0, 0, nullptr);
                g_dnd.Active = FALSE;
            } else if (phase == MIME_DND_PHASE_ENTER || phase == MIME_DND_PHASE_OVER) {
                host_effect(effect);
            }
            result = 1;
        }
        break;
    }
    case MIME_DND_STATE:
        if (g_dnd.Active && g_dnd.Id == (ULONG)a && out) {
            ULONG state[MIME_DND_STATE_SIZE] = {g_dnd.Kinds, g_dnd.Allowed, g_dnd.Keys, (ULONG)g_dnd.Pt.x,
                                                (ULONG)g_dnd.Pt.y};
            state[5] = formats_of(g_dnd.Kinds, &state[6]);
            if (NT_SUCCESS(MimeCopyToUser(out, state, sizeof(state))))
                result = 1;
        }
        break;
    case MIME_DND_ICON: {
        /* MimeDragIcon allows up to 512 pixels each way. */
        MimeDragIcon header;
        if (g_dnd.Icon) {
            MmFreeContiguousPages(g_dnd.Icon, g_dnd.IconBytes);
            g_dnd.Icon = nullptr;
        }
        if (!out || a < sizeof(header) || a > sizeof(header) + (ULONG_PTR)MIME_DRAG_ICON_MAX * MIME_DRAG_ICON_MAX * 4 ||
            !NT_SUCCESS(MimeCopyFromUser(&header, out, sizeof(header))) || !header.width || !header.height ||
            header.width > MIME_DRAG_ICON_MAX || header.height > MIME_DRAG_ICON_MAX ||
            a != sizeof(header) + (ULONG_PTR)header.width * header.height * 4)
            break;
        PVOID pages = MmAllocateContiguousPages((SIZE_T)a);
        if (!pages)
            break;
        if (!NT_SUCCESS(MimeCopyFromUser(pages, out, (SIZE_T)a))) {
            MmFreeContiguousPages(pages, (SIZE_T)a);
            break;
        }
        g_dnd.Icon = pages;
        g_dnd.IconBytes = (ULONG)a;
        result = 1;
        break;
    }
    case MIME_DND_START: {
        ULONG64 session = 0;
        ULONG64 icon = g_dnd.Icon ? (ULONG64)MmGetPhysicalAddress(g_dnd.Icon).QuadPart : 0;
        if (t && !g_dnd.SourceThread &&
            HalMimeHypercall(MIME_HC_DND_START, a, b, 0, icon, g_dnd.IconBytes, 0, &session) == MIME_HC_STATUS_OK && session) {
            g_dnd.SourceId = (ULONG)session;
            g_dnd.SourceThread = t;
            g_dnd.SourceHandler = (ULONG_PTR)out;
            g_dnd.SourceResult = NO_RESULT;
            memset(g_dnd.Requests, 0, sizeof(g_dnd.Requests));
            result = (ULONG_PTR)session;
        }
        /* the host copied the image, or this drag has none. */
        if (g_dnd.Icon) {
            MmFreeContiguousPages(g_dnd.Icon, g_dnd.IconBytes);
            g_dnd.Icon = nullptr;
        }
        break;
    }
    case MIME_DND_STOP:
        if (g_dnd.SourceThread && g_dnd.SourceId == (ULONG)a) {
            HalMimeHypercall(MIME_HC_DND_STOP, g_dnd.SourceId, 0, 0, 0, 0, 0, nullptr);
            g_dnd.SourceThread = nullptr;
            g_dnd.SourceId = 0;
            result = 1;
        }
        break;
    case MIME_DND_REQUEST:
        if (a < MAX_REQUESTS && g_dnd.Requests[a].Used && out) {
            ULONG pair[2] = {g_dnd.Requests[a].Kind, g_dnd.Requests[a].Id};
            if (NT_SUCCESS(MimeCopyToUser(out, pair, sizeof(pair)))) {
                g_dnd.Requests[a].Used = FALSE;
                result = 1;
            }
        }
        break;
    case MIME_DND_OUTSIDE:
        result = InputPointerOutside;
        break;
    case MIME_DND_RESULT:
        if (g_dnd.SourceId == (ULONG)a) {
            result = g_dnd.SourceResult;
            if (result != NO_RESULT) {
                g_dnd.SourceThread = nullptr;
                g_dnd.SourceId = 0;
            }
        } else {
            result = 0;
        }
        break;
    }
    UserLeave();
    return result;
}

/* reads the host's data for a drag over the guest, a chunk at a time. */
ULONG NTAPI NtUserMimeDndData(ULONG session, ULONG format, PUCHAR buffer, ULONG capacity, ULONG offset, PULONG total)
{
    ULONG kind;
    BOOLEAN current;
    UserEnter();
    current = g_dnd.Active && g_dnd.Id == session;
    kind = current ? ClipboardKindOfFormat(format) & g_dnd.Kinds : 0;
    UserLeave();
    if (!current || !kind || capacity > HOST_CHUNK)
        return FALSE;
    PUCHAR page = MmAllocateContiguousPages(HOST_CHUNK);
    if (!page)
        return FALSE;
    ULONG64 size = 0;
    ULONG64 status = HalMimeHypercall(MIME_HC_DND_FETCH, session, kind, (ULONG64)MmGetPhysicalAddress(page).QuadPart,
                                      capacity, offset, 0, &size);
    ULONG ok = FALSE;
    if (status == MIME_HC_STATUS_OK && size <= 0xFFFFFFF0u) {
        ULONG n = offset < size ? (ULONG)(size - offset < capacity ? size - offset : capacity) : 0;
        ULONG whole = (ULONG)size;
        ok = (!n || (buffer && NT_SUCCESS(MimeCopyToUser(buffer, page, n)))) &&
             (!total || NT_SUCCESS(MimeCopyToUser(total, &whole, sizeof(whole))));
    }
    MmFreeContiguousPages(page, HOST_CHUNK);
    return ok;
}

/* answers a host program reading the guest's drag, a chunk at a time. */
ULONG NTAPI NtUserMimeDndProvide(ULONG request, PUCHAR data, ULONG length, ULONG offset, ULONG total)
{
    ULONG64 whole = total == 0xFFFFFFFFu ? MIME_CLIP_NONE : total;
    if (length > HOST_CHUNK)
        return FALSE;
    if (!length || total == 0xFFFFFFFFu)
        return HalMimeHypercall(MIME_HC_CLIP_PROVIDE, request, 0, 0, 0, whole, 0, nullptr) == MIME_HC_STATUS_OK;
    PUCHAR page = MmAllocateContiguousPages(HOST_CHUNK);
    if (!page)
        return FALSE;
    ULONG ok = FALSE;
    if (NT_SUCCESS(MimeCopyFromUser(page, data, length)))
        ok = HalMimeHypercall(MIME_HC_CLIP_PROVIDE, request, (ULONG64)MmGetPhysicalAddress(page).QuadPart, length,
                              offset, whole, 0, nullptr) == MIME_HC_STATUS_OK;
    MmFreeContiguousPages(page, HOST_CHUNK);
    return ok;
}
