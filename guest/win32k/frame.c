/*
 * the window frame, drawn by win32k in the role of the desktop window manager: a windows 10 caption with the caption
 * buttons of the Window class of the visual style. the host compositor adds the 1 pixel border.
 *
 * resizable windows keep the invisible resize borders of windows 10 and 11: the window rectangle extends 7 pixels
 * at 96 dpi beyond the visible frame on the left, right and bottom.
 */
#include "win32k.h"
#include <msstyles/msstyles.h>
#include <png/png.h>

#define FRAME_TAG W32_TAG('W', 'f', 'r', 'm')

/* the parts of the Window and DWMWindow classes the frame draws, the right and bottom frames following the left. */
enum {
    WP_CAPTION = 1,
    WP_SMALLCAPTION = 2,
    WP_MINCAPTION = 3,
    WP_MAXCAPTION = 5,
    WP_FRAMELEFT = 7,
    WP_SMALLFRAMELEFT = 10,
    WP_MINBUTTON = 15,
    WP_MAXBUTTON = 17,
    WP_CLOSEBUTTON = 18,
    WP_SMALLCLOSEBUTTON = 19,
    WP_RESTOREBUTTON = 21,
    WP_HELPBUTTON = 23,
    WP_CAPTIONSIZINGTEMPLATE = 30,
    WP_SMALLCAPTIONSIZINGTEMPLATE = 31,
};

static LONG scale(LONG v, UINT dpi)
{
    return W32MulDiv(v, (LONG)dpi, 96);
}

/* ------------------------------------------------------------------ metrics */

/*
 * ThemeGetSystemMetricsForDpi of uxtheme 26300: a themed program sees SM_CXSIZE as the height of a caption button in
 * the ratio of the buttons of the style, 36 to 22. a small button stays square.
 */
LONG FrameThemedButtonWidth(UINT dpi)
{
    return W32MulDiv(MetricsGet(SM_CYSIZE, dpi), 36, 22);
}

BOOLEAN FrameHasCaption(DWORD style, DWORD ex_style)
{
    (void)ex_style;
    return (style & WS_CAPTION) == WS_CAPTION;
}

static BOOLEAN is_top_frame(DWORD style)
{
    return !(style & WS_CHILD);
}

void FrameGetMetrics(DWORD style, DWORD ex_style, UINT dpi, FRAME_METRICS *m)
{
    memset(m, 0, sizeof(*m));
    BOOLEAN caption = FrameHasCaption(style, ex_style);
    BOOLEAN thick = (style & WS_THICKFRAME) != 0;
    BOOLEAN dlg = (style & WS_DLGFRAME) || (ex_style & WS_EX_DLGMODALFRAME);
    if (is_top_frame(style) && (caption || thick || dlg || (style & WS_BORDER))) {
        /*
         * any caption window gets the sizing border and the padded border, sizable or not, a thick frame alone one pixel
         * less, the border it has not. only those have an invisible band out of the frame the dwm shows. measured on
         * windows 11 26100, as AdjustWindowRectEx of user32 has it.
         */
        if (caption)
            m->Border = MetricsGet(SM_CXFRAME, dpi) + MetricsGet(SM_CXPADDEDBORDER, dpi);
        else if (thick)
            m->Border = MetricsGet(SM_CXFRAME, dpi) + MetricsGet(SM_CXPADDEDBORDER, dpi) - ((style & WS_BORDER) ? 0 : 1);
        else if (dlg)
            m->Border = MetricsGet(SM_CXDLGFRAME, dpi);
        else
            m->Border = 1;
        m->VisibleInset = (caption || thick) && m->Border > 1 ? m->Border - 1 : 0;
        m->Caption = caption ? MetricsGet((ex_style & WS_EX_TOOLWINDOW) ? SM_CYSMCAPTION : SM_CYCAPTION, dpi) : 0;
        m->Top = m->Border + m->Caption;
        /* the dwm makes the buttons of a tool window square, 21/22 of its caption. */
        m->ButtonWidth = (ex_style & WS_EX_TOOLWINDOW) ? (m->Caption * 21 + 11) / 22 : scale(46, dpi);
        /*
         * GetEffectiveCornerStyle and GetRadiusFromCornerStyle of udwm 26100: a window with a frame is round by 8 pixels
         * at 96 dpi, a tool window with a caption by 4. windows draws them square without a gpu.
         */
        m->CornerRadius = scale((ex_style & WS_EX_TOOLWINDOW) && caption ? 4 : 8, dpi);
        return;
    }
    /*
     * a child window has the frame of a top level one, measured on windows 11 26100, and the edges of
     * WS_EX_CLIENTEDGE and WS_EX_STATICEDGE inside it. a top level window without a frame keeps the edges too.
     */
    if (caption)
        m->Border = MetricsGet(SM_CXFRAME, dpi) + MetricsGet(SM_CXPADDEDBORDER, dpi);
    else if (thick)
        m->Border = MetricsGet(SM_CXFRAME, dpi) + MetricsGet(SM_CXPADDEDBORDER, dpi) - ((style & WS_BORDER) ? 0 : 1);
    else if (dlg)
        m->Border = MetricsGet(SM_CXDLGFRAME, dpi);
    else if (style & WS_BORDER)
        m->Border = 1;
    m->Edge = ((ex_style & WS_EX_CLIENTEDGE) ? 2 : 0) + ((ex_style & WS_EX_STATICEDGE) ? 1 : 0);
    BOOLEAN tool = (ex_style & WS_EX_TOOLWINDOW) != 0;
    m->Caption = caption ? MetricsGet(tool ? SM_CYSMCAPTION : SM_CYCAPTION, dpi) : 0;
    m->Top = m->Border + m->Caption;
    /* _GetNcBtnMetrics of uxtheme: the size of the caption buttons less an edge on each side. */
    LONG edge = MetricsGet(SM_CXEDGE, dpi);
    m->ButtonWidth = (tool ? MetricsGet(SM_CXSMSIZE, dpi) : FrameThemedButtonWidth(dpi)) - 2 * edge;
    m->ButtonHeight = MetricsGet(tool ? SM_CYSMSIZE : SM_CYSIZE, dpi) - 2 * edge;
}

void FrameAdjustRect(RECT *r, DWORD style, DWORD ex_style, UINT dpi)
{
    FRAME_METRICS m;
    FrameGetMetrics(style, ex_style, dpi, &m);
    r->left -= m.Border + m.Edge;
    r->right += m.Border + m.Edge;
    r->top -= m.Top + m.Edge;
    r->bottom += m.Border + m.Edge;
}

void FrameClientFromWindow(RECT *r, DWORD style, DWORD ex_style, UINT dpi)
{
    FRAME_METRICS m;
    FrameGetMetrics(style, ex_style, dpi, &m);
    r->left += m.Border + m.Edge;
    r->right -= m.Border + m.Edge;
    r->top += m.Top + m.Edge;
    r->bottom -= m.Border + m.Edge;
    if (style & WS_VSCROLL)
        r->right -= MetricsGet(SM_CXVSCROLL, dpi);
    if (style & WS_HSCROLL)
        r->bottom -= MetricsGet(SM_CYHSCROLL, dpi);
    if (r->right < r->left)
        r->right = r->left;
    if (r->bottom < r->top)
        r->bottom = r->top;
}

/* a window whose WM_NCCALCSIZE took the whole frame as client area, as borderless toolkit windows do: no frame shows. */
BOOLEAN FrameClientOnly(const WND *w)
{
    return w->ClientRect.left == w->WindowRect.left && w->ClientRect.top == w->WindowRect.top &&
           w->ClientRect.right == w->WindowRect.right && w->ClientRect.bottom == w->WindowRect.bottom;
}

void FrameGetVisibleRect(WND *w, RECT *r)
{
    LONG width = w->WindowRect.right - w->WindowRect.left;
    LONG height = w->WindowRect.bottom - w->WindowRect.top;
    *r = (RECT){0, 0, width, height};
    if (!is_top_frame(w->Style))
        return;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndFrameDpi(w), &m);
    if (w->Style & WS_MAXIMIZE) {
        /*
         * a maximized window shows what of it is on the work area, where windows leaves its frame outside. a
         * borderless toolkit window, as sdl's, maximizes at the work area itself and shows whole.
         */
        if (WndIsTopLevel(w)) {
            RECT physical = w->WindowRect;
            DpiRectConvert(&physical, w->Awareness, DPI_PHYSICAL);
            RECT work = MonitorWorkOfRect(&physical);
            DpiRectConvert(&work, DPI_PHYSICAL, w->Awareness);
            RECT in = {W32Max(work.left - w->WindowRect.left, 0), W32Max(work.top - w->WindowRect.top, 0),
                       W32Min(work.right - w->WindowRect.left, width), W32Min(work.bottom - w->WindowRect.top, height)};
            if (in.right > in.left && in.bottom > in.top) {
                *r = in;
                return;
            }
        }
        *r = (RECT){m.Border, m.Border, width - m.Border, height - m.Border};
        return;
    }
    if (FrameClientOnly(w))
        return;
    r->left = m.VisibleInset;
    r->right = width - m.VisibleInset;
    r->bottom = height - m.VisibleInset;
}

static BOOLEAN has_minmax(WND *w)
{
    DWORD boxes = w->Style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    if (!WndCanMinimize(w))
        boxes &= ~(DWORD)WS_MINIMIZEBOX;
    return boxes && !(w->ExStyle & WS_EX_TOOLWINDOW);
}

static BOOLEAN has_help(WND *w)
{
    return (w->ExStyle & WS_EX_CONTEXTHELP) && !has_minmax(w);
}

/* window relative, right to left. */
void FrameGetButtonRect(WND *w, int button, RECT *r)
{
    *r = (RECT){0, 0, 0, 0};
    if (!FrameHasCaption(w->Style, w->ExStyle) || !(w->Style & WS_SYSMENU))
        return;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndFrameDpi(w), &m);
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    LONG top = vis.top;
    LONG bottom = (w->Style & WS_MAXIMIZE) ? m.Top : m.Top;
    LONG right = vis.right;
    int slot;
    switch (button) {
    case CAPTION_CLOSE:
        slot = 0;
        break;
    case CAPTION_MAX:
        if (!has_minmax(w))
            return;
        slot = 1;
        break;
    case CAPTION_MIN:
        if (!has_minmax(w) || !WndCanMinimize(w))
            return;
        slot = 2;
        break;
    case CAPTION_HELP:
        if (!has_help(w))
            return;
        slot = 1;
        break;
    default:
        return;
    }
    LONG bw = m.ButtonWidth;
    if (!is_top_frame(w->Style)) {
        /* _GetNcBtnMetrics of uxtheme: an edge apart and from the frame, centered on the caption. */
        LONG edge = MetricsGet(SM_CXEDGE, WndFrameDpi(w));
        right -= m.Border + edge;
        top = m.Border + (m.Caption - 1 - m.ButtonHeight) / 2;
        *r = (RECT){right - bw - slot * (bw + edge), top, right - slot * (bw + edge), top + m.ButtonHeight};
        return;
    }
    *r = (RECT){right - bw * (slot + 1), top, right - bw * slot, bottom};
}

static int button_at(WND *w, POINT wp)
{
    static const int buttons[] = {CAPTION_CLOSE, CAPTION_MAX, CAPTION_MIN, CAPTION_HELP};
    for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++) {
        RECT r;
        FrameGetButtonRect(w, buttons[i], &r);
        if (!RectEmpty(&r) && RectContains(&r, wp.x, wp.y))
            return buttons[i];
    }
    return CAPTION_NONE;
}

static LONG caption_buttons_left(WND *w)
{
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    LONG left = vis.right;
    static const int buttons[] = {CAPTION_CLOSE, CAPTION_MAX, CAPTION_MIN, CAPTION_HELP};
    for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++) {
        RECT r;
        FrameGetButtonRect(w, buttons[i], &r);
        if (!RectEmpty(&r) && r.left < left)
            left = r.left;
    }
    return left;
}

