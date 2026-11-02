/*
 * FindText and ReplaceText, the modeless find and replace dialogs. The dialog keeps the text and the options in the
 * program's FINDREPLACE structure and tells the owner window about every button with the registered message
 * "commdlg_FindReplace", whose lParam is the structure, the flags saying which button it was. Read from the
 * comdlg32 of Windows 11 (build 26300): CreateFindReplaceDlg, FindReplaceDlgProc and what they call.
 */
#include "dlgbase.h"
#include "dlgids.h"

typedef struct _FRDLG {
    BOOL Unicode;
    BOOL Replace;
    FINDREPLACEW *Fr;
} FRDLG;

#define FR_PROP L"mime_comdlg_findreplace"
#define FR_BUTTONS (FR_FINDNEXT | FR_REPLACE | FR_REPLACEALL)
#define FR_OPTIONS (FR_DOWN | FR_WHOLEWORD | FR_MATCHCASE)

static UINT g_find_message;
/* the hook of a dialog that is not made yet, for the messages before WM_INITDIALOG stores the dialog's own. */
static LPFRHOOKPROC g_creation_hook;

static LPDLGTEMPLATEW build_template(BOOL replace)
{
    static const DWORD visible = WS_VISIBLE | WS_CHILD;
    static const CTL find_controls[] = {
        {CTL_STATIC, L"Fi&nd what:", stc1, visible | SS_LEFT, 6, 10, 42, 9},
        {CTL_EDIT, L"", edt1, visible | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 50, 8, 110, 12},
        {CTL_BUTTON, L"Match &whole word only", chx1, visible | WS_TABSTOP | BS_AUTOCHECKBOX | WS_GROUP, 6, 32, 100, 10},
        {CTL_BUTTON, L"&Match case", chx2, visible | WS_TABSTOP | BS_AUTOCHECKBOX, 6, 46, 100, 10},
        {CTL_BUTTON, L"Direction", grp1, visible | BS_GROUPBOX | WS_GROUP, 108, 26, 52, 30},
        {CTL_BUTTON, L"&Up", rad1, visible | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP, 114, 36, 40, 10},
        {CTL_BUTTON, L"&Down", rad2, visible | BS_AUTORADIOBUTTON, 114, 46, 40, 10},
        {CTL_BUTTON, L"&Find Next", IDOK, visible | WS_TABSTOP | BS_DEFPUSHBUTTON | WS_GROUP, 168, 6, 52, 14},
        {CTL_BUTTON, L"Cancel", IDCANCEL, visible | WS_TABSTOP | BS_PUSHBUTTON, 168, 24, 52, 14},
        {CTL_BUTTON, L"&Help", pshHelp, visible | WS_TABSTOP | BS_PUSHBUTTON, 168, 42, 52, 14},
    };
    static const CTL replace_controls[] = {
        {CTL_STATIC, L"Fi&nd what:", stc1, visible | SS_LEFT, 6, 10, 46, 9},
        {CTL_EDIT, L"", edt1, visible | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 56, 8, 104, 12},
        {CTL_STATIC, L"Re&place with:", stc2, visible | SS_LEFT, 6, 28, 46, 9},
        {CTL_EDIT, L"", edt2, visible | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 56, 26, 104, 12},
        {CTL_BUTTON, L"Match &whole word only", chx1, visible | WS_TABSTOP | BS_AUTOCHECKBOX | WS_GROUP, 6, 50, 100, 10},
        {CTL_BUTTON, L"&Match case", chx2, visible | WS_TABSTOP | BS_AUTOCHECKBOX, 6, 64, 100, 10},
        {CTL_BUTTON, L"&Find Next", IDOK, visible | WS_TABSTOP | BS_DEFPUSHBUTTON | WS_GROUP, 168, 6, 56, 14},
        {CTL_BUTTON, L"&Replace", psh1, visible | WS_TABSTOP | BS_PUSHBUTTON, 168, 24, 56, 14},
        {CTL_BUTTON, L"Replace &All", psh2, visible | WS_TABSTOP | BS_PUSHBUTTON, 168, 42, 56, 14},
        {CTL_BUTTON, L"Cancel", IDCANCEL, visible | WS_TABSTOP | BS_PUSHBUTTON, 168, 60, 56, 14},
        {CTL_BUTTON, L"&Help", pshHelp, visible | WS_TABSTOP | BS_PUSHBUTTON, 168, 78, 56, 14},
    };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_3DLOOK;
    if (replace)
        return DlgBuild(L"Replace", style, 232, 98, replace_controls, RTL_NUMBER_OF(replace_controls));
    return DlgBuild(L"Find", style, 228, 64, find_controls, RTL_NUMBER_OF(find_controls));
}

