/*
 * the ndr engine of rpcrt4.dll: the stub message, the stub descriptors that midl generates and the functions that
 * marshal types by their format strings. The layouts are the ones midl compiled stubs are built against.
 */
#ifndef MIME_WIN32_RPCNDR_H
#define MIME_WIN32_RPCNDR_H

#include <mime/win32/rpc.h>
#include <mime/win32/ndrtypes.h>

#define NDR_CHAR_REP_MASK 0x0000000Fu
#define NDR_INT_REP_MASK 0x000000F0u
#define NDR_FLOAT_REP_MASK 0x0000FF00u
#define NDR_LITTLE_ENDIAN 0x00000010u
#define NDR_BIG_ENDIAN 0x00000000u
#define NDR_IEEE_FLOAT 0x00000000u
#define NDR_VAX_FLOAT 0x00000100u
#define NDR_IBM_FLOAT 0x00000300u
#define NDR_ASCII_CHAR 0x00000000u
#define NDR_EBCDIC_CHAR 0x00000001u
#define NDR_LOCAL_DATA_REPRESENTATION 0x00000010u

typedef unsigned char byte;
typedef unsigned char boolean;
typedef ULONG error_status_t;

typedef void *NDR_CCONTEXT;
typedef struct _NDR_SCONTEXT {
    void *pad[2];
    void *userContext;
} *NDR_SCONTEXT;
typedef void(__RPC_API *NDR_RUNDOWN)(void *context);
typedef void(__RPC_API *NDR_NOTIFY_ROUTINE)(void);
typedef void(__RPC_API *NDR_NOTIFY2_ROUTINE)(boolean flag);

typedef struct _SCONTEXT_QUEUE {
    ULONG NumberOfObjects;
    NDR_SCONTEXT *ArrayOfObjects;
} SCONTEXT_QUEUE, *PSCONTEXT_QUEUE;

#define cbNDRContext 20

struct _MIDL_STUB_MESSAGE;
struct _MIDL_STUB_DESC;
struct _FULL_PTR_XLAT_TABLES;
struct IRpcChannelBuffer;
struct IRpcStubBuffer;
struct _NDR_ASYNC_MESSAGE;
struct _NDR_CORRELATION_INFO;
struct _NDR_PROC_CONTEXT;
struct NDR_ALLOC_ALL_NODES_CONTEXT;
struct NDR_POINTER_QUEUE_STATE;

typedef const unsigned char *PFORMAT_STRING;
typedef unsigned char *RPC_BUFPTR;
typedef ULONG RPC_LENGTH;
typedef void(__RPC_API *EXPR_EVAL)(struct _MIDL_STUB_MESSAGE *);

typedef struct {
    LONG Dimension;
    ULONG *BufferConformanceMark;
    ULONG *BufferVarianceMark;
    ULONG *MaxCountArray;
    ULONG *OffsetArray;
    ULONG *ActualCountArray;
} ARRAY_INFO, *PARRAY_INFO;

typedef struct _NDR_ASYNC_MESSAGE *PNDR_ASYNC_MESSAGE;
typedef struct _NDR_CORRELATION_INFO *PNDR_CORRELATION_INFO;

typedef struct {
    ULONG WireCodeset;
    ULONG DesiredReceivingCodeset;
    void *CSArrayInfo;
} CS_STUB_INFO;

