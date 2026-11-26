#include "explorerframe_private.h"
#include <mime/win32/commctrl.h>
#include <mime/win32/shellapi.h>
#include <mime/win32/shlfolder.h>

#define TREE_ID 100

extern HINSTANCE g_explorerframe;
#define MAX_SINKS 8
/* windows tells the events of a selection change after the change, once the caller returned. */
#define WM_NSTC_SELECTION (WM_USER + 0x200)

static const GUID IID_INameSpaceTreeControlFlagsPrivate = {
    0x536E14ED, 0x11F6, 0x02DB, {0x49, 0xB8, 0x63, 0xD4, 0x85, 0x65, 0xD0, 0xCE}};
static const GUID IID_ICreateNewFolderPriv = {0xEE06D98F, 0x6542, 0x4655, {0x9A, 0x9C, 0x70, 0x5E, 0x10, 0xB7, 0x82, 0x9C}};

/* NSTCFLAGSPRIVATE: the folder browser asks for its own window class. */
#define NSTCFP_BROWSEFORFOLDER 0x1

/* the two private interfaces of CNscTree that shell32's folder browser uses, one method each. */
typedef struct IFlagsPrivate IFlagsPrivate;
#define IFlagsPrivate_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetPrivateFlags)(T * This, DWORD flags);
MIME_COM_INTERFACE(IFlagsPrivate)

typedef struct INewFolderPrivate INewFolderPrivate;
#define INewFolderPrivate_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateNewFolder)(T * This);
MIME_COM_INTERFACE(INewFolderPrivate)

typedef struct _ROOT {
    struct _ROOT *Next;
    PIDLIST_ABSOLUTE Pidl;
    SHCONTF Flags;
    NSTCROOTSTYLE Style;
    IShellItemFilter *Filter;
    /* the item of the root, the tree's root when the root is hidden. */
    HTREEITEM Item;
} ROOT;

typedef struct _NODE {
    PIDLIST_ABSOLUTE Pidl;
    ROOT *Root;
    BOOL Filled;
    SFGAOF Attributes;
} NODE;

typedef struct _NSTC {
    INameSpaceTreeControl2 Control;
    IOleWindow Window;
    IObjectWithSite Site;
    IFolderFilterSite FilterSite;
    IFlagsPrivate Flags;
    INewFolderPrivate NewFolder;
    LONG Refs;
    HWND Host, Tree, Name;
    NSTCSTYLE Style;
    DWORD PrivateFlags;
    ROOT *Roots;
    IFolderFilter *Filter;
    IUnknown *SiteObject;
    INameSpaceTreeControlEvents *Sinks[MAX_SINKS];
    HFONT Font;
    BOOL SelectionPending, Subclassed;
} NSTC;

static NSTC *from_control(INameSpaceTreeControl2 *p)
{
    return CONTAINING_RECORD(p, NSTC, Control);
}

/* ------------------------------------------------------------------ helpers */

static IShellFolder *bind_folder(PCIDLIST_ABSOLUTE pidl)
{
    IShellFolder *desktop = nullptr, *folder = nullptr;
    if (FAILED(SHGetDesktopFolder(&desktop)))
        return nullptr;
    if (!pidl || !pidl->mkid.cb)
        return desktop;
    if (FAILED(desktop->lpVtbl->BindToObject(desktop, pidl, nullptr, &IID_IShellFolder, (void **)&folder)))
        folder = nullptr;
    desktop->lpVtbl->Release(desktop);
    return folder;
}

static BOOL strret_text(STRRET *s, PCUITEMID_CHILD child, LPWSTR text, int chars)
{
    text[0] = 0;
    switch (s->uType) {
    case STRRET_WSTR:
        if (!s->pOleStr)
            return FALSE;
        lstrcpynW(text, s->pOleStr, chars);
        CoTaskMemFree(s->pOleStr);
        return TRUE;
    case STRRET_OFFSET:
        MultiByteToWideChar(CP_ACP, 0, (LPCSTR)child + s->uOffset, -1, text, chars);
        return TRUE;
    case STRRET_CSTR:
        MultiByteToWideChar(CP_ACP, 0, s->cStr, -1, text, chars);
        return TRUE;
    }
    return FALSE;
}

static UINT id_count(PCUIDLIST_RELATIVE pidl)
{
    UINT n = 0;
    for (const SHITEMID *id = &pidl->mkid; id->cb; id = (const SHITEMID *)((const BYTE *)id + id->cb))
        n++;
    return n;
}

/* the first count ids of a list. */
static PIDLIST_ABSOLUTE list_head(PCIDLIST_ABSOLUTE pidl, UINT count)
{
    SIZE_T size = 0;
    const SHITEMID *id = &pidl->mkid;
    for (UINT i = 0; i < count && id->cb; i++) {
        size += id->cb;
        id = (const SHITEMID *)((const BYTE *)id + id->cb);
    }
    PIDLIST_ABSOLUTE head = CoTaskMemAlloc(size + sizeof(USHORT));
    if (!head)
        return nullptr;
    memcpy(head, pidl, size);
    *(USHORT *)((BYTE *)head + size) = 0;
    return head;
}

/* the folder's own comparison, which tells delegate and real items of one place apart as windows does. */
static BOOL same_place(PCIDLIST_ABSOLUTE a, PCIDLIST_ABSOLUTE b)
{
    if (!a->mkid.cb || !b->mkid.cb)
        return !a->mkid.cb && !b->mkid.cb;
    return ILIsEqual(a, b);
}

static BOOL is_prefix(PCIDLIST_ABSOLUTE prefix, PCIDLIST_ABSOLUTE pidl)
{
    UINT n = id_count(prefix);
    if (n > id_count(pidl))
        return FALSE;
    PIDLIST_ABSOLUTE head = list_head(pidl, n);
    if (!head)
        return FALSE;
    BOOL same = same_place(prefix, head);
    CoTaskMemFree(head);
    return same;
}

static NODE *node_of(NSTC *t, HTREEITEM item)
{
    if (!item || item == TVI_ROOT)
        return nullptr;
    TVITEMW tv = {.mask = TVIF_PARAM, .hItem = item};
    if (!SendMessageW(t->Tree, TVM_GETITEMW, 0, (LPARAM)&tv))
        return nullptr;
    return (NODE *)tv.lParam;
}

static HTREEITEM tree_next(NSTC *t, HTREEITEM item, UINT code)
{
    return (HTREEITEM)SendMessageW(t->Tree, TVM_GETNEXTITEM, code, (LPARAM)item);
}

static HRESULT item_of(NSTC *t, HTREEITEM item, IShellItem **out)
{
    NODE *n = node_of(t, item);
    *out = nullptr;
    if (!n)
        return E_INVALIDARG;
    return SHCreateItemFromIDList(n->Pidl, &IID_IShellItem, (void **)out);
}

/* ------------------------------------------------------------------ events */

static void fire_item(NSTC *t, HTREEITEM item, int which)
{
    IShellItem *si;
    if (FAILED(item_of(t, item, &si)))
        return;
    t->Control.lpVtbl->AddRef(&t->Control);
    for (int i = 0; i < MAX_SINKS; i++) {
        INameSpaceTreeControlEvents *e = t->Sinks[i];
        if (!e)
            continue;
        switch (which) {
        case 0:
            e->lpVtbl->OnBeforeExpand(e, si);
            break;
        case 1:
            e->lpVtbl->OnAfterExpand(e, si);
            break;
        case 2:
            e->lpVtbl->OnBeginLabelEdit(e, si);
            break;
        case 3:
            e->lpVtbl->OnEndLabelEdit(e, si);
            break;
        }
    }
    si->lpVtbl->Release(si);
    t->Control.lpVtbl->Release(&t->Control);
}

