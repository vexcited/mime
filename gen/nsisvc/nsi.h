#ifndef NDRGEN_NSI_H
#define NDRGEN_NSI_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))

typedef struct NSI_RPC_LUID NSI_RPC_LUID;
typedef union NSI_RPC_MODULEID_UNION NSI_RPC_MODULEID_UNION;
typedef struct NSI_RPC_MODULEID NSI_RPC_MODULEID;
typedef struct NSI_RPC_OBJECT NSI_RPC_OBJECT;
typedef struct NSI_RPC_KEY NSI_RPC_KEY;
typedef struct NSI_RPC_PARAMETER NSI_RPC_PARAMETER;
typedef struct NSI_RPC_ALL_PARAMETERS NSI_RPC_ALL_PARAMETERS;
typedef struct NSI_RPC_KEYS NSI_RPC_KEYS;
typedef struct NSI_RPC_ALL_OBJECTS NSI_RPC_ALL_OBJECTS;
typedef struct NSI_RPC_RW NSI_RPC_RW;
typedef struct NSI_RPC_COMPARTMENT NSI_RPC_COMPARTMENT;
typedef struct NSI_RPC_COMPARTMENT_ID NSI_RPC_COMPARTMENT_ID;

typedef enum NSI_RPC_MODULEID_TYPE { NsiRpcModuleGuid = 1, NsiRpcModuleLuid = 2 } NSI_RPC_MODULEID_TYPE;
struct NSI_RPC_LUID {
    unsigned long LowPart;
    unsigned long HighPart;
};
union NSI_RPC_MODULEID_UNION {
    GUID Guid;
    NSI_RPC_LUID IfLuid;
};
struct NSI_RPC_MODULEID {
    unsigned short Length;
    NSI_RPC_MODULEID_TYPE Type;
    NSI_RPC_MODULEID_UNION u;
};
struct NSI_RPC_OBJECT {
    unsigned long ModuleHandle;
    NSI_RPC_MODULEID *ModuleId;
    unsigned long Table;
};
typedef enum NSI_RPC_STORE { NsiRpcPersistent = 0, NsiRpcActive = 1, NsiRpcBoth = 2, NsiRpcCurrent = 3 } NSI_RPC_STORE;
typedef enum NSI_RPC_GET_ACTION { NsiRpcGetExact = 0, NsiRpcGetFirst = 1, NsiRpcGetNext = 2 } NSI_RPC_GET_ACTION;
struct NSI_RPC_KEY {
    byte *Data;
    unsigned long Length;
};
typedef enum NSI_RPC_STRUCT_TYPE { NsiRpcRw = 0, NsiRpcDynamic = 1, NsiRpcStatic = 2 } NSI_RPC_STRUCT_TYPE;
struct NSI_RPC_PARAMETER {
    NSI_RPC_STRUCT_TYPE Type;
    byte *Data;
    unsigned long Length;
    unsigned long Offset;
};
struct NSI_RPC_ALL_PARAMETERS {
    byte *Rw;
    unsigned long RwLength;
    byte *Dynamic;
    unsigned long DynamicLength;
    byte *Static;
    unsigned long StaticLength;
};
struct NSI_RPC_KEYS {
    byte *Data;
    unsigned long Size;
    unsigned long Total;
};
struct NSI_RPC_ALL_OBJECTS {
    byte *Rw;
    unsigned long RwSize;
    unsigned long RwTotal;
    byte *Dynamic;
    unsigned long DynamicSize;
    unsigned long DynamicTotal;
    byte *Static;
    unsigned long StaticSize;
    unsigned long StaticTotal;
};
typedef enum NSI_RPC_SET_ACTION { NsiRpcSetDefault = 0, NsiRpcSetCreateOnly = 1, NsiRpcSetCreateOrSet = 2, NsiRpcSetDelete = 3, NsiRpcSetReset = 4, NsiRpcSetClear = 5, NsiRpcSetCreateOrSetWithReference = 6, NsiRpcSetDeleteWithReference = 7 } NSI_RPC_SET_ACTION;
struct NSI_RPC_RW {
    byte *Data;
    unsigned long Length;
};
struct NSI_RPC_COMPARTMENT {
    unsigned long Scope;
    unsigned long Id;
};
typedef void *NOTIFICATION_HANDLE;
typedef enum NSI_RPC_NOTIFICATION { NsiRpcParameterNotification = 0, NsiRpcAddInstance = 1, NsiRpcDeleteInstance = 2, NsiRpcInitialNotification = 3 } NSI_RPC_NOTIFICATION;
struct NSI_RPC_COMPARTMENT_ID {
    unsigned long Id;
};

