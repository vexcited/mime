/*
 * the file dialog window. the folder tree is on the left, the files of the
 * folder on the right, the address and navigation buttons above them and the
 * file name, file type and buttons below.
 *
 * the dialog is a modal dialog of user32 with its controls made in code, so it
 * is sized by dialog units and laid out again when it is resized.
 */
#include "filedlg.h"
#include <mime/win32/commctrl.h>
#include <mime/win32/shellapi.h>
#include <mime/win32/commdlg.h>
#include <mime/win32/shlwapi.h>
#include <mime/win32/winnls.h>

#define IDC_BACK 1001
#define IDC_UP 1002
#define IDC_NEWFOLDER 1003
#define IDC_ADDRESS 1004
#define IDC_TREE 1005
#define IDC_LIST 1006
#define IDC_NAMELABEL 1007
#define IDC_NAME 1008
#define IDC_TYPELABEL 1009
#define IDC_TYPE 1010
#define IDC_READONLY 1011
#define IDC_CUSTOM_BASE 2000

#define WM_FD_CLOSE (WM_APP + 0x40)
#define WM_FD_NAVIGATE (WM_APP + 0x41)

#define MAX_HISTORY 32

/* icons of the lists and the tree, in the image list. */
enum {
    ICON_FOLDER,
    ICON_FILE,
    ICON_DRIVE,
    ICON_COMPUTER,
    ICON_DESKTOP,
    ICON_DOCUMENTS,
    ICON_DOWNLOADS,
    ICON_PICTURES,
    ICON_MUSIC,
    ICON_VIDEOS,
    ICON_COUNT,
};

/* a node of the folder tree. */
typedef struct _FD_NODE {
    WCHAR Path[MAX_PATH];
    /* 0 a heading, 1 the computer, 2 a folder or drive. */
    int Kind;
    BOOL Filled;
} FD_NODE;

/* an entry of the list. */
typedef struct _FD_ENTRY {
    WCHAR Name[MAX_PATH];
    /* the text of the first column when it is not the name, for drives. */
    WCHAR Label[MAX_PATH];
    BOOL Folder;
    BOOL Drive;
    DWORD Attributes;
    ULONGLONG Size;
    FILETIME Time;
    int Icon;
} FD_ENTRY;

typedef struct _FD_CONTROL {
    struct _FD_CONTROL *Next;
    FD_CONTROL_KIND Kind;
    DWORD Id;
    HWND Window;
    LPWSTR Text;
    /* what an edit box holds, where Text is its label. */
    LPWSTR Content;
    int State;
    BOOL Checked;
    DWORD Selected;
    BOOL Prominent;
    struct _FD_ITEM {
        struct _FD_ITEM *Next;
        DWORD Id;
        LPWSTR Text;
        int State;
        HWND Window;
    } *Items;
    HWND Label;
    /* a group heading starts a box around the controls that follow it, until the next one. */
    BOOL Ended;
} FD_CONTROL;

struct _FILEDLG {
    BOOL Save;
    DWORD Options;
    FILEDLG_EVENTS Events;
    void *Context;

    LPWSTR Title, OkLabel, CancelLabel, NameLabel, DefaultExt;
    LPWSTR InitialName;
    WCHAR InitialFolder[MAX_PATH], DefaultFolder[MAX_PATH];
    LPWSTR *Places;
    UINT PlaceCount;
    UINT TypeCount, TypeIndex;
    LPWSTR *TypeNames, *TypeSpecs;
    BOOL (*Filter)(void *context, LPCWSTR path, BOOL folder);
    void *FilterContext;
    BOOL ReadOnlyShown, ReadOnlyChecked;

    /* the window and its controls. */
    HWND Hwnd, Owner, Back, Up, NewFolder, Address, Tree, List, NameLabelWindow, NameEdit, TypeLabel, TypeCombo,
        OkButton, CancelButton, ReadOnlyBox;
    HIMAGELIST Images;
    HICON BackIcon, UpIcon, NewFolderIcon;
    HFONT Font;
    int UnitX, UnitY;
    BOOL Showing;
    BOOL Closed;
    HRESULT Result;
    DWORD ThreadId;

    /* where it is. */
    WCHAR Folder[MAX_PATH];
    BOOL AtComputer;
    LPWSTR History[MAX_HISTORY];
    int HistoryCount, HistoryPos;
    FD_ENTRY *Entries;
    UINT EntryCount;
    int SortColumn;
    BOOL SortDescending;
    /* a typed wildcard replaces the file type's filter until the folder changes. */
    WCHAR Wildcard[MAX_PATH];
    BOOL Syncing, SyncingName, Navigating;
    int Busy;

    /* the answer. */
    LPWSTR *Results;
    UINT ResultCount;

    FD_CONTROL *Controls;
    void (*ControlHandler)(FILEDLG *, void *, int, DWORD, DWORD, BOOL);
    void *ControlContext;
    int CustomHeight;
};

/* the folder the last dialog ended in, for the next that has no better idea. */
static WCHAR g_last_folder[MAX_PATH];

/* ------------------------------------------------------------------ strings */

HRESULT FdDuplicate(LPCWSTR text, LPWSTR *copy)
{
    *copy = nullptr;
    if (!text)
        return S_OK;
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    *copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!*copy)
        return E_OUTOFMEMORY;
    memcpy(*copy, text, bytes);
    return S_OK;
}

static void fd_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

static HRESULT set_text(LPWSTR *field, LPCWSTR text)
{
    LPWSTR copy;
    HRESULT hr = FdDuplicate(text, &copy);
    if (FAILED(hr))
        return hr;
    fd_free(*field);
    *field = copy;
    return S_OK;
}

/* a copy for the caller to free with CoTaskMemFree, which is the process heap in mime. */
static HRESULT task_copy(LPCWSTR text, LPWSTR *out)
{
    return FdDuplicate(text, out);
}

/* ------------------------------------------------------------------ creating and configuring */

FILEDLG *FdCreate(BOOL save)
{
    FILEDLG *dialog = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dialog));
    if (!dialog)
        return nullptr;
    dialog->Save = save;
    dialog->Options = save ? (FOS_OVERWRITEPROMPT | FOS_NOCHANGEDIR | FOS_NOREADONLYRETURN | FOS_PATHMUSTEXIST)
                           : (FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    dialog->TypeIndex = 1;
    dialog->Result = E_UNEXPECTED;
    return dialog;
}

static void free_entries(FILEDLG *dialog)
{
    fd_free(dialog->Entries);
    dialog->Entries = nullptr;
    dialog->EntryCount = 0;
}

static void free_results(FILEDLG *dialog)
{
    FdFreePaths(dialog->Results, dialog->ResultCount);
    dialog->Results = nullptr;
    dialog->ResultCount = 0;
}

static void free_types(FILEDLG *dialog)
{
    for (UINT i = 0; i < dialog->TypeCount; i++) {
        fd_free(dialog->TypeNames[i]);
        fd_free(dialog->TypeSpecs[i]);
    }
    fd_free(dialog->TypeNames);
    fd_free(dialog->TypeSpecs);
    dialog->TypeNames = dialog->TypeSpecs = nullptr;
    dialog->TypeCount = 0;
}

static void free_controls(FILEDLG *dialog)
{
    while (dialog->Controls) {
        FD_CONTROL *c = dialog->Controls;
        dialog->Controls = c->Next;
        while (c->Items) {
            struct _FD_ITEM *item = c->Items;
            c->Items = item->Next;
            fd_free(item->Text);
            fd_free(item);
        }
        fd_free(c->Text);
        fd_free(c->Content);
        fd_free(c);
    }
}

void FdFree(FILEDLG *dialog)
{
    if (!dialog)
        return;
    free_entries(dialog);
    free_results(dialog);
    free_types(dialog);
    free_controls(dialog);
    fd_free(dialog->Title);
    fd_free(dialog->OkLabel);
    fd_free(dialog->CancelLabel);
    fd_free(dialog->NameLabel);
    fd_free(dialog->DefaultExt);
    fd_free(dialog->InitialName);
    for (UINT i = 0; i < dialog->PlaceCount; i++)
        fd_free(dialog->Places[i]);
    fd_free(dialog->Places);
    for (int i = 0; i < dialog->HistoryCount; i++)
        fd_free(dialog->History[i]);
    fd_free(dialog);
}

void FdSetEvents(FILEDLG *dialog, const FILEDLG_EVENTS *events, void *context)
{
    if (events)
        dialog->Events = *events;
    else
        memset(&dialog->Events, 0, sizeof(dialog->Events));
    dialog->Context = context;
}

HRESULT FdSetTitle(FILEDLG *dialog, LPCWSTR text)
{
    HRESULT hr = set_text(&dialog->Title, text);
    if (SUCCEEDED(hr) && dialog->Hwnd)
        SetWindowTextW(dialog->Hwnd, dialog->Title ? dialog->Title : L"");
    return hr;
}

HRESULT FdSetOkLabel(FILEDLG *dialog, LPCWSTR text)
{
    HRESULT hr = set_text(&dialog->OkLabel, text);
    if (SUCCEEDED(hr) && dialog->OkButton)
        SetWindowTextW(dialog->OkButton, dialog->OkLabel);
    return hr;
}

HRESULT FdSetCancelLabel(FILEDLG *dialog, LPCWSTR text)
{
    HRESULT hr = set_text(&dialog->CancelLabel, text);
    if (SUCCEEDED(hr) && dialog->CancelButton)
        SetWindowTextW(dialog->CancelButton, dialog->CancelLabel);
    return hr;
}

HRESULT FdSetNameLabel(FILEDLG *dialog, LPCWSTR text)
{
    HRESULT hr = set_text(&dialog->NameLabel, text);
    if (SUCCEEDED(hr) && dialog->NameLabelWindow)
        SetWindowTextW(dialog->NameLabelWindow, dialog->NameLabel);
    return hr;
}

HRESULT FdSetDefaultExtension(FILEDLG *dialog, LPCWSTR text)
{
    /* a leading dot is not part of it. */
    if (text && text[0] == L'.')
        text++;
    return set_text(&dialog->DefaultExt, text);
}

HRESULT FdSetTypes(FILEDLG *dialog, UINT count, const COMDLG_FILTERSPEC *types)
{
    free_types(dialog);
    if (!count)
        return S_OK;
    dialog->TypeNames = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(LPWSTR));
    dialog->TypeSpecs = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(LPWSTR));
    if (!dialog->TypeNames || !dialog->TypeSpecs) {
        fd_free(dialog->TypeNames);
        fd_free(dialog->TypeSpecs);
        dialog->TypeNames = dialog->TypeSpecs = nullptr;
        return E_OUTOFMEMORY;
    }
    dialog->TypeCount = count;
    for (UINT i = 0; i < count; i++) {
        HRESULT hr = FdDuplicate(types[i].pszName, &dialog->TypeNames[i]);
        if (SUCCEEDED(hr))
            hr = FdDuplicate(types[i].pszSpec, &dialog->TypeSpecs[i]);
        if (FAILED(hr)) {
            free_types(dialog);
            return hr;
        }
    }
    if (dialog->TypeIndex < 1 || dialog->TypeIndex > count)
        dialog->TypeIndex = 1;
    return S_OK;
}

UINT FdTypeCount(const FILEDLG *dialog)
{
    return dialog->TypeCount;
}

void FdSetTypeIndex(FILEDLG *dialog, UINT index)
{
    if (index > dialog->TypeCount)
        index = dialog->TypeCount;
    if (index < 1)
        index = 1;
    dialog->TypeIndex = index;
    if (dialog->TypeCombo)
        SendMessageW(dialog->TypeCombo, CB_SETCURSEL, index - 1, 0);
}

UINT FdTypeIndex(const FILEDLG *dialog)
{
    return dialog->TypeIndex;
}

void FdSetOptions(FILEDLG *dialog, DWORD options)
{
    dialog->Options = options;
}

DWORD FdOptions(const FILEDLG *dialog)
{
    return dialog->Options;
}

BOOL FdIsSave(const FILEDLG *dialog)
{
    return dialog->Save;
}

BOOL FdIsShowing(const FILEDLG *dialog)
{
    return dialog->Showing;
}

HWND FdWindow(const FILEDLG *dialog)
{
    return dialog->Hwnd;
}

HRESULT FdSetFileName(FILEDLG *dialog, LPCWSTR text)
{
    HRESULT hr = set_text(&dialog->InitialName, text);
    if (SUCCEEDED(hr) && dialog->NameEdit) {
        dialog->SyncingName = TRUE;
        SetWindowTextW(dialog->NameEdit, text ? text : L"");
        dialog->SyncingName = FALSE;
    }
    return hr;
}

HRESULT FdGetFileName(FILEDLG *dialog, LPWSTR *text)
{
    if (dialog->NameEdit) {
        int length = GetWindowTextLengthW(dialog->NameEdit);
        WCHAR *buffer = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)length + 1) * sizeof(WCHAR));
        if (!buffer)
            return E_OUTOFMEMORY;
        GetWindowTextW(dialog->NameEdit, buffer, length + 1);
        *text = buffer;
        return S_OK;
    }
    *text = nullptr;
    if (!dialog->InitialName)
        return E_FAIL;
    return task_copy(dialog->InitialName, text);
}

static void copy_path(WCHAR *to, LPCWSTR path)
{
    to[0] = 0;
    if (path && wcslen(path) < MAX_PATH)
        wcscpy(to, path);
}

HRESULT FdSetFolder(FILEDLG *dialog, LPCWSTR path)
{
    if (dialog->Hwnd && dialog->Showing) {
        if (!path)
            return E_INVALIDARG;
        WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, (wcslen(path) + 1) * sizeof(WCHAR));
        if (!copy)
            return E_OUTOFMEMORY;
        wcscpy(copy, path);
        /* the window handles it on its own thread, which owns the controls. */
        if (GetCurrentThreadId() == dialog->ThreadId) {
            SendMessageW(dialog->Hwnd, WM_FD_NAVIGATE, 0, (LPARAM)copy);
            fd_free(copy);
        } else {
            PostMessageW(dialog->Hwnd, WM_FD_NAVIGATE, 1, (LPARAM)copy);
        }
        return S_OK;
    }
    copy_path(dialog->InitialFolder, path);
    return S_OK;
}

HRESULT FdSetDefaultFolder(FILEDLG *dialog, LPCWSTR path)
{
    copy_path(dialog->DefaultFolder, path);
    return S_OK;
}

HRESULT FdSetClientGuid(FILEDLG *dialog, const GUID *guid)
{
    return S_OK;
}

HRESULT FdAddPlace(FILEDLG *dialog, LPCWSTR path, BOOL top)
{
    LPWSTR copy;
    HRESULT hr = FdDuplicate(path, &copy);
    if (FAILED(hr))
        return hr;
    LPWSTR *places = HeapReAlloc(GetProcessHeap(), 0, dialog->Places, (dialog->PlaceCount + 1) * sizeof(LPWSTR));
    if (!places) {
        fd_free(copy);
        return E_OUTOFMEMORY;
    }
    dialog->Places = places;
    if (top) {
        memmove(places + 1, places, dialog->PlaceCount * sizeof(LPWSTR));
        places[0] = copy;
    } else {
        places[dialog->PlaceCount] = copy;
    }
    dialog->PlaceCount++;
    return S_OK;
}

