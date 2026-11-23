#ifndef NDRGEN_DNSRSLVR_H
#define NDRGEN_DNSRSLVR_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))

typedef struct DNS_RPC_CACHE_ENTRY DNS_RPC_CACHE_ENTRY;

struct DNS_RPC_CACHE_ENTRY {
    DNS_RPC_CACHE_ENTRY *pNext;
    unsigned short *pszName;
    unsigned short wType;
    unsigned short wDataLength;
    unsigned short wFlags;
};

#ifdef _WIN64
_Static_assert(sizeof(DNS_RPC_CACHE_ENTRY) == 24, "size of DNS_RPC_CACHE_ENTRY");
_Static_assert(__builtin_offsetof(DNS_RPC_CACHE_ENTRY, pNext) == 0, "DNS_RPC_CACHE_ENTRY.pNext");
_Static_assert(__builtin_offsetof(DNS_RPC_CACHE_ENTRY, pszName) == 8, "DNS_RPC_CACHE_ENTRY.pszName");
_Static_assert(__builtin_offsetof(DNS_RPC_CACHE_ENTRY, wType) == 16, "DNS_RPC_CACHE_ENTRY.wType");
_Static_assert(__builtin_offsetof(DNS_RPC_CACHE_ENTRY, wDataLength) == 18, "DNS_RPC_CACHE_ENTRY.wDataLength");
_Static_assert(__builtin_offsetof(DNS_RPC_CACHE_ENTRY, wFlags) == 20, "DNS_RPC_CACHE_ENTRY.wFlags");
#else
_Static_assert(sizeof(DNS_RPC_CACHE_ENTRY) == 16, "size of DNS_RPC_CACHE_ENTRY");
_Static_assert(__builtin_offsetof(DNS_RPC_CACHE_ENTRY, pNext) == 0, "DNS_RPC_CACHE_ENTRY.pNext");
_Static_assert(__builtin_offsetof(DNS_RPC_CACHE_ENTRY, pszName) == 4, "DNS_RPC_CACHE_ENTRY.pszName");
_Static_assert(__builtin_offsetof(DNS_RPC_CACHE_ENTRY, wType) == 8, "DNS_RPC_CACHE_ENTRY.wType");
_Static_assert(__builtin_offsetof(DNS_RPC_CACHE_ENTRY, wDataLength) == 10, "DNS_RPC_CACHE_ENTRY.wDataLength");
_Static_assert(__builtin_offsetof(DNS_RPC_CACHE_ENTRY, wFlags) == 12, "DNS_RPC_CACHE_ENTRY.wFlags");
#endif

/* ------------------------------------------------------------------ interface resolver */

#ifndef RESOLVER_CLIENT_ONLY
extern long WINAPI resolver_R_ReadCache_s(handle_t Binding, long long Flags, DNS_RPC_CACHE_ENTRY **ppCacheTable);
extern long WINAPI resolver_R_ResolverGetConfig_s(handle_t Binding);
extern long WINAPI resolver_R_ResolverFlushCache_s(handle_t Binding);
extern long WINAPI resolver_R_ResolverFlushCacheEntry_s(handle_t Binding, unsigned short *pwsName);
extern long WINAPI resolver_R_ResolverQuery_s(handle_t Binding);
extern long WINAPI resolver_R_DnsRegisterLocal_s(handle_t Binding);
extern long WINAPI resolver_R_DnsDeRegisterLocal_s(handle_t Binding);
extern long WINAPI resolver_R_DnsStartMulticastQuery_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetMulticastData_s(handle_t Binding);
extern long WINAPI resolver_R_DnsStopMulticastQuery_s(handle_t Binding);
extern long WINAPI resolver_R_ResolverSimpleOp_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetProxyInformation_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetPolicyTableInfo_s(handle_t Binding);
extern long WINAPI resolver_R_DnsSetConnectionPolicyInfo_s(handle_t Binding);
extern long WINAPI resolver_R_DnsDeleteConnectionPolicyInfo_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetSettings_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetSettingsImpersonated_s(handle_t Binding);
extern long WINAPI resolver_R_DnsSetSettings_s(handle_t Binding);
extern long WINAPI resolver_R_DnsSetSettingsImpersonated_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetInterfaceSettings_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetInterfaceSettingsImpersonated_s(handle_t Binding);
extern long WINAPI resolver_R_DnsSetInterfaceSettings_s(handle_t Binding);
extern long WINAPI resolver_R_DnsSetInterfaceSettingsImpersonated_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetAdaptersInfo_s(handle_t Binding);
extern long WINAPI resolver_R_DnsSetNrptRules_s(handle_t Binding);
extern long WINAPI resolver_R_DnsSetWellKnownServerProperty_s(handle_t Binding);
extern long WINAPI resolver_R_DnsDeleteWellKnownServer_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetWellKnownServerProperty_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetWellKnownServers_s(handle_t Binding);
extern long WINAPI resolver_R_DnsDeleteSettings_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetDdrInfo_s(handle_t Binding);
extern long WINAPI resolver_R_ReadCacheRecords_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetNetworkServers_s(handle_t Binding);
extern long WINAPI resolver_R_DnsZtNotifyDnscache_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetZtFilterIds_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetZtConfig_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetZtTrustedServers_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetZtRules_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetZtClientCertConfig_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetZtTrustedCa_s(handle_t Binding);
extern long WINAPI resolver_R_DnsGetZtSettings_s(handle_t Binding);
extern long WINAPI resolver_R_DnsUpdate_s(handle_t Binding);
#endif
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char resolver_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x11, 0x14, 0x02, 0x00, /* 2: pointer */
    0x12, 0x00, 0x02, 0x00, /* 6: pointer */
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x09, 0x00, 0x36, 0x36, 0x06, 0x06, /* 10: struct DNS_RPC_CACHE_ENTRY (BOGUS) */
    0x06, 0x3e, 0x5b, 0x12, 0x00, 0xef, 0xff, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x08, 0x25, 0x5c, /* 33: pointer */
