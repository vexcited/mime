/*
 * what \GLOBAL?? holds on windows besides the drive letters: the interfaces of the hardware, each a link named by the
 * instance of its device and the class of the interface, and the names drivers give their control devices. measured
 * on windows 11 26300, where the directory lists 146 links and most are interfaces. the classes below are the ones
 * windows' own drivers register for each kind of device there.
 */
#define MIME_KERNEL_PNP
#include "../ntos.h"
#include <mime/pnp.h>

#define SYSTEM_CLASS L"{4d36e97d-e325-11ce-bfc1-08002be10318}"
#define DISPLAY_CLASS L"{4d36e968-e325-11ce-bfc1-08002be10318}"
#define DISK_CLASS L"{4d36e967-e325-11ce-bfc1-08002be10318}"
#define STORAGE_CLASS L"{4d36e97b-e325-11ce-bfc1-08002be10318}"
#define NET_CLASS L"{4d36e972-e325-11ce-bfc1-08002be10318}"
#define VOLUME_CLASS L"{71a27cdd-812a-11d0-bec7-08002be2092f}"
#define PROCESSOR_CLASS L"{50127dc3-0f36-415e-a6cc-4cb3be910b65}"

#define IF_PROCESSOR L"97fadb10-4e33-40ae-359c-8bef029dbdd0"
#define IF_COOLING L"dbe4373d-3c81-40cb-ace4-e0e5d05f0c9f"
#define IF_BATTERY L"72631e54-78a4-11d0-bcf7-00aa00b7b32a"
#define IF_THERMAL_ZONE L"4afa3d51-74a7-11d0-be5e-00a0c9062857"
#define IF_BUTTON L"4afa3d53-74a7-11d0-be5e-00a0c9062857"
#define IF_ACPI L"15c5e253-b8c4-452e-99df-461d0f5078c8"
#define IF_VOLMGR L"53f5630e-b6bf-11d0-94f2-00a0c91efb8b"
#define IF_SPACEPORT L"ef66a56f-88d1-4cd8-98c4-49faf57ad8af"
#define IF_SWENUM L"3c0d501a-140b-11d1-b40f-00a0c9223196"
#define IF_SWENUM_BUS L"4747b320-62ce-11cf-a5d6-28db04c10000"
#define IF_UMBUS L"65a9a6cf-64cd-480b-843e-32c86e1ba19f"
#define IF_VDRVROOT L"2e34d650-5819-42ca-84ae-d30803bae505"
#define IF_DISK L"53f56307-b6bf-11d0-94f2-00a0c91efb8b"
#define IF_VOLUME L"53f5630d-b6bf-11d0-94f2-00a0c91efb8b"
#define IF_STORAGE_PORT L"2accfe60-c130-11d2-b082-00a0c91efb8b"
#define IF_NET L"cac88484-7515-4c03-82e6-71a87abac361"
#define IF_NDIS_LAN L"ad498944-762f-11d0-8dcb-00c04fc3358c"
#define IF_DISPLAY_ADAPTER L"5b45201d-f2f2-4f3b-85bb-30ff1f953599"
#define IF_DISPLAY_ARRIVAL L"1ca05180-a699-450a-9a0c-de4fbe3ddd89"
#define IF_DISPLAY_INDIRECT L"1ca05181-a699-450a-9a0c-de4fbe3ddd89"
#define IF_DISPLAY_RENDER L"1024e4c9-47c9-48d3-b4a8-f9df78523b53"
#define IF_MONITOR L"e6f07b5f-ee97-4a90-b076-33f57bf4eaa7"
#define IF_MONITOR_BRIGHTNESS L"10910c20-0c64-4172-9409-add3064c0cad"
#define IF_MONITOR_VIDEO_OUTPUT L"866519b5-3f07-4c97-b7df-24c5d8a8ccb8"

static PDRIVER_OBJECT g_driver;
static ULONG g_gpus, g_monitors;

