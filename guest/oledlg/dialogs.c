/*
 * oledlg.dll: the OLE user interface dialogs. the server busy, insert object, paste special, change source and links
 * dialogs are built here in memory and run as dialogs; the others check their structures as the library does and
 * report that the dialog could not be made. exports are listed in oledlg.def.
 */
#define OLEDLGAPI
#include <mime/win32/oledlg.h>
#include <mime/win32/oleidl.h>
#include <mime/win32/ole2.h>
#include <mime/win32/winuser.h>
#include <mime/win32/commdlg.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define OLEUI_BZ_FLAGS_DISABLECANCEL 0x1
#define OLEUI_BZ_FLAGS_DISABLESWITCHTO 0x2
#define OLEUI_BZ_FLAGS_DISABLERETRY 0x4
#define OLEUI_BZ_FLAGS_NOTRESPONDING 0x8
#define OLEUIPASTE_PASTE 0x200
#define OLEUIPASTE_LINKANYTYPE 0x400

#ifndef DWLP_USER
#define DWLP_USER 16
#endif

/* ------------------------------------------------------------------ in-memory templates */

typedef struct {
    WORD atom;
    LPCWSTR text;
    WORD id;
    DWORD style;
    short x, y, w, h;
} CONTROL;

#define CTL_BUTTON 0x0080
#define CTL_EDIT 0x0081
#define CTL_STATIC 0x0082
#define CTL_LISTBOX 0x0083

static WORD *put_text(WORD *out, LPCWSTR text)
{
    while ((*out++ = *text++))
        ;
    return out;
}

/* a captioned dialog in the shell font, free it with LocalFree. */
static LPDLGTEMPLATEW build_template(LPCWSTR title, int width, int height, const CONTROL *controls, UINT count)
{
    SIZE_T room = 64 + (lstrlenW(title) + 16) * sizeof(WCHAR);
    for (UINT i = 0; i < count; i++)
        room += 32 + (lstrlenW(controls[i].text) + 2) * sizeof(WCHAR);
    WORD *base = LocalAlloc(LPTR, room), *out = base;
    if (!base)
        return nullptr;
    *(DWORD *)out = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_VISIBLE;
    out += 2;
    out += 2;
    *out++ = (WORD)count;
    out += 2;
    *out++ = (WORD)width;
    *out++ = (WORD)height;
    out += 2;
    out = put_text(out, title);
    *out++ = 8;
    out = put_text(out, L"MS Shell Dlg");
    for (UINT i = 0; i < count; i++) {
        out = (WORD *)(((ULONG_PTR)out - (ULONG_PTR)base + 3) / 4 * 4 + (ULONG_PTR)base);
        *(DWORD *)out = controls[i].style | WS_CHILD | WS_VISIBLE;
        out += 2;
        out += 2;
        *out++ = controls[i].x;
        *out++ = controls[i].y;
        *out++ = controls[i].w;
        *out++ = controls[i].h;
        *out++ = controls[i].id;
        *out++ = 0xffff;
        *out++ = controls[i].atom;
        out = put_text(out, controls[i].text);
        *out++ = 0;
    }
    return (LPDLGTEMPLATEW)base;
}

/* ------------------------------------------------------------------ the standard structure */

typedef struct {
    OLEUI_STANDARD_FIELDS;
} STANDARD;

/* the checks every dialog begins with, and the error of the library for the first that fails. */
static UINT validate(const void *structure, DWORD size)
{
    const STANDARD *s = structure;
    if (!s)
        return OLEUI_ERR_STRUCTURENULL;
    if (IsBadWritePtr((void *)s, size))
        return OLEUI_ERR_STRUCTUREINVALID;
    if (s->cbStruct != size)
        return OLEUI_ERR_CBSTRUCTINCORRECT;
    if (s->hWndOwner && !IsWindow(s->hWndOwner))
        return OLEUI_ERR_HWNDOWNERINVALID;
    if (s->lpszCaption && IsBadReadPtr(s->lpszCaption, 1))
        return OLEUI_ERR_LPSZCAPTIONINVALID;
    if (s->lpfnHook && IsBadCodePtr((FARPROC)(void *)s->lpfnHook))
        return OLEUI_ERR_LPFNHOOKINVALID;
    if (s->hInstance) {
        if (!s->lpszTemplate || (HIWORD((ULONG_PTR)s->lpszTemplate) && IsBadReadPtr(s->lpszTemplate, 1)))
            return OLEUI_ERR_LPSZTEMPLATEINVALID;
        HRSRC found = FindResourceW(s->hInstance, s->lpszTemplate, MAKEINTRESOURCEW(5));
        if (!found)
            return OLEUI_ERR_FINDTEMPLATEFAILURE;
        if (!LoadResource(s->hInstance, found))
            return OLEUI_ERR_LOADTEMPLATEFAILURE;
    }
    if (s->hResource && !LockResource(s->hResource))
        return OLEUI_ERR_HRESOURCEINVALID;
    return OLEUI_SUCCESS;
}

/* the hook of a structure sees the message first, and after the dialog is set up. */
typedef struct {
    STANDARD *standard;
    void *data;
    UINT result;
} STATE;

/* the hook is told that the dialog is set up, with the structure of the caller. */
static void init_hook(HWND dialog, STATE *state, WPARAM wparam)
{
    if (state->standard->lpfnHook)
        state->standard->lpfnHook(dialog, WM_INITDIALOG, wparam, (LPARAM)state->standard);
}

static INT_PTR hooked(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    STATE *state = (STATE *)GetWindowLongPtrW(dialog, DWLP_USER);
    if (state && state->standard->lpfnHook)
        return state->standard->lpfnHook(dialog, message, wparam, lparam) != 0;
    return FALSE;
}

static UINT run_dialog(const STANDARD *standard, LPCWSTR default_title, const CONTROL *controls, UINT count, int width,
                       int height, DLGPROC proc, void *data)
{
    STATE state = {(STANDARD *)standard, data, OLEUI_CANCEL};
    LPDLGTEMPLATEW template = build_template(standard->lpszCaption ? standard->lpszCaption : default_title, width,
                                             height, controls, count);
    if (!template)
        return OLEUI_ERR_LOCALMEMALLOC;
    INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), template, standard->hWndOwner, proc,
                                             (LPARAM)&state);
    LocalFree(template);
    return result == -1 ? OLEUI_ERR_DIALOGFAILURE : state.result;
}

