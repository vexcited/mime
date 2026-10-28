/* the file list of a dialog: filling a list box or a combo box with a directory, and reading the choice back. */
#include "user32_private.h"

#ifndef ERROR_CONTROL_ID_NOT_FOUND
#define ERROR_CONTROL_ID_NOT_FOUND 1421L
#endif

#ifndef LB_GETCURSEL
#define LB_GETCURSEL 0x0188
#define LB_GETTEXT 0x0189
#endif

enum { DIR_LISTBOX, DIR_COMBO };

/* the control must be of the kind asked, which is told by the class name. */
static HWND find_control(HWND dialog, int id, int kind)
{
    HWND control = dialog ? GetDlgItem(dialog, id) : nullptr;
    if (!control)
        return nullptr;
    WCHAR name[16];
    if (!GetClassNameW(control, name, ARRAYSIZE(name)))
        return nullptr;
    return !lstrcmpiW(name, kind == DIR_LISTBOX ? L"ListBox" : L"ComboBox") ? control : nullptr;
}

static BOOL has_wildcard(const WCHAR *text)
{
    return wcschr(text, L'*') || wcschr(text, L'?');
}

/*
 * splits the path specification in the directory, which becomes the current one, and the pattern, which stays in the
 * buffer. a specification that names a directory only leaves an empty pattern, and a pattern needs a wildcard.
 */
static BOOL change_directory(WCHAR *spec)
{
    CharUpperW(spec);
    if (!*spec)
        return TRUE;
    if (!has_wildcard(spec) && SetCurrentDirectoryW(spec)) {
        *spec = 0;
        return TRUE;
    }
    WCHAR *cut = spec + wcslen(spec);
    while (cut > spec && cut[-1] != L'\\' && cut[-1] != L'/' && cut[-1] != L':')
        cut--;
    if (*cut && !has_wildcard(cut)) {
        SetLastError(1417);
        return FALSE;
    }
    if (cut > spec) {
        WCHAR kept = *cut;
        *cut = 0;
        BOOL ok = SetCurrentDirectoryW(spec);
        *cut = kept;
        if (!ok)
            return FALSE;
        memmove(spec, cut, (wcslen(cut) + 1) * sizeof(WCHAR));
    }
    return TRUE;
}