static BOOLEAN exists(PCWSTR path)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    PVOID object = nullptr;
    if (!NT_SUCCESS(ObReferenceObjectByName(&name, OBJ_CASE_INSENSITIVE, nullptr, 0, nullptr, KernelMode, nullptr, &object)))
        return FALSE;
    ObDereferenceObject(object);
    return TRUE;
}

/* the device object pnp gives a device node, named by a number as windows names them. made when no driver made it. */
static void pdo(ULONG number, PWCHAR out, ULONG chars)
{
    _snwprintf(out, chars, L"\\Device\\%08x", number);
    out[chars - 1] = 0;
    if (exists(out) || !g_driver)
        return;
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, out);
    PDEVICE_OBJECT device;
    if (NT_SUCCESS(IoCreateDevice(g_driver, 0, &name, FILE_DEVICE_UNKNOWN, 0, FALSE, &device)))
        device->Flags &= ~(ULONG)DO_DEVICE_INITIALIZING;
}

static ULONG hex(PCWSTR text, ULONG digits)
{
    ULONG value = 0;
    for (ULONG i = 0; i < digits; i++) {
        WCHAR c = text[i];
        value = value << 4 | (c <= L'9' ? c - L'0' : (c | 0x20) - L'a' + 10);
    }
    return value;
}

/* "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx". */
static void interface(PCWSTR instance, PCWSTR klass, PCWSTR device)
{
    GUID guid = {hex(klass, 8), (USHORT)hex(klass + 9, 4), (USHORT)hex(klass + 14, 4), {}};
    for (ULONG i = 0; i < 8; i++)
        guid.Data4[i] = (UCHAR)hex(klass + (i < 2 ? 19 + i * 2 : 24 + (i - 2) * 2), 2);
    MimePnpAddInterface(instance, &guid, device);
}

static void interfaces(PCWSTR instance, PCWSTR device, PCWSTR a, PCWSTR b, PCWSTR c, PCWSTR d)
{
    PCWSTR classes[] = {a, b, c, d};
    for (ULONG i = 0; i < RTL_NUMBER_OF(classes); i++)
        if (classes[i])
            interface(instance, classes[i], device);
}

static void device(PCWSTR instance, PCWSTR parent, PCWSTR desc, PCWSTR hardware, PCWSTR compatible, PCWSTR klass,
                   PCWSTR guid, PCWSTR manufacturer, PCWSTR service, PCWSTR friendly)
{
    MIME_DEVICE_PROPERTIES p = {desc, hardware, compatible, klass, guid, manufacturer, service, nullptr,
                                L"{00000000-0000-0000-ffff-ffffffffffff}", friendly};
    MimePnpAddDevice(instance, parent, &p);
}

static void underscores(PWCHAR text)
{
    for (; *text; text++)
        if (*text == L' ')
            *text = L'_';
}

/*
 * a processor is a device of the acpi namespace: its id is the vendor, the identifier up to the model and the brand
 * string, spaces as underscores, and its instance is the number acpi has for it. the first four take the device
 * objects of the reference machine, the others ones past the last of them.
 */
