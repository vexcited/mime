#ifndef NDRGEN_LCLOR_H
#define NDRGEN_LCLOR_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))

typedef struct PROXY_DECODE_INFO PROXY_DECODE_INFO;
typedef struct DUALSTRINGARRAY DUALSTRINGARRAY;
typedef struct SECPKG SECPKG;
typedef struct OXID_OID_PAIR OXID_OID_PAIR;
typedef struct OXID_REF OXID_REF;
typedef struct OID_UNMARSHAL OID_UNMARSHAL;
typedef struct COMVERSION COMVERSION;
typedef struct CONTAINER_EXTENSION CONTAINER_EXTENSION;
typedef struct CONTAINER_EXTENSIONS CONTAINER_EXTENSIONS;
typedef struct CONTAINERVERSION CONTAINERVERSION;
typedef struct HSTRING_WIRE HSTRING_WIRE;
typedef struct OXID_INFO OXID_INFO;
typedef struct PROCESS_ACTIVATOR_TOKEN PROCESS_ACTIVATOR_TOKEN;

struct PROXY_DECODE_INFO {
    unsigned long offsetofChannelPtrFromProxyPtr;
    unsigned long long addressofOIDListHead;
    unsigned long sizeofIPIDEntry;
    unsigned long offsetofNextIPIDPointerInIPIDEntry;
    unsigned long offsetofFlagsInIPIDEntry;
    unsigned long offsetofIPIDInIPIDEntry;
    unsigned long offsetofServerAddressInIPIDEntry;
    unsigned long offsetofOIDFLinkInIPIDEntry;
    unsigned long serverIPIDEntryFlag;
    unsigned long invalidIPIDEntryFlags;
    unsigned long sizeofOxidEntry;
    unsigned long offsetofPidInOxidEntry;
    unsigned long offsetofTidInOxidEntry;
    unsigned long sizeofCClientChannel;
    unsigned long offsetofCanonicalIRpcChannelBufferInCClientChannel;
    unsigned long offsetofIPIDEntryInCClientChannel;
    unsigned long offsetofOxidEntryInCClientChannel;
    unsigned long offsetofSignatureInCClientChannel;
    GUID guidSignatureofCClientChannel;
};
typedef void *PHPROCESS;
struct DUALSTRINGARRAY {
    unsigned short wNumEntries;
    unsigned short wSecurityOffset;
    unsigned short aStringArray[1];
};
struct SECPKG {
    unsigned short *pName;
    unsigned short wId;
};
struct OXID_OID_PAIR {
    unsigned long long mid;
    unsigned long long oxid;
    unsigned long long oid;
    unsigned long flags;
};
struct OXID_REF {
    unsigned long long mid;
    unsigned long long oxid;
    unsigned long refs;
};
struct OID_UNMARSHAL {
    unsigned long long mid;
    unsigned long long oid;
    unsigned long unmarshalCount;
};
struct COMVERSION {
    unsigned short MajorVersion;
    unsigned short MinorVersion;
};
struct CONTAINER_EXTENSION {
    unsigned long id;
    unsigned long version;
    unsigned long size;
    byte data[1];
};
struct CONTAINER_EXTENSIONS {
    unsigned long size;
    unsigned long reserved;
    CONTAINER_EXTENSION **extensions;
};
struct CONTAINERVERSION {
    unsigned long version;
    unsigned long long capabilityFlags;
    CONTAINER_EXTENSIONS *extensions;
};
typedef enum OR_OXID_CLIENT_DEPENDENCY { OR_OXID_CLIENT_DEPENDENCY_NONE, OR_OXID_CLIENT_DEPENDENCY_UNIDIRECTIONAL, OR_OXID_CLIENT_DEPENDENCY_BIDIRECTIONAL } OR_OXID_CLIENT_DEPENDENCY;
struct HSTRING_WIRE {
    unsigned long Magic;
    unsigned long Bytes;
    byte Data[1];
};
typedef struct RPCSS_HSTRING_DATA * RPCSS_HSTRING;
struct OXID_INFO {
    unsigned long dwTid;
    unsigned long dwPid;
    unsigned long dwAuthnHint;
    COMVERSION dcomVersion;
    CONTAINERVERSION containerVersion;
    GUID ipidRemUnknown;
    unsigned long dwFlags;
    DUALSTRINGARRAY *psa;
    GUID guidProcessIdentifier;
    unsigned long long processHostId;
    OR_OXID_CLIENT_DEPENDENCY clientDependencyBehavior;
    RPCSS_HSTRING packageFullName;
    RPCSS_HSTRING userSid;
    RPCSS_HSTRING appcontainerSid;
    unsigned long long primaryOxid;
    GUID primaryIpidRemUnknown;
};
struct PROCESS_ACTIVATOR_TOKEN {
    GUID processGuid;
    GUID activatorGuid;
    unsigned long flags;
};

