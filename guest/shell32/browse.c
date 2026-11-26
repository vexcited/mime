#include "shell32_private.h"
#include <mime/win32/commctrl.h>
#include <mime/win32/ole2.h>
#include <mime/win32/shlwapi.h>

#define IDD_BROWSE_OLD 1079
#define IDD_BROWSE_NEW 1087
#define IDC_TREE 14145
#define IDC_TITLE 14146
#define IDC_STATUS 14147
#define IDC_EDIT 14148
#define IDC_NEW_FOLDER 14150
#define IDC_GRIP 14151
#define IDC_FOLDER_LABEL 14152
#define IDC_HINT 14153
#define NAMESPACE_TREE_ID 100

/* the selection changed while the callback set it. windows tells the callback once its own call returned. */
#define WM_BROWSE_SELCHANGED (WM_USER + 0x200)

static const WCHAR g_host_class[] = L"SHBrowseForFolder ShellNameSpace Control";
static const WCHAR g_size_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer";

enum {
    NODE_DESKTOP,
    NODE_COMPUTER,
    NODE_PATH,
};

typedef struct _BROWSE_NODE {
    struct _BROWSE_NODE *Next;
    int Kind;
    BOOL Filled;
    /* a place of the desktop or a drive, which keeps its name. */
    BOOL Fixed;
    BOOL Folder;
    int Image;
    LPWSTR Path;
} BROWSE_NODE;

typedef struct _BROWSE {
    IFolderFilterSite Site;
    LONG Refs;
    IFolderFilter *Filter;
    BROWSEINFOW Info;
    BOOL Ansi;
    BOOL NewStyle;
    HWND Dialog, Host, Tree, Edit, Status;
    HFONT Font;
    BROWSE_NODE *Nodes;
    HTREEITEM Root;
    BOOL EditChanged;
    /* BFFM_SETSELECTION from the callback: the callback hears of the change afterwards. */
    BOOL Quiet;
    BOOL ShowHidden, ShowSuper;
    /* client size of the template, window size as last laid out, and the size grip. */
    int MinW, MinH, WinW, WinH, GripW, GripH;
    PIDLIST_ABSOLUTE Result;
    WCHAR Name[MAX_PATH];
    int Image;
} BROWSE;

/* ------------------------------------------------------------------ the filter site of BFFM_IUNKNOWN */

static HRESULT STDMETHODCALLTYPE site_query_interface(IFolderFilterSite *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IFolderFilterSite)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE site_add_ref(IFolderFilterSite *This)
{
    return (ULONG)InterlockedIncrement(&((BROWSE *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE site_release(IFolderFilterSite *This)
{
    BROWSE *b = (BROWSE *)This;
    LONG refs = InterlockedDecrement(&b->Refs);
    if (!refs) {
        if (b->Filter)
            b->Filter->lpVtbl->Release(b->Filter);
        ShFree(b);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE site_set_filter(IFolderFilterSite *This, IUnknown *filter)
{
    BROWSE *b = (BROWSE *)This;
    IFolderFilter *f = nullptr;
    if (filter && FAILED(filter->lpVtbl->QueryInterface(filter, &IID_IFolderFilter, (void **)&f)))
        f = nullptr;
    if (b->Filter)
        b->Filter->lpVtbl->Release(b->Filter);
    b->Filter = f;
    return S_OK;
}

static const IFolderFilterSiteVtbl g_site_vtbl = {
    site_query_interface,
    site_add_ref,
    site_release,
    site_set_filter,
};

/* ------------------------------------------------------------------ nodes */

static int callback(BROWSE *b, UINT message, LPARAM lparam)
{
    if (!b->Info.lpfn)
        return 0;
    return b->Info.lpfn(b->Dialog, message, lparam, b->Info.lParam);
}

static BROWSE_NODE *new_node(BROWSE *b, int kind, LPCWSTR path)
{
    BROWSE_NODE *n = ShAllocZero(sizeof(*n));
    if (!n)
        return nullptr;
    n->Kind = kind;
    n->Folder = TRUE;
    if (path && !(n->Path = ShDuplicate(path))) {
        ShFree(n);
        return nullptr;
    }
    n->Next = b->Nodes;
    b->Nodes = n;
    return n;
}

static BROWSE_NODE *node_of(BROWSE *b, HTREEITEM item)
{
    if (!item)
        return nullptr;
    TVITEMW tv = {.mask = TVIF_PARAM, .hItem = item};
    if (!SendMessageW(b->Tree, TVM_GETITEMW, 0, (LPARAM)&tv))
        return nullptr;
    return (BROWSE_NODE *)tv.lParam;
}

static HTREEITEM selected_item(BROWSE *b)
{
    return (HTREEITEM)SendMessageW(b->Tree, TVM_GETNEXTITEM, TVGN_CARET, 0);
}

static HTREEITEM first_child(BROWSE *b, HTREEITEM item)
{
    return (HTREEITEM)SendMessageW(b->Tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)item);
}

static HTREEITEM next_sibling(BROWSE *b, HTREEITEM item)
{
    return (HTREEITEM)SendMessageW(b->Tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)item);
}

/* the desktop is the empty list. */
static PIDLIST_ABSOLUTE node_pidl(const BROWSE_NODE *n)
{
    switch (n->Kind) {
    case NODE_DESKTOP:
        return ShAllocZero(sizeof(USHORT));
    case NODE_COMPUTER:
        return ShComputerList();
    default:
        return ILCreateFromPathW(n->Path);
    }
}

/* the name, system image list index and folder flag of a place, as the shell gives them. */
static BOOL describe(const BROWSE_NODE *n, DWORD attributes, LPCWSTR plain_name, LPWSTR name, int *image)
{
    SHFILEINFOW info = {0};
    UINT flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
    if (n->Kind != NODE_PATH) {
        PIDLIST_ABSOLUTE pidl = node_pidl(n);
        if (!pidl)
            return FALSE;
        DWORD_PTR ok = SHGetFileInfoW((LPCWSTR)pidl, 0, &info, sizeof(info), flags | SHGFI_PIDL | SHGFI_DISPLAYNAME);
        ILFree(pidl);
        if (!ok)
            return FALSE;
        lstrcpynW(name, info.szDisplayName, MAX_PATH);
    } else if (plain_name && !(attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM)) &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        /* a folder without desktop.ini customization has its own name and the folder icon. */
        if (!SHGetFileInfoW(n->Path, FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info), flags | SHGFI_USEFILEATTRIBUTES))
            info.iIcon = 0;
        lstrcpynW(name, plain_name, MAX_PATH);
    } else {
        if (!SHGetFileInfoW(n->Path, 0, &info, sizeof(info), flags | SHGFI_DISPLAYNAME))
            return FALSE;
        lstrcpynW(name, info.szDisplayName[0] ? info.szDisplayName : (plain_name ? plain_name : n->Path), MAX_PATH);
    }
    *image = info.iIcon;
    return TRUE;
}

static HTREEITEM insert_node(BROWSE *b, HTREEITEM parent, BROWSE_NODE *n, DWORD attributes, LPCWSTR plain_name,
                             int children)
{
    WCHAR name[MAX_PATH];
    if (!describe(n, attributes, plain_name, name, &n->Image))
        return nullptr;
    TVINSERTSTRUCTW insert = {0};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
    insert.item.pszText = name;
    insert.item.iImage = n->Image;
    insert.item.iSelectedImage = n->Image;
    insert.item.lParam = (LPARAM)n;
    insert.item.cChildren = children;
    return (HTREEITEM)SendMessageW(b->Tree, TVM_INSERTITEMW, 0, (LPARAM)&insert);
}

static void set_children(BROWSE *b, HTREEITEM item, int children)
{
    TVITEMW tv = {.mask = TVIF_CHILDREN, .hItem = item, .cChildren = children};
    SendMessageW(b->Tree, TVM_SETITEMW, 0, (LPARAM)&tv);
}

static BOOL join_path(LPCWSTR folder, LPCWSTR name, LPWSTR path)
{
    SIZE_T length = wcslen(folder);
    BOOL slash = length && folder[length - 1] == L'\\';
    if (length + wcslen(name) + 2 > MAX_PATH)
        return FALSE;
    wcscpy(path, folder);
    if (!slash)
        wcscat(path, L"\\");
    wcscat(path, name);
    return TRUE;
}

/* the old dialog shows an expando only where there is a folder below. */
static BOOL has_subfolder(BROWSE *b, LPCWSTR path)
{
    WCHAR pattern[MAX_PATH];
    if (!join_path(path, L"*", pattern))
        return FALSE;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchLimitToDirectories, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL found = FALSE;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(b->Info.ulFlags & BIF_BROWSEINCLUDEFILES))
            continue;
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
            continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) && !b->ShowHidden)
            continue;
        found = TRUE;
    } while (!found && FindNextFileW(find, &data));
    FindClose(find);
    return found;
}

