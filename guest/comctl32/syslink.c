/*
 * the link control, SysLink: text with links in it, <a href="url" id="name">text</a>, wrapped to the width of the
 * control. A link is drawn underlined in the hot light color, answers a click or a key with NM_CLICK or NM_RETURN and
 * tells its text, address and identifier through LM_GETITEM.
 */
#include "comctl32_private.h"

#define LM_HITTEST (WM_USER + 0x300)
#define LM_GETIDEALHEIGHT (WM_USER + 0x301)
#define LM_SETITEM (WM_USER + 0x302)
#define LM_GETITEM (WM_USER + 0x303)
#define LM_GETIDEALSIZE (WM_USER + 0x311)

#define LIF_ITEMINDEX 0x1
#define LIF_STATE 0x2
#define LIF_ITEMID 0x4
#define LIF_URL 0x8

#define LIS_FOCUSED 0x1
#define LIS_ENABLED 0x2
#define LIS_VISITED 0x4
#define LIS_HOTTRACK 0x8
#define LIS_DEFAULTCOLORS 0x10

#define LWS_TRANSPARENT 0x1
#define LWS_IGNORERETURN 0x2
#define LWS_NOPREFIX 0x4
#define LWS_USEVISUALSTYLE 0x8
#define LWS_USECUSTOMTEXT 0x10
#define LWS_RIGHT 0x20

#define MAX_LINK_ID 48
#define MAX_LINK_URL 2084

typedef struct LITEM_ {
    UINT mask;
    int iLink;
    UINT state;
    UINT stateMask;
    WCHAR szID[MAX_LINK_ID];
    WCHAR szUrl[MAX_LINK_URL];
} LITEM_;

typedef struct NMLINK_ {
    NMHDR hdr;
    LITEM_ item;
} NMLINK_;

typedef struct LHITTESTINFO_ {
    POINT pt;
    LITEM_ item;
} LHITTESTINFO_;

/* a word of the text, in one link or in none, and where the last layout put it. */
typedef struct PIECE {
    int Start, Length;
    int Link; /* -1 for text outside the links */
    RECT Rect;
} PIECE;

typedef struct LINK {
    WCHAR Id[MAX_LINK_ID];
    PWSTR Url;
    UINT State;
} LINK;

typedef struct SYSLINK {
    HWND Hwnd;
    DWORD Style;
    CC_NOTIFY Notify;
    HFONT Font;
    PWSTR Text; /* what is shown, the markup taken out */
    PIECE *Pieces;
    int PieceCount;
    LINK *Links;
    int LinkCount;
    int Focus; /* the link with the focus, -1 for none */
    int Pressed;
    int Hot;
    int LayoutWidth;
    int LayoutHeight;
} SYSLINK;

static SYSLINK *get_data(HWND hwnd)
{
    return (SYSLINK *)GetWindowLongPtrW(hwnd, 0);
}

static void clear(SYSLINK *s)
{
    CcFree(s->Text);
    CcFree(s->Pieces);
    for (int i = 0; i < s->LinkCount; i++)
        CcFree(s->Links[i].Url);
    CcFree(s->Links);
    s->Text = nullptr;
    s->Pieces = nullptr;
    s->Links = nullptr;
    s->PieceCount = s->LinkCount = 0;
    s->Focus = s->Pressed = s->Hot = -1;
    s->LayoutWidth = -1;
}

/* the attribute of a tag, in double or single quotes. */
static BOOL attribute(PCWSTR tag, SIZE_T length, PCWSTR name, WCHAR *out, SIZE_T capacity)
{
    SIZE_T n = wcslen(name);
    for (SIZE_T i = 0; i + n + 2 < length; i++) {
        if (_wcsnicmp(tag + i, name, n) || tag[i + n] != L'=' || (tag[i + n + 1] != L'"' && tag[i + n + 1] != L'\''))
            continue;
        if (i && tag[i - 1] != L' ')
            continue;
        WCHAR quote = tag[i + n + 1];
        SIZE_T start = i + n + 2, end = start;
        while (end < length && tag[end] != quote)
            end++;
        SIZE_T count = end - start;
        if (count >= capacity)
            count = capacity - 1;
        memcpy(out, tag + start, count * sizeof(WCHAR));
        out[count] = 0;
        return TRUE;
    }
    return FALSE;
}

