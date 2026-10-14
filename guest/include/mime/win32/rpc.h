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

#endif