static void fire_selection(NSTC *t)
{
    t->SelectionPending = FALSE;
    IShellItem *si;
    if (FAILED(item_of(t, tree_next(t, nullptr, TVGN_CARET), &si)))
        return;
    IShellItemArray *array = nullptr;
    if (SUCCEEDED(SHCreateShellItemArrayFromShellItem(si, &IID_IShellItemArray, (void **)&array))) {
        t->Control.lpVtbl->AddRef(&t->Control);
        for (int i = 0; i < MAX_SINKS; i++)
            if (t->Sinks[i])
                t->Sinks[i]->lpVtbl->OnSelectionChanged(t->Sinks[i], array);
        t->Control.lpVtbl->Release(&t->Control);
        array->lpVtbl->Release(array);
    }
    si->lpVtbl->Release(si);
}

static void queue_selection(NSTC *t)
{
    if (t->SelectionPending)
        return;
    t->SelectionPending = TRUE;
    PostMessageW(t->Host, WM_NSTC_SELECTION, 0, 0);
}

/* ------------------------------------------------------------------ items */

static HTREEITEM insert_node(NSTC *t, HTREEITEM parent, HTREEITEM after, PIDLIST_ABSOLUTE pidl, ROOT *root,
                             IShellFolder *folder, PCUITEMID_CHILD child)
{
    NODE *n = CoTaskMemAlloc(sizeof(*n));
    if (!n) {
        ILFree(pidl);
        return nullptr;
    }
    n->Pidl = pidl;
    n->Root = root;
    n->Filled = FALSE;
    n->Attributes = SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_CANRENAME | SFGAO_FILESYSTEM;
    WCHAR name[MAX_PATH] = {0};
    STRRET s;
    if (folder && child) {
        if (FAILED(folder->lpVtbl->GetAttributesOf(folder, 1, &child, &n->Attributes)))
            n->Attributes = 0;
        if (SUCCEEDED(folder->lpVtbl->GetDisplayNameOf(folder, child, SHGDN_INFOLDER, &s)))
            strret_text(&s, child, name, MAX_PATH);
    }
    SHFILEINFOW info = {0};
    UINT flags = SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
    if (!name[0])
        flags |= SHGFI_DISPLAYNAME;
    if (!SHGetFileInfoW((LPCWSTR)pidl, 0, &info, sizeof(info), flags))
        info.iIcon = 0;
    if (!name[0])
        lstrcpynW(name, info.szDisplayName, MAX_PATH);
    if (!folder) {
        /* a root asks its parent folder for its attributes. */
        PCUITEMID_CHILD last = nullptr;
        IShellFolder *parent_folder = nullptr;
        if (pidl->mkid.cb && SUCCEEDED(SHBindToParent(pidl, &IID_IShellFolder, (void **)&parent_folder, &last))) {
            SFGAOF a = SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_CANRENAME | SFGAO_FILESYSTEM;
            if (SUCCEEDED(parent_folder->lpVtbl->GetAttributesOf(parent_folder, 1, &last, &a)))
                n->Attributes = a;
            parent_folder->lpVtbl->Release(parent_folder);
        }
    }
    TVINSERTSTRUCTW insert = {0};
    insert.hParent = parent;
    insert.hInsertAfter = after;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
    insert.item.pszText = name;
    insert.item.iImage = info.iIcon;
    insert.item.iSelectedImage = info.iIcon;
    insert.item.lParam = (LPARAM)n;
    insert.item.cChildren = (n->Attributes & SFGAO_FOLDER) && (n->Attributes & SFGAO_HASSUBFOLDER) ? 1 : 0;
    HTREEITEM item = (HTREEITEM)SendMessageW(t->Tree, TVM_INSERTITEMW, 0, (LPARAM)&insert);
    if (!item) {
        ILFree(n->Pidl);
        CoTaskMemFree(n);
    }
    return item;
}

typedef struct _CHILD {
    PITEMID_CHILD Id;
} CHILD;

/* the children of a folder in the folder's own order, filtered by the program's filters. */
static void fill(NSTC *t, HTREEITEM item, NODE *n, HTREEITEM parent_item)
{
    if (n->Filled)
        return;
    n->Filled = TRUE;
    IShellFolder *folder = bind_folder(n->Pidl);
    if (!folder)
        return;
    SHCONTF flags = n->Root->Flags;
    HWND owner = t->Host;
    if (t->Filter)
        t->Filter->lpVtbl->GetEnumFlags(t->Filter, folder, n->Pidl, &owner, &flags);
    if (n->Root->Filter) {
        IShellItem *si;
        if (SUCCEEDED(SHCreateItemFromIDList(n->Pidl, &IID_IShellItem, (void **)&si))) {
            n->Root->Filter->lpVtbl->GetEnumFlagsForItem(n->Root->Filter, si, &flags);
            si->lpVtbl->Release(si);
        }
    }
    IEnumIDList *e = nullptr;
    CHILD *children = nullptr;
    UINT count = 0, capacity = 0;
    if (folder->lpVtbl->EnumObjects(folder, owner, flags, &e) == S_OK && e) {
        PITEMID_CHILD id;
        while (e->lpVtbl->Next(e, 1, &id, nullptr) == S_OK) {
            if (t->Filter && t->Filter->lpVtbl->ShouldShow(t->Filter, folder, n->Pidl, id) != S_OK) {
                CoTaskMemFree(id);
                continue;
            }
            if (count == capacity) {
                UINT grown = capacity ? capacity * 2 : 32;
                CHILD *bigger = CoTaskMemRealloc(children, grown * sizeof(*children));
                if (!bigger) {
                    CoTaskMemFree(id);
                    break;
                }
                children = bigger;
                capacity = grown;
            }
            children[count++].Id = id;
        }
        e->lpVtbl->Release(e);
    }
    /* an insertion sort on the folder's comparison keeps the order it gives equal items. */
    for (UINT i = 1; i < count; i++) {
        CHILD held = children[i];
        UINT j = i;
        for (; j > 0; j--) {
            HRESULT hr = folder->lpVtbl->CompareIDs(folder, 0, children[j - 1].Id, held.Id);
            if (FAILED(hr) || (short)HRESULT_CODE(hr) <= 0)
                break;
            children[j] = children[j - 1];
        }
        children[j] = held;
    }
    for (UINT i = 0; i < count; i++) {
        PIDLIST_ABSOLUTE full = ILCombine(n->Pidl, children[i].Id);
        BOOL keep = full != nullptr;
        if (keep && n->Root->Filter) {
            IShellItem *si;
            if (SUCCEEDED(SHCreateItemFromIDList(full, &IID_IShellItem, (void **)&si))) {
                keep = n->Root->Filter->lpVtbl->IncludeItem(n->Root->Filter, si) == S_OK;
                si->lpVtbl->Release(si);
            }
        }
        if (keep)
            insert_node(t, parent_item, TVI_LAST, full, n->Root, folder, children[i].Id);
        else
            ILFree(full);
        CoTaskMemFree(children[i].Id);
    }
    CoTaskMemFree(children);
    folder->lpVtbl->Release(folder);
    if (item && !tree_next(t, item, TVGN_CHILD)) {
        TVITEMW tv = {.mask = TVIF_CHILDREN, .hItem = item, .cChildren = 0};
        SendMessageW(t->Tree, TVM_SETITEMW, 0, (LPARAM)&tv);
    }
}

static void fill_item(NSTC *t, HTREEITEM item)
{
    NODE *n = node_of(t, item);
    if (n)
        fill(t, item, n, item);
}

