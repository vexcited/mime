#ifndef __i386__
#include "mswsock_private.h"

static DWORD thread_slot = TLS_OUT_OF_INDEXES;
static CRITICAL_SECTION catalog_lock;
static WSPUPCALLTABLE upcalls;
static LONG started;

/* the catalog entries of this dll, as ws2_32 handed them to WSPStartup or WSCEnumProtocols lists them. */
#define MAX_ENTRIES 32
static WSAPROTOCOL_INFOW entries[MAX_ENTRIES];
static int entry_count;
static BOOL enumerated;

static const GUID msafd_v4 = {0xE70F1AA0, 0xAB8B, 0x11CF, {0x8C, 0xA3, 0x00, 0x80, 0x5F, 0x48, 0xA1, 0x92}};
static const GUID msafd_v6 = {0xF9EAB0C0, 0x26D4, 0x11D0, {0xBB, 0xBF, 0x00, 0xAA, 0x00, 0x6C, 0x34, 0xE4}};

static void free_thread(void)
{
    if (thread_slot == TLS_OUT_OF_INDEXES)
        return;
    WS_THREAD *t = TlsGetValue(thread_slot);
    if (t && t->event)
        CloseHandle(t->event);
    WsFree(t);
    TlsSetValue(thread_slot, nullptr);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        InitializeCriticalSection(&catalog_lock);
        thread_slot = TlsAlloc();
        WsSocketsInitialize();
        break;
    case DLL_THREAD_DETACH:
        free_thread();
        break;
    case DLL_PROCESS_DETACH:
        free_thread();
        if (thread_slot != TLS_OUT_OF_INDEXES)
            TlsFree(thread_slot);
        break;
    }
    return TRUE;
}

WS_THREAD *WsThread(void)
{
    if (thread_slot == TLS_OUT_OF_INDEXES)
        return nullptr;
    WS_THREAD *t = TlsGetValue(thread_slot);
    if (!t) {
        t = WsAllocZero(sizeof(*t));
        if (t)
            TlsSetValue(thread_slot, t);
    }
    return t;
}

/* ws2_32 checks WSAStartup before it calls a provider. */
int WsCheckStarted(void)
{
    return 0;
}

int WSAAPI WSAGetLastError(void)
{
    return (int)GetLastError();
}

void WSAAPI WSASetLastError(int error)
{
    SetLastError((DWORD)error);
}

u_long WSAAPI htonl(u_long v)
{
    return __builtin_bswap32(v);
}

u_long WSAAPI ntohl(u_long v)
{
    return __builtin_bswap32(v);
}

u_short WSAAPI htons(u_short v)
{
    return __builtin_bswap16(v);
}

u_short WSAAPI ntohs(u_short v)
{
    return __builtin_bswap16(v);
}

/* ------------------------------------------------------------------ the catalog */

/* the catalog keeps names as @dll,-id, which ws2_32 and this provider read with LoadStringW. */
static void resolve_name(WSAPROTOCOL_INFOW *info)
{
    WCHAR *text = info->szProtocol, path[MAX_PATH], expanded[MAX_PATH];
    if (text[0] != L'@')
        return;
    int comma = -1;
    for (int i = 1; text[i] && i < WSAPROTOCOL_LEN; i++) {
        if (text[i] == L',')
            comma = i;
    }
    if (comma < 2 || text[comma + 1] != L'-' || comma > MAX_PATH)
        return;
    UINT id = 0;
    for (int i = comma + 2; text[i] >= L'0' && text[i] <= L'9'; i++)
        id = id * 10 + (UINT)(text[i] - L'0');
    memcpy(path, text + 1, (comma - 1) * sizeof(WCHAR));
    path[comma - 1] = 0;
    if (!ExpandEnvironmentStringsW(path, expanded, MAX_PATH))
        return;
    HMODULE module = LoadLibraryExW(expanded, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module)
        return;
    WCHAR name[WSAPROTOCOL_LEN + 1];
    if (LoadStringW(module, id, name, RTL_NUMBER_OF(name)) > 0)
        memcpy(text, name, sizeof(name));
    FreeLibrary(module);
}

