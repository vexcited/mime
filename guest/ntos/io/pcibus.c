#include "../ntos.h"
#include "../ex/firmware.h"

PVOID NTAPI MmMapIoSpace(ULONG64 address, SIZE_T size, MEMORY_CACHING_TYPE cache);
void NTAPI MmUnmapIoSpace(PVOID base, SIZE_T size);

#define PCI_ROOT L"ACPI\\PNP0A08\\0"

static IOP_PCI_DEVICE *g_devices;
static ULONG g_count;

/* the hash plug and play gives a parent's instance path, from ntoskrnl's PipMakeGloballyUniqueId. */
static ULONG parent_hash(PCWSTR instance)
{
    ULONG h = 0;
    for (; *instance; instance++)
        h = (ULONG)towupper(*instance) + 37 * h;
    LONG v = (LONG)(h * (ULONG)-314159269);
    if (v < 0)
        v = (LONG)(h * 314159269u);
    return (ULONG)(v % 1000000007);
}

void IopParentIdPrefix(PCWSTR parent, ULONG level, PWCHAR out, ULONG chars)
{
    memset(out, 0, chars * sizeof(WCHAR));
    _snwprintf(out, chars - 1, L"%x&%x&0", level, parent_hash(parent));
}

#if defined(__x86_64__)

#define SYSTEM_CLASS L"{4d36e97d-e325-11ce-bfc1-08002be10318}"
#define HDC_CLASS L"{4d36e96a-e325-11ce-bfc1-08002be10318}"
#define SCSI_CLASS L"{4d36e97b-e325-11ce-bfc1-08002be10318}"
#define DISPLAY_CLASS L"{4d36e968-e325-11ce-bfc1-08002be10318}"
#define USB_CLASS L"{36fc9e60-c465-11cf-8056-444553540000}"
#define STANDARD_SYSTEM L"(Standard system devices)"
#define ANY 0xFF

/*
 * what the inbox infs install by class code, the best of the compatible ids windows matches: machine.inf, pci.inf,
 * display.inf, mshdc.inf, stornvme.inf, storufs.inf, usbxhci.inf, usbport.inf, 1394.inf, hdaudbus.inf, sdbus.inf,
 * pcmcia.inf and i3chost.inf of 26300. a device type before the plain class, as CC_0604&DT_4 ranks above CC_0604.
 */