/*
 * the icon uxtheme shows in the caption of a child window, AcquireFrameIcon and _GetWindowIcon of uxtheme 26300: its
 * own, then its class's, then the logo of windows unless it has a modal dialog frame.
 */
static HICON child_icon(WND *w)
{
    if (!(w->Style & WS_SYSMENU) || (w->ExStyle & WS_EX_TOOLWINDOW))
        return nullptr;
    HICON icon = w->IconSmall ? w->IconSmall
                 : w->Icon    ? w->Icon
                 : w->Class && w->Class->IconSmall ? w->Class->IconSmall
                 : w->Class ? w->Class->Icon : nullptr;
    if (!icon && !(w->ExStyle & WS_EX_DLGMODALFRAME))
        icon = CursorSystemIcon(32517 /* IDI_WINLOGO */);
    return icon;
}

/* window relative: an edge in from the frame, centered on the caption less its last line. */
static BOOLEAN child_icon_rect(WND *w, const FRAME_METRICS *m, RECT *r)
{
    if (!child_icon(w))
        return FALSE;
    UINT dpi = WndFrameDpi(w);
    LONG cx = MetricsGet(SM_CXSMICON, dpi), cy = MetricsGet(SM_CYSMICON, dpi);
    LONG x = m->Border + MetricsGet(SM_CXEDGE, dpi), y = m->Border + (m->Caption - 1 - cy) / 2;
    *r = (RECT){x, y, x + cx, y + cy};
    return TRUE;
}

ULONG FrameGetDragRects(WND *w, RECT *rects, ULONG max)
{
    if (!max || !FrameHasCaption(w->Style, w->ExStyle) || !is_top_frame(w->Style))
        return 0;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndFrameDpi(w), &m);
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    LONG top = vis.top;
    if ((w->Style & WS_THICKFRAME) && !(w->Style & WS_MAXIMIZE))
        top += scale(4, WndFrameDpi(w)); /* leave the top resize strip to the window. */
    /* a client area that took the caption with WM_NCCALCSIZE answers its own hit tests there. */
    LONG bottom = W32Min(m.Top, w->ClientRect.top - w->WindowRect.top);
    rects[0] = (RECT){vis.left, top, caption_buttons_left(w), bottom};
    return RectEmpty(&rects[0]) ? 0 : 1;
}

/*
 * CThemeWnd::GetTitleBarInfoEx of uxtheme 26300 answers for a child window with a caption. it clears the first four
 * states after it set the one of the title bar, and calls a button that is neither empty nor disabled pressed.
 */
static void child_title_bar_info(WND *w, const FRAME_METRICS *m, TITLEBARINFOEX *info)
{
    LONG x = w->WindowRect.left, y = w->WindowRect.top;
    LONG width = w->WindowRect.right - w->WindowRect.left;
    info->rcTitleBar = (RECT){m->Border, m->Border, width - m->Border, m->Top - 1};
    RECT icon;
    if (child_icon_rect(w, m, &icon))
        info->rcTitleBar.left += icon.right - icon.left;
    RectOffset(&info->rcTitleBar, x, y);
    info->rgstate[1] = STATE_SYSTEM_INVISIBLE;
    static const int buttons[4] = {CAPTION_MIN, CAPTION_MAX, CAPTION_HELP, CAPTION_CLOSE};
    for (int i = 0; i < 4; i++) {
        RECT r;
        FrameGetButtonRect(w, buttons[i], &r);
        if (RectEmpty(&r)) {
            info->rgstate[2 + i] = STATE_SYSTEM_INVISIBLE;
            continue;
        }
        BOOLEAN enabled = !(w->Style & WS_DISABLED) &&
                          (buttons[i] == CAPTION_MAX   ? (w->Style & WS_MAXIMIZEBOX) != 0
                           : buttons[i] == CAPTION_MIN ? (w->Style & WS_MINIMIZEBOX) != 0
                           : buttons[i] == CAPTION_CLOSE ? !(w->Flags & WNDF_NOCLOSE)
                                                         : TRUE);
        info->rgstate[2 + i] = enabled ? STATE_SYSTEM_PRESSED : STATE_SYSTEM_UNAVAILABLE;
        RectOffset(&r, x, y);
        info->rgrect[2 + i] = r;
    }
}

/*
 * a window without a caption, xxxGetTitleBarInfoEx and xxxCalcCaptionButton of win32kfull 26300: the title bar is
 * invisible and empty, and each button gets the place a caption would give it inside the borders of the window, the
 * help button without WS_EX_CONTEXTHELP the window inside its borders. the dwm answers a top level window with a
 * thick frame with no buttons.
 */
static void no_caption_title_bar_info(WND *w, TITLEBARINFOEX *info)
{
    info->rgstate[0] = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_INVISIBLE;
    if (is_top_frame(w->Style) && (w->Style & WS_THICKFRAME))
        return;
    UINT dpi = WndFrameDpi(w);
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, dpi, &m);
    LONG b = (w->Style & WS_MINIMIZE) ? 3 : m.Border;
    RECT inside = {w->WindowRect.left + b, w->WindowRect.top + b, w->WindowRect.right - b, w->WindowRect.bottom - b};
    LONG cx = scale(22, dpi), cy = scale(22, dpi);
    /* minimize, maximize, help and close, by how many buttons they stand from the right. */
    static const int slots[4] = {2, 1, 1, 0};
    for (int i = 0; i < 4; i++) {
        RECT r = inside;
        if (i == 2 && !(w->ExStyle & WS_EX_CONTEXTHELP)) {
            info->rgrect[2 + i] = r;
            continue;
        }
        r.right -= slots[i] * cx;
        r = (RECT){r.right - cx - 1, r.top - 1, r.right + 1, r.top + cy + 1};
        if (w->ExStyle & WS_EX_LAYOUTRTL) {
            LONG span = r.right - r.left;
            r.left = w->WindowRect.left + w->WindowRect.right - r.right;
            r.right = r.left + span;
        }
        info->rgrect[2 + i] = r;
    }
}

void FrameGetTitleBarInfo(WND *w, TITLEBARINFOEX *info)
{
    memset(&info->rcTitleBar, 0, sizeof(*info) - sizeof(info->cbSize));
    LONG x = w->WindowRect.left, y = w->WindowRect.top;
    if (!FrameHasCaption(w->Style, w->ExStyle)) {
        no_caption_title_bar_info(w, info);
        return;
    }
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndFrameDpi(w), &m);
    if (!is_top_frame(w->Style)) {
        child_title_bar_info(w, &m, info);
        return;
    }
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    info->rcTitleBar = (RECT){vis.left + 1, vis.top + 1, vis.right - 1, m.Top};
    RectOffset(&info->rcTitleBar, x, y);
    info->rgstate[0] = STATE_SYSTEM_FOCUSABLE;
    if (!WndIsVisible(w))
        info->rgstate[0] |= STATE_SYSTEM_INVISIBLE;
    info->rgstate[1] = STATE_SYSTEM_INVISIBLE;
    /* the buttons in the order of the structure: minimize, maximize, help and close. */
    static const int buttons[4] = {CAPTION_MIN, CAPTION_MAX, CAPTION_HELP, CAPTION_CLOSE};
    for (int i = 0; i < 4; i++) {
        RECT r;
        FrameGetButtonRect(w, buttons[i], &r);
        if (RectEmpty(&r)) {
            info->rgstate[2 + i] = STATE_SYSTEM_INVISIBLE;
            continue;
        }
        BOOLEAN enabled = !(w->Style & WS_DISABLED) &&
                          (buttons[i] == CAPTION_MAX   ? (w->Style & WS_MAXIMIZEBOX) != 0
                           : buttons[i] == CAPTION_MIN ? (w->Style & WS_MINIMIZEBOX) != 0
                           : buttons[i] == CAPTION_CLOSE ? !(w->Flags & WNDF_NOCLOSE)
                                                         : TRUE);
        if (!enabled)
            info->rgstate[2 + i] |= STATE_SYSTEM_UNAVAILABLE;
        if (w->CaptionPressed == buttons[i])
            info->rgstate[2 + i] |= STATE_SYSTEM_PRESSED;
        RectOffset(&r, x, y);
        info->rgrect[2 + i] = r;
    }
}

/* ------------------------------------------------------------------ hit testing */

/*
 * the buttons of the caption of a child window, _GetNcBtnHitTestRect of uxtheme 26300: from the frame to the last line
 * of the caption, the close button out to the frame, the minimize and help buttons a pixel to the left, the maximize
 * and help buttons one to the right, and the system menu from the frame through the icon. HTNOWHERE off them.
 */
static LRESULT child_button_hit(WND *w, POINT wp, const FRAME_METRICS *m)
{
    if (wp.y < m->Border || wp.y >= m->Top - 1)
        return HTNOWHERE;
    LONG width = w->WindowRect.right - w->WindowRect.left;
    static const struct {
        int Button;
        LONG Left, Right;
        LRESULT Hit;
    } order[] = {{CAPTION_CLOSE, 1, 0, HTCLOSE}, {CAPTION_MIN, 1, 0, HTMINBUTTON}, {CAPTION_MAX, 0, 1, HTMAXBUTTON}};
    for (ULONG i = 0; i < RTL_NUMBER_OF(order); i++) {
        RECT r;
        FrameGetButtonRect(w, order[i].Button, &r);
        LONG right = order[i].Button == CAPTION_CLOSE ? width - m->Border : r.right + order[i].Right;
        if (!RectEmpty(&r) && wp.x >= r.left - order[i].Left && wp.x < right)
            return order[i].Hit;
    }
    RECT icon;
    if (child_icon_rect(w, m, &icon) && wp.x >= m->Border && wp.x <= icon.right)
        return HTSYSMENU;
    RECT help;
    FrameGetButtonRect(w, CAPTION_HELP, &help);
    if (!RectEmpty(&help) && wp.x >= help.left - 1 && wp.x <= help.right)
        return HTHELP;
    return HTNOWHERE;
}

static LRESULT themed_child_hit(WND *w, POINT wp, const FRAME_METRICS *m, UINT dpi);

