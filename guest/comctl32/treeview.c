/* the tree view, SysTreeView32. */
#include "comctl32_private.h"

#define TREE_MAGIC 0x45455254

typedef struct _TREEITEM {
    DWORD Magic;
    struct _TREEITEM *Parent, *Child, *Next, *Prev;
    LPWSTR Text;
    int Image, Selected, Children, Integral;
    UINT State;
    LPARAM Param;
    /* -1 when hidden under a collapsed parent. */
    int Row;
} NODE;

typedef struct TREEVIEW {
    HWND Hwnd;
    CC_NOTIFY Notify;
    DWORD Style, ExStyle;
    NODE Root;
    int Count;
    NODE *Caret, *Drop, *Hot;
    HIMAGELIST Normal, StateList;
    BOOL OwnStateList;
    HFONT Font;
    HTHEME Theme;
    int Indent, ItemHeight;
    BOOL CustomHeight;
    int Top, ScrollX, Rows;
    COLORREF Bk, Text, Line, InsertMark;
    HWND Tooltips;
    UINT ScrollTime;
} TREEVIEW;

static TREEVIEW *get_data(HWND hwnd)
{
    return (TREEVIEW *)GetWindowLongPtrW(hwnd, 0);
}

static NODE *node(TREEVIEW *tv, HTREEITEM h)
{
    if (!h || h == TVI_ROOT)
        return nullptr;
    if (((ULONG_PTR)h & 3) || (ULONG_PTR)h >= (ULONG_PTR)TVI_ROOT)
        return nullptr;
    NODE *n = (NODE *)h;
    return n->Magic == TREE_MAGIC ? n : nullptr;
}

static HFONT tree_font(TREEVIEW *tv)
{
    return tv->Font ? tv->Font : CcIconTitleFont();
}

static void compute_height(TREEVIEW *tv)
{
    if (tv->CustomHeight)
        return;
    HDC dc = GetDC(tv->Hwnd);
    HGDIOBJ old = SelectObject(dc, tree_font(tv));
    SIZE size = {};
    GetTextExtentPoint32W(dc, L"J", 1, &size);
    SelectObject(dc, old);
    ReleaseDC(tv->Hwnd, dc);
    int h = size.cy + 2 * GetSystemMetrics(SM_CYBORDER);
    int cy = 0;
    if (tv->Normal)
        ImageList_GetIconSize(tv->Normal, nullptr, &cy);
    if (cy > h)
        h = cy;
    h += 1;
    if (!(tv->Style & TVS_NONEVENHEIGHT))
        h &= ~1;
    tv->ItemHeight = h > 1 ? h : 2;
}

/* the next item in display order, descending into expanded children. */
static NODE *next_visible(NODE *n)
{
    if (n->Child && (n->State & TVIS_EXPANDED))
        return n->Child;
    while (n) {
        if (n->Next)
            return n->Next;
        n = n->Parent;
        if (n && !n->Parent && n->Magic != TREE_MAGIC)
            return nullptr;
    }
    return nullptr;
}

static int depth(NODE *n)
{
    int d = 0;
    while (n->Parent && n->Parent->Magic == TREE_MAGIC) {
        d++;
        n = n->Parent;
    }
    return d;
}

static void number_rows(TREEVIEW *tv)
{
    int row = 0;
    for (NODE *n = tv->Root.Child; n; n = next_visible(n))
        n->Row = row++;
    tv->Rows = row;
}

static void hide_rows(NODE *n)
{
    for (; n; n = n->Next) {
        n->Row = -1;
        hide_rows(n->Child);
    }
}

static void relayout(TREEVIEW *tv)
{
    hide_rows(tv->Root.Child);
    number_rows(tv);
    RECT client;
    GetClientRect(tv->Hwnd, &client);
    int page = client.bottom / (tv->ItemHeight > 0 ? tv->ItemHeight : 1);
    if (tv->Top > tv->Rows - page)
        tv->Top = tv->Rows - page > 0 ? tv->Rows - page : 0;
    if (!(tv->Style & TVS_NOSCROLL)) {
        SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, tv->Rows > 0 ? tv->Rows - 1 : 0, (UINT)page,
                         tv->Top};
        SetScrollInfo(tv->Hwnd, SB_VERT, &si, TRUE);
    }
    InvalidateRect(tv->Hwnd, nullptr, TRUE);
}

static BOOL has_children(TREEVIEW *tv, NODE *n)
{
    if (n->Children == I_CHILDRENCALLBACK) {
        NMTVDISPINFOW nm = {};
        nm.item.mask = TVIF_CHILDREN | TVIF_HANDLE;
        nm.item.hItem = n;
        nm.item.lParam = n->Param;
        CcNotify(&tv->Notify, TVN_GETDISPINFOW, &nm.hdr);
        return nm.item.cChildren != 0;
    }
    return n->Child != nullptr || n->Children > 0;
}

static void item_text(TREEVIEW *tv, NODE *n, LPWSTR out, int max)
{
    if (!CcIsCallback(n->Text)) {
        CcCopyText(out, max, n->Text);
        return;
    }
    out[0] = 0;
    if (tv->Notify.Unicode) {
        NMTVDISPINFOW nm = {};
        nm.item.mask = TVIF_TEXT | TVIF_HANDLE;
        nm.item.hItem = n;
        nm.item.pszText = out;
        nm.item.cchTextMax = max;
        nm.item.lParam = n->Param;
        CcNotify(&tv->Notify, TVN_GETDISPINFOW, &nm.hdr);
        if (nm.item.pszText && nm.item.pszText != out && !CcIsCallback(nm.item.pszText))
            CcCopyText(out, max, nm.item.pszText);
        if (nm.item.mask & TVIF_DI_SETITEM)
            CcSetText(&n->Text, out);
    } else {
        char buffer[512] = "";
        NMTVDISPINFOA nm = {};
        nm.item.mask = TVIF_TEXT | TVIF_HANDLE;
        nm.item.hItem = n;
        nm.item.pszText = buffer;
        nm.item.cchTextMax = sizeof(buffer);
        nm.item.lParam = n->Param;
        CcNotify(&tv->Notify, TVN_GETDISPINFOW, &nm.hdr);
        if (nm.item.pszText && nm.item.pszText != LPSTR_TEXTCALLBACKA)
            MultiByteToWideChar(CP_ACP, 0, nm.item.pszText, -1, out, max);
        out[max - 1] = 0;
    }
}

