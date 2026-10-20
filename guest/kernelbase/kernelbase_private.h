/*
 * kernelbase internal interface shared by its source files.
 */
#ifndef MIME_KERNELBASE_PRIVATE_H
#define MIME_KERNELBASE_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

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

/* translates a status into the thread's last error; returns FALSE for convenience. */
BOOL KbSetStatus(NTSTATUS status);

/* bounded formatting that always terminates; returns the length or -1 when truncated. */
int KbFormat(PWSTR buffer, SIZE_T size, PCWSTR format, ...);
int KbFormatA(PSTR buffer, SIZE_T size, PCSTR format, ...);

/* converts milliseconds into a relative nt timeout, NULL for INFINITE. */
PLARGE_INTEGER KbTimeout(PLARGE_INTEGER storage, DWORD milliseconds);

/* converts a dos path into an nt path; sets the last error on failure. */
BOOL KbDosPathToNt(LPCWSTR dos, PUNICODE_STRING nt);

/* ansi helpers using the file api code page; the results come from the process heap. */
UINT KbFileApiCodePage(void);
PWSTR KbAnsiToWide(LPCSTR s);
PWSTR KbAnsiToWideCp(UINT cp, LPCSTR s);
PSTR KbWideToAnsi(LPCWSTR s);

/*
 * copies a wide result into an ansi buffer for functions that return a
 * character count: returns the characters written without the terminator,
 * or the size needed including it when the buffer is too small.
 */
DWORD KbWideResultToAnsi(LPCWSTR wide, LPSTR buffer, DWORD size);

/* named objects: builds attributes rooted in the session namespace. */
BOOL KbNamedObjectAttributes(POBJECT_ATTRIBUTES oa, PUNICODE_STRING us, LPCWSTR name, BOOL inherit,
                             const SECURITY_ATTRIBUTES *sa);

/* the NUL device is a value the kernel never hands out. */
#define KB_NULL_DEVICE ((HANDLE)(ULONG_PTR)0xF)

FORCEINLINE BOOL KbIsPseudoHandle(HANDLE h)
{
    return h == KB_NULL_DEVICE;
}

void KbInitializeConsole(void);
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

/* locale helpers shared by nls and formatting code. */
LCID KbResolveLcid(LCID lcid);
BOOL KbIsSupportedLcid(LCID lcid);

/* character classification shared by string functions. */
WORD KbCharType1(WCHAR c);
WCHAR KbUpcase(WCHAR c);
WCHAR KbDowncase(WCHAR c);

/* resource lookup helper used by FormatMessage and LoadString like code. */
BOOL KbFindMessage(HMODULE module, DWORD id, DWORD lang, LPWSTR *text, BOOL *allocated);

/* activation contexts: the version the program's manifest earns it, long paths, thread inheritance. */
void KbReportedVersion(PULONG major, PULONG minor, PULONG build);
void KbInitializeLongPaths(void);
BOOL KbInheritActivationContext(HANDLE process, PACTIVATION_CONTEXT *context);

/* time conversion helpers. */
void KbFileTimeToSystemTime(LONGLONG t, LPSYSTEMTIME st);
BOOL KbSystemTimeToFileTime(const SYSTEMTIME *st, LONGLONG *t);
LONG KbTimeZoneBiasMinutes(void);

#endif