typedef struct _ENTRY {
    WCHAR Name[MAX_PATH];
    DWORD Attributes;
} ENTRY;

/* a shell sort, in the logical name order of the shell. */
static void sort_entries(ENTRY *entries, UINT count)
{
    static const UINT gaps[] = {1750, 701, 301, 132, 57, 23, 10, 4, 1};
    ENTRY held;
    for (UINT g = 0; g < RTL_NUMBER_OF(gaps); g++) {
        UINT gap = gaps[g];
        for (UINT i = gap; i < count; i++) {
            held = entries[i];
            UINT j = i;
            for (; j >= gap && StrCmpLogicalW(entries[j - gap].Name, held.Name) > 0; j -= gap)
                entries[j] = entries[j - gap];
            entries[j] = held;
        }
    }
}

/* the program's IFolderFilter decides on each item of a folder, through the folder's IShellFolder. */
static BOOL filter_allows(BROWSE *b, IShellFolder *folder, PCIDLIST_ABSOLUTE folder_pidl, LPCWSTR path)
{
    if (!b->Filter || !folder)
        return TRUE;
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path);
    if (!pidl)
        return TRUE;
    HRESULT hr = b->Filter->lpVtbl->ShouldShow(b->Filter, folder, folder_pidl, ILFindLastID(pidl));
    ILFree(pidl);
    return hr == S_OK;
}

/* the folders, and with BIF_BROWSEINCLUDEFILES the files, of a directory in logical name order. */
static void fill_directory(BROWSE *b, HTREEITEM item, LPCWSTR directory)
{
    WCHAR pattern[MAX_PATH];
    if (!join_path(directory, L"*", pattern))
        return;
    DWORD flags = SHCONTF_FOLDERS;
    if (b->Info.ulFlags & BIF_BROWSEINCLUDEFILES)
        flags |= SHCONTF_NONFOLDERS;
    if (b->ShowHidden)
        flags |= SHCONTF_INCLUDEHIDDEN;
    IShellFolder *folder = nullptr;
    PIDLIST_ABSOLUTE folder_pidl = nullptr;
    if (b->Filter && (folder_pidl = ILCreateFromPathW(directory)) &&
        SUCCEEDED(ShBindAbsolute(folder_pidl, &IID_IShellFolder, (void **)&folder))) {
        HWND owner = b->Dialog;
        b->Filter->lpVtbl->GetEnumFlags(b->Filter, folder, folder_pidl, &owner, &flags);
    }
    ENTRY *entries = nullptr;
    UINT count = 0, capacity = 0;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
                continue;
            BOOL dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (dir ? !(flags & SHCONTF_FOLDERS) : !(flags & SHCONTF_NONFOLDERS))
                continue;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) && !(flags & SHCONTF_INCLUDEHIDDEN))
                continue;
            /* protected operating system files stay hidden while the shell hides them. */
            if ((data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) ==
                    (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM) &&
                !b->ShowSuper)
                continue;
            if (count == capacity) {
                UINT grown = capacity ? capacity * 2 : 64;
                ENTRY *bigger = ShAlloc(grown * sizeof(*bigger));
                if (!bigger)
                    break;
                if (entries)
                    memcpy(bigger, entries, count * sizeof(*entries));
                ShFree(entries);
                entries = bigger;
                capacity = grown;
            }
            wcscpy(entries[count].Name, data.cFileName);
            entries[count].Attributes = data.dwFileAttributes;
            count++;
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    sort_entries(entries, count);
    /* folders first, as the shell sorts them. */
    for (int pass = 0; pass < 2; pass++) {
        for (UINT i = 0; i < count; i++) {
            BOOL dir = (entries[i].Attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (dir != !pass)
                continue;
            WCHAR path[MAX_PATH];
            if (!join_path(directory, entries[i].Name, path) || !filter_allows(b, folder, folder_pidl, path))
                continue;
            BROWSE_NODE *n = new_node(b, NODE_PATH, path);
            if (!n)
                break;
            n->Folder = dir;
            int children = dir ? (b->NewStyle ? 1 : has_subfolder(b, path)) : 0;
            insert_node(b, item, n, entries[i].Attributes, entries[i].Name, children);
        }
    }
    ShFree(entries);
    if (folder)
        folder->lpVtbl->Release(folder);
    ILFree(folder_pidl);
}

static void add_path(BROWSE *b, HTREEITEM item, LPCWSTR path, BOOL fixed)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return;
    BROWSE_NODE *n = new_node(b, NODE_PATH, path);
    if (!n)
        return;
    n->Fixed = fixed;
    insert_node(b, item, n, 0, nullptr, b->NewStyle ? 1 : has_subfolder(b, path));
}

