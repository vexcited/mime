/* instances: where the instances of a class come from, the static ones of the repository and the providers of the machine. */
#include "wbemcore.h"
#include <mime/win32/winsvc.h>
#include <mime/win32/winspool.h>
#include <mime/ntrtl.h>
#include <mime/ntapi.h>

DECLSPEC_IMPORT BOOL WINAPI GetUserNameW(LPWSTR buffer, LPDWORD size);

/* ------------------------------------------------------------------ helpers to fill an instance */

static void put_raw(WObj *o, const WCHAR *name, VARIANT *raw)
{
    WProp *p = wo_prop(o, name);
    if (!p)
        return;
    VARIANT v;
    wv_init(&v);
    if (SUCCEEDED(wv_coerce(&v, raw, p->type))) {
        VariantClear(&p->value);
        p->value = v;
    } else {
        VariantClear(&v);
    }
}

static void put_str(WObj *o, const WCHAR *name, const WCHAR *text)
{
    VARIANT v;
    VariantInit(&v);
    if (!text) {
        v.vt = VT_NULL;
    } else {
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocString(text);
    }
    put_raw(o, name, &v);
    VariantClear(&v);
}

static void put_int(WObj *o, const WCHAR *name, LONGLONG n)
{
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I8;
    v.llVal = n;
    put_raw(o, name, &v);
}

static void put_uint(WObj *o, const WCHAR *name, ULONGLONG n)
{
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_UI8;
    v.ullVal = n;
    put_raw(o, name, &v);
}

static void put_bool(WObj *o, const WCHAR *name, BOOL b)
{
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BOOL;
    v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
    put_raw(o, name, &v);
}

static void put_strings(WObj *o, const WCHAR *name, const WCHAR *const *list, UINT count)
{
    SAFEARRAY *array = SafeArrayCreateVector(VT_VARIANT, 0, count);
    for (UINT i = 0; array && i < count; i++) {
        LONG index = (LONG)i;
        VARIANT item;
        VariantInit(&item);
        item.vt = VT_BSTR;
        item.bstrVal = SysAllocString(list[i]);
        SafeArrayPutElement(array, &index, &item);
        VariantClear(&item);
    }
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_ARRAY | VT_VARIANT;
    v.parray = array;
    put_raw(o, name, &v);
    VariantClear(&v);
}

/* yyyymmddHHMMSS.mmmmmm+UUU */
static WCHAR *datetime_text(const FILETIME *utc, LONG bias_minutes)
{
    FILETIME local;
    LONGLONG t = ((LONGLONG)utc->dwHighDateTime << 32 | utc->dwLowDateTime) - (LONGLONG)bias_minutes * 60 * 10000000LL;
    local.dwLowDateTime = (DWORD)t;
    local.dwHighDateTime = (DWORD)(t >> 32);
    SYSTEMTIME st;
    FileTimeToSystemTime(&local, &st);
    return wb_format(L"%04d%02d%02d%02d%02d%02d.%06d%c%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                     st.wMilliseconds * 1000, bias_minutes > 0 ? '-' : '+', bias_minutes > 0 ? bias_minutes : -bias_minutes);
}

static LONG time_bias(void)
{
    TIME_ZONE_INFORMATION tz;
    DWORD id = GetTimeZoneInformation(&tz);
    return -(tz.Bias + (id == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : tz.StandardBias));
}

static void put_datetime(WObj *o, const WCHAR *name, const FILETIME *utc)
{
    LONG offset = time_bias();
    WCHAR *text = datetime_text(utc, -offset);
    put_str(o, name, text);
    wb_free(text);
}

static WCHAR *registry_text(HKEY root, const WCHAR *path, const WCHAR *name)
{
    HKEY key;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &key))
        return nullptr;
    WCHAR value[512];
    DWORD size = sizeof(value) - sizeof(WCHAR), type;
    LONG error = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)value, &size);
    RegCloseKey(key);
    if (error || (type != REG_SZ && type != REG_EXPAND_SZ))
        return nullptr;
    value[size / sizeof(WCHAR)] = 0;
    return wb_wcsdup(value);
}

static BOOL registry_dword(HKEY root, const WCHAR *path, const WCHAR *name, DWORD *out)
{
    HKEY key;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &key))
        return FALSE;
    DWORD size = sizeof(*out), type;
    LONG error = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)out, &size);
    RegCloseKey(key);
    return !error && type == REG_DWORD;
}

static const WCHAR *computer_name(void)
{
    static WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    if (!name[0]) {
        DWORD size = RTL_NUMBER_OF(name);
        if (!GetComputerNameW(name, &size))
            wcscpy(name, L"MIME");
    }
    return name;
}

#define NT_VERSION_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"

/* ------------------------------------------------------------------ Win32_OperatingSystem */