#ifdef _WIN64
_Static_assert(sizeof(GUID) == 16, "size of GUID");
_Static_assert(__builtin_offsetof(GUID, Data1) == 0, "GUID.Data1");
_Static_assert(__builtin_offsetof(GUID, Data2) == 4, "GUID.Data2");
_Static_assert(__builtin_offsetof(GUID, Data3) == 6, "GUID.Data3");
_Static_assert(__builtin_offsetof(GUID, Data4) == 8, "GUID.Data4");
_Static_assert(sizeof(PROXY_DECODE_INFO) == 96, "size of PROXY_DECODE_INFO");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofChannelPtrFromProxyPtr) == 0, "PROXY_DECODE_INFO.offsetofChannelPtrFromProxyPtr");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, addressofOIDListHead) == 8, "PROXY_DECODE_INFO.addressofOIDListHead");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, sizeofIPIDEntry) == 16, "PROXY_DECODE_INFO.sizeofIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofNextIPIDPointerInIPIDEntry) == 20, "PROXY_DECODE_INFO.offsetofNextIPIDPointerInIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofFlagsInIPIDEntry) == 24, "PROXY_DECODE_INFO.offsetofFlagsInIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofIPIDInIPIDEntry) == 28, "PROXY_DECODE_INFO.offsetofIPIDInIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofServerAddressInIPIDEntry) == 32, "PROXY_DECODE_INFO.offsetofServerAddressInIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofOIDFLinkInIPIDEntry) == 36, "PROXY_DECODE_INFO.offsetofOIDFLinkInIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, serverIPIDEntryFlag) == 40, "PROXY_DECODE_INFO.serverIPIDEntryFlag");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, invalidIPIDEntryFlags) == 44, "PROXY_DECODE_INFO.invalidIPIDEntryFlags");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, sizeofOxidEntry) == 48, "PROXY_DECODE_INFO.sizeofOxidEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofPidInOxidEntry) == 52, "PROXY_DECODE_INFO.offsetofPidInOxidEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofTidInOxidEntry) == 56, "PROXY_DECODE_INFO.offsetofTidInOxidEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, sizeofCClientChannel) == 60, "PROXY_DECODE_INFO.sizeofCClientChannel");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofCanonicalIRpcChannelBufferInCClientChannel) == 64, "PROXY_DECODE_INFO.offsetofCanonicalIRpcChannelBufferInCClientChannel");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofIPIDEntryInCClientChannel) == 68, "PROXY_DECODE_INFO.offsetofIPIDEntryInCClientChannel");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofOxidEntryInCClientChannel) == 72, "PROXY_DECODE_INFO.offsetofOxidEntryInCClientChannel");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofSignatureInCClientChannel) == 76, "PROXY_DECODE_INFO.offsetofSignatureInCClientChannel");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, guidSignatureofCClientChannel) == 80, "PROXY_DECODE_INFO.guidSignatureofCClientChannel");
_Static_assert(sizeof(PHPROCESS) == 8, "size of PHPROCESS");
_Static_assert(sizeof(DUALSTRINGARRAY) == 6, "size of DUALSTRINGARRAY");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, wNumEntries) == 0, "DUALSTRINGARRAY.wNumEntries");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, wSecurityOffset) == 2, "DUALSTRINGARRAY.wSecurityOffset");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, aStringArray) == 4, "DUALSTRINGARRAY.aStringArray");
_Static_assert(sizeof(SECPKG) == 16, "size of SECPKG");
_Static_assert(__builtin_offsetof(SECPKG, pName) == 0, "SECPKG.pName");
_Static_assert(__builtin_offsetof(SECPKG, wId) == 8, "SECPKG.wId");
_Static_assert(sizeof(OXID_OID_PAIR) == 32, "size of OXID_OID_PAIR");
_Static_assert(__builtin_offsetof(OXID_OID_PAIR, mid) == 0, "OXID_OID_PAIR.mid");
_Static_assert(__builtin_offsetof(OXID_OID_PAIR, oxid) == 8, "OXID_OID_PAIR.oxid");
_Static_assert(__builtin_offsetof(OXID_OID_PAIR, oid) == 16, "OXID_OID_PAIR.oid");
_Static_assert(__builtin_offsetof(OXID_OID_PAIR, flags) == 24, "OXID_OID_PAIR.flags");
_Static_assert(sizeof(OXID_REF) == 24, "size of OXID_REF");
_Static_assert(__builtin_offsetof(OXID_REF, mid) == 0, "OXID_REF.mid");
_Static_assert(__builtin_offsetof(OXID_REF, oxid) == 8, "OXID_REF.oxid");
_Static_assert(__builtin_offsetof(OXID_REF, refs) == 16, "OXID_REF.refs");
_Static_assert(sizeof(OID_UNMARSHAL) == 24, "size of OID_UNMARSHAL");
_Static_assert(__builtin_offsetof(OID_UNMARSHAL, mid) == 0, "OID_UNMARSHAL.mid");
_Static_assert(__builtin_offsetof(OID_UNMARSHAL, oid) == 8, "OID_UNMARSHAL.oid");
_Static_assert(__builtin_offsetof(OID_UNMARSHAL, unmarshalCount) == 16, "OID_UNMARSHAL.unmarshalCount");
_Static_assert(sizeof(COMVERSION) == 4, "size of COMVERSION");
_Static_assert(__builtin_offsetof(COMVERSION, MajorVersion) == 0, "COMVERSION.MajorVersion");
_Static_assert(__builtin_offsetof(COMVERSION, MinorVersion) == 2, "COMVERSION.MinorVersion");
_Static_assert(sizeof(CONTAINER_EXTENSION) == 16, "size of CONTAINER_EXTENSION");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSION, id) == 0, "CONTAINER_EXTENSION.id");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSION, version) == 4, "CONTAINER_EXTENSION.version");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSION, size) == 8, "CONTAINER_EXTENSION.size");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSION, data) == 12, "CONTAINER_EXTENSION.data");
_Static_assert(sizeof(CONTAINER_EXTENSIONS) == 16, "size of CONTAINER_EXTENSIONS");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSIONS, size) == 0, "CONTAINER_EXTENSIONS.size");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSIONS, reserved) == 4, "CONTAINER_EXTENSIONS.reserved");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSIONS, extensions) == 8, "CONTAINER_EXTENSIONS.extensions");
_Static_assert(sizeof(CONTAINERVERSION) == 24, "size of CONTAINERVERSION");
_Static_assert(__builtin_offsetof(CONTAINERVERSION, version) == 0, "CONTAINERVERSION.version");
_Static_assert(__builtin_offsetof(CONTAINERVERSION, capabilityFlags) == 8, "CONTAINERVERSION.capabilityFlags");
_Static_assert(__builtin_offsetof(CONTAINERVERSION, extensions) == 16, "CONTAINERVERSION.extensions");
_Static_assert(sizeof(OR_OXID_CLIENT_DEPENDENCY) == 4, "size of OR_OXID_CLIENT_DEPENDENCY");
_Static_assert(sizeof(HSTRING_WIRE) == 12, "size of HSTRING_WIRE");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Magic) == 0, "HSTRING_WIRE.Magic");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Bytes) == 4, "HSTRING_WIRE.Bytes");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Data) == 8, "HSTRING_WIRE.Data");
_Static_assert(sizeof(RPCSS_HSTRING) == 8, "size of RPCSS_HSTRING");
_Static_assert(sizeof(OXID_INFO) == 152, "size of OXID_INFO");
_Static_assert(__builtin_offsetof(OXID_INFO, dwTid) == 0, "OXID_INFO.dwTid");
_Static_assert(__builtin_offsetof(OXID_INFO, dwPid) == 4, "OXID_INFO.dwPid");
_Static_assert(__builtin_offsetof(OXID_INFO, dwAuthnHint) == 8, "OXID_INFO.dwAuthnHint");
_Static_assert(__builtin_offsetof(OXID_INFO, dcomVersion) == 12, "OXID_INFO.dcomVersion");
_Static_assert(__builtin_offsetof(OXID_INFO, containerVersion) == 16, "OXID_INFO.containerVersion");
_Static_assert(__builtin_offsetof(OXID_INFO, ipidRemUnknown) == 40, "OXID_INFO.ipidRemUnknown");
_Static_assert(__builtin_offsetof(OXID_INFO, dwFlags) == 56, "OXID_INFO.dwFlags");
_Static_assert(__builtin_offsetof(OXID_INFO, psa) == 64, "OXID_INFO.psa");
_Static_assert(__builtin_offsetof(OXID_INFO, guidProcessIdentifier) == 72, "OXID_INFO.guidProcessIdentifier");
_Static_assert(__builtin_offsetof(OXID_INFO, processHostId) == 88, "OXID_INFO.processHostId");
_Static_assert(__builtin_offsetof(OXID_INFO, clientDependencyBehavior) == 96, "OXID_INFO.clientDependencyBehavior");
_Static_assert(__builtin_offsetof(OXID_INFO, packageFullName) == 104, "OXID_INFO.packageFullName");
_Static_assert(__builtin_offsetof(OXID_INFO, userSid) == 112, "OXID_INFO.userSid");
_Static_assert(__builtin_offsetof(OXID_INFO, appcontainerSid) == 120, "OXID_INFO.appcontainerSid");
_Static_assert(__builtin_offsetof(OXID_INFO, primaryOxid) == 128, "OXID_INFO.primaryOxid");
_Static_assert(__builtin_offsetof(OXID_INFO, primaryIpidRemUnknown) == 136, "OXID_INFO.primaryIpidRemUnknown");
_Static_assert(sizeof(PROCESS_ACTIVATOR_TOKEN) == 36, "size of PROCESS_ACTIVATOR_TOKEN");
_Static_assert(__builtin_offsetof(PROCESS_ACTIVATOR_TOKEN, processGuid) == 0, "PROCESS_ACTIVATOR_TOKEN.processGuid");
_Static_assert(__builtin_offsetof(PROCESS_ACTIVATOR_TOKEN, activatorGuid) == 16, "PROCESS_ACTIVATOR_TOKEN.activatorGuid");
_Static_assert(__builtin_offsetof(PROCESS_ACTIVATOR_TOKEN, flags) == 32, "PROCESS_ACTIVATOR_TOKEN.flags");
#else
_Static_assert(sizeof(GUID) == 16, "size of GUID");
_Static_assert(__builtin_offsetof(GUID, Data1) == 0, "GUID.Data1");
_Static_assert(__builtin_offsetof(GUID, Data2) == 4, "GUID.Data2");
_Static_assert(__builtin_offsetof(GUID, Data3) == 6, "GUID.Data3");
_Static_assert(__builtin_offsetof(GUID, Data4) == 8, "GUID.Data4");
_Static_assert(sizeof(PROXY_DECODE_INFO) == 96, "size of PROXY_DECODE_INFO");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofChannelPtrFromProxyPtr) == 0, "PROXY_DECODE_INFO.offsetofChannelPtrFromProxyPtr");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, addressofOIDListHead) == 8, "PROXY_DECODE_INFO.addressofOIDListHead");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, sizeofIPIDEntry) == 16, "PROXY_DECODE_INFO.sizeofIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofNextIPIDPointerInIPIDEntry) == 20, "PROXY_DECODE_INFO.offsetofNextIPIDPointerInIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofFlagsInIPIDEntry) == 24, "PROXY_DECODE_INFO.offsetofFlagsInIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofIPIDInIPIDEntry) == 28, "PROXY_DECODE_INFO.offsetofIPIDInIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofServerAddressInIPIDEntry) == 32, "PROXY_DECODE_INFO.offsetofServerAddressInIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofOIDFLinkInIPIDEntry) == 36, "PROXY_DECODE_INFO.offsetofOIDFLinkInIPIDEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, serverIPIDEntryFlag) == 40, "PROXY_DECODE_INFO.serverIPIDEntryFlag");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, invalidIPIDEntryFlags) == 44, "PROXY_DECODE_INFO.invalidIPIDEntryFlags");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, sizeofOxidEntry) == 48, "PROXY_DECODE_INFO.sizeofOxidEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofPidInOxidEntry) == 52, "PROXY_DECODE_INFO.offsetofPidInOxidEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofTidInOxidEntry) == 56, "PROXY_DECODE_INFO.offsetofTidInOxidEntry");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, sizeofCClientChannel) == 60, "PROXY_DECODE_INFO.sizeofCClientChannel");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofCanonicalIRpcChannelBufferInCClientChannel) == 64, "PROXY_DECODE_INFO.offsetofCanonicalIRpcChannelBufferInCClientChannel");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofIPIDEntryInCClientChannel) == 68, "PROXY_DECODE_INFO.offsetofIPIDEntryInCClientChannel");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofOxidEntryInCClientChannel) == 72, "PROXY_DECODE_INFO.offsetofOxidEntryInCClientChannel");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, offsetofSignatureInCClientChannel) == 76, "PROXY_DECODE_INFO.offsetofSignatureInCClientChannel");
_Static_assert(__builtin_offsetof(PROXY_DECODE_INFO, guidSignatureofCClientChannel) == 80, "PROXY_DECODE_INFO.guidSignatureofCClientChannel");
_Static_assert(sizeof(PHPROCESS) == 4, "size of PHPROCESS");
_Static_assert(sizeof(DUALSTRINGARRAY) == 6, "size of DUALSTRINGARRAY");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, wNumEntries) == 0, "DUALSTRINGARRAY.wNumEntries");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, wSecurityOffset) == 2, "DUALSTRINGARRAY.wSecurityOffset");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, aStringArray) == 4, "DUALSTRINGARRAY.aStringArray");
_Static_assert(sizeof(SECPKG) == 8, "size of SECPKG");
_Static_assert(__builtin_offsetof(SECPKG, pName) == 0, "SECPKG.pName");
_Static_assert(__builtin_offsetof(SECPKG, wId) == 4, "SECPKG.wId");
_Static_assert(sizeof(OXID_OID_PAIR) == 32, "size of OXID_OID_PAIR");
_Static_assert(__builtin_offsetof(OXID_OID_PAIR, mid) == 0, "OXID_OID_PAIR.mid");
_Static_assert(__builtin_offsetof(OXID_OID_PAIR, oxid) == 8, "OXID_OID_PAIR.oxid");
_Static_assert(__builtin_offsetof(OXID_OID_PAIR, oid) == 16, "OXID_OID_PAIR.oid");
_Static_assert(__builtin_offsetof(OXID_OID_PAIR, flags) == 24, "OXID_OID_PAIR.flags");
_Static_assert(sizeof(OXID_REF) == 24, "size of OXID_REF");
_Static_assert(__builtin_offsetof(OXID_REF, mid) == 0, "OXID_REF.mid");
_Static_assert(__builtin_offsetof(OXID_REF, oxid) == 8, "OXID_REF.oxid");
_Static_assert(__builtin_offsetof(OXID_REF, refs) == 16, "OXID_REF.refs");
_Static_assert(sizeof(OID_UNMARSHAL) == 24, "size of OID_UNMARSHAL");
_Static_assert(__builtin_offsetof(OID_UNMARSHAL, mid) == 0, "OID_UNMARSHAL.mid");
_Static_assert(__builtin_offsetof(OID_UNMARSHAL, oid) == 8, "OID_UNMARSHAL.oid");
_Static_assert(__builtin_offsetof(OID_UNMARSHAL, unmarshalCount) == 16, "OID_UNMARSHAL.unmarshalCount");
_Static_assert(sizeof(COMVERSION) == 4, "size of COMVERSION");
_Static_assert(__builtin_offsetof(COMVERSION, MajorVersion) == 0, "COMVERSION.MajorVersion");
_Static_assert(__builtin_offsetof(COMVERSION, MinorVersion) == 2, "COMVERSION.MinorVersion");
_Static_assert(sizeof(CONTAINER_EXTENSION) == 16, "size of CONTAINER_EXTENSION");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSION, id) == 0, "CONTAINER_EXTENSION.id");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSION, version) == 4, "CONTAINER_EXTENSION.version");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSION, size) == 8, "CONTAINER_EXTENSION.size");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSION, data) == 12, "CONTAINER_EXTENSION.data");
_Static_assert(sizeof(CONTAINER_EXTENSIONS) == 12, "size of CONTAINER_EXTENSIONS");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSIONS, size) == 0, "CONTAINER_EXTENSIONS.size");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSIONS, reserved) == 4, "CONTAINER_EXTENSIONS.reserved");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENSIONS, extensions) == 8, "CONTAINER_EXTENSIONS.extensions");
_Static_assert(sizeof(CONTAINERVERSION) == 24, "size of CONTAINERVERSION");
_Static_assert(__builtin_offsetof(CONTAINERVERSION, version) == 0, "CONTAINERVERSION.version");
_Static_assert(__builtin_offsetof(CONTAINERVERSION, capabilityFlags) == 8, "CONTAINERVERSION.capabilityFlags");
_Static_assert(__builtin_offsetof(CONTAINERVERSION, extensions) == 16, "CONTAINERVERSION.extensions");
_Static_assert(sizeof(OR_OXID_CLIENT_DEPENDENCY) == 4, "size of OR_OXID_CLIENT_DEPENDENCY");
_Static_assert(sizeof(HSTRING_WIRE) == 12, "size of HSTRING_WIRE");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Magic) == 0, "HSTRING_WIRE.Magic");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Bytes) == 4, "HSTRING_WIRE.Bytes");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Data) == 8, "HSTRING_WIRE.Data");
_Static_assert(sizeof(RPCSS_HSTRING) == 4, "size of RPCSS_HSTRING");
_Static_assert(sizeof(OXID_INFO) == 128, "size of OXID_INFO");
_Static_assert(__builtin_offsetof(OXID_INFO, dwTid) == 0, "OXID_INFO.dwTid");
_Static_assert(__builtin_offsetof(OXID_INFO, dwPid) == 4, "OXID_INFO.dwPid");
_Static_assert(__builtin_offsetof(OXID_INFO, dwAuthnHint) == 8, "OXID_INFO.dwAuthnHint");
_Static_assert(__builtin_offsetof(OXID_INFO, dcomVersion) == 12, "OXID_INFO.dcomVersion");
_Static_assert(__builtin_offsetof(OXID_INFO, containerVersion) == 16, "OXID_INFO.containerVersion");
_Static_assert(__builtin_offsetof(OXID_INFO, ipidRemUnknown) == 40, "OXID_INFO.ipidRemUnknown");
_Static_assert(__builtin_offsetof(OXID_INFO, dwFlags) == 56, "OXID_INFO.dwFlags");
_Static_assert(__builtin_offsetof(OXID_INFO, psa) == 60, "OXID_INFO.psa");
_Static_assert(__builtin_offsetof(OXID_INFO, guidProcessIdentifier) == 64, "OXID_INFO.guidProcessIdentifier");
_Static_assert(__builtin_offsetof(OXID_INFO, processHostId) == 80, "OXID_INFO.processHostId");
_Static_assert(__builtin_offsetof(OXID_INFO, clientDependencyBehavior) == 88, "OXID_INFO.clientDependencyBehavior");
_Static_assert(__builtin_offsetof(OXID_INFO, packageFullName) == 92, "OXID_INFO.packageFullName");
_Static_assert(__builtin_offsetof(OXID_INFO, userSid) == 96, "OXID_INFO.userSid");
_Static_assert(__builtin_offsetof(OXID_INFO, appcontainerSid) == 100, "OXID_INFO.appcontainerSid");
_Static_assert(__builtin_offsetof(OXID_INFO, primaryOxid) == 104, "OXID_INFO.primaryOxid");
_Static_assert(__builtin_offsetof(OXID_INFO, primaryIpidRemUnknown) == 112, "OXID_INFO.primaryIpidRemUnknown");
_Static_assert(sizeof(PROCESS_ACTIVATOR_TOKEN) == 36, "size of PROCESS_ACTIVATOR_TOKEN");
_Static_assert(__builtin_offsetof(PROCESS_ACTIVATOR_TOKEN, processGuid) == 0, "PROCESS_ACTIVATOR_TOKEN.processGuid");
_Static_assert(__builtin_offsetof(PROCESS_ACTIVATOR_TOKEN, activatorGuid) == 16, "PROCESS_ACTIVATOR_TOKEN.activatorGuid");
_Static_assert(__builtin_offsetof(PROCESS_ACTIVATOR_TOKEN, flags) == 32, "PROCESS_ACTIVATOR_TOKEN.flags");
#endif

