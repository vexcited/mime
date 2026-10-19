/*
 * the format characters of the ndr engine: the bytes that midl writes into the type and procedure format strings, and the
 * flag layouts of the procedure headers.
 */
#ifndef MIME_WIN32_NDRTYPES_H
#define MIME_WIN32_NDRTYPES_H

#include <mime/win32/winbase.h>

#define FC_ZERO 0x00
#define FC_BYTE 0x01
#define FC_CHAR 0x02
#define FC_SMALL 0x03
#define FC_USMALL 0x04
#define FC_WCHAR 0x05
#define FC_SHORT 0x06
#define FC_USHORT 0x07
#define FC_LONG 0x08
#define FC_ULONG 0x09
#define FC_FLOAT 0x0A
#define FC_HYPER 0x0B
#define FC_DOUBLE 0x0C
#define FC_ENUM16 0x0D
#define FC_ENUM32 0x0E
#define FC_IGNORE 0x0F
#define FC_ERROR_STATUS_T 0x10
#define FC_RP 0x11
#define FC_UP 0x12
#define FC_OP 0x13
#define FC_FP 0x14
#define FC_STRUCT 0x15
#define FC_PSTRUCT 0x16
#define FC_CSTRUCT 0x17
#define FC_CPSTRUCT 0x18
#define FC_CVSTRUCT 0x19
#define FC_BOGUS_STRUCT 0x1A
#define FC_CARRAY 0x1B
#define FC_CVARRAY 0x1C
#define FC_SMFARRAY 0x1D
#define FC_LGFARRAY 0x1E
#define FC_SMVARRAY 0x1F
#define FC_LGVARRAY 0x20
#define FC_BOGUS_ARRAY 0x21
#define FC_C_CSTRING 0x22
#define FC_C_BSTRING 0x23
#define FC_C_SSTRING 0x24
#define FC_C_WSTRING 0x25
#define FC_CSTRING 0x26
#define FC_BSTRING 0x27
#define FC_SSTRING 0x28
#define FC_WSTRING 0x29
#define FC_ENCAPSULATED_UNION 0x2A
#define FC_NON_ENCAPSULATED_UNION 0x2B
#define FC_BYTE_COUNT_POINTER 0x2C
#define FC_TRANSMIT_AS 0x2D
#define FC_REPRESENT_AS 0x2E
#define FC_IP 0x2F
#define FC_BIND_CONTEXT 0x30
#define FC_BIND_GENERIC 0x31
#define FC_BIND_PRIMITIVE 0x32
#define FC_AUTO_HANDLE 0x33
#define FC_CALLBACK_HANDLE 0x34
#define FC_UNUSED1 0x35
#define FC_POINTER 0x36
#define FC_ALIGNM2 0x37
#define FC_ALIGNM4 0x38
#define FC_ALIGNM8 0x39
#define FC_UNUSED2 0x3A
#define FC_UNUSED3 0x3B
#define FC_UNUSED4 0x3C
#define FC_STRUCTPAD1 0x3D
#define FC_STRUCTPAD2 0x3E
#define FC_STRUCTPAD3 0x3F
#define FC_STRUCTPAD4 0x40
#define FC_STRUCTPAD5 0x41
#define FC_STRUCTPAD6 0x42
#define FC_STRUCTPAD7 0x43
#define FC_STRING_SIZED 0x44
#define FC_UNUSED5 0x45
#define FC_NO_REPEAT 0x46
#define FC_FIXED_REPEAT 0x47
#define FC_VARIABLE_REPEAT 0x48
#define FC_FIXED_OFFSET 0x49
#define FC_VARIABLE_OFFSET 0x4A
#define FC_PP 0x4B
#define FC_EMBEDDED_COMPLEX 0x4C
#define FC_IN_PARAM 0x4D
#define FC_IN_PARAM_BASETYPE 0x4E
#define FC_IN_PARAM_NO_FREE_INST 0x4F
#define FC_IN_OUT_PARAM 0x50
#define FC_OUT_PARAM 0x51
#define FC_RETURN_PARAM 0x52
#define FC_RETURN_PARAM_BASETYPE 0x53
#define FC_DEREFERENCE 0x54
#define FC_DIV_2 0x55
#define FC_MULT_2 0x56
#define FC_ADD_1 0x57
#define FC_SUB_1 0x58
#define FC_CALLBACK 0x59
#define FC_CONSTANT_IID 0x5A
#define FC_END 0x5B
#define FC_PAD 0x5C
#define FC_SPLIT_DEREFERENCE 0x74
#define FC_SPLIT_DIV_2 0x75
#define FC_SPLIT_MULT_2 0x76
#define FC_SPLIT_ADD_1 0x77
#define FC_SPLIT_SUB_1 0x78
#define FC_SPLIT_CALLBACK 0x79
#define FC_HARD_STRUCT 0xB1
#define FC_TRANSMIT_AS_PTR 0xB2
#define FC_REPRESENT_AS_PTR 0xB3
#define FC_USER_MARSHAL 0xB4
#define FC_PIPE 0xB5
#define FC_BLKHOLE 0xB6
#define FC_RANGE 0xB7
#define FC_INT3264 0xB8
#define FC_UINT3264 0xB9