/* the file the user picks, through the common dialog that is loaded when it is needed. */
static BOOL browse(HWND dialog, WCHAR *path)
{
    OPENFILENAMEW open = {sizeof(open)};
    HMODULE module = LoadLibraryW(L"comdlg32.dll");
    BOOL(WINAPI * pick)(OPENFILENAMEW *) = module ? (BOOL(WINAPI *)(OPENFILENAMEW *))GetProcAddress(module, "GetOpenFileNameW") : nullptr;
    open.hwndOwner = dialog;
    open.lpstrFilter = L"All Files (*.*)\0*.*\0";
    open.lpstrFile = path;
    open.nMaxFile = MAX_PATH;
    open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    BOOL picked = pick && pick(&open);
    if (module)
        FreeLibrary(module);
    return picked;
}

/* the dialogs that are not made. */
static UINT not_made(void)
{
    return OLEUI_ERR_DIALOGFAILURE;
}

static LPWSTR task_copy(LPCWSTR text)
{
    SIZE_T bytes = (lstrlenW(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = CoTaskMemAlloc(bytes);
    if (copy)
        CopyMemory(copy, text, bytes);
    return copy;
}

static void substitute(const WCHAR *format, const WCHAR *first, const WCHAR *second, WCHAR *out, int size)
{
    int length = 0;
    for (; *format && length < size - 1; format++) {
        if (format[0] == L'%' && (format[1] == L'1' || format[1] == L's' || format[1] == L'2')) {
            const WCHAR *text = format[1] == L'2' ? second : first;
            for (; text && *text && length < size - 1; text++)
                out[length++] = *text;
            format++;
        } else {
            out[length++] = *format;
        }
    }
    out[length] = 0;
}

/* ------------------------------------------------------------------ the server busy dialog */

#define ID_BUSY_TEXT 100
#define ID_BUSY_SWITCH 101
#define ID_BUSY_RETRY 102

typedef struct {
    DWORD thread;
    WCHAR title[128];
} TASK_WINDOW;

static BOOL CALLBACK find_task_window(HWND window, LPARAM param)
{
    TASK_WINDOW *task = (TASK_WINDOW *)param;
    if (GetWindowThreadProcessId(window, nullptr) == task->thread && IsWindowVisible(window) &&
        !GetWindow(window, GW_OWNER) && GetWindowTextW(window, task->title, 120))
        return FALSE;
    return TRUE;
}

static INT_PTR CALLBACK busy_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG) {
        STATE *state = (STATE *)lparam;
        OLEUIBUSYW *busy = (OLEUIBUSYW *)state->standard;
        TASK_WINDOW task = {(DWORD)(ULONG_PTR)busy->hTask, L""};
        WCHAR *name = task.title, text[512];
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)state);
        EnumWindows(find_task_window, (LPARAM)&task);
        if (!name[0])
            lstrcpyW(name, L"Unknown Application");
        substitute((busy->dwFlags & OLEUI_BZ_FLAGS_NOTRESPONDING)
                       ? L"This action cannot be completed because the \"%1\" program is not responding.  Choose "
                         L"\"Switch To\" and correct the problem."
                       : L"This action cannot be completed because the \"%1\" program is busy. Choose \"Switch To\" and "
                         L"correct the problem.",
                   name, nullptr, text, 512);
        SetDlgItemTextW(dialog, ID_BUSY_TEXT, text);
        if (busy->dwFlags & OLEUI_BZ_FLAGS_DISABLESWITCHTO)
            EnableWindow(GetDlgItem(dialog, ID_BUSY_SWITCH), FALSE);
        if (busy->dwFlags & OLEUI_BZ_FLAGS_DISABLERETRY)
            EnableWindow(GetDlgItem(dialog, ID_BUSY_RETRY), FALSE);
        if (busy->dwFlags & OLEUI_BZ_FLAGS_DISABLECANCEL)
            EnableWindow(GetDlgItem(dialog, IDCANCEL), FALSE);
        if (busy->lphWndDialog)
            *busy->lphWndDialog = dialog;
        SetFocus(GetDlgItem(dialog, ID_BUSY_SWITCH));
        init_hook(dialog, state, wparam);
        return FALSE;
    }
    if (hooked(dialog, message, wparam, lparam))
        return TRUE;
    if (message == WM_COMMAND) {
        STATE *state = (STATE *)GetWindowLongPtrW(dialog, DWLP_USER);
        switch (LOWORD(wparam)) {
        case ID_BUSY_SWITCH:
            state->result = OLEUI_BZ_SWITCHTOSELECTED;
            break;
        case ID_BUSY_RETRY:
            state->result = OLEUI_BZ_RETRYSELECTED;
            break;
        case IDCANCEL:
            if (!IsWindowEnabled(GetDlgItem(dialog, IDCANCEL)))
                return TRUE;
            state->result = OLEUI_CANCEL;
            break;
        default:
            return FALSE;
        }
        EndDialog(dialog, 1);
        return TRUE;
    }
    return FALSE;
}

OLEDLGAPI UINT WINAPI OleUIBusyW(LPOLEUIBUSYW busy)
{
    static const CONTROL controls[] = {
        {CTL_STATIC, L"", ID_BUSY_TEXT, SS_LEFT, 8, 8, 200, 40},
        {CTL_BUTTON, L"&Switch To...", ID_BUSY_SWITCH, BS_DEFPUSHBUTTON | WS_TABSTOP, 8, 56, 60, 14},
        {CTL_BUTTON, L"&Retry", ID_BUSY_RETRY, BS_PUSHBUTTON | WS_TABSTOP, 74, 56, 60, 14},
        {CTL_BUTTON, L"Cancel", IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, 140, 56, 60, 14},
    };
    UINT result = validate(busy, sizeof(OLEUIBUSYW));
    if (result != OLEUI_SUCCESS)
        return result;
    if (!busy->hTask)
        return OLEUI_BZERR_HTASKINVALID;
    return run_dialog((STANDARD *)busy, (busy->dwFlags & OLEUI_BZ_FLAGS_NOTRESPONDING) ? L"Server Not Responding"
                                                                                         : L"Server Busy",
                      controls, 4, 216, 80, busy_proc, nullptr);
}

/* ------------------------------------------------------------------ the change source dialog */

#define ID_CS_FILE 100
#define ID_CS_ITEM 101
#define ID_CS_BROWSE 102
#define ID_CS_FROM 103

typedef struct {
    WCHAR original[1100];
    WCHAR file[MAX_PATH], item[MAX_PATH];
} SOURCE_STATE;

