/*
 * menus in use: the menu bar of a window, the system menu and the popup menus of TrackPopupMenu, run by one modal loop
 * in user32. a popup is a #32768 window owned by the thread, drawn here; the menu bar is drawn by win32k as part of the
 * frame and told which item is selected. messages come in the order windows sends them: WM_ENTERMENULOOP, WM_INITMENU,
 * WM_MENUSELECT and WM_INITMENUPOPUP while selecting, then WM_UNINITMENUPOPUP, WM_MENUSELECT with 0xFFFF and
 * WM_EXITMENULOOP when it closes, and the command is posted after that.
 */
#include "user32_private.h"

#define MAX_LEVELS 16
#define OPEN_TIMER 0xFFFD
#define SCROLL_TIMER 0xFFFC
#define ARROW_UP (-2)
#define ARROW_DOWN (-3)
#define SYSTEM_ICON (-2)
#define NO_ITEM (-1)

enum { HIT_NONE, HIT_LEVEL, HIT_BAR, HIT_BAR_GAP, HIT_ICON };

typedef struct ITEM {
    UINT Id;
    UINT Type;
    UINT State;
    HMENU SubMenu;
    HBITMAP Bitmap;
    HBITMAP Checked;
    HBITMAP Unchecked;
    ULONG_PTR Data;
    WCHAR Text[256];
    int Top;
    int Height;
    int BitmapWidth;
    int Left;         /* the column the item is in, in the window. */
    int Cx;           /* and its width. */
    BOOL RuleBefore;  /* the item starts a column that has a line before it. */
} ITEM;

struct LOOP;

typedef struct LEVEL {
    struct LOOP *Loop;
    HMENU Menu;
    HWND Window;
    ITEM *Items;
    int Count;
    int Hot;
    int Left, Top, Width, Height; /* on the screen. */
    int Gutter;                   /* the width of the column of check marks and bitmaps. */
    BOOL Scrollable;              /* the items are taller than the screen, so they scroll between two arrows. */
    int Columns;
    int Content;                  /* the height of all the items. */
    int Scroll;                   /* how far the items have moved up. */
    int Arrow;                    /* the height of an arrow zone, 0 when the menu does not scroll. */
    BOOL System;
    ULONG Serial;
    int ParentItem;
} LEVEL;

typedef struct LOOP {
    HWND Owner;
    HWND Capture;
    HWND BarWindow;
    HMENU BarMenu;
    HMENU SystemMenu;
    ITEM *BarItems;
    int BarCount;
    ULONG BarSerial;
    int BarSel;
    BOOL Dropped;       /* a popup of the bar has been opened. */
    BOOL Track;         /* TrackPopupMenu, not a bar. */
    UINT Flags;         /* TPM_* */
    BOOL Notify;
    BOOL Done;
    BOOL Selected;
    BOOL Keyboard;
    BOOL Cues;
    BOOL ButtonDown;
    BOOL DownSeen;      /* a button went down in the loop, as opposed to the one that opened the menu. */
    DWORD StartTick;
    DWORD Tid;
    POINT StartPoint;
    BOOL IdlePending;
    BOOL ByMouse;       /* the last selection came from the mouse. */
    UINT Dpi;
    HFONT Font, BoldFont;
    LEVEL Levels[MAX_LEVELS];
    int Depth;
    UINT ResultId;
    HMENU ResultMenu;
    int ResultIndex;
    BOOL ResultSystem;
    POINT ResultPos;
    POINT LastMouse;
    UINT_PTR Timer;
    UINT_PTR ScrollTimer;
    int ScrollLevel, ScrollDir;
    HWND TimerWindow;
    int TimerLevel, TimerItem;
    HWND StartActive;
    WCHAR LastChar;
    DWORD LastCharTick;
    TPMPARAMS Params;
    BOOL HasParams;
} LOOP;

/* a character that opened the menu from its key down, whose WM_SYSCHAR must not open it again. */
static struct {
    HWND Window;
    WCHAR Char;
    DWORD Tick;
} g_char_guard;

static void guard_clear(void)
{
    g_char_guard.Window = nullptr;
}

/* the loops that run, in the order they started. one thread has at most one, unless TPM_RECURSE nests them. */
static LOOP *g_loops[16];
static volatile LONG g_loops_lock;

static void loops_lock(void)
{
    while (InterlockedCompareExchange(&g_loops_lock, 1, 0))
        YieldProcessor();
}

static void loops_unlock(void)
{
    InterlockedExchange(&g_loops_lock, 0);
}

static BOOL loop_add(LOOP *l)
{
    BOOL added = FALSE;
    l->Tid = GetCurrentThreadId();
    loops_lock();
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_loops) && !added; i++)
        if (!g_loops[i]) {
            g_loops[i] = l;
            added = TRUE;
        }
    loops_unlock();
    return added;
}

static void loop_remove(LOOP *l)
{
    loops_lock();
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_loops); i++)
        if (g_loops[i] == l)
            g_loops[i] = nullptr;
    loops_unlock();
}

/* the innermost loop of the calling thread. */
static LOOP *thread_loop(void)
{
    LOOP *found = nullptr;
    DWORD tid = GetCurrentThreadId();
    loops_lock();
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_loops); i++)
        if (g_loops[i] && g_loops[i]->Tid == tid)
            found = g_loops[i];
    loops_unlock();
    return found;
}

/* the loop that runs the bar of a window, shows a popup window, or has a popup of a menu on show. */
static LOOP *loop_matching(HWND window, HMENU menu)
{
    LOOP *found = nullptr;
    loops_lock();
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_loops) && !found; i++) {
        LOOP *l = g_loops[i];
        if (!l)
            continue;
        if (window && l->BarWindow == window)
            found = l;
        for (int d = 0; l && d < l->Depth && !found; d++)
            if ((window && l->Levels[d].Window == window) || (menu && l->Levels[d].Menu == menu))
                found = l;
    }
    loops_unlock();
    return found;
}

static int scale(int v, UINT dpi)
{
    return UserMulDiv(v, (int)dpi, 96);
}

/* ------------------------------------------------------------------ items */

static BOOL selectable(const ITEM *it)
{
    return !(it->Type & MFT_SEPARATOR);
}

static BOOL enabled(const ITEM *it)
{
    return !(it->Type & MFT_SEPARATOR) && !(it->State & (MFS_GRAYED | MF_DISABLED));
}

static BOOL query_item(HMENU menu, int index, ITEM *it)
{
    MENUITEMINFOW mii = {sizeof(mii), MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU | MIIM_STRING | MIIM_BITMAP |
                                          MIIM_DATA | MIIM_CHECKMARKS};
    mii.dwTypeData = it->Text;
    mii.cch = RTL_NUMBER_OF(it->Text);
    it->Text[0] = 0;
    if (!NtUserMimeGetMenuItemInfo(menu, (UINT)index, TRUE, &mii))
        return FALSE;
    it->Id = mii.wID;
    it->Type = mii.fType;
    it->State = mii.fState;
    it->SubMenu = mii.hSubMenu;
    it->Bitmap = mii.hbmpItem;
    it->Checked = mii.hbmpChecked;
    it->Unchecked = mii.hbmpUnchecked;
    it->Data = mii.dwItemData;
    return TRUE;
}

static ITEM *load_items(HMENU menu, int *count)
{
    int n = GetMenuItemCount(menu);
    *count = 0;
    if (n <= 0)
        return nullptr;
    ITEM *items = UserAlloc((SIZE_T)n * sizeof(ITEM));
    if (!items)
        return nullptr;
    for (int i = 0; i < n; i++)
        if (!query_item(menu, i, &items[i]))
            items[i].Type = MFT_SEPARATOR;
    *count = n;
    return items;
}

static void split_accelerator(const WCHAR *text, int *label, const WCHAR **accel)
{
    const WCHAR *tab = wcschr(text, L'\t');
    *label = tab ? (int)(tab - text) : (int)wcslen(text);
    *accel = tab ? tab + 1 : nullptr;
}

/* the character after the ampersand, in upper case. */
static WCHAR mnemonic(const WCHAR *text)
{
    for (const WCHAR *p = text; *p && *p != L'\t'; p++) {
        if (*p == L'&') {
            if (p[1] == L'&') {
                p++;
                continue;
            }
            if (p[1] && p[1] != L'\t')
                return (WCHAR)(ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)p[1]);
            return 0;
        }
    }
    return 0;
}

static int bitmap_pixels(HBITMAP bitmap, int *height, UINT dpi)
{
    ULONG_PTR v = (ULONG_PTR)bitmap;
    if (v >= 1 && v <= 11) {
        *height = GetSystemMetricsForDpi(v == 1 ? SM_CYSMICON : SM_CYMENUSIZE, dpi);
        return GetSystemMetricsForDpi(v == 1 ? SM_CXSMICON : SM_CXMENUSIZE, dpi);
    }
    BITMAP bm;
    if (bitmap && GetObjectW(bitmap, sizeof(bm), &bm)) {
        *height = bm.bmHeight;
        return bm.bmWidth;
    }
    *height = 0;
    return 0;
}

/* ------------------------------------------------------------------ the popup windows */

#define MAX_COLUMNS 8

/*
 * the items go down a column, and an item with MFT_MENUBREAK or MFT_MENUBARBREAK starts the next one (the second with a
 * line before it). the columns have the width of their widest item.
 */