/* the kinds of conformance and variance descriptors, the high nibble of the first byte. */
#define FC_NORMAL_CONFORMANCE 0x00
#define FC_POINTER_CONFORMANCE 0x10
#define FC_TOP_LEVEL_CONFORMANCE 0x20
#define FC_CONSTANT_CONFORMANCE 0x40
#define FC_TOP_LEVEL_MULTID_CONFORMANCE 0x80

/* correlation flags of a descriptor from the new correlation format. */
#define FC_EARLY_CORRELATION 0x01
#define FC_SPLIT_CORRELATION 0x02
#define FC_IID_CORRELATION 0x04
#define FC_NOCHECK_CORRELATION 0x08

/* pointer attributes, the second byte of a pointer. */
#define FC_ALLOCATE_ALL_NODES 0x01
#define FC_DONT_FREE 0x02
#define FC_ALLOCED_ON_STACK 0x04
#define FC_SIMPLE_POINTER 0x08
#define FC_POINTER_DEREF 0x10

#define OPERATION_MAYBE 0x0001
#define OPERATION_BROADCAST 0x0002
#define OPERATION_IDEMPOTENT 0x0004
#define OPERATION_INPUT_SYNC 0x0008
#define OPERATION_ASYNC 0x0010
#define OPERATION_MESSAGE 0x0020

#define USER_MARSHAL_POINTER 0xC0
#define USER_MARSHAL_UNIQUE 0x80
#define USER_MARSHAL_REF 0x40
#define USER_MARSHAL_IID 0x20

#define HANDLE_PARAM_IS_VIA_PTR 0x80
#define HANDLE_PARAM_IS_IN 0x40
#define HANDLE_PARAM_IS_OUT 0x20
#define HANDLE_PARAM_IS_RETURN 0x10

#define NDR_STRICT_CONTEXT_HANDLE 0x08
#define NDR_CONTEXT_HANDLE_NOSERIALIZE 0x04
#define NDR_CONTEXT_HANDLE_SERIALIZE 0x02
#define NDR_CONTEXT_HANDLE_CANNOT_BE_NULL 0x01

#define Oi_FULL_PTR_USED 0x01
#define Oi_RPCSS_ALLOC_USED 0x02
#define Oi_OBJECT_PROC 0x04
#define Oi_HAS_RPCFLAGS 0x08
#define Oi_IGNORE_OBJECT_EXCEPTION_HANDLING 0x10
#define Oi_HAS_COMM_OR_FAULT 0x20
#define Oi_USE_NEW_INIT_ROUTINES 0x40

#define UNION_CONSECUTIVE_ARMS 1
#define UNION_SMALL_ARMS 2
#define UNION_LARGE_ARMS 3
#define MAGIC_UNION_SHORT 0x8000

#define FC_BIG_PIPE 0x80
#define FC_OBJECT_PIPE 0x40
#define FC_PIPE_HAS_RANGE 0x20

#define PRESENTED_TYPE_IS_ARRAY 0x10
#define PRESENTED_TYPE_ALIGN_4 0x20
#define PRESENTED_TYPE_ALIGN_8 0x40

#define NDR_VERSION_2_0 0x00020000
#define NDR_VERSION_5_0 0x00050000
#define NDR_VERSION_5_2 0x00050002
#define NDR_VERSION_5_4 0x00050004
/* the newest stub version this engine accepts, as the reference does (build 26300). */
#define NDR_STUB_VERSION_MAX 0x000A000C

/* the parameter attributes of an oif procedure, a 16 bit field. */
#define PA_MUST_SIZE 0x0001
#define PA_MUST_FREE 0x0002
#define PA_IS_PIPE 0x0004
#define PA_IS_IN 0x0008
#define PA_IS_OUT 0x0010
#define PA_IS_RETURN 0x0020
#define PA_IS_BASETYPE 0x0040
#define PA_IS_BY_VALUE 0x0080
#define PA_IS_SIMPLE_REF 0x0100
#define PA_DONT_CALL_FREE_INST 0x0200
#define PA_SAVE_FOR_ASYNC_FINISH 0x0400
#define PA_SERVER_ALLOC_SHIFT 13

/* the optimization flags of an oi2 procedure. */
#define OIF_SERVER_MUST_SIZE 0x01
#define OIF_CLIENT_MUST_SIZE 0x02
#define OIF_HAS_RETURN 0x04
#define OIF_HAS_PIPES 0x08
#define OIF_HAS_ASYNC_UUID 0x20
#define OIF_HAS_EXTENSIONS 0x40
#define OIF_HAS_ASYNC_HANDLE 0x80

#define OIF2_HAS_NEW_CORR_DESC 0x01
#define OIF2_CLIENT_CORR_CHECK 0x02
#define OIF2_SERVER_CORR_CHECK 0x04
#define OIF2_HAS_NOTIFY 0x08
#define OIF2_HAS_NOTIFY2 0x10

#endif