static void fill(BROWSE *b, HTREEITEM item)
{
    BROWSE_NODE *n = node_of(b, item);
    if (!n || n->Filled)
        return;
    n->Filled = TRUE;
    if (n->Kind == NODE_DESKTOP) {
        /* the user's folders, This PC, then what lies on the desktop. */
        static const KNOWNFOLDERID *const places[] = {
            &FOLDERID_Desktop, &FOLDERID_Documents, &FOLDERID_Downloads, &FOLDERID_Music,
            &FOLDERID_Pictures, &FOLDERID_Videos,   &FOLDERID_Profile,
        };
        for (UINT i = 0; i < RTL_NUMBER_OF(places); i++) {
            PWSTR path;
            if (SUCCEEDED(SHGetKnownFolderPath(places[i], 0, nullptr, &path))) {
                add_path(b, item, path, TRUE);
                CoTaskMemFree(path);
            }
        }
        BROWSE_NODE *computer = new_node(b, NODE_COMPUTER, nullptr);
        if (computer)
            insert_node(b, item, computer, 0, nullptr, 1);
        PWSTR desktop;
        if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Desktop, 0, nullptr, &desktop))) {
            fill_directory(b, item, desktop);
            CoTaskMemFree(desktop);
        }
    } else if (n->Kind == NODE_COMPUTER) {
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            WCHAR root[] = {(WCHAR)(L'A' + i), L':', L'\\', 0};
            if (!(drives & (1u << i)))
                continue;
            BROWSE_NODE *drive = new_node(b, NODE_PATH, root);
            if (!drive)
                break;
            drive->Fixed = TRUE;
            insert_node(b, item, drive, 0, nullptr, 1);
        }
    } else if (n->Folder) {
        fill_directory(b, item, n->Path);
    }
    if (!first_child(b, item))
        set_children(b, item, 0);
}

/* ------------------------------------------------------------------ selection */

static BOOL node_is_file_system(const BROWSE_NODE *n)
{
    return n->Kind == NODE_PATH || n->Kind == NODE_DESKTOP;
}

/* the folder a node stands for on disk: the desktop is the user's desktop directory. */
static BOOL node_directory(const BROWSE_NODE *n, LPWSTR path)
{
    if (n->Kind == NODE_PATH) {
        if (!n->Folder)
            return FALSE;
        wcscpy(path, n->Path);
        return TRUE;
    }
    if (n->Kind != NODE_DESKTOP)
        return FALSE;
    PWSTR desktop;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_Desktop, 0, nullptr, &desktop)))
        return FALSE;
    lstrcpynW(path, desktop, MAX_PATH);
    CoTaskMemFree(desktop);
    return TRUE;
}

/* what the flags let the dialog return, from _DoesMatchFilter with the item checked for OK. */
static BOOL node_allowed(BROWSE *b, const BROWSE_NODE *n)
{
    UINT flags = b->Info.ulFlags;
    if (flags & (BIF_BROWSEFORCOMPUTER | BIF_BROWSEFORPRINTER | BIF_DONTGOBELOWDOMAIN))
        return FALSE;
    if (!n->Folder && !(flags & BIF_BROWSEINCLUDEFILES))
        return FALSE;
    if (flags & BIF_RETURNONLYFSDIRS)
        return node_is_file_system(n);
    return TRUE;
}

static void node_name(BROWSE *b, HTREEITEM item, LPWSTR name)
{
    name[0] = 0;
    TVITEMW tv = {.mask = TVIF_TEXT, .hItem = item, .pszText = name, .cchTextMax = MAX_PATH};
    SendMessageW(b->Tree, TVM_GETITEMW, 0, (LPARAM)&tv);
}

/* windows fills the edit box also when it is hidden. */
static void set_edit_text(BROWSE *b, LPCWSTR text)
{
    HWND edit = GetDlgItem(b->Dialog, IDC_EDIT);
    if (edit)
        SetWindowTextW(edit, text);
    b->EditChanged = FALSE;
}

static void tell_selection(BROWSE *b)
{
    BROWSE_NODE *n = node_of(b, selected_item(b));
    if (!n)
        return;
    PIDLIST_ABSOLUTE pidl = node_pidl(n);
    if (!pidl)
        return;
    callback(b, BFFM_SELCHANGED, (LPARAM)pidl);
    ILFree(pidl);
}

static void on_selection(BROWSE *b)
{
    HTREEITEM item = selected_item(b);
    BROWSE_NODE *n = node_of(b, item);
    if (!n)
        return;
    WCHAR name[MAX_PATH], directory[MAX_PATH];
    node_name(b, item, name);
    set_edit_text(b, name);
    EnableWindow(GetDlgItem(b->Dialog, IDC_NEW_FOLDER), node_directory(n, directory));
    EnableWindow(GetDlgItem(b->Dialog, IDOK), node_allowed(b, n));
    if (b->Quiet)
        PostMessageW(b->Dialog, WM_BROWSE_SELCHANGED, 0, 0);
    else
        tell_selection(b);
}

static HTREEITEM find_child(BROWSE *b, HTREEITEM parent, int kind, LPCWSTR path)
{
    fill(b, parent);
    for (HTREEITEM child = first_child(b, parent); child; child = next_sibling(b, child)) {
        BROWSE_NODE *n = node_of(b, child);
        if (n && n->Kind == kind && (kind != NODE_PATH || !lstrcmpiW(n->Path, path)))
            return child;
    }
    return nullptr;
}

/* the item of a path below the root, filling the folders on the way. */
static HTREEITEM find_path(BROWSE *b, LPCWSTR text)
{
    WCHAR full[MAX_PATH];
    DWORD length = GetFullPathNameW(text, MAX_PATH, full, nullptr);
    if (!length || length >= MAX_PATH)
        return nullptr;
    if (length > 3 && full[length - 1] == L'\\')
        full[--length] = 0;
    DWORD attributes = GetFileAttributesW(full);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return nullptr;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) && !(b->Info.ulFlags & BIF_BROWSEINCLUDEFILES))
        return nullptr;
    BROWSE_NODE *root = node_of(b, b->Root);
    if (!root)
        return nullptr;
    HTREEITEM at = b->Root;
    LPCWSTR rest;
    if (root->Kind == NODE_PATH) {
        SIZE_T n = wcslen(root->Path);
        if (_wcsnicmp(full, root->Path, n))
            return nullptr;
        if (root->Path[n - 1] != L'\\' && full[n] && full[n] != L'\\')
            return nullptr;
        rest = full + n;
    } else {
        if (root->Kind == NODE_DESKTOP && !(at = find_child(b, at, NODE_COMPUTER, nullptr)))
            return nullptr;
        if (length < 3 || full[1] != L':')
            return nullptr;
        WCHAR drive[] = {full[0], L':', L'\\', 0};
        if (!(at = find_child(b, at, NODE_PATH, drive)))
            return nullptr;
        rest = full + 3;
    }
    while (*rest) {
        while (*rest == L'\\')
            rest++;
        if (!*rest)
            break;
        LPCWSTR end = wcschr(rest, L'\\');
        SIZE_T prefix = end ? (SIZE_T)(end - full) : wcslen(full);
        WCHAR step[MAX_PATH];
        memcpy(step, full, prefix * sizeof(WCHAR));
        step[prefix] = 0;
        if (!(at = find_child(b, at, NODE_PATH, step)))
            return nullptr;
        rest = end ? end : full + wcslen(full);
    }
    return at;
}

