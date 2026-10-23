/*
 * the host input method, for programs that use the input method manager. the host reports a composition as
 * MIME_INPUT_IME events with the text queued behind them, and this turns them into the messages an IME sends:
 * WM_IME_STARTCOMPOSITION, WM_IME_COMPOSITION with GCS_COMPSTR and its attributes while composing or with
 * GCS_RESULTSTR on commit, and WM_IME_ENDCOMPOSITION. they go to the focus window of the foreground thread. like
 * windows, DefWindowProc turns an unhandled result into WM_IME_CHAR and WM_CHAR.
 *
 * the strings stay here for ImmGetCompositionString, one composition at a time since there is one keyboard.
 *
 * a thread that uses the text services framework has a MIME_IME_TSF sink window. the events go to it as one
 * message, and it either takes them, composing in the program's own document, or declines and the messages above
 * follow.
 */
#include "win32k.h"

#define IME_MAX 256
#define IME_CHUNK 4096

#define GCS_COMPREADSTR 0x0001
#define GCS_COMPREADATTR 0x0002
#define GCS_COMPREADCLAUSE 0x0004
#define GCS_COMPSTR 0x0008
#define GCS_COMPATTR 0x0010
#define GCS_COMPCLAUSE 0x0020
#define GCS_CURSORPOS 0x0080
#define GCS_DELTASTART 0x0100
#define GCS_RESULTREADSTR 0x0200
#define GCS_RESULTREADCLAUSE 0x0400
#define GCS_RESULTSTR 0x0800
#define GCS_RESULTCLAUSE 0x1000
#define ATTR_INPUT 0
#define ATTR_TARGET_CONVERTED 1
#define ATTR_CONVERTED 2
#define CPS_COMPLETE 1
#define CPS_CANCEL 4

static struct {
    BOOLEAN Composing;
    HWND Window;               /* the window that got the start. */
    ULONG CompLength;
    WCHAR Comp[IME_MAX];
    UCHAR Attr[IME_MAX];
    ULONG Clause[3];           /* boundaries: 0, the target's, the end. */
    ULONG ClauseCount;
    ULONG Cursor;
    ULONG DeltaStart;
    ULONG ResultLength;
    WCHAR Result[IME_MAX];
    WCHAR Pending[IME_MAX];    /* the text of the event being sent to the sink window. */
    ULONG PendingLength;
    PWCHAR Page;               /* the hypercall buffer. */
    BOOLEAN Queried, Available;
    HWND AreaWindow;
    LONG Area[5];
} g_ime;

static BOOLEAN available(void)
{
    if (!g_ime.Queried) {
        ULONG64 value = 0;
        g_ime.Queried = TRUE;
        if (HalMimeHypercall(MIME_HC_IME_CONTROL, MIME_IME_QUERY, 0, 0, 0, 0, 0, &value) == MIME_HC_STATUS_OK)
            g_ime.Available = value != 0;
    }
    return g_ime.Available;
}

/* the focus window of the foreground thread. */
static WND *target_window(void)
{
    if (!InputForeground || !InputForeground->Thread)
        return nullptr;
    W32THREAD *t = InputForeground->Thread;
    WND *w = WndLookup(t->Focus);
    return w ? w : WndLookup(t->Active);
}

/* takes the oldest text the host queued. the length is in characters, 0 for none. */
static ULONG take_text(PWCHAR out, ULONG max)
{
    if (!g_ime.Page)
        g_ime.Page = MmAllocateContiguousPages(IME_CHUNK);
    if (!g_ime.Page)
        return 0;
    ULONG64 length = 0;
    if (HalMimeHypercall(MIME_HC_IME_TEXT, (ULONG64)MmGetPhysicalAddress(g_ime.Page).QuadPart, IME_CHUNK, 0, 0, 0, 0,
                         &length) != MIME_HC_STATUS_OK)
        return 0;
    ULONG n = length < IME_CHUNK / sizeof(WCHAR) ? (ULONG)length : IME_CHUNK / sizeof(WCHAR);
    if (n > max)
        n = max;
    memcpy(out, g_ime.Page, n * sizeof(WCHAR));
    return n;
}

static void send(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WND *w = WndLookup(hwnd);
    if (w)
        MsgSend(w, msg, wparam, lparam);
}

static void end_composition(void)
{
    HWND window = g_ime.Window;
    g_ime.Composing = FALSE;
    g_ime.CompLength = 0;
    g_ime.ClauseCount = 0;
    g_ime.Cursor = 0;
    send(window, WM_IME_ENDCOMPOSITION, 0, 0);
    g_ime.Window = nullptr;
}

