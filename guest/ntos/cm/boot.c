/*
 * specializes the template hives for this installation, as windows setup
 * does once and the kernel and session manager redo at each boot: version data,
 * machine and user identity, hardware description, processor variables.
 */
#include "cm.h"
#define MIME_KERNEL_PNP
#include <mime/pnp.h>
#include <mime/gpu_identity.h>
#include "../ex/firmware.h"

#define NT_CURRENT_VERSION L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
#define NT_CURRENT_VERSION_32 L"\\REGISTRY\\MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows NT\\CurrentVersion"
#define CONTROL L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control"
#define ENVIRONMENT CONTROL L"\\Session Manager\\Environment"

static CM_NODE *open_key(PCWSTR path)
{
    return CmpOpenPath(path, TRUE);
}

static void set(PCWSTR path, PCWSTR name, ULONG type, const void *data, ULONG length)
{
    CM_NODE *node = open_key(path);
    if (node)
        CmpSetValue(node, name, (ULONG)wcslen(name), type, data, length);
}

static void set_sz(PCWSTR path, PCWSTR name, ULONG type, PCWSTR text)
{
    set(path, name, type, text, (ULONG)(wcslen(text) + 1) * sizeof(WCHAR));
}

static void set_dword(PCWSTR path, PCWSTR name, ULONG value)
{
    set(path, name, REG_DWORD, &value, sizeof(value));
}

static void set_qword(PCWSTR path, PCWSTR name, ULONG64 value)
{
    set(path, name, REG_QWORD, &value, sizeof(value));
}

static void widen(PWCHAR out, ULONG count, PCSTR text)
{
    ULONG i = 0;
    for (; text[i] && i + 1 < count; i++)
        out[i] = (UCHAR)text[i];
    out[i] = 0;
}

#ifdef __aarch64__
/* the data names the x64 system's native folders and build, an arm64 system has its own. */
static void native_names(void)
{
    set_sz(NT_CURRENT_VERSION, L"BuildLabEx", REG_SZ, L"26100.1.arm64fre.ge_release.240331-1435");
    PCWSTR root = L"C:\\Windows\\Microsoft.NET\\FrameworkArm64\\";
    PCWSTR path = L"C:\\Windows\\Microsoft.NET\\FrameworkArm64\\v4.0.30319\\";
    static const PCWSTR ndp = L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\";
    WCHAR key[160];
    set_sz(L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\.NETFramework", L"InstallRoot", REG_SZ, root);
    static const PCWSTR editions[] = {L"v4\\Client", L"v4\\Full"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(editions); i++) {
        _snwprintf(key, RTL_NUMBER_OF(key) - 1, L"%ls%ls", ndp, editions[i]);
        set_sz(key, L"InstallPath", REG_SZ, path);
    }
    _snwprintf(key, RTL_NUMBER_OF(key) - 1, L"%lsCDF\\v4.0", ndp);
    set_sz(key, L"SMSvcHostPath", REG_SZ, path);
    static const PCWSTR current = L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion";
    set_sz(current, L"ProgramFilesDir (Arm)", REG_SZ, L"C:\\Program Files (Arm)");
    set_sz(current, L"CommonFilesDir (Arm)", REG_SZ, L"C:\\Program Files (Arm)\\Common Files");
}
#endif

/* the version of the system, in the key of each view, as 32-bit programs read it from their own. */
static void version_data(PCWSTR user)
{
    static const PCWSTR keys[] = {NT_CURRENT_VERSION, NT_CURRENT_VERSION_32};
    WCHAR build[16] = {};
    _snwprintf(build, RTL_NUMBER_OF(build) - 1, L"%u", MIME_NT_BUILD_NUMBER);
    ULONG64 installed = HalBootInfo->install_filetime;
    for (ULONG i = 0; i < RTL_NUMBER_OF(keys); i++) {
        PCWSTR key = keys[i];
        set_sz(key, L"SystemRoot", REG_SZ, L"C:\\Windows");
        set_sz(key, L"PathName", REG_SZ, L"C:\\Windows");
        set_sz(key, L"CurrentType", REG_SZ, L"Multiprocessor Free");
        set_sz(key, L"CurrentBuild", REG_SZ, build);
        set_sz(key, L"CurrentBuildNumber", REG_SZ, build);
        set_dword(key, L"CurrentMajorVersionNumber", MIME_NT_MAJOR_VERSION);
        set_dword(key, L"CurrentMinorVersionNumber", MIME_NT_MINOR_VERSION);
        set_dword(key, L"InstallDate", (ULONG)(installed / 10000000ull - 11644473600ull));
        set_qword(key, L"InstallTime", installed);
        set_sz(key, L"RegisteredOwner", REG_SZ, user);
        set_sz(key, L"RegisteredOrganization", REG_SZ, L"");
    }
#ifdef __aarch64__
    native_names();
#endif
}

/* where fetched feature packages are staged, on the host share so no mime folder appears on C:. */
static void feature_cache(void)
{
    if (!HalBootInfo->feature_cache[0])
        return;
    WCHAR path[260];
    widen(path, RTL_NUMBER_OF(path), HalBootInfo->feature_cache);
    set_sz(L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsFeatures", L"Cache",
           REG_SZ, path);
}

/* the local security policy values a fresh windows keeps in control\lsa. */
static void security_policy(void)
{
    static const PCWSTR lsa = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Lsa";
    set_dword(lsa, L"LimitBlankPasswordUse", 1);
    set_dword(lsa, L"RestrictAnonymous", 0);
    set_dword(lsa, L"RestrictAnonymousSAM", 1);
    set_dword(lsa, L"EveryoneIncludesAnonymous", 0);
    set_dword(lsa, L"ForceGuest", 0);
    set_dword(lsa, L"NoLmHash", 1);
    set_dword(lsa, L"LmCompatibilityLevel", 5);
    set_dword(lsa, L"DisableDomainCreds", 0);
    set_dword(lsa, L"FullPrivilegeAuditing", 0);
    set_sz(lsa, L"RestrictRemoteSAM", REG_SZ, L"O:BAG:BAD:(A;;RC;;;BA)");
}

static void identity(PCWSTR computer, PCWSTR user, PCWSTR sid)
{
    set_sz(CONTROL L"\\ComputerName\\ComputerName", L"ComputerName", REG_SZ, computer);
    CM_NODE *active = CmpOpenPath(CONTROL L"\\ComputerName", FALSE);
    if (active && !CmpFindChild(active, L"ActiveComputerName", 18))
        CmpCreateChild(active, L"ActiveComputerName", 18, CM_NODE_VOLATILE);
    set_sz(CONTROL L"\\ComputerName\\ActiveComputerName", L"ComputerName", REG_SZ, computer);
    PCWSTR tcpip = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters";
    set_sz(tcpip, L"Hostname", REG_SZ, computer);
    set_sz(tcpip, L"NV Hostname", REG_SZ, computer);

    WCHAR key[160] = {}, profile[96] = {};
    _snwprintf(key, RTL_NUMBER_OF(key) - 1, NT_CURRENT_VERSION L"\\ProfileList\\%ls", sid);
    _snwprintf(profile, RTL_NUMBER_OF(profile) - 1, L"C:\\Users\\%ls", user);
    set_sz(key, L"ProfileImagePath", REG_EXPAND_SZ, profile);
    set_dword(key, L"Flags", 0);
    set_dword(key, L"State", 0);
    set_dword(key, L"FullProfile", 1);
    UCHAR binary[28] = {1, 5, 0, 0, 0, 0, 0, 5};
    ULONG sub[5] = {21, HalBootInfo->user_sid[0], HalBootInfo->user_sid[1], HalBootInfo->user_sid[2], 1001};
    memcpy(binary + 8, sub, sizeof(sub));
    set(key, L"Sid", REG_BINARY, binary, sizeof(binary));
}

/*
 * the profiles of the accounts services run in, which windows setup writes: the folder of each and, for the two service
 * accounts, the hive the profile service loads at start with the place of their temporary files.
 */
static void service_profiles(void)
{
    static const struct {
        PCWSTR Sid;
        UCHAR Id;
        PCWSTR Folder;
        BOOLEAN Hive;
    } accounts[] = {{L"S-1-5-18", 18, L"%systemroot%\\system32\\config\\systemprofile", FALSE},
                    {L"S-1-5-19", 19, L"%systemroot%\\ServiceProfiles\\LocalService", TRUE},
                    {L"S-1-5-20", 20, L"%systemroot%\\ServiceProfiles\\NetworkService", TRUE}};
    for (ULONG i = 0; i < RTL_NUMBER_OF(accounts); i++) {
        WCHAR key[160] = {};
        _snwprintf(key, RTL_NUMBER_OF(key) - 1, NT_CURRENT_VERSION L"\\ProfileList\\%ls", accounts[i].Sid);
        set_sz(key, L"ProfileImagePath", REG_EXPAND_SZ, accounts[i].Folder);
        set_dword(key, L"Flags", 12);
        set_dword(key, L"State", 0);
        UCHAR binary[12] = {1, 1, 0, 0, 0, 0, 0, 5, accounts[i].Id, 0, 0, 0};
        set(key, L"Sid", REG_BINARY, binary, sizeof(binary));
        if (!accounts[i].Hive)
            continue;
        _snwprintf(key, RTL_NUMBER_OF(key) - 1, L"\\REGISTRY\\USER\\%ls\\Environment", accounts[i].Sid);
        set_sz(key, L"TEMP", REG_EXPAND_SZ, L"%USERPROFILE%\\AppData\\Local\\Temp");
        set_sz(key, L"TMP", REG_EXPAND_SZ, L"%USERPROFILE%\\AppData\\Local\\Temp");
    }
}

/*
 * what the State Repository service adds to its cache when the user signs in: a row for the account. the launcher
 * looks the caller up there, and falls back on the DefaultAccount, whose sid it asks the lsa for. the tables are in
 * data/registry/staterepository.reg, rows 1 and 2 there.
 */
static void state_repository_user(PCWSTR sid)
{
    WCHAR key[200] = {}, index[64] = {};
    PCWSTR cache = L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModel\\StateRepository\\Cache";
    _snwprintf(key, RTL_NUMBER_OF(key) - 1, L"%ls\\User\\Data\\3", cache);
    set_sz(key, L"UserSid", REG_SZ, sid);
    ULONG chars = (ULONG)_snwprintf(index, RTL_NUMBER_OF(index) - 2, L"UserSid\\%ls", sid);
    set(key, L"_IndexKeys", REG_MULTI_SZ, index, (chars + 2) * sizeof(WCHAR));
    _snwprintf(key, RTL_NUMBER_OF(key) - 1, L"%ls\\User\\Index\\UserSid\\%ls\\3", cache, sid);
    open_key(key);
}

/* what winlogon writes at logon: the volatile part of the environment. */
static void volatile_environment(PCWSTR computer, PCWSTR user, PCWSTR sid)
{
    WCHAR path[160] = {}, sub[160] = {}, text[160];
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"\\REGISTRY\\USER\\%ls", sid);
    CM_NODE *hive = CmpOpenPath(path, FALSE);
    if (!hive)
        return;
    CM_NODE *ve = CmpFindChild(hive, L"Volatile Environment", 20);
    if (!ve)
        ve = CmpCreateChild(hive, L"Volatile Environment", 20, CM_NODE_VOLATILE);
    if (!ve)
        return;
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"\\REGISTRY\\USER\\%ls\\Volatile Environment", sid);
    memset(text, 0, sizeof(text));
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"\\\\%ls", computer);
    set_sz(path, L"LOGONSERVER", REG_SZ, text);
    set_sz(path, L"USERDOMAIN", REG_SZ, computer);
    set_sz(path, L"USERNAME", REG_SZ, user);
    memset(text, 0, sizeof(text));
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"C:\\Users\\%ls", user);
    set_sz(path, L"USERPROFILE", REG_SZ, text);
    set_sz(path, L"HOMEDRIVE", REG_SZ, L"C:");
    set_sz(path, L"HOMEPATH", REG_SZ, text + 2);
    memset(text, 0, sizeof(text));
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"C:\\Users\\%ls\\AppData\\Roaming", user);
    set_sz(path, L"APPDATA", REG_SZ, text);
    memset(text, 0, sizeof(text));
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"C:\\Users\\%ls\\AppData\\Local", user);
    set_sz(path, L"LOCALAPPDATA", REG_SZ, text);
    set_sz(path, L"USERDOMAIN_ROAMINGPROFILE", REG_SZ, computer);
    if (!CmpFindChild(ve, L"1", 1))
        CmpCreateChild(ve, L"1", 1, CM_NODE_VOLATILE);
    _snwprintf(sub, RTL_NUMBER_OF(sub) - 1, L"%ls\\1", path);
    set_sz(sub, L"SESSIONNAME", REG_SZ, L"Console");
}