void FdSetCustomFilter(FILEDLG *dialog, BOOL (*filter)(void *context, LPCWSTR path, BOOL folder), void *context)
{
    dialog->Filter = filter;
    dialog->FilterContext = context;
}

void FdSetReadOnlyBox(FILEDLG *dialog, BOOL shown, BOOL checked)
{
    dialog->ReadOnlyShown = shown;
    dialog->ReadOnlyChecked = checked;
}

BOOL FdReadOnlyChecked(const FILEDLG *dialog)
{
    return dialog->ReadOnlyChecked;
}

void FdSetControlHandler(FILEDLG *dialog, void (*handler)(FILEDLG *, void *, int, DWORD, DWORD, BOOL), void *context)
{
    dialog->ControlHandler = handler;
    dialog->ControlContext = context;
}

UINT FdResultCount(const FILEDLG *dialog)
{
    return dialog->ResultCount;
}

LPCWSTR FdResultPath(const FILEDLG *dialog, UINT index)
{
    return index < dialog->ResultCount ? dialog->Results[index] : nullptr;
}

void FdFreePaths(LPWSTR *paths, UINT count)
{
    if (!paths)
        return;
    for (UINT i = 0; i < count; i++)
        fd_free(paths[i]);
    fd_free(paths);
}

void FdClose(FILEDLG *dialog, HRESULT result)
{
    dialog->Result = result;
    dialog->Closed = TRUE;
    if (dialog->Hwnd)
        PostMessageW(dialog->Hwnd, WM_FD_CLOSE, 0, 0);
}

/* ------------------------------------------------------------------ file system helpers */

static BOOL fd_path_exists(LPCWSTR path, BOOL *folder)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    if (folder)
        *folder = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return TRUE;
}

/* the path of a known folder that is there. */
static BOOL known_path(const GUID *id, WCHAR *path)
{
    PWSTR text;
    path[0] = 0;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &text)))
        return FALSE;
    BOOL ok = wcslen(text) < MAX_PATH;
    if (ok)
        wcscpy(path, text);
    CoTaskMemFree(text);
    return ok && PathIsDirectoryW(path);
}

/* a file type's extension when it names exactly one, as "*.txt" does. */
static BOOL spec_extension(LPCWSTR spec, WCHAR *extension, UINT chars)
{
    if (!spec || spec[0] != L'*' || spec[1] != L'.')
        return FALSE;
    UINT n = 0;
    for (LPCWSTR p = spec + 2; *p && *p != L';'; p++) {
        if (*p == L'*' || *p == L'?' || n + 1 >= chars)
            return FALSE;
        extension[n++] = *p;
    }
    extension[n] = 0;
    return n != 0;
}

/* the spec the list is filtered by. */
static LPCWSTR current_spec(const FILEDLG *dialog)
{
    if (dialog->Wildcard[0])
        return dialog->Wildcard;
    if (dialog->TypeCount && dialog->TypeIndex >= 1 && dialog->TypeIndex <= dialog->TypeCount)
        return dialog->TypeSpecs[dialog->TypeIndex - 1];
    return L"*.*";
}

static BOOL name_wanted(const FILEDLG *dialog, LPCWSTR name)
{
    LPCWSTR spec = current_spec(dialog);
    return !spec || !spec[0] || PathMatchSpecW(name, spec);
}

static BOOL has_wildcard(LPCWSTR text)
{
    return wcschr(text, L'*') || wcschr(text, L'?');
}

static void format_size(ULONGLONG size, WCHAR *out)
{
    ULONGLONG kb = (size + 1023) / 1024;
    WCHAR digits[32];
    wsprintfW(digits, L"%I64u", kb);
    int length = lstrlenW(digits), n = 0;
    for (int i = 0; i < length; i++) {
        if (i && (length - i) % 3 == 0)
            out[n++] = L',';
        out[n++] = digits[i];
    }
    out[n] = 0;
    lstrcatW(out, L" KB");
}

static void format_date(const FILETIME *time, WCHAR *out, int chars)
{
    FILETIME local;
    SYSTEMTIME st;
    out[0] = 0;
    if (!time->dwLowDateTime && !time->dwHighDateTime)
        return;
    if (!FileTimeToLocalFileTime(time, &local) || !FileTimeToSystemTime(&local, &st))
        return;
    int n = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, out, chars);
    if (n <= 0) {
        out[0] = 0;
        return;
    }
    n--;
    out[n++] = L' ';
    int t = GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, out + n, chars - n);
    if (t <= 0)
        out[n - 1] = 0;
}

/* the names of the file types seen in this folder, so that each is looked up once. */
typedef struct _TYPE_NAME {
    WCHAR Extension[16];
    WCHAR Name[64];
} TYPE_NAME;

static TYPE_NAME g_type_names[96];
static UINT g_type_name_count;

static LPCWSTR type_name(const FD_ENTRY *entry)
{
    if (entry->Drive)
        return entry->Icon == ICON_DRIVE ? L"Local Disk" : L"Drive";
    if (entry->Folder)
        return L"File folder";
    LPCWSTR extension = PathFindExtensionW(entry->Name);
    if (!extension[0] || wcslen(extension) >= 16)
        return L"File";
    for (UINT i = 0; i < g_type_name_count; i++)
        if (!_wcsicmp(g_type_names[i].Extension, extension))
            return g_type_names[i].Name;
    SHFILEINFOW info;
    memset(&info, 0, sizeof(info));
    WCHAR text[64] = L"File";
    if (SHGetFileInfoW(entry->Name, FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                       SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) &&
        info.szTypeName[0])
        lstrcpynW(text, info.szTypeName, 64);
    if (g_type_name_count >= RTL_NUMBER_OF(g_type_names))
        return L"File";
    TYPE_NAME *slot = &g_type_names[g_type_name_count++];
    wcscpy(slot->Extension, extension);
    wcscpy(slot->Name, text);
    return slot->Name;
}

static int compare_entries(const FILEDLG *dialog, const FD_ENTRY *a, const FD_ENTRY *b)
{
    if (a->Folder != b->Folder)
        return a->Folder ? -1 : 1;
    int result = 0;
    switch (dialog->SortColumn) {
    case 1: {
        LONG compare = CompareFileTime(&a->Time, &b->Time);
        result = compare;
        break;
    }
    case 2:
        result = StrCmpLogicalW(type_name(a), type_name(b));
        break;
    case 3:
        result = a->Size < b->Size ? -1 : a->Size > b->Size;
        break;
    }
    if (!result)
        result = StrCmpLogicalW(a->Drive ? a->Label : a->Name, b->Drive ? b->Label : b->Name);
    return dialog->SortDescending ? -result : result;
}

/* a merge sort of indices, so the entries themselves stay where they are. */
static void sort_indices(const FILEDLG *dialog, UINT *index, UINT *scratch, UINT count)
{
    if (count < 2)
        return;
    UINT half = count / 2;
    sort_indices(dialog, index, scratch, half);
    sort_indices(dialog, index + half, scratch, count - half);
    UINT i = 0, j = half, k = 0;
    while (i < half && j < count) {
        if (compare_entries(dialog, &dialog->Entries[index[j]], &dialog->Entries[index[i]]) < 0)
            scratch[k++] = index[j++];
        else
            scratch[k++] = index[i++];
    }
    while (i < half)
        scratch[k++] = index[i++];
    while (j < count)
        scratch[k++] = index[j++];
    memcpy(index, scratch, count * sizeof(UINT));
}

/* the path of an entry of the list. */
static BOOL entry_path(const FILEDLG *dialog, const FD_ENTRY *entry, WCHAR *path)
{
    if (entry->Drive) {
        wcscpy(path, entry->Name);
        return TRUE;
    }
    if (dialog->AtComputer || wcslen(dialog->Folder) + wcslen(entry->Name) + 2 >= MAX_PATH)
        return FALSE;
    wcscpy(path, dialog->Folder);
    PathAddBackslashW(path);
    wcscat(path, entry->Name);
    return TRUE;
}

static int drive_icon(UINT type)
{
    return type == DRIVE_REMOTE ? ICON_DRIVE : ICON_DRIVE;
}

static UINT add_entry(FILEDLG *dialog, UINT *capacity, const FD_ENTRY *entry)
{
    if (dialog->EntryCount == *capacity) {
        UINT grown = *capacity ? *capacity * 2 : 64;
        FD_ENTRY *entries = dialog->Entries ? HeapReAlloc(GetProcessHeap(), 0, dialog->Entries, grown * sizeof(*entries))
                                            : HeapAlloc(GetProcessHeap(), 0, grown * sizeof(*entries));
        if (!entries)
            return (UINT)-1;
        dialog->Entries = entries;
        *capacity = grown;
    }
    dialog->Entries[dialog->EntryCount] = *entry;
    return dialog->EntryCount++;
}

