#include "win32k.h"

enum { PART_NONE, PART_ARROW1, PART_PAGE1, PART_THUMB, PART_PAGE2, PART_ARROW2 };

/* the parts of the ScrollBar class of a visual style. */
enum {
    SBP_ARROWBTN = 1,
    SBP_THUMBBTNHORZ = 2,
    SBP_THUMBBTNVERT = 3,
    SBP_LOWERTRACKHORZ = 4,
    SBP_UPPERTRACKHORZ = 5,
    SBP_LOWERTRACKVERT = 6,
    SBP_UPPERTRACKVERT = 7,
    SBP_SIZEBOX = 10,
};

#define REPEAT_TIMER 0xFFF2
#define REPEAT_FIRST 400
#define REPEAT_NEXT 50

/* a bar laid out, in screen coordinates. */
typedef struct BAR {
    BOOLEAN Vert, Enabled, HasThumb;
    ULONG Disable; /* ESB_DISABLE_LTUP and ESB_DISABLE_RTDN */
    RECT Arrow1, Arrow2, Track, Thumb;
    LONG Room, Size;
    SCROLL_DATA Data;
} BAR;

/* the part the mouse holds down while a bar is tracked, with the position its thumb is dragged to, and the part the
   mouse is over. one thread tracks at a time, as one holds the capture. */
static struct {
    HWND Window;
    int Bar, Part;
    LONG Pos;
} g_held, g_hover;

static BOOLEAN bar_rect(const WND *w, int bar, RECT *r)
{
    RECT c = w->ClientRect;
    UINT dpi = WndGetDpi((WND *)w);
    if (bar == SB_VERT && (w->Style & WS_VSCROLL))
        *r = (RECT){c.right, c.top, c.right + MetricsGet(SM_CXVSCROLL, dpi), c.bottom};
    else if (bar == SB_HORZ && (w->Style & WS_HSCROLL))
        *r = (RECT){c.left, c.bottom, c.right, c.bottom + MetricsGet(SM_CYHSCROLL, dpi)};
    else
        return FALSE;
    return !RectEmpty(r);
}

static void layout(const WND *w, int bar, const RECT *r, BAR *b)
{
    memset(b, 0, sizeof(*b));
    b->Vert = bar == SB_VERT;
    /* a bar never set has the range 0 to 100, like on windows. */
    b->Data = w->Scroll ? w->Scroll->Bar[b->Vert] : (SCROLL_DATA){0, 100, 0, 0};
    b->Disable = w->Scroll ? (w->Scroll->Disable >> (b->Vert ? 2 : 0)) & 3 : 0;
    b->Enabled = !(w->Style & WS_DISABLED) && b->Disable != 3;
    LONG length = b->Vert ? r->bottom - r->top : r->right - r->left;
    LONG thick = b->Vert ? r->right - r->left : r->bottom - r->top;
    LONG arrow = thick * 2 <= length ? thick : length / 2;
    b->Arrow1 = b->Arrow2 = b->Track = *r;
    if (b->Vert) {
        b->Arrow1.bottom = b->Track.top = r->top + arrow;
        b->Arrow2.top = b->Track.bottom = r->bottom - arrow;
    } else {
        b->Arrow1.right = b->Track.left = r->left + arrow;
        b->Arrow2.left = b->Track.right = r->right - arrow;
    }
    const SCROLL_DATA *d = &b->Data;
    LONG64 span = (LONG64)d->Max - d->Min + 1;
    b->Room = b->Vert ? b->Track.bottom - b->Track.top : b->Track.right - b->Track.left;
    if (!b->Enabled || span <= 0 || b->Room <= 0 || (d->Page && d->Page >= span))
        return;
    UINT dpi = WndGetDpi((WND *)w);
    LONG smallest = MetricsGet(b->Vert ? SM_CYVTHUMB : SM_CXHTHUMB, dpi) / 2;
    /* CalcSBStuff2 rounds the size and the offset of the thumb with EngMulDiv. */
    LONG size = d->Page ? W32MulDiv(b->Room, (LONG)d->Page, (LONG)span) : thick;
    if (size < smallest)
        size = smallest;
    if (size > b->Room)
        return;
    LONG64 steps = span - (d->Page ? d->Page : 1);
    LONG pos = g_held.Window == WndHandle((WND *)w) && g_held.Bar == bar && g_held.Part == PART_THUMB ? g_held.Pos : d->Pos;
    LONG offset = steps > 0 ? (LONG)(((LONG64)(b->Room - size) * ((LONG64)pos - d->Min) + steps / 2) / steps) : 0;
    b->Size = size;
    b->HasThumb = TRUE;
    b->Thumb = b->Track;
    if (b->Vert) {
        b->Thumb.top += offset;
        b->Thumb.bottom = b->Thumb.top + size;
    } else {
        b->Thumb.left += offset;
        b->Thumb.right = b->Thumb.left + size;
    }
}

