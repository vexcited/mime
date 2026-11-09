/*
 * specializes the template hives for this installation, as windows setup
 * does once and the kernel and session manager redo at each boot: version data,
 * machine and user identity, hardware description, processor variables.
 */
#include "cm.h"
#ifdef __aarch64__
#include "../ex/acpi_tables.h"
#else
#include "../ex/acpi_tables_x64.h"
#endif

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
    widen(name, RTL_NUMBER_OF(name), HalBootInfo->cpu_name);
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
        if (HalBootInfo->cpu_mhz)
            set_dword(path, L"~MHz", HalBootInfo->cpu_mhz);
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

/*
 * the display class key and the video key of the adapter, where its device key leads: the driver description and
 * the identifiers a device reader of the class finds, the name and vendor of the reference gpu.
 */
static void display_adapter(void)
{
    static const WCHAR class_key[] = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class"
                                    L"\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000";
    static const WCHAR video_key[] = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Video"
                                    L"\\{b3f36b7e-6a4d-5a41-9e2c-4d8fa1c7e903}\\0000";
    static const PCWSTR keys[] = {class_key, video_key};
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
        set_sz(keys[i], L"DriverDesc", REG_SZ, L"Intel(R) Arc(TM) Graphics");
        set_sz(keys[i], L"ProviderName", REG_SZ, L"Intel Corporation");
        set_sz(keys[i], L"DriverVersion", REG_SZ, L"32.0.101.6734");
        set_sz(keys[i], L"DriverDate", REG_SZ, L"6-12-2025");
        set_sz(keys[i], L"MatchingDeviceId", REG_SZ, L"PCI\\VEN_8086&DEV_7D55&SUBSYS_20331414");
        set_sz(keys[i], L"InfPath", REG_SZ, L"oem4.inf");
        set_sz(keys[i], L"InfSection", REG_SZ, L"iigd_dch");
#endif
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
    for (ULONG i = 0; i < ACPI_TABLE_COUNT; i++) {
        const ACPI_TABLE_INFO *table = &acpi_tables[i];
        char signature[5] = {};
        memcpy(signature, &table->Signature, 4);
        WCHAR wide_sig[5], oem[7], table_id[9];
        widen(wide_sig, RTL_NUMBER_OF(wide_sig), signature);
        widen(oem, RTL_NUMBER_OF(oem), (const char *)table->Data + 10);
        widen(table_id, RTL_NUMBER_OF(table_id), (const char *)table->Data + 16);
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
            memcpy(&word, table->Data + at, sizeof(word));
            set_dword(path, name, word);
        }
    }
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
    shell_state(sid);
    firmware();
    acpi_description();
    display_adapter();
    machine_guid();
    time_zone();
    locale(sid);
    hardware();
    processor_environment();
    CmpUnlock();
}
