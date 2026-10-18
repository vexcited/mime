/*
 * miscellaneous runtime services: error translation, debug output,
 * versions, time, pointers encoding and memory helpers.
 */
#include "ntdll_private.h"

/* ------------------------------------------------------------------ status to win32 error */

typedef struct STATUS_MAP {
    NTSTATUS Status;
    ULONG Error;
} STATUS_MAP;

/* sorted by status for the binary search. */
static const STATUS_MAP g_status_map[] = {
#include "statuses.inc"
};

/*
 * windows' rules: success is 0 and a pending status is ERROR_IO_PENDING, a status with the customer bit is
 * its own error, a win32 error in a status of facility 7 is the error itself, the rest is looked up, in
 * the table with the mapped bits of an ntstatus that was made from an hresult cleared.
 */
NTDLL_EXPORT ULONG NTAPI RtlNtStatusToDosErrorNoTeb(NTSTATUS status)
{
    ULONG value = (ULONG)status;
    if (!value)
        return 0;
    if (value == 259)
        return 997;
    if (value & 0x20000000)
        return value;
    if ((value & 0xFF0000) == 0x70000 && ((value >> 24) == 0xC0 || (value >> 24) == 0x80))
        return value & 0xFFFF;
    if ((value & 0xF0000000) == 0xD0000000)
        value &= 0xCFFFFFFF;
    ULONG lo = 0, hi = RTL_NUMBER_OF(g_status_map);
    while (lo < hi) {
        ULONG mid = (lo + hi) / 2;
        if ((ULONG)g_status_map[mid].Status == value)
            return g_status_map[mid].Error;
        if ((ULONG)g_status_map[mid].Status < value)
            lo = mid + 1;
        else
            hi = mid;
    }
    /* what has no entry but a win32 error of facility 1 in the low word. */
    if ((value & 0xFFFF0000) == 0xC0010000)
        return value & 0xFFFF;
    return 317; /* ERROR_MR_MID_NOT_FOUND */
}