static void begin_composition(HWND window)
{
    g_ime.Composing = TRUE;
    g_ime.Window = window;
    g_ime.ResultLength = 0;
    send(window, WM_IME_STARTCOMPOSITION, 0, 0);
}

/* the selected part of the preedit text is the clause being converted. */
static void set_preedit(ULONG length, LONG begin, LONG end)
{
    ULONG old = g_ime.CompLength;
    g_ime.CompLength = length;
    g_ime.ClauseCount = 0;
    g_ime.Clause[g_ime.ClauseCount++] = 0;
    if (begin >= 0 && end > begin && (ULONG)end <= length) {
        for (ULONG i = 0; i < length; i++)
            g_ime.Attr[i] = (LONG)i >= begin && (LONG)i < end ? ATTR_TARGET_CONVERTED : ATTR_CONVERTED;
        if (begin > 0)
            g_ime.Clause[g_ime.ClauseCount++] = (ULONG)begin;
        if ((ULONG)end < length)
            g_ime.Clause[g_ime.ClauseCount++] = (ULONG)end;
        g_ime.Cursor = (ULONG)begin;
    } else {
        memset(g_ime.Attr, ATTR_INPUT, length);
        g_ime.Cursor = end >= 0 && (ULONG)end <= length ? (ULONG)end : length;
    }
    g_ime.Clause[g_ime.ClauseCount++] = length;
    /* the first character that differs from the string before. */
    g_ime.DeltaStart = old < length ? old : length;
}

/* a window that ImmAssociateContext gave no context does not take part. */
static BOOLEAN ime_disabled(WND *w)
{
    ATOM atom = AtomFind(L"MimeImmContext");
    return atom && PropGetData(w, atom) == (HANDLE)1;
}

/* returns whether the text services framework of the window's thread took the event. */
static BOOLEAN offer_to_text_services(WND *target, const MimeInputEvent *ev, const WCHAR *text, ULONG n)
{
    W32THREAD *t = target->Thread;
    WND *sink = t && t->TsfSink ? WndLookup(t->TsfSink) : nullptr;
    if (!sink)
        return FALSE;
    memcpy(g_ime.Pending, text, n * sizeof(WCHAR));
    g_ime.PendingLength = n;
    /* the selected part as two words, each the position plus one, or 0 for none. */
    LPARAM selection = 0;
    if (ev->flags == MIME_IME_PREEDIT)
        selection = (LPARAM)(((ev->x >= 0 ? ev->x + 1 : 0) & 0xFFFF) | ((ev->y >= 0 ? ev->y + 1 : 0) & 0xFFFF) << 16);
    LRESULT taken = MsgSend(sink, MIME_TSF_MESSAGE, ev->flags, selection);
    g_ime.PendingLength = 0;
    return taken != 0;
}

void ImeHostEvent(const MimeInputEvent *ev)
{
    WCHAR text[IME_MAX];
    ULONG n = ev->flags == MIME_IME_CANCELLED ? 0 : take_text(text, IME_MAX);
    WND *target = target_window();
    if (!target) {
        g_ime.Composing = FALSE;
        return;
    }
    HWND window = WndHandle(target);
    /* a composition the input method manager started stays with it, whatever the framework would do. */
    if (!g_ime.Composing && offer_to_text_services(target, ev, text, n))
        return;
    if (ime_disabled(target)) {
        /* committed text is typed, and the composition belongs to nobody. */
        if (ev->flags == MIME_IME_COMMIT)
            for (ULONG i = 0; i < n; i++)
                QueuePost(target->Thread, window, WM_CHAR, text[i], 1);
        return;
    }
    switch (ev->flags) {
    case MIME_IME_PREEDIT:
        if (!g_ime.Composing || g_ime.Window != window) {
            if (g_ime.Composing)
                end_composition();
            begin_composition(window);
        }
        memcpy(g_ime.Comp, text, n * sizeof(WCHAR));
        set_preedit(n, ev->x, ev->y);
        send(g_ime.Window, WM_IME_COMPOSITION, n ? g_ime.Comp[n - 1] : 0,
             GCS_COMPSTR | GCS_COMPATTR | GCS_COMPCLAUSE | GCS_CURSORPOS | GCS_DELTASTART | GCS_COMPREADSTR);
        break;
    case MIME_IME_COMMIT:
        if (!g_ime.Composing)
            begin_composition(window);
        g_ime.CompLength = 0;
        g_ime.ClauseCount = 0;
        memcpy(g_ime.Result, text, n * sizeof(WCHAR));
        g_ime.ResultLength = n;
        send(g_ime.Window, WM_IME_COMPOSITION, n ? text[0] : 0, GCS_RESULTSTR | GCS_RESULTREADSTR | GCS_RESULTCLAUSE);
        end_composition();
        break;
    case MIME_IME_CANCELLED:
        if (g_ime.Composing) {
            g_ime.CompLength = 0;
            send(g_ime.Window, WM_IME_COMPOSITION, 0, 0);
            end_composition();
        }
        break;
    }
}