static BOOL os_enum(WNs *ns, WObj *c, WEmit emit, void *context)
{
    WObj *o = wo_spawn(c);
    RTL_OSVERSIONINFOEXW version;
    memset(&version, 0, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    RtlGetVersion(&version);
    WCHAR *product = registry_text(HKEY_LOCAL_MACHINE, NT_VERSION_KEY, L"ProductName");
    WCHAR caption[160];
    const WCHAR *base = product ? product : L"Windows";
    if (version.dwBuildNumber >= 22000 && !_wcsnicmp(base, L"Windows 10", 10))
        _snwprintf(caption, RTL_NUMBER_OF(caption) - 1, L"Microsoft Windows 11%s", base + 10);
    else
        _snwprintf(caption, RTL_NUMBER_OF(caption) - 1, L"Microsoft %s", base);
    caption[RTL_NUMBER_OF(caption) - 1] = 0;
    WCHAR windows[MAX_PATH], system[MAX_PATH];
    GetSystemWindowsDirectoryW(windows, MAX_PATH);
    GetSystemDirectoryW(system, MAX_PATH);
    WCHAR *name = wb_format(L"%s|%s|\\Device\\Harddisk0\\Partition1", caption, windows);
    WCHAR build[16], full[48];
    _snwprintf(build, RTL_NUMBER_OF(build), L"%u", version.dwBuildNumber);
    _snwprintf(full, RTL_NUMBER_OF(full), L"%u.%u.%u", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
    put_str(o, L"Caption", caption);
    put_str(o, L"Name", name);
    put_str(o, L"Description", caption);
    put_str(o, L"CreationClassName", L"Win32_OperatingSystem");
    put_str(o, L"CSCreationClassName", L"Win32_ComputerSystem");
    put_str(o, L"CSName", computer_name());
    put_str(o, L"Version", full);
    put_str(o, L"BuildNumber", build);
    put_str(o, L"BuildType", L"Multiprocessor Free");
    put_str(o, L"Manufacturer", L"Microsoft Corporation");
    put_str(o, L"Status", L"OK");
    put_str(o, L"WindowsDirectory", windows);
    put_str(o, L"SystemDirectory", system);
    WCHAR drive[3] = {windows[0], ':', 0};
    put_str(o, L"SystemDrive", drive);
    put_str(o, L"BootDevice", L"\\Device\\HarddiskVolume1");
    put_str(o, L"SystemDevice", L"\\Device\\HarddiskVolume1");
    put_str(o, L"CSDVersion", version.szCSDVersion[0] ? version.szCSDVersion : nullptr);
    put_int(o, L"ServicePackMajorVersion", version.wServicePackMajor);
    put_int(o, L"ServicePackMinorVersion", version.wServicePackMinor);
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    put_str(o, L"OSArchitecture", si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
                                          si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64
                                      ? L"64-bit"
                                      : L"32-bit");
    put_uint(o, L"OSType", 18);
    put_uint(o, L"ProductType", version.wProductType);
    put_uint(o, L"OperatingSystemSKU", 48);
    put_uint(o, L"SuiteMask", version.wSuiteMask);
    put_bool(o, L"Primary", TRUE);
    put_bool(o, L"PortableOperatingSystem", FALSE);
    put_bool(o, L"Distributed", FALSE);
    put_uint(o, L"NumberOfUsers", 1);
    put_uint(o, L"NumberOfLicensedUsers", 0);
    put_uint(o, L"MaxNumberOfProcesses", 0xFFFFFFFF);
    put_uint(o, L"MaxProcessMemorySize", 137438953344ULL);
    put_uint(o, L"EncryptionLevel", 256);
    put_uint(o, L"ForegroundApplicationBoost", 2);
    put_uint(o, L"DataExecutionPrevention_Available", 1);
    put_bool(o, L"DataExecutionPrevention_Available", TRUE);
    put_bool(o, L"DataExecutionPrevention_32BitApplications", TRUE);
    put_bool(o, L"DataExecutionPrevention_SupportPolicy", TRUE);
    LCID lcid = GetSystemDefaultLCID();
    WCHAR locale[8], language[16], country[16], codeset[16];
    _snwprintf(locale, RTL_NUMBER_OF(locale), L"%04x", lcid & 0xFFFF);
    _snwprintf(language, RTL_NUMBER_OF(language), L"%u", lcid & 0xFFFF);
    GetLocaleInfoW(lcid, LOCALE_ICOUNTRY, country, RTL_NUMBER_OF(country));
    GetLocaleInfoW(lcid, LOCALE_IDEFAULTANSICODEPAGE, codeset, RTL_NUMBER_OF(codeset));
    put_str(o, L"Locale", locale);
    put_uint(o, L"OSLanguage", lcid & 0xFFFF);
    put_str(o, L"CountryCode", country);
    put_str(o, L"CodeSet", codeset);
    const WCHAR *mui[] = {L"en-US"};
    put_strings(o, L"MUILanguages", mui, 1);
    put_int(o, L"CurrentTimeZone", time_bias());
    MEMORYSTATUSEX memory;
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    put_uint(o, L"TotalVisibleMemorySize", memory.ullTotalPhys / 1024);
    put_uint(o, L"FreePhysicalMemory", memory.ullAvailPhys / 1024);
    put_uint(o, L"TotalVirtualMemorySize", memory.ullTotalPageFile / 1024);
    put_uint(o, L"FreeVirtualMemory", memory.ullAvailPageFile / 1024);
    put_uint(o, L"SizeStoredInPagingFiles", (memory.ullTotalPageFile - memory.ullTotalPhys) / 1024);
    put_uint(o, L"FreeSpaceInPagingFiles", (memory.ullAvailPageFile - memory.ullAvailPhys) / 1024);
    put_uint(o, L"TotalSwapSpaceSize", 0);
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    put_datetime(o, L"LocalDateTime", &now);
    ULONGLONG ticks = GetTickCount64();
    LONGLONG boot = ((LONGLONG)now.dwHighDateTime << 32 | now.dwLowDateTime) - (LONGLONG)ticks * 10000;
    FILETIME boot_time = {(DWORD)boot, (DWORD)(boot >> 32)};
    put_datetime(o, L"LastBootUpTime", &boot_time);
    DWORD installed;
    if (registry_dword(HKEY_LOCAL_MACHINE, NT_VERSION_KEY, L"InstallDate", &installed)) {
        LONGLONG t = ((LONGLONG)installed + 11644473600LL) * 10000000LL;
        FILETIME ft = {(DWORD)t, (DWORD)(t >> 32)};
        put_datetime(o, L"InstallDate", &ft);
    }
    WCHAR *serial = registry_text(HKEY_LOCAL_MACHINE, NT_VERSION_KEY, L"ProductId");
    put_str(o, L"SerialNumber", serial);
    wb_free(serial);
    WCHAR *owner = registry_text(HKEY_LOCAL_MACHINE, NT_VERSION_KEY, L"RegisteredOwner");
    WCHAR *org = registry_text(HKEY_LOCAL_MACHINE, NT_VERSION_KEY, L"RegisteredOrganization");
    put_str(o, L"RegisteredUser", owner);
    put_str(o, L"Organization", org);
    wb_free(owner);
    wb_free(org);
    ULONG needed = 0;
    NtQuerySystemInformation(SystemProcessInformation, nullptr, 0, &needed);
    put_uint(o, L"NumberOfProcesses", 0);
    BOOL more = emit(context, o);
    wo_free(o);
    wb_free(name);
    wb_free(product);
    return more;
}

/* ------------------------------------------------------------------ Win32_ComputerSystem */

static BOOL computer_enum(WNs *ns, WObj *c, WEmit emit, void *context)
{
    WObj *o = wo_spawn(c);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    MEMORYSTATUSEX memory;
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    WCHAR user[256], name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = RTL_NUMBER_OF(user) - 1;
    if (!GetUserNameW(user, &size))
        wcscpy(user, L"user");
    const WCHAR *machine = computer_name();
    wcscpy(name, machine);
    WCHAR *full = wb_format(L"%s\\%s", machine, user);
    WCHAR *vendor = registry_text(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer");
    WCHAR *model = registry_text(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName");
    put_str(o, L"Name", name);
    put_str(o, L"Caption", name);
    put_str(o, L"Description", L"AT/AT COMPATIBLE");
    put_str(o, L"CreationClassName", L"Win32_ComputerSystem");
    put_str(o, L"DNSHostName", name);
    put_str(o, L"Domain", L"WORKGROUP");
    put_str(o, L"Workgroup", L"WORKGROUP");
    put_bool(o, L"PartOfDomain", FALSE);
    put_uint(o, L"DomainRole", 0);
    put_str(o, L"Manufacturer", vendor ? vendor : L"Microsoft Corporation");
    put_str(o, L"Model", model ? model : L"Microsoft Surface");
    put_str(o, L"UserName", full);
    put_str(o, L"SystemType", si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? L"x64-based PC" : L"X86-based PC");
    put_str(o, L"Status", L"OK");
    put_str(o, L"BootupState", L"Normal boot");
    put_str(o, L"NameFormat", L"Win32");
    put_uint(o, L"NumberOfProcessors", 1);
    put_uint(o, L"NumberOfLogicalProcessors", si.dwNumberOfProcessors);
    put_uint(o, L"TotalPhysicalMemory", memory.ullTotalPhys);
    put_bool(o, L"HypervisorPresent", TRUE);
    put_uint(o, L"PCSystemType", 1);
    put_uint(o, L"AdminPasswordStatus", 3);
    put_uint(o, L"KeyboardPasswordStatus", 3);
    put_uint(o, L"PowerOnPasswordStatus", 3);
    put_uint(o, L"FrontPanelResetStatus", 3);
    put_uint(o, L"ChassisBootupState", 3);
    put_uint(o, L"ThermalState", 3);
    put_uint(o, L"PowerState", 0);
    put_bool(o, L"AutomaticManagedPagefile", TRUE);
    put_bool(o, L"AutomaticResetBootOption", TRUE);
    put_bool(o, L"NetworkServerModeEnabled", TRUE);
    put_bool(o, L"InfraredSupported", FALSE);
    put_bool(o, L"DaylightInEffect", FALSE);
    put_bool(o, L"EnableDaylightSavingsTime", TRUE);
    put_int(o, L"CurrentTimeZone", time_bias());
    BOOL more = emit(context, o);
    wo_free(o);
    wb_free(full);
    wb_free(vendor);
    wb_free(model);
    return more;
}

/* ------------------------------------------------------------------ Win32_Processor */

static BOOL processor_enum(WNs *ns, WObj *c, WEmit emit, void *context)
{
    WObj *o = wo_spawn(c);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const WCHAR *key = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    WCHAR *name = registry_text(HKEY_LOCAL_MACHINE, key, L"ProcessorNameString");
    WCHAR *vendor = registry_text(HKEY_LOCAL_MACHINE, key, L"VendorIdentifier");
    WCHAR *identifier = registry_text(HKEY_LOCAL_MACHINE, key, L"Identifier");
    DWORD mhz = 0;
    registry_dword(HKEY_LOCAL_MACHINE, key, L"~MHz", &mhz);
    put_str(o, L"DeviceID", L"CPU0");
    put_str(o, L"Name", name ? name : L"Virtual CPU");
    put_str(o, L"Caption", identifier ? identifier : L"x64 Family 6 Model 0 Stepping 0");
    put_str(o, L"Description", identifier ? identifier : L"x64 Family 6 Model 0 Stepping 0");
    put_str(o, L"Manufacturer", vendor ? vendor : L"GenuineIntel");
    put_str(o, L"SocketDesignation", L"CPU 0");
    put_str(o, L"Status", L"OK");
    put_str(o, L"SystemName", computer_name());
    put_str(o, L"SystemCreationClassName", L"Win32_ComputerSystem");
    put_str(o, L"CreationClassName", L"Win32_Processor");
    put_uint(o, L"MaxClockSpeed", mhz);
    put_uint(o, L"CurrentClockSpeed", mhz);
    put_uint(o, L"NumberOfCores", si.dwNumberOfProcessors);
    put_uint(o, L"NumberOfEnabledCore", si.dwNumberOfProcessors);
    put_uint(o, L"NumberOfLogicalProcessors", si.dwNumberOfProcessors);
    put_uint(o, L"ThreadCount", si.dwNumberOfProcessors);
    put_uint(o, L"AddressWidth", 64);
    put_uint(o, L"DataWidth", 64);
    put_uint(o, L"Architecture", si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? 9 : 0);
    put_uint(o, L"Family", 2);
    put_uint(o, L"Level", si.wProcessorLevel);
    put_uint(o, L"Revision", si.wProcessorRevision);
    put_uint(o, L"ProcessorType", 3);
    put_uint(o, L"CpuStatus", 1);
    put_uint(o, L"Availability", 3);
    put_uint(o, L"LoadPercentage", 0);
    put_bool(o, L"VirtualizationFirmwareEnabled", TRUE);
    BOOL more = emit(context, o);
    wo_free(o);
    wb_free(name);
    wb_free(vendor);
    wb_free(identifier);
    return more;
}

/* ------------------------------------------------------------------ Win32_PhysicalMemory */

static BOOL memory_enum(WNs *ns, WObj *c, WEmit emit, void *context)
{
    WObj *o = wo_spawn(c);
    MEMORYSTATUSEX memory;
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    ULONGLONG capacity = (memory.ullTotalPhys + 0xFFFFF) & ~0xFFFFFULL;
    put_str(o, L"Tag", L"Physical Memory 0");
    put_str(o, L"Name", L"Physical Memory");
    put_str(o, L"Caption", L"Physical Memory");
    put_str(o, L"Description", L"Physical Memory");
    put_str(o, L"BankLabel", L"BANK 0");
    put_str(o, L"DeviceLocator", L"DIMM 0");
    put_str(o, L"Manufacturer", L"Microsoft Corporation");
    put_str(o, L"CreationClassName", L"Win32_PhysicalMemory");
    put_uint(o, L"Capacity", capacity);
    put_uint(o, L"FormFactor", 8);
    put_uint(o, L"MemoryType", 2);
    put_uint(o, L"TypeDetail", 128);
    put_uint(o, L"DataWidth", 64);
    put_uint(o, L"TotalWidth", 64);
    BOOL more = emit(context, o);
    wo_free(o);
    return more;
}

/* ------------------------------------------------------------------ Win32_Battery */

static BOOL battery_enum(WNs *ns, WObj *c, WEmit emit, void *context)
{
    SYSTEM_POWER_STATUS power = {};
    GetSystemPowerStatus(&power);
    WObj *o = wo_spawn(c);
    put_str(o, L"DeviceID", L"Battery 0");
    put_str(o, L"Name", L"Battery 0");
    put_str(o, L"Caption", L"Internal Battery");
    put_str(o, L"Description", L"Internal Battery");
    put_str(o, L"Status", L"OK");
    put_str(o, L"SystemName", computer_name());
    put_str(o, L"CreationClassName", L"Win32_Battery");
    put_str(o, L"SystemCreationClassName", L"Win32_ComputerSystem");
    put_uint(o, L"BatteryStatus", power.ACLineStatus ? 2 : 1);
    put_uint(o, L"Chemistry", 6);
    put_uint(o, L"DesignCapacity", 70000);
    put_uint(o, L"DesignVoltage", 12000);
    put_uint(o, L"EstimatedChargeRemaining", power.BatteryLifePercent <= 100 ? power.BatteryLifePercent : 87);
    put_uint(o, L"EstimatedRunTime", 71582788);
    put_bool(o, L"PowerOnline", power.ACLineStatus != 0);
    put_uint(o, L"TimeOnBattery", 0);
    put_uint(o, L"TimeToFullCharge", 0);
    BOOL more = emit(context, o);
    wo_free(o);
    return more;
}

/* ------------------------------------------------------------------ Win32_LogicalDisk */

static BOOL disk_enum(WNs *ns, WObj *c, WEmit emit, void *context)
{
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(drives & (1u << i)))
            continue;
        WCHAR id[3] = {(WCHAR)('A' + i), ':', 0};
        WCHAR root[4] = {(WCHAR)('A' + i), ':', '\\', 0};
        UINT type = GetDriveTypeW(root);
        WObj *o = wo_spawn(c);
        static const WCHAR *const KINDS[] = {L"Unknown",       L"No Root Directory", L"Removable Disk", L"Local Disk",
                                             L"Network Connection", L"CD-ROM Disk",    L"RAM Disk"};
        put_str(o, L"DeviceID", id);
        put_str(o, L"Name", id);
        put_str(o, L"Caption", id);
        put_str(o, L"Description", KINDS[type < RTL_NUMBER_OF(KINDS) ? type : 0]);
        put_str(o, L"SystemName", computer_name());
        put_str(o, L"CreationClassName", L"Win32_LogicalDisk");
        put_str(o, L"SystemCreationClassName", L"Win32_ComputerSystem");
        put_uint(o, L"DriveType", type);
        put_uint(o, L"MediaType", type == DRIVE_FIXED ? 12 : type == DRIVE_REMOVABLE ? 11 : 0);
        WCHAR label[MAX_PATH + 1] = L"", fs[MAX_PATH + 1] = L"";
        DWORD serial = 0, maxlen = 0, flags = 0;
        if (GetVolumeInformationW(root, label, RTL_NUMBER_OF(label), &serial, &maxlen, &flags, fs, RTL_NUMBER_OF(fs))) {
            put_str(o, L"VolumeName", label);
            put_str(o, L"FileSystem", fs);
            WCHAR text[16];
            _snwprintf(text, RTL_NUMBER_OF(text), L"%08X", serial);
            put_str(o, L"VolumeSerialNumber", text);
            put_uint(o, L"MaximumComponentLength", maxlen);
            put_bool(o, L"Compressed", FALSE);
            put_bool(o, L"SupportsFileBasedCompression", (flags & FILE_FILE_COMPRESSION) != 0);
            put_bool(o, L"SupportsDiskQuotas", (flags & 0x20) != 0);
            put_bool(o, L"QuotasDisabled", TRUE);
            put_uint(o, L"Access", 0);
            ULARGE_INTEGER free_bytes, total, total_free;
            if (GetDiskFreeSpaceExW(root, &free_bytes, &total, &total_free)) {
                put_uint(o, L"Size", total.QuadPart);
                put_uint(o, L"FreeSpace", total_free.QuadPart);
            }
        }
        BOOL more = emit(context, o);
        wo_free(o);
        if (!more)
            return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ Win32_VideoController */

#define DISPLAY_CLASS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000"

/* the driver's date as setup writes it, month-day-year, in the form of a datetime. */
static WCHAR *driver_date(const WCHAR *text)
{
    int part[3] = {};
    if (!text)
        return nullptr;
    for (int i = 0; i < 3; i++) {
        if (*text < '0' || *text > '9')
            return nullptr;
        while (*text >= '0' && *text <= '9')
            part[i] = part[i] * 10 + (*text++ - '0');
        if (i < 2 && *text++ != '-')
            return nullptr;
    }
    return wb_format(L"%04d%02d%02d000000.000000+000", part[2], part[0], part[1]);
}

/* the pci device whose driver is the display class key's adapter, as the device tree names it. */
static WCHAR *display_instance(void)
{
    HKEY pci;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ, &pci))
        return nullptr;
    WCHAR *found = nullptr, hardware[200], instance[200], driver[128];
    for (DWORD i = 0; !found; i++) {
        DWORD length = RTL_NUMBER_OF(hardware);
        if (RegEnumKeyExW(pci, i, hardware, &length, nullptr, nullptr, nullptr, nullptr))
            break;
        HKEY device;
        if (RegOpenKeyExW(pci, hardware, 0, KEY_READ, &device))
            continue;
        for (DWORD j = 0; !found; j++) {
            length = RTL_NUMBER_OF(instance);
            if (RegEnumKeyExW(device, j, instance, &length, nullptr, nullptr, nullptr, nullptr))
                break;
            DWORD size = sizeof(driver);
            if (!RegGetValueW(device, instance, L"Driver", RRF_RT_REG_SZ, nullptr, driver, &size) &&
                !_wcsicmp(driver, L"{4d36e968-e325-11ce-bfc1-08002be10318}\\0000"))
                found = wb_format(L"PCI\\%s\\%s", hardware, instance);
        }
        RegCloseKey(device);
    }
    RegCloseKey(pci);
    return found;
}

static BOOL video_enum(WNs *ns, WObj *c, WEmit emit, void *context)
{
    WCHAR *name = registry_text(HKEY_LOCAL_MACHINE, DISPLAY_CLASS_KEY, L"DriverDesc");
    if (!name)
        return TRUE;
    WCHAR *vendor = registry_text(HKEY_LOCAL_MACHINE, DISPLAY_CLASS_KEY, L"ProviderName");
    WCHAR *version = registry_text(HKEY_LOCAL_MACHINE, DISPLAY_CLASS_KEY, L"DriverVersion");
    WCHAR *date_text = registry_text(HKEY_LOCAL_MACHINE, DISPLAY_CLASS_KEY, L"DriverDate");
    WCHAR *date = driver_date(date_text);
    WCHAR *inf = registry_text(HKEY_LOCAL_MACHINE, DISPLAY_CLASS_KEY, L"InfPath");
    WCHAR *section = registry_text(HKEY_LOCAL_MACHINE, DISPLAY_CLASS_KEY, L"InfSection");
    WCHAR *pnp = display_instance();
    WCHAR *description = nullptr;
    WObj *o = wo_spawn(c);
    put_str(o, L"DeviceID", L"VideoController1");
    put_str(o, L"Name", name);
    put_str(o, L"Caption", name);
    put_str(o, L"Description", name);
    put_str(o, L"AdapterCompatibility", vendor);
    put_str(o, L"VideoProcessor", name);
    put_str(o, L"DriverVersion", version);
    put_str(o, L"DriverDate", date);
    put_str(o, L"InfFilename", inf);
    put_str(o, L"InfSection", section);
    put_str(o, L"PNPDeviceID", pnp);
    put_str(o, L"SystemName", computer_name());
    put_str(o, L"CreationClassName", L"Win32_VideoController");
    put_str(o, L"SystemCreationClassName", L"Win32_ComputerSystem");
    put_str(o, L"Status", L"OK");
    put_str(o, L"AdapterDACType", L"Internal");
    put_uint(o, L"Availability", 3);
    put_uint(o, L"ConfigManagerErrorCode", 0);
    put_bool(o, L"ConfigManagerUserConfig", FALSE);
    put_uint(o, L"VideoArchitecture", 5);
    put_uint(o, L"VideoMemoryType", 2);
    put_bool(o, L"Monochrome", FALSE);
    put_uint(o, L"CurrentScanMode", 2);
    /* the mode of the primary display, when the process has user32. */
    typedef BOOL(WINAPI * EnumSettings)(LPCWSTR, DWORD, DEVMODEW *);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    EnumSettings settings = user32 ? (EnumSettings)(void *)GetProcAddress(user32, "EnumDisplaySettingsW") : nullptr;
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    if (settings && settings(nullptr, (DWORD)-1, &mode)) {
        ULONGLONG colors = mode.dmBitsPerPel >= 32 ? 4294967296ull : 1ull << mode.dmBitsPerPel;
        description = wb_format(L"%u x %u x %I64u colors", mode.dmPelsWidth, mode.dmPelsHeight, colors);
        put_uint(o, L"CurrentHorizontalResolution", mode.dmPelsWidth);
        put_uint(o, L"CurrentVerticalResolution", mode.dmPelsHeight);
        put_uint(o, L"CurrentBitsPerPixel", mode.dmBitsPerPel);
        put_uint(o, L"CurrentRefreshRate", mode.dmDisplayFrequency);
        put_uint(o, L"MaxRefreshRate", mode.dmDisplayFrequency);
        put_uint(o, L"MinRefreshRate", mode.dmDisplayFrequency);
        put_uint(o, L"CurrentNumberOfColors", colors);
        put_str(o, L"VideoModeDescription", description);
    }
    BOOL more = emit(context, o);
    wo_free(o);
    wb_free(name);
    wb_free(vendor);
    wb_free(version);
    wb_free(date_text);
    wb_free(date);
    wb_free(inf);
    wb_free(section);
    wb_free(pnp);
    wb_free(description);
    return more;
}

/* ------------------------------------------------------------------ Win32_Service */

static const WCHAR *state_name(DWORD state)
{
    switch (state) {
    case SERVICE_STOPPED:
        return L"Stopped";
    case SERVICE_START_PENDING:
        return L"Start Pending";
    case SERVICE_STOP_PENDING:
        return L"Stop Pending";
    case SERVICE_RUNNING:
        return L"Running";
    case SERVICE_CONTINUE_PENDING:
        return L"Continue Pending";
    case SERVICE_PAUSE_PENDING:
        return L"Pause Pending";
    case SERVICE_PAUSED:
        return L"Paused";
    }
    return L"Unknown";
}

static void fill_service(WObj *o, SC_HANDLE service, const WCHAR *name, const WCHAR *display, const SERVICE_STATUS_PROCESS *status)
{
    put_str(o, L"Name", name);
    put_str(o, L"DisplayName", display);
    put_str(o, L"Caption", display);
    put_str(o, L"SystemName", computer_name());
    put_str(o, L"CreationClassName", L"Win32_Service");
    put_str(o, L"SystemCreationClassName", L"Win32_ComputerSystem");
    put_str(o, L"Status", L"OK");
    put_str(o, L"State", state_name(status->dwCurrentState));
    put_bool(o, L"Started", status->dwCurrentState == SERVICE_RUNNING);
    put_uint(o, L"ProcessId", status->dwProcessId);
    put_uint(o, L"ExitCode", status->dwWin32ExitCode);
    put_uint(o, L"ServiceSpecificExitCode", status->dwServiceSpecificExitCode);
    put_uint(o, L"CheckPoint", status->dwCheckPoint);
    put_uint(o, L"WaitHint", status->dwWaitHint);
    put_bool(o, L"AcceptStop", (status->dwControlsAccepted & SERVICE_ACCEPT_STOP) != 0);
    put_bool(o, L"AcceptPause", (status->dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) != 0);
    put_bool(o, L"DesktopInteract", (status->dwServiceType & SERVICE_INTERACTIVE_PROCESS) != 0);
    DWORD need = 0;
    QueryServiceConfigW(service, nullptr, 0, &need);
    if (need) {
        QUERY_SERVICE_CONFIGW *config = wb_alloc(need);
        if (QueryServiceConfigW(service, config, need, &need)) {
            static const WCHAR *const START[] = {L"Boot", L"System", L"Auto", L"Manual", L"Disabled"};
            static const WCHAR *const ERRORS[] = {L"Ignore", L"Normal", L"Severe", L"Critical"};
            put_str(o, L"PathName", config->lpBinaryPathName);
            put_str(o, L"StartName", config->lpServiceStartName);
            put_str(o, L"StartMode", config->dwStartType < 5 ? START[config->dwStartType] : L"Unknown");
            put_str(o, L"ErrorControl", config->dwErrorControl < 4 ? ERRORS[config->dwErrorControl] : L"Unknown");
            put_uint(o, L"TagId", config->dwTagId);
            const WCHAR *kind = L"Unknown";
            DWORD t = config->dwServiceType & ~SERVICE_INTERACTIVE_PROCESS;
            if (t == SERVICE_WIN32_OWN_PROCESS)
                kind = L"Own Process";
            else if (t == SERVICE_WIN32_SHARE_PROCESS)
                kind = L"Share Process";
            else if (t == SERVICE_KERNEL_DRIVER)
                kind = L"Kernel Driver";
            else if (t == SERVICE_FILE_SYSTEM_DRIVER)
                kind = L"File System Driver";
            put_str(o, L"ServiceType", kind);
        }
        wb_free(config);
    }
    need = 0;
    QueryServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &need);
    if (need) {
        BYTE *buffer = wb_alloc(need);
        if (QueryServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, buffer, need, &need))
            put_str(o, L"Description", ((SERVICE_DESCRIPTIONW *)buffer)->lpDescription);
        wb_free(buffer);
    }
    SERVICE_DELAYED_AUTO_START_INFO delayed;
    need = sizeof(delayed);
    if (QueryServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, (BYTE *)&delayed, need, &need))
        put_bool(o, L"DelayedAutoStart", delayed.fDelayedAutostart);
}