/* the notification area has a per-user settings key even before it has saved any icons. */
static void shell_state(PCWSTR sid)
{
    WCHAR path[256];
    _snwprintf(path, RTL_NUMBER_OF(path),
               L"\\REGISTRY\\USER\\%ls\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows"
               L"\\CurrentVersion\\TrayNotify", sid);
    open_key(path);
}

/* HARDWARE\DESCRIPTION, rebuilt by the kernel at every boot. */
static void hardware(void)
{
    WCHAR identifier[96] = {}, vendor[48], name[64];
    widen(vendor, RTL_NUMBER_OF(vendor), KiProcessorVendor());
    widen(name, RTL_NUMBER_OF(name), KiProcessorBrand());
    KiFormatProcessorIdentifier(identifier, RTL_NUMBER_OF(identifier) - 1);
    for (ULONG cpu = 0; cpu < HalBootInfo->cpu_count; cpu++) {
        WCHAR path[128] = {};
        _snwprintf(path, RTL_NUMBER_OF(path) - 1,
                   L"\\REGISTRY\\MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\%u", cpu);
        if (!CmpOpenPath(path, TRUE))
            return;
        set_sz(path, L"Identifier", REG_SZ, identifier);
        set_sz(path, L"VendorIdentifier", REG_SZ, vendor);
        set_sz(path, L"ProcessorNameString", REG_SZ, name);
        if (KiProcessorMhz())
            set_dword(path, L"~MHz", KiProcessorMhz());
        KiSetProcessorRegistryValues(path, set);
    }
}

/* the smbios data, as the kernel fills the volatile hardware key from the firmware tables at boot. */
static void firmware(void)
{
    static const PCWSTR bios = L"\\REGISTRY\\MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS";
    WCHAR manufacturer[64], product[64], family[64], sku[48], vendor[64], version[64], date[16], serial[24];
    widen(manufacturer, RTL_NUMBER_OF(manufacturer), HalBootInfo->system_manufacturer);
    widen(product, RTL_NUMBER_OF(product), HalBootInfo->system_product);
    widen(family, RTL_NUMBER_OF(family), HalBootInfo->system_family);
    widen(sku, RTL_NUMBER_OF(sku), HalBootInfo->system_sku);
    widen(vendor, RTL_NUMBER_OF(vendor), HalBootInfo->bios_vendor);
    widen(version, RTL_NUMBER_OF(version), HalBootInfo->bios_version);
    widen(date, RTL_NUMBER_OF(date), HalBootInfo->bios_date);
    widen(serial, RTL_NUMBER_OF(serial), HalBootInfo->system_serial);
    set_sz(bios, L"SystemManufacturer", REG_SZ, manufacturer);
    set_sz(bios, L"SystemProductName", REG_SZ, product);
    set_sz(bios, L"SystemFamily", REG_SZ, family);
    set_sz(bios, L"SystemSKU", REG_SZ, sku);
    set_sz(bios, L"SystemVersion", REG_SZ, L"1");
    set_sz(bios, L"BIOSVendor", REG_SZ, vendor);
    set_sz(bios, L"BIOSVersion", REG_SZ, version);
    set_sz(bios, L"BIOSReleaseDate", REG_SZ, date);
    set_sz(bios, L"BaseBoardManufacturer", REG_SZ, manufacturer);
    set_sz(bios, L"BaseBoardProduct", REG_SZ, product);
    set_sz(bios, L"BaseBoardVersion", REG_SZ, L"1");
    set_sz(bios, L"SystemSerialNumber", REG_SZ, serial);

    /* the model, which win32_computersystem also reads. */
    static const PCWSTR sysinfo = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\SystemInformation";
    set_sz(sysinfo, L"SystemManufacturer", REG_SZ, manufacturer);
    set_sz(sysinfo, L"SystemProductName", REG_SZ, product);
    set_sz(sysinfo, L"SystemSKU", REG_SZ, sku);
    set_sz(sysinfo, L"SystemVersion", REG_SZ, L"1");
}

#define DISPLAY_DRIVER L"{4d36e968-e325-11ce-bfc1-08002be10318}\\0000"

#if !defined(__aarch64__)

/* the pnp identifiers of a host gpu, PCI\VEN_10DE&DEV_28A1&SUBSYS_0B6F1028&REV_A1. */
static void gpu_hardware_id(const MimeBootGpu *gpu, PWCHAR out, ULONG count)
{
    memset(out, 0, count * sizeof(WCHAR));
    _snwprintf(out, count - 1, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X", gpu->vendor, gpu->device,
               gpu->subsystem, gpu->revision & 0xFF);
}

/* the device key of a host gpu, where its pci location names the instance. */
static void gpu_instance(const MimeBootGpu *gpu, PWCHAR out, ULONG count)
{
    WCHAR hardware[96];
    gpu_hardware_id(gpu, hardware, RTL_NUMBER_OF(hardware));
    memset(out, 0, count * sizeof(WCHAR));
    _snwprintf(out, count - 1, L"%ls\\3&11583659&0&%02X", hardware, gpu->slot & 0xFF);
}

/* the inf section and kernel driver of the vendor's windows package. */
static PCWSTR gpu_inf_section(ULONG vendor)
{
    return vendor == 0x10DE ? L"Section048" : vendor == 0x1002 ? L"ati2mtag_R7000" : L"iigd_dch";
}

static PCWSTR gpu_service(ULONG vendor)
{
    return vendor == 0x10DE ? L"nvlddmkm" : vendor == 0x1002 ? L"amdkmdag" : L"igdkmd64";
}
#endif

#define SERVICES L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Services\\"
#define ENUM L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\"
#define DISPLAY_CLASS_KEY CONTROL L"\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}"
#define MONITOR_CLASS_KEY CONTROL L"\\Class\\{4d36e96e-e325-11ce-bfc1-08002be10318}"
/* the video keys: of the adapter that drives the display, which user32 reads, of the two root drivers and of the indirect display. */
#define VIDEO_ID L"{b3f36b7e-6a4d-5a41-9e2c-4d8fa1c7e903}"
#define BASIC_DISPLAY_VIDEO_ID L"{b3f36b6e-6a4d-5a41-9e2c-4d8fa1c7e903}"
#define BASIC_RENDER_VIDEO_ID L"{b3f36b70-6a4d-5a41-9e2c-4d8fa1c7e903}"
#define BASIC_DISPLAY_INSTANCE L"PCI\\VEN_1234&DEV_1111&SUBSYS_11001AF4&REV_02\\3&11583659&0&18"
#define set_multi(path, name, text) set(path, name, REG_MULTI_SZ, text, sizeof(text))

/*
 * what drives the display: the host gpu, with the driver package of its vendor. a machine with no gpu is as the
 * windows reference machine: its vga device runs windows' basic display adapter and direct3d is the basic render
 * driver.
 */
static BOOLEAN display_basic(void)
{
#if defined(__aarch64__)
    return FALSE;
#else
    return !HalBootInfo->gpu_count;
#endif
}

/*
 * a monitor that tells nothing of itself, as the one of the reference machine's vga device: windows names it
 * Default_Monitor and it has no edid. the basic display has that one.
 */
static BOOLEAN display_plain_monitor(void)
{
    return display_basic();
}

