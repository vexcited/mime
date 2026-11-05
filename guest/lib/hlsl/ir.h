#ifndef MIME_LIB_HLSL_IR_H
#define MIME_LIB_HLSL_IR_H

#include "internal.h"
#include "../dxbc/tokens.h"

enum {
    /* stores src[0] to the destination in store: an output, an indexable temp. */
    IR_STORE = 0x400,
    /* a removed instruction. */
    IR_NOP,
};

enum {
    SRC_NONE,
    SRC_REG,
    SRC_IMM,
    /* constant buffer: v the buffer, index the register, rel a register added to it. */
    SRC_CB,
    /* input register v, component comp; index and rel the vertex of 2d inputs. */
    SRC_IN,
    /* a system value with an operand type of its own, as vThreadID: v the operand type. */
    SRC_SV,
    /* immediate constant buffer: index the entry, rel added. */
    SRC_ICB,
    /* indexable temp v: index the register, rel added. */
    SRC_X,
    /* thread group shared memory and resources, as operands of loads, stores and atomics. */
    SRC_TGSM,
    SRC_RESOURCE,
    SRC_UAV,
    SRC_SAMPLER,
    /* output control points of a hull shader's later phases and the patch constants of a domain shader. */
    SRC_OUT_CP,
    SRC_PATCH,
};

enum { MODIFIER_NEG = 1, MODIFIER_ABS = 2 };

typedef struct IrSrc {
    uint8_t kind;
    uint8_t mod;
    uint8_t comp;
    uint32_t v;
    uint32_t index;
    uint32_t rel;
    uint8_t rel_comp;
} IrSrc;

enum { DST_REG, DST_OUT, DST_X, DST_SPECIAL, DST_TGSM, DST_OUT_CP };

typedef struct IrDst {
    uint8_t kind;
    uint8_t comp;
    uint32_t v;
    uint32_t index;
    uint32_t rel;
    /* the vertex or control point of 2d outputs. */
    uint32_t index2;
    uint32_t rel2;
} IrDst;

enum {
    IRF_SAT = 1,
    /* if, breakc, continuec, retc, discard: test non zero. */
    IRF_NZ = 2,
    IRF_PRECISE = 4,
};

#define IR_MAX_DST 4

/*
 * instructions of a list with the control flow markers of shader model 4,
 * with dxbc's opcodes. values are scalar virtual registers; a vector operand
 * lists its components, which the emitter gathers into one register.
 */
typedef struct IrIns {
    uint16_t op;
    uint8_t flags;
    uint8_t dst_count;
    uint8_t src_count;
    /* per operand, the scalars of a vector operand; zero means each source is its own operand. */
    uint8_t groups[8];
    uint8_t group_count;
    uint32_t dst[IR_MAX_DST];
    IrSrc *src;
    IrDst store;
    /* resource operand: kind SRC_RESOURCE, SRC_UAV, SRC_TGSM or SRC_SAMPLER of id res, with its swizzle. */
    uint8_t res_kind;
    uint32_t res;
    uint32_t res_rel;
    uint8_t res_swizzle[4];
    uint32_t sampler;
    uint32_t sampler_rel;
    int8_t offset[3];
    /* the first result component the swizzle reads, as ld_structured gives several. */
    uint8_t res_first;
    /* opcode specific: sync flags, case values, resinfo modes, stream indices. */
    uint32_t extra;
    /* the result is an int, for resinfo and sample_info's _uint forms. */
    bool uint_result;
} IrIns;

/* a resource the shader names: the variable and the element of an array of them. */
typedef struct IrResource {
    HlVar *var;
    uint32_t element;
    /* shader model 5.1 dynamic indices take the whole range. */
    bool dynamic;
    uint8_t kind;
    uint8_t object;
    uint32_t slot;
    uint32_t space;
    uint32_t count;
    uint32_t id;
    bool used;
    /* the counter of append, consume and structured views. */
    bool counter;
    /* comparison samplers. */
    bool comparison;
} IrResource;

