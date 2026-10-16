/*
 * the llvm 3.7 module of a dxil part, as the compiler reads it: types,
 * values numbered as the bitcode numbers them, functions of blocks of
 * instructions, and metadata.
 */
#ifndef MIME_LIB_DXIL_IR_H
#define MIME_LIB_DXIL_IR_H

#include "bitcode.h"

#define DX_NONE 0xFFFFFFFFu

typedef enum DxTypeKind {
    DT_VOID,
    DT_INT,
    DT_HALF,
    DT_FLOAT,
    DT_DOUBLE,
    DT_POINTER,
    DT_STRUCT,
    DT_ARRAY,
    DT_VECTOR,
    DT_FUNCTION,
    DT_LABEL,
    DT_METADATA,
    DT_OPAQUE,
} DxTypeKind;

typedef struct DxType {
    uint8_t kind;
    /* integers: the bit width; pointers: the address space. */
    uint32_t width;
    /* the pointee, the element, or a function's return type. */
    uint32_t element;
    /* array and vector lengths, struct members and function parameters. */
    uint32_t count;
    uint32_t *members;
    const char *name;
} DxType;

typedef enum DxValueKind {
    DV_GLOBAL,
    DV_FUNCTION,
    DV_INT,
    DV_FLOAT,
    DV_NULL,
    DV_UNDEF,
    DV_AGGREGATE,
    DV_EXPR,
    DV_ARGUMENT,
    DV_INSTRUCTION,
} DxValueKind;

/* llvm's opcodes of constant expressions and instructions. */
enum {
    DX_CAST_TRUNC,
    DX_CAST_ZEXT,
    DX_CAST_SEXT,
    DX_CAST_FPTOUI,
    DX_CAST_FPTOSI,
    DX_CAST_UITOFP,
    DX_CAST_SITOFP,
    DX_CAST_FPTRUNC,
    DX_CAST_FPEXT,
    DX_CAST_PTRTOINT,
    DX_CAST_INTTOPTR,
    DX_CAST_BITCAST,
    DX_CAST_ADDRSPACECAST,
};

enum {
    DX_BIN_ADD,
    DX_BIN_SUB,
    DX_BIN_MUL,
    DX_BIN_UDIV,
    DX_BIN_SDIV,
    DX_BIN_UREM,
    DX_BIN_SREM,
    DX_BIN_SHL,
    DX_BIN_LSHR,
    DX_BIN_ASHR,
    DX_BIN_AND,
    DX_BIN_OR,
    DX_BIN_XOR,
};

/* compare predicates: floating point 0 to 15, integers from 32. */
enum {
    DX_FCMP_FALSE,
    DX_FCMP_OEQ,
    DX_FCMP_OGT,
    DX_FCMP_OGE,
    DX_FCMP_OLT,
    DX_FCMP_OLE,
    DX_FCMP_ONE,
    DX_FCMP_ORD,
    DX_FCMP_UNO,
    DX_FCMP_UEQ,
    DX_FCMP_UGT,
    DX_FCMP_UGE,
    DX_FCMP_ULT,
    DX_FCMP_ULE,
    DX_FCMP_UNE,
    DX_FCMP_TRUE,
    DX_ICMP_EQ = 32,
    DX_ICMP_NE,
    DX_ICMP_UGT,
    DX_ICMP_UGE,
    DX_ICMP_ULT,
    DX_ICMP_ULE,
    DX_ICMP_SGT,
    DX_ICMP_SGE,
    DX_ICMP_SLT,
    DX_ICMP_SLE,
};

enum {
    DX_RMW_XCHG,
    DX_RMW_ADD,
    DX_RMW_SUB,
    DX_RMW_AND,
    DX_RMW_NAND,
    DX_RMW_OR,
    DX_RMW_XOR,
    DX_RMW_MAX,
    DX_RMW_MIN,
    DX_RMW_UMAX,
    DX_RMW_UMIN,
};

/* the kinds of constant expressions kept: casts and element pointers. */
enum { DX_EXPR_CAST, DX_EXPR_GEP, DX_EXPR_BINOP };

typedef struct DxValue {
    uint8_t kind;
    /* the opcode of an expression, or whether a global is constant. */
    uint8_t sub;
    uint32_t type;
    /* integers, and floats by their bits. */
    uint64_t bits;
    /* the elements of aggregates, the operands of expressions, a global's initializer as ops[0]. */
    uint32_t *ops;
    uint32_t count;
    /* constant data arrays and strings: the elements' bits, count of them. */
    uint64_t *data;
    /* instructions: their index in the function. */
    uint32_t inst;
    const char *name;
} DxValue;

