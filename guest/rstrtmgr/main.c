/*
 * rstrtmgr.dll: the restart manager. a session is a number the library gives out, and the resources registered with it
 * are not remembered, because there is no way here to find which processes hold a file open. so a list of the
 * applications that use the resources is empty and there is nothing to shut down or restart.
 */
#include <mime/win32/winbase.h>
#include <rtl.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define CCH_RM_SESSION_KEY 32
#define ERROR_MAX_SESSIONS_REACHED_ 353
#define SESSION_MAX 64

static SRWLOCK g_lock = SRWLOCK_INIT;
static BOOL g_session_open[SESSION_MAX];
static DWORD g_next;

DWORD WINAPI RmStartSession(DWORD *session, DWORD flags, WCHAR key[CCH_RM_SESSION_KEY + 1])
{
    if (!session || !key || flags)
        return ERROR_INVALID_PARAMETER;
    DWORD found = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (DWORD i = 0; i < SESSION_MAX && !found; i++) {
        DWORD candidate = (g_next + i) % SESSION_MAX;
        if (!g_session_open[candidate]) {
            g_session_open[candidate] = TRUE;
            g_next = candidate + 1;
            found = candidate + 1;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!found)
        return ERROR_MAX_SESSIONS_REACHED_;
    *session = found;
    /* the key that lets another process join, as 32 hex digits made of the session and the process. */
    static const WCHAR digits[] = L"0123456789ABCDEF";
    DWORD parts[4] = {found, GetCurrentProcessId(), GetTickCount(), 0x52737472};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
            key[i * 8 + j] = digits[(parts[i] >> (28 - 4 * j)) & 15];
    key[CCH_RM_SESSION_KEY] = 0;
    return ERROR_SUCCESS;
}

static BOOL session_exists(DWORD session)
{
    AcquireSRWLockShared(&g_lock);
    BOOL open = session >= 1 && session <= SESSION_MAX && g_session_open[session - 1];
    ReleaseSRWLockShared(&g_lock);
    return open;
}

DWORD WINAPI RmJoinSession(DWORD *session, const WCHAR key[CCH_RM_SESSION_KEY + 1])
{
    if (!session || !key)
        return ERROR_INVALID_PARAMETER;
    DWORD number = 0;
    for (int j = 0; j < 8; j++) {
        WCHAR c = key[j];
        number = number << 4 | (c >= L'A' ? (DWORD)(c - L'A' + 10) : (DWORD)(c - L'0'));
    }
    if (!session_exists(number))
        return ERROR_INVALID_HANDLE;
    *session = number;
    return ERROR_SUCCESS;
}

DWORD WINAPI RmEndSession(DWORD session)
{
    AcquireSRWLockExclusive(&g_lock);
    BOOL open = session >= 1 && session <= SESSION_MAX && g_session_open[session - 1];
    if (open)
        g_session_open[session - 1] = FALSE;
    ReleaseSRWLockExclusive(&g_lock);
    return open ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

DWORD WINAPI RmRegisterResources(DWORD session, UINT files, LPCWSTR *file_names, UINT applications, PVOID application_list,
                                 UINT services, LPCWSTR *service_names)
{
    if (!session_exists(session))
        return ERROR_INVALID_HANDLE;
    if ((files && !file_names) || (applications && !application_list) || (services && !service_names))
        return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}

DWORD WINAPI RmGetList(DWORD session, UINT *needed, UINT *count, PVOID applications, LPDWORD reboot_reasons)
{
    if (!session_exists(session))
        return ERROR_INVALID_HANDLE;
    if (!needed || !count || !reboot_reasons)
        return ERROR_INVALID_PARAMETER;
    *needed = 0;
    *count = 0;
    *reboot_reasons = 0;
    return ERROR_SUCCESS;
}

DWORD WINAPI RmShutdown(DWORD session, ULONG flags, PVOID status_callback)
{
    return session_exists(session) ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

DWORD WINAPI RmRestart(DWORD session, DWORD flags, PVOID status_callback)
{
    return session_exists(session) ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

DWORD WINAPI RmCancelCurrentTask(DWORD session)
{
    return session_exists(session) ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}
