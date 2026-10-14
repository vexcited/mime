#include "wts_private.h"
#include <mime/win32/winuser.h>

static SRWLOCK server_lock;
static struct {
    USHORT Generation;
    BOOL Active;
} servers[128];

static ULONG server_index(HANDLE server)
{
    ULONG_PTR value = (ULONG_PTR)server;
    if ((value & ~(ULONG_PTR)0xffffff) != 0x57000000 || !(value & 0xff) || (value & 0xff) > RTL_NUMBER_OF(servers))
        return RTL_NUMBER_OF(servers);
    ULONG index = (value & 0xff) - 1;
    return servers[index].Active && servers[index].Generation == ((value >> 8) & 0xffff) ? index : RTL_NUMBER_OF(servers);
}

BOOL MimeWtsServerValid(HANDLE server)
{
    if (!server)
        return TRUE;
    AcquireSRWLockShared(&server_lock);
    BOOL valid = server_index(server) < RTL_NUMBER_OF(servers);
    ReleaseSRWLockShared(&server_lock);
    return valid;
}

DECLSPEC_EXPORT HANDLE WINAPI WTSOpenServerW(LPWSTR name)
{
    if (name) {
        while (*name == L'\\')
            name++;
    }
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = RTL_NUMBER_OF(computer);
    if (name && *name && _wcsicmp(name, L".") && _wcsicmp(name, L"localhost") &&
        (!GetComputerNameW(computer, &size) || _wcsicmp(name, computer))) {
        SetLastError(ERROR_BAD_NETPATH);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE result = INVALID_HANDLE_VALUE;
    AcquireSRWLockExclusive(&server_lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(servers); i++) {
        if (servers[i].Active)
            continue;
        if (!++servers[i].Generation)
            servers[i].Generation++;
        servers[i].Active = TRUE;
        result = (HANDLE)(ULONG_PTR)(0x57000000u | (ULONG)servers[i].Generation << 8 | (i + 1));
        break;
    }
    ReleaseSRWLockExclusive(&server_lock);
    if (result == INVALID_HANDLE_VALUE)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return result;
}

DECLSPEC_EXPORT void WINAPI WTSCloseServer(HANDLE server)
{
    AcquireSRWLockExclusive(&server_lock);
    ULONG index = server_index(server);
    if (index < RTL_NUMBER_OF(servers))
        servers[index].Active = FALSE;
    ReleaseSRWLockExclusive(&server_lock);
}