static INT_PTR CALLBACK change_source_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG) {
        STATE *state = (STATE *)lparam;
        SOURCE_STATE *source = state->data;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)state);
        SetDlgItemTextW(dialog, ID_CS_FROM, source->original);
        SetDlgItemTextW(dialog, ID_CS_FILE, source->file);
        SetDlgItemTextW(dialog, ID_CS_ITEM, source->item);
        init_hook(dialog, state, wparam);
        return TRUE;
    }
    if (hooked(dialog, message, wparam, lparam))
        return TRUE;
    if (message != WM_COMMAND)
        return FALSE;
    STATE *state = (STATE *)GetWindowLongPtrW(dialog, DWLP_USER);
    OLEUICHANGESOURCEW *change = (OLEUICHANGESOURCEW *)state->standard;
    SOURCE_STATE *source = state->data;
    switch (LOWORD(wparam)) {
    case ID_CS_BROWSE: {
        WCHAR path[MAX_PATH];
        GetDlgItemTextW(dialog, ID_CS_FILE, path, MAX_PATH);
        if (browse(dialog, path))
            SetDlgItemTextW(dialog, ID_CS_FILE, path);
        return TRUE;
    }
    case IDOK: {
        WCHAR file[MAX_PATH], item[MAX_PATH], full[1100];
        ULONG eaten = 0;
        GetDlgItemTextW(dialog, ID_CS_FILE, file, MAX_PATH);
        GetDlgItemTextW(dialog, ID_CS_ITEM, item, MAX_PATH);
        lstrcpyW(full, file);
        lstrcatW(full, item);
        HRESULT hr = change->lpOleUILinkContainer->lpVtbl->SetLinkSource(change->lpOleUILinkContainer, change->dwLink,
                                                                        full, lstrlenW(file), &eaten, TRUE);
        if (FAILED(hr)) {
            if (MessageBoxW(dialog, L"Invalid Source : Do you want to correct it?", L"Change Source",
                            MB_YESNO | MB_ICONEXCLAMATION) == IDYES)
                return TRUE;
            hr = change->lpOleUILinkContainer->lpVtbl->SetLinkSource(change->lpOleUILinkContainer, change->dwLink, full,
                                                                    lstrlenW(file), &eaten, FALSE);
        }
        if (SUCCEEDED(hr)) {
            change->lpszFrom = task_copy(source->original);
            change->lpszTo = task_copy(full);
            change->lpszDisplayName = task_copy(full);
            change->nFileLength = lstrlenW(file);
            state->result = OLEUI_OK;
            EndDialog(dialog, 1);
        }
        return TRUE;
    }
    case IDCANCEL:
        state->result = OLEUI_CANCEL;
        EndDialog(dialog, 1);
        return TRUE;
    }
    return FALSE;
}

OLEDLGAPI UINT WINAPI OleUIChangeSourceW(LPOLEUICHANGESOURCEW change)
{
    static const CONTROL controls[] = {
        {CTL_STATIC, L"Current source:", 0xffff, SS_LEFT, 8, 8, 60, 10},
        {CTL_STATIC, L"", ID_CS_FROM, SS_LEFT | SS_PATHELLIPSIS, 72, 8, 200, 10},
        {CTL_STATIC, L"&File:", 0xffff, SS_LEFT, 8, 26, 60, 10},
        {CTL_EDIT, L"", ID_CS_FILE, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 72, 24, 150, 12},
        {CTL_BUTTON, L"&Browse...", ID_CS_BROWSE, BS_PUSHBUTTON | WS_TABSTOP, 228, 23, 44, 14},
        {CTL_STATIC, L"&Item:", 0xffff, SS_LEFT, 8, 44, 60, 10},
        {CTL_EDIT, L"", ID_CS_ITEM, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 72, 42, 150, 12},
        {CTL_BUTTON, L"OK", IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, 150, 66, 60, 14},
        {CTL_BUTTON, L"Cancel", IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, 216, 66, 56, 14},
    };
    UINT result = validate(change, sizeof(OLEUICHANGESOURCEW));
    if (result != OLEUI_SUCCESS)
        return result;
    if (!change->lpOleUILinkContainer)
        return OLEUI_CSERR_LINKCNTRNULL;
    if (change->lpszFrom)
        return OLEUI_CSERR_FROMNOTNULL;
    if (change->lpszTo)
        return OLEUI_CSERR_TONOTNULL;
    if (!change->lpszDisplayName) {
        if (FAILED(change->lpOleUILinkContainer->lpVtbl->GetLinkSource(change->lpOleUILinkContainer, change->dwLink,
                                                                       &change->lpszDisplayName, &change->nFileLength,
                                                                       nullptr, nullptr, nullptr, nullptr)))
            return OLEUI_CSERR_SOURCEINVALID;
    }
    SOURCE_STATE source = {};
    UINT length = lstrlenW(change->lpszDisplayName);
    if (length >= 1099)
        return OLEUI_CSERR_SOURCEINVALID;
    lstrcpyW(source.original, change->lpszDisplayName);
    if (change->nFileLength > length)
        change->nFileLength = length;
    lstrcpynW(source.file, change->lpszDisplayName, (change->nFileLength + 1 < MAX_PATH ? change->nFileLength + 1 : MAX_PATH));
    lstrcpynW(source.item, change->lpszDisplayName + change->nFileLength, MAX_PATH);
    return run_dialog((STANDARD *)change, L"Change Source", controls, 9, 280, 88, change_source_proc, &source);
}

/* ------------------------------------------------------------------ the paste special dialog */

#define ID_PS_LIST 100
#define ID_PS_PASTE 101
#define ID_PS_LINK 102
#define ID_PS_RESULT 103

static BOOL entry_available(OLEUIPASTESPECIALW *paste, int index, BOOL link)
{
    OLEUIPASTEENTRYW *entry = &paste->arrPasteEntries[index];
    DWORD link_bits = entry->dwFlags & (OLEUIPASTE_LINKANYTYPE | 0xff);
    if (link ? !link_bits : (entry->dwFlags & 0xff) && !(entry->dwFlags & OLEUIPASTE_PASTE))
        return FALSE;
    return paste->lpSrcDataObj->lpVtbl->QueryGetData(paste->lpSrcDataObj, &entry->fmtetc) == S_OK;
}

static void fill_paste_list(HWND dialog, OLEUIPASTESPECIALW *paste, BOOL link)
{
    HWND list = GetDlgItem(dialog, ID_PS_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < paste->cPasteEntries; i++) {
        if (!entry_available(paste, i, link))
            continue;
        WCHAR name[128];
        substitute(paste->arrPasteEntries[i].lpstrFormatName ? paste->arrPasteEntries[i].lpstrFormatName : L"", L"Unknown Source",
                   nullptr, name, 128);
        int at = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)name);
        SendMessageW(list, LB_SETITEMDATA, at, i);
    }
    SendMessageW(list, LB_SETCURSEL, 0, 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(ID_PS_LIST, LBN_SELCHANGE), (LPARAM)list);
}