LRESULT FrameHitTest(WND *w, POINT pt)
{
    if (!RectContains(&w->WindowRect, pt.x, pt.y))
        return HTNOWHERE;
    if (RectContains(&w->ClientRect, pt.x, pt.y))
        return HTCLIENT;
    if (w->Style & WS_MINIMIZE)
        return HTCAPTION;
    RECT bar;
    if (!(w->Style & WS_CHILD) && MenuBarArea(w, &bar) && RectContains(&bar, pt.x, pt.y))
        return HTMENU;
    UINT dpi = WndFrameDpi(w);
    POINT wp = {pt.x - w->WindowRect.left, pt.y - w->WindowRect.top};
    LONG width = w->WindowRect.right - w->WindowRect.left;
    LONG height = w->WindowRect.bottom - w->WindowRect.top;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, dpi, &m);

    /* scroll bars sit between the client area and the frame. */
    RECT inner = w->WindowRect;
    FrameClientFromWindow(&inner, w->Style & ~(WS_VSCROLL | WS_HSCROLL), w->ExStyle, dpi);
    if (RectContains(&inner, pt.x, pt.y)) {
        if ((w->Style & WS_VSCROLL) && pt.x >= w->ClientRect.right)
            return (w->Style & WS_HSCROLL) && pt.y >= w->ClientRect.bottom ? HTSIZE : HTVSCROLL;
        if ((w->Style & WS_HSCROLL) && pt.y >= w->ClientRect.bottom)
            return HTHSCROLL;
    }

    if (!is_top_frame(w->Style) && FrameHasCaption(w->Style, w->ExStyle) && FrameThemed()) {
        LRESULT hit = themed_child_hit(w, wp, &m, dpi);
        if (hit != HTNOWHERE)
            return hit;
    }

    if ((w->Style & WS_THICKFRAME) && !(w->Style & WS_MAXIMIZE)) {
        LONG edge = m.Border;
        LONG corner = scale(16, dpi);
        LONG top_edge = scale(4, dpi);
        BOOLEAN left = wp.x < edge, right = wp.x >= width - edge;
        BOOLEAN top = wp.y < top_edge, bottom = wp.y >= height - edge;
        if (top || bottom || left || right) {
            BOOLEAN near_left = wp.x < corner, near_right = wp.x >= width - corner;
            BOOLEAN near_top = wp.y < corner, near_bottom = wp.y >= height - corner;
            if ((top && near_left) || (left && near_top))
                return HTTOPLEFT;
            if ((top && near_right) || (right && near_top))
                return HTTOPRIGHT;
            if ((bottom && near_left) || (left && near_bottom))
                return HTBOTTOMLEFT;
            if ((bottom && near_right) || (right && near_bottom))
                return HTBOTTOMRIGHT;
            if (top)
                return HTTOP;
            if (bottom)
                return HTBOTTOM;
            return left ? HTLEFT : HTRIGHT;
        }
    }

    if (FrameHasCaption(w->Style, w->ExStyle) && wp.y < m.Top) {
        if (!is_top_frame(w->Style)) {
            LRESULT hit = child_button_hit(w, wp, &m);
            return hit != HTNOWHERE ? hit : HTCAPTION;
        }
        RECT vis;
        FrameGetVisibleRect(w, &vis);
        switch (button_at(w, wp)) {
        case CAPTION_CLOSE:
            return HTCLOSE;
        case CAPTION_MAX:
            return HTMAXBUTTON;
        case CAPTION_MIN:
            return HTMINBUTTON;
        case CAPTION_HELP:
            return HTHELP;
        }
        if ((w->Style & WS_SYSMENU) && (w->IconSmall || w->Icon || (w->Class && (w->Class->IconSmall || w->Class->Icon))) &&
            !(w->ExStyle & (WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW)) &&
            wp.x < vis.left + scale(32, dpi) && wp.y >= vis.top)
            return HTSYSMENU;
        if (wp.y >= vis.top && wp.x >= vis.left && wp.x < vis.right)
            return HTCAPTION;
    }
    return HTBORDER;
}

/* ------------------------------------------------------------------ the visual style */

static void *frame_alloc(void *ctx, size_t size)
{
    (void)ctx;
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, FRAME_TAG);
}

static void frame_free(void *ctx, void *p)
{
    (void)ctx;
    if (p)
        ExFreePool(p);
}

static const MimeAllocator g_frame_alloc = {frame_alloc, frame_free, nullptr};

/*
 * the classes of the style the frame and the menus read, light and dark. Window holds the frame uxtheme draws around
 * a child window with a caption, DWMWindow the caption buttons the dwm draws on a top level one.
 */
enum {
    CLASS_WINDOW,
    CLASS_WINDOW_DARK,
    CLASS_DWM,
    CLASS_DWM_DARK,
    CLASS_MENU,
    CLASS_MENU_DARK,
    CLASS_SCROLLBAR,
    CLASS_COUNT
};
/* the dark classes are named DarkMode::DWMWindow and DarkMode::Menu, the part before :: being the app. */
static const char *const g_class_apps[CLASS_COUNT] = {nullptr, "DarkMode", nullptr, "DarkMode", nullptr, "DarkMode", nullptr};
static const char *const g_class_names[CLASS_COUNT] = {"Window", "Window", "DWMWindow", "DWMWindow",
                                                       "Menu",   "Menu",   "ScrollBar"};

/* the coverage of a glyph image, to be tinted when drawn, and the color the image has itself. */
typedef struct GLYPH_MASK {
    struct GLYPH_MASK *Next;
    int32_t ResId, Left, Top;
    LONG Width, Height;
    COLORREF Color;
    PUCHAR Coverage;
} GLYPH_MASK;

static struct {
    BOOLEAN Tried;
    BOOLEAN Ready;
    PUCHAR File;
    MimeMsstyles Theme;
    int Class[CLASS_COUNT];
    GLYPH_MASK *Masks;
} g_theme;

static PUCHAR read_whole_file(PCWSTR path, SIZE_T *size_out)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    if (!NT_SUCCESS(ZwCreateFile(&h, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, nullptr, 0, FILE_SHARE_READ, FILE_OPEN,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, nullptr, 0)))
        return nullptr;
    FILE_STANDARD_INFORMATION info;
    PUCHAR data = nullptr;
    if (NT_SUCCESS(ZwQueryInformationFile(h, &iosb, &info, sizeof(info), FileStandardInformation)) &&
        info.EndOfFile.QuadPart > 0 && info.EndOfFile.QuadPart < 32 * 1024 * 1024) {
        SIZE_T size = (SIZE_T)info.EndOfFile.QuadPart;
        data = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, FRAME_TAG);
        LARGE_INTEGER off = {};
        if (data && (!NT_SUCCESS(ZwReadFile(h, nullptr, nullptr, nullptr, &iosb, data, (ULONG)size, &off, nullptr)) ||
                     iosb.Information != size)) {
            ExFreePool(data);
            data = nullptr;
        }
        if (data)
            *size_out = size;
    }
    ZwClose(h);
    return data;
}

static BOOLEAN load_theme(void)
{
    if (g_theme.Tried)
        return g_theme.Ready;
    g_theme.Tried = TRUE;
    SIZE_T size = 0;
    g_theme.File = read_whole_file(L"\\SystemRoot\\Resources\\Themes\\aero\\aero.msstyles", &size);
    if (!g_theme.File) {
        W32Trace("win32k: aero.msstyles not found, using the built in frame\n");
        return FALSE;
    }
    if (!mime_msstyles_open(&g_theme.Theme, g_theme.File, size, nullptr, 0, &g_frame_alloc)) {
        W32Trace("win32k: cannot parse aero.msstyles\n");
        return FALSE;
    }
    for (int i = 0; i < CLASS_COUNT; i++)
        g_theme.Class[i] = mime_msstyles_find_class(&g_theme.Theme, g_class_apps[i], g_class_names[i]);
    /* a style without dark classes shows the light ones. */
    if (g_theme.Class[CLASS_WINDOW_DARK] < 0)
        g_theme.Class[CLASS_WINDOW_DARK] = g_theme.Class[CLASS_WINDOW];
    if (g_theme.Class[CLASS_DWM] < 0)
        g_theme.Class[CLASS_DWM] = g_theme.Class[CLASS_WINDOW];
    if (g_theme.Class[CLASS_DWM_DARK] < 0)
        g_theme.Class[CLASS_DWM_DARK] = g_theme.Class[CLASS_DWM];
    if (g_theme.Class[CLASS_MENU_DARK] < 0)
        g_theme.Class[CLASS_MENU_DARK] = g_theme.Class[CLASS_MENU];
    g_theme.Ready = g_theme.Class[CLASS_WINDOW] >= 0;
    return g_theme.Ready;
}

/* a color of the style, as a COLORREF. */
static BOOLEAN theme_color(int which, int part, int state, int prop, COLORREF *out)
{
    uint32_t c;
    if (!load_theme() || MetricsHighContrast() || g_theme.Class[which] < 0 ||
        !mime_msstyles_get_color(&g_theme.Theme, g_theme.Class[which], part, state, prop, &c))
        return FALSE;
    *out = (COLORREF)c;
    return TRUE;
}

BOOLEAN FrameThemeColor(BOOLEAN menu, BOOLEAN dark, int part, int state, int prop, COLORREF *out)
{
    return theme_color(menu ? (dark ? CLASS_MENU_DARK : CLASS_MENU) : (dark ? CLASS_DWM_DARK : CLASS_DWM), part, state,
                       prop, out);
}

static void draw_glyph(SURFACE *s, const REGION *clip, const GLYPH_MASK *mask, const RECT *button, ULONG pixel);

/* the pixels of an image of the style, b, g, r and a with the colors multiplied by the alpha. */
static PUCHAR decode_image(const MimeMsstylesImage *img, MimePngInfo *info)
{
    LONG width = img->src.right - img->src.left, height = img->src.bottom - img->src.top;
    if (!mime_png_info(img->data, img->size, info) || width <= 0 || height <= 0 || info->width > 1024 ||
        info->height > 4096 || img->src.right > (LONG)info->width || img->src.bottom > (LONG)info->height)
        return nullptr;
    PUCHAR argb = frame_alloc(nullptr, (SIZE_T)info->width * info->height * 4);
    if (argb && mime_png_decode(img->data, img->size, &g_frame_alloc, MIME_PNG_PREMULTIPLIED, argb,
                                (size_t)info->width * 4) != MIME_PNG_OK) {
        frame_free(nullptr, argb);
        argb = nullptr;
    }
    return argb;
}

/* the glyph of a part of a class for a state at a dpi, as 8 bit coverage. */
static const GLYPH_MASK *class_glyph(int which, int part, int state, UINT dpi)
{
    MimeMsstylesImage img;
    if (!load_theme() || g_theme.Class[which] < 0 ||
        !mime_msstyles_get_image(&g_theme.Theme, g_theme.Class[which], part, state, dpi, 0, 0, MIME_MSSTYLES_GLYPH, &img))
        return nullptr;
    for (GLYPH_MASK *m = g_theme.Masks; m; m = m->Next)
        if (m->ResId == img.res_id && m->Top == img.src.top && m->Left == img.src.left)
            return m;
    MimePngInfo info;
    LONG width = img.src.right - img.src.left, height = img.src.bottom - img.src.top;
    PUCHAR argb = decode_image(&img, &info);
    GLYPH_MASK *mask = argb ? frame_alloc(nullptr, sizeof(*mask) + (SIZE_T)width * height) : nullptr;
    if (!mask) {
        frame_free(nullptr, argb);
        return nullptr;
    }
    mask->ResId = img.res_id;
    mask->Left = img.src.left;
    mask->Top = img.src.top;
    mask->Width = width;
    mask->Height = height;
    mask->Color = 0;
    mask->Coverage = (PUCHAR)(mask + 1);
    UCHAR most = 0;
    for (LONG y = 0; y < height; y++)
        for (LONG x = 0; x < width; x++) {
            const UCHAR *p = &argb[((SIZE_T)(img.src.top + y) * info.width + img.src.left + x) * 4];
            mask->Coverage[y * width + x] = p[3];
            if (p[3] > most) {
                most = p[3];
                mask->Color = RGB(p[2] * 255 / p[3], p[1] * 255 / p[3], p[0] * 255 / p[3]);
            }
        }
    frame_free(nullptr, argb);
    mask->Next = g_theme.Masks;
    g_theme.Masks = mask;
    return mask;
}

static const GLYPH_MASK *glyph_mask(BOOLEAN dark, int part, int state, UINT dpi)
{
    return class_glyph(dark ? CLASS_DWM_DARK : CLASS_DWM, part, state, dpi);
}

/* the color at the middle of the background image of a part, which a part of one color is filled with. */
typedef struct PART_FILL {
    struct PART_FILL *Next;
    int32_t ResId, Left, Top;
    COLORREF Color;
} PART_FILL;

static PART_FILL *g_fills;

