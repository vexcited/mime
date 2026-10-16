/*
 * setupapi: device information sets. mime has no devices to install or
 * enumerate yet, so every set is empty: programs that look for game
 * controllers or other hardware this way find none, as on a machine
 * without them.
 */
#define MIME_BUILD_SETUPAPI
#include <mime/win32/setupapi.h>

#define SET_MAGIC 0x53455444 /* 'DTES' */

typedef struct DEVICE_SET {
    DWORD Magic;
    GUID Class;
    DWORD Flags;
} DEVICE_SET;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

static DEVICE_SET *set_of(HDEVINFO h)
{
    DEVICE_SET *s = h;
    if (!s || h == INVALID_HANDLE_VALUE || s->Magic != SET_MAGIC) {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    return s;
}

WINSETUPAPI HDEVINFO WINAPI SetupDiGetClassDevsW(const GUID *guid, PCWSTR enumerator, HWND parent, DWORD flags)
{
    if (!guid && !(flags & DIGCF_ALLCLASSES)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    DEVICE_SET *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    s->Magic = SET_MAGIC;
    if (guid)
        s->Class = *guid;
    s->Flags = flags;
    return s;
}

WINSETUPAPI HDEVINFO WINAPI SetupDiGetClassDevsA(const GUID *guid, PCSTR enumerator, HWND parent, DWORD flags)
{
    return SetupDiGetClassDevsW(guid, NULL, parent, flags);
}

WINSETUPAPI BOOL WINAPI SetupDiDestroyDeviceInfoList(HDEVINFO set)
{
    DEVICE_SET *s = set_of(set);
    if (!s)
        return FALSE;
    s->Magic = 0;
    HeapFree(GetProcessHeap(), 0, s);
    return TRUE;
}

WINSETUPAPI BOOL WINAPI SetupDiEnumDeviceInfo(HDEVINFO set, DWORD index, PSP_DEVINFO_DATA data)
{
    if (!set_of(set))
        return FALSE;
    if (!data || data->cbSize != sizeof(*data)) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    SetLastError(ERROR_NO_MORE_ITEMS);
    return FALSE;
}

WINSETUPAPI BOOL WINAPI SetupDiEnumDeviceInterfaces(HDEVINFO set, PSP_DEVINFO_DATA device, const GUID *guid,
                                                    DWORD index, PSP_DEVICE_INTERFACE_DATA data)
{
    if (!set_of(set))
        return FALSE;
    if (!guid || !data || data->cbSize != sizeof(*data)) {
        SetLastError(!guid ? ERROR_INVALID_PARAMETER : ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    SetLastError(ERROR_NO_MORE_ITEMS);
    return FALSE;
}

/* no interface was handed out, so whichever is asked about is not one of the set's. */
WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                         PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail, DWORD size,
                                                         PDWORD required, PSP_DEVINFO_DATA device)
{
    if (set_of(set))
        SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceDetailA(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                         PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail, DWORD size,
                                                         PDWORD required, PSP_DEVINFO_DATA device)
{
    if (set_of(set))
        SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINSETUPAPI BOOL WINAPI SetupDiGetDeviceRegistryPropertyW(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                          PDWORD type, PBYTE buffer, DWORD size, PDWORD required)
{
    if (set_of(set))
        SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINSETUPAPI BOOL WINAPI SetupDiGetDeviceRegistryPropertyA(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                          PDWORD type, PBYTE buffer, DWORD size, PDWORD required)
{
    if (set_of(set))
        SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}