static BOOL level_measure(LOOP *l, LEVEL *lv)
{
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, l->Font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int item_height = tm.tmHeight + scale(10, l->Dpi);
    if (item_height < scale(24, l->Dpi))
        item_height = scale(24, l->Dpi);
    int label_width[MAX_COLUMNS] = {}, accel_width[MAX_COLUMNS] = {}, column_height[MAX_COLUMNS] = {};
    int bitmap_width = 0, column = 0;
    int pad = scale(4, l->Dpi);
    int y = pad;
    for (int i = 0; i < lv->Count; i++) {
        ITEM *it = &lv->Items[i];
        if (i > 0 && (it->Type & (MFT_MENUBREAK | MFT_MENUBARBREAK)) && column < MAX_COLUMNS - 1) {
            column_height[column] = y + pad;
            column++;
            y = pad;
            it->RuleBefore = (it->Type & MFT_MENUBARBREAK) != 0;
        } else {
            it->RuleBefore = FALSE;
        }
        it->Left = column; /* the index for now, the position when the widths are known. */
        it->Top = y;
        it->Height = item_height;
        if (it->Type & MFT_SEPARATOR) {
            it->Height = scale(9, l->Dpi);
        } else if (it->Type & MFT_OWNERDRAW) {
            MEASUREITEMSTRUCT mi = {ODT_MENU, 0, it->Id, 0, (UINT)item_height, it->Data};
            SendMessageW(l->Owner, WM_MEASUREITEM, 0, (LPARAM)&mi);
            it->Height = (int)mi.itemHeight;
            if ((int)mi.itemWidth > label_width[column])
                label_width[column] = (int)mi.itemWidth;
        } else {
            int label;
            const WCHAR *accel;
            split_accelerator(it->Text, &label, &accel);
            RECT r = {0, 0, 0, 0};
            DrawTextW(dc, it->Text, label, &r, DT_SINGLELINE | DT_CALCRECT);
            if (r.right > label_width[column])
                label_width[column] = r.right;
            if (accel) {
                SIZE sz;
                GetTextExtentPoint32W(dc, accel, (int)wcslen(accel), &sz);
                if (sz.cx > accel_width[column])
                    accel_width[column] = sz.cx;
            }
            if (it->Bitmap) {
                int h;
                it->BitmapWidth = bitmap_pixels(it->Bitmap, &h, l->Dpi);
                if (it->BitmapWidth > bitmap_width)
                    bitmap_width = it->BitmapWidth;
                if (h + scale(4, l->Dpi) > it->Height)
                    it->Height = h + scale(4, l->Dpi);
            }
        }
        y += it->Height;
    }
    column_height[column] = y + pad;
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    lv->Gutter = scale(36, l->Dpi);
    if (bitmap_width && bitmap_width + scale(16, l->Dpi) > lv->Gutter)
        lv->Gutter = bitmap_width + scale(16, l->Dpi);
    int left[MAX_COLUMNS] = {}, width[MAX_COLUMNS] = {};
    int total = 0, tallest = 0;
    for (int c = 0; c <= column; c++) {
        width[c] = lv->Gutter + label_width[c] + (accel_width[c] ? scale(32, l->Dpi) + accel_width[c] : 0) +
                   scale(32, l->Dpi);
        if (width[c] < scale(120, l->Dpi))
            width[c] = scale(120, l->Dpi);
        left[c] = total;
        total += width[c];
        if (column_height[c] > tallest)
            tallest = column_height[c];
    }
    for (int i = 0; i < lv->Count; i++) {
        ITEM *it = &lv->Items[i];
        int c = it->Left;
        it->Left = left[c];
        it->Cx = width[c];
    }
    lv->Width = total;
    lv->Height = tallest;
    lv->Content = lv->Height;
    lv->Columns = column + 1;
    return TRUE;
}

/* where an item is in the popup window, as the popup is scrolled. */
static int item_top(const LEVEL *lv, const ITEM *it)
{
    return it->Top - lv->Scroll + lv->Arrow;
}

static int max_scroll(const LEVEL *lv)
{
    int visible = lv->Height - 2 * lv->Arrow;
    return lv->Content > visible ? lv->Content - visible : 0;
}

static void scroll_to(LEVEL *lv, int scroll)
{
    if (!lv->Scrollable)
        return;
    if (scroll > max_scroll(lv))
        scroll = max_scroll(lv);
    if (scroll < 0)
        scroll = 0;
    if (scroll != lv->Scroll) {
        lv->Scroll = scroll;
        if (lv->Window)
            InvalidateRect(lv->Window, nullptr, FALSE);
    }
}

/* the item is made visible: the popup scrolls just enough. */
static void show_item(LEVEL *lv, int index)
{
    if (!lv->Scrollable || index < 0 || index >= lv->Count)
        return;
    const ITEM *it = &lv->Items[index];
    int visible = lv->Height - 2 * lv->Arrow;
    if (it->Top < lv->Scroll)
        scroll_to(lv, it->Top - 4);
    else if (it->Top + it->Height > lv->Scroll + visible)
        scroll_to(lv, it->Top + it->Height - visible + 4);
}

static void level_free_items(LEVEL *lv)
{
    UserFree(lv->Items);
    lv->Items = nullptr;
    lv->Count = 0;
}

/* the glyph a system bitmap of an item stands for, in strokes of the text color. */
static void draw_system_bitmap(HDC dc, ULONG_PTR kind, const RECT *r, COLORREF color, UINT dpi, HICON icon)
{
    int size = scale(8, dpi), th = scale(1, dpi) > 0 ? scale(1, dpi) : 1;
    int cx = (r->left + r->right) / 2, cy = (r->top + r->bottom) / 2;
    int l = cx - size / 2, t = cy - size / 2;
    HBRUSH brush = CreateSolidBrush(color);
    switch (kind) {
    case 1:
        if (icon)
            DrawIconEx(dc, (r->left + r->right - GetSystemMetricsForDpi(SM_CXSMICON, dpi)) / 2,
                       (r->top + r->bottom - GetSystemMetricsForDpi(SM_CYSMICON, dpi)) / 2, icon,
                       GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi), 0, nullptr,
                       DI_NORMAL);
        break;
    case 3:
    case 7:
    case 11: {
        RECT line = {l, cy + size / 4, l + size, cy + size / 4 + th};
        FillRect(dc, &line, brush);
        break;
    }
    case 10: {
        RECT edges[4] = {{l, t, l + size, t + th}, {l, t + size - th, l + size, t + size},
                         {l, t, l + th, t + size}, {l + size - th, t, l + size, t + size}};
        for (int i = 0; i < 4; i++)
            FillRect(dc, &edges[i], brush);
        break;
    }
    case 2:
    case 9: {
        int back = scale(2, dpi);
        RECT parts[7] = {{l, t + back, l + size - back, t + back + th},
                         {l, t + size - th, l + size - back, t + size},
                         {l, t + back, l + th, t + size},
                         {l + size - back - th, t + back, l + size - back, t + size},
                         {l + back, t, l + size, t + th},
                         {l + size - th, t, l + size, t + size - back},
                         {l + size - back - th, t + size - back - th, l + size, t + size - back}};
        for (int i = 0; i < 7; i++)
            FillRect(dc, &parts[i], brush);
        break;
    }
    case 5:
    case 6:
    case 8:
        for (int i = 0; i < size; i++) {
            RECT a = {l + i, t + i, l + i + th, t + i + th};
            RECT b = {l + size - 1 - i, t + i, l + size - 1 - i + th, t + i + th};
            FillRect(dc, &a, brush);
            FillRect(dc, &b, brush);
        }
        break;
    }
    DeleteObject(brush);
}