static BOOLEAN class_fill(int which, int part, int state, UINT dpi, COLORREF *out)
{
    MimeMsstylesImage img;
    if (!load_theme() || MetricsHighContrast() || g_theme.Class[which] < 0)
        return FALSE;
    /* a part made of two colors and no bitmap, as the style of mime has them. */
    MimeMsstylesColorImage colors;
    if (mime_msstyles_get_color_image(&g_theme.Theme, g_theme.Class[which], part, state, &colors)) {
        *out = RGB((colors.fill >> 16) & 0xFF, (colors.fill >> 8) & 0xFF, colors.fill & 0xFF);
        return TRUE;
    }
    if (!mime_msstyles_get_image(&g_theme.Theme, g_theme.Class[which], part, state, dpi, 0, 0, 0, &img))
        return FALSE;
    for (PART_FILL *f = g_fills; f; f = f->Next)
        if (f->ResId == img.res_id && f->Top == img.src.top && f->Left == img.src.left) {
            *out = f->Color;
            return TRUE;
        }
    MimePngInfo info;
    PUCHAR argb = decode_image(&img, &info);
    PART_FILL *f = argb ? frame_alloc(nullptr, sizeof(*f)) : nullptr;
    if (!f) {
        frame_free(nullptr, argb);
        return FALSE;
    }
    const UCHAR *p = &argb[((SIZE_T)((img.src.top + img.src.bottom) / 2) * info.width + (img.src.left + img.src.right) / 2) * 4];
    f->ResId = img.res_id;
    f->Left = img.src.left;
    f->Top = img.src.top;
    f->Color = RGB(p[2], p[1], p[0]);
    f->Next = g_fills;
    g_fills = f;
    frame_free(nullptr, argb);
    *out = f->Color;
    return TRUE;
}

/*
 * a part of the ScrollBar class in a state: the parts of the style are of one color each, with an arrow over the
 * buttons. FALSE when the style has none, and the caller draws the classic one.
 */