static int part_at(const BAR *b, POINT pt)
{
    if (RectContains(&b->Arrow1, pt.x, pt.y))
        return PART_ARROW1;
    if (RectContains(&b->Arrow2, pt.x, pt.y))
        return PART_ARROW2;
    if (!RectContains(&b->Track, pt.x, pt.y) || !b->HasThumb)
        return PART_NONE;
    if (RectContains(&b->Thumb, pt.x, pt.y))
        return PART_THUMB;
    return (b->Vert ? pt.y < b->Thumb.top : pt.x < b->Thumb.left) ? PART_PAGE1 : PART_PAGE2;
}

/*
 * GetScrollBarInfo of a bar of the frame: where it is on the screen, the thumb along it, and the states of the bar,
 * the first arrow, the page before the thumb, the thumb, the page after and the last arrow.
 */
void ScrollBarInfo(const WND *w, int bar, SCROLLBARINFO *info)
{
    memset(info, 0, sizeof(*info));
    info->cbSize = sizeof(*info);
    RECT r;
    if (!bar_rect(w, bar, &r)) {
        info->rgstate[0] = STATE_SYSTEM_INVISIBLE;
        return;
    }
    BAR b;
    layout(w, bar, &r, &b);
    LONG start = b.Vert ? r.top : r.left;
    info->rcScrollBar = r;
    info->dxyLineButton = b.Vert ? b.Arrow1.bottom - b.Arrow1.top : b.Arrow1.right - b.Arrow1.left;
    if (b.HasThumb) {
        info->xyThumbTop = (b.Vert ? b.Thumb.top : b.Thumb.left) - start;
        info->xyThumbBottom = (b.Vert ? b.Thumb.bottom : b.Thumb.right) - start;
    }
    ULONG disable = b.Enabled ? b.Disable : 3;
    if (disable & 1)
        info->rgstate[1] = info->rgstate[2] = STATE_SYSTEM_UNAVAILABLE;
    if (disable & 2)
        info->rgstate[5] = info->rgstate[4] = STATE_SYSTEM_UNAVAILABLE;
    if (disable == 3)
        info->rgstate[0] = STATE_SYSTEM_UNAVAILABLE;
    /* the parts are numbered as the states are. */
    if (g_held.Window == WndHandle((WND *)w) && g_held.Bar == bar && (g_held.Part == PART_ARROW1 || g_held.Part == PART_ARROW2))
        info->rgstate[g_held.Part] |= STATE_SYSTEM_PRESSED;
    if (!b.HasThumb || info->xyThumbTop == info->dxyLineButton)
        info->rgstate[2] |= STATE_SYSTEM_INVISIBLE;
    if (!b.HasThumb || info->xyThumbBottom == info->dxyLineButton + b.Room)
        info->rgstate[4] |= STATE_SYSTEM_INVISIBLE;
}

/* ------------------------------------------------------------------ drawing */