/* ------------------------------------------------------------------ interface ILocalObjectExporter */

#ifndef ILOCALOBJECTEXPORTER_CLIENT_ONLY
extern error_status_t WINAPI ILocalObjectExporter_Connect_s(handle_t hServer, unsigned short *pwszWinstaDesktop, unsigned short *pwszExePath, PROXY_DECODE_INFO *pProxyDecodeInfo, unsigned long dwProcessFlags, unsigned short processArchitecture, PHPROCESS *pProcess, unsigned long *pdwTimeoutInSeconds, DUALSTRINGARRAY **ppdsaOrBindings, unsigned long long *pLocalMid, unsigned long cIdsToReserve, unsigned long long *aIdsReserved, unsigned long *pcIdsReserved, unsigned long *pfConnectFlags, unsigned long *pdwConnect3, unsigned long *pdwConnect4, unsigned short **pwszLegacySecurity, unsigned long *pAuthnLevel, unsigned long *pImpLevel, unsigned long *pcServerSvc, unsigned short **aServerSvc, unsigned long *pcClientSvc, SECPKG **aClientSvc, unsigned long *pcChannelHook, GUID **aChannelHook, unsigned long *pProcessID, unsigned long *pScmProcessID, unsigned long long *pSignature, GUID *pguidRPCSSProcessIdentifier, unsigned long *pdwSDSizes, unsigned long *pdwSDBlob, byte **pSDBlob);
extern error_status_t WINAPI ILocalObjectExporter_SetAppID_s(handle_t hRpc, PHPROCESS phProcess, GUID *guidAppID);
extern error_status_t WINAPI ILocalObjectExporter_GetDefaultSecurityPermissions_s(handle_t hRpc, unsigned long *pdwFlags, unsigned long *pcbSD, byte **ppSD);
extern error_status_t WINAPI ILocalObjectExporter_AllocateReservedIds_s(handle_t hRpc, PHPROCESS phProcess, unsigned long cIdsToAlloc, unsigned long long *aIdsAllocated, unsigned long *pcIdsAllocated);
extern error_status_t WINAPI ILocalObjectExporter_BulkUpdateOIDs_s(handle_t hRpc, PHPROCESS phProcess, unsigned long cOidsToBeAdded, OXID_OID_PAIR *aOidsToBeAdded, unsigned long *aStatusOfAdds, unsigned long cOidsToBeRemoved, OXID_OID_PAIR *aOidsToBeRemoved, unsigned long cServerOidsToFree, unsigned long long *aServerOids, unsigned long cServerOidsToUnPin, unsigned long long *aServerOidsToUnPin, unsigned long cClientOxidsToFree, OXID_REF *aClientOxids, unsigned long cOidsUnmarshaled, OID_UNMARSHAL *aOidsUnmarshaled);
extern error_status_t WINAPI ILocalObjectExporter_ClientResolveOXID_s(handle_t hRpc, PHPROCESS phProcess, unsigned long long poxidServer, DUALSTRINGARRAY *pssaServerObjectResolverBindings, OXID_INFO *poxidInfo, unsigned long long *pLocalMidOfRemote, unsigned long *pulMarshaledTargetInfoLength, byte **pucMarshaledTargetInfo, unsigned short *pAuthnSvc);
extern error_status_t WINAPI ILocalObjectExporter_ServerAllocateOXIDAndOIDs_s(handle_t hRpc, PHPROCESS phProcess, unsigned long long *poxidServer, unsigned long long *pPrimaryOxid, unsigned long cOids, unsigned long long *aOid, unsigned long *pcOidsAllocated, OXID_INFO *pOxidInfo, DUALSTRINGARRAY *pdsaStringBindings, DUALSTRINGARRAY *pdsaSecurityBindings, unsigned long long *pdwOrBindingsID, DUALSTRINGARRAY **ppdsaOrBindings);
extern error_status_t WINAPI ILocalObjectExporter_ServerAllocateOIDs_s(handle_t hRpc, PHPROCESS phProcess, unsigned long long *poxidServer, unsigned long cOidsReturn, unsigned long long *aOidsReturn, unsigned long cOids, unsigned long long *aOids, unsigned long *pcOidsAllocated);
extern error_status_t WINAPI ILocalObjectExporter_ServerFreeOXIDAndOIDs_s(handle_t hRpc, PHPROCESS phProcess, unsigned long long oxidServer, unsigned long cOids, unsigned long long *aOids);
extern error_status_t WINAPI ILocalObjectExporter_SetServerOIDFlags_s(handle_t hRpc, PHPROCESS phProcess, unsigned long long oid, unsigned long dwFlags);
extern error_status_t WINAPI ILocalObjectExporter_Disconnect_s(handle_t hRpc, PHPROCESS *pphProcess, unsigned long dwFlags);
extern error_status_t WINAPI ILocalObjectExporter_GetUpdatedResolverBindings_s(handle_t hRpc, PHPROCESS phProcess, DUALSTRINGARRAY **ppdsaOrBindings, unsigned long long *pdwBindingsID);
#endif
ULONG WINAPI RPCSS_HSTRING_UserSize(ULONG *, ULONG, RPCSS_HSTRING *);
unsigned char *WINAPI RPCSS_HSTRING_UserMarshal(ULONG *, unsigned char *, RPCSS_HSTRING *);
unsigned char *WINAPI RPCSS_HSTRING_UserUnmarshal(ULONG *, unsigned char *, RPCSS_HSTRING *);
void WINAPI RPCSS_HSTRING_UserFree(ULONG *, RPCSS_HSTRING *);
void WINAPI PHPROCESS_rundown(void *);
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char ILocalObjectExporter_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
    0x12, 0x00, 0x02, 0x00, /* 6: pointer */
    0x15, 0x07, 0x60, 0x00, 0x08, 0x40, 0x0b, 0x08, 0x08, 0x08, 0x08, 0x08, /* 10: struct PROXY_DECODE_INFO (STRUCT) */
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x43,
    0x43, 0x3e, 0x5b,
    0x30, 0xa0, 0x00, 0x00, /* 37: context handle PHPROCESS */
    0x11, 0x14, 0x02, 0x00, /* 41: pointer */
    0x12, 0x00, 0x02, 0x00, /* 45: pointer */
    0x17, 0x01, 0x04, 0x00, 0x05, 0x00, 0x06, 0x06, 0x5b, /* 49: struct DUALSTRINGARRAY (CSTRUCT) */
    0x1b, 0x01, 0x02, 0x00, 0x06, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x06, 0x5b, /* 58: array */
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, /* 70: range */
    0x11, 0x04, 0x02, 0x00, /* 80: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x50, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 84: array */
    0x11, 0x14, 0xa0, 0xff, /* 96: pointer */
    0x11, 0x14, 0x02, 0x00, /* 100: pointer */
    0x12, 0x00, 0x02, 0x00, /* 104: pointer */
    0x1b, 0x01, 0x02, 0x00, 0x28, 0x54, 0x98, 0x00, 0x00, 0x00, 0x06, 0x5b, /* 108: array */
    0x11, 0x14, 0x02, 0x00, /* 120: pointer */
    0x12, 0x00, 0x02, 0x00, /* 124: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x54, 0xa8, 0x00, 0x00, 0x00, 0xff, 0xff, /* 128: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x36, 0x06, 0x42, 0x5b, /* 149: struct SECPKG (BOGUS) */
    0x12, 0x08, 0x25, 0x5c,
    0x11, 0x14, 0x02, 0x00, /* 165: pointer */
    0x12, 0x00, 0x02, 0x00, /* 169: pointer */
    0x1b, 0x03, 0x10, 0x00, 0x28, 0x54, 0xb8, 0x00, 0x00, 0x00, 0x4c, 0x00, /* 173: array */
    0x03, 0x00, 0x5b,
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 188: struct GUID (STRUCT) */
    0x11, 0x04, 0x02, 0x00, /* 198: pointer */
    0x1d, 0x03, 0x14, 0x00, 0x08, 0x5b, /* 202: array */
    0x11, 0x14, 0x02, 0x00, /* 208: pointer */
    0x12, 0x00, 0x02, 0x00, /* 212: pointer */
    0x1b, 0x00, 0x01, 0x00, 0x28, 0x54, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 216: array */
    0x30, 0x40, 0x00, 0x00, /* 228: context handle PHPROCESS */
    0x11, 0x04, 0x02, 0x00, /* 232: pointer */
    0x1d, 0x03, 0x10, 0x00, 0x08, 0x5b, /* 236: array */
    0x11, 0x14, 0x02, 0x00, /* 242: pointer */
    0x12, 0x00, 0x02, 0x00, /* 246: pointer */
    0x1b, 0x00, 0x01, 0x00, 0x28, 0x54, 0x10, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 250: array */
    0x11, 0x04, 0x02, 0x00, /* 262: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x10, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 266: array */
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, /* 278: range */
    0x11, 0x00, 0x02, 0x00, /* 288: pointer */
    0x1b, 0x07, 0x20, 0x00, 0x28, 0x00, 0x10, 0x00, 0x00, 0x00, 0x4c, 0x00, /* 292: array */
    0x03, 0x00, 0x5b,
    0x15, 0x07, 0x20, 0x00, 0x0b, 0x0b, 0x0b, 0x08, 0x40, 0x5b, /* 307: struct OXID_OID_PAIR (STRUCT) */
    0x11, 0x04, 0x02, 0x00, /* 317: pointer */
    0x1b, 0x03, 0x04, 0x00, 0x28, 0x00, 0x10, 0x00, 0x00, 0x00, 0x08, 0x5b, /* 321: array */
    0x11, 0x00, 0x02, 0x00, /* 333: pointer */
    0x1b, 0x07, 0x20, 0x00, 0x28, 0x00, 0x28, 0x00, 0x00, 0x00, 0x4c, 0x00, /* 337: array */
    0xd6, 0xff, 0x5b,
    0x11, 0x00, 0x02, 0x00, /* 352: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x38, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 356: array */
    0x11, 0x00, 0x02, 0x00, /* 368: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x48, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 372: array */
    0x11, 0x00, 0x02, 0x00, /* 384: pointer */
    0x1b, 0x07, 0x18, 0x00, 0x28, 0x00, 0x58, 0x00, 0x00, 0x00, 0x4c, 0x00, /* 388: array */
    0x03, 0x00, 0x5b,
    0x15, 0x07, 0x18, 0x00, 0x0b, 0x0b, 0x08, 0x40, 0x5b, /* 403: struct OXID_REF (STRUCT) */
    0x11, 0x00, 0x02, 0x00, /* 412: pointer */
    0x1b, 0x07, 0x18, 0x00, 0x28, 0x00, 0x68, 0x00, 0x00, 0x00, 0x4c, 0x00, /* 416: array */
    0x03, 0x00, 0x5b,
    0x15, 0x07, 0x18, 0x00, 0x0b, 0x0b, 0x08, 0x40, 0x5b, /* 431: struct OID_UNMARSHAL (STRUCT) */
    0x12, 0x00, 0x77, 0xfe, /* 440: pointer */
    0x1a, 0x07, 0x98, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x08, 0x08, 0x08, 0x4c, /* 444: struct OXID_INFO (BOGUS) */
    0x00, 0x29, 0x00, 0x4c, 0x00, 0x2c, 0x00, 0x4c, 0x00, 0xeb, 0xfe, 0x08,
    0x40, 0x36, 0x4c, 0x00, 0xe4, 0xfe, 0x0b, 0x0d, 0x4c, 0x04, 0x67, 0x00,
    0x4c, 0x00, 0x63, 0x00, 0x4c, 0x00, 0x5f, 0x00, 0x0b, 0x4c, 0x00, 0xd1,
    0xfe, 0x5b, 0x12, 0x00, 0x41, 0xfe,
    0x15, 0x01, 0x04, 0x00, 0x06, 0x06, 0x5b, /* 498: struct COMVERSION (STRUCT) */
    0x1a, 0x07, 0x18, 0x00, 0x00, 0x00, 0x07, 0x00, 0x08, 0x40, 0x0b, 0x36, /* 505: struct CONTAINERVERSION (BOGUS) */
    0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 522: struct CONTAINER_EXTENSIONS (BOGUS) */
    0x11, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 538: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x0c, 0x00, 0x06, 0x00, 0x08, 0x08, 0x08, 0x5b, /* 559: struct CONTAINER_EXTENSION (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 569: array */
    0xb4, 0x83, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, /* 581: user marshal RPCSS_HSTRING */
    0x12, 0x00, 0x02, 0x00, /* 591: pointer */
    0x17, 0x03, 0x08, 0x00, 0x05, 0x00, 0x08, 0x08, 0x5b, /* 595: struct HSTRING_WIRE (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 604: array */
    0x11, 0x14, 0x02, 0x00, /* 616: pointer */
    0x12, 0x00, 0x02, 0x00, /* 620: pointer */
    0x1b, 0x00, 0x01, 0x00, 0x28, 0x54, 0x30, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 624: array */
    0x11, 0x04, 0x02, 0x00, /* 636: pointer */
    0x1c, 0x07, 0x08, 0x00, 0x28, 0x00, 0x20, 0x00, 0x00, 0x00, 0x28, 0x54, /* 640: array */
    0x30, 0x00, 0x00, 0x00, 0x0b, 0x5b,
    0x11, 0x00, 0x9d, 0xfd, /* 658: pointer */
    0x11, 0x00, 0x99, 0xfd, /* 662: pointer */
    0x11, 0x14, 0x02, 0x00, /* 666: pointer */
    0x12, 0x00, 0x91, 0xfd, /* 670: pointer */
    0x11, 0x00, 0x02, 0x00, /* 674: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x18, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 678: array */
    0x11, 0x04, 0x02, 0x00, /* 690: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x28, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 694: array */
    0x11, 0x00, 0x02, 0x00, /* 706: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x18, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 710: array */
    0x30, 0xe0, 0x00, 0x00, /* 722: context handle PHPROCESS */
    0x11, 0x14, 0x02, 0x00, /* 726: pointer */
    0x12, 0x00, 0x55, 0xfd, /* 730: pointer */
