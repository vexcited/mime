/*
 * the socket driver, \Device\Afd: a socket is a file of this device bound
 * to a socket of the host. ws2_32 drives it with these private device
 * controls; sends, receives, connects and accepts can stay pending and
 * complete like any other overlapped i/o, through the status block, the
 * event, an apc or the completion port of the file.
 */
#ifndef MIME_AFD_H
#define MIME_AFD_H

#include <mime/nt.h>

#define MIME_AFD_DEVICE L"\\Device\\Afd"

/* FILE_DEVICE_NETWORK, METHOD_NEITHER: the driver reads the caller's buffers itself. */
#define MIME_AFD_IOCTL(n) (((ULONG)0x12 << 16) | ((ULONG)(n) << 2) | 3)

#define IOCTL_AFD_CREATE MIME_AFD_IOCTL(0x800)        /* in MIME_AFD_CREATE */
#define IOCTL_AFD_BIND MIME_AFD_IOCTL(0x801)          /* in MIME_AFD_ADDRESS */
#define IOCTL_AFD_LISTEN MIME_AFD_IOCTL(0x802)        /* in ULONG backlog */
#define IOCTL_AFD_CONNECT MIME_AFD_IOCTL(0x803)       /* in MIME_AFD_CONNECT, pends, information is the bytes sent */
#define IOCTL_AFD_ACCEPT MIME_AFD_IOCTL(0x804)        /* in MIME_AFD_ACCEPT, pends, information is the bytes received */
#define IOCTL_AFD_SEND MIME_AFD_IOCTL(0x805)          /* in MIME_AFD_TRANSFER, pends */
#define IOCTL_AFD_RECV MIME_AFD_IOCTL(0x806)          /* in MIME_AFD_TRANSFER, pends */
#define IOCTL_AFD_SHUTDOWN MIME_AFD_IOCTL(0x807)      /* in ULONG SD_* */
#define IOCTL_AFD_NAME MIME_AFD_IOCTL(0x808)          /* in ULONG peer, out MIME_AFD_ADDRESS */
#define IOCTL_AFD_OPTION MIME_AFD_IOCTL(0x809)        /* in MIME_AFD_OPTION, out ULONG value */
#define IOCTL_AFD_POLL MIME_AFD_IOCTL(0x80A)          /* in and out MIME_AFD_POLL, waits for its timeout */
#define IOCTL_AFD_EVENT_SELECT MIME_AFD_IOCTL(0x80B)  /* in MIME_AFD_EVENT_SELECT */
#define IOCTL_AFD_ENUM_EVENTS MIME_AFD_IOCTL(0x80C)   /* in HANDLE event to reset or NULL, out MIME_AFD_EVENTS */
#define IOCTL_AFD_STATE MIME_AFD_IOCTL(0x80D)         /* out MIME_AFD_STATE */
#define IOCTL_AFD_RESOLVE MIME_AFD_IOCTL(0x80E)       /* in MIME_AFD_RESOLVE, out MIME_AFD_ADDRESS list */
#define IOCTL_AFD_INTERFACES MIME_AFD_IOCTL(0x80F)    /* out MIME_AFD_INTERFACE list */
#define IOCTL_AFD_COMPLETE MIME_AFD_IOCTL(0x810)      /* in ULONG64 information, completes at once like a request */

/* a windows sockaddr_in or sockaddr_in6, the host's MimeNetAddr. */
typedef struct MIME_AFD_ADDRESS {
    USHORT Family;
    UCHAR Data[26];
} MIME_AFD_ADDRESS;

typedef struct MIME_AFD_CREATE {
    ULONG Family;
    ULONG Type;
    ULONG Protocol;
} MIME_AFD_CREATE;

/* the caller's WSABUF, the same on every 64 bit architecture. */
typedef struct MIME_AFD_BUFFER {
    ULONG Length;
    ULONG Pad;
    ULONG64 Data;
} MIME_AFD_BUFFER;

#define MIME_AFD_OVERLAPPED 0x1 /* the request may pend even on a non blocking socket */

typedef struct MIME_AFD_CONNECT {
    MIME_AFD_ADDRESS Address;
    ULONG AfdFlags;
    ULONG Length; /* ConnectEx data sent once connected */
    ULONG64 Buffer;
} MIME_AFD_CONNECT;

typedef struct MIME_AFD_ACCEPT {
    ULONG64 Socket; /* the handle of an unconnected socket that takes the connection */
    ULONG AfdFlags;
    ULONG Length;   /* AcceptEx data received once accepted */
    ULONG64 Buffer;
    ULONG64 Addresses; /* MIME_AFD_ADDRESS local then remote, written at completion */
} MIME_AFD_ACCEPT;

typedef struct MIME_AFD_TRANSFER {
    ULONG64 Buffers; /* MIME_AFD_BUFFER array, captured when the request starts */
    ULONG BufferCount;
    ULONG Flags;    /* MSG_* */
    ULONG AfdFlags;
    ULONG Pad;
    ULONG64 Address;       /* send: MIME_AFD_ADDRESS to send to; receive: the caller's sockaddr */
    ULONG64 AddressLength; /* receive: the caller's INT length, both read when starting and written */
} MIME_AFD_TRANSFER;