static BOOL service_enum(WNs *ns, WObj *c, WEmit emit, void *context)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
    if (!manager)
        return TRUE;
    DWORD need = 0, count = 0, resume = 0;
    EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, nullptr, 0, &need, &count, &resume,
                          nullptr);
    BYTE *buffer = wb_alloc(need + 4096);
    BOOL more = TRUE;
    resume = 0;
    if (EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buffer, need + 4096, &need, &count,
                              &resume, nullptr) || GetLastError() == ERROR_MORE_DATA) {
        ENUM_SERVICE_STATUS_PROCESSW *list = (ENUM_SERVICE_STATUS_PROCESSW *)buffer;
        for (DWORD i = 0; i < count && more; i++) {
            SC_HANDLE service = OpenServiceW(manager, list[i].lpServiceName, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
            if (!service)
                continue;
            WObj *o = wo_spawn(c);
            fill_service(o, service, list[i].lpServiceName, list[i].lpDisplayName, &list[i].ServiceStatusProcess);
            CloseServiceHandle(service);
            more = emit(context, o);
            wo_free(o);
        }
    }
    wb_free(buffer);
    CloseServiceHandle(manager);
    return more;
}

static DWORD service_result(DWORD error)
{
    switch (error) {
    case ERROR_SUCCESS:
        return 0;
    case ERROR_ACCESS_DENIED:
        return 2;
    case ERROR_DEPENDENT_SERVICES_RUNNING:
        return 3;
    case ERROR_INVALID_SERVICE_CONTROL:
        return 4;
    case ERROR_SERVICE_CANNOT_ACCEPT_CTRL:
        return 5;
    case ERROR_SERVICE_NOT_ACTIVE:
        return 6;
    case ERROR_SERVICE_REQUEST_TIMEOUT:
        return 7;
    case ERROR_SERVICE_ALREADY_RUNNING:
        return 10;
    case ERROR_SERVICE_DATABASE_LOCKED:
        return 11;
    case ERROR_SERVICE_DEPENDENCY_DELETED:
        return 12;
    case ERROR_SERVICE_DEPENDENCY_FAIL:
        return 13;
    case ERROR_SERVICE_DISABLED:
        return 14;
    case ERROR_SERVICE_LOGON_FAILED:
        return 15;
    case ERROR_SERVICE_MARKED_FOR_DELETE:
        return 16;
    case ERROR_SERVICE_NO_THREAD:
        return 17;
    case ERROR_CIRCULAR_DEPENDENCY:
        return 18;
    case ERROR_DUPLICATE_SERVICE_NAME:
        return 19;
    case ERROR_INVALID_NAME:
        return 20;
    case ERROR_INVALID_PARAMETER:
        return 21;
    case ERROR_INVALID_SERVICE_ACCOUNT:
        return 22;
    case ERROR_SERVICE_EXISTS:
        return 23;
    case ERROR_PATH_NOT_FOUND:
        return 9;
    default:
        return 8;
    }
}

