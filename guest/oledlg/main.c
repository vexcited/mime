/*
 * oledlg.dll: the OLE user interface dialogs. none is shown yet, each reports that the user cancelled. exports are
 * listed in oledlg.def.
 */
#define OLEDLGAPI
#include <mime/win32/oledlg.h>
#include <mime/win32/winuser.h>
#include <mime/stub.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/* each of these is told to the user as cancelled, once the structure is there. */
static UINT cancelled(const void *dialog)
{
    MIME_STUB("oledlg");
    return dialog ? OLEUI_CANCEL : OLEUI_ERR_STRUCTURENULL;
}

OLEDLGAPI UINT WINAPI OleUIBusyW(LPOLEUIBUSYW busy)
{
    return cancelled(busy);
}

OLEDLGAPI UINT WINAPI OleUIBusyA(void *busy)
{
    return cancelled(busy);
}

OLEDLGAPI UINT WINAPI OleUIInsertObjectW(void *insert)
{
    return cancelled(insert);
}

OLEDLGAPI UINT WINAPI OleUIInsertObjectA(void *insert)
{
    return cancelled(insert);
}

OLEDLGAPI UINT WINAPI OleUIPasteSpecialW(void *paste)
{
    return cancelled(paste);
}

OLEDLGAPI UINT WINAPI OleUIPasteSpecialA(void *paste)
{
    return cancelled(paste);
}

OLEDLGAPI UINT WINAPI OleUIObjectPropertiesW(void *properties)
{
    return cancelled(properties);
}

OLEDLGAPI UINT WINAPI OleUIObjectPropertiesA(void *properties)
{
    return cancelled(properties);
}

OLEDLGAPI UINT WINAPI OleUIConvertW(void *convert)
{
    return cancelled(convert);
}

OLEDLGAPI UINT WINAPI OleUIConvertA(void *convert)
{
    return cancelled(convert);
}

OLEDLGAPI UINT WINAPI OleUIChangeIconW(void *change)
{
    return cancelled(change);
}

OLEDLGAPI UINT WINAPI OleUIChangeIconA(void *change)
{
    return cancelled(change);
}

OLEDLGAPI UINT WINAPI OleUIChangeSourceW(void *change)
{
    return cancelled(change);
}

OLEDLGAPI UINT WINAPI OleUIChangeSourceA(void *change)
{
    return cancelled(change);
}

OLEDLGAPI UINT WINAPI OleUIEditLinksW(void *edit)
{
    return cancelled(edit);
}

OLEDLGAPI UINT WINAPI OleUIEditLinksA(void *edit)
{
    return cancelled(edit);
}

/* the links are not updated without the dialog that does it, which is the answer for a dialog that failed. */
OLEDLGAPI BOOL WINAPI OleUIUpdateLinksW(void *container, HWND parent, LPWSTR title, int links)
{
    MIME_STUB("oledlg");
    return FALSE;
}

OLEDLGAPI BOOL WINAPI OleUIUpdateLinksA(void *container, HWND parent, LPSTR title, int links)
{
    MIME_STUB("oledlg");
    return FALSE;
}