/* the host's MIME_NET_OPT_* values. */
#define MIME_AFD_OPT_REUSEADDR 1
#define MIME_AFD_OPT_KEEPALIVE 2
#define MIME_AFD_OPT_NODELAY 3
#define MIME_AFD_OPT_SNDBUF 4
#define MIME_AFD_OPT_RCVBUF 5
#define MIME_AFD_OPT_BROADCAST 6
#define MIME_AFD_OPT_V6ONLY 7
#define MIME_AFD_OPT_LINGER 8 /* on in bit 16, seconds below */
#define MIME_AFD_OPT_ERROR 9
#define MIME_AFD_OPT_OOBINLINE 10
#define MIME_AFD_OPT_TTL 11
#define MIME_AFD_OPT_NREAD 12
#define MIME_AFD_OPT_KEEPIDLE 13
#define MIME_AFD_OPT_KEEPINTVL 14
#define MIME_AFD_OPT_KEEPCNT 15
#define MIME_AFD_OPT_ATMARK 16
#define MIME_AFD_OPTION_NONBLOCKING 0x100 /* the FIONBIO mode, kept by the driver */

typedef struct MIME_AFD_OPTION {
    ULONG Option; /* MIME_NET_OPT_* or MIME_AFD_OPTION_* */
    ULONG Set;
    ULONG Value;
} MIME_AFD_OPTION;

#define MIME_AFD_POLL_READ 0x01    /* data, a connection to accept, or the end of the stream */
#define MIME_AFD_POLL_WRITE 0x02   /* room to send, or a connect that succeeded */
#define MIME_AFD_POLL_EXCEPT 0x04  /* a connect that failed */
#define MIME_AFD_POLL_HUP 0x08     /* the stream ended or broke */
#define MIME_AFD_POLL_ERROR 0x10   /* the connection broke or never formed */
#define MIME_AFD_POLL_INVALID 0x20 /* not a socket */
#define MIME_AFD_POLL_DATA 0x40    /* bytes or a connection are waiting, not only the end */

typedef struct MIME_AFD_POLL_ENTRY {
    ULONG64 Socket;
    ULONG Events;  /* asked */
    ULONG Results; /* observed, always with HUP, ERROR and INVALID */
} MIME_AFD_POLL_ENTRY;

typedef struct MIME_AFD_POLL {
    LONGLONG Timeout; /* milliseconds, negative waits forever */
    ULONG Count;
    ULONG Ready;
    MIME_AFD_POLL_ENTRY Entries[];
} MIME_AFD_POLL;

typedef struct MIME_AFD_EVENT_SELECT {
    ULONG64 Event;
    ULONG Mask; /* FD_* */
    ULONG Pad;
} MIME_AFD_EVENT_SELECT;

typedef struct MIME_AFD_EVENTS {
    ULONG Events;
    LONG Errors[10];
} MIME_AFD_EVENTS;

#define MIME_AFD_STATE_OPEN 0
#define MIME_AFD_STATE_CONNECTING 1
#define MIME_AFD_STATE_CONNECTED 2
#define MIME_AFD_STATE_LISTENING 3

typedef struct MIME_AFD_STATE {
    ULONG Family;
    ULONG Type;
    ULONG Protocol;
    ULONG State;
    ULONG Bound;
    ULONG NonBlocking;
    ULONG SelectMask;
    ULONG ShutFlags; /* 1 receive, 2 send */
    ULONG64 ConnectTime; /* interrupt time of the connection, 0 before */
} MIME_AFD_STATE;

typedef struct MIME_AFD_RESOLVE {
    ULONG Family; /* AF_UNSPEC, AF_INET or AF_INET6 */
    ULONG Length;
    CHAR Name[256]; /* utf-8 */
} MIME_AFD_RESOLVE;

/* the host's MimeNetInterface. */
typedef struct MIME_AFD_INTERFACE {
    CHAR Name[16];
    ULONG Index;
    ULONG Flags;
    ULONG Mtu;
    UCHAR Prefix;
    UCHAR MacLength;
    UCHAR Mac[8];
    UCHAR Pad[2];
    MIME_AFD_ADDRESS Address;
} MIME_AFD_INTERFACE;

#define MIME_AFD_IF_UP 0x1
#define MIME_AFD_IF_LOOPBACK 0x2
#define MIME_AFD_IF_BROADCAST 0x4
#define MIME_AFD_IF_MULTICAST 0x8
#define MIME_AFD_IF_POINTTOPOINT 0x10

