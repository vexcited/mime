/*
 * menus: the menu api over the menu objects of win32k, and popup menu
 * tracking. a popup is a #32768 window owned by the tracking thread, drawn
 * and driven by a modal loop in user mode. submenus are listed but not
 * opened yet.
 */
#include "user32_private.h"

#define OLD_MENUITEMINFO_SIZE ((UINT)offsetof(MENUITEMINFOW, hbmpItem))
#define WM_UNINITMENUPOPUP 0x0125

static BOOL valid_info_size(UINT size)
{
    return size == sizeof(MENUITEMINFOW) || size == OLD_MENUITEMINFO_SIZE;
}

/* ------------------------------------------------------------------ objects */

WINUSERAPI HMENU WINAPI CreateMenu(void)
{
    return NtUserCreateMenu();
}

WINUSERAPI HMENU WINAPI CreatePopupMenu(void)
{
    return NtUserCreatePopupMenu();
}

WINUSERAPI BOOL WINAPI DestroyMenu(HMENU menu)
{
    return NtUserDestroyMenu(menu);
}

WINUSERAPI BOOL WINAPI IsMenu(HMENU menu)
{
    DWORD error = GetLastError();
    BOOL r = menu && NtUserMimeCallMenu(menu, 0, MIME_MENU_ISMENU) != 0;
    SetLastError(error);
    return r;
}

WINUSERAPI HMENU WINAPI GetMenu(HWND hwnd)
{
    if (GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD)
        return NULL;
    return (HMENU)GetWindowLongPtrW(hwnd, GWLP_ID);
}

WINUSERAPI BOOL WINAPI SetMenu(HWND hwnd, HMENU menu)
{
    return NtUserSetMenu(hwnd, menu, TRUE);
}

WINUSERAPI BOOL WINAPI DrawMenuBar(HWND hwnd)
{
    return NtUserDrawMenuBar(hwnd);
}

WINUSERAPI HMENU WINAPI GetSystemMenu(HWND hwnd, BOOL revert)
{
    return NtUserGetSystemMenu(hwnd, revert);
}

WINUSERAPI int WINAPI GetMenuItemCount(HMENU menu)
{
    if (!IsMenu(menu)) {
        SetLastError(ERROR_INVALID_MENU_HANDLE);
        return -1;
    }
    return (int)NtUserMimeCallMenu(menu, 0, MIME_MENU_ITEMCOUNT);
}

/* ------------------------------------------------------------------ item information */

static BOOL set_item(HMENU menu, UINT item, BOOL by_position, BOOL insert, const MENUITEMINFOW *mii)
{
    UNICODE_STRING text, *ptext = NULL;
    BOOL wants_text = (mii->fMask & MIIM_STRING) ||
                      ((mii->fMask & MIIM_TYPE) && !(mii->fType & (MFT_BITMAP | MFT_SEPARATOR | MFT_OWNERDRAW)));
    if (wants_text) {
        RtlInitUnicodeString(&text, mii->dwTypeData);
        ptext = &text;
    }
    return NtUserThunkedMenuItemInfo(menu, item, by_position, insert, (LPMENUITEMINFOW)mii, ptext);
}