/* DrawPushButton of win32kfull for the thumb: EDGE_RAISED around the face. */
static void push_button(SURFACE *s, const REGION *clip, RECT r)
{
    static const int colors[2][2] = {{COLOR_3DLIGHT, COLOR_3DDKSHADOW}, {COLOR_3DHILIGHT, COLOR_3DSHADOW}};
    for (ULONG i = 0; i < 2 && r.right - r.left > 1 && r.bottom - r.top > 1; i++) {
        ULONG light = ColorToPixel(MetricsSysColor(colors[i][0])), dark = ColorToPixel(MetricsSysColor(colors[i][1]));
        DrawFillRect(s, clip, &(RECT){r.left, r.top, r.right - 1, r.top + 1}, light);
        DrawFillRect(s, clip, &(RECT){r.left, r.top, r.left + 1, r.bottom - 1}, light);
        DrawFillRect(s, clip, &(RECT){r.left, r.bottom - 1, r.right, r.bottom}, dark);
        DrawFillRect(s, clip, &(RECT){r.right - 1, r.top, r.right, r.bottom}, dark);
        r = (RECT){r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
    }
    DrawFillRect(s, clip, &r, ColorToPixel(MetricsSysColor(COLOR_3DFACE)));
}

/*
 * the groove of the classic look, in the brush DefWindowProc answers WM_CTLCOLORSCROLLBAR with: the highlight
 * when it is the colour of scroll bars and not the one of windows, else hbrGray with the face for its clear bits
 * and the highlight for the set ones. the pattern is aligned on the window, the origin of its dc.
 */
static void groove(SURFACE *s, const REGION *clip, const RECT *r, POINT origin)
{
    COLORREF light = MetricsSysColor(COLOR_3DHILIGHT);
    if (W32Display.Bits >= 8 && light == MetricsSysColor(COLOR_SCROLLBAR) && light != MetricsSysColor(COLOR_WINDOW)) {
        DrawFillRect(s, clip, r, ColorToPixel(light));
        return;
    }
    ULONG pixels[2] = {ColorToPixel(MetricsSysColor(COLOR_3DFACE)), ColorToPixel(light)};
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT part;
        if (!RectIntersect(&part, &clip->Rects[i], r))
            continue;
        for (LONG y = part.top; y < part.bottom; y++)
            for (LONG x = part.left; x < part.right; x++)
                SurfacePutPixel(s, x, y, pixels[(x - origin.x + y - origin.y) & 1]);
    }
}

/* DrawThumb2 inverts the page area a held page button scrolls by. */
static void invert(SURFACE *s, const REGION *clip, const RECT *r)
{
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT part;
        if (!RectIntersect(&part, &clip->Rects[i], r))
            continue;
        for (LONG y = part.top; y < part.bottom; y++)
            for (LONG x = part.left; x < part.right; x++)
                SurfacePutPixel(s, x, y, ~SurfaceGetPixel(s, x, y) & 0xFFFFFF);
    }
}