/* the strings of the structure are ansi in FINDREPLACEA. */
static void read_text(HWND dialog, int id, FINDREPLACEW *fr, BOOL unicode, BOOL replace_with)
{
    LPWSTR wide = replace_with ? fr->lpstrReplaceWith : fr->lpstrFindWhat;
    WORD length = replace_with ? fr->wReplaceWithLen : fr->wFindWhatLen;
    if (!wide || !length)
        return;
    if (unicode) {
        GetDlgItemTextW(dialog, id, wide, length);
        return;
    }
    WCHAR buffer[1024];
    GetDlgItemTextW(dialog, id, buffer, RTL_NUMBER_OF(buffer));
    DlgToAnsi(buffer, (LPSTR)wide, length);
}

/* the text and the options of the controls go to the structure. */
static void update_text_and_flags(HWND dialog, const FRDLG *d)
{
    FINDREPLACEW *fr = d->Fr;
    read_text(dialog, edt1, fr, d->Unicode, FALSE);
    if (d->Replace)
        read_text(dialog, edt2, fr, d->Unicode, TRUE);
    DWORD flags = fr->Flags & ~FR_OPTIONS;
    if (IsDlgButtonChecked(dialog, chx1) == BST_CHECKED)
        flags |= FR_WHOLEWORD;
    if (IsDlgButtonChecked(dialog, chx2) == BST_CHECKED)
        flags |= FR_MATCHCASE;
    /* a replace dialog has no direction and always goes down. */
    if (d->Replace || IsDlgButtonChecked(dialog, rad2) == BST_CHECKED)
        flags |= FR_DOWN;
    fr->Flags = flags;
}

static LRESULT notify_owner(FINDREPLACEW *fr)
{
    if (!fr->hwndOwner)
        return 0;
    UINT message = DlgMessage(&g_find_message, L"commdlg_FindReplace");
    return message ? SendMessageW(fr->hwndOwner, message, 0, (LPARAM)fr) : 0;
}

static void hide_or_disable(HWND dialog, int id, BOOL hide, BOOL disable)
{
    HWND control = GetDlgItem(dialog, id);
    if (!control)
        return;
    if (hide)
        ShowWindow(control, SW_HIDE);
    if (disable)
        EnableWindow(control, FALSE);
}

