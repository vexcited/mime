/*
 * specializes the template hives of the iso into this installation, as
 * windows setup does once and the kernel and session manager redo at each
 * boot: version data, identity of the machine and its user, the hardware
 * description and the processor environment variables.
 */
#include "cm.h"

#define NT_CURRENT_VERSION L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
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

/* the marketing name windows writes for an edition; windows 11 keeps the windows 10 names here. */
static PCWSTR product_name(PCWSTR edition)
{
    static const struct {
        PCWSTR Edition, Name;
    } names[] = {
        {L"Core", L"Windows 10 Home"},
        {L"CoreN", L"Windows 10 Home N"},
        {L"CoreSingleLanguage", L"Windows 10 Home Single Language"},
        {L"Professional", L"Windows 10 Pro"},
        {L"ProfessionalN", L"Windows 10 Pro N"},
        {L"ProfessionalEducation", L"Windows 10 Pro Education"},
        {L"ProfessionalWorkstation", L"Windows 10 Pro for Workstations"},
        {L"Education", L"Windows 10 Education"},
        {L"Enterprise", L"Windows 10 Enterprise"},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(names); i++)
        if (_wcsicmp(names[i].Edition, edition) == 0)
            return names[i].Name;
    return NULL;
}

static void version_data(PCWSTR user)
{
    set_sz(NT_CURRENT_VERSION, L"SystemRoot", REG_SZ, L"C:\\Windows");
    set_sz(NT_CURRENT_VERSION, L"PathName", REG_SZ, L"C:\\Windows");
    set_sz(NT_CURRENT_VERSION, L"CurrentType", REG_SZ, L"Multiprocessor Free");
    WCHAR build[16] = {0};
    _snwprintf(build, RTL_NUMBER_OF(build) - 1, L"%u", MIME_NT_BUILD_NUMBER);
    set_sz(NT_CURRENT_VERSION, L"CurrentBuild", REG_SZ, build);
    set_sz(NT_CURRENT_VERSION, L"CurrentBuildNumber", REG_SZ, build);
    set_dword(NT_CURRENT_VERSION, L"CurrentMajorVersionNumber", MIME_NT_MAJOR_VERSION);
    set_dword(NT_CURRENT_VERSION, L"CurrentMinorVersionNumber", MIME_NT_MINOR_VERSION);
    ULONG64 installed = HalBootInfo->install_filetime;
    set_dword(NT_CURRENT_VERSION, L"InstallDate", (ULONG)(installed / 10000000ull - 11644473600ull));
    set_qword(NT_CURRENT_VERSION, L"InstallTime", installed);
    set_sz(NT_CURRENT_VERSION, L"RegisteredOwner", REG_SZ, user);
    set_sz(NT_CURRENT_VERSION, L"RegisteredOrganization", REG_SZ, L"");

    CM_NODE *node = CmpOpenPath(NT_CURRENT_VERSION, FALSE);
    if (node) {
        CmpDeleteValue(node, L"PendingInstall", 14);
        CM_VALUE *edition = CmpFindValue(node, L"EditionID", 9);
        if (edition && edition->Type == REG_SZ) {
            WCHAR id[64] = {0};
            memcpy(id, edition->Data, edition->DataLength < sizeof(id) - 2 ? edition->DataLength : sizeof(id) - 2);
            PCWSTR name = product_name(id);
            if (name)
                set_sz(NT_CURRENT_VERSION, L"ProductName", REG_SZ, name);
        }
    }
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

    WCHAR key[160] = {0}, profile[96] = {0};
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

/* what winlogon writes when the user logs on: the volatile part of the environment. */
static void volatile_environment(PCWSTR computer, PCWSTR user, PCWSTR sid)
{
    WCHAR path[160] = {0}, sub[160] = {0}, text[160];
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

/* HARDWARE\DESCRIPTION, built by the kernel at every boot. */
static void hardware(void)
{
    WCHAR identifier[96] = {0}, vendor[48], name[64];
    widen(vendor, RTL_NUMBER_OF(vendor), KiProcessorVendor());
    widen(name, RTL_NUMBER_OF(name), HalBootInfo->cpu_name);
    _snwprintf(identifier, RTL_NUMBER_OF(identifier) - 1, L"ARMv8 (64-bit) Family 8 Model %X Revision %u",
               KiProcessorModel(), (ULONG)(KiProcessorIds.Midr & 0xF));
    static const struct {
        USHORT Encoding;
        SIZE_T Offset;
    } registers[] = {
        {0x4000, offsetof(KI_PROCESSOR_IDS, Midr)},  {0x4020, offsetof(KI_PROCESSOR_IDS, Pfr0)},
        {0x4021, offsetof(KI_PROCESSOR_IDS, Pfr1)},  {0x4028, offsetof(KI_PROCESSOR_IDS, Dfr0)},
        {0x4029, offsetof(KI_PROCESSOR_IDS, Dfr1)},  {0x402C, offsetof(KI_PROCESSOR_IDS, Afr0)},
        {0x402D, offsetof(KI_PROCESSOR_IDS, Afr1)},  {0x4030, offsetof(KI_PROCESSOR_IDS, Isar0)},
        {0x4031, offsetof(KI_PROCESSOR_IDS, Isar1)}, {0x4032, offsetof(KI_PROCESSOR_IDS, Isar2)},
        {0x4038, offsetof(KI_PROCESSOR_IDS, Mmfr0)}, {0x4039, offsetof(KI_PROCESSOR_IDS, Mmfr1)},
        {0x403A, offsetof(KI_PROCESSOR_IDS, Mmfr2)}, {0x5801, offsetof(KI_PROCESSOR_IDS, Ctr)},
    };
    for (ULONG cpu = 0; cpu < HalBootInfo->cpu_count; cpu++) {
        WCHAR path[128] = {0};
        _snwprintf(path, RTL_NUMBER_OF(path) - 1,
                   L"\\REGISTRY\\MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\%u", cpu);
        if (!CmpOpenPath(path, TRUE))
            return;
        set_sz(path, L"Identifier", REG_SZ, identifier);
        set_sz(path, L"VendorIdentifier", REG_SZ, vendor);
        set_sz(path, L"ProcessorNameString", REG_SZ, name);
        if (HalBootInfo->cpu_mhz)
            set_dword(path, L"~MHz", HalBootInfo->cpu_mhz);
        for (ULONG i = 0; i < RTL_NUMBER_OF(registers); i++) {
            WCHAR value[16] = {0};
            _snwprintf(value, RTL_NUMBER_OF(value) - 1, L"CP %04X", registers[i].Encoding);
            set_qword(path, value, *(const ULONG64 *)((const UCHAR *)&KiProcessorIds + registers[i].Offset));
        }
    }
}

/*
 * the smbios data of the machine, the way the kernel fills the volatile
 * hardware key from the firmware tables at every boot.
 */
static void firmware(void)
{
    static const PCWSTR bios = L"\\REGISTRY\\MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS";
    WCHAR manufacturer[32], product[32], family[24], vendor[24], version[32], date[16], serial[20];
    widen(manufacturer, RTL_NUMBER_OF(manufacturer), HalBootInfo->system_manufacturer);
    widen(product, RTL_NUMBER_OF(product), HalBootInfo->system_product);
    widen(family, RTL_NUMBER_OF(family), HalBootInfo->system_family);
    widen(vendor, RTL_NUMBER_OF(vendor), HalBootInfo->bios_vendor);
    widen(version, RTL_NUMBER_OF(version), HalBootInfo->bios_version);
    widen(date, RTL_NUMBER_OF(date), HalBootInfo->bios_date);
    widen(serial, RTL_NUMBER_OF(serial), HalBootInfo->system_serial);
    set_sz(bios, L"SystemManufacturer", REG_SZ, manufacturer);
    set_sz(bios, L"SystemProductName", REG_SZ, product);
    set_sz(bios, L"SystemFamily", REG_SZ, family);
    set_sz(bios, L"BIOSVendor", REG_SZ, vendor);
    set_sz(bios, L"BIOSVersion", REG_SZ, version);
    set_sz(bios, L"BIOSReleaseDate", REG_SZ, date);
    set_sz(bios, L"BaseBoardManufacturer", REG_SZ, manufacturer);
    set_sz(bios, L"BaseBoardProduct", REG_SZ, product);
    set_sz(bios, L"BaseBoardVersion", REG_SZ, L"1");
    set_sz(bios, L"SystemSerialNumber", REG_SZ, serial);

    /* the model, also the source of win32_computersystem. */
    static const PCWSTR sysinfo = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\SystemInformation";
    set_sz(sysinfo, L"SystemManufacturer", REG_SZ, manufacturer);
    set_sz(sysinfo, L"SystemProductName", REG_SZ, product);
}

/* the guid windows setup draws at installation time and keeps in the registry. */
static void machine_guid(void)
{
    static const WCHAR hex[] = L"0123456789abcdef";
    WCHAR text[40] = {0};
    ULONG pos = 0;
    for (ULONG i = 0; i < 16 && pos + 2 < RTL_NUMBER_OF(text); i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            text[pos++] = L'-';
        text[pos++] = hex[HalBootInfo->system_uuid[i] >> 4];
        text[pos++] = hex[HalBootInfo->system_uuid[i] & 0xF];
    }
    set_sz(L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", REG_SZ, text);
}

/* the processor variables the session manager writes at boot. */
static void processor_environment(void)
{
    WCHAR text[160] = {0}, vendor[48];
    widen(vendor, RTL_NUMBER_OF(vendor), KiProcessorVendor());
    set_sz(ENVIRONMENT, L"PROCESSOR_ARCHITECTURE", REG_SZ, L"ARM64");
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"ARMv8 (64-bit) Family 8 Model %X Revision %u, %ls", KiProcessorModel(),
               (ULONG)(KiProcessorIds.Midr & 0xF), vendor);
    set_sz(ENVIRONMENT, L"PROCESSOR_IDENTIFIER", REG_SZ, text);
    set_sz(ENVIRONMENT, L"PROCESSOR_LEVEL", REG_SZ, L"8");
    memset(text, 0, sizeof(text));
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%04x", KiProcessorRevision());
    set_sz(ENVIRONMENT, L"PROCESSOR_REVISION", REG_SZ, text);
    memset(text, 0, sizeof(text));
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%u", HalBootInfo->cpu_count);
    set_sz(ENVIRONMENT, L"NUMBER_OF_PROCESSORS", REG_SZ, text);
}

void CmpSpecialize(void)
{
    WCHAR computer[16], user[24], sid[96] = {0};
    widen(computer, RTL_NUMBER_OF(computer), HalBootInfo->computer_name);
    widen(user, RTL_NUMBER_OF(user), HalBootInfo->user_name);
    _snwprintf(sid, RTL_NUMBER_OF(sid) - 1, L"S-1-5-21-%u-%u-%u-1001", HalBootInfo->user_sid[0],
               HalBootInfo->user_sid[1], HalBootInfo->user_sid[2]);
    ExAcquireFastMutex(&CmpLock);
    version_data(user);
    identity(computer, user, sid);
    volatile_environment(computer, user, sid);
    firmware();
    machine_guid();
    hardware();
    processor_environment();
    ExReleaseFastMutex(&CmpLock);
}