static HRESULT service_method(WNs *ns, const WCHAR *name, const WCHAR *method, WObj *in, WObj **out)
{
    DWORD access = SERVICE_ALL_ACCESS;
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    DWORD result = 8;
    if (!manager) {
        result = service_result(GetLastError());
    } else {
        SC_HANDLE service = OpenServiceW(manager, name, access);
        if (!service) {
            result = GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST ? 8 : service_result(GetLastError());
        } else {
            SERVICE_STATUS status;
            BOOL ok = FALSE;
            if (wb_eq(method, L"StartService"))
                ok = StartServiceW(service, 0, nullptr);
            else if (wb_eq(method, L"StopService"))
                ok = ControlService(service, SERVICE_CONTROL_STOP, &status);
            else if (wb_eq(method, L"PauseService"))
                ok = ControlService(service, SERVICE_CONTROL_PAUSE, &status);
            else if (wb_eq(method, L"ResumeService"))
                ok = ControlService(service, SERVICE_CONTROL_CONTINUE, &status);
            else if (wb_eq(method, L"InterrogateService"))
                ok = ControlService(service, SERVICE_CONTROL_INTERROGATE, &status);
            else if (wb_eq(method, L"Delete"))
                ok = DeleteService(service);
            else if (wb_eq(method, L"ChangeStartMode") && in) {
                WProp *mode = wo_prop(in, L"StartMode");
                DWORD start = SERVICE_NO_CHANGE;
                if (mode && mode->value.vt == VT_BSTR && mode->value.bstrVal) {
                    const WCHAR *m = mode->value.bstrVal;
                    start = wb_eq(m, L"Boot") ? SERVICE_BOOT_START
                            : wb_eq(m, L"System")   ? SERVICE_SYSTEM_START
                            : wb_eq(m, L"Automatic") || wb_eq(m, L"Auto") ? SERVICE_AUTO_START
                            : wb_eq(m, L"Manual") ? SERVICE_DEMAND_START
                            : wb_eq(m, L"Disabled") ? SERVICE_DISABLED
                                                    : SERVICE_NO_CHANGE;
                }
                ok = start != SERVICE_NO_CHANGE &&
                     ChangeServiceConfigW(service, SERVICE_NO_CHANGE, start, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr,
                                          nullptr, nullptr, nullptr, nullptr);
                if (start == SERVICE_NO_CHANGE)
                    SetLastError(ERROR_INVALID_PARAMETER);
            } else {
                CloseServiceHandle(service);
                CloseServiceHandle(manager);
                return WBEM_E_INVALID_METHOD;
            }
            result = ok ? 0 : service_result(GetLastError());
            CloseServiceHandle(service);
        }
        CloseServiceHandle(manager);
    }
    WObj *class_object = wr_find_class(ns, L"Win32_Service");
    WMeth *m = class_object ? wo_meth(class_object, method) : nullptr;
    WObj *result_object = m && m->out ? wo_spawn(m->out) : wo_new(FALSE, L"__PARAMETERS", nullptr);
    put_uint(result_object, L"ReturnValue", result);
    *out = result_object;
    return S_OK;
}

