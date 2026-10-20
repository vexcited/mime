/*
 * system information: processor and memory, versions, computer name and
 * the logical processor topology.
 */
#include "kernelbase_private.h"

#define SYSTEM_PERFORMANCE_AVAILABLE_PAGES 0x4C

static void basic_information(SYSTEM_BASIC_INFORMATION *basic)
{
    memset(basic, 0, sizeof(*basic));
    if (!NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, basic, sizeof(*basic), nullptr))) {
        basic->PageSize = 0x1000;
        basic->AllocationGranularity = 0x10000;
        basic->MinimumUserModeAddress = 0x10000;
        basic->MaximumUserModeAddress = (ULONG_PTR)(sizeof(void *) == 4 ? 0x7FFEFFFFull : 0x7FFFFFFEFFFFull);
        basic->NumberOfProcessors = 1;
        basic->ActiveProcessorsAffinityMask = 1;
        basic->NumberOfPhysicalPages = USER_SHARED_DATA->NumberOfPhysicalPages;
    }
}

static ULONG processor_count(void)
{
    ULONG n = USER_SHARED_DATA->ActiveProcessorCount;
    return n ? n : 1;
}

/* ------------------------------------------------------------------ system info */

/* the processor of the machine for GetNativeSystemInfo, the one a translated program is shown for GetSystemInfo. */
static void system_info(LPSYSTEM_INFO info, BOOL native)
{
    SYSTEM_BASIC_INFORMATION basic;
    SYSTEM_PROCESSOR_INFORMATION cpu = {PROCESSOR_ARCHITECTURE_ARM64, 8, 0, 1, 0};
    basic_information(&basic);
    NtQuerySystemInformation(SystemProcessorInformation, &cpu, sizeof(cpu), nullptr);
    USHORT machine = cpu.ProcessorArchitecture;
#if (defined(__x86_64__) && !defined(MIME_X64_NATIVE)) || defined(__i386__)
    /* the processor the translator presents, family 6 model 0x9e stepping 10 as its cpuid says. */
#ifdef __i386__
    cpu.ProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
#else
    cpu.ProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
#endif
    cpu.ProcessorLevel = 6;
    cpu.ProcessorRevision = 0x9E0A;
#endif
    memset(info, 0, sizeof(*info));
    info->wProcessorArchitecture = native ? machine : cpu.ProcessorArchitecture;
    info->dwPageSize = basic.PageSize;
    info->lpMinimumApplicationAddress = (LPVOID)basic.MinimumUserModeAddress;
    info->lpMaximumApplicationAddress = (LPVOID)basic.MaximumUserModeAddress;
    info->dwActiveProcessorMask = basic.ActiveProcessorsAffinityMask;
    info->dwNumberOfProcessors = (DWORD)(UCHAR)basic.NumberOfProcessors;
    switch (info->wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        info->dwProcessorType = PROCESSOR_AMD_X8664;
        break;
    case PROCESSOR_ARCHITECTURE_INTEL:
        info->dwProcessorType = PROCESSOR_INTEL_PENTIUM;
        break;
    default:
        info->dwProcessorType = 0;
        break;
    }
    info->dwAllocationGranularity = basic.AllocationGranularity;
    info->wProcessorLevel = cpu.ProcessorLevel;
    info->wProcessorRevision = cpu.ProcessorRevision;
}

void WINAPI GetSystemInfo(LPSYSTEM_INFO info)
{
    system_info(info, FALSE);
}

/* the machine the system runs on: a 32-bit program on a 64-bit system, or an emulated one, is told the machine's. */
void WINAPI GetNativeSystemInfo(LPSYSTEM_INFO info)
{
    system_info(info, TRUE);
}

/* ------------------------------------------------------------------ versions */

/*
 * programs without a compatibility manifest are told they run on windows
 * 8, version 6.2.9200, like windows does, and the windows 8.1 or 10 ids of
 * their manifest earn them more: KbReportedVersion decides it.
 * RtlGetVersion keeps reporting the truth.
 */