#else
    0x00, 0x00, /* 0: pad */
    0x11, 0x14, 0x02, 0x00, /* 2: pointer */
    0x12, 0x00, 0x02, 0x00, /* 6: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x09, 0x00, 0x36, 0x36, 0x06, 0x06, /* 10: struct DNS_RPC_CACHE_ENTRY (BOGUS) */
    0x06, 0x3e, 0x5b, 0x12, 0x00, 0xef, 0xff, 0x12, 0x08, 0x25, 0x5c,
    0x12, 0x08, 0x25, 0x5c, /* 33: pointer */
#endif
};

static const unsigned char resolver_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x0f, 0x00, /* 2: resolver.R_ReadCache */
    0x07, 0x00, 0x45, 0x03, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x00, 0x08, 0x00, 0x0b, 0x5c, 0x13, 0x20, 0x10, 0x00,
    0x02, 0x00, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 46: resolver.R_ResolverGetConfig */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 78: resolver.R_ResolverFlushCache */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 110: resolver.R_ResolverFlushCacheEntry */
    0x07, 0x00, 0x46, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x21, 0x00, 0x70, 0x00, 0x10, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 148: resolver.R_ResolverQuery */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 180: resolver.R_DnsRegisterLocal */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 212: resolver.R_DnsDeRegisterLocal */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 244: resolver.R_DnsStartMulticastQuery */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 276: resolver.R_DnsGetMulticastData */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x09, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 308: resolver.R_DnsStopMulticastQuery */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0a, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 340: resolver.R_ResolverSimpleOp */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0b, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 372: resolver.R_DnsGetProxyInformation */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0c, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 404: resolver.R_DnsGetPolicyTableInfo */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0d, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 436: resolver.R_DnsSetConnectionPolicyInfo */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0e, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 468: resolver.R_DnsDeleteConnectionPolicyInfo */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0f, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 500: resolver.R_DnsGetSettings */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x10, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 532: resolver.R_DnsGetSettingsImpersonated */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x11, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 564: resolver.R_DnsSetSettings */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x12, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 596: resolver.R_DnsSetSettingsImpersonated */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x13, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 628: resolver.R_DnsGetInterfaceSettings */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x14, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 660: resolver.R_DnsGetInterfaceSettingsImpersonated */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x15, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 692: resolver.R_DnsSetInterfaceSettings */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x16, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 724: resolver.R_DnsSetInterfaceSettingsImpersonated */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x17, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 756: resolver.R_DnsGetAdaptersInfo */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x18, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 788: resolver.R_DnsSetNrptRules */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x19, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 820: resolver.R_DnsSetWellKnownServerProperty */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1a, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 852: resolver.R_DnsDeleteWellKnownServer */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1b, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 884: resolver.R_DnsGetWellKnownServerProperty */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1c, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 916: resolver.R_DnsGetWellKnownServers */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1d, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 948: resolver.R_DnsDeleteSettings */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1e, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 980: resolver.R_DnsGetDdrInfo */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1f, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1012: resolver.R_ReadCacheRecords */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x20, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1044: resolver.R_DnsGetNetworkServers */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x21, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1076: resolver.R_DnsZtNotifyDnscache */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x22, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1108: resolver.R_DnsGetZtFilterIds */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x23, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1140: resolver.R_DnsGetZtConfig */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x24, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1172: resolver.R_DnsGetZtTrustedServers */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x25, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1204: resolver.R_DnsGetZtRules */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x26, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1236: resolver.R_DnsGetZtClientCertConfig */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x27, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1268: resolver.R_DnsGetZtTrustedCa */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x28, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1300: resolver.R_DnsGetZtSettings */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x29, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1332: resolver.R_DnsUpdate */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x14, 0x00, 0x32, 0x00, 0x00, 0x00, 0x0f, 0x00, /* 2: resolver.R_ReadCache */
    0x07, 0x00, 0x45, 0x03, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x00, 0x04, 0x00, 0x0b, 0x5c, 0x13, 0x20, 0x0c, 0x00, 0x02, 0x00,
    0x70, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 44: resolver.R_ResolverGetConfig */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 74: resolver.R_ResolverFlushCache */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x0c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 104: resolver.R_ResolverFlushCacheEntry */
    0x07, 0x00, 0x46, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x04, 0x00, 0x21, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 140: resolver.R_ResolverQuery */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 170: resolver.R_DnsRegisterLocal */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 200: resolver.R_DnsDeRegisterLocal */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 230: resolver.R_DnsStartMulticastQuery */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 260: resolver.R_DnsGetMulticastData */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x09, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 290: resolver.R_DnsStopMulticastQuery */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0a, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 320: resolver.R_ResolverSimpleOp */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0b, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 350: resolver.R_DnsGetProxyInformation */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0c, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 380: resolver.R_DnsGetPolicyTableInfo */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0d, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 410: resolver.R_DnsSetConnectionPolicyInfo */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0e, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 440: resolver.R_DnsDeleteConnectionPolicyInfo */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0f, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 470: resolver.R_DnsGetSettings */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x10, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 500: resolver.R_DnsGetSettingsImpersonated */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x11, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 530: resolver.R_DnsSetSettings */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x12, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 560: resolver.R_DnsSetSettingsImpersonated */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x13, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 590: resolver.R_DnsGetInterfaceSettings */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x14, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 620: resolver.R_DnsGetInterfaceSettingsImpersonated */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x15, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 650: resolver.R_DnsSetInterfaceSettings */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x16, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 680: resolver.R_DnsSetInterfaceSettingsImpersonated */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x17, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 710: resolver.R_DnsGetAdaptersInfo */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x18, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 740: resolver.R_DnsSetNrptRules */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x19, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 770: resolver.R_DnsSetWellKnownServerProperty */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1a, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 800: resolver.R_DnsDeleteWellKnownServer */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1b, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 830: resolver.R_DnsGetWellKnownServerProperty */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1c, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 860: resolver.R_DnsGetWellKnownServers */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1d, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 890: resolver.R_DnsDeleteSettings */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1e, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 920: resolver.R_DnsGetDdrInfo */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1f, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 950: resolver.R_ReadCacheRecords */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x20, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 980: resolver.R_DnsGetNetworkServers */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x21, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1010: resolver.R_DnsZtNotifyDnscache */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x22, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1040: resolver.R_DnsGetZtFilterIds */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x23, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1070: resolver.R_DnsGetZtConfig */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x24, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1100: resolver.R_DnsGetZtTrustedServers */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x25, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1130: resolver.R_DnsGetZtRules */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x26, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1160: resolver.R_DnsGetZtClientCertConfig */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x27, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1190: resolver.R_DnsGetZtTrustedCa */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x28, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1220: resolver.R_DnsGetZtSettings */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x29, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1250: resolver.R_DnsUpdate */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short resolver_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 46, 78, 110, 148, 180, 212, 244, 276, 308, 340, 372, 404, 436, 468, 500, 532, 564, 596, 628, 660, 692, 724, 756, 788, 820, 852, 884, 916, 948, 980, 1012, 1044, 1076, 1108, 1140, 1172, 1204, 1236, 1268, 1300, 1332