/* turns the markup into plain text, the links and the words with the link each belongs to. */
static BOOL parse(SYSLINK *s, PCWSTR markup)
{
    clear(s);
    SIZE_T length = wcslen(markup);
    s->Text = CcAlloc((length + 1) * sizeof(WCHAR));
    s->Pieces = CcAlloc((length + 1) * sizeof(PIECE));
    if (!s->Text || !s->Pieces) {
        clear(s);
        return FALSE;
    }
    SIZE_T count = 0;
    int link = -1;
    BOOL in_word = FALSE;
    for (SIZE_T i = 0; i < length;) {
        if (markup[i] == L'<') {
            SIZE_T end = i + 1;
            while (end < length && markup[end] != L'>')
                end++;
            SIZE_T tag_length = end - i - 1;
            if (end < length && tag_length >= 1 && (markup[i + 1] == L'a' || markup[i + 1] == L'A') &&
                (tag_length == 1 || markup[i + 2] == L' ')) {
                LINK *grown = CcReAlloc(s->Links, (s->LinkCount + 1) * sizeof(LINK));
                if (grown) {
                    s->Links = grown;
                    LINK *added = &s->Links[s->LinkCount];
                    memset(added, 0, sizeof(*added));
                    WCHAR url[MAX_LINK_URL];
                    if (attribute(markup + i + 1, tag_length, L"href", url, MAX_LINK_URL))
                        added->Url = CcStrDupW(url);
                    attribute(markup + i + 1, tag_length, L"id", added->Id, MAX_LINK_ID);
                    added->State = LIS_ENABLED;
                    link = s->LinkCount++;
                    in_word = FALSE;
                }
                i = end + 1;
                continue;
            }
            if (end < length && tag_length == 2 && markup[i + 1] == L'/' && (markup[i + 2] == L'a' || markup[i + 2] == L'A')) {
                link = -1;
                in_word = FALSE;
                i = end + 1;
                continue;
            }
        }
        WCHAR c = markup[i++];
        if (c == L'&' && !(s->Style & LWS_NOPREFIX) && i < length && markup[i] != L'&')
            continue;
        s->Text[count] = c;
        BOOL space = c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
        if (!space && !in_word) {
            PIECE *p = &s->Pieces[s->PieceCount++];
            memset(p, 0, sizeof(*p));
            p->Start = (int)count;
            p->Link = link;
            in_word = TRUE;
        }
        if (space)
            in_word = FALSE;
        if (!space)
            s->Pieces[s->PieceCount - 1].Length = (int)count + 1 - s->Pieces[s->PieceCount - 1].Start;
        count++;
    }
    s->Text[count] = 0;
    s->Focus = -1;
    return TRUE;
}

static void reload(SYSLINK *s)
{
    int length = GetWindowTextLengthW(s->Hwnd);
    PWSTR markup = CcAlloc(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!markup)
        return;
    GetWindowTextW(s->Hwnd, markup, length + 1);
    parse(s, markup);
    CcFree(markup);
}

