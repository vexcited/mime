/* the winusb user library, winusb.h and usb descriptor layouts. */
#ifndef MIME_WIN32_WINUSB_H
#define MIME_WIN32_WINUSB_H

#include <mime/win32/winbase.h>

#ifndef WINUSBAPI
#define WINUSBAPI DECLSPEC_IMPORT
#endif

typedef PVOID WINUSB_INTERFACE_HANDLE, *PWINUSB_INTERFACE_HANDLE;

#pragma pack(push, 1)
typedef struct _WINUSB_SETUP_PACKET {
    UCHAR RequestType;
    UCHAR Request;
    USHORT Value;
    USHORT Index;
    USHORT Length;
} WINUSB_SETUP_PACKET, *PWINUSB_SETUP_PACKET;

typedef struct _USB_COMMON_DESCRIPTOR {
    UCHAR bLength;
    UCHAR bDescriptorType;
} USB_COMMON_DESCRIPTOR, *PUSB_COMMON_DESCRIPTOR;

typedef struct _USB_CONFIGURATION_DESCRIPTOR {
    UCHAR bLength;
    UCHAR bDescriptorType;
    USHORT wTotalLength;
    UCHAR bNumInterfaces;
    UCHAR bConfigurationValue;
    UCHAR iConfiguration;
    UCHAR bmAttributes;
    UCHAR MaxPower;
} USB_CONFIGURATION_DESCRIPTOR, *PUSB_CONFIGURATION_DESCRIPTOR;

typedef struct _USB_INTERFACE_DESCRIPTOR {
    UCHAR bLength;
    UCHAR bDescriptorType;
    UCHAR bInterfaceNumber;
    UCHAR bAlternateSetting;
    UCHAR bNumEndpoints;
    UCHAR bInterfaceClass;
    UCHAR bInterfaceSubClass;
    UCHAR bInterfaceProtocol;
    UCHAR iInterface;
} USB_INTERFACE_DESCRIPTOR, *PUSB_INTERFACE_DESCRIPTOR;
#pragma pack(pop)

#define USB_INTERFACE_DESCRIPTOR_TYPE 0x04

WINUSBAPI BOOL WINAPI WinUsb_Initialize(HANDLE device, PWINUSB_INTERFACE_HANDLE handle);
WINUSBAPI BOOL WINAPI WinUsb_Free(WINUSB_INTERFACE_HANDLE handle);
WINUSBAPI BOOL WINAPI WinUsb_ControlTransfer(WINUSB_INTERFACE_HANDLE handle, WINUSB_SETUP_PACKET setup, PUCHAR buffer,
                                             ULONG length, PULONG transferred, LPOVERLAPPED overlapped);
WINUSBAPI BOOL WINAPI WinUsb_ReadPipe(WINUSB_INTERFACE_HANDLE handle, UCHAR pipe, PUCHAR buffer, ULONG length,
                                      PULONG transferred, LPOVERLAPPED overlapped);
WINUSBAPI PUSB_COMMON_DESCRIPTOR WINAPI WinUsb_ParseDescriptors(PVOID buffer, ULONG length, PVOID start, LONG type);
WINUSBAPI PUSB_INTERFACE_DESCRIPTOR WINAPI WinUsb_ParseConfigurationDescriptor(PUSB_CONFIGURATION_DESCRIPTOR config,
                                                                              PVOID start, LONG number,
                                                                              LONG alternate, LONG klass,
                                                                              LONG subclass, LONG protocol);

#endif