#ifdef _WIN64
_Static_assert(sizeof(NSI_RPC_MODULEID_TYPE) == 4, "size of NSI_RPC_MODULEID_TYPE");
_Static_assert(sizeof(GUID) == 16, "size of GUID");
_Static_assert(__builtin_offsetof(GUID, Data1) == 0, "GUID.Data1");
_Static_assert(__builtin_offsetof(GUID, Data2) == 4, "GUID.Data2");
_Static_assert(__builtin_offsetof(GUID, Data3) == 6, "GUID.Data3");
_Static_assert(__builtin_offsetof(GUID, Data4) == 8, "GUID.Data4");
_Static_assert(sizeof(NSI_RPC_LUID) == 8, "size of NSI_RPC_LUID");
_Static_assert(__builtin_offsetof(NSI_RPC_LUID, LowPart) == 0, "NSI_RPC_LUID.LowPart");
_Static_assert(__builtin_offsetof(NSI_RPC_LUID, HighPart) == 4, "NSI_RPC_LUID.HighPart");
_Static_assert(sizeof(NSI_RPC_MODULEID_UNION) == 16, "size of NSI_RPC_MODULEID_UNION");
_Static_assert(sizeof(NSI_RPC_MODULEID) == 24, "size of NSI_RPC_MODULEID");
_Static_assert(__builtin_offsetof(NSI_RPC_MODULEID, Length) == 0, "NSI_RPC_MODULEID.Length");
_Static_assert(__builtin_offsetof(NSI_RPC_MODULEID, Type) == 4, "NSI_RPC_MODULEID.Type");
_Static_assert(__builtin_offsetof(NSI_RPC_MODULEID, u) == 8, "NSI_RPC_MODULEID.u");
_Static_assert(sizeof(NSI_RPC_OBJECT) == 24, "size of NSI_RPC_OBJECT");
_Static_assert(__builtin_offsetof(NSI_RPC_OBJECT, ModuleHandle) == 0, "NSI_RPC_OBJECT.ModuleHandle");
_Static_assert(__builtin_offsetof(NSI_RPC_OBJECT, ModuleId) == 8, "NSI_RPC_OBJECT.ModuleId");
_Static_assert(__builtin_offsetof(NSI_RPC_OBJECT, Table) == 16, "NSI_RPC_OBJECT.Table");
_Static_assert(sizeof(NSI_RPC_STORE) == 4, "size of NSI_RPC_STORE");
_Static_assert(sizeof(NSI_RPC_GET_ACTION) == 4, "size of NSI_RPC_GET_ACTION");
_Static_assert(sizeof(NSI_RPC_KEY) == 16, "size of NSI_RPC_KEY");
_Static_assert(__builtin_offsetof(NSI_RPC_KEY, Data) == 0, "NSI_RPC_KEY.Data");
_Static_assert(__builtin_offsetof(NSI_RPC_KEY, Length) == 8, "NSI_RPC_KEY.Length");
_Static_assert(sizeof(NSI_RPC_STRUCT_TYPE) == 4, "size of NSI_RPC_STRUCT_TYPE");
_Static_assert(sizeof(NSI_RPC_PARAMETER) == 24, "size of NSI_RPC_PARAMETER");
_Static_assert(__builtin_offsetof(NSI_RPC_PARAMETER, Type) == 0, "NSI_RPC_PARAMETER.Type");
_Static_assert(__builtin_offsetof(NSI_RPC_PARAMETER, Data) == 8, "NSI_RPC_PARAMETER.Data");
_Static_assert(__builtin_offsetof(NSI_RPC_PARAMETER, Length) == 16, "NSI_RPC_PARAMETER.Length");
_Static_assert(__builtin_offsetof(NSI_RPC_PARAMETER, Offset) == 20, "NSI_RPC_PARAMETER.Offset");
_Static_assert(sizeof(NSI_RPC_ALL_PARAMETERS) == 48, "size of NSI_RPC_ALL_PARAMETERS");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, Rw) == 0, "NSI_RPC_ALL_PARAMETERS.Rw");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, RwLength) == 8, "NSI_RPC_ALL_PARAMETERS.RwLength");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, Dynamic) == 16, "NSI_RPC_ALL_PARAMETERS.Dynamic");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, DynamicLength) == 24, "NSI_RPC_ALL_PARAMETERS.DynamicLength");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, Static) == 32, "NSI_RPC_ALL_PARAMETERS.Static");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, StaticLength) == 40, "NSI_RPC_ALL_PARAMETERS.StaticLength");
_Static_assert(sizeof(NSI_RPC_KEYS) == 16, "size of NSI_RPC_KEYS");
_Static_assert(__builtin_offsetof(NSI_RPC_KEYS, Data) == 0, "NSI_RPC_KEYS.Data");
_Static_assert(__builtin_offsetof(NSI_RPC_KEYS, Size) == 8, "NSI_RPC_KEYS.Size");
_Static_assert(__builtin_offsetof(NSI_RPC_KEYS, Total) == 12, "NSI_RPC_KEYS.Total");
_Static_assert(sizeof(NSI_RPC_ALL_OBJECTS) == 48, "size of NSI_RPC_ALL_OBJECTS");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, Rw) == 0, "NSI_RPC_ALL_OBJECTS.Rw");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, RwSize) == 8, "NSI_RPC_ALL_OBJECTS.RwSize");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, RwTotal) == 12, "NSI_RPC_ALL_OBJECTS.RwTotal");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, Dynamic) == 16, "NSI_RPC_ALL_OBJECTS.Dynamic");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, DynamicSize) == 24, "NSI_RPC_ALL_OBJECTS.DynamicSize");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, DynamicTotal) == 28, "NSI_RPC_ALL_OBJECTS.DynamicTotal");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, Static) == 32, "NSI_RPC_ALL_OBJECTS.Static");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, StaticSize) == 40, "NSI_RPC_ALL_OBJECTS.StaticSize");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, StaticTotal) == 44, "NSI_RPC_ALL_OBJECTS.StaticTotal");
_Static_assert(sizeof(NSI_RPC_SET_ACTION) == 4, "size of NSI_RPC_SET_ACTION");
_Static_assert(sizeof(NSI_RPC_RW) == 16, "size of NSI_RPC_RW");
_Static_assert(__builtin_offsetof(NSI_RPC_RW, Data) == 0, "NSI_RPC_RW.Data");
_Static_assert(__builtin_offsetof(NSI_RPC_RW, Length) == 8, "NSI_RPC_RW.Length");
_Static_assert(sizeof(NSI_RPC_COMPARTMENT) == 8, "size of NSI_RPC_COMPARTMENT");
_Static_assert(__builtin_offsetof(NSI_RPC_COMPARTMENT, Scope) == 0, "NSI_RPC_COMPARTMENT.Scope");
_Static_assert(__builtin_offsetof(NSI_RPC_COMPARTMENT, Id) == 4, "NSI_RPC_COMPARTMENT.Id");
_Static_assert(sizeof(NOTIFICATION_HANDLE) == 8, "size of NOTIFICATION_HANDLE");
_Static_assert(sizeof(NSI_RPC_NOTIFICATION) == 4, "size of NSI_RPC_NOTIFICATION");
_Static_assert(sizeof(NSI_RPC_COMPARTMENT_ID) == 4, "size of NSI_RPC_COMPARTMENT_ID");
_Static_assert(__builtin_offsetof(NSI_RPC_COMPARTMENT_ID, Id) == 0, "NSI_RPC_COMPARTMENT_ID.Id");
#else
_Static_assert(sizeof(NSI_RPC_MODULEID_TYPE) == 4, "size of NSI_RPC_MODULEID_TYPE");
_Static_assert(sizeof(GUID) == 16, "size of GUID");
_Static_assert(__builtin_offsetof(GUID, Data1) == 0, "GUID.Data1");
_Static_assert(__builtin_offsetof(GUID, Data2) == 4, "GUID.Data2");
_Static_assert(__builtin_offsetof(GUID, Data3) == 6, "GUID.Data3");
_Static_assert(__builtin_offsetof(GUID, Data4) == 8, "GUID.Data4");
_Static_assert(sizeof(NSI_RPC_LUID) == 8, "size of NSI_RPC_LUID");
_Static_assert(__builtin_offsetof(NSI_RPC_LUID, LowPart) == 0, "NSI_RPC_LUID.LowPart");
_Static_assert(__builtin_offsetof(NSI_RPC_LUID, HighPart) == 4, "NSI_RPC_LUID.HighPart");
_Static_assert(sizeof(NSI_RPC_MODULEID_UNION) == 16, "size of NSI_RPC_MODULEID_UNION");
_Static_assert(sizeof(NSI_RPC_MODULEID) == 24, "size of NSI_RPC_MODULEID");
_Static_assert(__builtin_offsetof(NSI_RPC_MODULEID, Length) == 0, "NSI_RPC_MODULEID.Length");
_Static_assert(__builtin_offsetof(NSI_RPC_MODULEID, Type) == 4, "NSI_RPC_MODULEID.Type");
_Static_assert(__builtin_offsetof(NSI_RPC_MODULEID, u) == 8, "NSI_RPC_MODULEID.u");
_Static_assert(sizeof(NSI_RPC_OBJECT) == 12, "size of NSI_RPC_OBJECT");
_Static_assert(__builtin_offsetof(NSI_RPC_OBJECT, ModuleHandle) == 0, "NSI_RPC_OBJECT.ModuleHandle");
_Static_assert(__builtin_offsetof(NSI_RPC_OBJECT, ModuleId) == 4, "NSI_RPC_OBJECT.ModuleId");
_Static_assert(__builtin_offsetof(NSI_RPC_OBJECT, Table) == 8, "NSI_RPC_OBJECT.Table");
_Static_assert(sizeof(NSI_RPC_STORE) == 4, "size of NSI_RPC_STORE");
_Static_assert(sizeof(NSI_RPC_GET_ACTION) == 4, "size of NSI_RPC_GET_ACTION");
_Static_assert(sizeof(NSI_RPC_KEY) == 8, "size of NSI_RPC_KEY");
_Static_assert(__builtin_offsetof(NSI_RPC_KEY, Data) == 0, "NSI_RPC_KEY.Data");
_Static_assert(__builtin_offsetof(NSI_RPC_KEY, Length) == 4, "NSI_RPC_KEY.Length");
_Static_assert(sizeof(NSI_RPC_STRUCT_TYPE) == 4, "size of NSI_RPC_STRUCT_TYPE");
_Static_assert(sizeof(NSI_RPC_PARAMETER) == 16, "size of NSI_RPC_PARAMETER");
_Static_assert(__builtin_offsetof(NSI_RPC_PARAMETER, Type) == 0, "NSI_RPC_PARAMETER.Type");
_Static_assert(__builtin_offsetof(NSI_RPC_PARAMETER, Data) == 4, "NSI_RPC_PARAMETER.Data");
_Static_assert(__builtin_offsetof(NSI_RPC_PARAMETER, Length) == 8, "NSI_RPC_PARAMETER.Length");
_Static_assert(__builtin_offsetof(NSI_RPC_PARAMETER, Offset) == 12, "NSI_RPC_PARAMETER.Offset");
_Static_assert(sizeof(NSI_RPC_ALL_PARAMETERS) == 24, "size of NSI_RPC_ALL_PARAMETERS");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, Rw) == 0, "NSI_RPC_ALL_PARAMETERS.Rw");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, RwLength) == 4, "NSI_RPC_ALL_PARAMETERS.RwLength");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, Dynamic) == 8, "NSI_RPC_ALL_PARAMETERS.Dynamic");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, DynamicLength) == 12, "NSI_RPC_ALL_PARAMETERS.DynamicLength");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, Static) == 16, "NSI_RPC_ALL_PARAMETERS.Static");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_PARAMETERS, StaticLength) == 20, "NSI_RPC_ALL_PARAMETERS.StaticLength");
_Static_assert(sizeof(NSI_RPC_KEYS) == 12, "size of NSI_RPC_KEYS");
_Static_assert(__builtin_offsetof(NSI_RPC_KEYS, Data) == 0, "NSI_RPC_KEYS.Data");
_Static_assert(__builtin_offsetof(NSI_RPC_KEYS, Size) == 4, "NSI_RPC_KEYS.Size");
_Static_assert(__builtin_offsetof(NSI_RPC_KEYS, Total) == 8, "NSI_RPC_KEYS.Total");
_Static_assert(sizeof(NSI_RPC_ALL_OBJECTS) == 36, "size of NSI_RPC_ALL_OBJECTS");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, Rw) == 0, "NSI_RPC_ALL_OBJECTS.Rw");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, RwSize) == 4, "NSI_RPC_ALL_OBJECTS.RwSize");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, RwTotal) == 8, "NSI_RPC_ALL_OBJECTS.RwTotal");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, Dynamic) == 12, "NSI_RPC_ALL_OBJECTS.Dynamic");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, DynamicSize) == 16, "NSI_RPC_ALL_OBJECTS.DynamicSize");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, DynamicTotal) == 20, "NSI_RPC_ALL_OBJECTS.DynamicTotal");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, Static) == 24, "NSI_RPC_ALL_OBJECTS.Static");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, StaticSize) == 28, "NSI_RPC_ALL_OBJECTS.StaticSize");
_Static_assert(__builtin_offsetof(NSI_RPC_ALL_OBJECTS, StaticTotal) == 32, "NSI_RPC_ALL_OBJECTS.StaticTotal");
_Static_assert(sizeof(NSI_RPC_SET_ACTION) == 4, "size of NSI_RPC_SET_ACTION");
_Static_assert(sizeof(NSI_RPC_RW) == 8, "size of NSI_RPC_RW");
_Static_assert(__builtin_offsetof(NSI_RPC_RW, Data) == 0, "NSI_RPC_RW.Data");
_Static_assert(__builtin_offsetof(NSI_RPC_RW, Length) == 4, "NSI_RPC_RW.Length");
_Static_assert(sizeof(NSI_RPC_COMPARTMENT) == 8, "size of NSI_RPC_COMPARTMENT");
_Static_assert(__builtin_offsetof(NSI_RPC_COMPARTMENT, Scope) == 0, "NSI_RPC_COMPARTMENT.Scope");
_Static_assert(__builtin_offsetof(NSI_RPC_COMPARTMENT, Id) == 4, "NSI_RPC_COMPARTMENT.Id");
_Static_assert(sizeof(NOTIFICATION_HANDLE) == 4, "size of NOTIFICATION_HANDLE");
_Static_assert(sizeof(NSI_RPC_NOTIFICATION) == 4, "size of NSI_RPC_NOTIFICATION");
_Static_assert(sizeof(NSI_RPC_COMPARTMENT_ID) == 4, "size of NSI_RPC_COMPARTMENT_ID");
_Static_assert(__builtin_offsetof(NSI_RPC_COMPARTMENT_ID, Id) == 0, "NSI_RPC_COMPARTMENT_ID.Id");
#endif

