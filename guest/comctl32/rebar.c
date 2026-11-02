/* the rebar control, ReBarWindow32: bands in rows, each with a gripper, a caption and a child window. */
#include "comctl32_private.h"

#define GRIPPER_WIDTH 8
#define CAPTION_PAD 4

typedef struct BAND {
    UINT Style;
    COLORREF Fore, Back;
    LPWSTR Text;
    int Image;
    HWND Child;
    UINT CxMin, CyMin, Cx;
    HBITMAP Bitmap;
    UINT Id;
    UINT CyChild, CyMax, CyIntegral, CxIdeal;
    LPARAM Param;
    UINT CxHeader;
    BOOL HeaderSet;
    /* where layout put it. */
    RECT Rect;
    int Row;
} BAND;

typedef struct REBAR {
    HWND Hwnd;
    CC_NOTIFY Notify;
    DWORD Style, ExStyle;
    BAND *Bands;
    int Count;
    HIMAGELIST Images;
    HFONT Font;
    COLORREF Back, Text;
    HWND Tooltips;
    int Rows;
    int *RowHeights;
    int BarHeight;
    BOOL InSize;
    BOOL Vertical;
    HTHEME Theme;
} REBAR;

static REBAR *get_data(HWND hwnd)
{
    return (REBAR *)GetWindowLongPtrW(hwnd, 0);
}

static HFONT bar_font(REBAR *r)
{
    return r->Font ? r->Font : CcIconTitleFont();
}

/* the room a band's caption takes: the gripper, the image and the text. */
static int header_width(REBAR *r, BAND *b, BOOL grip, HDC dc)
{
    if (b->HeaderSet)
        return (int)b->CxHeader;
    int w = grip ? GRIPPER_WIDTH : 0;
    int content = 0;
    if (r->Images && b->Image >= 0) {
        int cx = 0;
        ImageList_GetIconSize(r->Images, &cx, nullptr);
        content += cx + 2;
    }
    if (b->Text && *b->Text && !(b->Style & RBBS_HIDETITLE)) {
        SIZE size = {};
        GetTextExtentPoint32W(dc, b->Text, (int)wcslen(b->Text), &size);
        content += size.cx + 2;
    }
    if (content)
        content += CAPTION_PAD;
    return w + content;
}

static BOOL band_visible(const BAND *b)
{
    return !(b->Style & RBBS_HIDDEN);
}

static BOOL show_grip(REBAR *r, const BAND *b, int visible_count)
{
    if (b->Style & RBBS_NOGRIPPER)
        return FALSE;
    if (b->Style & RBBS_GRIPPERALWAYS)
        return TRUE;
    return !(r->Style & RBS_FIXEDORDER) || visible_count > 1;
}

static int band_height(REBAR *r, BAND *b, int text_height)
{
    int h = (int)b->CyMin;
    if ((r->Style & RBS_VARHEIGHT) && (b->Style & RBBS_VARIABLEHEIGHT)) {
        int want = (int)b->CyChild;
        if (want < (int)b->CyMin)
            want = (int)b->CyMin;
        if (b->CyMax && want > (int)b->CyMax)
            want = (int)b->CyMax;
        if (b->CyIntegral > 1) {
            int step = (int)b->CyIntegral;
            want = (int)b->CyMin + (want - (int)b->CyMin) / step * step;
        }
        h = want;
    }
    int image = 0;
    if (r->Images && b->Image >= 0)
        ImageList_GetIconSize(r->Images, nullptr, &image);
    int caption = (b->Text && *b->Text) ? text_height : 0;
    if (image > h)
        h = image;
    if (caption > h && !b->Child)
        h = caption;
    if (b->Style & RBBS_CHILDEDGE)
        h += 4;
    return h;
}