static const struct {
    UCHAR Class, Subclass, Interface, Port;
    PCWSTR Desc, ClassName, ClassGuid, Mfg, Service;
} g_inbox[] = {
    {0x06, 0x00, ANY, ANY, L"PCI standard host CPU bridge", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, nullptr},
    {0x06, 0x01, ANY, ANY, L"PCI standard ISA bridge", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, L"msisadrv"},
    {0x06, 0x02, ANY, ANY, L"PCI standard EISA bridge", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, L"msisadrv"},
    {0x06, 0x04, ANY, 4, L"PCI Express Root Port", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, L"pci"},
    {0x06, 0x04, ANY, 5, L"PCI Express Upstream Switch Port", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, L"pci"},
    {0x06, 0x04, ANY, 6, L"PCI Express Downstream Switch Port", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, L"pci"},
    {0x06, 0x04, ANY, 7, L"PCI Express to PCI/PCI-X Bridge", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, L"pci"},
    {0x06, 0x04, ANY, 8, L"PCI/PCI-X to PCI Express Bridge", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, L"pci"},
    {0x06, 0x04, ANY, ANY, L"PCI-to-PCI Bridge", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, L"pci"},
    {0x08, 0x07, ANY, 10, L"PCI Express Root Complex Event Collector", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, L"pci"},
    {0x03, 0x00, ANY, ANY, L"Microsoft Basic Display Adapter", L"Display", DISPLAY_CLASS, L"(Standard display types)",
     L"BasicDisplay"},
    {0x03, 0x01, ANY, ANY, L"Microsoft Basic Display Adapter", L"Display", DISPLAY_CLASS, L"(Standard display types)",
     L"BasicDisplay"},
    {0x05, 0x00, ANY, ANY, L"PCI standard RAM Controller", L"System", SYSTEM_CLASS, STANDARD_SYSTEM, nullptr},
    {0x01, 0x01, ANY, ANY, L"Standard Dual Channel PCI IDE Controller", L"hdc", HDC_CLASS,
     L"(Standard IDE ATA/ATAPI controllers)", L"pciide"},
    {0x01, 0x06, 0x01, ANY, L"Standard SATA AHCI Controller", L"hdc", HDC_CLASS, L"Standard SATA AHCI Controller",
     L"storahci"},
    {0x01, 0x08, 0x02, ANY, L"Standard NVM Express Controller", L"SCSIAdapter", SCSI_CLASS,
     L"Standard NVM Express Controller", L"stornvme"},
    {0x01, 0x09, 0x01, ANY, L"Universal Flash Storage (UFS) Host Controller", L"SCSIAdapter", SCSI_CLASS, L"Generic",
     L"storufs"},
    {0x0C, 0x03, 0x30, ANY, L"USB xHCI Compliant Host Controller", L"USB", USB_CLASS, L"Generic USB xHCI Host Controller",
     L"USBXHCI"},
    {0x0C, 0x03, 0x20, ANY, L"Standard Enhanced PCI to USB Host Controller", L"USB", USB_CLASS,
     L"(Standard USB Host Controller)", L"usbehci"},
    {0x0C, 0x03, 0x10, ANY, L"Standard OpenHCD USB Host Controller", L"USB", USB_CLASS, L"(Standard USB Host Controller)",
     L"usbohci"},
    {0x0C, 0x03, 0x00, ANY, L"Standard Universal PCI to USB Host Controller", L"USB", USB_CLASS,
     L"(Standard USB Host Controller)", L"usbuhci"},
    {0x0C, 0x00, 0x10, ANY, L"1394 OHCI Compliant Host Controller", L"1394", L"{6bdd1fc1-810f-11d0-bec7-08002be2092f}",
     L"1394 OHCI Compliant Host Controller", L"1394ohci"},
    {0x0C, 0x0A, 0x00, ANY, L"I3C Host Controller", L"I3C", L"{13cfe1b1-6b17-424c-ac3f-16ace8733898}",
     L"Standard I3C Host Controller", L"I3CHost"},
    {0x04, 0x03, ANY, ANY, L"High Definition Audio Controller", L"System", SYSTEM_CLASS, L"Microsoft", L"HDAudBus"},
    {0x08, 0x05, 0x00, ANY, L"SDA Standard Compliant SD Host Controller", L"SDHost",
     L"{a0a588a4-c46f-4b37-b7ea-c82fe89870c6}", L"SDA Standard Compliant SD Host Controller Vendor", L"sdbus"},
    {0x08, 0x05, 0x01, ANY, L"SDA Standard Compliant SD Host Controller", L"SDHost",
     L"{a0a588a4-c46f-4b37-b7ea-c82fe89870c6}", L"SDA Standard Compliant SD Host Controller Vendor", L"sdbus"},
    {0x06, 0x07, ANY, ANY, L"Generic CardBus Controller", L"PCMCIA", L"{4d36e977-e325-11ce-bfc1-08002be10318}",
     L"Microsoft", L"pcmcia"},
};