static void add_entry(const WSAPROTOCOL_INFOW *info)
{
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].dwCatalogEntryId == info->dwCatalogEntryId)
            return;
    }
    if (entry_count < MAX_ENTRIES) {
        entries[entry_count] = *info;
        resolve_name(&entries[entry_count++]);
    }
}

typedef int(WSAAPI *WSC_ENUM_PROTOCOLS)(LPINT protocols, LPWSAPROTOCOL_INFOW buffer, LPDWORD length, LPINT error);

/* the entries ws2_32 has not started yet, for sockets of a kind this process has not made itself. */
static void enumerate_catalog(void)
{
    enumerated = TRUE;
    HMODULE ws2 = GetModuleHandleW(L"ws2_32.dll");
    WSC_ENUM_PROTOCOLS enum_protocols = ws2 ? (WSC_ENUM_PROTOCOLS)GetProcAddress(ws2, "WSCEnumProtocols") : nullptr;
    if (!enum_protocols)
        return;
    DWORD size = 0;
    int error = 0;
    enum_protocols(nullptr, nullptr, &size, &error);
    WSAPROTOCOL_INFOW *list = size ? WsAlloc(size) : nullptr;
    if (!list)
        return;
    int n = enum_protocols(nullptr, list, &size, &error);
    for (int i = 0; i < n; i++)
        add_entry(&list[i]);
    WsFree(list);
}

static BOOL is_msafd(const WSAPROTOCOL_INFOW *info)
{
    return !memcmp(&info->ProviderId, &msafd_v4, sizeof(GUID)) || !memcmp(&info->ProviderId, &msafd_v6, sizeof(GUID));
}

static const WSAPROTOCOL_INFOW *find_entry(int family, int type)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < entry_count; i++) {
            const WSAPROTOCOL_INFOW *e = &entries[i];
            if (e->iAddressFamily == family && e->iSocketType == type && e->ProtocolChain.ChainLen == BASE_PROTOCOL &&
                (pass || is_msafd(e)))
                return e;
        }
    }
    return nullptr;
}

static BOOL catalog_entry(int family, int type, WSAPROTOCOL_INFOW *info)
{
    EnterCriticalSection(&catalog_lock);
    const WSAPROTOCOL_INFOW *e = find_entry(family, type);
    if (!e && !enumerated) {
        enumerate_catalog();
        e = find_entry(family, type);
    }
    if (e)
        *info = *e;
    LeaveCriticalSection(&catalog_lock);
    return e != nullptr;
}

/* an ifs socket is a file handle, which ws2_32 records under the catalog entry of its kind. */
static int announce_socket(SOCKET s, int family, int type)
{
    WSAPROTOCOL_INFOW info;
    if (!catalog_entry(family, type, &info))
        return WSAEPROTONOSUPPORT;
    int error = 0;
    if (upcalls.lpWPUModifyIFSHandle(info.dwCatalogEntryId, s, &error) == INVALID_SOCKET)
        return error ? error : WSAENOBUFS;
    return 0;
}

/* ------------------------------------------------------------------ the procedures */

static int result(int r, LPINT error)
{
    if (r == SOCKET_ERROR)
        *error = (int)GetLastError();
    return r;
}

static SOCKET socket_result(SOCKET s, LPINT error)
{
    if (s == INVALID_SOCKET)
        *error = (int)GetLastError();
    return s;
}

static SOCKET WSPAPI wsp_accept(SOCKET s, struct sockaddr *addr, LPINT addrlen, LPCONDITIONPROC condition,
                                DWORD_PTR data, LPINT error)
{
    return socket_result(WSAAccept(s, addr, addrlen, condition, data), error);
}

static INT WSPAPI wsp_address_to_string(LPSOCKADDR address, DWORD length, LPWSAPROTOCOL_INFOW info, LPWSTR string,
                                        LPDWORD string_length, LPINT error)
{
    return result(WSAAddressToStringW(address, length, info, string, string_length), error);
}

