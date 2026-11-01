/*
 * the call the shell makes to start a program from a descriptor of the installer. the descriptor names a feature and
 * a component of a product, which the installer checks, installs or repairs before it answers with the command line.
 */
#include "advapi32_private.h"

#define INSTALLUILEVEL_NOCHANGE 0
#define INSTALLUILEVEL_NONE 2

typedef INT(WINAPI *MSI_SET_INTERNAL_UI)(INT level, HWND *window);
typedef UINT(WINAPI *MSI_DECOMPOSE_DESCRIPTOR)(LPCWSTR descriptor, LPWSTR product, LPWSTR feature, LPWSTR component,
                                               LPDWORD used);
typedef UINT(WINAPI *MSI_PROVIDE_FROM_DESCRIPTOR)(LPCWSTR descriptor, LPWSTR path, LPDWORD size, LPDWORD args);
typedef HANDLE(WINAPI *GET_PROCESS_WINDOW_STATION)(void);
typedef BOOL(WINAPI *GET_USER_OBJECT_INFORMATION)(HANDLE object, int index, PVOID info, DWORD length, LPDWORD needed);

/* a desktop session is the one of the window station WinSta0, anything else has nobody to ask. */
static BOOL interactive_station(DWORD *error)
{
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    GET_PROCESS_WINDOW_STATION get_station =
        user32 ? (GET_PROCESS_WINDOW_STATION)(void *)GetProcAddress(user32, "GetProcessWindowStation") : nullptr;
    GET_USER_OBJECT_INFORMATION get_name =
        user32 ? (GET_USER_OBJECT_INFORMATION)(void *)GetProcAddress(user32, "GetUserObjectInformationW") : nullptr;
    if (!get_station || !get_name) {
        *error = ERROR_OUTOFMEMORY;
        return FALSE;
    }
    WCHAR name[64] = L"";
    HANDLE station = get_station();
    if (station)
        get_name(station, 2, name, sizeof(name), nullptr);
    *error = ERROR_SUCCESS;
    return !wcscmp(name, L"WinSta0");
}

ADVAPI DWORD WINAPI CommandLineFromMsiDescriptor(LPWSTR descriptor, LPWSTR command_line, DWORD *length)
{
    HMODULE msi = LoadLibraryW(L"msi.dll");
    if (!msi)
        return GetLastError();
    MSI_SET_INTERNAL_UI set_ui = (MSI_SET_INTERNAL_UI)(void *)GetProcAddress(msi, "MsiSetInternalUI");
    MSI_DECOMPOSE_DESCRIPTOR decompose =
        (MSI_DECOMPOSE_DESCRIPTOR)(void *)GetProcAddress(msi, "MsiDecomposeDescriptorW");
    MSI_PROVIDE_FROM_DESCRIPTOR provide =
        (MSI_PROVIDE_FROM_DESCRIPTOR)(void *)GetProcAddress(msi, "MsiProvideComponentFromDescriptorW");
    if (!set_ui || !decompose || !provide)
        return ERROR_PROC_NOT_FOUND;
    DWORD error;
    BOOL interactive = interactive_station(&error);
    if (error)
        return error;
    if (!interactive)
        set_ui(INSTALLUILEVEL_NONE, nullptr);
    INT level = set_ui(INSTALLUILEVEL_NOCHANGE, nullptr);
    if (level != INSTALLUILEVEL_NONE) {
        /* the policy of the user for the product sets how much the installer shows. */
        WCHAR product[39];
        error = decompose(descriptor, product, nullptr, nullptr, nullptr);
        if (error)
            return error;
        HKEY key;
        DWORD policy, size = sizeof(policy);
        if (!RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Group Policy\\Appmgmt",
                           0, KEY_READ, &key)) {
            LSTATUS status = RegQueryValueExW(key, product, nullptr, nullptr, (BYTE *)&policy, &size);
            RegCloseKey(key);
            if (!status)
                set_ui((INT)policy, nullptr);
        }
    }
    error = provide(descriptor, command_line, length, nullptr);
    /* a reboot that is under way or due, or an installation that was suspended, is no failure of the command line. */
    if (error == 1641 || error == 3010 || error == 1604)
        error = ERROR_SUCCESS;
    set_ui(level, nullptr);
    return error;
}
