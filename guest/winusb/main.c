/*
 * winusb: no usb device is bound to winusb in mime, so no interface handle
 * ever exists. initializing checks the device handle as windows does and
 * then fails with the error the device's driver gives the winusb request;
 * the calls on interface handles fail with ERROR_INVALID_HANDLE; parsing
 * descriptors is real.
 */
#define WINUSBAPI
#include <mime/win32/winusb.h>
#include <mime/ntapi.h>

#define IOCTL_WINUSB_INITIALIZE 0x0350C068
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/* an overlapped handle that answers the winusb version query. */
BOOL WINAPI WinUsb_Initialize(HANDLE device, PWINUSB_INTERFACE_HANDLE handle)
{
    IO_STATUS_BLOCK iosb;
    FILE_MODE_INFORMATION mode = {0};
    if (!device || device == INVALID_HANDLE_VALUE ||
        !NT_SUCCESS(NtQueryInformationFile(device, &iosb, &mode, sizeof(mode), FileModeInformation)) ||
        (mode.Mode & FILE_SYNCHRONOUS_IO_NONALERT)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    USHORT version = 0x100, answer = 0;
    DWORD returned;
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL ok = DeviceIoControl(device, IOCTL_WINUSB_INITIALIZE, &version, 2, &answer, 2, &returned, &overlapped);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult(device, &overlapped, &returned, TRUE);
    DWORD error = ok ? ERROR_NOT_SUPPORTED : GetLastError();
    CloseHandle(overlapped.hEvent);
    if (handle)
        *handle = nullptr;
    SetLastError(error);
    return FALSE;
}

BOOL WINAPI WinUsb_Free(WINUSB_INTERFACE_HANDLE handle)
{
    return TRUE;
}

BOOL WINAPI WinUsb_ControlTransfer(WINUSB_INTERFACE_HANDLE handle, WINUSB_SETUP_PACKET setup, PUCHAR buffer,
                                   ULONG length, PULONG transferred, LPOVERLAPPED overlapped)
{
    if (transferred)
        *transferred = 0;
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

/* the next descriptor of the type from start on, as long as the lengths stay inside the buffer. */
PUSB_COMMON_DESCRIPTOR WINAPI WinUsb_ParseDescriptors(PVOID buffer, ULONG length, PVOID start, LONG type)
{
    BYTE *end = (BYTE *)buffer + length, *at = start;
    while (at < end && end - at >= 2) {
        if (at[1] == type)
            return (PUSB_COMMON_DESCRIPTOR)at;
        if (!at[0] || at + at[0] < at)
            break;
        at += at[0];
    }
    SetLastError(ERROR_NOT_FOUND);
    return nullptr;
}

/* the first interface descriptor matching every criterion that is not -1. */
PUSB_INTERFACE_DESCRIPTOR WINAPI WinUsb_ParseConfigurationDescriptor(PUSB_CONFIGURATION_DESCRIPTOR config,
                                                                    PVOID start, LONG number, LONG alternate,
                                                                    LONG klass, LONG subclass, LONG protocol)
{
    BYTE *end = (BYTE *)config + config->wTotalLength;
    for (;;) {
        BYTE *found = (BYTE *)WinUsb_ParseDescriptors(config, config->wTotalLength, start,
                                                      USB_INTERFACE_DESCRIPTOR_TYPE);
        if (!found)
            break;
        SIZE_T left = (SIZE_T)(end - found);
        BOOL match = TRUE;
        if (number != -1 && (left < 3 || found[2] != number))
            match = FALSE;
        if (alternate != -1 && (left < 4 || found[3] != alternate))
            match = FALSE;
        if (klass != -1 && (left < 6 || found[5] != klass))
            match = FALSE;
        if (subclass != -1 && (left < 7 || found[6] != subclass))
            match = FALSE;
        if (protocol != -1 && (left < 8 || found[7] != protocol))
            match = FALSE;
        if (match)
            return (PUSB_INTERFACE_DESCRIPTOR)found;
        if (!found[0])
            break;
        start = found + found[0];
    }
    SetLastError(ERROR_NOT_FOUND);
    return nullptr;
}