enum {
    MATCH_NONE,
    MATCH_ANCESTOR,
    MATCH_EQUAL,
};

/* how a node stands to a target: by its list, else by the file system path, so that the Documents of the desktop
   holds C:\Users\name\Documents\x as on windows. */
static int match(PCIDLIST_ABSOLUTE node, PCIDLIST_ABSOLUTE target, LPCWSTR target_path)
{
    if (is_prefix(node, target))
        return id_count(node) == id_count(target) ? MATCH_EQUAL : MATCH_ANCESTOR;
    WCHAR path[MAX_PATH];
    if (!target_path[0] || !node->mkid.cb || !SHGetPathFromIDListW(node, path) || !path[0])
        return MATCH_NONE;
    int length = lstrlenW(path);
    if (!lstrcmpiW(path, target_path))
        return MATCH_EQUAL;
    if (length && path[length - 1] == L'\\')
        length--;
    if (lstrlenW(target_path) > length && target_path[length] == L'\\' &&
        CompareStringOrdinal(path, length, target_path, length, TRUE) == CSTR_EQUAL)
        return MATCH_ANCESTOR;
    return MATCH_NONE;
}

/* the item of a list, filling the folders on the way when expand is set. */
static HTREEITEM find_item(NSTC *t, PCIDLIST_ABSOLUTE pidl, BOOL expand)
{
    WCHAR target_path[MAX_PATH] = {0};
    if (pidl->mkid.cb && !SHGetPathFromIDListW(pidl, target_path))
        target_path[0] = 0;
    for (ROOT *r = t->Roots; r; r = r->Next) {
        int how = match(r->Pidl, pidl, target_path);
        if (how == MATCH_NONE)
            continue;
        HTREEITEM at = r->Item;
        NODE *n = r->Style & NSTCRS_HIDDEN ? nullptr : node_of(t, at);
        if (n && how == MATCH_EQUAL)
            return at;
        for (;;) {
            if (n) {
                if (!n->Filled && !expand)
                    return nullptr;
                fill(t, at, n, at);
            }
            HTREEITEM child = tree_next(t, n ? at : nullptr, n ? TVGN_CHILD : TVGN_ROOT);
            for (; child; child = tree_next(t, child, TVGN_NEXT)) {
                NODE *c = node_of(t, child);
                if (c && (how = match(c->Pidl, pidl, target_path)) != MATCH_NONE)
                    break;
            }
            if (!child)
                break;
            if (how == MATCH_EQUAL)
                return child;
            at = child;
            n = node_of(t, child);
        }
    }
    return nullptr;
}

static HTREEITEM find_shell_item(NSTC *t, IShellItem *item, BOOL expand)
{
    PIDLIST_ABSOLUTE pidl;
    if (!item || FAILED(SHGetIDListFromObject((IUnknown *)item, &pidl)))
        return nullptr;
    HTREEITEM found = find_item(t, pidl, expand);
    ILFree(pidl);
    return found;
}

/* ------------------------------------------------------------------ making folders and renaming */

static HRESULT new_folder(NSTC *t)
{
    HTREEITEM parent = tree_next(t, nullptr, TVGN_CARET);
    NODE *n = node_of(t, parent);
    if (n && !(n->Attributes & SFGAO_FOLDER)) {
        parent = tree_next(t, parent, TVGN_PARENT);
        n = node_of(t, parent);
    }
    WCHAR directory[MAX_PATH], path[MAX_PATH], base[64], name[80];
    if (!n || !SHGetPathFromIDListW(n->Pidl, directory))
        return E_FAIL;
    HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
    if (!shell32 || !LoadStringW(shell32, 30396, base, RTL_NUMBER_OF(base)))
        lstrcpyW(base, L"New folder");
    for (int i = 1;; i++) {
        if (i == 1)
            lstrcpyW(name, base);
        else
            wsprintfW(name, L"%s (%d)", base, i);
        SIZE_T length = lstrlenW(directory);
        if (length + lstrlenW(name) + 2 > MAX_PATH)
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        lstrcpyW(path, directory);
        if (length && path[length - 1] != L'\\')
            lstrcatW(path, L"\\");
        lstrcatW(path, name);
        if (CreateDirectoryW(path, nullptr))
            break;
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS || i > 9999)
            return HRESULT_FROM_WIN32(error);
    }
    /* the folder is listed again with the new one in its place. */
    HTREEITEM child;
    while ((child = tree_next(t, parent, TVGN_CHILD)))
        SendMessageW(t->Tree, TVM_DELETEITEM, 0, (LPARAM)child);
    n->Filled = FALSE;
    TVITEMW tv = {.mask = TVIF_CHILDREN, .hItem = parent, .cChildren = 1};
    SendMessageW(t->Tree, TVM_SETITEMW, 0, (LPARAM)&tv);
    fill(t, parent, n, parent);
    SendMessageW(t->Tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)parent);
    HTREEITEM item = tree_next(t, parent, TVGN_CHILD);
    for (; item; item = tree_next(t, item, TVGN_NEXT)) {
        NODE *c = node_of(t, item);
        WCHAR child_path[MAX_PATH];
        if (c && SHGetPathFromIDListW(c->Pidl, child_path) && !lstrcmpiW(child_path, path))
            break;
    }
    if (!item)
        return S_OK;
    SendMessageW(t->Tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)item);
    SendMessageW(t->Tree, TVM_ENSUREVISIBLE, 0, (LPARAM)item);
    SetFocus(t->Tree);
    SendMessageW(t->Tree, TVM_EDITLABELW, 0, (LPARAM)item);
    return S_OK;
}

static BOOL rename_item(NSTC *t, HTREEITEM item, LPCWSTR text)
{
    NODE *n = node_of(t, item);
    PCUITEMID_CHILD last = nullptr;
    IShellFolder *folder = nullptr;
    if (!n || !text || !text[0] || !n->Pidl->mkid.cb ||
        FAILED(SHBindToParent(n->Pidl, &IID_IShellFolder, (void **)&folder, &last)))
        return FALSE;
    PITEMID_CHILD renamed = nullptr;
    HRESULT hr = folder->lpVtbl->SetNameOf(folder, t->Host, last, text, SHGDN_INFOLDER, &renamed);
    if (SUCCEEDED(hr) && renamed) {
        PIDLIST_ABSOLUTE parent = ILClone(n->Pidl);
        if (parent && ILRemoveLastID(parent)) {
            PIDLIST_ABSOLUTE full = ILCombine(parent, renamed);
            if (full) {
                ILFree(n->Pidl);
                n->Pidl = full;
            }
        }
        ILFree(parent);
        WCHAR name[MAX_PATH] = {0};
        STRRET s;
        if (SUCCEEDED(folder->lpVtbl->GetDisplayNameOf(folder, renamed, SHGDN_INFOLDER, &s)) &&
            strret_text(&s, renamed, name, MAX_PATH)) {
            TVITEMW tv = {.mask = TVIF_TEXT, .hItem = item, .pszText = name};
            SendMessageW(t->Tree, TVM_SETITEMW, 0, (LPARAM)&tv);
        }
        /* the children were listed under the old name. */
        HTREEITEM child;
        while ((child = tree_next(t, item, TVGN_CHILD)))
            SendMessageW(t->Tree, TVM_DELETEITEM, 0, (LPARAM)child);
        n->Filled = FALSE;
        CoTaskMemFree(renamed);
    }
    folder->lpVtbl->Release(folder);
    return SUCCEEDED(hr);
}

/* ------------------------------------------------------------------ the windows */