/* reads the folder, or the drives, into the entries. */
static void read_entries(FILEDLG *dialog)
{
    UINT capacity = 0;
    free_entries(dialog);
    if (dialog->AtComputer) {
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (!(drives & (1u << i)))
                continue;
            FD_ENTRY entry;
            memset(&entry, 0, sizeof(entry));
            WCHAR root[4] = {(WCHAR)(L'A' + i), L':', L'\\', 0};
            wcscpy(entry.Name, root);
            entry.Folder = entry.Drive = TRUE;
            UINT type = GetDriveTypeW(root);
            entry.Icon = drive_icon(type);
            WCHAR label[40] = L"";
            if (type != DRIVE_REMOVABLE && type != DRIVE_CDROM)
                GetVolumeInformationW(root, label, 32, nullptr, nullptr, nullptr, nullptr, 0);
            LPCWSTR kind = type == DRIVE_CDROM ? L"CD Drive" : type == DRIVE_REMOVABLE ? L"Removable Disk"
                         : type == DRIVE_REMOTE ? L"Network Drive" : L"Local Disk";
            wsprintfW(entry.Label, L"%s (%c:)", label[0] ? label : kind, (WCHAR)(L'A' + i));
            ULARGE_INTEGER total = {0}, available, free_bytes;
            if (type == DRIVE_FIXED && GetDiskFreeSpaceExW(root, &available, &total, &free_bytes))
                entry.Size = total.QuadPart;
            if (add_entry(dialog, &capacity, &entry) == (UINT)-1)
                return;
        }
        return;
    }
    WCHAR pattern[MAX_PATH];
    if (wcslen(dialog->Folder) + 3 >= MAX_PATH)
        return;
    wcscpy(pattern, dialog->Folder);
    PathAddBackslashW(pattern);
    wcscat(pattern, L"*");
    WIN32_FIND_DATAW find;
    HANDLE handle = FindFirstFileExW(pattern, FindExInfoBasic, &find, FindExSearchNameMatch, nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    BOOL hidden_too = (dialog->Options & FOS_FORCESHOWHIDDEN) != 0;
    BOOL folders_only = (dialog->Options & FOS_PICKFOLDERS) != 0;
    do {
        if (!wcscmp(find.cFileName, L".") || !wcscmp(find.cFileName, L".."))
            continue;
        if (!hidden_too && (find.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
            continue;
        BOOL folder = (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!folder && (folders_only || !name_wanted(dialog, find.cFileName)))
            continue;
        if (dialog->Filter) {
            WCHAR path[MAX_PATH];
            if (wcslen(dialog->Folder) + wcslen(find.cFileName) + 2 < MAX_PATH) {
                wcscpy(path, dialog->Folder);
                PathAddBackslashW(path);
                wcscat(path, find.cFileName);
                if (!dialog->Filter(dialog->FilterContext, path, folder))
                    continue;
            }
        }
        FD_ENTRY entry;
        memset(&entry, 0, sizeof(entry));
        lstrcpynW(entry.Name, find.cFileName, MAX_PATH);
        entry.Folder = folder;
        entry.Attributes = find.dwFileAttributes;
        entry.Size = ((ULONGLONG)find.nFileSizeHigh << 32) | find.nFileSizeLow;
        entry.Time = find.ftLastWriteTime;
        entry.Icon = folder ? ICON_FOLDER : ICON_FILE;
        if (add_entry(dialog, &capacity, &entry) == (UINT)-1)
            break;
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
}

/* ------------------------------------------------------------------ the list */

static void list_set_columns(FILEDLG *dialog)
{
    static const struct {
        LPCWSTR Text;
        int Width;
        int Format;
    } columns[] = {
        {L"Name", 230, LVCFMT_LEFT}, {L"Date modified", 130, LVCFMT_LEFT}, {L"Type", 105, LVCFMT_LEFT},
        {L"Size", 75, LVCFMT_RIGHT},
    };
    for (int i = 0; i < 4; i++) {
        LVCOLUMNW column;
        memset(&column, 0, sizeof(column));
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
        column.pszText = (LPWSTR)columns[i].Text;
        column.cx = columns[i].Width;
        column.fmt = columns[i].Format;
        column.iSubItem = i;
        SendMessageW(dialog->List, LVM_INSERTCOLUMNW, i, (LPARAM)&column);
    }
}

static void list_fill(FILEDLG *dialog)
{
    UINT count = dialog->EntryCount;
    UINT *index = nullptr, *scratch = nullptr;
    SendMessageW(dialog->List, WM_SETREDRAW, FALSE, 0);
    SendMessageW(dialog->List, LVM_DELETEALLITEMS, 0, 0);
    if (count) {
        index = HeapAlloc(GetProcessHeap(), 0, count * 2 * sizeof(UINT));
        if (index) {
            scratch = index + count;
            for (UINT i = 0; i < count; i++)
                index[i] = i;
            sort_indices(dialog, index, scratch, count);
        }
    }
    SendMessageW(dialog->List, LVM_SETITEMCOUNT, count, 0);
    for (UINT i = 0; i < count && index; i++) {
        const FD_ENTRY *entry = &dialog->Entries[index[i]];
        WCHAR text[80];
        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = (int)i;
        item.pszText = (LPWSTR)(entry->Drive ? entry->Label : entry->Name);
        item.iImage = entry->Icon;
        item.lParam = (LPARAM)index[i];
        SendMessageW(dialog->List, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        if (!entry->Drive) {
            format_date(&entry->Time, text, 80);
            item.iSubItem = 1;
            item.pszText = text;
            SendMessageW(dialog->List, LVM_SETITEMW, 0, (LPARAM)&item);
        }
        item.iSubItem = 2;
        item.pszText = (LPWSTR)type_name(entry);
        SendMessageW(dialog->List, LVM_SETITEMW, 0, (LPARAM)&item);
        if (!entry->Folder) {
            format_size(entry->Size, text);
            item.iSubItem = 3;
            item.pszText = text;
            SendMessageW(dialog->List, LVM_SETITEMW, 0, (LPARAM)&item);
        } else if (entry->Drive && entry->Size) {
            StrFormatByteSizeW((LONGLONG)entry->Size, text, 80);
            item.iSubItem = 3;
            item.pszText = text;
            SendMessageW(dialog->List, LVM_SETITEMW, 0, (LPARAM)&item);
        }
    }
    fd_free(index);
    SendMessageW(dialog->List, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(dialog->List, nullptr, TRUE);
}

/* the entry behind a row. */
static const FD_ENTRY *row_entry(FILEDLG *dialog, int row)
{
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!SendMessageW(dialog->List, LVM_GETITEMW, 0, (LPARAM)&item) || (UINT)item.lParam >= dialog->EntryCount)
        return nullptr;
    return &dialog->Entries[item.lParam];
}

static int selected_count(FILEDLG *dialog)
{
    return (int)SendMessageW(dialog->List, LVM_GETSELECTEDCOUNT, 0, 0);
}

/* the selected entries, in list order. */
static UINT selected_entries(FILEDLG *dialog, const FD_ENTRY **entries, UINT max)
{
    UINT n = 0;
    int row = -1;
    while (n < max && (row = (int)SendMessageW(dialog->List, LVM_GETNEXTITEM, row, LVNI_SELECTED)) >= 0) {
        const FD_ENTRY *entry = row_entry(dialog, row);
        if (entry)
            entries[n++] = entry;
    }
    return n;
}

UINT FdGetSelection(FILEDLG *dialog, LPWSTR **paths)
{
    *paths = nullptr;
    if (!dialog->List)
        return 0;
    UINT count = (UINT)selected_count(dialog);
    if (!count)
        return 0;
    const FD_ENTRY **entries = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*entries));
    LPWSTR *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(LPWSTR));
    if (!entries || !list) {
        fd_free(entries);
        fd_free(list);
        return 0;
    }
    UINT n = selected_entries(dialog, entries, count), made = 0;
    for (UINT i = 0; i < n; i++) {
        WCHAR path[MAX_PATH];
        if (entry_path(dialog, entries[i], path) && SUCCEEDED(FdDuplicate(path, &list[made])))
            made++;
    }
    fd_free(entries);
    if (!made) {
        fd_free(list);
        return 0;
    }
    *paths = list;
    return made;
}

/* where a dialog starts: the folder it was told to, the last one a dialog ended in, the default, the documents. */
static BOOL starting_folder(FILEDLG *dialog, LPWSTR path)
{
    path[0] = 0;
    if (dialog->InitialFolder[0] && PathIsDirectoryW(dialog->InitialFolder))
        wcscpy(path, dialog->InitialFolder);
    else if (g_last_folder[0] && PathIsDirectoryW(g_last_folder))
        wcscpy(path, g_last_folder);
    else if (dialog->DefaultFolder[0] && PathIsDirectoryW(dialog->DefaultFolder))
        wcscpy(path, dialog->DefaultFolder);
    else
        known_path(&FOLDERID_Documents, path);
    return path[0] != 0;
}

BOOL FdGetFolder(FILEDLG *dialog, LPWSTR path)
{
    if (dialog->Showing) {
        if (dialog->AtComputer)
            return FALSE;
        wcscpy(path, dialog->Folder);
        return TRUE;
    }
    return starting_folder(dialog, path);
}

/* ------------------------------------------------------------------ icons */

/*
 * the icons of the tree and the list are drawn here, 16 by 16 pixels from a palette, since the imageres of
 * a machine without Windows' own artwork has none for folders and files.
 */
static const char g_art[ICON_COUNT][16][17] = {
    /* folder */
    {
        "................",
        "................",
        ".ooooo..........",
        ".oyyyyoooooooo..",
        ".oyyyyyyyyyyyyo.",
        ".oyyyyyyyyyyyyo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYYYYYYYYYo.",
        ".oooooooooooooo.",
        "................",
        "................",
    },
    /* file */
    {
        "................",
        "...kkkkkkk......",
        "...kwwwwwkk.....",
        "...kwwwwwkwk....",
        "...kwwwwwkkkk...",
        "...kwwwwwwwwk...",
        "...kwccccccwk...",
        "...kwwwwwwwwk...",
        "...kwggggggwk...",
        "...kwwwwwwwwk...",
        "...kwggggggwk...",
        "...kwwwwwwwwk...",
        "...kwggggggwk...",
        "...kwwwwwwwwk...",
        "...kkkkkkkkkk...",
        "................",
    },
    /* drive */
    {
        "................",
        "................",
        "................",
        "................",
        "..kkkkkkkkkkkk..",
        ".kllllllllllllk.",
        ".kllllllllllllk.",
        ".kggggggggggggk.",
        ".kGGGGGGGGGGGGk.",
        ".kGGGGGGGGGnGGk.",
        ".kGGGGGGGGGGGGk.",
        "..kkkkkkkkkkkk..",
        "................",
        "................",
        "................",
        "................",
    },
    /* computer */
    {
        "................",
        "..kkkkkkkkkkkk..",
        ".kllllllllllllk.",
        ".klbbbbbbbbbblk.",
        ".klbcccccccbblk.",
        ".klbcccccccbblk.",
        ".klbbbbbbbbbblk.",
        ".klbbbbbbbbbblk.",
        ".kllllllllllllk.",
        "..kkkkkkkkkkkk..",
        ".......kk.......",
        "......kllk......",
        "....kkkkkkkk....",
        "....kllllllk....",
        ".....kkkkkk.....",
        "................",
    },
    /* desktop */
    {
        "................",
        "................",
        ".kkkkkkkkkkkkkk.",
        ".kBBBBBBBBBBBBk.",
        ".kBccccBBBBBBBk.",
        ".kBccccBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        ".kkkkkkkkkkkkkk.",
        ".....kllllk.....",
        "....kkkkkkkk....",
        "................",
        "................",
    },
    /* documents */
    {
        "................",
        "................",
        ".ooooo..........",
        ".oyyyyoooooooo..",
        ".oyyyyyyyyyyyyo.",
        ".oyyyyyyyyyyyyo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYkkkkkkkYo.",
        ".oYYYYkwwwwwkYo.",
        ".oYYYYkwcccwkYo.",
        ".oYYYYkwwwwwkYo.",
        ".oYYYYkwcccwkYo.",
        ".oYYYYkwwwwwkYo.",
        ".oooookwcccwkoo.",
        "......kkkkkkk...",
        "................",
    },
    /* downloads */
    {
        "................",
        "................",
        ".ooooo..........",
        ".oyyyyoooooooo..",
        ".oyyyyyyyyyyyyo.",
        ".oyyyyyyyyyyyyo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYYYYnnYYYo.",
        ".oYYYYYYYnnYYYo.",
        ".oYYYYYYYnnYYYo.",
        ".oYYYYYnnnnnnYo.",
        ".oYYYYYYnnnnYYo.",
        ".oYYYYYYYnnYYYo.",
        ".oooooooooooooo.",
        "................",
        "................",
    },
    /* pictures */
    {
        "................",
        "................",
        ".ooooo..........",
        ".oyyyyoooooooo..",
        ".oyyyyyyyyyyyyo.",
        ".oyyyyyyyyyyyyo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYkkkkkkkko.",
        ".oYYYYkcccaacko.",
        ".oYYYYkccaaacko.",
        ".oYYYYkccccccko.",
        ".oYYYYknnccncko.",
        ".oYYYYknnnnnnko.",
        ".oooookkkkkkkko.",
        "................",
        "................",
    },
    /* music */
    {
        "................",
        "................",
        ".ooooo..........",
        ".oyyyyoooooooo..",
        ".oyyyyyyyyyyyyo.",
        ".oyyyyyyyyyyyyo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYYYYppppYo.",
        ".oYYYYYYYpYYpYo.",
        ".oYYYYYYYpYYpYo.",
        ".oYYYYYYYpYYpYo.",
        ".oYYYYYppppYpYo.",
        ".oYYYYppppYYppo.",
        ".ooooooppoooopo.",
        "................",
        "................",
    },
    /* videos */
    {
        "................",
        "................",
        ".ooooo..........",
        ".oyyyyoooooooo..",
        ".oyyyyyyyyyyyyo.",
        ".oyyyyyyyyyyyyo.",
        ".oYYYYYYYYYYYYo.",
        ".oYYYYkkkkkkkko.",
        ".oYYYYkwkkkkwko.",
        ".oYYYYkkkkkkkko.",
        ".oYYYYkkYnnYkko.",
        ".oYYYYkkkkkkkko.",
        ".oYYYYkwkkkkwko.",
        ".oooookkkkkkkko.",
        "................",
        "................",
    },

};

static DWORD art_color(char c)
{
    switch (c) {
    case 'o':
        return 0xFFC69A1A;
    case 'y':
        return 0xFFFFE699;
    case 'Y':
        return 0xFFF7C948;
    case 'w':
        return 0xFFFFFFFF;
    case 'k':
        return 0xFF5A5A5A;
    case 'l':
        return 0xFFE6E6E6;
    case 'g':
        return 0xFFB4B4B4;
    case 'G':
        return 0xFF7A7A7A;
    case 'b':
        return 0xFF4A90D9;
    case 'B':
        return 0xFF1F5FA8;
    case 'c':
        return 0xFF8EC5F5;
    case 'n':
        return 0xFF3DA143;
    case 'r':
        return 0xFFD64545;
    case 'p':
        return 0xFF8A5CC2;
    case 'a':
        return 0xFFF2994A;
    }
    return 0;
}

/* an icon of a picture, each pixel repeated to fill the size asked for. */
static HICON icon_from_art(const char rows[16][17], int size)
{
    int scale = size / 16 ? size / 16 : 1;
    int actual = 16 * scale;
    DWORD *pixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)actual * actual * sizeof(DWORD));
    int stride = (actual + 15) / 16;
    WORD *mask = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)stride * actual * sizeof(WORD));
    HICON icon = nullptr;
    if (pixels && mask) {
        for (int y = 0; y < actual; y++) {
            for (int x = 0; x < actual; x++) {
                DWORD color = art_color(rows[y / scale][x / scale]);
                pixels[y * actual + x] = color;
                if (!color) {
                    WORD bit = (WORD)(0x8000 >> (x & 15));
                    WORD *word = &mask[y * stride + x / 16];
                    *word |= (WORD)((bit >> 8) | (bit << 8));
                }
            }
        }
        ICONINFO info;
        memset(&info, 0, sizeof(info));
        info.fIcon = TRUE;
        info.hbmMask = CreateBitmap(actual, actual, 1, 1, mask);
        info.hbmColor = CreateBitmap(actual, actual, 1, 32, pixels);
        if (info.hbmMask && info.hbmColor)
            icon = CreateIconIndirect(&info);
        if (info.hbmMask)
            DeleteObject(info.hbmMask);
        if (info.hbmColor)
            DeleteObject(info.hbmColor);
    }
    fd_free(pixels);
    fd_free(mask);
    return icon;
}

static void load_images(FILEDLG *dialog)
{
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size < 16)
        size = 16;
    size = size / 16 * 16;
    dialog->Images = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, ICON_COUNT, 4);
    if (!dialog->Images)
        return;
    for (int i = 0; i < ICON_COUNT; i++) {
        HICON icon = icon_from_art(g_art[i], size);
        ImageList_ReplaceIcon(dialog->Images, -1, icon);
        if (icon)
            DestroyIcon(icon);
    }
}

/* ------------------------------------------------------------------ the tree */

static HTREEITEM tree_add(FILEDLG *dialog, HTREEITEM parent, LPCWSTR text, int icon, int kind, LPCWSTR path,
                          BOOL children)
{
    FD_NODE *node = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*node));
    if (!node)
        return nullptr;
    node->Kind = kind;
    if (path)
        lstrcpynW(node->Path, path, MAX_PATH);
    TVINSERTSTRUCTW insert;
    memset(&insert, 0, sizeof(insert));
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
    insert.item.pszText = (LPWSTR)text;
    insert.item.iImage = insert.item.iSelectedImage = icon;
    insert.item.lParam = (LPARAM)node;
    insert.item.cChildren = children ? 1 : 0;
    HTREEITEM item = (HTREEITEM)SendMessageW(dialog->Tree, TVM_INSERTITEMW, 0, (LPARAM)&insert);
    if (!item)
        fd_free(node);
    return item;
}

static FD_NODE *tree_node(FILEDLG *dialog, HTREEITEM item)
{
    TVITEMW tv;
    memset(&tv, 0, sizeof(tv));
    tv.mask = TVIF_PARAM;
    tv.hItem = item;
    if (!SendMessageW(dialog->Tree, TVM_GETITEMW, 0, (LPARAM)&tv))
        return nullptr;
    return (FD_NODE *)tv.lParam;
}

static int icon_for_known(const GUID *id)
{
    if (IsEqualGUID(id, &FOLDERID_Desktop))
        return ICON_DESKTOP;
    if (IsEqualGUID(id, &FOLDERID_Downloads))
        return ICON_DOWNLOADS;
    if (IsEqualGUID(id, &FOLDERID_Documents))
        return ICON_DOCUMENTS;
    if (IsEqualGUID(id, &FOLDERID_Pictures))
        return ICON_PICTURES;
    if (IsEqualGUID(id, &FOLDERID_Music))
        return ICON_MUSIC;
    if (IsEqualGUID(id, &FOLDERID_Videos))
        return ICON_VIDEOS;
    return ICON_FOLDER;
}

/* the subfolders of a path, sorted, as names. */
typedef struct _NAMES {
    WCHAR (*Names)[MAX_PATH];
    UINT Count, Capacity;
} NAMES;

static void names_sort(WCHAR (*names)[MAX_PATH], UINT count)
{
    /* a binary insertion by shifting is enough for the folders of one level. */
    for (UINT i = 1; i < count; i++) {
        WCHAR key[MAX_PATH];
        wcscpy(key, names[i]);
        UINT j = i;
        while (j > 0 && StrCmpLogicalW(names[j - 1], key) > 0) {
            wcscpy(names[j], names[j - 1]);
            j--;
        }
        wcscpy(names[j], key);
    }
}

