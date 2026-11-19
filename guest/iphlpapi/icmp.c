#include "private.h"

typedef struct _TP_IO *PTP_IO;
typedef void(CALLBACK *ICMP_IO_CALLBACK)(PVOID instance, PVOID context, PVOID overlapped, ULONG result,
                                         ULONG_PTR bytes, PTP_IO io);
WINBASEAPI PTP_IO WINAPI CreateThreadpoolIo(HANDLE file, ICMP_IO_CALLBACK callback, PVOID context, PVOID environment);
WINBASEAPI void WINAPI StartThreadpoolIo(PTP_IO io);
WINBASEAPI void WINAPI CancelThreadpoolIo(PTP_IO io);

/*
 * windows 11 keeps a handle of IcmpCreateFile in the heap: the process, the family and the count of echoes still
 * running, which IcmpCloseHandle waits for. an echo goes to tcpip through nsi and completes on a thread of nsi's
 * notifications, which fills the reply, then sets the caller's event or queues the caller's apc to the thread that
 * sent it. here the echo is a request of the socket driver whose completion reaches the thread pool.
 */
typedef struct ICMP_FILE {
    DWORD process;
    BOOLEAN ipv4;
    LONG pending;
} ICMP_FILE;

typedef struct ICMP_ECHO {
    OVERLAPPED overlapped;
    ICMP_FILE *file;
    HANDLE event;
    HANDLE thread;
    PIO_APC_ROUTINE_ICMP apc;
    PVOID apc_context;
    IO_STATUS_BLOCK iosb; /* what the apc receives. */
    PUCHAR reply;
    MIME_AFD_ECHO request;
    MIME_AFD_ECHO_RESULT result;
} ICMP_ECHO;

static SRWLOCK echo_lock = SRWLOCK_INIT;
static HANDLE echo_device;
static PTP_IO echo_io;

static void CALLBACK echo_done(PVOID instance, PVOID context, PVOID overlapped, ULONG result, ULONG_PTR bytes,
                               PTP_IO io);

static NTSTATUS echo_open(void)
{
    NTSTATUS status = STATUS_SUCCESS;
    AcquireSRWLockExclusive(&echo_lock);
    if (!echo_io) {
        UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_AFD_DEVICE);
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &name, 0, nullptr, nullptr);
        IO_STATUS_BLOCK io;
        HANDLE h;
        status = NtCreateFile(&h, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io, nullptr, 0,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, nullptr, 0);
        if (NT_SUCCESS(status)) {
            echo_io = CreateThreadpoolIo(h, echo_done, nullptr, nullptr);
            if (echo_io) {
                echo_device = h;
            } else {
                status = STATUS_INSUFFICIENT_RESOURCES;
                NtClose(h);
            }
        }
    }
    ReleaseSRWLockExclusive(&echo_lock);
    return status;
}

static HANDLE icmp_create(BOOLEAN ipv4)
{
    ICMP_FILE *f = IphAlloc(sizeof(*f));
    if (!f)
        return INVALID_HANDLE_VALUE;
    NTSTATUS status = echo_open();
    if (!NT_SUCCESS(status)) {
        IphFree(f);
        SetLastError(RtlNtStatusToDosError(status));
        return INVALID_HANDLE_VALUE;
    }
    f->process = GetCurrentProcessId();
    f->ipv4 = ipv4;
    /* the connection to nsi windows makes for every handle leaves the last error at 0. */
    SetLastError(ERROR_SUCCESS);
    return f;
}

HANDLE WINAPI IcmpCreateFile(void)
{
    return icmp_create(TRUE);
}

HANDLE WINAPI Icmp6CreateFile(void)
{
    return icmp_create(FALSE);
}

static BOOL valid(HANDLE handle)
{
    return handle && handle != INVALID_HANDLE_VALUE;
}

