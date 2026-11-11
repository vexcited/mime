/*
 * notepad.exe: the text editor that Windows opens text files with. It is the classic one: a window around a multiline
 * edit control, with the menus, the shortcuts, the encodings and the questions of Windows 10's.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/winnls.h>
#include <mime/win32/commdlg.h>
#include <mime/win32/shellapi.h>
#include <rtl.h>

#define APP_NAME L"Notepad"

enum {
    IDM_NEW = 1, IDM_OPEN, IDM_SAVE, IDM_SAVE_AS, IDM_EXIT,
    IDM_UNDO, IDM_CUT, IDM_COPY, IDM_PASTE, IDM_DELETE, IDM_FIND, IDM_FIND_NEXT, IDM_SELECT_ALL, IDM_TIME_DATE,
    IDM_WORD_WRAP, IDM_ABOUT,
};

enum { ENCODING_ANSI, ENCODING_UTF16, ENCODING_UTF16_BE, ENCODING_UTF8, ENCODING_UTF8_BOM };

static HWND g_window, g_edit, g_find_dialog;
static HFONT g_font;
static WCHAR g_path[MAX_PATH];
static int g_encoding = ENCODING_UTF8;
static BOOL g_wrap, g_unix_lines;
static UINT g_find_message;
static FINDREPLACEW g_find;
static WCHAR g_find_text[128];

static PCWSTR file_title(void)
{
    if (!g_path[0])
        return L"Untitled";
    PCWSTR name = g_path;
    for (PCWSTR p = g_path; *p; p++)
        if (*p == '\\' || *p == '/')
            name = p + 1;
    return name;
}

static BOOL modified(void)
{
    return SendMessageW(g_edit, EM_GETMODIFY, 0, 0) != 0;
}

static void update_title(void)
{
    WCHAR title[MAX_PATH + 32];
    _snwprintf(title, RTL_NUMBER_OF(title), L"%ls%ls - " APP_NAME, modified() ? L"*" : L"", file_title());
    title[RTL_NUMBER_OF(title) - 1] = 0;
    SetWindowTextW(g_window, title);
}

static int box(PCWSTR text, UINT flags)
{
    return MessageBoxW(g_window, text, APP_NAME, flags);
}

/* ------------------------------------------------------------------ the edit control */

static void create_edit(PCWSTR text)
{
    RECT client;
    GetClientRect(g_window, &client);
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL | ES_WANTRETURN;
    if (!g_wrap)
        style |= WS_HSCROLL | ES_AUTOHSCROLL;
    g_edit = CreateWindowExW(0, L"EDIT", text, style, 0, 0, client.right, client.bottom, g_window, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, FALSE);
    SendMessageW(g_edit, EM_SETLIMITTEXT, 0, 0);
    SetFocus(g_edit);
}

static PWSTR edit_text(SIZE_T *chars)
{
    int length = GetWindowTextLengthW(g_edit);
    PWSTR text = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!text)
        return nullptr;
    *chars = (SIZE_T)GetWindowTextW(g_edit, text, length + 1);
    return text;
}

/* the control can only wrap or not from its creation on, so the text moves to a new one. */
static void toggle_wrap(void)
{
    SIZE_T chars;
    PWSTR text = edit_text(&chars);
    if (!text)
        return;
    BOOL was_modified = modified();
    g_wrap = !g_wrap;
    DestroyWindow(g_edit);
    create_edit(text);
    SendMessageW(g_edit, EM_SETMODIFY, was_modified, 0);
    HeapFree(GetProcessHeap(), 0, text);
}

/* ------------------------------------------------------------------ files */

static BOOL valid_utf8(const BYTE *data, SIZE_T size)
{
    return !size || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (PCSTR)data, (int)size, nullptr, 0) > 0;
}