static BOOL names_subfolders(FILEDLG *dialog, LPCWSTR path, NAMES *names)
{
    names->Names = nullptr;
    names->Count = names->Capacity = 0;
    WCHAR pattern[MAX_PATH];
    if (wcslen(path) + 3 >= MAX_PATH)
        return FALSE;
    wcscpy(pattern, path);
    PathAddBackslashW(pattern);
    wcscat(pattern, L"*");
    WIN32_FIND_DATAW find;
    HANDLE handle = FindFirstFileExW(pattern, FindExInfoBasic, &find, FindExSearchLimitToDirectories, nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL hidden_too = (dialog->Options & FOS_FORCESHOWHIDDEN) != 0;
    do {
        if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(find.cFileName, L".") ||
            !wcscmp(find.cFileName, L".."))
            continue;
        if (!hidden_too && (find.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
            continue;
        if (names->Count == names->Capacity) {
            UINT grown = names->Capacity ? names->Capacity * 2 : 32;
            void *memory = names->Names ? HeapReAlloc(GetProcessHeap(), 0, names->Names, grown * sizeof(*names->Names))
                                        : HeapAlloc(GetProcessHeap(), 0, grown * sizeof(*names->Names));
            if (!memory)
                break;
            names->Names = memory;
            names->Capacity = grown;
        }
        lstrcpynW(names->Names[names->Count++], find.cFileName, MAX_PATH);
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
    names_sort(names->Names, names->Count);
    return TRUE;
}

static void tree_fill(FILEDLG *dialog, HTREEITEM item)
{
    FD_NODE *node = tree_node(dialog, item);
    if (!node || node->Filled)
        return;
    node->Filled = TRUE;
    if (node->Kind == 0)
        return;
    if (node->Kind == 1) {
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (!(drives & (1u << i)))
                continue;
            WCHAR root[4] = {(WCHAR)(L'A' + i), L':', L'\\', 0};
            WCHAR label[32] = L"", text[64];
            UINT type = GetDriveTypeW(root);
            if (type != DRIVE_REMOVABLE && type != DRIVE_CDROM)
                GetVolumeInformationW(root, label, 32, nullptr, nullptr, nullptr, nullptr, 0);
            wsprintfW(text, L"%s (%c:)", label[0] ? label : (type == DRIVE_CDROM ? L"CD Drive" : L"Local Disk"),
                      (WCHAR)(L'A' + i));
            tree_add(dialog, item, text, ICON_DRIVE, 2, root, TRUE);
        }
        return;
    }
    NAMES names;
    WCHAR path[MAX_PATH];
    if (names_subfolders(dialog, node->Path, &names)) {
        for (UINT i = 0; i < names.Count; i++) {
            wcscpy(path, node->Path);
            PathAddBackslashW(path);
            if (wcslen(path) + wcslen(names.Names[i]) >= MAX_PATH)
                continue;
            wcscat(path, names.Names[i]);
            tree_add(dialog, item, names.Names[i], ICON_FOLDER, 2, path, TRUE);
        }
        fd_free(names.Names);
    }
    /* a folder with no subfolders loses its expander. */
    if (!SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)item)) {
        TVITEMW tv;
        memset(&tv, 0, sizeof(tv));
        tv.mask = TVIF_CHILDREN;
        tv.hItem = item;
        tv.cChildren = 0;
        SendMessageW(dialog->Tree, TVM_SETITEMW, 0, (LPARAM)&tv);
    }
}

static void tree_build(FILEDLG *dialog)
{
    static const struct {
        const GUID *Id;
        LPCWSTR Name;
    } quick[] = {
        {&FOLDERID_Desktop, L"Desktop"},   {&FOLDERID_Downloads, L"Downloads"}, {&FOLDERID_Documents, L"Documents"},
        {&FOLDERID_Pictures, L"Pictures"}, {&FOLDERID_Music, L"Music"},         {&FOLDERID_Videos, L"Videos"},
    };
    SendMessageW(dialog->Tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)dialog->Images);
    SendMessageW(dialog->Tree, TVM_SETINDENT, 14, 0);
    HTREEITEM heading = tree_add(dialog, TVI_ROOT, L"Quick access", ICON_FOLDER, 0, nullptr, TRUE);
    WCHAR path[MAX_PATH];
    for (int i = 0; heading && i < 6; i++) {
        if (!known_path(quick[i].Id, path))
            continue;
        HTREEITEM item = tree_add(dialog, heading, quick[i].Name, icon_for_known(quick[i].Id), 2, path, TRUE);
        (void)item;
    }
    for (UINT i = 0; heading && i < dialog->PlaceCount; i++) {
        if (!PathIsDirectoryW(dialog->Places[i]))
            continue;
        LPCWSTR name = PathFindFileNameW(dialog->Places[i]);
        tree_add(dialog, heading, name, ICON_FOLDER, 2, dialog->Places[i], TRUE);
    }
    if (heading) {
        FD_NODE *node = tree_node(dialog, heading);
        if (node)
            node->Filled = TRUE;
        SendMessageW(dialog->Tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)heading);
    }
    HTREEITEM computer = tree_add(dialog, TVI_ROOT, L"This PC", ICON_COMPUTER, 1, nullptr, TRUE);
    (void)computer;
}

/* the child of an item with a given name. */
static HTREEITEM tree_child_named(FILEDLG *dialog, HTREEITEM parent, LPCWSTR name)
{
    for (HTREEITEM child = (HTREEITEM)SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)parent); child;
         child = (HTREEITEM)SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)child)) {
        FD_NODE *node = tree_node(dialog, child);
        if (!node || !node->Path[0])
            continue;
        LPCWSTR last = PathFindFileNameW(node->Path);
        if (!_wcsicmp(last, name) || (node->Path[1] == L':' && !node->Path[3] && !_wcsnicmp(node->Path, name, 2)))
            return child;
    }
    return nullptr;
}

/* selects what the folder shown is in the tree, expanding the way to it. */
static void tree_select_path(FILEDLG *dialog, LPCWSTR path)
{
    dialog->Syncing = TRUE;
    HTREEITEM target = nullptr;
    if (!path || !path[0]) {
        for (HTREEITEM root = (HTREEITEM)SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_ROOT, 0); root;
             root = (HTREEITEM)SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)root)) {
            FD_NODE *node = tree_node(dialog, root);
            if (node && node->Kind == 1)
                target = root;
        }
    } else {
        /* a folder of the quick access list. */
        HTREEITEM root = (HTREEITEM)SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_ROOT, 0);
        for (HTREEITEM child = root ? (HTREEITEM)SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)root)
                                    : nullptr;
             child && !target; child = (HTREEITEM)SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)child)) {
            FD_NODE *node = tree_node(dialog, child);
            if (node && !_wcsicmp(node->Path, path))
                target = child;
        }
        if (!target) {
            HTREEITEM computer = nullptr;
            for (HTREEITEM r = root; r; r = (HTREEITEM)SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)r)) {
                FD_NODE *node = tree_node(dialog, r);
                if (node && node->Kind == 1)
                    computer = r;
            }
            if (computer) {
                tree_fill(dialog, computer);
                WCHAR copy[MAX_PATH];
                lstrcpynW(copy, path, MAX_PATH);
                HTREEITEM at = computer;
                WCHAR *p = copy;
                while (*p && at) {
                    WCHAR *end = p;
                    while (*end && *end != L'\\')
                        end++;
                    WCHAR saved = *end;
                    *end = 0;
                    if (end > p) {
                        FD_NODE *node = tree_node(dialog, at);
                        if (node && !node->Filled)
                            tree_fill(dialog, at);
                        HTREEITEM next = tree_child_named(dialog, at, p);
                        if (!next)
                            break;
                        at = next;
                    }
                    *end = saved;
                    p = saved ? end + 1 : end;
                }
                if (at != computer)
                    target = at;
                else if (path[1] != L':')
                    target = computer;
            }
        }
    }
    if (target) {
        SendMessageW(dialog->Tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)target);
        SendMessageW(dialog->Tree, TVM_ENSUREVISIBLE, 0, (LPARAM)target);
    }
    dialog->Syncing = FALSE;
}

/* ------------------------------------------------------------------ messages to the user */

static int fd_message(FILEDLG *dialog, LPCWSTR text, UINT flags)
{
    WCHAR title[128] = L"";
    if (dialog->Hwnd)
        GetWindowTextW(dialog->Hwnd, title, 128);
    return MessageBoxW(dialog->Hwnd, text, title[0] ? title : (dialog->Save ? L"Save As" : L"Open"), flags);
}

static void message_with_name(FILEDLG *dialog, LPCWSTR name, LPCWSTR text)
{
    WCHAR full[MAX_PATH + 160];
    wsprintfW(full, L"%s\n%s", name, text);
    fd_message(dialog, full, MB_OK | MB_ICONWARNING);
}

/* ------------------------------------------------------------------ navigation */

static void update_buttons(FILEDLG *dialog)
{
    EnableWindow(dialog->Back, dialog->HistoryPos > 0);
    EnableWindow(dialog->Up, !dialog->AtComputer);
}

static void history_push(FILEDLG *dialog, LPCWSTR where)
{
    if (dialog->HistoryPos + 1 < dialog->HistoryCount) {
        for (int i = dialog->HistoryPos + 1; i < dialog->HistoryCount; i++)
            fd_free(dialog->History[i]);
        dialog->HistoryCount = dialog->HistoryPos + 1;
    }
    if (dialog->HistoryCount && !_wcsicmp(dialog->History[dialog->HistoryCount - 1], where))
        return;
    if (dialog->HistoryCount == MAX_HISTORY) {
        fd_free(dialog->History[0]);
        memmove(dialog->History, dialog->History + 1, (MAX_HISTORY - 1) * sizeof(LPWSTR));
        dialog->HistoryCount--;
    }
    if (SUCCEEDED(FdDuplicate(where, &dialog->History[dialog->HistoryCount]))) {
        dialog->HistoryCount++;
        dialog->HistoryPos = dialog->HistoryCount - 1;
    }
}

/* shows a folder, "" for the drives. FALSE when it cannot be shown. */
static BOOL navigate(FILEDLG *dialog, LPCWSTR where, BOOL record, BOOL sync_tree)
{
    WCHAR full[MAX_PATH];
    BOOL computer = !where || !where[0];
    if (!computer) {
        DWORD n = GetFullPathNameW(where, MAX_PATH, full, nullptr);
        if (!n || n >= MAX_PATH || !PathIsDirectoryW(full))
            return FALSE;
        /* the folder of a root keeps its separator, any other loses it. */
        if (!PathIsRootW(full))
            PathRemoveBackslashW(full);
        if (dialog->Events.FolderChanging && !dialog->Events.FolderChanging(dialog, dialog->Context, full))
            return FALSE;
    }
    dialog->Navigating = TRUE;
    dialog->AtComputer = computer;
    if (computer)
        dialog->Folder[0] = 0;
    else
        wcscpy(dialog->Folder, full);
    dialog->Wildcard[0] = 0;
    read_entries(dialog);
    list_fill(dialog);
    SetWindowTextW(dialog->Address, computer ? L"This PC" : dialog->Folder);
    if (record)
        history_push(dialog, computer ? L"" : dialog->Folder);
    update_buttons(dialog);
    if (sync_tree)
        tree_select_path(dialog, computer ? nullptr : dialog->Folder);
    if (!computer)
        wcscpy(g_last_folder, dialog->Folder);
    dialog->Navigating = FALSE;
    if (dialog->Events.FolderChange)
        dialog->Events.FolderChange(dialog, dialog->Context);
    return TRUE;
}

static void go_up(FILEDLG *dialog)
{
    if (dialog->AtComputer)
        return;
    WCHAR parent[MAX_PATH];
    wcscpy(parent, dialog->Folder);
    if (PathIsRootW(parent)) {
        navigate(dialog, L"", TRUE, TRUE);
        return;
    }
    PathRemoveFileSpecW(parent);
    if (!parent[0])
        return;
    navigate(dialog, parent, TRUE, TRUE);
}

static void go_back(FILEDLG *dialog)
{
    if (dialog->HistoryPos <= 0)
        return;
    int target = dialog->HistoryPos - 1;
    if (navigate(dialog, dialog->History[target], FALSE, TRUE))
        dialog->HistoryPos = target;
    update_buttons(dialog);
}

static void go_forward(FILEDLG *dialog)
{
    if (dialog->HistoryPos + 1 >= dialog->HistoryCount)
        return;
    int target = dialog->HistoryPos + 1;
    if (navigate(dialog, dialog->History[target], FALSE, TRUE))
        dialog->HistoryPos = target;
    update_buttons(dialog);
}

static void refresh(FILEDLG *dialog)
{
    read_entries(dialog);
    list_fill(dialog);
}

/* ------------------------------------------------------------------ what was typed */

/* the file name box without the blanks around it. */
static void name_text(FILEDLG *dialog, WCHAR *text, int chars)
{
    text[0] = 0;
    GetWindowTextW(dialog->NameEdit, text, chars);
    WCHAR *start = text;
    while (*start == L' ')
        start++;
    if (start != text)
        memmove(text, start, (wcslen(start) + 1) * sizeof(WCHAR));
    int length = lstrlenW(text);
    while (length > 0 && text[length - 1] == L' ')
        text[--length] = 0;
}

/* sets the name box from the selected files. */
static void name_from_selection(FILEDLG *dialog)
{
    if (dialog->Navigating || dialog->SyncingName)
        return;
    int count = selected_count(dialog);
    if (!count)
        return;
    const FD_ENTRY *entries[64];
    UINT n = selected_entries(dialog, entries, 64);
    WCHAR text[MAX_PATH * 2];
    text[0] = 0;
    BOOL folders = (dialog->Options & FOS_PICKFOLDERS) != 0;
    UINT used = 0;
    for (UINT i = 0; i < n; i++) {
        if (entries[i]->Folder != folders || entries[i]->Drive)
            continue;
        used++;
    }
    if (!used)
        return;
    for (UINT i = 0; i < n; i++) {
        if (entries[i]->Folder != folders || entries[i]->Drive)
            continue;
        if (used == 1) {
            wcscpy(text, entries[i]->Name);
            break;
        }
        if (wcslen(text) + wcslen(entries[i]->Name) + 4 >= RTL_NUMBER_OF(text))
            break;
        wcscat(text, text[0] ? L" \"" : L"\"");
        wcscat(text, entries[i]->Name);
        wcscat(text, L"\"");
    }
    dialog->SyncingName = TRUE;
    SetWindowTextW(dialog->NameEdit, text);
    dialog->SyncingName = FALSE;
}

/* a path from what was typed: absolute as it is, otherwise below the folder shown. */
static BOOL resolve_name(FILEDLG *dialog, LPCWSTR text, WCHAR *full)
{
    WCHAR joined[MAX_PATH * 2];
    BOOL drive = ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) && text[1] == L':';
    if ((drive && text[2] == L'\\') || (text[0] == L'\\' && text[1] == L'\\')) {
        lstrcpynW(joined, text, RTL_NUMBER_OF(joined));
    } else if (drive) {
        return FALSE;
    } else if (dialog->AtComputer) {
        return FALSE;
    } else if (text[0] == L'\\') {
        if (dialog->Folder[1] != L':')
            return FALSE;
        wsprintfW(joined, L"%c:%s", dialog->Folder[0], text);
    } else {
        if (wcslen(dialog->Folder) + wcslen(text) + 2 >= RTL_NUMBER_OF(joined))
            return FALSE;
        wcscpy(joined, dialog->Folder);
        PathAddBackslashW(joined);
        wcscat(joined, text);
    }
    DWORD n = GetFullPathNameW(joined, MAX_PATH, full, nullptr);
    return n && n < MAX_PATH;
}

