/* raw input and the cursor's confinement, kept by win32k. */
#include "user32_private.h"

WINUSERAPI BOOL WINAPI RegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT count, UINT size)
{
    return NtUserRegisterRawInputDevices(devices, count, size);
}

WINUSERAPI UINT WINAPI GetRegisteredRawInputDevices(PRAWINPUTDEVICE devices, PUINT count, UINT size)
{
    return NtUserGetRegisteredRawInputDevices(devices, count, size);
}

WINUSERAPI UINT WINAPI GetRawInputData(HRAWINPUT input, UINT command, LPVOID data, PUINT size, UINT header_size)
{
    return NtUserGetRawInputData(input, command, data, size, header_size);
}

WINUSERAPI UINT WINAPI GetRawInputBuffer(PRAWINPUT data, PUINT size, UINT header_size)
{
    return NtUserGetRawInputBuffer(data, size, header_size);
}

WINUSERAPI UINT WINAPI GetRawInputDeviceList(PRAWINPUTDEVICELIST list, PUINT count, UINT size)
{
    return NtUserGetRawInputDeviceList(list, count, size);
}

WINUSERAPI UINT WINAPI GetRawInputDeviceInfoW(HANDLE device, UINT command, LPVOID data, PUINT size)
{
    return NtUserGetRawInputDeviceInfo(device, command, data, size);
}

/* the device name comes back in the ansi code page with its size in bytes, other commands go to the wide version. */
WINUSERAPI UINT WINAPI GetRawInputDeviceInfoA(HANDLE device, UINT command, LPVOID data, PUINT size)
{
    if (command != RIDI_DEVICENAME)
        return NtUserGetRawInputDeviceInfo(device, command, data, size);
    if (!size) {
        SetLastError(ERROR_NOACCESS);
        return (UINT)-1;
    }
    WCHAR name[256];
    UINT chars = RTL_NUMBER_OF(name);
    UINT r = NtUserGetRawInputDeviceInfo(device, command, name, &chars);
    if (r == (UINT)-1)
        return r;
    int bytes = WideCharToMultiByte(CP_ACP, 0, name, (int)r, nullptr, 0, nullptr, nullptr);
    if (!data || *size < (UINT)bytes) {
        *size = (UINT)bytes;
        if (!data)
            return 0;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return (UINT)-1;
    }
    return (UINT)WideCharToMultiByte(CP_ACP, 0, name, (int)r, data, bytes, nullptr, nullptr);
}

/* the packets need no cleanup: win32k reuses them in turn. */
WINUSERAPI LRESULT WINAPI DefRawInputProc(PRAWINPUT *input, INT count, UINT header_size)
{
    return header_size == sizeof(RAWINPUTHEADER) ? 0 : -1;
}

WINUSERAPI BOOL WINAPI ClipCursor(const RECT *rect)
{
    return NtUserClipCursor(rect);
}

WINUSERAPI BOOL WINAPI GetClipCursor(LPRECT rect)
{
    return NtUserGetClipCursor(rect);
}
