#ifndef NDRGEN_DHCPCORE_H
#define NDRGEN_DHCPCORE_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))

typedef struct DHCPV6_RPC_LUID DHCPV6_RPC_LUID;
typedef struct DHCPV6_RPC_ADDRESS DHCPV6_RPC_ADDRESS;
typedef struct DHCPV6_RPC_LEASE_ADDRESS DHCPV6_RPC_LEASE_ADDRESS;
typedef struct DHCPV6_RPC_ASSOCIATION DHCPV6_RPC_ASSOCIATION;
typedef struct DHCPV6_RPC_PREFIX DHCPV6_RPC_PREFIX;
typedef struct DHCPV6_RPC_LEASE DHCPV6_RPC_LEASE;
typedef struct DHCPV6_RPC_DUID DHCPV6_RPC_DUID;
typedef struct DHCPV6_RPC_OPTIONS DHCPV6_RPC_OPTIONS;
typedef struct DHCPV6_RPC_LEASE_ENTRY DHCPV6_RPC_LEASE_ENTRY;
typedef struct DHCPV6_RPC_LEASE_ARRAY DHCPV6_RPC_LEASE_ARRAY;

struct DHCPV6_RPC_LUID {
    unsigned long long Value;
};
typedef enum DHCPV6_RPC_STATE { Dhcpv6State0 = 0, Dhcpv6State1 = 1, Dhcpv6State2 = 2 } DHCPV6_RPC_STATE;
struct DHCPV6_RPC_ADDRESS {
    byte Bytes[16];
};
struct DHCPV6_RPC_LEASE_ADDRESS {
    DHCPV6_RPC_ADDRESS Address;
    unsigned long Preferred;
    unsigned long Valid;
    unsigned long Obtained;
    unsigned long Expires;
};
struct DHCPV6_RPC_ASSOCIATION {
    unsigned long T1;
    unsigned long T2;
    unsigned long Obtained;
    DHCPV6_RPC_LEASE_ADDRESS Addresses[2];
    unsigned short Count;
};
struct DHCPV6_RPC_PREFIX {
    unsigned long Length;
    DHCPV6_RPC_LEASE_ADDRESS Prefix;
};
struct DHCPV6_RPC_LEASE {
    DHCPV6_RPC_STATE State;
    DHCPV6_RPC_ASSOCIATION Association;
    byte HasAssociation;
    DHCPV6_RPC_PREFIX Delegated;
    byte HasServer;
    DHCPV6_RPC_ADDRESS Server;
};
struct DHCPV6_RPC_DUID {
    unsigned short Length;
    byte *Data;
};
struct DHCPV6_RPC_OPTIONS {
    byte Flags[4];
    unsigned long Version;
    unsigned long Limit;
    byte Stateless;
    unsigned long ClassIdLength;
    byte *ClassId;
    unsigned long DomainCount;
    char **Domains;
    unsigned long ServerCount;
    DHCPV6_RPC_ADDRESS *Servers;
};
struct DHCPV6_RPC_LEASE_ENTRY {
    byte Found;
    DHCPV6_RPC_LUID *Luid;
    DHCPV6_RPC_LEASE *Lease;
    DHCPV6_RPC_DUID *ServerDuid;
    DHCPV6_RPC_DUID *ClientDuid;
    DHCPV6_RPC_OPTIONS *Options;
    unsigned long Iaid;
};
struct DHCPV6_RPC_LEASE_ARRAY {
    unsigned long Count;
    DHCPV6_RPC_LEASE_ENTRY *Entries;
};

