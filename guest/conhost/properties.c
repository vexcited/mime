/*
 * the properties of a console window: the choices that belong to the console and apply to it, as the dialog of the system
 * menu of the console window of Windows has them, with the edit modes, the size of the buffer and the command history.
 */
#include "conhost.h"

enum {
    ID_QUICK_EDIT = 101,
    ID_INSERT,
    ID_WIDTH,
    ID_HEIGHT,
    ID_HISTORY_SIZE,
    ID_HISTORY_BUFFERS,
    ID_DISCARD_DUPLICATES,
    ID_FONT,
};

typedef struct Builder {
    BYTE *Data;
    SIZE_T Used, Capacity;
} Builder;

static void put(Builder *b, const void *data, SIZE_T size)
{
    if (b->Used + size > b->Capacity)
        return;
    memcpy(b->Data + b->Used, data, size);
    b->Used += size;
}

static void align(Builder *b)
{
    while (b->Used & 3) {
        BYTE zero = 0;
        put(b, &zero, 1);
    }
}

static void put_string(Builder *b, const WCHAR *s)
{
    put(b, s, (lstrlenW(s) + 1) * sizeof(WCHAR));
}

static void put_item(Builder *b, DWORD style, int x, int y, int w, int h, WORD id, WORD class_atom, const WCHAR *text)
{
    align(b);
    DWORD ex = 0;
    SHORT xs[4] = {(SHORT)x, (SHORT)y, (SHORT)w, (SHORT)h};
    put(b, &style, 4);
    put(b, &ex, 4);
    put(b, xs, sizeof(xs));
    put(b, &id, 2);
    WORD atom[2] = {0xFFFF, class_atom};
    put(b, atom, sizeof(atom));
    put_string(b, text);
    WORD extra = 0;
    put(b, &extra, 2);
}

static DLGTEMPLATE *build_template(Builder *b)
{
    DWORD style = DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD ex = 0;
    WORD count = 12;
    SHORT box[4] = {0, 0, 210, 150};
    put(b, &style, 4);
    put(b, &ex, 4);
    put(b, &count, 2);
    put(b, box, sizeof(box));
    WORD none = 0;
    put(b, &none, 2); /* no menu */
    put(b, &none, 2); /* the dialog class */
    put_string(b, L"Console Window Properties");
    WORD size = 9;
    put(b, &size, 2);
    put_string(b, L"Segoe UI");
    const DWORD child = WS_CHILD | WS_VISIBLE;
    put_item(b, child | WS_TABSTOP | BS_AUTOCHECKBOX, 10, 10, 190, 12, ID_QUICK_EDIT, 0x80, L"&QuickEdit Mode");
    put_item(b, child | WS_TABSTOP | BS_AUTOCHECKBOX, 10, 25, 190, 12, ID_INSERT, 0x80, L"&Insert Mode");
    put_item(b, child, 10, 45, 100, 10, 0xFFFF, 0x82, L"Screen buffer width:");
    put_item(b, child | WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 130, 43, 40, 12, ID_WIDTH, 0x81, L"");
    put_item(b, child, 10, 62, 100, 10, 0xFFFF, 0x82, L"Screen buffer height:");
    put_item(b, child | WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 130, 60, 40, 12, ID_HEIGHT, 0x81, L"");
    put_item(b, child, 10, 79, 110, 10, 0xFFFF, 0x82, L"Commands in a history buffer:");
    put_item(b, child | WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 130, 77, 40, 12, ID_HISTORY_SIZE, 0x81, L"");
    put_item(b, child, 10, 96, 110, 10, 0xFFFF, 0x82, L"Number of buffers:");
    put_item(b, child | WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 130, 94, 40, 12, ID_HISTORY_BUFFERS, 0x81, L"");
    put_item(b, child | WS_TABSTOP | BS_DEFPUSHBUTTON, 95, 125, 50, 14, IDOK, 0x80, L"OK");
    put_item(b, child | WS_TABSTOP | BS_PUSHBUTTON, 150, 125, 50, 14, IDCANCEL, 0x80, L"Cancel");
    return (DLGTEMPLATE *)b->Data;
}

static void set_number(HWND dialog, int id, int value)
{
    SetDlgItemInt(dialog, id, (UINT)value, FALSE);
}

static INT_PTR CALLBACK dialog_proc(HWND dialog, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(dialog, ID_QUICK_EDIT, g.State.QuickEdit ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, ID_INSERT, g.State.InsertMode ? BST_CHECKED : BST_UNCHECKED);
        set_number(dialog, ID_WIDTH, g.State.Size.X);
        set_number(dialog, ID_HEIGHT, g.State.Size.Y);
        set_number(dialog, ID_HISTORY_SIZE, (int)g.State.HistoryBufferSize);
        set_number(dialog, ID_HISTORY_BUFFERS, (int)g.State.HistoryBuffers);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK) {
            MIME_CONDRV_HOST_SETTINGS s = {};
            BOOL ok;
            s.Set = 2 | 4;
            s.QuickEdit = IsDlgButtonChecked(dialog, ID_QUICK_EDIT) == BST_CHECKED;
            s.Insert = IsDlgButtonChecked(dialog, ID_INSERT) == BST_CHECKED;
            s.HistoryBufferSize = GetDlgItemInt(dialog, ID_HISTORY_SIZE, &ok, FALSE);
            s.HistoryBuffers = GetDlgItemInt(dialog, ID_HISTORY_BUFFERS, &ok, FALSE);
            s.HistoryFlags = g.State.HistoryFlags;
            int width = (int)GetDlgItemInt(dialog, ID_WIDTH, &ok, FALSE);
            int height = (int)GetDlgItemInt(dialog, ID_HEIGHT, &ok, FALSE);
            if (width != g.State.Size.X || height != g.State.Size.Y) {
                s.Set |= 8;
                s.Size.X = (SHORT)width;
                s.Size.Y = (SHORT)height;
            }
            if (!HostControl(IOCTL_CONDRV_HOST_SETTINGS, &s, sizeof(s), nullptr, 0))
                MessageBoxW(dialog, L"The console does not accept these values.", L"Console Window Properties", MB_OK | MB_ICONEXCLAMATION);
            else
                EndDialog(dialog, IDOK);
            HostUpdate();
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void PropertiesShow(HWND owner)
{
    static BYTE storage[2048];
    Builder b = {storage, 0, sizeof(storage)};
    memset(storage, 0, sizeof(storage));
    DLGTEMPLATE *t = build_template(&b);
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr), t, owner, dialog_proc, 0);
}
