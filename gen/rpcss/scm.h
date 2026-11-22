#ifndef NDRGEN_SCM_H
#define NDRGEN_SCM_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))

typedef struct HSTRING_WIRE HSTRING_WIRE;
typedef struct REG_INPUT_ENTRY REG_INPUT_ENTRY;
typedef struct REG_INPUT REG_INPUT;
typedef struct REG_OUTPUT REG_OUTPUT;
typedef struct REVOKE_ENTRY REVOKE_ENTRY;
typedef struct REVOKE_INPUT REVOKE_INPUT;
typedef struct ACTIVATABLE_CLASS_REG_ENTRY ACTIVATABLE_CLASS_REG_ENTRY;
typedef struct ACTIVATABLE_CLASS_REG_INPUT ACTIVATABLE_CLASS_REG_INPUT;
typedef struct ACTIVATABLE_CLASS_STOP_ENTRY ACTIVATABLE_CLASS_STOP_ENTRY;
typedef struct ACTIVATABLE_CLASS_STOP_INPUT ACTIVATABLE_CLASS_STOP_INPUT;
typedef struct STDOBJREF STDOBJREF;
typedef struct COMVERSION COMVERSION;
typedef struct CONTAINER_EXTENSION CONTAINER_EXTENSION;
typedef struct CONTAINER_EXTENSIONS CONTAINER_EXTENSIONS;
typedef struct CONTAINERVERSION CONTAINERVERSION;
typedef struct DUALSTRINGARRAY DUALSTRINGARRAY;
typedef struct OXID_INFO OXID_INFO;
typedef struct ORPC_EXTENT ORPC_EXTENT;
typedef struct ORPC_EXTENT_ARRAY ORPC_EXTENT_ARRAY;
typedef struct ORPCTHIS ORPCTHIS;
typedef struct ASYNC_REQUEST_BLOCK ASYNC_REQUEST_BLOCK;
typedef struct TOUCHED_ASTA_ARRAY TOUCHED_ASTA_ARRAY;
typedef struct CONTAINER_EXTENT CONTAINER_EXTENT;
typedef struct CONTAINER_EXTENT_ARRAY CONTAINER_EXTENT_ARRAY;
typedef struct CONTAINERTHIS CONTAINERTHIS;
typedef struct LOCALTHIS LOCALTHIS;
typedef struct ORPCTHAT ORPCTHAT;
typedef struct ASYNC_SUCCESS ASYNC_SUCCESS;
typedef struct ASYNC_FAILURE ASYNC_FAILURE;
typedef struct ASYNC_OUTCOME_DETAILS ASYNC_OUTCOME_DETAILS;
typedef struct ASYNC_RESPONSE_BLOCK ASYNC_RESPONSE_BLOCK;
typedef struct CONTAINERTHAT CONTAINERTHAT;
typedef struct LOCALTHAT LOCALTHAT;
typedef struct MInterfacePointer MInterfacePointer;