#else
    2, 44, 74, 104, 140, 170, 200, 230, 260, 290, 320, 350, 380, 410, 440, 470, 500, 530, 560, 590, 620, 650, 680, 710, 740, 770, 800, 830, 860, 890, 920, 950, 980, 1010, 1040, 1070, 1100, 1130, 1160, 1190, 1220, 1250
#endif
};

static RPC_CLIENT_INTERFACE resolver_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x45776b01, 0x5956, 0x4485, {0x9f, 0x80, 0xf4, 0x28, 0xf7, 0xd6, 0x01, 0x29}}, {2, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE resolver_c_ifspec = (RPC_IF_HANDLE)&resolver_c_ifspec_data;

static MIDL_STUB_DESC resolver_StubDesc = {
    .RpcInterfaceInformation = &resolver_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .pFormatTypes = resolver_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef RESOLVER_CLIENT_ONLY
static const SERVER_ROUTINE resolver_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)resolver_R_ReadCache_s,
    (SERVER_ROUTINE)(void *)resolver_R_ResolverGetConfig_s,
    (SERVER_ROUTINE)(void *)resolver_R_ResolverFlushCache_s,
    (SERVER_ROUTINE)(void *)resolver_R_ResolverFlushCacheEntry_s,
    (SERVER_ROUTINE)(void *)resolver_R_ResolverQuery_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsRegisterLocal_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsDeRegisterLocal_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsStartMulticastQuery_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetMulticastData_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsStopMulticastQuery_s,
    (SERVER_ROUTINE)(void *)resolver_R_ResolverSimpleOp_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetProxyInformation_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetPolicyTableInfo_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsSetConnectionPolicyInfo_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsDeleteConnectionPolicyInfo_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetSettings_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetSettingsImpersonated_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsSetSettings_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsSetSettingsImpersonated_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetInterfaceSettings_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetInterfaceSettingsImpersonated_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsSetInterfaceSettings_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsSetInterfaceSettingsImpersonated_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetAdaptersInfo_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsSetNrptRules_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsSetWellKnownServerProperty_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsDeleteWellKnownServer_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetWellKnownServerProperty_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetWellKnownServers_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsDeleteSettings_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetDdrInfo_s,
    (SERVER_ROUTINE)(void *)resolver_R_ReadCacheRecords_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetNetworkServers_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsZtNotifyDnscache_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetZtFilterIds_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetZtConfig_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetZtTrustedServers_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetZtRules_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetZtClientCertConfig_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetZtTrustedCa_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsGetZtSettings_s,
    (SERVER_ROUTINE)(void *)resolver_R_DnsUpdate_s
};

