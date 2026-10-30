/*
 * wkscli.dll: the client of the workstation service. netapi32 loads it for NetWkstaGetInfo and the others and it talks to
 * LanmanWorkstation over \PIPE\wkssvc, binding to the local machine or to a server named by the caller. A call that
 * cannot reach the service is told apart as Windows' NetpHandleRpcFailure does: the service not started, or started and the
 * machine not reachable.
 */
#define WKSSVC_CLIENT_ONLY
#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/rpc.h>
#include "wkssvc/wkssvc.h"

typedef DWORD NET_API_STATUS;
#define GetExceptionCode() __exception_code()
#define NERR_WkstaNotStarted 2138
#define NERR_InternalError 2140
#define ERROR_BAD_NETPATH_ 53
#define STATUS_ACCESS_VIOLATION_ ((DWORD)0xC0000005)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

void *WINAPI MIDL_user_allocate(size_t size)
{
    return LocalAlloc(LMEM_FIXED, size);
}

void WINAPI MIDL_user_free(void *p)
{
    LocalFree(p);
}

/* ------------------------------------------------------------------ binding */

static BOOL is_local(const WCHAR *server)
{
    if (!server || !*server)
        return TRUE;
    while (*server == L'\\')
        server++;
    if (!*server || !_wcsicmp(server, L".") || !_wcsicmp(server, L"localhost"))
        return TRUE;
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = RTL_NUMBER_OF(computer);
    return GetComputerNameW(computer, &n) && !_wcsicmp(server, computer);
}

static handle_t bind_to(const WCHAR *server)
{
    WCHAR host[260];
    WCHAR *string = nullptr;
    handle_t binding = nullptr;
    if (is_local(server)) {
        host[0] = 0;
    } else {
        /* a name without the two backslashes gets them. */
        if (server[0] == L'\\' && server[1] == L'\\')
            _snwprintf(host, RTL_NUMBER_OF(host) - 1, L"%ls", server);
        else
            _snwprintf(host, RTL_NUMBER_OF(host) - 1, L"\\\\%ls", server);
        host[RTL_NUMBER_OF(host) - 1] = 0;
    }
    if (RpcStringBindingComposeW(nullptr, (RPC_WSTR)L"ncacn_np", host[0] ? (RPC_WSTR)host : nullptr,
                                 (RPC_WSTR)L"\\PIPE\\wkssvc", nullptr, (RPC_WSTR *)&string) != RPC_S_OK)
        return nullptr;
    if (RpcBindingFromStringBindingW((RPC_WSTR)string, &binding) != RPC_S_OK)
        binding = nullptr;
    RpcStringFreeW((RPC_WSTR *)&string);
    return binding;
}

handle_t WINAPI WKSSVC_IDENTIFY_HANDLE_bind(WKSSVC_IDENTIFY_HANDLE server)
{
    return bind_to(server);
}

void WINAPI WKSSVC_IDENTIFY_HANDLE_unbind(WKSSVC_IDENTIFY_HANDLE server, handle_t binding)
{
    RpcBindingFree(&binding);
}

handle_t WINAPI WKSSVC_IMPERSONATE_HANDLE_bind(WKSSVC_IMPERSONATE_HANDLE server)
{
    return bind_to(server);
}

void WINAPI WKSSVC_IMPERSONATE_HANDLE_unbind(WKSSVC_IMPERSONATE_HANDLE server, handle_t binding)
{
    RpcBindingFree(&binding);
}

/* ------------------------------------------------------------------ failures */

static BOOL service_started(void)
{
    BOOL started = FALSE;
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return FALSE;
    SC_HANDLE service = OpenServiceW(manager, L"LanmanWorkstation", SERVICE_QUERY_STATUS);
    if (service) {
        SERVICE_STATUS status;
        if (QueryServiceStatus(service, &status))
            started = (status.dwCurrentState >= SERVICE_RUNNING && status.dwCurrentState <= SERVICE_PAUSED) ||
                      status.dwCurrentState == SERVICE_RUNNING;
        CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
    return started;
}

/* what an rpc status becomes for the caller. */
static NET_API_STATUS rpc_failure(DWORD code)
{
    if (code == 0)
        return NERR_InternalError;
    if (code == RPC_S_SERVER_UNAVAILABLE || code == RPC_S_SERVER_TOO_BUSY) {
        if (service_started())
            return code == RPC_S_SERVER_UNAVAILABLE ? ERROR_BAD_NETPATH_ : code;
        return NERR_WkstaNotStarted;
    }
    if (code == STATUS_ACCESS_VIOLATION_)
        return ERROR_NOACCESS;
    return code;
}

#define CALL(status, call)                                                                                            \
    do {                                                                                                              \
        __try {                                                                                                       \
            (status) = (call);                                                                                        \
        }                                                                                                             \
        __except (EXCEPTION_EXECUTE_HANDLER) {                                                                        \
            (status) = rpc_failure(GetExceptionCode());                                                               \
        }                                                                                                             \
    } while (0)

/* ------------------------------------------------------------------ copies in one block */

typedef struct BLOCK {
    BYTE *Start, *Strings, *Limit;
} BLOCK;

static unsigned short *put(BLOCK *b, const unsigned short *text)
{
    if (!text)
        return nullptr;
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    b->Strings -= bytes;
    memcpy(b->Strings, text, bytes);
    return (unsigned short *)b->Strings;
}

static SIZE_T text_bytes(const unsigned short *text)
{
    return text ? (wcslen(text) + 1) * sizeof(WCHAR) : 0;
}

/* a result of the service is a structure with strings that were allocated one by one; the caller gets one block. */
static void *flatten(const void *fixed, SIZE_T fixed_size, const SIZE_T *offsets, const unsigned short *const *strings,
                     SIZE_T count)
{
    SIZE_T total = (fixed_size + 7) & ~(SIZE_T)7;
    for (SIZE_T i = 0; i < count; i++)
        total += text_bytes(strings[i]);
    BLOCK b;
    b.Start = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, total);
    if (!b.Start)
        return nullptr;
    b.Limit = b.Start + total;
    b.Strings = b.Limit;
    memcpy(b.Start, fixed, fixed_size);
    for (SIZE_T i = 0; i < count; i++)
        *(unsigned short **)(b.Start + offsets[i]) = put(&b, strings[i]);
    return b.Start;
}