/* puts the words on lines no wider than the width; returns the height. */
static int layout(SYSLINK *s, HDC dc, int width)
{
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SIZE space;
    GetTextExtentPoint32W(dc, L" ", 1, &space);
    int x = 0, y = 0;
    BOOL first = TRUE;
    for (int i = 0; i < s->PieceCount; i++) {
        PIECE *p = &s->Pieces[i];
        SIZE size;
        GetTextExtentPoint32W(dc, s->Text + p->Start, p->Length, &size);
        BOOL forced = FALSE;
        for (int k = i ? s->Pieces[i - 1].Start + s->Pieces[i - 1].Length : 0; k < p->Start && !forced; k++)
            forced = s->Text[k] == L'\n';
        if (!first && (forced || x + space.cx + size.cx > width)) {
            x = 0;
            y += tm.tmHeight;
        } else if (!first) {
            x += space.cx;
        }
        p->Rect.left = x;
        p->Rect.top = y;
        p->Rect.right = x + size.cx;
        p->Rect.bottom = y + tm.tmHeight;
        x += size.cx;
        first = FALSE;
    }
    s->LayoutWidth = width;
    s->LayoutHeight = y + tm.tmHeight;
    return s->LayoutHeight;
}

static int piece_at(SYSLINK *s, int x, int y)
{
    for (int i = 0; i < s->PieceCount; i++)
        if (s->Pieces[i].Link >= 0 && x >= s->Pieces[i].Rect.left && x < s->Pieces[i].Rect.right &&
            y >= s->Pieces[i].Rect.top && y < s->Pieces[i].Rect.bottom)
            return i;
    return -1;
}

static void fill_item(SYSLINK *s, int index, LITEM_ *item)
{
    LINK *l = &s->Links[index];
    if (item->mask & LIF_ITEMINDEX)
        item->iLink = index;
    if (item->mask & LIF_STATE)
        item->state = l->State | (s->Focus == index ? LIS_FOCUSED : 0) | (s->Hot == index ? LIS_HOTTRACK : 0);
    if (item->mask & LIF_ITEMID)
        wcscpy(item->szID, l->Id);
    if (item->mask & LIF_URL) {
        item->szUrl[0] = 0;
        if (l->Url)
            lstrcpynW(item->szUrl, l->Url, MAX_LINK_URL);
    }
}

static void notify(SYSLINK *s, UINT code, int index)
{
    NMLINK_ nm = {};
    nm.item.mask = LIF_ITEMINDEX | LIF_STATE | LIF_ITEMID | LIF_URL;
    nm.item.stateMask = (UINT)-1;
    fill_item(s, index, &nm.item);
    CcNotifyInit(&s->Notify, s->Hwnd, GetParent(s->Hwnd));
    CcNotify(&s->Notify, code, &nm.hdr);
}

static void paint(SYSLINK *s, HDC dc)
{
    RECT client;
    GetClientRect(s->Hwnd, &client);
    if ((s->Style & LWS_TRANSPARENT) == 0 || FAILED(DrawThemeParentBackground(s->Hwnd, dc, &client)))
        FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
    HGDIOBJ old = SelectObject(dc, s->Font ? s->Font : GetStockObject(DEFAULT_GUI_FONT));
    layout(s, dc, client.right - client.left);
    LOGFONTW lf;
    GetObjectW(GetCurrentObject(dc, OBJ_FONT), sizeof(lf), &lf);
    lf.lfUnderline = TRUE;
    HFONT underlined = CreateFontIndirectW(&lf);
    HGDIOBJ plain = GetCurrentObject(dc, OBJ_FONT);
    SetBkMode(dc, TRANSPARENT);
    BOOL enabled = IsWindowEnabled(s->Hwnd);
    for (int i = 0; i < s->PieceCount; i++) {
        PIECE *p = &s->Pieces[i];
        BOOL link = p->Link >= 0;
        SelectObject(dc, link ? underlined : plain);
        SetTextColor(dc, !enabled ? GetSysColor(COLOR_GRAYTEXT)
                                  : link ? GetSysColor(COLOR_HOTLIGHT) : GetSysColor(COLOR_WINDOWTEXT));
        int x = p->Rect.left;
        if (s->Style & LWS_RIGHT) {
            int used = 0;
            for (int k = 0; k < s->PieceCount; k++)
                if (s->Pieces[k].Rect.top == p->Rect.top && s->Pieces[k].Rect.right > used)
                    used = s->Pieces[k].Rect.right;
            x += client.right - client.left - used;
        }
        TextOutW(dc, x, p->Rect.top, s->Text + p->Start, p->Length);
        if (link && s->Focus == p->Link && GetFocus() == s->Hwnd) {
            RECT focus = {x, p->Rect.top, x + p->Rect.right - p->Rect.left, p->Rect.bottom};
            DrawFocusRect(dc, &focus);
        }
    }
    SelectObject(dc, old);
    DeleteObject(underlined);
}