typedef void *PHPROCESS;
struct HSTRING_WIRE {
    unsigned long Magic;
    unsigned long Bytes;
    byte Data[1];
};
typedef struct RPCSS_HSTRING_DATA * RPCSS_HSTRING;
struct REG_INPUT_ENTRY {
    GUID clsid;
    unsigned long long oxid;
    GUID ipid;
    unsigned long dwFlags;
    unsigned long dwReserved;
};
struct REG_INPUT {
    unsigned long dwSize;
    REG_INPUT_ENTRY rginent[1];
};
struct REG_OUTPUT {
    unsigned long dwSize;
    unsigned long RegKeys[1];
};
struct REVOKE_ENTRY {
    GUID clsid;
    unsigned long dwReg;
};
struct REVOKE_INPUT {
    unsigned long dwSize;
    REVOKE_ENTRY revent[1];
};
struct ACTIVATABLE_CLASS_REG_ENTRY {
    unsigned short *activatableClassId;
    unsigned short *serverName;
    unsigned long long oxid;
    GUID ipid;
    unsigned long dwFlags;
    unsigned long dwReserved;
};
struct ACTIVATABLE_CLASS_REG_INPUT {
    unsigned long dwSize;
    ACTIVATABLE_CLASS_REG_ENTRY entries[1];
};
struct ACTIVATABLE_CLASS_STOP_ENTRY {
    unsigned short *activatableClassId;
    unsigned short *serverName;
    unsigned long dwReg;
};
struct ACTIVATABLE_CLASS_STOP_INPUT {
    unsigned long dwSize;
    ACTIVATABLE_CLASS_STOP_ENTRY entries[1];
};
struct STDOBJREF {
    unsigned long flags;
    unsigned long cPublicRefs;
    unsigned long long oxid;
    unsigned long long oid;
    GUID ipid;
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
struct DUALSTRINGARRAY {
    unsigned short wNumEntries;
    unsigned short wSecurityOffset;
    unsigned short aStringArray[1];
};
typedef enum OR_OXID_CLIENT_DEPENDENCY { OR_OXID_CLIENT_DEPENDENCY_NONE, OR_OXID_CLIENT_DEPENDENCY_UNIDIRECTIONAL, OR_OXID_CLIENT_DEPENDENCY_BIDIRECTIONAL } OR_OXID_CLIENT_DEPENDENCY;
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
struct ORPC_EXTENT {
    GUID id;
    unsigned long size;
    byte data[1];
};
struct ORPC_EXTENT_ARRAY {
    unsigned long size;
    unsigned long reserved;
    ORPC_EXTENT **extent;
};
struct ORPCTHIS {
    COMVERSION version;
    unsigned long flags;
    unsigned long reserved1;
    GUID cid;
    ORPC_EXTENT_ARRAY *extensions;
};
struct ASYNC_REQUEST_BLOCK {
    GUID asyncOperationId;
    unsigned long long oxidClientProcessNA;
    GUID originalClientLogicalThreadId;
    unsigned long long uClientCausalityTraceId;
};
struct TOUCHED_ASTA_ARRAY {
    unsigned long size;
    unsigned long reserved;
    unsigned long long *pAstaOxids;
};
struct CONTAINER_EXTENT {
    unsigned long id;
    unsigned long version;
    unsigned long size;
    byte data[1];
};
struct CONTAINER_EXTENT_ARRAY {
    unsigned long size;
    unsigned long reserved;
    CONTAINER_EXTENT **extent;
};
struct CONTAINERTHIS {
    unsigned long dcomVersionUnused;
    unsigned long version;
    unsigned long long capabilityFlags;
    unsigned long long requestFlags;
    GUID causalityId;
    GUID unassignedSyncPassthroughGuid;
    GUID passthroughTraceActivity;
    GUID unassignedAlwaysPassthroughGuid_2;
    GUID callId;
    GUID reservedGuid_2;
    GUID reservedGuid_3;
    GUID reservedGuid_4;
    unsigned long long unassignedSyncPassthroughUint64_1;
    unsigned long long unassignedSyncPassthroughUint64_2;
    unsigned long long unassignedAlwaysPassthroughUint64_1;
    unsigned long long unassignedAlwaysPassthroughUint64_2;
    unsigned long long reservedUint64_1;
    unsigned long long reservedUint64_2;
    unsigned long long reservedUint64_3;
    unsigned long long reservedUint64_4;
    unsigned long reservedUint32;
    CONTAINER_EXTENT_ARRAY *extensions;
};
struct LOCALTHIS {
    unsigned long dwFlags;
    unsigned long dwClientThread;
    GUID passthroughTraceActivity;
    GUID callId;
    ASYNC_REQUEST_BLOCK asyncRequestBlock;
    TOUCHED_ASTA_ARRAY *pTouchedAstaArray;
    CONTAINERTHIS *containerPassthroughData;
};
struct ORPCTHAT {
    unsigned long flags;
    ORPC_EXTENT_ARRAY *extensions;
};
struct ASYNC_SUCCESS {
    unsigned long sizeOfMarshaledResults;
    unsigned long reserved;
    byte *pMarshaledResults;
};
struct ASYNC_FAILURE {
    unsigned long pointOfFailure;
    long hrFailure;
    unsigned long sizeOfMarshaledErrorInfo;
    unsigned long reserved;
    byte *pMarshaledErrorInfo;
};
struct ASYNC_OUTCOME_DETAILS {
    unsigned long outcome;
    union {
        ASYNC_SUCCESS successDetails;
        ASYNC_FAILURE failureDetails;
    } u;
};
struct ASYNC_RESPONSE_BLOCK {
    unsigned long asyncStatus;
    unsigned long reserved1;
    unsigned long long uServerCausalityTraceId;
    GUID completionTraceActivity;
    unsigned long reserved2;
    ASYNC_OUTCOME_DETAILS *pOutcomeDetails;
};
struct CONTAINERTHAT {
    unsigned long long responseFlags;
    GUID unassignedPassthroughGuid_1;
    GUID unassignedPassthroughGuid_2;
    GUID unassignedPassthroughGuid_3;
    GUID unassignedPassthroughGuid_4;
    GUID reservedGuid_1;
    GUID reservedGuid_2;
    GUID reservedGuid_3;
    GUID reservedGuid_4;
    unsigned long long unassignedPassthroughUint64_1;
    unsigned long long unassignedPassthroughUint64_2;
    unsigned long long unassignedPassthroughUint64_3;
    unsigned long long unassignedPassthroughUint64_4;
    unsigned long long marshalingSetId;
    unsigned long long reservedUint64_2;
    unsigned long long reservedUint64_3;
    unsigned long long reservedUint64_4;
    unsigned long reservedUint32;
    CONTAINER_EXTENT_ARRAY *extensions;
};
struct LOCALTHAT {
    unsigned long long marshalingSetId;
    unsigned long reserved;
    ASYNC_RESPONSE_BLOCK *pAsyncResponseBlock;
    CONTAINER_EXTENT *containerErrorInformation;
    CONTAINERTHAT *containerPassthroughData;
};
struct MInterfacePointer {
    unsigned long ulCntData;
    byte abData[1];
};

#ifdef _WIN64
_Static_assert(sizeof(PHPROCESS) == 8, "size of PHPROCESS");
_Static_assert(sizeof(HSTRING_WIRE) == 12, "size of HSTRING_WIRE");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Magic) == 0, "HSTRING_WIRE.Magic");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Bytes) == 4, "HSTRING_WIRE.Bytes");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Data) == 8, "HSTRING_WIRE.Data");
_Static_assert(sizeof(RPCSS_HSTRING) == 8, "size of RPCSS_HSTRING");
_Static_assert(sizeof(GUID) == 16, "size of GUID");
_Static_assert(__builtin_offsetof(GUID, Data1) == 0, "GUID.Data1");
_Static_assert(__builtin_offsetof(GUID, Data2) == 4, "GUID.Data2");
_Static_assert(__builtin_offsetof(GUID, Data3) == 6, "GUID.Data3");
_Static_assert(__builtin_offsetof(GUID, Data4) == 8, "GUID.Data4");
_Static_assert(sizeof(REG_INPUT_ENTRY) == 48, "size of REG_INPUT_ENTRY");
_Static_assert(__builtin_offsetof(REG_INPUT_ENTRY, clsid) == 0, "REG_INPUT_ENTRY.clsid");
_Static_assert(__builtin_offsetof(REG_INPUT_ENTRY, oxid) == 16, "REG_INPUT_ENTRY.oxid");
_Static_assert(__builtin_offsetof(REG_INPUT_ENTRY, ipid) == 24, "REG_INPUT_ENTRY.ipid");
_Static_assert(__builtin_offsetof(REG_INPUT_ENTRY, dwFlags) == 40, "REG_INPUT_ENTRY.dwFlags");
_Static_assert(__builtin_offsetof(REG_INPUT_ENTRY, dwReserved) == 44, "REG_INPUT_ENTRY.dwReserved");
_Static_assert(sizeof(REG_INPUT) == 56, "size of REG_INPUT");
_Static_assert(__builtin_offsetof(REG_INPUT, dwSize) == 0, "REG_INPUT.dwSize");
_Static_assert(__builtin_offsetof(REG_INPUT, rginent) == 8, "REG_INPUT.rginent");
_Static_assert(sizeof(REG_OUTPUT) == 8, "size of REG_OUTPUT");
_Static_assert(__builtin_offsetof(REG_OUTPUT, dwSize) == 0, "REG_OUTPUT.dwSize");
_Static_assert(__builtin_offsetof(REG_OUTPUT, RegKeys) == 4, "REG_OUTPUT.RegKeys");
_Static_assert(sizeof(REVOKE_ENTRY) == 20, "size of REVOKE_ENTRY");
_Static_assert(__builtin_offsetof(REVOKE_ENTRY, clsid) == 0, "REVOKE_ENTRY.clsid");
_Static_assert(__builtin_offsetof(REVOKE_ENTRY, dwReg) == 16, "REVOKE_ENTRY.dwReg");
_Static_assert(sizeof(REVOKE_INPUT) == 24, "size of REVOKE_INPUT");
_Static_assert(__builtin_offsetof(REVOKE_INPUT, dwSize) == 0, "REVOKE_INPUT.dwSize");
_Static_assert(__builtin_offsetof(REVOKE_INPUT, revent) == 4, "REVOKE_INPUT.revent");
_Static_assert(sizeof(ACTIVATABLE_CLASS_REG_ENTRY) == 48, "size of ACTIVATABLE_CLASS_REG_ENTRY");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, activatableClassId) == 0, "ACTIVATABLE_CLASS_REG_ENTRY.activatableClassId");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, serverName) == 8, "ACTIVATABLE_CLASS_REG_ENTRY.serverName");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, oxid) == 16, "ACTIVATABLE_CLASS_REG_ENTRY.oxid");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, ipid) == 24, "ACTIVATABLE_CLASS_REG_ENTRY.ipid");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, dwFlags) == 40, "ACTIVATABLE_CLASS_REG_ENTRY.dwFlags");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, dwReserved) == 44, "ACTIVATABLE_CLASS_REG_ENTRY.dwReserved");
_Static_assert(sizeof(ACTIVATABLE_CLASS_REG_INPUT) == 56, "size of ACTIVATABLE_CLASS_REG_INPUT");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_INPUT, dwSize) == 0, "ACTIVATABLE_CLASS_REG_INPUT.dwSize");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_INPUT, entries) == 8, "ACTIVATABLE_CLASS_REG_INPUT.entries");
_Static_assert(sizeof(ACTIVATABLE_CLASS_STOP_ENTRY) == 24, "size of ACTIVATABLE_CLASS_STOP_ENTRY");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_STOP_ENTRY, activatableClassId) == 0, "ACTIVATABLE_CLASS_STOP_ENTRY.activatableClassId");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_STOP_ENTRY, serverName) == 8, "ACTIVATABLE_CLASS_STOP_ENTRY.serverName");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_STOP_ENTRY, dwReg) == 16, "ACTIVATABLE_CLASS_STOP_ENTRY.dwReg");
_Static_assert(sizeof(ACTIVATABLE_CLASS_STOP_INPUT) == 32, "size of ACTIVATABLE_CLASS_STOP_INPUT");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_STOP_INPUT, dwSize) == 0, "ACTIVATABLE_CLASS_STOP_INPUT.dwSize");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_STOP_INPUT, entries) == 8, "ACTIVATABLE_CLASS_STOP_INPUT.entries");
_Static_assert(sizeof(STDOBJREF) == 40, "size of STDOBJREF");
_Static_assert(__builtin_offsetof(STDOBJREF, flags) == 0, "STDOBJREF.flags");
_Static_assert(__builtin_offsetof(STDOBJREF, cPublicRefs) == 4, "STDOBJREF.cPublicRefs");
_Static_assert(__builtin_offsetof(STDOBJREF, oxid) == 8, "STDOBJREF.oxid");
_Static_assert(__builtin_offsetof(STDOBJREF, oid) == 16, "STDOBJREF.oid");
_Static_assert(__builtin_offsetof(STDOBJREF, ipid) == 24, "STDOBJREF.ipid");
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
_Static_assert(sizeof(DUALSTRINGARRAY) == 6, "size of DUALSTRINGARRAY");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, wNumEntries) == 0, "DUALSTRINGARRAY.wNumEntries");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, wSecurityOffset) == 2, "DUALSTRINGARRAY.wSecurityOffset");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, aStringArray) == 4, "DUALSTRINGARRAY.aStringArray");
_Static_assert(sizeof(OR_OXID_CLIENT_DEPENDENCY) == 4, "size of OR_OXID_CLIENT_DEPENDENCY");
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
_Static_assert(sizeof(ORPC_EXTENT) == 24, "size of ORPC_EXTENT");
_Static_assert(__builtin_offsetof(ORPC_EXTENT, id) == 0, "ORPC_EXTENT.id");
_Static_assert(__builtin_offsetof(ORPC_EXTENT, size) == 16, "ORPC_EXTENT.size");
_Static_assert(__builtin_offsetof(ORPC_EXTENT, data) == 20, "ORPC_EXTENT.data");
_Static_assert(sizeof(ORPC_EXTENT_ARRAY) == 16, "size of ORPC_EXTENT_ARRAY");
_Static_assert(__builtin_offsetof(ORPC_EXTENT_ARRAY, size) == 0, "ORPC_EXTENT_ARRAY.size");
_Static_assert(__builtin_offsetof(ORPC_EXTENT_ARRAY, reserved) == 4, "ORPC_EXTENT_ARRAY.reserved");
_Static_assert(__builtin_offsetof(ORPC_EXTENT_ARRAY, extent) == 8, "ORPC_EXTENT_ARRAY.extent");
_Static_assert(sizeof(ORPCTHIS) == 40, "size of ORPCTHIS");
_Static_assert(__builtin_offsetof(ORPCTHIS, version) == 0, "ORPCTHIS.version");
_Static_assert(__builtin_offsetof(ORPCTHIS, flags) == 4, "ORPCTHIS.flags");
_Static_assert(__builtin_offsetof(ORPCTHIS, reserved1) == 8, "ORPCTHIS.reserved1");
_Static_assert(__builtin_offsetof(ORPCTHIS, cid) == 12, "ORPCTHIS.cid");
_Static_assert(__builtin_offsetof(ORPCTHIS, extensions) == 32, "ORPCTHIS.extensions");
_Static_assert(sizeof(ASYNC_REQUEST_BLOCK) == 48, "size of ASYNC_REQUEST_BLOCK");
_Static_assert(__builtin_offsetof(ASYNC_REQUEST_BLOCK, asyncOperationId) == 0, "ASYNC_REQUEST_BLOCK.asyncOperationId");
_Static_assert(__builtin_offsetof(ASYNC_REQUEST_BLOCK, oxidClientProcessNA) == 16, "ASYNC_REQUEST_BLOCK.oxidClientProcessNA");
_Static_assert(__builtin_offsetof(ASYNC_REQUEST_BLOCK, originalClientLogicalThreadId) == 24, "ASYNC_REQUEST_BLOCK.originalClientLogicalThreadId");
_Static_assert(__builtin_offsetof(ASYNC_REQUEST_BLOCK, uClientCausalityTraceId) == 40, "ASYNC_REQUEST_BLOCK.uClientCausalityTraceId");
_Static_assert(sizeof(TOUCHED_ASTA_ARRAY) == 16, "size of TOUCHED_ASTA_ARRAY");
_Static_assert(__builtin_offsetof(TOUCHED_ASTA_ARRAY, size) == 0, "TOUCHED_ASTA_ARRAY.size");
_Static_assert(__builtin_offsetof(TOUCHED_ASTA_ARRAY, reserved) == 4, "TOUCHED_ASTA_ARRAY.reserved");
_Static_assert(__builtin_offsetof(TOUCHED_ASTA_ARRAY, pAstaOxids) == 8, "TOUCHED_ASTA_ARRAY.pAstaOxids");
_Static_assert(sizeof(CONTAINER_EXTENT) == 16, "size of CONTAINER_EXTENT");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT, id) == 0, "CONTAINER_EXTENT.id");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT, version) == 4, "CONTAINER_EXTENT.version");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT, size) == 8, "CONTAINER_EXTENT.size");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT, data) == 12, "CONTAINER_EXTENT.data");
_Static_assert(sizeof(CONTAINER_EXTENT_ARRAY) == 16, "size of CONTAINER_EXTENT_ARRAY");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT_ARRAY, size) == 0, "CONTAINER_EXTENT_ARRAY.size");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT_ARRAY, reserved) == 4, "CONTAINER_EXTENT_ARRAY.reserved");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT_ARRAY, extent) == 8, "CONTAINER_EXTENT_ARRAY.extent");
_Static_assert(sizeof(CONTAINERTHIS) == 232, "size of CONTAINERTHIS");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, dcomVersionUnused) == 0, "CONTAINERTHIS.dcomVersionUnused");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, version) == 4, "CONTAINERTHIS.version");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, capabilityFlags) == 8, "CONTAINERTHIS.capabilityFlags");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, requestFlags) == 16, "CONTAINERTHIS.requestFlags");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, causalityId) == 24, "CONTAINERTHIS.causalityId");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedSyncPassthroughGuid) == 40, "CONTAINERTHIS.unassignedSyncPassthroughGuid");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, passthroughTraceActivity) == 56, "CONTAINERTHIS.passthroughTraceActivity");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedAlwaysPassthroughGuid_2) == 72, "CONTAINERTHIS.unassignedAlwaysPassthroughGuid_2");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, callId) == 88, "CONTAINERTHIS.callId");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedGuid_2) == 104, "CONTAINERTHIS.reservedGuid_2");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedGuid_3) == 120, "CONTAINERTHIS.reservedGuid_3");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedGuid_4) == 136, "CONTAINERTHIS.reservedGuid_4");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedSyncPassthroughUint64_1) == 152, "CONTAINERTHIS.unassignedSyncPassthroughUint64_1");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedSyncPassthroughUint64_2) == 160, "CONTAINERTHIS.unassignedSyncPassthroughUint64_2");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedAlwaysPassthroughUint64_1) == 168, "CONTAINERTHIS.unassignedAlwaysPassthroughUint64_1");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedAlwaysPassthroughUint64_2) == 176, "CONTAINERTHIS.unassignedAlwaysPassthroughUint64_2");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedUint64_1) == 184, "CONTAINERTHIS.reservedUint64_1");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedUint64_2) == 192, "CONTAINERTHIS.reservedUint64_2");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedUint64_3) == 200, "CONTAINERTHIS.reservedUint64_3");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedUint64_4) == 208, "CONTAINERTHIS.reservedUint64_4");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedUint32) == 216, "CONTAINERTHIS.reservedUint32");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, extensions) == 224, "CONTAINERTHIS.extensions");
_Static_assert(sizeof(LOCALTHIS) == 104, "size of LOCALTHIS");
_Static_assert(__builtin_offsetof(LOCALTHIS, dwFlags) == 0, "LOCALTHIS.dwFlags");
_Static_assert(__builtin_offsetof(LOCALTHIS, dwClientThread) == 4, "LOCALTHIS.dwClientThread");
_Static_assert(__builtin_offsetof(LOCALTHIS, passthroughTraceActivity) == 8, "LOCALTHIS.passthroughTraceActivity");
_Static_assert(__builtin_offsetof(LOCALTHIS, callId) == 24, "LOCALTHIS.callId");
_Static_assert(__builtin_offsetof(LOCALTHIS, asyncRequestBlock) == 40, "LOCALTHIS.asyncRequestBlock");
_Static_assert(__builtin_offsetof(LOCALTHIS, pTouchedAstaArray) == 88, "LOCALTHIS.pTouchedAstaArray");
_Static_assert(__builtin_offsetof(LOCALTHIS, containerPassthroughData) == 96, "LOCALTHIS.containerPassthroughData");
_Static_assert(sizeof(ORPCTHAT) == 16, "size of ORPCTHAT");
_Static_assert(__builtin_offsetof(ORPCTHAT, flags) == 0, "ORPCTHAT.flags");
_Static_assert(__builtin_offsetof(ORPCTHAT, extensions) == 8, "ORPCTHAT.extensions");
_Static_assert(sizeof(ASYNC_SUCCESS) == 16, "size of ASYNC_SUCCESS");
_Static_assert(__builtin_offsetof(ASYNC_SUCCESS, sizeOfMarshaledResults) == 0, "ASYNC_SUCCESS.sizeOfMarshaledResults");
_Static_assert(__builtin_offsetof(ASYNC_SUCCESS, reserved) == 4, "ASYNC_SUCCESS.reserved");
_Static_assert(__builtin_offsetof(ASYNC_SUCCESS, pMarshaledResults) == 8, "ASYNC_SUCCESS.pMarshaledResults");
_Static_assert(sizeof(ASYNC_FAILURE) == 24, "size of ASYNC_FAILURE");
_Static_assert(__builtin_offsetof(ASYNC_FAILURE, pointOfFailure) == 0, "ASYNC_FAILURE.pointOfFailure");
_Static_assert(__builtin_offsetof(ASYNC_FAILURE, hrFailure) == 4, "ASYNC_FAILURE.hrFailure");
_Static_assert(__builtin_offsetof(ASYNC_FAILURE, sizeOfMarshaledErrorInfo) == 8, "ASYNC_FAILURE.sizeOfMarshaledErrorInfo");
_Static_assert(__builtin_offsetof(ASYNC_FAILURE, reserved) == 12, "ASYNC_FAILURE.reserved");
_Static_assert(__builtin_offsetof(ASYNC_FAILURE, pMarshaledErrorInfo) == 16, "ASYNC_FAILURE.pMarshaledErrorInfo");
_Static_assert(sizeof(ASYNC_OUTCOME_DETAILS) == 32, "size of ASYNC_OUTCOME_DETAILS");
_Static_assert(__builtin_offsetof(ASYNC_OUTCOME_DETAILS, u) == 8, "ASYNC_OUTCOME_DETAILS.u");
_Static_assert(sizeof(ASYNC_RESPONSE_BLOCK) == 48, "size of ASYNC_RESPONSE_BLOCK");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, asyncStatus) == 0, "ASYNC_RESPONSE_BLOCK.asyncStatus");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, reserved1) == 4, "ASYNC_RESPONSE_BLOCK.reserved1");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, uServerCausalityTraceId) == 8, "ASYNC_RESPONSE_BLOCK.uServerCausalityTraceId");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, completionTraceActivity) == 16, "ASYNC_RESPONSE_BLOCK.completionTraceActivity");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, reserved2) == 32, "ASYNC_RESPONSE_BLOCK.reserved2");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, pOutcomeDetails) == 40, "ASYNC_RESPONSE_BLOCK.pOutcomeDetails");
_Static_assert(sizeof(CONTAINERTHAT) == 216, "size of CONTAINERTHAT");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, responseFlags) == 0, "CONTAINERTHAT.responseFlags");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughGuid_1) == 8, "CONTAINERTHAT.unassignedPassthroughGuid_1");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughGuid_2) == 24, "CONTAINERTHAT.unassignedPassthroughGuid_2");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughGuid_3) == 40, "CONTAINERTHAT.unassignedPassthroughGuid_3");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughGuid_4) == 56, "CONTAINERTHAT.unassignedPassthroughGuid_4");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedGuid_1) == 72, "CONTAINERTHAT.reservedGuid_1");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedGuid_2) == 88, "CONTAINERTHAT.reservedGuid_2");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedGuid_3) == 104, "CONTAINERTHAT.reservedGuid_3");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedGuid_4) == 120, "CONTAINERTHAT.reservedGuid_4");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughUint64_1) == 136, "CONTAINERTHAT.unassignedPassthroughUint64_1");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughUint64_2) == 144, "CONTAINERTHAT.unassignedPassthroughUint64_2");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughUint64_3) == 152, "CONTAINERTHAT.unassignedPassthroughUint64_3");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughUint64_4) == 160, "CONTAINERTHAT.unassignedPassthroughUint64_4");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, marshalingSetId) == 168, "CONTAINERTHAT.marshalingSetId");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedUint64_2) == 176, "CONTAINERTHAT.reservedUint64_2");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedUint64_3) == 184, "CONTAINERTHAT.reservedUint64_3");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedUint64_4) == 192, "CONTAINERTHAT.reservedUint64_4");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedUint32) == 200, "CONTAINERTHAT.reservedUint32");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, extensions) == 208, "CONTAINERTHAT.extensions");
_Static_assert(sizeof(LOCALTHAT) == 40, "size of LOCALTHAT");
_Static_assert(__builtin_offsetof(LOCALTHAT, marshalingSetId) == 0, "LOCALTHAT.marshalingSetId");
_Static_assert(__builtin_offsetof(LOCALTHAT, reserved) == 8, "LOCALTHAT.reserved");
_Static_assert(__builtin_offsetof(LOCALTHAT, pAsyncResponseBlock) == 16, "LOCALTHAT.pAsyncResponseBlock");
_Static_assert(__builtin_offsetof(LOCALTHAT, containerErrorInformation) == 24, "LOCALTHAT.containerErrorInformation");
_Static_assert(__builtin_offsetof(LOCALTHAT, containerPassthroughData) == 32, "LOCALTHAT.containerPassthroughData");
_Static_assert(sizeof(MInterfacePointer) == 8, "size of MInterfacePointer");
_Static_assert(__builtin_offsetof(MInterfacePointer, ulCntData) == 0, "MInterfacePointer.ulCntData");
_Static_assert(__builtin_offsetof(MInterfacePointer, abData) == 4, "MInterfacePointer.abData");
#else
_Static_assert(sizeof(PHPROCESS) == 4, "size of PHPROCESS");
_Static_assert(sizeof(HSTRING_WIRE) == 12, "size of HSTRING_WIRE");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Magic) == 0, "HSTRING_WIRE.Magic");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Bytes) == 4, "HSTRING_WIRE.Bytes");
_Static_assert(__builtin_offsetof(HSTRING_WIRE, Data) == 8, "HSTRING_WIRE.Data");
_Static_assert(sizeof(RPCSS_HSTRING) == 4, "size of RPCSS_HSTRING");
_Static_assert(sizeof(GUID) == 16, "size of GUID");
_Static_assert(__builtin_offsetof(GUID, Data1) == 0, "GUID.Data1");
_Static_assert(__builtin_offsetof(GUID, Data2) == 4, "GUID.Data2");
_Static_assert(__builtin_offsetof(GUID, Data3) == 6, "GUID.Data3");
_Static_assert(__builtin_offsetof(GUID, Data4) == 8, "GUID.Data4");
_Static_assert(sizeof(REG_INPUT_ENTRY) == 48, "size of REG_INPUT_ENTRY");
_Static_assert(__builtin_offsetof(REG_INPUT_ENTRY, clsid) == 0, "REG_INPUT_ENTRY.clsid");
_Static_assert(__builtin_offsetof(REG_INPUT_ENTRY, oxid) == 16, "REG_INPUT_ENTRY.oxid");
_Static_assert(__builtin_offsetof(REG_INPUT_ENTRY, ipid) == 24, "REG_INPUT_ENTRY.ipid");
_Static_assert(__builtin_offsetof(REG_INPUT_ENTRY, dwFlags) == 40, "REG_INPUT_ENTRY.dwFlags");
_Static_assert(__builtin_offsetof(REG_INPUT_ENTRY, dwReserved) == 44, "REG_INPUT_ENTRY.dwReserved");
_Static_assert(sizeof(REG_INPUT) == 56, "size of REG_INPUT");
_Static_assert(__builtin_offsetof(REG_INPUT, dwSize) == 0, "REG_INPUT.dwSize");
_Static_assert(__builtin_offsetof(REG_INPUT, rginent) == 8, "REG_INPUT.rginent");
_Static_assert(sizeof(REG_OUTPUT) == 8, "size of REG_OUTPUT");
_Static_assert(__builtin_offsetof(REG_OUTPUT, dwSize) == 0, "REG_OUTPUT.dwSize");
_Static_assert(__builtin_offsetof(REG_OUTPUT, RegKeys) == 4, "REG_OUTPUT.RegKeys");
_Static_assert(sizeof(REVOKE_ENTRY) == 20, "size of REVOKE_ENTRY");
_Static_assert(__builtin_offsetof(REVOKE_ENTRY, clsid) == 0, "REVOKE_ENTRY.clsid");
_Static_assert(__builtin_offsetof(REVOKE_ENTRY, dwReg) == 16, "REVOKE_ENTRY.dwReg");
_Static_assert(sizeof(REVOKE_INPUT) == 24, "size of REVOKE_INPUT");
_Static_assert(__builtin_offsetof(REVOKE_INPUT, dwSize) == 0, "REVOKE_INPUT.dwSize");
_Static_assert(__builtin_offsetof(REVOKE_INPUT, revent) == 4, "REVOKE_INPUT.revent");
_Static_assert(sizeof(ACTIVATABLE_CLASS_REG_ENTRY) == 40, "size of ACTIVATABLE_CLASS_REG_ENTRY");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, activatableClassId) == 0, "ACTIVATABLE_CLASS_REG_ENTRY.activatableClassId");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, serverName) == 4, "ACTIVATABLE_CLASS_REG_ENTRY.serverName");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, oxid) == 8, "ACTIVATABLE_CLASS_REG_ENTRY.oxid");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, ipid) == 16, "ACTIVATABLE_CLASS_REG_ENTRY.ipid");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, dwFlags) == 32, "ACTIVATABLE_CLASS_REG_ENTRY.dwFlags");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_ENTRY, dwReserved) == 36, "ACTIVATABLE_CLASS_REG_ENTRY.dwReserved");
_Static_assert(sizeof(ACTIVATABLE_CLASS_REG_INPUT) == 48, "size of ACTIVATABLE_CLASS_REG_INPUT");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_INPUT, dwSize) == 0, "ACTIVATABLE_CLASS_REG_INPUT.dwSize");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_REG_INPUT, entries) == 8, "ACTIVATABLE_CLASS_REG_INPUT.entries");
_Static_assert(sizeof(ACTIVATABLE_CLASS_STOP_ENTRY) == 12, "size of ACTIVATABLE_CLASS_STOP_ENTRY");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_STOP_ENTRY, activatableClassId) == 0, "ACTIVATABLE_CLASS_STOP_ENTRY.activatableClassId");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_STOP_ENTRY, serverName) == 4, "ACTIVATABLE_CLASS_STOP_ENTRY.serverName");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_STOP_ENTRY, dwReg) == 8, "ACTIVATABLE_CLASS_STOP_ENTRY.dwReg");
_Static_assert(sizeof(ACTIVATABLE_CLASS_STOP_INPUT) == 16, "size of ACTIVATABLE_CLASS_STOP_INPUT");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_STOP_INPUT, dwSize) == 0, "ACTIVATABLE_CLASS_STOP_INPUT.dwSize");
_Static_assert(__builtin_offsetof(ACTIVATABLE_CLASS_STOP_INPUT, entries) == 4, "ACTIVATABLE_CLASS_STOP_INPUT.entries");
_Static_assert(sizeof(STDOBJREF) == 40, "size of STDOBJREF");
_Static_assert(__builtin_offsetof(STDOBJREF, flags) == 0, "STDOBJREF.flags");
_Static_assert(__builtin_offsetof(STDOBJREF, cPublicRefs) == 4, "STDOBJREF.cPublicRefs");
_Static_assert(__builtin_offsetof(STDOBJREF, oxid) == 8, "STDOBJREF.oxid");
_Static_assert(__builtin_offsetof(STDOBJREF, oid) == 16, "STDOBJREF.oid");
_Static_assert(__builtin_offsetof(STDOBJREF, ipid) == 24, "STDOBJREF.ipid");
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
_Static_assert(sizeof(DUALSTRINGARRAY) == 6, "size of DUALSTRINGARRAY");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, wNumEntries) == 0, "DUALSTRINGARRAY.wNumEntries");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, wSecurityOffset) == 2, "DUALSTRINGARRAY.wSecurityOffset");
_Static_assert(__builtin_offsetof(DUALSTRINGARRAY, aStringArray) == 4, "DUALSTRINGARRAY.aStringArray");
_Static_assert(sizeof(OR_OXID_CLIENT_DEPENDENCY) == 4, "size of OR_OXID_CLIENT_DEPENDENCY");
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
_Static_assert(sizeof(ORPC_EXTENT) == 24, "size of ORPC_EXTENT");
_Static_assert(__builtin_offsetof(ORPC_EXTENT, id) == 0, "ORPC_EXTENT.id");
_Static_assert(__builtin_offsetof(ORPC_EXTENT, size) == 16, "ORPC_EXTENT.size");
_Static_assert(__builtin_offsetof(ORPC_EXTENT, data) == 20, "ORPC_EXTENT.data");
_Static_assert(sizeof(ORPC_EXTENT_ARRAY) == 12, "size of ORPC_EXTENT_ARRAY");
_Static_assert(__builtin_offsetof(ORPC_EXTENT_ARRAY, size) == 0, "ORPC_EXTENT_ARRAY.size");
_Static_assert(__builtin_offsetof(ORPC_EXTENT_ARRAY, reserved) == 4, "ORPC_EXTENT_ARRAY.reserved");
_Static_assert(__builtin_offsetof(ORPC_EXTENT_ARRAY, extent) == 8, "ORPC_EXTENT_ARRAY.extent");
_Static_assert(sizeof(ORPCTHIS) == 32, "size of ORPCTHIS");
_Static_assert(__builtin_offsetof(ORPCTHIS, version) == 0, "ORPCTHIS.version");
_Static_assert(__builtin_offsetof(ORPCTHIS, flags) == 4, "ORPCTHIS.flags");
_Static_assert(__builtin_offsetof(ORPCTHIS, reserved1) == 8, "ORPCTHIS.reserved1");
_Static_assert(__builtin_offsetof(ORPCTHIS, cid) == 12, "ORPCTHIS.cid");
_Static_assert(__builtin_offsetof(ORPCTHIS, extensions) == 28, "ORPCTHIS.extensions");
_Static_assert(sizeof(ASYNC_REQUEST_BLOCK) == 48, "size of ASYNC_REQUEST_BLOCK");
_Static_assert(__builtin_offsetof(ASYNC_REQUEST_BLOCK, asyncOperationId) == 0, "ASYNC_REQUEST_BLOCK.asyncOperationId");
_Static_assert(__builtin_offsetof(ASYNC_REQUEST_BLOCK, oxidClientProcessNA) == 16, "ASYNC_REQUEST_BLOCK.oxidClientProcessNA");
_Static_assert(__builtin_offsetof(ASYNC_REQUEST_BLOCK, originalClientLogicalThreadId) == 24, "ASYNC_REQUEST_BLOCK.originalClientLogicalThreadId");
_Static_assert(__builtin_offsetof(ASYNC_REQUEST_BLOCK, uClientCausalityTraceId) == 40, "ASYNC_REQUEST_BLOCK.uClientCausalityTraceId");
_Static_assert(sizeof(TOUCHED_ASTA_ARRAY) == 12, "size of TOUCHED_ASTA_ARRAY");
_Static_assert(__builtin_offsetof(TOUCHED_ASTA_ARRAY, size) == 0, "TOUCHED_ASTA_ARRAY.size");
_Static_assert(__builtin_offsetof(TOUCHED_ASTA_ARRAY, reserved) == 4, "TOUCHED_ASTA_ARRAY.reserved");
_Static_assert(__builtin_offsetof(TOUCHED_ASTA_ARRAY, pAstaOxids) == 8, "TOUCHED_ASTA_ARRAY.pAstaOxids");
_Static_assert(sizeof(CONTAINER_EXTENT) == 16, "size of CONTAINER_EXTENT");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT, id) == 0, "CONTAINER_EXTENT.id");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT, version) == 4, "CONTAINER_EXTENT.version");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT, size) == 8, "CONTAINER_EXTENT.size");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT, data) == 12, "CONTAINER_EXTENT.data");
_Static_assert(sizeof(CONTAINER_EXTENT_ARRAY) == 12, "size of CONTAINER_EXTENT_ARRAY");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT_ARRAY, size) == 0, "CONTAINER_EXTENT_ARRAY.size");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT_ARRAY, reserved) == 4, "CONTAINER_EXTENT_ARRAY.reserved");
_Static_assert(__builtin_offsetof(CONTAINER_EXTENT_ARRAY, extent) == 8, "CONTAINER_EXTENT_ARRAY.extent");
_Static_assert(sizeof(CONTAINERTHIS) == 224, "size of CONTAINERTHIS");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, dcomVersionUnused) == 0, "CONTAINERTHIS.dcomVersionUnused");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, version) == 4, "CONTAINERTHIS.version");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, capabilityFlags) == 8, "CONTAINERTHIS.capabilityFlags");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, requestFlags) == 16, "CONTAINERTHIS.requestFlags");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, causalityId) == 24, "CONTAINERTHIS.causalityId");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedSyncPassthroughGuid) == 40, "CONTAINERTHIS.unassignedSyncPassthroughGuid");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, passthroughTraceActivity) == 56, "CONTAINERTHIS.passthroughTraceActivity");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedAlwaysPassthroughGuid_2) == 72, "CONTAINERTHIS.unassignedAlwaysPassthroughGuid_2");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, callId) == 88, "CONTAINERTHIS.callId");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedGuid_2) == 104, "CONTAINERTHIS.reservedGuid_2");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedGuid_3) == 120, "CONTAINERTHIS.reservedGuid_3");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedGuid_4) == 136, "CONTAINERTHIS.reservedGuid_4");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedSyncPassthroughUint64_1) == 152, "CONTAINERTHIS.unassignedSyncPassthroughUint64_1");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedSyncPassthroughUint64_2) == 160, "CONTAINERTHIS.unassignedSyncPassthroughUint64_2");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedAlwaysPassthroughUint64_1) == 168, "CONTAINERTHIS.unassignedAlwaysPassthroughUint64_1");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, unassignedAlwaysPassthroughUint64_2) == 176, "CONTAINERTHIS.unassignedAlwaysPassthroughUint64_2");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedUint64_1) == 184, "CONTAINERTHIS.reservedUint64_1");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedUint64_2) == 192, "CONTAINERTHIS.reservedUint64_2");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedUint64_3) == 200, "CONTAINERTHIS.reservedUint64_3");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedUint64_4) == 208, "CONTAINERTHIS.reservedUint64_4");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, reservedUint32) == 216, "CONTAINERTHIS.reservedUint32");
_Static_assert(__builtin_offsetof(CONTAINERTHIS, extensions) == 220, "CONTAINERTHIS.extensions");
_Static_assert(sizeof(LOCALTHIS) == 96, "size of LOCALTHIS");
_Static_assert(__builtin_offsetof(LOCALTHIS, dwFlags) == 0, "LOCALTHIS.dwFlags");
_Static_assert(__builtin_offsetof(LOCALTHIS, dwClientThread) == 4, "LOCALTHIS.dwClientThread");
_Static_assert(__builtin_offsetof(LOCALTHIS, passthroughTraceActivity) == 8, "LOCALTHIS.passthroughTraceActivity");
_Static_assert(__builtin_offsetof(LOCALTHIS, callId) == 24, "LOCALTHIS.callId");
_Static_assert(__builtin_offsetof(LOCALTHIS, asyncRequestBlock) == 40, "LOCALTHIS.asyncRequestBlock");
_Static_assert(__builtin_offsetof(LOCALTHIS, pTouchedAstaArray) == 88, "LOCALTHIS.pTouchedAstaArray");
_Static_assert(__builtin_offsetof(LOCALTHIS, containerPassthroughData) == 92, "LOCALTHIS.containerPassthroughData");
_Static_assert(sizeof(ORPCTHAT) == 8, "size of ORPCTHAT");
_Static_assert(__builtin_offsetof(ORPCTHAT, flags) == 0, "ORPCTHAT.flags");
_Static_assert(__builtin_offsetof(ORPCTHAT, extensions) == 4, "ORPCTHAT.extensions");
_Static_assert(sizeof(ASYNC_SUCCESS) == 12, "size of ASYNC_SUCCESS");
_Static_assert(__builtin_offsetof(ASYNC_SUCCESS, sizeOfMarshaledResults) == 0, "ASYNC_SUCCESS.sizeOfMarshaledResults");
_Static_assert(__builtin_offsetof(ASYNC_SUCCESS, reserved) == 4, "ASYNC_SUCCESS.reserved");
_Static_assert(__builtin_offsetof(ASYNC_SUCCESS, pMarshaledResults) == 8, "ASYNC_SUCCESS.pMarshaledResults");
_Static_assert(sizeof(ASYNC_FAILURE) == 20, "size of ASYNC_FAILURE");
_Static_assert(__builtin_offsetof(ASYNC_FAILURE, pointOfFailure) == 0, "ASYNC_FAILURE.pointOfFailure");
_Static_assert(__builtin_offsetof(ASYNC_FAILURE, hrFailure) == 4, "ASYNC_FAILURE.hrFailure");
_Static_assert(__builtin_offsetof(ASYNC_FAILURE, sizeOfMarshaledErrorInfo) == 8, "ASYNC_FAILURE.sizeOfMarshaledErrorInfo");
_Static_assert(__builtin_offsetof(ASYNC_FAILURE, reserved) == 12, "ASYNC_FAILURE.reserved");
_Static_assert(__builtin_offsetof(ASYNC_FAILURE, pMarshaledErrorInfo) == 16, "ASYNC_FAILURE.pMarshaledErrorInfo");
_Static_assert(sizeof(ASYNC_OUTCOME_DETAILS) == 24, "size of ASYNC_OUTCOME_DETAILS");
_Static_assert(__builtin_offsetof(ASYNC_OUTCOME_DETAILS, u) == 4, "ASYNC_OUTCOME_DETAILS.u");
_Static_assert(sizeof(ASYNC_RESPONSE_BLOCK) == 40, "size of ASYNC_RESPONSE_BLOCK");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, asyncStatus) == 0, "ASYNC_RESPONSE_BLOCK.asyncStatus");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, reserved1) == 4, "ASYNC_RESPONSE_BLOCK.reserved1");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, uServerCausalityTraceId) == 8, "ASYNC_RESPONSE_BLOCK.uServerCausalityTraceId");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, completionTraceActivity) == 16, "ASYNC_RESPONSE_BLOCK.completionTraceActivity");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, reserved2) == 32, "ASYNC_RESPONSE_BLOCK.reserved2");
_Static_assert(__builtin_offsetof(ASYNC_RESPONSE_BLOCK, pOutcomeDetails) == 36, "ASYNC_RESPONSE_BLOCK.pOutcomeDetails");
_Static_assert(sizeof(CONTAINERTHAT) == 208, "size of CONTAINERTHAT");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, responseFlags) == 0, "CONTAINERTHAT.responseFlags");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughGuid_1) == 8, "CONTAINERTHAT.unassignedPassthroughGuid_1");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughGuid_2) == 24, "CONTAINERTHAT.unassignedPassthroughGuid_2");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughGuid_3) == 40, "CONTAINERTHAT.unassignedPassthroughGuid_3");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughGuid_4) == 56, "CONTAINERTHAT.unassignedPassthroughGuid_4");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedGuid_1) == 72, "CONTAINERTHAT.reservedGuid_1");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedGuid_2) == 88, "CONTAINERTHAT.reservedGuid_2");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedGuid_3) == 104, "CONTAINERTHAT.reservedGuid_3");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedGuid_4) == 120, "CONTAINERTHAT.reservedGuid_4");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughUint64_1) == 136, "CONTAINERTHAT.unassignedPassthroughUint64_1");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughUint64_2) == 144, "CONTAINERTHAT.unassignedPassthroughUint64_2");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughUint64_3) == 152, "CONTAINERTHAT.unassignedPassthroughUint64_3");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, unassignedPassthroughUint64_4) == 160, "CONTAINERTHAT.unassignedPassthroughUint64_4");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, marshalingSetId) == 168, "CONTAINERTHAT.marshalingSetId");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedUint64_2) == 176, "CONTAINERTHAT.reservedUint64_2");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedUint64_3) == 184, "CONTAINERTHAT.reservedUint64_3");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedUint64_4) == 192, "CONTAINERTHAT.reservedUint64_4");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, reservedUint32) == 200, "CONTAINERTHAT.reservedUint32");
_Static_assert(__builtin_offsetof(CONTAINERTHAT, extensions) == 204, "CONTAINERTHAT.extensions");
_Static_assert(sizeof(LOCALTHAT) == 24, "size of LOCALTHAT");
_Static_assert(__builtin_offsetof(LOCALTHAT, marshalingSetId) == 0, "LOCALTHAT.marshalingSetId");
_Static_assert(__builtin_offsetof(LOCALTHAT, reserved) == 8, "LOCALTHAT.reserved");
_Static_assert(__builtin_offsetof(LOCALTHAT, pAsyncResponseBlock) == 12, "LOCALTHAT.pAsyncResponseBlock");
_Static_assert(__builtin_offsetof(LOCALTHAT, containerErrorInformation) == 16, "LOCALTHAT.containerErrorInformation");
_Static_assert(__builtin_offsetof(LOCALTHAT, containerPassthroughData) == 20, "LOCALTHAT.containerPassthroughData");
_Static_assert(sizeof(MInterfacePointer) == 8, "size of MInterfacePointer");
_Static_assert(__builtin_offsetof(MInterfacePointer, ulCntData) == 0, "MInterfacePointer.ulCntData");
_Static_assert(__builtin_offsetof(MInterfacePointer, abData) == 4, "MInterfacePointer.abData");
#endif