/* ------------------------------------------------------------------ the calls */

NET_API_STATUS WINAPI NetWkstaGetInfo(LPWSTR server, DWORD level, LPBYTE *buffer)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    WKSTA_INFO info = {};
    NET_API_STATUS status;
    CALL(status, wkssvc_NetrWkstaGetInfo(server, level, &info));
    if (status != NO_ERROR)
        return status;
    if (level == 502) {
        *buffer = (LPBYTE)info.WkstaInfo502;
        return NO_ERROR;
    }
    /* the three levels begin alike. */
    WKSTA_INFO_102 *in = (WKSTA_INFO_102 *)info.WkstaInfo102;
    if (!in)
        return NERR_InternalError;
    const SIZE_T offsets[3] = {offsetof(WKSTA_INFO_102, wki102_computername), offsetof(WKSTA_INFO_102, wki102_langroup),
                               offsetof(WKSTA_INFO_102, wki102_lanroot)};
    const unsigned short *strings[3] = {in->wki102_computername, in->wki102_langroup, in->wki102_lanroot};
    SIZE_T size = level == 100 ? sizeof(WKSTA_INFO_100) : level == 101 ? sizeof(WKSTA_INFO_101) : sizeof(WKSTA_INFO_102);
    *buffer = flatten(in, size, offsets, strings, level == 100 ? 2 : 3);
    MIDL_user_free(in->wki102_computername);
    MIDL_user_free(in->wki102_langroup);
    MIDL_user_free(in->wki102_lanroot);
    MIDL_user_free(in);
    return *buffer ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
}

NET_API_STATUS WINAPI NetGetJoinInformation(LPCWSTR server, LPWSTR *name, NETSETUP_JOIN_STATUS *type)
{
    if (!name || !type)
        return ERROR_INVALID_PARAMETER;
    unsigned short *text = nullptr;
    NET_API_STATUS status;
    CALL(status, wkssvc_NetrGetJoinInformation((LPWSTR)server, &text, type));
    if (status == NO_ERROR)
        *name = text;
    return status;
}

NET_API_STATUS WINAPI NetWkstaUserGetInfo(LPWSTR reserved, DWORD level, LPBYTE *buffer)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    WKSTA_USER_INFO info = {};
    NET_API_STATUS status;
    CALL(status, wkssvc_NetrWkstaUserGetInfo(reserved, level, &info));
    if (status != NO_ERROR)
        return status;
    if (level == 0) {
        WKSTA_USER_INFO_0 *in = info.UserInfo0;
        const SIZE_T offsets[1] = {0};
        const unsigned short *strings[1] = {in->wkui0_username};
        *buffer = flatten(in, sizeof(*in), offsets, strings, 1);
        MIDL_user_free(in->wkui0_username);
        MIDL_user_free(in);
    } else if (level == 1101) {
        WKSTA_USER_INFO_1101 *in = info.UserInfo1101;
        const SIZE_T offsets[1] = {0};
        const unsigned short *strings[1] = {in->wkui1101_oth_domains};
        *buffer = flatten(in, sizeof(*in), offsets, strings, 1);
        MIDL_user_free(in->wkui1101_oth_domains);
        MIDL_user_free(in);
    } else {
        WKSTA_USER_INFO_1 *in = info.UserInfo1;
        const SIZE_T offsets[4] = {offsetof(WKSTA_USER_INFO_1, wkui1_username),
                                   offsetof(WKSTA_USER_INFO_1, wkui1_logon_domain),
                                   offsetof(WKSTA_USER_INFO_1, wkui1_oth_domains),
                                   offsetof(WKSTA_USER_INFO_1, wkui1_logon_server)};
        const unsigned short *strings[4] = {in->wkui1_username, in->wkui1_logon_domain, in->wkui1_oth_domains,
                                            in->wkui1_logon_server};
        *buffer = flatten(in, sizeof(*in), offsets, strings, 4);
        MIDL_user_free(in->wkui1_username);
        MIDL_user_free(in->wkui1_logon_domain);
        MIDL_user_free(in->wkui1_oth_domains);
        MIDL_user_free(in->wkui1_logon_server);
        MIDL_user_free(in);
    }
    return *buffer ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
}