#ifdef _WIN64
_Static_assert(sizeof(DHCPV6_RPC_LUID) == 8, "size of DHCPV6_RPC_LUID");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LUID, Value) == 0, "DHCPV6_RPC_LUID.Value");
_Static_assert(sizeof(DHCPV6_RPC_STATE) == 4, "size of DHCPV6_RPC_STATE");
_Static_assert(sizeof(DHCPV6_RPC_ADDRESS) == 16, "size of DHCPV6_RPC_ADDRESS");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ADDRESS, Bytes) == 0, "DHCPV6_RPC_ADDRESS.Bytes");
_Static_assert(sizeof(DHCPV6_RPC_LEASE_ADDRESS) == 32, "size of DHCPV6_RPC_LEASE_ADDRESS");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ADDRESS, Address) == 0, "DHCPV6_RPC_LEASE_ADDRESS.Address");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ADDRESS, Preferred) == 16, "DHCPV6_RPC_LEASE_ADDRESS.Preferred");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ADDRESS, Valid) == 20, "DHCPV6_RPC_LEASE_ADDRESS.Valid");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ADDRESS, Obtained) == 24, "DHCPV6_RPC_LEASE_ADDRESS.Obtained");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ADDRESS, Expires) == 28, "DHCPV6_RPC_LEASE_ADDRESS.Expires");
_Static_assert(sizeof(DHCPV6_RPC_ASSOCIATION) == 80, "size of DHCPV6_RPC_ASSOCIATION");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ASSOCIATION, T1) == 0, "DHCPV6_RPC_ASSOCIATION.T1");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ASSOCIATION, T2) == 4, "DHCPV6_RPC_ASSOCIATION.T2");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ASSOCIATION, Obtained) == 8, "DHCPV6_RPC_ASSOCIATION.Obtained");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ASSOCIATION, Addresses) == 12, "DHCPV6_RPC_ASSOCIATION.Addresses");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ASSOCIATION, Count) == 76, "DHCPV6_RPC_ASSOCIATION.Count");
_Static_assert(sizeof(DHCPV6_RPC_PREFIX) == 36, "size of DHCPV6_RPC_PREFIX");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_PREFIX, Length) == 0, "DHCPV6_RPC_PREFIX.Length");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_PREFIX, Prefix) == 4, "DHCPV6_RPC_PREFIX.Prefix");
_Static_assert(sizeof(DHCPV6_RPC_LEASE) == 144, "size of DHCPV6_RPC_LEASE");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, State) == 0, "DHCPV6_RPC_LEASE.State");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, Association) == 4, "DHCPV6_RPC_LEASE.Association");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, HasAssociation) == 84, "DHCPV6_RPC_LEASE.HasAssociation");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, Delegated) == 88, "DHCPV6_RPC_LEASE.Delegated");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, HasServer) == 124, "DHCPV6_RPC_LEASE.HasServer");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, Server) == 125, "DHCPV6_RPC_LEASE.Server");
_Static_assert(sizeof(DHCPV6_RPC_DUID) == 16, "size of DHCPV6_RPC_DUID");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_DUID, Length) == 0, "DHCPV6_RPC_DUID.Length");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_DUID, Data) == 8, "DHCPV6_RPC_DUID.Data");
_Static_assert(sizeof(DHCPV6_RPC_OPTIONS) == 64, "size of DHCPV6_RPC_OPTIONS");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Flags) == 0, "DHCPV6_RPC_OPTIONS.Flags");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Version) == 4, "DHCPV6_RPC_OPTIONS.Version");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Limit) == 8, "DHCPV6_RPC_OPTIONS.Limit");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Stateless) == 12, "DHCPV6_RPC_OPTIONS.Stateless");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, ClassIdLength) == 16, "DHCPV6_RPC_OPTIONS.ClassIdLength");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, ClassId) == 24, "DHCPV6_RPC_OPTIONS.ClassId");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, DomainCount) == 32, "DHCPV6_RPC_OPTIONS.DomainCount");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Domains) == 40, "DHCPV6_RPC_OPTIONS.Domains");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, ServerCount) == 48, "DHCPV6_RPC_OPTIONS.ServerCount");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Servers) == 56, "DHCPV6_RPC_OPTIONS.Servers");
_Static_assert(sizeof(DHCPV6_RPC_LEASE_ENTRY) == 56, "size of DHCPV6_RPC_LEASE_ENTRY");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, Found) == 0, "DHCPV6_RPC_LEASE_ENTRY.Found");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, Luid) == 8, "DHCPV6_RPC_LEASE_ENTRY.Luid");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, Lease) == 16, "DHCPV6_RPC_LEASE_ENTRY.Lease");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, ServerDuid) == 24, "DHCPV6_RPC_LEASE_ENTRY.ServerDuid");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, ClientDuid) == 32, "DHCPV6_RPC_LEASE_ENTRY.ClientDuid");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, Options) == 40, "DHCPV6_RPC_LEASE_ENTRY.Options");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, Iaid) == 48, "DHCPV6_RPC_LEASE_ENTRY.Iaid");
_Static_assert(sizeof(DHCPV6_RPC_LEASE_ARRAY) == 16, "size of DHCPV6_RPC_LEASE_ARRAY");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ARRAY, Count) == 0, "DHCPV6_RPC_LEASE_ARRAY.Count");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ARRAY, Entries) == 8, "DHCPV6_RPC_LEASE_ARRAY.Entries");
#else
_Static_assert(sizeof(DHCPV6_RPC_LUID) == 8, "size of DHCPV6_RPC_LUID");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LUID, Value) == 0, "DHCPV6_RPC_LUID.Value");
_Static_assert(sizeof(DHCPV6_RPC_STATE) == 4, "size of DHCPV6_RPC_STATE");
_Static_assert(sizeof(DHCPV6_RPC_ADDRESS) == 16, "size of DHCPV6_RPC_ADDRESS");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ADDRESS, Bytes) == 0, "DHCPV6_RPC_ADDRESS.Bytes");
_Static_assert(sizeof(DHCPV6_RPC_LEASE_ADDRESS) == 32, "size of DHCPV6_RPC_LEASE_ADDRESS");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ADDRESS, Address) == 0, "DHCPV6_RPC_LEASE_ADDRESS.Address");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ADDRESS, Preferred) == 16, "DHCPV6_RPC_LEASE_ADDRESS.Preferred");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ADDRESS, Valid) == 20, "DHCPV6_RPC_LEASE_ADDRESS.Valid");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ADDRESS, Obtained) == 24, "DHCPV6_RPC_LEASE_ADDRESS.Obtained");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ADDRESS, Expires) == 28, "DHCPV6_RPC_LEASE_ADDRESS.Expires");
_Static_assert(sizeof(DHCPV6_RPC_ASSOCIATION) == 80, "size of DHCPV6_RPC_ASSOCIATION");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ASSOCIATION, T1) == 0, "DHCPV6_RPC_ASSOCIATION.T1");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ASSOCIATION, T2) == 4, "DHCPV6_RPC_ASSOCIATION.T2");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ASSOCIATION, Obtained) == 8, "DHCPV6_RPC_ASSOCIATION.Obtained");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ASSOCIATION, Addresses) == 12, "DHCPV6_RPC_ASSOCIATION.Addresses");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_ASSOCIATION, Count) == 76, "DHCPV6_RPC_ASSOCIATION.Count");
_Static_assert(sizeof(DHCPV6_RPC_PREFIX) == 36, "size of DHCPV6_RPC_PREFIX");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_PREFIX, Length) == 0, "DHCPV6_RPC_PREFIX.Length");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_PREFIX, Prefix) == 4, "DHCPV6_RPC_PREFIX.Prefix");
_Static_assert(sizeof(DHCPV6_RPC_LEASE) == 144, "size of DHCPV6_RPC_LEASE");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, State) == 0, "DHCPV6_RPC_LEASE.State");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, Association) == 4, "DHCPV6_RPC_LEASE.Association");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, HasAssociation) == 84, "DHCPV6_RPC_LEASE.HasAssociation");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, Delegated) == 88, "DHCPV6_RPC_LEASE.Delegated");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, HasServer) == 124, "DHCPV6_RPC_LEASE.HasServer");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE, Server) == 125, "DHCPV6_RPC_LEASE.Server");
_Static_assert(sizeof(DHCPV6_RPC_DUID) == 8, "size of DHCPV6_RPC_DUID");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_DUID, Length) == 0, "DHCPV6_RPC_DUID.Length");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_DUID, Data) == 4, "DHCPV6_RPC_DUID.Data");
_Static_assert(sizeof(DHCPV6_RPC_OPTIONS) == 40, "size of DHCPV6_RPC_OPTIONS");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Flags) == 0, "DHCPV6_RPC_OPTIONS.Flags");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Version) == 4, "DHCPV6_RPC_OPTIONS.Version");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Limit) == 8, "DHCPV6_RPC_OPTIONS.Limit");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Stateless) == 12, "DHCPV6_RPC_OPTIONS.Stateless");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, ClassIdLength) == 16, "DHCPV6_RPC_OPTIONS.ClassIdLength");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, ClassId) == 20, "DHCPV6_RPC_OPTIONS.ClassId");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, DomainCount) == 24, "DHCPV6_RPC_OPTIONS.DomainCount");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Domains) == 28, "DHCPV6_RPC_OPTIONS.Domains");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, ServerCount) == 32, "DHCPV6_RPC_OPTIONS.ServerCount");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_OPTIONS, Servers) == 36, "DHCPV6_RPC_OPTIONS.Servers");
_Static_assert(sizeof(DHCPV6_RPC_LEASE_ENTRY) == 28, "size of DHCPV6_RPC_LEASE_ENTRY");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, Found) == 0, "DHCPV6_RPC_LEASE_ENTRY.Found");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, Luid) == 4, "DHCPV6_RPC_LEASE_ENTRY.Luid");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, Lease) == 8, "DHCPV6_RPC_LEASE_ENTRY.Lease");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, ServerDuid) == 12, "DHCPV6_RPC_LEASE_ENTRY.ServerDuid");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, ClientDuid) == 16, "DHCPV6_RPC_LEASE_ENTRY.ClientDuid");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, Options) == 20, "DHCPV6_RPC_LEASE_ENTRY.Options");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ENTRY, Iaid) == 24, "DHCPV6_RPC_LEASE_ENTRY.Iaid");
_Static_assert(sizeof(DHCPV6_RPC_LEASE_ARRAY) == 8, "size of DHCPV6_RPC_LEASE_ARRAY");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ARRAY, Count) == 0, "DHCPV6_RPC_LEASE_ARRAY.Count");
_Static_assert(__builtin_offsetof(DHCPV6_RPC_LEASE_ARRAY, Entries) == 4, "DHCPV6_RPC_LEASE_ARRAY.Entries");
#endif