/* the text of a file as the control wants it: utf-16 with every line ending in a carriage return and a line feed. */
static PWSTR decode(const BYTE *data, SIZE_T size, int *encoding, BOOL *unix_lines)
{
    PWSTR wide = nullptr;
    SIZE_T chars = 0;
    if (size >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        BOOL big = data[0] == 0xFE;
        *encoding = big ? ENCODING_UTF16_BE : ENCODING_UTF16;
        chars = (size - 2) / 2;
        wide = HeapAlloc(GetProcessHeap(), 0, (chars + 1) * sizeof(WCHAR));
        if (!wide)
            return nullptr;
        for (SIZE_T i = 0; i < chars; i++) {
            const BYTE *p = data + 2 + i * 2;
            wide[i] = big ? (WCHAR)(p[0] << 8 | p[1]) : (WCHAR)(p[1] << 8 | p[0]);
        }
    } else {
        UINT page = CP_UTF8;
        *encoding = ENCODING_UTF8;
        if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
            *encoding = ENCODING_UTF8_BOM;
            data += 3;
            size -= 3;
        } else if (!valid_utf8(data, size)) {
            *encoding = ENCODING_ANSI;
            page = CP_ACP;
        }
        chars = size ? (SIZE_T)MultiByteToWideChar(page, 0, (PCSTR)data, (int)size, nullptr, 0) : 0;
        wide = HeapAlloc(GetProcessHeap(), 0, (chars + 1) * sizeof(WCHAR));
        if (!wide)
            return nullptr;
        if (chars)
            MultiByteToWideChar(page, 0, (PCSTR)data, (int)size, wide, (int)chars);
    }
    SIZE_T bare = 0, pairs = 0;
    for (SIZE_T i = 0; i < chars; i++) {
        if (wide[i] == '\r' && i + 1 < chars && wide[i + 1] == '\n') {
            pairs++;
            i++;
        } else if (wide[i] == '\n' || wide[i] == '\r') {
            bare++;
        }
    }
    *unix_lines = bare && !pairs;
    PWSTR text = HeapAlloc(GetProcessHeap(), 0, (chars + bare + 1) * sizeof(WCHAR));
    if (text) {
        SIZE_T n = 0;
        for (SIZE_T i = 0; i < chars; i++) {
            if (wide[i] == '\r' && i + 1 < chars && wide[i + 1] == '\n') {
                text[n++] = '\r';
                text[n++] = '\n';
                i++;
            } else if (wide[i] == '\n' || wide[i] == '\r') {
                text[n++] = '\r';
                text[n++] = '\n';
            } else {
                text[n++] = wide[i] ? wide[i] : ' ';
            }
        }
        text[n] = 0;
    }
    HeapFree(GetProcessHeap(), 0, wide);
    return text;
}

static void report(PCWSTR format, PCWSTR path)
{
    WCHAR text[MAX_PATH + 160];
    _snwprintf(text, RTL_NUMBER_OF(text), format, path);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    box(text, MB_ICONEXCLAMATION | MB_OK);
}

static BOOL load(PCWSTR path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        report(GetLastError() == ERROR_ACCESS_DENIED ? L"Access is denied.\n\n%ls" : L"Cannot open the %ls file.", path);
        return FALSE;
    }
    LARGE_INTEGER size;
    BYTE *data = nullptr;
    DWORD got = 0;
    BOOL ok = GetFileSizeEx(file, &size) && size.QuadPart < 0x40000000;
    if (ok) {
        data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart + 1);
        ok = data && ReadFile(file, data, (DWORD)size.QuadPart, &got, nullptr);
    }
    CloseHandle(file);
    PWSTR text = ok ? decode(data, got, &g_encoding, &g_unix_lines) : nullptr;
    HeapFree(GetProcessHeap(), 0, data);
    if (!text) {
        report(L"The %ls file is too large for Notepad.", path);
        return FALSE;
    }
    SetWindowTextW(g_edit, text);
    HeapFree(GetProcessHeap(), 0, text);
    SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    GetFullPathNameW(path, MAX_PATH, g_path, nullptr);
    update_title();
    return TRUE;
}

