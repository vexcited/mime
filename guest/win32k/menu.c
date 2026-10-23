/* menus are drawn and tracked in user32. win32k owns the objects so handles are shared across session threads. */
#include "win32k.h"

typedef struct MENU_ITEM {
    UINT Type;
    UINT State;
    BOOLEAN StateExplicit;
    UINT Id;
    HMENU SubMenu;
    HBITMAP Checked;
    HBITMAP Unchecked;
    HBITMAP Bitmap;
    ULONG_PTR Data;
    PWSTR Text;
    ULONG TextLength;
} MENU_ITEM;

typedef struct MENU {
    USER_OBJECT Header;
    LIST_ENTRY Links;
    HANDLE OwnerPid;
    BOOLEAN Popup;
    WND *SystemOf; /* window that owns this system menu. */
    ULONG Count;
    ULONG Capacity;
    MENU_ITEM *Items;
    DWORD Style;
    UINT MaxHeight;
    HBRUSH Background;
    DWORD HelpId;
    ULONG_PTR MenuData;
} MENU;

static LIST_ENTRY g_menus = {&g_menus, &g_menus};

#define ITEM_STATE_MASK (MFS_GRAYED | MFS_CHECKED | MFS_HILITE | MFS_DEFAULT)
#define ITEM_TYPE_MASK (MFT_BITMAP | MFT_MENUBARBREAK | MFT_MENUBREAK | MFT_OWNERDRAW | MFT_RADIOCHECK | \
                        MFT_SEPARATOR | MFT_RIGHTORDER | MFT_RIGHTJUSTIFY)

static MENU *menu_lookup(HMENU h)
{
    return (MENU *)UserLookup(h, USER_TYPE_MENU);
}

BOOLEAN MenuCommandEnabled(WND *w, UINT command)
{
    if (command == SC_SIZE && (!(w->Style & WS_THICKFRAME) || (w->Style & (WS_MAXIMIZE | WS_MINIMIZE))))
        return FALSE;
    MENU *menu = menu_lookup(w->SystemMenu);
    if (!menu)
        return TRUE;
    for (ULONG i = 0; i < menu->Count; i++)
        if (menu->Items[i].Id == command)
            return !(menu->Items[i].State & (MFS_DISABLED | MFS_GRAYED));
    return FALSE;
}

static MENU *menu_create(BOOLEAN popup)
{
    MENU *m = W32Alloc(sizeof(MENU));
    if (!m)
        return nullptr;
    if (!UserCreateHandle(&m->Header, USER_TYPE_MENU)) {
        W32Free(m);
        return nullptr;
    }
    m->Popup = popup;
    m->OwnerPid = PsGetCurrentProcessId();
    InsertTailList(&g_menus, &m->Links);
    return m;
}

static void free_item(MENU_ITEM *it)
{
    W32Free(it->Text);
    it->Text = nullptr;
    it->TextLength = 0;
}

static void menu_destroy(MENU *m)
{
    if (!m || m->Header.Destroyed)
        return;
    m->Header.Destroyed = TRUE;
    RemoveEntryList(&m->Links);
    for (ULONG i = 0; i < m->Count; i++) {
        MENU_ITEM *it = &m->Items[i];
        if (it->SubMenu)
            menu_destroy(menu_lookup(it->SubMenu));
        free_item(it);
    }
    W32Free(m->Items);
    m->Items = nullptr;
    m->Count = 0;
    if (m->SystemOf && m->SystemOf->SystemMenu == (HMENU)m->Header.Handle)
        m->SystemOf->SystemMenu = nullptr;
    UserDereference(&m->Header);
}