/* ------------------------------------------------------------------ interface Dhcpv6 */

#ifndef DHCPV6_CLIENT_ONLY
extern unsigned long WINAPI Dhcpv6_RequestPrefixEx_s(unsigned short *ServerName);
extern unsigned long WINAPI Dhcpv6_RenewPrefixEx_s(unsigned short *ServerName);
extern unsigned long WINAPI Dhcpv6_ReleasePrefixEx_s(unsigned short *ServerName);
extern unsigned long WINAPI Dhcpv6_CancelOperation_s(unsigned short *ServerName);
extern unsigned long WINAPI Dhcpv6_RequestParams_s(unsigned short *ServerName);
extern unsigned long WINAPI Dhcpv6_AcquireParametersv6_s(unsigned short *ServerName);
extern unsigned long WINAPI Dhcpv6_ReleaseParametersv6_s(unsigned short *ServerName);
extern unsigned long WINAPI Dhcpv6_QueryLeaseInfov6_s(unsigned short *ServerName, DHCPV6_RPC_LUID *Luid, DHCPV6_RPC_LEASE *Lease, DHCPV6_RPC_DUID *ServerDuid, DHCPV6_RPC_DUID *ClientDuid, DHCPV6_RPC_OPTIONS *Options, unsigned long *Iaid);
extern unsigned long WINAPI Dhcpv6_GetTraceArray_s(unsigned short *ServerName);
extern unsigned long WINAPI Dhcpv6_SetUserClass_s(unsigned short *ServerName);
extern unsigned long WINAPI Dhcpv6_QueryLeaseInfov6Array_s(unsigned short *ServerName, DHCPV6_RPC_LEASE_ARRAY *Array);
extern unsigned long WINAPI Dhcpv6_EnableDhcpv6_s(unsigned short *ServerName);
extern unsigned long WINAPI Dhcpv6_RequestCachedParamsEx_s(unsigned short *ServerName);
#endif
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char Dhcpv6_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
    0x15, 0x07, 0x08, 0x00, 0x0b, 0x5b, /* 6: struct DHCPV6_RPC_LUID (STRUCT) */
    0x1a, 0x03, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x4c, 0x00, 0x0e, /* 12: struct DHCPV6_RPC_LEASE (BOGUS) */
    0x00, 0x01, 0x4c, 0x03, 0x30, 0x00, 0x01, 0x4c, 0x00, 0x36, 0x00, 0x3f,
    0x5b,
    0x1a, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x4c, /* 37: struct DHCPV6_RPC_ASSOCIATION (BOGUS) */
    0x00, 0x05, 0x00, 0x06, 0x3e, 0x5b,
    0x1d, 0x03, 0x40, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b, /* 55: array */
    0x15, 0x03, 0x20, 0x00, 0x43, 0x43, 0x3e, 0x08, 0x08, 0x08, 0x08, 0x5b, /* 64: struct DHCPV6_RPC_LEASE_ADDRESS (STRUCT) */
    0x15, 0x03, 0x24, 0x00, 0x08, 0x43, 0x43, 0x43, 0x43, 0x40, 0x5b, /* 76: struct DHCPV6_RPC_PREFIX (STRUCT) */
    0x15, 0x00, 0x10, 0x00, 0x43, 0x43, 0x3e, 0x5b, /* 87: struct DHCPV6_RPC_ADDRESS (STRUCT) */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x06, 0x42, 0x36, 0x5b, /* 95: struct DHCPV6_RPC_DUID (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x16, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 111: array */
    0x1a, 0x03, 0x40, 0x00, 0x00, 0x00, 0x14, 0x00, 0x4c, 0x00, 0x1c, 0x00, /* 123: struct DHCPV6_RPC_OPTIONS (BOGUS) */
    0x08, 0x08, 0x01, 0x3f, 0x08, 0x40, 0x36, 0x08, 0x40, 0x36, 0x08, 0x40,
    0x36, 0x5b, 0x12, 0x00, 0x10, 0x00, 0x12, 0x00, 0x18, 0x00, 0x12, 0x00,
    0x29, 0x00,
    0x1d, 0x00, 0x04, 0x00, 0x01, 0x5b, /* 161: array */
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x10, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 167: array */
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x20, 0x00, 0x01, 0x00, 0xff, 0xff, /* 179: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x08, 0x22, 0x5c, 0x5b,
    0x1b, 0x00, 0x10, 0x00, 0x18, 0x00, 0x30, 0x00, 0x01, 0x00, 0x4c, 0x00, /* 200: array */
    0x83, 0xff, 0x5b,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x40, 0x36, 0x5b, /* 215: struct DHCPV6_RPC_LEASE_ARRAY (BOGUS) */
    0x11, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 231: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x38, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x43, 0x36, 0x36, /* 252: struct DHCPV6_RPC_LEASE_ENTRY (BOGUS) */
    0x36, 0x36, 0x36, 0x08, 0x40, 0x5b, 0x11, 0x00, 0xf6, 0xfe, 0x11, 0x00,
    0xf8, 0xfe, 0x11, 0x00, 0x47, 0xff, 0x11, 0x00, 0x43, 0xff, 0x11, 0x00,
    0x5b, 0xff,