static HTREEITEM find_pidl(BROWSE *b, PCIDLIST_ABSOLUTE pidl)
{
    WCHAR path[MAX_PATH];
    if (!pidl)
        return nullptr;
    BROWSE_NODE *root = node_of(b, b->Root);
    if (!pidl->mkid.cb)
        return root && root->Kind == NODE_DESKTOP ? b->Root : nullptr;
    if (ShIsComputerItem((const BYTE *)ILFindLastID(pidl)) && ILGetSize(pidl) == ILGetSize(ILFindLastID(pidl))) {
        if (root && root->Kind == NODE_COMPUTER)
            return b->Root;
        return root && root->Kind == NODE_DESKTOP ? find_child(b, b->Root, NODE_COMPUTER, nullptr) : nullptr;
    }
    if (!SHGetPathFromIDListW(pidl, path))
        return nullptr;
    return find_path(b, path);
}

static BOOL select_item(BROWSE *b, HTREEITEM item, BOOL expand)
{
    if (!item)
        return FALSE;
    SendMessageW(b->Tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)item);
    if (expand)
        SendMessageW(b->Tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)item);
    SendMessageW(b->Tree, TVM_ENSUREVISIBLE, 0, (LPARAM)item);
    return TRUE;
}

/* BFFM_SETSELECTION and BFFM_SETEXPANDED. */
static BOOL set_selection(BROWSE *b, BOOL is_path, LPARAM where, BOOL ansi, BOOL expand)
{
    HTREEITEM item;
    if (is_path) {
        WCHAR path[MAX_PATH];
        if (!where)
            return FALSE;
        if (ansi)
            MultiByteToWideChar(CP_ACP, 0, (LPCSTR)where, -1, path, MAX_PATH);
        else
            lstrcpynW(path, (LPCWSTR)where, MAX_PATH);
        item = find_path(b, path);
    } else {
        item = find_pidl(b, (PCIDLIST_ABSOLUTE)where);
    }
    b->Quiet = TRUE;
    BOOL done = select_item(b, item, expand);
    b->Quiet = FALSE;
    return done;
}

/* ------------------------------------------------------------------ making and renaming folders */

static void show_error(BROWSE *b, LPCWSTR name, DWORD error)
{
    WCHAR message[512], text[700], title[100];
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, message,
                        RTL_NUMBER_OF(message), nullptr))
        message[0] = 0;
    _snwprintf(text, RTL_NUMBER_OF(text), L"Unable to create the folder '%ls'\n\n%ls", name, message);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    GetWindowTextW(b->Dialog, title, RTL_NUMBER_OF(title));
    MessageBoxW(b->Dialog, text, title, MB_OK | MB_ICONERROR);
}

static void new_folder(BROWSE *b)
{
    HTREEITEM parent = selected_item(b);
    BROWSE_NODE *pn = node_of(b, parent);
    WCHAR directory[MAX_PATH], path[MAX_PATH], name[64];
    if (!pn || !node_directory(pn, directory))
        return;
    for (int i = 1;; i++) {
        if (i == 1)
            wcscpy(name, L"New folder");
        else
            _snwprintf(name, RTL_NUMBER_OF(name), L"New folder (%d)", i);
        if (!join_path(directory, name, path)) {
            show_error(b, name, ERROR_FILENAME_EXCED_RANGE);
            return;
        }
        if (CreateDirectoryW(path, nullptr))
            break;
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS || i > 9999) {
            show_error(b, name, error);
            return;
        }
    }
    BOOL filled = pn->Filled;
    fill(b, parent);
    HTREEITEM item = nullptr;
    if (filled) {
        BROWSE_NODE *n = new_node(b, NODE_PATH, path);
        if (n)
            item = insert_node(b, parent, n, FILE_ATTRIBUTE_DIRECTORY, name, 0);
    } else {
        item = find_child(b, parent, NODE_PATH, path);
    }
    set_children(b, parent, 1);
    SendMessageW(b->Tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)parent);
    if (!item)
        return;
    select_item(b, item, FALSE);
    SetFocus(b->Tree);
    SendMessageW(b->Tree, TVM_EDITLABELW, 0, (LPARAM)item);
}

static int CALLBACK compare_names(LPARAM a, LPARAM b, LPARAM tree)
{
    const BROWSE_NODE *na = (const BROWSE_NODE *)a, *nb = (const BROWSE_NODE *)b;
    if (na->Kind != NODE_PATH || nb->Kind != NODE_PATH || na->Fixed || nb->Fixed)
        return 0;
    if (na->Folder != nb->Folder)
        return na->Folder ? -1 : 1;
    LPCWSTR sa = wcsrchr(na->Path, L'\\'), sb = wcsrchr(nb->Path, L'\\');
    return StrCmpLogicalW(sa ? sa + 1 : na->Path, sb ? sb + 1 : nb->Path);
}

static BOOL rename_node(BROWSE *b, HTREEITEM item, LPCWSTR text)
{
    BROWSE_NODE *n = node_of(b, item);
    if (!n || !text || !text[0])
        return FALSE;
    for (LPCWSTR c = text; *c; c++) {
        if (wcschr(L"\\/:*?\"<>|", *c)) {
            MessageBeep(MB_ICONWARNING);
            return FALSE;
        }
    }
    LPCWSTR slash = wcsrchr(n->Path, L'\\');
    if (!slash || !wcscmp(slash + 1, text))
        return FALSE;
    WCHAR directory[MAX_PATH], path[MAX_PATH];
    lstrcpynW(directory, n->Path, (int)(slash - n->Path) + 1);
    if (!join_path(directory, text, path))
        return FALSE;
    if (!MoveFileW(n->Path, path)) {
        WCHAR message[512], title[100];
        if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), 0,
                            message, RTL_NUMBER_OF(message), nullptr))
            message[0] = 0;
        GetWindowTextW(b->Dialog, title, RTL_NUMBER_OF(title));
        MessageBoxW(b->Dialog, message, title, MB_OK | MB_ICONERROR);
        return FALSE;
    }
    LPWSTR copy = ShDuplicate(path);
    if (!copy)
        return FALSE;
    CoTaskMemFree(n->Path);
    n->Path = copy;
    /* the children were made under the old name. */
    HTREEITEM child;
    while ((child = first_child(b, item)))
        SendMessageW(b->Tree, TVM_DELETEITEM, 0, (LPARAM)child);
    n->Filled = FALSE;
    set_children(b, item, 1);
    return TRUE;
}

/* ------------------------------------------------------------------ the namespace window of the new dialog */