static void draw_bar(WND *w, int bar, SURFACE *s, const REGION *clip, POINT shift)
{
    RECT r;
    if (!bar_rect(w, bar, &r))
        return;
    BAR b;
    layout(w, bar, &r, &b);
    RECT *rects[] = {&b.Arrow1, &b.Arrow2, &b.Track, &b.Thumb};
    for (ULONG i = 0; i < RTL_NUMBER_OF(rects); i++)
        RectOffset(rects[i], shift.x, shift.y);
    HWND handle = WndHandle(w);
    UINT dpi = WndGetDpi(w);
    int held = g_held.Window == handle && g_held.Bar == bar ? g_held.Part : PART_NONE;
    int over = g_hover.Window == handle && g_hover.Bar == bar && held == PART_NONE ? g_hover.Part : PART_NONE;
    BOOLEAN off1 = !b.Enabled || (b.Disable & 1), off2 = !b.Enabled || (b.Disable & 2);
    /* the buttons up, down, left and right have four states each: normal, hot, pressed and disabled. */
    int state1 = (b.Vert ? 0 : 8) + (off1 ? 4 : held == PART_ARROW1 ? 3 : over == PART_ARROW1 ? 2 : 1);
    int state2 = (b.Vert ? 4 : 12) + (off2 ? 4 : held == PART_ARROW2 ? 3 : over == PART_ARROW2 ? 2 : 1);
    RECT before = b.Track, after = b.Track;
    if (b.HasThumb) {
        if (b.Vert) {
            before.bottom = b.Thumb.top;
            after.top = b.Thumb.bottom;
        } else {
            before.right = b.Thumb.left;
            after.left = b.Thumb.right;
        }
    } else {
        after.left = after.right;
    }
    int lower = b.Vert ? SBP_LOWERTRACKVERT : SBP_LOWERTRACKHORZ, upper = b.Vert ? SBP_UPPERTRACKVERT : SBP_UPPERTRACKHORZ;
    if (FrameDrawScrollPart(s, clip, &b.Arrow1, SBP_ARROWBTN, state1, dpi)) {
        FrameDrawScrollPart(s, clip, &b.Arrow2, SBP_ARROWBTN, state2, dpi);
        if (!RectEmpty(&before))
            FrameDrawScrollPart(s, clip, &before, lower, !b.Enabled ? 4 : held == PART_PAGE1 ? 3 : 1, dpi);
        if (!RectEmpty(&after))
            FrameDrawScrollPart(s, clip, &after, upper, held == PART_PAGE2 ? 3 : 1, dpi);
        if (b.HasThumb)
            FrameDrawScrollPart(s, clip, &b.Thumb, b.Vert ? SBP_THUMBBTNVERT : SBP_THUMBBTNHORZ,
                                held == PART_THUMB ? 3 : over == PART_THUMB ? 2 : 1, dpi);
        return;
    }
    /*
     * the arrows of the strip, up, down, right and left, pushed flat while held and inactive when disabled. the
     * classic look has no hot arrow.
     */
    OemDrawScrollArrow(s, clip, &b.Arrow1, b.Vert ? 0 : 3, off1 ? 3 : held == PART_ARROW1 ? 1 : 0);
    OemDrawScrollArrow(s, clip, &b.Arrow2, b.Vert ? 1 : 2, off2 ? 3 : held == PART_ARROW2 ? 1 : 0);
    POINT origin = {w->WindowRect.left + shift.x, w->WindowRect.top + shift.y};
    if (!b.HasThumb) {
        groove(s, clip, &b.Track, origin);
        return;
    }
    if (!RectEmpty(&before))
        groove(s, clip, &before, origin);
    if (!RectEmpty(&after))
        groove(s, clip, &after, origin);
    push_button(s, clip, b.Thumb);
    if (held == PART_PAGE1 && !RectEmpty(&before))
        invert(s, clip, &before);
    else if (held == PART_PAGE2 && !RectEmpty(&after))
        invert(s, clip, &after);
}

/* the bars of a window into the surface of its top level window, with the clip of its frame. */
void ScrollPaint(WND *w, SURFACE *s, const REGION *clip)
{
    if (!(w->Style & (WS_VSCROLL | WS_HSCROLL)) || (w->Style & WS_MINIMIZE))
        return;
    WND *top = WndTopLevel(w);
    POINT shift = {-top->WindowRect.left, -top->WindowRect.top};
    draw_bar(w, SB_VERT, s, clip, shift);
    draw_bar(w, SB_HORZ, s, clip, shift);
    RECT v, h;
    if (bar_rect(w, SB_VERT, &v) && bar_rect(w, SB_HORZ, &h)) {
        RECT box = {v.left + shift.x, h.top + shift.y, v.right + shift.x, h.bottom + shift.y};
        if (!FrameDrawScrollPart(s, clip, &box, SBP_SIZEBOX, 1, WndGetDpi(w)))
            DrawFillRect(s, clip, &box, ColorToPixel(MetricsSysColor(COLOR_3DFACE)));
    }
}

