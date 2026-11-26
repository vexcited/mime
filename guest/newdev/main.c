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

#define INSTALLFLAG_BITS 0x00000007

/* whether a present device names the id among its hardware or compatible ids. */
static BOOL device_with_id(PCWSTR id)
{
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL found = FALSE;
    SP_DEVINFO_DATA device = {sizeof(device)};
    for (DWORD i = 0; !found && SetupDiEnumDeviceInfo(set, i, &device); i++) {
        static const DWORD lists[] = {SPDRP_HARDWAREID, SPDRP_COMPATIBLEIDS};
        for (DWORD l = 0; !found && l < RTL_NUMBER_OF(lists); l++) {
            WCHAR ids[1024];
            if (!SetupDiGetDeviceRegistryPropertyW(set, &device, lists[l], nullptr, (PBYTE)ids, sizeof(ids) - 2 * sizeof(WCHAR),
                                                   nullptr))
                continue;
            ids[RTL_NUMBER_OF(ids) - 2] = ids[RTL_NUMBER_OF(ids) - 1] = 0;
            for (PCWSTR at = ids; *at && !found; at += lstrlenW(at) + 1)
                found = !lstrcmpiW(at, id);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

/*
 * measured on windows 11 26100 with api.c of scratch/pr17 in the research repository: a 32-bit caller gets
 * ERROR_IN_WOW64 before anything is looked at, a flag that is none ERROR_INVALID_FLAGS, a missing or empty id or no inf
 * ERROR_INVALID_PARAMETER, all three leaving the reboot answer alone, and an inf that is not there ERROR_FILE_NOT_FOUND.
 * no present device with the id is ERROR_NO_SUCH_DEVINST with the reboot answer cleared. a device that has the id gets
 * no driver: no driver package installs here, which windows reports as a driver no better than the current one.
 */
BOOL WINAPI UpdateDriverForPlugAndPlayDevicesW(HWND parent, PCWSTR id, PCWSTR inf, DWORD flags, PBOOL reboot)
{
    BOOL wow64 = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &wow64) && wow64) {
        SetLastError(ERROR_IN_WOW64_);
        return FALSE;
    }
    if (flags & ~INSTALLFLAG_BITS) {
        SetLastError(ERROR_INVALID_FLAGS_);
        return FALSE;
    }
    if (!id || !id[0] || !inf) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (GetFileAttributesW(inf) == INVALID_FILE_ATTRIBUTES) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    if (reboot)
        *reboot = FALSE;
    SetLastError(device_with_id(id) ? ERROR_NO_MORE_ITEMS : ERROR_NO_SUCH_DEVINST);
    return FALSE;
}

BOOL WINAPI UpdateDriverForPlugAndPlayDevicesA(HWND parent, PCSTR id, PCSTR inf, DWORD flags, PBOOL reboot)
{
    WCHAR wide_id[MAX_DEVICE_ID_LEN], wide_inf[MAX_PATH];
    if (id && !MultiByteToWideChar(CP_ACP, 0, id, -1, wide_id, RTL_NUMBER_OF(wide_id)))
        wide_id[0] = 0;
    if (inf && !MultiByteToWideChar(CP_ACP, 0, inf, -1, wide_inf, RTL_NUMBER_OF(wide_inf)))
        wide_inf[0] = 0;
    return UpdateDriverForPlugAndPlayDevicesW(parent, id ? wide_id : nullptr, inf ? wide_inf : nullptr, flags, reboot);
}