static void processors(void)
{
    WCHAR vendor[48] = {}, brand[64] = {}, model[96] = {}, friendly[64] = {}, hardware[320] = {};
    PCSTR v = KiProcessorVendor(), b = KiProcessorBrand();
    for (ULONG i = 0; v[i] && i < RTL_NUMBER_OF(vendor) - 1; i++)
        vendor[i] = (WCHAR)(UCHAR)v[i];
    for (ULONG i = 0; b[i] && i < RTL_NUMBER_OF(brand) - 1; i++)
        friendly[i] = brand[i] = (WCHAR)(UCHAR)b[i];
    KiFormatProcessorIdentifier(model, RTL_NUMBER_OF(model) - 1);
    PWCHAR stepping = wcsstr(model, L" Stepping");
    if (stepping)
        *stepping = 0;
    underscores(model);
    underscores(brand);
    BOOLEAN amd = !wcscmp(vendor, L"AuthenticAMD");
    /* the ids go from the model to the architecture, each with the bus and as a bare one. */
    ULONG at = 0;
    WCHAR id[128] = {};
    _snwprintf(id, RTL_NUMBER_OF(id) - 1, L"%ls_-_%ls", vendor, model);
    static const PCWSTR cuts[] = {nullptr, L"_Model", L"_Family"};
    for (ULONG cut = 0; cut < RTL_NUMBER_OF(cuts); cut++) {
        PWCHAR end = cuts[cut] ? wcsstr(id, cuts[cut]) : nullptr;
        if (cuts[cut] && !end)
            continue;
        if (end)
            *end = 0;
        at += (ULONG)_snwprintf(hardware + at, RTL_NUMBER_OF(hardware) - at - 2, L"ACPI\\%ls", id) + 1;
        at += (ULONG)_snwprintf(hardware + at, RTL_NUMBER_OF(hardware) - at - 2, L"*%ls", id) + 1;
    }
    for (ULONG cpu = 0; cpu < HalBootInfo->cpu_count; cpu++) {
        WCHAR instance[200] = {}, target[32];
        _snwprintf(instance, RTL_NUMBER_OF(instance) - 1, L"ACPI\\%ls_-_%ls_-_%ls\\_%u", vendor, model, brand, cpu);
        device(instance, L"ACPI_HAL\\PNP0C08\\0", amd ? L"AMD Processor" : L"Intel Processor", hardware,
               L"ACPI\\Processor\0", L"Processor", PROCESSOR_CLASS, amd ? L"Advanced Micro Devices" : L"Intel",
               amd ? L"AmdPPM" : L"intelppm", friendly);
        pdo(cpu < 4 ? 0x1b + cpu : 0x40 + cpu, target, RTL_NUMBER_OF(target));
        interfaces(instance, target, IF_PROCESSOR, IF_COOLING, nullptr, nullptr);
    }
}

/* the devices windows' drivers report on every machine, which the tree did not hold: each with its service. */
static void software(void)
{
    static const struct {
        PCWSTR Instance, Desc, Hardware, Guid, Manufacturer, Service;
        ULONG Pdo;
        PCWSTR Classes[2];
    } table[] = {
        {L"ACPI_HAL\\PNP0C08\\0", L"Microsoft ACPI-Compliant System", L"ACPI_HAL\\PNP0C08\0*PNP0C08\0", SYSTEM_CLASS,
         L"Microsoft", L"ACPI", 0x12, {IF_ACPI}},
        {L"ACPI\\FixedButton\\2&daba3ff&0", L"ACPI Fixed Feature Button", L"ACPI\\FixedButton\0*FixedButton\0",
         SYSTEM_CLASS, L"(Standard system devices)", nullptr, 0x1a, {IF_BUTTON}},
        {L"ROOT\\volmgr\\0000", L"Volume Manager", L"ROOT\\VOLMGR\0", SYSTEM_CLASS, L"Microsoft", L"volmgr", 0x1,
         {IF_VOLMGR}},
        {L"ROOT\\spaceport\\0000", L"Microsoft Storage Spaces Controller", L"Root\\Spaceport\0", STORAGE_CLASS,
         L"Microsoft", L"spaceport", 0x6, {IF_VOLMGR, IF_SPACEPORT}},
        {L"ROOT\\SYSTEM\\0000", L"Plug and Play Software Device Enumerator", L"ROOT\\SWENUM\0", SYSTEM_CLASS,
         L"(Standard system devices)", L"swenum", 0x10, {IF_SWENUM, IF_SWENUM_BUS}},
        {L"ROOT\\UMBUS\\0000", L"UMBus Root Bus Enumerator", L"root\\umbus\0", SYSTEM_CLASS, L"Microsoft", L"umbus", 0x8,
         {IF_UMBUS}},
        {L"ROOT\\vdrvroot\\0000", L"Microsoft Virtual Drive Enumerator", L"ROOT\\vdrvroot\0", SYSTEM_CLASS,
         L"Microsoft", L"vdrvroot", 0x5, {IF_VDRVROOT}},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(table); i++) {
        WCHAR target[32];
        device(table[i].Instance, nullptr, table[i].Desc, table[i].Hardware, nullptr,
               wcscmp(table[i].Guid, STORAGE_CLASS) ? L"System" : L"SCSIAdapter", table[i].Guid, table[i].Manufacturer,
               table[i].Service, nullptr);
        pdo(table[i].Pdo, target, RTL_NUMBER_OF(target));
        interfaces(table[i].Instance, target, table[i].Classes[0], table[i].Classes[1], nullptr, nullptr);
    }
}