/* ------------------------------------------------------------------ the disk */

/* the sector the data partition starts at, from the sector table of mimefs: recovery, efi system and msr come before. */
#define DISK_DATA_FIRST 411648ull
#define DISK_SECTORS_TAIL 34ull /* the backup table and header at the end of the disk. */

static ULONGLONG disk_bytes(void)
{
    ULONGLONG total = 0, free_bytes = 0, total_free = 0;
    WCHAR windows[MAX_PATH];
    if (!GetWindowsDirectoryW(windows, MAX_PATH))
        return 0;
    WCHAR root[4] = {windows[0], ':', '\\', 0};
    if (!GetDiskFreeSpaceExW(root, (PULARGE_INTEGER)&free_bytes, (PULARGE_INTEGER)&total, (PULARGE_INTEGER)&total_free))
        return 0;
    return total;
}

static BOOL diskdrive_enum(WNs *ns, WObj *c, WEmit emit, void *context)
{
    ULONGLONG bytes = disk_bytes();
    WObj *o = wo_spawn(c);
    put_str(o, L"DeviceID", L"\\\\.\\PHYSICALDRIVE0");
    put_str(o, L"Name", L"\\\\.\\PHYSICALDRIVE0");
    put_str(o, L"Caption", L"SAMSUNG MZVL21T0HCLR-00BL2");
    put_str(o, L"Description", L"Disk drive");
    put_str(o, L"Model", L"SAMSUNG MZVL21T0HCLR-00BL2");
    put_str(o, L"Manufacturer", L"(Standard disk drives)");
    put_str(o, L"InterfaceType", L"SCSI");
    put_str(o, L"MediaType", L"Fixed hard disk media");
    put_str(o, L"PNPDeviceID", L"SCSI\\DISK&VEN_NVME&PROD_SAMSUNG_MZVL21T0\\5&2A9C1B3F&0&000000");
    put_str(o, L"SerialNumber", L"0025_38B1_81A3_5C2F.");
    put_str(o, L"FirmwareRevision", L"AGXA0101");
    put_uint(o, L"Signature", 0x4c3a91b7);
    put_str(o, L"Status", L"OK");
    put_str(o, L"CreationClassName", L"Win32_DiskDrive");
    put_str(o, L"SystemName", computer_name());
    put_str(o, L"SystemCreationClassName", L"Win32_ComputerSystem");
    put_uint(o, L"Index", 0);
    put_uint(o, L"Partitions", 4);
    put_uint(o, L"BytesPerSector", 512);
    put_uint(o, L"SectorsPerTrack", 63);
    put_uint(o, L"TracksPerCylinder", 255);
    put_uint(o, L"TotalSectors", bytes / 512);
    put_uint(o, L"TotalCylinders", bytes / 512 / (63 * 255));
    put_uint(o, L"TotalHeads", 255);
    put_uint(o, L"TotalTracks", bytes / 512 / 63);
    put_uint(o, L"Size", bytes);
    put_uint(o, L"ConfigManagerErrorCode", 0);
    put_bool(o, L"ConfigManagerUserConfig", FALSE);
    BOOL more = emit(context, o);
    wo_free(o);
    return more;
}