/* the handle count of 16-bit windows: there is no limit to raise. */
UINT WINAPI SetHandleCount(UINT count) { return count; }

DWORD WINAPI GetVersion(void)
{
    ULONG major, minor, build;
    KbReportedVersion(&major, &minor, &build);
    return ((DWORD)build << 16) | ((DWORD)minor << 8) | major;
}

BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW info)
{
    if (!info || (info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOW) &&
                  info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOEXW))) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    ULONG major, minor, build;
    KbReportedVersion(&major, &minor, &build);
    info->dwMajorVersion = major;
    info->dwMinorVersion = minor;
    info->dwBuildNumber = build;
    info->dwPlatformId = VER_PLATFORM_WIN32_NT;
    info->szCSDVersion[0] = 0;
    if (info->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXW)) {
        OSVERSIONINFOEXW *ex = (OSVERSIONINFOEXW *)info;
        ex->wServicePackMajor = 0;
        ex->wServicePackMinor = 0;
        ex->wSuiteMask = VER_SUITE_SINGLEUSERTS;
        ex->wProductType = VER_NT_WORKSTATION;
        ex->wReserved = 0;
    }
    return TRUE;
}

BOOL WINAPI GetVersionExA(LPOSVERSIONINFOA info)
{
    if (!info || (info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOA) &&
                  info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOEXA))) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    OSVERSIONINFOEXW w;
    w.dwOSVersionInfoSize = sizeof(w);
    GetVersionExW((LPOSVERSIONINFOW)&w);
    info->dwMajorVersion = w.dwMajorVersion;
    info->dwMinorVersion = w.dwMinorVersion;
    info->dwBuildNumber = w.dwBuildNumber;
    info->dwPlatformId = w.dwPlatformId;
    info->szCSDVersion[0] = 0;
    if (info->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXA)) {
        OSVERSIONINFOEXA *ex = (OSVERSIONINFOEXA *)info;
        ex->wServicePackMajor = w.wServicePackMajor;
        ex->wServicePackMinor = w.wServicePackMinor;
        ex->wSuiteMask = w.wSuiteMask;
        ex->wProductType = w.wProductType;
        ex->wReserved = 0;
    }
    return TRUE;
}

static BYTE condition_of(ULONGLONG mask, DWORD type)
{
    for (int bit = 0; bit < 8; bit++)
        if (type == (1u << bit))
            return (BYTE)((mask >> (bit * 3)) & 7);
    return 0;
}

static BOOL compare_values(ULONG current, ULONG wanted, BYTE condition)
{
    switch (condition) {
    case VER_EQUAL:
        return current == wanted;
    case VER_GREATER:
        return current > wanted;
    case VER_GREATER_EQUAL:
        return current >= wanted;
    case VER_LESS:
        return current < wanted;
    case VER_LESS_EQUAL:
        return current <= wanted;
    default:
        return FALSE;
    }
}