int (*resolver_hook)(PRPC_MESSAGE);

static void __RPC_STUB resolver_dispatch_0(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_1(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_2(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_3(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_4(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_5(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_6(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_7(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_8(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_9(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_10(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_11(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_12(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_13(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_14(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_15(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_16(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_17(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_18(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_19(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_20(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_21(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_22(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_23(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_24(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_25(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_26(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_27(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_28(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_29(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_30(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_31(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_32(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_33(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_34(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_35(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_36(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_37(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_38(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_39(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_40(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB resolver_dispatch_41(PRPC_MESSAGE m)
{
    if (resolver_hook && resolver_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION resolver_DispatchFunctions[] = {
resolver_dispatch_0, resolver_dispatch_1, resolver_dispatch_2, resolver_dispatch_3, resolver_dispatch_4, resolver_dispatch_5, resolver_dispatch_6, resolver_dispatch_7, resolver_dispatch_8, resolver_dispatch_9, resolver_dispatch_10, resolver_dispatch_11, resolver_dispatch_12, resolver_dispatch_13, resolver_dispatch_14, resolver_dispatch_15, resolver_dispatch_16, resolver_dispatch_17, resolver_dispatch_18, resolver_dispatch_19, resolver_dispatch_20, resolver_dispatch_21, resolver_dispatch_22, resolver_dispatch_23, resolver_dispatch_24, resolver_dispatch_25, resolver_dispatch_26, resolver_dispatch_27, resolver_dispatch_28, resolver_dispatch_29, resolver_dispatch_30, resolver_dispatch_31, resolver_dispatch_32, resolver_dispatch_33, resolver_dispatch_34, resolver_dispatch_35, resolver_dispatch_36, resolver_dispatch_37, resolver_dispatch_38, resolver_dispatch_39, resolver_dispatch_40, resolver_dispatch_41
};
static RPC_DISPATCH_TABLE resolver_DispatchTable = { 42, resolver_DispatchFunctions, 0 };
static MIDL_SERVER_INFO resolver_ServerInfo = {
    .pStubDesc = &resolver_StubDesc,
    .DispatchTable = resolver_ServerRoutineTable,
    .ProcString = resolver_ProcFormat,
    .FmtStringOffset = resolver_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE resolver_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x45776b01, 0x5956, 0x4485, {0x9f, 0x80, 0xf4, 0x28, 0xf7, 0xd6, 0x01, 0x29}}, {2, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &resolver_DispatchTable, 0, 0, 0, &resolver_ServerInfo, 0 };
RPC_IF_HANDLE resolver_s_ifspec = (RPC_IF_HANDLE)&resolver_s_ifspec_data;
#endif

static NDRGEN_UNUSED long WINAPI resolver_R_ReadCache(handle_t Binding, long long Flags, DNS_RPC_CACHE_ENTRY **ppCacheTable)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[0], Binding, Flags, ppCacheTable).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
        long long Flags;
        DNS_RPC_CACHE_ENTRY **ppCacheTable;
    } frame = { Binding, Flags, ppCacheTable };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_ResolverGetConfig(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[1], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[1], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_ResolverFlushCache(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[2], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[2], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_ResolverFlushCacheEntry(handle_t Binding, unsigned short *pwsName)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[3], Binding, pwsName).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
        unsigned short *pwsName;
    } frame = { Binding, pwsName };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[3], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_ResolverQuery(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[4], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[4], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsRegisterLocal(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[5], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[5], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsDeRegisterLocal(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[6], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[6], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsStartMulticastQuery(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[7], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[7], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetMulticastData(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[8], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[8], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsStopMulticastQuery(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[9], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[9], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_ResolverSimpleOp(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[10], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[10], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetProxyInformation(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[11], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[11], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetPolicyTableInfo(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[12], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[12], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsSetConnectionPolicyInfo(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[13], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[13], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsDeleteConnectionPolicyInfo(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[14], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[14], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetSettings(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[15], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[15], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetSettingsImpersonated(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[16], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[16], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsSetSettings(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[17], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[17], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsSetSettingsImpersonated(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[18], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[18], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetInterfaceSettings(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[19], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[19], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetInterfaceSettingsImpersonated(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[20], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[20], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsSetInterfaceSettings(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[21], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[21], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsSetInterfaceSettingsImpersonated(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[22], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[22], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetAdaptersInfo(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[23], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[23], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsSetNrptRules(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[24], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[24], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsSetWellKnownServerProperty(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[25], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[25], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsDeleteWellKnownServer(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[26], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[26], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetWellKnownServerProperty(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[27], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[27], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetWellKnownServers(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[28], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[28], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsDeleteSettings(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[29], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[29], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetDdrInfo(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[30], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[30], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_ReadCacheRecords(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[31], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[31], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetNetworkServers(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[32], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[32], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsZtNotifyDnscache(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[33], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[33], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetZtFilterIds(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[34], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[34], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetZtConfig(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[35], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[35], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetZtTrustedServers(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[36], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[36], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetZtRules(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[37], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[37], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetZtClientCertConfig(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[38], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[38], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetZtTrustedCa(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[39], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[39], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsGetZtSettings(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[40], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[40], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI resolver_R_DnsUpdate(handle_t Binding)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[41], Binding).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
    } frame = { Binding };
#pragma pack(pop)
    return (long)NdrClientCall2(&resolver_StubDesc, resolver_ProcFormat + resolver_FormatStringOffsetTable[41], (unsigned char *)&frame).Simple;
#endif
}

/* ------------------------------------------------------------------ interface mimeresolver */

#ifndef MIMERESOLVER_CLIENT_ONLY
extern long WINAPI mimeresolver_MimeResolverLookup_s(handle_t Binding, char *Name, unsigned short Type, long long Options, byte *Buffer, unsigned long cbBuffer, unsigned long *cbNeeded);
#endif
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char mimeresolver_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x11, 0x08, 0x22, 0x5c, /* 2: pointer */
    0x11, 0x04, 0x02, 0x00, /* 6: pointer */
    0x1b, 0x00, 0x01, 0x00, 0x28, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 10: array */
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, /* 22: range */
#else
    0x00, 0x00, /* 0: pad */
    0x11, 0x08, 0x22, 0x5c, /* 2: pointer */
    0x11, 0x04, 0x02, 0x00, /* 6: pointer */
    0x1b, 0x00, 0x01, 0x00, 0x28, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 10: array */
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, /* 22: range */
#endif
};

static const unsigned char mimeresolver_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x40, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 2: mimeresolver.MimeResolverLookup */
    0x0f, 0x00, 0x47, 0x07, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x06, 0x5c, 0x48, 0x00, 0x18, 0x00, 0x0b, 0x5c, 0x13, 0x00, 0x20, 0x00,
    0x06, 0x00, 0x88, 0x00, 0x28, 0x00, 0x16, 0x00, 0x50, 0x21, 0x30, 0x00,
    0x08, 0x5c, 0x70, 0x00, 0x38, 0x00, 0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x24, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 2: mimeresolver.MimeResolverLookup */
    0x0f, 0x00, 0x47, 0x07, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x06, 0x5c,
    0x48, 0x00, 0x0c, 0x00, 0x0b, 0x5c, 0x13, 0x00, 0x14, 0x00, 0x06, 0x00,
    0x88, 0x00, 0x18, 0x00, 0x16, 0x00, 0x50, 0x21, 0x1c, 0x00, 0x08, 0x5c,
    0x70, 0x00, 0x20, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short mimeresolver_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2
#else
    2
#endif
};

static RPC_CLIENT_INTERFACE mimeresolver_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0xb8e7a1d4, 0x5c2f, 0x4e0b, {0x9a, 0x61, 0x3f, 0x2d, 0x7c, 0x9e, 0x4b, 0x15}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE mimeresolver_c_ifspec = (RPC_IF_HANDLE)&mimeresolver_c_ifspec_data;

static MIDL_STUB_DESC mimeresolver_StubDesc = {
    .RpcInterfaceInformation = &mimeresolver_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .pFormatTypes = mimeresolver_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef MIMERESOLVER_CLIENT_ONLY
static const SERVER_ROUTINE mimeresolver_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)mimeresolver_MimeResolverLookup_s
};

int (*mimeresolver_hook)(PRPC_MESSAGE);

static void __RPC_STUB mimeresolver_dispatch_0(PRPC_MESSAGE m)
{
    if (mimeresolver_hook && mimeresolver_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION mimeresolver_DispatchFunctions[] = {
mimeresolver_dispatch_0
};
static RPC_DISPATCH_TABLE mimeresolver_DispatchTable = { 1, mimeresolver_DispatchFunctions, 0 };
static MIDL_SERVER_INFO mimeresolver_ServerInfo = {
    .pStubDesc = &mimeresolver_StubDesc,
    .DispatchTable = mimeresolver_ServerRoutineTable,
    .ProcString = mimeresolver_ProcFormat,
    .FmtStringOffset = mimeresolver_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE mimeresolver_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0xb8e7a1d4, 0x5c2f, 0x4e0b, {0x9a, 0x61, 0x3f, 0x2d, 0x7c, 0x9e, 0x4b, 0x15}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &mimeresolver_DispatchTable, 0, 0, 0, &mimeresolver_ServerInfo, 0 };
RPC_IF_HANDLE mimeresolver_s_ifspec = (RPC_IF_HANDLE)&mimeresolver_s_ifspec_data;
#endif

static NDRGEN_UNUSED long WINAPI mimeresolver_MimeResolverLookup(handle_t Binding, char *Name, unsigned short Type, long long Options, byte *Buffer, unsigned long cbBuffer, unsigned long *cbNeeded)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&mimeresolver_StubDesc, mimeresolver_ProcFormat + mimeresolver_FormatStringOffsetTable[0], Binding, Name, Type, Options, Buffer, cbBuffer, cbNeeded).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t Binding;
        char *Name;
        long Type;
        long long Options;
        byte *Buffer;
        unsigned long cbBuffer;
        unsigned long *cbNeeded;
    } frame = { Binding, Name, Type, Options, Buffer, cbBuffer, cbNeeded };
#pragma pack(pop)
    return (long)NdrClientCall2(&mimeresolver_StubDesc, mimeresolver_ProcFormat + mimeresolver_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

#endif