/* the text pci.sys gives a function no inf installs, its message class << 8 | subclass, 0x10002 for the rest. */
static const struct {
    USHORT Id;
    PCWSTR Text;
} g_texts[] = {
    {0x0000, L"PCI Device"}, {0x0001, L"VGA Device"}, {0x0100, L"SCSI Controller"}, {0x0101, L"IDE Controller"},
    {0x0102, L"Floppy Controller"}, {0x0103, L"IPI Controller"}, {0x0104, L"RAID Controller"},
    {0x0105, L"ATA Controller"}, {0x0106, L"Serial ATA Controller"}, {0x0107, L"SAS Controller"},
    {0x0180, L"Mass Storage Controller"}, {0x0200, L"Ethernet Controller"}, {0x0201, L"Token Ring Network Controller"},
    {0x0202, L"FDDI Network Controller"}, {0x0203, L"ATM Network Controller"}, {0x0204, L"ISDN Controller"},
    {0x0205, L"WorldFip Controller"}, {0x0280, L"Network Controller"}, {0x0300, L"Video Controller (VGA Compatible)"},
    {0x0301, L"Video Controller (VGA Compatible)"}, {0x0302, L"3D Video Controller"}, {0x0380, L"Video Controller"},
    {0x0400, L"Multimedia Video Controller"}, {0x0401, L"Multimedia Audio Controller"},
    {0x0402, L"Computer Telephony Device"}, {0x0480, L"Multimedia Controller"}, {0x0500, L"PCI Memory"},
    {0x0501, L"PCI FLASH Memory"}, {0x0580, L"PCI Memory Controller"}, {0x0600, L"PCI HOST Bridge"},
    {0x0601, L"PCI to ISA Bridge"}, {0x0602, L"PCI to EISA Bridge"}, {0x0603, L"PCI to Micro Channel Bridge"},
    {0x0604, L"PCI to PCI Bridge"}, {0x0605, L"PCI to PCMCIA Bridge"}, {0x0606, L"PCI to NUBUS Bridge"},
    {0x0607, L"PCI to CARDBUS Bridge"}, {0x0608, L"PCI to RACEway Bridge"},
    {0x0609, L"Semi-transparent PCI to PCI Bridge"}, {0x060A, L"Infiniband to PCI Host Bridge"},
    {0x0680, L"Other PCI Bridge Device"}, {0x0700, L"PCI Serial Port"}, {0x0701, L"PCI Parallel Port"},
    {0x0702, L"PCI Multiport Serial Controller"}, {0x0703, L"PCI Modem"}, {0x0704, L"PCI GPIB Controller"},
    {0x0705, L"PCI Smart Card"}, {0x0780, L"PCI Simple Communications Controller"},
    {0x0800, L"System Interrupt Controller"}, {0x0801, L"System DMA Controller"}, {0x0802, L"System Timer"},
    {0x0803, L"Real Time Clock"}, {0x0804, L"Generic PCI Hot-Plug Controller"}, {0x0805, L"SD Host Controller"},
    {0x0880, L"Base System Device"}, {0x0900, L"PCI Keyboard Controller"}, {0x0901, L"PCI Digitizer"},
    {0x0902, L"PCI Mouse Controller"}, {0x0903, L"PCI Scanner Controller"}, {0x0904, L"PCI Gameport Controller"},
    {0x0980, L"PCI Input Device"}, {0x0A00, L"Docking Station"}, {0x0A80, L"Docking Station (Unknown type)"},
    {0x0B00, L"386 Processor"}, {0x0B01, L"486 Processor"}, {0x0B02, L"Pentium Processor"},
    {0x0B10, L"ALPHA Processor"}, {0x0B20, L"PowerPC Processor"}, {0x0B40, L"Coprocessor"},
    {0x0C00, L"IEEE 1394 Controller"}, {0x0C01, L"ACCESS Bus Controller"}, {0x0C02, L"SSA Controller"},
    {0x0C03, L"Universal Serial Bus (USB) Controller"}, {0x0C04, L"Fibre Channel Controller"},
    {0x0C05, L"SM Bus Controller"}, {0x0C06, L"Infiniband Controller"}, {0x0C07, L"IPMI Interface"},
    {0x0C08, L"SERCOS Interface"}, {0x0C09, L"CANbus Controller"}, {0x0D00, L"iRDA Compatible Controller"},
    {0x0D01, L"Consumer IR Controller"}, {0x0D10, L"RF Controller"}, {0x0D11, L"Bluetooth Controller"},
    {0x0D12, L"Broadband Controller"}, {0x0D20, L"Ethernet 802.11a Controller"},
    {0x0D21, L"Ethernet 802.11b Controller"}, {0x0D80, L"PCI Wireless Controller"},
    {0x0E00, L"Intelligent I/O (I2O) Controller"}, {0x0F01, L"Satellite Communications Television Controller"},
    {0x0F02, L"Satellite Communications Audio Controller"}, {0x0F03, L"Satellite Communications Voice Controller"},
    {0x0F04, L"Satellite Communications Data Controller"},
    {0x1000, L"Network and Computing Encryption/Decryption Controller"},
    {0x1010, L"Entertainment Encryption/Decryption Controller"}, {0x1080, L"PCI Encryption/Decryption Controller"},
    {0x1100, L"DPIO Module"}, {0x1101, L"Performance Counters"},
    {0x1110, L"PCI Communications Synchronization plus Time and Frequency Test/Measurement Controller"},
    {0x1120, L"Management Card"}, {0x1180, L"PCI Data Acquisition and Signal Processing Controller"},
};