static INT_PTR CALLBACK paste_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG) {
        STATE *state = (STATE *)lparam;
        OLEUIPASTESPECIALW *paste = (OLEUIPASTESPECIALW *)state->standard;
        BOOL link = (paste->dwFlags & PSF_SELECTPASTELINK) != 0;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)state);
        CheckRadioButton(dialog, ID_PS_PASTE, ID_PS_LINK, link ? ID_PS_LINK : ID_PS_PASTE);
        fill_paste_list(dialog, paste, link);
        init_hook(dialog, state, wparam);
        return TRUE;
    }
    if (hooked(dialog, message, wparam, lparam))
        return TRUE;
    if (message != WM_COMMAND)
        return FALSE;
    STATE *state = (STATE *)GetWindowLongPtrW(dialog, DWLP_USER);
    OLEUIPASTESPECIALW *paste = (OLEUIPASTESPECIALW *)state->standard;
    switch (LOWORD(wparam)) {
    case ID_PS_PASTE:
    case ID_PS_LINK:
        fill_paste_list(dialog, paste, LOWORD(wparam) == ID_PS_LINK);
        return TRUE;
    case ID_PS_LIST:
        if (HIWORD(wparam) == LBN_SELCHANGE) {
            HWND list = GetDlgItem(dialog, ID_PS_LIST);
            int at = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
            WCHAR result[512] = L"";
            if (at >= 0) {
                int index = (int)SendMessageW(list, LB_GETITEMDATA, at, 0);
                if (paste->arrPasteEntries[index].lpstrResultText)
                    substitute(paste->arrPasteEntries[index].lpstrResultText, L"Unknown Source", nullptr, result, 512);
            }
            SetDlgItemTextW(dialog, ID_PS_RESULT, result);
            EnableWindow(GetDlgItem(dialog, IDOK), at >= 0);
        }
        if (HIWORD(wparam) != LBN_DBLCLK)
            return TRUE;
        [[fallthrough]];
    case IDOK: {
        HWND list = GetDlgItem(dialog, ID_PS_LIST);
        int at = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
        if (at < 0)
            return TRUE;
        paste->nSelectedIndex = (int)SendMessageW(list, LB_GETITEMDATA, at, 0);
        paste->fLink = IsDlgButtonChecked(dialog, ID_PS_LINK) == BST_CHECKED;
        state->result = OLEUI_OK;
        EndDialog(dialog, 1);
        return TRUE;
    }
    case IDCANCEL:
        state->result = OLEUI_CANCEL;
        EndDialog(dialog, 1);
        return TRUE;
    }
    return FALSE;
}

OLEDLGAPI UINT WINAPI OleUIPasteSpecialW(LPOLEUIPASTESPECIALW paste)
{
    static const CONTROL controls[] = {
        {CTL_BUTTON, L"&Paste", ID_PS_PASTE, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 8, 8, 60, 10},
        {CTL_BUTTON, L"Paste &Link", ID_PS_LINK, BS_AUTORADIOBUTTON, 8, 20, 60, 10},
        {CTL_STATIC, L"&As:", 0xffff, SS_LEFT, 76, 8, 40, 10},
        {CTL_LISTBOX, L"", ID_PS_LIST, LBS_NOTIFY | WS_BORDER | WS_VSCROLL | WS_TABSTOP, 76, 20, 150, 60},
        {CTL_STATIC, L"", ID_PS_RESULT, SS_LEFT, 8, 86, 218, 30},
        {CTL_BUTTON, L"OK", IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, 100, 122, 60, 14},
        {CTL_BUTTON, L"Cancel", IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, 166, 122, 60, 14},
    };
    UINT result = validate(paste, sizeof(OLEUIPASTESPECIALW));
    if (result != OLEUI_SUCCESS)
        return result;
    UINT error = OLEUI_SUCCESS;
    if (paste->lpSrcDataObj && IsBadReadPtr(paste->lpSrcDataObj, 8))
        error = OLEUI_PSERR_SRCDATAOBJECTINVALID;
    if (!paste->arrPasteEntries || IsBadReadPtr(paste->arrPasteEntries, sizeof(OLEUIPASTEENTRYW) * paste->cPasteEntries))
        error = OLEUI_PSERR_ARRPASTEENTRIESINVALID;
    if ((UINT)paste->cLinkTypes > 8 || (paste->cLinkTypes > 0 && !paste->arrLinkTypes) ||
        IsBadReadPtr(paste->arrLinkTypes, 4 * paste->cLinkTypes))
        error = OLEUI_PSERR_ARRLINKTYPESINVALID;
    if (paste->cClsidExclude && (!paste->lpClsidExclude || IsBadReadPtr(paste->lpClsidExclude, 16 * paste->cClsidExclude)))
        error = OLEUI_IOERR_LPCLSIDEXCLUDEINVALID;
    if (!paste->lpSrcDataObj) {
        if (OleGetClipboard(&paste->lpSrcDataObj) != S_OK || !paste->lpSrcDataObj)
            return OLEUI_PSERR_GETCLIPBOARDFAILED;
    }
    if (error >= 100)
        return error;
    return run_dialog((STANDARD *)paste, L"Paste Special", controls, 7, 234, 144, paste_proc, nullptr);
}

/* ------------------------------------------------------------------ the insert object dialog */

#define ID_IO_NEW 100
#define ID_IO_FILE 101
#define ID_IO_LIST 102
#define ID_IO_PATH 103
#define ID_IO_BROWSE 104
#define ID_IO_LINK 105
#define ID_IO_ICON 106

typedef struct {
    CLSID *classes;
    int count;
} CLASS_LIST;

static BOOL excluded(OLEUIINSERTOBJECTW *insert, const CLSID *clsid)
{
    for (UINT i = 0; i < insert->cClsidExclude; i++)
        if (IsEqualCLSID(&insert->lpClsidExclude[i], clsid))
            return TRUE;
    return FALSE;
}