static int item_image(TREEVIEW *tv, NODE *n, BOOL selected)
{
    int image = selected ? n->Selected : n->Image;
    if (image == I_IMAGECALLBACK) {
        NMTVDISPINFOW nm = {};
        nm.item.mask = (selected ? TVIF_SELECTEDIMAGE : TVIF_IMAGE) | TVIF_HANDLE;
        nm.item.hItem = n;
        nm.item.lParam = n->Param;
        CcNotify(&tv->Notify, TVN_GETDISPINFOW, &nm.hdr);
        image = selected ? nm.item.iSelectedImage : nm.item.iImage;
    }
    return image;
}

/* a visible row's parts: the indent with the button, the state and normal images, the label. */
typedef struct ROW_RECTS {
    RECT Row, Button, State, Image, Label;
} ROW_RECTS;

static BOOL row_rects(TREEVIEW *tv, NODE *n, ROW_RECTS *r)
{
    if (n->Row < 0)
        return FALSE;
    RECT client;
    GetClientRect(tv->Hwnd, &client);
    int y = (n->Row - tv->Top) * tv->ItemHeight;
    SetRect(&r->Row, 0, y, client.right, y + tv->ItemHeight);
    int level = depth(n) + ((tv->Style & TVS_LINESATROOT) ? 1 : 0);
    int x = level * tv->Indent - tv->ScrollX;
    SetRect(&r->Button, x - tv->Indent, y, x, y + tv->ItemHeight);
    int state_cx = 0, image_cx = 0;
    if (tv->StateList)
        ImageList_GetIconSize(tv->StateList, &state_cx, nullptr);
    if (tv->Normal)
        ImageList_GetIconSize(tv->Normal, &image_cx, nullptr);
    SetRect(&r->State, x, y, x + state_cx, y + tv->ItemHeight);
    x += state_cx;
    SetRect(&r->Image, x, y, x + image_cx, y + tv->ItemHeight);
    x += image_cx + (image_cx ? 3 : 0);
    WCHAR text[260];
    item_text(tv, n, text, 260);
    HDC dc = GetDC(tv->Hwnd);
    HGDIOBJ old = SelectObject(dc, tree_font(tv));
    SIZE size = {};
    if (text[0])
        GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, old);
    ReleaseDC(tv->Hwnd, dc);
    SetRect(&r->Label, x, y, x + size.cx + 4, y + tv->ItemHeight);
    return TRUE;
}

/* ------------------------------------------------------------------ notifications */

static void fill_item(TVITEMW *item, NODE *n)
{
    memset(item, 0, sizeof(*item));
    if (!n)
        return;
    item->mask = TVIF_HANDLE | TVIF_STATE | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
    item->hItem = n;
    item->state = n->State;
    item->stateMask = 0xFFFF;
    item->iImage = n->Image;
    item->iSelectedImage = n->Selected;
    item->cChildren = n->Children;
    item->lParam = n->Param;
}

static LRESULT notify_tree(TREEVIEW *tv, UINT code, UINT action, NODE *old, NODE *new_)
{
    NMTREEVIEWW nm = {};
    nm.action = action;
    fill_item(&nm.itemOld, old);
    fill_item(&nm.itemNew, new_);
    return CcNotify(&tv->Notify, code, &nm.hdr);
}

/* ------------------------------------------------------------------ changes */

static void ensure_visible(TREEVIEW *tv, NODE *n);

static BOOL expand(TREEVIEW *tv, NODE *n, UINT action, BOOL notify)
{
    if (!n)
        return FALSE;
    UINT code = action & 3;
    BOOL expanded = (n->State & TVIS_EXPANDED) != 0;
    if (code == TVE_TOGGLE)
        code = expanded ? TVE_COLLAPSE : TVE_EXPAND;
    if (code == TVE_EXPAND && expanded)
        return FALSE;
    if (code == TVE_COLLAPSE && !expanded && !(action & TVE_COLLAPSERESET))
        return FALSE;
    if (!has_children(tv, n))
        return FALSE;
    if (notify && notify_tree(tv, TVN_ITEMEXPANDINGW, code, nullptr, n))
        return FALSE;
    if (code == TVE_EXPAND)
        n->State |= TVIS_EXPANDED | TVIS_EXPANDEDONCE;
    else
        n->State &= ~TVIS_EXPANDED;
    if (code == TVE_COLLAPSE && (action & TVE_COLLAPSERESET)) {
        n->State &= ~TVIS_EXPANDEDONCE;
    }
    relayout(tv);
    if (notify)
        notify_tree(tv, TVN_ITEMEXPANDEDW, code, nullptr, n);
    return TRUE;
}

static BOOL select_item(TREEVIEW *tv, NODE *n, UINT cause)
{
    if (n == tv->Caret)
        return TRUE;
    if (notify_tree(tv, TVN_SELCHANGINGW, cause, tv->Caret, n))
        return FALSE;
    NODE *old = tv->Caret;
    if (old)
        old->State &= ~TVIS_SELECTED;
    tv->Caret = n;
    if (n) {
        n->State |= TVIS_SELECTED;
        for (NODE *p = n->Parent; p && p->Magic == TREE_MAGIC; p = p->Parent)
            if (!(p->State & TVIS_EXPANDED))
                expand(tv, p, TVE_EXPAND, TRUE);
        if ((tv->Style & TVS_SINGLEEXPAND) && cause != TVC_UNKNOWN) {
            expand(tv, n, TVE_EXPAND, TRUE);
            if (old && old->Magic == TREE_MAGIC && old != n->Parent)
                expand(tv, old, TVE_COLLAPSE, TRUE);
        }
        ensure_visible(tv, n);
    }
    InvalidateRect(tv->Hwnd, nullptr, TRUE);
    notify_tree(tv, TVN_SELCHANGEDW, cause, old, n);
    return TRUE;
}

