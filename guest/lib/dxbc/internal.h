/* the decoded form of shader model 4 and 5 instructions, internal to the compiler. */
#ifndef MIME_LIB_DXBC_INTERNAL_H
#define MIME_LIB_DXBC_INTERNAL_H

#include "dxbc.h"
#include "tokens.h"

typedef struct DxbcOperand DxbcOperand;

typedef struct DxbcIndex {
    uint32_t immediate;
    /* the register added to the immediate, or NULL. */
    const DxbcOperand *relative;
} DxbcIndex;

struct DxbcOperand {
    uint32_t type;
    /* 0, 1 or 4 components. */
    uint32_t components;
    uint32_t selection;
    uint8_t mask;
    uint8_t swizzle[4];
    uint32_t index_count;
    DxbcIndex index[3];
    uint32_t modifier;
    uint32_t immediate[4];
    uint64_t immediate64[2];
};

#define DXBC_MAX_OPERANDS 8

typedef struct DxbcInstruction {
    uint32_t opcode;
    uint32_t token;
    uint32_t length;
    const uint32_t *words;
    int saturate;
    int nonzero;
    uint32_t precise;
    /* the extended opcode tokens: immediate offsets, the resource dimension and return type. */
    int8_t offset[3];
    uint32_t resource_dimension;
    uint32_t resource_stride;
    uint32_t return_type[4];
    uint32_t operand_count;
    DxbcOperand operands[DXBC_MAX_OPERANDS];
    /* the words after the operands of declarations. */
    const uint32_t *extra;
    uint32_t extra_count;
    /* the operands relative indices point to. */
    DxbcOperand relative[8];
    uint32_t relative_count;
} DxbcInstruction;

typedef struct DxbcReader {
    const uint32_t *words;
    uint32_t count;
    uint32_t at;
} DxbcReader;

/* the next instruction; 0 at the end, -1 on malformed tokens. */
int dxbc_read(DxbcReader *r, DxbcInstruction *ins);

#endif