#if !defined(__aarch64__)
/* the host gpu a function of the bus is, nullptr for any other function and behind the basic display adapter. */
static const MimeBootGpu *pci_gpu(const IOP_PCI_DEVICE *d)
{
    for (ULONG i = 0; !display_basic() && i < HalBootInfo->gpu_count && i < MIME_BOOT_GPUS; i++) {
        const MimeBootGpu *gpu = &HalBootInfo->gpus[i];
        if (gpu->bus == d->Bus && (gpu->slot & 0xFF) == d->Devfn && gpu->vendor == d->Vendor && gpu->device == d->Device)
            return gpu;
    }
    return nullptr;
}

/* what windows' basic display adapter drives: the first vga or xga controller, which display.inf matches. */
static const IOP_PCI_DEVICE *pci_basic_display(void)
{
    const IOP_PCI_DEVICE *list;
    ULONG count = IopPciDevices(&list);
    for (ULONG i = 0; display_basic() && i < count; i++)
        if (list[i].Class == 0x03 && list[i].Subclass <= 0x01)
            return &list[i];
    return nullptr;
}

#endif

/* a host whose pci has no vga controller keeps the vga device of the reference machine. */
static PCWSTR basic_display_instance(void)
{
#if !defined(__aarch64__)
    const IOP_PCI_DEVICE *d = pci_basic_display();
    if (d)
        return d->Instance;
#endif
    return BASIC_DISPLAY_INSTANCE;
}

/* the driver key of a device, in its class key and in its video key: what its inf left there. */
static void driver_key(PCWSTR key, PCWSTR desc, PCWSTR provider, PCWSTR date, ULONG64 date_data, PCWSTR version,
                       PCWSTR inf, PCWSTR section, PCWSTR matching, ULONG score)
{
    set_sz(key, L"DriverDesc", REG_SZ, desc);
    set_sz(key, L"ProviderName", REG_SZ, provider);
    set(key, L"DriverDateData", REG_BINARY, &date_data, sizeof(date_data));
    set_sz(key, L"DriverDate", REG_SZ, date);
    set_sz(key, L"DriverVersion", REG_SZ, version);
    set_sz(key, L"InfPath", REG_SZ, inf);
    set_sz(key, L"InfSection", REG_SZ, section);
    set_sz(key, L"MatchingDeviceId", REG_SZ, matching);
    if (score)
        set_dword(key, L"FeatureScore", score);
}

/* the Video key of a video id, which names the service and the driver key of the adapter. */
static void video_key(PCWSTR id, PCWSTR service, PCWSTR desc, ULONG score, PCWSTR driver)
{
    WCHAR path[200] = CONTROL L"\\Video\\";
    wcscat(path, id);
    wcscat(path, L"\\Video");
    set_sz(path, L"Service", REG_SZ, service);
    set_sz(path, L"DeviceDesc", REG_SZ, desc);
    if (score)
        set_dword(path, L"FeatureScore", score);
    set_sz(path, L"Driver", REG_SZ, driver);
}

/*
 * the map of the video devices, in the order the reference machine holds it: the adapter of the display, the root
 * fallback driver, the remote desktop display, and an indirect display after them.
 */
