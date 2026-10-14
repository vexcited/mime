/*
 * oledlg.dll: the ole user interface dialogs. mime shows none of them yet;
 * each reports that the user cancelled.
 */
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