static BOOL save(PCWSTR path)
{
    SIZE_T chars;
    PWSTR text = edit_text(&chars);
    if (!text)
        return FALSE;
    if (g_unix_lines) {
        SIZE_T n = 0;
        for (SIZE_T i = 0; i < chars; i++)
            if (!(text[i] == '\r' && i + 1 < chars && text[i + 1] == '\n'))
                text[n++] = text[i];
        chars = n;
    }
    BYTE *bytes = nullptr;
    SIZE_T size = 0;
    if (g_encoding == ENCODING_UTF16 || g_encoding == ENCODING_UTF16_BE) {
        size = 2 + chars * 2;
        bytes = HeapAlloc(GetProcessHeap(), 0, size);
        if (bytes) {
            BOOL big = g_encoding == ENCODING_UTF16_BE;
            bytes[0] = big ? 0xFE : 0xFF;
            bytes[1] = big ? 0xFF : 0xFE;
            for (SIZE_T i = 0; i < chars; i++) {
                bytes[2 + i * 2 + (big ? 1 : 0)] = (BYTE)text[i];
                bytes[2 + i * 2 + (big ? 0 : 1)] = (BYTE)(text[i] >> 8);
            }
        }
    } else {
        UINT page = g_encoding == ENCODING_ANSI ? CP_ACP : CP_UTF8;
        SIZE_T mark = g_encoding == ENCODING_UTF8_BOM ? 3 : 0;
        int need = chars ? WideCharToMultiByte(page, 0, text, (int)chars, nullptr, 0, nullptr, nullptr) : 0;
        size = mark + (SIZE_T)need;
        bytes = HeapAlloc(GetProcessHeap(), 0, size + 1);
        if (bytes) {
            if (mark)
                memcpy(bytes, "\xEF\xBB\xBF", 3);
            if (need)
                WideCharToMultiByte(page, 0, text, (int)chars, (PSTR)bytes + mark, need, nullptr, nullptr);
        }
    }
    HeapFree(GetProcessHeap(), 0, text);
    BOOL ok = FALSE;
    if (bytes) {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD wrote;
            ok = WriteFile(file, bytes, (DWORD)size, &wrote, nullptr) && wrote == size;
            CloseHandle(file);
        }
        HeapFree(GetProcessHeap(), 0, bytes);
    }
    if (!ok) {
        report(L"Cannot create the %ls file.\n\nMake sure that the path and file name are correct.", path);
        return FALSE;
    }
    SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    GetFullPathNameW(path, MAX_PATH, g_path, nullptr);
    update_title();
    return TRUE;
}

static BOOL ask_file(BOOL saving, WCHAR path[MAX_PATH])
{
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window;
    ofn.lpstrFilter = L"Text Documents (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return saving ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}

static BOOL save_as(void)
{
    WCHAR path[MAX_PATH];
    if (g_path[0])
        wcscpy(path, g_path);
    else
        wcscpy(path, L"*.txt");
    return ask_file(TRUE, path) && save(path);
}

static BOOL save_current(void)
{
    return g_path[0] ? save(g_path) : save_as();
}

/* false when the user keeps the text that is there. */
static BOOL may_discard(void)
{
    if (!modified())
        return TRUE;
    WCHAR text[MAX_PATH + 64];
    _snwprintf(text, RTL_NUMBER_OF(text), L"Do you want to save changes to %ls?", g_path[0] ? g_path : L"Untitled");
    text[RTL_NUMBER_OF(text) - 1] = 0;
    int answer = box(text, MB_ICONEXCLAMATION | MB_YESNOCANCEL);
    if (answer == IDYES)
        return save_current();
    return answer == IDNO;
}

static void new_file(void)
{
    SetWindowTextW(g_edit, L"");
    SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    g_path[0] = 0;
    g_encoding = ENCODING_UTF8;
    g_unix_lines = FALSE;
    update_title();
}

/* ------------------------------------------------------------------ editing */

static void insert_time_date(void)
{
    SYSTEMTIME now;
    WCHAR time[64], date[64], text[136];
    GetLocalTime(&now);
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, time, RTL_NUMBER_OF(time));
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &now, nullptr, date, RTL_NUMBER_OF(date));
    _snwprintf(text, RTL_NUMBER_OF(text), L"%ls %ls", time, date);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)text);
}

