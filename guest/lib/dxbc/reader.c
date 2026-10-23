/*
 * decodes shader model 4 and 5 instructions: the opcode token and its
 * extensions, then operands up to the instruction length. declarations
 * keep the words after their operands raw.
 */
#include "internal.h"

static int read_operand(DxbcInstruction *ins, const uint32_t **p, const uint32_t *end, DxbcOperand *op);

static int read_index(DxbcInstruction *ins, const uint32_t **p, const uint32_t *end, uint32_t representation,
                      DxbcIndex *index)
{
    index->immediate = 0;
    index->relative = nullptr;
    switch (representation) {
    case INDEX_IMMEDIATE32:
    case INDEX_IMMEDIATE32_RELATIVE:
        if (*p >= end)
            return -1;
        index->immediate = *(*p)++;
        break;
    case INDEX_IMMEDIATE64:
    case INDEX_IMMEDIATE64_RELATIVE:
        if (end - *p < 2)
            return -1;
        index->immediate = (*p)[0];
        *p += 2;
        break;
    case INDEX_RELATIVE:
        break;
    default:
        return -1;
    }
    if (representation == INDEX_RELATIVE || representation == INDEX_IMMEDIATE32_RELATIVE ||
        representation == INDEX_IMMEDIATE64_RELATIVE) {
        if (ins->relative_count >= 8)
            return -1;
        DxbcOperand *relative = &ins->relative[ins->relative_count++];
        if (read_operand(ins, p, end, relative))
            return -1;
        index->relative = relative;
    }
    return 0;
}

static int read_operand(DxbcInstruction *ins, const uint32_t **p, const uint32_t *end, DxbcOperand *op)
{
    __builtin_memset(op, 0, sizeof(*op));
    if (*p >= end)
        return -1;
    uint32_t token = *(*p)++;
    op->type = OPD_TYPE(token);
    static const uint32_t COMPONENTS[4] = {0, 1, 4, 4};
    op->components = COMPONENTS[OPD_COMPONENTS(token)];
    for (int i = 0; i < 4; i++)
        op->swizzle[i] = (uint8_t)i;
    if (op->components == 4) {
        op->selection = OPD_SELECTION(token);
        switch (op->selection) {
        case SELECT_MASK:
            op->mask = (uint8_t)OPD_MASK(token);
            break;
        case SELECT_SWIZZLE:
            for (int i = 0; i < 4; i++)
                op->swizzle[i] = (uint8_t)((OPD_SWIZZLE(token) >> (2 * i)) & 3);
            op->mask = 0xF;
            break;
        default:
            for (int i = 0; i < 4; i++)
                op->swizzle[i] = (uint8_t)OPD_SELECT1(token);
            op->mask = (uint8_t)(1 << OPD_SELECT1(token));
            break;
        }
    } else if (op->components == 1) {
        op->mask = 1;
        for (int i = 0; i < 4; i++)
            op->swizzle[i] = 0;
    }
    uint32_t extended = OPD_EXTENDED(token);
    while (extended) {
        if (*p >= end)
            return -1;
        uint32_t ext = *(*p)++;
        if ((ext & 0x3F) == 1) {
            op->modifier = (ext >> 6) & 0xFF;
            op->nonuniform = (ext >> 17) & 1;
            op->min_precision = (ext >> 14) & 7;
        }
        extended = ext >> 31;
    }
    op->index_count = OPD_INDEX_DIMENSION(token);
    for (uint32_t i = 0; i < op->index_count; i++)
        if (read_index(ins, p, end, OPD_INDEX_REPRESENTATION(token, i), &op->index[i]))
            return -1;
    if (op->type == OPERAND_IMMEDIATE32) {
        uint32_t n = op->components ? op->components : 1;
        if ((uint32_t)(end - *p) < n)
            return -1;
        for (uint32_t i = 0; i < n; i++)
            op->immediate[i] = (*p)[i];
        if (n == 1)
            op->immediate[1] = op->immediate[2] = op->immediate[3] = op->immediate[0];
        *p += n;
    } else if (op->type == OPERAND_IMMEDIATE64) {
        uint32_t n = op->components == 4 ? 4 : 1;
        if ((uint32_t)(end - *p) < 2 * n)
            return -1;
        for (uint32_t i = 0; i < n && i < 2; i++)
            op->immediate64[i] = (*p)[2 * i] | (uint64_t)(*p)[2 * i + 1] << 32;
        if (n == 1)
            op->immediate64[1] = op->immediate64[0];
        for (uint32_t i = 0; i < 4; i++)
            op->immediate[i] = (uint32_t)(op->immediate64[i / 2] >> (32 * (i & 1)));
        *p += 2 * n;
    }
    return 0;
}