void ScrollRedraw(WND *w)
{
    WND *top = WndTopLevel(w);
    if (!top || !top->Surface || !WndIsVisible(w) || !(w->Style & (WS_VSCROLL | WS_HSCROLL)))
        return;
    REGION clip;
    RgnInit(&clip);
    WndComputeVisRgn(w, FALSE, 0, &clip);
    RECT client = w->ClientRect, window = w->WindowRect;
    RectOffset(&client, -top->WindowRect.left, -top->WindowRect.top);
    RectOffset(&window, -top->WindowRect.left, -top->WindowRect.top);
    RgnCombineRect(&clip, &clip, &client, RGN_DIFF);
    if (!RgnIsEmpty(&clip)) {
        ScrollPaint(w, top->Surface, &clip);
        SurfaceMarkDrawn(top->Surface, &window, &clip);
        if (top->Thread == W32GetThread() && (top->Flags & WNDF_PRESENTED))
            PaintPresent(top);
    }
    RgnFree(&clip);
}

/* ------------------------------------------------------------------ the mouse */

static void hover(WND *w, int bar, int part)
{
    HWND handle = part == PART_NONE ? nullptr : WndHandle(w);
    if (g_hover.Window == handle && (!handle || (g_hover.Bar == bar && g_hover.Part == part)))
        return;
    WND *old = g_hover.Window && g_hover.Window != handle ? WndLookup(g_hover.Window) : nullptr;
    g_hover.Window = handle;
    g_hover.Bar = bar;
    g_hover.Part = part;
    if (old)
        ScrollRedraw(old);
    ScrollRedraw(w);
}

/* the mouse over the frame of a window, or gone from it: the part under it is drawn hot. */
void ScrollMouse(WND *w, UINT msg, LONG hit, POINT pt)
{
    int bar = hit == HTVSCROLL ? SB_VERT : hit == HTHSCROLL ? SB_HORZ : -1;
    RECT r;
    if (msg != WM_NCMOUSEMOVE || bar < 0 || !bar_rect(w, bar, &r)) {
        if (g_hover.Window == WndHandle(w))
            hover(w, 0, PART_NONE);
        return;
    }
    BAR b;
    layout(w, bar, &r, &b);
    hover(w, bar, part_at(&b, pt));
}

BOOLEAN ScrollTrackPos(const WND *w, int bar, LONG *pos)
{
    if (g_held.Window != WndHandle((WND *)w) || g_held.Bar != bar || g_held.Part != PART_THUMB)
        return FALSE;
    *pos = g_held.Pos;
    return TRUE;
}

void ScrollWindowDestroyed(WND *w)
{
    if (g_held.Window == WndHandle(w))
        g_held.Window = nullptr;
    if (g_hover.Window == WndHandle(w))
        g_hover.Window = nullptr;
}

static LONG thumb_position(const BAR *b, POINT pt, LONG grab)
{
    const SCROLL_DATA *d = &b->Data;
    LONG64 steps = (LONG64)d->Max - d->Min + 1 - (d->Page ? d->Page : 1);
    LONG room = b->Room - b->Size;
    LONG at = (b->Vert ? pt.y - b->Track.top : pt.x - b->Track.left) - grab;
    if (at < 0)
        at = 0;
    if (at > room)
        at = room;
    return room > 0 && steps > 0 ? (LONG)(d->Min + ((LONG64)at * steps + room / 2) / room) : d->Min;
}

/* WM_HSCROLL or WM_VSCROLL to the window. FALSE once the window is gone or no longer has the bar. */
static BOOLEAN notify(HWND handle, int bar, UINT code, LONG pos)
{
    WND *w = WndLookup(handle);
    if (!w)
        return FALSE;
    MsgSend(w, bar == SB_VERT ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(code, (WORD)pos), 0);
    w = WndLookup(handle);
    return w && (w->Style & (bar == SB_VERT ? WS_VSCROLL : WS_HSCROLL));
}

/*
 * xxxSBTrackInit: a press on a bar, until the button is released. an arrow and the track scroll once, then again
 * and again after a pause while the mouse stays on the part, and the thumb follows the mouse.
 */
