/*
 * the file dialog the common item dialogs and the classic file name
 * functions share: a window with a folder tree, the files of a folder, a file
 * name, a file type and the buttons, and the rules that turn what was typed
 * or selected into the paths it returns.
 */
#ifndef MIME_COMDLG32_FILEDLG_H
#define MIME_COMDLG32_FILEDLG_H

#include "comdlg32_private.h"
#include <mime/win32/shobjidl.h>

typedef struct _FILEDLG FILEDLG;

/* what the owner of a dialog hears from it. every function is optional. */
typedef struct _FILEDLG_EVENTS {
    /* the window exists and is about to be shown. */
    void (*Initialized)(FILEDLG *dialog, void *context);
    /* a file was chosen; FALSE keeps the dialog open. */
    BOOL (*FileOk)(FILEDLG *dialog, void *context);
    BOOL (*FolderChanging)(FILEDLG *dialog, void *context, LPCWSTR folder);
    void (*FolderChange)(FILEDLG *dialog, void *context);
    void (*SelectionChange)(FILEDLG *dialog, void *context);
    void (*TypeChange)(FILEDLG *dialog, void *context);
    /* a file that exists is to be replaced: an FDEOR_ value. */
    int (*Overwrite)(FILEDLG *dialog, void *context, LPCWSTR path);
    /* the window is going away; its state can still be read. */
    void (*Closing)(FILEDLG *dialog, void *context);
    /* a message of the window the owner may handle first; returns TRUE with the result when it did. */
    BOOL (*Message)(FILEDLG *dialog, void *context, UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result);
} FILEDLG_EVENTS;

/* the controls the dialog has no part in, laid out above the buttons. */
typedef enum _FD_CONTROL_KIND {
    FDC_TEXT,
    FDC_CHECK,
    FDC_BUTTON,
    FDC_COMBO,
    FDC_RADIO,
    FDC_EDIT,
    FDC_SEPARATOR,
    FDC_GROUP,
} FD_CONTROL_KIND;

FILEDLG *FdCreate(BOOL save);
void FdFree(FILEDLG *dialog);
void FdSetEvents(FILEDLG *dialog, const FILEDLG_EVENTS *events, void *context);

/* configuration, valid before and while the dialog is up. */
HRESULT FdSetTitle(FILEDLG *dialog, LPCWSTR text);
HRESULT FdSetOkLabel(FILEDLG *dialog, LPCWSTR text);
HRESULT FdSetCancelLabel(FILEDLG *dialog, LPCWSTR text);
HRESULT FdSetNameLabel(FILEDLG *dialog, LPCWSTR text);
HRESULT FdSetDefaultExtension(FILEDLG *dialog, LPCWSTR text);
HRESULT FdSetTypes(FILEDLG *dialog, UINT count, const COMDLG_FILTERSPEC *types);
UINT FdTypeCount(const FILEDLG *dialog);
void FdSetTypeIndex(FILEDLG *dialog, UINT index);
UINT FdTypeIndex(const FILEDLG *dialog);
void FdSetOptions(FILEDLG *dialog, DWORD options);
DWORD FdOptions(const FILEDLG *dialog);
BOOL FdIsSave(const FILEDLG *dialog);
BOOL FdIsShowing(const FILEDLG *dialog);
HRESULT FdSetFileName(FILEDLG *dialog, LPCWSTR text);
/* the text of the file name box, a copy for the caller to free with CoTaskMemFree. */
HRESULT FdGetFileName(FILEDLG *dialog, LPWSTR *text);
HRESULT FdSetFolder(FILEDLG *dialog, LPCWSTR path);
HRESULT FdSetDefaultFolder(FILEDLG *dialog, LPCWSTR path);
HRESULT FdSetClientGuid(FILEDLG *dialog, const GUID *guid);
HRESULT FdAddPlace(FILEDLG *dialog, LPCWSTR path, BOOL top);
/* the folder shown, or the one it will start in. */
BOOL FdGetFolder(FILEDLG *dialog, LPWSTR path);
/* the items selected in the list, as paths for the caller to free with FdFreePaths. */
UINT FdGetSelection(FILEDLG *dialog, LPWSTR **paths);
void FdFreePaths(LPWSTR *paths, UINT count);
void FdSetCustomFilter(FILEDLG *dialog, BOOL (*filter)(void *context, LPCWSTR path, BOOL folder), void *context);

/* running it. */
HRESULT FdShow(FILEDLG *dialog, HWND owner);
void FdClose(FILEDLG *dialog, HRESULT result);
HWND FdWindow(const FILEDLG *dialog);
/* the paths chosen, after FdShow returned success. */
UINT FdResultCount(const FILEDLG *dialog);
LPCWSTR FdResultPath(const FILEDLG *dialog, UINT index);
/* whether the read only box was checked, and the extension was one the filter did not name. */
BOOL FdReadOnlyChecked(const FILEDLG *dialog);
void FdSetReadOnlyBox(FILEDLG *dialog, BOOL shown, BOOL checked);

/* controls added by the owner; the identifier is the owner's own. */
HRESULT FdAddControl(FILEDLG *dialog, FD_CONTROL_KIND kind, DWORD id, LPCWSTR text);
HRESULT FdAddControlItem(FILEDLG *dialog, DWORD id, DWORD item, LPCWSTR text);
HRESULT FdRemoveControlItem(FILEDLG *dialog, DWORD id, DWORD item, BOOL all);
HRESULT FdSetControlText(FILEDLG *dialog, DWORD id, DWORD item, LPCWSTR text, BOOL label);
HRESULT FdGetControlText(FILEDLG *dialog, DWORD id, LPWSTR *text);
HRESULT FdSetControlState(FILEDLG *dialog, DWORD id, int state);
HRESULT FdGetControlState(FILEDLG *dialog, DWORD id, int *state);
HRESULT FdSetControlChecked(FILEDLG *dialog, DWORD id, BOOL checked);
HRESULT FdGetControlChecked(FILEDLG *dialog, DWORD id, BOOL *checked);
HRESULT FdSetControlSelection(FILEDLG *dialog, DWORD id, DWORD item);
HRESULT FdGetControlSelection(FILEDLG *dialog, DWORD id, DWORD *item);
/* hears about the owner's controls: kind of event 0 button, 1 check, 2 item selected. */
void FdSetControlHandler(FILEDLG *dialog, void (*handler)(FILEDLG *dialog, void *context, int event, DWORD id,
                                                           DWORD item, BOOL checked), void *context);

/* shared text helpers. */
HRESULT FdDuplicate(LPCWSTR text, LPWSTR *copy);

#endif