static WCHAR fold(WCHAR c, BOOL match_case)
{
    return match_case ? c : (WCHAR)(ULONG_PTR)CharLowerW((PWSTR)(ULONG_PTR)c);
}

static void find_next(void)
{
    SIZE_T chars, wanted = wcslen(g_find_text);
    if (!wanted)
        return;
    PWSTR text = edit_text(&chars);
    if (!text)
        return;
    DWORD start = 0, end = 0;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    BOOL down = (g_find.Flags & FR_DOWN) != 0, match_case = (g_find.Flags & FR_MATCHCASE) != 0;
    LONG_PTR found = -1;
    for (LONG_PTR at = down ? (LONG_PTR)end : (LONG_PTR)start - 1; at >= 0 && (SIZE_T)at + wanted <= chars;
         at += down ? 1 : -1) {
        SIZE_T k = 0;
        while (k < wanted && fold(text[at + k], match_case) == fold(g_find_text[k], match_case))
            k++;
        if (k == wanted) {
            found = at;
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, text);
    if (found < 0) {
        WCHAR message[200];
        _snwprintf(message, RTL_NUMBER_OF(message), L"Cannot find \"%ls\"", g_find_text);
        message[RTL_NUMBER_OF(message) - 1] = 0;
        MessageBoxW(g_find_dialog ? g_find_dialog : g_window, message, APP_NAME, MB_ICONINFORMATION | MB_OK);
        return;
    }
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)found, (LPARAM)(found + (LONG_PTR)wanted));
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
}

static void show_find(void)
{
    if (g_find_dialog) {
        SetFocus(g_find_dialog);
        return;
    }
    g_find.lStructSize = sizeof(g_find);
    g_find.hwndOwner = g_window;
    g_find.lpstrFindWhat = g_find_text;
    g_find.wFindWhatLen = RTL_NUMBER_OF(g_find_text);
    g_find.Flags = (g_find.Flags & FR_MATCHCASE) | FR_DOWN | FR_HIDEWHOLEWORD;
    g_find_dialog = FindTextW(&g_find);
}

static void command(UINT id)
{
    WCHAR path[MAX_PATH];
    switch (id) {
    case IDM_NEW:
        if (may_discard())
            new_file();
        break;
    case IDM_OPEN:
        path[0] = 0;
        if (may_discard() && ask_file(FALSE, path))
            load(path);
        break;
    case IDM_SAVE:
        save_current();
        break;
    case IDM_SAVE_AS:
        save_as();
        break;
    case IDM_EXIT:
        SendMessageW(g_window, WM_CLOSE, 0, 0);
        break;
    case IDM_UNDO:
        SendMessageW(g_edit, EM_UNDO, 0, 0);
        break;
    case IDM_CUT:
        SendMessageW(g_edit, WM_CUT, 0, 0);
        break;
    case IDM_COPY:
        SendMessageW(g_edit, WM_COPY, 0, 0);
        break;
    case IDM_PASTE:
        SendMessageW(g_edit, WM_PASTE, 0, 0);
        break;
    case IDM_DELETE:
        SendMessageW(g_edit, WM_CLEAR, 0, 0);
        break;
    case IDM_FIND:
        show_find();
        break;
    case IDM_FIND_NEXT:
        if (g_find_text[0])
            find_next();
        else
            show_find();
        break;
    case IDM_SELECT_ALL:
        SendMessageW(g_edit, EM_SETSEL, 0, -1);
        break;
    case IDM_TIME_DATE:
        insert_time_date();
        break;
    case IDM_WORD_WRAP:
        toggle_wrap();
        break;
    case IDM_ABOUT:
        ShellAboutW(g_window, APP_NAME, nullptr, nullptr);
        break;
    }
}

/* ------------------------------------------------------------------ the window */