/* ------------------------------------------------------------------ interface nsirpc */

#ifndef NSIRPC_CLIENT_ONLY
extern unsigned long WINAPI nsirpc_GetParameter_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store, NSI_RPC_GET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_PARAMETER *parameter);
extern unsigned long WINAPI nsirpc_GetAllParameters_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store, NSI_RPC_GET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_ALL_PARAMETERS *parameters);
extern unsigned long WINAPI nsirpc_EnumerateObjectsAllParameters_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store, NSI_RPC_GET_ACTION action, NSI_RPC_KEYS *keys, NSI_RPC_ALL_OBJECTS *objects, unsigned long *count);
extern unsigned long WINAPI nsirpc_SetParameter_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store, NSI_RPC_SET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_PARAMETER *parameter);
extern unsigned long WINAPI nsirpc_SetAllParameters_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store, NSI_RPC_SET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_RW *parameters);
extern unsigned long WINAPI nsirpc_RegisterChangeNotification_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_COMPARTMENT compartment, NOTIFICATION_HANDLE *handle);
extern unsigned long WINAPI nsirpc_DeregisterChangeNotification_s(handle_t h, NSI_RPC_OBJECT *object, NOTIFICATION_HANDLE *handle);
extern void WINAPI nsirpc_RequestChangeNotification_s(PRPC_ASYNC_STATE async, handle_t h, NOTIFICATION_HANDLE handle, NSI_RPC_KEY *key, NSI_RPC_PARAMETER *parameter, NSI_RPC_NOTIFICATION *type);
extern unsigned long WINAPI nsirpc_ParameterChange_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_KEY *key, NSI_RPC_PARAMETER *parameter, NSI_RPC_COMPARTMENT_ID compartment, NSI_RPC_NOTIFICATION type);
#endif
void WINAPI NOTIFICATION_HANDLE_rundown(void *);
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char nsirpc_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x40, 0x36, 0x08, /* 2: struct NSI_RPC_OBJECT (BOGUS) */
    0x40, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x3e, 0x0d, 0x4c, /* 20: struct NSI_RPC_MODULEID (BOGUS) */
    0x00, 0x03, 0x00, 0x5b,
    0x2b, 0x0d, 0x0d, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x02, 0x00, 0x10, 0x00, /* 36: union NSI_RPC_MODULEID_UNION */
    0x02, 0x30, 0x01, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0xff, 0xff,
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 64: struct GUID (STRUCT) */
    0x15, 0x03, 0x08, 0x00, 0x08, 0x08, 0x5b, /* 74: struct NSI_RPC_LUID (STRUCT) */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, /* 81: range */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, /* 91: range */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x36, 0x08, 0x40, 0x5b, /* 101: struct NSI_RPC_KEY (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 117: array */
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0d, 0x40, 0x36, 0x08, /* 129: struct NSI_RPC_PARAMETER (BOGUS) */
    0x08, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x10, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 147: array */
    0x1a, 0x03, 0x30, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x36, 0x08, 0x40, 0x36, /* 159: struct NSI_RPC_ALL_PARAMETERS (BOGUS) */
    0x08, 0x40, 0x36, 0x08, 0x40, 0x5b, 0x12, 0x00, 0x0a, 0x00, 0x12, 0x00,
    0x12, 0x00, 0x12, 0x00, 0x1a, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 189: array */
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x18, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 201: array */
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x28, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 213: array */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, /* 225: range */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, /* 235: range */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x36, 0x08, 0x08, 0x5b, /* 245: struct NSI_RPC_KEYS (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 261: array */
    0x1a, 0x03, 0x30, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x36, 0x08, 0x08, 0x36, /* 273: struct NSI_RPC_ALL_OBJECTS (BOGUS) */
    0x08, 0x08, 0x36, 0x08, 0x08, 0x5b, 0x12, 0x00, 0x0a, 0x00, 0x12, 0x00,
    0x12, 0x00, 0x12, 0x00, 0x1a, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 303: array */
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x1c, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 315: array */
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x2c, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 327: array */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, /* 339: range */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, /* 349: range */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x36, 0x08, 0x40, 0x5b, /* 359: struct NSI_RPC_RW (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 375: array */
    0x15, 0x03, 0x08, 0x00, 0x08, 0x08, 0x5b, /* 387: struct NSI_RPC_COMPARTMENT (STRUCT) */
    0x30, 0xa4, 0x00, 0x00, /* 394: context handle NOTIFICATION_HANDLE */
    0x30, 0xe4, 0x00, 0x00, /* 398: context handle NOTIFICATION_HANDLE */
    0x30, 0x44, 0x00, 0x00, /* 402: context handle NOTIFICATION_HANDLE */
    0x15, 0x03, 0x04, 0x00, 0x08, 0x5b, /* 406: struct NSI_RPC_COMPARTMENT_ID (STRUCT) */