/* the classes that say they can be inserted: an Insertable key under their class key. */
static void fill_class_list(HWND dialog, OLEUIINSERTOBJECTW *insert, CLASS_LIST *classes)
{
    HWND list = GetDlgItem(dialog, ID_IO_LIST);
    HKEY root;
    WCHAR key[64];
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID", 0, KEY_READ, &root) != ERROR_SUCCESS)
        return;
    classes->classes = LocalAlloc(LPTR, 512 * sizeof(CLSID));
    for (DWORD i = 0; classes->classes && classes->count < 512 && RegEnumKeyW(root, i, key, 64) == ERROR_SUCCESS; i++) {
        CLSID clsid;
        HKEY class_key, insertable;
        WCHAR name[128];
        DWORD size = sizeof(name), type;
        if (FAILED(CLSIDFromString(key, &clsid)) || excluded(insert, &clsid))
            continue;
        if (RegOpenKeyExW(root, key, 0, KEY_READ, &class_key) != ERROR_SUCCESS)
            continue;
        if (RegOpenKeyExW(class_key, L"Insertable", 0, KEY_READ, &insertable) == ERROR_SUCCESS) {
            RegCloseKey(insertable);
            if (RegQueryValueExW(class_key, nullptr, nullptr, &type, (BYTE *)name, &size) == ERROR_SUCCESS && name[0]) {
                int at = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)name);
                classes->classes[classes->count] = clsid;
                SendMessageW(list, LB_SETITEMDATA, at, classes->count++);
            }
        }
        RegCloseKey(class_key);
    }
    RegCloseKey(root);
    SendMessageW(list, LB_SETCURSEL, 0, 0);
}

static void show_mode(HWND dialog, BOOL create_new)
{
    EnableWindow(GetDlgItem(dialog, ID_IO_LIST), create_new);
    EnableWindow(GetDlgItem(dialog, ID_IO_PATH), !create_new);
    EnableWindow(GetDlgItem(dialog, ID_IO_BROWSE), !create_new);
    EnableWindow(GetDlgItem(dialog, ID_IO_LINK), !create_new);
}

static INT_PTR CALLBACK insert_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG) {
        STATE *state = (STATE *)lparam;
        OLEUIINSERTOBJECTW *insert = (OLEUIINSERTOBJECTW *)state->standard;
        BOOL from_file = (insert->dwFlags & IOF_SELECTCREATEFROMFILE) != 0;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)state);
        fill_class_list(dialog, insert, state->data);
        CheckRadioButton(dialog, ID_IO_NEW, ID_IO_FILE, from_file ? ID_IO_FILE : ID_IO_NEW);
        show_mode(dialog, !from_file);
        if (insert->dwFlags & IOF_DISABLELINK)
            EnableWindow(GetDlgItem(dialog, ID_IO_LINK), FALSE);
        CheckDlgButton(dialog, ID_IO_LINK, (insert->dwFlags & IOF_CHECKLINK) ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, ID_IO_ICON, (insert->dwFlags & IOF_CHECKDISPLAYASICON) ? BST_CHECKED : BST_UNCHECKED);
        if (insert->dwFlags & IOF_DISABLEDISPLAYASICON)
            EnableWindow(GetDlgItem(dialog, ID_IO_ICON), FALSE);
        SetDlgItemTextW(dialog, ID_IO_PATH, insert->lpszFile);
        init_hook(dialog, state, wparam);
        return TRUE;
    }
    if (hooked(dialog, message, wparam, lparam))
        return TRUE;
    if (message != WM_COMMAND)
        return FALSE;
    STATE *state = (STATE *)GetWindowLongPtrW(dialog, DWLP_USER);
    OLEUIINSERTOBJECTW *insert = (OLEUIINSERTOBJECTW *)state->standard;
    CLASS_LIST *classes = state->data;
    switch (LOWORD(wparam)) {
    case ID_IO_NEW:
    case ID_IO_FILE:
        show_mode(dialog, LOWORD(wparam) == ID_IO_NEW);
        return TRUE;
    case ID_IO_BROWSE: {
        WCHAR path[MAX_PATH];
        GetDlgItemTextW(dialog, ID_IO_PATH, path, MAX_PATH);
        if (browse(dialog, path))
            SetDlgItemTextW(dialog, ID_IO_PATH, path);
        return TRUE;
    }
    case ID_IO_LIST:
        if (HIWORD(wparam) != LBN_DBLCLK)
            return TRUE;
        [[fallthrough]];
    case IDOK: {
        BOOL create_new = IsDlgButtonChecked(dialog, ID_IO_NEW) == BST_CHECKED;
        BOOL link = !create_new && IsDlgButtonChecked(dialog, ID_IO_LINK) == BST_CHECKED;
        BOOL icon = IsDlgButtonChecked(dialog, ID_IO_ICON) == BST_CHECKED;
        insert->dwFlags &= ~(IOF_SELECTCREATENEW | IOF_SELECTCREATEFROMFILE | IOF_CHECKLINK | IOF_CHECKDISPLAYASICON);
        insert->dwFlags |= (create_new ? IOF_SELECTCREATENEW : IOF_SELECTCREATEFROMFILE) | (link ? IOF_CHECKLINK : 0) |
                           (icon ? IOF_CHECKDISPLAYASICON : 0);
        if (create_new) {
            int at = (int)SendMessageW(GetDlgItem(dialog, ID_IO_LIST), LB_GETCURSEL, 0, 0);
            if (at < 0)
                return TRUE;
            insert->clsid = classes->classes[SendMessageW(GetDlgItem(dialog, ID_IO_LIST), LB_GETITEMDATA, at, 0)];
        } else {
            WCHAR path[MAX_PATH];
            GetDlgItemTextW(dialog, ID_IO_PATH, path, MAX_PATH);
            if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
                WCHAR text[MAX_PATH + 32];
                substitute(L"File %s does not exist.", path, nullptr, text, RTL_NUMBER_OF(text));
                MessageBoxW(dialog, text, insert->lpszCaption ? insert->lpszCaption : L"Insert Object",
                            MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            lstrcpynW(insert->lpszFile, path, insert->cchFile);
            insert->clsid = GUID_NULL;
        }
        state->result = OLEUI_OK;
        EndDialog(dialog, 1);
        return TRUE;
    }
    case IDCANCEL:
        state->result = OLEUI_CANCEL;
        EndDialog(dialog, 1);
        return TRUE;
    }
    return FALSE;
}