static LRESULT on_notify(NSTC *t, NMHDR *header)
{
    if (header->hwndFrom != t->Tree)
        return 0;
    switch (header->code) {
    case TVN_ITEMEXPANDINGW: {
        NMTREEVIEWW *nm = (NMTREEVIEWW *)header;
        if ((nm->action & 3) == TVE_EXPAND) {
            fire_item(t, nm->itemNew.hItem, 0);
            fill_item(t, nm->itemNew.hItem);
            if (!tree_next(t, nm->itemNew.hItem, TVGN_CHILD))
                return TRUE;
        }
        return 0;
    }
    case TVN_ITEMEXPANDEDW: {
        NMTREEVIEWW *nm = (NMTREEVIEWW *)header;
        if ((nm->action & 3) == TVE_EXPAND)
            fire_item(t, nm->itemNew.hItem, 1);
        return 0;
    }
    case TVN_SELCHANGEDW:
        queue_selection(t);
        return 0;
    case TVN_DELETEITEMW: {
        NMTREEVIEWW *nm = (NMTREEVIEWW *)header;
        NODE *n = (NODE *)nm->itemOld.lParam;
        if (n) {
            ILFree(n->Pidl);
            CoTaskMemFree(n);
        }
        return 0;
    }
    case TVN_BEGINLABELEDITW: {
        NMTVDISPINFOW *nm = (NMTVDISPINFOW *)header;
        NODE *n = node_of(t, nm->item.hItem);
        if (!n || (t->Style & NSTCS_NOEDITLABELS) || !(n->Attributes & SFGAO_CANRENAME))
            return TRUE;
        /* the box shows the name for editing, which keeps the extension the display hides. */
        PCUITEMID_CHILD last;
        IShellFolder *folder;
        if (SUCCEEDED(SHBindToParent(n->Pidl, &IID_IShellFolder, (void **)&folder, &last))) {
            STRRET s;
            WCHAR name[MAX_PATH];
            HWND edit = (HWND)SendMessageW(t->Tree, TVM_GETEDITCONTROL, 0, 0);
            if (SUCCEEDED(folder->lpVtbl->GetDisplayNameOf(folder, last, SHGDN_INFOLDER | SHGDN_FOREDITING, &s)) &&
                strret_text(&s, last, name, MAX_PATH) && edit)
                SetWindowTextW(edit, name);
            folder->lpVtbl->Release(folder);
        }
        fire_item(t, nm->item.hItem, 2);
        return FALSE;
    }
    case TVN_ENDLABELEDITW: {
        NMTVDISPINFOW *nm = (NMTVDISPINFOW *)header;
        BOOL renamed = nm->item.pszText && rename_item(t, nm->item.hItem, nm->item.pszText);
        fire_item(t, nm->item.hItem, 3);
        if (renamed && nm->item.hItem == tree_next(t, nullptr, TVGN_CARET))
            queue_selection(t);
        return FALSE;
    }
    case NM_CLICK:
    case NM_DBLCLK:
    case NM_RCLICK: {
        TVHITTESTINFO hit = {0};
        GetCursorPos(&hit.pt);
        ScreenToClient(t->Tree, &hit.pt);
        HTREEITEM item = (HTREEITEM)SendMessageW(t->Tree, TVM_HITTEST, 0, (LPARAM)&hit);
        IShellItem *si;
        if (!item || FAILED(item_of(t, item, &si)))
            return 0;
        NSTCECLICKTYPE click = header->code == NM_RCLICK ? NSTCECT_RBUTTON
                               : header->code == NM_DBLCLK ? NSTCECT_LBUTTON | NSTCECT_DBLCLICK
                                                           : NSTCECT_LBUTTON;
        BOOL handled = FALSE;
        t->Control.lpVtbl->AddRef(&t->Control);
        for (int i = 0; i < MAX_SINKS && !handled; i++)
            if (t->Sinks[i])
                handled = t->Sinks[i]->lpVtbl->OnItemClick(t->Sinks[i], si, hit.flags, click) == S_OK;
        t->Control.lpVtbl->Release(&t->Control);
        si->lpVtbl->Release(si);
        return handled;
    }
    }
    return 0;
}

/* the program sees the keys first; a failure keeps them from the tree, as CNscTree does. */
static LRESULT CALLBACK tree_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    NSTC *t = (NSTC *)data;
    switch (message) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP: {
        BOOL swallowed = FALSE;
        t->Control.lpVtbl->AddRef(&t->Control);
        for (int i = 0; i < MAX_SINKS && !swallowed; i++)
            if (t->Sinks[i])
                swallowed = FAILED(t->Sinks[i]->lpVtbl->OnKeyboardInput(t->Sinks[i], message, wparam, lparam));
        t->Control.lpVtbl->Release(&t->Control);
        if (swallowed)
            return 0;
        break;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, tree_proc, id);
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static LRESULT CALLBACK host_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    NSTC *t = (NSTC *)GetWindowLongPtrW(hwnd, 0);
    if (message == WM_NCCREATE) {
        t = ((CREATESTRUCTW *)lparam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)t);
    }
    if (!t)
        return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_SIZE:
        if (t->Tree)
            MoveWindow(t->Tree, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        return 0;
    case WM_SETFOCUS:
        if (t->Tree)
            SetFocus(t->Tree);
        return 0;
    case WM_SETTEXT:
        /* the hidden static carries the name of the tree for accessibility. */
        if (t->Name)
            SetWindowTextW(t->Name, (LPCWSTR)lparam);
        break;
    case WM_NOTIFY:
        return on_notify(t, (NMHDR *)lparam);
    case WM_NSTC_SELECTION:
        if (t->SelectionPending)
            fire_selection(t);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, 0, 0);
        t->Host = nullptr;
        t->Tree = nullptr;
        t->Name = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

/* NSTCSTYLE to tree styles, from the table of CNscTree::_FlagsFromStyles and CNscTree::_SetStyle. */
static void tree_styles(NSTCSTYLE style, DWORD *tree, DWORD *extended)
{
    static const struct {
        DWORD Nstc;
        BOOL Inverted;
        DWORD Tree, Extended;
    } table[] = {
        {NSTCS_HASEXPANDOS, FALSE, TVS_HASBUTTONS, 0},
        {NSTCS_HASLINES, FALSE, TVS_HASLINES, 0},
        {NSTCS_SINGLECLICKEXPAND, FALSE, TVS_SINGLEEXPAND, 0},
        {NSTCS_FULLROWSELECT, FALSE, TVS_FULLROWSELECT, 0},
        {NSTCS_SPRINGEXPAND, TRUE, 0, TVS_EX_NOSINGLECOLLAPSE},
        {NSTCS_HORIZONTALSCROLL, FALSE, WS_HSCROLL, 0},
        {NSTCS_ROOTHASEXPANDO, FALSE, TVS_LINESATROOT, 0},
        {NSTCS_SHOWSELECTIONALWAYS, FALSE, TVS_SHOWSELALWAYS, 0},
        {NSTCS_NOINFOTIP, TRUE, TVS_INFOTIP, 0},
        {NSTCS_EVENHEIGHT, TRUE, TVS_NONEVENHEIGHT, 0},
        {NSTCS_DISABLEDRAGDROP, FALSE, TVS_DISABLEDRAGDROP, 0},
        {NSTCS_RICHTOOLTIP, FALSE, 0, TVS_EX_RICHTOOLTIP},
        {NSTCS_NOEDITLABELS, TRUE, TVS_EDITLABELS, 0},
        {NSTCS_AUTOHSCROLL, FALSE, 0, TVS_EX_AUTOHSCROLL},
        {NSTCS_FADEINOUTEXPANDOS, FALSE, 0, TVS_EX_FADEINOUTEXPANDOS},
        {NSTCS_CHECKBOXES, FALSE, TVS_CHECKBOXES, 0},
        {NSTCS_PARTIALCHECKBOXES, FALSE, 0, TVS_EX_PARTIALCHECKBOXES},
        {NSTCS_EXCLUSIONCHECKBOXES, FALSE, 0, TVS_EX_EXCLUSIONCHECKBOXES},
        {NSTCS_DIMMEDCHECKBOXES, FALSE, 0, TVS_EX_DIMMEDCHECKBOXES},
        {NSTCS_NOINDENTCHECKS, FALSE, 0, TVS_EX_NOINDENTSTATE},
    };
    DWORD s = TVS_TRACKSELECT | TVS_EDITLABELS, x = 0;
    for (UINT i = 0; i < RTL_NUMBER_OF(table); i++) {
        if (((style & table[i].Nstc) != 0) != table[i].Inverted) {
            s |= table[i].Tree;
            x |= table[i].Extended;
        }
    }
    if (!(style & NSTCS_EVENHEIGHT))
        s |= TVS_NONEVENHEIGHT;
    if (s & WS_HSCROLL)
        s &= ~WS_HSCROLL;
    else
        s |= TVS_NOHSCROLL;
    if (s & TVS_HASLINES)
        s &= ~TVS_FULLROWSELECT;
    *tree = s | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VSCROLL | WS_TABSTOP;
    x |= TVS_EX_DOUBLEBUFFER | TVS_EX_DRAWIMAGEASYNC;
    if (!(style & NSTCS_FAVORITESMODE))
        x |= TVS_EX_RICHTOOLTIP;
    *extended = x;
}