/*
 * the disk behind the nvme controller, named as its inquiry answers in mimefs.c, and the two volumes the machine
 * boots with. their interfaces open the devices the drive names open.
 */
static void storage(void)
{
#if defined(__aarch64__)
#define DISK_PRODUCT L"KBG40ZNS1T02_NVM"
#define DISK_NAME L"KBG40ZNS1T02 NVMe KIOXIA 1024GB"
#else
#define DISK_PRODUCT L"SAMSUNG_MZVL21T0"
#define DISK_NAME L"SAMSUNG MZVL21T0HCLR-00BL2"
#endif
    static const WCHAR disk[] = L"SCSI\\Disk&Ven_NVMe&Prod_" DISK_PRODUCT L"\\5&2f0c7c9c&0&000000";
    device(disk, L"PCI\\VEN_144D&DEV_A80A&SUBSYS_00000000&REV_00\\3&11583659&0&1D", L"Disk drive",
           L"SCSI\\DiskNVMe____" DISK_PRODUCT L"AGXA\0SCSI\\DiskNVMe____" DISK_PRODUCT L"\0SCSI\\DiskNVMe____\0"
           L"SCSI\\NVMe____" DISK_PRODUCT L"A\0NVMe____" DISK_PRODUCT L"A\0GenDisk\0",
           L"SCSI\\Disk\0SCSI\\RAW\0", L"DiskDrive", DISK_CLASS, L"(Standard disk drives)", L"disk", DISK_NAME);
    interfaces(disk, L"\\Device\\Harddisk0", IF_DISK, nullptr, nullptr, nullptr);
    for (ULONG n = 1; n <= 2; n++) {
        WCHAR instance[96] = {}, target[40] = {}, name[80] = {};
        _snwprintf(target, RTL_NUMBER_OF(target) - 1, L"\\Device\\HarddiskVolume%u", n);
        if (!exists(target))
            continue;
        _snwprintf(instance, RTL_NUMBER_OF(instance) - 1,
                   L"STORAGE\\Volume\\{4d696d65-0000-0000-0000-000000000000}#%016llX", (ULONG64)n << 20);
        device(instance, disk, L"Volume", L"STORAGE\\Volume\0", nullptr, L"Volume", VOLUME_CLASS, L"Microsoft", L"volume",
               nullptr);
        interfaces(instance, target, IF_VOLUME, nullptr, nullptr, nullptr);
        /* the names of the volume: its number, its partition, and the guid kernelbase makes from its letter. */
        _snwprintf(name, RTL_NUMBER_OF(name) - 1, L"\\GLOBAL??\\HarddiskVolume%u", n);
        ObCreateSymbolicLink(name, target);
        _snwprintf(name, RTL_NUMBER_OF(name) - 1, L"\\GLOBAL??\\Harddisk0Partition%u", n);
        ObCreateSymbolicLink(name, target);
        ULONG letter = n == 1 ? L'C' : L'Z';
        _snwprintf(name, RTL_NUMBER_OF(name) - 1, L"\\GLOBAL??\\Volume{4d696d65-0000-00%02x-0000-0000000000%02x}", letter,
                   letter);
        ObCreateSymbolicLink(name, target);
    }
}