#else
    0x00, 0x00, /* 0: pad */
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 2: struct NSI_RPC_OBJECT (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x08, 0x36, 0x08, 0x5b,
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x3e, 0x0d, 0x4c, /* 23: struct NSI_RPC_MODULEID (BOGUS) */
    0x00, 0x03, 0x00, 0x5b,
    0x2b, 0x0d, 0x0d, 0x00, 0xfc, 0xff, 0x01, 0x00, 0x02, 0x00, 0x10, 0x00, /* 39: union NSI_RPC_MODULEID_UNION */
    0x02, 0x30, 0x01, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0xff, 0xff,
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 67: struct GUID (STRUCT) */
    0x15, 0x03, 0x08, 0x00, 0x08, 0x08, 0x5b, /* 77: struct NSI_RPC_LUID (STRUCT) */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, /* 84: range */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, /* 94: range */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 104: struct NSI_RPC_KEY (PSTRUCT) */
    0x12, 0x00, 0x06, 0x00, 0x5b, 0x36, 0x08, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 124: array */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x0d, 0x36, 0x08, 0x08, /* 136: struct NSI_RPC_PARAMETER (BOGUS) */
    0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 153: array */
    0x16, 0x03, 0x18, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 165: struct NSI_RPC_ALL_PARAMETERS (PSTRUCT) */
    0x12, 0x00, 0x1e, 0x00, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, 0x12, 0x00,
    0x20, 0x00, 0x46, 0x5c, 0x10, 0x00, 0x10, 0x00, 0x12, 0x00, 0x22, 0x00,
    0x5b, 0x36, 0x08, 0x36, 0x08, 0x36, 0x08, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 209: array */
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 221: array */
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x14, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 233: array */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, /* 245: range */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, /* 255: range */
    0x16, 0x03, 0x0c, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 265: struct NSI_RPC_KEYS (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x36, 0x08, 0x08, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 286: array */
    0x16, 0x03, 0x24, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 298: struct NSI_RPC_ALL_OBJECTS (PSTRUCT) */
    0x12, 0x00, 0x21, 0x00, 0x46, 0x5c, 0x0c, 0x00, 0x0c, 0x00, 0x12, 0x00,
    0x23, 0x00, 0x46, 0x5c, 0x18, 0x00, 0x18, 0x00, 0x12, 0x00, 0x25, 0x00,
    0x5b, 0x36, 0x08, 0x08, 0x36, 0x08, 0x08, 0x36, 0x08, 0x08, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x08, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 345: array */
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x14, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 357: array */
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x20, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 369: array */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, /* 381: range */
    0xb7, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, /* 391: range */
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 401: struct NSI_RPC_RW (PSTRUCT) */
    0x12, 0x00, 0x06, 0x00, 0x5b, 0x36, 0x08, 0x5b,
    0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x5b, /* 421: array */
    0x15, 0x03, 0x08, 0x00, 0x08, 0x08, 0x5b, /* 433: struct NSI_RPC_COMPARTMENT (STRUCT) */
    0x30, 0xa4, 0x00, 0x00, /* 440: context handle NOTIFICATION_HANDLE */
    0x30, 0xe4, 0x00, 0x00, /* 444: context handle NOTIFICATION_HANDLE */
    0x30, 0x44, 0x00, 0x00, /* 448: context handle NOTIFICATION_HANDLE */
    0x15, 0x03, 0x04, 0x00, 0x08, 0x5b, /* 452: struct NSI_RPC_COMPARTMENT_ID (STRUCT) */
#endif
};

static const unsigned char nsirpc_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x38, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 2: nsirpc.GetParameter */
    0x07, 0x00, 0x47, 0x06, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x88, 0x00, 0x10, 0x00,
    0x51, 0x00, 0x88, 0x00, 0x18, 0x00, 0x5b, 0x00, 0x1b, 0x01, 0x20, 0x00,
    0x65, 0x00, 0x1b, 0x01, 0x28, 0x00, 0x81, 0x00, 0x70, 0x00, 0x30, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x38, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 64: nsirpc.GetAllParameters */
    0x07, 0x00, 0x47, 0x06, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x88, 0x00, 0x10, 0x00,
    0x51, 0x00, 0x88, 0x00, 0x18, 0x00, 0x5b, 0x00, 0x1b, 0x01, 0x20, 0x00,
    0x65, 0x00, 0x1b, 0x01, 0x28, 0x00, 0x9f, 0x00, 0x70, 0x00, 0x30, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x40, 0x00, 0x32, 0x00, 0x00, 0x00, 0x0f, 0x00, /* 126: nsirpc.EnumerateObjectsAllParameters */
    0x0f, 0x00, 0x47, 0x07, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x88, 0x00, 0x10, 0x00,
    0xe1, 0x00, 0x88, 0x00, 0x18, 0x00, 0xeb, 0x00, 0x1b, 0x01, 0x20, 0x00,
    0xf5, 0x00, 0x1b, 0x01, 0x28, 0x00, 0x11, 0x01, 0x58, 0x01, 0x30, 0x00,
    0x08, 0x5c, 0x70, 0x00, 0x38, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x38, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 194: nsirpc.SetParameter */
    0x07, 0x00, 0x47, 0x06, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x88, 0x00, 0x10, 0x00,
    0x53, 0x01, 0x88, 0x00, 0x18, 0x00, 0x5d, 0x01, 0x1b, 0x01, 0x20, 0x00,
    0x65, 0x00, 0x1b, 0x01, 0x28, 0x00, 0x81, 0x00, 0x70, 0x00, 0x30, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x38, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 256: nsirpc.SetAllParameters */
    0x07, 0x00, 0x47, 0x06, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x88, 0x00, 0x10, 0x00,
    0x53, 0x01, 0x88, 0x00, 0x18, 0x00, 0x5d, 0x01, 0x1b, 0x01, 0x20, 0x00,
    0x65, 0x00, 0x1b, 0x01, 0x28, 0x00, 0x67, 0x01, 0x70, 0x00, 0x30, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x28, 0x00, 0x32, 0x00, 0x00, 0x00, 0x0b, 0x00, /* 318: nsirpc.RegisterChangeNotification */
    0x1f, 0x00, 0x46, 0x04, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00,
    0x83, 0x01, 0x10, 0x01, 0x18, 0x00, 0x8a, 0x01, 0x70, 0x00, 0x20, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 368: nsirpc.DeregisterChangeNotification */
    0x1f, 0x00, 0x46, 0x03, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x18, 0x01, 0x10, 0x00,
    0x8e, 0x01, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x38, 0x00, 0x32, 0x00, 0x08, 0x00, 0x17, 0x00, /* 412: nsirpc.RequestChangeNotification */
    0x0b, 0x00, 0xc5, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x10, 0x00, 0x92, 0x01, 0x13, 0x41, 0x18, 0x00,
    0x65, 0x00, 0x13, 0x61, 0x20, 0x00, 0x81, 0x00, 0x50, 0x21, 0x28, 0x00,
    0x0d, 0x5c, 0x70, 0x00, 0x30, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x38, 0x00, 0x32, 0x00, 0x00, 0x00, 0x0b, 0x00, /* 468: nsirpc.ParameterChange */
    0x07, 0x00, 0x46, 0x06, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x10, 0x00,
    0x65, 0x00, 0x0b, 0x01, 0x18, 0x00, 0x81, 0x00, 0x8a, 0x00, 0x20, 0x00,
    0x96, 0x01, 0x48, 0x00, 0x28, 0x00, 0x0d, 0x5c, 0x70, 0x00, 0x30, 0x00,
    0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x1c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 2: nsirpc.GetParameter */
    0x07, 0x00, 0x47, 0x06, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x88, 0x00, 0x08, 0x00, 0x54, 0x00,
    0x88, 0x00, 0x0c, 0x00, 0x5e, 0x00, 0x1b, 0x01, 0x10, 0x00, 0x68, 0x00,
    0x1b, 0x01, 0x14, 0x00, 0x88, 0x00, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x1c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 62: nsirpc.GetAllParameters */
    0x07, 0x00, 0x47, 0x06, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x88, 0x00, 0x08, 0x00, 0x54, 0x00,
    0x88, 0x00, 0x0c, 0x00, 0x5e, 0x00, 0x1b, 0x01, 0x10, 0x00, 0x68, 0x00,
    0x1b, 0x01, 0x14, 0x00, 0xa5, 0x00, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x0f, 0x00, /* 122: nsirpc.EnumerateObjectsAllParameters */
    0x0f, 0x00, 0x47, 0x07, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x88, 0x00, 0x08, 0x00, 0xf5, 0x00,
    0x88, 0x00, 0x0c, 0x00, 0xff, 0x00, 0x1b, 0x01, 0x10, 0x00, 0x09, 0x01,
    0x1b, 0x01, 0x14, 0x00, 0x2a, 0x01, 0x58, 0x01, 0x18, 0x00, 0x08, 0x5c,
    0x70, 0x00, 0x1c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x1c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 188: nsirpc.SetParameter */
    0x07, 0x00, 0x47, 0x06, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x88, 0x00, 0x08, 0x00, 0x7d, 0x01,
    0x88, 0x00, 0x0c, 0x00, 0x87, 0x01, 0x1b, 0x01, 0x10, 0x00, 0x68, 0x00,
    0x1b, 0x01, 0x14, 0x00, 0x88, 0x00, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x1c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 248: nsirpc.SetAllParameters */
    0x07, 0x00, 0x47, 0x06, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x88, 0x00, 0x08, 0x00, 0x7d, 0x01,
    0x88, 0x00, 0x0c, 0x00, 0x87, 0x01, 0x1b, 0x01, 0x10, 0x00, 0x68, 0x00,
    0x1b, 0x01, 0x14, 0x00, 0x91, 0x01, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x18, 0x00, 0x32, 0x00, 0x00, 0x00, 0x0b, 0x00, /* 308: nsirpc.RegisterChangeNotification */
    0x1f, 0x00, 0x46, 0x04, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x8a, 0x00, 0x08, 0x00, 0xb1, 0x01,
    0x10, 0x01, 0x10, 0x00, 0xb8, 0x01, 0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x17, 0x00, /* 356: nsirpc.DeregisterChangeNotification */
    0x1f, 0x00, 0x46, 0x03, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x18, 0x01, 0x08, 0x00, 0xbc, 0x01,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x07, 0x00, 0x1c, 0x00, 0x32, 0x00, 0x04, 0x00, 0x17, 0x00, /* 398: nsirpc.RequestChangeNotification */
    0x0b, 0x00, 0xc5, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x08, 0x00, 0xc0, 0x01, 0x13, 0x21, 0x0c, 0x00, 0x68, 0x00,
    0x13, 0x41, 0x10, 0x00, 0x88, 0x00, 0x50, 0x21, 0x14, 0x00, 0x0d, 0x5c,
    0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x08, 0x00, 0x1c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x0b, 0x00, /* 452: nsirpc.ParameterChange */
    0x07, 0x00, 0x46, 0x06, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x01, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x08, 0x00, 0x68, 0x00,
    0x0b, 0x01, 0x0c, 0x00, 0x88, 0x00, 0x8a, 0x00, 0x10, 0x00, 0xc4, 0x01,
    0x48, 0x00, 0x14, 0x00, 0x0d, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short nsirpc_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 64, 126, 194, 256, 318, 368, 412, 468
#else
    2, 62, 122, 188, 248, 308, 356, 398, 452
#endif
};