/* one entry of the partition table, as wmi names the parts of the layout. */
static void put_partition(WObj *o, ULONG index, const WCHAR *type, ULONGLONG first, ULONGLONG last, BOOL bootable,
                          BOOL primary)
{
    WCHAR id[32], name[32];
    _snwprintf(id, RTL_NUMBER_OF(id), L"Disk #0, Partition #%u", index);
    _snwprintf(name, RTL_NUMBER_OF(name), L"Disk #0, Partition #%u", index);
    put_str(o, L"DeviceID", id);
    put_str(o, L"Name", name);
    put_str(o, L"Caption", id);
    put_str(o, L"Description", type);
    put_str(o, L"Type", type);
    put_str(o, L"Status", L"OK");
    put_str(o, L"CreationClassName", L"Win32_DiskPartition");
    put_str(o, L"SystemName", computer_name());
    put_str(o, L"SystemCreationClassName", L"Win32_ComputerSystem");
    put_uint(o, L"DiskIndex", 0);
    put_uint(o, L"Index", index);
    put_uint(o, L"NumberOfBlocks", last >= first ? last - first + 1 : 0);
    put_uint(o, L"StartingOffset", first * 512);
    put_uint(o, L"Size", last >= first ? (last - first + 1) * 512 : 0);
    put_bool(o, L"Bootable", bootable);
    put_bool(o, L"BootPartition", bootable);
    put_bool(o, L"PrimaryPartition", primary);
}