NTDLL_EXPORT ULONG NTAPI RtlNtStatusToDosError(NTSTATUS status)
{
    NtCurrentTeb()->LastStatusValue = status;
    return RtlNtStatusToDosErrorNoTeb(status);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlGetLastNtStatus(void)
{
    return NtCurrentTeb()->LastStatusValue;
}

NTDLL_EXPORT ULONG NTAPI RtlGetLastWin32Error(void)
{
    return NtCurrentTeb()->LastErrorValue;
}

NTDLL_EXPORT void NTAPI RtlSetLastWin32Error(ULONG error)
{
    NtCurrentTeb()->LastErrorValue = error;
}

NTDLL_EXPORT void NTAPI RtlRestoreLastWin32Error(ULONG error)
{
    NtCurrentTeb()->LastErrorValue = error;
}

NTDLL_EXPORT void NTAPI RtlSetLastWin32ErrorAndNtStatusFromNtStatus(NTSTATUS status)
{
    NtCurrentTeb()->LastErrorValue = RtlNtStatusToDosError(status);
}

/* ------------------------------------------------------------------ debug output */

static void output_utf8(const char *text, SIZE_T length)
{
    WCHAR wide[512];
    while (length) {
        ULONG chunk = length > 400 ? 400 : (ULONG)length;
        ULONG bytes = 0;
        RtlUTF8ToUnicodeN(wide, sizeof(wide), &bytes, text, chunk);
        UNICODE_STRING s = {(USHORT)bytes, (USHORT)bytes, wide};
        NtDisplayString(&s);
        text += chunk;
        length -= chunk;
    }
}

NTDLL_EXPORT ULONG vDbgPrintEx(ULONG component, ULONG level, PCSTR format, va_list ap)
{
    char buf[1024];
    int n = _vsnprintf(buf, sizeof(buf), format, ap);
    if (n < 0 || n >= (int)sizeof(buf))
        n = (int)strlen(buf);
    output_utf8(buf, (SIZE_T)n);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT ULONG DbgPrintEx(ULONG component, ULONG level, PCSTR format, ...)
{
    va_list ap;
    va_start(ap, format);
    ULONG r = vDbgPrintEx(component, level, format, ap);
    va_end(ap);
    return r;
}

NTDLL_EXPORT ULONG DbgPrint(PCSTR format, ...)
{
    va_list ap;
    va_start(ap, format);
    ULONG r = vDbgPrintEx((ULONG)-1, 0, format, ap);
    va_end(ap);
    return r;
}

#if defined(__x86_64__) || defined(__i386__)
#define BREAKPOINT() __asm__ volatile("int3")
#else
#define BREAKPOINT() __asm__ volatile("brk #0xF000")
#endif

NTDLL_EXPORT void NTAPI DbgBreakPoint(void)
{
    BREAKPOINT();
}

NTDLL_EXPORT void NTAPI DbgUserBreakPoint(void)
{
    BREAKPOINT();
}

/* ------------------------------------------------------------------ versions */

/*
 * three condition bits per VER_ type, at the position of the type's
 * highest bit, ORed into the mask. bit 63 marks a mask built this way,
 * which RtlVerifyVersionInfo reads per type. no type gives 0.
 */
NTDLL_EXPORT ULONGLONG NTAPI VerSetConditionMask(ULONGLONG mask, ULONG type, UCHAR condition)
{
    if (!type)
        return 0;
    ULONG index = 0;
    while (type >>= 1)
        index++;
    return mask | ((ULONGLONG)(condition & 7) << (3 * index)) | (1ull << 63);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlGetVersion(RTL_OSVERSIONINFOEXW *info)
{
    PPEB peb = NtCurrentPeb();
    info->dwMajorVersion = peb->OSMajorVersion;
    info->dwMinorVersion = peb->OSMinorVersion;
    info->dwBuildNumber = peb->OSBuildNumber;
    info->dwPlatformId = peb->OSPlatformId;
    info->szCSDVersion[0] = 0;
    if (info->dwOSVersionInfoSize >= sizeof(RTL_OSVERSIONINFOEXW)) {
        info->wServicePackMajor = 0;
        info->wServicePackMinor = 0;
        info->wSuiteMask = 0x100; /* VER_SUITE_SINGLEUSERTS */
        info->wProductType = 1;  /* VER_NT_WORKSTATION */
        info->wReserved = 0;
    }
    return STATUS_SUCCESS;
}

NTDLL_EXPORT void NTAPI RtlGetNtVersionNumbers(PULONG major, PULONG minor, PULONG build)
{
    PPEB peb = NtCurrentPeb();
    if (major)
        *major = peb->OSMajorVersion;
    if (minor)
        *minor = peb->OSMinorVersion;
    if (build)
        *build = 0xF0000000u | peb->OSBuildNumber;
}

/* a REG_DWORD of a key of the machine, false when the value is not there. */
static BOOLEAN read_machine_ulong(PCWSTR path, PCWSTR value_name, ULONG *value)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE key;
    if (!NT_SUCCESS(NtOpenKey(&key, 1 /* KEY_QUERY_VALUE */, &oa)))
        return FALSE;
    UNICODE_STRING value_unicode;
    RtlInitUnicodeString(&value_unicode, value_name);
    struct {
        ULONG TitleIndex, Type, DataLength;
        ULONG Data[2];
    } info;
    ULONG returned;
    BOOLEAN found = NT_SUCCESS(NtQueryValueKey(key, &value_unicode, 2 /* KeyValuePartialInformation */, &info,
                                               sizeof(info), &returned)) &&
                    info.Type == 4 /* REG_DWORD */ && info.DataLength == 4;
    if (found)
        *value = info.Data[0];
    NtClose(key);
    return found;
}

/*
 * the build as one 64 bit number, major, minor, build and the update revision in 16 bits each, the
 * device family, 3 for a desktop, and the form the oem key names, 0 when unspecified.
 */
NTDLL_EXPORT void NTAPI RtlGetDeviceFamilyInfoEnum(ULONGLONG *version, ULONG *family, ULONG *form)
{
    static const WCHAR current_version[] = L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion";
    if (version) {
        ULONG revision = 0;
        read_machine_ulong(current_version, L"UBR", &revision);
        PPEB peb = NtCurrentPeb();
        *version = ((ULONGLONG)peb->OSMajorVersion << 48) | ((ULONGLONG)peb->OSMinorVersion << 32) |
                   ((ULONGLONG)(peb->OSBuildNumber & 0xFFFF) << 16) | (revision & 0xFFFF);
    }
    if (family)
        *family = 3; /* DEVICEFAMILYINFOENUM_DESKTOP */
    if (form) {
        *form = 0;
        read_machine_ulong(L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion\\OEM", L"DeviceForm", form);
    }
}

NTDLL_EXPORT BOOLEAN NTAPI RtlGetNtProductType(ULONG *type)
{
    *type = 1; /* NtProductWinNt */
    return TRUE;
}

NTDLL_EXPORT PCWSTR NTAPI RtlGetNtSystemRoot(void)
{
    return (PCWSTR)USER_SHARED_DATA->NtSystemRoot;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlIsProcessorFeaturePresent(ULONG feature)
{
    return feature < 64 && USER_SHARED_DATA->ProcessorFeatures[feature];
}

/* ------------------------------------------------------------------ time */

/* the kernel lets user mode read the processor's counter, the performance counter of every processor. */
NTDLL_EXPORT BOOLEAN NTAPI RtlQueryPerformanceCounter(PLARGE_INTEGER counter)
{
    counter->QuadPart = (LONGLONG)MimeReadQpc();
    return TRUE;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlQueryPerformanceFrequency(PLARGE_INTEGER frequency)
{
    frequency->QuadPart = USER_SHARED_DATA->QpcFrequency;
    return TRUE;
}

NTDLL_EXPORT LONGLONG NTAPI RtlGetSystemTimePrecise(void)
{
    return (LONGLONG)MimePreciseTime(TRUE);
}

#define TICKS_PER_SECOND 10000000ll
#define SECONDS_1601_TO_1970 11644473600ll
#define SECONDS_1601_TO_1980 11960006400ll

NTDLL_EXPORT BOOLEAN NTAPI RtlTimeToSecondsSince1970(const LARGE_INTEGER *time, PULONG seconds)
{
    LONGLONG s = time->QuadPart / TICKS_PER_SECOND - SECONDS_1601_TO_1970;
    if (s < 0 || s > 0xFFFFFFFFll)
        return FALSE;
    *seconds = (ULONG)s;
    return TRUE;
}

NTDLL_EXPORT void NTAPI RtlSecondsSince1970ToTime(ULONG seconds, PLARGE_INTEGER time)
{
    time->QuadPart = ((LONGLONG)seconds + SECONDS_1601_TO_1970) * TICKS_PER_SECOND;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlTimeToSecondsSince1980(const LARGE_INTEGER *time, PULONG seconds)
{
    LONGLONG s = time->QuadPart / TICKS_PER_SECOND - SECONDS_1601_TO_1980;
    if (s < 0 || s > 0xFFFFFFFFll)
        return FALSE;
    *seconds = (ULONG)s;
    return TRUE;
}

NTDLL_EXPORT void NTAPI RtlSecondsSince1980ToTime(ULONG seconds, PLARGE_INTEGER time)
{
    time->QuadPart = ((LONGLONG)seconds + SECONDS_1601_TO_1980) * TICKS_PER_SECOND;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlSystemTimeToLocalTime(const LARGE_INTEGER *system, PLARGE_INTEGER local)
{
    volatile KUSER_SHARED_DATA *k = USER_SHARED_DATA;
    LONGLONG bias = ((LONGLONG)k->TimeZoneBias.High1Time << 32) | k->TimeZoneBias.LowPart;
    local->QuadPart = system->QuadPart - bias;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlLocalTimeToSystemTime(const LARGE_INTEGER *local, PLARGE_INTEGER system)
{
    volatile KUSER_SHARED_DATA *k = USER_SHARED_DATA;
    LONGLONG bias = ((LONGLONG)k->TimeZoneBias.High1Time << 32) | k->TimeZoneBias.LowPart;
    system->QuadPart = local->QuadPart + bias;
    return STATUS_SUCCESS;
}

static const UCHAR g_month_days[2][12] = {{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
                                          {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};

static BOOLEAN is_leap(LONG y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlTimeFieldsToTime(PTIME_FIELDS tf, PLARGE_INTEGER time)
{
    if (tf->Month < 1 || tf->Month > 12 || tf->Day < 1 || tf->Year < 1601 || tf->Hour > 23 || tf->Minute > 59 ||
        tf->Second > 59 || tf->Milliseconds > 999)
        return FALSE;
    if (tf->Day > g_month_days[is_leap(tf->Year)][tf->Month - 1])
        return FALSE;
    LONGLONG days = 0;
    for (LONG y = 1601; y < tf->Year; y++)
        days += is_leap(y) ? 366 : 365;
    for (LONG m = 1; m < tf->Month; m++)
        days += g_month_days[is_leap(tf->Year)][m - 1];
    days += tf->Day - 1;
    LONGLONG secs = days * 86400 + tf->Hour * 3600 + tf->Minute * 60 + tf->Second;
    time->QuadPart = secs * TICKS_PER_SECOND + (LONGLONG)tf->Milliseconds * 10000;
    return TRUE;
}

/* ------------------------------------------------------------------ pointer encoding */

static ULONG process_cookie(void)
{
    static ULONG cookie;
    if (!cookie) {
        ULONG c = 0;
        NtQueryInformationProcess(NtCurrentProcess(), (PROCESSINFOCLASS)36 /* ProcessCookie */, &c, sizeof(c), nullptr);
        cookie = c | 1;
    }
    return cookie;
}

static PVOID encode(PVOID p, ULONG cookie)
{
    ULONG_PTR v = (ULONG_PTR)p ^ cookie;
    ULONG bits = sizeof(ULONG_PTR) * 8;
    ULONG shift = cookie & (bits - 1);
    return (PVOID)((v >> shift) | (v << ((bits - shift) & (bits - 1))));
}

static PVOID decode(PVOID p, ULONG cookie)
{
    ULONG_PTR v = (ULONG_PTR)p;
    ULONG bits = sizeof(ULONG_PTR) * 8;
    ULONG shift = cookie & (bits - 1);
    v = (v << shift) | (v >> ((bits - shift) & (bits - 1)));
    return (PVOID)(v ^ cookie);
}

NTDLL_EXPORT PVOID NTAPI RtlEncodePointer(PVOID p)
{
    return encode(p, process_cookie());
}

NTDLL_EXPORT PVOID NTAPI RtlDecodePointer(PVOID p)
{
    return decode(p, process_cookie());
}

NTDLL_EXPORT PVOID NTAPI RtlEncodeSystemPointer(PVOID p)
{
    return encode(p, USER_SHARED_DATA->Cookie | 1);
}

NTDLL_EXPORT PVOID NTAPI RtlDecodeSystemPointer(PVOID p)
{
    return decode(p, USER_SHARED_DATA->Cookie | 1);
}

/* ------------------------------------------------------------------ memory helpers */

NTDLL_EXPORT SIZE_T NTAPI RtlCompareMemory(const void *a, const void *b, SIZE_T length)
{
    const UCHAR *x = a, *y = b;
    SIZE_T i = 0;
    while (i < length && x[i] == y[i])
        i++;
    return i;
}

NTDLL_EXPORT SIZE_T NTAPI RtlCompareMemoryUlong(const void *source, SIZE_T length, ULONG pattern)
{
    const ULONG *p = source;
    SIZE_T i = 0;
    while (i < length / sizeof(ULONG) && p[i] == pattern)
        i++;
    return i * sizeof(ULONG);
}

#undef RtlFillMemory
#undef RtlZeroMemory
#undef RtlMoveMemory
#undef RtlCopyMemory

NTDLL_EXPORT void NTAPI RtlFillMemory(void *dst, SIZE_T length, UCHAR fill)
{
    memset(dst, fill, length);
}

NTDLL_EXPORT void NTAPI RtlZeroMemory(void *dst, SIZE_T length)
{
    memset(dst, 0, length);
}

NTDLL_EXPORT void NTAPI RtlMoveMemory(void *dst, const void *src, SIZE_T length)
{
    memmove(dst, src, length);
}

NTDLL_EXPORT void NTAPI RtlCopyMemory(void *dst, const void *src, SIZE_T length)
{
    memmove(dst, src, length);
}

NTDLL_EXPORT ULONG NTAPI RtlComputeCrc32(ULONG initial, const UCHAR *data, ULONG length)
{
    ULONG crc = ~initial;
    while (length--) {
        crc ^= *data++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

NTDLL_EXPORT ULONG NTAPI RtlRandomEx(PULONG seed)
{
    /* the park miller generator ntdll documents for RtlRandom. */
    ULONG64 next = ((ULONG64)*seed * 16807u) % 0x7FFFFFFFu;
    *seed = (ULONG)next;
    return (ULONG)next;
}

NTDLL_EXPORT ULONG NTAPI RtlRandom(PULONG seed)
{
    return RtlRandomEx(seed);
}

NTDLL_EXPORT ULONG NTAPI RtlUniform(PULONG seed)
{
    *seed = *seed * 0x7FFFFFED + 0x7FFFFFC3;
    return *seed & 0x7FFFFFFF;
}

NTDLL_EXPORT USHORT NTAPI RtlCaptureStackBackTrace(ULONG skip, ULONG count, PVOID *frames, PULONG hash)
{
    /* walk the frame record chain; x29 points at {previous x29, x30}. */
    ULONG64 *fp = __builtin_frame_address(0);
    PTEB teb = NtCurrentTeb();
    USHORT n = 0;
    ULONG h = 0;
    while (fp && (PVOID)fp >= teb->NtTib.StackLimit && (PVOID)fp < teb->NtTib.StackBase && n < count) {
        PVOID ret = (PVOID)fp[1];
        if (skip)
            skip--;
        else {
            frames[n++] = ret;
            h += (ULONG)(ULONG_PTR)ret;
        }
        fp = (ULONG64 *)fp[0];
    }
    if (hash)
        *hash = h;
    return n;
}