static BOOL valid_name(LPCWSTR text)
{
    for (LPCWSTR p = text; *p; p++)
        if (*p == L'<' || *p == L'>' || *p == L'|' || *p == L'"' || *p < 0x20)
            return FALSE;
    return TRUE;
}

/* the extension to put on a file saved with none. */
static BOOL extension_to_add(FILEDLG *dialog, WCHAR *extension, UINT chars)
{
    if (dialog->DefaultExt && dialog->DefaultExt[0]) {
        lstrcpynW(extension, dialog->DefaultExt, chars);
        return TRUE;
    }
    if (dialog->TypeCount && dialog->TypeIndex >= 1 && dialog->TypeIndex <= dialog->TypeCount)
        return spec_extension(dialog->TypeSpecs[dialog->TypeIndex - 1], extension, chars);
    return FALSE;
}

static void add_extension(FILEDLG *dialog, WCHAR *path)
{
    WCHAR extension[32];
    LPCWSTR name = PathFindFileNameW(path);
    LPCWSTR dot = PathFindExtensionW(name);
    if ((dot[0] && dot[1]) || !extension_to_add(dialog, extension, 32))
        return;
    SIZE_T length = wcslen(path);
    if (length + wcslen(extension) + 2 >= MAX_PATH)
        return;
    if (dot[0] == L'.' && !dot[1])
        path[length - 1] = 0;
    wcscat(path, L".");
    wcscat(path, extension);
}

static BOOL add_result(FILEDLG *dialog, LPCWSTR path)
{
    LPWSTR *grown = dialog->Results ? HeapReAlloc(GetProcessHeap(), 0, dialog->Results,
                                                  (dialog->ResultCount + 1) * sizeof(LPWSTR))
                                    : HeapAlloc(GetProcessHeap(), 0, sizeof(LPWSTR));
    if (!grown)
        return FALSE;
    dialog->Results = grown;
    if (FAILED(FdDuplicate(path, &grown[dialog->ResultCount])))
        return FALSE;
    dialog->ResultCount++;
    return TRUE;
}

/* the names in the file name box: one, or several in quotes. */
static UINT split_names(LPWSTR text, LPWSTR *names, UINT max)
{
    UINT n = 0;
    if (text[0] != L'"') {
        names[n++] = text;
        return n;
    }
    WCHAR *p = text;
    while (*p && n < max) {
        while (*p == L' ')
            p++;
        if (*p != L'"')
            break;
        names[n++] = ++p;
        while (*p && *p != L'"')
            p++;
        if (*p)
            *p++ = 0;
    }
    return n;
}

/* turns the file name box and the selection into results, or says why not. TRUE when the dialog may end. */
static BOOL accept(FILEDLG *dialog)
{
    WCHAR text[MAX_PATH * 4];
    LPWSTR names[64];
    UINT name_count = 0;
    BOOL pick_folders = (dialog->Options & FOS_PICKFOLDERS) != 0;
    name_text(dialog, text, RTL_NUMBER_OF(text));
    free_results(dialog);

    if (!text[0]) {
        const FD_ENTRY *entries[64];
        UINT n = selected_entries(dialog, entries, 64);
        if (!n) {
            if (pick_folders && !dialog->AtComputer)
                return add_result(dialog, dialog->Folder);
            MessageBeep(MB_ICONASTERISK);
            return FALSE;
        }
        if (!pick_folders && n == 1 && entries[0]->Folder) {
            WCHAR path[MAX_PATH];
            if (entry_path(dialog, entries[0], path))
                navigate(dialog, path, TRUE, TRUE);
            return FALSE;
        }
        for (UINT i = 0; i < n; i++) {
            WCHAR path[MAX_PATH];
            if (entries[i]->Folder != pick_folders || entries[i]->Drive || !entry_path(dialog, entries[i], path))
                continue;
            if (!add_result(dialog, path))
                return FALSE;
        }
        if (!dialog->ResultCount)
            return FALSE;
    } else {
        name_count = split_names(text, names, 64);
        if (!name_count)
            return FALSE;
        if (name_count > 1 && !(dialog->Options & FOS_ALLOWMULTISELECT)) {
            message_with_name(dialog, text, L"Only one file can be chosen.");
            return FALSE;
        }
        for (UINT i = 0; i < name_count; i++) {
            WCHAR full[MAX_PATH];
            LPCWSTR name = names[i];
            if (!name[0])
                continue;
            if (!valid_name(name)) {
                message_with_name(dialog, name, L"The file name is not valid.\nCheck the name and try again.");
                free_results(dialog);
                return FALSE;
            }
            if (has_wildcard(name)) {
                if (name_count > 1 || PathIsDirectoryW(name)) {
                    message_with_name(dialog, name, L"The file name is not valid.\nCheck the name and try again.");
                    return FALSE;
                }
                lstrcpynW(dialog->Wildcard, name, MAX_PATH);
                refresh(dialog);
                return FALSE;
            }
            if (!resolve_name(dialog, name, full)) {
                message_with_name(dialog, name, L"The path does not exist.\nCheck the path and try again.");
                free_results(dialog);
                return FALSE;
            }
            BOOL folder = FALSE, exists = fd_path_exists(full, &folder);
            if (exists && folder) {
                if (pick_folders) {
                    add_result(dialog, full);
                    continue;
                }
                if (name_count == 1) {
                    if (navigate(dialog, full, TRUE, TRUE)) {
                        dialog->SyncingName = TRUE;
                        SetWindowTextW(dialog->NameEdit, L"");
                        dialog->SyncingName = FALSE;
                    }
                    return FALSE;
                }
                message_with_name(dialog, name, L"This is a folder.\nChoose a file and try again.");
                free_results(dialog);
                return FALSE;
            }
            if (pick_folders) {
                message_with_name(dialog, name, L"The folder does not exist.\nCheck the name and try again.");
                free_results(dialog);
                return FALSE;
            }
            if (dialog->Save) {
                add_extension(dialog, full);
                exists = fd_path_exists(full, &folder);
                if (exists && folder) {
                    if (navigate(dialog, full, TRUE, TRUE))
                        SetWindowTextW(dialog->NameEdit, L"");
                    return FALSE;
                }
            } else if (!exists && dialog->DefaultExt && dialog->DefaultExt[0]) {
                WCHAR with[MAX_PATH];
                if (wcslen(full) + wcslen(dialog->DefaultExt) + 2 < MAX_PATH) {
                    wcscpy(with, full);
                    wcscat(with, L".");
                    wcscat(with, dialog->DefaultExt);
                    if (fd_path_exists(with, &folder) && !folder) {
                        wcscpy(full, with);
                        exists = TRUE;
                    }
                }
            }
            WCHAR parent[MAX_PATH];
            wcscpy(parent, full);
            PathRemoveFileSpecW(parent);
            if (!exists || (dialog->Save && (dialog->Options & FOS_PATHMUSTEXIST))) {
                if ((dialog->Options & FOS_PATHMUSTEXIST) && parent[0] && !PathIsDirectoryW(parent) &&
                    !PathIsRootW(parent)) {
                    message_with_name(dialog, full, L"The path does not exist.\nCheck the path and try again.");
                    free_results(dialog);
                    return FALSE;
                }
            }
            if (!exists && !dialog->Save) {
                if (dialog->Options & FOS_CREATEPROMPT) {
                    WCHAR ask[MAX_PATH + 100];
                    wsprintfW(ask, L"%s does not exist.\nDo you want to create a new file?", full);
                    if (fd_message(dialog, ask, MB_YESNO | MB_ICONQUESTION) != IDYES) {
                        free_results(dialog);
                        return FALSE;
                    }
                } else if (dialog->Options & FOS_FILEMUSTEXIST) {
                    message_with_name(dialog, PathFindFileNameW(full), L"File not found.\nCheck the file name and try again.");
                    free_results(dialog);
                    return FALSE;
                }
            }
            if (exists && dialog->Save && (dialog->Options & FOS_OVERWRITEPROMPT)) {
                int answer = FDEOR_DEFAULT;
                if (dialog->Events.Overwrite)
                    answer = dialog->Events.Overwrite(dialog, dialog->Context, full);
                if (answer == FDEOR_REFUSE) {
                    free_results(dialog);
                    return FALSE;
                }
                if (answer == FDEOR_DEFAULT) {
                    WCHAR ask[MAX_PATH + 100];
                    wsprintfW(ask, L"%s already exists.\nDo you want to replace it?", PathFindFileNameW(full));
                    if (fd_message(dialog, ask, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                        free_results(dialog);
                        return FALSE;
                    }
                }
            }
            if (!add_result(dialog, full)) {
                free_results(dialog);
                return FALSE;
            }
        }
        if (!dialog->ResultCount)
            return FALSE;
    }
    if (dialog->Events.FileOk && !dialog->Events.FileOk(dialog, dialog->Context)) {
        free_results(dialog);
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the owner's controls */

static FD_CONTROL *find_control(FILEDLG *dialog, DWORD id)
{
    for (FD_CONTROL *c = dialog->Controls; c; c = c->Next)
        if (c->Id == id)
            return c;
    return nullptr;
}

static int control_command_id(FILEDLG *dialog, const FD_CONTROL *control)
{
    int n = 0;
    for (FD_CONTROL *c = dialog->Controls; c; c = c->Next, n++)
        if (c == control)
            return IDC_CUSTOM_BASE + n * 64;
    return 0;
}

static struct _FD_ITEM *find_item(FD_CONTROL *control, DWORD item)
{
    for (struct _FD_ITEM *i = control->Items; i; i = i->Next)
        if (i->Id == item)
            return i;
    return nullptr;
}

static void set_font(FILEDLG *dialog, HWND window)
{
    if (window && dialog->Font)
        SendMessageW(window, WM_SETFONT, (WPARAM)dialog->Font, FALSE);
}

static HWND make_child(FILEDLG *dialog, LPCWSTR class_name, LPCWSTR text, DWORD style, DWORD ex_style, int id)
{
    HWND window = CreateWindowExW(ex_style, class_name, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, dialog->Hwnd,
                                  (HMENU)(INT_PTR)id, nullptr, nullptr);
    set_font(dialog, window);
    return window;
}

/* makes the windows of one control of the owner, if the dialog is up. */
static void create_control_windows(FILEDLG *dialog, FD_CONTROL *control)
{
    if (!dialog->Hwnd || control->Window || control->Label)
        return;
    int id = control_command_id(dialog, control);
    switch (control->Kind) {
    case FDC_TEXT:
        control->Window = make_child(dialog, L"Static", control->Text ? control->Text : L"", SS_LEFT | SS_NOPREFIX, 0, id);
        break;
    case FDC_CHECK:
        control->Window = make_child(dialog, L"Button", control->Text ? control->Text : L"",
                                     BS_AUTOCHECKBOX | WS_TABSTOP, 0, id);
        SendMessageW(control->Window, BM_SETCHECK, control->Checked ? BST_CHECKED : BST_UNCHECKED, 0);
        break;
    case FDC_BUTTON:
        control->Window = make_child(dialog, L"Button", control->Text ? control->Text : L"",
                                     BS_PUSHBUTTON | WS_TABSTOP, 0, id);
        break;
    case FDC_COMBO:
        control->Label = make_child(dialog, L"Static", control->Text ? control->Text : L"", SS_LEFT | SS_NOPREFIX, 0, -1);
        control->Window = make_child(dialog, L"ComboBox", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, id);
        for (struct _FD_ITEM *item = control->Items; item; item = item->Next) {
            int at = (int)SendMessageW(control->Window, CB_ADDSTRING, 0, (LPARAM)(item->Text ? item->Text : L""));
            SendMessageW(control->Window, CB_SETITEMDATA, at, item->Id);
            if (item->Id == control->Selected)
                SendMessageW(control->Window, CB_SETCURSEL, at, 0);
        }
        break;
    case FDC_RADIO: {
        int n = 0;
        control->Label = make_child(dialog, L"Static", control->Text ? control->Text : L"", SS_LEFT | SS_NOPREFIX, 0, -1);
        for (struct _FD_ITEM *item = control->Items; item; item = item->Next, n++) {
            item->Window = make_child(dialog, L"Button", item->Text ? item->Text : L"",
                                      BS_AUTORADIOBUTTON | WS_TABSTOP | (n ? 0 : WS_GROUP), 0, id + 1 + n);
            if (item->Id == control->Selected)
                SendMessageW(item->Window, BM_SETCHECK, BST_CHECKED, 0);
        }
        break;
    }
    case FDC_EDIT:
        control->Label = make_child(dialog, L"Static", control->Text ? control->Text : L"", SS_LEFT | SS_NOPREFIX, 0, -1);
        control->Window = make_child(dialog, L"Edit", control->Content ? control->Content : L"",
                                     ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, id);
        break;
    case FDC_SEPARATOR:
        control->Window = make_child(dialog, L"Static", L"", SS_ETCHEDHORZ, 0, id);
        break;
    case FDC_GROUP:
        control->Window = make_child(dialog, L"Static", control->Text ? control->Text : L"", SS_LEFT | SS_NOPREFIX, 0, id);
        break;
    }
    if (control->State == 0 && control->Kind != FDC_SEPARATOR && control->Kind != FDC_GROUP && control->Kind != FDC_TEXT)
        control->State = CDCS_ENABLEDVISIBLE;
}

static void apply_control_state(FD_CONTROL *control)
{
    BOOL visible = (control->State & CDCS_VISIBLE) != 0, enabled = (control->State & CDCS_ENABLED) != 0;
    HWND windows[3] = {control->Window, control->Label, nullptr};
    for (int i = 0; i < 2; i++) {
        if (!windows[i])
            continue;
        ShowWindow(windows[i], visible ? SW_SHOWNA : SW_HIDE);
        EnableWindow(windows[i], enabled);
    }
    for (struct _FD_ITEM *item = control->Items; control->Kind == FDC_RADIO && item; item = item->Next) {
        if (!item->Window)
            continue;
        ShowWindow(item->Window, visible && (item->State & CDCS_VISIBLE) ? SW_SHOWNA : SW_HIDE);
        EnableWindow(item->Window, enabled && (item->State & CDCS_ENABLED));
    }
}

HRESULT FdAddControl(FILEDLG *dialog, FD_CONTROL_KIND kind, DWORD id, LPCWSTR text)
{
    if (find_control(dialog, id))
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    FD_CONTROL *control = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*control));
    if (!control)
        return E_OUTOFMEMORY;
    control->Kind = kind;
    control->Id = id;
    control->State = CDCS_ENABLEDVISIBLE;
    if (text && FAILED(FdDuplicate(text, kind == FDC_EDIT ? &control->Content : &control->Text))) {
        fd_free(control);
        return E_OUTOFMEMORY;
    }
    if (kind == FDC_CHECK && text)
        control->Checked = FALSE;
    FD_CONTROL **tail = &dialog->Controls;
    while (*tail)
        tail = &(*tail)->Next;
    *tail = control;
    if (dialog->Hwnd) {
        create_control_windows(dialog, control);
        apply_control_state(control);
        SendMessageW(dialog->Hwnd, WM_SIZE, SIZE_RESTORED, 0);
    }
    return S_OK;
}

HRESULT FdAddControlItem(FILEDLG *dialog, DWORD id, DWORD item_id, LPCWSTR text)
{
    FD_CONTROL *control = find_control(dialog, id);
    if (!control || (control->Kind != FDC_COMBO && control->Kind != FDC_RADIO))
        return E_FAIL;
    if (find_item(control, item_id))
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    struct _FD_ITEM *item = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*item));
    if (!item)
        return E_OUTOFMEMORY;
    item->Id = item_id;
    item->State = CDCS_ENABLEDVISIBLE;
    if (text && FAILED(FdDuplicate(text, &item->Text))) {
        fd_free(item);
        return E_OUTOFMEMORY;
    }
    struct _FD_ITEM **tail = &control->Items;
    while (*tail)
        tail = &(*tail)->Next;
    *tail = item;
    if (dialog->Hwnd && control->Window) {
        if (control->Kind == FDC_COMBO) {
            int at = (int)SendMessageW(control->Window, CB_ADDSTRING, 0, (LPARAM)(text ? text : L""));
            SendMessageW(control->Window, CB_SETITEMDATA, at, item_id);
            if (SendMessageW(control->Window, CB_GETCURSEL, 0, 0) == CB_ERR)
                SendMessageW(control->Window, CB_SETCURSEL, at, 0);
        }
    }
    if (dialog->Hwnd && control->Kind == FDC_RADIO) {
        int n = 0, id_base = control_command_id(dialog, control);
        for (struct _FD_ITEM *i = control->Items; i; i = i->Next)
            n++;
        item->Window = make_child(dialog, L"Button", text ? text : L"", BS_AUTORADIOBUTTON | WS_TABSTOP | (n == 1 ? WS_GROUP : 0),
                                  0, id_base + n);
        SendMessageW(dialog->Hwnd, WM_SIZE, SIZE_RESTORED, 0);
    }
    return S_OK;
}