static int next_link(SYSLINK *s, int from, int step)
{
    for (int i = from + step; i >= 0 && i < s->LinkCount; i += step)
        if (s->Links[i].State & LIS_ENABLED)
            return i;
    return -1;
}

static void set_focus_link(SYSLINK *s, int index)
{
    s->Focus = index;
    InvalidateRect(s->Hwnd, nullptr, TRUE);
}

static int measure(SYSLINK *s, int width, SIZE *size)
{
    HDC dc = GetDC(s->Hwnd);
    HGDIOBJ old = SelectObject(dc, s->Font ? s->Font : GetStockObject(DEFAULT_GUI_FONT));
    int height = layout(s, dc, width);
    int widest = 0;
    for (int i = 0; i < s->PieceCount; i++)
        if (s->Pieces[i].Rect.right > widest)
            widest = s->Pieces[i].Rect.right;
    SelectObject(dc, old);
    ReleaseDC(s->Hwnd, dc);
    s->LayoutWidth = -1;
    if (size) {
        size->cx = widest;
        size->cy = height;
    }
    return height;
}

static void ensure_layout(SYSLINK *s)
{
    RECT client;
    GetClientRect(s->Hwnd, &client);
    int width = client.right - client.left;
    if (s->LayoutWidth == width)
        return;
    HDC dc = GetDC(s->Hwnd);
    HGDIOBJ old = SelectObject(dc, s->Font ? s->Font : GetStockObject(DEFAULT_GUI_FONT));
    layout(s, dc, width);
    SelectObject(dc, old);
    ReleaseDC(s->Hwnd, dc);
}