BOOL WINAPI IcmpCloseHandle(HANDLE handle)
{
    if (!valid(handle)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ICMP_FILE *f = handle;
    while (InterlockedCompareExchange(&f->pending, 0, 0))
        Sleep(10);
    IphFree(f);
    return TRUE;
}

/* NtStatusToIpStatus of windows, with the icmp error tables of each family. */
static ULONG icmp4_status(UCHAR type, UCHAR code)
{
    switch (type) {
    case 3:
        switch (code) {
        case 1:
        case 7:
        case 10:
        case 12:
            return IP_DEST_HOST_UNREACHABLE;
        case 2:
            return IP_DEST_PROT_UNREACHABLE;
        case 3:
            return IP_DEST_PORT_UNREACHABLE;
        case 4:
            return IP_PACKET_TOO_BIG;
        case 5:
            return IP_BAD_ROUTE;
        }
        return IP_DEST_NET_UNREACHABLE;
    case 4:
        return IP_SOURCE_QUENCH;
    case 11:
        return code == 0 ? IP_TTL_EXPIRED_TRANSIT : code == 1 ? IP_TTL_EXPIRED_REASSEM : IP_GENERAL_FAILURE;
    case 12:
        return IP_PARAM_PROBLEM;
    }
    return IP_GENERAL_FAILURE;
}

static ULONG icmp6_status(UCHAR type, UCHAR code)
{
    switch (type) {
    case 1:
        return code == 2 ? IP_DEST_SCOPE_MISMATCH
               : code == 3 ? IP_DEST_HOST_UNREACHABLE
               : code == 4 ? IP_DEST_PORT_UNREACHABLE
                           : IP_DEST_NET_UNREACHABLE;
    case 3:
        return code == 0 ? IP_TTL_EXPIRED_TRANSIT : code == 1 ? IP_TTL_EXPIRED_REASSEM : IP_GENERAL_FAILURE;
    case 4:
        return IP_PARAM_PROBLEM;
    }
    return IP_GENERAL_FAILURE;
}

static ULONG ip_status(NTSTATUS status, const MIME_AFD_ECHO_RESULT *r, BOOLEAN ipv4)
{
    switch ((ULONG)status) {
    case 0:
        return IP_SUCCESS;
    case 0xC0000001:
        if (!r->Error)
            return IP_GENERAL_FAILURE;
        return ipv4 ? icmp4_status(r->Type, r->Code) : icmp6_status(r->Type, r->Code);
    case 0xC000009A:
        return IP_NO_RESOURCES;
    case 0xC00000BD:
        return IP_STATUS_BASE + 29;
    case 0xC0000206:
        return IP_PACKET_TOO_BIG;
    case 0xC000A010:
        return IP_STATUS_BASE + 32;
    case 0x102:
        return IP_REQ_TIMED_OUT;
    }
    return IP_GENERAL_FAILURE;
}

static void CALLBACK echo_apc(ULONG_PTR parameter)
{
    ICMP_ECHO *e = (ICMP_ECHO *)parameter;
    e->apc(e->apc_context, &e->iosb, 0);
    CloseHandle(e->thread);
    IphFree(e);
}

static void CALLBACK echo_done(PVOID instance, PVOID context, PVOID overlapped, ULONG result, ULONG_PTR bytes,
                               PTP_IO io)
{
    ICMP_ECHO *e = CONTAINING_RECORD(overlapped, ICMP_ECHO, overlapped);
    NTSTATUS status = (NTSTATUS)e->overlapped.Internal;
    BOOLEAN answered = status == STATUS_SUCCESS || (status == STATUS_UNSUCCESSFUL && e->result.Error);
    ULONG size;
    if (e->file->ipv4) {
        ICMP_ECHO_REPLY *r = (ICMP_ECHO_REPLY *)e->reply;
        memset(r, 0, sizeof(*r));
        size = sizeof(*r);
        if (answered) {
            memcpy(&r->Address, e->result.From.Data + 2, 4);
            r->RoundTripTime = e->result.RoundTrip;
            r->DataSize = (USHORT)e->result.DataSize;
            r->Reserved = 1;
            r->Data = e->reply + sizeof(*r);
            r->Options.Ttl = e->result.Ttl;
            r->Options.Tos = e->result.Tos;
            r->Options.OptionsData = e->reply + sizeof(*r) + r->DataSize;
            size += r->DataSize;
        }
        r->Status = ip_status(status, &e->result, TRUE);
    } else {
        ICMPV6_ECHO_REPLY *r = (ICMPV6_ECHO_REPLY *)e->reply;
        memset(r, 0, sizeof(*r));
        size = sizeof(*r);
        if (answered) {
            memcpy(r->Address.sin6_addr, e->result.From.Data + 6, 16);
            r->RoundTripTime = e->result.RoundTrip;
            size += e->result.DataSize;
        }
        r->Status = ip_status(status, &e->result, FALSE);
    }
    e->iosb.Status = status;
    e->iosb.Information = size;
    InterlockedDecrement(&e->file->pending);
    if (e->event) {
        SetEvent(e->event);
        IphFree(e);
    } else if (!QueueUserAPC(echo_apc, e->thread, (ULONG_PTR)e)) {
        CloseHandle(e->thread);
        IphFree(e);
    }
}

static void address(MIME_AFD_ADDRESS *out, const SOCKADDR_INET *in)
{
    memset(out, 0, sizeof(*out));
    memcpy(out, in, in->si_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
}

static DWORD send_echo(HANDLE handle, BOOLEAN ipv4, HANDLE event, PIO_APC_ROUTINE_ICMP apc, PVOID apc_context,
                       const SOCKADDR_INET *source, const SOCKADDR_INET *destination, LPVOID request,
                       WORD request_size, PIP_OPTION_INFORMATION options, LPVOID reply, DWORD reply_size,
                       DWORD timeout)
{
    ULONG header = ipv4 ? sizeof(ICMP_ECHO_REPLY) : sizeof(ICMPV6_ECHO_REPLY);
    if (!valid(handle) || ((ICMP_FILE *)handle)->ipv4 != ipv4 || reply_size < header || !reply) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ICMP_ECHO *e = IphAlloc(sizeof(*e));
    if (!e) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    HANDLE own = nullptr;
    if (event) {
        e->event = event;
    } else if (apc) {
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &e->thread, 0, FALSE,
                             DUPLICATE_SAME_ACCESS)) {
            IphFree(e);
            return 0;
        }
        e->apc = apc;
        e->apc_context = apc_context;
    } else {
        own = e->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!own) {
            IphFree(e);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
    }
    e->file = handle;
    e->reply = reply;
    address(&e->request.Destination, destination);
    if (source)
        address(&e->request.Source, source);
    e->request.Request = (ULONG64)(ULONG_PTR)request;
    e->request.RequestSize = request_size;
    e->request.Timeout = timeout;
    e->request.Reply = (ULONG64)(ULONG_PTR)reply + header;
    e->request.ReplySize = reply_size - header;
    /* windows asks for a ttl of 255 without options, and only the ipv4 echo looks at the flags. */
    e->request.Ttl = options ? options->Ttl : 255;
    if (ipv4 && options && (options->Flags & IP_FLAG_DF))
        e->request.Flags = MIME_AFD_ECHO_DONT_FRAGMENT;
    InterlockedIncrement(&e->file->pending);
    StartThreadpoolIo(echo_io);
    if (!DeviceIoControl(echo_device, IOCTL_AFD_ECHO, &e->request, sizeof(e->request), &e->result,
                         sizeof(e->result), nullptr, &e->overlapped) &&
        GetLastError() != ERROR_IO_PENDING) {
        DWORD error = GetLastError();
        CancelThreadpoolIo(echo_io);
        InterlockedDecrement(&e->file->pending);
        if (own)
            CloseHandle(own);
        if (e->thread)
            CloseHandle(e->thread);
        IphFree(e);
        SetLastError(error);
        return 0;
    }
    if (!own) {
        SetLastError(ERROR_IO_PENDING);
        return 0;
    }
    WaitForSingleObject(own, INFINITE);
    CloseHandle(own);
    /* nsi leaves the last error at 0 when it took the echo, only a failed one sets it. */
    SetLastError(ERROR_SUCCESS);
    if (ipv4) {
        const ICMP_ECHO_REPLY *r = reply;
        return IcmpParseReplies(reply, r->DataSize + sizeof(*r) + r->Options.OptionsSize);
    }
    ULONG status = ((const ICMPV6_ECHO_REPLY *)reply)->Status;
    if (status == IP_SUCCESS || status == IP_TTL_EXPIRED_TRANSIT)
        return 1;
    SetLastError(status);
    return 0;
}