BOOL WINAPI VerifyVersionInfoW(LPOSVERSIONINFOEXW info, DWORD type, DWORDLONG condition)
{
    if (!type || !condition || !info) {
        KbSetError(ERROR_BAD_ARGUMENTS);
        return FALSE;
    }
    /* the version compared is the one GetVersionEx reports, as RtlSwitchedVVI does it on windows. */
    RTL_OSVERSIONINFOEXW cur;
    cur.dwOSVersionInfoSize = sizeof(cur);
    RtlGetVersion(&cur);
    KbReportedVersion(&cur.dwMajorVersion, &cur.dwMinorVersion, &cur.dwBuildNumber);
    BOOL ok = TRUE;
    if (type & VER_BUILDNUMBER)
        ok = ok && compare_values(cur.dwBuildNumber, info->dwBuildNumber, condition_of(condition, VER_BUILDNUMBER));
    if (type & VER_PLATFORMID)
        ok = ok && compare_values(cur.dwPlatformId, info->dwPlatformId, condition_of(condition, VER_PLATFORMID));
    if (type & VER_PRODUCT_TYPE)
        ok = ok && compare_values(cur.wProductType, info->wProductType, condition_of(condition, VER_PRODUCT_TYPE));
    if (type & VER_SUITENAME) {
        BYTE c = condition_of(condition, VER_SUITENAME);
        if (c == VER_AND)
            ok = ok && (cur.wSuiteMask & info->wSuiteMask) == info->wSuiteMask;
        else if (c == VER_OR)
            ok = ok && (cur.wSuiteMask & info->wSuiteMask) != 0;
        else
            ok = FALSE;
    }
    /* major, minor and service pack compare as one hierarchical version number. */
    static const DWORD order[] = {VER_MAJORVERSION, VER_MINORVERSION, VER_SERVICEPACKMAJOR, VER_SERVICEPACKMINOR};
    ULONG current[4] = {cur.dwMajorVersion, cur.dwMinorVersion, cur.wServicePackMajor, cur.wServicePackMinor};
    ULONG wanted[4] = {info->dwMajorVersion, info->dwMinorVersion, info->wServicePackMajor, info->wServicePackMinor};
    BYTE last = 0;
    BOOL decided = FALSE;
    for (int i = 0; i < 4 && ok && !decided; i++) {
        if (!(type & order[i]))
            continue;
        BYTE c = condition_of(condition, order[i]);
        last = c;
        if (current[i] != wanted[i]) {
            ok = compare_values(current[i], wanted[i], c);
            decided = TRUE;
        }
    }
    if (ok && !decided && last)
        ok = last == VER_EQUAL || last == VER_GREATER_EQUAL || last == VER_LESS_EQUAL;
    if (!ok) {
        KbSetError(ERROR_OLD_WIN_VERSION);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI VerifyVersionInfoA(LPOSVERSIONINFOEXA info, DWORD type, DWORDLONG condition)
{
    if (!info) {
        KbSetError(ERROR_BAD_ARGUMENTS);
        return FALSE;
    }
    OSVERSIONINFOEXW w;
    memset(&w, 0, sizeof(w));
    w.dwOSVersionInfoSize = sizeof(w);
    w.dwMajorVersion = info->dwMajorVersion;
    w.dwMinorVersion = info->dwMinorVersion;
    w.dwBuildNumber = info->dwBuildNumber;
    w.dwPlatformId = info->dwPlatformId;
    w.wServicePackMajor = info->wServicePackMajor;
    w.wServicePackMinor = info->wServicePackMinor;
    w.wSuiteMask = info->wSuiteMask;
    w.wProductType = info->wProductType;
    return VerifyVersionInfoW(&w, type, condition);
}

BOOL WINAPI GetProductInfo(DWORD major, DWORD minor, DWORD sp_major, DWORD sp_minor, PDWORD type)
{
    if (!type) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *type = major >= 6 ? PRODUCT_PROFESSIONAL : PRODUCT_UNDEFINED;
    return TRUE;
}

BOOL WINAPI IsProcessorFeaturePresent(DWORD feature)
{
    return RtlIsProcessorFeaturePresent(feature);
}

/* ------------------------------------------------------------------ memory status */

BOOL WINAPI GlobalMemoryStatusEx(LPMEMORYSTATUSEX status)
{
    if (!status || status->dwLength != sizeof(*status)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SYSTEM_BASIC_INFORMATION basic;
    basic_information(&basic);
    UCHAR perf[0x158];
    ULONGLONG total = (ULONGLONG)basic.NumberOfPhysicalPages * basic.PageSize;
    ULONGLONG available = total / 2, limit = total * 2, committed = total - available;
    if (NT_SUCCESS(NtQuerySystemInformation(SystemPerformanceInformation, perf, sizeof(perf), nullptr))) {
        available = (ULONGLONG)*(PULONG)(perf + SYSTEM_PERFORMANCE_AVAILABLE_PAGES) * basic.PageSize;
        committed = (ULONGLONG)*(PULONG)(perf + 0x50) * basic.PageSize;
        if (*(PULONG)(perf + 0x54))
            limit = (ULONGLONG)*(PULONG)(perf + 0x54) * basic.PageSize;
    }
    if (available > total)
        available = total;
    if (committed > limit)
        committed = limit;
    status->dwMemoryLoad = total ? (DWORD)((total - available) * 100 / total) : 0;
    status->ullTotalPhys = total;
    status->ullAvailPhys = available;
    /* the commit limit and what is left of it, as windows reports them. */
    status->ullTotalPageFile = limit;
    status->ullAvailPageFile = limit - committed;
    status->ullTotalVirtual = basic.MaximumUserModeAddress - basic.MinimumUserModeAddress + 1;
    /* what is left of the address space is what the process has not reserved. */
    SIZE_T counters[sizeof(void *) == 8 ? 14 : 12] = {};
    ULONGLONG reserved = total - available;
    if (NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessVmCounters, counters, sizeof(counters), nullptr)))
        reserved = counters[1];
    status->ullAvailVirtual = reserved < status->ullTotalVirtual ? status->ullTotalVirtual - reserved : 0;
    status->ullAvailExtendedVirtual = 0;
    return TRUE;
}

void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS status)
{
    MEMORYSTATUSEX ex;
    ex.dwLength = sizeof(ex);
    GlobalMemoryStatusEx(&ex);
    status->dwLength = sizeof(*status);
    status->dwMemoryLoad = ex.dwMemoryLoad;
    status->dwTotalPhys = ex.ullTotalPhys;
    status->dwAvailPhys = ex.ullAvailPhys;
    status->dwTotalPageFile = ex.ullTotalPageFile;
    status->dwAvailPageFile = ex.ullAvailPageFile;
    status->dwTotalVirtual = ex.ullTotalVirtual;
    status->dwAvailVirtual = ex.ullAvailVirtual;
}