static void init_controls(HWND dialog, const FRDLG *d)
{
    FINDREPLACEW *fr = d->Fr;
    DWORD flags = fr->Flags;
    if (fr->lpstrFindWhat) {
        WCHAR text[1024];
        if (d->Unicode)
            lstrcpynW(text, fr->lpstrFindWhat, RTL_NUMBER_OF(text));
        else
            DlgToWide((LPCSTR)fr->lpstrFindWhat, text, RTL_NUMBER_OF(text));
        text[RTL_NUMBER_OF(text) - 1] = 0;
        SetDlgItemTextW(dialog, edt1, text);
    }
    if (fr->wFindWhatLen)
        SendDlgItemMessageW(dialog, edt1, EM_LIMITTEXT, fr->wFindWhatLen - 1, 0);
    if (d->Replace) {
        if (fr->lpstrReplaceWith) {
            WCHAR text[1024];
            if (d->Unicode)
                lstrcpynW(text, fr->lpstrReplaceWith, RTL_NUMBER_OF(text));
            else
                DlgToWide((LPCSTR)fr->lpstrReplaceWith, text, RTL_NUMBER_OF(text));
            text[RTL_NUMBER_OF(text) - 1] = 0;
            SetDlgItemTextW(dialog, edt2, text);
        }
        if (fr->wReplaceWithLen)
            SendDlgItemMessageW(dialog, edt2, EM_LIMITTEXT, fr->wReplaceWithLen - 1, 0);
    }
    CheckDlgButton(dialog, chx1, (flags & FR_WHOLEWORD) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, chx2, (flags & FR_MATCHCASE) ? BST_CHECKED : BST_UNCHECKED);
    CheckRadioButton(dialog, rad1, rad2, (flags & FR_DOWN) ? rad2 : rad1);
    hide_or_disable(dialog, chx1, flags & FR_HIDEWHOLEWORD, flags & FR_NOWHOLEWORD);
    hide_or_disable(dialog, chx2, flags & FR_HIDEMATCHCASE, flags & FR_NOMATCHCASE);
    hide_or_disable(dialog, grp1, flags & FR_HIDEUPDOWN, flags & FR_NOUPDOWN);
    hide_or_disable(dialog, rad1, flags & FR_HIDEUPDOWN, flags & FR_NOUPDOWN);
    hide_or_disable(dialog, rad2, flags & FR_HIDEUPDOWN, flags & FR_NOUPDOWN);
    hide_or_disable(dialog, pshHelp, !(flags & FR_SHOWHELP), FALSE);
    /* nothing to look for, nothing to do. */
    BOOL has_text = GetWindowTextLengthW(GetDlgItem(dialog, edt1)) != 0;
    EnableWindow(GetDlgItem(dialog, IDOK), has_text);
    if (d->Replace) {
        EnableWindow(GetDlgItem(dialog, psh1), has_text);
        EnableWindow(GetDlgItem(dialog, psh2), has_text);
    }
}

static void close_dialog(HWND dialog, FRDLG *d)
{
    FINDREPLACEW *fr = d->Fr;
    fr->Flags = (fr->Flags & ~FR_BUTTONS) | FR_DIALOGTERM;
    notify_owner(fr);
    RemovePropW(dialog, FR_PROP);
    DestroyWindow(dialog);
    HeapFree(GetProcessHeap(), 0, d);
}

