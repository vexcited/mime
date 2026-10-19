/*
 * rpc runtime: uuids and rpc strings of rpcrt4.dll.
 */
#ifndef MIME_WIN32_RPC_H
#define MIME_WIN32_RPC_H

#include <mime/win32/winbase.h>

#ifndef RPCRTAPI
#define RPCRTAPI DECLSPEC_IMPORT
#endif

#define RPC_ENTRY WINAPI

typedef LONG RPC_STATUS;
typedef GUID UUID;
typedef unsigned char *RPC_CSTR;
typedef unsigned short *RPC_WSTR;

#define RPC_S_OK 0
#define RPC_S_INVALID_ARG ERROR_INVALID_PARAMETER
#define RPC_S_OUT_OF_MEMORY ERROR_OUTOFMEMORY
#define RPC_S_INVALID_STRING_UUID 1705
#define RPC_S_UUID_LOCAL_ONLY 1824
#define RPC_S_UUID_NO_ADDRESS 1739
#define RPC_S_INVALID_BINDING 1702
#define RPC_S_PROTSEQ_NOT_SUPPORTED 1703
#define RPC_S_INVALID_RPC_PROTSEQ 1704
#define RPC_S_INVALID_STRING_BINDING 1700
#define RPC_S_INVALID_ENDPOINT_FORMAT 1706
#define RPC_S_NO_CALL_ACTIVE 1725
#define RPC_S_BINDING_HAS_NO_AUTH 1714
#define RPC_S_UNKNOWN_AUTHN_SERVICE 1721
#define RPC_S_UNKNOWN_AUTHN_LEVEL 1708
#define RPC_S_NO_BINDINGS 1718

typedef void *RPC_BINDING_HANDLE;
typedef struct {
    ULONG Count;
    RPC_BINDING_HANDLE BindingH[1];
} RPC_BINDING_VECTOR;

RPCRTAPI RPC_STATUS RPC_ENTRY UuidCreate(UUID *uuid);
RPCRTAPI RPC_STATUS RPC_ENTRY UuidCreateSequential(UUID *uuid);
RPCRTAPI RPC_STATUS RPC_ENTRY UuidCreateNil(UUID *uuid);
RPCRTAPI RPC_STATUS RPC_ENTRY UuidToStringA(const UUID *uuid, RPC_CSTR *string);
RPCRTAPI RPC_STATUS RPC_ENTRY UuidToStringW(const UUID *uuid, RPC_WSTR *string);
RPCRTAPI RPC_STATUS RPC_ENTRY UuidFromStringA(RPC_CSTR string, UUID *uuid);
RPCRTAPI RPC_STATUS RPC_ENTRY UuidFromStringW(RPC_WSTR string, UUID *uuid);
RPCRTAPI signed int RPC_ENTRY UuidCompare(UUID *a, UUID *b, RPC_STATUS *status);
RPCRTAPI int RPC_ENTRY UuidEqual(UUID *a, UUID *b, RPC_STATUS *status);
RPCRTAPI int RPC_ENTRY UuidIsNil(UUID *uuid, RPC_STATUS *status);
RPCRTAPI unsigned short RPC_ENTRY UuidHash(UUID *uuid, RPC_STATUS *status);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringFreeA(RPC_CSTR *string);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringFreeW(RPC_WSTR *string);

RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringBindingComposeW(RPC_WSTR object, RPC_WSTR protseq, RPC_WSTR address,
                                                       RPC_WSTR endpoint, RPC_WSTR options, RPC_WSTR *binding);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringBindingComposeA(RPC_CSTR object, RPC_CSTR protseq, RPC_CSTR address,
                                                       RPC_CSTR endpoint, RPC_CSTR options, RPC_CSTR *binding);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringBindingParseW(RPC_WSTR binding, RPC_WSTR *object, RPC_WSTR *protseq,
                                                     RPC_WSTR *address, RPC_WSTR *endpoint, RPC_WSTR *options);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcStringBindingParseA(RPC_CSTR binding, RPC_CSTR *object, RPC_CSTR *protseq,
                                                     RPC_CSTR *address, RPC_CSTR *endpoint, RPC_CSTR *options);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingFromStringBindingW(RPC_WSTR string, RPC_BINDING_HANDLE *handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingFromStringBindingA(RPC_CSTR string, RPC_BINDING_HANDLE *handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingToStringBindingW(RPC_BINDING_HANDLE handle, RPC_WSTR *string);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingToStringBindingA(RPC_BINDING_HANDLE handle, RPC_CSTR *string);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingFree(RPC_BINDING_HANDLE *handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingCopy(RPC_BINDING_HANDLE source, RPC_BINDING_HANDLE *destination);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqObject(RPC_BINDING_HANDLE handle, UUID *object);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetObject(RPC_BINDING_HANDLE handle, UUID *object);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingVectorFree(RPC_BINDING_VECTOR **vector);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetOption(RPC_BINDING_HANDLE handle, ULONG option, ULONG_PTR value);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoW(RPC_BINDING_HANDLE handle, RPC_WSTR principal, ULONG level,
                                                     ULONG service, void *identity, ULONG authz);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoA(RPC_BINDING_HANDLE handle, RPC_CSTR principal, ULONG level,
                                                     ULONG service, void *identity, ULONG authz);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoExW(RPC_BINDING_HANDLE handle, RPC_WSTR principal, ULONG level,
                                                       ULONG service, void *identity, ULONG authz, void *qos);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthInfoExW(RPC_BINDING_HANDLE handle, RPC_WSTR *principal, ULONG *level,
                                                       ULONG *service, void **identity, ULONG *authz, ULONG qos_size,
                                                       void *qos);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcImpersonateClient(RPC_BINDING_HANDLE handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcRevertToSelfEx(RPC_BINDING_HANDLE handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcRevertToSelf(void);
RPCRTAPI int RPC_ENTRY I_RpcExceptionFilter(ULONG code);
RPCRTAPI int RPC_ENTRY RpcExceptionFilter(ULONG code);
RPCRTAPI DECLSPEC_NORETURN void RPC_ENTRY RpcRaiseException(RPC_STATUS status);
RPCRTAPI LONG RPC_ENTRY I_RpcMapWin32Status(RPC_STATUS status);
RPCRTAPI void *RPC_ENTRY NdrOleAllocate(SIZE_T size);
RPCRTAPI void RPC_ENTRY NdrOleFree(void *memory);

#endif
