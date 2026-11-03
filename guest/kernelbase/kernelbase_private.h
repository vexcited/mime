/* kernelbase internals shared by its source files. */
#ifndef MIME_KERNELBASE_PRIVATE_H
#define MIME_KERNELBASE_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/feature.h>
#include "nlssort.h"

FORCEINLINE PPEB KbPeb(void)
{
    return NtCurrentTeb()->ProcessEnvironmentBlock;
}

FORCEINLINE PRTL_USER_PROCESS_PARAMETERS KbParams(void)
{
    return KbPeb()->ProcessParameters;
}

FORCEINLINE PVOID KbAlloc(SIZE_T size)
{
    return RtlAllocateHeap(KbPeb()->ProcessHeap, 0, size);
}

FORCEINLINE PVOID KbAllocZero(SIZE_T size)
{
    return RtlAllocateHeap(KbPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size);
}

FORCEINLINE void KbFree(PVOID p)
{
    if (p)
        RtlFreeHeap(KbPeb()->ProcessHeap, 0, p);
}

FORCEINLINE void KbSetError(DWORD error)
{
    NtCurrentTeb()->LastErrorValue = error;
}

/* elevation.c: ERROR_ELEVATION_REQUIRED when the manifest of the program asks for more than the token has. */
DWORD KbCheckElevation(HANDLE token, PCWSTR path);

/* sets the thread's last error from a status and returns FALSE. */
BOOL KbSetStatus(NTSTATUS status);

/* bounded formatting that always terminates; returns the length or -1 when truncated. */
int KbFormat(PWSTR buffer, SIZE_T size, PCWSTR format, ...);
int KbFormatA(PSTR buffer, SIZE_T size, PCSTR format, ...);

/* milliseconds to a relative nt timeout, NULL for INFINITE. */
PLARGE_INTEGER KbTimeout(PLARGE_INTEGER storage, DWORD milliseconds);

/* sets the last error on failure. */
BOOL KbDosPathToNt(LPCWSTR dos, PUNICODE_STRING nt);

/* ansi helpers using the file api code page; the results come from the process heap. */
UINT KbFileApiCodePage(void);
PWSTR KbAnsiToWide(LPCSTR s);
PWSTR KbAnsiToWideCp(UINT cp, LPCSTR s);
PSTR KbWideToAnsi(LPCWSTR s);

/*
 * copies a wide result into an ansi buffer for functions that return a character count: the characters written
 * without the terminator, or the size needed including it when the buffer is too small.
 */
DWORD KbWideResultToAnsi(LPCWSTR wide, LPSTR buffer, DWORD size);

/* named objects: attributes rooted in the session namespace. */
HANDLE KbNamedObjectRoot(void);
HANDLE KbPrivateNamespaceLookup(LPCWSTR *name);
BOOL KbNamedObjectAttributes(POBJECT_ATTRIBUTES oa, PUNICODE_STRING us, LPCWSTR name, BOOL inherit,
                             const SECURITY_ATTRIBUTES *sa);

/* the NUL device is a value the kernel never hands out. */
#define KB_NULL_DEVICE ((HANDLE)(ULONG_PTR)0xF)

FORCEINLINE BOOL KbIsPseudoHandle(HANDLE h)
{
    return h == KB_NULL_DEVICE;
}

void KbInitializeConsole(void);
HANDLE KbPseudoConsoleHandle(HPCON console);
HANDLE KbOpenConsoleDevice(LPCWSTR name, DWORD access, DWORD share, const SECURITY_ATTRIBUTES *sa, DWORD disposition);

/* debug output through NtDisplayString, chunked for its limits. */
void KbDisplayText(LPCWSTR text, SIZE_T chars);

/* process wide state set up by DllMain. */
void KbInitializeNls(void);
void KbInitializeCommandLine(void);
void KbThreadAttach(void);
void KbThreadDetach(void);
void KbInitializeDebug(void);
void KbProcessDetach(void);
void KbFlsThreadCleanup(void);

/* user apcs queued by kernelbase count themselves, so alertable waits can report them. */
void KbInitializeApc(void);
void KbNoteApc(void);
BOOL KbRunPendingApcs(void);

/* pe headers of a mapped image, or NULL. */
PIMAGE_NT_HEADERS KbImageNtHeader(PVOID base);