static LRESULT CALLBACK host_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_SIZE: {
        HWND tree = GetDlgItem(hwnd, NAMESPACE_TREE_ID);
        if (tree)
            MoveWindow(tree, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        return 0;
    }
    case WM_SETFOCUS: {
        HWND tree = GetDlgItem(hwnd, NAMESPACE_TREE_ID);
        if (tree)
            SetFocus(tree);
        return 0;
    }
    case WM_NOTIFY:
    case WM_CONTEXTMENU:
        return SendMessageW(GetParent(hwnd), message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static BOOL register_host(void)
{
    WNDCLASSW wc = {0};
    if (GetClassInfoW(g_shell32, g_host_class, &wc))
        return TRUE;
    wc.lpfnWndProc = host_proc;
    wc.hInstance = g_shell32;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = g_host_class;
    return RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

typedef HRESULT(WINAPI *SET_WINDOW_THEME)(HWND, LPCWSTR, LPCWSTR);

static void theme_tree(HWND tree)
{
    HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
    SET_WINDOW_THEME set = uxtheme ? (SET_WINDOW_THEME)(void *)GetProcAddress(uxtheme, "SetWindowTheme") : nullptr;
    if (set)
        set(tree, L"Explorer", nullptr);
}

static void setup_tree(BROWSE *b)
{
    struct _IMAGELIST *small = nullptr;
    if (Shell_GetImageLists(nullptr, &small))
        SendMessageW(b->Tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)small);
    theme_tree(b->Tree);
}

/* ------------------------------------------------------------------ sizing of the new dialog */

static void set_dialog_size(BROWSE *b, int width, int height)
{
    SetWindowPos(GetDlgItem(b->Dialog, IDC_GRIP), nullptr, width - b->GripW, height - b->GripH, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    /* windows adjusts with this fixed style and no extended style. */
    RECT r = {0, 0, width, height};
    AdjustWindowRect(&r, 0x80CC2284, FALSE);
    SetWindowPos(b->Dialog, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static BOOL work_area(HWND hwnd, RECT *work)
{
    MONITORINFO mi = {.cbSize = sizeof(mi)};
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor || !GetMonitorInfoW(monitor, &mi))
        return FALSE;
    *work = mi.rcWork;
    return TRUE;
}

static void initial_size(BROWSE *b)
{
    DWORD width = 0, height = 0, size = sizeof(DWORD);
    RECT work;
    if (!work_area(b->Dialog, &work))
        return;
    if (!RegGetValueW(HKEY_CURRENT_USER, g_size_key, L"Browse For Folder Width", RRF_RT_REG_DWORD, nullptr, &width,
                      &size) &&
        (size = sizeof(DWORD), !RegGetValueW(HKEY_CURRENT_USER, g_size_key, L"Browse For Folder Height",
                                             RRF_RT_REG_DWORD, nullptr, &height, &size))) {
        if (width < (DWORD)(work.right - work.left) && height < (DWORD)(work.bottom - work.top)) {
            set_dialog_size(b, width < (DWORD)b->MinW ? b->MinW : (int)width,
                            height < (DWORD)b->MinH ? b->MinH : (int)height);
        }
        return;
    }
    /* a third of the work area's height, for the height and in the template's ratio for the width. */
    UINT third = (UINT)(work.bottom - work.top) / 3;
    UINT wide = third * (UINT)b->MinH / (UINT)b->MinW;
    if (wide > 1200)
        wide = 1200;
    if (third > 1600)
        third = 1600;
    set_dialog_size(b, wide >= (UINT)b->MinW ? (int)wide : b->MinW, third >= (UINT)b->MinH ? (int)third : b->MinH);
}

static void save_size(BROWSE *b)
{
    if (!b->NewStyle)
        return;
    RECT r;
    GetClientRect(b->Dialog, &r);
    DWORD width = (DWORD)(r.right - r.left), height = (DWORD)(r.bottom - r.top);
    RegSetKeyValueW(HKEY_CURRENT_USER, g_size_key, L"Browse For Folder Width", REG_DWORD, &width, sizeof(width));
    RegSetKeyValueW(HKEY_CURRENT_USER, g_size_key, L"Browse For Folder Height", REG_DWORD, &height, sizeof(height));
}

static void on_size(BROWSE *b, int client_w, int client_h)
{
    RECT window;
    GetWindowRect(b->Dialog, &window);
    int w = window.right - window.left, h = window.bottom - window.top;
    if (w < b->MinW)
        w = b->MinW;
    if (h < b->MinH)
        h = b->MinH;
    int dx = w - b->WinW, dy = h - b->WinH;
    b->WinW = w;
    b->WinH = h;
    if (!dx && !dy)
        return;
    SetWindowPos(GetDlgItem(b->Dialog, IDC_GRIP), nullptr, client_w - b->GripW, client_h - b->GripH, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    HDWP defer = BeginDeferWindowPos(15);
    if (!defer)
        return;
    RECT r;
    GetWindowRect(b->Host, &r);
    MapWindowPoints(nullptr, b->Dialog, (POINT *)&r, 2);
    defer = DeferWindowPos(defer, b->Host, nullptr, 0, 0, r.right - r.left + dx, r.bottom - r.top + dy,
                           SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (HWND child = GetWindow(b->Dialog, GW_CHILD); child && defer; child = GetWindow(child, GW_HWNDNEXT)) {
        GetWindowRect(child, &r);
        MapWindowPoints(nullptr, b->Dialog, (POINT *)&r, 2);
        UINT move = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE, both = SWP_NOZORDER | SWP_NOACTIVATE;
        switch (GetDlgCtrlID(child)) {
        case IDOK:
        case IDCANCEL:
            defer = DeferWindowPos(defer, child, nullptr, r.left + dx, r.top + dy, 0, 0, move);
            break;
        case IDC_TITLE:
            defer = DeferWindowPos(defer, child, nullptr, r.left, r.top, r.right - r.left + dx, r.bottom - r.top, both);
            InvalidateRect(child, nullptr, TRUE);
            break;
        case IDC_EDIT:
        case IDC_HINT:
            defer = DeferWindowPos(defer, child, nullptr, r.left, r.top + dy, r.right - r.left + dx, r.bottom - r.top,
                                   both);
            break;
        case IDC_NEW_FOLDER:
        case IDC_FOLDER_LABEL:
            defer = DeferWindowPos(defer, child, nullptr, r.left, r.top + dy, 0, 0, move);
            break;
        }
    }
    if (defer)
        EndDeferWindowPos(defer);
}

/* ------------------------------------------------------------------ the dialog */

static void hide_control(HWND dialog, int id)
{
    HWND control = GetDlgItem(dialog, id);
    EnableWindow(control, FALSE);
    ShowWindow(control, SW_HIDE);
}

static void set_title(BROWSE *b, HWND control)
{
    LPCWSTR title = b->Info.lpszTitle;
    if (!title)
        return;
    if (IS_INTRESOURCE(title)) {
        WCHAR text[260];
        if (LoadStringW(g_shell32, (UINT)(ULONG_PTR)title, text, RTL_NUMBER_OF(text)))
            SetWindowTextW(control, text);
        return;
    }
    SetWindowTextW(control, title);
}

static void adjust_caption(BROWSE *b)
{
    LPCWSTR caption = nullptr;
    if (b->Info.ulFlags & BIF_BROWSEFORCOMPUTER)
        caption = L"Browse for Computer";
    else if (b->Info.ulFlags & BIF_BROWSEFORPRINTER)
        caption = L"Browse for Printer";
    else if (b->Info.ulFlags & BIF_BROWSEINCLUDEFILES)
        caption = L"Browse for Files or Folders";
    if (caption)
        SetWindowTextW(b->Dialog, caption);
}

/* the root the program asked for: a list, a CSIDL below 0x10000, or the desktop. */
static HTREEITEM make_root(BROWSE *b)
{
    PCIDLIST_ABSOLUTE given = b->Info.pidlRoot;
    PIDLIST_ABSOLUTE special = nullptr;
    BROWSE_NODE *n = nullptr;
    if (given && (ULONG_PTR)given < 0x10000) {
        if (FAILED(SHGetSpecialFolderLocation(nullptr, (int)(ULONG_PTR)given, &special)))
            special = nullptr;
        given = special;
    }
    WCHAR path[MAX_PATH];
    if (!given || !given->mkid.cb) {
        n = new_node(b, NODE_DESKTOP, nullptr);
    } else if (ShIsComputerItem((const BYTE *)ILFindLastID(given)) &&
               ILGetSize(given) == ILGetSize(ILFindLastID(given))) {
        n = new_node(b, NODE_COMPUTER, nullptr);
    } else if (SHGetPathFromIDListW(given, path)) {
        n = new_node(b, NODE_PATH, path);
        if (n)
            n->Fixed = TRUE;
    } else {
        n = new_node(b, NODE_DESKTOP, nullptr);
    }
    ILFree(special);
    if (!n)
        return nullptr;
    HTREEITEM root = insert_node(b, TVI_ROOT, n, 0, nullptr, 1);
    if (root) {
        b->Root = root;
        fill(b, root);
        SendMessageW(b->Tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)root);
    }
    return root;
}

static void init_new(BROWSE *b, HWND dialog)
{
    RECT rect;
    HWND placeholder = GetDlgItem(dialog, IDC_TREE);
    GetWindowRect(placeholder, &rect);
    if ((b->Info.ulFlags & (BIF_EDITBOX | BIF_UAHINT)) == (BIF_EDITBOX | BIF_UAHINT))
        b->Info.ulFlags &= ~BIF_UAHINT;
    if (b->Info.ulFlags & BIF_NONEWFOLDERBUTTON)
        hide_control(dialog, IDC_NEW_FOLDER);
    LONG bottom = 0;
    if (!(b->Info.ulFlags & BIF_EDITBOX)) {
        RECT edit;
        hide_control(dialog, IDC_EDIT);
        hide_control(dialog, IDC_FOLDER_LABEL);
        GetWindowRect(GetDlgItem(dialog, IDC_EDIT), &edit);
        bottom = edit.bottom;
    } else {
        b->Edit = GetDlgItem(dialog, IDC_EDIT);
    }
    if (!(b->Info.ulFlags & BIF_UAHINT))
        hide_control(dialog, IDC_HINT);
    if (!(b->Info.ulFlags & (BIF_EDITBOX | BIF_UAHINT)))
        rect.bottom = bottom;
    hide_control(dialog, IDC_TREE);
    MapWindowPoints(nullptr, dialog, (POINT *)&rect, 2);

    register_host();
    WCHAR title[260] = {0};
    if (b->Info.lpszTitle && !IS_INTRESOURCE(b->Info.lpszTitle))
        lstrcpynW(title, b->Info.lpszTitle, RTL_NUMBER_OF(title));
    b->Host = CreateWindowExW(WS_EX_CONTROLPARENT, g_host_class, title,
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_BORDER, rect.left,
                              rect.top, rect.right - rect.left, rect.bottom - rect.top, dialog, nullptr, g_shell32,
                              nullptr);
    if (!b->Host)
        return;
    SetWindowPos(b->Host, placeholder, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    /* the name of the tree for accessibility, as windows keeps it. */
    CreateWindowExW(0, L"Static", title, WS_CHILD, 0, 0, 0, 0, b->Host, nullptr, g_shell32, nullptr);
    RECT client;
    GetClientRect(b->Host, &client);
    b->Tree = CreateWindowExW(0, WC_TREEVIEWW, L"Navigation Pane",
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP | WS_VSCROLL |
                                  TVS_HASBUTTONS | TVS_EDITLABELS | TVS_SHOWSELALWAYS | TVS_TRACKSELECT |
                                  TVS_SINGLEEXPAND | TVS_INFOTIP | TVS_NONEVENHEIGHT,
                              0, 0, client.right, client.bottom, b->Host, (HMENU)(ULONG_PTR)NAMESPACE_TREE_ID,
                              g_shell32, nullptr);
    if (!b->Tree)
        return;
    LOGFONTW lf;
    if (SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0) && (b->Font = CreateFontIndirectW(&lf)))
        SendMessageW(b->Tree, WM_SETFONT, (WPARAM)b->Font, FALSE);
    setup_tree(b);
    SendMessageW(b->Tree, TVM_SETBKCOLOR, 0, (LPARAM)GetSysColor(COLOR_WINDOW));
    /* the namespace tree is as tall as the font or the small icons, with two pixels above and below. */
    HDC dc = GetDC(b->Tree);
    if (dc) {
        TEXTMETRICW tm;
        HGDIOBJ old = SelectObject(dc, (HGDIOBJ)SendMessageW(b->Tree, WM_GETFONT, 0, 0));
        if (GetTextMetricsW(dc, &tm)) {
            int icon = GetSystemMetrics(SM_CYSMICON);
            SendMessageW(b->Tree, TVM_SETITEMHEIGHT, (tm.tmHeight > icon ? tm.tmHeight : icon) + 4, 0);
        }
        SelectObject(dc, old);
        ReleaseDC(b->Tree, dc);
    }
    SendMessageW(b->Tree, TVM_SETEXTENDEDSTYLE,
                 TVS_EX_NOSINGLECOLLAPSE | TVS_EX_DOUBLEBUFFER | TVS_EX_RICHTOOLTIP | TVS_EX_DRAWIMAGEASYNC,
                 TVS_EX_NOSINGLECOLLAPSE | TVS_EX_DOUBLEBUFFER | TVS_EX_RICHTOOLTIP | TVS_EX_DRAWIMAGEASYNC);
}

static void init_old(BROWSE *b, HWND dialog)
{
    b->Tree = GetDlgItem(dialog, IDC_TREE);
    if (!b->Tree)
        return;
    /* windows places the tree at its own client rectangle, which takes the border off each side. */
    UINT flags = SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;
    RECT rect;
    GetClientRect(b->Tree, &rect);
    MapWindowPoints(b->Tree, dialog, (POINT *)&rect, 2);
    if (!(b->Info.ulFlags & BIF_STATUSTEXT)) {
        HWND status = GetDlgItem(dialog, IDC_STATUS);
        POINT at = {0, 0};
        ShowWindow(status, SW_HIDE);
        MapWindowPoints(status, dialog, &at, 1);
        rect.top = at.y;
        flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;
    } else {
        b->Status = GetDlgItem(dialog, IDC_STATUS);
    }
    HWND edit = GetDlgItem(dialog, IDC_EDIT);
    if (b->Info.ulFlags & BIF_EDITBOX) {
        RECT e;
        GetClientRect(edit, &e);
        SetWindowPos(edit, nullptr, rect.left, rect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        rect.top += e.bottom - e.top + 4 * GetSystemMetrics(SM_CYEDGE);
        flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;
        b->Edit = edit;
    } else {
        DestroyWindow(edit);
    }
    setup_tree(b);
    SendMessageW(b->Tree, TVM_SETEXTENDEDSTYLE, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    int height = (int)SendMessageW(b->Tree, TVM_GETITEMHEIGHT, 0, 0);
    SendMessageW(b->Tree, TVM_SETITEMHEIGHT, height + 1, 0);
    SetWindowPos(b->Tree, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, flags);
}

static void on_init(BROWSE *b, HWND dialog)
{
    b->Dialog = dialog;
    SHELLSTATEW state = {0};
    SHGetSetSettings(&state, SSF_SHOWALLOBJECTS | SSF_SHOWSUPERHIDDEN, FALSE);
    b->ShowHidden = state.fShowAllObjects;
    b->ShowSuper = state.fShowSuperHidden;
    set_title(b, GetDlgItem(dialog, IDC_TITLE));
    if (b->NewStyle)
        init_new(b, dialog);
    else
        init_old(b, dialog);
    if (b->Tree) {
        HTREEITEM root = make_root(b);
        /* without a root of its own the new dialog starts on the user's folder and the old one on This PC, which it
           opens. */
        HTREEITEM start = root;
        PWSTR profile;
        if (root && !b->Info.pidlRoot && !b->NewStyle) {
            HTREEITEM computer = find_child(b, root, NODE_COMPUTER, nullptr);
            if (computer) {
                SendMessageW(b->Tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)computer);
                start = computer;
            }
        } else if (root && !b->Info.pidlRoot && SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Profile, 0, nullptr,
                                                                               &profile))) {
            for (HTREEITEM child = first_child(b, root); child; child = next_sibling(b, child)) {
                BROWSE_NODE *n = node_of(b, child);
                if (n && n->Kind == NODE_PATH && n->Fixed && !lstrcmpiW(n->Path, profile)) {
                    start = child;
                    break;
                }
            }
            CoTaskMemFree(profile);
        }
        select_item(b, start, FALSE);
    }
    adjust_caption(b);
    callback(b, BFFM_INITIALIZED, 0);
    if (b->NewStyle)
        callback(b, BFFM_IUNKNOWN, (LPARAM)&b->Site);
    if (b->NewStyle) {
        RECT r;
        GetClientRect(dialog, &r);
        b->GripW = GetSystemMetrics(SM_CXVSCROLL);
        b->GripH = GetSystemMetrics(SM_CYHSCROLL);
        b->MinW = r.right - r.left;
        b->MinH = r.bottom - r.top;
        GetWindowRect(dialog, &r);
        b->WinW = r.right - r.left;
        b->WinH = r.bottom - r.top;
        initial_size(b);
    }
    if (b->Tree)
        SetFocus(b->Tree);
}

/* the path typed in the edit box, with BIF_VALIDATE's callback when it names nothing. */
static BOOL take_edit(BROWSE *b, HTREEITEM *chosen)
{
    WCHAR text[MAX_PATH], path[MAX_PATH];
    GetWindowTextW(b->Edit, text, RTL_NUMBER_OF(text));
    DWORD n = ExpandEnvironmentStringsW(text, path, RTL_NUMBER_OF(path));
    if (!n || n > RTL_NUMBER_OF(path))
        wcscpy(path, text);
    BOOL rooted = (path[0] && path[1] == L':') || (path[0] == L'\\' && path[1] == L'\\');
    HTREEITEM item = rooted ? find_path(b, path) : nullptr;
    if (item) {
        b->Quiet = TRUE;
        select_item(b, item, FALSE);
        b->Quiet = FALSE;
        *chosen = item;
        return TRUE;
    }
    if (b->Info.ulFlags & BIF_VALIDATE) {
        int keep;
        if (b->Ansi) {
            char ansi[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, path, -1, ansi, sizeof(ansi), nullptr, nullptr);
            keep = callback(b, BFFM_VALIDATEFAILEDA, (LPARAM)ansi);
        } else {
            keep = callback(b, BFFM_VALIDATEFAILEDW, (LPARAM)path);
        }
        if (keep)
            return FALSE;
    }
    return TRUE;
}

static void on_ok(BROWSE *b)
{
    HTREEITEM item = selected_item(b);
    if (b->Edit && b->EditChanged && !take_edit(b, &item))
        return;
    BROWSE_NODE *n = node_of(b, item);
    if (!n || !(b->Result = node_pidl(n)))
        return;
    node_name(b, item, b->Name);
    b->Image = n->Image;
    save_size(b);
    EndDialog(b->Dialog, IDOK);
}

static LRESULT on_notify(BROWSE *b, NMHDR *header)
{
    if (header->hwndFrom != b->Tree)
        return 0;
    switch (header->code) {
    case TVN_ITEMEXPANDINGW: {
        NMTREEVIEWW *nm = (NMTREEVIEWW *)header;
        if ((nm->action & 3) == TVE_EXPAND) {
            fill(b, nm->itemNew.hItem);
            if (!first_child(b, nm->itemNew.hItem))
                return TRUE;
        }
        return 0;
    }
    case TVN_SELCHANGEDW:
        on_selection(b);
        return 0;
    case TVN_BEGINLABELEDITW: {
        NMTVDISPINFOW *nm = (NMTVDISPINFOW *)header;
        BROWSE_NODE *n = node_of(b, nm->item.hItem);
        return !n || n->Kind != NODE_PATH || n->Fixed;
    }
    case TVN_ENDLABELEDITW: {
        NMTVDISPINFOW *nm = (NMTVDISPINFOW *)header;
        if (!nm->item.pszText || !rename_node(b, nm->item.hItem, nm->item.pszText))
            return FALSE;
        PostMessageW(b->Dialog, WM_BROWSE_SELCHANGED, 1, (LPARAM)nm->item.hItem);
        return TRUE;
    }
    }
    return 0;
}

static INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    BROWSE *b = (BROWSE *)GetWindowLongPtrW(hwnd, DWLP_USER);
    if (message == WM_INITDIALOG) {
        b = (BROWSE *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, lparam);
        on_init(b, hwnd);
        return FALSE;
    }
    if (!b)
        return FALSE;
    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDOK:
            on_ok(b);
            return TRUE;
        case IDCANCEL:
            save_size(b);
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        case IDC_NEW_FOLDER:
            new_folder(b);
            return TRUE;
        case IDC_EDIT:
            if (HIWORD(wparam) == EN_CHANGE) {
                WCHAR c[4] = {0};
                GetDlgItemTextW(hwnd, IDC_EDIT, c, RTL_NUMBER_OF(c));
                EnableWindow(GetDlgItem(hwnd, IDOK), c[0] != 0);
                b->EditChanged = TRUE;
            }
            return FALSE;
        }
        return FALSE;
    case WM_CLOSE:
        save_size(b);
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    case WM_DESTROY:
        if (b->NewStyle)
            callback(b, BFFM_IUNKNOWN, 0);
        return FALSE;
    case WM_SIZE:
        if (b->NewStyle && b->Host)
            on_size(b, LOWORD(lparam), HIWORD(lparam));
        return FALSE;
    case WM_GETMINMAXINFO:
        if (b->NewStyle && b->MinW) {
            MINMAXINFO *info = (MINMAXINFO *)lparam;
            info->ptMinTrackSize.x = b->MinW;
            info->ptMinTrackSize.y = b->MinH;
            return TRUE;
        }
        return FALSE;
    case WM_NOTIFY:
        SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, on_notify(b, (NMHDR *)lparam));
        return TRUE;
    case WM_BROWSE_SELCHANGED:
        if (wparam) {
            /* a rename: the folders sort again and the program hears of the new path. */
            TVSORTCB sort = {.hParent = (HTREEITEM)SendMessageW(b->Tree, TVM_GETNEXTITEM, TVGN_PARENT, lparam),
                             .lpfnCompare = compare_names};
            if (sort.hParent)
                SendMessageW(b->Tree, TVM_SORTCHILDRENCB, 0, (LPARAM)&sort);
            SendMessageW(b->Tree, TVM_ENSUREVISIBLE, 0, lparam);
            on_selection(b);
        } else {
            tell_selection(b);
        }
        return TRUE;
    case BFFM_SETSTATUSTEXTA:
        if (b->Status) {
            WCHAR text[260] = {0};
            if (lparam)
                MultiByteToWideChar(CP_ACP, 0, (LPCSTR)lparam, -1, text, RTL_NUMBER_OF(text));
            SetWindowTextW(b->Status, text);
        }
        return TRUE;
    case BFFM_SETSTATUSTEXTW:
        if (b->Status)
            SetWindowTextW(b->Status, (LPCWSTR)lparam);
        return TRUE;
    case BFFM_ENABLEOK:
        EnableWindow(GetDlgItem(hwnd, IDOK), lparam != 0);
        return TRUE;
    case BFFM_SETSELECTIONA:
        return set_selection(b, wparam != 0, lparam, TRUE, FALSE);
    case BFFM_SETSELECTIONW:
        return set_selection(b, wparam != 0, lparam, FALSE, FALSE);
    case BFFM_SETOKTEXT:
        SetDlgItemTextW(hwnd, IDOK, (LPCWSTR)lparam);
        return TRUE;
    case BFFM_SETEXPANDED:
        return set_selection(b, wparam != 0, lparam, FALSE, TRUE);
    }
    return FALSE;
}