#else
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
    0x12, 0x00, 0x02, 0x00, /* 6: pointer */
    0x15, 0x07, 0x60, 0x00, 0x08, 0x40, 0x0b, 0x08, 0x08, 0x08, 0x08, 0x08, /* 10: struct PROXY_DECODE_INFO (STRUCT) */
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x43,
    0x43, 0x3e, 0x5b,
    0x30, 0xa0, 0x00, 0x00, /* 37: context handle PHPROCESS */
    0x11, 0x14, 0x02, 0x00, /* 41: pointer */
    0x12, 0x00, 0x02, 0x00, /* 45: pointer */
    0x17, 0x01, 0x04, 0x00, 0x05, 0x00, 0x06, 0x06, 0x5b, /* 49: struct DUALSTRINGARRAY (CSTRUCT) */
    0x1b, 0x01, 0x02, 0x00, 0x06, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x06, 0x5b, /* 58: array */
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, /* 70: range */
    0x11, 0x04, 0x02, 0x00, /* 80: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x28, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 84: array */
    0x11, 0x14, 0xa0, 0xff, /* 96: pointer */
    0x11, 0x14, 0x02, 0x00, /* 100: pointer */
    0x12, 0x00, 0x02, 0x00, /* 104: pointer */
    0x1b, 0x01, 0x02, 0x00, 0x28, 0x54, 0x4c, 0x00, 0x00, 0x00, 0x06, 0x5b, /* 108: array */
    0x11, 0x14, 0x02, 0x00, /* 120: pointer */
    0x12, 0x00, 0x02, 0x00, /* 124: pointer */
    0x1b, 0x03, 0x08, 0x00, 0x28, 0x54, 0x54, 0x00, 0x00, 0x00, 0x4b, 0x5c, /* 128: array */
    0x48, 0x49, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 162: struct SECPKG (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x36, 0x06, 0x3e, 0x5b,
    0x11, 0x14, 0x02, 0x00, /* 183: pointer */
    0x12, 0x00, 0x02, 0x00, /* 187: pointer */
    0x1b, 0x03, 0x10, 0x00, 0x28, 0x54, 0x5c, 0x00, 0x00, 0x00, 0x4c, 0x00, /* 191: array */
    0x03, 0x00, 0x5b,
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 206: struct GUID (STRUCT) */
    0x11, 0x04, 0x02, 0x00, /* 216: pointer */
    0x1d, 0x03, 0x14, 0x00, 0x08, 0x5b, /* 220: array */
    0x11, 0x14, 0x02, 0x00, /* 226: pointer */
    0x12, 0x00, 0x02, 0x00, /* 230: pointer */
    0x1b, 0x00, 0x01, 0x00, 0x28, 0x54, 0x78, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 234: array */
    0x30, 0x40, 0x00, 0x00, /* 246: context handle PHPROCESS */
    0x11, 0x04, 0x02, 0x00, /* 250: pointer */
    0x1d, 0x03, 0x10, 0x00, 0x08, 0x5b, /* 254: array */
    0x11, 0x14, 0x02, 0x00, /* 260: pointer */
    0x12, 0x00, 0x02, 0x00, /* 264: pointer */
    0x1b, 0x00, 0x01, 0x00, 0x28, 0x54, 0x08, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 268: array */
    0x11, 0x04, 0x02, 0x00, /* 280: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x08, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 284: array */
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, /* 296: range */
    0x11, 0x00, 0x02, 0x00, /* 306: pointer */
    0x1b, 0x07, 0x20, 0x00, 0x28, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4c, 0x00, /* 310: array */
    0x03, 0x00, 0x5b,
    0x15, 0x07, 0x20, 0x00, 0x0b, 0x0b, 0x0b, 0x08, 0x40, 0x5b, /* 325: struct OXID_OID_PAIR (STRUCT) */
    0x11, 0x04, 0x02, 0x00, /* 335: pointer */
    0x1b, 0x03, 0x04, 0x00, 0x28, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x5b, /* 339: array */
    0x11, 0x00, 0x02, 0x00, /* 351: pointer */
    0x1b, 0x07, 0x20, 0x00, 0x28, 0x00, 0x14, 0x00, 0x00, 0x00, 0x4c, 0x00, /* 355: array */
    0xd6, 0xff, 0x5b,
    0x11, 0x00, 0x02, 0x00, /* 370: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 374: array */
    0x11, 0x00, 0x02, 0x00, /* 386: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x24, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 390: array */
    0x11, 0x00, 0x02, 0x00, /* 402: pointer */
    0x1b, 0x07, 0x18, 0x00, 0x28, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x4c, 0x00, /* 406: array */
    0x03, 0x00, 0x5b,
    0x15, 0x07, 0x18, 0x00, 0x0b, 0x0b, 0x08, 0x40, 0x5b, /* 421: struct OXID_REF (STRUCT) */
    0x11, 0x00, 0x02, 0x00, /* 430: pointer */
    0x1b, 0x07, 0x18, 0x00, 0x28, 0x00, 0x34, 0x00, 0x00, 0x00, 0x4c, 0x00, /* 434: array */
    0x03, 0x00, 0x5b,
    0x15, 0x07, 0x18, 0x00, 0x0b, 0x0b, 0x08, 0x40, 0x5b, /* 449: struct OID_UNMARSHAL (STRUCT) */
    0x12, 0x00, 0x65, 0xfe, /* 458: pointer */
    0x1a, 0x07, 0x80, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x08, 0x08, 0x08, 0x4c, /* 462: struct OXID_INFO (BOGUS) */
    0x00, 0x28, 0x00, 0x4c, 0x00, 0x2b, 0x00, 0x4c, 0x00, 0xeb, 0xfe, 0x08,
    0x36, 0x4c, 0x00, 0xe5, 0xfe, 0x0b, 0x0d, 0x4c, 0x00, 0x72, 0x00, 0x4c,
    0x00, 0x6e, 0x00, 0x4c, 0x00, 0x6a, 0x00, 0x0b, 0x4c, 0x00, 0xd2, 0xfe,
    0x5b, 0x12, 0x00, 0x30, 0xfe,
    0x15, 0x01, 0x04, 0x00, 0x06, 0x06, 0x5b, /* 515: struct COMVERSION (STRUCT) */
    0x16, 0x07, 0x18, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x10, 0x00, 0x10, 0x00, /* 522: struct CONTAINERVERSION (PSTRUCT) */
    0x12, 0x00, 0x09, 0x00, 0x5b, 0x08, 0x40, 0x0b, 0x36, 0x40, 0x5b,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 545: struct CONTAINER_EXTENSIONS (PSTRUCT) */
    0x11, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 566: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x0c, 0x00, 0x06, 0x00, 0x08, 0x08, 0x08, 0x5b, /* 587: struct CONTAINER_EXTENSION (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 597: array */
    0xb4, 0x83, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, /* 609: user marshal RPCSS_HSTRING */
    0x12, 0x00, 0x02, 0x00, /* 619: pointer */
    0x17, 0x03, 0x08, 0x00, 0x05, 0x00, 0x08, 0x08, 0x5b, /* 623: struct HSTRING_WIRE (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 632: array */
    0x11, 0x14, 0x02, 0x00, /* 644: pointer */
    0x12, 0x00, 0x02, 0x00, /* 648: pointer */
    0x1b, 0x00, 0x01, 0x00, 0x28, 0x54, 0x1c, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 652: array */
    0x11, 0x04, 0x02, 0x00, /* 664: pointer */
    0x1c, 0x07, 0x08, 0x00, 0x28, 0x00, 0x10, 0x00, 0x00, 0x00, 0x28, 0x54, /* 668: array */
    0x18, 0x00, 0x00, 0x00, 0x0b, 0x5b,
    0x11, 0x00, 0x81, 0xfd, /* 686: pointer */
    0x11, 0x00, 0x7d, 0xfd, /* 690: pointer */
    0x11, 0x14, 0x02, 0x00, /* 694: pointer */
    0x12, 0x00, 0x75, 0xfd, /* 698: pointer */
    0x11, 0x00, 0x02, 0x00, /* 702: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 706: array */
    0x11, 0x04, 0x02, 0x00, /* 718: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x14, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 722: array */
    0x11, 0x00, 0x02, 0x00, /* 734: pointer */
    0x1b, 0x07, 0x08, 0x00, 0x28, 0x00, 0x10, 0x00, 0x00, 0x00, 0x0b, 0x5b, /* 738: array */
    0x30, 0xe0, 0x00, 0x00, /* 750: context handle PHPROCESS */
    0x11, 0x14, 0x02, 0x00, /* 754: pointer */
    0x12, 0x00, 0x39, 0xfd, /* 758: pointer */
#endif
};