HRESULT FdRemoveControlItem(FILEDLG *dialog, DWORD id, DWORD item_id, BOOL all)
{
    FD_CONTROL *control = find_control(dialog, id);
    if (!control)
        return E_FAIL;
    for (struct _FD_ITEM **p = &control->Items; *p;) {
        struct _FD_ITEM *item = *p;
        if (!all && item->Id != item_id) {
            p = &item->Next;
            continue;
        }
        *p = item->Next;
        if (control->Kind == FDC_COMBO && control->Window) {
            int count = (int)SendMessageW(control->Window, CB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; i++)
                if ((DWORD)SendMessageW(control->Window, CB_GETITEMDATA, i, 0) == item->Id) {
                    SendMessageW(control->Window, CB_DELETESTRING, i, 0);
                    break;
                }
        }
        if (item->Window)
            DestroyWindow(item->Window);
        fd_free(item->Text);
        fd_free(item);
        if (!all)
            break;
    }
    if (dialog->Hwnd)
        SendMessageW(dialog->Hwnd, WM_SIZE, SIZE_RESTORED, 0);
    return S_OK;
}

HRESULT FdSetControlText(FILEDLG *dialog, DWORD id, DWORD item_id, LPCWSTR text, BOOL label)
{
    FD_CONTROL *control = find_control(dialog, id);
    if (!control)
        return E_FAIL;
    if (!label && control->Kind == FDC_EDIT) {
        HRESULT hr = set_text(&control->Content, text);
        if (control->Window)
            SetWindowTextW(control->Window, text ? text : L"");
        return hr;
    }
    if (!label) {
        struct _FD_ITEM *item = find_item(control, item_id);
        if (!item)
            return E_FAIL;
        set_text(&item->Text, text);
        if (item->Window)
            SetWindowTextW(item->Window, text ? text : L"");
        if (control->Kind == FDC_COMBO && control->Window) {
            int count = (int)SendMessageW(control->Window, CB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; i++)
                if ((DWORD)SendMessageW(control->Window, CB_GETITEMDATA, i, 0) == item_id) {
                    int selected = (int)SendMessageW(control->Window, CB_GETCURSEL, 0, 0);
                    SendMessageW(control->Window, CB_DELETESTRING, i, 0);
                    SendMessageW(control->Window, CB_INSERTSTRING, i, (LPARAM)(text ? text : L""));
                    SendMessageW(control->Window, CB_SETITEMDATA, i, item_id);
                    SendMessageW(control->Window, CB_SETCURSEL, selected, 0);
                    break;
                }
        }
        return S_OK;
    }
    HRESULT hr = set_text(&control->Text, text);
    if (control->Kind == FDC_COMBO || control->Kind == FDC_RADIO || control->Kind == FDC_EDIT) {
        if (control->Label)
            SetWindowTextW(control->Label, text ? text : L"");
    } else if (control->Window && control->Kind != FDC_SEPARATOR) {
        SetWindowTextW(control->Window, text ? text : L"");
    }
    if (dialog->Hwnd)
        SendMessageW(dialog->Hwnd, WM_SIZE, SIZE_RESTORED, 0);
    return hr;
}

HRESULT FdGetControlText(FILEDLG *dialog, DWORD id, LPWSTR *text)
{
    FD_CONTROL *control = find_control(dialog, id);
    *text = nullptr;
    if (!control || control->Kind != FDC_EDIT)
        return E_FAIL;
    if (control->Window) {
        int length = GetWindowTextLengthW(control->Window);
        WCHAR *buffer = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)length + 1) * sizeof(WCHAR));
        if (!buffer)
            return E_OUTOFMEMORY;
        GetWindowTextW(control->Window, buffer, length + 1);
        *text = buffer;
        return S_OK;
    }
    return FdDuplicate(control->Content ? control->Content : L"", text);
}

HRESULT FdSetControlState(FILEDLG *dialog, DWORD id, int state)
{
    FD_CONTROL *control = find_control(dialog, id);
    if (!control)
        return E_FAIL;
    control->State = state;
    apply_control_state(control);
    if (dialog->Hwnd)
        SendMessageW(dialog->Hwnd, WM_SIZE, SIZE_RESTORED, 0);
    return S_OK;
}

HRESULT FdGetControlState(FILEDLG *dialog, DWORD id, int *state)
{
    FD_CONTROL *control = find_control(dialog, id);
    if (!control)
        return E_FAIL;
    *state = control->State;
    return S_OK;
}

HRESULT FdSetControlChecked(FILEDLG *dialog, DWORD id, BOOL checked)
{
    FD_CONTROL *control = find_control(dialog, id);
    if (!control || control->Kind != FDC_CHECK)
        return E_FAIL;
    control->Checked = checked;
    if (control->Window)
        SendMessageW(control->Window, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return S_OK;
}

HRESULT FdGetControlChecked(FILEDLG *dialog, DWORD id, BOOL *checked)
{
    FD_CONTROL *control = find_control(dialog, id);
    if (!control || control->Kind != FDC_CHECK)
        return E_FAIL;
    if (control->Window)
        control->Checked = SendMessageW(control->Window, BM_GETCHECK, 0, 0) == BST_CHECKED;
    *checked = control->Checked;
    return S_OK;
}

HRESULT FdSetControlSelection(FILEDLG *dialog, DWORD id, DWORD item_id)
{
    FD_CONTROL *control = find_control(dialog, id);
    if (!control || (control->Kind != FDC_COMBO && control->Kind != FDC_RADIO) || !find_item(control, item_id))
        return E_FAIL;
    control->Selected = item_id;
    if (control->Kind == FDC_COMBO && control->Window) {
        int count = (int)SendMessageW(control->Window, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < count; i++)
            if ((DWORD)SendMessageW(control->Window, CB_GETITEMDATA, i, 0) == item_id)
                SendMessageW(control->Window, CB_SETCURSEL, i, 0);
    }
    if (control->Kind == FDC_RADIO)
        for (struct _FD_ITEM *item = control->Items; item; item = item->Next)
            if (item->Window)
                SendMessageW(item->Window, BM_SETCHECK, item->Id == item_id ? BST_CHECKED : BST_UNCHECKED, 0);
    return S_OK;
}

HRESULT FdGetControlSelection(FILEDLG *dialog, DWORD id, DWORD *item_id)
{
    FD_CONTROL *control = find_control(dialog, id);
    if (!control || (control->Kind != FDC_COMBO && control->Kind != FDC_RADIO))
        return E_FAIL;
    if (control->Kind == FDC_COMBO && control->Window) {
        int at = (int)SendMessageW(control->Window, CB_GETCURSEL, 0, 0);
        if (at != CB_ERR)
            control->Selected = (DWORD)SendMessageW(control->Window, CB_GETITEMDATA, at, 0);
    }
    if (control->Kind == FDC_RADIO)
        for (struct _FD_ITEM *item = control->Items; item; item = item->Next)
            if (item->Window && SendMessageW(item->Window, BM_GETCHECK, 0, 0) == BST_CHECKED)
                control->Selected = item->Id;
    *item_id = control->Selected;
    return S_OK;
}

/* the controls of the owner, in a column above the file name, as high as they need. */
static int place_controls(FILEDLG *dialog, int x, int y, int width, BOOL move)
{
    int row = MulDiv(14, dialog->UnitY, 8), gap = MulDiv(2, dialog->UnitY, 8);
    int start = y, label_width = MulDiv(54, dialog->UnitX, 4);
    for (FD_CONTROL *c = dialog->Controls; c; c = c->Next) {
        if (!(c->State & CDCS_VISIBLE))
            continue;
        int height = row;
        switch (c->Kind) {
        case FDC_TEXT:
        case FDC_CHECK:
        case FDC_GROUP:
            if (move && c->Window)
                MoveWindow(c->Window, x, y, width, row, TRUE);
            break;
        case FDC_BUTTON:
            if (move && c->Window)
                MoveWindow(c->Window, x, y, MulDiv(70, dialog->UnitX, 4), row, TRUE);
            break;
        case FDC_COMBO:
        case FDC_EDIT: {
            int lw = (c->Text && c->Text[0]) ? label_width : 0;
            if (move && c->Label)
                MoveWindow(c->Label, x, y + MulDiv(2, dialog->UnitY, 8), lw, row, TRUE);
            if (move && c->Window)
                MoveWindow(c->Window, x + lw, y, c->Kind == FDC_COMBO ? width - lw : width - lw, c->Kind == FDC_COMBO ? row * 8 : row, TRUE);
            break;
        }
        case FDC_RADIO: {
            int n = 0;
            for (struct _FD_ITEM *item = c->Items; item; item = item->Next) {
                if (!(item->State & CDCS_VISIBLE))
                    continue;
                if (move && item->Window)
                    MoveWindow(item->Window, x, y + n * row, width, row, TRUE);
                n++;
            }
            height = n ? n * row : row;
            break;
        }
        case FDC_SEPARATOR:
            height = MulDiv(4, dialog->UnitY, 8);
            if (move && c->Window)
                MoveWindow(c->Window, x, y, width, 2, TRUE);
            break;
        }
        y += height + gap;
    }
    return y - start;
}

/* ------------------------------------------------------------------ glyphs for the buttons */

/* a 16 by 16 black picture drawn with '#', transparent elsewhere. */
static HICON make_glyph(const char *const rows[16])
{
    WORD and_bits[16];
    DWORD pixels[16 * 16];
    for (int y = 0; y < 16; y++) {
        WORD bits = 0;
        for (int x = 0; x < 16; x++) {
            BOOL set = rows[y][x] == '#';
            pixels[y * 16 + x] = set ? 0xFF000000 : 0;
            if (!set)
                bits |= (WORD)(0x8000 >> x);
        }
        and_bits[y] = (WORD)((bits >> 8) | (bits << 8));
    }
    ICONINFO info;
    memset(&info, 0, sizeof(info));
    info.fIcon = TRUE;
    info.hbmMask = CreateBitmap(16, 16, 1, 1, and_bits);
    info.hbmColor = CreateBitmap(16, 16, 1, 32, pixels);
    HICON icon = info.hbmMask && info.hbmColor ? CreateIconIndirect(&info) : nullptr;
    if (info.hbmMask)
        DeleteObject(info.hbmMask);
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    return icon;
}

static void make_glyphs(FILEDLG *dialog)
{
    static const char *const back[16] = {
        "................", "................", "................", ".....#..........",
        "....##..........", "...###..........", "..#############.", ".###############",
        "..#############.", "...###..........", "....##..........", ".....#..........",
        "................", "................", "................", "................",
    };
    static const char *const up[16] = {
        "................", ".......##.......", "......####......", ".....######.....",
        "....########....", "...##########...", "......####......", "......####......",
        "......####......", "......####......", "......####......", "......####......",
        "................", "................", "................", "................",
    };
    static const char *const folder[16] = {
        "................", "................", ".#####..........", "#.....#.........",
        "#......########.", "#..............#", "#......##......#", "#......##......#",
        "#...#######....#", "#...#######....#", "#......##......#", "#......##......#",
        "#..............#", ".##############.", "................", "................",
    };
    dialog->BackIcon = make_glyph(back);
    dialog->UpIcon = make_glyph(up);
    dialog->NewFolderIcon = make_glyph(folder);
}

/* ------------------------------------------------------------------ layout */

#define DX(n) MulDiv((n), dialog->UnitX, 4)
#define DY(n) MulDiv((n), dialog->UnitY, 8)

static int text_width(FILEDLG *dialog, LPCWSTR text)
{
    HDC dc = GetDC(dialog->Hwnd);
    HGDIOBJ old = dialog->Font ? SelectObject(dc, dialog->Font) : nullptr;
    SIZE size = {0, 0};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    if (old)
        SelectObject(dc, old);
    ReleaseDC(dialog->Hwnd, dc);
    return size.cx;
}

/* the name column takes what the others leave. */
static void fit_columns(FILEDLG *dialog)
{
    RECT rect;
    GetClientRect(dialog->List, &rect);
    int others = 0;
    for (int i = 1; i < 4; i++)
        others += (int)SendMessageW(dialog->List, LVM_GETCOLUMNWIDTH, i, 0);
    int width = rect.right - others - GetSystemMetrics(SM_CXVSCROLL);
    if (width < 120)
        width = 120;
    SendMessageW(dialog->List, LVM_SETCOLUMNWIDTH, 0, width);
}

static void layout(FILEDLG *dialog)
{
    RECT client;
    if (!dialog->Hwnd || !dialog->UnitX)
        return;
    GetClientRect(dialog->Hwnd, &client);
    int w = client.right, h = client.bottom;
    if (w < 50 || h < 50)
        return;
    int m = DX(7), gx = DX(4), gy = DY(4), row = DY(14), label_w = DX(54);
    int button_w = DX(50);
    WCHAR text[64];
    GetWindowTextW(dialog->OkButton, text, 64);
    if (text_width(dialog, text) + DX(12) > button_w)
        button_w = text_width(dialog, text) + DX(12);
    GetWindowTextW(dialog->CancelButton, text, 64);
    if (text_width(dialog, text) + DX(12) > button_w)
        button_w = text_width(dialog, text) + DX(12);

    int y_type = h - m - row, y_name = y_type - gy - row;
    int custom_h = place_controls(dialog, 0, 0, 0, FALSE);
    if (dialog->ReadOnlyShown)
        custom_h += row + DY(2);
    int list_bottom = y_name - gy - (custom_h ? custom_h + gy : 0);

    MoveWindow(dialog->Back, m, m, row, row, TRUE);
    MoveWindow(dialog->Up, m + row + gx / 2, m, row, row, TRUE);
    MoveWindow(dialog->NewFolder, w - m - row, m, row, row, TRUE);
    int address_x = m + 2 * row + gx / 2 + gx;
    MoveWindow(dialog->Address, address_x, m, w - m - row - gx - address_x, row, TRUE);

    int mid_y = m + row + gy, mid_h = list_bottom - mid_y;
    int tree_w = w * 3 / 10;
    if (tree_w < DX(100))
        tree_w = DX(100);
    MoveWindow(dialog->Tree, m, mid_y, tree_w, mid_h, TRUE);
    int list_x = m + tree_w + gx;
    MoveWindow(dialog->List, list_x, mid_y, w - m - list_x, mid_h, TRUE);
    fit_columns(dialog);

    int custom_y = list_bottom + gy;
    if (dialog->ReadOnlyShown) {
        MoveWindow(dialog->ReadOnlyBox, m + label_w, custom_y, w - 2 * m - label_w, row, TRUE);
        custom_y += row + DY(2);
    }
    place_controls(dialog, m + label_w, custom_y, w - 2 * m - label_w, TRUE);

    int edit_w = w - m - button_w - gx - (m + label_w);
    MoveWindow(dialog->NameLabelWindow, m, y_name + DY(2), label_w, row, TRUE);
    MoveWindow(dialog->NameEdit, m + label_w, y_name, edit_w, row, TRUE);
    MoveWindow(dialog->TypeLabel, m, y_type + DY(2), label_w, row, TRUE);
    MoveWindow(dialog->TypeCombo, m + label_w, y_type, edit_w, row * 8, TRUE);
    MoveWindow(dialog->OkButton, w - m - button_w, y_name, button_w, row, TRUE);
    MoveWindow(dialog->CancelButton, w - m - button_w, y_type, button_w, row, TRUE);
}


/* ------------------------------------------------------------------ a new folder */

typedef struct _PROMPT {
    WCHAR Text[MAX_PATH];
    HFONT Font;
} PROMPT;

/* a small dialog that asks for a name. */
static INT_PTR CALLBACK prompt_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    PROMPT *prompt = (PROMPT *)GetWindowLongPtrW(hwnd, DWLP_USER);
    switch (message) {
    case WM_INITDIALOG: {
        prompt = (PROMPT *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)prompt);
        RECT unit = {0, 0, 4, 8};
        MapDialogRect(hwnd, &unit);
        int ux = unit.right ? unit.right : 6, uy = unit.bottom ? unit.bottom : 13;
        HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        SetWindowTextW(hwnd, L"New folder");
        HWND label = CreateWindowExW(0, L"Static", L"Folder &name:", WS_CHILD | WS_VISIBLE | SS_LEFT, MulDiv(7, ux, 4),
                                     MulDiv(9, uy, 8), MulDiv(150, ux, 4), MulDiv(9, uy, 8), hwnd, (HMENU)-1, nullptr, nullptr);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", prompt->Text,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, MulDiv(7, ux, 4), MulDiv(20, uy, 8),
                                    MulDiv(176, ux, 4), MulDiv(14, uy, 8), hwnd, (HMENU)101, nullptr, nullptr);
        HWND ok = CreateWindowExW(0, L"Button", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                  MulDiv(75, ux, 4), MulDiv(42, uy, 8), MulDiv(50, ux, 4), MulDiv(14, uy, 8), hwnd,
                                  (HMENU)IDOK, nullptr, nullptr);
        HWND cancel = CreateWindowExW(0, L"Button", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                      MulDiv(133, ux, 4), MulDiv(42, uy, 8), MulDiv(50, ux, 4), MulDiv(14, uy, 8), hwnd,
                                      (HMENU)IDCANCEL, nullptr, nullptr);
        HWND all[4] = {label, edit, ok, cancel};
        for (int i = 0; i < 4; i++)
            SendMessageW(all[i], WM_SETFONT, (WPARAM)font, FALSE);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK) {
            GetWindowTextW(GetDlgItem(hwnd, 101), prompt->Text, MAX_PATH);
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* asks for a name and makes the folder, which is then the one selected. */
static void new_folder(FILEDLG *dialog)
{
    if (dialog->AtComputer)
        return;
    static const WORD tmpl[] = {0, 0, 0, 0, 0, 0, 0, 190, 62, 0, 0, 0, 9, 'S', 'e', 'g', 'o', 'e', ' ', 'U', 'I', 0};
    WORD *copy = HeapAlloc(GetProcessHeap(), 0, sizeof(tmpl));
    PROMPT prompt;
    if (!copy)
        return;
    memcpy(copy, tmpl, sizeof(tmpl));
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_VISIBLE;
    copy[0] = LOWORD(style);
    copy[1] = HIWORD(style);
    /* the first name that is free. */
    WCHAR path[MAX_PATH];
    for (int i = 1; i < 1000; i++) {
        if (i == 1)
            wcscpy(prompt.Text, L"New folder");
        else
            wsprintfW(prompt.Text, L"New folder (%d)", i);
        if (wcslen(dialog->Folder) + wcslen(prompt.Text) + 2 >= MAX_PATH)
            break;
        wcscpy(path, dialog->Folder);
        PathAddBackslashW(path);
        wcscat(path, prompt.Text);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            break;
    }
    INT_PTR answer = DialogBoxIndirectParamW(g_comdlg32, (LPCDLGTEMPLATEW)copy, dialog->Hwnd, prompt_proc, (LPARAM)&prompt);
    HeapFree(GetProcessHeap(), 0, copy);
    if (answer != IDOK || !prompt.Text[0])
        return;
    if (!valid_name(prompt.Text) || has_wildcard(prompt.Text) || wcslen(dialog->Folder) + wcslen(prompt.Text) + 2 >= MAX_PATH) {
        message_with_name(dialog, prompt.Text, L"The folder name is not valid.\nCheck the name and try again.");
        return;
    }
    wcscpy(path, dialog->Folder);
    PathAddBackslashW(path);
    wcscat(path, prompt.Text);
    if (!CreateDirectoryW(path, nullptr)) {
        DWORD error = GetLastError();
        message_with_name(dialog, prompt.Text,
                          error == ERROR_ALREADY_EXISTS ? L"A file or folder with this name already exists."
                                                        : L"The folder could not be created.");
        return;
    }
    refresh(dialog);
    dialog->Busy++;
    int rows = (int)SendMessageW(dialog->List, LVM_GETITEMCOUNT, 0, 0);
    for (int row = 0; row < rows; row++) {
        const FD_ENTRY *entry = row_entry(dialog, row);
        if (entry && !_wcsicmp(entry->Name, prompt.Text)) {
            LVITEMW item;
            memset(&item, 0, sizeof(item));
            item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
            item.state = LVIS_SELECTED | LVIS_FOCUSED;
            SendMessageW(dialog->List, LVM_SETITEMSTATE, row, (LPARAM)&item);
            SendMessageW(dialog->List, LVM_ENSUREVISIBLE, row, FALSE);
            break;
        }
    }
    dialog->Busy--;
    /* a node of the tree for it, if its parent shows its folders. */
    HTREEITEM selected = (HTREEITEM)SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_CARET, 0);
    FD_NODE *node = selected ? tree_node(dialog, selected) : nullptr;
    if (node && node->Filled && node->Kind == 2 && !_wcsicmp(node->Path, dialog->Folder)) {
        node->Filled = FALSE;
        while (SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)selected))
            SendMessageW(dialog->Tree, TVM_DELETEITEM, 0,
                         SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)selected));
        TVITEMW tv;
        memset(&tv, 0, sizeof(tv));
        tv.mask = TVIF_CHILDREN;
        tv.hItem = selected;
        tv.cChildren = 1;
        SendMessageW(dialog->Tree, TVM_SETITEMW, 0, (LPARAM)&tv);
    }
}