static void ensure_visible(TREEVIEW *tv, NODE *n)
{
    for (NODE *p = n->Parent; p && p->Magic == TREE_MAGIC; p = p->Parent)
        if (!(p->State & TVIS_EXPANDED))
            expand(tv, p, TVE_EXPAND, TRUE);
    number_rows(tv);
    RECT client;
    GetClientRect(tv->Hwnd, &client);
    int page = client.bottom / (tv->ItemHeight > 0 ? tv->ItemHeight : 1);
    if (page < 1)
        page = 1;
    if (n->Row < tv->Top)
        tv->Top = n->Row;
    else if (n->Row >= tv->Top + page)
        tv->Top = n->Row - page + 1;
    relayout(tv);
}

static int compare_text(NODE *a, NODE *b)
{
    return lstrcmpiW(a->Text && !CcIsCallback(a->Text) ? a->Text : L"",
                     b->Text && !CcIsCallback(b->Text) ? b->Text : L"");
}

static void link_node(NODE *parent, NODE *n, NODE *after, BOOL sorted)
{
    n->Parent = parent;
    if (sorted) {
        after = nullptr;
        for (NODE *c = parent->Child; c && compare_text(c, n) <= 0; c = c->Next)
            after = c;
    }
    if (!after) {
        n->Next = parent->Child;
        if (parent->Child)
            parent->Child->Prev = n;
        parent->Child = n;
        n->Prev = nullptr;
    } else {
        n->Next = after->Next;
        n->Prev = after;
        if (after->Next)
            after->Next->Prev = n;
        after->Next = n;
    }
}

static void unlink_node(NODE *n)
{
    if (n->Prev)
        n->Prev->Next = n->Next;
    else if (n->Parent)
        n->Parent->Child = n->Next;
    if (n->Next)
        n->Next->Prev = n->Prev;
    n->Next = n->Prev = nullptr;
}

static NODE *insert_item(TREEVIEW *tv, const TVINSERTSTRUCTW *ins, BOOL ansi)
{
    if (!ins)
        return nullptr;
    NODE *parent = (!ins->hParent || ins->hParent == TVI_ROOT) ? &tv->Root : node(tv, ins->hParent);
    if (!parent)
        return nullptr;
    NODE *after = nullptr;
    BOOL sorted = FALSE;
    if (ins->hInsertAfter == TVI_FIRST) {
        after = nullptr;
    } else if (ins->hInsertAfter == TVI_SORT) {
        sorted = TRUE;
    } else if (ins->hInsertAfter == TVI_LAST || !ins->hInsertAfter || ins->hInsertAfter == TVI_ROOT) {
        for (after = parent->Child; after && after->Next; after = after->Next)
            ;
    } else {
        after = node(tv, ins->hInsertAfter);
        if (!after || after->Parent != parent)
            for (after = parent->Child; after && after->Next; after = after->Next)
                ;
    }
    NODE *n = CcAlloc(sizeof(*n));
    if (!n)
        return nullptr;
    n->Magic = TREE_MAGIC;
    n->Row = -1;
    const TVITEMW *it = &ins->item;
    if (it->mask & TVIF_TEXT) {
        BOOL ok = ansi ? CcSetTextA(&n->Text, (LPCSTR)it->pszText) : CcSetText(&n->Text, it->pszText);
        if (!ok) {
            CcFree(n);
            return nullptr;
        }
    }
    if (it->mask & TVIF_IMAGE)
        n->Image = it->iImage;
    if (it->mask & TVIF_SELECTEDIMAGE)
        n->Selected = it->iSelectedImage;
    if (it->mask & TVIF_CHILDREN)
        n->Children = it->cChildren;
    if (it->mask & TVIF_PARAM)
        n->Param = it->lParam;
    if (it->mask & TVIF_STATE)
        n->State = it->state & it->stateMask & ~TVIS_SELECTED;
    if ((tv->Style & TVS_CHECKBOXES) && !(n->State & TVIS_STATEIMAGEMASK))
        n->State |= INDEXTOSTATEIMAGEMASK(1);
    link_node(parent, n, after, sorted);
    tv->Count++;
    relayout(tv);
    return n;
}

static void delete_subtree(TREEVIEW *tv, NODE *n)
{
    while (n->Child)
        delete_subtree(tv, n->Child);
    notify_tree(tv, TVN_DELETEITEMW, 0, n, nullptr);
    unlink_node(n);
    if (tv->Drop == n)
        tv->Drop = nullptr;
    if (tv->Hot == n)
        tv->Hot = nullptr;
    if (n->Text != LPSTR_TEXTCALLBACKW)
        CcFree(n->Text);
    n->Magic = 0;
    CcFree(n);
    tv->Count--;
}

static BOOL delete_item(TREEVIEW *tv, HTREEITEM h)
{
    if (!h || h == TVI_ROOT) {
        if (tv->Caret)
            select_item(tv, nullptr, TVC_UNKNOWN);
        while (tv->Root.Child)
            delete_subtree(tv, tv->Root.Child);
        tv->Top = 0;
        relayout(tv);
        return TRUE;
    }
    NODE *n = node(tv, h);
    if (!n)
        return FALSE;
    /* the caret leaves the deleted branch for its neighbor or parent. */
    for (NODE *c = tv->Caret; c && c->Magic == TREE_MAGIC; c = c->Parent)
        if (c == n) {
            NODE *next = n->Next ? n->Next : n->Prev ? n->Prev : (n->Parent && n->Parent->Magic == TREE_MAGIC ? n->Parent : nullptr);
            if (!select_item(tv, next, TVC_UNKNOWN)) {
                tv->Caret = nullptr;
            }
            break;
        }
    NODE *parent = n->Parent;
    delete_subtree(tv, n);
    if (parent && parent->Magic == TREE_MAGIC && !parent->Child)
        parent->State &= ~TVIS_EXPANDED;
    relayout(tv);
    return TRUE;
}