BOOL WINAPI GetPhysicallyInstalledSystemMemory(PULONGLONG kilobytes)
{
    if (!kilobytes) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SYSTEM_BASIC_INFORMATION basic;
    basic_information(&basic);
    *kilobytes = (ULONGLONG)basic.NumberOfPhysicalPages * basic.PageSize / 1024;
    return TRUE;
}

/* ------------------------------------------------------------------ computer name */

/* reads a string value of the machine registry, as kernelbase does for computer names. */
static DWORD read_machine_string(PCWSTR key, PCWSTR name, PWSTR out, DWORD capacity)
{
    UNICODE_STRING path, value;
    RtlInitUnicodeString(&path, key);
    RtlInitUnicodeString(&value, name);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE h;
    out[0] = 0;
    if (!NT_SUCCESS(NtOpenKey(&h, KEY_READ, &oa)))
        return 0;
    ULONG buffer[(12 + 64 * sizeof(WCHAR)) / sizeof(ULONG) + 1];
    ULONG length;
    NTSTATUS status = NtQueryValueKey(h, &value, 2 /* KeyValuePartialInformation */, buffer, sizeof(buffer), &length);
    NtClose(h);
    if (!NT_SUCCESS(status) || (buffer[1] != REG_SZ && buffer[1] != REG_EXPAND_SZ))
        return 0;
    DWORD n = buffer[2] / sizeof(WCHAR);
    PCWSTR text = (PCWSTR)&buffer[3];
    while (n && !text[n - 1])
        n--;
    if (n >= capacity)
        n = capacity - 1;
    memcpy(out, text, n * sizeof(WCHAR));
    out[n] = 0;
    return n;
}

static DWORD computer_name(PWSTR out, DWORD capacity)
{
    return read_machine_string(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\ComputerName\\ActiveComputerName",
                               L"ComputerName", out, capacity);
}