static HMENU build_menu(void)
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(), format = CreatePopupMenu(),
          help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_NEW, L"&New\tCtrl+N");
    AppendMenuW(file, MF_STRING, IDM_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDM_SAVE_AS, L"Save &As...\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit");
    AppendMenuW(edit, MF_STRING, IDM_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_CUT, L"Cu&t\tCtrl+X");
    AppendMenuW(edit, MF_STRING, IDM_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(edit, MF_STRING, IDM_PASTE, L"&Paste\tCtrl+V");
    AppendMenuW(edit, MF_STRING, IDM_DELETE, L"De&lete\tDel");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_FIND, L"&Find...\tCtrl+F");
    AppendMenuW(edit, MF_STRING, IDM_FIND_NEXT, L"Find &Next\tF3");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_SELECT_ALL, L"Select &All\tCtrl+A");
    AppendMenuW(edit, MF_STRING, IDM_TIME_DATE, L"Time/&Date\tF5");
    AppendMenuW(format, MF_STRING, IDM_WORD_WRAP, L"&Word Wrap");
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&About Notepad");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)format, L"F&ormat");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

static void update_menu(HMENU menu)
{
    DWORD start = 0, end = 0;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    UINT selection = start != end ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(menu, IDM_UNDO, SendMessageW(g_edit, EM_CANUNDO, 0, 0) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, IDM_CUT, selection);
    EnableMenuItem(menu, IDM_COPY, selection);
    EnableMenuItem(menu, IDM_DELETE, selection);
    EnableMenuItem(menu, IDM_PASTE, IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_ENABLED : MF_GRAYED);
    BOOL any = GetWindowTextLengthW(g_edit) != 0;
    EnableMenuItem(menu, IDM_FIND, any ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, IDM_FIND_NEXT, any ? MF_ENABLED : MF_GRAYED);
    CheckMenuItem(menu, IDM_WORD_WRAP, g_wrap ? MF_CHECKED : MF_UNCHECKED);
}

static void create_font(void)
{
    LOGFONTW font = {};
    font.lfHeight = -MulDiv(11, (int)GetDpiForWindow(g_window), 72);
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcscpy(font.lfFaceName, L"Consolas");
    if (g_font)
        DeleteObject(g_font);
    g_font = CreateFontIndirectW(&font);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (g_find_message && message == g_find_message) {
        if (g_find.Flags & FR_DIALOGTERM)
            g_find_dialog = nullptr;
        else if (g_find.Flags & FR_FINDNEXT)
            find_next();
        return 0;
    }
    switch (message) {
    case WM_CREATE:
        g_window = window;
        create_font();
        create_edit(L"");
        DragAcceptFiles(window, TRUE);
        return 0;
    case WM_SIZE:
        MoveWindow(g_edit, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_edit);
        return 0;
    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)lparam;
        create_font();
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, TRUE);
        SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_INITMENUPOPUP:
        update_menu((HMENU)wparam);
        return 0;
    case WM_COMMAND:
        if ((HWND)lparam == g_edit) {
            if (HIWORD(wparam) == EN_CHANGE)
                update_title();
            else if (HIWORD(wparam) == EN_ERRSPACE || HIWORD(wparam) == EN_MAXTEXT)
                box(L"Not enough memory available to complete this operation.", MB_ICONEXCLAMATION | MB_OK);
            return 0;
        }
        command(LOWORD(wparam));
        return 0;
    case WM_DROPFILES: {
        WCHAR path[MAX_PATH];
        if (DragQueryFileW((HDROP)wparam, 0, path, MAX_PATH) && may_discard())
            load(path);
        DragFinish((HDROP)wparam);
        return 0;
    }
    case WM_QUERYENDSESSION:
        return may_discard();
    case WM_CLOSE:
        if (may_discard())
            DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