/* the statuses of socket errors, with their win32 meaning where windows has one. */
#define STATUS_AFD_LOCAL_DISCONNECT ((NTSTATUS)0xC000013BL)
#define STATUS_AFD_REMOTE_DISCONNECT ((NTSTATUS)0xC000013CL)
#define STATUS_AFD_INVALID_CONNECTION ((NTSTATUS)0xC0000140L)
#define STATUS_AFD_INVALID_ADDRESS_COMPONENT ((NTSTATUS)0xC0000207L)
#define STATUS_AFD_ADDRESS_ALREADY_EXISTS ((NTSTATUS)0xC000020AL)
#define STATUS_AFD_CONNECTION_RESET ((NTSTATUS)0xC000020DL)
#define STATUS_AFD_CONNECTION_REFUSED ((NTSTATUS)0xC0000236L)
#define STATUS_AFD_CONNECTION_ACTIVE ((NTSTATUS)0xC000023BL)
#define STATUS_AFD_NETWORK_UNREACHABLE ((NTSTATUS)0xC000023CL)
#define STATUS_AFD_HOST_UNREACHABLE ((NTSTATUS)0xC000023DL)
#define STATUS_AFD_PORT_UNREACHABLE ((NTSTATUS)0xC000023FL)
#define STATUS_AFD_CONNECTION_ABORTED ((NTSTATUS)0xC0000241L)
#define STATUS_AFD_HOST_DOWN ((NTSTATUS)0xC0000350L)
#define STATUS_AFD_IO_TIMEOUT ((NTSTATUS)0xC00000B5L)
#define STATUS_AFD_PIPE_DISCONNECTED ((NTSTATUS)0xC00000B0L)
#define STATUS_AFD_CANCELLED ((NTSTATUS)0xC0000120L)
#define STATUS_AFD_WOULD_BLOCK ((NTSTATUS)0xC00000A3L) /* STATUS_DEVICE_NOT_READY */

/* the other winsock errors travel as facility win32 statuses. */
static inline NTSTATUS MimeAfdStatus(LONG error)
{
    switch (error) {
    case 0:
        return 0;
    case 10035:
        return STATUS_AFD_WOULD_BLOCK;
    case 10054:
        return STATUS_AFD_CONNECTION_RESET;
    case 10053:
        return STATUS_AFD_CONNECTION_ABORTED;
    case 10061:
        return STATUS_AFD_CONNECTION_REFUSED;
    case 10060:
        return STATUS_AFD_IO_TIMEOUT;
    case 10051:
        return STATUS_AFD_NETWORK_UNREACHABLE;
    case 10065:
        return STATUS_AFD_HOST_UNREACHABLE;
    case 10064:
        return STATUS_AFD_HOST_DOWN;
    case 10048:
        return STATUS_AFD_ADDRESS_ALREADY_EXISTS;
    case 10049:
        return STATUS_AFD_INVALID_ADDRESS_COMPONENT;
    case 10057:
        return STATUS_AFD_INVALID_CONNECTION;
    case 10056:
        return STATUS_AFD_CONNECTION_ACTIVE;
    case 10058:
        return STATUS_AFD_PIPE_DISCONNECTED;
    case 10040:
        return (NTSTATUS)0x80000005L; /* STATUS_BUFFER_OVERFLOW */
    case 10055:
        return (NTSTATUS)0xC000009AL; /* STATUS_INSUFFICIENT_RESOURCES */
    case 10014:
        return (NTSTATUS)0xC0000005L; /* STATUS_ACCESS_VIOLATION */
    case 10022:
        return (NTSTATUS)0xC000000DL; /* STATUS_INVALID_PARAMETER */
    case 995:
        return STATUS_AFD_CANCELLED;
    }
    return (NTSTATUS)(0xC0070000UL | (ULONG)(error & 0xFFFF));
}

static inline LONG MimeAfdError(NTSTATUS status)
{
    switch ((ULONG)status) {
    case 0:
        return 0;
    case 0xC00000A3:
        return 10035;
    case 0xC000020D:
    case 0xC000013C:
    case 0xC000023F:
        return 10054;
    case 0xC0000241:
    case 0xC000013B:
        return 10053;
    case 0xC0000236:
        return 10061;
    case 0xC00000B5:
        return 10060;
    case 0xC000023C:
        return 10051;
    case 0xC000023D:
        return 10065;
    case 0xC0000350:
        return 10064;
    case 0xC000020A:
        return 10048;
    case 0xC0000207:
        return 10049;
    case 0xC0000140:
        return 10057;
    case 0xC000023B:
        return 10056;
    case 0xC00000B0:
        return 10058;
    case 0x80000005:
        return 10040;
    case 0xC000009A:
    case 0xC0000017:
        return 10055;
    case 0xC0000005:
        return 10014;
    case 0xC0000008:
    case 0xC0000024:
        return 10038;
    case 0xC0000022:
        return 10013;
    case 0xC0000120:
        return 995; /* WSA_OPERATION_ABORTED */
    }
    if (((ULONG)status & 0xFFFF0000) == 0xC0070000)
        return (LONG)((ULONG)status & 0xFFFF);
    return 10022;
}

#endif