/* the interfaces of a device the tree already had, by what it is. */
static void present(PCWSTR instance, PCWSTR klass)
{
    static const UCHAR gpu_pdo[] = {3, 6, 8};
    WCHAR target[32];
    if (!_wcsnicmp(instance, L"DISPLAY\\", 8)) {
        ULONG n = g_monitors++;
        pdo(n < 2 ? 0x2e + n : 0x80 + n, target, RTL_NUMBER_OF(target));
        interfaces(instance, target, IF_MONITOR, IF_MONITOR_BRIGHTNESS, IF_MONITOR_VIDEO_OUTPUT, nullptr);
    } else if (!_wcsnicmp(instance, L"ROOT\\BasicDisplay\\", 18)) {
        pdo(0x2, target, RTL_NUMBER_OF(target));
        interfaces(instance, target, IF_DISPLAY_ADAPTER, nullptr, nullptr, nullptr);
    } else if (!_wcsnicmp(instance, L"ROOT\\BasicRender\\", 17)) {
        pdo(0xd, target, RTL_NUMBER_OF(target));
        interfaces(instance, target, IF_DISPLAY_RENDER, IF_DISPLAY_ARRIVAL, nullptr, nullptr);
    } else if (!_wcsnicmp(instance, L"ROOT\\MTTVDD\\", 12)) {
        pdo(0xc, target, RTL_NUMBER_OF(target));
        interfaces(instance, target, IF_DISPLAY_RENDER, IF_DISPLAY_ARRIVAL, IF_DISPLAY_INDIRECT, IF_DISPLAY_ADAPTER);
    } else if (!_wcsnicmp(instance, L"ACPI\\PNP0C0A\\", 13)) {
        pdo(0x21, target, RTL_NUMBER_OF(target));
        interfaces(instance, target, IF_BATTERY, nullptr, nullptr, nullptr);
    } else if (!_wcsnicmp(instance, L"ACPI\\PNP0C0B\\", 13)) {
        pdo(0x20, target, RTL_NUMBER_OF(target));
        interfaces(instance, target, IF_THERMAL_ZONE, nullptr, nullptr, nullptr);
    } else if (!_wcsnicmp(instance, L"PCI\\", 4)) {
        ULONG number;
        PCWSTR a, b, c = nullptr;
        if (!_wcsicmp(klass, DISPLAY_CLASS)) {
            number = gpu_pdo[g_gpus < RTL_NUMBER_OF(gpu_pdo) ? g_gpus : RTL_NUMBER_OF(gpu_pdo) - 1];
            g_gpus++;
            a = IF_DISPLAY_RENDER, b = IF_DISPLAY_ARRIVAL, c = IF_DISPLAY_ADAPTER;
        } else if (!_wcsicmp(klass, NET_CLASS)) {
            number = 4;
            a = IF_NET, b = IF_NDIS_LAN;
        } else if (!_wcsicmp(klass, DISK_CLASS) || !_wcsicmp(klass, STORAGE_CLASS)) {
            /* the storage controller, which is also what cools itself. */
            number = 1;
            a = IF_STORAGE_PORT, b = IF_COOLING;
        } else {
            return;
        }
        _snwprintf(target, RTL_NUMBER_OF(target), L"\\Device\\NTPNP_PCI%04u", number);
        interfaces(instance, target, a, b, c, nullptr);
    }
}

