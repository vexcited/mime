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
    {(NTSTATUS)0x00000000, 0},     /* STATUS_SUCCESS */
    {(NTSTATUS)0x00000102, 258},   /* STATUS_TIMEOUT -> WAIT_TIMEOUT */
    {(NTSTATUS)0x00000103, 997},   /* STATUS_PENDING -> ERROR_IO_PENDING */
    {(NTSTATUS)0x00000104, 0},     /* STATUS_REPARSE */
    {(NTSTATUS)0x00000105, 234},   /* STATUS_MORE_ENTRIES -> ERROR_MORE_DATA */
    {(NTSTATUS)0x0000010C, 1022},  /* STATUS_NOTIFY_ENUM_DIR */
    {(NTSTATUS)0x40000000, 80},    /* STATUS_OBJECT_NAME_EXISTS -> ERROR_FILE_EXISTS */
    {(NTSTATUS)0x40000003, 0},     /* STATUS_IMAGE_NOT_AT_BASE */
    {(NTSTATUS)0x80000002, 998},   /* STATUS_DATATYPE_MISALIGNMENT -> ERROR_NOACCESS */
    {(NTSTATUS)0x80000005, 234},   /* STATUS_BUFFER_OVERFLOW -> ERROR_MORE_DATA */
    {(NTSTATUS)0x80000006, 18},    /* STATUS_NO_MORE_FILES */
    {(NTSTATUS)0x8000001A, 259},   /* STATUS_NO_MORE_ENTRIES -> ERROR_NO_MORE_ITEMS */
    {(NTSTATUS)0xC0000001, 31},    /* STATUS_UNSUCCESSFUL -> ERROR_GEN_FAILURE */
    {(NTSTATUS)0xC0000002, 1},     /* STATUS_NOT_IMPLEMENTED -> ERROR_INVALID_FUNCTION */
    {(NTSTATUS)0xC0000003, 87},    /* STATUS_INVALID_INFO_CLASS */
    {(NTSTATUS)0xC0000004, 24},    /* STATUS_INFO_LENGTH_MISMATCH -> ERROR_BAD_LENGTH */
    {(NTSTATUS)0xC0000005, 998},   /* STATUS_ACCESS_VIOLATION -> ERROR_NOACCESS */
    {(NTSTATUS)0xC0000008, 6},     /* STATUS_INVALID_HANDLE */
    {(NTSTATUS)0xC000000D, 87},    /* STATUS_INVALID_PARAMETER */
    {(NTSTATUS)0xC000000E, 2},     /* STATUS_NO_SUCH_DEVICE -> ERROR_FILE_NOT_FOUND */
    {(NTSTATUS)0xC000000F, 2},     /* STATUS_NO_SUCH_FILE */
    {(NTSTATUS)0xC0000010, 1},     /* STATUS_INVALID_DEVICE_REQUEST */
    {(NTSTATUS)0xC0000011, 38},    /* STATUS_END_OF_FILE -> ERROR_HANDLE_EOF */
    {(NTSTATUS)0xC0000017, 8},     /* STATUS_NO_MEMORY -> ERROR_NOT_ENOUGH_MEMORY */
    {(NTSTATUS)0xC0000018, 487},   /* conflicting addresses map to invalid address. */
    {(NTSTATUS)0xC000001C, 1},     /* STATUS_INVALID_SYSTEM_SERVICE */
    {(NTSTATUS)0xC0000022, 5},     /* STATUS_ACCESS_DENIED */
    {(NTSTATUS)0xC0000023, 122},   /* STATUS_BUFFER_TOO_SMALL -> ERROR_INSUFFICIENT_BUFFER */
    {(NTSTATUS)0xC0000024, 6},     /* STATUS_OBJECT_TYPE_MISMATCH -> ERROR_INVALID_HANDLE */
    {(NTSTATUS)0xC0000025, 0x25},  /* STATUS_NONCONTINUABLE_EXCEPTION */
    {(NTSTATUS)0xC0000030, 87},    /* STATUS_INVALID_PARAMETER_MIX */
    {(NTSTATUS)0xC0000033, 123},   /* STATUS_OBJECT_NAME_INVALID -> ERROR_INVALID_NAME */
    {(NTSTATUS)0xC0000034, 2},     /* STATUS_OBJECT_NAME_NOT_FOUND -> ERROR_FILE_NOT_FOUND */
    {(NTSTATUS)0xC0000035, 183},   /* STATUS_OBJECT_NAME_COLLISION -> ERROR_ALREADY_EXISTS */
    {(NTSTATUS)0xC0000039, 161},   /* STATUS_OBJECT_PATH_INVALID -> ERROR_BAD_PATHNAME */
    {(NTSTATUS)0xC000003A, 3},     /* STATUS_OBJECT_PATH_NOT_FOUND -> ERROR_PATH_NOT_FOUND */
    {(NTSTATUS)0xC000003B, 161},   /* STATUS_OBJECT_PATH_SYNTAX_BAD */
    {(NTSTATUS)0xC0000043, 32},    /* STATUS_SHARING_VIOLATION */
    {(NTSTATUS)0xC0000046, 288},   /* STATUS_MUTANT_NOT_OWNED -> ERROR_NOT_OWNER */
    {(NTSTATUS)0xC0000047, 298},   /* STATUS_SEMAPHORE_LIMIT_EXCEEDED -> ERROR_TOO_MANY_POSTS */
    {(NTSTATUS)0xC0000054, 33},    /* STATUS_FILE_LOCK_CONFLICT -> ERROR_LOCK_VIOLATION */
    {(NTSTATUS)0xC0000056, 5},     /* STATUS_DELETE_PENDING -> ERROR_ACCESS_DENIED */
    {(NTSTATUS)0xC000007A, 127},   /* STATUS_PROCEDURE_NOT_FOUND */
    {(NTSTATUS)0xC000007C, 1008},  /* STATUS_NO_TOKEN */
    {(NTSTATUS)0xC000007F, 112},   /* STATUS_DISK_FULL */
    {(NTSTATUS)0xC000009A, 1450},  /* STATUS_INSUFFICIENT_RESOURCES -> ERROR_NO_SYSTEM_RESOURCES */
    {(NTSTATUS)0xC00000A3, 21},    /* STATUS_DEVICE_NOT_READY -> ERROR_NOT_READY */
    {(NTSTATUS)0xC00000AB, 231},   /* STATUS_INSTANCE_NOT_AVAILABLE -> ERROR_PIPE_BUSY */
    {(NTSTATUS)0xC00000AC, 231},   /* STATUS_PIPE_NOT_AVAILABLE -> ERROR_PIPE_BUSY */
    {(NTSTATUS)0xC00000AD, 230},   /* STATUS_INVALID_PIPE_STATE -> ERROR_BAD_PIPE */
    {(NTSTATUS)0xC00000AE, 231},   /* STATUS_PIPE_BUSY -> ERROR_PIPE_BUSY */
    {(NTSTATUS)0xC00000B0, 233},   /* STATUS_PIPE_DISCONNECTED -> ERROR_PIPE_NOT_CONNECTED */
    {(NTSTATUS)0xC00000B1, 232},   /* STATUS_PIPE_CLOSING -> ERROR_NO_DATA */
    {(NTSTATUS)0xC00000B2, 535},   /* STATUS_PIPE_CONNECTED -> ERROR_PIPE_CONNECTED */
    {(NTSTATUS)0xC00000B3, 536},   /* STATUS_PIPE_LISTENING -> ERROR_PIPE_LISTENING */
    {(NTSTATUS)0xC00000B5, 121},   /* STATUS_IO_TIMEOUT -> ERROR_SEM_TIMEOUT */
    {(NTSTATUS)0xC00000BA, 5},     /* STATUS_FILE_IS_A_DIRECTORY -> ERROR_ACCESS_DENIED */
    {(NTSTATUS)0xC00000BB, 50},    /* STATUS_NOT_SUPPORTED */
    {(NTSTATUS)0xC00000E5, 1359},  /* STATUS_INTERNAL_ERROR */
    {(NTSTATUS)0xC00000EF, 87},    /* STATUS_INVALID_PARAMETER_1 */
    {(NTSTATUS)0xC00000F0, 87},
    {(NTSTATUS)0xC00000F1, 87},
    {(NTSTATUS)0xC00000F2, 87},
    {(NTSTATUS)0xC0000101, 145},   /* STATUS_DIRECTORY_NOT_EMPTY -> ERROR_DIR_NOT_EMPTY */
    {(NTSTATUS)0xC0000103, 267},   /* STATUS_NOT_A_DIRECTORY -> ERROR_DIRECTORY */
    {(NTSTATUS)0xC0000106, 206},   /* STATUS_NAME_TOO_LONG -> ERROR_FILENAME_EXCED_RANGE */
    {(NTSTATUS)0xC0000120, 995},   /* STATUS_CANCELLED -> ERROR_OPERATION_ABORTED */
    {(NTSTATUS)0xC0000135, 126},   /* STATUS_DLL_NOT_FOUND -> ERROR_MOD_NOT_FOUND */
    {(NTSTATUS)0xC0000138, 182},   /* STATUS_ORDINAL_NOT_FOUND -> ERROR_INVALID_ORDINAL */
    {(NTSTATUS)0xC0000139, 127},   /* STATUS_ENTRYPOINT_NOT_FOUND -> ERROR_PROC_NOT_FOUND */
    {(NTSTATUS)0xC000013B, 64},    /* STATUS_LOCAL_DISCONNECT -> ERROR_NETNAME_DELETED */
    {(NTSTATUS)0xC000013C, 64},    /* STATUS_REMOTE_DISCONNECT -> ERROR_NETNAME_DELETED */
    {(NTSTATUS)0xC0000140, 1229},  /* STATUS_INVALID_CONNECTION -> ERROR_CONNECTION_INVALID */
    {(NTSTATUS)0xC0000142, 1114},  /* STATUS_DLL_INIT_FAILED */
    {(NTSTATUS)0xC000014B, 109},   /* STATUS_PIPE_BROKEN -> ERROR_BROKEN_PIPE */
    {(NTSTATUS)0xC0000184, 21},    /* STATUS_INVALID_DEVICE_STATE */
    {(NTSTATUS)0xC00001AD, 8},     /* STATUS_NO_MEMORY family */
    {(NTSTATUS)0xC0000207, 1214},  /* STATUS_INVALID_ADDRESS_COMPONENT -> ERROR_INVALID_NETNAME */
    {(NTSTATUS)0xC000020A, 52},    /* STATUS_ADDRESS_ALREADY_EXISTS -> ERROR_DUP_NAME */
    {(NTSTATUS)0xC000020D, 64},    /* STATUS_CONNECTION_RESET -> ERROR_NETNAME_DELETED */
    {(NTSTATUS)0xC0000225, 1168},  /* STATUS_NOT_FOUND -> ERROR_NOT_FOUND */
    {(NTSTATUS)0xC0000236, 1225},  /* STATUS_CONNECTION_REFUSED */
    {(NTSTATUS)0xC0000237, 1226},  /* STATUS_GRACEFUL_DISCONNECT */
    {(NTSTATUS)0xC000023B, 1230},  /* STATUS_CONNECTION_ACTIVE */
    {(NTSTATUS)0xC000023C, 1231},  /* STATUS_NETWORK_UNREACHABLE */
    {(NTSTATUS)0xC000023D, 1232},  /* STATUS_HOST_UNREACHABLE */
    {(NTSTATUS)0xC000023E, 1233},  /* STATUS_PROTOCOL_UNREACHABLE */
    {(NTSTATUS)0xC000023F, 1234},  /* STATUS_PORT_UNREACHABLE */
    {(NTSTATUS)0xC0000241, 1236},  /* STATUS_CONNECTION_ABORTED */
    {(NTSTATUS)0xC00002EB, 1115},  /* STATUS_SYSTEM_SHUTDOWN -> ERROR_SHUTDOWN_IN_PROGRESS */
    {(NTSTATUS)0xC0000350, 1256},  /* STATUS_HOST_DOWN */
};

NTDLL_EXPORT ULONG NTAPI RtlNtStatusToDosErrorNoTeb(NTSTATUS status)
{
    ULONG lo = 0, hi = RTL_NUMBER_OF(g_status_map);
    while (lo < hi) {
        ULONG mid = (lo + hi) / 2;
        if ((ULONG)g_status_map[mid].Status == (ULONG)status)
            return g_status_map[mid].Error;
        if ((ULONG)g_status_map[mid].Status < (ULONG)status)
            lo = mid + 1;
        else
            hi = mid;
    }
    /* facility win32 statuses carry the error code directly. */
    if (((ULONG)status & 0xFFFF0000) == 0x80070000 || ((ULONG)status & 0xFFFF0000) == 0xC0070000)
        return (ULONG)status & 0xFFFF;
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
        NtQueryInformationProcess(NtCurrentProcess(), (PROCESSINFOCLASS)36 /* ProcessCookie */, &c, sizeof(c), NULL);
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