BOOL WINAPI GetComputerNameW(LPWSTR buffer, LPDWORD size)
{
    if (!size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR name[64];
    DWORD n = computer_name(name, RTL_NUMBER_OF(name));
    if (*size <= n || !buffer) {
        *size = n + 1;
        KbSetError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    memcpy(buffer, name, (n + 1) * sizeof(WCHAR));
    *size = n;
    return TRUE;
}

BOOL WINAPI GetComputerNameA(LPSTR buffer, LPDWORD size)
{
    if (!size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR name[64];
    DWORD n = computer_name(name, RTL_NUMBER_OF(name));
    if (*size <= n || !buffer) {
        *size = n + 1;
        KbSetError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    for (DWORD i = 0; i <= n; i++)
        buffer[i] = (CHAR)name[i];
    *size = n;
    return TRUE;
}

BOOL WINAPI GetComputerNameExW(COMPUTER_NAME_FORMAT format, LPWSTR buffer, LPDWORD size)
{
    WCHAR value[64];
    PCWSTR tcpip = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\Tcpip\\Parameters";
    switch (format) {
    case ComputerNameNetBIOS:
    case ComputerNamePhysicalNetBIOS:
        computer_name(value, RTL_NUMBER_OF(value));
        break;
    case ComputerNameDnsHostname:
    case ComputerNamePhysicalDnsHostname:
    case ComputerNameDnsFullyQualified:
    case ComputerNamePhysicalDnsFullyQualified:
        if (!read_machine_string(tcpip, L"Hostname", value, RTL_NUMBER_OF(value)))
            computer_name(value, RTL_NUMBER_OF(value));
        break;
    case ComputerNameDnsDomain:
    case ComputerNamePhysicalDnsDomain:
        read_machine_string(tcpip, L"Domain", value, RTL_NUMBER_OF(value));
        break;
    default:
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD n = (DWORD)wcslen(value);
    if (*size <= n || !buffer) {
        *size = n + 1;
        KbSetError(ERROR_MORE_DATA);
        return FALSE;
    }
    memcpy(buffer, value, (n + 1) * sizeof(WCHAR));
    *size = n;
    return TRUE;
}

BOOL WINAPI GetComputerNameExA(COMPUTER_NAME_FORMAT format, LPSTR buffer, LPDWORD size)
{
    if (!size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR w[64];
    DWORD n = RTL_NUMBER_OF(w);
    if (!GetComputerNameExW(format, w, &n))
        return FALSE;
    if (*size <= n || !buffer) {
        *size = n + 1;
        KbSetError(ERROR_MORE_DATA);
        return FALSE;
    }
    WideCharToMultiByte(CP_ACP, 0, w, -1, buffer, (int)*size, nullptr, nullptr);
    *size = n;
    return TRUE;
}

BOOL WINAPI SetComputerNameW(LPCWSTR name)
{
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}

/* ------------------------------------------------------------------ processor topology */

#define L1_SIZE (64 * 1024)
#define L2_SIZE (4 * 1024 * 1024)

BOOL WINAPI GetLogicalProcessorInformation(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer, PDWORD length)
{
    if (!length) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG n = processor_count();
    /* per core: the core and its two first level caches; then the l2, the package and the node. */
    DWORD count = n * 3 + 3;
    DWORD need = count * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    if (*length < need || !buffer) {
        *length = need;
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    ULONG_PTR all = n >= 64 ? ~(ULONG_PTR)0 : (((ULONG_PTR)1 << n) - 1);
    memset(buffer, 0, need);
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION p = buffer;
    for (ULONG i = 0; i < n; i++) {
        p->ProcessorMask = (ULONG_PTR)1 << i;
        p->Relationship = RelationProcessorCore;
        p++;
        for (int k = 0; k < 2; k++) {
            p->ProcessorMask = (ULONG_PTR)1 << i;
            p->Relationship = RelationCache;
            p->Cache.Level = 1;
            p->Cache.Associativity = 8;
            p->Cache.LineSize = 64;
            p->Cache.Size = L1_SIZE;
            p->Cache.Type = k ? CacheInstruction : CacheData;
            p++;
        }
    }
    p->ProcessorMask = all;
    p->Relationship = RelationCache;
    p->Cache.Level = 2;
    p->Cache.Associativity = 16;
    p->Cache.LineSize = 128;
    p->Cache.Size = L2_SIZE;
    p->Cache.Type = CacheUnified;
    p++;
    p->ProcessorMask = all;
    p->Relationship = RelationProcessorPackage;
    p++;
    p->ProcessorMask = all;
    p->Relationship = RelationNumaNode;
    p->NumaNode.NodeNumber = 0;
    *length = need;
    return TRUE;
}

static DWORD append_ex(PUCHAR out, DWORD used, DWORD capacity, LOGICAL_PROCESSOR_RELATIONSHIP rel, DWORD size,
                       const void *body, DWORD body_size)
{
    if (out && used + size <= capacity) {
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX e = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(out + used);
        memset(e, 0, size);
        e->Relationship = rel;
        e->Size = size;
        memcpy(&e->Processor, body, body_size);
    }
    return used + size;
}

BOOL WINAPI GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP relationship,
                                             PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer, PDWORD length)
{
    if (!length) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG n = processor_count();
    KAFFINITY all = n >= 64 ? ~(KAFFINITY)0 : (((KAFFINITY)1 << n) - 1);
    BOOL any = relationship == RelationAll;
    DWORD capacity = *length;
    PUCHAR out = (PUCHAR)buffer;
    DWORD used = 0;
    DWORD header = (DWORD)offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor);
    if (any || relationship == RelationProcessorCore) {
        for (ULONG i = 0; i < n; i++) {
            PROCESSOR_RELATIONSHIP core;
            memset(&core, 0, sizeof(core));
            core.GroupCount = 1;
            core.GroupMask[0].Mask = (KAFFINITY)1 << i;
            used = append_ex(out, used, capacity, RelationProcessorCore, header + sizeof(core), &core, sizeof(core));
        }
    }
    if (any || relationship == RelationCache) {
        for (ULONG i = 0; i < n; i++) {
            for (int k = 0; k < 2; k++) {
                CACHE_RELATIONSHIP c;
                memset(&c, 0, sizeof(c));
                c.Level = 1;
                c.Associativity = 8;
                c.LineSize = 64;
                c.CacheSize = L1_SIZE;
                c.Type = k ? CacheInstruction : CacheData;
                c.GroupCount = 1;
                c.GroupMask.Mask = (KAFFINITY)1 << i;
                used = append_ex(out, used, capacity, RelationCache, header + sizeof(c), &c, sizeof(c));
            }
        }
        CACHE_RELATIONSHIP c;
        memset(&c, 0, sizeof(c));
        c.Level = 2;
        c.Associativity = 16;
        c.LineSize = 128;
        c.CacheSize = L2_SIZE;
        c.Type = CacheUnified;
        c.GroupCount = 1;
        c.GroupMask.Mask = all;
        used = append_ex(out, used, capacity, RelationCache, header + sizeof(c), &c, sizeof(c));
    }
    if (any || relationship == RelationProcessorPackage) {
        PROCESSOR_RELATIONSHIP package;
        memset(&package, 0, sizeof(package));
        package.GroupCount = 1;
        package.GroupMask[0].Mask = all;
        used = append_ex(out, used, capacity, RelationProcessorPackage, header + sizeof(package), &package,
                         sizeof(package));
    }
    if (any || relationship == RelationNumaNode || relationship == RelationNumaNodeEx) {
        NUMA_NODE_RELATIONSHIP node;
        memset(&node, 0, sizeof(node));
        node.GroupCount = 1;
        node.GroupMask.Mask = all;
        used = append_ex(out, used, capacity, RelationNumaNode, header + sizeof(node), &node, sizeof(node));
    }
    if (any || relationship == RelationGroup) {
        GROUP_RELATIONSHIP group;
        memset(&group, 0, sizeof(group));
        group.MaximumGroupCount = 1;
        group.ActiveGroupCount = 1;
        group.GroupInfo[0].MaximumProcessorCount = (BYTE)n;
        group.GroupInfo[0].ActiveProcessorCount = (BYTE)n;
        group.GroupInfo[0].ActiveProcessorMask = all;
        used = append_ex(out, used, capacity, RelationGroup, header + sizeof(group), &group, sizeof(group));
    }
    if (!used) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (used > capacity || !buffer) {
        *length = used;
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    *length = used;
    return TRUE;
}

WORD WINAPI GetActiveProcessorGroupCount(void)
{
    return 1;
}

WORD WINAPI GetMaximumProcessorGroupCount(void)
{
    return 1;
}

DWORD WINAPI GetActiveProcessorCount(WORD group)
{
    if (group && group != 0xFFFF) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return processor_count();
}

DWORD WINAPI GetMaximumProcessorCount(WORD group)
{
    return GetActiveProcessorCount(group);
}

BOOL WINAPI GetNumaHighestNodeNumber(PULONG node)
{
    if (!node) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *node = 0;
    return TRUE;
}

BOOL WINAPI GetNumaProcessorNode(UCHAR processor, PUCHAR node)
{
    if (processor >= processor_count()) {
        *node = 0xFF;
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *node = 0;
    return TRUE;
}

BOOL WINAPI GetNumaNodeProcessorMask(UCHAR node, PULONGLONG mask)
{
    if (node) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG n = processor_count();
    *mask = n >= 64 ? ~0ull : ((1ull << n) - 1);
    return TRUE;
}

BOOL WINAPI GetNumaAvailableMemoryNode(UCHAR node, PULONGLONG bytes)
{
    if (node) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    *bytes = status.ullAvailPhys;
    return TRUE;
}

/*
 * the firmware tables of the machine, the raw smbios data. the request
 * goes to the kernel through SystemFirmwareTableInformation, like the
 * windows implementation does, with the wdm buffer protocol: a too small
 * buffer learns the required size in TableBufferLength.
 */
static UINT firmware_tables(ULONG action, DWORD provider, DWORD id, PVOID buffer, DWORD size, ULONG *ids)
{
    if (provider != 'RSMB') {
        KbSetError(ERROR_NOT_FOUND);
        return 0;
    }
    SYSTEM_FIRMWARE_TABLE_INFORMATION *request = KbAlloc(offsetof(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer) + size);
    if (!request) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    request->ProviderSignature = provider;
    request->Action = action;
    request->TableId = id;
    request->TableBufferLength = size;
    ULONG returned = 0;
    NTSTATUS status = NtQuerySystemInformation(SystemFirmwareTableInformation, request,
                                               offsetof(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer) + size,
                                               &returned);
    UINT result = 0;
    if (status == STATUS_SUCCESS) {
        PVOID out = ids ? ids : buffer;
        if (out)
            memcpy(out, request->TableBuffer, request->TableBufferLength);
        result = request->TableBufferLength;
    } else if (status == STATUS_BUFFER_TOO_SMALL) {
        /* a null buffer: the required size. a too small one: an error. */
        if (size) {
            result = 0;
            KbSetError(ERROR_INSUFFICIENT_BUFFER);
        } else {
            result = request->TableBufferLength;
        }
    } else {
        KbSetStatus(status);
    }
    KbFree(request);
    return result;
}

UINT WINAPI GetSystemFirmwareTable(DWORD provider, DWORD id, PVOID buffer, DWORD size)
{
    return firmware_tables(SystemFirmwareTable_Get, provider, id, buffer, size, nullptr);
}

UINT WINAPI EnumSystemFirmwareTables(DWORD provider, PVOID buffer, DWORD size)
{
    return firmware_tables(SystemFirmwareTable_Enumerate, provider, 0, buffer, size, buffer);
}

/* ------------------------------------------------------------------ power */

BOOL WINAPI GetSystemPowerStatus(LPSYSTEM_POWER_STATUS status)
{
    if (!status) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    status->ACLineStatus = 1;
    status->BatteryFlag = 128;
    status->BatteryLifePercent = 255;
    status->SystemStatusFlag = 0;
    status->BatteryLifeTime = (DWORD)-1;
    status->BatteryFullLifeTime = (DWORD)-1;
    return TRUE;
}

static EXECUTION_STATE g_execution_state = ES_CONTINUOUS;

EXECUTION_STATE WINAPI SetThreadExecutionState(EXECUTION_STATE flags)
{
    EXECUTION_STATE old = g_execution_state;
    if (flags & ES_CONTINUOUS)
        g_execution_state = flags;
    return old;
}

HANDLE WINAPI PowerCreateRequest(PREASON_CONTEXT context)
{
    HANDLE h;
    NTSTATUS status = NtCreateEvent(&h, EVENT_ALL_ACCESS, nullptr, NotificationEvent, FALSE);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

BOOL WINAPI PowerSetRequest(HANDLE request, int type)
{
    return TRUE;
}

BOOL WINAPI PowerClearRequest(HANDLE request, int type)
{
    return TRUE;
}

BOOL WINAPI IsNativeVhdBoot(PBOOL native)
{
    if (native)
        *native = FALSE;
    return TRUE;
}

BOOL WINAPI GetFirmwareType(PDWORD type)
{
    if (!type) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *type = 2; /* FirmwareTypeUefi. */
    return TRUE;
}

BOOL WINAPI Beep(DWORD frequency, DWORD duration)
{
    return TRUE;
}

/* ------------------------------------------------------------------ safe mode and computer names */

/* 0 for a normal start, 1 for safe mode, 2 for safe mode with a network: the option the loader recorded. */
BOOL WINAPI GetOsSafeBootMode(PDWORD flags)
{
    if (!flags) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UNICODE_STRING path, value;
    RtlInitUnicodeString(&path, L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\SafeBoot\\Option");
    RtlInitUnicodeString(&value, L"OptionValue");
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    *flags = 0;
    HANDLE h;
    NTSTATUS status = NtOpenKey(&h, KEY_QUERY_VALUE, &oa);
    if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        return TRUE;
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    ULONG buffer[8], length;
    status = NtQueryValueKey(h, &value, 2 /* KeyValuePartialInformation */, buffer, sizeof(buffer), &length);
    NtClose(h);
    if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        return TRUE;
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    if (buffer[1] == REG_DWORD && buffer[2] == sizeof(DWORD))
        *flags = buffer[3];
    return TRUE;
}

/* the name of the machine on the old networks from a dns host name: its first 15 characters in upper case. */
BOOL WINAPI DnsHostnameToComputerNameExW(LPCWSTR host, LPWSTR name, LPDWORD size)
{
    if (!host || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR first[16], upper[16];
    DWORD length = 0;
    while (length < 15 && host[length]) {
        first[length] = host[length];
        length++;
    }
    first[length] = 0;
    if (length && LCMapStringEx(nullptr, LCMAP_UPPERCASE, first, (int)length + 1, upper, 16, nullptr, nullptr, 0))
        memcpy(first, upper, (length + 1) * sizeof(WCHAR));
    DWORD capacity = *size;
    *size = length;
    if (capacity < length) {
        KbSetError(ERROR_MORE_DATA);
        return FALSE;
    }
    if (capacity && name) {
        DWORD n = capacity <= length ? capacity - 1 : length;
        memcpy(name, first, n * sizeof(WCHAR));
        name[n] = 0;
    }
    return TRUE;
}

/* changing the name needs the rights of an administrator, which a program here never has. */
BOOL WINAPI SetComputerNameA(LPCSTR name)
{
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}

BOOL WINAPI SetComputerNameExA(COMPUTER_NAME_FORMAT format, LPCSTR name)
{
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}

BOOL WINAPI SetComputerNameExW(COMPUTER_NAME_FORMAT format, LPCWSTR name)
{
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}

BOOL WINAPI SetComputerNameEx2W(COMPUTER_NAME_FORMAT format, DWORD flags, LPCWSTR name)
{
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}
