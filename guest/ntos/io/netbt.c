#include "nsi_p.h"

#define IOCTL_NETBT_FIND_NAME 0x210096
#define IOCTL_NETBT_GET_WINS_ADDR 0x21009A

/* What netbt has in place of a name server nobody configured: 127.0.0.0. */
#define NETBT_NO_SERVER 0x7F000000u
/* A broadcast node, with the bit that says nobody configured the type. */
#define NETBT_NODE_DEFAULT_BROADCAST 0x1001u

#define NETBT_MAX_ADAPTERS 64

typedef struct NETBT_DEVICE {
    GUID Guid;
    BOOLEAN Export;
} NETBT_DEVICE;

/* The answer of IOCTL_NETBT_GET_WINS_ADDR, as NbtQueryWinsSettings of netbt.sys 26300 fills it. */
typedef struct NETBT_WINS_SETTINGS {
    ULONG Primary;
    ULONG Secondary;
    ULONG Others[10];
    USHORT OtherCount;
    USHORT Reserved;
    ULONG NodeType;
    BOOLEAN NetbiosEnabled;
    UCHAR Padding[3];
    BOOLEAN Last;
} NETBT_WINS_SETTINGS;

static PDRIVER_OBJECT g_driver;
static PDEVICE_OBJECT g_adapters[NETBT_MAX_ADAPTERS];
static ULONG g_adapter_count;

BOOLEAN IopIsNetbt(PFILE_OBJECT file)
{
    return g_driver && file->DeviceObject && file->DeviceObject->DriverObject == g_driver;
}

/*
 * Measured on Windows 11 26100. The control is of method out direct and the driver writes through the mdl, so the
 * caller is told a length of zero whatever was written. A buffer shorter than the two servers of the first version is
 * an invalid device request. One shorter than 60 bytes gets those two only. The three bytes after the netbios flag are
 * left as they were. A failure does not reach the status block.
 */
static NTSTATUS wins_settings(const NETBT_DEVICE *d, PVOID output, ULONG out_len)
{
    if (!output || out_len < 8)
        return STATUS_INVALID_DEVICE_REQUEST;
    NETBT_WINS_SETTINGS s = {};
    if (!d->Export)
        s.Primary = s.Secondary = NETBT_NO_SERVER;
    s.NodeType = NETBT_NODE_DEFAULT_BROADCAST;
    s.NetbiosEnabled = !d->Export;
    if (out_len < 60)
        return KiCopyToUser(output, &s, 8);
    NTSTATUS status = KiCopyToUser(output, &s, FIELD_OFFSET(NETBT_WINS_SETTINGS, Padding));
    if (NT_SUCCESS(status) && out_len >= sizeof(s))
        status = KiCopyToUser((PUCHAR)output + FIELD_OFFSET(NETBT_WINS_SETTINGS, Last), &s.Last, sizeof(s.Last));
    return status;
}

NTSTATUS NetbtDeviceControl(PFILE_OBJECT file, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb,
                            ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len)
{
    const NETBT_DEVICE *d = file->DeviceObject->DeviceExtension;
    NTSTATUS status;
    switch (code) {
    case IOCTL_NETBT_GET_WINS_ADDR:
        status = wins_settings(d, output, out_len);
        break;
    case IOCTL_NETBT_FIND_NAME:
        /* A name query. Measured without a name to look for only. No name is registered here. */
        status = STATUS_UNSUCCESSFUL;
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }
    if (NT_ERROR(status))
        return status;
    NTSTATUS created;
    IO_USER_REQUEST *r = IopNewUserRequest(file, event, apc, context, iosb, nullptr, 0, &created);
    if (!r)
        return created;
    return IopImmediateUserRequest(r, status, 0);
}

static NTSTATUS complete_irp(PIRP irp, NTSTATUS status)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = 0;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS NTAPI netbt_create_close(PDEVICE_OBJECT device, PIRP irp)
{
    return complete_irp(irp, STATUS_SUCCESS);
}

static PDEVICE_OBJECT new_device(PCWSTR path, const GUID *guid)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    PDEVICE_OBJECT device;
    if (!NT_SUCCESS(IoCreateDevice(g_driver, sizeof(NETBT_DEVICE), &name, FILE_DEVICE_NETWORK, FILE_DEVICE_SECURE_OPEN,
                                   FALSE, &device)))
        return nullptr;
    NETBT_DEVICE *d = device->DeviceExtension;
    if (guid)
        d->Guid = *guid;
    else
        d->Export = TRUE;
    device->Flags &= ~(ULONG)DO_DEVICE_INITIALIZING;
    return device;
}

static BOOLEAN bound(const NSI_IF *a)
{
    return !a->Loopback && a->Ipv4;
}

/* A device for each adapter the ip stack is bound to. Called with the lock of the store held. */
void NetbtSync(const NSI_NET *net)
{
    if (!g_driver)
        return;
    for (ULONG i = 0; i < g_adapter_count;) {
        const NETBT_DEVICE *d = g_adapters[i]->DeviceExtension;
        BOOLEAN found = FALSE;
        for (ULONG j = 0; j < net->IfCount && !found; j++)
            found = bound(&net->Ifs[j]) && !memcmp(&net->Ifs[j].Guid, &d->Guid, sizeof(GUID));
        if (found) {
            i++;
            continue;
        }
        IoDeleteDevice(g_adapters[i]);
        g_adapters[i] = g_adapters[--g_adapter_count];
    }
    for (ULONG j = 0; j < net->IfCount && g_adapter_count < NETBT_MAX_ADAPTERS; j++) {
        const GUID *g = &net->Ifs[j].Guid;
        if (!bound(&net->Ifs[j]))
            continue;
        BOOLEAN found = FALSE;
        for (ULONG i = 0; i < g_adapter_count && !found; i++)
            found = !memcmp(g, &((const NETBT_DEVICE *)g_adapters[i]->DeviceExtension)->Guid, sizeof(GUID));
        if (found)
            continue;
        WCHAR path[80];
        _snwprintf(path, RTL_NUMBER_OF(path), L"\\Device\\NetBT_Tcpip_{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                   (unsigned)g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
                   g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
        path[RTL_NUMBER_OF(path) - 1] = 0;
        PDEVICE_OBJECT device = new_device(path, g);
        if (device)
            g_adapters[g_adapter_count++] = device;
    }
}

static NTSTATUS NTAPI NetbtDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = netbt_create_close;
    driver->MajorFunction[IRP_MJ_CLEANUP] = netbt_create_close;
    driver->MajorFunction[IRP_MJ_CLOSE] = netbt_create_close;
    return STATUS_SUCCESS;
}

void IopInitializeNetbt(void)
{
    PDRIVER_OBJECT driver;
    NTSTATUS status = IoCreateDriver(L"\\Driver\\NetBT", NetbtDriverEntry, &driver);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("netbt failed to start: %08x", status);
        return;
    }
    g_driver = driver;
    new_device(L"\\Device\\NetBt_Wins_Export", nullptr);
}