static void move_child(REBAR *r, BAND *b, int k, BOOL grip, int header)
{
    if (!b->Child || !IsWindow(b->Child))
        return;
    RECT child = b->Rect;
    child.left += header;
    if (b->Style & RBBS_CHILDEDGE) {
        InflateRect(&child, -2, -2);
    }
    if (r->Style & RBS_BANDBORDERS)
        child.bottom -= 2;
    if (child.right < child.left)
        child.right = child.left;
    NMREBARCHILDSIZE nm = {};
    nm.uBand = (UINT)k;
    nm.wID = b->Id;
    nm.rcChild = child;
    nm.rcBand = b->Rect;
    CcNotify(&r->Notify, RBN_CHILDSIZE, &nm.hdr);
    child = nm.rcChild;
    SetWindowPos(b->Child, nullptr, child.left, child.top, child.right - child.left, child.bottom - child.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

/* puts the bands in rows that fit the client width and sizes them; returns the height the bands need. */
static int layout(REBAR *r, int width)
{
    HDC dc = GetDC(r->Hwnd);
    HGDIOBJ old = SelectObject(dc, bar_font(r));
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    int visible = 0;
    for (int k = 0; k < r->Count; k++)
        if (band_visible(&r->Bands[k]))
            visible++;
    int *grip = CcAlloc((SIZE_T)(r->Count + 1) * sizeof(int));
    int *header = CcAlloc((SIZE_T)(r->Count + 1) * sizeof(int));
    int *minw = CcAlloc((SIZE_T)(r->Count + 1) * sizeof(int));
    int *heights = CcAlloc((SIZE_T)(r->Count + 1) * sizeof(int));
    CcFree(r->RowHeights);
    r->RowHeights = CcAlloc((SIZE_T)(r->Count + 1) * sizeof(int));
    if (!grip || !header || !minw || !heights || !r->RowHeights) {
        CcFree(grip);
        CcFree(header);
        CcFree(minw);
        CcFree(heights);
        SelectObject(dc, old);
        ReleaseDC(r->Hwnd, dc);
        return r->BarHeight;
    }
    for (int k = 0; k < r->Count; k++) {
        BAND *b = &r->Bands[k];
        grip[k] = show_grip(r, b, visible);
        header[k] = header_width(r, b, grip[k], dc);
        minw[k] = header[k] + (int)b->CxMin + ((b->Style & RBBS_CHILDEDGE) ? 4 : 0);
        heights[k] = band_height(r, b, tm.tmHeight);
    }
    SelectObject(dc, old);
    ReleaseDC(r->Hwnd, dc);

    int border = (r->Style & RBS_BANDBORDERS) ? 2 : 0;
    int row = 0, y = 0, k = 0;
    while (k < r->Count) {
        if (!band_visible(&r->Bands[k])) {
            r->Bands[k].Row = row;
            SetRectEmpty(&r->Bands[k].Rect);
            k++;
            continue;
        }
        /* the bands of one row: until one asks for a break or does not fit beside the others at its smallest. */
        int first = k, last = k, smallest = minw[k];
        for (int n = k + 1; n < r->Count; n++) {
            if (!band_visible(&r->Bands[n])) {
                last = n;
                continue;
            }
            if ((r->Bands[n].Style & RBBS_BREAK) || smallest + minw[n] > width)
                break;
            smallest += minw[n];
            last = n;
        }
        int *size = CcAlloc((SIZE_T)(last - first + 1) * sizeof(int));
        if (!size)
            break;
        int total = 0, row_height = 0;
        for (int n = first; n <= last; n++) {
            if (!band_visible(&r->Bands[n]))
                continue;
            size[n - first] = (int)r->Bands[n].Cx > minw[n] ? (int)r->Bands[n].Cx : minw[n];
            total += size[n - first];
            if (heights[n] > row_height)
                row_height = heights[n];
        }
        /* too wide: the last bands give up room down to their smallest. */
        for (int n = last; n >= first && total > width; n--) {
            if (!band_visible(&r->Bands[n]))
                continue;
            int spare = size[n - first] - minw[n];
            int take = total - width < spare ? total - width : spare;
            size[n - first] -= take;
            total -= take;
        }
        /* too narrow: the last band that may grow takes what is left. */
        for (int n = last; n >= first && total < width; n--) {
            if (!band_visible(&r->Bands[n]) || (r->Bands[n].Style & RBBS_FIXEDSIZE))
                continue;
            size[n - first] += width - total;
            total = width;
        }
        int x = 0;
        for (int n = first; n <= last; n++) {
            BAND *b = &r->Bands[n];
            b->Row = row;
            if (!band_visible(b)) {
                SetRectEmpty(&b->Rect);
                continue;
            }
            SetRect(&b->Rect, x, y, x + size[n - first], y + row_height + border);
            b->Cx = (UINT)size[n - first];
            b->CxHeader = (UINT)header[n];
            x += size[n - first];
        }
        r->RowHeights[row] = row_height + border;
        y += row_height + border;
        row++;
        CcFree(size);
        k = last + 1;
    }
    r->Rows = row;
    CcFree(grip);
    CcFree(header);
    CcFree(minw);
    CcFree(heights);
    return y;
}

static void place_children(REBAR *r)
{
    int visible = 0;
    for (int k = 0; k < r->Count; k++)
        if (band_visible(&r->Bands[k]))
            visible++;
    for (int k = 0; k < r->Count; k++) {
        BAND *b = &r->Bands[k];
        if (!band_visible(b)) {
            if (b->Child && IsWindow(b->Child))
                ShowWindow(b->Child, SW_HIDE);
            continue;
        }
        move_child(r, b, k, show_grip(r, b, visible), (int)b->CxHeader);
    }
}

/* lays the bands out again and tells the parent when the bar's height changed. */
static void relayout(REBAR *r)
{
    RECT client;
    GetClientRect(r->Hwnd, &client);
    int width = client.right - client.left;
    int height = layout(r, width);
    place_children(r);
    if (height != r->BarHeight) {
        r->BarHeight = height;
        CcNotifyHdr(&r->Notify, RBN_HEIGHTCHANGE);
        /* a bar that is not docked by the program sizes itself to what its bands need. */
        if (!(r->Style & CCS_NORESIZE) && !r->InSize) {
            RECT window;
            GetWindowRect(r->Hwnd, &window);
            r->InSize = TRUE;
            SetWindowPos(r->Hwnd, nullptr, 0, 0, window.right - window.left, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            r->InSize = FALSE;
        }
    }
    InvalidateRect(r->Hwnd, nullptr, TRUE);
}

/* a bar that follows its parent: the parent's width, at its top or bottom. */
static void follow_parent(REBAR *r)
{
    if ((r->Style & CCS_NORESIZE) || (r->Style & CCS_NOPARENTALIGN) || r->InSize)
        return;
    HWND parent = GetParent(r->Hwnd);
    if (!parent)
        return;
    RECT area;
    GetClientRect(parent, &area);
    int height = r->BarHeight;
    int y = (r->Style & CCS_BOTTOM) == CCS_BOTTOM ? area.bottom - height : 0;
    r->InSize = TRUE;
    SetWindowPos(r->Hwnd, nullptr, 0, y, area.right - area.left, height, SWP_NOZORDER | SWP_NOACTIVATE);
    r->InSize = FALSE;
}

static BOOL band_valid(REBAR *r, int index)
{
    return index >= 0 && index < r->Count;
}

static void apply_info(REBAR *r, BAND *b, const REBARBANDINFOW *src, BOOL ansi)
{
    UINT mask = src->fMask;
    BOOL v6 = src->cbSize > REBARBANDINFO_V3_SIZE;
    if (mask & RBBIM_STYLE)
        b->Style = src->fStyle;
    if (mask & RBBIM_COLORS) {
        b->Fore = src->clrFore;
        b->Back = src->clrBack;
    }
    if (mask & RBBIM_TEXT) {
        if (ansi)
            CcSetTextA(&b->Text, (LPCSTR)src->lpText);
        else
            CcSetText(&b->Text, src->lpText);
    }
    if (mask & RBBIM_IMAGE)
        b->Image = src->iImage;
    if (mask & RBBIM_CHILD) {
        b->Child = src->hwndChild;
        if (b->Child && IsWindow(b->Child) && GetParent(b->Child) != r->Hwnd) {
            SetParent(b->Child, r->Hwnd);
        }
    }
    if (mask & RBBIM_CHILDSIZE) {
        b->CxMin = src->cxMinChild;
        b->CyMin = src->cyMinChild;
        if (v6) {
            b->CyChild = src->cyChild;
            b->CyMax = src->cyMaxChild;
            b->CyIntegral = src->cyIntegral;
        }
    }
    if (mask & RBBIM_SIZE)
        b->Cx = src->cx;
    if (mask & RBBIM_BACKGROUND)
        b->Bitmap = src->hbmBack;
    if (mask & RBBIM_ID)
        b->Id = src->wID;
    if (v6 && (mask & RBBIM_IDEALSIZE))
        b->CxIdeal = src->cxIdeal;
    if (v6 && (mask & RBBIM_LPARAM))
        b->Param = src->lParam;
    if (v6 && (mask & RBBIM_HEADERSIZE)) {
        b->CxHeader = src->cxHeader;
        b->HeaderSet = src->cxHeader != (UINT)-1;
    }
}

static BOOL insert_band(REBAR *r, int index, const REBARBANDINFOW *src, BOOL ansi)
{
    if (!src || src->cbSize < REBARBANDINFO_V3_SIZE)
        return FALSE;
    if (index < 0 || index > r->Count)
        index = r->Count;
    BAND *bands = CcReAlloc(r->Bands, ((SIZE_T)r->Count + 1) * sizeof(BAND));
    if (!bands)
        return FALSE;
    r->Bands = bands;
    memmove(&r->Bands[index + 1], &r->Bands[index], (SIZE_T)(r->Count - index) * sizeof(BAND));
    BAND *b = &r->Bands[index];
    memset(b, 0, sizeof(*b));
    b->Image = -1;
    b->Fore = CLR_DEFAULT;
    b->Back = CLR_DEFAULT;
    b->CyIntegral = 1;
    r->Count++;
    apply_info(r, b, src, ansi);
    relayout(r);
    return TRUE;
}

static BOOL set_info(REBAR *r, int index, const REBARBANDINFOW *src, BOOL ansi)
{
    if (!band_valid(r, index) || !src || src->cbSize < REBARBANDINFO_V3_SIZE)
        return FALSE;
    apply_info(r, &r->Bands[index], src, ansi);
    relayout(r);
    return TRUE;
}

static BOOL get_info(REBAR *r, int index, REBARBANDINFOW *dst, BOOL ansi)
{
    if (!band_valid(r, index) || !dst || dst->cbSize < REBARBANDINFO_V3_SIZE)
        return FALSE;
    BAND *b = &r->Bands[index];
    UINT mask = dst->fMask;
    BOOL v6 = dst->cbSize > REBARBANDINFO_V3_SIZE;
    if (mask & RBBIM_STYLE)
        dst->fStyle = b->Style;
    if (mask & RBBIM_COLORS) {
        dst->clrFore = b->Fore;
        dst->clrBack = b->Back;
    }
    if (mask & RBBIM_TEXT) {
        if (ansi)
            CcCopyTextA((LPSTR)dst->lpText, (int)dst->cch, b->Text);
        else
            CcCopyText(dst->lpText, (int)dst->cch, b->Text);
    }
    if (mask & RBBIM_IMAGE)
        dst->iImage = b->Image;
    if (mask & RBBIM_CHILD)
        dst->hwndChild = b->Child;
    if (mask & RBBIM_CHILDSIZE) {
        dst->cxMinChild = b->CxMin;
        dst->cyMinChild = b->CyMin;
        if (v6) {
            dst->cyChild = b->CyChild;
            dst->cyMaxChild = b->CyMax;
            dst->cyIntegral = b->CyIntegral;
        }
    }
    if (mask & RBBIM_SIZE)
        dst->cx = b->Cx;
    if (mask & RBBIM_BACKGROUND)
        dst->hbmBack = b->Bitmap;
    if (mask & RBBIM_ID)
        dst->wID = b->Id;
    if (v6 && (mask & RBBIM_IDEALSIZE))
        dst->cxIdeal = b->CxIdeal;
    if (v6 && (mask & RBBIM_LPARAM))
        dst->lParam = b->Param;
    if (v6 && (mask & RBBIM_HEADERSIZE))
        dst->cxHeader = b->CxHeader;
    return TRUE;
}

static void delete_band(REBAR *r, int index)
{
    NMREBAR nm = {};
    nm.uBand = (UINT)index;
    nm.wID = r->Bands[index].Id;
    nm.lParam = r->Bands[index].Param;
    CcNotify(&r->Notify, RBN_DELETINGBAND, &nm.hdr);
    CcFree(r->Bands[index].Text);
    memmove(&r->Bands[index], &r->Bands[index + 1], (SIZE_T)(r->Count - index - 1) * sizeof(BAND));
    r->Count--;
    CcNotify(&r->Notify, RBN_DELETEDBAND, &nm.hdr);
    relayout(r);
}

static void draw_gripper(REBAR *r, HDC dc, const RECT *band, int height)
{
    int x = band->left + 2;
    int top = band->top + 2, bottom = band->bottom - 2;
    if (r->Style & RBS_BANDBORDERS)
        bottom -= 1;
    (void)height;
    for (int i = 0; i < 2; i++) {
        RECT dark = {x + i * 3, top, x + i * 3 + 1, bottom};
        RECT light = {x + i * 3 + 1, top, x + i * 3 + 2, bottom};
        CcFillSolid(dc, &dark, GetSysColor(COLOR_BTNSHADOW));
        CcFillSolid(dc, &light, GetSysColor(COLOR_BTNHILIGHT));
    }
}

static void paint(REBAR *r, HDC dc)
{
    RECT client;
    GetClientRect(r->Hwnd, &client);
    COLORREF back = r->Back == CLR_NONE ? GetSysColor(COLOR_BTNFACE) : r->Back;
    CcFillSolid(dc, &client, back);
    HGDIOBJ old = SelectObject(dc, bar_font(r));
    SetBkMode(dc, TRANSPARENT);
    int visible = 0;
    for (int k = 0; k < r->Count; k++)
        if (band_visible(&r->Bands[k]))
            visible++;
    for (int k = 0; k < r->Count; k++) {
        BAND *b = &r->Bands[k];
        if (!band_visible(b))
            continue;
        RECT band = b->Rect;
        if (b->Back != CLR_DEFAULT && b->Back != CLR_NONE)
            CcFillSolid(dc, &band, b->Back);
        BOOL grip = show_grip(r, b, visible);
        if (grip)
            draw_gripper(r, dc, &band, band.bottom - band.top);
        int x = band.left + (grip ? GRIPPER_WIDTH : 0);
        if (r->Images && b->Image >= 0) {
            int cx = 0, cy = 0;
            ImageList_GetIconSize(r->Images, &cx, &cy);
            ImageList_Draw(r->Images, b->Image, dc, x, (band.top + band.bottom - cy) / 2, ILD_TRANSPARENT);
            x += cx + 2;
        }
        if (b->Text && *b->Text && !(b->Style & RBBS_HIDETITLE)) {
            SetTextColor(dc, b->Fore != CLR_DEFAULT ? b->Fore : r->Text == CLR_NONE ? GetSysColor(COLOR_BTNTEXT) : r->Text);
            RECT text = {x, band.top, band.left + (int)b->CxHeader, band.bottom};
            DrawTextW(dc, b->Text, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
        /* a line above every row after the first and before every band after the first of its row. */
        if (r->Style & RBS_BANDBORDERS) {
            if (b->Row > 0) {
                RECT line = {band.left, band.top, band.right, band.top + 1};
                CcFillSolid(dc, &line, GetSysColor(COLOR_BTNSHADOW));
                RECT light = {band.left, band.top + 1, band.right, band.top + 2};
                CcFillSolid(dc, &light, GetSysColor(COLOR_BTNHILIGHT));
            }
        }
    }
    SelectObject(dc, old);
}

static int hit_test(REBAR *r, POINT pt, UINT *flags)
{
    int visible = 0;
    for (int k = 0; k < r->Count; k++)
        if (band_visible(&r->Bands[k]))
            visible++;
    for (int k = 0; k < r->Count; k++) {
        BAND *b = &r->Bands[k];
        if (!band_visible(b) || !PtInRect(&b->Rect, pt))
            continue;
        BOOL grip = show_grip(r, b, visible);
        if (grip && pt.x < b->Rect.left + GRIPPER_WIDTH)
            *flags = RBHT_GRABBER;
        else if (pt.x < b->Rect.left + (int)b->CxHeader)
            *flags = RBHT_CAPTION;
        else
            *flags = RBHT_CLIENT;
        return k;
    }
    *flags = RBHT_NOWHERE;
    return -1;
}

LRESULT CALLBACK RebarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    REBAR *r = get_data(hwnd);
    if (!r && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        r = CcAlloc(sizeof(*r));
        if (!r)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)r);
        r->Hwnd = hwnd;
        r->Style = (DWORD)cs->style;
        r->Back = CLR_NONE;
        r->Text = CLR_NONE;
        r->Vertical = (r->Style & CCS_VERT) != 0;
        CcNotifyInit(&r->Notify, hwnd, cs->hwndParent);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_CREATE:
        r->Theme = OpenThemeData(hwnd, L"Rebar");
        return 0;
    case WM_DESTROY:
        if (r->Theme)
            CloseThemeData(r->Theme);
        r->Theme = nullptr;
        return 0;
    case WM_NCDESTROY:
        for (int k = 0; k < r->Count; k++)
            CcFree(r->Bands[k].Text);
        CcFree(r->Bands);
        CcFree(r->RowHeights);
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(r);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND: {
        /* children that draw the bar's background ask for it with a device context of their own. */
        RECT client;
        GetClientRect(hwnd, &client);
        CcFillSolid((HDC)wparam, &client, r->Back == CLR_NONE ? GetSysColor(COLOR_BTNFACE) : r->Back);
        return 1;
    }
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (dc) {
            RECT client;
            GetClientRect(hwnd, &client);
            CC_DRAW_BUFFER b;
            paint(r, wparam ? dc : CcBeginBuffer(&b, dc, &client));
            if (!wparam)
                CcEndBuffer(&b);
        }
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (!r->InSize) {
            follow_parent(r);
            relayout(r);
            if (r->Style & RBS_AUTOSIZE) {
                NMRBAUTOSIZE nm = {};
                GetClientRect(hwnd, &nm.rcTarget);
                nm.rcActual = nm.rcTarget;
                CcNotify(&r->Notify, RBN_AUTOSIZE, &nm.hdr);
            }
        }
        return 0;
    case WM_SETFONT:
        r->Font = (HFONT)wparam;
        if (lparam)
            relayout(r);
        return 0;
    case WM_GETFONT:
        return (LRESULT)r->Font;
    case WM_STYLECHANGED:
        if (wparam == (WPARAM)GWL_STYLE) {
            r->Style = ((STYLESTRUCT *)lparam)->styleNew;
            relayout(r);
        }
        return 0;
    case WM_THEMECHANGED:
        if (r->Theme)
            CloseThemeData(r->Theme);
        r->Theme = OpenThemeData(hwnd, L"Rebar");
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_NOTIFYFORMAT:
        return CcNotifyFormat(&r->Notify, (HWND)wparam, lparam);
    case WM_LBUTTONDBLCLK: {
        if (!(r->Style & RBS_DBLCLKTOGGLE))
            break;
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        UINT flags;
        int k = hit_test(r, pt, &flags);
        if (k >= 0 && flags != RBHT_CLIENT) {
            BAND *b = &r->Bands[k];
            /* a band that is as wide as it likes goes to its smallest and back. */
            int smallest = (int)b->CxHeader + (int)b->CxMin;
            b->Cx = (int)b->Cx > smallest ? (UINT)smallest : (b->CxIdeal ? b->CxIdeal : (UINT)smallest + 100);
            relayout(r);
        }
        return 0;
    }
    case RB_INSERTBANDA:
    case RB_INSERTBANDW:
        return insert_band(r, (int)wparam, (const REBARBANDINFOW *)lparam, msg == RB_INSERTBANDA);
    case RB_SETBANDINFOA:
    case RB_SETBANDINFOW:
        return set_info(r, (int)wparam, (const REBARBANDINFOW *)lparam, msg == RB_SETBANDINFOA);
    case RB_GETBANDINFOA:
    case RB_GETBANDINFOW:
        return get_info(r, (int)wparam, (REBARBANDINFOW *)lparam, msg == RB_GETBANDINFOA);
    case RB_DELETEBAND:
        if (!band_valid(r, (int)wparam))
            return FALSE;
        delete_band(r, (int)wparam);
        return TRUE;
    case RB_GETBANDCOUNT:
        return r->Count;
    case RB_GETROWCOUNT:
        return r->Rows;
    case RB_GETROWHEIGHT:
        return (int)wparam >= 0 && (int)wparam < r->Rows ? r->RowHeights[wparam] : 0;
    case RB_GETBARHEIGHT:
        return r->BarHeight;
    case RB_GETRECT: {
        if (!band_valid(r, (int)wparam) || !lparam)
            return FALSE;
        *(RECT *)lparam = r->Bands[wparam].Rect;
        return TRUE;
    }
    case RB_IDTOINDEX:
        for (int k = 0; k < r->Count; k++)
            if (r->Bands[k].Id == (UINT)wparam)
                return k;
        return -1;
    case RB_HITTEST: {
        RBHITTESTINFO *info = (RBHITTESTINFO *)lparam;
        if (!info)
            return -1;
        info->iBand = hit_test(r, info->pt, &info->flags);
        return info->iBand;
    }
    case RB_GETBARINFO: {
        REBARINFO *info = (REBARINFO *)lparam;
        if (!info || info->cbSize < sizeof(REBARINFO))
            return FALSE;
        info->himl = r->Images;
        return TRUE;
    }
    case RB_SETBARINFO: {
        REBARINFO *info = (REBARINFO *)lparam;
        if (!info || info->cbSize < sizeof(REBARINFO))
            return FALSE;
        if (info->fMask & RBIM_IMAGELIST)
            r->Images = info->himl;
        relayout(r);
        return TRUE;
    }
    case RB_SETBKCOLOR: {
        COLORREF old = r->Back == CLR_NONE ? CLR_DEFAULT : r->Back;
        r->Back = (COLORREF)lparam == CLR_DEFAULT ? CLR_NONE : (COLORREF)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return (LRESULT)old;
    }
    case RB_GETBKCOLOR:
        return (LRESULT)(r->Back == CLR_NONE ? CLR_DEFAULT : r->Back);
    case RB_SETTEXTCOLOR: {
        COLORREF old = r->Text == CLR_NONE ? CLR_DEFAULT : r->Text;
        r->Text = (COLORREF)lparam == CLR_DEFAULT ? CLR_NONE : (COLORREF)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return (LRESULT)old;
    }
    case RB_GETTEXTCOLOR:
        return (LRESULT)(r->Text == CLR_NONE ? CLR_DEFAULT : r->Text);
    case RB_SETPARENT: {
        HWND old = r->Notify.Parent;
        r->Notify.Parent = (HWND)wparam;
        return (LRESULT)old;
    }
    case RB_GETTOOLTIPS:
        return (LRESULT)r->Tooltips;
    case RB_SETTOOLTIPS:
        r->Tooltips = (HWND)wparam;
        return 0;
    case RB_SIZETORECT: {
        const RECT *rc = (const RECT *)lparam;
        if (!rc)
            return FALSE;
        r->InSize = TRUE;
        SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        r->InSize = FALSE;
        relayout(r);
        return TRUE;
    }
    case RB_SHOWBAND:
        if (!band_valid(r, (int)wparam))
            return FALSE;
        if (lparam)
            r->Bands[wparam].Style &= ~(UINT)RBBS_HIDDEN;
        else
            r->Bands[wparam].Style |= RBBS_HIDDEN;
        relayout(r);
        return TRUE;
    case RB_MINIMIZEBAND:
        if (!band_valid(r, (int)wparam))
            return 0;
        r->Bands[wparam].Cx = 0;
        relayout(r);
        return 0;
    case RB_MAXIMIZEBAND: {
        if (!band_valid(r, (int)wparam))
            return 0;
        RECT client;
        GetClientRect(hwnd, &client);
        BAND *b = &r->Bands[wparam];
        b->Cx = lparam && b->CxIdeal ? b->CxIdeal + b->CxHeader : (UINT)(client.right - client.left);
        relayout(r);
        return 0;
    }
    case RB_SETBANDWIDTH:
        if (!band_valid(r, (int)wparam))
            return FALSE;
        r->Bands[wparam].Cx = (UINT)lparam;
        relayout(r);
        return TRUE;
    case RB_MOVEBAND: {
        int from = (int)wparam, to = (int)lparam;
        if (!band_valid(r, from) || !band_valid(r, to))
            return FALSE;
        BAND moving = r->Bands[from];
        if (from < to)
            memmove(&r->Bands[from], &r->Bands[from + 1], (SIZE_T)(to - from) * sizeof(BAND));
        else
            memmove(&r->Bands[to + 1], &r->Bands[to], (SIZE_T)(from - to) * sizeof(BAND));
        r->Bands[to] = moving;
        CcNotifyHdr(&r->Notify, RBN_LAYOUTCHANGED);
        relayout(r);
        return TRUE;
    }
    case RB_GETBANDBORDERS: {
        RECT *border = (RECT *)lparam;
        if (!band_valid(r, (int)wparam) || !border)
            return 0;
        SetRect(border, (int)r->Bands[wparam].CxHeader, (r->Style & RBS_BANDBORDERS) ? 1 : 0, 0,
                (r->Style & RBS_BANDBORDERS) ? 1 : 0);
        return 0;
    }
    case RB_GETBANDMARGINS: {
        MARGINS *margins = (MARGINS *)lparam;
        if (margins) {
            margins->cxLeftWidth = r->Count ? (int)r->Bands[0].CxHeader : 0;
            margins->cxRightWidth = 0;
            margins->cyTopHeight = (r->Style & RBS_BANDBORDERS) ? 1 : 0;
            margins->cyBottomHeight = margins->cyTopHeight;
        }
        return 0;
    }
    case RB_SETEXTENDEDSTYLE: {
        DWORD old = r->ExStyle;
        r->ExStyle = wparam ? (r->ExStyle & ~(DWORD)wparam) | ((DWORD)lparam & (DWORD)wparam) : (DWORD)lparam;
        return (LRESULT)old;
    }
    case RB_GETEXTENDEDSTYLE:
        return (LRESULT)r->ExStyle;
    case RB_BEGINDRAG:
    case RB_ENDDRAG:
    case RB_DRAGMOVE:
    case RB_PUSHCHEVRON:
    case RB_SETPALETTE:
    case RB_GETPALETTE:
    case RB_GETDROPTARGET:
        return 0;
    case RB_SETUNICODEFORMAT: {
        BOOL old = r->Notify.Unicode;
        r->Notify.Unicode = wparam != 0;
        return old;
    }
    case RB_GETUNICODEFORMAT:
        return r->Notify.Unicode;
    case RB_SETWINDOWTHEME:
        return 0;
    case WM_GETOBJECT:
        if ((LONG)lparam == -12)
            return 0x1000F;
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