/* like windows, a name without an extension is tried with .txt, and a file that is not there may be created. */
static void open_argument(PCWSTR name)
{
    WCHAR path[MAX_PATH];
    if (wcslen(name) + 5 > MAX_PATH)
        return;
    wcscpy(path, name);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        PCWSTR dot = nullptr;
        for (PCWSTR p = path; *p; p++) {
            if (*p == '.')
                dot = p;
            else if (*p == '\\' || *p == '/')
                dot = nullptr;
        }
        if (!dot)
            wcscat(path, L".txt");
    }
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        load(path);
        return;
    }
    WCHAR text[MAX_PATH + 96];
    _snwprintf(text, RTL_NUMBER_OF(text), L"Cannot find the %ls file.\n\nDo you want to create a new file?", path);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    int answer = box(text, MB_ICONEXCLAMATION | MB_YESNOCANCEL);
    if (answer == IDCANCEL) {
        DestroyWindow(g_window);
        return;
    }
    if (answer == IDYES) {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            load(path);
        }
    }
}

/* the file is what follows the program's own name and the switches, taken whole so that its spaces need no quotes. */
static PCWSTR argument(PWSTR line, BOOL *print)
{
    PWSTR p = line;
    if (*p == '"') {
        for (p++; *p && *p != '"'; p++) {
        }
        if (*p)
            p++;
    } else {
        while (*p && *p != ' ' && *p != '\t')
            p++;
    }
    for (;;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p != '/')
            break;
        if ((p[1] == 'p' || p[1] == 'P'))
            *print = TRUE;
        while (*p && *p != ' ' && *p != '\t')
            p++;
    }
    SIZE_T length = wcslen(p);
    while (length && (p[length - 1] == ' ' || p[length - 1] == '\t'))
        p[--length] = 0;
    if (length >= 2 && p[0] == '"') {
        p++;
        for (PWSTR q = p; *q; q++) {
            if (*q == '"') {
                *q = 0;
                break;
            }
        }
    }
    return p;
}

void entry(void)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    PCWSTR command_line = GetCommandLineW();
    SIZE_T size = (wcslen(command_line) + 1) * sizeof(WCHAR);
    PWSTR line = HeapAlloc(GetProcessHeap(), 0, size);
    if (!line)
        ExitProcess(1);
    memcpy(line, command_line, size);
    BOOL print = FALSE;
    PCWSTR name = argument(line, &print);
    /* there is no printer to print on. */
    if (print)
        ExitProcess(0);

    WNDCLASSEXW class = {};
    class.cbSize = sizeof(class);
    class.lpfnWndProc = window_proc;
    class.hInstance = instance;
    class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    class.lpszClassName = APP_NAME;
    RegisterClassExW(&class);
    g_find_message = RegisterWindowMessageW(FINDMSGSTRINGW);
    HWND window = CreateWindowExW(0, APP_NAME, L"Untitled - " APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, build_menu(),
                                  instance, nullptr);
    if (!window)
        ExitProcess(1);
    STARTUPINFOW startup;
    GetStartupInfoW(&startup);
    ShowWindow(window, startup.dwFlags & STARTF_USESHOWWINDOW ? startup.wShowWindow : SW_SHOWNORMAL);
    UpdateWindow(window);
    if (name[0])
        open_argument(name);

    ACCEL keys[] = {
        {FVIRTKEY | FCONTROL, 'N', IDM_NEW},           {FVIRTKEY | FCONTROL, 'O', IDM_OPEN},
        {FVIRTKEY | FCONTROL, 'S', IDM_SAVE},          {FVIRTKEY | FCONTROL | FSHIFT, 'S', IDM_SAVE_AS},
        {FVIRTKEY | FCONTROL, 'F', IDM_FIND},          {FVIRTKEY, VK_F3, IDM_FIND_NEXT},
        {FVIRTKEY | FCONTROL, 'A', IDM_SELECT_ALL},    {FVIRTKEY, VK_F5, IDM_TIME_DATE},
    };
    HACCEL accelerators = CreateAcceleratorTableW(keys, RTL_NUMBER_OF(keys));
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (g_find_dialog && IsDialogMessageW(g_find_dialog, &message))
            continue;
        if (TranslateAcceleratorW(window, accelerators, &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    ExitProcess((UINT)message.wParam);
}