/* operands a declaration has before its raw words. */
static int declaration_operands(uint32_t opcode)
{
    switch (opcode) {
    case OP_DCL_RESOURCE:
    case OP_DCL_CONSTANT_BUFFER:
    case OP_DCL_SAMPLER:
    case OP_DCL_INDEX_RANGE:
    case OP_DCL_INPUT:
    case OP_DCL_INPUT_SGV:
    case OP_DCL_INPUT_SIV:
    case OP_DCL_INPUT_PS:
    case OP_DCL_INPUT_PS_SGV:
    case OP_DCL_INPUT_PS_SIV:
    case OP_DCL_OUTPUT:
    case OP_DCL_OUTPUT_SGV:
    case OP_DCL_OUTPUT_SIV:
    case OP_DCL_STREAM:
    case OP_DCL_UAV_TYPED:
    case OP_DCL_UAV_RAW:
    case OP_DCL_UAV_STRUCTURED:
    case OP_DCL_TGSM_RAW:
    case OP_DCL_TGSM_STRUCTURED:
    case OP_DCL_RESOURCE_RAW:
    case OP_DCL_RESOURCE_STRUCTURED:
        return 1;
    case OP_DCL_GS_OUTPUT_TOPOLOGY:
    case OP_DCL_GS_INPUT_PRIMITIVE:
    case OP_DCL_MAX_OUTPUT_VERTEX_COUNT:
    case OP_DCL_TEMPS:
    case OP_DCL_INDEXABLE_TEMP:
    case OP_DCL_GLOBAL_FLAGS:
    case OP_DCL_FUNCTION_BODY:
    case OP_DCL_FUNCTION_TABLE:
    case OP_DCL_INTERFACE:
    case OP_DCL_INPUT_CONTROL_POINT_COUNT:
    case OP_DCL_OUTPUT_CONTROL_POINT_COUNT:
    case OP_DCL_TESS_DOMAIN:
    case OP_DCL_TESS_PARTITIONING:
    case OP_DCL_TESS_OUTPUT_PRIMITIVE:
    case OP_DCL_HS_MAX_TESSFACTOR:
    case OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT:
    case OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT:
    case OP_DCL_THREAD_GROUP:
    case OP_DCL_GS_INSTANCE_COUNT:
    case OP_HS_DECLS:
    case OP_HS_CONTROL_POINT_PHASE:
    case OP_HS_FORK_PHASE:
    case OP_HS_JOIN_PHASE:
        return 0;
    default:
        return -1;
    }
}

int dxbc_read(DxbcReader *r, DxbcInstruction *ins)
{
    if (r->at >= r->count)
        return 0;
    const uint32_t *start = r->words + r->at;
    uint32_t token = start[0];
    __builtin_memset(ins, 0, sizeof(*ins));
    ins->opcode = TOK_OPCODE(token);
    ins->token = token;
    ins->words = start;
    uint32_t length = TOK_LENGTH(token);
    if (ins->opcode == OP_CUSTOMDATA) {
        if (r->at + 1 >= r->count)
            return -1;
        length = start[1];
    }
    if (!length || length > r->count - r->at)
        return -1;
    ins->length = length;
    r->at += length;
    const uint32_t *p = start + 1, *end = start + length;
    if (ins->opcode == OP_CUSTOMDATA) {
        ins->extra = start + 2;
        ins->extra_count = length - 2;
        return 1;
    }
    ins->saturate = TOK_SATURATE(token);
    ins->nonzero = TOK_TEST_NONZERO(token);
    ins->precise = TOK_PRECISE(token);
    uint32_t extended = TOK_EXTENDED(token);
    while (extended) {
        if (p >= end)
            return -1;
        uint32_t ext = *p++;
        switch (ext & 0x3F) {
        case EXT_SAMPLE_CONTROLS:
            /* 4-bit signed offsets. */
            for (int i = 0; i < 3; i++)
                ins->offset[i] = (int8_t)((int32_t)(ext << (32 - 13 - 4 * i)) >> 28);
            break;
        case EXT_RESOURCE_DIM:
            ins->resource_dimension = (ext >> 6) & 0x1F;
            ins->resource_stride = (ext >> 11) & 0xFFF;
            break;
        case EXT_RESOURCE_RETURN_TYPE:
            for (int i = 0; i < 4; i++)
                ins->return_type[i] = (ext >> (6 + 4 * i)) & 0xF;
            break;
        default:
            break;
        }
        extended = ext >> 31;
    }
    int declared = declaration_operands(ins->opcode);
    if (declared >= 0) {
        for (int i = 0; i < declared; i++)
            if (read_operand(ins, &p, end, &ins->operands[ins->operand_count++]))
                return -1;
        ins->extra = p;
        ins->extra_count = (uint32_t)(end - p);
        return 1;
    }
    while (p < end) {
        if (ins->operand_count >= DXBC_MAX_OPERANDS)
            return -1;
        if (read_operand(ins, &p, end, &ins->operands[ins->operand_count++]))
            return -1;
    }
    return 1;
}
