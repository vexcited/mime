#include "mscms_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved) { return TRUE; }

HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

HRESULT WINAPI DllGetClassObject(const GUID *clsid, const GUID *iid, void **out)
{
    if (out)
        *out = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* postscript color spaces and rendering dictionaries are for printer drivers, which mime does not have. */
BOOL WINAPI GetPS2ColorSpaceArray(HANDLE profile, DWORD intent, DWORD type, BYTE *buffer, DWORD *size, BOOL *binary)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI GetPS2ColorRenderingIntent(HANDLE profile, DWORD intent, BYTE *buffer, DWORD *size)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI GetPS2ColorRenderingDictionary(HANDLE profile, DWORD intent, BYTE *buffer, DWORD *size, BOOL *binary)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/* the spooler tells about renamed printers and copied driver files: no profile follows them here. */
BOOL WINAPI DeviceRenameEvent(const WCHAR *machine, const WCHAR *old_name, const WCHAR *new_name) { return TRUE; }

DWORD WINAPI SpoolerCopyFileEvent(const WCHAR *printer, const WCHAR *key, DWORD event) { return TRUE; }

DWORD WINAPI GenerateCopyFilePaths(const WCHAR *printer, const WCHAR *directory, BYTE *client_info, DWORD level,
                                   WCHAR *source, DWORD *source_size, WCHAR *target, DWORD *target_size, DWORD flags)
{
    return ERROR_SUCCESS;
}
