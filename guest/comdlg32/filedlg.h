/*
 * the file dialog shared by the common item dialogs and the classic file name functions: a folder tree, the
 * folder's files, a file name, a file type and buttons, plus the rules that turn what was typed or selected into
 * the returned paths.
 */
#ifndef MIME_COMDLG32_FILEDLG_H
#define MIME_COMDLG32_FILEDLG_H

#include "comdlg32_private.h"
#include <mime/win32/shobjidl.h>

typedef struct _FILEDLG FILEDLG;

/* events for the dialog's owner. every callback is optional. */
typedef struct _FILEDLG_EVENTS {
    /* the window exists and is about to be shown. */
    void (*Initialized)(FILEDLG *dialog, void *context);
    /* a file was chosen. FALSE keeps the dialog open. */
    BOOL (*FileOk)(FILEDLG *dialog, void *context);
    BOOL (*FolderChanging)(FILEDLG *dialog, void *context, LPCWSTR folder);
    void (*FolderChange)(FILEDLG *dialog, void *context);
    void (*SelectionChange)(FILEDLG *dialog, void *context);
    void (*TypeChange)(FILEDLG *dialog, void *context);
    /* an existing file is about to be replaced. returns an FDEOR_ value. */
    int (*Overwrite)(FILEDLG *dialog, void *context, LPCWSTR path);
    /* the window is going away, its state can still be read. */
    void (*Closing)(FILEDLG *dialog, void *context);
    /* the owner may handle a window message first. returns TRUE with the result when it did. */
    BOOL (*Message)(FILEDLG *dialog, void *context, UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result);
} FILEDLG_EVENTS;

/* owner controls the dialog itself ignores, laid out above the buttons. */
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
/* the file name box text. free the copy with CoTaskMemFree. */
HRESULT FdGetFileName(FILEDLG *dialog, LPWSTR *text);
HRESULT FdSetFolder(FILEDLG *dialog, LPCWSTR path);
HRESULT FdSetDefaultFolder(FILEDLG *dialog, LPCWSTR path);
HRESULT FdSetClientGuid(FILEDLG *dialog, const GUID *guid);
HRESULT FdAddPlace(FILEDLG *dialog, LPCWSTR path, BOOL top);
/* the folder shown, or the one it will start in. */
BOOL FdGetFolder(FILEDLG *dialog, LPWSTR path);
/* the selected items as paths. free them with FdFreePaths. */
UINT FdGetSelection(FILEDLG *dialog, LPWSTR **paths);
void FdFreePaths(LPWSTR *paths, UINT count);
void FdSetCustomFilter(FILEDLG *dialog, BOOL (*filter)(void *context, LPCWSTR path, BOOL folder), void *context);

HRESULT FdShow(FILEDLG *dialog, HWND owner);
void FdClose(FILEDLG *dialog, HRESULT result);
HWND FdWindow(const FILEDLG *dialog);
/* valid once FdShow succeeded. */
UINT FdResultCount(const FILEDLG *dialog);
LPCWSTR FdResultPath(const FILEDLG *dialog, UINT index);
BOOL FdReadOnlyChecked(const FILEDLG *dialog);
void FdSetReadOnlyBox(FILEDLG *dialog, BOOL shown, BOOL checked);

/* controls added by the owner, with the owner's own ids. */
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
/* events for the owner's controls: 0 button, 1 check, 2 item selected. */
void FdSetControlHandler(FILEDLG *dialog, void (*handler)(FILEDLG *dialog, void *context, int event, DWORD id,
                                                           DWORD item, BOOL checked), void *context);

HRESULT FdDuplicate(LPCWSTR text, LPWSTR *copy);

#endif
