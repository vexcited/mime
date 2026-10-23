/* the rpc runtime of rpcrt4.dll: bindings, servers, uuids, async calls and status codes. */
#ifndef MIME_WIN32_RPC_H
#define MIME_WIN32_RPC_H

#include <mime/win32/winbase.h>

#ifndef RPCRTAPI
#define RPCRTAPI DECLSPEC_IMPORT
#endif

#define RPC_ENTRY WINAPI
#define RPC_VAR_ENTRY __cdecl
#define __RPC_API WINAPI
#define __RPC_USER WINAPI
#define __RPC_STUB WINAPI

typedef LONG RPC_STATUS;
typedef GUID UUID;
typedef unsigned char *RPC_CSTR;
typedef unsigned short *RPC_WSTR;
typedef void *I_RPC_HANDLE;
typedef I_RPC_HANDLE RPC_BINDING_HANDLE;
typedef RPC_BINDING_HANDLE handle_t;
typedef void *RPC_IF_HANDLE;
typedef void *RPC_AUTH_IDENTITY_HANDLE;
typedef void *RPC_AUTHZ_HANDLE;

/* ------------------------------------------------------------------ status codes */

#define RPC_S_OK 0
#define RPC_S_INVALID_ARG ERROR_INVALID_PARAMETER
#define RPC_S_OUT_OF_MEMORY ERROR_OUTOFMEMORY
#define RPC_S_OUT_OF_THREADS 164
#define RPC_S_INVALID_LEVEL ERROR_INVALID_PARAMETER
#define RPC_S_BUFFER_TOO_SMALL 122
#define RPC_S_INVALID_SECURITY_DESC 1338
#define RPC_S_ACCESS_DENIED 5
#define RPC_S_SERVER_OUT_OF_MEMORY 1130
#define RPC_S_ASYNC_CALL_PENDING 997
#define RPC_S_UNKNOWN_PRINCIPAL 1332
#define RPC_S_TIMEOUT 1460
#define RPC_S_INVALID_STRING_BINDING 1700
#define RPC_S_WRONG_KIND_OF_BINDING 1701
#define RPC_S_INVALID_BINDING 1702
#define RPC_S_PROTSEQ_NOT_SUPPORTED 1703
#define RPC_S_INVALID_RPC_PROTSEQ 1704
#define RPC_S_INVALID_STRING_UUID 1705
#define RPC_S_INVALID_ENDPOINT_FORMAT 1706
#define RPC_S_INVALID_NET_ADDR 1707
#define RPC_S_NO_ENDPOINT_FOUND 1708
#define RPC_S_INVALID_TIMEOUT 1709
#define RPC_S_OBJECT_NOT_FOUND 1710
#define RPC_S_ALREADY_REGISTERED 1711
#define RPC_S_TYPE_ALREADY_REGISTERED 1712
#define RPC_S_ALREADY_LISTENING 1713
#define RPC_S_NO_PROTSEQS_REGISTERED 1714
#define RPC_S_NOT_LISTENING 1715
#define RPC_S_UNKNOWN_MGR_TYPE 1716
#define RPC_S_UNKNOWN_IF 1717
#define RPC_S_NO_BINDINGS 1718
#define RPC_S_NO_PROTSEQS 1719
#define RPC_S_CANT_CREATE_ENDPOINT 1720
#define RPC_S_OUT_OF_RESOURCES 1721
#define RPC_S_SERVER_UNAVAILABLE 1722
#define RPC_S_SERVER_TOO_BUSY 1723
#define RPC_S_INVALID_NETWORK_OPTIONS 1724
#define RPC_S_NO_CALL_ACTIVE 1725
#define RPC_S_CALL_FAILED 1726
#define RPC_S_CALL_FAILED_DNE 1727
#define RPC_S_PROTOCOL_ERROR 1728
#define RPC_S_PROXY_ACCESS_DENIED 1729
#define RPC_S_UNSUPPORTED_TRANS_SYN 1730
#define RPC_S_UNSUPPORTED_TYPE 1732
#define RPC_S_INVALID_TAG 1733
#define RPC_S_INVALID_BOUND 1734
#define RPC_S_NO_ENTRY_NAME 1735
#define RPC_S_INVALID_NAME_SYNTAX 1736
#define RPC_S_UNSUPPORTED_NAME_SYNTAX 1737
#define RPC_S_UUID_NO_ADDRESS 1739
#define RPC_S_DUPLICATE_ENDPOINT 1740
#define RPC_S_UNKNOWN_AUTHN_TYPE 1741
#define RPC_S_MAX_CALLS_TOO_SMALL 1742
#define RPC_S_STRING_TOO_LONG 1743
#define RPC_S_PROTSEQ_NOT_FOUND 1744
#define RPC_S_PROCNUM_OUT_OF_RANGE 1745
#define RPC_S_BINDING_HAS_NO_AUTH 1746
#define RPC_S_UNKNOWN_AUTHN_SERVICE 1747
#define RPC_S_UNKNOWN_AUTHN_LEVEL 1748
#define RPC_S_INVALID_AUTH_IDENTITY 1749
#define RPC_S_UNKNOWN_AUTHZ_SERVICE 1750
#define EPT_S_INVALID_ENTRY 1751
#define EPT_S_CANT_PERFORM_OP 1752
#define EPT_S_NOT_REGISTERED 1753
#define RPC_S_NOTHING_TO_EXPORT 1754
#define RPC_S_INCOMPLETE_NAME 1755
#define RPC_S_INVALID_VERS_OPTION 1756
#define RPC_S_NO_MORE_MEMBERS 1757
#define RPC_S_NOT_ALL_OBJS_UNEXPORTED 1758
#define RPC_S_INTERFACE_NOT_FOUND 1759
#define RPC_S_ENTRY_ALREADY_EXISTS 1760
#define RPC_S_ENTRY_NOT_FOUND 1761
#define RPC_S_NAME_SERVICE_UNAVAILABLE 1762
#define RPC_S_INVALID_NAF_ID 1763
#define RPC_S_CANNOT_SUPPORT 1764
#define RPC_S_NO_CONTEXT_AVAILABLE 1765
#define RPC_S_INTERNAL_ERROR 1766
#define RPC_S_ZERO_DIVIDE 1767
#define RPC_S_ADDRESS_ERROR 1768
#define RPC_S_FP_DIV_ZERO 1769
#define RPC_S_FP_UNDERFLOW 1770
#define RPC_S_FP_OVERFLOW 1771
#define RPC_X_NO_MORE_ENTRIES 1772
#define RPC_X_SS_CHAR_TRANS_OPEN_FAIL 1773
#define RPC_X_SS_CHAR_TRANS_SHORT_FILE 1774
#define RPC_X_SS_IN_NULL_CONTEXT 1775
#define RPC_X_SS_CONTEXT_DAMAGED 1777
#define RPC_X_SS_HANDLES_MISMATCH 1778
#define RPC_X_SS_CANNOT_GET_CALL_HANDLE 1779
#define RPC_X_NULL_REF_POINTER 1780
#define RPC_X_ENUM_VALUE_OUT_OF_RANGE 1781
#define RPC_X_BYTE_COUNT_TOO_SMALL 1782
#define RPC_X_BAD_STUB_DATA 1783
#define RPC_S_CALL_IN_PROGRESS 1791
#define RPC_S_NO_MORE_BINDINGS 1806
#define RPC_S_NO_INTERFACES 1817
#define RPC_S_CALL_CANCELLED 1818
#define RPC_S_BINDING_INCOMPLETE 1819
#define RPC_S_COMM_FAILURE 1820
#define RPC_S_UNSUPPORTED_AUTHN_LEVEL 1821
#define RPC_S_NO_PRINC_NAME 1822
#define RPC_S_NOT_RPC_ERROR 1823
#define RPC_S_UUID_LOCAL_ONLY 1824
#define RPC_S_SEC_PKG_ERROR 1825
#define RPC_S_NOT_CANCELLED 1826
#define RPC_X_INVALID_ES_ACTION 1827
#define RPC_X_WRONG_ES_VERSION 1828
#define RPC_X_WRONG_STUB_VERSION 1829
#define RPC_X_INVALID_PIPE_OBJECT 1830
#define RPC_X_WRONG_PIPE_ORDER 1831
#define RPC_X_WRONG_PIPE_VERSION 1832
#define RPC_S_COOKIE_AUTH_FAILED 1833
#define RPC_S_DO_NOT_DISTURB 1834
#define RPC_S_SYSTEM_HANDLE_COUNT_EXCEEDED 1835
#define RPC_S_SYSTEM_HANDLE_TYPE_MISMATCH 1836
#define RPC_S_GROUP_MEMBER_NOT_FOUND 1898
#define EPT_S_CANT_CREATE 1899
#define RPC_S_INVALID_OBJECT 1900
#define RPC_S_SEND_INCOMPLETE 1913
#define RPC_S_INVALID_ASYNC_HANDLE 1914
#define RPC_S_INVALID_ASYNC_CALL 1915
#define RPC_X_PIPE_CLOSED 1916
#define RPC_X_PIPE_DISCIPLINE_ERROR 1917
#define RPC_X_PIPE_EMPTY 1918
#define RPC_X_NO_MEMORY RPC_S_OUT_OF_MEMORY
#define RPC_X_INVALID_BOUND RPC_S_INVALID_BOUND
#define RPC_X_INVALID_TAG RPC_S_INVALID_TAG
#define RPC_X_ENUM_VALUE_TOO_LARGE RPC_X_ENUM_VALUE_OUT_OF_RANGE
#define RPC_X_SS_CONTEXT_MISMATCH ERROR_INVALID_HANDLE
#define RPC_X_INVALID_BUFFER ERROR_INVALID_USER_BUFFER
#define RPC_X_PIPE_APP_MEMORY ERROR_OUTOFMEMORY
#define RPC_X_INVALID_PIPE_OPERATION RPC_X_WRONG_PIPE_ORDER


