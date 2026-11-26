/*
 * services.exe: memory, strings and the log.
 */
#include "services.h"
#include <stdarg.h>

CRITICAL_SECTION g_lock;
CONDITION_VARIABLE g_changed;

void *SvcAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void *SvcAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void SvcFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

WCHAR *SvcDupN(const WCHAR *s, SIZE_T chars)
{
    if (!s)
        return nullptr;
    WCHAR *d = SvcAlloc((chars + 1) * sizeof(WCHAR));
    if (d) {
        memcpy(d, s, chars * sizeof(WCHAR));
        d[chars] = 0;
    }
    return d;
}

WCHAR *SvcDup(const WCHAR *s)
{
    return s ? SvcDupN(s, wcslen(s)) : nullptr;
}

/* the characters of a multi sz including its final terminator, 1 for an empty one. */
DWORD SvcMultiSzChars(const WCHAR *m)
{
    if (!m)
        return 0;
    DWORD n = 0;
    while (m[n]) {
        n += (DWORD)wcslen(m + n) + 1;
    }
    return n + 1;
}

/* whether the list ends with two terminators within chars. */
BOOL SvcValidMultiSz(const WCHAR *m, DWORD chars)
{
    if (!m || !chars)
        return TRUE;
    return chars >= 2 ? (m[chars - 1] == 0 && m[chars - 2] == 0) : m[0] == 0;
}

static volatile LONG64 g_id = 0x10000;

UINT64 SvcNewId(void)
{
    return (UINT64)InterlockedIncrement64(&g_id) * 8;
}

void SvcLog(const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    int n = _vsnprintf(text, sizeof(text) - 2, format, args);
    va_end(args);
    if (n < 0 || n > (int)sizeof(text) - 2)
        n = sizeof(text) - 2;
    text[n++] = '\n';
    text[n] = 0;
    OutputDebugStringA(text);
}

/* the lock is held on entry and on return. */
void SvcSleepChanged(DWORD ms)
{
    SleepConditionVariableCS(&g_changed, &g_lock, ms);
}

/* a manual reset event with the SCM_EVENT_SDDL descriptor, for the session's programs to wait on. */
HANDLE SvcCreateWorldEvent(const WCHAR *name)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, FALSE};
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(SCM_EVENT_SDDL, SDDL_REVISION_1, &sd, nullptr))
        sa.lpSecurityDescriptor = sd;
    HANDLE event = CreateEventW(&sa, TRUE, FALSE, name);
    DWORD error = GetLastError();
    if (sd)
        LocalFree(sd);
    SetLastError(error);
    return event;
}

/*
 * the event the local session manager of windows makes and sets once the sessions can be asked about, which
 * winscard.dll opens before it does anything. the descriptor is the one of windows 11 26100: everyone and the app
 * containers wait on it, the system sets it. mime has no session manager process, its sessions are there from the
 * start, so the service manager makes the event set.
 */
void SvcCreateSessionReadyEvent(void)
{
    static const WCHAR sddl[] =
        L"O:BAG:SYD:(A;;0x100000;;;WD)(A;;0x100000;;;AC)"
        L"(A;;0x100000;;;S-1-15-3-1024-1864111754-776273317-3666925027-2523908081-3792458206-3582472437-4114419977-1582884857)"
        L"(A;;DC;;;SY)";
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), nullptr, FALSE};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, 1, &attributes.lpSecurityDescriptor, nullptr))
        return;
    CreateEventW(&attributes, TRUE, TRUE, L"Global\\TermSrvReadyEvent");
    LocalFree(attributes.lpSecurityDescriptor);
}