typedef struct _MIDL_STUB_MESSAGE {
    PRPC_MESSAGE RpcMsg;
    unsigned char *Buffer;
    unsigned char *BufferStart;
    unsigned char *BufferEnd;
    unsigned char *BufferMark;
    ULONG BufferLength;
    ULONG MemorySize;
    unsigned char *Memory;
    unsigned char IsClient;
    unsigned char Pad;
    unsigned short uFlags2;
    int ReuseBuffer;
    struct NDR_ALLOC_ALL_NODES_CONTEXT *pAllocAllNodesContext;
    struct NDR_POINTER_QUEUE_STATE *pPointerQueueState;
    int IgnoreEmbeddedPointers;
    unsigned char *PointerBufferMark;
    unsigned char CorrDespIncrement;
    unsigned char uFlags;
    unsigned short UniquePtrCount;
    ULONG_PTR MaxCount;
    ULONG Offset;
    ULONG ActualCount;
    void *(__RPC_API *pfnAllocate)(size_t);
    void(__RPC_API *pfnFree)(void *);
    unsigned char *StackTop;
    unsigned char *pPresentedType;
    unsigned char *pTransmitType;
    handle_t SavedHandle;
    const struct _MIDL_STUB_DESC *StubDesc;
    struct _FULL_PTR_XLAT_TABLES *FullPtrXlatTables;
    ULONG FullPtrRefId;
    ULONG PointerLength;
    unsigned int fInDontFree : 1;
    unsigned int fDontCallFreeInst : 1;
    unsigned int fInOnlyParam : 1;
    unsigned int fHasReturn : 1;
    unsigned int fHasExtensions : 1;
    unsigned int fHasNewCorrDesc : 1;
    unsigned int fIsIn : 1;
    unsigned int fIsOut : 1;
    unsigned int fIsOicf : 1;
    unsigned int fBufferValid : 1;
    unsigned int fHasMemoryValidateCallback : 1;
    unsigned int fInFree : 1;
    unsigned int fNeedMCCP : 1;
    unsigned int fUnused2 : 3;
    unsigned int fUnused3 : 16;
    ULONG dwDestContext;
    void *pvDestContext;
    NDR_SCONTEXT *SavedContextHandles;
    LONG ParamNumber;
    struct IRpcChannelBuffer *pRpcChannelBuffer;
    PARRAY_INFO pArrayInfo;
    ULONG *SizePtrCountArray;
    ULONG *SizePtrOffsetArray;
    ULONG *SizePtrLengthArray;
    void *pArgQueue;
    ULONG dwStubPhase;
    void *LowStackMark;
    PNDR_ASYNC_MESSAGE pAsyncMsg;
    PNDR_CORRELATION_INFO pCorrInfo;
    unsigned char *pCorrMemory;
    void *pMemoryList;
    CS_STUB_INFO *pCSInfo;
    unsigned char *ConformanceMark;
    unsigned char *VarianceMark;
    INT_PTR Unused;
    struct _NDR_PROC_CONTEXT *pContext;
    void *ContextHandleHash;
    void *pUserMarshalList;
    unsigned char *pFullPtrFormat;
    INT_PTR Reserved51_4;
    INT_PTR Reserved51_5;
} MIDL_STUB_MESSAGE, *PMIDL_STUB_MESSAGE;

typedef void *(__RPC_API *GENERIC_BINDING_ROUTINE)(void *);
typedef void(__RPC_API *GENERIC_UNBIND_ROUTINE)(void *, unsigned char *);

typedef struct _GENERIC_BINDING_ROUTINE_PAIR {
    GENERIC_BINDING_ROUTINE pfnBind;
    GENERIC_UNBIND_ROUTINE pfnUnbind;
} GENERIC_BINDING_ROUTINE_PAIR, *PGENERIC_BINDING_ROUTINE_PAIR;

typedef struct __GENERIC_BINDING_INFO {
    void *pObj;
    unsigned int Size;
    GENERIC_BINDING_ROUTINE pfnBind;
    GENERIC_UNBIND_ROUTINE pfnUnbind;
} GENERIC_BINDING_INFO, *PGENERIC_BINDING_INFO;

typedef void(__RPC_API *XMIT_HELPER_ROUTINE)(PMIDL_STUB_MESSAGE);

typedef struct _XMIT_ROUTINE_QUINTUPLE {
    XMIT_HELPER_ROUTINE pfnTranslateToXmit;
    XMIT_HELPER_ROUTINE pfnTranslateFromXmit;
    XMIT_HELPER_ROUTINE pfnFreeXmit;
    XMIT_HELPER_ROUTINE pfnFreeInst;
} XMIT_ROUTINE_QUINTUPLE, *PXMIT_ROUTINE_QUINTUPLE;

typedef ULONG(__RPC_API *USER_MARSHAL_SIZING_ROUTINE)(ULONG *, ULONG, void *);
typedef unsigned char *(__RPC_API *USER_MARSHAL_MARSHALLING_ROUTINE)(ULONG *, unsigned char *, void *);
typedef unsigned char *(__RPC_API *USER_MARSHAL_UNMARSHALLING_ROUTINE)(ULONG *, unsigned char *, void *);
typedef void(__RPC_API *USER_MARSHAL_FREEING_ROUTINE)(ULONG *, void *);

typedef struct _USER_MARSHAL_ROUTINE_QUADRUPLE {
    USER_MARSHAL_SIZING_ROUTINE pfnBufferSize;
    USER_MARSHAL_MARSHALLING_ROUTINE pfnMarshall;
    USER_MARSHAL_UNMARSHALLING_ROUTINE pfnUnmarshall;
    USER_MARSHAL_FREEING_ROUTINE pfnFree;
} USER_MARSHAL_ROUTINE_QUADRUPLE;

#define USER_MARSHAL_CB_SIGNATURE 0x55535243u