static int dir_list(HWND dialog, WCHAR *spec, int control_id, int label_id, UINT type, int kind)
{
    if (type & 0xFFFF1FC8) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (!dialog && (control_id || label_id)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BOOL post = (type & DDL_POSTMSGS) != 0;
    type &= ~0x1000u;
    HWND control = control_id ? find_control(dialog, control_id, kind) : nullptr;
    if (control_id && !control)
        SetLastError(ERROR_CONTROL_ID_NOT_FOUND);
    WCHAR pattern[MAX_PATH + 2];
    lstrcpyW(pattern, L"*");
    if (spec) {
        if (!*spec) {
            lstrcpyW(pattern, L"\\*");
        } else {
            if (lstrlenW(spec) > 260)
                return 0;
            if (!change_directory(spec))
                return 0;
            lstrcpynW(pattern, *spec ? spec : L"*", MAX_PATH);
        }
    }
    if (label_id) {
        WCHAR current[MAX_PATH];
        if (GetCurrentDirectoryW(MAX_PATH, current)) {
            CharLowerW(current);
            SetDlgItemTextW(dialog, label_id, current);
        }
    }
    if (!control)
        return 1;
    UINT reset = kind == DIR_LISTBOX ? LB_RESETCONTENT : CB_RESETCONTENT;
    UINT list = kind == DIR_LISTBOX ? LB_DIR : CB_DIR;
    BOOL redraw = FALSE;
    if (!post) {
        redraw = IsWindowVisible(control) != 0;
        if (redraw)
            SendMessageW(control, WM_SETREDRAW, FALSE, 0);
        SendMessageW(control, reset, 0, 0);
    } else {
        PostMessageW(control, reset, 0, 0);
    }
    UINT flags = type;
    if (flags == DDL_DRIVES)
        flags = DDL_DRIVES | DDL_EXCLUSIVE;
    /* the files that match come first, then the directories and drives. */
    UINT first = flags;
    if ((flags & DDL_EXCLUSIVE) && (flags & (DDL_DRIVES | DDL_DIRECTORY)))
        first = flags | 0x1000;
    if (!(first & 0x1000)) {
        UINT files = flags & ~(UINT)(DDL_DRIVES | DDL_DIRECTORY);
        if (post)
            PostMessageW(control, list, files, (LPARAM)pattern);
        else
            SendMessageW(control, list, files, (LPARAM)pattern);
    }
    UINT rest = (flags & ((flags & 0x27) | DDL_DRIVES | DDL_POSTMSGS | DDL_DIRECTORY)) | 0x1000;
    if (rest & (DDL_DRIVES | DDL_DIRECTORY)) {
        if (post)
            PostMessageW(control, list, rest | DDL_EXCLUSIVE, (LPARAM)L"*");
        else
            SendMessageW(control, list, rest | DDL_EXCLUSIVE, (LPARAM)L"*");
    }
    if (redraw) {
        SendMessageW(control, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(control, nullptr, TRUE);
    }
    return 1;
}

/* the text of the chosen entry: a directory in brackets is the name with a backslash, a drive is its letter and a colon. */
static BOOL dir_select(WCHAR *out, int size, HWND control, int kind)
{
    if (!control) {
        SetLastError(ERROR_CONTROL_ID_NOT_FOUND);
        return FALSE;
    }
    UINT current = kind == DIR_LISTBOX ? LB_GETCURSEL : CB_GETCURSEL;
    UINT length = kind == DIR_LISTBOX ? LB_GETTEXTLEN : CB_GETLBTEXTLEN;
    UINT text = kind == DIR_LISTBOX ? LB_GETTEXT : CB_GETLBTEXT;
    LRESULT index = SendMessageW(control, current, 0, 0);
    if (index < 0)
        return FALSE;
    if ((ULONG_PTR)SendMessageW(control, length, (WPARAM)index, 0) >= MAX_PATH + 2)
        return FALSE;
    WCHAR entry[MAX_PATH + 8] = {};
    SendMessageW(control, text, (WPARAM)index, (LPARAM)entry);
    WCHAR *tab = wcschr(entry, L'\t');
    if (tab)
        *tab = 0;
    BOOL directory = entry[0] == L'[';
    WCHAR *result = entry;
    int n = lstrlenW(entry);
    if (directory) {
        result = entry + 1;
        if (n > 1)
            entry[n - 1] = L'\\';
        if (result[0] == L'-' && n > 3) {
            result = entry + 2;
            result[1] = L':';
            result[2] = 0;
        }
    } else if (!wcschr(entry, L'.')) {
        entry[n] = L'.';
        entry[n + 1] = 0;
    }
    if (out && size > 0)
        lstrcpynW(out, result, size);
    return directory;
}

WINUSERAPI int WINAPI DlgDirListW(HWND dialog, LPWSTR spec, int list, int label, UINT type)
{
    return dir_list(dialog, spec, list, label, type, DIR_LISTBOX);
}

WINUSERAPI int WINAPI DlgDirListComboBoxW(HWND dialog, LPWSTR spec, int combo, int label, UINT type)
{
    return dir_list(dialog, spec, combo, label, type, DIR_COMBO);
}

WINUSERAPI BOOL WINAPI DlgDirSelectExW(HWND dialog, LPWSTR path, int count, int list)
{
    return dir_select(path, count, dialog ? GetDlgItem(dialog, list) : nullptr, DIR_LISTBOX);
}

WINUSERAPI BOOL WINAPI DlgDirSelectComboBoxExW(HWND dialog, LPWSTR path, int count, int combo)
{
    return dir_select(path, count, dialog ? GetDlgItem(dialog, combo) : nullptr, DIR_COMBO);
}

static int dir_list_ansi(HWND dialog, LPSTR spec, int control_id, int label_id, UINT type, int kind)
{
    if (!IsWindow(dialog))
        return 0;
    LPWSTR wide = nullptr;
    if (spec) {
        wide = UserAtoW(spec);
        if (!wide)
            return 0;
    }
    int result = dir_list(dialog, wide, control_id, label_id, type, kind);
    if (wide) {
        if (result)
            WideCharToMultiByte(CP_ACP, 0, wide, -1, spec, 0x7FFFFFFF, nullptr, nullptr);
        UserFree(wide);
    }
    return result;
}

static BOOL dir_select_ansi(HWND dialog, LPSTR path, int count, int id, int kind)
{
    if (count <= 0)
        return FALSE;
    LPWSTR wide = UserAlloc((SIZE_T)count * sizeof(WCHAR));
    if (!wide)
        return FALSE;
    BOOL result = dir_select(wide, count, dialog ? GetDlgItem(dialog, id) : nullptr, kind);
    WideCharToMultiByte(CP_ACP, 0, wide, -1, path, count, nullptr, nullptr);
    UserFree(wide);
    return result;
}

WINUSERAPI int WINAPI DlgDirListA(HWND dialog, LPSTR spec, int list, int label, UINT type)
{
    return dir_list_ansi(dialog, spec, list, label, type, DIR_LISTBOX);
}

WINUSERAPI int WINAPI DlgDirListComboBoxA(HWND dialog, LPSTR spec, int combo, int label, UINT type)
{
    return dir_list_ansi(dialog, spec, combo, label, type, DIR_COMBO);
}

WINUSERAPI BOOL WINAPI DlgDirSelectExA(HWND dialog, LPSTR path, int count, int list)
{
    return dir_select_ansi(dialog, path, count, list, DIR_LISTBOX);
}

WINUSERAPI BOOL WINAPI DlgDirSelectComboBoxExA(HWND dialog, LPSTR path, int count, int combo)
{
    return dir_select_ansi(dialog, path, count, combo, DIR_COMBO);
}
