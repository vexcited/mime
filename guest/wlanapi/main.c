/*
 * wlanapi.dll: the wireless LAN api. the service that answers it, WLAN AutoConfig, is not running, which is the answer
 * of a machine with no wireless adapter: opening a client handle fails because the service is not active, and any
 * other handle is not valid.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

typedef ULONG_PTR ARG;

#define ERROR_SERVICE_NOT_ACTIVE_ 1062

DWORD WINAPI WlanOpenHandle(DWORD client_version, PVOID reserved, PDWORD negotiated, PHANDLE client)
{
    if (!negotiated || !client || reserved)
        return ERROR_INVALID_PARAMETER;
    if (client_version != 1 && client_version != 2)
        return ERROR_NOT_SUPPORTED;
    *negotiated = 0;
    *client = nullptr;
    return ERROR_SERVICE_NOT_ACTIVE_;
}

DWORD WINAPI WlanCloseHandle(HANDLE client, PVOID reserved)
{
    return client && !reserved ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER;
}

PVOID WINAPI WlanAllocateMemory(DWORD size)
{
    return size ? HeapAlloc(GetProcessHeap(), 0, size) : nullptr;
}

VOID WINAPI WlanFreeMemory(PVOID memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

/* a call on a handle gives the error of a handle that was never opened. */
static DWORD invalid(HANDLE client)
{
    return client ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI WlanEnumInterfaces(HANDLE client, PVOID reserved, PVOID *list)
{
    if (list)
        *list = nullptr;
    return !list || reserved ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanQueryInterface(HANDLE client, const GUID *interface_guid, ARG op_code, PVOID reserved, PDWORD size,
                                PVOID *data, ARG value_type)
{
    return !interface_guid || reserved || !size || !data ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanScan(HANDLE client, const GUID *interface_guid, PVOID ssid, PVOID raw_data, PVOID reserved)
{
    return !interface_guid || reserved ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanGetNetworkBssList(HANDLE client, const GUID *interface_guid, PVOID ssid, ARG bss_type, BOOL security,
                                   PVOID reserved, PVOID *list)
{
    if (list)
        *list = nullptr;
    return !interface_guid || reserved || !list ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanGetAvailableNetworkList(HANDLE client, const GUID *interface_guid, DWORD flags, PVOID reserved,
                                         PVOID *list)
{
    if (list)
        *list = nullptr;
    return !interface_guid || reserved || !list ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanGetProfileList(HANDLE client, const GUID *interface_guid, PVOID reserved, PVOID *list)
{
    if (list)
        *list = nullptr;
    return !interface_guid || reserved || !list ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanGetProfile(HANDLE client, const GUID *interface_guid, PCWSTR name, PVOID reserved, PWSTR *xml,
                            PDWORD flags, PDWORD access)
{
    if (xml)
        *xml = nullptr;
    return !interface_guid || !name || reserved || !xml ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanSetProfile(HANDLE client, const GUID *interface_guid, DWORD flags, PCWSTR xml, PCWSTR security, BOOL overwrite,
                            PVOID reserved, PDWORD reason)
{
    return !interface_guid || !xml || reserved || !reason ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanDeleteProfile(HANDLE client, const GUID *interface_guid, PCWSTR name, PVOID reserved)
{
    return !interface_guid || !name || reserved ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanConnect(HANDLE client, const GUID *interface_guid, PVOID parameters, PVOID reserved)
{
    return !interface_guid || !parameters || reserved ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanDisconnect(HANDLE client, const GUID *interface_guid, PVOID reserved)
{
    return !interface_guid || reserved ? ERROR_INVALID_PARAMETER : invalid(client);
}

DWORD WINAPI WlanRegisterNotification(HANDLE client, DWORD sources, BOOL ignore_duplicates, PVOID callback, PVOID context,
                                      PVOID reserved, PDWORD previous_sources)
{
    return reserved ? ERROR_INVALID_PARAMETER : invalid(client);
}