static PIDLIST_ABSOLUTE browse(const BROWSEINFOW *info, BOOL ansi, LPWSTR name, int *image)
{
    if (!info)
        return nullptr;
    BROWSE *b = ShAllocZero(sizeof(*b));
    if (!b)
        return nullptr;
    b->Site.lpVtbl = &g_site_vtbl;
    b->Refs = 1;
    b->Info = *info;
    b->Ansi = ansi;
    b->NewStyle = (info->ulFlags & BIF_NEWDIALOGSTYLE) != 0;
    HRESULT co = b->NewStyle ? OleInitialize(nullptr) : CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    /* the tree class is registered when comctl32 loads. */
    LoadLibraryW(L"comctl32.dll");
    HCURSOR cursor = SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_WAIT));
    INT_PTR result = DialogBoxParamW(g_shell32, MAKEINTRESOURCEW(b->NewStyle ? IDD_BROWSE_NEW : IDD_BROWSE_OLD),
                                     info->hwndOwner, dialog_proc, (LPARAM)b);
    if (cursor)
        SetCursor(cursor);
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (result == IDOK && b->Result) {
        pidl = b->Result;
        if (name)
            wcscpy(name, b->Name);
        /* only the old dialog reports the icon. */
        if (!b->NewStyle)
            *image = b->Image;
    } else {
        ILFree(b->Result);
    }
    if (b->Font)
        DeleteObject(b->Font);
    while (b->Nodes) {
        BROWSE_NODE *next = b->Nodes->Next;
        CoTaskMemFree(b->Nodes->Path);
        ShFree(b->Nodes);
        b->Nodes = next;
    }
    b->Site.lpVtbl->Release(&b->Site);
    if (SUCCEEDED(co)) {
        if (info->ulFlags & BIF_NEWDIALOGSTYLE)
            OleUninitialize();
        else
            CoUninitialize();
    }
    return pidl;
}

