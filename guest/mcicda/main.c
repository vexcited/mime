/*
 * mcicda.dll, the cdaudio device. windows refuses to open it without an optical
 * drive, and open cdaudio reports MCIERR_CANNOT_LOAD_DRIVER. this machine has
 * none and disc audio is not supported, so the driver always answers that way.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/mmsystem.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}

DECLSPEC_EXPORT LRESULT WINAPI DriverProc(DWORD_PTR id, HDRVR driver, UINT message, LPARAM param1, LPARAM param2)
{
    switch (message) {
    case DRV_LOAD:
    case DRV_FREE:
    case DRV_ENABLE:
    case DRV_DISABLE:
    case DRV_CLOSE:
        return 1;
    case DRV_OPEN:
        /* an open without mci parameters, as for configuration, works, a device open does not. */
        return param2 ? 0 : 1;
    case DRV_QUERYCONFIGURE:
        return 0;
    case DRV_CONFIGURE:
        return DRVCNF_CANCEL;
    case DRV_INSTALL:
    case DRV_REMOVE:
        return DRVCNF_RESTART;
    }
    return message >= MCI_FIRST && message <= MCI_LAST ? MCIERR_INVALID_DEVICE_ID : 0;
}