static const NDR_RUNDOWN nsirpc_RundownRoutines[] = {
    (NDR_RUNDOWN)(void *)NOTIFICATION_HANDLE_rundown
};

static RPC_CLIENT_INTERFACE nsirpc_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x7ea70bcf, 0x48af, 0x4f6a, {0x89, 0x68, 0x6a, 0x44, 0x07, 0x54, 0xd5, 0xfa}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE nsirpc_c_ifspec = (RPC_IF_HANDLE)&nsirpc_c_ifspec_data;

static MIDL_STUB_DESC nsirpc_StubDesc = {
    .RpcInterfaceInformation = &nsirpc_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .apfnNdrRundownRoutines = nsirpc_RundownRoutines,
    .pFormatTypes = nsirpc_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef NSIRPC_CLIENT_ONLY
static const SERVER_ROUTINE nsirpc_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)nsirpc_GetParameter_s,
    (SERVER_ROUTINE)(void *)nsirpc_GetAllParameters_s,
    (SERVER_ROUTINE)(void *)nsirpc_EnumerateObjectsAllParameters_s,
    (SERVER_ROUTINE)(void *)nsirpc_SetParameter_s,
    (SERVER_ROUTINE)(void *)nsirpc_SetAllParameters_s,
    (SERVER_ROUTINE)(void *)nsirpc_RegisterChangeNotification_s,
    (SERVER_ROUTINE)(void *)nsirpc_DeregisterChangeNotification_s,
    (SERVER_ROUTINE)(void *)nsirpc_RequestChangeNotification_s,
    (SERVER_ROUTINE)(void *)nsirpc_ParameterChange_s
};