OLEDLGAPI UINT WINAPI OleUIInsertObjectW(LPOLEUIINSERTOBJECTW insert)
{
    static const CONTROL controls[] = {
        {CTL_BUTTON, L"Create &New", ID_IO_NEW, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 8, 8, 70, 10},
        {CTL_BUTTON, L"Create from &File", ID_IO_FILE, BS_AUTORADIOBUTTON, 8, 20, 70, 10},
        {CTL_STATIC, L"Object &Type:", 0xffff, SS_LEFT, 90, 8, 100, 10},
        {CTL_LISTBOX, L"", ID_IO_LIST, LBS_NOTIFY | WS_BORDER | WS_VSCROLL | WS_TABSTOP, 90, 20, 140, 60},
        {CTL_EDIT, L"", ID_IO_PATH, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 90, 88, 100, 12},
        {CTL_BUTTON, L"&Browse...", ID_IO_BROWSE, BS_PUSHBUTTON | WS_TABSTOP, 196, 87, 34, 14},
        {CTL_BUTTON, L"&Link", ID_IO_LINK, BS_AUTOCHECKBOX | WS_TABSTOP, 90, 104, 60, 10},
        {CTL_BUTTON, L"&Display as icon", ID_IO_ICON, BS_AUTOCHECKBOX | WS_TABSTOP, 8, 120, 80, 10},
        {CTL_BUTTON, L"OK", IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, 100, 134, 60, 14},
        {CTL_BUTTON, L"Cancel", IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, 166, 134, 64, 14},
    };
    UINT result = validate(insert, sizeof(OLEUIINSERTOBJECTW));
    if (result != OLEUI_SUCCESS)
        return result;
    UINT error = OLEUI_SUCCESS;
    if (insert->lpszFile) {
        if (insert->cchFile - 1 > 0x103)
            error = OLEUI_IOERR_CCHFILEINVALID;
        if (IsBadWritePtr(insert->lpszFile, 2 * insert->cchFile))
            error = OLEUI_IOERR_LPSZFILEINVALID;
    } else {
        error = OLEUI_IOERR_LPSZFILEINVALID;
    }
    if (insert->cClsidExclude && (!insert->lpClsidExclude || IsBadReadPtr(insert->lpClsidExclude, 16 * insert->cClsidExclude)))
        error = OLEUI_IOERR_LPCLSIDEXCLUDEINVALID;
    if (insert->dwFlags & (IOF_CREATENEWOBJECT | IOF_CREATEFILEOBJECT | IOF_CREATELINKOBJECT)) {
        if (insert->lpFormatEtc && IsBadReadPtr(insert->lpFormatEtc, sizeof(FORMATETC)))
            error = OLEUI_IOERR_LPFORMATETCINVALID;
        if (insert->ppvObj && IsBadWritePtr(insert->ppvObj, sizeof(void *)))
            error = OLEUI_IOERR_PPVOBJINVALID;
        if (insert->lpIOleClientSite && IsBadReadPtr(insert->lpIOleClientSite, sizeof(void *)))
            error = OLEUI_IOERR_LPIOLECLIENTSITEINVALID;
        if (insert->lpIStorage && IsBadReadPtr(insert->lpIStorage, sizeof(void *)))
            error = OLEUI_IOERR_LPISTORAGEINVALID;
        if (insert->ppvObj && !insert->lpIStorage)
            error = OLEUI_IOERR_LPISTORAGEINVALID;
    }
    if (error >= 100)
        return error;
    CLASS_LIST classes = {};
    result = run_dialog((STANDARD *)insert, L"Insert Object", controls, 10, 238, 156, insert_proc, &classes);
    LocalFree(classes.classes);
    if (result != OLEUI_OK || !(insert->dwFlags & (IOF_CREATENEWOBJECT | IOF_CREATEFILEOBJECT | IOF_CREATELINKOBJECT)))
        return result;
    /* the object is made for the caller when the flags ask for it, and an error of the making is told in sc. */
    HRESULT hr = E_INVALIDARG;
    if ((insert->dwFlags & IOF_SELECTCREATENEW) && (insert->dwFlags & IOF_CREATENEWOBJECT))
        hr = OleCreate(&insert->clsid, &insert->iid, insert->oleRender, insert->lpFormatEtc, insert->lpIOleClientSite,
                       insert->lpIStorage, insert->ppvObj);
    else if ((insert->dwFlags & IOF_CHECKLINK) && (insert->dwFlags & IOF_CREATELINKOBJECT))
        hr = OleCreateLinkToFile(insert->lpszFile, &insert->iid, insert->oleRender, insert->lpFormatEtc,
                                 insert->lpIOleClientSite, insert->lpIStorage, insert->ppvObj);
    else if (insert->dwFlags & IOF_CREATEFILEOBJECT)
        hr = OleCreateFromFile(&GUID_NULL, insert->lpszFile, &insert->iid, insert->oleRender, insert->lpFormatEtc,
                               insert->lpIOleClientSite, insert->lpIStorage, insert->ppvObj);
    insert->sc = hr;
    return FAILED(hr) ? OLEUI_IOERR_SCODEHASERROR : OLEUI_OK;
}

/* ------------------------------------------------------------------ the dialogs that are not made */

OLEDLGAPI UINT WINAPI OleUIChangeIconW(LPOLEUICHANGEICONW change)
{
    UINT result = validate(change, sizeof(OLEUICHANGEICONW));
    if (result != OLEUI_SUCCESS)
        return result;
    if (change->hMetaPict && !GlobalLock(change->hMetaPict))
        return OLEUI_CIERR_MUSTHAVECURRENTMETAPICT;
    if (change->hMetaPict)
        GlobalUnlock(change->hMetaPict);
    HGLOBAL icon = OleGetIconOfClass(&change->clsid, nullptr, TRUE);
    if (!icon)
        return OLEUI_CIERR_MUSTHAVECLSID;
    GlobalFree(icon);
    if ((change->dwFlags & 0x10) && (UINT)(change->cchIconExe - 1) > 0x103)
        return OLEUI_CIERR_SZICONEXEINVALID;
    return not_made();
}

OLEDLGAPI UINT WINAPI OleUIConvertW(LPOLEUICONVERTW convert)
{
    UINT result = validate(convert, sizeof(OLEUICONVERTW));
    if (result != OLEUI_SUCCESS)
        return result;
    if (convert->hMetaPict && !GlobalLock(convert->hMetaPict))
        return OLEUI_CTERR_HMETAPICTINVALID;
    if (convert->hMetaPict)
        GlobalUnlock(convert->hMetaPict);
    UINT error = OLEUI_SUCCESS;
    if ((convert->dwFlags & 2) && IsEqualCLSID(&convert->clsidConvertDefault, &GUID_NULL))
        error = OLEUI_CTERR_CLASSIDINVALID;
    if ((convert->dwFlags & 4) && IsEqualCLSID(&convert->clsidActivateDefault, &GUID_NULL))
        error = OLEUI_CTERR_CLASSIDINVALID;
    if (convert->dvAspect != DVASPECT_ICON && convert->dvAspect != DVASPECT_CONTENT)
        error = OLEUI_CTERR_DVASPECTINVALID;
    WCHAR format[8];
    if (convert->wFormat >= 0xc000 && !GetClipboardFormatNameW(convert->wFormat, format, 8))
        error = OLEUI_CTERR_CBFORMATINVALID;
    if ((convert->lpszUserType && IsBadReadPtr(convert->lpszUserType, 1)) ||
        (convert->lpszDefLabel && IsBadReadPtr(convert->lpszDefLabel, 1)))
        error = OLEUI_CTERR_STRINGINVALID;
    if (convert->cClsidExclude && (!convert->lpClsidExclude || IsBadReadPtr(convert->lpClsidExclude, 16 * convert->cClsidExclude)))
        return OLEUI_IOERR_LPCLSIDEXCLUDEINVALID;
    if (error >= 100)
        return error;
    return not_made();
}