/* rpc hresults that com sees. */
#ifndef RPC_E_CALL_REJECTED
#define RPC_E_CALL_REJECTED ((HRESULT)0x80010001L)
#endif
#ifndef RPC_E_CALL_CANCELED
#define RPC_E_CALL_CANCELED ((HRESULT)0x80010002L)
#endif
#ifndef RPC_E_CANTPOST_INSENDCALL
#define RPC_E_CANTPOST_INSENDCALL ((HRESULT)0x80010003L)
#endif
#ifndef RPC_E_CANTCALLOUT_INASYNCCALL
#define RPC_E_CANTCALLOUT_INASYNCCALL ((HRESULT)0x80010004L)
#endif
#ifndef RPC_E_CANTCALLOUT_INEXTERNALCALL
#define RPC_E_CANTCALLOUT_INEXTERNALCALL ((HRESULT)0x80010005L)
#endif
#ifndef RPC_E_CONNECTION_TERMINATED
#define RPC_E_CONNECTION_TERMINATED ((HRESULT)0x80010006L)
#endif
#ifndef RPC_E_SERVER_DIED
#define RPC_E_SERVER_DIED ((HRESULT)0x80010007L)
#endif
#ifndef RPC_E_CLIENT_DIED
#define RPC_E_CLIENT_DIED ((HRESULT)0x80010008L)
#endif
#ifndef RPC_E_INVALID_DATAPACKET
#define RPC_E_INVALID_DATAPACKET ((HRESULT)0x80010009L)
#endif
#ifndef RPC_E_CANTTRANSMIT_CALL
#define RPC_E_CANTTRANSMIT_CALL ((HRESULT)0x8001000AL)
#endif
#ifndef RPC_E_CLIENT_CANTMARSHAL_DATA
#define RPC_E_CLIENT_CANTMARSHAL_DATA ((HRESULT)0x8001000BL)
#endif
#ifndef RPC_E_CLIENT_CANTUNMARSHAL_DATA
#define RPC_E_CLIENT_CANTUNMARSHAL_DATA ((HRESULT)0x8001000CL)
#endif
#ifndef RPC_E_SERVER_CANTMARSHAL_DATA
#define RPC_E_SERVER_CANTMARSHAL_DATA ((HRESULT)0x8001000DL)
#endif
#ifndef RPC_E_SERVER_CANTUNMARSHAL_DATA
#define RPC_E_SERVER_CANTUNMARSHAL_DATA ((HRESULT)0x8001000EL)
#endif
#ifndef RPC_E_INVALID_DATA
#define RPC_E_INVALID_DATA ((HRESULT)0x8001000FL)
#endif
#ifndef RPC_E_INVALID_PARAMETER
#define RPC_E_INVALID_PARAMETER ((HRESULT)0x80010010L)
#endif
#ifndef RPC_E_CANTCALLOUT_AGAIN
#define RPC_E_CANTCALLOUT_AGAIN ((HRESULT)0x80010011L)
#endif
#ifndef RPC_E_SERVER_DIED_DNE
#define RPC_E_SERVER_DIED_DNE ((HRESULT)0x80010012L)
#endif
#ifndef RPC_E_SYS_CALL_FAILED
#define RPC_E_SYS_CALL_FAILED ((HRESULT)0x80010100L)
#endif
#ifndef RPC_E_OUT_OF_RESOURCES
#define RPC_E_OUT_OF_RESOURCES ((HRESULT)0x80010101L)
#endif
#ifndef RPC_E_ATTEMPTED_MULTITHREAD
#define RPC_E_ATTEMPTED_MULTITHREAD ((HRESULT)0x80010102L)
#endif
#ifndef RPC_E_NOT_REGISTERED
#define RPC_E_NOT_REGISTERED ((HRESULT)0x80010103L)
#endif
#ifndef RPC_E_FAULT
#define RPC_E_FAULT ((HRESULT)0x80010104L)
#endif
#ifndef RPC_E_SERVERFAULT
#define RPC_E_SERVERFAULT ((HRESULT)0x80010105L)
#endif
#ifndef RPC_E_CHANGED_MODE
#define RPC_E_CHANGED_MODE ((HRESULT)0x80010106L)
#endif
#ifndef RPC_E_INVALIDMETHOD
#define RPC_E_INVALIDMETHOD ((HRESULT)0x80010107L)
#endif
#ifndef RPC_E_DISCONNECTED
#define RPC_E_DISCONNECTED ((HRESULT)0x80010108L)
#endif
#ifndef RPC_E_RETRY
#define RPC_E_RETRY ((HRESULT)0x80010109L)
#endif
#ifndef RPC_E_SERVERCALL_RETRYLATER
#define RPC_E_SERVERCALL_RETRYLATER ((HRESULT)0x8001010AL)
#endif
#ifndef RPC_E_SERVERCALL_REJECTED
#define RPC_E_SERVERCALL_REJECTED ((HRESULT)0x8001010BL)
#endif
#ifndef RPC_E_INVALID_CALLDATA
#define RPC_E_INVALID_CALLDATA ((HRESULT)0x8001010CL)
#endif
#ifndef RPC_E_CANTCALLOUT_ININPUTSYNCCALL
#define RPC_E_CANTCALLOUT_ININPUTSYNCCALL ((HRESULT)0x8001010DL)
#endif
#ifndef RPC_E_WRONG_THREAD
#define RPC_E_WRONG_THREAD ((HRESULT)0x8001010EL)
#endif
#ifndef RPC_E_THREAD_NOT_INIT
#define RPC_E_THREAD_NOT_INIT ((HRESULT)0x8001010FL)
#endif
#ifndef RPC_E_VERSION_MISMATCH
#define RPC_E_VERSION_MISMATCH ((HRESULT)0x80010110L)
#endif
#ifndef RPC_E_INVALID_HEADER
#define RPC_E_INVALID_HEADER ((HRESULT)0x80010111L)
#endif
#ifndef RPC_E_INVALID_EXTENSION
#define RPC_E_INVALID_EXTENSION ((HRESULT)0x80010112L)
#endif
#ifndef RPC_E_INVALID_IPID
#define RPC_E_INVALID_IPID ((HRESULT)0x80010113L)
#endif
#ifndef RPC_E_INVALID_OBJECT
#define RPC_E_INVALID_OBJECT ((HRESULT)0x80010114L)
#endif
#ifndef RPC_S_CALLPENDING
#define RPC_S_CALLPENDING ((HRESULT)0x80010115L)
#endif
#ifndef RPC_S_WAITONTIMER
#define RPC_S_WAITONTIMER ((HRESULT)0x80010116L)
#endif
#ifndef RPC_E_CALL_COMPLETE
#define RPC_E_CALL_COMPLETE ((HRESULT)0x80010117L)
#endif
#ifndef RPC_E_UNSECURE_CALL
#define RPC_E_UNSECURE_CALL ((HRESULT)0x80010118L)
#endif
#ifndef RPC_E_TOO_LATE
#define RPC_E_TOO_LATE ((HRESULT)0x80010119L)
#endif
#ifndef RPC_E_NO_GOOD_SECURITY_PACKAGES
#define RPC_E_NO_GOOD_SECURITY_PACKAGES ((HRESULT)0x8001011AL)
#endif
#ifndef RPC_E_ACCESS_DENIED
#define RPC_E_ACCESS_DENIED ((HRESULT)0x8001011BL)
#endif
#ifndef RPC_E_REMOTE_DISABLED
#define RPC_E_REMOTE_DISABLED ((HRESULT)0x8001011CL)
#endif
#ifndef RPC_E_INVALID_OBJREF
#define RPC_E_INVALID_OBJREF ((HRESULT)0x8001011DL)
#endif
#ifndef RPC_E_NO_CONTEXT
#define RPC_E_NO_CONTEXT ((HRESULT)0x8001011EL)
#endif
#ifndef RPC_E_TIMEOUT
#define RPC_E_TIMEOUT ((HRESULT)0x8001011FL)
#endif
#ifndef RPC_E_NO_SYNC
#define RPC_E_NO_SYNC ((HRESULT)0x80010120L)
#endif
#ifndef RPC_E_FULLSIC_REQUIRED
#define RPC_E_FULLSIC_REQUIRED ((HRESULT)0x80010121L)
#endif
#ifndef RPC_E_INVALID_STD_NAME
#define RPC_E_INVALID_STD_NAME ((HRESULT)0x80010122L)
#endif
#ifndef RPC_E_UNEXPECTED
#define RPC_E_UNEXPECTED ((HRESULT)0x8001FFFFL)
#endif