typedef struct IrIndexable {
    uint32_t size;
    uint32_t components;
} IrIndexable;

typedef struct IrTgsm {
    HlVar *var;
    uint32_t stride;
    uint32_t count;
    bool raw;
} IrTgsm;

/* a signature element and the register it got. */
typedef struct IrElement {
    const char *name;
    uint32_t index;
    uint32_t reg;
    uint8_t mask;
    uint8_t used;
    uint32_t sysval;
    uint32_t component_type;
    uint32_t interp;
    uint32_t stream;
    /* the operand type of values with their own registers, as oDepth, or 0. */
    uint32_t operand;
    /* arrays take a register per element. */
    uint32_t rows;
    uint8_t start;
} IrElement;

typedef struct IrSignature {
    IrElement *elements;
    uint32_t count, capacity;
} IrSignature;

typedef struct IrCbuffer {
    HlBuffer *buffer;
    uint32_t slot;
    uint32_t space;
    uint32_t size;
    bool used;
    bool dynamic;
    /* the highest register read plus one. */
    uint32_t extent;
} IrCbuffer;

typedef struct IrShader {
    Hlsl *h;
    HlProgram *prog;
    HlProfile profile;
    IrIns *ins;
    uint32_t count, capacity;
    uint32_t vregs;
    IrResource *resources;
    uint32_t resource_count, resource_capacity;
    IrIndexable *indexables;
    uint32_t indexable_count, indexable_capacity;
    IrTgsm *tgsm;
    uint32_t tgsm_count, tgsm_capacity;
    uint32_t *icb;
    uint32_t icb_count, icb_capacity;
    IrCbuffer *cbuffers;
    uint32_t cbuffer_count;
    IrSignature inputs, outputs, patch_constants;
    /* system value operands the shader reads, by operand type: the mask read. */
    uint8_t sv_mask[64];
    uint32_t thread_group[3];
    /* geometry shaders. */
    uint32_t gs_input_primitive, gs_output_topology, gs_max_vertices, gs_instances;
    uint32_t gs_stream_mask;
    /* hull and domain shaders. */
    uint32_t input_control_points, output_control_points;
    uint32_t tess_domain, tess_partitioning, tess_output_primitive;
    float max_tess_factor;
    /* hull shaders run the control point code, then the patch constant code as a fork phase. */
    uint32_t fork_start;
    bool uses_discard;
    bool early_depth;
    bool uses_doubles;
    bool refactoring_allowed;
    bool row_major_default;
    uint32_t uav_count;
    /* statistics for STAT. */
    uint32_t temp_count;
} IrShader;

static inline IrSrc ir_reg(uint32_t v) { return (IrSrc){.kind = SRC_REG, .v = v}; }
static inline IrSrc ir_imm(uint32_t bits) { return (IrSrc){.kind = SRC_IMM, .v = bits}; }
static inline IrSrc ir_immf(float f)
{
    union {
        float f;
        uint32_t u;
    } c = {.f = f};
    return ir_imm(c.u);
}
static inline bool ir_is_imm(const IrSrc *s) { return s->kind == SRC_IMM && !s->mod; }

uint32_t ir_new_vreg(IrShader *s);
IrIns *ir_emit(IrShader *s, uint16_t op, uint32_t dst_count, uint32_t src_count);
const char *ir_op_name(uint32_t op);
void ir_dump(IrShader *s, HlBuf *out);

/* lowering from the checked program; 0 on success. */
int ir_lower(IrShader *s, HlFunc *entry);
/* constant folding, copy propagation and dead code elimination. */
void ir_optimize(IrShader *s);
/* register allocation and the shader model 4 and 5 bytecode with its declarations. */
int ir_emit_sm4(IrShader *s, HlBuf *code);
/* the container with RDEF, the signatures, SHEX and STAT. */
int ir_container(IrShader *s, const HlBuf *code, HlBuf *out);

#endif
