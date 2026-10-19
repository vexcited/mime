/*
 * mime's plug and play: what its drivers tell the kernel about devices,
 * and the NtPlugPlayControl requests cfgmgr32 reads the device tree with.
 * the requests hold no pointers, so x86 and x64 share their layout.
 */
#ifndef MIME_PNP_H
#define MIME_PNP_H

typedef enum MIME_PNP_CLASS {
    MIME_PNP_LIST_INTERFACES = 9, /* the device paths of a class's interfaces, of one device or all, as a multi-sz */
    MIME_PNP_RELATED = 12,        /* the parent, first child or next sibling of a device */
    MIME_PNP_INTERFACE_DEVICE = 13, /* the device and class of an interface path */
    MIME_PNP_STATUS = 14,         /* succeeds when the device is present */
    MIME_PNP_LIST_DEVICES = 100,  /* the instance ids of the present devices, as a multi-sz */
    /*
     * the generation of the tree, which goes up at every device or interface added or removed. Flags is the one the
     * caller saw, or 0xFFFFFFFF for none: the request returns at once with the current one if that differs, and
     * waits for a change if not. an alertable wait.
     */
    MIME_PNP_WAIT_CHANGE = 101,
} MIME_PNP_CLASS;

enum {
    MIME_PNP_PARENT = 1,
    MIME_PNP_CHILD = 2,
    MIME_PNP_SIBLING = 3,
};

typedef struct MIME_PNP_REQUEST {
    ULONG Flags;     /* the relation; for interfaces, nonzero for every class */
    ULONG OutChars;  /* written to Out, or needed */
    GUID Class;
    WCHAR Instance[260]; /* an instance id or an interface path */
    WCHAR Out[];
} MIME_PNP_REQUEST;

/* the values of a device's Enum key; multi-sz ones end in two nulls. */
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
/* an interface whose path ends in a reference string, the name the device is opened with: "\\?\...#{class}\global". */
NTKERNELAPI NTSTATUS NTAPI MimePnpAddInterfaceRef(PCWSTR instance, const GUID *klass, PCWSTR device, PCWSTR reference);
#endif

#endif
