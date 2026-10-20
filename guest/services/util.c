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

/* the characters of a multi sz with its last terminator, or 1 for an empty one. */
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

/* whether a list of strings of the given length ends with the two terminators. */
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

/* waits for any change of state, with the lock held on entry and on return. */
void SvcSleepChanged(DWORD ms)
{
    SleepConditionVariableCS(&g_changed, &g_lock, ms);
}

/* a manual reset event the programs of the session wait on, made with the descriptor of SCM_EVENT_SDDL. */
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
