/* oledlg.dll: the OLE user interface dialogs. none is shown yet, each reports that the user cancelled. */
#define OLEDLGAPI DECLSPEC_EXPORT
#include <mime/win32/oledlg.h>
#include <mime/stub.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

OLEDLGAPI UINT WINAPI OleUIBusyW(LPOLEUIBUSYW busy)
{
    MIME_STUB("oledlg");
    if (!busy)
        return OLEUI_ERR_STRUCTURENULL;
    return OLEUI_CANCEL;
}

/* each of these is told to the user as cancelled, once the structure is there. */
static UINT cancelled(const void *dialog)
{
    MIME_STUB("oledlg");
    return dialog ? OLEUI_CANCEL : OLEUI_ERR_STRUCTURENULL;
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