static void video_device_map(void)
{
    static const PCWSTR map = L"\\REGISTRY\\MACHINE\\HARDWARE\\DEVICEMAP\\VIDEO";
    static const ULONG numbers[2] = {0, 1};
    ULONG last = 1;
    set_sz(map, L"\\Device\\Video0", REG_SZ,
           L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\" VIDEO_ID L"\\0000");
    set_dword(map, L"MaxObjectNumber", last);
    set(map, L"ObjectNumberList", REG_BINARY, numbers, (last + 1) * sizeof(ULONG));
    set_sz(map, L"\\Device\\Video1", REG_SZ, L"\\REGISTRY\\MACHINE\\SYSTEM\\ControlSet001\\Services\\BasicDisplay");
    set_sz(map, L"\\Device\\Disc", REG_SZ, L"\\REGISTRY\\Machine\\System\\CurrentControlSet\\Services\\TSDDD\\Device0");
}

/* a kernel driver's service key, with the devices it runs. */
static void driver_service(PCWSTR name, PCWSTR image, ULONG start, PCWSTR group, ULONG tag, PCWSTR first, PCWSTR second)
{
    WCHAR path[160] = SERVICES, sub[176];
    wcscat(path, name);
    set_sz(path, L"ImagePath", REG_EXPAND_SZ, image);
    set_dword(path, L"Type", 1);
    set_dword(path, L"Start", start);
    set_dword(path, L"ErrorControl", 0);
    if (group) {
        set_sz(path, L"Group", REG_SZ, group);
        set_dword(path, L"Tag", tag);
    }
    wcscpy(sub, path);
    wcscat(sub, L"\\Enum");
    ULONG count = (first ? 1 : 0) + (second ? 1 : 0);
    if (first)
        set_sz(sub, L"0", REG_SZ, first);
    if (second)
        set_sz(sub, L"1", REG_SZ, second);
    set_dword(sub, L"Count", count);
    set_dword(sub, L"NextInstance", count);
}

/* an adapter as dxgkrnl records it for the directx database, under its video id. */
static void directx_adapter(PCWSTR id, ULONG type, ULONG luid)
{
    WCHAR path[160] = L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\DirectX\\";
    wcscat(path, id);
    /* the version of d3d10warp.dll, 10.0.26100.9818. */
    set_qword(path, L"DriverVersion", 0x000A000065F4265A);
    set_qword(path, L"UMDVersion", 0x000A000065F4265A);
    set_dword(path, L"AdapterType", type);
    set_dword(path, L"SchemaVersion", 2);
    set_sz(path, L"Description", REG_SZ, L"Microsoft Basic Render Driver");
    set_qword(path, L"AdapterLuid", luid);
    set_dword(path, L"VendorId", 0x1414);
    set_dword(path, L"DeviceId", 0x8C);
    set_qword(path, L"DedicatedVideoMemory", 0);
    set_qword(path, L"DedicatedSystemMemory", 0);
    set_qword(path, L"SharedSystemMemory", (ULONG64)HalBootInfo->ram_size / 2);
    set_dword(path, L"MaxD3D11FeatureLevel", 0xC100);
    set_dword(path, L"MaxD3D12FeatureLevel", 0xC100);
}

/* a monitor: its driver key in the monitor class and its device under the display enumerator. */
static void monitor(PCWSTR number, PCWSTR instance, PCWSTR desc, PCWSTR section, PCWSTR matching, PCWSTR hardware,
                    ULONG hardware_size, PCWSTR friendly, ULONG address, PCWSTR container, PCWSTR parent,
                    const UCHAR *edid, ULONG edid_size)
{
    WCHAR key[200] = MONITOR_CLASS_KEY L"\\", driver[64] = L"{4d36e96e-e325-11ce-bfc1-08002be10318}\\", path[220] = ENUM;
    wcscat(key, number);
    wcscat(driver, number);
    driver_key(key, desc, L"Microsoft", L"6-21-2006", 0x01C694C5A38C8000, L"10.0.26100.9818", L"monitor.inf", section,
               matching, 0);
    set_sz(key, L"MaxResolution", REG_SZ, L"1600,1200");
    /* monitor.inf writes it as text for a plug and play monitor, and as a number for one that is not. */
    if (!wcscmp(section, L"PnPMonitor.Install"))
        set_sz(key, L"DPMS", REG_SZ, L"1");
    else
        set_dword(key, L"DPMS", 1);
    wcscat(key, L"\\MODES");
    open_key(key);
    MIME_DEVICE_PROPERTIES p = {desc, hardware, L"*PNP09FF\0", L"Monitor", L"{4d36e96e-e325-11ce-bfc1-08002be10318}",
                                L"(Standard monitor types)", L"monitor", nullptr, container, friendly};
    MimePnpAddDevice(instance, parent, &p);
    wcscat(path, instance);
    set(path, L"HardwareID", REG_MULTI_SZ, hardware, hardware_size);
    set_dword(path, L"Capabilities", 0xE4);
    set_dword(path, L"Address", address);
    set_dword(path, L"ConfigFlags", 0);
    set_sz(path, L"Driver", REG_SZ, driver);
    /* the monitor driver's list of its devices, in the order they came, which user32 names the monitors of a display by. */
    static ULONG monitors;
    WCHAR order[8] = {};
    _snwprintf(order, RTL_NUMBER_OF(order) - 1, L"%u", monitors++);
    set_sz(SERVICES L"monitor\\Enum", order, REG_SZ, instance);
    set_dword(SERVICES L"monitor\\Enum", L"Count", monitors);
    set_dword(SERVICES L"monitor\\Enum", L"NextInstance", monitors);
    /* where the monitor driver keeps what the monitor told of itself, which programs read to know the screen. */
    wcscat(path, L"\\Device Parameters");
    if (edid)
        set(path, L"EDID", REG_BINARY, edid, edid_size);
    else
        open_key(path);
    /* the framework's key of the monitor driver, empty on the reference machine. */
    wcscat(path, L"\\WDF");
    open_key(path);
}

/*
 * the edid of a host monitor, as a displayport panel of its size tells it: the identifiers user32 names the monitor
 * by, MS_0025, one detailed timing for its mode with the reduced blanking of the cvt standard, its range and its name.
 */
static void monitor_edid(ULONG index, ULONG width, ULONG height, ULONG hz, ULONG width_mm, ULONG height_mm,
                         const USHORT *name, UCHAR edid[128])
{
    static const UCHAR head[] = {
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
        0x36, 0x7F,             /* "MS_", five bits a letter. */
        0x25, 0x00,             /* the product, 0025. */
        0x00, 0x00, 0x00, 0x00, /* the serial number, the monitor's index below. */
        0x01, 0x22,             /* the first week of 2024. */
        0x01, 0x04,             /* edid 1.4. */
        0xA5,                   /* digital, eight bits a color, displayport. */
        0x00, 0x00,             /* the size in centimeters, below. */
        0x78,                   /* gamma 2.2. */
        0x06,                   /* srgb, and the first timing is the native one. */
        0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54, /* the srgb primaries. */
        0x00, 0x00, 0x00,       /* no established timings. */
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    };
    memset(edid, 0, 128);
    memcpy(edid, head, sizeof(head));
    edid[12] = (UCHAR)(index + 1);
    edid[21] = (UCHAR)(width_mm / 10);
    edid[22] = (UCHAR)(height_mm / 10);
    /* a reader derives millimeters from these and rejects a zero, so an unknown physical size still gets one. */
    if (!edid[21])
        edid[21] = 34;
    if (!edid[22])
        edid[22] = 19;
    if (!width || !height || width > 4095 || height > 4095) {
        width = 1920;
        height = 1080;
    }
    if (hz < 24 || hz > 500)
        hz = 60;
    /* reduced blanking: 160 pixels a line, and the lines 460 microseconds take, or the porches and the sync at least. */
    ULONG hblank = 160, hfront = 48, hsync = 32, vfront = 3;
    ULONG vsync = width * 3 == height * 4 ? 4 : width * 10 == height * 16 ? 6 : 5;
    ULONG vblank, clock;
    for (;;) {
        ULONG line_ns = (1000000000 / hz - 460000) / height;
        vblank = 460000 / line_ns + 1;
        if (vblank < vfront + vsync + 6)
            vblank = vfront + vsync + 6;
        clock = (ULONG)((ULONG64)(width + hblank) * (height + vblank) * hz / 10000);
        /* the pixel clock has sixteen bits of ten kilohertz: a mode past it is told at sixty hertz. */
        if (clock <= 0xFFFF || hz == 60)
            break;
        hz = 60;
    }
    if (clock > 0xFFFF)
        clock = 0xFFFF;
    UCHAR *t = edid + 54;
    t[0] = (UCHAR)clock;
    t[1] = (UCHAR)(clock >> 8);
    t[2] = (UCHAR)width;
    t[3] = (UCHAR)hblank;
    t[4] = (UCHAR)((width >> 8) << 4 | hblank >> 8);
    t[5] = (UCHAR)height;
    t[6] = (UCHAR)vblank;
    t[7] = (UCHAR)((height >> 8) << 4 | vblank >> 8);
    t[8] = (UCHAR)hfront;
    t[9] = (UCHAR)hsync;
    t[10] = (UCHAR)(vfront << 4 | vsync);
    t[12] = (UCHAR)width_mm;
    t[13] = (UCHAR)height_mm;
    t[14] = (UCHAR)((width_mm >> 8 & 15) << 4 | (height_mm >> 8 & 15));
    t[17] = 0x1A; /* separate syncs, the horizontal one positive. */
    UCHAR *range = edid + 72;
    ULONG line_khz = clock * 10 / (width + hblank);
    range[3] = 0xFD;
    range[5] = 48;
    range[6] = (UCHAR)(hz > 255 ? 255 : hz);
    range[7] = 30;
    range[8] = (UCHAR)(line_khz + 1 > 255 ? 255 : line_khz + 1);
    range[9] = (UCHAR)(clock / 1000 + 1 > 255 ? 255 : clock / 1000 + 1);
    range[10] = 0x01;
    range[11] = 0x0A;
    memset(range + 12, 0x20, 6);
    UCHAR *label = edid + 90;
    label[3] = 0xFC;
    ULONG chars = 0;
    for (; chars < 13 && name && name[chars] >= 0x20 && name[chars] < 0x7F; chars++)
        label[5 + chars] = (UCHAR)name[chars];
    if (!chars) {
        memcpy(label + 5, "Monitor", 7);
        chars = 7;
    }
    if (chars < 13) {
        label[5 + chars] = 0x0A;
        memset(label + 6 + chars, 0x20, 12 - chars);
    }
    edid[108 + 3] = 0x10;
    UCHAR sum = 0;
    for (ULONG i = 0; i < 127; i++)
        sum += edid[i];
    edid[127] = (UCHAR)(0 - sum);
}

/*
 * the monitors of the host under the adapter that drives them, each with the instance user32 names it by and the
 * edid a real one leaves in its device key. returns how many there are.
 */
static void monitor_friendly(const UCHAR *edid, WCHAR out[32]);
static void monitor_name(const UCHAR *edid, WCHAR name[16]);

static ULONG host_monitors(PCWSTR parent, ULONG level)
{
    WCHAR prefix[32];
    IopParentIdPrefix(parent, level, prefix, RTL_NUMBER_OF(prefix));
    ULONG count = 0;
    MimeMonitor *list = MmAllocateContiguousPages(4096);
    if (list) {
        ULONG64 value = 0;
        memset(list, 0, MIME_MAX_MONITORS * sizeof(MimeMonitor));
        if (HalMimeHypercall(MIME_HC_MONITORS, (ULONG64)MmGetPhysicalAddress(list).QuadPart, MIME_MAX_MONITORS, 0, 0, 0, 0,
                             &value) == MIME_HC_STATUS_OK &&
            value <= MIME_MAX_MONITORS)
            count = (ULONG)value;
    }
    MimeMonitor only = {};
    if (!count) {
        /* before the host has its monitors, the display of the boot information is the one there is. */
        only.rect.right = (int32_t)HalBootInfo->display.width;
        only.rect.bottom = (int32_t)HalBootInfo->display.height;
        only.refresh_hz = HalBootInfo->display.refresh_hz;
        count = 1;
    }
    for (ULONG i = 0; i < count; i++) {
        const MimeMonitor *m = list && !only.rect.right ? &list[i] : &only;
        UCHAR edid[128];
        WCHAR number[8] = {}, instance[64] = {};
        monitor_edid(i, (ULONG)(m->rect.right - m->rect.left), (ULONG)(m->rect.bottom - m->rect.top), m->refresh_hz,
                     m->width_mm, m->height_mm, m->name, edid);
        _snwprintf(number, RTL_NUMBER_OF(number) - 1, L"%04u", i);
        /* the instance ends with the target of the adapter, a plain number, after the prefix of its parent. */
        if (display_plain_monitor()) {
            _snwprintf(instance, RTL_NUMBER_OF(instance) - 1, L"DISPLAY\\Default_Monitor\\%ls&UID%u", prefix, i);
            monitor(number, instance, L"Generic Non-PnP Monitor", L"NonPnPMonitor.Install", L"MONITOR\\Default_Monitor",
                    L"MONITOR\\Default_Monitor\0", sizeof(L"MONITOR\\Default_Monitor\0"), L"Generic Monitor", i,
                    L"{376ddafa-71e0-5880-a232-a4a16069cc71}", parent, nullptr, 0);
            continue;
        }
        /* the panel of the host names the monitor, when the host had one to read. */
        WCHAR name[16] = L"MS_0025", class[24] = L"MONITOR\\MS_0025", friendly[32] = L"Generic PnP Monitor";
        if (HalBootInfo->monitor_edid_size == sizeof(edid) && i == 0) {
            memcpy(edid, HalBootInfo->monitor_edid, sizeof(edid));
            monitor_name(edid, name);
            _snwprintf(class, RTL_NUMBER_OF(class) - 1, L"MONITOR\\%ls", name);
            monitor_friendly(edid, friendly);
            if (!friendly[0])
                memcpy(friendly, L"Generic PnP Monitor", sizeof(L"Generic PnP Monitor"));
        }
        WCHAR cls[26] = {};
        memcpy(cls, class, sizeof(cls));
        _snwprintf(instance, RTL_NUMBER_OF(instance) - 1, L"DISPLAY\\%ls\\%ls&UID%u", name, prefix, i);
        monitor(number, instance, friendly, L"PnPMonitor.Install", L"*PNP09FF", cls, sizeof(cls), L"Generic Monitor", i,
                L"{376ddafa-71e0-5880-a232-a4a16069cc71}", parent, edid, sizeof(edid));
    }
    if (list)
        MmFreeContiguousPages(list, 4096);
    return count;
}

/* the plug and play name of a monitor is its maker, three letters, then the product, as its edid spells them. */
static void monitor_name(const UCHAR *edid, WCHAR name[16])
{
    ULONG maker = (ULONG)edid[8] << 8 | edid[9];
    name[0] = (WCHAR)('A' + ((maker >> 10) & 0x1F) - 1);
    name[1] = (WCHAR)('A' + ((maker >> 5) & 0x1F) - 1);
    name[2] = (WCHAR)('A' + (maker & 0x1F) - 1);
    _snwprintf(name + 3, 12, L"%04X", (ULONG)(edid[10] | edid[11] << 8));
}

/* the description the panel wrote in its edid, the monitor's name in the device manager. */
static void monitor_friendly(const UCHAR *edid, WCHAR out[32])
{
    out[0] = 0;
    for (ULONG at = 0x36; at + 18 <= 0x7E; at += 18) {
        if (edid[at] || edid[at + 1] || edid[at + 2] || edid[at + 3] != 0xFC || edid[at + 4])
            continue;
        ULONG n = 0;
        for (; n < 13 && edid[at + 5 + n]; n++)
            out[n] = edid[at + 5 + n];
        while (n && out[n - 1] == ' ')
            n--;
        out[n] = 0;
        return;
    }
}

/* the two drivers windows always runs on the root bus, whatever drives the display: its fallback and warp's kernel half. */
static void root_display_drivers(void)
{
    static const struct {
        PCWSTR instance, hardware, service, desc, mfg, driver, video;
        ULONG hardware_size;
    } roots[] = {
        {L"ROOT\\BasicDisplay\\0000", L"ROOT\\BasicDisplay\0", L"BasicDisplay",
         L"@basicdisplay.inf,%msbddfallbackname%;Microsoft Basic Display Driver",
         L"@basicdisplay.inf,%std%;(Standard display types)", L"{4d36e97d-e325-11ce-bfc1-08002be10318}\\0021",
         BASIC_DISPLAY_VIDEO_ID, sizeof(L"ROOT\\BasicDisplay\0")},
        {L"ROOT\\BasicRender\\0000", L"ROOT\\BasicRender\0", L"BasicRender",
         L"@basicrender.inf,%basicrender%;Microsoft Basic Render Driver", L"@basicrender.inf,%ms%;Microsoft",
         L"{4d36e97d-e325-11ce-bfc1-08002be10318}\\0022", BASIC_RENDER_VIDEO_ID, sizeof(L"ROOT\\BasicRender\0")},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(roots); i++) {
        MIME_DEVICE_PROPERTIES p = {roots[i].desc, roots[i].hardware, nullptr, L"System",
                                    L"{4d36e97d-e325-11ce-bfc1-08002be10318}", roots[i].mfg, roots[i].service, nullptr,
                                    L"{00000000-0000-0000-FFFF-FFFFFFFFFFFF}", nullptr};
        MimePnpAddDevice(roots[i].instance, nullptr, &p);
        WCHAR path[200] = ENUM;
        wcscat(path, roots[i].instance);
        set(path, L"HardwareID", REG_MULTI_SZ, roots[i].hardware, roots[i].hardware_size);
        set_dword(path, L"ConfigFlags", 0);
        set_dword(path, L"DeviceReported", 1);
        set_dword(path, L"Capabilities", 0);
        set_sz(path, L"Driver", REG_SZ, roots[i].driver);
        wcscat(path, L"\\Device Parameters");
        set_sz(path, L"VideoID", REG_SZ, roots[i].video);
    }
    driver_service(L"BasicDisplay",
                   L"\\SystemRoot\\System32\\DriverStore\\FileRepository\\basicdisplay.inf_amd64_9f34636ebdd89def\\BasicDisplay.sys",
                   1, L"Video", 1, L"ROOT\\BasicDisplay\\0000", display_basic() ? basic_display_instance() : nullptr);
    set_multi(SERVICES L"BasicDisplay", L"Owners", L"display.inf\0basicdisplay.inf\0");
    set_dword(SERVICES L"BasicDisplay\\Parameters", L"SingleDeviceInstall", 1);
    driver_service(L"BasicRender",
                   L"\\SystemRoot\\System32\\DriverStore\\FileRepository\\basicrender.inf_amd64_ff9364d9eff240c3\\BasicRender.sys",
                   1, L"Video", 2, L"ROOT\\BasicRender\\0000", nullptr);
    set_multi(SERVICES L"BasicRender", L"Owners", L"basicrender.inf\0");
    set_dword(SERVICES L"BasicRender\\Parameters", L"SingleDeviceInstall", 1);
    driver_service(L"DXGKrnl", L"System32\\drivers\\dxgkrnl.sys", 0, L"Video Init", 1, nullptr, nullptr);
    set_sz(SERVICES L"DXGKrnl", L"DisplayName", REG_SZ, L"LDDM Graphics Subsystem");
    set_sz(SERVICES L"DXGKrnl", L"Description", REG_SZ,
           L"Controls the underlying video driver stacks to provide fully-featured display capabilities.");
    /* its devices are listed as the monitors come, see monitor(). */
    driver_service(L"monitor", L"\\SystemRoot\\System32\\drivers\\monitor.sys", 3, nullptr, 0, nullptr, nullptr);
    set_dword(SERVICES L"monitor", L"ErrorControl", 1);
    set_sz(SERVICES L"monitor", L"DisplayName", REG_SZ,
           L"@monitor.inf,%Monitor.SVCDESC%;Microsoft Monitor Class Function Driver Service");
    set_multi(SERVICES L"monitor", L"Owners", L"monitor.inf\0");
    /* the video key of the remote desktop display, the same id on every windows. */
    set_dword(CONTROL L"\\Video\\{DEB039CC-B704-4F53-B43E-9DD4432FA2E9}\\0000", L"TSCompatible", 1);
    static const PCWSTR graphics = CONTROL L"\\GraphicsDrivers";
    set_dword(graphics, L"PlatformSupportMiracast", 1);
    set_dword(graphics, L"DxgKrnlVersion", 0x11007);
    set_dword(graphics, L"MinDxgKrnlVersion", 0x5013);
    set_dword(CONTROL L"\\GraphicsDrivers\\DCI", L"Timeout", 7);
    static const PCWSTR display = DISPLAY_CLASS_KEY, monitors = MONITOR_CLASS_KEY;
    set_sz(display, L"Class", REG_SZ, L"Display");
    set_sz(display, L"ClassDesc", REG_SZ, L"@c_display.inf,%ClassDesc%;Display adapters");
    set_multi(display, L"IconPath", L"%SystemRoot%\\system32\\setupapi.dll,-1\0");
    set_sz(display, L"LowerLogoVersion", REG_SZ, L"6.0");
    set_sz(monitors, L"Class", REG_SZ, L"Monitor");
    set_sz(monitors, L"ClassDesc", REG_SZ, L"@c_monitor.inf,%ClassDesc%;Monitors");
    set_multi(monitors, L"IconPath", L"%SystemRoot%\\system32\\setupapi.dll,-35\0");
    set_sz(monitors, L"NoInstallClass", REG_SZ, L"1");
    set_sz(monitors, L"SilentInstall", REG_SZ, L"1");
    set_sz(monitors, L"Default Service", REG_SZ, L"monitor");
}

/* windows' basic display adapter on the machine's vga device, its monitor, and the adapters of the directx database. */
static void basic_display(void)
{
    static const PCWSTR keys[] = {DISPLAY_CLASS_KEY L"\\0000", CONTROL L"\\Video\\" VIDEO_ID L"\\0000"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(keys); i++) {
        driver_key(keys[i], L"Microsoft Basic Display Adapter", L"Microsoft", L"6-21-2006", 0x01C694C5A38C8000,
                   L"10.0.26100.1", L"display.inf", L"MSBDA", L"PCI\\CC_0300", 0xFB);
        set_multi(keys[i], L"IncludedInfs", L"basicdisplay.inf\0");
        set_sz(keys[i], L"HardwareInformation.ChipType", REG_SZ, L"");
        set_sz(keys[i], L"HardwareInformation.DacType", REG_SZ, L"");
        set_sz(keys[i], L"HardwareInformation.AdapterString", REG_SZ, L"Microsoft Basic Display Adapter");
        set_sz(keys[i], L"HardwareInformation.BiosString", REG_SZ, L"");
        set_dword(keys[i], L"HardwareInformation.MemorySize", 0);
    }
    video_key(VIDEO_ID, L"BasicDisplay", L"@display.inf,%msbdaname%;Microsoft Basic Display Adapter", 0xFB, DISPLAY_DRIVER);
    static const PCWSTR service_video = SERVICES L"BasicDisplay\\Video";
    set_sz(service_video, L"Service", REG_SZ, L"BasicDisplay");
    set_sz(service_video, L"DeviceDesc", REG_SZ, L"@display.inf,%msbdaname%;Microsoft Basic Display Adapter");
    set_dword(service_video, L"FeatureScore", 0xFB);
    set_sz(service_video, L"VideoID", REG_SZ, VIDEO_ID);
    set_dword(CONTROL L"\\GraphicsDrivers\\BasicDisplay", L"BasicDisplayUserNotified", 1);
    video_device_map();
    /* the luids dxgi gives these adapters, see its main.c. */
    directx_adapter(VIDEO_ID, 0x10A, 0xD3D9);
    directx_adapter(BASIC_RENDER_VIDEO_ID, 0x105, 0xD3D8);
}

/*
 * the display class key and the video key of the adapter, where its device key leads: the driver description and
 * the identifiers a device reader of the class finds. on x64 that is the host gpu dxgi lists first, and windows'
 * basic display adapter on a host without one.
 */
static void display_adapter(void)
{
#if !defined(__aarch64__)
    root_display_drivers();
    if (display_basic()) {
        basic_display();
        return;
    }
#endif
    static const WCHAR class_key[] = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class"
                                    L"\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000";
    static const WCHAR driver_video_key[] = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Video"
                                    L"\\{b3f36b7e-6a4d-5a41-9e2c-4d8fa1c7e903}\\0000";
    static const PCWSTR keys[] = {class_key, driver_video_key};
#if !defined(__aarch64__)
    const MimeBootGpu *gpu = &HalBootInfo->gpus[0];
    WCHAR name[128], version[48] = {}, hardware[96];
    USHORT parts[4];
    MimeGpuName(gpu->name, name, RTL_NUMBER_OF(name));
    MimeGpuDriverVersion(gpu->vendor, gpu->driver_version, parts);
    _snwprintf(version, RTL_NUMBER_OF(version) - 1, L"%u.%u.%u.%u", parts[0], parts[1], parts[2], parts[3]);
    gpu_hardware_id(gpu, hardware, RTL_NUMBER_OF(hardware));
    /* what every adapter of the reference machine has beside its driver key: the Video key of its id and its place in the map. */
    video_key(VIDEO_ID, gpu_service(gpu->vendor), name, 0, DISPLAY_DRIVER);
    video_device_map();
#endif
    for (ULONG i = 0; i < RTL_NUMBER_OF(keys); i++) {
        if (!CmpOpenPath(keys[i], TRUE))
            continue;
#if defined(__aarch64__)
        set_sz(keys[i], L"DriverDesc", REG_SZ, L"Qualcomm(R) Adreno(TM) X1-85 GPU");
        set_sz(keys[i], L"ProviderName", REG_SZ, L"Qualcomm");
        set_sz(keys[i], L"DriverVersion", REG_SZ, L"31.0.63.7");
        set_sz(keys[i], L"DriverDate", REG_SZ, L"6-12-2025");
        set_sz(keys[i], L"MatchingDeviceId", REG_SZ, L"ACPI\\VEN_QCOM&DEV_0C36&SUBSYS_10280C69&REV_0086");
        set_sz(keys[i], L"InfPath", REG_SZ, L"oem12.inf");
        set_sz(keys[i], L"InfSection", REG_SZ, L"qcdx8380_DevDesc");
#else
        set_sz(keys[i], L"DriverDesc", REG_SZ, name);
        set_sz(keys[i], L"ProviderName", REG_SZ, MimeGpuProvider(gpu->vendor));
        set_sz(keys[i], L"DriverVersion", REG_SZ, version);
        set_sz(keys[i], L"DriverDate", REG_SZ, L"6-12-2025");
        set_sz(keys[i], L"MatchingDeviceId", REG_SZ, hardware);
        set_sz(keys[i], L"InfPath", REG_SZ, L"oem4.inf");
        set_sz(keys[i], L"InfSection", REG_SZ, gpu_inf_section(gpu->vendor));
        set_qword(keys[i], L"HardwareInformation.qwMemorySize", gpu->memory);
        set(keys[i], L"HardwareInformation.AdapterString", REG_BINARY, name, (ULONG)(wcslen(name) + 1) * sizeof(WCHAR));
#endif
        /* the manifests of the vulkan driver, where the khronos loader a program ships looks for them. */
        static const WCHAR icd[] = L"C:\\Windows\\System32\\vulkan-icd.json\0";
        static const WCHAR icd_wow[] = L"C:\\Windows\\SysWOW64\\vulkan-icd.json\0";
        set(keys[i], L"VulkanDriverName", REG_MULTI_SZ, icd, sizeof(icd));
        set(keys[i], L"VulkanDriverNameWoW", REG_MULTI_SZ, icd_wow, sizeof(icd_wow));
    }
}

/* windows setup draws this guid at install time. */
static void machine_guid(void)
{
    static const WCHAR hex[] = L"0123456789abcdef";
    WCHAR text[40] = {};
    ULONG pos = 0;
    for (ULONG i = 0; i < 16 && pos + 2 < RTL_NUMBER_OF(text); i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            text[pos++] = L'-';
        text[pos++] = hex[HalBootInfo->system_uuid[i] >> 4];
        text[pos++] = hex[HalBootInfo->system_uuid[i] & 0xF];
    }
    set_sz(L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", REG_SZ, text);
    set_sz(L"\\REGISTRY\\MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Cryptography", L"MachineGuid", REG_SZ, text);
}

/* in the proleptic Gregorian calendar. */
static ULONG year_of(ULONG64 filetime)
{
    LONG64 days = (LONG64)(filetime / 864000000000ull) - 134774 + 719468; /* days since 0000-03-01 */
    LONG64 era = days / 146097;
    LONG64 day_of_era = days - era * 146097;
    LONG64 year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    LONG64 day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    LONG64 month = (5 * day_of_year + 2) / 153;
    return (ULONG)(year_of_era + era * 400 + (month >= 10 ? 1 : 0));
}

/*
 * the machine's time zone, which setup asks for: the rules of the Windows zone the host is in, copied from the
 * template's zone key into the key the system reads.
 */
static void time_zone_for_year(ULONG year)
{
    if (!HalBootInfo->timezone_key[0])
        return;
    WCHAR key[64], path[220] = {};
    widen(key, RTL_NUMBER_OF(key), HalBootInfo->timezone_key);
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, NT_CURRENT_VERSION L"\\Time Zones\\%ls", key);
    CM_NODE *zone = CmpOpenPath(path, FALSE);
    if (!zone || !NT_SUCCESS(CmpLoadValues(zone)))
        return;
    /*
     * the rules in force this year. for a zone that changed its rules, TZI holds the oldest ones, years with their
     * own rules are values of Dynamic DST, and the last of them applies to every later year.
     */
    CM_VALUE *tzi = CmpFindValue(zone, L"TZI", 3);
    CM_NODE *dynamic = CmpFindChild(zone, L"Dynamic DST", 11);
    if (dynamic && NT_SUCCESS(CmpLoadValues(dynamic))) {
        CM_VALUE *first = CmpFindValue(dynamic, L"FirstEntry", 10), *last = CmpFindValue(dynamic, L"LastEntry", 9);
        if (first && last && first->Type == REG_DWORD && last->Type == REG_DWORD && first->DataLength >= 4 &&
            last->DataLength >= 4) {
            ULONG low = *(ULONG *)first->Data, high = *(ULONG *)last->Data;
            year = year < low ? low : year > high ? high : year;
            WCHAR name[8] = {};
            _snwprintf(name, RTL_NUMBER_OF(name) - 1, L"%u", year);
            CM_VALUE *yearly = CmpFindValue(dynamic, name, (ULONG)wcslen(name));
            if (yearly && yearly->Type == REG_BINARY && yearly->DataLength >= 44)
                tzi = yearly;
        }
    }
    if (!tzi || tzi->DataLength < 44)
        return;
    static const PCWSTR info = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation";
    const LONG *rules = (const LONG *)tzi->Data;
    CM_VALUE *standard = CmpFindValue(zone, L"Std", 3), *daylight = CmpFindValue(zone, L"Dlt", 3);
    set_dword(info, L"Bias", (ULONG)rules[0]);
    set_dword(info, L"StandardBias", (ULONG)rules[1]);
    set_dword(info, L"DaylightBias", (ULONG)rules[2]);
    set(info, L"StandardStart", REG_BINARY, (const UCHAR *)tzi->Data + 12, 16);
    set(info, L"DaylightStart", REG_BINARY, (const UCHAR *)tzi->Data + 28, 16);
    if (standard && standard->Type == REG_SZ)
        set(info, L"StandardName", REG_SZ, standard->Data, standard->DataLength);
    if (daylight && daylight->Type == REG_SZ)
        set(info, L"DaylightName", REG_SZ, daylight->Data, daylight->DataLength);
    set_sz(info, L"TimeZoneKeyName", REG_SZ, key);
    set_dword(info, L"DynamicDaylightTimeDisabled", 0);
    /* the host's current offset, the zone bias plus its standard or daylight bias. ex/timezone.c keeps it up to date. */
    set_dword(info, L"ActiveTimeBias", (ULONG)HalBootInfo->timezone_bias);
}

static void time_zone(void)
{
    time_zone_for_year(year_of(KeBootFiletime));
}

/* a new year may have different rules: zones that changed theirs keep one set per year. */
void CmRefreshTimeZoneRules(ULONG year)
{
    ExAcquireFastMutex(&CmpLock);
    time_zone_for_year(year);
    ExReleaseFastMutex(&CmpLock);
}

/*
 * the installation language the host chose: system and user locale and their code pages. the language of the
 * installed system itself stays English.
 */
static void locale(PCWSTR sid)
{
    WCHAR text[16], name[40], hex8[16], path[160] = {};
    ULONG lcid = HalBootInfo->locale_lcid ? HalBootInfo->locale_lcid : 0x0409;
    widen(name, RTL_NUMBER_OF(name), HalBootInfo->locale_name[0] ? HalBootInfo->locale_name : "en-US");
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%04x", lcid & 0xFFFF);
    set_sz(CONTROL L"\\Nls\\Language", L"Default", REG_SZ, text);
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%u", HalBootInfo->ansi_code_page ? HalBootInfo->ansi_code_page : 1252);
    set_sz(CONTROL L"\\Nls\\CodePage", L"ACP", REG_SZ, text);
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%u", HalBootInfo->oem_code_page ? HalBootInfo->oem_code_page : 437);
    set_sz(CONTROL L"\\Nls\\CodePage", L"OEMCP", REG_SZ, text);
    _snwprintf(hex8, RTL_NUMBER_OF(hex8) - 1, L"%08x", lcid & 0xFFFF);
    set_sz(L"\\REGISTRY\\USER\\.DEFAULT\\Control Panel\\International", L"LocaleName", REG_SZ, name);
    set_sz(L"\\REGISTRY\\USER\\.DEFAULT\\Control Panel\\International", L"Locale", REG_SZ, hex8);
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"\\REGISTRY\\USER\\%ls\\Control Panel\\International", sid);
    set_sz(path, L"LocaleName", REG_SZ, name);
    set_sz(path, L"Locale", REG_SZ, hex8);
}