WINUSERAPI BOOL WINAPI InsertMenuItemW(HMENU menu, UINT item, BOOL by_position, LPCMENUITEMINFOW info)
{
    if (!info || !valid_info_size(info->cbSize)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return set_item(menu, item, by_position, TRUE, info);
}

WINUSERAPI BOOL WINAPI SetMenuItemInfoW(HMENU menu, UINT item, BOOL by_position, LPCMENUITEMINFOW info)
{
    if (!info || !valid_info_size(info->cbSize)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return set_item(menu, item, by_position, FALSE, info);
}

WINUSERAPI BOOL WINAPI GetMenuItemInfoW(HMENU menu, UINT item, BOOL by_position, LPMENUITEMINFOW info)
{
    if (!info || !valid_info_size(info->cbSize)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtUserMimeGetMenuItemInfo(menu, item, by_position, info);
}

/* ansi item information converts the string both ways; the structures share their layout. */
static BOOL item_info_a(HMENU menu, UINT item, BOOL by_position, const MENUITEMINFOA *a, BOOL insert)
{
    if (!a || !valid_info_size(a->cbSize)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MENUITEMINFOW w;
    memset(&w, 0, sizeof(w));
    memcpy(&w, a, a->cbSize);
    w.cbSize = a->cbSize;
    BOOL text = (a->fMask & MIIM_STRING) ||
                ((a->fMask & MIIM_TYPE) && !(a->fType & (MFT_BITMAP | MFT_SEPARATOR | MFT_OWNERDRAW)));
    if (text)
        w.dwTypeData = UserAtoW(a->dwTypeData);
    BOOL ok = set_item(menu, item, by_position, insert, &w);
    if (text)
        UserFreeConverted(w.dwTypeData, a->dwTypeData);
    return ok;
}

WINUSERAPI BOOL WINAPI InsertMenuItemA(HMENU menu, UINT item, BOOL by_position, LPCMENUITEMINFOA info)
{
    return item_info_a(menu, item, by_position, info, TRUE);
}

WINUSERAPI BOOL WINAPI SetMenuItemInfoA(HMENU menu, UINT item, BOOL by_position, LPCMENUITEMINFOA info)
{
    return item_info_a(menu, item, by_position, info, FALSE);
}

WINUSERAPI BOOL WINAPI GetMenuItemInfoA(HMENU menu, UINT item, BOOL by_position, LPMENUITEMINFOA info)
{
    if (!info || !valid_info_size(info->cbSize)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MENUITEMINFOW w;
    memset(&w, 0, sizeof(w));
    memcpy(&w, info, info->cbSize);
    BOOL text = (info->fMask & (MIIM_STRING | MIIM_TYPE)) != 0;
    WCHAR buffer[1024];
    if (text && info->dwTypeData && info->cch) {
        w.dwTypeData = buffer;
        w.cch = RTL_NUMBER_OF(buffer);
    } else if (text) {
        w.dwTypeData = NULL;
        w.cch = 0;
    }
    if (!NtUserMimeGetMenuItemInfo(menu, item, by_position, &w))
        return FALSE;
    LPSTR user_text = info->dwTypeData;
    UINT user_cch = info->cch;
    memcpy(info, &w, info->cbSize);
    info->dwTypeData = user_text;
    if (text && (w.fType & MFT_BITMAP) && (info->fMask & MIIM_TYPE)) {
        info->dwTypeData = (LPSTR)w.dwTypeData;
    } else if (text && user_text && user_cch && w.dwTypeData == buffer) {
        int n = WideCharToMultiByte(CP_ACP, 0, buffer, (int)w.cch, user_text, (int)user_cch - 1, NULL, NULL);
        user_text[n] = 0;
        info->cch = (UINT)n;
    } else if (text) {
        info->cch = w.cch;
    }
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetMenuInfo(HMENU menu, LPMENUINFO info)
{
    if (!info || info->cbSize != sizeof(MENUINFO)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return (BOOL)NtUserMimeCallMenu(menu, (ULONG_PTR)info, MIME_MENU_GETINFO);
}

WINUSERAPI BOOL WINAPI SetMenuInfo(HMENU menu, LPCMENUINFO info)
{
    if (!info || info->cbSize != sizeof(MENUINFO)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtUserThunkedMenuInfo(menu, info);
}

WINUSERAPI HMENU WINAPI GetSubMenu(HMENU menu, int position)
{
    MENUITEMINFOW mii = {sizeof(mii), MIIM_SUBMENU};
    if (!NtUserMimeGetMenuItemInfo(menu, (UINT)position, TRUE, &mii))
        return NULL;
    return mii.hSubMenu;
}

WINUSERAPI UINT WINAPI GetMenuItemID(HMENU menu, int position)
{
    MENUITEMINFOW mii = {sizeof(mii), MIIM_ID | MIIM_SUBMENU};
    if (!NtUserMimeGetMenuItemInfo(menu, (UINT)position, TRUE, &mii) || mii.hSubMenu)
        return (UINT)-1;
    return mii.wID;
}

WINUSERAPI UINT WINAPI GetMenuState(HMENU menu, UINT id, UINT flags)
{
    MENUITEMINFOW mii = {sizeof(mii), MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU};
    if (!NtUserMimeGetMenuItemInfo(menu, id, (flags & MF_BYPOSITION) != 0, &mii))
        return (UINT)-1;
    UINT state = mii.fState | (mii.fType & ~MFT_RADIOCHECK);
    if (mii.hSubMenu) {
        int count = GetMenuItemCount(mii.hSubMenu);
        return (UINT)((count > 0 ? count : 0) << 8) | (state & 0xFF) | MF_POPUP;
    }
    return state;
}

WINUSERAPI int WINAPI GetMenuStringW(HMENU menu, UINT id, LPWSTR buffer, int max, UINT flags)
{
    MENUITEMINFOW mii = {sizeof(mii), MIIM_STRING};
    mii.dwTypeData = max > 0 ? buffer : NULL;
    mii.cch = max > 0 && buffer ? (UINT)max : 0;
    if (!NtUserMimeGetMenuItemInfo(menu, id, (flags & MF_BYPOSITION) != 0, &mii))
        return 0;
    return (int)mii.cch;
}

WINUSERAPI int WINAPI GetMenuStringA(HMENU menu, UINT id, LPSTR buffer, int max, UINT flags)
{
    WCHAR w[1024];
    int n = GetMenuStringW(menu, id, w, RTL_NUMBER_OF(w), flags);
    if (!buffer || max <= 0)
        return n ? WideCharToMultiByte(CP_ACP, 0, w, n, NULL, 0, NULL, NULL) : 0;
    int a = n ? WideCharToMultiByte(CP_ACP, 0, w, n, buffer, max - 1, NULL, NULL) : 0;
    buffer[a] = 0;
    return a;
}

/* ------------------------------------------------------------------ legacy item calls */

/* the MF_* form of an item as a MENUITEMINFOW that replaces every field. */
static void flags_to_info(UINT flags, UINT_PTR id, LPCWSTR item, MENUITEMINFOW *mii)
{
    memset(mii, 0, sizeof(*mii));
    mii->cbSize = sizeof(*mii);
    mii->fMask = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU | MIIM_STRING | MIIM_BITMAP | MIIM_DATA;
    mii->fType = flags & (MFT_MENUBARBREAK | MFT_MENUBREAK | MFT_RIGHTJUSTIFY | MFT_RIGHTORDER);
    mii->fState = flags & (MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_HILITE | MF_DEFAULT);
    if (flags & MF_POPUP) {
        mii->hSubMenu = (HMENU)id;
        mii->wID = (UINT)id;
    } else {
        mii->wID = (UINT)id;
    }
    if (flags & MF_SEPARATOR) {
        mii->fType |= MFT_SEPARATOR;
        mii->fMask &= ~MIIM_STRING;
    } else if (flags & MF_BITMAP) {
        mii->hbmpItem = (HBITMAP)item;
        mii->fMask &= ~MIIM_STRING;
    } else if (flags & MF_OWNERDRAW) {
        mii->fType |= MFT_OWNERDRAW;
        mii->dwItemData = (ULONG_PTR)item;
        mii->fMask &= ~MIIM_STRING;
    } else {
        mii->dwTypeData = (LPWSTR)item;
    }
}

WINUSERAPI BOOL WINAPI InsertMenuW(HMENU menu, UINT position, UINT flags, UINT_PTR id, LPCWSTR item)
{
    MENUITEMINFOW mii;
    flags_to_info(flags, id, item, &mii);
    return set_item(menu, position, (flags & MF_BYPOSITION) != 0, TRUE, &mii);
}

WINUSERAPI BOOL WINAPI AppendMenuW(HMENU menu, UINT flags, UINT_PTR id, LPCWSTR item)
{
    return InsertMenuW(menu, (UINT)-1, flags | MF_BYPOSITION, id, item);
}

WINUSERAPI BOOL WINAPI ModifyMenuW(HMENU menu, UINT position, UINT flags, UINT_PTR id, LPCWSTR item)
{
    BOOL by_position = (flags & MF_BYPOSITION) != 0;
    MENUITEMINFOW old = {sizeof(old), MIIM_SUBMENU};
    if (!NtUserMimeGetMenuItemInfo(menu, position, by_position, &old))
        return FALSE;
    MENUITEMINFOW mii;
    flags_to_info(flags, id, item, &mii);
    if (!set_item(menu, position, by_position, FALSE, &mii))
        return FALSE;
    /* a replaced drop down menu is destroyed. */
    if (old.hSubMenu && old.hSubMenu != mii.hSubMenu)
        DestroyMenu(old.hSubMenu);
    return TRUE;
}

static LPWSTR item_a_to_w(UINT flags, LPCSTR item)
{
    if (flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR))
        return (LPWSTR)item;
    return UserAtoW(item);
}

static void free_item_w(UINT flags, LPWSTR w, LPCSTR item)
{
    if (!(flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR)))
        UserFreeConverted(w, item);
}

WINUSERAPI BOOL WINAPI InsertMenuA(HMENU menu, UINT position, UINT flags, UINT_PTR id, LPCSTR item)
{
    LPWSTR w = item_a_to_w(flags, item);
    BOOL ok = InsertMenuW(menu, position, flags, id, w);
    free_item_w(flags, w, item);
    return ok;
}

WINUSERAPI BOOL WINAPI AppendMenuA(HMENU menu, UINT flags, UINT_PTR id, LPCSTR item)
{
    return InsertMenuA(menu, (UINT)-1, flags | MF_BYPOSITION, id, item);
}

WINUSERAPI BOOL WINAPI ModifyMenuA(HMENU menu, UINT position, UINT flags, UINT_PTR id, LPCSTR item)
{
    LPWSTR w = item_a_to_w(flags, item);
    BOOL ok = ModifyMenuW(menu, position, flags, id, w);
    free_item_w(flags, w, item);
    return ok;
}

WINUSERAPI BOOL WINAPI DeleteMenu(HMENU menu, UINT position, UINT flags)
{
    return NtUserDeleteMenu(menu, position, flags);
}

WINUSERAPI BOOL WINAPI RemoveMenu(HMENU menu, UINT position, UINT flags)
{
    return NtUserRemoveMenu(menu, position, flags);
}

WINUSERAPI BOOL WINAPI EnableMenuItem(HMENU menu, UINT id, UINT flags)
{
    return (BOOL)NtUserEnableMenuItem(menu, id, flags);
}

WINUSERAPI DWORD WINAPI CheckMenuItem(HMENU menu, UINT id, UINT flags)
{
    return NtUserCheckMenuItem(menu, id, flags);
}

static int item_position(HMENU menu, UINT item, BOOL by_position)
{
    if (by_position)
        return (int)item;
    int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; i++) {
        MENUITEMINFOW mii = {sizeof(mii), MIIM_ID};
        if (NtUserMimeGetMenuItemInfo(menu, (UINT)i, TRUE, &mii) && mii.wID == item)
            return i;
    }
    return -1;
}

WINUSERAPI BOOL WINAPI CheckMenuRadioItem(HMENU menu, UINT first, UINT last, UINT check, UINT flags)
{
    BOOL by_position = (flags & MF_BYPOSITION) != 0;
    int a = item_position(menu, first, by_position), b = item_position(menu, last, by_position);
    int c = item_position(menu, check, by_position);
    if (a < 0 || b < 0 || a > b)
        return FALSE;
    BOOL found = FALSE;
    for (int i = a; i <= b; i++) {
        MENUITEMINFOW mii = {sizeof(mii), MIIM_FTYPE | MIIM_STATE};
        if (!NtUserMimeGetMenuItemInfo(menu, (UINT)i, TRUE, &mii) || (mii.fType & MFT_SEPARATOR))
            continue;
        if (i == c) {
            mii.fType |= MFT_RADIOCHECK;
            mii.fState |= MFS_CHECKED;
            found = TRUE;
        } else {
            mii.fState &= ~MFS_CHECKED;
        }
        NtUserThunkedMenuItemInfo(menu, (UINT)i, TRUE, FALSE, &mii, NULL);
    }
    return found;
}

WINUSERAPI BOOL WINAPI SetMenuDefaultItem(HMENU menu, UINT item, UINT by_position)
{
    return NtUserSetMenuDefaultItem(menu, item, by_position);
}

WINUSERAPI UINT WINAPI GetMenuDefaultItem(HMENU menu, UINT by_position, UINT flags)
{
    return (UINT)NtUserMimeCallMenu(menu, (flags & 0xFFFF) | (by_position ? 0x10000 : 0), MIME_MENU_DEFAULTITEM);
}

/* ------------------------------------------------------------------ popup tracking */

typedef struct POPUP_ITEM {
    UINT Id;
    UINT Type;
    UINT State;
    HMENU SubMenu;
    WCHAR Text[128];
    int Top;
    int Height;
} POPUP_ITEM;

typedef struct POPUP_STATE {
    HMENU Menu;
    HWND Owner;
    HWND Window;
    UINT Flags;
    UINT Dpi;
    HFONT Font;
    POPUP_ITEM *Items;
    int Count;
    int Hot;
    int Width;
    int Height;
    UINT Result;
    BOOL Done;
    BOOL Selected;
} POPUP_STATE;

static POPUP_STATE *volatile g_tracking;

static int scale(int v, UINT dpi)
{
    return UserMulDiv(v, (int)dpi, 96);
}

static BOOL item_selectable(const POPUP_ITEM *it)
{
    return !(it->Type & MFT_SEPARATOR) && !(it->State & (MFS_GRAYED | MF_DISABLED));
}

static void split_accelerator(const WCHAR *text, int *label, const WCHAR **accel)
{
    const WCHAR *tab = wcschr(text, L'\t');
    *label = tab ? (int)(tab - text) : (int)wcslen(text);
    *accel = tab ? tab + 1 : NULL;
}

static BOOL load_items(POPUP_STATE *ps)
{
    int count = GetMenuItemCount(ps->Menu);
    if (count <= 0)
        return FALSE;
    ps->Items = UserAlloc((SIZE_T)count * sizeof(POPUP_ITEM));
    if (!ps->Items)
        return FALSE;
    ps->Count = count;
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, ps->Font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int item_height = tm.tmHeight + scale(10, ps->Dpi);
    if (item_height < scale(24, ps->Dpi))
        item_height = scale(24, ps->Dpi);
    int label_width = 0, accel_width = 0;
    int y = scale(4, ps->Dpi);
    for (int i = 0; i < count; i++) {
        POPUP_ITEM *it = &ps->Items[i];
        MENUITEMINFOW mii = {sizeof(mii), MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU | MIIM_STRING};
        mii.dwTypeData = it->Text;
        mii.cch = RTL_NUMBER_OF(it->Text);
        if (NtUserMimeGetMenuItemInfo(ps->Menu, (UINT)i, TRUE, &mii)) {
            it->Id = mii.wID;
            it->Type = mii.fType;
            it->State = mii.fState;
            it->SubMenu = mii.hSubMenu;
        }
        it->Top = y;
        it->Height = (it->Type & MFT_SEPARATOR) ? scale(9, ps->Dpi) : item_height;
        y += it->Height;
        if (!(it->Type & MFT_SEPARATOR)) {
            int label;
            const WCHAR *accel;
            split_accelerator(it->Text, &label, &accel);
            RECT r = {0, 0, 0, 0};
            DrawTextW(dc, it->Text, label, &r, DT_SINGLELINE | DT_CALCRECT);
            if (r.right > label_width)
                label_width = r.right;
            if (accel) {
                SIZE sz;
                GetTextExtentPoint32W(dc, accel, (int)wcslen(accel), &sz);
                if (sz.cx > accel_width)
                    accel_width = sz.cx;
            }
        }
    }
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    ps->Width = scale(36, ps->Dpi) + label_width + (accel_width ? scale(32, ps->Dpi) + accel_width : 0) +
                scale(32, ps->Dpi);
    if (ps->Width < scale(120, ps->Dpi))
        ps->Width = scale(120, ps->Dpi);
    ps->Height = y + scale(4, ps->Dpi);
    return TRUE;
}

static void paint_popup(POPUP_STATE *ps, HDC dc)
{
    RECT all = {0, 0, ps->Width, ps->Height};
    FillRect(dc, &all, GetSysColorBrush(COLOR_MENU));
    HBRUSH frame = CreateSolidBrush(RGB(0xC8, 0xC8, 0xC8));
    FrameRect(dc, &all, frame);
    DeleteObject(frame);
    HGDIOBJ old_font = SelectObject(dc, ps->Font);
    SetBkMode(dc, TRANSPARENT);
    int pad = scale(4, ps->Dpi), left = scale(36, ps->Dpi), right = scale(16, ps->Dpi);
    for (int i = 0; i < ps->Count; i++) {
        POPUP_ITEM *it = &ps->Items[i];
        RECT r = {pad, it->Top, ps->Width - pad, it->Top + it->Height};
        if (it->Type & MFT_SEPARATOR) {
            RECT line = {r.left + scale(8, ps->Dpi), (r.top + r.bottom) / 2, r.right - scale(8, ps->Dpi),
                         (r.top + r.bottom) / 2 + 1};
            FillRect(dc, &line, GetSysColorBrush(COLOR_3DSHADOW));
            continue;
        }
        BOOL hot = i == ps->Hot;
        BOOL grey = (it->State & (MFS_GRAYED | MF_DISABLED)) != 0;
        if (hot)
            FillRect(dc, &r, GetSysColorBrush(COLOR_MENUHILIGHT));
        COLORREF color = grey ? GetSysColor(COLOR_GRAYTEXT) : hot ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                                                                 : GetSysColor(COLOR_MENUTEXT);
        SetTextColor(dc, color);
        if (it->State & MFS_CHECKED) {
            /* a check mark, or a dot for radio items. */
            HPEN pen = CreatePen(PS_SOLID, scale(2, ps->Dpi), color);
            HGDIOBJ old_pen = SelectObject(dc, pen);
            int cx = r.left + scale(14, ps->Dpi), cy = (r.top + r.bottom) / 2, s = scale(4, ps->Dpi);
            if (it->Type & MFT_RADIOCHECK) {
                HBRUSH dot = CreateSolidBrush(color);
                HGDIOBJ old_brush = SelectObject(dc, dot);
                Ellipse(dc, cx - s / 2 - 1, cy - s / 2 - 1, cx + s / 2 + 2, cy + s / 2 + 2);
                SelectObject(dc, old_brush);
                DeleteObject(dot);
            } else {
                MoveToEx(dc, cx - s, cy, NULL);
                LineTo(dc, cx - s / 3, cy + s - s / 3);
                LineTo(dc, cx + s + 1, cy - s);
            }
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        }
        int label;
        const WCHAR *accel;
        split_accelerator(it->Text, &label, &accel);
        RECT text = {r.left + left - pad, r.top, r.right - right, r.bottom};
        UINT bold_flags = DT_SINGLELINE | DT_VCENTER | DT_LEFT;
        DrawTextW(dc, it->Text, label, &text, bold_flags);
        if (accel)
            DrawTextW(dc, accel, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        if (it->SubMenu) {
            /* the submenu arrow. */
            int ax = r.right - scale(12, ps->Dpi), ay = (r.top + r.bottom) / 2, s = scale(3, ps->Dpi);
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
    SelectObject(dc, old_font);
}

LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    POPUP_STATE *ps = (POPUP_STATE *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT p;
        HDC dc = BeginPaint(hwnd, &p);
        if (ps && dc)
            paint_popup(ps, dc);
        EndPaint(hwnd, &p);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        if (ps && (msg == WM_CANCELMODE || (HWND)lparam != hwnd))
            ps->Done = TRUE;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static int item_at(POPUP_STATE *ps, int x, int y)
{
    if (x < 0 || x >= ps->Width)
        return -1;
    for (int i = 0; i < ps->Count; i++)
        if (y >= ps->Items[i].Top && y < ps->Items[i].Top + ps->Items[i].Height)
            return i;
    return -1;
}

static void set_hot(POPUP_STATE *ps, int hot)
{
    if (hot == ps->Hot)
        return;
    ps->Hot = hot;
    InvalidateRect(ps->Window, NULL, FALSE);
    if (!(ps->Flags & TPM_NONOTIFY)) {
        UINT flags = 0, id = 0;
        if (hot >= 0) {
            POPUP_ITEM *it = &ps->Items[hot];
            flags = (it->State & 0xFF) | (it->SubMenu ? MF_POPUP : 0) | MF_HILITE;
            id = it->SubMenu ? (UINT)hot : it->Id;
        }
        SendMessageW(ps->Owner, WM_MENUSELECT, MAKEWPARAM(id, flags), (LPARAM)ps->Menu);
    }
}

static void choose(POPUP_STATE *ps, int index)
{
    if (index < 0 || index >= ps->Count || !item_selectable(&ps->Items[index]) || ps->Items[index].SubMenu)
        return;
    ps->Result = ps->Items[index].Id;
    ps->Selected = TRUE;
    ps->Done = TRUE;
}

static void move_hot(POPUP_STATE *ps, int step)
{
    int i = ps->Hot;
    for (int n = 0; n < ps->Count; n++) {
        i = i < 0 ? (step > 0 ? 0 : ps->Count - 1) : (i + step + ps->Count) % ps->Count;
        if (!(ps->Items[i].Type & MFT_SEPARATOR)) {
            set_hot(ps, i);
            return;
        }
    }
}

/* the mnemonic character after & in an item label. */
static WCHAR mnemonic(const WCHAR *text)
{
    for (const WCHAR *p = text; *p && *p != L'\t'; p++)
        if (*p == L'&' && p[1] && p[1] != L'&')
            return (WCHAR)(ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)p[1]);
        else if (*p == L'&')
            p++;
    return 0;
}

static void track(POPUP_STATE *ps)
{
    SetCapture(ps->Window);
    UINT up = (ps->Flags & TPM_RIGHTBUTTON) ? 0 : 1;
    while (!ps->Done) {
        MSG msg;
        if (!GetMessageW(&msg, NULL, 0, 0)) {
            /* WM_QUIT ends the menu and stays in the queue for the caller. */
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (ps->Done)
            break;
        BOOL handled = TRUE;
        switch (msg.message) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            POINT pt = {(SHORT)LOWORD(msg.lParam), (SHORT)HIWORD(msg.lParam)};
            if (msg.hwnd != ps->Window)
                MapWindowPoints(msg.hwnd, ps->Window, &pt, 1);
            int hit = item_at(ps, pt.x, pt.y);
            BOOL inside = pt.x >= 0 && pt.y >= 0 && pt.x < ps->Width && pt.y < ps->Height;
            if (msg.message == WM_MOUSEMOVE) {
                if (inside)
                    set_hot(ps, hit >= 0 && !(ps->Items[hit].Type & MFT_SEPARATOR) ? hit : -1);
            } else if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN) {
                if (!inside)
                    ps->Done = TRUE;
            } else if (inside && (msg.message == WM_LBUTTONUP || up == 0)) {
                choose(ps, hit);
            }
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            switch (msg.wParam) {
            case VK_UP:
                move_hot(ps, -1);
                break;
            case VK_DOWN:
                move_hot(ps, 1);
                break;
            case VK_HOME:
                ps->Hot = -1;
                move_hot(ps, 1);
                break;
            case VK_END:
                ps->Hot = -1;
                move_hot(ps, -1);
                break;
            case VK_RETURN:
                choose(ps, ps->Hot);
                break;
            case VK_ESCAPE:
            case VK_LEFT:
            case VK_MENU:
            case VK_F10:
                ps->Done = TRUE;
                break;
            }
            break;
        case WM_CHAR:
        case WM_SYSCHAR: {
            WCHAR c = (WCHAR)(ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)(WCHAR)msg.wParam);
            for (int i = 0; i < ps->Count; i++)
                if (!(ps->Items[i].Type & MFT_SEPARATOR) && mnemonic(ps->Items[i].Text) == c) {
                    set_hot(ps, i);
                    choose(ps, i);
                    break;
                }
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            break;
        default:
            handled = FALSE;
            break;
        }
        if (!handled) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!ps->Done && GetCapture() != ps->Window)
            ps->Done = TRUE;
    }
    if (GetCapture() == ps->Window)
        ReleaseCapture();
}

static void place_popup(POPUP_STATE *ps, int *x, int *y, const RECT *exclude)
{
    UINT h = ps->Flags & (TPM_CENTERALIGN | TPM_RIGHTALIGN);
    if (h == TPM_RIGHTALIGN)
        *x -= ps->Width;
    else if (h == TPM_CENTERALIGN)
        *x -= ps->Width / 2;
    if (ps->Flags & TPM_BOTTOMALIGN)
        *y -= ps->Height;
    else if (ps->Flags & TPM_VCENTERALIGN)
        *y -= ps->Height / 2;
    POINT pt = {*x, *y};
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    RECT area = (ps->Flags & TPM_WORKAREA) ? mi.rcWork : mi.rcMonitor;
    /* a popup that does not fit opens on the other side of the point or of the excluded rectangle. */
    if (*x + ps->Width > area.right) {
        int flipped = (exclude ? exclude->left : *x) - ps->Width;
        *x = flipped >= area.left ? flipped : area.right - ps->Width;
    }
    if (*y + ps->Height > area.bottom) {
        int flipped = (exclude ? exclude->top : *y) - ps->Height;
        *y = flipped >= area.top ? flipped : area.bottom - ps->Height;
    }
    if (*x < area.left)
        *x = area.left;
    if (*y < area.top)
        *y = area.top;
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
    if (g_tracking && !(flags & TPM_RECURSE)) {
        SetLastError(ERROR_POPUP_ALREADY_ACTIVE);
        return FALSE;
    }
    if (params && params->cbSize != sizeof(TPMPARAMS)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL notify = !(flags & TPM_NONOTIFY);
    BOOL system = (HWND)NtUserMimeCallMenu(menu, 0, MIME_MENU_SYSTEMWINDOW) != NULL;
    if (notify) {
        SendMessageW(hwnd, WM_ENTERMENULOOP, TRUE, 0);
        SendMessageW(hwnd, WM_INITMENUPOPUP, (WPARAM)menu, MAKELPARAM(0, system));
    }
    POPUP_STATE ps;
    memset(&ps, 0, sizeof(ps));
    ps.Menu = menu;
    ps.Owner = hwnd;
    ps.Flags = flags;
    ps.Hot = -1;
    ps.Dpi = UserWindowDpi(hwnd);
    ps.Font = UserGetMessageFont(ps.Dpi);
    BOOL result = FALSE;
    if (load_items(&ps)) {
        place_popup(&ps, &x, &y, params ? &params->rcExclude : NULL);
        ps.Window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"#32768", NULL, WS_POPUP, x,
                                    y, ps.Width, ps.Height, hwnd, NULL, User32Instance, NULL);
        if (ps.Window) {
            SetWindowLongPtrW(ps.Window, GWLP_USERDATA, (LONG_PTR)&ps);
            ShowWindow(ps.Window, SW_SHOWNA);
            UpdateWindow(ps.Window);
            POPUP_STATE *previous = g_tracking;
            g_tracking = &ps;
            track(&ps);
            g_tracking = previous;
            SetWindowLongPtrW(ps.Window, GWLP_USERDATA, 0);
            DestroyWindow(ps.Window);
            result = TRUE;
        }
    }
    UserFree(ps.Items);
    if (notify) {
        SendMessageW(hwnd, WM_MENUSELECT, MAKEWPARAM(0, 0xFFFF), 0);
        SendMessageW(hwnd, WM_UNINITMENUPOPUP, (WPARAM)menu, MAKELPARAM(0, system ? MF_SYSMENU : 0));
        SendMessageW(hwnd, WM_EXITMENULOOP, TRUE, 0);
    }
    if (!result)
        return FALSE;
    if (flags & TPM_RETURNCMD)
        return ps.Selected ? (BOOL)ps.Result : 0;
    if (ps.Selected) {
        if (system)
            PostMessageW(hwnd, WM_SYSCOMMAND, ps.Result, MAKELPARAM(x, y));
        else
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ps.Result, 0), 0);
    }
    return TRUE;
}

WINUSERAPI BOOL WINAPI TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved, HWND hwnd,
                                      const RECT *rect)
{
    (void)reserved;
    (void)rect;
    return TrackPopupMenuEx(menu, flags, x, y, hwnd, NULL);
}

WINUSERAPI BOOL WINAPI EndMenu(void)
{
    POPUP_STATE *ps = g_tracking;
    if (ps) {
        ps->Done = TRUE;
        PostMessageW(ps->Window, WM_NULL, 0, 0);
    }
    return TRUE;
}