#else
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
    0x15, 0x07, 0x08, 0x00, 0x0b, 0x5b, /* 6: struct DHCPV6_RPC_LUID (STRUCT) */
    0x1a, 0x03, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x4c, 0x00, 0x0e, /* 12: struct DHCPV6_RPC_LEASE (BOGUS) */
    0x00, 0x01, 0x4c, 0x03, 0x30, 0x00, 0x01, 0x4c, 0x00, 0x36, 0x00, 0x3f,
    0x5b,
    0x1a, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x4c, /* 37: struct DHCPV6_RPC_ASSOCIATION (BOGUS) */
    0x00, 0x05, 0x00, 0x06, 0x3e, 0x5b,
    0x1d, 0x03, 0x40, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b, /* 55: array */
    0x15, 0x03, 0x20, 0x00, 0x43, 0x43, 0x3e, 0x08, 0x08, 0x08, 0x08, 0x5b, /* 64: struct DHCPV6_RPC_LEASE_ADDRESS (STRUCT) */
    0x15, 0x03, 0x24, 0x00, 0x08, 0x43, 0x43, 0x43, 0x43, 0x40, 0x5b, /* 76: struct DHCPV6_RPC_PREFIX (STRUCT) */
    0x15, 0x00, 0x10, 0x00, 0x43, 0x43, 0x3e, 0x5b, /* 87: struct DHCPV6_RPC_ADDRESS (STRUCT) */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 95: struct DHCPV6_RPC_DUID (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x06, 0x3e, 0x36, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x16, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 116: array */
    0x16, 0x03, 0x28, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x14, 0x00, 0x14, 0x00, /* 128: struct DHCPV6_RPC_OPTIONS (PSTRUCT) */
    0x12, 0x00, 0x23, 0x00, 0x46, 0x5c, 0x1c, 0x00, 0x1c, 0x00, 0x12, 0x00,
    0x25, 0x00, 0x46, 0x5c, 0x24, 0x00, 0x24, 0x00, 0x12, 0x00, 0x30, 0x00,
    0x5b, 0x40, 0x08, 0x08, 0x01, 0x3f, 0x08, 0x36, 0x08, 0x36, 0x08, 0x36,
    0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x10, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 177: array */
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x18, 0x00, 0x01, 0x00, 0xff, 0xff, /* 189: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x08, 0x22, 0x5c, 0x5b,
    0x1b, 0x00, 0x10, 0x00, 0x18, 0x00, 0x20, 0x00, 0x01, 0x00, 0x4c, 0x00, /* 210: array */
    0x79, 0xff, 0x5b,
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 225: struct DHCPV6_RPC_LEASE_ARRAY (PSTRUCT) */
    0x11, 0x00, 0x06, 0x00, 0x5b, 0x08, 0x36, 0x5b,
    0x1b, 0x03, 0x1c, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x4b, 0x5c, /* 245: array */
    0x48, 0x49, 0x1c, 0x00, 0x00, 0x00, 0x05, 0x00, 0x04, 0x00, 0x04, 0x00,
    0x11, 0x00, 0xf7, 0xfe, 0x08, 0x00, 0x08, 0x00, 0x11, 0x00, 0xf5, 0xfe,
    0x0c, 0x00, 0x0c, 0x00, 0x11, 0x00, 0x40, 0xff, 0x10, 0x00, 0x10, 0x00,
    0x11, 0x00, 0x38, 0xff, 0x14, 0x00, 0x14, 0x00, 0x11, 0x00, 0x51, 0xff,
    0x5b, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x16, 0x03, 0x1c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 311: struct DHCPV6_RPC_LEASE_ENTRY (PSTRUCT) */
    0x11, 0x00, 0xc1, 0xfe, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, 0x11, 0x00,
    0xbd, 0xfe, 0x46, 0x5c, 0x0c, 0x00, 0x0c, 0x00, 0x11, 0x00, 0x06, 0xff,
    0x46, 0x5c, 0x10, 0x00, 0x10, 0x00, 0x11, 0x00, 0xfc, 0xfe, 0x46, 0x5c,
    0x14, 0x00, 0x14, 0x00, 0x11, 0x00, 0x13, 0xff, 0x5b, 0x01, 0x3f, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x08, 0x5b,
#endif
};