static KSPIN_LOCK g_lock;
static ULONG64 g_ecam;
static ULONG g_room;

/* the pair is one access, so no other processor or interrupt may move 0xcf8 between them. */
static ULONG config_read(ULONG bus, ULONG devfn, ULONG offset)
{
    ULONG address = 0x80000000u | bus << 16 | devfn << 8 | (offset & 0xFC), value;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_lock);
    ULONG64 state = KiDisableInterrupts();
    __asm__ volatile("outl %0, %w1" : : "a"(address), "Nd"((USHORT)0xCF8));
    __asm__ volatile("inl %w1, %0" : "=a"(value) : "Nd"((USHORT)0xCFC));
    KiRestoreInterrupts(state);
    KeReleaseSpinLock(&g_lock, irql);
    return value;
}

/* the extended space is only in memory, at the window of the mcfg, and only where no guest memory is. */
static void find_ecam(void)
{
    for (ULONG i = 0; i < KiFirmwareAcpiCount(); i++) {
        ULONG signature, size;
        const UCHAR *table = KiFirmwareAcpi(i, &signature, &size);
        if (!table || signature != 0x4746434Du)
            continue;
        for (ULONG at = 44; at + 16 <= size; at += 16) {
            ULONG64 base;
            USHORT segment;
            memcpy(&base, table + at, sizeof(base));
            memcpy(&segment, table + at + 8, sizeof(segment));
            ULONG64 end = base + (((ULONG64)table[at + 11] + 1) << 20);
            if (!segment && !table[at + 10] && end <= HalBootInfo->ram_base)
                g_ecam = base;
        }
    }
}

/* windows names a function by the serial number of its pcie capability when it has one. */
static BOOLEAN serial_number(ULONG bus, ULONG devfn, PULONG64 serial)
{
    if (!g_ecam)
        return FALSE;
    volatile ULONG *space = MmMapIoSpace(g_ecam + ((ULONG64)bus << 20 | (ULONG64)devfn << 12), PAGE_SIZE, MmNonCached);
    if (!space)
        return FALSE;
    BOOLEAN found = FALSE;
    ULONG at = 0x100;
    for (ULONG n = 0; n < 256 && at >= 0x100 && at < PAGE_SIZE && !found; n++) {
        ULONG header = space[at / 4];
        if (!header || header == 0xFFFFFFFF)
            break;
        if ((header & 0xFFFF) == 3) {
            *serial = space[at / 4 + 1] | (ULONG64)space[at / 4 + 2] << 32;
            found = TRUE;
        }
        at = header >> 20 & 0xFFC;
    }
    MmUnmapIoSpace((PVOID)space, PAGE_SIZE);
    return found;
}

static void append(PWCHAR *at, PWCHAR end, PCWSTR format, ...)
{
    va_list args;
    va_start(args, format);
    if (*at < end - 1) {
        int n = _vsnwprintf(*at, (size_t)(end - *at - 1), format, args);
        *at += n > 0 ? n + 1 : 0;
    }
    va_end(args);
}