static void ipv4_of(SOCKADDR_INET *out, IPAddr address)
{
    memset(out, 0, sizeof(*out));
    out->Ipv4.sin_family = AF_INET;
    memcpy(&out->Ipv4.sin_addr, &address, 4);
}

DWORD WINAPI IcmpSendEcho2Ex(HANDLE handle, HANDLE event, PIO_APC_ROUTINE_ICMP apc, PVOID context, IPAddr source,
                             IPAddr destination, LPVOID request, WORD request_size, PIP_OPTION_INFORMATION options,
                             LPVOID reply, DWORD reply_size, DWORD timeout)
{
    SOCKADDR_INET from, to;
    ipv4_of(&from, source);
    ipv4_of(&to, destination);
    return send_echo(handle, TRUE, event, apc, context, source ? &from : nullptr, &to, request, request_size, options,
                     reply, reply_size, timeout);
}

DWORD WINAPI IcmpSendEcho2(HANDLE handle, HANDLE event, PIO_APC_ROUTINE_ICMP apc, PVOID context, IPAddr destination,
                           LPVOID request, WORD request_size, PIP_OPTION_INFORMATION options, LPVOID reply,
                           DWORD reply_size, DWORD timeout)
{
    return IcmpSendEcho2Ex(handle, event, apc, context, 0, destination, request, request_size, options, reply,
                           reply_size, timeout);
}