/* ------------------------------------------------------------------ the links dialog */

#define ID_EL_LIST 100
#define ID_EL_UPDATE 101
#define ID_EL_OPEN 102
#define ID_EL_CHANGE 103
#define ID_EL_BREAK 104
#define ID_EL_AUTO 105
#define ID_EL_MANUAL 106
#define OLEUPDATE_ALWAYS_ 1
#define OLEUPDATE_ONCALL_ 3

static DWORD selected_link(HWND dialog)
{
    HWND list = GetDlgItem(dialog, ID_EL_LIST);
    int at = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    return at < 0 ? 0 : (DWORD)SendMessageW(list, LB_GETITEMDATA, at, 0);
}

static void free_task(void *memory)
{
    CoTaskMemFree(memory);
}

/* each link as a line: where it comes from, what it is, and how it is kept up to date. */
static void fill_links(HWND dialog, LPOLEUILINKCONTAINERW container)
{
    HWND list = GetDlgItem(dialog, ID_EL_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (DWORD link = container->lpVtbl->GetNextLink(container, 0); link;
         link = container->lpVtbl->GetNextLink(container, link)) {
        LPWSTR name = nullptr, full_type = nullptr, short_type = nullptr;
        ULONG file_length = 0;
        BOOL available = TRUE, selected = FALSE;
        DWORD options = 0;
        if (FAILED(container->lpVtbl->GetLinkSource(container, link, &name, &file_length, &full_type, &short_type,
                                                    &available, &selected)))
            continue;
        container->lpVtbl->GetLinkUpdateOptions(container, link, &options);
        WCHAR line[600];
        wsprintfW(line, L"%s\t%s\t%s", name ? name : L"", full_type ? full_type : L"",
                  !available ? L"Unavailable" : options == OLEUPDATE_ALWAYS_ ? L"Automatic" : L"Manual");
        int at = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line);
        SendMessageW(list, LB_SETITEMDATA, at, link);
        free_task(name);
        free_task(full_type);
        free_task(short_type);
    }
    SendMessageW(list, LB_SETCURSEL, 0, 0);
    SendMessageW(GetParent(list), WM_COMMAND, MAKEWPARAM(ID_EL_LIST, LBN_SELCHANGE), (LPARAM)list);
}

static INT_PTR CALLBACK edit_links_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG) {
        STATE *state = (STATE *)lparam;
        OLEUIEDITLINKSW *edit = (OLEUIEDITLINKSW *)state->standard;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)state);
        fill_links(dialog, edit->lpOleUILinkContainer);
        init_hook(dialog, state, wparam);
        return TRUE;
    }
    if (hooked(dialog, message, wparam, lparam))
        return TRUE;
    if (message != WM_COMMAND)
        return FALSE;
    STATE *state = (STATE *)GetWindowLongPtrW(dialog, DWLP_USER);
    LPOLEUILINKCONTAINERW container = ((OLEUIEDITLINKSW *)state->standard)->lpOleUILinkContainer;
    DWORD link = selected_link(dialog);
    switch (LOWORD(wparam)) {
    case ID_EL_LIST:
        if (HIWORD(wparam) == LBN_SELCHANGE) {
            DWORD options = 0;
            BOOL any = link != 0;
            if (any)
                container->lpVtbl->GetLinkUpdateOptions(container, link, &options);
            for (int id = ID_EL_UPDATE; id <= ID_EL_MANUAL; id++)
                EnableWindow(GetDlgItem(dialog, id), any);
            CheckRadioButton(dialog, ID_EL_AUTO, ID_EL_MANUAL, options == OLEUPDATE_ALWAYS_ ? ID_EL_AUTO : ID_EL_MANUAL);
        }
        return TRUE;
    case ID_EL_UPDATE:
        container->lpVtbl->UpdateLink(container, link, TRUE, FALSE);
        fill_links(dialog, container);
        return TRUE;
    case ID_EL_OPEN:
        container->lpVtbl->OpenLinkSource(container, link);
        return TRUE;
    case ID_EL_AUTO:
    case ID_EL_MANUAL:
        container->lpVtbl->SetLinkUpdateOptions(container, link,
                                                LOWORD(wparam) == ID_EL_AUTO ? OLEUPDATE_ALWAYS_ : OLEUPDATE_ONCALL_);
        fill_links(dialog, container);
        return TRUE;
    case ID_EL_CHANGE: {
        OLEUICHANGESOURCEW change = {};
        change.cbStruct = sizeof(change);
        change.hWndOwner = dialog;
        change.lpOleUILinkContainer = container;
        change.dwLink = link;
        if (OleUIChangeSourceW(&change) == OLEUI_OK)
            fill_links(dialog, container);
        free_task(change.lpszDisplayName);
        free_task(change.lpszFrom);
        free_task(change.lpszTo);
        return TRUE;
    }
    case ID_EL_BREAK:
        if (MessageBoxW(dialog,
                        L"Breaking a link will disconnect it from its link source.  Are you sure you want to break "
                        L"this link?",
                        L"Links", MB_YESNO | MB_ICONEXCLAMATION) == IDYES) {
            container->lpVtbl->CancelLink(container, link);
            fill_links(dialog, container);
        }
        return TRUE;
    case IDCANCEL:
        state->result = OLEUI_OK;
        EndDialog(dialog, 1);
        return TRUE;
    }
    return FALSE;
}

