/*
 * miscellaneous runtime services: error translation, debug output,
 * versions, time, pointer encoding and memory helpers.
 */
#include "ntdll_private.h"
#include <mime/ntrtlsec.h>

/* ------------------------------------------------------------------ status to win32 error */

typedef struct STATUS_MAP {
    NTSTATUS Status;
    ULONG Error;
} STATUS_MAP;

/* sorted by status for binary search. */
static const STATUS_MAP g_status_map[] = {
#include "statuses.inc"
};

/*
 * like windows: STATUS_PENDING is ERROR_IO_PENDING, a status with the customer bit is its own error, facility 7
 * carries a win32 error in its low word, and a status made from an hresult is looked up with those bits cleared.
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
    /* unlisted facility 1 statuses carry a win32 error in the low word. */
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

NTDLL_EXPORT ULONG NTAPI vDbgPrintExWithPrefix(PCSTR prefix, ULONG component, ULONG level, PCSTR format,
                                             va_list ap)
{
    char buf[512];
    SIZE_T prefix_length = strnlen(prefix, sizeof(buf) - 1);
    memcpy(buf, prefix, prefix_length);
    SIZE_T available = sizeof(buf) - prefix_length;
    int n = mime_vformat(buf + prefix_length, available, format, ap);
    SIZE_T length = prefix_length;
    if (n < 0 || (SIZE_T)n >= available) {
        length = sizeof(buf) - 1;
        buf[length - 1] = '\n';
        buf[length] = 0;
    } else {
        length += (SIZE_T)n;
    }
    output_utf8(buf, length);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT ULONG NTAPI vDbgPrintEx(ULONG component, ULONG level, PCSTR format, va_list ap)
{
    return vDbgPrintExWithPrefix("", component, level, format, ap);
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
 * highest bit. bit 63 marks a mask built this way, which
 * RtlVerifyVersionInfo reads per type.
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

/* the VER_ condition values and the type bits that RtlVerifyVersionInfo takes. */
enum {
    VERIFY_MINOR = 0x01,
    VERIFY_MAJOR = 0x02,
    VERIFY_BUILD = 0x04,
    VERIFY_PLATFORM = 0x08,
    VERIFY_SP_MINOR = 0x10,
    VERIFY_SP_MAJOR = 0x20,
    VERIFY_SUITE = 0x40,
    VERIFY_PRODUCT = 0x80,
};
enum { COND_EQUAL = 1, COND_GREATER, COND_GREATER_EQUAL, COND_LESS, COND_LESS_EQUAL };
enum { COND_AND = 6, COND_OR = 7 };

/* the three bits of a type in a mask made by VerSetConditionMask, which sits at the type's highest bit. */
static ULONG condition_of(ULONGLONG mask, ULONG type)
{
    ULONG index = 0;
    while (type >>= 1)
        index++;
    return (ULONG)(mask >> (3 * index)) & 7;
}

static BOOLEAN compare_version_value(ULONG current, ULONG wanted, ULONG condition)
{
    switch (condition) {
    case COND_EQUAL:
        return current == wanted;
    case COND_GREATER:
        return current > wanted;
    case COND_GREATER_EQUAL:
        return current >= wanted;
    case COND_LESS:
        return current < wanted;
    case COND_LESS_EQUAL:
        return current <= wanted;
    }
    return FALSE;
}

/*
 * true when the system is what the structure and the conditions of VerSetConditionMask ask for: STATUS_SUCCESS, or
 * STATUS_REVISION_MISMATCH when it is not, and STATUS_INVALID_PARAMETER when no type is asked for (read from build
 * 26300). Major and minor version and the service pack versions count as one number, the first of them that
 * differs decides, and the others are only compared when all the asked ones are equal. WPF's native library calls
 * this to tell which Windows it runs on, by name from ntdll.dll.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlVerifyVersionInfo(RTL_OSVERSIONINFOEXW *info, ULONG type, ULONGLONG condition)
{
    if (!type)
        return STATUS_INVALID_PARAMETER;
    RTL_OSVERSIONINFOEXW now = {};
    now.dwOSVersionInfoSize = sizeof(now);
    NTSTATUS status = RtlGetVersion(&now);
    if (!NT_SUCCESS(status))
        return status;
    if ((type & VERIFY_SUITE) && info->wSuiteMask) {
        ULONG c = condition_of(condition, VERIFY_SUITE);
        if (c != COND_AND && c != COND_OR)
            return STATUS_INVALID_PARAMETER;
        USHORT shared = now.wSuiteMask & info->wSuiteMask;
        if (c == COND_AND ? shared != info->wSuiteMask : !shared)
            return STATUS_REVISION_MISMATCH;
    }
    static const ULONG order[] = {VERIFY_MAJOR, VERIFY_MINOR, VERIFY_SP_MAJOR, VERIFY_SP_MINOR};
    ULONG current[4] = {now.dwMajorVersion, now.dwMinorVersion, now.wServicePackMajor, now.wServicePackMinor};
    ULONG wanted[4] = {info->dwMajorVersion, info->dwMinorVersion, info->wServicePackMajor, info->wServicePackMinor};
    ULONG last = 0;
    BOOLEAN decided = FALSE;
    for (int i = 0; i < 4 && !decided; i++) {
        if (!(type & order[i]))
            continue;
        last = condition_of(condition, order[i]);
        if (current[i] != wanted[i]) {
            if (!compare_version_value(current[i], wanted[i], last))
                return STATUS_REVISION_MISMATCH;
            decided = TRUE;
        }
    }
    if (!decided && last && last != COND_EQUAL && last != COND_GREATER_EQUAL && last != COND_LESS_EQUAL)
        return STATUS_REVISION_MISMATCH;
    if ((type & VERIFY_BUILD) &&
        !compare_version_value(now.dwBuildNumber, info->dwBuildNumber, condition_of(condition, VERIFY_BUILD)))
        return STATUS_REVISION_MISMATCH;
    if ((type & VERIFY_PLATFORM) &&
        !compare_version_value(now.dwPlatformId, info->dwPlatformId, condition_of(condition, VERIFY_PLATFORM)))
        return STATUS_REVISION_MISMATCH;
    if ((type & VERIFY_PRODUCT) &&
        !compare_version_value(now.wProductType, info->wProductType, condition_of(condition, VERIFY_PRODUCT)))
        return STATUS_REVISION_MISMATCH;
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

/* FALSE when the value is missing. */
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

/* version packs major, minor, build and UBR in 16 bits each. form comes from the OEM key, 0 when unset. */
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

/* the uac policy read by the kernel at boot: elevation, virtualization and installer detection, one bit each. */
NTDLL_EXPORT NTSTATUS NTAPI RtlQueryElevationFlags(PULONG flags)
{
    if (!flags)
        return STATUS_ACCESS_VIOLATION;
    *flags = (USER_SHARED_DATA->SharedDataFlags >> 1) & 7;
    return STATUS_SUCCESS;
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

/* user mode may read the processor counter directly, it is the performance counter on every processor. */
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

/*
 * the weekday counts from Sunday, 1 January 1601 was a Monday. leap seconds that Windows can keep in the process
 * block are ignored.
 */
NTDLL_EXPORT void NTAPI RtlTimeToTimeFields(PLARGE_INTEGER time, PVOID fields)
{
    PTIME_FIELDS tf = fields;
    ULONGLONG ticks = (ULONGLONG)time->QuadPart;
    ULONGLONG milliseconds = ticks / 10000;
    ULONGLONG days = milliseconds / 86400000;
    ULONG rest = (ULONG)(milliseconds % 86400000);
    tf->Weekday = (SHORT)((days + 1) % 7);
    LONG year = 1601;
    /* 146097 days are 400 gregorian years, 36524 a century and 1461 four years. */
    year += 400 * (LONG)(days / 146097);
    days %= 146097;
    LONG centuries = (LONG)(days / 36524);
    if (centuries == 4)
        centuries = 3;
    year += 100 * centuries;
    days -= (ULONGLONG)centuries * 36524;
    year += 4 * (LONG)(days / 1461);
    days %= 1461;
    LONG single = (LONG)(days / 365);
    if (single == 4)
        single = 3;
    year += single;
    days -= (ULONGLONG)single * 365;
    LONG month = 0;
    while (days >= g_month_days[is_leap(year)][month])
        days -= g_month_days[is_leap(year)][month++];
    tf->Year = (SHORT)year;
    tf->Month = (SHORT)(month + 1);
    tf->Day = (SHORT)(days + 1);
    tf->Hour = (SHORT)(rest / 3600000);
    tf->Minute = (SHORT)(rest / 60000 % 60);
    tf->Second = (SHORT)(rest / 1000 % 60);
    tf->Milliseconds = (SHORT)(rest % 1000);
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
    /* the park miller generator documented for RtlRandom. */
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

#if defined(__x86_64__)
NTDLL_EXPORT PVOID NTAPI RtlVirtualUnwind(ULONG type, ULONG64 base, ULONG64 pc, PVOID function,
                                                        PCONTEXT context, PVOID *data, PULONG64 frame, PVOID pointers);

/* the callers of the caller, found with the unwind data of the images, so code built without frame pointers shows up. */
NTDLL_EXPORT USHORT NTAPI RtlCaptureStackBackTrace(ULONG skip, ULONG count, PVOID *frames, PULONG hash)
{
    CONTEXT context;
    RtlCaptureContext(&context);
    USHORT n = 0;
    ULONG h = 0;
    PTEB teb = NtCurrentTeb();
    for (ULONG depth = 0; depth < 256 && n < count; depth++) {
        ULONG64 base;
        PVOID function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
        if (function) {
            PVOID handler_data;
            ULONG64 frame;
            RtlVirtualUnwind(0, base, context.Rip, function, &context, &handler_data, &frame, nullptr);
        } else {
            /* a leaf without data returns to the address on top of the stack. */
            if (context.Rsp < (ULONG64)teb->NtTib.StackLimit || context.Rsp >= (ULONG64)teb->NtTib.StackBase)
                break;
            context.Rip = *(ULONG64 *)context.Rsp;
            context.Rsp += sizeof(ULONG64);
        }
        if (!context.Rip)
            break;
        if (skip) {
            skip--;
            continue;
        }
        frames[n++] = (PVOID)context.Rip;
        h += (ULONG)context.Rip;
    }
    if (hash)
        *hash = h;
    return n;
}
#else
NTDLL_EXPORT USHORT NTAPI RtlCaptureStackBackTrace(ULONG skip, ULONG count, PVOID *frames, PULONG hash)
{
    /* walk the frame record chain, {previous frame pointer, return address}. */
    ULONG_PTR *fp = __builtin_frame_address(0);
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
        fp = (ULONG_PTR *)fp[0];
    }
    if (hash)
        *hash = h;
    return n;
}
#endif

/* where the caller of the caller returns to, and where its own caller does. */
NTDLL_EXPORT void NTAPI RtlGetCallersAddress(PVOID *caller, PVOID *callers_caller)
{
    PVOID frames[8], own = __builtin_return_address(0);
    USHORT n = RtlCaptureStackBackTrace(0, RTL_NUMBER_OF(frames), frames, nullptr);
    USHORT i = 0;
    while (i < n && frames[i] != own)
        i++;
    *caller = i + 1 < n ? frames[i + 1] : nullptr;
    *callers_caller = i + 2 < n ? frames[i + 2] : nullptr;
}

/* the telemetry session of windows is not kept. */
NTDLL_EXPORT void NTAPI WinSqmSetDWORD(const GUID *session, ULONG id, ULONG value)
{
}