static const unsigned char ILocalObjectExporter_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x08, 0x01, 0x32, 0x00, 0x00, 0x00, 0x13, 0x00, /* 2: ILocalObjectExporter.Connect */
    0xbb, 0x00, 0x47, 0x20, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x10, 0x00,
    0x02, 0x00, 0x0b, 0x00, 0x18, 0x00, 0x06, 0x00, 0x48, 0x00, 0x20, 0x00,
    0x08, 0x5c, 0x48, 0x00, 0x28, 0x00, 0x06, 0x5c, 0x10, 0x01, 0x30, 0x00,
    0x25, 0x00, 0x50, 0x21, 0x38, 0x00, 0x08, 0x5c, 0x13, 0x20, 0x40, 0x00,
    0x29, 0x00, 0x50, 0x21, 0x48, 0x00, 0x0b, 0x5c, 0x88, 0x00, 0x50, 0x00,
    0x46, 0x00, 0x13, 0x00, 0x58, 0x00, 0x50, 0x00, 0x50, 0x21, 0x60, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x68, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x70, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x78, 0x00, 0x08, 0x5c, 0x13, 0x20, 0x80, 0x00,
    0x60, 0x00, 0x50, 0x21, 0x88, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x90, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x98, 0x00, 0x08, 0x5c, 0x13, 0x20, 0xa0, 0x00,
    0x64, 0x00, 0x50, 0x21, 0xa8, 0x00, 0x08, 0x5c, 0x13, 0x20, 0xb0, 0x00,
    0x78, 0x00, 0x50, 0x21, 0xb8, 0x00, 0x08, 0x5c, 0x13, 0x20, 0xc0, 0x00,
    0xa5, 0x00, 0x50, 0x21, 0xc8, 0x00, 0x08, 0x5c, 0x50, 0x21, 0xd0, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0xd8, 0x00, 0x0b, 0x5c, 0x12, 0x41, 0xe0, 0x00,
    0xbc, 0x00, 0x13, 0x00, 0xe8, 0x00, 0xc6, 0x00, 0x50, 0x21, 0xf0, 0x00,
    0x08, 0x5c, 0x13, 0x20, 0xf8, 0x00, 0xd0, 0x00, 0x70, 0x00, 0x00, 0x01,
    0x10, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2b, 0x00, /* 220: ILocalObjectExporter.SetAppID */
    0x07, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0xe4, 0x00, 0x0a, 0x01, 0x10, 0x00,
    0xbc, 0x00, 0x70, 0x00, 0x18, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 264: ILocalObjectExporter.GetDefaultSecurityPermissions */
    0x0f, 0x00, 0x45, 0x04, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x13, 0x00, 0x08, 0x00, 0xe8, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x08, 0x5c, 0x13, 0x20, 0x18, 0x00, 0xf2, 0x00, 0x70, 0x00, 0x20, 0x00,
    0x10, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 314: ILocalObjectExporter.AllocateReservedIds */
    0x0f, 0x00, 0x45, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0xe4, 0x00, 0x88, 0x00, 0x10, 0x00,
    0x46, 0x00, 0x13, 0x00, 0x18, 0x00, 0x06, 0x01, 0x50, 0x21, 0x20, 0x00,
    0x08, 0x5c, 0x70, 0x00, 0x28, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x80, 0x00, 0x32, 0x00, 0x00, 0x00, 0x47, 0x00, /* 370: ILocalObjectExporter.BulkUpdateOIDs */
    0x07, 0x00, 0x47, 0x0f, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0xe4, 0x00, 0x88, 0x00, 0x10, 0x00,
    0x16, 0x01, 0x0b, 0x00, 0x18, 0x00, 0x20, 0x01, 0x13, 0x00, 0x20, 0x00,
    0x3d, 0x01, 0x88, 0x00, 0x28, 0x00, 0x16, 0x01, 0x0b, 0x00, 0x30, 0x00,
    0x4d, 0x01, 0x88, 0x00, 0x38, 0x00, 0x16, 0x01, 0x0b, 0x00, 0x40, 0x00,
    0x60, 0x01, 0x88, 0x00, 0x48, 0x00, 0x16, 0x01, 0x0b, 0x00, 0x50, 0x00,
    0x70, 0x01, 0x88, 0x00, 0x58, 0x00, 0x16, 0x01, 0x0b, 0x00, 0x60, 0x00,
    0x80, 0x01, 0x88, 0x00, 0x68, 0x00, 0x16, 0x01, 0x0b, 0x00, 0x70, 0x00,
    0x9c, 0x01, 0x70, 0x00, 0x78, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x50, 0x00, 0x32, 0x00, 0x00, 0x00, 0x27, 0x00, /* 486: ILocalObjectExporter.ClientResolveOXID */
    0x23, 0x00, 0x47, 0x09, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0xe4, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x0b, 0x5c, 0x0b, 0x00, 0x18, 0x00, 0xb8, 0x01, 0x13, 0x01, 0x20, 0x00,
    0xbc, 0x01, 0x50, 0x21, 0x28, 0x00, 0x0b, 0x5c, 0x50, 0x21, 0x30, 0x00,
    0x08, 0x5c, 0x13, 0x20, 0x38, 0x00, 0x68, 0x02, 0x50, 0x21, 0x40, 0x00,
    0x06, 0x5c, 0x70, 0x00, 0x48, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x68, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 566: ILocalObjectExporter.ServerAllocateOXIDAndOIDs */
    0x3f, 0x00, 0x47, 0x0c, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0xe4, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x0b, 0x5c, 0x50, 0x21, 0x18, 0x00, 0x0b, 0x5c, 0x88, 0x00, 0x20, 0x00,
    0x16, 0x01, 0x13, 0x00, 0x28, 0x00, 0x7c, 0x02, 0x50, 0x21, 0x30, 0x00,
    0x08, 0x5c, 0x0b, 0x01, 0x38, 0x00, 0xbc, 0x01, 0x0b, 0x00, 0x40, 0x00,
    0x92, 0x02, 0x0b, 0x00, 0x48, 0x00, 0x96, 0x02, 0x50, 0x21, 0x50, 0x00,
    0x0b, 0x5c, 0x13, 0x20, 0x58, 0x00, 0x9a, 0x02, 0x70, 0x00, 0x60, 0x00,
    0x10, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x48, 0x00, 0x32, 0x00, 0x00, 0x00, 0x37, 0x00, /* 664: ILocalObjectExporter.ServerAllocateOIDs */
    0x0f, 0x00, 0x47, 0x08, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0xe4, 0x00, 0x48, 0x01, 0x10, 0x00,
    0x0b, 0x5c, 0x88, 0x00, 0x18, 0x00, 0x16, 0x01, 0x0b, 0x00, 0x20, 0x00,
    0xa2, 0x02, 0x88, 0x00, 0x28, 0x00, 0x16, 0x01, 0x13, 0x00, 0x30, 0x00,
    0xb2, 0x02, 0x50, 0x21, 0x38, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x40, 0x00,
    0x10, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2f, 0x00, /* 738: ILocalObjectExporter.ServerFreeOXIDAndOIDs */
    0x07, 0x00, 0x46, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0xe4, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x0b, 0x5c, 0x88, 0x00, 0x18, 0x00, 0x16, 0x01, 0x0b, 0x00, 0x20, 0x00,
    0xc2, 0x02, 0x70, 0x00, 0x28, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x09, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2f, 0x00, /* 794: ILocalObjectExporter.SetServerOIDFlags */
    0x07, 0x00, 0x44, 0x04, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0xe4, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x0b, 0x5c, 0x48, 0x00, 0x18, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x20, 0x00,
    0x10, 0x5c,
    0x00, 0x40, 0x0a, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 844: ILocalObjectExporter.Disconnect */
    0x1f, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x01, 0x08, 0x00, 0xd2, 0x02, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x0b, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 888: ILocalObjectExporter.GetUpdatedResolverBindings */
    0x17, 0x00, 0x45, 0x04, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0xe4, 0x00, 0x13, 0x20, 0x10, 0x00,
    0xd6, 0x02, 0x50, 0x21, 0x18, 0x00, 0x0b, 0x5c, 0x70, 0x00, 0x20, 0x00,
    0x10, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x84, 0x00, 0x32, 0x00, 0x00, 0x00, 0x13, 0x00, /* 2: ILocalObjectExporter.Connect */
    0xbb, 0x00, 0x47, 0x20, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x02, 0x00,
    0x0b, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x48, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x48, 0x00, 0x14, 0x00, 0x06, 0x5c, 0x10, 0x01, 0x18, 0x00, 0x25, 0x00,
    0x50, 0x21, 0x1c, 0x00, 0x08, 0x5c, 0x13, 0x20, 0x20, 0x00, 0x29, 0x00,
    0x50, 0x21, 0x24, 0x00, 0x0b, 0x5c, 0x88, 0x00, 0x28, 0x00, 0x46, 0x00,
    0x13, 0x00, 0x2c, 0x00, 0x50, 0x00, 0x50, 0x21, 0x30, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x34, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x38, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x3c, 0x00, 0x08, 0x5c, 0x13, 0x20, 0x40, 0x00, 0x60, 0x00,
    0x50, 0x21, 0x44, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x48, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x4c, 0x00, 0x08, 0x5c, 0x13, 0x20, 0x50, 0x00, 0x64, 0x00,
    0x50, 0x21, 0x54, 0x00, 0x08, 0x5c, 0x13, 0x20, 0x58, 0x00, 0x78, 0x00,
    0x50, 0x21, 0x5c, 0x00, 0x08, 0x5c, 0x13, 0x20, 0x60, 0x00, 0xb7, 0x00,
    0x50, 0x21, 0x64, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x68, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x6c, 0x00, 0x0b, 0x5c, 0x12, 0x41, 0x70, 0x00, 0xce, 0x00,
    0x13, 0x00, 0x74, 0x00, 0xd8, 0x00, 0x50, 0x21, 0x78, 0x00, 0x08, 0x5c,
    0x13, 0x20, 0x7c, 0x00, 0xe2, 0x00, 0x70, 0x00, 0x80, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2b, 0x00, /* 218: ILocalObjectExporter.SetAppID */
    0x07, 0x00, 0x44, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0xf6, 0x00, 0x0a, 0x01, 0x08, 0x00, 0xce, 0x00,
    0x70, 0x00, 0x0c, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x14, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 260: ILocalObjectExporter.GetDefaultSecurityPermissions */
    0x0f, 0x00, 0x45, 0x04, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x04, 0x00, 0xfa, 0x00, 0x50, 0x21, 0x08, 0x00, 0x08, 0x5c,
    0x13, 0x20, 0x0c, 0x00, 0x04, 0x01, 0x70, 0x00, 0x10, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 308: ILocalObjectExporter.AllocateReservedIds */
    0x0f, 0x00, 0x45, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0xf6, 0x00, 0x88, 0x00, 0x08, 0x00, 0x46, 0x00,
    0x13, 0x00, 0x0c, 0x00, 0x18, 0x01, 0x50, 0x21, 0x10, 0x00, 0x08, 0x5c,
    0x70, 0x00, 0x14, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x40, 0x00, 0x32, 0x00, 0x00, 0x00, 0x47, 0x00, /* 362: ILocalObjectExporter.BulkUpdateOIDs */
    0x07, 0x00, 0x47, 0x0f, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0xf6, 0x00, 0x88, 0x00, 0x08, 0x00, 0x28, 0x01,
    0x0b, 0x00, 0x0c, 0x00, 0x32, 0x01, 0x13, 0x00, 0x10, 0x00, 0x4f, 0x01,
    0x88, 0x00, 0x14, 0x00, 0x28, 0x01, 0x0b, 0x00, 0x18, 0x00, 0x5f, 0x01,
    0x88, 0x00, 0x1c, 0x00, 0x28, 0x01, 0x0b, 0x00, 0x20, 0x00, 0x72, 0x01,
    0x88, 0x00, 0x24, 0x00, 0x28, 0x01, 0x0b, 0x00, 0x28, 0x00, 0x82, 0x01,
    0x88, 0x00, 0x2c, 0x00, 0x28, 0x01, 0x0b, 0x00, 0x30, 0x00, 0x92, 0x01,
    0x88, 0x00, 0x34, 0x00, 0x28, 0x01, 0x0b, 0x00, 0x38, 0x00, 0xae, 0x01,
    0x70, 0x00, 0x3c, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x2c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x27, 0x00, /* 476: ILocalObjectExporter.ClientResolveOXID */
    0x23, 0x00, 0x47, 0x09, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0xf6, 0x00, 0x48, 0x00, 0x08, 0x00, 0x0b, 0x5c,
    0x0b, 0x00, 0x10, 0x00, 0xca, 0x01, 0x13, 0x01, 0x14, 0x00, 0xce, 0x01,
    0x50, 0x21, 0x18, 0x00, 0x0b, 0x5c, 0x50, 0x21, 0x1c, 0x00, 0x08, 0x5c,
    0x13, 0x20, 0x20, 0x00, 0x84, 0x02, 0x50, 0x21, 0x24, 0x00, 0x06, 0x5c,
    0x70, 0x00, 0x28, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x34, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 554: ILocalObjectExporter.ServerAllocateOXIDAndOIDs */
    0x3f, 0x00, 0x47, 0x0c, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0xf6, 0x00, 0x50, 0x21, 0x08, 0x00, 0x0b, 0x5c,
    0x50, 0x21, 0x0c, 0x00, 0x0b, 0x5c, 0x88, 0x00, 0x10, 0x00, 0x28, 0x01,
    0x13, 0x00, 0x14, 0x00, 0x98, 0x02, 0x50, 0x21, 0x18, 0x00, 0x08, 0x5c,
    0x0b, 0x01, 0x1c, 0x00, 0xce, 0x01, 0x0b, 0x00, 0x20, 0x00, 0xae, 0x02,
    0x0b, 0x00, 0x24, 0x00, 0xb2, 0x02, 0x50, 0x21, 0x28, 0x00, 0x0b, 0x5c,
    0x13, 0x20, 0x2c, 0x00, 0xb6, 0x02, 0x70, 0x00, 0x30, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x24, 0x00, 0x32, 0x00, 0x00, 0x00, 0x37, 0x00, /* 650: ILocalObjectExporter.ServerAllocateOIDs */
    0x0f, 0x00, 0x47, 0x08, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0xf6, 0x00, 0x48, 0x01, 0x08, 0x00, 0x0b, 0x5c,
    0x88, 0x00, 0x0c, 0x00, 0x28, 0x01, 0x0b, 0x00, 0x10, 0x00, 0xbe, 0x02,
    0x88, 0x00, 0x14, 0x00, 0x28, 0x01, 0x13, 0x00, 0x18, 0x00, 0xce, 0x02,
    0x50, 0x21, 0x1c, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x20, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x1c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2f, 0x00, /* 722: ILocalObjectExporter.ServerFreeOXIDAndOIDs */
    0x07, 0x00, 0x46, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0xf6, 0x00, 0x48, 0x00, 0x08, 0x00, 0x0b, 0x5c,
    0x88, 0x00, 0x10, 0x00, 0x28, 0x01, 0x0b, 0x00, 0x14, 0x00, 0xde, 0x02,
    0x70, 0x00, 0x18, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x09, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2f, 0x00, /* 776: ILocalObjectExporter.SetServerOIDFlags */
    0x07, 0x00, 0x44, 0x04, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0xf6, 0x00, 0x48, 0x00, 0x08, 0x00, 0x0b, 0x5c,
    0x48, 0x00, 0x10, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x14, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x0a, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 824: ILocalObjectExporter.Disconnect */
    0x1f, 0x00, 0x44, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x18, 0x01, 0x04, 0x00, 0xee, 0x02, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x70, 0x00, 0x0c, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x0b, 0x00, 0x14, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 866: ILocalObjectExporter.GetUpdatedResolverBindings */
    0x17, 0x00, 0x45, 0x04, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0xf6, 0x00, 0x13, 0x20, 0x08, 0x00, 0xf2, 0x02,
    0x50, 0x21, 0x0c, 0x00, 0x0b, 0x5c, 0x70, 0x00, 0x10, 0x00, 0x10, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short ILocalObjectExporter_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 220, 264, 314, 370, 486, 566, 664, 738, 794, 844, 888