int (*nsirpc_hook)(PRPC_MESSAGE);

static void __RPC_STUB nsirpc_dispatch_0(PRPC_MESSAGE m)
{
    if (nsirpc_hook && nsirpc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB nsirpc_dispatch_1(PRPC_MESSAGE m)
{
    if (nsirpc_hook && nsirpc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB nsirpc_dispatch_2(PRPC_MESSAGE m)
{
    if (nsirpc_hook && nsirpc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB nsirpc_dispatch_3(PRPC_MESSAGE m)
{
    if (nsirpc_hook && nsirpc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB nsirpc_dispatch_4(PRPC_MESSAGE m)
{
    if (nsirpc_hook && nsirpc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB nsirpc_dispatch_5(PRPC_MESSAGE m)
{
    if (nsirpc_hook && nsirpc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB nsirpc_dispatch_6(PRPC_MESSAGE m)
{
    if (nsirpc_hook && nsirpc_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB nsirpc_dispatch_7(PRPC_MESSAGE m)
{
    if (nsirpc_hook && nsirpc_hook(m))
        return;
    NdrAsyncServerCall(m);
}
static void __RPC_STUB nsirpc_dispatch_8(PRPC_MESSAGE m)
{
    if (nsirpc_hook && nsirpc_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION nsirpc_DispatchFunctions[] = {
nsirpc_dispatch_0, nsirpc_dispatch_1, nsirpc_dispatch_2, nsirpc_dispatch_3, nsirpc_dispatch_4, nsirpc_dispatch_5, nsirpc_dispatch_6, nsirpc_dispatch_7, nsirpc_dispatch_8
};
static RPC_DISPATCH_TABLE nsirpc_DispatchTable = { 9, nsirpc_DispatchFunctions, 0 };
static MIDL_SERVER_INFO nsirpc_ServerInfo = {
    .pStubDesc = &nsirpc_StubDesc,
    .DispatchTable = nsirpc_ServerRoutineTable,
    .ProcString = nsirpc_ProcFormat,
    .FmtStringOffset = nsirpc_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE nsirpc_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x7ea70bcf, 0x48af, 0x4f6a, {0x89, 0x68, 0x6a, 0x44, 0x07, 0x54, 0xd5, 0xfa}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &nsirpc_DispatchTable, 0, 0, 0, &nsirpc_ServerInfo, 0 };
RPC_IF_HANDLE nsirpc_s_ifspec = (RPC_IF_HANDLE)&nsirpc_s_ifspec_data;
#endif

static NDRGEN_UNUSED unsigned long WINAPI nsirpc_GetParameter(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store, NSI_RPC_GET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_PARAMETER *parameter)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[0], h, object, store, action, key, parameter).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
        NSI_RPC_OBJECT *object;
        NSI_RPC_STORE store;
        NSI_RPC_GET_ACTION action;
        NSI_RPC_KEY *key;
        NSI_RPC_PARAMETER *parameter;
    } frame = { h, object, store, action, key, parameter };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI nsirpc_GetAllParameters(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store, NSI_RPC_GET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_ALL_PARAMETERS *parameters)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[1], h, object, store, action, key, parameters).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
        NSI_RPC_OBJECT *object;
        NSI_RPC_STORE store;
        NSI_RPC_GET_ACTION action;
        NSI_RPC_KEY *key;
        NSI_RPC_ALL_PARAMETERS *parameters;
    } frame = { h, object, store, action, key, parameters };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[1], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI nsirpc_EnumerateObjectsAllParameters(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store, NSI_RPC_GET_ACTION action, NSI_RPC_KEYS *keys, NSI_RPC_ALL_OBJECTS *objects, unsigned long *count)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[2], h, object, store, action, keys, objects, count).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
        NSI_RPC_OBJECT *object;
        NSI_RPC_STORE store;
        NSI_RPC_GET_ACTION action;
        NSI_RPC_KEYS *keys;
        NSI_RPC_ALL_OBJECTS *objects;
        unsigned long *count;
    } frame = { h, object, store, action, keys, objects, count };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[2], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI nsirpc_SetParameter(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store, NSI_RPC_SET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_PARAMETER *parameter)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[3], h, object, store, action, key, parameter).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
        NSI_RPC_OBJECT *object;
        NSI_RPC_STORE store;
        NSI_RPC_SET_ACTION action;
        NSI_RPC_KEY *key;
        NSI_RPC_PARAMETER *parameter;
    } frame = { h, object, store, action, key, parameter };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[3], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI nsirpc_SetAllParameters(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store, NSI_RPC_SET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_RW *parameters)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[4], h, object, store, action, key, parameters).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
        NSI_RPC_OBJECT *object;
        NSI_RPC_STORE store;
        NSI_RPC_SET_ACTION action;
        NSI_RPC_KEY *key;
        NSI_RPC_RW *parameters;
    } frame = { h, object, store, action, key, parameters };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[4], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI nsirpc_RegisterChangeNotification(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_COMPARTMENT compartment, NOTIFICATION_HANDLE *handle)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[5], h, object, compartment, handle).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
        NSI_RPC_OBJECT *object;
        NSI_RPC_COMPARTMENT compartment;
        NOTIFICATION_HANDLE *handle;
    } frame = { h, object, compartment, handle };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[5], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI nsirpc_DeregisterChangeNotification(handle_t h, NSI_RPC_OBJECT *object, NOTIFICATION_HANDLE *handle)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[6], h, object, handle).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
        NSI_RPC_OBJECT *object;
        NOTIFICATION_HANDLE *handle;
    } frame = { h, object, handle };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[6], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED unsigned long WINAPI nsirpc_ParameterChange(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_KEY *key, NSI_RPC_PARAMETER *parameter, NSI_RPC_COMPARTMENT_ID compartment, NSI_RPC_NOTIFICATION type)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[8], h, object, key, parameter, compartment, type).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
        NSI_RPC_OBJECT *object;
        NSI_RPC_KEY *key;
        NSI_RPC_PARAMETER *parameter;
        NSI_RPC_COMPARTMENT_ID compartment;
        NSI_RPC_NOTIFICATION type;
    } frame = { h, object, key, parameter, compartment, type };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&nsirpc_StubDesc, nsirpc_ProcFormat + nsirpc_FormatStringOffsetTable[8], (unsigned char *)&frame).Simple;
#endif
}

#endif