/* the ids pci.sys reports, in its order. */
static void identify(IOP_PCI_DEVICE *d)
{
    PWCHAR h = d->HardwareIds, h_end = h + RTL_NUMBER_OF(d->HardwareIds);
    PWCHAR c = d->CompatibleIds, c_end = c + RTL_NUMBER_OF(d->CompatibleIds);
    ULONG v = d->Vendor, dev = d->Device, s = d->Subsystem, r = d->Revision;
    ULONG k = d->Class, sub = d->Subclass, pi = d->Interface;
    BOOLEAN express = d->PortType != ANY;
    append(&h, h_end, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X", v, dev, s, r);
    append(&h, h_end, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X", v, dev, s);
    if (!s) {
        append(&h, h_end, L"PCI\\VEN_%04X&DEV_%04X&REV_%02X", v, dev, r);
        append(&h, h_end, L"PCI\\VEN_%04X&DEV_%04X", v, dev);
    }
    append(&h, h_end, L"PCI\\VEN_%04X&DEV_%04X&CC_%02X%02X%02X", v, dev, k, sub, pi);
    append(&h, h_end, L"PCI\\VEN_%04X&DEV_%04X&CC_%02X%02X", v, dev, k, sub);
    if (s) {
        append(&c, c_end, L"PCI\\VEN_%04X&DEV_%04X&REV_%02X", v, dev, r);
        append(&c, c_end, L"PCI\\VEN_%04X&DEV_%04X", v, dev);
    }
    append(&c, c_end, L"PCI\\VEN_%04X&CC_%02X%02X%02X", v, k, sub, pi);
    append(&c, c_end, L"PCI\\VEN_%04X&CC_%02X%02X", v, k, sub);
    append(&c, c_end, L"PCI\\VEN_%04X", v);
    if (express)
        append(&c, c_end, L"PCI\\CC_%02X%02X%02X&DT_%01X", k, sub, pi, d->PortType);
    append(&c, c_end, L"PCI\\CC_%02X%02X%02X", k, sub, pi);
    if (express)
        append(&c, c_end, L"PCI\\CC_%02X%02X&DT_%01X", k, sub, d->PortType);
    append(&c, c_end, L"PCI\\CC_%02X%02X", k, sub);
}

/*
 * the device id, then the instance pci.sys gives: the serial number and the slot, or the slots from the function up
 * to the root bus after the prefix of its parent.
 */
static void name(IOP_PCI_DEVICE *d)
{
    WCHAR id[64] = {}, prefix[32] = {}, slots[40] = {};
    _snwprintf(id, RTL_NUMBER_OF(id) - 1, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X", d->Vendor, d->Device,
               d->Subsystem, d->Revision);
    if (d->HasSerial) {
        _snwprintf(d->Instance, RTL_NUMBER_OF(d->Instance) - 1, L"%ls\\%ls%016llX%02X", id, d->PortType == 5 ? L"U" : L"",
                   d->Serial, d->Devfn);
        return;
    }
    const IOP_PCI_DEVICE *parent = d->Parent >= 0 ? &g_devices[d->Parent] : nullptr;
    IopParentIdPrefix(parent ? parent->Instance : PCI_ROOT, parent ? parent->Level : 3, prefix, RTL_NUMBER_OF(prefix));
    ULONG length = 0;
    for (const IOP_PCI_DEVICE *p = d; p && length + 2 < RTL_NUMBER_OF(slots); p = p->Parent >= 0 ? &g_devices[p->Parent] : nullptr)
        length += (ULONG)_snwprintf(slots + length, RTL_NUMBER_OF(slots) - 1 - length, L"%02X", p->Devfn);
    _snwprintf(d->Instance, RTL_NUMBER_OF(d->Instance) - 1, L"%ls\\%ls&%ls", id, prefix, slots);
}

static void present(IOP_PCI_DEVICE *d)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_inbox); i++) {
        if (g_inbox[i].Class != d->Class || g_inbox[i].Subclass != d->Subclass ||
            (g_inbox[i].Interface != ANY && g_inbox[i].Interface != d->Interface) ||
            (g_inbox[i].Port != ANY && g_inbox[i].Port != d->PortType))
            continue;
        d->Desc = g_inbox[i].Desc;
        d->ClassName = g_inbox[i].ClassName;
        d->ClassGuid = g_inbox[i].ClassGuid;
        d->Manufacturer = g_inbox[i].Mfg;
        d->Service = g_inbox[i].Service;
        return;
    }
    USHORT id = (USHORT)(d->Class << 8 | d->Subclass);
    d->Desc = L"PCI Device";
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_texts); i++)
        if (g_texts[i].Id == id)
            d->Desc = g_texts[i].Text;
}

static BOOLEAN room(void)
{
    if (g_count < g_room)
        return TRUE;
    ULONG grown = g_room ? g_room * 2 : 32;
    IOP_PCI_DEVICE *list = ExAllocatePool2(POOL_FLAG_NON_PAGED, grown * sizeof(*list), KI_TAG('P', 'c', 'i', 'E'));
    if (!list)
        return FALSE;
    if (g_devices) {
        memcpy(list, g_devices, g_count * sizeof(*list));
        ExFreePool(g_devices);
    }
    g_devices = list;
    g_room = grown;
    return TRUE;
}