/* the processor variables the session manager writes at boot. */
static void processor_environment(void)
{
    WCHAR text[160] = {}, identifier[96] = {}, vendor[48];
    widen(vendor, RTL_NUMBER_OF(vendor), KiProcessorVendor());
    set_sz(ENVIRONMENT, L"PROCESSOR_ARCHITECTURE", REG_SZ, MIME_NATIVE_ARCHITECTURE_NAME);
    KiFormatProcessorIdentifier(identifier, RTL_NUMBER_OF(identifier) - 1);
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%ls, %ls", identifier, vendor);
    set_sz(ENVIRONMENT, L"PROCESSOR_IDENTIFIER", REG_SZ, text);
    memset(text, 0, sizeof(text));
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%u", KiProcessorLevel());
    set_sz(ENVIRONMENT, L"PROCESSOR_LEVEL", REG_SZ, text);
    memset(text, 0, sizeof(text));
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%04x", KiProcessorRevision());
    set_sz(ENVIRONMENT, L"PROCESSOR_REVISION", REG_SZ, text);
    memset(text, 0, sizeof(text));
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%u", HalBootInfo->cpu_count);
    set_sz(ENVIRONMENT, L"NUMBER_OF_PROCESSORS", REG_SZ, text);
}

/* the table headers of the firmware, under HARDWARE\ACPI as acpi.sys keeps them, one key for each table and
 * the four byte words of its header, so a reader of the registry finds the same oem ids as the tables. */