typedef enum DxOpcode {
    DI_BINOP,
    DI_CAST,
    DI_GEP,
    DI_SELECT,
    DI_EXTRACTELT,
    DI_INSERTELT,
    DI_SHUFFLEVEC,
    DI_CMP,
    DI_RET,
    DI_BR,
    DI_SWITCH,
    DI_UNREACHABLE,
    DI_PHI,
    DI_ALLOCA,
    DI_LOAD,
    DI_STORE,
    DI_EXTRACTVAL,
    DI_INSERTVAL,
    DI_CALL,
    DI_ATOMICRMW,
    DI_CMPXCHG,
} DxOpcode;

/*
 * an instruction. ops are absolute value ids, except branches and
 * switches, which hold block indices where llvm has blocks, and phis,
 * which alternate values and blocks.
 *   binop: a, b; sub the opcode.        cast: value; sub the opcode.
 *   gep: base, indices.                 select: condition, true, false.
 *   cmp: a, b; sub the predicate.       ret: nothing or the value.
 *   br: target, or true, false, cond.   switch: condition, default, then value and block pairs.
 *   phi: value, block pairs.            alloca: the count; type the pointer.
 *   load: pointer.                      store: pointer, value.
 *   extractval: aggregate, indices as literal numbers. insertval: aggregate, value, indices.
 *   call: callee, arguments.            atomicrmw: pointer, value; sub the operation.
 *   cmpxchg: pointer, compared, new.
 */
typedef struct DxInst {
    uint8_t op;
    uint8_t sub;
    uint16_t flags;
    uint32_t type;
    uint32_t value;
    uint32_t block;
    uint32_t count;
    uint32_t *ops;
} DxInst;

enum { DX_INST_INBOUNDS = 1, DX_INST_VOLATILE = 2, DX_INST_PRECISE = 4 };

typedef struct DxBlock {
    uint32_t first, count;
} DxBlock;

typedef struct DxFunction {
    uint32_t type;
    uint32_t value;
    int prototype;
    const char *name;
    DxBlock *blocks;
    uint32_t block_count;
    DxInst *insts;
    uint32_t inst_count;
    /* the values after the module's: arguments, constants, then instruction results. */
    DxValue *values;
    uint32_t value_count;
} DxFunction;

typedef enum DxMetadataKind { DM_NULL, DM_STRING, DM_VALUE, DM_NODE } DxMetadataKind;

typedef struct DxMetadata {
    uint8_t kind;
    const char *string;
    /* values: the value id and its type. */
    uint32_t value;
    uint32_t type;
    /* nodes: metadata ids, DX_NONE for null operands. */
    uint32_t *ops;
    uint32_t count;
} DxMetadata;

typedef struct DxNamedMetadata {
    const char *name;
    uint32_t *nodes;
    uint32_t count;
} DxNamedMetadata;

typedef struct DxModule {
    BcArena arena;
    DxType *types;
    uint32_t type_count, type_capacity;
    DxValue *values;
    uint32_t value_count;
    DxFunction *functions;
    uint32_t function_count;
    DxMetadata *metadata;
    uint32_t metadata_count;
    DxNamedMetadata *named;
    uint32_t named_count;
    char error[96];
} DxModule;

int dx_module_read(DxModule *m, const uint8_t *bitcode, size_t size, const MimeAllocator *alloc);
void dx_module_free(DxModule *m);

/* a value of the module, or of the function past the module's. */
const DxValue *dx_value(const DxModule *m, const DxFunction *f, uint32_t id);
/* a pointer to element in the address space, made when the module has none. */
uint32_t dx_pointer_type(DxModule *m, uint32_t element, uint32_t space);
const DxNamedMetadata *dx_named(const DxModule *m, const char *name);
/* a node's operand, or NULL when null or out of range. */
const DxMetadata *dx_md_op(const DxModule *m, const DxMetadata *node, uint32_t index);
/* an integer constant a metadata operand holds, or fallback. */
uint64_t dx_md_int(const DxModule *m, const DxMetadata *node, uint32_t index, uint64_t fallback);
const char *dx_md_string(const DxModule *m, const DxMetadata *node, uint32_t index);

#endif
