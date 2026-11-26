#include "../ntos.h"

#define IOCTL_MOUNTMGR_QUERY_POINTS 0x6D0008
#define IOCTL_MOUNTMGR_QUERY_DOS_VOLUME_PATH 0x6D0030
#define IOCTL_MOUNTMGR_QUERY_DOS_VOLUME_PATHS 0x6D0034

#define POINT_SIZE 24 /* MOUNTMGR_MOUNT_POINT: three offsets and lengths of names. */

static NTSTATUS complete(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

/*
 * which drive letters and volume names point at which volume, what kernelbase builds GetFinalPathNameByHandle,
 * GetVolumeNameForVolumeMountPoint and GetVolumePathName on. mime's volumes are fixed by their letter: volume 1 is C:,
 * 2 is Z:, a host drive of letter L is volume L - 'A'. the answers are laid out as the mount manager of windows 11 lays
 * them out.
 */

/* the letter of a volume number, or 0 when that volume has none. */
static WCHAR volume_letter(ULONG number)
{
    if (number == 1)
        return L'C';
    if (number == 2)
        return L'Z';
    if (number >= MIME_FS_DRIVE_FIRST - 'A' && number <= MIME_FS_DRIVE_LAST - 'A')
        return (WCHAR)(L'A' + number);
    return 0;
}

static BOOLEAN volume_present(ULONG number)
{
    WCHAR path[40];
    _snwprintf(path, RTL_NUMBER_OF(path), L"\\Device\\HarddiskVolume%u", number);
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    PVOID object = nullptr;
    if (!NT_SUCCESS(ObReferenceObjectByName(&name, OBJ_CASE_INSENSITIVE, nullptr, 0, nullptr, KernelMode, nullptr,
                                            &object)))
        return FALSE;
    ObDereferenceObject(object);
    return TRUE;
}

/* the guid of the volume of a letter, as the \GLOBAL??\Volume{...} links of devlinks.c name it. */
void MountVolumeGuid(WCHAR letter, GUID *guid)
{
    memset(guid, 0, sizeof(*guid));
    guid->Data1 = 0x4d696d65;
    guid->Data3 = (USHORT)letter;
    guid->Data4[7] = (UCHAR)letter;
}

/* the unique id the volume device answers: the tag of the volume manager and the guid. */
ULONG MountVolumeUniqueId(WCHAR letter, UCHAR out[24])
{
    memcpy(out, "DMIO:ID:", 8);
    GUID guid;
    MountVolumeGuid(letter, &guid);
    memcpy(out + 8, &guid, sizeof(guid));
    return 24;
}

static ULONG put_text(WCHAR *out, PCWSTR format, ULONG a)
{
    return (ULONG)_snwprintf(out, 64, format, a);
}

static ULONG volume_name_link(WCHAR letter, WCHAR *out)
{
    GUID g;
    MountVolumeGuid(letter, &g);
    return (ULONG)_snwprintf(out, 64, L"\\??\\Volume{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g.Data1, g.Data2,
                             g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
                             g.Data4[7]);
}

/* the volume a name says: \Device\HarddiskVolumeN, \DosDevices\L:, \??\L: or \??\Volume{guid}. 0 when none. */
static ULONG volume_of(const WCHAR *name, ULONG chars)
{
    WCHAR text[80];
    if (!chars || chars >= RTL_NUMBER_OF(text))
        return 0;
    memcpy(text, name, chars * sizeof(WCHAR));
    text[chars] = 0;
    for (ULONG n = 1; n < 32; n++) {
        WCHAR letter = volume_letter(n), probe[64];
        if (!letter || !volume_present(n))
            continue;
        put_text(probe, L"\\Device\\HarddiskVolume%u", n);
        if (!_wcsicmp(text, probe))
            return n;
        _snwprintf(probe, RTL_NUMBER_OF(probe), L"\\DosDevices\\%c:", letter);
        if (!_wcsicmp(text, probe))
            return n;
        _snwprintf(probe, RTL_NUMBER_OF(probe), L"\\??\\%c:", letter);
        if (!_wcsicmp(text, probe))
            return n;
        volume_name_link(letter, probe);
        if (!_wcsicmp(text, probe))
            return n;
    }
    return 0;
}

/* MOUNTMGR_TARGET_NAME in, MOUNTMGR_VOLUME_PATHS out: the letter of the volume as "L:" in a list of strings. */
static NTSTATUS dos_volume_path(PIRP irp, PIO_STACK_LOCATION stack)
{
    ULONG in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PUCHAR buf = irp->AssociatedIrp.SystemBuffer;
    if (in_len < sizeof(USHORT) || in_len < sizeof(USHORT) + *(PUSHORT)buf || out_len < sizeof(ULONG))
        return complete(irp, STATUS_INVALID_PARAMETER, 0);
    ULONG volume = volume_of((const WCHAR *)(buf + sizeof(USHORT)), *(PUSHORT)buf / sizeof(WCHAR));
    if (!volume)
        return complete(irp, STATUS_OBJECT_NAME_NOT_FOUND, 0);
    WCHAR letter = volume_letter(volume);
    const WCHAR paths[4] = {letter, L':', 0, 0};
    *(PULONG)buf = sizeof(paths);
    if (out_len < sizeof(ULONG) + sizeof(paths))
        return complete(irp, STATUS_BUFFER_OVERFLOW, sizeof(ULONG));
    memcpy(buf + sizeof(ULONG), paths, sizeof(paths));
    return complete(irp, STATUS_SUCCESS, sizeof(ULONG) + sizeof(paths));
}

typedef struct POINT_NAMES {
    WCHAR Link[64];
    ULONG LinkChars;
} POINT_NAMES;

/*
 * MOUNTMGR_MOUNT_POINT in, the points that match it out. asked by its link it is one point, laid out as the link, the
 * unique id, the device name; asked by the device it is every link of the volume, the letter then the volume name,
 * after the one unique id and device name they share.
 */
static NTSTATUS query_points(PIRP irp, PIO_STACK_LOCATION stack)
{
    ULONG in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PUCHAR buf = irp->AssociatedIrp.SystemBuffer;
    if (in_len < POINT_SIZE || out_len < 8 + POINT_SIZE)
        return complete(irp, STATUS_INVALID_PARAMETER, 0);
    ULONG link_offset = *(PULONG)buf, device_offset = *(PULONG)(buf + 16);
    USHORT link_length = *(PUSHORT)(buf + 4), device_length = *(PUSHORT)(buf + 20);
    if ((link_length && (link_offset > in_len || link_length > in_len - link_offset)) ||
        (device_length && (device_offset > in_len || device_length > in_len - device_offset)))
        return complete(irp, STATUS_INVALID_PARAMETER, 0);
    BOOLEAN by_link = link_length != 0;
    ULONG volume = by_link ? volume_of((const WCHAR *)(buf + link_offset), link_length / sizeof(WCHAR))
                           : volume_of((const WCHAR *)(buf + device_offset), device_length / sizeof(WCHAR));
    if (!volume)
        return complete(irp, STATUS_OBJECT_NAME_NOT_FOUND, 0);
    WCHAR letter = volume_letter(volume), device[40];
    ULONG device_chars = put_text(device, L"\\Device\\HarddiskVolume%u", volume);
    UCHAR id[24];
    ULONG id_bytes = MountVolumeUniqueId(letter, id);
    POINT_NAMES links[2];
    links[0].LinkChars = (ULONG)_snwprintf(links[0].Link, 64, L"\\DosDevices\\%c:", letter);
    links[1].LinkChars = volume_name_link(letter, links[1].Link);
    ULONG count = by_link ? 1 : 2;
    ULONG size = 8 + count * POINT_SIZE + id_bytes + device_chars * sizeof(WCHAR);
    for (ULONG i = 0; i < count; i++)
        size += links[i].LinkChars * sizeof(WCHAR);
    *(PULONG)buf = size;
    *(PULONG)(buf + 4) = count;
    if (out_len < size)
        return complete(irp, STATUS_BUFFER_OVERFLOW, 8);
    memset(buf + 8, 0, count * POINT_SIZE);
    ULONG at = 8 + count * POINT_SIZE, id_at, device_at;
    PUCHAR point = buf + 8;
    if (by_link) {
        *(PULONG)point = at;
        *(PUSHORT)(point + 4) = (USHORT)(links[0].LinkChars * sizeof(WCHAR));
        memcpy(buf + at, links[0].Link, links[0].LinkChars * sizeof(WCHAR));
        at += links[0].LinkChars * sizeof(WCHAR);
    }
    id_at = at;
    memcpy(buf + at, id, id_bytes);
    at += id_bytes;
    device_at = at;
    memcpy(buf + at, device, device_chars * sizeof(WCHAR));
    at += device_chars * sizeof(WCHAR);
    for (ULONG i = 0; i < count; i++, point += POINT_SIZE) {
        if (!by_link) {
            *(PULONG)point = at;
            *(PUSHORT)(point + 4) = (USHORT)(links[i].LinkChars * sizeof(WCHAR));
            memcpy(buf + at, links[i].Link, links[i].LinkChars * sizeof(WCHAR));
            at += links[i].LinkChars * sizeof(WCHAR);
        }
        *(PULONG)(point + 8) = id_at;
        *(PUSHORT)(point + 12) = (USHORT)id_bytes;
        *(PULONG)(point + 16) = device_at;
        *(PUSHORT)(point + 20) = (USHORT)(device_chars * sizeof(WCHAR));
    }
    return complete(irp, STATUS_SUCCESS, size);
}

static NTSTATUS NTAPI mountmgr_create_close(PDEVICE_OBJECT device, PIRP irp)
{
    return complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI mountmgr_control(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_MOUNTMGR_QUERY_POINTS:
        return query_points(irp, stack);
    case IOCTL_MOUNTMGR_QUERY_DOS_VOLUME_PATH:
    case IOCTL_MOUNTMGR_QUERY_DOS_VOLUME_PATHS:
        return dos_volume_path(irp, stack);
    default:
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

static NTSTATUS NTAPI MountMgrEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = mountmgr_create_close;
    driver->MajorFunction[IRP_MJ_CLOSE] = mountmgr_create_close;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = mountmgr_control;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\MountPointManager");
    PDEVICE_OBJECT device;
    NTSTATUS status = IoCreateDevice(driver, 0, &name, 0x4D /* FILE_DEVICE_MOUNTMGR */, 0, FALSE, &device);
    if (NT_SUCCESS(status))
        device->Flags &= ~DO_DEVICE_INITIALIZING;
    return status;
}

void IopInitializeMountManager(void)
{
    NTSTATUS status = IoCreateDriver(L"\\Driver\\mountmgr", MountMgrEntry, nullptr);
    if (!NT_SUCCESS(status))
        KiLogPrintf("mountmgr failed to start: %08x", status);
}