/* the system menu decides whether the caption close button is enabled. */
static void sync_close_button(MENU *m)
{
    WND *w = m->SystemOf;
    if (!w || w->Header.Destroyed)
        return;
    BOOLEAN enabled = FALSE;
    for (ULONG i = 0; i < m->Count; i++)
        if (m->Items[i].Id == SC_CLOSE && !(m->Items[i].Type & MFT_SEPARATOR))
            enabled = !(m->Items[i].State & MFS_GRAYED);
    ULONG old = w->Flags;
    if (enabled)
        w->Flags &= ~WNDF_NOCLOSE;
    else
        w->Flags |= WNDF_NOCLOSE;
    if (old != w->Flags && FrameHasCaption(w->Style, w->ExStyle)) {
        w->Flags |= WNDF_FRAME_DIRTY;
        WndInvalidateRegion(w, nullptr, RDW_FRAME | RDW_INVALIDATE);
    }
}

/* command lookup also searches submenus. */
static MENU_ITEM *find_item(MENU *m, UINT item, BOOLEAN by_position, MENU **owner, ULONG *index, int depth)
{
    if (!m || depth > 16)
        return nullptr;
    if (by_position) {
        if (item >= m->Count)
            return nullptr;
        if (owner)
            *owner = m;
        if (index)
            *index = item;
        return &m->Items[item];
    }
    for (ULONG i = 0; i < m->Count; i++) {
        MENU_ITEM *it = &m->Items[i];
        if (it->SubMenu) {
            MENU_ITEM *found = find_item(menu_lookup(it->SubMenu), item, FALSE, owner, index, depth + 1);
            if (found)
                return found;
            /* a popup item matches its own id only when the command is not in the submenu. */
        }
        if (it->Id == item && !(it->Type & MFT_SEPARATOR && !it->Id)) {
            if (owner)
                *owner = m;
            if (index)
                *index = i;
            return it;
        }
    }
    return nullptr;
}

static BOOLEAN insert_slot(MENU *m, ULONG pos)
{
    if (m->Count == m->Capacity) {
        ULONG cap = m->Capacity ? m->Capacity * 2 : 8;
        MENU_ITEM *items = W32Alloc(cap * sizeof(MENU_ITEM));
        if (!items)
            return FALSE;
        if (m->Count)
            memcpy(items, m->Items, m->Count * sizeof(MENU_ITEM));
        W32Free(m->Items);
        m->Items = items;
        m->Capacity = cap;
    }
    if (pos > m->Count)
        pos = m->Count;
    memmove(&m->Items[pos + 1], &m->Items[pos], (m->Count - pos) * sizeof(MENU_ITEM));
    memset(&m->Items[pos], 0, sizeof(MENU_ITEM));
    m->Count++;
    return TRUE;
}

static void remove_slot(MENU *m, ULONG pos, BOOLEAN destroy_submenu)
{
    MENU_ITEM *it = &m->Items[pos];
    if (destroy_submenu && it->SubMenu)
        menu_destroy(menu_lookup(it->SubMenu));
    free_item(it);
    memmove(&m->Items[pos], &m->Items[pos + 1], (m->Count - pos - 1) * sizeof(MENU_ITEM));
    m->Count--;
}

static void set_text(MENU_ITEM *it, PWSTR text, SIZE_T chars)
{
    free_item(it);
    it->Text = text;
    it->TextLength = (ULONG)chars;
}