static const unsigned char Dhcpv6_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x32, 0x40, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 2: Dhcpv6.RequestPrefixEx */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x01, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 36: Dhcpv6.RenewPrefixEx */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x02, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 70: Dhcpv6.ReleasePrefixEx */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x03, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 104: Dhcpv6.CancelOperation */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x04, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 138: Dhcpv6.RequestParams */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x05, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 172: Dhcpv6.AcquireParametersv6 */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x06, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 206: Dhcpv6.ReleaseParametersv6 */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x07, 0x00, 0x40, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x47, 0x08, /* 240: Dhcpv6.QueryLeaseInfov6 */
    0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x0a, 0x01, 0x08, 0x00, 0x06, 0x00, 0x13, 0x01,
    0x10, 0x00, 0x0c, 0x00, 0x13, 0x41, 0x18, 0x00, 0x5f, 0x00, 0x13, 0x41,
    0x20, 0x00, 0x5f, 0x00, 0x13, 0x01, 0x28, 0x00, 0x7b, 0x00, 0x50, 0x21,
    0x30, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x38, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x08, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 310: Dhcpv6.GetTraceArray */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x09, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 344: Dhcpv6.SetUserClass */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x0a, 0x00, 0x18, 0x00, 0x00, 0x00, 0x07, 0x00, 0x47, 0x03, /* 378: Dhcpv6.QueryLeaseInfov6Array */
    0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x1b, 0x01, 0x08, 0x00, 0xd7, 0x00, 0x70, 0x00,
    0x10, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x0b, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 418: Dhcpv6.EnableDhcpv6 */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x0c, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 452: Dhcpv6.RequestCachedParamsEx */
    0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x32, 0x40, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 2: Dhcpv6.RequestPrefixEx */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 34: Dhcpv6.RenewPrefixEx */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x02, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 66: Dhcpv6.ReleasePrefixEx */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 98: Dhcpv6.CancelOperation */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 130: Dhcpv6.RequestParams */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x05, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 162: Dhcpv6.AcquireParametersv6 */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x06, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 194: Dhcpv6.ReleaseParametersv6 */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x07, 0x00, 0x20, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x47, 0x08, /* 226: Dhcpv6.QueryLeaseInfov6 */
    0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x0a, 0x01, 0x04, 0x00, 0x06, 0x00, 0x13, 0x01, 0x08, 0x00,
    0x0c, 0x00, 0x13, 0x21, 0x0c, 0x00, 0x5f, 0x00, 0x13, 0x21, 0x10, 0x00,
    0x5f, 0x00, 0x13, 0xa1, 0x14, 0x00, 0x80, 0x00, 0x50, 0x21, 0x18, 0x00,
    0x08, 0x5c, 0x70, 0x00, 0x1c, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 294: Dhcpv6.GetTraceArray */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x09, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 326: Dhcpv6.SetUserClass */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x0a, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x07, 0x00, 0x47, 0x03, /* 358: Dhcpv6.QueryLeaseInfov6Array */
    0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x1b, 0x01, 0x04, 0x00, 0xe1, 0x00, 0x70, 0x00, 0x08, 0x00,
    0x08, 0x5c,
    0x32, 0x40, 0x0b, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 396: Dhcpv6.EnableDhcpv6 */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x32, 0x40, 0x0c, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x46, 0x02, /* 428: Dhcpv6.RequestCachedParamsEx */
    0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short Dhcpv6_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 36, 70, 104, 138, 172, 206, 240, 310, 344, 378, 418, 452