static INT_PTR CALLBACK find_replace_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    FRDLG *d = GetPropW(dialog, FR_PROP);
    FINDREPLACEW *fr = d ? d->Fr : nullptr;
    LPFRHOOKPROC hook = fr ? ((fr->Flags & FR_ENABLEHOOK) ? fr->lpfnHook : nullptr) : g_creation_hook;
    /* the hook sees everything first, except the initialization of a dialog it does not own yet. */
    if (hook && (d || message != WM_INITDIALOG)) {
        INT_PTR result = (INT_PTR)hook(dialog, message, wparam, lparam);
        if (result)
            return result;
    }
    switch (message) {
    case WM_INITDIALOG: {
        d = (FRDLG *)lparam;
        SetPropW(dialog, FR_PROP, d);
        g_creation_hook = nullptr;
        init_controls(dialog, d);
        INT_PTR result = TRUE;
        if (d->Fr->Flags & FR_ENABLEHOOK && d->Fr->lpfnHook) {
            result = (INT_PTR)d->Fr->lpfnHook(dialog, WM_INITDIALOG, wparam, (LPARAM)d->Fr);
            if (!result)
                return result;
        }
        ShowWindow(dialog, SW_SHOW);
        UpdateWindow(dialog);
        return result;
    }
    case WM_CLOSE:
        SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        return TRUE;
    case WM_COMMAND:
        if (!d)
            return FALSE;
        switch (LOWORD(wparam)) {
        case IDOK:
            update_text_and_flags(dialog, d);
            fr->Flags = (fr->Flags & ~FR_BUTTONS) | FR_FINDNEXT;
            notify_owner(fr);
            return TRUE;
        case psh1:
        case psh2:
            update_text_and_flags(dialog, d);
            fr->Flags = (fr->Flags & ~FR_BUTTONS) | (LOWORD(wparam) == psh1 ? FR_REPLACE : FR_REPLACEALL);
            notify_owner(fr);
            return TRUE;
        case IDCANCEL:
            close_dialog(dialog, d);
            return TRUE;
        case pshHelp: {
            static UINT help_message;
            UINT help = DlgMessage(&help_message, L"commdlg_help");
            if (help && fr->hwndOwner)
                SendMessageW(fr->hwndOwner, help, (WPARAM)dialog, (LPARAM)fr);
            return TRUE;
        }
        case edt1:
            if (HIWORD(wparam) == EN_CHANGE) {
                BOOL has_text = GetWindowTextLengthW(GetDlgItem(dialog, edt1)) != 0;
                EnableWindow(GetDlgItem(dialog, IDOK), has_text);
                if (d->Replace) {
                    EnableWindow(GetDlgItem(dialog, psh1), has_text);
                    EnableWindow(GetDlgItem(dialog, psh2), has_text);
                }
            }
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

static HWND create_find_replace(FINDREPLACEW *fr, BOOL replace, BOOL unicode)
{
    DlgSetError(0);
    if (!fr) {
        DlgSetError(CDERR_INITIALIZATION);
        return nullptr;
    }
    DWORD error = 0;
    if (!DlgHookValid(fr->Flags, FR_ENABLEHOOK, fr->lpfnHook, &error)) {
        DlgSetError(error);
        return nullptr;
    }
    if (!fr->lpstrFindWhat || !fr->wFindWhatLen) {
        DlgSetError(CDERR_FINDRESFAILURE);
        return nullptr;
    }
    DLGSOURCE source = {fr->Flags, FR_ENABLETEMPLATE, FR_ENABLETEMPLATEHANDLE, fr->hInstance, fr->lpTemplateName,
                        !unicode};
    const void *given = DlgFindTemplate(&source, &error);
    if (error) {
        DlgSetError(error);
        return nullptr;
    }
    LPDLGTEMPLATEW built = given ? nullptr : build_template(replace);
    FRDLG *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d || (!given && !built)) {
        if (built)
            HeapFree(GetProcessHeap(), 0, built);
        if (d)
            HeapFree(GetProcessHeap(), 0, d);
        DlgSetError(CDERR_MEMALLOCFAILURE);
        return nullptr;
    }
    d->Unicode = unicode;
    d->Replace = replace;
    d->Fr = fr;
    g_creation_hook = (fr->Flags & FR_ENABLEHOOK) ? fr->lpfnHook : nullptr;
    HWND dialog = CreateDialogIndirectParamW(g_comdlg32, given ? (LPCDLGTEMPLATEW)given : built, fr->hwndOwner,
                                             find_replace_proc, (LPARAM)d);
    g_creation_hook = nullptr;
    if (built)
        HeapFree(GetProcessHeap(), 0, built);
    if (!dialog) {
        HeapFree(GetProcessHeap(), 0, d);
        DlgSetError(CDERR_DIALOGFAILURE);
    }
    return dialog;
}

CDAPI HWND WINAPI FindTextW(LPFINDREPLACEW fr)
{
    return create_find_replace(fr, FALSE, TRUE);
}

CDAPI HWND WINAPI FindTextA(LPFINDREPLACEA fr)
{
    return create_find_replace((FINDREPLACEW *)fr, FALSE, FALSE);
}

CDAPI HWND WINAPI ReplaceTextW(LPFINDREPLACEW fr)
{
    return create_find_replace(fr, TRUE, TRUE);
}

CDAPI HWND WINAPI ReplaceTextA(LPFINDREPLACEA fr)
{
    return create_find_replace((FINDREPLACEW *)fr, TRUE, FALSE);
}