static void acpi_description(void)
{
    static const WCHAR root[] = L"\\REGISTRY\\MACHINE\\HARDWARE\\ACPI";
    CmpOpenPath(root, TRUE);
    for (ULONG i = 0; i < KiFirmwareAcpiCount(); i++) {
        ULONG signature_word, size;
        const UCHAR *data = KiFirmwareAcpi(i, &signature_word, &size);
        if (!data || size < 36)
            continue;
        char signature[5] = {};
        memcpy(signature, &signature_word, 4);
        WCHAR wide_sig[5], oem[7], table_id[9];
        widen(wide_sig, RTL_NUMBER_OF(wide_sig), signature);
        widen(oem, RTL_NUMBER_OF(oem), (const char *)data + 10);
        widen(table_id, RTL_NUMBER_OF(table_id), (const char *)data + 16);
        /* the instance, then the data key, where the four byte words of the header are named by their offset. */
        WCHAR instance[180] = {};
        _snwprintf(instance, RTL_NUMBER_OF(instance) - 1, L"%ls\\%ls\\%ls\\%ls\\00000001", root, wide_sig, oem, table_id);
        WCHAR path[200] = {};
        _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"%ls\\00000000", instance);
        if (!CmpOpenPath(path, TRUE))
            continue;
        for (ULONG at = 0; at + 4 <= 36; at += 4) {
            WCHAR name[9];
            _snwprintf(name, RTL_NUMBER_OF(name), L"%08x", at);
            ULONG word;
            memcpy(&word, data + at, sizeof(word));
            set_dword(path, name, word);
        }
    }
}