/* ------------------------------------------------------------------ interface ISCM */

#ifndef ISCM_CLIENT_ONLY
extern long WINAPI ISCM_NotifyStarted_s(handle_t hRpc, PHPROCESS phProcess, unsigned long cPackages, RPCSS_HSTRING *packages, REG_INPUT *pRegInput, REG_OUTPUT **ppRegOutput, error_status_t *prpcstat);
extern void WINAPI ISCM_NotifyStopped_s(handle_t hRpc, PHPROCESS phProcess, REVOKE_INPUT *pRevInput, error_status_t *prpcstat);
extern long WINAPI ISCM_NotifyActivatableClassStarted_s(handle_t hRpc, PHPROCESS phProcess, ACTIVATABLE_CLASS_REG_INPUT *pRegInput, REG_OUTPUT **ppRegOutput, error_status_t *prpcstat);
extern void WINAPI ISCM_NotifyActivatableClassStopped_s(handle_t hRpc, PHPROCESS phProcess, ACTIVATABLE_CLASS_STOP_INPUT *pRevInput, error_status_t *prpcstat);
extern void WINAPI ISCM_GetThreadID_s(handle_t hRpc, PHPROCESS phProcess, unsigned long *pThreadID, error_status_t *prpcstat);
extern void WINAPI ISCM_UpdateActivationSettings_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat);
extern long WINAPI ISCM_RegisterWindowPropInterface_s(handle_t hRpc, PHPROCESS phProcess, UINT_PTR hWnd, STDOBJREF *pStd, OXID_INFO *pOxidInfo, unsigned long fFlags, UINT_PTR *pdwCookie, error_status_t *prpcstat);
extern long WINAPI ISCM_GetWindowPropInterface_s(handle_t hRpc, PHPROCESS phProcess, UINT_PTR hWnd, UINT_PTR dwCookie, unsigned long fRevoke, STDOBJREF *pStd, OXID_INFO *pOxidInfo, error_status_t *prpcstat);
extern long WINAPI ISCM_EnableDisableDynamicIPTracking_s(handle_t hRpc, PHPROCESS phProcess, unsigned long fEnable, error_status_t *prpcstat);
extern long WINAPI ISCM_GetCurrentAddrExclusionList_s(handle_t hRpc, PHPROCESS phProcess, unsigned long *pdwNumStrings, unsigned short ***pppszStrings, error_status_t *prpcstat);
extern long WINAPI ISCM_SetAddrExclusionList_s(handle_t hRpc, PHPROCESS phProcess, unsigned long dwNumStrings, unsigned short **ppszStrings, error_status_t *prpcstat);
extern long WINAPI ISCM_FlushSCMBindings_s(handle_t hRpc, PHPROCESS phProcess, unsigned short *pszMachineName, error_status_t *prpcstat);
extern long WINAPI ISCM_RetireServer_s(handle_t hRpc, PHPROCESS phProcess, GUID *pguidProcessIdentifier, error_status_t *prpcstat);
extern long WINAPI ISCM_NotifyDDStartOrStop_s(handle_t hRpc, PHPROCESS phProcess, unsigned long fStart, error_status_t *prpcstat);
extern long WINAPI ISCM_IsObjectCreationAllowed_s(handle_t hRpc, PHPROCESS phProcess, GUID *clsid, unsigned long cPackages, RPCSS_HSTRING *packages, unsigned long cMains, RPCSS_HSTRING *mains, unsigned long dwFlags, unsigned long dwContext, unsigned long *pfAllowed, unsigned long *pfRegistered, unsigned long *pdwProcessId, error_status_t *prpcstat);
extern long WINAPI ISCM_ControlTracingForProcess_s(handle_t hRpc, PHPROCESS phProcess, unsigned long dwPID, unsigned long fEnable, error_status_t *prpcstat);
extern long WINAPI ISCM_QueryPIDForActivation_s(handle_t hRpc, PHPROCESS phProcess, GUID *clsid, unsigned long dwClsCtx, unsigned long *pdwPID, error_status_t *prpcstat);
extern long WINAPI ISCM_NotifyWinRTActivationStoreChanged_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat);
extern long WINAPI ISCM_DecodeProxy_s(handle_t hRpc, PHPROCESS phProcess, unsigned long dwClientPid, unsigned long long ui64ProxyAddress, unsigned long *pdwServerPid, unsigned long *pdwServerTid, unsigned long long *pui64ServerAddress, error_status_t *prpcstat);
extern long WINAPI ISCM_NotifyPsmResume_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat);
extern long WINAPI ISCM_QueryServerProcessHandleHeld_s(handle_t hRpc, PHPROCESS phProcess, GUID *pguidProcess, unsigned long *pfHeld, error_status_t *prpcstat);
extern long WINAPI ISCM_RegisterRacActivationToken_s(handle_t hRpc, PHPROCESS phProcess, unsigned long hToken, unsigned long long *pId, error_status_t *prpcstat);
extern long WINAPI ISCM_RevokeRacActivationToken_s(handle_t hRpc, PHPROCESS phProcess, unsigned long long id, error_status_t *prpcstat);
extern long WINAPI ISCM_RegisterConsoleHandles_s(handle_t hRpc, PHPROCESS phProcess, unsigned long hInput, unsigned long hOutput, unsigned long hError, unsigned long long *pId, error_status_t *prpcstat);
extern long WINAPI ISCM_RevokeConsoleHandles_s(handle_t hRpc, PHPROCESS phProcess, unsigned long long id, error_status_t *prpcstat);
extern long WINAPI ISCM_NotifyComClassChangesFromDeployment_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat);
extern long WINAPI ISCM_LogMachineClassesRootPermissions_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat);
#endif
ULONG WINAPI RPCSS_HSTRING_UserSize(ULONG *, ULONG, RPCSS_HSTRING *);
unsigned char *WINAPI RPCSS_HSTRING_UserMarshal(ULONG *, unsigned char *, RPCSS_HSTRING *);
unsigned char *WINAPI RPCSS_HSTRING_UserUnmarshal(ULONG *, unsigned char *, RPCSS_HSTRING *);
void WINAPI RPCSS_HSTRING_UserFree(ULONG *, RPCSS_HSTRING *);
void WINAPI PHPROCESS_rundown(void *);
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char ISCM_TypeFormat[] = {
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
    0x11, 0x00, 0x02, 0x00, /* 66: pointer */
    0x17, 0x07, 0x08, 0x00, 0x05, 0x00, 0x08, 0x40, 0x5b, /* 70: struct REG_INPUT (CSTRUCT) */
    0x1b, 0x07, 0x30, 0x00, 0x08, 0x00, 0xf8, 0xff, 0x01, 0x00, 0x4c, 0x00, /* 79: array */
    0x03, 0x00, 0x5b,
    0x15, 0x07, 0x30, 0x00, 0x43, 0x43, 0x3e, 0x0b, 0x43, 0x43, 0x3e, 0x08, /* 94: struct REG_INPUT_ENTRY (STRUCT) */
    0x08, 0x5b,
    0x11, 0x14, 0x02, 0x00, /* 108: pointer */
    0x12, 0x00, 0x02, 0x00, /* 112: pointer */
    0x17, 0x03, 0x04, 0x00, 0x04, 0x00, 0x08, 0x5b, /* 116: struct REG_OUTPUT (CSTRUCT) */
    0x1b, 0x03, 0x04, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x08, 0x5b, /* 124: array */
    0x11, 0x00, 0x02, 0x00, /* 136: pointer */
    0x17, 0x03, 0x04, 0x00, 0x04, 0x00, 0x08, 0x5b, /* 140: struct REVOKE_INPUT (CSTRUCT) */
    0x1b, 0x03, 0x14, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x4c, 0x00, /* 148: array */
    0x03, 0x00, 0x5b,
    0x15, 0x03, 0x14, 0x00, 0x43, 0x43, 0x3e, 0x08, 0x5b, /* 163: struct REVOKE_ENTRY (STRUCT) */
    0x11, 0x00, 0x02, 0x00, /* 172: pointer */
    0x1a, 0x07, 0x08, 0x00, 0x07, 0x00, 0x00, 0x00, 0x08, 0x40, 0x5b, /* 176: struct ACTIVATABLE_CLASS_REG_INPUT (BOGUS) */
    0x21, 0x07, 0x00, 0x00, 0x08, 0x00, 0xf8, 0xff, 0x01, 0x00, 0xff, 0xff, /* 187: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x07, 0x30, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x36, 0x36, 0x0b, 0x4c, /* 208: struct ACTIVATABLE_CLASS_REG_ENTRY (BOGUS) */
    0x00, 0x0d, 0x00, 0x08, 0x08, 0x5b, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08,
    0x25, 0x5c,
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 234: struct GUID (STRUCT) */
    0x11, 0x14, 0x02, 0x00, /* 244: pointer */
    0x12, 0x00, 0x7a, 0xff, /* 248: pointer */
    0x11, 0x00, 0x02, 0x00, /* 252: pointer */
    0x1a, 0x03, 0x08, 0x00, 0x07, 0x00, 0x00, 0x00, 0x08, 0x40, 0x5b, /* 256: struct ACTIVATABLE_CLASS_STOP_INPUT (BOGUS) */
    0x21, 0x03, 0x00, 0x00, 0x08, 0x00, 0xf8, 0xff, 0x01, 0x00, 0xff, 0xff, /* 267: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x07, 0x00, 0x36, 0x36, 0x08, 0x40, /* 288: struct ACTIVATABLE_CLASS_STOP_ENTRY (BOGUS) */
    0x5b, 0x12, 0x08, 0x25, 0x5c, 0x12, 0x08, 0x25, 0x5c,
    0x15, 0x07, 0x28, 0x00, 0x08, 0x08, 0x0b, 0x0b, 0x43, 0x43, 0x3e, 0x5b, /* 309: struct STDOBJREF (STRUCT) */
    0x1a, 0x07, 0x98, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x08, 0x08, 0x08, 0x4c, /* 321: struct OXID_INFO (BOGUS) */
    0x00, 0x29, 0x00, 0x4c, 0x00, 0x2c, 0x00, 0x4c, 0x00, 0x94, 0xff, 0x08,
    0x40, 0x36, 0x4c, 0x00, 0x8d, 0xff, 0x0b, 0x0d, 0x4c, 0x04, 0xbc, 0xfe,
    0x4c, 0x00, 0xb8, 0xfe, 0x4c, 0x00, 0xb4, 0xfe, 0x0b, 0x4c, 0x00, 0x7a,
    0xff, 0x5b, 0x12, 0x00, 0x55, 0x00,
    0x15, 0x01, 0x04, 0x00, 0x06, 0x06, 0x5b, /* 375: struct COMVERSION (STRUCT) */
    0x1a, 0x07, 0x18, 0x00, 0x00, 0x00, 0x07, 0x00, 0x08, 0x40, 0x0b, 0x36, /* 382: struct CONTAINERVERSION (BOGUS) */
    0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 399: struct CONTAINER_EXTENSIONS (BOGUS) */
    0x11, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 415: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x0c, 0x00, 0x06, 0x00, 0x08, 0x08, 0x08, 0x5b, /* 436: struct CONTAINER_EXTENSION (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 446: array */
    0x17, 0x01, 0x04, 0x00, 0x05, 0x00, 0x06, 0x06, 0x5b, /* 458: struct DUALSTRINGARRAY (CSTRUCT) */
    0x1b, 0x01, 0x02, 0x00, 0x06, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x06, 0x5b, /* 467: array */
    0x11, 0x14, 0x02, 0x00, /* 479: pointer */
    0x12, 0x00, 0x02, 0x00, /* 483: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x54, 0x10, 0x00, 0x00, 0x00, 0xff, 0xff, /* 487: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x08, 0x25, 0x5c, 0x5b,
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, /* 508: range */
    0x12, 0x00, 0x02, 0x00, /* 518: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x10, 0x00, 0x00, 0x00, 0xff, 0xff, /* 522: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x08, 0x25, 0x5c, 0x5b,
    0x11, 0x08, 0x25, 0x5c, /* 543: pointer */
    0x12, 0x00, 0x02, 0x00, /* 547: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x18, 0x00, 0x00, 0x00, 0xff, 0xff, /* 551: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0xe6, 0xfd, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 572: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x28, 0x00, 0x00, 0x00, 0xff, 0xff, /* 576: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0xcd, 0xfd, 0x5b,
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
    0x11, 0x00, 0x02, 0x00, /* 66: pointer */
    0x17, 0x07, 0x08, 0x00, 0x05, 0x00, 0x08, 0x40, 0x5b, /* 70: struct REG_INPUT (CSTRUCT) */
    0x1b, 0x07, 0x30, 0x00, 0x08, 0x00, 0xf8, 0xff, 0x01, 0x00, 0x4c, 0x00, /* 79: array */
    0x03, 0x00, 0x5b,
    0x15, 0x07, 0x30, 0x00, 0x43, 0x43, 0x3e, 0x0b, 0x43, 0x43, 0x3e, 0x08, /* 94: struct REG_INPUT_ENTRY (STRUCT) */
    0x08, 0x5b,
    0x11, 0x14, 0x02, 0x00, /* 108: pointer */
    0x12, 0x00, 0x02, 0x00, /* 112: pointer */
    0x17, 0x03, 0x04, 0x00, 0x04, 0x00, 0x08, 0x5b, /* 116: struct REG_OUTPUT (CSTRUCT) */
    0x1b, 0x03, 0x04, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x08, 0x5b, /* 124: array */
    0x11, 0x00, 0x02, 0x00, /* 136: pointer */
    0x17, 0x03, 0x04, 0x00, 0x04, 0x00, 0x08, 0x5b, /* 140: struct REVOKE_INPUT (CSTRUCT) */
    0x1b, 0x03, 0x14, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x4c, 0x00, /* 148: array */
    0x03, 0x00, 0x5b,
    0x15, 0x03, 0x14, 0x00, 0x43, 0x43, 0x3e, 0x08, 0x5b, /* 163: struct REVOKE_ENTRY (STRUCT) */
    0x11, 0x00, 0x02, 0x00, /* 172: pointer */
    0x1a, 0x07, 0x08, 0x00, 0x07, 0x00, 0x00, 0x00, 0x08, 0x40, 0x5b, /* 176: struct ACTIVATABLE_CLASS_REG_INPUT (BOGUS) */
    0x1b, 0x07, 0x28, 0x00, 0x08, 0x00, 0xf8, 0xff, 0x01, 0x00, 0x4b, 0x5c, /* 187: array */
    0x48, 0x49, 0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x04, 0x00, 0x04, 0x00, 0x12, 0x08, 0x25, 0x5c,
    0x5b, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x16, 0x07, 0x28, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 229: struct ACTIVATABLE_CLASS_REG_ENTRY (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, 0x12, 0x08,
    0x25, 0x5c, 0x5b, 0x36, 0x36, 0x0b, 0x43, 0x43, 0x3e, 0x08, 0x08, 0x5b,
    0x11, 0x14, 0x02, 0x00, /* 265: pointer */
    0x12, 0x00, 0x65, 0xff, /* 269: pointer */
    0x11, 0x00, 0x02, 0x00, /* 273: pointer */
    0x1a, 0x03, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x08, 0x5b, /* 277: struct ACTIVATABLE_CLASS_STOP_INPUT (BOGUS) */
    0x1b, 0x03, 0x0c, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x4b, 0x5c, /* 287: array */
    0x48, 0x49, 0x0c, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x08, 0x25, 0x5c, 0x04, 0x00, 0x04, 0x00, 0x12, 0x08, 0x25, 0x5c,
    0x5b, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 329: struct ACTIVATABLE_CLASS_STOP_ENTRY (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, 0x12, 0x08,
    0x25, 0x5c, 0x5b, 0x36, 0x36, 0x08, 0x5b,
    0x15, 0x07, 0x28, 0x00, 0x08, 0x08, 0x0b, 0x0b, 0x43, 0x43, 0x3e, 0x5b, /* 360: struct STDOBJREF (STRUCT) */
    0x1a, 0x07, 0x80, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x08, 0x08, 0x08, 0x4c, /* 372: struct OXID_INFO (BOGUS) */
    0x00, 0x28, 0x00, 0x4c, 0x00, 0x2b, 0x00, 0x4c, 0x00, 0x79, 0x00, 0x08,
    0x36, 0x4c, 0x00, 0x73, 0x00, 0x0b, 0x0d, 0x4c, 0x00, 0x8a, 0xfe, 0x4c,
    0x00, 0x86, 0xfe, 0x4c, 0x00, 0x82, 0xfe, 0x0b, 0x4c, 0x00, 0x60, 0x00,
    0x5b, 0x12, 0x00, 0x65, 0x00,
    0x15, 0x01, 0x04, 0x00, 0x06, 0x06, 0x5b, /* 425: struct COMVERSION (STRUCT) */
    0x1a, 0x07, 0x18, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x40, 0x0b, 0x36, /* 432: struct CONTAINERVERSION (BOGUS) */
    0x40, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 450: struct CONTAINER_EXTENSIONS (PSTRUCT) */
    0x11, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 471: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x0c, 0x00, 0x06, 0x00, 0x08, 0x08, 0x08, 0x5b, /* 492: struct CONTAINER_EXTENSION (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 502: array */
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 514: struct GUID (STRUCT) */
    0x17, 0x01, 0x04, 0x00, 0x05, 0x00, 0x06, 0x06, 0x5b, /* 524: struct DUALSTRINGARRAY (CSTRUCT) */
    0x1b, 0x01, 0x02, 0x00, 0x06, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x06, 0x5b, /* 533: array */
    0x11, 0x14, 0x02, 0x00, /* 545: pointer */
    0x12, 0x00, 0x02, 0x00, /* 549: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x54, 0x08, 0x00, 0x00, 0x00, 0xff, 0xff, /* 553: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x08, 0x25, 0x5c, 0x5b,
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, /* 574: range */
    0x12, 0x00, 0x02, 0x00, /* 584: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x08, 0x00, 0x00, 0x00, 0xff, 0xff, /* 588: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x08, 0x25, 0x5c, 0x5b,
    0x11, 0x08, 0x25, 0x5c, /* 609: pointer */
    0x12, 0x00, 0x02, 0x00, /* 613: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x0c, 0x00, 0x00, 0x00, 0xff, 0xff, /* 617: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0xa4, 0xfd, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 638: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x14, 0x00, 0x00, 0x00, 0xff, 0xff, /* 642: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x8b, 0xfd, 0x5b,
#endif
};

static const unsigned char ISCM_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x40, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 2: ISCM.NotifyStarted */
    0x0f, 0x00, 0x47, 0x07, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x0b, 0x00, 0x18, 0x00, 0x06, 0x00, 0x0b, 0x00, 0x20, 0x00,
    0x42, 0x00, 0x13, 0x20, 0x28, 0x00, 0x6c, 0x00, 0x50, 0x21, 0x30, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x38, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 70: ISCM.NotifyStopped */
    0x07, 0x00, 0x42, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x10, 0x00,
    0x88, 0x00, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 114: ISCM.NotifyActivatableClassStarted */
    0x0f, 0x00, 0x47, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x10, 0x00,
    0xac, 0x00, 0x13, 0x20, 0x18, 0x00, 0xf4, 0x00, 0x50, 0x21, 0x20, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x28, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 170: ISCM.NotifyActivatableClassStopped */
    0x07, 0x00, 0x42, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x10, 0x00,
    0xfc, 0x00, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 214: ISCM.GetThreadID */
    0x0f, 0x00, 0x40, 0x03, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 258: ISCM.UpdateActivationSettings */
    0x07, 0x00, 0x40, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x10, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x48, 0x00, 0x32, 0x00, 0x00, 0x00, 0x57, 0x00, /* 296: ISCM.RegisterWindowPropInterface */
    0x17, 0x00, 0x46, 0x08, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0xb9, 0x5c, 0x0a, 0x01, 0x18, 0x00, 0x35, 0x01, 0x0b, 0x01, 0x20, 0x00,
    0x41, 0x01, 0x48, 0x00, 0x28, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x30, 0x00,
    0xb9, 0x5c, 0x50, 0x21, 0x38, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x40, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x48, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2f, 0x00, /* 370: ISCM.GetWindowPropInterface */
    0x3f, 0x00, 0x45, 0x08, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0xb9, 0x5c, 0x48, 0x00, 0x18, 0x00, 0xb9, 0x5c, 0x48, 0x00, 0x20, 0x00,
    0x08, 0x5c, 0x12, 0xa1, 0x28, 0x00, 0x35, 0x01, 0x13, 0x01, 0x30, 0x00,
    0x41, 0x01, 0x50, 0x21, 0x38, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x40, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 444: ISCM.EnableDisableDynamicIPTracking */
    0x0f, 0x00, 0x44, 0x04, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x20, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x09, 0x00, 0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 494: ISCM.GetCurrentAddrExclusionList */
    0x17, 0x00, 0x45, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x08, 0x5c, 0x13, 0x20, 0x18, 0x00, 0xdf, 0x01, 0x50, 0x21, 0x20, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x28, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0a, 0x00, 0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 550: ISCM.SetAddrExclusionList */
    0x0f, 0x00, 0x46, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x88, 0x00, 0x10, 0x00,
    0xfc, 0x01, 0x0b, 0x00, 0x18, 0x00, 0x06, 0x02, 0x50, 0x21, 0x20, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x28, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0b, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 606: ISCM.FlushSCMBindings */
    0x0f, 0x00, 0x46, 0x04, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x10, 0x00,
    0x1f, 0x02, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x20, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x0c, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2b, 0x00, /* 656: ISCM.RetireServer */
    0x0f, 0x00, 0x44, 0x04, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0a, 0x01, 0x10, 0x00,
    0xea, 0x00, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x20, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x0d, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 706: ISCM.NotifyDDStartOrStop */
    0x0f, 0x00, 0x44, 0x04, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x20, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x0e, 0x00, 0x70, 0x00, 0x32, 0x00, 0x00, 0x00, 0x4b, 0x00, /* 756: ISCM.IsObjectCreationAllowed */
    0x27, 0x00, 0x46, 0x0d, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0a, 0x01, 0x10, 0x00,
    0xea, 0x00, 0x48, 0x00, 0x18, 0x00, 0x08, 0x5c, 0x0b, 0x00, 0x20, 0x00,
    0x23, 0x02, 0x48, 0x00, 0x28, 0x00, 0x08, 0x5c, 0x0b, 0x00, 0x30, 0x00,
    0x3c, 0x02, 0x48, 0x00, 0x38, 0x00, 0x08, 0x5c, 0x48, 0x00, 0x40, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x48, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x50, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x58, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x60, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x68, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0f, 0x00, 0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x27, 0x00, /* 860: ISCM.ControlTracingForProcess */
    0x0f, 0x00, 0x44, 0x05, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x48, 0x00, 0x18, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x20, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x28, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x10, 0x00, 0x38, 0x00, 0x32, 0x00, 0x00, 0x00, 0x33, 0x00, /* 916: ISCM.QueryPIDForActivation */
    0x17, 0x00, 0x44, 0x06, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0a, 0x01, 0x10, 0x00,
    0xea, 0x00, 0x48, 0x00, 0x18, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x20, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x28, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x30, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x11, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 978: ISCM.NotifyWinRTActivationStoreChanged */
    0x0f, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x12, 0x00, 0x48, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2f, 0x00, /* 1022: ISCM.DecodeProxy */
    0x2f, 0x00, 0x44, 0x08, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x48, 0x00, 0x18, 0x00, 0x0b, 0x5c, 0x50, 0x21, 0x20, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x28, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x30, 0x00,
    0x0b, 0x5c, 0x50, 0x21, 0x38, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x40, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x13, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 1096: ISCM.NotifyPsmResume */
    0x0f, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x14, 0x00, 0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2b, 0x00, /* 1140: ISCM.QueryServerProcessHandleHeld */
    0x17, 0x00, 0x44, 0x05, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0a, 0x01, 0x10, 0x00,
    0xea, 0x00, 0x50, 0x21, 0x18, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x20, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x28, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x15, 0x00, 0x30, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 1196: ISCM.RegisterRacActivationToken */
    0x1f, 0x00, 0x44, 0x05, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x18, 0x00, 0x0b, 0x5c, 0x50, 0x21, 0x20, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x28, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x16, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x27, 0x00, /* 1252: ISCM.RevokeRacActivationToken */
    0x0f, 0x00, 0x44, 0x04, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x0b, 0x5c, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x20, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x17, 0x00, 0x40, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2f, 0x00, /* 1302: ISCM.RegisterConsoleHandles */
    0x1f, 0x00, 0x44, 0x07, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x08, 0x5c, 0x48, 0x00, 0x18, 0x00, 0x08, 0x5c, 0x48, 0x00, 0x20, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x28, 0x00, 0x0b, 0x5c, 0x50, 0x21, 0x30, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x38, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x18, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x27, 0x00, /* 1370: ISCM.RevokeConsoleHandles */
    0x0f, 0x00, 0x44, 0x04, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00,
    0x0b, 0x5c, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x20, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x19, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 1420: ISCM.NotifyComClassChangesFromDeployment */
    0x0f, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1a, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 1464: ISCM.LogMachineClassesRootPermissions */
    0x0f, 0x00, 0x44, 0x03, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x50, 0x21, 0x10, 0x00,
    0x10, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 2: ISCM.NotifyStarted */
    0x0f, 0x00, 0x47, 0x07, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x0b, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x0b, 0x00, 0x10, 0x00, 0x42, 0x00,
    0x13, 0x20, 0x14, 0x00, 0x6c, 0x00, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x1c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 68: ISCM.NotifyStopped */
    0x07, 0x00, 0x42, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x88, 0x00,
    0x50, 0x21, 0x0c, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 110: ISCM.NotifyActivatableClassStarted */
    0x0f, 0x00, 0x47, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x08, 0x00, 0xac, 0x00,
    0x13, 0x20, 0x0c, 0x00, 0x09, 0x01, 0x50, 0x21, 0x10, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 164: ISCM.NotifyActivatableClassStopped */
    0x07, 0x00, 0x42, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x11, 0x01,
    0x50, 0x21, 0x0c, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 206: ISCM.GetThreadID */
    0x0f, 0x00, 0x40, 0x03, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x0c, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x0c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 248: ISCM.UpdateActivationSettings */
    0x07, 0x00, 0x40, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x10, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x24, 0x00, 0x32, 0x00, 0x00, 0x00, 0x57, 0x00, /* 284: ISCM.RegisterWindowPropInterface */
    0x17, 0x00, 0x46, 0x08, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0xb9, 0x5c,
    0x0a, 0x01, 0x0c, 0x00, 0x68, 0x01, 0x0b, 0x01, 0x10, 0x00, 0x74, 0x01,
    0x48, 0x00, 0x14, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x18, 0x00, 0xb9, 0x5c,
    0x50, 0x21, 0x1c, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x20, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x24, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2f, 0x00, /* 356: ISCM.GetWindowPropInterface */
    0x3f, 0x00, 0x45, 0x08, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0xb9, 0x5c,
    0x48, 0x00, 0x0c, 0x00, 0xb9, 0x5c, 0x48, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x12, 0xa1, 0x14, 0x00, 0x68, 0x01, 0x13, 0x01, 0x18, 0x00, 0x74, 0x01,
    0x50, 0x21, 0x1c, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x20, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x14, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 428: ISCM.EnableDisableDynamicIPTracking */
    0x0f, 0x00, 0x44, 0x04, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x0c, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x09, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 476: ISCM.GetCurrentAddrExclusionList */
    0x17, 0x00, 0x45, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x08, 0x5c,
    0x13, 0x20, 0x0c, 0x00, 0x21, 0x02, 0x50, 0x21, 0x10, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0a, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 530: ISCM.SetAddrExclusionList */
    0x0f, 0x00, 0x46, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x88, 0x00, 0x08, 0x00, 0x3e, 0x02,
    0x0b, 0x00, 0x0c, 0x00, 0x48, 0x02, 0x50, 0x21, 0x10, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0b, 0x00, 0x14, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 584: ISCM.FlushSCMBindings */
    0x0f, 0x00, 0x46, 0x04, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x61, 0x02,
    0x50, 0x21, 0x0c, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0c, 0x00, 0x14, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2b, 0x00, /* 632: ISCM.RetireServer */
    0x0f, 0x00, 0x44, 0x04, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0a, 0x01, 0x08, 0x00, 0x02, 0x02,
    0x50, 0x21, 0x0c, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0d, 0x00, 0x14, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 680: ISCM.NotifyDDStartOrStop */
    0x0f, 0x00, 0x44, 0x04, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x0c, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0e, 0x00, 0x38, 0x00, 0x32, 0x00, 0x00, 0x00, 0x4b, 0x00, /* 728: ISCM.IsObjectCreationAllowed */
    0x27, 0x00, 0x46, 0x0d, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0a, 0x01, 0x08, 0x00, 0x02, 0x02,
    0x48, 0x00, 0x0c, 0x00, 0x08, 0x5c, 0x0b, 0x00, 0x10, 0x00, 0x65, 0x02,
    0x48, 0x00, 0x14, 0x00, 0x08, 0x5c, 0x0b, 0x00, 0x18, 0x00, 0x7e, 0x02,
    0x48, 0x00, 0x1c, 0x00, 0x08, 0x5c, 0x48, 0x00, 0x20, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x24, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x28, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x2c, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x30, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x34, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x0f, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x27, 0x00, /* 830: ISCM.ControlTracingForProcess */
    0x0f, 0x00, 0x44, 0x05, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x48, 0x00, 0x0c, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x10, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x10, 0x00, 0x1c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x33, 0x00, /* 884: ISCM.QueryPIDForActivation */
    0x17, 0x00, 0x44, 0x06, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0a, 0x01, 0x08, 0x00, 0x02, 0x02,
    0x48, 0x00, 0x0c, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x10, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x14, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x11, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 944: ISCM.NotifyWinRTActivationStoreChanged */
    0x0f, 0x00, 0x44, 0x03, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x12, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2f, 0x00, /* 986: ISCM.DecodeProxy */
    0x2f, 0x00, 0x44, 0x08, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x48, 0x00, 0x0c, 0x00, 0x0b, 0x5c, 0x50, 0x21, 0x14, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x18, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x1c, 0x00, 0x0b, 0x5c,
    0x50, 0x21, 0x20, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x24, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x13, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 1058: ISCM.NotifyPsmResume */
    0x0f, 0x00, 0x44, 0x03, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x14, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2b, 0x00, /* 1100: ISCM.QueryServerProcessHandleHeld */
    0x17, 0x00, 0x44, 0x05, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0a, 0x01, 0x08, 0x00, 0x02, 0x02,
    0x50, 0x21, 0x0c, 0x00, 0x08, 0x5c, 0x50, 0x21, 0x10, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x15, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 1154: ISCM.RegisterRacActivationToken */
    0x1f, 0x00, 0x44, 0x05, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x0c, 0x00, 0x0b, 0x5c, 0x50, 0x21, 0x10, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x16, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x27, 0x00, /* 1208: ISCM.RevokeRacActivationToken */
    0x0f, 0x00, 0x44, 0x04, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x0b, 0x5c,
    0x50, 0x21, 0x10, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x17, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2f, 0x00, /* 1256: ISCM.RegisterConsoleHandles */
    0x1f, 0x00, 0x44, 0x07, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x48, 0x00, 0x0c, 0x00, 0x08, 0x5c, 0x48, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x14, 0x00, 0x0b, 0x5c, 0x50, 0x21, 0x18, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x1c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x18, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x27, 0x00, /* 1322: ISCM.RevokeConsoleHandles */
    0x0f, 0x00, 0x44, 0x04, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x48, 0x00, 0x08, 0x00, 0x0b, 0x5c,
    0x50, 0x21, 0x10, 0x00, 0x10, 0x5c, 0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x19, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 1370: ISCM.NotifyComClassChangesFromDeployment */
    0x0f, 0x00, 0x44, 0x03, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x1a, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 1412: ISCM.LogMachineClassesRootPermissions */
    0x0f, 0x00, 0x44, 0x03, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x50, 0x21, 0x08, 0x00, 0x10, 0x5c,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short ISCM_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 70, 114, 170, 214, 258, 296, 370, 444, 494, 550, 606, 656, 706, 756, 860, 916, 978, 1022, 1096, 1140, 1196, 1252, 1302, 1370, 1420, 1464
#else
    2, 68, 110, 164, 206, 248, 284, 356, 428, 476, 530, 584, 632, 680, 728, 830, 884, 944, 986, 1058, 1100, 1154, 1208, 1256, 1322, 1370, 1412
#endif
};

static const NDR_RUNDOWN ISCM_RundownRoutines[] = {
    (NDR_RUNDOWN)(void *)PHPROCESS_rundown
};

static const USER_MARSHAL_ROUTINE_QUADRUPLE ISCM_UserMarshalRoutines[] = {
    { (USER_MARSHAL_SIZING_ROUTINE)(void *)RPCSS_HSTRING_UserSize, (USER_MARSHAL_MARSHALLING_ROUTINE)(void *)RPCSS_HSTRING_UserMarshal,
      (USER_MARSHAL_UNMARSHALLING_ROUTINE)(void *)RPCSS_HSTRING_UserUnmarshal, (USER_MARSHAL_FREEING_ROUTINE)(void *)RPCSS_HSTRING_UserFree }
};

static RPC_CLIENT_INTERFACE ISCM_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x412f241e, 0xc12a, 0x11ce, {0xab, 0xff, 0x00, 0x20, 0xaf, 0x6e, 0x7a, 0x17}}, {0, 2}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE ISCM_c_ifspec = (RPC_IF_HANDLE)&ISCM_c_ifspec_data;

static MIDL_STUB_DESC ISCM_StubDesc = {
    .RpcInterfaceInformation = &ISCM_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .apfnNdrRundownRoutines = ISCM_RundownRoutines,
    .pFormatTypes = ISCM_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .aUserMarshalQuadruple = ISCM_UserMarshalRoutines,
    .mFlags = 1,
};

#ifndef ISCM_CLIENT_ONLY
static const SERVER_ROUTINE ISCM_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)ISCM_NotifyStarted_s,
    (SERVER_ROUTINE)(void *)ISCM_NotifyStopped_s,
    (SERVER_ROUTINE)(void *)ISCM_NotifyActivatableClassStarted_s,
    (SERVER_ROUTINE)(void *)ISCM_NotifyActivatableClassStopped_s,
    (SERVER_ROUTINE)(void *)ISCM_GetThreadID_s,
    (SERVER_ROUTINE)(void *)ISCM_UpdateActivationSettings_s,
    (SERVER_ROUTINE)(void *)ISCM_RegisterWindowPropInterface_s,
    (SERVER_ROUTINE)(void *)ISCM_GetWindowPropInterface_s,
    (SERVER_ROUTINE)(void *)ISCM_EnableDisableDynamicIPTracking_s,
    (SERVER_ROUTINE)(void *)ISCM_GetCurrentAddrExclusionList_s,
    (SERVER_ROUTINE)(void *)ISCM_SetAddrExclusionList_s,
    (SERVER_ROUTINE)(void *)ISCM_FlushSCMBindings_s,
    (SERVER_ROUTINE)(void *)ISCM_RetireServer_s,
    (SERVER_ROUTINE)(void *)ISCM_NotifyDDStartOrStop_s,
    (SERVER_ROUTINE)(void *)ISCM_IsObjectCreationAllowed_s,
    (SERVER_ROUTINE)(void *)ISCM_ControlTracingForProcess_s,
    (SERVER_ROUTINE)(void *)ISCM_QueryPIDForActivation_s,
    (SERVER_ROUTINE)(void *)ISCM_NotifyWinRTActivationStoreChanged_s,
    (SERVER_ROUTINE)(void *)ISCM_DecodeProxy_s,
    (SERVER_ROUTINE)(void *)ISCM_NotifyPsmResume_s,
    (SERVER_ROUTINE)(void *)ISCM_QueryServerProcessHandleHeld_s,
    (SERVER_ROUTINE)(void *)ISCM_RegisterRacActivationToken_s,
    (SERVER_ROUTINE)(void *)ISCM_RevokeRacActivationToken_s,
    (SERVER_ROUTINE)(void *)ISCM_RegisterConsoleHandles_s,
    (SERVER_ROUTINE)(void *)ISCM_RevokeConsoleHandles_s,
    (SERVER_ROUTINE)(void *)ISCM_NotifyComClassChangesFromDeployment_s,
    (SERVER_ROUTINE)(void *)ISCM_LogMachineClassesRootPermissions_s
};