static int WSPAPI wsp_async_select(SOCKET s, HWND hwnd, unsigned int msg, long events, LPINT error)
{
    return result(WSAAsyncSelect(s, hwnd, msg, events), error);
}

static int WSPAPI wsp_bind(SOCKET s, const struct sockaddr *name, int namelen, LPINT error)
{
    return result(bind(s, name, namelen), error);
}

static int WSPAPI wsp_cancel_blocking_call(LPINT error)
{
    return 0;
}

static int WSPAPI wsp_cleanup(LPINT error)
{
    if (InterlockedDecrement(&started) == 0)
        WsCloseAllSockets();
    return 0;
}

static int WSPAPI wsp_close_socket(SOCKET s, LPINT error)
{
    return result(closesocket(s), error);
}

static int WSPAPI wsp_connect(SOCKET s, const struct sockaddr *name, int namelen, LPWSABUF caller_data,
                              LPWSABUF callee_data, void *sqos, void *gqos, LPINT error)
{
    return result(WSAConnect(s, name, namelen, caller_data, callee_data, sqos, gqos), error);
}

static int WSPAPI wsp_duplicate_socket(SOCKET s, DWORD process_id, LPWSAPROTOCOL_INFOW info, LPINT error)
{
    return result(WSADuplicateSocketW(s, process_id, info), error);
}

static int WSPAPI wsp_enum_network_events(SOCKET s, WSAEVENT event, LPWSANETWORKEVENTS events, LPINT error)
{
    return result(WSAEnumNetworkEvents(s, event, events), error);
}

static int WSPAPI wsp_event_select(SOCKET s, WSAEVENT event, long events, LPINT error)
{
    return result(WSAEventSelect(s, event, events), error);
}

static BOOL WSPAPI wsp_get_overlapped_result(SOCKET s, LPWSAOVERLAPPED overlapped, LPDWORD transferred, BOOL wait,
                                             LPDWORD flags, LPINT error)
{
    BOOL ok = WSAGetOverlappedResult(s, overlapped, transferred, wait, flags);
    if (!ok)
        *error = (int)GetLastError();
    return ok;
}

static int WSPAPI wsp_get_peer_name(SOCKET s, struct sockaddr *name, LPINT namelen, LPINT error)
{
    return result(getpeername(s, name, namelen), error);
}

static int WSPAPI wsp_get_sock_name(SOCKET s, struct sockaddr *name, LPINT namelen, LPINT error)
{
    return result(getsockname(s, name, namelen), error);
}

static int WSPAPI wsp_get_sock_opt(SOCKET s, int level, int name, char *value, LPINT length, LPINT error)
{
    return result(getsockopt(s, level, name, value, length), error);
}

static BOOL WSPAPI wsp_get_qos_by_name(SOCKET s, LPWSABUF name, void *qos, LPINT error)
{
    *error = WSAEINVAL;
    return FALSE;
}

/* the in and out buffer of the control ws2_32's WSAPoll sends to the base provider. */
typedef struct {
    INT Result;
    ULONG Count;
    INT Timeout;
    ULONG Pad;
    WSAPOLLFD Fds[];
} WS_POLL_REQUEST;

#define SIO_WS2_POLL 0xC800001F

/* the input of SIO_EXT_SENDMSG, the arguments ws2_32's WSASendMsg got after the socket. */
typedef struct {
    LPWSAMSG Msg;
    DWORD Flags;
    LPDWORD Sent;
    LPWSAOVERLAPPED Overlapped;
    LPWSAOVERLAPPED_COMPLETION_ROUTINE Routine;
} WS_SENDMSG_REQUEST;

int WSAAPI WSASendMsg(SOCKET s, LPWSAMSG msg, DWORD flags, LPDWORD sent, LPWSAOVERLAPPED overlapped,
                      LPWSAOVERLAPPED_COMPLETION_ROUTINE routine);