BOOLEAN FrameDrawScrollPart(SURFACE *s, const REGION *clip, const RECT *r, int part, int state, UINT dpi)
{
    COLORREF fill;
    if (!class_fill(CLASS_SCROLLBAR, part, state, dpi, &fill))
        return FALSE;
    DrawFillRect(s, clip, r, ColorToPixel(fill));
    const GLYPH_MASK *mask = part == 1 ? class_glyph(CLASS_SCROLLBAR, part, state, dpi) : nullptr;
    if (mask) {
        REGION inside;
        RgnInit(&inside);
        RgnCombineRect(&inside, clip, r, RGN_AND);
        draw_glyph(s, &inside, mask, r, ColorToPixel(mask->Color));
        RgnFree(&inside);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ painting */

/*
 * what the frame draws when the style has no value: the caption of windows 11, measured on windows 11 26100. the text
 * of an inactive window is 40% of the active one over the caption.
 */
#define CAPTION_COLOR RGB(0xF3, 0xF3, 0xF3)
#define CAPTION_DARK RGB(0x20, 0x20, 0x20)
#define TEXT_ACTIVE RGB(0x00, 0x00, 0x00)
#define TEXT_ACTIVE_DARK RGB(0xFF, 0xFF, 0xFF)
#define TEXT_INACTIVE RGB(0x91, 0x91, 0x91)
#define TEXT_INACTIVE_DARK RGB(0x79, 0x79, 0x79)

/* dwm color attributes are stored as the value plus one, so zero means unset. */
static BOOLEAN dwm_color(WND *w, PCWSTR name, ULONG *out)
{
    ULONG_PTR v = (ULONG_PTR)PropGetData(w, PropAtom(name));
    if (!v)
        return FALSE;
    *out = (ULONG)(v - 1);
    return TRUE;
}

static BOOLEAN dark_frame(WND *w)
{
    return PropGetData(w, PropAtom(L"DwmDarkMode")) != nullptr;
}

BOOLEAN FrameIsDark(WND *w)
{
    return dark_frame(w);
}

/* the visual style is there: the menu bar takes the look of its Menu class instead of the flat system colors. */
BOOLEAN FrameThemed(void)
{
    return load_theme() && !MetricsHighContrast();
}

typedef struct FRAME_COLORS {
    ULONG Caption, Border, TextActive, TextInactive;
} FRAME_COLORS;

/* the resize border takes the window background, as the app paints over the caption. */
static BOOLEAN background_color(WND *w, ULONG *out)
{
    HBRUSH b = w->Class ? w->Class->Background : nullptr;
    if ((ULONG_PTR)b && (ULONG_PTR)b <= COLOR_MENUBAR + 1)
        b = MetricsSysColorBrush((int)(ULONG_PTR)b - 1);
    BRUSH *brush = b ? (BRUSH *)GdiLookup(b, GDI_TYPE_BRUSH) : nullptr;
    if (!brush)
        return FALSE;
    *out = brush->Color;
    return TRUE;
}

static void frame_colors(WND *w, FRAME_COLORS *c)
{
    BOOLEAN dark = dark_frame(w);
    if (MetricsHighContrast()) {
        /* the caption and its buttons are drawn in the colors of the system. */
        c->Caption = c->Border = MetricsSysColor(COLOR_ACTIVECAPTION);
        c->TextActive = MetricsSysColor(COLOR_CAPTIONTEXT);
        c->TextInactive = MetricsSysColor(COLOR_INACTIVECAPTIONTEXT);
        return;
    }
    /* the dwm of windows 11 draws the caption in its own colors, not the ones of the style. */
    c->Caption = dark ? CAPTION_DARK : CAPTION_COLOR;
    c->Border = c->Caption;
    c->TextActive = dark ? TEXT_ACTIVE_DARK : TEXT_ACTIVE;
    c->TextInactive = dark ? TEXT_INACTIVE_DARK : TEXT_INACTIVE;
    ULONG custom;
    BOOLEAN caption_set = dwm_color(w, L"DwmCaptionColor", &custom);
    if (caption_set)
        c->Caption = c->Border = custom;
    if (dwm_color(w, L"DwmTextColor", &custom))
        c->TextActive = c->TextInactive = custom;
    else if (caption_set) {
        /* a caption of another color takes the text that reads on it. */
        ULONG cap = c->Caption;
        ULONG lum = ((cap & 0xFF) * 299 + ((cap >> 8) & 0xFF) * 587 + ((cap >> 16) & 0xFF) * 114) / 1000;
        c->TextActive = lum < 128 ? TEXT_ACTIVE_DARK : TEXT_ACTIVE;
        c->TextInactive = lum < 128 ? TEXT_INACTIVE_DARK : TEXT_INACTIVE;
    }
    if (!caption_set && background_color(w, &custom))
        c->Border = custom;
}

static FONT_INSTANCE *caption_font(UINT dpi)
{
    static FONT_INSTANCE *font;
    static UINT font_dpi;
    if (!font || font_dpi != dpi) {
        LOGFONTW lf = W32CaptionFontForDpi(dpi);
        FONT_INSTANCE *f = FontRealize(&lf, dpi);
        if (f) {
            font = f;
            font_dpi = dpi;
        }
    }
    return font;
}

void FrameDrawText(SURFACE *s, const REGION *clip, FONT_INSTANCE *fi, LONG x, LONG baseline, PCWSTR text,
                   ULONG count, LONG max_right, ULONG pixel)
{
    for (ULONG i = 0; i < count; i++) {
        const GLYPH *g = FontGetGlyph(fi, text[i]);
        if (!g)
            continue;
        if (x + g->Advance > max_right)
            break;
        DrawGlyph(s, clip, x, baseline, g, pixel);
        x += g->Advance;
    }
}

/* like the windows caption, a title that does not fit ends with "...". */
static ULONG fit_title(FONT_INSTANCE *fi, PCWSTR text, ULONG count, LONG width, PWSTR out, ULONG cap)
{
    if (FontTextWidth(fi, text, count, 0) <= width) {
        ULONG n = count < cap ? count : cap;
        memcpy(out, text, n * sizeof(WCHAR));
        return n;
    }
    LONG dots = FontTextWidth(fi, L"...", 3, 0);
    ULONG n = 0;
    LONG used = 0;
    while (n < count && n + 3 < cap) {
        const GLYPH *g = FontGetGlyph(fi, text[n]);
        LONG adv = g ? g->Advance : 0;
        if (used + adv + dots > width)
            break;
        used += adv;
        out[n] = text[n];
        n++;
    }
    out[n++] = L'.';
    out[n++] = L'.';
    out[n++] = L'.';
    return n;
}

/* a glyph of the style tinted with a color. */
static void draw_glyph(SURFACE *s, const REGION *clip, const GLYPH_MASK *mask, const RECT *button, ULONG pixel)
{
    LONG x = button->left + (button->right - button->left - mask->Width) / 2;
    LONG y = button->top + (button->bottom - button->top - mask->Height) / 2;
    DrawCoverage(s, clip, x, y, mask->Coverage, mask->Width, mask->Height, mask->Width, pixel);
}

/* fallback glyphs when the style is missing: 1 pixel strokes scaled to the dpi. */
static void draw_fallback_glyph(SURFACE *s, const REGION *clip, int button, BOOLEAN maximized, const RECT *br,
                                ULONG pixel, UINT dpi)
{
    LONG size = scale(10, dpi);
    LONG cx = (br->left + br->right) / 2, cy = (br->top + br->bottom) / 2;
    LONG l = cx - size / 2, t = cy - size / 2;
    LONG th = W32Max(1, scale(1, dpi));
    switch (button) {
    case CAPTION_MIN: {
        RECT r = {l, cy, l + size, cy + th};
        DrawFillRect(s, clip, &r, pixel);
        break;
    }
    case CAPTION_MAX: {
        RECT edges[4] = {{l, t, l + size, t + th}, {l, t + size - th, l + size, t + size},
                         {l, t, l + th, t + size}, {l + size - th, t, l + size, t + size}};
        if (maximized)
            for (int i = 0; i < 4; i++)
                RectOffset(&edges[i], 0, scale(2, dpi));
        for (int i = 0; i < 4; i++)
            DrawFillRect(s, clip, &edges[i], pixel);
        break;
    }
    case CAPTION_CLOSE:
    case CAPTION_HELP:
        for (LONG i = 0; i < size; i++) {
            RECT a = {l + i, t + i, l + i + th, t + i + th};
            RECT b = {l + size - 1 - i, t + i, l + size - 1 - i + th, t + i + th};
            DrawFillRect(s, clip, &a, pixel);
            DrawFillRect(s, clip, &b, pixel);
        }
        break;
    }
}

static void draw_button(WND *w, SURFACE *s, const REGION *clip, POINT origin, int button, UINT dpi, BOOLEAN active,
                        const FRAME_COLORS *colors)
{
    RECT br;
    FrameGetButtonRect(w, button, &br);
    if (RectEmpty(&br))
        return;
    RectOffset(&br, origin.x, origin.y);
    BOOLEAN disabled = (button == CAPTION_MAX && !(w->Style & WS_MAXIMIZEBOX)) ||
                       (button == CAPTION_MIN && !(w->Style & WS_MINIMIZEBOX)) ||
                       (button == CAPTION_CLOSE && (w->Flags & WNDF_NOCLOSE)) || (w->Style & WS_DISABLED);
    BOOLEAN hot = !disabled && w->CaptionHot == button && (!w->CaptionTracking || w->CaptionTracking == button);
    BOOLEAN pressed = hot && w->CaptionPressed == button;
    BOOLEAN maximized = (w->Style & WS_MAXIMIZE) != 0;
    BOOLEAN dark = dark_frame(w);
    int part;
    switch (button) {
    case CAPTION_CLOSE:
        part = WP_CLOSEBUTTON;
        break;
    case CAPTION_HELP:
        part = WP_HELPBUTTON;
        break;
    case CAPTION_MAX:
        part = maximized ? WP_RESTOREBUTTON : WP_MAXBUTTON;
        break;
    default:
        part = WP_MINBUTTON;
        break;
    }
    /* normal, hot, pushed, disabled, inactive. */
    int state = disabled ? 4 : pressed ? 3 : hot ? 2 : active ? 1 : 5;
    COLORREF fill, glyph_color;
    if (MetricsHighContrast() && (state == 2 || state == 3))
        DrawFillRect(s, clip, &br, ColorToPixel(MetricsSysColor(COLOR_HIGHLIGHT)));
    else if ((state == 2 || state == 3) && FrameThemeColor(FALSE, dark, part, state, MIME_TMT_FILLCOLOR, &fill))
        DrawFillRect(s, clip, &br, ColorToPixel(fill));
    glyph_color = disabled ? RGB(0xC0, 0xC0, 0xC0) : active ? colors->TextActive : colors->TextInactive;
    if (MetricsHighContrast())
        glyph_color = disabled ? MetricsSysColor(COLOR_GRAYTEXT)
                      : hot    ? MetricsSysColor(COLOR_HIGHLIGHTTEXT)
                               : glyph_color;
    else if (state >= 2 && state <= 4)
        FrameThemeColor(FALSE, dark, part, state, MIME_TMT_TEXTCOLOR, &glyph_color);
    const GLYPH_MASK *mask = glyph_mask(dark, part, state, dpi);
    if (mask)
        draw_glyph(s, clip, mask, &br, ColorToPixel(glyph_color));
    else
        draw_fallback_glyph(s, clip, button, maximized, &br, ColorToPixel(glyph_color), dpi);
}

static void draw_ring(SURFACE *s, const REGION *clip, RECT r, LONG width, ULONG pixel)
{
    if (width <= 0)
        return;
    RECT edges[4] = {{r.left, r.top, r.right, r.top + width},
                     {r.left, r.bottom - width, r.right, r.bottom},
                     {r.left, r.top + width, r.left + width, r.bottom - width},
                     {r.right - width, r.top + width, r.right, r.bottom - width}};
    for (int i = 0; i < 4; i++)
        DrawFillRect(s, clip, &edges[i], pixel);
}

/* the images of the style decoded once, as surfaces to blend from. */
typedef struct PART_SURFACE {
    struct PART_SURFACE *Next;
    int32_t ResId;
    SURFACE *Surface;
} PART_SURFACE;

static PART_SURFACE *g_part_surfaces;

static SURFACE *image_surface(const MimeMsstylesImage *img)
{
    for (PART_SURFACE *p = g_part_surfaces; p; p = p->Next)
        if (p->ResId == img->res_id)
            return p->Surface;
    MimePngInfo info;
    PUCHAR argb = decode_image(img, &info);
    PART_SURFACE *p = argb ? frame_alloc(nullptr, sizeof(*p)) : nullptr;
    SURFACE *surface = p ? SurfaceWrap((LONG)info.width, (LONG)info.height, 32, argb, (LONG)info.width * 4, nullptr) : nullptr;
    if (!surface) {
        frame_free(nullptr, p);
        frame_free(nullptr, argb);
        return nullptr;
    }
    p->ResId = img->res_id;
    p->Surface = surface;
    p->Next = g_part_surfaces;
    g_part_surfaces = p;
    return surface;
}

static void blend_cell(SURFACE *dst, const REGION *clip, LONG x, LONG y, LONG w, LONG h, SURFACE *src, LONG sx, LONG sy,
                       LONG sw, LONG sh)
{
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0)
        return;
    RECT to = {x, y, x + w, y + h}, from = {sx, sy, sx + sw, sy + sh};
    AlphaBlendSurface(dst, clip, &to, src, &from, (BLENDFUNCTION){AC_SRC_OVER, 0, 255, AC_SRC_ALPHA});
}

/*
 * an image of the style in a rectangle as DrawThemeBackground draws it: at its size and aligned, or stretched between
 * its sizing margins. glyphs are always at their size.
 */
static void draw_image(SURFACE *s, const REGION *clip, const MimeMsstylesImage *img, const RECT *rect, BOOLEAN glyph)
{
    SURFACE *src = image_surface(img);
    LONG x = rect->left, y = rect->top, w = rect->right - rect->left, h = rect->bottom - rect->top;
    if (!src || w <= 0 || h <= 0)
        return;
    LONG sx = img->src.left, sy = img->src.top, sw = img->src.right - sx, sh = img->src.bottom - sy;
    if (glyph || img->sizing_type == MIME_ST_TRUESIZE) {
        LONG dw = glyph || !img->draw_width ? sw : img->draw_width, dh = glyph || !img->draw_height ? sh : img->draw_height;
        LONG dx = img->halign == MIME_HA_CENTER ? x + (w - dw) / 2 : img->halign == MIME_HA_RIGHT ? x + w - dw : x;
        LONG dy = img->valign == MIME_VA_CENTER ? y + (h - dh) / 2 : img->valign == MIME_VA_BOTTOM ? y + h - dh : y;
        blend_cell(s, clip, dx, dy, dw, dh, src, sx, sy, sw, sh);
        return;
    }
    LONG scale_by = img->margin_scale > 0 ? img->margin_scale : 1;
    LONG sl = img->sizing_margins.left, sr = img->sizing_margins.right;
    LONG st = img->sizing_margins.top, sb = img->sizing_margins.bottom;
    if (sl < 0 || sr < 0 || st < 0 || sb < 0 || sl + sr > sw || st + sb > sh)
        sl = sr = st = sb = 0;
    LONG l = sl * scale_by, r = sr * scale_by, t = st * scale_by, b = sb * scale_by;
    if (l + r > w) {
        l = w * l / (l + r);
        r = w - l;
    }
    if (t + b > h) {
        t = h * t / (t + b);
        b = h - t;
    }
    LONG cw = w - l - r, ch = h - t - b, scw = sw - sl - sr, sch = sh - st - sb;
    blend_cell(s, clip, x, y, l, t, src, sx, sy, sl, st);
    blend_cell(s, clip, x + l, y, cw, t, src, sx + sl, sy, scw, st);
    blend_cell(s, clip, x + w - r, y, r, t, src, sx + sw - sr, sy, sr, st);
    blend_cell(s, clip, x, y + t, l, ch, src, sx, sy + st, sl, sch);
    blend_cell(s, clip, x + w - r, y + t, r, ch, src, sx + sw - sr, sy + st, sr, sch);
    blend_cell(s, clip, x, y + h - b, l, b, src, sx, sy + sh - sb, sl, sb);
    blend_cell(s, clip, x + l, y + h - b, cw, b, src, sx + sl, sy + sh - sb, scw, sb);
    blend_cell(s, clip, x + w - r, y + h - b, r, b, src, sx + sw - sr, sy + sh - sb, sr, sb);
    if (!img->border_only)
        blend_cell(s, clip, x + l, y + t, cw, ch, src, sx + sl, sy + st, scw, sch);
}

static BOOLEAN window_part_image(int part, int state, UINT dpi, const RECT *r, MimeMsstylesImage *img)
{
    return load_theme() && !MetricsHighContrast() && g_theme.Class[CLASS_WINDOW] >= 0 &&
           mime_msstyles_get_image(&g_theme.Theme, g_theme.Class[CLASS_WINDOW], part, state, dpi,
                                   r ? r->right - r->left : 0, r ? r->bottom - r->top : 0, 0, img);
}

/* a part of the Window class as DrawThemeBackground draws it: the background, then the glyph image over it. */
static BOOLEAN draw_window_part(SURFACE *s, const REGION *clip, int part, int state, UINT dpi, const RECT *r)
{
    MimeMsstylesImage img;
    if (!window_part_image(part, state, dpi, r, &img))
        return FALSE;
    draw_image(s, clip, &img, r, FALSE);
    if (img.glyph_type != MIME_GT_IMAGEGLYPH)
        return TRUE;
    RECT content = {r->left + img.content_margins.left, r->top + img.content_margins.top,
                    r->right - img.content_margins.right, r->bottom - img.content_margins.bottom};
    if (mime_msstyles_get_image(&g_theme.Theme, g_theme.Class[CLASS_WINDOW], part, state, dpi, r->right - r->left,
                                r->bottom - r->top, MIME_MSSTYLES_GLYPH, &img))
        draw_image(s, clip, &img, &content, TRUE);
    return TRUE;
}

/* where a point of a stretched or tiled image falls in its frame, along one axis. */
static LONG grid_source(LONG d, LONG size, LONG from, LONG to, LONG a, LONG b, BOOLEAN tile)
{
    LONG span = to - from, sa = a, sb = b;
    if (a + b > size) {
        a = size * a / (a + b);
        b = size - a;
    }
    if (d < a)
        return from + d * sa / W32Max(a, 1);
    if (d >= size - b)
        return to - 1 - (size - 1 - d) * sb / W32Max(b, 1);
    LONG mid = span - sa - sb, room = size - a - b;
    if (mid <= 0 || room <= 0)
        return from + sa;
    return from + sa + (tile ? (d - a) % mid : (d - a) * mid / room);
}

/*
 * HitTest9Grid of uxtheme 26300 for a fixed frame: on the border of a box less its margins, inclusive at the left
 * and the top. a left margin of 0 beside a right one, or a top margin of 0 above a bottom one, has no edge.
 */
static BOOLEAN grid_border(POINT p, LONG width, LONG height, const MimeMsstylesMargins *mm)
{
    if (!(mm->left == 0 && mm->right != 0) && p.x <= mm->left)
        return TRUE;
    if (p.x >= width - mm->right)
        return TRUE;
    if ((mm->top != 0 || mm->bottom == 0) && p.y <= mm->top)
        return TRUE;
    return p.y >= height - mm->bottom;
}

/*
 * a sizable caption, HitTestTemplate and _HitTestResizingTemplate of uxtheme 26300: the opaque pixels of the sizing
 * template of the style, laid over the caption as it is drawn, resize the window, by the content margins of the
 * template from its edges, inclusive at the left and the top. anywhere else is the caption.
 */
static LRESULT caption_template_hit(POINT wp, LONG width, LONG top, BOOLEAN tool, UINT dpi)
{
    MimeMsstylesImage img;
    RECT caption = {0, 0, width, top};
    if (!window_part_image(tool ? WP_SMALLCAPTIONSIZINGTEMPLATE : WP_CAPTIONSIZINGTEMPLATE, 1, dpi, &caption, &img))
        return HTCAPTION;
    SURFACE *src = image_surface(&img);
    if (!src)
        return HTCAPTION;
    LONG k = img.margin_scale > 0 ? img.margin_scale : 1;
    BOOLEAN tile = img.sizing_type == MIME_ST_TILE;
    LONG sx = grid_source(wp.x, width, img.src.left, img.src.right, img.sizing_margins.left * k,
                          img.sizing_margins.right * k, tile);
    LONG sy = grid_source(wp.y, top, img.src.top, img.src.bottom, img.sizing_margins.top * k,
                          img.sizing_margins.bottom * k, tile);
    if (!(SurfaceGetPixel(src, sx, sy) >> 24))
        return HTCAPTION;
    LONG left = W32MulDiv(img.content_margins.left, (LONG)dpi, 96), right = W32MulDiv(img.content_margins.right, (LONG)dpi, 96);
    LONG up = W32MulDiv(img.content_margins.top, (LONG)dpi, 96);
    BOOLEAN on_left = wp.x <= left, on_right = !on_left && wp.x >= width - right;
    if (wp.y <= up)
        return on_left ? HTTOPLEFT : on_right ? HTTOPRIGHT : HTTOP;
    return on_left ? HTLEFT : on_right ? HTRIGHT : HTCAPTION;
}

/*
 * CThemeWnd::NcHitTest and NcBackgroundHitTest of uxtheme 26300 for a child window with a caption: the buttons and the
 * system menu, then the part of the frame under the point. a sizable caption follows its template, the sides are
 * the edges whole and the bottom has corners as wide as a scroll bar. a fixed frame is a border on the sizing
 * margins of its part laid over the window and a client area inside them, and its caption is the caption all over.
 * HTNOWHERE inside the frame, which user32 answers as for any window.
 */
static LRESULT themed_child_hit(WND *w, POINT wp, const FRAME_METRICS *m, UINT dpi)
{
    LONG width = w->WindowRect.right - w->WindowRect.left, height = w->WindowRect.bottom - w->WindowRect.top;
    BOOLEAN tool = (w->ExStyle & WS_EX_TOOLWINDOW) != 0;
    BOOLEAN sizable = (w->Style & WS_THICKFRAME) && !(w->Style & (WS_MINIMIZE | WS_MAXIMIZE));
    LRESULT hit = child_button_hit(w, wp, m);
    if (hit != HTNOWHERE)
        return hit;
    RECT inner = {m->Border, m->Top, width - m->Border, height - m->Border};
    if (RectContains(&inner, wp.x, wp.y))
        return HTNOWHERE;
    if (wp.y < m->Top)
        return sizable ? caption_template_hit(wp, width, m->Top, tool, dpi) : HTCAPTION;
    if (sizable) {
        if (wp.y >= height - m->Border) {
            LONG corner = MetricsGet(SM_CXVSCROLL, dpi);
            return wp.x <= corner ? HTBOTTOMLEFT : wp.x >= width - corner ? HTBOTTOMRIGHT : HTBOTTOM;
        }
        return wp.x < m->Border ? HTLEFT : HTRIGHT;
    }
    int part = wp.y >= height - m->Border ? WP_FRAMELEFT + 2 : wp.x < m->Border ? WP_FRAMELEFT : WP_FRAMELEFT + 1;
    MimeMsstylesImage img;
    if (!window_part_image(tool ? part - WP_FRAMELEFT + WP_SMALLFRAMELEFT : part, 1, dpi, nullptr, &img))
        return HTBORDER;
    LONG k = img.margin_scale > 0 ? img.margin_scale : 1;
    MimeMsstylesMargins mm = {img.sizing_margins.left * k, img.sizing_margins.right * k, img.sizing_margins.top * k,
                              img.sizing_margins.bottom * k};
    return grid_border(wp, width, height, &mm) ? HTBORDER : HTCLIENT;
}

/*
 * the frame uxtheme draws around a child window with a caption, CThemeWnd::NcPaint and NcPaintCaption of uxtheme
 * 26300: the frame parts, the caption over them, the caption buttons, the title and the icon. FALSE without the style.
 */
static BOOLEAN paint_child_caption(WND *w, SURFACE *s, const REGION *clip, POINT origin, const FRAME_METRICS *m,
                                   UINT dpi, BOOLEAN active)
{
    BOOLEAN tool = (w->ExStyle & WS_EX_TOOLWINDOW) != 0;
    BOOLEAN minimized = (w->Style & WS_MINIMIZE) != 0, maximized = (w->Style & WS_MAXIMIZE) != 0;
    LONG width = w->WindowRect.right - w->WindowRect.left, height = w->WindowRect.bottom - w->WindowRect.top;
    int caption_part = tool ? WP_SMALLCAPTION : minimized ? WP_MINCAPTION : maximized ? WP_MAXCAPTION : WP_CAPTION;
    RECT caption = {origin.x, origin.y, origin.x + width, origin.y + (minimized ? height : m->Top)};
    MimeMsstylesImage img;
    if (!FrameHasCaption(w->Style, w->ExStyle) || !window_part_image(caption_part, 1, dpi, &caption, &img))
        return FALSE;
    int frame_state = active ? 1 : 2;
    if (!minimized && !maximized) {
        RECT parts[3] = {{0, m->Top, m->Border, height - m->Border},
                         {width - m->Border, m->Top, width, height - m->Border},
                         {0, height - m->Border, width, height}};
        for (int i = 0; i < 3; i++) {
            RectOffset(&parts[i], origin.x, origin.y);
            draw_window_part(s, clip, (tool ? WP_SMALLFRAMELEFT : WP_FRAMELEFT) + i, frame_state, dpi, &parts[i]);
        }
    }
    draw_window_part(s, clip, caption_part, frame_state, dpi, &caption);

    /* the buttons: normal, hot, pushed and disabled, then the same four for an inactive window. */
    static const int buttons[] = {CAPTION_CLOSE, CAPTION_MAX, CAPTION_MIN, CAPTION_HELP};
    LONG text_right = width - m->Border;
    for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++) {
        RECT br;
        FrameGetButtonRect(w, buttons[i], &br);
        if (RectEmpty(&br))
            continue;
        text_right = W32Min(text_right, br.left);
        RectOffset(&br, origin.x, origin.y);
        int part = buttons[i] == CAPTION_CLOSE ? (tool ? WP_SMALLCLOSEBUTTON : WP_CLOSEBUTTON)
                   : buttons[i] == CAPTION_MAX ? (maximized ? WP_RESTOREBUTTON : WP_MAXBUTTON)
                   : buttons[i] == CAPTION_MIN ? (minimized ? WP_RESTOREBUTTON : WP_MINBUTTON)
                                               : WP_HELPBUTTON;
        BOOLEAN disabled = (buttons[i] == CAPTION_MAX && !(w->Style & WS_MAXIMIZEBOX)) ||
                           (buttons[i] == CAPTION_MIN && !(w->Style & WS_MINIMIZEBOX)) ||
                           (buttons[i] == CAPTION_CLOSE && (w->Flags & WNDF_NOCLOSE)) || (w->Style & WS_DISABLED);
        BOOLEAN hot = !disabled && w->CaptionHot == buttons[i] && (!w->CaptionTracking || w->CaptionTracking == buttons[i]);
        int state = disabled ? 4 : hot && w->CaptionPressed == buttons[i] ? 3 : hot ? 2 : 1;
        draw_window_part(s, clip, part, state + 4 * (frame_state - 1), dpi, &br);
    }

    /* the title between the icon or the frame and the buttons, inside the caption margins of the part. */
    RECT icon;
    BOOLEAN has_icon = child_icon_rect(w, m, &icon);
    MimeMsstylesMargins margins = {};
    mime_msstyles_get_margins(&g_theme.Theme, g_theme.Class[CLASS_WINDOW], caption_part, 0, MIME_TMT_CAPTIONMARGINS,
                              &margins);
    FONT_INSTANCE *fi = caption_font(dpi);
    if (fi && w->Text && w->TextLength) {
        LONG left = origin.x + (has_icon ? icon.right : m->Border) + margins.left;
        LONG right = origin.x + text_right - margins.right;
        LONG text_h = fi->Metrics.tmAscent + fi->Metrics.tmDescent;
        LONG top = origin.y + m->Border + (m->Top - m->Border - text_h) / 2;
        /* GetCaptionColor of uxtheme: the inactive title is 40% of its color over the white of the caption. */
        COLORREF color = MetricsSysColor(active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT);
        if (!active)
            color = RGB((GetRValue(color) * 2 + 0xFF * 3 + 2) / 5, (GetGValue(color) * 2 + 0xFF * 3 + 2) / 5,
                        (GetBValue(color) * 2 + 0xFF * 3 + 2) / 5);
        WCHAR title[260];
        ULONG n = fit_title(fi, w->Text, w->TextLength, right - left, title, RTL_NUMBER_OF(title));
        REGION inside;
        RgnInit(&inside);
        RECT box = {left, origin.y + m->Border, right, origin.y + m->Top};
        RgnCombineRect(&inside, clip, &box, RGN_AND);
        FrameDrawText(s, &inside, fi, left, top + fi->Metrics.tmAscent, title, n, right, ColorToPixel(color));
        RgnFree(&inside);
    }
    if (has_icon) {
        HDC hdc = PaintGetDC(w, nullptr, DCX_WINDOW);
        DC *dc = DcLookup(hdc);
        if (dc) {
            RgnCopy(&dc->VisRgn, clip);
            CursorDrawIcon(dc, icon.left, icon.top, child_icon(w), icon.right - icon.left, icon.bottom - icon.top,
                           DI_NORMAL);
            PaintReleaseDC(dc);
        }
    }
    return TRUE;
}