/* ------------------------------------------------------------------ interfaces and messages */

typedef struct _RPC_VERSION {
    unsigned short MajorVersion;
    unsigned short MinorVersion;
} RPC_VERSION;

typedef struct _RPC_SYNTAX_IDENTIFIER {
    GUID SyntaxGUID;
    RPC_VERSION SyntaxVersion;
} RPC_SYNTAX_IDENTIFIER, *PRPC_SYNTAX_IDENTIFIER;

typedef struct _RPC_IF_ID {
    UUID Uuid;
    unsigned short VersMajor;
    unsigned short VersMinor;
} RPC_IF_ID;

#define RPC_MGR_EPV void

typedef struct _RPC_MESSAGE {
    RPC_BINDING_HANDLE Handle;
    ULONG DataRepresentation;
    void *Buffer;
    unsigned int BufferLength;
    unsigned int ProcNum;
    PRPC_SYNTAX_IDENTIFIER TransferSyntax;
    void *RpcInterfaceInformation;
    void *ReservedForRuntime;
    RPC_MGR_EPV *ManagerEpv;
    void *ImportContext;
    ULONG RpcFlags;
} RPC_MESSAGE, *PRPC_MESSAGE;

typedef void(__RPC_STUB *RPC_DISPATCH_FUNCTION)(PRPC_MESSAGE message);

typedef struct {
    unsigned int DispatchTableCount;
    RPC_DISPATCH_FUNCTION *DispatchTable;
    LONG_PTR Reserved;
} RPC_DISPATCH_TABLE, *PRPC_DISPATCH_TABLE;

typedef struct _RPC_PROTSEQ_ENDPOINT {
    unsigned char *RpcProtocolSequence;
    unsigned char *Endpoint;
} RPC_PROTSEQ_ENDPOINT, *PRPC_PROTSEQ_ENDPOINT;