typedef enum _USER_MARSHAL_CB_TYPE {
    USER_MARSHAL_CB_BUFFER_SIZE,
    USER_MARSHAL_CB_MARSHALL,
    USER_MARSHAL_CB_UNMARSHALL,
    USER_MARSHAL_CB_FREE
} USER_MARSHAL_CB_TYPE;

typedef struct _USER_MARSHAL_CB {
    ULONG Flags;
    PMIDL_STUB_MESSAGE pStubMsg;
    PFORMAT_STRING pReserve;
    ULONG Signature;
    USER_MARSHAL_CB_TYPE CBType;
    PFORMAT_STRING pFormat;
    PFORMAT_STRING pTypeFormat;
} USER_MARSHAL_CB;

#define USER_CALL_CTXT_MASK(f) ((f) & 0x00ff)
#define USER_CALL_AUX_MASK(f) ((f) & 0xff00)
#define GET_USER_DATA_REP(f) ((f) >> 16)
#define USER_CALL_IS_ASYNC 0x0100
#define USER_CALL_NEW_CORRELATION_DESC 0x0200

typedef struct _MALLOC_FREE_STRUCT {
    void *(__RPC_API *pfnAllocate)(size_t);
    void(__RPC_API *pfnFree)(void *);
} MALLOC_FREE_STRUCT;

typedef struct _COMM_FAULT_OFFSETS {
    short CommOffset;
    short FaultOffset;
} COMM_FAULT_OFFSETS;

typedef struct _NDR_EXPR_DESC {
    const unsigned short *pOffset;
    PFORMAT_STRING pFormatExpr;
} NDR_EXPR_DESC;

typedef struct _NDR_CS_SIZE_CONVERT_ROUTINES {
    void *pfnNetSize;
    void *pfnToNetCs;
    void *pfnLocalSize;
    void *pfnFromNetCs;
} NDR_CS_SIZE_CONVERT_ROUTINES;

typedef struct _NDR_CS_ROUTINES {
    NDR_CS_SIZE_CONVERT_ROUTINES *pSizeConvertRoutines;
    void **pTagGettingRoutines;
} NDR_CS_ROUTINES;

typedef struct _MIDL_STUB_DESC {
    void *RpcInterfaceInformation;
    void *(__RPC_API *pfnAllocate)(size_t);
    void(__RPC_API *pfnFree)(void *);
    union {
        handle_t *pAutoHandle;
        handle_t *pPrimitiveHandle;
        PGENERIC_BINDING_INFO pGenericBindingInfo;
    } IMPLICIT_HANDLE_INFO;
    const NDR_RUNDOWN *apfnNdrRundownRoutines;
    const GENERIC_BINDING_ROUTINE_PAIR *aGenericBindingRoutinePairs;
    const EXPR_EVAL *apfnExprEval;
    const XMIT_ROUTINE_QUINTUPLE *aXmitQuintuple;
    const unsigned char *pFormatTypes;
    int fCheckBounds;
    ULONG Version;
    MALLOC_FREE_STRUCT *pMallocFreeStruct;
    LONG MIDLVersion;
    const COMM_FAULT_OFFSETS *CommFaultOffsets;
    const USER_MARSHAL_ROUTINE_QUADRUPLE *aUserMarshalQuadruple;
    const NDR_NOTIFY_ROUTINE *NotifyRoutineTable;
    ULONG_PTR mFlags;
    const NDR_CS_ROUTINES *CsRoutineTables;
    void *ProxyServerInfo;
    const NDR_EXPR_DESC *pExprInfo;
} MIDL_STUB_DESC;
typedef const MIDL_STUB_DESC *PMIDL_STUB_DESC;

typedef struct _MIDL_FORMAT_STRING {
    short Pad;
    unsigned char Format[1];
} MIDL_FORMAT_STRING;

typedef void(__RPC_API *STUB_THUNK)(PMIDL_STUB_MESSAGE);
typedef LONG(__RPC_API *SERVER_ROUTINE)();

typedef struct _MIDL_METHOD_PROPERTY {
    ULONG Id;
    ULONG_PTR Value;
} MIDL_METHOD_PROPERTY, *PMIDL_METHOD_PROPERTY;

typedef struct _MIDL_METHOD_PROPERTY_MAP {
    ULONG Count;
    const MIDL_METHOD_PROPERTY *Properties;
} MIDL_METHOD_PROPERTY_MAP, *PMIDL_METHOD_PROPERTY_MAP;

typedef struct _MIDL_INTERFACE_METHOD_PROPERTIES {
    unsigned short MethodCount;
    const MIDL_METHOD_PROPERTY_MAP *const *MethodProperties;
} MIDL_INTERFACE_METHOD_PROPERTIES;

