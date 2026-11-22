#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/setupapi.h>

WINBASEAPI BOOL WINAPI IsUserAnAdmin(void);

#define DIF_REMOVE_ 0x00000005
#define ERROR_INVALID_FLAGS_ 1004
#define ERROR_IN_WOW64_ 0xE0000235

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/*
 * measured on windows 11 26100 with dm.c of scratch/g2/bth, the same in both widths: any flag is
 * ERROR_INVALID_FLAGS and leaves the reboot answer alone, which is then cleared, a set that is none is
 * ERROR_INVALID_HANDLE and a device that is not of the set ERROR_INVALID_PARAMETER. what follows is documented and
 * not measured, as it needs a device to remove: a caller that is no administrator gets ERROR_ACCESS_DENIED, a 32-bit
 * one on a 64-bit system ERROR_IN_WOW64, and the device goes through the class installer's DIF_REMOVE.
 */
BOOL WINAPI DiUninstallDevice(HWND parent, HDEVINFO set, PSP_DEVINFO_DATA device, DWORD flags, PBOOL reboot)
{
    if (flags) {
        SetLastError(ERROR_INVALID_FLAGS_);
        return FALSE;
    }
    if (reboot)
        *reboot = FALSE;
    WCHAR id[400];
    if (!set || set == INVALID_HANDLE_VALUE) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!device || !SetupDiGetDeviceInstanceIdW(set, device, id, RTL_NUMBER_OF(id), nullptr)) {
        SetLastError(device && GetLastError() == ERROR_INVALID_HANDLE ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!IsUserAnAdmin()) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    BOOL wow64 = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &wow64) && wow64) {
        SetLastError(ERROR_IN_WOW64_);
        return FALSE;
    }
    return SetupDiCallClassInstaller(DIF_REMOVE_, set, device);
}