/* the names drivers give their devices for programs to open, each made when mime has the device it names. */
static void names(void)
{
    static const struct {
        PCWSTR Name, Target;
    } table[] = {
        {L"ACPI_ROOT_OBJECT", L"\\Device\\00000012"},
        {L"ahcache", L"\\Device\\ahcache"},
        {L"AppID", L"\\Device\\AppID"},
        {L"AppidEDPPlugin", L"\\Device\\AppidEDPPlugin"},
        {L"BitLocker", L"\\Device\\BitLocker"},
        {L"BootPartition", L"\\Device\\HarddiskVolume1"},
        {L"clfscntrl", L"\\Device\\clfscntrl"},
        {L"CON", L"\\Device\\ConDrv\\Console"},
        {L"CONIN$", L"\\Device\\ConDrv\\CurrentIn"},
        {L"CONOUT$", L"\\Device\\ConDrv\\CurrentOut"},
        {L"DD9ADAAA-2585-41C5-9847-28A19BA9F344", L"\\Device\\DD9ADAAA-2585-41C5-9847-28A19BA9F344"},
        {L"DISPLAY1", L"\\Device\\Video0"},
        {L"DISPLAY2", L"\\Device\\Video1"},
        {L"F70CD4CC-76B0-48DE-817F-4E22CCABCC17", L"\\Device\\F70CD4CC-76B0-48DE-817F-4E22CCABCC17"},
        {L"fsWrap", L"\\Device\\FsWrap"},
        {L"IPSECDOSPDevice", L"\\Device\\IPSECDOSP"},
        {L"kloader", L"\\Device\\kloader"},
        {L"KslD", L"\\Device\\KslD"},
        {L"LLDPCTRL", L"\\Device\\LLDPCTRL"},
        {L"MountPointManager", L"\\Device\\MountPointManager"},
        {L"MpsDevice", L"\\Device\\MPS"},
        {L"NDIS", L"\\Device\\Ndis"},
        {L"NduIoDevice", L"\\Device\\NduIoDevice"},
        {L"Nsi", L"\\Device\\Nsi"},
        {L"NXTIPSECDevice", L"\\Device\\NXTIPSEC"},
        {L"PartmgrControl", L"\\Device\\PartmgrControl"},
        {L"PciControl", L"\\Device\\PciControl"},
        {L"PEAuth", L"\\Device\\PEAuth"},
        {L"Psched", L"\\Device\\Psched"},
        {L"RESOURCE_HUB", L"\\Device\\RESOURCE_HUB"},
        {L"Scsi0:", L"\\Device\\RaidPort0"},
        {L"Spaceport", L"\\Device\\Spaceport"},
        {L"SrpDevice", L"\\Device\\SrpDevice"},
        {L"SystemPartition", L"\\Device\\HarddiskVolume1"},
        {L"Tcp", L"\\Device\\Tcp"},
        {L"UNC", L"\\Device\\Mup"},
        {L"VDRVROOT", L"\\Device\\00000005"},
        {L"VolMgrControl", L"\\Device\\VolMgrControl"},
        {L"vwififlt", L"\\Device\\vwififlt"},
        {L"wdnisdrv", L"\\Device\\wdnisdrv"},
        {L"WfpAle", L"\\Device\\WfpAle"},
        {L"WFPDev", L"\\Device\\WFP"},
        {L"WindowsTrustedRT", L"\\Device\\WindowsTrustedRT"},
        {L"WMIDataDevice", L"\\Device\\WMIDataDevice"},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(table); i++) {
        /* the device is the name after \Device, whatever path follows it. */
        WCHAR device_name[96] = {}, link[96] = L"\\GLOBAL??\\";
        wcscpy(device_name, table[i].Target);
        PWCHAR rest = wcschr(device_name + 8, L'\\');
        if (rest)
            *rest = 0;
        if (!exists(device_name))
            continue;
        wcscat(link, table[i].Name);
        ObCreateSymbolicLink(link, table[i].Target);
    }
    /* the way from a dos path back to the whole namespace: a link to nothing, so what follows starts at the root. */
    ObCreateSymbolicLink(L"\\GLOBAL??\\GLOBALROOT", L"");
}

void IopPublishDeviceLinks(void)
{
    /* acpi owns the numbered devices windows has, and takes the ones made here. */
    UNICODE_STRING acpi = RTL_CONSTANT_STRING(L"\\Driver\\ACPI");
    if (NT_SUCCESS(ObReferenceObjectByName(&acpi, OBJ_CASE_INSENSITIVE, nullptr, 0, IoDriverObjectType, KernelMode, nullptr,
                                           (PVOID *)&g_driver)))
        ObDereferenceObject(g_driver);
    software();
    IopPnpEachDevice(present);
    processors();
    storage();
    names();
}
