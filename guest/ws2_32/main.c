/*
 * ws2_32 entry point, WSAStartup and WSACleanup, the last error, byte
 * order and the per thread buffers.
 */
#include "ws2_32_private.h"

static CRITICAL_SECTION startup_lock;
static LONG startup_count;
static DWORD thread_slot = TLS_OUT_OF_INDEXES;

static void free_thread(void)
{
    if (thread_slot == TLS_OUT_OF_INDEXES)
        return;
    WsFree(TlsGetValue(thread_slot));
    TlsSetValue(thread_slot, NULL);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        InitializeCriticalSection(&startup_lock);
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
        return NULL;
    WS_THREAD *t = TlsGetValue(thread_slot);
    if (!t) {
        t = WsAllocZero(sizeof(*t));
        if (t)
            TlsSetValue(thread_slot, t);
    }
    return t;
}

int WsCheckStarted(void)
{
    return InterlockedCompareExchange(&startup_count, 0, 0) > 0 ? 0 : WSANOTINITIALISED;
}

/* ------------------------------------------------------------------ startup */

static void copy_bounded(char *dst, SIZE_T size, const char *src)
{
    SIZE_T n = strlen(src);
    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

int WSAAPI WSAStartup(WORD version, LPWSADATA data)
{
    BYTE major = LOBYTE(version), minor = HIBYTE(version);
    WORD granted;
    int result = 0;
    if (!data)
        return WSAEFAULT;
    if (major == 0) {
        result = WSAVERNOTSUPPORTED;
        major = 2;
        granted = MAKEWORD(2, 2);
    } else if (major == 1) {
        granted = MAKEWORD(1, minor ? 1 : 0);
    } else if (major == 2) {
        granted = MAKEWORD(2, minor > 2 ? 2 : minor);
    } else {
        major = 2;
        granted = MAKEWORD(2, 2);
    }
    data->wVersion = granted;
    data->wHighVersion = MAKEWORD(2, 2);
    /* winsock 1 programs are told the historical limits, winsock 2 has none. */
    data->iMaxSockets = major == 1 ? 32767 : 0;
    data->iMaxUdpDg = major == 1 ? 65467 : 0;
    data->lpVendorInfo = NULL;
    copy_bounded(data->szDescription, sizeof(data->szDescription), "WinSock 2.0");
    copy_bounded(data->szSystemStatus, sizeof(data->szSystemStatus), "Running");
    if (result)
        return result;
    EnterCriticalSection(&startup_lock);
    startup_count++;
    LeaveCriticalSection(&startup_lock);
    return 0;
}

int WSAAPI WSACleanup(void)
{
    EnterCriticalSection(&startup_lock);
    if (startup_count <= 0) {
        LeaveCriticalSection(&startup_lock);
        return WsFail(WSANOTINITIALISED);
    }
    if (--startup_count == 0)
        WsCloseAllSockets();
    LeaveCriticalSection(&startup_lock);
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

/* ------------------------------------------------------------------ byte order */

u_long WSAAPI htonl(u_long v)
{
    return __builtin_bswap32(v);
}

u_short WSAAPI htons(u_short v)
{
    return __builtin_bswap16(v);
}

u_long WSAAPI ntohl(u_long v)
{
    return __builtin_bswap32(v);
}

u_short WSAAPI ntohs(u_short v)
{
    return __builtin_bswap16(v);
}

/* the WSA forms check the socket first, every provider is little endian. */
static int check_order_call(SOCKET s, void *out)
{
    int error = WsCheckStarted();
    if (!error && !WsIsSocket(s))
        error = WSAENOTSOCK;
    if (!error && !out)
        error = WSAEFAULT;
    if (error)
        SetLastError(error);
    return error;
}

int WSAAPI WSAHtonl(SOCKET s, u_long v, u_long *out)
{
    if (check_order_call(s, out))
        return SOCKET_ERROR;
    *out = htonl(v);
    return 0;
}

int WSAAPI WSAHtons(SOCKET s, u_short v, u_short *out)
{
    if (check_order_call(s, out))
        return SOCKET_ERROR;
    *out = htons(v);
    return 0;
}

int WSAAPI WSANtohl(SOCKET s, u_long v, u_long *out)
{
    if (check_order_call(s, out))
        return SOCKET_ERROR;
    *out = ntohl(v);
    return 0;
}

int WSAAPI WSANtohs(SOCKET s, u_short v, u_short *out)
{
    if (check_order_call(s, out))
        return SOCKET_ERROR;
    *out = ntohs(v);
    return 0;
}

/* ------------------------------------------------------------------ fd sets and events */

int WSAAPI __WSAFDIsSet(SOCKET fd, fd_set *set)
{
    if (!set)
        return 0;
    for (u_int i = 0; i < set->fd_count; i++) {
        if (set->fd_array[i] == fd)
            return 1;
    }
    return 0;
}

WSAEVENT WSAAPI WSACreateEvent(void)
{
    return CreateEventA(NULL, TRUE, FALSE, NULL);
}

BOOL WSAAPI WSACloseEvent(WSAEVENT event)
{
    return CloseHandle(event);
}

BOOL WSAAPI WSASetEvent(WSAEVENT event)
{
    return SetEvent(event);
}

BOOL WSAAPI WSAResetEvent(WSAEVENT event)
{
    return ResetEvent(event);
}

DWORD WSAAPI WSAWaitForMultipleEvents(DWORD count, const WSAEVENT *events, BOOL all, DWORD timeout, BOOL alertable)
{
    return WaitForMultipleObjectsEx(count, events, all, timeout, alertable);
}

/* ------------------------------------------------------------------ winsock 1 blocking hooks */

/* winsock 2 has no blocking hooks: blocking calls are never "in progress" for these. */
BOOL WSAAPI WSAIsBlocking(void)
{
    return FALSE;
}

int WSAAPI WSACancelBlockingCall(void)
{
    return WsFail(WSAEOPNOTSUPP);
}

FARPROC WSAAPI WSASetBlockingHook(FARPROC hook)
{
    SetLastError(WSAEOPNOTSUPP);
    return NULL;
}

int WSAAPI WSAUnhookBlockingHook(void)
{
    return WsFail(WSAEOPNOTSUPP);
}