#else
    2, 34, 66, 98, 130, 162, 194, 226, 294, 326, 358, 396, 428
#endif
};

handle_t Dhcpv6_IfHandle;

static RPC_CLIENT_INTERFACE Dhcpv6_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x3c4728c5, 0xf0ab, 0x448b, {0xbd, 0xa1, 0x6c, 0xe0, 0x1e, 0xb0, 0xa6, 0xd6}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE Dhcpv6_c_ifspec = (RPC_IF_HANDLE)&Dhcpv6_c_ifspec_data;

static MIDL_STUB_DESC Dhcpv6_StubDesc = {
    .RpcInterfaceInformation = &Dhcpv6_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .IMPLICIT_HANDLE_INFO.pPrimitiveHandle = &Dhcpv6_IfHandle,
    .pFormatTypes = Dhcpv6_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef DHCPV6_CLIENT_ONLY
static const SERVER_ROUTINE Dhcpv6_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)Dhcpv6_RequestPrefixEx_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_RenewPrefixEx_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_ReleasePrefixEx_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_CancelOperation_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_RequestParams_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_AcquireParametersv6_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_ReleaseParametersv6_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_QueryLeaseInfov6_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_GetTraceArray_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_SetUserClass_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_QueryLeaseInfov6Array_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_EnableDhcpv6_s,
    (SERVER_ROUTINE)(void *)Dhcpv6_RequestCachedParamsEx_s
};