DWORD WINAPI IcmpSendEcho(HANDLE handle, IPAddr destination, LPVOID request, WORD request_size,
                          PIP_OPTION_INFORMATION options, LPVOID reply, DWORD reply_size, DWORD timeout)
{
    return IcmpSendEcho2Ex(handle, nullptr, nullptr, nullptr, 0, destination, request, request_size, options, reply,
                           reply_size, timeout);
}

DWORD WINAPI Icmp6SendEcho2(HANDLE handle, HANDLE event, PIO_APC_ROUTINE_ICMP apc, PVOID context,
                            struct sockaddr_in6 *source, struct sockaddr_in6 *destination, LPVOID request,
                            WORD request_size, PIP_OPTION_INFORMATION options, LPVOID reply, DWORD reply_size,
                            DWORD timeout)
{
    SOCKADDR_INET from, to;
    memset(&from, 0, sizeof(from));
    memset(&to, 0, sizeof(to));
    /* windows takes the address and the zone of both and nothing else. */
    to.Ipv6.sin6_family = AF_INET6;
    if (destination) {
        to.Ipv6.sin6_addr = destination->sin6_addr;
        to.Ipv6.sin6_scope_id = destination->sin6_scope_id;
    }
    BOOL any = TRUE;
    if (source) {
        from.Ipv6.sin6_family = AF_INET6;
        from.Ipv6.sin6_addr = source->sin6_addr;
        from.Ipv6.sin6_scope_id = source->sin6_scope_id;
        for (int i = 0; i < 16; i++)
            any &= source->sin6_addr.u.Byte[i] == 0;
    }
    return send_echo(handle, FALSE, event, apc, context, any ? nullptr : &from, &to, request, request_size, options,
                     reply, reply_size, timeout);
}

DWORD WINAPI IcmpParseReplies(LPVOID reply, DWORD reply_size)
{
    if (!reply_size || !reply) {
        SetLastError(ERROR_GEN_FAILURE);
        return 0;
    }
    ICMP_ECHO_REPLY *r = reply;
    DWORD count = r->Reserved;
    r->Reserved = 0;
    if (!count)
        SetLastError(r->Status);
    return count;
}

DWORD WINAPI Icmp6ParseReplies(LPVOID reply, DWORD reply_size)
{
    DWORD status = ERROR_GEN_FAILURE;
    if (reply && reply_size) {
        status = ((const ICMPV6_ECHO_REPLY *)reply)->Status;
        if (status == IP_SUCCESS || status == IP_TTL_EXPIRED_TRANSIT)
            return 1;
    }
    SetLastError(status);
    return 0;
}