static int WSPAPI wsp_ioctl(SOCKET s, DWORD code, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size,
                            LPDWORD returned, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine,
                            LPWSATHREADID thread, LPINT error)
{
    if (code == SIO_WS2_POLL) {
        WS_POLL_REQUEST *p = in;
        if (!p || in_size < sizeof(*p) || in_size < sizeof(*p) + (SIZE_T)p->Count * sizeof(WSAPOLLFD)) {
            *error = WSAEFAULT;
            return SOCKET_ERROR;
        }
        p->Result = WSAPoll(p->Fds, p->Count, p->Timeout);
        if (p->Result == SOCKET_ERROR) {
            *error = (int)GetLastError();
            return SOCKET_ERROR;
        }
        if (returned)
            *returned = in_size;
        return 0;
    }
    /*
     * ws2_32's WSASendMsg is this control on the base provider. read from WSPIoctl of mswsock 26100: the output is
     * zeroed first, a missing input is WSAEFAULT, and a failed send is not a failed control, its error is the output
     * and the control succeeds with 4 bytes.
     */
    if (code == SIO_EXT_SENDMSG) {
        WS_SENDMSG_REQUEST *m = in;
        *(INT *)out = 0;
        if (!m) {
            *error = WSAEFAULT;
            return SOCKET_ERROR;
        }
        if (WSASendMsg(s, m->Msg, m->Flags, m->Sent, m->Overlapped, m->Routine))
            *(INT *)out = (INT)GetLastError();
        *returned = sizeof(INT);
        *error = 0;
        return 0;
    }
    return result(WSAIoctl(s, code, in, in_size, out, out_size, returned, overlapped, routine), error);
}

static SOCKET WSPAPI wsp_join_leaf(SOCKET s, const struct sockaddr *name, int namelen, LPWSABUF caller_data,
                                   LPWSABUF callee_data, void *sqos, void *gqos, DWORD flags, LPINT error)
{
    return socket_result(WSAJoinLeaf(s, name, namelen, caller_data, callee_data, sqos, gqos, flags), error);
}

static int WSPAPI wsp_listen(SOCKET s, int backlog, LPINT error)
{
    return result(listen(s, backlog), error);
}

static int WSPAPI wsp_recv(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags,
                           LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine,
                           LPWSATHREADID thread, LPINT error)
{
    return result(WSARecv(s, buffers, count, received, flags, overlapped, routine), error);
}

/* a stream carries no disconnect data. */
static int WSPAPI wsp_recv_disconnect(SOCKET s, LPWSABUF data, LPINT error)
{
    if (data)
        data->len = 0;
    return result(shutdown(s, SD_RECEIVE), error);
}

static int WSPAPI wsp_recv_from(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags,
                                struct sockaddr *from, LPINT fromlen, LPWSAOVERLAPPED overlapped,
                                LPWSAOVERLAPPED_COMPLETION_ROUTINE routine, LPWSATHREADID thread, LPINT error)
{
    return result(WSARecvFrom(s, buffers, count, received, flags, from, fromlen, overlapped, routine), error);
}

static int WSPAPI wsp_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                             const struct timeval *timeout, LPINT error)
{
    return result(select(nfds, readfds, writefds, exceptfds, timeout), error);
}

static int WSPAPI wsp_send(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags,
                           LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine,
                           LPWSATHREADID thread, LPINT error)
{
    return result(WSASend(s, buffers, count, sent, flags, overlapped, routine), error);
}

static int WSPAPI wsp_send_disconnect(SOCKET s, LPWSABUF data, LPINT error)
{
    return result(WSASendDisconnect(s, data), error);
}

static int WSPAPI wsp_send_to(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags,
                              const struct sockaddr *to, int tolen, LPWSAOVERLAPPED overlapped,
                              LPWSAOVERLAPPED_COMPLETION_ROUTINE routine, LPWSATHREADID thread, LPINT error)
{
    return result(WSASendTo(s, buffers, count, sent, flags, to, tolen, overlapped, routine), error);
}

static int WSPAPI wsp_set_sock_opt(SOCKET s, int level, int name, const char *value, int length, LPINT error)
{
    return result(setsockopt(s, level, name, value, length), error);
}

static int WSPAPI wsp_shutdown(SOCKET s, int how, LPINT error)
{
    return result(shutdown(s, how), error);
}