void ScrollTrack(WND *w, int bar, POINT pt)
{
    static const UINT codes[] = {0, SB_LINEUP, SB_PAGEUP, SB_THUMBTRACK, SB_PAGEDOWN, SB_LINEDOWN};
    W32THREAD *t = W32GetThread();
    RECT r;
    if (!t || w->Thread != t || !bar_rect(w, bar, &r))
        return;
    BAR b;
    layout(w, bar, &r, &b);
    int part = part_at(&b, pt);
    if (!b.Enabled || part == PART_NONE || (part == PART_ARROW1 && (b.Disable & 1)) ||
        (part == PART_ARROW2 && (b.Disable & 2)))
        return;
    HWND handle = WndHandle(w);
    LONG grab = part == PART_THUMB ? (b.Vert ? pt.y - b.Thumb.top : pt.x - b.Thumb.left) : 0;
    g_held.Window = handle;
    g_held.Bar = bar;
    g_held.Part = part;
    g_held.Pos = b.Data.Pos;
    InputSetCapture(t, w, 0);
    ScrollRedraw(w);
    BOOLEAN alive = TRUE;
    if (part != PART_THUMB) {
        alive = notify(handle, bar, codes[part], 0);
        if (alive)
            TimerSet(t, handle, REPEAT_TIMER, REPEAT_FIRST, nullptr, TRUE);
    }
    while (alive && t->Capture == handle && g_held.Window == handle) {
        MSG msg;
        memset(&msg, 0, sizeof(msg));
        if (!MsgPeek(t, &msg, nullptr, 0, 0, PM_REMOVE, TRUE))
            break;
        if (msg.message == WM_QUIT) {
            t->QuitPosted = TRUE;
            t->QuitCode = (int)msg.wParam;
            break;
        }
        if (msg.message == WM_LBUTTONUP || msg.message == WM_NCLBUTTONUP)
            break;
        if ((msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) && msg.wParam == VK_ESCAPE) {
            g_held.Pos = b.Data.Pos;
            break;
        }
        w = WndLookup(handle);
        if (!w || !bar_rect(w, bar, &r))
            break;
        if (msg.message == WM_MOUSEMOVE) {
            MSG next;
            while (MsgPeek(t, &next, nullptr, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE, FALSE))
                msg = next;
            if (part != PART_THUMB)
                continue;
            layout(w, bar, &r, &b);
            LONG pos = thumb_position(&b, msg.pt, grab);
            if (pos != g_held.Pos) {
                g_held.Pos = pos;
                ScrollRedraw(w);
                alive = notify(handle, bar, SB_THUMBTRACK, pos);
            }
        } else if (msg.message == WM_SYSTIMER && msg.hwnd == handle && msg.wParam == REPEAT_TIMER) {
            TimerSet(t, handle, REPEAT_TIMER, REPEAT_NEXT, nullptr, TRUE);
            layout(w, bar, &r, &b);
            if (part_at(&b, InputCursorPos) == part)
                alive = notify(handle, bar, codes[part], 0);
        } else if ((msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) ||
                   (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST)) {
            continue;
        } else {
            WND *target = WndLookup(msg.hwnd);
            if (target && target->Thread == t) {
                MsgCallWindowProc(target, target->Proc, target->Ansi, msg.message, msg.wParam, msg.lParam, MIME_LPARAM_VALUE, 0);
                if (msg.message == WM_PAINT)
                    MsgPaintDispatched(target);
            }
        }
    }
    TimerKill(t, handle, REPEAT_TIMER, TRUE);
    if (t->Capture == handle)
        InputSetCapture(t, nullptr, 0);
    LONG pos = g_held.Pos;
    BOOLEAN ours = g_held.Window == handle;
    g_held.Window = nullptr;
    if (!ours || !alive)
        return;
    if (part == PART_THUMB && !notify(handle, bar, SB_THUMBPOSITION, pos))
        return;
    if (!notify(handle, bar, SB_ENDSCROLL, 0))
        return;
    w = WndLookup(handle);
    if (w)
        ScrollRedraw(w);
}