/* ------------------------------------------------------------------ the dialog procedure */

static FILEDLG *dialog_of(HWND hwnd)
{
    return (FILEDLG *)GetWindowLongPtrW(hwnd, DWLP_USER);
}

static void fill_types(FILEDLG *dialog)
{
    SendMessageW(dialog->TypeCombo, CB_RESETCONTENT, 0, 0);
    for (UINT i = 0; i < dialog->TypeCount; i++)
        SendMessageW(dialog->TypeCombo, CB_ADDSTRING, 0, (LPARAM)dialog->TypeNames[i]);
    if (dialog->TypeCount)
        SendMessageW(dialog->TypeCombo, CB_SETCURSEL, dialog->TypeIndex - 1, 0);
    BOOL shown = dialog->TypeCount != 0;
    ShowWindow(dialog->TypeCombo, shown ? SW_SHOWNA : SW_HIDE);
    ShowWindow(dialog->TypeLabel, shown ? SW_SHOWNA : SW_HIDE);
}

static void create_controls(FILEDLG *dialog)
{
    HWND hwnd = dialog->Hwnd;
    DWORD list_style = WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | LVS_AUTOARRANGE;
    if (!(dialog->Options & FOS_ALLOWMULTISELECT))
        list_style |= LVS_SINGLESEL;
    dialog->Back = make_child(dialog, L"Button", L"", BS_ICON | WS_TABSTOP, 0, IDC_BACK);
    dialog->Up = make_child(dialog, L"Button", L"", BS_ICON | WS_TABSTOP, 0, IDC_UP);
    dialog->NewFolder = make_child(dialog, L"Button", L"", BS_ICON | WS_TABSTOP, 0, IDC_NEWFOLDER);
    SendMessageW(dialog->Back, BM_SETIMAGE, IMAGE_ICON, (LPARAM)dialog->BackIcon);
    SendMessageW(dialog->Up, BM_SETIMAGE, IMAGE_ICON, (LPARAM)dialog->UpIcon);
    SendMessageW(dialog->NewFolder, BM_SETIMAGE, IMAGE_ICON, (LPARAM)dialog->NewFolderIcon);
    dialog->Address = make_child(dialog, L"Edit", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_ADDRESS);
    dialog->Tree = make_child(dialog, L"SysTreeView32", L"",
                              TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS | WS_TABSTOP,
                              WS_EX_CLIENTEDGE, IDC_TREE);
    dialog->List = make_child(dialog, L"SysListView32", L"", list_style, WS_EX_CLIENTEDGE, IDC_LIST);
    SendMessageW(dialog->List, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SendMessageW(dialog->List, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)dialog->Images);
    list_set_columns(dialog);
    dialog->NameLabelWindow = make_child(dialog, L"Static", dialog->NameLabel ? dialog->NameLabel : L"File &name:",
                                         SS_LEFT, 0, IDC_NAMELABEL);
    dialog->NameEdit = make_child(dialog, L"Edit", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_NAME);
    dialog->TypeLabel = make_child(dialog, L"Static", dialog->Save ? L"Save as &type:" : L"Files of &type:", SS_LEFT, 0,
                                   IDC_TYPELABEL);
    dialog->TypeCombo = make_child(dialog, L"ComboBox", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDC_TYPE);
    dialog->ReadOnlyBox = make_child(dialog, L"Button", L"Open as &read-only", BS_AUTOCHECKBOX | WS_TABSTOP, 0,
                                     IDC_READONLY);
    ShowWindow(dialog->ReadOnlyBox, dialog->ReadOnlyShown ? SW_SHOWNA : SW_HIDE);
    SendMessageW(dialog->ReadOnlyBox, BM_SETCHECK, dialog->ReadOnlyChecked ? BST_CHECKED : BST_UNCHECKED, 0);
    LPCWSTR ok = dialog->OkLabel ? dialog->OkLabel
                 : (dialog->Options & FOS_PICKFOLDERS) ? L"Select Folder"
                 : dialog->Save ? L"&Save" : L"&Open";
    dialog->OkButton = make_child(dialog, L"Button", ok, BS_DEFPUSHBUTTON | WS_TABSTOP, 0, IDOK);
    dialog->CancelButton = make_child(dialog, L"Button", dialog->CancelLabel ? dialog->CancelLabel : L"Cancel",
                                      BS_PUSHBUTTON | WS_TABSTOP, 0, IDCANCEL);
    for (FD_CONTROL *c = dialog->Controls; c; c = c->Next) {
        create_control_windows(dialog, c);
        apply_control_state(c);
    }
    (void)hwnd;
}

/* the part of a name that is not the extension, for selecting it in the box. */
static int base_length(LPCWSTR name)
{
    LPCWSTR dot = wcsrchr(name, L'.');
    return dot && dot != name ? (int)(dot - name) : lstrlenW(name);
}

static void on_type_changed(FILEDLG *dialog)
{
    int sel = (int)SendMessageW(dialog->TypeCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR)
        return;
    UINT previous = dialog->TypeIndex;
    dialog->TypeIndex = (UINT)sel + 1;
    dialog->Wildcard[0] = 0;
    /* a name saved with the old type's extension follows the new type. */
    if (dialog->Save && previous != dialog->TypeIndex && previous >= 1 && previous <= dialog->TypeCount) {
        WCHAR old_ext[32], new_ext[32], text[MAX_PATH];
        if (spec_extension(dialog->TypeSpecs[previous - 1], old_ext, 32) &&
            spec_extension(dialog->TypeSpecs[dialog->TypeIndex - 1], new_ext, 32)) {
            GetWindowTextW(dialog->NameEdit, text, MAX_PATH);
            LPWSTR dot = wcsrchr(text, L'.');
            if (dot && !_wcsicmp(dot + 1, old_ext) && (dot - text) + 2 + wcslen(new_ext) < MAX_PATH) {
                wcscpy(dot + 1, new_ext);
                dialog->SyncingName = TRUE;
                SetWindowTextW(dialog->NameEdit, text);
                dialog->SyncingName = FALSE;
            }
        }
    }
    refresh(dialog);
    if (dialog->Events.TypeChange)
        dialog->Events.TypeChange(dialog, dialog->Context);
}