typedef struct _MIDL_SYNTAX_INFO {
    RPC_SYNTAX_IDENTIFIER TransferSyntax;
    RPC_DISPATCH_TABLE *DispatchTable;
    PFORMAT_STRING ProcString;
    const unsigned short *FmtStringOffset;
    PFORMAT_STRING TypeString;
    const void *aUserMarshalQuadruple;
    const MIDL_INTERFACE_METHOD_PROPERTIES *pMethodProperties;
    ULONG_PTR pReserved2;
} MIDL_SYNTAX_INFO, *PMIDL_SYNTAX_INFO;

typedef struct _MIDL_SERVER_INFO_ {
    PMIDL_STUB_DESC pStubDesc;
    const SERVER_ROUTINE *DispatchTable;
    PFORMAT_STRING ProcString;
    const unsigned short *FmtStringOffset;
    const STUB_THUNK *ThunkTable;
    PRPC_SYNTAX_IDENTIFIER pTransferSyntax;
    ULONG_PTR nCount;
    PMIDL_SYNTAX_INFO pSyntaxInfo;
} MIDL_SERVER_INFO, *PMIDL_SERVER_INFO;

typedef struct _MIDL_STUBLESS_PROXY_INFO {
    PMIDL_STUB_DESC pStubDesc;
    PFORMAT_STRING ProcFormatString;
    const unsigned short *FormatStringOffset;
    PRPC_SYNTAX_IDENTIFIER pTransferSyntax;
    ULONG_PTR nCount;
    PMIDL_SYNTAX_INFO pSyntaxInfo;
} MIDL_STUBLESS_PROXY_INFO, *PMIDL_STUBLESS_PROXY_INFO;

typedef struct _MIDL_TYPE_PICKLING_INFO {
    ULONG Version;
    ULONG Flags;
    ULONG_PTR Reserved[3];
} MIDL_TYPE_PICKLING_INFO, *PMIDL_TYPE_PICKLING_INFO;

typedef union _CLIENT_CALL_RETURN {
    void *Pointer;
    LONG_PTR Simple;
} CLIENT_CALL_RETURN;

typedef enum { XLAT_SERVER = 1, XLAT_CLIENT } XLAT_SIDE;

typedef struct _FULL_PTR_TO_REFID_ELEMENT {
    struct _FULL_PTR_TO_REFID_ELEMENT *Next;
    void *Pointer;
    ULONG RefId;
    unsigned char State;
} FULL_PTR_TO_REFID_ELEMENT, *PFULL_PTR_TO_REFID_ELEMENT;

typedef struct _FULL_PTR_XLAT_TABLES {
    struct {
        void **XlatTable;
        unsigned char *StateTable;
        ULONG NumberOfEntries;
    } RefIdToPointer;
    struct {
        PFULL_PTR_TO_REFID_ELEMENT *XlatTable;
        ULONG NumberOfBuckets;
        ULONG HashMask;
    } PointerToRefId;
    ULONG NextRefId;
    XLAT_SIDE XlatSide;
} FULL_PTR_XLAT_TABLES, *PFULL_PTR_XLAT_TABLES;

typedef struct _NDR_USER_MARSHAL_INFO_LEVEL1 {
    void *Buffer;
    ULONG BufferSize;
    void *(__RPC_API *pfnAllocate)(size_t);
    void(__RPC_API *pfnFree)(void *);
    struct IRpcChannelBuffer *pRpcChannelBuffer;
    ULONG_PTR Reserved[5];
} NDR_USER_MARSHAL_INFO_LEVEL1;

typedef struct _NDR_USER_MARSHAL_INFO {
    ULONG InformationLevel;
    union {
        NDR_USER_MARSHAL_INFO_LEVEL1 Level1;
    };
} NDR_USER_MARSHAL_INFO;

typedef void *RPC_SS_THREAD_HANDLE;
typedef void *(__RPC_API RPC_CLIENT_ALLOC)(size_t size);
typedef void(__RPC_API RPC_CLIENT_FREE)(void *pointer);

/* ------------------------------------------------------------------ memory, handles and buffers */

void *__RPC_USER MIDL_user_allocate(size_t size);
void __RPC_USER MIDL_user_free(void *pointer);