LRESULT CALLBACK LinkProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    SYSLINK *s = get_data(hwnd);
    if (!s && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE: {
        s = CcAlloc(sizeof(*s));
        if (!s)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)s);
        s->Hwnd = hwnd;
        s->Style = (DWORD)((CREATESTRUCTW *)lparam)->style;
        s->Focus = s->Pressed = s->Hot = -1;
        CcNotifyInit(&s->Notify, hwnd, ((CREATESTRUCTW *)lparam)->hwndParent);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_CREATE:
        reload(s);
        return 0;
    case WM_NCDESTROY:
        clear(s);
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(s);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SETTEXT: {
        LRESULT result = DefWindowProcW(hwnd, msg, wparam, lparam);
        reload(s);
        InvalidateRect(hwnd, nullptr, TRUE);
        return result;
    }
    case WM_SETFONT:
        s->Font = (HFONT)wparam;
        s->LayoutWidth = -1;
        if (lparam)
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)s->Font;
    case WM_SIZE:
        s->LayoutWidth = -1;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        paint(s, dc);
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_GETDLGCODE:
        return s->LinkCount ? DLGC_WANTARROWS | DLGC_WANTCHARS : DLGC_STATIC;
    case WM_SETFOCUS: {
        int first = next_link(s, -1, 1);
        if (s->Focus < 0)
            set_focus_link(s, first);
        else
            InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_KEYDOWN:
        s->Style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
        if (wparam == VK_RETURN && s->Focus >= 0 && !(s->Style & LWS_IGNORERETURN))
            notify(s, NM_RETURN, s->Focus);
        else if (wparam == VK_SPACE && s->Focus >= 0)
            notify(s, NM_CLICK, s->Focus);
        else if (wparam == VK_TAB || wparam == VK_RIGHT || wparam == VK_DOWN || wparam == VK_LEFT || wparam == VK_UP) {
            BOOL back = wparam == VK_LEFT || wparam == VK_UP || (wparam == VK_TAB && GetKeyState(VK_SHIFT) < 0);
            int target = next_link(s, s->Focus, back ? -1 : 1);
            if (target >= 0)
                set_focus_link(s, target);
        }
        return 0;
    case WM_MOUSEMOVE: {
        ensure_layout(s);
        int x = (short)LOWORD(lparam), y = (short)HIWORD(lparam);
        int hit = piece_at(s, x, y);
        int hot = hit >= 0 ? s->Pieces[hit].Link : -1;
        if (hot != s->Hot) {
            s->Hot = hot;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (s->Hot >= 0) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_LBUTTONDOWN: {
        ensure_layout(s);
        int hit = piece_at(s, (short)LOWORD(lparam), (short)HIWORD(lparam));
        SetFocus(hwnd);
        s->Pressed = hit >= 0 ? s->Pieces[hit].Link : -1;
        if (s->Pressed >= 0) {
            SetCapture(hwnd);
            set_focus_link(s, s->Pressed);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        ensure_layout(s);
        int hit = piece_at(s, (short)LOWORD(lparam), (short)HIWORD(lparam));
        int pressed = s->Pressed;
        s->Pressed = -1;
        if (GetCapture() == hwnd)
            ReleaseCapture();
        if (pressed >= 0 && hit >= 0 && s->Pieces[hit].Link == pressed)
            notify(s, NM_CLICK, pressed);
        return 0;
    }
    case WM_MOUSELEAVE:
    case WM_CAPTURECHANGED:
        s->Hot = -1;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_NOTIFYFORMAT:
        return CcNotifyFormat(&s->Notify, (HWND)wparam, lparam);
    case LM_HITTEST: {
        LHITTESTINFO_ *info = (LHITTESTINFO_ *)lparam;
        if (!info)
            return FALSE;
        ensure_layout(s);
        int hit = piece_at(s, info->pt.x, info->pt.y);
        if (hit < 0)
            return FALSE;
        fill_item(s, s->Pieces[hit].Link, &info->item);
        return TRUE;
    }
    case LM_GETIDEALHEIGHT:
    case LM_GETIDEALSIZE: {
        RECT client;
        GetClientRect(hwnd, &client);
        int width = msg == LM_GETIDEALSIZE ? (int)wparam : client.right - client.left;
        if (msg == LM_GETIDEALHEIGHT && wparam)
            width = (int)wparam;
        SIZE size = {};
        int height = measure(s, width, &size);
        if (msg == LM_GETIDEALSIZE && lparam)
            *(SIZE *)lparam = size;
        return height;
    }
    case LM_SETITEM:
    case LM_GETITEM: {
        LITEM_ *item = (LITEM_ *)lparam;
        if (!item || !(item->mask & LIF_ITEMINDEX) || item->iLink < 0 || item->iLink >= s->LinkCount)
            return FALSE;
        if (msg == LM_GETITEM) {
            fill_item(s, item->iLink, item);
            return TRUE;
        }
        LINK *l = &s->Links[item->iLink];
        if (item->mask & LIF_STATE)
            l->State = (l->State & ~item->stateMask & ~(UINT)(LIS_FOCUSED | LIS_HOTTRACK)) | (item->state & item->stateMask);
        if ((item->mask & LIF_STATE) && (item->stateMask & LIS_FOCUSED))
            s->Focus = (item->state & LIS_FOCUSED) ? item->iLink : (s->Focus == item->iLink ? -1 : s->Focus);
        if (item->mask & LIF_ITEMID)
            lstrcpynW(l->Id, item->szID, MAX_LINK_ID);
        if (item->mask & LIF_URL) {
            CcFree(l->Url);
            l->Url = CcStrDupW(item->szUrl);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return TRUE;
    }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