OLEDLGAPI UINT WINAPI OleUIEditLinksW(LPOLEUIEDITLINKSW edit)
{
    static const CONTROL controls[] = {
        {CTL_STATIC, L"Links:", 0xffff, SS_LEFT, 8, 8, 100, 10},
        {CTL_LISTBOX, L"", ID_EL_LIST, LBS_NOTIFY | LBS_USETABSTOPS | WS_BORDER | WS_VSCROLL | WS_TABSTOP, 8, 20, 250, 70},
        {CTL_BUTTON, L"&Update Now", ID_EL_UPDATE, BS_PUSHBUTTON | WS_TABSTOP, 266, 20, 64, 14},
        {CTL_BUTTON, L"&Open Source", ID_EL_OPEN, BS_PUSHBUTTON | WS_TABSTOP, 266, 38, 64, 14},
        {CTL_BUTTON, L"Change &Source...", ID_EL_CHANGE, BS_PUSHBUTTON | WS_TABSTOP, 266, 56, 64, 14},
        {CTL_BUTTON, L"&Break Link", ID_EL_BREAK, BS_PUSHBUTTON | WS_TABSTOP, 266, 74, 64, 14},
        {CTL_BUTTON, L"&Automatic", ID_EL_AUTO, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 8, 98, 70, 10},
        {CTL_BUTTON, L"&Manual", ID_EL_MANUAL, BS_AUTORADIOBUTTON, 84, 98, 70, 10},
        {CTL_BUTTON, L"Close", IDCANCEL, BS_DEFPUSHBUTTON | WS_TABSTOP, 266, 96, 64, 14},
    };
    UINT result = validate(edit, sizeof(OLEUIEDITLINKSW));
    if (result != OLEUI_SUCCESS)
        return result;
    if (!edit->lpOleUILinkContainer)
        return OLEUI_ELERR_LINKCNTRNULL;
    if (IsBadReadPtr(edit->lpOleUILinkContainer, 8))
        return OLEUI_ELERR_LINKCNTRINVALID;
    return run_dialog((STANDARD *)edit, L"Links", controls, 9, 338, 118, edit_links_proc, nullptr);
}

OLEDLGAPI UINT WINAPI OleUIObjectPropertiesW(void *properties)
{
    return properties ? not_made() : OLEUI_ERR_STRUCTURENULL;
}

/* the links are not updated without the dialog that does it, which is the answer for a dialog that failed. */
/* TRUE says every link was brought up to date, which a refused argument does not, and no dialog updates them yet. */
OLEDLGAPI BOOL WINAPI OleUIUpdateLinksW(LPOLEUILINKCONTAINERW container, HWND parent, LPWSTR title, int links)
{
    return FALSE;
}

/* ------------------------------------------------------------------ the ansi entries */

static LPCWSTR wide_copy(LPCSTR text, WCHAR *out, int chars)
{
    if (!text)
        return nullptr;
    MultiByteToWideChar(CP_ACP, 0, text, -1, out, chars);
    return out;
}

OLEDLGAPI UINT WINAPI OleUIBusyA(void *busy)
{
    WCHAR caption[128];
    OLEUIBUSYW wide;
    if (!busy || IsBadReadPtr(busy, sizeof(wide)))
        return validate(busy, sizeof(wide));
    CopyMemory(&wide, busy, sizeof(wide));
    wide.lpszCaption = wide_copy((LPCSTR)wide.lpszCaption, caption, 128);
    UINT result = OleUIBusyW(&wide);
    ((OLEUIBUSYW *)busy)->dwFlags = wide.dwFlags;
    return result;
}

OLEDLGAPI UINT WINAPI OleUIInsertObjectA(void *insert)
{
    WCHAR caption[128], file[MAX_PATH];
    OLEUIINSERTOBJECTW wide;
    if (!insert || IsBadReadPtr(insert, sizeof(wide)))
        return validate(insert, sizeof(wide));
    CopyMemory(&wide, insert, sizeof(wide));
    OLEUIINSERTOBJECTW *original = insert;
    UINT chars = wide.cchFile;
    wide.lpszCaption = wide_copy((LPCSTR)wide.lpszCaption, caption, 128);
    if (wide.lpszFile) {
        wide_copy((LPCSTR)wide.lpszFile, file, MAX_PATH);
        wide.lpszFile = file;
        wide.cchFile = chars < MAX_PATH ? chars : MAX_PATH;
    }
    UINT result = OleUIInsertObjectW(&wide);
    if (wide.lpszFile && original->lpszFile)
        WideCharToMultiByte(CP_ACP, 0, file, -1, (char *)original->lpszFile, chars, nullptr, nullptr);
    original->dwFlags = wide.dwFlags;
    original->clsid = wide.clsid;
    original->sc = wide.sc;
    original->hMetaPict = wide.hMetaPict;
    return result;
}

OLEDLGAPI UINT WINAPI OleUIPasteSpecialA(void *paste)
{
    WCHAR caption[128];
    OLEUIPASTESPECIALW wide;
    if (!paste || IsBadReadPtr(paste, sizeof(wide)))
        return validate(paste, sizeof(wide));
    CopyMemory(&wide, paste, sizeof(wide));
    wide.lpszCaption = wide_copy((LPCSTR)wide.lpszCaption, caption, 128);
    UINT result = OleUIPasteSpecialW(&wide);
    OLEUIPASTESPECIALW *original = paste;
    original->lpSrcDataObj = wide.lpSrcDataObj;
    original->nSelectedIndex = wide.nSelectedIndex;
    original->fLink = wide.fLink;
    original->hMetaPict = wide.hMetaPict;
    return result;
}

OLEDLGAPI UINT WINAPI OleUIChangeSourceA(void *change)
{
    WCHAR caption[128];
    OLEUICHANGESOURCEW wide;
    if (!change || IsBadReadPtr(change, sizeof(wide)))
        return validate(change, sizeof(wide));
    CopyMemory(&wide, change, sizeof(wide));
    wide.lpszCaption = wide_copy((LPCSTR)wide.lpszCaption, caption, 128);
    return OleUIChangeSourceW(&wide);
}

OLEDLGAPI UINT WINAPI OleUIObjectPropertiesA(void *properties)
{
    return OleUIObjectPropertiesW(properties);
}

OLEDLGAPI UINT WINAPI OleUIConvertA(void *convert)
{
    return OleUIConvertW(convert);
}

OLEDLGAPI UINT WINAPI OleUIChangeIconA(void *change)
{
    return OleUIChangeIconW(change);
}

OLEDLGAPI UINT WINAPI OleUIEditLinksA(void *edit)
{
    return OleUIEditLinksW(edit);
}

OLEDLGAPI BOOL WINAPI OleUIUpdateLinksA(LPOLEUILINKCONTAINERW container, HWND parent, LPSTR title, int links)
{
    return OleUIUpdateLinksW(container, parent, nullptr, links);
}