/* the sunken edge of WS_EX_CLIENTEDGE and the line of WS_EX_STATICEDGE, inside the frame. */
static void paint_client_edge(WND *w, SURFACE *s, const REGION *clip, RECT r)
{
    if (w->ExStyle & WS_EX_CLIENTEDGE) {
        ULONG shadow = ColorToPixel(MetricsSysColor(COLOR_3DSHADOW));
        ULONG light = ColorToPixel(MetricsSysColor(COLOR_3DHILIGHT));
        ULONG dark = ColorToPixel(MetricsSysColor(COLOR_3DDKSHADOW));
        ULONG face = ColorToPixel(MetricsSysColor(COLOR_3DLIGHT));
        for (int i = 0; i < 2; i++) {
            ULONG tl = i == 0 ? shadow : dark, br = i == 0 ? light : face;
            RECT top = {r.left, r.top, r.right, r.top + 1}, left = {r.left, r.top, r.left + 1, r.bottom};
            RECT bottom = {r.left, r.bottom - 1, r.right, r.bottom}, right = {r.right - 1, r.top, r.right, r.bottom};
            DrawFillRect(s, clip, &top, tl);
            DrawFillRect(s, clip, &left, tl);
            DrawFillRect(s, clip, &bottom, br);
            DrawFillRect(s, clip, &right, br);
            r.left++, r.top++, r.right--, r.bottom--;
        }
    }
    if (w->ExStyle & WS_EX_STATICEDGE)
        draw_ring(s, clip, r, 1, ColorToPixel(MetricsSysColor(COLOR_3DSHADOW)));
}

/*
 * the frame win32k draws around a child window that uxtheme does not theme, measured on windows 11 26100: the raised
 * edge of a window edge, then the active or inactive border of a thick frame or the face of a dialog frame. a border
 * alone is a line of the window frame color.
 */
static void paint_classic_frame(WND *w, SURFACE *s, const REGION *clip, RECT r, LONG width, BOOLEAN active)
{
    if (width <= 0)
        return;
    BOOLEAN thick = (w->Style & WS_THICKFRAME) != 0;
    if ((thick || (w->Style & WS_DLGFRAME) || (w->ExStyle & WS_EX_DLGMODALFRAME)) && width >= 2) {
        ULONG light = ColorToPixel(MetricsSysColor(COLOR_3DLIGHT)), hilight = ColorToPixel(MetricsSysColor(COLOR_3DHILIGHT));
        ULONG dark = ColorToPixel(MetricsSysColor(COLOR_3DDKSHADOW)), shadow = ColorToPixel(MetricsSysColor(COLOR_3DSHADOW));
        for (int i = 0; i < 2; i++) {
            ULONG tl = i == 0 ? light : hilight, br = i == 0 ? dark : shadow;
            RECT top = {r.left, r.top, r.right - 1, r.top + 1}, left = {r.left, r.top, r.left + 1, r.bottom - 1};
            RECT bottom = {r.left, r.bottom - 1, r.right, r.bottom}, right = {r.right - 1, r.top, r.right, r.bottom};
            DrawFillRect(s, clip, &top, tl);
            DrawFillRect(s, clip, &left, tl);
            DrawFillRect(s, clip, &bottom, br);
            DrawFillRect(s, clip, &right, br);
            r.left++, r.top++, r.right--, r.bottom--;
        }
        width -= 2;
        int fill = thick ? (active ? COLOR_ACTIVEBORDER : COLOR_INACTIVEBORDER) : COLOR_3DFACE;
        draw_ring(s, clip, r, width, ColorToPixel(MetricsSysColor(fill)));
        return;
    }
    if (w->Style & WS_BORDER)
        draw_ring(s, clip, r, 1, ColorToPixel(MetricsSysColor(COLOR_WINDOWFRAME)));
}

/* the caption of a child window when there is no style or the contrast is high: the classic caption colors. */
static void paint_classic_caption(WND *w, SURFACE *s, const REGION *clip, POINT origin, const FRAME_METRICS *m, UINT dpi,
                                  BOOLEAN active)
{
    if (!FrameHasCaption(w->Style, w->ExStyle))
        return;
    LONG width = w->WindowRect.right - w->WindowRect.left;
    RECT bar = {origin.x + m->Border, origin.y + m->Border, origin.x + width - m->Border, origin.y + m->Top - 1};
    DrawFillRect(s, clip, &bar, ColorToPixel(MetricsSysColor(active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION)));
    RECT line = {bar.left, bar.bottom, bar.right, bar.bottom + 1};
    DrawFillRect(s, clip, &line, ColorToPixel(MetricsSysColor(COLOR_3DFACE)));
    FRAME_COLORS colors;
    frame_colors(w, &colors);
    static const int buttons[] = {CAPTION_CLOSE, CAPTION_MAX, CAPTION_MIN, CAPTION_HELP};
    LONG right = bar.right;
    for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++) {
        RECT br;
        FrameGetButtonRect(w, buttons[i], &br);
        if (!RectEmpty(&br))
            right = W32Min(right, origin.x + br.left);
        draw_button(w, s, clip, origin, buttons[i], dpi, active, &colors);
    }
    FONT_INSTANCE *fi = caption_font(dpi);
    if (!fi || !w->Text || !w->TextLength)
        return;
    LONG left = bar.left + MetricsGet(SM_CXEDGE, dpi);
    WCHAR title[260];
    ULONG n = fit_title(fi, w->Text, w->TextLength, right - left, title, RTL_NUMBER_OF(title));
    LONG top = bar.top + (bar.bottom - bar.top - (fi->Metrics.tmAscent + fi->Metrics.tmDescent)) / 2;
    FrameDrawText(s, clip, fi, left, top + fi->Metrics.tmAscent, title, n, right,
                  ColorToPixel(MetricsSysColor(active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT)));
}