static void on_control_command(FILEDLG *dialog, int id, int code)
{
    int n = (id - IDC_CUSTOM_BASE) / 64, sub = (id - IDC_CUSTOM_BASE) % 64;
    FD_CONTROL *control = dialog->Controls;
    while (control && n--)
        control = control->Next;
    if (!control || !dialog->ControlHandler)
        return;
    switch (control->Kind) {
    case FDC_BUTTON:
        if (code == BN_CLICKED)
            dialog->ControlHandler(dialog, dialog->ControlContext, 0, control->Id, 0, FALSE);
        break;
    case FDC_CHECK:
        if (code == BN_CLICKED) {
            control->Checked = SendMessageW(control->Window, BM_GETCHECK, 0, 0) == BST_CHECKED;
            dialog->ControlHandler(dialog, dialog->ControlContext, 1, control->Id, 0, control->Checked);
        }
        break;
    case FDC_COMBO:
        if (code == CBN_SELCHANGE) {
            DWORD item = 0;
            FdGetControlSelection(dialog, control->Id, &item);
            dialog->ControlHandler(dialog, dialog->ControlContext, 2, control->Id, item, FALSE);
        }
        break;
    case FDC_RADIO:
        if (code == BN_CLICKED && sub >= 1) {
            int index = 1;
            for (struct _FD_ITEM *item = control->Items; item; item = item->Next, index++)
                if (index == sub) {
                    control->Selected = item->Id;
                    dialog->ControlHandler(dialog, dialog->ControlContext, 2, control->Id, item->Id, FALSE);
                    break;
                }
        }
        break;
    default:
        break;
    }
}

static void end_dialog(FILEDLG *dialog, HRESULT result, INT_PTR code)
{
    if (dialog->Closed)
        result = dialog->Result;
    dialog->Result = result;
    EndDialog(dialog->Hwnd, code);
}

static void on_ok(FILEDLG *dialog)
{
    HWND focus = GetFocus();
    if (focus == dialog->Address) {
        WCHAR text[MAX_PATH];
        GetWindowTextW(dialog->Address, text, MAX_PATH);
        if (!_wcsicmp(text, L"This PC"))
            navigate(dialog, L"", TRUE, TRUE);
        else if (!navigate(dialog, text, TRUE, TRUE))
            message_with_name(dialog, text, L"The folder does not exist.\nCheck the path and try again.");
        return;
    }
    if (focus == dialog->Tree) {
        HTREEITEM item = (HTREEITEM)SendMessageW(dialog->Tree, TVM_GETNEXTITEM, TVGN_CARET, 0);
        if (item)
            SendMessageW(dialog->Tree, TVM_EXPAND, TVE_TOGGLE, (LPARAM)item);
        return;
    }
    if (accept(dialog)) {
        if (!dialog->AtComputer && !(dialog->Options & FOS_NOCHANGEDIR) && 0)
            SetCurrentDirectoryW(dialog->Folder);
        end_dialog(dialog, S_OK, IDOK);
    }
}

static void remember_selection(FILEDLG *dialog, WCHAR names[][MAX_PATH], UINT *count, UINT max)
{
    const FD_ENTRY *entries[64];
    UINT n = selected_entries(dialog, entries, 64);
    *count = 0;
    for (UINT i = 0; i < n && *count < max; i++)
        wcscpy(names[(*count)++], entries[i]->Name);
}

static void restore_selection(FILEDLG *dialog, WCHAR names[][MAX_PATH], UINT count)
{
    int rows = (int)SendMessageW(dialog->List, LVM_GETITEMCOUNT, 0, 0);
    for (int row = 0; row < rows; row++) {
        const FD_ENTRY *entry = row_entry(dialog, row);
        if (!entry)
            continue;
        for (UINT i = 0; i < count; i++)
            if (!_wcsicmp(entry->Name, names[i])) {
                LVITEMW item;
                memset(&item, 0, sizeof(item));
                item.stateMask = LVIS_SELECTED;
                item.state = LVIS_SELECTED;
                SendMessageW(dialog->List, LVM_SETITEMSTATE, row, (LPARAM)&item);
            }
    }
}

static void on_list_notify(FILEDLG *dialog, NMHDR *header)
{
    switch (header->code) {
    case NM_DBLCLK: {
        NMITEMACTIVATE *activate = (NMITEMACTIVATE *)header;
        const FD_ENTRY *entry = row_entry(dialog, activate->iItem);
        if (!entry)
            break;
        WCHAR path[MAX_PATH];
        if (entry->Folder && entry_path(dialog, entry, path))
            navigate(dialog, path, TRUE, TRUE);
        else
            PostMessageW(dialog->Hwnd, WM_COMMAND, IDOK, 0);
        break;
    }
    case LVN_ITEMCHANGED: {
        NMLISTVIEW *change = (NMLISTVIEW *)header;
        if (dialog->Busy || !(change->uChanged & LVIF_STATE) || ((change->uNewState ^ change->uOldState) & LVIS_SELECTED) == 0)
            break;
        name_from_selection(dialog);
        if (dialog->Events.SelectionChange && !dialog->Navigating)
            dialog->Events.SelectionChange(dialog, dialog->Context);
        break;
    }
    case LVN_COLUMNCLICK: {
        NMLISTVIEW *click = (NMLISTVIEW *)header;
        WCHAR names[16][MAX_PATH];
        UINT count;
        remember_selection(dialog, names, &count, 16);
        if (dialog->SortColumn == click->iSubItem)
            dialog->SortDescending = !dialog->SortDescending;
        else {
            dialog->SortColumn = click->iSubItem;
            dialog->SortDescending = FALSE;
        }
        dialog->Busy++;
        list_fill(dialog);
        restore_selection(dialog, names, count);
        dialog->Busy--;
        break;
    }
    case LVN_KEYDOWN: {
        NMLVKEYDOWN *key = (NMLVKEYDOWN *)header;
        BOOL alt = GetKeyState(VK_MENU) < 0;
        if (key->wVKey == VK_BACK || (alt && key->wVKey == VK_UP))
            go_up(dialog);
        else if (key->wVKey == VK_F5)
            refresh(dialog);
        else if (alt && key->wVKey == VK_LEFT)
            go_back(dialog);
        else if (alt && key->wVKey == VK_RIGHT)
            go_forward(dialog);
        break;
    }
    }
}

static void on_tree_notify(FILEDLG *dialog, NMHDR *header)
{
    NMTREEVIEWW *tv = (NMTREEVIEWW *)header;
    switch (header->code) {
    case TVN_SELCHANGEDW: {
        if (dialog->Syncing)
            break;
        FD_NODE *node = (FD_NODE *)tv->itemNew.lParam;
        if (!node)
            break;
        if (node->Kind == 1)
            navigate(dialog, L"", TRUE, FALSE);
        else if (node->Kind == 2)
            navigate(dialog, node->Path, TRUE, FALSE);
        break;
    }
    case TVN_ITEMEXPANDINGW:
        if (tv->action == TVE_EXPAND && tv->itemNew.hItem)
            tree_fill(dialog, tv->itemNew.hItem);
        break;
    case TVN_DELETEITEMW:
        fd_free((void *)tv->itemOld.lParam);
        break;
    }
}

static INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    FILEDLG *dialog = message == WM_INITDIALOG ? (FILEDLG *)lparam : dialog_of(hwnd);
    if (!dialog)
        return FALSE;
    switch (message) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)dialog);
        dialog->Hwnd = hwnd;
        RECT unit = {0, 0, 4, 8};
        MapDialogRect(hwnd, &unit);
        dialog->UnitX = unit.right ? unit.right : 6;
        dialog->UnitY = unit.bottom ? unit.bottom : 13;
        dialog->Font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        SetWindowTextW(hwnd, dialog->Title ? dialog->Title : (dialog->Save ? L"Save As" : L"Open"));
        load_images(dialog);
        make_glyphs(dialog);
        create_controls(dialog);
        /* laid out before it has text, since a box holding text when it grows keeps the text scrolled. */
        dialog->Showing = TRUE;
        layout(dialog);
        fill_types(dialog);
        tree_build(dialog);
        SendMessageW(hwnd, DM_SETDEFID, IDOK, 0);
        WCHAR start[MAX_PATH];
        dialog->Showing = FALSE;
        if (!starting_folder(dialog, start))
            start[0] = 0;
        dialog->Showing = TRUE;
        if (!navigate(dialog, start, TRUE, TRUE))
            navigate(dialog, L"", TRUE, TRUE);
        if (dialog->InitialName) {
            dialog->SyncingName = TRUE;
            SetWindowTextW(dialog->NameEdit, dialog->InitialName);
            dialog->SyncingName = FALSE;
            if (dialog->Save)
                SendMessageW(dialog->NameEdit, EM_SETSEL, 0, base_length(dialog->InitialName));
        }
        layout(dialog);
        if (dialog->Events.Initialized)
            dialog->Events.Initialized(dialog, dialog->Context);
        SetFocus(dialog->NameEdit);
        return FALSE;
    }
    case WM_SIZE:
        layout(dialog);
        return FALSE;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lparam;
        RECT r = {0, 0, 250, 140};
        MapDialogRect(hwnd, &r);
        RECT frame = {0, 0, 0, 0};
        AdjustWindowRectEx(&frame, GetWindowLongW(hwnd, GWL_STYLE), FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
        info->ptMinTrackSize.x = r.right + (frame.right - frame.left);
        info->ptMinTrackSize.y = r.bottom + (frame.bottom - frame.top);
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wparam), code = HIWORD(wparam);
        switch (id) {
        case IDOK:
            on_ok(dialog);
            return TRUE;
        case IDCANCEL:
            end_dialog(dialog, HRESULT_FROM_WIN32(ERROR_CANCELLED), IDCANCEL);
            return TRUE;
        case IDC_BACK:
            go_back(dialog);
            return TRUE;
        case IDC_UP:
            go_up(dialog);
            return TRUE;
        case IDC_NEWFOLDER:
            new_folder(dialog);
            return TRUE;
        case IDC_TYPE:
            if (code == CBN_SELCHANGE)
                on_type_changed(dialog);
            return TRUE;
        case IDC_READONLY:
            if (code == BN_CLICKED)
                dialog->ReadOnlyChecked = SendMessageW(dialog->ReadOnlyBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
            return TRUE;
        default:
            if (id >= IDC_CUSTOM_BASE)
                on_control_command(dialog, id, code);
            return FALSE;
        }
    }
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)lparam;
        if (header->idFrom == IDC_LIST)
            on_list_notify(dialog, header);
        else if (header->idFrom == IDC_TREE)
            on_tree_notify(dialog, header);
        return FALSE;
    }
    case CDM_GETSPEC:
    case CDM_GETFILEPATH:
    case CDM_GETFOLDERPATH: {
        WCHAR text[MAX_PATH * 2];
        text[0] = 0;
        if (message == CDM_GETFOLDERPATH) {
            lstrcpynW(text, dialog->Folder, MAX_PATH);
        } else {
            name_text(dialog, text, RTL_NUMBER_OF(text));
            if (message == CDM_GETFILEPATH && text[0] != L'"') {
                WCHAR full[MAX_PATH];
                if (resolve_name(dialog, text, full))
                    lstrcpynW(text, full, RTL_NUMBER_OF(text));
            }
        }
        int needed = lstrlenW(text) + 1;
        if (lparam && (int)wparam >= needed)
            lstrcpyW((WCHAR *)lparam, text);
        SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, needed);
        return TRUE;
    }
    case CDM_SETCONTROLTEXT:
    case CDM_HIDECONTROL: {
        /* the controls under the numbers dlgs.h gives them. */
        HWND control = nullptr;
        switch (wparam) {
        case IDOK:
            control = dialog->OkButton;
            break;
        case IDCANCEL:
            control = dialog->CancelButton;
            break;
        case 0x441:
            control = dialog->TypeLabel;
            break;
        case 0x442:
            control = dialog->NameLabelWindow;
            break;
        case 0x470:
            control = dialog->TypeCombo;
            break;
        case 0x480:
            control = dialog->NameEdit;
            break;
        case 0x410:
            control = dialog->ReadOnlyBox;
            break;
        }
        if (control) {
            if (message == CDM_SETCONTROLTEXT)
                SetWindowTextW(control, (LPCWSTR)lparam);
            else
                ShowWindow(control, SW_HIDE);
        }
        return TRUE;
    }
    case CDM_SETDEFEXT:
        FdSetDefaultExtension(dialog, (LPCWSTR)lparam);
        return TRUE;
    case WM_FD_CLOSE:
        end_dialog(dialog, dialog->Result, IDABORT);
        return TRUE;
    case WM_FD_NAVIGATE:
        if (lparam) {
            navigate(dialog, (LPCWSTR)lparam, TRUE, TRUE);
            if (wparam)
                fd_free((void *)lparam);
        }
        return TRUE;
    case WM_DESTROY:
        if (dialog->Events.Closing)
            dialog->Events.Closing(dialog, dialog->Context);
        if (dialog->Tree)
            SendMessageW(dialog->Tree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
        free_entries(dialog);
        if (dialog->BackIcon)
            DestroyIcon(dialog->BackIcon);
        if (dialog->UpIcon)
            DestroyIcon(dialog->UpIcon);
        if (dialog->NewFolderIcon)
            DestroyIcon(dialog->NewFolderIcon);
        dialog->BackIcon = dialog->UpIcon = dialog->NewFolderIcon = nullptr;
        return FALSE;
    case WM_NCDESTROY:
        if (dialog->Images)
            ImageList_Destroy(dialog->Images);
        dialog->Images = nullptr;
        dialog->Hwnd = dialog->Back = dialog->Up = dialog->NewFolder = dialog->Address = dialog->Tree = dialog->List =
            dialog->NameLabelWindow = dialog->NameEdit = dialog->TypeLabel = dialog->TypeCombo = dialog->OkButton =
                dialog->CancelButton = dialog->ReadOnlyBox = nullptr;
        for (FD_CONTROL *c = dialog->Controls; c; c = c->Next) {
            c->Window = c->Label = nullptr;
            for (struct _FD_ITEM *i = c->Items; i; i = i->Next)
                i->Window = nullptr;
        }
        return FALSE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ showing it */

HRESULT FdShow(FILEDLG *dialog, HWND owner)
{
    static const INITCOMMONCONTROLSEX classes = {sizeof(INITCOMMONCONTROLSEX),
                                                  ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES};
    /* a dialog template: a captioned, sized frame in dialog units, with Segoe UI 9. */
    static const WORD tmpl[] = {
        0, 0, 0, 0, 0, 0, 0, 440, 250, 0, 0, 0, 9, 'S', 'e', 'g', 'o', 'e', ' ', 'U', 'I', 0,
    };
    WORD *copy = HeapAlloc(GetProcessHeap(), 0, sizeof(tmpl));
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, tmpl, sizeof(tmpl));
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN | DS_MODALFRAME | DS_SETFONT |
                  DS_CENTER | WS_VISIBLE;
    copy[0] = LOWORD(style);
    copy[1] = HIWORD(style);
    InitCommonControlsEx(&classes);
    dialog->Owner = owner;
    dialog->ThreadId = GetCurrentThreadId();
    dialog->Closed = FALSE;
    dialog->Result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    free_results(dialog);
    INT_PTR answer = DialogBoxIndirectParamW(g_comdlg32, (LPCDLGTEMPLATEW)copy, owner, dialog_proc, (LPARAM)dialog);
    HeapFree(GetProcessHeap(), 0, copy);
    dialog->Showing = FALSE;
    if (answer == -1)
        return HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_INVALID_PARAMETER);
    return dialog->Result;
}
