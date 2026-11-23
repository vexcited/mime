/*
 * plug and play: what mime's drivers tell the kernel about devices, and the NtPlugPlayControl requests cfgmgr32
 * reads the device tree with. the requests hold no pointers, so x86 and x64 share their layout.
 */
#ifndef MIME_PNP_H
#define MIME_PNP_H

typedef enum MIME_PNP_CLASS {
    MIME_PNP_LIST_INTERFACES = 9, /* interface paths of a class, for one device or all, as a multi-sz. */
    MIME_PNP_RELATED = 12,        /* the parent, first child or next sibling of a device. */
    MIME_PNP_INTERFACE_DEVICE = 13, /* the device and class of an interface path. */
    MIME_PNP_STATUS = 14,         /* succeeds when the device is present. */
    MIME_PNP_LIST_DEVICES = 100,  /* instance ids of the present devices, as a multi-sz. */
    /*
     * the tree generation, bumped whenever a device or interface comes or goes. Flags holds the one the caller
     * saw, or 0xFFFFFFFF for none: if the current one differs it returns at once, else it waits alertably for a
     * change.
     */
    MIME_PNP_WAIT_CHANGE = 101,
    /*
     * makes the Enum key of a device that is not there, with the value Phantom, for CM_Create_DevNode. windows'
     * plug and play manager does this for a caller it grants write access, an administrator.
     */
    MIME_PNP_CREATE_PHANTOM = 102,
} MIME_PNP_CLASS;

enum {
    MIME_PNP_PARENT = 1,
    MIME_PNP_CHILD = 2,
    MIME_PNP_SIBLING = 3,
};

/*
 * the same requests as controls of \Device\DeviceApi\CMApi, for a 32-bit process: the function of the code is the
 * class, input and output are the request. a control that finds the buffer too small succeeds with this in Flags and
 * the size it needs in OutChars.
 */
#define MIME_PNP_CONTROL(klass) (0x00220000u | (0x800u + (klass)) << 2)
#define MIME_PNP_CONTROL_CLASS(code) ((((code) >> 2) & 0xFFF) - 0x800u)
#define MIME_PNP_TOO_SMALL 0xFFFFFFFEu

typedef struct MIME_PNP_REQUEST {
    ULONG Flags;     /* the relation. for interfaces, nonzero for every class. */
    ULONG OutChars;  /* written to Out, or needed. */
    GUID Class;
    WCHAR Instance[260]; /* an instance id or an interface path. */
    WCHAR Out[];
} MIME_PNP_REQUEST;

/* the values of a device's Enum key. multi-sz ones end in two nulls. */
typedef struct MIME_DEVICE_PROPERTIES {
    PCWSTR DeviceDesc;
    PCWSTR HardwareIds;
    PCWSTR CompatibleIds;
    PCWSTR Class;
    PCWSTR ClassGuid;
    PCWSTR Manufacturer;
    PCWSTR Service;
    PCWSTR Location;
    PCWSTR ContainerId;
    PCWSTR FriendlyName;
} MIME_DEVICE_PROPERTIES;

#ifdef MIME_KERNEL_PNP
NTKERNELAPI NTSTATUS NTAPI MimePnpAddDevice(PCWSTR instance, PCWSTR parent, const MIME_DEVICE_PROPERTIES *props);
NTKERNELAPI void NTAPI MimePnpRemoveDevice(PCWSTR instance);
NTKERNELAPI NTSTATUS NTAPI MimePnpAddInterface(PCWSTR instance, const GUID *klass, PCWSTR device);
/* the path ends in a reference string, the name the device is opened with: "\\?\...#{class}\global". */
NTKERNELAPI NTSTATUS NTAPI MimePnpAddInterfaceRef(PCWSTR instance, const GUID *klass, PCWSTR device, PCWSTR reference);
#endif

#endif