void FramePaint(WND *w)
{
    WND *top = WndTopLevel(w);
    if (!top || !top->Surface || !WndIsVisible(w))
        return;
    SURFACE *s = top->Surface;
    UINT dpi = WndFrameDpi(w);
    POINT origin = {w->WindowRect.left - top->WindowRect.left, w->WindowRect.top - top->WindowRect.top};

    REGION clip;
    RgnInit(&clip);
    WndComputeVisRgn(w, FALSE, 0, &clip);
    RECT client = w->ClientRect;
    RectOffset(&client, -top->WindowRect.left, -top->WindowRect.top);
    RgnCombineRect(&clip, &clip, &client, RGN_DIFF);
    if (RgnIsEmpty(&clip)) {
        RgnFree(&clip);
        return;
    }

    RECT window = {origin.x, origin.y, origin.x + (w->WindowRect.right - w->WindowRect.left),
                   origin.y + (w->WindowRect.bottom - w->WindowRect.top)};
    if (!is_top_frame(w->Style)) {
        FRAME_METRICS m;
        FrameGetMetrics(w->Style, w->ExStyle, dpi, &m);
        BOOLEAN active = (w->Flags & WNDF_ACTIVE_FRAME) != 0;
        if (!paint_child_caption(w, s, &clip, origin, &m, dpi, active)) {
            paint_classic_frame(w, s, &clip, window, m.Border, active);
            paint_classic_caption(w, s, &clip, origin, &m, dpi, active);
        }
        RECT inner = {window.left + m.Border, window.top + m.Top, window.right - m.Border, window.bottom - m.Border};
        paint_client_edge(w, s, &clip, inner);
        ScrollPaint(w, s, &clip);
        SurfaceMarkDrawn(s, &window, &clip);
        RgnFree(&clip);
        if (top->Thread == W32GetThread() && (top->Flags & WNDF_PRESENTED))
            PaintPresent(top);
        return;
    }

    load_theme();
    BOOLEAN active = (w->Flags & WNDF_ACTIVE_FRAME) != 0;
    FRAME_COLORS colors;
    frame_colors(w, &colors);
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, dpi, &m);
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    RectOffset(&vis, origin.x, origin.y);

    /* the resize border takes the window background, the caption the caption color; the host draws the border line over it. */
    DrawFillRect(s, &clip, &window, ColorToPixel(colors.Border));
    if (FrameHasCaption(w->Style, w->ExStyle)) {
        RECT caption = {vis.left + 1, vis.top, vis.right - 1, origin.y + m.Top};
        if (caption.bottom > caption.top && caption.right > caption.left)
            DrawFillRect(s, &clip, &caption, ColorToPixel(colors.Caption));
    }

    if (FrameHasCaption(w->Style, w->ExStyle)) {
        LONG caption_top = vis.top;
        LONG caption_bottom = origin.y + m.Top;
        LONG x = vis.left + scale(12, dpi);
        /* like windows, the small icon of the window, then of its class, then the large ones. */
        HICON shown_icon = w->IconSmall ? w->IconSmall
                           : w->Class && w->Class->IconSmall ? w->Class->IconSmall
                           : w->Icon ? w->Icon : w->Class ? w->Class->Icon : nullptr;
        BOOLEAN icon = (w->Style & WS_SYSMENU) && shown_icon && !(w->ExStyle & (WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW));
        if (icon) {
            LONG size = MetricsGet(SM_CXSMICON, dpi);
            LONG ix = vis.left + scale(8, dpi);
            LONG iy = caption_top + (caption_bottom - caption_top - size) / 2;
            RECT selection = {vis.left + 1, caption_top, ix + size + scale(8, dpi), caption_bottom};
            MenuBarPaintIcon(w, s, &clip, &selection);
            HDC hdc = PaintGetDC(w, nullptr, DCX_WINDOW);
            DC *dc = DcLookup(hdc);
            if (dc) {
                RgnCopy(&dc->VisRgn, &clip);
                CursorDrawIcon(dc, ix - origin.x, iy - origin.y, shown_icon, size, size, DI_NORMAL);
                PaintReleaseDC(dc);
            }
            x = ix + size + scale(8, dpi);
        }
        FONT_INSTANCE *fi = caption_font(dpi);
        if (fi && w->Text && w->TextLength) {
            LONG right = caption_buttons_left(w) + origin.x - scale(4, dpi);
            WCHAR title[260];
            ULONG n = fit_title(fi, w->Text, w->TextLength, right - x, title, RTL_NUMBER_OF(title));
            LONG text_h = fi->Metrics.tmAscent + fi->Metrics.tmDescent;
            LONG baseline = caption_top + (caption_bottom - caption_top - text_h) / 2 + fi->Metrics.tmAscent;
            FrameDrawText(s, &clip, fi, x, baseline, title, n, right, ColorToPixel(active ? colors.TextActive : colors.TextInactive));
        }
        static const int buttons[] = {CAPTION_CLOSE, CAPTION_MAX, CAPTION_MIN, CAPTION_HELP};
        for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++)
            draw_button(w, s, &clip, origin, buttons[i], dpi, active, &colors);
    }
    MenuBarPaint(w, s, &clip, origin);
    ScrollPaint(w, s, &clip);
    /* drawn straight into the surface, so the present needs to know. */
    SurfaceMarkDrawn(s, &window, &clip);
    RgnFree(&clip);
    if (top->Thread == W32GetThread() && (top->Flags & WNDF_PRESENTED))
        PaintPresent(top);
}

/* ------------------------------------------------------------------ the frame extended into the client area */

/*
 * dwmapi keeps the margins of DwmExtendFrameIntoClientArea in two properties of 16 bit halves, left and right in
 * DwmMarginsLR, top and bottom in DwmMarginsTB. a negative margin makes the whole client area a sheet of glass.
 */
static BOOLEAN frame_margins(WND *w, LONG margins[4])
{
    ULONG_PTR lr = (ULONG_PTR)PropGetData(w, PropAtom(L"DwmMarginsLR"));
    ULONG_PTR tb = (ULONG_PTR)PropGetData(w, PropAtom(L"DwmMarginsTB"));
    margins[0] = (SHORT)(lr & 0xFFFF);
    margins[1] = (SHORT)((lr >> 16) & 0xFFFF);
    margins[2] = (SHORT)(tb & 0xFFFF);
    margins[3] = (SHORT)((tb >> 16) & 0xFFFF);
    return (lr || tb) && is_top_frame(w->Style);
}

/* the part of the client area, window relative, where the frame shows under what the program draws. */
BOOLEAN FrameExtension(WND *w, REGION *out)
{
    RgnInit(out);
    LONG margins[4];
    if (!frame_margins(w, margins))
        return FALSE;
    RECT client = w->ClientRect;
    RectOffset(&client, -w->WindowRect.left, -w->WindowRect.top);
    if (RectEmpty(&client))
        return FALSE;
    RgnSetRect(out, &client);
    if (margins[0] >= 0 && margins[1] >= 0 && margins[2] >= 0 && margins[3] >= 0) {
        RECT inner = {client.left + margins[0], client.top + margins[2], client.right - margins[1],
                      client.bottom - margins[3]};
        if (!RectEmpty(&inner))
            RgnCombineRect(out, out, &inner, RGN_DIFF);
    }
    if (RgnIsEmpty(out)) {
        RgnFree(out);
        return FALSE;
    }
    return TRUE;
}

/*
 * the dwm draws the caption buttons over a client area that took the caption, as long as the frame reaches them: the
 * top margin, or a sheet of glass.
 */
BOOLEAN FrameDwmButtons(WND *w)
{
    LONG margins[4];
    if (!frame_margins(w, margins) || !FrameHasCaption(w->Style, w->ExStyle) || !(w->Style & WS_SYSMENU))
        return FALSE;
    RECT close;
    FrameGetButtonRect(w, CAPTION_CLOSE, &close);
    LONG client_top = w->ClientRect.top - w->WindowRect.top;
    if (RectEmpty(&close) || client_top >= close.bottom)
        return FALSE;
    return margins[2] < 0 || margins[0] < 0 || margins[1] < 0 || margins[3] < 0 || margins[2] > 0;
}

/*
 * DwmEnableBlurBehindWindow: dwmapi keeps whether it is on in DwmBlurBehind, and win32k copies the region of
 * DwmBlurRegion as it is set, client relative, since the program may delete its own at once.
 */
typedef struct BLUR_REGION {
    struct BLUR_REGION *Next;
    WND *Window;
    REGION Region;
} BLUR_REGION;

static BLUR_REGION *g_blur_regions;

static BOOLEAN blur_behind(WND *w)
{
    return PropGetData(w, PropAtom(L"DwmBlurBehind")) != nullptr;
}

static BLUR_REGION **blur_region(WND *w)
{
    BLUR_REGION **link = &g_blur_regions;
    while (*link && (*link)->Window != w)
        link = &(*link)->Next;
    return link;
}

void FrameForgetBlurRegion(WND *w)
{
    BLUR_REGION **link = blur_region(w), *b = *link;
    if (!b)
        return;
    *link = b->Next;
    RgnFree(&b->Region);
    W32Free(b);
}

void FrameSetBlurRegion(WND *w, const REGION *region)
{
    FrameForgetBlurRegion(w);
    BLUR_REGION *b = region ? W32Alloc(sizeof(BLUR_REGION)) : nullptr;
    if (!b)
        return;
    b->Window = w;
    RgnInit(&b->Region);
    RgnCopy(&b->Region, region);
    b->Next = g_blur_regions;
    g_blur_regions = b;
}

/* a frame of its own that the dwm draws under extended glass: a thick frame or a caption, measured on windows 11. */
static BOOLEAN dwm_frame(DWORD style)
{
    return (style & WS_THICKFRAME) || (style & WS_CAPTION) == WS_CAPTION;
}

/* a premultiplied color under the pixels of a region. gdi leaves its colors at alpha 0, so the sum is held to white. */
static void fill_under(SURFACE *dst, const REGION *region, ULONG color)
{
    for (ULONG i = 0; i < region->Count; i++) {
        const RECT *r = &region->Rects[i];
        for (LONG y = r->top; y < r->bottom; y++) {
            ULONG *row = (ULONG *)(dst->Bits + (LONG_PTR)y * dst->Stride);
            for (LONG x = r->left; x < r->right; x++) {
                ULONG s = row[x], a = 255 - (s >> 24), out = 0;
                if (!a)
                    continue;
                for (int shift = 0; shift < 32; shift += 8) {
                    ULONG c = ((s >> shift) & 0xFF) + (((color >> shift) & 0xFF) * a + 127) / 255;
                    out |= (c > 255 ? 255 : c) << shift;
                }
                row[x] = out;
            }
        }
    }
}

/* whether the window is composed by the alpha of its pixels: it extended its frame or asked for blur behind. */
BOOLEAN FrameComposesAlpha(WND *w)
{
    REGION ext;
    if (FrameExtension(w, &ext)) {
        RgnFree(&ext);
        return TRUE;
    }
    return is_top_frame(w->Style) && blur_behind(w);
}