typedef HRESULT(WINAPI *SET_WINDOW_THEME)(HWND, LPCWSTR, LPCWSTR);

static void set_theme(HWND hwnd, LPCWSTR theme)
{
    HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
    SET_WINDOW_THEME set = uxtheme ? (SET_WINDOW_THEME)(void *)GetProcAddress(uxtheme, "SetWindowTheme") : nullptr;
    if (set)
        set(hwnd, theme, nullptr);
}

static HRESULT STDMETHODCALLTYPE control_initialize(INameSpaceTreeControl2 *This, HWND parent, RECT *rect,
                                                    NSTCSTYLE style)
{
    NSTC *t = from_control(This);
    if (t->Host)
        return E_UNEXPECTED;
    LPCWSTR class_name = (t->PrivateFlags & NSTCFP_BROWSEFORFOLDER) ? L"SHBrowseForFolder ShellNameSpace Control"
                                                                     : L"NamespaceTreeControl";
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = host_proc;
    wc.cbWndExtra = sizeof(void *);
    wc.hInstance = g_explorerframe;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = class_name;
    RegisterClassW(&wc);
    /* the tree class is registered when comctl32 loads. */
    LoadLibraryW(L"comctl32.dll");
    RECT r;
    if (rect)
        r = *rect;
    else
        GetClientRect(parent, &r);
    t->Style = style;
    DWORD host_style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    if (style & NSTCS_BORDER)
        host_style |= WS_BORDER;
    t->Host = CreateWindowExW((style & NSTCS_TABSTOP) ? WS_EX_CONTROLPARENT : 0, class_name, L"Namespace Tree Control",
                              host_style, r.left, r.top, r.right - r.left, r.bottom - r.top, parent, nullptr,
                              g_explorerframe, t);
    if (!t->Host)
        return HRESULT_FROM_WIN32(GetLastError());
    t->Name = CreateWindowExW(0, L"Static", L"Namespace Tree Control", WS_CHILD, 0, 0, 0, 0, t->Host, nullptr,
                              g_explorerframe, nullptr);
    DWORD tree_style, extended;
    tree_styles(style, &tree_style, &extended);
    RECT client;
    GetClientRect(t->Host, &client);
    t->Tree = CreateWindowExW(0, WC_TREEVIEWW, L"Navigation Pane", tree_style | WS_VISIBLE, 0, 0, client.right,
                              client.bottom, t->Host, (HMENU)(ULONG_PTR)TREE_ID, g_explorerframe, nullptr);
    if (!t->Tree) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        DestroyWindow(t->Host);
        return hr;
    }
    set_theme(t->Tree, L"Explorer");
    SendMessageW(t->Tree, TVM_SETSCROLLTIME, 100, 0);
    SendMessageW(t->Tree, TVM_SETEXTENDEDSTYLE, extended, extended);
    SendMessageW(t->Tree, TVM_SETBKCOLOR, 0, (LPARAM)GetSysColor(COLOR_WINDOW));
    LOGFONTW lf;
    if (SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0) && (t->Font = CreateFontIndirectW(&lf)))
        SendMessageW(t->Tree, WM_SETFONT, (WPARAM)t->Font, FALSE);
    struct _IMAGELIST *small = nullptr;
    if (Shell_GetImageLists(nullptr, &small))
        SendMessageW(t->Tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)small);
    /* as tall as the font or the small icons, with two pixels above and below. */
    HDC dc = GetDC(t->Tree);
    if (dc) {
        TEXTMETRICW tm;
        HGDIOBJ old = SelectObject(dc, (HGDIOBJ)SendMessageW(t->Tree, WM_GETFONT, 0, 0));
        if (GetTextMetricsW(dc, &tm)) {
            int icon = GetSystemMetrics(SM_CYSMICON);
            SendMessageW(t->Tree, TVM_SETITEMHEIGHT, (tm.tmHeight > icon ? tm.tmHeight : icon) + 4, 0);
        }
        SelectObject(dc, old);
        ReleaseDC(t->Tree, dc);
    }
    t->Subclassed = SetWindowSubclass(t->Tree, tree_proc, 0, (DWORD_PTR)t);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_tree_advise(INameSpaceTreeControl2 *This, IUnknown *sink, DWORD *cookie)
{
    NSTC *t = from_control(This);
    if (!sink || !cookie)
        return E_INVALIDARG;
    *cookie = 0;
    for (int i = 0; i < MAX_SINKS; i++) {
        if (t->Sinks[i])
            continue;
        HRESULT hr =
            sink->lpVtbl->QueryInterface(sink, &IID_INameSpaceTreeControlEvents, (void **)&t->Sinks[i]);
        if (FAILED(hr)) {
            t->Sinks[i] = nullptr;
            return hr;
        }
        *cookie = (DWORD)i + 1;
        return S_OK;
    }
    return E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE control_tree_unadvise(INameSpaceTreeControl2 *This, DWORD cookie)
{
    NSTC *t = from_control(This);
    if (!cookie || cookie > MAX_SINKS || !t->Sinks[cookie - 1])
        return E_INVALIDARG;
    INameSpaceTreeControlEvents *e = t->Sinks[cookie - 1];
    t->Sinks[cookie - 1] = nullptr;
    e->lpVtbl->Release(e);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_insert_root(INameSpaceTreeControl2 *This, int index, IShellItem *root,
                                                     SHCONTF flags, NSTCROOTSTYLE style, IShellItemFilter *filter)
{
    NSTC *t = from_control(This);
    if (!root)
        return E_INVALIDARG;
    if (!t->Tree)
        return E_UNEXPECTED;
    PIDLIST_ABSOLUTE pidl;
    HRESULT hr = SHGetIDListFromObject((IUnknown *)root, &pidl);
    if (FAILED(hr))
        return hr;
    ROOT *r = CoTaskMemAlloc(sizeof(*r));
    if (!r) {
        ILFree(pidl);
        return E_OUTOFMEMORY;
    }
    r->Pidl = pidl;
    r->Flags = flags;
    r->Style = style;
    r->Filter = filter;
    if (filter)
        filter->lpVtbl->AddRef(filter);
    /* the place in the list of roots and in the tree. */
    ROOT **link = &t->Roots;
    HTREEITEM after = TVI_FIRST;
    for (int i = 0; *link && (index < 0 || i < index); i++) {
        if (!((*link)->Style & NSTCRS_HIDDEN))
            after = (*link)->Item;
        link = &(*link)->Next;
    }
    r->Next = *link;
    *link = r;
    if (style & NSTCRS_HIDDEN) {
        r->Item = nullptr;
        NODE hidden = {.Pidl = pidl, .Root = r};
        fill(t, nullptr, &hidden, TVI_ROOT);
        return S_OK;
    }
    PIDLIST_ABSOLUTE copy = ILClone(pidl);
    r->Item = copy ? insert_node(t, TVI_ROOT, after, copy, r, nullptr, nullptr) : nullptr;
    if (!r->Item)
        return E_FAIL;
    if (style & NSTCRS_EXPANDED)
        SendMessageW(t->Tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)r->Item);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_append_root(INameSpaceTreeControl2 *This, IShellItem *root, SHCONTF flags,
                                                     NSTCROOTSTYLE style, IShellItemFilter *filter)
{
    return control_insert_root(This, -1, root, flags, style, filter);
}

static void free_root(NSTC *t, ROOT *r)
{
    if (r->Item) {
        SendMessageW(t->Tree, TVM_DELETEITEM, 0, (LPARAM)r->Item);
    } else if (t->Tree) {
        /* the children of a hidden root are at the top of the tree. */
        for (HTREEITEM item = tree_next(t, nullptr, TVGN_ROOT), next; item; item = next) {
            next = tree_next(t, item, TVGN_NEXT);
            NODE *n = node_of(t, item);
            if (n && n->Root == r)
                SendMessageW(t->Tree, TVM_DELETEITEM, 0, (LPARAM)item);
        }
    }
    if (r->Filter)
        r->Filter->lpVtbl->Release(r->Filter);
    ILFree(r->Pidl);
    CoTaskMemFree(r);
}

static HRESULT STDMETHODCALLTYPE control_remove_root(INameSpaceTreeControl2 *This, IShellItem *root)
{
    NSTC *t = from_control(This);
    PIDLIST_ABSOLUTE pidl;
    if (!root || FAILED(SHGetIDListFromObject((IUnknown *)root, &pidl)))
        return E_INVALIDARG;
    HRESULT hr = E_FAIL;
    for (ROOT **link = &t->Roots; *link; link = &(*link)->Next) {
        if (same_place((*link)->Pidl, pidl)) {
            ROOT *r = *link;
            *link = r->Next;
            free_root(t, r);
            hr = S_OK;
            break;
        }
    }
    ILFree(pidl);
    return hr;
}

static HRESULT STDMETHODCALLTYPE control_remove_all_roots(INameSpaceTreeControl2 *This)
{
    NSTC *t = from_control(This);
    while (t->Roots) {
        ROOT *r = t->Roots;
        t->Roots = r->Next;
        free_root(t, r);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_get_root_items(INameSpaceTreeControl2 *This, IShellItemArray **roots)
{
    NSTC *t = from_control(This);
    if (!roots)
        return E_POINTER;
    *roots = nullptr;
    UINT count = 0;
    for (ROOT *r = t->Roots; r; r = r->Next)
        count++;
    if (!count)
        return E_FAIL;
    PCIDLIST_ABSOLUTE *lists = CoTaskMemAlloc(count * sizeof(*lists));
    if (!lists)
        return E_OUTOFMEMORY;
    UINT i = 0;
    for (ROOT *r = t->Roots; r; r = r->Next)
        lists[i++] = r->Pidl;
    HRESULT hr = SHCreateShellItemArrayFromIDLists(count, lists, roots);
    CoTaskMemFree(lists);
    return hr;
}

static HRESULT STDMETHODCALLTYPE control_set_item_state(INameSpaceTreeControl2 *This, IShellItem *item,
                                                        NSTCITEMSTATE mask, NSTCITEMSTATE flags)
{
    NSTC *t = from_control(This);
    if (!t->Tree)
        return E_UNEXPECTED;
    HTREEITEM found = find_shell_item(t, item, (flags & mask & (NSTCIS_SELECTED | NSTCIS_EXPANDED |
                                                                NSTCIS_SELECTEDNOEXPAND)) != 0);
    if (!found)
        return E_INVALIDARG;
    if (mask & NSTCIS_BOLD) {
        TVITEMW tv = {.mask = TVIF_STATE, .hItem = found, .state = (flags & NSTCIS_BOLD) ? TVIS_BOLD : 0,
                      .stateMask = TVIS_BOLD};
        SendMessageW(t->Tree, TVM_SETITEMW, 0, (LPARAM)&tv);
    }
    if (mask & (NSTCIS_SELECTED | NSTCIS_SELECTEDNOEXPAND)) {
        if (flags & (NSTCIS_SELECTED | NSTCIS_SELECTEDNOEXPAND)) {
            SendMessageW(t->Tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)found);
            SendMessageW(t->Tree, TVM_ENSUREVISIBLE, 0, (LPARAM)found);
        } else if (tree_next(t, nullptr, TVGN_CARET) == found) {
            SendMessageW(t->Tree, TVM_SELECTITEM, TVGN_CARET, 0);
        }
    }
    if (mask & NSTCIS_EXPANDED)
        SendMessageW(t->Tree, TVM_EXPAND, (flags & NSTCIS_EXPANDED) ? TVE_EXPAND : TVE_COLLAPSE, (LPARAM)found);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_get_item_state(INameSpaceTreeControl2 *This, IShellItem *item,
                                                        NSTCITEMSTATE mask, NSTCITEMSTATE *flags)
{
    NSTC *t = from_control(This);
    if (!flags)
        return E_POINTER;
    *flags = 0;
    HTREEITEM found = t->Tree ? find_shell_item(t, item, FALSE) : nullptr;
    if (!found)
        return E_INVALIDARG;
    UINT state = (UINT)SendMessageW(t->Tree, TVM_GETITEMSTATE, (WPARAM)found, TVIS_SELECTED | TVIS_EXPANDED |
                                                                                TVIS_BOLD);
    if (state & TVIS_SELECTED)
        *flags |= NSTCIS_SELECTED;
    if (state & TVIS_EXPANDED)
        *flags |= NSTCIS_EXPANDED;
    if (state & TVIS_BOLD)
        *flags |= NSTCIS_BOLD;
    *flags &= mask;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_get_selected_items(INameSpaceTreeControl2 *This, IShellItemArray **items)
{
    NSTC *t = from_control(This);
    if (!items)
        return E_POINTER;
    *items = nullptr;
    IShellItem *si;
    HRESULT hr = t->Tree ? item_of(t, tree_next(t, nullptr, TVGN_CARET), &si) : E_UNEXPECTED;
    if (FAILED(hr))
        return E_FAIL;
    hr = SHCreateShellItemArrayFromShellItem(si, &IID_IShellItemArray, (void **)items);
    si->lpVtbl->Release(si);
    return hr;
}

static HRESULT STDMETHODCALLTYPE control_get_item_custom_state(INameSpaceTreeControl2 *This, IShellItem *item,
                                                               int *state)
{
    NSTC *t = from_control(This);
    if (!state)
        return E_POINTER;
    HTREEITEM found = t->Tree ? find_shell_item(t, item, FALSE) : nullptr;
    if (!found)
        return E_INVALIDARG;
    *state = (int)(((UINT)SendMessageW(t->Tree, TVM_GETITEMSTATE, (WPARAM)found, TVIS_STATEIMAGEMASK)) >> 12);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_set_item_custom_state(INameSpaceTreeControl2 *This, IShellItem *item,
                                                               int state)
{
    NSTC *t = from_control(This);
    HTREEITEM found = t->Tree ? find_shell_item(t, item, TRUE) : nullptr;
    if (!found)
        return E_INVALIDARG;
    TVITEMW tv = {.mask = TVIF_STATE, .hItem = found, .state = INDEXTOSTATEIMAGEMASK(state),
                  .stateMask = TVIS_STATEIMAGEMASK};
    SendMessageW(t->Tree, TVM_SETITEMW, 0, (LPARAM)&tv);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_ensure_item_visible(INameSpaceTreeControl2 *This, IShellItem *item)
{
    NSTC *t = from_control(This);
    HTREEITEM found = t->Tree ? find_shell_item(t, item, TRUE) : nullptr;
    if (!found)
        return E_INVALIDARG;
    SendMessageW(t->Tree, TVM_ENSUREVISIBLE, 0, (LPARAM)found);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_set_theme(INameSpaceTreeControl2 *This, LPCWSTR theme)
{
    NSTC *t = from_control(This);
    if (!t->Tree)
        return E_UNEXPECTED;
    set_theme(t->Tree, theme);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_get_next_item(INameSpaceTreeControl2 *This, IShellItem *item, NSTCGNI which,
                                                       IShellItem **next)
{
    NSTC *t = from_control(This);
    static const UINT codes[] = {TVGN_NEXT,   TVGN_NEXTVISIBLE,  TVGN_PREVIOUS, TVGN_PREVIOUSVISIBLE,
                                 TVGN_PARENT, TVGN_CHILD,        TVGN_FIRSTVISIBLE, TVGN_LASTVISIBLE};
    if (!next)
        return E_POINTER;
    *next = nullptr;
    if ((UINT)which >= RTL_NUMBER_OF(codes) || !t->Tree)
        return E_INVALIDARG;
    HTREEITEM from = nullptr;
    if (which != NSTCGNI_FIRSTVISIBLE && which != NSTCGNI_LASTVISIBLE) {
        from = find_shell_item(t, item, FALSE);
        if (!from)
            return E_INVALIDARG;
        if (which == NSTCGNI_CHILD)
            fill_item(t, from);
    }
    HTREEITEM to = tree_next(t, from, codes[which]);
    return to ? item_of(t, to, next) : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE control_hit_test(INameSpaceTreeControl2 *This, POINT *point, IShellItem **item)
{
    NSTC *t = from_control(This);
    if (!point || !item)
        return E_POINTER;
    *item = nullptr;
    if (!t->Tree)
        return E_UNEXPECTED;
    TVHITTESTINFO hit = {.pt = *point};
    HTREEITEM found = (HTREEITEM)SendMessageW(t->Tree, TVM_HITTEST, 0, (LPARAM)&hit);
    return found ? item_of(t, found, item) : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE control_get_item_rect(INameSpaceTreeControl2 *This, IShellItem *item, RECT *rect)
{
    NSTC *t = from_control(This);
    if (!rect)
        return E_POINTER;
    HTREEITEM found = t->Tree ? find_shell_item(t, item, FALSE) : nullptr;
    if (!found)
        return E_INVALIDARG;
    *(HTREEITEM *)rect = found;
    return SendMessageW(t->Tree, TVM_GETITEMRECT, TRUE, (LPARAM)rect) ? S_OK : E_FAIL;
}

static void collapse(NSTC *t, HTREEITEM item)
{
    for (; item; item = tree_next(t, item, TVGN_NEXT)) {
        collapse(t, tree_next(t, item, TVGN_CHILD));
        SendMessageW(t->Tree, TVM_EXPAND, TVE_COLLAPSE, (LPARAM)item);
    }
}

static HRESULT STDMETHODCALLTYPE control_collapse_all(INameSpaceTreeControl2 *This)
{
    NSTC *t = from_control(This);
    if (t->Tree)
        collapse(t, tree_next(t, nullptr, TVGN_ROOT));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_set_control_style(INameSpaceTreeControl2 *This, NSTCSTYLE mask,
                                                           NSTCSTYLE style)
{
    NSTC *t = from_control(This);
    t->Style = (t->Style & ~mask) | (style & mask);
    if (t->Tree) {
        DWORD tree_style, extended;
        tree_styles(t->Style, &tree_style, &extended);
        SetWindowLongW(t->Tree, GWL_STYLE, (LONG)(tree_style | WS_VISIBLE));
        SendMessageW(t->Tree, TVM_SETEXTENDEDSTYLE, ~0u, extended);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_get_control_style(INameSpaceTreeControl2 *This, NSTCSTYLE mask,
                                                           NSTCSTYLE *style)
{
    if (!style)
        return E_POINTER;
    *style = from_control(This)->Style & mask;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_set_control_style2(INameSpaceTreeControl2 *This, NSTCSTYLE2 mask,
                                                            NSTCSTYLE2 style)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE control_get_control_style2(INameSpaceTreeControl2 *This, NSTCSTYLE2 mask,
                                                            NSTCSTYLE2 *style)
{
    if (!style)
        return E_POINTER;
    *style = 0;
    return S_OK;
}

/* ------------------------------------------------------------------ identity */

static HRESULT nstc_query(NSTC *t, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_INameSpaceTreeControl) ||
        IsEqualIID(riid, &IID_INameSpaceTreeControl2))
        *object = &t->Control;
    else if (IsEqualIID(riid, &IID_IOleWindow))
        *object = &t->Window;
    else if (IsEqualIID(riid, &IID_IObjectWithSite))
        *object = &t->Site;
    else if (IsEqualIID(riid, &IID_IFolderFilterSite))
        *object = &t->FilterSite;
    else if (IsEqualIID(riid, &IID_INameSpaceTreeControlFlagsPrivate))
        *object = &t->Flags;
    else if (IsEqualIID(riid, &IID_ICreateNewFolderPriv))
        *object = &t->NewFolder;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&t->Refs);
    return S_OK;
}

static ULONG nstc_release(NSTC *t)
{
    LONG refs = InterlockedDecrement(&t->Refs);
    if (refs)
        return (ULONG)refs;
    if (t->Host)
        DestroyWindow(t->Host);
    while (t->Roots) {
        ROOT *r = t->Roots;
        t->Roots = r->Next;
        r->Item = nullptr;
        if (r->Filter)
            r->Filter->lpVtbl->Release(r->Filter);
        ILFree(r->Pidl);
        CoTaskMemFree(r);
    }
    for (int i = 0; i < MAX_SINKS; i++)
        if (t->Sinks[i])
            t->Sinks[i]->lpVtbl->Release(t->Sinks[i]);
    if (t->Filter)
        t->Filter->lpVtbl->Release(t->Filter);
    if (t->SiteObject)
        t->SiteObject->lpVtbl->Release(t->SiteObject);
    if (t->Font)
        DeleteObject(t->Font);
    CoTaskMemFree(t);
    return 0;
}

static HRESULT STDMETHODCALLTYPE control_query_interface(INameSpaceTreeControl2 *This, REFIID riid, void **object)
{
    return nstc_query(from_control(This), riid, object);
}

static ULONG STDMETHODCALLTYPE control_add_ref(INameSpaceTreeControl2 *This)
{
    return (ULONG)InterlockedIncrement(&from_control(This)->Refs);
}

static ULONG STDMETHODCALLTYPE control_release(INameSpaceTreeControl2 *This)
{
    return nstc_release(from_control(This));
}

static const INameSpaceTreeControl2Vtbl g_control_vtbl = {
    control_query_interface,     control_add_ref,
    control_release,             control_initialize,
    control_tree_advise,         control_tree_unadvise,
    control_append_root,         control_insert_root,
    control_remove_root,         control_remove_all_roots,
    control_get_root_items,      control_set_item_state,
    control_get_item_state,      control_get_selected_items,
    control_get_item_custom_state, control_set_item_custom_state,
    control_ensure_item_visible, control_set_theme,
    control_get_next_item,       control_hit_test,
    control_get_item_rect,       control_collapse_all,
    control_set_control_style,   control_get_control_style,
    control_set_control_style2,  control_get_control_style2,
};

#define NSTC_FROM(member, p) CONTAINING_RECORD(p, NSTC, member)

static HRESULT STDMETHODCALLTYPE window_query_interface(IOleWindow *This, REFIID riid, void **object)
{
    return nstc_query(NSTC_FROM(Window, This), riid, object);
}

static ULONG STDMETHODCALLTYPE window_add_ref(IOleWindow *This)
{
    return (ULONG)InterlockedIncrement(&NSTC_FROM(Window, This)->Refs);
}

static ULONG STDMETHODCALLTYPE window_release(IOleWindow *This)
{
    return nstc_release(NSTC_FROM(Window, This));
}

static HRESULT STDMETHODCALLTYPE window_get_window(IOleWindow *This, HWND *window)
{
    NSTC *t = NSTC_FROM(Window, This);
    if (!window)
        return E_POINTER;
    *window = t->Host;
    return t->Host ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE window_context_sensitive_help(IOleWindow *This, BOOL enter)
{
    return E_NOTIMPL;
}

static const IOleWindowVtbl g_window_vtbl = {
    window_query_interface, window_add_ref, window_release, window_get_window, window_context_sensitive_help,
};

static HRESULT STDMETHODCALLTYPE site_query_interface(IObjectWithSite *This, REFIID riid, void **object)
{
    return nstc_query(NSTC_FROM(Site, This), riid, object);
}

static ULONG STDMETHODCALLTYPE site_add_ref(IObjectWithSite *This)
{
    return (ULONG)InterlockedIncrement(&NSTC_FROM(Site, This)->Refs);
}

static ULONG STDMETHODCALLTYPE site_release(IObjectWithSite *This)
{
    return nstc_release(NSTC_FROM(Site, This));
}

static HRESULT STDMETHODCALLTYPE site_set_site(IObjectWithSite *This, IUnknown *site)
{
    NSTC *t = NSTC_FROM(Site, This);
    if (site)
        site->lpVtbl->AddRef(site);
    if (t->SiteObject)
        t->SiteObject->lpVtbl->Release(t->SiteObject);
    t->SiteObject = site;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_get_site(IObjectWithSite *This, REFIID riid, void **site)
{
    NSTC *t = NSTC_FROM(Site, This);
    if (!site)
        return E_POINTER;
    *site = nullptr;
    if (!t->SiteObject)
        return E_FAIL;
    return t->SiteObject->lpVtbl->QueryInterface(t->SiteObject, riid, site);
}

static const IObjectWithSiteVtbl g_site_vtbl = {
    site_query_interface, site_add_ref, site_release, site_set_site, site_get_site,
};

static HRESULT STDMETHODCALLTYPE filter_query_interface(IFolderFilterSite *This, REFIID riid, void **object)
{
    return nstc_query(NSTC_FROM(FilterSite, This), riid, object);
}

static ULONG STDMETHODCALLTYPE filter_add_ref(IFolderFilterSite *This)
{
    return (ULONG)InterlockedIncrement(&NSTC_FROM(FilterSite, This)->Refs);
}

static ULONG STDMETHODCALLTYPE filter_release(IFolderFilterSite *This)
{
    return nstc_release(NSTC_FROM(FilterSite, This));
}

static HRESULT STDMETHODCALLTYPE filter_set_filter(IFolderFilterSite *This, IUnknown *filter)
{
    NSTC *t = NSTC_FROM(FilterSite, This);
    IFolderFilter *f = nullptr;
    if (filter && FAILED(filter->lpVtbl->QueryInterface(filter, &IID_IFolderFilter, (void **)&f)))
        f = nullptr;
    if (t->Filter)
        t->Filter->lpVtbl->Release(t->Filter);
    t->Filter = f;
    return S_OK;
}

static const IFolderFilterSiteVtbl g_filter_vtbl = {
    filter_query_interface, filter_add_ref, filter_release, filter_set_filter,
};

static HRESULT STDMETHODCALLTYPE flags_query_interface(IFlagsPrivate *This, REFIID riid, void **object)
{
    return nstc_query(NSTC_FROM(Flags, This), riid, object);
}

static ULONG STDMETHODCALLTYPE flags_add_ref(IFlagsPrivate *This)
{
    return (ULONG)InterlockedIncrement(&NSTC_FROM(Flags, This)->Refs);
}

static ULONG STDMETHODCALLTYPE flags_release(IFlagsPrivate *This)
{
    return nstc_release(NSTC_FROM(Flags, This));
}

static HRESULT STDMETHODCALLTYPE flags_set_private_flags(IFlagsPrivate *This, DWORD flags)
{
    NSTC_FROM(Flags, This)->PrivateFlags = flags;
    return S_OK;
}

static const IFlagsPrivateVtbl g_flags_vtbl = {
    flags_query_interface, flags_add_ref, flags_release, flags_set_private_flags,
};

static HRESULT STDMETHODCALLTYPE folder_query_interface(INewFolderPrivate *This, REFIID riid, void **object)
{
    return nstc_query(NSTC_FROM(NewFolder, This), riid, object);
}

static ULONG STDMETHODCALLTYPE folder_add_ref(INewFolderPrivate *This)
{
    return (ULONG)InterlockedIncrement(&NSTC_FROM(NewFolder, This)->Refs);
}

static ULONG STDMETHODCALLTYPE folder_release(INewFolderPrivate *This)
{
    return nstc_release(NSTC_FROM(NewFolder, This));
}

static HRESULT STDMETHODCALLTYPE folder_create_new_folder(INewFolderPrivate *This)
{
    NSTC *t = NSTC_FROM(NewFolder, This);
    return t->Tree ? new_folder(t) : E_UNEXPECTED;
}

static const INewFolderPrivateVtbl g_new_folder_vtbl = {
    folder_query_interface, folder_add_ref, folder_release, folder_create_new_folder,
};

/* ------------------------------------------------------------------ class factory */

static HRESULT STDMETHODCALLTYPE factory_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid,
                                                         void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    NSTC *t = CoTaskMemAlloc(sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    memset(t, 0, sizeof(*t));
    t->Control.lpVtbl = &g_control_vtbl;
    t->Window.lpVtbl = &g_window_vtbl;
    t->Site.lpVtbl = &g_site_vtbl;
    t->FilterSite.lpVtbl = &g_filter_vtbl;
    t->Flags.lpVtbl = &g_flags_vtbl;
    t->NewFolder.lpVtbl = &g_new_folder_vtbl;
    t->Refs = 1;
    HRESULT hr = nstc_query(t, riid, object);
    nstc_release(t);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};

static IClassFactory g_factory = {&g_factory_vtbl};

IClassFactory *NamespaceTreeControlFactory(void)
{
    return &g_factory;
}