#else
    2, 218, 260, 308, 362, 476, 554, 650, 722, 776, 824, 866
#endif
};

static const NDR_RUNDOWN ILocalObjectExporter_RundownRoutines[] = {
    (NDR_RUNDOWN)(void *)PHPROCESS_rundown
};

static const USER_MARSHAL_ROUTINE_QUADRUPLE ILocalObjectExporter_UserMarshalRoutines[] = {
    { (USER_MARSHAL_SIZING_ROUTINE)(void *)RPCSS_HSTRING_UserSize, (USER_MARSHAL_MARSHALLING_ROUTINE)(void *)RPCSS_HSTRING_UserMarshal,
      (USER_MARSHAL_UNMARSHALLING_ROUTINE)(void *)RPCSS_HSTRING_UserUnmarshal, (USER_MARSHAL_FREEING_ROUTINE)(void *)RPCSS_HSTRING_UserFree }
};

static RPC_CLIENT_INTERFACE ILocalObjectExporter_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0xe60c73e6, 0x88f9, 0x11cf, {0x9a, 0xf1, 0x00, 0x20, 0xaf, 0x6e, 0x72, 0xf4}}, {2, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE ILocalObjectExporter_c_ifspec = (RPC_IF_HANDLE)&ILocalObjectExporter_c_ifspec_data;

static MIDL_STUB_DESC ILocalObjectExporter_StubDesc = {
    .RpcInterfaceInformation = &ILocalObjectExporter_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .apfnNdrRundownRoutines = ILocalObjectExporter_RundownRoutines,
    .pFormatTypes = ILocalObjectExporter_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .aUserMarshalQuadruple = ILocalObjectExporter_UserMarshalRoutines,
    .mFlags = 1,
};

#ifndef ILOCALOBJECTEXPORTER_CLIENT_ONLY
static const SERVER_ROUTINE ILocalObjectExporter_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_Connect_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_SetAppID_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_GetDefaultSecurityPermissions_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_AllocateReservedIds_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_BulkUpdateOIDs_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_ClientResolveOXID_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_ServerAllocateOXIDAndOIDs_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_ServerAllocateOIDs_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_ServerFreeOXIDAndOIDs_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_SetServerOIDFlags_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_Disconnect_s,
    (SERVER_ROUTINE)(void *)ILocalObjectExporter_GetUpdatedResolverBindings_s
};

int (*ILocalObjectExporter_hook)(PRPC_MESSAGE);