int (*ISCM_hook)(PRPC_MESSAGE);

static void __RPC_STUB ISCM_dispatch_0(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_1(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_2(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_3(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_4(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_5(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_6(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_7(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_8(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_9(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_10(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_11(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_12(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_13(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_14(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_15(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_16(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_17(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_18(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_19(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_20(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_21(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_22(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_23(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_24(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_25(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCM_dispatch_26(PRPC_MESSAGE m)
{
    if (ISCM_hook && ISCM_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION ISCM_DispatchFunctions[] = {
ISCM_dispatch_0, ISCM_dispatch_1, ISCM_dispatch_2, ISCM_dispatch_3, ISCM_dispatch_4, ISCM_dispatch_5, ISCM_dispatch_6, ISCM_dispatch_7, ISCM_dispatch_8, ISCM_dispatch_9, ISCM_dispatch_10, ISCM_dispatch_11, ISCM_dispatch_12, ISCM_dispatch_13, ISCM_dispatch_14, ISCM_dispatch_15, ISCM_dispatch_16, ISCM_dispatch_17, ISCM_dispatch_18, ISCM_dispatch_19, ISCM_dispatch_20, ISCM_dispatch_21, ISCM_dispatch_22, ISCM_dispatch_23, ISCM_dispatch_24, ISCM_dispatch_25, ISCM_dispatch_26
};
static RPC_DISPATCH_TABLE ISCM_DispatchTable = { 27, ISCM_DispatchFunctions, 0 };
static MIDL_SERVER_INFO ISCM_ServerInfo = {
    .pStubDesc = &ISCM_StubDesc,
    .DispatchTable = ISCM_ServerRoutineTable,
    .ProcString = ISCM_ProcFormat,
    .FmtStringOffset = ISCM_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE ISCM_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x412f241e, 0xc12a, 0x11ce, {0xab, 0xff, 0x00, 0x20, 0xaf, 0x6e, 0x7a, 0x17}}, {0, 2}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &ISCM_DispatchTable, 0, 0, 0, &ISCM_ServerInfo, 0 };
RPC_IF_HANDLE ISCM_s_ifspec = (RPC_IF_HANDLE)&ISCM_s_ifspec_data;
#endif

static NDRGEN_UNUSED long WINAPI ISCM_NotifyStarted(handle_t hRpc, PHPROCESS phProcess, unsigned long cPackages, RPCSS_HSTRING *packages, REG_INPUT *pRegInput, REG_OUTPUT **ppRegOutput, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[0], hRpc, phProcess, cPackages, packages, pRegInput, ppRegOutput, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long cPackages;
        RPCSS_HSTRING *packages;
        REG_INPUT *pRegInput;
        REG_OUTPUT **ppRegOutput;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, cPackages, packages, pRegInput, ppRegOutput, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED void WINAPI ISCM_NotifyStopped(handle_t hRpc, PHPROCESS phProcess, REVOKE_INPUT *pRevInput, error_status_t *prpcstat)
{
#ifdef _WIN64
    NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[1], hRpc, phProcess, pRevInput, prpcstat);
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        REVOKE_INPUT *pRevInput;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, pRevInput, prpcstat };
#pragma pack(pop)
    NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[1], (unsigned char *)&frame);
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_NotifyActivatableClassStarted(handle_t hRpc, PHPROCESS phProcess, ACTIVATABLE_CLASS_REG_INPUT *pRegInput, REG_OUTPUT **ppRegOutput, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[2], hRpc, phProcess, pRegInput, ppRegOutput, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        ACTIVATABLE_CLASS_REG_INPUT *pRegInput;
        REG_OUTPUT **ppRegOutput;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, pRegInput, ppRegOutput, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[2], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED void WINAPI ISCM_NotifyActivatableClassStopped(handle_t hRpc, PHPROCESS phProcess, ACTIVATABLE_CLASS_STOP_INPUT *pRevInput, error_status_t *prpcstat)
{
#ifdef _WIN64
    NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[3], hRpc, phProcess, pRevInput, prpcstat);
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        ACTIVATABLE_CLASS_STOP_INPUT *pRevInput;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, pRevInput, prpcstat };
#pragma pack(pop)
    NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[3], (unsigned char *)&frame);
#endif
}

static NDRGEN_UNUSED void WINAPI ISCM_GetThreadID(handle_t hRpc, PHPROCESS phProcess, unsigned long *pThreadID, error_status_t *prpcstat)
{
#ifdef _WIN64
    NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[4], hRpc, phProcess, pThreadID, prpcstat);
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long *pThreadID;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, pThreadID, prpcstat };
#pragma pack(pop)
    NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[4], (unsigned char *)&frame);
#endif
}

static NDRGEN_UNUSED void WINAPI ISCM_UpdateActivationSettings(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
#ifdef _WIN64
    NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[5], hRpc, phProcess, prpcstat);
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, prpcstat };
#pragma pack(pop)
    NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[5], (unsigned char *)&frame);
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_RegisterWindowPropInterface(handle_t hRpc, PHPROCESS phProcess, UINT_PTR hWnd, STDOBJREF *pStd, OXID_INFO *pOxidInfo, unsigned long fFlags, UINT_PTR *pdwCookie, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[6], hRpc, phProcess, hWnd, pStd, pOxidInfo, fFlags, pdwCookie, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        UINT_PTR hWnd;
        STDOBJREF *pStd;
        OXID_INFO *pOxidInfo;
        unsigned long fFlags;
        UINT_PTR *pdwCookie;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, hWnd, pStd, pOxidInfo, fFlags, pdwCookie, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[6], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_GetWindowPropInterface(handle_t hRpc, PHPROCESS phProcess, UINT_PTR hWnd, UINT_PTR dwCookie, unsigned long fRevoke, STDOBJREF *pStd, OXID_INFO *pOxidInfo, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[7], hRpc, phProcess, hWnd, dwCookie, fRevoke, pStd, pOxidInfo, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        UINT_PTR hWnd;
        UINT_PTR dwCookie;
        unsigned long fRevoke;
        STDOBJREF *pStd;
        OXID_INFO *pOxidInfo;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, hWnd, dwCookie, fRevoke, pStd, pOxidInfo, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[7], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_EnableDisableDynamicIPTracking(handle_t hRpc, PHPROCESS phProcess, unsigned long fEnable, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[8], hRpc, phProcess, fEnable, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long fEnable;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, fEnable, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[8], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_GetCurrentAddrExclusionList(handle_t hRpc, PHPROCESS phProcess, unsigned long *pdwNumStrings, unsigned short ***pppszStrings, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[9], hRpc, phProcess, pdwNumStrings, pppszStrings, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long *pdwNumStrings;
        unsigned short ***pppszStrings;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, pdwNumStrings, pppszStrings, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[9], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_SetAddrExclusionList(handle_t hRpc, PHPROCESS phProcess, unsigned long dwNumStrings, unsigned short **ppszStrings, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[10], hRpc, phProcess, dwNumStrings, ppszStrings, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long dwNumStrings;
        unsigned short **ppszStrings;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, dwNumStrings, ppszStrings, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[10], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_FlushSCMBindings(handle_t hRpc, PHPROCESS phProcess, unsigned short *pszMachineName, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[11], hRpc, phProcess, pszMachineName, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned short *pszMachineName;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, pszMachineName, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[11], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_RetireServer(handle_t hRpc, PHPROCESS phProcess, GUID *pguidProcessIdentifier, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[12], hRpc, phProcess, pguidProcessIdentifier, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        GUID *pguidProcessIdentifier;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, pguidProcessIdentifier, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[12], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_NotifyDDStartOrStop(handle_t hRpc, PHPROCESS phProcess, unsigned long fStart, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[13], hRpc, phProcess, fStart, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long fStart;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, fStart, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[13], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_IsObjectCreationAllowed(handle_t hRpc, PHPROCESS phProcess, GUID *clsid, unsigned long cPackages, RPCSS_HSTRING *packages, unsigned long cMains, RPCSS_HSTRING *mains, unsigned long dwFlags, unsigned long dwContext, unsigned long *pfAllowed, unsigned long *pfRegistered, unsigned long *pdwProcessId, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[14], hRpc, phProcess, clsid, cPackages, packages, cMains, mains, dwFlags, dwContext, pfAllowed, pfRegistered, pdwProcessId, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        GUID *clsid;
        unsigned long cPackages;
        RPCSS_HSTRING *packages;
        unsigned long cMains;
        RPCSS_HSTRING *mains;
        unsigned long dwFlags;
        unsigned long dwContext;
        unsigned long *pfAllowed;
        unsigned long *pfRegistered;
        unsigned long *pdwProcessId;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, clsid, cPackages, packages, cMains, mains, dwFlags, dwContext, pfAllowed, pfRegistered, pdwProcessId, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[14], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_ControlTracingForProcess(handle_t hRpc, PHPROCESS phProcess, unsigned long dwPID, unsigned long fEnable, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[15], hRpc, phProcess, dwPID, fEnable, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long dwPID;
        unsigned long fEnable;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, dwPID, fEnable, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[15], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_QueryPIDForActivation(handle_t hRpc, PHPROCESS phProcess, GUID *clsid, unsigned long dwClsCtx, unsigned long *pdwPID, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[16], hRpc, phProcess, clsid, dwClsCtx, pdwPID, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        GUID *clsid;
        unsigned long dwClsCtx;
        unsigned long *pdwPID;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, clsid, dwClsCtx, pdwPID, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[16], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_NotifyWinRTActivationStoreChanged(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[17], hRpc, phProcess, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[17], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_DecodeProxy(handle_t hRpc, PHPROCESS phProcess, unsigned long dwClientPid, unsigned long long ui64ProxyAddress, unsigned long *pdwServerPid, unsigned long *pdwServerTid, unsigned long long *pui64ServerAddress, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[18], hRpc, phProcess, dwClientPid, ui64ProxyAddress, pdwServerPid, pdwServerTid, pui64ServerAddress, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long dwClientPid;
        unsigned long long ui64ProxyAddress;
        unsigned long *pdwServerPid;
        unsigned long *pdwServerTid;
        unsigned long long *pui64ServerAddress;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, dwClientPid, ui64ProxyAddress, pdwServerPid, pdwServerTid, pui64ServerAddress, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[18], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_NotifyPsmResume(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[19], hRpc, phProcess, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[19], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_QueryServerProcessHandleHeld(handle_t hRpc, PHPROCESS phProcess, GUID *pguidProcess, unsigned long *pfHeld, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[20], hRpc, phProcess, pguidProcess, pfHeld, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        GUID *pguidProcess;
        unsigned long *pfHeld;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, pguidProcess, pfHeld, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[20], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_RegisterRacActivationToken(handle_t hRpc, PHPROCESS phProcess, unsigned long hToken, unsigned long long *pId, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[21], hRpc, phProcess, hToken, pId, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long hToken;
        unsigned long long *pId;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, hToken, pId, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[21], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_RevokeRacActivationToken(handle_t hRpc, PHPROCESS phProcess, unsigned long long id, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[22], hRpc, phProcess, id, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long long id;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, id, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[22], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_RegisterConsoleHandles(handle_t hRpc, PHPROCESS phProcess, unsigned long hInput, unsigned long hOutput, unsigned long hError, unsigned long long *pId, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[23], hRpc, phProcess, hInput, hOutput, hError, pId, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long hInput;
        unsigned long hOutput;
        unsigned long hError;
        unsigned long long *pId;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, hInput, hOutput, hError, pId, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[23], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_RevokeConsoleHandles(handle_t hRpc, PHPROCESS phProcess, unsigned long long id, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[24], hRpc, phProcess, id, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        unsigned long long id;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, id, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[24], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_NotifyComClassChangesFromDeployment(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[25], hRpc, phProcess, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[25], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCM_LogMachineClassesRootPermissions(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[26], hRpc, phProcess, prpcstat).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        PHPROCESS phProcess;
        error_status_t *prpcstat;
    } frame = { hRpc, phProcess, prpcstat };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCM_StubDesc, ISCM_ProcFormat + ISCM_FormatStringOffsetTable[26], (unsigned char *)&frame).Simple;
#endif
}

/* ------------------------------------------------------------------ interface ISCMLocalActivator */

#ifndef ISCMLOCALACTIVATOR_CLIENT_ONLY
extern void WINAPI ISCMLocalActivator_QueryInterface_s(handle_t hRpc);
extern void WINAPI ISCMLocalActivator_AddRef_s(handle_t hRpc);
extern void WINAPI ISCMLocalActivator_Release_s(handle_t hRpc);
extern long WINAPI ISCMLocalActivator_GetClassObject_s(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis, ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties);
extern long WINAPI ISCMLocalActivator_CreateInstance_s(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis, ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *pUnkOuter, MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties);
#endif
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char ISCMLocalActivator_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x1a, 0x03, 0x28, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x4c, 0x00, 0x0f, 0x00, /* 2: struct ORPCTHIS (BOGUS) */
    0x08, 0x08, 0x4c, 0x00, 0x10, 0x00, 0x40, 0x36, 0x5b, 0x12, 0x00, 0x13,
    0x00,
    0x15, 0x01, 0x04, 0x00, 0x06, 0x06, 0x5b, /* 27: struct COMVERSION (STRUCT) */
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 34: struct GUID (STRUCT) */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 44: struct ORPC_EXTENT_ARRAY (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x10, 0x59, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 60: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x14, 0x00, 0x07, 0x00, 0x43, 0x43, 0x3e, 0x08, 0x5b, /* 81: struct ORPC_EXTENT (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x00, 0x59, 0x01, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 92: array */
    0x1a, 0x07, 0x68, 0x00, 0x00, 0x00, 0x13, 0x00, 0x08, 0x08, 0x4c, 0x00, /* 104: struct LOCALTHIS (BOGUS) */
    0xae, 0xff, 0x4c, 0x00, 0xaa, 0xff, 0x4c, 0x00, 0x0d, 0x00, 0x36, 0x36,
    0x5b, 0x12, 0x00, 0x13, 0x00, 0x12, 0x00, 0x2b, 0x00,
    0x15, 0x07, 0x30, 0x00, 0x43, 0x43, 0x3e, 0x0b, 0x43, 0x43, 0x3e, 0x0b, /* 137: struct ASYNC_REQUEST_BLOCK (STRUCT) */
    0x5b,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 150: struct TOUCHED_ASTA_ARRAY (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x1b, 0x07, 0x08, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x5b, /* 166: array */
    0x1a, 0x07, 0xe8, 0x00, 0x00, 0x00, 0x32, 0x00, 0x08, 0x08, 0x0b, 0x0b, /* 178: struct CONTAINERTHIS (BOGUS) */
    0x4c, 0x00, 0x62, 0xff, 0x4c, 0x00, 0x5e, 0xff, 0x4c, 0x00, 0x5a, 0xff,
    0x4c, 0x00, 0x56, 0xff, 0x4c, 0x00, 0x52, 0xff, 0x4c, 0x00, 0x4e, 0xff,
    0x4c, 0x00, 0x4a, 0xff, 0x4c, 0x00, 0x46, 0xff, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x08, 0x40, 0x36, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 238: struct CONTAINER_EXTENT_ARRAY (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 254: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x0c, 0x00, 0x06, 0x00, 0x08, 0x08, 0x08, 0x5b, /* 275: struct CONTAINER_EXTENT (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 285: array */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x40, 0x36, 0x5b, /* 297: struct ORPCTHAT (BOGUS) */
    0x12, 0x00, 0xf5, 0xfe,
    0x1a, 0x07, 0x28, 0x00, 0x00, 0x00, 0x09, 0x00, 0x0b, 0x08, 0x40, 0x36, /* 313: struct LOCALTHAT (BOGUS) */
    0x36, 0x36, 0x5b, 0x12, 0x00, 0x0a, 0x00, 0x12, 0x00, 0xc5, 0xff, 0x12,
    0x00, 0x67, 0x00,
    0x1a, 0x07, 0x30, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x08, 0x08, 0x0b, 0x4c, /* 340: struct ASYNC_RESPONSE_BLOCK (BOGUS) */
    0x00, 0xc1, 0xfe, 0x08, 0x40, 0x36, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x2a, 0x88, 0x18, 0x00, 0x02, 0x30, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, /* 363: union ASYNC_OUTCOME_DETAILS */
    0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0xff, 0xff,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 383: struct ASYNC_SUCCESS (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 399: array */
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x08, 0x08, /* 411: struct ASYNC_FAILURE (BOGUS) */
    0x36, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 429: array */
    0x1a, 0x07, 0xd8, 0x00, 0x00, 0x00, 0x2f, 0x00, 0x0b, 0x4c, 0x00, 0x5e, /* 441: struct CONTAINERTHAT (BOGUS) */
    0xfe, 0x4c, 0x00, 0x5a, 0xfe, 0x4c, 0x00, 0x56, 0xfe, 0x4c, 0x00, 0x52,
    0xfe, 0x4c, 0x00, 0x4e, 0xfe, 0x4c, 0x00, 0x4a, 0xfe, 0x4c, 0x00, 0x46,
    0xfe, 0x4c, 0x00, 0x42, 0xfe, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x08, 0x40, 0x36, 0x5b, 0x12, 0x00, 0xfe, 0xfe,
    0x12, 0x00, 0x02, 0x00, /* 498: pointer */
    0x17, 0x03, 0x04, 0x00, 0x04, 0x00, 0x08, 0x5b, /* 502: struct MInterfacePointer (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 510: array */
    0x11, 0x14, 0x02, 0x00, /* 522: pointer */
    0x12, 0x00, 0xe6, 0xff, /* 526: pointer */
    0x12, 0x00, 0xe2, 0xff, /* 530: pointer */
    0x12, 0x00, 0xde, 0xff, /* 534: pointer */
    0x11, 0x14, 0x02, 0x00, /* 538: pointer */
    0x12, 0x00, 0xd6, 0xff, /* 542: pointer */
#else
    0x00, 0x00, /* 0: pad */
    0x16, 0x03, 0x20, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x1c, 0x00, 0x1c, 0x00, /* 2: struct ORPCTHIS (PSTRUCT) */
    0x12, 0x00, 0x0b, 0x00, 0x5b, 0x40, 0x08, 0x08, 0x43, 0x43, 0x3e, 0x36,
    0x5b,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 27: struct ORPC_EXTENT_ARRAY (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x21, 0x03, 0x00, 0x00, 0x10, 0x59, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 48: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x14, 0x00, 0x07, 0x00, 0x43, 0x43, 0x3e, 0x08, 0x5b, /* 69: struct ORPC_EXTENT (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x00, 0x59, 0x01, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 80: array */
    0x16, 0x07, 0x60, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x58, 0x00, 0x58, 0x00, /* 92: struct LOCALTHIS (PSTRUCT) */
    0x12, 0x00, 0x1f, 0x00, 0x46, 0x5c, 0x5c, 0x00, 0x5c, 0x00, 0x12, 0x00,
    0x36, 0x00, 0x5b, 0x08, 0x08, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43,
    0x43, 0x43, 0x43, 0x43, 0x43, 0x42, 0x36, 0x36, 0x5b,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 137: struct TOUCHED_ASTA_ARRAY (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x1b, 0x07, 0x08, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x5b, /* 158: array */
    0x16, 0x07, 0xe0, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0xdc, 0x00, 0xdc, 0x00, /* 170: struct CONTAINERTHIS (PSTRUCT) */
    0x12, 0x00, 0x2a, 0x00, 0x5b, 0x08, 0x08, 0x0b, 0x0b, 0x43, 0x43, 0x3e,
    0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e,
    0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x08, 0x36, 0x5b,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 226: struct CONTAINER_EXTENT_ARRAY (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 247: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x0c, 0x00, 0x06, 0x00, 0x08, 0x08, 0x08, 0x5b, /* 268: struct CONTAINER_EXTENT (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 278: array */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 290: struct ORPCTHAT (PSTRUCT) */
    0x12, 0x00, 0xeb, 0xfe, 0x5b, 0x08, 0x36, 0x5b,
    0x16, 0x07, 0x18, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x0c, 0x00, 0x0c, 0x00, /* 310: struct LOCALTHAT (PSTRUCT) */
    0x12, 0x00, 0x1d, 0x00, 0x46, 0x5c, 0x10, 0x00, 0x10, 0x00, 0x12, 0x00,
    0xbe, 0xff, 0x46, 0x5c, 0x14, 0x00, 0x14, 0x00, 0x12, 0x00, 0x7b, 0x00,
    0x5b, 0x0b, 0x08, 0x36, 0x36, 0x36, 0x5b,
    0x16, 0x07, 0x28, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x24, 0x00, 0x24, 0x00, /* 353: struct ASYNC_RESPONSE_BLOCK (PSTRUCT) */
    0x12, 0x00, 0x0c, 0x00, 0x5b, 0x08, 0x08, 0x0b, 0x43, 0x43, 0x3e, 0x08,
    0x36, 0x5b,
    0x2a, 0x48, 0x14, 0x00, 0x02, 0x30, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, /* 379: union ASYNC_OUTCOME_DETAILS */
    0x01, 0x00, 0x00, 0x00, 0x25, 0x00, 0xff, 0xff,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 399: struct ASYNC_SUCCESS (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 420: array */
    0x16, 0x03, 0x14, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x10, 0x00, 0x10, 0x00, /* 432: struct ASYNC_FAILURE (PSTRUCT) */
    0x12, 0x00, 0x09, 0x00, 0x5b, 0x08, 0x08, 0x08, 0x08, 0x36, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 455: array */
    0x16, 0x07, 0xd0, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0xcc, 0x00, 0xcc, 0x00, /* 467: struct CONTAINERTHAT (PSTRUCT) */
    0x12, 0x00, 0x01, 0xff, 0x5b, 0x0b, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e,
    0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e,
    0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x08, 0x36, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 520: pointer */
    0x17, 0x03, 0x04, 0x00, 0x04, 0x00, 0x08, 0x5b, /* 524: struct MInterfacePointer (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 532: array */
    0x11, 0x14, 0x02, 0x00, /* 544: pointer */
    0x12, 0x00, 0xe6, 0xff, /* 548: pointer */
    0x12, 0x00, 0xe2, 0xff, /* 552: pointer */
    0x12, 0x00, 0xde, 0xff, /* 556: pointer */
    0x11, 0x14, 0x02, 0x00, /* 560: pointer */
    0x12, 0x00, 0xd6, 0xff, /* 564: pointer */
#endif
};

static const unsigned char ISCMLocalActivator_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2: ISCMLocalActivator.QueryInterface */
    0x00, 0x00, 0x40, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x40, 0x01, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 28: ISCMLocalActivator.AddRef */
    0x00, 0x00, 0x40, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x40, 0x02, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 54: ISCMLocalActivator.Release */
    0x00, 0x00, 0x40, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x40, 0x03, 0x00, 0x40, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 80: ISCMLocalActivator.GetClassObject */
    0x07, 0x00, 0x47, 0x07, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x10, 0x00,
    0x68, 0x00, 0x13, 0x41, 0x18, 0x00, 0x29, 0x01, 0x13, 0xa1, 0x20, 0x00,
    0x39, 0x01, 0x0b, 0x00, 0x28, 0x00, 0xf2, 0x01, 0x13, 0x20, 0x30, 0x00,
    0x0a, 0x02, 0x70, 0x00, 0x38, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x48, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 148: ISCMLocalActivator.CreateInstance */
    0x07, 0x00, 0x47, 0x08, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x10, 0x00,
    0x68, 0x00, 0x13, 0x41, 0x18, 0x00, 0x29, 0x01, 0x13, 0xa1, 0x20, 0x00,
    0x39, 0x01, 0x0b, 0x00, 0x28, 0x00, 0x12, 0x02, 0x0b, 0x00, 0x30, 0x00,
    0x16, 0x02, 0x13, 0x20, 0x38, 0x00, 0x1a, 0x02, 0x70, 0x00, 0x40, 0x00,
    0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x04, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2: ISCMLocalActivator.QueryInterface */
    0x00, 0x00, 0x40, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x01, 0x00, 0x04, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 26: ISCMLocalActivator.AddRef */
    0x00, 0x00, 0x40, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x02, 0x00, 0x04, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 50: ISCMLocalActivator.Release */
    0x00, 0x00, 0x40, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x03, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 74: ISCMLocalActivator.GetClassObject */
    0x07, 0x00, 0x47, 0x07, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x5c, 0x00,
    0x13, 0x21, 0x0c, 0x00, 0x22, 0x01, 0x13, 0x61, 0x10, 0x00, 0x36, 0x01,
    0x0b, 0x00, 0x14, 0x00, 0x08, 0x02, 0x13, 0x20, 0x18, 0x00, 0x20, 0x02,
    0x70, 0x00, 0x1c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x24, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 140: ISCMLocalActivator.CreateInstance */
    0x07, 0x00, 0x47, 0x08, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x5c, 0x00,
    0x13, 0x21, 0x0c, 0x00, 0x22, 0x01, 0x13, 0x61, 0x10, 0x00, 0x36, 0x01,
    0x0b, 0x00, 0x14, 0x00, 0x28, 0x02, 0x0b, 0x00, 0x18, 0x00, 0x2c, 0x02,
    0x13, 0x20, 0x1c, 0x00, 0x30, 0x02, 0x70, 0x00, 0x20, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short ISCMLocalActivator_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 28, 54, 80, 148
#else
    2, 26, 50, 74, 140
#endif
};

static void WINAPI ISCMLocalActivator_ExprEval_0(PMIDL_STUB_MESSAGE pStubMsg)
{
#ifdef _WIN64
    pStubMsg->Offset = 0;
    pStubMsg->MaxCount = (ULONG_PTR)(((((*(unsigned long *)(pStubMsg->StackTop + (0))) + 1) / 2) * 2));
#else
    pStubMsg->Offset = 0;
    pStubMsg->MaxCount = (ULONG_PTR)(((((*(unsigned long *)(pStubMsg->StackTop + (0))) + 1) / 2) * 2));
#endif
}

static void WINAPI ISCMLocalActivator_ExprEval_1(PMIDL_STUB_MESSAGE pStubMsg)
{
#ifdef _WIN64
    pStubMsg->Offset = 0;
    pStubMsg->MaxCount = (ULONG_PTR)(((((*(unsigned long *)(pStubMsg->StackTop + (-4))) + 7) / 8) * 8));
#else
    pStubMsg->Offset = 0;
    pStubMsg->MaxCount = (ULONG_PTR)(((((*(unsigned long *)(pStubMsg->StackTop + (-4))) + 7) / 8) * 8));
#endif
}

static const EXPR_EVAL ISCMLocalActivator_ExprEvalRoutines[] = {
    ISCMLocalActivator_ExprEval_0, ISCMLocalActivator_ExprEval_1
};

static RPC_CLIENT_INTERFACE ISCMLocalActivator_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x00000136, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}, {0, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE ISCMLocalActivator_c_ifspec = (RPC_IF_HANDLE)&ISCMLocalActivator_c_ifspec_data;

static MIDL_STUB_DESC ISCMLocalActivator_StubDesc = {
    .RpcInterfaceInformation = &ISCMLocalActivator_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .apfnExprEval = ISCMLocalActivator_ExprEvalRoutines,
    .pFormatTypes = ISCMLocalActivator_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef ISCMLOCALACTIVATOR_CLIENT_ONLY
static const SERVER_ROUTINE ISCMLocalActivator_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)ISCMLocalActivator_QueryInterface_s,
    (SERVER_ROUTINE)(void *)ISCMLocalActivator_AddRef_s,
    (SERVER_ROUTINE)(void *)ISCMLocalActivator_Release_s,
    (SERVER_ROUTINE)(void *)ISCMLocalActivator_GetClassObject_s,
    (SERVER_ROUTINE)(void *)ISCMLocalActivator_CreateInstance_s
};

int (*ISCMLocalActivator_hook)(PRPC_MESSAGE);

static void __RPC_STUB ISCMLocalActivator_dispatch_0(PRPC_MESSAGE m)
{
    if (ISCMLocalActivator_hook && ISCMLocalActivator_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCMLocalActivator_dispatch_1(PRPC_MESSAGE m)
{
    if (ISCMLocalActivator_hook && ISCMLocalActivator_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCMLocalActivator_dispatch_2(PRPC_MESSAGE m)
{
    if (ISCMLocalActivator_hook && ISCMLocalActivator_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCMLocalActivator_dispatch_3(PRPC_MESSAGE m)
{
    if (ISCMLocalActivator_hook && ISCMLocalActivator_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ISCMLocalActivator_dispatch_4(PRPC_MESSAGE m)
{
    if (ISCMLocalActivator_hook && ISCMLocalActivator_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION ISCMLocalActivator_DispatchFunctions[] = {
ISCMLocalActivator_dispatch_0, ISCMLocalActivator_dispatch_1, ISCMLocalActivator_dispatch_2, ISCMLocalActivator_dispatch_3, ISCMLocalActivator_dispatch_4
};
static RPC_DISPATCH_TABLE ISCMLocalActivator_DispatchTable = { 5, ISCMLocalActivator_DispatchFunctions, 0 };
static MIDL_SERVER_INFO ISCMLocalActivator_ServerInfo = {
    .pStubDesc = &ISCMLocalActivator_StubDesc,
    .DispatchTable = ISCMLocalActivator_ServerRoutineTable,
    .ProcString = ISCMLocalActivator_ProcFormat,
    .FmtStringOffset = ISCMLocalActivator_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE ISCMLocalActivator_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x00000136, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}, {0, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &ISCMLocalActivator_DispatchTable, 0, 0, 0, &ISCMLocalActivator_ServerInfo, 0 };
RPC_IF_HANDLE ISCMLocalActivator_s_ifspec = (RPC_IF_HANDLE)&ISCMLocalActivator_s_ifspec_data;
#endif

static NDRGEN_UNUSED void WINAPI ISCMLocalActivator_QueryInterface(handle_t hRpc)
{
#ifdef _WIN64
    NdrClientCall2(&ISCMLocalActivator_StubDesc, ISCMLocalActivator_ProcFormat + ISCMLocalActivator_FormatStringOffsetTable[0], hRpc);
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
    } frame = { hRpc };
#pragma pack(pop)
    NdrClientCall2(&ISCMLocalActivator_StubDesc, ISCMLocalActivator_ProcFormat + ISCMLocalActivator_FormatStringOffsetTable[0], (unsigned char *)&frame);
#endif
}

static NDRGEN_UNUSED void WINAPI ISCMLocalActivator_AddRef(handle_t hRpc)
{
#ifdef _WIN64
    NdrClientCall2(&ISCMLocalActivator_StubDesc, ISCMLocalActivator_ProcFormat + ISCMLocalActivator_FormatStringOffsetTable[1], hRpc);
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
    } frame = { hRpc };
#pragma pack(pop)
    NdrClientCall2(&ISCMLocalActivator_StubDesc, ISCMLocalActivator_ProcFormat + ISCMLocalActivator_FormatStringOffsetTable[1], (unsigned char *)&frame);
#endif
}

static NDRGEN_UNUSED void WINAPI ISCMLocalActivator_Release(handle_t hRpc)
{
#ifdef _WIN64
    NdrClientCall2(&ISCMLocalActivator_StubDesc, ISCMLocalActivator_ProcFormat + ISCMLocalActivator_FormatStringOffsetTable[2], hRpc);
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
    } frame = { hRpc };
#pragma pack(pop)
    NdrClientCall2(&ISCMLocalActivator_StubDesc, ISCMLocalActivator_ProcFormat + ISCMLocalActivator_FormatStringOffsetTable[2], (unsigned char *)&frame);
#endif
}

static NDRGEN_UNUSED long WINAPI ISCMLocalActivator_GetClassObject(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis, ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCMLocalActivator_StubDesc, ISCMLocalActivator_ProcFormat + ISCMLocalActivator_FormatStringOffsetTable[3], hRpc, orpcthis, localthis, orpcthat, localthat, pActProperties, ppActProperties).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        ORPCTHIS *orpcthis;
        LOCALTHIS *localthis;
        ORPCTHAT *orpcthat;
        LOCALTHAT *localthat;
        MInterfacePointer *pActProperties;
        MInterfacePointer **ppActProperties;
    } frame = { hRpc, orpcthis, localthis, orpcthat, localthat, pActProperties, ppActProperties };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCMLocalActivator_StubDesc, ISCMLocalActivator_ProcFormat + ISCMLocalActivator_FormatStringOffsetTable[3], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ISCMLocalActivator_CreateInstance(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis, ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *pUnkOuter, MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ISCMLocalActivator_StubDesc, ISCMLocalActivator_ProcFormat + ISCMLocalActivator_FormatStringOffsetTable[4], hRpc, orpcthis, localthis, orpcthat, localthat, pUnkOuter, pActProperties, ppActProperties).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        ORPCTHIS *orpcthis;
        LOCALTHIS *localthis;
        ORPCTHAT *orpcthat;
        LOCALTHAT *localthat;
        MInterfacePointer *pUnkOuter;
        MInterfacePointer *pActProperties;
        MInterfacePointer **ppActProperties;
    } frame = { hRpc, orpcthis, localthis, orpcthat, localthat, pUnkOuter, pActProperties, ppActProperties };
#pragma pack(pop)
    return (long)NdrClientCall2(&ISCMLocalActivator_StubDesc, ISCMLocalActivator_ProcFormat + ISCMLocalActivator_FormatStringOffsetTable[4], (unsigned char *)&frame).Simple;
#endif
}

/* ------------------------------------------------------------------ interface ILocalSystemActivator */

#ifndef ILOCALSYSTEMACTIVATOR_CLIENT_ONLY
extern void WINAPI ILocalSystemActivator_QueryInterface_s(handle_t hRpc);
extern void WINAPI ILocalSystemActivator_AddRef_s(handle_t hRpc);
extern void WINAPI ILocalSystemActivator_Release_s(handle_t hRpc);
extern long WINAPI ILocalSystemActivator_GetClassObject_s(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis, ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties);
extern long WINAPI ILocalSystemActivator_CreateInstance_s(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis, ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *pUnkOuter, MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties);
#endif
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char ILocalSystemActivator_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x1a, 0x03, 0x28, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x4c, 0x00, 0x0f, 0x00, /* 2: struct ORPCTHIS (BOGUS) */
    0x08, 0x08, 0x4c, 0x00, 0x10, 0x00, 0x40, 0x36, 0x5b, 0x12, 0x00, 0x13,
    0x00,
    0x15, 0x01, 0x04, 0x00, 0x06, 0x06, 0x5b, /* 27: struct COMVERSION (STRUCT) */
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 34: struct GUID (STRUCT) */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 44: struct ORPC_EXTENT_ARRAY (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x10, 0x59, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 60: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x14, 0x00, 0x07, 0x00, 0x43, 0x43, 0x3e, 0x08, 0x5b, /* 81: struct ORPC_EXTENT (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x00, 0x59, 0x01, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 92: array */
    0x1a, 0x07, 0x68, 0x00, 0x00, 0x00, 0x13, 0x00, 0x08, 0x08, 0x4c, 0x00, /* 104: struct LOCALTHIS (BOGUS) */
    0xae, 0xff, 0x4c, 0x00, 0xaa, 0xff, 0x4c, 0x00, 0x0d, 0x00, 0x36, 0x36,
    0x5b, 0x12, 0x00, 0x13, 0x00, 0x12, 0x00, 0x2b, 0x00,
    0x15, 0x07, 0x30, 0x00, 0x43, 0x43, 0x3e, 0x0b, 0x43, 0x43, 0x3e, 0x0b, /* 137: struct ASYNC_REQUEST_BLOCK (STRUCT) */
    0x5b,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 150: struct TOUCHED_ASTA_ARRAY (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x1b, 0x07, 0x08, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x5b, /* 166: array */
    0x1a, 0x07, 0xe8, 0x00, 0x00, 0x00, 0x32, 0x00, 0x08, 0x08, 0x0b, 0x0b, /* 178: struct CONTAINERTHIS (BOGUS) */
    0x4c, 0x00, 0x62, 0xff, 0x4c, 0x00, 0x5e, 0xff, 0x4c, 0x00, 0x5a, 0xff,
    0x4c, 0x00, 0x56, 0xff, 0x4c, 0x00, 0x52, 0xff, 0x4c, 0x00, 0x4e, 0xff,
    0x4c, 0x00, 0x4a, 0xff, 0x4c, 0x00, 0x46, 0xff, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x08, 0x40, 0x36, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 238: struct CONTAINER_EXTENT_ARRAY (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 254: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x0c, 0x00, 0x06, 0x00, 0x08, 0x08, 0x08, 0x5b, /* 275: struct CONTAINER_EXTENT (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 285: array */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x40, 0x36, 0x5b, /* 297: struct ORPCTHAT (BOGUS) */
    0x12, 0x00, 0xf5, 0xfe,
    0x1a, 0x07, 0x28, 0x00, 0x00, 0x00, 0x09, 0x00, 0x0b, 0x08, 0x40, 0x36, /* 313: struct LOCALTHAT (BOGUS) */
    0x36, 0x36, 0x5b, 0x12, 0x00, 0x0a, 0x00, 0x12, 0x00, 0xc5, 0xff, 0x12,
    0x00, 0x67, 0x00,
    0x1a, 0x07, 0x30, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x08, 0x08, 0x0b, 0x4c, /* 340: struct ASYNC_RESPONSE_BLOCK (BOGUS) */
    0x00, 0xc1, 0xfe, 0x08, 0x40, 0x36, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x2a, 0x88, 0x18, 0x00, 0x02, 0x30, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, /* 363: union ASYNC_OUTCOME_DETAILS */
    0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0xff, 0xff,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x08, 0x36, 0x5b, /* 383: struct ASYNC_SUCCESS (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 399: array */
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x08, 0x08, /* 411: struct ASYNC_FAILURE (BOGUS) */
    0x36, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 429: array */
    0x1a, 0x07, 0xd8, 0x00, 0x00, 0x00, 0x2f, 0x00, 0x0b, 0x4c, 0x00, 0x5e, /* 441: struct CONTAINERTHAT (BOGUS) */
    0xfe, 0x4c, 0x00, 0x5a, 0xfe, 0x4c, 0x00, 0x56, 0xfe, 0x4c, 0x00, 0x52,
    0xfe, 0x4c, 0x00, 0x4e, 0xfe, 0x4c, 0x00, 0x4a, 0xfe, 0x4c, 0x00, 0x46,
    0xfe, 0x4c, 0x00, 0x42, 0xfe, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x08, 0x40, 0x36, 0x5b, 0x12, 0x00, 0xfe, 0xfe,
    0x12, 0x00, 0x02, 0x00, /* 498: pointer */
    0x17, 0x03, 0x04, 0x00, 0x04, 0x00, 0x08, 0x5b, /* 502: struct MInterfacePointer (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 510: array */
    0x11, 0x14, 0x02, 0x00, /* 522: pointer */
    0x12, 0x00, 0xe6, 0xff, /* 526: pointer */
    0x12, 0x00, 0xe2, 0xff, /* 530: pointer */
    0x12, 0x00, 0xde, 0xff, /* 534: pointer */
    0x11, 0x14, 0x02, 0x00, /* 538: pointer */
    0x12, 0x00, 0xd6, 0xff, /* 542: pointer */
#else
    0x00, 0x00, /* 0: pad */
    0x16, 0x03, 0x20, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x1c, 0x00, 0x1c, 0x00, /* 2: struct ORPCTHIS (PSTRUCT) */
    0x12, 0x00, 0x0b, 0x00, 0x5b, 0x40, 0x08, 0x08, 0x43, 0x43, 0x3e, 0x36,
    0x5b,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 27: struct ORPC_EXTENT_ARRAY (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x21, 0x03, 0x00, 0x00, 0x10, 0x59, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, /* 48: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x14, 0x00, 0x07, 0x00, 0x43, 0x43, 0x3e, 0x08, 0x5b, /* 69: struct ORPC_EXTENT (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x00, 0x59, 0x01, 0x00, 0x00, 0x00, 0x01, 0x5b, /* 80: array */
    0x16, 0x07, 0x60, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x58, 0x00, 0x58, 0x00, /* 92: struct LOCALTHIS (PSTRUCT) */
    0x12, 0x00, 0x1f, 0x00, 0x46, 0x5c, 0x5c, 0x00, 0x5c, 0x00, 0x12, 0x00,
    0x36, 0x00, 0x5b, 0x08, 0x08, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43,
    0x43, 0x43, 0x43, 0x43, 0x43, 0x42, 0x36, 0x36, 0x5b,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 137: struct TOUCHED_ASTA_ARRAY (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x1b, 0x07, 0x08, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x5b, /* 158: array */
    0x16, 0x07, 0xe0, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0xdc, 0x00, 0xdc, 0x00, /* 170: struct CONTAINERTHIS (PSTRUCT) */
    0x12, 0x00, 0x2a, 0x00, 0x5b, 0x08, 0x08, 0x0b, 0x0b, 0x43, 0x43, 0x3e,
    0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e,
    0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x08, 0x36, 0x5b,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 226: struct CONTAINER_EXTENT_ARRAY (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 247: array */
    0xff, 0xff, 0x00, 0x00, 0x12, 0x00, 0x03, 0x00, 0x5b,
    0x17, 0x03, 0x0c, 0x00, 0x06, 0x00, 0x08, 0x08, 0x08, 0x5b, /* 268: struct CONTAINER_EXTENT (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 278: array */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 290: struct ORPCTHAT (PSTRUCT) */
    0x12, 0x00, 0xeb, 0xfe, 0x5b, 0x08, 0x36, 0x5b,
    0x16, 0x07, 0x18, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x0c, 0x00, 0x0c, 0x00, /* 310: struct LOCALTHAT (PSTRUCT) */
    0x12, 0x00, 0x1d, 0x00, 0x46, 0x5c, 0x10, 0x00, 0x10, 0x00, 0x12, 0x00,
    0xbe, 0xff, 0x46, 0x5c, 0x14, 0x00, 0x14, 0x00, 0x12, 0x00, 0x7b, 0x00,
    0x5b, 0x0b, 0x08, 0x36, 0x36, 0x36, 0x5b,
    0x16, 0x07, 0x28, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x24, 0x00, 0x24, 0x00, /* 353: struct ASYNC_RESPONSE_BLOCK (PSTRUCT) */
    0x12, 0x00, 0x0c, 0x00, 0x5b, 0x08, 0x08, 0x0b, 0x43, 0x43, 0x3e, 0x08,
    0x36, 0x5b,
    0x2a, 0x48, 0x14, 0x00, 0x02, 0x30, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, /* 379: union ASYNC_OUTCOME_DETAILS */
    0x01, 0x00, 0x00, 0x00, 0x25, 0x00, 0xff, 0xff,
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, /* 399: struct ASYNC_SUCCESS (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x08, 0x36, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 420: array */
    0x16, 0x03, 0x14, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x10, 0x00, 0x10, 0x00, /* 432: struct ASYNC_FAILURE (PSTRUCT) */
    0x12, 0x00, 0x09, 0x00, 0x5b, 0x08, 0x08, 0x08, 0x08, 0x36, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 455: array */
    0x16, 0x07, 0xd0, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0xcc, 0x00, 0xcc, 0x00, /* 467: struct CONTAINERTHAT (PSTRUCT) */
    0x12, 0x00, 0x01, 0xff, 0x5b, 0x0b, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e,
    0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e,
    0x43, 0x43, 0x3e, 0x43, 0x43, 0x3e, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x08, 0x36, 0x5b,
    0x12, 0x00, 0x02, 0x00, /* 520: pointer */
    0x17, 0x03, 0x04, 0x00, 0x04, 0x00, 0x08, 0x5b, /* 524: struct MInterfacePointer (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x08, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x01, 0x5b, /* 532: array */
    0x11, 0x14, 0x02, 0x00, /* 544: pointer */
    0x12, 0x00, 0xe6, 0xff, /* 548: pointer */
    0x12, 0x00, 0xe2, 0xff, /* 552: pointer */
    0x12, 0x00, 0xde, 0xff, /* 556: pointer */
    0x11, 0x14, 0x02, 0x00, /* 560: pointer */
    0x12, 0x00, 0xd6, 0xff, /* 564: pointer */
#endif
};

static const unsigned char ILocalSystemActivator_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2: ILocalSystemActivator.QueryInterface */
    0x00, 0x00, 0x40, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x40, 0x01, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 28: ILocalSystemActivator.AddRef */
    0x00, 0x00, 0x40, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x40, 0x02, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 54: ILocalSystemActivator.Release */
    0x00, 0x00, 0x40, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x40, 0x03, 0x00, 0x40, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 80: ILocalSystemActivator.GetClassObject */
    0x07, 0x00, 0x47, 0x07, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x10, 0x00,
    0x68, 0x00, 0x13, 0x41, 0x18, 0x00, 0x29, 0x01, 0x13, 0xa1, 0x20, 0x00,
    0x39, 0x01, 0x0b, 0x00, 0x28, 0x00, 0xf2, 0x01, 0x13, 0x20, 0x30, 0x00,
    0x0a, 0x02, 0x70, 0x00, 0x38, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x48, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 148: ILocalSystemActivator.CreateInstance */
    0x07, 0x00, 0x47, 0x08, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x10, 0x00,
    0x68, 0x00, 0x13, 0x41, 0x18, 0x00, 0x29, 0x01, 0x13, 0xa1, 0x20, 0x00,
    0x39, 0x01, 0x0b, 0x00, 0x28, 0x00, 0x12, 0x02, 0x0b, 0x00, 0x30, 0x00,
    0x16, 0x02, 0x13, 0x20, 0x38, 0x00, 0x1a, 0x02, 0x70, 0x00, 0x40, 0x00,
    0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x04, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2: ILocalSystemActivator.QueryInterface */
    0x00, 0x00, 0x40, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x01, 0x00, 0x04, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 26: ILocalSystemActivator.AddRef */
    0x00, 0x00, 0x40, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x02, 0x00, 0x04, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 50: ILocalSystemActivator.Release */
    0x00, 0x00, 0x40, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x03, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 74: ILocalSystemActivator.GetClassObject */
    0x07, 0x00, 0x47, 0x07, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x5c, 0x00,
    0x13, 0x21, 0x0c, 0x00, 0x22, 0x01, 0x13, 0x61, 0x10, 0x00, 0x36, 0x01,
    0x0b, 0x00, 0x14, 0x00, 0x08, 0x02, 0x13, 0x20, 0x18, 0x00, 0x20, 0x02,
    0x70, 0x00, 0x1c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x24, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 140: ILocalSystemActivator.CreateInstance */
    0x07, 0x00, 0x47, 0x08, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x5c, 0x00,
    0x13, 0x21, 0x0c, 0x00, 0x22, 0x01, 0x13, 0x61, 0x10, 0x00, 0x36, 0x01,
    0x0b, 0x00, 0x14, 0x00, 0x28, 0x02, 0x0b, 0x00, 0x18, 0x00, 0x2c, 0x02,
    0x13, 0x20, 0x1c, 0x00, 0x30, 0x02, 0x70, 0x00, 0x20, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short ILocalSystemActivator_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 28, 54, 80, 148
#else
    2, 26, 50, 74, 140
#endif
};

static void WINAPI ILocalSystemActivator_ExprEval_0(PMIDL_STUB_MESSAGE pStubMsg)
{
#ifdef _WIN64
    pStubMsg->Offset = 0;
    pStubMsg->MaxCount = (ULONG_PTR)(((((*(unsigned long *)(pStubMsg->StackTop + (0))) + 1) / 2) * 2));
#else
    pStubMsg->Offset = 0;
    pStubMsg->MaxCount = (ULONG_PTR)(((((*(unsigned long *)(pStubMsg->StackTop + (0))) + 1) / 2) * 2));
#endif
}

static void WINAPI ILocalSystemActivator_ExprEval_1(PMIDL_STUB_MESSAGE pStubMsg)
{
#ifdef _WIN64
    pStubMsg->Offset = 0;
    pStubMsg->MaxCount = (ULONG_PTR)(((((*(unsigned long *)(pStubMsg->StackTop + (-4))) + 7) / 8) * 8));
#else
    pStubMsg->Offset = 0;
    pStubMsg->MaxCount = (ULONG_PTR)(((((*(unsigned long *)(pStubMsg->StackTop + (-4))) + 7) / 8) * 8));
#endif
}

static const EXPR_EVAL ILocalSystemActivator_ExprEvalRoutines[] = {
    ILocalSystemActivator_ExprEval_0, ILocalSystemActivator_ExprEval_1
};

static RPC_CLIENT_INTERFACE ILocalSystemActivator_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x00000132, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}, {0, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE ILocalSystemActivator_c_ifspec = (RPC_IF_HANDLE)&ILocalSystemActivator_c_ifspec_data;

static MIDL_STUB_DESC ILocalSystemActivator_StubDesc = {
    .RpcInterfaceInformation = &ILocalSystemActivator_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .apfnExprEval = ILocalSystemActivator_ExprEvalRoutines,
    .pFormatTypes = ILocalSystemActivator_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef ILOCALSYSTEMACTIVATOR_CLIENT_ONLY
static const SERVER_ROUTINE ILocalSystemActivator_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)ILocalSystemActivator_QueryInterface_s,
    (SERVER_ROUTINE)(void *)ILocalSystemActivator_AddRef_s,
    (SERVER_ROUTINE)(void *)ILocalSystemActivator_Release_s,
    (SERVER_ROUTINE)(void *)ILocalSystemActivator_GetClassObject_s,
    (SERVER_ROUTINE)(void *)ILocalSystemActivator_CreateInstance_s
};

int (*ILocalSystemActivator_hook)(PRPC_MESSAGE);

static void __RPC_STUB ILocalSystemActivator_dispatch_0(PRPC_MESSAGE m)
{
    if (ILocalSystemActivator_hook && ILocalSystemActivator_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalSystemActivator_dispatch_1(PRPC_MESSAGE m)
{
    if (ILocalSystemActivator_hook && ILocalSystemActivator_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalSystemActivator_dispatch_2(PRPC_MESSAGE m)
{
    if (ILocalSystemActivator_hook && ILocalSystemActivator_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalSystemActivator_dispatch_3(PRPC_MESSAGE m)
{
    if (ILocalSystemActivator_hook && ILocalSystemActivator_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB ILocalSystemActivator_dispatch_4(PRPC_MESSAGE m)
{
    if (ILocalSystemActivator_hook && ILocalSystemActivator_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION ILocalSystemActivator_DispatchFunctions[] = {
ILocalSystemActivator_dispatch_0, ILocalSystemActivator_dispatch_1, ILocalSystemActivator_dispatch_2, ILocalSystemActivator_dispatch_3, ILocalSystemActivator_dispatch_4
};
static RPC_DISPATCH_TABLE ILocalSystemActivator_DispatchTable = { 5, ILocalSystemActivator_DispatchFunctions, 0 };
static MIDL_SERVER_INFO ILocalSystemActivator_ServerInfo = {
    .pStubDesc = &ILocalSystemActivator_StubDesc,
    .DispatchTable = ILocalSystemActivator_ServerRoutineTable,
    .ProcString = ILocalSystemActivator_ProcFormat,
    .FmtStringOffset = ILocalSystemActivator_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE ILocalSystemActivator_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x00000132, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}, {0, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &ILocalSystemActivator_DispatchTable, 0, 0, 0, &ILocalSystemActivator_ServerInfo, 0 };
RPC_IF_HANDLE ILocalSystemActivator_s_ifspec = (RPC_IF_HANDLE)&ILocalSystemActivator_s_ifspec_data;
#endif

static NDRGEN_UNUSED void WINAPI ILocalSystemActivator_QueryInterface(handle_t hRpc)
{
#ifdef _WIN64
    NdrClientCall2(&ILocalSystemActivator_StubDesc, ILocalSystemActivator_ProcFormat + ILocalSystemActivator_FormatStringOffsetTable[0], hRpc);
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
    } frame = { hRpc };
#pragma pack(pop)
    NdrClientCall2(&ILocalSystemActivator_StubDesc, ILocalSystemActivator_ProcFormat + ILocalSystemActivator_FormatStringOffsetTable[0], (unsigned char *)&frame);
#endif
}

static NDRGEN_UNUSED void WINAPI ILocalSystemActivator_AddRef(handle_t hRpc)
{
#ifdef _WIN64
    NdrClientCall2(&ILocalSystemActivator_StubDesc, ILocalSystemActivator_ProcFormat + ILocalSystemActivator_FormatStringOffsetTable[1], hRpc);
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
    } frame = { hRpc };
#pragma pack(pop)
    NdrClientCall2(&ILocalSystemActivator_StubDesc, ILocalSystemActivator_ProcFormat + ILocalSystemActivator_FormatStringOffsetTable[1], (unsigned char *)&frame);
#endif
}

static NDRGEN_UNUSED void WINAPI ILocalSystemActivator_Release(handle_t hRpc)
{
#ifdef _WIN64
    NdrClientCall2(&ILocalSystemActivator_StubDesc, ILocalSystemActivator_ProcFormat + ILocalSystemActivator_FormatStringOffsetTable[2], hRpc);
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
    } frame = { hRpc };
#pragma pack(pop)
    NdrClientCall2(&ILocalSystemActivator_StubDesc, ILocalSystemActivator_ProcFormat + ILocalSystemActivator_FormatStringOffsetTable[2], (unsigned char *)&frame);
#endif
}

static NDRGEN_UNUSED long WINAPI ILocalSystemActivator_GetClassObject(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis, ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ILocalSystemActivator_StubDesc, ILocalSystemActivator_ProcFormat + ILocalSystemActivator_FormatStringOffsetTable[3], hRpc, orpcthis, localthis, orpcthat, localthat, pActProperties, ppActProperties).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        ORPCTHIS *orpcthis;
        LOCALTHIS *localthis;
        ORPCTHAT *orpcthat;
        LOCALTHAT *localthat;
        MInterfacePointer *pActProperties;
        MInterfacePointer **ppActProperties;
    } frame = { hRpc, orpcthis, localthis, orpcthat, localthat, pActProperties, ppActProperties };
#pragma pack(pop)
    return (long)NdrClientCall2(&ILocalSystemActivator_StubDesc, ILocalSystemActivator_ProcFormat + ILocalSystemActivator_FormatStringOffsetTable[3], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI ILocalSystemActivator_CreateInstance(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis, ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *pUnkOuter, MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&ILocalSystemActivator_StubDesc, ILocalSystemActivator_ProcFormat + ILocalSystemActivator_FormatStringOffsetTable[4], hRpc, orpcthis, localthis, orpcthat, localthat, pUnkOuter, pActProperties, ppActProperties).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        ORPCTHIS *orpcthis;
        LOCALTHIS *localthis;
        ORPCTHAT *orpcthat;
        LOCALTHAT *localthat;
        MInterfacePointer *pUnkOuter;
        MInterfacePointer *pActProperties;
        MInterfacePointer **ppActProperties;
    } frame = { hRpc, orpcthis, localthis, orpcthat, localthat, pUnkOuter, pActProperties, ppActProperties };
#pragma pack(pop)
    return (long)NdrClientCall2(&ILocalSystemActivator_StubDesc, ILocalSystemActivator_ProcFormat + ILocalSystemActivator_FormatStringOffsetTable[4], (unsigned char *)&frame).Simple;
#endif
}

#endif