static BOOL get_item(TREEVIEW *tv, TVITEMW *it, BOOL ansi)
{
    if (!it)
        return FALSE;
    NODE *n = node(tv, it->hItem);
    if (!n)
        return FALSE;
    if (it->mask & TVIF_TEXT) {
        WCHAR text[1024];
        item_text(tv, n, text, 1024);
        if (ansi)
            CcCopyTextA((LPSTR)it->pszText, it->cchTextMax, text);
        else
            CcCopyText(it->pszText, it->cchTextMax, text);
    }
    if (it->mask & TVIF_IMAGE)
        it->iImage = item_image(tv, n, FALSE);
    if (it->mask & TVIF_SELECTEDIMAGE)
        it->iSelectedImage = item_image(tv, n, TRUE);
    if (it->mask & TVIF_CHILDREN)
        it->cChildren = n->Children == I_CHILDRENCALLBACK ? has_children(tv, n) : (n->Child ? 1 : n->Children);
    if (it->mask & TVIF_PARAM)
        it->lParam = n->Param;
    if (it->mask & TVIF_STATE)
        it->state = n->State & it->stateMask;
    if ((it->mask & TVIF_INTEGRAL) && ansi == FALSE)
        ((TVITEMEXW *)it)->iIntegral = n->Integral ? n->Integral : 1;
    return TRUE;
}

static BOOL set_item(TREEVIEW *tv, const TVITEMW *it, BOOL ansi)
{
    if (!it)
        return FALSE;
    NODE *n = node(tv, it->hItem);
    if (!n)
        return FALSE;
    if (it->mask & TVIF_TEXT) {
        if (ansi)
            CcSetTextA(&n->Text, (LPCSTR)it->pszText);
        else
            CcSetText(&n->Text, it->pszText);
    }
    if (it->mask & TVIF_IMAGE)
        n->Image = it->iImage;
    if (it->mask & TVIF_SELECTEDIMAGE)
        n->Selected = it->iSelectedImage;
    if (it->mask & TVIF_CHILDREN)
        n->Children = it->cChildren;
    if (it->mask & TVIF_PARAM)
        n->Param = it->lParam;
    if (it->mask & TVIF_STATE) {
        UINT mask = it->stateMask & ~TVIS_SELECTED;
        n->State = (n->State & ~mask) | (it->state & mask);
        if (mask & TVIS_EXPANDED)
            relayout(tv);
    }
    InvalidateRect(tv->Hwnd, nullptr, TRUE);
    return TRUE;
}

static NODE *last_visible(TREEVIEW *tv)
{
    NODE *last = nullptr;
    for (NODE *n = tv->Root.Child; n; n = next_visible(n))
        last = n;
    return last;
}

static NODE *previous_visible(TREEVIEW *tv, NODE *n)
{
    NODE *prev = nullptr;
    for (NODE *m = tv->Root.Child; m && m != n; m = next_visible(m))
        prev = m;
    return prev;
}