/* nls locale helpers. a locale is the index of its entry in the generated tables. */
LCID KbResolveLcid(LCID lcid);
BOOL KbIsSupportedLcid(LCID lcid);
UINT KbLocaleFromLcid(LCID lcid, BOOL allow_neutral);
UINT KbLocaleFromName(LPCWSTR name);
UINT KbLocaleCodePage(LCID lcid, UINT kind);
UINT KbResolveCodePage(UINT cp);
UINT KbLocaleCount(void);
PCWSTR KbLocaleName(UINT locale);
PCWSTR KbLocaleText(UINT locale, UINT slot);
INT KbLocaleNumber(UINT locale, UINT slot);
BOOL KbLocaleIsNeutral(UINT locale);
UINT KbLocaleSpecific(UINT locale);
const UINT8 *KbLocaleCalendars(UINT locale);
PCWSTR KbLocaleCalendarText(UINT locale, CALID cal, UINT slot);
PCWSTR KbLocaleCalendarList(UINT locale, CALID cal, CALTYPE type);
UINT KbLocaleCalendarEraYears(CALID cal, const UINT16 **years);
BOOL KbLocaleCalendarFacts(CALID cal, UINT *offset_range, UINT *two_digit_max);
UINT KbLocaleCalendarTypeKind(CALTYPE type);
BOOL KbLocaleSupportsCalendar(UINT locale, CALID cal);

/* calendar.c: dates in the windows calendars. */
typedef struct KB_CALDATE {
    int Year, Month, Day;
    int Era, EraYear;
    int DayOfWeek; /* Sunday is 0. */
} KB_CALDATE;

BOOL KbValidCalendar(CALID cal);
BOOL KbCalendarDate(CALID cal, LONG jdn, KB_CALDATE *out);
LONG KbGregorianToJdn(int year, int month, int day);
int KbDaysInGregorianMonth(int year, int month);
PCWSTR KbCalendarMonthName(UINT locale, CALID cal, int month, BOOL abbreviated, BOOL genitive);
PCWSTR KbCalendarEraName(UINT locale, CALID cal, int era, BOOL abbreviated);
PCWSTR KbCalendarDayName(UINT locale, CALID cal, int weekday, BOOL abbreviated);
int KbCalendarInfo(UINT locale, CALID calendar, CALTYPE type, LPWSTR data, int size, LPDWORD value);
PCWSTR KbCalendarPicture(UINT locale, CALID cal, UINT kind); /* kind: 0 short, 1 long, 2 year and month, 3 month and day. */

/* unidata.c: Unicode data for nls. */
UINT KbUniCcc(ULONG cp);
BOOL KbUniIsNonspacing(ULONG cp);
int KbUniDecompose(ULONG cp, BOOL compat, WCHAR *out); /* up to 20 units. */
ULONG KbUniCompose(ULONG first, ULONG second, BOOL hangul);
ULONG KbUniUpper(ULONG cp);
ULONG KbUniLower(ULONG cp);
ULONG KbUniFold(ULONG cp);

/* nlssort_data.c: the order of strings of Windows, its sort keys and its hash. */
const NLS_SORT_ORDER *KbSortOrderByName(PCWSTR name);
int KbCompareStrings(const NLS_SORT_ORDER *order, DWORD flags, PCWSTR a, int alen, PCWSTR b, int blen);
int KbSortKey(const NLS_SORT_ORDER *order, DWORD flags, PCWSTR src, int len, PBYTE key, int size);
int NlsSortFindString(const NLS_SORT_ORDER *order, DWORD flags, PCWSTR source, int n, PCWSTR value, int m, int *found);
BOOL KbSortIsDefined(PCWSTR text, int length);
DWORD KbSortId(const NLS_SORT_ORDER *order);
DWORD KbSortVersion(void);
DWORD KbSortHash(const NLS_SORT_ORDER *order, DWORD flags, PCWSTR src, int len);

/* character classification shared by string functions. */
WORD KbCharType1(WCHAR c);
WCHAR KbUpcase(WCHAR c);
WCHAR KbDowncase(WCHAR c);

/* resource lookup for FormatMessage and LoadString like code. */
BOOL KbFindMessage(HMODULE module, DWORD id, DWORD lang, LPWSTR *text, BOOL *allocated);

/* activation contexts: the manifest's reported version, long paths, thread inheritance. */
void KbReportedVersion(PULONG major, PULONG minor, PULONG build);
void KbInitializeLongPaths(void);
BOOL KbInheritActivationContext(HANDLE process, PACTIVATION_CONTEXT *context);

void KbFileTimeToSystemTime(LONGLONG t, LPSYSTEMTIME st);
BOOL KbSystemTimeToFileTime(const SYSTEMTIME *st, LONGLONG *t);
LONG KbTimeZoneBiasMinutes(void);

#endif