static SOCKET WSPAPI wsp_socket(int af, int type, int protocol, LPWSAPROTOCOL_INFOW info, GROUP g, DWORD flags,
                                LPINT error)
{
    if (info) {
        EnterCriticalSection(&catalog_lock);
        add_entry(info);
        LeaveCriticalSection(&catalog_lock);
    }
    return socket_result(WSASocketW(af, type, protocol, info, g, flags), error);
}

static INT WSPAPI wsp_string_to_address(LPWSTR string, INT family, LPWSAPROTOCOL_INFOW info, LPSOCKADDR address,
                                        LPINT length, LPINT error)
{
    return result(WSAStringToAddressW(string, family, info, address, length), error);
}

int WSPAPI WSPStartup(WORD version, LPWSPDATA data, LPWSAPROTOCOL_INFOW info, WSPUPCALLTABLE upcall_table,
                      LPWSPPROC_TABLE procs)
{
    if (!data || !procs)
        return WSAEFAULT;
    if (version < MAKEWORD(2, 2))
        return WSAVERNOTSUPPORTED;
    data->wVersion = MAKEWORD(2, 2);
    data->wHighVersion = MAKEWORD(2, 2);
    WsWiden("Microsoft Windows Sockets Version 2.", data->szDescription, RTL_NUMBER_OF(data->szDescription));
    EnterCriticalSection(&catalog_lock);
    upcalls = upcall_table;
    if (info)
        add_entry(info);
    WsCatalogEntry = catalog_entry;
    WsAnnounceSocket = announce_socket;
    LeaveCriticalSection(&catalog_lock);
    InterlockedIncrement(&started);
    procs->lpWSPAccept = (PVOID)wsp_accept;
    procs->lpWSPAddressToString = (PVOID)wsp_address_to_string;
    procs->lpWSPAsyncSelect = (PVOID)wsp_async_select;
    procs->lpWSPBind = (PVOID)wsp_bind;
    procs->lpWSPCancelBlockingCall = (PVOID)wsp_cancel_blocking_call;
    procs->lpWSPCleanup = (PVOID)wsp_cleanup;
    procs->lpWSPCloseSocket = (PVOID)wsp_close_socket;
    procs->lpWSPConnect = (PVOID)wsp_connect;
    procs->lpWSPDuplicateSocket = (PVOID)wsp_duplicate_socket;
    procs->lpWSPEnumNetworkEvents = (PVOID)wsp_enum_network_events;
    procs->lpWSPEventSelect = (PVOID)wsp_event_select;
    procs->lpWSPGetOverlappedResult = (PVOID)wsp_get_overlapped_result;
    procs->lpWSPGetPeerName = (PVOID)wsp_get_peer_name;
    procs->lpWSPGetSockName = (PVOID)wsp_get_sock_name;
    procs->lpWSPGetSockOpt = (PVOID)wsp_get_sock_opt;
    procs->lpWSPGetQOSByName = (PVOID)wsp_get_qos_by_name;
    procs->lpWSPIoctl = (PVOID)wsp_ioctl;
    procs->lpWSPJoinLeaf = (PVOID)wsp_join_leaf;
    procs->lpWSPListen = (PVOID)wsp_listen;
    procs->lpWSPRecv = (PVOID)wsp_recv;
    procs->lpWSPRecvDisconnect = (PVOID)wsp_recv_disconnect;
    procs->lpWSPRecvFrom = (PVOID)wsp_recv_from;
    procs->lpWSPSelect = (PVOID)wsp_select;
    procs->lpWSPSend = (PVOID)wsp_send;
    procs->lpWSPSendDisconnect = (PVOID)wsp_send_disconnect;
    procs->lpWSPSendTo = (PVOID)wsp_send_to;
    procs->lpWSPSetSockOpt = (PVOID)wsp_set_sock_opt;
    procs->lpWSPShutdown = (PVOID)wsp_shutdown;
    procs->lpWSPSocket = (PVOID)wsp_socket;
    procs->lpWSPStringToAddress = (PVOID)wsp_string_to_address;
    return 0;
}
#endif