static NODE *get_next(TREEVIEW *tv, UINT code, HTREEITEM h)
{
    NODE *n = node(tv, h);
    switch (code & 0xFF) {
    case TVGN_ROOT:
        return tv->Root.Child;
    case TVGN_NEXT:
        return n ? n->Next : nullptr;
    case TVGN_PREVIOUS:
        return n ? n->Prev : nullptr;
    case TVGN_PARENT:
        return n && n->Parent && n->Parent->Magic == TREE_MAGIC ? n->Parent : nullptr;
    case TVGN_CHILD:
        if (!h || h == TVI_ROOT)
            return tv->Root.Child;
        return n ? n->Child : nullptr;
    case TVGN_FIRSTVISIBLE: {
        for (NODE *m = tv->Root.Child; m; m = next_visible(m))
            if (m->Row == tv->Top)
                return m;
        return nullptr;
    }
    case TVGN_NEXTVISIBLE:
        return n && n->Row >= 0 ? next_visible(n) : nullptr;
    case TVGN_PREVIOUSVISIBLE:
        return n && n->Row >= 0 ? previous_visible(tv, n) : nullptr;
    case TVGN_DROPHILITE:
        return tv->Drop;
    case TVGN_CARET:
        return tv->Caret;
    case TVGN_LASTVISIBLE:
        return last_visible(tv);
    case TVGN_NEXTSELECTED:
        return h ? nullptr : tv->Caret;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ painting */

static void draw_dotted(HDC dc, int x0, int y0, int x1, int y1, COLORREF color)
{
    for (int x = x0; x <= x1; x += 2)
        for (int y = y0; y <= y1; y += 2)
            SetPixel(dc, x, y, color);
}

static void paint(TREEVIEW *tv, HDC dc, const RECT *update)
{
    RECT client;
    GetClientRect(tv->Hwnd, &client);
    COLORREF bk = tv->Bk == CLR_DEFAULT ? GetSysColor(COLOR_WINDOW) : tv->Bk;
    CcFillSolid(dc, update, bk);
    HGDIOBJ old = SelectObject(dc, tree_font(tv));
    SetBkMode(dc, TRANSPARENT);
    BOOL focused = GetFocus() == tv->Hwnd;
    COLORREF line = tv->Line == CLR_DEFAULT ? GetSysColor(COLOR_GRAYTEXT) : tv->Line;
    for (NODE *n = tv->Root.Child; n; n = next_visible(n)) {
        ROW_RECTS r;
        if (n->Row < tv->Top || !row_rects(tv, n, &r))
            continue;
        if (r.Row.top >= client.bottom)
            break;
        int mid = (r.Row.top + r.Row.bottom) / 2, cx = (r.Button.left + r.Button.right) / 2;
        BOOL at_root = !n->Parent || n->Parent->Magic != TREE_MAGIC;
        if ((tv->Style & TVS_HASLINES) && (!at_root || (tv->Style & TVS_LINESATROOT))) {
            draw_dotted(dc, cx, mid, r.Button.right - 2, mid, line);
            draw_dotted(dc, cx, n->Prev || !at_root ? r.Row.top : mid, cx, n->Next ? r.Row.bottom : mid, line);
            /* ancestor lines continue past this row while the ancestor has later siblings. */
            int x = cx - tv->Indent;
            for (NODE *p = n->Parent; p && p->Magic == TREE_MAGIC; p = p->Parent, x -= tv->Indent) {
                BOOL p_root = !p->Parent || p->Parent->Magic != TREE_MAGIC;
                if (p->Next && (!p_root || (tv->Style & TVS_LINESATROOT)))
                    draw_dotted(dc, x, r.Row.top, x, r.Row.bottom, line);
            }
        }
        if ((tv->Style & TVS_HASBUTTONS) && (!at_root || (tv->Style & TVS_LINESATROOT)) && has_children(tv, n)) {
            BOOL open = (n->State & TVIS_EXPANDED) != 0;
            SIZE size = {9, 9};
            if (tv->Theme)
                GetThemePartSize(tv->Theme, dc, CC_TVP_GLYPH, open ? 2 : 1, nullptr, TS_DRAW, &size);
            RECT g = {cx - size.cx / 2, mid - size.cy / 2, cx - size.cx / 2 + size.cx, mid - size.cy / 2 + size.cy};
            if (tv->Theme) {
                DrawThemeBackground(tv->Theme, dc, CC_TVP_GLYPH, open ? 2 : 1, &g, nullptr);
            } else {
                CcFillSolid(dc, &g, bk);
                FrameRect(dc, &g, GetSysColorBrush(COLOR_GRAYTEXT));
                RECT h = {g.left + 2, mid, g.right - 2, mid + 1};
                CcFillSolid(dc, &h, GetSysColor(COLOR_WINDOWTEXT));
                if (!open) {
                    RECT v = {cx, g.top + 2, cx + 1, g.bottom - 2};
                    CcFillSolid(dc, &v, GetSysColor(COLOR_WINDOWTEXT));
                }
            }
        }
        if (tv->StateList) {
            int si = (int)((n->State & TVIS_STATEIMAGEMASK) >> 12);
            int scy;
            ImageList_GetIconSize(tv->StateList, nullptr, &scy);
            if (si > 0)
                ImageList_Draw(tv->StateList, si, dc, r.State.left, r.Row.top + (tv->ItemHeight - scy) / 2,
                               ILD_NORMAL);
        }
        BOOL selected = n == tv->Caret || n == tv->Drop;
        if (tv->Normal) {
            int image = item_image(tv, n, n == tv->Caret);
            int icy;
            ImageList_GetIconSize(tv->Normal, nullptr, &icy);
            if (image >= 0)
                ImageList_Draw(tv->Normal, image, dc, r.Image.left, r.Row.top + (tv->ItemHeight - icy) / 2,
                               ILD_NORMAL | (n->State & TVIS_OVERLAYMASK));
        }
        WCHAR text[260];
        item_text(tv, n, text, 260);
        COLORREF text_color = tv->Text == CLR_DEFAULT ? GetSysColor(COLOR_WINDOWTEXT) : tv->Text;
        RECT label = r.Label;
        if ((tv->Style & TVS_FULLROWSELECT) && !(tv->Style & TVS_HASLINES))
            label.right = client.right;
        if (selected && (focused || n == tv->Drop)) {
            CcFillSolid(dc, &label, GetSysColor(COLOR_HIGHLIGHT));
            text_color = GetSysColor(COLOR_HIGHLIGHTTEXT);
        } else if (selected && (tv->Style & TVS_SHOWSELALWAYS)) {
            CcFillSolid(dc, &label, GetSysColor(COLOR_BTNFACE));
            text_color = GetSysColor(COLOR_BTNTEXT);
        }
        SetTextColor(dc, text_color);
        HFONT bold = nullptr;
        if (n->State & TVIS_BOLD) {
            LOGFONTW lf;
            GetObjectW(tree_font(tv), sizeof(lf), &lf);
            lf.lfWeight = FW_BOLD;
            bold = CreateFontIndirectW(&lf);
            SelectObject(dc, bold);
        }
        RECT tr = r.Label;
        tr.left += 2;
        DrawTextW(dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (bold) {
            SelectObject(dc, tree_font(tv));
            DeleteObject(bold);
        }
        if (n == tv->Caret && focused && !CcFocusCuesHidden(tv->Hwnd))
            DrawFocusRect(dc, &label);
    }
    SelectObject(dc, old);
}

static NODE *hit_test(TREEVIEW *tv, POINT pt, UINT *flags)
{
    RECT client;
    GetClientRect(tv->Hwnd, &client);
    *flags = 0;
    if (pt.x < client.left)
        *flags |= TVHT_TOLEFT;
    else if (pt.x >= client.right)
        *flags |= TVHT_TORIGHT;
    if (pt.y < client.top)
        *flags |= TVHT_ABOVE;
    else if (pt.y >= client.bottom)
        *flags |= TVHT_BELOW;
    if (*flags)
        return nullptr;
    int row = tv->Top + pt.y / (tv->ItemHeight > 0 ? tv->ItemHeight : 1);
    for (NODE *n = tv->Root.Child; n; n = next_visible(n)) {
        if (n->Row != row)
            continue;
        ROW_RECTS r;
        row_rects(tv, n, &r);
        if (pt.x < r.Button.left)
            *flags = TVHT_ONITEMINDENT;
        else if (pt.x < r.Button.right)
            *flags = TVHT_ONITEMBUTTON;
        else if (pt.x < r.State.right)
            *flags = TVHT_ONITEMSTATEICON;
        else if (pt.x < r.Image.right)
            *flags = TVHT_ONITEMICON;
        else if (pt.x < r.Label.right)
            *flags = TVHT_ONITEMLABEL;
        else
            *flags = TVHT_ONITEMRIGHT;
        return n;
    }
    *flags = TVHT_NOWHERE;
    return nullptr;
}

static void ensure_checkboxes(TREEVIEW *tv)
{
    if (tv->StateList)
        return;
    int cx = GetSystemMetrics(SM_CXMENUCHECK), cy = GetSystemMetrics(SM_CYMENUCHECK);
    HIMAGELIST list = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, 3, 3);
    if (!list)
        return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = cx * 3;
    bi.bmiHeader.biHeight = -cy;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits;
    HBITMAP bm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC mem = CreateCompatibleDC(nullptr);
    HTHEME theme = OpenThemeData(tv->Hwnd, L"Button");
    if (bm && mem) {
        HGDIOBJ old = SelectObject(mem, bm);
        for (int k = 1; k < 3; k++) {
            RECT r = {k * cx, 0, (k + 1) * cx, cy};
            if (theme) {
                DrawThemeBackground(theme, mem, CC_BP_CHECKBOX, k == 2 ? 5 : 1, &r, nullptr);
            } else {
                CcFillSolid(mem, &r, GetSysColor(COLOR_WINDOW));
                DrawFrameControl(mem, &r, DFC_BUTTON, DFCS_BUTTONCHECK | DFCS_FLAT | (k == 2 ? DFCS_CHECKED : 0));
                for (int y = 0; y < cy; y++)
                    for (int x = k * cx; x < (k + 1) * cx; x++)
                        ((DWORD *)bits)[y * cx * 3 + x] |= 0xFF000000;
            }
        }
        SelectObject(mem, old);
        ImageList_Add(list, bm, nullptr);
    }
    if (theme)
        CloseThemeData(theme);
    if (mem)
        DeleteDC(mem);
    if (bm)
        DeleteObject(bm);
    tv->StateList = list;
    tv->OwnStateList = TRUE;
}

static void sort_children(TREEVIEW *tv, NODE *parent, PFNTVCOMPARE compare, LPARAM param, BOOL recurse)
{
    int count = 0;
    for (NODE *c = parent->Child; c; c = c->Next)
        count++;
    if (count > 1) {
        NODE **list = CcAlloc((SIZE_T)count * sizeof(NODE *));
        if (!list)
            return;
        int k = 0;
        for (NODE *c = parent->Child; c; c = c->Next)
            list[k++] = c;
        /* an insertion sort keeps equal items in order. */
        for (int i = 1; i < count; i++) {
            NODE *v = list[i];
            int j = i - 1;
            while (j >= 0 && (compare ? compare(list[j]->Param, v->Param, param) : compare_text(list[j], v)) > 0) {
                list[j + 1] = list[j];
                j--;
            }
            list[j + 1] = v;
        }
        parent->Child = list[0];
        for (int i = 0; i < count; i++) {
            list[i]->Prev = i ? list[i - 1] : nullptr;
            list[i]->Next = i + 1 < count ? list[i + 1] : nullptr;
        }
        CcFree(list);
    }
    if (recurse)
        for (NODE *c = parent->Child; c; c = c->Next)
            sort_children(tv, c, compare, param, TRUE);
}

LRESULT CALLBACK TreeViewProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TREEVIEW *tv = get_data(hwnd);
    if (!tv && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        tv = CcAlloc(sizeof(*tv));
        if (!tv)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)tv);
        tv->Hwnd = hwnd;
        tv->Style = (DWORD)cs->style;
        tv->Indent = 19;
        tv->Bk = tv->Text = tv->Line = tv->InsertMark = CLR_DEFAULT;
        tv->ScrollTime = 400;
        CcNotifyInit(&tv->Notify, hwnd, cs->hwndParent);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_CREATE:
        tv->Theme = OpenThemeData(hwnd, L"TreeView");
        compute_height(tv);
        if (tv->Style & TVS_CHECKBOXES)
            ensure_checkboxes(tv);
        relayout(tv);
        return 0;
    case WM_DESTROY:
        delete_item(tv, TVI_ROOT);
        if (tv->Theme)
            CloseThemeData(tv->Theme);
        tv->Theme = nullptr;
        return 0;
    case WM_NCDESTROY:
        if (tv->OwnStateList && tv->StateList)
            ImageList_Destroy(tv->StateList);
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(tv);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_NCPAINT: {
        LRESULT result = DefWindowProcW(hwnd, msg, wparam, lparam);
        CcDrawThemedEdge(hwnd, tv->Theme);
        return result;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (dc) {
            RECT r;
            if (wparam)
                GetClientRect(hwnd, &r);
            else
                r = ps.rcPaint;
            CC_DRAW_BUFFER b;
            paint(tv, wparam ? dc : CcBeginBuffer(&b, dc, &r), &r);
            if (!wparam)
                CcEndBuffer(&b);
        }
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        relayout(tv);
        return 0;
    case WM_SETFONT:
        tv->Font = (HFONT)wparam;
        compute_height(tv);
        relayout(tv);
        return 0;
    case WM_GETFONT:
        return (LRESULT)tv->Font;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        CcNotifyHdr(&tv->Notify, msg == WM_SETFOCUS ? NM_SETFOCUS : NM_KILLFOCUS);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        UINT flags;
        NODE *n = hit_test(tv, pt, &flags);
        SetFocus(hwnd);
        if (msg == WM_RBUTTONDOWN) {
            if (!CcNotifyHdr(&tv->Notify, NM_RCLICK)) {
                POINT screen = pt;
                ClientToScreen(hwnd, &screen);
                SendMessageW(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd, MAKELPARAM(screen.x, screen.y));
            }
            return 0;
        }
        if (n && (flags & TVHT_ONITEMBUTTON)) {
            expand(tv, n, TVE_TOGGLE, TRUE);
        } else if (n && (flags & TVHT_ONITEMSTATEICON) && (tv->Style & TVS_CHECKBOXES)) {
            int image = (int)((n->State & TVIS_STATEIMAGEMASK) >> 12);
            n->State = (n->State & ~TVIS_STATEIMAGEMASK) | INDEXTOSTATEIMAGEMASK(image == 2 ? 1 : 2);
            InvalidateRect(hwnd, nullptr, TRUE);
        } else if (n && (flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT | TVHT_ONITEMINDENT))) {
            select_item(tv, n, TVC_BYMOUSE);
        }
        if (msg == WM_LBUTTONDBLCLK) {
            if (!CcNotifyHdr(&tv->Notify, NM_DBLCLK) && n && (flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT)))
                expand(tv, n, TVE_TOGGLE, TRUE);
        } else {
            CcNotifyHdr(&tv->Notify, NM_CLICK);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        NMKEY nm = {};
        nm.nVKey = (UINT)wparam;
        if (CcNotify(&tv->Notify, TVN_KEYDOWN, &nm.hdr))
            return 0;
        NODE *c = tv->Caret, *target = nullptr;
        RECT client;
        GetClientRect(hwnd, &client);
        int page = client.bottom / (tv->ItemHeight > 0 ? tv->ItemHeight : 1);
        switch (wparam) {
        case VK_UP:
            target = c ? previous_visible(tv, c) : tv->Root.Child;
            break;
        case VK_DOWN:
            target = c ? next_visible(c) : tv->Root.Child;
            break;
        case VK_HOME:
            target = tv->Root.Child;
            break;
        case VK_END:
            target = last_visible(tv);
            break;
        case VK_PRIOR:
        case VK_NEXT:
            target = c;
            for (int k = 0; target && k < (page > 1 ? page - 1 : 1); k++) {
                NODE *m = wparam == VK_NEXT ? next_visible(target) : previous_visible(tv, target);
                if (!m)
                    break;
                target = m;
            }
            break;
        case VK_LEFT:
            if (c && (c->State & TVIS_EXPANDED))
                expand(tv, c, TVE_COLLAPSE, TRUE);
            else if (c && c->Parent && c->Parent->Magic == TREE_MAGIC)
                target = c->Parent;
            break;
        case VK_RIGHT:
            if (c && !(c->State & TVIS_EXPANDED))
                expand(tv, c, TVE_EXPAND, TRUE);
            else if (c)
                target = c->Child;
            break;
        case VK_ADD:
            if (c)
                expand(tv, c, TVE_EXPAND, TRUE);
            break;
        case VK_SUBTRACT:
            if (c)
                expand(tv, c, TVE_COLLAPSE, TRUE);
            break;
        case VK_SPACE:
            if (c && (tv->Style & TVS_CHECKBOXES)) {
                int image = (int)((c->State & TVIS_STATEIMAGEMASK) >> 12);
                c->State = (c->State & ~TVIS_STATEIMAGEMASK) | INDEXTOSTATEIMAGEMASK(image == 2 ? 1 : 2);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;
        case VK_RETURN:
            CcNotifyHdr(&tv->Notify, NM_RETURN);
            break;
        }
        if (target)
            select_item(tv, target, TVC_BYKEYBOARD);
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        GetScrollInfo(hwnd, SB_VERT, &si);
        int top = tv->Top;
        switch (LOWORD(wparam)) {
        case SB_LINEUP:
            top--;
            break;
        case SB_LINEDOWN:
            top++;
            break;
        case SB_PAGEUP:
            top -= (int)si.nPage;
            break;
        case SB_PAGEDOWN:
            top += (int)si.nPage;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            top = si.nTrackPos;
            break;
        case SB_TOP:
            top = 0;
            break;
        case SB_BOTTOM:
            top = tv->Rows;
            break;
        }
        int max = tv->Rows - (int)si.nPage;
        tv->Top = top < 0 ? 0 : top > max ? (max > 0 ? max : 0) : top;
        relayout(tv);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int top = tv->Top - (short)HIWORD(wparam) / WHEEL_DELTA * 3;
        RECT client;
        GetClientRect(hwnd, &client);
        int max = tv->Rows - client.bottom / (tv->ItemHeight > 0 ? tv->ItemHeight : 1);
        tv->Top = top < 0 ? 0 : top > max ? (max > 0 ? max : 0) : top;
        relayout(tv);
        return 0;
    }
    case WM_THEMECHANGED:
        if (tv->Theme)
            CloseThemeData(tv->Theme);
        tv->Theme = OpenThemeData(hwnd, L"TreeView");
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_STYLECHANGED:
        if (wparam == (WPARAM)GWL_STYLE) {
            tv->Style = ((STYLESTRUCT *)lparam)->styleNew;
            if (tv->Style & TVS_CHECKBOXES)
                ensure_checkboxes(tv);
            relayout(tv);
        }
        return 0;
    case WM_NOTIFYFORMAT:
        return CcNotifyFormat(&tv->Notify, (HWND)wparam, lparam);
    case TVM_INSERTITEMA:
    case TVM_INSERTITEMW:
        return (LRESULT)insert_item(tv, (const TVINSERTSTRUCTW *)lparam, msg == TVM_INSERTITEMA);
    case TVM_DELETEITEM:
        return delete_item(tv, (HTREEITEM)lparam);
    case TVM_EXPAND:
        return expand(tv, node(tv, (HTREEITEM)lparam), (UINT)wparam, TRUE);
    case TVM_GETITEMRECT: {
        RECT *r = (RECT *)lparam;
        if (!r)
            return FALSE;
        NODE *n = node(tv, *(HTREEITEM *)lparam);
        ROW_RECTS rr;
        if (!n || !row_rects(tv, n, &rr))
            return FALSE;
        *r = wparam ? rr.Label : rr.Row;
        return TRUE;
    }
    case TVM_GETCOUNT:
        return tv->Count;
    case TVM_GETINDENT:
        return tv->Indent;
    case TVM_SETINDENT:
        tv->Indent = (int)wparam < 5 ? 5 : (int)wparam;
        relayout(tv);
        return 0;
    case TVM_GETIMAGELIST:
        return (LRESULT)(wparam == TVSIL_STATE ? tv->StateList : wparam == TVSIL_NORMAL ? tv->Normal : nullptr);
    case TVM_SETIMAGELIST: {
        HIMAGELIST old;
        if (wparam == TVSIL_NORMAL) {
            old = tv->Normal;
            tv->Normal = (HIMAGELIST)lparam;
        } else if (wparam == TVSIL_STATE) {
            old = tv->StateList;
            if (tv->OwnStateList && old) {
                ImageList_Destroy(old);
                old = nullptr;
            }
            tv->StateList = (HIMAGELIST)lparam;
            tv->OwnStateList = FALSE;
        } else {
            return 0;
        }
        compute_height(tv);
        relayout(tv);
        return (LRESULT)old;
    }
    case TVM_GETNEXTITEM:
        return (LRESULT)get_next(tv, (UINT)wparam, (HTREEITEM)lparam);
    case TVM_SELECTITEM: {
        NODE *n = node(tv, (HTREEITEM)lparam);
        if (lparam && !n)
            return FALSE;
        switch (wparam & 0xFF) {
        case TVGN_CARET:
            return select_item(tv, n, TVC_UNKNOWN);
        case TVGN_DROPHILITE:
            tv->Drop = n;
            InvalidateRect(hwnd, nullptr, TRUE);
            return TRUE;
        case TVGN_FIRSTVISIBLE:
            if (!n)
                return FALSE;
            ensure_visible(tv, n);
            tv->Top = n->Row;
            relayout(tv);
            return TRUE;
        }
        return FALSE;
    }
    case TVM_GETITEMA:
    case TVM_GETITEMW:
        return get_item(tv, (TVITEMW *)lparam, msg == TVM_GETITEMA);
    case TVM_SETITEMA:
    case TVM_SETITEMW:
        return set_item(tv, (const TVITEMW *)lparam, msg == TVM_SETITEMA);
    case TVM_GETITEMSTATE: {
        NODE *n = node(tv, (HTREEITEM)wparam);
        return n ? n->State & (UINT)lparam : 0;
    }
    case TVM_GETVISIBLECOUNT: {
        RECT client;
        GetClientRect(hwnd, &client);
        return client.bottom / (tv->ItemHeight > 0 ? tv->ItemHeight : 1);
    }
    case TVM_HITTEST: {
        TVHITTESTINFO *info = (TVHITTESTINFO *)lparam;
        if (!info)
            return 0;
        info->hItem = hit_test(tv, info->pt, &info->flags);
        return (LRESULT)info->hItem;
    }
    case TVM_ENSUREVISIBLE: {
        NODE *n = node(tv, (HTREEITEM)lparam);
        if (!n)
            return FALSE;
        ensure_visible(tv, n);
        return TRUE;
    }
    case TVM_SORTCHILDREN: {
        NODE *n = (!lparam || (HTREEITEM)lparam == TVI_ROOT) ? &tv->Root : node(tv, (HTREEITEM)lparam);
        if (!n)
            return FALSE;
        sort_children(tv, n, nullptr, 0, wparam != 0);
        relayout(tv);
        return TRUE;
    }
    case TVM_SORTCHILDRENCB: {
        TVSORTCB *cb = (TVSORTCB *)lparam;
        if (!cb)
            return FALSE;
        NODE *n = (!cb->hParent || cb->hParent == TVI_ROOT) ? &tv->Root : node(tv, cb->hParent);
        if (!n)
            return FALSE;
        sort_children(tv, n, cb->lpfnCompare, cb->lParam, FALSE);
        relayout(tv);
        return TRUE;
    }
    case TVM_GETITEMHEIGHT:
        return tv->ItemHeight;
    case TVM_SETITEMHEIGHT: {
        int old = tv->ItemHeight;
        if ((int)wparam == -1) {
            tv->CustomHeight = FALSE;
            compute_height(tv);
        } else {
            int h = (int)wparam < 1 ? 1 : (int)wparam;
            if (!(tv->Style & TVS_NONEVENHEIGHT))
                h &= ~1;
            tv->ItemHeight = h > 0 ? h : 1;
            tv->CustomHeight = TRUE;
        }
        relayout(tv);
        return old;
    }
    case TVM_SETBKCOLOR:
    case TVM_SETTEXTCOLOR:
    case TVM_SETLINECOLOR:
    case TVM_SETINSERTMARKCOLOR: {
        COLORREF *slot = msg == TVM_SETBKCOLOR ? &tv->Bk : msg == TVM_SETTEXTCOLOR ? &tv->Text
                         : msg == TVM_SETLINECOLOR ? &tv->Line : &tv->InsertMark;
        COLORREF old = *slot;
        *slot = (COLORREF)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return old == CLR_DEFAULT ? (LRESULT)-1 : old;
    }
    case TVM_GETBKCOLOR:
        return tv->Bk == CLR_DEFAULT ? (LRESULT)-1 : tv->Bk;
    case TVM_GETTEXTCOLOR:
        return tv->Text == CLR_DEFAULT ? (LRESULT)-1 : tv->Text;
    case TVM_GETLINECOLOR:
        return tv->Line;
    case TVM_GETINSERTMARKCOLOR:
        return tv->InsertMark;
    case TVM_SETINSERTMARK:
        return TRUE;
    case TVM_SETSCROLLTIME: {
        UINT old = tv->ScrollTime;
        tv->ScrollTime = (UINT)wparam < 100 ? 100 : (UINT)wparam;
        return old;
    }
    case TVM_GETSCROLLTIME:
        return tv->ScrollTime;
    case TVM_SETTOOLTIPS: {
        HWND old = tv->Tooltips;
        tv->Tooltips = (HWND)wparam;
        return (LRESULT)old;
    }
    case TVM_GETTOOLTIPS:
        return (LRESULT)tv->Tooltips;
    case TVM_SETEXTENDEDSTYLE: {
        tv->ExStyle = (tv->ExStyle & ~(DWORD)wparam) | ((DWORD)lparam & (DWORD)wparam);
        InvalidateRect(hwnd, nullptr, TRUE);
        return S_OK;
    }
    case TVM_GETEXTENDEDSTYLE:
        return tv->ExStyle;
    case TVM_EDITLABELA:
    case TVM_EDITLABELW:
    case TVM_GETEDITCONTROL:
    case TVM_CREATEDRAGIMAGE:
        return 0;
    case TVM_ENDEDITLABELNOW:
        return FALSE;
    case CCM_SETUNICODEFORMAT: {
        BOOL old = tv->Notify.Unicode;
        tv->Notify.Unicode = wparam != 0;
        return old;
    }
    case CCM_GETUNICODEFORMAT:
        return tv->Notify.Unicode;
    case CCM_SETVERSION:
    case CCM_GETVERSION:
        return 6;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