/* applies the fields of a MENUITEMINFOW selected by fMask. text is owned by the item afterwards. */
static BOOLEAN apply_info(MENU *m, MENU_ITEM *it, const MENUITEMINFOW *mii, PWSTR text, SIZE_T chars)
{
    if (mii->fMask & MIIM_TYPE) {
        it->Type = (it->Type & ~ITEM_TYPE_MASK) | (mii->fType & ITEM_TYPE_MASK);
        if (mii->fType & MFT_BITMAP) {
            it->Bitmap = (HBITMAP)(ULONG_PTR)(DWORD)(ULONG_PTR)mii->dwTypeData;
            set_text(it, nullptr, 0);
            W32Free(text);
            text = nullptr;
        } else if (mii->fType & MFT_SEPARATOR) {
            set_text(it, nullptr, 0);
        } else {
            set_text(it, text, chars);
            text = nullptr;
        }
    }
    if (mii->fMask & MIIM_FTYPE)
        it->Type = mii->fType & ITEM_TYPE_MASK & ~MFT_BITMAP;
    if (mii->fMask & MIIM_STRING) {
        set_text(it, text, chars);
        text = nullptr;
    }
    if (mii->fMask & MIIM_BITMAP)
        it->Bitmap = mii->hbmpItem;
    if (mii->fMask & MIIM_STATE) {
        it->State = mii->fState & ITEM_STATE_MASK;
        it->StateExplicit = TRUE;
        /* one default item per menu. */
        if (it->State & MFS_DEFAULT)
            for (ULONG i = 0; i < m->Count; i++)
                if (&m->Items[i] != it)
                    m->Items[i].State &= ~MFS_DEFAULT;
    }
    if (mii->fMask & MIIM_ID)
        it->Id = mii->wID;
    if (mii->fMask & MIIM_SUBMENU) {
        MENU *sub = mii->hSubMenu ? menu_lookup(mii->hSubMenu) : nullptr;
        if (mii->hSubMenu && (!sub || sub == m)) {
            W32Free(text);
            return FALSE;
        }
        it->SubMenu = mii->hSubMenu;
        if (sub)
            sub->Popup = TRUE;
    }
    if (mii->fMask & MIIM_CHECKMARKS) {
        it->Checked = mii->hbmpChecked;
        it->Unchecked = mii->hbmpUnchecked;
    }
    if (mii->fMask & MIIM_DATA)
        it->Data = mii->dwItemData;
    W32Free(text);
    return TRUE;
}

/* cleanup. */

void MenuFreeProcess(W32PROCESS *p)
{
    PLIST_ENTRY e = g_menus.Flink;
    while (e != &g_menus) {
        MENU *m = CONTAINING_RECORD(e, MENU, Links);
        e = e->Flink;
        if (m->OwnerPid == p->Pid && !m->SystemOf) {
            /* submenus may be next in the list; restart after a recursive destroy. */
            menu_destroy(m);
            e = g_menus.Flink;
        }
    }
}

void MenuWindowDestroyed(WND *w)
{
    if (w->SystemMenu) {
        MENU *m = menu_lookup(w->SystemMenu);
        w->SystemMenu = nullptr;
        if (m) {
            m->SystemOf = nullptr;
            menu_destroy(m);
        }
    }
    if (!(w->Style & WS_CHILD) && w->Id) {
        MENU *m = menu_lookup((HMENU)w->Id);
        if (m)
            menu_destroy(m);
        w->Id = 0;
    }
}

/* the system menu. */

typedef struct SYSMENU_ENTRY {
    UINT Id;
    PCWSTR Text;
} SYSMENU_ENTRY;

/* default system menu strings; tabs separate accelerator text. */
static const SYSMENU_ENTRY g_sysmenu[] = {
    {SC_RESTORE, L"&Restore"}, {SC_MOVE, L"&Move"},          {SC_SIZE, L"&Size"}, {SC_MINIMIZE, L"Mi&nimize"},
    {SC_MAXIMIZE, L"Ma&ximize"}, {0, nullptr},                  {SC_CLOSE, L"&Close\tAlt+F4"},
};

static void append_item(MENU *m, UINT id, PCWSTR text, UINT type, UINT state)
{
    if (!insert_slot(m, m->Count))
        return;
    MENU_ITEM *it = &m->Items[m->Count - 1];
    it->Id = id;
    it->Type = type;
    it->State = state;
    if (text) {
        it->TextLength = (ULONG)wcslen(text);
        it->Text = W32StrDup(text, it->TextLength);
    }
}