#define NT351_INTERFACE_SIZE 0x40
#define RPC_INTERFACE_HAS_PIPES 0x0001

typedef struct _RPC_SERVER_INTERFACE {
    unsigned int Length;
    RPC_SYNTAX_IDENTIFIER InterfaceId;
    RPC_SYNTAX_IDENTIFIER TransferSyntax;
    PRPC_DISPATCH_TABLE DispatchTable;
    unsigned int RpcProtseqEndpointCount;
    PRPC_PROTSEQ_ENDPOINT RpcProtseqEndpoint;
    RPC_MGR_EPV *DefaultManagerEpv;
    void const *InterpreterInfo;
    unsigned int Flags;
} RPC_SERVER_INTERFACE, *PRPC_SERVER_INTERFACE;

typedef struct _RPC_CLIENT_INTERFACE {
    unsigned int Length;
    RPC_SYNTAX_IDENTIFIER InterfaceId;
    RPC_SYNTAX_IDENTIFIER TransferSyntax;
    PRPC_DISPATCH_TABLE DispatchTable;
    unsigned int RpcProtseqEndpointCount;
    PRPC_PROTSEQ_ENDPOINT RpcProtseqEndpoint;
    ULONG_PTR Reserved;
    void const *InterpreterInfo;
    unsigned int Flags;
} RPC_CLIENT_INTERFACE, *PRPC_CLIENT_INTERFACE;

#define RPC_NCA_FLAGS_DEFAULT 0x00000000
#define RPC_NCA_FLAGS_IDEMPOTENT 0x00000001
#define RPC_NCA_FLAGS_BROADCAST 0x00000002
#define RPC_NCA_FLAGS_MAYBE 0x00000004
#define RPC_BUFFER_COMPLETE 0x00001000
#define RPC_BUFFER_PARTIAL 0x00002000
#define RPC_BUFFER_EXTRA 0x00004000
#define RPC_BUFFER_ASYNC 0x00008000
#define RPC_BUFFER_NONOTIFY 0x00010000
#define RPCFLG_MESSAGE 0x01000000u
#define RPCFLG_AUTO_COMPLETE 0x08000000u
#define RPCFLG_LOCAL_CALL 0x10000000u
#define RPCFLG_INPUT_SYNCHRONOUS 0x20000000u
#define RPCFLG_ASYNCHRONOUS 0x40000000u
#define RPCFLG_NON_NDR 0x80000000u
#define RPCFLG_HAS_MULTI_SYNTAXES 0x02000000u
#define RPCFLG_HAS_CALLBACK 0x04000000u

#define RPC_CONTEXT_HANDLE_DEFAULT_GUARD ((void *)-4083)
#define RPC_CONTEXT_HANDLE_DEFAULT_FLAGS 0x00000000u
#define RPC_CONTEXT_HANDLE_FLAGS 0x30000000u
#define RPC_CONTEXT_HANDLE_SERIALIZE 0x10000000u
#define RPC_CONTEXT_HANDLE_DONT_SERIALIZE 0x20000000u

/* ------------------------------------------------------------------ vectors and options */

typedef struct _RPC_BINDING_VECTOR {
    ULONG Count;
    RPC_BINDING_HANDLE BindingH[1];
} RPC_BINDING_VECTOR;

typedef struct _UUID_VECTOR {
    ULONG Count;
    UUID *Uuid[1];
} UUID_VECTOR;

typedef struct _RPC_PROTSEQ_VECTORA {
    unsigned int Count;
    unsigned char *Protseq[1];
} RPC_PROTSEQ_VECTORA;

typedef struct _RPC_PROTSEQ_VECTORW {
    unsigned int Count;
    unsigned short *Protseq[1];
} RPC_PROTSEQ_VECTORW;

typedef struct _RPC_POLICY {
    unsigned int Length;
    ULONG EndpointFlags;
    ULONG NICFlags;
} RPC_POLICY, *PRPC_POLICY;

typedef struct {
    unsigned int Count;
    ULONG Stats[1];
} RPC_STATS_VECTOR;

typedef struct {
    ULONG Count;
    RPC_IF_ID *IfId[1];
} RPC_IF_ID_VECTOR;

#define RPC_C_STATS_CALLS_IN 0
#define RPC_C_STATS_CALLS_OUT 1
#define RPC_C_STATS_PKTS_IN 2
#define RPC_C_STATS_PKTS_OUT 3

#define RPC_C_BINDING_INFINITE_TIMEOUT 10
#define RPC_C_BINDING_MIN_TIMEOUT 0
#define RPC_C_BINDING_DEFAULT_TIMEOUT 5
#define RPC_C_BINDING_MAX_TIMEOUT 9
#define RPC_C_CANCEL_INFINITE_TIMEOUT (-1)
#define RPC_C_LISTEN_MAX_CALLS_DEFAULT 1234
#define RPC_C_PROTSEQ_MAX_REQS_DEFAULT 10
#define RPC_C_BIND_TO_ALL_NICS 1
#define RPC_C_USE_INTERNET_PORT 0x1
#define RPC_C_USE_INTRANET_PORT 0x2
#define RPC_C_DONT_FAIL 0x4

#define RPC_C_OPT_BINDING_NONCAUSAL 9
#define RPC_C_OPT_SECURITY_CALLBACK 10
#define RPC_C_OPT_UNIQUE_BINDING 11
#define RPC_C_OPT_CALL_TIMEOUT 12
#define RPC_C_OPT_DONT_LINGER 13
#define RPC_C_OPT_MAX_OPTIONS 14

#define RPC_C_EP_ALL_ELTS 0
#define RPC_C_EP_MATCH_BY_IF 1
#define RPC_C_EP_MATCH_BY_OBJ 2
#define RPC_C_EP_MATCH_BY_BOTH 3
#define RPC_C_VERS_ALL 1
#define RPC_C_VERS_COMPATIBLE 2
#define RPC_C_VERS_EXACT 3
#define RPC_C_VERS_MAJOR_ONLY 4
#define RPC_C_VERS_UPTO 5

#define RPC_C_MGMT_INQ_IF_IDS 0
#define RPC_C_MGMT_INQ_PRINC_NAME 1
#define RPC_C_MGMT_INQ_STATS 2
#define RPC_C_MGMT_IS_SERVER_LISTEN 3
#define RPC_C_MGMT_STOP_SERVER_LISTEN 4

#define RPC_C_PARM_MAX_PACKET_LENGTH 1
#define RPC_C_PARM_BUFFER_LENGTH 2

