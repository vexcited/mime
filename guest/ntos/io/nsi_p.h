#ifndef MIME_NTOS_NSI_P_H
#define MIME_NTOS_NSI_P_H

#include "../ntos.h"

#include <mime/afd.h>
#include <mime/nsi.h>

#define FILE_DEVICE_NETWORK 0x12

#define NSI_AF_INET 2
#define NSI_AF_INET6 23

#define STATUS_NSI_NOT_FOUND ((NTSTATUS)0xC0000225L) /* STATUS_NOT_FOUND, error 1168 */
#define STATUS_NSI_NO_OBJECT ((NTSTATUS)0xC0000034L) /* what ndis answers for an interface it has not, error 2 */

typedef struct NSI_IF {
    ULONG Index;
    ULONG64 Luid;
    GUID Guid;
    WCHAR Alias[64];
    WCHAR Description[96];
    CHAR Host[16];
    ULONG Type;
    ULONG Mtu;
    ULONG64 Speed;
    ULONG64 RxBytes, RxPackets, RxMulticast, RxErrors, RxDropped;
    ULONG64 TxBytes, TxPackets, TxErrors, TxDropped;
    UCHAR Mac[8];
    ULONG MacLength;
    BOOLEAN Loopback;
    BOOLEAN AdminUp;
    BOOLEAN Up;
    BOOLEAN Hardware;
    BOOLEAN Ipv4;
    BOOLEAN Ipv6;
    BOOLEAN HasLinkLocal;
    UCHAR LinkLocal[16];
    ULONG Metric;
    ULONG Zones[16];
} NSI_IF;

typedef struct NSI_ADDRESS {
    UCHAR Family;
    UCHAR Prefix;
    UCHAR Bytes[16];
    ULONG If; /* position in Ifs */
    ULONG Flags; /* MIME_AFD_ADDR_* */
    ULONG Valid;
    ULONG Preferred;
    ULONG PrefixOrigin;
    ULONG SuffixOrigin;
    ULONG Dad;
    ULONG Scope;
} NSI_ADDRESS;

typedef struct NSI_ROUTE {
    UCHAR Family;
    UCHAR Prefix;
    UCHAR Destination[16];
    UCHAR NextHop[16];
    ULONG If;
    ULONG Metric;
    ULONG Protocol;
    ULONG Origin;
    BOOLEAN Loopback;
    BOOLEAN Immortal;
} NSI_ROUTE;

typedef struct NSI_GROUP {
    UCHAR Family;
    UCHAR Bytes[16];
    ULONG If;
} NSI_GROUP;

/* the machine's network as the store tells it: the host's configuration and the software loopback. */
typedef struct NSI_NET {
    ULONG64 Taken; /* interrupt time */
    ULONG IfCount;
    NSI_IF *Ifs; /* by luid */
    ULONG AddressCount;
    NSI_ADDRESS *Addresses;
    ULONG RouteCount;
    NSI_ROUTE *Routes;
    ULONG GroupCount;
    NSI_GROUP *Groups;
    ULONG SocketCount;
    MIME_AFD_SOCKET_INFO *Sockets;
} NSI_NET;

#define NSI_T_SORT 0x1        /* objects come out in the order of their keys' bytes */
#define NSI_T_COUNT_MORE 0x2  /* a buffer too small gets the whole count with the status of more entries */
#define NSI_T_UNSUPPORTED 0x4 /* enumeration is not supported rather than not implemented */
#define NSI_T_COUNTERS 0x8    /* the dynamic structure is counters, no part of what a change is */

typedef struct NSI_TABLE {
    USHORT Key, Rw, Dynamic, Static;
    ULONG Flags;
    /* the objects of the table, each with all of its structures. */
    ULONG (*Count)(const NSI_NET *net, ULONG module, ULONG table);
    void (*Object)(const NSI_NET *net, ULONG module, ULONG table, ULONG i, PUCHAR key, PUCHAR rw, PUCHAR dynamic,
                   PUCHAR fixed);
    /* an object that is computed from its key, or the one of a table with no key. */
    NTSTATUS (*Get)(const NSI_NET *net, ULONG module, ULONG table, const UCHAR *key, PUCHAR rw, PUCHAR dynamic,
                    PUCHAR fixed);
    /* a parameter or all parameters set, or an object deleted or reset: the action is netio's, the key may be the
       start of the table's. */
    NTSTATUS (*Set)(const NSI_NET *net, ULONG module, ULONG table, ULONG action, const UCHAR *key, ULONG key_size,
                    ULONG type, const UCHAR *data, ULONG size, ULONG offset);
} NSI_TABLE;

/* nsinet.c */
void NsiNetInitialize(void);
const NSI_TABLE *NsiNetTable(ULONG module, ULONG table, ULONG *tables);
/* a snapshot at most half a second old, with the sockets of this instant for the tcp and udp modules; the caller holds
   the store's lock. */
const NSI_NET *NsiNetCurrent(BOOLEAN fresh, ULONG module);
/* the eight bytes of WNF_NLM_GLOBAL_CONNECTIVITY_STATE for a snapshot. */
ULONG64 NsiNetConnectivity(const NSI_NET *net);

/* netbt.c */
void NetbtSync(const NSI_NET *net);

#endif