static BOOL partition_enum(WNs *ns, WObj *c, WEmit emit, void *context)
{
    ULONGLONG sectors = disk_bytes() / 512;
    ULONGLONG last = sectors > DISK_DATA_FIRST + DISK_SECTORS_TAIL ? sectors - DISK_SECTORS_TAIL : DISK_DATA_FIRST;
    struct {
        const WCHAR *type;
        ULONGLONG first, last;
        BOOL bootable, primary;
    } parts[] = {
        {L"GPT: Unknown", 2048, 208895, FALSE, FALSE},
        {L"GPT: System", 208896, 395263, FALSE, FALSE},
        {L"GPT: Reserved", 395264, 411647, FALSE, FALSE},
        {L"GPT: Basic Data", DISK_DATA_FIRST, last, TRUE, TRUE},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(parts); i++) {
        WObj *o = wo_spawn(c);
        put_partition(o, i, parts[i].type, parts[i].first, parts[i].last, parts[i].bootable, parts[i].primary);
        BOOL more = emit(context, o);
        wo_free(o);
        if (!more)
            return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the table */

typedef struct Native {
    const WCHAR *class_name;
    BOOL (*enumerate)(WNs *ns, WObj *class_object, WEmit emit, void *context);
    HRESULT (*method)(WNs *ns, const WCHAR *key, const WCHAR *method, WObj *in, WObj **out);
} Native;

static const Native NATIVES[] = {
    {L"Win32_OperatingSystem", os_enum, nullptr}, {L"Win32_ComputerSystem", computer_enum, nullptr},
    {L"Win32_Processor", processor_enum, nullptr}, {L"Win32_PhysicalMemory", memory_enum, nullptr},
    {L"Win32_LogicalDisk", disk_enum, nullptr},    {L"Win32_VideoController", video_enum, nullptr},
    {L"Win32_Battery", battery_enum, nullptr},    {L"Win32_DiskDrive", diskdrive_enum, nullptr},
    {L"Win32_DiskPartition", partition_enum, nullptr},
    {L"Win32_Service", service_enum, service_method},
};

static const Native *find_native(const WCHAR *name)
{
    for (UINT i = 0; i < RTL_NUMBER_OF(NATIVES); i++)
        if (wb_eq(NATIVES[i].class_name, name))
            return &NATIVES[i];
    return nullptr;
}

/* ------------------------------------------------------------------ the entry points */

HRESULT wprov_enum(WNs *ns, const WCHAR *class_name, BOOL deep, WEmit emit, void *context)
{
    WObj *base = wr_find_class(ns, class_name);
    if (!base)
        return WBEM_E_INVALID_CLASS;
    WObj **subs = nullptr;
    UINT sub_count = deep ? wr_subclasses(ns, class_name, TRUE, &subs) : 0;
    HRESULT hr = S_OK;
    for (UINT i = 0; i <= sub_count && SUCCEEDED(hr); i++) {
        WObj *c = i ? subs[i - 1] : base;
        if (wq_true(&c->quals, L"abstract"))
            continue;
        const Native *native = find_native(c->name);
        if (native && !native->enumerate(ns, c, emit, context))
            break;
        BOOL stop = FALSE;
        for (UINT k = 0; k < ns->instance_count && !stop; k++) {
            if (!wb_eq(ns->instances[k]->name, c->name))
                continue;
            WObj *copy = wo_clone(ns->instances[k]);
            copy->builtin = FALSE;
            stop = !emit(context, copy);
            wo_free(copy);
        }
        if (stop)
            break;
    }
    wb_free(subs);
    return hr;
}

typedef struct FindState {
    const WPath *path;
    WObj *found;
} FindState;

static BOOL key_matches(const WObj *o, const WPath *path)
{
    for (UINT i = 0; i < path->key_count; i++) {
        const WKey *k = &path->keys[i];
        const WProp *p = nullptr;
        if (k->name) {
            p = wo_prop(o, k->name);
        } else {
            for (UINT j = 0; j < o->prop_count; j++)
                if (wq_true(&o->props[j].quals, L"key")) {
                    p = &o->props[j];
                    break;
                }
        }
        if (!p || !wq_true(&p->quals, L"key"))
            return FALSE;
        VARIANT want;
        wv_init(&want);
        if (FAILED(wv_coerce(&want, &k->value, p->type)))
            return FALSE;
        int order;
        BOOL same = wv_compare(&p->value, &want, p->type, &order) && order == 0;
        VariantClear(&want);
        if (!same)
            return FALSE;
    }
    return path->key_count > 0 || path->singleton;
}

static BOOL find_emit(void *context, WObj *object)
{
    FindState *s = context;
    if (!key_matches(object, s->path))
        return TRUE;
    s->found = wo_clone(object);
    return FALSE;
}

HRESULT wprov_get(WNs *ns, const WCHAR *relpath, WObj **object)
{
    WPath path;
    wp_init(&path);
    HRESULT hr = wp_parse(&path, relpath, WBEMPATH_CREATE_ACCEPT_ALL);
    if (FAILED(hr) || !path.has_class || !path.class_name) {
        wp_free(&path);
        return FAILED(hr) ? hr : WBEM_E_INVALID_OBJECT_PATH;
    }
    WObj *c = wr_find_class(ns, path.class_name);
    if (!c) {
        wp_free(&path);
        return WBEM_E_NOT_FOUND;
    }
    if (!path.key_count && !path.singleton) {
        *object = wo_clone(c);
        wp_free(&path);
        return S_OK;
    }
    FindState state = {&path, nullptr};
    wprov_enum(ns, c->name, FALSE, find_emit, &state);
    wp_free(&path);
    if (!state.found)
        return WBEM_E_NOT_FOUND;
    *object = state.found;
    return S_OK;
}

HRESULT wprov_put(WNs *ns, WObj *instance, LONG flags)
{
    if (find_native(instance->name))
        return WBEM_E_PROVIDER_NOT_CAPABLE;
    return wr_put_instance(ns, instance, flags);
}

HRESULT wprov_delete(WNs *ns, const WCHAR *relpath)
{
    WPath path;
    wp_init(&path);
    HRESULT hr = wp_parse(&path, relpath, WBEMPATH_CREATE_ACCEPT_ALL);
    if (SUCCEEDED(hr) && path.class_name && find_native(path.class_name))
        hr = WBEM_E_PROVIDER_NOT_CAPABLE;
    else if (SUCCEEDED(hr))
        hr = wr_delete_instance(ns, relpath);
    wp_free(&path);
    return hr;
}

HRESULT wprov_exec(WNs *ns, const WCHAR *object, const WCHAR *method, WObj *in, WObj **out)
{
    WPath path;
    wp_init(&path);
    HRESULT hr = wp_parse(&path, object, WBEMPATH_CREATE_ACCEPT_ALL);
    if (FAILED(hr) || !path.class_name) {
        wp_free(&path);
        return FAILED(hr) ? hr : WBEM_E_INVALID_OBJECT_PATH;
    }
    WObj *c = wr_find_class(ns, path.class_name);
    if (!c) {
        wp_free(&path);
        return WBEM_E_NOT_FOUND;
    }
    WMeth *m = wo_meth(c, method);
    if (!m) {
        wp_free(&path);
        return WBEM_E_METHOD_NOT_IMPLEMENTED;
    }
    const Native *native = nullptr;
    for (UINT i = 0; i < c->derivation_count + 1 && !native; i++)
        native = find_native(i ? c->derivation[i - 1] : c->name);
    if (native && native->method) {
        WCHAR *key = nullptr;
        if (path.key_count && path.keys[0].value.vt == VT_BSTR)
            key = wb_wcsdup(path.keys[0].value.bstrVal);
        hr = key ? native->method(ns, key, method, in, out) : WBEM_E_INVALID_OBJECT_PATH;
        wb_free(key);
    } else {
        hr = WBEM_E_METHOD_NOT_IMPLEMENTED;
    }
    wp_free(&path);
    return hr;
}