#define RPC_IF_AUTOLISTEN 0x0001
#define RPC_IF_OLE 0x0002
#define RPC_IF_ALLOW_UNKNOWN_AUTHORITY 0x0004
#define RPC_IF_ALLOW_SECURE_ONLY 0x0008
#define RPC_IF_ALLOW_CALLBACKS_WITH_NO_AUTH 0x0010
#define RPC_IF_ALLOW_LOCAL_ONLY 0x0020
#define RPC_IF_SEC_NO_CACHE 0x0040
#define RPC_IF_SEC_CACHE_PER_PROC 0x0080
#define RPC_IF_ASYNC_CALLBACK 0x0100

/* ------------------------------------------------------------------ security */

#define RPC_C_AUTHN_LEVEL_DEFAULT 0
#define RPC_C_AUTHN_LEVEL_NONE 1
#define RPC_C_AUTHN_LEVEL_CONNECT 2
#define RPC_C_AUTHN_LEVEL_CALL 3
#define RPC_C_AUTHN_LEVEL_PKT 4
#define RPC_C_AUTHN_LEVEL_PKT_INTEGRITY 5
#define RPC_C_AUTHN_LEVEL_PKT_PRIVACY 6

#define RPC_C_IMP_LEVEL_DEFAULT 0
#define RPC_C_IMP_LEVEL_ANONYMOUS 1
#define RPC_C_IMP_LEVEL_IDENTIFY 2
#define RPC_C_IMP_LEVEL_IMPERSONATE 3
#define RPC_C_IMP_LEVEL_DELEGATE 4

#define RPC_C_QOS_IDENTITY_STATIC 0
#define RPC_C_QOS_IDENTITY_DYNAMIC 1

#define RPC_C_AUTHN_NONE 0
#define RPC_C_AUTHN_DCE_PRIVATE 1
#define RPC_C_AUTHN_DCE_PUBLIC 2
#define RPC_C_AUTHN_DEC_PUBLIC 4
#define RPC_C_AUTHN_GSS_NEGOTIATE 9
#define RPC_C_AUTHN_WINNT 10
#define RPC_C_AUTHN_GSS_SCHANNEL 14
#define RPC_C_AUTHN_GSS_KERBEROS 16
#define RPC_C_AUTHN_DPA 17
#define RPC_C_AUTHN_MSN 18
#define RPC_C_AUTHN_DIGEST 21
#define RPC_C_AUTHN_MQ 100
#define RPC_C_AUTHN_DEFAULT 0xFFFFFFFFu

#define RPC_C_AUTHZ_NONE 0
#define RPC_C_AUTHZ_NAME 1
#define RPC_C_AUTHZ_DCE 2
#define RPC_C_AUTHZ_DEFAULT 0xFFFFFFFFu

#define RPC_C_NO_CREDENTIALS ((RPC_AUTH_IDENTITY_HANDLE)(ULONG_PTR)-1)

#define RPC_C_SECURITY_QOS_VERSION 1
#define RPC_C_SECURITY_QOS_VERSION_1 1
#define RPC_C_SECURITY_QOS_VERSION_2 2
#define RPC_C_SECURITY_QOS_VERSION_3 3

typedef struct _RPC_SECURITY_QOS {
    ULONG Version;
    ULONG Capabilities;
    ULONG IdentityTracking;
    ULONG ImpersonationType;
} RPC_SECURITY_QOS, *PRPC_SECURITY_QOS;

typedef struct _SEC_WINNT_AUTH_IDENTITY_W {
    unsigned short *User;
    ULONG UserLength;
    unsigned short *Domain;
    ULONG DomainLength;
    unsigned short *Password;
    ULONG PasswordLength;
    ULONG Flags;
} SEC_WINNT_AUTH_IDENTITY_W, *PSEC_WINNT_AUTH_IDENTITY_W;

typedef void(__RPC_API *RPC_AUTH_KEY_RETRIEVAL_FN)(void *arg, unsigned short *principal, ULONG version, void **key,
                                                   RPC_STATUS *status);
typedef RPC_STATUS(RPC_ENTRY RPC_IF_CALLBACK_FN)(RPC_IF_HANDLE interface_uuid, void *context);
typedef void(RPC_ENTRY RPC_SECURITY_CALLBACK_FN)(void *context);
typedef void(__RPC_API RPC_OBJECT_INQ_FN)(UUID *object, UUID *type, RPC_STATUS *status);
typedef int(__RPC_API *RPC_MGMT_AUTHORIZATION_FN)(RPC_BINDING_HANDLE client, ULONG operation, RPC_STATUS *status);

#define RPC_CALL_ATTRIBUTES_VERSION 1
#define RPC_QUERY_SERVER_PRINCIPAL_NAME 2
#define RPC_QUERY_CLIENT_PRINCIPAL_NAME 4
#define RPC_QUERY_CALL_LOCAL_ADDRESS 8
#define RPC_QUERY_CLIENT_PID 16
#define RPC_QUERY_IS_CLIENT_LOCAL 32
#define RPC_QUERY_NO_AUTH_REQUIRED 64

typedef struct tagRPC_CALL_ATTRIBUTES_V1_W {
    unsigned int Version;
    ULONG Flags;
    ULONG ServerPrincipalNameBufferLength;
    unsigned short *ServerPrincipalName;
    ULONG ClientPrincipalNameBufferLength;
    unsigned short *ClientPrincipalName;
    ULONG AuthenticationLevel;
    ULONG AuthenticationService;
    BOOL NullSession;
} RPC_CALL_ATTRIBUTES_V1_W;

typedef struct tagRPC_CALL_ATTRIBUTES_V1_A {
    unsigned int Version;
    ULONG Flags;
    ULONG ServerPrincipalNameBufferLength;
    unsigned char *ServerPrincipalName;
    ULONG ClientPrincipalNameBufferLength;
    unsigned char *ClientPrincipalName;
    ULONG AuthenticationLevel;
    ULONG AuthenticationService;
    BOOL NullSession;
} RPC_CALL_ATTRIBUTES_V1_A;

typedef struct tagRPC_CALL_ATTRIBUTES_V2_W {
    unsigned int Version;
    ULONG Flags;
    ULONG ServerPrincipalNameBufferLength;
    unsigned short *ServerPrincipalName;
    ULONG ClientPrincipalNameBufferLength;
    unsigned short *ClientPrincipalName;
    ULONG AuthenticationLevel;
    ULONG AuthenticationService;
    BOOL NullSession;
    BOOL KernelModeCaller;
    ULONG ProtocolSequence;
    ULONG IsClientLocal;
    HANDLE ClientPID;
    ULONG CallStatus;
    int CallType;
    void *CallLocalAddress;
    unsigned short OpNum;
    UUID InterfaceUuid;
} RPC_CALL_ATTRIBUTES_V2_W;

#define RPC_CALL_STATUS_IN_PROGRESS 0x01
#define RPC_CALL_STATUS_CANCELLED 0x02
#define RPC_CALL_STATUS_DISCONNECTED 0x03