#define SYSTEM_CLASS L"{4d36e97d-e325-11ce-bfc1-08002be10318}"
#define DISPLAY_CLASS L"{4d36e968-e325-11ce-bfc1-08002be10318}"
#define DISK_CLASS L"{4d36e967-e325-11ce-bfc1-08002be10318}"
#define MEDIA_CLASS L"{4d36e96c-e325-11ce-bfc1-08002be10318}"
#define NET_CLASS L"{4d36e972-e325-11ce-bfc1-08002be10318}"
#define BATTERY_CLASS L"{72631e54-78a4-11d0-bcf7-00aa00b7b32a}"
#define KEYBOARD_CLASS L"{4d36e96b-e325-11ce-bfc1-08002be10318}"
#define MOUSE_CLASS L"{4d36e96f-e325-11ce-bfc1-08002be10318}"

/* the instances raw input names its keyboard and mouse by, see win32k's rawinput.c. */
#define KEYBOARD_INSTANCE L"ACPI\\PNP0303\\4&38a0320a&0"
#define MOUSE_INSTANCE L"ACPI\\PNP0F13\\4&38a0320a&0"
static const GUID GUID_DEVINTERFACE_KEYBOARD = {0x884b96c3, 0x56ef, 0x11d1, {0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd}};
static const GUID GUID_DEVINTERFACE_MOUSE = {0x378de44c, 0x56ef, 0x11d1, {0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd}};

static void pnp(PCWSTR instance, PCWSTR parent, PCWSTR desc, PCWSTR hardware, PCWSTR klass, PCWSTR guid,
                PCWSTR manufacturer, PCWSTR service, PCWSTR location)
{
    MIME_DEVICE_PROPERTIES p = {desc, hardware, hardware, klass, guid, manufacturer, service, location, L"",
                                desc};
    MimePnpAddDevice(instance, parent, &p);
}

#if !defined(__aarch64__)
/*
 * a function of the host's pci, under the bridge it is behind. a host gpu has the driver package of its vendor and
 * the first one the display key's, the basic display adapter drives the vga controller of a host without one, and
 * every other function has what the inbox inf of its class installs, or nothing.
 */
static void pci_device(const IOP_PCI_DEVICE *list, const IOP_PCI_DEVICE *d, PCWSTR root)
{
    WCHAR location[64] = {}, name[128];
    _snwprintf(location, RTL_NUMBER_OF(location) - 1, L"PCI bus %u, device %u, function %u", d->Bus, d->Devfn >> 3,
               d->Devfn & 7);
    MIME_DEVICE_PROPERTIES p = {d->Desc, d->HardwareIds, d->CompatibleIds, d->ClassName, d->ClassGuid,
                                d->Manufacturer, d->Service, location, L"{00000000-0000-0000-ffff-ffffffffffff}",
                                nullptr};
    const MimeBootGpu *gpu = pci_gpu(d);
    BOOLEAN basic = d == pci_basic_display();
    if (gpu) {
        MimeGpuName(gpu->name, name, RTL_NUMBER_OF(name));
        p.DeviceDesc = p.FriendlyName = name;
        p.Manufacturer = MimeGpuProvider(gpu->vendor);
        p.Service = gpu_service(gpu->vendor);
    } else if (basic) {
        p.DeviceDesc = L"Microsoft Basic Display Adapter";
        p.Manufacturer = L"(Standard display types)";
        p.Service = L"BasicDisplay";
    }
    if (gpu || basic) {
        p.Class = L"Display";
        p.ClassGuid = DISPLAY_CLASS;
    }
    MimePnpAddDevice(d->Instance, d->Parent >= 0 ? list[d->Parent].Instance : root, &p);
    WCHAR path[220] = ENUM;
    wcscat(path, d->Instance);
    set_dword(path, L"Address", (ULONG)(d->Devfn >> 3) << 16 | (d->Devfn & 7));
    set_dword(path, L"Capabilities", 0);
    /* what windows leaves on a device it found no driver for. */
    set_dword(path, L"ConfigFlags", p.ClassGuid ? 0 : 0x40);
    if (gpu != HalBootInfo->gpus && !basic)
        return;
    set_sz(path, L"Driver", REG_SZ, DISPLAY_DRIVER);
    wcscat(path, L"\\Device Parameters");
    set_sz(path, L"VideoID", REG_SZ, VIDEO_ID);
    host_monitors(d->Instance, d->Level);
}

static BOOLEAN gpu_on_bus(const MimeBootGpu *gpu)
{
    const IOP_PCI_DEVICE *list;
    ULONG count = IopPciDevices(&list);
    for (ULONG i = 0; i < count; i++)
        if (pci_gpu(&list[i]) == gpu)
            return TRUE;
    return FALSE;
}
#endif

/* the host's gpus the bus does not have, when the host could not read its pci: under the root, where the host has them. */
static void host_gpus(PCWSTR root)
{
#if !defined(__aarch64__)
    if (display_basic())
        return;
    for (ULONG i = 0; i < HalBootInfo->gpu_count && i < MIME_BOOT_GPUS; i++) {
        const MimeBootGpu *gpu = &HalBootInfo->gpus[i];
        if (gpu_on_bus(gpu))
            continue;
        WCHAR instance[160], hardware[100] = {}, name[128], location[64] = {};
        gpu_instance(gpu, instance, RTL_NUMBER_OF(instance));
        gpu_hardware_id(gpu, hardware, RTL_NUMBER_OF(hardware) - 1);
        MimeGpuName(gpu->name, name, RTL_NUMBER_OF(name));
        _snwprintf(location, RTL_NUMBER_OF(location) - 1, L"PCI bus %u, device %u, function %u", gpu->bus,
                   gpu->slot >> 3, gpu->slot & 7);
        pnp(instance, root, name, hardware, L"Display", DISPLAY_CLASS, MimeGpuProvider(gpu->vendor),
            gpu_service(gpu->vendor), location);
        if (i == 0) {
            host_monitors(instance, 4);
            WCHAR path[220] = ENUM;
            wcscat(path, instance);
            set_sz(path, L"Driver", REG_SZ, L"{4d36e968-e325-11ce-bfc1-08002be10318}\\0000");
            wcscat(path, L"\\Device Parameters");
            set_sz(path, L"VideoID", REG_SZ, VIDEO_ID);
        }
    }
#endif
}

/*
 * the vga device of the reference machine under windows' basic display adapter, with the monitor it cannot identify,
 * for a host whose pci has no vga controller to drive.
 */
static void basic_display_devices(PCWSTR root)
{
#if !defined(__aarch64__)
    if (!display_basic() || pci_basic_display())
        return;
    static const WCHAR hardware[] = L"PCI\\VEN_1234&DEV_1111&SUBSYS_11001AF4&REV_02\0PCI\\VEN_1234&DEV_1111&SUBSYS_11001AF4\0"
                                    L"PCI\\VEN_1234&DEV_1111&CC_030000\0PCI\\VEN_1234&DEV_1111&CC_0300\0";
    static const WCHAR compatible[] = L"PCI\\VEN_1234&DEV_1111&REV_02\0PCI\\VEN_1234&DEV_1111\0PCI\\VEN_1234&CC_030000\0"
                                      L"PCI\\VEN_1234&CC_0300\0PCI\\VEN_1234\0PCI\\CC_030000\0PCI\\CC_0300\0";
    MIME_DEVICE_PROPERTIES vga = {L"Microsoft Basic Display Adapter", hardware, compatible, L"Display", DISPLAY_CLASS,
                                  L"(Standard display types)", L"BasicDisplay", L"PCI bus 0, device 3, function 0",
                                  L"{00000000-0000-0000-ffff-ffffffffffff}", nullptr};
    MimePnpAddDevice(BASIC_DISPLAY_INSTANCE, root, &vga);
    static const PCWSTR path = ENUM BASIC_DISPLAY_INSTANCE;
    set(path, L"HardwareID", REG_MULTI_SZ, hardware, sizeof(hardware));
    set(path, L"CompatibleIDs", REG_MULTI_SZ, compatible, sizeof(compatible));
    set_dword(path, L"Capabilities", 0);
    set_dword(path, L"ConfigFlags", 0);
    set_dword(path, L"Address", 0x30000);
    set_sz(path, L"Driver", REG_SZ, DISPLAY_DRIVER);
    set_sz(ENUM BASIC_DISPLAY_INSTANCE L"\\Device Parameters", L"VideoID", REG_SZ, VIDEO_ID);
    /* the reference machine's vga device has a monitor with no edid, and so have the host's monitors under it. */
    host_monitors(BASIC_DISPLAY_INSTANCE, 4);
#endif
}

/*
 * the device tree as the firmware presents it: the pci root bridge, the functions of the pci, the battery and thermal
 * zone of a laptop. on x64 the functions are the host's, as the guest reads them from the bus; arm64 keeps those of
 * the reference machine. mime has no acpi or pci driver, so these are the enumeration a program reads; the drivers
 * of the devices themselves are win32k's or the host's own services.
 */