static void __RPC_STUB ILocalObjectExporter_dispatch_0(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_1(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_2(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_3(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_4(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_5(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_6(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_7(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_8(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_9(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_10(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalObjectExporter_dispatch_11(PRPC_MESSAGE m)
{
    if (ILocalObjectExporter_hook && ILocalObjectExporter_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION ILocalObjectExporter_DispatchFunctions[] = {
ILocalObjectExporter_dispatch_0, ILocalObjectExporter_dispatch_1, ILocalObjectExporter_dispatch_2, ILocalObjectExporter_dispatch_3, ILocalObjectExporter_dispatch_4, ILocalObjectExporter_dispatch_5, ILocalObjectExporter_dispatch_6, ILocalObjectExporter_dispatch_7, ILocalObjectExporter_dispatch_8, ILocalObjectExporter_dispatch_9, ILocalObjectExporter_dispatch_10, ILocalObjectExporter_dispatch_11
};
static RPC_DISPATCH_TABLE ILocalObjectExporter_DispatchTable = { 12, ILocalObjectExporter_DispatchFunctions, 0 };
static MIDL_SERVER_INFO ILocalObjectExporter_ServerInfo = {
    .pStubDesc = &ILocalObjectExporter_StubDesc,
    .DispatchTable = ILocalObjectExporter_ServerRoutineTable,
    .ProcString = ILocalObjectExporter_ProcFormat,
    .FmtStringOffset = ILocalObjectExporter_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE ILocalObjectExporter_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0xe60c73e6, 0x88f9, 0x11cf, {0x9a, 0xf1, 0x00, 0x20, 0xaf, 0x6e, 0x72, 0xf4}}, {2, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &ILocalObjectExporter_DispatchTable, 0, 0, 0, &ILocalObjectExporter_ServerInfo, 0 };
RPC_IF_HANDLE ILocalObjectExporter_s_ifspec = (RPC_IF_HANDLE)&ILocalObjectExporter_s_ifspec_data;
#endif

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_Connect(handle_t hServer, unsigned short *pwszWinstaDesktop, unsigned short *pwszExePath, PROXY_DECODE_INFO *pProxyDecodeInfo, unsigned long dwProcessFlags, unsigned short processArchitecture, PHPROCESS *pProcess, unsigned long *pdwTimeoutInSeconds, DUALSTRINGARRAY **ppdsaOrBindings, unsigned long long *pLocalMid, unsigned long cIdsToReserve, unsigned long long *aIdsReserved, unsigned long *pcIdsReserved, unsigned long *pfConnectFlags, unsigned long *pdwConnect3, unsigned long *pdwConnect4, unsigned short **pwszLegacySecurity, unsigned long *pAuthnLevel, unsigned long *pImpLevel, unsigned long *pcServerSvc, unsigned short **aServerSvc, unsigned long *pcClientSvc, SECPKG **aClientSvc, unsigned long *pcChannelHook, GUID **aChannelHook, unsigned long *pProcessID, unsigned long *pScmProcessID, unsigned long long *pSignature, GUID *pguidRPCSSProcessIdentifier, unsigned long *pdwSDSizes, unsigned long *pdwSDBlob, byte **pSDBlob)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[0], hServer, pwszWinstaDesktop, pwszExePath, pProxyDecodeInfo, dwProcessFlags, processArchitecture, pProcess, pdwTimeoutInSeconds, ppdsaOrBindings, pLocalMid, cIdsToReserve, aIdsReserved, pcIdsReserved, pfConnectFlags, pdwConnect3, pdwConnect4, pwszLegacySecurity, pAuthnLevel, pImpLevel, pcServerSvc, aServerSvc, pcClientSvc, aClientSvc, pcChannelHook, aChannelHook, pProcessID, pScmProcessID, pSignature, pguidRPCSSProcessIdentifier, pdwSDSizes, pdwSDBlob, pSDBlob).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hServer;
        unsigned short *pwszWinstaDesktop;
        unsigned short *pwszExePath;
        PROXY_DECODE_INFO *pProxyDecodeInfo;
        unsigned long dwProcessFlags;
        long processArchitecture;
        PHPROCESS *pProcess;
        unsigned long *pdwTimeoutInSeconds;
        DUALSTRINGARRAY **ppdsaOrBindings;
        unsigned long long *pLocalMid;
        unsigned long cIdsToReserve;
        unsigned long long *aIdsReserved;
        unsigned long *pcIdsReserved;
        unsigned long *pfConnectFlags;
        unsigned long *pdwConnect3;
        unsigned long *pdwConnect4;
        unsigned short **pwszLegacySecurity;
        unsigned long *pAuthnLevel;
        unsigned long *pImpLevel;
        unsigned long *pcServerSvc;
        unsigned short **aServerSvc;
        unsigned long *pcClientSvc;
        SECPKG **aClientSvc;
        unsigned long *pcChannelHook;
        GUID **aChannelHook;
        unsigned long *pProcessID;
        unsigned long *pScmProcessID;
        unsigned long long *pSignature;
        GUID *pguidRPCSSProcessIdentifier;
        unsigned long *pdwSDSizes;
        unsigned long *pdwSDBlob;
        byte **pSDBlob;
    } frame = { hServer, pwszWinstaDesktop, pwszExePath, pProxyDecodeInfo, dwProcessFlags, processArchitecture, pProcess, pdwTimeoutInSeconds, ppdsaOrBindings, pLocalMid, cIdsToReserve, aIdsReserved, pcIdsReserved, pfConnectFlags, pdwConnect3, pdwConnect4, pwszLegacySecurity, pAuthnLevel, pImpLevel, pcServerSvc, aServerSvc, pcClientSvc, aClientSvc, pcChannelHook, aChannelHook, pProcessID, pScmProcessID, pSignature, pguidRPCSSProcessIdentifier, pdwSDSizes, pdwSDBlob, pSDBlob };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_SetAppID(handle_t hRpc, PHPROCESS phProcess, GUID *guidAppID)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[1], hRpc, phProcess, guidAppID).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        GUID *guidAppID;
    } frame = { hRpc, phProcess, guidAppID };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[1], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_GetDefaultSecurityPermissions(handle_t hRpc, unsigned long *pdwFlags, unsigned long *pcbSD, byte **ppSD)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[2], hRpc, pdwFlags, pcbSD, ppSD).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        unsigned long *pdwFlags;
        unsigned long *pcbSD;
        byte **ppSD;
    } frame = { hRpc, pdwFlags, pcbSD, ppSD };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[2], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_AllocateReservedIds(handle_t hRpc, PHPROCESS phProcess, unsigned long cIdsToAlloc, unsigned long long *aIdsAllocated, unsigned long *pcIdsAllocated)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[3], hRpc, phProcess, cIdsToAlloc, aIdsAllocated, pcIdsAllocated).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long cIdsToAlloc;
        unsigned long long *aIdsAllocated;
        unsigned long *pcIdsAllocated;
    } frame = { hRpc, phProcess, cIdsToAlloc, aIdsAllocated, pcIdsAllocated };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[3], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_BulkUpdateOIDs(handle_t hRpc, PHPROCESS phProcess, unsigned long cOidsToBeAdded, OXID_OID_PAIR *aOidsToBeAdded, unsigned long *aStatusOfAdds, unsigned long cOidsToBeRemoved, OXID_OID_PAIR *aOidsToBeRemoved, unsigned long cServerOidsToFree, unsigned long long *aServerOids, unsigned long cServerOidsToUnPin, unsigned long long *aServerOidsToUnPin, unsigned long cClientOxidsToFree, OXID_REF *aClientOxids, unsigned long cOidsUnmarshaled, OID_UNMARSHAL *aOidsUnmarshaled)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[4], hRpc, phProcess, cOidsToBeAdded, aOidsToBeAdded, aStatusOfAdds, cOidsToBeRemoved, aOidsToBeRemoved, cServerOidsToFree, aServerOids, cServerOidsToUnPin, aServerOidsToUnPin, cClientOxidsToFree, aClientOxids, cOidsUnmarshaled, aOidsUnmarshaled).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long cOidsToBeAdded;
        OXID_OID_PAIR *aOidsToBeAdded;
        unsigned long *aStatusOfAdds;
        unsigned long cOidsToBeRemoved;
        OXID_OID_PAIR *aOidsToBeRemoved;
        unsigned long cServerOidsToFree;
        unsigned long long *aServerOids;
        unsigned long cServerOidsToUnPin;
        unsigned long long *aServerOidsToUnPin;
        unsigned long cClientOxidsToFree;
        OXID_REF *aClientOxids;
        unsigned long cOidsUnmarshaled;
        OID_UNMARSHAL *aOidsUnmarshaled;
    } frame = { hRpc, phProcess, cOidsToBeAdded, aOidsToBeAdded, aStatusOfAdds, cOidsToBeRemoved, aOidsToBeRemoved, cServerOidsToFree, aServerOids, cServerOidsToUnPin, aServerOidsToUnPin, cClientOxidsToFree, aClientOxids, cOidsUnmarshaled, aOidsUnmarshaled };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[4], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_ClientResolveOXID(handle_t hRpc, PHPROCESS phProcess, unsigned long long poxidServer, DUALSTRINGARRAY *pssaServerObjectResolverBindings, OXID_INFO *poxidInfo, unsigned long long *pLocalMidOfRemote, unsigned long *pulMarshaledTargetInfoLength, byte **pucMarshaledTargetInfo, unsigned short *pAuthnSvc)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[5], hRpc, phProcess, poxidServer, pssaServerObjectResolverBindings, poxidInfo, pLocalMidOfRemote, pulMarshaledTargetInfoLength, pucMarshaledTargetInfo, pAuthnSvc).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long long poxidServer;
        DUALSTRINGARRAY *pssaServerObjectResolverBindings;
        OXID_INFO *poxidInfo;
        unsigned long long *pLocalMidOfRemote;
        unsigned long *pulMarshaledTargetInfoLength;
        byte **pucMarshaledTargetInfo;
        unsigned short *pAuthnSvc;
    } frame = { hRpc, phProcess, poxidServer, pssaServerObjectResolverBindings, poxidInfo, pLocalMidOfRemote, pulMarshaledTargetInfoLength, pucMarshaledTargetInfo, pAuthnSvc };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[5], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_ServerAllocateOXIDAndOIDs(handle_t hRpc, PHPROCESS phProcess, unsigned long long *poxidServer, unsigned long long *pPrimaryOxid, unsigned long cOids, unsigned long long *aOid, unsigned long *pcOidsAllocated, OXID_INFO *pOxidInfo, DUALSTRINGARRAY *pdsaStringBindings, DUALSTRINGARRAY *pdsaSecurityBindings, unsigned long long *pdwOrBindingsID, DUALSTRINGARRAY **ppdsaOrBindings)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[6], hRpc, phProcess, poxidServer, pPrimaryOxid, cOids, aOid, pcOidsAllocated, pOxidInfo, pdsaStringBindings, pdsaSecurityBindings, pdwOrBindingsID, ppdsaOrBindings).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long long *poxidServer;
        unsigned long long *pPrimaryOxid;
        unsigned long cOids;
        unsigned long long *aOid;
        unsigned long *pcOidsAllocated;
        OXID_INFO *pOxidInfo;
        DUALSTRINGARRAY *pdsaStringBindings;
        DUALSTRINGARRAY *pdsaSecurityBindings;
        unsigned long long *pdwOrBindingsID;
        DUALSTRINGARRAY **ppdsaOrBindings;
    } frame = { hRpc, phProcess, poxidServer, pPrimaryOxid, cOids, aOid, pcOidsAllocated, pOxidInfo, pdsaStringBindings, pdsaSecurityBindings, pdwOrBindingsID, ppdsaOrBindings };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[6], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_ServerAllocateOIDs(handle_t hRpc, PHPROCESS phProcess, unsigned long long *poxidServer, unsigned long cOidsReturn, unsigned long long *aOidsReturn, unsigned long cOids, unsigned long long *aOids, unsigned long *pcOidsAllocated)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[7], hRpc, phProcess, poxidServer, cOidsReturn, aOidsReturn, cOids, aOids, pcOidsAllocated).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long long *poxidServer;
        unsigned long cOidsReturn;
        unsigned long long *aOidsReturn;
        unsigned long cOids;
        unsigned long long *aOids;
        unsigned long *pcOidsAllocated;
    } frame = { hRpc, phProcess, poxidServer, cOidsReturn, aOidsReturn, cOids, aOids, pcOidsAllocated };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[7], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_ServerFreeOXIDAndOIDs(handle_t hRpc, PHPROCESS phProcess, unsigned long long oxidServer, unsigned long cOids, unsigned long long *aOids)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[8], hRpc, phProcess, oxidServer, cOids, aOids).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long long oxidServer;
        unsigned long cOids;
        unsigned long long *aOids;
    } frame = { hRpc, phProcess, oxidServer, cOids, aOids };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[8], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_SetServerOIDFlags(handle_t hRpc, PHPROCESS phProcess, unsigned long long oid, unsigned long dwFlags)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[9], hRpc, phProcess, oid, dwFlags).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long long oid;
        unsigned long dwFlags;
    } frame = { hRpc, phProcess, oid, dwFlags };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[9], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_Disconnect(handle_t hRpc, PHPROCESS *pphProcess, unsigned long dwFlags)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[10], hRpc, pphProcess, dwFlags).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS *pphProcess;
        unsigned long dwFlags;
    } frame = { hRpc, pphProcess, dwFlags };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[10], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED error_status_t WINAPI ILocalObjectExporter_GetUpdatedResolverBindings(handle_t hRpc, PHPROCESS phProcess, DUALSTRINGARRAY **ppdsaOrBindings, unsigned long long *pdwBindingsID)
{
#ifdef _WIN64
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[11], hRpc, phProcess, ppdsaOrBindings, pdwBindingsID).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        DUALSTRINGARRAY **ppdsaOrBindings;
        unsigned long long *pdwBindingsID;
    } frame = { hRpc, phProcess, ppdsaOrBindings, pdwBindingsID };
#pragma pack(pop)
    return (error_status_t)NdrClientCall2(&ILocalObjectExporter_StubDesc, ILocalObjectExporter_ProcFormat + ILocalObjectExporter_FormatStringOffsetTable[11], (unsigned char *)&frame).Simple;
#endif
}

/* ------------------------------------------------------------------ interface IMachineActivatorControl */

