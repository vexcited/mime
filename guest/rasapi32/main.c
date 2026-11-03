#include <mime/win32/winbase.h>

/* a machine with no dial-up or vpn connection and no phonebook entry. */

#define ERROR_INVALID_SIZE_ 632
#define ERROR_INVALID_HANDLE_RAS_ 6

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/* every RAS structure starts with its size. */
static DWORD enumerate(const DWORD *first, LPDWORD bytes, LPDWORD count)
{
    if (!bytes || !count)
        return ERROR_INVALID_PARAMETER;
    if (first && (*bytes < sizeof(DWORD) || !*first))
        return ERROR_INVALID_SIZE_;
    *bytes = 0;
    *count = 0;
    return ERROR_SUCCESS;
}

DWORD WINAPI RasEnumConnectionsW(PVOID connections, LPDWORD bytes, LPDWORD count)
{
    return enumerate(connections, bytes, count);
}

DWORD WINAPI RasEnumConnectionsA(PVOID connections, LPDWORD bytes, LPDWORD count)
{
    return enumerate(connections, bytes, count);
}

DWORD WINAPI RasEnumEntriesW(LPCWSTR reserved, LPCWSTR phonebook, PVOID entries, LPDWORD bytes, LPDWORD count)
{
    return reserved ? ERROR_NOT_SUPPORTED : enumerate(entries, bytes, count);
}

DWORD WINAPI RasEnumEntriesA(LPCSTR reserved, LPCSTR phonebook, PVOID entries, LPDWORD bytes, LPDWORD count)
{
    return reserved ? ERROR_NOT_SUPPORTED : enumerate(entries, bytes, count);
}

/* the event is never set as no connection ever comes or goes. */
DWORD WINAPI RasConnectionNotificationW(HANDLE connection, HANDLE event, DWORD flags)
{
    return event ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI RasConnectionNotificationA(HANDLE connection, HANDLE event, DWORD flags)
{
    return RasConnectionNotificationW(connection, event, flags);
}

DWORD WINAPI RasGetConnectStatusW(HANDLE connection, PVOID status)
{
    return ERROR_INVALID_HANDLE_RAS_;
}

DWORD WINAPI RasGetConnectStatusA(HANDLE connection, PVOID status)
{
    return ERROR_INVALID_HANDLE_RAS_;
}

DWORD WINAPI RasHangUpW(HANDLE connection)
{
    return ERROR_INVALID_HANDLE_RAS_;
}

DWORD WINAPI RasHangUpA(HANDLE connection)
{
    return ERROR_INVALID_HANDLE_RAS_;
}