SHSTDAPI PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderW(LPBROWSEINFOW info)
{
    WCHAR name[MAX_PATH];
    if (!info)
        return nullptr;
    int image = info->iImage;
    PIDLIST_ABSOLUTE pidl = browse(info, FALSE, name, &image);
    if (pidl) {
        if (info->pszDisplayName)
            lstrcpynW(info->pszDisplayName, name, MAX_PATH);
        info->iImage = image;
    }
    return pidl;
}

SHSTDAPI PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderA(LPBROWSEINFOA info)
{
    WCHAR name[MAX_PATH];
    if (!info)
        return nullptr;
    BROWSEINFOW wide = {
        .hwndOwner = info->hwndOwner,
        .pidlRoot = info->pidlRoot,
        .ulFlags = info->ulFlags,
        .lpfn = info->lpfn,
        .lParam = info->lParam,
        .iImage = info->iImage,
    };
    LPWSTR title = nullptr;
    if (info->lpszTitle && IS_INTRESOURCE(info->lpszTitle))
        wide.lpszTitle = (LPCWSTR)info->lpszTitle;
    else if (info->lpszTitle)
        wide.lpszTitle = title = ShWiden(info->lpszTitle);
    int image = info->iImage;
    PIDLIST_ABSOLUTE pidl = browse(&wide, TRUE, name, &image);
    if (pidl) {
        if (info->pszDisplayName)
            WideCharToMultiByte(CP_ACP, 0, name, -1, info->pszDisplayName, MAX_PATH, nullptr, nullptr);
        info->iImage = image;
    }
    CoTaskMemFree(title);
    return pidl;
}
