/* the menu API over win32k menu objects. showing and running menus is in menuloop.c. */
#include "user32_private.h"

#define OLD_MENUITEMINFO_SIZE ((UINT)offsetof(MENUITEMINFOW, hbmpItem))

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
        return nullptr;
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
    UNICODE_STRING text, *ptext = nullptr;
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

/* ansi item info converts the string both ways, the structures share their layout. */
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
        w.dwTypeData = nullptr;
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
        int n = WideCharToMultiByte(CP_ACP, 0, buffer, (int)w.cch, user_text, (int)user_cch - 1, nullptr, nullptr);
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
        return nullptr;
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
    mii.dwTypeData = max > 0 ? buffer : nullptr;
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
        return n ? WideCharToMultiByte(CP_ACP, 0, w, n, nullptr, 0, nullptr, nullptr) : 0;
    int a = n ? WideCharToMultiByte(CP_ACP, 0, w, n, buffer, max - 1, nullptr, nullptr) : 0;
    buffer[a] = 0;
    return a;
}

/* ------------------------------------------------------------------ legacy item calls */

/* converts MF_* flags to a MENUITEMINFOW that replaces every field. */
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
        NtUserThunkedMenuItemInfo(menu, (UINT)i, TRUE, FALSE, &mii, nullptr);
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

WINUSERAPI BOOL WINAPI SetMenuItemBitmaps(HMENU menu, UINT item, UINT flags, HBITMAP unchecked, HBITMAP checked)
{
    MENUITEMINFOW mii = {sizeof(mii), MIIM_CHECKMARKS};
    mii.hbmpChecked = checked;
    mii.hbmpUnchecked = unchecked;
    return set_item(menu, item, (flags & MF_BYPOSITION) != 0, FALSE, &mii);
}

WINUSERAPI LONG WINAPI GetMenuCheckMarkDimensions(void)
{
    return MAKELONG(GetSystemMetrics(SM_CXMENUCHECK), GetSystemMetrics(SM_CYMENUCHECK));
}

/* ------------------------------------------------------------------ menu templates */

static int menu_string_length(const WCHAR *s, const WCHAR *end)
{
    const WCHAR *p = s;
    while (p < end && *p)
        p++;
    return p < end ? (int)(p - s) : -1;
}

/* parses MENUITEMTEMPLATE items up to the MF_END item, returns the end or NULL. */
static const BYTE *parse_menu(const BYTE *p, const BYTE *end, HMENU menu, int depth)
{
    WORD flags;
    do {
        if (depth > 32 || p + sizeof(WORD) > end)
            return nullptr;
        flags = *(const WORD *)p;
        p += sizeof(WORD);
        WORD id = 0;
        if (!(flags & MF_POPUP)) {
            if (p + sizeof(WORD) > end)
                return nullptr;
            id = *(const WORD *)p;
            p += sizeof(WORD);
        }
        const WCHAR *text = (const WCHAR *)p;
        int len = menu_string_length(text, (const WCHAR *)end);
        if (len < 0)
            return nullptr;
        p += (SIZE_T)(len + 1) * sizeof(WCHAR);
        UINT item_flags = flags & ~(MF_END | MF_POPUP);
        if (flags & MF_POPUP) {
            HMENU sub = CreatePopupMenu();
            if (!sub)
                return nullptr;
            p = parse_menu(p, end, sub, depth + 1);
            if (!p) {
                DestroyMenu(sub);
                return nullptr;
            }
            AppendMenuW(menu, item_flags | MF_POPUP, (UINT_PTR)sub, text);
        } else if (!len && !id) {
            AppendMenuW(menu, item_flags | MF_SEPARATOR, 0, nullptr);
        } else {
            AppendMenuW(menu, item_flags, id, text);
        }
    } while (!(flags & MF_END));
    return p;
}

static const BYTE *align_up(const BYTE *p, const BYTE *base, SIZE_T alignment)
{
    SIZE_T offset = (SIZE_T)(p - base);
    return base + ((offset + alignment - 1) & ~(alignment - 1));
}