static void add(ULONG bus, ULONG devfn, LONG parent)
{
    if (!room())
        return;
    IOP_PCI_DEVICE *d = &g_devices[g_count];
    memset(d, 0, sizeof(*d));
    d->Bus = (UCHAR)bus;
    d->Devfn = (UCHAR)devfn;
    d->Parent = parent;
    d->Level = parent >= 0 ? g_devices[parent].Level + 1 : 4;
    ULONG id = config_read(bus, devfn, 0), klass = config_read(bus, devfn, 8);
    d->Vendor = (USHORT)id;
    d->Device = (USHORT)(id >> 16);
    d->Revision = (UCHAR)klass;
    d->Interface = (UCHAR)(klass >> 8);
    d->Subclass = (UCHAR)(klass >> 16);
    d->Class = (UCHAR)(klass >> 24);
    d->Header = (UCHAR)(config_read(bus, devfn, 0x0C) >> 16 & 0x7F);
    d->PortType = ANY;
    if (d->Header == 0)
        d->Subsystem = config_read(bus, devfn, 0x2C);
    else if (d->Header == 2)
        d->Subsystem = config_read(bus, devfn, 0x40);
    if (config_read(bus, devfn, 4) >> 16 & 0x10) {
        ULONG at = config_read(bus, devfn, d->Header == 2 ? 0x14 : 0x34) & 0xFC;
        for (ULONG n = 0; n < 48 && at >= 0x40; n++) {
            ULONG header = config_read(bus, devfn, at);
            if ((header & 0xFF) == 0x10)
                d->PortType = (UCHAR)(header >> 20 & 0xF);
            else if ((header & 0xFF) == 0x0D && d->Header == 1)
                d->Subsystem = config_read(bus, devfn, at + 4);
            at = header >> 8 & 0xFC;
        }
    }
    if (d->Header == 1)
        d->Secondary = (UCHAR)(config_read(bus, devfn, 0x18) >> 8);
    /* a serial number of zero names nothing, pci.sys passes it over. */
    if (d->PortType != ANY)
        d->HasSerial = serial_number(bus, devfn, &d->Serial) && d->Serial;
    identify(d);
    name(d);
    present(d);
    g_count++;
}

typedef struct PCI_PENDING_BUS {
    UCHAR Bus;
    LONG Bridge;
} PCI_PENDING_BUS;

/* bus 0 first, then the bus behind each bridge as it is found, the order pci.sys gives the functions. */
static void scan(void)
{
    find_ecam();
    PCI_PENDING_BUS queue[256];
    BOOLEAN seen[256] = {TRUE};
    ULONG head = 0, tail = 0;
    queue[tail++] = (PCI_PENDING_BUS){0, -1};
    while (head < tail) {
        ULONG bus = queue[head].Bus;
        LONG bridge = queue[head++].Bridge;
        for (ULONG slot = 0; slot < 32; slot++) {
            if ((config_read(bus, slot << 3, 0) & 0xFFFF) == 0xFFFF)
                continue;
            ULONG functions = config_read(bus, slot << 3, 0x0C) >> 16 & 0x80 ? 8 : 1;
            for (ULONG function = 0; function < functions; function++) {
                ULONG devfn = slot << 3 | function, before = g_count;
                if ((config_read(bus, devfn, 0) & 0xFFFF) == 0xFFFF)
                    continue;
                add(bus, devfn, bridge);
                if (g_count == before)
                    return;
                const IOP_PCI_DEVICE *d = &g_devices[before];
                if (d->Header == 1 && d->Secondary > bus && !seen[d->Secondary]) {
                    seen[d->Secondary] = TRUE;
                    queue[tail++] = (PCI_PENDING_BUS){d->Secondary, (LONG)before};
                }
            }
        }
    }
}

#endif

ULONG IopPciDevices(const IOP_PCI_DEVICE **devices)
{
#if defined(__x86_64__)
    static BOOLEAN scanned;
    if (!scanned) {
        scanned = TRUE;
        scan();
    }
#endif
    *devices = g_devices;
    return g_count;
}