int (*Dhcpv6_hook)(PRPC_MESSAGE);

static void __RPC_STUB Dhcpv6_dispatch_0(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_1(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_2(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_3(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_4(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_5(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_6(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_7(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_8(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_9(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_10(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_11(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB Dhcpv6_dispatch_12(PRPC_MESSAGE m)
{
    if (Dhcpv6_hook && Dhcpv6_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION Dhcpv6_DispatchFunctions[] = {
Dhcpv6_dispatch_0, Dhcpv6_dispatch_1, Dhcpv6_dispatch_2, Dhcpv6_dispatch_3, Dhcpv6_dispatch_4, Dhcpv6_dispatch_5, Dhcpv6_dispatch_6, Dhcpv6_dispatch_7, Dhcpv6_dispatch_8, Dhcpv6_dispatch_9, Dhcpv6_dispatch_10, Dhcpv6_dispatch_11, Dhcpv6_dispatch_12
};
static RPC_DISPATCH_TABLE Dhcpv6_DispatchTable = { 13, Dhcpv6_DispatchFunctions, 0 };
static MIDL_SERVER_INFO Dhcpv6_ServerInfo = {
    .pStubDesc = &Dhcpv6_StubDesc,
    .DispatchTable = Dhcpv6_ServerRoutineTable,
    .ProcString = Dhcpv6_ProcFormat,
    .FmtStringOffset = Dhcpv6_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE Dhcpv6_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x3c4728c5, 0xf0ab, 0x448b, {0xbd, 0xa1, 0x6c, 0xe0, 0x1e, 0xb0, 0xa6, 0xd6}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &Dhcpv6_DispatchTable, 0, 0, 0, &Dhcpv6_ServerInfo, 0 };
RPC_IF_HANDLE Dhcpv6_s_ifspec = (RPC_IF_HANDLE)&Dhcpv6_s_ifspec_data;
#endif

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_RequestPrefixEx(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[0], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_RenewPrefixEx(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[1], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[1], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_ReleasePrefixEx(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[2], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[2], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_CancelOperation(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[3], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[3], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_RequestParams(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[4], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[4], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_AcquireParametersv6(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[5], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[5], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_ReleaseParametersv6(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[6], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[6], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_QueryLeaseInfov6(unsigned short *ServerName, DHCPV6_RPC_LUID *Luid, DHCPV6_RPC_LEASE *Lease, DHCPV6_RPC_DUID *ServerDuid, DHCPV6_RPC_DUID *ClientDuid, DHCPV6_RPC_OPTIONS *Options, unsigned long *Iaid)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[7], ServerName, Luid, Lease, ServerDuid, ClientDuid, Options, Iaid).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
        DHCPV6_RPC_LUID *Luid;
        DHCPV6_RPC_LEASE *Lease;
        DHCPV6_RPC_DUID *ServerDuid;
        DHCPV6_RPC_DUID *ClientDuid;
        DHCPV6_RPC_OPTIONS *Options;
        unsigned long *Iaid;
    } frame = { ServerName, Luid, Lease, ServerDuid, ClientDuid, Options, Iaid };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[7], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_GetTraceArray(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[8], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[8], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_SetUserClass(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[9], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[9], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_QueryLeaseInfov6Array(unsigned short *ServerName, DHCPV6_RPC_LEASE_ARRAY *Array)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[10], ServerName, Array).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
        DHCPV6_RPC_LEASE_ARRAY *Array;
    } frame = { ServerName, Array };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[10], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_EnableDhcpv6(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[11], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[11], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI Dhcpv6_RequestCachedParamsEx(unsigned short *ServerName)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[12], ServerName).Simple;
#else
#pragma pack(push, 4)
    struct {
        unsigned short *ServerName;
    } frame = { ServerName };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&Dhcpv6_StubDesc, Dhcpv6_ProcFormat + Dhcpv6_FormatStringOffsetTable[12], (unsigned char *)&frame).Simple;
#endif
}

#endif