static void draw_item_bitmap(LOOP *l, HDC dc, HBITMAP bitmap, const RECT *area, COLORREF fg, COLORREF bg)
{
    ULONG_PTR v = (ULONG_PTR)bitmap;
    if (v >= 1 && v <= 11) {
        HICON icon = (HICON)SendMessageW(l->Owner, WM_GETICON, ICON_SMALL, 0);
        draw_system_bitmap(dc, v, area, fg, l->Dpi, icon);
        return;
    }
    BITMAP bm;
    if (!bitmap || !GetObjectW(bitmap, sizeof(bm), &bm))
        return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, bitmap);
    int x = area->left + (area->right - area->left - bm.bmWidth) / 2;
    int y = area->top + (area->bottom - area->top - bm.bmHeight) / 2;
    if (bm.bmBitsPixel == 1) {
        COLORREF old_text = SetTextColor(dc, fg), old_back = SetBkColor(dc, bg);
        BitBlt(dc, x, y, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
        SetTextColor(dc, old_text);
        SetBkColor(dc, old_back);
    } else if (bm.bmBitsPixel == 32) {
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        GdiAlphaBlend(dc, x, y, bm.bmWidth, bm.bmHeight, mem, 0, 0, bm.bmWidth, bm.bmHeight, blend);
    } else {
        BitBlt(dc, x, y, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void paint_level(LOOP *l, LEVEL *lv, HDC dc)
{
    /* with the visual style the popup has the Menu class of aero.msstyles: a light gray, a blue selection. */
    BOOL themed = UserThemeActive();
    RECT all = {0, 0, lv->Width, lv->Height};
    COLORREF face = themed ? RGB(0xF2, 0xF2, 0xF2) : GetSysColor(COLOR_MENU);
    HBRUSH face_brush = CreateSolidBrush(face);
    FillRect(dc, &all, face_brush);
    DeleteObject(face_brush);
    HBRUSH frame = CreateSolidBrush(RGB(0xCC, 0xCC, 0xCC));
    FrameRect(dc, &all, frame);
    DeleteObject(frame);
    HGDIOBJ old_font = SelectObject(dc, l->Font);
    SetBkMode(dc, TRANSPARENT);
    int pad = scale(4, l->Dpi), right = scale(16, l->Dpi);
    for (int i = 0; i < lv->Count; i++) {
        ITEM *it = &lv->Items[i];
        int top = item_top(lv, it);
        if (lv->Scrollable && (top + it->Height <= lv->Arrow || top >= lv->Height - lv->Arrow))
            continue;
        RECT r = {it->Left + pad, top, it->Left + it->Cx - pad, top + it->Height};
        if (it->RuleBefore && (i == 0 || lv->Items[i - 1].Left != it->Left)) {
            RECT rule = {it->Left, pad, it->Left + 1, lv->Height - pad};
            HBRUSH rule_brush = CreateSolidBrush(themed ? RGB(0xD7, 0xD7, 0xD7) : GetSysColor(COLOR_3DSHADOW));
            FillRect(dc, &rule, rule_brush);
            DeleteObject(rule_brush);
        }
        if (it->Type & MFT_SEPARATOR) {
            RECT line = {r.left + scale(8, l->Dpi), (r.top + r.bottom) / 2, r.right - scale(8, l->Dpi),
                         (r.top + r.bottom) / 2 + 1};
            HBRUSH rule = CreateSolidBrush(themed ? RGB(0xD7, 0xD7, 0xD7) : GetSysColor(COLOR_3DSHADOW));
            FillRect(dc, &line, rule);
            DeleteObject(rule);
            continue;
        }
        BOOL hot = i == lv->Hot || (it->State & MFS_HILITE) != 0;
        BOOL grey = (it->State & (MFS_GRAYED | MF_DISABLED)) != 0;
        if (it->Type & MFT_OWNERDRAW) {
            DRAWITEMSTRUCT di;
            memset(&di, 0, sizeof(di));
            di.CtlType = ODT_MENU;
            di.itemID = it->Id;
            di.itemAction = ODA_DRAWENTIRE;
            di.itemState = (hot ? ODS_SELECTED : 0) | (grey ? ODS_GRAYED | ODS_DISABLED : 0) |
                           ((it->State & MFS_CHECKED) ? ODS_CHECKED : 0) | ((it->State & MFS_DEFAULT) ? ODS_DEFAULT : 0) |
                           (l->Cues ? 0 : ODS_NOACCEL) | ODS_NOFOCUSRECT;
            di.hwndItem = (HWND)lv->Menu;
            di.hDC = dc;
            di.rcItem = r;
            di.itemData = it->Data;
            HRGN clip = CreateRectRgnIndirect(&r);
            SelectClipRgn(dc, clip);
            SendMessageW(l->Owner, WM_DRAWITEM, 0, (LPARAM)&di);
            SelectClipRgn(dc, nullptr);
            DeleteObject(clip);
            continue;
        }
        COLORREF hot_color = themed ? (grey ? RGB(0xE6, 0xE6, 0xE6) : RGB(0x91, 0xC9, 0xF7)) : GetSysColor(COLOR_MENUHILIGHT);
        if (hot) {
            HBRUSH fill = CreateSolidBrush(hot_color);
            FillRect(dc, &r, fill);
            DeleteObject(fill);
        }
        COLORREF bg = hot ? hot_color : face;
        COLORREF color = grey ? (themed ? RGB(0x6D, 0x6D, 0x6D) : GetSysColor(COLOR_GRAYTEXT))
                              : hot && !themed ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                                               : GetSysColor(COLOR_MENUTEXT);
        SetTextColor(dc, color);
        RECT gutter = {r.left, r.top, r.left + lv->Gutter - pad, r.bottom};
        HBITMAP mark = (it->State & MFS_CHECKED) ? it->Checked : it->Unchecked;
        if (mark) {
            draw_item_bitmap(l, dc, mark, &gutter, color, bg);
        } else if (it->Bitmap && !(it->Type & MFT_BITMAP)) {
            draw_item_bitmap(l, dc, it->Bitmap, &gutter, color, bg);
        } else if (it->State & MFS_CHECKED) {
            /* a check mark, or a dot for radio items. */
            HPEN pen = CreatePen(PS_SOLID, scale(2, l->Dpi), color);
            HGDIOBJ old_pen = SelectObject(dc, pen);
            int cx = r.left + scale(14, l->Dpi), cy = (r.top + r.bottom) / 2, s = scale(4, l->Dpi);
            if (it->Type & MFT_RADIOCHECK) {
                HBRUSH dot = CreateSolidBrush(color);
                HGDIOBJ old_brush = SelectObject(dc, dot);
                Ellipse(dc, cx - s / 2 - 1, cy - s / 2 - 1, cx + s / 2 + 2, cy + s / 2 + 2);
                SelectObject(dc, old_brush);
                DeleteObject(dot);
            } else {
                MoveToEx(dc, cx - s, cy, nullptr);
                LineTo(dc, cx - s / 3, cy + s - s / 3);
                LineTo(dc, cx + s + 1, cy - s);
            }
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        }
        RECT text = {r.left + lv->Gutter - pad, r.top, r.right - right, r.bottom};
        if (it->Type & MFT_BITMAP) {
            /* an old style bitmap item is the bitmap alone. */
            draw_item_bitmap(l, dc, it->Bitmap, &text, color, bg);
            continue;
        }
        int label;
        const WCHAR *accel;
        split_accelerator(it->Text, &label, &accel);
        UINT hide = l->Cues ? 0 : DT_HIDEPREFIX;
        HGDIOBJ previous = nullptr;
        if ((it->State & MFS_DEFAULT) && l->BoldFont)
            previous = SelectObject(dc, l->BoldFont);
        DrawTextW(dc, it->Text, label, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | hide);
        if (accel)
            DrawTextW(dc, accel, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        if (previous)
            SelectObject(dc, previous);
        if (it->SubMenu) {
            /* the submenu arrow. */
            int ax = r.right - scale(12, l->Dpi), ay = (r.top + r.bottom) / 2, s = scale(3, l->Dpi);
            POINT tri[3] = {{ax - s, ay - s * 2}, {ax + s, ay}, {ax - s, ay + s * 2}};
            HBRUSH b = CreateSolidBrush(color);
            HPEN p = CreatePen(PS_SOLID, 1, color);
            HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
            Polygon(dc, tri, 3);
            SelectObject(dc, ob);
            SelectObject(dc, op);
            DeleteObject(b);
            DeleteObject(p);
        }
    }
    if (lv->Scrollable) {
        /* the arrows cover the ends of the list. */
        RECT top_zone = {1, 1, lv->Width - 1, lv->Arrow}, bottom_zone = {1, lv->Height - lv->Arrow, lv->Width - 1, lv->Height - 1};
        HBRUSH face_brush2 = CreateSolidBrush(face);
        FillRect(dc, &top_zone, face_brush2);
        FillRect(dc, &bottom_zone, face_brush2);
        DeleteObject(face_brush2);
        COLORREF ink = GetSysColor(COLOR_MENUTEXT);
        HBRUSH ink_brush = CreateSolidBrush(ink);
        HGDIOBJ ob = SelectObject(dc, ink_brush);
        HPEN ink_pen = CreatePen(PS_SOLID, 1, ink);
        HGDIOBJ op = SelectObject(dc, ink_pen);
        int cx = lv->Width / 2, s = scale(4, l->Dpi);
        if (lv->Scroll > 0) {
            int cy = lv->Arrow / 2;
            POINT tri[3] = {{cx - s, cy + s / 2}, {cx + s, cy + s / 2}, {cx, cy - s / 2}};
            Polygon(dc, tri, 3);
        }
        if (lv->Scroll < max_scroll(lv)) {
            int cy = lv->Height - lv->Arrow / 2;
            POINT tri[3] = {{cx - s, cy - s / 2}, {cx + s, cy - s / 2}, {cx, cy + s / 2}};
            Polygon(dc, tri, 3);
        }
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(ink_brush);
        DeleteObject(ink_pen);
    }
    SelectObject(dc, old_font);
}

LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    LEVEL *lv = (LEVEL *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT p;
        HDC dc = BeginPaint(hwnd, &p);
        if (lv && lv->Loop && dc)
            paint_level(lv->Loop, lv, dc);
        EndPaint(hwnd, &p);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_CANCELMODE:
        if (lv && lv->Loop)
            lv->Loop->Done = TRUE;
        return 0;
    case WM_GETICON:
    case WM_NCHITTEST:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* ------------------------------------------------------------------ notifications */

/* the flags of WM_MENUSELECT for an item. */
static UINT select_flags(const ITEM *it, BOOL system, BOOL mouse)
{
    UINT f = (it->Type & 0x0B64) | (it->State & 0x8B) | MF_HILITE;
    if (it->SubMenu)
        f |= MF_POPUP;
    if (system)
        f |= MF_SYSMENU;
    if (mouse)
        f |= MF_MOUSESELECT;
    return f;
}

static void send_select(LOOP *l, HMENU menu, int index, const ITEM *it, BOOL system)
{
    if (!l->Notify)
        return;
    if (index < 0 || !it) {
        SendMessageW(l->Owner, WM_MENUSELECT, 0, 0);
        return;
    }
    UINT id = it->SubMenu ? (UINT)index : it->Id;
    SendMessageW(l->Owner, WM_MENUSELECT, MAKEWPARAM(id, select_flags(it, system, l->ByMouse)), (LPARAM)menu);
}

static BOOL in_range(int i, int count)
{
    return i >= 0 && i < count;
}

static void set_bar_state(LOOP *l)
{
    if (!l->BarWindow)
        return;
    MIME_MENUBAR req;
    memset(&req, 0, sizeof(req));
    req.Op = MIME_MENUBAR_STATE;
    req.Item = l->BarSel == SYSTEM_ICON ? -2 : in_range(l->BarSel, l->BarCount) ? l->BarSel : -1;
    req.Pushed = (l->Dropped && l->Depth > 0 && req.Item >= 0) ? req.Item : -1;
    NtUserCallHwndParam(l->BarWindow, (ULONG_PTR)&req, MIME_HWNDPARAM_MENUBAR);
}

/* ------------------------------------------------------------------ levels */

/* a popup that was measured again is cut to its monitor, where it is. */
static void fit_height(LOOP *l, LEVEL *lv)
{
    POINT pt = {lv->Left, lv->Top};
    MONITORINFO mi = {sizeof(mi)};
    lv->Scrollable = FALSE;
    lv->Arrow = 0;
    if (GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi) &&
        lv->Content > mi.rcMonitor.bottom - mi.rcMonitor.top && lv->Columns == 1) {
        lv->Scrollable = TRUE;
        lv->Arrow = scale(16, l->Dpi);
        lv->Height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }
}

static void place_popup(LOOP *l, LEVEL *lv, int *x, int *y, const RECT *exclude, UINT flags, BOOL from_bar)
{
    POINT pt = {*x, *y};
    MONITORINFO mi = {sizeof(mi)};
    BOOL have_area = GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    RECT area = (flags & TPM_WORKAREA) ? mi.rcWork : mi.rcMonitor;
    /* a menu taller than the screen scrolls between two arrows. */
    if (have_area && lv->Height > area.bottom - area.top && lv->Columns == 1) {
        lv->Scrollable = TRUE;
        lv->Arrow = scale(16, l->Dpi);
        lv->Height = area.bottom - area.top;
    }
    UINT h = flags & (TPM_CENTERALIGN | TPM_RIGHTALIGN);
    if (h == TPM_RIGHTALIGN)
        *x -= lv->Width;
    else if (h == TPM_CENTERALIGN)
        *x -= lv->Width / 2;
    if (flags & TPM_BOTTOMALIGN)
        *y -= lv->Height;
    else if (flags & TPM_VCENTERALIGN)
        *y -= lv->Height / 2;
    if (!have_area)
        return;
    /* a popup that does not fit opens on the other side of the point or of the excluded rectangle. */
    if (*x + lv->Width > area.right) {
        /* a popup under a bar item slides left to the edge, one at the side of another opens at its other side. */
        int flipped = (exclude ? exclude->left : *x) - lv->Width;
        *x = flipped >= area.left && !from_bar ? flipped : area.right - lv->Width;
    }
    if (*y + lv->Height > area.bottom) {
        int flipped = (exclude ? exclude->top : *y) - lv->Height;
        *y = flipped >= area.top ? flipped : area.bottom - lv->Height;
    }
    if (*x < area.left)
        *x = area.left;
    if (*y < area.top)
        *y = area.top;
}

static BOOL level_create(LOOP *l, HMENU menu, BOOL system, int parent_item, int x, int y, const RECT *exclude, UINT flags,
                         BOOL from_bar)
{
    if (l->Depth >= MAX_LEVELS)
        return FALSE;
    LEVEL *lv = &l->Levels[l->Depth];
    memset(lv, 0, sizeof(*lv));
    lv->Loop = l;
    lv->Menu = menu;
    lv->System = system;
    lv->Hot = NO_ITEM;
    lv->ParentItem = parent_item;
    lv->Items = load_items(menu, &lv->Count);
    if (!lv->Items)
        return FALSE;
    lv->Serial = (ULONG)NtUserMimeCallMenu(menu, 0, MIME_MENU_SERIAL);
    level_measure(l, lv);
    place_popup(l, lv, &x, &y, exclude, flags, from_bar);
    lv->Left = x;
    lv->Top = y;
    lv->Window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"#32768", nullptr, WS_POPUP, x, y,
                                 lv->Width, lv->Height, l->Owner, nullptr, User32Instance, nullptr);
    if (!lv->Window) {
        level_free_items(lv);
        return FALSE;
    }
    SetWindowLongPtrW(lv->Window, GWLP_USERDATA, (LONG_PTR)lv);
    l->Depth++;
    ShowWindow(lv->Window, SW_SHOWNA);
    UpdateWindow(lv->Window);
    return TRUE;
}

static void level_close(LOOP *l)
{
    if (l->Depth <= 0)
        return;
    LEVEL *lv = &l->Levels[--l->Depth];
    if (lv->Window) {
        SetWindowLongPtrW(lv->Window, GWLP_USERDATA, 0);
        DestroyWindow(lv->Window);
        lv->Window = nullptr;
    }
    level_free_items(lv);
    if (l->Notify)
        SendMessageW(l->Owner, WM_UNINITMENUPOPUP, (WPARAM)lv->Menu, MAKELPARAM(0, lv->System ? MF_SYSMENU : 0));
}

static void close_to_depth(LOOP *l, int depth)
{
    while (l->Depth > depth)
        level_close(l);
}

static void kill_open_timer(LOOP *l)
{
    if (l->Timer) {
        KillTimer(l->TimerWindow, l->Timer);
        l->Timer = 0;
    }
}

static void stop_scroll(LOOP *l)
{
    if (l->ScrollTimer) {
        KillTimer(l->TimerWindow, l->ScrollTimer);
        l->ScrollTimer = 0;
    }
}

static void start_scroll(LOOP *l, int level, int direction)
{
    if (l->ScrollTimer && l->ScrollLevel == level && l->ScrollDir == direction)
        return;
    stop_scroll(l);
    l->ScrollLevel = level;
    l->ScrollDir = direction;
    l->ScrollTimer = SCROLL_TIMER;
    SetTimer(l->TimerWindow, SCROLL_TIMER, 50, nullptr);
}

static void redraw_level(LEVEL *lv)
{
    if (lv->Window)
        InvalidateRect(lv->Window, nullptr, FALSE);
}

/* an edit of a menu that is on show takes effect at once. */
static void refresh(LOOP *l)
{
    for (int d = 0; d < l->Depth; d++) {
        LEVEL *lv = &l->Levels[d];
        if (!IsMenu(lv->Menu)) {
            close_to_depth(l, d);
            if (d == 0 && l->Track)
                l->Done = TRUE;
            return;
        }
        ULONG serial = (ULONG)NtUserMimeCallMenu(lv->Menu, 0, MIME_MENU_SERIAL);
        if (serial == lv->Serial)
            continue;
        ITEM *items;
        int count;
        items = load_items(lv->Menu, &count);
        int hot = lv->Hot;
        level_free_items(lv);
        lv->Items = items;
        lv->Count = count;
        lv->Serial = serial;
        if (!count) {
            close_to_depth(l, d);
            return;
        }
        if (hot >= count)
            hot = NO_ITEM;
        lv->Hot = hot;
        level_measure(l, lv);
        fit_height(l, lv);
        scroll_to(lv, lv->Scroll);
        SetWindowPos(lv->Window, nullptr, 0, 0, lv->Width, lv->Height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        redraw_level(lv);
    }
    if (l->BarMenu && IsMenu(l->BarMenu)) {
        ULONG serial = (ULONG)NtUserMimeCallMenu(l->BarMenu, 0, MIME_MENU_SERIAL);
        if (serial != l->BarSerial) {
            UserFree(l->BarItems);
            l->BarItems = load_items(l->BarMenu, &l->BarCount);
            l->BarSerial = serial;
            if (l->BarSel >= l->BarCount)
                l->BarSel = NO_ITEM;
            set_bar_state(l);
        }
    }
}

/* ------------------------------------------------------------------ selection */

static BOOL bar_item_rect(LOOP *l, int index, RECT *r)
{
    MIME_MENUBAR req;
    memset(&req, 0, sizeof(req));
    req.Op = MIME_MENUBAR_RECT;
    req.Item = index + 1;
    if (!l->BarWindow || !NtUserCallHwndParam(l->BarWindow, (ULONG_PTR)&req, MIME_HWNDPARAM_MENUBAR))
        return FALSE;
    *r = req.Rect;
    return TRUE;
}

/* the icon of the system menu, where the popup of the system menu hangs. */
static void icon_anchor(LOOP *l, RECT *r)
{
    RECT window;
    GetWindowRect(l->BarWindow, &window);
    LONG style = GetWindowLongW(l->BarWindow, GWL_STYLE);
    int frame = (style & WS_MAXIMIZE) ? 0 : GetSystemMetricsForDpi(SM_CXSIZEFRAME, l->Dpi) +
                                                 GetSystemMetricsForDpi(SM_CXPADDEDBORDER, l->Dpi);
    int caption = GetSystemMetricsForDpi(SM_CYCAPTION, l->Dpi);
    int size = GetSystemMetricsForDpi(SM_CXSMICON, l->Dpi);
    r->left = window.left + frame;
    r->top = window.top + frame;
    r->right = r->left + size + scale(16, l->Dpi);
    r->bottom = r->top + caption;
}

static int first_valid(const ITEM *items, int count, int from, int step)
{
    for (int n = 0, i = from; n < count; n++, i += step) {
        if (i < 0 || i >= count)
            return NO_ITEM;
        if (selectable(&items[i]))
            return i;
    }
    return NO_ITEM;
}

static void open_submenu(LOOP *l, int level, int item, BOOL select_first);

/* selects an item of an open popup. the popups below it close. */
static void select_level_item(LOOP *l, int level, int item, BOOL mouse)
{
    LEVEL *lv = &l->Levels[level];
    l->ByMouse = mouse;
    if (item == lv->Hot && l->Depth == level + 1)
        return;
    if (l->Timer && (l->TimerLevel != level || l->TimerItem != item))
        kill_open_timer(l);
    close_to_depth(l, level + 1);
    lv->Hot = item;
    if (!mouse)
        show_item(lv, item);
    redraw_level(lv);
    send_select(l, lv->Menu, item, in_range(item, lv->Count) ? &lv->Items[item] : nullptr, lv->System);
    if (mouse && in_range(item, lv->Count) && lv->Items[item].SubMenu && enabled(&lv->Items[item])) {
        l->Timer = OPEN_TIMER;
        l->TimerLevel = level;
        l->TimerItem = item;
        DWORD delay = 400;
        SystemParametersInfoW(SPI_GETMENUSHOWDELAY, 0, &delay, 0);
        SetTimer(l->TimerWindow, OPEN_TIMER, delay ? delay : 1, nullptr);
    }
}

static void select_bar(LOOP *l, int item, BOOL open, BOOL mouse)
{
    l->ByMouse = mouse;
    kill_open_timer(l);
    if (item != l->BarSel) {
        close_to_depth(l, 0);
        l->BarSel = item;
        if (item == SYSTEM_ICON) {
            if (l->Notify)
                SendMessageW(l->Owner, WM_MENUSELECT, MAKEWPARAM(0, MF_POPUP | MF_SYSMENU | MF_HILITE |
                                                                     (mouse ? MF_MOUSESELECT : 0)),
                             (LPARAM)l->SystemMenu);
        } else {
            send_select(l, l->BarMenu, item, in_range(item, l->BarCount) ? &l->BarItems[item] : nullptr, FALSE);
        }
        set_bar_state(l);
    }
    if (open && l->Depth == 0) {
        if (item == SYSTEM_ICON && l->SystemMenu) {
            RECT anchor;
            icon_anchor(l, &anchor);
            if (l->Notify)
                SendMessageW(l->Owner, WM_INITMENUPOPUP, (WPARAM)l->SystemMenu, MAKELPARAM(0, 1));
            if (IsMenu(l->SystemMenu) && level_create(l, l->SystemMenu, TRUE, item, anchor.left, anchor.bottom, &anchor, 0, TRUE)) {
                l->Dropped = TRUE;
                set_bar_state(l);
            }
        } else if (in_range(item, l->BarCount) && l->BarItems[item].SubMenu && enabled(&l->BarItems[item])) {
            RECT r;
            if (!bar_item_rect(l, item, &r))
                return;
            HMENU sub = l->BarItems[item].SubMenu;
            if (l->Notify)
                SendMessageW(l->Owner, WM_INITMENUPOPUP, (WPARAM)sub, MAKELPARAM(item, 0));
            if (IsMenu(sub) && level_create(l, sub, FALSE, item, r.left, r.bottom, &r, 0, TRUE)) {
                l->Dropped = TRUE;
                set_bar_state(l);
            }
        }
    }
}

static void open_submenu(LOOP *l, int level, int item, BOOL select_first)
{
    LEVEL *parent = &l->Levels[level];
    if (!in_range(item, parent->Count))
        return;
    ITEM *it = &parent->Items[item];
    if (!it->SubMenu || !enabled(it))
        return;
    kill_open_timer(l);
    if (l->Depth > level + 1 && l->Levels[level + 1].Menu == it->SubMenu)
        return;
    close_to_depth(l, level + 1);
    HMENU sub = it->SubMenu;
    if (l->Notify)
        SendMessageW(l->Owner, WM_INITMENUPOPUP, (WPARAM)sub, MAKELPARAM(item, parent->System ? 1 : 0));
    if (!IsMenu(sub))
        return;
    /* the first item of the submenu is level with its item. */
    int row = item_top(parent, it);
    int x = parent->Left + it->Left + it->Cx - scale(3, l->Dpi), y = parent->Top + row - scale(4, l->Dpi);
    RECT around = {parent->Left + it->Left, parent->Top + row, parent->Left + it->Left + it->Cx,
                   parent->Top + row + it->Height};
    if (!level_create(l, sub, parent->System, item, x, y, &around, 0, FALSE))
        return;
    if (select_first) {
        LEVEL *lv = &l->Levels[l->Depth - 1];
        int first = first_valid(lv->Items, lv->Count, 0, 1);
        if (first != NO_ITEM)
            select_level_item(l, l->Depth - 1, first, FALSE);
    }
}

static void execute(LOOP *l, HMENU menu, int index, const ITEM *it, BOOL system)
{
    l->Selected = TRUE;
    l->ResultMenu = menu;
    l->ResultIndex = index;
    l->ResultId = it->Id;
    l->ResultSystem = system;
    l->Done = TRUE;
}

/* ------------------------------------------------------------------ hit testing */

static BOOL screen_point_in_level(const LEVEL *lv, POINT pt, int *item)
{
    if (!lv->Window || pt.x < lv->Left || pt.y < lv->Top || pt.x >= lv->Left + lv->Width || pt.y >= lv->Top + lv->Height)
        return FALSE;
    *item = NO_ITEM;
    int y = pt.y - lv->Top;
    if (lv->Scrollable && y < lv->Arrow) {
        *item = ARROW_UP;
        return TRUE;
    }
    if (lv->Scrollable && y >= lv->Height - lv->Arrow) {
        *item = ARROW_DOWN;
        return TRUE;
    }
    int x = pt.x - lv->Left;
    for (int i = 0; i < lv->Count; i++) {
        int top = item_top(lv, &lv->Items[i]);
        if (y >= top && y < top + lv->Items[i].Height && x >= lv->Items[i].Left && x < lv->Items[i].Left + lv->Items[i].Cx) {
            *item = i;
            break;
        }
    }
    return TRUE;
}

static int hit_test(LOOP *l, POINT pt, int *level, int *item)
{
    *level = -1;
    *item = NO_ITEM;
    for (int d = l->Depth - 1; d >= 0; d--)
        if (screen_point_in_level(&l->Levels[d], pt, item)) {
            *level = d;
            return HIT_LEVEL;
        }
    if (l->BarWindow) {
        MIME_MENUBAR req;
        memset(&req, 0, sizeof(req));
        req.Op = MIME_MENUBAR_HIT;
        req.Point = pt;
        if (NtUserCallHwndParam(l->BarWindow, (ULONG_PTR)&req, MIME_HWNDPARAM_MENUBAR)) {
            *item = req.Item;
            return req.Item >= 0 ? HIT_BAR : HIT_BAR_GAP;
        }
        if (l->SystemMenu && SendMessageW(l->BarWindow, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y)) == HTSYSMENU)
            return HIT_ICON;
    }
    return HIT_NONE;
}

/* ------------------------------------------------------------------ keys */

/* moves among the items of an open popup, around the ends. */
static void move_hot(LOOP *l, int level, int step)
{
    LEVEL *lv = &l->Levels[level];
    int i = lv->Hot;
    for (int n = 0; n < lv->Count; n++) {
        i = i < 0 ? (step > 0 ? 0 : lv->Count - 1) : (i + step + lv->Count) % lv->Count;
        if (selectable(&lv->Items[i])) {
            select_level_item(l, level, i, FALSE);
            return;
        }
    }
}

/* the order of the bar for the left and right keys: the system menu icon first, when the window has one. */
static int bar_step(LOOP *l, int from, int step)
{
    int lead = l->SystemMenu ? 1 : 0;
    int span = l->BarCount + lead;
    if (span <= 0)
        return NO_ITEM;
    int pos = from == NO_ITEM ? (step > 0 ? -1 : 0) : from == SYSTEM_ICON ? 0 : from + lead;
    pos = (pos + step + span) % span;
    if (lead && pos == 0)
        return SYSTEM_ICON;
    return pos - lead;
}

static void bar_to(LOOP *l, int item, BOOL open)
{
    if (item == NO_ITEM)
        return;
    select_bar(l, item, open, FALSE);
    if (open && l->Depth > 0) {
        LEVEL *lv = &l->Levels[0];
        int first = first_valid(lv->Items, lv->Count, 0, 1);
        if (first != NO_ITEM)
            select_level_item(l, 0, first, FALSE);
    }
}

static void activate_item(LOOP *l, int level)
{
    LEVEL *lv = &l->Levels[level];
    if (!in_range(lv->Hot, lv->Count))
        return;
    ITEM *it = &lv->Items[lv->Hot];
    if (it->SubMenu) {
        open_submenu(l, level, lv->Hot, TRUE);
        return;
    }
    if (enabled(it))
        execute(l, lv->Menu, lv->Hot, it, lv->System);
}

static void activate_bar(LOOP *l)
{
    if (l->BarSel == SYSTEM_ICON) {
        bar_to(l, SYSTEM_ICON, TRUE);
        return;
    }
    if (!in_range(l->BarSel, l->BarCount))
        return;
    ITEM *it = &l->BarItems[l->BarSel];
    if (it->SubMenu) {
        if (enabled(it)) {
            bar_to(l, l->BarSel, TRUE);
        }
        return;
    }
    if (enabled(it))
        execute(l, l->BarMenu, l->BarSel, it, FALSE);
}

static void end_loop(LOOP *l)
{
    l->Done = TRUE;
}

/* a mnemonic character: it picks an item of the innermost menu, or asks the window with WM_MENUCHAR. */
static void handle_char(LOOP *l, WCHAR ch)
{
    ITEM *items;
    int count, start, level = l->Depth - 1;
    HMENU menu;
    BOOL system = FALSE;
    if (l->Depth > 0) {
        LEVEL *lv = &l->Levels[level];
        items = lv->Items;
        count = lv->Count;
        start = lv->Hot;
        menu = lv->Menu;
        system = lv->System;
    } else if (l->BarWindow) {
        items = l->BarItems;
        count = l->BarCount;
        start = l->BarSel;
        menu = l->BarMenu;
    } else {
        return;
    }
    WCHAR upper = (WCHAR)(ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)ch);
    int first = NO_ITEM, matches = 0, after = NO_ITEM;
    for (int i = 0; i < count; i++) {
        if ((items[i].Type & MFT_SEPARATOR) || mnemonic(items[i].Text) != upper)
            continue;
        if (first == NO_ITEM)
            first = i;
        if (after == NO_ITEM && i > start)
            after = i;
        matches++;
    }
    if (matches) {
        int pick = matches == 1 ? first : (after != NO_ITEM ? after : first);
        if (l->Depth > 0)
            select_level_item(l, level, pick, FALSE);
        else
            select_bar(l, pick, FALSE, FALSE);
        /* the only match is carried out. */
        if (matches == 1) {
            if (l->Depth > 0)
                activate_item(l, level);
            else
                activate_bar(l);
        }
        return;
    }
    LRESULT r = l->Notify ? SendMessageW(l->Owner, WM_MENUCHAR, MAKEWPARAM(upper, (l->Depth > 0 ? MF_POPUP : 0) |
                                                                                    (system ? MF_SYSMENU : 0)),
                                         (LPARAM)menu)
                          : 0;
    switch (HIWORD(r)) {
    case MNC_CLOSE:
        end_loop(l);
        break;
    case MNC_EXECUTE:
        if (in_range((int)(short)LOWORD(r), count)) {
            int pick = (short)LOWORD(r);
            if (l->Depth > 0) {
                select_level_item(l, level, pick, FALSE);
                activate_item(l, level);
            } else {
                select_bar(l, pick, FALSE, FALSE);
                activate_bar(l);
            }
        }
        break;
    case MNC_SELECT:
        if (in_range((int)(short)LOWORD(r), count)) {
            if (l->Depth > 0)
                select_level_item(l, level, (short)LOWORD(r), FALSE);
            else
                select_bar(l, (short)LOWORD(r), FALSE, FALSE);
        }
        break;
    default:
        MessageBeep(0);
        if (l->Depth == 0 && !l->Track && l->BarSel == NO_ITEM)
            end_loop(l);
        break;
    }
}

static void show_cues(LOOP *l)
{
    if (l->Cues)
        return;
    l->Cues = TRUE;
    SendMessageW(l->Owner, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEACCEL | UISF_HIDEFOCUS), 0);
    for (int d = 0; d < l->Depth; d++)
        redraw_level(&l->Levels[d]);
}

static void key_down(LOOP *l, UINT vk)
{
    l->Keyboard = TRUE;
    show_cues(l);
    switch (vk) {
    case VK_ESCAPE:
        if (l->Depth > 0) {
            close_to_depth(l, l->Depth - 1);
            if (l->Depth == 0 && !l->BarWindow)
                end_loop(l);
            else if (l->Depth == 0) {
                l->Dropped = FALSE;
                set_bar_state(l);
            }
        } else {
            end_loop(l);
        }
        break;
    case VK_UP:
    case VK_DOWN: {
        int step = vk == VK_DOWN ? 1 : -1;
        if (l->Depth > 0) {
            move_hot(l, l->Depth - 1, step);
        } else if (l->BarWindow && (l->BarSel == SYSTEM_ICON || in_range(l->BarSel, l->BarCount))) {
            bar_to(l, l->BarSel, TRUE);
            if (step < 0 && l->Depth > 0) {
                LEVEL *lv = &l->Levels[0];
                int last = first_valid(lv->Items, lv->Count, lv->Count - 1, -1);
                if (last != NO_ITEM)
                    select_level_item(l, 0, last, FALSE);
            }
        }
        break;
    }
    case VK_HOME:
    case VK_END:
        if (l->Depth > 0) {
            LEVEL *lv = &l->Levels[l->Depth - 1];
            int pick = vk == VK_HOME ? first_valid(lv->Items, lv->Count, 0, 1)
                                     : first_valid(lv->Items, lv->Count, lv->Count - 1, -1);
            if (pick != NO_ITEM)
                select_level_item(l, l->Depth - 1, pick, FALSE);
        }
        break;
    case VK_LEFT:
        if (l->Depth > 1) {
            close_to_depth(l, l->Depth - 1);
        } else if (l->BarWindow) {
            BOOL open = l->Depth > 0;
            bar_to(l, bar_step(l, l->BarSel, -1), open);
        }
        break;
    case VK_RIGHT:
        if (l->Depth > 0 && in_range(l->Levels[l->Depth - 1].Hot, l->Levels[l->Depth - 1].Count) &&
            l->Levels[l->Depth - 1].Items[l->Levels[l->Depth - 1].Hot].SubMenu) {
            open_submenu(l, l->Depth - 1, l->Levels[l->Depth - 1].Hot, TRUE);
        } else if (l->BarWindow) {
            BOOL open = l->Depth > 0;
            bar_to(l, bar_step(l, l->BarSel, 1), open);
        }
        break;
    case VK_RETURN:
        if (l->Depth > 0)
            activate_item(l, l->Depth - 1);
        else if (l->BarWindow)
            activate_bar(l);
        break;
    case VK_MENU:
    case VK_F10:
        /* the key that ends the menu must not open it again when it comes up. */
        NtUserCallTwoParam(0, 0, MIME_TWOPARAM_ALTALONE);
        end_loop(l);
        break;
    case VK_TAB:
    case VK_SHIFT:
    case VK_CONTROL:
        break;
    default: {
        UINT ch = MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR);
        if (ch && vk != VK_PACKET && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            l->LastChar = (WCHAR)ch;
            l->LastCharTick = GetTickCount();
            handle_char(l, (WCHAR)ch);
        }
        break;
    }
    }
}