/* parses MENUEX_TEMPLATE_ITEM entries; text is word aligned, the next field dword aligned. */
static const BYTE *parse_menu_ex(const BYTE *p, const BYTE *base, const BYTE *end, HMENU menu, int depth)
{
    WORD info;
    do {
        p = align_up(p, base, 4);
        if (depth > 32 || p + 14 > end)
            return nullptr;
        MENUITEMINFOW mii;
        memset(&mii, 0, sizeof(mii));
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID;
        mii.fType = *(const DWORD *)p;
        mii.fState = *(const DWORD *)(p + 4);
        mii.wID = *(const DWORD *)(p + 8);
        info = *(const WORD *)(p + 12);
        p += 14;
        const WCHAR *text = (const WCHAR *)p;
        int len = menu_string_length(text, (const WCHAR *)end);
        if (len < 0)
            return nullptr;
        p += (SIZE_T)(len + 1) * sizeof(WCHAR);
        if (len) {
            mii.fMask |= MIIM_STRING;
            mii.dwTypeData = (LPWSTR)text;
        }
        if (info & 0x01) {
            p = align_up(p, base, 4) + sizeof(DWORD); /* help id. */
            HMENU sub = CreatePopupMenu();
            if (!sub)
                return nullptr;
            p = parse_menu_ex(p, base, end, sub, depth + 1);
            if (!p) {
                DestroyMenu(sub);
                return nullptr;
            }
            mii.fMask |= MIIM_SUBMENU;
            mii.hSubMenu = sub;
        } else if (!len && !(mii.fType & MFT_SEPARATOR) && !mii.wID) {
            mii.fType |= MFT_SEPARATOR;
        }
        set_item(menu, (UINT)-1, TRUE, TRUE, &mii);
    } while (!(info & MF_END));
    return p;
}

static HMENU load_menu_template(const BYTE *data, DWORD size)
{
    const BYTE *end = data + size;
    if (size < 4)
        return nullptr;
    WORD version = *(const WORD *)data, offset = *(const WORD *)(data + 2);
    const BYTE *items = data + 4 + offset;
    if (version > 1 || items > end)
        return nullptr;
    HMENU menu = CreateMenu();
    if (!menu)
        return nullptr;
    const BYTE *done = version ? parse_menu_ex(items, data, end, menu, 0) : parse_menu(items, end, menu, 0);
    if (!done) {
        DestroyMenu(menu);
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
    return menu;
}

WINUSERAPI HMENU WINAPI LoadMenuW(HINSTANCE instance, LPCWSTR name)
{
    HRSRC res = FindResourceW(instance, name, MAKEINTRESOURCEW(4));
    const BYTE *data = res ? LockResource(LoadResource(instance, res)) : nullptr;
    if (!data)
        return nullptr;
    return load_menu_template(data, SizeofResource(instance, res));
}

WINUSERAPI HMENU WINAPI LoadMenuA(HINSTANCE instance, LPCSTR name)
{
    LPWSTR w = UserAtoW(name);
    HMENU menu = LoadMenuW(instance, w);
    UserFreeConverted(w, name);
    return menu;
}

/* the template has no size, so it is read up to the item that ends it, within 64 KB. */
WINUSERAPI HMENU WINAPI LoadMenuIndirectW(const MENUTEMPLATEW *template)
{
    if (!template) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return load_menu_template((const BYTE *)template, 0x10000);
}

WINUSERAPI HMENU WINAPI LoadMenuIndirectA(const MENUTEMPLATEA *template)
{
    return LoadMenuIndirectW((const MENUTEMPLATEW *)template);
}

WINUSERAPI DWORD WINAPI GetMenuContextHelpId(HMENU menu)
{
    MENUINFO info = {sizeof(info), MIM_HELPID};
    return GetMenuInfo(menu, &info) ? info.dwContextHelpID : 0;
}

WINUSERAPI BOOL WINAPI SetMenuContextHelpId(HMENU menu, DWORD id)
{
    MENUINFO info = {sizeof(info), MIM_HELPID};
    info.dwContextHelpID = id;
    return SetMenuInfo(menu, &info);
}
