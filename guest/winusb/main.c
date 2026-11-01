/*
 * no USB device is bound to winusb, so no interface handles exist. initialization validates the device handle then
 * returns the error of its driver, and interface calls fail as windows does on a handle it cannot use. descriptor
 * parsing and the frame number adjustment are real.
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

/* finds the next descriptor of the requested type, checking each length against the buffer. */
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

/*
 * no interface handle is ever made, so every handle is invalid. windows reads the handle without a check on most
 * calls and faults on garbage, here a handle that is not null is reported like a null one.
 */

typedef struct {
    UCHAR PipeType;
    UCHAR PipeId;
    USHORT MaximumPacketSize;
    UCHAR Interval;
} PIPE_INFORMATION;

typedef struct {
    ULONG PipeType;
    UCHAR PipeId;
    USHORT MaximumPacketSize;
    UCHAR Interval;
    ULONG MaximumBytesPerInterval;
} PIPE_INFORMATION_EX;

static BOOL invalid_handle(void)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

static BOOL invalid_parameter(void)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL WINAPI WinUsb_AbortPipe(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_AbortPipeAsync(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe, LPOVERLAPPED overlapped)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_FlushPipe(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_ResetPipe(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_ResetPipeAsync(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe, LPOVERLAPPED overlapped)
{
    return invalid_handle();
}

/* this one needs no device, it adds the time since the stamp in milliseconds to the frame number. */
BOOL WINAPI WinUsb_GetAdjustedFrameNumber(PULONG frame, LARGE_INTEGER stamp)
{
    LARGE_INTEGER count = {0}, frequency = {0};
    if (!frame)
        return invalid_parameter();
    if (!QueryPerformanceCounter(&count) || !QueryPerformanceFrequency(&frequency))
        return FALSE;
    LONGLONG elapsed = 1000 * (count.QuadPart - stamp.QuadPart) / frequency.QuadPart;
    if (elapsed > 1000)
        return invalid_parameter();
    *frame += (ULONG)elapsed;
    return TRUE;
}

BOOL WINAPI WinUsb_GetAssociatedInterface(WINUSB_INTERFACE_HANDLE handle, UCHAR index,
                                          PWINUSB_INTERFACE_HANDLE associated)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_GetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE handle, PUCHAR setting)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_GetCurrentFrameNumber(WINUSB_INTERFACE_HANDLE handle, PULONG frame, LARGE_INTEGER *stamp)
{
    if (!frame || !stamp)
        return invalid_parameter();
    return invalid_handle();
}

BOOL WINAPI WinUsb_GetCurrentFrameNumberAndQpc(WINUSB_INTERFACE_HANDLE handle, PVOID info)
{
    if (!handle || !info)
        return invalid_parameter();
    return invalid_handle();
}

BOOL WINAPI WinUsb_StartTrackingForTimeSync(WINUSB_INTERFACE_HANDLE handle, PVOID info)
{
    if (!handle || !info)
        return invalid_parameter();
    return invalid_handle();
}

BOOL WINAPI WinUsb_StopTrackingForTimeSync(WINUSB_INTERFACE_HANDLE handle, PVOID info)
{
    if (!handle || !info)
        return invalid_parameter();
    return invalid_handle();
}

BOOL WINAPI WinUsb_GetDescriptor(WINUSB_INTERFACE_HANDLE handle, UCHAR type, UCHAR index, USHORT language,
                                 PUCHAR buffer, ULONG length, PULONG transferred)
{
    if (transferred)
        *transferred = 0;
    return invalid_handle();
}

BOOL WINAPI WinUsb_GetOverlappedResult(WINUSB_INTERFACE_HANDLE handle, LPOVERLAPPED overlapped, LPDWORD transferred,
                                       BOOL wait)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_GetPipePolicy(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe, ULONG policy, PULONG length, PVOID value)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_SetPipePolicy(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe, ULONG policy, ULONG length, PVOID value)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_GetPowerPolicy(WINUSB_INTERFACE_HANDLE handle, ULONG policy, PULONG length, PVOID value)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_SetPowerPolicy(WINUSB_INTERFACE_HANDLE handle, ULONG policy, ULONG length, PVOID value)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_QueryDeviceInformation(WINUSB_INTERFACE_HANDLE handle, ULONG type, PULONG length, PVOID buffer)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_QueryInterfaceSettings(WINUSB_INTERFACE_HANDLE handle, UCHAR alternate,
                                          PUSB_INTERFACE_DESCRIPTOR descriptor)
{
    return invalid_handle();
}

/* the information pointer is checked after the handle. */
BOOL WINAPI WinUsb_QueryPipe(WINUSB_INTERFACE_HANDLE handle, UCHAR alternate, UCHAR index, PIPE_INFORMATION *info)
{
    if (handle && !info)
        return invalid_parameter();
    return invalid_handle();
}

BOOL WINAPI WinUsb_QueryPipeEx(WINUSB_INTERFACE_HANDLE handle, UCHAR alternate, UCHAR index,
                               PIPE_INFORMATION_EX *info)
{
    if (handle && !info)
        return invalid_parameter();
    return invalid_handle();
}

BOOL WINAPI WinUsb_ReadPipe(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe, PUCHAR buffer, ULONG length,
                            PULONG transferred, LPOVERLAPPED overlapped)
{
    if (transferred)
        *transferred = 0;
    return invalid_handle();
}

BOOL WINAPI WinUsb_WritePipe(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe, PUCHAR buffer, ULONG length,
                             PULONG transferred, LPOVERLAPPED overlapped)
{
    if (transferred)
        *transferred = 0;
    return invalid_handle();
}

BOOL WINAPI WinUsb_SetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE handle, UCHAR setting)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_SetCurrentAlternateSettingAsync(WINUSB_INTERFACE_HANDLE handle, UCHAR setting,
                                                   LPOVERLAPPED overlapped)
{
    return invalid_handle();
}

/* the isochronous buffer handle is checked to point to itself, which no handle here does. */
BOOL WINAPI WinUsb_RegisterIsochBuffer(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe, PUCHAR buffer, ULONG length,
                                       PVOID *isoch)
{
    *isoch = nullptr;
    if (!length || (ULONG_PTR)buffer + length < (ULONG_PTR)buffer) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    return invalid_handle();
}

BOOL WINAPI WinUsb_UnregisterIsochBuffer(PVOID isoch)
{
    return invalid_handle();
}

BOOL WINAPI WinUsb_ReadIsochPipe(PVOID isoch, ULONG offset, ULONG length, PULONG frame, ULONG packets,
                                 PVOID descriptors, LPOVERLAPPED overlapped)
{
    return invalid_parameter();
}

BOOL WINAPI WinUsb_ReadIsochPipeAsap(PVOID isoch, ULONG offset, ULONG length, BOOL continue_stream, ULONG packets,
                                     PVOID descriptors, LPOVERLAPPED overlapped)
{
    return invalid_parameter();
}

BOOL WINAPI WinUsb_WriteIsochPipe(PVOID isoch, ULONG offset, ULONG length, PULONG frame, LPOVERLAPPED overlapped)
{
    if (!length) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    return invalid_parameter();
}

BOOL WINAPI WinUsb_WriteIsochPipeAsap(PVOID isoch, ULONG offset, ULONG length, BOOL continue_stream,
                                      LPOVERLAPPED overlapped)
{
    if (!length) {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    return invalid_parameter();
}