void MenuRefreshWindowState(WND *w)
{
    MENU *m = menu_lookup(w->SystemMenu);
    if (!m)
        return;
    for (ULONG i = 0; i < m->Count; i++) {
        MENU_ITEM *it = &m->Items[i];
        if (it->StateExplicit)
            continue;
        BOOLEAN grey = FALSE;
        switch (it->Id) {
        case SC_MINIMIZE:
            grey = !(w->Style & WS_MINIMIZEBOX) || (w->Style & WS_MINIMIZE);
            break;
        case SC_MAXIMIZE:
            grey = !(w->Style & WS_MAXIMIZEBOX) || (w->Style & WS_MAXIMIZE);
            break;
        case SC_RESTORE:
            grey = !(w->Style & (WS_MAXIMIZE | WS_MINIMIZE));
            break;
        case SC_SIZE:
            grey = !(w->Style & WS_THICKFRAME) || (w->Style & (WS_MAXIMIZE | WS_MINIMIZE));
            break;
        case SC_MOVE:
            grey = (w->Style & WS_MAXIMIZE) != 0;
            break;
        case SC_CLOSE:
            grey = (w->Flags & WNDF_NOCLOSE) != 0;
            break;
        }
        it->State = (it->State & ~MFS_GRAYED) | (grey ? MFS_GRAYED : 0);
    }
}

static MENU *system_menu(WND *w, BOOLEAN create)
{
    MENU *m = w->SystemMenu ? menu_lookup(w->SystemMenu) : nullptr;
    if (m || !create)
        return m;
    m = menu_create(TRUE);
    if (!m)
        return nullptr;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_sysmenu); i++)
        append_item(m, g_sysmenu[i].Id, g_sysmenu[i].Text, g_sysmenu[i].Text ? MFT_STRING : MFT_SEPARATOR,
                    g_sysmenu[i].Id == SC_CLOSE ? MFS_DEFAULT : 0);
    m->SystemOf = w;
    m->OwnerPid = w->Thread ? w->Thread->Process->Pid : m->OwnerPid;
    w->SystemMenu = (HMENU)m->Header.Handle;
    MenuRefreshWindowState(w);
    return m;
}

/* services. */

HMENU NTAPI NtUserCreateMenu(void)
{
    UserEnter();
    MENU *m = menu_create(FALSE);
    HMENU h = m ? (HMENU)m->Header.Handle : nullptr;
    UserLeave();
    if (!h)
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return h;
}

HMENU NTAPI NtUserCreatePopupMenu(void)
{
    UserEnter();
    MENU *m = menu_create(TRUE);
    HMENU h = m ? (HMENU)m->Header.Handle : nullptr;
    UserLeave();
    if (!h)
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return h;
}

BOOL NTAPI NtUserDestroyMenu(HMENU menu)
{
    UserEnter();
    MENU *m = menu_lookup(menu);
    if (m && m->SystemOf)
        m->SystemOf->SystemMenu = nullptr;
    if (m) {
        m->SystemOf = nullptr;
        menu_destroy(m);
    }
    UserLeave();
    if (!m)
        W32SetLastError(ERROR_INVALID_MENU_HANDLE);
    return m != nullptr;
}