/* ------------------------------------------------------------------ the loop */

static int absi(int v)
{
    return v < 0 ? -v : v;
}

static int pt_distance_x(const LOOP *l, POINT pt)
{
    return pt.x - l->StartPoint.x;
}

static int pt_distance_y(const LOOP *l, POINT pt)
{
    return pt.y - l->StartPoint.y;
}

static void mouse_message(LOOP *l, const MSG *msg)
{
    POINT pt = msg->pt;
    int level, item;
    int hit = hit_test(l, pt, &level, &item);
    l->ByMouse = TRUE;
    switch (msg->message) {
    case WM_MOUSEMOVE:
    case WM_NCMOUSEMOVE: {
        if (pt.x == l->LastMouse.x && pt.y == l->LastMouse.y)
            return;
        l->LastMouse = pt;
        if (hit == HIT_LEVEL) {
            LEVEL *lv = &l->Levels[level];
            int pick = in_range(item, lv->Count) && selectable(&lv->Items[item]) ? item : NO_ITEM;
            select_level_item(l, level, pick, TRUE);
            if (item == ARROW_UP || item == ARROW_DOWN)
                start_scroll(l, level, item == ARROW_UP ? -1 : 1);
            else
                stop_scroll(l);
        } else if (hit == HIT_BAR && item != l->BarSel) {
            select_bar(l, item, l->Dropped, TRUE);
        } else if (hit == HIT_ICON && l->BarSel != SYSTEM_ICON) {
            select_bar(l, SYSTEM_ICON, l->Dropped, TRUE);
        } else if (hit == HIT_NONE && l->Depth > 0) {
            LEVEL *lv = &l->Levels[l->Depth - 1];
            if (lv->Hot != NO_ITEM && !(l->Depth > 0 && lv->Hot >= 0 && lv->Items[lv->Hot].SubMenu))
                select_level_item(l, l->Depth - 1, NO_ITEM, TRUE);
        }
        break;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
        l->ButtonDown = TRUE;
        l->DownSeen = TRUE;
        if (hit == HIT_NONE) {
            end_loop(l);
        } else if (hit == HIT_LEVEL) {
            LEVEL *lv = &l->Levels[level];
            if (in_range(item, lv->Count) && selectable(&lv->Items[item])) {
                select_level_item(l, level, item, TRUE);
                if (lv->Items[item].SubMenu)
                    open_submenu(l, level, item, FALSE);
            }
        } else if (hit == HIT_BAR) {
            if (item == l->BarSel && l->Depth > 0)
                end_loop(l);
            else {
                select_bar(l, item, TRUE, TRUE);
                l->Dropped = l->Depth > 0;
                set_bar_state(l);
            }
        } else if (hit == HIT_ICON) {
            if (l->BarSel == SYSTEM_ICON && l->Depth > 0)
                end_loop(l);
            else
                select_bar(l, SYSTEM_ICON, TRUE, TRUE);
        }
        break;
    case WM_LBUTTONUP:
    case WM_NCLBUTTONUP:
    case WM_RBUTTONUP:
    case WM_NCRBUTTONUP: {
        BOOL right = msg->message == WM_RBUTTONUP || msg->message == WM_NCRBUTTONUP;
        if (right && hit == HIT_LEVEL && l->Notify) {
            LEVEL *lv = &l->Levels[level];
            if (in_range(item, lv->Count))
                SendMessageW(l->Owner, WM_MENURBUTTONUP, (WPARAM)item, (LPARAM)lv->Menu);
        } else if (right && hit == HIT_BAR && l->Notify && in_range(item, l->BarCount)) {
            SendMessageW(l->Owner, WM_MENURBUTTONUP, (WPARAM)item, (LPARAM)l->BarMenu);
        }
        BOOL was_down = l->ButtonDown;
        l->ButtonDown = FALSE;
        if (!was_down)
            break;
        if (right && !(l->Flags & TPM_RIGHTBUTTON))
            break;
        if (hit == HIT_NONE) {
            /* the button that opened the menu comes up next to where it went down, which leaves the menu there. */
            int drag = GetSystemMetrics(SM_CXDRAG);
            BOOL near_start = absi(pt_distance_x(l, msg->pt)) <= drag && absi(pt_distance_y(l, msg->pt)) <= drag;
            if (l->DownSeen || !(near_start || GetTickCount() - l->StartTick < GetDoubleClickTime()))
                end_loop(l);
        } else if (hit == HIT_LEVEL) {
            LEVEL *lv = &l->Levels[level];
            if (in_range(item, lv->Count) && selectable(&lv->Items[item]) && !lv->Items[item].SubMenu) {
                if (enabled(&lv->Items[item]))
                    execute(l, lv->Menu, item, &lv->Items[item], lv->System);
            }
        } else if (hit == HIT_BAR && in_range(item, l->BarCount)) {
            ITEM *it = &l->BarItems[item];
            if (!it->SubMenu && enabled(it)) {
                if (item != l->BarSel)
                    select_bar(l, item, FALSE, TRUE);
                execute(l, l->BarMenu, item, it, FALSE);
            }
        }
        break;
    }
    }
}

static BOOL handle_message(LOOP *l, MSG *msg)
{
    switch (msg->message) {
    case WM_QUIT:
        return FALSE;
    case WM_TIMER:
        if (msg->wParam == SCROLL_TIMER && msg->hwnd == l->TimerWindow) {
            if (l->ScrollLevel < l->Depth)
                scroll_to(&l->Levels[l->ScrollLevel], l->Levels[l->ScrollLevel].Scroll + l->ScrollDir * scale(12, l->Dpi));
            else
                stop_scroll(l);
            return TRUE;
        }
        if (msg->wParam == OPEN_TIMER && msg->hwnd == l->TimerWindow) {
            int level = l->TimerLevel, item = l->TimerItem;
            kill_open_timer(l);
            if (level < l->Depth && l->Levels[level].Hot == item)
                open_submenu(l, level, item, FALSE);
            return TRUE;
        }
        return FALSE;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_NCMOUSEMOVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
        mouse_message(l, msg);
        return TRUE;
    case WM_MOUSEWHEEL: {
        /* the wheel scrolls the popup under the pointer, or the innermost one. */
        POINT pt = msg->pt;
        int level, item;
        if (hit_test(l, pt, &level, &item) != HIT_LEVEL)
            level = l->Depth - 1;
        if (level >= 0 && level < l->Depth && l->Levels[level].Scrollable) {
            LEVEL *lv = &l->Levels[level];
            int notches = (short)HIWORD(msg->wParam) / 120;
            scroll_to(lv, lv->Scroll - notches * scale(24, l->Dpi) * 3);
        }
        return TRUE;
    }
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEHWHEEL:
        return TRUE;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if ((HIWORD(msg->lParam) & KF_REPEAT) && (msg->wParam == VK_MENU || msg->wParam == VK_F10))
            return TRUE;
        key_down(l, (UINT)msg->wParam);
        return TRUE;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        return TRUE;
    case WM_CHAR:
    case WM_SYSCHAR: {
        WCHAR ch = (WCHAR)msg->wParam;
        BOOL seen = l->LastChar && GetTickCount() - l->LastCharTick < 1000 &&
                    CharUpperW((LPWSTR)(ULONG_PTR)ch) == CharUpperW((LPWSTR)(ULONG_PTR)l->LastChar);
        l->LastChar = 0;
        guard_clear();
        if (!seen && ch >= 0x20)
            handle_char(l, ch);
        return TRUE;
    }
    case WM_CANCELMODE:
        end_loop(l);
        return FALSE;
    }
    return FALSE;
}

static void run(LOOP *l)
{
    SetCapture(l->Capture);
    while (!l->Done) {
        MSG msg;
        if (l->IdlePending && !PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE)) {
            l->IdlePending = FALSE;
            if (l->Notify)
                SendMessageW(l->Owner, WM_ENTERIDLE, MSGF_MENU, (LPARAM)l->Owner);
            continue;
        }
        if (!GetMessageW(&msg, nullptr, 0, 0)) {
            /* WM_QUIT ends the menu and stays in the queue for the caller. */
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (l->Done)
            break;
        refresh(l);
        if (!handle_message(l, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        l->IdlePending = TRUE;
        if (l->Done)
            break;
        if (!IsWindow(l->Owner) || GetCapture() != l->Capture) {
            l->Done = TRUE;
            break;
        }
        /* a window menu ends when another window becomes active. */
        if (!l->Track && GetActiveWindow() != l->StartActive && !IsChild(l->StartActive, GetActiveWindow()))
            l->Done = TRUE;
    }
    if (GetCapture() == l->Capture)
        ReleaseCapture();
}


/* ------------------------------------------------------------------ starting */

static BOOL start_common(LOOP *l, HWND owner)
{
    memset(l, 0, sizeof(*l));
    l->Owner = owner;
    l->BarSel = NO_ITEM;
    l->Dpi = UserWindowDpi(owner);
    l->Font = UserGetMessageFont(l->Dpi);
    LOGFONTW lf;
    if (GetObjectW(l->Font, sizeof(lf), &lf)) {
        lf.lfWeight = FW_BOLD;
        l->BoldFont = CreateFontIndirectW(&lf);
    }
    LRESULT ui = SendMessageW(owner, WM_QUERYUISTATE, 0, 0);
    l->Cues = !(ui & UISF_HIDEACCEL);
    l->ButtonDown = (GetKeyState(VK_LBUTTON) & 0x8000) || (GetKeyState(VK_RBUTTON) & 0x8000);
    l->StartActive = GetActiveWindow();
    if (!l->StartActive)
        l->StartActive = GetAncestor(owner, GA_ROOT);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    l->StartTick = GetTickCount();
    GetCursorPos(&l->StartPoint);
    l->LastMouse = l->StartPoint;
    return TRUE;
}

static void finish(LOOP *l)
{
    kill_open_timer(l);
    stop_scroll(l);
    close_to_depth(l, 0);
    l->BarSel = NO_ITEM;
    l->Dropped = FALSE;
    set_bar_state(l);
    if (l->Notify) {
        SendMessageW(l->Owner, WM_MENUSELECT, MAKEWPARAM(0, 0xFFFF), 0);
        SendMessageW(l->Owner, WM_EXITMENULOOP, l->Track ? TRUE : FALSE, 0);
    }
    UserFree(l->BarItems);
    l->BarItems = nullptr;
    if (l->BoldFont)
        DeleteObject(l->BoldFont);
}

/* the command a closed menu leaves behind. */
static void post_command(LOOP *l)
{
    if (!l->Selected || !IsWindow(l->Owner))
        return;
    if (l->ResultSystem) {
        PostMessageW(l->Owner, WM_SYSCOMMAND, l->ResultId, MAKELPARAM(l->ResultPos.x, l->ResultPos.y));
        return;
    }
    MENUINFO mi = {sizeof(mi), MIM_STYLE};
    if (GetMenuInfo(l->ResultMenu, &mi) && (mi.dwStyle & MNS_NOTIFYBYPOS))
        PostMessageW(l->Owner, WM_MENUCOMMAND, (WPARAM)l->ResultIndex, (LPARAM)l->ResultMenu);
    else
        PostMessageW(l->Owner, WM_COMMAND, MAKEWPARAM(l->ResultId, 0), 0);
}

/*
 * when the desktop asked a notification icon for its menu, the desktop shows the menu the program pops up: win32k
 * hands it over and returns the choice. the thread keeps handling its messages meanwhile.
 */
static BOOL host_menu(HMENU menu, HWND hwnd, LOOP *l)
{
    MIME_TRAY_REQUEST request;
    memset(&request, 0, sizeof(request));
    request.Window = (ULONG)(ULONG_PTR)hwnd;
    request.Menu = (ULONG)(ULONG_PTR)menu;
    if (!NtUserMimeTray(MIME_TRAYOP_MENU, (PUCHAR)&request, sizeof(request)))
        return FALSE;
    for (;;) {
        ULONG answer = NtUserMimeTray(MIME_TRAYOP_MENU_RESULT, (PUCHAR)&request, sizeof(request));
        if (answer) {
            l->Selected = (answer & 0x80000000u) != 0;
            l->ResultId = answer & 0x7FFFFFFF;
            return TRUE;
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage((int)msg.wParam);
                return TRUE;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
    }
}

WINUSERAPI BOOL WINAPI TrackPopupMenuEx(HMENU menu, UINT flags, int x, int y, HWND hwnd, LPTPMPARAMS params)
{
    if (!IsMenu(menu)) {
        SetLastError(ERROR_INVALID_MENU_HANDLE);
        return FALSE;
    }
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (thread_loop() && !(flags & TPM_RECURSE)) {
        SetLastError(ERROR_POPUP_ALREADY_ACTIVE);
        return FALSE;
    }
    if (params && params->cbSize != sizeof(TPMPARAMS)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LOOP loop;
    start_common(&loop, hwnd);
    loop.Track = TRUE;
    loop.Flags = flags;
    loop.Notify = !(flags & TPM_NONOTIFY);
    HWND system_window = (HWND)NtUserMimeCallMenu(menu, 0, MIME_MENU_SYSTEMWINDOW);
    BOOL system = system_window != nullptr;
    loop.TimerWindow = hwnd;
    loop.ResultPos = (POINT){x, y};
    if (loop.Notify) {
        SendMessageW(hwnd, WM_ENTERMENULOOP, TRUE, 0);
        SendMessageW(hwnd, WM_INITMENU, (WPARAM)menu, 0);
        SendMessageW(hwnd, WM_INITMENUPOPUP, (WPARAM)menu, MAKELPARAM(0, system));
    }
    BOOL result = FALSE;
    if (!system && host_menu(menu, hwnd, &loop)) {
        result = TRUE;
    } else if (IsMenu(menu) && level_create(&loop, menu, system, NO_ITEM, x, y, params ? &params->rcExclude : nullptr,
                                            flags, FALSE)) {
        loop.Capture = loop.Levels[0].Window;
        loop.TimerWindow = loop.Capture;
        BOOL registered = loop_add(&loop);
        run(&loop);
        if (registered)
            loop_remove(&loop);
        result = TRUE;
    }
    finish(&loop);
    if (!result)
        return FALSE;
    if (flags & TPM_RETURNCMD)
        return loop.Selected ? (BOOL)loop.ResultId : 0;
    if (loop.Selected && !loop.ResultMenu) {
        /* the answer of the desktop. */
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(loop.ResultId, 0), 0);
    } else {
        post_command(&loop);
    }
    return TRUE;
}

WINUSERAPI BOOL WINAPI TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved, HWND hwnd,
                                      const RECT *rect)
{
    (void)reserved;
    (void)rect;
    return TrackPopupMenuEx(menu, flags, x, y, hwnd, nullptr);
}

WINUSERAPI BOOL WINAPI EndMenu(void)
{
    LOOP *l = thread_loop();
    if (l) {
        l->Done = TRUE;
        PostMessageW(l->Owner, WM_NULL, 0, 0);
    }
    return TRUE;
}

/*
 * the menu bar of a window and its system menu, run with the mouse or the keys. start is where the selection begins:
 * an item of the bar, SYSTEM_ICON, or NO_ITEM for the first one. a character is a mnemonic typed with Alt.
 */
static void run_window_menu(HWND hwnd, int start, BOOL open, WCHAR ch, BOOL mouse, POINT pos)
{
    if (thread_loop() || !IsWindow(hwnd))
        return;
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (style & WS_CHILD)
        return;
    HMENU bar = GetMenu(hwnd);
    if (bar && (!IsMenu(bar) || GetMenuItemCount(bar) <= 0))
        bar = nullptr;
    HMENU system = (style & WS_SYSMENU) ? GetSystemMenu(hwnd, FALSE) : nullptr;
    if (!bar && !system)
        return;
    if (!bar && ch) {
        /* nothing to look in: the window may still know the character, else it is a mistake. */
        LRESULT r = SendMessageW(hwnd, WM_MENUCHAR, MAKEWPARAM(ch, MF_SYSMENU), (LPARAM)system);
        if (!HIWORD(r))
            MessageBeep(0);
        return;
    }
    LOOP loop;
    start_common(&loop, hwnd);
    loop.Notify = TRUE;
    loop.BarWindow = hwnd;
    loop.BarMenu = bar;
    loop.SystemMenu = system;
    loop.Capture = hwnd;
    loop.TimerWindow = hwnd;
    loop.Keyboard = !mouse;
    loop.ResultPos = pos;
    if (!mouse) {
        loop.Cues = TRUE;
        SendMessageW(hwnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEACCEL | UISF_HIDEFOCUS), 0);
        loop.ButtonDown = FALSE;
    }
    SendMessageW(hwnd, WM_ENTERMENULOOP, FALSE, 0);
    BOOL system_first = start == SYSTEM_ICON && system;
    SendMessageW(hwnd, WM_INITMENU, (WPARAM)(system_first || !bar ? system : bar), 0);
    if (bar) {
        loop.BarSerial = (ULONG)NtUserMimeCallMenu(bar, 0, MIME_MENU_SERIAL);
        loop.BarItems = load_items(bar, &loop.BarCount);
    }
    BOOL registered = loop_add(&loop);
    if (!bar && start != SYSTEM_ICON)
        start = SYSTEM_ICON;
    /* Alt alone selects the first item, Alt and a character looks for its item without selecting one. */
    if (start == NO_ITEM && bar && !ch)
        start = first_valid(loop.BarItems, loop.BarCount, 0, 1);
    if (start != NO_ITEM && !(start == SYSTEM_ICON && !system)) {
        select_bar(&loop, start, open, mouse);
        if (open && !mouse && loop.Depth > 0) {
            LEVEL *lv = &loop.Levels[0];
            int first = first_valid(lv->Items, lv->Count, 0, 1);
            if (first != NO_ITEM)
                select_level_item(&loop, 0, first, FALSE);
        }
    }
    if (ch) {
        loop.LastChar = ch;
        loop.LastCharTick = GetTickCount();
        handle_char(&loop, ch);
        /* a character that finds nothing leaves the menu. */
        if (!loop.Done && loop.BarSel == NO_ITEM && loop.Depth == 0)
            loop.Done = TRUE;
    }
    if (!loop.Done)
        run(&loop);
    if (registered)
        loop_remove(&loop);
    finish(&loop);
    post_command(&loop);
}

/* the commands DefWindowProc turns into a menu. */
BOOL UserDefMenuCommand(HWND hwnd, WPARAM command, LPARAM lparam)
{
    switch (command & 0xFFF0) {
    case SC_KEYMENU: {
        WCHAR ch = (WCHAR)lparam;
        POINT none = {0, 0};
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        if (ch == L' ') {
            if (style & WS_SYSMENU)
                run_window_menu(hwnd, SYSTEM_ICON, TRUE, 0, FALSE, none);
        } else if (ch) {
            run_window_menu(hwnd, NO_ITEM, FALSE, ch, FALSE, none);
        } else {
            run_window_menu(hwnd, NO_ITEM, FALSE, 0, FALSE, none);
        }
        return TRUE;
    }
    case SC_MOUSEMENU: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        MIME_MENUBAR req;
        memset(&req, 0, sizeof(req));
        req.Op = MIME_MENUBAR_HIT;
        req.Point = pt;
        int start = NO_ITEM;
        if (NtUserCallHwndParam(hwnd, (ULONG_PTR)&req, MIME_HWNDPARAM_MENUBAR) && req.Item >= 0)
            start = req.Item;
        else if ((command & 0xF) == HTSYSMENU)
            start = SYSTEM_ICON;
        else
            return TRUE;
        run_window_menu(hwnd, start, TRUE, 0, TRUE, pt);
        return TRUE;
    }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ questions about the bar */

WINUSERAPI BOOL WINAPI GetMenuBarInfo(HWND hwnd, LONG object, LONG item, PMENUBARINFO info)
{
    if (!info || info->cbSize != sizeof(MENUBARINFO)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(&info->rcBar, 0, sizeof(*info) - offsetof(MENUBARINFO, rcBar));
    if (!IsWindow(hwnd) || item < 0)
        return FALSE;
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (object == OBJID_MENU) {
        HMENU menu = (style & WS_CHILD) ? nullptr : GetMenu(hwnd);
        if (!menu || !IsMenu(menu) || item > GetMenuItemCount(menu))
            return FALSE;
        info->hMenu = menu;
        MIME_MENUBAR req;
        memset(&req, 0, sizeof(req));
        req.Op = MIME_MENUBAR_RECT;
        req.Item = item;
        if (NtUserCallHwndParam(hwnd, (ULONG_PTR)&req, MIME_HWNDPARAM_MENUBAR))
            info->rcBar = req.Rect;
        LOOP *l = loop_matching(hwnd, nullptr);
        if (l && l->BarWindow == hwnd) {
            info->fBarFocused = -1;
            if (item == 0) {
                info->fFocused = -1;
            } else if (l->BarSel == item - 1) {
                info->fFocused = -1;
                if (l->Depth > 0)
                    info->hwndMenu = l->Levels[0].Window;
            }
        }
        return TRUE;
    }
    if (object == OBJID_SYSMENU) {
        HMENU menu = (style & WS_SYSMENU) ? GetSystemMenu(hwnd, FALSE) : nullptr;
        if (!menu || item > GetMenuItemCount(menu))
            return FALSE;
        info->hMenu = menu;
        if (item == 0 && (style & WS_CAPTION) == WS_CAPTION) {
            RECT window;
            GetWindowRect(hwnd, &window);
            UINT dpi = UserWindowDpi(hwnd);
            int frame = (style & WS_MAXIMIZE) ? 0 : GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                                                         GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            info->rcBar = (RECT){window.left + frame, window.top + frame,
                                 window.left + frame + GetSystemMetricsForDpi(SM_CXSIZE, dpi),
                                 window.top + frame + GetSystemMetricsForDpi(SM_CYSIZE, dpi)};
        }
        LOOP *l = loop_matching(hwnd, nullptr);
        if (l && l->BarWindow == hwnd && l->BarSel == SYSTEM_ICON) {
            info->fBarFocused = -1;
            if (item == 0) {
                info->fFocused = -1;
            } else if (l->Depth > 0 && l->Levels[0].Hot == item - 1) {
                info->fFocused = -1;
            }
        }
        return TRUE;
    }
    if (object == OBJID_CLIENT) {
        /* a popup window of the loop. */
        LOOP *l = loop_matching(hwnd, nullptr);
        for (int d = 0; l && d < l->Depth; d++) {
            LEVEL *lv = &l->Levels[d];
            if (lv->Window != hwnd || item > lv->Count)
                continue;
            info->hMenu = lv->Menu;
            if (item == 0) {
                info->rcBar = (RECT){lv->Left, lv->Top, lv->Left + lv->Width, lv->Top + lv->Height};
            } else {
                ITEM *it = &lv->Items[item - 1];
                info->rcBar = (RECT){lv->Left + it->Left, lv->Top + item_top(lv, it), lv->Left + it->Left + it->Cx,
                                 lv->Top + item_top(lv, it) + it->Height};
            }
            info->fBarFocused = d == l->Depth - 1;
            info->fFocused = item == 0 || lv->Hot == item - 1;
            return TRUE;
        }
    }
    return FALSE;
}

WINUSERAPI BOOL WINAPI GetMenuItemRect(HWND window, HMENU menu, UINT item, LPRECT rect)
{
    if (!rect)
        return FALSE;
    *rect = (RECT){0, 0, 0, 0};
    if (!IsMenu(menu)) {
        SetLastError(ERROR_INVALID_MENU_HANDLE);
        return FALSE;
    }
    LOOP *l = loop_matching(nullptr, menu);
    for (int d = 0; l && d < l->Depth; d++) {
        LEVEL *lv = &l->Levels[d];
        if (lv->Menu == menu && item < (UINT)lv->Count) {
            ITEM *it = &lv->Items[item];
            *rect = (RECT){lv->Left + it->Left, lv->Top + item_top(lv, it), lv->Left + it->Left + it->Cx,
                           lv->Top + item_top(lv, it) + it->Height};
            return TRUE;
        }
    }
    if (window && IsWindow(window) && menu == GetMenu(window) && item < (UINT)GetMenuItemCount(menu)) {
        MIME_MENUBAR req;
        memset(&req, 0, sizeof(req));
        req.Op = MIME_MENUBAR_RECT;
        req.Item = (LONG)item + 1;
        if (NtUserCallHwndParam(window, (ULONG_PTR)&req, MIME_HWNDPARAM_MENUBAR)) {
            *rect = req.Rect;
            return TRUE;
        }
    }
    return FALSE;
}

/* the item at a point of the screen, in the bar of the window or an open popup of the menu. */
WINUSERAPI int WINAPI MenuItemFromPoint(HWND window, HMENU menu, POINT screen)
{
    LOOP *l = loop_matching(nullptr, menu);
    for (int d = 0; l && d < l->Depth; d++) {
        LEVEL *lv = &l->Levels[d];
        int item;
        if (lv->Menu == menu && screen_point_in_level(lv, screen, &item))
            return item;
    }
    if (window && IsWindow(window) && menu == GetMenu(window)) {
        MIME_MENUBAR req;
        memset(&req, 0, sizeof(req));
        req.Op = MIME_MENUBAR_HIT;
        req.Point = screen;
        if (NtUserCallHwndParam(window, (ULONG_PTR)&req, MIME_HWNDPARAM_MENUBAR))
            return req.Item;
    }
    return -1;
}

/* the highlight of a bar item, drawn by the frame. */
WINUSERAPI BOOL WINAPI HiliteMenuItem(HWND window, HMENU menu, UINT item, UINT flags)
{
    MENUITEMINFOW mii = {sizeof(mii), MIIM_STATE};
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!NtUserMimeGetMenuItemInfo(menu, item, (flags & MF_BYPOSITION) != 0, &mii))
        return FALSE;
    mii.fState = (mii.fState & ~MFS_HILITE) | ((flags & MF_HILITE) ? MFS_HILITE : 0);
    if (!NtUserThunkedMenuItemInfo(menu, item, (flags & MF_BYPOSITION) != 0, FALSE, &mii, nullptr))
        return FALSE;
    if (menu == GetMenu(window))
        DrawMenuBar(window);
    return TRUE;
}

/* the parts of DefWindowProc that open the menus of a window. */
BOOL UserDefMenu(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    /* the keys of a menu are the top level window's business, so a child gives them to its parent. */
    BOOL keys = msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_SYSCHAR ||
                ((msg == WM_KEYUP) && (wparam == VK_MENU || wparam == VK_F10)) ||
                (msg == WM_SYSCOMMAND && ((wparam & 0xFFF0) == SC_KEYMENU));
    if (keys && (GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD)) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            *result = SendMessageW(parent, msg, wparam, lparam);
            return TRUE;
        }
    }
    switch (msg) {
    case WM_NCLBUTTONDOWN:
        if (wparam == HTMENU || wparam == HTSYSMENU) {
            SendMessageW(hwnd, WM_SYSCOMMAND, SC_MOUSEMENU + wparam, lparam);
            *result = 0;
            return TRUE;
        }
        return FALSE;
    case WM_SYSCOMMAND:
        if (UserDefMenuCommand(hwnd, wparam, lparam)) {
            *result = 0;
            return TRUE;
        }
        return FALSE;
    case WM_SYSKEYDOWN: {
        /* Alt shows the mnemonics of the bar for as long as it is down. */
        if (wparam == VK_MENU && !(lparam & (1 << 30)) && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            MIME_MENUBAR cues = {MIME_MENUBAR_CUES, 1};
            NtUserCallHwndParam(hwnd, (ULONG_PTR)&cues, MIME_HWNDPARAM_MENUBAR);
            return FALSE;
        }
        /* Shift and F10 are the menu key. */
        if (wparam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000)) {
            SendMessageW(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd, (LPARAM)-1);
            *result = 0;
            return TRUE;
        }
        /* a letter with Alt picks an item of the bar. the translated character that follows is not used again. */
        if (!(lparam & (1 << 29)) || (GetKeyState(VK_CONTROL) & 0x8000) || wparam == VK_MENU)
            return FALSE;
        BOOL space = wparam == VK_SPACE;
        UINT ch = (wparam >= '0' && wparam <= 'Z') || space || (wparam >= VK_OEM_1 && wparam <= VK_OEM_8)
                      ? MapVirtualKeyW((UINT)wparam, MAPVK_VK_TO_CHAR)
                      : 0;
        if (!ch)
            return FALSE;
        g_char_guard.Window = hwnd;
        g_char_guard.Char = (WCHAR)(ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)ch);
        g_char_guard.Tick = GetTickCount();
        SendMessageW(hwnd, WM_SYSCOMMAND, SC_KEYMENU, (LPARAM)ch);
        *result = 0;
        return TRUE;
    }
    case WM_SYSCHAR: {
        WCHAR ch = (WCHAR)wparam;
        if (g_char_guard.Window == hwnd && GetTickCount() - g_char_guard.Tick < 2000 &&
            CharUpperW((LPWSTR)(ULONG_PTR)ch) == (LPWSTR)(ULONG_PTR)g_char_guard.Char) {
            guard_clear();
            *result = 0;
            return TRUE;
        }
        if (ch == L'\r' && IsIconic(hwnd)) {
            PostMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
            *result = 0;
            return TRUE;
        }
        if (ch < 0x20 && ch != L' ')
            return FALSE;
        SendMessageW(hwnd, WM_SYSCOMMAND, SC_KEYMENU, (LPARAM)ch);
        *result = 0;
        return TRUE;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wparam == VK_MENU) {
            MIME_MENUBAR cues = {MIME_MENUBAR_CUES, 0};
            NtUserCallHwndParam(hwnd, (ULONG_PTR)&cues, MIME_HWNDPARAM_MENUBAR);
        }
        /* Alt or F10 alone select the menu bar when they come up. */
        if ((wparam == VK_MENU || wparam == VK_F10) && NtUserCallTwoParam(0, 0, MIME_TWOPARAM_ALTALONE)) {
            SendMessageW(hwnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
            *result = 0;
            return TRUE;
        }
        if (msg == WM_KEYUP && wparam == VK_APPS) {
            SendMessageW(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd, (LPARAM)-1);
            *result = 0;
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}