/* ------------------------------------------------------------------ async calls */

typedef enum _RPC_NOTIFICATION_TYPES {
    RpcNotificationTypeNone,
    RpcNotificationTypeEvent,
    RpcNotificationTypeApc,
    RpcNotificationTypeIoc,
    RpcNotificationTypeHwnd,
    RpcNotificationTypeCallback
} RPC_NOTIFICATION_TYPES;

typedef enum _RPC_ASYNC_EVENT { RpcCallComplete, RpcSendComplete, RpcReceiveComplete } RPC_ASYNC_EVENT;

struct _RPC_ASYNC_STATE;
typedef void(RPC_ENTRY RPCNOTIFICATION_ROUTINE)(struct _RPC_ASYNC_STATE *async, void *context, RPC_ASYNC_EVENT event);
typedef RPCNOTIFICATION_ROUTINE *PFN_RPCNOTIFICATION_ROUTINE;

typedef struct _RPC_ASYNC_STATE {
    unsigned int Size;
    ULONG Signature;
    LONG Lock;
    ULONG Flags;
    void *StubInfo;
    void *UserInfo;
    void *RuntimeInfo;
    RPC_ASYNC_EVENT Event;
    RPC_NOTIFICATION_TYPES NotificationType;
    union {
        struct {
            PFN_RPCNOTIFICATION_ROUTINE NotificationRoutine;
            HANDLE hThread;
        } APC;
        struct {
            HANDLE hIOPort;
            DWORD dwNumberOfBytesTransferred;
            DWORD_PTR dwCompletionKey;
            LPOVERLAPPED lpOverlapped;
        } IOC;
        struct {
            HWND hWnd;
            UINT Msg;
        } HWND;
        HANDLE hEvent;
        PFN_RPCNOTIFICATION_ROUTINE NotificationRoutine;
    } u;
    LONG_PTR Reserved[4];
} RPC_ASYNC_STATE, *PRPC_ASYNC_STATE;

#define RPC_ASYNC_VERSION_1_0 sizeof(RPC_ASYNC_STATE)
#define RPC_C_NOTIFY_ON_SEND_COMPLETE 0x1
#define RPC_C_INFINITE_TIMEOUT INFINITE

/* ------------------------------------------------------------------ extended error information */

typedef enum tagExtendedErrorParamTypes {
    eeptAnsiString = 1,
    eeptUnicodeString,
    eeptLongVal,
    eeptShortVal,
    eeptPointerVal,
    eeptNone,
    eeptBinary
} ExtendedErrorParamTypes;

#define MaxNumberOfEEInfoParams 4
#define RPC_EEINFO_VERSION 1

typedef struct tagBinaryParam {
    void *Buffer;
    short Size;
} BinaryParam;

typedef struct tagRPC_EE_INFO_PARAM {
    ExtendedErrorParamTypes ParameterType;
    union {
        LPSTR AnsiString;
        LPWSTR UnicodeString;
        LONG LVal;
        short SVal;
        ULONGLONG PVal;
        BinaryParam BVal;
    } u;
} RPC_EE_INFO_PARAM;

typedef struct tagRPC_EXTENDED_ERROR_INFO {
    ULONG Version;
    LPWSTR ComputerName;
    ULONG ProcessID;
    union {
        SYSTEMTIME SystemTime;
        FILETIME FileTime;
    } u;
    ULONG GeneratingComponent;
    ULONG Status;
    USHORT DetectionLocation;
    USHORT Flags;
    int NumberOfParameters;
    RPC_EE_INFO_PARAM Parameters[MaxNumberOfEEInfoParams];
} RPC_EXTENDED_ERROR_INFO;

typedef struct tagRPC_ERROR_ENUM_HANDLE {
    ULONG Signature;
    void *CurrentPos;
    void *Head;
} RPC_ERROR_ENUM_HANDLE;

typedef I_RPC_HANDLE *RPC_EP_INQ_HANDLE;

/* ------------------------------------------------------------------ uuids */

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