BOOL NTAPI NtUserThunkedMenuItemInfo(HMENU menu, UINT item, BOOL by_position, BOOL insert, LPMENUITEMINFOW user_info,
                                     PUNICODE_STRING user_text)
{
    MENUITEMINFOW mii;
    memset(&mii, 0, sizeof(mii));
    if (!user_info || !NT_SUCCESS(MimeCopyFromUser(&mii, user_info, offsetof(MENUITEMINFOW, hbmpItem)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    if (mii.cbSize >= sizeof(MENUITEMINFOW))
        MimeCopyFromUser(&mii, user_info, sizeof(mii));
    else
        mii.fMask &= ~(MIIM_BITMAP | MIIM_STRING | MIIM_FTYPE);
    PWSTR text = nullptr;
    SIZE_T chars = 0;
    if (user_text && !NT_SUCCESS(W32CaptureString(user_text, &text, &chars))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    UserEnter();
    MENU *m = menu_lookup(menu);
    BOOL ok = FALSE;
    ULONG error = 0;
    if (!m) {
        error = ERROR_INVALID_MENU_HANDLE;
        W32Free(text);
    } else if (insert) {
        ULONG pos = m->Count;
        MENU *owner = m;
        if (by_position) {
            pos = item > m->Count ? m->Count : item;
        } else if (item != (UINT)-1) {
            ULONG index;
            if (find_item(m, item, FALSE, &owner, &index, 0))
                pos = index;
            else
                owner = m;
        }
        if (insert_slot(owner, pos)) {
            ok = apply_info(owner, &owner->Items[pos], &mii, text, chars);
            if (!ok) {
                remove_slot(owner, pos, FALSE);
                error = ERROR_INVALID_PARAMETER;
            }
        } else {
            error = ERROR_NOT_ENOUGH_MEMORY;
            W32Free(text);
        }
        if (ok && owner->SystemOf)
            sync_close_button(owner);
    } else {
        MENU *owner = nullptr;
        MENU_ITEM *it = find_item(m, item, by_position, &owner, nullptr, 0);
        if (it) {
            ok = apply_info(owner, it, &mii, text, chars);
            if (!ok)
                error = ERROR_INVALID_PARAMETER;
            else if (owner->SystemOf)
                sync_close_button(owner);
        } else {
            error = ERROR_MENU_ITEM_NOT_FOUND;
            W32Free(text);
        }
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    return ok;
}

BOOL NTAPI NtUserMimeGetMenuItemInfo(HMENU menu, UINT item, BOOL by_position, LPMENUITEMINFOW user_info)
{
    MENUITEMINFOW mii;
    if (!user_info || !NT_SUCCESS(MimeCopyFromUser(&mii, user_info, offsetof(MENUITEMINFOW, hbmpItem)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    BOOLEAN full = mii.cbSize >= sizeof(MENUITEMINFOW);
    if (full)
        MimeCopyFromUser(&mii, user_info, sizeof(mii));
    UserEnter();
    MENU *m = menu_lookup(menu);
    MENU_ITEM *it = m ? find_item(m, item, by_position, nullptr, nullptr, 0) : nullptr;
    PWSTR text = nullptr;
    ULONG length = 0;
    BOOLEAN want_text = FALSE;
    if (it) {
        if (mii.fMask & (MIIM_TYPE | MIIM_FTYPE))
            mii.fType = it->Type | (it->Bitmap && (mii.fMask & MIIM_TYPE) && !it->Text ? MFT_BITMAP : 0);
        if (mii.fMask & MIIM_STATE)
            mii.fState = it->State;
        if (mii.fMask & MIIM_ID)
            mii.wID = it->Id;
        if (mii.fMask & MIIM_SUBMENU)
            mii.hSubMenu = it->SubMenu;
        if (mii.fMask & MIIM_CHECKMARKS) {
            mii.hbmpChecked = it->Checked;
            mii.hbmpUnchecked = it->Unchecked;
        }
        if (mii.fMask & MIIM_DATA)
            mii.dwItemData = it->Data;
        if (mii.fMask & MIIM_BITMAP)
            mii.hbmpItem = it->Bitmap;
        if ((mii.fMask & MIIM_TYPE) && (mii.fType & MFT_BITMAP)) {
            mii.dwTypeData = (LPWSTR)it->Bitmap;
        } else if ((mii.fMask & MIIM_STRING) ||
                   ((mii.fMask & MIIM_TYPE) && !(it->Type & (MFT_SEPARATOR | MFT_OWNERDRAW)))) {
            want_text = TRUE;
            length = it->TextLength;
            if (length && mii.dwTypeData && mii.cch)
                text = W32StrDup(it->Text, length);
        }
    }
    UserLeave();
    if (!it) {
        W32SetLastError(m ? ERROR_MENU_ITEM_NOT_FOUND : ERROR_INVALID_MENU_HANDLE);
        return FALSE;
    }
    BOOL ok = TRUE;
    if (want_text) {
        if (!mii.dwTypeData || !mii.cch) {
            mii.cch = length;
        } else {
            ULONG n = length < mii.cch - 1 ? length : mii.cch - 1;
            WCHAR zero = 0;
            if ((n && !NT_SUCCESS(MimeCopyToUser(mii.dwTypeData, text, n * sizeof(WCHAR)))) ||
                !NT_SUCCESS(MimeCopyToUser(mii.dwTypeData + n, &zero, sizeof(zero))))
                ok = FALSE;
            mii.cch = n;
        }
    }
    W32Free(text);
    if (ok)
        ok = NT_SUCCESS(MimeCopyToUser(user_info, &mii, full ? sizeof(mii) : offsetof(MENUITEMINFOW, hbmpItem)));
    if (!ok)
        W32SetLastError(ERROR_NOACCESS);
    return ok;
}

ULONG_PTR NTAPI NtUserMimeCallMenu(HMENU menu, ULONG_PTR param, ULONG routine)
{
    UserEnter();
    MENU *m = menu_lookup(menu);
    ULONG_PTR r = 0;
    ULONG error = m ? 0 : ERROR_INVALID_MENU_HANDLE;
    if (m) {
        switch (routine) {
        case MIME_MENU_ISMENU:
            r = 1;
            break;
        case MIME_MENU_ITEMCOUNT:
            r = m->Count;
            break;
        case MIME_MENU_SYSTEMWINDOW:
            r = (ULONG_PTR)WndHandle(m->SystemOf);
            break;
        case MIME_MENU_DEFAULTITEM: {
            BOOLEAN by_position = (param & 0x10000) != 0;
            UINT flags = (UINT)(param & 0xFFFF);
            r = (ULONG_PTR)(UINT)-1;
            for (ULONG i = 0; i < m->Count; i++) {
                MENU_ITEM *it = &m->Items[i];
                if (!(it->State & MFS_DEFAULT))
                    continue;
                if ((it->State & MFS_GRAYED) && !(flags & 1 /* GMDI_USEDISABLED */))
                    break;
                MENU *sub = it->SubMenu ? menu_lookup(it->SubMenu) : nullptr;
                if (sub && (flags & 2 /* GMDI_GOINTOPOPUPS */)) {
                    /* the user lock is recursive. */
                    ULONG_PTR inner = NtUserMimeCallMenu(it->SubMenu, param, routine);
                    if (inner != (ULONG_PTR)(UINT)-1) {
                        r = inner;
                        break;
                    }
                }
                r = by_position ? i : it->Id;
                break;
            }
            break;
        }
        case MIME_MENU_GETINFO: {
            MENUINFO mi;
            if (!NT_SUCCESS(MimeCopyFromUser(&mi, (PVOID)param, sizeof(mi))) || mi.cbSize != sizeof(mi)) {
                error = ERROR_INVALID_PARAMETER;
                break;
            }
            if (mi.fMask & MIM_STYLE)
                mi.dwStyle = m->Style;
            if (mi.fMask & MIM_MAXHEIGHT)
                mi.cyMax = m->MaxHeight;
            if (mi.fMask & MIM_BACKGROUND)
                mi.hbrBack = m->Background;
            if (mi.fMask & MIM_HELPID)
                mi.dwContextHelpID = m->HelpId;
            if (mi.fMask & MIM_MENUDATA)
                mi.dwMenuData = m->MenuData;
            r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &mi, sizeof(mi)));
            if (!r)
                error = ERROR_NOACCESS;
            break;
        }
        }
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    return r;
}

static void apply_menu_info(MENU *m, const MENUINFO *mi, int depth)
{
    if (mi->fMask & MIM_STYLE)
        m->Style = mi->dwStyle;
    if (mi->fMask & MIM_MAXHEIGHT)
        m->MaxHeight = mi->cyMax;
    if (mi->fMask & MIM_BACKGROUND)
        m->Background = mi->hbrBack;
    if (mi->fMask & MIM_HELPID)
        m->HelpId = mi->dwContextHelpID;
    if (mi->fMask & MIM_MENUDATA)
        m->MenuData = mi->dwMenuData;
    if ((mi->fMask & MIM_APPLYTOSUBMENUS) && depth < 16)
        for (ULONG i = 0; i < m->Count; i++) {
            MENU *sub = m->Items[i].SubMenu ? menu_lookup(m->Items[i].SubMenu) : nullptr;
            if (sub)
                apply_menu_info(sub, mi, depth + 1);
        }
}

BOOL NTAPI NtUserThunkedMenuInfo(HMENU menu, LPCMENUINFO user_info)
{
    MENUINFO mi;
    if (!user_info || !NT_SUCCESS(MimeCopyFromUser(&mi, user_info, sizeof(mi))) || mi.cbSize != sizeof(mi)) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    MENU *m = menu_lookup(menu);
    if (m)
        apply_menu_info(m, &mi, 0);
    UserLeave();
    if (!m)
        W32SetLastError(ERROR_INVALID_MENU_HANDLE);
    return m != nullptr;
}

static BOOL remove_item(HMENU menu, UINT position, UINT flags, BOOLEAN destroy)
{
    UserEnter();
    MENU *m = menu_lookup(menu);
    MENU *owner = nullptr;
    ULONG index = 0;
    BOOL ok = FALSE;
    if (m && find_item(m, position, (flags & MF_BYPOSITION) != 0, &owner, &index, 0)) {
        remove_slot(owner, index, destroy);
        if (owner->SystemOf)
            sync_close_button(owner);
        ok = TRUE;
    }
    UserLeave();
    if (!ok)
        W32SetLastError(m ? ERROR_MENU_ITEM_NOT_FOUND : ERROR_INVALID_MENU_HANDLE);
    return ok;
}

BOOL NTAPI NtUserDeleteMenu(HMENU menu, UINT position, UINT flags)
{
    return remove_item(menu, position, flags, TRUE);
}

BOOL NTAPI NtUserRemoveMenu(HMENU menu, UINT position, UINT flags)
{
    return remove_item(menu, position, flags, FALSE);
}

UINT NTAPI NtUserEnableMenuItem(HMENU menu, UINT id, UINT flags)
{
    UserEnter();
    MENU *m = menu_lookup(menu);
    MENU *owner = nullptr;
    MENU_ITEM *it = m ? find_item(m, id, (flags & MF_BYPOSITION) != 0, &owner, nullptr, 0) : nullptr;
    UINT old = (UINT)-1;
    if (it) {
        old = it->State & (MF_GRAYED | MF_DISABLED);
        it->State = (it->State & ~(MF_GRAYED | MF_DISABLED)) | (flags & (MF_GRAYED | MF_DISABLED));
        it->StateExplicit = TRUE;
        if (owner->SystemOf)
            sync_close_button(owner);
    }
    UserLeave();
    return old;
}

DWORD NTAPI NtUserCheckMenuItem(HMENU menu, UINT id, UINT flags)
{
    UserEnter();
    MENU *m = menu_lookup(menu);
    MENU_ITEM *it = m ? find_item(m, id, (flags & MF_BYPOSITION) != 0, nullptr, nullptr, 0) : nullptr;
    DWORD old = (DWORD)-1;
    if (it) {
        old = it->State & MF_CHECKED;
        it->State = (it->State & ~MF_CHECKED) | (flags & MF_CHECKED);
    }
    UserLeave();
    return old;
}

BOOL NTAPI NtUserSetMenuDefaultItem(HMENU menu, UINT item, UINT by_position)
{
    UserEnter();
    MENU *m = menu_lookup(menu);
    BOOL ok = FALSE;
    if (m) {
        if (item == (UINT)-1) {
            for (ULONG i = 0; i < m->Count; i++)
                m->Items[i].State &= ~MFS_DEFAULT;
            ok = TRUE;
        } else {
            /* only this menu, excluding submenus. */
            for (ULONG i = 0; i < m->Count; i++) {
                BOOLEAN match = by_position ? i == item : m->Items[i].Id == item;
                if (match && !ok) {
                    m->Items[i].State |= MFS_DEFAULT;
                    ok = TRUE;
                } else {
                    m->Items[i].State &= ~MFS_DEFAULT;
                }
            }
        }
    }
    UserLeave();
    if (!ok)
        W32SetLastError(m ? ERROR_MENU_ITEM_NOT_FOUND : ERROR_INVALID_MENU_HANDLE);
    return ok;
}

BOOL NTAPI NtUserSetMenu(HWND hwnd, HMENU menu, BOOL repaint)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    MENU *m = menu ? menu_lookup(menu) : nullptr;
    ULONG error = 0;
    if (!w)
        error = ERROR_INVALID_WINDOW_HANDLE;
    else if (w->Style & WS_CHILD)
        error = ERROR_INVALID_PARAMETER;
    else if (menu && !m)
        error = ERROR_INVALID_MENU_HANDLE;
    if (!error) {
        /* menu bars are not drawn yet: the menu is only recorded for GetMenu. */
        w->Id = (UINT_PTR)menu;
        if (m)
            m->Popup = FALSE;
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    (void)repaint;
    return !error;
}

HMENU NTAPI NtUserGetSystemMenu(HWND hwnd, BOOL revert)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    HMENU h = nullptr;
    if (w) {
        if (revert) {
            MENU *m = system_menu(w, FALSE);
            if (m) {
                w->SystemMenu = nullptr;
                m->SystemOf = nullptr;
                menu_destroy(m);
            }
            if (w->Flags & WNDF_NOCLOSE) {
                w->Flags &= ~WNDF_NOCLOSE;
                if (FrameHasCaption(w->Style, w->ExStyle))
                    WndInvalidateRegion(w, nullptr, RDW_FRAME | RDW_INVALIDATE);
            }
        } else if (w->Style & WS_SYSMENU || !(w->Style & WS_CHILD)) {
            MENU *m = system_menu(w, TRUE);
            h = m ? (HMENU)m->Header.Handle : nullptr;
        }
    }
    UserLeave();
    if (!w)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return h;
}

BOOL NTAPI NtUserDrawMenuBar(HWND hwnd)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    UserLeave();
    if (!w)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return w != nullptr;
}

BOOL NTAPI NtUserEndMenu(void)
{
    /* menu tracking runs in user32, which ends its own loop. */
    return TRUE;
}

/* menus for the host. */

static void flatten_menu(MENU *menu, ULONG parent, MimeTrayItem *items, ULONG *ids, ULONG *count, ULONG max, ULONG depth)
{
    for (ULONG i = 0; menu && i < menu->Count && *count < max; i++) {
        const MENU_ITEM *it = &menu->Items[i];
        ULONG index = (*count)++;
        MimeTrayItem *out = &items[index];
        memset(out, 0, sizeof(*out));
        out->parent = parent;
        if (it->Type & MFT_SEPARATOR)
            out->flags |= MIME_TRAY_ITEM_SEPARATOR;
        if (it->State & (MFS_GRAYED | MFS_DISABLED))
            out->flags |= MIME_TRAY_ITEM_DISABLED;
        if (it->State & MFS_CHECKED)
            out->flags |= MIME_TRAY_ITEM_CHECKED;
        if (it->Type & MFT_RADIOCHECK)
            out->flags |= MIME_TRAY_ITEM_RADIO;
        if (it->State & MFS_DEFAULT)
            out->flags |= MIME_TRAY_ITEM_DEFAULT;
        for (ULONG c = 0; it->Text && c < it->TextLength && c < RTL_NUMBER_OF(out->label) - 1; c++)
            out->label[c] = it->Text[c];
        ids[index] = it->Id;
        if (it->SubMenu && depth < 8)
            flatten_menu(menu_lookup(it->SubMenu), index + 1, items, ids, count, max, depth + 1);
    }
}

/* lists items and submenus in host display order, retaining application command ids. */
ULONG MenuFlattenForHost(HMENU handle, MimeTrayItem *items, ULONG *ids, ULONG max)
{
    ULONG count = 0;
    flatten_menu(menu_lookup(handle), 0, items, ids, &count, max, 0);
    return count;
}