void ImeWindowDestroyed(WND *w)
{
    if (w->Thread && w->Thread->TsfSink == WndHandle(w))
        w->Thread->TsfSink = nullptr;
    if (g_ime.Window == WndHandle(w)) {
        if (g_ime.Composing)
            HalMimeHypercall(MIME_HC_IME_CONTROL, MIME_IME_CANCEL, (ULONG64)(ULONG_PTR)g_ime.Window, 0, 0, 0, 0,
                             nullptr);
        g_ime.Composing = FALSE;
        g_ime.CompLength = 0;
        g_ime.Window = nullptr;
    }
    if (g_ime.AreaWindow == WndHandle(w))
        g_ime.AreaWindow = nullptr;
}

/* what DefWindowProc does with a committed result: one WM_IME_CHAR per character. */
LRESULT ImeDefaultCompositionChars(WND *w)
{
    WCHAR result[IME_MAX];
    ULONG n = g_ime.ResultLength;
    memcpy(result, g_ime.Result, n * sizeof(WCHAR));
    HWND hwnd = WndHandle(w);
    for (ULONG i = 0; i < n; i++) {
        if (!WndLookup(hwnd))
            break;
        MsgSend(w, WM_IME_CHAR, result[i], 1);
    }
    return 0;
}

/* a completed composition becomes the result, otherwise it is dropped. the host forgets it either way. */
static void finish_composition(BOOLEAN complete)
{
    HalMimeHypercall(MIME_HC_IME_CONTROL, MIME_IME_CANCEL, (ULONG64)(ULONG_PTR)g_ime.Window, 0, 0, 0, 0, nullptr);
    if (complete && g_ime.CompLength) {
        memcpy(g_ime.Result, g_ime.Comp, g_ime.CompLength * sizeof(WCHAR));
        g_ime.ResultLength = g_ime.CompLength;
        g_ime.CompLength = 0;
        send(g_ime.Window, WM_IME_COMPOSITION, g_ime.Result[0], GCS_RESULTSTR | GCS_RESULTREADSTR | GCS_RESULTCLAUSE);
    } else {
        g_ime.CompLength = 0;
        send(g_ime.Window, WM_IME_COMPOSITION, 0, 0);
    }
    end_composition();
}

/*
 * a composition belongs to the focus window: when the focus moves elsewhere, or nowhere, it is completed where it
 * was. this is a guess at windows, where an input method completes it when its context is deactivated.
 */
void ImeFocusChanged(void)
{
    if (!g_ime.Composing)
        return;
    WND *w = WndLookup(g_ime.Window);
    if (w && target_window() == w)
        return;
    if (!w) {
        g_ime.Composing = FALSE;
        g_ime.CompLength = 0;
        g_ime.Window = nullptr;
        return;
    }
    UserReference(&w->Header);
    finish_composition(TRUE);
    UserDereference(&w->Header);
}

/* tells the host where text is entered, in client coordinates of a window of this thread. */
static void set_area(HWND hwnd, const LONG *area)
{
    WND *w = WndLookup(hwnd);
    W32THREAD *t = W32GetThread();
    if (!w || !t || w->Thread != t || !available())
        return;
    /* only the window the input method composes for matters. */
    WND *target = target_window();
    if (!target || (target != w && !WndIsChildOf(target, w) && !WndIsChildOf(w, target)))
        return;
    WND *top = WndTopLevel(w);
    if (!top)
        return;
    LONG rect[5] = {area[0] + w->ClientRect.left - top->WindowRect.left, area[1] + w->ClientRect.top - top->WindowRect.top,
                    area[2] + w->ClientRect.left - top->WindowRect.left, area[3] + w->ClientRect.top - top->WindowRect.top,
                    area[4] + w->ClientRect.left - top->WindowRect.left};
    if (g_ime.AreaWindow == WndHandle(top) && !memcmp(rect, g_ime.Area, sizeof(rect)))
        return;
    g_ime.AreaWindow = WndHandle(top);
    memcpy(g_ime.Area, rect, sizeof(rect));
    HalMimeHypercall(MIME_HC_IME_AREA, (ULONG64)(ULONG_PTR)WndHandle(top), (ULONG64)(LONG64)rect[0],
                     (ULONG64)(LONG64)rect[1], (ULONG64)(LONG64)rect[2], (ULONG64)(LONG64)rect[3],
                     (ULONG64)(LONG64)rect[4], nullptr);
}