/* ------------------------------------------------------------------ string bindings and handles */

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
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingReset(RPC_BINDING_HANDLE handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqObject(RPC_BINDING_HANDLE handle, UUID *object);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetObject(RPC_BINDING_HANDLE handle, UUID *object);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingVectorFree(RPC_BINDING_VECTOR **vector);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetOption(RPC_BINDING_HANDLE handle, ULONG option, ULONG_PTR value);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqOption(RPC_BINDING_HANDLE handle, ULONG option, ULONG_PTR *value);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingServerFromClient(RPC_BINDING_HANDLE client, RPC_BINDING_HANDLE *server);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcSsGetContextBinding(void *context, RPC_BINDING_HANDLE *binding);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcIfInqId(RPC_IF_HANDLE interface_handle, RPC_IF_ID *id);

RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoW(RPC_BINDING_HANDLE handle, RPC_WSTR principal, ULONG level,
                                                     ULONG service, void *identity, ULONG authz);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoA(RPC_BINDING_HANDLE handle, RPC_CSTR principal, ULONG level,
                                                     ULONG service, void *identity, ULONG authz);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoExW(RPC_BINDING_HANDLE handle, RPC_WSTR principal, ULONG level,
                                                       ULONG service, void *identity, ULONG authz,
                                                       RPC_SECURITY_QOS *qos);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingSetAuthInfoExA(RPC_BINDING_HANDLE handle, RPC_CSTR principal, ULONG level,
                                                       ULONG service, void *identity, ULONG authz,
                                                       RPC_SECURITY_QOS *qos);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthInfoExW(RPC_BINDING_HANDLE handle, RPC_WSTR *principal, ULONG *level,
                                                       ULONG *service, void **identity, ULONG *authz, ULONG qos_version,
                                                       RPC_SECURITY_QOS *qos);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthInfoExA(RPC_BINDING_HANDLE handle, RPC_CSTR *principal, ULONG *level,
                                                       ULONG *service, void **identity, ULONG *authz, ULONG qos_version,
                                                       RPC_SECURITY_QOS *qos);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthInfoW(RPC_BINDING_HANDLE handle, RPC_WSTR *principal, ULONG *level,
                                                     ULONG *service, void **identity, ULONG *authz);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthInfoA(RPC_BINDING_HANDLE handle, RPC_CSTR *principal, ULONG *level,
                                                     ULONG *service, void **identity, ULONG *authz);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthClientW(RPC_BINDING_HANDLE client, RPC_AUTHZ_HANDLE *privs,
                                                       RPC_WSTR *principal, ULONG *level, ULONG *service, ULONG *authz);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthClientA(RPC_BINDING_HANDLE client, RPC_AUTHZ_HANDLE *privs,
                                                       RPC_CSTR *principal, ULONG *level, ULONG *service, ULONG *authz);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthClientExW(RPC_BINDING_HANDLE client, RPC_AUTHZ_HANDLE *privs,
                                                         RPC_WSTR *principal, ULONG *level, ULONG *service,
                                                         ULONG *authz, ULONG flags);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcBindingInqAuthClientExA(RPC_BINDING_HANDLE client, RPC_AUTHZ_HANDLE *privs,
                                                         RPC_CSTR *principal, ULONG *level, ULONG *service,
                                                         ULONG *authz, ULONG flags);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcImpersonateClient(RPC_BINDING_HANDLE handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcRevertToSelfEx(RPC_BINDING_HANDLE handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcRevertToSelf(void);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqDefaultProtectLevel(ULONG service, ULONG *level);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterAuthInfoW(RPC_WSTR principal, ULONG service,
                                                         RPC_AUTH_KEY_RETRIEVAL_FN get_key, void *arg);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterAuthInfoA(RPC_CSTR principal, ULONG service,
                                                         RPC_AUTH_KEY_RETRIEVAL_FN get_key, void *arg);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqDefaultPrincNameW(ULONG service, RPC_WSTR *principal);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqDefaultPrincNameA(ULONG service, RPC_CSTR *principal);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqCallAttributesW(RPC_BINDING_HANDLE client, void *attributes);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqCallAttributesA(RPC_BINDING_HANDLE client, void *attributes);

/* ------------------------------------------------------------------ server */

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqW(RPC_WSTR protseq, unsigned int max_calls, void *sd);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqA(RPC_CSTR protseq, unsigned int max_calls, void *sd);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqExW(RPC_WSTR protseq, unsigned int max_calls, void *sd,
                                                     PRPC_POLICY policy);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqExA(RPC_CSTR protseq, unsigned int max_calls, void *sd,
                                                     PRPC_POLICY policy);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqEpW(RPC_WSTR protseq, unsigned int max_calls, RPC_WSTR endpoint,
                                                     void *sd);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqEpA(RPC_CSTR protseq, unsigned int max_calls, RPC_CSTR endpoint,
                                                     void *sd);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqEpExW(RPC_WSTR protseq, unsigned int max_calls, RPC_WSTR endpoint,
                                                       void *sd, PRPC_POLICY policy);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqEpExA(RPC_CSTR protseq, unsigned int max_calls, RPC_CSTR endpoint,
                                                       void *sd, PRPC_POLICY policy);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqIfW(RPC_WSTR protseq, unsigned int max_calls, RPC_IF_HANDLE interface_spec,
                                                     void *sd);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqIfA(RPC_CSTR protseq, unsigned int max_calls, RPC_IF_HANDLE interface_spec,
                                                     void *sd);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqIfExW(RPC_WSTR protseq, unsigned int max_calls,
                                                       RPC_IF_HANDLE interface_spec, void *sd, PRPC_POLICY policy);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseProtseqIfExA(RPC_CSTR protseq, unsigned int max_calls,
                                                       RPC_IF_HANDLE interface_spec, void *sd, PRPC_POLICY policy);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseAllProtseqs(unsigned int max_calls, void *sd);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseAllProtseqsEx(unsigned int max_calls, void *sd, PRPC_POLICY policy);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseAllProtseqsIf(unsigned int max_calls, RPC_IF_HANDLE interface_spec, void *sd);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUseAllProtseqsIfEx(unsigned int max_calls, RPC_IF_HANDLE interface_spec,
                                                          void *sd, PRPC_POLICY policy);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterIf(RPC_IF_HANDLE interface_spec, UUID *type, RPC_MGR_EPV *epv);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterIfEx(RPC_IF_HANDLE interface_spec, UUID *type, RPC_MGR_EPV *epv,
                                                    unsigned int flags, unsigned int max_calls,
                                                    RPC_IF_CALLBACK_FN *callback);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterIf2(RPC_IF_HANDLE interface_spec, UUID *type, RPC_MGR_EPV *epv,
                                                   unsigned int flags, unsigned int max_calls, unsigned int max_rpc_size,
                                                   RPC_IF_CALLBACK_FN *callback);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerRegisterIf3(RPC_IF_HANDLE interface_spec, UUID *type, RPC_MGR_EPV *epv,
                                                   unsigned int flags, unsigned int max_calls, unsigned int max_rpc_size,
                                                   RPC_IF_CALLBACK_FN *callback, void *sd);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUnregisterIf(RPC_IF_HANDLE interface_spec, UUID *type,
                                                    unsigned int wait_for_calls);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUnregisterIfEx(RPC_IF_HANDLE interface_spec, UUID *type, int rundown_contexts);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqIf(RPC_IF_HANDLE interface_spec, UUID *type, RPC_MGR_EPV **epv);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqBindings(RPC_BINDING_VECTOR **vector);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerInqBindingHandle(RPC_BINDING_HANDLE *binding);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerListen(unsigned int min_threads, unsigned int max_calls, unsigned int dont_wait);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtWaitServerListen(void);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtStopServerListening(RPC_BINDING_HANDLE binding);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtIsServerListening(RPC_BINDING_HANDLE binding);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqIfIds(RPC_BINDING_HANDLE binding, RPC_IF_ID_VECTOR **vector);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcIfIdVectorFree(RPC_IF_ID_VECTOR **vector);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqStats(RPC_BINDING_HANDLE binding, RPC_STATS_VECTOR **stats);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtStatsVectorFree(RPC_STATS_VECTOR **stats);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtSetServerStackSize(ULONG size);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEnableIdleCleanup(void);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtSetAuthorizationFn(RPC_MGMT_AUTHORIZATION_FN function);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqComTimeout(RPC_BINDING_HANDLE binding, unsigned int *timeout);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtSetComTimeout(RPC_BINDING_HANDLE binding, unsigned int timeout);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtSetCancelTimeout(LONG timeout);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqServerPrincNameW(RPC_BINDING_HANDLE binding, ULONG service, RPC_WSTR *principal);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtInqServerPrincNameA(RPC_BINDING_HANDLE binding, ULONG service, RPC_CSTR *principal);
RPCRTAPI void RPC_ENTRY RpcServerYield(void);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcObjectInqType(UUID *object, UUID *type);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcObjectSetInqFn(RPC_OBJECT_INQ_FN *function);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcObjectSetType(UUID *object, UUID *type);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcNetworkIsProtseqValidW(RPC_WSTR protseq);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcNetworkIsProtseqValidA(RPC_CSTR protseq);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcNetworkInqProtseqsW(RPC_PROTSEQ_VECTORW **vector);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcNetworkInqProtseqsA(RPC_PROTSEQ_VECTORA **vector);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcProtseqVectorFreeW(RPC_PROTSEQ_VECTORW **vector);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcProtseqVectorFreeA(RPC_PROTSEQ_VECTORA **vector);

/* ------------------------------------------------------------------ endpoint mapper */

RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpRegisterW(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings,
                                             UUID_VECTOR *objects, RPC_WSTR annotation);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpRegisterA(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings,
                                             UUID_VECTOR *objects, RPC_CSTR annotation);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpRegisterNoReplaceW(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings,
                                                      UUID_VECTOR *objects, RPC_WSTR annotation);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpRegisterNoReplaceA(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings,
                                                      UUID_VECTOR *objects, RPC_CSTR annotation);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpUnregister(RPC_IF_HANDLE interface_spec, RPC_BINDING_VECTOR *bindings,
                                              UUID_VECTOR *objects);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcEpResolveBinding(RPC_BINDING_HANDLE binding, RPC_IF_HANDLE interface_spec);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEpEltInqBegin(RPC_BINDING_HANDLE ep_binding, ULONG inquiry_type, RPC_IF_ID *id,
                                                   ULONG version_option, UUID *object, RPC_EP_INQ_HANDLE *context);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEpEltInqDone(RPC_EP_INQ_HANDLE *context);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEpEltInqNextW(RPC_EP_INQ_HANDLE context, RPC_IF_ID *id, RPC_BINDING_HANDLE *binding,
                                                   UUID *object, RPC_WSTR *annotation);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEpEltInqNextA(RPC_EP_INQ_HANDLE context, RPC_IF_ID *id, RPC_BINDING_HANDLE *binding,
                                                   UUID *object, RPC_CSTR *annotation);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcMgmtEpUnregister(RPC_BINDING_HANDLE ep_binding, RPC_IF_ID *id,
                                                  RPC_BINDING_HANDLE binding, UUID *object);

/* ------------------------------------------------------------------ statuses and cancellation */

RPCRTAPI int RPC_ENTRY I_RpcExceptionFilter(ULONG code);
RPCRTAPI int RPC_ENTRY RpcExceptionFilter(ULONG code);
RPCRTAPI DECLSPEC_NORETURN void RPC_ENTRY RpcRaiseException(RPC_STATUS status);
RPCRTAPI LONG RPC_ENTRY I_RpcMapWin32Status(RPC_STATUS status);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcTestCancel(void);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerTestCancel(RPC_BINDING_HANDLE binding);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcCancelThread(void *thread);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcCancelThreadEx(void *thread, LONG timeout);
RPCRTAPI RPC_STATUS RPC_ENTRY DceErrorInqTextW(RPC_STATUS status, RPC_WSTR text);
RPCRTAPI RPC_STATUS RPC_ENTRY DceErrorInqTextA(RPC_STATUS status, RPC_CSTR text);
#define DCE_C_ERROR_STRING_LEN 256

RPCRTAPI RPC_STATUS RPC_ENTRY RpcAsyncInitializeHandle(PRPC_ASYNC_STATE async, unsigned int size);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcAsyncRegisterInfo(PRPC_ASYNC_STATE async);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcAsyncGetCallStatus(PRPC_ASYNC_STATE async);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcAsyncCompleteCall(PRPC_ASYNC_STATE async, void *reply);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcAsyncAbortCall(PRPC_ASYNC_STATE async, ULONG code);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcAsyncCancelCall(PRPC_ASYNC_STATE async, BOOL abort);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcAsyncCleanupThread(DWORD timeout);

/* ------------------------------------------------------------------ stub runtime */

RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcNegotiateTransferSyntax(RPC_MESSAGE *message);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcGetBuffer(RPC_MESSAGE *message);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcGetBufferWithObject(RPC_MESSAGE *message, UUID *object);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcSendReceive(RPC_MESSAGE *message);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcFreeBuffer(RPC_MESSAGE *message);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcSend(PRPC_MESSAGE message);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcReceive(PRPC_MESSAGE message, unsigned int size);
RPCRTAPI void *RPC_ENTRY I_RpcAllocate(unsigned int size);
RPCRTAPI void RPC_ENTRY I_RpcFree(void *object);
RPCRTAPI void RPC_ENTRY I_RpcPauseExecution(ULONG milliseconds);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcGetExtendedError(void);
RPCRTAPI RPC_BINDING_HANDLE RPC_ENTRY I_RpcGetCurrentCallHandle(void);
/* mime only: the wait function a thread uses for replies, so it keeps serving its windows meanwhile. */
RPCRTAPI void RPC_ENTRY I_RpcMimeSetWaitFunction(DWORD(WINAPI *function)(DWORD, const HANDLE *, DWORD, void *),
                                                 void *context, void *previous[2]);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcAsyncSetHandle(PRPC_MESSAGE message, PRPC_ASYNC_STATE async);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcAsyncAbortCall(PRPC_ASYNC_STATE async, ULONG code);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcOpenClientProcess(void *binding, ULONG access, void **process);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcBindingInqLocalClientPID(RPC_BINDING_HANDLE binding, ULONG *pid);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcBindingIsClientLocal(RPC_BINDING_HANDLE binding, unsigned int *local);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcBindingIsServerLocal(RPC_BINDING_HANDLE binding, unsigned int *local);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcBindingInqTransportType(RPC_BINDING_HANDLE binding, unsigned int *type);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcServerInqTransportType(unsigned int *type);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcIfInqTransferSyntaxes(RPC_IF_HANDLE handle, void *syntaxes, unsigned int size,
                                                         unsigned int *count);
RPCRTAPI RPC_STATUS RPC_ENTRY I_UuidCreate(UUID *uuid);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcBindingCopy(RPC_BINDING_HANDLE source, RPC_BINDING_HANDLE *destination);
RPCRTAPI void RPC_ENTRY I_RpcSsDontSerializeContext(void);
RPCRTAPI void RPC_ENTRY I_RpcSessionStrictContextHandle(void);
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcTurnOnEEInfoPropagation(void);

#define TRANSPORT_TYPE_CN 0x01
#define TRANSPORT_TYPE_DG 0x02
#define TRANSPORT_TYPE_LPC 0x04
#define TRANSPORT_TYPE_WMSG 0x08

RPCRTAPI RPC_STATUS RPC_ENTRY RpcErrorStartEnumeration(RPC_ERROR_ENUM_HANDLE *handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcErrorGetNextRecord(RPC_ERROR_ENUM_HANDLE *handle, BOOL copy_strings,
                                                    RPC_EXTENDED_ERROR_INFO *info);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcErrorEndEnumeration(RPC_ERROR_ENUM_HANDLE *handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcErrorResetEnumeration(RPC_ERROR_ENUM_HANDLE *handle);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcErrorGetNumberOfRecords(RPC_ERROR_ENUM_HANDLE *handle, int *records);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcErrorAddRecord(RPC_EXTENDED_ERROR_INFO *info);
RPCRTAPI void RPC_ENTRY RpcErrorClearInformation(void);

#endif