static void devices(void)
{
    static const WCHAR root[] = L"ACPI\\PNP0A08\\0";
    pnp(root, nullptr, L"PCI Express Root Complex", L"ACPI\\PNP0A08\0ACPI\\PNP0A03\0", L"System", SYSTEM_CLASS,
        L"Microsoft", L"pci", L"PCI bus 0");
#if !defined(__aarch64__)
    const IOP_PCI_DEVICE *list;
    ULONG count = IopPciDevices(&list);
    for (ULONG i = 0; i < count; i++)
        pci_device(list, &list[i], root);
    basic_display_devices(root);
    host_gpus(root);
#else
    pnp(L"PCI\\VEN_8086&DEV_7E00&SUBSYS_00000000&REV_01\\3&11583659&0&00", root, L"Intel(R) Host Bridge",
        L"PCI\\VEN_8086&DEV_7E00&SUBSYS_00000000&REV_01\0", L"System", SYSTEM_CLASS, L"Intel", L"", L"PCI bus 0, device 0, function 0");
    pnp(L"PCI\\VEN_8086&DEV_7E28&SUBSYS_00000000&REV_01\\3&11583659&0&1F", root, L"Intel(R) LPC Controller",
        L"PCI\\VEN_8086&DEV_7E28&SUBSYS_00000000&REV_01\0", L"System", SYSTEM_CLASS, L"Intel", L"msisadrv",
        L"PCI bus 0, device 31, function 0");
    pnp(L"PCI\\VEN_8086&DEV_7E22&SUBSYS_00000000&REV_01\\3&11583659&0&1F4", root, L"Intel(R) SMBus Controller",
        L"PCI\\VEN_8086&DEV_7E22&SUBSYS_00000000&REV_01\0", L"System", SYSTEM_CLASS, L"Intel", L"", L"PCI bus 0, device 31, function 4");
    pnp(L"PCI\\VEN_8086&DEV_7E40&SUBSYS_00000000&REV_01\\3&11583659&0&14", root, L"Intel(R) Wi-Fi 7 BE200 320MHz",
        L"PCI\\VEN_8086&DEV_7E40&SUBSYS_00000000&REV_01\0", L"Net", NET_CLASS, L"Intel Corporation", L"netwtw",
        L"PCI bus 0, device 20, function 0");
    pnp(L"PCI\\VEN_8086&DEV_7E28&SUBSYS_00000000&REV_01\\3&11583659&0&1F3", root, L"Intel(R) Smart Sound Technology Audio Controller",
        L"PCI\\VEN_8086&DEV_7E28&SUBSYS_00000000&REV_01\0", L"Media", MEDIA_CLASS, L"Intel", L"IntcAudioBus",
        L"PCI bus 0, device 31, function 3");
    pnp(L"PCI\\VEN_144D&DEV_A80A&SUBSYS_00000000&REV_00\\3&11583659&0&1D", root, L"Samsung NVMe Controller",
        L"PCI\\VEN_144D&DEV_A80A&SUBSYS_00000000&REV_00\0", L"DiskDrive", DISK_CLASS, L"Samsung",
        L"stornvme", L"PCI bus 0, device 29, function 0");
#endif
    /* the keyboard and mouse every pc has, whose interfaces lead to the class devices the raw input names open. */
    pnp(KEYBOARD_INSTANCE, root, L"Standard PS/2 Keyboard", L"ACPI\\PNP0303\0*PNP0303\0", L"Keyboard", KEYBOARD_CLASS,
        L"(Standard keyboards)", L"i8042prt", L"plugged into keyboard port");
    MimePnpAddInterface(KEYBOARD_INSTANCE, &GUID_DEVINTERFACE_KEYBOARD, L"\\Device\\KeyboardClass0");
    pnp(MOUSE_INSTANCE, root, L"PS/2 Compatible Mouse", L"ACPI\\PNP0F13\0*PNP0F13\0", L"Mouse", MOUSE_CLASS,
        L"(Standard mouse types)", L"i8042prt", L"plugged into PS/2 mouse port");
    MimePnpAddInterface(MOUSE_INSTANCE, &GUID_DEVINTERFACE_MOUSE, L"\\Device\\PointerClass0");
    pnp(L"ACPI\\PNP0C0A\\1", root, L"Microsoft ACPI-Compliant Control Method Battery", L"ACPI\\PNP0C0A\0",
        L"Battery", BATTERY_CLASS, L"Microsoft", L"CmBatt", L"");
    pnp(L"ACPI\\PNP0C0B\\2", root, L"ACPI Thermal Zone", L"ACPI\\PNP0C0B\0", L"System", SYSTEM_CLASS,
        L"Microsoft", L"", L"");
}

/* the drivers of the devices, empty but present, so the manager and \Driver show them running as on the reference machine. */
static NTSTATUS NTAPI empty_driver(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    return STATUS_SUCCESS;
}

/*
 * the class drivers of the keyboard and mouse: their devices open, as programs that look a raw input device up by its
 * name do, and take no requests, since the window manager reads the input.
 */
static NTSTATUS NTAPI class_open(PDEVICE_OBJECT device, PIRP irp)
{
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI class_refuse(PDEVICE_OBJECT device, PIRP irp)
{
    irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    irp->IoStatus.Information = 0;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

static NTSTATUS class_driver(PDRIVER_OBJECT driver, PCWSTR device_name, ULONG type)
{
    for (ULONG i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
        driver->MajorFunction[i] = class_refuse;
    driver->MajorFunction[IRP_MJ_CREATE] = class_open;
    driver->MajorFunction[IRP_MJ_CLEANUP] = class_open;
    driver->MajorFunction[IRP_MJ_CLOSE] = class_open;
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, device_name);
    PDEVICE_OBJECT device;
    return IoCreateDevice(driver, 0, &name, type, 0, FALSE, &device);
}

static NTSTATUS NTAPI kbdclass_entry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    return class_driver(driver, L"\\Device\\KeyboardClass0", 0x0b /* FILE_DEVICE_KEYBOARD */);
}

static NTSTATUS NTAPI mouclass_entry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    return class_driver(driver, L"\\Device\\PointerClass0", 0x0f /* FILE_DEVICE_MOUSE */);
}

static void drivers(void)
{
    IoCreateDriver(L"\\Driver\\kbdclass", kbdclass_entry, nullptr);
    IoCreateDriver(L"\\Driver\\mouclass", mouclass_entry, nullptr);
    static const PCWSTR names[] = {
        L"\\Driver\\ACPI", L"\\Driver\\pci", L"\\Driver\\disk", L"\\Driver\\partmgr", L"\\Driver\\volmgr",
        L"\\Driver\\volsnap", L"\\Driver\\mountmgr", L"\\Driver\\stornvme", L"\\Driver\\CmBatt",
        L"\\Driver\\i8042prt", L"\\FileSystem\\FltMgr",
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(names); i++)
        IoCreateDriver(names[i], empty_driver, nullptr);
#if defined(__aarch64__)
    static const PCWSTR chipset[] = {L"\\Driver\\msisadrv", L"\\Driver\\netwtw", L"\\Driver\\IntcAudioBus"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(chipset); i++)
        IoCreateDriver(chipset[i], empty_driver, nullptr);
#else
    /* the drivers the inbox infs install on the functions of the bus, once each. */
    const IOP_PCI_DEVICE *list;
    ULONG count = IopPciDevices(&list);
    for (ULONG i = 0; i < count; i++) {
        BOOLEAN made = !list[i].Service || pci_gpu(&list[i]) || !_wcsicmp(list[i].Service, L"pci") ||
                       !_wcsicmp(list[i].Service, L"stornvme") || !_wcsicmp(list[i].Service, L"BasicDisplay");
        for (ULONG j = 0; j < i && !made; j++)
            made = list[j].Service && !_wcsicmp(list[j].Service, list[i].Service);
        if (made)
            continue;
        WCHAR name[48] = L"\\Driver\\";
        wcscat(name, list[i].Service);
        IoCreateDriver(name, empty_driver, nullptr);
    }
#endif
#if defined(__aarch64__)
    static const PCWSTR vendors[] = {L"\\Driver\\nvlddmkm", L"\\Driver\\amdkmdag", L"\\Driver\\igdkmd64"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(vendors); i++)
        IoCreateDriver(vendors[i], empty_driver, nullptr);
#else
    /* the display drivers: windows' own two always, then the monitor's or the gpu vendors' as the display has them. */
    IoCreateDriver(L"\\Driver\\BasicDisplay", empty_driver, nullptr);
    IoCreateDriver(L"\\Driver\\BasicRender", empty_driver, nullptr);
    if (display_basic())
        IoCreateDriver(L"\\Driver\\monitor", empty_driver, nullptr);
    if (!display_basic()) {
        IoCreateDriver(L"\\Driver\\nvlddmkm", empty_driver, nullptr);
        IoCreateDriver(L"\\Driver\\amdkmdag", empty_driver, nullptr);
        IoCreateDriver(L"\\Driver\\igdkmd64", empty_driver, nullptr);
    }
#endif
}

void CmpSpecialize(void)
{
    WCHAR computer[16], user[24], sid[96] = {};
    widen(computer, RTL_NUMBER_OF(computer), HalBootInfo->computer_name);
    widen(user, RTL_NUMBER_OF(user), HalBootInfo->user_name);
    _snwprintf(sid, RTL_NUMBER_OF(sid) - 1, L"S-1-5-21-%u-%u-%u-1001", HalBootInfo->user_sid[0],
               HalBootInfo->user_sid[1], HalBootInfo->user_sid[2]);
    ExAcquireFastMutex(&CmpLock);
    version_data(user);
    identity(computer, user, sid);
    security_policy();
    feature_cache();
    service_profiles();
    volatile_environment(computer, user, sid);
    state_repository_user(sid);
    shell_state(sid);
    {
        /* the compatibility layers of programs: both keys are there on Windows, empty. */
        WCHAR layers[200] = {};
        open_key(NT_CURRENT_VERSION L"\\AppCompatFlags\\Layers");
        _snwprintf(layers, RTL_NUMBER_OF(layers) - 1,
                   L"\\REGISTRY\\USER\\%ls\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", sid);
        open_key(layers);
    }
    firmware();
    acpi_description();
    display_adapter();
    machine_guid();
    time_zone();
    locale(sid);
    hardware();
    devices();
    drivers();
    processor_environment();
    CmpUnlock();
}