RPCRTAPI void *RPC_ENTRY NdrAllocate(PMIDL_STUB_MESSAGE msg, size_t length);
RPCRTAPI void RPC_ENTRY NdrClearOutParameters(PMIDL_STUB_MESSAGE msg, PFORMAT_STRING format, void *argument);
RPCRTAPI void *RPC_ENTRY NdrOleAllocate(size_t size);
RPCRTAPI void RPC_ENTRY NdrOleFree(void *memory);
RPCRTAPI unsigned char *RPC_ENTRY NdrGetBuffer(PMIDL_STUB_MESSAGE msg, ULONG length, RPC_BINDING_HANDLE handle);
RPCRTAPI unsigned char *RPC_ENTRY NdrNsGetBuffer(PMIDL_STUB_MESSAGE msg, ULONG length, RPC_BINDING_HANDLE handle);
RPCRTAPI unsigned char *RPC_ENTRY NdrSendReceive(PMIDL_STUB_MESSAGE msg, unsigned char *buffer_end);
RPCRTAPI unsigned char *RPC_ENTRY NdrNsSendReceive(PMIDL_STUB_MESSAGE msg, unsigned char *buffer_end,
                                                   RPC_BINDING_HANDLE *handle);
RPCRTAPI void RPC_ENTRY NdrFreeBuffer(PMIDL_STUB_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrClientInitializeNew(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE msg, PMIDL_STUB_DESC desc,
                                               unsigned int proc_num);
RPCRTAPI void RPC_ENTRY NdrClientInitialize(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE msg, PMIDL_STUB_DESC desc,
                                            unsigned int proc_num);
RPCRTAPI unsigned char *RPC_ENTRY NdrServerInitializeNew(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE msg,
                                                         PMIDL_STUB_DESC desc);
RPCRTAPI unsigned char *RPC_ENTRY NdrServerInitialize(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE msg,
                                                      PMIDL_STUB_DESC desc);
RPCRTAPI unsigned char *RPC_ENTRY NdrServerInitializeUnmarshall(PMIDL_STUB_MESSAGE msg, PMIDL_STUB_DESC desc,
                                                                PRPC_MESSAGE rpc_msg);
RPCRTAPI void RPC_ENTRY NdrServerInitializeMarshall(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE msg);
RPCRTAPI unsigned char *RPC_ENTRY NdrServerInitializePartial(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE msg,
                                                             PMIDL_STUB_DESC desc, ULONG requested_buffer_size);
RPCRTAPI void RPC_ENTRY NdrConvert2(PMIDL_STUB_MESSAGE msg, PFORMAT_STRING format, LONG number_params);
RPCRTAPI void RPC_ENTRY NdrConvert(PMIDL_STUB_MESSAGE msg, PFORMAT_STRING format);
RPCRTAPI RPC_STATUS RPC_ENTRY NdrMapCommAndFaultStatus(PMIDL_STUB_MESSAGE msg, ULONG *comm, ULONG *fault,
                                                       RPC_STATUS status);
RPCRTAPI RPC_STATUS RPC_ENTRY NdrGetUserMarshalInfo(ULONG *flags, ULONG level, NDR_USER_MARSHAL_INFO *info);
RPCRTAPI RPC_STATUS RPC_ENTRY NdrCreateServerInterfaceFromStub(struct IRpcStubBuffer *stub,
                                                               RPC_SERVER_INTERFACE *server_interface);
RPCRTAPI ULONG RPC_ENTRY NdrGetDcomProtocolVersion(PMIDL_STUB_MESSAGE msg);

/* ------------------------------------------------------------------ calls */

RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY NdrClientCall2(PMIDL_STUB_DESC desc, PFORMAT_STRING format, ...);
RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY NdrClientCall(PMIDL_STUB_DESC desc, PFORMAT_STRING format, ...);
RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY NdrAsyncClientCall(PMIDL_STUB_DESC desc, PFORMAT_STRING format, ...);
RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY NdrDcomAsyncClientCall(PMIDL_STUB_DESC desc, PFORMAT_STRING format, ...);
RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY NdrClientCall4(MIDL_STUBLESS_PROXY_INFO *info, ULONG proc, void *return_value, ...);
RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY NdrClientCall3(MIDL_STUBLESS_PROXY_INFO *info, ULONG proc, void *return_value, ...);
RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY Ndr64AsyncClientCall(MIDL_STUBLESS_PROXY_INFO *info, ULONG proc, void *return_value, ...);
RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY Ndr64DcomAsyncClientCall(MIDL_STUBLESS_PROXY_INFO *info, ULONG proc,
                                                          void *return_value, ...);
RPCRTAPI void RPC_ENTRY NdrServerCall2(PRPC_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrServerCall(PRPC_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrServerCallAll(PRPC_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrServerCallNdr64(PRPC_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrServerCall3(PRPC_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrAsyncServerCall(PRPC_MESSAGE msg);
RPCRTAPI void RPC_ENTRY Ndr64AsyncServerCall(PRPC_MESSAGE msg);
RPCRTAPI void RPC_ENTRY Ndr64AsyncServerCall64(PRPC_MESSAGE msg);
RPCRTAPI void RPC_ENTRY Ndr64AsyncServerCallAll(PRPC_MESSAGE msg);
RPCRTAPI LONG RPC_ENTRY NdrStubCall2(struct IRpcStubBuffer *stub, struct IRpcChannelBuffer *channel, PRPC_MESSAGE msg,
                                     ULONG *phase);
RPCRTAPI LONG RPC_ENTRY NdrStubCall(struct IRpcStubBuffer *stub, struct IRpcChannelBuffer *channel, PRPC_MESSAGE msg,
                                    ULONG *phase);
RPCRTAPI LONG RPC_ENTRY NdrStubCall3(struct IRpcStubBuffer *stub, struct IRpcChannelBuffer *channel, PRPC_MESSAGE msg,
                                     ULONG *phase);
RPCRTAPI LONG RPC_ENTRY NdrAsyncStubCall(struct IRpcStubBuffer *stub, struct IRpcChannelBuffer *channel,
                                         PRPC_MESSAGE msg, ULONG *phase);
RPCRTAPI LONG RPC_ENTRY NdrDcomAsyncStubCall(struct IRpcStubBuffer *stub, struct IRpcChannelBuffer *channel,
                                             PRPC_MESSAGE msg, ULONG *phase);
RPCRTAPI LONG RPC_ENTRY Ndr64AsyncStubCall(struct IRpcStubBuffer *stub, struct IRpcChannelBuffer *channel,
                                           PRPC_MESSAGE msg, ULONG *phase);
RPCRTAPI LONG RPC_ENTRY Ndr64DcomAsyncStubCall(struct IRpcStubBuffer *stub, struct IRpcChannelBuffer *channel,
                                               PRPC_MESSAGE msg, ULONG *phase);

/* ------------------------------------------------------------------ simple types and context handles */

RPCRTAPI void RPC_ENTRY NdrSimpleTypeMarshall(PMIDL_STUB_MESSAGE msg, unsigned char *memory, unsigned char format_char);
RPCRTAPI void RPC_ENTRY NdrSimpleTypeUnmarshall(PMIDL_STUB_MESSAGE msg, unsigned char *memory,
                                                unsigned char format_char);
RPCRTAPI unsigned char *RPC_ENTRY NdrUnmarshallBasetypeInline(PMIDL_STUB_MESSAGE msg, unsigned char *memory,
                                                              unsigned char format_char);
RPCRTAPI unsigned char *RPC_ENTRY NdrUserMarshalSimpleTypeConvert(ULONG *flags, unsigned char *buffer,
                                                                  unsigned char format_char);
RPCRTAPI ULONG RPC_ENTRY NdrGetSimpleTypeBufferAlignment(unsigned char format_char);
RPCRTAPI ULONG RPC_ENTRY NdrGetSimpleTypeBufferSize(unsigned char format_char);
RPCRTAPI ULONG RPC_ENTRY NdrGetSimpleTypeMemorySize(unsigned char format_char);
RPCRTAPI RPC_BINDING_HANDLE RPC_ENTRY NDRCContextBinding(NDR_CCONTEXT context);
RPCRTAPI void RPC_ENTRY NDRCContextMarshall(NDR_CCONTEXT context, void *buffer);
RPCRTAPI void RPC_ENTRY NDRCContextUnmarshall(NDR_CCONTEXT *context, RPC_BINDING_HANDLE binding, void *buffer,
                                              ULONG drep);
RPCRTAPI void RPC_ENTRY NDRSContextMarshall(NDR_SCONTEXT context, void *buffer, NDR_RUNDOWN rundown);
RPCRTAPI NDR_SCONTEXT RPC_ENTRY NDRSContextUnmarshall(void *buffer, ULONG drep);
RPCRTAPI void RPC_ENTRY NDRSContextMarshallEx(RPC_BINDING_HANDLE binding, NDR_SCONTEXT context, void *buffer,
                                              NDR_RUNDOWN rundown);
RPCRTAPI NDR_SCONTEXT RPC_ENTRY NDRSContextUnmarshallEx(RPC_BINDING_HANDLE binding, void *buffer, ULONG drep);
RPCRTAPI void RPC_ENTRY NDRSContextMarshall2(RPC_BINDING_HANDLE binding, NDR_SCONTEXT context, void *buffer,
                                             NDR_RUNDOWN rundown, void *guard, ULONG flags);
RPCRTAPI NDR_SCONTEXT RPC_ENTRY NDRSContextUnmarshall2(RPC_BINDING_HANDLE binding, void *buffer, ULONG drep,
                                                       void *guard, ULONG flags);
RPCRTAPI void RPC_ENTRY RpcSsDestroyClientContext(void **context);
RPCRTAPI void RPC_ENTRY NdrClientContextMarshall(PMIDL_STUB_MESSAGE msg, NDR_CCONTEXT context, int check);
RPCRTAPI void RPC_ENTRY NdrClientContextUnmarshall(PMIDL_STUB_MESSAGE msg, NDR_CCONTEXT *context,
                                                   RPC_BINDING_HANDLE binding);
RPCRTAPI void RPC_ENTRY NdrServerContextMarshall(PMIDL_STUB_MESSAGE msg, NDR_SCONTEXT context, NDR_RUNDOWN rundown);
RPCRTAPI NDR_SCONTEXT RPC_ENTRY NdrServerContextUnmarshall(PMIDL_STUB_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrServerContextNewMarshall(PMIDL_STUB_MESSAGE msg, NDR_SCONTEXT context, NDR_RUNDOWN rundown,
                                                    PFORMAT_STRING format);
RPCRTAPI NDR_SCONTEXT RPC_ENTRY NdrServerContextNewUnmarshall(PMIDL_STUB_MESSAGE msg, PFORMAT_STRING format);
RPCRTAPI NDR_SCONTEXT RPC_ENTRY NdrContextHandleInitialize(PMIDL_STUB_MESSAGE msg, PFORMAT_STRING format);
RPCRTAPI void RPC_ENTRY NdrContextHandleSize(PMIDL_STUB_MESSAGE msg, unsigned char *memory, PFORMAT_STRING format);

/* ------------------------------------------------------------------ full pointers and correlation */

RPCRTAPI PFULL_PTR_XLAT_TABLES RPC_ENTRY NdrFullPointerXlatInit(ULONG pointers, XLAT_SIDE side);
RPCRTAPI void RPC_ENTRY NdrFullPointerXlatFree(PFULL_PTR_XLAT_TABLES tables);
RPCRTAPI int RPC_ENTRY NdrFullPointerQueryPointer(PFULL_PTR_XLAT_TABLES tables, void *pointer, unsigned char query,
                                                  ULONG *ref_id);
RPCRTAPI int RPC_ENTRY NdrFullPointerQueryRefId(PFULL_PTR_XLAT_TABLES tables, ULONG ref_id, unsigned char query,
                                                void **pointer);
RPCRTAPI void RPC_ENTRY NdrFullPointerInsertRefId(PFULL_PTR_XLAT_TABLES tables, ULONG ref_id, void *pointer);
RPCRTAPI int RPC_ENTRY NdrFullPointerFree(PFULL_PTR_XLAT_TABLES tables, void *pointer);
RPCRTAPI void RPC_ENTRY NdrCorrelationInitialize(PMIDL_STUB_MESSAGE msg, void *memory, ULONG cache_size, ULONG flags);
RPCRTAPI void RPC_ENTRY NdrCorrelationPass(PMIDL_STUB_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrCorrelationFree(PMIDL_STUB_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrOutInit(PMIDL_STUB_MESSAGE msg, PFORMAT_STRING format, unsigned char **memory);

/* ------------------------------------------------------------------ the type marshalers */

#define NDR_TYPE_ENTRY(name)                                                                                        \
    RPCRTAPI unsigned char *RPC_ENTRY Ndr##name##Marshall(PMIDL_STUB_MESSAGE msg, unsigned char *memory,           \
                                                           PFORMAT_STRING format);                                  \
    RPCRTAPI void RPC_ENTRY Ndr##name##BufferSize(PMIDL_STUB_MESSAGE msg, unsigned char *memory,                   \
                                                   PFORMAT_STRING format);                                          \
    RPCRTAPI unsigned char *RPC_ENTRY Ndr##name##Unmarshall(PMIDL_STUB_MESSAGE msg, unsigned char **memory,        \
                                                             PFORMAT_STRING format, unsigned char must_alloc);      \
    RPCRTAPI ULONG RPC_ENTRY Ndr##name##MemorySize(PMIDL_STUB_MESSAGE msg, PFORMAT_STRING format);                 \
    RPCRTAPI void RPC_ENTRY Ndr##name##Free(PMIDL_STUB_MESSAGE msg, unsigned char *memory, PFORMAT_STRING format)

NDR_TYPE_ENTRY(Pointer);
NDR_TYPE_ENTRY(SimpleStruct);
NDR_TYPE_ENTRY(ConformantStruct);
NDR_TYPE_ENTRY(ConformantVaryingStruct);
NDR_TYPE_ENTRY(ComplexStruct);
NDR_TYPE_ENTRY(FixedArray);
NDR_TYPE_ENTRY(ConformantArray);
NDR_TYPE_ENTRY(ConformantVaryingArray);
NDR_TYPE_ENTRY(VaryingArray);
NDR_TYPE_ENTRY(ComplexArray);
NDR_TYPE_ENTRY(NonConformantString);
NDR_TYPE_ENTRY(ConformantString);
NDR_TYPE_ENTRY(EncapsulatedUnion);
NDR_TYPE_ENTRY(NonEncapsulatedUnion);
NDR_TYPE_ENTRY(ByteCountPointer);
NDR_TYPE_ENTRY(XmitOrRepAs);
NDR_TYPE_ENTRY(UserMarshal);
NDR_TYPE_ENTRY(InterfacePointer);

RPCRTAPI unsigned char *RPC_ENTRY NdrRangeUnmarshall(PMIDL_STUB_MESSAGE msg, unsigned char **memory,
                                                     PFORMAT_STRING format, unsigned char must_alloc);
RPCRTAPI void RPC_ENTRY NdrTypeFree(PMIDL_STUB_MESSAGE msg, unsigned char *memory, PFORMAT_STRING format);
RPCRTAPI void RPC_ENTRY NdrTypeMarshall(PMIDL_STUB_MESSAGE msg, unsigned char *memory, PFORMAT_STRING format);
RPCRTAPI void RPC_ENTRY NdrTypeUnmarshall(PMIDL_STUB_MESSAGE msg, unsigned char **memory, PFORMAT_STRING format);
RPCRTAPI void RPC_ENTRY NdrTypeSize(PMIDL_STUB_MESSAGE msg, unsigned char *memory, PFORMAT_STRING format);

/* ------------------------------------------------------------------ the rpc ss and sm allocators */

RPCRTAPI void *RPC_ENTRY RpcSsAllocate(size_t size);
RPCRTAPI void RPC_ENTRY RpcSsDisableAllocate(void);
RPCRTAPI void RPC_ENTRY RpcSsEnableAllocate(void);
RPCRTAPI void RPC_ENTRY RpcSsFree(void *node);
RPCRTAPI RPC_SS_THREAD_HANDLE RPC_ENTRY RpcSsGetThreadHandle(void);
RPCRTAPI void RPC_ENTRY RpcSsSetClientAllocFree(RPC_CLIENT_ALLOC *alloc, RPC_CLIENT_FREE *free_function);
RPCRTAPI void RPC_ENTRY RpcSsSetThreadHandle(RPC_SS_THREAD_HANDLE id);
RPCRTAPI void RPC_ENTRY RpcSsSwapClientAllocFree(RPC_CLIENT_ALLOC *alloc, RPC_CLIENT_FREE *free_function,
                                                 RPC_CLIENT_ALLOC **old_alloc, RPC_CLIENT_FREE **old_free);
RPCRTAPI void *RPC_ENTRY RpcSmAllocate(size_t size, RPC_STATUS *status);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmClientFree(void *node);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmDestroyClientContext(void **context);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmDisableAllocate(void);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmEnableAllocate(void);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmFree(void *node);
RPCRTAPI RPC_SS_THREAD_HANDLE RPC_ENTRY RpcSmGetThreadHandle(RPC_STATUS *status);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmSetClientAllocFree(RPC_CLIENT_ALLOC *alloc, RPC_CLIENT_FREE *free_function);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmSetThreadHandle(RPC_SS_THREAD_HANDLE id);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmSwapClientAllocFree(RPC_CLIENT_ALLOC *alloc, RPC_CLIENT_FREE *free_function,
                                                       RPC_CLIENT_ALLOC **old_alloc, RPC_CLIENT_FREE **old_free);
RPCRTAPI void RPC_ENTRY NdrRpcSsEnableAllocate(PMIDL_STUB_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrRpcSsDisableAllocate(PMIDL_STUB_MESSAGE msg);
RPCRTAPI void RPC_ENTRY NdrRpcSmSetClientToOsf(PMIDL_STUB_MESSAGE msg);
RPCRTAPI void *RPC_ENTRY NdrRpcSmClientAllocate(size_t size);
RPCRTAPI void RPC_ENTRY NdrRpcSmClientFree(void *node);
RPCRTAPI void *RPC_ENTRY NdrRpcSsDefaultAllocate(size_t size);
RPCRTAPI void RPC_ENTRY NdrRpcSsDefaultFree(void *node);
RPCRTAPI void RPC_ENTRY RpcUserFree(handle_t handle, void *buffer);

#endif