ULONG NTAPI NtUserMimeIme(ULONG op, ULONG_PTR a, ULONG_PTR b, PUCHAR buffer, ULONG size)
{
    ULONG result = 0;
    UserEnter();
    W32THREAD *t = W32GetThread();
    switch (op) {
    case MIME_IME_AVAILABLE:
        result = available();
        break;
    case MIME_IME_AREA: {
        LONG area[5];
        if (buffer && size >= sizeof(area) && NT_SUCCESS(MimeCopyFromUser(area, buffer, sizeof(area)))) {
            set_area((HWND)a, area);
            result = 1;
        }
        break;
    }
    case MIME_IME_TSF: {
        WND *sink = a ? WndLookup((HWND)a) : nullptr;
        if (a && (!sink || sink->Thread != t))
            break;
        t->TsfSink = sink ? (HWND)a : nullptr;
        result = 1;
        break;
    }
    case MIME_IME_PENDING: {
        ULONG bytes = g_ime.PendingLength * sizeof(WCHAR);
        if (buffer && size) {
            ULONG n = bytes < size ? bytes : size;
            if (n && !NT_SUCCESS(MimeCopyToUser(buffer, g_ime.Pending, n)))
                break;
            result = n;
        } else {
            result = bytes;
        }
        break;
    }
    case MIME_IME_RESET: {
        WND *target = target_window();
        if (target && target->Thread == t && available())
            HalMimeHypercall(MIME_HC_IME_CONTROL, MIME_IME_CANCEL, (ULONG64)(ULONG_PTR)WndHandle(target), 0, 0, 0, 0,
                             nullptr);
        result = 1;
        break;
    }
    case MIME_IME_NOTIFY:
        if (g_ime.Composing) {
            WND *w = WndLookup(g_ime.Window);
            if (w && w->Thread == t) {
                finish_composition(a == CPS_COMPLETE);
                result = 1;
            }
        }
        break;
    case MIME_IME_GET: {
        WND *w = g_ime.Composing || g_ime.ResultLength ? WndLookup(g_ime.Window) : nullptr;
        /* the composition belongs to the thread of the window it composes for. */
        if (!w || w->Thread != t)
            break;
        const VOID *data = nullptr;
        ULONG bytes = 0;
        switch (a) {
        case GCS_COMPSTR:
        case GCS_COMPREADSTR:
            data = g_ime.Comp;
            bytes = g_ime.CompLength * sizeof(WCHAR);
            break;
        case GCS_COMPATTR:
        case GCS_COMPREADATTR:
            data = g_ime.Attr;
            bytes = g_ime.CompLength;
            break;
        case GCS_COMPCLAUSE:
        case GCS_COMPREADCLAUSE:
            data = g_ime.Clause;
            bytes = g_ime.CompLength ? g_ime.ClauseCount * sizeof(ULONG) : 0;
            break;
        case GCS_RESULTSTR:
        case GCS_RESULTREADSTR:
            data = g_ime.Result;
            bytes = g_ime.ResultLength * sizeof(WCHAR);
            break;
        case GCS_RESULTCLAUSE:
        case GCS_RESULTREADCLAUSE: {
            static ULONG clause[2];
            clause[0] = 0;
            clause[1] = g_ime.ResultLength;
            data = clause;
            bytes = g_ime.ResultLength ? sizeof(clause) : 0;
            break;
        }
        case GCS_CURSORPOS:
            result = g_ime.Cursor;
            goto done;
        case GCS_DELTASTART:
            result = g_ime.DeltaStart;
            goto done;
        default:
            goto done;
        }
        if (buffer && size) {
            ULONG n = bytes < size ? bytes : size;
            if (a == GCS_COMPSTR || a == GCS_COMPREADSTR || a == GCS_RESULTSTR || a == GCS_RESULTREADSTR)
                n &= ~1u;
            if (n && !NT_SUCCESS(MimeCopyToUser(buffer, data, n)))
                break;
            result = n;
        } else {
            result = bytes;
        }
        break;
    }
    }
done:
    UserLeave();
    return result;
}
