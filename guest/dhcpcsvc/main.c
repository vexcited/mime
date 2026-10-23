/*
 * the dhcp client api. mime runs no dhcp client service, so calls check
 * their arguments like windows, then find no service to ask.
 */
#include <mime/win32/winbase.h>

#define RPC_S_SERVER_UNAVAILABLE 1722L

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

typedef struct {
    ULONG Flags;
    ULONG OptionId;
    BOOL IsVendor;
    LPBYTE Data;
    DWORD nBytesData;
} DHCPCAPI_PARAMS;

typedef struct {
    ULONG nParams;
    DHCPCAPI_PARAMS *Params;
} DHCPCAPI_PARAMS_ARRAY;

typedef struct {
    ULONG Flags;
    LPBYTE Data;
    ULONG nBytesData;
} DHCPCAPI_CLASSID;

DWORD WINAPI DhcpCApiInitialize(LPDWORD version)
{
    if (version)
        *version = 2;
    return ERROR_SUCCESS;
}

void WINAPI DhcpCApiCleanup(void)
{
}

DWORD WINAPI DhcpRequestParams(DWORD flags, LPVOID reserved, LPWSTR adapter, DHCPCAPI_CLASSID *class_id,
                               DHCPCAPI_PARAMS_ARRAY send, DHCPCAPI_PARAMS_ARRAY receive, LPBYTE buffer,
                               LPDWORD size, LPWSTR request_id)
{
    if (!adapter || !size || !(flags & 3) || (flags & ~3u) || reserved || !receive.nParams || (!buffer && *size))
        return ERROR_INVALID_PARAMETER;
    if (class_id && (class_id->Flags || !class_id->Data || !class_id->nBytesData))
        return ERROR_INVALID_PARAMETER;
    if (!receive.Params || (!send.Params && send.nParams))
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI DhcpUndoRequestParams(DWORD flags, LPVOID reserved, LPWSTR adapter, LPWSTR request_id)
{
    if (flags || reserved || !request_id)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

DWORD WINAPI DhcpRegisterParamChange(DWORD flags, LPVOID reserved, LPWSTR adapter, DHCPCAPI_CLASSID *class_id,
                                     DHCPCAPI_PARAMS_ARRAY params, LPVOID handle)
{
    if (flags != 1 || reserved || !adapter || !params.nParams || !handle || params.nParams > 0x200)
        return ERROR_INVALID_PARAMETER;
    return RPC_S_SERVER_UNAVAILABLE;
}

/* nothing was ever registered. */
DWORD WINAPI DhcpDeRegisterParamChange(DWORD flags, LPVOID reserved, LPVOID event)
{
    return ERROR_INVALID_PARAMETER;
}