#ifndef IMACHINEACTIVATORCONTROL_CLIENT_ONLY
extern long WINAPI IMachineActivatorControl_NotifySurrogateStarted_s(handle_t hRpc, PHPROCESS phProcess, unsigned long cPackages, RPCSS_HSTRING *packages, PROCESS_ACTIVATOR_TOKEN *pToken, error_status_t *prpcstat);
extern long WINAPI IMachineActivatorControl_NotifySurrogateInitializing_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat);
extern long WINAPI IMachineActivatorControl_NotifySurrogateReady_s(handle_t hRpc, PHPROCESS phProcess, unsigned long cPackages, RPCSS_HSTRING *packages, error_status_t *prpcstat);
extern long WINAPI IMachineActivatorControl_NotifySurrogateStopped_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat);
extern long WINAPI IMachineActivatorControl_NotifySurrogatePaused_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat);
extern long WINAPI IMachineActivatorControl_NotifySurrogateResumed_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat);
extern long WINAPI IMachineActivatorControl_NotifySurrogateUserInitializing_s(handle_t hRpc, PHPROCESS phProcess, unsigned long cPackages, RPCSS_HSTRING *packages, error_status_t *prpcstat);
#endif
ULONG WINAPI RPCSS_HSTRING_UserSize(ULONG *, ULONG, RPCSS_HSTRING *);
unsigned char *WINAPI RPCSS_HSTRING_UserMarshal(ULONG *, unsigned char *, RPCSS_HSTRING *);
unsigned char *WINAPI RPCSS_HSTRING_UserUnmarshal(ULONG *, unsigned char *, RPCSS_HSTRING *);
void WINAPI RPCSS_HSTRING_UserFree(ULONG *, RPCSS_HSTRING *);
void WINAPI PHPROCESS_rundown(void *);
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char IMachineActivatorControl_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x30, 0x40, 0x00, 0x00, /* 2: context handle PHPROCESS */
    0x12, 0x00, 0x02, 0x00, /* 6: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x10, 0x00, 0x00, 0x00, 0xff, 0xff, /* 10: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0xb4, 0x83, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, /* 31: user marshal RPCSS_HSTRING */
    0x12, 0x00, 0x02, 0x00, /* 41: pointer */
    0x17, 0x03, 0x08, 0x00, 0x05, 0x00, 0x08, 0x08, 0x5b, /* 45: struct HSTRING_WIRE (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 54: array */
    0x15, 0x03, 0x24, 0x00, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x08, 0x5b, /* 66: struct PROCESS_ACTIVATOR_TOKEN (STRUCT) */
    0x12, 0x00, 0x02, 0x00, /* 78: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x10, 0x00, 0x00, 0x00, 0xff, 0xff, /* 82: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0xbb, 0xff, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 103: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x10, 0x00, 0x00, 0x00, 0xff, 0xff, /* 107: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0xa2, 0xff, 0x5b,
#else
    0x00, 0x00, /* 0: pad */
    0x30, 0x40, 0x00, 0x00, /* 2: context handle PHPROCESS */
    0x12, 0x00, 0x02, 0x00, /* 6: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x08, 0x00, 0x00, 0x00, 0xff, 0xff, /* 10: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0xb4, 0x83, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, /* 31: user marshal RPCSS_HSTRING */
    0x12, 0x00, 0x02, 0x00, /* 41: pointer */
    0x17, 0x03, 0x08, 0x00, 0x05, 0x00, 0x08, 0x08, 0x5b, /* 45: struct HSTRING_WIRE (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 54: array */
    0x15, 0x03, 0x24, 0x00, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x08, 0x5b, /* 66: struct PROCESS_ACTIVATOR_TOKEN (STRUCT) */
    0x12, 0x00, 0x02, 0x00, /* 78: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x08, 0x00, 0x00, 0x00, 0xff, 0xff, /* 82: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0xbb, 0xff, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 103: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x08, 0x00, 0x00, 0x00, 0xff, 0xff, /* 107: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0xa2, 0xff, 0x5b,
#endif
};

static const unsigned char IMachineActivatorControl_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x38, 0x00, 0x32, 0x00, 0x00, 0x00, 0x47, 0x00, /* 2: IMachineActivatorControl.NotifySurrogateStarted */
    0x0f, 0x00, 0x46, 0x06, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x0b, 0x00, 0x18, 0x00, 0x06, 0x00, 0x0a, 0x01, 0x20, 0x00,
    0x42, 0x00, 0x50, 0x21, 0x28, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x30, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 64: IMachineActivatorControl.NotifySurrogateInitializing */
    0x0f, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 108: IMachineActivatorControl.NotifySurrogateReady */
    0x0f, 0x00, 0x46, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x0b, 0x00, 0x18, 0x00, 0x4e, 0x00, 0x50, 0x21, 0x20, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x28, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 164: IMachineActivatorControl.NotifySurrogateStopped */
    0x0f, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 208: IMachineActivatorControl.NotifySurrogatePaused */
    0x0f, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 252: IMachineActivatorControl.NotifySurrogateResumed */
    0x0f, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 296: IMachineActivatorControl.NotifySurrogateUserInitializing */
    0x0f, 0x00, 0x46, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x0b, 0x00, 0x18, 0x00, 0x67, 0x00, 0x50, 0x21, 0x20, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x28, 0x00, 0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x1c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x47, 0x00, /* 2: IMachineActivatorControl.NotifySurrogateStarted */
    0x0f, 0x00, 0x46, 0x06, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x0b, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x0a, 0x01, 0x10, 0x00, 0x42, 0x00,
    0x50, 0x21, 0x14, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 62: IMachineActivatorControl.NotifySurrogateInitializing */
    0x0f, 0x00, 0x44, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 104: IMachineActivatorControl.NotifySurrogateReady */
    0x0f, 0x00, 0x46, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x0b, 0x00, 0x0c, 0x00, 0x4e, 0x00, 0x50, 0x21, 0x10, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 158: IMachineActivatorControl.NotifySurrogateStopped */
    0x0f, 0x00, 0x44, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 200: IMachineActivatorControl.NotifySurrogatePaused */
    0x0f, 0x00, 0x44, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 242: IMachineActivatorControl.NotifySurrogateResumed */
    0x0f, 0x00, 0x44, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 284: IMachineActivatorControl.NotifySurrogateUserInitializing */
    0x0f, 0x00, 0x46, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x0b, 0x00, 0x0c, 0x00, 0x67, 0x00, 0x50, 0x21, 0x10, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short IMachineActivatorControl_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 64, 108, 164, 208, 252, 296
#else
    2, 62, 104, 158, 200, 242, 284
#endif
};

static const NDR_RUNDOWN IMachineActivatorControl_RundownRoutines[] = {
    (NDR_RUNDOWN)(void *)PHPROCESS_rundown
};

static const USER_MARSHAL_ROUTINE_QUADRUPLE IMachineActivatorControl_UserMarshalRoutines[] = {
    { (USER_MARSHAL_SIZING_ROUTINE)(void *)RPCSS_HSTRING_UserSize, (USER_MARSHAL_MARSHALLING_ROUTINE)(void *)RPCSS_HSTRING_UserMarshal,
      (USER_MARSHAL_UNMARSHALLING_ROUTINE)(void *)RPCSS_HSTRING_UserUnmarshal, (USER_MARSHAL_FREEING_ROUTINE)(void *)RPCSS_HSTRING_UserFree }
};

static RPC_CLIENT_INTERFACE IMachineActivatorControl_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0xc6f3ee72, 0xce7e, 0x11d1, {0xb7, 0x1e, 0x00, 0xc0, 0x4f, 0xc3, 0x11, 0x1a}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE IMachineActivatorControl_c_ifspec = (RPC_IF_HANDLE)&IMachineActivatorControl_c_ifspec_data;

static MIDL_STUB_DESC IMachineActivatorControl_StubDesc = {
    .RpcInterfaceInformation = &IMachineActivatorControl_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .apfnNdrRundownRoutines = IMachineActivatorControl_RundownRoutines,
    .pFormatTypes = IMachineActivatorControl_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .aUserMarshalQuadruple = IMachineActivatorControl_UserMarshalRoutines,
    .mFlags = 1,
};

#ifndef IMACHINEACTIVATORCONTROL_CLIENT_ONLY
static const SERVER_ROUTINE IMachineActivatorControl_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)IMachineActivatorControl_NotifySurrogateStarted_s,
    (SERVER_ROUTINE)(void *)IMachineActivatorControl_NotifySurrogateInitializing_s,
    (SERVER_ROUTINE)(void *)IMachineActivatorControl_NotifySurrogateReady_s,
    (SERVER_ROUTINE)(void *)IMachineActivatorControl_NotifySurrogateStopped_s,
    (SERVER_ROUTINE)(void *)IMachineActivatorControl_NotifySurrogatePaused_s,
    (SERVER_ROUTINE)(void *)IMachineActivatorControl_NotifySurrogateResumed_s,
    (SERVER_ROUTINE)(void *)IMachineActivatorControl_NotifySurrogateUserInitializing_s
};

int (*IMachineActivatorControl_hook)(PRPC_MESSAGE);

static void __RPC_STUB IMachineActivatorControl_dispatch_0(PRPC_MESSAGE m)
{
    if (IMachineActivatorControl_hook && IMachineActivatorControl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB IMachineActivatorControl_dispatch_1(PRPC_MESSAGE m)
{
    if (IMachineActivatorControl_hook && IMachineActivatorControl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB IMachineActivatorControl_dispatch_2(PRPC_MESSAGE m)
{
    if (IMachineActivatorControl_hook && IMachineActivatorControl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB IMachineActivatorControl_dispatch_3(PRPC_MESSAGE m)
{
    if (IMachineActivatorControl_hook && IMachineActivatorControl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB IMachineActivatorControl_dispatch_4(PRPC_MESSAGE m)
{
    if (IMachineActivatorControl_hook && IMachineActivatorControl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB IMachineActivatorControl_dispatch_5(PRPC_MESSAGE m)
{
    if (IMachineActivatorControl_hook && IMachineActivatorControl_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB IMachineActivatorControl_dispatch_6(PRPC_MESSAGE m)
{
    if (IMachineActivatorControl_hook && IMachineActivatorControl_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION IMachineActivatorControl_DispatchFunctions[] = {
IMachineActivatorControl_dispatch_0, IMachineActivatorControl_dispatch_1, IMachineActivatorControl_dispatch_2, IMachineActivatorControl_dispatch_3, IMachineActivatorControl_dispatch_4, IMachineActivatorControl_dispatch_5, IMachineActivatorControl_dispatch_6
};
static RPC_DISPATCH_TABLE IMachineActivatorControl_DispatchTable = { 7, IMachineActivatorControl_DispatchFunctions, 0 };
static MIDL_SERVER_INFO IMachineActivatorControl_ServerInfo = {
    .pStubDesc = &IMachineActivatorControl_StubDesc,
    .DispatchTable = IMachineActivatorControl_ServerRoutineTable,
    .ProcString = IMachineActivatorControl_ProcFormat,
    .FmtStringOffset = IMachineActivatorControl_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE IMachineActivatorControl_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0xc6f3ee72, 0xce7e, 0x11d1, {0xb7, 0x1e, 0x00, 0xc0, 0x4f, 0xc3, 0x11, 0x1a}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &IMachineActivatorControl_DispatchTable, 0, 0, 0, &IMachineActivatorControl_ServerInfo, 0 };
RPC_IF_HANDLE IMachineActivatorControl_s_ifspec = (RPC_IF_HANDLE)&IMachineActivatorControl_s_ifspec_data;
#endif

static NDRGEN_UNUSED long WINAPI IMachineActivatorControl_NotifySurrogateStarted(handle_t hRpc, PHPROCESS phProcess, unsigned long cPackages, RPCSS_HSTRING *packages, PROCESS_ACTIVATOR_TOKEN *pToken, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[0], hRpc, phProcess, cPackages, packages, pToken, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long cPackages;
        RPCSS_HSTRING *packages;
        PROCESS_ACTIVATOR_TOKEN *pToken;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, cPackages, packages, pToken, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI IMachineActivatorControl_NotifySurrogateInitializing(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[1], hRpc, phProcess, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[1], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI IMachineActivatorControl_NotifySurrogateReady(handle_t hRpc, PHPROCESS phProcess, unsigned long cPackages, RPCSS_HSTRING *packages, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[2], hRpc, phProcess, cPackages, packages, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long cPackages;
        RPCSS_HSTRING *packages;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, cPackages, packages, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[2], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI IMachineActivatorControl_NotifySurrogateStopped(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[3], hRpc, phProcess, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[3], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI IMachineActivatorControl_NotifySurrogatePaused(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[4], hRpc, phProcess, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[4], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI IMachineActivatorControl_NotifySurrogateResumed(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[5], hRpc, phProcess, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[5], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI IMachineActivatorControl_NotifySurrogateUserInitializing(handle_t hRpc, PHPROCESS phProcess, unsigned long cPackages, RPCSS_HSTRING *packages, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[6], hRpc, phProcess, cPackages, packages, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long cPackages;
        RPCSS_HSTRING *packages;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, cPackages, packages, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&IMachineActivatorControl_StubDesc, IMachineActivatorControl_ProcFormat + IMachineActivatorControl_FormatStringOffsetTable[6], (unsigned char *)&frame).Simple;
#endif
}

#endif