/*
 * what the dwm of windows 11 composes under a window that extended its frame or asked for blur behind. dst holds the
 * window's pixels in area, window relative.
 *
 * - out of the client area the window is opaque, whatever alpha gdi left there.
 * - the extended part shows the caption color of a window with a thick frame or a caption, and what is behind the
 *   window otherwise, or when the whole client area is glass and blur is on.
 * - without blur the rest of the client area is opaque. with it, the client area inside the margins and the blur
 *   region, the whole of it without one, has a fill under it: the caption color when the frame is extended, else a
 *   dark tint of a quarter, the backdrop going to 192/255 of itself plus 2. the rest shows what is behind.
 *
 * the caption buttons go over the extended part.
 */
void FrameCompose(WND *w, SURFACE *dst, const RECT *area)
{
    REGION ext;
    BOOLEAN extended = FrameExtension(w, &ext), blur = is_top_frame(w->Style) && blur_behind(w);
    if (!extended && !blur)
        return;
    LONG margins[4] = {};
    BOOLEAN with_margins = frame_margins(w, margins);
    BOOLEAN sheet = with_margins && (margins[0] < 0 || margins[1] < 0 || margins[2] < 0 || margins[3] < 0);
    FRAME_COLORS colors;
    frame_colors(w, &colors);
    /* glass is white in light mode, where the caption is a grey. measured on windows 11 26100. */
    ULONG custom;
    BOOLEAN white = !dark_frame(w) && !dwm_color(w, L"DwmCaptionColor", &custom);
    ULONG frame = (white ? 0xFFFFFFu : ColorToPixel(colors.Caption)) | 0xFF000000u;
    RECT client = w->ClientRect;
    RectOffset(&client, -w->WindowRect.left, -w->WindowRect.top);
    REGION opaque, fill;
    RgnInit(&opaque);
    RgnInit(&fill);
    RgnSetRect(&opaque, area);
    if (blur) {
        RgnCombineRect(&opaque, &opaque, &client, RGN_DIFF);
        RECT inner = client;
        if (sheet) {
            inner = (RECT){};
        } else if (with_margins) {
            inner.left += margins[0];
            inner.right -= margins[1];
            inner.top += margins[2];
            inner.bottom -= margins[3];
        }
        if (!RectEmpty(&inner)) {
            RgnSetRect(&fill, &inner);
            BLUR_REGION *b = *blur_region(w);
            if (b) {
                REGION shape;
                RgnInit(&shape);
                RgnCopy(&shape, &b->Region);
                RgnOffset(&shape, client.left, client.top);
                RgnCombine(&fill, &fill, &shape, RGN_AND);
                RgnFree(&shape);
            }
            RgnCombineRect(&fill, &fill, area, RGN_AND);
        }
    } else {
        RgnCombine(&opaque, &opaque, &ext, RGN_DIFF);
    }
    /* what was never drawn of a window without a border shows what is under it. */
    if (w->Surface && w->Surface->Unpainted)
        RgnCombine(&opaque, &opaque, w->Surface->Unpainted, RGN_DIFF);
    for (ULONG i = 0; i < opaque.Count; i++) {
        const RECT *r = &opaque.Rects[i];
        for (LONG y = r->top; y < r->bottom; y++) {
            ULONG *row = (ULONG *)(dst->Bits + (LONG_PTR)y * dst->Stride);
            for (LONG x = r->left; x < r->right; x++)
                row[x] |= 0xFF000000u;
        }
    }
    RgnFree(&opaque);
    fill_under(dst, &fill, with_margins ? frame : 0x3F020202u);
    RgnFree(&fill);
    if (extended && dwm_frame(w->Style) && !(sheet && blur)) {
        RgnCombineRect(&ext, &ext, area, RGN_AND);
        fill_under(dst, &ext, frame);
    }
    RgnFree(&ext);
    if (!FrameDwmButtons(w))
        return;
    load_theme();
    REGION clip;
    RgnInit(&clip);
    RgnSetRect(&clip, area);
    BOOLEAN active = (w->Flags & WNDF_ACTIVE_FRAME) != 0;
    static const int buttons[] = {CAPTION_CLOSE, CAPTION_MAX, CAPTION_MIN, CAPTION_HELP};
    for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++)
        draw_button(w, dst, &clip, (POINT){0, 0}, buttons[i], WndFrameDpi(w), active, &colors);
    RgnFree(&clip);
}

/*
 * the pixels of a window keep the alpha windows' redirection surfaces have, so the black gdi paints with alpha 0 shows
 * what is under it in a window that extended its frame or asked for blur behind. the surface of another window holds
 * alpha 255 for the host, which composes by alpha, and keeps windows' alpha aside, see SurfaceSetAlpha. when the dwm
 * starts or stops composing a window by its alpha, the pixels take the one or the other. what was never drawn stays
 * clear either way.
 */
void FrameSurfaceAlpha(WND *w)
{
    SURFACE *s = w->Surface;
    if (!s)
        return;
    BOOLEAN device = !FrameComposesAlpha(w);
    if (device == ((s->Flags & SURF_DEVICE) != 0))
        return;
    if (s->Bpp != 32 || (w->LayerFlags & MIME_LAYER_UPDATE)) {
        s->Flags = device ? s->Flags | SURF_DEVICE : s->Flags & ~SURF_DEVICE;
        return;
    }
    for (LONG y = 0; y < s->Height; y++) {
        ULONG *row = (ULONG *)(s->Bits + (LONG_PTR)y * s->Stride);
        for (LONG x = 0; x < s->Width; x++) {
            if (device) {
                SurfaceSetAlpha(s, x, y, 1, (UCHAR)(row[x] >> 24));
                row[x] |= 0xFF000000u;
            } else {
                row[x] = (row[x] & 0xFFFFFF) | (ULONG)SurfaceAlphaAt(s, x, y) << 24;
            }
        }
    }
    if (device) {
        s->Flags |= SURF_DEVICE;
        for (ULONG i = 0; s->Unpainted && i < s->Unpainted->Count; i++) {
            const RECT *r = &s->Unpainted->Rects[i];
            for (LONG y = r->top; y < r->bottom; y++)
                memset(s->Bits + (LONG_PTR)y * s->Stride + (LONG_PTR)r->left * 4, 0, (SIZE_T)(r->right - r->left) * 4);
        }
    } else {
        s->Flags &= ~SURF_DEVICE;
        W32Free(s->Alpha);
        s->Alpha = nullptr;
    }
    RECT all = {0, 0, s->Width, s->Height};
    SurfaceMarkDirty(s, &all);
}

/* the caption buttons the dwm shows over the client area changed state. */
static void recompose_buttons(WND *w)
{
    if (!w->Surface || !FrameDwmButtons(w))
        return;
    RECT all = {0, 0, 0, 0};
    static const int buttons[] = {CAPTION_CLOSE, CAPTION_MAX, CAPTION_MIN, CAPTION_HELP};
    for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++) {
        RECT r;
        FrameGetButtonRect(w, buttons[i], &r);
        if (RectEmpty(&r))
            continue;
        if (RectEmpty(&all))
            all = r;
        else
            RectUnion(&all, &all, &r);
    }
    if (RectEmpty(&all))
        return;
    SurfaceMarkDirty(w->Surface, &all);
    if (w->Thread == W32GetThread() && (w->Flags & WNDF_PRESENTED))
        PaintPresent(w);
}

/*
 * the visible 1 pixel border takes the color just inside it, so a dark program does not show the light
 * frame the caption color would give. runs before the present, after the program painted.
 */
void FrameBleed(WND *w)
{
    SURFACE *s = w->Surface;
    if (!s || !WndIsTopLevel(w))
        return;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndFrameDpi(w), &m);
    if (!m.VisibleInset || FrameClientOnly(w))
        return;
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    LONG left = vis.left, right = vis.right - 1, bottom = vis.bottom - 1;
    if (left < 0 || right >= s->Width || bottom <= 0 || bottom >= s->Height || right <= left)
        return;
    for (LONG x = left; x <= right; x++) {
        ULONG *dst = (ULONG *)(s->Bits + (SIZE_T)bottom * s->Stride);
        ULONG *src = (ULONG *)(s->Bits + (SIZE_T)(bottom - 1) * s->Stride);
        dst[x] = src[x];
    }
    for (LONG y = vis.top; y <= bottom; y++) {
        ULONG *row = (ULONG *)(s->Bits + (SIZE_T)y * s->Stride);
        row[left] = row[left + 1];
        row[right] = row[right - 1];
    }
}

/* ------------------------------------------------------------------ mouse */
static void repaint_buttons(WND *w)
{
    if (!WndIsVisible(w))
        return;
    FramePaint(w);
    recompose_buttons(w);
}

static UINT button_command(WND *w, int button)
{
    switch (button) {
    case CAPTION_CLOSE:
        return SC_CLOSE;
    case CAPTION_MAX:
        return (w->Style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE;
    case CAPTION_MIN:
        return SC_MINIMIZE;
    case CAPTION_HELP:
        return SC_CONTEXTHELP;
    }
    return 0;
}

static BOOLEAN button_enabled(WND *w, int button)
{
    if (w->Style & WS_DISABLED)
        return FALSE;
    if (button == CAPTION_MAX)
        return (w->Style & WS_MAXIMIZEBOX) != 0;
    if (button == CAPTION_MIN)
        return (w->Style & WS_MINIMIZEBOX) != 0;
    if (button == CAPTION_CLOSE)
        return !(w->Flags & WNDF_NOCLOSE);
    return button != CAPTION_NONE;
}

LRESULT FrameMouse(WND *w, UINT msg, LONG hit, POINT pt)
{
    POINT wp = {pt.x - w->WindowRect.left, pt.y - w->WindowRect.top};
    int over = button_at(w, wp);
    if (!button_enabled(w, over))
        over = CAPTION_NONE;
    W32THREAD *t = w->Thread;
    switch (msg) {
    case WM_NCMOUSEMOVE:
        if (w->CaptionHot != over) {
            w->CaptionHot = (UCHAR)over;
            repaint_buttons(w);
        }
        break;
    case WM_NCLBUTTONDOWN:
        if (over != CAPTION_NONE) {
            w->CaptionHot = w->CaptionPressed = w->CaptionTracking = (UCHAR)over;
            if (t)
                InputSetCapture(t, w, (ULONG)hit);
            repaint_buttons(w);
        }
        break;
    case WM_NCLBUTTONUP:
        if (w->CaptionTracking) {
            int tracked = w->CaptionTracking;
            w->CaptionTracking = w->CaptionPressed = 0;
            w->CaptionHot = (UCHAR)over;
            if (t && t->Capture == WndHandle(w))
                InputSetCapture(t, nullptr, 0);
            repaint_buttons(w);
            if (over == tracked && !w->Header.Destroyed) {
                UINT cmd = button_command(w, tracked);
                if (cmd)
                    MsgSend(w, WM_SYSCOMMAND, cmd, MAKELPARAM(pt.x, pt.y));
            }
        }
        break;
    case WM_NCLBUTTONDBLCLK:
        if (hit == HTCAPTION && (w->Style & WS_MAXIMIZEBOX) && (w->Style & WS_THICKFRAME))
            MsgSend(w, WM_SYSCOMMAND, (w->Style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE, MAKELPARAM(pt.x, pt.y));
        else if (hit == HTSYSMENU)
            MsgSend(w, WM_SYSCOMMAND, SC_CLOSE, MAKELPARAM(pt.x, pt.y));
        else if (over != CAPTION_NONE)
            return FrameMouse(w, WM_NCLBUTTONDOWN, hit, pt);
        break;
    case WM_NCMOUSELEAVE:
        if (w->CaptionHot && !w->CaptionTracking) {
            w->CaptionHot = CAPTION_NONE;
            repaint_buttons(w);
        }
        break;
    }
    return 0;
}
